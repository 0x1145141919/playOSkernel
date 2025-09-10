#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include "Memory.h"
#include "phygpsmemmgr.h"
#include "kpoolmemmgr.h"
// 辅助函数：修剪字符串两端的空格

// 辅助函数：修剪字符串
static char* trim(char* str) {
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = 0;
    
    return str;
}

// 将类型字符串转换为枚举值
static EFI_MEMORY_TYPE parse_memory_type(const char* type_str) {
    if (strstr(type_str, "Conventional")) return EfiConventionalMemory;
    if (strstr(type_str, "LoaderCode")) return EfiLoaderCode;
    if (strstr(type_str, "LoaderData")) return EfiLoaderData;
    if (strstr(type_str, "BootServicesCode")) return EfiBootServicesCode;
    if (strstr(type_str, "BootServicesData")) return EfiBootServicesData;
    if (strstr(type_str, "RuntimeServicesCode")) return EfiRuntimeServicesCode;
    if (strstr(type_str, "RuntimeServicesData")) return EfiRuntimeServicesData;
    if (strstr(type_str, "ACPIReclaim")) return EfiACPIReclaimMemory;
    if (strstr(type_str, "ACPIMemoryNVS")) return EfiACPIMemoryNVS;
    if (strstr(type_str, "MemoryMappedIO")) return EfiMemoryMappedIO;
    if (strstr(type_str, "Reserved")) return EfiReservedMemoryType;
    return EfiReservedMemoryType;
}

// 解析属性字符串
static UINT64 parse_attributes(const char* attr_str) {
    UINT64 attr = 0;
    
    if (strstr(attr_str, "UC")) attr |= EFI_MEMORY_UC;
    if (strstr(attr_str, "WC")) attr |= EFI_MEMORY_WC;
    if (strstr(attr_str, "WT")) attr |= EFI_MEMORY_WT;
    if (strstr(attr_str, "WB")) attr |= EFI_MEMORY_WB;
    if (strstr(attr_str, "UCE")) attr |= EFI_MEMORY_UCE;
    if (strstr(attr_str, "WP")) attr |= EFI_MEMORY_WP;
    if (strstr(attr_str, "RP")) attr |= EFI_MEMORY_RP;
    if (strstr(attr_str, "XP")) attr |= EFI_MEMORY_XP;
    if (strstr(attr_str, "RUNTIME")) attr |= EFI_MEMORY_RUNTIME;
    
    return attr;
}

