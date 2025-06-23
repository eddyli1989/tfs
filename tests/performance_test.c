#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>

// 测试配置
#define MIN_FILE_SIZE (4 * 1024)        // 4KB
#define MAX_FILE_SIZE (4 * 1024 * 1024) // 4MB
#define BLOCK_SIZE 4096                 // 4KB块大小
#define NUM_ITERATIONS 5                // 每个测试重复次数
#define RANDOM_OPS 1000                 // 随机操作次数

// 测试类型
typedef enum {
    TEST_WRITE_SEQ,
    TEST_READ_SEQ,
    TEST_WRITE_RANDOM,
    TEST_READ_RANDOM,
    TEST_MMAP_WRITE_SEQ,
    TEST_MMAP_READ_SEQ,
    TEST_MMAP_WRITE_RANDOM,
    TEST_MMAP_READ_RANDOM
} test_type_t;

// 获取当前时间（微秒）
long long get_time_usec() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

// 创建测试数据
void create_test_data(char* buffer, size_t size) {
    size_t i;
    for (i = 0; i < size; i++) {
        buffer[i] = 'A' + (i % 26);
    }
}

// 执行顺序写入测试
double test_sequential_write(const char* filename, size_t file_size) {
    int fd;
    char* buffer;
    size_t bytes_written = 0;
    long long start_time, end_time;
    ssize_t write_result;
    
    // 分配缓冲区
    buffer = (char*)malloc(BLOCK_SIZE);
    if (!buffer) {
        perror("malloc failed");
        return -1.0;
    }
    
    create_test_data(buffer, BLOCK_SIZE);
    
    // 打开文件
    fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open failed");
        free(buffer);
        return -1.0;
    }
    
    // 开始计时
    start_time = get_time_usec();
    
    // 顺序写入
    while (bytes_written < file_size) {
        write_result = write(fd, buffer, BLOCK_SIZE);
        if (write_result == -1) {
            perror("write failed");
            close(fd);
            free(buffer);
            return -1.0;
        }
        bytes_written += write_result;
    }
    
    // 确保数据写入磁盘
    fsync(fd);
    
    // 结束计时
    end_time = get_time_usec();
    
    close(fd);
    free(buffer);
    
    // 计算吞吐量（MB/s）
    double elapsed_sec = (end_time - start_time) / 1000000.0;
    double throughput = (file_size / (1024.0 * 1024.0)) / elapsed_sec;
    
    return throughput;
}

// 执行顺序读取测试
double test_sequential_read(const char* filename, size_t file_size) {
    int fd;
    char* buffer;
    size_t bytes_read = 0;
    long long start_time, end_time;
    ssize_t read_result;
    
    // 分配缓冲区
    buffer = (char*)malloc(BLOCK_SIZE);
    if (!buffer) {
        perror("malloc failed");
        return -1.0;
    }
    
    // 打开文件
    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("open failed");
        free(buffer);
        return -1.0;
    }
    
    // 开始计时
    start_time = get_time_usec();
    
    // 顺序读取
    while (bytes_read < file_size) {
        read_result = read(fd, buffer, BLOCK_SIZE);
        if (read_result == -1) {
            perror("read failed");
            close(fd);
            free(buffer);
            return -1.0;
        }
        if (read_result == 0) {
            // 文件结束
            break;
        }
        bytes_read += read_result;
    }
    
    // 结束计时
    end_time = get_time_usec();
    
    close(fd);
    free(buffer);
    
    // 计算吞吐量（MB/s）
    double elapsed_sec = (end_time - start_time) / 1000000.0;
    double throughput = (bytes_read / (1024.0 * 1024.0)) / elapsed_sec;
    
    return throughput;
}

// 执行随机写入测试
double test_random_write(const char* filename, size_t file_size) {
    int fd;
    char* buffer;
    long long start_time, end_time;
    int i;
    off_t offset;
    
    // 分配缓冲区
    buffer = (char*)malloc(BLOCK_SIZE);
    if (!buffer) {
        perror("malloc failed");
        return -1.0;
    }
    
    create_test_data(buffer, BLOCK_SIZE);
    
    // 打开文件
    fd = open(filename, O_WRONLY | O_CREAT, 0644);
    if (fd == -1) {
        perror("open failed");
        free(buffer);
        return -1.0;
    }
    
    // 确保文件大小
    if (ftruncate(fd, file_size) == -1) {
        perror("ftruncate failed");
        close(fd);
        free(buffer);
        return -1.0;
    }
    
    // 开始计时
    start_time = get_time_usec();
    
    // 随机写入
    for (i = 0; i < RANDOM_OPS; i++) {
        // 生成随机偏移量（按块对齐）
        offset = (rand() % (file_size / BLOCK_SIZE)) * BLOCK_SIZE;
        
        // 定位到随机位置
        if (lseek(fd, offset, SEEK_SET) == -1) {
            perror("lseek failed");
            close(fd);
            free(buffer);
            return -1.0;
        }
        
        // 写入数据
        if (write(fd, buffer, BLOCK_SIZE) == -1) {
            perror("write failed");
            close(fd);
            free(buffer);
            return -1.0;
        }
    }
    
    // 确保数据写入磁盘
    fsync(fd);
    
    // 结束计时
    end_time = get_time_usec();
    
    close(fd);
    free(buffer);
    
    // 计算IOPS（每秒操作数）
    double elapsed_sec = (end_time - start_time) / 1000000.0;
    double iops = RANDOM_OPS / elapsed_sec;
    
    return iops;
}

