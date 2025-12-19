#include "memory/AddresSpace.h"
#include "memory/Memory.h"
#include "memory/phygpsmemmgr.h"
#include "memory/kpoolmemmgr.h"
#include "os_error_definitions.h"
#include "memory/PagetbHeapMgr.h"
#include "linker_symbols.h"
#include "VideoDriver.h"
#include "util/OS_utils.h"
#include "panic.h"
PageTableEntryUnion  KernelSpacePgsMemMgr::kspace_root_pml4tb[512] __attribute__((section(".pml4_kernel_root_table")));
PageTableEntryUnion KernelSpacePgsMemMgr::kspaceUPpdpt[256*512] __attribute__((section(".kspace_uppdpt")));
shared_inval_VMentry_info_t shared_inval_kspace_VMentry_info={0};
bool KernelSpacePgsMemMgr::is_default_pat_config_enabled=false;


int KernelSpacePgsMemMgr::Init()
{
    roottbv=&kspace_root_pml4tb[0];
    root_pml4_phyaddr=(phyaddr_t)&_pml4_lma;
    kspace_uppdpt_phyaddr=(phyaddr_t)&_kspace_uppdpt_lma;
    kspace_vm_table=new kspace_vm_table_t();
    for(int i=0;i<256;i++)kspace_root_pml4tb[i].raw=0;
    cache_table_idx_struct_t WB_idx=cache_strategy_to_idx(WB);
    setmem(&kspace_root_pml4tb,sizeof(kspace_root_pml4tb),0);
    for(int i=0;i<256;i++){
        kspace_root_pml4tb[256+i].pml4.pdpte_addr=(kspace_uppdpt_phyaddr>>12)+1;
        kspace_root_pml4tb[256+i].pml4.present=ENTRY_PRESENT;
        kspace_root_pml4tb[256+i].pml4.KERNELbit=KERNE_BIT_KERNEL;
        kspace_root_pml4tb[256+i].pml4.RWbit=RWBIT_WRITE_ALLOW;
        kspace_root_pml4tb[256+i].pml4.EXECUTE_DENY=XD_BIT_EXECUTABLE;
        kspace_root_pml4tb[256+i].pml4.PWT=WB_idx.PWT;
        kspace_root_pml4tb[256+i].pml4.PCD=WB_idx.PCD;
    }
    
}
void nonleaf_pgtbentry_flagsset(PageTableEntryUnion&entry){//内核态的是全局的，
    entry.raw=0;
    entry.pte.present=1;
    entry.pte.RWbit=1;
    entry.pte.KERNELbit=0;
    entry.pte.global=1;
}
/**
 * 锁在外部接口中持有
 */
