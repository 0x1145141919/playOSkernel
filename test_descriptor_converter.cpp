#include "src/include/util/OS_utils.h"
#include "src/include/memory/Memory.h"
#include <cstdio>

int main() {
    printf("Converting memory descriptor table from logs/log_selftest.txt\n");
    print_memory_descriptor_for_gbasememmgr("logs/log_selftest.txt");
    return 0;
}
