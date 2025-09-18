#include "phygpsmemmgr.h"
#include "kpoolmemmgr.h"
#include "Memory.h"
#include "VideoDriver.h"
#include "os_error_definitions.h"
#include "OS_utils.h"
#include "pgtable45.h"
#include "processor_self_manage.h"

static inline void bitset64bits_set(uint64_t&bitset,uint64_t bitpos)
{
    bitset|=1<<bitpos;
}
static inline void bitset64bits_clear(uint64_t&bitset,uint64_t bitpos)
{
    bitset&=~(1<<bitpos);
}
static inline void bitset64bits_write(uint64_t&bitset,uint64_t bitpos,bool value){
    if (value)
    {
        bitset64bits_set(bitset,bitpos);
    }else{
        bitset64bits_clear(bitset,bitpos);
    }
    
}
int KernelSpacePgsMemMgr::pgtb_entry_convert(uint64_t&pgtb_entry,
    PgControlBlockHeader PgCB,
    phyaddr_t alloced_loweraddr )
{
    uint8_t cache_strategy_index;
    if(PgCB.flags.is_exist==0)
    {
        bitset64bits_clear(pgtb_entry,PageTableEntry::P_BIT);
        return OS_SUCCESS;
    }else{
        bitset64bits_write(pgtb_entry,PageTableEntry::RW_BIT,PgCB.flags.is_writable);
        bitset64bits_write(pgtb_entry,PageTableEntry::US_BIT,!PgCB.flags.is_kernel);
        bitset64bits_write(pgtb_entry,PageTableEntry::XD_BIT,!PgCB.flags.is_executable);
        
        for(int i=0;i<8;i++)
        {
            if(PgCB.flags.cache_strateggy==cache_strategy_table.mapped_entry[i])
            {
                cache_strategy_index=i;
                break;
            }
        }
    }bitset64bits_write(pgtb_entry,PageTableEntry::PWT_BIT,(bool)(cache_strategy_index&1));
         bitset64bits_write(pgtb_entry,PageTableEntry::PCD_BIT,(bool)(cache_strategy_index&2));
        switch (PgCB.flags.pg_lv)
        {
        case 0:
        bitset64bits_write(pgtb_entry,PTE::G_BIT,PgCB.flags.is_global);
        pgtb_entry&=~PTE::ADDR_MASK;
        if(alloced_loweraddr&PAGE_OFFSET_MASK[0])return OS_INVALID_ADDRESS;
        pgtb_entry|=alloced_loweraddr;
         bitset64bits_write(pgtb_entry,PTE::PAT_BIT,(bool)(cache_strategy_index&4));
        break;
        case 1:
        
        bitset64bits_write(pgtb_entry,PDE::PS_BIT,PgCB.flags.is_atom);
        if (PgCB.flags.is_atom)
        {
            bitset64bits_write(pgtb_entry,PDE::G_BIT,PgCB.flags.is_global);
            pgtb_entry&=~PDE::ADDR_2MB_MASK;
            if(alloced_loweraddr&PAGE_OFFSET_MASK[1])return OS_INVALID_ADDRESS;
            pgtb_entry|=alloced_loweraddr;
            bitset64bits_write(pgtb_entry,PDE::PAT_BIT,cache_strategy_index&4);
        }else
        { 
            pgtb_entry&=~PDE::ADDR_PT_MASK;
            if(alloced_loweraddr&PAGE_OFFSET_MASK[0])return OS_INVALID_ADDRESS;
            pgtb_entry|=alloced_loweraddr;
        }
        
        break;
        case 2:
        
        bitset64bits_write(pgtb_entry,PDPTE::PS_BIT,PgCB.flags.is_atom);
        if (PgCB.flags.is_atom)
        {
            bitset64bits_write(pgtb_entry,PDPTE::G_BIT,PgCB.flags.is_global);          
          pgtb_entry&=~PDPTE::ADDR_1GB_MASK;
            if(alloced_loweraddr&PAGE_OFFSET_MASK[1])return OS_INVALID_ADDRESS;
            pgtb_entry|=alloced_loweraddr;
            bitset64bits_write(pgtb_entry,PDPTE::PAT_BIT,cache_strategy_index&4);
        }else
        { 
            pgtb_entry&=~PDPTE::ADDR_PD_MASK;
            if(alloced_loweraddr&PAGE_OFFSET_MASK[0])return OS_INVALID_ADDRESS;
            pgtb_entry|=alloced_loweraddr;
        }
        
        break;
        case 3:
        pgtb_entry&=~PML4E::ADDR_MASK;
            if(alloced_loweraddr&PAGE_OFFSET_MASK[0])return OS_INVALID_ADDRESS;
            pgtb_entry|=alloced_loweraddr;
            break;
        case 4:
             pgtb_entry&=~PML5E::ADDR_MASK;
            if(alloced_loweraddr&PAGE_OFFSET_MASK[0])return OS_INVALID_ADDRESS;
            pgtb_entry|=alloced_loweraddr;;
            break;
        default:return OS_INVALID_PARAMETER;
        }
    }

