SECTION .text
global atoimc_kthread_load
bits 64
%define kthread_call_ivec 226
%define KTHREAD_CALL_EXIT   0
%define KTHREAD_CALL_SLEEP  1
%define KTHREAD_CALL_YIELD  2
%define KTHREAD_CALL_JOIN   3
%define KTHREAD_CALL_BLOCK  4
atoimc_kthread_load:
    mov rsp, rdi
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rsi
    pop rdi
    pop rbp
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15  
    iretq
extern kthread_yield_true_enter
extern get_scheduler_private_stack_top
global kthread_yield
kthread_yield:
    
    mov rax, KTHREAD_CALL_YIELD
    int kthread_call_ivec
    ret
    nop
global kthread_exit
extern kthread_true_exit
kthread_exit:
    mov rax, KTHREAD_CALL_EXIT
    int kthread_call_ivec
    ud2
global kthread_self_blocked
extern kthread_self_blocked_cppenter
kthread_self_blocked:
    mov rax, KTHREAD_CALL_BLOCK
    int kthread_call_ivec
    ret
    nop
global kthread_sleep
extern kthread_sleep_cppenter
kthread_sleep:
    mov rax, KTHREAD_CALL_SLEEP
    int kthread_call_ivec
    ret