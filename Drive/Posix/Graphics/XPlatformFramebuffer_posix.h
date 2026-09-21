/******************************************************************************
 * @file       XPlatformFramebuffer_posix.h
 * @brief      Linux fbdev 显示驱动模板（远端规划 §23.4 方向 2）：
 *             /dev/fb0 + mmap 直写 + FBIOPAN_DISPLAY 翻页。
 * @details    为嵌入式 RGB565 面板的"软件渲染直写 framebuffer"铺路：
 *             面板像素接口出厂固定（典型 RGB565，每像素 2 字节），没有
 *             窗口系统，进程直接 mmap 帧缓冲写入、FBIOPAN_DISPLAY 翻页、
 *             DMA scanout 前做 cache 同步、FBIO_WAITFORVSYNC 防撕裂。
 *             本文件实现 XPlatformDisplayDriver 契约（6 钩子 ops 表），
 *             经 XPlatformDisplayDriver_register 注册接入，与既有 posix
 *             窗口后端（X11）互斥共存：注册显式发生、X11 路径不查询
 *             显示驱动注册表，桌面构建行为零变化。
 * @note       编译门控：__linux__ && XGUI_ON && XPLATFORM_FBDEV_ON
 *             （默认 0）。无 /dev/fb 的环境（如开发机桌面）全部路径可
 *             编译，register 内部 probe /dev/fb0 失败即返回 false，不
 *             注册、不占用资源；真机行为以本模板 + 板级 ioctl 扩展点
 *             验证。设备节点经 XPLATFORM_FBDEV_DEVICE 宏定制（默认
 *             "/dev/fb0"），内存体系：驱动无堆分配，状态全部静态 +
 *             mmap 设备映射（XMemory 管辖堆内存，此处无堆可管）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPLATFORMFRAMEBUFFER_POSIX_H
#define XPLATFORMFRAMEBUFFER_POSIX_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XPlatformDisplayDriver.h"

#if defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON

/** @brief fbdev 设备节点默认值（板级可在编译选项覆盖为 /dev/fb1 等）。 */
#ifndef XPLATFORM_FBDEV_DEVICE
#define XPLATFORM_FBDEV_DEVICE "/dev/fb0"
#endif

/**
 * @brief      探测并注册 fbdev 显示驱动（板级启动代码显式调用一次）。
 * @details    流程：open(XPLATFORM_FBDEV_DEVICE) ->
 *             FBIOGET_FSCREENINFO/FBIOGET_VSCREENINFO -> mmap ->
 *             识别面板格式 -> XPlatformDisplayDriver_register。任一步
 *             失败即整体不可用并释放已取得资源（桌面/无 fb 环境的预期
 *             路径，返回 false）。重复调用幂等。
 * @return     true 已注册为活动显示驱动；false 不可用或已有活动驱动。
 */
bool XPlatformFramebuffer_register(void);

/** @brief 注销 fbdev 显示驱动并释放设备/映射（未注册安全 no-op）。 */
void XPlatformFramebuffer_unregister(void);

/** @brief 查询 fbdev 驱动是否已注册可用。 */
bool XPlatformFramebuffer_isAvailable(void);

/** @brief 返回 fbdev 驱动 ops 表（未注册也返回表本体，供直接调用前自检）。 */
const XPlatformDisplayDriverOps* XPlatformFramebuffer_driverOps(void);

/**
 * @brief      板级便捷入口：按设备路径探测并注册 fbdev 显示驱动
 *             （probe + register 一步完成，嵌入式 main 在 GUI 初始化前
 *             调用；桌面默认不调用，零影响）。
 * @details    成功后公共层消费链接管：后备存储按面板格式协商分配
 *             （Src/XGui/Platform/XPlatformBackingStore.c）、present 经
 *             直写路径上屏（Drive/Posix/Graphics/
 *             XPlatformBackingStore_posix.c），X11 窗口创建被拒绝
 *             （单屏互斥告警）。重复调用幂等；设备路径仅首次 probe 前
 *             生效，NULL/空串用编译期宏 XPLATFORM_FBDEV_DEVICE。
 * @param      device fb 设备节点（如 "/dev/fb1"）；可为 NULL。
 * @return     true 已注册为活动显示驱动；false 不可用或已有活动驱动。
 */
bool XPlatformNativeWindow_useFramebufferDriver(const char* device);

#endif /* defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPLATFORMFRAMEBUFFER_POSIX_H */
