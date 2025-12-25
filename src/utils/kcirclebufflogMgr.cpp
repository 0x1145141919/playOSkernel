#include "kcirclebufflogMgr.h"
#include "util/OS_utils.h"
extern "C"
{
    extern char* __klog_start;
    extern uint64_t __KLOG_SIZE;
}
 kcirclebufflogMgr gkcirclebufflogMgr;
void kcirclebufflogMgr::Init()
{
    buff= __klog_start;
    buffSize=__KLOG_SIZE;

    tailIndex=0;
}

// 在文档2中更新putsk函数实现
void kcirclebufflogMgr::putsk(char *str, uint64_t len_in_bytes) {
    if (len_in_bytes == 0 || buffSize == 0) return;

    // 处理超长数据（保留最后buffSize字节）
    if (len_in_bytes > buffSize) {
        str += (len_in_bytes - buffSize);
        len_in_bytes = buffSize;
    }

    // 计算尾部连续空间
    uint64_t remaining = buffSize - tailIndex;
    
    if (len_in_bytes <= remaining) {
        // 单次拷贝即可完成
        ksystemramcpy(str, buff + tailIndex, len_in_bytes);
        tailIndex += len_in_bytes;
        // 无需取模，因为remaining确保不会越界
    } else {
        // 分两段拷贝（跨越缓冲区末尾）
        ksystemramcpy(str, buff + tailIndex, remaining);
        uint64_t secondPart = len_in_bytes - remaining;
        ksystemramcpy(str + remaining, buff, secondPart);
        tailIndex = secondPart;  // 尾部指针回绕到开头
    }

    // 确保尾部指针不越界（当remaining恰好相等时）
    if (tailIndex >= buffSize) {
        tailIndex = 0;
    }
}
// 在源文件(kcirclebufflogMgr.cpp)中实现重载函数
void kcirclebufflogMgr::putsk(char *str) {
    // 安全处理空指针
    if (str == nullptr) return;
    
    // 手动计算字符串长度（不依赖标准库strlen）
    uint64_t len = 0;
    char *p = str;
    
    // 设置最大长度保护（防止无限循环）
    const uint64_t maxLen = buffSize * 2; // 允许最多2倍缓冲区长度
    
    while (*p != '\0' && len < maxLen) {
        len++;
        p++;
    }
    
    // 调用原始实现写入数据
    putsk(str, len);
}
kcirclebufflogMgr::~kcirclebufflogMgr()
{
}