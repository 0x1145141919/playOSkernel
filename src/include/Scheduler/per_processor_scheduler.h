#pragma once
#include "GS_Slots_index_definitions.h"
#include "pt_regs.h"
#include "os_error_definitions.h"
#include "util/Ktemplats.h"
#include "util/huge_bitmap.h"
#include "util/lock.h"
#include "time.h"
namespace Scheduler{
    constexpr uint8_t self_scheduler=1;
    constexpr uint8_t scheduler_task_pool=2;
    namespace scheduler_task_pool_events{
        constexpr uint8_t init=0;
        constexpr uint8_t slot_alloc=1;
        namespace slot_alloc_results{
            namespace fail_reasons{
                constexpr uint16_t not_found=1;
            }
        }
        constexpr uint8_t slot_free=2;
        namespace slot_free_results{ 
            namespace fail_reasons{
                constexpr uint16_t index_out_of_range=1;
                constexpr uint16_t not_allocated=2;
            }
        }
    }
    namespace scheduler_task_dll_events{
        constexpr uint8_t push_head=0;
        namespace push_head_results{
            namespace fail_reasons{
                constexpr uint16_t null_pool=1;
            }
        }
        constexpr uint8_t push_tail=1;
        namespace push_tail_results{
            namespace fail_reasons{
                constexpr uint16_t null_pool=1;
            }
        }
        constexpr uint8_t pop_head=2;
        namespace pop_head_results{
            namespace fail_reasons{
                constexpr uint16_t null_pool=1;
                constexpr uint16_t empty=2;
            }
        }
        constexpr uint8_t pop_tail=3;
        namespace pop_tail_results{
            namespace fail_reasons{
                constexpr uint16_t null_pool=1;
                constexpr uint16_t empty=2;
            }
        }
        constexpr uint8_t insert_after=4;
        namespace insert_after_results{
            namespace fail_reasons{
                constexpr uint16_t null_pool=1;
                constexpr uint16_t invalid_pre_index=2;
            }
        }
        constexpr uint8_t remove=5;
        namespace remove_results{
            namespace fail_reasons{
                constexpr uint16_t null_pool=1;
                constexpr uint16_t invalid_index=2;
            }
        }
    }
    constexpr uint8_t scheduler_task_dll=3;
};
enum task_type_t:uint8_t{
    kthreadm,
    userthread,
    vCPU
};
enum task_state_t:uint8_t{
    init=0,
    ready,
    running,
    blocked,
    dying,
    dead
};
enum task_blocked_reason_t:uint8_t{
    invalid
};
struct kthread_yield_raw_context{
    uint64_t rsp;//注意这里的rsp是特指原来上下文的rsp
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rflags;
    uint64_t rip;
};
struct x64_basic_context{ //后续设计上只有这些寄存器被认为是属于内核上下文
    uint64_t rax;
    uint64_t rbx;
    uint64_t rcx;
    uint64_t rdx;
    uint64_t rsi;
    uint64_t rdi;
    uint64_t rbp;
    uint64_t rsp;
    uint64_t r8;
    uint64_t r9;
    uint64_t r10;
    uint64_t r11;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rip;
    uint64_t rflags;
};
struct kthread_context{
    x64_basic_context regs;
    
};
struct userthread_context{
    x64_basic_context regs;
    //预留给cr3,pcid
    //预留给XSAVE等等
};
struct vCPU_context{
    //预留给VMCS等等
};
class task{
    public:
    task_type_t task_type;
    uint64_t task_id;
    task_state_t task_state;
    task_blocked_reason_t blocked_reason;
    miusecond_time_stamp_t accumulated_time;
    miusecond_time_stamp_t lastest_run_stamp;
    miusecond_time_stamp_t lastest_span_length;
    union{
        kthread_context*kthread;
        userthread_context*userthread;
        vCPU_context*vCPU;
    }context;
    task(task_type_t task_type,uint64_t task_id,void*context);//原子性初始化一个task
    KURD_t atomic_load();//原子性状态切换到running并且加载对应的上下文（必然会切换到上下文制定的rsp与rip）
    bool set_ready();//成功返回true,但是必须从init/blocked切换到才合法/成功，非法不会改状态字段
    bool set_blocked();//成功返回true,但是必须从running切换到才合法/成功，非法不会改状态字段
    bool set_dead();//成功返回true,只能由dying切换到才合法/成功，非法不会改状态字段
    bool set_dying();//合法前驱仅限running,blocked,ready
    ~task();//析构函数,原本是想只有dead才能运行，但是受abi制约，行为是无条件折构，只不过非dead状态会warning记录到日志

};
extern  task*idle_task;
constexpr uint32_t INVALID_NODE_INDEX=~0;
struct task_node{
    uint32_t pre_node;
    uint32_t next_node;
    task*task_ptr;
};
extern task_node null_task_node;
class per_processor_scheduler { 
    private:
    trylock_cpp_t this_try_lock;
    static constexpr uint32_t defual_scheduler_stack_size=1<<14;
    //todo :  构造函数中栈空间分配，使用__wrapped_pgs_valloc,大小defual_scheduler_stack_size
    uint64_t*scheduler_private_stack;
    public:
    
