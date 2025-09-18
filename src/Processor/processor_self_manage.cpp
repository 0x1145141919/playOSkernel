#include    "stdint.h"
#include "processor_self_manage.h"
uint64_t LocalCPU::set_bit(uint64_t value, uint8_t bit_position) const {
    return value | (1ULL << bit_position);
}

uint64_t LocalCPU::clear_bit(uint64_t value, uint8_t bit_position) const {
    return value & ~(1ULL << bit_position);
}

uint64_t LocalCPU::get_cr3_address_mask() const {
    // 计算物理地址位宽 (MAXPHYADDR)
    const uint8_t addr_bits = max_phy_addr;
    
    // 生成掩码：bits [M-1:12] 为1，其余为0
    if (addr_bits <= 12) return 0;
    const uint64_t addr_mask = ((1ULL << (addr_bits - 12)) - 1) << 12;
    
    // 确保不覆盖保留位和LAM位
    return addr_mask & ~CR3_LAM_MASK & ~CR3_RESERVED_MASK;
}

LocalCPU::LocalCPU()
{
    // 读取控制寄存器
        asm volatile("mov %%cr0, %0" : "=r"(cr0));
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        asm volatile("mov %%cr3, %0" : "=r"(cr3));
        asm volatile("mov %%cr4, %0" : "=r"(cr4));
        asm volatile("mov %%cr8, %0" : "=r"(cr8));
        
        // 读取 RFLAGS
       
        asm volatile("pushfq;  pop %0 " : "=r"(rflags));
        
        // 读取 IDTR 和 GDTR
        struct {
            uint16_t limit;
            uint64_t base;
        } __attribute__((packed)) idtr_temp, gdtr_temp;
        
        asm volatile("sidt %0" : "=m"(idtr_temp));
        asm volatile("sgdt %0" : "=m"(gdtr_temp));
        
        idtr.base = idtr_temp.base;
        idtr.limit = idtr_temp.limit;
        gdtr.base = gdtr_temp.base;
        gdtr.limit = gdtr_temp.limit;
        uint32_t ia32_pat_high=0;
        uint32_t ia32_pat_low=0;
        asm volatile (
        "rdmsr"
        : "=a" (ia32_pat_low), "=d" (ia32_pat_high) // "=a" 输出到 low (eax), "=d" 输出到 high (edx)
        : "c" (0x277)             // "c" 表示将 0x277 放入 ecx
        : // 显式声明可能被修改的寄存器（此处 ecx 已通过输入约束告知编译器，通常无需额外声明）
    );
        ia32_pat = ia32_pat_low | (static_cast<uint64_t>(ia32_pat_high) << 32);
        // 其他寄存器初始化为0（暂时不初始化）
        tr = 0;
        dr0 = dr1 = dr2 = dr3 = dr6 = dr7 = 0;
        ia32_efer = ia32_fs_base = ia32_gs_base = ia32_kernel_gs_base = ia32_lstar = 0;
        mxcsr = 0;
        xcr0 = 0;
}
LocalCPU::~LocalCPU()
{
}
void LocalCPU::set_cr0_bit(CR0Bits bit, bool enable)
{
    if (enable) {
        cr0 = set_bit(cr0, static_cast<uint8_t>(bit));
    } else {
        cr0 = clear_bit(cr0, static_cast<uint8_t>(bit));
    }
}

void LocalCPU::set_cr4_bit(CR4Bits bit, bool enable) {
    if (enable) {
        cr4 = set_bit(cr4, static_cast<uint8_t>(bit));
    } else {
        cr4 = clear_bit(cr4, static_cast<uint8_t>(bit));
    }
}

bool LocalCPU::get_cr4_bit(CR4Bits bit) const {
    return (cr4 >> static_cast<uint8_t>(bit)) & 1;
}

void LocalCPU::set_cr3_base_address(uint64_t base_addr) {
    const uint64_t addr_mask = get_cr3_address_mask();
    
    // 4KB对齐处理
    base_addr &= ~0xFFFULL;
    
    // 根据PCIDE状态选择处理方式
    if (get_cr4_bit(CR4Bits::PCIDE)) {
        // PCIDE=1模式：保留PCID部分
        cr3 = (cr3 & ~CR3_PCIDE1_ADDR_MASK) | (base_addr & addr_mask);
    } else {
        // PCIDE=0模式：保留PWT/PCD部分
        const uint64_t attr_mask = (1ULL << (uint8_t)CR3Bits::PWT) | (1ULL << (uint8_t)CR3Bits::PCD);
        cr3 = (cr3 & ~addr_mask & ~CR3_PCIDE0_IGNORED_MASK) 
            | (base_addr & addr_mask)
            | (cr3 & attr_mask);
    }
    
    // 强制保留位为0
    cr3 &= ~CR3_RESERVED_MASK;
}

void LocalCPU::set_cr3_pcid(uint16_t pcid) {
    if (get_cr4_bit(CR4Bits::PCIDE)) {
        cr3 = (cr3 & ~CR3_PCID_MASK) | (pcid & 0xFFF);
        cr3 &= ~CR3_RESERVED_MASK; // 确保保留位为0
    }
    // PCIDE=0时忽略操作
}

