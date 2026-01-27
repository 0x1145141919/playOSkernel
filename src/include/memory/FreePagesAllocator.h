#pragma once
#include "stdint.h"
#include "memmodule_err_definitions.h"
#include "Memory.h"
#include "util/lock.h"
#include "util/Ktemplats.h"
#include "util/bitmap.h"
struct free_page_node_t {
        uint32_t in_bcb_offset_specify_idx;
        uint32_t next_node_array_idx;//规定0xFFFFFFFF表示没有下一个节点
    };
namespace MEMMODULE_LOCAIONS{
        constexpr uint8_t LOCATION_CODE_FREEPAGES_ALLOCATOR=28;
        constexpr uint8_t LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK=32;
        constexpr uint8_t LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY=33;
        constexpr uint8_t LOCATION_CODE_FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST=34;
    namespace FREEPAGES_ALLOCATOR{
        
    }
    namespace FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY_EVENTS_CODES{
        namespace INIT_RESULTS_CODE{
            namespace FAIL_REASONS_CODE{
                constexpr uint8_t FAIL_REASON_CODE_BITMAP_INIT_FAIL = 1;
            }
        }
        constexpr uint8_t EVENT_CODE_ALL_EVENTS_SHARED=1;
        namespace ALL_EVENTS_SHARED{
            namespace FAIL_REASONS_CODE{
                constexpr uint8_t FAIL_REASON_CODE_INVALID_NODE_INDEX = 1;
                constexpr uint8_t FAIL_REASON_CODE_NODE_ALREADY_ALLOCATED = 2;
                constexpr uint8_t FAIL_REASON_CODE_NODE_NOT_ALLOCATED = 3;
            }
        }
        constexpr uint8_t EVENT_CODE_ALLOC_NODE=4;
        namespace ALLOC_NODE{
            namespace FAIL_REASONS_CODE{
                constexpr uint8_t FAIL_REASON_CODE_NO_FREE_NODE = 1;
            }
        }
    }
    namespace FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_ORDER_LIST_EVENTS_CODES{
        constexpr uint8_t EVENT_CODE_PUSH_HEAD=1;
        constexpr uint8_t EVENT_CODE_POP_HEAD=2;
        namespace POP_HEAD_EVENTS{
            namespace FAIL_REASONS_CODE{
                constexpr uint8_t FAIL_REASON_CODE_LIST_EMPTY = 1;
            }
            namespace FATAL_REASONS_CODE{
                constexpr uint8_t HEAD_NODE_INVALID = 1;
            }
        }
        constexpr uint8_t EVENT_CODE_INC_COUNT=3;
        namespace INC_COUNT_EVENTS{
            namespace FAIL_REASONS_CODE{
                constexpr uint8_t FAIL_REASON_CODE_COUNT_OVERFLOW=1;
            }
        }
        constexpr uint8_t EVENT_CODE_DEC_COUNT=4;
        namespace DEC_COUNT_EVENTS{
            namespace FAIL_REASONS_CODE{
                constexpr uint8_t FAIL_REASON_CODE_COUNT_UNDERFLOW=1;
            }
        }
        constexpr uint8_t EVENT_CODE_DEL_NODE_BY_PAGE_IDX=5;
        namespace DEL_NODE_BY_PAGE_IDX_EVENTS{
            namespace FAIL_REASONS_CODE{
                constexpr uint8_t FAIL_REASON_CODE_TARGET_NODE_NOT_FOUND = 1;
            }
            namespace FATAL_REASONS_CODE{
                constexpr uint8_t LENGTH_CHECK_FAIL = 1;
            }
        }
    }
    namespace FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_EVENTS_CODES{ 
        constexpr uint8_t EVENT_CODE_SUB_MODULES_INIT = 0;
        namespace SUB_MODULES_INIT_RESULTS_CODE{
            namespace FAIL_REASONS_CODE{
                constexpr uint8_t FAIL_REASON_CODE_ORDER_SLL_INIT_FAIL = 1;//链表模块初始化没有第二阶段init，所以需要上层BCB为其收尸
                constexpr uint8_t FAIL_REASON_CODE_PARAM_CONFLICT = 2;
                constexpr uint8_t FAIL_REASON_CODE_ZERO_SIZE = 3;
                constexpr uint8_t FAIL_REASON_CODE_ORDER_TOO_LARGE = 4;
            }
        }
        constexpr uint8_t EVENT_CODE_ALLOCATE_BUDY_WAY = 1;
        namespace ALLOCATE_BUDY_WAY_RESULTS_CODE{ 
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
        static constexpr uint8_t DESINGED_MAX_SUPPORT_ORDER=32;
        uint8_t MAX_SUPPORT_ORDER;
        uint32_t MIN_ALIGN_CONTENT;//= 1<<(12+MAX_SUPPORT_ORDER+1);
        phyaddr_t base;//base%MIN_ALIGN_CONTENT==0
        uint64_t size;//size%MIN_ALIGN_CONTENT==0
        KURD_t default_kurd();
        KURD_t default_success();
        KURD_t default_error();
        KURD_t default_fatal();
        class nodes_array_t{
        private:
            free_page_node_t* nodes;//这个的free_page_node_t相当大，一般需要上页框分配，为自举第一个1GB BCB使用预定义的
            uint32_t nodes_maxcount;//很明显最大到0xFFFFFFFF，这样引索最大值0xFFFFFFFE
            Ktemplats::kernel_bitmap*nodes_bitmap;
            KURD_t default_kurd();
            KURD_t default_success();
            KURD_t default_error();
            KURD_t default_fatal();
        public:
            KURD_t free_node(uint32_t node_idx);//对应位置0（先检查有没有占有，没有就返回错误）
            KURD_t regist_node(uint32_t node_idx);
            KURD_t get_node(uint32_t node_idx,free_page_node_t&node);
            KURD_t modify_node(uint32_t node_idx,free_page_node_t node);
            KURD_t alloc_node(uint32_t&node_idx,free_page_node_t&node);//找到一个空闲节点，标记为占用，返回引索
            nodes_array_t(uint32_t nodes_maxcount);
            KURD_t second_stage_init();
            ~nodes_array_t();
        };
        nodes_array_t* nodes_array;
        
