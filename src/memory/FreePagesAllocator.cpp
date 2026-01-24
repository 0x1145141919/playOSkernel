#include "memory/FreePagesAllocator.h"
FreePagesAllocator::free_pages_in_seg_control_block *FreePagesAllocator::first_BCB;
constexpr uint64_t max_predefined_bcb_gb=4;
constexpr uint64_t predefined_bcb_gb_MAX_ORDER=9;
free_page_node_t pre_defined_nodes_array[predefined_bcb_gb_MAX_ORDER*(max_predefined_bcb_gb<<30)/(1<<(12+predefined_bcb_gb_MAX_ORDER+1))];
uint64_t pre_defined_mixed_pages_bitmap[(max_predefined_bcb_gb<<30)/(4096*sizeof(uint64_t))];
FreePagesAllocator::free_pages_in_seg_control_block::free_pages_in_seg_control_block(
    phyaddr_t base,
    uint64_t size,
    uint8_t max_support_order
)
{
    this->base = base;
    this->size = size;
    this->MAX_SUPPORT_ORDER = max_support_order;
    this->MIN_ALIGN_CONTENT=1<<(12+MAX_SUPPORT_ORDER+1);
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::second_stage_init()
{
    KURD_t fail = default_error();
    KURD_t submodules_result_container;

    // 参数校验
    if (base % MIN_ALIGN_CONTENT != 0 || size % MIN_ALIGN_CONTENT != 0) {
        fail.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES
                      ::SUB_MODULES_INIT_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_PARAM_CONFLICT;
        return fail;
    }
    if (size == 0) {
        fail.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES
                      ::SUB_MODULES_INIT_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_ZERO_SIZE;
        return fail;
    }

    // 边界检查：防止移位溢出和数组越界
    if (MAX_SUPPORT_ORDER > DESINGED_MAX_SUPPORT_ORDER ||
        (12ULL + MAX_SUPPORT_ORDER + 1) > 63) {
        fail.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES
                      ::SUB_MODULES_INIT_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_ORDER_TOO_LARGE;
        return fail;
    }

    const uint64_t package_count = size / MIN_ALIGN_CONTENT;

    // 分配节点数组（最坏情况：全分裂到 order 0）
    nodes_array = new nodes_array_t(package_count * (2 + MAX_SUPPORT_ORDER));
    submodules_result_container = nodes_array->second_stage_init();
    if (error_kurd(submodules_result_container)) {
        return submodules_result_container;
    }

    // 计算每个 order 在混合位图中的起始位偏移
    uint64_t accumulated_base = 0;
    uint64_t order_entries_count = size / _4KB_PAGESIZE;  // 总 4KB 页数
    for (int i = 0; i <= MAX_SUPPORT_ORDER; ++i) {
        order_bitmap_bases[i] = accumulated_base;
        accumulated_base += order_entries_count;
        order_entries_count >>= 1;
    }

    // 分配混合位图（使用精确大小，避免浪费）
    mixed_pages_bitmap = new BCB_mixed_pages_bitmap(accumulated_base);
    submodules_result_container = mixed_pages_bitmap->second_stage_init();
    if (error_kurd(submodules_result_container)) {
        return submodules_result_container;
    }

    // 构建链表的 lambda（每个 order 分配 package_count 个块，order 0 特殊处理）
    auto build_order_list = [&](uint8_t order,
                               phyaddr_t offset_in_segment,
                               uint64_t nodes_array_base,
                               uint64_t blocks_count) -> KURD_t {
        if (blocks_count == 0) return default_success();

        uint64_t block_size = 1ULL << (12 + order);
        uint64_t base_block_idx = offset_in_segment / block_size;

        free_page_node_t node;
        KURD_t sub_result;

        for (uint64_t i = 0; i < blocks_count; ++i) {
            node.inhcb_offset_specify_idx = base_block_idx + i;
            node.next_node_array_idx = (i < blocks_count - 1) ? (nodes_array_base + i + 1) : 0xFFFFFFFF;

            sub_result = nodes_array->regist_node(nodes_array_base + i);
            if (error_kurd(sub_result)) return sub_result;

            sub_result = nodes_array->modify_node(nodes_array_base + i, node);
            if (error_kurd(sub_result)) return sub_result;
        }

        // 创建单链表头
        orders_lists[order] = new free_page_node_t_in_array_SLL(
            order, nodes_array_base, blocks_count);

        // 标记位图：注意加上 order 的偏移
        uint64_t bit_start = order_bitmap_bases[order] + base_block_idx;
        mixed_pages_bitmap->bits_set(bit_start, blocks_count, true);

        return default_success();
    };

    // 从最高阶开始初始化
    phyaddr_t scanner = 0;
    uint64_t current_node_base = 0;

    // 高阶（order MAX 到 1）：每个 order 分配 package_count 个块
    for (int order = MAX_SUPPORT_ORDER; order >= 1; --order) {
        KURD_t res = build_order_list(order, scanner, current_node_base, package_count);
        if (error_kurd(res)) return res;

        current_node_base += package_count;
        scanner += (1ULL << (12 + order)) * package_count;
    }

    // order 0：剩余全部页（通常是 package_count * 2）
    uint64_t remaining_pages = (size - scanner) / _4KB_PAGESIZE;
    KURD_t res = build_order_list(0, scanner, current_node_base, remaining_pages);
    if (error_kurd(res)) return res;

    return default_success();
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::default_kurd()
{
    KURD_t kurd(0,0,module_code::MEMORY,MEMMODULE_LOCAIONS::in_module_location_code::LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK,0,0,err_domain::CORE_MODULE);
    return kurd;
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::default_success()
{
    KURD_t kurd=default_kurd();
    kurd.result=result_code::SUCCESS;
    kurd.level=level_code::INFO;
    return kurd;
}

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::default_error()
{
    KURD_t kurd=default_kurd();
    kurd=set_fatal_result_level(kurd);
    return kurd;
}

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::default_fatal()
{
    KURD_t kurd=default_kurd();
    kurd.result=result_code::FATAL;
    kurd.level=level_code::FATAL;
    return kurd;
}

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::nodes_array_t::second_stage_init()
{
    if(this==first_BCB->nodes_array){
        nodes=pre_defined_nodes_array;
    }else{//走页框/first_BCB分配页，并且映射

    }
    nodes_bitmap=new  Ktemplats::kernel_bitmap(nodes_maxcount);
    if(nodes_bitmap==nullptr){
        if(this==first_BCB->nodes_array){
        KURD_t fatal=default_fatal();
        fatal.event_code=0;
        fatal.reason=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY_EVENTS_CODES::INIT_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_BITMAP_INIT_FAIL;
        return fatal;
        }else{
            KURD_t error=default_error();
        error.event_code=0;
        error.reason=MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY_EVENTS_CODES::INIT_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_BITMAP_INIT_FAIL;
        return error;
        }
    }
    return default_success();
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::nodes_array_t::default_kurd()
{
    KURD_t kurd(0,0,module_code::MEMORY,MEMMODULE_LOCAIONS::in_module_location_code::LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY,0,0,err_domain::CORE_MODULE);
    return kurd;
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::nodes_array_t::default_success()
{
    KURD_t kurd=default_kurd();
    kurd.result=result_code::SUCCESS;
    kurd.level=level_code::INFO;
    return kurd;
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::nodes_array_t::default_error()
{
    KURD_t kurd=default_kurd();
    kurd=set_result_fail_and_error_level(kurd);
    return kurd;
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::nodes_array_t::default_fatal()
{
    KURD_t kurd=default_kurd();
    kurd.result=result_code::FATAL;
    kurd.level=level_code::FATAL;
    return kurd;
}
FreePagesAllocator::free_pages_in_seg_control_block::nodes_array_t::nodes_array_t(uint32_t nodes_maxcount)
{
    this->nodes_maxcount=nodes_maxcount;
}
FreePagesAllocator::free_pages_in_seg_control_block::BCB_mixed_pages_bitmap::BCB_mixed_pages_bitmap(uint64_t entryies_count)
{
    this->entryies_count=entryies_count;
}

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::BCB_mixed_pages_bitmap::second_stage_init()
{
    if(this==first_BCB->mixed_pages_bitmap){
        this->bitmap=pre_defined_mixed_pages_bitmap;
    }else{

    }
    return KURD_t();
}
