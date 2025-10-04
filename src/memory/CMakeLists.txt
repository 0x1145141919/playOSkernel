# 创建内存管理模块的静态库Mem_module
# 包含内核池内存管理、物理内存段管理等相关功能的实现文件
add_library(Mem_module OBJECT 
    kpoolmemmgr.cpp
    Memory.cpp
    pgtable_query.cpp
    phygpsmemmgr_freememmanage.cpp
    phygpsmemmgr_freememmanage_vaddrmanage.cpp
    phygpsmemmgr_init.cpp
    phygpsmemmgr_new_cr3.cpp
    phygpsmemmgr_print.cpp
    phymemSegsSubMgr.cpp
)

# 设置目标属性，开启位置无关代码（PIC）支持
# 使得生成的代码可以在内存中任意位置加载和执行
set_target_properties(Mem_module PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OBJECT_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/obj"
)

# 设置私有包含目录，指定编译时需要的头文件搜索路径
# 包括上层目录的include目录和当前目录下的includes目录
target_include_directories(Mem_module PRIVATE
"/usr/include/efi/"
${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/includes
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
         -fno-stack-protector
        #-fno-asynchronous-unwind-tables
         -DKERNEL_MODE
    )
    target_link_options(Mem_module PRIVATE -Tpid_module.ld)