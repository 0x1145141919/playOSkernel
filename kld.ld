ENTRY(_kernel_Init)
__KImgSize = 128M;
EXTERN(PAGE_LEVEL)
PAGE_LEVEL_5 = 0xFF00000000000000;
PAGE_LEVEL_4 = 0xFFFF800000000000;
__HEAP_SIZE = 16M;
__KLOG_SIZE = 2M;
__STACK_SIZE = 8M;
__PGTB_HEAP_SIZE = 4M;
init_sec_base = 0x100000;
init_sec_size   = 0x100000;
KImgphybase = 0x200000;
KImgvbase = KImgphybase;
Koffset = KImgvbase-KImgphybase;
MEMORY {
    /* 虚拟内存内核主体区域 */
    VRAM (rwx) : ORIGIN = KImgvbase, LENGTH = __KImgSize
    /*物理内存初始化代码区 */
    init (rx) : ORIGIN = init_sec_base, LENGTH = init_sec_size
}

SECTIONS { 
    . = init_sec_base;
    .init_text : {
        CMakeFiles/kernel.elf.dir/src/kernel_entry.asm.o(.boottext)
        . = ALIGN(4096);
    } > init : init_text
    .init_rodata : {
        CMakeFiles/kernel.elf.dir/src/kernel_entry.asm.o(.init_rodata)
        . = ALIGN(2M);
    } > init : init_rodata
    . = KImgvbase;
    .text_main : AT(KImgphybase) {
         *(.text .text.*)
        . = ALIGN(4096); 
    } > VRAM :text_main
    . = ALIGN(4096); 
    /* 计算数据段加载地址 */
    _data_lma = . - Koffset;
    .data : AT(_data_lma) {
        *(.data .data.*)
    } > VRAM :data
    . = ALIGN(4096); 
        .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } > VRAM :bss
     . = ALIGN(4096);
    /* 计算只读数据段加载地址 */
    _rodata_lma = ALIGN(. - Koffset, 0x1000);
    .rodata : AT(_rodata_lma) {
         . = ALIGN(4096); 
    } > VRAM :rodata
    
    . = ALIGN(4096); 
    /* 计算栈段加载地址 */
.other_meta : AT(. - Koffset) {
    /* 强制4K对齐（兼容MMU页表） */
    . = ALIGN(4096);    
    /* 指定存放的内容（示例） */
     *(.got .got.plt)       /* 全局偏移表 */
    *(.fini_array .init_array)
    *(.note.gnu.build-id)   /* 内核版本信息 */
    *(.eh_frame)      /* 引导参数结构体 */
    *(.security_keys)    /* 加密密钥 */
    /* 填充至下一个4K边界 */
    . = ALIGN(4096);
/* 关键属性控制 */
} > VRAM :other_meta   /* > VRAM表示虚拟地址区域，:NONE禁止加载 */
    . = ALIGN(2M);
    _stack_lma = ALIGN(. - Koffset, 2M);
    .stack : AT(_stack_lma) {
        _stack_bottom = .;
        . = . + __STACK_SIZE; 
        _stack_top = .;
        . = ALIGN(2M); 
    } > VRAM :stack
    . = ALIGN(2M);
    /* 计算堆段加载地址 */
    _heap_lma = ALIGN(. - Koffset, 2M);
    .heap : AT(_heap_lma) {
        __heap_start = .;
        . = . + __HEAP_SIZE;
        __heap_end = .;
    } > VRAM :heap
    
    /* 计算页表堆段加载地址 */
    _pgtb_heap_lma = ALIGN(. - Koffset, 2M);
    .pgtb_heap : AT(_pgtb_heap_lma) {
        __pgtbhp_start = .;
        . = . + __PGTB_HEAP_SIZE;
        __pgtbhp_end = .;
    } > VRAM :pgtb_heap
    
    /* 计算内核日志段加载地址 */
    _klog_lma = ALIGN(. - Koffset, 2M);
    .klog : AT(_klog_lma) {
        __klog_start = .;
        . = . + __KLOG_SIZE;
        __klog_end = .;
    } > VRAM :klog


}
/* 定义段权限 */
PHDRS {
    init_text PT_LOAD FLAGS(5);      /* PF_R | PF_X (RX) */
    init_rodata PT_LOAD FLAGS(4);
    text_main PT_LOAD FLAGS(5);      /* PF_R | PF_X (RX) */
    data PT_LOAD FLAGS(6);      /* PF_R | PF_W (RW) */   
    bss PT_LOAD FLAGS(6);
    rodata PT_LOAD FLAGS(4);    /* PF_R (R) */
    other_meta PT_LOAD FLAGS(6);
    stack PT_LOAD FLAGS(6);     /* PF_R | PF_W (RW) */
    heap PT_LOAD FLAGS(6);
    pgtb_heap PT_LOAD FLAGS(6);
    klog PT_LOAD FLAGS(6);
    
}