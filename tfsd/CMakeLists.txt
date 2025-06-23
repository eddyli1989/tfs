add_executable(tfsd tfsd.cpp)

# 依赖查找
find_package(Threads REQUIRED)
find_package(LibURING REQUIRED)

if(LIBURING_FOUND)
    target_include_directories(tfsd PRIVATE ${LIBURING_INCLUDE_DIRS})
    target_link_libraries(tfsd PRIVATE ${LIBURING_LIBRARIES})
else()
    message(WARNING "liburing not found - using fallback implementation")
    target_compile_definitions(tfsd PRIVATE NO_IO_URING)
endif()

target_link_libraries(tfsd PRIVATE Threads::Threads)

# 安装规则
install(TARGETS tfsd
        RUNTIME DESTINATION bin
        COMPONENT userspace)