int KernelSpacePgsMemMgr::_4lv_pte_4KB_entries_set(
    phyaddr_t phybase,
    vaddr_t vaddr_base,
    uint16_t count,
    pgaccess access,
    bool is_pagetballoc_reserved
    )
{//暂时只有(is_phypgsmgr_enabled==false）的逻辑
    uint64_t highoffset=vaddr_base-PAGELV4_KSPACE_BASE;
    uint32_t pdpte_index=(highoffset>>30)&((1<<17)-1);
    uint16_t pde_index=(highoffset>>21)&((1<<9)-1);
    uint16_t pte_index=(highoffset>>12)&((1<<9)-1);
    PageTableEntryUnion* PDEvbase=nullptr;
    PageTableEntryUnion* PTEvbase=nullptr;
    if(pte_index+count>512){
        kputsSecure("KernelSpacePgsMemMgr::_4lv_pte_4KB_entries_set:cross page directory boundary not allowed\n");
        return OS_OUT_OF_RANGE;//跨页目录边界不允许
    }
    if(phybase%_4KB_SIZE||vaddr_base%_4KB_SIZE)return OS_INVALID_PARAMETER;
    if(kspaceUPpdpt[pdpte_index].raw&PageTableEntry::P_MASK){
        if(kspaceUPpdpt[pdpte_index].raw&PDPTE::PS_MASK)
        {
            kputsSecure("KernelSpacePgsMemMgr::_4lv_pte_4KB_entries_set:PDPTE attempt to dispatch existing 1GB page into 4kb PAGE\n");
            return OS_BAD_FUNCTION;//一个1GB大页面下面居然要4KB小页面
        }
        PDEvbase=(PageTableEntryUnion*)kpoolmemmgr_t::get_virt(kspaceUPpdpt[pdpte_index].pdpte.PD_addr<<12);
    }else{//presentBit==0说明要新建
        PDPTEEntry&_1=kspaceUPpdpt[pdpte_index].pdpte;
        phyaddr_t _1_subphy=0;
        if(is_phypgsmgr_enabled==false){
            PDEvbase=new PageTableEntryUnion[512];
            _1_subphy=kpoolmemmgr_t::get_phy((vaddr_t)PDEvbase);
        }
        if(!PDEvbase)return OS_OUT_OF_RESOURCE;
        nonleaf_pgtbentry_flagsset(kspaceUPpdpt[pdpte_index]);
        _1.PD_addr=_1_subphy>>12;
    }
    if(PDEvbase[pde_index].raw&PageTableEntry::P_MASK)
    {
        if(PDEvbase[pde_index].raw&PDE::PS_MASK)
        {
            kputsSecure("KernelSpacePgsMemMgr::_4lv_pte_4KB_entries_set:PDE attempt to dispatch existing 2MB page into 4kb PAGE\n");
            return OS_BAD_FUNCTION;//一个2MB大页面下面居然要4KB小页面
        }
        PTEvbase=(PageTableEntryUnion*)kpoolmemmgr_t::get_virt(PDEvbase[pde_index].pde.pt_addr<<12);
    }else{
        phyaddr_t _2_subphy=0;
        if(is_phypgsmgr_enabled==false){
            PTEvbase=new PageTableEntryUnion[512];
            _2_subphy=kpoolmemmgr_t::get_phy((vaddr_t)PTEvbase);
        }
        nonleaf_pgtbentry_flagsset(PDEvbase[pde_index]);
        PDEvbase[pde_index].pde.pt_addr=_2_subphy>>12;
    }
    cache_table_idx_struct_t cache_table_idx=cache_strategy_to_idx(access.cache_strategy);
    for (uint16_t i=0;i<count;i++)
    {
        PTEEntry&leaf=PTEvbase[pte_index+i].pte;
        leaf.present=1;
        leaf.page_addr=(phybase>>12)+i;
        leaf.PAT=cache_table_idx.PAT;
        leaf.PCD=cache_table_idx.PCD;
        leaf.PWT=cache_table_idx.PWT;
        leaf.RWbit=access.is_writeable;
        leaf.EXECUTE_DENY=!access.is_executable;
        leaf.global=access.is_global;
        leaf.KERNELbit=!access.is_kernel;
    }
    return OS_SUCCESS;
}
// ====================================================================
void KernelSpacePgsMemMgr::invalidate_seg()
{
    asm volatile(
        "cli"
    );
    constexpr uint32_t _4KB_SIZE = 0x1000;
    constexpr uint32_t _2MB_SIZE = 1ULL << 21;
    constexpr uint32_t _1GB_SIZE = 1ULL << 30;
    
    if (shared_inval_kspace_VMentry_info.is_package_valid == false) {
        kputsSecure("[KERNEL] invalid_kspace_VMentry_handler: stared_inval_kspace_VMentry_info is invalid\n");
        asm volatile(
        "sti"
        );
        KernelPanicManager::panic("invalid_k space_VMentry_handler: stared_inval_kspace_VMentry_info is invalid");
        return;
    }
    
    for (uint8_t i = 0; i < 5; i++) {
        seg_to_pages_info_pakage_t::pages_info_t& entry = 
            shared_inval_kspace_VMentry_info.info_package.entryies[i];
            
        if (entry.page_size_in_byte == 0 || entry.num_of_pages == 0) 
            continue;
            
        switch (entry.page_size_in_byte) {
            case _4KB_SIZE:
                for (uint32_t j = 0; j < entry.num_of_pages; j++) {
                    asm volatile(
                        "invlpg (%0)"
                        :
                        : "r" (entry.vbase + j * _4KB_SIZE)
                        : "memory"
                    );
                }
                break;
                
            case _2MB_SIZE: 
                for (uint32_t j = 0; j < entry.num_of_pages; j++) {
                    asm volatile(
                        "invlpg (%0)"
                        :
                        : "r" (entry.vbase + j * _2MB_SIZE)
                        : "memory"
                    );
                }
                break;
                
            case _1GB_SIZE:
                for (uint32_t j = 0; j < entry.num_of_pages; j++) {
                    asm volatile(
                        "invlpg (%0)"
                        :
                        : "r" (entry.vbase + j * _1GB_SIZE)
                        : "memory"
                    );
                }
                break;
                
            default:
                kputsSecure("[KERNEL] invalid_kspace_VMentry_handler: invalid page size in kspace_VMentry_info\n");
                asm volatile(
        "sti"
        );
                KernelPanicManager::panic("invalid_kspace_VMentry_handler: invalid page size in kspace_VMentry_info");
                return;  // 添加 return 避免继续执行
        }
    }
    
    // 正确的原子递增
    uint32_t& completed_count = shared_inval_kspace_VMentry_info.completed_processors_count;
    asm volatile(
        "lock incl %0"
        : "+m" (completed_count)
        :
        : "cc", "memory"
    );
    asm volatile(
        "sti"
        );
}
// 2MB 大页映射（PDE 级别）
// 要求：不能跨 PDPT 边界（即一次最多映射 512 个 2MB 页，覆盖 1GB）
// ====================================================================
int KernelSpacePgsMemMgr::_4lv_pde_2MB_entries_set(
                                                phyaddr_t phybase,
                                                  vaddr_t vaddr_base,
                                                  uint16_t count,
                                                  pgaccess access,
                                                  bool is_pagetballoc_reserved)
{
    if (count == 0 || count > 512) [[unlikely]] {
        kputsSecure("KernelSpacePgsMemMgr::_4lv_pde_2MB_entries_set: count invalid\n");
        return OS_INVALID_PARAMETER;
    }
    if ((phybase & (_2MB_SIZE - 1)) || (vaddr_base & (_2MB_SIZE - 1))) [[unlikely]] {
        return OS_INVALID_PARAMETER; // 2MB 对齐
    }

    uint64_t highoffset = vaddr_base - PAGELV4_KSPACE_BASE;
    uint32_t pdpt_index = (highoffset >> 30) & ((1ULL << 17) - 1);  // 合并 PDPT 表索引
    uint16_t pde_index  = (highoffset >> 21) & 0x1FF;               // 在 PD 表内的起始位置

    if (pde_index + count > 512) [[unlikely]] {
        kputsSecure("KernelSpacePgsMemMgr::_4lv_pde_2MB_entries_set: cross PDPT boundary not allowed\n");
        return OS_OUT_OF_RANGE;
    }

    // 取出或创建对应的 PD 页表
    PageTableEntryUnion* PD_vbase = nullptr;
    if (kspaceUPpdpt[pdpt_index].raw & PageTableEntry::P_MASK) {
        if (kspaceUPpdpt[pdpt_index].raw & PDPTE::PS_MASK) [[unlikely]] {
            kputsSecure("KernelSpacePgsMemMgr::_4lv_pde_2MB_entries_set: under a 1GB huge page\n");
            return OS_BAD_FUNCTION;
        }
        if(is_phypgsmgr_enabled==false)
        {
            PD_vbase = (PageTableEntryUnion*)kpoolmemmgr_t::get_virt(kspaceUPpdpt[pdpt_index].pdpte.PD_addr<<12);
        }else{//todo

        }
    } else {//暂时只考虑了(is_phypgsmgr_enabled==false)的情况，注意要改写
        phyaddr_t pd_phy = 0;
        PD_vbase=new(true,12) PageTableEntryUnion[512];
        if(PD_vbase==nullptr)return OS_OUT_OF_MEMORY;
        pd_phy=kpoolmemmgr_t::get_phy((vaddr_t)PD_vbase);
        nonleaf_pgtbentry_flagsset(kspaceUPpdpt[pdpt_index]);
        kspaceUPpdpt[pdpt_index].pdpte.PD_addr= pd_phy >> 12;
    }

    cache_table_idx_struct_t idx = cache_strategy_to_idx(access.cache_strategy);

    for (uint16_t i = 0; i < count; i++) {
        PageTableEntryUnion& entry = PD_vbase[pde_index + i];
        if (entry.raw & PageTableEntry::P_MASK) [[unlikely]] {
            kputsSecure("KernelSpacePgsMemMgr::_4lv_pde_2MB_entries_set: 2MB entry already mapped\n");
            return OS_TARGET_BUSY;
        }

        PDEntry2MB& huge = entry.pde2MB;
        huge.present      = 1;
        huge.RWbit        = access.is_writeable;
        huge.KERNELbit    = !access.is_kernel;
        huge.large        = 1;        // 必须为 1
        huge.global       = access.is_global;
        huge.PAT          = idx.PAT;
        huge.PWT          = idx.PWT;
        huge.PCD          = idx.PCD;
        huge.EXECUTE_DENY = !access.is_executable;
        huge._2mb_Addr    = ((phybase >> 21) + i) & ((1ULL << 27) - 1);
    }

    return OS_SUCCESS;
}
// ====================================================================
// 1GB 大页映射（PDPTE 级别）
// 要求：不能跨 PML4E 边界（即一次最多映射 512 个 1GB 页，覆盖 512GB）
// ====================================================================
int KernelSpacePgsMemMgr::_4lv_pdpte_1GB_entries_set(phyaddr_t phybase,
                                                    vaddr_t vaddr_base,
                                                    uint16_t count,
                                                    pgaccess access)
{
    if (count == 0){
        kputsSecure("KernelSpacePgsMemMgr::_4lv_pdpte_1GB_entries_set: count invalid\n");
        return OS_INVALID_PARAMETER;
    }
    if ((phybase & (_1GB_SIZE - 1)) || (vaddr_base & (_1GB_SIZE - 1))) [[unlikely]] {
        return OS_INVALID_PARAMETER; // 1GB 对齐
    }

    uint64_t highoffset = vaddr_base - PAGELV4_KSPACE_BASE;
    uint32_t pdpt_index = (highoffset >> 30) & ((1ULL << 17) - 1);  // 256×512 合并表索引
    uint16_t start_idx  = (highoffset >> 30) & 0x1FF;               // 在该 PDPTE 表内的起始偏移
    cache_table_idx_struct_t idx = cache_strategy_to_idx(access.cache_strategy);

    for (uint16_t i = 0; i < count; i++) {
        PageTableEntryUnion& entry = kspaceUPpdpt[pdpt_index + i];
        if (entry.raw & PageTableEntry::P_MASK) [[unlikely]] {
            kputsSecure("KernelSpacePgsMemMgr::_4lv_pdpte_1GB_entries_set: 1GB entry already present\n");
            return OS_TARGET_BUSY;
        }
        PDPTEEntry1GB& huge = entry.pdpte1GB;
        huge.present       = 1;
        huge.RWbit         = access.is_writeable;
        huge.KERNELbit     = !access.is_kernel;
        huge.large         = 1;        // 必须为 1
        huge.global        = access.is_global;
        huge.PAT           = idx.PAT;
        huge.PWT           = idx.PWT;
        huge.PCD           = idx.PCD;
        huge.EXECUTE_DENY  = !access.is_executable;
        huge._1GB_Addr     = ((phybase >> 30) + i) & ((1ULL << 18) - 1); // 低 18 位是地址高部
    }
    return OS_SUCCESS;
}

