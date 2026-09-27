/******************************************************************************
 * @file       XPlatformFbInput_posix.h
 * @brief      Linux fbdev 模式触摸输入驱动（linux/input.h evdev 读取 +
 *             指针事件注入）：fbdev 直写显示（XPlatformFramebuffer_posix）
 *             的配套输入端。
 * @details    嵌入式单屏无窗口系统场景下，X11 输入路径不存在（无 X 连接，
 *             XPlatformNativeWindow 回落虚拟 WId），触摸事件没有任何进入
 *             框架的通道。本文件补齐该通道：阻塞读取交给事件循环，本实
 *             现按非阻塞逐帧读取 evdev 报文，把单点触摸（ABS_X/ABS_Y 或
 *             MT 协议 ABS_MT_POSITION_X/Y + BTN_TOUCH/ABS_MT_TRACKING_ID/
 *             ABS_PRESSURE）归一为面板像素坐标，复用框架既有注入面投递：
 *             - 坐标校准：/etc/pointercal（QWS 7 参矩阵，路径可经
 *               XPLATFORM_FBINPUT_POINTERCAL 宏覆盖）存在时优先按厂商
 *               矩阵变换（涵盖轴交换/旋转/镜像与触摸区边沿）；缺席回退
 *               「EVIOCGABS 范围线性归一」，再退坐标直通+钳位；
 *             - 目标窗口：XGuiApplication_topLevelAt（全局坐标命中顶层，
 *               对标 QGuiApplication 的事件窗口命中）；
 *             - 注入入口：XWindowSystemInterface_handleMouseEvent_ex
 *               （指针按下/移动/抬起；控件级命中/抓取/模态拦截由
 *               VXWidgetWindow_event -> XWidget_dispatchPointerEvent
 *               既有管线完成，本文件不重复实现）。
 *             轮询挂钩复用 XAbstractEventDispatcher_addPollCallback（与
 *             XGuiApplication 原生事件泵同链），每轮 processEvents 抽干
 *             evdev 积压报文，不新增线程。
 * @note       编译门控：__linux__ && XGUI_ON && XPLATFORM_FBDEV_ON &&
 *             XPLATFORM_FBINPUT_ON（后者默认 0，#ifndef 兜底于此；随
 *             fbdev 构建以 -DXPLATFORM_FBINPUT_ON=ON 开启）。运行期设备
 *             节点优先读环境变量 XPLATFORM_FBINPUT_DEVICE，未设置用编译
 *             期宏 XPLATFORM_FBINPUT_DEVICE（默认 "/dev/input/event0"）；
 *             环境变量 XPLATFORM_FBINPUT=0 可在编译开启时运行期禁用。
 *             内存体系：全程无堆分配，状态静态；单线程主循环设计（与
 *             fbdev 显示驱动同口径），不加锁。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPLATFORMFBINPUT_POSIX_H
#define XPLATFORMFBINPUT_POSIX_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XPlatformDisplayDriver.h"

#if defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON

/* 模块开关：XPLATFORM_FBINPUT_ON 默认 0（默认不构建）。#ifndef 兜底
 * 保证任何包含顺序下取值确定；正式取值经 CMake 裁剪开关转发注入。 */
#ifndef XPLATFORM_FBINPUT_ON
#define XPLATFORM_FBINPUT_ON 0
#endif

#if XPLATFORM_FBINPUT_ON

/** @brief evdev 设备节点默认值（运行期可被环境变量
 *  XPLATFORM_FBINPUT_DEVICE 覆盖；板级也可用编译选项改默认值）。 */
#ifndef XPLATFORM_FBINPUT_DEVICE
#define XPLATFORM_FBINPUT_DEVICE "/dev/input/event0"
#endif

/** @brief 厂商触摸校准文件路径（QWS pointercal 7 参矩阵；板级可用
 *  编译选项覆盖；运行期文件缺席即回退线性归一）。 */
#ifndef XPLATFORM_FBINPUT_POINTERCAL
#define XPLATFORM_FBINPUT_POINTERCAL "/etc/pointercal"
#endif

/**
 * @brief      打开 evdev 设备并把读取泵挂入事件分发器轮询链。
 * @details    流程：XSystem_environment(XPLATFORM_FBINPUT_DEVICE) 解析
 *             节点（"0" 视为禁用）-> open(O_RDONLY|O_NONBLOCK) ->
 *             EVIOCGNAME 打印一次设备标识 -> EVIOCGBIT(EV_ABS) 探测
 *             ABS_X/ABS_MT 与取值范围（EVIOCGABS，坐标归一基准）->
 *             加载 /etc/pointercal 厂商校准（存在即优先）-> addPollCallback
 *             注册读泵。读泵注册要求事件分发器已存在：须在 XGuiApplication
 *             创建之后调用（板级引导顺序：fbdev 显示驱动注册可在应用创建
 *             前，本函数在后）。重复调用幂等。
 * @return     true 设备已打开且读泵已挂链；false 设备不可用或分发器
 *             未就绪（无输入路径，应用仍可无输入运行）。
 */
bool XPlatformFbInput_register(void);

/** @brief 摘除读泵并关闭设备（未注册安全 no-op）。 */
void XPlatformFbInput_unregister(void);

/** @brief 查询触摸输入是否已注册可用。 */
bool XPlatformFbInput_isAvailable(void);

#endif /* XPLATFORM_FBINPUT_ON */

#endif /* defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPLATFORMFBINPUT_POSIX_H */
