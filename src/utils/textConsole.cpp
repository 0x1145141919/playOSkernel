#include "util/textConsole.h"
#include "util/OS_utils.h"
#include "memory/phygpsmemmgr.h"
#include "16x32AsciiCharacterBitmapSet.h"
#include "util/kout.h"
#include "panic.h"
namespace {
static void textconsole_backend_write(const char* buf, uint64_t len)
{
    if (!buf || len == 0) return;
    for (uint64_t i = 0; i < len; ++i) {
        textconsole_GoP::PutChar(buf[i]);
    }
    //if(GlobalKernelStatus==kernel_state::PANIC)GfxPrim::Flush();
}
    
} // namespace

GfxImage textconsole_GoP::template_character = {};
TextViewport textconsole_GoP::view = {};
TextCursor textconsole_GoP::cursor = {};
const unsigned char* textconsole_GoP::font_bitmap = nullptr;
uint32_t textconsole_GoP::font_color = 0;
uint32_t textconsole_GoP::background_color = 0;
void* textconsole_GoP::glyph_cache = nullptr;
bool textconsole_GoP::ready = false;
uint16_t textconsole_GoP::glyph_index[256] = {};

KURD_t textconsole_GoP::default_kurd()
{
    return KURD_t(
        0,
        0,
        module_code::INFRA,
        infrastructure_location_code::location_code_textconsole_GoP,
        0,
        0,
        err_domain::CORE_MODULE
    );
}

KURD_t textconsole_GoP::default_success()
{
    KURD_t kurd = default_kurd();
    kurd.result = result_code::SUCCESS;
    kurd.level = level_code::INFO;
    return kurd;
}

KURD_t textconsole_GoP::default_fail()
{
    KURD_t kurd = default_kurd();
    kurd = set_result_fail_and_error_level(kurd);
    return kurd;
}

KURD_t textconsole_GoP::default_fatal()
{
    KURD_t kurd = default_kurd();
    kurd = set_fatal_result_level(kurd);
    return kurd;
}

KURD_t textconsole_GoP::Init(
    const unsigned char* font_bitmap_param,
    Vec2i cell_size,
    uint32_t font_color_param,
    uint32_t background_color_param
)
{
    KURD_t success = default_success();
    KURD_t fail = default_fail();
    success.event_code = infrastructure_location_code::textconsole_GoP_events::textconsole_GoP_event_init;
    fail.event_code = infrastructure_location_code::textconsole_GoP_events::textconsole_GoP_event_init;

    if (ready) {
        fail.reason = infrastructure_location_code::textconsole_GoP_events::init_results::fail_reasons::already_init;
        return fail;
    }
    if (font_bitmap_param == nullptr) {
        fail.reason = infrastructure_location_code::textconsole_GoP_events::init_results::fail_reasons::font_bitmap_is_null;
        return fail;
    }
    if (!GfxPrim::Ready()) {
        fail.reason = infrastructure_location_code::textconsole_GoP_events::init_results::fail_reasons::gfx_not_ready;
        return fail;
    }
    if (cell_size.x != 16 || cell_size.y != 32) {
        fail.reason = infrastructure_location_code::textconsole_GoP_events::init_results::fail_reasons::bad_cell_size;
        return fail;
    }

    kio::kout_backend backend = {
        .name = "textconsole_gop",
        .is_masked = 0,
        .reserved = 0,
        .running_stage_write = nullptr,
        .panic_write = &textconsole_backend_write,
        .early_write = &textconsole_backend_write,
    };
    if (kio::bsp_kout.register_backend(backend) == ~0ULL) {
        fail.reason = infrastructure_location_code::textconsole_GoP_events::init_results::fail_reasons::backend_register_fail;
        return fail;
    }

    const GfxPrim::Info info = GfxPrim::GetInfo();
    view.pos = {0, 0};
    view.size = {static_cast<int>(info.width), static_cast<int>(info.height)};
    view.cell = cell_size;
    view.cols = view.size.x / view.cell.x;
    view.rows = view.size.y / view.cell.y;
    if (view.cols <= 0 || view.rows <= 0) {
        fail.reason = infrastructure_location_code::textconsole_GoP_events::init_results::fail_reasons::zero_grid;
        return fail;
    }

    static constexpr uint32_t kFirstPrintable = 0x20;
    static constexpr uint32_t kLastPrintable = 0x7E;
    static constexpr uint32_t kPrintableCount = kLastPrintable - kFirstPrintable + 1;
    static constexpr uint32_t kPlaceholderIndex = 0;
    static constexpr uint32_t kPrintableBaseIndex = 1;
    const uint32_t glyph_count = kPrintableCount + 1;
    const uint64_t glyph_bytes = static_cast<uint64_t>(view.cell.x) * view.cell.y * 4;
    const uint64_t total_bytes = glyph_bytes * glyph_count;
    const uint64_t alloc_bytes = align_up(total_bytes, 0x1000);
    const uint64_t page_count = alloc_bytes / 0x1000;

    KURD_t kurd = empty_kurd;
    glyph_cache = __wrapped_pgs_valloc(&kurd, page_count, KERNEL, 12);
    if (glyph_cache == nullptr || error_kurd(kurd)) {
        fail.reason = infrastructure_location_code::textconsole_GoP_events::init_results::fail_reasons::glyph_cache_alloc_fail;
        return error_kurd(kurd) ? kurd : fail;
    }

    font_bitmap = font_bitmap_param;
    font_color = font_color_param;
    background_color = background_color_param;

    template_character.width = static_cast<uint32_t>(view.cell.x);
    template_character.height = static_cast<uint32_t>(view.cell.y);
    template_character.stride_bytes = static_cast<uint32_t>(view.cell.x) * 4;
    template_character.pixels = glyph_cache;

    for (uint32_t ch = 0; ch < 256; ++ch) {
        if (ch >= kFirstPrintable && ch <= kLastPrintable) {
            glyph_index[ch] = static_cast<uint16_t>(kPrintableBaseIndex + (ch - kFirstPrintable));
        } else {
            glyph_index[ch] = static_cast<uint16_t>(kPlaceholderIndex);
        }
    }

    const uint8_t* bitmap = font_bitmap_param;
    const uint32_t bytes_per_row = 2;
    const uint32_t row_bytes = template_character.stride_bytes;
    const uint64_t color64_bg = (static_cast<uint64_t>(background_color) << 32) | background_color;

    for (uint32_t idx = 0; idx < glyph_count; ++idx) {
        uint32_t render_idx = (idx == kPlaceholderIndex)
            ? 2
            : (kFirstPrintable + (idx - kPrintableBaseIndex));
        uint8_t* glyph_base = static_cast<uint8_t*>(glyph_cache) + glyph_bytes * idx;
        const uint8_t* glyph_bitmap = bitmap + render_idx * view.cell.y * bytes_per_row;

        for (int row = 0; row < view.cell.y; ++row) {
            uint8_t* dst_row = glyph_base + static_cast<uint64_t>(row) * row_bytes;
            ksetmem_64(dst_row, color64_bg, row_bytes);

            uint8_t b0 = glyph_bitmap[row * bytes_per_row + 0];
            uint8_t b1 = glyph_bitmap[row * bytes_per_row + 1];
            for (int bit = 0; bit < 8; ++bit) {
                if (b0 & (0x80 >> bit)) {
                    reinterpret_cast<uint32_t*>(dst_row)[bit] = font_color;
                }
            }
            for (int bit = 0; bit < 8; ++bit) {
                if (b1 & (0x80 >> bit)) {
                    reinterpret_cast<uint32_t*>(dst_row)[8 + bit] = font_color;
                }
            }
        }
    }

    cursor = {0, 0};
    ready = true;
    return success;
}

