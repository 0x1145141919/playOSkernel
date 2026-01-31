#include "memory/Memory.h"
#include "util/OS_utils.h"
#include "os_error_definitions.h"
#include "util/kout.h"
#include "panic.h"
#include "memory/kpoolmemmgr.h"
#include "util/cpuid_intel.h"
#include "util/kptrace.h"
#ifdef USER_MODE
#include "stdlib.h"
#include "kpoolmemmgr.h"
#endif  

// 定义类中的静态成员变量
bool kpoolmemmgr_t::is_able_to_alloc_new_hcb = false;
kpoolmemmgr_t::HCB_v2 kpoolmemmgr_t::first_linekd_heap;

KURD_t kpoolmemmgr_t::default_kurd()
{
    return KURD_t(0,0,module_code::MEMORY,MEMMODULE_LOCAIONS::LOCATION_CODE_KPOOLMEMMGR,0,0,err_domain::CORE_MODULE);
}
KURD_t kpoolmemmgr_t::default_success()
{
    KURD_t kurd=default_kurd();
    kurd.result = result_code::SUCCESS;
    kurd.level = level_code::INFO;
    return  kurd;
}
KURD_t kpoolmemmgr_t::default_fail()
{
    KURD_t kurd=default_kurd();
    kurd=set_result_fail_and_error_level(kurd);
    return  kurd;
}
KURD_t kpoolmemmgr_t::default_fatal()
{
    KURD_t kurd=default_kurd();
    kurd=set_fatal_result_level(kurd);
    return  kurd;
}


kpoolmemmgr_t::HCB_v2 *kpoolmemmgr_t::find_hcb_by_address(void *ptr)
{
    auto* heap_complex = get_current_heap_complex();
    if (!heap_complex) return nullptr;
    
    for (uint16_t i = 0; i < kpoolmemmgr_t::PER_CPU_HEAP_MAX_HCB_COUNT; ++i) {
        auto* hcb = heap_complex->hcb_array[i];
        if (hcb && hcb->is_addr_belong_to_this_hcb(ptr)) {
            return hcb;
        }
    }
    return nullptr;
}

void kpoolmemmgr_t::enable_new_hcb_alloc()
{
    is_able_to_alloc_new_hcb = true;
}

// 辅助函数：处理堆对象被销毁的错误
static void handle_heap_obj_destroyed(const char* operation, void* ptr) {
    kio::bsp_kout << kio::now << "kpoolmemmgr_t::" << operation 
                  << ": heap object destroyed at address 0x" << (void*)ptr << kio::kendl;
    self_trace();
    Panic::panic("Heap object has been destroyed");
}

// 辅助函数：获取当前处理器的堆复合结构
kpoolmemmgr_t::GS_per_cpu_heap_complex_t* kpoolmemmgr_t::get_current_heap_complex() {
    return reinterpret_cast<kpoolmemmgr_t::GS_per_cpu_heap_complex_t*>(
        read_gs_u64(PER_CPU_HEAP_COMPLEX_GS_INDEX));
}

