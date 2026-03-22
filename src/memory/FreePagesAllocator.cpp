#include "memory/FreePagesAllocator.h"
#include "memory/all_pages_arr.h"
#include "firmware/ACPI_APIC.h"
#include "panic.h"
#include "util/kout.h"
#include "util/arch/x86-64/cpuid_intel.h"
#ifdef KERNEL_MODE
#include "memory/kpoolmemmgr.h"
#endif
#ifdef USER_MODE
#include "new"
#include <unistd.h>
#include <stdlib.h>
#endif
namespace {
static uint64_t fpa_get_cpu_count()
{
#ifdef KERNEL_MODE
    if (!gAnalyzer || !gAnalyzer->processor_x64_list) {
        return 1;
    }
    uint64_t count = gAnalyzer->processor_x64_list->size();
    return count == 0 ? 1 : count;
#endif
#ifdef USER_MODE
    long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_count <= 0) return 1;
    return static_cast<uint64_t>(cpu_count);
#endif
}

static void fpa_wrapped_pgs_vfree(void* addr, uint64_t pages)
{
#ifdef KERNEL_MODE
    __wrapped_pgs_vfree(addr, pages);
#endif
#ifdef USER_MODE
    (void)addr;
    size_t bytes = static_cast<size_t>(pages) * 4096;
    void* tmp = malloc(bytes ? bytes : 1);
    (void)tmp;
#endif
}

