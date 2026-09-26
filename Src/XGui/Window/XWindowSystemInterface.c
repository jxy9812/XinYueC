/******************************************************************************
 * @file       XWindowSystemInterface.c
 * @brief      窗口系统事件注入接口实现（对标 Qt 6.8 QWindowSystemInterface）。
 * @details    平台后端调用 handle* 注入函数时，构造携带负载的具体事件
 *             （XResizeEvent / XExposeEvent / XPaintEvent / XFocusEvent /
 *             XCloseEvent / XShowEvent / XHideEvent / XKeyEvent /
 *             XMouseEvent / XWheelEvent / XEnterEvent / XTouchEvent /
 *             XTabletEvent），并经
 *             XGuiApplication_sendSpontaneousEvent 以自发事件语义同步
 *             投递，XWindow_event_base 按事件类型路由到对应窗口事件槽。
 *             屏幕接入系列（handleScreenAdded/Removed/GeometryChange/
 *             LogicalDotsPerInchChange，对标 QWindowSystemInterface 同名
 *             接口）为同步属性同步入口：直接转发 XScreen 注册表与
 *             XGuiApplication 屏幕信号，不走事件队列。
 *             事件投递后立即释放，符合 XEvent 的事件所有权约定。
 *             本文件不引用任何平台 API，嵌入式可用。
 * @note       模块总开关 XWINDOWSYSTEMINTERFACE_ON 定义于 XGuiConfig.h；
 *             依赖 XGUIAPPLICATION_ON / XWINDOW_ON / XWINDOWEVENT_ON。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XWindowSystemInterface.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XPrintf.h"

#if XWINDOWSYSTEMINTERFACE_ON && XGUIAPPLICATION_ON && XWINDOW_ON && XWINDOWEVENT_ON

#if XWIDGET_ON
/* 控件级悬停合成依赖控件域公共 API（命中测试/树遍历/属性查询）；
   XWidget.c 不在本修复改动面内（N5-2 硬约束），仅经公共头引用。 */
#include "XWidget.h"
#include "XWidget_Protected.h"
#endif /* XWIDGET_ON */

/** @brief 当前同步派发中的触摸事件时间戳（毫秒）；仅 handleTouchEvent(_ex)
 *         同步投递栈内有定义，供 touch→mouse 合成器透传（详见
 *         XWindowSystemInterface_touchTimestamp 注释）。 */
static uint32_t g_touchTimestamp = 0;

#if XWIDGET_ON
/* ==================== 控件级悬停合成（对标 Qt
   QApplicationPrivate::dispatchEnterLeave） ==================== */

/* 根因修复（第六轮路5 / N5-2）：ENTER/LEAVE 仅由原生窗边界产生
 * （posix EnterNotify/LeaveNotify → handleEnterEvent/handleLeaveEvent，
 * 唯一生成点），窗内移动鼠标时 MOUSE_MOVE 经桥接命中测试直投目标
 * 控件、从不合成控件级 ENTER/LEAVE——CSS :hover / Fusion lighter
 * 悬停不可达且粘滞（原生窗进入时的命中靶移开后仍保持 UnderMouse，
 * 仅 XDialog [×] 等坐标自算型控件走 mouseMove 幸免）。
 * 对标 Qt：QApplicationPrivate::dispatchEnterLeave 在指针移动派发路径
 * 上按新旧靶控件合成 ENTER/LEAVE——旧靶沿父链逐级 LEAVE 至公共祖先
 * （不含），新靶自公共祖先（不含）自上而下逐级 ENTER；UnderMouse
 * 置位沿用既有 XEVENT 处理（VXWidget_event 的 ENTER/LEAVE 分支），
 * 本文件只合成事件、不改控件内部。 */

/** @brief 当前悬停靶控件（对标 QApplicationPrivate::enter_widget；借用）。 */
static XWidget* g_hoverEnterWidget = NULL;
/** @brief 悬停靶登记时其所属顶层控件：用于靶控件析构后按「顶层注册表
 *         在册 + 子树指针扫描」验活，全程不解引用失放对象（UAF 防护）。 */
static XWidget* g_hoverEnterTop = NULL;

/** @brief 原生窗 → 顶层控件：优先经控件域三个活登记锚正向解析——应用
 *         焦点控件（点击/Tab 编辑控件后即活）、应用模态控件、鼠标抓取
 *         控件；三锚由 XWidget 析构链自清理（VXWidget_deinit →
 *         clearFocusBase / grab 清位 / setApplicationModalWidget(NULL)），
 *         恒为活对象。
 * @note   回退④（第八轮 R1 悬停收口）：三锚全空（或皆不指向本窗）时改
 *         用合成器登记靶 g_hoverEnterWidget 解析——该靶由 XWidget.c 的
 *         桥实投 ENTER 落点回报钩子
 *         （XWindowSystemInterface_setHoverTarget）在投递现场登记（回报
 *         即活对象），并由 VXWidget_deinit 析构链自清位
 *         （XWindowSystemInterface_clearHoverTarget）保证恒不悬空。
 *         「纯悬停冷会话（无焦点/模态/抓取交互）」此前无锚不换靶的残界
 *         由此收口：原生窗边界 ENTER 先经桥命中实投（钩子登记），窗内
 *         后续移动合成器即可解析换靶。窗口域反向注册表仍不可用：
 *         XApplication 顶层表依赖子类 XApplication 才置位的 g_xapp
 *         （XApplication.c:89），基类 XGuiApplication 用法下恒空。 */
