#include "memory/kpoolmemmgr.h"
#include "memory/phygpsmemmgr.h"
#include "memory/AddresSpace.h"
#include "util/OS_utils.h"
// 重载全局 new/delete 操作符

void* __wrapped_pgs_valloc(uint64_t _4kbpgscount, phymemspace_mgr::page_state_t TYPE, uint8_t alignment_log2) {
    phyaddr_t phybase=gPhyPgsMemMgr.pages_alloc(_4kbpgscount, TYPE, alignment_log2);
    if(phybase==0||phybase&0x1000)return nullptr;
    vaddr_t vbase=(vaddr_t)KspaceMapMgr::pgs_remapp(phybase, _4kbpgscount*0x1000, KspaceMapMgr::PG_RWX);
    return (void*)vbase;
}
int __wrapped_pgs_free(void*vbase,uint64_t _4kbpgscount){
    phyaddr_t pbase=0;
    int status=KspaceMapMgr::v_to_phyaddrtraslation((vaddr_t)vbase,pbase);
    if(status!=OS_SUCCESS)return OS_MEMORY_FREE_FAULT;
    return gPhyPgsMemMgr.pages_recycle(pbase,_4kbpgscount);
}
void* __wrapped_heap_alloc(uint64_t size, bool is_longtime, bool vaddraquire, uint8_t alignment) {
    return kpoolmemmgr_t::kalloc(size,is_longtime, vaddraquire, alignment);
}
void __wrapped_heap_free(void*addr){
    kpoolmemmgr_t::kfree(addr);
}
void* __wrapped_heap_realloc(void*addr,uint64_t size,bool vaddraquire,uint8_t alignment) {
    return kpoolmemmgr_t::realloc(addr, size, vaddraquire, alignment);
}
void* operator new(size_t size) {

    return __wrapped_heap_alloc(size,false, true, 4);

}
void* operator new(size_t size, bool vaddraquire, uint8_t alignment) {

        return kpoolmemmgr_t::kalloc(size, vaddraquire, alignment);

}

void* operator new[](size_t size) {

    return kpoolmemmgr_t::kalloc(size,false, true, 4);
 
}

void* operator new[](size_t size, bool vaddraquire, uint8_t alignment) {

        return kpoolmemmgr_t::kalloc(size, vaddraquire, alignment);

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