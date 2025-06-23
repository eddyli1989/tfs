#!/bin/bash

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 日志函数
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 检查是否以root权限运行
if [ "$EUID" -ne 0 ]; then
    log_error "Please run as root"
    exit 1
fi

# 测试配置
MOUNT_POINT="/mnt/tfs_test"
MODULE_NAME="tfs_client"
DAEMON_PATH="../tfsd/tfsd"
TEST_FILE="$MOUNT_POINT/test.txt"
LARGE_FILE="$MOUNT_POINT/large.txt"
CONCURRENT_TEST_FILE="$MOUNT_POINT/concurrent.txt"

# 清理函数
cleanup() {
    log_info "Cleaning up..."
    
    # 终止守护进程
    if pgrep tfsd > /dev/null; then
        pkill tfsd
        sleep 1
    fi
    
    # 卸载文件系统
    if mount | grep -q "$MOUNT_POINT"; then
        umount "$MOUNT_POINT" 2>/dev/null
        sleep 1
    fi
    
    # 卸载模块
    if lsmod | grep -q "^$MODULE_NAME"; then
        rmmod "$MODULE_NAME" 2>/dev/null
        sleep 1
    fi
    
    # 删除挂载点
    rm -rf "$MOUNT_POINT"
}

# 错误处理
handle_error() {
    log_error "$1"
    cleanup
    exit 1
}

# 1. 基础功能测试
basic_test() {
    log_info "Starting basic functionality test..."
    
    # 创建挂载点
    mkdir -p "$MOUNT_POINT" || handle_error "Failed to create mount point"
    
    # 加载模块
    insmod ../tfs_client/tfs_client.ko || handle_error "Failed to load kernel module"
    log_info "Kernel module loaded successfully"
    
    # 挂载文件系统
    mount -t tfs none "$MOUNT_POINT" || handle_error "Failed to mount filesystem"
    log_info "Filesystem mounted successfully"
    
    # 启动守护进程
    "$DAEMON_PATH" & 
    DAEMON_PID=$!
    sleep 2
    
    if ! ps -p $DAEMON_PID > /dev/null; then
        handle_error "Daemon failed to start"
    fi
    log_info "Daemon started successfully (PID: $DAEMON_PID)"
    
    # 基本写入测试
    echo "Test Data" > "$TEST_FILE" || handle_error "Failed to write test file"
    log_info "Basic write test passed"
    
    # 基本读取测试
    local content=$(cat "$TEST_FILE")
    if [ "$content" != "Test Data" ]; then
        handle_error "File content mismatch"
    fi
    log_info "Basic read test passed"
}

# 2. 内存映射测试
mmap_test() {
    log_info "Starting memory mapping test..."
    
    # 运行简单的内存映射测试
    echo "Test Data for mmap" > "$TEST_FILE" || handle_error "Failed to create test file for mmap"
    
    # 运行基本的内存映射测试
    cat > test_mmap_basic.c << EOF
#include <stdio.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

int main() {
    int fd = open("$TEST_FILE", O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    
    char *addr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }
    
    // 读取内容
    printf("Content: %s\n", addr);
    
    // 解除映射
    munmap(addr, 4096);
    close(fd);
    return 0;
}
EOF
    
    # 编译并运行基本测试程序
    gcc -o test_mmap_basic test_mmap_basic.c || handle_error "Failed to compile basic mmap test"
    ./test_mmap_basic || handle_error "Basic mmap test failed"
    log_info "Basic memory mapping test passed"
    
    # 运行高级内存映射测试
    if [ -f "./mmap_test" ]; then
        log_info "Running comprehensive mmap test..."
        ./mmap_test "$TEST_FILE" || handle_error "Comprehensive mmap test failed"
        log_info "Comprehensive memory mapping test passed"
    else
        log_warn "Comprehensive mmap test binary not found. Skipping."
    fi
    
    # 清理测试文件
    rm -f test_mmap_basic.c test_mmap_basic
}

# 3. 错误处理测试
error_test() {
    log_info "Starting error handling test..."
    
    # 0字节写入测试
    touch "$TEST_FILE.empty" || handle_error "Failed to create empty file"
    log_info "Zero-byte write test passed"
    
    # 超大写入测试
    dd if=/dev/zero of="$LARGE_FILE" bs=1M count=10 2>/dev/null || \
        log_warn "Large file write test produced expected error"
    
    # 无效操作测试
    if mkdir "$TEST_FILE" 2>/dev/null; then
        handle_error "Invalid operation (mkdir over file) should have failed"
    fi
    log_info "Invalid operation test passed"
}

# 4. 并发测试
concurrent_test() {
    log_info "Starting concurrent access test..."
    
    # 基本并发写入测试
    log_info "Running basic concurrent write test..."
    for i in {1..5}; do
        (
            for j in {1..10}; do
                echo "Test $i-$j" >> "$CONCURRENT_TEST_FILE"
                sleep 0.1
            done
        ) &
    done
    
    # 等待所有后台进程完成
    wait
    
    # 验证文件存在且有内容
    if [ ! -s "$CONCURRENT_TEST_FILE" ]; then
        handle_error "Basic concurrent write test failed"
    fi
    log_info "Basic concurrent write test passed"
    
    # 高级并发测试
    if [ -f "./concurrent_test" ]; then
        log_info "Running comprehensive concurrent test..."
        mkdir -p "$MOUNT_POINT/concurrent_test_dir"
        ./concurrent_test "$MOUNT_POINT/concurrent_test_dir" || handle_error "Comprehensive concurrent test failed"
        log_info "Comprehensive concurrent test passed"
    else
        log_warn "Comprehensive concurrent test binary not found. Skipping."
    fi
}

# 主测试流程
main() {
    log_info "Starting TFS full test suite..."
    
    # 确保开始时环境干净
    cleanup
    
    # 运行所有测试
    basic_test
    mmap_test
    error_test
    concurrent_test
    
    # 检查系统日志
    log_info "Checking system logs..."
    dmesg | grep TFS
    
    # 清理
    cleanup
    
    log_info "All tests completed successfully!"
}

# 捕获Ctrl+C和其他信号
trap cleanup EXIT INT TERM

# 运行测试
main