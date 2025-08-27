#ifndef XMUTEX_H
#define XMUTEX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

// 只声明结构体，不定义具体实现
typedef struct XMutex XMutex;

// 互斥锁类型
typedef enum {
    XMutex_Normal,      // 普通锁，同一线程不能重复上锁
    XMutex_Recursive    // 递归锁，同一线程可以多次上锁
} XMutex_Type;

//获取此类型的大小
size_t XMutex_geTypetSize();

/**
 * @brief 初始化互斥锁（栈对象）
 * @param mutex 互斥锁指针
 * @param type 互斥锁类型
 */
void XMutex_init(XMutex* mutex, XMutex_Type type);

/**
 * @brief 销毁互斥锁（栈对象）
 * @param mutex 互斥锁指针
 */
void XMutex_deinit(XMutex* mutex);

/**
 * @brief 创建互斥锁（堆对象）
 * @param type 互斥锁类型
 * @return 成功返回XMutex指针，失败返回NULL
 */
XMutex* XMutex_create(XMutex_Type type);

/**
 * @brief 销毁并释放互斥锁（堆对象）
 * @param mutex 互斥锁指针
 */
void XMutex_delete(XMutex* mutex);

/**
 * @brief 上锁（阻塞等待）
 * @param mutex 互斥锁指针
 */
void XMutex_lock(XMutex* mutex);

/**
 * @brief 尝试上锁（非阻塞）
 * @param mutex 互斥锁指针
 * @return 成功返回true，失败返回false
 */
bool XMutex_tryLock(XMutex* mutex);

/**
 * @brief 超时等待上锁
 * @param mutex 互斥锁指针
 * @param timeout 超时时间（毫秒）
 * @return 成功返回true，超时返回false
 */
bool XMutex_tryLockTimeout(XMutex* mutex, uint32_t timeout);

/**
 * @brief 解锁
 * @param mutex 互斥锁指针
 */
void XMutex_unlock(XMutex* mutex);

/**
 * @brief 判断是否为递归锁
 * @param mutex 互斥锁指针
 * @return 是递归锁返回true
 */
bool XMutex_isRecursive(XMutex* mutex);

// 内部函数声明（供XWaitCondition使用）
void* XMutex_getNativeHandle(XMutex* mutex);


#ifdef __cplusplus
}
#endif

#endif // XMUTEX_H