global longmode_interrupt_handlers
global idt_table_lm
global idt_descriptor_lm
%include "checkpoint.inc"
%define LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC  0x13
%define LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC  0x14
extern longmode_enter_checkpoint
; IDT定义 - 长模式64位下的前32个向量
SECTION .boottext
; 长模式下的中断处理例程标签
longmode_interrupt_handlers:
bits 64
    .divide_by_zero:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 0
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .debug:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 1
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .nmi:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 2
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .bp:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 3
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .of:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 4
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .br:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 5
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .ud:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 6
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .nm:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 7
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .df:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 8
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .cross:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 9
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .tss:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 10
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .NP:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 11
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .SS:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 12
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .GP:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 13
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .PF:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 14
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .vec15:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 15
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .MF:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 16
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .AC:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 17
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .MC:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 18
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .XM:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 19
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .VE:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 20
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt
    
    .CP:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rax, cr0
    push rax
    mov rax, cr2
    push rax
    mov rax, cr3
    push rax
    mov rax, cr4
    push rax
    xor rax, rax
    mov ax, es
    push rax
    mov ax, ds
    push rax
    mov ax, ss
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    mov ecx, 0xC0000080
    rdmsr
    push rax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC
    push rax
    
    ; 使用通用寄存器 + 64位绝对寻址
    mov rbx, qword longmode_enter_checkpoint  ; 加载64位绝对地址到寄存器
    mov word [rbx + check_point.failure_final_stack_top], sp
    mov byte [rbx + check_point.failure_caused_excption_num], 21
    sfence
    mov byte [rbx + check_point.failure_flags], 0x3
    hlt

SECTION .init_rodata
idt_table_lm:
    ; IDT条目格式 (每个条目16字节):
    ; 偏移[0:1] - 偏移地址的低16位
    ; 段选择子[2:3] - 代码段选择子
    ; IST[7:3] - 中断栈表索引
    ; 类型和属性[4] - 保留
    ; 类型和属性[5] - 门类型描述符
    ; IST[7:4] - 中断栈表高4位
    ; 偏移[6:7] - 偏移地址的中间16位
    ; 偏移[8:11] - 偏移地址的高32位
    ; 保留[12:15] - 必须为0
    dw longmode_interrupt_handlers.divide_by_zero     ; Vector 0 - Divide by Zero
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.debug             ; Vector 1 - Debug
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.nmi               ; Vector 2 - NMI
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.bp                ; Vector 3 - Breakpoint
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.of                ; Vector 4 - Overflow
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.br                ; Vector 5 - Bound Range
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.ud                ; Vector 6 - Invalid Opcode
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.nm                ; Vector 7 - Device Not Available
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.df                ; Vector 8 - Double Fault
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.cross             ; Vector 9 - Coprocessor Segment Overrun
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.tss               ; Vector 10 - Invalid TSS
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.NP                ; Vector 11 - Segment Not Present
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.SS                ; Vector 12 - Stack Segment Fault
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.GP                ; Vector 13 - General Protection
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.PF                ; Vector 14 - Page Fault
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.vec15             ; Vector 15 - Reserved
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.MF                ; Vector 16 - x87 FPU Floating Point Error
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.AC                ; Vector 17 - Alignment Check
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.MC                ; Vector 18 - Machine Check
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.XM                ; Vector 19 - SIMD Floating Point Exception
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.VE                ; Vector 20 - Virtualization Exception
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved
    
    dw longmode_interrupt_handlers.CP                ; Vector 21 - Control Protection Exception
    dw 0x18                                          ; Code segment selector
    db 0                                             ; IST and reserved
    db 0x8E                                          ; Type and attributes (Interrupt Gate)
    dw 0                                             ; High part of offset (bits 31:16)
    dd 0                                             ; Highest part of offset (bits 63:32)
    dd 0                                             ; Reserved

; 长模式IDT描述符
idt_descriptor_lm:
    dw $ - idt_table_lm - 1     ; Limit (size of IDT - 1)
    dq idt_table_lm             ; Base address of IDT (64-bit)
