#include "panic.h"
#include "core_hardwares/VideoDriver.h"
#include <efi.h>
#include <efilib.h>
#include "firmware/UefiRunTimeServices.h"
#include "util/kout.h"
#include "util/kptrace.h"
#include "util/OS_utils.h"
#include "msr_offsets_definitions.h"
#include "linker_symbols.h"
using namespace kio;

#ifdef USER_MODE
#include <unistd.h>
#endif 

panic_last_will will;
bool KernelPanicManager::is_latest_panic_valid;
// 注意：global_gST已经在UefiRunTimeServices.cpp中定义，此处不再重复定义
// EFI_SYSTEM_TABLE* global_gST = nullptr;

void KernelPanicManager::write_will()
{
    uint64_t start_addr = (uint64_t)&__heap_bitmap_start;
    uint64_t end_addr = (uint64_t)&__heap_bitmap_end;
    
    // 将起始地址对齐到4KB边界
    start_addr = (start_addr + 0xFFF) & (~0xFFF);
    
    // 遍历__heap_bitmap_start和__heap_bitmap_end之间每个4KB页对齐的地址并复制panic_last_will结构
    for (uint64_t addr = start_addr; addr + sizeof(panic_last_will) <= end_addr; addr += 0x1000) {
        panic_last_will* will_ptr = (panic_last_will*)addr;
        ksystemramcpy(&will, will_ptr, sizeof(panic_last_will));
    }
    
    // 同样处理__heap_start到__heap_end之间的内存区域
    start_addr = (uint64_t)&__heap_start;
    end_addr = (uint64_t)&__heap_end;
    
    // 将起始地址对齐到4KB边界
    start_addr = (start_addr + 0xFFF) & (~0xFFF);
    
    // 遍历__heap_start和__heap_end之间每个4KB页对齐的地址并复制panic_last_will结构
    for (uint64_t addr = start_addr; addr + sizeof(panic_last_will) <= end_addr; addr += 0x1000) {
        panic_last_will* will_ptr = (panic_last_will*)addr;
        ksystemramcpy(&will, will_ptr, sizeof(panic_last_will));
    }
}

/**
 * 私有构造函数
 */
KernelPanicManager::KernelPanicManager(){
    // shutdownDelay已经作为静态成员初始化为5
}

KernelPanicManager::~KernelPanicManager()
{
}





panic_context::x64_context KernelPanicManager::convert_to_panic_context(x64_Interrupt_saved_context_no_errcode *regs)
{
    panic_context::x64_context context{};
    
    // 直接从参数中复制已有寄存器值
    context.rax = regs->rax;
    context.rbx = regs->rbx;
    context.rcx = regs->rcx;
    context.rdx = regs->rdx;
    context.rsi = regs->rsi;
    context.rdi = regs->rdi;
    context.r8 = regs->r8;
    context.r9 = regs->r9;
    context.r10 = regs->r10;
    context.r11 = regs->r11;
    context.r12 = regs->r12;
    context.r13 = regs->r13;
    context.r14 = regs->r14;
    context.r15 = regs->r15;
    context.rbp = regs->rbp;
    context.rip = regs->rip;
    context.cs = regs->cs;
    context.rflags = regs->rflags;
    context.rsp = regs->rsp;
    context.ss = regs->ss;
    
    // 使用内联汇编获取其他寄存器值
    uint16_t ds_val, es_val, fs_val, gs_val;
    uint64_t rflags_full, cr0_val, cr2_val, cr3_val, cr4_val, efer_val;
    uint64_t fs_base_val, gs_base_val;
    
    asm volatile("movw %%ds, %0" : "=r"(ds_val));
    asm volatile("movw %%es, %0" : "=r"(es_val));
    asm volatile("movw %%fs, %0" : "=r"(fs_val));
    asm volatile("movw %%gs, %0" : "=r"(gs_val));
    
    asm volatile("pushf; pop %0" : "=rm"(rflags_full));
    
    asm volatile("mov %%cr0, %0" : "=r"(cr0_val));
    asm volatile("mov %%cr2, %0" : "=r"(cr2_val));
    asm volatile("mov %%cr3, %0" : "=r"(cr3_val));
    asm volatile("mov %%cr4, %0" : "=r"(cr4_val));
    
    // 读取EFER MSR
    efer_val = rdmsr(msr::IA32_EFER);
    
    // 读取FS/GS基址
    fs_base_val = rdmsr(msr::syscall::IA32_FS_BASE);
    gs_base_val = rdmsr(msr::syscall::IA32_GS_BASE);
    
    // 获取GDTR和IDTR
    asm volatile("sgdt %0" : "=m"(context.gdtr));
    asm volatile("sidt %0" : "=m"(context.idtr));
    
    // 设置获取的值
    context.ds = ds_val;
    context.es = es_val;
    context.fs = fs_val;
    context.gs = gs_val;
    context.rflags = rflags_full;
    context.cr0 = cr0_val;
    context.cr2 = cr2_val;
    context.cr3 = cr3_val;
    context.cr4 = cr4_val;
    context.IA32_EFER = efer_val;
    context.fs_base = fs_base_val;
    context.gs_base = gs_base_val;
    context.specific.is_hadware_interrupt = 0;
    return context;
}