void KernelSpacePgsMemMgr::enable_new_cr3()
{
   switch (cpu_pglv)
   {
   case 4:
    pgtb_lowaddr_equalmap_construct_4lvpg();
    break;
   
   case 5:

   break;
    default:return;
   }
    
}
/**
 * 构建PTE级别（最后一级）的页表
 */
int KernelSpacePgsMemMgr::construct_pte_level(
    uint64_t* pt_base, 
    lowerlv_PgCBtb* pt_PgCBtb, 
    uint64_t pml4_index, 
    uint64_t pdpt_index, 
    uint64_t pd_index
) {
    for (int l = 0; l < 512; l++) {
        pgflags tmp_flags = pt_PgCBtb->entries[l].flags;
        if (tmp_flags.is_exist) {
            phyaddr_t page_base = (pml4_index << 39) + (pdpt_index << 30) + (pd_index << 21) + (l << 12);
            int result = pgtb_entry_convert(pt_base[l], pt_PgCBtb->entries[l], page_base);
            if (result != OS_SUCCESS) {
                return result;
            }
        }
    }
    return OS_SUCCESS;
}

/**
 * 构建PDE级别（第三级）的页表
 */
int KernelSpacePgsMemMgr::construct_pde_level(
    uint64_t* pd_base, 
    lowerlv_PgCBtb* pd_PgCBtb, 
    uint64_t pml4_index, 
    uint64_t pdpt_index
) {
    for (int k = 0; k < 512; k++) {
        pgflags tmp_flags = pd_PgCBtb->entries[k].flags;
        if (tmp_flags.is_exist) {
            if (tmp_flags.is_atom) {
                // 处理2MB大页
                phyaddr_t huge_2mb_base = (pml4_index << 39) + (pdpt_index << 30) + (k << 21);
                int result = pgtb_entry_convert(pd_base[k], pd_PgCBtb->entries[k], huge_2mb_base);
                if (result != OS_SUCCESS) {
                    return result;
                }
            } else {
                // 处理指向PT的普通PDE
                uint64_t* pt_base = (uint64_t*)pgtb_heap_ptr->pgalloc();
                if (pt_base == nullptr) {
                    kputsSecure("construct_pde_level:pgalloc failed for pt_base\n");
                    return OS_OUT_OF_MEMORY;
                }
                
                int result = pgtb_entry_convert(pd_base[k], pd_PgCBtb->entries[k], (phyaddr_t)pt_base);
                if (result != OS_SUCCESS) {
                    pgtb_heap_ptr->free((phyaddr_t)pt_base);
                    return result;
                }
                
                lowerlv_PgCBtb* pt_PgCBtb = pd_PgCBtb->entries[k].base.lowerlvPgCBtb;
                
                // 构建PTE级别
                result = construct_pte_level(pt_base, pt_PgCBtb, pml4_index, pdpt_index, k);
                if (result != OS_SUCCESS) {
                    pgtb_heap_ptr->free((phyaddr_t)pt_base);
                    return result;
                }
            }
        }
    }
    return OS_SUCCESS;
}

/**
 * 构建PDPTE级别（第二级）的页表
 */
