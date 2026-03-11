# 文本打印层设计（基于 GfxImage）

日期：2026-02-08  
最后更新：2026-03-09  
范围：文本渲染/打印层（基于 Primitive GOP，输出到 GfxImage/backbuffer）  
定位：**early-boot / panic 路径足够**，支持**早期直接位图渲染**和**后期缓存渲染**两种模式，接口**面向非调度**环境，**不考虑多线程/调度语义**。  
落地实现：`textconsole_GoP` 全局静态类。

## 目标
- 文本打印层支持两种工作模式：
  - **直接位图渲染模式**：初始化后默认模式，直接调用 `PutPixel` 逐 bit 绘制，无需内存分配，适合 early-boot 阶段
  - **缓存渲染模式**：调用 `enable_font_render()` 后切换，使用预渲染的 glyph_cache，通过 `Blit` 快速绘制，性能更优
- 光标状态机基于字符坐标 `(m,n)`，运行时只做乘法换算得到像素起始点。
- 初始化阶段不分配内存，后期可切换到缓存渲染模式提升性能。

## 字符集与占位符
- 字符集来源：`16x32AsciiCharacterBitmapSet.h` 提供 256 字符位图。
- 预渲染范围（缓存渲染模式）：
  - 可视 ASCII：`0x20–0x7E`
  - 特殊字符：`\n \r \t \b \0`
- 非可视且非特殊字符统一使用 **占位符图像**：`ter16x32_data[2]`。

## 坐标与光标模型
- 字符坐标 `(m,n)` -> 像素起始地址：`(m * cell.w, n * cell.h)`。
- 依赖初始化时计算上界：
  - `cols = view.size.x / cell.w`
  - `rows = view.size.y / cell.h`
- 光标只维护 `(m,n)`，不维护像素坐标表。

## 关键常量
- `constexpr int kTabWidth = 4;`（实现中固定为 4）。
- `cell_size = (16, 32)`（固定字符尺寸）。

## 数据结构（落地后）
```cpp
struct TextViewport {
    Vec2i pos;      // 文字区域左上角（像素）
    Vec2i size;     // 文字区域大小（像素）
    Vec2i cell;     // 字符宽高（像素）
    int cols;       // size.x / cell.x
    int rows;       // size.y / cell.y
};

struct TextCursor {
    int m;          // 列
    int n;          // 行
};

class textconsole_GoP {
public:
    // 初始化：进入直接位图渲染模式
    static KURD_t Init(
        const unsigned char* font_bitmap,
        Vec2i cell_size,
        uint32_t font_color,
        uint32_t background_color
    );

    // 切换到缓存渲染模式：申请 glyph_cache 并预渲染字符
    static KURD_t enable_font_render();

    static bool Ready();
    static void PutChar(char ch);
    static void PutString(const char* s, uint64_t len);
    static void Clear();

private:
    static GfxImage template_character; // 仅切换 pixels 指针的 hack
    static TextViewport view;
    static TextCursor cursor;
    static const unsigned char* font_bitmap;
    static uint32_t font_color;
    static uint32_t background_color;
    static void* glyph_cache;           // 缓存渲染模式下有效
    static uint16_t glyph_index[256];
    static bool ready;
    static bool is_font_rendered;       // true: 缓存渲染模式，false: 直接位图渲染模式
};
```

## 初始化流程（直接位图渲染模式）
1. 接收 `font_bitmap`、字符尺寸、前景/背景色；屏幕尺寸来自 `GfxPrim::GetInfo()`。
2. 视口起点固定 `(0,0)`，计算 `cols` / `rows`。
3. **不分配 glyph_cache 内存**，设置 `is_font_rendered = false`。
4. 建立 `glyph_index[256]`：
   - 可视字符映射到 `[1..95]`
   - 其余映射到 `0`（占位符）
5. 初始化光标 `m=0, n=0`，标记 `ready = true`。
6. 此时调用 `PutChar` 会逐 bit调用 `GfxPrim::PutPixel` 直接绘制。

## 缓存渲染模式启用流程
1. 调用 `enable_font_render()` 函数。
2. 使用 `__wrapped_pgs_valloc` 分配 glyph_cache 内存：
   - **仅分配可视 ASCII(0x20–0x7E) + 占位符** 的预渲染结果。
   - 总大小 = `(95 + 1) * cell.w * cell.h * 4` 字节（对齐到 4KB）。
3. 执行预渲染：
   - 0 号索引渲染 `ter16x32_data[2]` 占位符
   - 其余索引按 `0x20–0x7E` 顺序渲染到 glyph_cache
