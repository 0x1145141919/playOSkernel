#include "memory/phygpsmemmgr.h"
#include "util/OS_utils.h"
#include "memory/kpoolmemmgr.h"
#include "util/kout.h"
#ifdef KERNEL_MODE
#include "util/kptrace.h"
#endif
static constexpr uint64_t PAGES_4KB_PER_2MB = 512;
static constexpr uint64_t PAGES_2MB_PER_1GB = 512;
static constexpr uint64_t PAGES_4KB_PER_1GB = PAGES_4KB_PER_2MB * PAGES_2MB_PER_1GB;
KURD_t phymemspace_mgr::pages_dram_buddy_regist(phyaddr_t phybase, uint64_t numof_4kbpgs)
{
    KURD_t fail=default_failure();
    module_global_lock.lock();
    KURD_t contain=KURD_t();
    PHYSEG*seg=physeg_list->get_seg_by_addr(phybase,contain);
    if(seg->type!=DRAM_SEG){
        module_global_lock.unlock();
        //fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_RESULTS_CODE::FAIL_REASONS::REASON_CODE_DRAMSEG_NOT_EXIST;
    }
    dram_pages_state_set_flags_t flags={
        .state=FREE,
        .op=dram_pages_state_set_flags_t::buddypages_regist,
        .params{
            .if_init_ref_count=true
        }
    };
    KURD_t status= dram_pages_state_set(
        *seg,phybase,numof_4kbpgs,flags
    );
    module_global_lock.unlock();
    return status;
}
KURD_t phymemspace_mgr::pages_dram_buddy_unregist(phyaddr_t phybase, uint64_t numof_4kbpgs)
{
    KURD_t fail=default_failure();
    module_global_lock.lock();
    KURD_t contain=KURD_t();
    PHYSEG*seg=physeg_list->get_seg_by_addr(phybase,contain);
    if(!seg || seg->type!=DRAM_SEG){
        module_global_lock.unlock();
        //fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_RESULTS_CODE::FAIL_REASONS::REASON_CODE_DRAMSEG_NOT_EXIST;
    }
    dram_pages_state_set_flags_t flags={
        .state=FREE,
        .op=dram_pages_state_set_flags_t::buddypages_regist,
        .params{
            .if_init_ref_count=false,
        }
        
    };
    KURD_t status= dram_pages_state_set(
        *seg,phybase,numof_4kbpgs,flags
    );
    module_global_lock.unlock();
    return status;
}

KURD_t phymemspace_mgr::pages_dram_buddy_pages_set(phyaddr_t phybase, uint64_t numof_4kbpgs, page_state_t state)
{
    KURD_t success=default_success();
    KURD_t fail=default_failure();
    KURD_t fatal=default_fatal();
    success.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_SET_DRAM;
    fail.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_SET_DRAM;
    fatal.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_SET_DRAM;
    if(numof_4kbpgs==0){
        fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FAIL_REASONS::REASON_CODE_PAGES_COUNT_ZERO;
        return fail;
    }
    module_global_lock.lock();
    KURD_t contain=KURD_t();
    PHYSEG*seg=physeg_list->get_seg_by_addr(phybase,contain);
    if(!seg || seg->type!=DRAM_SEG){
        module_global_lock.unlock();
        fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FAIL_REASONS::REASON_CODE_DRAMSEG_NOT_EXIST;
        return fail;
    }
    KURD_t status;
    if(state==KERNEL||state==USER_ANONYMOUS||state==USER_FILE||state==DMA||state==FREE){//只允许设置这几种用于dram的类型
        dram_pages_state_set_flags_t flags={
        .state=state,
        .op=dram_pages_state_set_flags_t::normal,
        .params{
            .if_init_ref_count=true,
        }
        
    };
    status= dram_pages_state_set(
        *seg,phybase,numof_4kbpgs,flags
    );
    module_global_lock.unlock();
    if(status.event_code==0){
        status.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_SET_DRAM;
    }
    return status;
    }
    //非法类型，报错
    module_global_lock.unlock();
    fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FAIL_REASONS::REASON_CODE_INVALID_STATE;
    return fail;
    
    
    
}

