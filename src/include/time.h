#pragma once
#include <stdint.h>
#include <efi.h>
#include <efilib.h>
#include "os_error_definitions.h"
typedef  uint64_t miusecond_time_stamp_t;
struct hardware_time_base_token{
    miusecond_time_stamp_t hpet_base;
    uint64_t tsc_base;
    uint32_t tsc_fs_per_cycle;
    uint64_t lapic_fs_per_cycle;
};
struct time_complex{
    hardware_time_base_token private_token;
    class time_interrupt_generator*private_clock;

};
namespace time
{
    
    class macro_time
    {
        private:
        public:
        static int Init();
        static EFI_TIME GetTime_in_os();  
        static int modify_time(EFI_TIME time);
    };
    class hardware_time
    {
        private:
        static bool is_tsc_reliable;
        static uint32_t bsp_tsc_fs_per_cycle;
        static bool is_hpet_initialized;
        static bool is_bsp_registed;//这个位为false时默认只有bsp,当bsp注册好之后就应该设置这个位为true
        //则控制所有处理器都使用hardware_time_base_token里面的tsc_fs_per_cycle
        public:
        static int try_tsc();
        static int inform_initialized_hpet();
        static bool get_tsc_reliable();
        static bool get_if_hpet_initialized();
        static void processor_regist();
        static miusecond_time_stamp_t get_stamp();
        static void timer_polling_spin_delay(uint64_t mius);
    };
    enum backend_choose{
            lapic_normal,
            lapic_tscddline
    };
    class time_interrupt_generator
    {
        private:
        static backend_choose back_end_type;//全局采用全同时钟中断是刻意设计
        public:
        static void bsp_init();//默认只能由bsp初始化
        static void ap_init();
        static void set_clock_by_stamp(miusecond_time_stamp_t stamp);
        static void set_clock_by_offset(miusecond_time_stamp_t offset);
        static uint64_t get_current_clock(); //注意，此接口是根据后端选择而有所不同
        static void cancel_clock();
        static bool is_alarm_set();     
        static backend_choose get_backend_type();  
    };
}