# 物理内存页管理模块设计文档

## 概述

本模块 (`PgsMemMgr`) 是一个用于管理物理内存的复杂系统，通过多级页表结构实现对物理内存的精细管理。系统支持四级或五级分页模式，能够管理最大128TB（四级）或64PB（五级）的物理内存空间。

## 核心数据结构

### 内存类型枚举
```cpp
enum Phy_mem_type : uint8_t {
    FREE = 0,       // 空闲内存
    OCCUPYIED = 1,  // 已占用内存
    RESERVED = 2    // 保留内存
};
```

### 页标志位结构 (pgflags)
```cpp
struct pgflags {
    uint64_t physical_or_virtual_pg:1;  // 0=物理页, 1=虚拟页
    uint64_t is_exist:1;                // 页是否存在
    uint64_t is_atom:1;                 // 是否为原子节点
    uint64_t is_dirty:1;                // 脏页标志
    uint64_t is_lowerlv_bitmap:1;       // 下级使用位图管理
    uint64_t is_locked:1;               // 页是否被锁定
    uint64_t is_shared:1;               // 页是否共享
    uint64_t is_reserved:1;             // 页是否保留
    uint64_t is_occupied:1;             // 页是否被占用
    uint64_t is_kernel:1;               // 内核内存标志
    uint64_t is_readable:1;             // 可读权限
    uint64_t is_writable:1;             // 可写权限
    uint64_t is_executable:1;           // 可执行权限
    uint64_t is_remaped:1;              // 重映射标志
    uint64_t pg_lv:3;                   // 页级别(0-4)
};
```

### 页控制块头 (PgControlBlockHeader)
核心管理结构，包含页标志和指向下级结构的指针：
```cpp
struct PgControlBlockHeader {
    pgflags flags;
    union {
        lowerlv_PgCBtb* lowerlvPgCBtb;          // 指向下级页表
        lowerlv_bitmap_entry_width1bit* map_tpye1; // 1位宽位图
        lowerlv_bitmap_entry_width2bits* map_tpye2; // 2位宽位图
        void* pgextentions;                     // 扩展数据
    } base;
};
```

### 位图管理结构
系统使用两种位图进行精细内存管理：
- `lowerlv_bitmap_entry_width1bit`: 1位宽位图，用于普通内存管理
- `lowerlv_bitmap_entry_width2bits`: 2位宽位图，用于包含保留内存的区域

## 类设计：PgsMemMgr

### 主要功能
1. 物理内存分配和释放
2. 多级页表结构管理
3. 内存使用情况查询
4. 页表优化和压缩

### 核心成员变量
```cpp
uint8_t cpu_pglv;                    // CPU分页模式(4或5级)
uint64_t kernel_space_cr3;           // 内核空间CR3寄存器值
psmemmgr_flags_t flags;              // 内存管理器标志
PgControlBlockHeader* rootlv4PgCBtb; // 四级页表根节点
```

### 函数指针数组
系统使用函数指针数组实现多级页表的统一操作接口：
```cpp
// 页表构建函数数组
int (PgsMemMgr::*PgCBtb_construct_func[5])(phyaddr_t, pgflags);

// 页表查询函数数组  
PgControlBlockHeader (PgsMemMgr::*PgCBtb_query_func[5])(phyaddr_t);

// 页管理函数数组
int (PgsMemMgr::*PgCBtb_pgmanage_func[5])(phyaddr_t, bool, pgaccess);
```

### 公有接口
```cpp
// 内存分配接口
void* pgs_allocate(size_t size_in_byte, pgaccess access, uint8_t align_require = 12);
int pgs_fixedaddr_allocate(phyaddr_t addr, size_t size_in_byte, pgaccess access, PHY_MEM_TYPE type);

// 内存释放接口
int pgs_free(void* addr, size_t size_in_byte);

// 内存清理接口
int pgs_clear(void* addr, size_t size_in_byte);

// 查询接口
PgControlBlockHeader pgbase_search(phyaddr_t addr);
phy_memDesriptor* queryPhysicalMemoryUsage(phyaddr_t base, uint64_t len_in_bytes);

// 初始化接口
void Init();
void PrintPgsMemMgrStructure();
```

