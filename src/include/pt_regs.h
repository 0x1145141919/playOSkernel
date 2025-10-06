#pragma once
#include<cstdint>
//用于保存当前上下文（寄存器、栈等，由 pt_regs 结构体封装）
struct pt_regs {
    /*
     * 第一部分：通用寄存器（软件手动压栈）
     * 顺序：从 r15 到 rdi，与汇编 push 顺序一一对应
     * 包含所有非易失性寄存器（r15~rbx）和易失性寄存器（r11~rdi）
     */
    uint64_t r15;
    uint64_t r14;
    uint64_t r13;
    uint64_t r12;
    uint64_t rbp;  // 栈基址寄存器（函数调用帧指针）
    uint64_t rbx;  // 非易失性寄存器（需保存）
    uint64_t r11;  // 易失性寄存器（函数调用临时值）
    uint64_t r10;
    uint64_t r9;
    uint64_t r8;
    uint64_t rax;  // 累加器（系统调用返回值常用）
    uint64_t rcx;  // 计数器（函数第4参数）
    uint64_t rdx;  // 数据寄存器（函数第3参数）
    uint64_t rsi;  // 源变址寄存器（函数第2参数）
    uint64_t rdi;  // 目的变址寄存器（函数第1参数）

    /*
     * 第二部分：软件扩展字段（非硬件自动压栈）
     * orig_rax：用于区分系统调用（值为系统调用号，-1 表示非系统调用）
     * 仅在系统调用或相关异常中有效
     */
    uint64_t orig_rax;

    /*
     * 第三部分：硬件自动压栈寄存器（Intel 64 架构强制顺序）
     * 异常触发时 CPU 自动按此顺序压栈，软件不可修改
     */
    uint64_t rip;       // 指令指针（异常发生时的下一条指令地址）
    uint64_t cs;        // 代码段寄存器（低 2 位为 CPL 特权级：0=内核态，3=用户态）
    uint64_t eflags;    // 标志寄存器（64位实际为 rflags，兼容 32 位命名）
    uint64_t rsp;       // 栈指针（异常发生时的栈顶地址，用户态/内核态）
    uint64_t ss;        // 栈段寄存器（与 rsp 配合使用）
};

