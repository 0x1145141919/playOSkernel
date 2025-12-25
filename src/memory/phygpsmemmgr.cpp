#include "memory/phygpsmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#include "memory/kpoolmemmgr.h"
#include "linker_symbols.h"
#include "VideoDriver.h"
#include "memory/phyaddr_accessor.h"
#ifdef USER_MODE
#include <elf.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
// 定义phymemspace_mgr的静态成员变量
phymemspace_mgr::phymemmgr_statistics_t phymemspace_mgr::statisitcs = {0};
phymemspace_mgr::PHYSEG_LIST_ITEM* phymemspace_mgr::physeg_list = nullptr;
spinlock_cpp_t phymemspace_mgr::module_global_lock = spinlock_cpp_t();
Ktemplats::sparse_table_2level_no_OBJCONTENT<uint32_t, phymemspace_mgr::page_size1gb_t, __builtin_ctz(MAX_PHYADDR_1GB_PGS_COUNT)-9, 9>* phymemspace_mgr::top_1gb_table = nullptr;
phymemspace_mgr::low1mb_mgr_t* phymemspace_mgr::low1mb_mgr = nullptr;

// 定义low1mb_mgr_t的静态成员变量
phymemspace_mgr::low1mb_mgr_t::interval_LinkList phymemspace_mgr::low1mb_mgr_t::low1mb_seg_list;
void phymemspace_mgr::phy_to_indices(phyaddr_t p, uint64_t &idx_1gb, uint64_t &idx_2mb, uint64_t &idx_4kb)
{//建议修改为匿名函数
    int64_t off = p; // base 是 segment base
    idx_1gb = (off>>30)&511;
    idx_2mb = (off>>21)&511;
    idx_4kb = (off>>12)&511;
}

phymemspace_mgr::atom_page_ptr::atom_page_ptr(
    uint32_t idx1g, uint16_t idx2m, uint16_t idx4k)
{



    _1gbtb_idx = idx1g;
    _2mbtb_offestidx = (idx2m < 512 ? idx2m : 0);
    _4kb_offestidx  = (idx4k < 512 ? idx4k : 0);

    page_size1gb_t *p1 = top_1gb_table->get(_1gbtb_idx);
    if(!p1){page_size=1;return;}//1GB表项不存在,不能返回错误值但是可以用非法page_size来占位，后续the_next会检查到并且返回错误码
    // ====== Case 1: 1GB 原子页 =================================
    if (!p1->flags.is_sub_valid) {
        page_size = _1GB_PG_SIZE;
        _2mbtb_offestidx = 0;
        _4kb_offestidx = 0;
        page_strut_ptr = p1;
        return;
    }

    // ====== Case 2: 2MB 级别存在 ===============================
    page_size2mb_t *p2 = &(p1->sub2mbpages[_2mbtb_offestidx]);

    if (!p2->flags.is_sub_valid) {
        // 2MB 原子页
        page_size = _2MB_PG_SIZE;
        _4kb_offestidx = 0;
        page_strut_ptr = p2;
        return;
    }

    // ====== Case 3: 4KB 子表存在 ===============================
    page_size = _4KB_PG_SIZE;
    
    if (_4kb_offestidx >= 512)
        _4kb_offestidx = 0;

    page_strut_ptr = &(p2->sub_pages[_4kb_offestidx]);
}


