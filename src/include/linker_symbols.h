#ifndef LINKER_SYMBOLS_H
#define LINKER_SYMBOLS_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// 内核内存布局常量
extern const uint64_t __KImgSize;
extern const uint64_t PAGE_LEVEL_5;
extern const uint64_t PAGE_LEVEL_4;
extern const uint64_t __HEAP_SIZE;
extern const uint64_t __KLOG_SIZE;
extern const uint64_t __STACK_SIZE;
extern const uint64_t __PGTB_HEAP_SIZE;
extern const uint64_t init_sec_base;
extern const uint64_t init_sec_size;
extern const uint64_t KImgphybase;
extern const uint64_t KImgvbase;
extern const uint64_t Koffset;

// 初始化代码段符号
extern uint8_t init_text_begin;
extern uint8_t init_text_end;
extern uint8_t init_rodata_begin;
extern uint8_t init_rodata_end;
extern uint8_t init_data_begin;
extern uint8_t init_data_end;
extern uint8_t init_stack_begin;
extern uint8_t init_stack_end;
// 主文本段符号
extern uint8_t text_begin;
extern uint8_t text_end;

// 数据段符号
extern uint8_t _data_lma;
extern uint8_t _data_start;
extern uint8_t _data_end;

// 只读数据段符号
extern uint8_t _rodata_lma;
extern uint8_t _rodata_start;
extern uint8_t _rodata_end;

// 栈段符号
extern uint8_t _stack_lma;
extern uint8_t _stack_bottom;
extern uint8_t _stack_top;

//堆位图符号
extern uint8_t _heap_bitmap_lma;
extern uint8_t __heap_bitmap_start;
extern uint8_t __heap_bitmap_end;
// 堆段符号
extern uint8_t _heap_lma;
extern uint8_t __heap_start;
extern uint8_t __heap_end;

// 内核日志段符号
extern uint8_t _klog_lma;
extern uint8_t __klog_start;
extern uint8_t __klog_end;


extern uint8_t _kspace_uppdpt_lma;
extern uint8_t __kspace_uppdpt_start;
extern uint8_t __kspace_uppdpt_end;
// 获取地址的宏定义，方便使用
#define GET_SYMBOL_ADDR(symbol) ((void*)&symbol)

#ifdef __cplusplus
}
#endif

#endif // LINKER_SYMBOLS_H