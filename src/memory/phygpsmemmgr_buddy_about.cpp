#include "memory/phygpsmemmgr.h"
#include "util/OS_utils.h"
#include "memory/kpoolmemmgr.h"
#include "util/kout.h"
#ifdef KERNEL_MODE
#include "util/kptrace.h"
#endif
KURD_t phymemspace_mgr::pages_dram_buddy_regist(phyaddr_t phybase, uint64_t numof_4kbpgs)
{
    KURD_t fail=default_failure();
    module_global_lock.lock();
    PHYSEG seg=get_physeg_by_addr(phybase);
    if(seg.type!=DRAM_SEG){
        module_global_lock.unlock();
        //fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_RESULTS_CODE::FAIL_REASONS::REASON_CODE_DRAMSEG_NOT_EXIST;
    }
    dram_pages_state_set_flags_t flags={
        .state=FREE,
        .op=dram_pages_state_set_flags_t::buddypages_regist,
        .params{
            .expect_meet_atom_pages_free=true,
            .expect_meet_buddy_pages=false,
            .if_init_ref_count=true
        }
    };
    KURD_t status= dram_pages_state_set(
        seg,phybase,numof_4kbpgs,flags
    );
    module_global_lock.unlock();
    return status;
}
KURD_t phymemspace_mgr::pages_dram_buddy_unregist(phyaddr_t phybase, uint64_t numof_4kbpgs)
{
    KURD_t fail=default_failure();
    module_global_lock.lock();
    PHYSEG seg=get_physeg_by_addr(phybase);
    if(seg.type!=DRAM_SEG){
        module_global_lock.unlock();
        //fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_RESULTS_CODE::FAIL_REASONS::REASON_CODE_DRAMSEG_NOT_EXIST;
    }
    dram_pages_state_set_flags_t flags={
        .state=FREE,
        .op=dram_pages_state_set_flags_t::buddypages_regist,
        .params{
            .expect_meet_atom_pages_free=true,
            .expect_meet_buddy_pages=true,
            .if_init_ref_count=false,
        }
        
    };
    KURD_t status= dram_pages_state_set(
        seg,phybase,numof_4kbpgs,flags
    );
    module_global_lock.unlock();
    return status;
}

KURD_t phymemspace_mgr::pages_dram_buddy_pages_set(phyaddr_t phybase, uint64_t numof_4kbpgs, page_state_t state)
{
    KURD_t fail=default_failure();
    module_global_lock.lock();
    PHYSEG seg=get_physeg_by_addr(phybase);
    if(seg.type!=DRAM_SEG){
        module_global_lock.unlock();
        //fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_RESULTS_CODE::FAIL_REASONS::REASON_CODE_DRAMSEG_NOT_EXIST;
    }
    if(state==KERNEL||state==USER_ANONYMOUS||state==USER_FILE||state==DMA){

    }
    dram_pages_state_set_flags_t flags={
        .state=state,
        .op=dram_pages_state_set_flags_t::normal,
        .params{
            .expect_meet_atom_pages_free=false,
            .expect_meet_buddy_pages=true,
            .if_init_ref_count=false,
        }
        
    };
    KURD_t status= dram_pages_state_set(
        seg,phybase,numof_4kbpgs,flags
    );
    module_global_lock.unlock();
    return status;
}

KURD_t phymemspace_mgr::pages_dram_buddy_pages_free(phyaddr_t phybase, uint64_t numof_4kbpgs)
{
    KURD_t fail=default_failure();
    module_global_lock.lock();
    PHYSEG seg=get_physeg_by_addr(phybase);
    if(seg.type!=DRAM_SEG){
        module_global_lock.unlock();
        //fail.reason=MEMMODULE_LOCAIONS::PHYMEMSPACE_MGR_EVENTS_CODE::PAGES_RECYCLE_RESULTS_CODE::FAIL_REASONS::REASON_CODE_DRAMSEG_NOT_EXIST;
    }
    page_state_t state=FREE;
    KURD_t status=pages_recycle_verify(
        phybase,numof_4kbpgs,state
    );
    if(status.result!=result_code::SUCCESS){
        module_global_lock.unlock();
        return status;
    }
    dram_pages_state_set_flags_t flags={
        .state=state,
        .op=dram_pages_state_set_flags_t::normal,
        .params{
            .expect_meet_atom_pages_free=false,
            .expect_meet_buddy_pages=true,
            .if_init_ref_count=true,
        }
        
    };
    status= dram_pages_state_set(
        seg,phybase,numof_4kbpgs,flags
    );
    module_global_lock.unlock();
    return status;
}
