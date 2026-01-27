#include "memory/FreePagesAllocator.h"
FreePagesAllocator::free_pages_in_seg_control_block::free_page_node_t_in_array_SLL::free_page_node_t_in_array_SLL
(uint8_t order, uint32_t head_idx, uint32_t nodes_count,nodes_array_t*nodes_array_arg)
{
    this->order = order;
    this->head_idx = head_idx;
    this->nodes_count = nodes_count;
    this->nodes_array = nodes_array_arg;
}

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::free_page_node_t_in_array_SLL::default_kurd()
{
    KURD_t kurd(0,0,module_code::MEMORY,MEMMODULE_LOCAIONS::LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST,0,0,err_domain::CORE_MODULE);
    return kurd;
}

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::free_page_node_t_in_array_SLL::default_success()
{
    KURD_t kurd=default_kurd();
    kurd.result=result_code::SUCCESS;
    kurd.level=level_code::INFO;
    return kurd;
}

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::free_page_node_t_in_array_SLL::default_error()
{
    KURD_t kurd=default_kurd();
    kurd=set_result_fail_and_error_level(kurd);
    return kurd;
}

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::free_page_node_t_in_array_SLL::default_fatal()
{
    KURD_t kurd=default_kurd();
    kurd.result=result_code::FATAL;
    kurd.level=level_code::FATAL;
    return kurd;
}

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::free_page_node_t_in_array_SLL::push_head(uint32_t in_bcb_page_location_idx)
{
    free_page_node_t node={
        .in_bcb_offset_specify_idx = in_bcb_page_location_idx,
        .next_node_array_idx = 0xFFFFFFFF
    };
    uint32_t in_nodes_array_idx;
    KURD_t get_result = nodes_array->alloc_node(in_nodes_array_idx, node);
    if(get_result.result != result_code::SUCCESS) {
        // 直接返回从nodes_array->alloc_node获得的错误信息
        return get_result;
    }
    // 将当前头节点作为新节点的下一个节点
    node.next_node_array_idx = head_idx;
    
    // 更新头节点为新节点
    KURD_t modify_result = nodes_array->modify_node(in_nodes_array_idx, node);
    if(modify_result.result != result_code::SUCCESS) {
        return modify_result;
    }
    
    // 更新头索引
    head_idx = in_nodes_array_idx;
    
    // 增加节点计数
    KURD_t inc_result = nodes_count_inc();
    if(inc_result.result != result_code::SUCCESS) {
        return inc_result;
    }
    
    // 成功推送
    KURD_t result = default_success();
    result.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::EVENT_CODE_PUSH_HEAD;
    return result;
}

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::free_page_node_t_in_array_SLL::pop_head(uint32_t& in_bcb_page_location_idx)
{
    if(head_idx == 0xFFFFFFFF) {
        // 链表为空
        KURD_t result = default_error();
        result.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::
                        POP_HEAD_EVENTS::FAIL_REASONS_CODE::FAIL_REASON_CODE_LIST_EMPTY;
        result.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::EVENT_CODE_POP_HEAD;
        return result;
    }
    
    free_page_node_t node;
    KURD_t get_result = nodes_array->get_node(head_idx, node);
    
    if(get_result.result != result_code::SUCCESS) {
        // 直接返回从nodes_array->get_node获得的错误信息
        return get_result;
    }
    
    in_bcb_page_location_idx = node.in_bcb_offset_specify_idx;
    nodes_array->free_node(head_idx);
    // 更新头节点为下一个节点
    head_idx = node.next_node_array_idx;
    
    // 减少节点计数
    KURD_t dec_result = nodes_count_dec();
    if(dec_result.result != result_code::SUCCESS) {
        return dec_result;
    }
    
    // 成功弹出
    KURD_t result = default_success();
    result.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::EVENT_CODE_POP_HEAD;
    return result;
}

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::free_page_node_t_in_array_SLL::nodes_count_inc()
{
    if(nodes_count == UINT32_MAX) {
        KURD_t result = default_error();
        result.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::
                        INC_COUNT_EVENTS::FAIL_REASONS_CODE::FAIL_REASON_CODE_COUNT_OVERFLOW;
        result.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::EVENT_CODE_INC_COUNT;
        return result;
    }
    nodes_count++;
    
    KURD_t result = default_success();
    result.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::EVENT_CODE_INC_COUNT;
    return result;
}

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::free_page_node_t_in_array_SLL::nodes_count_dec()
{
    if(nodes_count == 0) {
        KURD_t result = default_error();
        result.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::
                        DEC_COUNT_EVENTS::FAIL_REASONS_CODE::FAIL_REASON_CODE_COUNT_UNDERFLOW;
        result.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::EVENT_CODE_DEC_COUNT;
        return result;
    }
    nodes_count--;
    
    KURD_t result = default_success();
    result.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::EVENT_CODE_DEC_COUNT;
    return result;
}

