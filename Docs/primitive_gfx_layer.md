# Primitive 渲染层设计（草案）

日期：2026-02-08
负责人：Codex（草案供评审）
范围：Primitive 渲染层（framebuffer + 基础 blit/几何），双缓冲

## 目标
- 提供与文本/console 逻辑解耦的 primitive 渲染层。
- 从 `kinit.cpp` 的 `TFG`（即 `transfer->graphic_metainfo`）初始化。
- 缓冲区大小严格使用 `FrameBufferSize`（不再重算）。
- 像素格式严格限制为 `PixelBlueGreenRedReserved8BitPerColor`。
- 引入 DRAM 软件后备缓冲区，并提供 `ksystemramcpy` + `sfence` 刷新路径。
- 规定图片格式以支持快速 blit。

## 非目标
- 字体/console 渲染、滚屏、Shell 行为。
- 多格式转换（不支持的格式直接失败）。

## 数据模型

### Framebuffer 信息（来源）
- 来源于 `TFG`（`BootInfoHeader::graphic_metainfo`）。
- `FrameBufferSize` 是唯一有效大小来源。
- `pixelFormat` 必须为 `PixelBlueGreenRedReserved8BitPerColor`。

### Primitive 层类设计（C++，全局静态）
Primitive 层以 **C++ 类** 形式组织，但实例为 **全局静态单例**，且只能通过唯一入口初始化。
```
struct Vec2i {
    int x;
    int y;
};
```
```
class GfxPrim {
public: 
    struct Info {
        uint32_t width;              // horizentalResolution
        uint32_t height;             // verticalResolution
        uint32_t pitch_pixels;       // PixelsPerScanLine
        uint32_t format;             // PixelBlueGreenRedReserved8BitPerColor
        uint32_t fb_bytes;           // FrameBufferSize
        uintptr_t fb_paddr;          // TFG 提供的物理地址
        uintptr_t fb_vaddr;          // 经过重映射的虚拟地址（WC）
        void* backbuffer;            // DRAM 后备缓冲区（WB），大小 == fb_bytes
    };

    // 唯一入口
    static KURD_t Init(GlobalBasicGraphicInfoType* metainf);

    static bool Ready();
    static const Info GetInfo();
    static void* BackBuffer();

    // Flush
    static void Flush();
    static void FlushRect(Vec2i pos, Vec2i size);

    // Pixel / Geometry
    static void PutPixelUnsafe(Vec2i pos, uint32_t color);
    static void PutPixel(Vec2i pos, uint32_t color);
    static void DrawHLine(Vec2i pos, int len, uint32_t color);
    static void DrawVLine(Vec2i pos, int len, uint32_t color);
    static void FillRect(Vec2i pos, Vec2i size, uint32_t color);
    static void MoveUp(Vec2i pos, Vec2i size, int dy, uint32_t fill_color);

    // Image blit
    static void Blit(Vec2i pos, const GfxImage* img);

private:
    static Info s_info;
    static bool s_ready;
};
```

## 缓存策略
- 硬件 framebuffer（MMIO-like）映射：**WC**（写合并）。
- 软件 backbuffer（DRAM）：**WB**（默认）。
- 刷新屏障：`sfence`。

## 初始化流程（从 `kinit.cpp`）
1. 从 `TFG` 读取参数（`GlobalBasicGraphicInfoType* metainf` 作为 `GfxPrim::Init` 入参）。
2. 校验：
   - `FrameBufferBase != 0`
   - `FrameBufferSize != 0`
   - `pixelFormat == PixelBlueGreenRedReserved8BitPerColor`
3. 映射 framebuffer 到内核虚拟地址：
   - 使用 `KspaceMapMgr::pgs_remapp` 且 cache 策略为 **WC**。
4. 分配后备缓冲区：
   - `__wrapped_pgs_valloc(kurd, fb_bytes / 4096, KERNEL, alignment_log2)`
   - 后备缓冲区是 DRAM，默认 WB。
5. 记录 `fb_vaddr` 与 `backbuffer`。
6. Primitive 层进入 `Ready` 状态（`GfxPrim::Ready()` 为 true）。

## 对外接口（草案）

### 初始化 / 状态
- `static KURD_t GfxPrim::Init(GlobalBasicGraphicInfoType* metainf);`（唯一入口）
- `static bool GfxPrim::Ready();`
- `static const GfxPrim::Info GfxPrim::GetInfo();`

### 双缓冲
- `static void* GfxPrim::BackBuffer();`
- `static void GfxPrim::Flush();`
- `static void GfxPrim::FlushRect(Vec2i pos, Vec2i size);`

刷新行为：
- `ksystemramcpy(backbuffer, framebuffer, fb_bytes);`
- `sfence` 确保可见性。

### 像素 / 几何
- `static void GfxPrim::PutPixelUnsafe(Vec2i pos, uint32_t color);`
- `static void GfxPrim::PutPixel(Vec2i pos, uint32_t color);`
- `static void GfxPrim::DrawHLine(Vec2i pos, int len, uint32_t color);`
- `static void GfxPrim::DrawVLine(Vec2i pos, int len, uint32_t color);`
- `static void GfxPrim::FillRect(Vec2i pos, Vec2i size, uint32_t color);`
- `static void GfxPrim::MoveUp(Vec2i pos, Vec2i size, int dy, uint32_t fill_color);`

### 图片 blit
GOP 层只支持 **单一像素格式**（`PixelBlueGreenRedReserved8BitPerColor`）。调用者应通过 `GetInfo()` 获取像素格式并自行生成图片数据。
因此 `GfxImage` 不再包含格式字段，`Blit` 以快速拷贝为目标。
```
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;
    const void* pixels;
} GfxImage;
```

- `static void GfxPrim::Blit(Vec2i pos, const GfxImage* img);`

调用语义（当前实现）：
- `PutPixel/DrawHLine/DrawVLine/FillRect/Blit` 在越界时静默返回，不抛错。
- `DrawHLine/FillRect` 使用 `ksetmem_64` 提升填充性能。
- `Blit` 使用 `ksystemramcpy` 按行复制。

## 错误语义
- `Init` 使用 `KURD_t` 并结合 `COREHARDWARES_LOCATIONS::GOP_PRIMITIVE_DRIVERS_EVENTS::INIT` 的失败原因。
- 像素/几何/Blit 为 `void`，越界或无效状态下静默返回。

## 集成说明
- 现有 `InitialGlobalBasicGraphicInfo` 可被替换或由 `GfxPrim::Init` 进行包装。
- `KspaceMapMgr::Init()` 之后进行 framebuffer 映射与 backbuffer 分配。
- console/text 层应渲染到 backbuffer，再调用 `GfxPrim::Flush()`。

