#!/bin/bash
# Minimal Test Script for TFS Distributed File System
# This script performs only the most basic operations to identify crash points
# Run as: sudo ./minimal_test.sh

# Configuration
TEST_MOUNT="/mnt/tfs_test"
LOG_FILE="tfs_minimal_test.log"
KERNEL_LOG="/tmp/kernel_log_before_crash.txt"

# Color definitions
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Logging functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1" | tee -a $LOG_FILE
    # Flush log to disk immediately
    sync
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1" | tee -a $LOG_FILE
    sync
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a $LOG_FILE
    sync
}

# Initialize test
start_test() {
    echo "===== TFS Minimal Test =====" | tee $LOG_FILE
    echo "Starting test at $(date)" | tee -a $LOG_FILE
    
    # Check if running as root
    if [ "$EUID" -ne 0 ]; then
        log_error "This script must be run as root"
        exit 1
    fi
    
    # Save initial kernel log
    dmesg > $KERNEL_LOG
    log_info "Saved initial kernel log to $KERNEL_LOG"
}

# Check kernel logs for errors
check_kernel_logs() {
    local check_point="$1"
    log_info "Checking kernel logs at checkpoint: $check_point"
    
    # 保存检查点之前的日志时间戳
    local before_time=$(date +%s%N)
    
    # Get recent kernel messages
    local recent_logs=$(dmesg | tail -n 100)
    
    # 检查TFS相关的信息性日志
    log_info "TFS related logs at checkpoint $check_point:"
    echo "$recent_logs" | grep -i "tfs" | while read -r line; do
        echo "  $line" | tee -a $LOG_FILE
    done
    
    # 检查错误模式
    local error_patterns="panic|oops|segfault|fault|error|fail|bug|tfs.*error"
    local warning_patterns="warning|tfs.*warn"
    
    # 检查严重错误
    if echo "$recent_logs" | grep -i -E "$error_patterns" > /dev/null; then
        # Exclude known benign warnings
        if ! echo "$recent_logs" | grep -q "module verification failed: signature"; then
            log_error "Kernel errors detected at checkpoint: $check_point"
            echo "Critical errors found:" | tee -a $LOG_FILE
            echo "$recent_logs" | grep -i -E "$error_patterns" | tee -a $LOG_FILE
            return 1
        fi
    fi
    
    # 检查警告
    if echo "$recent_logs" | grep -i -E "$warning_patterns" > /dev/null; then
        log_warn "Kernel warnings detected at checkpoint: $check_point"
        echo "Warnings found:" | tee -a $LOG_FILE
        echo "$recent_logs" | grep -i -E "$warning_patterns" | tee -a $LOG_FILE
    fi
    
    # 检查零拷贝相关的日志
    if echo "$recent_logs" | grep -i "tfs.*zero.*copy" > /dev/null; then
        log_info "Zero-copy operations detected:"
        echo "$recent_logs" | grep -i "tfs.*zero.*copy" | tee -a $LOG_FILE
    fi
    
    # 检查空文件处理相关的日志
    if echo "$recent_logs" | grep -i "tfs.*empty.*file" > /dev/null; then
        log_info "Empty file operations detected:"
        echo "$recent_logs" | grep -i "tfs.*empty.*file" | tee -a $LOG_FILE
    fi
    
    log_info "Kernel log check completed at checkpoint: $check_point"
    return 0
}

# Setup Environment
setup_environment() {
    log_info "Setting up test environment..."
    
    # Create test mount point
    if [ ! -d "$TEST_MOUNT" ]; then
        mkdir -p $TEST_MOUNT
        log_info "Created mount point: $TEST_MOUNT"
    fi
    
    # Ensure directory is empty
    if mountpoint -q $TEST_MOUNT; then
        log_warn "$TEST_MOUNT is already mounted. Unmounting..."
        umount $TEST_MOUNT || {
            log_error "Failed to unmount $TEST_MOUNT"
            return 1
        }
    fi
    
    # Check for existing module
    if lsmod | grep -q tfs_client; then
        log_warn "TFS module is already loaded. Unloading..."
        rmmod tfs_client || {
            log_error "Failed to unload existing module"
            return 1
        }
    fi
    
    log_info "Environment setup completed"
    check_kernel_logs "after_setup"
    return 0
}

