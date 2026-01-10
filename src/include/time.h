#pragma once
#include <stdint.h>
#include <efi.h>
#include <efilib.h>
namespace time
{
    using miusecond_time_stamp_t = uint64_t;
    class macro_time
    {
        private:
        public:
        static int Init();
        static EFI_TIME GetTime_in_os();  
        static int modify_time(EFI_TIME time);
    };
    class hardware_time_base_token{
        private:
        hardware_time_base_token(){}
        friend class hardware_time;
        miusecond_time_stamp_t hpet_base;
        uint64_t tsc_base;
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
        static hardware_time_base_token processor_regist();
        static miusecond_time_stamp_t get_stamp(hardware_time_base_token token);
    };
    extern hardware_time_base_token*bsp_token;
}