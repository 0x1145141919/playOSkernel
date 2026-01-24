; 物理地址类型 (MAXPHYADDR ≤ 52)
%define phys_addr_t dq

; 页大小数组
%define PAGE_SIZE_IN_LV_0 (1 << 12)    ; NORMAL_4kb_MASK_OFFSET
%define PAGE_SIZE_IN_LV_1 (1 << 21)    ; NORMAL_2MB_MASK_OFFSET
%define PAGE_SIZE_IN_LV_2 (1 << 30)    ; NORMAL_1GB_MASK_OFFSET
%define PAGE_SIZE_IN_LV_3 (1 << 39)    ; NORMAL_512GB_MASK_OFFSET
%define PAGE_SIZE_IN_LV_4 (1 << 48)    ; NORMAL_256TB_MASK_OFFSET

; 页偏移掩码数组
%define PAGE_OFFSET_MASK_0 ((1 << 12) - 1)   ; NORMAL_4kb_MASK_OFFSET
%define PAGE_OFFSET_MASK_1 ((1 << 21) - 1)   ; NORMAL_2MB_MASK_OFFSET
%define PAGE_OFFSET_MASK_2 ((1 << 30) - 1)   ; NORMAL_1GB_MASK_OFFSET
%define PAGE_OFFSET_MASK_3 ((1 << 39) - 1)   ; NORMAL_512GB_MASK_OFFSET
%define PAGE_OFFSET_MASK_4 ((1 << 48) - 1)   ; NORMAL_256TB_MASK_OFFSET


; 物理地址宽度（假设为52位）
%define PHYS_ADDR_WIDTH 52
%define PHYS_ADDR_MASK (1 << PHYS_ADDR_WIDTH) - 1

; 页表项位字段偏移和掩码
%define P_BIT    0   ; Present 位偏移
%define RW_BIT   1   ; Read/Write 位偏移
%define US_BIT   2   ; User/Supervisor 位偏移
%define PS_BIT  7
; 通用掩码
%define P_MASK   (1 << P_BIT)
%define RW_MASK  (1 << RW_BIT)
%define US_MASK  (1 << US_BIT)
%define PS_MASK  (1 << PS_BIT)
%define PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC  0x11
%define PE_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC  0x12
%define LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC  0x13
%define LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC  0x14
extern KImgphybase 
extern _stack_top
extern kernel_start
extern ap_init
extern bsp_init_gdt_entries
extern bsp_init_gdt_descriptor
extern bsp_init_idt_entries
extern bsp_init_idtr
global _kernel_Init
global pml4_table_init
global pml5_table_init
global idt_descriptor_pe
global idt_table_pe
global pe_interrupt_vector
global AP_realmode_start
global assigned_processor_id
SECTION .bootdata
; 按照头文件定义的check_point结构体格式
struc check_point
    .success_word:        resd 1   ; uint32_t success_word
    .failure_flags:       resb 1   ; uint8_t failure_flags
    .failure_caused_excption_num: resb 1   ; uint8_t failure_caused_excption_num
    .failure_final_stack_top:     resw 1   ; uint16_t failure_final_stack_top
    .check_point_id:      resw 1   ; uint16_t check_point_id
    .reserved:            resw 3   ; uint16_t reserved[3]
endstruc

; 定义两个check_point结构体
realmode_enter_checkpoint:
    istruc check_point
        at check_point.success_word, dd 0
        at check_point.failure_flags, db 0
        at check_point.failure_caused_excption_num, db 0
        at check_point.failure_final_stack_top, dw 0
        at check_point.check_point_id, dw 0
        at check_point.reserved, dw 0, 0, 0
    iend

pemode_enter_checkpoint:
    istruc check_point
        at check_point.success_word, dd 0
        at check_point.failure_flags, db 0
        at check_point.failure_caused_excption_num, db 0
        at check_point.failure_final_stack_top, dw 0
        at check_point.check_point_id, dw 0
        at check_point.reserved, dw 0, 0, 0
    iend
longmode_enter_checkpoint:
    istruc check_point
        at check_point.success_word, dd 0
        at check_point.failure_flags, db 0
        at check_point.failure_caused_excption_num, db 0
        at check_point.failure_final_stack_top, dw 0
        at check_point.check_point_id, dw 0
        at check_point.reserved, dw 0, 0, 0
    iend
assigned_processor_id:
    dd 0
SECTION .boottext

AP_realmode_start:
.ap_start:
bits 16
cli
mov eax, [assigned_processor_id]
mov [enter_realmode_word], eax

    ; 加载GDT
    lgdt [gdt_descriptor]

    ; 设置CR0寄存器，启用保护模式
    mov eax, cr0
    or eax, 1                           ; Set PE (Protection Enable) bit
    
    mov cr0, eax
    
    ; 长跳转到保护模式代码段
    jmp 0x8:.ap_start_pe               ; 使用代码段选择子0x08 (代码段在GDT中的偏移)