static void* fpa_wrapped_pgs_valloc(KURD_t* kurd, uint64_t pages, page_state_t state, uint8_t align_log2)
{
#ifdef KERNEL_MODE
    return __wrapped_pgs_valloc(kurd, pages, state, align_log2);
#endif
#ifdef USER_MODE
    (void)state;
    (void)align_log2;
    if (kurd) {
        *kurd = KURD_t();
    }
    size_t bytes = static_cast<size_t>(pages) * 4096;
    void* ptr = malloc(bytes ? bytes : 1);
    if (!ptr && kurd) {
        kurd->result = result_code::FAIL;
        kurd->level = level_code::ERROR;
    }
    return ptr;
#endif
}
}
FreePagesAllocator::flags_t FreePagesAllocator::flags;
uint64_t FreePagesAllocator::main_BCB_count;
FreePagesAllocator::BuddyControlBlock* FreePagesAllocator::mainBCBS;
uint64_t FreePagesAllocator::vice_BCB_count;
FreePagesAllocator::BuddyControlBlock* FreePagesAllocator::vice_BCBS;
fpa_stats* FreePagesAllocator::statistics_arr;
uint64_t*FreePagesAllocator::processors_preffered_bcb_idx;
namespace {
    struct BCB_plan_entry{
        phyaddr_t base;
        uint8_t order;
    };
    constexpr uint8_t min_main_bcb_order = 14;//64mb
    constexpr uint8_t min_vice_bcb_order = 10;//4mb
    Ktemplats::list_doubly<BCB_plan_entry> g_mainBCBs_plan_list;
    Ktemplats::list_doubly<BCB_plan_entry> g_viceBCBs_plan_list;
    uint64_t g_all_avaliable_mem_accumulate = 0;
}
KURD_t FreePagesAllocator::Init()
{
    flags.allow_new_BCB=false;
    g_mainBCBs_plan_list.clear();
    g_viceBCBs_plan_list.clear();
    g_all_avaliable_mem_accumulate = 0;

    all_pages_arr::free_segs_t* free_segs = all_pages_arr::free_segs_get();
    if(!free_segs){
        return set_fatal_result_level(KURD_t());
    }
    auto free_free_segs = [&]() {
        if (!free_segs) return;
        if (free_segs->entries) {
            delete[] free_segs->entries;
            free_segs->entries = nullptr;
        }
        delete free_segs;
        free_segs = nullptr;
    };
    if(free_segs->count!=0 && free_segs->entries==nullptr){
        free_free_segs();
        return set_fatal_result_level(KURD_t());
    }
    auto cur = free_segs->entries;
    for(uint64_t i=0;i<free_segs->count;++i){
        auto& seg = cur[i];

        uint64_t order_max=0;
        phyaddr_t base=seg.base;
        if(seg.base==0){
            order_max=0;
        }else{
            uint64_t tz=__builtin_ctzll(seg.base);
            order_max=(tz>12)?(tz-12):0;
        }
        const phyaddr_t end=seg.base+seg.size;
        g_all_avaliable_mem_accumulate+=seg.size;
        while(true){
            uint8_t order=order_max;
            phyaddr_t top=base+(1ull<<(order+12));
            while(top>end){
                if(order==0)break;
                --order;
                top=base+(1ull<<(order+12));
            }
            if(top==end){
                if(order>=min_main_bcb_order){
                    g_mainBCBs_plan_list.push_back(BCB_plan_entry{base,(uint8_t)order});
                }else if(order>=min_vice_bcb_order){
                    g_viceBCBs_plan_list.push_back(BCB_plan_entry{base,(uint8_t)order});
                }else{
                    //太小不用
                }
                break;
            }
            if(top<end){
                if(order>=min_main_bcb_order){
                    g_mainBCBs_plan_list.push_back(BCB_plan_entry{base,(uint8_t)order});
                }else if(order>=min_vice_bcb_order){
                    g_viceBCBs_plan_list.push_back(BCB_plan_entry{base,(uint8_t)order});
                }else{
                    //太小不用
                }
                base=top;
                if(top==0){
                    order_max=0;
                }else{
                    uint64_t tz=__builtin_ctzll(top);
                    order_max=(tz>12)?(tz-12):0;
                }
            }
        }
    }
    free_free_segs();

    if (g_mainBCBs_plan_list.empty()) {
        return set_fatal_result_level(KURD_t());
    }
    uint8_t min_order = 0xFF;
    phyaddr_t min_base = ~0ULL;
    for (auto it = g_mainBCBs_plan_list.begin(); it != g_mainBCBs_plan_list.end(); ++it) {
        const BCB_plan_entry plan = *it;
        if (plan.order < min_order || (plan.order == min_order && plan.base < min_base)) {
            min_order = plan.order;
            min_base = plan.base;
        }
    }
    if (min_order == 0xFF) {
        return set_fatal_result_level(KURD_t());
    }
    BCB_plan_entry first_plan{min_base, min_order};
    Ktemplats::list_doubly<BCB_plan_entry> filtered_main;
    bool removed = false;
    for (auto it = g_mainBCBs_plan_list.begin(); it != g_mainBCBs_plan_list.end(); ++it) {
        BCB_plan_entry plan = *it;
        if (!removed && plan.order == first_plan.order && plan.base == first_plan.base) {
            removed = true;
            continue;
        }
        filtered_main.push_back(plan);
    }
    g_mainBCBs_plan_list.clear();
    for (auto it = filtered_main.begin(); it != filtered_main.end(); ++it) {
        g_mainBCBs_plan_list.push_back(*it);
    }

    first_BCB = new BuddyControlBlock(first_plan.base, first_plan.order);
    first_BCB->first_bcb_specified_init();

    bsp_kout<<DEC<<"sizeof BuddyControlBlock: "<<sizeof(BuddyControlBlock)<<kendl;
    return KURD_t();
}
KURD_t FreePagesAllocator::second_stage(strategy_t strategy)
{
    auto default_kurd = []() -> KURD_t {
        return KURD_t(
            0, 0, module_code::MEMORY,
            MEMMODULE_LOCAIONS::LOCATION_CODE_FREEPAGES_ALLOCATOR,
            0, 0, err_domain::CORE_MODULE
        );
    };
    auto make_success = [&]() -> KURD_t {
        KURD_t kurd = default_kurd();
        kurd.result = result_code::SUCCESS;
        kurd.level = level_code::INFO;
        kurd.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::EVENT_CODE_INIT_SECOND_STAGE;
        return kurd;
    };
    auto make_fail = [&](uint16_t reason) -> KURD_t {
        KURD_t kurd = set_result_fail_and_error_level(default_kurd());
        kurd.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::EVENT_CODE_INIT_SECOND_STAGE;
        kurd.reason = reason;
        return kurd;
    };
    auto make_fatal = [&](uint16_t reason) -> KURD_t {
        KURD_t kurd = set_fatal_result_level(default_kurd());
        kurd.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::EVENT_CODE_INIT_SECOND_STAGE;
        kurd.reason = reason;
        return kurd;
    };
    auto get_processor_count = []() -> uint64_t {
        return fpa_get_cpu_count();
    };

    if (g_mainBCBs_plan_list.empty() && g_viceBCBs_plan_list.empty()) {
        return make_fatal(
            MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::INIT_SECOND_STAGE_RESULTS_CODE::FATAL_REASONS_CODE::FAIL_NO_AVALIABLE_MEM
        );
    }
    auto build_BCBs=[](
        Ktemplats::list_doubly<BCB_plan_entry>& mainBCBs_plan_list,
        Ktemplats::list_doubly<BCB_plan_entry>& viceBCBs_plan_list
    )->KURD_t{
        auto pages_for = [](uint64_t bytes) -> uint64_t {
            return bytes == 0 ? 0 : ((bytes + 4095) >> 12);
        };

        mainBCBS = nullptr;
        vice_BCBS = nullptr;
        main_BCB_count = mainBCBs_plan_list.size();
        vice_BCB_count = viceBCBs_plan_list.size();

        uint64_t main_pages = pages_for(main_BCB_count * sizeof(BuddyControlBlock));
        uint64_t vice_pages = pages_for(vice_BCB_count * sizeof(BuddyControlBlock));

        KURD_t kurd=KURD_t();
        if(main_pages != 0){
            mainBCBS=(BuddyControlBlock *)fpa_wrapped_pgs_valloc(
                &kurd,
                main_pages,
                page_state_t::kernel_pinned,
                12
            );
            if(error_kurd(kurd)){
                return kurd;
            }
        }
        if(vice_pages != 0){
            vice_BCBS=(BuddyControlBlock *)fpa_wrapped_pgs_valloc(
                &kurd,
                vice_pages,
                page_state_t::kernel_pinned,
                12
            );
            if(error_kurd(kurd)){
                if(main_pages != 0){
                    fpa_wrapped_pgs_vfree(mainBCBS, main_pages);
                    mainBCBS = nullptr;
                }
                return kurd;
            }
        }

        uint64_t main_constructed = 0;
        uint64_t vice_constructed = 0;
        uint64_t i=0;
        for(auto it=mainBCBs_plan_list.begin();it!=mainBCBs_plan_list.end();++it,++i){
            BCB_plan_entry plan=*it;
            new (mainBCBS + i) BuddyControlBlock(
                plan.base,
                plan.order
            );
            ++main_constructed;
            kurd=mainBCBS[i].second_stage_init();
            if(error_kurd(kurd)){
                for(uint64_t j=main_constructed;j>0;--j){
                    mainBCBS[j-1].~BuddyControlBlock();
                }
                if(vice_pages != 0){
                    fpa_wrapped_pgs_vfree(vice_BCBS, vice_pages);
                    vice_BCBS = nullptr;
                }
                if(main_pages != 0){
                    fpa_wrapped_pgs_vfree(mainBCBS, main_pages);
                    mainBCBS = nullptr;
                }
                return kurd;
            }
        }
        i=0;
        for(auto it=viceBCBs_plan_list.begin();it!=viceBCBs_plan_list.end();++it,++i){
            BCB_plan_entry plan=*it;
            new (vice_BCBS + i) BuddyControlBlock(
                plan.base,
                plan.order
            );
            ++vice_constructed;
            kurd=vice_BCBS[i].second_stage_init();
            if(error_kurd(kurd)){
                for(uint64_t j=vice_constructed;j>0;--j){
                    vice_BCBS[j-1].~BuddyControlBlock();
                }
                for(uint64_t j=main_constructed;j>0;--j){
                    mainBCBS[j-1].~BuddyControlBlock();
                }
                if(vice_pages != 0){
                    fpa_wrapped_pgs_vfree(vice_BCBS, vice_pages);
                    vice_BCBS = nullptr;
                }
                if(main_pages != 0){
                    fpa_wrapped_pgs_vfree(mainBCBS, main_pages);
                    mainBCBS = nullptr;
                }
                return kurd;
            }
        }
        return KURD_t();
    };

    KURD_t result;
    if(strategy.strategy==second_stage_init_strategy::INIT_STRATEGY_MATCH_THREAD){
        uint64_t thread_count=[]()->uint64_t{
            #ifdef __x86_64__
            return fpa_get_cpu_count();
            #endif

        }();
        if(strategy.thread_coefficient==0){
            return make_fail(
                MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::INIT_SECOND_STAGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_BAD_PARAM_MATCH_THREAD_BUT_BAD_ZERO_CONEFFICIENCY
            );
        }
        if(g_all_avaliable_mem_accumulate==0){
            KURD_t fatal = make_fatal(
                MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::INIT_SECOND_STAGE_RESULTS_CODE::FATAL_REASONS_CODE::FAIL_NO_AVALIABLE_MEM
            );
            panic_info_inshort info = {.is_bug=0, .is_policy=1, .is_hw_fault=0, .is_mem_corruption=0, .is_escalated=0};
            Panic::panic(default_panic_behaviors_flags,(char*)"FreePagesAllocator::second_stage no available memory",nullptr,&info,fatal);
            return fatal;
        }
        if(thread_count==0){
            KURD_t fatal = make_fatal(
                MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::INIT_SECOND_STAGE_RESULTS_CODE::FATAL_REASONS_CODE::FAIL_THREAD_COUNT_ZERO
            );
            panic_info_inshort info = {.is_bug=0, .is_policy=1, .is_hw_fault=0, .is_mem_corruption=0, .is_escalated=0};
            Panic::panic(default_panic_behaviors_flags,(char*)"FreePagesAllocator::second_stage thread count is zero",nullptr,&info,fatal);
            return fatal;
        }
        uint64_t denominator = thread_count*strategy.thread_coefficient;
        if(denominator==0){
            return make_fail(
                MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::INIT_SECOND_STAGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_BAD_PARAM_MATCH_THREAD_BUT_BAD_ZERO_CONEFFICIENCY
            );
        }
        uint64_t bytes_per_main_bcb = g_all_avaliable_mem_accumulate/denominator;
        if(bytes_per_main_bcb==0){
            KURD_t fatal = make_fatal(
                MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::INIT_SECOND_STAGE_RESULTS_CODE::FATAL_REASONS_CODE::FAIL_NO_AVALIABLE_MEM
            );
            panic_info_inshort info = {.is_bug=0, .is_policy=1, .is_hw_fault=0, .is_mem_corruption=0, .is_escalated=0};
            Panic::panic(default_panic_behaviors_flags,(char*)"FreePagesAllocator::second_stage no memory per thread target",nullptr,&info,fatal);
            return fatal;
        }
        int64_t main_bcb_max_order_i64=(63-__builtin_clzll(bytes_per_main_bcb))-12;
        uint64_t main_bcb_max_order=main_bcb_max_order_i64>0?(uint64_t)main_bcb_max_order_i64:0;
        Ktemplats::list_doubly<BCB_plan_entry> mainBCBs_plan_list_real;
        for(auto old_plan=g_mainBCBs_plan_list.begin();old_plan!=g_mainBCBs_plan_list.end();++old_plan){
            BCB_plan_entry plan=*old_plan;
            if(plan.order<=main_bcb_max_order){
                mainBCBs_plan_list_real.push_back(plan);
            }else{
                uint64_t split_bcb_count=1ull<<(plan.order-main_bcb_max_order);
                uint64_t split_size=1ull<<(main_bcb_max_order+12);
                for(uint64_t i=0;i<split_bcb_count;++i){
                    mainBCBs_plan_list_real.push_back(BCB_plan_entry{plan.base+i*split_size,(uint8_t)main_bcb_max_order});
                }
            }
        }
        result=build_BCBs(mainBCBs_plan_list_real,g_viceBCBs_plan_list);
    }
    else{
        result=build_BCBs(g_mainBCBs_plan_list,g_viceBCBs_plan_list);
    }

    if(error_kurd(result)){
        return result;
    }
    uint64_t processor_count = get_processor_count();
    statistics_arr = new fpa_stats[processor_count];
    processors_preffered_bcb_idx = new uint64_t[processor_count];
    ksetmem_8(statistics_arr, 0, processor_count * sizeof(fpa_stats));
    ksetmem_64(processors_preffered_bcb_idx, ~0ULL, processor_count * sizeof(uint64_t));
    flags.allow_new_BCB=true;
    return make_success();
}
uint8_t size_to_order(uint64_t size)
{
    if (size == 0) {
        return 0;
    }
    
    // 计算需要多少个4KB页面（向上取整）
    uint64_t numof_4kbpgs = (size + 4095) / 4096;
    
    // 使用匿名函数计算向上取整的2的幂
    auto next_pow2 = [](uint64_t n) -> uint64_t {
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        n++;
        return n;
    };
    
    // 将页面数向上取整到2的幂
    uint64_t np2 = next_pow2(numof_4kbpgs);
    
    // 用__builtin_clzll取log2
    // 注意：np2 保证是2的幂且不为0
    return 63 - __builtin_clzll(np2);
}

