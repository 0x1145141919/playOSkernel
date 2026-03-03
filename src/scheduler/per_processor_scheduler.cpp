#include "Scheduler/per_processor_scheduler.h"
#include "memory/kpoolmemmgr.h"
#include "memory/phygpsmemmgr.h"
#include "util/kout.h"
#include "Interrupt_system/Interrupt.h"
#include "core_hardwares/lapic.h"
#include "panic.h"
// 添加适配函数来解决类型不匹配问题
extern "C" void secure_hlt();
static void secure_hlt_wrapper(void* unused) {
    (void)unused;  // 忽略未使用的参数
    secure_hlt();  // 返回值不会被使用，但满足函数签名要求
}
per_processor_scheduler** all_scheduler_ptr;
namespace {
constexpr uint64_t kthread_yield_saved_stack_delta = 16 * sizeof(uint64_t);
constexpr uint32_t invalid_task_id = ~0u;

static inline KURD_t self_scheduler_default_kurd()
{
    return KURD_t(0, 0, module_code::SCHEDULER, Scheduler::self_scheduler, 0, 0, err_domain::CORE_MODULE);
}

static inline KURD_t make_self_scheduler_fatal(uint8_t event_code, uint16_t reason)
{
    KURD_t kurd = self_scheduler_default_kurd();
    kurd.event_code = event_code;
    kurd.reason = reason;
    return set_fatal_result_level(kurd);
}

static inline void panic_with_kurd(const x64_Interrupt_saved_context_no_errcode *frame, KURD_t kurd)
{
    panic_info_inshort inshort{
        .is_bug = true,
        .is_policy = true,
        .is_hw_fault = false,
        .is_mem_corruption = false,
        .is_escalated = false
    };
    panic_context::x64_context panic_ctx = Panic::convert_to_panic_context(
        const_cast<x64_Interrupt_saved_context_no_errcode*>(frame)
    );
    Panic::panic(default_panic_behaviors_flags,
        nullptr,
        &panic_ctx,
        &inshort,
        kurd
    );
}

static inline void panic_with_kurd(KURD_t kurd)
{
    panic_info_inshort inshort{
        .is_bug = true,
        .is_policy = true,
        .is_hw_fault = false,
        .is_mem_corruption = false,
        .is_escalated = false
    };
    Panic::panic(default_panic_behaviors_flags,
        nullptr,
        nullptr,
        &inshort,
        kurd
    );
}

static inline void convert_interrupt_no_err_to_basic(const x64_Interrupt_saved_context_no_errcode *frame,
                                                     x64_basic_context *out)
{
    if (!frame || !out) {
        return;
    }
    out->rax = frame->rax;
    out->rbx = frame->rbx;
    out->rcx = frame->rcx;
    out->rdx = frame->rdx;
    out->rsi = frame->rsi;
    out->rdi = frame->rdi;
    out->rbp = frame->rbp;
    out->rsp = frame->rsp;
    out->r8 = frame->r8;
    out->r9 = frame->r9;
    out->r10 = frame->r10;
    out->r11 = frame->r11;
    out->r12 = frame->r12;
    out->r13 = frame->r13;
    out->r14 = frame->r14;
    out->r15 = frame->r15;
    out->rip = frame->rip;
    out->rflags = frame->rflags;
}

static inline void convert_kthread_yield_raw_to_basic(const kthread_yield_raw_context *context,
                                                      x64_basic_context *out)
{
    if (!context || !out) {
        return;
    }
    out->rax = context->rax;
    out->rbx = context->rbx;
    out->rcx = context->rcx;
    out->rdx = context->rdx;
    out->rsi = context->rsi;
    out->rdi = context->rdi;
    out->rbp = context->rbp;
    out->r8 = context->r8;
    out->r9 = context->r9;
    out->r10 = context->r10;
    out->r11 = context->r11;
    out->r12 = context->r12;
    out->r13 = context->r13;
    out->r14 = context->r14;
    out->r15 = context->r15;
    out->rflags = context->rflags;

    const uint64_t real_rsp = context->rsp + kthread_yield_saved_stack_delta;
    const auto *ret_slot = reinterpret_cast<const uint64_t *>(real_rsp);
    out->rsp = real_rsp;
    out->rip = *ret_slot;
    out->rsp+= sizeof(uint64_t);
}
} // namespace

