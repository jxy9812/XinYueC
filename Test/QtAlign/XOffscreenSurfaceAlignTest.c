/**
 * @file       XOffscreenSurfaceAlignTest.c
 * @brief      XOffscreenSurface × Qt 6.8 QOffscreenSurface 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtGui/qoffscreensurface.h
 *             与 Qt 6.8 源码 src/gui/kernel/qoffscreensurface.cpp
 *             （jsdelivr v6.8.0 原文）。关键行为用本机 Qt 6.8.0
 *             （QT_QPA_PLATFORM=offscreen）探针实测：
 *             - 默认 size=(1,1)、surfaceType=1（OpenGL）、isValid=false
 *               （探针「size=1x1 type=1 valid=0」）；
 *             - setFormat 只记**请求**格式（qoffscreensurface.cpp:189-195）：
 *               create() 之前 format()==requestedFormat()（探针 depth=24/24）；
 *               create() 之后 format() 是平台解析后的实际格式（探针 depth=-1），
 *               本框架无平台表面 ⇒ 实际恒等请求（已登记差异）；
 *             - screen() 构造即回落主屏（qoffscreensurface.cpp:78-82，
 *               探针 screen()==primaryScreen()）；
 *             - setScreen 同屏重复设置**不发射** screenChanged
 *               （qoffscreensurface.cpp:264-277 的 `if (newScreen != d->screen)`，
 *               探针 sig 恒 0）；
 *             - Qt 6 已无 setSize（qoffscreensurface.h:36-42 无该成员），
 *               本类同步删除该 setter（见交付 removedApis）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWINDOW_ON && XSCREEN_ON && XSURFACEFORMAT_ON

#include "XOffscreenSurface.h"
#include "XSurfaceFormat.h"
#include "XScreen.h"
#include "XObject.h"
#include "XClass.h"
#include "XVarList.h"

/* ==================== 信号采样 ==================== */

static int g_osScreenChanged;
static XScreen* g_osLastScreen;

static void xosTestReset(void)
{
    g_osScreenChanged = 0;
    g_osLastScreen = NULL;
}

static void xosTestScreenSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XScreen*, screen);
    ++g_osScreenChanged;
    g_osLastScreen = (XScreen*)screen;
}

/* ==================== ① 默认值（Qt 探针 1x1 / OpenGL / false） ========== */

XQ_TEST(XOffscreenSurface, constructorDefaults)
{
    XOffscreenSurface* surface = XOffscreenSurface_create();
    XQ_REQUIRE(surface != NULL);

    /* 探针：size=1x1、surfaceType=1（OpenGL）、isValid=0。 */
    XQ_EQ_I64(XOffscreenSurface_size(surface).width, 1);
    XQ_EQ_I64(XOffscreenSurface_size(surface).height, 1);
    XQ_EQ_I64((int)XOffscreenSurface_surfaceType(surface),
              (int)XWindowSurface_OpenGL);
    XQ_CHECK(!XOffscreenSurface_isValid(surface));
    /* 构造即记主屏（qoffscreensurface.cpp:78-82）：无主屏（无 GUI 应用）
     * 时为 NULL，有主屏则恒等于 primaryScreen——两种环境都成立。 */
    XQ_CHECK(XOffscreenSurface_screen(surface) == XScreen_primaryScreen());

    /* NULL 安全。 */
    XQ_EQ_I64(XOffscreenSurface_size(NULL).width, 1);
    XQ_EQ_I64(XOffscreenSurface_size(NULL).height, 1);
    XQ_CHECK(!XOffscreenSurface_isValid(NULL));
    XQ_EQ_I64((int)XOffscreenSurface_surfaceType(NULL),
              (int)XWindowSurface_OpenGL);
    XQ_CHECK(XOffscreenSurface_screen(NULL) == NULL);

    XClassDelete((XClass*)surface);
}

/* ==================== ② setFormat → format / requestedFormat ============ */

XQ_TEST(XOffscreenSurface, formatVersusRequestedFormat)
{
    XOffscreenSurface* surface = XOffscreenSurface_create();
    XSurfaceFormat fmt;
    XSurfaceFormat gotReq;
    XSurfaceFormat gotFmt;
    XQ_REQUIRE(surface != NULL);

    /* setFormat 只记请求格式；create() 之前 format() 与 requestedFormat()
     * 同值（Qt 探针 depth=24/24）。 */
    fmt = XSurfaceFormat_create();
    XSurfaceFormat_setDepthBufferSize(&fmt, 24);
    XOffscreenSurface_setFormat(surface, fmt);

    gotReq = XOffscreenSurface_requestedFormat(surface);
    gotFmt = XOffscreenSurface_format(surface);
    XQ_EQ_I64(XSurfaceFormat_depthBufferSize(&gotReq), 24);
    XQ_EQ_I64(XSurfaceFormat_depthBufferSize(&gotFmt), 24);

    /* create/destroy 只翻 isValid（无平台表面 ⇒ 实际格式恒等请求格式）。 */
    XOffscreenSurface_createSurface(surface);
    XQ_CHECK(XOffscreenSurface_isValid(surface));
    gotReq = XOffscreenSurface_requestedFormat(surface);
    XQ_EQ_I64(XSurfaceFormat_depthBufferSize(&gotReq), 24);
    XOffscreenSurface_destroy(surface);
    XQ_CHECK(!XOffscreenSurface_isValid(surface));

    /* NULL 安全：返回缺省格式对象（与 XSurfaceFormat_create() 同值）。 */
    {
        XSurfaceFormat def = XOffscreenSurface_requestedFormat(NULL);
        XSurfaceFormat ref = XSurfaceFormat_create();
        XQ_EQ_I64(XSurfaceFormat_depthBufferSize(&def),
                  XSurfaceFormat_depthBufferSize(&ref));
        XQ_EQ_I64(XSurfaceFormat_stencilBufferSize(&def),
                  XSurfaceFormat_stencilBufferSize(&ref));
    }

    XClassDelete((XClass*)surface);
}

/* ==================== ③ setScreen：同屏幂等不发射 screenChanged ======== */

XQ_TEST(XOffscreenSurface, setScreenEmitsOnlyOnRealChange)
{
    XOffscreenSurface* surface = XOffscreenSurface_create();
    XQ_REQUIRE(surface != NULL);
    xosTestReset();
    XObject_connect((XObject*)surface,
                      XSignal(XOffscreenSurface_screenChanged_signal),
                      (XObject*)surface, xosTestScreenSlot,
                      XConnectionType_Direct);

    /* 传 NULL 回落主屏（qoffscreensurface.cpp:259-262）：构造时已是主屏
     * ⇒ 目标与当前相同 ⇒ 不发射（Qt 探针 sig 恒 0）。 */
    XOffscreenSurface_setScreen(surface, NULL);
    XQ_EQ_I64(g_osScreenChanged, 0);
    XQ_CHECK(XOffscreenSurface_screen(surface) == XScreen_primaryScreen());

    /* 重复设置同一屏幕仍不发射。 */
    XOffscreenSurface_setScreen(surface, XOffscreenSurface_screen(surface));
    XQ_EQ_I64(g_osScreenChanged, 0);

    /* 信号标识本身可用（self==NULL 只返回标识；供 XObject_connect）。 */
    XQ_CHECK_PTR(XOffscreenSurface_screenChanged_signal(surface, NULL));
    XQ_CHECK_PTR(XOffscreenSurface_screenChanged_signal(NULL, NULL));

    XClassDelete((XClass*)surface);
}

#endif /* XGUI_ON && XWINDOW_ON && XSCREEN_ON && XSURFACEFORMAT_ON */
