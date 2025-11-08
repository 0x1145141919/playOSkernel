# 文件系统模块的CMakeLists.txt

# 定义文件系统源文件
set(FS_SOURCES
    init_fs.cpp
    initfs_bits_operations.cpp
    initfs_inode_content_read.cpp
    initfs_inode_content_write.cpp
    initfs_inode_operations.cpp
    initfs_inode_resize_about.cpp
    initfs_out_surfaces.cpp
    initfs_pathanalyze_about.cpp
)

# 创建静态库供内核使用 (KERNEL_MODE)
add_library(fs_static STATIC ${FS_SOURCES})

# 设置静态库的包含目录
target_include_directories(fs_static PUBLIC
    ${CMAKE_SOURCE_DIR}/src/include
    /usr/include/efi
)

# 设置静态库的编译选项
target_compile_options(fs_static PRIVATE
    -DKERNEL_MODE
    -ffreestanding
    -nostdlib
    -fno-exceptions
    -fno-rtti
    -fno-builtin
    -static
)

# 创建动态库供用户空间使用 (USER_MODE)
add_library(fs_user_test STATIC ${FS_SOURCES})

# 设置动态库的包含目录
target_include_directories(fs_user_test PUBLIC
    ${CMAKE_SOURCE_DIR}/src/include
    /usr/include/efi
)

# 设置动态库的编译选项
target_compile_options(fs_user_test PRIVATE
    -DUSER_MODE
    -fsanitize=address
)

# 设置库的属性
set_target_properties(fs_static PROPERTIES
    OUTPUT_NAME "fs"
)

set_target_properties(fs_user_test PROPERTIES
    OUTPUT_NAME "fsuser"
    VERSION 1.0
    SOVERSION 1
)