/** @file XSync_config.h
 * @brief XSync 线程同步模块配置文件
 *
 * 通过此配置文件可以裁剪 XSync 模块的各个子功能：
 *   1. XMUTEX_ON              - 互斥锁（XMutex / XRecursiveMutex）
 *   2. XREADWRITELOCK_ON      - 读写锁（XReadWriteLock / XReadLocker / XWriteLocker）
 *   3. XRECURSIVELOCKSTATE_ON - 递归锁状态（XRecursiveLockState）
 *   4. XSEMAPHORE_ON          - 信号量（XSemaphore）
 *   5. XTHREADDATA_ON         - 线程私有数据与事件分发器（XThreadData/TLS/currentThreadId）
 *   6. XTHREAD_ON             - 线程对象（XThread / XThreadData）
 *   7. XTHREADPOOL_ON         - 线程池（XThreadPool / XRunnable）
 *   8. XWAITCONDITION_ON      - 条件变量（XWaitCondition）
 *
 * 模块总开关 XSYNC_ON 在 CXinYueConfig.h 中定义，此处仅提供默认值。
 * 关闭后若仍有其它模块无条件引用 XSync 符号，需同步裁剪对应依赖。
 */

#ifndef XSYNC_CONFIG_H
#define XSYNC_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                        模块总开关                                        */
/* ========================================================================== */
/** @brief XSync 模块总开关；置 0 时裁剪整个 XSync 公共 API 和所有子功能。
 *  @note 总开关在 CXinYueConfig.h 中统一定义，此处仅兜底默认值。
 *        关闭后若仍有其它模块无条件引用 XSync 符号，需同步裁剪对应依赖。
 */
#ifndef XSYNC_ON
#define XSYNC_ON 1
#endif

#if XSYNC_ON

/* ========================================================================== */
/*                        子功能开关                                        */
/* ========================================================================== */

/** @brief 互斥锁（XMutex / XRecursiveMutex） */
#ifndef XMUTEX_ON
#define XMUTEX_ON 1
#endif

/** @brief 读写锁（XReadWriteLock / XReadLocker / XWriteLocker） */
#ifndef XREADWRITELOCK_ON
#define XREADWRITELOCK_ON 1
#endif

/** @brief 递归锁状态（XRecursiveLockState） */
#ifndef XRECURSIVELOCKSTATE_ON
#define XRECURSIVELOCKSTATE_ON 1
#endif

/** @brief 信号量（XSemaphore） */
#ifndef XSEMAPHORE_ON
#define XSEMAPHORE_ON 1
#endif

/** @brief 线程对象（XThread）开关；关闭时仅剪裁 XThread 类与线程创建/start/wait 等 API。
 *  @note XThreadData、TLS、currentThreadId 属于 XTHREADDATA_ON 管辖，不随 XTHREAD_ON 关闭，以保证事件循环在单线程最小模式下可用。 */
#ifndef XTHREAD_ON
#define XTHREAD_ON 1
#endif

/** @brief 线程私有数据/事件分发器基础设施（XThreadData/TLS/currentThreadId）开关。
 *  @note 事件循环(XEvent)、对象系统(XObject)、互斥锁(XMutex)均依赖此基础设施；即使关闭 XTHREAD_ON，也必须保持此开关为 1，否则事件循环无法编译运行。 */
#ifndef XTHREADDATA_ON
#define XTHREADDATA_ON XSYNC_ON
#endif

/** @brief 线程池（XThreadPool / XRunnable） */
#ifndef XTHREADPOOL_ON
#define XTHREADPOOL_ON XTHREAD_ON
#endif

/** @brief 条件变量（XWaitCondition） */
#ifndef XWAITCONDITION_ON
#define XWAITCONDITION_ON 1
#endif

#endif /* XSYNC_ON */

#ifdef __cplusplus
}
#endif

#endif /* XSYNC_CONFIG_H */
