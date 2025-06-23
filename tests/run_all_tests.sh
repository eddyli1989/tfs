#!/bin/bash
# Master Test Script for TFS Distributed File System
# This script runs all tests in a safe order and generates a comprehensive report
# Run as: sudo ./run_all_tests.sh

# Color definitions
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
MOUNT_POINT="/mnt/tfs_test"
REPORT_DIR="test_reports"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
MAIN_LOG="$REPORT_DIR/test_run_${TIMESTAMP}.log"
HTML_REPORT="$REPORT_DIR/test_report_${TIMESTAMP}.html"

# Test status tracking
declare -A TEST_RESULTS
declare -A TEST_TIMES
declare -A TEST_LOGS

# Logging functions
log_info() {
    echo -e "${GREEN}[INFO]${NC} $1" | tee -a "$MAIN_LOG"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1" | tee -a "$MAIN_LOG"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a "$MAIN_LOG"
}

# Initialize test environment
init_test_env() {
    log_info "Initializing test environment..."
    
    # Create report directory
    mkdir -p "$REPORT_DIR"
    
    # Create mount point if it doesn't exist
    if [ ! -d "$MOUNT_POINT" ]; then
        mkdir -p "$MOUNT_POINT"
    fi
    
    # Clean up any existing test files
    if mountpoint -q "$MOUNT_POINT"; then
        log_warn "Filesystem already mounted at $MOUNT_POINT, attempting to unmount..."
        umount "$MOUNT_POINT" || {
            log_error "Failed to unmount existing filesystem"
            exit 1
        }
    fi
    
    # Check for existing kernel module
    if lsmod | grep -q tfs_client; then
        log_warn "TFS module already loaded, attempting to unload..."
        rmmod tfs_client || {
            log_error "Failed to unload existing module"
            exit 1
        }
    fi
    
    log_info "Test environment initialized"
}

# Run a test with timeout and logging
run_test() {
    local test_name="$1"
    local test_script="$2"
    local timeout="$3"
    local test_log="$REPORT_DIR/${test_name}_${TIMESTAMP}.log"
    
    log_info "Starting test: $test_name"
    TEST_LOGS[$test_name]="$test_log"
    
    local start_time=$(date +%s)
    
    # Run test with timeout
    timeout "$timeout" bash "$test_script" > "$test_log" 2>&1
    local status=$?
    
    local end_time=$(date +%s)
    TEST_TIMES[$test_name]=$((end_time - start_time))
    
    if [ $status -eq 124 ]; then
        log_error "Test $test_name timed out after $timeout seconds"
        TEST_RESULTS[$test_name]="TIMEOUT"
        return 1
    elif [ $status -ne 0 ]; then
        log_error "Test $test_name failed with status $status"
        TEST_RESULTS[$test_name]="FAILED"
        return 1
    else
        log_info "Test $test_name completed successfully"
        TEST_RESULTS[$test_name]="PASSED"
        return 0
    }
}

# Generate HTML report
generate_html_report() {
    log_info "Generating HTML report..."
    
    # Create HTML header
    cat > "$HTML_REPORT" << EOF
<!DOCTYPE html>
<html>
<head>
    <title>TFS Test Report - ${TIMESTAMP}</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        h1 { color: #333; }
        .summary { margin: 20px 0; padding: 10px; background-color: #f5f5f5; }
        .test-result { margin: 10px 0; padding: 10px; border: 1px solid #ddd; }
        .passed { border-left: 5px solid #4CAF50; }
        .failed { border-left: 5px solid #f44336; }
        .timeout { border-left: 5px solid #ff9800; }
        .details { margin-left: 20px; }
        pre { background-color: #f8f8f8; padding: 10px; overflow-x: auto; }
    </style>
</head>
<body>
    <h1>TFS Test Report</h1>
    <div class="summary">
        <h2>Test Summary</h2>
        <p>Date: $(date)</p>
        <p>Total Tests: ${#TEST_RESULTS[@]}</p>
EOF
    
    # Add test results
    local passed=0
    local failed=0
    local timeout=0
    
    for test_name in "${!TEST_RESULTS[@]}"; do
        case "${TEST_RESULTS[$test_name]}" in
            "PASSED") ((passed++));;
            "FAILED") ((failed++));;
            "TIMEOUT") ((timeout++));;
        esac
    done
    
    cat >> "$HTML_REPORT" << EOF
        <p>Passed: $passed</p>
        <p>Failed: $failed</p>
        <p>Timeout: $timeout</p>
    </div>
    
    <h2>Detailed Results</h2>
EOF
    
    # Add detailed test results
    for test_name in "${!TEST_RESULTS[@]}"; do
        local result="${TEST_RESULTS[$test_name]}"
        local time="${TEST_TIMES[$test_name]}"
        local log="${TEST_LOGS[$test_name]}"
        
        cat >> "$HTML_REPORT" << EOF
    <div class="test-result ${result,,}">
        <h3>$test_name</h3>
        <div class="details">
            <p>Status: $result</p>
            <p>Duration: ${time}s</p>
            <h4>Log Output:</h4>
            <pre>
$(cat "$log")
            </pre>
        </div>
    </div>
EOF
    done
    
    # Close HTML
    cat >> "$HTML_REPORT" << EOF
</body>
</html>
EOF
    
    log_info "HTML report generated: $HTML_REPORT"
}

# Main test sequence
main() {
    log_info "Starting TFS test suite..."
    
    # Initialize environment
    init_test_env
    
    # Compile all test programs
    log_info "Compiling test programs..."
    ./compile_tests.sh || {
        log_error "Failed to compile test programs"
        exit 1
    }
    
    # Run safe tests first
    run_test "Safe Basic Tests" "./safe_test.sh" 300
    
    # If safe tests pass, run performance tests
    if [ "${TEST_RESULTS[Safe Basic Tests]}" == "PASSED" ]; then
        run_test "Performance Tests" "./simple_perf_test.sh" 600
    else
        log_warn "Skipping performance tests due to basic test failure"
    fi
    
    # Generate report
    generate_html_report
    
    # Final summary
    log_info "Test suite completed"
    log_info "Results:"
    for test_name in "${!TEST_RESULTS[@]}"; do
        echo -e "${BLUE}$test_name${NC}: ${TEST_RESULTS[$test_name]} (${TEST_TIMES[$test_name]}s)"
    done
    
    log_info "Detailed report available at: $HTML_REPORT"
    
    # Return overall status
    if [ "${TEST_RESULTS[Safe Basic Tests]}" == "PASSED" ]; then
        return 0
    else
        return 1
    fi
}

# Run main function
main