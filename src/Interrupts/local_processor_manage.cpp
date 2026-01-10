#include "Interrupt_system/loacl_processor.h"
#include "util/OS_utils.h"
#include "memory/phygpsmemmgr.h"
#include "memory/AddresSpace.h"
#include "kout.h"
#include "panic.h"
#include "util/cpuid_intel.h"
#include "msr_offsets_definitions.h"
x64_local_processor *x86_smp_processors_container::local_processor_interrupt_mgr_array[x86_smp_processors_container::max_processor_count];
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
void illeagale_interrupt_post(uint16_t vector)
{
    kio::bsp_kout<<kio::now<<"[x64_local_processor]illegal interrupt on vector"<<vector<<"and apicid:"<<query_apicid()<<kio::kendl;
}
template<uint8_t Vec>
__attribute__((interrupt))
void illegal_interrupt_handler(interrupt_frame* frame){
    if(frame->cs&0x3){
        kio::bsp_kout<<kio::now<<"[x64_local_processor]illegal interrupt on vector"<<Vec<<"and apicid:"<<query_apicid()<<kio::kendl;
    }
    illeagale_interrupt_post(Vec);
}
logical_idt template_idt[256];

template<uint8_t Vec>
struct illegal_idt_filler {
    static void fill(logical_idt* idt) {
        idt[Vec].handler   = (void*)illegal_interrupt_handler<Vec>;
        idt[Vec].type      = 0xE;
        idt[Vec].ist_index = 0;
        idt[Vec].dpl       = 0;

        illegal_idt_filler<Vec - 1>::fill(idt);
    }
};
template<>
struct illegal_idt_filler<0> {
static void fill(logical_idt* idt) {
        idt[0].handler   = (void*)illegal_interrupt_handler<0>;
        idt[0].type      = 0xE;
        idt[0].ist_index = 0;
        idt[0].dpl       = 0;
    }
};