task_node null_task_node={
    .pre_node=0,
    .next_node=0,
    .task_ptr=nullptr
};

KURD_t per_processor_scheduler::default_kurd()
{
    return KURD_t(0,0,module_code::SCHEDULER,Scheduler::self_scheduler,0,0,err_domain::CORE_MODULE);
}

KURD_t per_processor_scheduler::default_success()
{
    KURD_t kurd = default_kurd();
    kurd.result = result_code::SUCCESS;
    kurd.level = level_code::INFO;
    return kurd;
}

KURD_t per_processor_scheduler::default_fail()
{
    return set_result_fail_and_error_level(default_kurd());
}

KURD_t per_processor_scheduler::default_fatal()
{
    return set_fatal_result_level(default_kurd());
}

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
per_processor_scheduler::per_processor_scheduler(){
    const uint64_t page_count = (defual_scheduler_stack_size + 0xFFF) / 0x1000;
    KURD_t kurd{};
    uint8_t* scheduler_stack_base = static_cast<uint8_t*>(
        __wrapped_pgs_valloc(&kurd, page_count, KERNEL, 12)
    );
    // Use the top of the private stack (stack grows downward).
    scheduler_private_stack = reinterpret_cast<uint64_t*>(
        scheduler_stack_base + defual_scheduler_stack_size
    );
    // 初始化所有ready_queue的pool指针
    for (int i = 0; i < max_ready_queue_count; i++) {
        new (&ready_queue[i]) tasks_dll(&processor_self_task_pool);
    }
    new (&blocked_queue) tasks_dll(&processor_self_task_pool);
    new (&zombie_queue) tasks_dll(&processor_self_task_pool);
    kurd=processor_self_task_pool.second_state_init();
    if(!success_all_kurd(kurd)){
        panic_with_kurd(kurd);
    }
    create_kthread_param param={
        .result_kurd=KURD_t(),
        .kthread_stack_size=4096,
        .is_soon_ready=false
    };
    task* idle_task = create_kthread(
        secure_hlt_wrapper,nullptr,&param
    );
    if(!idle_task || !success_all_kurd(param.result_kurd)){
        panic_with_kurd(param.result_kurd);
    }
    idle_kthread_index=static_cast<uint32_t>(idle_task->location.in_pool_index);
}

extern "C" uint64_t* get_scheduler_private_stack_top(per_processor_scheduler* scheduler)
{
    return scheduler ? scheduler->get_scheduler_private_stack_top() : nullptr;
}

task::task(task_type_t task_type, uint64_t tid, void *context)
{
    this->task_type=task_type;
    this->tid=tid;
    this->location.processor_id=0;
    this->location.in_pool_index=INVALID_NODE_INDEX;
    this->task_state=init;
    this->blocked_reason=invalid;
    this->context.kthread=(kthread_context*)context;
}

uint64_t task::get_tid()
{
    return tid;
}

task_type_t task::get_task_type()
{
    return task_type;
}

uint32_t task::get_belonged_processor_id()
{
    return location.processor_id;
}

