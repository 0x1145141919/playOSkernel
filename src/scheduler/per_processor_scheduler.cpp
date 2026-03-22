#include "Scheduler/per_processor_scheduler.h"
#include "memory/kpoolmemmgr.h"
#include "memory/all_pages_arr.h"
#include "memory/FreePagesAllocator.h"
#include "util/kout.h"
#include "Interrupt_system/Interrupt.h"
#include "core_hardwares/lapic.h"
#include "util/arch/x86-64/cpuid_intel.h"
#include "panic.h"
task_pool::root_entry task_pool::root_table[root_table_entry_count];
spinrwlock_cpp_t task_pool::lock;
uint32_t task_pool::last_alloc_index = 0;
// 添加适配函数来解决类型不匹配问题
extern "C" void secure_hlt();
static void* secure_hlt_wrapper(void* unused) {
    (void)unused;  // 忽略未使用的参数
    secure_hlt();  // 返回值不会被使用，但满足函数签名要求
}
KURD_t task_pool::default_kurd()
{
    return KURD_t(0,0,module_code::SCHEDULER,Scheduler::scheduler_task_pool,0,0,err_domain::CORE_MODULE);
}
KURD_t task_pool::default_success()
{
    KURD_t kurd=default_kurd();
    kurd.result = result_code::SUCCESS;
    kurd.level = level_code::INFO;
    return kurd;
}
KURD_t task_pool::default_fail(){
    return set_result_fail_and_error_level(default_kurd());
}
KURD_t task_pool::default_fatal()
{
    return set_fatal_result_level(default_kurd());
}
KURD_t task_pool::enable_subtable(uint32_t high_idx)
{
    KURD_t kurd;
    root_entry& entry = root_table[high_idx];
    entry.sub=(subtable*)__wrapped_pgs_valloc(&kurd,(sizeof(task_pool::subtable)+4095)>>12,page_state_t::kernel_pinned,12);
    if(entry.sub==nullptr||error_kurd(kurd)){
        return kurd;
    }
    new(&entry.sub->used_bitmap) huge_bitmap(sub_table_entry_count);
    kurd=entry.sub->used_bitmap.second_stage_init();
    if(error_kurd(kurd)){
        return kurd;
    }
    ksetmem_8(entry.sub->task_table,0,sub_table_entry_count*sizeof(task_in_pool));
    for(uint32_t i=0;i<sub_table_entry_count;i++){
        entry.sub->task_table[i].slot_version=entry.last_max_slot_version;
    }
    return kurd;
}
KURD_t task_pool::try_disable_subtable(uint32_t high_idx)
{
    KURD_t kurd;
    root_entry& entry = root_table[high_idx];
    if(entry.sub==nullptr){

    }
    if(!entry.sub->used_bitmap.all_false()){
        //没有全空，报错
    }
    uint32_t max=0;
    for(uint32_t i=0;i<sub_table_entry_count;i++){
        if(entry.sub->task_table[i].slot_version>max){
            max=entry.sub->task_table[i].slot_version;
        }
    }
    entry.last_max_slot_version=max;
    entry.sub->used_bitmap.~huge_bitmap();
    kurd=__wrapped_pgs_vfree(entry.sub,(sizeof(task_pool::subtable)+4095)>>12);
    entry.sub=nullptr;
    return kurd;
}
int task_pool::Init()
{
    return OS_SUCCESS;
}

uint64_t task_pool::alloc_tid(KURD_t& kurd)
{
    KURD_t success = default_success();
    KURD_t fail = default_fail();
    success.event_code = Scheduler::task_pool_events::slot_alloc;
    fail.event_code = Scheduler::task_pool_events::slot_alloc;

    const uint32_t start_index = last_alloc_index;
    const uint64_t total_slots = (uint64_t)root_table_entry_count * sub_table_entry_count;

    for (uint64_t offset = 0; offset < total_slots; ++offset) {
        uint32_t idx = start_index + static_cast<uint32_t>(offset);
        uint32_t high_idx = idx >> 16;
        uint32_t low_idx = idx & 0xFFFF;

        if (root_table[high_idx].sub == nullptr) {
            KURD_t sub_kurd = enable_subtable(high_idx);
            if (error_kurd(sub_kurd)) {
                kurd = sub_kurd;
                return ~0ull;
            }
        }

        subtable* sub = root_table[high_idx].sub;
        if (!sub->used_bitmap.bit_get(low_idx)) {
            sub->used_bitmap.bit_set(low_idx, true);
            sub->used_bitmap.used_bit_count_add(1);
            last_alloc_index = idx + 1;
            uint64_t tid = (static_cast<uint64_t>(idx) << 32) | sub->task_table[low_idx].slot_version;
            kurd = success;
            return tid;
        }
    }

    fail.reason = Scheduler::task_pool_events::alloc_results::fail_reasons::not_found;
    kurd = fail;
    return ~0ull;
}

