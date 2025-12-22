#include "util/OS_utils.h"
#include "stdint.h"
#ifdef USER_MODE
#include <x86intrin.h>
#endif
#ifdef KERNEL_MODE 
#include "kintrin.h"
#endif
typedef uint64_t size_t;
const uint8_t masks_entry1bit_width[8]={128,64,32,16,8,4,2,1};
const uint8_t masks_entry2bits_width[4]={192,48,12,3};
const uint8_t bit_reverse_table[256] = {
        0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0, 0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
        0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8, 0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
        0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4, 0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
        0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC, 0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
        0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2, 0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
        0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA, 0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
        0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6, 0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
        0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE, 0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
        0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1, 0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
        0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9, 0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
        0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5, 0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
        0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED, 0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
        0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3, 0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
        0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB, 0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
        0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7, 0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
        0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF, 0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF
    };
uint64_t reverse_perbytes(uint64_t value) {
    uint64_t result = 0;
    uint8_t* p = (uint8_t*)&value;
    uint8_t* q = (uint8_t*)&result;
    for (int i = 0; i < 8; i++) {
       q[i] = bit_reverse_table[p[i]];
    }
    return result;
}
int strcmp(const char *str1, const char *str2, uint32_t max_strlen)
{
    for (uint32_t i = 0; i < max_strlen; i++) {
        if (str1[i] != str2[i]) {
            return (unsigned char)str1[i] - (unsigned char)str2[i];
        }
        if (str1[i] == '\0') {
            break;
        }
    }
    return 0;
}
int strlen(const char *s)
{
    int len = 0;
    while (*s++)
        len++;
    return len;
}
int get_first_true_bit_index(bitset512_t *bitmap) {//记得用bitreserve改写
    for (int i = 0; i < 8; i++) {
        uint64_t chunk = (*bitmap)[i]; // 获取第i个64位块
        if (chunk == 0) continue;      // 如果全0则跳过
        
        // 使用TZCNT找到最低位设置位的位置
        unsigned long index = __builtin_ctzll(chunk);
        
        // 计算全局位置：块索引*64 + 块内位置
        return (i << 6) + index&(~7)+(7-index&7);
    }
    return -1; // 没有找到任何设置位
}
int get_first_zero_bit_index(bitset512_t *bitmap) {
    for (int i = 0; i < 8; i++) {
        uint64_t chunk = (*bitmap)[i];
        uint64_t inverted_chunk = ~chunk;  // 取反，0 变 1，1 变 0
        
        if (inverted_chunk == 0) continue; // 如果全 1（取反后全 0），跳过
        
        // 使用 TZCNT 找到最低位的 1（即原数据的最低位的 0）
        unsigned long index = __builtin_ctzll(inverted_chunk);
        
        // 计算全局位置：块索引 * 64 + 块内位置
          return (i << 6) + index&(~7)+(7-index&7);
    }
    return -1;  // 没有找到任何 0 位（所有位都是 1）
}
int strncmp(const char* str1, const char* str2, size_t n) { 
    while (n-- && *str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    if (n == (size_t)-1) return 0; // 比较了n个字符且都相等
    return *(unsigned char*)str1 - *(unsigned char*)str2;
}

 void setmem(void* ptr, uint64_t size_in_byte, uint8_t value) {
    uint8_t* p = static_cast<uint8_t*>(ptr);
    
    // 使用64位写入来加速内存设置
    uint64_t value64 = value;
    value64 |= value64 << 8;
    value64 |= value64 << 16;
    value64 |= value64 << 32;
    
    // 处理前缀不对齐部分
    while (size_in_byte > 0 && (reinterpret_cast<uint64_t>(p) & 7)) {
        *p++ = value;
        size_in_byte--;
    }
    
    // 使用64位写入处理主体部分
    uint64_t* p64 = reinterpret_cast<uint64_t*>(p);
    while (size_in_byte >= 8) {
        *p64++ = value64;
        size_in_byte -= 8;
    }
    
    // 处理剩余部分
    p = reinterpret_cast<uint8_t*>(p64);
    while (size_in_byte > 0) {
        *p++ = value;
        size_in_byte--;
    }
}
void __kspace_stack_chk_fail(void)
{
    asm volatile ("int $0xc");
}

// 定义栈保护的canary值
uintptr_t __stack_chk_guard = 0x595e9f73bb9247cf;

// 使用链接器wrap选项重载__stack_chk_fail函数
extern "C" void __wrap___stack_chk_fail(void)
{
    __kspace_stack_chk_fail();
}

void ksystemramcpy(void*src,void*dest,size_t length)
//最好用于内核内存空间内的内存拷贝，不然会出现未定义行为
{  uint64_t remainder=length&0x7;
    uint64_t count=length>>3;
    //先范围重复判断
    if(uint64_t(src)>uint64_t(dest)){
    low_to_high:
    for(uint64_t i=0;i<count;i++)
    {
        ((uint64_t*)dest)[i]=((uint64_t*)src)[i];
    }
    for(uint64_t i=0;i<remainder;i++)
    {
        ((uint8_t*)dest)[length-remainder+i]=((uint8_t*)src)[length-remainder+i];
    }
    return ;
}else//源地址低目标地址高的时候就需要内存由高到低复制
//大多数情况源地址目标地址都对齐的情况下，先复制余数项（一次一字节）再续复制非余数项（一次八字节）
{
if((uint64_t(src)+length>uint64_t(dest)))
{
    for(uint64_t i=0;i<remainder;i++)
    {
        ((uint8_t*)dest)[length-i-1]=((uint8_t*)src)[length-i-1];
    }
    for (int i = count-1; i >= 0; i--)
    {
        ((uint64_t*)dest)[i]=((uint64_t*)src)[i];
    }
    
}else goto low_to_high;
}
}
/**
 * 此函数只会对物理内存描述符表中的物理内存起始地址按照低到高排序
 * 一般来说这个表会是分段有序的，采取插入排序
 * 不过前面会有一大段有序的表，所以直接遍历到第一个有序子表结束后再继续遍历
 */

// 带参数的构造函数（示例）
void linearTBSerialDelete(//这是一个对于线性表删除一段连续项,起始索引a,结束索引b的函数
    uint64_t*TotalEntryCount,
    uint64_t a,
    uint64_t b,
    void*linerTbBase,
    uint32_t entrysize
)
{ 
    char*bs=(char*)linerTbBase;
    char*srcadd=bs+entrysize*(b+1);
    char*destadd=bs+entrysize*a;
    uint64_t deletedEntryCount=b-a+1;
    ksystemramcpy((void*)srcadd,(void*)destadd,entrysize*(*TotalEntryCount-b-1));
    *TotalEntryCount-=deletedEntryCount;
}
/**
 * 在描述符表中插入一个或多个连续条目
 * 
 * @param TotalEntryCount 表项总数的指针（插入后会更新）
 * @param insertIndex     插入位置的起始索引（0-based）
 * @param newEntry        要插入的新条目（单个或多个连续条目）
 * @param linerTbBase     描述符表基地址
 * @param entrysize       单个条目的大小
 * @param entryCount      要插入的条目数量（默认为1）
 */
void linearTBSerialInsert(
    uint64_t* TotalEntryCount,
    uint64_t insertIndex,
    void* newEntry,
    void* linerTbBase,
    uint32_t entrysize,
    uint64_t entryCount
) {
    if (insertIndex > *TotalEntryCount) {
        // 插入位置超出当前表范围，直接追加到末尾
        insertIndex = *TotalEntryCount;
    }
    
    char* base = (char*)linerTbBase;
    char* src = (char*)newEntry;
    
    // 计算需要移动的数据量（从插入点到表尾）
    uint64_t moveCount = *TotalEntryCount - insertIndex;
    uint64_t moveSize = moveCount * entrysize;
    
    if (moveSize > 0) {
        // 向后移动现有条目（使用内存安全拷贝）
        char* srcStart = base + insertIndex * entrysize;
        char* destStart = srcStart + entryCount * entrysize;
        ksystemramcpy(srcStart, destStart, moveSize);
    }
    
    // 插入新条目
    for (uint64_t i = 0; i < entryCount; i++) {
        char* dest = base + (insertIndex + i) * entrysize;
        ksystemramcpy(src + i * entrysize, dest, entrysize);
    }
    
    // 更新表项总数
    *TotalEntryCount += entryCount;
}
bool getbit_entry1bit_width(bitset512_t* bitmap,uint16_t index)
{
    uint8_t* map=(uint8_t*)bitmap;
    return (map[index>>3]&masks_entry1bit_width[index&7])!=0;
}
void setbit_entry1bit_width(bitset512_t*bitmap,bool value,uint16_t index)
{
    uint8_t* map=(uint8_t*)bitmap;
    if(value)
        map[index>>3]|=masks_entry1bit_width[index&7];
    else
        map[index>>3]&=~masks_entry1bit_width[index&7];
}
void setbits_entry1bit_width(bitset512_t*bitmap,bool value,uint16_t Start_index,uint16_t len_in_bits)
{
    int bits_left=len_in_bits;
    uint8_t * map_8bit=(uint8_t*)bitmap;
    uint8_t fillcontent8=value?0xff:0;
    uint64_t* map_64bit=(uint64_t*)bitmap;
    uint64_t fillcontent64=value?0xffffffffffffffff:0;
    for (int i = Start_index; i < Start_index+len_in_bits; )
    {
       if (i&63ULL)
       {
not_aligned_6bits:
        if(i&7ULL)
        {
not_aligned_3bits:            
            setbit_entry1bit_width(bitmap,value,i);
            i++;
            bits_left--;
        }else{
            if(bits_left>=8)
            {
                map_8bit[i>>3]=fillcontent8;
                bits_left-=8;
                i+=8;
            }
            else{
                goto not_aligned_3bits;
            }
        }
       }else{
        if(bits_left>=64)
        {
            map_64bit[i>>6]=fillcontent64;  
            bits_left-=64;
            i+=64;
        }
        else{
            goto not_aligned_6bits;
        }
       }  
    }
}
// 获取2bit宽度位图中指定索引的值（返回0-3）
uint8_t getentry_entry2bits_width(pgsbitmap_entry2bits_width& bitmap, uint16_t index) {
    uint8_t byte = bitmap[index >> 2];  // 每个字节包含4个2bit条目
    uint8_t shift = (index & 3) * 2;    // 计算在字节内的偏移（0,2,4,6）
    return (byte >> shift) & 3;         // 提取2bit值
}

// 设置2bit宽度位图中指定索引的值
void setentry_entry2bits_width(pgsbitmap_entry2bits_width& bitmap, uint8_t value, uint16_t index) {
    uint8_t& byte = bitmap[index >> 2];     // 获取对应的字节引用
    uint8_t shift = (index & 3) * 2;        // 计算偏移量
    byte = (byte & ~(3 << shift)) |         // 清除原有值
           ((value & 3) << shift);          // 设置新值
}

// 设置2bit宽度位图中连续多个条目的值
// 优化后的批量设置2bit宽度位图函数
void setentries_entry2bits_width(pgsbitmap_entry2bits_width& bitmap, uint8_t value, uint16_t start_index, uint16_t len_in_entries) {
    value &= 3;  // 确保值在0-3范围内
    
    // 创建64位填充模式（每个2bit都是value）
    uint64_t fillpattern = 0;
    for (int i = 0; i < 32; i++) {  // 64位可容纳32个2bit条目
        fillpattern |= (static_cast<uint64_t>(value) << (i * 2));
    }
    
    uint16_t i = start_index;
    uint16_t end_index = start_index + len_in_entries;
    
    // 处理起始未对齐部分（按单个条目设置）
    while (i < end_index && (i & 31)) {
        setentry_entry2bits_width(bitmap, value, i);
        i++;
    }
    
    // 处理中间对齐部分（按64位块设置，每次设置32个条目）
    uint64_t* map_64bit = reinterpret_cast<uint64_t*>(&bitmap[i >> 2]);
    while (i + 31 < end_index) {
        *map_64bit++ = fillpattern;
        i += 32;
    }
    
    // 处理剩余未对齐部分（按单个条目设置）
    while (i < end_index) {
        setentry_entry2bits_width(bitmap, value, i);
        i++;
    }
}
uint64_t align_up(uint64_t value, uint64_t alignment) {
    // 检查alignment是否为2的幂
    if ((alignment & (alignment - 1)) != 0) {
        // 如果不是2的幂，可以返回0或者处理错误
        return 0; // 或者抛出错误/断言
    }
    // 计算对齐后的值
    return (value + alignment - 1) & ~(alignment - 1);
}
/* 带写屏障的8位原子写入 */
static inline void atomic_write8_wmb(volatile void *addr, uint8_t val)
{
    asm volatile("sfence\n\t"
                 "movb %1, %0"
                 : "=m" (*(volatile uint8_t *)addr)
                 : "q" (val)
                 : "memory");
}

/* 带写屏障的16位原子写入 */
static inline void atomic_write16_wmb(volatile void *addr, uint16_t val)
{
    asm volatile("sfence\n\t"
                 "movw %1, %0"
                 : "=m" (*(volatile uint16_t *)addr)
                 : "r" (val)
                 : "memory");
}

/* 带写屏障的32位原子写入 */
static inline void atomic_write32_wmb(volatile void *addr, uint32_t val)
{
    asm volatile("sfence\n\t"
                 "movl %1, %0"
                 : "=m" (*(volatile uint32_t *)addr)
                 : "r" (val)
                 : "memory");
}
/* 带写屏障的64位原子写入 */
static inline void atomic_write64_wmb(volatile void *addr, uint64_t val)
{
    asm volatile("sfence\n\t"
                 "movq %1, %0"
                 : "=m" (*(volatile uint64_t *)addr)
                 : "r" (val)
                 : "memory");
}
/* 带读屏障的8位原子读取 */
static inline uint8_t atomic_read8_rmb(volatile void *addr)
{
    uint8_t val;
    asm volatile("movb %1, %0\n\t"
                 "lfence"
                 : "=q" (val)
                 : "m" (*(volatile uint8_t *)addr)
                 : "memory");
    return val;
}

/* 带读屏障的16位原子读取 */
static inline uint16_t atomic_read16_rmb(volatile void *addr)
{
    uint16_t val;
    asm volatile("movw %1, %0\n\t"
                 "lfence"
                 : "=r" (val)
                 : "m" (*(volatile uint16_t *)addr)
                 : "memory");
    return val;
}

/* 带读屏障的32位原子读取 */
static inline uint32_t atomic_read32_rmb(volatile void *addr)
{
    uint32_t val;
    asm volatile("movl %1, %0\n\t"
                 "lfence"
                 : "=r" (val)
                 : "m" (*(volatile uint32_t *)addr)
                 : "memory");
    return val;
}
/* 带读屏障的64位原子读取 */
static inline uint64_t atomic_read64_rmb(volatile void *addr)
{
    uint64_t val;
    asm volatile("movq %1, %0\n\t"
                 "lfence"
                 : "=r" (val)
                 : "m" (*(volatile uint64_t *)addr)
                 : "memory");
    return val;
}

uint64_t rdmsr(uint32_t offset)
{
    uint32_t value_high, value_low; 
    asm volatile("rdmsr"
                 : "=a" (value_low),
                   "=d" (value_high)
                 : "c" (offset));
    return ((uint64_t)value_high << 32) | value_low;
}
void wrmsr(uint32_t offset, uint64_t value)
{
    uint32_t value_high=(value>>32)&0xffffffff, value_low=value&0xffffffff; 
    asm volatile("wrmsr"
                 :
                 : "c" (offset),
                   "a" (value_low),
                   "d" (value_high));
}