Alloc_result FreePagesAllocator::alloc(uint64_t size, buddy_alloc_params params,page_state_t interval_type)
{
    auto default_kurd = []() -> KURD_t {
        return KURD_t(
            0, 0, module_code::MEMORY,
            MEMMODULE_LOCAIONS::LOCATION_CODE_FREEPAGES_ALLOCATOR,
            0, 0, err_domain::CORE_MODULE
        );
    };
    auto make_success = [&]() -> KURD_t {
        KURD_t kurd = default_kurd();
        kurd.result = result_code::SUCCESS;
        kurd.level = level_code::INFO;
        kurd.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::EVENT_CODE_ALLOC;
        return kurd;
    };
    auto make_fail = [&](uint16_t reason) -> KURD_t {
        KURD_t kurd = set_result_fail_and_error_level(default_kurd());
        kurd.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::EVENT_CODE_ALLOC;
        kurd.reason = reason;
        return kurd;
    };
    auto make_retry = [&](uint16_t reason) -> KURD_t {
        KURD_t kurd = default_kurd();
        kurd.result = result_code::RETRY;
        kurd.level = level_code::WARNING;
        kurd.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::EVENT_CODE_ALLOC;
        kurd.reason = reason;
        return kurd;
    };
    auto merge_bcb_kurd = [&](KURD_t bcb_kurd) -> KURD_t {
        if (success_all_kurd(bcb_kurd)) {
            return make_success();
        }
        bcb_kurd.module_code = module_code::MEMORY;
        bcb_kurd.in_module_location = MEMMODULE_LOCAIONS::LOCATION_CODE_FREEPAGES_ALLOCATOR;
        bcb_kurd.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::EVENT_CODE_ALLOC;
        bcb_kurd.domain = err_domain::CORE_MODULE;
        return bcb_kurd;
    };
    auto make_result_fail = [&](uint16_t reason) -> Alloc_result {
        return Alloc_result{0, make_fail(reason)};
    };
    auto make_result_retry = [&](uint16_t reason) -> Alloc_result {
        return Alloc_result{0, make_retry(reason)};
    };
    auto mark_permenant_fail = [](bool* fail_map, uint64_t idx) {
        if (fail_map) {
            fail_map[idx] = true;
        }
    };
    auto is_all_permenant_failed = [](bool* fail_map, uint64_t count) -> bool {
        for (uint64_t i = 0; i < count; ++i) {
            if (!fail_map[i]) {
                return false;
            }
        }
        return true;
    };
    struct scan_round_result_t {
        bool success;
        bool any_busy;
        bool any_eligible;
        uint64_t scanned;
        Alloc_result alloc_result;
    };
    enum class alloc_state_t {
        SCAN_MAIN,
        SCAN_VICE,
        EVALUATE_ROUND,
        RETRY_ROUND,
        FINISH_SUCCESS,
        FINISH_FAIL,
        FINISH_RETRY
    };

    if (size == 0) {
        return make_result_fail(
            MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::ALLOC_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_INVALID_SIZE
        );
    }

    if (!flags.allow_new_BCB) {
        KURD_t kurd = make_success();
        phyaddr_t base = first_BCB->allocate_buddy_way(size, kurd, params.align_log2);
        if (flags.allow_new_BCB && statistics_arr) {
            uint32_t pid = fast_get_processor_id();
            fpa_stats& stat = statistics_arr[pid];
            stat.force_first_bcb_alloc++;
            if (!success_all_kurd(kurd)) {
                stat.alloc_fail++;
            }
        }
        return Alloc_result{base, merge_bcb_kurd(kurd)};
    }

    uint8_t order = size_to_order(size);

    uint32_t pid = fast_get_processor_id();
    fpa_stats& stat = statistics_arr[pid];
    uint64_t& preferred_idx = processors_preffered_bcb_idx[pid];

    bool* main_permenant_fail_map = main_BCB_count
        ? static_cast<bool*>(__builtin_alloca(main_BCB_count * sizeof(bool)))
        : nullptr;
    bool* vice_permenant_fail_map = vice_BCB_count
        ? static_cast<bool*>(__builtin_alloca(vice_BCB_count * sizeof(bool)))
        : nullptr;
    if (main_permenant_fail_map) {
        ksetmem_8(main_permenant_fail_map, 0, main_BCB_count * sizeof(bool));
    }
    if (vice_permenant_fail_map) {
        ksetmem_8(vice_permenant_fail_map, 0, vice_BCB_count * sizeof(bool));
    }

    auto get_main_bcb = [&](uint64_t idx) -> BuddyControlBlock& {
        return mainBCBS[idx];
    };
    auto apply_memmap_updates = [&](phyaddr_t base, uint64_t size) {
        uint64_t aligned_size = align_up(size, 4096);
        uint64_t base_idx = base >> 12;
        uint64_t end_idx = base_idx + (aligned_size >> 12);
        if (end_idx > all_pages_arr::mem_map_entry_count) {
            end_idx = all_pages_arr::mem_map_entry_count;
        }
        for (uint64_t idx = base_idx; idx < end_idx; ++idx) {
            page& p = all_pages_arr::mem_map[idx];
            p.page_flags.raw = 0;
            p.refcount = 1;
            p.head.type = static_cast<uint64_t>(interval_type);
            p.head.ptr = 0;
            p.head.order = 0;
        }
    };
    auto scan_main_bcb = [&](bool* permenant_fail_map) -> scan_round_result_t {
        scan_round_result_t round = {false, false, false, 0, Alloc_result{0, make_success()}};
        uint64_t count = main_BCB_count;
        if (count == 0) {
            return round;
        }

        auto try_index = [&](uint64_t idx) -> bool {
            if (permenant_fail_map[idx]) {
                return false;
            }
            BuddyControlBlock& bcb = get_main_bcb(idx);

            round.any_eligible = true;
            round.scanned++;
            stat.bcb_scan_total++;

            if (bcb.lock.is_locked()) {
                round.any_busy = true;
                stat.lock_try_fail++;
                return false;
            }
            KURD_t kurd = make_success();
            bcb.lock.lock();
            phyaddr_t base = bcb.allocate_buddy_way(size, kurd, params.align_log2);
            if (!success_all_kurd(kurd)) {
                mark_permenant_fail(permenant_fail_map, idx);
                bcb.lock.unlock();
                return false;
            }
                stat.alloc_main_hit++;
                preferred_idx = idx;

            round.success = true;
            round.alloc_result = Alloc_result{base, merge_bcb_kurd(kurd)};
            bcb.lock.unlock();
            return true;
        };

        if (preferred_idx < count) {
            if (try_index(preferred_idx)) {
                return round;
            }
        }

        for (uint64_t i = 0; i < count; ++i) {
            if (i == preferred_idx && preferred_idx < count) {
                continue;
            }
            if (try_index(i)) {
                return round;
            }
        }
        return round;
    };
    auto scan_bcb_array = [&](BuddyControlBlock* bcbs,
                              uint64_t count,
                              bool* permenant_fail_map,
                              bool is_main) -> scan_round_result_t {
        scan_round_result_t round = {false, false, false, 0, Alloc_result{0, make_success()}};
        if (count == 0) {
            return round;
        }

        auto try_index = [&](uint64_t idx) -> bool {
            if (permenant_fail_map[idx]) {
                return false;
            }
            BuddyControlBlock& bcb = bcbs[idx];

            round.any_eligible = true;
            round.scanned++;
            stat.bcb_scan_total++;

            if (!bcb.is_bcb_avaliable()) {
                round.any_busy = true;
                stat.lock_try_fail++;
                return false;
            }
            if (!bcb.can_alloc(order)) {
                mark_permenant_fail(permenant_fail_map, idx);
                return false;
            }

            KURD_t kurd = make_success();
            bcb.lock.lock();
            phyaddr_t base = bcb.allocate_buddy_way(size, kurd, params.align_log2);
            if (!success_all_kurd(kurd)) {
                mark_permenant_fail(permenant_fail_map, idx);
                bcb.lock.unlock();
                return false;
            }
            stat.alloc_vice_hit++;
            bcb.lock.unlock();
            round.success = true;
            round.alloc_result = Alloc_result{base, merge_bcb_kurd(kurd)};
            return true;
        };

        if (is_main && preferred_idx < count) {
            if (try_index(preferred_idx)) {
                return round;
            }
        }

        for (uint64_t i = 0; i < count; ++i) {
            if (is_main && i == preferred_idx && preferred_idx < count) {
                continue;
            }
            if (try_index(i)) {
                return round;
            }
        }
        return round;
    };

    alloc_state_t state = alloc_state_t::SCAN_MAIN;
    scan_round_result_t current_round = {false, false, false, 0, Alloc_result{0, make_success()}};
    Alloc_result final_result = {0, make_success()};

    while (true) {
        switch (state) {
        case alloc_state_t::SCAN_MAIN: {
            current_round = scan_main_bcb(main_permenant_fail_map);
            if (current_round.success) {
                final_result = current_round.alloc_result;
                state = alloc_state_t::FINISH_SUCCESS;
            } else {
                state = alloc_state_t::SCAN_VICE;
            }
            break;
        }
        case alloc_state_t::SCAN_VICE: {
            scan_round_result_t vice_round = scan_bcb_array(vice_BCBS, vice_BCB_count, vice_permenant_fail_map, false);
            current_round.any_busy = current_round.any_busy || vice_round.any_busy;
            current_round.any_eligible = current_round.any_eligible || vice_round.any_eligible;
            current_round.scanned += vice_round.scanned;
            if (vice_round.success) {
                final_result = vice_round.alloc_result;
                state = alloc_state_t::FINISH_SUCCESS;
            } else {
                state = alloc_state_t::EVALUATE_ROUND;
            }
            break;
        }
        case alloc_state_t::EVALUATE_ROUND: {
            if (stat.bcb_scan_max < current_round.scanned) {
                stat.bcb_scan_max = current_round.scanned;
            }

            bool all_main_permenant_failed = is_all_permenant_failed(main_permenant_fail_map, main_BCB_count);
            bool all_vice_permenant_failed = is_all_permenant_failed(vice_permenant_fail_map, vice_BCB_count);

            if (all_main_permenant_failed && all_vice_permenant_failed) {
                stat.alloc_fail++;
                uint16_t reason = current_round.any_eligible
                    ? MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::ALLOC_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_NO_AVALIABLE_BCB
                    : MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::ALLOC_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_NO_MATCHED_BCB;
                final_result = make_result_fail(reason);
                state = alloc_state_t::FINISH_FAIL;
                break;
            }

            if (!params.try_lock_always_try) {
                if (current_round.any_busy) {
                    final_result = make_result_retry(
                        MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::ALLOC_RESULTS_CODE::RETRY_REASONS_CODE::RETRY_REASON_CODE_TARGET_BUSY
                    );
                    state = alloc_state_t::FINISH_RETRY;
                } else {
                    stat.alloc_fail++;
                    final_result = make_result_fail(
                        MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::ALLOC_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_NO_AVALIABLE_BCB
                    );
                    state = alloc_state_t::FINISH_FAIL;
                }
                break;
            }

            state = alloc_state_t::RETRY_ROUND;
            break;
        }
        case alloc_state_t::RETRY_ROUND: {
            stat.lock_spin++;
            current_round = {false, false, false, 0, Alloc_result{0, make_success()}};
            state = alloc_state_t::SCAN_MAIN;
            break;
        }
        case alloc_state_t::FINISH_SUCCESS:
        case alloc_state_t::FINISH_FAIL:
        case alloc_state_t::FINISH_RETRY:
            return final_result;
        }
    }
}
KURD_t FreePagesAllocator::free(phyaddr_t base, uint64_t size)
{
    auto default_kurd = []() -> KURD_t {
        return KURD_t(
            0, 0, module_code::MEMORY,
            MEMMODULE_LOCAIONS::LOCATION_CODE_FREEPAGES_ALLOCATOR,
            MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::EVENT_CODE_FREE,
            level_code::INFO, err_domain::CORE_MODULE
        );
    };
    auto make_success = [&]() -> KURD_t {
        KURD_t kurd = default_kurd();
        kurd.result = result_code::SUCCESS;
        kurd.level = level_code::INFO;
        return kurd;
    };
    auto make_fail = [&](uint16_t reason) -> KURD_t {
        KURD_t kurd = set_result_fail_and_error_level(default_kurd());
        kurd.reason = reason;
        return kurd;
    };
    auto align_size = [&](uint64_t in, uint64_t& out) -> bool {
        if (in == 0) {
            out = 0;
            return false;
        }
        out = align_up(in, 4096);
        return true;
    };
    auto validate_mem_map = [&](uint64_t base_idx, uint64_t end_idx) -> bool {
        if (base_idx >= end_idx || end_idx > all_pages_arr::mem_map_entry_count) {
            return false;
        }
        bool type_set = false;
        uint64_t expected_type = 0;
        for (uint64_t idx = base_idx; idx < end_idx; ++idx) {
            page& p = all_pages_arr::mem_map[idx];
            if (p.page_flags.bitfield.is_skipped) {
                return false;
            }
            if (p.head.order != 0) {
                return false;
            }
            if (p.refcount != 1) {
                return false;
            }
            if (!type_set) {
                expected_type = p.head.type;
                type_set = true;
            } else if (p.head.type != expected_type) {
                return false;
            }
        }
        return type_set;
    };
    auto free_mem_map_4kb = [&](uint64_t base_idx, uint64_t end_idx) {
        for (uint64_t idx = base_idx; idx < end_idx; ++idx) {
            page& p = all_pages_arr::mem_map[idx];
            p.refcount = 0;
            p.page_flags.raw = 0;
            p.page_flags.bitfield.is_allocateble = 1;
            p.head.type = static_cast<uint64_t>(page_state_t::free);
            p.head.ptr = 0;
            p.head.order = 0;
        }
    };
    auto free_on_bcb = [&](BuddyControlBlock& bcb) -> KURD_t {
        uint64_t aligned_size = 0;
        if (!align_size(size, aligned_size)) {
            return make_fail(
                MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_BASE_NOT_BELONG
            );
        }
        uint64_t base_idx = base >> 12;
        uint64_t end_idx = base_idx + (aligned_size >> 12);
        bcb.lock.lock();
        if (!validate_mem_map(base_idx, end_idx)) {
            bcb.lock.unlock();
            return make_fail(
                MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_BASE_NOT_BELONG
            );
        }
        KURD_t kurd = bcb.free_buddy_way(base, size);
        if (error_kurd(kurd)) {
            bcb.lock.unlock();
            return kurd;
        }
        free_mem_map_4kb(base_idx, end_idx);
        bcb.lock.unlock();
        return make_success();
    };

    if(flags.allow_new_BCB){
        for(uint64_t i=0;i<main_BCB_count;++i){
            BuddyControlBlock& bcb = mainBCBS[i];
            if(bcb.is_addr_belong_to_this_BCB(base)){
                return free_on_bcb(bcb);
            }
        }
        for(uint64_t i=0;i<vice_BCB_count;++i){
            BuddyControlBlock& bcb=vice_BCBS[i];
            if(bcb.is_addr_belong_to_this_BCB(base)){
                return free_on_bcb(bcb);
            }
        }
    }
    return free_on_bcb(*first_BCB);
}
fpa_stats FreePagesAllocator::get_fpa_stats()
{
    return get_fpa_stats(fast_get_processor_id());
}

fpa_stats FreePagesAllocator::get_fpa_stats(uint64_t pid)
{
    return statistics_arr[pid];
}

fpa_stats FreePagesAllocator::get_fpa_stats_all()
{
    fpa_stats total = {};
    uint64_t processor_count = 1;
        processor_count = fpa_get_cpu_count();
        if (processor_count == 0) {
            processor_count = 1;
        }
    

    for (uint64_t pid = 0; pid < processor_count; ++pid) {
        const fpa_stats& current = statistics_arr[pid];
        total.alloc_main_hit += current.alloc_main_hit;
        total.alloc_vice_hit += current.alloc_vice_hit;
        if (total.bcb_scan_max < current.bcb_scan_max) {
            total.bcb_scan_max = current.bcb_scan_max;
        }
        total.bcb_scan_total += current.bcb_scan_total;
        total.alloc_fail += current.alloc_fail;
        total.constrained_alloc += current.constrained_alloc;
        total.constrained_retry += current.constrained_retry;
        total.lock_try_fail += current.lock_try_fail;
        total.lock_spin += current.lock_spin;
        total.force_first_bcb_alloc += current.force_first_bcb_alloc;
    }
    return total;
}
