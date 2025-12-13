# 创建内存管理模块的对象文件，用于内核空间
# 包含内核池内存管理、物理内存段管理等相关功能的实现文件
add_library(Mem_module OBJECT 
    AddresSpace.cpp
    kpoolmemmgr.cpp
    kpoolmemmgr_HCBv2.cpp
    Memory.cpp
    phygpsmemmgr.cpp
    phypgsmemmgr_alloc.cpp
    phygpsmemmgr_init.cpp
    phygpsmemmgr_pgcount_modify.cpp
    KspacMapMgr.cpp
    KspacMapMgr_pagediretct_operate.cpp
    PagetbHeapMgr.cpp
)

# 设置私有包含目录，指定编译时需要的头文件搜索路径
# 包括上层目录的include目录和当前目录下的includes目录
target_include_directories(Mem_module PRIVATE
"/usr/include/efi/"
${CMAKE_CURRENT_SOURCE_DIR}/../include
    )

    # 设置编译选项，添加-fPIC选项以生成位置无关代码
    target_compile_options(
        Mem_module PRIVATE 
        -nostdlib
        -ffreestanding
        -fno-use-cxa-atexit
        -fno-exceptions
        -fno-builtin
        -fno-rtti
        -fno-PIC
         -fno-stack-protector
        #-fno-asynchronous-unwind-tables
         -DKERNEL_MODE
         $<$<CONFIG:Debug>:-g -O0 -gdwarf-4>
         $<$<CONFIG:Release>:-O3>
         
    )

