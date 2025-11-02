#pragma once
#include <stdint.h>
namespace blockdevice_id
{
    constexpr uint32_t MEMDISK_V1=0;
    constexpr uint32_t MEMDISK_V2=1;
    constexpr uint32_t NVME_TOTAL_DISK=2;
    constexpr uint32_t NVME_PARITON=3;
    constexpr uint32_t USERSPACE_VIRTUAL_BLOCKDEVICE=4;
};// namespace blockdevice_id