4. 设置 `template_character.pixels = glyph_cache`。
5. 设置 `is_font_rendered = true`。
6. 此后调用 `PutChar` 会使用 `GfxPrim::Blit` 从 glyph_cache 快速绘制。

## 状态机规则（PutChar 实现）
### 控制字符处理（两种模式相同）
- 普通字符：
  - 计算像素起点 `(m*cell.w, n*cell.h)`
  - 根据 `is_font_rendered` 选择渲染方式
  - `m++`
- `\n`：`m=0, n++`
- `\r`：`m=0`
- `\t`：`m += kTabWidth`（实现为 4）
- `\b`：`m--` 并用背景色覆盖该字符格（`GfxPrim::FillRect`）

### 字符渲染逻辑
#### 直接位图渲染模式（`is_font_rendered == false`）
1. 根据字符索引从 `font_bitmap` 获取位图数据（每字符 `cell.y * 2` 字节）。
2. 用背景色填充字符区域（`GfxPrim::FillRect`）。
3. 逐行扫描位图数据：
   - 每行读取 2 字节（16bit，对应 16 像素宽度）
   - 对每个为 1 的 bit，调用 `GfxPrim::PutPixel(x, y, font_color)` 绘制前景像素
4. 直接写入 framebuffer，无中间缓存。

#### 缓存渲染模式（`is_font_rendered == true`）
1. 根据 `glyph_index[ch]` 获取预渲染索引。
2. 计算 glyph_cache 中的偏移：`offset = index * cell.w * cell.h * 4`。
3. 设置 `template_character.pixels = glyph_cache + offset`。
4. 调用 `GfxPrim::Blit(pos, &template_character)` 快速绘制。

### 滚屏处理（两种模式相同）
当 `m >= cols`：
- `m=0, n++`

当 `n >= rows`：
- 调用 `GfxPrim::MoveUp(view.pos, view.size, cell.h, bg)`
- `n = rows - 1`
- 调用 `GfxPrim::Flush()`

## 与 Primitive GOP 的关系
- **直接位图渲染模式**：
  - 直接调用 `GfxPrim::PutPixel` 写入 framebuffer
  - 调用 `GfxPrim::FillRect` 清空字符区域
  - 适合早期初始化阶段，无需内存分配
- **缓存渲染模式**：
  - 输出目标为 `GfxImage`（`template_character` 指向 glyph_cache）
  - 使用 `GfxPrim::Blit` 快速绘制预渲染字符
  - 性能更优，适合运行时环境
- 滚屏依赖 `GfxPrim::MoveUp()`。
- Flush 策略由上层统一调用 `GfxPrim::Flush()`。

## 接口设计（非调度 / 单线程语义）
> 以下接口默认运行在 early-boot / panic 路径，不提供锁、不保证线程安全，也不处理调度切换。
> 设计与 `GfxPrim` 一致，采用 **全局单例静态类**（实现为 `textconsole_GoP`）。

### 工作流程示例
```cpp
// 1. 早期初始化：直接位图渲染模式（无需内存分配）
textconsole_GoP::Init(ter16x32_data, {16, 32}, 0xFFFFFFFF, 0x000000);
textconsole_GoP::PutString("Early boot message\n");

// 2. 内存子系统就绪后：切换到缓存渲染模式
textconsole_GoP::enable_font_render();
textconsole_GoP::PutString("Cached rendering mode\n");

// 3. 运行时环境：可选地启动后台服务线程
textconsole_GoP::RuntimeInitServiceThread();
```

## 性能对比
| 特性 | 直接位图渲染模式 | 缓存渲染模式 |
|------|-----------------|-------------|
| 内存分配 | 无 | 需要 (约 25KB for 16x32) |
| 初始化时间 | 极快 | 较慢 (需预渲染) |
| 字符绘制速度 | 慢 (逐 pixel) | 快 (Blit 块传输) |
| 适用阶段 | early-boot / panic | runtime |
| framebuffer 访问 | 直接写入 | 通过 backbuffer |

## 已实现项
- ✅ `Init()` - 初始化为直接位图渲染模式
- ✅ `enable_font_render()` - 切换到缓存渲染模式
- ✅ `PutChar()` - 支持两种渲染模式
- ✅ `PutString()` - 字符串输出
- ✅ `Clear()` - 清屏
- ✅ `Ready()` - 状态查询

## 扩展项（可选）
- `RuntimeInitServiceThread()` - 后台服务线程
- `RuntimeSubmitString()` / `RuntimeSubmitChar()` / `RuntimeSubmitNum()` - 异步提交接口
- 多处理器同步与日志环形缓冲区支持
