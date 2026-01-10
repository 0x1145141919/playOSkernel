#include<stdint.h>
#include<efi.h>
#include "PortDriver.h"
#include "KernelEntryPointDefinetion.h"
#include "os_error_definitions.h"
#include "VideoDriver.h"
#include "kcirclebufflogMgr.h"
#include "16x32AsciiCharacterBitmapSet.h"
#include "HPET.h"
#include "Interrupt_system/loacl_processor.h"
#include "memory/Memory.h"
#include "memory/kpoolmemmgr.h"
#include "memory/phygpsmemmgr.h"
#include "memory/AddresSpace.h"
#include "time.h"
#include "kout.h"
#include "UefiRunTimeServices.h"
#include "panic.h"
#include "gSTResloveAPIs.h"
#undef __stack_chk_fail
extern  void __wrap___stack_chk_fail(void);
//extern char __ksymtab_start[];
//extern char __ksymtab_end[];
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
void delay(unsigned int milliseconds) {
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
extern "C" void kernel_start( BootInfoHeader* transfer) 
{   

    int  Status=0;
    time::hardware_time::try_tsc();
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
    KernelPanicManager::Init(5);
    // 初始化全局Ascii位图控制器
    kio::bsp_kout<<"Kernel Shell Initialed Success\n";
    gBaseMemMgr.Init(reinterpret_cast<EFI_MEMORY_DESCRIPTORX64*>(transfer->memory_map_ptr),transfer->memory_map_entry_count);
    gBaseMemMgr.printPhyMemDesTb();
    
    Status=phymemspace_mgr::Init();
    Status=KspaceMapMgr::Init();
    gKernelSpace=new AddressSpace();
    Status=gKernelSpace->second_stage_init();
    if(Status!=OS_SUCCESS)
    {
        kio::bsp_kout << "KernelSpace Init Failed" << kio::kendl;
        asm volatile("hlt");
    }
    Status=gKernelSpace->build_identity_map_ONLY_IN_gKERNELSPACE();
    if(Status!=OS_SUCCESS){
        kio::bsp_kout << "KernelSpace Build Identity Map Failed" << kio::kendl;
        asm volatile("hlt");
    }
    Status=EFI_RT_SVS::Init(transfer->gST_ptr,efi_map_ver);
    gAcpiVaddrSapceMgr.Init(global_gST);
    gKernelSpace->unsafe_load_pml4_to_cr3(KERNEL_SPACE_PCID);
    readonly_timer=new HPET_driver_only_read_time_stamp(
        (HPET::ACPItb::HPET_Table*)gAcpiVaddrSapceMgr.get_acpi_table((char*)"HPET")
    );
    Status=readonly_timer->second_stage_init();
    if(Status!=OS_SUCCESS){
        kio::bsp_kout<<"HPET Init Failed"<<kio::kendl;
        asm volatile("hlt");
    }
    time::hardware_time::inform_initialized_hpet();
    time::hardware_time_base_token* temp_token = (time::hardware_time_base_token*)kpoolmemmgr_t::kalloc(sizeof(time::hardware_time_base_token));
    if(temp_token != nullptr) {
        *temp_token = time::hardware_time::processor_regist();
        time::bsp_token = temp_token;
    } else {
        // 处理分配失败的情况，可能需要触发内核恐慌或其他错误处理
        kio::bsp_kout << "Failed to allocate memory for time::bsp_token" << kio::kendl;
        asm volatile("hlt"); // 停止系统
    }
    kio::bsp_kout<<kio::now<<"HPET Initialized Success"<<kio::kendl;
    x86_smp_processors_container::template_idt_init();
    x86_smp_processors_container::regist_core();
    kio::bsp_kout<<kio::now<<"BSP online"<<kio::kendl;
    //kio::bsp_kout<<"__ksymtab_start:"<<(void*)__ksymtab_start<<kio::kendl;
    //kio::bsp_kout<<"__ksymtab_end:"<<(void*)__ksymtab_end<<kio::kendl;
    //中断接管工作
    asm volatile("hlt");
    

}