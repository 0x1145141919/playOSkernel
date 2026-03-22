#include "memory/kpoolmemmgr.h"
#include "memory/all_pages_arr.h"
#include "memory/AddresSpace.h"
#include "memory/FreePagesAllocator.h"
#include "panic.h"
#include "util/OS_utils.h"
// 重载全局 new/delete 操作符
namespace MEMMODULE_LOCAIONS{
    namespace OUT_SRFACES_EVENTS{
        uint8_t EVENT_CODE_PAGES_VALLOC=0;
        uint8_t EVENT_CODE_PAGES_VFREE=1;
        namespace PAGES_VFREE_RESULTS{
            namespace FAIL_REASONS{
                uint16_t REMOVE_VMENTRY_FAIL=1;

            };
        };
        uint8_t EVENT_CODE_PAGES_ALLOC=2;
        uint8_t EVENT_CODE_PAGES_FREE=3;
        uint8_t EVENT_CODE_KEYWORD_NEW=4;
        uint8_t EVENT_CODE_KEYWORD_DELETE=5;
    }
}
#ifdef KERNEL_MODE
void* __wrapped_pgs_valloc(KURD_t*kurd_out,uint64_t _4kbpgscount, page_state_t TYPE, uint8_t alignment_log2) {
    spinlock_guard guard(kspace_pagetable_modify_lock);
    Alloc_result result=FreePagesAllocator::alloc(
        _4kbpgscount*0x1000,
        buddy_alloc_params{
            .numa=0,
            .try_lock_always_try=0,
            .align_log2=alignment_log2
        },
        TYPE
    );
    if(result.base==0||result.result.result!=result_code::SUCCESS){
        //尝试用phymemspace_mgr::pages_linear_scan_and_alloc
            *kurd_out=result.result;

            return nullptr;
    }
    vaddr_t vbase=kspace_vm_table->alloc_available_space(_4kbpgscount*0x1000,result.base%0x400000000);
    if(vbase==0){
        //回滚FreePagesAllocator::alloc

        *kurd_out=result.result;
        return nullptr;
    }
    vm_interval interval={
        .vbase=vbase,
        .pbase=result.base,
        .size=_4kbpgscount*0x1000,
        .access=KspacePageTable::PG_RW
    };
    *kurd_out=KspacePageTable::enable_VMentry(interval);

    return (void*)vbase;
}
vaddr_t stack_alloc(KURD_t *kurd_out, uint64_t _4kbpgscount)
{
    if(_4kbpgscount==0)return 0;
    vaddr_t stack_top=(vaddr_t)__wrapped_pgs_valloc(kurd_out,_4kbpgscount,page_state_t::kernel_pinned,12);
    return stack_top+(_4kbpgscount-1)*0x1000;
}
KURD_t __wrapped_pgs_vfree(void*vbase,uint64_t _4kbpgscount){
    phyaddr_t pbase=0;
    KURD_t status=KspacePageTable::v_to_phyaddrtraslation((vaddr_t)vbase,pbase);
    if(status.result!=result_code::SUCCESS){
        return status;
    }
    status=FreePagesAllocator::free(pbase,_4kbpgscount*0x1000);
    vm_interval interval={
        .vbase=(vaddr_t)vbase,
        .pbase=pbase,
        .size=_4kbpgscount*0x1000,
        .access=KspacePageTable::PG_RW
    };
    spinlock_guard guard(kspace_pagetable_modify_lock);
    int result= kspace_vm_table->remove((vaddr_t)vbase);
    if(result!=0){
        return KURD_t(result_code::FAIL,
            MEMMODULE_LOCAIONS::OUT_SRFACES_EVENTS::PAGES_VFREE_RESULTS::FAIL_REASONS::REMOVE_VMENTRY_FAIL,
            module_code::MEMORY,
            MEMMODULE_LOCAIONS::LOCATION_CODE_OUT_SURFACES,
            MEMMODULE_LOCAIONS::OUT_SRFACES_EVENTS::EVENT_CODE_PAGES_VFREE,
            level_code::ERROR,
            err_domain::CORE_MODULE
        );
    }
    return KspacePageTable::disable_VMentry(interval);
     

}

void* __wrapped_heap_alloc(uint64_t size,KURD_t*kurd,alloc_flags_t flags) {
    return kpoolmemmgr_t::kalloc(size,*kurd,flags);
}
void __wrapped_heap_free(void*addr){
    kpoolmemmgr_t::kfree(addr);
}
void* __wrapped_heap_realloc(void*addr,uint64_t size,KURD_t*kurd,alloc_flags_t flags) {
    return kpoolmemmgr_t::realloc(addr,*kurd, size, flags);
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