KURD_t FreePagesAllocator::free_pages_in_seg_control_block::free_page_node_t_in_array_SLL::del_node_by_bcb_location_idx(uint32_t in_bcb_page_location_idx)
{
    KURD_t success = default_success();
    success.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::EVENT_CODE_DEL_NODE_BY_PAGE_IDX;

    KURD_t fail = default_error();
    fail.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::EVENT_CODE_DEL_NODE_BY_PAGE_IDX;

    KURD_t fatal = default_fatal();
    fatal.event_code = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::EVENT_CODE_DEL_NODE_BY_PAGE_IDX;

    // 如果链表为空，直接返回错误
    if (head_idx == 0xFFFFFFFF) {
        fail.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::DEL_NODE_BY_PAGE_IDX_EVENTS::FAIL_REASONS_CODE::FAIL_REASON_CODE_TARGET_NODE_NOT_FOUND;
        return fail;
    }

    // 检查头节点是否是要删除的节点
    free_page_node_t head_node;
    KURD_t get_result = nodes_array->get_node(head_idx, head_node);
    if (get_result.result != result_code::SUCCESS) {
        // 直接返回从nodes_array->get_node获得的错误信息
        return get_result;
    }

    if (head_node.in_bcb_offset_specify_idx == in_bcb_page_location_idx) {
        // 删除头节点
        head_idx = head_node.next_node_array_idx;
        nodes_array->free_node(head_idx);
        
        // 减少节点计数
        KURD_t dec_result = nodes_count_dec();
        if(dec_result.result != result_code::SUCCESS) {
            return dec_result;
        }
        
        return success;
    }

    // 遍历链表寻找目标节点
    uint32_t current_idx = head_idx;
    free_page_node_t current_node = head_node;
    uint32_t count = 1;  // 已经遍历了一个节点（头节点）

    while (current_node.next_node_array_idx != 0xFFFFFFFF) {
        uint32_t next_idx = current_node.next_node_array_idx;
        free_page_node_t next_node;
        
        get_result = nodes_array->get_node(next_idx, next_node);
        if (get_result.result != result_code::SUCCESS) {
            // 直接返回从nodes_array->get_node获得的错误信息
            return get_result;
        }

        count++;  // 计数增加

        if (next_node.in_bcb_offset_specify_idx == in_bcb_page_location_idx) {
            // 找到目标节点，将其从前驱节点移除
            current_node.next_node_array_idx = next_node.next_node_array_idx;
            KURD_t modify_result = nodes_array->modify_node(current_idx, current_node);
            if (modify_result.result != result_code::SUCCESS) {
                return modify_result;
            }
            
            // 释放目标节点
            nodes_array->free_node(next_idx);
            
            // 减少节点计数
            KURD_t dec_result = nodes_count_dec();
            if(dec_result.result != result_code::SUCCESS) {
                return dec_result;
            }
            
            return success;
        }

        current_idx = next_idx;
        current_node = next_node;
    }

    // 检查链表长度是否与nodes_count字段相等
    if (count != nodes_count) {
        fatal.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::DEL_NODE_BY_PAGE_IDX_EVENTS::FATAL_REASONS_CODE::LENGTH_CHECK_FAIL;
        return fatal;
    }

    // 没有找到目标节点
    fail.reason = MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES::DEL_NODE_BY_PAGE_IDX_EVENTS::FAIL_REASONS_CODE::FAIL_REASON_CODE_TARGET_NODE_NOT_FOUND;
    return fail;
}
