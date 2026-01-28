#include "util/bitmap.h"
#include <cstdint>
#include <cstring>

// 添加__popcountdi2函数的实现（GCC内置函数）
extern "C" int __popcountdi2(unsigned long x) {
    // 使用位运算实现汉明重量(Hamming Weight)计算
    x = x - ((x >> 1) & 0x5555555555555555UL);
    x = (x & 0x3333333333333333UL) + ((x >> 2) & 0x3333333333333333UL);
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0fUL;
    x = x + (x >> 8);
    x = x + (x >> 16);
    x = x + (x >> 32);
    return x & 0x7f;
}

#include "util/Ktemplats.h"
#ifdef KERNEL_MODE
#include "memory/kpoolmemmgr.h"
#endif
void bitmap_t::bit_set(uint64_t bit_idx, bool value)
{
    if (value)
    {
        bitmap[bit_idx>>6] |= (1ULL << (bit_idx & 63));
    }
    else
    {
        bitmap[bit_idx>>6] &= ~(1ULL << (bit_idx & 63));
    }
}

bool bitmap_t::bit_get(uint64_t bit_idx)
{
    return bitmap[bit_idx>>6] & (1ULL << (bit_idx & 63));
}

void bitmap_t::bits_set(uint64_t start_bit_idx, uint64_t bit_count, bool value)
{
    uint64_t end_bit_idx = start_bit_idx + bit_count;
    
    // 处理起始部分，直到下一个64位边界
    while((start_bit_idx < end_bit_idx) && ((start_bit_idx & 0x3F) != 0)) {
        bit_set(start_bit_idx, value);
        start_bit_idx++;
    }
    
    // 如果还有剩余位需要设置，且至少有一个完整的64位单元
    if(start_bit_idx < end_bit_idx) {
        uint64_t start_u64_idx = start_bit_idx >> 6;
        uint64_t end_u64_idx = end_bit_idx >> 6;
        
        // 处理完整的64位单元
        if(start_u64_idx < end_u64_idx) {
            uint64_t u64_count = end_u64_idx - start_u64_idx;
            u64s_set(start_u64_idx, u64_count, value);
            start_bit_idx = end_u64_idx << 6; // 更新start_bit_idx到处理完的64位边界
        }
        
        // 处理最后不足64位的部分
        while(start_bit_idx < end_bit_idx) {
            bit_set(start_bit_idx, value);
            start_bit_idx++;
        }
    }
}

void bitmap_t::bytes_set(uint64_t start_byte_idx, uint64_t byte_count, bool value)
{
    uint64_t end_byte_idx = start_byte_idx + byte_count;
    uint8_t to_fill_value = value ? BYTE_FULL : 0x00;
    
    // 处理起始部分，直到下一个8字节(64位)边界
    while((start_byte_idx < end_byte_idx) && ((start_byte_idx & 0x7) != 0)) {
        byte_bitmap_base[start_byte_idx] = to_fill_value;
        start_byte_idx++;
    }
    
    // 如果还有剩余字节需要设置，且至少有一个完整的64位单元
    if(start_byte_idx < end_byte_idx) {
        uint64_t start_u64_idx = start_byte_idx >> 3;
        uint64_t end_u64_idx = end_byte_idx >> 3;
        
        // 处理完整的64位(8字节)单元
        if(start_u64_idx < end_u64_idx) {
            uint64_t u64_count = end_u64_idx - start_u64_idx;
            u64s_set(start_u64_idx, u64_count, value);
            start_byte_idx = end_u64_idx << 3; // 更新start_byte_idx到处理完的8字节边界
        }
        
        // 处理最后不足8字节的部分
        while(start_byte_idx < end_byte_idx) {
            byte_bitmap_base[start_byte_idx] = to_fill_value;
            start_byte_idx++;
        }
    }
}

void bitmap_t::u64s_set(uint64_t start_u64_idx, uint64_t u64_count, bool value)
{
    uint64_t to_fill_value= value ? U64_FULL_UNIT : 0x00;
    for(uint64_t i = start_u64_idx; i < start_u64_idx + u64_count; i++)
    bitmap[i]= to_fill_value;
}
/**
 * 采用类似于kmp的段分析算法，从0开始扫
 * 扫描的时候可以通过uit64_t U64_FULL_UNIT跳过64个字节
 * 
 */
