#include "memory/AddresSpace.h"
#include "memory/Memory.h"
#include "memory/phygpsmemmgr.h"
#include "memory/kpoolmemmgr.h"
#include "os_error_definitions.h"
#include "memory/PagetbHeapMgr.h"
#include "linker_symbols.h"
#include "VideoDriver.h"
#include "util/OS_utils.h"
bool pglv_4_or_5=PAGE_TBALE_LV::LV_4;//true代表4级页表，false代表5级页表
cache_table_idx_struct_t KernelSpacePgsMemMgr::cache_strategy_to_idx(cache_strategy_t cache_strategy)
{   
    uint8_t i=0;
    for(;i<8;i++){
        if(cache_strategy==DEFAULT_PAT_CONFIG.mapped_entry[i])break;
    }
    cache_table_idx_struct_t result={
        .PWT=(uint8_t)(i&1),
        .PCD=(uint8_t)((i>>1)&1),
        .PAT=(uint8_t)((i>>2)&1)
    };
    return result;
}
int KernelSpacePgsMemMgr::VM_search_by_vaddr(vaddr_t vaddr, VM_DESC &result){
    // 假定调用者持有 GMlock，并且前 valid_vmentry_count 项是紧凑排列的已用条目
    VM_DESC tmp_desc={
        0
    };
    tmp_desc.start = vaddr;
    tmp_desc.end = vaddr + 1; // exclusive end
    Node* result_node = kspace_vm_table->search(&tmp_desc);
    if(result_node) {
        result = *(static_cast<VM_DESC*>(result_node->data));
        return OS_SUCCESS;
    }
    return OS_NOT_EXIST; // not found
}


int KernelSpacePgsMemMgr::VM_add(VM_DESC vmentry){
    // 假定调用者持有 GMlock，且参数已校验（不做重复/越界检查）
    VM_DESC*vmentry_in_heap = new VM_DESC(vmentry);
    return kspace_vm_table->insert(vmentry_in_heap);
}


int KernelSpacePgsMemMgr::VM_del(VM_DESC*entry){
    int status = kspace_vm_table->remove(entry);
    delete entry;
    return status;
}