static XWidget* xwsi_hoverTopForWindow(XWindow* window)
{
    XWidget* anchors[3];
    int i;
    if (!window) return NULL;
    anchors[0] = XWidget_appFocusWidget();
    anchors[1] = XWidget_applicationModalWidget();
    anchors[2] = XWidget_mouseGrabber();
    for (i = 0; i < 3; ++i) {
        XWidget* top;
        if (!anchors[i]) continue;
        top = XWidget_topLevelWidget(anchors[i]);
        if (top && (XWindow*)top->m_windowHandle == window) return top;
    }
    /* 回退④：登记靶回退锚（原生窗与顶层控件一一对应，命中即同窗）。 */
    if (g_hoverEnterWidget) {
        XWidget* regTop = XWidget_topLevelWidget(g_hoverEnterWidget);
        if (regTop && (XWindow*)regTop->m_windowHandle == window)
            return regTop;
    }
    return NULL;
}

/** @brief 子树内按指针身份查找（只解引用在册活树的结点，用于验活）。 */
static bool xwsi_hoverSubtreeHas(XWidget* root, const XWidget* widget)
{
    const XVector* children;
    size_t n;
    size_t i;
    if (!root || !widget) return false;
    if (root == widget) return true;
    children = XObject_children((XObject*)root);
    n = children ? XVector_size_base((const XContainer*)children) : 0;
    for (i = 0; i < n; ++i) {
        XObject* child = *(XObject**)XVector_at_base((const XVector*)children,
                                                     (int64_t)i);
        if (child && child->is_widget &&
            xwsi_hoverSubtreeHas((XWidget*)child, widget))
            return true;
    }
    return false;
}

/** @brief 悬停靶是否仍存活：有锚命中登记树时按「顶层注册表在册 + 子树
 *         指针扫描」验活（值比较，顶层由活锚保证存活，靶本体失放时不会
 *         被解引用）；三锚全空（或皆不在登记树）时信任登记——第八轮 R1
 *         起登记靶由桥实投 ENTER 落点回报（回报即活对象），且
 *         VXWidget_deinit 析构链自清位（XWidget.c，与三锚自清理同纪律），
 *         登记非空即活对象，UAF 防护由清位纪律承担。若仍保守判死，纯
 *         悬停冷会话的登记会在首次移动即被清掉，合成器永不点火。 */
static bool xwsi_hoverTargetAlive(XWidget* widget)
{
    XWidget* anchors[3];
    int i;
    if (!widget || !g_hoverEnterTop) return false;
    anchors[0] = XWidget_appFocusWidget();
    anchors[1] = XWidget_applicationModalWidget();
    anchors[2] = XWidget_mouseGrabber();
    for (i = 0; i < 3; ++i) {
        if (anchors[i] &&
            XWidget_topLevelWidget(anchors[i]) == g_hoverEnterTop)
            return xwsi_hoverSubtreeHas(g_hoverEnterTop, widget);
    }
    return true;
}

/** @brief 靶控件局部坐标：顶层局部 pos 减去 target 相对顶层的累计窗口
 *         矩形偏移（与 XWidget_dispatchPointerEvent 逐接收者换算、
 *         XWidget_accumulateOffset 求和口径一致；顶层自身不计入）。 */
static XPoint xwsi_hoverLocalPos(const XWidget* target,
                                 const XPoint* topLocalPos)
{
    XPoint out;
    const XWidget* w = target;
    if (!topLocalPos) {
        XPoint_init(&out, 0, 0);
        return out;
    }
    out = *topLocalPos;
    while (w && !w->m_isWindow) {
        out.x -= w->m_windowRect.x;
        out.y -= w->m_windowRect.y;
        w = XWidget_parentWidget(w);
    }
    return out;
}

/** @brief 靶控件选取：childAt 命中 + 顶层遮罩回退（与桥接实投
 *         XWidget_dispatchPointerEvent 的命中分支完全同口径），再按 Qt
 *         语义把禁用/鼠标穿透靶上溯到最近可用祖先。不可见不入靶已由
 *         childAt 的 WA_WState_Hidden 跳过承担；禁用与鼠标穿透不入靶
 *         对应桥接实投传播循环的同名跳过位（首个非透明可用接收者才
 *         收到事件）。 */
