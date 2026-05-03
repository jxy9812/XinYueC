#ifndef XTHREADPOOL_H
#define XTHREADPOOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XObject.h"
#include "XRunnable.h"

/**
 * @brief XThreadPool类结构体定义，用于管理线程池
 * @note 继承自XObject，提供线程池功能，管理一组工作线程
 */
typedef struct XThreadPool
{
    XObject m_object;                    ///< 继承的基类成员

    // 线程池核心属性
    uint16_t max_thread_count;               ///< 最大线程数
    uint16_t active_thread_count;            ///< 当前活跃线程数
    uint32_t expiry_timeout;                 ///< 线程过期超时时间（毫秒）
    uint32_t stack_size;                ///< 工作线程栈大小
    // 内部实现相关
    void* private_data;                 ///< 私有数据指针，指向内部实现
} XThreadPool;

/**
 * @brief 开始定义XThreadPool类的虚函数表枚举
 * @note 继承QObject的所有虚函数，用于事件处理和对象生命周期管理
 */
XCLASS_DEFINE_BEGING(XThreadPool)
// 继承自XObject(QObjec)的所有虚函数
//XCLASS_DEFINE_ENUM(XThreadPool, Poll) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_END(XThreadPool)

/**
 * @brief 初始化XThreadPool类的虚函数表
 * @return 指向初始化完成的XVtable的指针
 */
XVtable* XThreadPool_class_init();

/**
 * @brief 在堆上创建XThreadPool实例并初始化
 * @param parent 父对象指针（可为NULL）
 * @return 指向新创建的XThreadPool对象的指针，失败返回NULL
 */
XThreadPool* XThreadPool_create(XObject* parent);

/**
 * @brief 初始化XThreadPool实例
 * @param pool 待初始化的XThreadPool对象指针（非NULL）
 * @param parent 父对象指针（可为NULL）
 */
void XThreadPool_init(XThreadPool* pool, XObject* parent);

/**
 * @brief 销毁XThreadPool实例
 * @param pool 要销毁的XThreadPool对象指针（非NULL）
 * @note 此函数会阻塞直到所有可运行任务完成
 */
void XThreadPool_delete(XThreadPool* pool);

/**
 * @brief 获取全局XThreadPool实例
 * @return 全局XThreadPool实例指针
 * @note 每个应用程序只有一个全局线程池实例
 */
XThreadPool* XThreadPool_globalInstance();

/**
 * @brief 提交可运行任务到线程池
 * @param pool 线程池对象指针（非NULL）
 * @param runnable 可运行任务指针（非NULL）
 * @param priority 任务优先级（默认为0）
 * @note 线程池会接管runnable的所有权（如果autoDelete为true）
 */
void XThreadPool_start(XThreadPool* pool, XRunnable* runnable, int priority);

/**
 * @brief 尝试启动可运行任务
 * @param pool 线程池对象指针（非NULL）
 * @param runnable 可运行任务指针（非NULL）
 * @return 如果成功启动返回true，否则返回false
 */
bool XThreadPool_tryStart(XThreadPool* pool, XRunnable* runnable);

/**
 * @brief 获取线程过期超时时间
 * @param pool 线程池对象指针（非NULL）
 * @return 过期超时时间（毫秒），负值表示禁用过期机制
 */
int XThreadPool_expiryTimeout(const XThreadPool* pool);

/**
 * @brief 设置线程过期超时时间
 * @param pool 线程池对象指针（非NULL）
 * @param expiryTimeout 过期超时时间（毫秒）
 */
void XThreadPool_setExpiryTimeout(XThreadPool* pool, int expiryTimeout);

/**
 * @brief 获取最大线程数
 * @param pool 线程池对象指针（非NULL）
 * @return 最大线程数
 */
int XThreadPool_maxThreadCount(const XThreadPool* pool);

/**
 * @brief 设置最大线程数
 * @param pool 线程池对象指针（非NULL）
 * @param maxThreadCount 最大线程数
 */
void XThreadPool_setMaxThreadCount(XThreadPool* pool, int maxThreadCount);

/**
 * @brief 获取当前活跃线程数
 * @param pool 线程池对象指针（非NULL）
 * @return 当前活跃线程数
 */
int XThreadPool_activeThreadCount(const XThreadPool* pool);

/**
 * @brief 设置工作线程栈大小
 * @param pool 线程池对象指针（非NULL）
 * @param stackSize 栈大小（字节），0表示使用系统默认值
 */
void XThreadPool_setStackSize(XThreadPool* pool, uint32_t stackSize);

/**
 * @brief 获取工作线程栈大小
 * @param pool 线程池对象指针（非NULL）
 * @return 栈大小（字节）
 */
uint32_t XThreadPool_stackSize(const XThreadPool* pool);

/**
 * @brief 预留线程供外部使用
 * @param pool 线程池对象指针（非NULL）
 * @note 临时增加活跃线程计数
 */
void XThreadPool_reserveThread(XThreadPool* pool);

/**
 * @brief 释放预留的线程
 * @param pool 线程池对象指针（非NULL）
 * @note 减少活跃线程计数，使线程可被重用
 */
void XThreadPool_releaseThread(XThreadPool* pool);

/**
 * @brief 等待所有任务完成
 * @param pool 线程池对象指针（非NULL）
 * @param msecs 等待超时时间（毫秒），-1表示无限等待
 * @return 如果所有任务在超时前完成返回true，否则返回false
 */
bool XThreadPool_waitForDone(XThreadPool* pool, int msecs);

/**
 * @brief 清空线程池中的所有待执行任务
 * @param pool 线程池对象指针（非NULL）
 * @note 不会影响正在执行的任务
 */
void XThreadPool_clear(XThreadPool* pool);

/**
 * @brief 尝试从队列中移除指定的可运行任务
 * @param pool 线程池对象指针（非NULL）
 * @param runnable 要移除的可运行任务指针（非NULL）
 * @return 如果成功移除返回true，否则返回false
 * @note 仅对尚未开始执行的任务有效
 */
bool XThreadPool_tryTake(XThreadPool* pool, XRunnable* runnable);


#ifdef __cplusplus
}
#endif

#endif // XTHREADPOOL_H