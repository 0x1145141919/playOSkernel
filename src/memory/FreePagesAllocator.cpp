#include "memory/FreePagesAllocator.h"
#include "memory/phygpsmemmgr.h"
#include "firmware/ACPI_APIC.h"
#include "panic.h"
#include <new>
FreePagesAllocator::flags_t FreePagesAllocator::flags;
uint64_t FreePagesAllocator::main_BCB_count;
FreePagesAllocator::BuddyControlBlock* FreePagesAllocator::main_BCBS;
uint64_t FreePagesAllocator::vice_BCB_count;
FreePagesAllocator::BuddyControlBlock* FreePagesAllocator::vice_BCBS;
fpa_stats* FreePagesAllocator::statistics_arr;
uint64_t*FreePagesAllocator::processors_preffered_bcb_idx;
KURD_t FreePagesAllocator::Init(loaded_VM_interval* first_BCB_bitmap)
{
    constexpr uint64_t default_first_bcb_order=18;
    flags.allow_new_BCB=false;
    KURD_t kurd=phymemspace_mgr::pages_dram_buddy_regist(
        0x100000000,1ull<<default_first_bcb_order
    );
    if(error_kurd(kurd))return kurd;
    first_BCB=new BuddyControlBlock(
        0x100000000,
        default_first_bcb_order
    );
    first_BCB->first_bcb_specified_init(first_BCB_bitmap);
    return KURD_t();//需要正确的kurd
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
        if (!gAnalyzer || !gAnalyzer->processor_x64_list) {
            return 1;
        }
        uint64_t count = gAnalyzer->processor_x64_list->size();
        return count == 0 ? 1 : count;
    };

    struct BCB_plan_entry{
        phyaddr_t base;
        uint8_t order;
    };
    constexpr uint8_t min_main_bcb_order=14;//64mb
    constexpr uint8_t min_vice_bcb_order=10;//4mb
    Ktemplats::list_doubly<BCB_plan_entry> mainBCBs_plan_list;
    Ktemplats::list_doubly<BCB_plan_entry> viceBCBs_plan_list;
    phymemspace_mgr::free_segs_t* free_segs=phymemspace_mgr::free_segs_get();
    if(!free_segs){
        return make_fatal(
            MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::INIT_SECOND_STAGE_RESULTS_CODE::FATAL_REASONS_CODE::FAIL_NO_AVALIABLE_MEM
        );
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
        return make_fatal(
            MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::INIT_SECOND_STAGE_RESULTS_CODE::FATAL_REASONS_CODE::FAIL_NO_AVALIABLE_MEM
        );
    }
    auto cur=free_segs->entries;
    uint64_t all_avaliable_mem_accumulate=0;
    for(uint64_t i=0;i<free_segs->count;++i){
        auto& seg=cur[i];

        uint64_t order_max=0;
        phyaddr_t base=seg.base;
        if(seg.base==0){
            order_max=0;
        }else{
            uint64_t tz=__builtin_ctzll(seg.base);
            order_max=(tz>12)?(tz-12):0;
        }
        const phyaddr_t end=seg.base+seg.size;
        all_avaliable_mem_accumulate+=seg.size;
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
                    mainBCBs_plan_list.push_back(BCB_plan_entry{base,(uint8_t)order});
                }else if(order>=min_vice_bcb_order){
                    viceBCBs_plan_list.push_back(BCB_plan_entry{base,(uint8_t)order});
                }else{
                    //太小不用
                }
                break;
            }
            if(top<end){
                if(order>=min_main_bcb_order){
                    mainBCBs_plan_list.push_back(BCB_plan_entry{base,(uint8_t)order});
                }else if(order>=min_vice_bcb_order){
                    viceBCBs_plan_list.push_back(BCB_plan_entry{base,(uint8_t)order});
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
    auto build_BCBs=[](
        Ktemplats::list_doubly<BCB_plan_entry>& mainBCBs_plan_list,
        Ktemplats::list_doubly<BCB_plan_entry>& viceBCBs_plan_list
    )->KURD_t{
        auto pages_for = [](uint64_t bytes) -> uint64_t {
            return bytes == 0 ? 0 : ((bytes + 4095) >> 12);
        };

        main_BCBS = nullptr;
        vice_BCBS = nullptr;
        main_BCB_count=mainBCBs_plan_list.size();
        vice_BCB_count=viceBCBs_plan_list.size();

        uint64_t main_pages = pages_for(main_BCB_count * sizeof(BuddyControlBlock));
        uint64_t vice_pages = pages_for(vice_BCB_count * sizeof(BuddyControlBlock));

        KURD_t kurd=KURD_t();
        if(main_pages != 0){
            main_BCBS=(BuddyControlBlock *)__wrapped_pgs_valloc(
                &kurd,
                main_pages,
                page_state_t::KERNEL,
                12
            );
            if(error_kurd(kurd)){
                return kurd;
            }
        }
        if(vice_pages != 0){
            vice_BCBS=(BuddyControlBlock *)__wrapped_pgs_valloc(
                &kurd,
                vice_pages,
                page_state_t::KERNEL,
                12
            );
            if(error_kurd(kurd)){
                if(main_pages != 0){
                    __wrapped_pgs_vfree(main_BCBS, main_pages);
                    main_BCBS = nullptr;
                }
                return kurd;
            }
        }

        uint64_t main_constructed = 0;
        uint64_t vice_constructed = 0;
        uint64_t i=0;
        for(auto it=mainBCBs_plan_list.begin();it!=mainBCBs_plan_list.end();++it,++i){
            BCB_plan_entry plan=*it;
            new (main_BCBS + i) BuddyControlBlock(
                plan.base,
                plan.order
            );
            ++main_constructed;
            kurd=main_BCBS[i].second_stage_init();
            if(error_kurd(kurd)){
                for(uint64_t j=main_constructed;j>0;--j){
                    main_BCBS[j-1].~BuddyControlBlock();
                }
                if(vice_pages != 0){
                    __wrapped_pgs_vfree(vice_BCBS, vice_pages);
                    vice_BCBS = nullptr;
                }
                if(main_pages != 0){
                    __wrapped_pgs_vfree(main_BCBS, main_pages);
                    main_BCBS = nullptr;
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
                    main_BCBS[j-1].~BuddyControlBlock();
                }
                if(vice_pages != 0){
                    __wrapped_pgs_vfree(vice_BCBS, vice_pages);
                    vice_BCBS = nullptr;
                }
                if(main_pages != 0){
                    __wrapped_pgs_vfree(main_BCBS, main_pages);
                    main_BCBS = nullptr;
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
            return gAnalyzer->processor_x64_list->size();
            #endif

        }();
        if(strategy.thread_coefficient==0){
            free_free_segs();
            return make_fail(
                MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::INIT_SECOND_STAGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_BAD_PARAM_MATCH_THREAD_BUT_BAD_ZERO_CONEFFICIENCY
            );
        }
        if(all_avaliable_mem_accumulate==0){
            KURD_t fatal = make_fatal(
                MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::INIT_SECOND_STAGE_RESULTS_CODE::FATAL_REASONS_CODE::FAIL_NO_AVALIABLE_MEM
            );
            panic_info_inshort info = {.is_bug=0, .is_policy=1, .is_hw_fault=0, .is_mem_corruption=0, .is_escalated=0};
            free_free_segs();
            Panic::panic(default_panic_behaviors_flags,(char*)"FreePagesAllocator::second_stage no available memory",nullptr,&info,fatal);
            return fatal;
        }
        if(thread_count==0){
            KURD_t fatal = make_fatal(
                MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::INIT_SECOND_STAGE_RESULTS_CODE::FATAL_REASONS_CODE::FAIL_THREAD_COUNT_ZERO
            );
            panic_info_inshort info = {.is_bug=0, .is_policy=1, .is_hw_fault=0, .is_mem_corruption=0, .is_escalated=0};
            free_free_segs();
            Panic::panic(default_panic_behaviors_flags,(char*)"FreePagesAllocator::second_stage thread count is zero",nullptr,&info,fatal);
            return fatal;
        }
        uint64_t denominator = thread_count*strategy.thread_coefficient;
        if(denominator==0){
            free_free_segs();
            return make_fail(
                MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::INIT_SECOND_STAGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_BAD_PARAM_MATCH_THREAD_BUT_BAD_ZERO_CONEFFICIENCY
            );
        }
        uint64_t bytes_per_main_bcb = all_avaliable_mem_accumulate/denominator;
        if(bytes_per_main_bcb==0){
            KURD_t fatal = make_fatal(
                MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::INIT_SECOND_STAGE_RESULTS_CODE::FATAL_REASONS_CODE::FAIL_NO_AVALIABLE_MEM
            );
            panic_info_inshort info = {.is_bug=0, .is_policy=1, .is_hw_fault=0, .is_mem_corruption=0, .is_escalated=0};
            free_free_segs();
            Panic::panic(default_panic_behaviors_flags,(char*)"FreePagesAllocator::second_stage no memory per thread target",nullptr,&info,fatal);
            return fatal;
        }
        int64_t main_bcb_max_order_i64=(63-__builtin_clzll(bytes_per_main_bcb))-12;
        uint64_t main_bcb_max_order=main_bcb_max_order_i64>0?(uint64_t)main_bcb_max_order_i64:0;
        Ktemplats::list_doubly<BCB_plan_entry> mainBCBs_plan_list_real;
        for(auto old_plan=mainBCBs_plan_list.begin();old_plan!=mainBCBs_plan_list.end();++old_plan){
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
        free_free_segs();
        result=build_BCBs(mainBCBs_plan_list_real,viceBCBs_plan_list);
    }
    else{
        free_free_segs();
        result=build_BCBs(mainBCBs_plan_list,viceBCBs_plan_list);
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
Alloc_result FreePagesAllocator::alloc(uint64_t size, alloc_params params)
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
    auto interval_end = [](uint64_t base, uint64_t size, uint64_t& end_out) -> bool {
        if (size == 0) {
            end_out = base;
            return true;
        }
        if (base > ~0ULL - size) {
            return false;
        }
        end_out = base + size;
        return true;
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
    auto bcb_range_end = [](BuddyControlBlock& bcb) -> uint64_t {
        return bcb.get_base() + (1ULL << (bcb.get_max_order() + 12));
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
    struct interval_limit {
        uint64_t base;
        uint64_t end;
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
    if (params.numa != 0) {
        return make_result_fail(
            MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::ALLOC_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_NUMA_NOT_SUPPORTED
        );
    }

    if (!flags.allow_new_BCB || params.force_first_bcb) {
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
    interval_limit effective_interval = {0, ~0ULL};
    bool constrained = false;

    if (!params.no_up_limit_bit) {
        constrained = true;
        effective_interval.end = params.up_phyaddr_limit;
    }
    if (!params.no_addr_constrain_bit) {
        constrained = true;
        uint64_t addr_end = 0;
        if (params.constrain_interval_size == 0 || !interval_end(params.constrain_base, params.constrain_interval_size, addr_end)) {
            return make_result_fail(
                MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::ALLOC_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_INVALID_CONSTRAIN
            );
        }
        if (effective_interval.base < params.constrain_base) {
            effective_interval.base = params.constrain_base;
        }
        if (effective_interval.end > addr_end) {
            effective_interval.end = addr_end;
        }
    }
    if (effective_interval.base >= effective_interval.end) {
        return make_result_fail(
            MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::ALLOC_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_INVALID_CONSTRAIN
        );
    }
    if (effective_interval.end - effective_interval.base < size) {
        return make_result_fail(
            MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR::ALLOC_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_NO_MATCHED_BCB
        );
    }

    uint32_t pid = fast_get_processor_id();
    fpa_stats& stat = statistics_arr[pid];
    uint64_t& preferred_idx = processors_preffered_bcb_idx[pid];
    if (constrained) {
        stat.constrained_alloc++;
    }

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

    auto bcb_in_interval = [&](BuddyControlBlock& bcb) -> bool {
        uint64_t bcb_base = bcb.get_base();
        uint64_t bcb_end = bcb_range_end(bcb);
        return effective_interval.base <= bcb_base && bcb_end <= effective_interval.end;
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
            if (!bcb_in_interval(bcb)) {
                mark_permenant_fail(permenant_fail_map, idx);
                return false;
            }

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
            phyaddr_t base = bcb.allocate_buddy_way(size, kurd, params.align_log2);
            if (!success_all_kurd(kurd)) {
                mark_permenant_fail(permenant_fail_map, idx);
                return false;
            }

            if (is_main) {
                stat.alloc_main_hit++;
                preferred_idx = idx;
            } else {
                stat.alloc_vice_hit++;
            }
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
            current_round = scan_bcb_array(main_BCBS, main_BCB_count, main_permenant_fail_map, true);
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
            if (constrained) {
                stat.constrained_retry++;
            }
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
    if(flags.allow_new_BCB){
        for(uint64_t i=0;i<main_BCB_count;++i){
            BuddyControlBlock& bcb=main_BCBS[i];
            if(bcb.is_addr_belong_to_this_BCB(base)){
                return bcb.free_buddy_way(base,size);
            }
        }
        for(uint64_t i=0;i<vice_BCB_count;++i){
            BuddyControlBlock& bcb=vice_BCBS[i];
            if(bcb.is_addr_belong_to_this_BCB(base)){
                return bcb.free_buddy_way(base,size);
            }
        }   
        //错误kurd

    }
    KURD_t kurd=first_BCB->free_buddy_way(base,size);
    return kurd;
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
    if (gAnalyzer && gAnalyzer->processor_x64_list) {
        processor_count = gAnalyzer->processor_x64_list->size();
        if (processor_count == 0) {
            processor_count = 1;
        }
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
