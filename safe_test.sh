#!/bin/bash
# Safe Test Script for TFS Distributed File System
# This script performs incremental testing with safety checks
# Run as: sudo ./safe_test.sh

# Configuration
TEST_MOUNT="/mnt/tfs_test"
LOG_FILE="tfs_safe_test.log"
DEBUG_LOG="/var/log/kern.log"
TIMEOUT=5  # Timeout in seconds for operations

# Color definitions
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Initialize logging
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1" | tee -a $LOG_FILE
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1" | tee -a $LOG_FILE
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a $LOG_FILE
}

start_test() {
    echo "===== TFS Distributed File System Safe Test =====" | tee $LOG_FILE
    echo "Starting test at $(date)" | tee -a $LOG_FILE
    
    # Check if running as root
    if [ "$EUID" -ne 0 ]; then
        log_error "This script must be run as root"
        exit 1
    fi
}

# Function to run a command with timeout
run_with_timeout() {
    local cmd="$1"
    local timeout="$2"
    local description="$3"
    
    log_info "Running: $description"
    
    # Start command in background
    eval "$cmd" &
    local pid=$!
    
    # Wait for command to complete or timeout
    local counter=0
    while kill -0 $pid 2>/dev/null; do
        if [ $counter -ge $timeout ]; then
            log_error "$description timed out after $timeout seconds"
            kill -9 $pid 2>/dev/null
            return 1
        fi
        sleep 1
        counter=$((counter + 1))
    done
    
    # Check exit status
    wait $pid
    local status=$?
    if [ $status -ne 0 ]; then
        log_error "$description failed with status $status"
        return $status
    fi
    
    log_info "$description completed successfully"
    return 0
}

# Check kernel logs for errors
check_kernel_logs() {
    local check_point="$1"
    log_info "Checking kernel logs at checkpoint: $check_point"
    
    # Get recent kernel messages
    local recent_logs=$(dmesg | tail -n 50)
    
    # Check for common error patterns
    if echo "$recent_logs" | grep -i -E "panic|oops|segfault|fault|error|fail|bug|warn|tfs.*error" > /dev/null; then
        log_error "Kernel errors detected at checkpoint: $check_point"
        echo "$recent_logs" | grep -i -E "panic|oops|segfault|fault|error|fail|bug|warn|tfs.*error" | tee -a $LOG_FILE
        return 1
    fi
    
    log_info "No kernel errors detected at checkpoint: $check_point"
    return 0
}

# Build Kernel Module
build_kernel_module() {
    log_info "Building kernel module..."
    
    # Check if module already exists and is loaded
    if lsmod | grep -q tfs_client; then
        log_warn "TFS module is already loaded. Unloading first..."
        rmmod tfs_client || {
            log_error "Failed to unload existing module"
            return 1
        }
    fi
    
    # Clean and build
    (cd tfs_client && make clean && make) >> $LOG_FILE 2>&1
    
    if [ ! -f "tfs_client/tfs_client.ko" ]; then
        log_error "Kernel module build failed!"
        return 1
    fi
    
    log_info "Kernel module built successfully"
    return 0
}

# Build User-space Daemon
build_userspace_daemon() {
    log_info "Building user-space daemon..."
    
    # Kill any running daemon
    pkill -f tfsd/tfsd 2>/dev/null || true
    
    # Build daemon
    g++ -std=c++11 -O2 -o tfsd/tfsd tfsd/tfsd.cpp >> $LOG_FILE 2>&1
    
    if [ ! -f "tfsd/tfsd" ]; then
        log_error "User-space daemon build failed!"
        return 1
    fi
    
    log_info "User-space daemon built successfully"
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
    return 0
}

# Load kernel module
load_kernel_module() {
    log_info "Loading kernel module..."
    
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
    sleep 1  # Give the system time to stabilize
    
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
    sleep 1  # Give the system time to stabilize
    
    # Check kernel logs
    check_kernel_logs "after_mount"
    return $?
}

# Start daemon
start_daemon() {
    log_info "Starting TFS daemon..."
    
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

# Basic file operations test
test_basic_file_ops() {
    log_info "Testing basic file operations..."
    
    # Test 1: Simple text write
    local test_file="$TEST_MOUNT/test.txt"
    local test_text="Hello TFS Zero-Copy World!"
    
    log_info "Writing test file: $test_file"
    echo "$test_text" > $test_file
    
    if [ $? -ne 0 ]; then
        log_error "Failed to write to $test_file"
        return 1
    fi
    
    # Check kernel logs
    check_kernel_logs "after_write"
    
    # Verify file exists
    if [ ! -f "$test_file" ]; then
        log_error "Test file $test_file does not exist"
        return 1
    fi
    
    # Read back and verify content
    local read_content=$(cat $test_file)
    
    if [ "$read_content" != "$test_text" ]; then
        log_error "Content verification failed"
        log_error "Expected: $test_text"
        log_error "Got: $read_content"
        return 1
    fi
    
    log_info "Basic file operations test passed"
    return 0
}

# Test small file operations
test_small_file_ops() {
    log_info "Testing small file operations..."
    
    # Create a small file
    local small_file="$TEST_MOUNT/small.txt"
    local small_content="This is a small file test."
    
    log_info "Writing small file: $small_file"
    echo "$small_content" > $small_file
    
    if [ $? -ne 0 ]; then
        log_error "Failed to write small file"
        return 1
    }
    
    # Check kernel logs
    check_kernel_logs "after_small_write"
    
    # Read back and verify
    local read_content=$(cat $small_file)
    
    if [ "$read_content" != "$small_content" ]; then
        log_error "Small file content verification failed"
        return 1
    }
    
    log_info "Small file operations test passed"
    return 0
}