int phymemspace_mgr::atom_page_ptr::the_next()
{//要改，page_size1gb_t改成获得指针而非引用


    switch (page_size)
    {
    // ----------------------------------------------------------------------
    // 4KB atomic page: increment _4kb offset, or move to next 2MB entry
    // ----------------------------------------------------------------------
    case _4KB_PG_SIZE:
    {
        if (_4kb_offestidx + 1 < 512) {
            _4kb_offestidx++;

            page_size1gb_t *p1 = top_1gb_table->get(_1gbtb_idx);
            page_size2mb_t *p2 = &(p1->sub2mbpages[_2mbtb_offestidx]);

            page_strut_ptr = &(p2->sub_pages[_4kb_offestidx]);
            return OS_SUCCESS;
        }

        // 否则进入下一个 2MB
        _4kb_offestidx = 0;
        // 继续向下执行 2MB 的逻辑
    }
    [[fallthrough]];

    // ----------------------------------------------------------------------
    // 2MB atomic page: increment _2mb offset, or move to next 1GB entry
    // ----------------------------------------------------------------------
    case _2MB_PG_SIZE:
    {
        page_size1gb_t *p1 = top_1gb_table->get(_1gbtb_idx);

        if (_2mbtb_offestidx + 1 < 512) {
            _2mbtb_offestidx++;

            page_size2mb_t *p2 = &(p1->sub2mbpages[_2mbtb_offestidx]);

            // 检查下一级子页是否存在

            bool has_sub = p2->flags.is_sub_valid;


            if (has_sub) {
                // 转变为 4KB atomic page
                page_size = _4KB_PG_SIZE;
                _4kb_offestidx = 0;
                page_strut_ptr = &(p2->sub_pages[0]);
            } else {
                // 仍然是 2MB atomic page
                page_strut_ptr = p2;
            }
            return 0;
        }

        // 否则进入下一条 1GB
        _2mbtb_offestidx = 0;
        // 继续向下执行 1GB 的逻辑
    }
    [[fallthrough]];

    // ----------------------------------------------------------------------
    // 1GB atomic page: increment _1gb index, or fail
    // ----------------------------------------------------------------------
    case _1GB_PG_SIZE:
    {
        
        _1gbtb_idx++;

        page_size1gb_t *p1 = top_1gb_table->get(_1gbtb_idx);
        if(!p1)return OS_INVALID_ADDRESS;//应该填遍历到无效表项

        // 是否有子表？
        bool has_sub = p1->flags.is_sub_valid;

        if (has_sub) {
            // 进入 2MB table
            page_size = _2MB_PG_SIZE;
            _2mbtb_offestidx = 0;
            page_size2mb_t *p2 = &(p1->sub2mbpages[0]);
            bool has_sub_2 = p2->flags.is_sub_valid;
            if (has_sub_2) {
                page_size = _4KB_PG_SIZE;
                _4kb_offestidx = 0;
                page_strut_ptr = &(p2->sub_pages[0]);
            } else {
                page_strut_ptr = p2;
            }
        }
        else {
            // 仍然是 1GB atomic page
            page_size = _1GB_PG_SIZE;
            _2mbtb_offestidx = 0;
            _4kb_offestidx = 0;
            page_strut_ptr = p1;
        }

        return 0;
    }

    default:
        return OS_INVALID_ADDRESS; // 非法 page_size
    }
    return OS_UNREACHABLE_CODE;
}
static inline uint64_t align_down(uint64_t x, uint64_t a){ return x & ~(a-1); }

static constexpr uint64_t PAGES_4KB_PER_2MB = 512;
static constexpr uint64_t PAGES_2MB_PER_1GB = 512;
static constexpr uint64_t PAGES_4KB_PER_1GB = PAGES_4KB_PER_2MB * PAGES_2MB_PER_1GB; // 262144
// 将 4KB 页号(从 segment base 开始的页号) -> 1GB 索引/2MB 索引/4KB offset
inline uint64_t pg4k_to_1gb_idx(uint64_t pg4k_index) {
    return pg4k_index / PAGES_4KB_PER_1GB;
}
inline uint64_t pg4k_to_2mb_idx(uint64_t pg4k_index) {
    return pg4k_index / PAGES_4KB_PER_2MB;
}
inline uint16_t pg4k_to_4kb_offset(uint64_t pg4k_index) {
    return (uint16_t)(pg4k_index & (PAGES_4KB_PER_2MB - 1)); // 0..511
}
inline uint16_t pg2mb_to_4kb_offset_in_2mb(uint64_t pg2mb_index) {
    return 0; // for clarity; used as above if needed
}



