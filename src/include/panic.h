#pragma once

#include "pt_regs.h"
#include "kernelTypename.h"
#include <cstdarg>

// 内核恐慌信息包结构体
typedef struct {
    bool has_regs;           // 是否包含寄存器信息
    pt_regs regs;            // 寄存器状态
    // 可以在未来添加更多字段，如进程信息、内存状态等
} panic_info_t;

/**
 * 内核恐慌管理器
 * 单例模式实现，用于处理内核严重错误
 */
class KernelPanicManager {
private:
    static int shutdownDelay; // 关机前等待时间（秒）
    
    // 私有构造函数，防止外部实例化
    
    

public:
    KernelPanicManager();
    ~KernelPanicManager();
    static void Init(uint64_t delay_sec);  
    // 设置关机前等待时间
    static void setShutdownDelay(int seconds);
    
    // 打印寄存器信息
    static void dumpRegisters(const pt_regs& regs);
    
    // 触发内核恐慌，打印错误信息并停机（无额外信息）
    static void panic(const char* message);
    
    // 触发内核恐慌，打印错误信息和寄存器信息并停机
    static void panic(const char* message, const pt_regs& regs);
    
    
    // 带信息包的内核恐慌函数
    static void panic(const char* message, const panic_info_t& info);
};