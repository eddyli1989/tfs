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

# 检查编译器是否可用
if ! command -v gcc &> /dev/null; then
    log_error "gcc compiler not found. Please install gcc."
    exit 1
fi

# 编译mmap测试程序
log_info "Compiling mmap_test.c..."
gcc -o mmap_test mmap_test.c -Wall -Wextra -pthread
if [ $? -ne 0 ]; then
    log_error "Failed to compile mmap_test.c"
    exit 1
fi
log_info "mmap_test compiled successfully"

# 编译并发测试程序
log_info "Compiling concurrent_test.c..."
gcc -o concurrent_test concurrent_test.c -Wall -Wextra -pthread
if [ $? -ne 0 ]; then
    log_error "Failed to compile concurrent_test.c"
    exit 1
fi
log_info "concurrent_test compiled successfully"

# 编译性能测试程序
log_info "Compiling performance_test.c..."
gcc -o performance_test performance_test.c -Wall -Wextra -lm
if [ $? -ne 0 ]; then
    log_error "Failed to compile performance_test.c"
    exit 1
fi
log_info "performance_test compiled successfully"

# 设置可执行权限
chmod +x mmap_test concurrent_test performance_test
chmod +x full_test.sh safe_test.sh simple_perf_test.sh

log_info "All test programs compiled successfully"
log_info "Available test scripts:"
log_info "  - ./full_test.sh       : Full test suite (may cause system instability)"
log_info "  - ./safe_test.sh       : Safe incremental testing"
log_info "  - ./simple_perf_test.sh: Simple performance testing"