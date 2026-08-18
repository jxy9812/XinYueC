/**
 * @file       XDeviceTimer.h
 * @brief      统一定时器设备接口。
 * @details    XDeviceTimer 通过 XFd 管理定时器实例。XTimerType 决定调度后端：
 *             Coarse/VeryCoarse 优先使用进程级时间轮，Precise 使用所属事件
 *             调度器的红黑树高精度队列。高精度队列不跨线程共享。设备本身
 *             不依赖 XObject；到期后只调用调用方提供的回调。
 */
#ifndef XDEVICETIMER_H
#define XDEVICETIMER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XDevice.h"

typedef struct XAbstractEventDispatcher XAbstractEventDispatcher;
typedef struct XTimeWheelGroup XTimeWheelGroup;

/**
 * @brief 定时器到期回调。
 * @param userData XDeviceTimer_setUserData 设置的用户指针，只借用不释放。
 * @note 回调在所属事件调度器线程中同步执行；回调期间不应关闭同一设备。
 */
typedef void (*XDeviceTimerCallback)(void* userData);

/** @brief 定时器设备专有控制命令。 */
typedef enum XDeviceTimerCommand
{
    XDeviceTimerCommand_Start = XDeviceCommand_Count, /**< 启动定时器；in/out 均为空。 */
    XDeviceTimerCommand_Stop,                          /**< 停止定时器；in/out 均为空。 */
    XDeviceTimerCommand_Restart,                       /**< 按当前参数重启定时器；in/out 均为空。 */
    XDeviceTimerCommand_Count                           /**< 定时器命令数量边界，不是有效命令。 */
} XDeviceTimerCommand;

/** @brief 定时器属性；通过 XDevice_setProperty 统一修改。 */
typedef enum XDeviceTimerProperty
{
    XDeviceTimerProperty_TimerType = XDeviceProperty_Count, /**< 定时器类型，值为 XTimerType。 */
    XDeviceTimerProperty_IntervalNs,       /**< 周期或延时，值为 XDuration，单位纳秒。 */
    XDeviceTimerProperty_Callback,         /**< 到期回调，值为 XDeviceTimerCallback。 */
    XDeviceTimerProperty_UserData,         /**< 回调用户数据，值为 void*，只借用不释放。 */
    XDeviceTimerProperty_Count                             /**< 定时器属性数量边界，不是有效属性。 */
} XDeviceTimerProperty;

/** @brief 兼容简短命名；单位仍为纳秒。 */
#define XDeviceTimerProperty_Interval XDeviceTimerProperty_IntervalNs

/**
 * @brief 定时器打开选项。
 * @details 可在打开时提供初始参数，也可在打开后通过属性设置。设备不保存
 *          XObject 接收者，也不创建事件对象。m_base 必须是第一个成员，因而
 *          可以交给 XDevice_open；启动前必须设置有效的回调和正间隔。
 */
typedef struct XDeviceTimerOpenOptions
{
    XDeviceOpenOptions m_base;       /**< 通用打开选项，必须是第一个成员。 */
    XDeviceTimerCallback m_callback;/**< 到期回调；启动前必须非 NULL。 */
    void* m_userData;                /**< 回调用户数据；只借用，不由设备释放。 */
    XDuration m_intervalNs;          /**< 定时周期或延时，单位纳秒，必须大于 0。 */
    uint32_t m_timerType : 2;        /**< XTimerType 定时器类型。 */
    uint32_t m_singleShot : 1;       /**< 是否单次触发；1 为单次，0 为周期。 */
    uint32_t m_reserved   : 29;      /**< 保留位，必须初始化为 0。 */
} XDeviceTimerOpenOptions;

/**
 * @brief 定时器打开上下文。
 * @details m_base.m_fd 是对外唯一的定时器标识。m_backendHandle 仅由设备内部
 *          保存，禁止传给业务层或直接调用时间轮/红黑树 API。
 */
