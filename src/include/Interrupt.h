#include <stdint.h>
#include "os_error_definitions.h"
#include "memory/kpoolmemmgr.h"
#include "memory/phygpsmemmgr.h"
#include "memory/AddresSpace.h"
#include "util/OS_utils.h"
typedef uint16_t u16;
typedef uint32_t x2apicid_t;
extern void (*global_ipi_handler)();
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
__attribute__((interrupt)) void invalid_kspace_VMentry_handler(interrupt_frame* frame, uint64_t error_code);
/**
 * 中断管理器
*管理全局中断资源(GDT,异常处理)，通过一个表管理每个核心的中断处理函数
*
 */
 
 namespace ACPI_MADT {
    typedef enum :uint8_t {
          LocalAPIC = 0,
          IOAPIC = 1,
          ISOverride = 2,
          NMI_Source = 3,
          LAPIC_NMI = 4,
          x2LocalAPIC = 0x9,
          Lx2APIC_NMI = 0xA
      }MADT_entrytype;
      struct Local_APIC_entry
      {
        MADT_entrytype type;
        uint8_t length;
        uint8_t ACPI_id;
        uint8_t APIC_id;
        uint32_t flags;
      }__attribute__((packed));
      struct io_apic_structure {
    MADT_entrytype  type;                    // Byte 0: 1 I/O APIC structure
    uint8_t  length;                  // Byte 1: 12
    uint8_t  io_apic_id;              // Byte 2: The I/O APIC's ID
    uint8_t  reserved;                // Byte 3: 0
    uint32_t io_apic_address;         // Byte 4-7: The 32-bit physical address
    uint32_t global_system_interrupt_base; // Byte 8-11: The global system interrupt base
} __attribute__((packed));
struct interrupt_source_override {
    MADT_entrytype  type;                    // Byte 0: 2 (Interrupt Source Override)
    uint8_t  length;                  // Byte 1: 10
    uint8_t  bus;                     // Byte 2: 0 (Constant, meaning ISA)
    uint8_t  source;                  // Byte 3: Bus-relative interrupt source (IRQ)
    uint32_t global_system_interrupt; // Byte 4-7: The Global System Interrupt
    uint16_t flags;                   // Byte 8-9: MPS INTI flags
} __attribute__((packed));
struct nmi_source_structure {
    MADT_entrytype  type;                    // Byte 0: 3 (NMI Source)
    uint8_t  length;                  // Byte 1: 8
    uint16_t flags;                   // Byte 2-3: Same as MPS INTI flags
    uint32_t global_system_interrupt; // Byte 4-7: The Global System Interrupt
} __attribute__((packed));
struct local_apic_nmi_structure {
    MADT_entrytype  type;                 // Byte 0: 4 (Local APIC NMI Structure)
    uint8_t  length;               // Byte 1: 6
    uint8_t  acpi_processor_uid;  // Byte 2: ACPI Processor UID
    uint16_t flags;                // Byte 3-4: MPS INTI flags
    uint8_t  local_apic_lint;      // Byte 5: Local APIC LINT#
} __attribute__((packed));
struct processor_local_x2apic_structure {
    MADT_entrytype  type;                 // Byte 0: 9 (Processor Local x2APIC structure)
    uint8_t  length;               // Byte 1: 16
    uint16_t reserved;             // Byte 2-3: Reserved - Must be zero
    uint32_t x2apic_id;            // Byte 4-7: The processor's local x2APIC ID
    uint32_t flags;                // Byte 8-11: Same as Local APIC flags
    uint32_t acpi_processor_uid;   // Byte 12-15: ACPI Processor UID
} __attribute__((packed));
struct local_x2apic_nmi_structure {
    MADT_entrytype  type;                    // Byte 0: 0x0A (Local x2APIC NMI Structure)
    uint8_t  length;                  // Byte 1: 12
    uint16_t flags;                   // Byte 2-3: Same as MPS INTI flags
    uint32_t acpi_processor_uid;      // Byte 4-7: ACPI Processor UID
    uint8_t  local_x2apic_lint;       // Byte 8: Local x2APIC LINT#
    uint8_t  reserved[3];             // Byte 9-11: Reserved - Must be zero
} __attribute__((packed));
};

