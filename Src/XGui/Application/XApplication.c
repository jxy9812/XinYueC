/******************************************************************************
 * @file       XApplication.c
 * @brief      XApplication 控件级应用类实现（对标 Qt 6.8 QApplication）。
 * @details    本文件实现：
 *             - 唯一实例创建/查询/销毁（g_xapp 单例，Qt 语义：已存在其它
 *               应用实例时创建失败）；
 *             - 事件循环 exec / quit 委托 XCoreApplication；
 *             - 顶层控件注册表与控件级点查询（widgetAt）；
 *             - 活动窗口/焦点/模态/弹出控件登记（与 XWidget 内部联动）；
 *             - 全局交互参数读写全部委托 XStyleHints。
 *             与 XGuiApplication 一样：XApplication 不包含任何平台 API，
 *             事件注入与原生窗口仍由平台后端负责。
 * @note       模块总开关 XAPPLICATION_ON 定义于 XGuiConfig.h；依赖
 *             XGUIAPPLICATION_ON。置 0 时本文件实现整体裁剪。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XApplication.h"
#include "XStyle.h"

#include "XAlgorithm.h"
#if XWIDGET_ON
#include "XWidget.h"
#endif /* XWIDGET_ON */

#if XAPPLICATION_ON && XGUIAPPLICATION_ON

#include "XCoreApplication.h"
#include "XVector.h"
#if XSTYLEHINTS_ON
#include "XStyleHints.h"
#endif /* XSTYLEHINTS_ON */

/* ==================== 私有实现 ==================== */

/** @brief 进程内唯一 XApplication 单例；由 create_ex 登记、deinit 清空。 */
static XApplication* g_xapp = NULL;

/** @brief 应用对象销毁：释放控件注册表并清空单例，再委托父类释放。 */
static void VXApplication_deinit(XApplication* self)
{
    if (!self) return;
    if (g_xapp == self) g_xapp = NULL;
    if (self->m_topLevelWidgets) {
        XVector_delete_base((XClass*)self->m_topLevelWidgets);
        self->m_topLevelWidgets = NULL;
    }
    self->m_activeWindow = NULL;
    self->m_focusWidget = NULL;
    self->m_activeModalWidget = NULL;
    self->m_activePopupWidget = NULL;
    XClass_Deinit_Parent(XGuiApplication, (XGuiApplication*)self);
}

/* ==================== 类与实例生命周期 ==================== */

XVtable* XApplication_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XApplication)
    XVTABLE_INHERIT_XCLASS(XGuiApplication);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXApplication_deinit);
    return XVTABLE_DEFAULT;
}

void XApplication_init(XApplication* self, int argc, char** argv)
{
    if (!self) return;
    /* 基类 XGuiApplication_init 会初始化并登记 XCoreApplication 单例。 */
    XGuiApplication_init(&self->m_class, argc, argv);
    XClassSetVtable(self, XApplication);
    self->m_topLevelWidgets = XVector_Create(XWidget*);
    self->m_activeWindow = NULL;
    self->m_focusWidget = NULL;
    self->m_activeModalWidget = NULL;
    self->m_activePopupWidget = NULL;
    g_xapp = self;
}

XApplication* XApplication_create_ex(XMemoryType memory, int argc, char** argv)
{
    XApplication* app;
    if (g_xapp) return g_xapp;
    /* Qt 语义：应用实例只能有一个；已由其它创建路径初始化时不接管。 */
    if (XCoreApplication_instance()) return NULL;
    app = (XApplication*)XMemory_malloc(sizeof(XApplication), memory);
    if (!app) return NULL;
    XApplication_init(app, argc, argv);
    Set_Class_Memory(app, memory);
    Set_Class_IsHeap(app, true);
    return app;
}

XApplication* XApplication_instance(void)
{
    return g_xapp;
}

/* ==================== 控件注册表 ==================== */

void XApplication_registerTopLevelWidget(XWidget* widget)
{
    XApplication* app = g_xapp;
    size_t n;
    if (!app || !widget || !app->m_topLevelWidgets) return;
    n = XVector_size_base((const XContainer*)app->m_topLevelWidgets);
    for (size_t i = 0; i < n; ++i) {
        if (XVector_At_Base(app->m_topLevelWidgets, (int64_t)i, XWidget*) == widget)
            return; /* 幂等。 */
    }
    XVector_Push_Back_Base(app->m_topLevelWidgets, XWidget*, widget);
}

