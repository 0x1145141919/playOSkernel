#include "util/OS_utils.h"
#include "stdint.h"
#ifdef USER_MODE
#include <x86intrin.h>
#endif
#ifdef KERNEL_MODE 
#include "kintrin.h"
#endif
typedef uint64_t size_t;


uint64_t align_down(uint64_t x, uint64_t a){ return x & ~(a-1); }
int strcmp_in_kernel(const char *str1, const char *str2, uint32_t max_strlen)
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

int strlen_in_kernel(const char *s)
{
    int len = 0;
    while (*s++)
        len++;
    return len;
}

int strncmp(const char* str1, const char* str2, size_t n) { 
    while (n-- && *str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    if (n == (size_t)-1) return 0; // 比较了n个字符且都相等
    return *(unsigned char*)str1 - *(unsigned char*)str2;
}

void ksetmem_8(void* ptr, uint8_t value, uint64_t size_in_byte) {
    if (size_in_byte == 0) return;
    
    uint8_t* p = static_cast<uint8_t*>(ptr);
    
    // 使用 rep stos 指令（最快）
    __asm__ volatile (
        "cld\n\t"                   // 清除方向标志（向前）
        "rep stosb\n\t"            // 重复存储字节
        : "+D" (p), "+c" (size_in_byte), "+a" (value)  // 输入/输出
        :                           // 无额外输入
        : "memory", "cc"           // 可能修改内存和标志寄存器
    );
}

void ksetmem_16(void* ptr, uint16_t value, uint64_t size_in_byte) {
    if (size_in_byte == 0) return;
    uint64_t count = size_in_byte / 2;
    uint16_t* p = static_cast<uint16_t*>(ptr);
    if (count) {
        __asm__ volatile (
            "cld\n\t"
            "rep stosw\n\t"
            : "+D" (p), "+c" (count), "+a" (value)
            :
            : "memory", "cc"
        );
    }
    if (size_in_byte & 1) {
        ksetmem_8(reinterpret_cast<uint8_t*>(p), static_cast<uint8_t>(value), 1);
    }
}

void ksetmem_32(void* ptr, uint32_t value, uint64_t size_in_byte) {
    if (size_in_byte == 0) return;
    uint64_t count = size_in_byte / 4;
    uint32_t* p = static_cast<uint32_t*>(ptr);
    if (count) {
        __asm__ volatile (
            "cld\n\t"
            "rep stosl\n\t"
            : "+D" (p), "+c" (count), "+a" (value)
            :
            : "memory", "cc"
        );
    }
    uint64_t tail = size_in_byte & 3;
    if (tail) {
        ksetmem_8(reinterpret_cast<uint8_t*>(p), static_cast<uint8_t>(value), tail);
    }
}

void ksetmem_64(void* ptr, uint64_t value, uint64_t size_in_byte) {
    if (size_in_byte == 0) return;
    uint64_t count = size_in_byte / 8;
    uint64_t* p = static_cast<uint64_t*>(ptr);
    if (count) {
        __asm__ volatile (
            "cld\n\t"
            "rep stosq\n\t"
            : "+D" (p), "+c" (count), "+a" (value)
            :
            : "memory", "cc"
        );
    }
    uint64_t tail = size_in_byte & 7;
    if (tail) {
        ksetmem_8(reinterpret_cast<uint8_t*>(p), static_cast<uint8_t>(value), tail);
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
{
    // 如果源地址和目标地址相同或者长度为0，无需复制
    if (src == dest || length == 0) {
        return;
    }

    // 检查内存区域是否有重叠
    bool overlap = (uint8_t*)src < (uint8_t*)dest + length && (uint8_t*)dest < (uint8_t*)src + length;

    if (!overlap) {
        // 没有重叠，可以直接使用rep movsb
        asm volatile (
            "rep movsb"
            : "=&D"(dest), "=&S"(src), "=&c"(length)
            : "0"(dest), "1"(src), "2"(length)
            : "memory"
        );
    } else {
        // 存在重叠，需要根据src和dest的相对位置决定复制方向
        if (src < dest) {
            // 从后往前复制，避免覆盖未复制的数据
            src = (uint8_t*)src + length - 1;
            dest = (uint8_t*)dest + length - 1;
            
            asm volatile (
                "std\n\t"           // 设置方向标志位，使地址递减
                "rep movsb\n\t"     // 执行复制
                "cld"               // 清除方向标志位，恢复默认递增
                : "=&D"(dest), "=&S"(src), "=&c"(length)
                : "0"(dest), "1"(src), "2"(length)
                : "memory"
            );
        } else {
            // 从前往后复制
            asm volatile (
                "rep movsb"
                : "=&D"(dest), "=&S"(src), "=&c"(length)
                : "0"(dest), "1"(src), "2"(length)
                : "memory"
            );
        }
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

// 获取2bit宽度位图中指定索引的值（返回0-3）
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
 void atomic_write8_wmb(volatile void *addr, uint8_t val)
{
    asm volatile("sfence\n\t"
                 "movb %1, %0"
                 : "=m" (*(volatile uint8_t *)addr)
                 : "q" (val)
                 : "memory");
}

/* 带写屏障的16位原子写入 */
  void atomic_write16_wmb(volatile void *addr, uint16_t val)
{
    asm volatile("sfence\n\t"
                 "movw %1, %0"
                 : "=m" (*(volatile uint16_t *)addr)
                 : "r" (val)
                 : "memory");
}

/* 带写屏障的32位原子写入 */
 void atomic_write32_wmb(volatile void *addr, uint32_t val)
{
    asm volatile("sfence\n\t"
                 "movl %1, %0"
                 : "=m" (*(volatile uint32_t *)addr)
                 : "r" (val)
                 : "memory");
}
/* 带写屏障的64位原子写入 */
  void atomic_write64_wmb(volatile void *addr, uint64_t val)
{
    asm volatile("sfence\n\t"
                 "movq %1, %0"
                 : "=m" (*(volatile uint64_t *)addr)
                 : "r" (val)
                 : "memory");
}
/* 带读屏障的8位原子读取 */
  uint8_t atomic_read8_rmb(volatile void *addr)
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
 uint16_t atomic_read16_rmb(volatile void *addr)
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
  uint32_t atomic_read32_rmb(volatile void *addr)
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
  uint64_t atomic_read64_rmb(volatile void *addr)
{
    uint64_t val;
    asm volatile("movq %1, %0\n\t"
                 "lfence"
                 : "=r" (val)
                 : "m" (*(volatile uint64_t *)addr)
                 : "memory");
    return val;
}

/* 带回读验证的8位原子写入 */
 void atomic_write8_rdbk(volatile void *addr, uint8_t val)
{
    *(volatile uint8_t *)addr = val;
    uint8_t read_val;
    do {
        read_val = *(volatile uint8_t *)addr;
    } while(read_val != val);  // 等待直到回读值与写入值一致
}

/* 带回读验证的16位原子写入 */
 void atomic_write16_rdbk(volatile void *addr, uint16_t val)
{
    *(volatile uint16_t *)addr = val;
    uint16_t read_val;
    do {
        read_val = *(volatile uint16_t *)addr;
    } while(read_val != val);  // 等待直到回读值与写入值一致
}

/* 带回读验证的32位原子写入 */
 void atomic_write32_rdbk(volatile void *addr, uint32_t val)
{
    *(volatile uint32_t *)addr = val;
    uint32_t read_val;
    do {
        read_val = *(volatile uint32_t *)addr;
    } while(read_val != val);  // 等待直到回读值与写入值一致
}

/* 带回读验证的64位原子写入 */
void atomic_write64_rdbk(volatile void *addr, uint64_t val)
{
    *(volatile uint64_t *)addr = val;
    uint64_t read_val;
    do {
        read_val = *(volatile uint64_t *)addr;
    } while(read_val != val);  // 等待直到回读值与写入值一致
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
uint64_t rdtsc() {
    unsigned int lo, hi;
    __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}
uint64_t read_gs_u64(size_t index)
{
    uint64_t value = 0;
    uint64_t offset = index * sizeof(uint64_t);
    
    asm volatile(
        "movq %%gs:(%[offset]), %[value]"
        : [value] "=r" (value)
        : [offset] "r" (offset)
        : "memory"
    );
    return value;
}

void gs_u64_write(uint32_t index, uint64_t value)
{
    uint64_t offset = index * sizeof(uint64_t);
    
    asm volatile(
        "movq %[val], %%gs:(%[offset])"
        :
        : [offset] "r" (offset),
          [val] "r" (value)
        : "memory"
    );
}
