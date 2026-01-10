#include "util/cpuid_intel.h"
inline void cpuid(uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx){
    asm volatile(
        "cpuid"
        : "=a"(*eax),
          "=b"(*ebx),
          "=c"(*ecx),
          "=d"(*edx)
        : "a"(*eax),
          "b"(*ebx),
          "c"(*ecx),
          "d"(*edx)
    );
}
uint8_t query_apicid(){
    uint32_t eax=1, ebx, ecx, edx;
    cpuid(&eax, &ebx, &ecx, &edx);
    return (ebx >> 24) & 0xFF;
}
uint32_t query_x2apicid(){
    uint32_t eax=0xB, ebx, ecx=1, edx;
    cpuid(&eax, &ebx, &ecx, &edx);
    return edx;
}
bool is_x2apic_supported(){
    uint32_t eax=1, ebx, ecx=0, edx;
    cpuid(&eax, &ebx, &ecx, &edx);
    return ecx & (1 << 21);
}