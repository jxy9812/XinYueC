#if XGUI_ON

#include "XDockWidget.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include "XAction.h"
#include "XMainWindow_Protected.h"
/* 拖出阈值（对标 QApplication::startDragDistance 的全局拖拽启动距离）。 */
#include "XApplication.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XPainter.h"
#include "XTextUtf8.h"
#include "XWidget_Protected.h"
#include "XWindow.h"              /* 浮动态顶层窗口句柄（移窗条带归位）。 */
#include "XWindowDecoration.h"    /* 浮动态让位条带归位（fbdev 单屏残影）。 */
#include "XBackingStore.h"        /* 浮动态自身内容直搬（fbdev 免重绘移窗）。 */
#include "XPlatformBackingStore.h"
#include "XGuiApplication.h"      /* visibilityChanged 载荷的屏幕可用区原点。 */
#include "XScreen.h"              /* 同上（XScreen_availableGeometry）。 */

#if XWIDGET_ON && XDOCKWIDGET_ON

/* 拖动跟随节流与调试跟踪共用的毫秒时钟。 */
#include "XDateTime.h"

/* ==================== 调试跟踪（XGUI_DOCK_TRACE，问题关闭后移除） ==================== */

#if XGUI_DOCK_TRACE
#include "XPrintf.h"
/* 拖放链路逐事件跟踪：毫秒时间戳 + 函数名 + 关键决策值，行末即刷。
 * 移动事件逐条记录——两次跟踪行的时间差即事件环真实节奏，用于定位
 * "停靠操作卡顿数秒"类问题（时间戳断崖 = 冻结窗口）。控制台输出经
 * 库内 XPrintf（带输出重定向栈），禁止直用 printf。 */
#define XDW_TRACE(...)                                                    \
    do {                                                                  \
        int64_t xdwTraceMs = XDateTime_currentMSecsSinceEpoch();          \
        XPrintf("[DOCK %lld.%03lld %s] ", (long long)(xdwTraceMs / 1000), \
                (long long)(xdwTraceMs % 1000), __func__);                \
        XPrintf(__VA_ARGS__);                                             \
        XPrintf("\n");                                                    \
    } while (0)
#else
#define XDW_TRACE(...) do {} while (0)
#endif

/* ==================== 内部常量 ==================== */

/** @brief 标题条交互高度：绘制 20 像素标题 + 1 像素底部分隔线；
 *         setWidget 的内容区从该高度起（与既有 setWidget/绘制口径一致）；
 *         垂直标题条时同值转为竖条宽度（对标 Qt QDockWidgetLayout
 *         垂直标题区宽 = titleHeight，qdockwidget.cpp:504-506）。 */
#define XDW_TITLE_H 21
/** @brief 标题条右侧关闭按钮命中区宽度（对标 Qt 标题条关闭按钮）。 */
#define XDW_CLOSE_BOX 18
/** @brief 标题文本左边距（手绘路径 drawText 起笔 x；垂直竖排横向
 *         居中口径不受影响）。 */
#define XDW_TITLE_LEFT_PAD 6
/** @brief 标题文本右侧安全余量（省略截断可用宽度口径的一部分）。 */
#define XDW_TITLE_SLACK 8

/** @brief 调用 XWidget 基类事件实现（经 XClass_Parent 取基类虚表槽位）。
 * @note  不可用 XWidget_*_base 入口转发：该入口按对象虚表再分派，会
 *        重新命中本类重载形成自递归（与 XToolBar 的
 *        XClass_Parent 转发同口径）。 */
#define xdw_callParent(self, eventSlot, eventArg)                      \
    XClass_Parent(XWidget, EXWidget_##eventSlot,                      \
                  void (*)(XWidget*, XEvent*))((self), (eventArg))

/* ==================== 内部工具 ==================== */

/** @brief 拖动跟随节流时钟（文件级：同一时刻至多一个拖动会话）。
 *         对标 Qt 鼠标事件压缩：拖动跟随为绝对定位，中间高频移动
 *         事件只需取最新一条落位。 */
static int64_t xdw_lastFollowMs;

/** @brief 宿主销毁槽前向声明（定义见保护接口一节；deinit 先用到）。 */
static void xdw_hostDestroyedSlot(XObject* receiver, XVarList* args);

/** @brief setFloating 实现体前向声明（定义见公共 API 一节；拖出路径
 *         先用到——对标 Qt startDrag→unplug 不消费 undockedGeometry）。 */
static void xdw_setFloatingImpl(XDockWidget* self, bool floating,
                                bool useUndockedGeometry);

