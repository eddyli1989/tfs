#!/bin/bash
# Simple Performance Test Script for TFS Distributed File System
# This script performs basic performance tests without risking system stability
# Run as: sudo ./simple_perf_test.sh <mount_point>

# Color definitions
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
DEFAULT_MOUNT="/mnt/tfs_test"
LOG_FILE="tfs_perf_test.log"
TEST_SIZES=(4 16 64 256 1024) # KB
NUM_ITERATIONS=3

# Logging functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1" | tee -a $LOG_FILE
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1" | tee -a $LOG_FILE
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a $LOG_FILE
}

log_result() {
    echo -e "${BLUE}[RESULT]${NC} $1" | tee -a $LOG_FILE
}

# Check arguments
if [ $# -lt 1 ]; then
    echo "Usage: $0 <mount_point>"
    echo "Using default mount point: $DEFAULT_MOUNT"
    MOUNT_POINT=$DEFAULT_MOUNT
else
    MOUNT_POINT=$1
fi

# Initialize test
init_test() {
    echo "===== TFS Simple Performance Test =====" | tee $LOG_FILE
    echo "Starting test at $(date)" | tee -a $LOG_FILE
    echo "Mount point: $MOUNT_POINT" | tee -a $LOG_FILE
    
    # Check if mount point exists and is mounted
    if [ ! -d "$MOUNT_POINT" ]; then
        log_error "Mount point $MOUNT_POINT does not exist"
        exit 1
    fi
    
    if ! mountpoint -q $MOUNT_POINT; then
        log_error "$MOUNT_POINT is not a mountpoint"
        exit 1
    fi
    
    # Create test directory
    TEST_DIR="$MOUNT_POINT/perf_test"
    mkdir -p $TEST_DIR
    
    if [ $? -ne 0 ]; then
        log_error "Failed to create test directory $TEST_DIR"
        exit 1
    fi
    
    log_info "Test initialized successfully"
}

# Clean up test files
cleanup() {
    log_info "Cleaning up test files..."
    rm -rf $TEST_DIR
    log_info "Cleanup completed"
}

# Sequential write test
test_seq_write() {
    local size_kb=$1
    local size_bytes=$((size_kb * 1024))
    local file="$TEST_DIR/seq_write_${size_kb}kb.dat"
    
    log_info "Testing sequential write: $size_kb KB"
    
    # Create test data
    local tmp_file="/tmp/tfs_test_data_${size_kb}kb.dat"
    dd if=/dev/urandom of=$tmp_file bs=1024 count=$size_kb &>/dev/null
    
    # Measure write performance
    local total_time=0
    local successful_runs=0
    
    for i in $(seq 1 $NUM_ITERATIONS); do
        log_info "  Run $i/$NUM_ITERATIONS..."
        
        # Time the write operation
        local start_time=$(date +%s.%N)
        cp $tmp_file $file
        local status=$?
        local end_time=$(date +%s.%N)
        
        if [ $status -ne 0 ]; then
            log_warn "  Write failed on iteration $i"
            continue
        fi
        
        local elapsed=$(echo "$end_time - $start_time" | bc)
        local throughput=$(echo "scale=2; $size_kb / $elapsed" | bc)
        
        log_info "  Time: ${elapsed}s, Throughput: ${throughput} KB/s"
        total_time=$(echo "$total_time + $elapsed" | bc)
        successful_runs=$((successful_runs + 1))
    done
    
    # Calculate average
    if [ $successful_runs -gt 0 ]; then
        local avg_time=$(echo "scale=4; $total_time / $successful_runs" | bc)
        local avg_throughput=$(echo "scale=2; $size_kb / $avg_time" | bc)
        log_result "Sequential Write ($size_kb KB): Avg Time: ${avg_time}s, Avg Throughput: ${avg_throughput} KB/s"
    else
        log_error "All sequential write tests failed for $size_kb KB"
    fi
    
    # Clean up
    rm -f $tmp_file
}

# Sequential read test
test_seq_read() {
    local size_kb=$1
    local size_bytes=$((size_kb * 1024))
    local file="$TEST_DIR/seq_write_${size_kb}kb.dat"
    
    # Check if file exists
    if [ ! -f "$file" ]; then
        log_error "Test file $file does not exist"
        return
    }
    
    log_info "Testing sequential read: $size_kb KB"
    
    # Measure read performance
    local total_time=0
    local successful_runs=0
    
    for i in $(seq 1 $NUM_ITERATIONS); do
        log_info "  Run $i/$NUM_ITERATIONS..."
        
        # Clear cache to ensure fair testing
        echo 3 > /proc/sys/vm/drop_caches
        
        # Time the read operation
        local start_time=$(date +%s.%N)
        cat $file > /dev/null
        local status=$?
        local end_time=$(date +%s.%N)
        
        if [ $status -ne 0 ]; then
            log_warn "  Read failed on iteration $i"
            continue
        fi
        
        local elapsed=$(echo "$end_time - $start_time" | bc)
        local throughput=$(echo "scale=2; $size_kb / $elapsed" | bc)
        
        log_info "  Time: ${elapsed}s, Throughput: ${throughput} KB/s"
        total_time=$(echo "$total_time + $elapsed" | bc)
        successful_runs=$((successful_runs + 1))
    done
    
    # Calculate average
    if [ $successful_runs -gt 0 ]; then
        local avg_time=$(echo "scale=4; $total_time / $successful_runs" | bc)
        local avg_throughput=$(echo "scale=2; $size_kb / $avg_time" | bc)
        log_result "Sequential Read ($size_kb KB): Avg Time: ${avg_time}s, Avg Throughput: ${avg_throughput} KB/s"
    else
        log_error "All sequential read tests failed for $size_kb KB"
    fi
}

