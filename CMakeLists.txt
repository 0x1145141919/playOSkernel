#CMakeLists.txt

cmake_minimum_required(VERSION 3.15)  # 推荐使用较新版本以支持现代特性
project(Kernel LANGUAGES C CXX)       # 显式声明支持 C 和 C++
# 设置构建类型变量，默认为Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build (Debug or Release)" FORCE)
endif()

# 全局编译选项（对 C 和 C++ 均生效）
add_compile_options(
    -Wall
    -Wextra
)
# 根据构建类型设置不同的编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Building in DEBUG mode")
    add_compile_options(-g -O0)
else()
    message(STATUS "Building in RELEASE mode")
    add_compile_options(-O3)
endif()
# 语言特定的选项（可选）
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c23")          # C 标准
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++23")    # C++ 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
add_compile_options(-fno-stack-protector)


# 定义通用源文件
set(SRC_COMMON
    src/memory/Memory.cpp
    src/memory/kpoolmemmgr.cpp
    src/memory/phygpsmemmgr_init.cpp
    src/memory/phygpsmemmgr_print.cpp
    src/memory/phygpsmemmgr_freememmanage.cpp
    src/memory/pgtable_query.cpp
    src/utils/util.cpp
    src/drivers/VideoDriver.c
    src/drivers/PortDriver.c
)

# 内核可执行文件
add_executable(kernel.elf
    ${CMAKE_SOURCE_DIR}/src/kinit.cpp
    ${SRC_COMMON}
)

# Memory 模块测试可执行文件
add_executable(memmory_module_test
    ${CMAKE_SOURCE_DIR}/src/tests/Memory_moudle_test.cpp
    ${SRC_COMMON}
)

# KPoolMemMgr 测试可执行文件
add_executable(test_kpoolmemmgr
    src/tests/test_kpoolmemmgr.cpp
    src/memory/kpoolmemmgr.cpp
    src/utils/util.cpp
    src/drivers/VideoDriver.c
    src/drivers/PortDriver.c
)

# 头文件路径（对 C/C++ 均可见）
target_include_directories(kernel.elf PRIVATE
    "/usr/include/efi/"
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/include"
)
target_include_directories(memmory_module_test PRIVATE
    "/usr/include/efi/"
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/include"
)
target_include_directories(test_kpoolmemmgr PRIVATE
    "/usr/include/efi/"
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src/include"
)
# 定义内核专用编译选项
set(KERNEL_COMPILE_OPTS
    -nostdlib
    -ffreestanding
    -fno-use-cxa-atexit
    -fno-exceptions
    -fno-builtin
    -static
    -DKERNEL_MODE
)

# 内核链接选项
set(KERNEL_LINK_OPTS
-nostdlib
    -ffreestanding
    -fno-use-cxa-atexit
    -fno-exceptions
    -fno-builtin
    -static
    -T${CMAKE_SOURCE_DIR}/linker_for_kernel.ld
    -lgcc
)

# 应用编译选项
target_compile_options(kernel.elf PRIVATE ${KERNEL_COMPILE_OPTS})
target_link_options(kernel.elf PRIVATE ${KERNEL_LINK_OPTS})
target_compile_options(memmory_module_test PRIVATE -DTEST_MODE)
target_compile_options(test_kpoolmemmgr PRIVATE -DTEST_MODE)