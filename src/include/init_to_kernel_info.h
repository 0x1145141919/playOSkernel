#pragma once
#include "stdint.h"
#include "memory/Memory.h"
#include "../init/include/initEntryPointDefinitions.h"
#include "../init/include/kernel_mmu.h"
// ============================================
// 内存类型枚举与常量定义 (与 kernel.elf 保持一致)
// ============================================

// BSP 初始栈：32KB, 对齐 4KB(2^12)
constexpr uint32_t VM_ID_BSP_INIT_STACK = 0x1001;
constexpr uint64_t BSP_INIT_STACK_SIZE = 32 * 1024;           // 32KB
constexpr uint8_t BSP_INIT_STACK_ALIGN_LOG2 = 12;             // 4KB 对齐

// 第一堆：4MB, 对齐 2MB(2^21)
constexpr uint32_t VM_ID_FIRST_HEAP = 0x1003;
constexpr uint64_t FIRST_HEAP_SIZE_CONST = 4 * 1024 * 1024;   // 4MB
constexpr uint8_t FIRST_HEAP_ALIGN_LOG2 = 21;     
// 第一堆位图：FIRST_HEAP_SIZE/128, 对齐 4KB(2^12)
// FIRST_HEAP_SIZE 在 init_linker_symbols.h 中定义为 2MB，所以位图大小 = 2MB/128 = 16KB
constexpr uint32_t VM_ID_FIRST_HEAP_BITMAP = 0x1002;
constexpr uint64_t FIRST_HEAP_BITMAP_SIZE = FIRST_HEAP_SIZE_CONST/128;        // 16KB
constexpr uint8_t FIRST_HEAP_BITMAP_ALIGN_LOG2 = 12;          // 4KB 对齐

            // 2MB 对齐

// 日志缓冲区：4MB, 对齐 2MB(2^21)
constexpr uint32_t VM_ID_LOGBUFFER = 0x1004;
constexpr uint64_t LOGBUFFER_SIZE = 4 * 1024 * 1024;          // 4MB
constexpr uint8_t LOGBUFFER_ALIGN_LOG2 = 21;                  // 2MB 对齐

// BCB 相关常量
constexpr uint8_t FIRST_BCB_MAX_ORDER = 20;
constexpr uint32_t FIRST_BCB_SIZE = 2*((1ULL << (FIRST_BCB_MAX_ORDER+12))/(4096*8));

// BCB 位图：FIRST_BCB_SIZE, 对齐 4KB(2^12)
constexpr uint32_t VM_ID_FIRST_BCB_BITMAP = 0x1005;
constexpr uint64_t FIRST_BCB_BITMAP_SIZE = FIRST_BCB_SIZE;
constexpr uint8_t FIRST_BCB_BITMAP_ALIGN_LOG2 = 12;           // 4KB 对齐

constexpr uint32_t VM_ID_KSYMBOLS = 0x1006;
// 上层内核空间页目录指针表：1MB, 对齐 4KB(2^12)
//0x2000~0x2fff是x86_64_PGLV4的架构锁死的传递的VM
constexpr uint32_t VM_ID_UP_KSPACE_PDPT = 0x2001;
constexpr uint32_t VM_ID_GRAPHIC_BUFFER = 0x2002;
constexpr uint64_t UP_KSPACE_PDPT_SIZE = 1 * 1024 * 1024;     // 1MB
constexpr uint8_t UP_KSPACE_PDPT_ALIGN_LOG2 = 12;             // 4KB 对齐


struct phymem_segment {
    phyaddr_t start;
    uint64_t size;
    PHY_MEM_TYPE type;
};
struct loaded_VM_interval {

    phyaddr_t pbase;
    vaddr_t vbase;
    uint64_t size;
    uint32_t VM_interval_specifyid;
    pgaccess access;
};
struct init_to_kernel_info {
    uint64_t magic;
    uint64_t self_pages_count;

    EFI_SYSTEM_TABLE* gST_ptr;

    uint64_t ksymbols_file_size;
    phyaddr_t kmmu_root_table;
    phymem_segment kmmu_interval;

    uint64_t phymem_segment_count;
    phymem_segment* memory_map;
    uint64_t loaded_VM_interval_count;
    loaded_VM_interval* loaded_VM_intervals;
    uint64_t pass_through_device_info_count;
    pass_through_device_info* pass_through_devices;
};
struct load_kernel_info_pack{
    kernel_mmu*kmmu;
    loaded_file_entry kernel_file_entry;
    uint64_t VM_entry_count;
    loaded_VM_interval* VM_entries;
    vaddr_t entry_vaddr;
    vaddr_t stack_bottom;
};