#include "Interrupt.h"
#include "../memory/includes/Memory.h"
#include "OS_utils.h"
#include "../memory/includes/phygpsmemmgr.h"
#include "processor_Ks_stacks_mgr.h"
#include "gSTResloveAPIs.h"
#include "panic.h"
#include "VideoDriver.h"
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
    global_gdt.entries[kspace_CS_gdt_selector>>3] = kspace_CS_entry;
    // 内核数据段
    global_gdt.entries[kspace_DS_SS_gdt_selector>>3] = kspace_DS_SS_entry;
    // 用户代码段
    global_gdt.entries[userspace_CS_gdt_selector>>3] = userspace_CS_entry;
    // 用户数据段
    global_gdt.entries[userspace_DS_SS_gdt_selector>>3] = userspace_DS_SS_entry;

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
    global_idt[Interrupt_mgr_t::DIVIDE_ERROR].offset_low = (uint16_t)((uint64_t)exception_handler_div_by_zero & 0xFFFF);
    global_idt[Interrupt_mgr_t::DIVIDE_ERROR].offset_mid = (uint16_t)(((uint64_t)exception_handler_div_by_zero >> 16) & 0xFFFF);
   global_idt[Interrupt_mgr_t::DIVIDE_ERROR].offset_high = (uint32_t)(((uint64_t)exception_handler_div_by_zero >> 32) & 0xFFFFFFFF);
   
    // 注册新增的异常处理函数
    global_idt[Interrupt_mgr_t::INVALID_OPCODE].offset_low = (uint16_t)((uint64_t)exception_handler_invalid_opcode & 0xFFFF);
    global_idt[Interrupt_mgr_t::INVALID_OPCODE].offset_mid = (uint16_t)(((uint64_t)exception_handler_invalid_opcode >> 16) & 0xFFFF);
    global_idt[Interrupt_mgr_t::INVALID_OPCODE].offset_high = (uint32_t)(((uint64_t)exception_handler_invalid_opcode >> 32) & 0xFFFFFFFF);
    
    global_idt[Interrupt_mgr_t::GENERAL_PROTECTION_FAULT].offset_low = (uint16_t)((uint64_t)exception_handler_general_protection & 0xFFFF);
    global_idt[Interrupt_mgr_t::GENERAL_PROTECTION_FAULT].offset_mid = (uint16_t)(((uint64_t)exception_handler_general_protection >> 16) & 0xFFFF);
    global_idt[Interrupt_mgr_t::GENERAL_PROTECTION_FAULT].offset_high = (uint32_t)(((uint64_t)exception_handler_general_protection >> 32) & 0xFFFFFFFF);
    
    global_idt[Interrupt_mgr_t::DOUBLE_FAULT].offset_low = (uint16_t)((uint64_t)exception_handler_double_fault & 0xFFFF);
    global_idt[Interrupt_mgr_t::DOUBLE_FAULT].offset_mid = (uint16_t)(((uint64_t)exception_handler_double_fault >> 16) & 0xFFFF);
    global_idt[Interrupt_mgr_t::DOUBLE_FAULT].offset_high = (uint32_t)(((uint64_t)exception_handler_double_fault >> 32) & 0xFFFFFFFF);
    
    global_idt[Interrupt_mgr_t::PAGE_FAULT].offset_low = (uint16_t)((uint64_t)exception_handler_page_fault & 0xFFFF);
    global_idt[Interrupt_mgr_t::PAGE_FAULT].offset_mid = (uint16_t)(((uint64_t)exception_handler_page_fault >> 16) & 0xFFFF);
    global_idt[Interrupt_mgr_t::PAGE_FAULT].offset_high = (uint32_t)(((uint64_t)exception_handler_page_fault >> 32) & 0xFFFFFFFF);
    
    global_idt[Interrupt_mgr_t::INVALID_TSS].offset_low = (uint16_t)((uint64_t)exception_handler_invalid_tss & 0xFFFF);
    global_idt[Interrupt_mgr_t::INVALID_TSS].offset_mid = (uint16_t)(((uint64_t)exception_handler_invalid_tss >> 16) & 0xFFFF);
    global_idt[Interrupt_mgr_t::INVALID_TSS].offset_high = (uint32_t)(((uint64_t)exception_handler_invalid_tss >> 32) & 0xFFFFFFFF);
    
    global_idt[Interrupt_mgr_t::SIMD_FLOATING_POINT_EXCEPTION].offset_low = (uint16_t)((uint64_t)exception_handler_simd_floating_point & 0xFFFF);
    global_idt[Interrupt_mgr_t::SIMD_FLOATING_POINT_EXCEPTION].offset_mid = (uint16_t)(((uint64_t)exception_handler_simd_floating_point >> 16) & 0xFFFF);
    global_idt[Interrupt_mgr_t::SIMD_FLOATING_POINT_EXCEPTION].offset_high = (uint32_t)(((uint64_t)exception_handler_simd_floating_point >> 32) & 0xFFFFFFFF);
    
    global_idt[Interrupt_mgr_t::VIRTUALIZATION_EXCEPTION].offset_low = (uint16_t)((uint64_t)exception_handler_virtualization & 0xFFFF);
    global_idt[Interrupt_mgr_t::VIRTUALIZATION_EXCEPTION].offset_mid = (uint16_t)(((uint64_t)exception_handler_virtualization >> 16) & 0xFFFF);
    global_idt[Interrupt_mgr_t::VIRTUALIZATION_EXCEPTION].offset_high = (uint32_t)(((uint64_t)exception_handler_virtualization >> 32) & 0xFFFFFFFF);
    
    // 根据Intel手册，某些中断号未被使用，将它们的present位设置为0
    global_idt[15].present = 0;  // 中断号15未被使用
    // 中断号22-27未被使用
    for (int i = 22; i <= 27; i++) {
        global_idt[i].present = 0;
    }
    
    global_idt[Interrupt_mgr_t::DOUBLE_FAULT].ist_index = double_fault_exception_ist_index; // 双重错误使用不同的IST栈


    // 使用CPUID指令查询APIC ID
    uint32_t eax, ebx, ecx, edx;
    // CPUID指令获取APIC ID
    // leaf 1, subleaf 0 contains APIC ID in EBX[31:24]
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
    processor_Interrupt_init(bsp_apic_id);
    //解析MADT表，加载IOAPIC信息
    MADT_Table*madt_table=(MADT_Table*)gAcpiVaddrSapceMgr.get_acpi_table("APIC");
    uint32_t processor_count=0;
    uint32_t io_apic_count=0;
    uint8_t*base_addr=(uint8_t*)madt_table;
    uint8_t*scannr=base_addr+sizeof(MADT_Table);
    uint8_t*end_addr=base_addr+madt_table->Header.Length;
    while (scannr<end_addr)
    {
     switch(*scannr)
     {
    case MADT_entrytype::IOAPIC: 
    {io_apic_structure*structure=(io_apic_structure*)scannr;
    io_apic_mgr_array[io_apic_count]=new io_apic_mgr_t(structure->io_apic_id,structure->io_apic_address);
    scannr+=sizeof(io_apic_structure); }
    break;
    case (uint8_t)MADT_entrytype::Lx2APIC_NMI: 
    scannr+=sizeof(local_x2apic_nmi_structure);
    break;
     case MADT_entrytype::LAPIC_NMI: 
     scannr+=sizeof(local_apic_nmi_structure);
     break;
     case MADT_entrytype::NMI_Source: 
     scannr+=sizeof(nmi_source_structure);
     break;
     case MADT_entrytype::x2LocalAPIC: 
     processor_count++;
     scannr+=sizeof(processor_local_x2apic_structure);
     break; 
     case MADT_entrytype::LocalAPIC: 
     processor_count++;
     scannr+=sizeof(Local_APIC_entry);
     break;

     default:
     kputsSecure("Unsupported MADT entry type");
     kpnumSecure(scannr,UNHEX,1);

     gkernelPanicManager.panic("Unsupported MADT entry type");
    }
    }
    total_processor_count=processor_count;
}
// 对于某个apic_id的核心进行中断初始化
int Interrupt_mgr_t::processor_Interrupt_init(uint32_t apic_id)
{
    local_processor_interrupt_mgr_array[apic_id]=new Local_processor_Interrupt_mgr_t(apic_id);
    return OS_SUCCESS;
}

