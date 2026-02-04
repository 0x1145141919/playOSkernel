#include "core_hardwares/PortDriver.h"
#include "KernelEntryPointDefinetion.h"
#include "os_error_definitions.h"
#include "core_hardwares/VideoDriver.h"
#include "kcirclebufflogMgr.h"
#include "16x32AsciiCharacterBitmapSet.h"
#include "core_hardwares/HPET.h"
#include "Interrupt_system/loacl_processor.h"
#include "core_hardwares/lapic.h"
#include "memory/Memory.h"
#include "memory/kpoolmemmgr.h"
#include "memory/phygpsmemmgr.h"
#include "memory/FreePagesAllocator.h"
#include "util/cpuid_intel.h"
#include "memory/AddresSpace.h"
#include "util/OS_utils.h"
#include "msr_offsets_definitions.h"
#include "time.h"
#include "util/kout.h"
#include "firmware/UefiRunTimeServices.h"
#include "panic.h"
#include "firmware/gSTResloveAPIs.h"
#include "util/kptrace.h"
#include "firmware/ACPI_APIC.h"
#include "Interrupt_system/AP_Init_error_observing_protocol.h"
#undef __stack_chk_fail
extern  void __wrap___stack_chk_fail(void);
// 定义C++运行时需要的符号
 extern "C" {
    // DSO句柄，对于静态链接的内核，可以简单定义为空
    void* __dso_handle = 0;
    
    // 用于注册析构函数的函数，这里提供一个空实现
    int __cxa_atexit(void (*func)(void*), void* arg, void* dso) {
        (void)func;
        (void)arg;
        (void)dso;
        return 0;
    }
}
extern "C" void delay(unsigned int milliseconds) {
    for (unsigned int i = 0; i < milliseconds * 10000; ++i) {
        // 空循环，占用 CPU
        asm volatile("nop"); // 防止编译器优化（可选）
    }
}
EFI_TIME global_time;
uint32_t efi_map_ver;
/*
注意，这个函数刚进入时还使用的是bootloader的栈
在特定几个函数初始化好后才能使用切换到映像的栈
*/
extern "C" void kernel_start(BootInfoHeader* transfer) 
{   
    GlobalKernelStatus=kernel_state::ENTER;
    int  Status=0;
    KURD_t bsp_init_kurd=KURD_t();
    time::hardware_time::try_tsc();
    ksymmanager::Init(transfer);    
    global_gST=transfer->gST_ptr;
    efi_map_ver=transfer->mapversion;
    GlobalBasicGraphicInfoType* TFG=(GlobalBasicGraphicInfoType*)&transfer->graphic_metainfo;
    Status = InitialGlobalBasicGraphicInfo(
        TFG->horizentalResolution,
        TFG->verticalResolution,
        TFG->pixelFormat,
        TFG->PixelsPerScanLine,
        TFG->FrameBufferBase,
        TFG->FrameBufferSize
    );
    serial_init_stage1();
    gkcirclebufflogMgr.Init();
    kio::bsp_kout.Init();
    kio::bsp_kout.shift_dec();
    if (Status!=OS_SUCCESS)
    {
        serial_puts("InitialGlobalBasicGraphicInfo Failed\n");
        return ;
    }
    kpoolmemmgr_t::Init();
    Status =InitialGlobalCharacterSetBitmapControler(
        32,
        16,
        0x00000000,
        0x00FFFFFF,
        (UINT8*)ter16x32_data[0][0],
        FALSE,
        NULL
    );
    Status=InitialKernelShellControler(
        GlobalBasicGraphicInfo.verticalResolution,
        GlobalBasicGraphicInfo.horizentalResolution,
        ASCII,
        0,0,0,0,
        GlobalKernelShellInputBuffer,
        GlobalKernelShellOutputBuffer,
        4096,
        &GlobalCharacterSetBitmapControler,
        4096,0,0
    );
    if (Status!=OS_SUCCESS)
    {
        kio::bsp_kout<<"InitialKernelShellControler Failed\n";
        return ;
    }
    // 初始化全局Ascii位图控制器
    kio::bsp_kout<<"Kernel Shell Initialed Success\n";
    GlobalKernelStatus=kernel_state::PANIC_WILL_ANALYZE;
    Panic::will_check();
    gBaseMemMgr.Init(reinterpret_cast<EFI_MEMORY_DESCRIPTORX64*>(transfer->memory_map_ptr),transfer->memory_map_entry_count);
    Status=gBaseMemMgr.FixedPhyaddPgallocate(
        transfer->ksymbols_table_phy_ptr,
        transfer->ksymbols_entry_size*transfer->ksymbols_entry_count,
        PHY_MEM_TYPE::OS_KERNEL_DATA
    );
    if(Status!=OS_SUCCESS){
        kio::bsp_kout << "FixedPhyaddPgallocate Failed for"<<Status << "\n";
    asm volatile("hlt");
    }
    gBaseMemMgr.printPhyMemDesTb();
    
    bsp_init_kurd=phymemspace_mgr::Init();
    if(error_kurd(bsp_init_kurd)){
        kio::bsp_kout<<"phymemspace_mgr Init Failed"<<kio::kendl;
        asm volatile("hlt");
    }
    bsp_init_kurd=FreePagesAllocator::Init();
    if(error_kurd(bsp_init_kurd)){
        kio::bsp_kout<<"FreePagesAllocator Init Failed"<<kio::kendl;
        asm volatile("hlt");
    }
    bsp_init_kurd=KspaceMapMgr::Init();
    if(error_kurd(bsp_init_kurd)){
        kio::bsp_kout<<"KspaceMapMgr Init Failed"<<kio::kendl;
        asm volatile("hlt");
    }
    gKernelSpace=new AddressSpace();
    bsp_init_kurd=gKernelSpace->second_stage_init();
    if(Status!=OS_SUCCESS)
    {
        kio::bsp_kout << "KernelSpace Init Failed" << "\n";
        asm volatile("hlt");
    }
    bsp_init_kurd=gKernelSpace->build_identity_map_ONLY_IN_gKERNELSPACE();
    if(Status!=OS_SUCCESS){
        kio::bsp_kout << "KernelSpace Build Identity Map Failed" << kio::kendl;
        asm volatile("hlt");
    }
    Status=EFI_RT_SVS::Init(transfer->gST_ptr,efi_map_ver);
    gAcpiVaddrSapceMgr.Init(global_gST);
    gKernelSpace->unsafe_load_pml4_to_cr3(KERNEL_SPACE_PCID);
    GlobalKernelStatus=kernel_state::MM_READY;
    phymemspace_mgr::subtb_alloc_is_pool_way_flag_enable();
    readonly_timer=new HPET_driver_only_read_time_stamp(
        (HPET::ACPItb::HPET_Table*)gAcpiVaddrSapceMgr.get_acpi_table((char*)"HPET")
    );
    bsp_init_kurd=readonly_timer->second_stage_init();
    if(error_kurd(bsp_init_kurd)){
        kio::bsp_kout<<"HPET Init Failed"<<kio::kendl;
        asm volatile("hlt");
    }
    time::hardware_time::inform_initialized_hpet();
    time::hardware_time::processor_regist();
    kio::bsp_kout<<kio::now<<"HPET Initialized Success"<<kio::kendl;
    x86_smp_processors_container::template_idt_init();
    x86_smp_processors_container::regist_core(0);
    kpoolmemmgr_t::self_heap_init();
    kio::bsp_kout<<kio::now<<"BSP online"<<kio::kendl;
    gAnalyzer=new APIC_table_analyzer((MADT_Table*)gAcpiVaddrSapceMgr.get_acpi_table("APIC"));
    x86_smp_processors_container::AP_Init_one_by_one();
    kpoolmemmgr_t::enable_new_hcb_alloc();
    //中断接管工作
    asm volatile("hlt");
    

}
extern check_point longmode_enter_checkpoint;
extern check_point init_finish_checkpoint;
extern "C" void ap_init(uint32_t processor_id)
{
    longmode_enter_checkpoint.check_point_id=~processor_id;
    asm volatile("sfence");
    gKernelSpace->unsafe_load_pml4_to_cr3(KERNEL_SPACE_PCID);
    x86_smp_processors_container::regist_core(processor_id); 
    kpoolmemmgr_t::self_heap_init();
    time::hardware_time::processor_regist();
    longmode_enter_checkpoint.check_point_id=~query_x2apicid();
    asm volatile("sfence");
    asm volatile("sti");
    asm volatile("hlt");
}