static void xdw_emitInt(XDockWidget* self, size_t signal, int value)
{
    XVarList* args = XVarList_Create(XVar(int, value));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xdw_emitBool(XDockWidget* self, size_t signal, bool value)
{
    XVarList* args = XVarList_Create(XVar(bool, value));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/**
 * @brief      指针设备全局坐标（浮动态跟指的唯一可信坐标源）。
 * @details    优先取事件的屏幕全局坐标（平台后端注入：X11 取根坐标、
 *             fbdev 取面板坐标、win32 取屏幕坐标）；平台未注入（恒
 *             (0,0)）时回落控件局部→全局映射（XWidget_mapToGlobal 契约
 *             = 设备全局）。
 *             **不可**改用事件局部坐标：拖出脱靠时面板会换原生窗口
 *             （停靠态是宿主窗的子控件、浮动态是独立顶层窗），脱靠后
 *             事件的局部坐标仍停留在旧宿主窗的坐标系（桌面端实测：
 *             脱靠后事件局部坐标 = 旧宿主窗口局部，面板跟指方程每步
 *             又把窗口自身原点加回一次 → 位置逐事件放大数百倍，浮动
 *             面板被甩到屏幕外「一点击就消失」= 本次根修对象）。
 * @param      self 目标控件（回落映射用）。
 * @param      me 鼠标事件（取全局坐标）。
 * @param      local 事件局部坐标（回落映射用）。
 * @return     设备全局坐标。
 */
static XPoint xdw_pointerGlobal(const XWidget* self, const XMouseEvent* me,
                                const XPoint* local)
{
    XPoint g = XMouseEvent_globalPosition(me);
    if (g.x == 0 && g.y == 0)
        g = XWidget_mapToGlobal(self, local);
    return g;
}

/**
 * @brief      浮动态移窗后的让位条带归位 + 自身内容直搬（fbdev 无 WM）。
 * @details    fbdev 单屏无 WM：顶层窗共用同一面板缓冲，窗口移开后旧位
 *             像素无人重绘=残影永留（昆仑通态屏实测）。两步（对标键盘
 *             紧凑悬浮拖移 xkb_compactDragMovePresent 同款机制）：
 *             ①让位条带按归属归位还原（XWindowDecoration_restoreExpose-
 *             Strips：条带与其他可见顶层的交集自各归属后备缓冲直搬，
 *             裸露面板余部填桌面底色）；②本窗既有后备缓冲整窗直搬两缓
 *             冲（纯移动内容零变化，零重绘零翻页）。后备缺位（未首绘等
 *             罕见态）退化为整窗 update（旧行为，慢但无残影）。
 * @param      dock 目标停靠面板。
 * @param      oldG 移窗前几何（全局坐标）。
 * @param      newG 移窗后几何（须先落几何再调用）。
 * @return     无返回值。
 */
static void xdw_restoreMovedArea(XDockWidget* dock, const XRect* oldG,
                                 const XRect* newG)
{
    XWindow* win;
    if (!dock || !oldG || !newG) return;
    win = (XWindow*)XWidget_windowHandle((XWidget*)dock);
    if (!win) return;
    /* ①让位条带归位（桌面 WM 环境为 no-op；fbdev 单屏必需）。 */
    XWindowDecoration_restoreExposeStrips(oldG, newG, win);
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON
    /* ②自身内容直搬（fbdev 免重绘移窗；桌面环境面板直写为 no-op）。
     * [悬垂绑定闸] XBackingStore 的 m_window 是**借用**指针，创建于首次
     * flush（XWidget.c 的 `XBackingStore_create_ex(..., top->m_windowHandle)`）
     * 且此后不重绑；浮→停这一跳 XWidget_setParent 会销毁桥接窗
     * （XWidget_destroyWindow），再次浮出时重建的是**另一个**窗口对象
     * ——旧绑定随即悬垂。blitPanelRects 内部按该绑定读旋转角/几何
     * （XPlatformBackingStore_posix.c:2503 → XWindow_contentRotationDegrees），
     * 悬垂即段错误（实测：XGuiDockDrag_Test 第二轮拖出稳定 AV，栈
     * xdw_restoreMovedArea ← VX_dockWidget_mouseMoveEvent）。
     * 绑定与当前句柄不一致时跳过直搬，退化为整窗 update 让常规管线
     * 补画（后备绑定随下一次 flush 重建）。 */
    {
        XBackingStore* bs = XWidget_backingStore((XWidget*)dock);
        XPlatformBackingStore* pbs = bs ? XBackingStore_handle(bs) : NULL;
        XImage* img = pbs ? XPlatformBackingStore_paintDevice(pbs) : NULL;
        if (pbs && img && !XImage_isNull(img) &&
            XBackingStore_window(bs) == win)
        {
            XRect whole;
            XPoint origin;
            XRect_init(&whole, newG->x, newG->y, newG->width, newG->height);
            XPoint_init(&origin, newG->x, newG->y);
            XPlatformBackingStore_blitPanelRects(pbs, &whole, 1, &origin);
            return;
        }
    }
#endif
    /* 罕见退化（后备缺位/绑定悬垂/裁剪构建）：整窗 update 让常规管线补画。 */
    XWidget_update((XWidget*)dock);
}

/**
 * @brief      停靠落位后同步重绘宿主（合成器语义的当帧收口）。
 * @details    对标 Qt QDockWidgetPrivate::setWindowState 的两处失效
 *             （qdockwidget.cpp:1198-1201 `if (q->isVisible()) q->hide();`
 *             → QWidgetPrivate::hide_sys 作废旧位、
 *             qwidget.cpp:7326-7336 setGeometry_sys 的旧∪新双失效）。
 *             Qt 由窗口系统/合成器保证失效区当帧重合成；本框架的 fbdev
 *             单屏模型里框架自己就是合成器，而落位链的标脏全部是**异步**
 *             的：
 *             - XDockWidget setFloating(false) → setParent + 宿主
 *               xmw_layout → 每个面板的 XWidget_setGeometry →
 *               XWidget_recomputeGeometry 的 XWidget_updateRect(parent,…)
 *               只是并入宿主 m_dirty 并投递 PAINT；
 *             - 而该 PAINT 在投递时刻就把 m_dirty 深拷贝进事件
 *               （XWidget_createPaintEvent），随后 xmw_layout 里其余面板的
 *               几何变更产生的矩形落在**这份快照之外**，只能等下一拍。
 *             实测（XGuiDockDrag_Test cycle 场景）：面板已回到停靠位，
 *             可见缓冲在该矩形上仍是宿主底色，强制整窗重绘后才出现。
 *             故此处按 X11 合成器语义当帧同步收口：宿主整窗同步重绘一次
 *             （XWidget_repaint 的同步绘制+提交），旧浮动态矩形与新停靠
 *             矩形都包含在内，无需逐矩形。旋转态（宿主或面板 rot≠0）两域
 *             不一致，跳过同步收口交常规异步失效兜底（rotate 分支本身会
 *             整帧提交，无此缺口）。
 *             代价=每次浮/停切换一次整窗重绘（切换是低频操作，非逐移动
 *             路径）；根治（PAINT 快照与 m_dirty 的增量合并）在
 *             XWidget.c，非本文件可改（见交付报告 unresolved）。
 * @param      dock 目标停靠面板；可为 NULL。
 * @return     无返回值。
 */
static void xdw_syncRepaintHostArea(XDockWidget* dock)
{
    XWidget* selfw;
    XWidget* host;
    XWindow* hostWin;
    XWindow* dockWin;
    if (!dock) return;
    selfw = (XWidget*)dock;
    host = dock->m_host;
    if (!host) return;
    if (!XWidget_isVisible(host) || !XWidget_isVisible(selfw)) return;
    hostWin = (XWindow*)XWidget_windowHandle(host);
    dockWin = (XWindow*)XWidget_windowHandle(selfw);
    if ((hostWin && XWindow_contentRotationDegrees(hostWin) != 0) ||
        (dockWin && XWindow_contentRotationDegrees(dockWin) != 0))
        return;
    XWidget_repaint(host);
}

/**
 * @brief      判断面板是否启用垂直标题条。
 * @details    对标 Qt QStyleOptionDockWidget::verticalTitleBar（绘制经
 *             transposed 转置，qcommonstyle.cpp:2171-2179；布局经
 *             QDockWidgetLayout 垂直标题区分支，qdockwidget.cpp:504-510）。
 * @param      dock 目标停靠面板；可为 NULL。
 * @return     XDockWidgetFeature_VerticalTitleBar 置位返回 true。
 */
static bool xdw_vertical(const XDockWidget* dock)
{
    return dock && (dock->m_features & XDockWidgetFeature_VerticalTitleBar);
}

/**
 * @brief      平台是否支持由窗口管理器绘制停靠浮窗的原生装饰。
 * @details    对标 Qt QDockWidgetLayout::wmSupportsNativeWindowDeco
 *             （qdockwidget.cpp:222-231）：
 *             - Android（`__ANDROID__`）恒 false（qdockwidget.cpp:222-224）；
 *             - xcb / wayland 平台名恒 false（qdockwidget.cpp:226-230）；
 *             - 其余后端（win32 等）true（qdockwidget.cpp:231）。
 *             本框架后端口径：win32（`_WIN32`，原生工具窗带系统标题条）
 *             ⇒ true；其余（fbdev 直写单屏、X11/posix、Android）⇒ false。
 *             纯编译期判定，无运行时探测开销。fbdev 直写单屏没有窗口
 *             管理器，X11 由平台层按 _MOTIF_WM_HINTS 交 WM 而控件自绘
 *             标题条（与 Qt xcb 分支同口径）——两者都是 false。
 * @return     平台可绘制原生停靠浮窗装饰返回 true。
 */
static bool xdw_wmSupportsNativeWindowDeco(void)
{
#if defined(_WIN32) && !defined(__ANDROID__)
    /* qdockwidget.cpp:231 的「非 Android 且非 xcb/wayland」分支。 */
    return true;
#else
    /* qdockwidget.cpp:222-230：Android / xcb / wayland 恒 false。 */
    return false;
#endif
}

/**
 * @brief      浮动态是否由原生标题条装饰（nativeDeco 路径）。
 * @details    对标 Qt QDockWidgetLayout::nativeWindowDeco
 *             （qdockwidget.cpp:246-250）：`wmSupportsNativeWindowDeco()
 *             && floating && 无自定义标题条`。**平台门不可省**：Qt 在
 *             xcb/wayland/Android 上恒 false ⇒ 自绘标题条 + FramelessWindowHint；
 *             旧实现只判 `m_floating && !titleBar`，在 X11 上会错误宣称
 *             原生装饰（不绘标题条、也不设无边框位 ⇒ 与 WM 标题条叠成
 *             双标题，且控件侧标题拖动/关闭命中全被门掉）。
 *             原生装饰形态下标题由原生工具窗标题承载（Tool 窗型 + 定制
 *             标题位，见 xdw_setFloatingImpl），控件不绘标题带、不布局
 *             标题区、不保留按钮命中区（qdockwidget.cpp:684-685
 *             hideButtons = nativeDeco）。
 * @param      dock 目标停靠面板；可为 NULL。
 * @return     平台支持原生装饰、浮动且无自定义标题条时返回 true。
 */
static XWidget* xdw_titleBar(const XDockWidget* dock);

static bool xdw_nativeDeco(const XDockWidget* dock)
{
    return dock && xdw_wmSupportsNativeWindowDeco() && dock->m_floating &&
           !xdw_titleBar(dock);
}

/**
 * @brief      判断标题条局部坐标是否命中关闭按钮。
 * @details    水平标题条命中右侧 XDW_CLOSE_BOX 条带；垂直标题条对标
 *             Qt 的转置几何（qcommonstyle.cpp:3082-3093 按钮沉到转置
 *             矩形右缘 = 竖条底部）：命中竖条底部 XDW_CLOSE_BOX 条带。
 * @param      dock 目标停靠面板；可为 NULL。
 * @param      pos 面板局部坐标；可为 NULL。
 * @return     命中标题条关闭区返回 true。
 */
static bool xdw_closeHit(const XDockWidget* dock, const XPoint* pos)
{
    int w;
    int h;
    if (!dock || !pos) return false;
    /* 原生装饰浮动态无自定义标题带（xdw_nativeDeco），关闭钮由原生
     * 标题条承载，控件侧命中区不复存在（对标 Qt updateButtons 的
     * hideButtons = nativeDeco 分支）。 */
    if (xdw_nativeDeco(dock)) return false;
    if (xdw_vertical(dock)) {
        h = XWidget_height((XWidget*)dock);
        return pos->x >= 0 && pos->x < XDW_TITLE_H &&
               pos->y >= h - XDW_CLOSE_BOX && pos->y < h;
    }
    w = XWidget_width((XWidget*)dock);
    return pos->y >= 0 && pos->y < XDW_TITLE_H &&
           pos->x >= w - XDW_CLOSE_BOX && pos->x < w;
}

/**
 * @brief      判断局部坐标是否落在浮动按钮内（关闭钮左侧一格/垂直条底部
 *             上一格；Floatable 时两种状态都显示，点击切换停靠/浮动）。
 * @details    对标 Qt QDockWidgetPrivate::init 创建 Float 与 Close 两个
 *             QDockWidgetTitleButton（qdockwidget.cpp:602-618），布局期
 *             按 style 的 SE_DockWidgetFloatButton/SE_DockWidgetCloseButton
 *             定位（qdockwidget.cpp:518-536）。本框架的点阵/公共风格把
 *             两格定在标题条右缘各 18px（close 最右、float 次之，见
 *             XCommonStyle.c 的 xcs_drawDockTitle 按钮几何），故命中区
 *             取同一格：水平标题条 [w-36, w-18)；垂直标题条与关闭钮同
 *             口径转置（close 沉底，float 占其上一格）。
 *             原生装饰浮动态无自定义标题带（xdw_nativeDeco），按钮由
 *             原生标题条承载，控件侧命中区不复存在（对标 Qt
 *             updateButtons 的 hideButtons = nativeDeco 分支）。
 * @param      dock 目标停靠面板；可为 NULL。
 * @param      pos 面板局部坐标；可为 NULL。
 * @return     命中浮动按钮区返回 true。
 */
static bool xdw_floatHit(const XDockWidget* dock, const XPoint* pos)
{
    int w;
    int h;
    if (!dock || !pos) return false;
    if (xdw_nativeDeco(dock)) return false;
    /* 自定义标题条在位时标题区交互归其子控件（见 mousePressEvent 门）。 */
    if (xdw_titleBar(dock)) return false;
    if (xdw_vertical(dock)) {
        h = XWidget_height((XWidget*)dock);
        return pos->x >= 0 && pos->x < XDW_TITLE_H &&
               pos->y >= h - XDW_CLOSE_BOX * 2 &&
               pos->y < h - XDW_CLOSE_BOX;
    }
    w = XWidget_width((XWidget*)dock);
    return pos->y >= 0 && pos->y < XDW_TITLE_H &&
           pos->x >= w - XDW_CLOSE_BOX * 2 && pos->x < w - XDW_CLOSE_BOX;
}

/**
 * @brief      判断局部坐标是否落在标题条内。
 * @details    水平标题条取顶部 XDW_TITLE_H 高条带；垂直标题条取左侧
 *             XDW_TITLE_H 宽竖条（与布局/绘制同一口径，见
 *             xdw_layoutTitleBar/xdw_paintVerticalTitle）。
 * @param      dock 目标停靠面板；可为 NULL。
 * @param      pos 面板局部坐标；可为 NULL。
 * @return     坐标位于标题条带内返回 true。
 */
static bool xdw_titleHit(const XDockWidget* dock, const XPoint* pos)
{
    if (!pos) return false;
    /* 原生装饰浮动态：标题带归原生标题条，客户区按下不启动拖动
     * （浮动窗的移动由原生标题拖拽承担，对标 Qt nativeDeco 形态）。 */
    if (xdw_nativeDeco(dock)) return false;
    if (xdw_vertical(dock))
        return pos->x >= 0 && pos->x < XDW_TITLE_H;
    return pos->y >= 0 && pos->y < XDW_TITLE_H;
}

/**
 * @brief      Show 事件的 visibilityChanged 载荷（对标 Qt 的几何可见性
 *             表达式）。
 * @details    对标 Qt QDockWidget::event 的 Show 分支
 *             （qdockwidget.cpp:1638-1650）：
 *             `emit visibilityChanged(geometry().right() >= parentTopLeft.x()
 *              && geometry().bottom() >= parentTopLeft.y());`
 *             其中 parentTopLeft 在浮动态取关联屏（无则主屏）的
 *             availableVirtualGeometry().topLeft()，停靠态恒 (0,0)。
 *             语义=「窗口右下角至少落在屏幕可用区内」——完全移出屏幕左上
 *             之外的浮窗报 false。**不是** isVisible 的复读（Qt 的 Hide
 *             分支载荷恒 false，见 qdockwidget.cpp:1636-1637）。
 * @param      dock 目标停靠面板；可为 NULL。
 * @return     几何与屏幕可用区相交返回 true。
 */
static bool xdw_visiblePayload(const XDockWidget* dock)
{
    XPoint origin;
    int right;
    int bottom;
    int topLeftX = 0;
    int topLeftY = 0;
    if (!dock) return false;
    if (dock->m_floating) {
#if XGUIAPPLICATION_ON && XSCREEN_ON
        XScreen* screen = XGuiApplication_primaryScreen();
        if (screen) {
            XRect avail = XScreen_availableGeometry(screen);
            topLeftX = avail.x;
            topLeftY = avail.y;
        }
#endif /* XGUIAPPLICATION_ON && XSCREEN_ON */
    }
    XPoint_init(&origin, 0, 0);
    origin = XWidget_mapToGlobal((const XWidget*)dock, &origin);
    right = origin.x + XWidget_width((const XWidget*)dock) - 1;
    bottom = origin.y + XWidget_height((const XWidget*)dock) - 1;
    /* Qt 的 geometry().right()/bottom() 是闭区间末像素。 */
    return right >= topLeftX && bottom >= topLeftY;
}

/**
 * @brief      广播可见性变化并联动宿主与切换动作。
 * @details    对标 Qt QDockWidget::event 的 Hide/Show 分支
 *             （qdockwidget.cpp:1634-1650）：
 *             - Hide：`layout->keepSize(this); toggleViewAction->
 *               setChecked(false); emit visibilityChanged(false);`
 *             - Show：`toggleViewAction->setChecked(true);
 *               emit visibilityChanged(<几何可见性表达式>);`
 *             Qt **不做去重**（事件驱动逐次发射）；旧实现用
 *             m_announcedVisible 去重且载荷恒传 isVisible，与 Qt 两处
 *             都不一致。
 *             keepSize 的等价物：本框架无 KeepSize 标志（全仓 grep 零
 *             命中），停靠区尺寸由 XMainWindow 的 m_dockHeights 记账，
 *             隐藏面板时 xmw_layout 会跳过它并把空间让给同区其余面板
 *             ——与 Qt keepSize「保留该面板占位」不同，属已知差异（需要
 *             改 XMainWindow.c，见交付报告 unresolved）。
 * @param      dock 目标停靠面板；可为 NULL。
 * @param      visible 事件来源（true=Show 分支，false=Hide 分支）。
 * @return     无返回值。
 */
static void xdw_announceVisible(XDockWidget* dock, bool visible)
{
    bool payload;
    if (!dock) return;
    if (dock->m_toggleAction)
        XAction_setChecked(dock->m_toggleAction, visible);
    payload = visible ? xdw_visiblePayload(dock) : false;
    xdw_emitBool(dock, (size_t)XDockWidget_visibilityChanged_signal, payload);
    if (dock->m_host)
        XMainWindow_updateDockLayout((XMainWindow*)dock->m_host);
}

/**
 * @brief      把内容控件摆到标题条以下全部区域。
 * @details    对标 Qt：QDockWidget 的内容控件恒填充标题条以外的全部
 *             客户区，停靠/浮动/缩放三态都跟随（QDockWidgetLayout 总是
 *             把 contents 重设为标题栏旁整块）。水平标题条时内容区为
 *             标题条（含分隔线，XDW_TITLE_H）以下整块；垂直标题条时
 *             对标 qdockwidget.cpp:541-543（r.setLeft(_titleArea.right()
 *             + 1)）改为标题竖条右侧整块。尺寸不足标题条时钳位 0（与
 *             既有 setWidget 口径一致）。
 * @param      dock 目标停靠面板；可为 NULL。
 * @return     无返回值。
 */
static void xdw_layoutContent(XDockWidget* dock)
{
    XRect r;
    int w;
    int h;
    if (!dock || !dock->m_widget) return;
    w = XWidget_width((XWidget*)dock);
    h = XWidget_height((XWidget*)dock);
    if (xdw_nativeDeco(dock)) {
        /* 原生装饰浮动态：标题由原生标题条承载（框架外），客户区全部
         * 归内容（对标 QDockWidgetLayout 的 !nativeDeco 门——仅无原生
         * 装饰时把标题高度计入内容布局）。 */
        XRect_init(&r, 0, 0, w, h);
    } else if (xdw_vertical(dock)) {
        /* 垂直标题条：内容区在竖条右侧（qdockwidget.cpp:541-543）。 */
        XRect_init(&r, XDW_TITLE_H, 0,
                   w > XDW_TITLE_H ? w - XDW_TITLE_H : 0, h);
    } else {
        XRect_init(&r, 0, XDW_TITLE_H, w,
                   h > XDW_TITLE_H ? h - XDW_TITLE_H : 0);
    }
    XWidget_setGeometryRect(dock->m_widget, &r);
}

/**
 * @brief      读取当前自定义标题条（随宏态分流：父类槽位或自有成员）。
 * @details    XGUI_CUSTOM_TITLEBAR_ON=1 时标题条存于父类控件级槽位，
 *             统一经 XWidget_titleBarWidget 读槽（写入经本类薄包装
 *             setter：父类挂载 + 停靠侧挂载即钉位，同走该槽位）；=0 时
 *             读本类自有成员，行为与改造前逐行等价。
 * @param      dock 目标停靠面板；可为 NULL。
 * @return     自定义标题条借用指针；未设置或空指针返回 NULL。
 */
static XWidget* xdw_titleBar(const XDockWidget* dock)
{
#if XGUI_CUSTOM_TITLEBAR_ON
    return XWidget_titleBarWidget((const XWidget*)dock);
#else
    return dock ? dock->m_titleBar : NULL;
#endif
}

/**
 * @brief      把自定义标题条摆满标题条区（复扫 R-83 配套）。
 * @details    对标 Qt QDockWidgetLayout：自定义标题条由面板布局接管，
 *             置于标题条区呈现。水平标题条时标题条区高度恒 XDW_TITLE_H
 *             （与内容区起点同口径），自定义条沿该区铺满宽度；垂直标题
 *             条时对标 qdockwidget.cpp:504-506（_titleArea =
 *             QSize(titleHeight, height - fw*2)）转为左侧 XDW_TITLE_H
 *             宽竖条、铺满全高；控件自身已有有效尺寸（0<有效边<=
 *             XDW_TITLE_H）时尊重其厚度。
 * @param      dock 目标停靠面板；可为 NULL。
 * @return     无返回值。
 */
static void xdw_layoutTitleBar(XDockWidget* dock)
{
    XRect r;
    int w;
    int h;
    XWidget* bar = xdw_titleBar(dock);
    if (!dock || !bar) return;
    w = XWidget_width((XWidget*)dock);
    h = XWidget_height(bar);
    if (h < 1 || h > XDW_TITLE_H) h = XDW_TITLE_H;
    XRect_init(&r, 0, 0, w, h);
    XWidget_setGeometryRect(bar, &r);
}

/**
 * @brief      按可用宽度绘制单行标题，超宽逐字截断补省略号。
 * @details    手绘路径的标题省略（对标 qcommonstyle CE_DockWidgetTitle
 *             的文本呈现：qcommonstyle.cpp:2184-2185 drawItemText，内部
 *             经 QCommonStylePrivate::drawItemText 的 elidedText，
 *             qcommonstyle.cpp:947）。量测用 XPainter_textWidth
 *             （XPainter.h:1273，取绘制器当前字体 XPainter_font）：
 *             整串放得下则原样单次绘制；放不下时按 UTF-8 码点边界
 *             （XTextUtf8_seqLen，绝不切在多字节序列中间）逐字累进、
 *             宽度经 XPainter_textWidthRange 逐级复测，直至"前缀宽 +
 *             省略号宽"超出可用宽为止，补 "..." 输出。
 * @param      painter 目标绘制器（已绑定设备）。
 * @param      x 首字形左上角 X。
 * @param      baselineY 文本基线 Y。
 * @param      utf8 UTF-8 标题文本；NULL/空串直接无操作。
 * @param      avail 可用宽度（像素）。
 * @param      color ARGB32 文本颜色。
 * @return     无返回值。
 */
static void xdw_drawElidedText(XPainter* painter, int x, int baselineY,
                               const char* utf8, int avail, uint32_t color)
{
    const XFont* font;
    int titleLen;
    int dotsW;
    int maxW;
    int keep;
    int len;
    char* buf;
    if (!painter || !utf8 || avail <= 0) return;
    font = XPainter_font(painter);
    if (XPainter_textWidth(font, utf8) <= avail) {
        /* 放得下：整串原样绘制（不触发省略）。 */
        XPainter_drawText(painter, x, baselineY, utf8, color);
        return;
    }
    dotsW = XPainter_textWidth(font, "...");
    maxW = avail - dotsW;
    titleLen = (int)XStrlen(utf8);
    keep = 0;
    len = 0;
    if (maxW > 0) {
        while (len < titleLen) {
            int w;
            len += XTextUtf8_seqLen(utf8 + len, titleLen - len);
            w = XPainter_textWidthRange(font, utf8, 0, len);
            if (w > maxW) break;
            keep = len;
        }
    }
    if (keep <= 0) return;
    /* 前缀 + 三个点 + NUL，恰好 keep+4 字节（库内 XMemory 分配，
     * 与 XCommonStyle 的省略路径同一口径）。 */
    buf = (char*)XMalloc_System((size_t)keep + 4);
    if (!buf) return;
    XStrncpy(buf, utf8, (size_t)keep);
    buf[keep] = '\0';
    XStrcpy(buf + keep, "...");
    XPainter_drawText(painter, x, baselineY, buf, color);
    XFree_System(buf);
}

/**
 * @brief      垂直标题条自绘：高亮竖条 + 标题逐字竖排 + 底部关闭标记。
 * @details    对标 Qt CE_DockWidgetTitle 的 verticalTitleBar 转置呈现
 *             （qcommonstyle.cpp:2170-2179：r=r.transposed() 后旋转 -90°
 *             绘制；本框架点阵字库按逐字形下排近似）与按钮转置沉底
 *             （qcommonstyle.cpp:3082-3093：转置几何下按钮贴右缘 =
 *             竖条底部）。标题条区为左侧 XDW_TITLE_H 宽竖条（与
 *             xdw_layoutContent/xdw_layoutTitleBar 同口径）：
 *             - 竖排起点基线 14（与水平路径同顶部留白），字进距取行高
 *               （XPainter_textHeight）；
 *             - 底部让位关闭钮命中区（Closable 时 XDW_CLOSE_BOX）；
 *             - 右缘 1px 竖分隔线对应水平路径底部分隔线。
 * @param      dock 目标停靠面板；可为 NULL。
 * @param      painter 目标绘制器（已绑定设备并完成平移）。
 * @param      h 面板高度（像素）。
 * @param      highlight 标题条底色（ARGB32）。
 * @param      windowText 文本/分隔线颜色（ARGB32）。
 * @return     无返回值。
 */
static void xdw_paintVerticalTitle(XDockWidget* dock, XPainter* painter,
                                   int h, uint32_t highlight,
                                   uint32_t windowText)
{
    XRect band;
    XRect line;
    const char* utf8;
    const XFont* font;
    int advance;
    int limitY;
    int baseline;
    int p;
    int len;
    int closeX;
    if (!dock || !painter) return;
    /* 竖条底色（水平路径 head 区的对改造型）。 */
    XRect_init(&band, 0, 0, XDW_TITLE_H, h);
    XPainter_fillRect(painter, &band, highlight);
    /* 标题逐字竖排：逐码点推进（XTextUtf8_seqLen 不切多字节序列），
     * 每字横向居中于竖条、纵向按行高下移。 */
    utf8 = dock->m_title ? XString_toUtf8(dock->m_title) : "";
    if (utf8 && utf8[0]) {
        font = XPainter_font(painter);
        advance = XPainter_textHeight(font);
        if (advance < 1) advance = 1;
        /* 底部按钮区让位：关闭钮占最后一格，浮动钮占其上一格
         * （与 xdw_closeHit/xdw_floatHit 的垂直分支同几何）。 */
        limitY = h - ((dock->m_features & XDockWidgetFeature_Closable)
                          ? XDW_CLOSE_BOX : 0) -
                 ((dock->m_features & XDockWidgetFeature_Floatable)
                          ? XDW_CLOSE_BOX : 0) -
                 XDW_TITLE_SLACK;
        len = (int)XStrlen(utf8);
        baseline = 14;
        p = 0;
        while (p < len && baseline <= limitY) {
            int glyphLen = XTextUtf8_seqLen(utf8 + p, len - p);
            int glyphW;
            int x;
            if (glyphLen <= 0) break;
            glyphW = XPainter_textWidthRange(font, utf8, p, p + glyphLen);
            x = (XDW_TITLE_H - glyphW) / 2;
            if (x < 0) x = 0;
            XPainter_drawGlyph(painter, x, baseline, utf8 + p, windowText);
            p += glyphLen;
            baseline += advance;
        }
    }
    if (dock->m_features & XDockWidgetFeature_Floatable) {
        /* 浮动标记：关闭钮上一格（命中区见 xdw_floatHit 垂直分支）。 */
        int fy = h - XDW_CLOSE_BOX * 2 + 4;
        XRect_init(&line, 5, fy, 10, 1);
        XPainter_fillRect(painter, &line, windowText);
        XRect_init(&line, 5, fy + 8, 10, 1);
        XPainter_fillRect(painter, &line, windowText);
        XRect_init(&line, 5, fy, 1, 9);
        XPainter_fillRect(painter, &line, windowText);
        XRect_init(&line, 14, fy, 1, 9);
        XPainter_fillRect(painter, &line, windowText);
    }
    if (dock->m_features & XDockWidgetFeature_Closable) {
        /* 关闭标记沉底（命中区见 xdw_closeHit 垂直分支）。 */
        closeX = (XDW_TITLE_H -
                  XPainter_textWidth(XPainter_font(painter), "×")) / 2;
        if (closeX < 0) closeX = 0;
        XPainter_drawText(painter, closeX, h - XDW_CLOSE_BOX + 14,
                          "×", windowText);
    }
    /* 竖条右缘分隔线（水平路径底部分隔线的对改造型）。 */
    XRect_init(&line, XDW_TITLE_H, 0, 1, h);
    XPainter_fillRect(painter, &line, windowText);
}

/* ==================== 事件处理 ==================== */

static void VX_dockWidget_paintEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect head;
    XRect line;
    uint32_t highlight;
    uint32_t windowText;
    int w;
    int h;
    if (!dock || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    /* 对标 Qt QDockWidget::paintEvent：先以 palette Window 回填整片
     * 面板背景。浮动态面板是独立顶层窗口，无宿主背景兜底——不回填
     * 时标题条以下露出原生窗口黑底，内容文字（深色）画在黑底上不
     * 可见（实测浮出面板内容区全黑）；停靠态宿主 autofill 同色，
     * 观感不变。 */
    {
        XRect full;
        uint32_t window;
#if XPALETTE_ON
        XPalette dpalette = XWidget_palette(self);
        XColor c = XPalette_color(&dpalette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Window);
        window = XColor_rgba(&c);
#else
        window = 0xFFF0F0F0u;
#endif /* XPALETTE_ON */
        XRect_init(&full, 0, 0, w, XWidget_height(self));
        XPainter_fillRect(&painter, &full, window);
    }
    if (xdw_nativeDeco(dock)) {
        /* 原生装饰浮动态：标题由原生工具窗标题承载，控件不再绘制标题
         * 带/分隔线/按钮（对标 Qt QDockWidget::paintEvent 的
         * `!nativeDeco && !customTitleBar` 绘制门，qdockwidget.cpp；
         * 否则与原生标题叠成双标题栏，实测浮出窗口双份 □ ×）。 */
        XPainter_deinit(&painter);
        return;
    }
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Highlight);
        highlight = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_WindowText);
        windowText = XColor_rgba(&c);
    }
