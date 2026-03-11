#pragma once
#include "stdint.h"
#include "memmodule_err_definitions.h"
#include "Memory.h"
#include "util/lock.h"
#include "util/Ktemplats.h"
#include "util/bitmap.h"
#include "memory/phygpsmemmgr.h"
class KspaceMapMgr;
struct fpa_stats {
    uint64_t alloc_main_hit;
    uint64_t alloc_vice_hit;
    uint64_t bcb_scan_total;
    uint64_t bcb_scan_max;
    uint64_t alloc_fail;
    uint64_t constrained_alloc;
    uint64_t constrained_retry;
    uint64_t lock_try_fail;
    uint64_t lock_spin;
    uint64_t force_first_bcb_alloc;
    };
namespace MEMMODULE_LOCAIONS{
    constexpr uint8_t LOCATION_CODE_FREEPAGES_ALLOCATOR=28;
    
    constexpr uint8_t LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK=32;
    namespace FREEPAGES_ALLOCATOR{
        constexpr uint8_t EVENT_CODE_INIT= 0;
        constexpr uint8_t EVENT_CODE_INIT_SECOND_STAGE = 1;
        constexpr uint8_t EVENT_CODE_ALLOC = 2;
        namespace INIT_SECOND_STAGE_RESULTS_CODE{
            //todo kurd语义设计以及错误码
            namespace FATAL_REASONS_CODE{
                constexpr uint16_t FAIL_NO_AVALIABLE_MEM= 1;
                constexpr uint16_t FAIL_THREAD_COUNT_ZERO= 2;
            }
            namespace FAIL_REASONS_CODE{
                constexpr uint16_t FAIL_REASON_CODE_BAD_PARAM_MATCH_THREAD_BUT_BAD_ZERO_CONEFFICIENCY= 1;
            }

        }
        namespace ALLOC_RESULTS_CODE{
            namespace FAIL_REASONS_CODE{
                constexpr uint16_t FAIL_REASON_CODE_INVALID_SIZE = 1;
                constexpr uint16_t FAIL_REASON_CODE_NUMA_NOT_SUPPORTED = 2;
                constexpr uint16_t FAIL_REASON_CODE_INVALID_CONSTRAIN = 3;
                constexpr uint16_t FAIL_REASON_CODE_NO_MATCHED_BCB = 4;
                constexpr uint16_t FAIL_REASON_CODE_NO_AVALIABLE_BCB = 5;
            }
            namespace RETRY_REASONS_CODE{
                constexpr uint16_t RETRY_REASON_CODE_TARGET_BUSY = 1;
            }
        }
    }
   namespace FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES{ 

