/**
 * 这是内核日志环形缓冲区模块头文件，负责把启动时的调试信息保存下来，易于启动后观察，gdb调试时查看。
 */
#pragma once
#include "stdint.h"
class kcirclebufflogMgr
{
private:
    char *buff;
    uint64_t buffSize;
    uint64_t tailIndex;
public:
    kcirclebufflogMgr(/* args */);
    void putsk(char *str,uint64_t len_in_bytes);
    void putsk(char *str);  // 自动计算以null结尾的字符串长度
    ~kcirclebufflogMgr();
};