    class task_pool{
        private: 
        KURD_t default_kurd();
        KURD_t default_success();
        KURD_t default_fail();
        static constexpr uint8_t toptable_len_log2=8;
        static constexpr uint8_t leaftable_len_log2=8;
        Ktemplats::sparse_table_2level_no_OBJCONTENT<uint32_t,task_node,toptable_len_log2,leaftable_len_log2> task_pool_table;
        huge_bitmap task_pool_bitmap;
        public:
        task_pool():task_pool_bitmap(1<<(toptable_len_log2+leaftable_len_log2)){
        }
        KURD_t second_state_init();
        uint32_t alloc(KURD_t&result_kurd);
        task_node& get_task_node(uint32_t index);
        KURD_t free(uint32_t index);
    }processor_self_task_pool;
    class tasks_dll{
        private:
        uint32_t head;
        uint32_t tail;
        uint32_t count;
        task_pool* pool;
        KURD_t default_kurd();
        KURD_t default_success();
        KURD_t default_fail();
        KURD_t default_fatal();
        public:
        tasks_dll():head(INVALID_NODE_INDEX),tail(INVALID_NODE_INDEX),count(0){
        }
        tasks_dll(task_pool* pool):head(INVALID_NODE_INDEX),tail(INVALID_NODE_INDEX),count(0),pool(pool){
        }
        KURD_t push_head(uint32_t index);
        KURD_t push_tail(uint32_t index);
        KURD_t pop_head(uint32_t&index);
        KURD_t pop_tail(uint32_t&index);
        KURD_t insert_after(uint32_t pre_index,uint32_t insertor_index);
        KURD_t remove(uint32_t index);
        uint32_t get_count();
        uint32_t get_head();
        uint32_t get_tail();
        class iterator{
            private:
            uint32_t index;
            task_pool* pool_ptr;
            public:
            iterator(task_pool* pool_ptr, uint32_t index) : pool_ptr(pool_ptr), index(index) {
            }
            uint32_t get_index();
            task_node& operator*();
            iterator& operator++();
            iterator& operator--();
            bool operator!=(const iterator& other);
            bool operator==(const iterator& other);
        };
    };
    static constexpr uint8_t max_ready_queue_count=8;
    tasks_dll ready_queue[max_ready_queue_count];
    tasks_dll blocked_queue;
    tasks_dll dying_queue;
    uint32_t now_running_task_index;
    per_processor_scheduler();
    /**
     * 此函数是在ready_queue中选择一个task（调度），
     * 并且使用atomic_load原子性加载对应的task的上下文
     * 存在非idle task时，默认调度一个非idle task
     * 只有idle task时，才会调度idle task
     * 内部检测到数据结构一致性违背，则会内部panic,
     * 显然，上下文保存/设置ddline/相关数据结构的设置是由调度函数外部完成，
     */
    void schedule_and_switch();
};
extern "C"{
    void kthread_yield_true_enter(kthread_yield_raw_context* context);
    void kthread_yield();
}