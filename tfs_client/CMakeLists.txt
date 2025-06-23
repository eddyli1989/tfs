# 使用自定义命令构建内核模块
add_custom_command(OUTPUT tfs_client.ko
    COMMAND make -f Makefile.kernel KERNELDIR=${KERNEL_HEADERS_DIR} CC=${CMAKE_C_COMPILER} AR=${CMAKE_AR} 
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building TFS kernel module"
)

add_custom_target(kernel_module ALL DEPENDS tfs_client.ko)

# 内核头文件检测
find_path(KERNEL_HEADERS_DIR
    NAMES linux/kernel.h
    PATHS /lib/modules/$<TARGET_FILE_DIR:uname -r>/build
          /usr/src/linux-headers-$<TARGET_FILE_DIR:uname -r>
    DOC "Kernel headers directory"
)

if(NOT KERNEL_HEADERS_DIR)
    message(WARNING "Kernel headers not found. Kernel module won't be built.")
    set(BUILD_KERNEL_MODULE OFF CACHE BOOL "" FORCE)
endif()

# 安装规则
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/tfs_client.ko
        DESTINATION /lib/modules/$<TARGET_FILE_DIR:uname -r>/extra
        COMPONENT kernel)