// 主解析函数
EFI_STATUS parse_memory_map_file(const char* filename, 
                                EFI_MEMORY_DESCRIPTORX64** map, 
                                UINTN* entry_count) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Error opening file: %s\n", filename);
        return 1;
    }

    *entry_count = 0;
    char line[512];
    int header_found = 0;
    int in_table = 0;

    // 第一遍：精确统计表项数量
    while (fgets(line, sizeof(line), file)) {
        // 检查详细内存映射表头
        if (strstr(line, "Detailed Memory Map") != NULL) {
            header_found = 1;
            continue;
        }
        
        // 找到表头后，下一行是列标题行
        if (header_found && !in_table) {
            in_table = 1;
            continue;
        }
        
        // 在表格区域中，检查表格结束标记
        if (in_table && strstr(line, "======") != NULL) {
            in_table = 0;
            break;
        }
        
        // 在表格区域中，检查有效数据行
        if (in_table) {
            // 有效数据行的特征：包含竖线分隔符
            if (strchr(line, '|') != NULL) {
                // 进一步验证：第一个字段应该是数字
                char *first_field = line;
                while (*first_field && isspace(*first_field)) first_field++;
                if (isdigit(*first_field)) {
                    (*entry_count)++;
                }
            }
        }
    }
    
    if (*entry_count == 0) {
        fclose(file);
        return 2;
    }
    
    // 分配内存
    *map = (EFI_MEMORY_DESCRIPTORX64*)malloc(*entry_count * sizeof(EFI_MEMORY_DESCRIPTORX64));
    if (!*map) {
        fclose(file);
        return 3;
    }
    
    // 第二遍：精确解析数据
    rewind(file);
    header_found = 0;
    in_table = 0;
    int index = 0;
    
    while (fgets(line, sizeof(line), file)) {
        // 定位详细内存映射表头
        if (strstr(line, "Detailed Memory Map") != NULL) {
            header_found = 1;
            continue;
        }
        
        // 定位列标题行
        if (header_found && !in_table) {
            in_table = 1;
            continue;
        }
        
        // 检查表格结束标记
        if (in_table && strstr(line, "======") != NULL) {
            break;
        }
        
        // 处理有效数据行
        if (in_table && strchr(line, '|') != NULL) {
            char *first_field = line;
            while (*first_field && isspace(*first_field)) first_field++;
            if (!isdigit(*first_field)) continue;
            
            char* cols[6];
            char* token = strtok(line, "|");
            int col = 0;
            
            // 分割列
            while (token != NULL && col < 6) {
                cols[col++] = trim(token);
                token = strtok(NULL, "|");
            }
            
            if (col < 6) continue; // 无效行
            
            // 解析类型
            if (strstr(cols[1], "Conventional")) (*map)[index].Type = EfiConventionalMemory;
            else if (strstr(cols[1], "LoaderCode")) (*map)[index].Type = EfiLoaderCode;
            else if (strstr(cols[1], "LoaderData")) (*map)[index].Type = EfiLoaderData;
            else if (strstr(cols[1], "BootServicesCode")) (*map)[index].Type = EfiBootServicesCode;
            else if (strstr(cols[1], "BootServicesData")) (*map)[index].Type = EfiBootServicesData;
            else if (strstr(cols[1], "RuntimeServicesCode")) (*map)[index].Type = EfiRuntimeServicesCode;
            else if (strstr(cols[1], "RuntimeServicesData")) (*map)[index].Type = EfiRuntimeServicesData;
            else if (strstr(cols[1], "ACPIReclaim")) (*map)[index].Type = EfiACPIReclaimMemory;
            else if (strstr(cols[1], "ACPIMemoryNVS")) (*map)[index].Type = EfiACPIMemoryNVS;
            else if (strstr(cols[1], "Reserved")) (*map)[index].Type = EfiReservedMemoryType;
            else (*map)[index].Type = EfiReservedMemoryType; // 未知类型
            
            // 解析物理地址
            (*map)[index].PhysicalStart = strtoull(cols[2], NULL, 16);
            
            // 解析页数
            (*map)[index].NumberOfPages = strtoull(cols[3], NULL, 10);
            
            // 解析属性
            (*map)[index].Attribute = parse_attributes(cols[5]);
            
            // 设置默认值
            (*map)[index].ReservedUnion.Reserved = 0;
            (*map)[index].VirtualStart = 0;
            (*map)[index].ReservedB = 0;
            
            index++;
        }
    }
    
    fclose(file);
    return EFI_SUCCESS;
}
const char* memTypeToString(UINT32 type) {
    switch(type) {
        case EfiReservedMemoryType: return "EFI_RESERVED";
        case EFI_LOADER_CODE:          return "EFI_LOADER_CODE";
        case EFI_LOADER_DATA:          return "EFI_LOADER_DATA";
        case freeSystemRam:            return "FREE_RAM";
        case OS_ALLOCATABLE_MEMORY:    return "OS_ALLOCATABLE";
        case OS_RESERVED_MEMORY:       return "OS_RESERVED";
        // ...添加其他类型的case
        default:
            if(type >= MEMORY_TYPE_OEM_RESERVED_MIN && 
               type <= MEMORY_TYPE_OEM_RESERVED_MAX) 
                return "OEM_RESERVED";
            if(type >= MEMORY_TYPE_OS_RESERVED_MIN && 
               type <= MEMORY_TYPE_OS_RESERVED_MAX) 
                return "OS_RESERVED_RANGE";
            return "UNKNOWN_TYPE";
    }
}

