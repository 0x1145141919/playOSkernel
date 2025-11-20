#include "lock.h"

void spinlock_cpp_t::lock()
 {
        __asm__ __volatile__ (
            "1:\n\t"
            "lock; xchgb %0, %1\n\t"    // 带lock前缀的原子交换
            "testb %%al, %%al\n\t"      // 测试原来的锁状态
            "jz 3f\n\t"                 // 如果为0，成功获取锁
            
            "2:\n\t"                    // 自旋等待
            "pause\n\t"
            "cmpb $0, %1\n\t"           // 检查锁状态（非原子读）
            "jz 1b\n\t"                 // 如果释放了，重新尝试获取
            "jmp 2b\n\t"                // 继续等待
            
            "3:\n\t"
            : "=a" (status)
            : "m" (status), "0" (LOCKED)
            : "memory"
        );
}

void spinlock_cpp_t::unlock()
 {
        // 关键修正：添加内存屏障确保解锁操作对所有CPU可见
        __asm__ __volatile__ (
            "movb %1, %0\n\t" 
            "sfence\n\t"          // status = UNLOCKED
            : "=m" (status)
            : "r" (UNLOCKED)
            : "memory"                  // 内存屏障，确保存储操作全局可见
        );
}

bool trylock_cpp_t::try_lock()
 {
        uint8_t old_status = UNLOCKED;
        __asm__ __volatile__ (
            "lock; xchgb %0, %1\n\t"    // 原子交换尝试
            : "=a" (old_status)         // 输出：原来的状态值
            : "m" (status), "0" (LOCKED) // 输入：内存位置和期望值
            : "memory"
        );
        return (old_status == UNLOCKED); // 如果原来是未锁定，则成功
    }

    void trylock_cpp_t::unlock()
    {
        __asm__ __volatile__ (
            "movb $0, %0"
            : "=m" (status)
            : 
            : "memory"
        );
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