int Interrupt_mgr_t::processor_Interrupt_register(uint32_t apic_id, uint8_t interrupt_number, void *handler)
{
    Local_processor_Interrupt_mgr_t*local_processor_interrupt_mgr=local_processor_interrupt_mgr_array[apic_id];
    if (apic_id>=total_processor_count)
    {
        return OS_INVALID_PARAMETER ;
    }
    
    return local_processor_interrupt_mgr->register_handler(interrupt_number,handler);
}

int Interrupt_mgr_t::processor_Interrupt_unregister(uint32_t apic_id, uint8_t interrupt_number)
{
    if(apic_id>=total_processor_count)return OS_INVALID_PARAMETER ;
    Local_processor_Interrupt_mgr_t*local_processor_interrupt_mgr=local_processor_interrupt_mgr_array[apic_id];
    return local_processor_interrupt_mgr->unregister_handler(interrupt_number);
}

int Interrupt_mgr_t::set_processor_count(uint32_t count)
{
    if(total_processor_count)return OS_BAD_FUNCTION;
    total_processor_count=count;
    return OS_SUCCESS;
}

int Interrupt_mgr_t::io_apic_rte_set(uint8_t io_apicid, uint8_t rte_index, io_apic_mgr_t::redirection_entry entry)
{   
    if(io_apicid>=16)return OS_INVALID_PARAMETER;
    int status;
    status=io_apic_mgr_array[io_apicid]->write_redirection_entry(rte_index,entry);
    if (status!=OS_SUCCESS)
    {
        /* code */
    }
    
    status = io_apic_mgr_array[io_apicid]->load_redirection_entry(rte_index);
    if (status!=OS_SUCCESS)
    {
        /* code */
    }
    return OS_SUCCESS;
}