static XWidget* xwsi_hoverPickTarget(XWidget* top, const XPoint* pos)
{
    XWidget* target;
    const XRegion* topMask;
    if (!top || !pos) return NULL;
    target = XWidget_childAt(top, pos);
    if (!target) {
        topMask = &top->m_mask;
        if (topMask->count <= 0 || XRegion_contains(topMask, pos->x, pos->y))
            target = top;
    }
    while (target &&
           (!XWidget_isEnabled(target) ||
            XWidget_testAttribute(target,
                                  XWidgetAttribute_TransparentForMouseEvents)))
        target = XWidget_parentWidget(target);
    return target;
}

/** @brief 向单个控件投递 ENTER（局部坐标按该控件相对顶层偏移换算）。 */
static void xwsi_hoverSendEnter(XWidget* w, const XPoint* topLocalPos,
                                const XPoint* globalPos)
{
    XEnterEvent* event;
    XPoint local;
    local = xwsi_hoverLocalPos(w, topLocalPos);
    event = XEnterEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                  XEVENT_TYPE_ENTER, &local, globalPos);
    if (!event) return;
    XCoreApplication_sendEvent((XObject*)w, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
}

/** @brief 按新旧靶合成 ENTER/LEAVE（对标 Qt dispatchEnterLeave 的链序
 *         判据——对称 isAncestorOf（含自身）双向截链）：
 *         LEAVE 发给 leave 及其祖先链，直到「同时是 enter 祖先（含
 *         自身）」的第一个为止（不含）——旧靶是新靶祖先时旧靶保持
 *         entered、不收 LEAVE；ENTER 自「同时是 leave 祖先（含自身）」
 *         的第一个（不含）之下逆序补齐到 enter——从父容器移入子控件
 *         时子控件必获 ENTER。跨顶层树（互不为祖先）则两条链各自整链
 *         投递。enter==leave 直接返回；任一为 NULL 则对应链整链投递。 */
static void xwsi_hoverDispatchEnterLeave(XWidget* enter, XWidget* leave,
                                         const XPoint* topLocalPos,
                                         const XPoint* globalPos)
{
    XEvent leaveEvent;
    XWidget* chain[64];
    XWidget* w;
    int depth = 0;
    int i;
    if (enter == leave) return;
    if (leave) {
        XEvent_init(&leaveEvent, XEVENT_TYPE_LEAVE);
        for (w = leave; w && !XWidget_isAncestorOf(w, enter);
             w = XWidget_parentWidget(w))
            XCoreApplication_sendEvent((XObject*)w, &leaveEvent);
    }
    if (enter) {
        for (w = enter; w && !XWidget_isAncestorOf(w, leave) && depth < 64;
             w = XWidget_parentWidget(w))
            chain[depth++] = w;
        for (i = depth - 1; i >= 0; --i)
            xwsi_hoverSendEnter(chain[i], topLocalPos, globalPos);
    }
}

/** @brief 指针移动悬停合成入口：在 MOUSE_MOVE 注入路径上按新旧靶合成
 *         ENTER/LEAVE（对标 Qt processMouseEvent → QWidgetWindow::
 *         handleMouseEvent 的 dispatchEnterLeave 挂点）。
 *         门序与实投链一致：①应用模态门（与桥接 VXWidgetWindow_event
 *         输入拦截同口径——被阻塞窗口不会实投移动，合成不得越门造悬停，
 *         顺带整链收尾登记靶以消除模态间歇期粘滞）；②鼠标抓取重路由
 *         （抓取期间移动一律以抓取控件为靶，跨顶层时坐标经全局换算，
 *         与 XWidget_dispatchPointerEvent 的抓取分支同口径）；③常规
 *         childAt 命中。 */
