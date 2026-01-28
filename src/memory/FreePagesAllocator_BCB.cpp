#include "memory/FreePagesAllocator.h"

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::default_success()
{
    KURD_t kurd=default_kurd();
    kurd.result=result_code::SUCCESS;
    kurd.level=level_code::INFO;
    return kurd;
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::default_fatal()
{
    KURD_t kurd=default_kurd();
    kurd=set_fatal_result_level(kurd);
    return kurd;
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::default_error()
{
    KURD_t kurd=default_kurd();
    kurd=set_result_fail_and_error_level(kurd);
    return kurd;
}
uint8_t FreePagesAllocator::free_pages_in_seg_control_block::get_max_order()
{
    return this->MAX_SUPPORT_ORDER;
}

uint8_t FreePagesAllocator::free_pages_in_seg_control_block::size_to_order(uint64_t size)
{
    if (size == 0) {
        return 0;
    }
    
    // 计算需要多少个4KB页面（向上取整）
    uint64_t numof_4kbpgs = (size + _4KB_PAGESIZE - 1) / _4KB_PAGESIZE;
    
    // 使用匿名函数计算向上取整的2的幂
    auto next_pow2 = [](uint64_t n) -> uint64_t {
        n--;
        n |= n >> 1;
        n |= n >> 2;
        n |= n >> 4;
        n |= n >> 8;
        n |= n >> 16;
        n |= n >> 32;
        n++;
        return n;
    };
    
    // 将页面数向上取整到2的幂
    uint64_t np2 = next_pow2(numof_4kbpgs);
    
    // 用__builtin_clzll取log2
    // 注意：np2 保证是2的幂且不为0
    return 63 - __builtin_clzll(np2);
}
phyaddr_t FreePagesAllocator::free_pages_in_seg_control_block::allocate_buddy_way(uint64_t size, KURD_t &result)
{
    KURD_t success=default_success();
    KURD_t error=default_error();
    KURD_t fatal=default_fatal();
    KURD_t contain=KURD_t();
    success.event_code=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::EVENT_CODE_ALLOCATE_BUDY_WAY;
    error.event_code=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::EVENT_CODE_ALLOCATE_BUDY_WAY;
    fatal.event_code=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::EVENT_CODE_ALLOCATE_BUDY_WAY;
    uint8_t order=size_to_order(size);
    if(order>=MAX_SUPPORT_ORDER){
        //不支持，报错
    }

    for(uint8_t i=order;i<=MAX_SUPPORT_ORDER;i++){
        if(suggest_order_free_page_index[i]!=INVALID_INBCB_INDEX&&order_bitmaps->bit_get(order_bases[i]+suggest_order_free_page_index[i])){
            order_bitmaps->bit_set(order_bases[i]+suggest_order_free_page_index[i],false);
            if(i>order){
                split_page(suggest_order_free_page_index[i],i,order);
            }
            phyaddr_t res_addr=base+(suggest_order_free_page_index[i]<< (i+12));
            return res_addr;
        }
    }
    for(uint8_t i=order;i<=MAX_SUPPORT_ORDER;i++){//线性扫描
        uint64_t found_idx=order_bitmaps->find_free_in_interval(
            order_bases[i],
            1ULL<<(MAX_SUPPORT_ORDER-i)
        );
        if(found_idx!=0xFFFFFFFFFFFFFFFF){
            order_bitmaps->bit_set(order_bases[i]+found_idx,false);
            if(i>order){
                split_page(found_idx,i,order);
            }
            phyaddr_t res_addr=base+(found_idx<< (i+12));
            return res_addr;
        }
    }

}
FreePagesAllocator::free_pages_in_seg_control_block::free_pages_in_seg_control_block(phyaddr_t base, uint8_t max_support_order)
{
    this->MAX_SUPPORT_ORDER=max_support_order;
    this->base=base;
    for(uint8_t i=0;i<MAX_SUPPORT_ORDER;i++){
        suggest_order_free_page_index[i]=~0;
    }
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::second_stage_init()
{
    KURD_t success=default_success();
    KURD_t error=default_error();
    KURD_t fatal=default_fatal();
    KURD_t contain=KURD_t();
    success.event_code=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::EVENT_CODE_INIT;
    error.event_code=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::EVENT_CODE_INIT;
    fatal.event_code=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::EVENT_CODE_INIT;
    order_bitmaps=new mixed_bitmap_t(1ULL<<(MAX_SUPPORT_ORDER+1));
    contain=order_bitmaps->second_stage_init();
    if(contain.result!=result_code::SUCCESS){
        return contain;
    }
    order_bases[0]=0;
    for(uint8_t order=0;order<MAX_SUPPORT_ORDER;order++){
        order_bases[order+1]=order_bases[order]+(1ULL<<order);
    }
    free_page_without_merge(0,MAX_SUPPORT_ORDER);
    return success;
}
void FreePagesAllocator::free_pages_in_seg_control_block::free_page_without_merge(uint64_t in_bcb_idx, uint8_t order)
{
    order_bitmaps->bit_set(order_bases[order]+in_bcb_idx,true);
    suggest_order_free_page_index[order]=in_bcb_idx;
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::split_page(uint64_t splited_idx, uint8_t splited_order, uint8_t target_order)
{
    KURD_t success=default_success();
    KURD_t error=default_error();
    KURD_t fatal=default_fatal();
    KURD_t contain=KURD_t();
    if(splited_order==target_order){//触底成功
        order_bitmaps->bit_set(order_bases[splited_order]+splited_idx,false);
        return success;
    }
    if(splited_order<target_order){//外部的错误参数
        return error;
    }
    if(!order_bitmaps->bit_get(order_bases[splited_order]+splited_idx)){//调用者传入了错误的参数
        return error;
    }
    order_bitmaps->bit_set(order_bases[splited_order]+splited_idx,false);
    free_page_without_merge(1+(splited_idx<<1),splited_order-1);
    return split_page(splited_idx<<1,splited_order-1,target_order);
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::conanico_free(uint64_t in_bcb_idx, uint8_t order)
{
    KURD_t success=default_success();
    KURD_t error=default_error();
    KURD_t fatal=default_fatal();
    KURD_t contain=KURD_t();
    uint8_t current_order=order;
    uint64_t current_idx=in_bcb_idx;
    for(;current_order<MAX_SUPPORT_ORDER;current_order++){
        uint64_t buddy_idx=current_idx^1;
        if(!order_bitmaps->bit_get(order_bases[current_order]+buddy_idx)){
            break;
        }
        order_bitmaps->bit_set(order_bases[current_order]+buddy_idx,false);
        order_bitmaps->bit_set(order_bases[current_order]+current_idx,false);
        current_idx=current_idx>>1;
        order_bitmaps->bit_set(order_bases[current_order+1]+current_idx,true);
    }
    return success;
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::free_buddy_way(phyaddr_t base, uint64_t size)
{
    uint8_t order=size_to_order(size);
    uint64_t in_bcb_idx=(base-this->base)>>(order+12);//还要对齐校验
    return conanico_free(in_bcb_idx,order);
}
bool FreePagesAllocator::free_pages_in_seg_control_block::is_reclusive_fold_success(uint64_t idx, uint8_t order)
{
    if(order>MAX_SUPPORT_ORDER){
        return false;
    }
   
    bool left=order_bitmaps->bit_get(order_bases[order-1]+(idx<<1));
    bool right=order_bitmaps->bit_get(order_bases[order-1]+(idx<<1)+1);
    if(left&&right){
        order_bitmaps->bit_set(order_bases[order]+idx,true);
        order_bitmaps->bit_set(order_bases[order-1]+(idx<<1),false);
        order_bitmaps->bit_set(order_bases[order-1]+(idx<<1)+1,false);
        return true;
    }
    bool left_succeed=false;
    bool right_succeed=false;
    if(!left){
        left_succeed=is_reclusive_fold_success(idx<<1,order-1);
    }
    if(!right){
        right_succeed=is_reclusive_fold_success((idx<<1)+1,order-1);
    }
    if(left_succeed&&right_succeed){
        order_bitmaps->bit_set(order_bases[order]+idx,true);
        order_bitmaps->bit_set(order_bases[order-1]+(idx<<1),false);
        order_bitmaps->bit_set(order_bases[order-1]+(idx<<1)+1,false);
        return true;
    }
        return false;

}
void FreePagesAllocator::free_pages_in_seg_control_block::top_fold()
{
    is_reclusive_fold_success(0,MAX_SUPPORT_ORDER);
}