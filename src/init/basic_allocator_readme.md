# Basic Allocator - EFI 内存管理器初始化

## 文件组织

### 头文件：`include/util/pages_alloc.h`
- `basic_allocator` 类定义
- `PHY_MEM_TYPE` 内存类型枚举
- 公共 API 声明

### 源文件：`pages_alloc.cpp`
- 静态成员变量定义
- 所有成员函数实现

## 功能概述

`basic_allocator::Init()` 函数负责处理 UEFI 传递的内存描述符表，完成以下三个核心任务:

1. **排序**: 按物理地址升序排列所有内存段
2. **填充空洞**: 检测并填充内存段之间的空隙
3. **回收内存**: 将 Loader 和 Boot Services 相关内存标记为可用 RAM

## API 接口

```cpp
class basic_allocator { 
public:
    static int Init(EFI_MEMORY_DESCRIPTORX64* memory_map_ptr, uint16_t entry_count);
};
```

### 参数说明

| 参数 | 类型 | 说明 |
|------|------|------|
| `memory_map_ptr` | `EFI_MEMORY_DESCRIPTORX64*` | UEFI 提供的内存描述符表指针 |
| `entry_count` | `uint16_t` | 描述符条目数量 |

### 返回值

| 返回值 | 含义 |
|--------|------|
| `0` | 成功 |
| `-1` | 参数错误 (空指针或 entry_count=0) |
| `-2` | 内存分配失败 (new list_doubly 失败) |

## 私有方法

### 1. sort_by_physical_address()

```cpp
static void sort_by_physical_address();
```

**功能**: 按物理地址升序排序 (冒泡排序)

**实现细节**:
- 使用 `Ktemplats::list_doubly` 的迭代器 API
- 原地交换元素值，不需要额外内存
- 时间复杂度 O(n²)，适合 EFI 内存表 (通常 < 1000 项)

### 2. fill_memory_holes()

```cpp
static void fill_memory_holes();
```

**功能**: 检测并填充内存空洞

**实现细节**:
- 遍历相邻内存段，检测地址不连续的区域
- 创建 `EFI_RESERVED_MEMORY_TYPE` 类型的描述符
- 通过重建链表实现插入操作

**示例**:
```
原始:
[0x00000000, 100 页]     [0x00100000, 200 页]
                ↑ 空洞 (1MB = 256 页)

填充后:
[0x00000000, 100 页] [Reserved, 256 页] [0x00100000, 200 页]
```

### 3. reclaim_loader_and_boot_services()

```cpp
static void reclaim_loader_and_boot_services();
```

**功能**: 回收 Loader 和 Boot Services 内存并合并相邻空闲段

**处理流程**:
1. **类型转换**: 遍历所有内存描述符，将 Loader/Boot Services 相关类型转换为 `freeSystemRam`
2. **合并空闲段**: 调用 `merge_adjacent_free_segments()` 合并所有物理地址连续的空闲段

**转换规则**:
| 原类型 | 转换后 | 说明 |
|--------|--------|------|
| `EFI_LOADER_CODE` | `freeSystemRam` | UEFI Loader 代码段 |
| `EFI_LOADER_DATA` | `freeSystemRam` | UEFI Loader 数据段 |
| `EFI_BOOT_SERVICES_CODE` | `freeSystemRam` | Boot Services 代码 |
| `EFI_BOOT_SERVICES_DATA` | `freeSystemRam` | Boot Services 数据 |

**合并示例**:
```
转换前:
[0x00000000, 100 页，freeSystemRam]
[0x00100000, 50 页，freeSystemRam] (原 EFI_LOADER_CODE)
[0x001A0000, 200 页，freeSystemRam]
[0x00200000, 30 页，RuntimeServices]

合并后:
[0x00000000, 350 页，freeSystemRam] ← 三段合并为一段
[0x00200000, 30 页，RuntimeServices]
```

### 4. merge_adjacent_free_segments()

```cpp
static void merge_adjacent_free_segments();
```

**功能**: 合并所有物理地址连续且类型为 `freeSystemRam` 的相邻内存段

