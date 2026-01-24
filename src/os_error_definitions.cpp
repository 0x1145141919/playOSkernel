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

bool error_kurd(KURD_t kurd)
{
    return kurd.reason>=result_code::FAIL;
}