        constexpr uint8_t EVENT_CODE_INIT= 0;
        constexpr uint8_t EVENT_CODE_ALLOCATE_BUDY_WAY = 1;
        namespace ALLOCATE_RESULTS_CODE{ 
            namespace FAIL_REASONS_CODE{
                constexpr uint16_t FAIL_REASON_CODE_ACQUIRE_SIZE_TO_BIG= 1;
                constexpr uint16_t FAIL_REASON_CODE_NO_AVALIABLE_BUDDY= 2;
            }
        }
        constexpr uint8_t EVENT_CODE_CONANICO_FREE = 2;
        namespace CONANICO_FREE_RESULTS_CODE{
            namespace FAIL_REASONS_CODE{
                constexpr uint16_t FAIL_REASON_CODE_INVALID_PAGE_INDEX= 1;
                constexpr uint16_t FAIL_REASON_CODE_INVALID_ORDER= 2;
                constexpr uint16_t FAIL_REASON_CODE_COALESCING_FAILED= 3;
                constexpr uint16_t FAIL_REASON_CODE_DOUBLE_FREE= 5;
            }
            namespace FATAL_REASONS_CODE{
                constexpr uint16_t BIN_TREE_CONSISTENCY_VIOLATION=1;
            }
        }
        constexpr uint8_t EVENT_CODE_SPLIT_PAGE = 3;
        namespace SPLIT_PAGE_RESULTS_CODE{ 
            namespace FAIL_REASONS_CODE{
                constexpr uint16_t FAIL_REASON_CODE_INVALID_ORDER= 1;
                constexpr uint16_t FAIL_REASON_CODE_PAGE_NOT_FREE= 2;
            }
        }
        constexpr uint8_t EVENT_CODE_FLUSH_FREE_COUNT = 4;
        namespace FLUSH_FREE_COUNT_RESULTS_CODE{
            namespace FATAL_REASONS_CODE{
                constexpr uint16_t COSISTENCY_VIOLATION=1; 
            }
        }
        constexpr uint8_t EVENT_CODE_TOP_FOLD = 5;
        constexpr uint8_t EVENT_CODE_FREE_PAGES_FLUSH = 6;
        namespace FREE_PAGES_FLUSH_RESULTS_CODE{
            namespace FAIL_REASONS_CODE{
                constexpr uint16_t FAIL_REASON_ADDR_NOT_BELONG=1;
            }
            namespace FATAL_REASONS_CODE{
                constexpr uint16_t COSISTENCY_VIOLATION=1; 
            }
        }
        constexpr uint8_t EVENT_CODE_FREE=7;
        namespace FREE_RESULTS_CODE{
            namespace FAIL_REASONS_CODE{
                constexpr uint16_t FAIL_REASON_CODE_BASE_NOT_BELONG= 1;
            }
        }
        constexpr uint8_t EVENT_CODE_REPLAY_VALIDATE=8;
        namespace REPLAY_VALIDATE_RESULTS_CODE{
            namespace FATAL_REASONS_CODE{
                constexpr uint16_t FAIL_REASON_CODE_INVALID_ORDER=1;
                constexpr uint16_t FAIL_REASON_CODE_INVALID_INDEX=2;
                constexpr uint16_t FAIL_REASON_CODE_INTERNAL_CONFLICT=3;
                constexpr uint16_t FAIL_REASON_CODE_PARENT_FREE_CHILD_USED=4;
                constexpr uint16_t FAIL_REASON_CODE_PARENT_USED_CHILD_USED=5;
                constexpr uint16_t FAIL_REASON_CODE_INTERNAL_BITMAP_MISSING=6;
            }
        }
    }
    constexpr uint8_t LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_BITMAP=33;
    namespace FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_BITMAP{
        constexpr uint8_t EVENT_CODE_INIT=0;
    }
};
struct alloc_params;
struct Alloc_result{
    uint64_t base;
    KURD_t result;
};
enum second_stage_init_strategy{
    INIT_STRATEGY_BEST_ALIGN_FIT=0,
    INIT_STRATEGY_MATCH_THREAD=1
};
class FreePagesAllocator {
public:
    struct flags_t {
        uint64_t allow_new_BCB : 1;
    };

public:
    static constexpr uint16_t _4KB_PAGESIZE = 4096;
    static flags_t flags;

    class BuddyControlBlock {
    private:
        static constexpr uint64_t INVALID_INBCB_INDEX = ~0;
        static constexpr uint8_t DESINGED_MAX_SUPPORT_ORDER = 64;
        static constexpr uint8_t PER_ORDER_CACHE_SUGGEST_COUNT = 8;
        using cache_order_suggest_t = uint64_t[PER_ORDER_CACHE_SUGGEST_COUNT];
        spinlock_cpp_t lock;
        bool is_splited_bitmap_valid;
        uint8_t max_supprt_order;
        phyaddr_t base; // base 至少是 4KB 对齐的，可以不是 1<<(MAX_SUPPORT_ORDER+12) 对齐的，只会影响外部获得的地址
        KURD_t default_kurd();
        KURD_t default_success();
        KURD_t default_error();
        KURD_t default_fatal();