#else
    highlight = 0xFF3080C0u;
    windowText = 0xFF000000u;
#endif /* XPALETTE_ON */
#if XSTYLE_ON
    /* 复扫 R-83：自定义标题条接管呈现时，默认标题带不再由本控件绘制
     * （对标 Qt QDockWidgetLayout——自定义条即标题区的唯一呈现者），
     * 该区域交由自定义条子控件自绘。 */
    if (XStyle_defaultStyle() != NULL && !xdw_titleBar(dock)) {
        /* Fusion/公共风格接管：标题栏走 CE_DockWidgetTitle
         * （highlight 标题条 + 标题文本 + 底部分隔线）。 */
        XStyle* style = XStyle_defaultStyle();
        XStyleOption opt;
        XStyleOption_init(&opt, XStyleCE_DockWidgetTitle);
        XRect_init(&opt.m_rect, 0, 0, w, 20);
        opt.m_state = XWidget_isEnabled((XWidget*)dock)
            ? XStyleState_Enabled : 0;
        opt.m_text = dock->m_title ? XString_toUtf8(dock->m_title) : "";
        opt.m_closable = (dock->m_features &
                          XDockWidgetFeature_Closable) != 0;
        opt.m_movable = (dock->m_features &
                         XDockWidgetFeature_Movable) != 0;
        opt.m_floatable = (dock->m_features &
                           XDockWidgetFeature_Floatable) != 0;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette((XWidget*)dock);
#endif
        XStyle_drawControl(style, XStyleCE_DockWidgetTitle, &opt, &painter,
                           (XWidget*)dock);
        XPainter_deinit(&painter);
        return;
    }