int bitmap_t::continual_avaliable_bits_search(uint64_t bit_count, uint64_t &result_base_idx)
{
    uint64_t bit_scanner_end = bitmap_size_in_64bit_units << 6;
    uint64_t seg_base_bit_idx = 0;
    bool seg_value = bit_get(0);
    uint64_t seg_length = 1; // 初始化长度为1，因为已经包含了seg_base_bit_idx

    while (seg_base_bit_idx + seg_length < bit_scanner_end) {
        uint64_t current_bit_idx = seg_base_bit_idx + seg_length;
        
        // 64位对齐优化检查
        if ((current_bit_idx & 0x3F) == 0) { // 检查是否是64位对齐
            uint64_t* to_scan_u64 = reinterpret_cast<uint64_t*>(
                byte_bitmap_base + (current_bit_idx >> 3));
            
            if (seg_value) {
                // 当前是占用段，检查是否可以跳过整个64位占用
                if (*to_scan_u64 == U64_FULL_UNIT) {
                    seg_length += 64;
                    continue; // 继续跳过占用段
                }
            } else {
                // 当前是空闲段，检查是否可以跳过整个64位空闲
                if (*to_scan_u64 == 0x00) {
                    seg_length += 64;
                    // 检查是否已经找到足够的连续空闲位
                    if (seg_length >= bit_count) {
                        result_base_idx = seg_base_bit_idx;
                        return OS_SUCCESS;
                    }
                    continue; // 继续尝试跳过更多空闲段
                }
            }
        }
        
        // 逐位扫描
        bool new_bit_value = bit_get(current_bit_idx);
        
        if (seg_value == new_bit_value) {
            // 位值相同，扩展当前段
            seg_length++;
            
            if (!seg_value && seg_length >= bit_count) {
                // 找到足够的连续空闲位
                result_base_idx = seg_base_bit_idx;
                return OS_SUCCESS;
            }
        } else {
            // 位值变化，切换到新段
            if (!seg_value && seg_length >= bit_count) {
                // 当前空闲段已经满足要求
                result_base_idx = seg_base_bit_idx;
                return OS_SUCCESS;
            }
            
            // 开始新段
            seg_base_bit_idx = current_bit_idx;
            seg_value = new_bit_value;
            seg_length = 1;
        }
    }
    
    // 检查最后一个段是否满足条件
    if (!seg_value && seg_length >= bit_count) {
        result_base_idx = seg_base_bit_idx;
        return OS_SUCCESS;
    }
    
    return OS_NOT_EXIST;
}

int bitmap_t::continual_avaliable_bytes_search(uint64_t byte_count, uint64_t &result_base_idx)
{
    const uint64_t total_bytes = bitmap_size_in_64bit_units * 8; // 每个u64含8字节
    uint64_t seg_base_byte_idx = 0;
    bool seg_value = !!byte_bitmap_base[0]; // true=占用，false=空闲
    uint64_t seg_length = 1; // 当前段长度(单位:字节)

    while (seg_base_byte_idx + seg_length < total_bytes) {
        uint64_t current_byte_idx = seg_base_byte_idx + seg_length;

        // ===== 64bit对齐优化（跳过8字节） =====
        if ((current_byte_idx & 0x7) == 0) { // 字节对齐到8
            uint64_t* to_scan_u64 = reinterpret_cast<uint64_t*>(
                byte_bitmap_base + current_byte_idx);

            if (seg_value) {
                // 当前是占用段，检查是否可以跳过整块占用
                if (*to_scan_u64 == U64_FULL_UNIT) {
                    seg_length += 8;
                    continue;
                }
            } else {
                // 当前是空闲段
                if (*to_scan_u64 == 0x00ULL) {
                    seg_length += 8;
                    if (seg_length >= byte_count) {
                        result_base_idx = seg_base_byte_idx;
                        return OS_SUCCESS;
                    }
                    continue;
                }
            }
        }

        // ===== 单字节扫描 =====
        bool new_value = !!byte_bitmap_base[current_byte_idx] ;

        if (seg_value == new_value) {
            seg_length++;
            if (!seg_value && seg_length >= byte_count) {
                result_base_idx = seg_base_byte_idx;
                return OS_SUCCESS;
            }
        } else {
            if (!seg_value && seg_length >= byte_count) {
                result_base_idx = seg_base_byte_idx;
                return OS_SUCCESS;
            }
            seg_base_byte_idx = current_byte_idx;
            seg_value = new_value;
            seg_length = 1;
        }
    }

    if (!seg_value && seg_length >= byte_count) {
        result_base_idx = seg_base_byte_idx;
        return OS_SUCCESS;
    }

    return OS_NOT_EXIST;
}

