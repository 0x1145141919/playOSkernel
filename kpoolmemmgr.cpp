#include "kpoolmemmgr.h"
constexpr uint16_t FirstStaticHeapMaxObjCount = 1024;
HeapObjectMeta objMetaTable_for_FirstStaticHeap[FirstStaticHeapMaxObjCount] = {0};
extern "C" {
    extern char __heap_start;
    extern char __heap_end;
}

void *kpoolmemmgr::kalloc(uint64_t size)
{
    if (flags.ableto_Expand==0)
    {
        HeapObjectMeta*infotb=first_static_heap.heap.metaInfo.objMetaTable;
        uint64_t MaxObjCount = first_static_heap.heap.metaInfo.header.objMetaMaxCount;
        uint64_t ObjCount=first_static_heap.heap.metaInfo.header.objMetaCount;
        
    }else{

    }
    
    
    
    return nullptr;
}

void kpoolmemmgr::kfree(void *ptr)
{
}

kpoolmemmgr::kpoolmemmgr()
{
    // 初始化first_static_heap
    first_static_heap.heap.heapStart = (phyaddr_t)&__heap_start;
    first_static_heap.heap.heapVStart = (vaddr_t)&__heap_start;
    first_static_heap.heap.heapSize = (uint64_t)(&__heap_end - &__heap_start);
    first_static_heap.heap.freeSize = (uint64_t)(&__heap_end - &__heap_start);
    first_static_heap.heap.status = HEAP_BLOCK_FREE;
    
    // 初始化元信息数组
    first_static_heap.heap.metaInfo.header.magic = 0x48504D41; // "HPM A"
    first_static_heap.heap.metaInfo.header.version = 1;
    first_static_heap.heap.metaInfo.header.objMetaCount = 0;
    first_static_heap.heap.metaInfo.header.objMetaMaxCount = FirstStaticHeapMaxObjCount;
    first_static_heap.heap.metaInfo.objMetaTable = objMetaTable_for_FirstStaticHeap;
    
    // 初始化链表指针
    first_static_heap.prev = nullptr;
    first_static_heap.next = nullptr;
    
    // 初始化flags结构
    // 根据文档说明，设置flags里面的位域值
    flags.ableto_Expand = 0;  // 使用0而不是false，因为这是位域
    flags.is_sorted = 0;      // 使用0而不是false，因为这是位域
    
    HCB_count = 1;
}

kpoolmemmgr::~kpoolmemmgr()
{
}