static void xwsi_hoverSynthesizeMouseMove(XWindow* window,
                                          const XPoint* position,
                                          const XPoint* globalPosition)
{
    XWidget* top;
    XWidget* enter = NULL;
    XWidget* leave;
    XPoint topLocal;
    XPoint enterLocal;
    XPoint globalBuf;
    const XPoint* global = globalPosition;

    XPoint_init(&topLocal, 0, 0);
    XPoint_init(&enterLocal, 0, 0);
    leave = g_hoverEnterWidget;
    /* 析构验活：失放靶视同无靶（只清状态，不向失放对象投递）。 */
    if (leave && !xwsi_hoverTargetAlive(leave)) {
        g_hoverEnterWidget = NULL;
        g_hoverEnterTop = NULL;
        leave = NULL;
    }
    top = xwsi_hoverTopForWindow(window);
    if (top) {
        /* ① 应用模态门（先于抓取重路由，与桥拦截同序）。 */
        {
            XWidget* modal = XWidget_applicationModalWidget();
            if (modal) {
                XWidget* modalTop = XWidget_topLevelWidget(modal);
                XWindow* topWin = XWidget_windowHandle(top);
                bool popupTop = topWin &&
                                XWindow_type(topWin) == XWindowType_Popup;
                if (modalTop && modalTop != top && !popupTop) {
                    xwsi_hoverDispatchEnterLeave(NULL, leave, NULL, NULL);
                    g_hoverEnterWidget = NULL;
                    g_hoverEnterTop = NULL;
                    return;
                }
            }
        }
        /* ② 鼠标抓取重路由 / ③ 常规命中。 */
        {
            XWidget* grabber = XWidget_mouseGrabber();
            if (grabber) {
                XWidget* grabTop = XWidget_topLevelWidget(grabber);
                if (grabTop && grabTop != top) {
                    globalBuf = XWidget_mapToGlobal(top, position);
                    global = &globalBuf;
                    topLocal = XWidget_mapFromGlobal(grabTop, &globalBuf);
                    top = grabTop;
                } else {
                    topLocal = *position;
                }
                enter = grabber;
            } else {
                topLocal = *position;
                enter = xwsi_hoverPickTarget(top, &topLocal);
            }
        }
        enterLocal = xwsi_hoverLocalPos(enter, &topLocal);
    }

    if (enter == leave) return;
    /* 先置状态再投递：ENTER/LEAVE 槽内可能同步再入（如槽内开弹层）。 */
    g_hoverEnterWidget = enter;
    g_hoverEnterTop = enter ? XWidget_topLevelWidget(enter) : NULL;
    xwsi_hoverDispatchEnterLeave(enter, leave,
                                 enter ? &enterLocal : NULL, global);
}

void XWindowSystemInterface_setHoverTarget(XWidget* widget)
{
    /* 幂等：同靶重复登记为 no-op（合成器派发链期间桥不重入此路径——
       合成 ENTER 走 XCoreApplication_sendEvent 直投控件事件槽，不经
       XWidget_dispatchPointerEvent 桥，回报钩子只在原生窗边界 ENTER
       实投时触发）。先置状态无投递，不存在槽内再入窗口。 */
    if (widget == g_hoverEnterWidget) return;
    g_hoverEnterWidget = widget;
    g_hoverEnterTop = widget ? XWidget_topLevelWidget(widget) : NULL;
}

void XWindowSystemInterface_clearHoverTarget(const XWidget* widget)
{
    if (!widget || (const XWidget*)g_hoverEnterWidget != widget) return;
    g_hoverEnterWidget = NULL;
    g_hoverEnterTop = NULL;
}
#endif /* XWIDGET_ON */

void XWindowSystemInterface_handleGeometryChange(XWindow* window, const XRect* rect)
{
    XResizeEvent* event;
    XRect old;
    XSize oldSize;
    XSize newSize;
    if (!window || !rect) return;

    /* 1) 持久化新几何：保证 resize 槽内可读到新尺寸（Qt 语义）。 */
    old = XWindow_geometry(window);
    oldSize.width = old.width;
    oldSize.height = old.height;
    XWindow_setGeometry_rect(window, rect);

    /* 2) 投递 Resize 事件（oldSize 为变化前尺寸）。 */
    newSize.width = XWindow_width(window);
    newSize.height = XWindow_height(window);
    event = XResizeEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                   XEVENT_TYPE_RESIZE, &newSize, &oldSize);
    if (!event) return;
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
}

bool XWindowSystemInterface_handleExposeEvent(XWindow* window, const XRegion* region)
{
    XExposeEvent* event;
    XRegion payload;
    bool handled;
    bool exposed;
    if (!window) return false;
    event = XExposeEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                   XEVENT_TYPE_EXPOSE, region);
    if (!event) return false;
    /* 对齐 Qt QGuiApplicationPrivate::processExposeEvent：投递前先把
       「是否暴露」同步到窗口（区域非空即已暴露，区域为空即整窗隐藏）。 */
    payload = XExposeEvent_region(event);
    exposed = !XRegion_isEmpty(&payload);
    XRegion_deinit(&payload);
    XWindow_setExposed(window, exposed);
    handled = XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return handled;
}

bool XWindowSystemInterface_handlePaintEvent(XWindow* window, const XRegion* region)
{
    XPaintEvent* event;
    bool handled;
    if (!window) return false;
    event = XPaintEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                  XEVENT_TYPE_PAINT, region);
    if (!event) return false;
    handled = XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return handled;
}

void XWindowSystemInterface_handleFocusWindowChanged(XWindow* window,
                                                     XFocusReason reason)
{
    XWindow* oldFocused;
    XFocusEvent* event;

    /* 根因修复（复扫 R-34）：此前只向新窗口投递 FocusIn，从不更新
       XGuiApplication 焦点窗口——focusWindow() 脱钩、focusWindowChanged
       不发射、IME setFocusObject 链不触发。对标
       QGuiApplicationPrivate::processFocusWindowChanged 在本入口收口
       完整焦点切换语义：
       1) 非顶层窗口折算到其顶层（Qt: window = window->window()）；
       2) 向旧焦点窗口补发 FocusOut（先失焦后聚焦，与 Qt 同序）；
       3) 向新焦点窗口派发 FocusIn；
       4) 经 XGuiApplication_setFocusWindow 更新焦点窗口（值变化时内部
          发射 focusWindowChanged，并联动 focusObject / 平台输入上下文
          setFocusObject 的 IME 链）。
       新旧窗口的 m_active / activeChanged 联动由 XWindow 焦点事件路由
       （XWindow.c VXWindow_event）在事件派发时统一完成，与本入口解耦。 */

    /* Qt 语义：子窗口获得焦点即其顶层获得焦点（沿普通父链上溯）。 */
    while (window) {
        XWindow* parent = XWindow_parent(window,
                                         XWindowAncestor_ExcludeTransients);
        if (!parent) break;
        window = parent;
    }

    oldFocused = XGuiApplication_focusWindow();
    if (oldFocused == window) return; /* Qt：焦点窗口未变直接返回。 */

    /* 旧窗口补发 FocusOut。仅当旧窗口仍处于激活态时补发：按本头文件
       既有约定，平台后端会在切换前自行向旧窗口投递 FocusOut（X11
       FocusOut 原生事件直投路径），以 isActive 守卫保证同一失焦只
       派发一次；纯 WSI 注入路径（平台未先派发）则由此处补齐 Qt 语义。 */
    if (oldFocused && XWindow_isActive(oldFocused)) {
        event = XFocusEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                      XEVENT_TYPE_FOCUS_OUT, reason);
        if (event) {
            XGuiApplication_sendSpontaneousEvent((XObject*)oldFocused,
                                                 (XEvent*)event);
            XEvent_delete_base((XEvent*)event);
        }
    }

    /* 新窗口 FocusIn（window 为 NULL 表示整体失焦：跳过聚焦、仅走
       下方焦点窗口清空，与 Qt processFocusWindowChanged(NULL) 一致）。 */
    if (window) {
        event = XFocusEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                      XEVENT_TYPE_FOCUS_IN, reason);
        if (!event) return;
        XGuiApplication_sendSpontaneousEvent((XObject*)window,
                                             (XEvent*)event);
        XEvent_delete_base((XEvent*)event);
    }

    /* 更新应用焦点窗口与焦点对象：内部发射 focusWindowChanged /
       focusObjectChanged，并经平台输入上下文 setFocusObject 触发
       IME 聚焦链（此三件事此前全部脱钩）。object 传 NULL 按应用层
       既有语义缺省为窗口自身。 */
    XGuiApplication_setFocusWindow(window, NULL);
}

void XWindowSystemInterface_handleWindowStateChanged(XWindow* window,
                                                     XWindowState newState)
{
    /* 对标 QWindowSystemInterface::handleWindowStateChanged：平台是窗口
       状态的事实来源（WM 实测结果），经 report 通道持久化并发射
       windowStateChanged 信号、联动可见性（内部不回写平台层）。 */
    if (!window) return;
    XWindow_reportWindowStateChanged(window, newState);
}

void XWindowSystemInterface_handleScreenAdded(XScreen* screen)
{
    /* 对标 QWindowSystemInterface::handleScreenAdded：平台层枚举到屏幕后
       统一经本入口登记；登记与 screenAdded 信号由 XGuiApplication 负责。 */
    if (!screen) return;
    XGuiApplication_screenAdded(screen);
}

void XWindowSystemInterface_handleScreenRemoved(XScreen* screen)
{
    if (!screen) return;
    XGuiApplication_screenRemoved(screen);
}

void XWindowSystemInterface_handleScreenGeometryChange(XScreen* screen,
                                                       const XRect* geometry,
                                                       const XRect* availableGeometry)
{
#if XSCREEN_ON
    if (!screen) return;
    /* 更新顺序与 QPlatformScreen::geometryChanged 一致：先 geometry
       （其内部已联动 availableGeometry/virtualGeometry/物理 DPI），再按
       平台提供的独立可用几何覆盖。 */
    if (geometry) XScreen_setGeometry(screen, geometry);
    if (availableGeometry) XScreen_setAvailableGeometry(screen, availableGeometry);
#else
    (void)screen; (void)geometry; (void)availableGeometry;
#endif /* XSCREEN_ON */
}

void XWindowSystemInterface_handleScreenLogicalDotsPerInchChange(XScreen* screen,
                                                                 float dpi)
{
#if XSCREEN_ON
    if (!screen) return;
    /* Qt 的 handleScreenLogicalDotsPerInchChange 用单值同时更新 X/Y。 */
    XScreen_setLogicalDotsPerInch(screen, dpi, dpi);
#else
    (void)screen; (void)dpi;
#endif /* XSCREEN_ON */
}

