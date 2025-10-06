

#include <stdint.h>
#include <stdbool.h>

// ========================== CPU 特性枚举 ==========================
// CPU 制造商
typedef enum {
    CPU_VENDOR_INTEL,
    CPU_VENDOR_AMD,
    CPU_VENDOR_UNKNOWN
} cpu_vendor_t;

// CPU 特性标志（通过 CPUID 检测）
typedef struct {
    bool pae;       // 物理地址扩展（支持 64GB 内存）
    bool lm;        // 长模式（64位支持）
    bool mmx;       // MMX 指令集
    bool sse;       // SSE 指令集
    bool sse2;      // SSE2 指令集
    bool sse3;      // SSE3 指令集
    bool ssse3;     // SSSE3 指令集
    bool aes;       // AES 加密指令集
    bool avx;       // AVX 指令集
    bool mce;       // 机器检查错误支持
    bool apic;      // 本地 APIC 支持
} cpu_features_t;

// CPU 信息结构体
typedef struct {
    cpu_vendor_t vendor;        // 制造商
    char vendor_id[13];         // 制造商 ID 字符串（如 "GenuineIntel"）
    char brand_string[49];      // 型号名称（如 "Intel(R) Core(TM) i7-8700K"）
    uint32_t family;            // 家族 ID
    uint32_t model;             // 型号 ID
    uint32_t stepping;          // 步进 ID
    cpu_features_t features;    // 支持的特性
} cpu_info_t;

