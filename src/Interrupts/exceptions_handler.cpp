#include "Interrupt_system/Interrupt.h"
#include "kout.h"
#include "util/OS_utils.h"
#include "VideoDriver.h"
#include "panic.h"
#include "pt_regs.h"
void (*global_ipi_handler)()=nullptr;
__attribute__((interrupt)) void exception_handler_div_by_zero(interrupt_frame* frame)
{
    pt_regs regs;

    regs.cs = frame->cs;
    regs.rip = frame->rip;
    regs.eflags = frame->rflags;
    regs.rsp = frame->rsp;
    regs.ss = frame->ss;
    regs.rax = frame->rax;
    regs.rbx = frame->rbx;
    regs.rcx = frame->rcx;
    regs.rdx = frame->rdx;
    regs.rbp = frame->rbp;
    regs.rdi = frame->rdi;
    regs.rsi = frame->rsi;
    regs.r8 = frame->r8;
    regs.r9 = frame->r9;
    regs.r10 = frame->r10;
    regs.r11 = frame->r11;
    regs.r12 = frame->r12;
    regs.r13 = frame->r13;
    regs.r14 = frame->r14;
    regs.r15 = frame->r15;
    kio::bsp_kout << "[EXCEPTION] some exception happened: Divide by zero" << kio::kendl;
    if(frame->cs&0x3){
        kio::bsp_kout << "[USER] ";
        //用户态异常有待处理
        return;
    }else{
        kio::bsp_kout << "[KERNEL] ";
        KernelPanicManager::panic("Divide by zero", regs);
    }
        
}

// 无效操作码异常 (#UD)
__attribute__((interrupt)) void exception_handler_invalid_opcode(interrupt_frame* frame)
{
    pt_regs regs;

    regs.cs = frame->cs;
    regs.rip = frame->rip;
    regs.eflags = frame->rflags;
    regs.rsp = frame->rsp;
    regs.ss = frame->ss;
    regs.rax = frame->rax;
    regs.rbx = frame->rbx;
    regs.rcx = frame->rcx;
    regs.rdx = frame->rdx;
    regs.rbp = frame->rbp;
    regs.rdi = frame->rdi;
    regs.rsi = frame->rsi;
    regs.r8 = frame->r8;
    regs.r9 = frame->r9;
    regs.r10 = frame->r10;
    regs.r11 = frame->r11;
    regs.r12 = frame->r12;
    regs.r13 = frame->r13;
    regs.r14 = frame->r14;
    regs.r15 = frame->r15;
    kio::bsp_kout << "[EXCEPTION] some exception happened: Invalid Opcode (#UD)" << kio::kendl;
    if(frame->cs&0x3){
        kio::bsp_kout << "[USER] ";
        //用户态异常有待处理
        return;
    }else{
        kio::bsp_kout << "[KERNEL] ";
        KernelPanicManager::panic("Invalid Opcode", regs);
    }
}

// 通用保护异常 (#GP)
__attribute__((interrupt)) void exception_handler_general_protection(interrupt_frame* frame, uint64_t error_code)
{
    pt_regs regs;

    regs.cs = frame->cs;
    regs.rip = frame->rip;
    regs.eflags = frame->rflags;
    regs.rsp = frame->rsp;
    regs.ss = frame->ss;
    regs.rax = frame->rax;
    regs.rbx = frame->rbx;
    regs.rcx = frame->rcx;
    regs.rdx = frame->rdx;
    regs.rbp = frame->rbp;
    regs.rdi = frame->rdi;
    regs.rsi = frame->rsi;
    regs.r8 = frame->r8;
    regs.r9 = frame->r9;
    regs.r10 = frame->r10;
    regs.r11 = frame->r11;
    regs.r12 = frame->r12;
    regs.r13 = frame->r13;
    regs.r14 = frame->r14;
    regs.r15 = frame->r15;
    
    kio::bsp_kout << "[EXCEPTION] some exception happened: General Protection (#GP)" << kio::kendl;
    kio::bsp_kout << "Error code: 0x" << error_code << kio::kendl;
    
    if(frame->cs&0x3){
        kio::bsp_kout << "[USER] ";
        //用户态异常有待处理
        return;
    }else{
        kio::bsp_kout << "[KERNEL] ";
        KernelPanicManager::panic("General Protection", regs);
    }
}

