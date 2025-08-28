#pragma once
#include  <stdint.h>
#include  <efi.h>
#include  "pgtable45.h"
typedef enum {
    EFI_RESERVED_MEMORY_TYPE,
    EFI_LOADER_CODE,
    EFI_LOADER_DATA,
    EFI_BOOT_SERVICES_CODE,
    EFI_BOOT_SERVICES_DATA,
    EFI_RUNTIME_SERVICES_CODE,
    EFI_RUNTIME_SERVICES_DATA,
    freeSystemRam,
    EFI_UNUSABLE_MEMORY,
    EFI_ACPI_RECLAIM_MEMORY,
    EFI_ACPI_MEMORY_NVS,
    EFI_MEMORY_MAPPED_IO,
    EFI_MEMORY_MAPPED_IO_PORT_SPACE,
    EFI_PAL_CODE,
    EFI_PERSISTENT_MEMORY,
    EFI_UNACCEPTED_MEMORY_TYPE,
    EFI_MAX_MEMORY_TYPE,
    OS_KERNEL_DATA,
    OS_KERNEL_CODE,
    OS_KERNEL_STACK,
    ERROR_FAIL_TO_FIND,
    MEMORY_TYPE_OEM_RESERVED_MIN = 0x70000000,
  MEMORY_TYPE_OEM_RESERVED_MAX = 0x7FFFFFFF,
  MEMORY_TYPE_OS_RESERVED_MIN  = 0x80000000,
  MEMORY_TYPE_OS_RESERVED_MAX  = 0xFFFFFFFF
} PHY_MEM_TYPE;
typedef uint64_t phyaddr_t;
#define IN
#define OUT
#pragma pack(push, 8)  // 强制8字节对齐（x64 ABI要求）

typedef struct {
    UINT32     Type;          // 4字节
    union {
        UINT32 Reserved;      // 4字节（填充对齐到8字节边界）
        struct {
            UINT16 NextIndex; // 2字节（数组索引）
            UINT16 Flags;     // 2字节
        } TmpChainList;          // 命名内嵌结构体
    } ReservedUnion;          // 命名共用体
    
    EFI_PHYSICAL_ADDRESS  PhysicalStart;  // 8字节
    EFI_VIRTUAL_ADDRESS   VirtualStart;   // 8字节
    UINT64                NumberOfPages;  // 8字节
    UINT64                Attribute;      // 8字节
    UINT64                ReservedB;      // 8字节
} EFI_MEMORY_DESCRIPTORX64;

#pragma pack(pop)  // 恢复默认对齐
typedef struct phy_memDesriptor{
    UINT32                          Type;           // Field size is 32 bits followed by 32 bit pad
    union {
        UINT32 Reserved;      // 4字节（填充对齐到8字节边界）
        struct {
            UINT16 NextIndex; // 2字节（数组索引）
            UINT16 Flags;     // 2字节
        } TmpChainList;          // 命名内嵌结构体
    } ReservedUnion;          // 命名共用体
    
    EFI_PHYSICAL_ADDRESS            PhysicalStart;  // Field size is 64 bits
    EFI_VIRTUAL_ADDRESS             VirtualStart;   // Field size is 64 bits
    UINT64                          NumberOfPages;  // Field size is 64 bits
    UINT64                          Attribute;      // Field size is 64 bits
    phy_memDesriptor*submaptable;
}  phy_memDesriptor; //PHY_MEMORY_DESCRIPTOR
class GlobalMemoryPGlevelMgr_t {
    private:
    phy_memDesriptor *rootPhyMemDscptTbBsPtr;//这个结构是后面在init函数中内部接口reclaimBootTimeMemory创建的
    EFI_MEMORY_DESCRIPTORX64 *EfiMemMap;
    uint64_t EfiMemMapEntryCount;
    uint64_t rootPhymemTbentryCount;
    uint64_t Statusflags;
    void dirtyentrydelete(uint64_t tbid);
    bool Ismemspaceneighbors(uint16_t index_a, uint16_t index_b,uint64_t tbid);
    void reclaimBootTimeMemoryonEfiTb();
    void InitrootPhyMemDscptTbBsPtr();
    void sortEfiMemoryMapByPhysicalStart();
    void fillMemoryHolesInEfiMap();
    void reclaimLoaderMemory();
    public:
    void printEfiMemoryDescriptorTable();
    GlobalMemoryPGlevelMgr_t();
    GlobalMemoryPGlevelMgr_t(EFI_MEMORY_DESCRIPTORX64* gEfiMemdescriptromap, uint64_t entryCount);
    // 移除析构函数声明，因为在freestanding环境中不需要
    void Init(EFI_MEMORY_DESCRIPTORX64* gEfiMemdescriptromap, uint64_t entryCount);
    phy_memDesriptor* queryPhysicalMemoryUsage(phyaddr_t addr);
    phy_memDesriptor* getGlobalPhysicalMemoryInfo();
    uint64_t getRootPhysicalMemoryDescriptorTableEntryCount();
    int FixedPhyaddPgallocate(IN phyaddr_t addr, 
        IN uint64_t size,
        IN PHY_MEM_TYPE type);//这个函数分配的内存会在根物理描述符表中增加表项
    int defaultPhyaddPgallocate( IN OUT phyaddr_t& addr,uint64_t size,PHY_MEM_TYPE type);//默认分配器，从前往后尽可能减少内存碎片
    void* SameNeighborMerge(phyaddr_t addr);
    int pageRecycle(phyaddr_t EntryStartphyaddr);//回收起始地址为addr项的内存,若是已经合并了则不可以
    void pageSetValue(phyaddr_t EntryStartphyaddr,uint64_t value);//起始地址为addr项的内存,若是已经合并了则不可以
    void printPhyMemDesTb();
    void DisableBasicMemService();
    uint64_t getGlobalPhysicalMemoryMgrflags();
};
extern GlobalMemoryPGlevelMgr_t gBaseMemMgr;
// 位图状态定义
enum BitmapEntryState : uint8_t {
    BM_FREE        = 0x0, // 00: 完全空闲
    BM_PARTIAL     = 0x1, // 01: 部分使用(需要查下级位图)
    BM_RESERVED_PARTIAL    = 0x2, // 10: 包含不可分配内存
    BM_FULL        = 0x3  // 11: 完全分配
};

