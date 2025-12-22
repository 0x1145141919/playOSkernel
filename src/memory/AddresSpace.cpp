#include "memory/AddresSpace.h"
#include "memory/Memory.h"
#include "memory/phygpsmemmgr.h"
#include "memory/kpoolmemmgr.h"
#include "memory/phyaddr_accessor.h"    
#include "os_error_definitions.h"
#include "linker_symbols.h"
#include "VideoDriver.h"
#include "util/OS_utils.h"
AddressSpace::AddressSpace()
{
}
static inline uint64_t align_down(uint64_t x, uint64_t a){ return x & ~(a-1); }
int AddressSpace::enable_VM_desc(VM_DESC desc)
    
{
    /**
     * 先搞参数检验
     */
    if(
        desc.start>=desc.end||
        desc.end>(pglv_4_or_5?PAGE_LV4_USERSPACE_SIZE:PAGE_LV5_USERSPACE_SIZE)||
        desc.start%_4KB_SIZE||
        desc.end%_4KB_SIZE||
        desc.phys_start<16*_4KB_SIZE||
        desc.end%_4KB_SIZE
    )return OS_INVALID_ADDRESS;
    PageTableEntryUnion*pml4eroottb=(PageTableEntryUnion*)pml4;
    pgaccess desc_access=desc.access;
    cache_table_idx_struct_t atompages_cache_table_idx=cache_strategy_to_idx(desc_access.cache_strategy);
    
    
    /**
     * 
     * int _4lv_pdpte_1GB_entries_set(phyaddr_t phybase,vaddr_t vaddr_base,uint16_t count,pgaccess access);//这里要求的是不能跨pml4e边界
    int _4lv_pde_2MB_entries_set(phyaddr_t phybase,vaddr_t vaddr_base,uint16_t count,pgaccess access);//这里要求的是不能跨页目录指针边界
    int _4lv_pte_4KB_entries_set(phyaddr_t phybase,vaddr_t vaddr_base,uint16_t count,pgaccess access);//这里要求的是不能跨页
    三个工具匿名函数，用于设置4级页表项，返回值是错误码 
    */
    // 匿名函数替代原来的get_sub_tb函数
    bool alloced=false;
    auto get_sub_tb = [&alloced](PageTableEntryUnion& entry, PageTableEntryType Clevel) -> phyaddr_t {
        if(Clevel==PageTableEntryType::PT) return 0;
        constexpr uint32_t _4KB_SIZE=0x1000;
        PageTableEntryUnion  copy=entry;
        if(copy.raw&PageTableEntry::P_MASK){
            phyaddr_t subtb_phybase=(copy.pte.page_addr*_4KB_SIZE) & PHYS_ADDR_MASK;
            if(Clevel==PageTableEntryType::PDPT||Clevel==PageTableEntryType::PD)
            {
                if(copy.raw&PDE::PS_MASK) return 0;
            }
            
            return subtb_phybase;
        }else{
            phyaddr_t entry_to_alloc_phybase=0;
            entry_to_alloc_phybase = phymemspace_mgr::pages_alloc(1, phymemspace_mgr::KERNEL, 12);
            if(!entry_to_alloc_phybase) return 0;
            
            // 初始化新分配的页表内存为0
            for(uint16_t i=0; i<512; i++) {
                uint64_t offset = sizeof(PageTableEntryUnion) * i;
                PhyAddrAccessor::writeu64(entry_to_alloc_phybase + offset, 0);
            }
            alloced=true;
            entry.raw=0;
            entry.pte.KERNELbit=1;
            entry.pte.present=1;
            entry.pte.RWbit=1;
            entry.pte.page_addr=entry_to_alloc_phybase/_4KB_SIZE;
            return entry_to_alloc_phybase;
        }
    };

    auto _4lv_pte_4KB_entries_set=[atompages_cache_table_idx,pml4eroottb, get_sub_tb,desc_access](phyaddr_t phybase,vaddr_t vaddr_base,uint16_t count)->int{
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
        phyaddr_t pdpte_tb_phyaddr = get_sub_tb(pml4e,PageTableEntryType::PML4);
        if(!pdpte_tb_phyaddr)return OS_OUT_OF_MEMORY;
        
        uint64_t pdpte_offset = sizeof(PageTableEntryUnion) * pdpte_index;
        uint64_t pdpte_addr = pdpte_tb_phyaddr + pdpte_offset;
        uint64_t pdpte_raw = PhyAddrAccessor::readu64(pdpte_addr);
        PageTableEntryUnion pdpte_entry = { .raw = pdpte_raw };
        
        phyaddr_t pde_tb_phyaddr = get_sub_tb(pdpte_entry, PageTableEntryType::PDPT);
        if(!pde_tb_phyaddr)
            if((pdpte_raw&PDPTE::PS_MASK)&&(pdpte_raw&PageTableEntry::P_MASK))
                return OS_RESOURCE_CONFILICT;
            else 
                return OS_OUT_OF_MEMORY;
        // 如果在get_sub_tb中修改了pdpte_entry（例如分配了新的页表），需要将修改写回
        if (!(pdpte_raw & PageTableEntry::P_MASK)&&(pdpte_entry.pdpte.present)) {
            PhyAddrAccessor::writeu64(pdpte_addr, pdpte_entry.raw);
        }
        
        uint64_t pde_offset = sizeof(PageTableEntryUnion) * pde_index;
        uint64_t pde_addr = pde_tb_phyaddr + pde_offset;
        uint64_t pde_raw = PhyAddrAccessor::readu64(pde_addr);
        PageTableEntryUnion pde_entry = { .raw = pde_raw };
        
        phyaddr_t pte_tb_phyaddr = get_sub_tb(pde_entry, PageTableEntryType::PD);
        if(!pte_tb_phyaddr)
            if(pde_raw&PageTableEntry::P_MASK&&pde_raw&PDE::PS_MASK)
            return OS_OUT_OF_MEMORY;
            else return OS_RESOURCE_CONFILICT;
        // 如果在get_sub_tb中修改了pde_entry（例如分配了新的页表），需要将修改写回
        if (!(pde_raw & PageTableEntry::P_MASK)&&(pde_entry.pde.present)) {
            PhyAddrAccessor::writeu64(pde_addr, pde_entry.raw);
        }
        
        //设置pte项
        for(uint16_t i=0;i<count;i++){
            uint64_t pte_offset = sizeof(PageTableEntryUnion) * (pte_index + i);
            uint64_t pte_addr = pte_tb_phyaddr + pte_offset;
            
            PageTableEntryUnion pte_entry;
            pte_entry.raw=0;
            pte_entry.pte.present=1;
            pte_entry.pte.RWbit=desc_access.is_writeable;
            pte_entry.pte.KERNELbit=!desc_access.is_kernel;
            pte_entry.pte.page_addr=(phybase/0x1000)+i;
            pte_entry.pte.PWT=atompages_cache_table_idx.PWT;
            pte_entry.pte.PCD=atompages_cache_table_idx.PCD;
            pte_entry.pte.PAT=atompages_cache_table_idx.PAT;
            pte_entry.pte.EXECUTE_DENY=!desc_access.is_executable;
            pte_entry.pte.global=desc_access.is_global;
            
            PhyAddrAccessor::writeu64(pte_addr, pte_entry.raw);
        }
        return OS_SUCCESS;
    };
    // ==================== 2. 2MB 大页版 ====================
    auto _4lv_pde_2MB_entries_set = [desc_access,atompages_cache_table_idx, pml4eroottb, get_sub_tb](
        phyaddr_t phybase, vaddr_t vaddr_base, uint16_t count) -> int {
        if (count == 0 || count > 512) {
            kputsSecure("AddressSpace::enable_VM_desc::_4lv_pde_2MB_entries_set: invalid count\n");
            return OS_OUT_OF_RANGE;
        }
        constexpr uint32_t _2MB_SIZE = 0x200000;
        if (phybase % _2MB_SIZE || vaddr_base % _2MB_SIZE) return OS_INVALID_PARAMETER;

        uint16_t pml4_index  = (vaddr_base >> 39) & 0x1FF;
        uint16_t pdpte_index = (vaddr_base >> 30) & 0x1FF;
        uint16_t pde_index   = (vaddr_base >> 21) & 0x1FF;

        if (pde_index + count > 512) return OS_OUT_OF_RANGE;  // 不能跨 PD 边界

        PageTableEntryUnion pml4e = pml4eroottb[pml4_index];
        phyaddr_t pdpte_tb_phyaddr = get_sub_tb(pml4e, PageTableEntryType::PML4);
        if (!pdpte_tb_phyaddr) return OS_OUT_OF_MEMORY;
        
        uint64_t pdpte_offset = sizeof(PageTableEntryUnion) * pdpte_index;
        uint64_t pdpte_addr = pdpte_tb_phyaddr + pdpte_offset;
        uint64_t pdpte_raw = PhyAddrAccessor::readu64(pdpte_addr);
        PageTableEntryUnion pdpte_entry = { .raw = pdpte_raw };
        
        phyaddr_t pde_tb_phyaddr = get_sub_tb(pdpte_entry, PageTableEntryType::PDPT);
        if(!pde_tb_phyaddr)
            if((pdpte_raw&PDPTE::PS_MASK)&&(pdpte_raw&PageTableEntry::P_MASK))
                return OS_RESOURCE_CONFILICT;
            else 
                return OS_OUT_OF_MEMORY;
            
        // 如果在get_sub_tb中修改了pdpte_entry（例如分配了新的页表），需要将修改写回
        if (!(pdpte_raw & PageTableEntry::P_MASK)&&(pdpte_entry.pdpte.present)) {
            PhyAddrAccessor::writeu64(pdpte_addr, pdpte_entry.raw);
        }
        
        // 设置pde项
        for(uint16_t i=0;i<count;i++){
            uint64_t pde_offset = sizeof(PageTableEntryUnion) * (pde_index + i);
            uint64_t pde_addr = pde_tb_phyaddr + pde_offset;
            
            PageTableEntryUnion pde_entry;
            pde_entry.raw = 0;
            pde_entry.raw |= PDE::PS_MASK;
            pde_entry.pde2MB.present = 1;
            pde_entry.pde2MB.RWbit = desc_access.is_writeable;
            pde_entry.pde2MB.KERNELbit = !desc_access.is_kernel;
            pde_entry.pde2MB._2mb_Addr = phybase / _2MB_SIZE + i;
            pde_entry.pde2MB.PWT = atompages_cache_table_idx.PWT;
            pde_entry.pde2MB.PCD = atompages_cache_table_idx.PCD;
            pde_entry.pde2MB.PAT = atompages_cache_table_idx.PAT;
            pde_entry.pde2MB.EXECUTE_DENY = !desc_access.is_executable;
            pde_entry.pde2MB.global = desc_access.is_global;
            
            PhyAddrAccessor::writeu64(pde_addr, pde_entry.raw);
        }
        return OS_SUCCESS;
    };

    // ==================== 3. 1GB 大页版 ====================
    auto _4lv_pdpte_1GB_entries_set = [desc_access,atompages_cache_table_idx, pml4eroottb, get_sub_tb](
        phyaddr_t phybase, vaddr_t vaddr_base, uint64_t count) -> int {
        if (count == 0 || count > 512) {
            kputsSecure("AddressSpace::enable_VM_desc::_4lv_pdpte_1GB_entries_set: invalid count\n");
            return OS_OUT_OF_RANGE;
        }
        constexpr uint64_t _1GB_SIZE = 0x40000000ULL;
        if (phybase % _1GB_SIZE || vaddr_base % _1GB_SIZE) return OS_INVALID_PARAMETER;

        uint16_t pml4_index  = (vaddr_base >> 39) & 0x1FF;
        uint16_t pdpte_index = (vaddr_base >> 30) & 0x1FF;

        if (pdpte_index + count > 512) return OS_OUT_OF_RANGE;  // 不能跨 PML4 边界

        PageTableEntryUnion pml4e = pml4eroottb[pml4_index];
        phyaddr_t pdpte_tb_phyaddr = get_sub_tb(pml4e, PageTableEntryType::PML4);
        if (!pdpte_tb_phyaddr) return OS_OUT_OF_MEMORY;
        
        uint64_t pdpte_offset = sizeof(PageTableEntryUnion) * pdpte_index;
        uint64_t pdpte_addr = pdpte_tb_phyaddr + pdpte_offset;
        uint64_t pdpte_raw = PhyAddrAccessor::readu64(pdpte_addr);
        PageTableEntryUnion pdpte_entry = { .raw = pdpte_raw };
        
        // 如果在get_sub_tb中修改了pml4e（例如分配了新的页表），需要将修改写回
        if (!(pdpte_raw & PageTableEntry::P_MASK)&&(pml4e.pml4.present)) {
            PhyAddrAccessor::writeu64(pdpte_addr, pml4e.raw);
        }
        
        // 设置pdpte项
        for(uint16_t i=0;i<count;i++){
            uint64_t pdpte_offset = sizeof(PageTableEntryUnion) * (pdpte_index + i);
            uint64_t pdpte_addr = pdpte_tb_phyaddr + pdpte_offset;
            
            PageTableEntryUnion pdpte_entry;
            pdpte_entry.raw = 0;
            pdpte_entry.raw |= PDPTE::PS_MASK;
            pdpte_entry.pdpte1GB.present = 1;
            pdpte_entry.pdpte1GB.RWbit = desc_access.is_writeable;
            pdpte_entry.pdpte1GB.KERNELbit = !desc_access.is_kernel;
            pdpte_entry.pdpte1GB._1GB_Addr = (phybase / _1GB_SIZE) + i;
            pdpte_entry.pdpte1GB.PWT = atompages_cache_table_idx.PWT;
            pdpte_entry.pdpte1GB.PCD = atompages_cache_table_idx.PCD;
            pdpte_entry.pdpte1GB.PAT = atompages_cache_table_idx.PAT;
            pdpte_entry.pdpte1GB.EXECUTE_DENY = !desc_access.is_executable;
            pdpte_entry.pdpte1GB.global = desc_access.is_global;
            
            PhyAddrAccessor::writeu64(pdpte_addr, pdpte_entry.raw);
        }
        return OS_SUCCESS;
    };
    /**
     * 还有一个seg_to_pages_info_get
     */
    auto seg_to_pages_info_get = [desc](seg_to_pages_info_pakage_t& result) -> int { 
        VM_DESC vmentry = desc;
        if (vmentry.start % _4KB_SIZE || vmentry.end % _4KB_SIZE || vmentry.start % _1GB_SIZE != vmentry.phys_start % _1GB_SIZE)
        return OS_INVALID_PARAMETER;

    // initialize
    for (int i = 0; i < 5; i++) {
        result.entryies[i].vbase = 0;
        result.entryies[i].base = 0;
        result.entryies[i].page_size_in_byte = 0;
        result.entryies[i].num_of_pages = 0;
    }

    int idx = 0;

    // Middle full 1GB-aligned region(s)
    vaddr_t _1GB_base = align_up(vmentry.start, _1GB_SIZE);
    vaddr_t _1GB_end = align_down(vmentry.end, _1GB_SIZE);

    if (_1GB_end > _1GB_base) {
        result.entryies[idx].vbase = _1GB_base;
        result.entryies[idx].base = vmentry.phys_start + (_1GB_base - vmentry.start);
        result.entryies[idx].page_size_in_byte = _1GB_SIZE;
        result.entryies[idx].num_of_pages = (_1GB_end - _1GB_base) / _1GB_SIZE;
        idx++;
    }

    // Helper lambda to process a leftover segment [seg_s, seg_e)
    auto process_segment = [&](vaddr_t seg_s, vaddr_t seg_e) {
        if (seg_e <= seg_s) return;
        // 2MB aligned inner region
        vaddr_t _2MB_base = align_up(seg_s, _2MB_SIZE);
        vaddr_t _2MB_end = align_down(seg_e, _2MB_SIZE);

        // head 4KB chunk (before first 2MB boundary)
        if (_2MB_base > seg_s) {
            if (idx >= 5) return; // defensive, shouldn't happen
            result.entryies[idx].vbase = seg_s;
            result.entryies[idx].base = vmentry.phys_start + (seg_s - vmentry.start);
            result.entryies[idx].page_size_in_byte = _4KB_SIZE;
            result.entryies[idx].num_of_pages = (_2MB_base - seg_s) / _4KB_SIZE;
            idx++;
        }

        // middle 2MB-aligned pages
        if (_2MB_end > _2MB_base) {
            if (idx >= 5) return;
            result.entryies[idx].vbase = _2MB_base;
            result.entryies[idx].base = vmentry.phys_start + (_2MB_base - vmentry.start);
            result.entryies[idx].page_size_in_byte = _2MB_SIZE;
            result.entryies[idx].num_of_pages = (_2MB_end - _2MB_base) / _2MB_SIZE;
            idx++;
        }

        // tail 4KB chunk (after last 2MB boundary)
        if (_2MB_end < seg_e) {
            if (idx >= 5) return;
            result.entryies[idx].vbase = _2MB_end;
            result.entryies[idx].base = vmentry.phys_start + (_2MB_end - vmentry.start);
            result.entryies[idx].page_size_in_byte = _4KB_SIZE;
            result.entryies[idx].num_of_pages = (seg_e - _2MB_end) / _4KB_SIZE;
            idx++;
        }
    };

    // Process lower leftover: [start, _1GB_base)
    process_segment(vmentry.start, _1GB_base > vmentry.start ? _1GB_base : vmentry.start);

    // Process upper leftover: [_1GB_end, end)
    process_segment(_1GB_end < vmentry.end ? _1GB_end : vmentry.end, vmentry.end);

    return OS_SUCCESS;
    };
    //这里才是正式逻辑
    seg_to_pages_info_pakage_t package={0};
    int status = seg_to_pages_info_get(package);
    if(pglv_4_or_5==PAGE_TBALE_LV::LV_4)
    {
        lock.write_lock();
        for(int i=0;i<5;i++)
        {
            seg_to_pages_info_pakage_t::pages_info_t entry=package.entryies[i];
            if(entry.page_size_in_byte==_4KB_SIZE){
                status=_4lv_pte_4KB_entries_set(entry.vbase,entry.num_of_pages,entry.num_of_pages);
                if(status!=OS_SUCCESS)goto sub_step_invalid;
            }else if(entry.page_size_in_byte==_2MB_SIZE)
            {
                status=_4lv_pde_2MB_entries_set(entry.vbase,entry.num_of_pages,entry.num_of_pages);
                if(status!=OS_SUCCESS)goto sub_step_invalid;
            }else if(entry.page_size_in_byte==_1GB_SIZE)
            {
                uint64_t pdpte_equal_offset = entry.vbase / _1GB_SIZE;
                uint64_t count_to_assign_left = entry.num_of_pages;
                uint16_t pml4_idx = pdpte_equal_offset >> 9;
                uint16_t pdpte_idx = pdpte_equal_offset & 0x1FF;
                uint64_t processed_pages = 0;
                auto min = [](uint64_t a, uint64_t b)->uint64_t { return a < b ? a : b; };
                while(count_to_assign_left > 0){
                    uint16_t this_count = static_cast<uint16_t>(min(count_to_assign_left, 512 - pdpte_idx));
                    phyaddr_t this_phybase = entry.base + processed_pages * _1GB_SIZE;
                    vaddr_t this_vbase = entry.vbase + processed_pages * _1GB_SIZE;
                    status = _4lv_pdpte_1GB_entries_set(this_phybase, this_vbase, this_count);
                    if(status != OS_SUCCESS) goto sub_step_invalid;
                    count_to_assign_left -= this_count;
                    processed_pages += this_count;
                    pdpte_idx = 0;
                    pml4_idx++;
                }
                
            }else if(entry.page_size_in_byte!=0){
                goto page_size_invalid;
            }
        }
        occupyied_size+=(desc.end-desc.start);
        goto success;
    }else{
        return OS_NOT_SUPPORT;
    }

    
    success:
    lock.write_unlock();
    return OS_SUCCESS;
    page_size_invalid:
    lock.write_unlock();
    return OS_ERROR_BASE;
    sub_step_invalid:
    lock.write_unlock();
    return status;
}
int AddressSpace::disable_VM_desc(VM_DESC desc)
{
    // 参数校验（与 enable_VM_desc 保持一致）
    if(
        desc.start>=desc.end||
        desc.end>(pglv_4_or_5?PAGE_LV4_USERSPACE_SIZE:PAGE_LV5_USERSPACE_SIZE)||
        desc.start%_4KB_SIZE||
        desc.end%_4KB_SIZE||
        desc.phys_start<16*_4KB_SIZE||
        desc.end%_4KB_SIZE
    )return OS_INVALID_ADDRESS;
    uint64_t currunt_roottb_phyaddr=0;
    asm volatile("mov %%cr3, %0" : "=r"(currunt_roottb_phyaddr));
    bool will_invalidate_soon=align_down(currunt_roottb_phyaddr, _4KB_SIZE)==this->pml4_phybase;
    PageTableEntryUnion* pml4eroottb = (PageTableEntryUnion*)pml4;
    pgaccess desc_access = desc.access;
    cache_table_idx_struct_t atompages_cache_table_idx = cache_strategy_to_idx(desc_access.cache_strategy);

    //新增错误码OS_PGTB_FREE_VALIDATION_FAIL
    // 清 4KB PTE 范围（不会分配表，表不存在就认为已清空）
   // ==== 4KB PTE 清理（含物理地址校验 + invlpg） ====
auto _4lv_pte_4KB_entries_clear = [pml4eroottb, will_invalidate_soon](
    phyaddr_t phybase, vaddr_t vaddr_base, uint16_t count) -> int
{
    if (count == 0 || count > 512) {
        kputsSecure("AddressSpace::disable_VM_desc::_4lv_pte_4KB_entries_clear: invalid count\n");
        return OS_OUT_OF_RANGE;
    }
    if (vaddr_base % _4KB_SIZE) return OS_INVALID_PARAMETER;

    uint16_t pml4_index  = (vaddr_base >> 39) & 0x1FF;
    uint16_t pdpte_index = (vaddr_base >> 30) & 0x1FF;
    uint16_t pde_index   = (vaddr_base >> 21) & 0x1FF;
    uint16_t pte_index   = (vaddr_base >> 12) & 0x1FF;

    if (pte_index + count > 512) return OS_OUT_OF_RANGE;

    phyaddr_t pdpt_base=pml4eroottb[pml4_index].pml4.pdpte_addr<<12;
    uint64_t pdpte_raw=PhyAddrAccessor::readu64(pdpt_base+pdpte_index*sizeof(PageTableEntryUnion));
    if(!(pdpte_raw&PageTableEntry::P_MASK))
    { 
        return OS_RESOURCE_CONFILICT;
    }else {
        if(pdpte_raw&PDPTE::PS_MASK)return OS_RESOURCE_CONFILICT;
    }
    phyaddr_t pde_base=align_down(pdpte_raw,_4KB_SIZE)&PHYS_ADDR_MASK;
    uint64_t pde_raw=PhyAddrAccessor::readu64(pde_base+pde_index*sizeof(PageTableEntryUnion));
    if(!(pde_raw&PageTableEntry::P_MASK))
    { 
        return OS_RESOURCE_CONFILICT;
    }else {
        if(pde_raw&PDE::PS_MASK)return OS_RESOURCE_CONFILICT;
    }
    phyaddr_t pte_base=align_down(pde_raw,_4KB_SIZE)&PHYS_ADDR_MASK;
    for(uint16_t i=pte_index;i<pte_index+count;i++)
    {
        uint64_t pte_raw=PhyAddrAccessor::readu64(pte_base+i*sizeof(PageTableEntryUnion));
        if(!(pte_raw&PageTableEntry::P_MASK))
        {
            return OS_RESOURCE_CONFILICT;
        }
        phyaddr_t pte_phyaddr=align_down(pte_raw,_4KB_SIZE)&PHYS_ADDR_MASK;
        if(pte_phyaddr!=phybase+(i-pte_index)*_4KB_SIZE)return OS_RESOURCE_CONFILICT;
        PhyAddrAccessor::writeu64(pte_base+i*sizeof(PageTableEntryUnion),0);
        vaddr_t vaddr=vaddr_base+(i-pte_index)*_4KB_SIZE;
        asm volatile("invlpg %0" : : "m"(vaddr));
    }
    bool all_clear=true;
    for(uint16_t i=0;i<512;i++){
        if(PhyAddrAccessor::readu64(pte_base+i*sizeof(PageTableEntryUnion))){
            all_clear=false;
            break;
        }
    }
    if(all_clear)
     {
        phymemspace_mgr::pages_recycle(pte_base,1);
        PhyAddrAccessor::writeu64(pde_base+pde_index*sizeof(PageTableEntryUnion),0);
        
        // Check if the entire PD is now empty and can be recycled
        bool pd_all_clear = true;
        for (uint16_t i = 0; i < 512; i++) {
            if (PhyAddrAccessor::readu64(pde_base + i * sizeof(PageTableEntryUnion))) {
                pd_all_clear = false;
                break;
            }
        }
        
        if (pd_all_clear) {
            // Recycle the entire PD page
            phymemspace_mgr::pages_recycle(pde_base, 1);
            PhyAddrAccessor::writeu64(pdpt_base + pdpte_index * sizeof(PageTableEntryUnion), 0);
            
            // Check if the entire PDPT is now empty and can be recycled
            bool pdpt_all_clear = true;
            for (uint16_t i = 0; i < 512; i++) {
                if (PhyAddrAccessor::readu64(pdpt_base + i * sizeof(PageTableEntryUnion))) {
                    pdpt_all_clear = false;
                    break;
                }
            }
            
            if (pdpt_all_clear) {
                // Recycle the entire PDPT page
                phymemspace_mgr::pages_recycle(pdpt_base, 1);
                PhyAddrAccessor::writeu64((phyaddr_t)pml4eroottb + pml4_index * sizeof(PageTableEntryUnion), 0);
            }
        }
     }
    return OS_SUCCESS;
};



    // 清 2MB PDE 范围（若上级不存在则视为已清空）
    // ==== 2MB PDE 清理（含物理地址校验 + invlpg） ====
auto _4lv_pde_2MB_entries_clear = [pml4eroottb, will_invalidate_soon](
    phyaddr_t phybase, vaddr_t vaddr_base, uint16_t count) -> int
{
    if (count == 0 || count > 512) {
        kputsSecure("AddressSpace::disable_VM_desc::_4lv_pde_2MB_entries_clear: invalid count\n");
        return OS_OUT_OF_RANGE;
    }
    constexpr uint32_t _2MB_SIZE = 0x200000;
    if (vaddr_base % _2MB_SIZE) return OS_INVALID_PARAMETER;

    uint16_t pml4_index  = (vaddr_base >> 39) & 0x1FF;
    uint16_t pdpte_index = (vaddr_base >> 30) & 0x1FF;
    uint16_t pde_index   = (vaddr_base >> 21) & 0x1FF;

    if (pde_index + count > 512) return OS_OUT_OF_RANGE;

    phyaddr_t pdpt_base = pml4eroottb[pml4_index].pml4.pdpte_addr << 12;
    uint64_t pdpte_raw = PhyAddrAccessor::readu64(pdpt_base + pdpte_index * sizeof(PageTableEntryUnion));
    
    // 检查PDPT条目是否存在且不是1GB大页
    if (!(pdpte_raw & PageTableEntry::P_MASK)) {
        return OS_RESOURCE_CONFILICT;
    } else {
        if (pdpte_raw & PDPTE::PS_MASK) return OS_RESOURCE_CONFILICT;
    }
    
    phyaddr_t pde_base = align_down(pdpte_raw, _4KB_SIZE) & PHYS_ADDR_MASK;
    
    // 逐项校验：条目必须为 2MB（PS=1）并且物理地址对齐匹配
    uint64_t expected_2mb = phybase / _2MB_SIZE;
    for (int i = 0; i < count; i++) {
        uint64_t pde_raw = PhyAddrAccessor::readu64(pde_base + (pde_index + i) * sizeof(PageTableEntryUnion));
        
        // 检查是否为2MB大页
        if (!(pde_raw & PDE::PS_MASK)) {
            // 不是 2MB 大页，校验不通过
            return OS_PGTB_FREE_VALIDATION_FAIL;
        }
        
        // 解析2MB页的物理地址
        uint64_t pde_phyaddr = align_down(pde_raw, _2MB_SIZE) & PHYS_ADDR_MASK;
        if (pde_phyaddr != (expected_2mb + i) * _2MB_SIZE) {
            return OS_PGTB_FREE_VALIDATION_FAIL;
        }
        
        // 清零并立即 invlpg（按 2MB 大页的虚地址）
        PhyAddrAccessor::writeu64(pde_base + (pde_index + i) * sizeof(PageTableEntryUnion), 0);
        if (will_invalidate_soon) {
            vaddr_t va = vaddr_base + (vaddr_t)i * _2MB_SIZE;
            asm volatile("invlpg %0" : : "m"(va));
        }
    }

    // 检查 PDE 表是否全空
    bool pde_all_empty = true;
    for (int i = 0; i < 512; i++) {
        if (PhyAddrAccessor::readu64(pde_base + i * sizeof(PageTableEntryUnion))) {
            pde_all_empty = false;
            break;
        }
    }
    if (!pde_all_empty) return OS_SUCCESS;

    // 释放 PDE 表
    phymemspace_mgr::pages_recycle(pde_base, 1);
    PhyAddrAccessor::writeu64(pdpt_base + pdpte_index * sizeof(PageTableEntryUnion), 0);

    // 检查 PDPT 是否全空
    bool pdpt_all_empty = true;
    for (int i = 0; i < 512; i++) {
        if (PhyAddrAccessor::readu64(pdpt_base + i * sizeof(PageTableEntryUnion))) {
            pdpt_all_empty = false;
            break;
        }
    }
    if (!pdpt_all_empty) return OS_SUCCESS;

    // 释放 PDPT
    phymemspace_mgr::pages_recycle(pdpt_base, 1);
    PhyAddrAccessor::writeu64((phyaddr_t)pml4eroottb + pml4_index * sizeof(PageTableEntryUnion), 0);
    
    // 检查 PML4 是否全空（理论上不太可能发生）
    bool pml4_all_empty = true;
    for (int i = 0; i < 512; i++) {
        if (PhyAddrAccessor::readu64((phyaddr_t)pml4eroottb + i * sizeof(PageTableEntryUnion))) {
            pml4_all_empty = false;
            break;
        }
    }
    
    // 如果整个PML4也为空（理论上不太可能发生），记录日志但不处理
    if (pml4_all_empty) {
        // 这里不需要实际回收根页表，只是记录信息
        // 因为根页表还在使用中
    }

    return OS_SUCCESS;
};


    // 清 1GB PDPTE 范围
   // ==== 1GB PDPT 清理（含物理地址校验 + invlpg） ====
auto _4lv_pdpte_1GB_entries_clear = [pml4eroottb, will_invalidate_soon](
    phyaddr_t phybase, vaddr_t vaddr_base, uint64_t count) -> int {
    if (count == 0 || count > 512) {
        kputsSecure("AddressSpace::disable_VM_desc::_4lv_pdpte_1GB_entries_clear: invalid count\n");
        return OS_OUT_OF_RANGE;
    }
    constexpr uint64_t _1GB_SIZE = 0x40000000ULL;
    if (vaddr_base % _1GB_SIZE) return OS_INVALID_PARAMETER;

    uint16_t pml4_index  = (vaddr_base >> 39) & 0x1FF;
    uint16_t pdpte_index = (vaddr_base >> 30) & 0x1FF;

    if (pdpte_index + count > 512) return OS_OUT_OF_RANGE;

    phyaddr_t pdpt_base = pml4eroottb[pml4_index].pml4.pdpte_addr << 12;

    // 逐项校验并清除
    uint64_t expected_1gb = phybase / _1GB_SIZE;
    for (uint16_t i = 0; i < count; i++) {
        uint64_t pdpte_raw = PhyAddrAccessor::readu64(pdpt_base + (pdpte_index + i) * sizeof(PageTableEntryUnion));
        
        // 必须是 1GB 大页（PS=1）且物理地址一致
        if (!(pdpte_raw & PDPTE::PS_MASK)) return OS_PGTB_FREE_VALIDATION_FAIL;
        
        // 解析1GB页的物理地址
        uint64_t pdpte_phyaddr = align_down(pdpte_raw, _1GB_SIZE) & PHYS_ADDR_MASK;
        if (pdpte_phyaddr != (expected_1gb + i) * _1GB_SIZE) return OS_PGTB_FREE_VALIDATION_FAIL;
        
        PhyAddrAccessor::writeu64(pdpt_base + (pdpte_index + i) * sizeof(PageTableEntryUnion), 0);
        if (will_invalidate_soon) {
            vaddr_t va = vaddr_base + (vaddr_t)i * _1GB_SIZE;
            asm volatile("invlpg %0" : : "m"(va));
        }
    }

    // 检查 PDPT 是否全空
    bool pdpt_all_empty = true;
    for (uint16_t i = 0; i < 512; i++) {
        if (PhyAddrAccessor::readu64(pdpt_base + i * sizeof(PageTableEntryUnion))) {
            pdpt_all_empty = false;
            break;
        }
    }
    if (!pdpt_all_empty) return OS_SUCCESS;

    // 释放 PDPT
    phymemspace_mgr::pages_recycle(pdpt_base, 1);
    PhyAddrAccessor::writeu64((phyaddr_t)pml4eroottb + pml4_index * sizeof(PageTableEntryUnion), 0);
    
    // 检查 PML4 是否全空（理论上不太可能发生）
    bool pml4_all_empty = true;
    for (int i = 0; i < 512; i++) {
        if (PhyAddrAccessor::readu64((phyaddr_t)pml4eroottb + i * sizeof(PageTableEntryUnion))) {
            pml4_all_empty = false;
            break;
        }
    }
    
    // 如果整个PML4也为空（理论上不太可能发生），记录日志但不处理
    if (pml4_all_empty) {
        // 这里不需要实际回收根页表，只是记录信息
        // 因为根页表还在使用中
    }

    return OS_SUCCESS;
};


    // seg_to_pages_info_get 与 enable 中保持相同逻辑（把段拆成最多 5 段）
    auto seg_to_pages_info_get = [desc](seg_to_pages_info_pakage_t& result) -> int {
        VM_DESC vmentry = desc;
        if (vmentry.start % _4KB_SIZE || vmentry.end % _4KB_SIZE || vmentry.start % _1GB_SIZE != vmentry.phys_start % _1GB_SIZE)
            return OS_INVALID_PARAMETER;

        // initialize
        for (int i = 0; i < 5; i++) {
            result.entryies[i].vbase = 0;
            result.entryies[i].base = 0;
            result.entryies[i].page_size_in_byte = 0;
            result.entryies[i].num_of_pages = 0;
        }

        int idx = 0;
        vaddr_t _1GB_base = align_up(vmentry.start, _1GB_SIZE);
        vaddr_t _1GB_end = align_down(vmentry.end, _1GB_SIZE);

        if (_1GB_end > _1GB_base) {
            result.entryies[idx].vbase = _1GB_base;
            result.entryies[idx].base = vmentry.phys_start + (_1GB_base - vmentry.start);
            result.entryies[idx].page_size_in_byte = _1GB_SIZE;
            result.entryies[idx].num_of_pages = (_1GB_end - _1GB_base) / _1GB_SIZE;
            idx++;
        }

        auto process_segment = [&](vaddr_t seg_s, vaddr_t seg_e) {
            if (seg_e <= seg_s) return;
            vaddr_t _2MB_base = align_up(seg_s, _2MB_SIZE);
            vaddr_t _2MB_end = align_down(seg_e, _2MB_SIZE);

            if (_2MB_base > seg_s) {
                if (idx >= 5) return;
                result.entryies[idx].vbase = seg_s;
                result.entryies[idx].base = vmentry.phys_start + (seg_s - vmentry.start);
                result.entryies[idx].page_size_in_byte = _4KB_SIZE;
                result.entryies[idx].num_of_pages = (_2MB_base - seg_s) / _4KB_SIZE;
                idx++;
            }

            if (_2MB_end > _2MB_base) {
                if (idx >= 5) return;
                result.entryies[idx].vbase = _2MB_base;
                result.entryies[idx].base = vmentry.phys_start + (_2MB_base - vmentry.start);
                result.entryies[idx].page_size_in_byte = _2MB_SIZE;
                result.entryies[idx].num_of_pages = (_2MB_end - _2MB_base) / _2MB_SIZE;
                idx++;
            }

            if (_2MB_end < seg_e) {
                if (idx >= 5) return;
                result.entryies[idx].vbase = _2MB_end;
                result.entryies[idx].base = vmentry.phys_start + (_2MB_end - vmentry.start);
                result.entryies[idx].page_size_in_byte = _4KB_SIZE;
                result.entryies[idx].num_of_pages = (seg_e - _2MB_end) / _4KB_SIZE;
                idx++;
            }
        };

        process_segment(vmentry.start, _1GB_base > vmentry.start ? _1GB_base : vmentry.start);
        process_segment(_1GB_end < vmentry.end ? _1GB_end : vmentry.end, vmentry.end);

        return OS_SUCCESS;
    };

    // 主流程
    seg_to_pages_info_pakage_t package = {0};
    int status = seg_to_pages_info_get(package);
    if (pglv_4_or_5 == PAGE_TBALE_LV::LV_4) {
        lock.write_lock();
        for (int i = 0; i < 5; i++) {
            seg_to_pages_info_pakage_t::pages_info_t entry = package.entryies[i];
            if (entry.page_size_in_byte == _4KB_SIZE) {
                // 注意：参数顺序与 set 函数一致（传 physbase 以便风格一致）
                status = _4lv_pte_4KB_entries_clear(entry.base, entry.vbase, static_cast<uint16_t>(entry.num_of_pages));
                if (status != OS_SUCCESS) goto sub_step_invalid;
            } else if (entry.page_size_in_byte == _2MB_SIZE) {
                status = _4lv_pde_2MB_entries_clear(entry.base, entry.vbase, static_cast<uint16_t>(entry.num_of_pages));
                if (status != OS_SUCCESS) goto sub_step_invalid;
            } else if (entry.page_size_in_byte == _1GB_SIZE) {
                uint64_t pdpte_equal_offset = entry.vbase / _1GB_SIZE;
                uint64_t count_to_clear_left = entry.num_of_pages;
                uint16_t pml4_idx = pdpte_equal_offset >> 9;
                uint16_t pdpte_idx = pdpte_equal_offset & 0x1FF;
                uint64_t processed_pages = 0;
                auto min = [](uint64_t a, uint64_t b)->uint64_t { return a < b ? a : b; };
                while (count_to_clear_left > 0) {
                    uint16_t this_count = static_cast<uint16_t>(min(count_to_clear_left, 512 - pdpte_idx));
                    // 使用 vbase/physbase 计算本段起点
                    phyaddr_t this_phybase = entry.base + processed_pages * _1GB_SIZE;
                    vaddr_t this_vbase = entry.vbase + processed_pages * _1GB_SIZE;
                    status = _4lv_pdpte_1GB_entries_clear(this_phybase, this_vbase, this_count);
                    if (status != OS_SUCCESS) goto sub_step_invalid;
                    count_to_clear_left -= this_count;
                    processed_pages += this_count;
                    pdpte_idx = 0;
                    pml4_idx++;
                }
            } else if (entry.page_size_in_byte != 0) {
                goto page_size_invalid;
            }
        }
        occupyied_size-=(desc.end-desc.start);
        //todo:广播所有核心重新
        goto success;
    } else {
        return OS_NOT_SUPPORT;
    }

success:
    lock.write_unlock();
    return OS_SUCCESS;
page_size_invalid:
    lock.write_unlock();
    return OS_ERROR_BASE;
sub_step_invalid:
    lock.write_unlock();
    return status;
}
phyaddr_t AddressSpace::vaddr_to_paddr(vaddr_t vaddr)
{
    uint16_t pml5_idx = (vaddr >> 48)&511;
    uint16_t pml4_idx = (vaddr >> 39)&511;
    uint16_t pdpte_idx = (vaddr >> 30)&511;
    uint16_t pde_idx = (vaddr >> 21)&511;
    uint16_t pte_idx = (vaddr >> 12)&511;
    lock.read_lock();
    if(pglv_4_or_5 == PAGE_TBALE_LV::LV_4){
        if(pml4_idx>255)goto ret0;// 高一半是内核空间,这里无权限访问
        PML4Entry pml4_entry=pml4[pml4_idx].pml4;
        if(!pml4_entry.present)goto ret0;
        phyaddr_t pdpte_base_phyaddr = pml4_entry.pdpte_addr << 12;
        uint64_t pdpte_raw = PhyAddrAccessor::readu64(pdpte_base_phyaddr + pdpte_idx * sizeof(PageTableEntryUnion));
        PageTableEntryUnion pdpte;
        pdpte.raw = pdpte_raw;
        if(!pdpte.pdpte.present)goto ret0;
        else if(pdpte.pdpte.large)goto pdpte_end;
        
        phyaddr_t pde_base_phyaddr = pdpte.pdpte.PD_addr << 12;
        uint64_t pde_raw = PhyAddrAccessor::readu64(pde_base_phyaddr + pde_idx * sizeof(PageTableEntryUnion));
        PageTableEntryUnion pde;
        pde.raw = pde_raw;
        if(!pde.pde.present)goto ret0;
        else if(pde.raw&PDE::PS_MASK)goto pde_end;
        
        phyaddr_t pte_base_phyaddr = pde.pde.pt_addr << 12;
        uint64_t pte_raw = PhyAddrAccessor::readu64(pte_base_phyaddr + pte_idx * sizeof(PageTableEntryUnion));
        PageTableEntryUnion pte;
        pte.raw = pte_raw;
        goto pte_end;
    }else{
        return 0;
    }


    ret0:
    lock.read_unlock();
    return 0;
    pdpte_end:
    lock.read_unlock();
    return (uint64_t(pml5_idx)<<48)+(uint64_t(pml4_idx)<<39)+(uint64_t(pdpte_idx)<<30)+(vaddr&(_1GB_SIZE-1));
    pde_end:
    lock.read_unlock();
    return (uint64_t(pml5_idx)<<48)+(uint64_t(pml4_idx)<<39)+(uint64_t(pde_idx)<<21)+(vaddr&(_2MB_SIZE-1));
    pte_end:
    lock.read_unlock();
    return (uint64_t(pml5_idx)<<48)+(uint64_t(pml4_idx)<<39)+(uint64_t(pde_idx)<<21)+(uint64_t(pte_idx)<<12)+(vaddr&(_4KB_SIZE-1));
}

void AddressSpace::load_pml4_to_cr3()
{
    asm volatile("mov %0, %%cr3"::"r"(pml4_phybase));
}

AddressSpace::~AddressSpace()
{
    delete[] pml4;
    pml4_phybase=0;
}
int AddressSpace::second_stage_init()
{
    
    pml4=(PageTableEntryUnion *)kpoolmemmgr_t::kalloc(sizeof(PageTableEntryUnion)*512,1,1,12);
    if(pml4==nullptr)return OS_OUT_OF_MEMORY;
    kpoolmemmgr_t::clear(pml4);
    pml4_phybase=kpoolmemmgr_t::get_phy((vaddr_t)pml4);
    phyaddr_t kspacUPpdpt_phybase=KspaceMapMgr::kspace_uppdpt_phyaddr;
    PageTableEntryUnion Up_pml4e_template=KspaceMapMgr::high_half_template;
    for(uint16_t i=0;i<256;i++)
    {
        uint64_t raw=Up_pml4e_template.raw;
        raw|=(kspacUPpdpt_phybase+i*4096)&PHYS_ADDR_MASK;
        pml4[i].raw=raw;
    }
    occupyied_size=0;
    return OS_SUCCESS;
}