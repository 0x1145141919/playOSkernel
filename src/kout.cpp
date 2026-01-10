#include "kout.h"
#include "kcirclebufflogMgr.h"
#include "VideoDriver.h"
#include "PortDriver.h"
#include "util/OS_utils.h"
#include "time.h"
#ifdef USER_MODE
#include <cstring> 
#include <unistd.h>
 #endif
kio::kout kio::bsp_kout;
kio::endl kio::kendl;
kio::now_time kio::now;
void kio::kout::print_numer(
    uint64_t *num_ptr, 
    numer_system_select numer_system, 
    uint8_t len_in_bytes, 
    bool is_signed)
{
    char buf[70];          // 足够覆盖 64bit BIN + 符号
    uint32_t idx = 0;

    uint64_t value = 0;

    // ===== 加载数值 =====
    switch (len_in_bytes) {
        case 1: value = *(uint8_t*)num_ptr; break;
        case 2: value = *(uint16_t*)num_ptr; break;
        case 4: value = *(uint32_t*)num_ptr; break;
        case 8: value = *(uint64_t*)num_ptr; break;
        default:
            return;
    }

    // ===== 处理 DEC 下的符号 =====
    bool negative = false;
    if (numer_system == DEC && is_signed) {
        int64_t signed_val = 0;
        switch (len_in_bytes) {
            case 1: signed_val = *(int8_t*)num_ptr; break;
            case 2: signed_val = *(int16_t*)num_ptr; break;
            case 4: signed_val = *(int32_t*)num_ptr; break;
            case 8: signed_val = *(int64_t*)num_ptr; break;
        }
        if (signed_val < 0) {
            negative = true;
            value = (uint64_t)(-signed_val);
        }
    }

    // ===== 特殊情况：0 =====
    if (value == 0) {
        buf[idx++] = '0';
    } else {
        uint32_t base = 10;
        if (numer_system == BIN) base = 2;
        else if (numer_system == HEX) base = 16;

        while (value > 0) {
            uint32_t digit = value % base;
            value /= base;

            if (base == 16)
                buf[idx++] = hex_chars[digit];
            else
                buf[idx++] = '0' + digit;
        }
    }

    // ===== 负号 =====
    if (negative) {
        buf[idx++] = '-';
    }
    auto inner_putchar = [&](char c) {
            gkcirclebufflogMgr.putsk(&c, 1);
            #ifdef KERNEL_MODE
            if (is_print_to_polling_uart) {
                serial_putc(c);
            }
            if (is_print_to_gop) {
                kputcharSecure(c);
            }
            #endif
            #ifdef USER_MODE
            if (is_print_to_stdout) write(1, &c, 1);
            if (is_print_to_stderr) write(2, &c, 1);
            #endif
        };
    // ===== 反向输出 =====
    for (int i = idx - 1; i >= 0; --i) {
        inner_putchar(buf[i]);
    }
    statistics.total_printed_chars += idx;
}

kio::kout &kio::kout::operator<<(const char *str)
{
    gkcirclebufflogMgr.putsk((char *)str,MAX_STRING_LEN);
    #ifdef KERNEL_MODE
    if (is_print_to_polling_uart) {
        serial_puts(str);
    }
    if(is_print_to_gop) {
        kputsSecure(str);
    }
    #endif
    #ifdef USER_MODE
    if (is_print_to_stdout) write(1, str, strlen(str));
    if (is_print_to_stderr) write(2, str, strlen(str));
    #endif
    statistics.calls_str++;
    //别看这个写法很弱智，但是KERNEL_MODE和USER_MODE是用的两套strlen虽然长得一样但是无宏包裹下就不存在
    #ifdef KERNEL_MODE
    statistics.total_printed_chars += strlen(str);
    #endif
    #ifdef USER_MODE
    statistics.total_printed_chars += strlen(str);
    #endif
    return *this;
}

