#pragma once
#include "stdint.h"
#include "memmodule_err_definitions.h"
#include "Memory.h"
#include "util/lock.h"
#include "util/Ktemplats.h"
#include "util/bitmap.h"
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
            namespace FATAL_REASONS_CODE{
                constexpr uint16_t UNREACHEABLE_CODE= 1;
                constexpr uint16_t INCONSISTENT_BITMAP_STATE= 2;
                constexpr uint16_t SUB_MODULES_NOT_ALL_INIT= 3;
            
            }
        }
        constexpr uint8_t EVENT_CODE_CONANICO_FREE = 2;
        namespace CONANICO_FREE_RESULTS_CODE{
            namespace FAIL_REASONS_CODE{
                constexpr uint16_t FAIL_REASON_CODE_INVALID_PAGE_INDEX= 1;
                constexpr uint16_t FAIL_REASON_CODE_INVALID_ORDER= 2;
                constexpr uint16_t FAIL_REASON_CODE_COALESCING_FAILED= 3;
                constexpr uint16_t FAIL_REASON_CODE_FAIL_REASON_CODE_DOUBLE_FREE= 4;
                constexpr uint16_t FAIL_REASON_CODE_DOUBLE_FREE= 5;
            }
            namespace FATAL_REASONS_CODE{
                constexpr uint16_t UNREACHEABLE_CODE= 1;
                constexpr uint16_t BUDDY_NOT_IN_LIST= 2;
                constexpr uint16_t LIST_REMOVE_FAILED= 3;
            }
        }
    }
};

class FreePagesAllocator{ 
private:
 static constexpr uint16_t _4KB_PAGESIZE=4096;
    
    
    class free_pages_in_seg_control_block{
        private:
        static constexpr uint64_t INVALID_INBCB_INDEX=~0;
        static constexpr uint8_t DESINGED_MAX_SUPPORT_ORDER=64;
        trylock_cpp_t lock;
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
        mixed_bitmap_t*order_bitmaps;
        uint64_t order_bases[DESINGED_MAX_SUPPORT_ORDER];//在mixed_bitmap_t里面各order的引索基址    
        KURD_t split_page(
            uint64_t splited_idx,
            uint8_t splited_order,
            uint8_t target_order
        );
        bool is_reclusive_fold_success(uint64_t idx, uint8_t order);//true成功,false失败,是对这个order之下的所有二叉树进行折叠
        public:
        uint8_t get_max_order();
        static uint8_t size_to_order(uint64_t size);
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
        ~free_pages_in_seg_control_block();
    };
    static free_pages_in_seg_control_block*first_BCB;//通过pages_alloc在align_log2=30时分配一个1GB页，哪个页用来初始化这个

};