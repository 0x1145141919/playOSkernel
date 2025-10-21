#pragma once
#include <stdint.h>

namespace devices_ids {
    typedef uint64_t device_id_t;
    constexpr device_id_t DEVICE_ID_UNKNOWN = ~0;
    constexpr device_id_t IO_APIC_DEVICE_ID = 0x0;
    // ...
}