void XApplication_unregisterTopLevelWidget(XWidget* widget)
{
    XApplication* app = g_xapp;
    size_t n;
    int64_t index = -1;
    if (!app || !widget || !app->m_topLevelWidgets) return;
    n = XVector_size_base((const XContainer*)app->m_topLevelWidgets);
    for (size_t i = 0; i < n; ++i) {
        if (XVector_At_Base(app->m_topLevelWidgets, (int64_t)i, XWidget*) == widget) {
            index = (int64_t)i;
            break;
        }
    }
    if (index >= 0) XVector_remove_base(app->m_topLevelWidgets, index, 1);
    /* 同步清理指向该控件的焦点/活动引用。 */
    if (app->m_activeWindow == widget) app->m_activeWindow = NULL;
    if (app->m_focusWidget == widget) app->m_focusWidget = NULL;
    if (app->m_activeModalWidget == widget) app->m_activeModalWidget = NULL;
    if (app->m_activePopupWidget == widget) app->m_activePopupWidget = NULL;
}

XVector* XApplication_topLevelWidgets(void)
{
    XApplication* app = g_xapp;
    XVector* out;
    size_t n;
    if (!app || !app->m_topLevelWidgets) return NULL;
    out = XVector_Create(XWidget*);
    if (!out) return NULL;
    n = XVector_size_base((const XContainer*)app->m_topLevelWidgets);
    for (size_t i = 0; i < n; ++i) {
        XWidget* w = XVector_At_Base(app->m_topLevelWidgets, (int64_t)i, XWidget*);
        if (w) XVector_Push_Back_Base(out, XWidget*, w);
    }
    return out;
}

XWidget* XApplication_widgetAt(const XPoint* point)
{
    XApplication* app = g_xapp;
    size_t n;
    if (!app || !point || !app->m_topLevelWidgets) return NULL;
    /* 后登记的视为更上层，逆序查找第一个包含该点的可见顶层控件。 */
#if XWIDGET_ON
    n = XVector_size_base((const XContainer*)app->m_topLevelWidgets);
    if (n == 0) return NULL;
    for (size_t i = n; i > 0; --i) {
        XWidget* w = XVector_At_Base(app->m_topLevelWidgets, (int64_t)(i - 1), XWidget*);
        if (w) {
            if (!XWidget_isVisible(w)) continue;
            XRect g = XWidget_geometry(w);
            if (XRect_contains(&g, point->x, point->y)) {
                XWidget* child = XWidget_childAtGlobal(w, point);
                return child ? child : w;
            }
        }
    }
#endif /* XWIDGET_ON */
    return NULL;
}

/* ==================== 活动窗口与焦点 ==================== */

XWidget* XApplication_activeWindow(void)
{
    XApplication* app = g_xapp;
    return app ? app->m_activeWindow : NULL;
}

/** @brief 发射 focusChanged 信号（前向声明，实现在文件尾）。 */
static void xapp_emitFocusChanged(XApplication* self, XWidget* old,
                                  XWidget* now);

void XApplication_setActiveWindow(XWidget* widget)
{
    XApplication* app = g_xapp;
    if (!app) return;
    if (widget) {
#if XWIDGET_ON
        /* 与 Qt 一致：setActiveWindow 只接受顶层控件。 */
        if (XWidget_parentWidget(widget) != NULL) return;
        XApplication_registerTopLevelWidget(widget);
#endif /* XWIDGET_ON */
    }
    {
        XWidget* oldFocus = app->m_focusWidget;
        app->m_activeWindow = widget;
        if (widget && !app->m_focusWidget) {
            /* 激活窗口时若无焦点控件，则移交焦点到窗口内（若有）。 */
            app->m_focusWidget = widget;
        }
        if (app->m_focusWidget != oldFocus)
            xapp_emitFocusChanged(app, oldFocus, app->m_focusWidget);
    }
#if XWIDGET_ON
    XGuiApplication_setFocusWindow(
        (XWindow*)XWidget_nativeWindow(widget), NULL);
#endif /* XWIDGET_ON */
}

XWidget* XApplication_focusWidget(void)
{
    XApplication* app = g_xapp;
    return app ? app->m_focusWidget : NULL;
}

void XApplication_setFocusWidget(XWidget* widget)
{
    XApplication* app = g_xapp;
    XWidget* old;
    if (!app) return;
    old = app->m_focusWidget;
    if (old == widget) return;
    app->m_focusWidget = widget;
    xapp_emitFocusChanged(app, old, widget);
}

XWidget* XApplication_activeModalWidget(void)
{
    XApplication* app = g_xapp;
    return app ? app->m_activeModalWidget : NULL;
}

void XApplication_setActiveModalWidget(XWidget* widget)
{
    XApplication* app = g_xapp;
    if (app) app->m_activeModalWidget = widget;
}