# Load kernel module
load_kernel_module() {
    log_info "Loading kernel module..."
    
    # Check if module exists
    if [ ! -f "tfs_client/tfs_client.ko" ]; then
        log_error "Kernel module file not found. Building..."
        (cd tfs_client && make) >> $LOG_FILE 2>&1
        
        if [ ! -f "tfs_client/tfs_client.ko" ]; then
            log_error "Failed to build kernel module"
            return 1
        fi
    fi
    
    # Load module
    insmod tfs_client/tfs_client.ko
    local status=$?
    
    if [ $status -ne 0 ]; then
        log_error "Failed to load kernel module (status $status)"
        return 1
    fi
    
    # Check if module is loaded
    if ! lsmod | grep -q tfs_client; then
        log_error "Module not found in lsmod output"
        return 1
    fi
    
    log_info "Kernel module loaded successfully"
    sleep 2  # Give the system time to stabilize
    
    # Check kernel logs
    check_kernel_logs "after_module_load"
    return $?
}

# Mount filesystem
mount_filesystem() {
    log_info "Mounting TFS filesystem..."
    
    mount -t tfs none $TEST_MOUNT
    local status=$?
    
    if [ $status -ne 0 ]; then
        log_error "Failed to mount filesystem (status $status)"
        return 1
    fi
    
    # Verify mount
    if ! mountpoint -q $TEST_MOUNT; then
        log_error "$TEST_MOUNT is not a mountpoint"
        return 1
    fi
    
    log_info "Mounted TFS at $TEST_MOUNT"
    sleep 2  # Give the system time to stabilize
    
    # Check kernel logs
    check_kernel_logs "after_mount"
    return $?
}

# Start daemon
start_daemon() {
    log_info "Starting TFS daemon..."
    
    # Check if daemon exists
    if [ ! -f "tfsd/tfsd" ]; then
        log_error "Daemon executable not found. Building..."
        g++ -std=c++11 -O2 -o tfsd/tfsd tfsd/tfsd.cpp >> $LOG_FILE 2>&1
        
        if [ ! -f "tfsd/tfsd" ]; then
            log_error "Failed to build daemon"
            return 1
        fi
    fi
    
    # Start daemon in background
    ./tfsd/tfsd >> $LOG_FILE 2>&1 &
    DAEMON_PID=$!
    
    # Wait for daemon to initialize
    sleep 2
    
    # Check if daemon is running
    if ! ps -p $DAEMON_PID > /dev/null; then
        log_error "Daemon failed to start or crashed"
        return 1
    fi
    
    log_info "Daemon started (PID $DAEMON_PID)"
    
    # Check kernel logs
    check_kernel_logs "after_daemon_start"
    return $?
}

# Test minimal file creation
test_minimal_file_creation() {
    log_info "Testing minimal file creation..."
    
    # Create a very small file
    local test_file="$TEST_MOUNT/minimal.txt"
    log_info "Creating empty file: $test_file"
    
    touch $test_file
    local status=$?
    
    if [ $status -ne 0 ]; then
        log_error "Failed to create empty file (status $status)"
        return 1
    fi
    
    log_info "Empty file created successfully"
    sleep 2  # 增加等待时间，确保守护进程有足够时间处理
    
    # 检查文件是否存在
    if [ ! -f "$test_file" ]; then
        log_error "Empty file does not exist after creation"
        return 1
    fi
    
    log_info "Empty file exists on filesystem"
    
    # 检查守护进程是否仍在运行
    if [ -n "$DAEMON_PID" ] && ! ps -p $DAEMON_PID > /dev/null; then
        log_error "Daemon crashed after empty file creation"
        return 1
    fi
    
    log_info "Daemon still running after empty file creation"
    
    # 检查文件大小
    local file_size=$(stat -c %s "$test_file" 2>/dev/null || echo "error")
    if [ "$file_size" = "error" ]; then
        log_error "Failed to get file size"
        return 1
    fi
    
    log_info "Empty file size: $file_size bytes"
    
    # Check kernel logs
    check_kernel_logs "after_file_creation"
    return $?
}

