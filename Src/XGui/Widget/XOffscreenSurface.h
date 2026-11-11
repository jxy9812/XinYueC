/******************************************************************************
 * @file       XOffscreenSurface.h
 * @brief      XOffscreenSurface 离屏表面类（对标 Qt 6.8 QOffscreenSurface
 *            : QObject, QSurface）。
 * @details    继承 XObject，提供离屏表面的公共 API 面：setFormat/format/
 *             requestedFormat（XSurfaceFormat 值类型）、size（XSize，只读
 *             ——Qt 6 已删 setSize，qoffscreensurface.h:37）、
 *             setScreen/screen（XScreen 借用，NULL 回落主屏）、
 *             create()/destroy()/isValid()、surfaceType()（返回
 *             XWindowSurfaceType，数值与 QSurface::SurfaceType 一致：
 *             Raster=0/OpenGL=1）、screenChanged(XScreen*) 信号。
 * @note       模块总开关 XWINDOW_ON && XSCREEN_ON && XSURFACEFORMAT_ON
 *             有效（依赖 XWindow/XScreen/XSurfaceFormat 类型）。
 * @note       无真实离屏渲染：create() 仅置 isValid 标志，destroy() 清除；
 *             平台离屏资源与 OpenGL 上下文接入为后续扩展。默认值与 Qt
 *             6.8.3 qoffscreensurface_p.h 对齐：surfaceType=OpenGL、
 *             size=(1,1)、format=XSurfaceFormat_create()、screen=NULL。
 * @note       已登记差异（需平台离屏表面子系统，当前 deferred）：
 *             ① Qt 的 format() 在 create() 后返回**平台实际格式**（可能
 *                被改写，qoffscreensurface.cpp:222-233；本机探针 create 后
 *                depth -1 vs 请求 24），本框架无平台表面 ⇒
 *                format() 恒等于 requestedFormat()；
 *             ② Qt 暴露 handle()（QPlatformOffscreenSurface*）与
 *                nativeInterface()（qoffscreensurface.h:45-47），本框架
 *                无平台表面对象可暴露，故不伪造这两个接口；
 *             ③ 创建/销毁命名为 createSurface()/destroy()（Qt 为
 *                create()/destroy()）：create 已被对象工厂宏
 *                XOffscreenSurface_create 占用，属仓库构造命名规矩。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XOFFSCREENSURFACE_H
#define XOFFSCREENSURFACE_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XObject.h"
#include "XGeometry.h"
#if XSURFACEFORMAT_ON
#include "XSurfaceFormat.h"
#endif /* XSURFACEFORMAT_ON */
#if XSCREEN_ON
#include "XScreen.h"
#endif /* XSCREEN_ON */
#if XWINDOW_ON
#include "XWindow.h"
#endif /* XWINDOW_ON */

#if XWINDOW_ON && XSCREEN_ON && XSURFACEFORMAT_ON

XCLASS_DEFINE_BEGING(XOffscreenSurface)
XCLASS_DEFINE_EXTEND_END(XOffscreenSurface, XObject)

/**
 * @brief      XOffscreenSurface 离屏表面对象；m_class 必须是第一个成员。
 * @details    全部为值/借用字段：m_format（值）、m_size（值）、
 *             m_screen（借用）、m_isValid（create 置真/destroy 置假）。
 */
typedef struct XOffscreenSurface
{
    XObject m_class;          /**< 基类成员；必须是第一个。 */
    XScreen* m_screen;        /**< 屏幕（借用；对标 screen）。 */
    XSurfaceFormat m_format;  /**< 表面格式（值类型；≥64B，专项设计后再动）。 */
    XSize m_size;             /**< 表面尺寸（值类型）。 */
    /* [内存治理 2026-10-11] 标志位字：单 bool 位域。 */
    uint32_t m_isValid : 1;   /**< 是否已创建（对标 isValid）。 */
    uint32_t m_offscreenFlagsUnused : 31; /**< 保留位。 */
} XOffscreenSurface;

/**
 * @brief      XOffscreenSurface 类虚函数表初始化（对标 Qt 同名接口）。
 * @return     共享 XVtable 指针。
 */
XVtable* XOffscreenSurface_class_init(void);

/**
 * @brief      初始化 XOffscreenSurface（对标 QOffscreenSurface 构造）。
 * @param      self 目标对象指针；不可为 NULL。
 * @return     无返回值。
 */
void XOffscreenSurface_init(XOffscreenSurface* self);
#define XOffscreenSurface_create() XOffscreenSurface_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
/**
 * @brief      使用指定内存类型创建 XOffscreenSurface。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XOffscreenSurface* XOffscreenSurface_create_ex(XMemoryType memory);

/**
 * @brief      设置表面格式（对标 QOffscreenSurface::setFormat）。
 * @details    Qt 语义（qoffscreensurface.cpp:189-195）：本函数**只记请求
 *             格式**（requestedFormat），真实格式在 create() 时由平台解析；
 *             create 之后调用不会重新解析（本框架无平台表面，故 format()
 *             恒等于 requestedFormat()）。
 * @param      self 目标表面。
 * @param      format 表面格式（值拷贝）。
 * @return     无返回值。
 */
