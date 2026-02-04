# 创建内存管理模块的对象文件，用于内核空间
# 包含内核池内存管理、物理内存段管理等相关功能的实现文件
set(MEMORY_SOURCE 
    ${CMAKE_CURRENT_SOURCE_DIR}/Memory.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/phygpsmemmgr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/phygpsmemmgr_pgcount_modify.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/phygpsmemmgr_pgs_set.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/phygpsmemmgr_seg_managment.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/phygpsmemmgr_availabe_search.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/phypgsmemmgr_alloc.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/phygpsmemmgr_buddy_about.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/kpoolmemmgr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/kpoolmemmgr_HCBv2.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AddresSpace.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/KspacMapMgr.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/KspacMapMgr_pagediretct_operate.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/out_surfaces.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/PhyAddrAccessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FreePagesAllocator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FreePagesAllocator_BCB.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mixed_bitmap.cpp
)

add_library(Mem_module OBJECT ${MEMORY_SOURCE})

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
        -fno-threadsafe-statics
        -fno-builtin
        -fno-rtti
        -fno-PIC
        -mgeneral-regs-only
        -mno-red-zone
         -fno-stack-protector
        #-fno-asynchronous-unwind-tables
         -DKERNEL_MODE
         -DPGLV_4
         -DPRINT_OUT
         $<$<CONFIG:Debug>:-g -O0 -gdwarf-4>
         $<$<CONFIG:Release>:-O2>
         
    )

