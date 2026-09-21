/******************************************************************************
 * @file       XPlatformDisplayDriver.h
 * @brief      XPlatformDisplayDriver 显示驱动公共契约（对标 Qt 6.8
 *             QPlatformScreen 格式协商 + eglfs/linuxfb 设备集成的
 *             单屏直写模型）。
 * @details    桌面平台（X11/Win32）的表面格式与上屏由窗口系统决定，
 *             本契约不参与；嵌入式单屏平台（fbdev/KFB/RKFB 等直写
 *             framebuffer 的场景）没有窗口系统，需要显示驱动向框架
 *             报告面板能力并完成翻页/缓存同步/垂直同步。对标关系：
 *             - probe            <- eglfs 设备插件的 open/probe（
 *                                  QEglFSDeviceIntegration::createScreen
 *                                  前的探测；linuxfb 的 openFbDevice）；
 *             - formatNegotiate  <- QPlatformScreen::format() 报告屏幕
 *                                  原生格式 + QBackingStore 按其协商缓冲
 *                                  格式（本框架表面格式的编译期选择器
 *                                  XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16
 *                                  已存在于 XGuiConfig.h，协商结果与之
 *                                  对照即可判定"直写 or 转换"）；
 *             - pan              <- linuxfb 后端的 FBIOPAN_DISPLAY 翻页
 *                                  （Qt eglfs 的 swapBuffers 等价物）；
 *             - cacheSync        <- 无窗口系统平台特有：DMA scanout 前
 *                                  对 CPU 侧脏数据做 cache clean/
 *                                  invalidate（Qt 桌面路径由驱动统一
 *                                  处理，无需暴露）；为什么必须有：非
 *                                  一致性内存的板子上显示控制器直接读
 *                                  DRAM，CPU 只写进 cache 的内容它看
 *                                  不见，会出现"画了不显示/闪旧帧"；
 *             - waitVsync        <- 对标 eglfs fence/fbdev vblank 等待
 *                                  （QPlatformBackingStore 的撕裂防护）；
 *             - stride           <- QImage::bytesPerLine 的硬件版：面板
 *                                  行距取 fb_fix_screeninfo.line_length
 *                                  （硬件对齐可能大于 width*bpp）。
 *             注册式互斥（为什么）：进程内至多一个活动显示驱动（单屏
 *             嵌入式模型，对标 eglfs 单设备集成）；先注册者生效，后
 *             注册者被拒绝。X11/Win32 窗口路径不查询本注册表，桌面
 *             构建即使链接了驱动实现也不改变任何既有行为；驱动是否
 *             注册由板级启动代码显式调用（fbdev 实现另见
 *             Drive/Posix/Graphics/XPlatformFramebuffer_posix.h）。
 * @note       编译开关 XPLATFORM_FBDEV_ON（默认 0，#ifndef 兜底于此，
 *             正式取值经 CMake -DXPLATFORM_FBDEV_ON=1 注入）：置 0 时
 *             本契约整体裁剪，零新增 ABI 面；契约头不含任何平台 API
 *             头。本头暂只声明注册表入口；注册表实现当前随唯一驱动
 *             XPlatformFramebuffer_posix.c 编译，后续驱动增多再上提
 *             公共层 .c（文件所有权另行批次处理）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPLATFORMDISPLAYDRIVER_H
#define XPLATFORMDISPLAYDRIVER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XImageFormat.h"

/* 模块开关：XPLATFORM_FBDEV_ON 默认 0。#ifndef 兜底保证任何包含顺序
 * 下取值确定；CMake 裁剪开关转发（根 CMakeLists.txt）负责把命令行
 * -DXPLATFORM_FBDEV_ON=ON 转成全局编译定义。 */
#ifndef XPLATFORM_FBDEV_ON
#define XPLATFORM_FBDEV_ON 0
#endif

#if XGUI_ON && XPLATFORM_FBDEV_ON

/** @brief 驱动契约 ABI 版本（ops 结构布局变更时递增，供驱动侧自检）。 */
#define XPLATFORM_DISPLAY_DRIVER_ABI_VERSION 1u

/**
 * @brief      显示驱动 probe 结果（面板能力快照，对标 QPlatformScreen
 *             的 geometry/depth/format 三件套）。
 * @note       m_frameBuffer 是设备映射地址，归驱动所有，调用方只借用
 *             直写；生命期到驱动注销为止。
 */
typedef struct XPlatformDisplayInfo
{
    int m_width;             /**< 可见宽度（像素，var.xres）。 */
    int m_height;            /**< 可见高度（像素，var.yres）。 */
    XImageFormat m_format;   /**< 面板扫描格式（无法识别时 Invalid）。 */
    int m_bitsPerPixel;      /**< 面板每像素位数（var.bits_per_pixel）。 */
    size_t m_stride;         /**< 硬件行距字节（fix.line_length）。 */
    void* m_frameBuffer;     /**< mmap 直写地址（驱动所有，借用）。 */
    size_t m_frameBufferSize;/**< 映射区总长（字节）。 */
    bool m_doubleBuffered;   /**< yres_virtual >= 2*yres，可后台翻页。 */
} XPlatformDisplayInfo;

