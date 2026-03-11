#include "memory/phygpsmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#include "memory/kpoolmemmgr.h"
#include "linker_symbols.h"
#include "memory/phyaddr_accessor.h"
#include "util/kout.h"
#include "panic.h"
#ifdef KERNEL_MODE
#include "util/kptrace.h"
#endif
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
    idx_1gb = (off>>30)&((1<<18)-1);
    idx_2mb = (off>>21)&511;
    idx_4kb = (off>>12)&511;
}
KURD_t phymemspace_mgr::default_kurd()
{
    return KURD_t(0,0,module_code::MEMORY,MEMMODULE_LOCAIONS::LOCATION_CODE_PHYMEMSPACE_MGR,0,0,err_domain::CORE_MODULE);
}

KURD_t phymemspace_mgr::default_success()
{
    KURD_t kurd=default_kurd();
    kurd.result=result_code::SUCCESS;
    kurd.level=level_code::INFO;
    return kurd;
}
KURD_t phymemspace_mgr::default_failure()
{
    KURD_t kurd=default_kurd();
    kurd=set_result_fail_and_error_level(kurd);
    return kurd;
}
KURD_t phymemspace_mgr::default_fatal()
{
    KURD_t kurd=default_kurd();
    kurd=set_fatal_result_level(kurd);
    return kurd;
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



KURD_t phymemspace_mgr::pages_recycle(phyaddr_t phybase, uint64_t numof_4kbpgs)
{
    KURD_t fail=default_failure();
    fail.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_RECYCLE;
    module_global_lock.lock();
    PHYSEG seg=get_physeg_by_addr(phybase);
    if(seg.type!=DRAM_SEG){
        module_global_lock.unlock();
        fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_RESULTS_CODE::FAIL_REASONS::REASON_CODE_DRAMSEG_NOT_EXIST;
    }
    page_state_t to_del_page_state;
    KURD_t status=pages_recycle_verify(phybase,numof_4kbpgs,to_del_page_state);
    if(status.result!=result_code::SUCCESS){module_global_lock.unlock();return status;}
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

KURD_t phymemspace_mgr::pages_mmio_unregist(phyaddr_t phybase, uint64_t numof_4kbpgs)
{
    module_global_lock.lock();
    PHYSEG seg=get_physeg_by_addr(phybase);
    if(seg.type!=MMIO_SEG){
        module_global_lock.unlock();
        KURD_t fail=default_failure();
        fail.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_MMIO_UNREGIST;
        fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::MMIO_UNREGIST_RESULTS_CODE::FAIL_REASONS::REASON_CODE_MMIOSEG_NOT_EXIST;
        return fail;
    }
    pages_state_set_flags_t flags={
        .op=pages_state_set_flags_t::normal,
        .params={
            .if_init_ref_count=0,
            .if_mmio=1
        }
    };
    KURD_t status=pages_state_set(phybase,numof_4kbpgs,MMIO_FREE,flags);
    module_global_lock.unlock();
    return status;
}
void phymemspace_mgr::in_module_panic(KURD_t kurd)
{
    panic_info_inshort inshort={
        .is_bug=1,
        .is_policy=0,
        .is_hw_fault=0,
        .is_mem_corruption=0,
        .is_escalated=0
    };
    Panic::panic(
        default_panic_behaviors_flags,
        "Panic in phymemspace_mgr",
        nullptr,
        &inshort,
        kurd
    );
}
KURD_t phymemspace_mgr::Init(init_to_kernel_info*info)
{
    subtb_alloc_is_pool_way_flag=true;
    KURD_t status=KURD_t();   
    KURD_t fail=default_failure(); 
    KURD_t success=default_success();
    fail.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_INIT;
    success.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_INIT;
    
    // 初始化基本数据结构
    physeg_list=new PHYSEG_LIST_ITEM();
    top_1gb_table=new Ktemplats::sparse_table_2level_no_OBJCONTENT<uint32_t,page_size1gb_t,__builtin_ctz(MAX_PHYADDR_1GB_PGS_COUNT)-9,9>;
    low1mb_mgr=new low1mb_mgr_t();
    
    // ============================================
    // 第一步：根据 memory_map 注册所有内存段
    // ============================================
    for(uint64_t i = 0; i < info->phymem_segment_count; i++) {
        phymem_segment& seg = info->memory_map[i];
        
        // 跳过无效段或低端 1MB（由后续专门处理）
        if(seg.start < 0x100000) continue;
        
        seg_type_t seg_state;
        switch(seg.type) {
            case freeSystemRam:
            case OS_ALLOCATABLE_MEMORY:
                seg_state = DRAM_SEG;
                break;
            case EFI_RUNTIME_SERVICES_CODE:
            case EFI_RUNTIME_SERVICES_DATA:
            case EFI_ACPI_RECLAIM_MEMORY:
            case EFI_ACPI_MEMORY_NVS:
            case OS_KERNEL_CODE:
            case OS_KERNEL_DATA:
            case OS_KERNEL_STACK:
            case OS_RESERVED_MEMORY:
                seg_state = RESERVED_SEG;
                break;
            case EFI_MEMORY_MAPPED_IO:
            case EFI_MEMORY_MAPPED_IO_PORT_SPACE:
            case OS_HARDWARE_GRAPHIC_BUFFER:
                seg_state = MMIO_SEG;
                break;
            case EFI_RESERVED_MEMORY_TYPE:
            case EFI_UNUSABLE_MEMORY:
            case EFI_PERSISTENT_MEMORY:
            case EFI_UNACCEPTED_MEMORY_TYPE:
                seg_state = RESERVED_SEG;
                break;
            default:
                seg_state = RESERVED_SEG;
                break;
        }
        
        blackhole_acclaim_flags_t flags = {
            .a = 0
        };
        status = blackhole_acclaim(seg.start, seg.size / _4KB_PG_SIZE, seg_state, flags);
        if(status.result != result_code::SUCCESS) {
            kio::bsp_kout << "[WARN] Failed to acclaim segment at " 
                          << (void*)seg.start << ", size: " << seg.size 
                          << ", type: " << static_cast<uint32_t>(seg.type) 
                          << ", error: " << status.result << kio::kendl;
            continue;
        }
    }
    
    // ============================================
    // 第二步：注册低端 1MB 为 LOW1MB
    // ============================================
    pages_state_set_flags_t low1mb_pgs_set = {
        .op = pages_state_set_flags_t::normal,
        .params = {
            .if_init_ref_count = 1,
            .if_mmio = 0
        }
    };
    status = pages_state_set(0, 256, LOW1MB, low1mb_pgs_set);
    if(!success_all_kurd(status)) return status;
    
    // ============================================
    // 第三步：根据 loaded_VM_intervals 注册内核加载段
    // 需要判断物理地址是否低于 1MB 来选择合适的状态
    // ============================================
    constexpr phyaddr_t LOW1MB_THRESHOLD = 0x100000; // 1MB
    
    for(uint64_t i = 0; i < info->loaded_VM_interval_count; i++) {
        loaded_VM_interval& vm_interval = info->loaded_VM_intervals[i];
        
        // 获取对应的物理段
        PHYSEG* seg = physeg_list->get_seg_by_addr(vm_interval.pbase, status);
        if(!success_all_kurd(status) || !seg) {
            kio::bsp_kout << "[WARN] Failed to find segment for VM interval at " 
                          << (void*)vm_interval.pbase << kio::kendl;
            continue;
        }
        
        // 计算页数
        uint64_t page_count = vm_interval.size / _4KB_PG_SIZE;
        
        // 根据物理地址位置决定状态
        page_state_t target_state;
        if(vm_interval.pbase < LOW1MB_THRESHOLD) {
            // 低于 1MB 的物理地址，使用 LOW1MB 状态
            target_state = LOW1MB;
        } else {
            // 高于 1MB 的物理地址，使用 KERNEL_PERSIST 状态
            target_state = KERNEL_PERSIST;
        }
        
        dram_pages_state_set_flags_t dram_set = {
            .state = target_state,
            .op = dram_pages_state_set_flags_t::normal,
            .params = {
                .if_init_ref_count = 1,
            }
        };
        
        status = dram_pages_state_set(*seg, vm_interval.pbase, page_count, dram_set);
        if(!success_all_kurd(status)) {
            kio::bsp_kout << "[WARN] Failed to set state for VM interval at " 
                          << (void*)vm_interval.pbase << ", pages: " << page_count 
                          << ", state: " << static_cast<uint32_t>(target_state)
                          << ", error: " << status.result << kio::kendl;
            continue;
        }
        
        // 更新统计信息
        if(target_state == KERNEL_PERSIST) {
            seg->statistics.kernel_persisit += page_count;
            statisitcs.kernel_persisit += page_count;
        }
    }
    
    // ============================================
    // 第四步：特殊处理 - 注册 info 自身和 kmmu_interval 为 KERNEL 状态
    // ============================================
    
    // 4.1 注册 info 自身所在的页框为 KERNEL 状态（后续可能回收）
    phyaddr_t info_phy_addr = reinterpret_cast<phyaddr_t>(info);
    // 确保对齐到 4KB
    info_phy_addr &= ~(_4KB_PG_SIZE - 1);
    
    PHYSEG* info_seg = physeg_list->get_seg_by_addr(info_phy_addr, status);
    if(success_all_kurd(status) && info_seg) {
        dram_pages_state_set_flags_t info_set = {
            .state = KERNEL,
            .op = dram_pages_state_set_flags_t::normal,
            .params = {
                .if_init_ref_count = 1,
            }
        };
        status = dram_pages_state_set(*info_seg, info_phy_addr, 1, info_set);
        if(success_all_kurd(status)) {
            kio::bsp_kout << "[INFO] Registered info structure at " 
                          << (void*)info_phy_addr << " as KERNEL state" << kio::kendl;
        }
    }
    
    // 4.2 注册 kmmu_interval 为 KERNEL 状态（后续可能回收）
    if(info->kmmu_interval.size > 0) {
        phyaddr_t kmmu_base = info->kmmu_interval.start;
        uint64_t kmmu_pages = info->kmmu_interval.size / _4KB_PG_SIZE;
        
        PHYSEG* kmmu_seg = physeg_list->get_seg_by_addr(kmmu_base, status);
        if(success_all_kurd(status) && kmmu_seg) {
            dram_pages_state_set_flags_t kmmu_set = {
                .state = KERNEL,
                .op = dram_pages_state_set_flags_t::normal,
                .params = {
                    .if_init_ref_count = 1,
                }
            };
            status = dram_pages_state_set(*kmmu_seg, kmmu_base, kmmu_pages, kmmu_set);
            if(success_all_kurd(status)) {
                kio::bsp_kout << "[INFO] Registered kmmu_interval at " 
                              << (void*)kmmu_base << ", pages: " << kmmu_pages 
                              << " as KERNEL state" << kio::kendl;
            }
        }
    }
    
    return success;
}

const char* page_state_to_string(page_state_t state) {
    switch (state) {
        case RESERVED: return "RESERVED";
        case FREE: return "FREE";
        case NOT_ATOM: return "NOT_ATOM";
        case FULL: return "FULL";
        case MMIO_FREE: return "MMIO_FREE";
        case KERNEL: return "KERNEL";
        case KERNEL_PERSIST: return "KERNEL_PERSIST";
        case UEFI_RUNTIME: return "UEFI_RUNTIME";
        case ACPI_TABLES: return "ACPI_TABLES";
        case ACPI_NVS: return "ACPI_NVS";
        case USER_FILE: return "USER_FILE";
        case USER_ANONYMOUS: return "USER_ANONYMOUS";
        case DMA: return "DMA";
        case MMIO: return "MMIO";
        case LOW1MB: return "LOW1MB";
        default: return "UNKNOWN";
    }
}

int phymemspace_mgr::print_all_atom_table()
{
    if (!top_1gb_table) {
        kio::bsp_kout << "top_1gb_table is null" << kio::kendl;
        return 0;
    }

    kio::bsp_kout << "Printing all atom table entries:" << kio::kendl;
    
    // 遍历稀疏表的所有项
    for (uint32_t i = 0; i < MAX_PHYADDR_1GB_PGS_COUNT; i++) {
        page_size1gb_t *pg_1gb = top_1gb_table->get(i);
        if (pg_1gb) {
            kio::bsp_kout << "1GB Page Table Entry [" << i << "]:" << kio::kendl;
            kio::bsp_kout << "  Base Address: 0x";
            kio::bsp_kout.shift_hex();
            kio::bsp_kout << (uint64_t)i << "00000000" << kio::kendl;
            kio::bsp_kout << "  State: " << page_state_to_string(static_cast<page_state_t>(pg_1gb->flags.state)) << kio::kendl;
            kio::bsp_kout << "  Sub-table valid: " << (pg_1gb->flags.is_sub_valid ? "true" : "false") << kio::kendl;
            kio::bsp_kout << "  Ref Count: ";
            kio::bsp_kout.shift_dec();
            kio::bsp_kout << pg_1gb->ref_count << kio::kendl;
            kio::bsp_kout << "  Map Count: " << pg_1gb->map_count << kio::kendl;
            
            if (pg_1gb->flags.is_sub_valid) {
                kio::bsp_kout << "  2MB Sub-table:" << kio::kendl;
                for (uint16_t j = 0; j < 512; j++) {
                    page_size2mb_t &pg_2mb = pg_1gb->sub2mbpages[j];
                    if (&pg_2mb) { // 检查是否存在
                        kio::bsp_kout << "    2MB Page Entry [" << i << "][" << j << "]:" << kio::kendl;
                        kio::bsp_kout << "      State: " << page_state_to_string(static_cast<page_state_t>(pg_2mb.flags.state)) << kio::kendl;
                        kio::bsp_kout << "      Sub-table valid: " << (pg_2mb.flags.is_sub_valid ? "true" : "false") << kio::kendl;
                        kio::bsp_kout << "      Ref Count: ";
                        kio::bsp_kout.shift_dec();
                        kio::bsp_kout << pg_2mb.ref_count << kio::kendl;
                        kio::bsp_kout << "      Map Count: " << pg_2mb.map_count << kio::kendl;
                        
                        if (pg_2mb.flags.is_sub_valid) {
                            kio::bsp_kout << "      4KB Sub-table:" << kio::kendl;
                            for (uint16_t k = 0; k < 512; k++) {
                                page_size4kb_t &pg_4kb = pg_2mb.sub_pages[k];
                                if (&pg_4kb) { // 检查是否存在
                                    kio::bsp_kout << "        4KB Page Entry [" << i << "][" << j << "][" << k << "]:" << kio::kendl;
                                    kio::bsp_kout << "          State: " << page_state_to_string(static_cast<page_state_t>(pg_4kb.flags.state)) << kio::kendl;
                                    kio::bsp_kout << "          Ref Count: ";
                                    kio::bsp_kout.shift_dec();
                                    kio::bsp_kout << pg_4kb.ref_count << kio::kendl;
                                    kio::bsp_kout << "          Map Count: " << pg_4kb.map_count << kio::kendl;
                                }
                            }
                        }
                    }
                }
            }
            kio::bsp_kout << kio::kendl;
        }
    }
    
    return 0;
}
int phymemspace_mgr::print_allseg()
{
    if (!physeg_list) {
        kio::bsp_kout << "physeg_list is null" << kio::kendl;
        return 0;
    }

    kio::bsp_kout << "Printing all segments:" << kio::kendl;
    kio::bsp_kout << "Total segments: ";
    kio::bsp_kout.shift_dec();
    kio::bsp_kout << physeg_list->size() << kio::kendl;
    
    int idx = 0;
    for (auto it = physeg_list->begin(); it != physeg_list->end(); ++it, ++idx) {
        PHYSEG& seg = *it;
        kio::bsp_kout << "Segment [" << idx << "]:" << kio::kendl;
        kio::bsp_kout << "  Base: 0x";
        kio::bsp_kout.shift_hex();
        kio::bsp_kout << seg.base << kio::kendl;
        kio::bsp_kout << "  Size: 0x";
        kio::bsp_kout.shift_hex();
        kio::bsp_kout << seg.seg_size << " bytes (";
        kio::bsp_kout.shift_dec();
        kio::bsp_kout << (seg.seg_size / (1024 * 1024)) << " MB)" << kio::kendl;
        kio::bsp_kout << "  Type: ";
        switch(seg.type) {
            case DRAM_SEG: kio::bsp_kout << "DRAM_SEG" << kio::kendl; break;
            case FIRMWARE_RESERVED_SEG: kio::bsp_kout << "FIRMWARE_RESERVED_SEG" << kio::kendl; break;
            case RESERVED_SEG: kio::bsp_kout << "RESERVED_SEG" << kio::kendl; break;
            case MMIO_SEG: kio::bsp_kout << "MMIO_SEG" << kio::kendl; break;
            case LOW1MB_SEG: kio::bsp_kout << "LOW1MB_SEG" << kio::kendl; break;
            default: kio::bsp_kout << "Unknown(";
                     kio::bsp_kout.shift_dec();
                     kio::bsp_kout << seg.type << ")" << kio::kendl; break;
        }
        kio::bsp_kout << "  Flags: 0x";
        kio::bsp_kout.shift_hex();
        kio::bsp_kout << seg.flags << kio::kendl;
        kio::bsp_kout << "  Statistics:" << kio::kendl;
        kio::bsp_kout << "    Total Pages: ";
        kio::bsp_kout.shift_dec();
        kio::bsp_kout << seg.statistics.total_pages << kio::kendl;
        kio::bsp_kout << "    MMIO: " << seg.statistics.mmio << " pages (" << seg.statistics.mmio * 4096 << " bytes)" << kio::kendl;
        kio::bsp_kout << "    Kernel: " << seg.statistics.kernel << " pages (" << seg.statistics.kernel * 4096 << " bytes)" << kio::kendl;
        kio::bsp_kout << "    Kernel Persist: " << seg.statistics.kernel_persisit << " pages (" << seg.statistics.kernel_persisit * 4096 << " bytes)" << kio::kendl;
        kio::bsp_kout << "    User File: " << seg.statistics.user_file << " pages (" << seg.statistics.user_file * 4096 << " bytes)" << kio::kendl;
        kio::bsp_kout << "    User Anonymous: " << seg.statistics.user_anonymous << " pages (" << seg.statistics.user_anonymous * 4096 << " bytes)" << kio::kendl;
        kio::bsp_kout << "    DMA: " << seg.statistics.dma << " pages (" << seg.statistics.dma * 4096 << " bytes)" << kio::kendl;
        kio::bsp_kout << kio::kendl;
    }
    
    return 0;
}

phymemspace_mgr::free_segs_t *phymemspace_mgr::free_segs_get()
{
    free_segs_t* out = new free_segs_t();
    out->count = 0;
    out->entries = nullptr;
    module_global_lock.lock();

    auto idx_to_phyaddr = [](uint32_t idx1, uint16_t idx2, uint16_t idx4) -> phyaddr_t {
        return ((static_cast<uint64_t>(idx1) * 512ULL * 512ULL) +
                (static_cast<uint64_t>(idx2) * 512ULL) +
                static_cast<uint64_t>(idx4)) << 12;
    };

    auto scan_free_atomic_runs = [&](bool fill_entries, free_segs_t::entry_t* entries, uint64_t& out_count) {
        out_count = 0;
        if (!physeg_list || !top_1gb_table) return;

        for (auto it = physeg_list->begin(); it != physeg_list->end(); ++it) {
            PHYSEG& seg = *it;
            if (seg.type != DRAM_SEG || seg.seg_size == 0) continue;

            phyaddr_t seg_begin = seg.base;
            phyaddr_t seg_end = seg.base + seg.seg_size;

            uint64_t idx1 = 0, idx2 = 0, idx4 = 0;
            phy_to_indices(seg_begin, idx1, idx2, idx4);
            atom_page_ptr cur(static_cast<uint32_t>(idx1), static_cast<uint16_t>(idx2), static_cast<uint16_t>(idx4));

            bool has_run = false;
            phyaddr_t run_base = 0;
            uint64_t run_size = 0;

            auto flush_run = [&]() {
                if (!has_run || run_size == 0) return;
                if (fill_entries) {
                    entries[out_count].base = run_base;
                    entries[out_count].size = run_size;
                }
                out_count++;
                has_run = false;
                run_size = 0;
            };

            while (true) {
                if (cur.page_size != _4KB_PG_SIZE &&
                    cur.page_size != _2MB_PG_SIZE &&
                    cur.page_size != _1GB_PG_SIZE) {
                    flush_run();
                    break;
                }

                phyaddr_t atom_base = idx_to_phyaddr(cur._1gbtb_idx, cur._2mbtb_offestidx, cur._4kb_offestidx);
                if (atom_base >= seg_end) {
                    flush_run();
                    break;
                }

                uint64_t atom_size = cur.page_size;
                phyaddr_t atom_end = atom_base + atom_size;
                bool is_free_atomic_page = false;

                if (atom_base >= seg_begin && atom_end <= seg_end) {
                    if (cur.page_size == _4KB_PG_SIZE) {
                        auto* p4 = reinterpret_cast<page_size4kb_t*>(cur.page_strut_ptr);
                        is_free_atomic_page = p4 && (p4->flags.state == FREE) && !p4->flags.is_belonged_to_buddy;
                    } else if (cur.page_size == _2MB_PG_SIZE) {
                        auto* p2 = reinterpret_cast<page_size2mb_t*>(cur.page_strut_ptr);
                        is_free_atomic_page = p2 && (p2->flags.state == FREE) && !p2->flags.is_belonged_to_buddy;
                    } else {
                        auto* p1 = reinterpret_cast<page_size1gb_t*>(cur.page_strut_ptr);
                        is_free_atomic_page = p1 && (p1->flags.state == FREE) && !p1->flags.is_belonged_to_buddy;
                    }
                }

                if (is_free_atomic_page) {
                    if (!has_run) {
                        has_run = true;
                        run_base = atom_base;
                        run_size = atom_size;
                    } else if (run_base + run_size == atom_base) {
                        run_size += atom_size;
                    } else {
                        flush_run();
                        has_run = true;
                        run_base = atom_base;
                        run_size = atom_size;
                    }
                } else {
                    flush_run();
                }

                if (cur.the_next() != OS_SUCCESS) {
                    flush_run();
                    break;
                }
            }
        }
    };

    uint64_t total_entries = 0;
    scan_free_atomic_runs(false, nullptr, total_entries);

    out->count = total_entries;
    if (total_entries > 0) {
        out->entries = new free_segs_t::entry_t[total_entries];
        uint64_t filled_entries = 0;
        scan_free_atomic_runs(true, out->entries, filled_entries);
        if (filled_entries < out->count) {
            out->count = filled_entries;
        }
    }

    module_global_lock.unlock();
    return out;
}
// ------------------------
// 仅用于 pages_recycle 的回收前校验
// ------------------------
KURD_t phymemspace_mgr::pages_recycle_verify(
    phyaddr_t phybase,
    uint64_t num_of_4kbpgs,
    page_state_t& state
) {
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    success.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_RECYCLE_VERIFY;
    fail.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_RECYCLE_VERIFY;
    fatal.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_RECYCLE_VERIFY;

    if (num_of_4kbpgs == 0) {
        fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::BAD_PARAM;
        return fail;
    }
    if (phybase & (_4KB_PG_SIZE - 1)) {
        fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::BAD_PARAM;
        return fail;
    }

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
            if (!p4) {
                fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FATAL_REASONS::REASON_CODE_PAGE_FRAME_FAIL_GET;
                return fatal;//fatal,对应页框结构失败
            }

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
                        fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_UNRECYCABLE_PAGE_STATE;
                        return fail;
                }
            }

            if (p4->flags.state != orig_state) {
                fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_PAGE_STATE_SHIFT;
                return fail;
            }

            if (p4->ref_count != 1) {
                fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_REF_COUNT_NET_ALLOW_RECYCLE;
                return fail;
            }

            if (p4->map_count != 0) {
                fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_MAP_COUNT_NOT_CLEAR;
                return fail;
            }

            if(p4->flags.is_sub_valid) {
                fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_4KBPAGE_SUBTABLE_BIT_VALID;
                return fail;//fatal,非法状态
            }
            checked++;
            if (cur.the_next() != OS_SUCCESS) break;
            continue;
        }


        // -----------------------------------------------------
        // 2MB 原子页
        // -----------------------------------------------------
        if (cur.page_size == _2MB_PG_SIZE) {

            auto *p2 = reinterpret_cast<page_size2mb_t*>(cur.page_strut_ptr);
            if (!p2) {
                fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FATAL_REASONS::REASON_CODE_PAGE_FRAME_FAIL_GET;
                return fatal;
            }

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
                        fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_UNRECYCABLE_PAGE_STATE;
                        return fail;
                }
            }

            if (p2->flags.state != orig_state) {
                fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_PAGE_STATE_SHIFT;
                return fail;
            }

            if (p2->ref_count != 1) {
                fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_REF_COUNT_NET_ALLOW_RECYCLE;
                return fail;
            }

            if (p2->map_count != 0) {
                fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_MAP_COUNT_NOT_CLEAR;
                return fail;
            }

            // 原子 2MB 页必须整块被回收
            if (num_of_4kbpgs - checked < 512) {
                fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::BAD_PARAM;
                return fail;
            }

            checked += 512;

            if (cur.the_next() != OS_SUCCESS) break;
            continue;
        }


        // -----------------------------------------------------
        // 1GB 原子页
        // -----------------------------------------------------
        if (cur.page_size == _1GB_PG_SIZE) {

            auto *p1 = reinterpret_cast<page_size1gb_t*>(cur.page_strut_ptr);
            if (!p1) {
                fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FATAL_REASONS::REASON_CODE_PAGE_FRAME_FAIL_GET;
                return fatal;
            }

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
                        fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_UNRECYCABLE_PAGE_STATE;
                        return fail;
                }
            }

            if (p1->flags.state != orig_state) {
                fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_PAGE_STATE_SHIFT;
                return fail;
            }

            if (p1->ref_count != 1) {
                fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_REF_COUNT_NET_ALLOW_RECYCLE;
                return fail;
            }

            if (p1->map_count != 0) {
                fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::REASON_CODE_MAP_COUNT_NOT_CLEAR;
                return fail;
            }
            // 原子 1GB 必须整块被回收
            const uint64_t block = 512ULL * 512ULL;

            if (num_of_4kbpgs - checked < block) {
                fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FAIL_REASONS::BAD_PARAM;
                return fail;
            }

            checked += block;

            if (cur.the_next() != OS_SUCCESS) break;
            continue;
        }

        // 不可能到这
        fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_VERIFY_RESULTS_CODE::FATAL_REASONS::REASON_CODE_UNREACHABLE_CODE;
        return fatal;
    }
    
    // 全部通过
    state=orig_state;
    return success;
}


phymemspace_mgr::phymemmgr_statistics_t phymemspace_mgr::get_statisit_copy()
{
    return statisitcs;
}
void phymemspace_mgr::subtb_alloc_shift_pages_way()
{
    subtb_alloc_is_pool_way_flag = false;
}