KURD_t phymemspace_mgr::pages_dram_buddy_pages_free(phyaddr_t phybase, uint64_t numof_4kbpgs)
{
    KURD_t success=default_success();
    KURD_t fail=default_failure();
    KURD_t fatal=default_fatal();
    success.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_SET_DRAM;
    fail.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_SET_DRAM;
    fatal.event_code=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::EVENT_CODE_PAGES_SET_DRAM;
    if(numof_4kbpgs==0){
        fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FAIL_REASONS::REASON_CODE_PAGES_COUNT_ZERO;
        return fail;
    }
    module_global_lock.lock();
    KURD_t contain=KURD_t();
    PHYSEG*seg=physeg_list->get_seg_by_addr(phybase,contain);
    if(!seg || seg->type!=DRAM_SEG){
        module_global_lock.unlock();
        fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FAIL_REASONS::REASON_CODE_DRAMSEG_NOT_EXIST;
        return fail;
    }
    if(seg->base>phybase||(seg->base+seg->seg_size)<(phybase+numof_4kbpgs*_4KB_PG_SIZE)){
        module_global_lock.unlock();
        fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FAIL_REASONS::REASON_CODE_FAIL_TO_SPLIT_SEG;
        return fail;
    }
    seg_to_pages_info_package_t pak;
    int r = phymemseg_to_pacage(phybase, numof_4kbpgs, pak);
    if (r != 0) {
        module_global_lock.unlock();
        fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FAIL_REASONS::REASON_CODE_FAIL_TO_SPLIT_SEG;
        return fail;
    }
    enum folds_functions_result_t:uint8_t{
        NO_FOLDED=0,
        FOLDED_FREE=1,
        FOLDED_FULL=2
    };
    auto try_fold_2mb_lambda = [](page_size2mb_t &p2) -> folds_functions_result_t {
        if (!p2.flags.is_sub_valid)
            return NO_FOLDED;
        bool all_free = true;
        bool all_full = true;
        bool all_budy = true;
        for (uint16_t i = 0; i < PAGES_4KB_PER_2MB; i++)
        {
            uint8_t st = p2.sub_pages[i].flags.state;
            if (st != FREE) all_free = false;
            if (st == FREE) all_full = false;
            if(!p2.sub_pages[i].flags.is_belonged_to_buddy)all_budy=false;
            if (!all_free && !all_full)
                break;
        }
        if (all_free)
        {
            free_4kb_subtable(p2.sub_pages);
            p2.sub_pages = nullptr;
            p2.flags.is_sub_valid = 0;
            p2.flags.state = FREE;
            p2.flags.is_belonged_to_buddy = all_budy;
            return FOLDED_FREE;
        }
        if (all_full)
        {
            p2.flags.state = FULL;
            return FOLDED_FULL;
        }
        p2.flags.state = NOT_ATOM;
        return NO_FOLDED;
    };
    auto try_fold_1gb_lambda = [&try_fold_2mb_lambda](page_size1gb_t &p1) -> folds_functions_result_t {
        if (!p1.flags.is_sub_valid)
            return NO_FOLDED;
        bool all_free = true;
        bool all_full = true;
        bool all_budy = true;
        for (uint16_t i = 0; i < PAGES_2MB_PER_1GB; i++)
        {
            page_size2mb_t &p2 = p1.sub2mbpages[i];
            if (p2.flags.is_sub_valid)
            {
                try_fold_2mb_lambda(p2);
            }
            if (p2.flags.state != FREE) all_free = false;
            if (p2.flags.state != FULL) all_full = false;
            if(!p2.flags.is_belonged_to_buddy)all_budy=false;
            if (!all_free && !all_full)
                break;
        }
        if (all_free)
        {
            free_2mb_subtable(p1.sub2mbpages);
            p1.sub2mbpages = nullptr;
            p1.flags.is_sub_valid = 0;
            p1.flags.state = FREE;
            p1.flags.is_belonged_to_buddy = all_budy;
            return FOLDED_FREE;
        }
        if (all_full)
        {
            p1.flags.state = FULL;
            return FOLDED_FULL;
        }
        p1.flags.state = NOT_ATOM;
        return NO_FOLDED;
    };
    auto is_allowed_free_state = [](page_state_t st) {
        return (st==KERNEL||st==USER_ANONYMOUS||st==USER_FILE||st==DMA);
    };
    auto check_and_prepare_free = [&](page_state_t st, uint32_t ref_count, uint32_t map_count) -> KURD_t {
        if(!is_allowed_free_state(st)){
            KURD_t k=fail;
            k.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FAIL_REASONS::REASON_CODE_FREE_STATE_NOT_ALLOWED;
            return k;
        }
        if(map_count!=0){
            KURD_t k=fail;
            k.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FAIL_REASONS::REASON_CODE_FREE_MAPCOUNT_NOT_ZERO;
            return k;
        }
        if(ref_count!=1){
            KURD_t k=fail;
            k.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FAIL_REASONS::REASON_CODE_FREE_REFCNT_NOT_ONE;
            return k;
        }
        return success;
    };
    for(int i=0;i<5;i++){
        auto &ent = pak.entries[i];
        switch(ent.page_size_in_byte){
            case _1GB_PG_SIZE:{
                if(ent.num_of_pages==0)break;
                uint64_t entry_base_idx=ent.base>>30;
                for(uint64_t j=0;j<ent.num_of_pages;j++){
                    page_size1gb_t* _1 = top_1gb_table->get(entry_base_idx + j);
                    if(_1==nullptr){
                        fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_TOP1GB_ENABLE_FAIL;
                        phymemspace_mgr::in_module_panic(fatal);
                        module_global_lock.unlock();
                        return fatal;
                    }
                    if(_1->flags.is_sub_valid){
                        fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_NORMAL_ATOM_EXPECTION_VIOLATION;
                        phymemspace_mgr::in_module_panic(fatal);
                        module_global_lock.unlock();
                        return fatal;
                    }
                    if(_1->flags.is_belonged_to_buddy==false){
                        fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_NORMAL_BUDDY_EXPECTION_VIOLATION;
                        phymemspace_mgr::in_module_panic(fatal);
                        module_global_lock.unlock();
                        return fatal;
                    }
                    KURD_t chk = check_and_prepare_free(_1->flags.state, _1->ref_count, _1->map_count);
                    if(!success_all_kurd(chk)){
                        module_global_lock.unlock();
                        return chk;
                    }
                    _1->flags.state=FREE;
                    _1->ref_count=0;
                    _1->map_count=0;
                }
            }
            break;
            case _2MB_PG_SIZE:{
                if(ent.num_of_pages==0)break;
                uint64_t entry_base_idx=ent.base>>30;
                page_size1gb_t *p1 = top_1gb_table->get(entry_base_idx);
                if(p1==nullptr||p1->sub2mbpages==nullptr){
                    fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_BUDDY_UNREGIS_SUBTB_NOT_EXSIT;
                    phymemspace_mgr::in_module_panic(fatal);
                    module_global_lock.unlock();
                    return fatal;
                }
                uint16_t _2mb_off_idx=(ent.base>>21)&(PAGES_2MB_PER_1GB-1);
                page_size2mb_t* p2 = p1->sub2mbpages+_2mb_off_idx;
                for(uint64_t j=0;j<ent.num_of_pages;j++){
                    if((p2+j)->flags.is_sub_valid){
                        fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_NORMAL_ATOM_EXPECTION_VIOLATION;
                        phymemspace_mgr::in_module_panic(fatal);
                        module_global_lock.unlock();
                        return fatal;
                    }
                    if((p2+j)->flags.is_belonged_to_buddy==false){
                        fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_NORMAL_BUDDY_EXPECTION_VIOLATION;
                        phymemspace_mgr::in_module_panic(fatal);
                        module_global_lock.unlock();
                        return fatal;
                    }
                    KURD_t chk = check_and_prepare_free((p2+j)->flags.state, (p2+j)->ref_count, (p2+j)->map_count);
                    if(!success_all_kurd(chk)){
                        module_global_lock.unlock();
                        return chk;
                    }
                    (p2+j)->flags.state=FREE;
                    (p2+j)->ref_count=0;
                    (p2+j)->map_count=0;
                }
                try_fold_1gb_lambda(*p1);
            }
            break;
            case _4KB_PG_SIZE:{
                if(ent.num_of_pages==0)break;
                uint64_t entry_base_idx=ent.base>>30;
                page_size1gb_t *p1 = top_1gb_table->get(entry_base_idx);
                if(p1==nullptr||p1->sub2mbpages==nullptr){
                    fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_BUDDY_UNREGIS_SUBTB_NOT_EXSIT;
                    phymemspace_mgr::in_module_panic(fatal);
                    module_global_lock.unlock();
                    return fatal;
                }
                uint16_t _2mb_off_idx=(ent.base>>21)&(PAGES_2MB_PER_1GB-1);
                page_size2mb_t* p2 = p1->sub2mbpages+_2mb_off_idx;
                if(p2->sub_pages==nullptr){
                    fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_BUDDY_UNREGIS_SUBTB_NOT_EXSIT;
                    phymemspace_mgr::in_module_panic(fatal);
                    module_global_lock.unlock();
                    return fatal;
                }
                uint16_t _4kb_off_idx=(ent.base>>12)&(PAGES_4KB_PER_2MB-1);
                page_size4kb_t* p4 = p2->sub_pages+_4kb_off_idx;
                for(uint64_t j=0;j<ent.num_of_pages;j++){
                    if((p4+j)->flags.is_belonged_to_buddy==false){
                        fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_WHEN_NORMAL_BUDDY_EXPECTION_VIOLATION;
                        phymemspace_mgr::in_module_panic(fatal);
                        module_global_lock.unlock();
                        return fatal;
                    }
                    KURD_t chk = check_and_prepare_free((p4+j)->flags.state, (p4+j)->ref_count, (p4+j)->map_count);
                    if(!success_all_kurd(chk)){
                        module_global_lock.unlock();
                        return chk;
                    }
                    (p4+j)->flags.state=FREE;
                    (p4+j)->ref_count=0;
                    (p4+j)->map_count=0;
                }
                try_fold_2mb_lambda(*p2);
                try_fold_1gb_lambda(*p1);
            }
            break;
            case 0:
            break;
            default:{
                fatal.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_SET_DRAM_RESULTS_CODE::FATAL_REASONS::REASON_CODE_BAD_PAGE_SIZE;
                phymemspace_mgr::in_module_panic(fatal);
                module_global_lock.unlock();
                return fatal;
            }
        }
    }
    module_global_lock.unlock();
    return success;
}
