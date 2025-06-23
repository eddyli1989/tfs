#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>

#define NUM_PROCESSES 5
#define NUM_THREADS 3
#define NUM_ITERATIONS 20
#define PAGE_SIZE 4096
#define TEST_FILE_TEMPLATE "%s/concurrent_test_%d.txt"
#define MAX_FILENAME_LEN 256

// 线程参数结构
typedef struct {
    int thread_id;
    int process_id;
    char base_path[MAX_FILENAME_LEN];
} thread_args_t;

// 获取当前时间的毫秒表示
long long current_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// 线程函数：执行文件操作
void* file_operations_thread(void* arg) {
    thread_args_t* args = (thread_args_t*)arg;
    char filename[MAX_FILENAME_LEN];
    char buffer[PAGE_SIZE];
    int i, fd;
    
    // 构建唯一的文件名
    snprintf(filename, sizeof(filename), TEST_FILE_TEMPLATE, 
             args->base_path, args->thread_id + args->process_id * NUM_THREADS);
    
    printf("[P%d-T%d] Thread started, operating on file: %s\n", 
           args->process_id, args->thread_id, filename);
    
    // 执行多次迭代的文件操作
    for (i = 0; i < NUM_ITERATIONS; i++) {
        // 生成唯一的测试数据
        snprintf(buffer, sizeof(buffer), 
                 "Process %d, Thread %d, Iteration %d, Time %lld", 
                 args->process_id, args->thread_id, i, current_time_ms());
        
        // 写入操作
        fd = open(filename, O_RDWR | O_CREAT, 0644);
        if (fd == -1) {
            perror("open for write failed");
            continue;
        }
        
        if (write(fd, buffer, strlen(buffer)) == -1) {
            perror("write failed");
            close(fd);
            continue;
        }
        
        // 确保文件大小至少为一页
        if (ftruncate(fd, PAGE_SIZE) == -1) {
            perror("ftruncate failed");
        }
        
        close(fd);
        
        // 随机延迟，增加并发冲突的可能性
        usleep(rand() % 10000);
        
        // 内存映射操作
        fd = open(filename, O_RDWR);
        if (fd == -1) {
            perror("open for mmap failed");
            continue;
        }
        
        char* mapped_data = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mapped_data == MAP_FAILED) {
            perror("mmap failed");
            close(fd);
            continue;
        }
        
        // 验证数据
        if (strncmp(mapped_data, buffer, strlen(buffer)) != 0) {
            printf("[P%d-T%d] Data verification failed in iteration %d!\n", 
                   args->process_id, args->thread_id, i);
            printf("  Expected: %s\n", buffer);
            printf("  Got: %s\n", mapped_data);
        }
        
        // 修改映射的数据
        snprintf(mapped_data, PAGE_SIZE, 
                 "MMAP: Process %d, Thread %d, Iteration %d, Time %lld", 
                 args->process_id, args->thread_id, i, current_time_ms());
        
        // 同步到磁盘
        if (msync(mapped_data, PAGE_SIZE, MS_SYNC) == -1) {
            perror("msync failed");
        }
        
        // 解除映射
        if (munmap(mapped_data, PAGE_SIZE) == -1) {
            perror("munmap failed");
        }
        
        close(fd);
        
        // 再次随机延迟
        usleep(rand() % 10000);
        
        // 读取并验证修改后的数据
        fd = open(filename, O_RDONLY);
        if (fd == -1) {
            perror("open for read failed");
            continue;
        }
        
        memset(buffer, 0, sizeof(buffer));
        if (read(fd, buffer, sizeof(buffer) - 1) == -1) {
            perror("read failed");
            close(fd);
            continue;
        }
        
        close(fd);
        
        // 每5次迭代报告一次进度
        if (i % 5 == 0) {
            printf("[P%d-T%d] Completed %d iterations\n", 
                   args->process_id, args->thread_id, i);
        }
    }
    
    printf("[P%d-T%d] Thread completed all %d iterations\n", 
           args->process_id, args->thread_id, NUM_ITERATIONS);
    return NULL;
}

// 进程函数：创建多个线程
void process_function(int process_id, const char* base_path) {
    pthread_t threads[NUM_THREADS];
    thread_args_t thread_args[NUM_THREADS];
    int i;
    
    printf("[P%d] Process started\n", process_id);
    
    // 初始化随机数生成器
    srand(time(NULL) + process_id);
    
    // 创建多个线程
    for (i = 0; i < NUM_THREADS; i++) {
        thread_args[i].thread_id = i;
        thread_args[i].process_id = process_id;
        strncpy(thread_args[i].base_path, base_path, MAX_FILENAME_LEN - 1);
        thread_args[i].base_path[MAX_FILENAME_LEN - 1] = '\0';
        
        if (pthread_create(&threads[i], NULL, file_operations_thread, &thread_args[i]) != 0) {
            perror("pthread_create failed");
            exit(EXIT_FAILURE);
        }
    }
    
    // 等待所有线程完成
    for (i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("[P%d] Process completed\n", process_id);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <base_path>\n", argv[0]);
        return 1;
    }
    
    const char* base_path = argv[1];
    pid_t pids[NUM_PROCESSES];
    int i, status;
    
    printf("Starting concurrent test with %d processes, each with %d threads\n", 
           NUM_PROCESSES, NUM_THREADS);
    printf("Each thread will perform %d iterations of file operations\n", NUM_ITERATIONS);
    printf("Base path for test files: %s\n", base_path);
    
    // 创建多个进程
    for (i = 0; i < NUM_PROCESSES; i++) {
        pids[i] = fork();
        
        if (pids[i] < 0) {
            perror("fork failed");
            exit(EXIT_FAILURE);
        } else if (pids[i] == 0) {
            // 子进程
            process_function(i, base_path);
            exit(EXIT_SUCCESS);
        }
    }
    
    // 父进程等待所有子进程完成
    for (i = 0; i < NUM_PROCESSES; i++) {
        waitpid(pids[i], &status, 0);
        if (WIFEXITED(status)) {
            printf("Process %d exited with status %d\n", i, WEXITSTATUS(status));
        } else {
            printf("Process %d terminated abnormally\n", i);
        }
    }
    
    printf("All processes completed. Concurrent test finished.\n");
    return 0;
}