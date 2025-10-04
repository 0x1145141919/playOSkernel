#pragma once
#include "stdint.h"
typedef uint64_t vaddr_t;
typedef uint8_t bitset512_t[64];
class processor_Ks_stacks_mgr
{
private:
    static constexpr uint64_t ks_stack_size = 16384; // 每个内核栈大小16KB
    static constexpr uint64_t ks_stack_num = 2<<10;
    static constexpr uint64_t heap_size = ks_stack_size * ks_stack_num; // 16MB堆空间
    static constexpr uint64_t bitset512count= heap_size /( (2<<12)*4*(2<<9)); 
    bitset512_t heap_maps[bitset512count];
    /* data */
    vaddr_t heap_base;
public:
    void Init();
    //栈是向下生长的注意
    vaddr_t AllocateStack();
    void FreeStack(vaddr_t stack_base);
    processor_Ks_stacks_mgr(/* args */);
    ~processor_Ks_stacks_mgr();
};
extern processor_Ks_stacks_mgr gProcessor_Ks_stacks_mgr;


