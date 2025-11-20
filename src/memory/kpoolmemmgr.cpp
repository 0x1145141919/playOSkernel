#include "memory/Memory.h"
#include "OS_utils.h"
#include "os_error_definitions.h"
#include "VideoDriver.h"
#include "panic.h"
#include "memory/kpoolmemmgr.h"
#ifdef TEST_MODE
#include "stdlib.h"
#endif  

// 定义全局变量gKpoolmemmgr
kpoolmemmgr_t gKpoolmemmgr;

void kpoolmemmgr_t::enable_new_hcb_alloc()
{
    is_able_to_alloc_new_hcb=true;
}

void *kpoolmemmgr_t::kalloc(uint64_t size, bool is_longtime, bool vaddraquire, uint8_t alignment)
{
    if(this!=&gKpoolmemmgr) return nullptr;
    void*ptr;
    if(is_able_to_alloc_new_hcb)//先尝试在现核心的堆中分配，再尝试建立新堆，最后再次借用别人的堆
    { 

    }else{//只能在first_linekd_heap中分配
        int status=first_linekd_heap.in_heap_alloc(ptr,size,is_longtime,vaddraquire,alignment);
        if(status==OS_SUCCESS)
        {
            return ptr;
        }
    }
    return nullptr;
}

void *kpoolmemmgr_t::realloc(void *ptr, uint64_t size,bool vaddraquire,uint8_t alignment)
{
    if(this!=&gKpoolmemmgr) return nullptr;
    void*old_ptr=ptr;
    int status=0;
    if(is_able_to_alloc_new_hcb)
    {

    }else{
        status= first_linekd_heap.in_heap_realloc(ptr,size,vaddraquire,alignment);
        if(status==OS_HEAP_OBJ_DESTROYED)//这个分支直接内核恐慌
        {
            kputsSecure("kpoolmemmgr_t::realloc:first_linekd_heap.in_heap_realloc() return OS_HEAP_OBJ_DESTROYED\n when reallocating at address 0x");
            kpnumSecure(&ptr,UNHEX,8);
            gkernelPanicManager.panic("\nInit error,first_linekd_heap has been destroyed\n");

        }
    }
    return status==OS_SUCCESS?ptr:nullptr;
}

void kpoolmemmgr_t::clear(void *ptr)
{
    if(this!=&gKpoolmemmgr) return;
    if(is_able_to_alloc_new_hcb)
    {

    }else{
        int status=first_linekd_heap.clear(ptr);
        if(status==OS_HEAP_OBJ_DESTROYED){
            kputsSecure("kpoolmemmgr_t::realloc:first_linekd_heap.in_heap_realloc() return OS_HEAP_OBJ_DESTROYED\n when clearing at address 0x");
            kpnumSecure(&ptr,UNHEX,8);
            gkernelPanicManager.panic("\nInit error,first_linekd_heap has been destroyed\n");
        }
    }
}

int kpoolmemmgr_t::Init()
{
    if(this!=&gKpoolmemmgr) return OS_BAD_FUNCTION;
    is_able_to_alloc_new_hcb=false;
    first_linekd_heap.first_linekd_heap_Init();
    for(uint32_t i=0;i<HCB_ARRAY_MAX_COUNT;i++)
    {
        HCB_ARRAY[i]=nullptr;
    }
}

void kpoolmemmgr_t::kfree(void *ptr)
{
    if(this!=&gKpoolmemmgr) return;
    if(is_able_to_alloc_new_hcb)
    {

    }else{
        int status=first_linekd_heap.free(ptr);
        if(status==OS_HEAP_OBJ_DESTROYED)
        {
             kputsSecure("kpoolmemmgr_t::realloc:first_linekd_heap.in_heap_realloc() return OS_HEAP_OBJ_DESTROYED\n when freeing at address 0x");
            kpnumSecure(&ptr,UNHEX,8);
            gkernelPanicManager.panic("\nInit error,first_linekd_heap has been destroyed\n");
        }
    }
}

kpoolmemmgr_t::~kpoolmemmgr_t()
{
}
// 重载全局 new/delete 操作符
void* operator new(size_t size) {
    return gKpoolmemmgr.kalloc(size,false, true, 4);
}

void* operator new(size_t size, bool vaddraquire, uint8_t alignment) {
    return gKpoolmemmgr.kalloc(size, vaddraquire, alignment);
}

void* operator new[](size_t size) {
    return gKpoolmemmgr.kalloc(size,false,true, 3);
}

void* operator new[](size_t size, bool vaddraquire, uint8_t alignment) {
    return gKpoolmemmgr.kalloc(size, vaddraquire, alignment);
}

void operator delete(void* ptr) noexcept {
    gKpoolmemmgr.kfree(ptr);
}

void operator delete(void* ptr, size_t) noexcept {
    gKpoolmemmgr.kfree(ptr);
}

void operator delete[](void* ptr) noexcept {
    gKpoolmemmgr.kfree(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    gKpoolmemmgr.kfree(ptr);
}

// 放置 new 操作符
void* operator new(size_t, void* ptr) noexcept {
    return ptr;
}

void* operator new[](size_t, void* ptr) noexcept {
    return ptr;
}