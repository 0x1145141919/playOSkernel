#include "Scheduler/per_processor_scheduler.h"
#include "memory/kpoolmemmgr.h"
#include "Interrupt_system/Interrupt.h"
task_node null_task_node={
    .pre_node=0,
    .next_node=0,
    .task_ptr=nullptr
};
KURD_t per_processor_scheduler::task_pool::default_kurd()
{
    return KURD_t(0,0,module_code::SCHEDULER,Scheduler::scheduler_task_pool,0,0,err_domain::CORE_MODULE);
}
KURD_t per_processor_scheduler::task_pool::default_success()
{
    KURD_t kurd= default_kurd();
    kurd.result=result_code::SUCCESS;
    kurd.level=level_code::INFO;
    return kurd;
}
KURD_t per_processor_scheduler::task_pool::default_fail()
{
    return set_fatal_result_level(default_kurd());
}
// per_processor_scheduler构造函数
per_processor_scheduler::per_processor_scheduler() : processor_self_task_pool() {
    // 初始化所有ready_queue的pool指针
    for (int i = 0; i < max_ready_queue_count; i++) {
        new (&ready_queue[i]) tasks_dll(&processor_self_task_pool);
    }
    new (&blocked_queue) tasks_dll(&processor_self_task_pool);
    new (&dying_queue) tasks_dll(&processor_self_task_pool);
}

task::task(task_type_t task_type, uint64_t task_id, void *context)
{
    this->task_type=task_type;
    this->task_id=task_id;
    this->task_state=init;
    this->blocked_reason=invalid;
    this->context.kthread=(kthread_context*)context;
}
KURD_t per_processor_scheduler::task_pool::second_state_init()
{
    return task_pool_bitmap.second_stage_init();
}
KURD_t per_processor_scheduler::tasks_dll::default_kurd()
{
    return KURD_t(0,0,module_code::SCHEDULER,Scheduler::scheduler_task_dll,0,0,err_domain::CORE_MODULE);
}
KURD_t per_processor_scheduler::tasks_dll::default_success()
{
    KURD_t kurd= default_kurd();
    kurd.result=result_code::SUCCESS;
    kurd.level=level_code::INFO;
    return kurd;
}
KURD_t per_processor_scheduler::tasks_dll::default_fail()
{
    return set_result_fail_and_error_level(default_kurd());
}
KURD_t per_processor_scheduler::tasks_dll::default_fatal()
{
    return set_fatal_result_level(default_kurd());
}
uint32_t per_processor_scheduler::task_pool::alloc(KURD_t &result_kurd)
{
    uint64_t avail;
    int result=task_pool_bitmap.avaliable_bit_search(avail);
    if(result==OS_SUCCESS){
        task_pool_bitmap.used_bit_count_add(1);
        result=task_pool_table.enable_idx(avail);
        return avail;
    }
    return ~0;
}
task_node &per_processor_scheduler::task_pool::get_task_node(uint32_t index)
{
    task_node* res= task_pool_table.get(index);
    if(res)return *res;
    return null_task_node;
}
KURD_t per_processor_scheduler::task_pool::free(uint32_t index)
{
    int result=task_pool_table.release(index);
    if(result==OS_SUCCESS){
        if(task_pool_bitmap.bit_get(index)){
            task_pool_bitmap.bit_set(index,false);
            task_pool_bitmap.used_bit_count_sub(1);
        }else{
            //返回已经释放
        }
        
        //返回成功
    }else{
        //返回越界
    }
}

// queue类成员函数实现
KURD_t per_processor_scheduler::tasks_dll::push_head(uint32_t index) {
    KURD_t success = default_success();
    KURD_t fail = default_fail();
    success.event_code = Scheduler::scheduler_task_dll_events::push_head;
    fail.event_code = Scheduler::scheduler_task_dll_events::push_head;

    // 检查pool指针是否有效
    if (!pool) {
        fail.reason = Scheduler::scheduler_task_dll_events::push_head_results::fail_reasons::null_pool;
        return fail;
    }
    
    // 如果队列为空
    if (head == INVALID_NODE_INDEX) {
        head = index;
        tail = index;
        count = 1;
        
        // 初始化节点的前后指针
        task_node& node = pool->get_task_node(index);
        node.pre_node = INVALID_NODE_INDEX;
        node.next_node = INVALID_NODE_INDEX;
    } else {
        // 将新节点插入到头部
        task_node& new_node = pool->get_task_node(index);
        task_node& old_head = pool->get_task_node(head);
        
        // 设置新节点
        new_node.pre_node = INVALID_NODE_INDEX;
        new_node.next_node = head;
        
        // 更新原头节点
        old_head.pre_node = index;
        
        // 更新队列头指针
        head = index;
        count++;
    }
    return success;
}