namespace Interrupt_num
{
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
static constexpr uint8_t LAPIC_TIMER = 32;
static constexpr uint8_t IPI = 33;
};

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
static constexpr uint64_t base0_mask = 0xFFFF;
static constexpr uint64_t base1_mask = 0xFFULL;
static constexpr uint64_t base2_mask = 0xFFULL;
static constexpr uint64_t base3_mask = 0xFFFFFFFFULL;
struct TSSDescriptorEntry
{
    uint16_t limit;
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
static constexpr uint8_t gdt_headcount = 0x6;
struct x64GDT
{
    x64_gdtentry entries[gdt_headcount];
    TSSDescriptorEntry tss_entry;
}__attribute__((packed));
namespace LAPIC_PARAMS_ENUM{
    enum TIMER_MODE_T:uint8_t
    {
        ONE_SHOT = 0,
        PERIODIC = 1,
        TSC_DDLINE = 2
    };
    enum DELIVERY_MODE_T:uint8_t
    {
        FIXED = 0,
        LOWEST_PRIORITY = 1,
        SMI = 2,
        NMI = 4,
        INIT = 5,
        EXTINT = 7
    };
    enum PIN_POLARITY_T:uint8_t
    {
        ACTIVE_HIGH = 0,
        ACTIVE_LOW = 1
    };
    enum TRIGGER_MODE_T:uint8_t
    {
        EDGE = 0,
        LEVEL = 1
    };
    enum DELIVERY_STATUS_T:uint8_t
    {
        IDLE = 0,
        SEND_PENDING = 1
    };  
    enum MASK_T:uint8_t
    {
        NO = 0,
        YES = 1
    };
    enum DESTINATION_T:uint8_t
    {
        PHYSICAL = 0,
        LOGICAL = 1
    };
    enum DESTINATION_SHORTHAND_T:uint8_t
    {
        NO_SHORTHAND = 0,
        SELF = 1,
        ALL_INCLUDING_SELF = 2,
        ALL_EXCLUDING_SELF = 3
    };
    enum INTERUPT_LEVEL:uint8_t
    {
        DE_ASSERT = 0,
        ASSERT = 1
    };
    
}
union timer_lvt_entry
{
    struct params
    {
    uint8_t vector;
    uint8_t reserved1:4;
    uint8_t deliver_status:1;
    uint8_t reserved2:3;
    uint8_t masked:1;
    uint8_t timermode:2;
    uint8_t reserved3:5;
    uint8_t reserved4;
    uint32_t reserved5;
    };
    params param;
    uint64_t raw;
}__attribute__((packed));
constexpr timer_lvt_entry ddline_timer{
    .param{
        .vector =Interrupt_num::LAPIC_TIMER,
        .reserved1 = 0,
        .deliver_status = LAPIC_PARAMS_ENUM::DELIVERY_STATUS_T::IDLE,
        .reserved2 = 0,
        .masked = LAPIC_PARAMS_ENUM::MASK_T::NO,
        .timermode = LAPIC_PARAMS_ENUM::TIMER_MODE_T::TSC_DDLINE,        
        .reserved3 = 0,
        .reserved4 = 0,
        .reserved5 = 0
    }
};
union devide_reg_t
{
    uint64_t raw; 
    struct params
    {
        uint8_t param_low2bit:2;
        uint8_t reserved1:1;
        uint8_t param_high1bit:1;
        uint8_t reserved2:4;
        uint8_t reserved3[7];
    };
    params param;
};
typedef uint32_t initcout_reg_t;
typedef uint32_t current_reg_t;
union x2apic_destination_t {
        uint32_t raw;
        x2apicid_t id;
        struct logical_params
        {
            uint16_t cluster_bitmap;
            uint16_t cluster_idx;
        };
        logical_params l_dest;
    };
union x2apic_icr_t { 
    uint64_t raw;
    struct params
    { 
        uint8_t vector;
        uint8_t delivery_mode:3;
        uint8_t destination_mode:1;
        uint8_t reserved1:2;
        uint8_t level:1;
        uint8_t trigger_mode:1;
        uint8_t reserved2:2;
        uint8_t destination_shothand:2;
        uint16_t reserved3:12;
        x2apic_destination_t destination;
    };
    params param;
};
constexpr x2apic_icr_t broadcast_exself_icr{
    .param{
        .vector = Interrupt_num::IPI,
        .delivery_mode = LAPIC_PARAMS_ENUM::DELIVERY_MODE_T::FIXED,
        .destination_mode = LAPIC_PARAMS_ENUM::DESTINATION_T::PHYSICAL,
        .reserved1 = 0,
        .level = LAPIC_PARAMS_ENUM::INTERUPT_LEVEL::ASSERT,
        .trigger_mode=LAPIC_PARAMS_ENUM::TRIGGER_MODE_T::EDGE,
        .reserved2 = 0,
        .destination_shothand=LAPIC_PARAMS_ENUM::DESTINATION_SHORTHAND_T::ALL_EXCLUDING_SELF,
        .reserved3 = 0,
        .destination = {.raw = 0},
    }
};
class local_processor {
    private:
    static constexpr uint8_t K_cs_idx = 0x1;
    static constexpr uint8_t K_ds_ss_idx = 0x2;
    static constexpr uint8_t U_cs_idx = 0x3;
    static constexpr uint8_t U_ds_ss_idx = 0x4;
    static constexpr uint32_t  RSP0_STACKSIZE= 0x8000;
    static constexpr uint32_t  DF_STACKSIZE= 0x1000;
    static constexpr uint32_t  MC_STACKSIZE= 0x2000;
    static constexpr uint32_t  NMI_STACKSIZE= 0x2000;
    static constexpr uint32_t  total_stack_size= RSP0_STACKSIZE+DF_STACKSIZE+MC_STACKSIZE+NMI_STACKSIZE;
    x64GDT gdt;
    TSSentry tss;
    x2apicid_t apic_id;
    spinlock_cpp_t  lock;
    //x2apic下的lapic配置相关函数
    //比如计时器相关配置，优先级相关配置，核间中断相关配置
    //pcid管理相关接口，给某某cr3注册/注销pcid
    static void set_tsc_ddline(uint64_t time);