**合并条件**:
1. 当前段和后继段的 Type 都是 `freeSystemRam`
2. 当前段的结束地址 = 后继段的起始地址 (物理地址连续)

**算法实现**:
```cpp
void merge_adjacent_free_segments() {
    auto it = memory_map->begin();
    while (it != memory_map->end()) {
        // 只处理 freeSystemRam 类型
        if (curr.Type != freeSystemRam) {
            ++it;
            continue;
        }
        
        // 尝试与后继段合并
        do {
            auto next_it = it;
            ++next_it;
            
            if (next_it == end()) break;
            
            // 检查类型和地址连续性
            if (next_desc.Type != freeSystemRam) break;
            if (curr_end != next_desc.PhysicalStart) break;
            
            // 合并：当前段页数增加，删除后继段
            curr.NumberOfPages += next_desc.NumberOfPages;
            memory_map->erase(next_it);
            
        } while (merged);
        
        ++it;
    }
}
```

**时间复杂度**: O(n),其中 n 是描述符数量

### 5. pages_set() - 精确内存类型设置

```cpp
static int pages_set(mem_interval interval, PHY_MEM_TYPE type);
```

**功能**: 设置指定物理内存区域的类型，自动处理段的分裂和重组

**核心特性**:
- ✅ **精确匹配**: 完全覆盖时直接修改类型
- ✅ **智能分裂**: 部分覆盖时自动分裂段
- ✅ **保持完整**: 未覆盖部分保持原有类型和属性
- ✅ **错误检测**: 跨越多个段时返回错误

**参数说明**:
| 参数 | 类型 | 说明 |
|------|------|------|
| `interval` | `mem_interval` | 内存区间 (起始地址和大小) |
| `type` | `PHY_MEM_TYPE` | 要设置的内存类型 |

**返回值**:
| 返回值 | 含义 |
|--------|------|
| `0` | 成功 |
| `-1` | 参数错误或未找到匹配的内存区域 |
| `-2` | 区域跨越多个描述符，无法安全设置 |

#### 段分裂场景详解

**场景 1: 完全匹配**
```
原始: [0x100000, 100 页，freeSystemRam]
设置：[0x100000, 100 页] → OS_KERNEL_CODE

结果: [0x100000, 100 页，OS_KERNEL_CODE]
操作：直接修改 Type 字段
```

**场景 2: 覆盖前部**
```
原始: [0x100000, 100 页，freeSystemRam]
设置：[0x100000, 40 页] → OS_KERNEL_CODE

结果: 
  [0x100000, 40 页，OS_KERNEL_CODE]     ← 修改为 new type
  [0x140000, 60 页，freeSystemRam]      ← 新建剩余段

操作:
  1. 修改当前段：Type=new_type, Pages=40
  2. 创建新段：Type=orig_type, Start=0x140000, Pages=60
  3. 在当前段后插入新段
```

**场景 3: 覆盖后部**
```
原始: [0x100000, 100 页，freeSystemRam]
设置：[0x160000, 40 页] → OS_KERNEL_CODE

结果:
  [0x100000, 60 页，freeSystemRam]      ← 新建前部段
  [0x160000, 40 页，OS_KERNEL_CODE]     ← 修改为 new type

操作:
  1. 创建新段：Type=orig_type, Start=0x100000, Pages=60
  2. 修改当前段：Start=0x160000, Pages=40, Type=new_type
  3. 在当前段前插入新段
```

**场景 4: 覆盖中间**
```
原始: [0x100000, 100 页，freeSystemRam]
设置：[0x120000, 20 页] → OS_KERNEL_CODE

结果:
  [0x100000, 32 页，freeSystemRam]      ← 前部空闲
  [0x120000, 20 页，OS_KERNEL_CODE]     ← 中间 new type
  [0x148000, 48 页，freeSystemRam]      ← 后部空闲

操作:
  1. 删除原段
  2. 创建三个新段并插入链表:
     - 前部：Type=orig_type, Start=0x100000, Pages=32
     - 中间：Type=new_type, Start=0x120000, Pages=20
     - 后部：Type=orig_type, Start=0x148000, Pages=48
```

