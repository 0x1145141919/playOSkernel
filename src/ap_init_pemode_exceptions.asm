global idt_descriptor_pe
global idt_table_pe
global pe_interrupt_vector
%define PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC  0x11
%define PE_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC  0x12
%include "checkpoint.inc"
extern pemode_enter_checkpoint
SECTION .init_rodata
; IDT定义 - 保护模式32位下的前32个向量
idt_table_pe:
    ; IDT条目格式 (每个条目8字节):
    ; 偏移[0:1] - 偏移地址的低16位
    ; 段选择子[2:3] - 代码段选择子
    ; 保留位[4] - 必须为0
    ; 类型和属性[5] - 门类型描述符
    ; 偏移[6:7] - 偏移地址的高16位
    dw pe_interrupt_handlers.divide_by_zero     ; Vector 0 - Divide by Zero
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0  ; Offset high bits
    
    dw pe_interrupt_handlers.debug              ; Vector 1 - Debug
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0       ; Offset high bits
    
    dw pe_interrupt_handlers.nmi                ; Vector 2 - NMI
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0         ; Offset high bits
    
    dw pe_interrupt_handlers.bp                 ; Vector 3 - Breakpoint
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0          ; Offset high bits
    
    dw pe_interrupt_handlers.of                 ; Vector 4 - Overflow
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0        ; Offset high bits
    
    dw pe_interrupt_handlers.br                 ; Vector 5 - Bound Range
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0          ; Offset high bits
    
    dw pe_interrupt_handlers.ud                 ; Vector 6 - Invalid Opcode
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0          ; Offset high bits
    
    dw pe_interrupt_handlers.nm                 ; Vector 7 - Device Not Available
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0          ; Offset high bits
    
    dw pe_interrupt_handlers.df                 ; Vector 8 - Double Fault
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0          ; Offset high bits
    
    dw pe_interrupt_handlers.cross              ; Vector 9 - Coprocessor Segment Overrun
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0       ; Offset high bits
    
    dw pe_interrupt_handlers.tss                ; Vector 10 - Invalid TSS
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0         ; Offset high bits
    
    dw pe_interrupt_handlers.NP                 ; Vector 11 - Segment Not Present
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0          ; Offset high bits
    
    dw pe_interrupt_handlers.SS                 ; Vector 12 - Stack Segment Fault
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0          ; Offset high bits
    
    dw pe_interrupt_handlers.GP                 ; Vector 13 - General Protection
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0          ; Offset high bits
    
    dw pe_interrupt_handlers.PF                 ; Vector 14 - Page Fault
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0          ; Offset high bits
    
    dw pe_interrupt_handlers.vec15              ; Vector 15 - Reserved
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0       ; Offset high bits
    
    dw pe_interrupt_handlers.MF                 ; Vector 16 - x87 FPU Floating Point Error
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0          ; Offset high bits
    
    dw pe_interrupt_handlers.AC                 ; Vector 17 - Alignment Check
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0          ; Offset high bits
    
    dw pe_interrupt_handlers.MC                 ; Vector 18 - Machine Check
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0          ; Offset high bits
    
    dw pe_interrupt_handlers.XM                 ; Vector 19 - SIMD Floating Point Exception
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0          ; Offset high bits
    
    dw pe_interrupt_handlers.VE                 ; Vector 20 - Virtualization Exception
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0       ; Offset high bits
    
    dw pe_interrupt_handlers.CP                 ; Vector 21 - Control Protection Exception
    dw 0x18                                          ; Code segment selector
    dw 0x8E00                                        ; Type and attributes (Interrupt Gate)
    dw 0          ; Offset high bits
; IDT描述符
idt_descriptor_pe:
    dw $ - idt_table_pe - 1     ; Limit (size of IDT - 1)
    dd idt_table_pe             ; Base address of IDT
SECTION .boottext
pe_interrupt_handlers:
bits 32
    .divide_by_zero:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 0
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .debug:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 1
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .nmi:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 2
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .bp:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 3
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .of:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 4
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .br:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 5
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .ud:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 6
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .nm:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 7
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .df:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, 8               ; 将中断向量号加载到eax
    push eax                 ; 保存vector num到栈上
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 8
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .cross:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 9
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .tss:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, 10              ; 将中断向量号加载到eax
    push eax                 ; 保存vector num到栈上
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 10
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .NP:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, 11              ; 将中断向量号加载到eax
    push eax                 ; 保存vector num到栈上
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 11
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .SS:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, 12              ; 将中断向量号加载到eax
    push eax                 ; 保存vector num到栈上
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 12
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .GP:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, 13              ; 将中断向量号加载到eax
    push eax                 ; 保存vector num到栈上
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 13
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .PF:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, 14              ; 将中断向量号加载到eax
    push eax                 ; 保存vector num到栈上
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 14
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .vec15:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 15
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .MF:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 16
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .AC:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, 17              ; 将中断向量号加载到eax
    push eax                 ; 保存vector num到栈上
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 17
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .MC:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 18
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .XM:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 19
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .VE:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 20
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机
    
    .CP:
    pushad                   ; 保存所有32位寄存器
    push es
    push ds
    push ss
    push fs
    push gs
    mov eax, 21              ; 将中断向量号加载到eax
    push eax                 ; 保存vector num到栈上
    mov eax, cr0
    push eax
    mov eax, cr2
    push eax
    mov eax, cr3
    push eax
    mov eax, cr4
    push eax
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov eax, PE_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push eax
    mov byte [pemode_enter_checkpoint+check_point.failure_caused_excption_num], 21
    mov word [pemode_enter_checkpoint+check_point.failure_final_stack_top], sp
    sfence                   ; 确保内存写入对其他核心可见
    mov byte [pemode_enter_checkpoint+check_point.failure_flags], 0x3
    hlt                      ; 停机