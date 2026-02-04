#include "memory/kpoolmemmgr.h"
#include "memory/phygpsmemmgr.h"
#include "memory/AddresSpace.h"
#include "memory/FreePagesAllocator.h"
#include "panic.h"
#include "util/OS_utils.h"
// 重载全局 new/delete 操作符
namespace MEMMODULE_LOCAIONS{
    namespace OUT_SRFACES_EVENTS{
        uint8_t EVENT_CODE_PAGES_VALLOC=0;
        uint8_t EVENT_CODE_PAGES_VFREE=1;
        uint8_t EVENT_CODE_PAGES_ALLOC=2;
        uint8_t EVENT_CODE_PAGES_FREE=3;
        uint8_t EVENT_CODE_KEYWORD_NEW=4;
        uint8_t EVENT_CODE_KEYWORD_DELETE=5;
    }
}
phyaddr_t __wrapped_pgs_alloc(KURD_t *kurd_out, uint64_t _4kbpgscount, page_state_t TYPE, uint8_t alignment_log2)
{
    
    Alloc_result result=FreePagesAllocator::alloc(_4kbpgscount*0x1000, alloc_params{.numa=0,.flags_bits=0,.align_log2=alignment_log2});
    if(result.base==0||result.result.result!=result_code::SUCCESS){
        //尝试用phymemspace_mgr::pages_linear_scan_and_alloc
        Alloc_result result={0};
        //result.base=phymemspace_mgr::pages_linear_scan_and_alloc(_4kbpgscount,result.result,TYPE,alignment_log2);
        if(result.base==0||result.result.result!=result_code::SUCCESS){
            *kurd_out=result.result;
            return 0;
        }
    }else{
        KURD_t kurd=phymemspace_mgr::pages_dram_buddy_pages_set(
            result.base,
            _4kbpgscount,
            TYPE
        );
        if(kurd.result!=result_code::SUCCESS){
            *kurd_out=kurd;
            return 0;
        }
    }
    return result.base;
}

KURD_t __wrapped_pgs_free(phyaddr_t phybase, uint64_t _4kbpgscount)
{
    KURD_t kurd=FreePagesAllocator::free(phybase,_4kbpgscount*0x1000);
    /*if(!success_all_kurd(kurd)){
        return phymemspace_mgr::pages_recycle(phybase,_4kbpgscount);
    }*/
    kurd=phymemspace_mgr::pages_dram_buddy_pages_set(
            phybase,
            _4kbpgscount,
            FREE
        );
    return kurd;
}


