#pragma once
#include "stdint.h"
typedef uint64_t size_t;
typedef uint8_t _2mb_pg_bitmapof_4kbpgs[64];
typedef uint8_t pgsbitmap_entry2bits_width[128];
/**
 * @brief 根据给定掩码转换值的位布局
 * 
 * 此函数将输入值的低位连续位重新分布到由掩码指定的位位置上。
 * 例如，如果掩码是0b1010，表示目标位置是第1位和第3位，那么输入值的第0位会放到目标的第1位，
 * 输入值的第1位会放到目标的第3位。
 * 
 * @tparam T 整数类型模板参数
 * @param value 需要转换的输入值，其低位将被映射到掩码为1的位上
 * @param mask 位掩码，指示值的各个位应该放置的位置
 * @return T 转换后的值，其中输入值的位已经按照掩码重新分布
 * 
 * @details 函数执行以下操作：
 * 1. 计算掩码中为1的位数，确定有效位数
 * 2. 检查输入值是否适合掩码所能表示的范围
 * 3. 将输入值的低位依次映射到掩码为1的位位置上
 * 
 * @note 如果输入值超出掩码所能表示的范围，则返回0
 * @note 此函数在UEFI内核环境中使用，不依赖标准库
 */
template<typename T>
inline T transformValueForMask(T value, T mask) {
    // 计算掩码中1的个数（即要设置的位数）
    int bitCount = 0;
    T tempMask = mask;
    while (tempMask) {
        bitCount += tempMask & 1;
        tempMask >>= 1;
    }
    
    // 检查值是否适合指定的位数
    // 注意：对于T类型，我们使用(static_cast<T>(1) << bitCount)来避免溢出问题
    if (bitCount < sizeof(T) * 8) { // 只有当位数小于类型位数时才需要检查
        T maxValue = (static_cast<T>(1) << bitCount) - 1;
        if (value > maxValue) {
            // 在内核环境中，我们可能使用断言或特定的错误处理机制
            // 这里简单返回0作为错误指示，实际应用中应根据内核错误处理策略调整
            return 0;
        }
    }
    
    // 处理不连续掩码：将输入值的低位依次放置到掩码的各个位上
    T result = 0;
    int valueBitIndex = 0; // 当前处理的输入值位索引
    
    // 遍历掩码的所有位
    for (int i = 0; i < sizeof(T) * 8; i++) {
        // 检查掩码的第i位是否为1
        if (mask & (static_cast<T>(1) << i)) {
            // 如果输入值的当前位为1，则设置结果的对应位
            if (value & (static_cast<T>(1) << valueBitIndex)) {
                result |= (static_cast<T>(1) << i);
            }
            valueBitIndex++;
        }
    }
    
    return result;
}
template<typename T>
inline void SetBitsinMask(T& uint,T mask,T value)
{
 T background=uint&(~mask);
    background|=transformValueForMask(value, mask);
    uint=background;
}
void ksystemramcpy(void*src,void*dest,size_t length);
void linearTBSerialDelete(//这是一个对于线性表删除一段连续项,起始索引a,结束索引b的函数
    uint64_t*TotalEntryCount,
    uint64_t a,
    uint64_t b,
    void*linerTbBase,
    uint32_t entrysize
);
void linearTBSerialInsert(
    uint64_t* TotalEntryCount,
    uint64_t insertIndex,
    void* newEntry,
    void* linerTbBase,
    uint32_t entrysize,
    uint64_t entryCount = 1
) ;
int strlen(const char *s);
bool getbit_entry1bit_width(_2mb_pg_bitmapof_4kbpgs* bitmap,uint16_t index);
void setbit_entry1bit_width(_2mb_pg_bitmapof_4kbpgs*bitmap,bool value,uint16_t index);
void setbits_entry1bit_width(_2mb_pg_bitmapof_4kbpgs*bitmap,bool value,uint16_t Start_index,uint16_t len_in_bits);
uint8_t getentry_entry2bits_width(pgsbitmap_entry2bits_width& bitmap, uint16_t index);
void setentry_entry2bits_width(pgsbitmap_entry2bits_width& bitmap, uint8_t value, uint16_t index);
void setentries_entry2bits_width(pgsbitmap_entry2bits_width& bitmap, uint8_t value, uint16_t start_index, uint16_t len_in_entries);
void setmem(void* ptr, uint64_t size_in_byte, uint8_t value);