int KernelSpacePgsMemMgr::pgs_remapped_free(vaddr_t addr)
{
    int status = OS_SUCCESS;
    VM_DESC nullentry = {0};
    VM_DESC&vmentry=nullentry;
    GMlock.lock();
    status=VM_search_by_vaddr(addr, vmentry);
    GMlock.unlock();
    if (status != OS_SUCCESS)
    {
        return status;
    }
    GMlock.lock();
    status=disable_VMentry(vmentry);
    GMlock.unlock();
    if (status != OS_SUCCESS)
    {
        return status;
    }
    shared_inval_kspace_VMentry_info.is_package_valid = true;
    shared_inval_kspace_VMentry_info.completed_processors_count = 0;
    
    status=invalidate_tlb_entry();
    status=VM_del(&vmentry);
    if (status != OS_SUCCESS)return status;
    //todo:广播其他处理器失效tlb以及等待校验，若超时要用其它手段跳出直接panic
    
}
int KernelSpacePgsMemMgr::invalidate_tlb_entry()
{
    if (shared_inval_kspace_VMentry_info.is_package_valid == false) {
        return OS_INVALID_PARAMETER;
    }
    
    for (uint8_t i = 0; i < 5; i++) {
        seg_to_pages_info_pakage_t::pages_info_t& entry = 
            shared_inval_kspace_VMentry_info.info_package.entryies[i];
            
        if (entry.page_size_in_byte == 0 || entry.num_of_pages == 0) 
            continue;
            
        switch (entry.page_size_in_byte) {
            case _4KB_SIZE:
                for (uint32_t j = 0; j < entry.num_of_pages; j++) {
                    asm volatile(
                        "invlpg (%0)"
                        :
                        : "r" (entry.vbase + j * _4KB_SIZE)
                        : "memory"
                    );
                }
                break;
                
            case _2MB_SIZE: 
                for (uint32_t j = 0; j < entry.num_of_pages; j++) {
                    asm volatile(
                        "invlpg (%0)"
                        :
                        : "r" (entry.vbase + j * _2MB_SIZE)
                        : "memory"
                    );
                }
                break;
                
            case _1GB_SIZE:
                for (uint32_t j = 0; j < entry.num_of_pages; j++) {
                    asm volatile(
                        "invlpg (%0)"
                        :
                        : "r" (entry.vbase + j * _1GB_SIZE)
                        : "memory"
                    );
                }
                break;
                
            default:
                return OS_INVALID_PARAMETER;
        }
    }
    
    // 正确的原子递增
    uint32_t& completed_count = shared_inval_kspace_VMentry_info.completed_processors_count;
    asm volatile(
        "lock incl %0"
        : "+m" (completed_count)
        :
        : "cc", "memory"
    );
    return OS_SUCCESS;
}

