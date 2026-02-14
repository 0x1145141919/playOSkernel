# per_processor_scheduler 设计与外部接口行为（当前版本）

日期：2026-02-12  
范围：`src/include/Scheduler/per_processor_scheduler.h` 对应当前实现（含 `src/scheduler/per_processor_scheduler.cpp`、`src/scheduler/kthread_interfaces.cpp`、`src/scheduler/yield.asm`）  
目标：总结当前调度器结构，并重点说明可调用接口/外部接口的行为语义。

## 1. 总体模型

- 这是一个 **per-CPU 调度器**，每个 CPU 对应一个 `per_processor_scheduler` 实例。
- 调度实体是 `task`，状态为：`init -> ready -> running -> blocked/zombie -> dead`。
- 当前调度策略只使用 `ready_queue[0]`，`ready_queue[1..7]` 预留未使用。
- 关键数据结构：
  - `task_pool`：索引槽位分配（位图 + 稀疏表）。
  - `tasks_dll`：双向链表队列（ready/blocked/zombie）。
  - `now_running_task_index`：当前运行任务索引。
  - `idle_kthread_index`：空闲线程索引。

## 2. 初始化与基础约束

`per_processor_scheduler::per_processor_scheduler()`（`src/scheduler/per_processor_scheduler.cpp:171`）：

- 分配调度器私有栈（`defual_scheduler_stack_size = 16KB`）。
- 初始化全部队列，使其绑定到本 CPU 的 `task_pool`。
- 调用 `task_pool.second_state_init()` 完成二阶段初始化。
- 自动创建 `idle` kthread（入口 `secure_hlt_wrapper`），并记录为 `idle_kthread_index`。
- 失败路径：直接 `panic`。

依赖约束：

- 外部必须正确维护 `all_scheduler_ptr`，用于校验 task 归属 CPU（`task_set_*`/`get_now_running_task` 都依赖它）。
- 汇编入口通过 `GS:SCHEDULER_PRIVATE_GS_INDEX` 取当前调度器指针。

## 3. 状态迁移语义（当前实现）

内部状态修改函数在 `src/scheduler/per_processor_scheduler.cpp:237` 起：

- `set_ready()` 允许前驱：`init`/`blocked`/`running`。
- `set_blocked()` 允许前驱：`running`。
- `set_zombie()` 允许前驱：`running`/`blocked`/`ready`。
- `set_dead()` 允许前驱：`zombie`。

队列联动由 `task_set_ready/task_set_blocked/task_set_zombie/task_set_dead` 负责：

- `task_set_ready()`：
  - `init` 直接入 `ready_queue[0]`。
  - `blocked` 先从 `blocked_queue` 删除，再入 ready。
  - `running` 需要与 `now_running_task_index` 一致，清空运行位后入 ready。
- `task_set_blocked()`：
  - 仅允许 running 任务，且必须是当前运行任务。
  - 清空 `now_running_task_index` 后入 `blocked_queue`，写入 `blocked_reason`。
- `task_set_zombie()`：
  - running/blocked/ready 都可转入 `zombie_queue`，并清空 `blocked_reason`。
- `task_set_dead()`：
  - 仅允许 zombie，先从 `zombie_queue` 删除，再释放 `task_pool` 槽位。

## 4. C++ 可调用接口行为（per_processor_scheduler 对外）

### 4.1 `task* create_kthread(void (*func)(void*), void* data, create_kthread_param* param)`
位置：`src/scheduler/kthread_interfaces.cpp:125`

- 分配 task 槽位和 `kthread_context`。
- 初始寄存器：
  - `rip = func`
  - `rdi = data`
  - `rsp = stack_top + stacksize`
  - `rflags = INIT_DEFAULT_RFLAGS`
- `param->is_soon_ready == 1` 时会直接转 ready 并入队。
- 返回值：
  - 成功返回 `task*`。
  - 失败返回 `nullptr`，并在 `param->result_kurd` 记录失败。

### 4.2 `task* get_now_running_task(KURD_t& result_kurd)`
位置：`src/scheduler/per_processor_scheduler.cpp:734`

- 若当前无运行任务：返回 `nullptr`，`result_kurd` 置失败（`not_running`）。
- 若发现内部结构不一致（槽位无效、task 空、归属不一致、状态非 running）：直接 `panic`。
- 成功返回当前 running task。

### 4.3 `KURD_t task_set_ready(task* task_ptr)`
位置：`src/scheduler/per_processor_scheduler.cpp:788`

- 校验参数非空、owner scheduler 存在且为 `this`、pool 槽位一致。
- 根据当前状态做合法迁移并入 ready 队列。
- 失败返回 `fail KURD`（含具体 reason），不主动 panic。

### 4.4 `KURD_t task_set_blocked(task* task_ptr, task_blocked_reason_t reason)`
位置：`src/scheduler/per_processor_scheduler.cpp:866`