// 打印物理内存描述符数组
void printPhysicalMemoryUsage(phy_memDesriptor* descArray) {
    if(!descArray) {
        printf("Memory descriptor array is NULL!\n");
        return;
    }

    printf("%-12s %-16s %-16s %-12s %-10s %-8s %-8s %-8s %s\n",
           "Type", "PhysStart", "VirtStart", "Pages", "Size(MB)",
           "Used", "Read", "Write", "Exec");

    for(int i = 0; ; i++) {
        phy_memDesriptor* desc = &descArray[i];
        
        // 检查结束标记（全零描述符）
        if(desc->Type == 0 &&
           desc->PhysicalStart == 0 &&
           desc->VirtualStart == 0 &&
           desc->NumberOfPages == 0 &&
           desc->Attribute == 0) {
            break;
        }

        // 计算内存大小（MB）
        uint64_t size_bytes = desc->NumberOfPages * 4096;
        double size_mb = size_bytes / (1024.0 * 1024.0);
        
        // 解析属性位
        int isUsed  = (desc->Attribute & 0x1) ? 1 : 0;
        int isRead  = (desc->Attribute & 0x2) ? 1 : 0;
        int isWrite = (desc->Attribute & 0x4) ? 1 : 0;
        int isExec  = (desc->Attribute & 0x8) ? 1 : 0;

        printf("%-12s 0x%016lx 0x%016lx %-8lu %-10.2f %-8s %-8s %-8s %-8s\n",
               memTypeToString(desc->Type),
               (unsigned long)desc->PhysicalStart,
               (unsigned long)desc->VirtualStart,
               (unsigned long)desc->NumberOfPages,
               size_mb,
               isUsed ? "Y" : "N",
               isRead ? "Y" : "N",
               isWrite ? "Y" : "N",
               isExec ? "Y" : "N");
    }
    printf("End of memory descriptor list\n");
}
/**
 * run_memory_manager_tests的例子还暂时没有跑通
 * 先暂时不用，等后面有时间的时候继续调试，修补页内存管理这个模块
 */
