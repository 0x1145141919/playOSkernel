#include "gSTResloveAPIs.h"
#include "memory/Memory.h"
#include "memory/AddresSpace.h"
#include "os_error_definitions.h"
#include "panic.h"
acpimgr_t gAcpiVaddrSapceMgr;
typedef uint64_t GUID_DEF[2];
bool is_guid_equal(GUID_DEF *a, GUID_DEF *b)
{
    return ((*a)[0] == (*b)[0]) && ((*a)[1] == (*b)[1]);
}

int acpimgr_t::Init(EFI_SYSTEM_TABLE *st)
{
    RSDP_struct *rsdp_phy = nullptr;
    uint8_t configtb_count=st->NumberOfTableEntries;
    bool compare_result;
    EFI_CONFIGURATION_TABLE *configtb_base = st->ConfigurationTable;
    EFI_GUID acpi_guid = ACPI_20_TABLE_GUID;
    for (int i = 0; i < configtb_count; i++)
    {
       compare_result = is_guid_equal((GUID_DEF *)&acpi_guid, (GUID_DEF *)&configtb_base[i].VendorGuid);
       if (compare_result)
       {
           rsdp_phy = (RSDP_struct *)configtb_base[i].VendorTable;
           break;
       }
    }
    
    // 检查是否找到ACPI表
    if (!rsdp_phy) {
        return OS_ACPI_NOT_FOUNED;
    }
    phy_memDescriptor *des;
    uint64_t xsdt_phy = rsdp_phy->XsdtAddress;
    des = gBaseMemMgr.queryPhysicalMemoryUsage((phyaddr_t)xsdt_phy);
    if (!des || des->Type!=EFI_ACPI_RECLAIM_MEMORY)
    {
        return OS_INVALID_PARAMETER;
    }
    acpi_seg_vbase=(vaddr_t)KspaceMapMgr::pgs_remapp(des->PhysicalStart, des->NumberOfPages * 0x1000, KspaceMapMgr::PG_RW,0);
    acpi_seg_pbase=des->PhysicalStart;
    acpi_seg_size=des->NumberOfPages * 0x1000;
    if(acpi_seg_vbase==0)return OS_MEMRY_ALLOCATE_FALT;
    uint64_t acpi_seg_offset = acpi_seg_vbase - des->PhysicalStart;
    XSDT_OFFSET = xsdt_phy-acpi_seg_pbase;
    XSDT_Table*vXSDT = (XSDT_Table*)(XSDT_OFFSET+acpi_seg_pbase);
    xsdt_entry_count = (vXSDT->Header.Length - sizeof(ACPI_Table_Header)) / sizeof(uint64_t);
    for (int i = 0; i < xsdt_entry_count; i++)
    {
       uint32_t Sig=*(uint32_t*)vXSDT->Entry[i];
       switch (Sig)
       {
        case FADT_SIGNATURE_UINT32:
        {
        FADT_OFFSET = (uint64_t)((FADT_Table*)vXSDT->Entry[i])-acpi_seg_pbase;
        FADT_Table*vFADT = (FADT_Table*)(acpi_seg_pbase+FADT_OFFSET);
        DSDT_OFFSET = (uint64_t)(vFADT->Dsdt-acpi_seg_pbase);
        break;
        }
        case FACS_SIGNATURE_UINT32:
        FACS_OFFSET = (uint64_t)((FACS_Table*)vXSDT->Entry[i])-acpi_seg_pbase;
        /* code */
        break;
        case MADT_SIGNATURE_UINT32:
        MADT_OFFSET = (uint64_t)((MADT_Table*)vXSDT->Entry[i])-acpi_seg_pbase;
        break;
        case MCFG_SIGNATURE_UINT32:
        MCFG_OFFSET = (uint64_t)((MCFG_Table*)vXSDT->Entry[i])-acpi_seg_pbase;
        break;
        case HPET_SIGNATURE_UINT32:
        HPET_OFFSET = (uint64_t)((HPET_Table*)vXSDT->Entry[i])-acpi_seg_pbase;
        break;
        default:
        break;
       }
    }
    
    return OS_SUCCESS;
}
void *acpimgr_t::get_acpi_table(char *signature)
{
    // 检查acpi_seg_vbase是否已初始化
    if (!acpi_seg_vbase) {
        return nullptr;
    }
    
    // 将字符签名转换为uint32_t进行比较
    uint32_t sig = *(uint32_t*)signature;
    
    switch (sig) {
    case XSDT_SIGNATURE_UINT32:
        return (void*)(acpi_seg_vbase+XSDT_OFFSET);
    case FADT_SIGNATURE_UINT32:
        return (void*)(acpi_seg_vbase+FADT_OFFSET);
    case FACS_SIGNATURE_UINT32:
        return (void*)(acpi_seg_vbase+FACS_OFFSET);
    case MADT_SIGNATURE_UINT32:
        return (void*)(acpi_seg_vbase+MADT_OFFSET);
    case DSDT_SIGNATURE_UINT32:
        return (void*)(acpi_seg_vbase+DSDT_OFFSET);
    case MCFG_SIGNATURE_UINT32:
        return (void*)(acpi_seg_vbase+MCFG_OFFSET);
    case HPET_SIGNATURE_UINT32:
        return (void*)(acpi_seg_vbase+HPET_OFFSET);
    default:
        // 对于未直接作为类成员的SSDT表或其他表，可以在XSDT中查找
        XSDT_Table*vXSDT = (XSDT_Table*)(acpi_seg_vbase+XSDT_OFFSET);
        for (int i = 0; i < xsdt_entry_count; i++) {
            uint32_t entry_sig = *(uint32_t*)vXSDT->Entry[i]; // 修复：直接从Entry获取签名
            if (entry_sig == sig) {
                return (void*)vXSDT->Entry[i];
            }
        }
        break;
    }
    
    return nullptr;
}