void XOffscreenSurface_setFormat(XOffscreenSurface* self, XSurfaceFormat format);
/**
 * @brief      获取表面格式（对标 QOffscreenSurface::format）。
 * @details    Qt 返回**实际**格式（创建后由平台解析，可能不同于请求
 *             格式，qoffscreensurface.cpp:222-233）；本框架没有真实平台
 *             表面，故恒返回 setFormat 的请求格式（与 requestedFormat
 *             同值）——这是已知差异，见交付 gaps。
 * @param      self 目标表面；可为 NULL。
 * @return     表面格式值；无效时返回默认格式。
 */
XSurfaceFormat XOffscreenSurface_format(const XOffscreenSurface* self);
/**
 * @brief      获取请求的表面格式（对标 QOffscreenSurface::requestedFormat）。
 * @details    Qt 语义（qoffscreensurface.cpp:207-211）：返回 setFormat 设置
 *             的值，与平台是否兑现无关。
 * @param      self 目标表面；可为 NULL。
 * @return     请求格式值；无效时返回默认格式。
 */
XSurfaceFormat XOffscreenSurface_requestedFormat(
    const XOffscreenSurface* self);
/**
 * @brief      获取表面尺寸（对标 QOffscreenSurface::size）。
 * @details    Qt 的尺寸来自平台表面（qoffscreensurface.cpp:237-241 返回
 *             `d->size`），缺省 (1,1)；Qt 6 已**没有** setSize（Qt 5 遗留），
 *             本类同步删除该 setter——尺寸只读。
 * @param      self 目标表面；可为 NULL。
 * @return     表面尺寸；无效时返回 (1,1)（Qt 默认）。
 */
XSize XOffscreenSurface_size(const XOffscreenSurface* self);
/**
 * @brief      设置屏幕（对标 QOffscreenSurface::setScreen）。
 * @details    Qt 语义（qoffscreensurface.cpp:259-282）：传 NULL 时回落到
 *             主屏（`newScreen = QGuiApplication::primaryScreen()`）；
 *             屏幕**实际变化**时发射 screenChanged(screen)（同屏重复设置
 *             不发射，本机 Qt 6.8.0 实测）。已创建时 Qt 会 destroy+create
 *             重建平台表面——本框架无平台表面，仅改记录并更新 isValid
 *             不变。
 * @param      self 目标表面。
 * @param      screen 屏幕借用指针；可为 NULL（回落主屏）。
 * @return     无返回值。
 */
void XOffscreenSurface_setScreen(XOffscreenSurface* self, XScreen* screen);
/**
 * @brief      获取屏幕（对标 QOffscreenSurface::screen）。
 * @details    Qt 构造即把屏幕记为主屏（`if (!d->screen) d->screen =
 *             QGuiApplication::primaryScreen();`，qoffscreensurface.cpp:
 *             78-82），故 screen() 恒非 NULL（本机 Qt 6.8.0 实测
 *             screen()==primaryScreen()）。
 * @param      self 目标表面；可为 NULL。
 * @return     屏幕借用指针；无主屏（无 GUI 应用）时返回 NULL。
 */
XScreen* XOffscreenSurface_screen(const XOffscreenSurface* self);
/**
 * @brief      创建离屏表面（对标 QOffscreenSurface::create）。
 * @note       无真实离屏渲染：仅置 isValid 标志。
 * @param      self 目标表面。
 * @return     无返回值。
 */
void XOffscreenSurface_createSurface(XOffscreenSurface* self);
/**
 * @brief      销毁离屏表面（对标 QOffscreenSurface::destroy）。
 * @note       无真实离屏渲染：仅清除 isValid 标志。
 * @param      self 目标表面。
 * @return     无返回值。
 */
void XOffscreenSurface_destroy(XOffscreenSurface* self);
/**
 * @brief      查询离屏表面是否有效（对标 QOffscreenSurface::isValid）。
 * @param      self 目标表面；可为 NULL。
 * @return     create 后返回 true；无效返回 false。
 */
bool XOffscreenSurface_isValid(const XOffscreenSurface* self);
/** @brief screenChanged(XScreen*) 信号（对标
 *         QOffscreenSurface::screenChanged）。
 * @details setScreen 实际换屏时真发射（同屏重复设置不发射，对齐
 *          qoffscreensurface.cpp:264-277 的 `if (newScreen != d->screen)`）；
 *          self 为 NULL 时只返回信号标识，不发射。
 * @param self 目标表面；可为 NULL。
 * @param screen 新的屏幕借用指针（可为 NULL）。
 * @return 不透明的信号标识（非 NULL）；供 XObject_connect 使用。
 */
void* XOffscreenSurface_screenChanged_signal(XOffscreenSurface* self,
                                             XScreen* screen);
/**
 * @brief      获取表面类型（对标 QOffscreenSurface::surfaceType）。
 * @details    返回 XWindowSurfaceType（数值与 QSurface::SurfaceType 一致）；
 *             默认 XWindowSurface_OpenGL（Qt 6.8 默认）。
 * @param      self 目标表面；可为 NULL。
 * @return     表面类型；无效时返回 XWindowSurface_OpenGL。
 */
XWindowSurfaceType XOffscreenSurface_surfaceType(const XOffscreenSurface* self);

#endif /* XWINDOW_ON && XSCREEN_ON && XSURFACEFORMAT_ON */

#endif /* XGUI_ON */

#endif /* XOFFSCREENSURFACE_H */