void LocalCPU::set_cr3_bit(CR3Bits bit, bool enable) {
    const uint8_t pos = static_cast<uint8_t>(bit);
    
    // 特殊处理：LAM48在LAM57启用时忽略
    if (bit == CR3Bits::LAM48 && (cr3 & (1ULL << (uint8_t)CR3Bits::LAM57))) {
        return; // 忽略设置
    }
    
    // 通用位设置
    if (enable) {
        cr3 |= (1ULL << pos);
    } else {
        cr3 &= ~(1ULL << pos);
    }
    
    // 特殊处理：PWT/PCD仅在PCIDE=0时有效
    if ((bit == CR3Bits::PWT || bit == CR3Bits::PCD) && 
        get_cr4_bit(CR4Bits::PCIDE)) 
    {
        cr3 &= ~(1ULL << pos); // 强制清除
    }
    
    // 强制保留位为0
    cr3 &= ~CR3_RESERVED_MASK;
}

uint64_t LocalCPU::get_cr3_base_address() const {
    return cr3 & get_cr3_address_mask();
}

uint16_t LocalCPU::get_cr3_pcid() const {
    return (get_cr4_bit(CR4Bits::PCIDE)) ? (cr3 & 0xFFF) : 0;
}
// 辅助函数实现

// CR0 相关接口实现
uint64_t LocalCPU::get_cr0() const {
    return cr0;
}

void LocalCPU::set_cr0(uint64_t value) {
    // 在实际内核中，这里可以添加权限检查
    cr0 = value;
}

void LocalCPU::load_cr0() {
    // 内联汇编实现将cr0加载到物理寄存器
    asm volatile (
        "mov %0, %%cr0"
        : // 无输出
        : "r" (cr0)
        : "memory"
    );
}

bool LocalCPU::get_cr0_bit(CR0Bits bit) const {
    uint8_t bit_pos = static_cast<uint8_t>(bit);
    return (cr0 & (1ULL << bit_pos)) != 0;
}

// CR4 相关接口实现
uint64_t LocalCPU::get_cr4() const {
    return cr4;
}

void LocalCPU::set_cr4(uint64_t value) {
    // 在实际内核中，这里可以添加权限检查
    cr4 = value;
}

void LocalCPU::load_cr4() {
    // 内联汇编实现将cr4加载到物理寄存器
    asm volatile (
        "mov %0, %%cr4"
        : // 无输出
        : "r" (cr4)
        : "memory"
    );
}

// CR3 相关接口实现
uint64_t LocalCPU::get_cr3() const {
    return cr3;
}

void LocalCPU::set_cr3(uint64_t value) {
    // 在实际内核中，这里可以添加权限检查
    cr3 = value;
}

void LocalCPU::load_cr3() {
    // 内联汇编实现将cr3加载到物理寄存器
    asm volatile (
        "mov %0, %%cr3"
        : // 无输出
        : "r" (cr3)
        : "memory"
    );
}




// CR2 相关接口实现
uint64_t LocalCPU::get_cr2()  {
    return cr2;
}

void LocalCPU::set_cr2(uint64_t value) {
    // 在实际内核中，这里可以添加权限检查
    cr2 = value;
}

void LocalCPU::load_cr2() {
    // 内联汇编实现将cr2加载到物理寄存器
    asm volatile (
        "mov %0, %%cr2"
        : // 无输出
        : "r" (cr2)
        : "memory"
    );
}

// CR8 相关接口实现
uint64_t LocalCPU::get_cr8()  {
    return cr8;
}

void LocalCPU::set_cr8(uint64_t value) {
    // 在实际内核中，这里可以添加权限检查
    cr8 = value;
}

void LocalCPU::load_cr8() {
    // 内联汇编实现将cr8加载到物理寄存器
    asm volatile (
        "mov %0, %%cr8"
        : // 无输出
        : "r" (cr8)
        : "memory"
    );
}

uint64_t LocalCPU::get_ia32_pat() 
{
    uint32_t ia32_pat_high=0;
        uint32_t ia32_pat_low=0;
        asm volatile (
        "rdmsr"
        : "=a" (ia32_pat_low), "=d" (ia32_pat_high) // "=a" 输出到 low (eax), "=d" 输出到 high (edx)
        : "c" (0x277)             // "c" 表示将 0x277 放入 ecx
        : // 显式声明可能被修改的寄存器（此处 ecx 已通过输入约束告知编译器，通常无需额外声明）
       );
        ia32_pat = ia32_pat_low | (static_cast<uint64_t>(ia32_pat_high) << 32);
        return ia32_pat;
}

void LocalCPU::set_ia32_pat(uint64_t value)
{
    ia32_pat=value;
}

void LocalCPU::load_ia32_pat()
{
    asm volatile (
        "wrmsr"
        : // 输出部分（此处没有输出）
        : "a" (ia32_pat & 0xFFFFFFFF), "d" (ia32_pat >> 32), "c" (0x277) // "a" 输入到 low (eax), "d" 输入到 high (edx)
        : // 显式声明可能被修改的寄存器（此处 ecx 已通过输入约束告知编译器，通常无需额外声明）
    );
}
