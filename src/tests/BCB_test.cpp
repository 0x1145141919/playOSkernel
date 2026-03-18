
#ifdef BEHAVIOR_SAVE
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstring>
#endif
#include "memory/FreePagesAllocator.h"
#include "util/kout.h"
#include <vector>
#include <random>
#include "util/OS_utils.h"
#include "cmath"
struct mem_seg
{
    uint64_t start_addr;//为1代表无效地址，其它4k对齐的地址都是有效地址
    uint64_t size;
};

struct test_set_record
{
    uint32_t index;
    uint64_t size;
    uint64_t initial_addr;
};

enum class op_type : uint8_t
{
    ALLOCATE = 0,
    FREE = 1,
};

struct op_record
{
    uint32_t cycle;
    op_type op;
    uint32_t index;
    uint64_t request_size;
    uint64_t addr;
    uint32_t result_code;
};

static void* map_file_rw(const char* path, size_t size, int& fd_out)
{
#ifdef BEHAVIOR_SAVE
    int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return MAP_FAILED;
    }
    if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
        close(fd);
        return MAP_FAILED;
    }
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        close(fd);
        return MAP_FAILED;
    }
    fd_out = fd;
    return p;
#else
    (void)path;
    (void)size;
    (void)fd_out;
    return nullptr;
#endif
}

static inline bool is_no_available_buddy_fail(const KURD_t& k)
{
    return k.result == result_code::FAIL &&
           k.module_code == module_code::MEMORY &&
           k.in_module_location == MEMMODULE_LOCAIONS::LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK &&
           k.event_code == MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::EVENT_CODE_ALLOCATE_BUDY_WAY &&
           k.reason == MEMMODULE_LOCAIONS::FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES::ALLOCATE_RESULTS_CODE::FAIL_REASONS_CODE::FAIL_REASON_CODE_NO_AVALIABLE_BUDDY;
}

static int exit_with_cleanup(int code)
{
    if (FreePagesAllocator::first_BCB) {
        FreePagesAllocator::first_BCB->free_pages_flush();
        delete FreePagesAllocator::first_BCB;
        FreePagesAllocator::first_BCB = nullptr;
    }
    exit(code);
}

