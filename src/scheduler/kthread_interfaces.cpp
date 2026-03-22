#include "Scheduler/per_processor_scheduler.h"
#include "panic.h"
#include "core_hardwares/lapic.h"
#include "Interrupt_system/loacl_processor.h"
#include "abi/arch/x86-64/GS_Slots_index_definitions.h"
#include "util/arch/x86-64/cpuid_intel.h"
#include "util/kout.h"
#include "memory/FreePagesAllocator.h"
alignas(64) per_processor_scheduler global_schedulers[MAX_PROCESSORS_COUNT];
namespace {
constexpr uint64_t kthread_yield_saved_stack_delta = 16 * sizeof(uint64_t);
spinlock_cpp_t global_tid_lock;
uint64_t global_tid_counter = 0;


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

void allthread_true_enter(void *(*entry)(void *), void *arg){
    uint64_t return_value=(uint64_t)entry(arg);
    kthread_true_exit(return_value);
};
uint64_t create_kthread(void *(*entry)(void *), void *arg, KURD_t *out_kurd)
{
    kthread_context* context = new kthread_context();
    context->regs.rip = (uint64_t)allthread_true_enter;
    context->regs.rsi = (uint64_t)arg;
    context->regs.rdi = (uint64_t)entry;
    context->stacksize = DEFAULT_STACK_SIZE;
    context->stack_top = (uint64_t)__wrapped_pgs_valloc(out_kurd, DEFAULT_STACK_PG_COUNT, page_state_t::kernel_pinned, 12);
    context->regs.rsp = context->stack_top + context->stacksize;
    context->regs.rflags = INIT_DEFAULT_RFLAGS;
    task* new_task = new task(task_type_t::kthreadm, context);
    new_task->task_lock.lock();
    uint64_t assigned_tid = task_pool::alloc(new_task, *out_kurd);
    if(error_kurd(*out_kurd)){
        new_task->task_lock.unlock();
        return INVALID_TID;
    }
    new_task->assign_valid_tid(assigned_tid);
    new_task->set_ready();
    per_processor_scheduler&self_scheduler = global_schedulers[fast_get_processor_id()];
    new_task->task_lock.unlock();
    self_scheduler.ready_queues_lock.lock();
    *out_kurd=self_scheduler.insert_ready_task(new_task);
    if(error_kurd(*out_kurd)){
         delete context;
        delete new_task;
        self_scheduler.ready_queue.pop_back();
        self_scheduler.ready_queues_lock.unlock();
        return INVALID_TID;
    }
    self_scheduler.ready_queues_lock.unlock();
    return assigned_tid;
}
void kthread_yield_true_enter(kthread_yield_raw_context *context)
{
    x64_basic_context basic_ctx{};
    convert_kthread_yield_raw_to_basic(context, &basic_ctx);
    per_processor_scheduler&scheduler=global_schedulers[fast_get_processor_id()];
    KURD_t running_task_kurd=KURD_t();
    task* interrupted_task=task_pool::get_by_tid(scheduler.now_running_tid,running_task_kurd);
    interrupted_task->task_lock.lock();
    if(!interrupted_task){
        interrupted_task->task_lock.unlock();
        panic_with_kurd(running_task_kurd);
    }
    interrupted_task->lastest_span_length=ktime::hardware_time::get_stamp()-interrupted_task->lastest_run_stamp;
    interrupted_task->accumulated_time+=interrupted_task->lastest_span_length;
    if(interrupted_task->get_task_type()!=task_type_t::kthreadm){
        interrupted_task->task_lock.unlock();
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::kthread_yield_enter,
            Scheduler::self_scheduler_events::kthread_yield_enter_results::fatal_reasons::invalid_task_type
        ));
    }
    if(!interrupted_task->context.kthread){
        interrupted_task->task_lock.unlock();
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::kthread_yield_enter,
            Scheduler::self_scheduler_events::kthread_yield_enter_results::fatal_reasons::null_kthread_context
        ));
    }
    interrupted_task->context.kthread->regs=basic_ctx;
    if(interrupted_task->context.kthread->stacksize==0){
        interrupted_task->task_lock.unlock();
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::kthread_yield_enter,
            Scheduler::self_scheduler_events::kthread_yield_enter_results::fatal_reasons::invalid_stack_size
        ));
    }
    {
        vaddr_t stack_top = interrupted_task->context.kthread->stack_top;
        vaddr_t stack_bottom = stack_top + interrupted_task->context.kthread->stacksize;
        if(basic_ctx.rsp < stack_top || basic_ctx.rsp >= stack_bottom){
            interrupted_task->task_lock.unlock();
            panic_with_kurd(make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::kthread_yield_enter,
                Scheduler::self_scheduler_events::kthread_yield_enter_results::fatal_reasons::rsp_out_of_range
            ));
        }
    }
    interrupted_task->set_ready();
    interrupted_task->task_lock.unlock();
    scheduler.ready_queues_lock.lock();
    scheduler.now_running_tid=~0;
    KURD_t kurd=scheduler.insert_ready_task(interrupted_task);
    if(error_kurd(kurd)){
        scheduler.ready_queues_lock.unlock();
        panic_with_kurd(kurd);
    }
    scheduler.ready_queues_lock.unlock();
    scheduler.sched();

}
void timer_cpp_enter(x64_Interrupt_saved_context_no_errcode *frame)
{
    asm volatile("cli");
    x64_basic_context basic_ctx{};
    KURD_t kurd=KURD_t();
    convert_interrupt_no_err_to_basic(frame, &basic_ctx);
    per_processor_scheduler&scheduler=global_schedulers[fast_get_processor_id()];
    KURD_t running_task_kurd=KURD_t();
    task* interrupted_task=task_pool::get_by_tid(scheduler.now_running_tid,running_task_kurd);
    if(!success_all_kurd(running_task_kurd) || !interrupted_task){
        panic_with_kurd(frame, running_task_kurd);
    }
    interrupted_task->task_lock.lock();
    interrupted_task->lastest_span_length=ktime::hardware_time::get_stamp()-interrupted_task->lastest_run_stamp;
    interrupted_task->accumulated_time+=interrupted_task->lastest_span_length;
    switch(interrupted_task->get_task_type()){
        case task_type_t::kthreadm:
        if((frame->cs&3)!=0){
            interrupted_task->task_lock.unlock();
            panic_with_kurd(frame, make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::timer_cpp_enter,
                Scheduler::self_scheduler_events::timer_cpp_enter_results::fatal_reasons::invalid_cs
            ));
        }
        if(!interrupted_task->context.kthread){
            interrupted_task->task_lock.unlock();
            panic_with_kurd(frame, make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::timer_cpp_enter,
                Scheduler::self_scheduler_events::timer_cpp_enter_results::fatal_reasons::null_kthread_context
            ));
        }
        interrupted_task->context.kthread->regs=basic_ctx;
        if(interrupted_task->context.kthread->stacksize==0){
            interrupted_task->task_lock.unlock();
            panic_with_kurd(frame, make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::timer_cpp_enter,
                Scheduler::self_scheduler_events::timer_cpp_enter_results::fatal_reasons::invalid_stack_size
            ));
        }
        {
            vaddr_t stack_top = interrupted_task->context.kthread->stack_top;
            vaddr_t stack_bottom = stack_top + interrupted_task->context.kthread->stacksize;
            if(basic_ctx.rsp < stack_top || basic_ctx.rsp >= stack_bottom){
                interrupted_task->task_lock.unlock();
                panic_with_kurd(frame, make_self_scheduler_fatal(
                    Scheduler::self_scheduler_events::timer_cpp_enter,
                    Scheduler::self_scheduler_events::timer_cpp_enter_results::fatal_reasons::rsp_out_of_range
                ));
            }
        }
            break;
        case task_type_t::userthread:
        if((frame->cs&3)!=3){
            interrupted_task->task_lock.unlock();
            panic_with_kurd(frame, make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::timer_cpp_enter,
                Scheduler::self_scheduler_events::timer_cpp_enter_results::fatal_reasons::invalid_cs
            ));
        }
        if(!interrupted_task->context.userthread){
            interrupted_task->task_lock.unlock();
            panic_with_kurd(frame, make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::timer_cpp_enter,
                Scheduler::self_scheduler_events::timer_cpp_enter_results::fatal_reasons::null_userthread_context
            ));
        }

            break;
        default:
        interrupted_task->task_lock.unlock();
            panic_with_kurd(frame, make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::timer_cpp_enter,
                Scheduler::self_scheduler_events::timer_cpp_enter_results::fatal_reasons::invalid_task_type
            ));
            break;
    }
    interrupted_task->set_ready();
    interrupted_task->task_lock.unlock();
    scheduler.ready_queues_lock.lock();
    scheduler.now_running_tid=~0;
    kurd=scheduler.insert_ready_task(interrupted_task);
    if(error_kurd(kurd)){
        scheduler.ready_queues_lock.unlock();
        panic_with_kurd(frame, kurd);
    }
    scheduler.ready_queues_lock.unlock();
    asm volatile("sti");
    x2apic::x2apic_driver::write_eoi();
    ktime::time_interrupt_generator::set_clock_by_offset(DEFALUT_TIMER_SPAN_MIUS);
    scheduler.sched();
}
void kthread_true_exit(uint64_t will) 
{
    per_processor_scheduler&scheduler=global_schedulers[fast_get_processor_id()];
    KURD_t running_task_kurd=KURD_t();
    task*exit_task=task_pool::get_by_tid(scheduler.now_running_tid,running_task_kurd);
    if(!success_all_kurd(running_task_kurd) || !exit_task){
        panic_with_kurd(running_task_kurd);
    }
    exit_task->task_lock.lock();
    exit_task->lastest_span_length=ktime::hardware_time::get_stamp()-exit_task->lastest_run_stamp;
    exit_task->accumulated_time+=exit_task->lastest_span_length;
    exit_task->context.kthread->regs.rax=will;
    exit_task->task_lock.unlock();
    scheduler.ready_queues_lock.lock();
    scheduler.now_running_tid=INVALID_TID;
    scheduler.ready_queues_lock.unlock();
    if(!exit_task->set_zombie()){
        
    }
    scheduler.sched();
}