    public:
    local_processor();
    static uint64_t rdtsc();
    static  void raw_config_timer(timer_lvt_entry entry);
    static void raw_config_timer_init_count(initcout_reg_t count);
    static void raw_config_timer_divider(devide_reg_t reg);
    static current_reg_t get_timer_current_count();
    static void set_tsc_ddline_on_vec(uint8_t vector,uint64_t ddline);
    static void set_tsc_ddline_default(uint64_t ddline);
    static void cancel_tsc_ddline();
    static void raw_send_ipi(x2apic_icr_t icr);
    static void broadcast_exself_fixed_ipi(void(*ipi_handler)());
    /**
     * x2apic相关接口，操作时必须确认本核心
     * 使用头文件里写死的偏移量
     * 1.rdtsc
     * 2.raw_config_timer//通用模式
     * 2.1.raw_config_timer_lvt
     * 2.2.raw_config_timer_init_count
     * 2.3.raw_config_timer_divider
     * 2.4.get_timer_current_count()
     * 3.set_tsc//特化tsc-deadline模式
     * 3.1 private set_tsc_ddline(uint64_t time)
     * 3.2 set_tsc_vec(idtvector_t ,uint64_t ddline)
     * 3.3 cancel_tsc_ddline();
     * 4.set_priority
     * 5.ipi系列
     * 5.1 raw_send_ipi(..infopackage..)
     * 5.2 broadcast_exself_fixed_ipi(void(*)())
     * 6.自我ipi
     */

};
/**
 * 全局单例作为所有cpu的资源管理器，主要行为靠各个核心的Local_processor_Interrupt_mgr_t实现
 * 硬件层面只考虑x2apic，所以硬件层面对于处理器只考虑x2apicid编号
 * 但是系统逻辑层面使用processor_id对逻辑处理器编号
 */
typedef uint16_t prcessor_id_t;
class  ProccessorsManager_t { 
    private:
    static constexpr uint32_t max_processor_count=4096;
    static IDTEntry idt[256];
    static uint32_t total_processor_count;
    static x2apicid_t bsp_apic_id;
    static local_processor *local_processor_interrupt_mgr_array[max_processor_count];
    static spinlock_cpp_t lock;
    static prcessor_id_t*x2apicid_to_processor_id_array;
    public:
    static local_processor*get_currunt_mgr();//cpuid可以在内部查询apicid作为唯一标识，不需要额外参数
    static local_processor*get_processor_mgr_by_x2apicid(x2apicid_t apic_id);//也允许拿别人的，但是注意锁，以及慎用
    static local_processor*get_processor_mgr_by_processor_id(prcessor_id_t id);
    /**
     * 初始化函数，必须由bsp调用，大体思路有：
     * 解析madt表，注册bsp,
     */
    static int Init();
    static int regist_core();
    static int unregist_core();
    static void*get_idt_readonly_ptr();//返回idt只读指针
    static void regist_interrupt_routine(void* routine,uint8_t vector);
    static x2apicid_t tran_apic_id();
};