#!/bin/bash
# Full System Test Script for TFS Distributed File System
# Run from project root as: sudo ./test.sh

set -e # Exit immediately if any command fails

# Configuration
TEST_MOUNT="/mnt/tfs_test"
LOG_FILE="tfs_test.log"

# Initialize logging
start_test() {
    echo "===== TFS Distributed File System Test =====" | tee $LOG_FILE
    echo "Starting test at $(date)" | tee -a $LOG_FILE
}

# Build Kernel Module
build_kernel_module() {
    echo ">> Building kernel module..." | tee -a $LOG_FILE
    cd tfs_client
    make -s >> ../$LOG_FILE 2>&1
    if [ ! -f "tfs_client.ko" ]; then
        echo "Error: Kernel module build failed!" | tee -a ../$LOG_FILE
        exit 1
    fi
    cd ..
    echo "Kernel module built successfully" | tee -a $LOG_FILE
}

# Build User-space Daemon
build_userspace_daemon() {
    echo ">> Building user-space daemon..." | tee -a $LOG_FILE
    g++ -std=c++11 -O2 -o tfsd/tfsd tfsd/tfsd.cpp >> $LOG_FILE 2>&1
    if [ ! -f "tfsd/tfsd" ]; then
        echo "Error: User-space daemon build failed!" | tee -a $LOG_FILE
        exit 1
    fi
    echo "User-space daemon built successfully" | tee -a $LOG_FILE
}

# Setup Environment
setup_environment() {
    echo ">> Setting up test environment..." | tee -a $LOG_FILE
    
    # Create test mount point
    if [ ! -d "$TEST_MOUNT" ]; then
        sudo mkdir -p $TEST_MOUNT
        echo "Created mount point: $TEST_MOUNT" | tee -a $LOG_FILE
    fi
    
    # Ensure directory is empty
    if [ "$(ls -A $TEST_MOUNT 2>/dev/null)" ]; then
        echo "Warning: $TEST_MOUNT is not empty! Contents will not be modified." | tee -a $LOG_FILE
    fi
}

# Load and Initialize TFS
start_tfs() {
    echo ">> Starting TFS system..." | tee -a $LOG_FILE
    
    # Load kernel module
    if sudo insmod tfs_client/tfs_client.ko >> $LOG_FILE 2>&1; then
        echo "Kernel module loaded successfully" | tee -a $LOG_FILE
    else
        echo "Error: Failed to load kernel module!" | tee -a $LOG_FILE
        exit 1
    fi
    echo "start mount tfs..." | tee -a $LOG_FILE
    sleep 1

    # Mount filesystem
    if sudo mount -t tfs none $TEST_MOUNT >> $LOG_FILE 2>&1; then
        echo "Mounted TFS at $TEST_MOUNT" | tee -a $LOG_FILE
    else
        echo "Error: Failed to mount TFS filesystem!" | tee -a $LOG_FILE
        unload_tfs
        exit 1
    fi
    echo "starting tfsd..." | tee -a $LOG_FILE
    
    # Start daemon in background
    sudo ./tfsd/tfsd >> $LOG_FILE 2>&1 &
    DAEMON_PID=$!
    sleep 1 # Allow daemon to start
    if ps -p $DAEMON_PID > /dev/null; then
        echo "Daemon started (PID $DAEMON_PID)" | tee -a $LOG_FILE
    else
        echo "Error: Daemon failed to start!" | tee -a $LOG_FILE
        unload_tfs
        exit 1
    fi
}

# Run actual tests
run_tests() {
    echo ">> Running test cases..." | tee -a $LOG_FILE
    
    # Test 1: Simple text write
    TEST_FILE="$TEST_MOUNT/test.txt"
    TEST_TEXT="Hello TFS Zero-Copy World!"
    echo "Writing test file: $TEST_FILE" | tee -a $LOG_FILE
    echo "$TEST_TEXT" | sudo tee $TEST_FILE >/dev/null
    
    # Allow time for processing
    sleep 1
    
    # Test 2: Binary data write
    BIN_FILE="$TEST_MOUNT/random.bin"
    echo "Writing binary file: $BIN_FILE" | tee -a $LOG_FILE
    head -c 256 /dev/urandom | sudo tee $BIN_FILE >/dev/null
    
    # Allow time for processing
    sleep 1
    
    echo "Test files written successfully" | tee -a $LOG_FILE
}

# Verify test results
verify_results() {
    echo ">> Verifying test results..." | tee -a $LOG_FILE
    
    # Check log for expected outputs
    grep_result=$(grep -c "Content Preview" $LOG_FILE || true)
    
    if [ "$grep_result" -ge 2 ]; then
        echo "Success: Found $grep_result data transfers in log" | tee -a $LOG_FILE
    else
        echo "Error: Data transfers not detected!" | tee -a $LOG_FILE
        TEST_FAILED=true
    fi
    
    # Verify text file content preview
    if grep -q "Hello TFS Zero-Copy World!" $LOG_FILE; then
        echo "Success: Text content preview verified" | tee -a $LOG_FILE
    else
        echo "Error: Text content not found in log!" | tee -a $LOG_FILE
        TEST_FAILED=true
    fi
    
    # Verify binary signature in log
    if grep -q "Hex Dump" $LOG_FILE; then
        echo "Success: Binary data dump detected" | tee -a $LOG_FILE
    else
        echo "Error: Binary data dump not found!" | tee -a $LOG_FILE
        TEST_FAILED=true
    fi
    
    # Final verification status
    if [ -z "${TEST_FAILED}" ]; then
        echo "All tests passed successfully!" | tee -a $LOG_FILE
        echo "Complete log available at: $PWD/$LOG_FILE"
    else
        echo "Some tests failed! Check log for details: $PWD/$LOG_FILE" | tee -a $LOG_FILE
    fi
}

# Unload TFS and clean up
unload_tfs() {
    echo ">> Cleaning up test environment..." | tee -a $LOG_FILE
    
    # Kill daemon if still running
    if [ -n "$DAEMON_PID" ] && ps -p $DAEMON_PID > /dev/null; then
        sudo kill $DAEMON_PID
        echo "Stopped daemon (PID $DAEMON_PID)" | tee -a $LOG_FILE
    fi
    
    # Unmount filesystem
    if mountpoint -q $TEST_MOUNT; then
        sudo umount $TEST_MOUNT
        echo "Unmounted $TEST_MOUNT" | tee -a $LOG_FILE
    fi
    
    # Remove kernel module
    if lsmod | grep -q tfs_client; then
        sudo rmmod tfs_client
        echo "Unloaded kernel module" | tee -a $LOG_FILE
    fi
    
    # Clean build artifacts
    echo "Cleaning build artifacts..." | tee -a $LOG_FILE
    make -s -C tfs_client clean >> $LOG_FILE 2>&1
    rm -f tfsd/tfsd tfs_test.log
    
    # Preserve mount point for inspection
    #sudo rmdir $TEST_MOUNT 2>/dev/null || true
}

# Main execution
main() {
    start_test
    build_kernel_module
    build_userspace_daemon
    setup_environment
    start_tfs
    run_tests
    sleep 2 # Allow time for processing
    verify_results
    unload_tfs
    
    # Final message
    echo "Test sequence completed. See detailed results in $LOG_FILE" | tee -a $LOG_FILE
}

# Run main function
main