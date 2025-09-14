#pragma once
#include <stdint.h>

// 物理地址类型 (MAXPHYADDR ≤ 52)
typedef uint64_t phys_addr_t;

constexpr uint64_t PAGE_SIZE_IN_LV[] = {
    (1ULL << 12) ,  // NORMAL_4kb_MASK_OFFSET
    (1ULL << 21) ,  // NORMAL_2MB_MASK_OFFSET
    (1ULL << 30) ,  // NORMAL_1GB_MASK_OFFSET
    (1ULL << 39) ,  // NORMAL_512GB_MASK_OFFSET
    (1ULL << 48)    // NORMAL_256TB_MASK_OFFSET
};
constexpr uint64_t PAGE_OFFSET_MASK[]={
     (1ULL << 12)-1 ,  // NORMAL_4kb_MASK_OFFSET
    (1ULL << 21) -1,  // NORMAL_2MB_MASK_OFFSET
    (1ULL << 30) -1,  // NORMAL_1GB_MASK_OFFSET
    (1ULL << 39) -1,  // NORMAL_512GB_MASK_OFFSET
    (1ULL << 48)  -1
};
constexpr uint64_t Max_huge_pg_index =2;//上面那个表中现在的cpu暂时只支持到1GB大页

typedef uint64_t paging_entry_t;

constexpr uint64_t PT_INDEX_MASK_lv0=0x00000000000001FF<<12;
constexpr uint64_t PD_INDEX_MASK_lv1=PT_INDEX_MASK_lv0<<9;
constexpr uint64_t PDPT_INDEX_MASK_lv2=PD_INDEX_MASK_lv1<<9;
constexpr uint64_t  PML4_INDEX_MASK_lv3=PDPT_INDEX_MASK_lv2<<9;
constexpr uint64_t PML5_INDEX_MASK_lv4=PML4_INDEX_MASK_lv3<<9;
//cr3寄存器结构体
typedef uint64_t cr3_t;
// 基础类型定义
using uint64 = uint64_t;

// 物理地址宽度（假设为52位）
constexpr uint64 PHYS_ADDR_WIDTH = 52;
constexpr uint64 PHYS_ADDR_MASK = (1ULL << PHYS_ADDR_WIDTH) - 1;

// 通用页表项位字段偏移和掩码（适用于所有级别的页表项）
namespace PageTableEntry {
    constexpr uint64 P_BIT    = 0;   // Present 位偏移
    constexpr uint64 RW_BIT   = 1;   // Read/Write 位偏移
    constexpr uint64 US_BIT   = 2;   // User/Supervisor 位偏移
    constexpr uint64 PWT_BIT  = 3;   // Page-Level Write-Through 位偏移
    constexpr uint64 PCD_BIT  = 4;   // Page-Level Cache Disable 位偏移
    constexpr uint64 A_BIT    = 5;   // Accessed 位偏移
    constexpr uint64 XD_BIT   = 63;  // Execute-Disable 位偏移

    // 通用掩码
    constexpr uint64 P_MASK   = 1ULL << P_BIT;
    constexpr uint64 RW_MASK  = 1ULL << RW_BIT;
    constexpr uint64 US_MASK  = 1ULL << US_BIT;
    constexpr uint64 PWT_MASK = 1ULL << PWT_BIT;
    constexpr uint64 PCD_MASK = 1ULL << PCD_BIT;
    constexpr uint64 A_MASK   = 1ULL << A_BIT;
    constexpr uint64 XD_MASK  = 1ULL << XD_BIT;
}

// PML5E（指向PML4表）
namespace PML5E {
    // 特定字段偏移（除通用字段外）
    constexpr uint64 R_BIT    = 11;  // Restart 位偏移（HLAT分页）
    constexpr uint64 PS_BIT   = 7;   // Reserved（必须为0）

    // 特定字段掩码
    constexpr uint64 R_MASK   = 1ULL << R_BIT;
    constexpr uint64 PS_MASK  = 1ULL << PS_BIT;

    // 物理地址字段（位12到M-1，M通常为52）
    constexpr uint64 ADDR_OFFSET = 12;
    constexpr uint64 ADDR_BITS = PHYS_ADDR_WIDTH - ADDR_OFFSET;
    constexpr uint64 ADDR_MASK = ((1ULL << ADDR_BITS) - 1) << ADDR_OFFSET;
}

// PML4E（指向PDPT）
namespace PML4E {
    // 特定字段偏移（除通用字段外）
    constexpr uint64 R_BIT    = 11;  // Restart 位偏移（HLAT分页）
    constexpr uint64 PS_BIT   = 7;   // Reserved（必须为0）

    // 特定字段掩码
    constexpr uint64 R_MASK   = 1ULL << R_BIT;
    constexpr uint64 PS_MASK  = 1ULL << PS_BIT;

    // 物理地址字段（位12到M-1，M通常为52）
    constexpr uint64 ADDR_OFFSET = 12;
    constexpr uint64 ADDR_BITS = PHYS_ADDR_WIDTH - ADDR_OFFSET;
    constexpr uint64 ADDR_MASK = ((1ULL << ADDR_BITS) - 1) << ADDR_OFFSET;
}

