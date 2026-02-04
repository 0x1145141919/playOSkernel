#include "memory/kpoolmemmgr.h"
#include "util/cpuid_intel.h"
#include "util/OS_utils.h"
#include "memory/AddresSpace.h"
#include "memory/phygpsmemmgr.h"
#include "memory/FreePagesAllocator.h"
#include "util/kout.h"
#include <unistd.h>
#include <vector>
#include <random>
struct mem_seg
{
    uint64_t start_addr;//为1代表无效地址，其它4k对齐的地址都是有效地址
    uint64_t size;
    uint8_t page_state_replace;//表示页类型，0:KERNEL,1:USER_ANONYMOUS,2:USER_FILE,3:DMA
};
KURD_t test_basic_init() {
    kio::bsp_kout << "Testing basic initialization..." << kio::kendl;
    
    gBaseMemMgr.Init();
    
    gBaseMemMgr.printPhyMemDesTb();
    
    KURD_t status = phymemspace_mgr::Init();
    if (!success_all_kurd(status)) {
        kio::bsp_kout << "phymemspace_mgr::Init() failed with status: ";
        kio::bsp_kout << status;
        kio::bsp_kout << kio::kendl;
        return status;
    }
    status=FreePagesAllocator::Init();
    kio::bsp_kout << "Basic initialization test passed." << kio::kendl;
    return status;
}


// 压力测试函数
KURD_t stress_test_page_allocation_and_deallocation() {
    kio::bsp_kout << "Starting stress test for page allocation and deallocation..." << kio::kendl;
    
    // 生成随机mem_seg向量
    std::vector<mem_seg> allocations;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::lognormal_distribution<double> size_dist(log(1<<14), 1.0); // 平均约16KB大小

    const int num_allocations = 10000; // 生成10000个测试项
    kio::bsp_kout << "Generating " << num_allocations << " random allocations..." << kio::kendl;

    for (int i = 0; i < num_allocations; ++i) {
        mem_seg seg;
        seg.start_addr = 1; // 按照注释初始化为无效地址
        // 使用对数正态分布生成大小，并转换为uint64_t类型，限制最大值为256MB
        uint64_t size = static_cast<uint64_t>(size_dist(gen)) % 0x10000000 + 1;
        seg.size = size; // 使用对数正态分布生成的size
        // 随机选择页面类型 (0:KERNEL,1:USER_ANONYMOUS,2:USER_FILE,3:DMA)
        seg.page_state_replace = static_cast<uint8_t>(gen() % 4);
        allocations.push_back(seg);
    }

    // 创建状态机进行100万次分配和释放操作
    std::uniform_int_distribution<int> index_dist(0, num_allocations - 1);
    kio::bsp_kout << "Starting state machine with 1,000,000 allocation/deallocation cycles..." << kio::kendl;
    uint64_t old_stamp=rdtsc();
    
    for (int cycle = 0; cycle < 1000000; ++cycle) {
        // 随机选择一个索引
        int idx = index_dist(gen);
        
        // 获取对应的mem_seg
        mem_seg& seg = allocations[idx];
        
        if (seg.start_addr == 1) {
            // 地址无效，进行分配
            KURD_t alloc_result;
            // 根据page_state_replace确定页面类型
            page_state_t page_type;
            switch(seg.page_state_replace) {
                case 0: page_type = page_state_t::KERNEL; break;
                case 1: page_type = page_state_t::USER_ANONYMOUS; break;
                case 2: page_type = page_state_t::USER_FILE; break;
                case 3: page_type = page_state_t::DMA; break;
                default: page_type = page_state_t::KERNEL; break; // 默认使用KERNEL
            }
            
            // 计算需要的页数（向上取整）
            uint64_t page_count = (seg.size + 0xFFF) >> 12; // 4KB页大小
            
            phyaddr_t addr = __wrapped_pgs_alloc(&alloc_result, page_count, page_type, 12);
            
            if (alloc_result.result == result_code::SUCCESS && addr != 0) {
                seg.start_addr = addr; // 更新为实际分配的地址
            } else {
                kio::bsp_kout << "Allocation failed at cycle " << cycle 
                             << ", size=" << seg.size << kio::kendl;
            }
        } else {
            // 地址有效，进行释放
            // 计算页数
            uint64_t page_count = (seg.size + 0xFFF) >> 12;
            
            KURD_t free_result = __wrapped_pgs_free(seg.start_addr, page_count);
            
            if (free_result.result == result_code::SUCCESS) {
                seg.start_addr = 1; // 重置为无效地址
            } else {
                kio::bsp_kout << "Deallocation failed at cycle " << cycle 
                             << ", addr=0x" << seg.start_addr << kio::kendl;
            }
        }
        
        // 每隔100000次输出一次进度和时间
        if ((cycle + 1) % 100000 == 0) {
            kio::bsp_kout << "Completed " << (cycle + 1) << " cycles" << kio::kendl;
            uint64_t now_tsc=rdtsc();
            kio::bsp_kout << "Elapsed time: " << (now_tsc-old_stamp)/100000 << " tsc avg per 100k ops" << kio::kendl;
            old_stamp=now_tsc;
            kio::bsp_kout << "Timestamp: " <<kio::now<< kio::kendl;
        }
    }
    
    kio::bsp_kout << "=== Stress Test Completed ===" << kio::kendl;

    return KURD_t();
}

int main() {
    kio::bsp_kout.Init();
    kio::bsp_kout << "Starting comprehensive phymemspace_mgr tests..." << kio::kendl;
    
    // 基本初始化测试
    KURD_t status = test_basic_init();
    if (!success_all_kurd(status)) {
        kio::bsp_kout << "Basic init test failed with status: ";
        kio::bsp_kout << status;
        kio::bsp_kout << kio::kendl;
        return -1;
    }    
    
    // 执行压力测试
    status = stress_test_page_allocation_and_deallocation();
    if (!success_all_kurd(status)) {
        kio::bsp_kout << "Stress test failed with status: ";
        kio::bsp_kout << status;
        kio::bsp_kout << kio::kendl;
        return -1;
    }
    
    // 打印初始状态
    phymemspace_mgr::print_all_atom_table();
    phymemspace_mgr::print_allseg();
    
    return 0;
}