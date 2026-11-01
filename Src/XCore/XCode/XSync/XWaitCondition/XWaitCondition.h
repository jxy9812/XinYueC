
#ifndef XWAITCONDITION_H
#define XWAITCONDITION_H
#include "XSync_config.h"
#if XSYNC_ON
#if XWAITCONDITION_ON

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "XMutex.h"  // 依赖互斥锁

// 只声明结构体，不定义具体实现
typedef struct XWaitCondition XWaitCondition;
//获取此类型的大小
size_t XWaitCondition_typeSize();
/**
 * @brief 初始化条件变量（栈对象）
 * @param cond 待初始化的XWaitCondition指针
 */
void XWaitCondition_init(XWaitCondition* cond);

/**
 * @brief 销毁条件变量（栈对象）
 * @param cond 待销毁的XWaitCondition指针
 */
void XWaitCondition_deinit(XWaitCondition* cond);

/**
 * @brief 创建条件变量（堆对象）
 * @return 成功返回XWaitCondition指针，失败返回NULL
 */
XWaitCondition* XWaitCondition_create();

/**
 * @brief 销毁并释放条件变量（堆对象）
 * @param cond 待销毁的XWaitCondition指针
 */
void XWaitCondition_delete(XWaitCondition* cond);

/**
 * @brief 等待条件满足（必须已持有mutex锁）
 * @param cond 条件变量指针
 * @param mutex 关联的互斥锁（必须已上锁,不能是自旋模式）
 * @param timeout 超时时间（毫秒），-1表示无限等待
 * @return 成功唤醒返回true，超时返回false
 */
bool XWaitCondition_wait(XWaitCondition* cond, XMutex* mutex, int32_t timeout);

/**
 * @brief 唤醒一个等待的线程
 * @param cond 条件变量指针
 */
void XWaitCondition_wakeOne(XWaitCondition* cond);

/**
 * @brief 唤醒所有等待的线程
 * @param cond 条件变量指针
 */
void XWaitCondition_wakeAll(XWaitCondition* cond);

#ifdef __cplusplus
}
#endif

#endif // XWAITCONDITION_ON
#endif /* XSYNC_ON */
#endif  // XWAITCONDITION_H