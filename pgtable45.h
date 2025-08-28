#pragma once
#include <stdint.h>

// 物理地址类型 (MAXPHYADDR ≤ 52)
typedef uint64_t phys_addr_t;
#define PHYS_ADDR_MASK 0x000FFFFFFFFFF000  // 52位物理地址掩码
#define HUGE_1GB_MASK 0x000FFFFFC0000000  // 1GB对齐
#define HUGE_2MB_MASK 0x000FFFFFFFE00000  // 2MB对齐
// 页表项基础类型 (所有表项均为64位)
typedef uint64_t paging_entry_t;

/* 通用标志位掩码 (图5-11) */
#define P_FLAG_MASK           (1ULL << 0)  // Present
#define RW_FLAG_MASK          (1ULL << 1)  // Read/Write
#define US_FLAG_MASK          (1ULL << 2)  // User/Supervisor
#define PWT_FLAG_MASK         (1ULL << 3)  // Page Write Through
#define PCD_FLAG_MASK         (1ULL << 4)  // Page Cache Disable
#define A_FLAG_MASK           (1ULL << 5)  // Accessed
#define D_FLAG_MASK           (1ULL << 6)  // Dirty (仅页映射项)
#define PS_FLAG_MASK          (1ULL << 7)  // Page Size (PDPTE/PDE)
#define G_FLAG_MASK           (1ULL << 8)  // Global
#define R_FLAG_MASK           (1ULL << 11) // Restart (HLAT)
#define PAT_FLAG_MASK         (1ULL << 12) // PAT位 (1GB/2MB页)
#define XD_FLAG_MASK          (1ULL << 63) // Execute Disable
//cr3寄存器结构体
typedef uint64_t cr3_t;

/**
 * PML5 Entry (5-level paging only)
 * 控制256TB地址空间，指向PML4表
 *
 * @field phys_addr   PML4表物理地址 (bits 51:12)
 * @field xd          执行禁止位 (bit 63)
 */
#define PML5E_GET_ADDR(entry) ((entry) & PHYS_ADDR_MASK)
#define PML5E_SET_ADDR(entry, addr) ((entry) = ((entry) & ~PHYS_ADDR_MASK) | ((addr) & PHYS_ADDR_MASK))
#define PML5E_GET_XD(entry)    (!!((entry) & XD_FLAG_MASK))
/**
 * PML4 Entry (控制512GB地址空间)
 * 指向页目录指针表(PDPT)或映射512GB大页
 *
 * @field phys_addr   PDPT物理地址 (bits 51:12)
 * @field ps          页大小标志 (reserved=0)
 * @field pk          保护键 (bits 62:59)
 */
#define PML4E_GET_ADDR(entry)  ((entry) & PHYS_ADDR_MASK)
#define PML4E_GET_PK(entry)    (((entry) >> 59) & 0xF)  // 保护键提取
#define PML4E_SET_PK(entry, pk) ((entry) = ((entry) & ~(0xFULL << 59)) | ((uint64_t)(pk & 0xF) << 59))
/**
 * PDPT Entry (控制1GB地址空间)
 *
 * PS=1时映射1GB页 (Table 5-16)
 *   @field phys_addr   1GB页基址 (bits 51:30)
 *   @field pat         PAT位 (bit 12)
 * 
 * PS=0时指向页目录 (Table 5-17)
 *   @field phys_addr   页目录物理地址 (bits 51:12)
 */
#define PDPTE_GET_PS(entry)    (!!((entry) & PS_FLAG_MASK))
#define PDPTE_GET_ADDR(entry)  (PDPTE_GET_PS(entry) ?  ((entry) & (PHYS_ADDR_MASK | 0x1FFFF000)) : ((entry) & PHYS_ADDR_MASK))
/**
 * Page Directory Entry (控制2MB地址空间)
 *
 * PS=1时映射2MB页 (Table 5-18)
 *   @field phys_addr   2MB页基址 (bits 51:21)
 *   @field pat         PAT位 (bit 12)
 * 
 * PS=0时指向页表 (Table 5-19)
 *   @field phys_addr   页表物理地址 (bits 51:12)
 */
#define PDE_GET_PS(entry)      (!!((entry) & PS_FLAG_MASK))
#define PDE_GET_ADDR(entry)    (PDE_GET_PS(entry) ?((entry) & (PHYS_ADDR_MASK | 0x1FFFFF000)) : ((entry) & PHYS_ADDR_MASK))
/**
 * Page Table Entry (控制4KB页)
 * 
 * @field phys_addr   4KB页物理地址 (bits 51:12)
 * @field pat         PAT位 (bit 7)
 * @field pk          保护键 (bits 62:59)
 * @field xd          执行禁止位 (bit 63)
 */
