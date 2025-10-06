#include "panic.h"
#include "VideoDriver.h"
#include <efi.h>
#include <efilib.h>
#include "UefiRunTimeServices.h"
// 延时函数，简单的忙等待
KernelPanicManager gkernelPanicManager;
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
KernelPanicManager::KernelPanicManager() : shutdownDelay(5) {
    // 默认等待5秒
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
void KernelPanicManager::dumpRegisters(const pt_regs& regs) const {
    kputsSecure((char*)"Register dump:");
    kputsSecure((char*)"");

    kputsSecure((char*)"RAX: 0x");
    kpnumSecure((void*)&regs.rax, UNHEX, 8);
    kputsSecure((char*)", RBX: 0x");
    kpnumSecure((void*)&regs.rbx, UNHEX, 8);
    kputsSecure((char*)", RCX: 0x");
    kpnumSecure((void*)&regs.rcx, UNHEX, 8);
    kputsSecure((char*)" ");

    kputsSecure((char*)"RDX: 0x");
    kpnumSecure((void*)&regs.rdx, UNHEX, 8);
    kputsSecure((char*)", RSI: 0x");
    kpnumSecure((void*)&regs.rsi, UNHEX, 8);
    kputsSecure((char*)", RDI: 0x");
    kpnumSecure((void*)&regs.rdi, UNHEX, 8);
    kputsSecure((char*)" ");

    kputsSecure((char*)"RBP: 0x");
    kpnumSecure((void*)&regs.rbp, UNHEX, 8);
    kputsSecure((char*)", RSP: 0x");
    kpnumSecure((void*)&regs.rsp, UNHEX, 8);
    kputsSecure((char*)", RIP: 0x");
    kpnumSecure((void*)&regs.rip, UNHEX, 8);
    kputsSecure((char*)" ");

    kputsSecure((char*)"R8:  0x");
    kpnumSecure((void*)&regs.r8, UNHEX, 8);
    kputsSecure((char*)", R9:  0x");
    kpnumSecure((void*)&regs.r9, UNHEX, 8);
    kputsSecure((char*)", R10: 0x");
    kpnumSecure((void*)&regs.r10, UNHEX, 8);
    kputsSecure((char*)"");

    kputsSecure((char*)"R11: 0x");
    kpnumSecure((void*)&regs.r11, UNHEX, 8);
    kputsSecure((char*)", R12: 0x");
    kpnumSecure((void*)&regs.r12, UNHEX, 8);
    kputsSecure((char*)", R13: 0x");
    kpnumSecure((void*)&regs.r13, UNHEX, 8);
    kputsSecure((char*)"");

    kputsSecure((char*)"R14: 0x");
    kpnumSecure((void*)&regs.r14, UNHEX, 8);
    kputsSecure((char*)", R15: 0x");
    kpnumSecure((void*)&regs.r15, UNHEX, 8);
    kputsSecure((char*)"");

    kputsSecure((char*)"CS:  0x");
    kpnumSecure((void*)&regs.cs, UNHEX, 2);
    kputsSecure((char*)", SS:  0x");
    kpnumSecure((void*)&regs.ss, UNHEX, 2);
    kputsSecure((char*)", EFLAGS: 0x");
    kpnumSecure((void*)&regs.eflags, UNHEX, 4);
    kputsSecure((char*)" ");
}

/**
 * 触发内核恐慌，打印错误信息并停机（无额外信息）
 */
void KernelPanicManager::panic(const char* message) const {
    kputsSecure((char*)"======= KERNEL PANIC =======");
    kputsSecure((char*)message);

    // 等待一段时间
    delay(shutdownDelay * 1000);

    gRuntimeServices.rt_shutdown();    // 停机

}

/**
 * 触发内核恐慌，打印错误信息和寄存器信息并停机
 */
void KernelPanicManager::panic(const char* message, const pt_regs& regs) const {
    kputsSecure((char*)"\n======= KERNEL PANIC =======\n");
    kputsSecure((char*)message);
    kputsSecure((char*)"");

    dumpRegisters(regs);

    // 等待一段时间
    delay(shutdownDelay * 1000);

    gRuntimeServices.rt_shutdown();
}


/**
 * 带信息包的内核恐慌函数
 */
void KernelPanicManager::panic(const char* message, const panic_info_t& info) const {
    kputsSecure((char*)"\n======= KERNEL PANIC =======\n");
    kputsSecure((char*)message);
    kputsSecure((char*)"\n");

    // 如果信息包中包含寄存器信息，则打印寄存器状态
    if (info.has_regs) {
        dumpRegisters(info.regs);
    }

    // 等待一段时间
    delay(shutdownDelay * 1000);

    gRuntimeServices.rt_shutdown();
}