#include "XReadLocker.h"
#include "XMemory.h"
#include <stdlib.h>



// 获取结构体大小（供内存分配参考）
static size_t XReadLocker_getTypeSize() {
    return sizeof(struct XReadLocker);
}

void XReadLocker_init(XReadLocker* locker, XReadWriteLock* rwlock) {
    if (!locker || !rwlock) return;

    locker->rwlock = rwlock;
    XReadWriteLock_lockForRead(rwlock);
    locker->locked = true;
}

void XReadLocker_deinit(XReadLocker* locker) {
    if (!locker) return;

    XReadLocker_unlock(locker);
}

XReadLocker* XReadLocker_create(XReadWriteLock* rwlock) {
    if (!rwlock) return NULL;

    XReadLocker* locker = (XReadLocker*)XMemory_malloc(XReadLocker_getTypeSize());
    if (locker) {
        XReadLocker_init(locker, rwlock);
    }
    return locker;
}

void XReadLocker_delete(XReadLocker* locker) {
    if (!locker) return;

    XReadLocker_deinit(locker);
    XMemory_free(locker);
}

void XReadLocker_unlock(XReadLocker* locker) {
    if (locker && locker->locked && locker->rwlock) {
        XReadWriteLock_unlock(locker->rwlock);
        locker->locked = false;
    }
}

void XReadLocker_relock(XReadLocker* locker) {
    if (locker && locker->rwlock && !locker->locked) {
        XReadWriteLock_lockForRead(locker->rwlock);
        locker->locked = true;
    }
}

XReadWriteLock* XReadLocker_rwlock(XReadLocker* locker) {
    return locker ? locker->rwlock : NULL;
}