extern "C" void atoimc_kthread_load(x64_basic_context* context);
bool task::set_ready()
{
    if(task_state==task_state_t::init ||
       task_state==task_state_t::blocked ||
       task_state==task_state_t::running){
        task_state=task_state_t::ready;
        blocked_reason=task_blocked_reason_t::invalid;
        return true;
    }
    return false;
}
bool task::set_blocked()
{
    if(task_state==task_state_t::running){
        task_state=task_state_t::blocked;
        return true;
    }
    return false;
}
bool task::set_dead()
{
    if(task_state==task_state_t::zombie){
        task_state=task_state_t::dead;
        return true;
    }
    return false;
}
bool task::set_zombie()
{
    if(task_state==task_state_t::running || task_state==task_state_t::blocked || task_state==task_state_t::ready){
        task_state=task_state_t::zombie;
        return true;
    }
    return false;
}
task::~task()
{
}
void task::atomic_load()
{
    this->lastest_run_stamp=ktime::hardware_time::get_stamp();
    task_state=task_state_t::running;
    switch(task_type){
        case task_type_t::kthreadm:{
            atoimc_kthread_load(&context.kthread->regs);
        }
        case task_type_t::userthread:{

        }
        case task_type_t::vCPU:{

        }
        default:{
            //特殊kurd
            //return KURD_t(0,0,module_code::SCHEDULER,Scheduler::scheduler_task_pool,0,0,err_domain::CORE_MODULE);
        }
    }
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
    KURD_t success=default_success();
    KURD_t fail=default_fail();
    success.event_code=Scheduler::scheduler_task_pool_events::slot_alloc;
    fail.event_code=Scheduler::scheduler_task_pool_events::slot_alloc;
    uint64_t avail;
    int result=task_pool_bitmap.avaliable_bit_search(avail);
    if(result==OS_SUCCESS){
        task_pool_bitmap.used_bit_count_add(1);
        result=task_pool_table.enable_idx(avail);
        task_pool_bitmap.bit_set(avail,true);
        result_kurd=success;
        return avail;
    }
    result_kurd=fail;
    return ~0;
}
task_node *per_processor_scheduler::task_pool::get_task_node(uint32_t index)
{
    task_node* res= task_pool_table.get(index);
    if(res)return res;
    return nullptr;
}
KURD_t per_processor_scheduler::task_pool::free(uint32_t index)
{
    KURD_t success = default_success();
    KURD_t fail = default_fail();
    success.event_code = Scheduler::scheduler_task_pool_events::slot_free;
    fail.event_code = Scheduler::scheduler_task_pool_events::slot_free;
    int result=task_pool_table.release(index);
    if(result==OS_SUCCESS){
        if(task_pool_bitmap.bit_get(index)){
            task_pool_bitmap.bit_set(index,false);
            task_pool_bitmap.used_bit_count_sub(1);
            return success;
        }else{
            fail.reason = Scheduler::scheduler_task_pool_events::slot_free_results::fail_reasons::not_allocated;
            return fail;
        }
    }else{
        fail.reason = Scheduler::scheduler_task_pool_events::slot_free_results::fail_reasons::index_out_of_range;
        return fail;
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
        task_node* node = pool->get_task_node(index);
        if(!node){
            fail.reason = Scheduler::scheduler_task_dll_events::push_head_results::fail_reasons::null_pool;
            return fail;
        }
        node->pre_node = INVALID_NODE_INDEX;
        node->next_node = INVALID_NODE_INDEX;
    } else {
        // 将新节点插入到头部
        task_node* new_node = pool->get_task_node(index);
        task_node* old_head = pool->get_task_node(head);
        if(!new_node || !old_head){
            fail.reason = Scheduler::scheduler_task_dll_events::push_head_results::fail_reasons::null_pool;
            return fail;
        }
        
        // 设置新节点
        new_node->pre_node = INVALID_NODE_INDEX;
        new_node->next_node = head;
        
        // 更新原头节点
        old_head->pre_node = index;
        
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
        task_node* node = pool->get_task_node(index);
        if(!node){
            fail.reason = Scheduler::scheduler_task_dll_events::push_tail_results::fail_reasons::null_pool;
            return fail;
        }
        node->pre_node = INVALID_NODE_INDEX;
        node->next_node = INVALID_NODE_INDEX;
    } else {
        // 将新节点插入到尾部
        task_node* new_node = pool->get_task_node(index);
        task_node* old_tail = pool->get_task_node(tail);
        if(!new_node || !old_tail){
            fail.reason = Scheduler::scheduler_task_dll_events::push_tail_results::fail_reasons::null_pool;
            return fail;
        }
        
        // 设置新节点
        new_node->pre_node = tail;
        new_node->next_node = INVALID_NODE_INDEX;
        
        // 更新原尾节点
        old_tail->next_node = index;
        
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
        task_node* current_head = pool->get_task_node(head);
        if(!current_head){
            fail.reason = Scheduler::scheduler_task_dll_events::pop_head_results::fail_reasons::null_pool;
            return fail;
        }
        uint32_t next_index = current_head->next_node;
        task_node* next_node = pool->get_task_node(next_index);
        if(!next_node){
            fail.reason = Scheduler::scheduler_task_dll_events::pop_head_results::fail_reasons::null_pool;
            return fail;
        }
        
        // 更新下一个节点的前向指针
        next_node->pre_node = INVALID_NODE_INDEX;
        
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
        task_node* current_tail = pool->get_task_node(tail);
        if(!current_tail){
            fail.reason = Scheduler::scheduler_task_dll_events::pop_tail_results::fail_reasons::null_pool;
            return fail;
        }
        uint32_t prev_index = current_tail->pre_node;
        task_node* prev_node = pool->get_task_node(prev_index);
        if(!prev_node){
            fail.reason = Scheduler::scheduler_task_dll_events::pop_tail_results::fail_reasons::null_pool;
            return fail;
        }
        
        // 更新前一个节点的后向指针
        prev_node->next_node = INVALID_NODE_INDEX;
        
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
    
    task_node* pre_node = pool->get_task_node(pre_index);
    task_node* insertor_node = pool->get_task_node(insertor_index);
    if(!pre_node || !insertor_node){
        fail.reason = Scheduler::scheduler_task_dll_events::insert_after_results::fail_reasons::null_pool;
        return fail;
    }
    
    uint32_t next_index = pre_node->next_node;
    
    // 设置插入节点的链接
    insertor_node->pre_node = pre_index;
    insertor_node->next_node = next_index;
    
    // 更新前置节点的后向链接
    pre_node->next_node = insertor_index;
    
    // 如果前置节点是尾节点，更新尾指针
    if (pre_index == tail) {
        tail = insertor_index;
    }
    
    // 如果后继节点存在，更新其前向链接
    if (next_index != INVALID_NODE_INDEX) {
        task_node* next_node = pool->get_task_node(next_index);
        if(!next_node){
            fail.reason = Scheduler::scheduler_task_dll_events::insert_after_results::fail_reasons::null_pool;
            return fail;
        }
        next_node->pre_node = insertor_index;
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
    
    task_node* node = pool->get_task_node(index);
    if(!node){
        fail.reason = Scheduler::scheduler_task_dll_events::remove_results::fail_reasons::null_pool;
        return fail;
    }
    
    uint32_t pre_index = node->pre_node;
    uint32_t next_index = node->next_node;
    
    // 更新前向节点的链接
    if (pre_index != INVALID_NODE_INDEX) {
        task_node* pre_node = pool->get_task_node(pre_index);
        if(!pre_node){
            fail.reason = Scheduler::scheduler_task_dll_events::remove_results::fail_reasons::null_pool;
            return fail;
        }
        pre_node->next_node = next_index;
    } else {
        // 要删除的是头节点
        head = next_index;
    }
    
    // 更新后向节点的链接
    if (next_index != INVALID_NODE_INDEX) {
        task_node* next_node = pool->get_task_node(next_index);
        if(!next_node){
            fail.reason = Scheduler::scheduler_task_dll_events::remove_results::fail_reasons::null_pool;
            return fail;
        }
        next_node->pre_node = pre_index;
    } else {
        // 要删除的是尾节点
        tail = pre_index;
    }
    
    // 重置被删除节点的链接
    node->pre_node = INVALID_NODE_INDEX;
    node->next_node = INVALID_NODE_INDEX;
    
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
        task_node* node = pool_ptr->get_task_node(index);
        if(node){
            return *node;
        }
    }
    return null_task_node;
}

per_processor_scheduler::tasks_dll::iterator& per_processor_scheduler::tasks_dll::iterator::operator++() {
    if (pool_ptr && index != INVALID_NODE_INDEX) {
        task_node* node = pool_ptr->get_task_node(index);
        if(node){
            index = node->next_node;
        }
    }
    return *this;
}

per_processor_scheduler::tasks_dll::iterator& per_processor_scheduler::tasks_dll::iterator::operator--() {
    if (pool_ptr && index != INVALID_NODE_INDEX) {
        task_node* node = pool_ptr->get_task_node(index);
        if(node){
            index = node->pre_node;
        }
    }
    return *this;
}

bool per_processor_scheduler::tasks_dll::iterator::operator!=(const iterator& other) {
    return index != other.index || pool_ptr != other.pool_ptr;
}

bool per_processor_scheduler::tasks_dll::iterator::operator==(const iterator& other) {
    return index == other.index && pool_ptr == other.pool_ptr;
}

task* per_processor_scheduler::get_now_running_task(KURD_t& result_kurd)
{
    lock.lock();
    auto return_with_unlock = [&](task* value) -> task* {
        lock.unlock();
        return value;
    };
    KURD_t success = default_success();
    KURD_t fail = default_fail();
    success.event_code = Scheduler::self_scheduler_events::get_now_running_task;
    fail.event_code = Scheduler::self_scheduler_events::get_now_running_task;

    if (now_running_task_index == INVALID_NODE_INDEX) {
        fail.reason = Scheduler::self_scheduler_events::get_now_running_task_results::fail_reasons::not_running;
        result_kurd = fail;
        return return_with_unlock(nullptr);
    }

    auto panic_inconsistency = [&](uint16_t fatal_reason) -> task* {
        KURD_t fatal = default_fatal();
        fatal.event_code = Scheduler::self_scheduler_events::get_now_running_task;
        fatal.reason = fatal_reason;
        panic_with_kurd(fatal);
        return return_with_unlock(nullptr);
    };

    task_node* node = processor_self_task_pool.get_task_node(now_running_task_index);
    if (!node) {
        return panic_inconsistency(
            Scheduler::self_scheduler_events::get_now_running_task_results::fatal_reasons::task_node_invalid
        );
    }
    task* task_ptr = node->task_ptr;
    if (!task_ptr) {
        return panic_inconsistency(
            Scheduler::self_scheduler_events::get_now_running_task_results::fatal_reasons::task_ptr_null
        );
    }
    if (task_ptr->location.in_pool_index != now_running_task_index) {
        return panic_inconsistency(
            Scheduler::self_scheduler_events::get_now_running_task_results::fatal_reasons::location_pool_mismatch
        );
    }
    if (all_scheduler_ptr == nullptr ||
        all_scheduler_ptr[task_ptr->location.processor_id] != this) {
        return panic_inconsistency(
            Scheduler::self_scheduler_events::get_now_running_task_results::fatal_reasons::location_owner_mismatch
        );
    }
    if (task_ptr->task_state != task_state_t::running) {
        return panic_inconsistency(
            Scheduler::self_scheduler_events::get_now_running_task_results::fatal_reasons::state_not_running
        );
    }

    result_kurd = success;
    return return_with_unlock(task_ptr);
}

KURD_t per_processor_scheduler::task_set_ready(task* task_ptr)
{
    lock.lock();
    auto return_with_unlock = [&](KURD_t value) -> KURD_t {
        lock.unlock();
        return value;
    };
    KURD_t success = default_success();
    KURD_t fail = default_fail();
    success.event_code = Scheduler::self_scheduler_events::task_set_ready;
    fail.event_code = Scheduler::self_scheduler_events::task_set_ready;

    if (!task_ptr) {
        fail.reason = Scheduler::self_scheduler_events::task_set_ready_results::fail_reasons::null_task_ptr;
        return return_with_unlock(fail);
    }

    const uint32_t owner_processor_id = task_ptr->location.processor_id;
    if (all_scheduler_ptr == nullptr) {
        fail.reason = Scheduler::self_scheduler_events::task_set_ready_results::fail_reasons::owner_scheduler_not_found;
        return return_with_unlock(fail);
    }

    per_processor_scheduler* owner_scheduler = all_scheduler_ptr[owner_processor_id];
    if (!owner_scheduler) {
        fail.reason = Scheduler::self_scheduler_events::task_set_ready_results::fail_reasons::owner_scheduler_not_found;
        return return_with_unlock(fail);
    }
    if (owner_scheduler != this) {
        fail.reason = Scheduler::self_scheduler_events::task_set_ready_results::fail_reasons::wrong_owner_scheduler;
        return return_with_unlock(fail);
    }

    const uint32_t pool_index = task_ptr->location.in_pool_index;
    task_node* node = processor_self_task_pool.get_task_node(pool_index);
    if (!node || node->task_ptr != task_ptr) {
        fail.reason = Scheduler::self_scheduler_events::task_set_ready_results::fail_reasons::task_node_invalid;
        return return_with_unlock(fail);
    }

    KURD_t queue_kurd = KURD_t();
    switch (task_ptr->task_state) {
        case task_state_t::init:
            if (!task_ptr->set_ready()) {
                fail.reason = Scheduler::self_scheduler_events::task_set_ready_results::fail_reasons::state_transition_invalid;
                return return_with_unlock(fail);
            }
            break;
        case task_state_t::blocked:
            queue_kurd = blocked_queue.remove(pool_index);
            if (!success_all_kurd(queue_kurd)) {
                fail.reason = Scheduler::self_scheduler_events::task_set_ready_results::fail_reasons::queue_op_failed;
                return return_with_unlock(fail);
            }
            if (!task_ptr->set_ready()) {
                fail.reason = Scheduler::self_scheduler_events::task_set_ready_results::fail_reasons::state_transition_invalid;
                return return_with_unlock(fail);
            }
            break;
        case task_state_t::running:
            if (now_running_task_index != pool_index) {
                fail.reason = Scheduler::self_scheduler_events::task_set_ready_results::fail_reasons::running_index_mismatch;
                return return_with_unlock(fail);
            }
            if (!task_ptr->set_ready()) {
                fail.reason = Scheduler::self_scheduler_events::task_set_ready_results::fail_reasons::state_transition_invalid;
                return return_with_unlock(fail);
            }
            now_running_task_index = INVALID_NODE_INDEX;
            break;
        default:
            fail.reason = Scheduler::self_scheduler_events::task_set_ready_results::fail_reasons::state_transition_invalid;
            return return_with_unlock(fail);
    }

    queue_kurd = ready_queue[0].push_tail(pool_index);
    if (!success_all_kurd(queue_kurd)) {
        fail.reason = Scheduler::self_scheduler_events::task_set_ready_results::fail_reasons::queue_op_failed;
        return return_with_unlock(fail);
    }
    return return_with_unlock(success);
}

KURD_t per_processor_scheduler::task_set_blocked(task* task_ptr, task_blocked_reason_t reason)
{
    lock.lock();
    auto return_with_unlock = [&](KURD_t value) -> KURD_t {
        lock.unlock();
        return value;
    };
    KURD_t success = default_success();
    KURD_t fail = default_fail();
    success.event_code = Scheduler::self_scheduler_events::task_set_blocked;
    fail.event_code = Scheduler::self_scheduler_events::task_set_blocked;

    if (!task_ptr) {
        fail.reason = Scheduler::self_scheduler_events::task_set_blocked_results::fail_reasons::null_task_ptr;
        return return_with_unlock(fail);
    }
    if (reason == task_blocked_reason_t::invalid) {
        fail.reason = Scheduler::self_scheduler_events::task_set_blocked_results::fail_reasons::invalid_block_reason;
        return return_with_unlock(fail);
    }

    const uint32_t owner_processor_id = task_ptr->location.processor_id;
    if (all_scheduler_ptr == nullptr) {
        fail.reason = Scheduler::self_scheduler_events::task_set_blocked_results::fail_reasons::owner_scheduler_not_found;
        return return_with_unlock(fail);
    }
    per_processor_scheduler* owner_scheduler = all_scheduler_ptr[owner_processor_id];
    if (!owner_scheduler) {
        fail.reason = Scheduler::self_scheduler_events::task_set_blocked_results::fail_reasons::owner_scheduler_not_found;
        return return_with_unlock(fail);
    }
    if (owner_scheduler != this) {
        fail.reason = Scheduler::self_scheduler_events::task_set_blocked_results::fail_reasons::wrong_owner_scheduler;
        return return_with_unlock(fail);
    }

    const uint32_t pool_index = task_ptr->location.in_pool_index;
    task_node* node = processor_self_task_pool.get_task_node(pool_index);
    if (!node || node->task_ptr != task_ptr) {
        fail.reason = Scheduler::self_scheduler_events::task_set_blocked_results::fail_reasons::task_node_invalid;
        return return_with_unlock(fail);
    }

    if (task_ptr->task_state != task_state_t::running) {
        fail.reason = Scheduler::self_scheduler_events::task_set_blocked_results::fail_reasons::state_transition_invalid;
        return return_with_unlock(fail);
    }
    if (now_running_task_index != pool_index) {
        fail.reason = Scheduler::self_scheduler_events::task_set_blocked_results::fail_reasons::running_index_mismatch;
        return return_with_unlock(fail);
    }

    now_running_task_index = INVALID_NODE_INDEX;
    if (!task_ptr->set_blocked()) {
        now_running_task_index = pool_index;
        fail.reason = Scheduler::self_scheduler_events::task_set_blocked_results::fail_reasons::state_transition_invalid;
        return return_with_unlock(fail);
    }
    task_ptr->blocked_reason = reason;

    KURD_t queue_kurd = blocked_queue.push_tail(pool_index);
    if (!success_all_kurd(queue_kurd)) {
        fail.reason = Scheduler::self_scheduler_events::task_set_blocked_results::fail_reasons::queue_op_failed;
        return return_with_unlock(fail);
    }
    return return_with_unlock(success);
}

KURD_t per_processor_scheduler::task_set_zombie(task* task_ptr)
{
    lock.lock();
    auto return_with_unlock = [&](KURD_t value) -> KURD_t {
        lock.unlock();
        return value;
    };
    KURD_t success = default_success();
    KURD_t fail = default_fail();
    success.event_code = Scheduler::self_scheduler_events::task_set_zombie;
    fail.event_code = Scheduler::self_scheduler_events::task_set_zombie;

    if (!task_ptr) {
        fail.reason = Scheduler::self_scheduler_events::task_set_zombie_results::fail_reasons::null_task_ptr;
        return return_with_unlock(fail);
    }

    const uint32_t owner_processor_id = task_ptr->location.processor_id;
    if (all_scheduler_ptr == nullptr) {
        fail.reason = Scheduler::self_scheduler_events::task_set_zombie_results::fail_reasons::owner_scheduler_not_found;
        return return_with_unlock(fail);
    }
    per_processor_scheduler* owner_scheduler = all_scheduler_ptr[owner_processor_id];
    if (!owner_scheduler) {
        fail.reason = Scheduler::self_scheduler_events::task_set_zombie_results::fail_reasons::owner_scheduler_not_found;
        return return_with_unlock(fail);
    }
    if (owner_scheduler != this) {
        fail.reason = Scheduler::self_scheduler_events::task_set_zombie_results::fail_reasons::wrong_owner_scheduler;
        return return_with_unlock(fail);
    }

    const uint32_t pool_index = task_ptr->location.in_pool_index;
    task_node* node = processor_self_task_pool.get_task_node(pool_index);
    if (!node || node->task_ptr != task_ptr) {
        fail.reason = Scheduler::self_scheduler_events::task_set_zombie_results::fail_reasons::task_node_invalid;
        return return_with_unlock(fail);
    }

    KURD_t queue_kurd = KURD_t();
    switch (task_ptr->task_state) {
        case task_state_t::running:
            if (now_running_task_index != pool_index) {
                fail.reason = Scheduler::self_scheduler_events::task_set_zombie_results::fail_reasons::running_index_mismatch;
                return return_with_unlock(fail);
            }
            now_running_task_index = INVALID_NODE_INDEX;
            break;
        case task_state_t::blocked:
            queue_kurd = blocked_queue.remove(pool_index);
            if (!success_all_kurd(queue_kurd)) {
                fail.reason = Scheduler::self_scheduler_events::task_set_zombie_results::fail_reasons::queue_op_failed;
                return return_with_unlock(fail);
            }
            break;
        case task_state_t::ready:
            queue_kurd = ready_queue[0].remove(pool_index);
            if (!success_all_kurd(queue_kurd)) {
                fail.reason = Scheduler::self_scheduler_events::task_set_zombie_results::fail_reasons::queue_op_failed;
                return return_with_unlock(fail);
            }
            break;
        default:
            fail.reason = Scheduler::self_scheduler_events::task_set_zombie_results::fail_reasons::state_transition_invalid;
            return return_with_unlock(fail);
    }

    if (!task_ptr->set_zombie()) {
        fail.reason = Scheduler::self_scheduler_events::task_set_zombie_results::fail_reasons::state_transition_invalid;
        return return_with_unlock(fail);
    }
    task_ptr->blocked_reason = task_blocked_reason_t::invalid;

    queue_kurd = zombie_queue.push_tail(pool_index);
    if (!success_all_kurd(queue_kurd)) {
        fail.reason = Scheduler::self_scheduler_events::task_set_zombie_results::fail_reasons::queue_op_failed;
        return return_with_unlock(fail);
    }
    return return_with_unlock(success);
}

KURD_t per_processor_scheduler::task_set_dead(task* task_ptr)//会释放对应槽位
{
    lock.lock();
    auto return_with_unlock = [&](KURD_t value) -> KURD_t {
        lock.unlock();
        return value;
    };
    KURD_t success = default_success();
    KURD_t fail = default_fail();
    success.event_code = Scheduler::self_scheduler_events::task_set_dead;
    fail.event_code = Scheduler::self_scheduler_events::task_set_dead;

    if (!task_ptr) {
        fail.reason = Scheduler::self_scheduler_events::task_set_dead_results::fail_reasons::null_task_ptr;
        return return_with_unlock(fail);
    }

    const uint32_t owner_processor_id = task_ptr->location.processor_id;
    if (all_scheduler_ptr == nullptr) {
        fail.reason = Scheduler::self_scheduler_events::task_set_dead_results::fail_reasons::owner_scheduler_not_found;
        return return_with_unlock(fail);
    }
    per_processor_scheduler* owner_scheduler = all_scheduler_ptr[owner_processor_id];
    if (!owner_scheduler) {
        fail.reason = Scheduler::self_scheduler_events::task_set_dead_results::fail_reasons::owner_scheduler_not_found;
        return return_with_unlock(fail);
    }
    if (owner_scheduler != this) {
        fail.reason = Scheduler::self_scheduler_events::task_set_dead_results::fail_reasons::wrong_owner_scheduler;
        return return_with_unlock(fail);
    }

    const uint32_t pool_index = task_ptr->location.in_pool_index;
    task_node* node = processor_self_task_pool.get_task_node(pool_index);
    if (!node || node->task_ptr != task_ptr) {
        fail.reason = Scheduler::self_scheduler_events::task_set_dead_results::fail_reasons::task_node_invalid;
        return return_with_unlock(fail);
    }

    if (task_ptr->task_state != task_state_t::zombie) {
        fail.reason = Scheduler::self_scheduler_events::task_set_dead_results::fail_reasons::state_transition_invalid;
        return return_with_unlock(fail);
    }

    KURD_t queue_kurd = zombie_queue.remove(pool_index);
    if (!success_all_kurd(queue_kurd)) {
        fail.reason = Scheduler::self_scheduler_events::task_set_dead_results::fail_reasons::queue_op_failed;
        return return_with_unlock(fail);
    }

    if (!task_ptr->set_dead()) {
        fail.reason = Scheduler::self_scheduler_events::task_set_dead_results::fail_reasons::state_transition_invalid;
        return return_with_unlock(fail);
    }
    task_ptr->blocked_reason = task_blocked_reason_t::invalid;
    queue_kurd=this->processor_self_task_pool.free(pool_index);
    if(error_kurd(queue_kurd))return return_with_unlock(queue_kurd);
    return return_with_unlock(success);
}

void per_processor_scheduler::schedule_and_switch()
{//先暂时只考虑单队列,但是此函数不返回
    lock.lock();

    KURD_t kurd=ready_queue[0].pop_head(now_running_task_index);
    if(!success_all_kurd(kurd)){
        if(kurd.event_code==Scheduler::scheduler_task_dll_events::pop_head
        &&kurd.reason==Scheduler::scheduler_task_dll_events::pop_head_results::fail_reasons::empty){
            if(idle_kthread_index==invalid_task_id){
                lock.unlock();
                panic_with_kurd(make_self_scheduler_fatal(
                    Scheduler::self_scheduler_events::schedule_and_switch,
                    Scheduler::self_scheduler_events::schedule_and_switch_results::fatal_reasons::empty_no_idle
                ));
            }
            now_running_task_index=idle_kthread_index;
        }else{
            lock.unlock();
            panic_with_kurd(kurd);
        }
    }
    task_node* node=processor_self_task_pool.get_task_node(now_running_task_index);
    if(!node || !node->task_ptr){
        lock.unlock();
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::schedule_and_switch,
            Scheduler::self_scheduler_events::schedule_and_switch_results::fatal_reasons::null_task_ptr
        ));
    }
    lock.unlock();
    node->task_ptr->atomic_load();
}
