#include "memory/all_pages_arr.h"
#include "abi/os_error_definitions.h"
#include "util/OS_utils.h"
#include "memory/kpoolmemmgr.h"
#include "linker_symbols.h"
#include "memory/phyaddr_accessor.h"
#include "util/kout.h"
#include "panic.h"
#include "util/kptrace.h"
uint64_t all_pages_arr::mem_map_entry_count;
page*all_pages_arr::mem_map;
void *ptr_dump(page *p)
{
    return (void*)(0xFFFF000000000000+(uint64_t(p->head.ptr)<<4));
}

all_pages_arr::free_segs_t* all_pages_arr::free_segs_get()
{
    if (!mem_map || mem_map_entry_count == 0) {
        return nullptr;
    }
    auto is_free_4kb = [](const page& p) -> bool {
        return !p.page_flags.bitfield.is_skipped
            && p.page_flags.bitfield.is_allocateble
            && p.head.order == 0
            && p.head.type == static_cast<uint64_t>(page_state_t::free)
            && p.refcount == 0;
    };

    uint64_t seg_count = 0;
    bool in_run = false;
    uint64_t run_start = 0;
    for (uint64_t idx = 0; idx < mem_map_entry_count; ++idx) {
        if (is_free_4kb(mem_map[idx])) {
            if (!in_run) {
                in_run = true;
                run_start = idx;
            }
        } else if (in_run) {
            seg_count++;
            in_run = false;
        }
    }
    if (in_run) {
        seg_count++;
    }

    free_segs_t* result = new free_segs_t();
    result->count = seg_count;
    if (seg_count == 0) {
        result->entries = nullptr;
        return result;
    }

    result->entries = new free_segs_t::entry_t[seg_count];
    uint64_t out_idx = 0;
    in_run = false;
    run_start = 0;
    for (uint64_t idx = 0; idx < mem_map_entry_count; ++idx) {
        if (is_free_4kb(mem_map[idx])) {
            if (!in_run) {
                in_run = true;
                run_start = idx;
            }
        } else if (in_run) {
            uint64_t run_len = idx - run_start;
            result->entries[out_idx++] = {
                .base = run_start << 12,
                .size = run_len << 12
            };
            in_run = false;
        }
    }
    if (in_run) {
        uint64_t run_len = mem_map_entry_count - run_start;
        result->entries[out_idx++] = {
            .base = run_start << 12,
            .size = run_len << 12
        };
    }

    return result;
}

KURD_t all_pages_arr::Init(init_to_kernel_info *info)
{
    phymem_segment*segs=info->memory_map;
    uint64_t physegs_count=info->phymem_segment_count;
    loaded_VM_interval*mem_map_interval=nullptr;
    for(int i=0;i<info->loaded_VM_interval_count;i++)
    { 
        if(info->loaded_VM_intervals[i].VM_interval_specifyid==VM_ID_MEM_MAP){
            mem_map_interval=&info->loaded_VM_intervals[i];
        }
    }
    if(!mem_map_interval){
        return set_fatal_result_level(KURD_t());
    }
    mem_map=(page*)mem_map_interval->vbase;
    mem_map_entry_count=mem_map_interval->size/sizeof(page);
    auto pages_for = [](phyaddr_t base, uint64_t size, uint64_t& out_start, uint64_t& out_end) -> bool {
        if (size == 0) {
            out_start = 0;
            out_end = 0;
            return false;
        }
        if (base > ~0ULL - size) {
            return false;
        }
        uint64_t end = base + size;
        out_start = base >> 12;
        out_end = (end + 4095) >> 12;
        return true;
    };
    auto register_range = [&](phyaddr_t base,
                              uint64_t size,
                              page_state_t type,
                              bool allocatable) {
        uint64_t start_idx = 0;
        uint64_t end_idx = 0;
        if (!pages_for(base, size, start_idx, end_idx)) {
            return;
        }
        if (start_idx >= mem_map_entry_count) {
            return;
        }
        if (end_idx > mem_map_entry_count) {
            end_idx = mem_map_entry_count;
        }
        for (uint64_t idx = start_idx; idx < end_idx; ++idx) {
            page& p = mem_map[idx];
            p.refcount = 0;
            p.page_flags.raw = 0;
            p.page_flags.bitfield.is_skipped = 0;
            p.page_flags.bitfield.is_allocateble = allocatable ? 1 : 0;
            p.head.type = static_cast<uint64_t>(type);
            p.head.ptr = 0;
            p.head.order = 0;
        }
    };
    for(int i=0;i<physegs_count;i++)
    {
        page_state_t type = page_state_t::reserved;
        bool allocatable = false;
        switch (segs[i].type) {
        case EFI_LOADER_CODE:
        case EFI_LOADER_DATA:
        case EFI_BOOT_SERVICES_CODE:
        case EFI_BOOT_SERVICES_DATA:
        case freeSystemRam:
        case OS_ALLOCATABLE_MEMORY:
            type = page_state_t::free;
            allocatable = true;
            break;
        case EFI_ACPI_RECLAIM_MEMORY:
        type = page_state_t::acpi_tables;
            allocatable = false;
            break;
        case EFI_ACPI_MEMORY_NVS:
            type =page_state_t::acpi_nvs;
            allocatable = false;
            break;
        case EFI_RUNTIME_SERVICES_CODE:
        case EFI_RUNTIME_SERVICES_DATA:
        type =page_state_t:: uefi_runtime;
            allocatable = false;
            break;
        case EFI_MEMORY_MAPPED_IO:
            type = page_state_t::mmio;
            allocatable = false;
            break;
        default:
            type = page_state_t::reserved;
            allocatable = false;
            break;
        }
        register_range(segs[i].start, segs[i].size, type, allocatable);
    }
    loaded_VM_interval*kintervals_base=info->loaded_VM_intervals;
    for(int i=0;i<info->loaded_VM_interval_count;i++){

        const loaded_VM_interval& interval = kintervals_base[i];
        page_state_t type = page_state_t::kernel_persisit;
        bool allocatable = false;
        if (interval.VM_interval_specifyid == VM_ID_GRAPHIC_BUFFER) {
            type = page_state_t::mmio;
            allocatable = false;
        }
        register_range(interval.pbase, interval.size, type, allocatable);
    }
    register_range((phyaddr_t)info, info->self_pages_count * 4096, page_state_t::kernel_persisit, false);
    register_range(info->kmmu_interval.start, info->kmmu_interval.size, page_state_t::kernel_persisit, false);
    PhyAddrAccessor::BASIC_DESC.SEG_SIZE_ONLY_UES_IN_BASIC_SEG=mem_map_entry_count<<12;
    return KURD_t();
}
void all_pages_arr::simp_pages_set(phyaddr_t phybase, uint64_t _4kbpgscount, page_state_t TYPE)
{
    uint64_t base_idx=phybase>>12;
    if(base_idx>=mem_map_entry_count)return;
    uint64_t end_idx=base_idx+_4kbpgscount;
    end_idx=end_idx>mem_map_entry_count?mem_map_entry_count:end_idx;
    for(uint64_t i=base_idx;i<end_idx;i++){
        mem_map[i].head.type=static_cast<uint64_t>(TYPE);
        mem_map[i].refcount=1;
    }

}

