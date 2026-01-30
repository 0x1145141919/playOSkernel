#include "core_hardwares/lapic.h"
#include "util/OS_utils.h"
#include "util/cpuid_intel.h"
#include "msr_offsets_definitions.h"
#include "Interrupt_system/Interrupt.h"
#include "firmware/gSTResloveAPIs.h"
#include "memory/phygpsmemmgr.h"
#include "memory/AddresSpace.h"
#include "util/kout.h"
void x2apic::x2apic_driver::raw_config_timer(timer_lvt_entry entry)
{
    wrmsr(msr::apic::IA32_X2APIC_LVT_TIMER,entry.raw);
}

void x2apic::x2apic_driver::raw_config_timer_init_count(initcout_reg_t count)
{
    wrmsr(msr::apic::IA32_X2APIC_TIMER_INITIAL_COUNT,(uint64_t)count);
}

void x2apic::x2apic_driver::raw_config_timer_divider(devide_reg_t reg)
{
    wrmsr(msr::apic::IA32_X2APIC_TIMER_DIVIDE_CONFIG,reg.raw);
}

x2apic::current_reg_t x2apic::x2apic_driver::get_timer_current_count()
{
    return current_reg_t(rdmsr(msr::apic::IA32_X2APIC_TIMER_CURRENT_COUNT));
}


void x2apic::x2apic_driver::raw_send_ipi(x2apic_icr_t icr)
{
    wrmsr(msr::apic::IA32_X2APIC_ICR,icr.raw);
}

void x2apic::x2apic_driver::broadcast_exself_fixed_ipi(void (*ipi_handler)())
{
    global_ipi_handler=ipi_handler;
    asm volatile (
        "sfence"    
    );
    raw_send_ipi(broadcast_exself_icr);
}