**场景 5: 跨越多个段 (错误)**
```
原始:
  [0x100000, 50 页，freeSystemRam]
  [0x180000, 50 页，RuntimeServices]

设置：[0x140000, 80 页] → OS_KERNEL_CODE
      (跨越两个段)

结果：返回 -2 (错误)
```

#### 实现细节

**对齐验证**:
```cpp
// 必须 4KB 对齐
if (start % 0x1000 != 0 || interval.size % 0x1000 != 0) {
    return -1;
}
```

**交集检测**:
```cpp
// 检查是否与当前段有交集
if (start < desc_end && end > desc_start) {
    // 有交集，需要处理
}
```

**段分裂算法**:
```cpp
const uint64_t offset_start = start - desc_start;  // 相对偏移
const uint64_t offset_end = offset_start + size;   // 结束偏移
const uint64_t seg_size = desc.NumberOfPages * 0x1000;

// 根据 offset_start 和 offset_end 判断覆盖场景
if (offset_start == 0 && offset_end == seg_size) {
    // 完全匹配
} else if (offset_start == 0 && offset_end < seg_size) {
    // 覆盖前部
} else if (offset_start > 0 && offset_end == seg_size) {
    // 覆盖后部
} else if (offset_start > 0 && offset_end < seg_size) {
    // 覆盖中间
}
```

**属性继承**:
```
// 保存原始属性用于新建段
const uint64_t orig_attr = desc.Attribute;
const PHY_MEM_TYPE orig_type = desc.Type;

// 新建段时继承原属性
new_desc.Attribute = orig_attr;
new_desc.Type = orig_type;
```

#### 使用示例

```cpp
#include "util/pages_alloc.h"

void setup_memory_layout()
{
    // 示例 1: 标记内核代码段 (完全匹配)
    mem_interval kernel_code = {0x100000, 0x50000}; // 320KB
    basic_allocator::pages_set(kernel_code, OS_KERNEL_CODE);
    
    // 示例 2: 从大空闲块中分配内核栈 (覆盖前部)
    // 假设有一个大的 freeSystemRam 段 [0x200000, 1MB]
    mem_interval kernel_stack = {0x200000, 0x4000}; // 16KB
    basic_allocator::pages_set(kernel_stack, OS_KERNEL_STACK);
    // 结果:
    //   [0x200000, 16KB, OS_KERNEL_STACK]
    //   [0x204000, ~1MB, freeSystemRam]
    
    // 示例 3: 标记 MMIO 区域在段中间 (覆盖中间)
    // 假设有一段 [0x300000, 1MB, freeSystemRam]
    mem_interval mmio_region = {0x380000, 0x10000}; // 64KB 在中间
    basic_allocator::pages_set(mmio_region, EFI_MEMORY_MAPPED_IO);
    // 结果:
    //   [0x300000, 512KB, freeSystemRam]
    //   [0x380000, 64KB, EFI_MEMORY_MAPPED_IO]
    //   [0x390000, 512KB, freeSystemRam]
    
    // 示例 4: 错误处理 - 跨越多个段
    mem_interval cross_seg = {0x100000, 0x200000};
    int result = basic_allocator::pages_set(cross_seg, OS_KERNEL_DATA);
    if (result == -2) {
        kio::bsp_kout << "Error: Region spans multiple segments" << kio::kendl;
    }
}
```

#### 性能特点

**时间复杂度**: O(n + m)
- n: 查找目标段的迭代次数
- m: 重建链表的复制开销 (通常 m ≈ n)

**空间复杂度**: O(1)
- 只创建必要的新段描述符
- 不需要额外的缓存或辅助数据结构

**内存开销**:
- 每次分裂最多创建 2 个新描述符
- 每个描述符占用 48 字节 (EFI_MEMORY_DESCRIPTORX64 大小)

#### 注意事项

1. **4KB 对齐**: 起始地址和大小必须是 4KB 的整数倍
2. **原子性**: 操作要么完全成功，要么失败，不会留下部分修改
3. **线程安全**: 当前实现未加锁，适合单核 BSP 阶段使用
4. **属性继承**: 新建段自动继承原段的 Attribute 字段
5. **类型转换**: 可以任意转换类型，包括转换为相同的类型

