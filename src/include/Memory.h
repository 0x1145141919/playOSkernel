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
    OS_ALLOCATABLE_MEMORY,
    OS_RESERVED_MEMORY,
    MEMORY_TYPE_OEM_RESERVED_MIN = 0x70000000,
  MEMORY_TYPE_OEM_RESERVED_MAX = 0x7FFFFFFF,
  MEMORY_TYPE_OS_RESERVED_MIN  = 0x80000000,
  MEMORY_TYPE_OS_RESERVED_MAX  = 0xFFFFFFFF
} PHY_MEM_TYPE;
typedef uint64_t phyaddr_t;
typedef uint64_t vaddr_t;
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
    /**
     *  上面的attribute0位是否占用，1位是否读，2位是否写，3位是否可执行，
     * 
     * 上面的约定只在phy_memDesriptor *KernelSpacePgsMemMgr::queryPhysicalMemoryUsage(phyaddr_t base, uint64_t len_in_bytes)
     * 的返回结果中有效
     */
    phy_memDesriptor*submaptable;
}  phy_memDesriptor; //PHY_MEMORY_DESCRIPTOR
class GlobalMemoryPGlevelMgr_t {
    private:
    phy_memDesriptor *rootPhyMemDscptTbBsPtr;//这个结构是后面在init函数中内部接口reclaimBootTimeMemory创建的
    EFI_MEMORY_DESCRIPTORX64 *EfiMemMap;
    uint64_t EfiMemMapEntryCount;
    uint64_t rootPhymemTbentryCount;
    uint64_t Statusflags;
    phyaddr_t max_phy_addr;
    void dirtyentrydelete(uint64_t tbid);
    bool Ismemspaceneighbors(uint16_t index_a, uint16_t index_b,uint64_t tbid);
    void reclaimBootTimeMemoryonEfiTb();
    void InitrootPhyMemDscptTbBsPtr();
    void sortEfiMemoryMapByPhysicalStart();
    void fillMemoryHolesInEfiMap();
    void reclaimLoaderMemory();
    public:
    phyaddr_t getMaxPhyaddr();
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