#define PTE_GET_ADDR(entry)    ((entry) & PHYS_ADDR_MASK)
#define PTE_GET_PAT(entry)     (!!((entry) & (1ULL << 7)))  // PAT位在bit7
#define PTE_GET_XD(entry)      (!!((entry) & XD_FLAG_MASK))
#define ENTRY_GET_FLAG(entry, mask) (!!((entry) & (mask)))
#define ENTRY_SET_FLAG(entry, mask, val)    ((entry) = (val) ? ((entry) | (mask)) : ((entry) & ~(mask)))
/**
 * 4级页表结构 (每表512条目)
 * 符合5.5.2节CR3格式要求
 */
#define PAGING_LEVEL_ENTRIES 512
/* 保护键作用域声明 (文档5.6.2节) */
// PDPTE: 1GB页映射时有效 (Table 5-16)
#define PDPTE_PK_VALID(entry) (PDPTE_GET_PS(entry))
// PDE: 2MB页映射时有效 (Table 5-18)
#define PDE_PK_VALID(entry)   (PDE_GET_PS(entry))
// PTE: 始终有效 (Table 5-20)
#define PTE_PK_VALID(entry)   (1)
typedef struct {
    union {
        paging_entry_t pml4e[PAGING_LEVEL_ENTRIES]; // PML4表 (4级分页)
        paging_entry_t pml5e[PAGING_LEVEL_ENTRIES]; // PML5表 (5级分页)
    };
} page_map_top_level;

/**
 * 页表结构内存布局示例:
 * 
 * 5级分页: CR3 → PML5 → PML4 → PDPT → PD → PT
 * 4级分页: CR3 → PML4 → PDPT → PD → PT
 */
/**
 * 设置PML4E物理地址 (文档Table 5-15)
 * @param entry   PML4E表项指针
 * @param addr    52位物理地址 (bits 51:12)
 * @note 需确保地址4KB对齐且不超MAXPHYADDR
 */
// 地址对齐验证
static inline bool is_1gb_aligned(phys_addr_t addr) {
    return (addr & 0x000000003FFFFFFF) == 0;
}
 static inline void pml4e_set_addr(paging_entry_t *entry, phys_addr_t addr)
{
    *entry = (*entry & ~PHYS_ADDR_MASK) | (addr & PHYS_ADDR_MASK);
}
/**
 * 设置PDPTE物理地址 (文档Table 5-16/17)
 * @param entry   PDPTE表项指针
 * @param addr    物理地址
 * @param is_1gb  是否1GB大页映射 (PS=1)
 * 
 * 大页地址要求:
 *   - 1GB对齐 (bits 51:30)
 *   - 地址掩码: 0x000FFFFF_C0000000
 * 
 * 普通映射要求:
 *   - 4KB对齐 (bits 51:12)
 */
static inline void pdpte_set_addr(paging_entry_t *entry, phys_addr_t addr, bool is_1gb)
{
    if (is_1gb) {
        // 1GB大页处理 (文档5.5.4节)
        *entry = (*entry & ~(PHYS_ADDR_MASK | 0x1FFFF000)) | 
                 (addr & 0x000FFFFFC0000000);
    } else {
        // 普通页目录指针
        *entry = (*entry & ~PHYS_ADDR_MASK) | (addr & PHYS_ADDR_MASK);
    }
}
/**
 * 设置PDE物理地址 (文档Table 5-18/19)
 * @param entry   PDE表项指针
 * @param addr    物理地址
 * @param is_2mb  是否2MB大页映射 (PS=1)
 * 
 * 大页地址要求:
 *   - 2MB对齐 (bits 51:21)
 *   - 地址掩码: 0x000FFFFF_FFE00000
 */
static inline void pde_set_addr(paging_entry_t *entry, phys_addr_t addr, bool is_2mb)
{
    if (is_2mb) {
        // 2MB大页处理
        *entry = (*entry & ~(PHYS_ADDR_MASK | 0x1FFFFF)) | 
                 (addr & 0x000FFFFFFFE00000);
    } else {
        // 普通页表指针
        *entry = (*entry & ~PHYS_ADDR_MASK) | (addr & PHYS_ADDR_MASK);
    }
}
/**
 * 设置PTE物理地址 (文档Table 5-20)
 * @param entry   PTE表项指针
 * @param addr    4KB对齐物理地址 (bits 51:12)
 */
static inline void pte_set_addr(paging_entry_t *entry, phys_addr_t addr)
{
    *entry = (*entry & ~PHYS_ADDR_MASK) | (addr & PHYS_ADDR_MASK);
}
/**
 * 提取有效保护键 (文档5.6.2节)
 * @return 4-bit保护键值 (bits 62:59)
 *         无效时返回PK_INVALID(0xF)
 */
#define PK_INVALID 0xF

// PDPTE保护键提取
static inline uint8_t pdpte_get_pk(paging_entry_t entry)
{
    return PDPTE_PK_VALID(entry) ? ((entry >> 59) & 0xF) : PK_INVALID;
}

// PDE保护键提取
static inline uint8_t pde_get_pk(paging_entry_t entry)
{
    return PDE_PK_VALID(entry) ? ((entry >> 59) & 0xF) : PK_INVALID;
}

// PTE保护键提取
static inline uint8_t pte_get_pk(paging_entry_t entry)
{
    return (entry >> 59) & 0xF;
}