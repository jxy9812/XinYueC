#ifndef XWAITCONDITION_H
#define XWAITCONDITION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "XMutex.h"  // 依赖互斥锁接口

// 平台无关结构体声明
typedef struct XWaitCondition XWaitCondition;

// 平台相关句柄定义（内部使用，用户无需关心）
#ifdef _WIN32
#include <windows.h>
typedef CONDITION_VARIABLE XWaitConditionHandle;
#elif defined(configUSE_FREERTOS)
#include "FreeRTOS.h"
#include "semphr.h"
    typedef struct {
        SemaphoreHandle_t sem;       // 信号量
        volatile int wait_count;     // 等待线程数
        StaticSemaphore_t sem_buf;   // 静态信号量缓冲区
    } XWaitConditionHandle;
#else
#include <pthread.h>
    typedef pthread_cond_t XWaitConditionHandle;
#endif

// 条件变量结构体（不依赖XClass，无继承）
struct XWaitCondition 
{
    XWaitConditionHandle handle;  // 平台相关句柄
};

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
 * @param mutex 关联的互斥锁（必须已上锁）
 * @param timeout 超时时间（毫秒），-1表示无限等待
 * @return 成功唤醒返回true，超时返回false
 */
bool XWaitCondition_wait(XWaitCondition* cond, XMutex* mutex, int timeout);

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

#endif  // XWAITCONDITION_H