# Test medium file operations
test_medium_file_ops() {
    log_info "Testing medium file operations..."
    
    # Create a medium-sized file (100KB)
    local medium_file="$TEST_MOUNT/medium.dat"
    
    log_info "Writing medium file: $medium_file"
    dd if=/dev/urandom of=$medium_file bs=1024 count=100 2>> $LOG_FILE
    
    if [ $? -ne 0 ]; then
        log_error "Failed to write medium file"
        return 1
    }
    
    # Check kernel logs
    check_kernel_logs "after_medium_write"
    
    # Verify file size
    local file_size=$(stat -c%s "$medium_file")
    
    if [ "$file_size" -ne 102400 ]; then
        log_error "Medium file size verification failed"
        log_error "Expected: 102400 bytes"
        log_error "Got: $file_size bytes"
        return 1
    }
    
    log_info "Medium file operations test passed"
    return 0
}

# Safe memory mapping test
test_safe_mmap() {
    log_info "Testing safe memory mapping..."
    
    # Create a test file for mapping
    local mmap_file="$TEST_MOUNT/mmap_test.dat"
    local test_pattern="MMAP_TEST_PATTERN"
    
    # Write initial content
    log_info "Creating file for mmap test: $mmap_file"
    echo "$test_pattern" > $mmap_file
    
    if [ $? -ne 0 ]; then
        log_error "Failed to create file for mmap test"
        return 1
    }
    
    # Create a simple C program for mmap testing
    local mmap_test_prog="mmap_test_prog.c"
    cat > $mmap_test_prog << EOF
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file_path>\n", argv[0]);
        return 1;
    }
    
    const char *file_path = argv[1];
    int fd = open(file_path, O_RDWR);
    if (fd == -1) {
        perror("open failed");
        return 1;
    }
    
    // Get file size
    off_t file_size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    
    if (file_size <= 0) {
        fprintf(stderr, "Invalid file size: %ld\n", (long)file_size);
        close(fd);
        return 1;
    }
    
    printf("File size: %ld bytes\n", (long)file_size);
    
    // Map the file
    char *mapped_data = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_data == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }
    
    // Read and print current content
    printf("Current content: %s\n", mapped_data);
    
    // Modify the content
    const char *new_content = "MODIFIED_BY_MMAP";
    if (strlen(new_content) <= file_size) {
        strcpy(mapped_data, new_content);
        printf("Content modified to: %s\n", new_content);
    } else {
        fprintf(stderr, "New content too large for file\n");
    }
    
    // Sync changes to disk
    if (msync(mapped_data, file_size, MS_SYNC) == -1) {
        perror("msync failed");
    }
    
    // Unmap the file
    if (munmap(mapped_data, file_size) == -1) {
        perror("munmap failed");
    }
    
    close(fd);
    printf("Memory mapping test completed successfully\n");
    return 0;
}
EOF
    
    # Compile the test program
    log_info "Compiling mmap test program..."
    gcc -o mmap_test_prog $mmap_test_prog
    
    if [ $? -ne 0 ]; then
        log_error "Failed to compile mmap test program"
        return 1
    }
    
    # Run the test program
    log_info "Running mmap test program..."
    ./mmap_test_prog $mmap_file >> $LOG_FILE 2>&1
    
    if [ $? -ne 0 ]; then
        log_error "mmap test program failed"
        return 1
    }
    
    # Check kernel logs
    check_kernel_logs "after_mmap_test"
    
    # Verify the file was modified
    local read_content=$(cat $mmap_file)
    
    if [ "$read_content" != "MODIFIED_BY_MMAP" ]; then
        log_error "mmap content verification failed"
        log_error "Expected: MODIFIED_BY_MMAP"
        log_error "Got: $read_content"
        return 1
    }
    
    log_info "Safe memory mapping test passed"
    
    # Clean up
    rm -f mmap_test_prog mmap_test_prog.c
    
    return 0
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
    
    # Build and setup
    build_kernel_module || exit 1
    build_userspace_daemon || exit 1
    setup_environment || exit 1
    
    # Load module and mount filesystem
    load_kernel_module || exit 1
    mount_filesystem || exit 1
    start_daemon || exit 1
    
    # Run tests incrementally with safety checks
    log_info "Starting incremental tests..."
    
    # Basic file operations
    run_with_timeout "test_basic_file_ops" $TIMEOUT "Basic file operations test" || {
        log_error "Basic file operations test failed, aborting further tests"
        exit 1
    }
    
    # Small file operations
    run_with_timeout "test_small_file_ops" $TIMEOUT "Small file operations test" || {
        log_warn "Small file operations test failed, continuing with next test"
    }
    
    # Medium file operations
    run_with_timeout "test_medium_file_ops" $TIMEOUT "Medium file operations test" || {
        log_warn "Medium file operations test failed, continuing with next test"
    }
    
    # Safe memory mapping test
    run_with_timeout "test_safe_mmap" $TIMEOUT "Safe memory mapping test" || {
        log_warn "Memory mapping test failed"
    }
    
    # Final status
    log_info "All tests completed"
    log_info "See detailed results in $LOG_FILE"
    
    # Don't call cleanup here as it will be called by the trap
}

# Run main function
main