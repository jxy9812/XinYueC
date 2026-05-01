#ifndef XREADWRITELOCK_H
#define XREADWRITELOCK_H

#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "XAtomic.h"
#include "XTypes.h"

//读写自旋锁
typedef struct XReadWriteLock
{
    XLock_Type type;
    XAtomic_size_t state; // 核心状态变量
    char m_d[];//非自旋模式扩展数据
}XReadWriteLock;
//获取此类型的大小
size_t XReadLocker_typetSize(XLock_Type type);
/**
 * @brief 初始化读写锁(栈对象)
 * @param rwlock 读写锁指针
 * @param type 锁类型(递归/非递归)
 */
void XReadWriteLock_init(XReadWriteLock* rwlock, XLock_Type type);

/**
 * @brief 销毁读写锁(栈对象)
 * @param rwlock 读写锁指针
 */
void XReadWriteLock_deinit(XReadWriteLock* rwlock);

/**
 * @brief 创建读写锁(堆对象)
 * @param type 锁类型(递归/非递归)
 * @return 成功返回读写锁指针，失败返回NULL
 */
XReadWriteLock* XReadWriteLock_create(XLock_Type type);

/**
 * @brief 销毁并释放读写锁(堆对象)
 * @param rwlock 读写锁指针
 */
void XReadWriteLock_delete(XReadWriteLock* rwlock);

/**
 * @brief 获取读锁(阻塞)
 * @param rwlock 读写锁指针
 */
void XReadWriteLock_lockForRead(XReadWriteLock* rwlock);

/**
 * @brief 获取写锁(阻塞)
 * @param rwlock 读写锁指针
 */
void XReadWriteLock_lockForWrite(XReadWriteLock* rwlock);

/**
 * @brief 尝试获取读锁(非阻塞)
 * @param rwlock 读写锁指针
 * @return 成功返回true，失败返回false
 */
bool XReadWriteLock_tryLockForRead(XReadWriteLock* rwlock);

/**
 * @brief 尝试获取写锁(非阻塞)
 * @param rwlock 读写锁指针
 * @return 成功返回true，失败返回false
 */
bool XReadWriteLock_tryLockForWrite(XReadWriteLock* rwlock);

/**
 * @brief 超时等待获取读锁
 * @param rwlock 读写锁指针
 * @param timeout 超时时间(毫秒)，-1表示永久等待
 * @return 成功返回true，超时返回false
 */
bool XReadWriteLock_tryLockForReadTimeout(XReadWriteLock* rwlock, int32_t timeout);

/**
 * @brief 超时等待获取写锁
 * @param rwlock 读写锁指针
 * @param timeout 超时时间(毫秒)，-1表示永久等待
 * @return 成功返回true，超时返回false
 */
bool XReadWriteLock_tryLockForWriteTimeout(XReadWriteLock* rwlock, int32_t timeout);

/**
 * @brief 释放锁(读锁/写锁通用)
 * @param rwlock 读写锁指针
 */
void XReadWriteLock_unlock(XReadWriteLock* rwlock);

/**
 * @brief 获取当前锁类型
 * @param rwlock 读写锁指针
 * @return 锁类型枚举值
 */
XLock_Type XReadWriteLock_type(XReadWriteLock* rwlock);

/**
 * @brief 判断当前线程是否持有读锁
 * @param rwlock 读写锁指针
 * @return 持有返回true，否则返回false
 */
bool XReadWriteLock_hasReadLock(XReadWriteLock* rwlock);

/**
 * @brief 判断当前线程是否持有写锁
 * @param rwlock 读写锁指针
 * @return 持有返回true，否则返回false
 */
bool XReadWriteLock_hasWriteLock(XReadWriteLock* rwlock);

#ifdef __cplusplus
}
#endif

#endif // XREADWRITELOCK_H