// 执行随机读取测试
double test_random_read(const char* filename, size_t file_size) {
    int fd;
    char* buffer;
    long long start_time, end_time;
    int i;
    off_t offset;
    
    // 分配缓冲区
    buffer = (char*)malloc(BLOCK_SIZE);
    if (!buffer) {
        perror("malloc failed");
        return -1.0;
    }
    
    // 打开文件
    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("open failed");
        free(buffer);
        return -1.0;
    }
    
    // 开始计时
    start_time = get_time_usec();
    
    // 随机读取
    for (i = 0; i < RANDOM_OPS; i++) {
        // 生成随机偏移量（按块对齐）
        offset = (rand() % (file_size / BLOCK_SIZE)) * BLOCK_SIZE;
        
        // 定位到随机位置
        if (lseek(fd, offset, SEEK_SET) == -1) {
            perror("lseek failed");
            close(fd);
            free(buffer);
            return -1.0;
        }
        
        // 读取数据
        if (read(fd, buffer, BLOCK_SIZE) == -1) {
            perror("read failed");
            close(fd);
            free(buffer);
            return -1.0;
        }
    }
    
    // 结束计时
    end_time = get_time_usec();
    
    close(fd);
    free(buffer);
    
    // 计算IOPS（每秒操作数）
    double elapsed_sec = (end_time - start_time) / 1000000.0;
    double iops = RANDOM_OPS / elapsed_sec;
    
    return iops;
}

// 执行顺序内存映射写入测试
double test_mmap_sequential_write(const char* filename, size_t file_size) {
    int fd;
    char* mapped_data;
    long long start_time, end_time;
    size_t offset;
    
    // 打开文件
    fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("open failed");
        return -1.0;
    }
    
    // 确保文件大小
    if (ftruncate(fd, file_size) == -1) {
        perror("ftruncate failed");
        close(fd);
        return -1.0;
    }
    
    // 映射文件
    mapped_data = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_data == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return -1.0;
    }
    
    // 开始计时
    start_time = get_time_usec();
    
    // 顺序写入
    for (offset = 0; offset < file_size; offset += BLOCK_SIZE) {
        size_t bytes_to_write = (offset + BLOCK_SIZE <= file_size) ? BLOCK_SIZE : (file_size - offset);
        memset(mapped_data + offset, 'A' + (offset % 26), bytes_to_write);
    }
    
    // 确保数据写入磁盘
    if (msync(mapped_data, file_size, MS_SYNC) == -1) {
        perror("msync failed");
    }
    
    // 结束计时
    end_time = get_time_usec();
    
    // 解除映射
    munmap(mapped_data, file_size);
    close(fd);
    
    // 计算吞吐量（MB/s）
    double elapsed_sec = (end_time - start_time) / 1000000.0;
    double throughput = (file_size / (1024.0 * 1024.0)) / elapsed_sec;
    
    return throughput;
}

// 执行顺序内存映射读取测试
double test_mmap_sequential_read(const char* filename, size_t file_size) {
    int fd;
    char* mapped_data;
    long long start_time, end_time;
    size_t offset;
    volatile char dummy = 0; // 防止编译器优化
    
    // 打开文件
    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("open failed");
        return -1.0;
    }
    
    // 映射文件
    mapped_data = mmap(NULL, file_size, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped_data == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return -1.0;
    }
    
    // 开始计时
    start_time = get_time_usec();
    
    // 顺序读取
    for (offset = 0; offset < file_size; offset += BLOCK_SIZE) {
        size_t bytes_to_read = (offset + BLOCK_SIZE <= file_size) ? BLOCK_SIZE : (file_size - offset);
        size_t i;
        for (i = 0; i < bytes_to_read; i++) {
            dummy += mapped_data[offset + i];
        }
    }
    
    // 结束计时
    end_time = get_time_usec();
    
    // 解除映射
    munmap(mapped_data, file_size);
    close(fd);
    
    // 计算吞吐量（MB/s）
    double elapsed_sec = (end_time - start_time) / 1000000.0;
    double throughput = (file_size / (1024.0 * 1024.0)) / elapsed_sec;
    
    return throughput;
}

// 执行随机内存映射写入测试
double test_mmap_random_write(const char* filename, size_t file_size) {
    int fd;
    char* mapped_data;
    long long start_time, end_time;
    int i;
    
    // 打开文件
    fd = open(filename, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        perror("open failed");
        return -1.0;
    }
    
    // 确保文件大小
    if (ftruncate(fd, file_size) == -1) {
        perror("ftruncate failed");
        close(fd);
        return -1.0;
    }
    
    // 映射文件
    mapped_data = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_data == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return -1