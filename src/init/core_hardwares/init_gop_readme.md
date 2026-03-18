# Init GOP 驱动 - 简化版图形输出协议驱动

## 设计目标

为 init.elf 阶段提供轻量级的图形输出支持，直接写入硬件帧缓冲区，无需分配后备缓冲区。

## 与 kernel.elf 的 primitive_gop 的区别

| 特性 | primitive_gop (kernel.elf) | init_gop (init.elf) |
|------|---------------------------|---------------------|
| 后备缓冲区 | 需要分配 DRAM backbuffer | 不需要，直接写 framebuffer |
| 虚拟地址映射 | 需要 KspaceMapMgr 重映射 | 直接使用物理地址 |
| 内存分配 | 需要页分配器 | 无需分配 |
| 适用阶段 | 内核主阶段 | 早期初始化阶段 |
| 性能 | 较高 (WC 缓存) | 一般 (直接写入) |
| 复杂度 | 较高 | 低 |

## 使用方法

### 1. 初始化

```cpp
#include "core_hardwares/init_gop.h"

// 从 UEFI GOP 获取信息
GlobalBasicGraphicInfoType gfx_info = {
    .horizentalResolution = width,
    .verticalResolution = height,
    .pixelFormat = PixelBlueGreenRedReserved8BitPerColor,
    .PixelsPerScanLine = pixels_per_scanline,
    .FrameBufferBase = framebuffer_phys_addr,
    .FrameBufferSize = framebuffer_size
};

// 初始化驱动
KURD_t status = InitGop::Init(&gfx_info);
if (status.result != result_code::SUCCESS) {
    // 处理错误
}
```

### 2. 绘图操作

```cpp
// 画点
InitGop::PutPixel({x, y}, color);

// 画矩形
InitGop::FillRect({x, y}, {width, height}, color);

// 画线
InitGop::DrawHLine({x, y}, length, color);
InitGop::DrawVLine({x, y}, length, color);

// 刷新到屏幕 (可选，因为直接写 framebuffer)
InitGop::Flush();
```

### 3. 图像 Blit

```cpp
GfxImage image = {
    .width = img_width,
    .height = img_height,
    .stride_bytes = stride,
    .pixels = pixel_data
};

InitGop::Blit({x, y}, &image);
```

## 像素格式

使用 BGRA 8888 格式:
- Bit 0-7: Blue
- Bit 8-15: Green  
- Bit 16-23: Red
- Bit 24-31: Reserved/Alpha

示例颜色:
```cpp
constexpr uint32_t COLOR_BLACK   = 0xFF000000;
constexpr uint32_t COLOR_WHITE   = 0xFFFFFFFF;
constexpr uint32_t COLOR_RED     = 0xFFFF0000;
constexpr uint32_t COLOR_GREEN   = 0xFF00FF00;
constexpr uint32_t COLOR_BLUE    = 0xFF0000FF;
```

## API 参考

### 初始化
- `static KURD_t Init(GlobalBasicGraphicInfoType* metainf)` - 初始化 GOP 驱动
- `static bool Ready()` - 检查是否已初始化
- `static const Info GetInfo()` - 获取显示信息
- `static void* FrameBuffer()` - 获取帧缓冲区指针

### 绘图原语
- `static void PutPixel(Vec2i pos, uint32_t color)` - 画点 (带边界检查)
- `static void PutPixelUnsafe(Vec2i pos, uint32_t color)` - 画点 (无检查)
- `static void DrawHLine(Vec2i pos, int len, uint32_t color)` - 画水平线
- `static void DrawVLine(Vec2i pos, int len, uint32_t color)` - 画垂直线
- `static void FillRect(Vec2i pos, Vec2i size, uint32_t color)` - 填充矩形
- `static void MoveUp(...)` - 屏幕上移 (用于滚动)
- `static void Blit(Vec2i pos, const GfxImage* img)` - 图像复制

### 刷新
- `static void Flush()` - 刷新整个屏幕 (执行 sfence)
- `static void FlushRect(Vec2i pos, Vec2i size)` - 刷新局部区域

## 注意事项

1. **直接硬件访问**: init_gop 直接写入 framebuffer，所有修改立即可见
2. **无需刷新**: 由于没有 backbuffer，大部分情况下不需要调用 Flush()
3. **缓存一致性**: Flush() 主要提供 sfence 指令确保缓存一致性
4. **边界检查**: PutPixel 等函数有边界检查，PutPixelUnsafe 没有
5. **线程安全**: 当前实现不是线程安全的，在单核 BSP 初始化阶段使用

## 典型应用场景

- UEFI Shell 中的简单图形演示
- 内核早期初始化的进度显示
- Bootloader 阶段的图形界面
- 调试信息的可视化输出

## 依赖

- `abi/os_error_definitions.h` - 错误码定义
- `efi.h` - UEFI 类型定义
- `util/OS_utils.h` - 内存操作函数 (ksetmem_64, ksystemramcpy)