int KernelSpacePgsMemMgr::_4lv_pte_4KB_entries_clear(vaddr_t vaddr_base, uint16_t count)
{
    if (count == 0) return OS_SUCCESS;  // 或 OS_INVALID_PARAMETER，根据需求

    uint64_t highoffset = vaddr_base - PAGELV4_KSPACE_BASE;
    uint32_t pdpte_index = (highoffset >> 30) & ((1 << 17) - 1);
    uint16_t pde_index = (highoffset >> 21) & ((1 << 9) - 1);
    uint16_t pte_index = (highoffset >> 12) & ((1 << 9) - 1);
    PageTableEntryUnion* PDEvbase = nullptr;
    PageTableEntryUnion* PTEvbase = nullptr;

    if (pte_index + count > 512) {
        kputsSecure("KernelSpacePgsMemMgr::_4lv_pte_4KB_entries_clear: cross page directory boundary not allowed\n");
        return OS_OUT_OF_RANGE;
    }
    if (vaddr_base % _4KB_SIZE) return OS_INVALID_PARAMETER;

    if (kspaceUPpdpt[pdpte_index].raw & PageTableEntry::P_MASK) {
        if (kspaceUPpdpt[pdpte_index].raw & PDPTE::PS_MASK) {
            kputsSecure("KernelSpacePgsMemMgr::_4lv_pte_4KB_entries_clear: PDPTE is existing 1GB page\n");
            return OS_BAD_FUNCTION;
        }
        PDEvbase = (PageTableEntryUnion*)gPgtbHeapMgr.phyaddr_to_vaddr(kspaceUPpdpt[pdpte_index].pdpte.PD_addr << 12);
        if (!PDEvbase) return OS_INVALID_ADDRESS;  // 加 NULL 检查
    } else {
        return OS_INVALID_ADDRESS;
    }

    if (PDEvbase[pde_index].raw & PageTableEntry::P_MASK) {
        if (PDEvbase[pde_index].raw & PDE::PS_MASK) {
            kputsSecure("KernelSpacePgsMemMgr::_4lv_pte_4KB_entries_clear: PDE is existing 2MB page\n");
            return OS_BAD_FUNCTION;
        }
        PTEvbase = (PageTableEntryUnion*)gPgtbHeapMgr.phyaddr_to_vaddr(PDEvbase[pde_index].pde.pt_addr << 12);
        if (!PTEvbase) return OS_INVALID_ADDRESS;
    } else {
        return OS_INVALID_ADDRESS;
    }

    // 清除指定范围 PTE
    for (uint16_t i = 0; i < count; i++) {
        PTEvbase[pte_index + i].raw = 0;
    }

    // 检查整个 PT 是否全空（不限 may_full_del）
    bool is_full_del = true;
    for (uint16_t i = 0; i < 512; i++) {
        if (PTEvbase[i].raw & PageTableEntry::P_MASK) {
            is_full_del = false;
            break;
        }
    }

    if (is_full_del) {
        phyaddr_t pt_phy = (phyaddr_t)(PDEvbase[pde_index].pde.pt_addr << 12);
        gPgtbHeapMgr.free_pgtb_by_phyaddr(pt_phy);
        PDEvbase[pde_index].raw = 0;
        // 可选：递归检查 PD 是否空，回收 PD
    }

    return OS_SUCCESS;
}
// ====================================================================
// 1GB 大页清除（PDPTE 级别）
// 功能：直接清除 kspaceUPpdpt 中的 PDPTE 条目（无需下级页表）
// ====================================================================
int KernelSpacePgsMemMgr::_4lv_pdpte_1GB_entries_clear(vaddr_t vaddr_base, uint16_t count)
{
    if (count == 0) return OS_SUCCESS;
    if (vaddr_base & (_1GB_SIZE - 1)){
        return OS_INVALID_PARAMETER;
    }

    uint64_t highoffset = vaddr_base - PAGELV4_KSPACE_BASE;
    uint32_t pdpt_index = (highoffset >> 30) & ((1ULL << 17) - 1);  // 合并索引
    uint16_t start_idx  = (highoffset >> 30) & 0x1FF;               // 在 512 项中的起始位置

    if (start_idx + count > 512){
        kputsSecure("KernelSpacePgsMemMgr::_4lv_pdpte_1GB_entries_clear: cross PML4E boundary not allowed\n");
        return OS_OUT_OF_RANGE;
    }

    for (uint16_t i = 0; i < count; i++) {
        PageTableEntryUnion& entry = kspaceUPpdpt[pdpt_index + i];
        if (!(entry.raw & PageTableEntry::P_MASK)) {
            continue;  // 已空
        }
        if (!(entry.raw & PDPTE::PS_MASK)) {
            kputsSecure("KernelSpacePgsMemMgr::_4lv_pdpte_1GB_entries_clear: trying to clear non-1GB PDPTE\n");
            return OS_BAD_FUNCTION;
        }
        entry.raw = 0;
    }

    // 注意：1GB 页清除后不需要回收下级页表（因为 PS=1 时没有下级）
    // 也不回收整个 512GB 块（内核空间一般保留结构）

    return OS_SUCCESS;
}
// ====================================================================
// 2MB 大页清除（PDE 级别）
// 要求：不能跨 PDPT 边界（即一次最多清除 512 个 2MB 页，覆盖 1GB）
// 功能：清除 PDE 条目 + 检测整个 PD 是否全空 → 回收 PD 页表 + 清上级 PDPTE
// ====================================================================
int KernelSpacePgsMemMgr::_4lv_pde_2MB_entries_clear(vaddr_t vaddr_base, uint16_t count)
{
    if (count == 0) return OS_SUCCESS;
    if (count > 512) [[unlikely]] {
        kputsSecure("KernelSpacePgsMemMgr::_4lv_pde_2MB_entries_clear: count exceeds 512\n");
        return OS_INVALID_PARAMETER;
    }
    if (vaddr_base & (_2MB_SIZE - 1)) [[unlikely]] {
        return OS_INVALID_PARAMETER;
    }

    uint64_t highoffset = vaddr_base - PAGELV4_KSPACE_BASE;
    uint32_t pdpt_index = (highoffset >> 30) & ((1ULL << 17) - 1);  // 合并 PDPT 索引
    uint16_t pde_index  = (highoffset >> 21) & 0x1FF;               // 在 PD 内的起始索引

    if (pde_index + count > 512) [[unlikely]] {
        kputsSecure("KernelSpacePgsMemMgr::_4lv_pde_2MB_entries_clear: cross PDPT boundary not allowed\n");
        return OS_OUT_OF_RANGE;
    }

    // 获取或验证 PD 页表
    PageTableEntryUnion* PD_vbase = nullptr;
    if (!(kspaceUPpdpt[pdpt_index].raw & PageTableEntry::P_MASK)) {
        return OS_INVALID_ADDRESS;  // 上级 PDPTE 不存在
    }
    if (kspaceUPpdpt[pdpt_index].raw & PDPTE::PS_MASK) {
        kputsSecure("KernelSpacePgsMemMgr::_4lv_pde_2MB_entries_clear: under a 1GB huge page\n");
        return OS_BAD_FUNCTION;
    }

    PD_vbase = (PageTableEntryUnion*)gPgtbHeapMgr.phyaddr_to_vaddr(
        kspaceUPpdpt[pdpt_index].pdpte.PD_addr << 12);
    if (!PD_vbase) return OS_INVALID_ADDRESS;

    // 清除指定范围的 2MB PDE 条目
    for (uint16_t i = 0; i < count; i++) {
        PageTableEntryUnion& entry = PD_vbase[pde_index + i];
        if (!(entry.raw & PageTableEntry::P_MASK)) {
            // 已经为空，继续（允许重复清除）
            continue;
        }
        if (!(entry.raw & PDE::PS_MASK)) {
            kputsSecure("KernelSpacePgsMemMgr::_4lv_pde_2MB_entries_clear: trying to clear non-huge PDE\n");
            return OS_BAD_FUNCTION;  // 不应该出现在 2MB clear 中
        }
        entry.raw = 0;
    }

    // 检查整个 PD 是否完全为空 → 回收 PD 页表页
    bool pd_empty = true;
    for (uint16_t i = 0; i < 512; i++) {
        if (PD_vbase[i].raw & PageTableEntry::P_MASK) {
            pd_empty = false;
            break;
        }
    }

    if (pd_empty) {
        phyaddr_t pd_phy = kspaceUPpdpt[pdpt_index].pdpte.PD_addr<< 12;
        gPgtbHeapMgr.free_pgtb_by_phyaddr(pd_phy);
        kspaceUPpdpt[pdpt_index].raw = 0;  // 清上级 PDPTE
        // 注意：这里不需要递归检查 PDPTE 是否全空，因为内核空间一般不回收整个 512GB 块
    }

    return OS_SUCCESS;
}
int KernelSpacePgsMemMgr::v_to_phyaddrtraslation_entry
(vaddr_t vaddr, 
    PageTableEntryUnion &result, 
    uint32_t &page_size)
{
    if(pglv_4_or_5){
        uint64_t highoffset = vaddr - PAGELV4_KSPACE_BASE;
        uint32_t pdpte_index = (highoffset >> 30) & ((1 << 17) - 1);
    uint16_t pde_index = (highoffset >> 21) & ((1 << 9) - 1);
    uint16_t pte_index = (highoffset >> 12) & ((1 << 9) - 1);
    PageTableEntryUnion* PDEvbase = nullptr;
    PageTableEntryUnion* PTEvbase = nullptr;
    if (kspaceUPpdpt[pdpte_index].raw & PageTableEntry::P_MASK) {
        if (kspaceUPpdpt[pdpte_index].raw & PDPTE::PS_MASK) {
            result=kspaceUPpdpt[pdpte_index];
            return OS_SUCCESS;
        }
        PDEvbase = (PageTableEntryUnion*)gPgtbHeapMgr.phyaddr_to_vaddr(kspaceUPpdpt[pdpte_index].pdpte.PD_addr << 12);
        page_size=_1GB_SIZE;
        if (!PDEvbase) return OS_INVALID_ADDRESS;  // 加 NULL 检查
    } else {
        return OS_INVALID_ADDRESS;
    }

    if (PDEvbase[pde_index].raw & PageTableEntry::P_MASK) {
        if (PDEvbase[pde_index].raw & PDE::PS_MASK) {
            result=PDEvbase[pde_index];
            return OS_SUCCESS;
        }
        PTEvbase = (PageTableEntryUnion*)gPgtbHeapMgr.phyaddr_to_vaddr(PDEvbase[pde_index].pde.pt_addr << 12);
        page_size=_2MB_SIZE;
        if (!PTEvbase) return OS_INVALID_ADDRESS;
    } else {
        return OS_INVALID_ADDRESS;
    }
    page_size=_4KB_SIZE;
    result=PTEvbase[pte_index];
    return OS_SUCCESS;
    }else{
        return OS_NOT_SUPPORTED;
    }
    return OS_UNREACHABLE_CODE;
}
