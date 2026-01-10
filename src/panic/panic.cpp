#include "panic.h"
#include "VideoDriver.h"
#include <efi.h>
#include <efilib.h>
#include "UefiRunTimeServices.h"
#include "kout.h"
#ifdef USER_MODE
#include <unistd.h>
#endif 

// 定义KernelPanicManager的静态成员变量
int KernelPanicManager::shutdownDelay = 5; // 静态成员变量定义及初始化

// 注意：global_gST已经在UefiRunTimeServices.cpp中定义，此处不再重复定义
// EFI_SYSTEM_TABLE* global_gST = nullptr;

void delay(uint64_t milliseconds)
{
    for (size_t i = 0; i < (milliseconds << 20); i++)
    {
        asm volatile("nop");
    }
    
}
/**
 * 私有构造函数
 */
KernelPanicManager::KernelPanicManager(){
    // shutdownDelay已经作为静态成员初始化为5
}

KernelPanicManager::~KernelPanicManager()
{
}

/**
 * 获取单例实例
 */
void KernelPanicManager::Init(uint64_t delay_sec)
{
    shutdownDelay = delay_sec;
}
/**
 * 设置关机前等待时间
 */
void KernelPanicManager::setShutdownDelay(int seconds) {
    shutdownDelay = seconds;
}

/**
 * 打印寄存器信息
 */
void KernelPanicManager::dumpRegisters(const pt_regs& regs){
    kio::bsp_kout << "Register dump:" << kio::kendl;

    kio::bsp_kout.shift_hex();
    kio::bsp_kout << "RAX: 0x" << regs.rax 
                  << ", RBX: 0x" << regs.rbx 
                  << ", RCX: 0x" << regs.rcx 
                  << kio::kendl;

    kio::bsp_kout << "RDX: 0x" << regs.rdx 
                  << ", RSI: 0x" << regs.rsi 
                  << ", RDI: 0x" << regs.rdi 
                  << kio::kendl;

    kio::bsp_kout << "RBP: 0x" << regs.rbp 
                  << ", RSP: 0x" << regs.rsp 
                  << ", RIP: 0x" << regs.rip 
                  << kio::kendl;

    kio::bsp_kout << "R8:  0x" << regs.r8 
                  << ", R9:  0x" << regs.r9 
                  << ", R10: 0x" << regs.r10 
                  << kio::kendl;

    kio::bsp_kout << "R11: 0x" << regs.r11 
                  << ", R12: 0x" << regs.r12 
                  << ", R13: 0x" << regs.r13 
                  << kio::kendl;

    kio::bsp_kout << "R14: 0x" << regs.r14 
                  << ", R15: 0x" << regs.r15 
                  << kio::kendl;

    kio::bsp_kout << "CS:  0x" << (uint16_t)regs.cs 
                  << ", SS:  0x" << (uint16_t)regs.ss 
                  << ", EFLAGS: 0x" << (uint32_t)regs.eflags 
                  << kio::kendl;
}

/**
 * 触发内核恐慌，打印错误信息并停机（无额外信息）
 */
void KernelPanicManager::panic(const char* message){
    kio::bsp_kout << "======= KERNEL PANIC =======" << kio::kendl;
    kio::bsp_kout << message << kio::kendl;
#ifdef USER_MODE
 _exit(1);
#endif 
   #ifdef KERNEL_MODE
// 等待一段时间
    delay(shutdownDelay * 1000);

    EFI_RT_SVS::rt_shutdown();
#endif 

}

/**
 * 触发内核恐慌，打印错误信息和寄存器信息并停机
 */
void KernelPanicManager::panic(const char* message, const pt_regs& regs){
    kio::bsp_kout << "\n======= KERNEL PANIC =======\n" << kio::kendl;
    kio::bsp_kout << message << kio::kendl;
    kio::bsp_kout << "" << kio::kendl;

    dumpRegisters(regs);
    #ifdef USER_MODE
 _exit(1);
#endif 
    #ifdef KERNEL_MODE
// 等待一段时间
    //delay(shutdownDelay * 1000);

    EFI_RT_SVS::rt_shutdown();
#endif 
}


/**
 * 带信息包的内核恐慌函数
 */
void KernelPanicManager::panic(const char* message, const panic_info_t& info){
    kio::bsp_kout << "\n======= KERNEL PANIC =======\n" << kio::kendl;
    kio::bsp_kout << message << kio::kendl;
    kio::bsp_kout << "\n" << kio::kendl;

    // 如果信息包中包含寄存器信息，则打印寄存器状态
    if (info.has_regs) {
        dumpRegisters(info.regs);
    }
    #ifdef USER_MODE
 _exit(1);
#endif 
#ifdef KERNEL_MODE
// 等待一段时间
    delay(shutdownDelay * 1000);

    EFI_RT_SVS::rt_shutdown();
#endif 
    
}