void kthread_self_blocked_cppenter(kthread_yield_raw_context* context)
{
    x64_basic_context basic_ctx{};
    convert_kthread_yield_raw_to_basic(context, &basic_ctx);
    per_processor_scheduler&scheduler=global_schedulers[fast_get_processor_id()];
    KURD_t running_task_kurd=KURD_t();
    task* interrupted_task=task_pool::get_by_tid(scheduler.now_running_tid,running_task_kurd);
    interrupted_task->task_lock.lock();
    if(!success_all_kurd(running_task_kurd) || !interrupted_task){
        interrupted_task->task_lock.unlock();
        panic_with_kurd(running_task_kurd);
    }
    interrupted_task->lastest_span_length=ktime::hardware_time::get_stamp()-interrupted_task->lastest_run_stamp;
    interrupted_task->accumulated_time+=interrupted_task->lastest_span_length;
    if(interrupted_task->get_task_type()!=task_type_t::kthreadm){
        interrupted_task->task_lock.unlock();
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::kthread_block,
            Scheduler::self_scheduler_events::kthread_block_results::fatal_reasons::bad_task_type
        ));
    }
    if(!interrupted_task->context.kthread){
        interrupted_task->task_lock.unlock();
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::kthread_block,
            Scheduler::self_scheduler_events::kthread_block_results::fatal_reasons::context_nullptr
        ));
    }
    interrupted_task->context.kthread->regs=basic_ctx;
    if(interrupted_task->context.kthread->stacksize==0){
        interrupted_task->task_lock.unlock();
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::kthread_block,
            Scheduler::self_scheduler_events::kthread_block_results::fatal_reasons::context_null_stack_size
        ));
    }
    {
        vaddr_t stack_top = interrupted_task->context.kthread->stack_top;
        vaddr_t stack_bottom = stack_top + interrupted_task->context.kthread->stacksize;
        if(basic_ctx.rsp < stack_top || basic_ctx.rsp >= stack_bottom){
            interrupted_task->task_lock.unlock();
            panic_with_kurd(make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::kthread_block,
                Scheduler::self_scheduler_events::kthread_block_results::fatal_reasons::context_stackptr_out_of_range
            ));
        }
    }
    interrupted_task->blocked_reason = static_cast<task_blocked_reason_t>(context->rdi);
    if(interrupted_task->get_state()!=task_state_t::running){
        interrupted_task->task_lock.unlock();
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::kthread_block,
            Scheduler::self_scheduler_events::kthread_block_results::fatal_reasons::illeage_state
        ));
    }
    interrupted_task->set_blocked();
    interrupted_task->task_lock.unlock();
    scheduler.sched();
}
uint64_t wakeup_thread(uint64_t tid){
    KURD_t kurd=KURD_t();
    KURD_t success,fail,fatal;
    success=KURD_t(result_code::SUCCESS,0,module_code::SCHEDULER,Scheduler::scheduler_task_pool,Scheduler::self_scheduler_events::wake_up_kthread,level_code::INFO,err_domain::CORE_MODULE);
    fail=KURD_t(result_code::FAIL,0,module_code::SCHEDULER,Scheduler::scheduler_task_pool,Scheduler::self_scheduler_events::wake_up_kthread,level_code::ERROR,err_domain::CORE_MODULE);
    fatal=KURD_t(result_code::FATAL,0,module_code::SCHEDULER,Scheduler::scheduler_task_pool,Scheduler::self_scheduler_events::wake_up_kthread,level_code::FATAL,err_domain::CORE_MODULE);
    task*task_ptr=task_pool::get_by_tid(tid,kurd);
    if(!success_all_kurd(kurd)){
        return kurd_get_raw(kurd);
    }
    if(task_ptr->wakeup.try_lock()){
        per_processor_scheduler&task_in_processor=global_schedulers[task_ptr->get_belonged_processor_id()];
        task_ptr->task_lock.lock();
        if(task_ptr->get_state()==task_state_t::ready||
    task_ptr->get_state()==task_state_t::running){
        task_ptr->task_lock.unlock();
        task_ptr->wakeup.unlock();
        success.reason=Scheduler::self_scheduler_events::wake_up_kthread_results::success_reasons::already_wakeup_or_running;
        return kurd_get_raw(success);
        //成功但是已经运行
    }else if(task_ptr->get_state()==task_state_t::blocked){
        task_ptr->set_ready();
        task_ptr->task_lock.unlock();
        task_in_processor.ready_queues_lock.lock();
        kurd=task_in_processor.insert_ready_task(task_ptr);
        task_in_processor.ready_queues_lock.unlock();
        task_ptr->wakeup.unlock();
        return kurd_get_raw(kurd);
    }else{
        task_ptr->task_lock.unlock();
        task_ptr->wakeup.unlock();
        fail.reason=Scheduler::self_scheduler_events::wake_up_kthread_results::fail_reasons::bad_task_state;
        return kurd_get_raw(fail);
    }
    }else{
        success.reason=Scheduler::self_scheduler_events::wake_up_kthread_results::success_reasons::other_entity_wakeup;
        return kurd_get_raw(success);
    }
}