        cache_order_suggest_t suggest_order_free_page_index[DESINGED_MAX_SUPPORT_ORDER]; // 每个 order 多条缓存，基于 BCB 开始地址的索引
        uint8_t suggest_order_cache_cursor[DESINGED_MAX_SUPPORT_ORDER];
        void cache_insert(uint8_t order, uint64_t idx);
        bool cache_pick(uint8_t order, uint64_t& out_idx);
        KURD_t conanico_free(
            uint64_t in_bcb_idx,
            uint8_t order
        );
        void free_page_without_merge( // 需要事件码
            uint64_t in_bcb_idx,
            uint8_t order
        );

        class mixed_bitmap_t : bitmap_t {
        private:
            uint64_t entry_count;
            KURD_t default_kurd();
            KURD_t default_success();
            KURD_t default_error();
            KURD_t default_fatal();

        public:
            using bitmap_t::bit_set;
            using bitmap_t::bit_get;
            mixed_bitmap_t(uint64_t entry_count);
            KURD_t second_stage_init();
            void first_bcb_specified_init(loaded_VM_interval* first_BCB_bitmap);
            uint64_t find_free_in_interval(
                uint64_t start_idx,
                uint64_t interval_length
            );
            ~mixed_bitmap_t();
        };

#ifdef REPALY_MODE
        void replay_internal_init();
        void replay_internal_mark_split(uint8_t order, uint64_t idx);
        void replay_internal_mark_free(uint8_t order, uint64_t idx);
        KURD_t replay_validate_node(uint8_t order, uint64_t idx, const char* tag);

        Ktemplats::kernel_bitmap* order_Internal_bitmap[DESINGED_MAX_SUPPORT_ORDER];
#endif
        friend mixed_bitmap_t::mixed_bitmap_t(uint64_t entry_count);
        mixed_bitmap_t* order_freepage_existency_bitmaps; // 这个位图编码为 1 表示这个 order 的对应引索的页面是存在的，反之不存在
        uint64_t order_bases[DESINGED_MAX_SUPPORT_ORDER]; // 在 mixed_bitmap_t 里面各 order 的引索基址

        struct BCB_statistics {
            uint64_t free_count[DESINGED_MAX_SUPPORT_ORDER];
            uint64_t suggest_hit[DESINGED_MAX_SUPPORT_ORDER];
            uint64_t suggest_miss[DESINGED_MAX_SUPPORT_ORDER];
            uint64_t alloc_times_success;
            uint64_t free_times_success;
            uint64_t alloc_times_fail;
            uint64_t scan_count;
            uint64_t fold_count_success;
            uint64_t fold_count_fail;
            uint64_t split_count;
        } statistics;

        KURD_t split_page(
            uint64_t splited_idx,
            uint8_t splited_order,
            uint8_t target_order
        );
        static uint8_t size_to_order(uint64_t size);
        bool is_reclusive_fold_success(uint64_t idx, uint8_t order); // true 成功，false 失败，是对这个 order 之下的所有二叉树进行折叠
        bool is_addr_belong_to_this_BCB_no_lock(phyaddr_t addr);
        void print_basic_info_no_lock();
        void print_bitmap_info_no_lock();
        void print_bitmap_order_info_compress_no_lock(uint8_t order);
        void print_bitmap_order_interval_compress_no_lock(uint8_t order, uint64_t base, uint64_t length);
#ifdef REPALY_MODE
        KURD_t replay_validate_tree_no_lock(const char* tag);
#endif

