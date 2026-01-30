#include "memory/FreePagesAllocator.h"
#include "util/kout.h"
#include "util/OS_utils.h"
FreePagesAllocator::free_pages_in_seg_control_block*FreePagesAllocator::first_BCB;
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
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::default_kurd()
{
    return KURD_t(0,0,module_code::MEMORY,MEMMODULE_LOCAIONS::LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK,0,0,err_domain::CORE_MODULE);
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
    success.event_code=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::EVENT_CODE_ALLOCATE_BUDY_WAY;
    error.event_code=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::EVENT_CODE_ALLOCATE_BUDY_WAY;
    fatal.event_code=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::EVENT_CODE_ALLOCATE_BUDY_WAY;
    uint8_t order=size_to_order(size);
    if(order>=MAX_SUPPORT_ORDER){
        error.reason=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::ALLOCATE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_ACQUIRE_SIZE_TO_BIG;
        result=error;
        return 0;
    }

    for(uint8_t i=order;i<=MAX_SUPPORT_ORDER;i++){
        if(suggest_order_free_page_index[i]!=INVALID_INBCB_INDEX&&order_freepage_existency_bitmaps->bit_get(order_bases[i]+suggest_order_free_page_index[i])){
            statistics.suggest_hit[order]++;
            if(i>order){
                split_page(suggest_order_free_page_index[i],i,order);
            }
            if(i==order){
                order_freepage_existency_bitmaps->bit_set(order_bases[i]+suggest_order_free_page_index[i],false);
                statistics.free_count[order]--;
            }
            phyaddr_t res_addr=base+(suggest_order_free_page_index[i]<< (i+12));
            return res_addr;
        }else statistics.suggest_miss[order]++;
    }
    statistics.scan_count++;
    for(uint8_t i=order;i<=MAX_SUPPORT_ORDER;i++){//线性扫描
        uint64_t found_idx=order_freepage_existency_bitmaps->find_free_in_interval(
            order_bases[i],
            1ULL<<(MAX_SUPPORT_ORDER-i)
        );
        if(found_idx!=0xFFFFFFFFFFFFFFFF){
            if(i>order){
                split_page(found_idx,i,order);
            }
            if(i==order){
                order_freepage_existency_bitmaps->bit_set(order_bases[i]+found_idx,false);
                statistics.free_count[order]--;
            }
            phyaddr_t res_addr=base+(found_idx<< (i+12));
            return res_addr;
        }
    }
    error.reason=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::ALLOCATE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_NO_AVALIABLE_BUDDY;
    result=error;
    return 0;
}
FreePagesAllocator::free_pages_in_seg_control_block::free_pages_in_seg_control_block(phyaddr_t base, uint8_t max_support_order)
{
    this->MAX_SUPPORT_ORDER=max_support_order;
    this->base=base;
    for(uint8_t i=0;i<MAX_SUPPORT_ORDER;i++){
        suggest_order_free_page_index[i]=~0;
        order_bases[i]=0;
    }
    this->is_splited_bitmap_valid=is_splited_bitmap_valid;
    setmem(&statistics,sizeof(statistics),0);
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
    order_freepage_existency_bitmaps=new mixed_bitmap_t(1ULL<<(MAX_SUPPORT_ORDER+1));
    contain=order_freepage_existency_bitmaps->second_stage_init();
    if(contain.result!=result_code::SUCCESS)return contain;
    order_bases[0]=0;
    for(uint8_t order=0;order<MAX_SUPPORT_ORDER;order++){
        order_bases[order+1]=order_bases[order]+(1ULL<<(MAX_SUPPORT_ORDER-order));
    }
    free_page_without_merge(0,MAX_SUPPORT_ORDER);
    return success;
}
void FreePagesAllocator::free_pages_in_seg_control_block::free_page_without_merge(uint64_t in_bcb_idx, uint8_t order)
{
    order_freepage_existency_bitmaps->bit_set(order_bases[order]+in_bcb_idx,true);
    statistics.free_count[order]++;
    suggest_order_free_page_index[order]=in_bcb_idx;
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::split_page(uint64_t splited_idx, uint8_t splited_order, uint8_t target_order)
{
    KURD_t success=default_success();
    KURD_t error=default_error();
    success.event_code=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::EVENT_CODE_SPLIT_PAGE;
    error.event_code=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::EVENT_CODE_SPLIT_PAGE;
    if(splited_order<target_order||target_order>MAX_SUPPORT_ORDER||splited_order>MAX_SUPPORT_ORDER){//外部的错误参数
        error.reason=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::SPLIT_PAGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_INVALID_ORDER;
        return error;
    }
    if(splited_order==target_order){//触底成功
        order_freepage_existency_bitmaps->bit_set(order_bases[splited_order]+splited_idx,false);
        statistics.free_count[splited_order]--;
        return success;
    }
    if(!order_freepage_existency_bitmaps->bit_get(order_bases[splited_order]+splited_idx)){//调用者传入了错误的参数
        error.reason=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::SPLIT_PAGE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_PAGE_NOT_FREE;
        return error;
    }
    order_freepage_existency_bitmaps->bit_set(order_bases[splited_order]+splited_idx,false);
    statistics.free_count[splited_order]--;
    order_freepage_existency_bitmaps->bit_set(order_bases[splited_order-1]+(splited_idx<<1),true);
    statistics.free_count[splited_order-1]++;
    free_page_without_merge(1+(splited_idx<<1),splited_order-1);
    statistics.split_count++;
    return split_page(splited_idx<<1,splited_order-1,target_order);
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::conanico_free(uint64_t in_bcb_idx, uint8_t order)
{
    KURD_t success=default_success();
    KURD_t error=default_error();
    KURD_t fatal=default_fatal();
    error.event_code=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::EVENT_CODE_CONANICO_FREE;
    success.event_code=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::EVENT_CODE_CONANICO_FREE;
    fatal.event_code=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::EVENT_CODE_CONANICO_FREE;
    if(order>MAX_SUPPORT_ORDER){
        error.reason=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::CONANICO_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_INVALID_ORDER;
        return error;
    }
    if(order_freepage_existency_bitmaps->bit_get(order_bases[order]+in_bcb_idx)){
        error.reason=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::CONANICO_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_DOUBLE_FREE;
        return error;
    }
    if(in_bcb_idx>=1ULL<<(MAX_SUPPORT_ORDER-order)){
        error.reason=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::CONANICO_FREE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_INVALID_PAGE_INDEX;
        return error;
    }
    order_freepage_existency_bitmaps->bit_set(order_bases[order]+in_bcb_idx,true);
    statistics.free_count[order]++;
    uint8_t current_order=order;
    uint64_t current_idx=in_bcb_idx;
    for(;current_order<MAX_SUPPORT_ORDER;current_order++){
        uint64_t buddy_idx=current_idx^1;
        if(!order_freepage_existency_bitmaps->bit_get(order_bases[current_order]+buddy_idx)){
            statistics.fold_count_fail++;
            break;
        }
        order_freepage_existency_bitmaps->bit_set(order_bases[current_order]+buddy_idx,false);
        order_freepage_existency_bitmaps->bit_set(order_bases[current_order]+current_idx,false);
        statistics.free_count[current_order]-=2;
        statistics.fold_count_success++;
        current_idx=current_idx>>1;
        if(order_freepage_existency_bitmaps->bit_get(order_bases[current_order+1]+current_idx)){
            //检测到二叉树结构被破坏，准备panic
            fatal.reason=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::CONANICO_FREE_RESULTS_CODE::FATAL_REASONS_CODE::BIN_TREE_CONSISTENCY_VIOLATION;
        }
        order_freepage_existency_bitmaps->bit_set(order_bases[current_order+1]+current_idx,true);
        statistics.free_count[current_order+1]++;
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
    if(order>MAX_SUPPORT_ORDER||order==0){
        return false;
    }
   
    bool left=order_freepage_existency_bitmaps->bit_get(order_bases[order-1]+(idx<<1));
    bool right=order_freepage_existency_bitmaps->bit_get(order_bases[order-1]+(idx<<1)+1);
    if(left&&right){
        order_freepage_existency_bitmaps->bit_set(order_bases[order]+idx,true);
        statistics.free_count[order]++;
        order_freepage_existency_bitmaps->bit_set(order_bases[order-1]+(idx<<1),false);
        order_freepage_existency_bitmaps->bit_set(order_bases[order-1]+(idx<<1)+1,false);
        statistics.free_count[order-1]-=2;
        statistics.fold_count_success++;
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
        order_freepage_existency_bitmaps->bit_set(order_bases[order]+idx,true);
        statistics.free_count[order]++;
        order_freepage_existency_bitmaps->bit_set(order_bases[order-1]+(idx<<1),false);
        order_freepage_existency_bitmaps->bit_set(order_bases[order-1]+(idx<<1)+1,false);
        statistics.free_count[order-1]-=2;
        statistics.fold_count_success++;
        return true;
    }
        statistics.fold_count_fail++;
        return false;

}
void FreePagesAllocator::free_pages_in_seg_control_block::top_fold()
{
    is_reclusive_fold_success(0,MAX_SUPPORT_ORDER);
}
void FreePagesAllocator::free_pages_in_seg_control_block::print_basic_info()
{
    /**
     * 打印base，MAX_SUPPORT_ORDER，suggest_order_free_page_index[DESINGED_MAX_SUPPORT_ORDER]数组的有效条目
     */
    kio::bsp_kout << "[FreePagesAllocator::free_pages_in_seg_control_block Info]" << kio::kendl;
    kio::bsp_kout << "Base Address: 0x";
    kio::bsp_kout.shift_hex();  // 切换到十六进制模式
    kio::bsp_kout << base << kio::kendl;
    kio::bsp_kout.shift_dec();  // 切换回十进制模式
    kio::bsp_kout << "Max Support Order: ";
    kio::bsp_kout << (uint32_t)MAX_SUPPORT_ORDER << kio::kendl;
    kio::bsp_kout << "Suggest Order Free Page Index Array:" << kio::kendl;
    
    for (uint8_t i = 0; i <=MAX_SUPPORT_ORDER; i++) {
        if (suggest_order_free_page_index[i] != INVALID_INBCB_INDEX) {
            kio::bsp_kout << "  [" << (uint32_t)i << "] = " << suggest_order_free_page_index[i] << kio::kendl;
        }
    }
}
void FreePagesAllocator::free_pages_in_seg_control_block::print_bitmap_info()
{
    kio::bsp_kout << "[Bitmap Info for all Orders]" << kio::kendl;
    
    for (uint8_t order = 0; order < MAX_SUPPORT_ORDER; order++) {
        print_bitmap_order_info_compress(order);
    }
}
void FreePagesAllocator::free_pages_in_seg_control_block::print_bitmap_order_info_compress(uint8_t order)
{
    print_bitmap_order_interval_compress(order, 0, 1ULL << (MAX_SUPPORT_ORDER - order));
}
void FreePagesAllocator::free_pages_in_seg_control_block::print_bitmap_order_interval_compress(uint8_t order, uint64_t base, uint64_t length)
{
    kio::bsp_kout << "[Bitmap Order " << (uint32_t)order << " Interval Compress Print]" << kio::kendl;
    
    if (order >= MAX_SUPPORT_ORDER) {
        kio::bsp_kout << "Invalid order!" << kio::kendl;
        return;
    }
    
    uint64_t max_count = 1 << (MAX_SUPPORT_ORDER - order);
    
    // 确保base和length不超过有效范围
    if (base >= max_count) {
        kio::bsp_kout << "Base index out of range!" << kio::kendl;
        return;
    }
    
    // 计算实际扫描长度
    uint64_t actual_length = length;
    if (base + length > max_count) {
        actual_length = max_count - base;
    }
    
    uint64_t i = base;
    bool scanning = false;  // 是否正在扫描连续区间
    bool current_state = false;  // 当前状态：true表示空闲页存在(1)，false表示不存在(0)
    uint64_t interval_start = 0;  // 当前区间的起始位置
    
    while (i < base + actual_length) {
        bool value = order_freepage_existency_bitmaps->bit_get(order_bases[order] + i); // 为1代表空闲页存在，为0代表不存在
        
        if (!scanning) {
            // 开始新的扫描
            scanning = true;
            current_state = value;
            interval_start = i;
        } else if (value != current_state) {
            // 状态发生变化，输出上一个区间
            kio::bsp_kout << "[" << interval_start << "," << i-1 << "]=";
            if (current_state) {
                kio::bsp_kout << "EXIST" << kio::kendl;
            } else {
                kio::bsp_kout << "NOT_EXIST" << kio::kendl;
            }
            
            // 更新状态，开始新区间
            current_state = value;
            interval_start = i;
        }
        
        i++;
    }
    
    // 输出最后一个区间
    if (scanning) {
        kio::bsp_kout << "[" << interval_start << "," << i-1 << "]=";
        if (current_state) {
            kio::bsp_kout << "EXIST" << kio::kendl;
        } else {
            kio::bsp_kout << "NOT_EXIST" << kio::kendl;
        }
    }
}