#include "os_error_definitions.h"

KURD_t set_result_fail_and_error_level(KURD_t pre)
{
    pre.level=level_code::ERROR;
    pre.result=result_code::FAIL;
    return pre;
}
KURD_t set_fatal_result_level(KURD_t pre)
{
    pre.level=level_code::FATAL;
    pre.result=result_code::FATAL;
    return pre;
}

uint64_t kurd_get_raw(KURD_t kurd)
{
    uint64_t value = 0;
    
    // 按照你注释中的位置将各个字段放入uint64_t中
    // [0:3] result
    value |= (static_cast<uint64_t>(kurd.result) & 0x0F);
    
    // [4:15] reason
    value |= (static_cast<uint64_t>(kurd.reason) & 0x0FFF) << 4;
    
    // [16:23] in_module_location
    value |= (static_cast<uint64_t>(kurd.in_module_location) & 0xFF) << 16;
    
    // [24:31] module_code
    value |= (static_cast<uint64_t>(kurd.module_code) & 0xFF) << 24;
    
    // [32:47] free_to_use
    value |= (static_cast<uint64_t>(kurd.free_to_use) & 0xFFFF) << 32;
    
    // [48:55] event_code
    value |= (static_cast<uint64_t>(kurd.event_code) & 0xFF) << 48;
    
    // [56:58] level
    value |= (static_cast<uint64_t>(kurd.level) & 0x07) << 56;
    
    // [59:63] domain
    value |= (static_cast<uint64_t>(kurd.domain) & 0x1F) << 59;
    
    return value;
}
KURD_t raw_analyze(uint64_t raw) {
    KURD_t kurd = KURD_t();  // 初始化所有字段为0
    
    // 提取result字段 [0:3]
    kurd.result = static_cast<uint16_t>(raw & 0x0F);
    
    // 提取reason字段 [4:15]
    kurd.reason = static_cast<uint16_t>((raw >> 4) & 0x0FFF);
    
    // 提取in_module_location字段 [16:23]
    kurd.in_module_location = static_cast<uint8_t>((raw >> 16) & 0xFF);
    
    // 提取module_code字段 [24:31]
    kurd.module_code = static_cast<uint8_t>((raw >> 24) & 0xFF);
    
    // 提取free_to_use字段 [32:47]
    kurd.free_to_use = static_cast<uint16_t>((raw >> 32) & 0xFFFF);
    
    // 提取event_code字段 [48:55]
    kurd.event_code = static_cast<uint8_t>((raw >> 48) & 0xFF);
    
    // 提取level字段 [56:58]
    kurd.level = static_cast<uint8_t>((raw >> 56) & 0x07);
    
    // 提取domain字段 [59:63]
    kurd.domain = static_cast<uint8_t>((raw >> 59) & 0x1F);
    
    return kurd;
}
bool error_kurd(KURD_t kurd)
{
    return kurd.reason>=result_code::FAIL;
}
