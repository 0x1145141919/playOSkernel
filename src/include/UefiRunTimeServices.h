#include <efi.h>


class runtime_services_t
{   
    EFI_SYSTEM_TABLE *gST;
    EFI_RESET_SYSTEM reset_system;
    EFI_GET_TIME get_time;
    EFI_SET_TIME set_time;
    EFI_GET_WAKEUP_TIME get_wakeup_time;
    EFI_SET_WAKEUP_TIME set_wakeup_time;
    EFI_SET_VIRTUAL_ADDRESS_MAP set_virtual_address_map;
    EFI_CONVERT_POINTER convert_pointer;
    bool is_virtual;//标记这些是否加载到高位空间内
    public:
        runtime_services_t();
        runtime_services_t(EFI_SYSTEM_TABLE *sti, uint64_t mapver);
        EFI_TIME rt_time_get();
        EFI_STATUS rt_time_set(EFI_TIME &time);
        EFI_STATUS rt_reset(
            EFI_RESET_TYPE reset_type,
            EFI_STATUS status,
            uint64_t data_size,
            void *data_ptr);
        void Init(EFI_SYSTEM_TABLE *sti, uint64_t mapver);
        void rt_hotreset();
        void rt_coldreset();
        void rt_shutdown();
};
extern runtime_services_t gRuntimeServices;
extern EFI_SYSTEM_TABLE*global_gST;