KURD_t per_processor_scheduler::tasks_dll::push_tail(uint32_t index) {
    KURD_t success = default_success();
    KURD_t fail = default_fail();
    success.event_code = Scheduler::scheduler_task_dll_events::push_tail;
    fail.event_code = Scheduler::scheduler_task_dll_events::push_tail;

    // 检查pool指针是否有效
    if (!pool) {
        fail.reason = Scheduler::scheduler_task_dll_events::push_tail_results::fail_reasons::null_pool;
        return fail;
    }
    
    // 如果队列为空
    if (tail == INVALID_NODE_INDEX) {
        head = index;
        tail = index;
        count = 1;
        
        // 初始化节点的前后指针
        task_node& node = pool->get_task_node(index);
        node.pre_node = INVALID_NODE_INDEX;
        node.next_node = INVALID_NODE_INDEX;
    } else {
        // 将新节点插入到尾部
        task_node& new_node = pool->get_task_node(index);
        task_node& old_tail = pool->get_task_node(tail);
        
        // 设置新节点
        new_node.pre_node = tail;
        new_node.next_node = INVALID_NODE_INDEX;
        
        // 更新原尾节点
        old_tail.next_node = index;
        
        // 更新队列尾指针
        tail = index;
        count++;
    }
    return success;
}

KURD_t per_processor_scheduler::tasks_dll::pop_head(uint32_t& index) {
    KURD_t success = default_success();
    KURD_t fail = default_fail();
    success.event_code = Scheduler::scheduler_task_dll_events::pop_head;
    fail.event_code = Scheduler::scheduler_task_dll_events::pop_head;

    // 检查pool指针是否有效
    if (!pool) {
        index = INVALID_NODE_INDEX;
        fail.reason = Scheduler::scheduler_task_dll_events::pop_head_results::fail_reasons::null_pool;
        return fail;
    }
    
    if (head == INVALID_NODE_INDEX) {
        // 队列为空
        index = INVALID_NODE_INDEX;
        fail.reason = Scheduler::scheduler_task_dll_events::pop_head_results::fail_reasons::empty;
        return fail;
    }
    
    index = head;
    if (head == tail) {
        // 队列只有一个元素
        head = INVALID_NODE_INDEX;
        tail = INVALID_NODE_INDEX;
        count = 0;
    } else {
        // 获取当前头节点和下一个节点
        task_node& current_head = pool->get_task_node(head);
        uint32_t next_index = current_head.next_node;
        task_node& next_node = pool->get_task_node(next_index);
        
        // 更新下一个节点的前向指针
        next_node.pre_node = INVALID_NODE_INDEX;
        
        // 更新队列头指针
        head = next_index;
        count--;
    }
    return success;
}

KURD_t per_processor_scheduler::tasks_dll::pop_tail(uint32_t& index) {
    KURD_t success = default_success();
    KURD_t fail = default_fail();
    success.event_code = Scheduler::scheduler_task_dll_events::pop_tail;
    fail.event_code = Scheduler::scheduler_task_dll_events::pop_tail;

    // 检查pool指针是否有效
    if (!pool) {
        index = INVALID_NODE_INDEX;
        fail.reason = Scheduler::scheduler_task_dll_events::pop_tail_results::fail_reasons::null_pool;
        return fail;
    }
    
    if (tail == INVALID_NODE_INDEX) {
        // 队列为空
        index = INVALID_NODE_INDEX;
        fail.reason = Scheduler::scheduler_task_dll_events::pop_tail_results::fail_reasons::empty;
        return fail;
    }
    
    index = tail;
    if (head == tail) {
        // 队列只有一个元素
        head = INVALID_NODE_INDEX;
        tail = INVALID_NODE_INDEX;
        count = 0;
    } else {
        // 获取当前尾节点和前一个节点
        task_node& current_tail = pool->get_task_node(tail);
        uint32_t prev_index = current_tail.pre_node;
        task_node& prev_node = pool->get_task_node(prev_index);
        
        // 更新前一个节点的后向指针
        prev_node.next_node = INVALID_NODE_INDEX;
        
        // 更新队列尾指针
        tail = prev_index;
        count--;
    }
    return success;
}

