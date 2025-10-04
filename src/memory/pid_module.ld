OUTPUT_FORMAT("elf64-x86-64")  # 根据目标架构调整
OUTPUT_ARCH(i386:x86-64)       # 根据目标架构调整

SECTIONS {
    /* 起始地址设为0，由加载器决定实际加载地址 */
    . = 0x0;
    
    /* 文本段 */
    .text : {
        *(.text .text.*)
        . = ALIGN(8);
    }
    
    /* 只读数据 */
    .rodata : {
        *(.rodata .rodata.*)
        . = ALIGN(8);
    }
    
    /* 数据段 */
    .data : {
        *(.data .data.*)
        . = ALIGN(8);
    }
    
    /* BSS段 */
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(8);
    }
    
    /* 动态链接相关部分 */
    .dynamic : { *(.dynamic) }
    .got : { *(.got) }
    .got.plt : { *(.got.plt) }
    
    /* 确保所有必要的节都被包含 */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
    }
}