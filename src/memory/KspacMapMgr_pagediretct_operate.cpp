#include "memory/AddresSpace.h"
#include "memory/Memory.h"
#include "memory/phygpsmemmgr.h"
#include "memory/kpoolmemmgr.h"
#include "memory/phyaddr_accessor.h"
#include "os_error_definitions.h"
#include "linker_symbols.h"
#include "VideoDriver.h"
#include "util/OS_utils.h"
#include "panic.h"
#include "msr_offsets_definitions.h"
PageTableEntryUnion KspaceMapMgr::kspaceUPpdpt[256*512] __attribute__((section(".kspace_uppdpt")));
shared_inval_VMentry_info_t shared_inval_kspace_VMentry_info={0};
bool KspaceMapMgr::is_default_pat_config_enabled=false;

int KspaceMapMgr::Init()
{
    kspace_uppdpt_phyaddr=(phyaddr_t)&_kspace_uppdpt_lma;
    kspace_vm_table=new kspace_vm_table_t();
    int status=0;
    //先注册内核映像
    pgaccess PG_RX{
        .is_kernel=1,
        .is_writeable=0,
        .is_readable =1,
        .is_executable=1,
        .is_global=1,
        .cache_strategy = cache_strategy_t::WB
    };
    vaddr_t result=0;
    uint64_t basic_seg_size=PhyAddrAccessor::BASIC_DESC.SEG_SIZE_ONLY_UES_IN_BASIC_SEG;
    result=(vaddr_t)pgs_remapp((uint64_t)&KImgphybase,(&text_end-&text_begin),PG_RX,(vaddr_t)&KImgvbase);
    if(result==0)goto regist_segment_fault;
    result=(vaddr_t)pgs_remapp((uint64_t)&_data_lma,(&_data_end-&_data_start),PG_RW,(vaddr_t)&_data_start);
    if(result==0)goto regist_segment_fault;
    result=(vaddr_t)pgs_remapp((uint64_t)&_rodata_lma,(&_rodata_end-&_rodata_start),PG_R,(vaddr_t)&_rodata_start);
    if(result==0)goto regist_segment_fault;
    result=(vaddr_t)pgs_remapp((uint64_t)&_stack_lma,(&__kspace_uppdpt_end-&_stack_bottom),PG_RW,(vaddr_t)&_stack_bottom);
    if(result==0)goto regist_segment_fault;
    result=(vaddr_t)pgs_remapp(0,basic_seg_size,PG_RW,0,true);
    if(result==0)goto regist_segment_fault;
    return OS_SUCCESS;  
    regist_segment_fault:
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
int KspaceMapMgr::_4lv_pte_4KB_entries_set(
    phyaddr_t phybase,
    vaddr_t vaddr_base,
    uint16_t count,
    pgaccess access
    )
{//暂时只有(is_phypgsmgr_enabled==false）的逻辑
    // 检查count参数有效性
    if (count == 0) {
        return 0; // 成功设置0个条目
    }

    uint64_t highoffset = vaddr_base - PAGELV4_KSPACE_BASE;
    
    // 使用常量替代魔法数字，提高可读性
    const uint32_t PDPT_INDEX_BITS = 17;
    const uint32_t PD_INDEX_BITS = 9;
    const uint32_t PT_INDEX_BITS = 9;

    uint32_t pdpte_index = (highoffset >> 30) & ((1 << PDPT_INDEX_BITS) - 1);
    uint16_t pde_index = (highoffset >> 21) & ((1 << PD_INDEX_BITS) - 1);
    uint16_t pte_index = (highoffset >> 12) & ((1 << PT_INDEX_BITS) - 1);

    PageTableEntryUnion& pdpte = kspaceUPpdpt[pdpte_index];
    if(pdpte.raw & PDPTE::PS_MASK)return OS_RESOURCE_CONFILICT;
    // 检查页目录指针表项是否存在且不是1GB页面
    if (!(pdpte.raw & PageTableEntry::P_MASK) ){
        nonleaf_pgtbentry_flagsset(pdpte);
        phyaddr_t pd_phyaddr = phymemspace_mgr::pages_alloc(1,phymemspace_mgr::KERNEL,12);
        if (pd_phyaddr == 0) return OS_OUT_OF_MEMORY;
        pdpte.pdpte.PD_addr = pd_phyaddr >> 12;
        // 初始化新分配的页目录中的所有页表项为0
        for(uint16_t i=0;i<512;i++)
        {
            PhyAddrAccessor::writeu64((pd_phyaddr & PHYS_ADDR_MASK) + sizeof(PageTableEntryUnion) * i, 0);
        }
    }
        

    // 修复位运算优先级问题：使用括号确保正确的运算顺序
    uint64_t pd_addr = pdpte.pdpte.PD_addr;
    uint64_t pde_offset = sizeof(PageTableEntryUnion) * pde_index;
    uint64_t pde_address = (pd_addr << 12) + pde_offset;
    uint64_t pderaw = PhyAddrAccessor::readu64(pde_address);
    PageTableEntryUnion pde = {
        .raw = pderaw
    };
    if(pderaw & PDE::PS_MASK)return OS_RESOURCE_CONFILICT;
    // 检查页目录项是否存在且不是2MB页面
    if (!(pderaw & PageTableEntry::P_MASK))
        {
            nonleaf_pgtbentry_flagsset(pde);
            phyaddr_t pt_phyaddr = phymemspace_mgr::pages_alloc(1,phymemspace_mgr::KERNEL,12);
            if (pt_phyaddr == 0) return OS_OUT_OF_MEMORY;
            pde.pde.pt_addr = pt_phyaddr >> 12;
            PhyAddrAccessor::writeu64(pde_address, pde.raw);
            // 初始化新分配的页表中的所有页表项为0
            for(uint16_t i=0;i<512;i++)
            {
                PhyAddrAccessor::writeu64((pt_phyaddr & PHYS_ADDR_MASK) + sizeof(PageTableEntryUnion) * i, 0);
            }
        }

    phyaddr_t pte_phybase = (pd_addr << 12) & PHYS_ADDR_MASK;
    cache_table_idx_struct_t idx = cache_strategy_to_idx(access.cache_strategy);

    PageTableEntryUnion template_entry = {
        .raw = 0
    };

    template_entry.pte.EXECUTE_DENY = !access.is_executable;
    template_entry.pte.KERNELbit = !access.is_kernel;
    template_entry.pte.PAT = idx.PAT;
    template_entry.pte.RWbit = access.is_writeable;
    template_entry.pte.global = 1;
    template_entry.pte.present = 1;
    template_entry.pte.PCD = idx.PCD;
    template_entry.pte.PWT = idx.PWT;

    for (uint16_t i = 0; i < count; i++) {
        uint64_t pte_offset = sizeof(PageTableEntryUnion) * (i + pte_index);
        uint64_t pte_value = template_entry.raw + phybase + (i + pte_index) * _4KB_SIZE;
        PhyAddrAccessor::writeu64(pte_phybase + pte_offset, pte_value);
    }

    // 添加缺失的返回值
    return 0; // 假设0表示成功
}
// ====================================================================
void KspaceMapMgr::invalidate_seg()
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
int KspaceMapMgr::_4lv_pde_2MB_entries_set(
                                                phyaddr_t phybase,
                                                  vaddr_t vaddr_base,
                                                  uint16_t count,
                                                  pgaccess access
                                                  )
{//暂时只有(is_phypgsmgr_enabled==false）的逻辑
    // 检查count参数有效性
    if (count == 0) {
        return 0; // 成功设置0个条目
    }
    
    if (count > 512) [[unlikely]] {
        kputsSecure("KspaceMapMgr::_4lv_pde_2MB_entries_set: count invalid\n");
        return OS_INVALID_PARAMETER;
    }
    
    // 检查物理地址和虚拟地址是否按照2MB对齐
    if ((phybase & (_2MB_SIZE - 1)) || (vaddr_base & (_2MB_SIZE - 1))) [[unlikely]] {
        return OS_INVALID_PARAMETER; // 2MB 对齐
    }

    uint64_t highoffset = vaddr_base - PAGELV4_KSPACE_BASE;
    
    // 使用常量替代魔法数字，提高可读性
    const uint32_t PDPT_INDEX_BITS = 17;
    const uint32_t PD_INDEX_BITS = 9;

    uint32_t pdpte_index = (highoffset >> 30) & ((1ULL << PDPT_INDEX_BITS) - 1);
    uint16_t pde_index = (highoffset >> 21) & ((1 << PD_INDEX_BITS) - 1);

    // 检查是否跨越PDPT边界
    if (pde_index + count > 512) [[unlikely]] {
        kputsSecure("KspaceMapMgr::_4lv_pde_2MB_entries_set: cross PDPT boundary not allowed\n");
        return OS_OUT_OF_RANGE;
    }

    PageTableEntryUnion& pdpte = kspaceUPpdpt[pdpte_index];
    
    // 检查页目录指针表项是否存在且不是1GB页面
    if (pdpte.raw & PDPTE::PS_MASK) {
        kputsSecure("KspaceMapMgr::_4lv_pde_2MB_entries_set: under a 1GB huge page\n");
        return OS_RESOURCE_CONFILICT;
    }
    
    // 如果页目录指针表项不存在，则创建新的页目录
    if (!(pdpte.raw & PageTableEntry::P_MASK)) {
        nonleaf_pgtbentry_flagsset(pdpte);
        phyaddr_t pd_phyaddr = phymemspace_mgr::pages_alloc(1,phymemspace_mgr::KERNEL,12);
        if (pd_phyaddr == 0) return OS_OUT_OF_MEMORY;
        pdpte.pdpte.PD_addr = pd_phyaddr >> 12;
        // 初始化新分配的页目录中的所有页表项为0
        for(uint16_t i=0;i<512;i++)
        {
            PhyAddrAccessor::writeu64((pd_phyaddr & PHYS_ADDR_MASK) + sizeof(PageTableEntryUnion) * i, 0);
        }
    }

    // 修复位运算优先级问题：使用括号确保正确的运算顺序
    phyaddr_t pd_phyaddr = pdpte.pdpte.PD_addr << 12;
    cache_table_idx_struct_t idx = cache_strategy_to_idx(access.cache_strategy);

    PageTableEntryUnion template_entry = {
        .raw = 0
    };

    template_entry.pde2MB.present      = 1;
    template_entry.pde2MB.RWbit        = access.is_writeable;
    template_entry.pde2MB.KERNELbit    = !access.is_kernel;
    template_entry.pde2MB.large        = 1;        // 必须为 1
    template_entry.pde2MB.global       = access.is_global;
    template_entry.pde2MB.PAT          = idx.PAT;
    template_entry.pde2MB.PWT          = idx.PWT;
    template_entry.pde2MB.PCD          = idx.PCD;
    template_entry.pde2MB.EXECUTE_DENY = !access.is_executable;

    for (uint16_t i = 0; i < count; i++) {
        uint64_t pde_offset = sizeof(PageTableEntryUnion) * (pde_index + i);
        template_entry.pde2MB._2mb_Addr = ((phybase >> 21) + i) & ((1ULL << 27) - 1);
        PhyAddrAccessor::writeu64(pd_phyaddr + pde_offset, template_entry.raw);
    }

    // 添加缺失的返回值
    return 0; // 假设0表示成功
}
// ====================================================================
// 1GB 大页映射（PDPTE 级别）
// 要求：不能跨 PML4E 边界（即一次最多映射 512 个 1GB 页，覆盖 512GB）
// ====================================================================
int KspaceMapMgr::_4lv_pdpte_1GB_entries_set(phyaddr_t phybase,
                                                    vaddr_t vaddr_base,
                                                    uint16_t count,
                                                    pgaccess access)
{
    if (count == 0){
        kputsSecure("KspaceMapMgr::_4lv_pdpte_1GB_entries_set: count invalid\n");
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
            kputsSecure("KspaceMapMgr::_4lv_pdpte_1GB_entries_set: 1GB entry already present\n");
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

int KspaceMapMgr::pgs_remapped_free(vaddr_t addr)
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
int KspaceMapMgr::invalidate_tlb_entry()
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

int KspaceMapMgr::_4lv_pte_4KB_entries_clear(vaddr_t vaddr_base, uint16_t count)
{
    if (count == 0) return OS_SUCCESS;  // 或 OS_INVALID_PARAMETER，根据需求

    uint64_t highoffset = vaddr_base - PAGELV4_KSPACE_BASE;
    uint32_t pdpte_index = (highoffset >> 30) & ((1 << 17) - 1);
    uint16_t pde_index = (highoffset >> 21) & ((1 << 9) - 1);
    uint16_t pte_index = (highoffset >> 12) & ((1 << 9) - 1);

    if (pte_index + count > 512) {
        kputsSecure("KspaceMapMgr::_4lv_pte_4KB_entries_clear: cross page directory boundary not allowed\n");
        return OS_OUT_OF_RANGE;
    }
    if (vaddr_base % _4KB_SIZE) return OS_INVALID_PARAMETER;

    PageTableEntryUnion& pdpte = kspaceUPpdpt[pdpte_index];
    if (!(pdpte.raw & PageTableEntry::P_MASK)) {
        return OS_INVALID_ADDRESS;  // 上级 PDPTE 不存在
    }
    if (pdpte.raw & PDPTE::PS_MASK) {
        kputsSecure("KspaceMapMgr::_4lv_pte_4KB_entries_clear: PDPTE is existing 1GB page\n");
        return OS_BAD_FUNCTION;
    }

    // 修复位运算优先级问题：使用括号确保正确的运算顺序
    uint64_t pd_addr = pdpte.pdpte.PD_addr;
    uint64_t pde_offset = sizeof(PageTableEntryUnion) * pde_index;
    uint64_t pde_address = (pd_addr << 12) + pde_offset;
    uint64_t pderaw = PhyAddrAccessor::readu64(pde_address);
    
    if (!(pderaw & PageTableEntry::P_MASK)) {
        return OS_INVALID_ADDRESS; // PDE不存在
    }
    
    if (pderaw & PDE::PS_MASK) {
        kputsSecure("KspaceMapMgr::_4lv_pte_4KB_entries_clear: PDE is existing 2MB page\n");
        return OS_BAD_FUNCTION;
    }
    
    PageTableEntryUnion pde = { .raw = pderaw };
    phyaddr_t pt_phyaddr = (pde.pde.pt_addr << 12) & PHYS_ADDR_MASK;

    // 清除指定范围 PTE
    for (uint16_t i = 0; i < count; i++) {
        uint64_t pte_offset = sizeof(PageTableEntryUnion) * (pte_index + i);
        PhyAddrAccessor::writeu64(pt_phyaddr + pte_offset, 0);
    }

    // 检查整个 PT 是否全空（不限 may_full_del）
    bool is_full_del = true;
    for (uint16_t i = 0; i < 512; i++) {
        uint64_t pte_offset = sizeof(PageTableEntryUnion) * i;
        uint64_t pte_value = PhyAddrAccessor::readu64(pt_phyaddr + pte_offset);
        if (pte_value & PageTableEntry::P_MASK) {
            is_full_del = false;
            break;
        }
    }

    if (is_full_del) {
        phymemspace_mgr::pages_recycle(pt_phyaddr, 1);
        // 清除PDE项
        PhyAddrAccessor::writeu64(pde_address, 0);
    }

    return OS_SUCCESS;
}
// ====================================================================
// 1GB 大页清除（PDPTE 级别）
// 功能：直接清除 kspaceUPpdpt 中的 PDPTE 条目（无需下级页表）
// ====================================================================
int KspaceMapMgr::_4lv_pdpte_1GB_entries_clear(vaddr_t vaddr_base, uint16_t count)
{
    if (count == 0) return OS_SUCCESS;
    if (vaddr_base & (_1GB_SIZE - 1)){
        return OS_INVALID_PARAMETER;
    }

    uint64_t highoffset = vaddr_base - PAGELV4_KSPACE_BASE;
    uint32_t pdpt_index = (highoffset >> 30) & ((1ULL << 17) - 1);  // 合并索引
    uint16_t start_idx  = (highoffset >> 30) & 0x1FF;               // 在 512 项中的起始位置

    if (start_idx + count > 512){
        kputsSecure("KspaceMapMgr::_4lv_pdpte_1GB_entries_clear: cross PML4E boundary not allowed\n");
        return OS_OUT_OF_RANGE;
    }

    for (uint16_t i = 0; i < count; i++) {
        PageTableEntryUnion& entry = kspaceUPpdpt[pdpt_index + i];
        if (!(entry.raw & PageTableEntry::P_MASK)) {
            continue;  // 已空
        }
        if (!(entry.raw & PDPTE::PS_MASK)) {
            kputsSecure("KspaceMapMgr::_4lv_pdpte_1GB_entries_clear: trying to clear non-1GB PDPTE\n");
            return OS_BAD_FUNCTION;
        }
        entry.raw = 0;
    }

    // 注意：1GB 页清除后不需要回收下级页表（因为 PS=1 时没有下级）
    // 也不回收整个 512GB 块（内核空间一般保留结构）

    return OS_SUCCESS;
}
// ====================================================================
void KspaceMapMgr::enable_DEFAULT_PAT_CONFIG()
{
    wrmsr(msr::mtrr::IA32_PAT,DEFAULT_PAT_CONFIG.value);
}
// 2MB 大页清除（PDE 级别）
// 要求：不能跨 PDPT 边界（即一次最多清除 512 个 2MB 页，覆盖 1GB）
// 功能：清除 PDE 条目 + 检测整个 PD 是否全空 → 回收 PD 页表 + 清上级 PDPTE
// ====================================================================
int KspaceMapMgr::_4lv_pde_2MB_entries_clear(vaddr_t vaddr_base, uint16_t count)
{
    if (count == 0) return OS_SUCCESS;
    if (count > 512) [[unlikely]] {
        kputsSecure("KspaceMapMgr::_4lv_pde_2MB_entries_clear: count exceeds 512\n");
        return OS_INVALID_PARAMETER;
    }
    if (vaddr_base & (_2MB_SIZE - 1)) [[unlikely]] {
        return OS_INVALID_PARAMETER;
    }

    uint64_t highoffset = vaddr_base - PAGELV4_KSPACE_BASE;
    uint32_t pdpt_index = (highoffset >> 30) & ((1ULL << 17) - 1);  // 合并 PDPT 索引
    uint16_t pde_index  = (highoffset >> 21) & 0x1FF;               // 在 PD 内的起始索引

    if (pde_index + count > 512) [[unlikely]] {
        kputsSecure("KspaceMapMgr::_4lv_pde_2MB_entries_clear: cross PDPT boundary not allowed\n");
        return OS_OUT_OF_RANGE;
    }

    // 获取或验证 PD 页表
    PageTableEntryUnion& pdpte = kspaceUPpdpt[pdpt_index];
    if (!(pdpte.raw & PageTableEntry::P_MASK)) {
        return OS_INVALID_ADDRESS;  // 上级 PDPTE 不存在
    }
    if (pdpte.raw & PDPTE::PS_MASK) {
        kputsSecure("KspaceMapMgr::_4lv_pde_2MB_entries_clear: under a 1GB huge page\n");
        return OS_BAD_FUNCTION;
    }

    phyaddr_t pd_phyaddr = (pdpte.pdpte.PD_addr << 12) & PHYS_ADDR_MASK;

    // 清除指定范围的 2MB PDE 条目
    for (uint16_t i = 0; i < count; i++) {
        uint64_t pde_offset = sizeof(PageTableEntryUnion) * (pde_index + i);
        uint64_t pde_value = PhyAddrAccessor::readu64(pd_phyaddr + pde_offset);
        
        if (!(pde_value & PageTableEntry::P_MASK)) {
            // 已经为空，继续（允许重复清除）
            continue;
        }
        if (!(pde_value & PDE::PS_MASK)) {
            kputsSecure("KspaceMapMgr::_4lv_pde_2MB_entries_clear: trying to clear non-huge PDE\n");
            return OS_BAD_FUNCTION;  // 不应该出现在 2MB clear 中
        }
        PhyAddrAccessor::writeu64(pd_phyaddr + pde_offset, 0);
    }

    // 检查整个 PD 是否完全为空 → 回收 PD 页表页
    bool pd_empty = true;
    for (uint16_t i = 0; i < 512; i++) {
        uint64_t pde_offset = sizeof(PageTableEntryUnion) * i;
        uint64_t pde_value = PhyAddrAccessor::readu64(pd_phyaddr + pde_offset);
        if (pde_value & PageTableEntry::P_MASK) {
            pd_empty = false;
            break;
        }
    }

    if (pd_empty) {
        phymemspace_mgr::pages_recycle(pd_phyaddr, 1);
        pdpte.raw = 0;  // 清上级 PDPTE
        // 注意：这里不需要递归检查 PDPTE 是否全空，因为内核空间一般不回收整个 512GB 块
    }

    return OS_SUCCESS;
}
int KspaceMapMgr::v_to_phyaddrtraslation_entry
(vaddr_t vaddr, 
    PageTableEntryUnion &result, 
    uint32_t &page_size)
{
    if(pglv_4_or_5){
        uint64_t highoffset = vaddr - PAGELV4_KSPACE_BASE;
        uint32_t pdpte_index = (highoffset >> 30) & ((1 << 17) - 1);
        uint16_t pde_index = (highoffset >> 21) & ((1 << 9) - 1);
        uint16_t pte_index = (highoffset >> 12) & ((1 << 9) - 1);
        
        // 检查PDPTE
        PageTableEntryUnion& pdpte = kspaceUPpdpt[pdpte_index];
        if (!(pdpte.raw & PageTableEntry::P_MASK)) {
            return OS_INVALID_ADDRESS;
        }
        
        // 1GB 页面
        if (pdpte.raw & PDPTE::PS_MASK) {
            result = pdpte;
            page_size = _1GB_SIZE;
            return OS_SUCCESS;
        }
        
        // 获取PD物理地址
        phyaddr_t pd_phyaddr = (pdpte.pdpte.PD_addr << 12) & PHYS_ADDR_MASK;
        uint64_t pde_offset = sizeof(PageTableEntryUnion) * pde_index;
        uint64_t pde_address = pd_phyaddr + pde_offset;
        uint64_t pde_raw = PhyAddrAccessor::readu64(pde_address);
        
        // 检查PDE
        if (!(pde_raw & PageTableEntry::P_MASK)) {
            return OS_INVALID_ADDRESS;
        }
        
        PageTableEntryUnion pde = { .raw = pde_raw };
        
        // 2MB 页面
        if (pde_raw & PDE::PS_MASK) {
            result = pde;
            page_size = _2MB_SIZE;
            return OS_SUCCESS;
        }
        
        // 获取PT物理地址
        phyaddr_t pt_phyaddr = (pde.pde.pt_addr << 12) & PHYS_ADDR_MASK;
        uint64_t pte_offset = sizeof(PageTableEntryUnion) * pte_index;
        uint64_t pte_address = pt_phyaddr + pte_offset;
        uint64_t pte_raw = PhyAddrAccessor::readu64(pte_address);
        
        // 设置结果
        page_size = _4KB_SIZE;
        result.raw = pte_raw;
        return OS_SUCCESS;
    }else{
        return OS_NOT_SUPPORTED;
    }
    return OS_UNREACHABLE_CODE;
}