        class free_page_node_t_in_array_SLL{
        private:
            nodes_array_t*nodes_array;
            uint64_t nodes_count;
            uint32_t head_idx;
            uint8_t order;
            KURD_t default_kurd();
            KURD_t default_success();
            KURD_t default_error();
            KURD_t default_fatal();
        public:
            uint32_t get_order();
            uint16_t get_head_idx();
            uint16_t get_nodes_count();
            KURD_t nodes_count_inc();
            KURD_t nodes_count_dec();
            free_page_node_t_in_array_SLL(
                uint8_t order,
                uint32_t head_idx,
                uint32_t nodes_count,
                nodes_array_t*nodes_array
            );
            KURD_t pop_head(uint32_t&in_bcb_page_location_idx);//会手动管理槽位
            //这里的push只是简单机械的头指针加入链表，并不涉及任何合并判断
            KURD_t push_head(uint32_t in_bcb_page_location_idx);//会手动管理槽位
            //真正的归还操作涉及可能的合并判断，暴露给free_pages_in_seg_control_block进行处理
            KURD_t del_node_by_bcb_location_idx(uint32_t in_bcb_page_location_idx);
            ~free_page_node_t_in_array_SLL();
            class iterator{//不会手动管理槽位
            private:
                uint32_t in_nodes_array_index;
                free_page_node_t_in_array_SLL&list;
            public:
                iterator(
                    uint32_t idx,
                    free_page_node_t_in_array_SLL&list
                );
                int operator*();
                iterator& operator++();
                bool is_end();
                iterator& operator=(const iterator& other);
        };
            friend class iterator;
        };
        free_page_node_t_in_array_SLL*orders_lists[DESINGED_MAX_SUPPORT_ORDER];
        uint64_t order_bitmap_bases[DESINGED_MAX_SUPPORT_ORDER];
        //这个模块没有KURD编号，读写基本是void会静默失败，没有越界检查
        class BCB_mixed_pages_bitmap:bitmap_t//位图语义上是1代表确定一个层级的空闲页存在，0代表这个空闲页不存在
        {//这个的位图相当大，一般需要上页框分配，为自举第一个1GB BCB使用预定义的
            uint64_t entryies_count;
        public:
        using bitmap_t::bits_set;
        using bitmap_t::bit_set;
        using bitmap_t::bit_get;
            BCB_mixed_pages_bitmap(
                uint64_t entryies_count
            );
            KURD_t second_stage_init();
            ~BCB_mixed_pages_bitmap();
        };
        BCB_mixed_pages_bitmap* mixed_pages_bitmap;
        struct page_record{
            uint32_t in_bcb_idx;
            uint8_t order;
        };
        KURD_t conanico_free(
            uint32_t in_bcb_idx,
            uint8_t order
        );
        public:
        struct tight_alloc_result_complex{
            /**
             * 此结构体是tight allocate的返回结果，包含了分配的页信息和用于释放的记录（不对外可见）
             * 此结构体以及pages_record_array的内存由new分配,使用者不建议尝试修改内容，由对应的BCB创建销毁
             */
            phyaddr_t base;
            uint64_t size;
            uint64_t check_sum;
            uint64_t pages_record_count;
            page_record* pages_record_array;
        };
        uint8_t get_max_order();
        static uint8_t size_to_order(uint64_t size);
        free_pages_in_seg_control_block(
            phyaddr_t base,
            uint64_t size,
            uint8_t max_support_order
        );
        KURD_t second_stage_init();
        phyaddr_t allocate_buddy_way(
            uint64_t size,
            KURD_t&result
        );
        
        KURD_t free_buddy_way(
            phyaddr_t base,
            uint64_t size
        );
        phyaddr_t allocate_buddy_way_but_higher_than_max_order(
            uint64_t size,
            KURD_t&result
        );
        KURD_t free_buddy_way_but_higher_than_max_order(//兼容free_buddy_way的没到最高order情况但是不推荐使用
            phyaddr_t base,
            uint64_t size
        );
        tight_alloc_result_complex* tight_allocate(
            uint64_t size,
            KURD_t& result
        );
        KURD_t tight_free(
            tight_alloc_result_complex* alloc_record
        );
        ~free_pages_in_seg_control_block();
    };
    static free_pages_in_seg_control_block*first_BCB;//通过pages_alloc在align_log2=30时分配一个1GB页，哪个页用来初始化这个

};