/**
 * @brief      CPU cache 同步方向（DMA scanout 前 clean，为什么见文件头）。
 */
typedef enum XPlatformDisplayCacheMode
{
    XPlatformDisplayCache_Clean = 0,           /**< 写回脏行（CPU->内存）。 */
    XPlatformDisplayCache_Invalidate = 1,      /**< 失效行（内存->CPU）。 */
    XPlatformDisplayCache_CleanInvalidate = 2  /**< 先写回再失效。 */
} XPlatformDisplayCacheMode;

/**
 * @brief      显示驱动 ops 表（契约核心：6 钩子 + 元数据，全部必填）。
 * @note       所有回调在驱动未 probe 成功/未连接时必须安全返回 false
 *             或 0；实现内部自行持有状态，公共层只经 ops 表调用。
 */
typedef struct XPlatformDisplayDriverOps
{
    const char* m_name;   /**< 驱动名（如 "fbdev"），静态串，不释放。 */
    uint32_t m_abiVersion;/**< 必须为 XPLATFORM_DISPLAY_DRIVER_ABI_VERSION。 */
    /**
     * @brief  探测设备可用性并输出面板能力快照。
     * @param  outInfo 能力输出；可为 NULL（只探测不取值）。
     * @return true 设备可用且已就绪；false 不可用（无设备/权限/参数）。
     */
    bool (*probe)(XPlatformDisplayInfo* outInfo);
    /**
     * @brief      表面格式协商（对标 QPlatformScreen::format）。
     * @param      preferred 调用方期望的表面格式；Invalid 表示纯查询。
     * @param      outFormat 输出面板原生扫描格式；不可为 NULL。
     * @return     true 面板可用且 preferred 可直写（或 preferred 为
     *             Invalid 的纯查询）；false 不可用或需转换（面板格式
     *             仍写入 outFormat 供调用方建立转换路径）。
     */
    bool (*formatNegotiate)(XImageFormat preferred, XImageFormat* outFormat);
    /**
     * @brief  翻页/交换（对标 eglfs swapBuffers / linuxfb FBIOPAN_DISPLAY）。
     * @param  bufferIndex 目标缓冲号（0 起始）；单缓冲驱动只接受 0。
     * @return true 已提交翻页（不等同已上屏，上屏完成以 waitVsync 为准）。
     */
    bool (*pan)(int bufferIndex);
    /**
     * @brief      CPU cache 同步（DMA scanout 前对直写区域调用）。
     * @param      mode   同步方向（Clean/Invalidate/CleanInvalidate）。
     * @param      address 目标地址；NULL 表示整个帧缓冲。
     * @param      length 字节数；address 为 NULL 时忽略。
     * @return true 同步完成或不需同步（一致性内存）；false 失败。
     */
    bool (*cacheSync)(XPlatformDisplayCacheMode mode, void* address,
                      size_t length);
    /**
     * @brief  等待垂直同步/fence（撕裂防护；对标 eglfs fence 等待）。
     * @param  timeoutMilliseconds 超时上限；<=0 表示驱动默认上限。
     * @return true 已等到一次 vsync；false 超时或驱动不支持。
     */
    bool (*waitVsync)(int timeoutMilliseconds);
    /**
     * @brief      按格式取行距（字节）。
     * @param      width  行宽（像素）；<=0 返回 0。
     * @param      format 目标格式；面板扫描格式返回硬件行距，其余格式
     *             返回软件口径（XImageFormat_bytesPerLine）。
     * @return 行距字节数；参数非法或驱动不可用返回 0。
     */
    size_t (*stride)(int width, XImageFormat format);
} XPlatformDisplayDriverOps;

/* ==================== 注册表（进程内至多一个活动驱动） ==================== */

/**
 * @brief      注册显示驱动（先注册者生效，后注册者拒绝——单屏互斥）。
 * @param      ops 驱动 ops 表；函数指针集必须完整，静态生命期。
 * @return     true 注册成功；false 入参非法/ABI 版本不符/已有活动驱动。
 */
bool XPlatformDisplayDriver_register(const XPlatformDisplayDriverOps* ops);

/**
 * @brief      注销显示驱动（仅注册者本人可注销；未注册安全 no-op）。
 * @param      ops 之前注册成功的 ops 表；可为 NULL。
 */
void XPlatformDisplayDriver_unregister(const XPlatformDisplayDriverOps* ops);

/**
 * @brief      查询当前活动驱动（公共层经 ops 表消费，不感知平台差异）。
 * @return     活动 ops 表；未注册驱动返回 NULL。
 */
const XPlatformDisplayDriverOps* XPlatformDisplayDriver_active(void);

#endif /* XGUI_ON && XPLATFORM_FBDEV_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPLATFORMDISPLAYDRIVER_H */
