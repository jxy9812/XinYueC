/******************************************************************************
 * @file       XOffscreenSurface.c
 * @brief      离屏表面类实现（对标 Qt 6.8 QOffscreenSurface 公共 API）。
 * @details    与同名头文件的公共 API 一一对应。字段存储 + isValid 标志：
 *             create() 置真、destroy() 置假；无真实离屏渲染。默认值与
 *             Qt 6.8.3 qoffscreensurface_p.h 对齐（surfaceType=OpenGL、
 *             size=(1,1)）。
 * @note       本文件不依赖任何平台 API。
 * @author     XinYueC 团队
 */

#include "XGuiConfig.h"
#include "XMemory.h"
#include "XGeometry.h"

#if XWINDOW_ON && XSCREEN_ON && XSURFACEFORMAT_ON

#include "XOffscreenSurface.h"

/* ==================== 类与实例生命周期 ==================== */

XVtable* XOffscreenSurface_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XOffscreenSurface)
    XVTABLE_INHERIT_XCLASS(XObject);
    return XVTABLE_DEFAULT;
}

void XOffscreenSurface_init(XOffscreenSurface* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XOffscreenSurface);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_format = XSurfaceFormat_create();
    XSize_init(&self->m_size, 1, 1);
    self->m_screen = NULL;
    self->m_isValid = false;
}

XOffscreenSurface* XOffscreenSurface_create_ex(XMemoryType memory)
{
    XOffscreenSurface* self =
        (XOffscreenSurface*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XOffscreenSurface_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 实例属性 ==================== */

void XOffscreenSurface_setFormat(XOffscreenSurface* self, XSurfaceFormat format)
{ if (self) self->m_format = format; }

XSurfaceFormat XOffscreenSurface_format(const XOffscreenSurface* self)
{
    return self ? self->m_format : XSurfaceFormat_create();
}

void XOffscreenSurface_setSize(XOffscreenSurface* self, XSize size)
{ if (self) self->m_size = size; }

XSize XOffscreenSurface_size(const XOffscreenSurface* self)
{
    XSize size;
    if (self) return self->m_size;
    XSize_init(&size, 1, 1);
    return size;
}

void XOffscreenSurface_setScreen(XOffscreenSurface* self, XScreen* screen)
{ if (self) self->m_screen = screen; }

XScreen* XOffscreenSurface_screen(const XOffscreenSurface* self)
{ return self ? self->m_screen : NULL; }

void XOffscreenSurface_createSurface(XOffscreenSurface* self)
{ if (self) self->m_isValid = true; }

void XOffscreenSurface_destroy(XOffscreenSurface* self)
{ if (self) self->m_isValid = false; }

bool XOffscreenSurface_isValid(const XOffscreenSurface* self)
{ return self ? self->m_isValid : false; }

XWindowSurfaceType XOffscreenSurface_surfaceType(const XOffscreenSurface* self)
{
    (void)self;
    /* Qt 6.8 QOffscreenSurfacePrivate 默认 OpenGLSurface。 */
    return XWindowSurface_OpenGL;
}

#endif /* XWINDOW_ON && XSCREEN_ON && XSURFACEFORMAT_ON */