kio::kout &kio::kout::operator<<(char c)
{
    gkcirclebufflogMgr.putsk(&c, 1);
    #ifdef KERNEL_MODE
    if (is_print_to_polling_uart) {
        serial_putc(c);
    }
    if (is_print_to_gop) {
        kputcharSecure(c);
    }
    #endif
    #ifdef USER_MODE
    if (is_print_to_stdout) write(1, &c, 1);
    if (is_print_to_stderr) write(2, &c, 1);
    #endif
    statistics.calls_char++;
    statistics.total_printed_chars++;
    return *this;
}

void kio::kout::Init()
{
    setmem(&statistics,sizeof(statistics),0);
    #ifdef KERNEL_MODE
    is_print_to_polling_uart = true;
    is_print_to_gop = false;
    #endif
    #ifdef USER_MODE
    is_print_to_stdout = true;
    is_print_to_stderr = false;
    #endif
}
kio::kout &kio::kout::operator<<(const void *ptr)
{
    gkcirclebufflogMgr.putsk("0x", 2);
    #ifdef KERNEL_MODE
    if (is_print_to_polling_uart) {
        serial_puts("0x");
    }
    if (is_print_to_gop) {
        kputsSecure("0x");
    }
    #endif
    #ifdef USER_MODE
    if (is_print_to_stdout) write(1, "0x", 2);
    if (is_print_to_stderr) write(2, "0x", 2);
    #endif
    uint64_t address = (uint64_t)ptr;
    print_numer(&address, HEX, sizeof(void *), false);
    statistics.calls_ptr++;
    return *this;
}
kio::kout &kio::kout::operator<<(uint64_t num)
{
    statistics.calls_u64++;
    print_numer(&num, curr_numer_system, 8, false);
    return *this;
}

kio::kout &kio::kout::operator<<(int64_t num)
{
    statistics.calls_s64++;
    print_numer((uint64_t*)&num, curr_numer_system, 8, true);
    return *this;
}

kio::kout &kio::kout::operator<<(uint32_t num)
{
    statistics.calls_u32++;
    print_numer((uint64_t*)&num, curr_numer_system, 4, false);
    return *this;
}

kio::kout &kio::kout::operator<<(int32_t num)
{
    statistics.calls_s32++;
    print_numer((uint64_t*)&num, curr_numer_system, 4, true);
    return *this;
}

kio::kout &kio::kout::operator<<(uint16_t num)
{
    statistics.calls_u16++;
    print_numer((uint64_t*)&num, curr_numer_system, 2, false);
    return *this;
}

kio::kout &kio::kout::operator<<(int16_t num)
{
    statistics.calls_s16++;
    print_numer((uint64_t*)&num, curr_numer_system, 2, true);
    return *this;
}

kio::kout &kio::kout::operator<<(uint8_t num)
{
    statistics.calls_u8++;
    print_numer((uint64_t*)&num, curr_numer_system, 1, false);
    return *this;
}

kio::kout &kio::kout::operator<<(int8_t num)
{
    statistics.calls_s8++;
    print_numer((uint64_t*)&num, curr_numer_system, 1, true);
    return *this;
}

void kio::kout::shift_bin()
{
    curr_numer_system = BIN;
}

void kio::kout::shift_dec()
{
    curr_numer_system = DEC;
}

void kio::kout::shift_hex()
{
    curr_numer_system = HEX;
}

kio::kout::kout_statistics_t kio::kout::get_statistics()
{
    return statistics;
}

kio::kout &kio::kout::operator<<(now_time time)
{
    statistics.calls_now_time++;
    if(time::hardware_time::get_if_hpet_initialized()){
        *this<<'[';
        time::miusecond_time_stamp_t stamp=time::hardware_time::get_stamp(
            *time::bsp_token
        );//先暂时用bsp_token，smp下每个核心用自己的token
        print_numer(&stamp, DEC, 8, false);

        *this<<']';
    }else{
        *this<<'<';
        time::miusecond_time_stamp_t stamp=time::hardware_time::get_stamp(
            *time::bsp_token
        );
        print_numer(&stamp, DEC, 8, false);
        *this<<'>';
    }
    return *this;
}

kio::kout &kio::kout::operator<<(endl end)
{
    statistics.explicit_endl++;
    *this<<'\n';
    return *this;
}