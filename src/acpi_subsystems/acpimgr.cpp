#include "gSTResloveAPIs.h"
#include "memory/Memory.h"
#include "panic.h"
acpimgr_t gAcpiVaddrSapceMgr;
typedef uint64_t GUID_DEF[2];
bool is_guid_equal(GUID_DEF *a, GUID_DEF *b)
{
    return ((*a)[0] == (*b)[0]) && ((*a)[1] == (*b)[1]);
}

void acpimgr_t::Init(EFI_SYSTEM_TABLE *st)
{
    RSDP_struct *rsdp_phy;
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
    phy_memDescriptor *des;
    uint64_t xsdt_phy = rsdp_phy->XsdtAddress;
    des = gBaseMemMgr.queryPhysicalMemoryUsage((phyaddr_t)xsdt_phy);
    if (des->Type!=EFI_ACPI_RECLAIM_MEMORY)
    {
        gkernelPanicManager.panic("acpimgr_t::Init: XSDT table is not in EFI_ACPI_RECLAIM_MEMORY");
    }
    uint64_t acpi_seg_offset = des->VirtualStart - des->PhysicalStart;
    vXSDT = (XSDT_Table*)(xsdt_phy + acpi_seg_offset);
    xsdt_entry_count = (vXSDT->Header.Length - sizeof(ACPI_Table_Header)) / 8;
    for (int i = 0; i < xsdt_entry_count; i++)
    {
       vXSDT->Entry[i]+=acpi_seg_offset;
       uint32_t Sig=*(uint32_t*)vXSDT->Entry[i];
       switch (Sig)
       {
       case FADT_SIGNATURE_UINT32:
       vFADT = (FADT_Table*)vXSDT->Entry[i];
        vDSDT=(DSDT_Table*)(vFADT->Dsdt+acpi_seg_offset);
        break;
         case FACS_SIGNATURE_UINT32:
       vFACS = (FACS_Table*)vXSDT->Entry[i];
        /* code */
        break;
         case MADT_SIGNATURE_UINT32:
       vMADT = (MADT_Table*)vXSDT->Entry[i];
       break;
       case MCFG_SIGNATURE_UINT32:
       vMCFG = (MCFG_Table*)vXSDT->Entry[i];
       break;
       default:
        break;
       }
    }
}
void *acpimgr_t::get_acpi_table(char *signature)
{
    // 将字符签名转换为uint32_t进行比较
    uint32_t sig = *(uint32_t*)signature;
    
    switch (sig) {
    case XSDT_SIGNATURE_UINT32:
        return vXSDT;
    case FADT_SIGNATURE_UINT32:
        return vFADT;
    case FACS_SIGNATURE_UINT32:
        return vFACS;
    case MADT_SIGNATURE_UINT32:
        return vMADT;
    case DSDT_SIGNATURE_UINT32:
        return vDSDT;
    case MCFG_SIGNATURE_UINT32:
        return vMCFG;
    default:
        // 对于未直接作为类成员的SSDT表或其他表，可以在XSDT中查找
        for (int i = 0; i < xsdt_entry_count; i++) {
            uint32_t entry_sig = *(uint32_t*)vXSDT->Entry[i];
            if (entry_sig == sig) {
                return (void*)vXSDT->Entry[i];
            }
        }
        break;
    }
    
    return nullptr;
}