section .text
global resources_shift
extern bsp_init_idtr
extern bsp_init_gdt_descriptor
extern pml4_table_init
extern pml5_table_init
%define K_cs_idx 8
resources_shift:
    sub rsp, 8
    mov rax, pml4_table_init
    mov cr3, rax
    lgdt [bsp_init_gdt_descriptor]
    mov rax, K_cs_idx
    push rax
    lea rax, [.load_cs_finish]
    push rax
    retfq
.load_cs_finish:
    lidt [bsp_init_idtr]
    mov ds, 16
    mov es, 16
    mov ss, 16
    mov fs, 16
    mov gs, 16
    add rsp, 8
    ret
    