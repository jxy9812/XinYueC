/******************************************************************************
 * @file       XPlatformFramebuffer_drm.h
 * @brief      Linux DRM/KMS 显示驱动（无 fbdev 环境的直写后端）：
 *             /dev/dri/cardN + dumb buffer + mmap 直写 + WAIT_VBLANK。
 * @details    现代嵌入式 BSP（如 RK3506 Buildroot 内核 6.1）默认关闭
 *             CONFIG_DRM_FBDEV_EMULATION，/dev/fb* 不存在、仅暴露
 *             /dev/dri/card0；XPlatformFramebuffer_posix.c 的 fbdev 后
 *             端在此类板上 probe 恒败。本文件实现 XPlatformDisplayDriver
 *             契约（6 钩子 ops 表）的 DRM/KMS 端：
 *             - probe：open(card) -> MODE_GETRESOURCES/GETCONNECTOR/
 *               GETENCODER -> 取 connected 连接器首选模式 ->
 *               MODE_CREATE_DUMB（16bpp 优先、失败退 32bpp）-> MODE_ADDFB
 *               -> MODE_SETCRTC（进程 open 卡即成为 DRM master，无需
 *               附加条件；无合成器环境恒可获取）-> MODE_MAP_DUMB + mmap；
 *             - pan：单 dumb buffer 直写即可见，恒 no-op（双缓冲页翻
 *               转需 PAGE_FLIP 事件泵，后续批落地）；
 *             - cacheSync：同 fbdev 模板口径（ARM 私有 cacheflush 系统
 *               调用，失败不谎报）；
 *             - waitVsync：DRM_IOCTL_WAIT_VBLANK（RELATIVE 1 帧阻塞等
 *               待，真硬件垂直同步）；
 *             - stride：dumb buffer pitch（硬件对齐可能大于 width*bpp）。
 *             与 fbdev 后端互斥共存：注册表（XPlatformDisplayDriver_
 *             register，现实现暂随 fbdev 编译单元）先注册者生效；板级
 *             引导先试 DRM、失败再试 fbdev（见 XGuiWindowDemo）。
 * @note       编译门控：__linux__ && XGUI_ON && XPLATFORM_FBDEV_ON &&
 *             XPLATFORM_DRM_ON（默认 0，注册表符号随 fbdev 编译单元，
 *             故 FBDEV 开关必须同开）。无 /dev/dri 的环境全部路径可编
 *             译，register 内部 probe 失败即返回 false，不注册、不占用
 *             资源。设备节点经 XPLATFORM_DRM_DEVICE 宏定制（默认
 *             "/dev/dri/card0"）。内核 UAPI 结构就地声明（对标 fbdev
 *             后端不经 libdrm 的零外链原则）。内存体系：驱动无堆分配，
 *             状态全部静态 + mmap 设备映射。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPLATFORMFRAMEBUFFER_DRM_H
#define XPLATFORMFRAMEBUFFER_DRM_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XPlatformDisplayDriver.h"

#if defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON

/** @brief DRM 设备节点默认值（板级可在编译选项覆盖为 /dev/dri/card1）。 */
#ifndef XPLATFORM_DRM_DEVICE
#define XPLATFORM_DRM_DEVICE "/dev/dri/card0"
#endif

/** @brief 面板 devicePixelRatio（缺省 1.0f=逐位直通，口径同 fbdev 后端
 *         XPLATFORM_FBDEV_DEVICE_PIXEL_RATIO：>1 必须与 present 放大管
 *         道同批落地，dpr 严禁由面板 mm 反推）。 */
#ifndef XPLATFORM_DRM_DEVICE_PIXEL_RATIO
#define XPLATFORM_DRM_DEVICE_PIXEL_RATIO 1.0f
#endif

/**
 * @brief      探测并注册 DRM/KMS 显示驱动（板级启动代码显式调用一次）。
 * @details    流程见文件头。任一步失败即整体不可用并释放已取得资源
 *             （无 DRM 卡的环境预期路径，返回 false）。重复调用幂等。
 * @return     true 已注册为活动显示驱动；false 不可用或已有活动驱动。
 */
bool XPlatformDrm_register(void);

/** @brief 注销 DRM 显示驱动并释放设备/映射/FB/dumb（未注册安全 no-op）。 */
void XPlatformDrm_unregister(void);

/** @brief 查询 DRM 驱动是否已注册可用。 */
bool XPlatformDrm_isAvailable(void);

/** @brief 返回 DRM 驱动 ops 表（未注册也返回表本体，供直接调用前自检）。 */
const XPlatformDisplayDriverOps* XPlatformDrm_driverOps(void);

/**
 * @brief      板级便捷入口：按设备路径探测并注册 DRM 显示驱动（probe +
 *             register 一步完成；嵌入式 main 在 GUI 初始化前调用）。
 * @details    重复调用幂等；设备路径仅首次 probe 前生效，NULL/空串用
 *             编译期宏 XPLATFORM_DRM_DEVICE。
 * @param      device DRM 卡节点（如 "/dev/dri/card0"）；可为 NULL。
 * @return     true 已注册为活动显示驱动；false 不可用或已有活动驱动。
 */
bool XPlatformNativeWindow_useDrmDriver(const char* device);

#endif /* defined(__linux__) && XGUI_ON && XPLATFORM_FBDEV_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPLATFORMFRAMEBUFFER_DRM_H */