#endif /* XSTYLE_ON */
    if (xdw_titleBar(dock)) {
        /* 自定义标题条接管：跳过默认标题带/分隔线绘制。 */
        XPainter_deinit(&painter);
        return;
    }
    if (xdw_vertical(dock)) {
        /* 垂直标题条：左侧竖条自绘（逐字竖排 + 按钮沉底）。 */
        xdw_paintVerticalTitle(dock, &painter, h, highlight, windowText);
        XPainter_deinit(&painter);
        return;
    }
    XRect_init(&head, 0, 0, w, 20);
    XPainter_fillRect(&painter, &head, highlight);
    /* 标题文本：超宽省略（对标 qcommonstyle CE_DockWidgetTitle 的
     * drawItemText 省略，qcommonstyle.cpp:2184-2185/947）。可用宽度 =
     * 面板宽 − 右侧按钮区（关闭/浮动各 XDW_CLOSE_BOX）− 左边距 −
     * 右侧安全余量。 */
    xdw_drawElidedText(&painter, XDW_TITLE_LEFT_PAD, 14,
                       dock->m_title ? XString_toUtf8(dock->m_title) : "",
                       w - XDW_CLOSE_BOX * 2 - XDW_TITLE_LEFT_PAD -
                           XDW_TITLE_SLACK,
                       windowText);
    if (dock->m_features & XDockWidgetFeature_Floatable) {
        /* 浮动钮（关闭钮左侧一格，与 xdw_floatHit 命中区同几何；对标
         * Qt 标题条的 Float 按钮 SP_TitleBarNormalButton，
         * qdockwidget.cpp:604-608/706）。手绘路径画方框，与样式路径
         * XCommonStyle.c 的 xcs_drawDockTitle 浮动框同形。 */
        int bx = w - XDW_CLOSE_BOX * 2 + 4;
        XRect_init(&line, bx, 6, 10, 1);
        XPainter_fillRect(&painter, &line, windowText);
        XRect_init(&line, bx, 14, 10, 1);
        XPainter_fillRect(&painter, &line, windowText);
        XRect_init(&line, bx, 6, 1, 9);
        XPainter_fillRect(&painter, &line, windowText);
        XRect_init(&line, bx + 9, 6, 1, 9);
        XPainter_fillRect(&painter, &line, windowText);
    }
    if (dock->m_features & XDockWidgetFeature_Closable) {
        /* Closable：绘制关闭标记（对标 Qt 标题条）。 */
        XPainter_drawText(&painter, w - XDW_CLOSE_BOX + 4, 14, "×",
                          windowText);
    }
    XRect_init(&line, 0, 20, w, 1);
    XPainter_fillRect(&painter, &line, windowText);
    XPainter_deinit(&painter);
}

/**
 * @brief      鼠标按下：标题条按钮命中与拖动启动（对标 Qt 浮动
 *             QDockWidget 标题条交互）。
 * @details    关闭按钮命中且 Closable 特性开启时隐藏面板（任务裁定：
 *             close 槽回归停靠区语义简化为隐藏，面板保持登记可复显）；
 *             浮动按钮命中且 Floatable 开启时切换停靠/浮动（对标
 *             QDockWidgetTitleButton clicked→toggleTopLevel）；标题条
 *             命中且 Movable 开启时开始拖动（记录抓取偏移并提升窗口）。
 *             停靠态标题条按下且 Floatable 开启时记录起始全局点并抓
 *             取鼠标，移动越过阈值（见 mouseMoveEvent）后脱靠成独立
 *             顶层窗口跟随（对标 Qt 拖出浮动；拖拽重停靠——拖回主窗
 *             /其它停靠区——见 mouseReleaseEvent）。两类拖动启动时均
 *             按 Ctrl 修饰键记录 m_ctrlDrag（对标 qdockwidget.cpp:934，
 *             qdockwidget_p.h:50 ctrlDrag）：置位时移动不吸附宿主落点、
 *             释放不落位，面板保持浮动。
 * @param      self 目标控件。
 * @param      event 鼠标按下事件。
 * @return     无返回值。
 */
static void VX_dockWidget_mousePressEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    XMouseEvent* me;
    XPoint pos;
    XPoint pos0;
    XPoint_init(&pos0, 0, 0);
    if (!dock || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        xdw_callParent(self, MousePressEvent, event);
        return;
    }
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    /* 复扫 R-83：自定义标题条接管标题区交互——关闭钮/浮动钮/拖动属
     * 默认标题条行为；自定义条在位时按下事件归其子控件（子 ignore 才
     * 落到本处理），此处一律不再按默认条命中处理。 */
    if (!xdw_titleBar(dock) && xdw_closeHit(dock, &pos) &&
        (dock->m_features & XDockWidgetFeature_Closable)) {
        /* 对标 Qt：标题条关闭钮接 QDockWidget::close（qdockwidget.cpp:617
         * 的 connect(button, clicked, q, &QDockWidget::close)）——关闭
         * 走 closeEvent 的 Closable 判定，而非无条件隐藏（旧实现直接
         * setVisible(false)，非 Closable 面板也能「关闭」，与 Qt 相反）。 */
        XDW_TRACE("title close button: close()");
        (void)XWidget_close(self);
        XEvent_accept(event);
        return;
    }
    if (xdw_floatHit(dock, &pos) &&
        (dock->m_features & XDockWidgetFeature_Floatable) &&
        XMouseEvent_button(me) == XMouseButton_LeftButton) {
        /* 对标 Qt：标题条浮动钮 clicked→toggleTopLevel
         * （qdockwidget.cpp:605-608、qdockwidget.cpp:706 的
         * `button->setVisible(canFloat && !hideButtons)`）。 */
        XDW_TRACE("title float button: toggleTopLevel");
        XDockWidget_setFloating(dock, !dock->m_floating);
        XEvent_accept(event);
        return;
    }
    /* 本分支只处理浮动态：对标 Qt qdockwidget.cpp:918-920 的
     * `(!Movable && !isFloating()) ⇒ return false`——Movable 关闭只挡
     * **停靠态**起拖；浮窗无论 Movable 与否都可拖动（旧实现把 Movable
     * 设为硬门，Movable 关闭的浮窗完全拖不动）。 */
    if (!xdw_titleBar(dock) && dock->m_floating && xdw_titleHit(dock, &pos) &&
        XMouseEvent_button(me) == XMouseButton_LeftButton) {
        /* 原生装饰浮动态（nativeDeco）时 xdw_titleHit 恒假：标题条在
         * 客户区上方（局部 y<0 的负带），驱动已把 Tool 窗
         * WM_NCLBUTTONDOWN(HTCAPTION) 转译为本按下事件（对标 Qt
         * nativeDeco 路径 NonClientAreaMouseButtonPress → initDrag
         * (nca=true)+startDrag，qdockwidget.cpp:1097-1111）——按下即
         * 拖动，无 startDragDistance 阈值（拖回停靠走 hover/finishDrop）。 */
        XPoint g = xdw_pointerGlobal(self, me, &pos);
        XPoint org = XWidget_mapToGlobal(self, &pos0);
        XDW_TRACE("float-drag start pos=(%d,%d) global=(%d,%d) nc=%d",
                  pos.x, pos.y, g.x, g.y, xdw_nativeDeco(dock));
        dock->m_dragging = true;
        /* Ctrl 拖动判定（对标 Qt qdockwidget.cpp:933-935：
         *   `ctrlDrag = (Floatable && ControlModifier) ||
         *               (!Movable && isFloating);`
         * 第二个子句是「Movable 关闭的浮窗拖动永不重停靠」——旧实现
         * 只按 Floatable&&Control 判定，该子句缺失。置位时移动不吸附、
         * 释放不落位。 */
        dock->m_ctrlDrag =
            (((dock->m_features & XDockWidgetFeature_Floatable) != 0 &&
              (XMouseEvent_modifiers(me) &
               XKeyboardModifier_ControlModifier) != 0) ||
             ((dock->m_features & XDockWidgetFeature_Movable) == 0 &&
              dock->m_floating));
        /* 抓取偏移 = 按下点 − 面板全局原点（对标 Qt QDockWidget 的
         * dragOffset：抓取点相对浮动面板原点的偏移，跟指期恒定，
         * 面板随指针平移而抓取点不动）。设备全局域，与跟指方程
         * （mouseMoveEvent 的 g−m_dragOffset）同域。 */
        dock->m_dragOffset.x = g.x - org.x;
        dock->m_dragOffset.y = g.y - org.y;
        dock->m_pressGlobal = g; /* 释放时与当前点比较过滤原地单击 */
        XWidget_raise(self); /* 对标 Qt：拖动前激活提升浮动窗口 */
        /* 全局鼠标抓取：指针拖出窗口后事件仍转投本控件（win32 普通
         * 移动消息不带全局坐标，抓取同时保证拖动坐标链稳定）。 */
        XWidget_grabMouse(self);
        XEvent_accept(event);
        return;
    }
    if (!xdw_titleBar(dock) && !dock->m_floating && dock->m_host &&
        xdw_titleHit(dock, &pos) &&
        (dock->m_features & XDockWidgetFeature_Floatable) &&
        (dock->m_features & XDockWidgetFeature_Movable) &&
        XMouseEvent_button(me) == XMouseButton_LeftButton) {
        /* 停靠态拖出（对标 Qt initDrag）：按下仅记录起始全局点并抓
         * 取鼠标，移动越过 startDragDistance 阈值才脱靠。 */
        XPoint armG = xdw_pointerGlobal(self, me, &pos);
        XPoint org = XWidget_mapToGlobal(self, &pos0);
        XDW_TRACE("docked-drag arm pos=(%d,%d) global=(%d,%d)",
                  pos.x, pos.y, armG.x, armG.y);
        dock->m_dragging = true;
        /* Ctrl 拖动判定（对标 Qt qdockwidget.cpp:933-935；分支条件已保证
         * Floatable 与 Movable，此处只看 ControlModifier）。 */
        dock->m_ctrlDrag =
            (XMouseEvent_modifiers(me) &
             XKeyboardModifier_ControlModifier) != 0;
        /* 抓取偏移 = 按下点 − 面板全局原点（停靠态面板的全局原点 =
         * 其在宿主窗内的位置 + 宿主窗原点；脱靠后浮动面板按同一全局
         * 原点原位落出，抓取偏移跨态不变——指针相对标题条的落点保持
         * 不变，脱靠瞬间面板不跳位）。 */
        dock->m_dragOffset.x = armG.x - org.x;
        dock->m_dragOffset.y = armG.y - org.y;
        /* 脱靠阈值与释放落位阈值的同域基准（设备全局）。 */
        dock->m_pressGlobal = armG;
        XWidget_grabMouse(self);
        XEvent_accept(event);
        return;
    }
    xdw_callParent(self, MousePressEvent, event);
}

