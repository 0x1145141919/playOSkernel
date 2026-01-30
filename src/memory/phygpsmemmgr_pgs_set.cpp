#include "memory/phygpsmemmgr.h"
#include "os_error_definitions.h"
#include "util/OS_utils.h"
#include "util/kout.h"
#include "memory/kpoolmemmgr.h"
#include "linker_symbols.h"
#include "memory/FreePagesAllocator.h"
#include "memory/AddresSpace.h"
bool phymemspace_mgr::subtb_alloc_is_pool_way_flag;
static constexpr uint64_t PAGES_4KB_PER_2MB = 512;
static constexpr uint64_t PAGES_2MB_PER_1GB = 512;
static constexpr uint64_t PAGES_4KB_PER_1GB = PAGES_4KB_PER_2MB * PAGES_2MB_PER_1GB; // 262144
phymemspace_mgr::page_size2mb_t *phymemspace_mgr::alloc_2mb_subtable()
{
    if(subtb_alloc_is_pool_way_flag)return new page_size2mb_t[PAGES_2MB_PER_1GB];
    else{
        uint32_t size=PAGES_4KB_PER_1GB * sizeof(page_size1gb_t);
        KURD_t kurd;
        phyaddr_t phybase=FreePagesAllocator::first_BCB->allocate_buddy_way(size,kurd);
        if(kurd.result!=result_code::SUCCESS){
            //原子操作失败，直接panic
        }
        page_size2mb_t*result=(page_size2mb_t*)KspaceMapMgr::pgs_remapp(
            phybase,
            size,
            KspaceMapMgr::PG_RW,
            0,
            true
        );
        if(result==nullptr){
            //内存分配失败，直接panic
        }
        return result;
    }
}
void phymemspace_mgr::free_2mb_subtable(page_size2mb_t *table)
{
    if(subtb_alloc_is_pool_way_flag){
        delete[] table;
    }
    else{
        uint32_t size=PAGES_4KB_PER_1GB * sizeof(page_size1gb_t);
        KURD_t kurd;
        phyaddr_t phybase;
        int res=KspaceMapMgr::v_to_phyaddrtraslation((vaddr_t)table,phybase);
        if(res!=OS_SUCCESS){
            //原子操作失败，直接panic
        }
        KspaceMapMgr::pgs_remapped_free((vaddr_t)table);
        kurd=FreePagesAllocator::first_BCB->free_buddy_way(phybase,size);
        if(kurd.result!=result_code::SUCCESS){
            //内存分配失败，直接panic
        }
    }
}
phymemspace_mgr::page_size4kb_t *phymemspace_mgr::alloc_4kb_subtable()
{
    if(subtb_alloc_is_pool_way_flag)return new page_size4kb_t[PAGES_2MB_PER_1GB];
    else{
        uint32_t size=PAGES_4KB_PER_1GB * sizeof(page_size4kb_t);
        KURD_t kurd;
        phyaddr_t phybase=FreePagesAllocator::first_BCB->allocate_buddy_way(size,kurd);
        if(kurd.result!=result_code::SUCCESS){
            //原子操作失败，直接panic
        }
        page_size4kb_t*result=(page_size4kb_t*)KspaceMapMgr::pgs_remapp(
            phybase,
            size,
            KspaceMapMgr::PG_RW,
            0,
            true
        );
        if(result==nullptr){
            //内存分配失败，直接panic
        }
        return result;
    }
}
void phymemspace_mgr::free_4kb_subtable(page_size4kb_t *table)
{
    if(subtb_alloc_is_pool_way_flag){
        delete[] table;
    }
    else{
        uint32_t size=PAGES_4KB_PER_1GB * sizeof(page_size1gb_t);
        KURD_t kurd;
        phyaddr_t phybase;
        int res=KspaceMapMgr::v_to_phyaddrtraslation((vaddr_t)table,phybase);
        if(res!=OS_SUCCESS){
            //原子操作失败，直接panic
        }
        KspaceMapMgr::pgs_remapped_free((vaddr_t)table);
        kurd=FreePagesAllocator::first_BCB->free_buddy_way(phybase,size);
        if(kurd.result!=result_code::SUCCESS){
            //内存分配失败，直接panic
        }
    }
}
KURD_t phymemspace_mgr::pages_state_set(phyaddr_t base,
                                    uint64_t num_of_4kbpgs,
                                    page_state_t state,
                                pages_state_set_flags_t flags)
{//todo 对新flags.is_blackhole_declaim的行为以及对
    //is_blackhole_declaim和is_blackhole_aclaim互斥合法性检验
    
    // 集中声明 KURD 对象
    KURD_t success = default_success();
    KURD_t fail = default_failure();
    KURD_t fatal = default_fatal();
    
    // 集中设置 event_code
    success.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_SET;
    fail.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_SET;
    fatal.event_code = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_SET;
    
    if (num_of_4kbpgs == 0) return success;

    // ---- Step 1: 切段 ----
    seg_to_pages_info_package_t pak;
    int r = phymemseg_to_pacage(base, num_of_4kbpgs, pak);
    if (r != OS_SUCCESS) {
        fail.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_RESULTS_CODE::FAIL_REASONS::REASON_CODE_FAIL_TO_SPLIT_SEG;
        return fail;
    }

    // 定义四个lambda函数替换原来的成员函数
    /**
     * 外部保证这个要拆分1GB表项的有效性
     */
    auto ensure_1gb_subtable_lambda = [flags](uint64_t idx_1gb){
    page_size1gb_t *p1 = top_1gb_table->get(idx_1gb);
    if (!p1->flags.is_sub_valid)
    {
        page_size2mb_t* sub2 = alloc_2mb_subtable();;//new失败里面自动panic,不用考虑空指针

        setmem(sub2, PAGES_2MB_PER_1GB * sizeof(page_size2mb_t), 0);

        p1->sub2mbpages = sub2;
        p1->flags.is_sub_valid = 1;
        p1->flags.state = NOT_ATOM;
        
        // 只有在非acclaim_backhole操作时才初始化子页状态
        if (flags.op != pages_state_set_flags_t::acclaim_backhole) {
            page_state_t state = flags.params.if_mmio ? MMIO_FREE : FREE;
            for(uint16_t i = 0; i < PAGES_2MB_PER_1GB; i++){
                sub2[i].flags.state = state;
            }
        }
    }
    };
    auto ensure_2mb_subtable_lambda = [flags](page_size2mb_t &p2){
        if (!p2.flags.is_sub_valid)
        {
            page_size4kb_t* sub4 = alloc_4kb_subtable();
            setmem(sub4, PAGES_4KB_PER_2MB * sizeof(page_size4kb_t), 0);
            p2.sub_pages = sub4;
            p2.flags.is_sub_valid = 1;
            p2.flags.state = NOT_ATOM;
            // 只有在非acclaim_backhole操作时才初始化子页状态
            if (flags.op != pages_state_set_flags_t::acclaim_backhole) {
                page_state_t state = flags.params.if_mmio ? MMIO_FREE : FREE;
                for(uint16_t i = 0; i < PAGES_4KB_PER_2MB; i++) {
                    sub4[i].flags.state = state;
                }
            }
        }
    };
    enum folds_functions_result_t:uint8_t{
        NO_FOLDED=0,
        FOLDED_FREE=1,
        FOLDED_FULL=2
    };
    auto try_fold_2mb_lambda = [flags](page_size2mb_t &p2) -> folds_functions_result_t {
        // 没有子表就是原子 2MB 页，无法向上折叠
        if (!p2.flags.is_sub_valid)
            return NO_FOLDED;

        bool all_free = true;
        bool all_full = true;

        for (uint16_t i = 0; i < PAGES_4KB_PER_2MB; i++)
        {
            uint8_t st = p2.sub_pages[i].flags.state;
            if(flags.params.if_mmio)
            {
            if (st == MMIO) all_free = false;
            // all_used_or_full 要求每个子页不是 FREE（即已被占用或被标为 FULL）
            if (st == MMIO_FREE) all_full = false;
            }else{
                if (st != FREE) all_free = false;
                if (st == FREE) all_full = false;
            }

            if (!all_free && !all_full)
                break; // 已经确定是 NOT_ATOM
        }

        if (all_free)
        {
            // 全部 free：可以删除子表，恢复为原子 FREE 2MB
            free_4kb_subtable(p2.sub_pages);
            p2.sub_pages = nullptr;
            p2.flags.is_sub_valid = 0;
            p2.flags.state = flags.params.if_mmio?MMIO_FREE:FREE;
            return FOLDED_FREE;
        }

        if (all_full)
        {
            // 全部被占用或被标为 FULL：将该 2MB 标记为 FULL
            // **注意：不删除子表**（后续可能会有引用计数变动）
            p2.flags.state = FULL;
            // 保持 p2.flags.is_sub_valid == 1
            return FOLDED_FULL;
        }

        // 混合：部分占用，标记为 PARTIAL，不删除子表
        p2.flags.state = NOT_ATOM;
        return NO_FOLDED;
    };

    auto try_fold_1gb_lambda = [&try_fold_2mb_lambda,flags](page_size1gb_t &p1) -> folds_functions_result_t {
        // 没有子表就是原子 1GB 页，无法向上折叠
        if (!p1.flags.is_sub_valid)
            return NO_FOLDED;

        bool all_free = true;
        bool all_full = true;

        for (uint16_t i = 0; i < PAGES_2MB_PER_1GB; i++)
        {
            page_size2mb_t &p2 = p1.sub2mbpages[i];

            // 如果 p2 是非原子（有子表），尝试折叠它（这样可以把能 collapse 的 2MB 变为 FREE/FULL）
            if (p2.flags.is_sub_valid)
            {
                try_fold_2mb_lambda(p2);
                // try_fold_2mb 会在 all-free 或 all-used 情况下修改 p2.flags.state，
                // 且在 all_free 情况下会释放 p2.sub_pages 并把 is_sub_valid 置 0。
            }

            // 现在基于 p2.flags.state 决策：
            if(flags.params.if_mmio==false)
            {
            if (p2.flags.state != FREE) all_free = false;
            }else{
                if (p2.flags.state != MMIO_FREE) all_free = false;
            }if (p2.flags.state != FULL) all_full = false;

            if (!all_free && !all_full)
                break; // 已经确定是 NOT_ATOM
        }

        if (all_free)
        {
            // 所有 2MB 都是 FREE：删除 1GB 的子表并恢复为原子 FREE
            free_2mb_subtable(p1.sub2mbpages);
            p1.sub2mbpages = nullptr;
            p1.flags.is_sub_valid = 0;
            p1.flags.state = flags.params.if_mmio?MMIO_FREE:FREE;
            return FOLDED_FREE;
        }

        if (all_full)
        {
            // 所有 2MB 都是 FULL：标记该 1GB 为 FULL
            // **注意：不删除子表**（保留子表以便未来 refcount 增加或子页操作）
            p1.flags.state =FULL;
            // 保持 p1.flags.is_sub_valid == 1
            return FOLDED_FULL;
        }

        // 混合：标记为 PARTIAL，保留子表
        p1.flags.state = NOT_ATOM;
        return NO_FOLDED;
    };

    auto _1gb_pages_state_set_lambda = [state,flags,&fatal](uint64_t entry_base_idx, uint64_t num_of_1gbpgs){
        int status = OS_SUCCESS;
        for (uint64_t i = 0; i < num_of_1gbpgs; i++) {
            page_size1gb_t* _1 = top_1gb_table->get(entry_base_idx + i);
            if(_1==nullptr){
                if(flags.op==pages_state_set_flags_t::acclaim_backhole){
                    status=top_1gb_table->enable_idx(entry_base_idx+i);
                    if(status!=OS_SUCCESS){
                        //可能是内存不足或者参数非法越界
                        kio::bsp_kout<<"_1gb_pages_state_set:enable_idx failed for index:"<<entry_base_idx+i<<kio::kendl;
                        //直接panic，这个核心数据结构因为误操作已经损毁不可信
                        fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_RESULTS_CODE::FATAL_REASONS::REASON_CODE_TOP1GB_ENABLE_FAIL;
                        phymemspace_mgr::in_module_panic(fatal);
                    }
                    _1=top_1gb_table->get(entry_base_idx+i);
                }else{//非黑洞模式不得创建
                    kio::bsp_kout<<"_1gb_pages_state_set:get failed for index:"<<entry_base_idx+i<<kio::kendl;
                    //直接panic，这个核心数据结构因为误操作已经损毁不可信
                    fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_RESULTS_CODE::FATAL_REASONS::REASON_CODE_attempt_create_new_entry_not_blackhole;
                    phymemspace_mgr::in_module_panic(fatal);
                }
            }
            _1->flags.state = state;
            if(flags.op==pages_state_set_flags_t::normal)if(flags.params.if_init_ref_count)_1->ref_count=1;
        }
    };

    auto _4kb_pages_state_set_lambda = [state,flags](uint64_t entry4kb_base_idx, uint64_t num_of_4kbpgs, page_size4kb_t *base_entry){
        for (uint64_t i = 0; i < num_of_4kbpgs; i++) {
            auto& page_entry = base_entry[entry4kb_base_idx + i];
            page_entry.flags.state = state;
            if(flags.op==pages_state_set_flags_t::normal)
                if(flags.params.if_init_ref_count)page_entry.ref_count=1;
        }
    };

    auto _2mb_pages_state_set_lambda = [state,flags](uint64_t entry2mb_base_idx, uint64_t num_of_2mbpgs, page_size2mb_t *base_entry){
        for (uint64_t i = 0; i < num_of_2mbpgs; i++) {
            auto& page_entry = base_entry[entry2mb_base_idx + i];
            page_entry.flags.state = state;
             if(flags.op==pages_state_set_flags_t::normal)
                if(flags.params.if_init_ref_count)page_entry.ref_count=1;
        }
    };
    if(flags.op!=pages_state_set_flags_t::declaim_blackhole)
    {// ---- Step 2: 遍历每个段条目 ----
    folds_functions_result_t fold_result;
    for (int i = 0; i < 5; i++)
    {
        auto &ent = pak.entries[i];
        if (ent.num_of_pages == 0) continue;

        uint64_t pg4k_start = ent.base >> 12;

        if (ent.page_size_in_byte == _1GB_PG_SIZE)
        {
            // ===== 1GB 级 =====
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;

            _1gb_pages_state_set_lambda(idx_1gb, ent.num_of_pages);

        }
        else if (ent.page_size_in_byte == _2MB_PG_SIZE)
        {
            // ===== 2MB 级 =====
            uint64_t idx_2mb = pg4k_start / PAGES_4KB_PER_2MB;
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;
            int status=0;
            page_size1gb_t *p1 = top_1gb_table->get(idx_1gb);
            if(p1==nullptr)
            {
                if(flags.op==pages_state_set_flags_t::acclaim_backhole)
                {
                    status=top_1gb_table->enable_idx(idx_1gb);
                    if(status!=OS_SUCCESS){
                        fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_RESULTS_CODE::FATAL_REASONS::REASON_CODE_TOP1GB_ENABLE_FAIL;
                        return fatal;
                    }
                    p1 = top_1gb_table->get(idx_1gb);
                    p1->flags.is_sub_valid=false;
                }else{
                    kio::bsp_kout<<"_4kb_pages_state_set:attempt to create a new entry in no black hole mode"<<kio::kendl;
                    fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_RESULTS_CODE::FATAL_REASONS::REASON_CODE_attempt_create_new_entry_not_blackhole;
                    return fatal;
                }
            }
            ensure_1gb_subtable_lambda(idx_1gb);
            // ---- 调用真正的 2MB setter ----
            page_size2mb_t *p2 = p1->sub2mbpages;
            _2mb_pages_state_set_lambda(idx_2mb % PAGES_2MB_PER_1GB,
                                     ent.num_of_pages,
                                     p2);
            try_fold_1gb_lambda(*top_1gb_table->get(idx_1gb));
        }
        
        else if (ent.page_size_in_byte == _4KB_PG_SIZE)
        {
            // ===== 4KB 级 =====
            uint64_t idx_2mb = pg4k_start / PAGES_4KB_PER_2MB;
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;
            int status=0;
            page_size1gb_t *p1 = top_1gb_table->get(idx_1gb);
            if(p1==nullptr)
            {
                if(flags.op==pages_state_set_flags_t::acclaim_backhole)
                {
                    status=top_1gb_table->enable_idx(idx_1gb);
                    if(status!=OS_SUCCESS){
                        fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_RESULTS_CODE::FATAL_REASONS::REASON_CODE_TOP1GB_ENABLE_FAIL;
                        return fatal;
                    }
                    p1 = top_1gb_table->get(idx_1gb);
                    p1->flags.is_sub_valid=false;
                }else{
                    kio::bsp_kout<<"_4kb_pages_state_set:attempt to create a new entry in no black hole mode"<<kio::kendl;
                    //直接panic，这个核心数据结构因为误操作已经损毁不可信
                    fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_RESULTS_CODE::FATAL_REASONS::REASON_CODE_attempt_create_new_entry_not_blackhole;
                    return fatal;
                }
            }
            ensure_1gb_subtable_lambda(idx_1gb);
            page_size2mb_t &p2ent = p1->sub2mbpages[idx_2mb%PAGES_2MB_PER_1GB];
            ensure_2mb_subtable_lambda(p2ent);
          
            // ---- 调用 4KB setter ----
            page_size4kb_t *p4 = p2ent.sub_pages;
            _4kb_pages_state_set_lambda(pg4k_start % PAGES_4KB_PER_2MB,
                                     ent.num_of_pages,
                                     p4);
                try_fold_2mb_lambda(p2ent);
                try_fold_1gb_lambda(*p1);
            
        }
    }
    }else{
    for (int i = 0; i < 5; i++)
    {
        auto &ent = pak.entries[i];
        if (ent.num_of_pages == 0) continue;
        
        
        uint64_t pg4k_start = ent.base >> 12;

        if (ent.page_size_in_byte == _1GB_PG_SIZE)
        {
            // ===== 1GB 级 =====
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;
            for(uint64_t i=0;i<ent.num_of_pages;i++)
                {
                        page_size1gb_t*pg=top_1gb_table->get(idx_1gb+i);
                        if(pg->flags.state!=((flags.params.if_mmio)?MMIO_FREE:FREE)){
                            //直接panic
                            fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_RESULTS_CODE::FATAL_REASONS::REASON_CODE_attempt_create_new_entry_not_blackhole;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                        pg->flags.state=RESERVED;
                        pg->sub2mbpages=nullptr;
                        pg->map_count=0;
                        pg->ref_count=0;
                        top_1gb_table->release(idx_1gb+i);                        
                }
                return success;
        }
        else if (ent.page_size_in_byte == _2MB_PG_SIZE)
        {
            // ===== 2MB 级 =====
            uint64_t idx_2mb = pg4k_start / PAGES_4KB_PER_2MB;
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;
            // ---- 调用真正的 2MB setter ----
            page_size2mb_t *p2 = top_1gb_table->get(idx_1gb)->sub2mbpages;
            uint16_t real_base2mb_idx=idx_2mb % PAGES_2MB_PER_1GB;

                for(uint64_t i=0;i<ent.num_of_pages;i++)
                {
                    page_size2mb_t*pg=&p2[real_base2mb_idx+i];
                    if(pg->flags.state!=((flags.params.if_mmio)?MMIO_FREE:FREE)&&
                pg->flags.is_sub_valid){
                            //直接panic
                            fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_RESULTS_CODE::FATAL_REASONS::REASON_CODE_attempt_create_new_entry_not_blackhole;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                    }
                    pg->flags.state=RESERVED;
                    pg->sub_pages=nullptr;//按照协议这里就算有指针也是无效的
                    pg->map_count=0;
                    pg->ref_count=0;
                }
                return success;
        }
        
        else if (ent.page_size_in_byte == _4KB_PG_SIZE)
        {
            // ===== 4KB 级 =====
            uint64_t idx_2mb = pg4k_start / PAGES_4KB_PER_2MB;
            uint64_t idx_1gb = pg4k_start / PAGES_4KB_PER_1GB;
            page_size1gb_t &p1 = *top_1gb_table->get(idx_1gb);
            page_size2mb_t &p2ent = p1.sub2mbpages[idx_2mb%PAGES_2MB_PER_1GB];
            // ---- 调用 4KB setter ----
            page_size4kb_t *p4 = p2ent.sub_pages;
            uint16_t real_base4kb_idx=pg4k_start % PAGES_4KB_PER_2MB;
            for(uint64_t i=0;i<ent.num_of_pages;i++)
                {
                    page_size4kb_t*pg=&p4[real_base4kb_idx+i];
                    if(pg->flags.state!=((flags.params.if_mmio)?MMIO_FREE:FREE)&&
                    pg->flags.is_sub_valid){
                            //直接panic
                            fatal.reason = MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_RESULTS_CODE::FATAL_REASONS::REASON_CODE_attempt_create_new_entry_not_blackhole;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                    }
                    pg->flags.state=RESERVED;
                    pg->map_count=0;
                    pg->ref_count=0;
                }
                return success;         
        }
        }

    }

    return success;
}
/**
 * TODO:panic机制完善
 * TODO:KURD完善
 */
KURD_t phymemspace_mgr::dram_pages_state_set(const PHYSEG &current_seg, phyaddr_t base, uint64_t numof_4kbpgs, dram_pages_state_set_flags_t flags)
{
    auto ensure_1gb_subtable_lambda = [](uint64_t idx_1gb){
            page_size1gb_t *p1 = top_1gb_table->get(idx_1gb);
            if(p1==nullptr){
                //panic,一致性违背
            }
            if (!p1->flags.is_sub_valid)
            {
            page_size2mb_t* sub2 =alloc_2mb_subtable();//new失败里面自动panic,不用考虑空指针

            setmem(sub2, PAGES_2MB_PER_1GB * sizeof(page_size2mb_t), 0);

            p1->sub2mbpages = sub2;
            p1->flags.is_sub_valid = 1;
            p1->flags.state = NOT_ATOM;
            }
        };
    auto ensure_2mb_subtable_lambda = [](page_size2mb_t &p2){
        if (!p2.flags.is_sub_valid)
        {
            page_size4kb_t* sub4 = alloc_4kb_subtable();
            setmem(sub4, PAGES_4KB_PER_2MB * sizeof(page_size4kb_t), 0);
            p2.sub_pages = sub4;
            p2.flags.is_sub_valid = 1;
            p2.flags.state = NOT_ATOM;
            
        }
        };
    enum folds_functions_result_t:uint8_t{
        NO_FOLDED=0,
        FOLDED_FREE=1,
        FOLDED_FULL=2
    };
    auto try_fold_2mb_lambda = [](page_size2mb_t &p2) -> folds_functions_result_t {
        // 没有子表就是原子 2MB 页，无法向上折叠
        if (!p2.flags.is_sub_valid)
            return NO_FOLDED;

        bool all_free = true;
        bool all_full = true;

        for (uint16_t i = 0; i < PAGES_4KB_PER_2MB; i++)
        {
            uint8_t st = p2.sub_pages[i].flags.state;
            if (st != FREE) all_free = false;
            if (st == FREE) all_full = false;
            if (!all_free && !all_full)
                break; // 已经确定是 NOT_ATOM
        }

        if (all_free)
        {
            // 全部 free：可以删除子表，恢复为原子 FREE 2MB
            free_4kb_subtable(p2.sub_pages);
            p2.sub_pages = nullptr;
            p2.flags.is_sub_valid = 0;
            p2.flags.state = FREE;
            return FOLDED_FREE;
        }

        if (all_full)
        {
            // 全部被占用或被标为 FULL：将该 2MB 标记为 FULL
            // **注意：不删除子表**（后续可能会有引用计数变动）
            p2.flags.state = FULL;
            // 保持 p2.flags.is_sub_valid == 1
            return FOLDED_FULL;
        }

        // 混合：部分占用，标记为 NOT_ATOM，不删除子表
        p2.flags.state = NOT_ATOM;
        return NO_FOLDED;
    };

    auto try_fold_1gb_lambda = [&try_fold_2mb_lambda](page_size1gb_t &p1) -> folds_functions_result_t {
        // 没有子表就是原子 1GB 页，无法向上折叠
        if (!p1.flags.is_sub_valid)
            return NO_FOLDED;

        bool all_free = true;
        bool all_full = true;

        for (uint16_t i = 0; i < PAGES_2MB_PER_1GB; i++)
        {
            page_size2mb_t &p2 = p1.sub2mbpages[i];

            // 如果 p2 是非原子（有子表），尝试折叠它（这样可以把能 collapse 的 2MB 变为 FREE/FULL）
            if (p2.flags.is_sub_valid)
            {
                try_fold_2mb_lambda(p2);
                // try_fold_2mb 会在 all-free 或 all-used 情况下修改 p2.flags.state，
                // 且在 all_free 情况下会释放 p2.sub_pages 并把 is_sub_valid 置 0。
            }
            if (p2.flags.state != FREE) all_free = false;
            if (p2.flags.state != FULL) all_full = false;

            if (!all_free && !all_full)
                break; // 已经确定是 NOT_ATOM
        }

        if (all_free)
        {
            // 所有 2MB 都是 FREE：删除 1GB 的子表并恢复为原子 FREE
            free_2mb_subtable(p1.sub2mbpages);
            p1.sub2mbpages = nullptr;
            p1.flags.is_sub_valid = 0;
            p1.flags.state = FREE;
            return FOLDED_FREE;
        }

        if (all_full)
        {
            // 所有 2MB 都是 FULL：标记该 1GB 为 FULL
            // **注意：不删除子表**（保留子表以便未来 refcount 增加或子页操作）
            p1.flags.state =FULL;
            // 保持 p1.flags.is_sub_valid == 1
            return FOLDED_FULL;
        }

        // 混合：标记为 NOT_ATOM，保留子表
        p1.flags.state = NOT_ATOM;
        return NO_FOLDED;
    };
    KURD_t success=default_success();
    KURD_t fail=default_failure();
    KURD_t fatal=default_fatal();
    success.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_SET_DRAM;
    fail.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_SET_DRAM;
    fatal.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_SET_DRAM;
    if(numof_4kbpgs==0){
        fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FAIL_REASONS::REASON_CODE_PAGES_COUNT_ZERO;
        return fail; // 返回失败而不是什么都不做
    }
    if(current_seg.base>base||(current_seg.base+current_seg.seg_size)<(base+numof_4kbpgs*_4KB_PG_SIZE)){
        fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FAIL_REASONS::REASON_CODE_FAIL_TO_SPLIT_SEG;
        return fail; // 返回失败而不是什么都不做
    }
    seg_to_pages_info_package_t pak;
    int r = phymemseg_to_pacage(base, numof_4kbpgs, pak);
    if (r != 0) {
        fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FAIL_REASONS::REASON_CODE_FAIL_TO_SPLIT_SEG;
        return fail;
    }
    switch(flags.op){
        case dram_pages_state_set_flags_t::buddypages_regist:{
            
        for(int i=0;i<5;i++){
            auto &ent = pak.entries[i];
            switch(ent.page_size_in_byte){
                case _1GB_PG_SIZE:{
                    uint64_t entry_base_idx=ent.base>>30;
                    for(uint64_t j=0;j<ent.num_of_pages;j++){
                         page_size1gb_t* _1 = top_1gb_table->get(entry_base_idx + j); // 修正这里应该是+j而不是+i
                        if(_1==nullptr){
                            //panic,一致性违背
                        }
                        if(_1->flags.state!=FREE||_1->flags.is_belonged_to_buddy==true){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_BUDDY_REGIST_STATE_CONFILICT;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                        _1->flags.is_belonged_to_buddy=true;
                    }
                }
                break; // 添加break语句
                case _2MB_PG_SIZE:{

                    uint64_t entry_base_idx=ent.base>>30;
                    ensure_1gb_subtable_lambda(entry_base_idx);
                    page_size1gb_t *p1 = top_1gb_table->get(entry_base_idx);
                    uint16_t _2mb_off_idx=(ent.base>>21)&(PAGES_2MB_PER_1GB-1);
                    page_size2mb_t* p2 = p1->sub2mbpages+_2mb_off_idx;
                    for(uint64_t j=0;j<ent.num_of_pages;j++){

                        if((p2+j)->flags.state!=FREE||(p2+j)->flags.is_belonged_to_buddy==true){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_BUDDY_REGIST_STATE_CONFILICT;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                        (p2+j)->flags.is_belonged_to_buddy=true;
                    }
                    
                }
                break; // 添加break语句
                case _4KB_PG_SIZE:{
                    uint64_t entry_base_idx=ent.base>>30;
                    ensure_1gb_subtable_lambda(entry_base_idx);
                    page_size1gb_t *p1 = top_1gb_table->get(entry_base_idx);
                    uint16_t _2mb_off_idx=(ent.base>>21)&(PAGES_2MB_PER_1GB-1);
                    page_size2mb_t* p2 = p1->sub2mbpages+_2mb_off_idx;
                    uint16_t _4kb_off_idx=(ent.base>>12)&(PAGES_4KB_PER_2MB-1);
                    ensure_2mb_subtable_lambda(*p2);
                    page_size4kb_t* p4 = p2->sub_pages+_4kb_off_idx;
                    for(uint64_t j=0;j<ent.num_of_pages;j++){

                        if((p4+j)->flags.state!=FREE||(p4+j)->flags.is_belonged_to_buddy==true){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_BUDDY_REGIST_STATE_CONFILICT;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                        (p4+j)->flags.is_belonged_to_buddy=true;
                    }
                }
                break; // 添加break语句
                default:{
                    //panic,前面的函数不可能出现，出现了只能说明内存损坏
                    fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_BAD_PAGE_SIZE;
                    return fatal; // 添加默认情况的返回
                }
            }
        }
        }
        break; // 添加break语句
        case dram_pages_state_set_flags_t::buddypages_unregist:{
            for(int i=0;i<5;i++){
            auto &ent = pak.entries[i];
            switch(ent.page_size_in_byte){
                case _1GB_PG_SIZE:{
                    uint64_t entry_base_idx=ent.base>>30;
                    for(uint64_t j=0;j<ent.num_of_pages;j++){
                         page_size1gb_t* _1 = top_1gb_table->get(entry_base_idx + j); // 修正这里应该是+j而不是+i
                        if(_1==nullptr){
                            //panic,一致性违背
                        }
                        if(_1->flags.state!=FREE||_1->flags.is_belonged_to_buddy==false){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_BUDDY_UNREGIS_STATE_CONFILICT;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                        _1->flags.is_belonged_to_buddy=false;
                    }
                }
                break; // 添加break语句
                case _2MB_PG_SIZE:{

                    uint64_t entry_base_idx=ent.base>>30;
                    page_size1gb_t *p1 = top_1gb_table->get(entry_base_idx);
                    if(p1==nullptr){
                            //panic,一致性违背
                    }
                    uint16_t _2mb_off_idx=(ent.base>>21)&(PAGES_2MB_PER_1GB-1);
                    if(p1->sub2mbpages==nullptr){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_BUDDY_UNREGIS_SUBTB_NOT_EXSIT;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                    }
                    page_size2mb_t* p2 = p1->sub2mbpages+_2mb_off_idx;
                    for(uint64_t j=0;j<ent.num_of_pages;j++){

                        if((p2+j)->flags.state!=FREE||(p2+j)->flags.is_belonged_to_buddy==false){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_BUDDY_UNREGIS_STATE_CONFILICT;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                        (p2+j)->flags.is_belonged_to_buddy=false;
                    }
                    
                }
                break; // 添加break语句
                case _4KB_PG_SIZE:{
                    uint64_t entry_base_idx=ent.base>>30;
                    page_size1gb_t *p1 = top_1gb_table->get(entry_base_idx);
                    if(p1==nullptr){
                            //panic,一致性违背
                    }
                    uint16_t _2mb_off_idx=(ent.base>>21)&(PAGES_2MB_PER_1GB-1);
                    if(p1->sub2mbpages==nullptr){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_BUDDY_UNREGIS_SUBTB_NOT_EXSIT;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                    }
                    page_size2mb_t* p2 = p1->sub2mbpages+_2mb_off_idx;
                    uint16_t _4kb_off_idx=(ent.base>>12)&(PAGES_4KB_PER_2MB-1);
                    if(p2->sub_pages==nullptr){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_BUDDY_UNREGIS_SUBTB_NOT_EXSIT;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                    }
                    page_size4kb_t* p4 = p2->sub_pages+_4kb_off_idx;
                    for(uint64_t j=0;j<ent.num_of_pages;j++){

                        if((p4+j)->flags.state!=FREE||(p4+j)->flags.is_belonged_to_buddy==false){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_BUDDY_UNREGIS_STATE_CONFILICT;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                        (p4+j)->flags.is_belonged_to_buddy=false;
                    }
                }
                break; // 添加break语句
                default:{
                    //panic,前面的函数不可能出现，出现了只能说明内存损坏
                    fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_BAD_PAGE_SIZE;
                    return fatal; // 添加默认情况的返回
                }
            }
        }
        }
        break; // 添加break语句
        case dram_pages_state_set_flags_t::normal:{
            for(int i=0;i<5;i++){
            auto &ent = pak.entries[i];
            switch(ent.page_size_in_byte){
                case _1GB_PG_SIZE:{
                    uint64_t entry_base_idx=ent.base>>30;
                    for(uint64_t j=0;j<ent.num_of_pages;j++){
                         page_size1gb_t* _1 = top_1gb_table->get(entry_base_idx + j); // 修正这里应该是+j而不是+i
                        if(_1==nullptr){
                            //panic,一致性违背
                        }
                        if(_1->flags.is_sub_valid){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_NORMAL_ATOM_EXPECTION_VIOLATION;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                        if(flags.params.expect_meet_atom_pages_free)if(_1->flags.state!=FREE){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_NORMAL_FREE_EXPECTION_VIOLATION;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                        if(flags.params.expect_meet_buddy_pages)if(_1->flags.is_belonged_to_buddy==false){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_NORMAL_BUDDY_EXPECTION_VIOLATION;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                        _1->flags.state=flags.state;
                    }
                }
                break; // 添加break语句
                case _2MB_PG_SIZE: {
                    uint64_t entry_base_idx=ent.base>>30;
                    ensure_1gb_subtable_lambda(entry_base_idx);
                    page_size1gb_t *p1 = top_1gb_table->get(entry_base_idx);
                    uint16_t _2mb_off_idx=(ent.base>>21)&(PAGES_2MB_PER_1GB-1);
                    page_size2mb_t* p2 = p1->sub2mbpages+_2mb_off_idx;
                    for(uint64_t j=0;j<ent.num_of_pages;j++){
                        if((p2+j)->flags.is_sub_valid){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_NORMAL_ATOM_EXPECTION_VIOLATION;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                        if(flags.params.expect_meet_atom_pages_free)if((p2+j)->flags.state!=FREE){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_NORMAL_FREE_EXPECTION_VIOLATION;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                        if(flags.params.expect_meet_buddy_pages)if((p2+j)->flags.is_belonged_to_buddy==false){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_NORMAL_BUDDY_EXPECTION_VIOLATION;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                        (p2+j)->flags.state=flags.state;
                    }
                    if(!flags.params.expect_meet_atom_pages_free)try_fold_1gb_lambda(*p1);
                }
                break; // 添加break语句
                case _4KB_PG_SIZE:{
                    uint64_t entry_base_idx=ent.base>>30;
                    ensure_1gb_subtable_lambda(entry_base_idx);
                    page_size1gb_t *p1 = top_1gb_table->get(entry_base_idx);
                    uint16_t _2mb_off_idx=(ent.base>>21)&(PAGES_2MB_PER_1GB-1);
                    page_size2mb_t* p2 = p1->sub2mbpages+_2mb_off_idx;
                    uint16_t _4kb_off_idx=(ent.base>>12)&(PAGES_4KB_PER_2MB-1);
                    ensure_2mb_subtable_lambda(*p2);
                    page_size4kb_t* p4 = p2->sub_pages+_4kb_off_idx;
                    for(uint64_t j=0;j<ent.num_of_pages;j++){

                        if((p4+j)->flags.is_sub_valid){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_NORMAL_ATOM_EXPECTION_VIOLATION;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                        if(flags.params.expect_meet_atom_pages_free)if((p4+j)->flags.state!=FREE){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_NORMAL_FREE_EXPECTION_VIOLATION;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                        if(flags.params.expect_meet_buddy_pages)if((p4+j)->flags.is_belonged_to_buddy==false){
                            fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_NORMAL_BUDDY_EXPECTION_VIOLATION;
                            phymemspace_mgr::in_module_panic(fatal);
                            return fatal;
                        }
                    (p4+j)->flags.state=flags.state;
                    }
                    if(!flags.params.expect_meet_atom_pages_free)try_fold_2mb_lambda(*p2);
                    if(!flags.params.expect_meet_atom_pages_free)try_fold_1gb_lambda(*p1);
                }
                break; // 添加break语句
                default:{
                    //panic,前面的函数不可能出现，出现了只能说明内存损坏
                    fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_BAD_PAGE_SIZE;
                    return fatal; // 添加默认情况的返回
                }
            }
        }
        }
        break; // 添加break语句
    }
    
    return success; // 添加缺失的返回语句
}