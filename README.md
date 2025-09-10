# UEFI 内核项目

这是一个为 UEFI 环境设计的操作系统内核项目，使用 C/C++ 编写，采用 CMake 构建系统进行项目管理。

## 项目概述

本项目是一个用于 UEFI 环境下的内核模块，主要实现内存管理、设备驱动等底层操作系统功能。项目目标是构建一个可在 UEFI 环境下运行的最小操作系统内核。

## 项目结构

```
kernel/
├── CMakeLists.txt                 # 项目构建配置文件
├── linker_for_kernel.ld           # 内核链接脚本
├── src/
│   ├── kinit.cpp                  # 内核初始化入口
│   ├── panic.cpp                  # 内核恐慌处理
│   ├── panic.h                    # 内核恐慌处理头文件
│   ├── KernelEntryPointDefinetion.h # 内核入口点定义
│   ├── drivers/                   # 驱动程序模块
│   │   ├── PortDriver.c           # 端口驱动实现
│   │   ├── PortDriver.h           # 端口驱动头文件
│   │   ├── VideoDriver.cpp        # 视频驱动实现
│   │   └── VideoDriver.h          # 视频驱动头文件
│   ├── include/                   # 头文件目录
│   ├── memory/                    # 内存管理模块
│   ├── tests/                     # 测试模块
│   └── utils/                     # 工具函数模块
├── docs/                          # 文档目录
└── README.md                      # 项目说明文档
```

## 构建配置

### CMake 配置

项目使用 CMake 3.15 或更高版本进行构建管理，在 [CMakeLists.txt](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/CMakeLists.txt) 中定义了以下配置：

- 项目语言：C 和 C++
- 默认构建类型：Release（可选 Debug）
- C 标准：C23
- C++ 标准：C++23

### 编译选项

#### 通用编译选项
- `-Wall` - 启用常见警告
- `-Wextra` - 启用额外警告
- `-fno-stack-protector` - 禁用栈保护

#### 构建类型特定选项
- Debug 模式：`-g -O0`（调试信息，无优化）
- Release 模式：`-O3`（优化级别3）

#### 内核专用编译选项
- `-nostdlib` - 不链接标准库
- `-ffreestanding` - 独立环境编译
- `-fno-use-cxa-atexit` - 禁用 atexit 机制
- `-fno-exceptions` - 禁用异常处理
- `-fno-builtin` - 禁用内置函数
- `-static` - 静态链接
- `-DKERNEL_MODE` - 内核模式宏定义

### 链接选项

内核链接使用自定义链接脚本 [linker_for_kernel.ld](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/linker_for_kernel.ld)，并包含以下选项：
- `-nostdlib` - 不链接标准库
- `-ffreestanding` - 独立环境链接
- `-fno-use-cxa-atexit` - 禁用 atexit 机制
- `-fno-exceptions` - 禁用异常处理
- `-fno-builtin` - 禁用内置函数
- `-static` - 静态链接
- `-lgcc` - 链接 GCC 库

## 模块说明

### 内核核心模块

#### 内存管理模块 (`src/memory/`)
- [Memory.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/memory/Memory.cpp) - 内存管理基础实现
- [kpoolmemmgr.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/memory/kpoolmemmgr.cpp) - 内核池内存管理器
- [phygpsmemmgr_init.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/memory/phygpsmemmgr_init.cpp) - 物理内存管理器初始化
- [phygpsmemmgr_print.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/memory/phygpsmemmgr_print.cpp) - 物理内存管理器打印功能
- [phygpsmemmgr_freememmanage.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/memory/phygpsmemmgr_freememmanage.cpp) - 物理内存空闲管理
- [pgtable_query.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/memory/pgtable_query.cpp) - 页表查询功能
- [gBitmapFreePhyMemMgr.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/memory/gBitmapFreePhyMemMgr.cpp) - 位图物理内存管理器

#### 工具模块 (`src/utils/`)
- [util.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/utils/util.cpp) - 通用工具函数
- [kcirclebufflogMgr.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/utils/kcirclebufflogMgr.cpp) - 环形缓冲区日志管理器


#### 驱动模块 (`src/drivers/`)
- [VideoDriver.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/drivers/VideoDriver.cpp) - 视频驱动
- [PortDriver.c](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/drivers/PortDriver.c) - 端口驱动

#### 测试模块 (`src/tests/`)
- [Memory_moudle_test.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/tests/Memory_moudle_test.cpp) - 内存模块测试主程序
- [test_gBitmapFreePhyMemMgr.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/tests/test_gBitmapFreePhyMemMgr.cpp) - 位图物理内存管理器测试
- [test_kpoolmemmgr.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/tests/test_kpoolmemmgr.cpp) - 内核池内存管理器测试

### 可执行文件

#### 内核镜像 ([kernel.elf](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/kernel.elf))
主内核可执行文件，包含所有核心模块，从 [src/kinit.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/kinit.cpp) 开始执行。

#### 内存模块测试 ([memmory_module_test](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/memmory_module_test))
专门用于测试内存管理模块的可执行文件，启用 `-DTEST_MODE` 宏。

## 链接脚本

[linker_for_kernel.ld](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/linker_for_kernel.ld) 定义了内核的内存布局：

- 入口点: `_kernel_Init`
- 内存区域: 128MB 从地址 0x4000000 开始
- 段划分:
  - `.text` - 代码段（只读可执行）
  - `.rodata` - 只读数据段
  - `.data` - 已初始化数据段
  - `.heap` - 堆段（4MB）
  - `.klog` - 内核日志段（2MB）
  - `.bss` - 未初始化数据段
  - `.stack` - 栈段（64KB）
  - `.eh_frame` - C++ 异常处理信息

## 内核初始化

内核入口点为 `_kernel_Init` 函数，位于 [src/kinit.cpp](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/src/kinit.cpp) 中。初始化过程包括：

1. 初始化全局图形信息
2. 初始化串口
3. 初始化日志管理器
4. 初始化内核内存池管理器
5. 初始化全局字符集位图控制器
6. 初始化内核 shell 控制器
7. 初始化物理内存管理器

## 构建说明

### 环境要求
- CMake 3.15 或更高版本
- 支持 C23 和 C++23 标准的编译器
- EFI 开发环境头文件（位于 `/usr/include/efi/`）

### 构建步骤

1. 创建构建目录：
   ```bash
   mkdir build
   cd build
   ```

2. 配置项目：
   ```bash
   # Release 模式（默认）
   cmake ..
   
   # 或 Debug 模式
   cmake -DCMAKE_BUILD_TYPE=Debug ..
   ```

3. 编译项目：
   ```bash
   make
   ```

构建后将生成两个可执行文件：
- [kernel.elf](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/kernel.elf) - 主内核镜像
- [memmory_module_test](file:///home/pangsong/PS_git/OS_pj_uefi/kernel/memmory_module_test) - 内存模块测试程序

## 头文件路径

项目包含以下头文件搜索路径：
- `/usr/include/efi/` - EFI 开发环境头文件
- 项目根目录
- `src/include/` - 项目专用头文件目录

## 开发模式

项目支持两种构建模式：
1. **Release 模式** - 默认模式，启用优化选项
2. **Debug 模式** - 开发调试模式，包含调试信息

通过设置 `CMAKE_BUILD_TYPE` 变量选择构建模式：
```bash
cmake -DCMAKE_BUILD_TYPE=Debug ..
```