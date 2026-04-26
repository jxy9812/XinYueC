#ifndef XSPINLOCK_H
#define XSPINLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "XAtomic.h" // 包含 XAtomic_bool 及其操作
// 前向声明结构体
// 定义自旋锁的实际结构
typedef struct XSpinLock 
{
   XAtomic_bool locked; // false 表示空闲，true 表示已锁定
}XSpinLock;

/**
 * @brief 初始化自旋锁（栈对象）
 * @param lock 自旋锁指针
 */
void XSpinLock_init(XSpinLock* lock);

/**
 * @brief 销毁自旋锁（栈对象）
 * @param lock 自旋锁指针
 */
void XSpinLock_deinit(XSpinLock* lock);

/**
 * @brief 创建自旋锁（堆对象）
 * @return 成功返回XSpinLock指针，失败返回NULL
 */
XSpinLock* XSpinLock_create();

/**
 * @brief 销毁并释放自旋锁（堆对象）
 * @param lock 自旋锁指针
 */
void XSpinLock_delete(XSpinLock* lock);

/**
 * @brief 上锁（忙等待/自旋）
 * @param lock 自旋锁指针
 */
void XSpinLock_lock(XSpinLock* lock);

/**
 * @brief 尝试上锁（非阻塞）
 * @param lock 自旋锁指针
 * @return 成功返回true，失败返回false
 */
bool XSpinLock_tryLock(XSpinLock* lock);

/**
 * @brief 解锁
 * @param lock 自旋锁指针
 */
void XSpinLock_unlock(XSpinLock* lock);

#ifdef __cplusplus
}
#endif

#endif // XSPINLOCK_H