void XWindowSystemInterface_handleThemeChanged(XStyleHintsColorScheme theme)
{
#if XSTYLEHINTS_ON
    /* 对标 QGuiApplicationPrivate::processThemeChanged：平台主题变化
       进入 styleHints 颜色方案（XGui 的 Theme 落点），值变化时由
       XStyleHints_setColorScheme 内部发射 colorSchemeChanged。 */
    XStyleHints* hints = XGuiApplication_styleHints();
    if (hints) XStyleHints_setColorScheme(hints, theme);
#else
    (void)theme;
#endif /* XSTYLEHINTS_ON */
}

void XWindowSystemInterface_handleLocaleChange(const char* localeUtf8)
{
    /* 对标 QWindowSystemInterface::handleLocaleChange：区域设置落位应用
       注入态（BCP 47 名称字符串），请求方向为 Auto 时按新语言重解析
       有效布局方向（值变化时发射 layoutDirectionChanged）。 */
    XGuiApplication_setPlatformLocaleUtf8(localeUtf8);
}

void XWindowSystemInterface_handleApplicationStateChanged(
        XGuiApplicationState state)
{
    /* 对标 QWindowSystemInterface::handleApplicationStateChanged：转发
       应用状态（值变化时内部发射 applicationStateChanged）。 */
    XGuiApplication_setApplicationState(state);
}

bool XWindowSystemInterface_handleCloseEvent(XWindow* window)
{
    XCloseEvent* event;
    bool handled;
    if (!window) return false;
    event = XCloseEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_CLOSE);
    if (!event) return false;
    handled = XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    if (handled) {
        /* 关闭事件以 accept 状态表达「是否允许关闭」，与 QCloseEvent 一致。 */
        handled = XEvent_isAccepted((const XEvent*)event);
    }
    XEvent_delete_base((XEvent*)event);
    return handled;
}

bool XWindowSystemInterface_handleShowEvent(XWindow* window)
{
    XShowEvent* event;
    bool handled;
    if (!window) return false;
    event = XShowEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_SHOW);
    if (!event) return false;
    handled = XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return handled;
}

bool XWindowSystemInterface_handleHideEvent(XWindow* window)
{
    XHideEvent* event;
    bool handled;
    if (!window) return false;
    event = XHideEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_HIDE);
    if (!event) return false;
    handled = XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return handled;
}

bool XWindowSystemInterface_handleKeyEvent(XWindow* window, XEventType type,
                                             int key, XKeyboardModifiers modifiers,
                                             bool autoRepeat)
{
    /* 既有签名向后兼容：平台未提供扫描码/时间时按 0 委托完整负载版。 */
    return XWindowSystemInterface_handleKeyEvent_ex(window, type, key,
                                                    modifiers, autoRepeat, 0, 0);
}

bool XWindowSystemInterface_handleKeyEvent_ex(XWindow* window, XEventType type,
                                              int key, XKeyboardModifiers modifiers,
                                              bool autoRepeat,
                                              uint32_t nativeScanCode,
                                              uint32_t timestamp)
{
    XKeyEvent* event;
    if (!window) return false;
    event = XKeyEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, key, modifiers);
    if (!event) return false;
    XKeyEvent_setAutoRepeat(event, autoRepeat);
    XKeyEvent_setNativeScanCode(event, nativeScanCode);
    XKeyEvent_setTimestamp(event, timestamp);
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return true;
}

