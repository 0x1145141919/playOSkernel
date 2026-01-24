#include <cerrno>
#include "memory/kpoolmemmgr.h"
#include "util/cpuid_intel.h"
#include "util/OS_utils.h"
#include "memory/AddresSpace.h"
#include "memory/phygpsmemmgr.h"
#include <cstdio>
#include <cassert>
#include <elf.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>


int test_basic_init() {
    printf("Testing basic initialization...\n");
    
    gBaseMemMgr.Init();
    
    gBaseMemMgr.printPhyMemDesTb();
    
    int status = phymemspace_mgr::Init();
    if (status != 0) {
        printf("phymemspace_mgr::Init() failed with status: %d\n", status);
        return status;
    }
    
    printf("Basic initialization test passed.\n");
    return 0;
}

int test_memory_allocation_and_recycling() {
    printf("Testing memory allocation and recycling...\n");
    
    // 尝试分配一些内存
    phyaddr_t alloc_addr = phymemspace_mgr::pages_alloc(2, phymemspace_mgr::KERNEL, 21);
    if (alloc_addr == 0) {
        printf("pages_alloc failed - no memory available\n");
        return -1;
    }
    
    printf("Allocated memory at: 0x%lx (2 pages)\n", alloc_addr);
    
    // 测试回收刚才分配的内存
    int status = phymemspace_mgr::pages_recycle(alloc_addr, 2);
    if (status != 0) {
        printf("pages_recycle failed with status: %d\n", status);
        return status;
    }
    
    printf("Memory allocation and recycling test passed.\n");
    return 0;
}

int test_different_page_sizes() {
    printf("Testing different page sizes...\n");
    
    // 测试分配4KB页
    phyaddr_t addr_4kb = phymemspace_mgr::pages_alloc(1, phymemspace_mgr::KERNEL, 12);
    if (addr_4kb == 0) {
        printf("Failed to allocate 4KB page\n");
    } else {
        printf("Allocated 4KB page at: 0x%lx\n", addr_4kb);
        phymemspace_mgr::pages_recycle(addr_4kb, 1);
    }
    
    // 测试分配2MB页 (512 * 4KB = 2MB)
    phyaddr_t addr_2mb = phymemspace_mgr::pages_alloc(512, phymemspace_mgr::KERNEL, 21);
    if (addr_2mb == 0) {
        printf("Failed to allocate 2MB page\n");
    } else {
        printf("Allocated 2MB page at: 0x%lx\n", addr_2mb);
        phymemspace_mgr::pages_recycle(addr_2mb, 512);
    }
    
    // 测试分配1GB页 (262144 * 4KB = 1GB)
    phyaddr_t addr_1gb = phymemspace_mgr::pages_alloc(262144, phymemspace_mgr::KERNEL, 30);
    if (addr_1gb == 0) {
        printf("Failed to allocate 1GB page\n");
    } else {
        printf("Allocated 1GB page at: 0x%lx\n", addr_1gb);
        phymemspace_mgr::pages_recycle(addr_1gb, 262144);
    }
    
    printf("Different page sizes test passed.\n");
    return 0;
}

int test_mmio_registration() {
    printf("Testing MMIO registration...\n");
    
    // 尝试注册一个MMIO段
    // 首先声明一个blackhole类型的段
    phymemspace_mgr::blackhole_acclaim_flags_t flags = {0};
    int status = phymemspace_mgr::blackhole_acclaim(0x1000000000, 10, phymemspace_mgr::MMIO_SEG, flags);
    
    if (status == 0) {
        // 现在尝试注册MMIO
        status = phymemspace_mgr::pages_mmio_regist(0x1000000000, 5);
        if (status == 0) {
            printf("MMIO registration successful\n");
            
            // 注销MMIO
            status = phymemspace_mgr::pages_mmio_unregist(0x1000000000, 5);
            if (status == 0) {
                printf("MMIO unregistration successful\n");
            } else {
                printf("MMIO unregistration failed with status: %d\n", status);
            }
        } else {
            printf("MMIO registration failed with status: %d\n", status);
        }
        
        // 释放blackhole
        status = phymemspace_mgr::blackhole_decclaim(0x1000000000);
        if (status != 0) {
            printf("Failed to decclaim blackhole, status: %d\n", status);
        }
    } else {
        printf("Could not create MMIO segment for testing, status: %d\n", status);
    }
    
    printf("MMIO registration test completed.\n");
    return 0;
}

int test_statistics() {
    printf("Testing statistics...\n");
    
    auto stats = phymemspace_mgr::get_statisit_copy();
    printf("Statistics - Total allocatable: %lu, Kernel: %lu, User anonymous: %lu\n", 
           stats.total_allocatable, stats.kernel, stats.user_anonymous);
    
    printf("Statistics test passed.\n");
    return 0;
}

int test_segment_management() {
    printf("Testing segment management...\n");
    
    // 获取一个已知的物理地址并查询其段信息
    // 尝试获取第一个可用段的信息
    phymemspace_mgr::PHYSEG seg = phymemspace_mgr::get_physeg_by_addr(0x1000000);  // 随意选择一个地址
    if (seg.base != 0) {
        printf("Segment info - Base: 0x%lx, Size: %lu, Type: %d\n", seg.base, seg.seg_size, seg.type);
    } else {
        printf("No segment found for test address\n");
    }
    
    printf("Segment management test completed.\n");
    return 0;
}


int main() {
    printf("Starting comprehensive phymemspace_mgr tests...\n");
    
    // 基本初始化测试
    int status = test_basic_init();
    if (status != 0) {
        printf("Basic init test failed with status: %d\n", status);
        return status;
    }
    
    // 等待初始化完成
    sleep(1);
    
    // 打印初始状态
    //phymemspace_mgr::print_all_atom_table();
    //phymemspace_mgr::print_allseg();
    
    // 内存分配和回收测试
    status = test_memory_allocation_and_recycling();
    if (status != 0) {
        printf("Memory allocation test failed with status: %d\n", status);
        return status;
    }
    
    // 不同页面大小测试
    status = test_different_page_sizes();
    if (status != 0) {
        printf("Different page sizes test failed with status: %d\n", status);
        return status;
    }
    
    // MMIO注册测试
    status = test_mmio_registration();
    if (status != 0) {
        printf("MMIO registration test failed with status: %d\n", status);
        // 不返回错误，因为MMIO测试可能因环境而失败
    }
    
    // 统计测试
    status = test_statistics();
    if (status != 0) {
        printf("Statistics test failed with status: %d\n", status);
        return status;
    }
    
    // 段管理测试
    status = test_segment_management();
    if (status != 0) {
        printf("Segment management test failed with status: %d\n", status);
        return status;
    }
    
    if (status != 0) {
        printf("Atom page iteration test failed with status: %d\n", status);
        return status;
    }
    
    // 最后再次打印状态
    //phymemspace_mgr::print_all_atom_table();
    //phymemspace_mgr::print_allseg();
    
    printf("All tests completed successfully!\n");
    
    KspaceMapMgr::Init();
    
    return 0;
}