#!/bin/bash

ACTION=${1:-all}

run_kernel_build() {
    cd tfs_client
    make clean
    KERNELDIR=/usr/src/kernels/$(uname -r) make
    cd ..
}

run_userspace_build() {
    mkdir -p build
    cd build
    cmake ..
    make -j$(nproc)
    cd ..
}

case $ACTION in
    kernel)
        run_kernel_build
        ;;
    userspace)
        run_userspace_build
        ;;
    all)
        run_kernel_build
        run_userspace_build
        ;;
    clean)
        cd tfs_client
        make -f Makefile.kernel clean
        cd ..
        rm -rf build
        ;;
    *)
        echo "Usage: $0 [kernel|userspace|all|clean]"
        exit 1
        ;;
esac

echo "Build operation '$ACTION' completed"