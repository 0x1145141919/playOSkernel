#pragma once
#include <stdint.h>
#include <efi.h>
#include <efilib.h>
typedef  uint64_t miusecond_time_stamp_t;
struct hardware_time_base_token{
    miusecond_time_stamp_t hpet_base;
    uint64_t tsc_base;
};
struct time_complex{
    hardware_time_base_token private_token;
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
        static uint32_t tsc_fs_per_cycle;
        static bool is_hpet_initialized;
        public:
        static int try_tsc();
        static int inform_initialized_hpet();
        static bool get_tsc_reliable();
        static bool get_if_hpet_initialized();
        static void processor_regist();
        static miusecond_time_stamp_t get_stamp();
        static void timer_polling_spin_delay(uint64_t mius);
    };
}