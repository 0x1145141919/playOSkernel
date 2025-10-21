#include "Interrupt.h"
#include "OS_utils.h"
#include "../memory/includes/kpoolmemmgr.h"
#include "../memory/includes/Memory.h"
#include "../memory/includes/phygpsmemmgr.h"
#include "panic.h"
// IO APIC寄存器索引定义

/**
 * 构造函数
 */
Interrupt_mgr_t::io_apic_mgr_t::io_apic_mgr_t(uint32_t io_apic_id, uint32_t regphybase) {
    asm ("cli");
    this->io_apic_id = io_apic_id;
    this->regphybase = regphybase;
    phy_memDescriptor*result=gBaseMemMgr.queryPhysicalMemoryUsage(regphybase);
    if(result==NULL)
    {
       phyaddr_t new_base=(regphybase>>12)<<12;
        gBaseMemMgr.registMMIO(new_base,1);
        vaddr_t vbase=(vaddr_t)gKspacePgsMemMgr.pgs_remapp(new_base,KernelSpacePgsMemMgr::kspace_mmio_flags);
        uint64_t offset=vbase-regphybase;
        regvbase=regphybase+offset;
    }else{
        uint64_t offset=result->VirtualStart-result->PhysicalStart;
        regvbase=regphybase+offset;
    }

    
    
    // 设置寄存器选择和窗口寄存器指针
    select_reg = (uint32_t*)regvbase;
    win_reg = (uint32_t*)(regvbase + 0x10);
    
    // 读取版本寄存器以获取重定向条目数量
    asm volatile("movl %1, %0" : "=m" (select_reg) : "r" (ID_REG) : "memory");
    asm volatile("mfence");
    typename Interrupt_mgr_t::io_apic_mgr_t::ver_reg* ver = (typename Interrupt_mgr_t::io_apic_mgr_t::ver_reg*)&this->win_reg[0];
    this->version = ver->version;
    this->redirection_entries_count = ver->max_redirection_entry_index + 1;
    
    // 分配重定向条目数组
    this->redirection_entry_array = new redirection_entry[redirection_entries_count];
    
    // 初始化所有重定向条目为默认值
    for (int i = 0; i < this->redirection_entries_count; i++) {
        this->redirection_entry_array[i].vector = 0;
        this->redirection_entry_array[i].deliveryMode = 0;
        this->redirection_entry_array[i].destinationMode = 0;
        this->redirection_entry_array[i].deliveryStatus = 0;
        this->redirection_entry_array[i].pinPolarity = 0;
        this->redirection_entry_array[i].remoteIRR = 0;
        this->redirection_entry_array[i].triggerMode = 0;
        this->redirection_entry_array[i].mask = 1;  // 默认屏蔽中断
        this->redirection_entry_array[i].reserved = 0;
        this->redirection_entry_array[i].Destination = 0;
    }
    asm ("sti");
}

/**
 * 析构函数
 */
Interrupt_mgr_t::io_apic_mgr_t::~io_apic_mgr_t() {
    if (redirection_entry_array) {
        delete[] this->redirection_entry_array;
        this->redirection_entry_array = nullptr;
    }
}

/**
 * 获取指定索引的重定向条目
 */
/**
 * 从IO APIC硬件读取指定索引的重定向条目
 * 注意：需要保证原子性操作
 */
typename Interrupt_mgr_t::io_apic_mgr_t::redirection_entry 
Interrupt_mgr_t::io_apic_mgr_t::get_redirection_entry(uint8_t index) {
    if (index >= this->redirection_entries_count) {
        // 返回一个默认的重定向条目（屏蔽的条目）
        typename Interrupt_mgr_t::io_apic_mgr_t::redirection_entry empty_entry = {0};
        empty_entry.mask = 1;
        return empty_entry;
    }
    
    // 禁用中断以保证原子性
    asm volatile("cli");
    
    // 计算IO APIC寄存器索引（每个重定向条目占2个32位寄存器）
    uint32_t reg_index = REDIRECTION_ENTRY_BASE + index * 2;
    
    // 读取低位双字
    asm volatile("movl %1, %0" : "=m" (*select_reg) : "r" (reg_index) : "memory");
    asm volatile("mfence");
    uint32_t low_dword = this->win_reg[0];
    
    // 读取高位双字  
    asm volatile("movl %1, %0" : "=m" (*select_reg) : "r" (reg_index + 1) : "memory");
    asm volatile("mfence");
    uint32_t high_dword = this->win_reg[0];
    
    // 恢复中断
    asm volatile("sti");
    
    // 组合成64位重定向条目
    uint64_t entry_value = ((uint64_t)high_dword << 32) | low_dword;
    
    // 转换为redirection_entry结构体
    typename Interrupt_mgr_t::io_apic_mgr_t::redirection_entry entry;
    *((uint64_t*)&entry) = entry_value;
    
    // 同时更新内存中的缓存数组
    this->redirection_entry_array[index] = entry;
    
    return entry;
}

/**
 * 写入指定索引的重定向条目（仅写入内存数组，不加载到硬件）
 */
int Interrupt_mgr_t::io_apic_mgr_t::write_redirection_entry(uint8_t index, typename Interrupt_mgr_t::io_apic_mgr_t::redirection_entry entry) {
    if (index >= this->redirection_entries_count) {
        return -1; // 索引超出范围
    }
    
    this->redirection_entry_array[index] = entry;
    return 0; // 成功
}

/**
 * 加载指定索引的重定向条目到IO APIC硬件
 */
int Interrupt_mgr_t::io_apic_mgr_t::load_redirection_entry(uint8_t index) {
    if (index >= this->redirection_entries_count) {
        return -1; // 索引超出范围
    }
    
    // 计算IO APIC寄存器索引（每个重定向条目占2个32位寄存器）
    uint32_t reg_index = REDIRECTION_ENTRY_BASE + index * 2;
    
    // 将64位重定向条目分为两个32位部分写入
    uint64_t entry_value = *((uint64_t*)&this->redirection_entry_array[index]);
    uint32_t low_dword = (uint32_t)(entry_value & 0xFFFFFFFF);
    uint32_t high_dword = (uint32_t)(entry_value >> 32);
    
    // 写入低位双字
    asm volatile("movl %1, %0" : "=m" (select_reg) : "r" (reg_index) : "memory");
        asm volatile("mfence");
    this->win_reg[0] = low_dword;
    
    // 写入高位双字
    asm volatile("movl %1, %0" : "=m" (select_reg) : "r" (reg_index + 1) : "memory");
        asm volatile("mfence");
    this->win_reg[0] = high_dword;
    
    return 0; // 成功
}

/**
 * 加载所有重定向条目到IO APIC硬件
 */
int Interrupt_mgr_t::io_apic_mgr_t::load_redirection_entry_array() {
    for (uint8_t i = 0; i < this->redirection_entries_count; i++) {
        int result = load_redirection_entry(i);
        if (result != 0) {
            return result; // 如果任何一个条目加载失败，返回错误
        }
    }
    return 0; // 成功
}