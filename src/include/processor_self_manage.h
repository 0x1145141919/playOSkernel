#include <stdint.h>
    enum class CR0Bits : uint8_t {
        PE = 0,   // Protection Enable (bit 0)
        MP = 1,   // Monitor Coprocessor (bit 1)
        EM = 2,   // Emulation (bit 2)
        TS = 3,   // Task Switched (bit 3)
        ET = 4,   // Extension Type (bit 4)
        NE = 5,   // Numeric Error (bit 5)
        WP = 16,  // Write Protect (bit 16)
        AM = 18,  // Alignment Mask (bit 18)
        NW = 29,  // Not Write-through (bit 29)
        CD = 30,  // Cache Disable (bit 30)
        PG = 31   // Paging (bit 31)
    };
    enum class CR3Bits : uint8_t {
    PWT = 3,    // Page-level write-through (bit 3)
    PCD = 4,    // Page-level cache disable (bit 4)
    LAM57 = 61, // Linear Address Masking 57-bit (bit 61)
    LAM48 = 62  // Linear Address Masking 48-bit (bit 62)
};
    enum class CR4Bits : uint8_t {
        VME = 0,        // Virtual-8086 Mode Extensions (bit 0)
        PVI = 1,        // Protected-Mode Virtual Interrupts (bit 1)
        TSD = 2,        // Time Stamp Disable (bit 2)
        DE = 3,         // Debugging Extensions (bit 3)
        PSE = 4,        // Page Size Extensions (bit 4)
        PAE = 5,        // Physical Address Extension (bit 5)
        MCE = 6,        // Machine-Check Enable (bit 6)
        PGE = 7,        // Page Global Enable (bit 7)
        PCE = 8,        // Performance-Monitoring Counter Enable (bit 8)
        OSFXSR = 9,     // Operating System Support for FXSAVE and FXRSTOR (bit 9)
        OSXMMEXCPT = 10,// Operating System Support for Unmasked SIMD Floating-Point Exceptions (bit 10)
        UMIP = 11,      // User-Mode Instruction Prevention (bit 11)
        LA57 = 12,      // 57-bit linear addresses (bit 12)
        VMXE = 13,      // VMX-Enable Bit (bit 13)
        SMXE = 14,      // SMX-Enable Bit (bit 14)
        FSGSBASE = 16,  // FSGSBASE-Enable Bit (bit 16)
        PCIDE = 17,      // PCID-Enable Bit (bit 17)
        OSXSAVE = 18,    // XSAVE and Processor Extended States-Enable Bit (bit 18)
        KL = 19,         // Key-Locker-Enable Bit (bit 19)
        SMEP = 20,       // SMEP-Enable Bit (bit 20)
        SMAP = 21,       // SMAP-Enable Bit (bit 21)
        PKE = 22,        // Enable protection keys for user-mode pages (bit 22)
        CET = 23,        // Control-flow Enforcement Technology (bit 23)
        PKS = 24,        // Enable protection keys for supervisor-mode pages (bit 24)
        UINTR = 25,      // User Interrupts Enable Bit (bit 25)
        LASS = 27,       // Linear-address-space Separation (bit 27)
        LAM_SUP = 28     // Supervisor LAM enable (bit 28)
    };
class LocalCPU {
private:
    // 控制寄存器
    uint64_t cr0, cr2, cr3, cr4, cr8;
    
    // 状态寄存器
    uint64_t rflags;
    
    // 系统表寄存器（可选）
    struct {
        uint64_t base;
        uint16_t limit;
    } idtr, gdtr;
    
    // 任务寄存器
    uint16_t tr;  // TSS 选择子
    
    // 调试寄存器
    uint64_t dr0, dr1, dr2, dr3, dr6, dr7;
    
    // 关键 MSR（通过方法封装访问）
    uint64_t ia32_efer;
    uint64_t ia32_pat;
    uint64_t ia32_fs_base, ia32_gs_base, ia32_kernel_gs_base;
    uint64_t ia32_lstar;  // SYSCALL 入口
    
    // 浮点/SIMD
    uint32_t mxcsr;
    uint64_t xcr0;
   // 设置64位值中特定位为1
   // 系统最大物理地址位数 (MAXPHYADDR)
    uint8_t max_phy_addr = 52; // 默认值52，实际需通过CPUID初始化

    // 通用掩码
    static constexpr uint64_t CR3_RESERVED_MASK = (1ULL << 63); // 位63必须为0
    static constexpr uint64_t CR3_LAM_MASK = (1ULL << 61) | (1ULL << 62);

    // PCIDE=0时的掩码
    static constexpr uint64_t CR3_PCIDE0_ADDR_MASK = 0x000FFFFFFFFFF000; // M=52时的地址掩码
    static constexpr uint64_t CR3_PCIDE0_IGNORED_MASK = 0x0000000000000FE0; // 位11:5

    // PCIDE=1时的掩码
    static constexpr uint64_t CR3_PCID_MASK = 0x0000000000000FFF; // 位11:0
    static constexpr uint64_t CR3_PCIDE1_ADDR_MASK = 0x000FFFFFFFFFF000; // M=52时的地址掩码
    
    uint64_t set_bit(uint64_t value, uint8_t bit_position) const;
    uint64_t clear_bit(uint64_t value, uint8_t bit_position) const;
    uint64_t get_cr3_address_mask() const;

public:
    
    /*
    暂时只初始化加载cr0, cr2, cr3, cr4, cr8，rflags，idtr, gdtr这几个寄存器
     */
    LocalCPU();
    ~LocalCPU();
    uint64_t get_cr0() const;
    void set_cr0(uint64_t value);
    void load_cr0();
    void set_cr0_bit(CR0Bits bit, bool enable);
    bool get_cr0_bit(CR0Bits bit) const;

    // CR4 相关接口
    uint64_t get_cr4() const;
    void set_cr4(uint64_t value);
    void load_cr4();
    void set_cr4_bit(CR4Bits bit, bool enable);
    bool get_cr4_bit(CR4Bits bit) const;
        // CR3 相关接口
    void set_cr3_base_address(uint64_t base_addr);
    void set_cr3_pcid(uint16_t pcid);
    void set_cr3_bit(CR3Bits bit, bool enable);
    uint64_t get_cr3_base_address() const;
    uint16_t get_cr3_pcid() const;
    uint64_t get_cr3() const;
    void set_cr3(uint64_t value);
    void load_cr3();
    
    /*
    其他寄存器值获取和设置函数
    */
    uint64_t get_cr2();
    void set_cr2(uint64_t value);
    void load_cr2();
    
    uint64_t get_cr8();
    void set_cr8(uint64_t value);
    void load_cr8();
    
   uint64_t get_ia32_pat();
    void set_ia32_pat(uint64_t value);
    void load_ia32_pat();
    /*
    一系列寄存器值获取函数
    */
   /*
   一系列寄存器设置函数，只能设定寄存器中某个成员，最好不要直接设置整个寄存器的值
   */
  /*
  对于cr0,cr3,cr4，rflags这种内含多个成员的寄存器设置专门的加载函数，
  也就是说前面的函数是先设置类里面的寄存器成员变量的值，后面再设置寄存器
  */
    /*
    先暂时做cr0,cr3,cr4,这几个寄存器的设置，类暂存值加载到寄存器的函数吧
    */
};