#### 与 kernel.elf 对比

| 特性 | kernel.elf | init.elf (pages_set) |
|------|------------|---------------------|
| **段分裂** | ✅ 支持 | ✅ 支持 (更精确) |
| **属性保留** | ✅ 支持 | ✅ 支持 |
| **跨段检测** | ❌ 无明确检测 | ✅ 返回 -2 错误 |
| **对齐验证** | ⚠️ 部分验证 | ✅ 严格 4KB 对齐检查 |
| **实现复杂度** | 高 (线性表操作) | 中 (链表操作) |

这个实现确保了内存布局的精确性和一致性！🎉

### 6. pages_alloc()

```cpp
static phyaddr_t pages_alloc(uint64_t size);
```

**功能**: 分配指定大小的物理连续内存

**分配策略**: 首次适配 (First Fit)

**参数说明**:
| 参数 | 类型 | 说明 |
|------|------|------|
| `size` | `uint64_t` | 请求的大小 (字节) |

**返回值**:
| 返回值 | 含义 |
|--------|------|
| `phyaddr_t` | 成功返回分配的物理地址 (4KB 对齐) |
| `0` | 失败 (没有足够的空闲内存) |

**分配过程**:
1. 计算需要的页数 (向上取整到 4KB)
2. 遍历所有 `freeSystemRam` 类型的内存段
3. 找到第一个足够大的空闲块
4. 从该块中分配所需大小的内存
5. 如果块有剩余，分裂为已分配段 + 剩余空闲段
6. 返回分配的起始物理地址

**使用示例**:
```
// 分配 4KB 内存 (1 页)
phyaddr_t page1 = basic_allocator::pages_alloc(4096);
if (page1 == 0) {
    // 分配失败处理
}

// 分配 64KB 连续内存
phyaddr_t large_mem = basic_allocator::pages_alloc(65536);

// 分配后需要手动设置类型
if (large_mem != 0) {
    mem_interval alloc_range = {large_mem, 65536};
    basic_allocator::pages_set(alloc_range, OS_KERNEL_DATA);
}
```

**内存对齐**:
- 返回的地址保证 4KB 对齐
- 分配大小自动向上取整到 4KB 边界

**性能特点**:
- **时间复杂度**: O(n),其中 n 是内存描述符数量
- **空间复杂度**: O(1),不需要额外缓存
- **碎片化**: 可能产生外部碎片，建议配合内存合并使用

## 完整使用流程

```
#include "util/pages_alloc.h"

void init_memory_management(BootInfoHeader* header)
{
    // 1. 初始化内存分配器
    int result = basic_allocator::Init(
        header->memory_map,
        header->memory_map_entry_count
    );
    
    if (result != 0) {
        // 初始化失败处理
        return;
    }
    
    // 2. 分配内核栈
    constexpr uint64_t STACK_SIZE = 0x4000; // 16KB
    phyaddr_t kernel_stack = basic_allocator::pages_alloc(STACK_SIZE);
    if (kernel_stack == 0) {
        // 分配失败
        return;
    }
    
    // 3. 设置内核栈类型
    mem_interval stack_interval = {kernel_stack, STACK_SIZE};
    basic_allocator::pages_set(stack_interval, OS_KERNEL_STACK);
    
    // 4. 分配内核数据段
    constexpr uint64_t DATA_SIZE = 0x10000; // 64KB
    phyaddr_t kernel_data = basic_allocator::pages_alloc(DATA_SIZE);
    if (kernel_data != 0) {
        mem_interval data_interval = {kernel_data, DATA_SIZE};
        basic_allocator::pages_set(data_interval, OS_KERNEL_DATA);
    }
    
    // 5. 输出分配结果
    kio::bsp_kout << "Kernel Stack: 0x" << kernel_stack << kio::kendl;
    kio::bsp_kout << "Kernel Data: 0x" << kernel_data << kio::kendl;
}
```

## 数据结构

### EFI_MEMORY_DESCRIPTORX64

