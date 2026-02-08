
#pragma once
#include <stdint.h>
#include <efi.h>
#include "memory/Memory.h"
#include "core_hardwares/primitive_gop.h"
typedef struct {
    uint32_t signature;          // 标识符，例如 'BOOTINFO'
    uint32_t total_size;         // 整个数据结构的总大小
    uint16_t version;            // 结构版本

    // 各字段的大小
    uint16_t memory_map_entry_count;
    uint16_t memory_map_entry_size;
    uint16_t graphic_info_size;
    uint32_t memory_map_size;
    // 各字段的偏移量(相对于结构起始地址)
    GlobalBasicGraphicInfoType graphic_metainfo;
    EFI_SYSTEM_TABLE* gST_ptr;

    EFI_MEMORY_DESCRIPTORX64* memory_map_ptr;    
    uint64_t ksymbols_table_phy_ptr;
    uint32_t ksymbols_entry_count;
    uint32_t ksymbols_entry_size;

    uint32_t mapversion;        // 内存映射版本// 其他元数据
    uint64_t flags;
    uint64_t checksum;           // 可选的数据校验
} BootInfoHeader;
//int (*kernel_start)(BootInfoHeader* header);