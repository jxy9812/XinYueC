/**
 * @file       XAnimationDriver.h
 * @brief      动画驱动器公开 API。
 * @details    对齐 Qt 6.8 QAnimationDriver（QtCore 动画模块）；实现只依赖
 *             XinYueC 抽象层，禁止调用 Win32、POSIX、Qt 或其他平台 API。
 *             框架内默认驱动器随首个顶层 Running 动画自动安装、随最后一个
 *             注销自动卸载，业务通常无需直接使用本类。
 */
#ifndef XANIMATIONDRIVER_H
#define XANIMATIONDRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XObject.h"

XCLASS_DEFINE_BEGING(XAnimationDriver)
XCLASS_DEFINE_EXTEND_END(XAnimationDriver, XObject)

/**
 * @brief 动画时间推进驱动器，对应 Qt 6.8 的 QAnimationDriver。
 * @note 框架维护一个模块级默认驱动器单例；install/uninstall 供需要自定义
 *       推进节奏的宿主（如渲染循环）接管时使用。
 */
typedef struct XAnimationDriver {
    XObject m_class;        ///< 继承 XObject，必须位于结构体第一位。
    XTimerId m_timerId;     ///< 驱动定时器编号；未安装时为 XTIMER_INVALID_ID。
    int64_t m_elapsedStart; ///< 启动时刻（单调毫秒），用于 elapsed()。
    bool m_running;         ///< 是否已安装并推进中。
} XAnimationDriver;

/**
 * @brief 初始化 XAnimationDriver 虚函数表。
 * @return XAnimationDriver 的共享虚函数表。
 */
XVtable* XAnimationDriver_class_init(void);
/**
 * @brief 创建动画驱动器（对标 QAnimationDriver(QObject*)）。
 * @param memory 内存类型。
 * @return 新驱动器；内存分配失败返回 NULL。
 */
XAnimationDriver* XAnimationDriver_create_ex(XMemoryType memory);
/**
 * @brief 初始化驱动器（栈对象或嵌入结构体场景）。
 * @param driver 调用者提供的未初始化存储。
 */
void XAnimationDriver_init(XAnimationDriver* driver);

/**
 * @brief 安装驱动器并开始按帧推进（对标 QAnimationDriver::install）。
 * @param driver 目标驱动器；NULL 时使用框架默认驱动器单例。
 * @note 已安装时为空操作。安装后由 16ms 周期定时器调用
 *       XAnimationDriver_advance。
 */
void XAnimationDriver_install(XAnimationDriver* driver);
/**
 * @brief 卸载驱动器并停止推进（对标 QAnimationDriver::uninstall）。
 * @param driver 目标驱动器；NULL 时作用于框架默认驱动器单例。
 */
void XAnimationDriver_uninstall(XAnimationDriver* driver);

/**
 * @brief 立即推进一帧（对标 QAnimationDriver::advance）。
 * @param driver 目标驱动器；NULL 时作用于框架默认驱动器单例。
 * @note 对全部顶层 Running 动画按距上一帧的单调时差 setCurrentTime。
 */
void XAnimationDriver_advance(XAnimationDriver* driver);

/**
 * @brief 查询驱动器是否推进中（对标 QAnimationDriver::isRunning）。
 * @param driver 目标驱动器；NULL 时作用于框架默认驱动器单例。
 * @return 已安装且未卸载返回 true。
 */
bool XAnimationDriver_isRunning(const XAnimationDriver* driver);

/**
 * @brief 获取自本次安装以来的毫秒数（对标 QAnimationDriver::elapsed）。
 * @param driver 目标驱动器；NULL 时作用于框架默认驱动器单例。
 * @return 单调毫秒数；未安装时返回 0。
 */
int64_t XAnimationDriver_elapsed(const XAnimationDriver* driver);

/**
 * @brief 获取框架默认驱动器单例（对标 Qt 内部 QUnifiedTimer 的默认驱动）。
 * @return 默认驱动器（懒创建）；内存分配失败返回 NULL。
 * @warning 借用语义：返回值归模块所有，调用者不得释放。
 */
XAnimationDriver* XAnimationDriver_defaultInstance(void);

/**
 * @brief 驱动器开始推进时发出的 started 信号（对标 started）。
 * @param driver 已开始推进的驱动器。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XAnimationDriver_started_signal(XAnimationDriver* driver);
/**
 * @brief 驱动器停止推进时发出的 stopped 信号（对标 stopped）。
 * @param driver 已停止推进的驱动器。
 * @return 信号标识，供 XSignal 宏和信号槽连接使用。
 */
void* XAnimationDriver_stopped_signal(XAnimationDriver* driver);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XAnimationDriver_create
#define XAnimationDriver_create() \
	XAnimationDriver_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XANIMATIONDRIVER_H
