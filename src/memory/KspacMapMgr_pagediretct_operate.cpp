#include "memory/AddresSpace.h"
#include "memory/Memory.h"
#include "memory/phygpsmemmgr.h"
#include "memory/kpoolmemmgr.h"
#include "memory/FreePagesAllocator.h"
#include "memory/phyaddr_accessor.h"
#include "os_error_definitions.h"
#include "linker_symbols.h"
#include "util/OS_utils.h"
#include "panic.h"
#include "msr_offsets_definitions.h"
#include "util/kptrace.h"
#ifdef USER_MODE
#include <elf.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

PageTableEntryUnion KspaceMapMgr::kspaceUPpdpt[256*512] __attribute__((section(".kspace_uppdpt")));
shared_inval_VMentry_info_t shared_inval_kspace_VMentry_info={0};
bool KspaceMapMgr::is_default_pat_config_enabled=false;
KURD_t KspaceMapMgr::default_kurd()
{
    return KURD_t(0,0,module_code::MEMORY,MEMMODULE_LOCAIONS::LOCATION_CODE_KSPACE_MAP_MGR,0,0,err_domain::CORE_MODULE);
}
KURD_t KspaceMapMgr::default_success()
{
    KURD_t kurd=default_kurd();
    kurd.result=result_code::SUCCESS;
    kurd.level=level_code::INFO;
    return kurd;
}
KURD_t KspaceMapMgr::default_failure()
{
    KURD_t kurd=default_kurd();
    kurd=set_result_fail_and_error_level(kurd);
    return kurd;
}
KURD_t KspaceMapMgr::default_fatal()
{
    KURD_t kurd=default_kurd();
    kurd=set_fatal_result_level(kurd);
    return kurd;
}
KURD_t KspaceMapMgr::Init()
{
    KURD_t success=default_success();
    success.event_code=MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_INIT;
    #ifdef KERNEL_MODE
    kspace_uppdpt_phyaddr=(phyaddr_t)&_kspace_uppdpt_lma;
    #endif
    kspace_vm_table=new kspace_vm_table_t();
    KURD_t status=KURD_t();
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
#ifdef KERNEL_MODE
    result=(vaddr_t)pgs_remapp(status,(uint64_t)&KImgphybase,(&text_end-&text_begin),PG_RX,(vaddr_t)&KImgvbase);
    if(result==0)goto regist_segment_fault;
    result=(vaddr_t)pgs_remapp(status,(uint64_t)&_data_lma,(&_data_end-&_data_start),PG_RW,(vaddr_t)&_data_start);
    if(result==0)goto regist_segment_fault;
    result=(vaddr_t)pgs_remapp(status,(uint64_t)&_rodata_lma,(&_rodata_end-&_rodata_start),PG_R,(vaddr_t)&_rodata_start);
    if(result==0)goto regist_segment_fault;
    result=(vaddr_t)pgs_remapp(status,(uint64_t)&_stack_lma,(&__klog_end-&_stack_bottom),PG_RW,(vaddr_t)&_stack_bottom);
    if(result==0)goto regist_segment_fault;
    result=(vaddr_t)pgs_remapp(status,ksymmanager::get_phybase(),align_up(sizeof(symbol_entry)*ksymmanager::get_entry_count(),4096),PG_R,ksymmanager::get_virtbase());
    if(result==0)goto regist_segment_fault;
    result=(vaddr_t)pgs_remapp(status,0,basic_seg_size,PG_RW,0,true);
    if(result==0)goto regist_segment_fault;

    PhyAddrAccessor::BASIC_DESC.start=result;
#endif
#ifdef USER_MODE
    // 需要读取kernel.elf的段信息进行模仿完成内核映像的虚拟映射注册
    // 在用户空间读取kernel.elf文件，分析其段信息并进行模拟映射
    
    // 打开kernel.elf文件
    const char* elf_path = "/home/pangsong/PS_git/OS_pj_uefi/kernel/kernel.elf";
    int fd = open(elf_path, O_RDONLY);
    if (fd < 0) {
        // 如果找不到kernel.elf，使用默认映射
        return OS_BAD_FUNCTION;
    }

    // 获取文件大小
    struct stat sb;
    if (fstat(fd, &sb) < 0) {
        close(fd);
        return OS_BAD_FUNCTION;
    }
    
    // 映射文件到内存
    void* elf_mapped = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (elf_mapped == MAP_FAILED) {
        close(fd);
        return OS_BAD_FUNCTION;
    }

    // ELF头部指针
    Elf64_Ehdr* ehdr = (Elf64_Ehdr*)elf_mapped;
    
    // 验证ELF头部
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 || 
        ehdr->e_ident[EI_MAG1] != ELFMAG1 || 
        ehdr->e_ident[EI_MAG2] != ELFMAG2 || 
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        munmap(elf_mapped, sb.st_size);
        close(fd);
        return OS_BAD_FUNCTION;
    }

    // 获取程序头表
    Elf64_Phdr* phdr = (Elf64_Phdr*)((char*)elf_mapped + ehdr->e_phoff);

    // 遍历程序头表，映射各段
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD && phdr[i].p_memsz > 0&&phdr[i].p_vaddr >= PAGELV4_KSPACE_BASE) {
            // 根据段的权限设置访问标志
            pgaccess access;
            access.is_kernel = 1;
            access.is_executable = (phdr[i].p_flags & PF_X) ? 1 : 0;
            access.is_writeable = (phdr[i].p_flags & PF_W) ? 1 : 0;
            access.is_readable = (phdr[i].p_flags & PF_R) ? 1 : 0;
            access.is_global = 1;
            access.cache_strategy = cache_strategy_t::WB;

            // 使用pgs_remapp映射段
            result = (vaddr_t)pgs_remapp(
                phdr[i].p_paddr,     // 虚拟地址
                phdr[i].p_memsz,     // 内存大小
                access,              // 访问权限
                phdr[i].p_vaddr      // 映射到相同虚拟地址
            );
            
            if (result == 0) {
                munmap(elf_mapped, sb.st_size);
                close(fd);
                goto regist_segment_fault;
            }
        }
    }

    // 清理资源
    munmap(elf_mapped, sb.st_size);
    close(fd);
    
