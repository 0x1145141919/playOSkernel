#include <stdint.h>
#include "fixed_interrupt_vectors.h"
extern void (*global_ipi_handler)();
/**
 * 中断管理器，管理着每个cpu的中断描述符表和本地apic
 * 当然，调用时必须上报其apic__id
 */
 // namespace gdtentry
struct interrupt_frame {
    uint64_t rip;    // 指令指针
    uint64_t cs;     // 代码段选择子
    uint64_t rflags; // CPU标志
    uint64_t rsp;    // 栈指针（仅特权级变化时压入）
    uint64_t ss;     // 栈段选择子（仅特权级变化时压入）
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
};
__attribute__((interrupt)) void exception_handler_div_by_zero(interrupt_frame* frame);
__attribute__((interrupt)) void exception_handler_breakpoint(interrupt_frame* frame);
__attribute__((interrupt)) void exception_handler_nmi(interrupt_frame* frame);
__attribute__((interrupt)) void exception_handler_breakpoint(interrupt_frame* frame);
__attribute__((interrupt)) void exception_handler_overflow(interrupt_frame* frame);
__attribute__((interrupt)) void exception_handler_invalid_opcode(interrupt_frame* frame);        // #UD
__attribute__((interrupt)) void exception_handler_general_protection(interrupt_frame* frame, uint64_t error_code); // #GP
__attribute__((interrupt)) void exception_handler_double_fault(interrupt_frame* frame, uint64_t error_code);       // #DF
__attribute__((interrupt)) void exception_handler_page_fault(interrupt_frame* frame, uint64_t error_code);         // #PF
__attribute__((interrupt)) void exception_handler_machine_check(interrupt_frame* frame);        // #MC
__attribute__((interrupt)) void exception_handler_invalid_tss(interrupt_frame* frame, uint64_t error_code);        // #TS
__attribute__((interrupt)) void exception_handler_simd_floating_point(interrupt_frame* frame);    // #XM
__attribute__((interrupt)) void exception_handler_virtualization(interrupt_frame* frame, uint64_t error_code);     // #VE
__attribute__((interrupt)) void timer_interrupt(interrupt_frame* frame);
__attribute__((interrupt)) void IPI(interrupt_frame* frame, uint64_t error_code);