// PDPTE（映射1GB页或指向PD）
namespace PDPTE {
    // 特定字段偏移（除通用字段外）
    constexpr uint64 D_BIT    = 6;   // Dirty 位偏移
    constexpr uint64 PS_BIT   = 7;   // Page Size 位偏移
    constexpr uint64 G_BIT    = 8;   // Global 位偏移
    constexpr uint64 R_BIT    = 11;  // Restart 位偏移（HLAT分页）
    constexpr uint64 PAT_BIT  = 12;  // PAT 位偏移
    constexpr uint64 PK_BITS  = 59;  // Protection Key 起始位（4位）

    // 特定字段掩码
    constexpr uint64 D_MASK   = 1ULL << D_BIT;
    constexpr uint64 PS_MASK  = 1ULL << PS_BIT;
    constexpr uint64 G_MASK   = 1ULL << G_BIT;
    constexpr uint64 R_MASK   = 1ULL << R_BIT;
    constexpr uint64 PAT_MASK = 1ULL << PAT_BIT;
    constexpr uint64 PK_MASK  = 0xFULL << PK_BITS;

    // 物理地址字段（根据PS位决定）
    // 当PS=0（指向页目录）时
    constexpr uint64 ADDR_PD_OFFSET = 12;
    constexpr uint64 ADDR_PD_BITS = PHYS_ADDR_WIDTH - ADDR_PD_OFFSET;
    constexpr uint64 ADDR_PD_MASK = ((1ULL << ADDR_PD_BITS) - 1) << ADDR_PD_OFFSET;
    
    // 当PS=1（映射1GB页）时
    constexpr uint64 ADDR_1GB_OFFSET = 30;
    constexpr uint64 ADDR_1GB_BITS = PHYS_ADDR_WIDTH - ADDR_1GB_OFFSET;
    constexpr uint64 ADDR_1GB_MASK = ((1ULL << ADDR_1GB_BITS) - 1) << ADDR_1GB_OFFSET;
}

// PDE（映射2MB页或指向PT）
namespace PDE {
    // 特定字段偏移（除通用字段外）
    constexpr uint64 D_BIT    = 6;   // Dirty 位偏移
    constexpr uint64 PS_BIT   = 7;   // Page Size 位偏移
    constexpr uint64 G_BIT    = 8;   // Global 位偏移
    constexpr uint64 R_BIT    = 11;  // Restart 位偏移（HLAT分页）
    constexpr uint64 PAT_BIT  = 12;  // PAT 位偏移
    constexpr uint64 PK_BITS  = 59;  // Protection Key 起始位（4位）

    // 特定字段掩码
    constexpr uint64 D_MASK   = 1ULL << D_BIT;
    constexpr uint64 PS_MASK  = 1ULL << PS_BIT;
    constexpr uint64 G_MASK   = 1ULL << G_BIT;
    constexpr uint64 R_MASK   = 1ULL << R_BIT;
    constexpr uint64 PAT_MASK = 1ULL << PAT_BIT;
    constexpr uint64 PK_MASK  = 0xFULL << PK_BITS;

    // 物理地址字段（根据PS位决定）
    // 当PS=0（指向页表）时
    constexpr uint64 ADDR_PT_OFFSET = 12;
    constexpr uint64 ADDR_PT_BITS = PHYS_ADDR_WIDTH - ADDR_PT_OFFSET;
    constexpr uint64 ADDR_PT_MASK = ((1ULL << ADDR_PT_BITS) - 1) << ADDR_PT_OFFSET;
    
    // 当PS=1（映射2MB页）时
    constexpr uint64 ADDR_2MB_OFFSET = 21;
    constexpr uint64 ADDR_2MB_BITS = PHYS_ADDR_WIDTH - ADDR_2MB_OFFSET;
    constexpr uint64 ADDR_2MB_MASK = ((1ULL << ADDR_2MB_BITS) - 1) << ADDR_2MB_OFFSET;
}

// PTE（映射4KB页）
namespace PTE {
    // 特定字段偏移（除通用字段外）
    constexpr uint64 D_BIT    = 6;   // Dirty 位偏移
    constexpr uint64 PAT_BIT  = 7;   // PAT 位偏移
    constexpr uint64 G_BIT    = 8;   // Global 位偏移
    constexpr uint64 R_BIT    = 11;  // Restart 位偏移（HLAT分页）
    constexpr uint64 PK_BITS  = 59;  // Protection Key 起始位（4位）

    // 特定字段掩码
    constexpr uint64 D_MASK   = 1ULL << D_BIT;
    constexpr uint64 PAT_MASK = 1ULL << PAT_BIT;
    constexpr uint64 G_MASK   = 1ULL << G_BIT;
    constexpr uint64 R_MASK   = 1ULL <<R_BIT;
    constexpr uint64 PK_MASK  = 0xFULL << PK_BITS;

    // 物理地址字段（位12到M-1，M通常为52）
    constexpr uint64 ADDR_OFFSET = 12;
    constexpr uint64 ADDR_BITS = PHYS_ADDR_WIDTH - ADDR_OFFSET;
    constexpr  uint64 ADDR_MASK = ((1ULL << ADDR_BITS) - 1) << ADDR_OFFSET;
}

// 枚举类型用于表示页表项类型
enum class PageTableEntryType {
    PML5,
    PML4,
    PDPT,
    PD,
    PT
};
