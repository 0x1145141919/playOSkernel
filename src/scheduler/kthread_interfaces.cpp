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
void sched(){

}
void allthread_true_enter(void *(*entry)(void *), void *arg){
    uint64_t return_value=(uint64_t)entry(arg);
    kthread_true_exit(return_value);
};
uint64_t create_kthread(void *(*entry)(void *), void *arg, KURD_t *out_kurd)
{
    kthread_context* context = new kthread_context();
    context->regs.rip = (uint64_t)allthread_true_enter;
    context->regs.rsi = (uint64_t)entry;
    context->regs.rdi = (uint64_t)arg;
    context->stacksize = DEFAULT_STACK_SIZE;
    context->stack_top = (uint64_t)__wrapped_pgs_valloc(out_kurd, DEFAULT_STACK_PG_COUNT, page_state_t::kernel_pinned, 12);
    context->regs.rsp = context->stack_top + context->stacksize;
    context->regs.rflags = INIT_DEFAULT_RFLAGS;
    task* new_task = new task(task_type_t::kthreadm, context);
    uint64_t assigned_pid = task_pool::alloc(new_task, *out_kurd);
    if(error_kurd(*out_kurd)){
        return INVALID_TID;
    }
    new_task->assign_valid_tid(assigned_pid);
    per_processor_scheduler&self_scheduler = global_schedulers[fast_get_processor_id()];
    self_scheduler.runqueue_locks[0].lock();
    if(self_scheduler.ready_queue[0].push_back(new_task)){
        new_task->set_ready();
    }else{
        //需要造kurd
        delete context;
        delete new_task;
        self_scheduler.ready_queue[0].pop_back();
        self_scheduler.runqueue_locks[0].unlock();
        return INVALID_TID;
    }
    self_scheduler.runqueue_locks[0].unlock();
    return assigned_pid;
}
void kthread_yield_true_enter(kthread_yield_raw_context *context)
{
    x64_basic_context basic_ctx{};
    convert_kthread_yield_raw_to_basic(context, &basic_ctx);
    per_processor_scheduler&scheduler=global_schedulers[fast_get_processor_id()];
    KURD_t running_task_kurd=KURD_t();
    task* interrupted_task=task_pool::get_by_tid(scheduler.now_running_tid,running_task_kurd);
    if(!interrupted_task){
        panic_with_kurd(running_task_kurd);
    }
    interrupted_task->lastest_span_length=ktime::hardware_time::get_stamp()-interrupted_task->lastest_run_stamp;
    interrupted_task->accumulated_time+=interrupted_task->lastest_span_length;
    if(interrupted_task->get_task_type()!=task_type_t::kthreadm){
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::kthread_yield_enter,
            Scheduler::self_scheduler_events::kthread_yield_enter_results::fatal_reasons::invalid_task_type
        ));
    }
    if(!interrupted_task->context.kthread){
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::kthread_yield_enter,
            Scheduler::self_scheduler_events::kthread_yield_enter_results::fatal_reasons::null_kthread_context
        ));
    }
    interrupted_task->context.kthread->regs=basic_ctx;
    if(interrupted_task->context.kthread->stacksize==0){
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::kthread_yield_enter,
            Scheduler::self_scheduler_events::kthread_yield_enter_results::fatal_reasons::invalid_stack_size
        ));
    }
    {
        vaddr_t stack_top = interrupted_task->context.kthread->stack_top;
        vaddr_t stack_bottom = stack_top + interrupted_task->context.kthread->stacksize;
        if(basic_ctx.rsp < stack_top || basic_ctx.rsp >= stack_bottom){
            panic_with_kurd(make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::kthread_yield_enter,
                Scheduler::self_scheduler_events::kthread_yield_enter_results::fatal_reasons::rsp_out_of_range
            ));
        }
    }
    scheduler.runqueue_locks[0].lock();
    scheduler.now_running_tid=~0;
    if(!scheduler.ready_queue[0].push_back(interrupted_task)){
        scheduler.runqueue_locks[0].unlock();
        //panic
    }
    scheduler.runqueue_locks[0].unlock();
    sched();
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
    interrupted_task->lastest_span_length=ktime::hardware_time::get_stamp()-interrupted_task->lastest_run_stamp;
    interrupted_task->accumulated_time+=interrupted_task->lastest_span_length;
    switch(interrupted_task->get_task_type()){
        case task_type_t::kthreadm:
        if((frame->cs&3)!=0){
            panic_with_kurd(frame, make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::timer_cpp_enter,
                Scheduler::self_scheduler_events::timer_cpp_enter_results::fatal_reasons::invalid_cs
            ));
        }
        if(!interrupted_task->context.kthread){
            panic_with_kurd(frame, make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::timer_cpp_enter,
                Scheduler::self_scheduler_events::timer_cpp_enter_results::fatal_reasons::null_kthread_context
            ));
        }
        interrupted_task->context.kthread->regs=basic_ctx;
        if(interrupted_task->context.kthread->stacksize==0){
            panic_with_kurd(frame, make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::timer_cpp_enter,
                Scheduler::self_scheduler_events::timer_cpp_enter_results::fatal_reasons::invalid_stack_size
            ));
        }
        {
            vaddr_t stack_top = interrupted_task->context.kthread->stack_top;
            vaddr_t stack_bottom = stack_top + interrupted_task->context.kthread->stacksize;
            if(basic_ctx.rsp < stack_top || basic_ctx.rsp >= stack_bottom){
                panic_with_kurd(frame, make_self_scheduler_fatal(
                    Scheduler::self_scheduler_events::timer_cpp_enter,
                    Scheduler::self_scheduler_events::timer_cpp_enter_results::fatal_reasons::rsp_out_of_range
                ));
            }
        }
            break;
        case task_type_t::userthread:
        if((frame->cs&3)!=3){
            panic_with_kurd(frame, make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::timer_cpp_enter,
                Scheduler::self_scheduler_events::timer_cpp_enter_results::fatal_reasons::invalid_cs
            ));
        }
        if(!interrupted_task->context.userthread){
            panic_with_kurd(frame, make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::timer_cpp_enter,
                Scheduler::self_scheduler_events::timer_cpp_enter_results::fatal_reasons::null_userthread_context
            ));
        }

            break;
        default:
            panic_with_kurd(frame, make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::timer_cpp_enter,
                Scheduler::self_scheduler_events::timer_cpp_enter_results::fatal_reasons::invalid_task_type
            ));
            break;
    }
    scheduler.runqueue_locks[0].lock();
    scheduler.now_running_tid=~0;
    if(!scheduler.ready_queue[0].push_back(interrupted_task)){
        scheduler.runqueue_locks[0].unlock();
        //panic
    }
    scheduler.runqueue_locks[0].unlock();
    asm volatile("sti");
    
    ktime::time_interrupt_generator::set_clock_by_offset(DEFALUT_TIMER_SPAN_MIUS);
    sched();
}
void kthread_true_exit(uint64_t will) 
{
    per_processor_scheduler&scheduler=global_schedulers[fast_get_processor_id()];
    KURD_t running_task_kurd=KURD_t();
    task*exit_task=task_pool::get_by_tid(scheduler.now_running_tid,running_task_kurd);
    if(!success_all_kurd(running_task_kurd) || !exit_task){
        panic_with_kurd(running_task_kurd);
    }
    exit_task->lastest_span_length=ktime::hardware_time::get_stamp()-exit_task->lastest_run_stamp;
    exit_task->accumulated_time+=exit_task->lastest_span_length;
    exit_task->context.kthread->regs.rax=will;
    if(!exit_task->set_zombie()){
        
    }
    sched();
}

