#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>

#define PAGE_SIZE 4096
#define TEST_STRING "This is a test string for memory mapping verification"
#define TEST_ITERATIONS 10

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file_path>\n", argv[0]);
        return 1;
    }
    
    const char *file_path = argv[1];
    int fd, i;
    char *mapped_data;
    struct stat st;
    
    printf("Starting comprehensive mmap test on %s\n", file_path);
    
    // 创建测试文件并写入初始数据
    fd = open(file_path, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        perror("Failed to open file");
        return 1;
    }
    
    // 确保文件大小至少为一页
    if (ftruncate(fd, PAGE_SIZE) == -1) {
        perror("Failed to set file size");
        close(fd);
        return 1;
    }
    
    printf("File created and sized to %d bytes\n", PAGE_SIZE);
    
    // 测试1: 基本内存映射和读写
    printf("\nTest 1: Basic memory mapping and read/write\n");
    mapped_data = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_data == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }
    
    // 写入测试字符串
    strcpy(mapped_data, TEST_STRING);
    printf("Written to mapped memory: %s\n", TEST_STRING);
    
    // 确保数据写入磁盘
    if (msync(mapped_data, PAGE_SIZE, MS_SYNC) == -1) {
        perror("msync failed");
    }
    
    // 解除映射
    if (munmap(mapped_data, PAGE_SIZE) == -1) {
        perror("munmap failed");
        close(fd);
        return 1;
    }
    
    // 关闭并重新打开文件以验证数据持久性
    close(fd);
    fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        perror("Failed to reopen file");
        return 1;
    }
    
    // 重新映射以读取
    mapped_data = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    if (mapped_data == MAP_FAILED) {
        perror("mmap failed on reopen");
        close(fd);
        return 1;
    }
    
    // 验证数据
    if (strncmp(mapped_data, TEST_STRING, strlen(TEST_STRING)) != 0) {
        fprintf(stderr, "Data verification failed!\n");
        fprintf(stderr, "Expected: %s\n", TEST_STRING);
        fprintf(stderr, "Got: %s\n", mapped_data);
        munmap(mapped_data, PAGE_SIZE);
        close(fd);
        return 1;
    }
    
    printf("Data verification successful\n");
    munmap(mapped_data, PAGE_SIZE);
    close(fd);
    
    // 测试2: 多次映射/解除映射循环
    printf("\nTest 2: Multiple map/unmap cycles\n");
    for (i = 0; i < TEST_ITERATIONS; i++) {
        char test_data[64];
        snprintf(test_data, sizeof(test_data), "Iteration %d: %ld", i, random());
        
        fd = open(file_path, O_RDWR);
        if (fd == -1) {
            perror("Failed to open file in iteration");
            return 1;
        }
        
        mapped_data = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mapped_data == MAP_FAILED) {
            perror("mmap failed in iteration");
            close(fd);
            return 1;
        }
        
        // 写入此迭代的数据
        strcpy(mapped_data, test_data);
        msync(mapped_data, PAGE_SIZE, MS_SYNC);
        
        // 解除映射并关闭
        munmap(mapped_data, PAGE_SIZE);
        close(fd);
        
        // 重新打开并验证
        fd = open(file_path, O_RDONLY);
        if (fd == -1) {
            perror("Failed to reopen file for verification");
            return 1;
        }
        
        mapped_data = mmap(NULL, PAGE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
        if (mapped_data == MAP_FAILED) {
            perror("mmap failed during verification");
            close(fd);
            return 1;
        }
        
        if (strncmp(mapped_data, test_data, strlen(test_data)) != 0) {
            fprintf(stderr, "Iteration %d: Data verification failed!\n", i);
            munmap(mapped_data, PAGE_SIZE);
            close(fd);
            return 1;
        }
        
        munmap(mapped_data, PAGE_SIZE);
        close(fd);
        printf("Iteration %d: Passed\n", i);
    }
    
    // 测试3: 边界条件测试
    printf("\nTest 3: Edge case testing\n");
    
    // 尝试映射0字节
    printf("Testing mapping 0 bytes (should fail gracefully)...\n");
    fd = open(file_path, O_RDWR);
    if (fd != -1) {
        mapped_data = mmap(NULL, 0, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mapped_data != MAP_FAILED) {
            fprintf(stderr, "mmap with 0 size unexpectedly succeeded\n");
            munmap(mapped_data, 0);
        } else {
            printf("mmap with 0 size failed as expected: %s\n", strerror(errno));
        }
        close(fd);
    }
    
    // 尝试映射超大偏移量
    printf("Testing mapping with large offset (should fail gracefully)...\n");
    fd = open(file_path, O_RDWR);
    if (fd != -1) {
        mapped_data = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 1000000);
        if (mapped_data != MAP_FAILED) {
            fprintf(stderr, "mmap with large offset unexpectedly succeeded\n");
            munmap(mapped_data, PAGE_SIZE);
        } else {
            printf("mmap with large offset failed as expected: %s\n", strerror(errno));
        }
        close(fd);
    }
    
    printf("\nAll memory mapping tests completed successfully!\n");
    return 0;
}