section .text

%define GP_GPR_COUNT 16
%define GP_GPR_BYTES (GP_GPR_COUNT * 8)
%define INTERRUPT_CONTEXT_SPECIFY_NO_MAGIC 0x8000000000000000
%define INTERRUPT_CONTEXT_SPECIFY_MAGIC    0x8000000000000001

%macro PUSH_GPRS 0
    push rbp
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
%endmacro

%macro POP_GPRS 0
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
    pop rbp
%endmacro

%macro CALL_CPP 1
    mov rdi, rsp
    mov rax, rsp
    and rsp, -16
    sub rsp, 8
    push rax
    mov rax, %1
    call rax
    pop rsp
    add rsp, 8
%endmacro

%macro EXCEPTION_NOERR 3
global %1
extern %2
%1:
    sub rsp, 8
    PUSH_GPRS
    mov rax, %3
    push rax
    mov rax, rsp
    add rax, GP_GPR_BYTES
    mov rbx, INTERRUPT_CONTEXT_SPECIFY_NO_MAGIC
    mov qword [rax], rbx
    CALL_CPP %2
    POP_GPRS
    add rsp, 8
    iretq
%endmacro

%macro EXCEPTION_ERR 3
global %1
extern %2
%1:
    sub rsp, 8
    PUSH_GPRS
    mov rax, %3
    push rax
    mov rax, rsp
    add rax, GP_GPR_BYTES
    mov rbx, INTERRUPT_CONTEXT_SPECIFY_MAGIC
    mov qword [rax], rbx
    CALL_CPP %2
    add rsp, 16
    POP_GPRS
    add rsp, 8
    iretq
%endmacro

EXCEPTION_NOERR div_by_zero_bare_enter, div_by_zero_cpp_enter, 0x0
EXCEPTION_NOERR breakpoint_bare_enter, breakpoint_cpp_enter, 0x3
EXCEPTION_NOERR nmi_bare_enter, nmi_cpp_enter, 0x2
EXCEPTION_NOERR overflow_bare_enter, overflow_cpp_enter, 0x4
EXCEPTION_NOERR invalid_opcode_bare_enter, invalid_opcode_cpp_enter, 0x6
EXCEPTION_ERR general_protection_bare_enter, general_protection_cpp_enter, (0x0D | (1 << 32))
EXCEPTION_ERR double_fault_bare_enter, double_fault_cpp_enter, (0x08 | (1 << 32))
EXCEPTION_ERR page_fault_bare_enter, page_fault_cpp_enter, (0x0E | (1 << 32))
EXCEPTION_NOERR machine_check_bare_enter, machine_check_cpp_enter, 0x12
EXCEPTION_ERR invalid_tss_bare_enter, invalid_tss_cpp_enter, (0x0A | (1 << 32))
EXCEPTION_NOERR simd_floating_point_bare_enter, simd_floating_point_cpp_enter, 0x13
EXCEPTION_ERR virtualization_bare_enter, virtualization_cpp_enter, (0x14 | (1 << 32))