int bitmap_t::continual_avaliable_u64s_search(uint64_t u64_count, uint64_t &result_base_idx)
{
    const uint64_t total_u64s = bitmap_size_in_64bit_units;
    uint64_t seg_base_u64_idx = 0;
    bool seg_value = !!bitmap[0];
    uint64_t seg_length = 1;

    while (seg_base_u64_idx + seg_length < total_u64s) {
        uint64_t current_u64_idx = seg_base_u64_idx + seg_length;
        bool new_value = !!bitmap[current_u64_idx];

        if (seg_value == new_value) {
            seg_length++;
            if (!seg_value && seg_length >= u64_count) {
                result_base_idx = seg_base_u64_idx;
                return OS_SUCCESS;
            }
        } else {
            if (!seg_value && seg_length >= u64_count) {
                result_base_idx = seg_base_u64_idx;
                return OS_SUCCESS;
            }
            seg_base_u64_idx = current_u64_idx;
            seg_value = new_value;
            seg_length = 1;
        }
    }

    if (!seg_value && seg_length >= u64_count) {
        result_base_idx = seg_base_u64_idx;
        return OS_SUCCESS;
    }

    return OS_NOT_EXIST;
}

int bitmap_t::get_bitmap_used_bit()
{
    return bitmap_used_bit;
}
int bitmap_t::used_bit_count_add(uint64_t add_count)
{
    used_bit_count_lock.lock();
    bitmap_used_bit += add_count;
    used_bit_count_lock.unlock();
    return OS_SUCCESS;
}
int bitmap_t::used_bit_count_sub(uint64_t sub_count)
{
    used_bit_count_lock.lock();
    sub_count>=bitmap_used_bit ? bitmap_used_bit=0 : bitmap_used_bit-=sub_count;
    used_bit_count_lock.unlock();
    return OS_SUCCESS;
}
void bitmap_t::count_bitmap_used_bit()
{
    uint64_t used = 0;
    for (uint64_t i = 0; i < bitmap_size_in_64bit_units; ++i) {
        uint64_t val = bitmap[i];
        uint64_t cnt=__builtin_popcountll(val);
        used += cnt;
    }
    bitmap_used_bit = used;
}
Ktemplats::kernel_bitmap::~kernel_bitmap()
    {
        if (bitmap) {
            delete[] bitmap;
            bitmap = nullptr;
        }
        bitmap_size_in_64bit_units = 0;
        bitmap_used_bit = 0;
        byte_bitmap_base = nullptr;
    }
Ktemplats::kernel_bitmap::kernel_bitmap(uint64_t bit_count)
{
        bitmap_used_bit = 0;

        uint64_t u64_count = (bit_count + 63) >> 6;
        bitmap_size_in_64bit_units = u64_count;

        bitmap = new uint64_t[u64_count];

        if (!bitmap) {
            bitmap_size_in_64bit_units = 0;
            byte_bitmap_base = nullptr;
            return;
        }

        for (uint64_t i = 0; i < u64_count; ++i)
            bitmap[i] = 0;

        byte_bitmap_base = reinterpret_cast<uint8_t*>(bitmap);
}
// 实现文件中的新增函数
int bitmap_t::avaliable_bit_search(uint64_t& result_base_idx) {
    // 获取读锁保护位图访问

    
    // 遍历所有64位单元
    for (uint64_t i = 0; i < bitmap_size_in_64bit_units; ++i) {
        uint64_t unit = bitmap[i];
        
        // 如果这个单元不是全1（即有空闲位）
        if (unit != U64_FULL_UNIT) {
            // 使用CTZ找到第一个0的位置（空闲位）
            // 注意：CTZ是计数尾随零，我们需要找到第一个0位
            // 对unit取反，这样0变成1，然后找第一个1的位置
            uint64_t inverted = ~unit;
            int bit_pos = __builtin_ctzll(inverted);
            
            // 计算全局位索引
            result_base_idx = i * 64 + bit_pos;

            return OS_SUCCESS;
        }
    }

    return OS_OUT_OF_RESOURCE; // 没有找到空闲位
}