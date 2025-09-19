#pragma once
#include "stdint.h"
//#include <new>
typedef uint64_t size_t;
typedef uint64_t phyaddr_t;
typedef uint64_t vaddr_t;

// HeapBlockStatus结构体位域定义
struct HeapBlockStatus{
    uint8_t block_exist:1;
    uint8_t block_tb_full:1;
    uint8_t block_full:1;
    uint8_t block_reserved:1;//堆保留不参与分配
    uint8_t block_merged:1;
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
// 堆对象元信息v2

struct HeapObjectMetav2
{
    uint32_t offset_in_heap;
    uint32_t size;
    KernelObjType type;
    uint8_t reserved[3];
};

// 堆元信息数组控制头
struct HeapMetaArrayHeader {
    uint32_t magic;          // 魔数标识(0x48504D41) "HPM A"
    uint32_t version;
    uint64_t objMetaCount;   // 对象元信息总数
    uint32_t objMetaMaxCount; // 最大对象元信息数量,决定HeapObjectMeta*objMetaTable有多少项
};
// 完整的堆元信息数组结构
/**
 * objMetaTable中的项目按照引索从小到大增加，物理地址也是从小到大增加的，
 * 相邻引索项之间没有空洞，每一块空闲内存一个空闲内存块来表示
 * 引索objMetaCount前面没有无效的项
 */
struct HeapMetaInfoArray {
    HeapMetaArrayHeader header;          
    HeapObjectMetav2*objMetaTable; 
};

// 堆控制块(HCB) - 管理一段连续堆内存的元信息

/**
 *  *  * 内核映像自带堆起始地址由连接器确定,如果是动态分配则需要2mb对齐
 * 内核映像自带堆大小4mb，若果是动态分配按照2mb大小的整数倍,如此一来容易让整个堆映射到高位空间
 * 也就是说如果堆对象有虚拟地址，其虚拟地址为堆对象在堆中偏移量加上hcb定义的堆起始虚拟基址
 * 物理地址也是从堆起始物理地址加上偏移量
*/
 
struct HeapControlBlock {
    phyaddr_t heapStart;      // 堆起始物理地址(8字节),
    vaddr_t heapVStart;       // 堆起始虚拟地址(8字节)
    uint64_t heapSize;        // 堆总大小(字节)(8字节)，
    uint64_t freeSize;        // 剩余可用大小(字节)(8字节)
    HeapMetaInfoArray metaInfo;
    HeapBlockStatus status;   // 当前堆状态(1字节)
    uint8_t  reserved[7];    // 保留字段(对齐到8字节)

};
class HCB
{
private:
    /* data */
public:
    HCB(/* args */);
    ~HCB();
};



struct kpoolmemmgr_flags_t
   {
   uint64_t ableto_Expand :1;
   uint64_t heap_vaddr_enabled :1;
   uint64_t alignment:8;
   };





struct HCB_chainlist_node
{
   HeapControlBlock heap;
   HCB_chainlist_node* prev;
   HCB_chainlist_node* next;
};
struct search_result
{
   int32_t index;
   bool if_exist;
};
class kpoolmemmgr_t
{
private:
   
   kpoolmemmgr_flags_t kpoolmemmgr_flags;              // 添加flags成员变量
   uint64_t HCB_count;
    HCB_chainlist_node first_static_heap;
    HCB_chainlist_node*last_heap_node;
    int addr_to_HCB_MetaInfotb_Index(HCB_chainlist_node HNode,uint8_t* addr);
   uint8_t* is_space_available(HCB_chainlist_node Heap,uint8_t* addr,uint64_t size); 

public:
   void print_hcb_status(HCB_chainlist_node* node); // 打印单个HCB状态
   void print_meta_table(HCB_chainlist_node* node); // 打印HCB的元信息表
   void print_all_hcb_status();                     // 打印所有HCB状态
   void*kalloc(uint64_t size,bool vaddraquire=false,uint8_t alignment=3);
   void*realloc(void*ptr,uint64_t size);//根据表在基地址不变的情况下尝试修改堆对象大小
   //向上增加地址可能失败
   void clear(void*ptr);// 主要用于结构体清理内存，new一个结构体后用这个函数根据传入的起始地址查找堆的元信息表项，并把该元信息项对应的内存空间全部写0
   //别用这个清理new之后的对象
   void Init();
   HCB_chainlist_node*getFirst_static_heap();
   kpoolmemmgr_flags_t getkpoolmemmgr_flags();
   int mgr_vaddr_enabled();
   void kfree(void*ptr);
    kpoolmemmgr_t();
    ~kpoolmemmgr_t();
};
constexpr int INDEX_NOT_EXIST = -100;
extern kpoolmemmgr_t gKpoolmemmgr;
// 全局 new/delete 操作符重载声明
void* operator new(size_t size);
void* operator new(size_t size, bool vaddraquire, uint8_t alignment = 3);
void* operator new[](size_t size);
void* operator new[](size_t size, bool vaddraquire, uint8_t alignment = 3);
void operator delete(void* ptr) noexcept;
void operator delete(void* ptr, size_t) noexcept;
void operator delete[](void* ptr) noexcept;
void operator delete[](void* ptr, size_t) noexcept;

// 放置 new 操作符
void* operator new(size_t, void* ptr) noexcept;
void* operator new[](size_t, void* ptr) noexcept;