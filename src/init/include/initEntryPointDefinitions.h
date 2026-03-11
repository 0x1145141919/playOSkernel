
#pragma once
#include <stdint.h>
#include <efi.h>
#include <memory/Memory.h>
#define LOADED_FILE_ENTRY_TYPE_ELF_REAL_LOAD 0x01
#define LOADED_FILE_ENTRY_TYPE_BINARY 0x02
#define LOADED_FILE_ENTRY_TYPE_ELF_NO_LOAD 0x03//不加载的elf文件，内核主体
#define PASS_THROUGH_DEVICE_GRAPHICS_INFO 0x0001
struct pass_through_device_info {
    uint16_t device_info;
    void* specify_data;
};
struct loaded_file_entry {
    char file_name[256];//文件系统路径
    uint64_t file_size; //文件大小
    uint16_t file_type;//file_type文件类型，是elf可执行文件还是裸二进制普通文件
    void* raw_data; //文件内容的原始指针，loader负责加载到内存中，内核负责解析,LOADED_FILE_ENTRY_TYPE_ELF_REAL_LOAD这里是nullptr(是按图索骥不是连续地址)
};

typedef struct {
    uint64_t signature;          // 标识符，例如 'BOOTINFO'
    uint32_t total_size;         // 整个数据结构的总大小
    uint32_t total_pages_count; // 包含内存映射和加载文件等信息所占用的总页数
    uint16_t version;            // 结构版本

    uint64_t loaded_file_count;     // 加载的文件数量
    struct loaded_file_entry* loaded_files; // 加载的文件列表指针
    // 各字段的大小
    uint16_t memory_map_entry_count;
    uint16_t memory_map_entry_size;
    uint32_t memory_map_size;
    uint64_t memory_map_version;
    EFI_MEMORY_DESCRIPTORX64* memory_map_ptr;

    uint64_t pass_through_device_info_count;
    struct pass_through_device_info* pass_through_devices; // 传递到内核的设备的数组
    
    EFI_SYSTEM_TABLE* gST_ptr;
    char parameter_area[512]; // 预留的参数区域，可以根据需要调整大小
    uint64_t flags;
    uint64_t checksum;           // 可选的数据校验
} BootInfoHeader;
//int (*kernel_start)(BootInfoHeader* header);