panic_context::x64_context KernelPanicManager::convert_to_panic_context(x64_Interrupt_saved_context *regs, uint8_t vec_num)
{
    panic_context::x64_context context{};
    
    // 直接从参数中复制已有寄存器值
    context.rax = regs->rax;
    context.rbx = regs->rbx;
    context.rcx = regs->rcx;
    context.rdx = regs->rdx;
    context.rsi = regs->rsi;
    context.rdi = regs->rdi;
    context.r8 = regs->r8;
    context.r9 = regs->r9;
    context.r10 = regs->r10;
    context.r11 = regs->r11;
    context.r12 = regs->r12;
    context.r13 = regs->r13;
    context.r14 = regs->r14;
    context.r15 = regs->r15;
    context.rbp = regs->rbp;
    context.rip = regs->rip;
    context.cs = regs->cs;
    context.rflags = regs->rflags;
    context.rsp = regs->rsp;
    context.ss = regs->ss;
    
    // 使用内联汇编获取其他寄存器值
    uint16_t ds_val, es_val, fs_val, gs_val;
    uint64_t rflags_full, cr0_val, cr2_val, cr3_val, cr4_val, efer_val;
    uint64_t fs_base_val, gs_base_val;
    
    asm volatile("movw %%ds, %0" : "=r"(ds_val));
    asm volatile("movw %%es, %0" : "=r"(es_val));
    asm volatile("movw %%fs, %0" : "=r"(fs_val));
    asm volatile("movw %%gs, %0" : "=r"(gs_val));
    
    asm volatile("pushf; pop %0" : "=rm"(rflags_full));
    
    asm volatile("mov %%cr0, %0" : "=r"(cr0_val));
    asm volatile("mov %%cr2, %0" : "=r"(cr2_val));
    asm volatile("mov %%cr3, %0" : "=r"(cr3_val));
    asm volatile("mov %%cr4, %0" : "=r"(cr4_val));
    
    // 读取EFER MSR
    efer_val = rdmsr(msr::IA32_EFER);
    
    // 读取FS/GS基址
    fs_base_val = rdmsr(msr::syscall::IA32_FS_BASE);
    gs_base_val = rdmsr(msr::syscall::IA32_GS_BASE);
    
    // 获取GDTR和IDTR
    asm volatile("sgdt %0" : "=m"(context.gdtr));
    asm volatile("sidt %0" : "=m"(context.idtr));
    
    // 设置获取的值
    context.ds = ds_val;
    context.es = es_val;
    context.fs = fs_val;
    context.gs = gs_val;
    context.rflags = rflags_full;
    context.cr0 = cr0_val;
    context.cr2 = cr2_val;
    context.cr3 = cr3_val;
    context.cr4 = cr4_val;
    context.IA32_EFER = efer_val;
    context.fs_base = fs_base_val;
    context.gs_base = gs_base_val;
    
    // 设置特定错误信息
    context.specific.interrupt_vec_num = vec_num;
    context.specific.hardware_errorcode= regs->errcode;
    context.specific.is_hadware_interrupt = 1;
    return context;
}
/**
 * 转储 panic_context 中的 CPU 寄存器信息
 */
