#include "XWriteLocker.h"
#include "XMemory.h"
#if XSYNC_ON
#if XREADWRITELOCK_ON



// 获取结构体大小（供内存分配参考）
static size_t XWriteLocker_getTypeSize() {
    return sizeof(struct XWriteLocker);
}

void XWriteLocker_init(XWriteLocker* locker, XReadWriteLock* rwlock) {
    if (!locker || !rwlock) return;

    locker->m_rwlock = rwlock;
    XReadWriteLock_lockForWrite(rwlock);
    locker->m_locked = true;
}

void XWriteLocker_deinit(XWriteLocker* locker) {
    if (!locker) return;

    XWriteLocker_unlock(locker);
}

XWriteLocker* XWriteLocker_create(XReadWriteLock* rwlock) {
    if (!rwlock) return NULL;

    XWriteLocker* locker = (XWriteLocker*)XMalloc_System(XWriteLocker_getTypeSize());
    if (locker) {
        XWriteLocker_init(locker, rwlock);
    }
    return locker;
}

void XWriteLocker_delete(XWriteLocker* locker) {
    if (!locker) return;

    XWriteLocker_deinit(locker);
    XFree_System(locker);
}

void XWriteLocker_unlock(XWriteLocker* locker) {
    if (locker && locker->m_locked && locker->m_rwlock) {
        XReadWriteLock_unlock(locker->m_rwlock);
        locker->m_locked = false;
    }
}

void XWriteLocker_relock(XWriteLocker* locker) {
    if (locker && locker->m_rwlock && !locker->m_locked) {
        XReadWriteLock_lockForWrite(locker->m_rwlock);
        locker->m_locked = true;
    }
}

XReadWriteLock* XWriteLocker_rwlock(XWriteLocker* locker) {
    return locker ? locker->m_rwlock : NULL;
}
#endif // XREADWRITELOCK_ON
#endif /* XSYNC_ON */