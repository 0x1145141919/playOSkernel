#include "kintrin.h"

 int  __builtin_ctzll(long long unsigned x)
{
    uint64_t result;
    asm volatile(
        "tzcnt %1, %0"
        : "=r"(result)
        : "r"(x)
    );
    return result;
}