task* task_pool::get_by_tid(uint64_t tid, KURD_t &kurd)
{
    spinrwlock_read_guard guard(lock);
    KURD_t success = default_success();
    KURD_t fail = default_fail();

    uint32_t idx = tid_to_idx(tid);
    uint32_t high_idx = idx >> 16;
    uint32_t low_idx = idx & 0xFFFF;
    uint32_t version = static_cast<uint32_t>(tid);

    if (high_idx >= root_table_entry_count) {
        kurd = fail;
        return nullptr;
    }

    subtable* sub = root_table[high_idx].sub;
    if (sub == nullptr) {
        kurd = fail;
        return nullptr;
    }

    if (!sub->used_bitmap.bit_get(low_idx)) {
        kurd = fail;
        return nullptr;
    }

    if (sub->task_table[low_idx].slot_version != version) {
        kurd = fail;
        return nullptr;
    }

    kurd = success;
    return sub->task_table[low_idx].task_ptr;
}

uint64_t task_pool::alloc(task* task_ptr, KURD_t& kurd)
{
    spinrwlock_write_guard guard(lock);
    if (!task_ptr) {
        kurd = default_fail();
        return ~0ull;
    }
    uint64_t tid = alloc_tid(kurd);
    if (tid == ~0ull || error_kurd(kurd)) {
        return ~0ull;
    }

    uint32_t idx = tid_to_idx(tid);
    uint32_t high_idx = idx >> 16;
    uint32_t low_idx = idx & 0xFFFF;
    root_table[high_idx].sub->task_table[low_idx].task_ptr = task_ptr;
    return tid;
}