int KernelSpacePgsMemMgr::construct_pdpte_level(
    uint64_t* pdpt_base, 
    lowerlv_PgCBtb* pdpt_PgCBtb, 
    uint64_t pml4_index
) {
    for (int j = 0; j < 512; j++) {
        pgflags tmp_flags = pdpt_PgCBtb->entries[j].flags;
        if (tmp_flags.is_exist) {
            if (tmp_flags.is_atom) {
                // 处理1GB大页
                phyaddr_t huge_1gb_base = (pml4_index << 39) + (j << 30);
                int result = pgtb_entry_convert(pdpt_base[j], pdpt_PgCBtb->entries[j], huge_1gb_base);
                if (result != OS_SUCCESS) {
                    return result;
                }
            } else {
                // 处理指向PD的普通PDPTE
                uint64_t* pd_base = (uint64_t*)pgtb_heap_ptr->pgalloc();
                if (pd_base == nullptr) {
                    kputsSecure("construct_pdpte_level:pgalloc failed for pd_base\n");
                    return OS_OUT_OF_MEMORY;
                }
                
                int result = pgtb_entry_convert(pdpt_base[j], pdpt_PgCBtb->entries[j], (phyaddr_t)pd_base);
                if (result != OS_SUCCESS) {
                    pgtb_heap_ptr->free((phyaddr_t)pd_base);
                    return result;
                }
                
                lowerlv_PgCBtb* pd_PgCBtb = pdpt_PgCBtb->entries[j].base.lowerlvPgCBtb;
                
                // 构建PDE级别
                result = construct_pde_level(pd_base, pd_PgCBtb, pml4_index, j);
                if (result != OS_SUCCESS) {
                    pgtb_heap_ptr->free((phyaddr_t)pd_base);
                    return result;
                }
            }
        }
    }
    return OS_SUCCESS;
}

/**
 * 构建PML4E级别（第一级）的页表
 */
int KernelSpacePgsMemMgr::construct_pml4e_level(
    uint64_t* rootPgtb, 
    lowerlv_PgCBtb* root_PgCBtb
) {
    for (uint16_t i = 0; i < 512; i++) {
        pgflags tmp_flags = root_PgCBtb->entries[i].flags;
        if (tmp_flags.is_exist) {
            if (tmp_flags.is_atom) {
                kputsSecure("construct_pml4e_level:atom entry in Pml4 not support\n");
                return OS_BAD_FUNCTION;
            } else {
                uint64_t* pdpt_base = (uint64_t*)pgtb_heap_ptr->pgalloc();
                if (pdpt_base == nullptr) {
                    kputsSecure("construct_pml4e_level:pgalloc failed for pdpt_base\n");
                    return OS_OUT_OF_MEMORY;
                }
                
                int result = pgtb_entry_convert(rootPgtb[i], root_PgCBtb->entries[i], (phyaddr_t)pdpt_base);
                if (result != OS_SUCCESS) {
                    pgtb_heap_ptr->free((phyaddr_t)pdpt_base);
                    return result;
                }
                
                lowerlv_PgCBtb* pdpt_PgCBtb = root_PgCBtb->entries[i].base.lowerlvPgCBtb;
                
                // 构建PDPTE级别
                result = construct_pdpte_level(pdpt_base, pdpt_PgCBtb, i);
                if (result != OS_SUCCESS) {
                    pgtb_heap_ptr->free((phyaddr_t)pdpt_base);
                    return result;
                }
            }
        }
    }
    return OS_SUCCESS;
}

/**
 * 构建四级页表的低地址恒等映射
 * 这个函数现在通过调用分层构建函数来实现
 */
int KernelSpacePgsMemMgr::pgtb_lowaddr_equalmap_construct_4lvpg() {
    lowerlv_PgCBtb* root_PgCBtb = rootlv4PgCBtb->base.lowerlvPgCBtb;
    uint64_t* rootPgtb = (uint64_t*)pgtb_heap_ptr->pgalloc();
    
    if (rootPgtb == nullptr) {
        kputsSecure("pgtb_lowaddr_equalmap_construct_4lvpg:pgalloc failed for rootPgtb\n");
        return OS_OUT_OF_MEMORY;
    }
    
    // 从PML4级别开始构建
    int result = construct_pml4e_level(rootPgtb, root_PgCBtb);
    
    if (result != OS_SUCCESS) {
        // 如果构建失败，释放根页表
        pgtb_heap_ptr->free((phyaddr_t)rootPgtb);
        return result;
    }
    
    // 设置PAT寄存器
    LocalCPU localcpu;
    localcpu.set_ia32_pat(cache_strategy_table.value);
    
    return OS_SUCCESS;
}
int KernelSpacePgsMemMgr::pgtb_lowaddr_equalmap_construct_5lvpg()
{
    return 0;
}