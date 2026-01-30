#include "../include/memory/FreePagesAllocator.h"
#include "../include/util/kout.h"
#include <vector>
#include <random>
#include "util/OS_utils.h"
#include "cmath"
struct mem_seg
{
    uint64_t start_addr;//为1代表无效地址，其它4k对齐的地址都是有效地址
    uint64_t size;
};

int main()
{
    kio::bsp_kout.Init();  
    kio::bsp_kout.shift_dec();
    kio::bsp_kout << "=== Starting FreePagesAllocator BCB Test ===" << kio::kendl;
     
    // 创建第一个BCB实例，用于管理从0x100000000开始的20阶内存区域（约1GB）
    FreePagesAllocator::first_BCB = new FreePagesAllocator::free_pages_in_seg_control_block(
        0x100000000, 20,true
    );

    // 验证BCB基本属性
    kio::bsp_kout << "Testing basic info..." << kio::kendl;
    kio::bsp_kout << "Max support order: " << (uint32_t)FreePagesAllocator::first_BCB->get_max_order() << kio::kendl;
    
    // 初始化第二阶段
    kio::bsp_kout << "Initializing second stage..." << kio::kendl;
    KURD_t init_result = FreePagesAllocator::first_BCB->second_stage_init();
    if (init_result.result == result_code::SUCCESS) {
        kio::bsp_kout << "Second stage initialization successful!" << kio::kendl;
    } else {
        kio::bsp_kout << "Second stage initialization failed!" << kio::kendl;
        return -1;
    }

    // 打印基本信息
    FreePagesAllocator::first_BCB->print_basic_info();

    // 生成随机mem_seg向量
    std::vector<mem_seg> allocations;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::lognormal_distribution<double> size_dist(log(1<<14), 1.0); 

    const int num_allocations = 10000; // 分配100次
    kio::bsp_kout << "Generating " << num_allocations << " random allocations..." << kio::kendl;

    for (int i = 0; i < num_allocations; ++i) {
        mem_seg seg;
        seg.start_addr = 1; // 按照注释初始化为无效地址
        // 使用对数正态分布生成大小，并转换为uint64_t类型，限制最大值为4MB
        uint64_t size = static_cast<uint64_t>(size_dist(gen)) % 0x100000000 + 1;
        seg.size = size; // 使用对数正态分布生成的size
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
            phyaddr_t addr = FreePagesAllocator::first_BCB->allocate_buddy_way(seg.size, alloc_result);
            
            if (alloc_result.result == result_code::SUCCESS && addr != 0) {
                seg.start_addr = addr; // 更新为实际分配的地址
            } else {
                kio::bsp_kout << "Allocation failed at cycle " << cycle 
                             << ", size=" << seg.size << kio::kendl;
            }
        } else {
            // 地址有效，进行释放
            KURD_t free_result = FreePagesAllocator::first_BCB->free_buddy_way(
                seg.start_addr, 
                seg.size
            );
            
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
            kio::bsp_kout << "Elapsed time: " << (now_tsc-old_stamp)/100000 << " tsc" << kio::kendl;
            old_stamp=now_tsc;
            kio::bsp_kout << "Timestamp: " <<kio::now<< kio::kendl;
        }
    }
    FreePagesAllocator::first_BCB->print_basic_info();
    kio::bsp_kout << "=== Random Allocation/Deallocation Test Completed ===" << kio::kendl;

    return 0;
}