bool XWindowSystemInterface_handleInputMethodEvent(
        XWindow* window, const char* preeditUtf8, const char* commitUtf8,
        int replacementStart, int replacementLength,
        int cursorPosition, int anchorPosition)
{
    XString* preedit;
    XString* commit;
    XInputMethodEvent* event;
    bool handled;
    if (!window) return false;
    preedit = XString_create_utf8(preeditUtf8 ? preeditUtf8 : "");
    commit = XString_create_utf8(commitUtf8 ? commitUtf8 : "");
    if (!preedit || !commit) {
        if (preedit) XString_delete_base((XClass*)preedit);
        if (commit) XString_delete_base((XClass*)commit);
        return false;
    }
    event = XInputMethodEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, preedit,
                                        commit, replacementStart,
                                        replacementLength, cursorPosition,
                                        anchorPosition);
    XString_delete_base((XClass*)preedit);
    XString_delete_base((XClass*)commit);
    if (!event) return false;
    /* 与其它窗口系统接口事件保持一致：经应用自发事件入口投递，
       由 XWidgetWindow 桥接到当前焦点控件（例如 XLineEdit）。
       直接调用窗口槽会绕过 notify/vtable 事件链，导致输入法提交
       只到达窗口而不会到达实际编辑控件。 */
    handled = XGuiApplication_sendSpontaneousEvent((XObject*)window,
                                                    (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return handled;
}

bool XWindowSystemInterface_handleDropEvent(
        XWindow* window, XEventType type, XPoint position,
        const XPoint* globalPosition, const char* mimeTypeUtf8,
        const char* dataUtf8)
{
    XString* mimeType;
    XString* data;
    XDropEvent* event;
    bool handled;
    if (!window || (type != XEVENT_TYPE_DRAG_ENTER &&
                    type != XEVENT_TYPE_DRAG_MOVE &&
                    type != XEVENT_TYPE_DRAG_LEAVE && type != XEVENT_TYPE_DROP))
        return false;
    mimeType = XString_create_utf8(mimeTypeUtf8 ? mimeTypeUtf8 : "");
    data = XString_create_utf8(dataUtf8 ? dataUtf8 : "");
    if (!mimeType || !data) {
        if (mimeType) XString_delete_base((XClass*)mimeType);
        if (data) XString_delete_base((XClass*)data);
        return false;
    }
    event = XDropEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, &position,
                                 globalPosition, mimeType, data);
    XString_delete_base((XClass*)mimeType);
    XString_delete_base((XClass*)data);
    if (!event) return false;
    handled = XGuiApplication_sendSpontaneousEvent((XObject*)window,
                                                   (XEvent*)event);
    handled = handled && XEvent_isAccepted((XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return handled;
}

bool XWindowSystemInterface_handleMouseEvent(XWindow* window, XEventType type,
                                             XMouseButton button,
                                             XMouseButton buttons,
                                             XKeyboardModifiers modifiers,
                                             XPoint position)
{
    /* 既有签名向后兼容：平台未提供全局坐标/时间时按零值委托完整负载版。 */
    return XWindowSystemInterface_handleMouseEvent_ex(window, type, button,
                                                      buttons, modifiers,
                                                      position, NULL, 0);
}

bool XWindowSystemInterface_handleMouseEvent_ex(XWindow* window, XEventType type,
                                                XMouseButton button,
                                                XMouseButton buttons,
                                                XKeyboardModifiers modifiers,
                                                XPoint position,
                                                const XPoint* globalPosition,
                                                uint32_t timestamp)
{
    XMouseEvent* event;
    if (!window) return false;
#if XWIDGET_ON
    /* 指针移动派发路径：按新旧靶控件合成 ENTER/LEAVE（控件级悬停
       根修 N5-2，见文件头 xwsi_hoverSynthesizeMouseMove 注释）。 */
    if (type == XEVENT_TYPE_MOUSE_MOVE)
        xwsi_hoverSynthesizeMouseMove(window, &position, globalPosition);
#endif /* XWIDGET_ON */
    event = XMouseEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, button,
                                  modifiers, position);
    if (!event) return false;
    XMouseEvent_setButtons(event, buttons);
    XMouseEvent_setGlobalPosition(event, globalPosition);
    XMouseEvent_setTimestamp(event, timestamp);
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return true;
}

bool XWindowSystemInterface_handleWheelEvent(XWindow* window,
                                             XMouseButton buttons,
                                             XKeyboardModifiers modifiers,
                                             XPoint position,
                                             const XPoint* angleDelta)
{
    XWheelEvent* event;
    if (!window) return false;
    event = XWheelEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                  XEVENT_TYPE_WHEEL, &position, NULL,
                                  angleDelta, buttons, modifiers);
    if (!event) return false;
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return true;
}

bool XWindowSystemInterface_handleTouchEvent(XWindow* window, XEventType type,
                                             XPoint position,
                                             const XPoint* globalPosition,
                                             int pointCount)
{
    /* 既有签名向后兼容：平台未提供时间时按 0 委托完整负载版。 */
    return XWindowSystemInterface_handleTouchEvent_ex(window, type, position,
                                                      globalPosition,
                                                      pointCount, 0);
}

bool XWindowSystemInterface_handleTouchEvent_ex(XWindow* window, XEventType type,
                                                XPoint position,
                                                const XPoint* globalPosition,
                                                int pointCount,
                                                uint32_t timestamp)
{
    XTouchEvent* event;
    /* 对标 QGuiApplicationPrivate::processTouchEvent 的 WSI 入口形态：
       平台后端只负责翻译原生触摸流，合成/命中/派发统一在本入口之后。
       X11 XI2 合成与嵌入式 tslib/evdev 注入不在本批（无硬件验证手段），
       本函数即其后续统一注入点（/tmp 探针可直接合成验证）。 */
    if (!window || (type != XEVENT_TYPE_TOUCH_BEGIN &&
                    type != XEVENT_TYPE_TOUCH_UPDATE &&
                    type != XEVENT_TYPE_TOUCH_END &&
                    type != XEVENT_TYPE_TOUCH_CANCEL))
        return false;
    if (pointCount < 1) pointCount = 1;
    /* 记录本序列时间戳供合成器读取：注入为同步自发投递（调用返回时
       事件已处理完毕），静态值的作用域即"当前同步派发中的触摸事件"，
       合成器在投递栈内读取恰好命中本序列；XTouchEvent 负载结构未扩展
       （字段归属 XWindowEvent.h，不在本批改动面），以通道方式透传。 */
    g_touchTimestamp = timestamp;
    event = XTouchEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, &position,
                                  globalPosition, pointCount);
    if (!event) {
        g_touchTimestamp = 0;
        return false;
    }
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    g_touchTimestamp = 0;
    return true;
}

