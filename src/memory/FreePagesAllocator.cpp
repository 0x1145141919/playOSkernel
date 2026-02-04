#include "memory/FreePagesAllocator.h"
#include "memory/phygpsmemmgr.h"
FreePagesAllocator::flags_t FreePagesAllocator::flags;

KURD_t FreePagesAllocator::Init()
{
    flags.allow_new_BCB=false;
    KURD_t kurd=phymemspace_mgr::pages_dram_buddy_regist(
        0x100000000,0x100000
    );
    if(error_kurd(kurd))return kurd;
    first_BCB=new free_pages_in_seg_control_block(
        0x100000000,
        20
    );
    return first_BCB->second_stage_init();
}
Alloc_result FreePagesAllocator::alloc(uint64_t size, alloc_params params)
{
    KURD_t kurd=KURD_t();
    phyaddr_t base=0;
    if(size==0)
    {

    }
    if(flags.allow_new_BCB){

    }
    base=first_BCB->allocate_buddy_way(size,kurd);
    return Alloc_result{base,kurd};
}
KURD_t FreePagesAllocator::free(phyaddr_t base, uint64_t size)
{
    if(flags.allow_new_BCB){

    }
    KURD_t kurd=first_BCB->free_buddy_way(base,size);
    return kurd;
}
void FreePagesAllocator::enable_new_BCB_allow()
{
    flags.allow_new_BCB=true;
}