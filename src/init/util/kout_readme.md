# Init Kout - 简化版内核输出系统

## 设计目标

为 init.elf 阶段提供轻量级的统一输出接口，支持同时向 UART 串口和文本控制台输出调试信息。

## 与 kernel.elf 的 kout 的区别

| 特性 | kout (kernel.elf) | init_kout (init.elf) |
|------|-------------------|---------------------|
| 后端数量 | ✅ 最多 64 个 | ❌ 固定 2 个 (UART + Text) |
| 运行时支持 | ✅ 有环形缓冲和服务线程 | ❌ 无，同步阻塞 |
| Panic 支持 | ✅ 独立的 panic_write | ❌ 无 |
| dmesg 缓冲 | ✅ 有 | ❌ 无 |
| 时间戳 | ✅ 支持 HPET | ❌ 无 |
| 内存分配 | 需要 new/delete | 最小化使用 |
| 统计功能 | ✅ 完整统计 | ✅ 简化统计 |

## 后端架构

```
kio::bsp_kout
├── Backend 0: UART_COM1 (0x3F8)
│   ├── write(): 直接写串口
│   └── num(): 数字转字符串后输出
│
└── Backend 1: textconsole (可选)
    └── write(): 调用 init_textconsole::PutChar()
```

## 使用方法

### 1. 初始化

```cpp
#include "util/kout.h"
#include "util/textConsole.h"
#include "16x32AsciiCharacterBitmapSet.h"

void init_logging()
{
    // 1. 初始化 GOP
    GlobalBasicGraphicInfoType gfx_info = {...};
    InitGop::Init(&gfx_info);
    
    // 2. 初始化文本控制台
    init_textconsole::Init(
        ter16x32_data,
        {16, 32},
        0xFFFFFFFF,
        0xFF000000
    );
    
    // 3. 初始化 kout (自动注册 UART 后端)
    kio::bsp_kout.Init();
    
    // 4. 开始输出
    kio::bsp_kout << "=== Init Logging Started ===" << kio::kendl;
}
```

### 2. 基本输出

```cpp
#include "util/kout.h"

// 输出字符串
kio::bsp_kout << "Hello, World!" << kio::kendl;

// 输出数字 (默认十进制)
kio::bsp_kout << "Count: " << 42 << kio::kendl;

// 输出十六进制
kio::bsp_kout << "Address: 0x" 
              << kio::HEX_shift << 0x12345678 
              << kio::DEC_shift << kio::kendl;

// 输出指针
void* ptr = (void*)0xFFFF8000;
kio::bsp_kout << "Pointer: " << ptr << kio::kendl;

// 输出 KURD_t 错误码
KURD_t status = some_function();
kio::bsp_kout << "Status: " << status << kio::kendl;
```

### 3. 进制切换

```cpp
// 切换到二进制
kio::bsp_kout << kio::BIN_shift << 0b10101010 << kio::kendl;

// 切换到十进制
kio::bsp_kout << kio::DEC_shift << 123456 << kio::kendl;

// 切换到十六进制
kio::bsp_kout << kio::HEX_shift << 0xDEADBEEF << kio::kendl;

// 链式调用
kio::bsp_kout << "BIN: " << kio::BIN_shift << 255 
              << ", DEC: " << kio::DEC_shift << 255
              << ", HEX: " << kio::HEX_shift << 255 
              << kio::kendl;
```

### 4. 格式化数字

```cpp
// 不同大小的整数
uint8_t u8_val = 0xFF;
int8_t s8_val = -42;
uint16_t u16_val = 0xABCD;
int16_t s16_val = -1000;
uint32_t u32_val = 0x12345678;
int32_t s32_val = -99999;
uint64_t u64_val = 0x123456789ABCDEF0;
int64_t s64_val = -123456789012345;

kio::bsp_kout << "u8: " << u8_val << kio::kendl;
kio::bsp_kout << "s8: " << s8_val << kio::kendl;
kio::bsp_kout << "u16: " << u16_val << kio::kendl;
kio::bsp_kout << "s16: " << s16_val << kio::kendl;
kio::bsp_kout << "u32: " << u32_val << kio::kendl;
kio::bsp_kout << "s32: " << s32_val << kio::kendl;
kio::bsp_kout << "u64: " << u64_val << kio::kendl;
kio::bsp_kout << "s64: " << s64_val << kio::kendl;
```

## API 参考

### 流操作符

```cpp
kout& operator<<(const char* str);
kout& operator<<(char c);
kout& operator<<(const void* ptr);
kout& operator<<(uint64_t num);
kout& operator<<(int64_t num);
kout& operator<<(uint32_t num);
kout& operator<<(int32_t num);
kout& operator<<(uint16_t num);
kout& operator<<(int16_t num);
kout& operator<<(uint8_t num);
kout& operator<<(int8_t num);
kout& operator<<(KURD_t info);
kout& operator<<(radix_shift_t radix);
kout& operator<<(endl end);
```

