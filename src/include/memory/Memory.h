#pragma once
#include  <stdint.h>
#include  <efi.h>
#include "pgtable45.h"
#include "devices_typeids.h"
typedef enum :uint32_t{
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
    OS_HARDWARE_GRAPHIC_BUFFER,
    ERROR_FAIL_TO_FIND,
    OS_ALLOCATABLE_MEMORY,
    OS_RESERVED_MEMORY,
    OS_PGTB_SEGS,
    OS_MEMSEG_HOLE,
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
    PHY_MEM_TYPE     Type;          // 4字节
    uint32_t ReservedA;
    
    EFI_PHYSICAL_ADDRESS  PhysicalStart;  // 8字节
    EFI_VIRTUAL_ADDRESS   VirtualStart;   // 8字节
    UINT64                NumberOfPages;  // 8字节
    UINT64                Attribute;      // 8字节
    UINT64                ReservedB;      // 8字节
} EFI_MEMORY_DESCRIPTORX64;
enum cache_strategy_t:uint8_t
{
    UC=0,
    WC=1,
    WT=4,
    WP=5,
    WB=6,
    UC_minus=7
};
struct cache_table_idx_struct_t
{
    uint8_t PWT:1;
    uint8_t PCD:1;
    uint8_t PAT:1;
};
enum vinterval_base_same_congruence_level{
    congruence_level_4kb,
    congruence_level_2mb,
    congruence_level_1gb
};
struct seg_to_pages_info_pakage_t{
   vinterval_base_same_congruence_level congruence_level;
        struct pages_info_t{
           vaddr_t vbase;
            phyaddr_t phybase;
           uint64_t page_size_in_byte;
           uint64_t num_of_pages;
    };
    pages_info_t entryies[5];//里面的地址顺序是无序的
    
    /**
     * 清空所有条目数据
     */
    void clear() {
        congruence_level = congruence_level_4kb;
        for (uint32_t i = 0; i < 5; i++) {
            entryies[i].vbase = 0;
            entryies[i].phybase = 0;
            entryies[i].page_size_in_byte = 0;
            entryies[i].num_of_pages = 0;
        }
    }
};
struct shared_inval_VMentry_info_t{
    seg_to_pages_info_pakage_t info_package;
    bool is_package_valid;
    uint32_t completed_processors_count;
};
union ia32_pat_t
{
   uint64_t value;
   cache_strategy_t  mapped_entry[8];
};
struct pgaccess
{
    uint8_t is_kernel:1;
    uint8_t is_writeable:1;
    uint8_t is_readable:1;
    uint8_t is_executable:1;
    uint8_t is_global:1;
    cache_strategy_t cache_strategy;
};
constexpr pgaccess KSPACE_RW_ACCESS={
    .is_kernel=1,
    .is_writeable=1,
    .is_readable=1,
    .is_executable=0,
    .is_global=1,
    .cache_strategy=WB
};
struct vphypair_t
{//三个参数至少4k对齐
    vaddr_t vaddr;
    phyaddr_t paddr;
    uint32_t size;
};

struct VM_DESC
{
    vaddr_t start;    // inclusive
    vaddr_t end;      // exclusive
                      // 区间长度 = end - start
    enum map_type_t : uint8_t {
        MAP_NONE = 0,     // 未分配物理页（仅占位）
        MAP_PHYSICAL,     // 连续物理页,只有内核因为立即要求而使用，用户空间不能用
        MAP_FILE,         // 文件映射
        MAP_ANON,          // 匿名映射（默认用户空间）
    } map_type;
    phyaddr_t phys_start;  // 当 map_type=MAP_PHYSICAL 时有效
                           // MAP_NONE 没有意义
    pgaccess access;       // 页权限/缓存策略
    uint8_t committed_full:1;   // 物理页是否完全已经分配（lazy allocation 用）
    uint8_t is_vaddr_alloced:1;    // 虚拟地址是否由地址空间管理器分配（否则为固定映射）
    uint8_t is_out_bound_protective:1; // 是否有越界保护区,只有is_vaddr_alloced为1的bit此位才有意义，
    uint64_t SEG_SIZE_ONLY_UES_IN_BASIC_SEG;
};
#pragma pack(pop)  // 恢复默认对齐
typedef struct phy_memDescriptor{
    PHY_MEM_TYPE                          Type;           // Field size is 32 bits followed by 32 bit pad
    uint32_t remapped_count;
    
    EFI_PHYSICAL_ADDRESS            PhysicalStart;  // Field size is 64 bits
    EFI_VIRTUAL_ADDRESS             VirtualStart;   // Field size is 64 bits
    UINT64                          NumberOfPages;  // Field size is 64 bits
    UINT64                          Attribute;      // Field size is 64 bits
    /**
     *  上面的attribute0位是否占用，1位是否读，2位是否写，3位是否可执行，
     * 
     * 上面的约定只在phy_memDesriptor *KspaceMapMgr::queryPhysicalMemoryUsage(phyaddr_t base, uint64_t len_in_bytes)
     * 的返回结果中有效
     */
    phy_memDescriptor*submaptable;
}  phy_memDescriptor; //PHY_MEMORY_DESCRIPTOR
constexpr uint64_t MAX_PHYADDR_1GB_PGS_COUNT=4096;
static_assert((MAX_PHYADDR_1GB_PGS_COUNT&(MAX_PHYADDR_1GB_PGS_COUNT-1))==0,"MAX_PHYADDR_1GB_PGS_COUNT must be power of 2");
static_assert((MAX_PHYADDR_1GB_PGS_COUNT>512),"MAX_PHYADDR_1GB_PGS_COUNT must be greater than 512");

/**
 * @brief 通用函数：将虚拟 - 物理地址区间按照页面大小拆分为多个条目
 * 
 * 根据虚拟地址和物理地址的同余关系（congruence），智能地将区间拆分为 1GB/2MB/4KB 的页面组合。
 * 拆分策略优先使用大页面以减少 TLB 条目数，边界不对齐部分使用小页面填充。
 * 
 * @param result 输出参数，存储拆分后的页面信息包
 * @param vmentry VM 描述符，包含虚拟地址和物理地址信息
 * @return int OS_SUCCESS 成功，其他错误码见 os_error_definitions.h
 * 
 * @note 该函数不依赖任何类实例，可在任何上下文中调用
 * @note 要求 vmentry 的 start, end, phys_start 都必须 4KB 对齐
 */
int vm_interval_to_pages_info(seg_to_pages_info_pakage_t &result, VM_DESC vmentry);

/**
 * @brief 通用函数：将虚拟 - 物理地址区间按照页面大小拆分为多个条目
 * 
 * 根据虚拟地址和物理地址的同余关系（congruence），智能地将区间拆分为 1GB/2MB/4KB 的页面组合。
 * 拆分策略优先使用大页面以减少 TLB 条目数，边界不对齐部分使用小页面填充。
 * 
 * @param result 输出参数，存储拆分后的页面信息包
 * @param vbase 虚拟地址起始（必须 4KB 对齐）
 * @param vend 虚拟地址结束（必须 4KB 对齐，开区间）
 * @param pbase 物理地址起始（必须 4KB 对齐）
 * @return int OS_SUCCESS 成功，其他错误码见 os_error_definitions.h
 * 
 * @note 该函数不依赖任何类实例，可在任何上下文中调用
 * @note 要求 vbase, vend, pbase 都必须 4KB 对齐
 */