```cpp
typedef struct {
    UINT32     Type;              // 内存类型
    uint32_t   ReservedA;         // 保留
    
    EFI_PHYSICAL_ADDRESS  PhysicalStart;  // 物理起始地址
    EFI_VIRTUAL_ADDRESS   VirtualStart;   // 虚拟起始地址
    UINT64                NumberOfPages;  // 页数 (4KB/页)
    UINT64                Attribute;      // 属性位
    UINT64                ReservedB;      // 保留
} EFI_MEMORY_DESCRIPTORX64;
```

### PHY_MEM_TYPE 枚举

```cpp
typedef enum : uint32_t {
    EFI_RESERVED_MEMORY_TYPE = 0,
    EFI_LOADER_CODE = 1,
    EFI_LOADER_DATA = 2,
    EFI_BOOT_SERVICES_CODE = 3,
    EFI_BOOT_SERVICES_DATA = 4,
    EFI_RUNTIME_SERVICES_CODE = 5,
    EFI_RUNTIME_SERVICES_DATA = 6,
    freeSystemRam = 7,              // 可用系统 RAM
    EFI_UNUSABLE_MEMORY = 8,
    EFI_ACPI_RECLAIM_MEMORY = 9,
    EFI_ACPI_MEMORY_NVS = 10,
    EFI_MEMORY_MAPPED_IO = 11,
    OS_KERNEL_DATA = 0x80000001,
    OS_KERNEL_CODE = 0x80000002,
    OS_MEMSEG_HOLE = 0x80000005,
} PHY_MEM_TYPE;
```

## 使用示例

```
#include "util/pages_alloc.h"

extern "C" void KernelMain(
    EFI_MEMORY_DESCRIPTORX64* memory_map,
    uint16_t entry_count
) {
    int result = basic_allocator::Init(memory_map, entry_count);
    if (result != 0) {
        // 初始化失败处理
        return;
    }
    
    // 现在可以开始使用内存管理系统
}
```

## 处理流程

```
UEFI 内存图 (原始)
       ↓
复制到双链表 (new list_doubly)
       ↓
按物理地址排序 (sort_by_physical_address)
       ↓
检测并填充空洞 (fill_memory_holes)
       ↓
回收 Loader/Boot 内存 (reclaim_loader_and_boot_services)
       ↓
整理后的内存图 (存储在 static memory_map 中)
```

## 设计特点

### 1. 头源分离
- **头文件**: 纯声明，包含详细注释
- **源文件**: 完整实现，包含算法说明

### 2. 零依赖
- 仅依赖 `Ktemplats::list_doubly` 模板
- 不使用复杂的内存管理器
- 适合早期初始化阶段

### 3. 类型安全
- 使用 C++ 模板和迭代器
- 避免裸指针运算
- 强类型枚举防止误用

### 4. 错误处理
- 返回负值错误码
- 参数验证严格
- 内存分配失败可检测

## 与 kernel.elf 对比

| 特性 | kernel.elf (gBaseMemmgr) | init.elf (basic_allocator) |
|------|--------------------------|----------------------------|
| **数据结构** | 线性数组 | 双链表 |
| **排序算法** | 冒泡排序 | 冒泡排序 |
| **空洞填充** | linearTBSerialInsert | 链表插入 |
| **内存分配** | 静态预分配 | new/delete |
| **适用阶段** | 内核主阶段 | 早期初始化 |
| **文件大小** | ~500 行 | ~150 行 |

## 注意事项

1. **单例模式**: `memory_map` 是静态成员，整个 init.elf 生命周期唯一
2. **所有权转移**: Init 成功后，原始 `memory_map_ptr` 不再被使用
3. **线程安全**: 非线程安全，仅在 BSP 处理器上运行
4. **内存开销**: 每个节点额外需要 16 字节 (prev/next 指针)

## 后续扩展

当前只实现了 `Init()` 方法，预留接口待实现:
- `static void* alloc(uint64_t size)` - 分配内存
- `static void free(void* ptr)` - 释放内存

这些方法将基于整理后的内存描述符表实现简单的页分配器。
