# UEFI 内核项目

这是一个为 UEFI 环境设计的操作系统内核项目，使用 C/C++ 编写，采用 CMake 构建系统进行项目管理。

## 项目概述

本项目是一个用于 UEFI 环境下的内核模块，主要实现内存管理、设备驱动、文件系统等底层操作系统功能。项目目标是构建一个可在 UEFI 环境下运行的最小操作系统内核。

## 项目结构

```
kernel/
├── CMakeLists.txt                 # 根项目构建配置文件
├── kld.ld                         # 内核链接脚本
├── src/
│   ├── kinit.cpp                  # 内核初始化入口
│   ├── kernel_entry.asm           # 内核入口汇编代码
│   ├── KernelEntryPointDefinetion.h # 内核入口点定义
│   ├── panic/                     # 内核恐慌处理模块
│   ├── Processor/                 # 处理器相关管理模块
│   ├── Interrupts/                # 中断处理模块
│   ├── efi_about/                 # EFI 运行时服务接口
│   ├── drivers/                   # 设备驱动模块
│   │   ├── PortDriver.cpp         # 端口驱动实现
│   │   ├── VideoDriver.cpp        # 视频驱动实现
│   │   └── MemoryDisk.cpp         # 内存磁盘驱动实现
│   ├── include/                   # 公共头文件目录
│   ├── memory/                    # 内存管理模块
│   ├── fs/                        # 文件系统模块
│   ├── utils/                     # 工具函数模块
│   ├── tests/                     # 测试模块
│   ├── acpi_subsystems/           # ACPI 子系统模块
│   └── virtualzition_about/       # 虚拟化相关模块
├── outsbmodules/                  # 外部模块
│   ├── gnu-efi                    # GNU-EFI 库
│   └── tinySTL                    # 简化版 STL 实现
└── README.md                      # 项目说明文档
```

## 构建配置

### CMake 配置

项目使用 CMake 3.15 或更高版本进行构建管理，在 [CMakeLists.txt](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/CMakeLists.txt) 中定义了以下配置：

- 项目语言：C、C++ 和 ASM_NASM
- 默认构建类型：Debug（可选 Release）
- C 标准：C23
- C++ 标准：C++23
- 汇编器：NASM（要求版本支持 elf64 格式）

### 编译选项

#### 通用编译选项
- `-Wall -Wextra` - 启用常见警告和额外警告
- `-ffreestanding` - 独立环境编译
- `-fno-exceptions` - 禁用异常处理
- `-fno-builtin` - 禁用内置函数
- `-fno-rtti` - 禁用运行时类型信息
- `-static` - 静态链接
- `-DKERNEL_MODE` - 内核模式宏定义

#### 构建类型特定选项
- Debug 模式：`-g -O0 -gdwarf-4`（调试信息，无优化）
- Release 模式：`-O3`（优化级别3）

#### 内核专用编译选项
- `-mgeneral-regs-only` - 仅使用通用寄存器
- `-fno-use-cxa-atexit` - 禁用 atexit 机制
- `-fno-PIC` - 禁用位置无关代码
- `-mcmodel=large` - 使用 large 内存模型

### 链接选项

内核链接使用自定义链接脚本 [kld.ld](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/kld.ld)，并包含以下选项：
- `-nostdlib` - 不链接标准库
- `-ffreestanding` - 独立环境链接
- `-fno-use-cxa-atexit` - 禁用 atexit 机制
- `-fno-exceptions` - 禁用异常处理
- `-fno-builtin` - 禁用内置函数
- `-static` - 静态链接
- `-lgcc` - 链接 GCC 库
- `-T kld.ld` - 指定链接脚本

### 链接脚本内存布局

[kld.ld](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/kld.ld) 定义了如下内存布局：
- `.init_text` - 初始化代码段（物理地址 0x1000 开始）
- `.text_main` - 主代码段（虚拟地址 0xFFFF800000000000 + 0x4000000 开始）
- `.data` - 数据段
- `.rodata` - 只读数据段
- `.stack` - 内核栈（64KB）
- `.heap` - 内核堆（16MB）
- `.pgtb_heap` - 页表堆（4MB）
- `.klog` - 内核日志区域（2MB）

## 模块说明

### 内核核心模块

#### 内存管理模块 (`src/memory/`)
实现物理内存管理和内核池内存管理：
- [Memory.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/memory/Memory.cpp) - 内存管理基础实现
- [kpoolmemmgr.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/memory/kpoolmemmgr.cpp) - 内核池内存管理器
- [phygpsmemmgr_init.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/memory/phygpsmemmgr_init.cpp) - 物理内存管理器初始化
- [phygpsmemmgr_print.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/memory/phygpsmemmgr_print.cpp) - 物理内存管理器打印功能
- [phygpsmemmgr_freememmanage.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/memory/phygpsmemmgr_freememmanage.cpp) - 物理内存空闲管理
- [pgtable_query.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/memory/pgtable_query.cpp) - 页表查询功能
- [phymemSegsSubMgr.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/memory/phymemSegsSubMgr.cpp) - 物理内存段子管理器

#### 文件系统模块 (`src/fs/`)
实现了自定义的 INIT 文件系统：
- [init_fs.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/fs/init_fs.cpp) - 文件系统主模块
- [initfs_bits_operations.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/fs/initfs_bits_operations.cpp) - 位图操作
- [initfs_inode_content_read.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/fs/initfs_inode_content_read.cpp) - inode 内容读取
- [initfs_inode_content_write.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/fs/initfs_inode_content_write.cpp) - inode 内容写入
- [initfs_inode_operations.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/fs/initfs_inode_operations.cpp) - inode 操作
- [initfs_inode_resize_about.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/fs/initfs_inode_resize_about.cpp) - inode 大小调整相关

文件系统特点：
- 分层结构设计，支持动态分区大小调整
- 块组管理机制
- inode 位图和簇位图管理
- extent 存储方式和索引表存储方式
- 支持目录和文件操作

#### 驱动模块 (`src/drivers/`)
- [VideoDriver.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/drivers/VideoDriver.cpp) - 视频驱动
- [PortDriver.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/drivers/PortDriver.cpp) - 端口驱动
- [MemoryDisk.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/drivers/MemoryDisk.cpp) - 内存磁盘驱动

#### 工具模块 (`src/utils/`)
- [util.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/utils/util.cpp) - 通用工具函数
- [kcirclebufflogMgr.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/utils/kcirclebufflogMgr.cpp) - 环形缓冲区日志管理器
- [kintrin.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/utils/kintrin.cpp) - 内核内在函数实现

#### 外部依赖模块 (`outsbmodules/`)

##### tinySTL
简化版的 STL 实现，包括：
- 容器：vector, list, deque, stack, queue, priority_queue, set, map 等
- 迭代器支持
- 算法库
- 类型特征

##### gnu-efi
GNU-EFI 库，提供 UEFI 环境下的运行时支持。

#### 测试模块 (`src/tests/`)
- [test_main.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/tests/test_main.cpp) - 主测试程序
- [test_kpoolmemmgr.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/tests/test_kpoolmemmgr.cpp) - 内核池内存管理器测试
- [fs_test_main.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/tests/fs_test_main.cpp) - 文件系统测试

## 构建说明

```bash
mkdir build
cd build
cmake ..
make
```

这将生成 `kernel.elf` 内核镜像文件。