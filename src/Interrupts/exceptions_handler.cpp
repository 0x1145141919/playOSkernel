#include "Interrupt.h"
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
    kputsSecure("[EXCEPTION] some exception happened: Divide by zero\n");
    if(frame->cs&0x3){
        kputsSecure("[USER] ");
        //用户态异常有待处理
        return;
    }else{
        kputsSecure("[KERNEL] ");
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
    kputsSecure("[EXCEPTION] some exception happened: Invalid Opcode (#UD)\n");
    if(frame->cs&0x3){
        kputsSecure("[USER] ");
        //用户态异常有待处理
        return;
    }else{
        kputsSecure("[KERNEL] ");
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
    
    kputsSecure("[EXCEPTION] some exception happened: General Protection (#GP)\n");
    kputsSecure("Error code: ");
    kpnumSecure(&error_code, UNHEX, sizeof(error_code));
    kputsSecure("\n");
    
    if(frame->cs&0x3){
        kputsSecure("[USER] ");
        //用户态异常有待处理
        return;
    }else{
        kputsSecure("[KERNEL] ");
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
    
    kputsSecure("[EXCEPTION] some exception happened: Double Fault (#DF)\n");
    kputsSecure("Error code: ");
    kpnumSecure(&error_code, UNHEX, sizeof(error_code));
    kputsSecure("\n");
    
    kputsSecure("[KERNEL] ");
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
    
    // 获取导致页错误的线性地址
    uint64_t fault_addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
    
    kputsSecure("[EXCEPTION] some exception happened: Page Fault (#PF)\n");
    kputsSecure("Error code: ");
    kpnumSecure(&error_code, UNHEX, sizeof(error_code));
    kputsSecure("\n");
    kputsSecure("Fault address: 0x");
    kpnumSecure(&fault_addr, UNHEX, sizeof(fault_addr));
    kputsSecure("\n");
    
    if(frame->cs&0x3){
        kputsSecure("[USER] ");
        //用户态异常有待处理
        return;
    }else{
        kputsSecure("[KERNEL] ");
        KernelPanicManager::panic("Page Fault", regs);
    }
}

// 无效TSS异常 (#TS)
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