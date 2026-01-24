#include "Interrupt_system/Interrupt.h"
#include "util/kout.h"
#include "util/OS_utils.h"
#include "panic.h"
#include "pt_regs.h"
#include "util/kptrace.h"
void (*global_ipi_handler)()=nullptr;
void div_by_zero_cpp_enter(x64_Interrupt_saved_context_no_errcode *frame)
{
    Interrupt_context::x64_context_no_errcode*context=(Interrupt_context::x64_context_no_errcode*)(frame);
    if(context->self_specify_magic!=0){
        KernelPanicManager::policy_panic("div_by_zero_cpp_enter bad magic");
    }
    if((context->cs&0x3)==0x3){//用户态，根据

    }else if((context->cs&0x3)==0x0){//内核态,panic
        panic_context::x64_context panic_context=KernelPanicManager::convert_to_panic_context(frame);
        KernelPanicManager::hard_panic("kernel_context cause #DE", &panic_context);
    }
}

void debug_cpp_enter(x64_Interrupt_saved_context_no_errcode *frame)
{

}

void nmi_cpp_enter(x64_Interrupt_saved_context_no_errcode *frame)
{

}

void breakpoint_cpp_enter(x64_Interrupt_saved_context_no_errcode *frame)
{

}
void overflow_cpp_enter(x64_Interrupt_saved_context_no_errcode *frame)
{
    Interrupt_context::x64_context_no_errcode* context = (Interrupt_context::x64_context_no_errcode*)(frame);
    if(context->self_specify_magic != 0x4){
        KernelPanicManager::policy_panic("overflow_cpp_enter bad magic");
    }
    if((context->cs & 0x3) == 0x3){
        // 用户态，根据
    } else if((context->cs & 0x3) == 0x0){
        // 内核态, panic
        panic_context::x64_context panic_context = KernelPanicManager::convert_to_panic_context(frame);
        KernelPanicManager::hard_panic("kernel_context cause #OF(Overflow)", &panic_context);
    }
}

void invalid_opcode_cpp_enter(x64_Interrupt_saved_context_no_errcode *frame)
{
    Interrupt_context::x64_context_no_errcode* context = (Interrupt_context::x64_context_no_errcode*)(frame);
    if(context->self_specify_magic != 0x6){
        KernelPanicManager::policy_panic("invalid_opcode_cpp_enter bad magic");
    }
    if((context->cs & 0x3) == 0x3){
        // 用户态，根据
    } else if((context->cs & 0x3) == 0x0){
        // 内核态, panic
        panic_context::x64_context panic_context = KernelPanicManager::convert_to_panic_context(frame);
        KernelPanicManager::hard_panic("kernel_context cause #UD(Invalid Opcode)", &panic_context);
    }
}

void double_fault_cpp_enter(x64_Interrupt_saved_context *frame)
{
    if(frame->self_specify_magic != (0x8 | (1ULL << 32))){
        KernelPanicManager::policy_panic("double_fault_cpp_enter bad magic");
    }
    panic_context::x64_context panic_context = KernelPanicManager::convert_to_panic_context(frame, 8);
    KernelPanicManager::hard_panic("kernel_context cause #DF(Double Fault)", &panic_context);
}

void invalid_tss_cpp_enter(x64_Interrupt_saved_context *frame)
{
    if(frame->self_specify_magic != (0xA | (1ULL << 32))){
        KernelPanicManager::policy_panic("invalid_tss_cpp_enter bad magic");
    }
    if((frame->cs & 0x3) == 0x3){
        // 用户态，根据
    } else if((frame->cs & 0x3) == 0x0){
        // 内核态, panic
        panic_context::x64_context panic_context = KernelPanicManager::convert_to_panic_context(frame, 0xA);
        KernelPanicManager::hard_panic("kernel_context cause #TS(Invalid TSS)", &panic_context);
    }
}

void general_protection_cpp_enter(x64_Interrupt_saved_context *frame)
{
    if(frame->self_specify_magic != (0xD | (1ULL << 32))){
        KernelPanicManager::policy_panic("general_protection_cpp_enter bad magic");
    }
    if((frame->cs & 0x3) == 0x3){
        // 用户态，根据
    } else if((frame->cs & 0x3) == 0x0){
        // 内核态, panic
        panic_context::x64_context panic_context = KernelPanicManager::convert_to_panic_context(frame, 0xD);
        KernelPanicManager::hard_panic("kernel_context cause #GP(General Protection)", &panic_context);
    }
}

void page_fault_cpp_enter(x64_Interrupt_saved_context *frame)
{
    if(frame->self_specify_magic != (0xE | (1ULL << 32))){
        KernelPanicManager::policy_panic("page_fault_cpp_enter bad magic");
    }
    if((frame->cs & 0x3) == 0x3){
        // 用户态，根据
    } else if((frame->cs & 0x3) == 0x0){
        // 内核态, panic
        panic_context::x64_context panic_context = KernelPanicManager::convert_to_panic_context(frame, 0xE);
        KernelPanicManager::hard_panic("kernel_context cause #PF(Page Fault)", &panic_context);
    }
}

void machine_check_cpp_enter(x64_Interrupt_saved_context_no_errcode *frame)
{
    if(frame->self_specify_magic != (0x12 | (1ULL << 32))){
        KernelPanicManager::policy_panic("machine_check_cpp_enter bad magic");
    }
    if((frame->cs & 0x3) == 0x3){
        // 用户态，根据
    } else if((frame->cs & 0x3) == 0x0){
        // 内核态, panic
        panic_context::x64_context panic_context = KernelPanicManager::convert_to_panic_context(frame);
        KernelPanicManager::hard_panic("kernel_context cause #MC(Machine Check)", &panic_context);
    }
}

void simd_floating_point_cpp_enter(x64_Interrupt_saved_context_no_errcode *frame)
{
    if(frame->self_specify_magic != (0x13 | (1ULL << 32))){
        KernelPanicManager::policy_panic("simd_floating_point_cpp_enter bad magic");
    }
    if((frame->cs & 0x3) == 0x3){
        // 用户态，根据
    } else if((frame->cs & 0x3) == 0x0){
        // 内核态, panic
        panic_context::x64_context panic_context = KernelPanicManager::convert_to_panic_context(frame);
        KernelPanicManager::hard_panic("kernel_context cause #XM(SIMD Floating Point)", &panic_context);
    }
}

void virtualization_cpp_enter(x64_Interrupt_saved_context_no_errcode *frame)
{

}

void Control_Protection_cpp_enter(x64_Interrupt_saved_context *frame)
{
    
}

void timer_cpp_enter(x64_Interrupt_saved_context_no_errcode *frame)
{

}

void ipi_cpp_enter(x64_Interrupt_saved_context_no_errcode *frame)
{

}

void asm_panic_cpp_enter(x64_Interrupt_saved_context_no_errcode *frame)
{
    panic_context::x64_context panic_context=KernelPanicManager::convert_to_panic_context(frame);
    KernelPanicManager::asm_panic(&KernelPanicManager::convert_to_panic_context(frame));
}
