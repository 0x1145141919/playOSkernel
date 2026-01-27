#include "memory/FreePagesAllocator.h"


KURD_t FreePagesAllocator::free_pages_in_seg_control_block::nodes_array_t::default_kurd()
{
    KURD_t kurd(0,0,module_code::MEMORY,MEMMODULE_LOCAIONS::LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY,0,0,err_domain::CORE_MODULE);
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

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::nodes_array_t::free_node(uint32_t node_idx)
{
    // 检查节点索引是否超出范围
    if (node_idx >= nodes_maxcount) {
        KURD_t result = default_error();
        result.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY_EVENTS_CODES::
                        ALL_EVENTS_SHARED::FAIL_REASONS_CODE::FAIL_REASON_CODE_INVALID_NODE_INDEX;
        return result;
    }
    // 检查节点是否已被分配（即是否被占用）
    bool is_allocated = nodes_bitmap->bit_get(node_idx);
    if (!is_allocated) {
        // 节点未被分配，不能释放，返回错误
        KURD_t result = default_error();
        result.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY_EVENTS_CODES::
                        ALL_EVENTS_SHARED::FAIL_REASONS_CODE::FAIL_REASON_CODE_NODE_NOT_ALLOCATED;
        return result;
    }

    // 清除位图中对应位置的位，表示该节点现在是空闲的
    nodes_bitmap->bit_set(node_idx, false);


    return default_success();
}

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::nodes_array_t::regist_node(uint32_t node_idx)
{
    // 检查节点索引是否超出范围
    if (node_idx >= nodes_maxcount) {
        KURD_t result = default_error();
        result.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY_EVENTS_CODES::
                        ALL_EVENTS_SHARED::FAIL_REASONS_CODE::FAIL_REASON_CODE_INVALID_NODE_INDEX;
        return result;
    }

    // 检查节点是否已经被注册（即是否已被占用）
    bool is_allocated = nodes_bitmap->bit_get(node_idx);
    if (is_allocated) {
        KURD_t result = default_error();
        result.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY_EVENTS_CODES::
                        ALL_EVENTS_SHARED::FAIL_REASONS_CODE::FAIL_REASON_CODE_NODE_ALREADY_ALLOCATED;
        return result;
    }

    // 设置位图中对应位置的位，表示该节点现在被占用
    nodes_bitmap->bit_set(node_idx, true);

    return default_success();
}

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::nodes_array_t::get_node(uint32_t node_idx, free_page_node_t& node)
{
    // 检查节点索引是否超出范围
    if (node_idx >= nodes_maxcount) {
        KURD_t result = default_error();
        result.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY_EVENTS_CODES::
                        ALL_EVENTS_SHARED::FAIL_REASONS_CODE::FAIL_REASON_CODE_INVALID_NODE_INDEX;
        return result;
    }

    // 检查节点是否存在
    bool is_allocated = nodes_bitmap->bit_get(node_idx);
    if (!is_allocated) {
        // 节点未被分配，不能获取，返回错误
        KURD_t result = default_error();
        result.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY_EVENTS_CODES::
                        ALL_EVENTS_SHARED::FAIL_REASONS_CODE::FAIL_REASON_CODE_NODE_NOT_ALLOCATED;
        return result;
    }

    // 获取节点数据
    node = nodes[node_idx];

    return default_success();
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::nodes_array_t::alloc_node(uint32_t &node_idx, free_page_node_t &node)
{
    uint64_t result_base_idx;
    int search_result = nodes_bitmap->avaliable_bit_search(result_base_idx);
    
    if (search_result == 0) { // 找到一个未分配的节点
        // 标记为已分配
        nodes_bitmap->bit_set(static_cast<uint32_t>(result_base_idx), true);
        
        // 返回节点索引
        node_idx = static_cast<uint32_t>(result_base_idx);
        nodes[node_idx] = node;
        // 成功也需要设置正确的事件代码
        KURD_t result = default_success();
        result.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY_EVENTS_CODES::EVENT_CODE_ALLOC_NODE;
        return result;
    }
    
    // 没有找到空闲节点
    KURD_t result = default_error();
    result.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY_EVENTS_CODES::
                    ALLOC_NODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_NO_FREE_NODE;
    result.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY_EVENTS_CODES::EVENT_CODE_ALLOC_NODE;
    return result;
}
KURD_t FreePagesAllocator::free_pages_in_seg_control_block::nodes_array_t::modify_node(uint32_t node_idx, free_page_node_t node)
{
    // 检查节点索引是否超出范围
    if (node_idx >= nodes_maxcount) {
        KURD_t result = default_error();
        result.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY_EVENTS_CODES::
                        ALL_EVENTS_SHARED::FAIL_REASONS_CODE::FAIL_REASON_CODE_INVALID_NODE_INDEX;
        return result;
    }

    // 检查节点是否存在
    bool is_allocated = nodes_bitmap->bit_get(node_idx);
    if (!is_allocated) {
        // 节点未被分配，不能修改，返回错误
        KURD_t result = default_error();
        result.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY_EVENTS_CODES::
                        ALL_EVENTS_SHARED::FAIL_REASONS_CODE::FAIL_REASON_CODE_NODE_NOT_ALLOCATED;
        return result;
    }

    // 修改节点数据
    nodes[node_idx] = node;

    return default_success();
}

FreePagesAllocator::free_pages_in_seg_control_block::nodes_array_t::nodes_array_t(uint32_t nodes_maxcount)
{
    this->nodes_maxcount=nodes_maxcount;
}
FreePagesAllocator::free_pages_in_seg_control_block::BCB_mixed_pages_bitmap::BCB_mixed_pages_bitmap(uint64_t entryies_count)
{
    this->entryies_count=entryies_count;
}
