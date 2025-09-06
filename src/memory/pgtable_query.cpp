
#include "pgtable45.h"
#include "phygpsmemmgr.h"
#include "kpoolmemmgr.h"
#include "Memory.h"
#include "VideoDriver.h"
#include "os_error_definitions.h"
#include "utils.h"
#include "DoubleLinkList.h"

// LV4查询（顶级节点，无父级位图处理）
PgControlBlockHeader PgsMemMgr::PgCBtb_lv4_entry_query(phyaddr_t addr) {
    uint64_t lv4_index = (addr & PML5_INDEX_MASK_lv4) >> 48;
    return gPgsMemMgr.rootlv4PgCBtb[lv4_index];
}

// LV3查询（需处理LV4父级的位图存储）
PgControlBlockHeader PgsMemMgr::PgCBtb_lv3_entry_query(phyaddr_t addr) {
    PgControlBlockHeader NullPgCBHeader = {0};
    uint16_t lv3_index = (addr & PML4_INDEX_MASK_lv3) >> 39;
    uint64_t lv4_index = (addr & PML5_INDEX_MASK_lv4) >> 48;

    PgCBlv4header* lv4_PgCBHeader = gPgsMemMgr.cpu_pglv == 5 ? 
                                    &gPgsMemMgr.rootlv4PgCBtb[lv4_index] : 
                                    gPgsMemMgr.rootlv4PgCBtb;
    if (lv4_PgCBHeader->flags.is_exist == 0||lv4_PgCBHeader->flags.is_atom==1) {
        return NullPgCBHeader;
    }

    // 处理LV4父级使用位图存储LV3的情况
    if (lv4_PgCBHeader->flags.is_lowerlv_bitmap) {
        PgCBlv3header header;
        header.flags = lv4_PgCBHeader->flags;
        header.base.lowerlvPgCBtb = nullptr;
        header.flags.is_atom = 1;
        
        if (lv4_PgCBHeader->flags.is_reserved) {
            pgsbitmap_entry2bits_width* mapbase = 
                &lv4_PgCBHeader->base.map_tpye2->bitmap;
            uint8_t entry_result = getentry_entry2bits_width(*mapbase, lv3_index);
            switch (entry_result) {
            case FREE:
                header.flags.is_occupied = 0;
                header.flags.is_reserved = 0;
                break;
            case RESERVED:
                header.flags.is_reserved = 1;
                break;
            case OCCUPYIED:
                header.flags.is_occupied = 1;
                header.flags.is_reserved = 0;
                break;
            default:
                return NullPgCBHeader;
            }
        } else {
            pgsbitmap_entry1bit_width* mapbase = 
                &lv4_PgCBHeader->base.map_tpye1->bitmap;
            bool value = getbit_entry1bit_width(mapbase, lv3_index);
            header.flags.is_occupied = value;
        }
        return header;
    } 
    // 常规数组存储
    else {
        return lv4_PgCBHeader->base.lowerlvPgCBtb->entries[lv3_index];
    }
}

