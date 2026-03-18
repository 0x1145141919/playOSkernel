#pragma once
#include "../abi/boot.h"
#include <cstdint>

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
 */
phymem_segment* load_and_parse_memory_map(const char* file_path, uint64_t& entry_count);