/**
 * @file       XReadLocker.h
 * @brief      读锁自动管理器公开 API。
 * @details    对齐 Qt 6.8 QReadLocker；RAII 语义（初始化加读锁、销毁自动释放），
 *             平台原语由 Drive 平台适配层提供，本模块禁止直接调用
 *             Win32、POSIX、Qt 或其他平台 API。
 */
#ifndef XREADLOCKER_H
#define XREADLOCKER_H
#include "XSync_config.h"
#if XSYNC_ON
#if XREADWRITELOCK_ON

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
    XReadWriteLock* m_rwlock;  // 关联的读写锁
    bool m_locked;             // 锁状态标记
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

#endif // XREADWRITELOCK_ON
#endif /* XSYNC_ON */
#endif // XREADLOCKER_H