# Simple mmap test
test_mmap() {
    local size_kb=$1
    local size_bytes=$((size_kb * 1024))
    local file="$TEST_DIR/mmap_${size_kb}kb.dat"
    
    log_info "Testing mmap operations: $size_kb KB"
    
    # Create test file
    dd if=/dev/urandom of=$file bs=1024 count=$size_kb &>/dev/null
    
    if [ $? -ne 0 ]; then
        log_error "Failed to create test file for mmap test"
        return
    }
    
    # Create a simple C program for mmap testing
    local mmap_test_prog="/tmp/mmap_perf_test.c"
    cat > $mmap_test_prog << EOF
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>

// Get current time in microseconds
long long get_time_usec() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000000 + tv.tv_usec;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file_path>\\n", argv[0]);
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
        fprintf(stderr, "Invalid file size: %ld\\n", (long)file_size);
        close(fd);
        return 1;
    }
    
    printf("File size: %ld bytes\\n", (long)file_size);
    
    // Time mmap operation
    long long start_time = get_time_usec();
    
    // Map the file
    char *mapped_data = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped_data == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }
    
    long long mmap_time = get_time_usec() - start_time;
    printf("mmap time: %lld microseconds\\n", mmap_time);
    
    // Time read operation through mmap
    start_time = get_time_usec();
    
    // Read all data (prevent optimization)
    volatile char sum = 0;
    for (off_t i = 0; i < file_size; i++) {
        sum += mapped_data[i];
    }
    
    long long read_time = get_time_usec() - start_time;
    printf("mmap read time: %lld microseconds\\n", read_time);
    
    // Time write operation through mmap
    start_time = get_time_usec();
    
    // Write data
    for (off_t i = 0; i < file_size; i++) {
        mapped_data[i] = (char)(i % 256);
    }
    
    long long write_time = get_time_usec() - start_time;
    printf("mmap write time: %lld microseconds\\n", write_time);
    
    // Time msync operation
    start_time = get_time_usec();
    
    if (msync(mapped_data, file_size, MS_SYNC) == -1) {
        perror("msync failed");
    }
    
    long long msync_time = get_time_usec() - start_time;
    printf("msync time: %lld microseconds\\n", msync_time);
    
    // Time munmap operation
    start_time = get_time_usec();
    
    if (munmap(mapped_data, file_size) == -1) {
        perror("munmap failed");
    }
    
    long long munmap_time = get_time_usec() - start_time;
    printf("munmap time: %lld microseconds\\n", munmap_time);
    
    close(fd);
    
    // Print summary
    double total_mb = file_size / (1024.0 * 1024.0);
    double read_throughput = total_mb / (read_time / 1000000.0);
    double write_throughput = total_mb / (write_time / 1000000.0);
    
    printf("Summary:\\n");
    printf("  File size: %.2f MB\\n", total_mb);
    printf("  Read throughput: %.2f MB/s\\n", read_throughput);
    printf("  Write throughput: %.2f MB/s\\n", write_throughput);
    
    return 0;
}
EOF
    
    # Compile the test program
    gcc -O2 -o /tmp/mmap_perf_test $mmap_test_prog
    
    if [ $? -ne 0 ]; then
        log_error "Failed to compile mmap test program"
        return
    }
    
    # Run the test program
    local total_read_tp=0
    local total_write_tp=0
    local successful_runs=0
    
    for i in $(seq 1 $NUM_ITERATIONS); do
        log_info "  Run $i/$NUM_ITERATIONS..."
        
        # Clear cache
        echo 3 > /proc/sys/vm/drop_caches
        
        # Run test
        local output=$(/tmp/mmap_perf_test $file 2>&1)
        local status=$?
        
        if [ $status -ne 0 ]; then
            log_warn "  mmap test failed on iteration $i"
            echo "$output" >> $LOG_FILE
            continue
        }
        
        # Extract results
        local read_tp=$(echo "$output" | grep "Read throughput" | awk '{print $3}')
        local write_tp=$(echo "$output" | grep "Write throughput" | awk '{print $3}')
        
        log_info "  Read throughput: ${read_tp} MB/s, Write throughput: ${write_tp} MB/s"
        
        total_read_tp=$(echo "$total_read_tp + $read_tp" | bc)
        total_write_tp=$(echo "$total_write_tp + $write_tp" | bc)
        successful_runs=$((successful_runs + 1))
    done
    
    # Calculate average
    if [ $successful_runs -gt 0 ]; then
        local avg_read_tp=$(echo "scale=2; $total_read_tp / $successful_runs" | bc)
        local avg_write_tp=$(echo "scale=2; $total_write_tp / $successful_runs" | bc)
        log_result "mmap ($size_kb KB): Avg Read: ${avg_read_tp} MB/s, Avg Write: ${avg_write_tp} MB/s"
    else
        log_error "All mmap tests failed for $size_kb KB"
    fi
    
    # Clean up
    rm -f /tmp/mmap_perf_test /tmp/mmap_perf_test.c
}

# Run all tests
run_all_tests() {
    log_info "Starting performance tests..."
    
    # Run tests for each file size
    for size in "${TEST_SIZES[@]}"; do
        # Sequential write test
        test_seq_write $size
        
        # Sequential read test
        test_seq_read $size
        
        # mmap test
        test_mmap $size
        
        echo "" | tee -a $LOG_FILE
    done
    
    log_info "All performance tests completed"
}

# Main function
main() {
    init_test
    run_all_tests
    cleanup
    
    log_info "Performance test completed. See detailed results in $LOG_FILE"
}

# Run main function
main