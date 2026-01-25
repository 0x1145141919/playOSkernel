; 物理地址类型 (MAXPHYADDR ≤ 52)
%define phys_addr_t dq
%include "checkpoint.inc"
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
extern KImgphybase 
extern _stack_top
extern kernel_start
extern ap_init
extern bsp_init_gdt_entries
extern bsp_init_gdt_descriptor
extern bsp_init_idt_entries
extern bsp_init_idtr
extern idt_table_rm
global _kernel_Init
global pml4_table_init
global pml5_table_init

global AP_realmode_start
global assigned_processor_id
SECTION .bootdata


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
align 0x1000
bits 16
cli
mov eax, [assigned_processor_id]
mov [realmode_enter_checkpoint.success_word], eax

    ; 加载GDT
    lgdt [gdt_descriptor]
    lidt [idt_table_rm]
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
; 实模式下的中断处理例程标签（用于模拟实模式，实际实模式使用向量跳转表）

SECTION .init_stack
align 0x1000
times 2048 dq 0
init_stack_end:                         ; 为AP初始栈分配空间