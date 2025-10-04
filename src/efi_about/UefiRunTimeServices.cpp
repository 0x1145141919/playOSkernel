#include "UefiRunTimeServices.h"
#include "OS_utils.h"
#include  "../memory/includes/Memory.h"
EFI_SYSTEM_TABLE*global_gST;
runtime_services_t gRuntimeServices;
runtime_services_t::runtime_services_t()
{
}
runtime_services_t::runtime_services_t(EFI_SYSTEM_TABLE *sti,uint64_t mapver)
{
    gST = sti;
    uint64_t filter_maxcount = gBaseMemMgr.getRootPhysicalMemoryDescriptorTableEntryCount();
    phy_memDesriptor entries_fileter[filter_maxcount];
    phy_memDesriptor*copyptr=gBaseMemMgr.getGlobalPhysicalMemoryInfo();
    uint64_t filter_count = 0;
    for (uint64_t i = 0; i < filter_maxcount; i++)
    {
        if (copyptr[i].Type == PHY_MEM_TYPE::EFI_RUNTIME_SERVICES_CODE ||
            copyptr[i].Type == PHY_MEM_TYPE::EFI_RUNTIME_SERVICES_DATA)
        {
            entries_fileter[filter_count] = copyptr[i];
            filter_count++;
    }
    }
    filter_count++;
    EFI_STATUS status;

    EFI_SET_VIRTUAL_ADDRESS_MAP set_virtual_address_map=gST->RuntimeServices->SetVirtualAddressMap;
    status = efi_call4((void*)set_virtual_address_map,
        (UINT64)(filter_count * sizeof(EFI_MEMORY_DESCRIPTORX64)),
        (UINT64)sizeof(EFI_MEMORY_DESCRIPTORX64),
        (UINT64)mapver,
        (UINT64)entries_fileter);
    if (status != EFI_SUCCESS)
    {
        return;
    }
    reset_system = (EFI_RESET_SYSTEM)gST->RuntimeServices->ResetSystem;
    get_time = (EFI_GET_TIME)gST->RuntimeServices->GetTime;
    set_time = (EFI_SET_TIME)gST->RuntimeServices->SetTime;
    get_wakeup_time = (EFI_GET_WAKEUP_TIME)gST->RuntimeServices->GetWakeupTime;
    set_wakeup_time = (EFI_SET_WAKEUP_TIME)gST->RuntimeServices->SetWakeupTime;
    set_virtual_address_map = (EFI_SET_VIRTUAL_ADDRESS_MAP)gST->RuntimeServices->SetVirtualAddressMap;
    convert_pointer = (EFI_CONVERT_POINTER)gST->RuntimeServices->ConvertPointer;
}

EFI_TIME runtime_services_t::rt_time_get()
{
    EFI_STATUS status;
    EFI_TIME time;
    // 使用efi_call2函数调用get_time
    status = efi_call2((void*)get_time, (UINT64)&time, (UINT64)NULL);
    if(status != EFI_SUCCESS)
    {
        setmem(&time,sizeof(EFI_TIME),0);
    }
    return time;
}

EFI_STATUS runtime_services_t::rt_time_set(EFI_TIME &time)
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

EFI_STATUS runtime_services_t::rt_reset(EFI_RESET_TYPE reset_type, EFI_STATUS status, uint64_t data_size, void *data_ptr)
{
    // 使用efi_call4函数调用reset_system
    efi_call4((void*)reset_system, (UINT64)reset_type, (UINT64)status, (UINT64)data_size, (UINT64)data_ptr);
    
    // 如果普通调用失败，尝试使用ms_abi调用
    typedef EFI_STATUS (__attribute__((msabi)) *MsabiResetSystem)(EFI_RESET_TYPE, EFI_STATUS, UINTN, VOID*);
    MsabiResetSystem MsAbi = (MsabiResetSystem)reset_system;
    MsAbi(reset_type, status, data_size, (CHAR16 *)data_ptr);
    
    return EFI_SUCCESS;
}

void runtime_services_t::Init(EFI_SYSTEM_TABLE *sti, uint64_t mapver)
{
    gST = sti;
   /* uint64_t filter_maxcount = gBaseMemMgr.getRootPhysicalMemoryDescriptorTableEntryCount();
    phy_memDesriptor entries_fileter[filter_maxcount];
    phy_memDesriptor*copyptr=gBaseMemMgr.getGlobalPhysicalMemoryInfo();
 uint64_t filter_count = 0;
    for (uint64_t i = 0; i < filter_maxcount; i++)
    {
        if (copyptr[i].Type == PHY_MEM_TYPE::EFI_RUNTIME_SERVICES_CODE ||
            copyptr[i].Type == PHY_MEM_TYPE::EFI_RUNTIME_SERVICES_DATA)
        {
            entries_fileter[filter_count] = copyptr[i];
            filter_count++;
    }
    }
    EFI_STATUS status;

    EFI_SET_VIRTUAL_ADDRESS_MAP set_virtual_address_map=gST->RuntimeServices->SetVirtualAddressMap;
    status = efi_call4((void*)set_virtual_address_map,
        (UINT64)(filter_count * sizeof(EFI_MEMORY_DESCRIPTORX64)),
        (UINT64)sizeof(EFI_MEMORY_DESCRIPTORX64),
        (UINT64)mapver,
        (UINT64)entries_fileter);
    if (status != EFI_SUCCESS)
    {
        return;
    }*/   
    reset_system = (EFI_RESET_SYSTEM)gST->RuntimeServices->ResetSystem;
    get_time = (EFI_GET_TIME)gST->RuntimeServices->GetTime;
    set_time = (EFI_SET_TIME)gST->RuntimeServices->SetTime;
    get_wakeup_time = (EFI_GET_WAKEUP_TIME)gST->RuntimeServices->GetWakeupTime;
    set_wakeup_time = (EFI_SET_WAKEUP_TIME)gST->RuntimeServices->SetWakeupTime;
    set_virtual_address_map = (EFI_SET_VIRTUAL_ADDRESS_MAP)gST->RuntimeServices->SetVirtualAddressMap;
    convert_pointer = (EFI_CONVERT_POINTER)gST->RuntimeServices->ConvertPointer;
}


void runtime_services_t::rt_hotreset()
{
    rt_reset(EfiResetWarm,EFI_SUCCESS,0,NULL);
}

void runtime_services_t::rt_coldreset()
{
    rt_reset(EfiResetCold,EFI_SUCCESS,0,NULL);
}

void runtime_services_t::rt_shutdown()
{
    rt_reset(EfiResetShutdown,EFI_SUCCESS,0,NULL);
    asm volatile("hlt");
}