/**
 * @brief      鼠标双击：标题条内且 Floatable 时切换停靠/浮动（对标
 *             QDockWidget::mouseDoubleClickEvent → toggleTopLevel）。
 * @param      self 目标控件。
 * @param      event 鼠标双击事件。
 * @return     无返回值。
 */
static void VX_dockWidget_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!dock || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) {
        xdw_callParent(self, MouseDoubleClickEvent, event);
        return;
    }
    if (xdw_titleBar(dock)) {
        xdw_callParent(self, MouseDoubleClickEvent, event);
        return;
    }
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if ((xdw_titleHit(dock, &pos) ||
         (xdw_nativeDeco(dock) && pos.y < 0)) &&
        (dock->m_features & XDockWidgetFeature_Floatable) &&
        XMouseEvent_button(me) == XMouseButton_LeftButton) {
        /* nativeDeco 标题条双击（客户区负 y 带）同样切换：对标 Qt
         * NonClientAreaMouseButtonDblClick → toggleTopLevel（qdockwidget.cpp:
         * 1128），浮动工具窗无自定义条时的唯一浮/停切换手势之一。 */
        XDockWidget_setFloating(dock, !dock->m_floating);
        XDW_TRACE("dbl-click: toggle -> floating=%d",
                  dock->m_floating ? 1 : 0);
        XEvent_accept(event);
        return;
    }
    xdw_callParent(self, MouseDoubleClickEvent, event);
}

/**
 * @brief      鼠标移动：拖动中的浮动窗口跟随全局坐标平移（对标 Qt 拖动
 *             浮动 QDockWidget 移动顶层窗口）；停靠态拖动越过阈值先
 *             脱靠（setFloating(true)）再继续跟随；浮动拖动期间衔接
 *             宿主落点指示器。
 * @details    跟指坐标源=指针**设备全局**（XMouseEvent_globalPosition，
 *             见 xdw_pointerGlobal 注：浮动态跨原生窗口切换后事件局部
 *             坐标失效，必须走全局坐标）。落位方程=指针全局 − 抓取
 *             偏移（对标 Qt QDockWidget::startDrag 的 dragOffset 语义：
 *             抓取点相对面板原点的偏移恒定，面板随指针平移）。脱靠
 *             阈值与宿主落点链（hoverDrop/finishDrop→xmw_dropAreaAt 的
 *             mapFromGlobal）同吃设备全局。脱靠调用走内部实现体（不消费
 *             undockedGeometry，对标 Qt startDrag→unplug 用当前几何原地
 *             脱靠，qdockwidget.cpp:767；公共 setFloating 才恢复
 *             undockedGeometry，qdockwidget.cpp:1465）。浮动拖动的每次
 *             移动在落位之后调用 XMainWindow_hoverDrop 更新宿主落点
 *             指示器（对标 qdockwidget.cpp:1051-1052：!ctrlDrag 时
 *             mwlayout->hover(widgetItem, globalPos)）；Ctrl 拖动
 *             （m_ctrlDrag）与无宿主时跳过。移窗落位后另经
 *             xdw_restoreMovedArea 做让位条带归位 + 自身内容直搬
 *             （fbdev 单屏无 WM：旧位像素无人重绘=残影永留）。
 * @param      self 目标控件。
 * @param      event 鼠标移动事件。
 * @return     无返回值。
 */
static void VX_dockWidget_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    XMouseEvent* me;
    if (!dock || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) {
        xdw_callParent(self, MouseMoveEvent, event);
        return;
    }
    if (dock->m_dragging) {
        XPoint g;
        me = (XMouseEvent*)event;
                if (!(XMouseEvent_buttons(me) & XMouseButton_LeftButton)) {
            /* 按键已释放：安全终止拖动；浮动态顺带按当前位置收尾
             * （finishDrop 内部隐藏落点指示器，悬停宿主内则落位），
             * 防止指示器残留与拖动悬挂。 */
            dock->m_dragging = false;
            XWidget_releaseMouse(self);
            if (dock->m_floating && dock->m_host && !dock->m_ctrlDrag) {
                XPoint lp = XMouseEvent_position(me);
                XPoint gg = xdw_pointerGlobal(self, me, &lp);
                XMainWindow_finishDrop((XMainWindow*)dock->m_host,
                                       self, &gg);
            }
            xdw_callParent(self, MouseMoveEvent, event);
            return;
        }
        {
            XPoint local = XMouseEvent_position(me);
            /* 跟指坐标源=指针设备全局坐标（浮动态跨原生窗口切换后事件
             * 局部坐标失效，见 xdw_pointerGlobal 注）。 */
            g = xdw_pointerGlobal(self, me, &local);
        }
        /* 移动压缩（对标 Qt 鼠标事件压缩，QWidgetWindow 聚合移动）：
         * 拖动跟随是绝对定位，高频移动（真实游戏鼠标 1kHz、远端批量
         * 注入）逐条 SetWindowPos+重绘+呈现会把呈现链路压出秒级停顿
         * （OrayIdd+AMD 栈实测 2.4-5.5s 断崖，见 XDW_TRACE 时间线）。
         * 距上次跟随 <16ms 直接丢弃本条中间位置——下一条移动以最新
         * 位置跟上，松开前的最后一条永远精确落位。 */
        {
            int64_t nowMs = XDateTime_currentMSecsSinceEpoch();
            if (nowMs - xdw_lastFollowMs < 16 &&
                nowMs >= xdw_lastFollowMs) {
                XDW_TRACE("move skip (throttled) global=(%d,%d)",
                          g.x, g.y);
                XEvent_accept(event);
                return;
            }
            xdw_lastFollowMs = nowMs;
        }
        if (!dock->m_floating) {
            /* 停靠态拖出阈值（对标 Qt qdockwidget.cpp:982-985）：按下
             * 点到当前点的曼哈顿距离严格大于 QApplication::
             * startDragDistance 才启动拖动并脱靠，单击不误浮动。
             * 基准=按下点（设备全局，见 m_pressGlobal）。 */
            int dx = g.x - dock->m_pressGlobal.x;
            int dy = g.y - dock->m_pressGlobal.y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx + dy <= XApplication_startDragDistance()) {
                XEvent_accept(event);
                return;
            }
            XDW_TRACE("undock: dist=(%d,%d) -> setFloating(true)", dx, dy);
                        xdw_setFloatingImpl(dock, true, false);
                        /* 实现体内部按"程序性浮动"清了 m_dragging 并解除了抓取：
             * 本路径是拖出延续，需重新置位拖动态并重新抓取鼠标，否则
             * 跟随中断、释放时回归停靠判定因 !m_dragging 早退。 */
            dock->m_dragging = true;
            XWidget_grabMouse(self);
            /* 脱靠后面板按停靠位原位落出（全局原点不变），抓取偏移
             * （按下点−面板原点）跨态不变——指针相对标题条的落点保持
             * 不变，脱靠瞬间面板不跳位。 */
        }
        if (dock->m_floating) {
            /* 浮动态跟指：面板原点 = 指针全局坐标 − 抓取偏移（对标 Qt
             * QDockWidget::startDrag 的 dragOffset 语义）。绝对定位式
             * （非增量式）：节流丢弃的中间位置由下一条移动按最新指针
             * 位置重算，末位永远精确落位。 */
            XRect oldG = XWidget_geometry(self);
            int nx = g.x - dock->m_dragOffset.x;
            int ny = g.y - dock->m_dragOffset.y;
            if (nx != oldG.x || ny != oldG.y) {
                XRect newG;
                XRect_init(&newG, nx, ny, oldG.width, oldG.height);
                XWidget_setGeometryRect(self, &newG);
                /* 让位条带归位 + 自身内容直搬（fbdev 单屏残影根修）。 */
                xdw_restoreMovedArea(dock, &oldG, &newG);
            }
        }
        XDW_TRACE("move global=(%d,%d) win=(%d,%d)",
                  g.x, g.y, XWidget_x(self), XWidget_y(self));
        if (dock->m_floating && dock->m_host && !dock->m_ctrlDrag) {
            /* 拖动中衔接宿主落点预览（qdockwidget.cpp:1051-1052）。 */
            XMainWindow_hoverDrop((XMainWindow*)dock->m_host, self, &g);
        }
        XEvent_accept(event);
        return;
    }
    xdw_callParent(self, MouseMoveEvent, event);
}

/**
 * @brief      结束拖动：清除拖出期间的 X11BypassWindowManagerHint 并收口
 *             浮动态（对标 Qt endDrag 的浮动保留分支）。
 * @details    对标 Qt QDockWidgetPrivate::endDrag（qdockwidget.cpp:806-856）：
 *             `if (mode == Abort || !mwLayout->plug(state->widgetItem)) {
 *                if (hasFeature(Floatable)) {
 *                  ... if (!nativeWindowDeco) {
 *                        flags &= ~X11BypassWindowManagerHint;
 *                        q->setWindowFlags(flags);
 *                        setResizerActive(q->isFloating());
 *                        q->show();
 *                      } ...
 *                  if (q->isFloating()) undockedGeometry = q->geometry();
 *                  q->activateWindow();
 *                } else {
 *                  mwLayout->revert(state->widgetItem);  // 不可浮动：回退原状态
 *                }
 *              }`
 *             即「plug 失败（无落点）」这一支：可浮动 ⇒ 保持浮动态、清
 *             bypass 位、记录 undockedGeometry（拖动结束的**第二个**记录
 *             点，qdockwidget.cpp:841-842——旧实现只在 resizeEvent 记录，
 *             拖出后未 resize 则快照不是最终浮动态几何）、激活窗口；
 *             不可浮动 ⇒ 回退原停靠状态。
 * @param      dock 目标停靠面板；可为 NULL。
 * @return     无返回值。
 */
static void xdw_endDragKeepFloating(XDockWidget* dock)
{
    XWidget* selfw;
    XWidgetFlags flags;
    if (!dock) return;
    selfw = (XWidget*)dock;
    if (!dock->m_floating) return; /* 已落位：plug 成功分支无需收口。 */
    if ((dock->m_features & XDockWidgetFeature_Floatable) == 0) {
        /* 不可浮动（qdockwidget.cpp:852-855 的 revert 分支）：回到停靠态。
         * 本框架的等价动作=setFloating(false)（宿主重排把面板摆回登记
         * 区域），revert 的「布局快照回滚」由 xmw_layout 的登记区域重算
         * 承载。 */
        XDW_TRACE("endDrag: not floatable -> revert to docked");
        XDockWidget_setFloating(dock, false);
        return;
    }
    /* 清 X11BypassWindowManagerHint（qdockwidget.cpp:830-838）：拖动期间
     * 追加的绕过 WM 位在收口时必须摘掉，否则窗口在 X11 上脱离 WM 管理
     * （无装饰/无焦点策略/永在最上层）。 */
    flags = (XWidgetFlags)XWidget_windowFlags(selfw);
    if (flags & (XWidgetFlags)XWindowType_BypassWindowManagerHint) {
        flags &= ~(XWidgetFlags)XWindowType_BypassWindowManagerHint;
        XWidget_setWindowFlags(selfw, flags);
        XDW_TRACE("endDrag: cleared BypassWindowManagerHint");
    }
    /* undockedGeometry = q->geometry()（qdockwidget.cpp:841-842）：拖动
     * 结束的第二个记录点。 */
    {
        XRect geo = XWidget_geometry(selfw);
        dock->m_undockedGeometry = geo;
        dock->m_undockedValid = true;
    }
    XWidget_show(selfw);          /* qdockwidget.cpp:837 */
    XWidget_activateWindow(selfw);/* qdockwidget.cpp:856 */
}