#endif

    
    return success;  
    regist_segment_fault:
    return status;

}
void nonleaf_pgtbentry_flagsset(PageTableEntryUnion&entry){//内核态的是全局的，
    entry.raw=0;
    entry.pte.present=1;
    entry.pte.RWbit=1;
    entry.pte.KERNELbit=0;
}
/**
 * 锁在外部接口中持有
 */
KURD_t KspaceMapMgr::_4lv_pte_4KB_entries_set(
    phyaddr_t phybase,
    vaddr_t vaddr_base,
    uint16_t count,
    pgaccess access
    )
{//暂时只有(is_phypgsmgr_enabled==false）的逻辑
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_SET;
    fail.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_SET;
    fatal.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_SET;

    // 检查count参数有效性
    if (count == 0) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_SET_RESULTS_CODE::FAIL_REASONS::REASON_CODE_BAD_COUNT;
        return fail;
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
    if(pdpte.raw & PDPTE::PS_MASK){
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_SET_RESULTS_CODE::FATAL_REASONS::REASON_CODE_HUGE_PDPTE_WHEN_GET_SUB;
        return fail;
    }
    // 检查页目录指针表项是否存在且不是1GB页面
    if (!(pdpte.raw & PageTableEntry::P_MASK) ){
        nonleaf_pgtbentry_flagsset(pdpte);
        KURD_t kurd;
        phyaddr_t pd_phyaddr = FreePagesAllocator::first_BCB->allocate_buddy_way(_4KB_SIZE,kurd);
        if (pd_phyaddr == 0||kurd.result!=result_code::SUCCESS) return kurd;
        kurd=phymemspace_mgr::pages_dram_buddy_pages_set(pd_phyaddr,1,KERNEL);
        if(kurd.result!=result_code::SUCCESS){
                return kurd;
        }
        pdpte.pdpte.PD_addr = pd_phyaddr >> 12;
        // 初始化新分配的页目录中的所有页表项为0
        for(uint16_t i=0;i<512;i++)
        {
            PhyAddrAccessor::writeu64((pd_phyaddr & PHYS_ADDR_MASK) + sizeof(PageTableEntryUnion) * i, 0);
        }
    }
    phyaddr_t pde_loacte_phyaddr = (pdpte.pdpte.PD_addr << 12) +sizeof(PageTableEntryUnion) * pde_index;
    uint64_t pderaw = PhyAddrAccessor::readu64(pde_loacte_phyaddr);
    PageTableEntryUnion pde = {
        .raw = pderaw
    };
    if(pderaw & PDE::PS_MASK){
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_SET_RESULTS_CODE::FATAL_REASONS::REASON_CODE_HUGE_PDE_WHEN_GET_SUB;
        return fail;
    }
    // 检查页目录项是否存在且不是2MB页面
    if (!(pderaw & PageTableEntry::P_MASK))
        {
            nonleaf_pgtbentry_flagsset(pde);
            KURD_t kurd;
            phyaddr_t pt_phyaddr = FreePagesAllocator::first_BCB->allocate_buddy_way(_4KB_SIZE,kurd);
            if (pt_phyaddr == 0||kurd.result!=result_code::SUCCESS) return kurd;
            kurd=phymemspace_mgr::pages_dram_buddy_pages_set(pt_phyaddr,1,KERNEL);
            if(kurd.result!=result_code::SUCCESS){
                return kurd;
            }
            pde.pde.pt_addr = pt_phyaddr >> 12;
            PhyAddrAccessor::writeu64(pde_loacte_phyaddr, pde.raw);
            // 初始化新分配的页表中的所有页表项为0
            for(uint16_t i=0;i<512;i++)
            {
                PhyAddrAccessor::writeu64((pt_phyaddr & PHYS_ADDR_MASK) + sizeof(PageTableEntryUnion) * i, 0);
            }
        }

    phyaddr_t pte_phybase = (pde.pde.pt_addr << 12) & PHYS_ADDR_MASK;
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
        uint64_t pte_value = template_entry.raw + phybase + i * _4KB_SIZE;
        PhyAddrAccessor::writeu64(pte_phybase + pte_offset, pte_value);
    }

    return success;
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
        Panic::panic("invalid_k space_VMentry_handler: stared_inval_kspace_VMentry_info is invalid");
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
                Panic::panic("invalid_kspace_VMentry_handler: invalid page size in kspace_VMentry_info");
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
KURD_t KspaceMapMgr::_4lv_pde_2MB_entries_set(
                                                phyaddr_t phybase,
                                                  vaddr_t vaddr_base,
                                                  uint16_t count,
                                                  pgaccess access
                                                  )
{//暂时只有(is_phypgsmgr_enabled==false）的逻辑
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_SET;
    fail.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_SET;
    fatal.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_SET;

    // 检查count参数有效性
    if (count == 0) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_SET_RESULTS_CODE::FAIL_REASONS::REASON_CODE_BAD_COUNT;
        return fail;
    }
    
    if (count > 512) [[unlikely]] {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_SET_RESULTS_CODE::FAIL_REASONS::REASON_CODE_COUNT_AND_BASEINDEX_OUT_OF_RANGE;
        return fail;
    }
    
    // 检查物理地址和虚拟地址是否按照2MB对齐
    if ((phybase & (_2MB_SIZE - 1)) || (vaddr_base & (_2MB_SIZE - 1))) [[unlikely]] {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_SET_RESULTS_CODE::FAIL_REASONS::REASON_CODE_BASE_NOT_ALIGNED;
        return fail;
    }

    uint64_t highoffset = vaddr_base - PAGELV4_KSPACE_BASE;
    
    // 使用常量替代魔法数字，提高可读性
    const uint32_t PDPT_INDEX_BITS = 17;
    const uint32_t PD_INDEX_BITS = 9;

    uint32_t pdpte_index = (highoffset >> 30) & ((1ULL << PDPT_INDEX_BITS) - 1);
    uint16_t pde_index = (highoffset >> 21) & ((1 << PD_INDEX_BITS) - 1);

    // 检查是否跨越PDPT边界
    if (pde_index + count > 512) [[unlikely]] {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_SET_RESULTS_CODE::FAIL_REASONS::REASON_CODE_COUNT_AND_BASEINDEX_OUT_OF_RANGE;
        return fail;
    }

    PageTableEntryUnion& pdpte = kspaceUPpdpt[pdpte_index];
    
    // 检查页目录指针表项是否存在且不是1GB页面
    if (pdpte.raw & PDPTE::PS_MASK) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_SET_RESULTS_CODE::FATAL_REASONS::REASON_CODE_HUGE_PDPTE_WHEN_GET_SUB;
        return fail;
    }
    
    // 如果页目录指针表项不存在，则创建新的页目录
    if (!(pdpte.raw & PageTableEntry::P_MASK)) {
        nonleaf_pgtbentry_flagsset(pdpte);
        KURD_t kurd;
        phyaddr_t pd_phyaddr = FreePagesAllocator::first_BCB->allocate_buddy_way(_4KB_SIZE,kurd);
        if (pd_phyaddr == 0||kurd.result!=result_code::SUCCESS) return kurd;
        kurd=phymemspace_mgr::pages_dram_buddy_pages_set(pd_phyaddr,1,KERNEL);
        if(kurd.result!=result_code::SUCCESS){
                return kurd;
        }
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

    return success;
}
// ====================================================================
// 1GB 大页映射（PDPTE 级别）
// 要求：不能跨 PML4E 边界（即一次最多映射 512 个 1GB 页，覆盖 512GB）
// ====================================================================
KURD_t KspaceMapMgr::_4lv_pdpte_1GB_entries_set(phyaddr_t phybase,
                                                    vaddr_t vaddr_base,
                                                    uint16_t count,
                                                    pgaccess access)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_SET;
    fail.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_SET;
    fatal.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_SET;

    if (count == 0){
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_SET_RESULTS_CODE::FAIL_REASONS::REASON_CODE_BAD_COUNT;
        return fail;
    }
    if ((phybase & (_1GB_SIZE - 1)) || (vaddr_base & (_1GB_SIZE - 1))) [[unlikely]] {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_SET_RESULTS_CODE::FAIL_REASONS::REASON_CODE_BASE_NOT_ALIGNED;
        return fail; // 1GB 对齐
    }

    uint64_t highoffset = vaddr_base - PAGELV4_KSPACE_BASE;
    uint32_t pdpt_index = (highoffset >> 30) & ((1ULL << 17) - 1);  // 256×512 合并表索引
    uint16_t start_idx  = (highoffset >> 30) & 0x1FF;               // 在该 PDPTE 表内的起始偏移
    cache_table_idx_struct_t idx = cache_strategy_to_idx(access.cache_strategy);

    for (uint16_t i = 0; i < count; i++) {
        PageTableEntryUnion& entry = kspaceUPpdpt[pdpt_index + i];
        if (entry.raw & PageTableEntry::P_MASK) [[unlikely]] {
            fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::ENABLE_VMENTRY_RESULTS::FAIL_REASONS::REASON_CODE_INVALID_PAGETABLE_ENTRY;
            return fail;
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
    return success;
}

KURD_t KspaceMapMgr::pgs_remapped_free(vaddr_t addr)
{
    KURD_t status = KURD_t();
    VM_DESC nullentry = {0};
    VM_DESC&vmentry=nullentry;
    GMlock.lock();
    status=VM_search_by_vaddr(addr, vmentry);
    GMlock.unlock();
    if (status.result != result_code::SUCCESS)
    {
        return status;
    }
    GMlock.lock();
    status=disable_VMentry(vmentry);
    GMlock.unlock();
    if (status.result != result_code::SUCCESS)
    {
        return status;
    }
    shared_inval_kspace_VMentry_info.is_package_valid = true;
    shared_inval_kspace_VMentry_info.completed_processors_count = 0;
    
    status=invalidate_tlb_entry();
    int i=VM_del(&vmentry);
    if (i){
        //todo:删除失败,构造新的
    }
    //todo:广播其他处理器失效tlb以及等待校验，若超时要用其它手段跳出直接panic
    
}
KURD_t KspaceMapMgr::invalidate_tlb_entry()
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_INVALIDATE_TLB;
    fail.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_INVALIDATE_TLB;
    fatal.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_INVALIDATE_TLB;
    
    if (shared_inval_kspace_VMentry_info.is_package_valid == false) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::INVALIDATE_TLB_RESULTS::FAIL_REASONS::REASON_CODE_BAD_VM_ENTRY;
        return fail;
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
                fatal.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::INVALIDATE_TLB_RESULTS::FATAL_REASONS::REASON_CODE_INVALID_PAGE_SIZE;
                return fatal;
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
    return success;
}

