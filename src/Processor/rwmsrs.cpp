#include "msr_offsets_definitions.h"
#include "os_error_definitions.h"
/* 64位版本的读取函数，返回uint64_t */
/**
 * 最好用头文件里面规定的msr偏移量，否则#GP后果自负
 */
static inline uint64_t rdmsr_no_verify(uint32_t msr)
{
    uint32_t low, high;
    
    asm volatile("rdmsr"
                 : "=a" (low), "=d" (high)
                 : "c" (msr)
                 : "memory");
    
    return ((uint64_t)high << 32) | low;
}

/* 64位版本的写入函数 */
static inline void wrmsr_no_verify(uint32_t msr, uint64_t value)
{
    uint32_t low = value & 0xFFFFFFFF;
    uint32_t high = value >> 32;
    
    asm volatile("wrmsr"
                 :
                 : "c" (msr), "a" (low), "d" (high)
                 : "memory");
}