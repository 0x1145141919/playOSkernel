#include "Scheduler/per_processor_scheduler.h"
#include "panic.h"
#include "core_hardwares/lapic.h"
#include "Interrupt_system/loacl_processor.h"
#include "msr_offsets_definitions.h"
#include "util/kout.h"
namespace {
constexpr uint64_t kthread_yield_saved_stack_delta = 16 * sizeof(uint64_t);
spinlock_cpp_t global_tid_lock;
uint64_t global_tid_counter = 0;

static inline uint64_t alloc_global_tid_monotonic()
{
    global_tid_lock.lock();
    const uint64_t tid = global_tid_counter;
    ++global_tid_counter;
    global_tid_lock.unlock();
    return tid;
}

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
task* per_processor_scheduler::create_kthread(void (*func)(void *data), void *data, create_kthread_param *param)
{
    lock.lock();
    auto return_with_unlock = [&](task* value) -> task* {
        lock.unlock();
        return value;
    };
    uint32_t result_task_idx=processor_self_task_pool.alloc(param->result_kurd);
    if(!success_all_kurd(param->result_kurd)){
        return return_with_unlock(nullptr);
    }
    task_node* node=processor_self_task_pool.get_task_node(result_task_idx);
    if(!node){
        param->result_kurd=set_fatal_result_level(param->result_kurd);
        return return_with_unlock(nullptr);
    }
    kthread_context*context=new kthread_context();
    ksetmem_8(context,0,sizeof(kthread_context));
    context->regs.rip=(uint64_t)func;
    context->regs.rdi=(uint64_t)data;
    node->task_ptr=new task(task_type_t::kthreadm, alloc_global_tid_monotonic(), context);
    node->task_ptr->location.in_pool_index = result_task_idx;
    node->task_ptr->location.processor_id = read_gs_u64(PROCESSOR_ID_GS_INDEX);
    context->stacksize=param->kthread_stack_size;
    vaddr_t stack_base=(vaddr_t)__wrapped_pgs_valloc(&param->result_kurd,param->kthread_stack_size/4096,KERNEL,12);
    context->stack_top=stack_base;
    if(!success_all_kurd(param->result_kurd)){
        delete context;
        delete node->task_ptr;
        processor_self_task_pool.free(result_task_idx);
        return return_with_unlock(nullptr);
    }
    if(param->is_soon_ready){
        node->task_ptr->set_ready();
        ready_queue[0].push_tail(result_task_idx);
    }
    context->regs.rsp=context->stack_top+context->stacksize;
    context->regs.rflags=INIT_DEFAULT_RFLAGS;
    return return_with_unlock(node->task_ptr);
}
void kthread_yield_true_enter(kthread_yield_raw_context *context)
{
    x64_basic_context basic_ctx{};
    convert_kthread_yield_raw_to_basic(context, &basic_ctx);
    per_processor_scheduler*scheduler=(per_processor_scheduler*)read_gs_u64(SCHEDULER_PRIVATE_GS_INDEX);
    if(!scheduler){
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::kthread_yield_enter,
            Scheduler::self_scheduler_events::kthread_yield_enter_results::fatal_reasons::null_scheduler
        ));
    }
    KURD_t running_task_kurd=KURD_t();
    task* interrupted_task=scheduler->get_now_running_task(running_task_kurd);
    if(!success_all_kurd(running_task_kurd) || !interrupted_task){
        panic_with_kurd(running_task_kurd);
    }
    interrupted_task->lastest_span_length=time::hardware_time::get_stamp()-interrupted_task->lastest_run_stamp;
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
    KURD_t kurd=scheduler->task_set_ready(interrupted_task);
    if(!success_all_kurd(kurd)){
        panic_with_kurd(kurd);
    }
    scheduler->schedule_and_switch();
}
void timer_cpp_enter(x64_Interrupt_saved_context_no_errcode *frame)
{
    asm volatile("cli");
    x64_basic_context basic_ctx{};
    KURD_t kurd=KURD_t();
    convert_interrupt_no_err_to_basic(frame, &basic_ctx);
    per_processor_scheduler*scheduler=(per_processor_scheduler*)read_gs_u64(SCHEDULER_PRIVATE_GS_INDEX);
    if(!scheduler){
        panic_with_kurd(frame, make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::timer_cpp_enter,
            Scheduler::self_scheduler_events::timer_cpp_enter_results::fatal_reasons::null_scheduler
        ));
    }
    KURD_t running_task_kurd=KURD_t();
    task* interrupted_task=scheduler->get_now_running_task(running_task_kurd);
    if(!success_all_kurd(running_task_kurd) || !interrupted_task){
        panic_with_kurd(frame, running_task_kurd);
    }
    interrupted_task->lastest_span_length=time::hardware_time::get_stamp()-interrupted_task->lastest_run_stamp;
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
    kurd=scheduler->task_set_ready(interrupted_task);
    if(!success_all_kurd(kurd)){
        panic_with_kurd(frame, kurd);
    }
    x2apic::x2apic_driver::write_eoi();
    //uint64_t apic_fault=rdmsr(msr::apic::IA32_X2APIC_ESR);
    //kio::bsp_kout<<"wrong with apic_error_status"<<apic_fault<<kio::kendl;
    asm volatile("sti");
    time::time_interrupt_generator::set_clock_by_offset(DEFALUT_TIMER_SPAN_MIUS);
    scheduler->schedule_and_switch();
}
void kthread_true_exit(uint64_t will) 
{
    per_processor_scheduler*scheduler=(per_processor_scheduler*)read_gs_u64(SCHEDULER_PRIVATE_GS_INDEX);
    KURD_t running_task_kurd=KURD_t();
    task*exit_task=scheduler->get_now_running_task(running_task_kurd);
    if(!success_all_kurd(running_task_kurd) || !exit_task){
        panic_with_kurd(running_task_kurd);
    }
    exit_task->lastest_span_length=time::hardware_time::get_stamp()-exit_task->lastest_run_stamp;
    exit_task->accumulated_time+=exit_task->lastest_span_length;
    exit_task->context.kthread->regs.rax=will;
    KURD_t kurd=scheduler->task_set_zombie(exit_task);
    if(!success_all_kurd(kurd)){
        panic_with_kurd(kurd);
    }
    scheduler->schedule_and_switch();
}
void kthread_dead_exit_cppenter()
{
    per_processor_scheduler*scheduler=(per_processor_scheduler*)read_gs_u64(SCHEDULER_PRIVATE_GS_INDEX);
    KURD_t running_task_kurd=KURD_t();
    task*dying_task=scheduler->get_now_running_task(running_task_kurd);
    if(!success_all_kurd(running_task_kurd) || !dying_task){
        panic_with_kurd(running_task_kurd);
    }
    KURD_t kurd= scheduler->task_set_zombie(dying_task);
    if(error_kurd(kurd)){
        panic_with_kurd(kurd);
    }
    delete dying_task->context.kthread;
    delete dying_task;
    kurd= scheduler->task_set_dead(dying_task);
    if(error_kurd(kurd)){
        panic_with_kurd(kurd);
    }
    scheduler->schedule_and_switch();
}

void kthread_self_blocked_cppenter(kthread_yield_raw_context* context)
{
    x64_basic_context basic_ctx{};
    convert_kthread_yield_raw_to_basic(context, &basic_ctx);
    per_processor_scheduler* scheduler=(per_processor_scheduler*)read_gs_u64(SCHEDULER_PRIVATE_GS_INDEX);
    if(!scheduler){
        panic_with_kurd(make_self_scheduler_fatal(
            Scheduler::self_scheduler_events::kthread_yield_enter,
            Scheduler::self_scheduler_events::kthread_yield_enter_results::fatal_reasons::null_scheduler
        ));
    }
    KURD_t running_task_kurd=KURD_t();
    task* interrupted_task=scheduler->get_now_running_task(running_task_kurd);
    if(!success_all_kurd(running_task_kurd) || !interrupted_task){
        panic_with_kurd(running_task_kurd);
    }
    interrupted_task->lastest_span_length=time::hardware_time::get_stamp()-interrupted_task->lastest_run_stamp;
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
    KURD_t kurd=scheduler->task_set_blocked(interrupted_task, reason);
    if(!success_all_kurd(kurd)){
        panic_with_kurd(kurd);
    }
    scheduler->schedule_and_switch();
}