// 双重错误异常 (#DF)
__attribute__((interrupt)) void exception_handler_double_fault(interrupt_frame* frame, uint64_t error_code)
{
    pt_regs regs;

    regs.cs = frame->cs;
    regs.rip = frame->rip;
    regs.eflags = frame->rflags;
    regs.rsp = frame->rsp;
    regs.ss = frame->ss;
    regs.rax = frame->rax;
    regs.rbx = frame->rbx;
    regs.rcx = frame->rcx;
    regs.rdx = frame->rdx;
    regs.rbp = frame->rbp;
    regs.rdi = frame->rdi;
    regs.rsi = frame->rsi;
    regs.r8 = frame->r8;
    regs.r9 = frame->r9;
    regs.r10 = frame->r10;
    regs.r11 = frame->r11;
    regs.r12 = frame->r12;
    regs.r13 = frame->r13;
    regs.r14 = frame->r14;
    regs.r15 = frame->r15;
    
    kio::bsp_kout << "[EXCEPTION] some exception happened: Double Fault (#DF)" << kio::kendl;
    kio::bsp_kout << "Error code: 0x" << error_code << kio::kendl;
    
    kio::bsp_kout << "[KERNEL] ";
    KernelPanicManager::panic("Double Fault", regs);
}

// 页错误异常 (#PF)
__attribute__((interrupt)) void exception_handler_page_fault(interrupt_frame* frame, uint64_t error_code)
{
    pt_regs regs;

    regs.cs = frame->cs;
    regs.rip = frame->rip;
    regs.eflags = frame->rflags;
    regs.rsp = frame->rsp;
    regs.ss = frame->ss;
    regs.rax = frame->rax;
    regs.rbx = frame->rbx;
    regs.rcx = frame->rcx;
    regs.rdx = frame->rdx;
    regs.rbp = frame->rbp;
    regs.rdi = frame->rdi;
    regs.rsi = frame->rsi;
    regs.r8 = frame->r8;
    regs.r9 = frame->r9;
    regs.r10 = frame->r10;
    regs.r11 = frame->r11;
    regs.r12 = frame->r12;
    regs.r13 = frame->r13;
    regs.r14 = frame->r14;
    regs.r15 = frame->r15;
    // 获取导致页错误的线性地址
    uint64_t fault_addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
    
    kio::bsp_kout << "[EXCEPTION] some exception happened: Page Fault (#PF)" << kio::kendl;
    kio::bsp_kout << "Error code: 0x" << error_code << kio::kendl;
    kio::bsp_kout << "Fault address: 0x" << fault_addr << kio::kendl;
    
    if(frame->cs&0x3){
        kio::bsp_kout << "[USER] ";
        //用户态异常有待处理
        return;
    }else{
        kio::bsp_kout << "[KERNEL] ";
        KernelPanicManager::panic("Page Fault", regs);
    }
}

__attribute__((interrupt))void exception_handler_machine_check(interrupt_frame *frame)
{
} // 无效TSS异常 (#TS)
__attribute__((interrupt)) void exception_handler_invalid_tss(interrupt_frame* frame, uint64_t error_code)
{
    pt_regs regs;

    regs.cs = frame->cs;
    regs.rip = frame->rip;
    regs.eflags = frame->rflags;
    regs.rsp = frame->rsp;
    regs.ss = frame->ss;
    regs.rax = frame->rax;
    regs.rbx = frame->rbx;
    regs.rcx = frame->rcx;
    regs.rdx = frame->rdx;
    regs.rbp = frame->rbp;
    regs.rdi = frame->rdi;
    regs.rsi = frame->rsi;
    regs.r8 = frame->r8;
    regs.r9 = frame->r9;
    regs.r10 = frame->r10;
    regs.r11 = frame->r11;
    regs.r12 = frame->r12;
    regs.r13 = frame->r13;
    regs.r14 = frame->r14;
    regs.r15 = frame->r15;
    kputsSecure("[EXCEPTION] some exception happened: Invalid TSS (#TS)\n");
    kputsSecure("Error code: ");
    kpnumSecure(&error_code, UNHEX, sizeof(error_code));
    kputsSecure("\n");
    
    if(frame->cs&0x3){
        kputsSecure("[USER] ");
        //用户态异常有待处理
        return;
    }else{
        kputsSecure("[KERNEL] ");
        KernelPanicManager::panic("Invalid TSS", regs);
    }
}

