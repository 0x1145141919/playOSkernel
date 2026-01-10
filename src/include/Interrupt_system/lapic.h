#pragma once
#include <stdint.h>
#include "memory/Memory.h"
#include "fixed_interrupt_vectors.h"
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
class apic_driver{
    private:
    static phyaddr_t lapic_phyaddr;
    static vaddr_t lapic_virtaddr;
    public:
    
};
namespace x2apic
{
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
        .vector =ivec::LAPIC_TIMER,
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
        uint32_t id;
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
        .vector = ivec::IPI,
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
    class x2apic_driver{
    private:
    public:
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
}