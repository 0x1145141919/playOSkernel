
#include <stdint.h>
#include <efi.h>
#include <efilib.h>

//
// ACPI 表签名常量定义（仅C++）
//
constexpr const char* ACPI_XSDT_SIGNATURE = "XSDT";
constexpr const char* ACPI_FADT_SIGNATURE = "FACP";
constexpr const char* ACPI_FACS_SIGNATURE = "FACS";
constexpr const char* ACPI_MADT_SIGNATURE = "APIC";
constexpr const char* ACPI_DSDT_SIGNATURE = "DSDT";
constexpr const char* ACPI_SSDT_SIGNATURE = "SSDT";
constexpr const char* ACPI_MCFG_SIGNATURE = "MCFG";



struct RSDP_struct{
    //
    // ACPI 1.0 基本字段 (前20字节)
    //
    CHAR8     Signature[8];          // 签名: "RSD PTR " (包含末尾空格)
    UINT8     Checksum;              // 前20字节的校验和 (字节0-19，包含本字段，总和必须为0)
    CHAR8     OemId[6];              // OEM提供的厂商识别字符串
    UINT8     Revision;              // 结构版本号 (ACPI 1.0为0，当前为2)
    UINT32    RsdtAddress;           // RSDT的32位物理地址
    
    //
    // ACPI 2.0+ 扩展字段
    //
    UINT32    Length;                // 整个RSDP表的长度(字节)，从偏移0开始
    UINT64    XsdtAddress;           // XSDT的64位物理地址
    UINT8     ExtendedChecksum;      // 整个表的校验和(包含两个校验和字段)
    UINT8     Reserved[3];           // 保留字段
} __attribute__((packed));

struct ACPI_Table_Header {
    char        Signature[4];        // 4字节 @偏移0: 表标识符的ASCII字符串表示
    uint32_t Length;            // 4字节 @偏移4: 整个表的长度（包含表头）
    uint8_t  Revision;          // 1字节 @偏移8: 对应签名字段的结构版本号
    uint8_t  Checksum;          // 1字节 @偏移9: 整个表的校验和（包含本字段，总和必须为0）
    char        OEMID[6];            // 6字节 @偏移10: OEM提供的厂商标识字符串
    char        OEM_Table_ID[8];     // 8字节 @偏移16: OEM提供的特定数据表标识字符串
    uint32_t OEM_Revision;      // 4字节 @偏移24: OEM提供的版本号
    char        Creator_ID[4];       // 4字节 @偏移28: 创建此表的工具厂商ID
    uint32_t Creator_Revision;  // 4字节 @偏移32: 创建此表的工具版本号

} __attribute__((packed));

//
// XSDT (Extended System Description Table)
//
struct XSDT_Table {
    struct ACPI_Table_Header Header;
    uint64_t Entry[/* 可变数量 */];
} __attribute__((packed));

//
// FADT (Fixed ACPI Description Table)
//
struct FADT_Table {
    struct ACPI_Table_Header Header;
    uint32_t FirmwareCtrl;              // 32位FACS地址
    uint32_t Dsdt;                      // 32位DSDT地址
    
    // 其他字段省略，这里只保留关键字段用于演示
    uint8_t  Reserved0[12];             // 保留字段
    
    uint32_t SMI_CommandPort;           // SMI命令I/O端口

} __attribute__((packed));

//
// FACS (Firmware ACPI Control Structure)
//
struct FACS_Table {
    char     Signature[4];              // "FACS"
    uint32_t Length;                    // 表长度
    uint32_t HardwareSignature;         // 硬件配置签名
    uint32_t FirmwareWakingVector;      // 32位唤醒向量
    uint32_t GlobalLock;                // 全局锁
    uint32_t Flags;                     // 标志位
    uint64_t XFirmwareWakingVector;     // 64位唤醒向量
    uint8_t  Version;                   // 版本
    uint8_t  Reserved[31];              // 保留字段
} __attribute__((packed));

//
// MADT (Multiple APIC Description Table)
//
struct MADT_Table {
    struct ACPI_Table_Header Header;
    uint32_t LocalApicAddress;          // 本地APIC地址
    uint32_t Flags;                     // 多种标志
    // 后续是可变长度的APIC结构数组
} __attribute__((packed));

//
// DSDT (Differentiated System Description Table)
// DSDT结构根据系统而异，通常包含AML代码，这里只定义表头
//
struct DSDT_Table {
    struct ACPI_Table_Header Header;
    // 后续是AML字节码
} __attribute__((packed));

//
// SSDT (Secondary System Description Table)
// SSDT同样包含AML代码，结构与DSDT类似
//
struct SSDT_Table {
    struct ACPI_Table_Header Header;
    // 后续是AML字节码
} __attribute__((packed));

//
// MCFG (PCI Memory Mapped Configuration Space Access Table)
//
struct MCFG_Table {
    struct ACPI_Table_Header Header;
    uint8_t  Reserved[8];               // 保留字段
    // 后续是设备配置空间描述符数组
} __attribute__((packed));

// 条件编译支持C和C++

class acpimgr_t {
private:
    SSDT_Table*vSSDT[40];
    // 对应的uint32_t类型常量
static constexpr uint32_t XSDT_SIGNATURE_UINT32 = 0x54445358; // 'XSDT'
static constexpr uint32_t FADT_SIGNATURE_UINT32 = 0x50434146; // 'FACP'
static constexpr uint32_t FACS_SIGNATURE_UINT32 = 0x53434146; // 'FACS'
static constexpr uint32_t MADT_SIGNATURE_UINT32 = 0x43495041; // 'APIC'
static constexpr uint32_t DSDT_SIGNATURE_UINT32 = 0x54445344; // 'DSDT'
static constexpr uint32_t SSDT_SIGNATURE_UINT32 = 0x54445353; // 'SSDT'
static constexpr uint32_t MCFG_SIGNATURE_UINT32 = 0x4746434D; // 'MCFG'
    uint16_t xsdt_entry_count;    
    RSDP_struct*vRSDP;
    XSDT_Table*vXSDT;
    FADT_Table*vFADT;
    FACS_Table*vFACS;
    MADT_Table*vMADT;
    DSDT_Table*vDSDT;
    MCFG_Table*vMCFG;
public:

    void Init(EFI_SYSTEM_TABLE* st);
    void* get_acpi_table(char* signature);
};
extern acpimgr_t gAcpiVaddrSapceMgr;
