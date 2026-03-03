#include "util/textConsole.h"
#include "util/OS_utils.h"
#include "memory/phygpsmemmgr.h"
#include "16x32AsciiCharacterBitmapSet.h"
#include "util/kout.h"
#include "Scheduler/per_processor_scheduler.h"
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

static void textconsole_backend_running_write(const char* buf, uint64_t len)
{
    if (!buf || len == 0) return;
    if (!textconsole_GoP::RuntimeSubmitString(buf, len, false)) {
        textconsole_backend_write(buf, len);
    }
}

static void textconsole_backend_running_num(uint64_t raw, num_format_t format, numer_system_select radix)
{
    if (textconsole_GoP::RuntimeSubmitNum(raw, format, radix, false)) {
        return;
    }
    char out[70];
    uint64_t n = format_num_to_buffer(out, raw, format, radix);
    if (n > 0) {
        textconsole_backend_write(out, n);
    }
}
    
} // namespace

GfxImage textconsole_GoP::template_character = {};
TextViewport textconsole_GoP::view = {};
TextCursor textconsole_GoP::cursor = {};
const unsigned char* textconsole_GoP::font_bitmap = nullptr;
uint32_t textconsole_GoP::font_color = 0;
uint32_t textconsole_GoP::background_color = 0;
void* textconsole_GoP::glyph_cache = nullptr;
task* textconsole_GoP::runtime_service_task = nullptr;
spintrylock_cpp_t textconsole_GoP::runtime_service_create_lock = {};
tc_ring textconsole_GoP::runtime_ring = {};
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
        .running_stage_write = &textconsole_backend_running_write,
        .running_stage_num = &textconsole_backend_running_num,
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
    runtime_ring.head = 0;
    runtime_ring.tail = 0;
    runtime_ring.seq_gen = 0;
    runtime_ring.drop_count = 0;
    runtime_ring.push_count = 0;
    runtime_ring.pop_count = 0;
    runtime_ring.service_thread_sleeping = false;
    runtime_service_task = nullptr;
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

bool textconsole_GoP::RuntimeInitServiceThread()
{
    if (!ready) return false;
    runtime_service_create_lock.lock();
    if (runtime_service_task) {
        runtime_service_create_lock.unlock();
        return true;
    }

    auto* scheduler = reinterpret_cast<per_processor_scheduler*>(read_gs_u64(SCHEDULER_PRIVATE_GS_INDEX));
    if (!scheduler) {
        runtime_service_create_lock.unlock();
        return false;
    }
    per_processor_scheduler::create_kthread_param param = per_processor_scheduler::default_kthread_param;
    param.is_soon_ready = 1;
    task* created = scheduler->create_kthread(&textconsole_GoP::RuntimeServiceThreadMain, nullptr, &param);
    if (!created || !success_all_kurd(param.result_kurd)) {
        runtime_service_create_lock.unlock();
        return false;
    }
    runtime_service_task = created;
    runtime_service_create_lock.unlock();
    return true;
}

void textconsole_GoP::RuntimeWakeServiceThread()
{
    if (!runtime_service_task || !all_scheduler_ptr) return;
    uint32_t owner = runtime_service_task->get_belonged_processor_id();
    per_processor_scheduler* owner_scheduler = all_scheduler_ptr[owner];
    if (!owner_scheduler) return;
    KURD_t kurd = owner_scheduler->task_set_ready(runtime_service_task);
    (void)kurd;
}

bool textconsole_GoP::RuntimeSubmitString(const char* s, uint64_t len, bool urgent)
{
    if (!ready || !runtime_service_task || !s || len == 0) return false;

    runtime_ring.lock.lock();
    const uint32_t next_tail = (runtime_ring.tail + 1) & (TC_RING_CAP - 1);
    if (next_tail == runtime_ring.head) {
        runtime_ring.drop_count++;
        runtime_ring.lock.unlock();
        return false;
    }
    tc_slot& slot = runtime_ring.slots[runtime_ring.tail];
    slot.head.type = tc_msg_type::string;
    slot.head.reserved = 0;
    slot.head.flags = urgent ? tc_msg_flag_urgent : tc_msg_flag_none;
    slot.head.producer_cpu = fast_get_processor_id();
    slot.head.seq = runtime_ring.seq_gen++;
    slot.payload.s.string = s;
    slot.payload.s.len = len;
    runtime_ring.tail = next_tail;
    runtime_ring.push_count++;
    const bool should_wake = runtime_ring.service_thread_sleeping;
    runtime_ring.service_thread_sleeping = false;
    runtime_ring.lock.unlock();

    if (should_wake) RuntimeWakeServiceThread();
    return true;
}

