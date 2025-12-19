#include "memory/kpoolmemmgr.h"
#include "util/cpuid_intel.h"
#include "util/OS_utils.h"
#include <cstdio>
#include <thread>
#include <vector>
#include <atomic>
#include <random>
#include <chrono>
#include <mutex>
#include <memory>
#include <algorithm>

// 测试统计信息
struct TestStats {
    std::atomic<uint64_t> alloc_count{0};
    std::atomic<uint64_t> free_count{0};
    std::atomic<uint64_t> realloc_count{0};
    std::atomic<uint64_t> error_count{0};
    std::atomic<uint64_t> total_allocated{0};
};

// 线程安全的随机数生成器
class ThreadSafeRandom {
private:
    std::mt19937_64 generator;
    std::uniform_int_distribution<size_t> size_dist;
    std::uniform_int_distribution<int> op_dist;
    std::mutex mtx;

public:
    ThreadSafeRandom() 
        : generator(std::chrono::steady_clock::now().time_since_epoch().count()),
          size_dist(16, 4096),  // 分配大小范围：16B - 4KB
          op_dist(0, 99) {      // 操作类型分布
    }

    size_t get_size() {
        std::lock_guard<std::mutex> lock(mtx);
        return size_dist(generator);
    }

    int get_operation() {
        std::lock_guard<std::mutex> lock(mtx);
        return op_dist(generator);
    }
};

// 每个线程的测试数据
struct ThreadData {
    std::vector<void*> allocated_ptrs;
    TestStats& stats;
    ThreadSafeRandom& random;
    int thread_id;
    bool should_stop{false};
    
    ThreadData(TestStats& s, ThreadSafeRandom& r, int id) 
        : stats(s), random(r), thread_id(id) {}
};

// 内存测试工作线程函数
void memory_test_worker(ThreadData& data) {
    const int max_ptrs_per_thread = 100; // 每个线程最大同时持有的指针数
    auto& stats = data.stats;
    auto& random = data.random;
    
    printf("Thread %d starting memory test...\n", data.thread_id);
    
    while (!data.should_stop) {
        int op = random.get_operation();
        
        // 操作分布：60%分配，30%释放，10%重新分配
        if (op < 60) { // 分配操作
            if (data.allocated_ptrs.size() < max_ptrs_per_thread) {
                size_t size = random.get_size();
                void* ptr = kpoolmemmgr_t::kalloc(size, false, true, 4); // 4表示16字节对齐
                
                if (ptr != nullptr) {
                    // 写入测试数据
                    setmem(ptr, size, 0xAA);
                    data.allocated_ptrs.push_back(ptr);
                    stats.alloc_count++;
                    stats.total_allocated += size;
                } else {
                    stats.error_count++;
                    printf("Thread %d: Allocation failed for size %zu\n", data.thread_id, size);
                }
            }
        } else if (op < 90) { // 释放操作
            if (!data.allocated_ptrs.empty()) {
                size_t idx = random.get_size() % data.allocated_ptrs.size();
                void* ptr = data.allocated_ptrs[idx];
                
                // 验证数据完整性（可选）
                kpoolmemmgr_t::kfree(ptr);
                data.allocated_ptrs.erase(data.allocated_ptrs.begin() + idx);
                stats.free_count++;
            }
        } else { // 重新分配操作
            if (!data.allocated_ptrs.empty()) {
                size_t idx = random.get_size() % data.allocated_ptrs.size();
                void* old_ptr = data.allocated_ptrs[idx];
                size_t new_size = random.get_size();
                
                void* new_ptr = kpoolmemmgr_t::realloc(old_ptr, new_size, true, 4);
                
                if (new_ptr != nullptr) {
                    data.allocated_ptrs[idx] = new_ptr;
                    stats.realloc_count++;
                } else {
                    stats.error_count++;
                    printf("Thread %d: Realloc failed for size %zu\n", data.thread_id, new_size);
                }
            }
        }
        
        // 偶尔清理一些内存，避免无限增长
        if ((random.get_operation() < 10) && !data.allocated_ptrs.empty()) {
            size_t cleanup_count = std::min(data.allocated_ptrs.size(), 
                                           static_cast<size_t>(random.get_size() % 10 + 1));
            
            for (size_t i = 0; i < cleanup_count && !data.allocated_ptrs.empty(); ++i) {
                size_t idx = data.allocated_ptrs.size() - 1;
                kpoolmemmgr_t::kfree(data.allocated_ptrs[idx]);
                data.allocated_ptrs.pop_back();
                stats.free_count++;
            }
        }
    }
    
    // 清理剩余的内存
    for (void* ptr : data.allocated_ptrs) {
        kpoolmemmgr_t::kfree(ptr);
        stats.free_count++;
    }
    data.allocated_ptrs.clear();
    
    printf("Thread %d finished. Final stats - Alloc: %lu, Free: %lu, Realloc: %lu, Errors: %lu\n",
           data.thread_id, stats.alloc_count.load(), stats.free_count.load(),
           stats.realloc_count.load(), stats.error_count.load());
}

