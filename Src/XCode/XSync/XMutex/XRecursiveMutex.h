#ifndef XRECURSIVEMUTEX_H
#define XRECURSIVEMUTEX_H
#include "XSync_config.h"
#if XSYNC_ON
#if XMUTEX_ON

#ifdef __cplusplus
extern "C" {
#endif
#include "XMutex.h"
// 只声明结构体，不定义具体实现
typedef struct  XMutex  XRecursiveMutex;

//获取此类型的大小
#define XRecursiveMutex_typetSize				XMutex_typetSize(XMutex_Recursive)

/**
 * @brief 初始化互斥锁（栈对象）
 * @param mutex 互斥锁指针
 * @param type 互斥锁类型
 */
#define XRecursiveMutex_init(mutex)				  XMutex_init(mutex,XMutex_Recursive)

/**
 * @brief 销毁互斥锁（栈对象）
 * @param mutex 互斥锁指针
 */
#define XRecursiveMutex_deinit				   XMutex_deinit

/**
 * @brief 创建互斥锁（堆对象）
 * @return 成功返回XRecursiveMutex指针，失败返回NULL
 */
#define XRecursiveMutex_create				XMutex_create(XMutex_Recursive)

/**
 * @brief 销毁并释放互斥锁（堆对象）
 * @param mutex 互斥锁指针
 */
#define XRecursiveMutex_delete				XMutex_delete

/**
 * @brief 上锁（阻塞等待）
 * @param mutex 互斥锁指针
 */
#define XRecursiveMutex_lock				XMutex_lock

/**
 * @brief 尝试上锁（非阻塞）
 * @param mutex 互斥锁指针
 * @return 成功返回true，失败返回false
 */
#define XRecursiveMutex_tryLock				XMutex_tryLock

/**
 * @brief 超时等待上锁
 * @param mutex 互斥锁指针
 * @param timeout 超时时间（毫秒）
 * @return 成功返回true，超时返回false
 */
#define XRecursiveMutex_tryLockTimeout			XMutex_tryLockTimeout

/**
 * @brief 解锁
 * @param mutex 互斥锁指针
 */
#define XRecursiveMutex_unlock					XMutex_unlock

/**
 * @brief 判断是否为递归锁
 * @param mutex 互斥锁指针
 * @return 是递归锁返回true
 */
#define XRecursiveMutex_isRecursive				XMutex_isRecursive


#ifdef __cplusplus
}
#endif

#endif // XMUTEX_ON
#endif /* XSYNC_ON */
#endif // XRecursiveMutex_H