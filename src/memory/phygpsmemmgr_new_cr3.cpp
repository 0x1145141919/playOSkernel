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
    bitset|=1ULL<<bitpos;
}
static inline void bitset64bits_clear(uint64_t&bitset,uint64_t bitpos)
{
    bitset&=~(1ULL<<bitpos);
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
        bitset64bits_write(pgtb_entry,PageTableEntry::P_BIT,false);
        return OS_SUCCESS;
    }else{
        bitset64bits_write(pgtb_entry,PageTableEntry::P_BIT,true);
        bitset64bits_write(pgtb_entry,PageTableEntry::RW_BIT,PgCB.flags.is_writable);
        bitset64bits_write(pgtb_entry,PageTableEntry::US_BIT,!PgCB.flags.is_kernel);
        bitset64bits_write(pgtb_entry,PageTableEntry::XD_BIT,!PgCB.flags.is_executable);
        //bitset64bits_write(pgtb_entry,PageTableEntry::XD_BIT,false);
        
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
    return OS_SUCCESS;
}
// 处理PTE级别（最后一级）页表的函数
int KernelSpacePgsMemMgr::process_pte_level(lowerlv_PgCBtb* pt_PgCBtb, uint64_t* pt_base, uint64_t base_vaddr) {
    pgflags tmp_flags;
    for (uint64_t l = 0; l < 512; l++) {
        tmp_flags = pt_PgCBtb->entries[l].flags;
        if (tmp_flags.is_exist) {
            // 处理4KB页
            phyaddr_t page_base = base_vaddr + (l << 12);
            pt_base[l]=0;
            int result = pgtb_entry_convert(pt_base[l], pt_PgCBtb->entries[l], page_base);
            if (result != OS_SUCCESS) {
                return result;
            }
        }
    }
    return OS_SUCCESS;
}

// 处理PD级别页表的函数
int KernelSpacePgsMemMgr::process_pd_level(lowerlv_PgCBtb* pd_PgCBtb, uint64_t* pd_base, uint64_t base_vaddr, bool is_5level) {
    pgflags tmp_flags;
    for (uint64_t k = 0; k < 512; k++) {
        tmp_flags = pd_PgCBtb->entries[k].flags;
        if (tmp_flags.is_exist) {
            if (tmp_flags.is_atom) {
                // 处理2MB大页
                phyaddr_t huge_2mb_base = base_vaddr + (k << 21);
                int result = pgtb_entry_convert(pd_base[k], pd_PgCBtb->entries[k], huge_2mb_base);
                if (result != OS_SUCCESS) {
                    return result;
                }
            } else {
                // 分配PT表
                uint64_t* pt_base = (uint64_t*)pgtb_heap_ptr->pgalloc();
                if (pt_base == nullptr) {
                    kputsSecure("pgalloc failed for pt_base\n");
                    return OS_OUT_OF_MEMORY;
                }
                pgtb_heap_ptr->clear((phyaddr_t)pt_base);
                // 转换PDE条目
                if(pd_PgCBtb->entries[k].flags.is_atom==0){
                    pd_PgCBtb->entries[k].flags.is_writable=1;
                }
                int result = pgtb_entry_convert(pd_base[k], pd_PgCBtb->entries[k], (phyaddr_t)pt_base);
                if (result != OS_SUCCESS) {
                    pgtb_heap_ptr->free((phyaddr_t)pt_base);
                    return result;
                }
                
                lowerlv_PgCBtb* pt_PgCBtb = pd_PgCBtb->entries[k].base.lowerlvPgCBtb;
                uint64_t next_base_vaddr = base_vaddr + (k << 21);
                result = process_pte_level(pt_PgCBtb, pt_base, next_base_vaddr);
                if (result != OS_SUCCESS) {
                    pgtb_heap_ptr->free((phyaddr_t)pt_base);
                    return result;
                }
            }
        }
    }
    return OS_SUCCESS;
}

