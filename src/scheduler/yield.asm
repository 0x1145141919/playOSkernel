SECTION .text
global atoimc_kthread_load
bits 64

atoimc_kthread_load:
    ; rdi = context pointer

    ; -------- 1. 恢复通用寄存器（除了 rdi / rsp） --------
    mov     rax, [rdi + 0x00]
    mov     rbx, [rdi + 0x08]
    mov     rcx, [rdi + 0x10]
    mov     rdx, [rdi + 0x18]
    mov     rsi, [rdi + 0x20]
    mov     rbp, [rdi + 0x30]

    mov     r8,  [rdi + 0x40]
    mov     r9,  [rdi + 0x48]
    mov     r10, [rdi + 0x50]
    mov     r11, [rdi + 0x58]
    mov     r12, [rdi + 0x60]
    mov     r13, [rdi + 0x68]
    mov     r14, [rdi + 0x70]
    mov     r15, [rdi + 0x78]

    ; -------- 2. 切换到目标线程栈 --------
    mov     rsp, [rdi + 0x38]

    ; -------- 3. 在目标栈上伪造“返回现场” --------
    ; 栈布局（低地址在下）：
    ;   +----------------+
    ;   |     rip        |  <- ret 使用
    ;   +----------------+
    ;   |    rflags      |  <- popfq
    ;   +----------------+

    push    qword [rdi + 0x80]    ; rip
    push    qword [rdi + 0x88]    ; rflags

    ; -------- 4. 恢复 rflags --------
    popfq

    ; -------- 5. 恢复 rdi（最后一步） --------
    mov     rdi, [rdi + 0x28]

    ; -------- 6. 原子切换到新 rip --------
    ret
extern kthread_yield_true_enter
extern get_scheduler_private_stack_top
global kthread_yield
kthread_yield:
    pushfq
    cli
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    push rsp
    mov r12, rsp
    ; 从 GS.base + 24 获取当前 CPU 的 per_processor_scheduler
    mov rbx, qword [gs:24]
    ; 读取该 scheduler 的私有栈顶
    mov rdi, rbx
    call get_scheduler_private_stack_top
    mov rsp, rax
    mov rdi, r12
    sti
    call kthread_yield_true_enter
global exit_kthread
extern kthread_true_exit
exit_kthread:
    cli
    mov r12, rdi
    mov rbx, qword [gs:24]
    ; 读取该 scheduler 的私有栈顶
    mov rdi, rbx
    call get_scheduler_private_stack_top
    mov rsp, rax
    mov rdi, r12
    sti
    call kthread_true_exit
global kthread_dead_exit
extern kthread_dead_exit_cppenter
kthread_dead_exit:
    cli
    mov r12, rdi
    mov rbx, qword [gs:24]
    ; 读取该 scheduler 的私有栈顶
    mov rdi, rbx
    call get_scheduler_private_stack_top
    mov rsp, rax
    mov rdi, r12
    sti
    call kthread_dead_exit_cppenter

global kthread_self_blocked
extern kthread_self_blocked_cppenter
kthread_self_blocked:
    pushfq
    cli
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rbp
    push rdi
    push rsi
    push rdx
    push rcx
    push rbx
    push rax
    push rsp
    mov r12, rsp
    mov rbx, qword [gs:24]
    mov rdi, rbx
    call get_scheduler_private_stack_top
    mov rsp, rax
    mov rdi, r12
    sti
    call kthread_self_blocked_cppenter