// LV2查询（需处理LV3父级的位图存储）
PgControlBlockHeader PgsMemMgr::PgCBtb_lv2_entry_query(phyaddr_t addr) {
    PgControlBlockHeader NullPgCBHeader = {0};
    uint16_t lv2_index = (addr & PDPT_INDEX_MASK_lv2) >> 30;
    uint16_t lv3_index = (addr & PML4_INDEX_MASK_lv3) >> 39;
    uint64_t lv4_index = (addr & PML5_INDEX_MASK_lv4) >> 48;

    PgCBlv4header* lv4_PgCBHeader = gPgsMemMgr.cpu_pglv == 5 ? 
                                    &gPgsMemMgr.rootlv4PgCBtb[lv4_index] : 
                                    gPgsMemMgr.rootlv4PgCBtb;
    if (lv4_PgCBHeader->flags.is_exist == 0||lv4_PgCBHeader->flags.is_atom==1||lv4_PgCBHeader->flags.is_lowerlv_bitmap==1) {
        return NullPgCBHeader;
    }

    // 获取LV3节点（处理LV4的位图存储）
    PgControlBlockHeader lv3_header = lv4_PgCBHeader->base.lowerlvPgCBtb->entries[lv3_index];
    if (lv3_header.flags.is_exist == 0||lv3_header.flags.is_atom==1) {
        return NullPgCBHeader;
    }

    // 处理LV3父级使用位图存储LV2的情况（1GB大页）
    if (lv3_header.flags.is_lowerlv_bitmap) {
        PgCBlv2header header;
        header.flags = lv3_header.flags;
        header.base.lowerlvPgCBtb = nullptr;
        header.flags.is_atom = 1;
        
        if (lv3_header.flags.is_reserved) {
            pgsbitmap_entry2bits_width* mapbase = 
                &lv3_header.base.map_tpye2->bitmap;
            uint8_t entry_result = getentry_entry2bits_width(*mapbase, lv2_index);
            switch (entry_result) {
            case FREE:
                header.flags.is_occupied = 0;
                header.flags.is_reserved = 0;
                break;
            case RESERVED:
                header.flags.is_reserved = 1;
                break;
            case OCCUPYIED:
                header.flags.is_occupied = 1;
                header.flags.is_reserved = 0;
                break;
            default:
                return NullPgCBHeader;
            }
        } else {
            pgsbitmap_entry1bit_width* mapbase = 
                &lv3_header.base.map_tpye1->bitmap;
            bool value = getbit_entry1bit_width(mapbase, lv2_index);
            header.flags.is_occupied = value;
        }
        return header;
    } 
    // 常规数组存储
    else {
        return lv3_header.base.lowerlvPgCBtb->entries[lv2_index];
    }
}

// LV1查询（需处理LV2父级的位图存储）
PgControlBlockHeader PgsMemMgr::PgCBtb_lv1_entry_query(phyaddr_t addr) {
    PgControlBlockHeader NullPgCBHeader = {0};
    uint16_t lv1_index = (addr & PD_INDEX_MASK_lv1) >> 21;
    uint16_t lv2_index = (addr & PDPT_INDEX_MASK_lv2) >> 30;
    uint16_t lv3_index = (addr & PML4_INDEX_MASK_lv3) >> 39;
    uint64_t lv4_index = (addr & PML5_INDEX_MASK_lv4) >> 48;

    PgCBlv4header* lv4_PgCBHeader = gPgsMemMgr.cpu_pglv == 5 ? 
                                    &gPgsMemMgr.rootlv4PgCBtb[lv4_index] : 
                                    gPgsMemMgr.rootlv4PgCBtb;
    if (lv4_PgCBHeader->flags.is_exist == 0||lv4_PgCBHeader->flags.is_atom==1||lv4_PgCBHeader->flags.is_lowerlv_bitmap==1) {
        return NullPgCBHeader;
    }

    // 获取LV3节点（处理LV4的位图存储）
    PgControlBlockHeader lv3_header = lv4_PgCBHeader->base.lowerlvPgCBtb->entries[lv3_index];
    if (lv3_header.flags.is_exist == 0||lv3_header.flags.is_atom==1||lv3_header.flags.is_lowerlv_bitmap==1) {
        return NullPgCBHeader;
    }

    // 获取LV2节点（处理LV3的位图存储）
    PgControlBlockHeader lv2_header = lv3_header.base.lowerlvPgCBtb->entries[lv2_index];
    if (lv2_header.flags.is_exist == 0||lv2_header.flags.is_atom==1) {
        return NullPgCBHeader;
    }

    // 处理LV2父级使用位图存储LV1的情况（2MB大页）
    if (lv2_header.flags.is_lowerlv_bitmap) {
        PgCBlv1header header;
        header.flags = lv2_header.flags;
        header.base.lowerlvPgCBtb = nullptr;
        header.flags.is_atom = 1;
        
        if (lv2_header.flags.is_reserved) {
            pgsbitmap_entry2bits_width* mapbase = 
                &lv2_header.base.map_tpye2->bitmap;
            uint8_t entry_result = getentry_entry2bits_width(*mapbase, lv1_index);
            switch (entry_result) {
            case FREE:
                header.flags.is_occupied = 0;
                header.flags.is_reserved = 0;
                break;
            case RESERVED:
                header.flags.is_reserved = 1;
                break;
            case OCCUPYIED:
                header.flags.is_occupied = 1;
                header.flags.is_reserved = 0;
                break;
            default:
                return NullPgCBHeader;
            }
        } else {
            pgsbitmap_entry1bit_width* mapbase = 
                &lv2_header.base.map_tpye1->bitmap;
            bool value = getbit_entry1bit_width(mapbase, lv1_index);
            header.flags.is_occupied = value;
        }
        return header;
    } 
    // 常规数组存储
    else {
        return lv2_header.base.lowerlvPgCBtb->entries[lv1_index];
    }
}