void KernelPanicManager::dumpregisters(panic_context::x64_context* regs) {
    kio::bsp_kout << "================= CPU REGISTERS DUMP =================" << kio::kendl;
    kio::bsp_kout << "General Purpose Registers:" << kio::kendl;
    kio::bsp_kout.shift_hex();
    kio::bsp_kout << "RAX: 0x" << regs->rax << kio::kendl;
    kio::bsp_kout << "RBX: 0x" << regs->rbx << kio::kendl;
    kio::bsp_kout << "RCX: 0x" << regs->rcx << kio::kendl;
    kio::bsp_kout << "RDX: 0x" << regs->rdx << kio::kendl;
    kio::bsp_kout << "RSI: 0x" << regs->rsi << kio::kendl;
    kio::bsp_kout << "RDI: 0x" << regs->rdi << kio::kendl;
    kio::bsp_kout << "RSP: 0x" << regs->rsp << kio::kendl;
    kio::bsp_kout << "RBP: 0x" << regs->rbp << kio::kendl;
    kio::bsp_kout << "R8:  0x" << regs->r8 << kio::kendl;
    kio::bsp_kout << "R9:  0x" << regs->r9 << kio::kendl;
    kio::bsp_kout << "R10: 0x" << regs->r10 << kio::kendl;
    kio::bsp_kout << "R11: 0x" << regs->r11 << kio::kendl;
    kio::bsp_kout << "R12: 0x" << regs->r12 << kio::kendl;
    kio::bsp_kout << "R13: 0x" << regs->r13 << kio::kendl;
    kio::bsp_kout << "R14: 0x" << regs->r14 << kio::kendl;
    kio::bsp_kout << "R15: 0x" << regs->r15 << kio::kendl;
    
    kio::bsp_kout << "\nControl Registers:" << kio::kendl;
    kio::bsp_kout << "CR0: 0x" << regs->cr0 << kio::kendl;
    kio::bsp_kout << "CR2: 0x" << regs->cr2 << kio::kendl;
    kio::bsp_kout << "CR3: 0x" << regs->cr3 << kio::kendl;
    kio::bsp_kout << "CR4: 0x" << regs->cr4 << kio::kendl;
    kio::bsp_kout << "EFER: 0x" << regs->IA32_EFER << kio::kendl;
    
    kio::bsp_kout << "\nSegment Registers:" << kio::kendl;
    kio::bsp_kout << "CS: 0x" << regs->cs << kio::kendl;
    kio::bsp_kout << "DS: 0x" << regs->ds << kio::kendl;
    kio::bsp_kout << "ES: 0x" << regs->es << kio::kendl;
    kio::bsp_kout << "FS: 0x" << regs->fs << kio::kendl;
    kio::bsp_kout << "GS: 0x" << regs->gs << kio::kendl;
    kio::bsp_kout << "SS: 0x" << regs->ss << kio::kendl;
    
    kio::bsp_kout << "\nOther Registers:" << kio::kendl;
    kio::bsp_kout << "RFLAGS: 0x" << regs->rflags << kio::kendl;
    kio::bsp_kout << "RIP: 0x" << regs->rip << kio::kendl;
    kio::bsp_kout << "FS_BASE: 0x" << regs->fs_base << kio::kendl;
    kio::bsp_kout << "GS_BASE: 0x" << regs->gs_base << kio::kendl;
    
    kio::bsp_kout << "\nDescriptor Tables:" << kio::kendl;
    kio::bsp_kout << "GDTR: Limit=0x" << regs->gdtr.limit << ", Base=0x" << regs->gdtr.base << kio::kendl;
    kio::bsp_kout << "IDTR: Limit=0x" << regs->idtr.limit << ", Base=0x" << regs->idtr.base << kio::kendl;
    
    // 如果是硬件中断，打印额外信息
    if (regs->specific.is_hadware_interrupt) {
        kio::bsp_kout << "\nHardware Interrupt Information:" << kio::kendl;
        kio::bsp_kout << "Hardware Error Code: 0x" << regs->specific.hardware_errorcode << kio::kendl;
        kio::bsp_kout << "Interrupt Vector Number: 0x" << (uint32_t)regs->specific.interrupt_vec_num << kio::kendl;
    }
    
    kio::bsp_kout << "=====================================================" << kio::kendl;
}