// 处理PDPT级别页表的函数
int KernelSpacePgsMemMgr::process_pdpt_level(lowerlv_PgCBtb* pdpt_PgCBtb, uint64_t* pdpt_base, uint64_t base_vaddr, bool is_5level) {
    pgflags tmp_flags;
    for (uint64_t j = 0; j < 512; j++) {
        tmp_flags = pdpt_PgCBtb->entries[j].flags;
        if (tmp_flags.is_exist) {
            if (tmp_flags.is_atom) {
                // 处理1GB大页
                phyaddr_t huge_1gb_base = base_vaddr + (j << 30);
                int result = pgtb_entry_convert(pdpt_base[j], pdpt_PgCBtb->entries[j], huge_1gb_base);
                if (result != OS_SUCCESS) {
                    return result;
                }
            } else {
                // 分配PD表
                uint64_t* pd_base = (uint64_t*)pgtb_heap_ptr->pgalloc();
                if (pd_base == nullptr) {
                    kputsSecure("pgalloc failed for pd_base\n");
                    return OS_OUT_OF_MEMORY;
                }
                pgtb_heap_ptr->clear((phyaddr_t)pd_base);
                // 转换PDPTE条目
                if(pdpt_PgCBtb->entries[j].flags.is_atom==0){
                    pdpt_PgCBtb->entries[j].flags.is_writable=1;
                }
                int result = pgtb_entry_convert(pdpt_base[j], pdpt_PgCBtb->entries[j], (phyaddr_t)pd_base);
                if (result != OS_SUCCESS) {
                    pgtb_heap_ptr->free((phyaddr_t)pd_base);
                    return result;
                }
                
                lowerlv_PgCBtb* pd_PgCBtb = pdpt_PgCBtb->entries[j].base.lowerlvPgCBtb;
                uint64_t next_base_vaddr = base_vaddr + (j << 30);
                result = process_pd_level(pd_PgCBtb, pd_base, next_base_vaddr, is_5level);
                if (result != OS_SUCCESS) {
                    pgtb_heap_ptr->free((phyaddr_t)pd_base);
                    return result;
                }
            }
        }
    }
    return OS_SUCCESS;
}

// 处理PML4级别页表的函数
int KernelSpacePgsMemMgr::process_pml4_level(lowerlv_PgCBtb* pml4_PgCBtb, uint64_t* pml4_base, uint64_t base_vaddr, bool is_5level) {
    pgflags tmp_flags;
    for (uint64_t i = 0; i < 512; i++) {
        tmp_flags = pml4_PgCBtb->entries[i].flags;
        if (tmp_flags.is_exist) {
            if (tmp_flags.is_atom) {
                kputsSecure("atom entry in Pml4 not support\n");
                return OS_BAD_FUNCTION;
            } else {
                // 分配PDPT表
                uint64_t* pdpt_base = (uint64_t*)pgtb_heap_ptr->pgalloc();
                if (pdpt_base == nullptr) {
                    kputsSecure("pgalloc failed for pdpt_base\n");
                    return OS_OUT_OF_MEMORY;
                }
                pgtb_heap_ptr->clear((phyaddr_t)pml4_base);
                // 转换PML4E条目
                if(pml4_PgCBtb->entries[i].flags.is_atom==0){
                    pml4_PgCBtb->entries[i].flags.is_writable=1;
                }
                int result = pgtb_entry_convert(pml4_base[i], pml4_PgCBtb->entries[i], (phyaddr_t)pdpt_base);
                if (result != OS_SUCCESS) {
                    pgtb_heap_ptr->free((phyaddr_t)pdpt_base);
                    return result;
                }
                
                lowerlv_PgCBtb* pdpt_PgCBtb = pml4_PgCBtb->entries[i].base.lowerlvPgCBtb;
                uint64_t next_base_vaddr = base_vaddr + (i << (is_5level ? 39 : 30));
                result = process_pdpt_level(pdpt_PgCBtb, pdpt_base, next_base_vaddr, is_5level);
                if (result != OS_SUCCESS) {
                    pgtb_heap_ptr->free((phyaddr_t)pdpt_base);
                    return result;
                }
            }
        }
    }
    return OS_SUCCESS;
}