typedef struct XDeviceTimerContext
{
    XDeviceContext m_base;                 /**< 通用设备上下文，必须是第一个成员。 */
    XAbstractEventDispatcher* m_dispatcher;/**< 所属事件调度器；只借用。 */
    XHandle m_backendHandle;               /**< 时间轮或红黑树后端句柄；内部管理。 */
    XDeviceTimerCallback m_callback;       /**< 到期回调；只借用。 */
    void* m_userData;                       /**< 回调用户数据；只借用。 */
    XDuration m_intervalNs;                 /**< 定时周期或延时，单位纳秒。 */
    uint32_t m_timerType   : 2;             /**< 请求的 XTimerType。 */
    uint32_t m_backendType : 2;             /**< 实际后端类型；内部枚举值。 */
    uint32_t m_singleShot  : 1;             /**< 是否单次触发。 */
    uint32_t m_active      : 1;             /**< 当前是否已加入后端队列。 */
    uint32_t m_reserved    : 26;             /**< 保留位，必须初始化为 0。 */
} XDeviceTimerContext;

/** @brief XDeviceTimer 虚函数表；继承 XDevice，不新增虚槽。 */
XCLASS_DEFINE_BEGING(XDeviceTimer)
XCLASS_DEFINE_EXTEND_END(XDeviceTimer, XDevice)

/** @brief 进程级定时器设备类对象。 */
typedef struct XDeviceTimer
{
    XDevice m_base;
    XTimeWheelGroup* m_timeWheel;
} XDeviceTimer;

/** @brief 初始化定时器设备类虚函数表。 @return 共享虚函数表，失败返回 NULL。 */
XVtable* XDeviceTimer_class_init(void);
/** @brief 初始化已分配的定时器设备对象。 @param self 待初始化对象，不能为 NULL。 */
void XDeviceTimer_init(XDeviceTimer* self);
/** @brief 创建定时器设备类对象。 @return 新对象，失败返回 NULL；调用方负责释放。 */
XDeviceTimer* XDeviceTimer_create(void);

/**
 * @brief 初始化并注册类别名为 "timer" 的定时器设备。
 * @details 同时创建唯一的进程级时间轮；重复调用安全。红黑树高精度队列不在
 *          此处创建，而由每个 XAbstractEventDispatcher 在首次 Precise 定时器时创建。
 */
bool XDeviceTimer_register(void);

/** @brief 以统一设备门面打开一个定时器；这是 XDevice_open 的宏封装。 */
#define XDeviceTimer_open(options, error) \
    XDevice_open(XDeviceType_Timer, \
        (const XDeviceOpenOptions*)(options), (error))

/** @brief 复用 XDevice 的通用设备操作；参数契约与父类 API 相同。 */
#define XDeviceTimer_close   XDevice_close
#define XDeviceTimer_control XDevice_control

/** @brief 启动定时器，内部发送 XDeviceTimerCommand_Start。 @param fd 已打开的定时器句柄。 @return 成功返回 true。 */
bool XDeviceTimer_start(XFd fd);
/** @brief 停止定时器，内部发送 XDeviceTimerCommand_Stop。 @param fd 已打开的定时器句柄。 @return 成功返回 true。 */
bool XDeviceTimer_stop(XFd fd);
/** @brief 按当前参数重启定时器。 @param fd 已打开的定时器句柄。 @return 成功返回 true。 */
bool XDeviceTimer_restart(XFd fd);

/** @brief 设置定时器类型；内部转发到 XDevice_setProperty。 @param fd 定时器句柄。 @param type 定时器类型。 @return 成功返回 true。 */
bool XDeviceTimer_setTimerType(XFd fd, XTimerType type);
/** @brief 设置定时器间隔；内部转发到 XDevice_setProperty。 @param fd 定时器句柄。 @param intervalNs 间隔，单位纳秒，必须大于 0。 @return 成功返回 true。 */
bool XDeviceTimer_setInterval(XFd fd, XDuration intervalNs);
/** @brief 设置到期回调；内部转发到 XDevice_setProperty。 @param fd 定时器句柄。 @param callback 回调函数，可为 NULL 以清除。 @return 成功返回 true。 */
bool XDeviceTimer_setCallback(XFd fd, XDeviceTimerCallback callback);
/** @brief 设置回调用户数据；内部转发到 XDevice_setProperty。 @param fd 定时器句柄。 @param userData 用户指针，只借用。 @return 成功返回 true。 */
bool XDeviceTimer_setUserData(XFd fd, void* userData);

#ifdef __cplusplus
}
#endif

#endif /* XDEVICETIMER_H */