int phymemspace_mgr::pages_recycle(phyaddr_t phybase, uint64_t numof_4kbpgs)
{
    module_global_lock.lock();
    PHYSEG seg=get_physeg_by_addr(phybase);
    if(seg.type!=DRAM_SEG){
        module_global_lock.unlock();
        return OS_NOT_SUPPORT;//表明的语义应该是不在dram段里面
    }
    page_state_t to_del_page_state;
    int status=pages_recycle_verify(phybase,numof_4kbpgs,to_del_page_state);
    if(status!=OS_SUCCESS){module_global_lock.unlock();return OS_RESOURCE_CONFILICT;}
    pages_state_set_flags_t flags={
        .op=pages_state_set_flags_t::normal,
        .params={
            .if_init_ref_count=0,
            .if_mmio=0
        }
    };
    status=pages_state_set(phybase,numof_4kbpgs,FREE,flags);
    switch(to_del_page_state){
        case KERNEL:
            statisitcs.kernel-=numof_4kbpgs;
            seg.statistics.kernel-=numof_4kbpgs;
            break;
        case USER_ANONYMOUS:
            statisitcs.user_anonymous-=numof_4kbpgs;
            seg.statistics.user_anonymous-=numof_4kbpgs;
            break;
        case USER_FILE:
            statisitcs.user_file-=numof_4kbpgs;
            seg.statistics.user_file-=numof_4kbpgs;
            break;
        case DMA:
            statisitcs.dma-=numof_4kbpgs;
            seg.statistics.dma-=numof_4kbpgs;  
        }
    module_global_lock.unlock();
    return status;

}

