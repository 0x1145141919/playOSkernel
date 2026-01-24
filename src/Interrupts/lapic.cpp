#include "core_hardwares/lapic.h"
#include "util/OS_utils.h"
#include "util/cpuid_intel.h"
#include "msr_offsets_definitions.h"
#include "Interrupt_system/Interrupt.h"
#include "firmware/gSTResloveAPIs.h"
#include "memory/phygpsmemmgr.h"
#include "memory/AddresSpace.h"
#include "util/kout.h"
phyaddr_t xapic::apic_driver::lapic_phyaddr;
vaddr_t xapic::apic_driver::lapic_virtaddr;
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
int xapic::apic_driver::Init()
{
    MADT_Table* madt=(MADT_Table*)gAcpiVaddrSapceMgr.get_acpi_table("APIC");
    lapic_phyaddr=madt->LocalApicAddress;
    int status=phymemspace_mgr::pages_mmio_regist(lapic_phyaddr,1);
    if(status!=OS_SUCCESS){
        phymemspace_mgr::blackhole_acclaim_flags_t flags={.a=0};
        status=phymemspace_mgr::blackhole_acclaim(lapic_phyaddr,1,phymemspace_mgr::MMIO_SEG,flags);
        if(status!=OS_SUCCESS)
        {
            kio::bsp_kout<<kio::now<<"xapic::apic_driver::Init() failed to aclaim lapic mmio"<<kio::kendl;
            return status;
        }
        status=phymemspace_mgr::pages_mmio_regist(lapic_phyaddr,1);
        if(status!=OS_SUCCESS)
        {
            kio::bsp_kout<<kio::now<<"xapic::apic_driver::Init() failed to register lapic mmio"<<kio::kendl;
            return status;
        }
    }
    pgaccess access=KspaceMapMgr::PG_RW;
    access.cache_strategy=UC;
    lapic_virtaddr=(vaddr_t)KspaceMapMgr::pgs_remapp(lapic_phyaddr,4096,access);
    if(lapic_virtaddr==0)return OS_MEMRY_ALLOCATE_FALT;
    return OS_SUCCESS;
}
void xapic::apic_driver::send_ipi(xapic_icr_dest_t dest, xapic_icr_command_t command)
{
    atomic_write32_wmb((void*)(lapic_virtaddr+0x300),command.raw);
    atomic_write32_wmb((void*)(lapic_virtaddr+0x310),dest.raw);
}