void* kpoolmemmgr_t::kalloc(uint64_t size,KURD_t&no_succes_report,alloc_flags_t flags)
{
    KURD_t success=default_success();
    KURD_t fail=default_fail();
    KURD_t fatal=default_fatal();
    success.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_EVENTS::EVENT_CODE_ALLOC;
    fail.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_EVENTS::EVENT_CODE_ALLOC;
    fatal.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_EVENTS::EVENT_CODE_ALLOC;
    // 参数验证
    if (size == 0) return nullptr;
    
    void* ptr = nullptr;
    if((!flags.force_first_linekd_heap)&&is_able_to_alloc_new_hcb) {
        // 尝试在当前处理器的堆中分配
        auto* heap_complex = get_current_heap_complex();
        if (!heap_complex) {
            KURD_t result_container = self_heap_init();
            if (result_container.result != result_code::SUCCESS) {
                kio::bsp_kout << kio::now 
                    << "kpoolmemmgr_t::kalloc: self_heap_init failed" << kio::kendl;
                no_succes_report = result_container;
                    return nullptr;
            }
            heap_complex = get_current_heap_complex();
        }
        
        // 遍历HCB数组尝试分配
        for (uint16_t i = 0; i < PER_CPU_HEAP_MAX_HCB_COUNT; ++i) {
            if (!heap_complex->hcb_array[i]) {
                // 初始化新的HCB
                alloc_flags_t meta_flags = flags;
                meta_flags.force_first_linekd_heap = true;
                heap_complex->hcb_array[i] = new(meta_flags) HCB_v2(query_x2apicid());
                if (heap_complex->hcb_array[i]->second_stage_Init().result != result_code::SUCCESS) {
                    delete heap_complex->hcb_array[i];
                    heap_complex->hcb_array[i] = nullptr;
                    continue;
                }
            }
            
            auto* hcb = heap_complex->hcb_array[i];
            if (!hcb->is_full()) {
                if (hcb->in_heap_alloc(ptr, size, flags).result == result_code::SUCCESS) {
                    return ptr;
                }
            }
        }
        
        // 所有每CPU堆都失败，回退到第一个链接堆
        kio::bsp_kout << kio::now << "kpoolmemmgr_t::kalloc: fallback to first_linekd_heap on processor " 
                      << query_x2apicid() << kio::kendl;
    }
    
    // 使用第一个链接堆分配
    if (first_linekd_heap.in_heap_alloc(ptr, size, flags).result == result_code::SUCCESS) {
        return ptr;
    }
    
    fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_EVENTS::ALLOC_RESULTS::FAIL_RESONS::REASON_CODE_NO_AVALIABLE_MEM;
    no_succes_report = fail;
    return nullptr;
}

void* kpoolmemmgr_t::realloc(void* ptr,KURD_t&no_succes_report, uint64_t size, alloc_flags_t flags)
{
    KURD_t success=default_success();
    KURD_t fail=default_fail();
    KURD_t fatal=default_fatal();
    success.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_EVENTS::EVENT_CODE_REALLOC;
    fail.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_EVENTS::EVENT_CODE_REALLOC;
    fatal.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_EVENTS::EVENT_CODE_REALLOC;
    if (!ptr) return kalloc(size,no_succes_report, flags);
    if (size == 0) {
        kfree(ptr);
        fail.reason=MEMMODULE_LOCAIONS::KPOOLMEMMGR_EVENTS::REALLOC_RESULTS::FAIL_RESONS::REASON_CODE_DEMAND_SIZE_IS_ZERO;
        no_succes_report = fail;
        return nullptr;
    }
    
    KURD_t subhcb_results_contianer = KURD_t();
    HCB_v2* located_private_heap=find_hcb_by_address(ptr);
    if(!located_private_heap)located_private_heap =(first_linekd_heap.is_addr_belong_to_this_hcb(ptr)?&first_linekd_heap:nullptr);
    if(!located_private_heap){
        return nullptr;
    }
    if(!flags.is_when_realloc_force_new_addr)subhcb_results_contianer=located_private_heap->in_heap_realloc(ptr, size, flags);
    if (subhcb_results_contianer.result==result_code::FATAL&&subhcb_results_contianer.level==level_code::FATAL&&
        subhcb_results_contianer.in_module_location==MEMMODULE_LOCAIONS::LOCATION_CODE_KPOOLMEMMGR_HCB&&
        subhcb_results_contianer.event_code==MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_INHEAP_REALLOC&&
        subhcb_results_contianer.reason==MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::INHEAP_REALLOC_RESULTS::FATAL_REASONS::REASON_CODE_METADATA_DESTROYED) {
        handle_heap_obj_destroyed("realloc", ptr);
    }   
    if(subhcb_results_contianer.result == result_code::SUCCESS){
        no_succes_report=subhcb_results_contianer;
        return ptr;
    }
    if (is_able_to_alloc_new_hcb) {
        // 尝试分配新内存并复制数据
        KURD_t err_container = KURD_t();
        void* new_ptr = kalloc(size,err_container, flags);
        if (new_ptr) {
            // 获取旧数据大小
            auto* old_meta = reinterpret_cast<HCB_v2::data_meta*>(
                reinterpret_cast<uint64_t>(ptr) - sizeof(HCB_v2::data_meta));
            ksystemramcpy(ptr, new_ptr, old_meta->data_size);
            kfree(ptr);
            return new_ptr;
        }
        return nullptr;
    } else {
        subhcb_results_contianer = first_linekd_heap.in_heap_realloc(ptr, size, flags);
        if (subhcb_results_contianer.result==result_code::FATAL&&subhcb_results_contianer.level==level_code::FATAL&&
            subhcb_results_contianer.in_module_location==MEMMODULE_LOCAIONS::LOCATION_CODE_KPOOLMEMMGR_HCB&&
        subhcb_results_contianer.event_code==MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_INHEAP_REALLOC&&
            subhcb_results_contianer.reason==MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::INHEAP_REALLOC_RESULTS::FATAL_REASONS::REASON_CODE_METADATA_DESTROYED) {
            handle_heap_obj_destroyed("realloc", ptr);
        }
    }
    
    return (subhcb_results_contianer.result == result_code::SUCCESS) ? ptr : nullptr;
}

