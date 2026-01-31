#include "memory/kpoolmemmgr.h"
#include "memory/phygpsmemmgr.h"
#include "memory/AddresSpace.h"
#include "memory/FreePagesAllocator.h"
#include "util/OS_utils.h"
// 重载全局 new/delete 操作符

void* __wrapped_pgs_valloc(KURD_t*kurd,uint64_t _4kbpgscount, page_state_t TYPE, uint8_t alignment_log2) {
    
    Alloc_result result=FreePagesAllocator::alloc(_4kbpgscount*0x1000, alloc_params{.numa=0,.flags_bits=0,.align_log2=alignment_log2});
    if(result.base==0||result.result.result!=result_code::SUCCESS){
        //尝试用phymemspace_mgr::pages_linear_scan_and_alloc
        result.base=phymemspace_mgr::pages_linear_scan_and_alloc(_4kbpgscount,result.result,TYPE,alignment_log2);
        if(result.base==0||result.result.result!=result_code::SUCCESS){
            //fail
        }
    }else{
        KURD_t kurd=phymemspace_mgr::pages_dram_buddy_pages_set(
            result.base,
            _4kbpgscount,
            TYPE
        );
        if(kurd.result!=result_code::SUCCESS){
            //fail
        }
    }
    vaddr_t vbase=(vaddr_t)KspaceMapMgr::pgs_remapp(result.base, _4kbpgscount*0x1000, KspaceMapMgr::PG_RWX);
    return (void*)vbase;
}
KURD_t __wrapped_pgs_free(void*vbase,uint64_t _4kbpgscount){
    phyaddr_t pbase=0;
    KURD_t status=KspaceMapMgr::v_to_phyaddrtraslation((vaddr_t)vbase,pbase);
    if(status.result!=result_code::SUCCESS)return OS_MEMORY_FREE_FAULT;
    KspaceMapMgr::pgs_remapped_free((vaddr_t)vbase);
    KURD_t kurd=FreePagesAllocator::free(pbase,_4kbpgscount);
    if(kurd.result!=result_code::SUCCESS){
        return phymemspace_mgr::pages_recycle(pbase,_4kbpgscount);
    }
    return kurd;
}
void* __wrapped_heap_alloc(uint64_t size,alloc_flags_t flags) {
    KURD_t kurd;
    return kpoolmemmgr_t::kalloc(size,kurd,flags);
}
void __wrapped_heap_free(void*addr){
    kpoolmemmgr_t::kfree(addr);
}
void* __wrapped_heap_realloc(void*addr,uint64_t size,alloc_flags_t flags) {
    KURD_t kurd;
    return kpoolmemmgr_t::realloc(addr,kurd, size, flags);
}
void* operator new(size_t size) {
    return __wrapped_heap_alloc(size,default_flags);
}

void *operator new(size_t size, alloc_flags_t flags)
{
    return __wrapped_heap_alloc(size,flags);
}
void *operator new[](size_t size)
{

    return kpoolmemmgr_t::kalloc(size,default_flags);
}

void *operator new[](size_t size, alloc_flags_t flags)
{
    return __wrapped_heap_alloc(size,flags);
}

void* operator new[](size_t size, bool vaddraquire, uint8_t alignment) {

        return kpoolmemmgr_t::kalloc(size, default_flags);

}

void operator delete(void* ptr) noexcept {
    kpoolmemmgr_t::kfree(ptr);
}

void operator delete(void* ptr, size_t) noexcept {

}

void operator delete[](void* ptr) noexcept {
    kpoolmemmgr_t::kfree(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {

}

// 放置 new 操作符
void* operator new(size_t, void* ptr) noexcept {
    return ptr;
}

void* operator new[](size_t, void* ptr) noexcept {
    return ptr;
}