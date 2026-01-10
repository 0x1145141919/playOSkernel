// 实现128位整数除法函数，用于支持__uint128_t运算
extern "C" {

// 无符号128位整数除法
unsigned __int128 __udivti3(unsigned __int128 n, unsigned __int128 d) {
    if (d == 0) {
        // 除零情况，理论上不应该发生，但为了安全
        return 0;
    }
    
    if (d > n) {
        return 0;
    }
    
    if (d == 1) {
        return n;
    }
    
    // 如果除数只有64位，可以使用更简单的方法
    if (d <= 0xFFFFFFFFFFFFFFFFULL) {
        // 将n分解为高64位和低64位
        unsigned long long n_high = (unsigned long long)(n >> 64);
        unsigned long long n_low = (unsigned long long)n;
        unsigned long long d_low = (unsigned long long)d;
        
        if (n_high == 0) {
            // 如果高64位为0，直接使用64位除法
            return n_low / d_low;
        }
        
        // 高64位不为0的情况
        // 先计算高64位的商
        unsigned long long q_high = n_high / d_low;
        // 计算余数
        unsigned long long r = n_high % d_low;
        // 将余数与低64位组合
        unsigned __int128 temp = ((unsigned __int128)r << 64) | n_low;
        unsigned long long q_low = (unsigned long long)(temp / d_low);
        
        return ((unsigned __int128)q_high << 64) | q_low;
    }
    
    // 完整128位除法算法
    unsigned __int128 q = 0;
    unsigned __int128 r = 0;
    
    // 从最高位开始处理
    for (int i = 127; i >= 0; i--) {
        r <<= 1;  // 左移余数
        // 取出n的第i位
        if ((n >> i) & 1) {
            r |= 1;
        }
        
        if (r >= d) {
            r -= d;
            q |= ((unsigned __int128)1 << i);
        }
    }
    
    return q;
}

// 无符号128位整数取模
unsigned __int128 __umodti3(unsigned __int128 n, unsigned __int128 d) {
    return n - (d * __udivti3(n, d));
}

}