- 只接受 running 且为当前 running 的任务。
- `reason` 不能是 `invalid`。
- 成功后任务进入 blocked 队列。

### 4.5 `KURD_t task_set_zombie(task* task_ptr)`
位置：`src/scheduler/per_processor_scheduler.cpp:929`

- 支持 running/blocked/ready 三种来源，统一进入 zombie 队列。

### 4.6 `KURD_t task_set_dead(task* task_ptr)`
位置：`src/scheduler/per_processor_scheduler.cpp:1005`

- 只接受 zombie。
- 释放 task_pool 槽位（`processor_self_task_pool.free`）。

### 4.7 `void schedule_and_switch()`
位置：`src/scheduler/per_processor_scheduler.cpp:1060`

- 从 `ready_queue[0]` 弹出 head。
- 若 ready 空，回退到 idle task。
- 若无 idle 或取到空 task 指针：`panic`。
- 最后调用 `task::atomic_load()` 切到目标上下文；该路径设计为“不返回”。

## 5. 外部 C/汇编接口行为（重点）

声明位置：`src/include/Scheduler/per_processor_scheduler.h:387`  
汇编入口：`src/scheduler/yield.asm`

### 5.1 `kthread_yield()`

- 汇编保存当前 kthread 上下文（通用寄存器 + `rflags` + 栈信息）。
- 切到调度器私有栈后调用 `kthread_yield_true_enter`（`src/scheduler/kthread_interfaces.cpp:162`）。
- C++ 入口会：
  - 校验当前运行任务为 kthread、栈范围合法。
  - 统计本次运行时长。
  - 将当前任务转为 ready。
  - 调 `schedule_and_switch()` 切走。
- 失败或不一致：`panic`。

### 5.2 `timer_cpp_enter(x64_Interrupt_saved_context_no_errcode* frame)`

- 时钟中断调度入口（`src/scheduler/kthread_interfaces.cpp:215`）。
- 根据 `cs` 特权级区分 kthread/userthread 并做一致性校验。
- 保存中断现场到任务上下文，更新运行时长。
- 将被打断任务转回 ready，发送 EOI，重设计时器，再触发调度切换。
- 异常路径：`panic`。

### 5.3 `kthread_self_blocked(task_blocked_reason_t reason)`

- 汇编保存现场并切到调度器私有栈，进入 `kthread_self_blocked_cppenter`（`src/scheduler/kthread_interfaces.cpp:336`）。
- C++ 入口读取 `reason`，将当前 running kthread 置为 blocked 并调度下一个任务。
- `reason == invalid` 时会在 `task_set_blocked` 失败并进入 `panic`。

### 5.4 `kthread_exit(uint64_t will)` / `kthread_true_exit(uint64_t will)`

- 语义：当前 kthread 主动退出，退出值写入 `regs.rax`，任务转 zombie，然后立即调度切换（`src/scheduler/kthread_interfaces.cpp:298`）。
- 注意：汇编里导出的符号名为 `exit_kthread`（`src/scheduler/yield.asm:79`），头文件声明是 `kthread_exit`，当前版本存在命名不一致风险。

### 5.5 `kthread_dead_exit()` / `kthread_dead_exit_cppenter()`

- 语义目标：把当前任务从 zombie 转 dead 并清理资源后切走（`src/scheduler/kthread_interfaces.cpp:315`）。
- 当前实现流程是：
  - `task_set_zombie(dying_task)`
  - `delete dying_task->context.kthread`
  - `delete dying_task`
  - `task_set_dead(dying_task)`
- 这意味着 `task_set_dead` 使用了已经 delete 的指针，属于实现风险点；文档仅记录现状。

### 5.6 `uint64_t* get_scheduler_private_stack_top(per_processor_scheduler* scheduler)`

- 返回调度器私有栈顶指针，供汇编入口切换到安全栈执行调度逻辑（`src/scheduler/per_processor_scheduler.cpp:205`）。

## 6. 错误处理风格

- “可恢复错误”多通过 `KURD_t` 返回（`task_set_*`、`get_now_running_task` 的 not_running）。
- “结构不一致/上下文不合法”走 `panic_with_kurd`，即强失败策略。
- 事件码/失败原因定义集中在 `Scheduler::self_scheduler_events` 与相关子命名空间（`src/include/Scheduler/per_processor_scheduler.h:72`）。

## 7. 当前版本行为小结

- 这是一个单 CPU 就绪队列优先（`ready_queue[0]`）+ idle 回退的抢占/协作混合框架。
- 外部常用入口是：`kthread_yield`、`kthread_self_blocked`、`timer_cpp_enter`、`kthread_exit`/`kthread_dead_exit`。
- 接口语义整体偏“内核强约束”：参数或状态不一致通常直接 panic，而不是吞错继续运行。
