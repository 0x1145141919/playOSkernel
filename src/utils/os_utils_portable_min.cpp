#include "util/OS_utils.h"

void ksetmem_8(void* ptr, uint8_t value, uint64_t size_in_byte)
{
    uint8_t* p = static_cast<uint8_t*>(ptr);
    for (uint64_t i = 0; i < size_in_byte; ++i) {
        p[i] = value;
    }
}