// 处理PML5级别页表的函数
int KernelSpacePgsMemMgr::process_pml5_level(PgControlBlockHeader* pml5_PgCBtb, uint64_t* pml5_base, bool is_5level) {
    pgflags tmp_flags;
    for (uint64_t i = 0; i < 512; i++) {
        tmp_flags = pml5_PgCBtb[i].flags;
        if (tmp_flags.is_exist) {
            if (tmp_flags.is_atom) {
                kputsSecure("atom entry in Pml5 not support\n");
                return OS_BAD_FUNCTION;
            } else {
                // 分配PML4表
                uint64_t* pml4_base = (uint64_t*)pgtb_heap_ptr->pgalloc();
                if (pml4_base == nullptr) {
                    kputsSecure("pgalloc failed for pml4_base\n");
                    return OS_OUT_OF_MEMORY;
                }
                
                // 转换PML5E条目
                int result = pgtb_entry_convert(pml5_base[i], pml5_PgCBtb[i], (phyaddr_t)pml4_base);
                if (result != OS_SUCCESS) {
                    pgtb_heap_ptr->free((phyaddr_t)pml4_base);
                    return result;
                }
                
                lowerlv_PgCBtb* pml4_PgCBtb = pml5_PgCBtb[i].base.lowerlvPgCBtb;
                uint64_t next_base_vaddr = static_cast<uint64_t>(i) << 48;
                result = process_pml4_level(pml4_PgCBtb, pml4_base, next_base_vaddr, is_5level);
                if (result != OS_SUCCESS) {
                    pgtb_heap_ptr->free((phyaddr_t)pml4_base);
                    return result;
                }
            }
        }
    }
    return OS_SUCCESS;
}

// 重构后的4级页表构建函数
int KernelSpacePgsMemMgr::pgtb_lowaddr_equalmap_construct_4lvpg() {
    // 获取PML4级别的控制块表
    lowerlv_PgCBtb *root_PgCBtb = rootlv4PgCBtb->base.lowerlvPgCBtb;
    uint64_t* rootPgtb = (uint64_t*)pgtb_heap_ptr->pgalloc();
    pgtb_heap_ptr->pgtb_root_phyaddr =rootPgtb;
    if (rootPgtb == nullptr) {
        kputsSecure("pgtb_lowaddr_equalmap_construct_4lvpg:pgalloc failed for rootPgtb\n");
        return OS_OUT_OF_MEMORY;
    }
    
    // 处理PML4级别
    int result = process_pml4_level(root_PgCBtb, rootPgtb, 0, false);
    if (result != OS_SUCCESS) {
        pgtb_heap_ptr->free((phyaddr_t)rootPgtb);
        return result;
    }
    

    
    return OS_SUCCESS;
}

// 重构后的5级页表构建函数
int KernelSpacePgsMemMgr::pgtb_lowaddr_equalmap_construct_5lvpg() {
    // 获取PML5级别的控制块表
    PgControlBlockHeader* root_PgCBtb = rootlv4PgCBtb;
    uint64_t* rootPgtb = (uint64_t*)pgtb_heap_ptr->pgalloc();
    
    if (rootPgtb == nullptr) {
        kputsSecure("pgtb_lowaddr_equalmap_construct_5lvpg:pgalloc failed for rootPgtb\n");
        return OS_OUT_OF_MEMORY;
    }
    
    // 处理PML5级别
    int result = process_pml5_level(root_PgCBtb, rootPgtb, true);
    if (result != OS_SUCCESS) {
        pgtb_heap_ptr->free((phyaddr_t)rootPgtb);
        return result;
    }
    
    
    return OS_SUCCESS;
}