.ap_start_pe:
bits 32
    ; 设置数据段寄存器
    mov ax, 0x10                        ; 数据段选择子0x10 (数据段在GDT中的偏移)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, init_stack_end
    mov eax, 0xB
    mov ecx, 1
    cpuid
    mov [enter_pemode_word], edx
    lidt [idt_descriptor_pe]
    mov cr0, eax
    mov eax, cr4
    or eax, 1<<5
    and eax, ~(1 << 12)
    and eax, ~(1 << 4)
    mov cr4, eax
    mov eax, pml4_table_init
    mov cr3, eax
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1<<11)|(1<<8)|1
    wrmsr
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax
    jmp 0x18:.ap_start_64          ; 使用代码段选择子0x08 (代码段在GDT中的偏移)
; 32个中断处理例程

.ap_start_64:
bits 64
    mov rdi, [assigned_processor_id]
    mov rax, ap_init
    mov rsp, init_stack_end
    call rax
    ; 添加跳转指令，防止执行下面的数据部分
    
    
    
_kernel_Init:
.skip_x2apic:
%ifdef PG5LV_ENABLE
; 检查是否支持五级分页
    mov eax, 7           ; CPUID 功能 7
    xor ecx, ecx         ; 子功能 0
    cpuid
    test ecx, (1 << 16)  ; 检查 LA57 位 (bit 16)
    jz .use_4level       ; 不支持则跳转到四级分页
    
    ; 支持五级分页，准备开启
    ; 1. 切换到兼容性 CR3 (四级页表结构)
    mov rax, pml5_table_pml4_low  ; 兼容性四级页表
    mov cr3, rax
    
    ; 2. 启用 CR4.LA57
    mov rax, cr4
    or rax, (1 << 12)   ; 设置 LA57 位 (bit 12)
    mov cr4, rax
    
    ; 3. 设置真正的五级页表 CR3
    mov rax, pml5_table
    add rax,3
    mov cr3, rax
    
    jmp .paging_done

%else
    ; 使用四级分页
    mov rax, pml4_table_init
    mov cr3, rax
%endif

.use_4level:
    ; 四级分页继续执行
    mov rax, pml4_table_init
    mov cr3, rax

.paging_done:
    ; 恢复寄存器
    mov rsp,_stack_top
    mov ecx, 0xC0000080
    rdmsr
    or eax, (1<<11)|(1<<8)|1
    wrmsr
    mov eax, cr0
    or eax, (1<<16)
    mov cr0, eax
    lgdt [bsp_init_gdt_descriptor]
    push 0x8
    mov rax, .jump_kernel
    push rax
    retfq
.jump_kernel:
    lidt [bsp_init_idtr]
    mov rax, kernel_start
    call  rax
    hlt

SECTION .init_rodata
align 0x1000
_32_bit_PD:
dd 3+(1<<7)
times 1023 dd 0
pml4_table_init:
dq pml4_table_pdpt_lowmem_rigon+0x3
times 255 dq 0
dq pml4_table_pdpt_lowmem_rigon+0x3
times 255 dq 0
pml4_table_pdpt_lowmem_rigon:
%assign i 0
%rep 512
dq i*PAGE_SIZE_IN_LV_2+3+PS_MASK
%assign i  i+1
%endrep
%undef i
pml5_table_init:
dq pml5_table_pml4_low+0x3
times 255 dq 0
dq pml5_table_pml4_low+0x3
times 255 dq 0
pml5_table_pml4_low:
dq pml4_table_pdpt_lowmem_rigon+0x3
times 511 dq 0
; GDT定义
gdt_start:
    dq 0                                ; Nullwan quan bu yi yang descriptor
gdt_code:
    dw 0xFFFF                           ; Limit
    dw 0                                ; Base (low)
    db 0                                ; Base (middle)
    db 10011010b                        ; Access byte (Present, Ring 0, Code, Exec Read)
    db 11001111b                        ; Flags (Granularity 4KB, 32-bit mode)
    db 0                                ; Base (high)
gdt_data:
    dw 0xFFFF                           ; Limit
    dw 0                                ; Base (low)
    db 0                                ; Base (middle)
    db 10010010b                        ; Access byte (Present, Ring 0, Data, Read Write)
    db 11001111b                        ; Flags (Granularity 4KB, 32-bit mode)
    db 0                                ; Base (high)
