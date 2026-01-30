#pragma once
#include "os_error_definitions.h"
namespace kio
{
class now_time
{
    private:  
    public:now_time(){};
};
class endl
{
    private:
    public:endl(){};
};

class kout
{
    public:
    struct kout_statistics_t
    {
    uint64_t total_printed_chars;
        //还有各种入口统计处,主要是对不同类型的<<重载调用次数的统计
         // ===== 输出类型调用统计 =====
    uint64_t calls_str;             // operator<<(const char*)
    uint64_t calls_char;            // operator<<(char)
    uint64_t calls_ptr;             // operator<<(const void*)

    uint64_t calls_u8;
    uint64_t calls_s8;
    uint64_t calls_u16;
    uint64_t calls_s16;
    uint64_t calls_u32;
    uint64_t calls_s32;
    uint64_t calls_u64;
    uint64_t calls_s64;
    uint64_t calls_KURD;
    uint64_t calls_now_time;        // operator<<(now_time)

    // ===== 控制/状态类调用 =====
    uint64_t calls_shift_bin;
    uint64_t calls_shift_dec;
    uint64_t calls_shift_hex;

    // ===== 行为统计 =====
    uint64_t explicit_endl;         // 通过 endl / kendl 结束的行

    // ===== 未来预留 =====
    //uint64_t reserved[8];
    };
    protected:
    #ifdef KERNEL_MODE
    bool is_print_to_polling_uart;
    bool is_print_to_gop;
    #endif
    
    #ifdef USER_MODE
    bool is_print_to_stdout;
    bool is_print_to_stderr;
    #endif
    kout_statistics_t statistics;
    enum numer_system_select : uint8_t
    {
        BIN,
        DEC,
        HEX
    };
    numer_system_select curr_numer_system;
    static constexpr char hex_chars[16]={
        '0','1','2','3','4','5','6','7',
        '8','9','A','B','C','D','E','F'
    };
    // 打印KURD_t中level字段对应的字符串表示
    void __print_level_code(KURD_t value);
    // 打印KURD_t中module_code字段对应的字符串表示
    void __print_module_code(KURD_t value);
    // 打印KURD_t中result_code字段对应的字符串表示
    void __print_result_code(KURD_t value);
    // 打印KURD_t中err_domain字段对应的字符串表示
    void __print_err_domain(KURD_t value);
    static void (*top_module_KURD_interpreter[256])(KURD_t info);
    /**
     * @brief 输出数字
     * 
     * @param num_ptr 数字的指针
     * @param numer_system 数字系统,准确说是进制，比如二进制，十进制，十六进制
     * @param len_in_bytes 数字的字节长度，合法长度只有1,2,4,8
     * @param is_signed 是否是带符号的数字，只有在DEC状态下判断是否是复数，然后进行打印，BIN/HEX统一当作无符号数打印
     */
    
    void print_numer(
        uint64_t*num_ptr,
        numer_system_select numer_system,
        uint8_t len_in_bytes,
        bool is_signed);
    static constexpr uint16_t MAX_STRING_LEN=4096;
    public:
    kout& operator<<(KURD_t info);
    kout& operator<<(const char* str);
    kout& operator<<(char c);
    kout& operator<<(const void* ptr);
    kout& operator<<(uint64_t num);
    kout& operator<<(int64_t num);
    kout& operator<<(uint32_t num);
    kout& operator<<(int32_t num);
    kout& operator<<(now_time time);
    kout& operator<<(endl end);
    kout& operator<<(uint16_t num);
    kout& operator<<(int16_t num);
    kout& operator<<(uint8_t num);
    kout& operator<<(int8_t num);
    void shift_bin();
    void shift_dec();
    void shift_hex();
    kout_statistics_t get_statistics();
    void Init();
    
    #ifdef KERNEL_MODE
    void enable_polling_uart_output(){is_print_to_polling_uart=true;};
    void disable_polling_uart_output(){is_print_to_polling_uart=false;};
    void enable_gop_output(){is_print_to_gop=true;};
    void disable_gop_output(){is_print_to_gop=false;};
    #endif
    ~kout(){};
};
extern kout bsp_kout;
extern endl kendl;
extern now_time now;
void defalut_KURD_module_interpator(KURD_t kurd);
}