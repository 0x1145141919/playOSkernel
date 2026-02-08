#pragma once
#include "stdint.h"
#include "stddef.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t size_t;
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

int strcmp_in_kernel(const char *str1, const char *str2,uint32_t max_strlen=4096);
int strlen_in_kernel(const char *s);
void ksetmem_8(void* ptr, uint8_t value, uint64_t size_in_byte);
void ksetmem_16(void* ptr, uint16_t value, uint64_t size_in_byte);
void ksetmem_32(void* ptr, uint32_t value, uint64_t size_in_byte);
void ksetmem_64(void* ptr, uint64_t value, uint64_t size_in_byte);
extern "C" void __wrap___stack_chk_fail(void);
uint64_t align_up(uint64_t value, uint64_t alignment);
extern const uint8_t bit_reverse_table[256];
uint64_t reverse_perbytes(uint64_t value) ;
void atomic_write8_wmb(volatile void *addr, uint8_t val);
void atomic_write16_wmb(volatile void *addr, uint16_t val);
void atomic_write32_wmb(volatile void *addr, uint32_t val);
void atomic_write64_wmb(volatile void *addr, uint64_t val);
uint8_t atomic_read8_rmb(volatile void *addr);
uint16_t atomic_read16_rmb(volatile void *addr);
uint32_t atomic_read32_rmb(volatile void *addr);
uint64_t atomic_read64_rmb(volatile void *addr);
void atomic_write8_rdbk(volatile void *addr, uint8_t val);
void atomic_write16_rdbk(volatile void *addr, uint16_t val);
void atomic_write32_rdbk(volatile void *addr, uint32_t val);
void atomic_write64_rdbk(volatile void *addr, uint64_t val);
uint64_t align_down(uint64_t value, uint64_t alignment);
uint64_t rdmsr(uint32_t offset);
void wrmsr(uint32_t offset,uint64_t value);
uint64_t rdtsc();
uint64_t read_gs_u64(size_t index);
void gs_u64_write(uint32_t index, uint64_t value);
#ifdef USER_MODE
// 将内存描述符表从文本格式转换为gBaseMemMgr.Init函数所需的格式
void print_memory_descriptor_for_gbasememmgr(const char* input_file_path);
int convert_memory_descriptor_table(const char* input_file_path, const char* output_file_path);
#endif

#ifdef __cplusplus
}
#endif