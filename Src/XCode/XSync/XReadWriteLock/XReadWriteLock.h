#ifndef XREADWRITELOCK_H
#define XREADWRITELOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 读写锁类型枚举
 * 对应QReadWriteLock的Recursive和NonRecursive类型
 */
typedef enum {
    XReadWriteLock_NonRecursive,  // 非递归模式(默认)
    XReadWriteLock_Recursive      // 递归模式
} XReadWriteLock_Type;

// 前向声明
typedef struct XReadWriteLock XReadWriteLock;

/**
 * @brief 获取类型大小
 * @return 结构体大小(字节)
 */
size_t XReadWriteLock_getTypeSize();

/**
 * @brief 初始化读写锁(栈对象)
 * @param rwlock 读写锁指针
 * @param type 锁类型(递归/非递归)
 */
void XReadWriteLock_init(XReadWriteLock* rwlock, XReadWriteLock_Type type);

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
XReadWriteLock* XReadWriteLock_create(XReadWriteLock_Type type);

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
XReadWriteLock_Type XReadWriteLock_type(XReadWriteLock* rwlock);

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
