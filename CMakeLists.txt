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


# 添加可执行文件（混合 C/C++ 源文件）
add_executable(kernel.elf #内核镜像
    kinit.cpp           
    VideoDriver.c
    PortDriver.c
    Memory.cpp
    kpoolmemmgr.cpp
    util.cpp
    phygpsmemmgr.cpp
)
#add_executable(test_moudle.elf
#test_main.cpp
#test_gBitmapFreePhyMemMgr.cpp
#)

add_executable(test_kpoolmemmgr
test_kpoolmemmgr.cpp
kpoolmemmgr.cpp
util.cpp
VideoDriver.c
PortDriver.c
)

# 头文件路径（对 C/C++ 均可见）
target_include_directories(kernel.elf PRIVATE
    "/usr/include/efi/"
    "${CMAKE_SOURCE_DIR}"
)

target_include_directories(test_kpoolmemmgr PRIVATE
    "/usr/include/efi/"
    "${CMAKE_SOURCE_DIR}"
)
target_compile_options(kernel.elf PRIVATE 
    "-nostdlib"#禁用标准库
    "-ffreestanding"#自由环境
    -fno-use-cxa-atexit #禁用c++的析构函数
        -fno-exceptions #禁用异常
     -fno-builtin #禁用内建函数
    "-static"
   -DKERNEL_MODE  # 内核专用宏定义
)
target_link_options(kernel.elf PRIVATE 
    "-T${CMAKE_SOURCE_DIR}/linker_for_kernel.ld"
    "-nostdlib"#禁用标准库
    "-ffreestanding"#自由环境
    -fno-use-cxa-atexit #禁用c++的析构函数
        -fno-exceptions #禁用异常
     -fno-builtin #禁用内建函数
    "-static"
    "-lgcc" 
        
)

target_compile_options(test_kpoolmemmgr PRIVATE
        -DTEST_MODE    # 测试专用宏定义
)