gdt_longmode_code:
    dw 0xFFFF                           ; Limit
    dw 0                                ; Base (low)
    db 0                                ; Base (middle)
    db 10011000b                        ; Access byte (Present, Ring 0, Code, Exec Read)
    db 10101111b                        ; Flags (Granularity 4KB, 64-bit mode)
    db 0  
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1          ; GDT limit
    dd gdt_start                        ; GDT base address
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 0
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 1
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 2
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 3
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
    hlt                      ; 停机
    ; 每个中断处理例程的标签
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 4
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 5
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 6
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 7
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 8
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 9
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
    hlt                      ; 停机
    ; 每个中断处理例程的标签
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 10
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
    hlt                      ; 停机
    ; 每个中断处理例程的标签
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 11
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
    hlt                      ; 停机
    ; 每个中断处理例程的标签
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 12
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
    hlt                      ; 停机
    ; 每个中断处理例程的标签
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 13
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
    hlt                      ; 停机
    ; 每个中断处理例程的标签
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 14
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
    hlt                      ; 停机
    ; 每个中断处理例程的标签
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 15
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
    hlt                      ; 停机
    ; 每个中断处理例程的标签
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 16
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
    hlt                      ; 停机
    ; 每个中断处理例程的标签
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 17
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
    hlt                      ; 停机
    ; 每个中断处理例程的标签
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 18
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
    hlt                      ; 停机
    ; 每个中断处理例程的标签
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 19
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
    hlt                      ; 停机
    ; 每个中断处理例程的标签
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 20
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
    hlt                      ; 停机
    ; 每个中断处理例程的标签
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
    mov byte [pemode_enter_checkpoint.failure_caused_excption_num], 21
    mov word [pemode_enter_checkpoint.failure_final_stack_top], esp
    mov byte [pemode_enter_checkpoint.failure_flags], 0x3
    hlt                      ; 停机
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],0 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    hlt
    .debug:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],1 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    hlt
    .nmi:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],2 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    hlt
    .bp:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],3 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    hlt
    .of:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],4 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    hlt
    .br:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],5 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    .ud:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],6 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    .nm:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],7 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    .df:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],8 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    .cross:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],9 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    .tss:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],10 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    .NP:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],11 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    .SS:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],12 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    .GP:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],13 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    .PF:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],14 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    .vec15:
    ; 中断处理代码将在实现时填充
    ; 每个中断处理例程的标签
    .MF:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],16 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    .AC:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],17 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    .MC:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],18 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    .XM:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],19 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    .VE:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],20 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3
    .CP:
    push rax
    push rbx
    push rcx
    push rdx
    push rbp
    push rsi
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
    push es
    push ds
    push ss
    push fs
    push gs
    mov ecx, 0xC0000080
    rdmsr
    push eax
    mov rax, LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC ;看前面的宏定义，有错误码和没错误码推入不同的魔数
    push rax
    mov word [longmode_enter_checkpoint.failure_final_stack_top],rsp
    mov byte [longmode_enter_checkpoint.failure_caused_excption_num],21 ;注意填写的是中断向量号
    mov byte [longmode_enter_checkpoint.failure_flags],0x3

; IDT定义 - 长模式64位下的前32个向量
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

; 实模式下的中断处理例程标签（用于模拟实模式，实际实模式使用向量跳转表）
realmode_interrupt_handlers:
bits 16
    .divide_by_zero:
    ; 中断处理代码将在实现时填充
    .debug:
    ; 中断处理代码将在实现时填充
    .nmi:
    ; 中断处理代码将在实现时填充
    .bp:
    ; 中断处理代码将在实现时填充
    ; 每个中断处理例程的标签
    .of:
    ; 中断处理代码将在实现时填充
    .br:
    ; 中断处理代码将在实现时填充
    .ud:
    ; 中断处理代码将在实现时填充
    .nm:
    ; 中断处理代码将在实现时填充
    .df:
    ; 中断处理代码将在实现时填充
    .cross:
    ; 中断处理代码将在实现时填充
    ; 每个中断处理例程的标签
    .tss:
    ; 中断处理代码将在实现时填充
    ; 每个中断处理例程的标签
    .NP:
    ; 中断处理代码将在实现时填充
    ; 每个中断处理例程的标签
    .SS:
    ; 中断处理代码将在实现时填充
    ; 每个中断处理例程的标签
    .GP:
    ; 中断处理代码将在实现时填充
    ; 每个中断处理例程的标签
    .PF:
    ; 中断处理代码将在实现时填充
    ; 每个中断处理例程的标签
    .vec15:
    ; 中断处理代码将在实现时填充
    ; 每个中断处理例程的标签
    .MF:
    ; 中断处理代码将在实现时填充
    ; 每个中断处理例程的标签
    .AC:
    ; 中断处理代码将在实现时填充
    ; 每个中断处理例程的标签
    .MC:
    ; 中断处理代码将在实现时填充
    ; 每个中断处理例程的标签
    .XM:
    ; 中断处理代码将在实现时填充
    ; 每个中断处理例程的标签
    .VE:
    ; 中断处理代码将在实现时填充
    ; 每个中断处理例程的标签
    .CP:
    ; 中断处理代码将在实现时填充

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

SECTION .init_stack
align 0x1000
times 2048 dq 0
init_stack_end:                         ; 为AP初始栈分配空间