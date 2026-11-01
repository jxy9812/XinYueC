#include "XReadLocker.h"
#include "XMemory.h"
#if XSYNC_ON
#if XREADWRITELOCK_ON



// 获取结构体大小（供内存分配参考）
static size_t XReadLocker_getTypeSize() {
    return sizeof(struct XReadLocker);
}

void XReadLocker_init(XReadLocker* locker, XReadWriteLock* rwlock) {
    if (!locker || !rwlock) return;

    locker->m_rwlock = rwlock;
    XReadWriteLock_lockForRead(rwlock);
    locker->m_locked = true;
}

void XReadLocker_deinit(XReadLocker* locker) {
    if (!locker) return;

    XReadLocker_unlock(locker);
}

XReadLocker* XReadLocker_create(XReadWriteLock* rwlock) {
    if (!rwlock) return NULL;

    XReadLocker* locker = (XReadLocker*)XMalloc_System(XReadLocker_getTypeSize());
    if (locker) {
        XReadLocker_init(locker, rwlock);
    }
    return locker;
}

void XReadLocker_delete(XReadLocker* locker) {
    if (!locker) return;

    XReadLocker_deinit(locker);
    XFree_System(locker);
}

void XReadLocker_unlock(XReadLocker* locker) {
    if (locker && locker->m_locked && locker->m_rwlock) {
        XReadWriteLock_unlock(locker->m_rwlock);
        locker->m_locked = false;
    }
}

void XReadLocker_relock(XReadLocker* locker) {
    if (locker && locker->m_rwlock && !locker->m_locked) {
        XReadWriteLock_lockForRead(locker->m_rwlock);
        locker->m_locked = true;
    }
}

XReadWriteLock* XReadLocker_rwlock(XReadLocker* locker) {
    return locker ? locker->m_rwlock : NULL;
}
#endif // XREADWRITELOCK_ON
#endif /* XSYNC_ON */