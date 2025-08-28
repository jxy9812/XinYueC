#ifndef XWRITELOCKER_H
#define XWRITELOCKER_H

#include "XReadWriteLock.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 写锁自动管理器
 * 采用RAII模式，在初始化时获取写锁，销毁时自动释放
 */
typedef struct XWriteLocker 
{
    XReadWriteLock* rwlock;  // 关联的读写锁
    bool locked;             // 锁状态标记
}XWriteLocker;
/**
 * @brief 初始化栈上的写锁管理器
 * @param locker 写锁管理器指针
 * @param rwlock 关联的读写锁
 */
void XWriteLocker_init(XWriteLocker* locker, XReadWriteLock* rwlock);

/**
 * @brief 销毁栈上的写锁管理器（自动释放锁）
 * @param locker 写锁管理器指针
 */
void XWriteLocker_deinit(XWriteLocker* locker);

/**
 * @brief 创建堆上的写锁管理器
 * @param rwlock 关联的读写锁
 * @return 成功返回写锁管理器指针，失败返回NULL
 */
XWriteLocker* XWriteLocker_create(XReadWriteLock* rwlock);

/**
 * @brief 销毁并释放堆上的写锁管理器（自动释放锁）
 * @param locker 写锁管理器指针
 */
void XWriteLocker_delete(XWriteLocker* locker);

/**
 * @brief 手动释放写锁
 * @param locker 写锁管理器指针
 */
void XWriteLocker_unlock(XWriteLocker* locker);

/**
 * @brief 手动重新获取写锁
 * @param locker 写锁管理器指针
 */
void XWriteLocker_relock(XWriteLocker* locker);

/**
 * @brief 获取关联的读写锁
 * @param locker 写锁管理器指针
 * @return 读写锁指针，NULL表示无效
 */
XReadWriteLock* XWriteLocker_rwlock(XWriteLocker* locker);

#ifdef __cplusplus
}
#endif

#endif // XWRITELOCKER_H