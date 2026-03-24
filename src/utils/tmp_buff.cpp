#include "util/kout.h"

namespace kio {

tmp_buff::tmp_buff()
    : num_sys(numer_system_select::DEC),
      entry_top(0)
{
    ksetmem_8(entry_array, 0, sizeof(entry_array));
}

tmp_buff::~tmp_buff()
{
    ksetmem_8(entry_array, 0, sizeof(entry_array));
    entry_top = 0;
    num_sys = numer_system_select::DEC;
}

tmp_buff& tmp_buff::operator<<(KURD_t info)
{
    if (entry_top >= entry_max) return *this;
    entry& e = entry_array[entry_top++];
    e.entry_type = entry_type_t::KURD;
    e.data.kurd = info;
    return *this;
}

tmp_buff& tmp_buff::operator<<(const char* str)
{
    if (entry_top >= entry_max) return *this;
    entry& e = entry_array[entry_top++];
    e.entry_type = entry_type_t::str;
    e.data.str = const_cast<char*>(str);
    e.str_len = str ? (uint32_t)strlen_in_kernel(str) : 0;
    return *this;
}

tmp_buff& tmp_buff::operator<<(char c)
{
    if (entry_top >= entry_max) return *this;
    entry& e = entry_array[entry_top++];
    e.entry_type = entry_type_t::character;
    e.data.character = c;
    return *this;
}

tmp_buff& tmp_buff::operator<<(const void* ptr)
{
    if (entry_top >= entry_max) return *this;
    entry& e = entry_array[entry_top++];
    e.entry_type = entry_type_t::num;
    e.num_type = num_format_t::u64;
    e.num_sys = numer_system_select::HEX;
    e.data.data = reinterpret_cast<uint64_t>(ptr);
    return *this;
}

tmp_buff& tmp_buff::operator<<(uint64_t num)
{
    if (entry_top >= entry_max) return *this;
    entry& e = entry_array[entry_top++];
    e.entry_type = entry_type_t::num;
    e.num_type = num_format_t::u64;
    e.num_sys = num_sys;
    e.data.data = num;
    return *this;
}

tmp_buff& tmp_buff::operator<<(int64_t num)
{
    if (entry_top >= entry_max) return *this;
    entry& e = entry_array[entry_top++];
    e.entry_type = entry_type_t::num;
    e.num_type = num_format_t::s64;
    e.num_sys = num_sys;
    e.data.data = static_cast<uint64_t>(num);
    return *this;
}

tmp_buff& tmp_buff::operator<<(uint32_t num)
{
    if (entry_top >= entry_max) return *this;
    entry& e = entry_array[entry_top++];
    e.entry_type = entry_type_t::num;
    e.num_type = num_format_t::u32;
    e.num_sys = num_sys;
    e.data.data = num;
    return *this;
}

tmp_buff& tmp_buff::operator<<(int32_t num)
{
    if (entry_top >= entry_max) return *this;
    entry& e = entry_array[entry_top++];
    e.entry_type = entry_type_t::num;
    e.num_type = num_format_t::s32;
    e.num_sys = num_sys;
    e.data.data = static_cast<uint64_t>(num);
    return *this;
}

tmp_buff& tmp_buff::operator<<(now_time time)
{
    (void)time;
    if (entry_top >= entry_max) return *this;
    entry& e = entry_array[entry_top++];
    e.entry_type = entry_type_t::time;
    return *this;
}

tmp_buff& tmp_buff::operator<<(endl end)
{
    (void)end;
    return (*this << '\n');
}

tmp_buff& tmp_buff::operator<<(uint16_t num)
{
    if (entry_top >= entry_max) return *this;
    entry& e = entry_array[entry_top++];
    e.entry_type = entry_type_t::num;
    e.num_type = num_format_t::u16;
    e.num_sys = num_sys;
    e.data.data = num;
    return *this;
}

tmp_buff& tmp_buff::operator<<(int16_t num)
{
    if (entry_top >= entry_max) return *this;
    entry& e = entry_array[entry_top++];
    e.entry_type = entry_type_t::num;
    e.num_type = num_format_t::s16;
    e.num_sys = num_sys;
    e.data.data = static_cast<uint64_t>(num);
    return *this;
}

tmp_buff& tmp_buff::operator<<(uint8_t num)
{
    if (entry_top >= entry_max) return *this;
    entry& e = entry_array[entry_top++];
    e.entry_type = entry_type_t::num;
    e.num_type = num_format_t::u8;
    e.num_sys = num_sys;
    e.data.data = num;
    return *this;
}

tmp_buff& tmp_buff::operator<<(int8_t num)
{
    if (entry_top >= entry_max) return *this;
    entry& e = entry_array[entry_top++];
    e.entry_type = entry_type_t::num;
    e.num_type = num_format_t::s8;
    e.num_sys = num_sys;
    e.data.data = static_cast<uint64_t>(num);
    return *this;
}

tmp_buff& tmp_buff::operator<<(numer_system_select radix)
{
    num_sys = radix;
    return *this;
}
bool kio::tmp_buff::is_full()
{
    return entry_top==entry_max;
}
} // namespace kio