void run_memory_manager_tests() {
    printf("\n===== Starting Memory Manager Tests =====\n");
    
    // 1. 测试基本分配功能
    printf("\n[Test 1: Basic Allocation]\n");
    phyaddr_t addr1 = (phyaddr_t)gPgsMemMgr.pgs_allocate(4096, gPgsMemMgr.PG_RW, 12); // 分配1页
    phyaddr_t addr2 = (phyaddr_t)gPgsMemMgr.pgs_allocate(8192, gPgsMemMgr.PG_RWX, 12); // 分配2页
    phyaddr_t addr3 = (phyaddr_t)gPgsMemMgr.pgs_allocate(16384, gPgsMemMgr.PG_R, 21); // 分配4页，要求2MB对齐
    printf("Allocated: 0x%lx (4KB), 0x%lx (8KB), 0x%lx (16KB, 2MB aligned)\n", 
           addr1, addr2, addr3);
    // 2. 测试固定地址分配
    printf("\n[Test 2: Fixed Address Allocation]\n");
    // 尝试在空闲区域分配
    int result1 = gPgsMemMgr.pgs_fixedaddr_allocate(0xB0000000, 4096, gPgsMemMgr.PG_RW);
    printf("Fixed allocation at 0xB0000000: %s\n", result1 == 0 ? "SUCCESS" : "FAILED");
    // 尝试在已分配区域分配（应失败）
    int result2 = gPgsMemMgr.pgs_fixedaddr_allocate(addr1, 4096, gPgsMemMgr.PG_RW);
    printf("Fixed allocation at already allocated 0x%lx: %s\n", addr1, result2 == 0 ? "SUCCESS" : "FAILED");
    
    // 尝试在保留区域分配（应失败）
    int result3 = gPgsMemMgr.pgs_fixedaddr_allocate(0xBFEAF000, 4096, gPgsMemMgr.PG_RW);
    printf("Fixed allocation at reserved 0xBFEAF000: %s\n", result3 == 0 ? "SUCCESS" : "FAILED");
    
    // 3. 测试内存释放
    printf("\n[Test 3: Memory Free]\n");
    int free_result1 = gPgsMemMgr.pgs_free(addr1, 4096);
    int free_result2 = gPgsMemMgr.pgs_free(addr2, 8192);
    printf("Free addr1 (0x%lx): %s\n", addr1, free_result1 == 0 ? "SUCCESS" : "FAILED");
    printf("Free addr2 (0x%lx): %s\n", addr2, free_result2 == 0 ? "SUCCESS" : "FAILED");
    
    // 尝试释放未分配的内存（应失败）
    int free_result3 = gPgsMemMgr.pgs_free(0xDEADBEEF, 4096);
    printf("Free unallocated 0xDEADBEEF: %s\n", free_result3 == 0 ? "SUCCESS" : "FAILED");
    
    // 4. 测试分配后内存布局变化
    printf("\n[Test 4: Memory Layout Changes]\n");
    printf("Memory layout after allocations and frees:\n");
    phy_memDesriptor* query = gPgsMemMgr.queryPhysicalMemoryUsage(0xB0000000, 1<<28);
    printPhysicalMemoryUsage(query);
    delete[] query;
    
    // 5. 测试边界情况
    printf("\n[Test 5: Edge Cases]\n");
    // 分配0字节（应失败）
    //这里出现了把静态堆内核池填满的bug,注意排查
    phyaddr_t addr_zero = (phyaddr_t)gPgsMemMgr.pgs_allocate(0, gPgsMemMgr.PG_RW, 12);
    printf("Allocate 0 bytes: %s\n", addr_zero ? "SUCCESS" : "FAILED");
    
    // 分配超大内存（超过可用空间）
    phyaddr_t addr_huge = (phyaddr_t)gPgsMemMgr.pgs_allocate(1ULL << 40, gPgsMemMgr.PG_RW, 12); // 1TB
    printf("Allocate 1TB: %s\n", addr_huge ? "SUCCESS" : "FAILED");
    
    // 释放部分分配的内存（应失败）
    int free_result4 = gPgsMemMgr.pgs_free(addr3, 4096); // 只释放部分
    printf("Partial free of 0x%lx: %s\n", addr3, free_result4 == 0 ? "SUCCESS" : "FAILED");
    
    // 6. 测试内存重用
    printf("\n[Test 6: Memory Reuse]\n");
    phyaddr_t addr_reuse = (phyaddr_t)gPgsMemMgr.pgs_allocate(4096, gPgsMemMgr.PG_RW, 12);
    printf("Allocated new block at 0x%lx\n", addr_reuse);
    
    // 检查是否重用了之前释放的内存
    if (addr_reuse == addr1 || addr_reuse == addr2) {
        printf("Successfully reused freed memory at 0x%lx\n", addr_reuse);
    } else {
        printf("Allocated new block at 0x%lx (not reused)\n", addr_reuse);
    }
    
    // 7. 最终内存布局
    printf("\n[Final Memory Layout]\n");
    query = gPgsMemMgr.queryPhysicalMemoryUsage(0xB0000000, 1<<28);
    printPhysicalMemoryUsage(query);
    delete[] query;
    
    // 清理
    gPgsMemMgr.pgs_free(addr3, 16384);
    gPgsMemMgr.pgs_free(addr_reuse, 4096);
    
    printf("\n===== Memory Manager Tests Completed =====\n");
}
int main() { 
    EFI_MEMORY_DESCRIPTORX64* map;
    UINTN entry_count;
    EFI_STATUS status = parse_memory_map_file("/home/pangsong/PS_git/OS_pj_uefi/kernel/logs/EFI_MEMTB_INSTANCE.txt", &map, &entry_count);
    if (status != EFI_SUCCESS) {
        fprintf(stderr, "Error parsing memory map file\n");
        return 1;
    }
    gBaseMemMgr.Init(map, entry_count);
    gBaseMemMgr.printPhyMemDesTb();
    gPgsMemMgr.Init();
    phy_memDesriptor* query=gPgsMemMgr.queryPhysicalMemoryUsage(0xB0000000,1<<28);
    printPhysicalMemoryUsage(query);
    delete[] query;
    run_memory_manager_tests();
    return 0;
}