void kpoolmemmgr_t::clear(void* ptr)
{
    if (!ptr) return;
    
    KURD_t subhcb_results_contianer = KURD_t();
    
    // 优先在每CPU堆中查找
    if (is_able_to_alloc_new_hcb) {
        auto* hcb = find_hcb_by_address(ptr);
        if (hcb) {
            subhcb_results_contianer = hcb->clear(ptr);
            if (subhcb_results_contianer.result==result_code::FATAL&&subhcb_results_contianer.level==level_code::FATAL&&
        subhcb_results_contianer.in_module_location==MEMMODULE_LOCAIONS::LOCATION_CODE_KPOOLMEMMGR_HCB&&
        subhcb_results_contianer.event_code==MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_INHEAP_REALLOC&&
        subhcb_results_contianer.reason==MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::INHEAP_REALLOC_RESULTS::FATAL_REASONS::REASON_CODE_METADATA_DESTROYED) {
                handle_heap_obj_destroyed("clear", ptr);
            }
            return;
        }
    }
    
    // 回退到第一个链接堆
    subhcb_results_contianer = first_linekd_heap.clear(ptr);
    if (subhcb_results_contianer.result==result_code::FATAL&&subhcb_results_contianer.level==level_code::FATAL&&
        subhcb_results_contianer.in_module_location==MEMMODULE_LOCAIONS::LOCATION_CODE_KPOOLMEMMGR_HCB&&
        subhcb_results_contianer.event_code==MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_INHEAP_REALLOC&&
        subhcb_results_contianer.reason==MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::INHEAP_REALLOC_RESULTS::FATAL_REASONS::REASON_CODE_METADATA_DESTROYED) {
        handle_heap_obj_destroyed("clear", ptr);
    }
}

int kpoolmemmgr_t::Init()
{
    is_able_to_alloc_new_hcb = false;
    return first_linekd_heap.first_linekd_heap_Init();
}

KURD_t kpoolmemmgr_t::self_heap_init()
{
    KURD_t success=default_success();
    KURD_t fail=default_fail();
    KURD_t fatal=default_fatal();
    success.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_EVENTS::EVENT_CODE_PER_PROCESSOR_HEAP_INIT;
    fail.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_EVENTS::EVENT_CODE_PER_PROCESSOR_HEAP_INIT;
    fatal.event_code=MEMMODULE_LOCAIONS::KPOOLMEMMGR_EVENTS::EVENT_CODE_PER_PROCESSOR_HEAP_INIT;
    auto* heap_complex = get_current_heap_complex();
    if (heap_complex) {
        return success; // 已经初始化
    }
    alloc_flags_t meta_flags = default_panic_behaviors_flags;
    meta_flags.force_first_linekd_heap = true;
    heap_complex = new(meta_flags) GS_per_cpu_heap_complex_t;
    heap_complex->hcb_array[0]=new(meta_flags) HCB_v2(query_x2apicid());
    KURD_t hcb_init_result_contain=heap_complex->hcb_array[0]->second_stage_Init();
    if (hcb_init_result_contain.result != result_code::SUCCESS) {
        delete heap_complex;
        return hcb_init_result_contain;
    }
    setmem(heap_complex, sizeof(GS_per_cpu_heap_complex_t), 0);
    gs_u64_write(PER_CPU_HEAP_COMPLEX_GS_INDEX, reinterpret_cast<uint64_t>(heap_complex));
    return success;
}

