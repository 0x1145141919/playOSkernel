#include "kpoolmemmgr.h"
#include "stdlib.h"
#include  <cstdio>
int main()
{ 
    printf("kpoolmemmgr test start\n");

    // 初始状态打印
    gKpoolmemmgr.print_hcb_status(gKpoolmemmgr.getFirst_static_heap());
    gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());

    // 测试1: 分配一个节点并释放
    printf("\n=== Test 1: Allocate and free a single node ===\n");
    HCB_chainlist_node* node = (HCB_chainlist_node*)gKpoolmemmgr.kalloc(sizeof(HCB_chainlist_node));
    if (node) {
        printf("Allocated node at %p\n", node);
        gKpoolmemmgr.print_hcb_status(gKpoolmemmgr.getFirst_static_heap());
        gKpoolmemmgr.kfree(node);
        printf("Freed node\n");
        gKpoolmemmgr.print_hcb_status(gKpoolmemmgr.getFirst_static_heap());
    }

    // 测试2: 测试不同对齐要求
    printf("\n=== Test 2: Different alignment requirements ===\n");
    void* ptr1 = gKpoolmemmgr.kalloc(65, false, 0); // 1-byte alignment
    gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
    void* ptr2 = gKpoolmemmgr.kalloc(64, false, 1); // 2-byte alignment
    gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
        
    void* ptr3 = gKpoolmemmgr.kalloc(64, false, 2); // 4-byte alignment
    gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());

    void* ptr4 = gKpoolmemmgr.kalloc(64, false, 3); // 8-byte alignment (default)
    gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
    void* ptr5 = gKpoolmemmgr.kalloc(64, false, 4); // 16-byte alignment
    gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
    printf("Allocated with different alignments:\n");
    printf("ptr1 (1-byte): %p\n", ptr1);
    printf("ptr2 (2-byte): %p\n", ptr2);
    printf("ptr3 (4-byte): %p\n", ptr3);
    printf("ptr4 (8-byte): %p\n", ptr4);
    printf("ptr5 (16-byte): %p\n", ptr5);

    // 验证对齐
    if (ptr1 && (uint64_t(ptr1) % 1 != 0)) printf("ERROR: ptr1 not 1-byte aligned!\n");
    if (ptr2 && (uint64_t(ptr2) % 2 != 0)) printf("ERROR: ptr2 not 2-byte aligned!\n");
    if (ptr3 && (uint64_t(ptr3) % 4 != 0)) printf("ERROR: ptr3 not 4-byte aligned!\n");
    if (ptr4 && (uint64_t(ptr4) % 8 != 0)) printf("ERROR: ptr4 not 8-byte aligned!\n");
    if (ptr5 && (uint64_t(ptr5) % 16 != 0)) printf("ERROR: ptr5 not 16-byte aligned!\n");

    gKpoolmemmgr.print_hcb_status(gKpoolmemmgr.getFirst_static_heap());

    // 释放部分指针
    if (ptr1) gKpoolmemmgr.kfree(ptr1);
    if (ptr3) gKpoolmemmgr.kfree(ptr3);
    if (ptr5) gKpoolmemmgr.kfree(ptr5);
    printf("Freed ptr1, ptr3, ptr5\n");
    gKpoolmemmgr.print_hcb_status(gKpoolmemmgr.getFirst_static_heap());
    gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
    // 测试3: 多次分配和释放
    printf("\n=== Test 3: Multiple allocations and frees ===\n");
    const int num_allocs = 5;
    void* allocs[num_allocs];
    for (int i = 0; i < num_allocs; i++) {
        allocs[i] = gKpoolmemmgr.kalloc(128, false, 3); // 8-byte alignment
        printf("Allocated %d at %p\n", i, allocs[i]);
    }

    gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
    // 释放一些
    for (int i = 0; i < num_allocs; i += 2) {
        if (allocs[i]) {
            gKpoolmemmgr.kfree(allocs[i]);
            printf("Freed %d\n", i);
        }
    }

    gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
    // 再分配一些
    void* extra_alloc = gKpoolmemmgr.kalloc(256, false, 3);
    printf("Allocated extra at %p\n", extra_alloc);
    gKpoolmemmgr.print_hcb_status(gKpoolmemmgr.getFirst_static_heap());

    // 释放剩下的
    for (int i = 1; i < num_allocs; i += 2) {
        if (allocs[i]) {
            gKpoolmemmgr.kfree(allocs[i]);
            printf("Freed %d\n", i);
        }
    }
    if (extra_alloc) gKpoolmemmgr.kfree(extra_alloc);
    printf("Freed extra and remaining allocations\n");
    gKpoolmemmgr.print_hcb_status(gKpoolmemmgr.getFirst_static_heap());

    // 测试4: 分配大块内存（接近堆大小）
    printf("\n=== Test 4: Large allocation ===\n");
    // 假设初始堆大小是4MB（0x400000字节）
    size_t large_size = 0x3F0000; // 接近4MB，但留有余地
    gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
    void* large_alloc = gKpoolmemmgr.kalloc(large_size, false, 3);
    if (large_alloc) {
        printf("Large allocation successful at %p\n", large_alloc);
        gKpoolmemmgr.print_hcb_status(gKpoolmemmgr.getFirst_static_heap());
        gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
        gKpoolmemmgr.kfree(large_alloc);
        printf("Freed large allocation\n");
        gKpoolmemmgr.print_hcb_status(gKpoolmemmgr.getFirst_static_heap());
        gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());
    } else {
        printf("Large allocation failed (expected if no expansion)\n");
    }

    // 测试5: 分配零字节（边界情况）
    printf("\n=== Test 5: Zero-size allocation ===\n");
    void* zero_alloc = gKpoolmemmgr.kalloc(0, false, 3);
    printf("Zero-size allocation returned %p\n", zero_alloc);
    if (zero_alloc) {
        gKpoolmemmgr.kfree(zero_alloc); // 应该没问题，但可能实现返回nullptr
    }

    // 最终状态
    printf("\n=== Final status ===\n");
    gKpoolmemmgr.print_hcb_status(gKpoolmemmgr.getFirst_static_heap());
    gKpoolmemmgr.print_meta_table(gKpoolmemmgr.getFirst_static_heap());

    printf("kpoolmemmgr test end\n");
    return 0;
}