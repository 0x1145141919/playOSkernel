global realmode_interrupt_handlers
global idt_descriptor_rm
global idt_table_rm
%include "checkpoint.inc"
%define realmode_magic 0x15
extern realmode_enter_checkpoint
SECTION .boottext
realmode_interrupt_handlers:
bits 16
    .divide_by_zero:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax ; 只保留低16位，尤其是PE位
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 0
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .debug:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 1
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .nmi:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 2
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .bp:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 3
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .of:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 4
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .br:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 5
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .ud:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 6
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .nm:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 7
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .df:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 8
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .cross:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 9
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .tss:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 10
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .NP:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 11
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .SS:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 12
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .GP:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 13
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .PF:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 14
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .vec15:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 15
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .MF:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 16
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .AC:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 17
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .MC:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 18
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .XM:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 19
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .VE:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 20
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt
    
    .CP:
    pusha
    push es
    push ds
    push ss
    push fs
    push gs
    mov cr0, eax
    push ax
    push realmode_magic
    mov word [realmode_enter_checkpoint.failure_final_stack_top], sp
    mov byte [realmode_enter_checkpoint.failure_caused_excption_num], 21
    sfence
    mov byte [realmode_enter_checkpoint.failure_flags], 3
    hlt

SECTION .init_rodata
; IDT定义 - 实模式16位下的前32个向量（实际上实模式使用向量跳转表，此处为模拟结构）
idt_table_rm:
    ; 实模式下实际不使用IDT，而是使用向量跳转表
    ; 但为了保持一致性，这里仍定义类似结构
    ; 每个条目4字节: 偏移量(2字节) + 段地址(2字节)
    dw realmode_interrupt_handlers.divide_by_zero     ; Vector 0 - Divide by Zero
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.debug             ; Vector 1 - Debug
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.nmi               ; Vector 2 - NMI
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.bp                ; Vector 3 - Breakpoint
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    e
    dw realmode_interrupt_handlers.of                ; Vector 4 - Overflow
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.br                ; Vector 5 - Bound Range
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.ud                ; Vector 6 - Invalid Opcode
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.nm                ; Vector 7 - Device Not Available
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.df                ; Vector 8 - Double Fault
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.cross             ; Vector 9 - Coprocessor Segment Overrun
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.tss               ; Vector 10 - Invalid TSS
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.NP                ; Vector 11 - Segment Not Present
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.SS                ; Vector 12 - Stack Segment Fault
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.GP                ; Vector 13 - General Protection
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.PF                ; Vector 14 - Page Fault
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.vec15             ; Vector 15 - Reserved
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.MF                ; Vector 16 - x87 FPU Floating Point Error
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.AC                ; Vector 17 - Alignment Check
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.MC                ; Vector 18 - Machine Check
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.XM                ; Vector 19 - SIMD Floating Point Exception
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.VE                ; Vector 20 - Virtualization Exception
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)
    
    dw realmode_interrupt_handlers.CP                ; Vector 21 - Control Protection Exception
    dw 0x0000                                         ; Code segment selector for real mode (实模式下CS必须为0)

; 实模式IDT描述符（实际上实模式使用中断向量表IVT，位于内存地址0x0000:0x0000-0x0000:0x03FF）
idt_descriptor_rm:
    dw $ - idt_table_rm - 1     ; Limit (size of IDT - 1)
    dd idt_table_rm             ; Base address of IDT (simulated for real mode)
