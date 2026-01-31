#pragma once
#include "stdint.h"
#include "memmodule_err_definitions.h"
#include "Memory.h"
#include "util/lock.h"
#include "util/Ktemplats.h"
#include "util/bitmap.h"
#include "memory/phygpsmemmgr.h"
class KspaceMapMgr;
namespace MEMMODULE_LOCAIONS{
    constexpr uint8_t LOCATION_CODE_FREEPAGES_ALLOCATOR=28;
    
    constexpr uint8_t LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK=32;
    namespace FREEPAGES_ALLOCATOR{
        
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
            namespace FATAL_REASONS_CODE{
                constexpr uint16_t COSISTENCY_VIOLATION=1; 
            }
        }
    }
    constexpr uint8_t LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_BITMAP=33;
    namespace FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_BITMAP{
        constexpr uint8_t EVENT_CODE_INIT=0;
    }
};
struct alloc_params{
    uint64_t numa;//不支持，暂时
    uint64_t flags_bits;
    uint8_t align_log2;//对于BCB是不支持的
};
struct Alloc_result{
    uint64_t base;
    KURD_t result;
};
class FreePagesAllocator{ 
    private:
        static constexpr uint16_t _4KB_PAGESIZE=4096;    
        static struct flags_t{
            uint64_t allow_new_BCB:1;
        }flags;
        class free_pages_in_seg_control_block{
        private:
        static constexpr uint64_t INVALID_INBCB_INDEX=~0;
        static constexpr uint8_t DESINGED_MAX_SUPPORT_ORDER=64;
        trylock_cpp_t lock;
        bool is_splited_bitmap_valid;
        uint8_t MAX_SUPPORT_ORDER;
        phyaddr_t base;//base至少是4KB对齐的，可以不是1<<(MAX_SUPPORT_ORDER+12)对齐的,只会影响外部获得的地址
        KURD_t default_kurd();
        KURD_t default_success();
        KURD_t default_error();
        KURD_t default_fatal();
        uint64_t suggest_order_free_page_index[DESINGED_MAX_SUPPORT_ORDER];//是基于BCB开始地址的引索
        KURD_t conanico_free(
            uint64_t in_bcb_idx,
            uint8_t order
        );
        void free_page_without_merge(//需要事件码
            uint64_t in_bcb_idx,
            uint8_t order
        );
        class mixed_bitmap_t:bitmap_t{
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
            uint64_t find_free_in_interval(
                uint64_t start_idx,
                uint64_t interval_length
            );
            ~mixed_bitmap_t();
        };
        friend mixed_bitmap_t::mixed_bitmap_t(uint64_t entry_count);
        mixed_bitmap_t*order_freepage_existency_bitmaps;//这个位图编码为1表示这个order的对应引索的页面是存在的，反之不存在
        uint64_t order_bases[DESINGED_MAX_SUPPORT_ORDER];//在mixed_bitmap_t里面各order的引索基址    
        struct BCB_statistics
        {
        uint64_t free_count[DESINGED_MAX_SUPPORT_ORDER];
        uint64_t suggest_hit[DESINGED_MAX_SUPPORT_ORDER];
        uint64_t suggest_miss[DESINGED_MAX_SUPPORT_ORDER];
        uint64_t scan_count;
        uint64_t fold_count_success;
        uint64_t fold_count_fail;
        uint64_t split_count;
        }
        statistics;
        KURD_t split_page(
            uint64_t splited_idx,
            uint8_t splited_order,
            uint8_t target_order
        );
        static uint8_t size_to_order(uint64_t size);
        bool is_reclusive_fold_success(uint64_t idx, uint8_t order);//true成功,false失败,是对这个order之下的所有二叉树进行折叠
        public:
        uint8_t get_max_order();
        friend phymemspace_mgr;
        void print_basic_info();
        void print_bitmap_info();
        void print_bitmap_order_info_compress(uint8_t order);
        void print_bitmap_order_interval_compress(uint8_t order,uint64_t base,uint64_t length);
        KURD_t free_pages_flush();//强制扫描位图校准free_count[DESINGED_MAX_SUPPORT_ORDER]数据结构
        free_pages_in_seg_control_block(
            phyaddr_t base,
            uint8_t max_support_order
        );
        KURD_t second_stage_init();
        phyaddr_t allocate_buddy_way(
            uint64_t size,
            KURD_t&result
        );
        void top_fold();
        KURD_t free_buddy_way(
            phyaddr_t base,
            uint64_t size
        );
        bool is_addr_belong_to_this_BCB(phyaddr_t addr);
        ~free_pages_in_seg_control_block()=default;
    };
    friend KspaceMapMgr;
    static free_pages_in_seg_control_block*first_BCB;//通过pages_alloc在align_log2=30时分配一个1GB页，哪个页用来初始化这个
    public:
    static void enable_new_BCB_allow();
    static KURD_t Init();
    static Alloc_result alloc(uint64_t size,alloc_params params);
    static KURD_t free(phyaddr_t base,uint64_t size);
};