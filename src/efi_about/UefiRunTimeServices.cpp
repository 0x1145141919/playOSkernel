#include "UefiRunTimeServices.h"
#include "util/OS_utils.h"
#include  "memory/Memory.h"
#include "memory/AddresSpace.h"
#include "os_error_definitions.h"
EFI_SYSTEM_TABLE*global_gST;
EFI_SYSTEM_TABLE* EFI_RT_SVS::gST;
EFI_RESET_SYSTEM EFI_RT_SVS::reset_system;
EFI_GET_TIME EFI_RT_SVS::get_time;
EFI_SET_TIME EFI_RT_SVS::set_time;
EFI_GET_WAKEUP_TIME EFI_RT_SVS::get_wakeup_time;
EFI_SET_WAKEUP_TIME EFI_RT_SVS::set_wakeup_time;
EFI_SET_VIRTUAL_ADDRESS_MAP EFI_RT_SVS::set_virtual_address_map;
EFI_CONVERT_POINTER EFI_RT_SVS::convert_pointer;
EFI_RT_SVS::EFI_RT_SVS()
{
}
int EFI_RT_SVS::Init(EFI_SYSTEM_TABLE *sti,uint64_t mapver)
{
    gST = sti;
    uint64_t filter_maxcount = gBaseMemMgr.getEfiMemoryDescriptorTableEntryCount();
    EFI_MEMORY_DESCRIPTORX64*entries_fileter= new EFI_MEMORY_DESCRIPTORX64[filter_maxcount];
    EFI_MEMORY_DESCRIPTORX64*copyptr=gBaseMemMgr.getEfiMemoryDescriptorTable();
    uint16_t gST_locate_index=0;
    for (uint64_t i = 0; i < filter_maxcount; i++)
    {
        if (copyptr[i].Attribute & EFI_MEMORY_RUNTIME)
        {
            vaddr_t vbase=(vaddr_t)KspaceMapMgr::pgs_remapp(copyptr[i].PhysicalStart,4096*copyptr[i].NumberOfPages,KspaceMapMgr::PG_RWX,0);
            if(!vbase)return OS_MEMRY_ALLOCATE_FALT;            
            copyptr[i].VirtualStart=(vaddr_t)vbase;
            if(copyptr[i].PhysicalStart<=(phyaddr_t)sti&&(phyaddr_t)sti<(copyptr[i].PhysicalStart+4096*copyptr[i].NumberOfPages))
            {
                gST_locate_index=i;
            }
        }
    }
    reset_system = (EFI_RESET_SYSTEM)gST->RuntimeServices->ResetSystem;
    get_time = (EFI_GET_TIME)gST->RuntimeServices->GetTime;
    set_time = (EFI_SET_TIME)gST->RuntimeServices->SetTime;
    get_wakeup_time = (EFI_GET_WAKEUP_TIME)gST->RuntimeServices->GetWakeupTime;
    set_wakeup_time = (EFI_SET_WAKEUP_TIME)gST->RuntimeServices->SetWakeupTime;
    set_virtual_address_map = (EFI_SET_VIRTUAL_ADDRESS_MAP)gST->RuntimeServices->SetVirtualAddressMap;
    convert_pointer = (EFI_CONVERT_POINTER)gST->RuntimeServices->ConvertPointer;
    gST=(EFI_SYSTEM_TABLE*)(copyptr[gST_locate_index].VirtualStart-copyptr[gST_locate_index].PhysicalStart+(uint64_t)sti);
    return OS_SUCCESS;
}

EFI_TIME EFI_RT_SVS::rt_time_get()
{
    EFI_STATUS status;
    EFI_TIME time;
    
    status = efi_call2((void*)get_time, (UINT64)&time, (UINT64)NULL);
    if(status != EFI_SUCCESS)
    {
        setmem(&time,sizeof(EFI_TIME),0);
    }
    return time;
}

EFI_STATUS EFI_RT_SVS::rt_time_set(EFI_TIME &time)
{
    EFI_STATUS status;
    // 使用efi_call1函数调用set_time
    status = efi_call1((void*)set_time, (UINT64)&time);
    if (status != EFI_SUCCESS)
    {
        // 如果普通调用失败，尝试使用ms_abi调用
        typedef EFI_STATUS (__attribute__((msabi)) *MsabiSetTime)(EFI_TIME*);
        MsabiSetTime msabi = (MsabiSetTime)set_time;
        status = msabi(&time);
    }
    if (status != EFI_SUCCESS)
    {
        return status;
    }
    
    return EFI_SUCCESS;
}

EFI_STATUS EFI_RT_SVS::rt_reset(EFI_RESET_TYPE reset_type, EFI_STATUS status, uint64_t data_size, void *data_ptr)
{
    // 使用efi_call4函数调用reset_system
    efi_call4((void*)reset_system, (UINT64)reset_type, (UINT64)status, (UINT64)data_size, (UINT64)data_ptr);
    
    // 如果普通调用失败，尝试使用ms_abi调用
    typedef EFI_STATUS (__attribute__((msabi)) *MsabiResetSystem)(EFI_RESET_TYPE, EFI_STATUS, UINTN, VOID*);
    MsabiResetSystem MsAbi = (MsabiResetSystem)reset_system;
    MsAbi(reset_type, status, data_size, (CHAR16 *)data_ptr);
    
    return EFI_SUCCESS;
}



void EFI_RT_SVS::rt_hotreset()
{
    rt_reset(EfiResetWarm,EFI_SUCCESS,0,NULL);
}

void EFI_RT_SVS::rt_coldreset()
{
    rt_reset(EfiResetCold,EFI_SUCCESS,0,NULL);
}

void EFI_RT_SVS::rt_shutdown()
{
    rt_reset(EfiResetShutdown,EFI_SUCCESS,0,NULL);
    asm volatile("hlt");
}


