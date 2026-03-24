#include "util/lock.h"
#include "util/arch/x86-64/cpuid_intel.h"
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
static inline void tmp_disable_interrupt(){
    asm volatile("cli" ::: "memory");
}
static inline uint32_t e()
{ 
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

spinlock_guard::spinlock_guard(spinlock_cpp_t& lock)
    : lock_ref(lock)
{
    lock_ref.lock();
}

spinlock_guard::~spinlock_guard()
{
    lock_ref.unlock();
}

reentrant_spinlock_cpp_t::reentrant_spinlock_cpp_t()
{
    complex.store(0);
}

void reentrant_spinlock_cpp_t::lock()
{
    const uint64_t pid = static_cast<uint64_t>(fast_get_processor_id());

    while (true) {
        uint64_t cur = complex.load();
        uint64_t depth = cur & DEPTH_MASK;
        uint64_t owner = cur >> PID_SHIFT;

        if (depth == 0) {
            uint64_t desired = (pid << PID_SHIFT) | 1;
            if (complex.cmpxchg_strong(cur, desired)) {
                return;
            }
        } else if (owner == pid) {
            if (depth >= DEPTH_MASK) {
                __builtin_trap();
            }
            uint64_t desired = (cur & ~DEPTH_MASK) | (depth + 1);
            if (complex.cmpxchg_strong(cur, desired)) {
                return;
            }
        } else {
            cpu_relax();
        }
    }
}

void reentrant_spinlock_cpp_t::unlock()
{
    const uint64_t pid = static_cast<uint64_t>(fast_get_processor_id());
    uint64_t cur = complex.load();
    uint64_t depth = cur & DEPTH_MASK;
    uint64_t owner = cur >> PID_SHIFT;

    if (depth == 0 || owner != pid) {
        return;
    }
    complex.store(0);
    
}

bool reentrant_spinlock_cpp_t::is_locked()
{
    constexpr uint64_t DEPTH_MASK = 0xFULL;
    uint64_t cur = complex.load();
    return (cur & DEPTH_MASK) != 0;
}

reentrant_spinlock_guard::reentrant_spinlock_guard(reentrant_spinlock_cpp_t& lock)
    : lock_ref(lock)
{
    lock_ref.lock();
}

reentrant_spinlock_guard::~reentrant_spinlock_guard()
{
    lock_ref.unlock();
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

spinrwlock_read_guard::spinrwlock_read_guard(spinrwlock_cpp_t& lock)
    : lock_ref(lock)
{
    lock_ref.read_lock();
}

spinrwlock_read_guard::~spinrwlock_read_guard()
{
    lock_ref.read_unlock();
}

spinrwlock_write_guard::spinrwlock_write_guard(spinrwlock_cpp_t& lock)
    : lock_ref(lock)
{
    lock_ref.write_lock();
}

spinrwlock_write_guard::~spinrwlock_write_guard()
{
    lock_ref.write_unlock();
}
