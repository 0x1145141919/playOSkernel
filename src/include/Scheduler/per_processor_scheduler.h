#pragma once
#include "abi/arch/x86-64/GS_Slots_index_definitions.h"
#include "abi/arch/x86-64/pt_regs.h"
#include "abi/os_error_definitions.h"
#include "util/Ktemplats.h"
#include "util/huge_bitmap.h"
#include "util/lock.h"
#include "ktime.h"
#include "memory/phygpsmemmgr.h"
namespace Scheduler{
    constexpr uint8_t self_scheduler=1;
    constexpr uint8_t scheduler_task_pool=2;
    namespace task_pool_events{
        constexpr uint8_t init=0;
        constexpr uint8_t slot_alloc=1;
        namespace alloc_results{
            namespace fail_reasons{
                constexpr uint16_t not_found=1;
            }
        }
        constexpr uint8_t slot_free=2;
        namespace free_results{ 
            namespace fail_reasons{
                constexpr uint16_t index_out_of_range=1;
                constexpr uint16_t not_allocated=2;
                constexpr uint16_t bad_tid=3;
                constexpr uint16_t sub_table_not_exist=4;
            }
        }
        namespace try_free_subtable{
            namespace fail_reasons{
                constexpr uint16_t null_pool=1;
                constexpr uint16_t not_all_empty=2;
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
    namespace self_scheduler_events{
        constexpr uint8_t timer_cpp_enter=0;
        namespace timer_cpp_enter_results{
            namespace fatal_reasons{
                constexpr uint16_t null_scheduler=1;
                constexpr uint16_t invalid_running_task_id=2;
                constexpr uint16_t null_task_ptr=3;
                constexpr uint16_t invalid_cs=4;
                constexpr uint16_t null_kthread_context=5;
                constexpr uint16_t invalid_stack_size=6;
                constexpr uint16_t rsp_out_of_range=7;
                constexpr uint16_t null_userthread_context=8;
                constexpr uint16_t invalid_task_type=9;
            }
        }
        constexpr uint8_t kthread_yield_enter=1;
        namespace kthread_yield_enter_results{
            namespace fatal_reasons{
                constexpr uint16_t null_scheduler=1;
                constexpr uint16_t invalid_running_task_id=2;
                constexpr uint16_t null_task_ptr=3;
                constexpr uint16_t null_kthread_context=4;
                constexpr uint16_t invalid_stack_size=5;
                constexpr uint16_t rsp_out_of_range=6;
                constexpr uint16_t invalid_task_type=7;
            }
        }
        constexpr uint8_t schedule_and_switch=2;
        namespace schedule_and_switch_results{
            namespace fatal_reasons{
                constexpr uint16_t empty_no_idle=1;
                constexpr uint16_t null_task_ptr=2;
            }
        }
        constexpr uint8_t task_set_ready=3;
        namespace task_set_ready_results{
            namespace fail_reasons{
                constexpr uint16_t null_task_ptr=1;
                constexpr uint16_t owner_scheduler_not_found=2;
                constexpr uint16_t wrong_owner_scheduler=3;
                constexpr uint16_t task_node_invalid=4;
                constexpr uint16_t state_transition_invalid=5;
                constexpr uint16_t queue_op_failed=6;
                constexpr uint16_t running_index_mismatch=7;
            }
        }
        constexpr uint8_t task_set_blocked=4;
        namespace task_set_blocked_results{
            namespace fail_reasons{
                constexpr uint16_t null_task_ptr=1;
                constexpr uint16_t owner_scheduler_not_found=2;
                constexpr uint16_t wrong_owner_scheduler=3;
                constexpr uint16_t task_node_invalid=4;
                constexpr uint16_t state_transition_invalid=5;
                constexpr uint16_t queue_op_failed=6;
                constexpr uint16_t running_index_mismatch=7;
                constexpr uint16_t invalid_block_reason=8;
            }
        }
        constexpr uint8_t task_set_zombie=5;
        namespace task_set_zombie_results{
            namespace fail_reasons{
                constexpr uint16_t null_task_ptr=1;
                constexpr uint16_t owner_scheduler_not_found=2;
                constexpr uint16_t wrong_owner_scheduler=3;
                constexpr uint16_t task_node_invalid=4;
                constexpr uint16_t state_transition_invalid=5;
                constexpr uint16_t queue_op_failed=6;
                constexpr uint16_t running_index_mismatch=7;
            }
        }
        constexpr uint8_t task_set_dead=6;
        namespace task_set_dead_results{
            namespace fail_reasons{
                constexpr uint16_t null_task_ptr=1;
                constexpr uint16_t owner_scheduler_not_found=2;
                constexpr uint16_t wrong_owner_scheduler=3;
                constexpr uint16_t task_node_invalid=4;
                constexpr uint16_t state_transition_invalid=5;
                constexpr uint16_t queue_op_failed=6;
            }
        }
        constexpr uint8_t get_now_running_task=7;
        namespace get_now_running_task_results{
            namespace fail_reasons{
                constexpr uint16_t not_running=1;
            }
            namespace fatal_reasons{
                constexpr uint16_t task_node_invalid=1;
                constexpr uint16_t task_ptr_null=2;
                constexpr uint16_t location_pool_mismatch=3;
                constexpr uint16_t location_owner_mismatch=4;
                constexpr uint16_t state_not_running=5;
            }
        }
    }
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
    zombie,
    dead
};
enum task_blocked_reason_t:uint8_t{
    invalid,
    sleeping,
    mutex,
    no_job
};
struct kthread_yield_raw_context{
    uint64_t rsp;//这里的rsp是最后push的rsp,然后后面就是结构体指针入rdi,切换栈
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
constexpr uint8_t DEFAULT_STACK_PG_COUNT=8;
constexpr uint32_t DEFAULT_STACK_SIZE=(DEFAULT_STACK_PG_COUNT-1)*4096;
struct kthread_context{
    x64_basic_context regs;
    vaddr_t stack_top;
    uint16_t stacksize;
};
struct userthread_context{
    x64_basic_context regs;
    //预留给cr3,pcid
    //预留给XSAVE等等
};
struct vCPU_context{
    //预留给VMCS等等
};
constexpr miusecond_time_stamp_t DEFALUT_TIMER_SPAN_MIUS=20000;
constexpr uint64_t INIT_DEFAULT_RFLAGS=0x202;
extern "C" void secure_hlt();
class task{
    private:
    