int phymemspace_mgr::pages_mmio_unregist(phyaddr_t phybase, uint64_t numof_4kbpgs)
{
    module_global_lock.lock();
    PHYSEG seg=get_physeg_by_addr(phybase);
    if(seg.type!=MMIO_SEG){
        module_global_lock.unlock();
        return OS_NOT_SUPPORT;//表明的语义应该是不在dram段里面
    }
    pages_state_set_flags_t flags={
        .op=pages_state_set_flags_t::normal,
        .params={
            .if_init_ref_count=0,
            .if_mmio=1
        }
    };
    int status=pages_state_set(phybase,numof_4kbpgs,MMIO_FREE,flags);
    module_global_lock.unlock();
    return status;
}
int phymemspace_mgr::Init()
{
    int status=0;    
    //todo 注册函数
    physeg_list=new PHYSEG_LIST_ITEM();
    top_1gb_table=new Ktemplats::sparse_table_2level_no_OBJCONTENT<uint32_t,page_size1gb_t,__builtin_ctz(MAX_PHYADDR_1GB_PGS_COUNT)-9,9>;
    uint32_t phymemtb_count=gBaseMemMgr.getRootPhysicalMemoryDescriptorTableEntryCount();
    phy_memDescriptor*base=gBaseMemMgr.getGlobalPhysicalMemoryInfo();
    
    for(uint32_t i=0;i<phymemtb_count;i++)
    {
        if(base[i].PhysicalStart<0x100000)continue;
        seg_type_t seg_state;
        switch(base[i].Type)
        {
            case freeSystemRam:seg_state=DRAM_SEG;break;
            case EFI_RUNTIME_SERVICES_DATA:
            case EFI_RUNTIME_SERVICES_CODE:
            case EFI_ACPI_RECLAIM_MEMORY:
            case EFI_ACPI_MEMORY_NVS:seg_state=RESERVED_SEG;break;
            case EFI_MEMORY_MAPPED_IO:
            case EFI_MEMORY_MAPPED_IO_PORT_SPACE:seg_state=MMIO_SEG;break;
            case EFI_RESERVED_MEMORY_TYPE:seg_state=RESERVED_SEG;break;
            case OS_MEMSEG_HOLE:continue;
            default:seg_state=RESERVED_SEG; 
            break;
        } 
        phy_memDescriptor& seg=base[i];
        phyaddr_t segbase=seg.PhysicalStart;
        blackhole_acclaim_flags_t flags = {
            .a=0
        };
        status=blackhole_acclaim(segbase,seg.NumberOfPages,seg_state,flags);//i==8时候出现了错误,问题在于有时候会跨越2mb段会跨越两个1gb表项但是没能正确处理
        
        if(status!=OS_SUCCESS)return status;

    }
    low1mb_mgr=new low1mb_mgr_t();
    pages_state_set_flags_t low1mb_pgs_set = {.op=pages_state_set_flags_t::normal,.params={.if_init_ref_count=1,.if_mmio=0}};
    status=pages_state_set(0,256,LOW1MB,low1mb_pgs_set);
#ifdef KERNEL_MODE
    VM_DESC& basic_desc_ref= PhyAddrAccessor::BASIC_DESC;
    basic_desc_ref.SEG_SIZE_ONLY_UES_IN_BASIC_SEG=gBaseMemMgr.getMaxPhyaddr();
    //内核映像注册
    pages_state_set_flags_t Kimage_regist_flags = {.op=pages_state_set_flags_t::normal,.params={.if_init_ref_count=1,.if_mmio=0}};
    
    
    if(status != OS_SUCCESS) return status;
    low1mb_mgr_t::low1mb_seg_t trampoile={
        .base=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&init_text_begin)),
        .size=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&init_stack_end) - reinterpret_cast<uintptr_t>(&init_text_begin)),
        .type=low1mb_mgr_t::LOW1MB_TRAMPOILE_SEG
    };
    status=low1mb_mgr->regist_seg(trampoile);
    if(status != OS_SUCCESS)return status;
    status=pages_state_set((phyaddr_t)&KImgphybase,(phyaddr_t)(text_end-text_begin)/_4KB_PG_SIZE,KERNEL_PERSIST, Kimage_regist_flags);
    if(status != OS_SUCCESS) return status;
    
    status=pages_state_set((phyaddr_t)&_data_lma,(&_data_end-&_data_start)/_4KB_PG_SIZE,KERNEL_PERSIST, Kimage_regist_flags);
    if(status != OS_SUCCESS) return status;
    
    status=pages_state_set((phyaddr_t)&_rodata_lma,(&_rodata_end-&_rodata_start)/_4KB_PG_SIZE,KERNEL_PERSIST, Kimage_regist_flags);
    if(status != OS_SUCCESS) return status;
    
    status=pages_state_set((phyaddr_t)&_stack_lma,(&_stack_top-&_stack_bottom)/_4KB_PG_SIZE,KERNEL_PERSIST, Kimage_regist_flags);
    if(status != OS_SUCCESS) return status;
    
    status=pages_state_set((phyaddr_t)&_heap_bitmap_lma,(&__heap_bitmap_end-&__heap_bitmap_start)/_4KB_PG_SIZE,KERNEL_PERSIST, Kimage_regist_flags);
    if(status != OS_SUCCESS) return status;
    
    status=pages_state_set((phyaddr_t)&_heap_lma,(&__heap_end-&__heap_start)/_4KB_PG_SIZE,KERNEL_PERSIST, Kimage_regist_flags);
    if(status != OS_SUCCESS) return status;
    
    status=pages_state_set((phyaddr_t)&_klog_lma,(&__klog_end-&__klog_start)/_4KB_PG_SIZE,KERNEL_PERSIST, Kimage_regist_flags);
    if(status != OS_SUCCESS) return status;

    status=pages_state_set((phyaddr_t)&_kspace_uppdpt_lma,(&__kspace_uppdpt_end-&__kspace_uppdpt_start)/_4KB_PG_SIZE,KERNEL_PERSIST, Kimage_regist_flags);
    return status;
#endif
#ifdef USER_MODE
    //这里要通过读取kernel.elf的段信息来模仿内核态行为
    const char* elf_path = "/home/pangsong/PS_git/OS_pj_uefi/kernel/kernel.elf";
    int fd = open(elf_path, O_RDONLY);
    if (fd < 0) {
        // 如果找不到kernel.elf，使用默认初始化
        return OS_SUCCESS;
    }

    // 获取文件大小
    struct stat sb;
    if (fstat(fd, &sb) < 0) {
        close(fd);
        return OS_SUCCESS;
    }
    
    // 映射文件到内存
    void* elf_mapped = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (elf_mapped == MAP_FAILED) {
        close(fd);
        return OS_SUCCESS;
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
        return OS_SUCCESS;
    }

    // 获取程序头表
    Elf64_Phdr* phdr = (Elf64_Phdr*)((char*)elf_mapped + ehdr->e_phoff);

    // 遍历程序头表，模拟内核段注册
    pages_state_set_flags_t Kimage_regist_flags = {.op=pages_state_set_flags_t::normal,.params={.if_init_ref_count=1,.if_mmio=0}};
    
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD && phdr[i].p_memsz > 0) {
            // 计算页数
            uint64_t page_count = (phdr[i].p_memsz + _4KB_PG_SIZE - 1) / _4KB_PG_SIZE;
            
            // 根据段的虚拟地址和类型来确定内存段类型
            page_state_t page_type = KERNEL_PERSIST; // 默认为内核持久段
            
            // 使用pages_state_set注册内存段
            status = pages_state_set(phdr[i].p_paddr, page_count, page_type, Kimage_regist_flags);
            if(status != OS_SUCCESS) {
                munmap(elf_mapped, sb.st_size);
                close(fd);
                return status;
            }
        }
    }

    // 清理资源
    munmap(elf_mapped, sb.st_size);
    close(fd);
    return OS_SUCCESS;
