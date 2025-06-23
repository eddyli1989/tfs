cmake_minimum_required(VERSION 3.15)
project(tfs_distributed_fs VERSION 1.0 LANGUAGES C CXX)

# 选项配置
option(BUILD_KERNEL_MODULE "Build TFS kernel module" ON)
option(BUILD_USERSPACE "Build userspace daemon" ON)
option(BUILD_TESTS "Build tests" ON)

# 全局设置
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# 内核模块构建
if(BUILD_KERNEL_MODULE)
    add_subdirectory(tfs_client)
endif()

# 用户态程序构建
if(BUILD_USERSPACE)
    add_subdirectory(tfsd)
endif()

# 测试套件
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

# 全局目标
add_custom_target(check_deps
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/check_deps.sh
    COMMENT "Checking system dependencies"
)

add_custom_target(clean_all
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target clean
    COMMAND ${MAKE} -C ${CMAKE_SOURCE_DIR}/tfs_client -f Makefile.kernel clean
    COMMAND rm -rf ${CMAKE_BINARY_DIR}/_deps ${CMAKE_BINARY_DIR}/Testing
    COMMENT "Cleaning all build artifacts"
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/tfs_client.ko
        DESTINATION /lib/modules/$(shell uname -r)/extra
        COMPONENT kernel)

# 安装目标
install(CODE [[
    message("\nInstallation complete!")
    message("To load kernel module: sudo insmod tfs_client/tfs_client.ko")
    message("To start daemon: sudo ${CMAKE_INSTALL_PREFIX}/bin/tfsd")
    message("To mount filesystem: mkdir -p /mnt/tfs && mount -t tfs none /mnt/tfs")
]])