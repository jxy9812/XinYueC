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

#if XGUI_ON

#include "XOffscreenSurface.h"
#include "XGuiConfig.h"
#include "XMemory.h"
#include "XGeometry.h"

#if XWINDOW_ON && XSCREEN_ON && XSURFACEFORMAT_ON

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
    /* 对标 QOffscreenSurfacePrivate 构造初始化表：requestedFormat 以
       QSurfaceFormat::defaultFormat() 播种（qoffscreensurface_p.h 构造函数
       `requestedFormat(QSurfaceFormat::defaultFormat())`；本机 Qt 6.8.3
       offscreen 探针实测：setDefaultFormat(depth16/red5/ver3.2) 后新建
       QOffscreenSurface 的 requestedFormat() == defaultFormat()）。
       注意与 XSurfaceFormat_create() 的区别：后者是值构造，恒出厂默认、
       显式不吃进程默认格式（XSurfaceFormat.c:55-66 注释）。 */
    self->m_format = XSurfaceFormat_defaultFormat();
    XSize_init(&self->m_size, 1, 1);
    /* 对标 QOffscreenSurface 构造（qoffscreensurface.cpp:78-82）：
     * `d->screen = targetScreen; if (!d->screen) d->screen =
     *  QGuiApplication::primaryScreen();` —— 构造即把屏幕记为主屏，
     *  screen() 因此恒非 NULL（无 GUI 应用时主屏为空，保持 NULL）。
     * 本机 Qt 6.8.0 实测：`surface.screen() == app.primaryScreen()`。 */
    self->m_screen = XScreen_primaryScreen();
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
    /* Qt 6.8 format() 三分支（qoffscreensurface.cpp format()）：
     *   if (platformOffscreenSurface) return platform->format();
     *   if (offscreenWindow)          return window->format();
     *   return d->requestedFormat;
     * 本框架平台层无离屏表面格式回读通道：XPlatformOffscreenSurface 结构只有
     * nativeState/memory/width/height/valid（XPlatformGraphics.c:27-34），
     * XPlatformGraphicsDriver_createOffscreen 签名亦无格式出参
     * （XPlatformGraphics.h:238-241）；而绑定平台表面需改 create()/destroy()
     * 产品语义（本轮 §B18④ 明确不擅自改）⇒ 恒走第三分支 return requested。
     * 该「created 后返回平台实际格式」的剩余偏差登记于台账 §B18；平台层补出
     * 格式回读后，本函数应改为：created && 平台格式已解析 ⇒ 返回平台值。 */
    return self ? self->m_format : XSurfaceFormat_create();
}

XSurfaceFormat XOffscreenSurface_requestedFormat(
    const XOffscreenSurface* self)
{
    /* 对标 qoffscreensurface.cpp:207-211：返回 setFormat 设置的值。 */
    return self ? self->m_format : XSurfaceFormat_create();
}

XSize XOffscreenSurface_size(const XOffscreenSurface* self)
{
    XSize size;
    if (self) return self->m_size;
    XSize_init(&size, 1, 1);
    return size;
}

void XOffscreenSurface_setScreen(XOffscreenSurface* self, XScreen* screen)
{
    XScreen* target;
    XScreen* previous;
    XVarList* args;
    if (!self) return;
    /* 对标 qoffscreensurface.cpp:259-262：传 NULL 回落主屏。 */
    target = screen ? screen : XScreen_primaryScreen();
    previous = self->m_screen;
    if (target == previous) return; /* 同屏幂等：Qt 不发射 screenChanged */
    self->m_screen = target;
    /* 对标 qoffscreensurface.cpp:276 `emit screenChanged(newScreen)`。
     * XVar 宏对表达式取址 ⇒ 载荷先落局部再取址。 */
    if (((XObject*)self)->m_signalSlot) {
        args = XVarList_Create(XVar(XScreen*, target));
        if (args) {
            XObject_emitSignal((XObject*)self,
                               (size_t)XOffscreenSurface_screenChanged_signal,
                               args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        }
    }
}

XScreen* XOffscreenSurface_screen(const XOffscreenSurface* self)
{
    /* 对标 Qt：构造已记主屏，运行期不会为空（除非无主屏）。 */
    return self ? self->m_screen : NULL;
}

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

/* ==================== 信号 ==================== */

void* XOffscreenSurface_screenChanged_signal(XOffscreenSurface* self,
                                             XScreen* screen)
{
    XVarList* args;
    XScreen* payload;
    (void)screen;
    if (self && ((XObject*)self)->m_signalSlot) {
        /* XVar 宏对表达式取址：载荷先落局部。 */
        payload = screen;
        args = XVarList_Create(XVar(XScreen*, payload));
        if (args) {
            XObject_emitSignal((XObject*)self,
                               (size_t)XOffscreenSurface_screenChanged_signal,
                               args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        }
    }
    return (void*)(size_t)XOffscreenSurface_screenChanged_signal;
}

#endif /* XWINDOW_ON && XSCREEN_ON && XSURFACEFORMAT_ON */

#endif /* XGUI_ON */