Interrupt_mgr_t::io_apic_mgr_t::redirection_entry Interrupt_mgr_t::io_apic_rte_get(uint8_t io_apicid, uint8_t rte_index)
{
    return io_apic_mgr_array[io_apicid]->get_redirection_entry(rte_index);
}

int Interrupt_mgr_t::Local_processor_Interrupt_mgr_t::register_handler(uint8_t interrupt_number, void *handler)
{
    uint64_t handler_addr = (uint64_t)handler;
    Interrupt_mgr_t::IDTEntry&entry=this->IDTEntry[interrupt_number];
    if(handler_addr<0xffff800000000000)//内核地址空间
    {
        return OS_INVALID_ADDRESS;
    }
    entry.offset_low = (uint16_t)(handler_addr & 0xFFFF);
    entry.offset_mid = (uint16_t)((handler_addr >> 16) & 0xFFFF);
    entry.offset_high = (uint32_t)((handler_addr >> 32) & 0xFFFFFFFF);
    entry.present = 1;

    return 0;
}

int Interrupt_mgr_t::Local_processor_Interrupt_mgr_t::unregister_handler(uint8_t interrupt_number)
{
    Interrupt_mgr_t::IDTEntry& entry = this->IDTEntry[interrupt_number];
    entry.present = 0;
    return 0;
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
    uint64_t cs_selector = kspace_CS_gdt_selector;
    asm volatile("pushq %0\n"
                 "leaq 1f(%%rip), %%rax\n"
                 "pushq %%rax\n"
                 "lretq\n"
                 "1:\n"
                 :
                 : "r"(cs_selector)
                 : "rax");
        IDTR idtr;
        idtr.limit = sizeof(IDTEntry) - 1;
        idtr.base = (uint64_t)IDTEntry;
    asm volatile("lidt %0"::"m"(idtr));
    uint16_t tss_gdt_index = gInterrupt_mgr.gdt_headcount + apic_id * 2;
    asm volatile("ltr %w0"::"r"(tss_gdt_index<<3));
}