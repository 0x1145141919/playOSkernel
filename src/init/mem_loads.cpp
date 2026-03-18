#include "abi/boot.h"
#include "../init/include/kernel_mmu.h"
#include "../init/include/pages_alloc.h"
#include "../init/include/util/textConsole.h"
#include "../init/include/util/kout.h"
#include "../init/include/core_hardwares/PortDriver.h"
#include "../init/include/panic.h"
#include "../init/include/load_kernel.h"
#include "../init/include/init_linker_symbols.h"
#include "16x32AsciiCharacterBitmapSet.h"
#include "core_hardwares/primitive_gop.h"
#include "abi/boot.h"
uint64_t va_alloc(uint64_t size,uint8_t align_log2);
int setup_low_identity_maps(kernel_mmu* kmmu, BootInfoHeader* header)
{
    uint64_t entry_count = 0;
    phymem_segment* pure_view = basic_allocator::get_pure_memory_view(&entry_count);
    if (!pure_view || entry_count == 0) {
        return -1;
    }

    for (uint64_t i = 0; i < entry_count; i++) {
        phymem_segment& seg = pure_view[i];
        vinterval identity_map_interval{seg.start, seg.start, seg.size};
        pgaccess access;
        access.is_kernel = 1;
        access.is_writeable = 1;
        access.is_readable = 1;
        access.is_executable = 1;
        access.is_global = 0;

        switch (seg.type) {
            case PHY_MEM_TYPE::EFI_RUNTIME_SERVICES_CODE:
            case PHY_MEM_TYPE::EFI_RUNTIME_SERVICES_DATA:
            case PHY_MEM_TYPE::freeSystemRam:
            case PHY_MEM_TYPE::EFI_ACPI_RECLAIM_MEMORY:
            case PHY_MEM_TYPE::OS_KERNEL_DATA:
            case PHY_MEM_TYPE::OS_KERNEL_CODE:
            case PHY_MEM_TYPE::OS_KERNEL_STACK:
            case PHY_MEM_TYPE::OS_ALLOCATABLE_MEMORY:
            case PHY_MEM_TYPE::OS_MEMSEG_HOLE:
                access.cache_strategy = cache_strategy_t::WB;
                break;
            case PHY_MEM_TYPE::EFI_ACPI_MEMORY_NVS:
            case PHY_MEM_TYPE::EFI_MEMORY_MAPPED_IO:
            case PHY_MEM_TYPE::EFI_RESERVED_MEMORY_TYPE:
            case PHY_MEM_TYPE::EFI_PERSISTENT_MEMORY:
                access.cache_strategy = cache_strategy_t::UC;
                break;
            default:
                access.cache_strategy = cache_strategy_t::UC;
                break;
        }

        int result = kmmu->map(identity_map_interval, access);
        if (result != 0) {
            bsp_kout<< "[WARN] Identity map failed for segment type: ";
            bsp_kout<< static_cast<uint32_t>(seg.type);
            bsp_kout<< ", start: 0x";
            bsp_kout<< seg.start;
            bsp_kout<< ", size: 0x";
            bsp_kout<< seg.size;
            bsp_kout<< ", error code: ";
            bsp_kout<< result;
            bsp_kout<< kendl;
            continue;
        }
    }
    return 0;
}

int map_symbols_file(kernel_mmu* kmmu, load_kernel_info_pack& pak, loaded_file_entry* symbol_file)
{
    uint64_t aligned_size = align_up(symbol_file->file_size, 4096);
    vaddr_t new_base = va_alloc(aligned_size,21);
    phyaddr_t pbase = basic_allocator::pages_alloc(aligned_size,21);
    ksystemramcpy((void*)symbol_file->raw_data,(void*)pbase , symbol_file->file_size);
    pgaccess access;
    access.is_kernel = 1;
    access.is_writeable = 1;
    access.is_readable = 1;
    access.is_executable = 0;
    access.is_global = 1;
    access.cache_strategy = static_cast<cache_strategy_t>(WB);
    int result = kmmu->map(vinterval{pbase, new_base, aligned_size}, access);
    pak.VM_entries[pak.VM_entry_count] = loaded_VM_interval{pbase, new_base, aligned_size, VM_ID_KSYMBOLS, access};
    pak.VM_entry_count++;
    return result;
}

int map_gop_buffer(kernel_mmu* kmmu, load_kernel_info_pack& pak, BootInfoHeader* header)
{
    pass_through_device_info* pt_info = header->pass_through_devices;
    pass_through_device_info* device_gop = nullptr;
    pgaccess gop_buffer_map_access;
    gop_buffer_map_access.is_kernel = 1;
    gop_buffer_map_access.is_writeable = 1;
    gop_buffer_map_access.is_readable = 1;
    gop_buffer_map_access.is_executable = 0;
    gop_buffer_map_access.is_global = 1;
    gop_buffer_map_access.cache_strategy = static_cast<cache_strategy_t>(WC);
    for(uint64_t i=0;i<header->pass_through_device_info_count;i++){
        if(pt_info[i].device_info==PASS_THROUGH_DEVICE_GRAPHICS_INFO){
            device_gop=&pt_info[i];
            break;
        }
    }
    if (!device_gop) {
        return -1;
    }
    GlobalBasicGraphicInfoType* graphic = (GlobalBasicGraphicInfoType*)device_gop->specify_data;  
    uint64_t aligned_size = align_up(graphic->FrameBufferSize, 4096);
    vaddr_t new_base = va_alloc(aligned_size,21);
    phyaddr_t pbase = graphic->FrameBufferBase;
    int result = kmmu->map(vinterval{pbase, new_base, aligned_size}, gop_buffer_map_access);
    if (result != OS_SUCCESS) {
        return result;
    }
    pak.VM_entries[pak.VM_entry_count] = loaded_VM_interval{pbase, new_base, aligned_size, VM_ID_GRAPHIC_BUFFER, gop_buffer_map_access};
    pak.VM_entry_count++;
    return result;
}