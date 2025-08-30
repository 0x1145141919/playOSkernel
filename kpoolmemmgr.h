#include "Memory.h"

enum HeapBlockStatus : uint8_t {
    HEAP_BLOCK_FREE = 0,      // 空闲块
    HEAP_BLOCK_USED = 1,      // 已使用块
    HEAP_BLOCK_RESERVED = 2,  // 保留块(不可分配)
    HEAP_BLOCK_MERGED = 3     // 已合并块(特殊状态)
};
enum KernelObjType : uint8_t {
    OBJ_TYPE_INVALID = 0,
    OBJ_TYPE_TASK = 1,
    OBJ_TYPE_THREAD = 2,
    OBJ_TYPE_MUTEX = 3,
    OBJ_TYPE_SEMAPHORE = 4,
    OBJ_TYPE_EVENT = 5,
    OBJ_TYPE_QUEUE = 6,
    OBJ_TYPE_TIMER = 7,
    OBJ_TYPE_DEVICE = 8,
    OBJ_TYPE_FILE = 9,
    OBJ_TYPE_SHM = 10,
    OBJ_TYPE_NORMAL = 11,
    OBJ_TYPE_FREE = 12,
    OBJ_TYPE_FIXED=13,
    OBJ_TYPE_MAX = 255
};
// 堆对象元信息
struct HeapObjectMeta {
    phyaddr_t base;          // 对象物理基址(8字节)
    vaddr_t vbase;           // 对象虚拟基址(8字节)
    uint64_t size;           // 对象大小(字节)(8字节)
    KernelObjType type;      // 对象类型(1字节)
    uint8_t  reserved[3];
    uint32_t checksum;       // 校验和(4字节)
};
// 堆元信息数组控制头
struct HeapMetaArrayHeader {
    uint32_t magic;          // 魔数标识(0x48504D41) "HPM A"
    uint32_t version;
    uint32_t objMetaCount;   // 对象元信息总数
    uint32_t objMetaMaxCount; // 最大对象元信息数量,决定HeapObjectMeta*objMetaTable有多少项
};
// 完整的堆元信息数组结构
struct HeapMetaInfoArray {
    HeapMetaArrayHeader header;          
    HeapObjectMeta*objMetaTable; 
};

// 堆控制块(HCB) - 管理一段连续堆内存的元信息
struct HeapControlBlock {
    phyaddr_t heapStart;      // 堆起始物理地址(8字节),内核映像自带堆起始地址由连接器确定
    vaddr_t heapVStart;       // 堆起始虚拟地址(8字节)
    uint64_t heapSize;        // 堆总大小(字节)(8字节)，内核映像自带堆大小4mb
    uint64_t freeSize;        // 剩余可用大小(字节)(8字节)
    HeapMetaInfoArray metaInfo;
    HeapBlockStatus status;   // 当前堆状态(1字节)
    uint8_t  reserved[7];    // 保留字段(对齐到8字节)

};






struct HCB_chainlist_node
{
   HeapControlBlock heap;
   HCB_chainlist_node* prev;
   HCB_chainlist_node* next;
};

class kpoolmemmgr
{
private:
   struct flags
   {
   uint64_t ableto_Expand :1;
   uint64_t is_sorted:1;
   };
   flags flags;              // 添加flags成员变量
   uint64_t HCB_count;
    HCB_chainlist_node first_static_heap;
public:
   void*kalloc(uint64_t size);
   void kfree(void*ptr);
    kpoolmemmgr();
    ~kpoolmemmgr();
};