bool textconsole_GoP::Ready()
{
    return ready;
}

void textconsole_GoP::PutChar(char ch)
{
    if (!ready) return;

    switch (ch) {
        case '\0':
            return;
        case '\n':
            cursor.m = 0;
            cursor.n += 1;
            break;
        case '\r':
            cursor.m = 0;
            break;
        case '\t': {
            static constexpr int kTabWidth = 4;
            cursor.m += kTabWidth;
            break;
        }
        case '\b': {
            if (cursor.m > 0) {
                cursor.m -= 1;
            } else if (cursor.n > 0) {
                cursor.n -= 1;
                cursor.m = view.cols - 1;
            } else {
                break;
            }
            Vec2i pos = {
                view.pos.x + cursor.m * view.cell.x,
                view.pos.y + cursor.n * view.cell.y
            };
            GfxPrim::FillRect(pos, view.cell, background_color);
            break;
        }
        default: {
            unsigned char uch = static_cast<unsigned char>(ch);
            uint16_t idx = glyph_index[uch];
            const uint64_t glyph_bytes = static_cast<uint64_t>(view.cell.x) * view.cell.y * 4;
            template_character.pixels = static_cast<uint8_t*>(glyph_cache) + glyph_bytes * idx;

            Vec2i pos = {
                view.pos.x + cursor.m * view.cell.x,
                view.pos.y + cursor.n * view.cell.y
            };
            GfxPrim::Blit(pos, &template_character);

            cursor.m += 1;
            break;
        }
    }

    if (cursor.m >= view.cols) {
        cursor.m = 0;
        cursor.n += 1;
    }
    if (cursor.n >= view.rows) {
        GfxPrim::MoveUp(view.pos, view.size, view.cell.y, background_color);
        cursor.n = view.rows - 1;
        GfxPrim::Flush(); 
    }
}

void textconsole_GoP::PutString(const char* s)
{
    if (!ready || s == nullptr) return;
    for (const char* p = s; *p; ++p) {
        PutChar(*p);
    }
    //GfxPrim::Flush();
}

void textconsole_GoP::Clear()
{
    if (!ready) return;
    GfxPrim::FillRect(view.pos, view.size, background_color);
    cursor.m = 0;
    cursor.n = 0;
    GfxPrim::Flush();
}
