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
extern KImgphybase 
extern _stack_top
extern kernel_start
global _kernel_Init
global pml4_table_init
global pml5_table_init
SECTION .bootdata
dq 0
rep times 512

SECTION .boottext
_kernel_Init:
bits 64

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
.paging_done:
    ; 恢复寄存器
    mov rsp,_stack_top
   mov rax, kernel_start
   call  rax




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
SECTION .init_stack
align 0x1000
dq 0
rep times 512

