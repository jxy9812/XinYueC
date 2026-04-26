#include "XSpinLock.h"
#include "XMemory.h" // 假设存在 XMemory_calloc 和 XMemory_free
void XSpinLock_init(XSpinLock* lock) {
    if (!lock) return;
    // 初始化为未锁定状态
    XAtomic_init(lock->locked, false);
}

void XSpinLock_deinit(XSpinLock* lock) {
    // 对于基于原子变量的自旋锁，通常不需要特殊的销毁逻辑
    (void)lock; // 避免未使用参数的警告
}

XSpinLock* XSpinLock_create() {
    XSpinLock* lock = (XSpinLock*)XMemory_calloc(1, sizeof(XSpinLock));
    if (lock) {
        XSpinLock_init(lock);
    }
    return lock;
}

void XSpinLock_delete(XSpinLock* lock) {
    if (!lock) return;
    XSpinLock_deinit(lock);
    XMemory_free(lock);
}

void XSpinLock_lock(XSpinLock* lock) {
    if (!lock) return;

    bool expected = false;
    // 循环尝试获取锁
    while (!XAtomic_compare_exchange_strong_bool(&lock->locked, &expected, true)) {
        expected = false; // 重置 expected，为下一次尝试做准备
        // 可选：在此处加入编译器/平台特定的提示指令以优化性能
        // 例如，在 x86 上: __builtin_ia32_pause();
    }
}

bool XSpinLock_tryLock(XSpinLock* lock) {
    if (!lock) return false;

    bool expected = false;
    // 尝试一次获取锁，不成功则立即返回
    return XAtomic_compare_exchange_strong_bool(&lock->locked, &expected, true);
}

void XSpinLock_unlock(XSpinLock* lock) {
    if (!lock) return;
    // 直接将锁状态重置为 false (空闲)
    XAtomic_store_bool(&lock->locked, false);
}