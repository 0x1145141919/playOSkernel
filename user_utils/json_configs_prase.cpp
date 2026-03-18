#include "uutils/json_prasers.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <abi/boot.h>
/**
 * @brief 将字符串类型的内存类型转换为 PHY_MEM_TYPE 枚举值
 * 
 * @param type_str 内存类型字符串，如 "ConventionalMemory", "Reserved" 等
 * @return PHY_MEM_TYPE 对应的枚举值
 * 
 * @note 支持的类型映射：
 * - ConventionalMemory -> freeSystemRam
 * - Reserved -> EFI_RESERVED_MEMORY_TYPE
 * - RuntimeServicesCode -> EFI_RUNTIME_SERVICES_CODE
 * - RuntimeServicesData -> EFI_RUNTIME_SERVICES_DATA
 * - BootServicesCode -> EFI_BOOT_SERVICES_CODE
 * - BootServicesData -> EFI_BOOT_SERVICES_DATA
 * - ACPIReclaimMemory -> EFI_ACPI_RECLAIM_MEMORY
 * - ACPIMemoryNVS -> EFI_ACPI_MEMORY_NVS
 * - MemoryMappedIO -> EFI_MEMORY_MAPPED_IO
 * - LoaderCode -> EFI_LOADER_CODE
 * - LoaderData -> EFI_LOADER_DATA
 * - OsReserved -> OS_RESERVED_MEMORY
 * - MemSegHole -> OS_MEMSEG_HOLE
 */
static PHY_MEM_TYPE parse_memory_type(const std::string& type_str) {
    if (type_str == "ConventionalMemory") {
        return freeSystemRam;
    } else if (type_str == "freeSystemRam") {
        return freeSystemRam;
    } else if (type_str == "Reserved") {
        return EFI_RESERVED_MEMORY_TYPE;
    } else if (type_str == "RuntimeServicesCode") {
        return EFI_RUNTIME_SERVICES_CODE;
    } else if (type_str == "RuntimeServicesData") {
        return EFI_RUNTIME_SERVICES_DATA;
    } else if (type_str == "BootServicesCode") {
        return EFI_BOOT_SERVICES_CODE;
    } else if (type_str == "BootServicesData") {
        return EFI_BOOT_SERVICES_DATA;
    } else if (type_str == "ACPIReclaimMemory") {
        return EFI_ACPI_RECLAIM_MEMORY;
    } else if (type_str == "ACPIMemoryNVS") {
        return EFI_ACPI_MEMORY_NVS;
    } else if (type_str == "MemoryMappedIO") {
        return EFI_MEMORY_MAPPED_IO;
    } else if (type_str == "MemoryMappedIOPortSpace") {
        return EFI_MEMORY_MAPPED_IO_PORT_SPACE;
    } else if (type_str == "LoaderCode") {
        return EFI_LOADER_CODE;
    } else if (type_str == "LoaderData") {
        return EFI_LOADER_DATA;
    } else if (type_str == "PALCode") {
        return EFI_PAL_CODE;
    } else if (type_str == "PersistentMemory") {
        return EFI_PERSISTENT_MEMORY;
    } else if (type_str == "UnacceptedMemoryType") {
        return EFI_UNACCEPTED_MEMORY_TYPE;
    } else if (type_str == "UnusableMemory") {
        return EFI_UNUSABLE_MEMORY;
    } else if (type_str == "OsReserved") {
        return OS_RESERVED_MEMORY;
    } else if (type_str == "MemSegHole") {
        return OS_MEMSEG_HOLE;
    } else if (type_str == "KernelCode") {
        return OS_KERNEL_CODE;
    } else if (type_str == "KernelData") {
        return OS_KERNEL_DATA;
    } else if (type_str == "KernelStack") {
        return OS_KERNEL_STACK;
    } else if (type_str == "HardwareGraphicBuffer") {
        return OS_HARDWARE_GRAPHIC_BUFFER;
    } else if (type_str == "AllocatableMemory") {
        return OS_ALLOCATABLE_MEMORY;
    } else if (type_str == "PgTbSegs") {
        return OS_PGTB_SEGS;
    } else {
        // 未知类型默认返回 EFI_RESERVED_MEMORY_TYPE
        return EFI_RESERVED_MEMORY_TYPE;
    }
}


