# Init TextConsole - 基于位图的文本控制台

## 设计目标

为 init.elf 阶段提供轻量级文本控制台支持，直接通过 16x32AsciiCharacterBitmapSet.h 的位图数据在 GOP 层逐像素渲染字符。

## 与 kernel.elf 的 textConsole 的区别

| 特性 | textConsole (kernel.elf) | init_textconsole (init.elf) |
|------|-------------------------|----------------------------|
| 字库缓存 | 需要分配 glyph_cache | ❌ 不需要，直接读位图 |
| Runtime 支持 | ✅ 有环形缓冲和服务线程 | ❌ 无，同步渲染 |
| kout 集成 | ✅ 注册为后端 | ❌ 未注册 |
| 渲染方式 | Blit 缓存的图像 | 逐 bit 扫描写像素 |
| 内存分配 | 需要页分配器 | 无需分配 |
| 适用阶段 | 内核主阶段 | 早期初始化阶段 |

## 使用方法

### 1. 初始化

```cpp
#include "util/textConsole.h"
#include "16x32AsciiCharacterBitmapSet.h"

// 初始化 GOP (必须先于 console 初始化)
GlobalBasicGraphicInfoType gfx_info = { ... };
InitGop::Init(&gfx_info);

// 初始化文本控制台
Vec2i cell_size = {16, 32};  // 必须匹配字库大小
uint32_t font_color = 0xFFFFFFFF;      // 白色文字
uint32_t bg_color = 0xFF000000;        // 黑色背景

KURD_t status = init_textconsole_GoP::Init(
    ter16x32_data,  // 来自 16x32AsciiCharacterBitmapSet.h
    cell_size,
    font_color,
    bg_color
);

if (status.result != result_code::SUCCESS) {
    // 处理错误
}
```

### 2. 输出文本

```cpp
// 输出单个字符
init_textconsole_GoP::PutChar('A');

// 输出字符串
init_textconsole_GoP::PutString("Hello, World!");

// 清屏
init_textconsole_GoP::Clear();

// 换行和特殊字符
init_textconsole_GoP::PutString("Line 1\nLine 2");
init_textconsole_GoP::PutString("Tab:\tHere");
init_textconsole_GoP::PutString("Backspace\b\b..");
```

### 3. 完整示例

```cpp
#include "util/textConsole.h"
#include "16x32AsciiCharacterBitmapSet.h"
#include "core_hardwares/init_gop.h"

void init_console_demo()
{
    // 1. 初始化图形输出
    GlobalBasicGraphicInfoType gfx_info = get_gop_info_from_uefi();
    if (InitGop::Init(&gfx_info).result != result_code::SUCCESS) {
        return;
    }
    
    // 2. 初始化文本控制台
    if (init_textconsole_GoP::Init(
            ter16x32_data,
            {16, 32},
            0xFFFFFFFF,  // 白色
            0xFF000000   // 黑色
        ).result != result_code::SUCCESS) {
        return;
    }
    
    // 3. 清屏
    init_textconsole_GoP::Clear();
    
    // 4. 输出欢迎信息
    init_textconsole_GoP::PutString("=== Init Console Demo ===\n");
    init_textconsole_GoP::PutString("Resolution: ");
    // ... 可以继续输出更多信息
    
    // 5. 刷新屏幕
    InitGop::Flush();
}
```

## 渲染原理

### 字库数据结构

`ter16x32_data` 是一个 `[256][32][2]` 的三维数组:
- **256**: ASCII 字符索引 (0-255)
- **32**: 每个字符 32 行
- **2**: 每行 2 字节 (16bit = 16 像素)

```cpp
// 字库数据格式
const unsigned char ter16x32_data[256][32][2];

// 每个 bit 代表一个像素
// byte0: bits 7-0 = 像素 0-7
// byte1: bits 7-0 = 像素 8-15
```

### 渲染流程

```
1. 根据字符 ASCII 码获取索引
   ↓
2. 定位到 ter16x32_data[ch] 的 32 行数据
   ↓
3. 逐行扫描 (32 行)
   ├─ 读取 byte0 (高 8 像素)
   │  └─ 逐 bit 检查，如果为 1 则绘制前景色像素
   └─ 读取 byte1 (低 8 像素)
      └─ 逐 bit 检查，如果为 1 则绘制前景色像素
   ↓
4. 调用 InitGop::PutPixel() 写入 framebuffer
```

### 渲染代码示例

```cpp
void render_glyph(int m, int n, unsigned char ch)
{
    const uint8_t* glyph_bitmap = font_bitmap + ch * 32 * 2;
    
    for (int row = 0; row < 32; ++row) {
        uint8_t b0 = glyph_bitmap[row * 2 + 0];
        uint8_t b1 = glyph_bitmap[row * 2 + 1];
        
        // 高 8 像素
        for (int bit = 0; bit < 8; ++bit) {
            if (b0 & (0x80 >> bit)) {
                InitGop::PutPixel({x + bit, y + row}, font_color);
            }
        }
        
        // 低 8 像素
        for (int bit = 0; bit < 8; ++bit) {
            if (b1 & (0x80 >> bit)) {
                InitGop::PutPixel({x + 8 + bit, y + row}, font_color);
            }
        }
    }
}
```

## 控制字符支持

| 字符 | ASCII 码 | 效果 |
|------|---------|------|
| `\n` | 0x0A | 换行，光标移到下一行首 |
| `\r` | 0x0D | 回车，光标移到行首 |
| `\t` | 0x09 | 制表符，前进 4 列 |
| `\b` | 0x08 | 退格，删除前一个字符 |
| ` ` | 0x20 | 空格，前进一列 |

## 自动滚动

当光标超出屏幕底部时:
1. 调用 `InitGop::MoveUp()` 上移整个屏幕内容
2. 光标停留在最后一行
3. 自动调用 `InitGop::Flush()` 刷新

## API 参考

### 初始化
- `static KURD_t Init(...)` - 初始化控制台
- `static bool Ready()` - 检查是否就绪

### 输出
- `static void PutChar(char ch)` - 输出字符
- `static void PutString(const char* s)` - 输出字符串
- `static void Clear()` - 清屏

## 性能优化建议

1. **批量输出**: 使用 `PutString` 而非多次 `PutChar`
2. **减少刷新**: 大量输出后再调用 `InitGop::Flush()`
3. **避免频繁滚动**: 滚动操作成本较高
4. **颜色选择**: 使用对比度高的颜色组合

## 注意事项

1. **字符尺寸固定**: 必须使用 16x32 的 cell_size
2. **无缓存**: 每次渲染都直接读取位图数据
3. **同步阻塞**: 所有操作立即执行，无后台服务
4. **线程安全**: 非线程安全，单核 BSP 阶段使用
5. **GOP 依赖**: 必须先初始化 InitGop

## 典型应用场景

- UEFI Shell 中的文本输出
- 内核早期初始化的调试信息
- Bootloader 阶段的命令行界面
- Panic 信息的可视化显示

## 依赖

- `init_gop.h` - 底层图形输出
- `16x32AsciiCharacterBitmapSet.h` - 字库数据
- `abi/os_error_definitions.h` - 错误码定义
- `util/OS_utils.h` - 内存操作函数