#ifdef KERNEL_MODE
void* __wrapped_heap_alloc(uint64_t size,KURD_t*kurd,alloc_flags_t flags) {
    return kpoolmemmgr_t::kalloc(size,*kurd,flags);
}
void __wrapped_heap_free(void*addr){
    kpoolmemmgr_t::kfree(addr);
}
void* __wrapped_heap_realloc(void*addr,uint64_t size,KURD_t*kurd,alloc_flags_t flags) {
    return kpoolmemmgr_t::realloc(addr,*kurd, size, flags);
}
void* __wrapped_pgs_valloc(KURD_t*kurd_out,uint64_t _4kbpgscount, page_state_t TYPE, uint8_t alignment_log2) {
    
    Alloc_result result=FreePagesAllocator::alloc(_4kbpgscount*0x1000, alloc_params{.numa=0,.flags_bits=0,.align_log2=alignment_log2});
    if(result.base==0||result.result.result!=result_code::SUCCESS){
        //尝试用phymemspace_mgr::pages_linear_scan_and_alloc
        result.base=phymemspace_mgr::pages_linear_scan_and_alloc(_4kbpgscount,result.result,TYPE,alignment_log2);
        if(result.base==0||result.result.result!=result_code::SUCCESS){
            *kurd_out=result.result;
            return nullptr;
        }
    }else{
        KURD_t kurd=phymemspace_mgr::pages_dram_buddy_pages_set(
            result.base,
            _4kbpgscount,
            TYPE
        );
        if(kurd.result!=result_code::SUCCESS){
            kurd=result.result;
            return nullptr;
        }
    }
    vaddr_t vbase=(vaddr_t)KspaceMapMgr::pgs_remapp(*kurd_out,result.base, _4kbpgscount*0x1000, KspaceMapMgr::PG_RWX);
    return (void*)vbase;
}
KURD_t __wrapped_pgs_vfree(void*vbase,uint64_t _4kbpgscount){
    phyaddr_t pbase=0;
    KURD_t status=KspaceMapMgr::v_to_phyaddrtraslation((vaddr_t)vbase,pbase);
    if(status.result!=result_code::SUCCESS)return status;
    KspaceMapMgr::pgs_remapped_free((vaddr_t)vbase);
    KURD_t kurd=FreePagesAllocator::free(pbase,_4kbpgscount*0x1000);
    if(kurd.result!=result_code::SUCCESS){
        return phymemspace_mgr::pages_recycle(pbase,_4kbpgscount);
    }
    return kurd;
}
void* operator new(size_t size) {
    KURD_t kurd;
    void* result= __wrapped_heap_alloc(size,&kurd,default_flags);
    if(error_kurd(kurd)||result==nullptr){
        panic_info_inshort inshort={
            .is_bug=false,
            .is_policy=true,
            .is_hw_fault=false,
            .is_mem_corruption=false,
            .is_escalated=false
        };
        Panic::panic(
            default_panic_behaviors_flags,
            "new operator failed",
            nullptr,
            &inshort,
            kurd
        );
    }
    return result;
}

void *operator new(size_t size, alloc_flags_t flags)
{
    KURD_t kurd;
    void* result= __wrapped_heap_alloc(size,&kurd,flags);
    if(error_kurd(kurd)||result==nullptr){
        panic_info_inshort inshort={
            .is_bug=false,
            .is_policy=true,
            .is_hw_fault=false,
            .is_mem_corruption=false,
            .is_escalated=false
        };
        Panic::panic(
            default_panic_behaviors_flags,
            "new operator failed",
            nullptr,
            &inshort,
            kurd
        );
    }
    return result;
}
void *operator new[](size_t size)
{
    KURD_t kurd;
    void* result= kpoolmemmgr_t::kalloc(size,kurd,default_flags);
    if(error_kurd(kurd)||result==nullptr){
        panic_info_inshort inshort={
            .is_bug=false,
            .is_policy=true,
            .is_hw_fault=false,
            .is_mem_corruption=false,
            .is_escalated=false
        };
        Panic::panic(
            default_panic_behaviors_flags,
            "new operator failed",
            nullptr,
            &inshort,
            kurd
        );
    }
    return result;
}

void *operator new[](size_t size, alloc_flags_t flags)
{
    KURD_t kurd;
    void* result= __wrapped_heap_alloc(size,&kurd,flags);
    if(error_kurd(kurd)||result==nullptr){
        panic_info_inshort inshort={
            .is_bug=false,
            .is_policy=true,
            .is_hw_fault=false,
            .is_mem_corruption=false,
            .is_escalated=false
        };
        Panic::panic(
            default_panic_behaviors_flags,
            "new operator failed",
            nullptr,
            &inshort,
            kurd
        );
    }
    return result;
}

void* operator new[](size_t size, bool vaddraquire, uint8_t alignment) {
    KURD_t kurd;
    void* result= kpoolmemmgr_t::kalloc(size,kurd, default_flags);
    if(error_kurd(kurd)||result==nullptr){
        panic_info_inshort inshort={
            .is_bug=false,
            .is_policy=true,
            .is_hw_fault=false,
            .is_mem_corruption=false,
            .is_escalated=false
        };
        Panic::panic(
            default_panic_behaviors_flags,
            "new operator failed",
            nullptr,
            &inshort,
            kurd
        );
    }
    return result;
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
#endif