/**
 * @brief 解析 JSON 内存映射文件，转换为 phymem_segment 数组
 * 
 * @param json_memory_map JSON 对象引用，应包含"phymem"数组
 * @param entry_count 输出参数，返回解析后的内存段数量
 * @return phymem_segment* 动态分配的内存段数组指针，调用者负责释放内存
 * 
 * @note 使用示例:
 * @code
 * nlohmann::json json_data;
 * std::ifstream file("test_set/vm_intance.json");
 * file >> json_data;
 * uint64_t count;
 * phymem_segment* segments = prase_json_memory_map(json_data, count);
 * @endcode
 */
phymem_segment* prase_json_memory_map(nlohmann::json& json_memory_map, uint64_t& entry_count) {
    // 验证 JSON 结构
    if (!json_memory_map.contains("phymem") || !json_memory_map["phymem"].is_array()) {
        return nullptr;
    }
    
    auto& phymem_array = json_memory_map["phymem"];
    entry_count = phymem_array.size();
    
    if (entry_count == 0) {
        return nullptr;
    }
    
    // 分配内存段数组
    phymem_segment* segments = new phymem_segment[entry_count];
    memset(segments, 0, sizeof(phymem_segment) * entry_count);
    
    // 遍历 JSON 数组并解析每个内存段
    for (uint64_t i = 0; i < entry_count; ++i) {
        auto& segment = phymem_array[i];
        
        // 解析起始地址（支持字符串或数值类型）
        if (segment.contains("start")) {
            if (segment["start"].is_string()) {
                // 十六进制字符串格式，如 "0x9F000"
                const char* start_str = segment["start"].get<std::string>().c_str();
                segments[i].start = strtoull(start_str, nullptr, 16);
            } else if (segment["start"].is_number()) {
                segments[i].start = segment["start"].get<uint64_t>();
            }
        }
        
        // 解析大小（支持字符串或数值类型）
        if (segment.contains("size")) {
            if (segment["size"].is_string()) {
                // 十六进制字符串格式，如 "0x1000"
                const char* size_str = segment["size"].get<std::string>().c_str();
                segments[i].size = strtoull(size_str, nullptr, 16);
            } else if (segment["size"].is_number()) {
                segments[i].size = segment["size"].get<uint64_t>();
            }
        }
        
        // 解析类型并转换为 PHY_MEM_TYPE 枚举
        if (segment.contains("type")) {
            std::string type_str = segment["type"].get<std::string>();
            segments[i].type = parse_memory_type(type_str);
        } else {
            segments[i].type = EFI_RESERVED_MEMORY_TYPE;
        }
    }
    
    return segments;
}
/**
 * @brief 从文件系统加载并解析 JSON 内存映射文件
 * 
 * @param file_path JSON 文件在文件系统中的路径
 * @param entry_count 输出参数，返回解析后的内存段数量
 * @return phymem_segment* 动态分配的内存段数组指针，调用者负责释放内存
 *         失败时返回 nullptr
 * 
 * @note 此接口完全隐藏了 JSON 相关的实现细节
 *       调用者无需包含任何 JSON 库头文件或了解 JSON 解析过程
 * 
 * @note 使用示例:
 * @code
 * uint64_t count;
 * phymem_segment* segments = load_and_parse_memory_map("test_set/vm_intance.json", count);
 * if (segments) {
 *     // 使用 segments 数组
 *     delete[] segments;
 * }
 * @endcode
 */
phymem_segment* load_and_parse_memory_map(const char* file_path, uint64_t& entry_count) {
    if (!file_path) {
        std::cerr << "[ERROR] File path is null" << std::endl;
        return nullptr;
    }
    
    // 打开文件
    std::ifstream file(file_path);
    if (!file.is_open()) {
        std::cerr << "[ERROR] Failed to open file: " << file_path << std::endl;
        return nullptr;
    }
    
    // 解析 JSON
    nlohmann::json json_data;
    try {
        file >> json_data;
    } catch (const nlohmann::json::parse_error& e) {
        std::cerr << "[ERROR] JSON parse error in " << file_path << ": " << e.what() << std::endl;
        return nullptr;
    }
    
    file.close();
    
    // 调用内部解析函数
    return prase_json_memory_map(json_data, entry_count);
}