void x86_smp_processors_container::template_idt_init(){
    illegal_idt_filler<255>::fill(template_idt);
    template_idt[ivec::DIVIDE_ERROR].handler=(void*)exception_handler_div_by_zero;
    template_idt[ivec::NMI].handler=(void*)exception_handler_nmi;
    template_idt[ivec::NMI].ist_index=3;
    template_idt[ivec::BREAKPOINT].handler=(void*)exception_handler_breakpoint;
    template_idt[ivec::BREAKPOINT].ist_index=4;
    template_idt[ivec::BREAKPOINT].dpl=3;
    template_idt[ivec::OVERFLOW].handler=(void*)exception_handler_overflow;
    template_idt[ivec::OVERFLOW].dpl=3;
    template_idt[ivec::INVALID_OPCODE].handler=(void*)exception_handler_invalid_opcode;
    template_idt[ivec::DOUBLE_FAULT].handler=(void*)exception_handler_double_fault;
    template_idt[ivec::DOUBLE_FAULT].ist_index=1;
    template_idt[ivec::INVALID_TSS].handler=(void*)exception_handler_invalid_tss;
    template_idt[ivec::GENERAL_PROTECTION_FAULT].handler=(void*)exception_handler_general_protection;
    template_idt[ivec::PAGE_FAULT].handler=(void*)exception_handler_page_fault;
    template_idt[ivec::MACHINE_CHECK].handler=(void*)exception_handler_machine_check;
    template_idt[ivec::MACHINE_CHECK].ist_index=2;
    template_idt[ivec::SIMD_FLOATING_POINT_EXCEPTION].handler=(void*)exception_handler_simd_floating_point;
    template_idt[ivec::VIRTUALIZATION_EXCEPTION].handler=(void*)exception_handler_virtualization;
    template_idt[ivec::LAPIC_TIMER].handler=(void*)timer_interrupt;
    template_idt[ivec::IPI].handler=(void*)IPI;
}
int x86_smp_processors_container::regist_core()
{
    uint64_t bsp_specify=rdmsr(msr::apic::IA32_APIC_BASE);
    if(bsp_specify&(1<<8)){
        local_processor_interrupt_mgr_array[0]=new x64_local_processor(0);
        return OS_SUCCESS;
    }else{
        for(uint32_t i=0;i<max_processor_count;i++){
            if(!local_processor_interrupt_mgr_array[i]){
                local_processor_interrupt_mgr_array[i]=new x64_local_processor(i);
                return OS_SUCCESS;
            }
        }
    }
    return OS_RESOURCE_CONFILICT;
}
x64_local_processor::x64_local_processor(uint32_t alloced_id)
{
    x2apicid_t id=query_x2apicid();
    this->apic_id=id;
    this->processor_id=alloced_id;
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
    phyaddr_t rsp0top=phymemspace_mgr::pages_alloc(total_stack_size/0x1000,phymemspace_mgr::page_state_t::KERNEL);
    vaddr_t rsp0=(vaddr_t)KspaceMapMgr::pgs_remapp(rsp0top,RSP0_STACKSIZE,KSPACE_RW_ACCESS,0,true);
    vaddr_t ist1=(vaddr_t)KspaceMapMgr::pgs_remapp(rsp0top+RSP0_STACKSIZE,DF_STACKSIZE,KSPACE_RW_ACCESS,0,true);
    vaddr_t ist2=(vaddr_t)KspaceMapMgr::pgs_remapp(rsp0top+RSP0_STACKSIZE+DF_STACKSIZE,MC_STACKSIZE,KSPACE_RW_ACCESS,0,true);
    vaddr_t ist3=(vaddr_t)KspaceMapMgr::pgs_remapp(rsp0top+RSP0_STACKSIZE+DF_STACKSIZE+MC_STACKSIZE,NMI_STACKSIZE,KSPACE_RW_ACCESS,0,true);
    vaddr_t ist4=(vaddr_t)KspaceMapMgr::pgs_remapp(rsp0top+RSP0_STACKSIZE+DF_STACKSIZE+MC_STACKSIZE+NMI_STACKSIZE,BP_DBG_STACKSIZE,KSPACE_RW_ACCESS,0,true);
    if (!rsp0 || !ist1 || !ist2 || !ist3|| !ist4) {
        KernelPanicManager::panic("[x64_local_processor]init stack failed");
    }
    constexpr uint16_t RESERVED_1PG_SIZE=0x1000;
    tss.rsp0=rsp0+RSP0_STACKSIZE-RESERVED_1PG_SIZE;
    tss.ist[0]=0;
    tss.ist[1]=ist1+DF_STACKSIZE-RESERVED_1PG_SIZE;
    tss.ist[2]=ist2+MC_STACKSIZE-RESERVED_1PG_SIZE;
    tss.ist[3]=ist3+NMI_STACKSIZE-RESERVED_1PG_SIZE;
    tss.ist[4]=ist4+BP_DBG_STACKSIZE-RESERVED_1PG_SIZE;
    GDTR gdtr={
        .limit=sizeof(gdt)-1,
        .base=reinterpret_cast<uint64_t>(&gdt)        
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
        "mov %0, %%fs\n\t"
        "mov %0, %%gs\n\t"
        // FS/GS 特殊，通常用来放 per-cpu 数据，稍后设置
        : : "r"(uint64_t(K_ds_ss_idx << 3)) : "memory"
    );
    auto tran_to_phy_IDT_ENTRY=[](logical_idt entry)->IDTEntry{
        IDTEntry result={0};
        result.offset_low=static_cast<uint16_t>(reinterpret_cast<uint64_t>(entry.handler)&0xFFFF);
        result.offset_mid=static_cast<uint16_t>((reinterpret_cast<uint64_t>(entry.handler)>>16)&0xFFFF);
        result.offset_high=static_cast<uint32_t>((reinterpret_cast<uint64_t>(entry.handler)>>32)&0xFFFFFFFF);
        result.type=entry.type;
        result.ist_index=entry.ist_index;
        result.dpl=entry.dpl;
        result.present=1;
        result.segment_selector=K_cs_idx<<3;
        return result;
    };
    for(uint16_t i=0;i<256;i++){
        idt[i]=tran_to_phy_IDT_ENTRY(template_idt[i]);
    }
    IDTR idtr={
        .limit=256*sizeof(IDTEntry)-1,
        .base=(uint64_t)idt
    };
    asm volatile("lidt %0"::"m"(idtr));
    uint16_t tss_selector = gdt_headcount << 3;
    asm volatile("ltr %0"::"m"(tss_selector));
    fs_slot[L_PROCESSOR_GS_IDX]=(uint64_t)this;
    wrmsr(msr::syscall::IA32_FS_BASE,(uint64_t)&fs_slot);
    gs_slot[STACK_PROTECTOR_CANARY_IDX]=0x2345676543;//应该用rdrand搞一个随机值
    wrmsr(msr::syscall::IA32_GS_BASE,(uint64_t)&gs_slot);
    if(is_x2apic_supported()){
        uint64_t ia32_apic_base=rdmsr(msr::apic::IA32_APIC_BASE);
        ia32_apic_base|=(1<<10);
        wrmsr(msr::apic::IA32_APIC_BASE,ia32_apic_base);
    }else{
        kio::bsp_kout<<kio::now<<"[x64_local_processor]x2apic not supported"<<kio::kendl;
        KernelPanicManager::panic("[x64_local_processor]x2apic not supported");
    }
}

int x64_local_processor::unsafe_handler_register_without_vecnum_chech(uint8_t vector, void *handler)
{
    idt[vector].offset_low = static_cast<uint16_t>(reinterpret_cast<uint64_t>(handler) & 0xFFFF);
    idt[vector].offset_mid = static_cast<uint16_t>((reinterpret_cast<uint64_t>(handler) >> 16) & 0xFFFF);
    idt[vector].offset_high = static_cast<uint32_t>((reinterpret_cast<uint64_t>(handler) >> 32) & 0xFFFFFFFF);
    idt[vector].present = 1;
    idt[vector].reserved3 = 0;
    idt[vector].reserved2 = 0;
    idt[vector].reserved1 = 0;
    return 0;
}

int x64_local_processor::unsafe_handler_unregister_without_vecnum_chech(uint8_t vector)
{
    
    idt[vector].present = 0;
    return 0;
}
int x64_local_processor::template_init()
{
    return 0;
}