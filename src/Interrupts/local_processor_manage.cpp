#include "Interrupt.h"
#include "util/OS_utils.h"
#include "memory/phygpsmemmgr.h"
#include "memory/AddresSpace.h"
#include "gSTResloveAPIs.h"
#include "panic.h"
#include "VideoDriver.h"
#include "util/cpuid_intel.h"
#include "msr_offsets_definitions.h"
static constexpr TSSDescriptorEntry kspace_TSS_entry = {
    .limit = sizeof(TSSentry) ,
    .base0 = 0,
    .base1 = 0,
    .type = 0x9, // 64位可用TSS
    .zero = 0,
    .dpl = 0,
    .p = 1,
    .limit1 = (sizeof(TSSentry) - 1) >> 16,
    .avl = 0,
    .reserved = 0,
    .g = 0, // 字节粒度
    .base2 = 0,
    .base3 = 0,
    .reserved2 = 0
};
local_processor::local_processor()
{
    x2apicid_t id=query_x2apicid();
    this->apic_id=id;
    gdt.entries[K_cs_idx]=kspace_CS_entry;
    gdt.entries[K_ds_ss_idx]=kspace_DS_SS_entry;
    gdt.entries[U_cs_idx]=userspace_CS_entry;
    gdt.entries[U_ds_ss_idx]=userspace_DS_SS_entry;
    gdt.tss_entry=kspace_TSS_entry;
    TSSDescriptorEntry& tss_entry=gdt.tss_entry;
    tss_entry.base0=static_cast<uint32_t>(reinterpret_cast<uint64_t>(&this->tss))&base0_mask;
    tss_entry.base1=static_cast<uint32_t>(reinterpret_cast<uint64_t>(&this->tss)>>16)&base1_mask;
    tss_entry.base2=static_cast<uint32_t>(reinterpret_cast<uint64_t>(&this->tss)>>24)&base2_mask;
    tss_entry.base3=static_cast<uint32_t>(reinterpret_cast<uint64_t>(&this->tss)>>32)&base3_mask;
    phyaddr_t rsp0top=gPhyPgsMemMgr.pages_alloc(0x4000/0x1000,phygpsmemmgr_t::page_state_t::KERNEL);
    phyaddr_t ist1top=gPhyPgsMemMgr.pages_alloc(0x4000/0x1000,phygpsmemmgr_t::page_state_t::KERNEL);
    phyaddr_t ist2top=gPhyPgsMemMgr.pages_alloc(0x4000/0x1000,phygpsmemmgr_t::page_state_t::KERNEL);
    phyaddr_t pcid_tb_phy=gPhyPgsMemMgr.pages_alloc(0x1000*sizeof(AddressSpace*)/0x1000,phygpsmemmgr_t::page_state_t::KERNEL);
    vaddr_t rsp0=(vaddr_t)gKspacePgsMemMgr.pgs_remapp(rsp0top,DEFAULT_KERNEL_STACK_SIZE,KSPACE_RW_ACCESS,0,true);
    vaddr_t ist1=(vaddr_t)gKspacePgsMemMgr.pgs_remapp(ist1top,DEFAULT_KERNEL_STACK_SIZE,KSPACE_RW_ACCESS,0,true);
    vaddr_t ist2=(vaddr_t)gKspacePgsMemMgr.pgs_remapp(ist2top,DEFAULT_KERNEL_STACK_SIZE,KSPACE_RW_ACCESS,0,true);
    pcid_table=(AddressSpace**)gKspacePgsMemMgr.pgs_remapp(pcid_tb_phy,0x1000*sizeof(AddressSpace*),KSPACE_RW_ACCESS);
    if(!(rsp0top&ist1top&ist2top&rsp0&ist1&ist2)){
        gkernelPanicManager.panic("[local_processor]init stack failed");
    }
    tss.rsp0=rsp0+DEFAULT_KERNEL_STACK_SIZE;
    tss.ist[0]=0;
    tss.ist[1]=ist1+DEFAULT_KERNEL_STACK_SIZE;
    tss.ist[2]=ist2+DEFAULT_KERNEL_STACK_SIZE;
    vaddr_t readonly_idt=(vaddr_t)gProccessorsManager.get_idt_readonly_ptr();
    GDTR gdtr={
        .base=reinterpret_cast<uint64_t>(&gdt),
        .limit=sizeof(gdt)-1
    };
    // 1. 加载新的 GDT
    asm volatile ("lgdt %0" : : "m"(gdtr) : "memory");

    // 2. 立刻用远跳转刷新 CS（最关键的一步！）
    uint16_t kcs_selector=K_cs_idx << 3;
    asm volatile (
        "pushq %0          \n\t"   // 新 CS 选择子
        "leaq 1f(%%rip), %%rax \n\t" // 新 RIP
        "pushq %%rax       \n\t"
        "retfq             \n\t"   // far return → 刷新 CS
        "1:                 \n\t"
        : : "m"(kcs_selector) : "rax", "memory"
    );

    // 3. 刷新其他段寄存器（DS/ES/SS/FS/GS）
    asm volatile (
        "mov %0, %%ds\n\t"
        "mov %0, %%es\n\t"
        "mov %0, %%ss\n\t"
        // FS/GS 特殊，通常用来放 per-cpu 数据，稍后设置
        : : "r"(uint64_t(K_ds_ss_idx << 3)) : "memory"
    );
    IDTR idtr={
        .base=readonly_idt,
        .limit=256*sizeof(IDTEntry)-1
    };
    asm volatile("lidt %0"::"m"(idtr));
    uint16_t tss_selector = gdt_headcount << 3;
    asm volatile("ltr %0"::"m"(tss_selector));
    if(is_x2apic_supported()){
        uint64_t ia32_apic_base=rdmsr(msr::apic::IA32_APIC_BASE);
        ia32_apic_base|=(1<<10);
        wrmsr(msr::apic::IA32_APIC_BASE,ia32_apic_base);
    }else{
        kputsSecure("[local_processor]x2apic not supported,unsupportted CPU");
        gkernelPanicManager.panic("[local_processor]x2apic not supported");
    }
}