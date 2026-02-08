# 文本打印层设计草案（基于 GfxImage）

日期：2026-02-08
范围：文本渲染/打印层（基于 Primitive GOP，输出到 GfxImage/backbuffer）
定位：**early-boot / panic 路径足够**，接口**面向非调度**环境，**不考虑多线程/调度语义**。
落地实现：`textconsole_GoP` 全局静态类。

## 目标
- 文本打印层仅操作 `GfxImage`，不直接访问 framebuffer。
- 光标状态机基于字符坐标 `(m,n)`，运行时只做乘法换算得到像素起始点。
- 初始化阶段预渲染字符图像缓存（方案 B），并提前分配内存。

## 字符集与占位符
- 字符集来源：`16x32AsciiCharacterBitmapSet.h` 提供 256 字符位图。
- 预渲染范围：
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

## 数据结构（落地后）
```
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
    static KURD_t Init(
        const unsigned char* font_bitmap,
        Vec2i cell_size,
        uint32_t font_color,
        uint32_t background_color
    );

    static bool Ready();
    static void PutChar(char ch);
    static void PutString(const char* s);   // 末尾 Flush
    static void Clear();                    // 末尾 Flush

private:
    static GfxImage template_character; // 仅切换 pixels 指针的 hack
    static TextViewport view;
    static TextCursor cursor;
    static const unsigned char* font_bitmap;
    static uint32_t font_color;
    static uint32_t background_color;
    static void* glyph_cache;
    static uint16_t glyph_index[256];
    static bool ready;
};
```

## 初始化流程（方案 B）
1. 接收 `font_bitmap`、字符尺寸、前景/背景色；屏幕尺寸来自 `GfxPrim::GetInfo()`。
2. 视口起点固定 `(0,0)`，计算 `cols` / `rows`。
3. 用 `__wrapped_pgs_valloc` 分配缓存：
   - **仅分配可视 ASCII(0x20–0x7E) + 占位符** 的预渲染结果。
4. 建立 `glyph_index[256]`：
   - 可视字符映射到 `[1..95]`
   - 其余映射到 `0`（占位符）
5. 执行预渲染：
   - 0 号索引渲染 `ter16x32_data[2]` 占位符
   - 其余索引按 `0x20–0x7E` 顺序渲染
6. 初始化光标 `m=0, n=0`，标记 ready。

## 状态机规则（草案）
- 普通字符：
  - 计算像素起点 `(m*cell.w, n*cell.h)`
  - `Blit` 渲染
  - `m++`
- `\n`：`m=0, n++`
- `\r`：`m=0`
- `\t`：`m += kTabWidth`（实现为 4）
- `\b`：`m--` 并用背景色覆盖该字符格

当 `m >= cols`：
- `m=0, n++`

当 `n >= rows`：
- 调用 `GfxPrim::MoveUp(view.pos, view.size, cell.h, bg)`
- `n = rows - 1`

## 与 Primitive GOP 的关系
- 输出目标为 `GfxImage`（通常是 `GfxPrim::BackBuffer()`）。
- 文本层不做 `Flush`，由上层统一调用 `GfxPrim::Flush()`。
- 滚屏依赖 `GfxPrim::MoveUp()`。

## 接口设计（非调度 / 单线程语义）
> 以下接口默认运行在 early-boot / panic 路径，不提供锁、不保证线程安全，也不处理调度切换。
> 设计与 `GfxPrim` 一致，采用 **全局单例静态类**（实现为 `textconsole_GoP`）。

## 待实现项
- `PutChar_runtime / PutString_runtime / Clear_runtime`（如需保留 runtime 分支）
