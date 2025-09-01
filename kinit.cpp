#include<stdint.h>
#include<efi.h>
#include "PortDriver.h"
#include "errno.h"
#include "VideoDriver.h"
#include "16x32AsciiCharacterBitmapSet.h"
#include "Memory.h"
#include "kpoolmemmgr.h"

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

GlobalMemoryPGlevelMgr_t gBaseMemMgr;
extern "C" int _kernel_Init(void* TransferPage,
    int numofpages,
    EFI_MEMORY_DESCRIPTORX64*memDescript,
    int numofDiscriptors,
     EFI_SYSTEM_TABLE*gST) 
{
    int  Status=0;
    ;
    GlobalBasicGraphicInfoType* TFG=(GlobalBasicGraphicInfoType*)TransferPage;
    Status = InitialGlobalBasicGraphicInfo(
        TFG->horizentalResolution,
        TFG->verticalResolution,
        TFG->pixelFormat,
        TFG->PixelsPerScanLine,
        TFG->FrameBufferBase,
        TFG->FrameBufferSize
    );
    serial_init();
    if (Status!=OS_SUCCESS)
    {
        serial_puts("InitialGlobalBasicGraphicInfo Failed\n");
        return Status;
    }
    gKpoolmemmgr.Init();
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
        serial_puts("InitialKernelShellControler Failed\n");
        return Status;
    }
    
    // 初始化全局Ascii位图控制器
    kputsSecure("Welcome to PlayOSKernelShell\n");
    gBaseMemMgr.Init(memDescript,numofDiscriptors);
    //gBaseMemMgr.printEfiMemoryDescriptorTable();
    gBaseMemMgr.printPhyMemDesTb();
    asm volatile("hlt");    
}