### 控制函数

```cpp
void shift_bin();    // 切换到二进制
void shift_dec();    // 切换到十进制
void shift_hex();    // 切换到十六进制
```

### 后端管理

```cpp
uint64_t register_backend(kout_backend backend);
bool unregister_backend(uint64_t index);
bool mask_backend(uint64_t index);
```

### 统计信息

```cpp
struct kout_statistics_t {
    uint64_t total_printed_chars;
    uint64_t calls_str;
    uint64_t calls_char;
    uint64_t calls_ptr;
    uint64_t calls_u8;
    uint64_t calls_s8;
    uint64_t calls_u16;
    uint64_t calls_s16;
    uint64_t calls_u32;
    uint64_t calls_s32;
    uint64_t calls_u64;
    uint64_t calls_s64;
    uint64_t calls_KURD;
    uint64_t calls_shift_bin;
    uint64_t calls_shift_dec;
    uint64_t calls_shift_hex;
    uint64_t explicit_endl;
};

kout_statistics_t get_statistics();
```

## 后端自定义

### 添加自定义后端

```cpp
// 定义后端处理函数
void my_backend_write(const char* buf, uint64_t len)
{
    // 自定义输出逻辑
    for (uint64_t i = 0; i < len; ++i) {
        // ... 处理每个字符
    }
}

void my_backend_num(uint64_t raw, num_format_t format, numer_system_select radix)
{
    // 自定义数字输出逻辑
}

// 注册后端
kio::kout_backend my_backend = {
    .name = "my_backend",
    .is_masked = 0,
    .reserved = 0,
    .write = &my_backend_write,
    .num = &my_backend_num
};

uint64_t index = kio::bsp_kout.register_backend(my_backend);
if (index == ~0ULL) {
    // 注册失败
}
```

### 禁用/启用后端

```cpp
// 禁用后端
kio::bsp_kout.mask_backend(index);

// 启用后端
kio::bsp_kout.mask_backend(index);  // toggle
```

### 删除后端

```cpp
kio::bsp_kout.unregister_backend(index);
```

## 实现细节

### UART 后端

- **端口**: COM1 (0x3F8)
- **波特率**: 115200 (除数 = 1)
- **数据位**: 8
- **停止位**: 1
- **校验位**: 无
- **发送等待**: 检查 THRE (Transmitter Holding Register Empty)

### 数字转换

```cpp
// 内部缓冲区大小
static constexpr uint16_t MAX_STRING_LEN = 4096;
static constexpr char hex_chars[16] = {
    '0','1','2','3','4','5','6','7',
    '8','9','A','B','C','D','E','F'
};

// 支持的最大输出长度
- BIN: 64 bits + 符号位
- DEC: 20 digits + 符号位
- HEX: 16 digits
```

## 性能优化建议

1. **减少 endl 使用**: 每行都刷新会影响性能
2. **批量输出**: 尽量使用字符串而非多次单字符输出
3. **避免频繁进制切换**: 保持当前进制状态
4. **选择性屏蔽后端**: 不需要时可以屏蔽某个后端

## 注意事项

1. **同步阻塞**: 所有输出立即执行，会阻塞直到完成
2. **UART 依赖**: 需要确保串口已正确初始化
3. **线程安全**: 非线程安全，单核 BSP 阶段使用
4. **内存限制**: 最多支持 2 个后端
5. **无时间戳**: 不支持硬件时间戳输出

## 典型应用场景

- UEFI Shell 中的调试输出
- 内核早期初始化的日志记录
- 多核启动时的处理器间通信
- Panic 信息的紧急输出

## 示例代码

```cpp
#include "util/kout.h"
#include "util/textConsole.h"

void demo_init_logging()
{
    // 初始化
    kio::bsp_kout.Init();
    
    // 欢迎信息
    kio::bsp_kout << "=== System Initialization ===" << kio::kendl;
    
    // 输出各种类型
    kio::bsp_kout << "Integer: " << 42 << kio::kendl;
    kio::bsp_kout << "Hex: 0x" << kio::HEX_shift << 0xDEADBEEF << kio::kendl;
    kio::bsp_kout << "Ptr: " << (void*)0xFFFF8000 << kio::kendl;
    
    // 输出错误码
    KURD_t status = some_init_function();
    kio::bsp_kout << "Init Status: " << status << kio::kendl;
    
    // 恢复十进制
    kio::bsp_kout << kio::DEC_shift;
    
    // 查看统计
    auto stats = kio::bsp_kout.get_statistics();
    kio::bsp_kout << "Total chars: " << stats.total_printed_chars << kio::kendl;
}
```

## 依赖

- `textConsole.h` - 文本控制台后端 (可选)
- `PortDriver.h` - UART 串口驱动
- `os_error_definitions.h` - 错误码定义
- `OS_utils.h` - 内存操作函数
