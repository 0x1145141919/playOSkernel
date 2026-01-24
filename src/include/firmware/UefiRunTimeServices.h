#include <efi.h>


class EFI_RT_SVS
{   
    static EFI_SYSTEM_TABLE *gST;
    static EFI_RESET_SYSTEM reset_system;
    static EFI_GET_TIME get_time;
    static EFI_SET_TIME set_time;
    static EFI_GET_WAKEUP_TIME get_wakeup_time;
    static EFI_SET_WAKEUP_TIME set_wakeup_time;
    static EFI_SET_VIRTUAL_ADDRESS_MAP set_virtual_address_map;
    static EFI_CONVERT_POINTER convert_pointer;
    static bool is_virtual;//标记这些是否加载到高位空间内
    public:
        EFI_RT_SVS();
        EFI_RT_SVS(EFI_SYSTEM_TABLE *sti, uint64_t mapver);
        static EFI_TIME rt_time_get();
        static EFI_STATUS rt_time_set(EFI_TIME &time);
        static EFI_STATUS rt_reset(
            EFI_RESET_TYPE reset_type,
            EFI_STATUS status,
            uint64_t data_size,
            void *data_ptr);
        static int Init(EFI_SYSTEM_TABLE *sti, uint64_t mapver);
        static void rt_hotreset();
        static void rt_coldreset();
        static void rt_shutdown();
};
extern EFI_SYSTEM_TABLE*global_gST;