// LV0查询（需处理LV1父级的位图存储）
PgControlBlockHeader PgsMemMgr::PgCBtb_lv0_entry_query(phyaddr_t addr) {
    PgControlBlockHeader NullPgCBHeader = {0};
    uint16_t lv0_index = (addr & PT_INDEX_MASK_lv0) >> 12;
    uint16_t lv1_index = (addr & PD_INDEX_MASK_lv1) >> 21;
    uint16_t lv2_index = (addr & PDPT_INDEX_MASK_lv2) >> 30;
    uint16_t lv3_index = (addr & PML4_INDEX_MASK_lv3) >> 39;
    uint64_t lv4_index = (addr & PML5_INDEX_MASK_lv4) >> 48;

    PgCBlv4header* lv4_PgCBHeader = gPgsMemMgr.cpu_pglv == 5 ? 
                                    &gPgsMemMgr.rootlv4PgCBtb[lv4_index] : 
                                    gPgsMemMgr.rootlv4PgCBtb;
    if (lv4_PgCBHeader->flags.is_exist == 0||lv4_PgCBHeader->flags.is_atom==1||lv4_PgCBHeader->flags.is_lowerlv_bitmap==1) {
        return NullPgCBHeader;
    }

    // 获取LV3节点（处理LV4的位图存储）
    PgControlBlockHeader lv3_header = lv4_PgCBHeader->base.lowerlvPgCBtb->entries[lv3_index];
    if (lv3_header.flags.is_exist == 0||lv3_header.flags.is_atom==1||lv3_header.flags.is_lowerlv_bitmap==1) {
        return NullPgCBHeader;
    }

    // 获取LV2节点（处理LV3的位图存储）
    PgControlBlockHeader lv2_header = lv3_header.base.lowerlvPgCBtb->entries[lv2_index];
    if (lv2_header.flags.is_exist == 0||lv2_header.flags.is_atom==1||lv2_header.flags.is_lowerlv_bitmap==1) {
        return NullPgCBHeader;
    }

    // 获取LV1节点（处理LV2的位图存储）
    PgControlBlockHeader lv1_header = lv2_header.base.lowerlvPgCBtb->entries[lv1_index];
    if (lv1_header.flags.is_exist == 0||lv1_header.flags.is_atom==1) {
        return NullPgCBHeader;
    }

    // 处理LV1父级使用位图存储LV0的情况（4KB页）
    if (lv1_header.flags.is_lowerlv_bitmap) {
        PgCBlv0header header;
        header.flags = lv1_header.flags;
        header.base.lowerlvPgCBtb = nullptr;
        header.flags.is_atom = 1;
        
        if (lv1_header.flags.is_reserved) {
            pgsbitmap_entry2bits_width* mapbase = 
                &lv1_header.base.map_tpye2->bitmap;
            uint8_t entry_result = getentry_entry2bits_width(*mapbase, lv0_index);
            switch (entry_result) {
            case FREE:
                header.flags.is_occupied = 0;
                header.flags.is_reserved = 0;
                break;
            case RESERVED:
                header.flags.is_reserved = 1;
                break;
            case OCCUPYIED:
                header.flags.is_occupied = 1;
                header.flags.is_reserved = 0;
                break;
            default:
                return NullPgCBHeader;
            }
        } else {
            pgsbitmap_entry1bit_width* mapbase = 
                &lv1_header.base.map_tpye1->bitmap;
            bool value = getbit_entry1bit_width(mapbase, lv0_index);
            header.flags.is_occupied = value;
        }
        return header;
    } 
    // 常规数组存储
    else {
        return lv1_header.base.lowerlvPgCBtb->entries[lv0_index];
    }
}
/**
 * 查询物理内存使用情况
 * 输入基址，长度
 * 返回物理内存使用情况,返回的的是phy_memDesriptor数组（最后一项全零）
 * 记得delete返回结果
 */