## 内存管理特性

### 1. 多级页表管理
系统支持4级或5级页表结构，每级页表包含512个表项，形成树状管理结构。
为了兼容
### 2. 位图优化
对于全是原子节点的表项，系统使用位图进行压缩管理：
- 1位位图：用于普通内存区域，标记页是否空闲
- 2位位图：用于包含保留内存的区域，提供更详细的状态信息

### 3. 权限管理
通过`pgaccess`结构实现对内存权限的精细控制：
- 内核/用户空间区分
- 读/写/执行权限控制

### 4. 内存类型支持
系统支持多种内存类型，包括：
- EFI标准内存类型
- 操作系统特有内存类型
- 保留内存区域
- OEM定制内存类型

## 设计特点

1. **灵活的页表结构**：支持动态创建和销毁多级页表
2. **高效的内存查询**：通过多级索引快速定位物理页信息
3. **精细的权限控制**：支持内核/用户空间和读/写/执行权限管理
4. **内存优化**：使用位图压缩减少管理开销
5. **兼容性**：支持多种内存类型和EFI标准

## 使用限制

1. 内存分配器必须在`is_pgsallocate_enable`标志开启后才能使用
2. 低物理地址恒等映射区间默认且必须全部设为内核内存
3. 具有执行权限的内存页不能使用位图管理
4. 内存操作需要至少12位（4KB）对齐

## 性能考虑

- 使用位图压缩减少管理结构的内存占用
- 多级页表结构提供O(log n)的查询效率
- 函数指针数组实现统一的操作接口，避免条件判断开销
1. 多级页表构建函数（PgCBtb_lvX_entry_construct）

这些函数用于构建多级页表结构。每个级别的构建函数负责处理该级别的页表项创建。

    实现思路：

        根据物理地址提取当前级别的索引。

        检查当前级别的页表项是否存在，如果不存在则创建新的页表（或位图结构）。

        如果已经存在，则根据标志位判断是否需要继续向下级构建。

        对于位图管理的页表，需要初始化位图的所有位。

    注意事项：

        构建过程中会直接复制传入的flags，不进行检查。因此调用者需要确保flags的正确性。

        对于位图管理的页表，需要根据is_reserved标志选择使用1位还是2位宽度的位图。

2. 多级页表查询函数（PgCBtb_lvX_entry_query）

这些函数用于查询指定物理地址对应的页表项。

    实现思路：

        根据物理地址逐级提取索引，遍历多级页表直到找到目标页表项。

        如果中间某一级的页表项不存在，则返回一个表示不存在的页表项（例如，设置is_exist为0）。

    注意事项：

        查询过程中需要处理位图管理的页表，此时需要计算位图中的具体位。

3. 页管理函数（PgCBtb_lvX_entry_pgmanage）

这些函数用于分配或释放一个原子页。

    实现思路：

        首先检查目标页是否是原子页，如果不是则报错。

        检查页的保留状态，试图操作保留页会报错。

        如果是分配操作，检查页是否已经被占用，如果未被占用则标记为占用，并设置权限。

        如果是释放操作，检查页是否被占用，如果被占用则标记为空闲。

    注意事项：

        对于位图管理的页，需要操作位图中的相应位。

        权限设置需要符合上级页表的权限（例如，上级页表没有写权限，则下级不能分配有写权限的页）。

4. 预优化函数（pre_pgtb_optimize）和优化节点（optimize_node）

这些函数用于对页表进行优化，主要是将全是原子节点的表项转换为位图管理。

    实现思路：

        遍历页表树，对于每个节点，检查其所有子节点是否都是原子节点且具有相同的权限和类型。

        如果满足条件，则将该节点转换为位图管理，并释放原来的子页表结构，改为位图。

        对于位图，根据是否存在保留页决定使用1位还是2位位图。

    注意事项：

        转换位图后，原来子页表项的状态需要正确地转换到位图中。

        优化过程可能需要递归进行。