int KernelSpacePgsMemMgr::enable_VMentry(VM_DESC &vmentry, bool is_pagetballoc_reserved)
{
    // basic alignment checks (4KB)
    if (vmentry.start % _4KB_SIZE || vmentry.end % _4KB_SIZE || vmentry.phys_start % _4KB_SIZE)
        return OS_INVALID_PARAMETER;

    if (vmentry.start >= vmentry.end) return OS_INVALID_PARAMETER;

    // require that vstart % 1GB == phys_start % 1GB (与 seg_to_pages_info_get 的前置条件一致)
    if ((vmentry.start % _1GB_SIZE) != (vmentry.phys_start % _1GB_SIZE))
        return OS_INVALID_PARAMETER;

    // Only implement 4-level paging path here
    if (!pglv_4_or_5) {
        // 5-level not implemented in this function
        return OS_NOT_SUPPORTED;
    }

    // 1) Split segment into page-sized runs
    seg_to_pages_info_pakage_t pkg;
    int rc = seg_to_pages_info_get(pkg, vmentry);
    if (rc != OS_SUCCESS) return rc;

    // Helper lambda: given page size (p), call the appropriate setter in chunks

    // 2) Iterate over pkg.entryies and dispatch
    for (int i = 0; i < 5; ++i) {
        auto &e = pkg.entryies[i];
        if (e.num_of_pages == 0) continue;

        uint64_t psize = e.page_size_in_byte;
        // sanity check: vbase and base should be aligned to page size
        if ((e.vbase % psize) != 0 || (e.base % psize) != 0) return OS_INVALID_PARAMETER;

        switch(e.page_size_in_byte) {
            case _1GB_SIZE: {
                uint16_t count = static_cast<uint16_t>(e.num_of_pages);
                rc = _4lv_pdpte_1GB_entries_set(e.base, e.vbase, count, vmentry.access);
                break;
            }
            case _2MB_SIZE: {
                uint16_t count = static_cast<uint16_t>(e.num_of_pages);
                rc = _4lv_pde_2MB_entries_set(e.base, e.vbase, count, vmentry.access, is_pagetballoc_reserved);
                break;
            }
            case _4KB_SIZE: {
                uint16_t count = static_cast<uint16_t>(e.num_of_pages);
                rc = _4lv_pte_4KB_entries_set(e.base, e.vbase, count, vmentry.access, is_pagetballoc_reserved);
                break;
            }
            default:
                return OS_INVALID_PARAMETER; // unknown page size
        }
        if (rc != OS_SUCCESS) return rc;
    }

    // Optionally mark vmentry as enabled (if VM_DESC has such a field)
    // vmentry.enabled = true;  // uncomment if VM_DESC supports it

    return OS_SUCCESS;
}
void *KernelSpacePgsMemMgr::pgs_remapp(
    phyaddr_t addr, 
    uint64_t size, 
    pgaccess access, 
    vaddr_t vbase,
    bool is_protective,
    bool is_pagetballoc_reserved
)
{
    if(addr%_4KB_SIZE||size%_4KB_SIZE||vbase%_4KB_SIZE)return nullptr;
    VM_DESC vmentry={
        .start=0,
        .end=0,
        .access=access,
        .phys_start=addr,
        .map_type=VM_DESC::MAP_PHYSICAL,//内核的内存你敢随便分配吗，必须第一时间分配映射
        .committed_full=0,
        .user_tag=0
    };
    GMlock.lock();
    int status;
    if(vbase==0){
        vmentry.is_vaddr_alloced=1;
        if(is_protective){
        vaddr_t new_base=kspace_vm_table->alloc_available_space(size+_4KB_SIZE*2, (addr-_4KB_SIZE)%_1GB_SIZE);
        GMlock.unlock();  
        vmentry.start=new_base;
        vmentry.end=new_base+size+_4KB_SIZE*2;  
    }else
        {
        vaddr_t new_base=kspace_vm_table->alloc_available_space(size, addr%_1GB_SIZE);
        GMlock.unlock();
        vmentry.start=new_base;
        vmentry.end=new_base+size;
    }
    }else{
        if(pglv_4_or_5){
            if(size+vbase-PAGELV4_KSPACE_BASE>PAGELV4_KSPACE_SIZE){
            GMlock.unlock();
            return nullptr;
            }
            vmentry.start=vbase;
            vmentry.end=vbase+size;
        }
    }
    status=VM_add(vmentry);
    if (status!=OS_SUCCESS)
    {
        GMlock.unlock();
        if(status==OS_OUT_OF_RESOURCE){
            kputsSecure("KernelSpacePgsMemMgr::pgs_remapp:VM_add entryies out of resource\n");
        }
        return nullptr;
    }
    VM_DESC vmentry_copy=vmentry;
    if(vmentry.is_vaddr_alloced&&vmentry.is_out_bound_protective){
        
        vmentry_copy.start+=_4KB_SIZE;
        vmentry_copy.end-=_4KB_SIZE;
    }
    status=enable_VMentry(vmentry_copy,is_pagetballoc_reserved);    
    GMlock.unlock();
    if(status==OS_SUCCESS)return(void*)vmentry_copy.start; 
    return nullptr;
}
static inline uint64_t align_down(uint64_t x, uint64_t a){ return x & ~(a-1); }
int KernelSpacePgsMemMgr::seg_to_pages_info_get(seg_to_pages_info_pakage_t &result, VM_DESC &vmentry)
{
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
}
int KernelSpacePgsMemMgr::disable_VMentry(VM_DESC &vmentry)
{
        // basic alignment checks (4KB)
    if (vmentry.start % _4KB_SIZE || vmentry.end % _4KB_SIZE || vmentry.phys_start % _4KB_SIZE)
        return OS_INVALID_PARAMETER;

    if (vmentry.start >= vmentry.end) return OS_INVALID_PARAMETER;

    // require that vstart % 1GB == phys_start % 1GB (与 seg_to_pages_info_get 的前置条件一致)
    if ((vmentry.start % _1GB_SIZE) != (vmentry.phys_start % _1GB_SIZE))
        return OS_INVALID_PARAMETER;
    int status=seg_to_pages_info_get(shared_inval_kspace_VMentry_info.info_package, vmentry);
    if (status != OS_SUCCESS) return status;
    for(uint8_t i = 0; i < 5; i++)
    {
        seg_to_pages_info_pakage_t::pages_info_t& entry = 
            shared_inval_kspace_VMentry_info.info_package.entryies[i];
            
        if (entry.page_size_in_byte == 0 || entry.num_of_pages == 0) 
            continue;
            
        switch (entry.page_size_in_byte) {
            case _4KB_SIZE:
                {
                    status = _4lv_pte_4KB_entries_clear(entry.vbase, static_cast<uint16_t>(entry.num_of_pages));
                    if(status != OS_SUCCESS) return status;
                    break;
                }
            case _2MB_SIZE:
            {
                status = _4lv_pde_2MB_entries_clear(entry.vbase, static_cast<uint16_t>(entry.num_of_pages));
                if(status != OS_SUCCESS) return status;
                break;
            }
            case _1GB_SIZE:
            {
                status = _4lv_pdpte_1GB_entries_clear(entry.vbase, static_cast<uint16_t>(entry.num_of_pages));
                if(status != OS_SUCCESS) return status;
                break;
            }
            default:
                return OS_INVALID_PARAMETER;
        
        }
    }
    return OS_SUCCESS;
}
int KernelSpacePgsMemMgr::v_to_phyaddrtraslation(vaddr_t vaddr, phyaddr_t &result)
{
    PageTableEntryUnion badentry={0};
    PageTableEntryUnion& result_entry=badentry;
    uint32_t page_size;
    int status = v_to_phyaddrtraslation_entry(vaddr, result_entry, page_size);
    switch (page_size)
    {
        case _4KB_SIZE:
            {
                result=result_entry.pte.page_addr<<12;
                return OS_SUCCESS;
            }
        case _2MB_SIZE:
            {
                result=result_entry.pde2MB._2mb_Addr<<21;
                return OS_SUCCESS;
            }
        case _1GB_SIZE:
            {
                result=result_entry.pdpte1GB._1GB_Addr<<30;
                return OS_SUCCESS;
            }
        default:
            return OS_INVALID_PARAMETER;
    }
}
/**
 * @brief 虚拟地址比较函数
 * 如果地址段有重复，则返回0,
 * a.start < b.start, 且 a.end <= b.start, 返回负值
 * a.start > b.start, 且 a.start >= b.end, 返回正值
 */