/**
 * 输入基址，长度
 * 返回物理内存使用情况，返回的物理内存使用情况
 * 输入的基址必须为页对齐,长度自动向上到4kb对齐
 * 函数逻辑为
 * 1.数据准备
 * 2.从低等级到高等级使用单个页查询函数
 * 。。。。。。
 * 如果是类型为freeSystemRam则需要特殊处理，不过这部分我来研究
 */

phy_memDesriptor *PgsMemMgr::queryPhysicalMemoryUsage(phyaddr_t base, uint64_t len_in_bytes)
{
    if (base & PAGE_OFFSET_MASK[0])         
        return nullptr;
    len_in_bytes += PAGE_SIZE_IN_LV[0] - 1;
    len_in_bytes &= ~PAGE_OFFSET_MASK[0];
    phyaddr_t endaddr=base+len_in_bytes;
    DoublyLinkedList<phy_memDesriptor> table;  
    phyaddr_t scan_addr=base;
    phy_memDesriptor dyn_descriptor={0};
    while (scan_addr<endaddr)//一次遍历至少一个页
    {   //返回结果中改成OS_ALLOCATABLE_MEMORY
        int lv=4;
        PgControlBlockHeader pg_entry;
        for(;lv>=0;lv--)
        {
            pg_entry=(gPgsMemMgr.*(gPgsMemMgr.PgCBtb_query_func[lv]))(scan_addr);
            if (pg_entry.flags.is_exist&&pg_entry.flags.is_atom)
            {
                break;
            }
        }
        /**
        *  上面的attribute0位是否占用，1位是否读，2位是否写，3位是否可执行，
        *
        * 上面的约定只在phy_memDesriptor *queryPhysicalMemoryUsage(phyaddr_t base, uint64_t len_in_bytes)
        * 的返回结果中有效
        */
        if(lv<0) return nullptr;
            
        uint64_t pg_attribute = 0;
        // 根据pg_entry构建pg_attribute
        pg_attribute |= pg_entry.flags.is_occupied;
        pg_attribute |= pg_entry.flags.is_readable << 1;
        pg_attribute |= pg_entry.flags.is_writable << 2;
        pg_attribute |= pg_entry.flags.is_executable << 3;
            
        if (dyn_descriptor.NumberOfPages == 0)
        {
            // 初始化动态描述符
            dyn_descriptor.PhysicalStart = scan_addr;
            dyn_descriptor.NumberOfPages = 0;
            dyn_descriptor.Attribute = pg_attribute;
            dyn_descriptor.Type = pg_entry.flags.is_reserved ? OS_RESERVED_MEMORY : OS_ALLOCATABLE_MEMORY;
        }
            
        if(pg_attribute != dyn_descriptor.Attribute ||
           ((dyn_descriptor.Type == OS_RESERVED_MEMORY) != (bool)pg_entry.flags.is_reserved))
        {
            table.push_back(dyn_descriptor);
            dyn_descriptor.PhysicalStart = scan_addr;
            dyn_descriptor.NumberOfPages = 0;
            dyn_descriptor.Attribute = pg_attribute;
            dyn_descriptor.Type = pg_entry.flags.is_reserved ? OS_RESERVED_MEMORY : OS_ALLOCATABLE_MEMORY;
        }
        pg_attribute=0;
        //根据pg_entry构建pg_attribute
        pg_attribute|=pg_entry.flags.is_occupied;
        pg_attribute|=pg_entry.flags.is_readable<<1;
        pg_attribute|=pg_entry.flags.is_writable<<2;
        pg_attribute|=pg_entry.flags.is_executable<<3;  
        if(pg_attribute!=dyn_descriptor.Attribute&&
        (dyn_descriptor.Type==OS_RESERVED_MEMORY)==(bool)pg_entry.flags.is_reserved)
        {
            table.push_back(dyn_descriptor);
            dyn_descriptor.PhysicalStart=scan_addr;
            dyn_descriptor.NumberOfPages=0;
            dyn_descriptor.Attribute=pg_attribute;
            dyn_descriptor.Type=pg_entry.flags.is_reserved?OS_RESERVED_MEMORY:OS_ALLOCATABLE_MEMORY;
        }else{
        }
        dyn_descriptor.NumberOfPages += 1ULL << 9*lv;
        scan_addr += PAGE_SIZE_IN_LV[lv];
    }
    return nullptr;
//未完待续
}