int main()
{
    bsp_kout.Init();  
    bsp_kout.shift_dec();
    bsp_kout<< "=== Starting FreePagesAllocator BCB Test ===" << kendl;
     
    // 创建第一个BCB实例，用于管理从0x100000000开始的20阶内存区域（约1GB）
    FreePagesAllocator::first_BCB = new FreePagesAllocator::BuddyControlBlock(
        0x100000000, 18
    );

    // 验证BCB基本属性
    bsp_kout<< "Testing basic info..." << kendl;
    bsp_kout<< "Max support order: " << (uint32_t)FreePagesAllocator::first_BCB->get_max_order() << kendl;
    
    // 初始化第二阶段
    bsp_kout<< "Initializing second stage..." << kendl;
    KURD_t init_result = FreePagesAllocator::first_BCB->second_stage_init();
    if (init_result.result == result_code::SUCCESS) {
        bsp_kout<< "Second stage initialization successful!" << kendl;
    } else {
        bsp_kout<< "Second stage initialization failed!" << kendl;
        return exit_with_cleanup(-1);
    }

    // 打印基本信息
    FreePagesAllocator::first_BCB->print_basic_info();

    // 生成随机mem_seg向量
    std::vector<mem_seg> allocations;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::lognormal_distribution<double> size_dist(log(1<<14), 1.0); 

    const int num_allocations = 10000; // 分配100000次
    bsp_kout<< "Generating " << num_allocations << " random allocations..." << kendl;

    // 创建文件A用于保存测试集（二进制结构体数组）
#ifdef BEHAVIOR_SAVE
    const char* test_set_path = "test_set_A.bin";
    const size_t test_set_bytes = static_cast<size_t>(num_allocations) * sizeof(test_set_record);
    int test_set_fd = -1;
    auto* test_set_map = static_cast<test_set_record*>(
        map_file_rw(test_set_path, test_set_bytes, test_set_fd)
    );
    if (test_set_map == MAP_FAILED) {
        bsp_kout<< "Failed to mmap " << test_set_path << " for writing!" << kendl;
        return exit_with_cleanup(-1);
    }
    std::memset(test_set_map, 0, test_set_bytes);
#endif

    for (int i = 0; i < num_allocations; ++i) {
        mem_seg seg;
        seg.start_addr = 1; // 按照注释初始化为无效地址
        // 使用对数正态分布生成大小，并转换为uint64_t类型，限制最大值为4MB
        uint64_t size = static_cast<uint64_t>(size_dist(gen)) % 0x100000000 + 1;
        seg.size = size; // 使用对数正态分布生成的size
        allocations.push_back(seg);
        
        // 保存到文件A（二进制）
#ifdef BEHAVIOR_SAVE
        test_set_map[i].index = static_cast<uint32_t>(i);
        test_set_map[i].size = size;
        test_set_map[i].initial_addr = seg.start_addr;
#endif
    }
#ifdef BEHAVIOR_SAVE
    msync(test_set_map, test_set_bytes, MS_SYNC);
    munmap(test_set_map, test_set_bytes);
    close(test_set_fd);
    bsp_kout<< "Test set saved to " << test_set_path << kendl;
#else
    bsp_kout<< "BEHAVIOR_SAVE not enabled: skip test set file output" << kendl;
#endif

    // 创建文件B用于记录操作日志（二进制结构体数组）
    const int num_cycles = 1000000;
#ifdef BEHAVIOR_SAVE
    const char* op_log_path = "operation_log_B.bin";
    const size_t op_log_bytes = static_cast<size_t>(num_cycles) * sizeof(op_record);
    int op_log_fd = -1;
    auto* op_log_map = static_cast<op_record*>(
        map_file_rw(op_log_path, op_log_bytes, op_log_fd)
    );
    if (op_log_map == MAP_FAILED) {
        bsp_kout<< "Failed to mmap " << op_log_path << " for writing!" << kendl;
        return exit_with_cleanup(-1);
    }
    std::memset(op_log_map, 0, op_log_bytes);
    size_t op_log_count = 0;
#else
    bsp_kout<< "BEHAVIOR_SAVE not enabled: skip operation log file output" << kendl;
#endif

    // 创建状态机进行100万次分配和释放操作
    std::uniform_int_distribution<int> index_dist(0, num_allocations - 1);
    bsp_kout<< "Starting state machine with 1,000,000 allocation/deallocation cycles..." << kendl;
    uint64_t old_stamp=rdtsc();
    int exit_code = 0;
    
    for (int cycle = 0; cycle < num_cycles; ++cycle) {
        // 随机选择一个索引
        int idx = index_dist(gen);
        
        // 获取对应的mem_seg
        mem_seg& seg = allocations[idx];
#ifdef BEHAVIOR_SAVE
        op_record& rec = op_log_map[op_log_count++];
        rec.cycle = static_cast<uint32_t>(cycle);
        rec.index = static_cast<uint32_t>(idx);
        rec.request_size = seg.size;
#endif
        
        if (seg.start_addr == 1) {
            // 地址无效，进行分配
            KURD_t alloc_result;
            phyaddr_t addr = FreePagesAllocator::first_BCB->allocate_buddy_way(seg.size, alloc_result);
            
            // 记录分配操作到文件B
#ifdef BEHAVIOR_SAVE
            rec.op = op_type::ALLOCATE;
            rec.addr = addr;
            rec.result_code = static_cast<uint32_t>(alloc_result.result);
#endif
            if (alloc_result.result == result_code::SUCCESS && addr != 0) {
                seg.start_addr = addr; // 更新为实际分配的地址
            } else if (is_no_available_buddy_fail(alloc_result)) {
                // 正常的内存不足情况，跳过
            } else {
                bsp_kout<< "Allocation failed at cycle " << cycle 
                             << ", size=" << seg.size << kendl;
                exit_code = 2;
                break;
            }
        } else {
            // 地址有效，进行对称释放
            KURD_t free_result = FreePagesAllocator::first_BCB->free_buddy_way(
                seg.start_addr, 
                seg.size
            );
            
            // 记录释放操作到文件B
#ifdef BEHAVIOR_SAVE
            rec.op = op_type::FREE;
            rec.addr = seg.start_addr;
            rec.result_code = static_cast<uint32_t>(free_result.result);
#endif
            if (free_result.result == result_code::SUCCESS) {
                seg.start_addr = 1; // 重置为无效地址
            } else {
                bsp_kout<< "Deallocation failed at cycle " << cycle 
                             << ", addr=0x" << seg.start_addr << kendl;
                exit_code = 3;
                break;
            }
        }
        
        // 每隔100000次输出一次进度和时间
        if ((cycle + 1) % 100000 == 0) {
            bsp_kout<< "Completed " << (cycle + 1) << " cycles" << kendl;
            uint64_t now_tsc=rdtsc();
            bsp_kout<< "Elapsed time: " << (now_tsc-old_stamp)/100000 << " tsc" << kendl;
            old_stamp=now_tsc;
            bsp_kout<< "Timestamp: " <<now<< kendl;
        }
    }
    
    // 关闭操作日志文件
#ifdef BEHAVIOR_SAVE
    const size_t used_log_bytes = op_log_count * sizeof(op_record);
    if (used_log_bytes > 0) {
        msync(op_log_map, used_log_bytes, MS_SYNC);
    }
    munmap(op_log_map, op_log_bytes);
    ftruncate(op_log_fd, static_cast<off_t>(used_log_bytes));
    close(op_log_fd);
    bsp_kout<< "Operation log saved to " << op_log_path << kendl;
#endif
    
    FreePagesAllocator::first_BCB->print_basic_info();
    KURD_t flush=FreePagesAllocator::first_BCB->free_pages_flush();
    if(!success_all_kurd(flush)){
        bsp_kout<< "violation detect" << kendl;
        FreePagesAllocator::first_BCB->print_basic_info();
    }
    bsp_kout<< "=== Random Allocation/Deallocation Test Completed ===" << kendl;
    return exit_with_cleanup(exit_code);
}
