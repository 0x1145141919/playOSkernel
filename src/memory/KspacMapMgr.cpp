#include "memory/AddresSpace.h"
#include "memory/Memory.h"
#include "memory/phygpsmemmgr.h"
#include "memory/kpoolmemmgr.h"
#include "os_error_definitions.h"
#include "linker_symbols.h"
#include "VideoDriver.h"
#include "util/OS_utils.h"
static inline uint64_t align_down(uint64_t x, uint64_t a){ return x & ~(a-1); }
// 定义KspaceMapMgr的静态成员变量
KspaceMapMgr::kspace_vm_table_t* KspaceMapMgr::kspace_vm_table = nullptr;
spinlock_cpp_t KspaceMapMgr::GMlock = spinlock_cpp_t();
phyaddr_t KspaceMapMgr::kspace_uppdpt_phyaddr = 0;


bool pglv_4_or_5=PAGE_TBALE_LV::LV_4;//true代表4级页表，false代表5级页表
cache_table_idx_struct_t cache_strategy_to_idx(cache_strategy_t cache_strategy)
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
int KspaceMapMgr::VM_search_by_vaddr(vaddr_t vaddr, VM_DESC &result){
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


int KspaceMapMgr::VM_add(VM_DESC vmentry){
    // 假定调用者持有 GMlock，且参数已校验（不做重复/越界检查）
    VM_DESC*vmentry_in_heap = new VM_DESC(vmentry);
    return kspace_vm_table->insert(vmentry_in_heap);
}


int KspaceMapMgr::VM_del(VM_DESC*entry){
    int status = kspace_vm_table->remove(entry);
    delete entry;
    return status;
}

int KspaceMapMgr::enable_VMentry(VM_DESC &vmentry)
{
    // basic alignment checks (4KB)
    if (vmentry.start % _4KB_SIZE || vmentry.end % _4KB_SIZE || vmentry.phys_start % _4KB_SIZE)
        return OS_INVALID_PARAMETER;

    if (vmentry.start >= vmentry.end) return OS_INVALID_PARAMETER;

    auto vmentry_congruence_vlidation = [vmentry]()->bool {
        enum PHY_SEG_MAX_PAGE:uint8_t{
        PHY_SEG_MAX_PAGE_4KB=0,
        PHY_SEG_MAX_PAGE_2MB,
        PHY_SEG_MAX_PAGE_1GB
    };
        auto max_type_identifier=[vmentry]()->PHY_SEG_MAX_PAGE{
        phyaddr_t phybase=vmentry.phys_start;
        phyaddr_t phyend=phybase+vmentry.end-vmentry.start;

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
        switch(max_type_identifier())
        {
            case PHY_SEG_MAX_PAGE_4KB:return true;
            case PHY_SEG_MAX_PAGE_2MB:return vmentry.start%_2MB_SIZE==vmentry.phys_start%_2MB_SIZE;
            case PHY_SEG_MAX_PAGE_1GB:return vmentry.start%_1GB_SIZE==vmentry.phys_start%_1GB_SIZE;
        }
    };
    if (!vmentry_congruence_vlidation())
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
        if ((e.vbase % psize) != 0 || (e.phybase % psize) != 0) return OS_INVALID_PARAMETER;

        switch(e.page_size_in_byte) {
            case _1GB_SIZE: {
                uint16_t count = static_cast<uint16_t>(e.num_of_pages);
                rc = _4lv_pdpte_1GB_entries_set(e.phybase, e.vbase, count, vmentry.access);
                break;
            }
            case _2MB_SIZE: {
                uint16_t count = static_cast<uint16_t>(e.num_of_pages);
                rc = _4lv_pde_2MB_entries_set(e.phybase, e.vbase, count, vmentry.access);
                break;
            }
            case _4KB_SIZE: {
                uint16_t count = static_cast<uint16_t>(e.num_of_pages);
                rc = _4lv_pte_4KB_entries_set(e.phybase, e.vbase, count, vmentry.access);
                break;
            }
            default:
                return OS_INVALID_PARAMETER; // unknown page size
        }
        if (rc != OS_SUCCESS) {
            return rc;
        }
    }

    // Optionally mark vmentry as enabled (if VM_DESC has such a field)
    // vmentry.enabled = true;  // uncomment if VM_DESC supports it

    return OS_SUCCESS;
}
void *KspaceMapMgr::pgs_remapp(
    phyaddr_t addr, 
    uint64_t size, 
    pgaccess access, 
    vaddr_t vbase,
    bool is_protective
)
{
    if(addr%_4KB_SIZE||size%_4KB_SIZE||vbase%_4KB_SIZE)return nullptr;
    VM_DESC vmentry={
        .start=0,
        .end=0,
        .map_type=VM_DESC::MAP_PHYSICAL,//内核的内存你敢随便分配吗，必须第一时间分配映射
        .phys_start=addr,
        .access=access,        
        .committed_full=0,
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
        vmentry.is_out_bound_protective=true;
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
            kputsSecure("KspaceMapMgr::pgs_remapp:VM_add entryies out of resource\n");
        }
        return nullptr;
    }
    VM_DESC vmentry_copy=vmentry;
    if(vmentry.is_vaddr_alloced&&vmentry.is_out_bound_protective){
        
        vmentry_copy.start+=_4KB_SIZE;
        vmentry_copy.end-=_4KB_SIZE;
    }
    status=enable_VMentry(vmentry_copy);    
    GMlock.unlock();
    if(status==OS_SUCCESS)return(void*)vmentry_copy.start; 
    return nullptr;
}

