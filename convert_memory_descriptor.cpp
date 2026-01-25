#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

// 包含必要的头文件
#include "src/include/memory/Memory.h"

#ifdef USER_MODE

// 将内存描述符表从文本格式转换为gBaseMemMgr.Init函数可以直接使用的格式
void print_memory_descriptor_for_gbasememmgr(const char* input_file_path) {
    FILE* input_file = fopen(input_file_path, "r");
    if (!input_file) {
        printf("Error: Cannot open input file %s\n", input_file_path);
        return;
    }

    // 跳过标题行
    char line[512];
    if (!fgets(line, sizeof(line), input_file)) {
        printf("Error: Cannot read header from input file\n");
        fclose(input_file);
        return;
    }
    if (!fgets(line, sizeof(line), input_file)) {
        printf("Error: Cannot read separator from input file\n");
        fclose(input_file);
        return;
    }

    // 读取内存描述符表
    phy_memDescriptor descriptors[256];
    int count = 0;
    
    while (fgets(line, sizeof(line), input_file) && count < 256) {
        // 跳过空行或格式不正确的行
        if (strlen(line) < 10) continue;
        
        // 解析每一行
        int idx;
        char type_str[64];
        unsigned long long physical_start;
        unsigned long long pages;
        unsigned long long size;
        char attributes[64];
        
        int items = sscanf(line, " %d | %s | 0x%llx | %llu | %llu MB | %s", 
                          &idx, type_str, &physical_start, &pages, &size, attributes);
        
        if (items < 4) continue; // 解析失败，跳过
        
        // 转换类型字符串为PHY_MEM_TYPE
        PHY_MEM_TYPE type = EFI_RESERVED_MEMORY_TYPE;
        if (strstr(type_str, "ConventionalMemory") != NULL) {
            type = freeSystemRam;
        } else if (strstr(type_str, "BootServicesData") != NULL) {
            type = EFI_BOOT_SERVICES_DATA;
        } else if (strstr(type_str, "BootServicesCode") != NULL) {
            type = EFI_BOOT_SERVICES_CODE;
        } else if (strstr(type_str, "RuntimeServicesCode") != NULL) {
            type = EFI_RUNTIME_SERVICES_CODE;
        } else if (strstr(type_str, "RuntimeServicesData") != NULL) {
            type = EFI_RUNTIME_SERVICES_DATA;
        } else if (strstr(type_str, "ACPIReclaimMemory") != NULL) {
            type = EFI_ACPI_RECLAIM_MEMORY;
        } else if (strstr(type_str, "ACPIMemoryNVS") != NULL) {
            type = EFI_ACPI_MEMORY_NVS;
        } else if (strstr(type_str, "MemoryMappedIO") != NULL) {
            type = EFI_MEMORY_MAPPED_IO;
        } else if (strstr(type_str, "LoaderCode") != NULL) {
            type = EFI_LOADER_CODE;
        } else if (strstr(type_str, "LoaderData") != NULL) {
            type = EFI_LOADER_DATA;
        } else if (strstr(type_str, "Reserved") != NULL) {
            type = EFI_RESERVED_MEMORY_TYPE;
        }
        
        descriptors[count].Type = type;
        descriptors[count].remapped_count = 0;  // 重映射计数
        descriptors[count].PhysicalStart = physical_start;
        descriptors[count].VirtualStart = 0;  // 暂时设为0
        descriptors[count].NumberOfPages = pages;
        descriptors[count].Attribute = 0;  // 暂时设为0
        descriptors[count].submaptable = nullptr;  // 子映射表指针设为nullptr
        
        count++;
    }
    
    fclose(input_file);
    
    // 打印可以直接用于gBaseMemMgr.Init的C++代码格式
    printf("// phy_memDescriptor array for gBaseMemMgr.Init (user mode)\n");
    printf("phy_memDescriptor memDescriptors[] = {\n");
    for (int i = 0; i < count; i++) {
        printf("    {\n");
        printf("        .Type = %s,\n", 
               descriptors[i].Type == freeSystemRam ? "freeSystemRam" :
               descriptors[i].Type == EFI_BOOT_SERVICES_DATA ? "EFI_BOOT_SERVICES_DATA" :
               descriptors[i].Type == EFI_BOOT_SERVICES_CODE ? "EFI_BOOT_SERVICES_CODE" :
               descriptors[i].Type == EFI_RUNTIME_SERVICES_CODE ? "EFI_RUNTIME_SERVICES_CODE" :
               descriptors[i].Type == EFI_RUNTIME_SERVICES_DATA ? "EFI_RUNTIME_SERVICES_DATA" :
               descriptors[i].Type == EFI_ACPI_RECLAIM_MEMORY ? "EFI_ACPI_RECLAIM_MEMORY" :
               descriptors[i].Type == EFI_ACPI_MEMORY_NVS ? "EFI_ACPI_MEMORY_NVS" :
               descriptors[i].Type == EFI_MEMORY_MAPPED_IO ? "EFI_MEMORY_MAPPED_IO" :
               descriptors[i].Type == EFI_LOADER_CODE ? "EFI_LOADER_CODE" :
               descriptors[i].Type == EFI_LOADER_DATA ? "EFI_LOADER_DATA" :
               "EFI_RESERVED_MEMORY_TYPE");
        printf("        .remapped_count = 0,\n");
        printf("        .PhysicalStart = 0x%llX,\n", descriptors[i].PhysicalStart);
        printf("        .VirtualStart = 0x%llX,\n", descriptors[i].VirtualStart);
        printf("        .NumberOfPages = %llu,\n", descriptors[i].NumberOfPages);
        printf("        .Attribute = 0,\n");
        printf("        .submaptable = nullptr\n");
        if (i < count - 1) {
            printf("    },\n");
        } else {
            printf("    }\n");
        }
    }
    printf("};\n");
    printf("uint64_t entryCount = %d;\n", count);
    
    // 打印调用示例
    printf("\n// Example usage in user mode:\n");
    printf("gBaseMemMgr.Init(memDescriptors, entryCount);\n");
}

int main() {
    printf("Converting memory descriptor table from logs/log_selftest.txt\n");
    print_memory_descriptor_for_gbasememmgr("logs/log_selftest.txt");
    return 0;
}

#endif