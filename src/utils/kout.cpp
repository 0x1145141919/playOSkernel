#include "util/kout.h"
#include "Interrupt_system/Interrupt.h"
#include "kcirclebufflogMgr.h"
#include "core_hardwares/VideoDriver.h"
#include "core_hardwares/PortDriver.h"
#include "util/OS_utils.h"
#include "time.h"
#ifdef USER_MODE
#include <cstring> 
#include <unistd.h>
 #endif
kio::kout kio::bsp_kout;
kio::endl kio::kendl;
kio::now_time kio::now;
void (*kio::kout::top_module_KURD_interpreter[256]) (KURD_t info);
void kio::defalut_KURD_module_interpator(KURD_t kurd)
{
    result_t result={
        .kernel_result=kurd
    };
    kio::bsp_kout<<"default_KURD_module_interpator the raw:"<<result.raw<<kendl;
}
void kio::kout::print_numer(
    uint64_t *num_ptr, 
    numer_system_select numer_system, 
    uint8_t len_in_bytes, 
    bool is_signed)
{
    char buf[70];          // 足够覆盖 64bit BIN + 符号
    char tmp_uart_buffer[70];
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
    
    // ===== 反向输出 =====
    for (int i = idx - 1; i >= 0; --i) {
        auto inner_putchar = [&](char c) {
        #ifdef KERNEL_MODE    
        gkcirclebufflogMgr.putsk(&c, 1);
            
            if (is_print_to_polling_uart) {
                tmp_uart_buffer[idx-1-i]=c;
            }
            if (is_print_to_gop) {
                kputchar(c);
            }
            #endif
            #ifdef USER_MODE
            if (is_print_to_stdout) write(1, &c, 1);
            if (is_print_to_stderr) write(2, &c, 1);
            #endif
        };
        inner_putchar(buf[i]);
    }
    tmp_uart_buffer[idx] = '\0';
    #ifdef KERNEL_MODE
    if(is_print_to_polling_uart){
        serial_puts(tmp_uart_buffer);
    }
    #endif
    statistics.total_printed_chars += idx;
}

void kio::kout::__print_level_code(KURD_t value)
{
    switch (value.level)
    {
        case level_code::INVALID:
            *this << "[level:INVALID]";
            break;
        case level_code::INFO:
            *this << "[level:INFO]";
            break;
        case level_code::NOTICE:
            *this << "[level:NOTICE]";
            break;
        case level_code::WARNING:
            *this << "[level:WARNING]";
            break;
        case level_code::ERROR:
            *this << "[level:ERROR]";
            break;
        case level_code::FATAL:
            *this << "[level:FATAL]";
            break;
        default:
            *this << "[level:unknown]";
            break;
    }
}

void kio::kout::__print_module_code(KURD_t value)
{
    switch (value.module_code)
    {
        case module_code::INVALID:
            *this << "[module_code:INVALID]";
            break;
        case module_code::MEMORY:
            *this << "[module_code:MEMORY]";
            break;
        case module_code::SCHEDULER:
            *this << "[module_code:SCHEDULER]";
            break;
        case module_code::INTERRUPT:
            *this << "[module_code:INTERRUPT]";
            break;
        case module_code::FIRMWARE:
            *this << "[module_code:FIRMWARE]";
            break;
        case module_code::VFS:
            *this << "[module_code:VFS]";
            break;
        case module_code::VMM:
            *this << "[module_code:VMM]";
            break;
        case module_code::INFRA:
            *this << "[module_code:INFRA]";
            break;
        case module_code::DEVICES:
            *this << "[module_code:DEVICES]";
            break;
        case module_code::DEVICES_CORE:
            *this << "[module_code:DEVICES_CORE]";
            break;
        case module_code::HARDWARE_DEBUG:
            *this << "[module_code:HARDWARE_DEBUG]";
            break;
        case module_code::USER_KERNEL_ABI:
            *this << "[module_code:USER_KERNEL_ABI]";
            break;
        case module_code::TIME:
            *this << "[module_code:TIME]";
            break;
        case module_code::PANIC:
            *this << "[module_code:PANIC]";
            break;
        default:
            *this << "[module_code:unknown]";
            break;
    }
}