KURD_t KspaceMapMgr::_4lv_pte_4KB_entries_clear(vaddr_t vaddr_base, uint16_t count)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_CLEAR;
    fail.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_CLEAR;
    fatal.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_CLEAR;

    if (count == 0) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FAIL_REASONS::REASON_CODE_BAD_COUNT;
        return fail;
    }

    uint64_t highoffset = vaddr_base - PAGELV4_KSPACE_BASE;
    uint32_t pdpte_index = (highoffset >> 30) & ((1 << 17) - 1);
    uint16_t pde_index = (highoffset >> 21) & ((1 << 9) - 1);
    uint16_t pte_index = (highoffset >> 12) & ((1 << 9) - 1);

    if (pte_index + count > 512) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FAIL_REASONS::REASON_CODE_COUNT_AND_BASEINDEX_OUT_OF_RANGE;
        return fail;
    }
    if (vaddr_base % _4KB_SIZE) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FAIL_REASONS::REASON_CODE_BASE_NOT_ALIGNED;
        return fail;
    }

    PageTableEntryUnion& pdpte = kspaceUPpdpt[pdpte_index];
    if (!(pdpte.raw & PageTableEntry::P_MASK)) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FATAL_REASONS::REASON_CODE_HUGE_PDPTE_SUBTABLE_NOT_EXIST;
        return fail;
    }
    if (pdpte.raw & PDPTE::PS_MASK) {
        fatal.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FATAL_REASONS::REASON_CODE_HUGE_PDPTE_UNTIMELY;
        return fatal;
    }

    // 修复位运算优先级问题：使用括号确保正确的运算顺序
    uint64_t pd_addr = pdpte.pdpte.PD_addr;
    uint64_t pde_offset = sizeof(PageTableEntryUnion) * pde_index;
    uint64_t pde_address = (pd_addr << 12) + pde_offset;
    uint64_t pderaw = PhyAddrAccessor::readu64(pde_address);
    
    if (!(pderaw & PageTableEntry::P_MASK)) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FATAL_REASONS::REASON_CODE_HUGE_PDE_SUBTABLE_NOT_EXIST;
        return fail;
    }
    
    if (pderaw & PDE::PS_MASK) {
        fatal.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FATAL_REASONS::REASON_CODE_HUGE_PDE_UNTIMELY;
        return fatal;
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

    return success;
}
// ====================================================================
// 1GB 大页清除（PDPTE 级别）
// 功能：直接清除 kspaceUPpdpt 中的 PDPTE 条目（无需下级页表）
// ====================================================================
KURD_t KspaceMapMgr::_4lv_pdpte_1GB_entries_clear(vaddr_t vaddr_base, uint16_t count)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_CLEAR;
    fail.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_CLEAR;
    fatal.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_CLEAR;

    if (count == 0) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FAIL_REASONS::REASON_CODE_BAD_COUNT;
        return fail;
    }
    if (vaddr_base & (_1GB_SIZE - 1)) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FAIL_REASONS::REASON_CODE_BASE_NOT_ALIGNED;
        return fail;
    }

    uint64_t highoffset = vaddr_base - PAGELV4_KSPACE_BASE;
    uint32_t pdpt_index = (highoffset >> 30) & ((1ULL << 17) - 1);  // 合并索引
    uint16_t start_idx  = (highoffset >> 30) & 0x1FF;               // 在 512 项中的起始位置

    if (start_idx + count > 512) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FAIL_REASONS::REASON_CODE_COUNT_AND_BASEINDEX_OUT_OF_RANGE;
        return fail;
    }

    for (uint16_t i = 0; i < count; i++) {
        PageTableEntryUnion& entry = kspaceUPpdpt[pdpt_index + i];
        if (!(entry.raw & PageTableEntry::P_MASK)) {
            continue;  // 已空
        }
        if (!(entry.raw & PDPTE::PS_MASK)) {
            fatal.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FATAL_REASONS::REASON_CODE_HUGE_PDPTE_NOT_EXIST;
            return fatal;
        }
        entry.raw = 0;
    }

    // 注意：1GB 页清除后不需要回收下级页表（因为 PS=1 时没有下级）
    // 也不回收整个 512GB 块（内核空间一般保留结构）

    return success;
}
// ====================================================================
void KspaceMapMgr::enable_DEFAULT_PAT_CONFIG()
{
    
    uint32_t value_high=(DEFAULT_PAT_CONFIG.value>>32)&0xffffffff, value_low=DEFAULT_PAT_CONFIG.value&0xffffffff; 
    asm volatile("wrmsr"
                 :
                 : "c" (msr::mtrr::IA32_PAT),
                   "a" (value_low),
                   "d" (value_high));
    
}
// 2MB 大页清除（PDE 级别）
// 要求：不能跨 PDPT 边界（即一次最多清除 512 个 2MB 页，覆盖 1GB）
// 功能：清除 PDE 条目 + 检测整个 PD 是否全空 → 回收 PD 页表 + 清上级 PDPTE
// ====================================================================
KURD_t KspaceMapMgr::_4lv_pde_2MB_entries_clear(vaddr_t vaddr_base, uint16_t count)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_CLEAR;
    fail.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_CLEAR;
    fatal.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_PAGES_CLEAR;

    if (count == 0) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FAIL_REASONS::REASON_CODE_BAD_COUNT;
        return fail;
    }
    if (count > 512) [[unlikely]] {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FAIL_REASONS::REASON_CODE_COUNT_AND_BASEINDEX_OUT_OF_RANGE;
        return fail;
    }
    if (vaddr_base & (_2MB_SIZE - 1)) [[unlikely]] {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FAIL_REASONS::REASON_CODE_BASE_NOT_ALIGNED;
        return fail;
    }

    uint64_t highoffset = vaddr_base - PAGELV4_KSPACE_BASE;
    uint32_t pdpt_index = (highoffset >> 30) & ((1ULL << 17) - 1);  // 合并 PDPT 索引
    uint16_t pde_index  = (highoffset >> 21) & 0x1FF;               // 在 PD 内的起始索引

    if (pde_index + count > 512) [[unlikely]] {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FAIL_REASONS::REASON_CODE_COUNT_AND_BASEINDEX_OUT_OF_RANGE;
        return fail;
    }

    // 获取或验证 PD 页表
    PageTableEntryUnion& pdpte = kspaceUPpdpt[pdpt_index];
    if (!(pdpte.raw & PageTableEntry::P_MASK)) {
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FATAL_REASONS::REASON_CODE_HUGE_PDPTE_SUBTABLE_NOT_EXIST;
        return fail;
    }
    if (pdpte.raw & PDPTE::PS_MASK) {
        fatal.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FATAL_REASONS::REASON_CODE_HUGE_PDPTE_UNTIMELY;
        return fatal;
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
            fatal.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::PAGES_CLEAR_RESULTS_CODE::FATAL_REASONS::REASON_CODE_HUGE_PDE_NOT_EXIST;
            return fatal;
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

    return success;
}
KURD_t KspaceMapMgr::v_to_phyaddrtraslation_entry
(vaddr_t vaddr, 
    PageTableEntryUnion &result, 
    uint32_t &page_size)
{
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_TRAN_TO_PHY_ENTRY;
    fail.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_TRAN_TO_PHY_ENTRY;
    fatal.event_code = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::EVENT_CODE_TRAN_TO_PHY_ENTRY;

    if(pglv_4_or_5){
        uint64_t highoffset = vaddr - PAGELV4_KSPACE_BASE;
        uint32_t pdpte_index = (highoffset >> 30) & ((1 << 17) - 1);
        uint16_t pde_index = (highoffset >> 21) & ((1 << 9) - 1);
        uint16_t pte_index = (highoffset >> 12) & ((1 << 9) - 1);
        
        // 检查PDPTE
        PageTableEntryUnion& pdpte = kspaceUPpdpt[pdpte_index];
        if (!(pdpte.raw & PageTableEntry::P_MASK)) {
            fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::TRAN_TO_PHY_ENTRY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_NOT_PRESENT_ENTRY;
            return fail;
        }
        
        // 1GB 页面
        if (pdpte.raw & PDPTE::PS_MASK) {
            result = pdpte;
            page_size = _1GB_SIZE;
            return success;
        }
        
        // 获取PD物理地址
        phyaddr_t pd_phyaddr = (pdpte.pdpte.PD_addr << 12) & PHYS_ADDR_MASK;
        uint64_t pde_offset = sizeof(PageTableEntryUnion) * pde_index;
        uint64_t pde_address = pd_phyaddr + pde_offset;
        uint64_t pde_raw = PhyAddrAccessor::readu64(pde_address);
        
        // 检查PDE
        if (!(pde_raw & PageTableEntry::P_MASK)) {
            fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::TRAN_TO_PHY_ENTRY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_NOT_PRESENT_ENTRY;
            return fail;
        }
        
        PageTableEntryUnion pde = { .raw = pde_raw };
        
        // 2MB 页面
        if (pde_raw & PDE::PS_MASK) {
            result = pde;
            page_size = _2MB_SIZE;
            return success;
        }
        
        // 获取PT物理地址
        phyaddr_t pt_phyaddr = (pde.pde.pt_addr << 12) & PHYS_ADDR_MASK;
        uint64_t pte_offset = sizeof(PageTableEntryUnion) * pte_index;
        uint64_t pte_address = pt_phyaddr + pte_offset;
        uint64_t pte_raw = PhyAddrAccessor::readu64(pte_address);
        
        // 设置结果
        page_size = _4KB_SIZE;
        result.raw = pte_raw;
        return success;
    }else{
        fail.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::TRAN_TO_PHY_ENTRY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_NOT_SUPPORT_LV5_PAGING;
        return fail;
    }
    fatal.reason = MEMMODULE_LOCAIONS::KSPACE_MAPPER_EVENTS::TRAN_TO_PHY_ENTRY_RESULTS_CODE::FATAL_REASONS::REASON_CODE_UNREACHABLE_CODE;
    return fatal;
}