int KspaceMapMgr::seg_to_pages_info_get(seg_to_pages_info_pakage_t &result, VM_DESC vmentry)
{
    if (vmentry.start % _4KB_SIZE || vmentry.end % _4KB_SIZE)
        return OS_INVALID_PARAMETER;
    constexpr uint64_t _4KB_PG_SIZE= _4KB_SIZE;
    constexpr uint64_t _2MB_PG_SIZE = _2MB_SIZE;
    constexpr uint64_t _1GB_PG_SIZE = _1GB_SIZE;
    
    // initialize
    for (int i = 0; i < 5; i++) {
        result.entryies[i].vbase = 0;
        result.entryies[i].phybase = 0;
        result.entryies[i].page_size_in_byte = 0;
        result.entryies[i].num_of_pages = 0;
    }
    vaddr_t vbase=vmentry.start;
    vaddr_t vend=vmentry.end;
    uint64_t offset=vmentry.start-vmentry.phys_start;
    vaddr_t start_up_2mb = align_up(vbase, _2MB_PG_SIZE);
    vaddr_t end_down_2mb = align_down(vend, _2MB_PG_SIZE);
    
    vaddr_t start_up_1gb = align_up(vbase, _1GB_PG_SIZE);
    vaddr_t end_down_1gb = align_down(vend, _1GB_PG_SIZE);

    bool is_cross_2mb_boud = false;
    bool is_cross_1gb_boud = false;
    
    if (start_up_2mb <= end_down_2mb) {
        is_cross_2mb_boud = true;
        if (start_up_1gb <= end_down_1gb) {
            is_cross_1gb_boud = true;
        } else {
            is_cross_1gb_boud = false;
        }
    } else {
        is_cross_1gb_boud = false;
        is_cross_2mb_boud = false;
    }
    

    
    if(is_cross_2mb_boud){
        if(is_cross_1gb_boud){
            // 处理跨越1GB边界的段
            uint64_t countmid1gb=(end_down_1gb - start_up_1gb)/_1GB_PG_SIZE;
            result.entryies[0].vbase = start_up_1gb;
            result.entryies[0].page_size_in_byte = _1GB_PG_SIZE;
            result.entryies[0].num_of_pages = countmid1gb;
            result.entryies[0].phybase = start_up_1gb - offset;
            
            // 处理1GB区域之前的2MB区域
            uint64_t count_down_2mb=(start_up_1gb - start_up_2mb)/_2MB_PG_SIZE;
            result.entryies[1].vbase = start_up_2mb;
            result.entryies[1].page_size_in_byte = _2MB_PG_SIZE;
            result.entryies[1].num_of_pages = count_down_2mb;
            result.entryies[1].phybase = start_up_2mb - offset;
            
            // 处理1GB区域之后的2MB区域
            uint64_t count_up_2mb=(end_down_2mb - end_down_1gb)/_2MB_PG_SIZE;
            result.entryies[2].vbase = end_down_1gb;
            result.entryies[2].page_size_in_byte = _2MB_PG_SIZE;
            result.entryies[2].num_of_pages = count_up_2mb;
            result.entryies[2].phybase = end_down_2mb - offset;
        }else{
            // 处理仅跨越2MB边界的段
            uint64_t count_2mb=(end_down_2mb - start_up_2mb)/_2MB_PG_SIZE;
            result.entryies[2].vbase = start_up_2mb;
            result.entryies[2].page_size_in_byte = _2MB_PG_SIZE;
            result.entryies[2].num_of_pages = count_2mb;
            result.entryies[2].phybase = start_up_2mb - offset;
        }
        
        // 处理起始部分的小页面
        uint64_t countdown4kb=(start_up_2mb-vbase)/_4KB_PG_SIZE;
        result.entryies[3].vbase = vbase;
        result.entryies[3].page_size_in_byte = _4KB_PG_SIZE;
        result.entryies[3].num_of_pages = countdown4kb;
        result.entryies[3].phybase = vbase - offset;
        
        // 处理结束部分的小页面
        uint64_t countup4kb=(vend-end_down_2mb)/_4KB_PG_SIZE;
        result.entryies[4].vbase = end_down_2mb;
        result.entryies[4].page_size_in_byte = _4KB_PG_SIZE;
        result.entryies[4].num_of_pages = countup4kb;
        result.entryies[4].phybase = end_down_2mb - offset;
    }else{
        // 仅使用4KB页面
        uint64_t count4kbpgs=(vend-vbase)/_4KB_PG_SIZE;
        result.entryies[4].vbase = vbase;
        result.entryies[4].page_size_in_byte = _4KB_PG_SIZE;
        result.entryies[4].num_of_pages = count4kbpgs;
        result.entryies[4].phybase = vbase - offset;
    }
    
    return OS_SUCCESS;
}
int KspaceMapMgr::disable_VMentry(VM_DESC &vmentry)
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
int KspaceMapMgr::v_to_phyaddrtraslation(vaddr_t vaddr, phyaddr_t &result)
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
vaddr_t KspaceMapMgr::kspace_vm_table_t::alloc_available_space(uint64_t size,uint32_t target_vaddroffset)
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