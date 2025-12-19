#pragma once
#include <stdint.h> 
#include <lock.h>
#include "os_error_definitions.h"
//这是个字节内不反转的位图实现
//使用每一项为宽度1bit的位图
//以每64bit为单元存储
//内核态常用数据结构
//私有接口不加锁,但提供自旋锁，自行在外部接口中使用
class bitmap_t { 
    protected:
    spinrwlock_cpp_t bitmap_rwlock;
    spinlock_cpp_t used_bit_count_lock;
    uint64_t*bitmap;
    uint64_t bitmap_size_in_64bit_units;
    uint64_t bitmap_used_bit;


    uint8_t* byte_bitmap_base;
    //byte,bit扫描的时候使用的指针，需要在构造函数里面初始化
    static constexpr uint64_t U64_FULL_UNIT = 0xFFFFFFFFFFFFFFFF;
    static constexpr uint8_t BYTE_FULL = 0xFF;
    //静态常量
    
public:    
    void bit_set(uint64_t bit_idx,bool value);
    bool bit_get(uint64_t bit_idx);
    void bits_set(uint64_t start_bit_idx,uint64_t bit_count,bool value);
    void bytes_set(uint64_t start_byte_idx,uint64_t byte_count,bool value);
    void u64s_set(uint64_t start_u64_idx,uint64_t u64_count,bool value);

    //上面三个函数不进行边界检查以及锁检查,设计思路上是基本操作，bits_set某种程度上可以被bytes_set,u64s_set优化但不会采用
    int continual_avaliable_bits_search(uint64_t bit_count,uint64_t&result_base_idx);
    int continual_avaliable_bytes_search(uint64_t byte_count,uint64_t&result_base_idx);
    int continual_avaliable_u64s_search(uint64_t u64_count,uint64_t&result_base_idx);
    //上面三个函数的结果引索是以各自单位为基本的引索，若返回值不为OS_SUCCESS则result_base_idx无效

    int get_bitmap_used_bit();
    void count_bitmap_used_bit();
    
    int used_bit_count_add(uint64_t add_count);//这操作是加锁的
    int used_bit_count_sub(uint64_t sub_count);//这操作是加锁的
};