void kthread_self_blocked_cppenter(kthread_yield_raw_context* context)
{
    x64_basic_context basic_ctx{};
    convert_kthread_yield_raw_to_basic(context, &basic_ctx);
    per_processor_scheduler&scheduler=global_schedulers[fast_get_processor_id()];
    KURD_t running_task_kurd=KURD_t();
    task* interrupted_task=task_pool::get_by_tid(scheduler.now_running_tid,running_task_kurd);
    if(!success_all_kurd(running_task_kurd) || !interrupted_task){
        panic_with_kurd(running_task_kurd);
    }
    interrupted_task->lastest_span_length=ktime::hardware_time::get_stamp()-interrupted_task->lastest_run_stamp;
    interrupted_task->accumulated_time+=interrupted_task->lastest_span_length;
    if(interrupted_task->get_task_type()!=task_type_t::kthreadm){
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::kthread_yield_enter,
            Scheduler::self_scheduler_events::kthread_yield_enter_results::fatal_reasons::invalid_task_type
        ));
    }
    if(!interrupted_task->context.kthread){
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::kthread_yield_enter,
            Scheduler::self_scheduler_events::kthread_yield_enter_results::fatal_reasons::null_kthread_context
        ));
    }
    interrupted_task->context.kthread->regs=basic_ctx;
    if(interrupted_task->context.kthread->stacksize==0){
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::kthread_yield_enter,
            Scheduler::self_scheduler_events::kthread_yield_enter_results::fatal_reasons::invalid_stack_size
        ));
    }
    {
        vaddr_t stack_top = interrupted_task->context.kthread->stack_top;
        vaddr_t stack_bottom = stack_top + interrupted_task->context.kthread->stacksize;
        if(basic_ctx.rsp < stack_top || basic_ctx.rsp >= stack_bottom){
            panic_with_kurd(make_self_scheduler_fatal(
                Scheduler::self_scheduler_events::kthread_yield_enter,
                Scheduler::self_scheduler_events::kthread_yield_enter_results::fatal_reasons::rsp_out_of_range
            ));
        }
    }
    task_blocked_reason_t reason = static_cast<task_blocked_reason_t>(context->rdi);
    if(!interrupted_task->set_blocked()){
        //panic
    }
    sched();
}
uint64_t wakeup_thread(uint64_t tid){

}