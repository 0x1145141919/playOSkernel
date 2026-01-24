#pragma once
#include "stdint.h"
#include "memmodule_err_definitions.h"
#include "Memory.h"
#include "util/lock.h"
#include "util/Ktemplats.h"
#include "util/bitmap.h"
struct free_page_node_t {
        uint32_t inhcb_offset_specify_idx;
        uint32_t next_node_array_idx;//规定0xFFFFFFFF表示没有下一个节点
    };
namespace MEMMODULE_LOCAIONS{
    namespace FREEPAGES_ALLOCATOR{
        
    }
    namespace FREEPAGES_ALLOCATOR_BUDDY_CONTROL_BLOCK_NODES_ARRAY_EVENTS_CODES{
        namespace INIT_RESULTS_CODE{
            namespace FAIL_REASONS_CODE{
                constexpr uint8_t FAIL_REASON_CODE_BITMAP_INIT_FAIL = 1;
            }
        }
        constexpr uint8_t EVENT_CODE_ALLOCATE_NODE = 1;
        namespace ALLOCATE_NODE_RESULTS_CODE{
            namespace FAIL_REASONS_CODE{
                constexpr uint8_t FAIL_REASON_CODE_NODE_RESOURCES_RAN_OUT = 1;
            }
        }
        constexpr uint8_t EVENT_CODE_FREE_NODE = 2;
        namespace FREE_NODE_RESULTS_CODE{
            namespace FAIL_REASONS_CODE{
                constexpr uint8_t FAIL_REASON_CODE_IDX_OUT_OF_RANGE = 1;
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
            KURD_t allocate_node(free_page_node_t node,uint32_t& node_idx);
            KURD_t free_node(uint32_t node_idx);
            KURD_t regist_node(uint32_t node_idx);
            KURD_t get_node(uint32_t node_idx,free_page_node_t&node);
            KURD_t modify_node(uint32_t node_idx,free_page_node_t node);
            nodes_array_t(uint32_t nodes_maxcount);
            KURD_t second_stage_init();
            ~nodes_array_t();
        };
        nodes_array_t* nodes_array;
        
        class free_page_node_t_in_array_SLL{
        private:
            nodes_array_t&nodes_array;
            uint64_t nodes_count;
            uint32_t head_idx;
            uint8_t order;
            trylock_cpp_t lock; 
        public:
            uint32_t get_order();
            uint16_t get_head_idx();
            uint16_t get_nodes_count();
            free_page_node_t_in_array_SLL(
                uint8_t order,
                uint32_t head_idx,
                uint32_t nodes_count
            );
            KURD_t pop_head(uint32_t&idx);
            //这里的push只是简单机械的头指针加入链表，并不涉及任何合并判断
            KURD_t push_head(uint32_t idx);
            //真正的归还操作涉及可能的合并判断，暴露给free_pages_in_seg_control_block进行处理
            ~free_page_node_t_in_array_SLL();
        };
        free_page_node_t_in_array_SLL*orders_lists[DESINGED_MAX_SUPPORT_ORDER];
        uint64_t order_bitmap_bases[DESINGED_MAX_SUPPORT_ORDER];
        //这个模块没有KURD编号，读写基本是void会静默失败，没有越界检查
        class BCB_mixed_pages_bitmap:bitmap_t//位图语义上是1代表确定一个层级的空闲页存在，0代表这个空闲页不存在
        {//这个的位图相当大，一般需要上页框分配，为自举第一个1GB BCB使用预定义的
            uint64_t entryies_count;
        public:
        using bitmap_t::bits_set;
            BCB_mixed_pages_bitmap(
                uint64_t entryies_count
            );
            KURD_t second_stage_init();
            ~BCB_mixed_pages_bitmap();
        };
        BCB_mixed_pages_bitmap* mixed_pages_bitmap;
        
        free_pages_in_seg_control_block(
            phyaddr_t base,
            uint64_t size,
            uint8_t max_support_order
        );
        KURD_t second_stage_init();
        KURD_t allocate_mem(
            phyaddr_t&base,
            uint64_t&size
        );
        KURD_t free_mem(
            phyaddr_t base,
            uint64_t size
        );
        ~free_pages_in_seg_control_block();
    };
    static free_pages_in_seg_control_block*first_BCB;//通过pages_alloc在align_log2=30时分配一个1GB页，哪个页用来初始化这个

};