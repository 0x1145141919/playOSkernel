#include "Interrupt_system/loacl_processor.h"
#include "Interrupt_system/AP_Init_error_observing_protocol.h"
#include "util/kout.h"
#include "firmware/ACPI_APIC.h"
#include "core_hardwares/lapic.h"
#include "time.h"
#include "util/cpuid_intel.h"
#include "util/textConsole.h"
check_point longmode_enter_checkpoint={0};
check_point init_finish_checkpoint={0};
constexpr uint32_t error_code_bitmap = 0
    | (1 << 8)   // #DF
    | (1 << 10)  // #TS
    | (1 << 11)  // #NP
    | (1 << 12)  // #SS
    | (1 << 13)  // #GP
    | (1 << 14)  // #PF
    | (1 << 17)  // #AC
    | (1 << 21); // #CP
KURD_t x86_smp_processors_container::AP_Init_one_by_one()
{
    //此函数会需要保证低0～64k内存的恒等映射
    // 使用gAnalyzer的processor_x64_list链表进行遍历
    KURD_t fail=default_fail();
    fail.event_code=INTERRUPT_SUB_MODULES_LOCATIONS::PROCESSORS_EVENT_CODE::EVENT_CODE_APS_INIT;
    KURD_t success=default_success();
    success.event_code=INTERRUPT_SUB_MODULES_LOCATIONS::PROCESSORS_EVENT_CODE::EVENT_CODE_APS_INIT;
    KURD_t fatal=default_fatal();
    fatal.event_code=INTERRUPT_SUB_MODULES_LOCATIONS::PROCESSORS_EVENT_CODE::EVENT_CODE_APS_INIT;
    if((uint64_t)&AP_realmode_start%4096){
        kio::bsp_kout<<"[x64_local_processor]AP_realmode_start is not aligned to 4K"<<kio::kendl;
        //ap入口必须4K对齐，否则触发bugpanic
    }
    if(gAnalyzer == nullptr) {
        kio::bsp_kout << "gAnalyzer is null, cannot initialize processors" << kio::kendl;
        fail.result=result_code::RETRY;
        fail.reason=INTERRUPT_SUB_MODULES_LOCATIONS::PROCESSORS_EVENT_CODE::APS_INIT_RESULTS_CODE::RETRY_REASON_CODE::RETRY_REASON_CODE_DEPENDIES_NOT_INITIALIZED;
        return fail;
    }
    x2apicid_t self_x2apicid = query_x2apicid();
    phyaddr_t AP_realmode_start_addr=(phyaddr_t)&AP_realmode_start;
    x2apic::x2apic_icr_t icr_sipi={
        .param={
            .vector=(uint8_t)(AP_realmode_start_addr/4096),
            .delivery_mode=6,
            .destination_mode=LAPIC_PARAMS_ENUM::DESTINATION_T::PHYSICAL,
            .reserved1=0,
            .level=1,
            .trigger_mode=0,
            .reserved2=0,
            .destination_shorthand=LAPIC_PARAMS_ENUM::DESTINATION_SHORTHAND_T::NO_SHORTHAND,
            .reserved3=0,
            .destination=0
        }
    };
    
    
    {
        x2apic::x2apic_icr_t icr_init={
        .param={
            .vector=0,
            .delivery_mode=5,
            .destination_mode=LAPIC_PARAMS_ENUM::DESTINATION_T::PHYSICAL,
            .reserved1=0,
            .level=1,
            .trigger_mode=0,
            .reserved2=0,
            .destination_shorthand=LAPIC_PARAMS_ENUM::DESTINATION_SHORTHAND_T::ALL_EXCLUDING_SELF,
            .reserved3=0,
            .destination=0
        }
        };
    kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one try init all exclued self prcessor"<<kio::kendl;
    x2apic::x2apic_driver::raw_send_ipi(icr_init);
    kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one init all exclued self prcessor sucess"<<kio::kendl;
    time::hardware_time::timer_polling_spin_delay(20000);
    }
    {
        x2apic::x2apic_icr_t icr_init_de_assert={
        .param={
            .vector=0,
            .delivery_mode=5,
            .destination_mode=LAPIC_PARAMS_ENUM::DESTINATION_T::PHYSICAL,
            .reserved1=0,
            .level=0,
            .trigger_mode=1,
            .reserved2=0,
            .destination_shorthand=LAPIC_PARAMS_ENUM::DESTINATION_SHORTHAND_T::ALL_INCLUDING_SELF,
            .reserved3=0,
            .destination=0
        }
    };
    kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one try init-de-assert all exclued self prcessor"<<kio::kendl;
    x2apic::x2apic_driver::raw_send_ipi(icr_init_de_assert);
    kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one init-de-assert all exclued self prcessor sucess"<<kio::kendl;
    time::hardware_time::timer_polling_spin_delay(1000);
    }
    
    uint32_t processor_id = 1;
    if(time::hardware_time::get_if_hpet_initialized()==false){
        fail.result=result_code::RETRY;
        fail.reason=INTERRUPT_SUB_MODULES_LOCATIONS::PROCESSORS_EVENT_CODE::APS_INIT_RESULTS_CODE::RETRY_REASON_CODE::RETRY_REASON_CODE_DEPENDIES_NOT_INITIALIZED;
        return fail;
    }
    enum ap_observe_once_result_t:uint8_t{
        WAIT_TAHT_TIME,
        SUCCESS_TAHT_TIME,
        FAIL_TAHT_TIME,
    };
    enum ap_observe_result_t:uint8_t{
        CHECKPOINT_SUCCESS,
        CHECKPOINT_FAIL,
        CHECKPOINT_TIMEOUT,
    };
    ap_observe_once_result_t (*observe_realmode)(uint32_t)=[](uint32_t succeed_word)->ap_observe_once_result_t {
        if(realmode_enter_checkpoint.success_word==succeed_word)
            return SUCCESS_TAHT_TIME;
        return WAIT_TAHT_TIME;
    };
    ap_observe_once_result_t (*observe_pemode_enter)(uint32_t)=[](uint32_t succeed_word)->ap_observe_once_result_t {
        if(pemode_enter_checkpoint.success_word==succeed_word)
            return SUCCESS_TAHT_TIME;
        if(realmode_enter_checkpoint.failure_flags&1)
            return FAIL_TAHT_TIME;
        return WAIT_TAHT_TIME;
    };
    void (*pe_fail_dealing)()=[](){
        kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one observe_realmode_enter failed with flags"<<realmode_enter_checkpoint.failure_flags<<kio::kendl;
        if(realmode_enter_checkpoint.failure_flags&2){
            kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one observe realmode exception:"<<realmode_enter_checkpoint.failure_caused_excption_num<<kio::kendl;
            uint8_t interrupt_vec=realmode_enter_checkpoint.failure_caused_excption_num;
            AP_Init_error_observing_protocol::realmode_final_stack_frame*ap_frame=(AP_Init_error_observing_protocol::realmode_final_stack_frame*)realmode_enter_checkpoint.failure_final_stack_top;
            if(ap_frame->magic==AP_Init_error_observing_protocol::realmode_magic){
                kio::bsp_kout<<kio::now<<"[Realmode Exception Frame]"<<kio::kendl;
                kio::bsp_kout<<kio::now<<"Magic: 0x"<<ap_frame->magic<<kio::kendl;
                kio::bsp_kout<<kio::now<<"CR0: 0x"<<ap_frame->cr0<<kio::kendl;
                kio::bsp_kout<<kio::now<<"GS: 0x"<<ap_frame->gs<<kio::kendl;
                kio::bsp_kout<<kio::now<<"FS: 0x"<<ap_frame->fs<<kio::kendl;
                kio::bsp_kout<<kio::now<<"SS: 0x"<<ap_frame->ss<<kio::kendl;
                kio::bsp_kout<<kio::now<<"DS: 0x"<<ap_frame->ds<<kio::kendl;
                kio::bsp_kout<<kio::now<<"ES: 0x"<<ap_frame->es<<kio::kendl;
                kio::bsp_kout<<kio::now<<"DI: 0x"<<ap_frame->di<<kio::kendl;
                kio::bsp_kout<<kio::now<<"SI: 0x"<<ap_frame->si<<kio::kendl;
                kio::bsp_kout<<kio::now<<"BP: 0x"<<ap_frame->bp<<kio::kendl;
                kio::bsp_kout<<kio::now<<"SP: 0x"<<ap_frame->sp<<kio::kendl;
                kio::bsp_kout<<kio::now<<"DX: 0x"<<ap_frame->dx<<kio::kendl;
                kio::bsp_kout<<kio::now<<"CX: 0x"<<ap_frame->cx<<kio::kendl;
                kio::bsp_kout<<kio::now<<"BX: 0x"<<ap_frame->bx<<kio::kendl;
                kio::bsp_kout<<kio::now<<"AX: 0x"<<ap_frame->ax<<kio::kendl;
                kio::bsp_kout<<kio::now<<"CS: 0x"<<ap_frame->cs<<kio::kendl;
                kio::bsp_kout<<kio::now<<"IP: 0x"<<ap_frame->ip<<kio::kendl;
                kio::bsp_kout<<kio::now<<"EFLAGS: 0x"<<ap_frame->eflags<<kio::kendl;
            }
        }
    };
    ap_observe_once_result_t (*observe_longmode_enter)(uint32_t)=[](uint32_t succeed_word)->ap_observe_once_result_t {
        if(longmode_enter_checkpoint.success_word==succeed_word)
            return SUCCESS_TAHT_TIME;
        if(pemode_enter_checkpoint.failure_flags&1)
            return FAIL_TAHT_TIME;
        return WAIT_TAHT_TIME;
    };
    void (*longmode_enter_fail_dealing)()=[](){
        kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one observe_pemode_enter failed with flags"<<pemode_enter_checkpoint.failure_flags<<kio::kendl;
        if(pemode_enter_checkpoint.failure_flags&2){
            kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one observe pemode exception:"<<pemode_enter_checkpoint.failure_caused_excption_num<<kio::kendl;
            uint8_t interrupt_vec=pemode_enter_checkpoint.failure_caused_excption_num;
            if((1ULL<<interrupt_vec)&error_code_bitmap){
                AP_Init_error_observing_protocol::pemode_final_stack_frame_with_errcode* AP_frame=
                (AP_Init_error_observing_protocol::pemode_final_stack_frame_with_errcode*)
                pemode_enter_checkpoint.failure_final_stack_top;
                if(AP_frame->magic==AP_Init_error_observing_protocol::PE_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC){
                    kio::bsp_kout<<kio::now<<"[Pemode Exception Frame with Error Code]"<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"Magic: 0x"<<AP_frame->magic<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"IA32_EFER: 0x"<<AP_frame->IA32_EFER<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR4: 0x"<<AP_frame->cr4<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR3: 0x"<<AP_frame->cr3<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR2: 0x"<<AP_frame->cr2<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR0: 0x"<<AP_frame->cr0<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"GS: 0x"<<AP_frame->gs<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"FS: 0x"<<AP_frame->fs<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"SS: 0x"<<AP_frame->ss<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"DS: 0x"<<AP_frame->ds<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"ES: 0x"<<AP_frame->es<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"EDI: 0x"<<AP_frame->edi<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"ESI: 0x"<<AP_frame->esi<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"EBP: 0x"<<AP_frame->ebp<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"ESP: 0x"<<AP_frame->esp<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"EDX: 0x"<<AP_frame->edx<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"ECX: 0x"<<AP_frame->ecx<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"EBX: 0x"<<AP_frame->ebx<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"EAX: 0x"<<AP_frame->eax<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"Error Code: 0x"<<AP_frame->errcode<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CS: 0x"<<AP_frame->cs<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"EIP: 0x"<<AP_frame->eip<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"EFLAGS: 0x"<<AP_frame->eflags<<kio::kendl;
                }
            }else{
                AP_Init_error_observing_protocol::pemode_final_stack_frame* AP_frame=
                (AP_Init_error_observing_protocol::pemode_final_stack_frame*)
                pemode_enter_checkpoint.failure_final_stack_top;
                if(AP_frame->magic==AP_Init_error_observing_protocol::PE_FINAL_STACK_NO_ERRCODE_TOP_MAGIC){
                    kio::bsp_kout<<kio::now<<"[Pemode Exception Frame without Error Code]"<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"Magic: 0x"<<AP_frame->magic<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"IA32_EFER: 0x"<<AP_frame->IA32_EFER<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR4: 0x"<<AP_frame->cr4<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR3: 0x"<<AP_frame->cr3<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR2: 0x"<<AP_frame->cr2<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR0: 0x"<<AP_frame->cr0<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"GS: 0x"<<AP_frame->gs<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"FS: 0x"<<AP_frame->fs<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"SS: 0x"<<AP_frame->ss<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"DS: 0x"<<AP_frame->ds<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"ES: 0x"<<AP_frame->es<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"EDI: 0x"<<AP_frame->edi<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"ESI: 0x"<<AP_frame->esi<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"EBP: 0x"<<AP_frame->ebp<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"ESP: 0x"<<AP_frame->esp<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"EDX: 0x"<<AP_frame->edx<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"ECX: 0x"<<AP_frame->ecx<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"EBX: 0x"<<AP_frame->ebx<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"EAX: 0x"<<AP_frame->eax<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CS: 0x"<<AP_frame->cs<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"EIP: 0x"<<AP_frame->eip<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"EFLAGS: 0x"<<AP_frame->eflags<<kio::kendl;
                }
            }
        }
    };
    ap_observe_once_result_t (*observe_finish)(uint32_t)=[](uint32_t succeed_word)->ap_observe_once_result_t {
        if(init_finish_checkpoint.success_word==succeed_word)
            return SUCCESS_TAHT_TIME;
        if(longmode_enter_checkpoint.failure_flags&1)
            return FAIL_TAHT_TIME;
        return WAIT_TAHT_TIME;
    };
    void (*finish_fail_dealing)()=[](){
        kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one observe_pemode_enter failed with flags"<<longmode_enter_checkpoint.failure_flags<<kio::kendl;
        if(longmode_enter_checkpoint.failure_flags&2){
            kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one observe pemode exception:"<<longmode_enter_checkpoint.failure_caused_excption_num<<kio::kendl;
            uint8_t interrupt_vec=longmode_enter_checkpoint.failure_caused_excption_num;
            if((1ULL<<interrupt_vec)&error_code_bitmap){
                AP_Init_error_observing_protocol::longmode_final_stack_frame_with_errcode* AP_frame=
                (AP_Init_error_observing_protocol::longmode_final_stack_frame_with_errcode*)
                longmode_enter_checkpoint.failure_final_stack_top;
                if(AP_frame->magic==AP_Init_error_observing_protocol::LONG_FINAL_STACK_WITH_ERRCODE_TOP_MAGIC){
                    kio::bsp_kout<<kio::now<<"[Longmode Exception Frame with Error Code]"<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"Magic: 0x"<<AP_frame->magic<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"IA32_EFER: 0x"<<AP_frame->IA32_EFER<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"GS: 0x"<<AP_frame->gs<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"FS: 0x"<<AP_frame->fs<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"SS: 0x"<<AP_frame->ss<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"DS: 0x"<<AP_frame->ds<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"ES: 0x"<<AP_frame->es<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR4: 0x"<<AP_frame->cr4<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR3: 0x"<<AP_frame->cr3<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR2: 0x"<<AP_frame->cr2<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR0: 0x"<<AP_frame->cr0<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R15: 0x"<<AP_frame->r15<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R14: 0x"<<AP_frame->r14<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R13: 0x"<<AP_frame->r13<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R12: 0x"<<AP_frame->r12<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R11: 0x"<<AP_frame->r11<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R10: 0x"<<AP_frame->r10<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R9: 0x"<<AP_frame->r9<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R8: 0x"<<AP_frame->r8<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RDI: 0x"<<AP_frame->rdi<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RSI: 0x"<<AP_frame->rsi<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RBP: 0x"<<AP_frame->rbp<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RDX: 0x"<<AP_frame->rdx<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RCX: 0x"<<AP_frame->rcx<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RBX: 0x"<<AP_frame->rbx<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RAX: 0x"<<AP_frame->rax<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"Error Code: 0x"<<AP_frame->errcode<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CS: 0x"<<AP_frame->cs<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RIP: 0x"<<AP_frame->rip<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RFLAGS: 0x"<<AP_frame->rflags<<kio::kendl;
                }
            }else{
                AP_Init_error_observing_protocol::longmode_final_stack_frame* AP_frame=
                (AP_Init_error_observing_protocol::longmode_final_stack_frame*)
                longmode_enter_checkpoint.failure_final_stack_top;
                if(AP_frame->magic==AP_Init_error_observing_protocol::LONG_FINAL_STACK_NO_ERRCODE_TOP_MAGIC){
                    kio::bsp_kout<<kio::now<<"[Longmode Exception Frame without Error Code]"<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"Magic: 0x"<<AP_frame->magic<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"IA32_EFER: 0x"<<AP_frame->IA32_EFER<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"GS: 0x"<<AP_frame->gs<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"FS: 0x"<<AP_frame->fs<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"SS: 0x"<<AP_frame->ss<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"DS: 0x"<<AP_frame->ds<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"ES: 0x"<<AP_frame->es<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR4: 0x"<<AP_frame->cr4<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR3: 0x"<<AP_frame->cr3<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR2: 0x"<<AP_frame->cr2<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CR0: 0x"<<AP_frame->cr0<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R15: 0x"<<AP_frame->r15<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R14: 0x"<<AP_frame->r14<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R13: 0x"<<AP_frame->r13<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R12: 0x"<<AP_frame->r12<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R11: 0x"<<AP_frame->r11<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R10: 0x"<<AP_frame->r10<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R9: 0x"<<AP_frame->r9<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"R8: 0x"<<AP_frame->r8<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RDI: 0x"<<AP_frame->rdi<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RSI: 0x"<<AP_frame->rsi<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RBP: 0x"<<AP_frame->rbp<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RDX: 0x"<<AP_frame->rdx<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RCX: 0x"<<AP_frame->rcx<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RBX: 0x"<<AP_frame->rbx<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RAX: 0x"<<AP_frame->rax<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"CS: 0x"<<AP_frame->cs<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RIP: 0x"<<AP_frame->rip<<kio::kendl;
                    kio::bsp_kout<<kio::now<<"RFLAGS: 0x"<<AP_frame->rflags<<kio::kendl;
                }
            }
        }
    };
    auto ap_init_stage_func=[](
        uint64_t delay_microseconds,
        ap_observe_once_result_t(*observe_ap)(uint32_t),//返回值0代表等待，返回值1代表成功，-1代表失败
        void(*failer_dealing)(),uint32_t success_word)->ap_observe_result_t{//启用全局错误码的情况下，0成功，1失败，2超时
        uint64_t now_microseconds = time::hardware_time::get_stamp();//GS槽位里面会专门放每个核心的时间token的
        uint64_t ddline_stamp = now_microseconds + delay_microseconds;
        while(now_microseconds < ddline_stamp){
            int observe_ap_result= observe_ap(success_word);
            if(observe_ap_result==SUCCESS_TAHT_TIME)return CHECKPOINT_SUCCESS;
            if(observe_ap_result==FAIL_TAHT_TIME){
                failer_dealing();
                return CHECKPOINT_FAIL;}    
            
            now_microseconds = time::hardware_time::get_stamp();
        }
        return CHECKPOINT_TIMEOUT;
    };
    uint64_t ipi_fai_count=0;
    // 遍历gAnalyzer的processor_x64_list链表
    for(auto it = gAnalyzer->processor_x64_list->begin(); it != gAnalyzer->processor_x64_list->end(); ++it) {
        APICtb_analyzed_structures::processor_x64_lapic_struct& proc = *it;
        // 跳过当前处理器（BSP）
        if(proc.apicid == self_x2apicid) continue;
        
        // 检查处理器是否启用（根据xAPIC或x2APIC类型）
        // 这里我们跳过检查is_bsp字段，因为我们要初始化所有AP
        
        icr_sipi.param.destination.raw = proc.apicid;
        assigned_processor_id=processor_id;
        asm volatile("sfence");
        kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one send sipi for "<<proc.apicid<<" processor"<<kio::kendl;
        GfxPrim::Flush();
        x2apic::x2apic_driver::raw_send_ipi(icr_sipi);
        
        ap_observe_result_t status= ap_init_stage_func(1000,observe_realmode,nullptr,processor_id);//只有成功/超时两种状态
        if(status==CHECKPOINT_TIMEOUT){
            kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one realmode enter timeout for processor "<<proc.apicid<<kio::kendl;
            ipi_fai_count++;
        }
        status= ap_init_stage_func(1000,observe_pemode_enter,pe_fail_dealing,proc.apicid);
        if(status==CHECKPOINT_FAIL){
            kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one pemode enter fail for processor "<<proc.apicid<<kio::kendl;
            goto stage_fail;
        }
        if(status==CHECKPOINT_TIMEOUT){
            kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one pemode enter timeout for processor "<<proc.apicid<<kio::kendl;
            goto stage_fail;
        }
        status= ap_init_stage_func(1000,observe_longmode_enter,longmode_enter_fail_dealing,~processor_id);
        if(status==CHECKPOINT_FAIL){
            kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one longmode enter fail for processor "<<proc.apicid<<kio::kendl;
            goto stage_fail;
        }
        if(status==CHECKPOINT_TIMEOUT){
            kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one longmode enter timeout for processor "<<proc.apicid<<kio::kendl;
            goto stage_fail;
        }
        status= ap_init_stage_func(20000,observe_finish,finish_fail_dealing,~proc.apicid);    
        if(status==CHECKPOINT_FAIL){
            kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one finish stage fail for processor "<<proc.apicid<<kio::kendl;
            goto stage_fail;
        }
        if(status==CHECKPOINT_TIMEOUT){
            kio::bsp_kout<<kio::now<<"[x64_local_processor]AP_Init_one_by_one finish stage timeout for processor "<<proc.apicid<<kio::kendl;
            goto stage_fail;
        }


    }
    if(ipi_fai_count){
        success.result=result_code::PARTIAL_SUCCESS;
        success.reason=INTERRUPT_SUB_MODULES_LOCATIONS::PROCESSORS_EVENT_CODE::APS_INIT_RESULTS_CODE::PARTIAL_SUCCESS_CODE::PARTIAL_SUCCESS_CODE_SOME_APS_IPI_TIME_OUT;
    }
    return success;
    stage_fail:
    fatal.reason=INTERRUPT_SUB_MODULES_LOCATIONS::PROCESSORS_EVENT_CODE::APS_INIT_RESULTS_CODE::FATAL_REASON::AP_STAGE_FAIL;
    return fatal;

}