int VM_vaddr_cmp(VM_DESC *a, VM_DESC *b)
{
    if(a->start < b->start&&a->end <= b->start)return -1;
    if(a->start > b->start&&a->start >= b->end)return 1;
    return 0;
}
/**
 * 这个接口有大问题，至少需要传入一个参数表示原有物理地址余1GB的偏移，虚拟地址必须与这个偏移对应
 *选取起始虚拟地址也有讲究，不能一刀切必须与物理地址1gb同余，这样要造成虚拟地址浪费
 *根据穿来的物理地址便宜和大小判断应该采取分配虚拟地址策略，如果对应物理段合并了2mb大页，那么虚拟地址，物理地址要2mb同余
*如果对应物理段合并了1gb大页，那么虚拟地址，物理地址要1gb同余
    *没有合并则最简单
 */
/**
 * 所以需要把起始虚拟地址向上调整操作，鉴定物理段使用的大页操作这些拿出来做成函数
 */
vaddr_t KernelSpacePgsMemMgr::kspace_vm_table_t::alloc_available_space(uint64_t size,uint32_t target_vaddroffset)
{
    if(size==0||size%_4KB_SIZE||target_vaddroffset>=_1GB_SIZE||target_vaddroffset%_4KB_SIZE)return 0;
    enum PHY_SEG_MAX_PAGE:uint8_t{
        PHY_SEG_MAX_PAGE_4KB=0,
        PHY_SEG_MAX_PAGE_2MB,
        PHY_SEG_MAX_PAGE_1GB
    };
    auto max_type_identifier=[size,target_vaddroffset]()->PHY_SEG_MAX_PAGE{
        phyaddr_t phybase=target_vaddroffset;
        phyaddr_t phyend=phybase+size;

        uint64_t base_1GB=align_up(phybase,_1GB_SIZE);
        uint64_t end_1GB=align_down(phyend,_1GB_SIZE);
        if(base_1GB<end_1GB){
            return PHY_SEG_MAX_PAGE_1GB;
        }
        uint64_t base_2MB=align_up(phybase,_2MB_SIZE);
        uint64_t end_2MB=align_down(phyend,_2MB_SIZE);
        if(base_2MB<end_2MB){
            return PHY_SEG_MAX_PAGE_2MB;
        }
        return PHY_SEG_MAX_PAGE_4KB;
    };
    PHY_SEG_MAX_PAGE max_page_type=max_type_identifier();
    auto base_addr_modifier=[max_page_type,target_vaddroffset](vaddr_t base)->vaddr_t{ 
        switch (max_page_type)
        {
            case PHY_SEG_MAX_PAGE_4KB:
                return base;
            case PHY_SEG_MAX_PAGE_2MB:
                {
                    uint32_t lower_offset=base%_2MB_SIZE;
                    uint64_t basebase=base-lower_offset;
                    uint32_t upper_offset=target_vaddroffset%_2MB_SIZE;
                    if(upper_offset<lower_offset)return basebase+upper_offset+_2MB_SIZE;
                    else return basebase+upper_offset;
                }
            case PHY_SEG_MAX_PAGE_1GB:
                {
                    uint32_t lower_offset=base%_1GB_SIZE;
                    uint64_t basebase=base-lower_offset;
                    uint32_t upper_offset=target_vaddroffset%_1GB_SIZE;
                    if(upper_offset<lower_offset)return basebase+upper_offset+_1GB_SIZE;
                    else return basebase+upper_offset;
                }
            default:
                    return 0;
        }
    };
    Node* maxnode=this->subtree_max(this->root);
    VM_DESC*maxentry=(VM_DESC*)(maxnode->data);
    vaddr_t upper_alloc_base=base_addr_modifier(maxentry->end);
    uint64_t upper_top_avaliable=~upper_alloc_base+1;
    if(upper_top_avaliable>size)return upper_alloc_base;
    Node* minnode=this->subtree_min(this->root);
    Node*backnode=minnode;
    Node*front_node=successor(minnode);
    do{
        vaddr_t gap_start=base_addr_modifier(((VM_DESC*)(backnode->data))->end);
        vaddr_t gap_end=((VM_DESC*)(front_node->data))->start;
        if(gap_end>gap_start&&(gap_end-gap_start)>=size){
            return gap_start;
        }
        backnode=front_node;
        front_node=successor(front_node);
    }while(front_node!=maxnode);
    return 0;
}