// 页表级别定义
enum PageTableLevel {
    LEVEL_4K = 0,  // 4KB页
    LEVEL_2M = 1,  // 2MB页
    LEVEL_1G = 2,  // 1GB页
    LEVEL_512G = 3,// 512GB页
    LEVEL_256T = 4 // 256TB页
};

/**
 * @brief 位图层级控制标志结构体
 * 
 * 用于标识位图层级控制器的状态类型
 */
struct PhyMemBitmapCtrloflevel_ctrlflags
{
    uint64_t type :2;  ///< 位图层级类型字段，占用2位
};

/**
 * @brief 位图层级控制器类型枚举
 * 
 * 定义位图层级控制器的三种状态类型
 */
enum BitmapctrlOflevel_types
{
    BMLV_STATU_UNUSED = 0,    ///< 未使用状态
    BMLV_STATU_STATIC = 1,    ///< 静态状态
    BMLV_STATU_DYNAMIC = 2    ///< 动态状态
};

/**
 * @brief 位图控制标志结构体
 * 
 * 用于标识物理内存位图控制器的状态
 */
struct PhyMemBitmapCtrl_ctrlflags
{
    uint64_t state :2;  ///< 控制器状态字段，占用2位
};

/**
 * @brief 物理内存位图控制器状态枚举
 * 
 * 定义物理内存位图控制器的三种状态
 */
enum PhyMemBitmapCtrl_states
{
    BM_STATU_UNINITIALIZED = 0,  ///< 未初始化状态
    BM_STATU_STATIC_ONLY = 1,    ///< 仅静态状态
    BM_STATU_DYNAMIC = 2         ///< 动态状态
};

/**
 * @brief 物理内存位图层级控制结构体
 * 
 * 管理特定层级的物理内存位图控制信息
 */
struct SubPhymemBitmapControler;
    struct PhyMemBitmapCtrloflevel {
        union 
        {
            uint8_t* bitmap;  ///< 位图数据指针
            SubPhymemBitmapControler* subbitmaphashtb; ///< 同级其它静态表查找的哈希表
        }base;
        
        uint64_t entryCount;      ///< 位图/哈希表项数量
        uint64_t managedSize;     ///< 每个位图项管理的内存大小
        PhyMemBitmapCtrloflevel_ctrlflags flags; ///< 位图控制器有未使用，静态，动态这几种类型
        PageTableLevel level;     ///< 当前层级
    };

/**
 * @brief 子物理内存位图控制器结构体
 * 
 * 用于管理特定内存页基址的位图控制器，支持哈希冲突时的链表结构
 */
    struct SubPhymemBitmapControler
    {
        phyaddr_t pgbase;              ///< 页基址
        PhyMemBitmapCtrloflevel ctrler; ///< 位图控制器
        PhyMemBitmapCtrloflevel* next;  ///< 如果哈希冲突采用链表法
    };
    
/**
 * @brief 位图空闲内存管理器类
 * 
 * 基于位图的物理内存分配器，用于内核初始化时的页级内存分配
 * 其主要任务是在系统启动时为内核分配页级内存并建立页表
 */
class BitmapFreeMemmgr_t {
    private:
    uint8_t CpuPglevel;              ///< 当前CPU的页表级别
    PhyMemBitmapCtrloflevel AllLvsBitmapCtrl[5]={0}; ///< 所有层级的位图控制结构数组
    uint8_t  mainbitmaplv;                  ///< 主位图层级
    uint8_t subbitmaplv;                    ///< 子位图层级
    PhyMemBitmapCtrl_ctrlflags statuflags;                    ///< 状态标志
    uint64_t maxphyaddr;                    ///< 最大物理地址
    uint8_t GetBitmapEntryState(uint8_t lv, uint64_t index);
    int BitmaplvctrlsInit(uint64_t entryCount[5]);                      ///< 位图初始化函数
    void StaticBitmapInit(uint8_t lv);
    void phymementry_to_4kbbitmap(phy_memDesriptor*gphymemtbbase,uint16_t index,uint8_t*bitmapbase);
    int SetBitmapentryiesmulty(uint8_t lv, uint64_t start_index,uint64_t numofEntries, uint8_t state);//设置从某一个引索开始的连续多个位图项
    public:
    BitmapFreeMemmgr_t();
    void PrintBitmapInfo();
    /**
     * @brief 初始化位图内存管理器
     * 
     * 设置并初始化位图内存管理器的各项参数和数据结构
     */
    void Init();
};
extern BitmapFreeMemmgr_t gBitmapFreePhyMemPGmgr;