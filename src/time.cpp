#include "time.h"
#include "util/OS_utils.h"
#include "util/cpuid_intel.h"
#include "core_hardwares/HPET.h"
#include "GS_Slots_index_definitions.h"
#include "core_hardwares/lapic.h"
bool time::hardware_time::is_tsc_reliable = false;
uint32_t time::hardware_time::bsp_tsc_fs_per_cycle = 0;
bool time::hardware_time::is_hpet_initialized = false;
bool time::hardware_time::is_bsp_registed;
int time::hardware_time::try_tsc()
{
    
    uint32_t eax=0x1, ebx=0, ecx=0, edx=0;
    cpuid(&eax, &ebx, &ecx, &edx);
    is_bsp_registed=false;
    if(ecx&(1<<24))
    {
        is_tsc_reliable = true;
        eax=0x15;
        cpuid(&eax, &ebx, &ecx, &edx);
        uint64_t tsc_freq_hz=(uint64_t)ecx*ebx/eax;
        bsp_tsc_fs_per_cycle=1000000000000000ULL/tsc_freq_hz;
    }else{
        is_tsc_reliable = false;
    }
    return 0;
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
void time::hardware_time::processor_regist()
{
    if(!is_tsc_reliable|| !is_hpet_initialized)
    {

    }else{
        time_complex*complex=new time_complex;
        //读取当前HPET计数器值
        complex->private_token.hpet_base = readonly_timer->get_time_stamp_in_mius();
        //读取当前TSC值
        complex->private_token.tsc_base=rdtsc();
        if(is_tsc_reliable){
            uint32_t eax=0x1, ebx=0, ecx=0, edx=0;
            eax=0x15;
            cpuid(&eax, &ebx, &ecx, &edx);
            uint64_t tsc_freq_hz=(uint64_t)ecx*ebx/eax;
            complex->private_token.tsc_fs_per_cycle=1000000000000000ULL/tsc_freq_hz;
        }
        is_bsp_registed=true;
        gs_u64_write(TIME_COMPLEX_GS_INDEX,(uint64_t)complex);
    }
}

miusecond_time_stamp_t time::hardware_time::get_stamp( )
{
    if(is_hpet_initialized){
        if(is_tsc_reliable){
            //同时可靠，使用TSC进行换算
            time_complex*complex=(time_complex*)read_gs_u64(TIME_COMPLEX_GS_INDEX);
            uint64_t current_tsc=rdtsc();
            uint64_t delta_cycles=current_tsc-complex->private_token.tsc_base;
            uint64_t tsc_fs_per_cycle=(is_bsp_registed?complex->private_token.tsc_fs_per_cycle:bsp_tsc_fs_per_cycle);
            uint64_t delta_mius=((__uint128_t)delta_cycles*tsc_fs_per_cycle)/1000000000;
            
            return complex->private_token.hpet_base+delta_mius;
        }else{
            return readonly_timer->get_time_stamp_in_mius();
        }
    }else{
        return rdtsc();
    }
    return 0;
}
constexpr uint64_t calibrated_cycles_per_us = 5000;
void time::hardware_time::timer_polling_spin_delay(uint64_t microseconds)
{
    
        if(is_tsc_reliable){
            uint64_t current_tsc=rdtsc();
            __uint128_t target_tsc=current_tsc+((__uint128_t)microseconds*1000000000)/bsp_tsc_fs_per_cycle;
            while(current_tsc<target_tsc){
                asm volatile("pause");
                current_tsc=rdtsc();
            }
        }else{
            if(is_hpet_initialized){
                uint64_t base_time=readonly_timer->get_time_stamp_in_mius();
                uint64_t current_time=base_time;
                while(current_time-base_time<microseconds){
                    asm volatile("pause");
                    current_time=readonly_timer->get_time_stamp_in_mius();
                }
            }else{
                uint64_t Approximate_count = microseconds * calibrated_cycles_per_us;
                asm volatile(
                "1:\n\t"
                "pause\n\t"
                "loop 1b\n\t"  // 使用loop指令
                : 
                : "c" (Approximate_count)  // 将计数器放入ecx寄存器
                : "memory"
                );
            }
        }
    
}
time::backend_choose time::time_interrupt_generator::back_end_type;
namespace {
inline time::backend_choose normalize_backend(time::backend_choose backend)
{
    switch (backend) {
        case time::lapic_normal:
        case time::lapic_tscddline:
            return backend;
        default:
            return time::lapic_normal;
    }
}
}
void time::time_interrupt_generator::bsp_init()
{
    if(hardware_time::get_tsc_reliable()){
        back_end_type=lapic_tscddline;
    }else{
        back_end_type=lapic_normal;
    }
    switch(back_end_type){
        case lapic_normal:
            x2apic::lapic_timer_one_shot::processor_regist();
            break;
        case lapic_tscddline:
            x2apic::lapic_timer_tsc_ddline::processor_regist();
            break;
    }
}
void time::time_interrupt_generator::set_clock_by_stamp(miusecond_time_stamp_t stamp)
{
    back_end_type = normalize_backend(back_end_type);
    switch(back_end_type){
        case lapic_normal:
            x2apic::lapic_timer_one_shot::set_clock_by_stamp(stamp);
            break;
        case lapic_tscddline:
            x2apic::lapic_timer_tsc_ddline::set_clock_by_stamp(stamp);
            break;
    };
}
void time::time_interrupt_generator::set_clock_by_offset(miusecond_time_stamp_t offset)
{
    back_end_type = normalize_backend(back_end_type);
    switch(back_end_type){
        case lapic_normal:
            x2apic::lapic_timer_one_shot::set_clock_by_offset(offset);
            break;
            case lapic_tscddline:
            x2apic::lapic_timer_tsc_ddline::set_clock_by_offset(offset);
            break;
    };
}
uint64_t time::time_interrupt_generator::get_current_clock()
{
    back_end_type = normalize_backend(back_end_type);
    switch(back_end_type){
        case lapic_normal:
            return x2apic::lapic_timer_one_shot::get_current_clock();
        case lapic_tscddline:
            return rdtsc();
    };
    return 0;
}
void time::time_interrupt_generator::cancel_clock()
{
    back_end_type = normalize_backend(back_end_type);
    switch(back_end_type){
        case lapic_normal:
            x2apic::lapic_timer_one_shot::cancel_clock();
            break;
        case lapic_tscddline:
            x2apic::lapic_timer_tsc_ddline::cancel_clock();
            break;
    };
}
bool time::time_interrupt_generator::is_alarm_set()
{
    back_end_type = normalize_backend(back_end_type);
    switch(back_end_type){
        case lapic_normal:
            return x2apic::lapic_timer_one_shot::is_alarm_valid();
        case lapic_tscddline:
            return x2apic::lapic_timer_tsc_ddline::is_alarm_valid();
    };
    return false;
}
time::backend_choose time::time_interrupt_generator::get_backend_type()
{
    back_end_type = normalize_backend(back_end_type);
    return back_end_type;
}
void time::time_interrupt_generator::ap_init()
{
    back_end_type = normalize_backend(back_end_type);
    switch(back_end_type){
        case lapic_normal:
            x2apic::lapic_timer_one_shot::processor_regist();
            break;
        case lapic_tscddline:
            x2apic::lapic_timer_tsc_ddline::processor_regist();
            break;
    };
}
