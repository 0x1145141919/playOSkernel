#include "memory/AddresSpace.h"
#include "memory/Memory.h"
#include "memory/phygpsmemmgr.h"
#include "memory/kpoolmemmgr.h"
#include "os_error_definitions.h"
#include "memory/PagetbHeapMgr.h"
#include "linker_symbols.h"
#include "VideoDriver.h"
#include "util/OS_utils.h"
cache_table_idx_struct_t cache_strategy_to_idx(cache_strategy_t cache_strategy)
{
    cache_table_idx_struct_t result={0};
    for(uint8_t i=0;i<8;i++){
        if(cache_strategy==DEFAULT_PAT_CONFIG.mapped_entry[i]){
            result.PAT=(i>>2)&1;
            result.PCD=(i>>1)&1;
            result.PWT=i&1;
            return result;
        }
    }
    return result;
}
int AddressSpace::enable_VM_desc(VM_DESC desc,bool is_pagetballoc_reserved)
{
    PageTableEntryUnion*pml4eroottb=(PageTableEntryUnion*)pml4;
    pgaccess desc_access=desc.access;
    cache_table_idx_struct_t atompages_cache_table_idx=cache_strategy_to_idx(desc_access.cache_strategy);
    /**
     * 先搞参数检验
     */
    
    /**
     * 
     * int _4lv_pdpte_1GB_entries_set(phyaddr_t phybase,vaddr_t vaddr_base,uint16_t count,pgaccess access);//这里要求的是不能跨pml4e边界
    int _4lv_pde_2MB_entries_set(phyaddr_t phybase,vaddr_t vaddr_base,uint16_t count,pgaccess access);//这里要求的是不能跨页目录指针边界
    int _4lv_pte_4KB_entries_set(phyaddr_t phybase,vaddr_t vaddr_base,uint16_t count,pgaccess access);//这里要求的是不能跨页
    三个工具匿名函数，用于设置4级页表项，返回值是错误码 
    */
    // 匿名函数替代原来的get_sub_tb函数
    auto get_sub_tb = [is_pagetballoc_reserved](PageTableEntryUnion& entry, PageTableEntryType Clevel) -> PageTableEntryUnion* {
        if(Clevel==PageTableEntryType::PT) return nullptr;
        constexpr uint32_t _4KB_SIZE=0x1000;
        PageTableEntryUnion  copy=entry;
        if(copy.raw&PageTableEntry::P_MASK){
            phyaddr_t subtb_phybase=copy.pte.page_addr*_4KB_SIZE;
            if(Clevel==PageTableEntryType::PDPT||Clevel==PageTableEntryType::PD)
            {
                if(copy.raw&PDE::PS_MASK) return nullptr;
            }
            return (PageTableEntryUnion*)gPgtbHeapMgr.phyaddr_to_vaddr(subtb_phybase);
        }else{
            phyaddr_t entry_to_alloc_phybase=0;
            PageTableEntryUnion*subtb=nullptr;
            if(is_pagetballoc_reserved)
            {
                subtb=(PageTableEntryUnion*)gPgtbHeapMgr.alloc_pgtb(entry_to_alloc_phybase);
            }else{
                subtb=(PageTableEntryUnion*)gPgtbHeapMgr.alloc_pgtb_no_reserve(entry_to_alloc_phybase);
            }
            if(!subtb) return nullptr;
            entry.raw=0;
            entry.pte.present=1;
            entry.pte.RWbit=1;
            entry.pte.page_addr=entry_to_alloc_phybase/_4KB_SIZE;
            return subtb;
        }
    };

    auto _4lv_pte_4KB_entries_set=[atompages_cache_table_idx,pml4eroottb, get_sub_tb](phyaddr_t phybase,vaddr_t vaddr_base,uint16_t count,pgaccess access)->int{
        if(count==0||count>512){
            kputsSecure("AddressSpace::enable_VM_desc::_4lv_pte_4KB_entries_set:cross page directory boundary not allowed\n");
            return OS_OUT_OF_RANGE;//跨页目录边界不允许
        }
        if(phybase%_4KB_SIZE||vaddr_base%_4KB_SIZE)return OS_INVALID_PARAMETER;
        uint16_t pml4_index=(vaddr_base>>39)&((1<<9)-1);
        uint16_t pdpte_index=(vaddr_base>>30)&((1<<9)-1);
        uint16_t pde_index=(vaddr_base>>21)&((1<<9)-1);
        uint16_t pte_index=(vaddr_base>>12)&((1<<9)-1);
        if(pte_index+count>512){
            kputsSecure("AddressSpace::enable_VM_desc::_4lv_pte_4KB_entries_set:cross page directory boundary not allowed\n");
            return OS_OUT_OF_RANGE;//跨页目录边界不允许
        }//这里权限问题待解决
       PageTableEntryUnion pml4e=pml4eroottb[pml4_index];
        PageTableEntryUnion* pdpte_tb=get_sub_tb(pml4e,PageTableEntryType::PML4);
        if(!pdpte_tb)return OS_OUT_OF_MEMORY;
        PageTableEntryUnion* pde_tb=get_sub_tb(pdpte_tb[pdpte_index],PageTableEntryType::PDPT);
        if(!pde_tb)return OS_OUT_OF_MEMORY;
        PageTableEntryUnion* pte_tb=get_sub_tb(pde_tb[pde_index],PageTableEntryType::PD);
        if(!pte_tb)return OS_OUT_OF_MEMORY;
        //设置pte项
        for(uint16_t i=0;i<count;i++){
            PageTableEntryUnion& pte_entry=pte_tb[pte_index+i];
            pte_entry.raw=0;
            pte_entry.pte.present=1;
            pte_entry.pte.RWbit=access.is_writeable;
            pte_entry.pte.KERNELbit=!access.is_kernel;
            pte_entry.pte.page_addr=phybase/0x1000+i;
            pte_entry.pte.PWT=atompages_cache_table_idx.PWT;
            pte_entry.pte.PCD=atompages_cache_table_idx.PCD;
            pte_entry.pte.PAT=atompages_cache_table_idx.PAT;
            pte_entry.pte.EXECUTE_DENY=!access.is_executable;
            pte_entry.pte.global=access.is_global;
        }
        return OS_SUCCESS;
    };
    
    return 0;
}