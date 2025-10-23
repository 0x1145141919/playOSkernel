ENTRY(_kernel_Init)
__KImgSize = 128M;
EXTERN(PAGE_LEVEL)
PAGE_LEVEL_5 = 0xFF00000000000000;
PAGE_LEVEL_4 = 0xFFFF800000000000;
__HEAP_SIZE = 16M;
__KLOG_SIZE = 2M;
__STACK_SIZE = 64K;
__PGTB_HEAP_SIZE = 4M;
init_sec_base = 0x1000;
init_sec_size   = 0x100000;
KImgphybase = 0x4000000;
PROVIDE(KImgphybase = 0x4000000);


KImgvbase = KImgphybase + PAGE_LEVEL_4;
Koffset = KImgvbase - KImgphybase;

MEMORY {
    /* 虚拟内存内核主体区域 */
    VRAM (rwx) : ORIGIN = KImgvbase, LENGTH = __KImgSize
    /*物理内存初始化代码区 */
    init (rx) : ORIGIN = init_sec_base, LENGTH = init_sec_size
}

SECTIONS { 
    . = init_sec_base;
    .init_text : {
        init_text_begin = .;
        CMakeFiles/kernel.elf.dir/src/kernel_entry.asm.o(.boottext)
        . = ALIGN(4096);
        init_text_end = .;
    } > init : init_text
    .init_rodata : {
        init_rodata_begin = .;
        CMakeFiles/kernel.elf.dir/src/kernel_entry.asm.o(.init_rodata)
        . = ALIGN(4K);
        init_rodata_end = .;
    } > init : init_rodata
    . = KImgvbase;
    .text_main :  AT( KImgphybase){
        text_begin = .;
         *(.text .text.*)
         . = ALIGN(4096); 
         text_end = . ;
    } > VRAM :text_main
    /* 计算数据段加载地址 */
    _data_lma = ALIGN(LOADADDR(.text_main) + SIZEOF( .text_main) ,4096);
    .data : AT(_data_lma){
        _data_start = .;
        *(.data .data.*)
         *(.got .got.*)
         *(.*array)
         *(.note.*)
         *(.lbss .lbss.*)
        *(.bss .bss.*)
        *(COMMON)
        *(.gnu.property)
    . = ALIGN(4096); 
    _data_end = . ;
    } > VRAM :data
    _rodata_lma = ALIGN(LOADADDR(.data) + SIZEOF(.data),4096);
    .rodata :  AT( _rodata_lma){
        _rodata_start = . ;
         *(.rodata .rodata.*)
         *(.eh_frame)
    . = ALIGN(4096);
    _rodata_end = . ;
    } > VRAM :rodata

    . = ALIGN(4096);
    _stack_lma = ALIGN(LOADADDR(.rodata) + SIZEOF(.rodata),4096);
    .stack :  AT( _stack_lma){
        _stack_bottom = .;
        . = . + __STACK_SIZE; 
        _stack_top = .;
        . = ALIGN(4096); 
            . = ALIGN(2M);
    } > VRAM :stack

    /* 计算堆段加载地址 */
    _heap_lma = ALIGN(LOADADDR(.stack) + SIZEOF(.stack),2M);
    . = ALIGN(2M);
    .heap :  AT( _heap_lma){
        __heap_start = .;
        . = . + __HEAP_SIZE;
        __heap_end = .;
    } > VRAM :heap
    
    /* 计算页表堆段加载地址 */
    _pgtb_heap_lma = ALIGN(LOADADDR(.heap) + SIZEOF(.heap),2M);
    .pgtb_heap : AT( _pgtb_heap_lma){
        __pgtbhp_start = .;
        . = . + __PGTB_HEAP_SIZE;
        __pgtbhp_end = .;
    } > VRAM :pgtb_heap
    
    /* 计算内核日志段加载地址 */
    _klog_lma = ALIGN(LOADADDR(.pgtb_heap) + SIZEOF(.pgtb_heap),2M);
    .klog : AT( _klog_lma){
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
    rodata PT_LOAD FLAGS(4);    /* PF_R (R) */
    stack PT_LOAD FLAGS(6);     /* PF_R | PF_W (RW) */
    heap PT_LOAD FLAGS(6);
    pgtb_heap PT_LOAD FLAGS(6);
    klog PT_LOAD FLAGS(6);
    
}