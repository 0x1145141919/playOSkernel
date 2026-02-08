#pragma once
#include "stdint.h"
#include "core_hardwares/primitive_gop.h"
struct TextViewport {
    Vec2i pos;      // 文字区域左上角（像素）
    Vec2i size;     // 文字区域大小（像素）
    Vec2i cell;     // 字符宽高（像素）
    int cols;       // size.x / cell.x
    int rows;       // size.y / cell.y
};
namespace infrastructure_location_code{ //bitmap和Ktemplate系列不能也不适合进入这个位置系统，数据结构必须实例化才有意义
    constexpr uint8_t location_code_textconsole_GoP=4;
    namespace textconsole_GoP_events{
        constexpr uint8_t textconsole_GoP_event_init=0;
        namespace init_results{
            namespace fail_reasons{
                constexpr uint8_t font_bitmap_is_null=0;
                constexpr uint8_t gfx_not_ready=1;
                constexpr uint8_t bad_cell_size=2;
                constexpr uint8_t zero_grid=3;
                constexpr uint8_t already_init=4;
                constexpr uint8_t glyph_cache_alloc_fail=5;
                constexpr uint8_t backend_register_fail=6;
            }
        }
    }
}
struct TextCursor {
    int m;          // 列
    int n;          // 行
};
class textconsole_GoP {
public:
    // 唯一入口
    static KURD_t Init(//屏幕分辨率大小查询GfxPrim，并且起始向量默认为（0,0）
        const unsigned char* font_bitmap,
        Vec2i cell_size,
        uint32_t font_color,
        uint32_t background_color
    );

    static bool Ready();
    static void PutChar(char ch);
    static void PutString(const char* s);
    static void Clear();

    static void PutChar_runtime(char ch);
    static void PutString_runtime(const char* s);
    static void Clear_runtime();
private:
    static GfxImage template_character;
    static TextViewport view;
    static TextCursor cursor;
    static const unsigned char* font_bitmap;
    static uint32_t font_color;
    static uint32_t background_color;
    static void* glyph_cache;
    static bool ready;
    static uint16_t glyph_index[256];
    static KURD_t default_kurd();
    static KURD_t default_success();
    static KURD_t default_fail();
    static KURD_t default_fatal();
};
