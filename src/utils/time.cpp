#include "time.h"
#include "util/OS_utils.h"
#include "util/cpuid_intel.h"
#include "HPET.h"
time::hardware_time_base_token* time::bsp_token;
bool time::hardware_time::is_tsc_reliable = false;
uint32_t time::hardware_time::tsc_fs_per_cycle = 0;
bool time::hardware_time::is_hpet_initialized = false;
int time::hardware_time::try_tsc()
{
    return 0;
    uint32_t eax=0x1, ebx=0, ecx=0, edx=0;
    cpuid(&eax, &ebx, &ecx, &edx);
    if(ecx&(1<<24))
    {
        is_tsc_reliable = true;
        eax=0x15;
        cpuid(&eax, &ebx, &ecx, &edx);
        uint64_t tsc_freq_hz=(uint64_t)ecx*ebx/eax;
        tsc_fs_per_cycle=1000000000000000ULL/tsc_freq_hz;
    }else{
        is_tsc_reliable = false;
    }
}

int time::hardware_time::inform_initialized_hpet()
{
    is_hpet_initialized = true;
    return 0;
}

bool time::hardware_time::get_tsc_reliable()
{
    return is_tsc_reliable;
}
bool time::hardware_time::get_if_hpet_initialized()
{
    return is_hpet_initialized;
}
time::hardware_time_base_token time::hardware_time::processor_regist()
{
    if(!is_tsc_reliable|| !is_hpet_initialized)
    {
        //两者都不可用，无法注册
        hardware_time_base_token null_token;
        setmem(&null_token,0,sizeof(hardware_time_base_token));
        return null_token;
    }else{
        hardware_time_base_token token;
        //读取当前HPET计数器值
        token.hpet_base = readonly_timer->get_time_stamp_in_mius();
        //读取当前TSC值
        token.tsc_base=rdtsc();
        return token;
    }
}

time::miusecond_time_stamp_t time::hardware_time::get_stamp(hardware_time_base_token token)
{
    if(is_hpet_initialized){
        if(is_tsc_reliable){
            //同时可靠，使用TSC进行换算
            uint64_t current_tsc=rdtsc();
            uint64_t delta_cycles=current_tsc-token.tsc_base;
            uint64_t delta_mius=((__uint128_t)delta_cycles*tsc_fs_per_cycle)/1000000000;
            
            return token.hpet_base+delta_mius;
        }else{
            return readonly_timer->get_time_stamp_in_mius();
        }
    }else{
        return rdtsc();
    }
    return 0;
}

