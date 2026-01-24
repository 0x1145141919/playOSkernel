#CMakeLists.txt

cmake_minimum_required(VERSION 3.15)  # 推荐使用较新版本以支持现代特性
project(Kernel LANGUAGES C CXX)       # 显式声明支持 C 和 C++
# 设置构建类型变量，默认为Debug
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build (Debug or Release)" FORCE)
endif()
# 查找 NASM 汇编器
find_program(NASM_EXECUTABLE nasm)
if(NOT NASM_EXECUTABLE)
    message(FATAL_ERROR "NASM assembler not found!")
endif()


    add_definitions(-D_GNU_SOURCE)  # 有时候需要这个

# 设置 NASM 编译选项（例如：-f elf64 或 -f win32）
set(NASM_FLAGS "-f elf64")
# 定义汇编文件编译规则
enable_language(ASM_NASM)  # CMake 3.8+ 支持
set(CMAKE_ASM_NASM_SOURCE_FILE_EXTENSIONS asm)  # 关联 .asm 后缀

# 设置 NASM 编译器和选项
set(CMAKE_ASM_NASM_COMPILER ${NASM_EXECUTABLE})
set(CMAKE_ASM_NASM_COMPILE_OBJECT "<CMAKE_ASM_NASM_COMPILER> <FLAGS> -gdwarf -o <OBJECT> <SOURCE>")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcmodel=large")
# 根据构建类型设置不同的编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(STATUS "Building in DEBUG mode")
    # 在Debug模式下添加调试信息并禁用优化
    add_compile_options(-g -O0 -gdwarf-4)
    set(DEBUG_LINK_FLAGS "-g")
else()
    message(STATUS "Building in RELEASE mode")
    add_compile_options(-O2)
endif()
# 语言特定的选项（可选）
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=c23")          # C 标准
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++23")    # C++ 标准
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_KEEP_BUILD_FILES ON)
# 定义通用源文件
set(SRC_COMMON
 #USERMODE需要替换成对应的标准库
    src/utils/util.cpp
    src/core_hardwares/VideoDriver.cpp
    src/core_hardwares/PortDriver.cpp
    src/utils/kintrin.cpp
    src/panic.cpp
    src/utils/bitmap.cpp
    src/utils/lock.cpp
    src/utils/RB_btree.cpp
    src/utils/kout.cpp
    src/utils/intel_cpuid.cpp
)

# 添加gnu-efi作为外部项目
include(ExternalProject)
ExternalProject_Add(
    gnu-efi
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/outsbmodules/gnu-efi
    CONFIGURE_COMMAND ""  # 跳过 configure 阶段
    BUILD_COMMAND make    # 调用 make 编译
    INSTALL_COMMAND ""     # 不安装
    BUILD_IN_SOURCE TRUE   # 在源目录构建
)
add_custom_target(kernel_resources
)
add_custom_target(ksymbols
    COMMAND ./symbols_dumper
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "运行自定义目标"
)
add_dependencies(ksymbols symbols_dumper)
add_custom_target(initramfs
    COMMAND ./mkinitramfs
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "运行自定义目标"
)
add_dependencies(initramfs mkinitramfs)
add_dependencies(kernel_resources 
    kernel.elf 
    initramfs
    ksymbols
) 
# 获取gnu-efi构建目录路径
ExternalProject_Get_Property(gnu-efi SOURCE_DIR)
set(GNU_EFI_SOURCE_DIR ${SOURCE_DIR})
add_subdirectory(src/memory)
add_subdirectory(outsbmodules/tinySTL)
add_subdirectory(src/fs)
add_executable(symbols_dumper
    build_utils/ksymbols_dumper.cpp
)
# 内核可执行文件
add_executable(kernel.elf
    src/firmware/UefiRunTimeServices.cpp
    src/kinit.cpp
        src/utils/kcirclebufflogMgr.cpp
        src/kernel_entry.asm
        src/utils/int128_div.cpp
        src/Interrupts/lapic.cpp
        src/Interrupts/exceptions_handler.cpp
        src/Processor/local_processor_manage.cpp
        src/firmware/APIC_convert_functions.cpp
        src/firmware/APIC_table_analyzer.cpp
        src/Interrupts/Sysdef_exception_entries.asm
        src/core_hardwares/HPET.cpp
        src/time.cpp
        src/utils/kptrace.cpp
        src/firmware/acpimgr.cpp
        $<TARGET_OBJECTS:Mem_module>
    ${SRC_COMMON}
)
add_executable(mkinitramfs
    src/tests/fs_test_main.cpp
    src/drivers/MemoryDisk.cpp
     ${SRC_COMMON}
)

target_include_directories(
    mkinitramfs PRIVATE
    "/usr/include/efi"
     "${CMAKE_SOURCE_DIR}/src/include"
)
target_compile_options(
    mkinitramfs PRIVATE
    -DUSER_MODE
    -fsanitize=address
)

# 链接gnu-efi库
target_link_libraries(mkinitramfs PRIVATE
    fs_user_test
    -fsanitize=address
)


add_executable(fs_test
    src/tests/fs_test_main.cpp
    src/drivers/MemoryDisk.cpp
     ${SRC_COMMON}
)
target_include_directories(
    fs_test PRIVATE
    "/usr/include/efi"
     "${CMAKE_SOURCE_DIR}/src/include"
)
target_compile_options(
    fs_test PRIVATE
    -DUSER_MODE
    -fsanitize=address
)

target_link_libraries(kernel.elf PRIVATE 
    ${GNU_EFI_SOURCE_DIR}/x86_64/lib/libefi.a
    ${GNU_EFI_SOURCE_DIR}/x86_64/gnuefi/libgnuefi.a
    fs_static
)



target_include_directories(kernel.elf PRIVATE
    "${CMAKE_SOURCE_DIR}/src/include"
    "${GNU_EFI_SOURCE_DIR}/inc"
    "${GNU_EFI_SOURCE_DIR}/inc/x86_64"
)

# 内核链接选项
set(KERNEL_LINK_OPTS
-nostdlib
    -ffreestanding
    -fno-use-cxa-atexit
    -fno-exceptions
    -fno-builtin
    -fno-rtti
    -fno-asynchronous-unwind-tables
    -ftls-model=local-exec
    -static
    -T${CMAKE_SOURCE_DIR}/kld.ld
    -lgcc
    -Wl,--wrap=__stack_chk_fail
)

# 如果在Debug模式下，添加调试链接标志
if(DEBUG_LINK_FLAGS)
    list(APPEND KERNEL_LINK_OPTS ${DEBUG_LINK_FLAGS})
endif()

# 应用编译选项
target_compile_options(kernel.elf PRIVATE 
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra  -mgeneral-regs-only
    -ffreestanding
    -fno-stack-protector 
    -fno-exceptions
    -fno-builtin
    -fno-rtti
    -static
    -mgeneral-regs-only
    -ftls-model=local-exec
    -fno-lto
    -fpermissive
    -fno-PIC
    -DKERNEL_MODE>
    -DPGLV_4>
$<$<COMPILE_LANGUAGE:ASM_NASM>:-felf64>
)
target_link_options(kernel.elf PRIVATE ${KERNEL_LINK_OPTS})