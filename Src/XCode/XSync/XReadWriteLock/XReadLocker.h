#ifndef XREADLOCKER_H
#define XREADLOCKER_H

#include "XReadWriteLock.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 读锁自动管理器
 * 采用RAII模式，在初始化时获取读锁，销毁时自动释放
 */
typedef struct XReadLocker
{
    XReadWriteLock* rwlock;  // 关联的读写锁
    bool locked;             // 锁状态标记
}XReadLocker;

/**
 * @brief 初始化栈上的读锁管理器
 * @param locker 读锁管理器指针
 * @param rwlock 关联的读写锁
 */
void XReadLocker_init(XReadLocker* locker, XReadWriteLock* rwlock);

/**
 * @brief 销毁栈上的读锁管理器（自动释放锁）
 * @param locker 读锁管理器指针
 */
void XReadLocker_deinit(XReadLocker* locker);

/**
 * @brief 创建堆上的读锁管理器
 * @param rwlock 关联的读写锁
 * @return 成功返回读锁管理器指针，失败返回NULL
 */
XReadLocker* XReadLocker_create(XReadWriteLock* rwlock);

/**
 * @brief 销毁并释放堆上的读锁管理器（自动释放锁）
 * @param locker 读锁管理器指针
 */
void XReadLocker_delete(XReadLocker* locker);

/**
 * @brief 手动释放读锁
 * @param locker 读锁管理器指针
 */
void XReadLocker_unlock(XReadLocker* locker);

/**
 * @brief 手动重新获取读锁
 * @param locker 读锁管理器指针
 */
void XReadLocker_relock(XReadLocker* locker);

/**
 * @brief 获取关联的读写锁
 * @param locker 读锁管理器指针
 * @return 读写锁指针，NULL表示无效
 */
XReadWriteLock* XReadLocker_rwlock(XReadLocker* locker);

#ifdef __cplusplus
}
#endif

#endif // XREADLOCKER_H