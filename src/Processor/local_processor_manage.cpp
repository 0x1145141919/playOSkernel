#include "Interrupt_system/loacl_processor.h"
#include "Interrupt_system/early_bsp_resouces.h"
#include "firmware/gSTResloveAPIs.h"
#include "firmware/ACPI_APIC.h"
#include "util/kout.h"
#include "linker_symbols.h"
#include "Interrupt_system/fixed_interrupt_vectors.h"
#include "util/cpuid_intel.h"
#include "util/OS_utils.h"
#include "memory/AddresSpace.h"
#include "memory/phygpsmemmgr.h"
#include "memory/phyaddr_accessor.h"
#include "core_hardwares/lapic.h"
#include "util/bitmap.h"
#include "Interrupt_system/AP_Init_error_observing_protocol.h"
#include "msr_offsets_definitions.h"
#include "panic.h"
#include <util/kptrace.h>
#include "time.h"

extern "C" check_point realmode_enter_checkpoint;
extern "C" check_point pemode_enter_checkpoint;
extern "C" char AP_realmode_start;
extern "C" uint32_t assigned_processor_id;
check_point longmode_enter_checkpoint;
check_point init_finish_checkpoint;
extern "C" const x64_gdtentry bsp_init_gdt_entries[] = {
    {0},                                    // Null descriptor (index 0)
    kspace_CS_entry,                        // 内核代码段 (index 1, selector = 0x08)
    kspace_DS_SS_entry,                     // 内核数据段 (index 2, selector = 0x10)
    userspace_CS_entry,                     // 用户代码段 (index 3, selector = 0x18)
    userspace_DS_SS_entry,                  // 用户数据段 (index 4, selector = 0x20)
    {0}                                     // 预留条目，供TSS使用 (index 5)
};
extern "C" const GDTR bsp_init_gdt_descriptor = {
    .limit = sizeof(bsp_init_gdt_entries) - 1,
    .base = reinterpret_cast<uint64_t>(bsp_init_gdt_entries)
};
// 定义前21个IDT项的数组
extern "C" const IDTEntry bsp_init_idt_entries[21] = {
    // ivec::DIVIDE_ERROR (0) - 除零异常
    {
        .offset_low = static_cast<uint16_t>(reinterpret_cast<uint64_t>(&div_by_zero_bare_enter) & 0xFFFF),
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xE << 8) |   // type: 中断门 (0b1110 = 14 = 0xE)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (1 << 15),     // present: 1
        .offset_mid = static_cast<uint16_t>((reinterpret_cast<uint64_t>(&div_by_zero_bare_enter) >> 16) & 0xFFFF),
        .offset_high = static_cast<uint32_t>((reinterpret_cast<uint64_t>(&div_by_zero_bare_enter) >> 32) & 0xFFFFFFFF),
        .reserved3 = 0
    },
    // ivec::DEBUG (1) - 调试异常
    {
        .offset_low = 0,   // 没有特定处理函数，置零
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xE << 8) |   // type: 中断门 (0b1110 = 14 = 0xE)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (0 << 15),     // present: 0 (不存在，置为0)
        .offset_mid = 0,  // 没有特定处理函数，置零
        .offset_high = 0, // 没有特定处理函数，置零
        .reserved3 = 0
    },
    // ivec::NMI (2) - NMI异常
    {
        .offset_low = static_cast<uint16_t>(reinterpret_cast<uint64_t>(&nmi_bare_enter) & 0xFFFF),
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xE << 8) |   // type: 中断门 (0b1110 = 14 = 0xE)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (1 << 15),     // present: 1
        .offset_mid = static_cast<uint16_t>((reinterpret_cast<uint64_t>(&nmi_bare_enter) >> 16) & 0xFFFF),
        .offset_high = static_cast<uint32_t>((reinterpret_cast<uint64_t>(&nmi_bare_enter) >> 32) & 0xFFFFFFFF),
        .reserved3 = 0
    },
    // ivec::BREAKPOINT (3) - 断点异常
    {
        .offset_low = static_cast<uint16_t>(reinterpret_cast<uint64_t>(&breakpoint_bare_enter) & 0xFFFF),
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xE << 8) |   // type: 中断门 (0b1110 = 14 = 0xE)
                      (0 << 12) |    // reserved2: 0
                      (3 << 13) |    // dpl: 3 (特权级3)
                      (1 << 15),     // present: 1
        .offset_mid = static_cast<uint16_t>((reinterpret_cast<uint64_t>(&breakpoint_bare_enter) >> 16) & 0xFFFF),
        .offset_high = static_cast<uint32_t>((reinterpret_cast<uint64_t>(&breakpoint_bare_enter) >> 32) & 0xFFFFFFFF),
        .reserved3 = 0
    },
    // ivec::OVERFLOW (4) - 溢出异常
    {
        .offset_low = static_cast<uint16_t>(reinterpret_cast<uint64_t>(&overflow_bare_enter) & 0xFFFF),
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xE << 8) |   // type: 中断门 (0b1110 = 14 = 0xE)
                      (0 << 12) |    // reserved2: 0
                      (3 << 13) |    // dpl: 3 (特权级3)
                      (1 << 15),     // present: 1
        .offset_mid = static_cast<uint16_t>((reinterpret_cast<uint64_t>(&overflow_bare_enter) >> 16) & 0xFFFF),
        .offset_high = static_cast<uint32_t>((reinterpret_cast<uint64_t>(&overflow_bare_enter) >> 32) & 0xFFFFFFFF),
        .reserved3 = 0
    },
    // ivec::BOUND_RANGE_EXCEEDED (5) - 越界异常
    {
        .offset_low = 0,   // 没有特定处理函数，置零
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xE << 8) |   // type: 中断门 (0b1110 = 14 = 0xE)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (0 << 15),     // present: 0 (不存在，置为0)
        .offset_mid = 0,  // 没有特定处理函数，置零
        .offset_high = 0, // 没有特定处理函数，置零
        .reserved3 = 0
    },
    // ivec::INVALID_OPCODE (6) - 无效操作码异常
    {
        .offset_low = static_cast<uint16_t>(reinterpret_cast<uint64_t>(&invalid_opcode_bare_enter) & 0xFFFF),
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xE << 8) |   // type: 中断门 (0b1110 = 14 = 0xE)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (1 << 15),     // present: 1
        .offset_mid = static_cast<uint16_t>((reinterpret_cast<uint64_t>(&invalid_opcode_bare_enter) >> 16) & 0xFFFF),
        .offset_high = static_cast<uint32_t>((reinterpret_cast<uint64_t>(&invalid_opcode_bare_enter) >> 32) & 0xFFFFFFFF),
        .reserved3 = 0
    },
    // ivec::DEVICE_NOT_AVAILABLE (7) - 设备不可用异常
    {
        .offset_low = 0,   // 没有特定处理函数，置零
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xE << 8) |   // type: 中断门 (0b1110 = 14 = 0xE)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (0 << 15),     // present: 0 (不存在，置为0)
        .offset_mid = 0,  // 没有特定处理函数，置零
        .offset_high = 0, // 没有特定处理函数，置零
        .reserved3 = 0
    },
    // ivec::DOUBLE_FAULT (8) - 双重故障异常
    {
        .offset_low = static_cast<uint16_t>(reinterpret_cast<uint64_t>(&double_fault_bare_enter) & 0xFFFF),
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xF << 8) |   // type: 陷阱门 (0b1111 = 15 = 0xF，有错误码)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (1 << 15),     // present: 1
        .offset_mid = static_cast<uint16_t>((reinterpret_cast<uint64_t>(&double_fault_bare_enter) >> 16) & 0xFFFF),
        .offset_high = static_cast<uint32_t>((reinterpret_cast<uint64_t>(&double_fault_bare_enter) >> 32) & 0xFFFFFFFF),
        .reserved3 = 0
    },
    // 向量9 - 未使用
    {
        .offset_low = 0,
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xE << 8) |   // type: 中断门 (0b1110 = 14 = 0xE)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (0 << 15),     // present: 0 (不存在，置为0)
        .offset_mid = 0,
        .offset_high = 0,
        .reserved3 = 0
    },
    // ivec::INVALID_TSS (10) - 无效TSS异常
    {
        .offset_low = static_cast<uint16_t>(reinterpret_cast<uint64_t>(&invalid_tss_bare_enter) & 0xFFFF),
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xF << 8) |   // type: 陷阱门 (0b1111 = 15 = 0xF，有错误码)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (1 << 15),     // present: 1
        .offset_mid = static_cast<uint16_t>((reinterpret_cast<uint64_t>(&invalid_tss_bare_enter) >> 16) & 0xFFFF),
        .offset_high = static_cast<uint32_t>((reinterpret_cast<uint64_t>(&invalid_tss_bare_enter) >> 32) & 0xFFFFFFFF),
        .reserved3 = 0
    },
    // ivec::SEGMENT_NOT_PRESENT (11) - 段不存在异常
    {
        .offset_low = 0,   // 没有特定处理函数，置零
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xF << 8) |   // type: 陷阱门 (0b1111 = 15 = 0xF，有错误码)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (0 << 15),     // present: 0 (不存在，置为0)
        .offset_mid = 0,  // 没有特定处理函数，置零
        .offset_high = 0, // 没有特定处理函数，置零
        .reserved3 = 0
    },
    // ivec::STACK_SEGMENT_FAULT (12) - 栈段错误异常
    {
        .offset_low = 0,   // 没有特定处理函数，置零
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xF << 8) |   // type: 陷阱门 (0b1111 = 15 = 0xF，有错误码)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (0 << 15),     // present: 0 (不存在，置为0)
        .offset_mid = 0,  // 没有特定处理函数，置零
        .offset_high = 0, // 没有特定处理函数，置零
        .reserved3 = 0
    },
    // ivec::GENERAL_PROTECTION_FAULT (13) - 一般保护错误异常
    {
        .offset_low = static_cast<uint16_t>(reinterpret_cast<uint64_t>(&general_protection_bare_enter) & 0xFFFF),
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xF << 8) |   // type: 陷阱门 (0b1111 = 15 = 0xF，有错误码)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (1 << 15),     // present: 1
        .offset_mid = static_cast<uint16_t>((reinterpret_cast<uint64_t>(&general_protection_bare_enter) >> 16) & 0xFFFF),
        .offset_high = static_cast<uint32_t>((reinterpret_cast<uint64_t>(&general_protection_bare_enter) >> 32) & 0xFFFFFFFF),
        .reserved3 = 0
    },
    // ivec::PAGE_FAULT (14) - 页面错误异常
    {
        .offset_low = static_cast<uint16_t>(reinterpret_cast<uint64_t>(&page_fault_bare_enter) & 0xFFFF),
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xF << 8) |   // type: 陷阱门 (0b1111 = 15 = 0xF，有错误码)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (1 << 15),     // present: 1
        .offset_mid = static_cast<uint16_t>((reinterpret_cast<uint64_t>(&page_fault_bare_enter) >> 16) & 0xFFFF),
        .offset_high = static_cast<uint32_t>((reinterpret_cast<uint64_t>(&page_fault_bare_enter) >> 32) & 0xFFFFFFFF),
        .reserved3 = 0
    },
    // 向量15 - 未使用
    {
        .offset_low = 0,
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xE << 8) |   // type: 中断门 (0b1110 = 14 = 0xE)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (0 << 15),     // present: 0 (不存在，置为0)
        .offset_mid = 0,
        .offset_high = 0,
        .reserved3 = 0
    },
    // ivec::x87_FPU_FLOATING_POINT_ERROR (16) - x87 FPU 浮点错误
    {
        .offset_low = 0,   // 没有特定处理函数，置零
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xE << 8) |   // type: 中断门 (0b1110 = 14 = 0xE)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (0 << 15),     // present: 0 (不存在，置为0)
        .offset_mid = 0,  // 没有特定处理函数，置零
        .offset_high = 0, // 没有特定处理函数，置零
        .reserved3 = 0
    },
    // ivec::ALIGNMENT_CHECK (17) - 对齐检查异常
    {
        .offset_low = 0,   // 没有特定处理函数，置零
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xF << 8) |   // type: 陷阱门 (0b1111 = 15 = 0xF，有错误码)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (0 << 15),     // present: 0 (不存在，置为0)
        .offset_mid = 0,  // 没有特定处理函数，置零
        .offset_high = 0, // 没有特定处理函数，置零
        .reserved3 = 0
    },
    // ivec::MACHINE_CHECK (18) - 机器检查异常
    {
        .offset_low = static_cast<uint16_t>(reinterpret_cast<uint64_t>(&machine_check_bare_enter) & 0xFFFF),
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xE << 8) |   // type: 中断门 (0b1110 = 14 = 0xE)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (1 << 15),     // present: 1
        .offset_mid = static_cast<uint16_t>((reinterpret_cast<uint64_t>(&machine_check_bare_enter) >> 16) & 0xFFFF),
        .offset_high = static_cast<uint32_t>((reinterpret_cast<uint64_t>(&machine_check_bare_enter) >> 32) & 0xFFFFFFFF),
        .reserved3 = 0
    },
    // ivec::SIMD_FLOATING_POINT_EXCEPTION (19) - SIMD浮点异常
    {
        .offset_low = static_cast<uint16_t>(reinterpret_cast<uint64_t>(&simd_floating_point_bare_enter) & 0xFFFF),
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xE << 8) |   // type: 中断门 (0b1110 = 14 = 0xE)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (1 << 15),     // present: 1
        .offset_mid = static_cast<uint16_t>((reinterpret_cast<uint64_t>(&simd_floating_point_bare_enter) >> 16) & 0xFFFF),
        .offset_high = static_cast<uint32_t>((reinterpret_cast<uint64_t>(&simd_floating_point_bare_enter) >> 32) & 0xFFFFFFFF),
        .reserved3 = 0
    },
    // ivec::VIRTUALIZATION_EXCEPTION (20) - 虚拟化异常
    {
        .offset_low = static_cast<uint16_t>(reinterpret_cast<uint64_t>(&virtualization_bare_enter) & 0xFFFF),
        .segment_selector = x64_local_processor::K_cs_idx << 3,
        .attributes = (0 << 0) |     // ist_index: 0
                      (0 << 3) |     // reserved1: 0
                      (0xE << 8) |   // type: 中断门 (0b1110 = 14 = 0xE)
                      (0 << 12) |    // reserved2: 0
                      (0 << 13) |    // dpl: 0 (特权级0)
                      (1 << 15),     // present: 1
        .offset_mid = static_cast<uint16_t>((reinterpret_cast<uint64_t>(&virtualization_bare_enter) >> 16) & 0xFFFF),
        .offset_high = static_cast<uint32_t>((reinterpret_cast<uint64_t>(&virtualization_bare_enter) >> 32) & 0xFFFFFFFF),
        .reserved3 = 0
    }
};
extern "C" IDTR const bsp_init_idtr={
    .limit = sizeof(bsp_init_idt_entries) - 1,
    .base = (uint64_t)bsp_init_idt_entries
};
uint64_t ap_hlt_word;
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
    template_idt[ivec::DIVIDE_ERROR].handler=(void*)&div_by_zero_bare_enter;
    template_idt[ivec::NMI].handler=(void*)&nmi_bare_enter;
    template_idt[ivec::NMI].ist_index=3;
    template_idt[ivec::BREAKPOINT].handler=(void*)&breakpoint_bare_enter;
    template_idt[ivec::BREAKPOINT].ist_index=4;
    template_idt[ivec::BREAKPOINT].dpl=3;
    template_idt[ivec::OVERFLOW].handler=(void*)&overflow_bare_enter;
    template_idt[ivec::OVERFLOW].dpl=3;
    template_idt[ivec::INVALID_OPCODE].handler=(void*)&invalid_opcode_bare_enter;
    template_idt[ivec::DOUBLE_FAULT].handler=(void*)&double_fault_bare_enter;
    template_idt[ivec::DOUBLE_FAULT].ist_index=1;
    template_idt[ivec::INVALID_TSS].handler=(void*)&invalid_tss_bare_enter;
    template_idt[ivec::GENERAL_PROTECTION_FAULT].handler=(void*)&general_protection_bare_enter;
    template_idt[ivec::PAGE_FAULT].handler=(void*)&page_fault_bare_enter;
    template_idt[ivec::MACHINE_CHECK].handler=(void*)&machine_check_bare_enter;
    template_idt[ivec::MACHINE_CHECK].ist_index=2;
    template_idt[ivec::SIMD_FLOATING_POINT_EXCEPTION].handler=(void*)&simd_floating_point_bare_enter;
    template_idt[ivec::VIRTUALIZATION_EXCEPTION].handler=(void*)&virtualization_bare_enter;
    template_idt[ivec::LAPIC_TIMER].handler=(void*)&timer_bare_enter;
    template_idt[ivec::IPI].handler=(void*)&ipi_bare_enter;
}
int x86_smp_processors_container::regist_core(uint32_t processor_id) 
{
    uint64_t bsp_specify=rdmsr(msr::apic::IA32_APIC_BASE);
    if(bsp_specify&(1<<8)){
        local_processor_interrupt_mgr_array[0]=new x64_local_processor(0);
        return OS_SUCCESS;
    }else{
        local_processor_interrupt_mgr_array[processor_id]=new x64_local_processor(processor_id);
        return OS_SUCCESS;
    }
    return OS_RESOURCE_CONFILICT;

}
KURD_t x86_smp_processors_container::AP_Init_one_by_one()
{//此函数会需要保证低0～64k内存的恒等映射
    // 使用gAnalyzer的processor_x64_list链表进行遍历
    KURD_t fail=default_fail();
    fail.event_code=INTERRUPT_SUB_MODULES_LOCATIONS::PROCESSORS_EVENT_CODE::EVENT_CODE_APS_INIT;
    KURD_t success=default_success();
    success.event_code=INTERRUPT_SUB_MODULES_LOCATIONS::PROCESSORS_EVENT_CODE::EVENT_CODE_APS_INIT;
    KURD_t fatal=default_fatal();
    fatal.event_code=INTERRUPT_SUB_MODULES_LOCATIONS::PROCESSORS_EVENT_CODE::EVENT_CODE_APS_INIT;
    if((uint64_t)&AP_realmode_start%4096){
        kio::bsp_kout<<"[x64_local_processor]AP_realmode_start is not aligned to 4K"<<kio::kendl;
        //ap入口必须4K对齐，否则触发bugpanic
    }
    if(gAnalyzer == nullptr) {
        kio::bsp_kout << "gAnalyzer is null, cannot initialize processors" << kio::kendl;
        fail.result=result_code::RETRY;
        fail.reason=INTERRUPT_SUB_MODULES_LOCATIONS::PROCESSORS_EVENT_CODE::APS_INIT_RESULTS_CODE::RETRY_REASON_CODE::RETRY_REASON_CODE_DEPENDIES_NOT_INITIALIZED;
        return fail;
    }
    x2apicid_t self_x2apicid = query_x2apicid();
    x2apic::x2apic_icr_t icr_sipi={
        .param={
            .vector=((uint64_t)&AP_realmode_start/4096),
            .delivery_mode=6,
            .destination_mode=LAPIC_PARAMS_ENUM::DESTINATION_T::PHYSICAL,
            .reserved1=0,
            .level=1,
            .trigger_mode=0,
            .reserved2=0,
            .destination_shorthand=LAPIC_PARAMS_ENUM::DESTINATION_SHORTHAND_T::NO_SHORTHAND,
            .reserved3=0,
            .destination=0
        }
    };
    
    
    {
        x2apic::x2apic_icr_t icr_init={
        .param={
            .vector=0,
            .delivery_mode=5,
            .destination_mode=LAPIC_PARAMS_ENUM::DESTINATION_T::PHYSICAL,
            .reserved1=0,
            .level=1,
            .trigger_mode=0,
            .reserved2=0,
            .destination_shorthand=LAPIC_PARAMS_ENUM::DESTINATION_SHORTHAND_T::ALL_EXCLUDING_SELF,
            .reserved3=0,
            .destination=0
        }
        };
    kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one try init all exclued self prcessor"<<kio::kendl;
    x2apic::x2apic_driver::raw_send_ipi(icr_init);
    kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one init all exclued self prcessor sucess"<<kio::kendl;
    time::hardware_time::timer_polling_spin_delay(20000);
    }
    {
        x2apic::x2apic_icr_t icr_init_de_assert={
        .param={
            .vector=0,
            .delivery_mode=5,
            .destination_mode=LAPIC_PARAMS_ENUM::DESTINATION_T::PHYSICAL,
            .reserved1=0,
            .level=0,
            .trigger_mode=1,
            .reserved2=0,
            .destination_shorthand=LAPIC_PARAMS_ENUM::DESTINATION_SHORTHAND_T::ALL_INCLUDING_SELF,
            .reserved3=0,
            .destination=0
        }
    };
    kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one try init-de-assert all exclued self prcessor"<<kio::kendl;
    x2apic::x2apic_driver::raw_send_ipi(icr_init_de_assert);
    kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one init-de-assert all exclued self prcessor sucess"<<kio::kendl;
    time::hardware_time::timer_polling_spin_delay(1000);
    }
    
    uint32_t processor_id = 1;
    if(time::hardware_time::get_if_hpet_initialized()==false){
        fail.result=result_code::RETRY;
        fail.reason=INTERRUPT_SUB_MODULES_LOCATIONS::PROCESSORS_EVENT_CODE::APS_INIT_RESULTS_CODE::RETRY_REASON_CODE::RETRY_REASON_CODE_DEPENDIES_NOT_INITIALIZED;
        return fail;
    }
    int (*observe_realmode)()=[&]()->int {

    };
    int (*observe_pemode_enter)()=[&]()->int {
        
    };
    void (*pe_fail_dealing)()=[](){
        
    };
    int (*observe_longmode_enter)()=[&]()->int {
        
    };
    void (*longmode_enter_fail_dealing)()=[](){
        
    };
    int (*observe_finish)()=[&]()->int {
        
    };
    void (*finish_fail_dealing)()=[](){
        
    };
    auto ap_init_stage_func=[](
        uint64_t delay_microseconds,
        int(*observe_ap)(),//返回值0代表等待，返回值1代表成功，-1代表失败
        void(*failer_dealing)())->int{//启用全局错误码的情况下，0成功，1失败，2超时
        uint64_t now_microseconds = time::hardware_time::get_stamp(time::hardware_time_base_token::bsp_token);//GS槽位里面会专门放每个核心的时间token的
        uint64_t ddline_stamp = now_microseconds + delay_microseconds;
        while(now_microseconds < ddline_stamp){
            int observe_ap_result= observe_ap();
            if(observe_ap_result){
                if(observe_ap_result==1)return 0;
                else {
                    failer_dealing();
                    return 1;
                }
            }
            now_microseconds = time::hardware_time::get_stamp(time::hardware_time_base_token::bsp_token);
        }
        return 2;
    };
    // 遍历gAnalyzer的processor_x64_list链表
    for(auto it = gAnalyzer->processor_x64_list->begin(); it != gAnalyzer->processor_x64_list->end(); ++it) {
        APICtb_analyzed_structures::processor_x64_lapic_struct& proc = *it;
        // 跳过当前处理器（BSP）
        if(proc.apicid == self_x2apicid) continue;
        
        // 检查处理器是否启用（根据xAPIC或x2APIC类型）
        // 这里我们跳过检查is_bsp字段，因为我们要初始化所有AP
        
        icr_sipi.param.destination.raw = proc.apicid;
        assigned_processor_id=processor_id;
        asm volatile("sfence");
        kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one send sipi for"<<proc.apicid<<"processor"<<kio::kendl;
        x2apic::x2apic_driver::raw_send_ipi(icr_sipi);
        int status= ap_init_stage_func(1000,observe_realmode,nullptr);//只有成功/超时两种状态
        if(status==2){
            kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one send sipi for"<<proc.apicid<<"processor timeout"<<kio::kendl;
        }
        int status= ap_init_stage_func(1000,observe_pemode_enter,pe_fail_dealing);
        if(status==1){

        }
        if(status==2){
            kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one send sipi for"<<proc.apicid<<"processor timeout"<<kio::kendl;
        }
        int status= ap_init_stage_func(1000,observe_longmode_enter,longmode_enter_fail_dealing);
        if(status==1){

        }
        if(status==2){
            kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one send sipi for"<<proc.apicid<<"processor timeout"<<kio::kendl;
        }
        int status= ap_init_stage_func(1000,observe_finish,finish_fail_dealing);    
        if(status==1){

        }
        if(status==2){
            kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one send sipi for"<<proc.apicid<<"processor timeout"<<kio::kendl;
        }
    }

    return success;
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
        ia32_apic_base|=(1<<11);
        ia32_apic_base|=(1<<10);
        wrmsr(msr::apic::IA32_APIC_BASE,ia32_apic_base);
        ia32_apic_base=rdmsr(msr::apic::IA32_APIC_BASE);
        //kio::bsp_kout<<(void*)ia32_apic_base<<kio::kendl;
        if(!(ia32_apic_base&(1<<10))){
            KernelPanicManager::panic("[x64_local_processor]x2apic not supported");
        }
        uint64_t icr=rdmsr(msr::apic::IA32_X2APIC_ICR);
    }else{
        kio::bsp_kout<<kio::now<<"[x64_local_processor]x2apic not supported"<<kio::kendl;
        KernelPanicManager::panic("[x64_local_processor]x2apic not supported");
    }
}
KURD_t x64_local_processor::default_kurd()
{
    return KURD_t(0,0,module_code::INTERRUPT,INTERRUPT_SUB_MODULES_LOCATIONS::LOCATION_CODE_PROCESSORS,0,0,err_domain::ARCH);
}
KURD_t x64_local_processor::default_success()
{
    KURD_t result=default_kurd();
    result.level=level_code::INFO;
    result.result=result_code::SUCCESS;
    return result;
}
KURD_t x64_local_processor::default_fail()
{
    KURD_t result=default_kurd();
    result=set_result_fail_and_error_level(result);
    return result;
}
KURD_t x64_local_processor::default_fatal()
{
    KURD_t result=default_kurd();
    result=set_fatal_result_level(result);
    return result;
}
KURD_t x86_smp_processors_container::default_kurd()
{
    return KURD_t(0,0,module_code::INTERRUPT,INTERRUPT_SUB_MODULES_LOCATIONS::LOCATION_CODE_PROCESSORS,0,0,err_domain::ARCH);
}
KURD_t x86_smp_processors_container::default_success()
{
    KURD_t result=default_kurd();
    result.level=level_code::INFO;
    result.result=result_code::SUCCESS;
    return result;
}
KURD_t x86_smp_processors_container::default_fail()
{
    KURD_t result=default_kurd();
    result=set_result_fail_and_error_level(result);
    return result;
}
KURD_t x86_smp_processors_container::default_fatal()
{
    KURD_t result=default_kurd();
    result=set_fatal_result_level(result);
    return result;
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
    return x64_local_processor::unsafe_handler_register_without_vecnum_chech(vector,template_idt[vector].handler);
}