KURD_t per_processor_scheduler::tasks_dll::insert_after(uint32_t pre_index, uint32_t insertor_index) {
    KURD_t success = default_success();
    KURD_t fail = default_fail();
    success.event_code = Scheduler::scheduler_task_dll_events::insert_after;
    fail.event_code = Scheduler::scheduler_task_dll_events::insert_after;

    // 检查pool指针是否有效
    if (!pool) {
        fail.reason = Scheduler::scheduler_task_dll_events::insert_after_results::fail_reasons::null_pool;
        return fail;
    }
    
    // 检查前置节点是否存在
    if (pre_index == INVALID_NODE_INDEX) {
        fail.reason = Scheduler::scheduler_task_dll_events::insert_after_results::fail_reasons::invalid_pre_index;
        return fail;
    }
    
    task_node& pre_node = pool->get_task_node(pre_index);
    task_node& insertor_node = pool->get_task_node(insertor_index);
    
    uint32_t next_index = pre_node.next_node;
    
    // 设置插入节点的链接
    insertor_node.pre_node = pre_index;
    insertor_node.next_node = next_index;
    
    // 更新前置节点的后向链接
    pre_node.next_node = insertor_index;
    
    // 如果前置节点是尾节点，更新尾指针
    if (pre_index == tail) {
        tail = insertor_index;
    }
    
    // 如果后继节点存在，更新其前向链接
    if (next_index != INVALID_NODE_INDEX) {
        task_node& next_node = pool->get_task_node(next_index);
        next_node.pre_node = insertor_index;
    }
    
    count++;
    return success;
}

KURD_t per_processor_scheduler::tasks_dll::remove(uint32_t index) {
    KURD_t success = default_success();
    KURD_t fail = default_fail();
    success.event_code = Scheduler::scheduler_task_dll_events::remove;
    fail.event_code = Scheduler::scheduler_task_dll_events::remove;

    // 检查pool指针是否有效
    if (!pool) {
        fail.reason = Scheduler::scheduler_task_dll_events::remove_results::fail_reasons::null_pool;
        return fail;
    }
    
    // 检查节点是否存在
    if (index == INVALID_NODE_INDEX) {
        fail.reason = Scheduler::scheduler_task_dll_events::remove_results::fail_reasons::invalid_index;
        return fail;
    }
    
    task_node& node = pool->get_task_node(index);
    
    uint32_t pre_index = node.pre_node;
    uint32_t next_index = node.next_node;
    
    // 更新前向节点的链接
    if (pre_index != INVALID_NODE_INDEX) {
        task_node& pre_node = pool->get_task_node(pre_index);
        pre_node.next_node = next_index;
    } else {
        // 要删除的是头节点
        head = next_index;
    }
    
    // 更新后向节点的链接
    if (next_index != INVALID_NODE_INDEX) {
        task_node& next_node = pool->get_task_node(next_index);
        next_node.pre_node = pre_index;
    } else {
        // 要删除的是尾节点
        tail = pre_index;
    }
    
    // 重置被删除节点的链接
    node.pre_node = INVALID_NODE_INDEX;
    node.next_node = INVALID_NODE_INDEX;
    
    if (count > 0) {
        count--;
    }
    return success;
}

uint32_t per_processor_scheduler::tasks_dll::get_count() {
    return count;
}

uint32_t per_processor_scheduler::tasks_dll::get_head() {
    return head;
}

uint32_t per_processor_scheduler::tasks_dll::get_tail() {
    return tail;
}

// iterator类成员函数实现
uint32_t per_processor_scheduler::tasks_dll::iterator::get_index() {
    return index;
}

task_node& per_processor_scheduler::tasks_dll::iterator::operator*() {
    if (pool_ptr && index != INVALID_NODE_INDEX) {
        return pool_ptr->get_task_node(index);
    }
    return null_task_node;
}

per_processor_scheduler::tasks_dll::iterator& per_processor_scheduler::tasks_dll::iterator::operator++() {
    if (pool_ptr && index != INVALID_NODE_INDEX) {
        task_node& node = pool_ptr->get_task_node(index);
        index = node.next_node;
    }
    return *this;
}

per_processor_scheduler::tasks_dll::iterator& per_processor_scheduler::tasks_dll::iterator::operator--() {
    if (pool_ptr && index != INVALID_NODE_INDEX) {
        task_node& node = pool_ptr->get_task_node(index);
        index = node.pre_node;
    }
    return *this;
}

bool per_processor_scheduler::tasks_dll::iterator::operator!=(const iterator& other) {
    return index != other.index || pool_ptr != other.pool_ptr;
}

bool per_processor_scheduler::tasks_dll::iterator::operator==(const iterator& other) {
    return index == other.index && pool_ptr == other.pool_ptr;
}
void timer_cpp_enter(x64_Interrupt_saved_context_no_errcode *frame)
{

}
void per_processor_scheduler::schedule_and_switch()
{//先暂时只考虑单队列

}