void kpoolmemmgr_t::kfree(void* ptr)
{
    if (!ptr) return;
    
    KURD_t subhcb_results_contianer = KURD_t();
    
    if (is_able_to_alloc_new_hcb) {
        auto* hcb = find_hcb_by_address(ptr);
        if (hcb) {
            subhcb_results_contianer = hcb->free(ptr);
            if (subhcb_results_contianer.result==result_code::FATAL&&subhcb_results_contianer.level==level_code::FATAL&&
        subhcb_results_contianer.in_module_location==MEMMODULE_LOCAIONS::LOCATION_CODE_KPOOLMEMMGR_HCB&&
        subhcb_results_contianer.event_code==MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_FREE&&
        subhcb_results_contianer.reason==MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::INHEAP_REALLOC_RESULTS::FATAL_REASONS::REASON_CODE_METADATA_DESTROYED) {
                handle_heap_obj_destroyed("kfree", ptr);
            }
            
            // 如果HCB为空，则销毁它
            if (hcb->get_used_bytes_count() == 0) {
                auto* heap_complex = get_current_heap_complex();
                if (heap_complex) {
                    for (uint16_t i = 0; i < PER_CPU_HEAP_MAX_HCB_COUNT; ++i) {
                        if (heap_complex->hcb_array[i] == hcb) {
                            hcb->~HCB_v2();
                            first_linekd_heap.free(hcb);
                            heap_complex->hcb_array[i] = nullptr;
                            break;
                        }
                    }
                }
            }
            return;
        }
    }
    
    // 回退到第一个链接堆
    subhcb_results_contianer = first_linekd_heap.free(ptr);
    if (subhcb_results_contianer.result==result_code::FATAL&&subhcb_results_contianer.level==level_code::FATAL&&
        subhcb_results_contianer.in_module_location==MEMMODULE_LOCAIONS::LOCATION_CODE_KPOOLMEMMGR_HCB&&
        subhcb_results_contianer.event_code==MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::EVENT_CODE_INHEAP_REALLOC&&
        subhcb_results_contianer.reason==MEMMODULE_LOCAIONS::KPOOLMEMMGR_HCB_EVENTS::INHEAP_REALLOC_RESULTS::FATAL_REASONS::REASON_CODE_METADATA_DESTROYED) {
        handle_heap_obj_destroyed("kfree", ptr);
    }
}

// 地址转换辅助函数
namespace {
    constexpr uint64_t get_min_kvaddr() {
        #ifdef PGLV_5
        return 0xff00000000000000;
        #elif defined(PGLV_4)
        return 0xffff800000000000;
        #else
        return 0;
        #endif
    }
    
    constexpr uint64_t get_max_phyaddr() {
        #ifdef PGLV_5
        return 1ULL << 56;
        #elif defined(PGLV_4)
        return 1ULL << 47;
        #else
        return 0;
        #endif
    }
}

phyaddr_t kpoolmemmgr_t::get_phy(vaddr_t addr)
{
    constexpr uint64_t MIN_KVADDR = get_min_kvaddr();
    constexpr uint64_t MAX_PHYADDR = get_max_phyaddr();
    
    if (addr < MIN_KVADDR && addr >= MAX_PHYADDR) {
        return 0;
    }
    
    if (is_able_to_alloc_new_hcb) {
        auto* hcb = find_hcb_by_address(reinterpret_cast<void*>(addr));
        if (hcb) {
            return hcb->tran_to_phy(reinterpret_cast<void*>(addr));
        }
    }
    
    return first_linekd_heap.tran_to_phy(reinterpret_cast<void*>(addr));
}

vaddr_t kpoolmemmgr_t::get_virt(phyaddr_t addr)
{
    constexpr uint64_t MIN_KVADDR = get_min_kvaddr();
    constexpr uint64_t MAX_PHYADDR = get_max_phyaddr();
    
    if (addr < MIN_KVADDR || addr >= MAX_PHYADDR) {
        return 0;
    }
    
    if (is_able_to_alloc_new_hcb) {
        auto* hcb = find_hcb_by_address(reinterpret_cast<void*>(addr));
        if (hcb) {
            return hcb->tran_to_virt(addr);
        }
    }
    
    return first_linekd_heap.tran_to_virt(addr);
}

kpoolmemmgr_t::kpoolmemmgr_t() = default;
kpoolmemmgr_t::~kpoolmemmgr_t() = default;