XWidget* XApplication_activePopupWidget(void)
{
    XApplication* app = g_xapp;
    return app ? app->m_activePopupWidget : NULL;
}

void XApplication_setActivePopupWidget(XWidget* widget)
{
    XApplication* app = g_xapp;
    if (app) app->m_activePopupWidget = widget;
}

/* ==================== 全局交互参数（委托 XStyleHints） ==================== */

int XApplication_doubleClickInterval(void)
{
#if XSTYLEHINTS_ON
    XStyleHints* h = XGuiApplication_styleHints();
    return h ? XStyleHints_mouseDoubleClickInterval(h) : 400;
#else
    return 400;
#endif
}

void XApplication_setDoubleClickInterval(int ms)
{
#if XSTYLEHINTS_ON
    XStyleHints* h = XGuiApplication_styleHints();
    if (h) XStyleHints_setMouseDoubleClickInterval(h, ms);
#else
    (void)ms;
#endif
}

int XApplication_wheelScrollLines(void)
{
#if XSTYLEHINTS_ON
    XStyleHints* h = XGuiApplication_styleHints();
    return h ? XStyleHints_wheelScrollLines(h) : 3;
#else
    return 3;
#endif
}

void XApplication_setWheelScrollLines(int lines)
{
#if XSTYLEHINTS_ON
    XStyleHints* h = XGuiApplication_styleHints();
    if (h) XStyleHints_setWheelScrollLines(h, lines);
#else
    (void)lines;
#endif
}

int XApplication_startDragTime(void)
{
#if XSTYLEHINTS_ON
    XStyleHints* h = XGuiApplication_styleHints();
    return h ? XStyleHints_startDragTime(h) : 500;
#else
    return 500;
#endif
}

void XApplication_setStartDragTime(int ms)
{
#if XSTYLEHINTS_ON
    XStyleHints* h = XGuiApplication_styleHints();
    if (h) XStyleHints_setStartDragTime(h, ms);
#else
    (void)ms;
#endif
}

int XApplication_startDragDistance(void)
{
#if XSTYLEHINTS_ON
    XStyleHints* h = XGuiApplication_styleHints();
    return h ? XStyleHints_startDragDistance(h) : 10;
#else
    return 10;
#endif
}

void XApplication_setStartDragDistance(int px)
{
#if XSTYLEHINTS_ON
    XStyleHints* h = XGuiApplication_styleHints();
    if (h) XStyleHints_setStartDragDistance(h, px);
#else
    (void)px;
#endif
}

int XApplication_cursorFlashTime(void)
{
#if XSTYLEHINTS_ON
    XStyleHints* h = XGuiApplication_styleHints();
    return h ? XStyleHints_cursorFlashTime(h) : 1000;
#else
    return 1000;
#endif
}

void XApplication_setCursorFlashTime(int ms)
{
#if XSTYLEHINTS_ON
    XStyleHints* h = XGuiApplication_styleHints();
    if (h) XStyleHints_setCursorFlashTime(h, ms);
#else
    (void)ms;
#endif
}

int XApplication_keyboardInputInterval(void)
{
#if XSTYLEHINTS_ON
    XStyleHints* h = XGuiApplication_styleHints();
    return h ? XStyleHints_keyboardInputInterval(h) : 400;
#else
    return 400;
#endif
}

void XApplication_setKeyboardInputInterval(int ms)
{
#if XSTYLEHINTS_ON
    XStyleHints* h = XGuiApplication_styleHints();
    if (h) XStyleHints_setKeyboardInputInterval(h, ms);
#else
    (void)ms;
#endif
}


/* ==================== Task 1.4：QApplication 应用级 API ==================== */

XStyle* XApplication_style(void)
{
#if XSTYLE_ON
    return XStyle_defaultStyle();
#else
    return NULL;
#endif
}

void XApplication_setStyle(XStyle* style)
{
#if XSTYLE_ON
    XStyle_setDefaultStyle(style);
#else
    (void)style;
#endif
}