KURD_t task_pool::release_tid(uint64_t tid)
{
    spinrwlock_write_guard guard(lock);
    KURD_t success = default_success();
    KURD_t fail = default_fail();
    success.event_code = Scheduler::task_pool_events::slot_free;
    fail.event_code = Scheduler::task_pool_events::slot_free;

    uint32_t idx = tid_to_idx(tid);
    uint32_t high_idx = idx >> 16;
    uint32_t low_idx = idx & 0xFFFF;
    uint32_t version = static_cast<uint32_t>(tid);

    if (high_idx >= root_table_entry_count) {
        fail.reason = Scheduler::task_pool_events::free_results::fail_reasons::index_out_of_range;
        return fail;
    }

    subtable* sub = root_table[high_idx].sub;
    if (sub == nullptr) {
        fail.reason = Scheduler::task_pool_events::free_results::fail_reasons::sub_table_not_exist;
        return fail;
    }

    if (!sub->used_bitmap.bit_get(low_idx)) {
        fail.reason = Scheduler::task_pool_events::free_results::fail_reasons::not_allocated;
        return fail;
    }

    if (sub->task_table[low_idx].slot_version != version) {
        fail.reason = Scheduler::task_pool_events::free_results::fail_reasons::bad_tid;
        return fail;
    }

    sub->task_table[low_idx].task_ptr = nullptr;
    sub->task_table[low_idx].slot_version++;
    sub->used_bitmap.bit_set(low_idx, false);
    sub->used_bitmap.used_bit_count_sub(1);
    last_alloc_index = idx;
    return success;
}
per_processor_scheduler::per_processor_scheduler()
{
    KURD_t kurd;
    stack_bottom=(vaddr_t)stack_alloc(&kurd,PRIVATE_STACK_DEFAULT_PG_COUNT);
    if(error_kurd(kurd)){
        Panic::panic(default_panic_behaviors_flags,"stack alloc failed when alloc per_processor_scheduler private stack",nullptr,nullptr,kurd);
    }
    // Idle task must not be enqueued into ready_queue.
    kthread_context* idle_ctx = new kthread_context();
    idle_ctx->regs.rip = (uint64_t)secure_hlt_wrapper;
    idle_ctx->regs.rsi = 0;
    idle_ctx->regs.rdi = 0;
    idle_ctx->stacksize = DEFAULT_STACK_SIZE;
    idle_ctx->stack_top = (uint64_t)__wrapped_pgs_valloc(&kurd, DEFAULT_STACK_PG_COUNT, page_state_t::kernel_pinned, 12);
    idle_ctx->regs.rsp = idle_ctx->stack_top + idle_ctx->stacksize;
    idle_ctx->regs.rflags = INIT_DEFAULT_RFLAGS;
    if(error_kurd(kurd) || idle_ctx->stack_top == 0){
        Panic::panic(default_panic_behaviors_flags,"idle task stack alloc failed",nullptr,nullptr,kurd);
    }
    task* idle_task = new task(task_type_t::kthreadm, idle_ctx);
    idle_task->task_lock.lock();
    uint64_t idle_tid = task_pool::alloc(idle_task, kurd);
    if(error_kurd(kurd) || idle_tid==INVALID_TID){
        idle_task->task_lock.unlock();
        Panic::panic(default_panic_behaviors_flags,"alloc idle task failed",nullptr,nullptr,kurd);
    }
    idle_task->assign_valid_tid(idle_tid);
    idle_task->set_ready();
    idle_task->set_belonged_processor_id(fast_get_processor_id());
    idle_task->task_lock.unlock();
    idle = idle_task;
}
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
void per_processor_scheduler::sched()
{
    ready_queues_lock.lock();
    task*to_run=[&]()->task*
    { 
        if(ready_queue.empty())return idle;
        else return ready_queue.pop_front_value();
    }();
    now_running_tid=to_run->get_tid();
    ready_queues_lock.unlock();
    to_run->task_lock.lock();
    to_run->set_running();
    to_run->lastest_run_stamp=ktime::hardware_time::get_stamp();
    to_run->task_lock.unlock();
    to_run->atomic_load();
}
KURD_t per_processor_scheduler::insert_ready_task(task *task_ptr)
{
    KURD_t fail=default_fail();
    KURD_t success=default_success();
    fail.event_code=Scheduler::self_scheduler_events::insert_ready_task;
    success.event_code=Scheduler::self_scheduler_events::insert_ready_task;
    if(task_ptr==idle){
        return success;
    }
    if(task_ptr==nullptr){
        fail.reason=Scheduler::self_scheduler_events::insert_ready_task_results::fail_reasons::null_task_ptr;
        return fail;
    }
    if(task_ptr->get_state()!=ready){
        fail.reason=Scheduler::self_scheduler_events::insert_ready_task_results::fail_reasons::bad_task_type;
        return fail;
    }
    if(!ready_queue.push_back(task_ptr)){
        fail.reason=Scheduler::self_scheduler_events::insert_ready_task_results::fail_reasons::insert_fail;
        return fail;
    }
    return success;
}
vaddr_t per_processor_scheduler::get_stack_top()
{
    return stack_bottom+((PRIVATE_STACK_DEFAULT_PG_COUNT-1)<<12);
}
extern "C" uint64_t* get_scheduler_private_stack_top()
{
    return (uint64_t*)global_schedulers[fast_get_processor_id()].get_stack_top();
}

task::task(task_type_t task_type, void *context)
{
    this->task_type=task_type;
    tid=INVALID_TID;
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
    return belonged_processor_id;
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
bool task::set_running()
{
    if(task_state==task_state_t::ready){
        this->task_state=running;
        return true;
    }
    return false;
}
task::~task()
{
}
void task::assign_valid_tid(uint64_t tid)
{
    if(this->tid==INVALID_TID){
        this->tid=tid;
    }
}
task_state_t task::get_state()
{
    return task_state;
}
void task::set_belonged_processor_id(uint32_t pid)
{
    belonged_processor_id=pid;
}
void task::atomic_load()
{
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
