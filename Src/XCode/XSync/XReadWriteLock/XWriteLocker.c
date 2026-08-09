#include "XWriteLocker.h"
#include "XMemory.h"
#include <stdlib.h>
#if XSYNC_ON
#if XREADWRITELOCK_ON



// 获取结构体大小（供内存分配参考）
static size_t XWriteLocker_getTypeSize() {
    return sizeof(struct XWriteLocker);
}

void XWriteLocker_init(XWriteLocker* locker, XReadWriteLock* rwlock) {
    if (!locker || !rwlock) return;

    locker->rwlock = rwlock;
    XReadWriteLock_lockForWrite(rwlock);
    locker->locked = true;
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
    if (locker && locker->locked && locker->rwlock) {
        XReadWriteLock_unlock(locker->rwlock);
        locker->locked = false;
    }
}

void XWriteLocker_relock(XWriteLocker* locker) {
    if (locker && locker->rwlock && !locker->locked) {
        XReadWriteLock_lockForWrite(locker->rwlock);
        locker->locked = true;
    }
}

XReadWriteLock* XWriteLocker_rwlock(XWriteLocker* locker) {
    return locker ? locker->rwlock : NULL;
}
#endif // XREADWRITELOCK_ON
#endif /* XSYNC_ON */