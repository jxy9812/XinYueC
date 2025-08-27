#ifndef XSEMAPHORE_H
#define XSEMAPHORE_H

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stdint.h>

// 只声明结构体，不定义具体实现（与XWaitCondition保持一致）
typedef struct XSemaphore XSemaphore;

// 获取结构体大小（参考XWaitCondition的GetTypeSize接口）
size_t XSemaphore_getTypeSize();

/**
 * @brief 初始化信号量(栈对象)
 * @param sem 信号量指针
 * @param initial 初始资源数量
 * @param maximum 最大资源数量
 */
void XSemaphore_init(XSemaphore* sem, int32_t initial, int32_t maximum);

/**
 * @brief 销毁信号量(栈对象)
 * @param sem 信号量指针
 */
void XSemaphore_deinit(XSemaphore* sem);

/**
 * @brief 创建信号量(堆对象)
 * @param initial 初始资源数量
 * @param maximum 最大资源数量
 * @return 信号量指针，失败返回NULL
 */
XSemaphore* XSemaphore_create(int32_t initial, int32_t maximum);

/**
 * @brief 销毁并释放信号量(堆对象)
 * @param sem 信号量指针
 */
void XSemaphore_delete(XSemaphore* sem);

/**
 * @brief 获取n个资源(阻塞)
 * @param sem 信号量指针
 * @param n 要获取的资源数量
 */
void XSemaphore_acquire(XSemaphore* sem, int32_t n);

/**
 * @brief 释放n个资源
 * @param sem 信号量指针
 * @param n 要释放的资源数量
 */
void XSemaphore_release(XSemaphore* sem, int32_t n);

/**
 * @brief 尝试获取n个资源(非阻塞)
 * @param sem 信号量指针
 * @param n 要获取的资源数量
 * @return 获取成功返回true
 */
bool XSemaphore_tryAcquire(XSemaphore* sem, int32_t n);

/**
 * @brief 超时等待获取n个资源
 * @param sem 信号量指针
 * @param n 要获取的资源数量
 * @param timeout 超时时间(毫秒)
 * @return 获取成功返回true
 */
bool XSemaphore_tryAcquireTimeout(XSemaphore* sem, int32_t n, uint32_t timeout);

/**
 * @brief 获取当前可用资源数量
 * @param sem 信号量指针
 * @return 可用资源数量
 */
int32_t XSemaphore_available(XSemaphore* sem);

#ifdef __cplusplus
}
#endif

#endif // XSEMAPHORE_H