bool textconsole_GoP::RuntimeSubmitChar(char ch, bool urgent)
{
    if (!ready || !runtime_service_task) return false;

    runtime_ring.lock.lock();
    const uint32_t next_tail = (runtime_ring.tail + 1) & (TC_RING_CAP - 1);
    if (next_tail == runtime_ring.head) {
        runtime_ring.drop_count++;
        runtime_ring.lock.unlock();
        return false;
    }
    tc_slot& slot = runtime_ring.slots[runtime_ring.tail];
    slot.head.type = tc_msg_type::single_character;
    slot.head.reserved = 0;
    slot.head.flags = urgent ? tc_msg_flag_urgent : tc_msg_flag_none;
    slot.head.producer_cpu = fast_get_processor_id();
    slot.head.seq = runtime_ring.seq_gen++;
    slot.payload.c.ch = ch;
    runtime_ring.tail = next_tail;
    runtime_ring.push_count++;
    const bool should_wake = runtime_ring.service_thread_sleeping;
    runtime_ring.service_thread_sleeping = false;
    runtime_ring.lock.unlock();

    if (should_wake) RuntimeWakeServiceThread();
    return true;
}

bool textconsole_GoP::RuntimeSubmitNum(uint64_t raw, num_format_t format, numer_system_select radix, bool urgent)
{
    if (!ready || !runtime_service_task) return false;

    runtime_ring.lock.lock();
    const uint32_t next_tail = (runtime_ring.tail + 1) & (TC_RING_CAP - 1);
    if (next_tail == runtime_ring.head) {
        runtime_ring.drop_count++;
        runtime_ring.lock.unlock();
        return false;
    }
    tc_slot& slot = runtime_ring.slots[runtime_ring.tail];
    slot.head.type = tc_msg_type::num;
    slot.head.reserved = 0;
    slot.head.flags = urgent ? tc_msg_flag_urgent : tc_msg_flag_none;
    slot.head.producer_cpu = fast_get_processor_id();
    slot.head.seq = runtime_ring.seq_gen++;
    slot.payload.n.num_raw = raw;
    slot.payload.n.format = format;
    slot.payload.n.radix = radix;
    runtime_ring.tail = next_tail;
    runtime_ring.push_count++;
    const bool should_wake = runtime_ring.service_thread_sleeping;
    runtime_ring.service_thread_sleeping = false;
    runtime_ring.lock.unlock();

    if (should_wake) RuntimeWakeServiceThread();
    return true;
}

bool textconsole_GoP::RuntimeSubmitFlush(bool urgent)
{
    if (!ready || !runtime_service_task) return false;

    runtime_ring.lock.lock();
    const uint32_t next_tail = (runtime_ring.tail + 1) & (TC_RING_CAP - 1);
    if (next_tail == runtime_ring.head) {
        runtime_ring.drop_count++;
        runtime_ring.lock.unlock();
        return false;
    }
    tc_slot& slot = runtime_ring.slots[runtime_ring.tail];
    slot.head.type = tc_msg_type::flush;
    slot.head.reserved = 0;
    slot.head.flags = urgent ? tc_msg_flag_urgent : tc_msg_flag_none;
    slot.head.producer_cpu = fast_get_processor_id();
    slot.head.seq = runtime_ring.seq_gen++;
    runtime_ring.tail = next_tail;
    runtime_ring.push_count++;
    const bool should_wake = runtime_ring.service_thread_sleeping;
    runtime_ring.service_thread_sleeping = false;
    runtime_ring.lock.unlock();

    if (should_wake) RuntimeWakeServiceThread();
    return true;
}

void textconsole_GoP::RuntimeServiceThreadMain(void* data)
{
    (void)data;
    tc_service_local_batch local_batch{};
    char num_buf[70];

    auto next_index = [](uint32_t idx) -> uint32_t {
        return (idx + 1) & (TC_RING_CAP - 1);
    };
    for (;;) {
        local_batch.count = 0;
        runtime_ring.lock.lock();
        while (runtime_ring.head != runtime_ring.tail &&
               local_batch.count < TC_SERVICE_POP_BATCH) {
            local_batch.items[local_batch.count] = runtime_ring.slots[runtime_ring.head];
            runtime_ring.head = next_index(runtime_ring.head);
            runtime_ring.pop_count++;
            local_batch.count++;
        }
        if (local_batch.count == 0) {
            runtime_ring.service_thread_sleeping = true;
        }
        runtime_ring.lock.unlock();

        if (local_batch.count == 0) {
            kthread_self_blocked(task_blocked_reason_t::no_job);
            continue;
        }

        bool need_flush = false;
        for (uint32_t i = 0; i < local_batch.count; ++i) {
            const tc_slot& slot = local_batch.items[i];
            switch (slot.head.type) {
                case tc_msg_type::string:
                    for (uint64_t j = 0; j < slot.payload.s.len; ++j) {
                        PutChar(slot.payload.s.string[j]);
                    }
                    break;
                case tc_msg_type::single_character:
                    PutChar(slot.payload.c.ch);
                    break;
                case tc_msg_type::num: {
                    const uint64_t n = format_num_to_buffer(
                        num_buf,
                        slot.payload.n.num_raw,
                        slot.payload.n.format,
                        slot.payload.n.radix
                    );
                    for (uint64_t j = 0; j < n; ++j) {
                        PutChar(num_buf[j]);
                    }
                    break;
                }
                case tc_msg_type::flush:
                    need_flush = true;
                    break;
                default:
                    break;
            }
            if (slot.head.flags & tc_msg_flag_urgent) {
                need_flush = true;
            }
        }
        if (need_flush) {
            GfxPrim::Flush();
        }
    }
}