/**
 * @brief      鼠标释放：结束标题栏拖动并解除全局鼠标抓取；浮动拖动
 *             释放经宿主落位服务衔接最终落点（对标 Qt 拖回停靠）。
 * @details    释放时先解鼠标抓取，随后把释放点全局坐标交给
 *             XMainWindow_finishDrop（对标 Qt endDrag→
 *             QMainWindowLayout::plug，qdockwidget.cpp:822）：返回
 *             非 NoDockWidgetArea 表示已落位（宿主内部完成
 *             setFloating(false)+重排，对标 plug→setWindowState）；
 *             返回 NoDockWidgetArea 表示无有效落点，面板保持浮动
 *             （对标 plug 失败分支：stay in the floating state，
 *             qdockwidget.cpp:822-824，收口见 xdw_endDragKeepFloating）。
 *             Ctrl 拖动（m_ctrlDrag）跳过落位（对标 qdockwidget.cpp:933-935
 *             置位 ctrlDrag 后 endDrag 经 layout->restore 保持浮动）。
 * @param      self 目标控件。
 * @param      event 鼠标释放事件。
 * @return     无返回值。
 */
static void VX_dockWidget_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    XMouseEvent* me;
    XPoint local;
    XPoint g;
    int area;
    if (!dock || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE ||
        !dock->m_dragging) {
        xdw_callParent(self, MouseReleaseEvent, event);
        return;
    }
    dock->m_dragging = false;
    /* 约束：落位调用在 releaseMouse 之后（先解除抓取再触发宿主重排）。 */
    XWidget_releaseMouse(self);
    if (dock->m_floating && dock->m_host && !dock->m_ctrlDrag) {
        me = (XMouseEvent*)event;
        local = XMouseEvent_position(me);
        g = xdw_pointerGlobal(self, me, &local);
        /* 对标 Qt：仅真实拖动（位移超过 startDragDistance）才尝试落位
         * ——原地单击（无位移）不落位，面板保持浮动；否则双击标题条
         * 的首击释放即回停靠、双击事件再浮出，净效果浮着不动（实测
         * "双击无法回归"根因）。 */
        {
            int dx = g.x - dock->m_pressGlobal.x;
            int dy = g.y - dock->m_pressGlobal.y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx + dy <= XApplication_startDragDistance()) {
                XDW_TRACE("release: below threshold dist=(%d,%d), "
                          "stay floating", dx, dy);
                XEvent_accept(event);
                return;
            }
        }
        XDW_TRACE("release: finishDrop global=(%d,%d)", g.x, g.y);
        area = XMainWindow_finishDrop((XMainWindow*)dock->m_host, self, &g);
        XDW_TRACE("release: finishDrop area=%d", area);
    }
    /* endDrag 收口（qdockwidget.cpp:806-856）：落位成功时面板已回停靠态
     * （m_floating=false），无需收口；其余一切仍处于浮动态的情形
     * ——plug 失败（无落点）、Ctrl 拖动、无宿主——统一走浮动保留分支
     * （清 bypass 位 + 记录 undockedGeometry + 激活；不可浮动则回退
     * 停靠态）。 */
    if (dock->m_floating)
        xdw_endDragKeepFloating(dock);
    /* 拖动状态复位（对标 Qt 拖动结束销毁 DragState：ctrlDrag=false，
     * qdockwidget.cpp:744；下次按下重新判定）。 */
    dock->m_ctrlDrag = false;
    XEvent_accept(event);
    return;
}

/** @brief 显示事件：转发父类后广播 visibilityChanged(true)。 */
static void VX_dockWidget_showEvent(XWidget* self, XEvent* event)
{
    xdw_callParent(self, ShowEvent, event);
    if (self)
        xdw_announceVisible((XDockWidget*)self, XWidget_isVisible(self));
}

/** @brief 隐藏事件：转发父类后广播 visibilityChanged(false)。 */
static void VX_dockWidget_hideEvent(XWidget* self, XEvent* event)
{
    xdw_callParent(self, HideEvent, event);
    if (self)
        xdw_announceVisible((XDockWidget*)self, false);
}

/**
 * @brief      关闭事件：按 Closable 特性决定是否接受（对标 Qt）。
 * @details    对标 Qt QDockWidget::closeEvent（qdockwidget.cpp:1576-1587）：
 *             `if (d->state) d->endDrag(Abort);`
 *             `const bool canClose = (features & DockWidgetClosable)
 *                                    || (!win || !win->isVisible());`
 *             `event->setAccepted(canClose);`
 *             即：Closable 面板接受关闭（Qt 关闭即隐藏，QWidget::close →
 *             hide）；**非 Closable 面板拒绝关闭**，唯一例外是宿主主窗
 *             已不可见（否则应用无法退出）。旧实现无条件隐藏后 ignore——
 *             非 Closable 面板照样能「关闭」，且 accept/ignore 语义与 Qt
 *             相反（Qt 接受关闭时事件被 accept）。
 *             本框架驱动侧的约定（XPlatformNativeWindow_posix.c:4482 /
 *             win32.c:1960：handleCloseEvent 被 accept 后隐藏并销毁原生
 *             窗）针对**浮动态顶层**——原生窗销毁后 toggleViewAction 复显
 *             会落空，故浮动态在此先自行隐藏（可见性传播收起原生窗，
 *             窗对象保留）再接受事件，驱动侧 destroy 分支因窗已不可见
 *             自然收敛。
 * @param      self 目标控件。
 * @param      event 关闭事件。
 * @return     无返回值。
 */
static void VX_dockWidget_closeEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    XWidget* host;
    bool canClose;
    if (!dock || !event) return;
    /* 对标 Qt：关闭时先终止进行中的拖动（qdockwidget.cpp:1579-1580
     * `if (d->state) d->endDrag(Abort);`）。 */
    if (dock->m_dragging) {
        dock->m_dragging = false;
        dock->m_ctrlDrag = false;
        XWidget_releaseMouse(self);
    }
    host = dock->m_host;
    canClose = ((dock->m_features & XDockWidgetFeature_Closable) != 0) ||
               (!host || !XWidget_isVisible(host));
    XDW_TRACE("closeEvent: canClose=%d", canClose ? 1 : 0);
    if (!canClose) {
        /* 非 Closable 且宿主可见：拒绝关闭（qdockwidget.cpp:1586-1587）。 */
        XEvent_ignore(event);
        return;
    }
    /* 接受关闭：Qt 的 close 语义即 hide（QWidget::close → setVisible(false)）。
     * 浮动态在此先行隐藏，避免驱动侧 accept→destroy 销毁原生窗。 */
    XWidget_setVisible(self, false);
    XEvent_accept(event);
}

/**
 * @brief      尺寸变更事件：内容控件重摆到标题条旁全部区域；浮动态
 *             记录 undockedGeometry。
 * @details    对标 Qt：QDockWidget 内容恒随面板尺寸跟随（停靠态主窗口
 *             重排、浮动态顶层缩放、回归停靠三态的几何变化都经
 *             ResizeEvent 到达），修复此前 setWidget 仅一次性摆位、
 *             之后内容矩形不随面板更新的缺陷。浮动态 resize 记录
 *             m_undockedGeometry（对标 qdockwidget.cpp:1710-1712：
 *             "if the mainwindow is plugging us, we don't want to
 *             update undocked geometry"——本框架回归停靠的 resize 发生
 *             在 setFloating(false) 已置 m_floating=false 之后，天然
 *             不记，无需 Qt 的 pluggingWidget 旁路）。
 * @param      self 目标控件。
 * @param      event 尺寸变更事件。
 * @return     无返回值。
 */
static void VX_dockWidget_resizeEvent(XWidget* self, XEvent* event)
{
    xdw_callParent(self, ResizeEvent, event);
    if (self) {
        XDockWidget* dock = (XDockWidget*)self;
        xdw_layoutTitleBar(dock);
        xdw_layoutContent(dock);
        if (dock->m_floating) {
            /* 浮动态几何快照（qdockwidget_p.h:88），供下次
             * setFloating(true) 恢复（qdockwidget.cpp:1465）。 */
            dock->m_undockedGeometry.x = XWidget_x(self);
            dock->m_undockedGeometry.y = XWidget_y(self);
            dock->m_undockedGeometry.width = XWidget_width(self);
            dock->m_undockedGeometry.height = XWidget_height(self);
            dock->m_undockedValid = true;
        }
    }
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_dockWidget_deinit(XDockWidget* self)
{
    if (!self) return;
    if (self->m_widget) {
        XClassDelete(self->m_widget);
        self->m_widget = NULL;
    }
    if (self->m_title) {
        XClassDelete(self->m_title);
        self->m_title = NULL;
    }
    if (self->m_toggleAction) {
        /* 切换动作归面板所有（对标 Qt toggleViewAction 归 dock 所有）。 */
        XClassDelete(self->m_toggleAction);
        self->m_toggleAction = NULL;
    }
    {
        /* 复扫 R-83 配套：自定义标题条为借用承载（宏开启=父类槽位，
         * 经 xdw_titleBar/XWidget_titleBarWidget 读）——析构级联删子前
         * 先摘除父链，控件归还调用方（面板不删除）；父类槽位清空由
         * 基类 VXWidget_deinit 收尾，行为与自有成员置空等价。 */
        XWidget* bar = xdw_titleBar(self);
        if (bar) {
            if (XWidget_parentWidget(bar) == (XWidget*)self)
                XWidget_setParent(bar, NULL, 0);
        }
    }
    if (self->m_host) {
        /* 析构时摘除宿主销毁监听（setHost 建立的回链）。 */
        XObject_disconnect_1((XObject*)self->m_host,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)self, xdw_hostDestroyedSlot);
    }
    self->m_host = NULL;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XDockWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDockWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_dockWidget_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_dockWidget_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VX_dockWidget_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VX_dockWidget_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VX_dockWidget_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_dockWidget_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VX_dockWidget_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, VX_dockWidget_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_CloseEvent, VX_dockWidget_closeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_dockWidget_deinit);
    return XVTABLE_DEFAULT;
}

void XDockWidget_init(XDockWidget* self, const char* utf8Title,
                      XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XDockWidget);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_title = XString_create_utf8(utf8Title ? utf8Title : "");
    self->m_features = (int)XDockWidgetFeature_Closable |
                       (int)XDockWidgetFeature_Movable |
                       (int)XDockWidgetFeature_Floatable;
    self->m_allowedAreas = (int)XDockWidgetArea_All;
    self->m_floating = false;
    /* 对标 Qt qwidget.cpp paintBackground 的 DrawAsRoot 分支（问题
     * #33 同款根因，XMainWindow_init/XDialog_init 同款置位先例）：
     * 面板脱靠成独立顶层窗口后，停靠态依赖的宿主主窗口背景填充
     * 不复存在，原生窗口 background_pixel=0（黑）直接露出——标题
     * 条以下全部区域黑底，深色内容文字画在黑底上不可见（实测浮出
     * 停靠面板内容区全黑）。置位后由默认绘制槽按 palette Window
     * 回填整片背景。 */
    XWidget_setAutoFillBackground(self, true);
}