void kio::kout::__print_result_code(KURD_t value)
{
    switch (value.result)
    {
        case result_code::SUCCESS:
            *this << "[result:SUCCESS]";
            break;
        case result_code::SUCCESS_BUT_SIDE_EFFECT:
            *this << "[result:SUCCESS_BUT_SIDE_EFFECT]";
            break;
        case result_code::PARTIAL_SUCCESS:
            *this << "[result:PARTIAL_SUCCESS]";
            break;
        case result_code::FAIL:
            *this << "[result:FAIL]";
            break;
        case result_code::RETRY:
            *this << "[result:RETRY]";
            break;
        case result_code::FATAL:
            *this << "[result:FATAL]";
            break;
        default:
            *this << "[result:unknown]";
            break;
    }
}

void kio::kout::__print_err_domain(KURD_t value)
{
    switch (value.domain)
    {
        case err_domain::INVALID:
            *this << "[err_domain:INVALID]";
            break;
        case err_domain::CORE_MODULE:
            *this << "[err_domain:CORE_MODULE]";
            break;
        case err_domain::ARCH:
            *this << "[err_domain:ARCH]";
            break;
        case err_domain::USER:
            *this << "[err_domain:USER]";
            break;
        case err_domain::HYPERVISOR:
            *this << "[err_domain:HYPERVISOR]";
            break;
        case err_domain::OUT_MODULES:
            *this << "[err_domain:OUT_MODULES]";
            break;
        case err_domain::FILE_SYSTEM:
            *this << "[err_domain:FILE_SYSTEM]";
            break;
        case err_domain::HARDWARE:
            *this << "[err_domain:HARDWARE]";
            break;
        default:
            *this << "[err_domain:unknown]";
            break;
    }
}

kio::kout &kio::kout::operator<<(KURD_t info)
{
    __print_level_code(info);
    __print_result_code(info);
    __print_err_domain(info);
    __print_module_code(info);
    top_module_KURD_interpreter[info.module_code](info);
    return *this;
}
kio::kout &kio::kout::operator<<(const char *str)
    
{
    #ifdef KERNEL_MODE
    gkcirclebufflogMgr.putsk((char *)str,MAX_STRING_LEN);
    
    if (is_print_to_polling_uart) {
        serial_puts(str);
    }
    if(is_print_to_gop) {
        kputsascii(str);
    }
    #endif
    #ifdef USER_MODE
    int strlen = strlen_in_kernel(str);
    if (is_print_to_stdout) write(1, str, strlen);

    if (is_print_to_stderr) write(2, str, strlen);
    #endif
    statistics.calls_str++;
    //别看这个写法很弱智，但是KERNEL_MODE和USER_MODE是用的两套strlen虽然长得一样但是无宏包裹下就不存在
    #ifdef KERNEL_MODE
    statistics.total_printed_chars += strlen_in_kernel(str);
    #endif
    #ifdef USER_MODE
    statistics.total_printed_chars += strlen;
    #endif
    return *this;
}

kio::kout &kio::kout::operator<<(char c)
{
    #ifdef KERNEL_MODE
    gkcirclebufflogMgr.putsk(&c, 1);
    
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
    for(int i = 0; i < 256; i++){
        top_module_KURD_interpreter[i]=defalut_KURD_module_interpator;
    }

    #ifdef KERNEL_MODE
    is_print_to_polling_uart = true;
    is_print_to_gop = true;
    #endif
    #ifdef USER_MODE
    is_print_to_stdout = true;
    is_print_to_stderr = false;
    #endif
}
kio::kout &kio::kout::operator<<(const void *ptr)
{
    #ifdef KERNEL_MODE
    gkcirclebufflogMgr.putsk("0x", 2);
    
    if (is_print_to_polling_uart) {
        serial_puts("0x");
    }
    if (is_print_to_gop) {
        kputsascii("0x");
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
    #ifdef KERNEL_MODE 
    
    if(time::hardware_time::get_if_hpet_initialized()){
        *this<<'[';
        miusecond_time_stamp_t stamp=time::hardware_time::get_stamp();
        print_numer(&stamp, DEC, 8, false);
        *this<<']';
    }else{
        *this<<'<';
        miusecond_time_stamp_t stamp=time::hardware_time::get_stamp();
        print_numer(&stamp, DEC, 8, false);
        *this<<'>';
    }
    #endif
    #ifdef USER_MODE
    *this<<"<tsc=";
    uint64_t tsc=rdtsc();
    print_numer(&tsc, DEC, 8, false);
    *this<<'>';
    #endif
    return *this;
}

kio::kout &kio::kout::operator<<(endl end)
{
    statistics.explicit_endl++;
    *this<<'\n';
    return *this;
}