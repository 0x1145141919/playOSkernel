#include <stdint.h>
#include "os_error_definitions.h"
#include "../memory/includes/kpoolmemmgr.h"
#include "../memory/includes/phygpsmemmgr.h"
#include "OS_utils.h"
typedef uint16_t u16;
/**
 * 中断管理器，管理着每个cpu的中断描述符表和本地apic
 * 当然，调用时必须上报其apic__id
 */
namespace gdtentry
{
    constexpr uint8_t execute_only_type = 0b1001;
    constexpr uint8_t read_write_type = 0b0011;
} // namespace gdtentry
struct interrupt_frame {
    uint64_t rip;    // 指令指针
    uint64_t cs;     // 代码段选择子
    uint64_t rflags; // CPU标志
    uint64_t rsp;    // 栈指针（仅特权级变化时压入）
    uint64_t ss;     // 栈段选择子（仅特权级变化时压入）
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
};
__attribute__((interrupt)) void exception_handler_div_by_zero(interrupt_frame* frame);
__attribute__((interrupt)) void exception_handler_invalid_opcode(interrupt_frame* frame);        // #UD
__attribute__((interrupt)) void exception_handler_general_protection(interrupt_frame* frame, uint64_t error_code); // #GP
__attribute__((interrupt)) void exception_handler_double_fault(interrupt_frame* frame, uint64_t error_code);       // #DF
__attribute__((interrupt)) void exception_handler_page_fault(interrupt_frame* frame, uint64_t error_code);         // #PF
__attribute__((interrupt)) void exception_handler_invalid_tss(interrupt_frame* frame, uint64_t error_code);        // #TS
__attribute__((interrupt)) void exception_handler_simd_floating_point(interrupt_frame* frame);    // #XM
__attribute__((interrupt)) void exception_handler_virtualization(interrupt_frame* frame, uint64_t error_code);     // #VE
class  Interrupt_mgr_t { 
      private:
    // 小于32的中断号定义常量
    static constexpr uint8_t DIVIDE_ERROR = 0;
    static constexpr uint8_t DEBUG = 1;
    static constexpr uint8_t NMI = 2;
    static constexpr uint8_t BREAKPOINT = 3;
    static constexpr uint8_t OVERFLOW = 4;
    static constexpr uint8_t BOUND_RANGE_EXCEEDED = 5;
    static constexpr uint8_t INVALID_OPCODE = 6;
    static constexpr uint8_t DEVICE_NOT_AVAILABLE = 7;
    static constexpr uint8_t DOUBLE_FAULT = 8;
    static constexpr uint8_t COPROCESSOR_SEGMENT_OVERRUN = 9;
    static constexpr uint8_t INVALID_TSS = 10;
    static constexpr uint8_t SEGMENT_NOT_PRESENT = 11;
    static constexpr uint8_t STACK_SEGMENT_FAULT = 12;
    static constexpr uint8_t GENERAL_PROTECTION_FAULT = 13;
    static constexpr uint8_t PAGE_FAULT = 14;
    static constexpr uint8_t X87_FPU_ERROR = 16;
    static constexpr uint8_t ALIGNMENT_CHECK = 17;
    static constexpr uint8_t MACHINE_CHECK = 18;
    static constexpr uint8_t SIMD_FLOATING_POINT_EXCEPTION = 19;
    static constexpr uint8_t VIRTUALIZATION_EXCEPTION = 20;
    static constexpr uint8_t CONTROL_PROTECTION_EXCEPTION = 21;
    static constexpr uint8_t HYPERVISOR_INJECTION_EXCEPTION = 28;
    static constexpr uint8_t VMM_COMMUNICATION_EXCEPTION = 29;
    static constexpr uint8_t SECURITY_EXCEPTION = 30; 
    static constexpr uint8_t normal_exception_ist_index= 1;
    static constexpr uint8_t double_fault_exception_ist_index= 2;
    struct x64_gdtentry {
	u16	limit0;
	u16	base0;
	u16	base1: 8, type: 4, s: 1, dpl: 2, p: 1;
	u16	limit1: 4, avl: 1, l: 1, d: 1, g: 1, base2: 8;
} __attribute__((packed));
static constexpr x64_gdtentry kspace_DS_SS_entry = {
    .limit0 = 0xFFFF,
    .base0 = 0x0000,
    .base1 = 0x00,
    .type = gdtentry::read_write_type,
    .s = 1,
    .dpl = 0,
    .p = 1,
    .limit1 = 0b1111,
    .avl = 0,
    .l = 0,
    .d = 1,
    .g = 1,
    .base2 = 0x00
};
static constexpr uint8_t kspace_DS_SS_gdt_selector = 0x10;
static constexpr x64_gdtentry kspace_CS_entry = {
    .limit0 = 0xFFFF,
    .base0 = 0x0000,
    .base1 = 0x00,
    .type = gdtentry::execute_only_type,
    .s = 1,
    .dpl = 0,
    .p = 1,
    .limit1 = 0b1111,
    .avl = 0,
     .l = 1,
    .d = 0,
    .g = 1,
    .base2 = 0x00
};
static constexpr uint8_t kspace_CS_gdt_selector = 0x08;
static constexpr x64_gdtentry userspace_DS_SS_entry = {
      .limit0 = 0xFFFF,
    .base0 = 0x0000,
    .base1 = 0x00,
    .type = gdtentry::read_write_type,
    .s = 1,
    .dpl = 3,
    .p = 1,
    .limit1 = 0b1111,
    .avl = 0,
    .l = 0,
    .d = 1,
    .g = 1,
    .base2 = 0x00
};
static constexpr uint8_t userspace_CS_gdt_selector = 0x18;
static constexpr x64_gdtentry userspace_CS_entry = {
    .limit0 = 0xFFFF,
    .base0 = 0x0000,
    .base1 = 0x00,
    .type = gdtentry::execute_only_type,
    .s = 1,
    .dpl = 3,
    .p = 1,
    .limit1 = 0b1111,
    .avl = 0,
     .l = 1,
    .d = 0,
    .g = 1,
    .base2 = 0x00
};
static constexpr uint8_t userspace_DS_SS_gdt_selector = 0x20;
struct IDTEntry {
    uint16_t offset_low;      // 中断处理程序地址的低16位 (位 15-0)
    uint16_t segment_selector;// 代码段选择子 (位 15-0)
    // 属性字段 (位 31-16)
    union {
        struct {
            uint8_t ist_index : 3;   // 中断栈表索引 (位 2-0)
            uint8_t reserved1 : 5;   // 保留位 (位 7-3)
            uint8_t type : 4;        // 门类型 (位 11-8)
            uint8_t reserved2 : 1;   // 保留位 (位 12)
            uint8_t dpl : 2;         // 描述符特权级 (位 14-13)
            uint8_t present : 1;     // 存在标志 (位 15)
        } __attribute__((packed));
        uint16_t attributes;
    };
    uint16_t offset_mid;      // 中断处理程序地址的中16位 (位 31-16)
    uint32_t offset_high;     // 中断处理程序地址的高32位 (位 63-32)
    uint32_t reserved3;       // 保留位 (位 95-64/31-0)
}__attribute__((packed));
struct TSSentry
{
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t ist[8];//根据文档ist[0]不使用,必须分配为NULL
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t io_map_base_offset;
    //简化设计，不配置io_map，用户态不准用io_map只允许内核态使用
} __attribute__((packed));
struct TSSDescriptorEntry
{
    uint16_t limit=sizeof(TSSentry)+1;
    uint16_t base0;
    uint8_t base1;
    uint8_t type : 4, zero : 1, dpl : 2, p : 1;
    uint8_t limit1 : 4, avl : 1, reserved : 2, g : 1;
    uint8_t base2;
    uint32_t base3;
    uint32_t reserved2;
}__attribute__((packed));

struct GDTR
{
    uint16_t limit;
    uint64_t base;
}__attribute__((packed));
struct IDTR
{
    uint16_t limit;
    uint64_t base;
}__attribute__((packed));
static constexpr uint32_t max_processor_count = 2048;
static constexpr uint8_t gdt_headcount = 0x20;
struct x64GDT
{
    x64_gdtentry entries[gdt_headcount];
    TSSDescriptorEntry tss_entry[max_processor_count];
}__attribute__((packed));
class Local_processor_Interrupt_mgr_t {
    private:
    IDTEntry IDTEntry[256];
    TSSentry tss;
    uint32_t apic_id;
    public:
    int register_handler(uint8_t interrupt_number,void* handler);
    int unregister_handler(uint8_t interrupt_number);
    Local_processor_Interrupt_mgr_t(uint32_t apic_id);
    };
uint32_t total_processor_count=0;

Local_processor_Interrupt_mgr_t *local_processor_interrupt_mgr_array[max_processor_count]={0};
IDTEntry global_idt[32];//低32固定的模板必须复制到其它核心
x64GDT global_gdt;
GDTR global_gdt_ptr;
protected:

    static constexpr uint64_t base0_mask = 0xFFFF;
    static constexpr uint64_t base1_mask = 0xFFULL;
    static constexpr uint64_t base2_mask = 0xFFULL;
    static constexpr uint64_t base3_mask = 0xFFFFFFFFULL;

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
public:
Interrupt_mgr_t();
void Init();
int processor_Interrupt_init(uint32_t apic_id);
int processor_Interrupt_register(uint32_t apic_id,uint8_t interrupt_number,void* handler);
int processor_Interrupt_unregister(uint32_t apic_id,uint8_t interrupt_number);
};
extern Interrupt_mgr_t gInterrupt_mgr;