# Test minimal file write
test_minimal_file_write() {
    log_info "Testing minimal file write..."
    
    # Write a very small amount of data
    local test_file="$TEST_MOUNT/minimal.txt"
    local test_data="test"
    
    log_info "Writing 4 bytes to file: $test_file"
    echo -n "$test_data" > $test_file
    local status=$?
    
    if [ $status -ne 0 ]; then
        log_error "Failed to write to file (status $status)"
        return 1
    fi
    
    log_info "File write completed successfully"
    sleep 2  # 增加等待时间，确保守护进程有足够时间处理
    
    # 检查文件是否存在
    if [ ! -f "$test_file" ]; then
        log_error "File does not exist after write"
        return 1
    fi
    
    # 检查文件大小
    local file_size=$(stat -c %s "$test_file" 2>/dev/null || echo "error")
    if [ "$file_size" = "error" ]; then
        log_error "Failed to get file size after write"
        return 1
    fi
    
    log_info "File size after write: $file_size bytes"
    
    # 检查守护进程是否仍在运行
    if [ -n "$DAEMON_PID" ] && ! ps -p $DAEMON_PID > /dev/null; then
        log_error "Daemon crashed after file write"
        return 1
    fi
    
    log_info "Daemon still running after file write"
    
    # Check kernel logs
    check_kernel_logs "after_file_write"
    return $?
}

# Test minimal file read
test_minimal_file_read() {
    log_info "Testing minimal file read..."
    
    # Read the file
    local test_file="$TEST_MOUNT/minimal.txt"
    
    log_info "Reading from file: $test_file"
    local content=$(cat $test_file)
    local status=$?
    
    if [ $status -ne 0 ]; then
        log_error "Failed to read from file (status $status)"
        return 1
    fi
    
    log_info "File read completed successfully. Content: '$content'"
    
    # 验证文件内容
    local expected_content="test"
    if [ "$content" != "$expected_content" ]; then
        log_error "File content mismatch. Expected: '$expected_content', Got: '$content'"
        return 1
    fi
    
    log_info "File content verified successfully"
    sleep 2  # 增加等待时间，确保守护进程有足够时间处理
    
    # 检查守护进程是否仍在运行
    if [ -n "$DAEMON_PID" ] && ! ps -p $DAEMON_PID > /dev/null; then
        log_error "Daemon crashed after file read"
        return 1
    fi
    
    log_info "Daemon still running after file read"
    
    # Check kernel logs
    check_kernel_logs "after_file_read"
    return $?
}

# Cleanup function
cleanup() {
    log_info "Cleaning up..."
    
    # Kill daemon if running
    if [ -n "$DAEMON_PID" ] && ps -p $DAEMON_PID > /dev/null; then
        log_info "Stopping daemon (PID $DAEMON_PID)"
        kill $DAEMON_PID
        sleep 1
    fi
    
    # Unmount filesystem
    if mountpoint -q $TEST_MOUNT; then
        log_info "Unmounting $TEST_MOUNT"
        umount $TEST_MOUNT
        sleep 1
    fi
    
    # Unload module
    if lsmod | grep -q tfs_client; then
        log_info "Unloading kernel module"
        rmmod tfs_client
        sleep 1
    fi
    
    log_info "Cleanup completed"
}

# Main test sequence
main() {
    start_test
    
    # Set trap for cleanup on exit
    trap cleanup EXIT
    
    # Setup environment
    setup_environment || exit 1
    
    # Load module
    log_info "Step 1: Loading kernel module"
    load_kernel_module || exit 1
    log_info "Step 1 completed successfully"
    sleep 2
    
    # Mount filesystem
    log_info "Step 2: Mounting filesystem"
    mount_filesystem || exit 1
    log_info "Step 2 completed successfully"
    sleep 2
    
    # Start daemon
    log_info "Step 3: Starting daemon"
    start_daemon || exit 1
    log_info "Step 3 completed successfully"
    sleep 2
    
    # Create empty file
    log_info "Step 4: Creating empty file"
    test_minimal_file_creation || exit 1
    log_info "Step 4 completed successfully"
    sleep 2
    
    # Write to file
    log_info "Step 5: Writing to file"
    test_minimal_file_write || exit 1
    log_info "Step 5 completed successfully"
    sleep 2
    
    # Read from file
    log_info "Step 6: Reading from file"
    test_minimal_file_read || exit 1
    log_info "Step 6 completed successfully"
    sleep 2
    
    # Final status
    log_info "All minimal tests completed successfully"
    log_info "See detailed results in $LOG_FILE"
}

# Run main function
main