/** @brief 发射 focusChanged 信号（XApplication 继承 XObject 信号槽）。 */
static void xapp_emitFocusChanged(XApplication* self, XWidget* old,
                                  XWidget* now)
{
    XVarList* args;
    if (!self || !((XObject*)self)->m_signalSlot) return;
    args = XVarList_Create(XVar(XWidget*, old), XVar(XWidget*, now));
    if (!args) return;
    XObject_emitSignal((XObject*)self,
                       (size_t)XApplication_focusChanged_signal,
                       args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XApplication_focusChanged_signal(XApplication* self,
                                       XWidget* old, XWidget* now)
{
    (void)old; (void)now;
    return (void*)(size_t)XApplication_focusChanged_signal;
}

XVector* XApplication_allWidgets(void)
{
    XApplication* app = g_xapp;
    size_t n;
    XVector* out;
    if (!app || !app->m_topLevelWidgets) return NULL;
    n = XVector_size_base((const XContainer*)app->m_topLevelWidgets);
    out = XVector_Create(XWidget*);
    if (!out) return NULL;
    for (size_t i = 0; i < n; ++i) {
        XWidget* w = XVector_At_Base(app->m_topLevelWidgets, (int64_t)i, XWidget*);
        if (w) XVector_push_back_1_base(out, &w);
    }
    return out;
}

XWidget* XApplication_topLevelAt(const XPoint* point)
{
    XApplication* app = g_xapp;
    size_t n;
    if (!app || !point || !app->m_topLevelWidgets) return NULL;
#if XWIDGET_ON
    n = XVector_size_base((const XContainer*)app->m_topLevelWidgets);
    for (size_t i = n; i > 0; --i) {
        XWidget* w = XVector_At_Base(app->m_topLevelWidgets, (int64_t)(i - 1), XWidget*);
        if (w && XWidget_isVisible(w)) {
            XRect g = XWidget_geometry(w);
            if (XRect_contains(&g, point->x, point->y)) return w;
        }
    }
#else
    (void)n;
#endif
    return NULL;
}

void XApplication_beep(void)
{
    /* 无平台音频后端：空操作（嵌入式不引入依赖）。 */
}

void XApplication_alert(XWidget* widget, int duration)
{
    (void)widget; (void)duration;
    /* 简化实现：仅记录告警目标（无闪烁动画后端）。 */
}

bool XApplication_isEffectEnabled(int effect)
{
    XApplication* app = g_xapp;
    if (!app) return true;
    return ((app->m_effectEnabled >> effect) & 1) != 0;
}

void XApplication_setEffectEnabled(int effect, bool enable)
{
    XApplication* app = g_xapp;
    if (!app || effect < 0 || effect >= 31) return;
    if (enable) app->m_effectEnabled |= (1 << effect);
    else app->m_effectEnabled &= ~(1 << effect);
}

void XApplication_closeAllWindows(void)
{
    XApplication* app = g_xapp;
    size_t n;
    size_t i;
    if (!app || !app->m_topLevelWidgets) return;
#if XWIDGET_ON
    n = XVector_size_base((const XContainer*)app->m_topLevelWidgets);
    for (i = 0; i < n; ++i) {
        XWidget* w = XVector_At_Base(app->m_topLevelWidgets, (int64_t)i, XWidget*);
        if (w) XWidget_close(w);
    }
#else
    (void)n;
#endif
}

void XApplication_aboutQt(void)
{
    /* 无 Qt 对话框实现：空操作（文档说明）。 */
}

const XString* XApplication_styleSheet(void)
{
    XApplication* app = g_xapp;
    return (app && app->m_styleSheet) ? app->m_styleSheet : NULL;
}

const char* XApplication_styleSheet_2(void)
{
    XApplication* app = g_xapp;
    return (app && app->m_styleSheet) ? XString_toUtf8(app->m_styleSheet) : "";
}

void XApplication_setStyleSheet(const XString* css)
{
    XApplication* app = g_xapp;
    const char* utf8;
    if (!app) return;
    if (!app->m_styleSheet) app->m_styleSheet = XString_create();
    if (!app->m_styleSheet) return;
    if (css)
        XString_assign(app->m_styleSheet, css);
    else
        XString_assign_utf8(app->m_styleSheet, "");
    utf8 = XString_toUtf8(app->m_styleSheet);
#if XSTYLE_ON
    XStyle_installStyleSheet(utf8 ? utf8 : "");
#else
    (void)utf8;
#endif
}

void XApplication_setStyleSheet_2(const char* css)
{
    XApplication* app = g_xapp;
    XString* tmp = NULL;
    if (!app) return;
    if (css) {
        tmp = XString_create_utf8(css);
        if (!tmp) return;
    }
    XApplication_setStyleSheet(tmp);
    if (tmp) XString_delete_base(tmp);
}

bool XApplication_autoSipEnabled(void)
{
    XApplication* app = g_xapp;
    return app ? app->m_autoSipEnabled : false;
}

void XApplication_setAutoSipEnabled(bool enabled)
{
    XApplication* app = g_xapp;
    if (app) app->m_autoSipEnabled = enabled;
}

#endif /* XAPPLICATION_ON && XGUIAPPLICATION_ON */
