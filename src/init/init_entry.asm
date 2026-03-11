bits 64

extern bsp_init_gdt_descriptor
extern bsp_init_idtr
extern bsp_init_idt_entries
extern __init_stack_end
extern init
extern div_by_zero_bare_enter
extern breakpoint_bare_enter
extern nmi_bare_enter
extern overflow_bare_enter
extern invalid_opcode_bare_enter
extern general_protection_bare_enter
extern double_fault_bare_enter
extern page_fault_bare_enter
extern machine_check_bare_enter
extern invalid_tss_bare_enter
extern simd_floating_point_bare_enter
extern virtualization_bare_enter
global _init_entry
global shift_kernel
section .text

%macro SET_IDT_OFFSET 2
    mov rax, %2
    mov word [rbx + %1*16 + 0], ax
    shr rax, 16
    mov word [rbx + %1*16 + 6], ax
    shr rax, 16
    mov dword [rbx + %1*16 + 8], eax
    mov dword [rbx + %1*16 + 12], 0
%endmacro

shift_kernel:
    mov rax, [rdi+0x20]
    mov cr3, rax
    mov rsp, rsi
    call rdx

_init_entry:
    lea rax, [rel bsp_init_gdt_descriptor]
    lgdt [rax]

    lea rbx, [rel bsp_init_idt_entries]
    SET_IDT_OFFSET 0, div_by_zero_bare_enter
    SET_IDT_OFFSET 2, nmi_bare_enter
    SET_IDT_OFFSET 3, breakpoint_bare_enter
    SET_IDT_OFFSET 4, overflow_bare_enter
    SET_IDT_OFFSET 6, invalid_opcode_bare_enter
    SET_IDT_OFFSET 8, double_fault_bare_enter
    SET_IDT_OFFSET 10, invalid_tss_bare_enter
    SET_IDT_OFFSET 13, general_protection_bare_enter
    SET_IDT_OFFSET 14, page_fault_bare_enter
    SET_IDT_OFFSET 18, machine_check_bare_enter
    SET_IDT_OFFSET 19, simd_floating_point_bare_enter
    SET_IDT_OFFSET 20, virtualization_bare_enter

    lea rax, [rel bsp_init_idtr]
    lidt [rax]

    lea rsp, [rel __init_stack_end]
    call init

init_hang:
    hlt
    jmp init_hang