pgflags标志位组合及其影响
1. 物理/虚拟页标志（physical_or_virtual_pg）

    0：物理页，表示该页表项管理的是物理内存。

    1：虚拟页，表示该页表项管理的是虚拟内存（可能涉及交换空间）。

2. 存在标志（is_exist）

    0：该页不存在。对于物理页，表示未分配；对于虚拟页，表示可能在交换空间。

    1：该页存在。

3. 原子节点标志（is_atom）

    0：非原子节点，表示该页表项下面还有子页表。

    1：原子节点，表示该页表项管理的是最小颗粒度的页（如4KB）。

4. 下级位图管理标志（is_lowerlv_bitmap）

    0：下级使用页表数组管理。

    1：下级使用位图管理。此时，该页表项必须是非原子节点（is_atom=0），且下级所有页表项都是原子节点。

5. 保留标志（is_reserved）

    0：普通内存区域。

    1：保留内存区域。当与is_lowerlv_bitmap结合时，表示下级位图使用2位宽度（因为需要区分保留和占用状态）；否则，使用1位宽度。

6. 占用标志（is_occupied）

    仅当physical_or_virtual_pg=0（物理页）、is_exist=1且is_atom=1时有效。

    0：空闲页。

    1：占用页。

7. 内核标志（is_kernel）

    0：用户内存。

    1：内核内存。低物理地址恒等映射内存必须设置为内核内存。

8. 权限标志（is_readable, is_writable, is_executable）

    控制内存的访问权限。注意：具有可执行权限的内存不能使用位图管理。

9. 重映射标志（is_remaped）

    表示该物理页是否被重映射（无论内核还是用户态）。

10. 页级别（pg_lv）

    表示该页表项所在的级别（0-4），0级为最底层（如4KB页），4级为最高级。

标志位组合示例
示例1：普通原子页
cpp

pgflags flags;
flags.physical_or_virtual_pg = 0; // 物理页
flags.is_exist = 1;               // 存在
flags.is_atom = 1;                // 原子节点
flags.is_occupied = 1;            // 占用
// 其他标志根据实际情况设置

示例2：使用位图管理的非原子页
cpp

pgflags flags;
flags.physical_or_virtual_pg = 0; // 物理页
flags.is_exist = 1;               // 存在
flags.is_atom = 0;                // 非原子节点
flags.is_lowerlv_bitmap = 1;      // 下级使用位图
flags.is_reserved = 0;            // 无保留页，使用1位位图

示例3：包含保留页的位图管理
cpp

pgflags flags;
flags.physical_or_virtual_pg = 0; // 物理页
flags.is_exist = 1;               // 存在
flags.is_atom = 0;                // 非原子节点
flags.is_lowerlv_bitmap = 1;      // 下级使用位图
flags.is_reserved = 1;            // 有保留页，使用2位位图

对其他结构的影响
1. 页控制块头（PgControlBlockHeader）

    根据标志位，联合体base将指向不同的结构：

        如果is_atom为0且is_lowerlv_bitmap为0，则使用lowerlvPgCBtb（页表数组）。

        如果is_atom为0且is_lowerlv_bitmap为1，则根据is_reserved选择map_tpye1（1位位图）或map_tpye2（2位位图）。

        如果is_atom为1，则联合体可能未被使用（因为原子节点没有下级结构）。

2. 位图结构（lowerlv_bitmap_entry_width1bit 和 lowerlv_bitmap_entry_width2bits）

    1位位图：每个位表示一个页的空闲（0）或占用（1）状态。

    2位位图：每两个位表示一个页的状态，可以表示四种状态（例如：00=空闲，01=占用，10=保留，11=未使用或错误）。

3. 内存描述符（phy_memDesriptor）

    在查询物理内存使用情况时，需要将页表项的状态转换为phy_memDesriptor中的类型（OS_RESERVED_MEMORY_TYPE, OS_OCCUPIED_MEMORY_TYPE, freeSystemRam）。