#pragma once
#include <stdint.h>
static constexpr uint8_t LOCKED = 1;
static constexpr uint8_t UNLOCKED = 0;
class spinlock_cpp_t {
    uint8_t status;  
public:
    spinlock_cpp_t() : status(UNLOCKED) {}
    
    void lock();
    bool is_locked();
    void unlock();
};
class spinlock_guard {
    spinlock_cpp_t& lock_ref;
public:
    explicit spinlock_guard(spinlock_cpp_t& lock);
    ~spinlock_guard();
    spinlock_guard(const spinlock_guard&) = delete;
    spinlock_guard& operator=(const spinlock_guard&) = delete;
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
class spinrwlock_read_guard {
    spinrwlock_cpp_t& lock_ref;
public:
    explicit spinrwlock_read_guard(spinrwlock_cpp_t& lock);
    ~spinrwlock_read_guard();
    spinrwlock_read_guard(const spinrwlock_read_guard&) = delete;
    spinrwlock_read_guard& operator=(const spinrwlock_read_guard&) = delete;
};
class spinrwlock_write_guard {
    spinrwlock_cpp_t& lock_ref;
public:
    explicit spinrwlock_write_guard(spinrwlock_cpp_t& lock);
    ~spinrwlock_write_guard();
    spinrwlock_write_guard(const spinrwlock_write_guard&) = delete;
    spinrwlock_write_guard& operator=(const spinrwlock_write_guard&) = delete;
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
class spintrylock_cpp_t {
    uint8_t status;
    static constexpr uint8_t LOCKED = 1;
    static constexpr uint8_t UNLOCKED = 0;

public:
    spintrylock_cpp_t() : status(UNLOCKED) {}

    // 阻塞式获取：失败时自旋直到成功
    void lock();

    // 非阻塞尝试：失败立即返回false
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