    public:
        bool is_bcb_avaliable();
        uint8_t get_max_order();
        friend phymemspace_mgr;
        void print_basic_info();
        void print_bitmap_info();
        void print_bitmap_order_info_compress(uint8_t order);
        void print_bitmap_order_interval_compress(uint8_t order, uint64_t base, uint64_t length);
        KURD_t free_pages_flush(); // 强制扫描位图校准 free_count[DESINGED_MAX_SUPPORT_ORDER] 数据结构
        BuddyControlBlock(
            phyaddr_t base,
            uint8_t max_support_order
        );
        void first_bcb_specified_init(loaded_VM_interval* first_BCB_bitmap);
        KURD_t second_stage_init();
        phyaddr_t allocate_buddy_way(
            uint64_t size,
            KURD_t& result,
            uint8_t align_log2=0
        );
        phyaddr_t get_base();
        void top_fold();
        KURD_t free_buddy_way(
            phyaddr_t base,
            uint64_t size
        );
#ifdef REPALY_MODE
        KURD_t replay_validate_tree(const char* tag);
#endif
        bool is_addr_belong_to_this_BCB(phyaddr_t addr);
        bool can_alloc(uint8_t order);
        ~BuddyControlBlock() = default;
    };
    friend phymemspace_mgr;
    friend KspaceMapMgr;
    static BuddyControlBlock* first_BCB; // 通过 pages_alloc 在 align_log2=30 时分配一个 1GB 页，哪个页用来初始化这个
    static uint64_t main_BCB_count;
    static BuddyControlBlock* main_BCBS;
    static uint64_t vice_BCB_count;
    static BuddyControlBlock* vice_BCBS;
    
    // second_stage后才FPA层级per_cpu的staisitics才会上线进行统计
    static fpa_stats*statistics_arr;
    static uint64_t*processors_preffered_bcb_idx;//也是只有后
public:
    struct strategy_t{
        second_stage_init_strategy strategy;
        uint64_t thread_coefficient;//当 strategy 是 MATCH_THREAD 时有效，表示每个线程建议的主BCB个数
    };
    static constexpr strategy_t BEST_FIT = {
        .strategy = INIT_STRATEGY_BEST_ALIGN_FIT,
        .thread_coefficient = 1
    };
    static KURD_t second_stage(strategy_t strategy);
    static KURD_t Init(loaded_VM_interval* first_BCB_bitmap);
    static Alloc_result alloc(uint64_t size, alloc_params params);//params只有在
    static KURD_t free(phyaddr_t base, uint64_t size);
    static fpa_stats get_fpa_stats();//当前本地CPU的统计数据，必须在second_stage初始化完成后才可以调用，否则行为未定义
    static fpa_stats get_fpa_stats(uint64_t pid);//pid为处理器id，必须在second_stage初始化完成后才可以调用，否则行为未定义,不提供锁保护
    static fpa_stats get_fpa_stats_all();//所有统计信息的总计，除bcb_scan_max是取最大，其他字段是求和，不在锁保护下
};

/**
 * @brief 
 * 对于分配参数的设计优先看位域控制，遵循以下的依赖
 * 1.force_first_bcb为1时强制只使用first_BCB，其它参数统统无效
 * 2.当force_first_bcb为0时，no_up_limit_bit，try_lock_always_try，no_addr_constrain_bit
 * 这三个位是三个独立的位
 * 2.1try_lock_always_try:为1只有确定所有所有BCB都不满足分配条件时才失败，在到达这个之前无限重试,为0时重试次数有上限
 * 2.2no_up_limit_bit:为0时等价于[0,up_phyaddr_limit)的地址限制
 * 2.3no_addr_constrain_bit:为0时等价于[constrain_base,constrain_base+constrain_interval_size)的地址限制]
 * 2.2和2.3若同时为0则区间取交集
 */
struct alloc_params{
    uint64_t numa;//不支持，暂时
    uint64_t constrain_base;
    uint64_t constrain_interval_size;
    uint64_t up_phyaddr_limit;
    uint64_t no_addr_constrain_bit:1;
    uint64_t try_lock_always_try:1;//多BCB的架构下，会尝试多次获取锁，失败次数过高会失败返回繁忙重试，这个标志位为1则永远尝试直到成功获取锁
    uint64_t no_up_limit_bit:1;
    uint64_t force_first_bcb:1;//强制只使用first_BCB,优先级最高的位
    uint8_t align_log2;
};
