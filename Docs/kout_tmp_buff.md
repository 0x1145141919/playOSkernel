# tmp_buff 设计说明

## 目标
`tmp_buff` 用于在**并发场景下临时拼接输出内容**，避免多线程直接竞争 `kout` 的全局状态。
它是一个**轻量、栈上分配、可顺序追加**的临时缓冲结构，最终由 `kout` 统一消费并输出。

## 适用范围
- 适用于短生命周期、线程内局部的日志/调试输出拼接。
- 不用于跨线程共享或长期缓存。
- 不负责最终 I/O，只负责**记录输出“意图”**。

## 非目标
- 不提供动态扩容。
- 不保证跨线程安全。
- 不承担复杂格式化（格式化仍由 `kout` 完成）。

## 数据结构

### entry 描述
每条记录是一个 `entry`：
- `entry_type`：表示条目类型（字符串、字符、数值、指针、时间、换行、进制切换等）。
- `num_type`：仅在数值类条目中使用，标识长度/有无符号。
- `str_len`：仅在字符串条目中使用，记录长度（必要时裁剪）。
- `data`：联合体，保存数值或指针。

推荐的 `entry_type` 逻辑编码（可在实现中固化为 enum）：
- `STR`：字符串
- `CHAR`：字符
- `NUM`：数值
- `TIME`：now_time

推荐的 `num_type` 逻辑编码：
- `U8/U16/U32/U64`
- `S8/S16/S32/S64`

### 缓冲区布局
`tmp_buff` 设计为**栈上连续布局**：

```
[ tmp_buff 对象 ][ entry 数组 (entry_max 个) ]
```

构造时让 `entry_array` 指向紧随其后的 `entry` 数组区域。

### 推荐的栈上构造方式
建议用 placement new 在单块栈内存上构造：

```
constexpr uint32_t N = 64;
alignas(kio::tmp_buff) uint8_t storage[sizeof(kio::tmp_buff) + sizeof(kio::tmp_buff::entry) * N];

kio::tmp_buff* buf = new (storage) kio::tmp_buff(N);
// 构造函数内部把 entry_array 指向 storage + sizeof(tmp_buff)
```

> 说明：需要在构造函数中约定 `entry_array = reinterpret_cast<entry*>(this + 1);`

## 生命周期
- 构造：绑定 `entry_array`，清空 `entry_top`。
- 使用：通过 `operator<<` 顺序追加 entry。
- 消费：由 `kout` 作为 friend 读取 `entry_array` 并输出。
- 销毁：无需显式释放（栈上自动回收）。

## 线程模型
- **线程内私有**：每个线程/调用栈持有自己的 `tmp_buff`。
- 不允许跨线程共享或在多个 CPU 栈上混用。

## 追加规则
- 追加成功：`entry_top++`。
- 追加失败（缓冲满）：
  - 推荐策略：丢弃该条目，并在缓冲中记录一次 `ENTRY_DROPPED` 计数，便于输出告警。
  - 或者直接忽略（当前行为可先简化为忽略）。

## 输出语义
- `tmp_buff` 只记录**“要输出什么”**，不直接序列化文本。
- `kout` 负责最终输出，包括：
  - 字符串拼接
  - 数值按当前 `num_sys` 格式化
  - `KURD_t` 的结构化打印
  - `endl` 的行结束处理

## num_sys 与进制切换
- `tmp_buff` 维持 `num_sys` 状态。
- 当遇到 `operator<<(numer_system_select)` 时，写入 `RADIX` 类型 entry。
- `kout` 消费时将 radix 应用于随后的数值条目。

## KURD_t 处理
- 以 `entry_type=KURD` 保存完整 `uint64_t raw` 或结构体拷贝（取决于后续实现）。
- 输出由 `kout` 统一处理模块/事件/结果码解析。

## 错误处理建议
- 字符串长度超过 `kout::MAX_STRING_LEN` 时：截断并保留结尾 `...` 标记。
- `nullptr` 字符串：输出 `(null)`。
- `nullptr` 指针：输出 `0x0`。

## 与 kout 的集成建议
- 增加 `kout::flush(tmp_buff&)` 或 `kout::operator<<(tmp_buff&)`。
- `flush` 中遍历 `entry_array[0..entry_top)`，逐项调用对应输出逻辑。
- `flush` 必须是原子输出，避免多线程 interleave。

## 统计与诊断
建议在 `tmp_buff` 中保留：
- `uint32_t dropped_count`：当缓冲满时递增。
- `uint32_t max_depth`：记录最大 entry_top。

## 兼容性与演进
- 若未来需要 heap 版本，可新增 `tmp_buff_heap`，但不破坏现有栈上约定。
- 若需要跨线程安全，可在 `kout` 层统一合并缓冲。