// 压力测试：大量小对象分配
void stress_test_worker(TestStats& stats, int thread_id, int iterations) {
    printf("Stress test thread %d starting %d iterations...\n", thread_id, iterations);
    
    for (int i = 0; i < iterations; ++i) {
        // 分配大量小对象
        std::vector<void*> small_ptrs;
        const int batch_size = 50;
        
        for (int j = 0; j < batch_size; ++j) {
            size_t size = (i * j) % 256 + 8; // 8-263字节
            void* ptr = kpoolmemmgr_t::kalloc(size, false, true, 3); // 8字节对齐
            
            if (ptr != nullptr) {
                setmem(ptr, size, thread_id); // 用线程ID填充
                small_ptrs.push_back(ptr);
                stats.alloc_count++;
            } else {
                stats.error_count++;
            }
        }
        
        // 随机释放一部分
        std::random_device rd;
        std::mt19937 g(rd());
        std::shuffle(small_ptrs.begin(), small_ptrs.end(), g);
        
        size_t free_count = small_ptrs.size() / 2;
        for (size_t j = 0; j < free_count && !small_ptrs.empty(); ++j) {
            kpoolmemmgr_t::kfree(small_ptrs.back());
            small_ptrs.pop_back();
            stats.free_count++;
        }
        
        // 重新分配剩余的一部分
        for (size_t j = 0; j < small_ptrs.size() / 2; ++j) {
            void* old_ptr = small_ptrs[j];
            size_t new_size = ((i + j) * 17) % 512 + 16;
            void* new_ptr = kpoolmemmgr_t::realloc(old_ptr, new_size, true, 4);
            
            if (new_ptr != nullptr) {
                small_ptrs[j] = new_ptr;
                stats.realloc_count++;
            } else {
                stats.error_count++;
            }
        }
        
        // 清理剩余
        for (void* ptr : small_ptrs) {
            kpoolmemmgr_t::kfree(ptr);
            stats.free_count++;
        }
    }
    
    printf("Stress test thread %d completed\n", thread_id);
}

int main() {
    printf("kpoolmemmgr multithreaded test start\n");
    
    // 初始化内存管理器
    if (kpoolmemmgr_t::Init() != 0) {
        printf("Failed to initialize kpoolmemmgr!\n");
        return -1;
    }
    
    // 启用新的HCB分配
    kpoolmemmgr_t::enable_new_hcb_alloc();
    printf("HCB allocation enabled\n");
    
    TestStats main_stats;
    ThreadSafeRandom random;
    const int num_threads = 4;
    const int test_duration_seconds = 10;
    
    std::vector<std::thread> threads;
    std::vector<std::unique_ptr<ThreadData>> thread_data;
    
    // 创建普通测试线程
    for (int i = 0; i < num_threads; ++i) {
        auto data = std::make_unique<ThreadData>(main_stats, random, i);
        thread_data.push_back(std::move(data));
        threads.emplace_back(memory_test_worker, std::ref(*thread_data[i]));
    }
    
    // 创建压力测试线程
    std::thread stress_thread([&main_stats]() {
        stress_test_worker(main_stats, num_threads, 1000);
    });
    
    // 运行测试一段时间
    printf("Running test for %d seconds...\n", test_duration_seconds);
    std::this_thread::sleep_for(std::chrono::seconds(test_duration_seconds));
    
    // 停止普通测试线程
    for (auto& data : thread_data) {
        data->should_stop = true;
    }
    
    // 等待所有线程结束
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    if (stress_thread.joinable()) {
        stress_thread.join();
    }
    
    // 打印最终统计
    printf("\n=== Final Test Statistics ===\n");
    printf("Total allocations: %lu\n", main_stats.alloc_count.load());
    printf("Total deallocations: %lu\n", main_stats.free_count.load());
    printf("Total reallocations: %lu\n", main_stats.realloc_count.load());
    printf("Total errors: %lu\n", main_stats.error_count.load());
    printf("Total allocated bytes: %lu\n", main_stats.total_allocated.load());
    
    // 最终清理测试：分配一些大对象
    printf("\n=== Final cleanup test ===\n");
    std::vector<void*> final_ptrs;
    for (int i = 0; i < 10; ++i) {
        size_t size = 1024 * (i + 1); // 1KB to 10KB
        void* ptr = kpoolmemmgr_t::kalloc(size, true, true, 4);
        if (ptr) {
            setmem(ptr, size, 0xCC);
            final_ptrs.push_back(ptr);
            printf("Allocated large block %d: %zu bytes\n", i, size);
        }
    }
    
    // 清理大对象
    for (void* ptr : final_ptrs) {
        kpoolmemmgr_t::kfree(ptr);
    }
    printf("Large blocks freed\n");
    
    printf("kpoolmemmgr multithreaded test end\n");
    return 0;
}