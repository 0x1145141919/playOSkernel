#include "memory/FreePagesAllocator.h"
#include "util/OS_utils.h"
constexpr uint8_t FIRST_BCB_MAX_ORDER=20;
constexpr uint64_t FIRST_BCB_MAX_PAGES_COUNT=1ULL<<FIRST_BCB_MAX_ORDER;
uint64_t FIRST_BCB_FIXED_BITMAP[2*FIRST_BCB_MAX_PAGES_COUNT/64]={0};
FreePagesAllocator::free_pages_in_seg_control_block::mixed_bitmap_t::mixed_bitmap_t(uint64_t entry_count)
{
    this->bitmap_size_in_64bit_units=(entry_count+63)/64;
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::mixed_bitmap_t::second_stage_init()
{
    if(this==first_BCB->order_bitmaps){
        this->bitmap=FIRST_BCB_FIXED_BITMAP;
        this->bitmap_size_in_64bit_units=(entry_count+63)/64;
    }
    bits_set(0,entry_count,false);
}
//返回0xFFFFFFFFFFFFFFFF表示没有找到
//标记为1的才认为是空闲，返回的引索是基于参数start_idx的偏移
//如果区间超过bitmap范围，则只在能搜索到的范围内搜索，不提前报错
uint64_t FreePagesAllocator::free_pages_in_seg_control_block::mixed_bitmap_t::find_free_in_interval(uint64_t start_idx, uint64_t interval_length)
{

    
    uint64_t end_idx = (start_idx + interval_length)> this->entry_count * 64 ? this->entry_count : (start_idx + interval_length);
    uint64_t u64_begin = align_up(start_idx, 64);
    uint64_t u64_end = align_down(end_idx, 64);
    for(uint64_t i = start_idx; i < u64_begin && i < end_idx; ++i) {
        if(this->bit_get(i)){
            return i-start_idx;
        }
    }
    for(uint64_t i = u64_begin/64; i < u64_end/64; i++) {
        uint64_t u64_idx = i / 64;
        if(this->bitmap[u64_idx]) {
            uint64_t unit_base=i*64;
            return unit_base+__builtin_ctz(this->bitmap[u64_idx])-start_idx;
        }
    }
    for(uint64_t i = u64_end; i < end_idx; ++i) {
        if(this->bit_get(i)){
            return i-start_idx;
        }
    }
    return 0xFFFFFFFFFFFFFFFF;  // 表示没有找到
}