#!/bin/bash

REQUIRED_KERNEL=5.4
FAILED=0

check_command() {
    if ! command -v $1 &> /dev/null; then
        echo "✖ '$1' command not found"
        FAILED=1
    else
        echo "✓ $1"
    fi
}

check_kernel_version() {
    current_kernel=$(uname -r | cut -d. -f1-2)
    if [ $(echo "$current_kernel >= $REQUIRED_KERNEL" | bc -l) -eq 1 ]; then
        echo "✓ Kernel >= $REQUIRED_KERNEL (current: $current_kernel)"
    else
        echo "✖ Kernel version $current_kernel < required $REQUIRED_KERNEL"
        FAILED=1
    fi
}

check_kernel_headers() {
    local kernel_version=$(uname -r)
    if [ -d "/usr/src/kernels/$kernel_version" ] || [ -d "/lib/modules/$kernel_version/build" ]; then
        echo "✓ Kernel headers available"
    else
        echo "✖ Kernel headers not found"
        FAILED=1
    fi
}

echo "=== System Dependency Check ==="
check_kernel_version
check_kernel_headers
check_command make
check_command cmake
check_command gcc
check_command g++
check_command python3

echo -e "\n=== Library Dependency Check ==="
if rpm -q liburing-devel &> /dev/null; then
    echo "✓ liburing-devel installed"
else
    echo "✖ liburing-devel not installed"
    FAILED=1
fi

# 检查开发工具组
if rpm -q "Development Tools" &> /dev/null; then
    echo "✓ Development Tools group installed"
else
    echo "✖ Development Tools group not installed"
    FAILED=1
fi

if [ $FAILED -eq 1 ]; then
    echo -e "\nSome dependencies are missing. Run './scripts/install_deps.sh' to install"
    exit 1
else
    echo -e "\nAll dependencies satisfied"
    exit 0
fi