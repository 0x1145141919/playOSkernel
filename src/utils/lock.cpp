#include "util/lock.h"
static inline void cpu_relax()
{
#if defined(__x86_64__) || defined(__i386__)
        __asm__ __volatile__("pause" ::: "memory");
#elif defined(__aarch64__)
        __asm__ __volatile__("yield" ::: "memory");
#else
        __asm__ __volatile__("" ::: "memory");
#endif
}

void spinlock_cpp_t::lock()
 {
        while (__atomic_test_and_set(&status, __ATOMIC_ACQUIRE)) {
                while (__atomic_load_n(&status, __ATOMIC_RELAXED) == LOCKED) {
                        cpu_relax();
                }
        }
}

bool spinlock_cpp_t::is_locked()
{
    return __atomic_load_n(&status, __ATOMIC_RELAXED) == LOCKED;
}
void spinlock_cpp_t::unlock()
{
    __atomic_clear(&status, __ATOMIC_RELEASE);
}


bool trylock_cpp_t::try_lock()
 {
        return !__atomic_test_and_set(&status, __ATOMIC_ACQUIRE);
}

    void trylock_cpp_t::unlock()
    {
        __atomic_clear(&status, __ATOMIC_RELEASE);
    }

void spintrylock_cpp_t::lock()
{
    while (__atomic_test_and_set(&status, __ATOMIC_ACQUIRE)) {
        while (__atomic_load_n(&status, __ATOMIC_RELAXED) == LOCKED) {
            cpu_relax();
        }
    }
}

bool spintrylock_cpp_t::try_lock()
{
    return !__atomic_test_and_set(&status, __ATOMIC_ACQUIRE);
}

void spintrylock_cpp_t::unlock()
{
    __atomic_clear(&status, __ATOMIC_RELEASE);
}
// 读锁实现
void spinrwlock_cpp_t::read_lock() {
    readlock.lock();      // 保护readers计数器
    readers++;
    if (readers == 1) {
        // 第一个读者需要获取写锁
        writelock.lock();
    }
    readlock.unlock();
}

// 读解锁实现  
void spinrwlock_cpp_t::read_unlock() {
    readlock.lock();      // 保护readers计数器
    readers--;
    if (readers == 0) {
        // 最后一个读者释放写锁
        writelock.unlock();
    }
    readlock.unlock();
}

// 写锁实现
void spinrwlock_cpp_t::write_lock() {
    writelock.lock();     // 直接获取写锁
}

// 写解锁实现
void spinrwlock_cpp_t::write_unlock() {
    writelock.unlock();   // 直接释放写锁
}