void KernelSpacePgsMemMgr::enable_new_cr3() {
    switch (cpu_pglv) {
        case 4:
            pgtb_lowaddr_equalmap_construct_4lvpg();
            break;
        case 5:
            pgtb_lowaddr_equalmap_construct_5lvpg();
            break;
        default:
            return;
    }
    /*phy_memDesriptor*global_tb=gBaseMemMgr.getGlobalPhysicalMemoryInfo();
    uint64_t count=gBaseMemMgr.getRootPhysicalMemoryDescriptorTableEntryCount();
    vaddr_t valloc_base=(cpu_pglv==4?0xffff800000000000:0xff<<56ULL);
    vaddr_t scan_addr=valloc_base;
    
    
    for(uint64_t i=0;i<count;i++)
    {   
        pgflags tmp_flags={0};
        pgaccess tmp_access={0};
        tmp_access.is_kernel=tmp_flags.is_kernel=1;
        tmp_flags.is_exist=1;
        tmp_access.is_readable= tmp_flags.is_readable=1;
        switch(global_tb[i].Type)
        {
            case EFI_RUNTIME_SERVICES_CODE:
            case OS_KERNEL_CODE:
            tmp_access.is_executable= tmp_flags.is_executable=1;
            case EFI_RUNTIME_SERVICES_DATA:
            case OS_KERNEL_DATA:
            case OS_KERNEL_STACK:
            case EFI_ACPI_RECLAIM_MEMORY:
            case EFI_MEMORY_MAPPED_IO:
            tmp_access.is_global= tmp_flags.is_global=1;
            tmp_flags.physical_or_virtual_pg=1;
            tmp_access.is_occupyied= tmp_flags.is_occupied=1;
            tmp_access.cache_strategy=tmp_flags.cache_strateggy=cache_strategy_t::WB;
            if(global_tb[i].Type==EFI_MEMORY_MAPPED_IO)
            tmp_access.cache_strategy=tmp_flags.cache_strateggy=cache_strategy_t::UC;
            vaddr_objs[valid_vaddrobj_count].base=scan_addr;
            global_tb[i].VirtualStart=scan_addr;
            vaddr_objs[valid_vaddrobj_count].type=(PHY_MEM_TYPE)global_tb[i].Type;
            vaddr_objs[valid_vaddrobj_count].size_in_numof4kbpgs=global_tb[i].NumberOfPages;
            vaddr_objs[valid_vaddrobj_count].flags=tmp_flags;
            vaddr_objs[valid_vaddrobj_count].max_num_of_subtb_entries=vaddr_objs[valid_vaddrobj_count].num_of_subtb_entries=1;
            vaddr_objs[valid_vaddrobj_count].subtb=new vaddr_seg_subtb_t;
            vaddr_objs[valid_vaddrobj_count].subtb->phybase=global_tb[i].PhysicalStart;
            vaddr_objs[valid_vaddrobj_count].subtb->num_of_4kbpgs=global_tb[i].NumberOfPages;
            phymem_pgs_queue* pgs_pacage=seg_to_queue(scan_addr,global_tb[i].NumberOfPages<<12);
            if(pgs_pacage==nullptr){
                kputsSecure("seg_to_queue failed\n");
                delete pgs_pacage;
                return;
            }
             Inner_fixed_addr_manage(scan_addr,*pgs_pacage,tmp_access,true);
             valid_vaddrobj_count++;
             delete pgs_pacage;
             break;            
        }
    }*/
    LocalCPU localcpu;
    uint64_t new_cr3=0;
    new_cr3=(uint64_t)pgtb_heap_ptr->pgtb_root_phyaddr;
    new_cr3|=kernel_sapce_PCID;
    localcpu.set_cr3(new_cr3);
    localcpu.load_cr3();
    asm volatile (
    "push %0\n\t"
    "pop %0\n\t"
    "mov %0, -8(%%rsp)"
    : 
    : "r" (new_cr3)
    : "memory"
);
    kputsSecure("new_cr3 enabled:");
    kpnumSecure(&new_cr3,UNHEX,8);
    localcpu.set_ia32_pat(cache_strategy_table.value);
}