XDockWidget* XDockWidget_create_ex(XMemoryType memory,
                                   const char* utf8Title,
                                   XWidget* parent, XWidgetFlags flags)
{
    XDockWidget* self =
        (XDockWidget*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XDockWidget_init(self, utf8Title, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 公共 API ==================== */

void XDockWidget_setWidget(XDockWidget* self, XWidget* widget)
{
    if (!self || self->m_widget == widget) return;
    if (self->m_widget) {
        /* 对标 Qt setWidget 的替换语义：已有内容时先摘除旧控件（转独立
         * 顶层即脱离本面板父链），所有权转移给调用方、由调用方决定释放
         * （Qt 中旧 widget 脱离 dock 后归调用方管理，dock 不再删除）。 */
        XWidget_setParent(self->m_widget, NULL, 0);
    }
    self->m_widget = widget;
    if (widget)
        XWidget_setParent(widget, (XWidget*)self, 0);
    /* 对标 Qt：新内容立即摆到标题条以下全部区域。 */
    xdw_layoutContent(self);
}

XWidget* XDockWidget_widget(const XDockWidget* self)
{
    return self ? self->m_widget : NULL;
}

void XDockWidget_setFeatures(XDockWidget* self, int features)
{
    int mask = (int)XDockWidgetFeature_FeatureMask;
    bool verticalChanged;
    if (!self) return;
    /* 对标 Qt setFeatures：先按 FeatureMask 裁剪无效高位。 */
    features &= mask;
    if (self->m_features == features) return;
    verticalChanged = ((self->m_features ^ features) &
                       (int)XDockWidgetFeature_VerticalTitleBar) != 0;
    self->m_features = features;
    /* 对标 Qt（qdockwidget.cpp:1413）：切换动作的可用意随 Closable 特性
     * 联动——面板不可关闭时「显示/隐藏」入口无意义（Qt 禁用该动作）。 */
    if (self->m_toggleAction)
        XAction_setEnabled(self->m_toggleAction,
                           (features & (int)XDockWidgetFeature_Closable) ==
                           (int)XDockWidgetFeature_Closable);
    xdw_emitInt(self, (size_t)XDockWidget_featuresChanged_signal,
                features);
    /* 对标 Qt（qdockwidget.cpp:1415 `update()`）：特性变化后重绘面板
     * （标题条按钮集随之变化）。 */
    XWidget_update((XWidget*)self);
    /* 竖排标题条特性变化会改标题条区几何（竖条 vs 横条），需宿主重排
     * （Qt 对标 layout->setVerticalTitleBar → 布局重解算）。 */
    if (verticalChanged && self->m_host)
        XMainWindow_updateDockLayout((XMainWindow*)self->m_host);
}

int XDockWidget_features(const XDockWidget* self)
{
    return self ? self->m_features : 0;
}

/**
 * @brief      setFloating 实现体（公共入口与拖出路径共用）。
 * @details    对标 Qt QDockWidgetPrivate::setFloating + setWindowState
 *             （qdockwidget.cpp:1447-1498 / 1183-1246），步骤与次序逐条
 *             对应：
 *             ① `if (!floating && parent) { ... dockWidgetArea==NoDockWidgetArea
 *                ⇒ return; }`（1188-1193，不可重停靠守卫）；
 *             ② `wasFloating` / `hidden` 快照（1196-1199）；
 *             ③ `if (q->isVisible()) q->hide();`（1200-1201）——先隐藏再
 *                改窗型，Qt 借此在旧父后备存储上作废旧矩形
 *                （QWidgetPrivate::hide_sys）；
 *             ④ `setWindowFlags(flags)`（1218）——`floating ? Qt::Tool
 *                : Qt::Widget`，非原生装饰再 `|= Qt::FramelessWindowHint`
 *                （1203-1215）；unplug 期间追加 X11BypassWindowManagerHint
 *                （1216-1221）；
 *             ⑤ `if (!rect.isNull()) q->setGeometry(rect);`（1223-1224）
 *                ——一次几何落位（不再拆 move+resize 两次几何事件）；
 *             ⑥ `updateButtons()`（1226）——按钮可见性随特性/装饰形态；
 *             ⑦ `if (!hidden) q->show();`（1228-1229）；
 *             ⑧ `topLevelChanged` + `dockLocationChanged`（1231-1242）；
 *             ⑨ `setResizerActive(!unplug && floating && !nativeDeco)`
 *                （1245）——浮动态边缘改尺寸由框架装饰层的 8 向带承担
 *                （XWindowDecoration，见 xdw_setFloatingImpl 的注释），
 *                此处只需保证浮窗窗型不带 Frameless 抑制（无原生边框时
 *                装饰层才会接管边缘带）。
 *             拖出路径（useUndockedGeometry=false，对标 startDrag→unplug，
 *             qdockwidget.cpp:1164-1170）用当前几何原地脱靠；公共入口
 *             （true）优先恢复 undockedGeometry（qdockwidget.cpp:1464-1468）。
 * @param      self 目标停靠面板；可为 NULL。
 * @param      floating true 转浮动，false 回归停靠。
 * @param      useUndockedGeometry true 恢复保存的浮动几何（有效时）。
 * @return     无返回值。
 */
static void xdw_setFloatingImpl(XDockWidget* self, bool floating,
                                bool useUndockedGeometry)
{
    XWidget* selfw;
    XWidget* host;
    XPoint origin;
    XPoint globalPos;
    int w;
    int h;
    bool hidden;
    if (!self) return;
    selfw = (XWidget*)self;
    host = self->m_host;
    /* ① 不可重停靠守卫（qdockwidget.cpp:1188-1193）：
     *    `if (!floating && parent) { ... dockWidgetArea(q) ==
     *     NoDockWidgetArea ⇒ return; // this dockwidget can't be redocked }`
     *    面板已登记宿主、登记区域为 NoDockWidgetArea、且**当前不是浮动
     *    态**时不得强行挂回（旧实现只判 host 非空，已登记但区域为 0 时
     *    仍 setParent 挂回宿主，与 Qt 拒绝重停靠语义相反）。
     *    `!m_floating` 是 Qt `parent` 判据的等价物：Qt 里浮动面板的
     *    parentWidget() 恒 NULL（顶层窗），故守卫对「浮动 → 回归停靠」这
     *    一跳天然不生效；C 的浮动位与父链是两个账本，必须显式排除浮动
     *    态，否则公共 setFloating(false) 与 XMainWindow_addDockWidget 的
     *    重停靠（面板刚从 removeDockWidget 摘除、区域查询归 0）都会被
     *    这条守卫误拦，面板永远停在浮动态。 */
    if (!floating && !self->m_floating && host &&
        XMainWindow_dockWidgetArea((const XMainWindow*)host, selfw) ==
            (int)XDockWidgetArea_NoDockWidgetArea)
        return;
    if (self->m_floating == floating) return;
    XDW_TRACE("setFloating %d (undocked=%d)", floating ? 1 : 0,
              useUndockedGeometry ? 1 : 0);
    /* 注意：不设「无宿主即早退」守卫——浮动态翻转与 topLevelChanged
     * 发射不依赖宿主登记（qdockwidget.cpp:1188-1193 的无可回归区守卫
     * 由上方分支承载）：无宿主面板 setFloating(false) 仍须复位
     * m_floating 并发射信号（回归契约，apitest D 段口径），提前返回会
     * 同时吞掉状态翻转与信号。 */
    /* ② 快照：当前几何/全局位置/可见性（qdockwidget.cpp:1196-1199）。
     * 必须在重设父对象前完成，子控件坐标经父链映射才有意义。
     * 落位值走 XWidget_mapToGlobal 的**设备全局**（浮动顶层几何账本
     * 域；与跟指方程的指针全局坐标同域——跨态一致才不跳位）。rot==0
     * 与旧的逻辑全局口径逐位一致；下方 undockedGeometry 恢复分支同域
     * （浮动态快照取自 XWidget_x/y 账本，resizeEvent 记录处）。 */
    XPoint_init(&origin, 0, 0);
    globalPos = XWidget_mapToGlobal(selfw, &origin);
    w = XWidget_width(selfw);
    h = XWidget_height(selfw);
    hidden = !XWidget_isVisible(selfw); /* qdockwidget.cpp:1199 的 `hidden` 快照。 */
    /* 对标 Qt setFloating 首行的 endDrag(Abort)（qdockwidget.cpp:1461）：
     * 状态切换必须终止进行中的拖动并解除本控件持有的全局鼠标抓取——
     * 否则双击切换（press 已抓取）后抓取泄漏，后续所有窗口的鼠标事件
     * 被重定向进本面板，表现为多次操作后整体交互失效。 */
    if (self->m_dragging) {
        self->m_dragging = false;
        XWidget_releaseMouse(selfw);
    }
    self->m_ctrlDrag = false;
    /* ③ 先隐藏（qdockwidget.cpp:1200-1201）：子控件形态的 hide 由
     *    XWidget_setVisible 的 hide 分支作废**父级**上该控件的原矩形
     *    （XWidget.c 的 hide_sys 等价物），旧位像素在改窗型/换父之前
     *    就进入宿主脏区。顶层形态的 hide 走 XWindow_setVisible 的
     *    fbdev 腾空区恢复（XWindow.c 的 hide 恢复链）。 */
    if (!hidden)
        XWidget_setVisible(selfw, false);
    self->m_floating = floating;
    if (floating) {
        /* 对标 Qt：setFloating(true) 脱离主窗布局，转成独立顶层窗口。
         * ④ 窗型（qdockwidget.cpp:1203-1222）：
         *    `Qt::WindowFlags flags = floating ? Qt::Tool : Qt::Widget;`
         *    nativeDeco 时补 CustomizeWindowHint|WindowTitleHint
         *    （+Closable 时 WindowCloseButtonHint），否则
         *    `flags |= Qt::FramelessWindowHint`；unplug 期间再追加
         *    X11BypassWindowManagerHint（1216-1221）。
         *    旧实现把「Tool+Frameless」降级成 Popup——Qt::Popup 是另一类
         *    窗口（无任务栏/自动关闭/override-redirect 语义），与本面板
         *    语义不符。几何来源对标 qdockwidget.cpp:1464-1468：
         *    undockedGeometry 有效（qdockwidget_p.h:88）则恢复上次浮动
         *    几何，否则保留当前尺寸并映射到原全局位置。 */
        bool nativeDeco;
        XWidgetFlags flags = (XWidgetFlags)XWindowType_Tool;
        if (useUndockedGeometry && self->m_undockedValid) {
            if (self->m_undockedGeometry.width > 0)
                w = self->m_undockedGeometry.width;
            if (self->m_undockedGeometry.height > 0)
                h = self->m_undockedGeometry.height;
            globalPos.x = self->m_undockedGeometry.x;
            globalPos.y = self->m_undockedGeometry.y;
        }
        nativeDeco = xdw_nativeDeco(self);
        if (nativeDeco) {
            /* qdockwidget.cpp:1206-1210：原生装饰只保留标题 + 关闭钮。 */
            flags |= (XWidgetFlags)(XWindowType_CustomizeWindowHint |
                                    XWindowType_WindowTitleHint);
            if (self->m_features & XDockWidgetFeature_Closable)
                flags |= (XWidgetFlags)XWindowType_WindowCloseButtonHint;
        } else {
            /* qdockwidget.cpp:1212-1214：无原生装饰 ⇒ 无边框位，标题由
             * 控件自绘（Qt::Tool|FramelessWindowHint 的同构映射）。 */
            flags |= (XWidgetFlags)XWindowType_FramelessWindowHint;
        }
        /* qdockwidget.cpp:1216-1221：拖出（unplug）期间追加
         * X11BypassWindowManagerHint，拖动结束（endDrag 落位成功或保持
         * 浮动收口）再清除（见 xdw_endDragKeepFloating）。 */
        if (!useUndockedGeometry)
            flags |= (XWidgetFlags)XWindowType_BypassWindowManagerHint;
        /* setParent(NULL, flags) 同时完成「脱离宿主」与「落窗型」：空父
         * 即顶层（XWidget_setParent 的 m_isWindow 规则），flags 原样写入
         * 控件侧并同步到桥接窗。 */
        XWidget_setParent(selfw, NULL, flags);
        /* 标题同值跳过（对标 Qt QWidget::setWindowTitle 对同值标题的
         * 幂等语义）：反复浮/停切换传同一标题时不再重建平台窗口标题
         * 字符串——旧实现每轮无条件重设，驱动 XWidget 侧堆字符串整建
         * 整毁，拖浮循环内存统计近似线性增长（每轮 ~96B 计量漂移）
         * 的直接来源；标题变化路径行为不变。 */
        {
            const XString* cur = XWidget_windowTitle(selfw);
            if (!cur ||
                !XString_equals(cur, self->m_title, XChar_CaseSensitive))
                XWidget_setWindowTitle(selfw, self->m_title);
        }
        if (w <= 0) w = 200; /* 无宿主几何时的兜底尺寸 */
        if (h <= 0) h = 150;
        /* ⑤ 一次几何落位（qdockwidget.cpp:1223-1224 的
         *    `q->setGeometry(rect)`）：不再拆 move+resize——两次几何事件
         *    会让浮动态 ResizeEvent 与 MoveEvent 各跑一遍，且中间态几何
         *    会被 undockedGeometry 快照误记。 */
        XWidget_setGeometry(selfw, globalPos.x, globalPos.y, w, h);
        /* 【同步补内容布局 2026-11-04】面板尺寸刚变（浮动态尺寸），但
         *   RESIZE 事件是**异步投递**的：本函数末尾的宿主当帧重绘
         *   （xdw_syncRepaintHostArea）先于 RESIZE 派发 ⇒ 当帧绘出的仍
         *   是旧尺寸的内容控件（真机：浮窗内容按停靠态窄宽画在新位）。
         *   此处同步补一次标题条/内容布局，让当帧重绘拿到新尺寸。 */
        xdw_layoutTitleBar(self);
        xdw_layoutContent(self);
        /* ⑦ `if (!hidden) q->show();`（qdockwidget.cpp:1228-1229）：
         *    浮动态还需提升/激活（Qt::Tool 浮窗的默认前台语义）。 */
        if (!hidden) {
            XWidget_show(selfw);
            XWidget_raise(selfw);
            XWidget_activateWindow(selfw); /* 对标 Qt：浮动窗获得焦点 */
        }
        /* 宿主侧旧位（面板在宿主内的停靠矩形）随浮出腾空：子控件已
         * 脱离宿主父链，宿主后备缓冲里该区仍是面板旧像素——按合成器
         * 语义当帧重绘（见 xdw_syncRepaintHostArea）。 */
        xdw_syncRepaintHostArea(self);
    } else {
        /* 对标 Qt：setFloating(false) 回归停靠区，重新挂回宿主主窗口，
         * 几何交还主窗口停靠布局（随后统一重排）。无宿主登记（从未
         * addDockWidget）时跳过挂回——浮动翻转与信号已在下方无条件
         * 承载（见函数首注）；此时不得走 setParent(selfw, NULL, 0)，
         * 否则面板被错误转成普通顶层窗口、浮/停语义失真。 */
        if (host) {
            /* ④ 回归停靠的窗型是 Qt::Widget（qdockwidget.cpp:1203），即
             *    普通子控件（无窗口标志位）。 */
            XWidget_setParent(selfw, host, (XWidgetFlags)XWindowType_Widget);
            if (!hidden) XWidget_show(selfw);
        }
    }
    if (host)
        XMainWindow_updateDockLayout((XMainWindow*)host);
    /* 【同步补内容布局 2026-11-04】回归停靠：宿主重排把面板摆到新的
     *   停靠矩形（尺寸随之变），而承载内容布局的 RESIZE 事件**异步
     *   投递**——本函数末尾的宿主当帧重绘先于 RESIZE 派发 ⇒ 当帧绘出
     *   的内容控件仍是**浮动态尺寸**（真机：回停靠后内容按 160x349
     *   画在宿主左上，盖住左面板标题条与中央区左缘；随后几何修正的
     *   脏区又落在已派发 PAINT 的快照之外 ⇒ 终态像素永不落屏）。
     *   此处同步补一次布局，使当帧重绘即拿到停靠态尺寸。 */
    xdw_layoutTitleBar(self);
    xdw_layoutContent(self);
    /* 落位后同步重绘宿主（合成器语义的当帧收口，见
     * xdw_syncRepaintHostArea 注）。浮/停两向都做：浮出时清掉宿主内的
     * 面板旧像素，回归时把新停靠矩形画出来（该矩形由 xmw_layout 的
     * setGeometry 异步标脏，而本次 PAINT 事件在投递时刻已把当时的
     * m_dirty 深拷贝成快照，随后 xmw_layout 里其余面板的几何变更落在
     * 快照之外，只能等下一拍——实测：面板已回停靠位，屏上仍是宿主
     * 底色）。 */
    if (host)
        xdw_syncRepaintHostArea(self);
    XDW_TRACE("setFloating done -> %d", self->m_floating ? 1 : 0);
    /* ⑧ `topLevelChanged` + `dockLocationChanged`（qdockwidget.cpp:1231-1242）：
     *    两者都在 show 之后、setResizerActive 之前发射。 */
    xdw_emitBool(self, (size_t)XDockWidget_topLevelChanged_signal,
                 floating);
    /* 对标 Qt：floating 与 dockLocationChanged 在同一切换点成对发射
     * （qdockwidget.cpp:1234-1242）——转浮动携带 NoDockWidgetArea，
     * 回归停靠携带宿主登记区域。 */
    if (floating) {
        xdw_emitInt(self, (size_t)XDockWidget_dockLocationChanged_signal,
                    (int)XDockWidgetArea_NoDockWidgetArea);
    } else {
        xdw_emitInt(self, (size_t)XDockWidget_dockLocationChanged_signal,
                    XMainWindow_dockWidgetArea(
                        (const XMainWindow*)host, selfw));
    }
}

void XDockWidget_setFloating(XDockWidget* self, bool floating)
{
    /* 公共入口：恢复语义对标 Qt 公共 setFloating（qdockwidget.cpp:
     * 1464-1468 用 undockedGeometry）；拖出路径走实现体 false 分支。 */
    xdw_setFloatingImpl(self, floating, true);
}

bool XDockWidget_isFloating(const XDockWidget* self)
{
    return self ? self->m_floating : false;
}

void XDockWidget_setAllowedAreas(XDockWidget* self, int areas)
{
    if (!self) return;
    /* 对标 Qt（qdockwidget.cpp:1518 `areas &= Qt::DockWidgetArea_Mask`）：
     * 只保留有效停靠区位（QDockWidgetArea 掩码 = 0xF，与本框架
     * XDockWidgetArea_All 同值），高位垃圾不进入记账。 */
    areas &= (int)XDockWidgetArea_All;
    if (self->m_allowedAreas == areas) return;
    self->m_allowedAreas = areas;
    xdw_emitInt(self, (size_t)XDockWidget_allowedAreasChanged_signal,
                areas);
}

int XDockWidget_allowedAreas(const XDockWidget* self)
{
    return self ? self->m_allowedAreas : 0;
}

bool XDockWidget_isAreaAllowed(const XDockWidget* self, int area)
{
    return self ? (self->m_allowedAreas & area) != 0 : false;
}

#if XGUI_CUSTOM_TITLEBAR_ON
void XDockWidget_setTitleBarWidget(XDockWidget* self, XWidget* widget)
{
    if (!self) return;
    /* 挂载/解挂/借用契约统一交父类控件级通用槽实现（同指针幂等短路、
     * 旧条隐藏解挂归还创建方不释放、新条挂为本面板子控件并显式 show、
     * update + 顶层装饰同步）；查询 API 无额外语义，头文件宏映射父类
     * XWidget_titleBarWidget 纯读槽。本面板内部消费点（布局钉位/绘制
     * 分流/输入门禁/析构解挂）统一经 xdw_titleBar 读父类槽位。 */
    XWidget_setTitleBarWidget((XWidget*)self, widget);
    /* 评审返修补回挂载即钉位（原自有 setter 语义）：父类实现只挂载不
     * 摆位（摆位归消费方），若不在挂载点立即铺满标题条区，已定型尺寸
     * 且可见的活面板换挂/新挂 0 尺寸条时，条保持 0/旧几何直至面板下一
     * 次 resizeEvent。钳宽=面板宽、高钳入 [1,XDW_TITLE_H]；条为空自短路
     * （xdw_layoutTitleBar 内判空）。 */
    xdw_layoutTitleBar(self);
}
#else
void XDockWidget_setTitleBarWidget(XDockWidget* self, XWidget* widget)
{
    XWidget* old;
    if (!self || self->m_titleBar == widget) return;
    old = self->m_titleBar;
    if (old) {
        /* 借用承载（头文件契约 m_titleBar 为借用）：旧自定义标题条
         * 不删除，若曾挂到本面板则摘父链归还调用方（同 setWidget 的
         * 摘除语义）。 */
        XWidget_setVisible(old, false);
        if (XWidget_parentWidget(old) == (XWidget*)self)
            XWidget_setParent(old, NULL, 0);
    }
    self->m_titleBar = widget;
    if (widget) {
        /* 复扫 R-83：此前纯存储——自定义标题条不 reparent/不入标题条
         * 区布局/paint 不消费，永不可见。现对标 Qt QDockWidget::
         * setTitleBarWidget 的接管呈现：挂为本面板子控件、铺满标题条
         * 区、显式 show（框架显式 show 语义）；绘制分流与标题区鼠标
         * 门禁见 paintEvent/mousePressEvent。传 NULL 恢复默认标题条。 */
        XWidget_setParent(widget, (XWidget*)self, 0);
        xdw_layoutTitleBar(self);
        XWidget_show(widget);
    }
    XWidget_update((XWidget*)self);
}

XWidget* XDockWidget_titleBarWidget(const XDockWidget* self)
{
    return self ? self->m_titleBar : NULL;
}
#endif /* XGUI_CUSTOM_TITLEBAR_ON */

/** @brief toggleViewAction 槽：翻转面板可见性（对标 QDockWidget
 *         toggleViewAction 的 triggered→setVisible 桥接）。 */
static void xdw_toggleViewSlot(XObject* receiver, XVarList* args)
{
    XDockWidget* dock = (XDockWidget*)receiver;
    (void)args;
    if (!dock) return;
    XWidget_setVisible((XWidget*)dock, !XWidget_isVisible((XWidget*)dock));
}

XAction* XDockWidget_toggleViewAction(XDockWidget* self)
{
#if XACTION_ON
    XAction* action;
    if (!self) return NULL;
    if (self->m_toggleAction) return self->m_toggleAction;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    self->m_toggleAction = action;
    XAction_setCheckable(action, true); /* 对标 Qt：切换动作可选中 */
    XAction_setText_2(action,
                      self->m_title ? XString_toUtf8(self->m_title) : "");
    XAction_setChecked(action, XWidget_isVisible((XWidget*)self));
    XObject_connect_1((XObject*)action, XSignal(XAction_triggered_signal),
                      (XObject*)self, xdw_toggleViewSlot,
                      XConnectionType_Direct);
    return self->m_toggleAction;
#else
    /* XACTION_ON 裁剪时保留遗留回退（无动作子系统）。 */
    (void)self;
    return NULL;
#endif /* XACTION_ON */
}

/* ==================== 保护接口（XDockWidget_Protected.h） ==================== */

/** @brief 宿主销毁槽：宿主主窗口释放时摘除回链（对标 Qt 的 QPointer
 *         防悬空语义；浮动面板已脱离宿主控件树，必须显式摘链）。 */
static void xdw_hostDestroyedSlot(XObject* receiver, XVarList* args)
{
    XDockWidget* dock = (XDockWidget*)receiver;
    (void)args;
    if (dock) dock->m_host = NULL;
}

void XDockWidget_setHost(XDockWidget* self, XWidget* host)
{
    if (!self) return;
    if (self->m_host) {
        XObject_disconnect_1((XObject*)self->m_host,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)self, xdw_hostDestroyedSlot);
    }
    self->m_host = host;
    if (host) {
        XObject_connect_1((XObject*)host,
                          XSignal(XObject_destroyed_signal),
                          (XObject*)self, xdw_hostDestroyedSlot,
                          XConnectionType_Direct);
    }
}

XWidget* XDockWidget_host(const XDockWidget* self)
{
    return self ? self->m_host : NULL;
}

/* ==================== 信号 ==================== */

void* XDockWidget_featuresChanged_signal(XDockWidget* self, int features)
{
    (void)self; (void)features;
    return (void*)(size_t)XDockWidget_featuresChanged_signal;
}

void* XDockWidget_topLevelChanged_signal(XDockWidget* self, bool topLevel)
{
    (void)self; (void)topLevel;
    return (void*)(size_t)XDockWidget_topLevelChanged_signal;
}

void* XDockWidget_allowedAreasChanged_signal(XDockWidget* self, int areas)
{
    (void)self; (void)areas;
    return (void*)(size_t)XDockWidget_allowedAreasChanged_signal;
}

void* XDockWidget_visibilityChanged_signal(XDockWidget* self, bool visible)
{
    (void)self; (void)visible;
    return (void*)(size_t)XDockWidget_visibilityChanged_signal;
}
void* XDockWidget_dockLocationChanged_signal(XDockWidget* self, int area)
{
    (void)self; (void)area;
    return (void*)(size_t)XDockWidget_dockLocationChanged_signal;
}














#endif /* XWIDGET_ON && XDOCKWIDGET_ON */

#endif /* XGUI_ON */