bool XWindowSystemInterface_handleTouchPoints_ex(XWindow* window,
                                                 XEventType type,
                                                 const XTouchPoint* points,
                                                 int count,
                                                 uint32_t timestamp)
{
    XTouchEvent* event;
    if (!window || !points || count < 1 ||
        (type != XEVENT_TYPE_TOUCH_BEGIN &&
         type != XEVENT_TYPE_TOUCH_UPDATE &&
         type != XEVENT_TYPE_TOUCH_END &&
         type != XEVENT_TYPE_TOUCH_CANCEL))
        return false;
    g_touchTimestamp = timestamp;
    /* 先建 1 点事件（旧 init 签名，主点取 points[0]），再注入完整列
       表（深拷贝+主点同步在 setPoints 内完成）。 */
    event = XTouchEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type,
                                  &points[0].m_position,
                                  &points[0].m_globalPosition, 1);
    if (!event) {
        g_touchTimestamp = 0;
        return false;
    }
    XTouchEvent_setPoints(event, points, count);
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    g_touchTimestamp = 0;
    return true;
}

uint32_t XWindowSystemInterface_touchTimestamp(void)
{
    return g_touchTimestamp;
}

bool XWindowSystemInterface_handleTabletEvent(XWindow* window, XEventType type,
                                              XPoint position,
                                              const XPoint* globalPosition,
                                              float pressure, int pointerType)
{
    XTabletEvent* event;
    /* 对标 QGuiApplicationPrivate::processTabletEvent：平台翻译原生笔事件
       后统一经本入口投递；X11 XI2 合成不在本批（无硬件验证手段）。 */
    if (!window || (type != XEVENT_TYPE_TABLET_PRESS &&
                    type != XEVENT_TYPE_TABLET_RELEASE &&
                    type != XEVENT_TYPE_TABLET_MOVE))
        return false;
    event = XTabletEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, type, &position,
                                   globalPosition, pressure, pointerType);
    if (!event) return false;
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
    return true;
}

void XWindowSystemInterface_handleEnterEvent(XWindow* window,
                                             XPoint position,
                                             const XPoint* globalPosition)
{
    XEnterEvent* event;
    if (!window) return;
#if XWIDGET_ON
    /* 悬停合成器同步：原生窗边界 ENTER 的实投靶由桥接命中测试决定
       （首个非透明可用接收者），与 xwsi_hoverPickTarget 同口径。预置
       登记靶后，窗内首次移动若靶未变则零合成，避免对同一靶重复投递
       ENTER（位与 enterEvent 槽均幂等，重复亦无害，此处省之）。 */
    {
        XWidget* top = xwsi_hoverTopForWindow(window);
        if (top && !XWidget_mouseGrabber()) {
            g_hoverEnterWidget = xwsi_hoverPickTarget(top, &position);
            g_hoverEnterTop = g_hoverEnterWidget
                ? XWidget_topLevelWidget(g_hoverEnterWidget) : NULL;
        }
    }
#endif /* XWIDGET_ON */
    event = XEnterEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                  XEVENT_TYPE_ENTER, &position,
                                  globalPosition);
    if (!event) return;
    XGuiApplication_sendSpontaneousEvent((XObject*)window, (XEvent*)event);
    XEvent_delete_base((XEvent*)event);
}

void XWindowSystemInterface_handleLeaveEvent(XWindow* window)
{
    XEvent event;
    if (!window) return;
#if XWIDGET_ON
    /* 指针离开原生窗边界：桥接层对该树递归清 UnderMouse；合成器登记靶
       同步置空（防靶析构悬空，且再入时不向已清树重复投 LEAVE）。 */
    g_hoverEnterWidget = NULL;
    g_hoverEnterTop = NULL;
#endif /* XWIDGET_ON */
    XEvent_init(&event, XEVENT_TYPE_LEAVE);
    XGuiApplication_sendSpontaneousEvent((XObject*)window, &event);
}

void XWindowSystemInterface_flushWindowSystemEvents(XEventLoopProcessEventsFlags flags)
{
    XGuiApplication_processEvents(flags);
}

#endif /* XWINDOWSYSTEMINTERFACE_ON && XGUIAPPLICATION_ON && XWINDOW_ON && XWINDOWEVENT_ON */
