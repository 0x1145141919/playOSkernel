#include "memory/phyaddr_accessor.h"
#include "util/OS_utils.h"

extern "C" {
    extern uint8_t pml4_table_init;
    extern uint8_t pml5_table_init;
}

// 定义PhyAddrAccessor的静态成员变量
// 根据编译时宏定义选择合适的页表根地址
phyaddr_t PhyAddrAccessor::init_pgtb_root =
#ifdef PGLV_4
    (phyaddr_t)&pml4_table_init;
#elif defined(PGLV_5)
    (phyaddr_t)&pml5_table_init;
#else
    // pml4_table_init
    (phyaddr_t)&pml4_table_init;
#endif

bool PhyAddrAccessor::is_init_cr3()
{
    uint64_t cr3=0;
    asm ("mov %%cr3,%0" : "=r"(cr3));
    return (cr3>>12)==(init_pgtb_root>>12); 
}

uint8_t PhyAddrAccessor::readu8(phyaddr_t addr)
{
    if (is_init_cr3()) {
        // 如果使用初始化页表，可以直接访问物理地址（恒等映射）
        volatile uint8_t* ptr = (volatile uint8_t*)addr;
        return *ptr;
    }
    // TODO: 实现非初始化页表情况下的访问逻辑
    return 0;
}

uint16_t PhyAddrAccessor::readu16(phyaddr_t addr)
{
    if (is_init_cr3()) {
        // 如果使用初始化页表，可以直接访问物理地址（恒等映射）
        volatile uint16_t* ptr = (volatile uint16_t*)addr;
        return *ptr;
    }
    // TODO: 实现非初始化页表情况下的访问逻辑
    return 0;
}

uint32_t PhyAddrAccessor::readu32(phyaddr_t addr)
{
    if (is_init_cr3()) {
        // 如果使用初始化页表，可以直接访问物理地址（恒等映射）
        volatile uint32_t* ptr = (volatile uint32_t*)addr;
        return *ptr;
    }
    // TODO: 实现非初始化页表情况下的访问逻辑
    return 0;
}

uint64_t PhyAddrAccessor::readu64(phyaddr_t addr)
{
    if (is_init_cr3()) {
        // 如果使用初始化页表，可以直接访问物理地址（恒等映射）
        volatile uint64_t* ptr = (volatile uint64_t*)addr;
        return *ptr;
    }
    // TODO: 实现非初始化页表情况下的访问逻辑
    return 0;
}

void PhyAddrAccessor::writeu8(phyaddr_t addr, uint8_t value)
{
    if (is_init_cr3()) {
        // 如果使用初始化页表，可以直接访问物理地址（恒等映射）
        volatile uint8_t* ptr = (volatile uint8_t*)addr;
        *ptr = value;
        return;
    }
    // TODO: 实现非初始化页表情况下的访问逻辑
}

void PhyAddrAccessor::writeu16(phyaddr_t addr, uint16_t value)
{
    if (is_init_cr3()) {
        // 如果使用初始化页表，可以直接访问物理地址（恒等映射）
        volatile uint16_t* ptr = (volatile uint16_t*)addr;
        *ptr = value;
        return;
    }
    // TODO: 实现非初始化页表情况下的访问逻辑
}

void PhyAddrAccessor::writeu32(phyaddr_t addr, uint32_t value)
{
    if (is_init_cr3()) {
        // 如果使用初始化页表，可以直接访问物理地址（恒等映射）
        volatile uint32_t* ptr = (volatile uint32_t*)addr;
        *ptr = value;
        return;
    }
    // TODO: 实现非初始化页表情况下的访问逻辑
}

void PhyAddrAccessor::writeu64(phyaddr_t addr, uint64_t value)
{
    if (is_init_cr3()) {
        // 如果使用初始化页表，可以直接访问物理地址（恒等映射）
        volatile uint64_t* ptr = (volatile uint64_t*)addr;
        *ptr = value;
        return;
    }
    // TODO: 实现非初始化页表情况下的访问逻辑
}