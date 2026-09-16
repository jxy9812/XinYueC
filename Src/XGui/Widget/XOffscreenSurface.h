/******************************************************************************
 * @file       XOffscreenSurface.h
 * @brief      XOffscreenSurface 离屏表面类（对标 Qt 6.8 QOffscreenSurface
 *            : QObject, QSurface）。
 * @details    继承 XObject，提供离屏表面的公共 API 面：setFormat/format
 *             （XSurfaceFormat 值类型）、setSize/size（XSize）、
 *             setScreen/screen（XScreen 借用）、create()/destroy()/
 *             isValid()、surfaceType()（返回 XWindowSurfaceType，数值与
 *             QSurface::SurfaceType 一致：Raster=0/OpenGL=1）。
 * @note       模块总开关 XWINDOW_ON && XSCREEN_ON && XSURFACEFORMAT_ON
 *             有效（依赖 XWindow/XScreen/XSurfaceFormat 类型）。
 * @note       无真实离屏渲染：create() 仅置 isValid 标志，destroy() 清除；
 *             平台离屏资源与 OpenGL 上下文接入为后续扩展。默认值与 Qt
 *             6.8.3 qoffscreensurface_p.h 对齐：surfaceType=OpenGL、
 *             size=(1,1)、format=XSurfaceFormat_create()、screen=NULL。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XOFFSCREENSURFACE_H
#define XOFFSCREENSURFACE_H
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
    XSurfaceFormat m_format;  /**< 表面格式（值类型）。 */
    XSize m_size;             /**< 表面尺寸（值类型）。 */
    XScreen* m_screen;        /**< 屏幕（借用；对标 screen）。 */
    bool m_isValid;           /**< 是否已创建（对标 isValid）。 */
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
#define XOffscreenSurface_deinit_base(self) XClass_deinit_base((XClass*)(self))
#define XOffscreenSurface_delete_base(self) XClass_delete_base((XClass*)(self))

/**
 * @brief      设置表面格式（对标 QOffscreenSurface::setFormat）。
 * @param      self 目标表面。
 * @param      format 表面格式（值拷贝）。
 * @return     无返回值。
 */
void XOffscreenSurface_setFormat(XOffscreenSurface* self, XSurfaceFormat format);
/**
 * @brief      获取表面格式（对标 QOffscreenSurface::format）。
 * @param      self 目标表面；可为 NULL。
 * @return     表面格式值；无效时返回默认格式。
 */
XSurfaceFormat XOffscreenSurface_format(const XOffscreenSurface* self);
/**
 * @brief      设置表面尺寸（对标 QOffscreenSurface::setSize）。
 * @param      self 目标表面。
 * @param      size 表面尺寸。
 * @return     无返回值。
 */
void XOffscreenSurface_setSize(XOffscreenSurface* self, XSize size);
/**
 * @brief      获取表面尺寸（对标 QOffscreenSurface::size）。
 * @param      self 目标表面；可为 NULL。
 * @return     表面尺寸；无效时返回 (1,1)（Qt 默认）。
 */
XSize XOffscreenSurface_size(const XOffscreenSurface* self);
/**
 * @brief      设置屏幕（对标 QOffscreenSurface::setScreen）。
 * @param      self 目标表面。
 * @param      screen 屏幕借用指针；可为 NULL 清除。
 * @return     无返回值。
 */
void XOffscreenSurface_setScreen(XOffscreenSurface* self, XScreen* screen);
/**
 * @brief      获取屏幕（对标 QOffscreenSurface::screen）。
 * @param      self 目标表面；可为 NULL。
 * @return     屏幕借用指针；未设置或无效返回 NULL。
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
/**
 * @brief      获取表面类型（对标 QOffscreenSurface::surfaceType）。
 * @details    返回 XWindowSurfaceType（数值与 QSurface::SurfaceType 一致）；
 *             默认 XWindowSurface_OpenGL（Qt 6.8 默认）。
 * @param      self 目标表面；可为 NULL。
 * @return     表面类型；无效时返回 XWindowSurface_OpenGL。
 */
XWindowSurfaceType XOffscreenSurface_surfaceType(const XOffscreenSurface* self);

#endif /* XWINDOW_ON && XSCREEN_ON && XSURFACEFORMAT_ON */

#endif /* XOFFSCREENSURFACE_H */
