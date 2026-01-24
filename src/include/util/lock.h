#pragma once
#include <stdint.h>
class spinlock_cpp_t {
    uint8_t status;
    static constexpr uint8_t LOCKED = 1;
    static constexpr uint8_t UNLOCKED = 0;
    
public:
    spinlock_cpp_t() : status(UNLOCKED) {}
    
    void lock();
    
    void unlock();
};
class spinrwlock_cpp_t{
    spinlock_cpp_t readlock;
    spinlock_cpp_t writelock;
    uint32_t readers=0;
public:
    spinrwlock_cpp_t()=default;
    void read_lock();
    void read_unlock();
    void write_lock();
    void write_unlock();
};
class trylock_cpp_t {
    uint8_t status;
    static constexpr uint8_t LOCKED = 1;
    static constexpr uint8_t UNLOCKED = 0;
    
public:
    trylock_cpp_t() : status(UNLOCKED) {}
    
    // 尝试获取锁，成功返回true，失败返回false
    bool try_lock();
    
    void unlock();
};
class trywritelock_cpp_t:public trylock_cpp_t {
    uint8_t status;
    static constexpr uint8_t LOCKED = 1;
    static constexpr uint8_t UNLOCKED = 0;
    
public:
    trywritelock_cpp_t() : status(UNLOCKED) {}
    
    // 尝试获取锁，成功返回true，失败返回false
    using trylock_cpp_t::try_lock;
    
    using trylock_cpp_t::unlock;
};