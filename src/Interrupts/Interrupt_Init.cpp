#include "Interrupt.h"
#include "../memory/includes/Memory.h"
#include "OS_utils.h"
#include "../memory/includes/phygpsmemmgr.h"
#include "processor_Ks_stacks_mgr.h"
/*

*/
Interrupt_mgr_t gInterrupt_mgr;
Interrupt_mgr_t::Interrupt_mgr_t()//假构造函数，占位不让编译器报错
{
}
/*
初始化全局中断资源如
全局idt共用部分
gdt表（所有核心共享）
gdtr结构
然后调用Local_processor_Interrupt_mgr_t为bsp初始化
先查询apic_id再初始化
*/
void cpuid(unsigned int leaf, unsigned int subleaf, unsigned int *eax, unsigned int *ebx, unsigned int *ecx, unsigned int *edx) {
    asm volatile("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf)
    );
}
void Interrupt_mgr_t::Init()//真正的初始化函数
{
    // 初始化全局GDT表
    // 内核代码段
    global_gdt.entries[1] = kspace_CS_entry;
    // 内核数据段
    global_gdt.entries[2] = kspace_DS_SS_entry;
    // 用户代码段
    global_gdt.entries[3] = userspace_CS_entry;
    // 用户数据段
    global_gdt.entries[4] = userspace_DS_SS_entry;
    
    // 设置GDTR
    global_gdt_ptr.limit = sizeof(global_gdt) - 1;
    global_gdt_ptr.base = (uint64_t)&global_gdt;
    
    // 初始化全局IDT（前32项）
    for (int i = 0; i < 32; i++) {
        global_idt[i] = {}; // 清零
        global_idt[i].segment_selector = 0x08; // 内核代码段选择子
        global_idt[i].type = 0b1110; // 中断门
        global_idt[i].present = 1;
        global_idt[i].ist_index = 1;
        global_idt[i].dpl = 0; // 内核特权级
    }
    
    // 根据Intel手册，某些中断号未被使用，将它们的present位设置为0
    global_idt[15].present = 0;  // 中断号15未被使用
    // 中断号22-27未被使用
    for (int i = 22; i <= 27; i++) {
        global_idt[i].present = 0;
    }
    
    global_idt[Interrupt_mgr_t::DOUBLE_FAULT].ist_index = double_fault_exception_ist_index; // 双重错误使用不同的IST栈

    
    // TODO: 需要设置实际的中断处理程序地址
    
    // 为BSP初始化Local_processor_Interrupt_mgr_t
    // 使用CPUID指令查询APIC ID
    uint32_t eax, ebx, ecx, edx;
    // CPUID指令获取APIC ID
    // leaf 1, subleaf 0 contains APIC ID in EBX[31:24]
    uint32_t bsp_apic_id = 0;
 asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
if (eax >= 0xB) {
    // 使用 CPUID leaf 0xB 获取 x2APIC ID
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(0xB), "c"(0));
    bsp_apic_id = edx; // x2APIC ID 在 EDX 中
} else {
    // 回退到传统 APIC（8 位）
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(1), "c"(0));
    bsp_apic_id = (ebx >> 24) & 0xFF; // APIC ID 在 EBX[31:24]
}   
        cpuid(0xB, 1, &eax, &ebx, &ecx, &edx);
    if (ecx == 2) {  // Level Type = 2 (Core)
        total_processor_count = ebx & 0xFFFF;  // EBX[15:0] = 物理核心数量
    }
    processor_Interrupt_init(bsp_apic_id);
}
//对于某个apic_id的核心进行中断初始化
int Interrupt_mgr_t::processor_Interrupt_init(uint32_t apic_id)
{
    local_processor_interrupt_mgr_array[apic_id]=new Local_processor_Interrupt_mgr_t(apic_id);
    return OS_SUCCESS;
}

Interrupt_mgr_t::Local_processor_Interrupt_mgr_t::Local_processor_Interrupt_mgr_t(uint32_t apic_id)
{
    this->apic_id=apic_id;
    //复制全局idt模板

    //初始化tss
    tss={};
    tss.ist[0]=0;//根据文档ist[0]不使用,必须分配为NULL
    tss.io_map_base_offset=0;//不使用io_map
    tss.rsp0=(uint64_t)gProcessor_Ks_stacks_mgr.AllocateStack();//分配内核栈给rsp0
    tss.ist[1]=(uint64_t)gProcessor_Ks_stacks_mgr.AllocateStack();//分配内核栈给ist1,这个是中断处理程序使用的栈
    tss.ist[2]=(uint64_t)gProcessor_Ks_stacks_mgr.AllocateStack();//分配内核栈给ist2,这个是double fault使用的栈
    //ist3-7暂时不使用
        for(int i=0;i<32;i++)
    {
        IDTEntry[i]=gInterrupt_mgr.global_idt[i];
    }
    x64GDT*gdt=&gInterrupt_mgr.global_gdt;
    gdt->tss_entry[apic_id]=kspace_TSS_entry;
    uint64_t tss_content=(uint64_t)(&tss);
    gdt->tss_entry[apic_id].base0=tss_content&base0_mask;
    gdt->tss_entry[apic_id].base1=(tss_content>>16)&(base1_mask);
    gdt->tss_entry[apic_id].base2=(tss_content>>24)&(base2_mask);
    gdt->tss_entry[apic_id].base3=(tss_content>>32)&(base3_mask);
    asm volatile("lgdt %0"::"m"(gInterrupt_mgr.global_gdt_ptr));
    uint16_t tss_gdt_index=gInterrupt_mgr.gdt_headcount+apic_id*2;
    asm volatile("ltr %w0"::"r"(tss_gdt_index<<3));
}