#endif
}
#ifdef USER_MODE
phymemspace_mgr::phymemspace_mgr()
{
}
const char* page_state_to_string(phymemspace_mgr::page_state_t state) {
    switch (state) {
        case phymemspace_mgr::RESERVED: return "RESERVED";
        case phymemspace_mgr::FREE: return "FREE";
        case phymemspace_mgr::PARTIAL: return "PARTIAL";
        case phymemspace_mgr::FULL: return "FULL";
        case phymemspace_mgr::MMIO_FREE: return "MMIO_FREE";
        case phymemspace_mgr::KERNEL: return "KERNEL";
        case phymemspace_mgr::KERNEL_PERSIST: return "KERNEL_PERSIST";
        case phymemspace_mgr::UEFI_RUNTIME: return "UEFI_RUNTIME";
        case phymemspace_mgr::ACPI_TABLES: return "ACPI_TABLES";
        case phymemspace_mgr::ACPI_NVS: return "ACPI_NVS";
        case phymemspace_mgr::USER_FILE: return "USER_FILE";
        case phymemspace_mgr::USER_ANONYMOUS: return "USER_ANONYMOUS";
        case phymemspace_mgr::DMA: return "DMA";
        case phymemspace_mgr::MMIO: return "MMIO";
        case phymemspace_mgr::LOW1MB: return "LOW1MB";
        default: return "UNKNOWN";
    }
}