// SIMD浮点异常 (#XM)
__attribute__((interrupt)) void exception_handler_simd_floating_point(interrupt_frame* frame)
{
    pt_regs regs;

    regs.cs = frame->cs;
    regs.rip = frame->rip;
    regs.eflags = frame->rflags;
    regs.rsp = frame->rsp;
    regs.ss = frame->ss;
    regs.rax = frame->rax;
    regs.rbx = frame->rbx;
    regs.rcx = frame->rcx;
    regs.rdx = frame->rdx;
    regs.rbp = frame->rbp;
    regs.rdi = frame->rdi;
    regs.rsi = frame->rsi;
    regs.r8 = frame->r8;
    regs.r9 = frame->r9;
    regs.r10 = frame->r10;
    regs.r11 = frame->r11;
    regs.r12 = frame->r12;
    regs.r13 = frame->r13;
    regs.r14 = frame->r14;
    regs.r15 = frame->r15;

    kputsSecure("[EXCEPTION] some exception happened: SIMD Floating-Point (#XM)\n");
    
    if(frame->cs&0x3){
        kputsSecure("[USER] ");
        //用户态异常有待处理
        return;
    }else{
        kputsSecure("[KERNEL] ");
        KernelPanicManager::panic("SIMD Floating-Point", regs);
    }
}

// 虚拟化异常 (#VE)
__attribute__((interrupt)) void exception_handler_virtualization(interrupt_frame* frame, uint64_t error_code)
{
    pt_regs regs;

    regs.cs = frame->cs;
    regs.rip = frame->rip;
    regs.eflags = frame->rflags;
    regs.rsp = frame->rsp;
    regs.ss = frame->ss;
    regs.rax = frame->rax;
    regs.rbx = frame->rbx;
    regs.rcx = frame->rcx;
    regs.rdx = frame->rdx;
    regs.rbp = frame->rbp;
    regs.rdi = frame->rdi;
    regs.rsi = frame->rsi;
    regs.r8 = frame->r8;
    regs.r9 = frame->r9;
    regs.r10 = frame->r10;
    regs.r11 = frame->r11;
    regs.r12 = frame->r12;
    regs.r13 = frame->r13;
    regs.r14 = frame->r14;
    regs.r15 = frame->r15;
    kputsSecure("[EXCEPTION] some exception happened: Virtualization (#VE)\n");
    kputsSecure("Error code: ");
    kpnumSecure(&error_code, UNHEX, sizeof(error_code));
    kputsSecure("\n");
    
    if(frame->cs&0x3){
        kputsSecure("[USER] ");
        //用户态异常有待处理
        return;
    }else{
        kputsSecure("[KERNEL] ");
        KernelPanicManager::panic("Virtualization", regs);
    }
}
__attribute__((interrupt)) void IPI(interrupt_frame* frame, uint64_t error_code)
{
    global_ipi_handler();
}
__attribute__((interrupt)) void exception_handler_breakpoint(interrupt_frame* frame)
{
    // 处理断点异常的代码
}
__attribute__((interrupt)) void exception_handler_nmi(interrupt_frame* frame)
{

}
__attribute__((interrupt)) void exception_handler_overflow(interrupt_frame* frame)
{
    // 处理溢出异常的代码
}
__attribute__((interrupt)) void timer_interrupt(interrupt_frame* frame)
{
    // 处理溢出异常的代码
}