    task_state_t task_state;
    task_blocked_reason_t blocked_reason;
    task_type_t task_type;
    struct {
        uint32_t processor_id;
        uint32_t in_pool_index;
    }location;
    public:
    task(task_type_t task_type,void*context);//原子性初始化一个task
    void atomic_load();//原子性状态切换到running并且加载对应的上下文（必然会切换到上下文制定的rsp与rip）
    bool set_ready();//成功返回true,但是必须从init/blocked切换到才合法/成功，非法不会改状态字段
    bool set_blocked();//成功返回true,但是必须从running切换到才合法/成功，非法不会改状态字段
    bool set_dead();//成功返回true,只能由zombie切换到才合法/成功，非法不会改状态字段
    bool set_zombie();//合法前驱仅限running,blocked,ready
    ~task();//析构函数,原本是想只有dead才能运行，但是受abi制约，行为是无条件折构，只不过非dead状态会warning记录到日志
    miusecond_time_stamp_t accumulated_time;
    miusecond_time_stamp_t lastest_run_stamp;
    miusecond_time_stamp_t lastest_span_length;
    union{
        kthread_context*kthread;
        userthread_context*userthread;
        vCPU_context*vCPU;
    }context;
    friend class per_processor_scheduler;
    uint64_t get_tid();
    task_type_t get_task_type();
    uint32_t get_belonged_processor_id();
};
struct task_in_pool{
    task*task_ptr;
    uint32_t slot_version;
};
/**
 * 
 */
uint32_t tid_to_idx(uint64_t tid);
class task_pool{
    private:
    static KURD_t default_kurd();
    static KURD_t default_success();
    static KURD_t default_fail();
    static KURD_t default_fatal();
    static spinrwlock_cpp_t lock;//技术债之写饥饿，后续考虑无锁操作
    static constexpr uint32_t root_table_entry_count=1<<16;
    static constexpr uint32_t sub_table_entry_count=1<<16;
    struct subtable{
        huge_bitmap used_bitmap;
        task_in_pool task_table[sub_table_entry_count];
    };
    struct root_entry
    {
        subtable*sub;
        uint32_t last_max_slot_version;
    };
    static root_entry root_table[root_table_entry_count];
    static KURD_t enable_subtable(uint32_t high_idx);
    static KURD_t try_disable_subtable(uint32_t high_idx);
    static uint32_t last_alloc_index;
    /*
    alloc_tid逻辑为从last_alloc_index开始扫描位图，若找到空闲槽位则返回index,并且更新last_alloc_index为index+1，途中若遇到空subtable则创建
    根据index返回(index<<32)|table[index].slot_version(示意写法)为tid
    需要发明kurd错误语义
    */
    static uint64_t alloc_tid(KURD_t&kurd);
    public:
    static task* get_by_tid(uint64_t tid,KURD_t &kurd);
    static uint64_t alloc(
        task* task_ptr,
        KURD_t&kurd
    );//行为为若alloc_tid成功则在对应的tid的槽位填写task_ptr,并返回tid,否则返回～0ll并且传递kurd错误语义
    static KURD_t release_tid(uint64_t tid);
    static int Init();
};
struct per_processor_scheduler{ 
    private:
    uint32_t belonged_processor_id;
    static constexpr uint8_t PRIVATE_STACK_DEFAULT_PG_COUNT=4;
    vaddr_t stack_bottom;
    public:
    uint64_t now_running_tid;
    static constexpr uint16_t ready_queue_count=5;
    Ktemplats::list_doubly<task*> ready_queue[ready_queue_count];
    spinlock_cpp_t runqueue_locks[ready_queue_count];
};
constexpr uint32_t INVALID_NODE_INDEX=~0;
struct task_node{
    uint32_t pre_node;
    uint32_t next_node;
    task*task_ptr;
};
extern task_node null_task_node;
extern "C"{
    uint64_t create_kthread(void*(*entry)(void*),void*arg,KURD_t*out_kurd);
    void kthread_yield_true_enter(kthread_yield_raw_context* context);
    void kthread_yield();
    uint64_t* get_scheduler_private_stack_top(per_processor_scheduler* scheduler);
    void kthread_exit(uint64_t will);
    void kthread_dead_exit();
    void kthread_dead_exit_cppenter();
    void kthread_true_exit(uint64_t will);
    void kthread_self_blocked(task_blocked_reason_t reason);
    void kthread_self_blocked_cppenter(kthread_yield_raw_context* context);
    uint64_t wakeup_thread(uint64_t tid);//返回的是KURD但是受限于abi，需要分析
}