int phymemspace_mgr::print_all_atom_table()
{
    if (!top_1gb_table) {
        printf("top_1gb_table is null\n");
        return 0;
    }

    printf("Printing all atom table entries:\n");
    
    // 遍历稀疏表的所有项
    for (uint32_t i = 0; i < MAX_PHYADDR_1GB_PGS_COUNT; i++) {
        page_size1gb_t *pg_1gb = top_1gb_table->get(i);
        if (pg_1gb) {
            printf("1GB Page Table Entry [%u]:\n", i);
            printf("  Base Address: 0x%lx00000000\n", (uint64_t)i);
            printf("  State: %s\n", page_state_to_string(static_cast<page_state_t>(pg_1gb->flags.state)));
            printf("  Sub-table valid: %s\n", pg_1gb->flags.is_sub_valid ? "true" : "false");
            printf("  Ref Count: %u\n", pg_1gb->ref_count);
            printf("  Map Count: %u\n", pg_1gb->map_count);
            
            if (pg_1gb->flags.is_sub_valid) {
                printf("  2MB Sub-table:\n");
                for (uint16_t j = 0; j < 512; j++) {
                    page_size2mb_t &pg_2mb = pg_1gb->sub2mbpages[j];
                    if (&pg_2mb) { // 检查是否存在
                        printf("    2MB Page Entry [%u][%u]:\n", i, j);
                        printf("      State: %s\n", page_state_to_string(static_cast<page_state_t>(pg_2mb.flags.state)));
                        printf("      Sub-table valid: %s\n", pg_2mb.flags.is_sub_valid ? "true" : "false");
                        printf("      Ref Count: %u\n", pg_2mb.ref_count);
                        printf("      Map Count: %u\n", pg_2mb.map_count);
                        
                        if (pg_2mb.flags.is_sub_valid) {
                            printf("      4KB Sub-table:\n");
                            for (uint16_t k = 0; k < 512; k++) {
                                page_size4kb_t &pg_4kb = pg_2mb.sub_pages[k];
                                if (&pg_4kb) { // 检查是否存在
                                    printf("        4KB Page Entry [%u][%u][%u]:\n", i, j, k);
                                    printf("          State: %s\n", page_state_to_string(static_cast<page_state_t>(pg_4kb.flags.state)));
                                    printf("          Ref Count: %u\n", pg_4kb.ref_count);
                                    printf("          Map Count: %u\n", pg_4kb.map_count);
                                }
                            }
                        }
                    }
                }
            }
            printf("\n");
        }
    }
    
    return 0;
}
int phymemspace_mgr::print_allseg()
{
    if (!physeg_list) {
        printf("physeg_list is null\n");
        return 0;
    }

    printf("Printing all segments:\n");
    printf("Total segments: %zu\n", physeg_list->size());
    
    int idx = 0;
    for (auto it = physeg_list->begin(); it != physeg_list->end(); ++it, ++idx) {
        PHYSEG& seg = *it;
        printf("Segment [%d]:\n", idx);
        printf("  Base: 0x%lx\n", seg.base);
        printf("  Size: 0x%lx bytes (%.2f MB)\n", seg.seg_size, seg.seg_size / (1024.0 * 1024.0));
        printf("  Type: ");
        switch(seg.type) {
            case DRAM_SEG: printf("DRAM_SEG\n"); break;
            case FIRMWARE_RESERVED_SEG: printf("FIRMWARE_RESERVED_SEG\n"); break;
            case RESERVED_SEG: printf("RESERVED_SEG\n"); break;
            case MMIO_SEG: printf("MMIO_SEG\n"); break;
            case LOW1MB_SEG: printf("LOW1MB_SEG\n"); break;
            default: printf("Unknown(%d)\n", seg.type); break;
        }
        printf("  Flags: 0x%lx\n", seg.flags);
        printf("  Statistics:\n");
        printf("    Total Pages: %lu\n", seg.statistics.total_pages);
        printf("    MMIO: %lu pages (%lu bytes)\n", seg.statistics.mmio, seg.statistics.mmio * 4096);
        printf("    Kernel: %lu pages (%lu bytes)\n", seg.statistics.kernel, seg.statistics.kernel * 4096);
        printf("    Kernel Persist: %lu pages (%lu bytes)\n", seg.statistics.kernel_persisit, seg.statistics.kernel_persisit * 4096);
        printf("    User File: %lu pages (%lu bytes)\n", seg.statistics.user_file, seg.statistics.user_file * 4096);
        printf("    User Anonymous: %lu pages (%lu bytes)\n", seg.statistics.user_anonymous, seg.statistics.user_anonymous * 4096);
        printf("    DMA: %lu pages (%lu bytes)\n", seg.statistics.dma, seg.statistics.dma * 4096);
        printf("\n");
    }
    
    return 0;
}
#endif
// ------------------------
// 仅用于 pages_recycle 的回收前校验
// ------------------------
int phymemspace_mgr::pages_recycle_verify(
    phyaddr_t phybase,
    uint64_t num_of_4kbpgs,
    page_state_t& state
) {
    if (num_of_4kbpgs == 0) return OS_INVALID_PARAMETER;
    if (phybase & (_4KB_PG_SIZE - 1)) return OS_INVALID_PARAMETER;

    uint64_t idx1, idx2, idx4;
    phy_to_indices(phybase, idx1, idx2, idx4);

    // 构造 atom_page_ptr 自动折叠到正确原子页
    atom_page_ptr cur(idx1, idx2, idx4);

    // 第一页决定类型
    page_state_t orig_state = FREE;
    bool state_initialized = false;

    uint64_t checked = 0;

    while (checked < num_of_4kbpgs) {

        if (cur.page_size == _4KB_PG_SIZE) {

            auto *p4 = reinterpret_cast<page_size4kb_t*>(cur.page_strut_ptr);
            if (!p4) return OS_INVALID_ADDRESS;

            if (!state_initialized) {
                orig_state = p4->flags.state;
                state_initialized = true;

                // 必须属于可回收类型
                switch (orig_state) {
                    case KERNEL:
                    case USER_FILE:
                    case USER_ANONYMOUS:
                    case DMA:
                        break;
                    default:
                        return OS_PAGE_STATE_ERROR;
                }
            }

            if (p4->flags.state != orig_state)
                return OS_PAGE_STATE_ERROR;

            if (p4->ref_count != 1)
                return OS_PAGE_REFCOUNT_NONZERO;

            if (p4->map_count != 0)
                return OS_PAGE_MAPCOUNT_NONZERO;

            checked++;
            if (cur.the_next() != OS_SUCCESS) break;
            continue;
        }


        // -----------------------------------------------------
        // 2MB 原子页
        // -----------------------------------------------------
        if (cur.page_size == _2MB_PG_SIZE) {

            auto *p2 = reinterpret_cast<page_size2mb_t*>(cur.page_strut_ptr);
            if (!p2) return OS_INVALID_ADDRESS;

            if (!state_initialized) {
                orig_state = p2->flags.state;
                state_initialized = true;

                switch (orig_state) {
                    case KERNEL:
                    case USER_FILE:
                    case USER_ANONYMOUS:
                    case DMA:
                        break;
                    default:
                        return OS_PAGE_STATE_ERROR;
                }
            }

            if (p2->flags.state != orig_state)
                return OS_PAGE_STATE_ERROR;

            if (p2->ref_count != 1)
                return OS_PAGE_REFCOUNT_NONZERO;

            if (p2->map_count != 0)
                return OS_PAGE_MAPCOUNT_NONZERO;

            // 原子 2MB 页必须整块被回收
            if (num_of_4kbpgs - checked < 512)
                return OS_INVALID_PARAMETER;

            checked += 512;

            if (cur.the_next() != OS_SUCCESS) break;
            continue;
        }


        // -----------------------------------------------------
        // 1GB 原子页
        // -----------------------------------------------------
        if (cur.page_size == _1GB_PG_SIZE) {

            auto *p1 = reinterpret_cast<page_size1gb_t*>(cur.page_strut_ptr);
            if (!p1) return OS_INVALID_ADDRESS;

            if (!state_initialized) {
                orig_state = p1->flags.state;
                state_initialized = true;

                switch (orig_state) {
                    case KERNEL:
                    case USER_FILE:
                    case USER_ANONYMOUS:
                    case DMA:
                        break;
                    default:
                        return OS_PAGE_STATE_ERROR;
                }
            }

            if (p1->flags.state != orig_state)
                return OS_PAGE_STATE_ERROR;

            if (p1->ref_count != 1)
                return OS_PAGE_REFCOUNT_NONZERO;

            if (p1->map_count != 0)
                return OS_PAGE_MAPCOUNT_NONZERO;

            // 原子 1GB 必须整块被回收
            const uint64_t block = 512ULL * 512ULL;

            if (num_of_4kbpgs - checked < block)
                return OS_INVALID_PARAMETER;

            checked += block;

            if (cur.the_next() != OS_SUCCESS) break;
            continue;
        }

        // 不可能到这
        return OS_INVALID_ADDRESS;
    }
    
    // 全部通过
    state=orig_state;
    return OS_SUCCESS;
}
int phymemspace_mgr::PHYSEG_LIST_ITEM::get_seg_by_addr(phyaddr_t addr, PHYSEG &seg)
{
    if(m_head==nullptr&&m_tail==nullptr)
    return OS_NOT_EXIST;
    for(node*cur=m_head;cur!=nullptr;cur=cur->next){
        if(cur->value.base<=addr&&(addr<cur->value.base+cur->value.seg_size)){
            seg=cur->value;
            return OS_SUCCESS;
        }
    }
    return OS_NOT_EXIST;
}
phymemspace_mgr::PHYSEG phymemspace_mgr::get_physeg_by_addr(phyaddr_t addr)
{
    PHYSEG result=NULL_SEG;
    physeg_list->get_seg_by_addr(addr,result);
    return result;
}
phymemspace_mgr::phymemmgr_statistics_t phymemspace_mgr::get_statisit_copy()
{
    return statisitcs;
}