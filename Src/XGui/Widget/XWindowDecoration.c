/**
 * @file       XWindowDecoration.c
 * @brief      框架级窗口装饰实现（判定/抑制/承载/输入拦截层，与桌面
 *             WM 标题栏同语义）。
 * @details    与同名头文件的公共 API 一一对应。条带绘制已迁出为树内
 *             标题条控件 XTitleBar 的 paintEvent 自绘（原 CC_TitleBar
 *             样式绘制实现单一事实源迁入该控件，本文件旧 xwd_ 绘制
 *             符号同步删除）；本模块保留归属判定（平台策略抽象）、
 *             CSD 抑制位兜底、标题条控件承载与输入拦截。输入拦截先
 *             于控件命中（XWidget_dispatchPointerEvent 头部调用），
 *             几何操作只走 XWidget/XWindow 公共 API，无平台 API 残留。
 * @author     XinYueC 团队
 */

#include "XWindowDecoration.h"
#include "XMemory.h"
#include "XObject.h"
#include "XSystem.h"
#include "XEvent.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XScreen.h"
#include "XPlatformBackingStore.h"
#include "XImage.h" /* 拖动快照离屏图像（窗口本地全幅；XImage_reinit_ex 复用分配） */
#include "XWindow_Protected.h"
#include "XCursor.h" /* 边缘改尺寸悬停光标：标准形状经 XWidget_setCursor 生效链 */
#include "XPlatformTheme.h"
#include "XPlatformNativeWindow.h" /* 拖拽平台指针抓取：指针出窗仍持续投递 MOVE（软件重路由的出窗盲区兜底） */
#include "XDateTime.h" /* 交互限帧计时：单调毫秒（时间源约束同 XWidget.c present 限频）；[gesture] 分相探针同源 */
#include "XTitleBar.h"
#include "XTitleBar_Protected.h"
#include "XGuiConfig.h" /* XGUIAPPLICATION_ON 开关必须先于 XGuiApplication.h 生效——否则其声明块被整体裁掉、函数按隐式声明返回 int（64 位指针截断，2026-10-04 实证；注意不可放入 FBDEV 条件块，X11 侧同样要可见） */
#include "XGuiApplication.h" /* clearOutsideWindow 扣除其他可见顶层：Z 序枚举 */
#include "XApplication.h" /* restoreExposeStrips 窗口→顶层控件反查（XApplication_topLevelWidgets） */
#include "XBackingStore.h" /* restoreExposeStrips：XBackingStore_handle 取平台后备存储 */
#include "XWindowSystemInterface.h" /* 被占顶层暴露重绘注入 */
#include <stdio.h> /* [gesture] 分相探针 stderr 报告（探针关零输出） */
#include <stdlib.h> /* XGUI_DRAG_SNAPSHOT_BLIT / XGUI_GESTURE_PROF env 门控 */
#if XGUI_ON && XPLATFORM_FBDEV_ON
#include "XPlatformDisplayDriver.h"
#endif
#if XMENU_ON
#include "XMenu.h"
#include "XAction.h"
#endif

#if XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON

/* ==================== 常量与状态 ==================== */

/** @brief 边缘改尺寸命中带宽度（W/E/S，像素）。
 *  电阻屏手指精度 ±10px 级，窄带真机实测多次才触发（昆仑通态
 *  2026-09-28 教训：6px→24px）；带与内容控件重叠按宿主双口径分流：
 *  有原生窗（桌面 WM）边缘带优先于内容子控件（对标 WM 改尺寸框帧外
 *  恒胜内容命中）；无原生窗（fbdev 直写触屏）保持子控件优先（带侵入
 *  内容区，内容可点优先）。
 *  DPI 接线：经 XStyle_dpiScaled 按屏幕逻辑 DPI 缩放（守卫下 dpr=1
 *  逐位保持 24/8 昆仑通态调校；安卓经 present 物理只会更大不会更小，
 *  dpr>1 平台自动跟随 scaleDpi）。option=NULL 恒取主屏。 */
#define XWD_RESIZE_ZONE XStyle_dpiScaled(24, NULL)
/** @brief 顶边改尺寸命中带宽度（窄带：标题栏大部分区域归拖拽移动）。 */
#define XWD_RESIZE_ZONE_N XStyle_dpiScaled(8, NULL)

/** @brief 无窗口管理器环境的「桌面底色」（面板原生像素）。
 *  框架即合成器：窗口外的面板区域没有 WM 铺桌面，恒为黑会被用户
 *  视作「显示不全的黑块」（昆仑通态真机 2026-09-28 用户指正）——统一
 *  填与标题栏同源的浅灰桌面底色。值为 RGB565(239,239,239)=0xEF7D（绿 6 位域 59=239；0xEBBD 曾误编码绿 30→粉紫底，真机用户指正）
 *  （本面板固定 16 位格式）；非 565 面板上呈近似深色，仅观感差异。 */
#define XWD_DESKTOP_PIXEL 0xEF7Du

/** @brief 改尺寸方向位（8 向掩码的 N/S/E/W 组合）。 */
enum {
    XWD_RZ_N = 1,
    XWD_RZ_S = 2,
    XWD_RZ_E = 4,
    XWD_RZ_W = 8
};

/**
 * @brief 单个被装饰顶层的交互状态。
 * @details 登记于静态注册表（键 = 顶层控件指针，窗口指针随查随新）；
 *          顶层控件销毁时经 XGuiApplication_removeWindow 钩子摘除。
 */
typedef struct XWindowDecorationState
{
    XWidget* m_top;        /**< 顶层控件（借用；注册表键）。 */
    XWindow* m_window;     /**< 桥接窗口（借用；ensureState 时刷新）。 */
    XWidget* m_bar;        /**< 当前生效的标题条控件（借用；自定义条优
                                先，否则为默认 XTitleBar 实例）。 */
    XWidget* m_defaultBar; /**< 本模块创建的默认 XTitleBar 实例（堆对象；
                                父=顶层；装饰失活/宿主注销时经
                                XObject_deleteLater 异步释放，见
                                xwd_dropDefaultBar，期间指针即清空）。 */
    int m_armed;           /**< 按住中的按钮子控件位（XStyleSC_TitleBar*；0 无）。 */
    int m_hot;             /**< 悬停中的按钮子控件位（0 无）。 */
    bool m_dragging;       /**< 标题栏拖拽移动进行中。 */
    XPoint m_dragLast;     /**< 拖拽上一采样点（全局坐标；窗口自身移动
                                不改变全局系，增量才不自指）。 */
    bool m_resizing;       /**< 边缘改尺寸进行中。 */
    bool m_shaded;         /**< 最小化=卷起态（窗口只剩标题条）。 */
    XRect m_preShadeGeometry; /**< 卷起前的完整几何（展开恢复）。 */
    int m_resizeMask;      /**< 改尺寸方向位掩码（XWD_RZ_* 组合）。 */
    XPoint m_resizeAnchor; /**< 按下点（全局坐标；改尺寸增量=距按下点
                                总位移）。 */
    XRect m_resizeGeometry;/**< 按下时窗口几何（改尺寸基准）。 */
    int64_t m_lastGestureStepMs; /**< 交互限帧节流戳（XDateTime 单调毫秒；
                                0=本手势首步恒放行）。拖拽移动与改尺寸
                                互斥，两路限帧闸（XGUI_PRESENT_MAX_FPS /
                                XGUI_RESIZE_REPAINT_MAX_FPS）共用本戳：
                                置手势时清零防跨手势首帧被旧戳误跳；
                                ensureState 的 XMemset 已含零初始化。 */
    int m_edgeCursor;      /**< 装饰侧边缘光标当前形状键（XCursorShape+1；
                                0=未接管，与 Arrow(0) 区分；ensureState 的
                                XMemset 清零即未接管）。 */
    XImage m_dragSnap;     /**< 拖动快照图像（窗口本地全幅；拖动开始时自
                                后备缓冲已合成内容整幅拷入，每步整块 blit
                                进 fb 可见面，拖动结束立即释放。快照模式
                                未启用时保持未初始化空壳）。 */
    bool m_dragSnapActive; /**< 拖动快照 blit 模式进行中（快照已取且平台
                                原语可用；false=本手势走既有 flush 路径）。 */
} XWindowDecorationState;

/** @brief 装饰状态注册表（懒扩容；容量以 2 的幂增长）。 */
static XWindowDecorationState* g_xwdStates = NULL;
static int g_xwdCount = 0;
static int g_xwdCapacity = 0;

/** @brief 进程内快照 blit 拖动步累计（诊断/离屏测试自证快照路径真实
 *  生效用；恒单调，成功直写一步加一，降级步不计）。定义前移至文件头：
 *  XWindowDecoration_dragSnapshotStepCount 访问器位于手势态查询区。 */
static int g_xwdDragSnapStepCount = 0;

/* ==================== 内部辅助：注册表 ==================== */

/** @brief 按顶层控件查状态；无登记返回 NULL。 */
static XWindowDecorationState* xwd_stateFor(const XWidget* top)
{
    int i;
    for (i = 0; i < g_xwdCount; ++i) {
        if (g_xwdStates[i].m_top == top) return &g_xwdStates[i];
    }
    return NULL;
}

/** @brief 按桥接窗口反查顶层控件；无登记返回 NULL。 */
static XWidget* xwd_topForWindow(const XWindow* win)
{
    int i;
    if (!win) return NULL;
    for (i = 0; i < g_xwdCount; ++i) {
        if (g_xwdStates[i].m_window == win) return g_xwdStates[i].m_top;
    }
    return NULL;
}

/** @brief 查状态，缺失时登记新项；分配失败返回 NULL。 */
static XWindowDecorationState* xwd_ensureState(XWidget* top)
{
    XWindowDecorationState* st = xwd_stateFor(top);
    if (st) {
        st->m_window = top->m_windowHandle; /* 桥接窗口可能重建。 */
        return st;
    }
    if (g_xwdCount >= g_xwdCapacity) {
        int cap = g_xwdCapacity > 0 ? g_xwdCapacity * 2 : 8;
        XWindowDecorationState* grown = (XWindowDecorationState*)
            XMemory_realloc(g_xwdStates, (size_t)cap * sizeof(*grown),
                            XCLASS_DEFAULT_MEMORY_TYPE);
        if (!grown) return NULL;
        g_xwdStates = grown;
        g_xwdCapacity = cap;
    }
    st = &g_xwdStates[g_xwdCount++];
    XMemset(st, 0, sizeof(*st));
    st->m_top = top;
    st->m_window = top->m_windowHandle;
    return st;
}

/* ==================== 手势态查询 ==================== */

bool XWindowDecoration_gestureActive(void)
{
    int i;
    for (i = 0; i < g_xwdCount; ++i) {
        if (g_xwdStates[i].m_dragging || g_xwdStates[i].m_resizing)
            return true;
    }
    return false;
}
/** @brief      查询顶层窗口是否处于最小化卷起态（Shade：只剩标题条）。
 *  @details    [2026-10-06] 供宿主悬浮件（如性能悬浮窗）在卷起时随内容
 *              隐藏——卷起后窗口只剩条高，右下角重锚被钳到 y=0 的悬浮件
 *              会盖住标题条（真机用户实测）。未装饰/无状态/桌面原生窗
 *              (iconify 语义, 不做卷起)一律返回 false。
 * @param      top 顶层控件；可为 NULL。
 * @return     卷起态返回 true。
 */
bool XWindowDecoration_isShaded(const XWidget* top)
{
    const XWindowDecorationState* st = xwd_stateFor(top);
    return st ? st->m_shaded : false;
}

int XWindowDecoration_dragSnapshotStepCount(void)
{
    return g_xwdDragSnapStepCount;
}

/* ==================== 内部辅助：判定与度量 ==================== */

/** @brief 条带高度：标题条控件实际高度优先（自定义条可高/矮于默认），
 *  无条或条未定尺寸时回退 XTitleBar_defaultHeight（原 xwd_titleBarHeight
 *  的样式 PM 度量与最小钳制一并迁入该控件）。 */
static int xwd_stripHeight(const XWindowDecorationState* st)
{
    int h;
    if (st && st->m_bar) {
        h = XWidget_height(st->m_bar);
        if (h > 0) return h;
    }
    return XTitleBar_defaultHeight();
}

/** @brief 把标题条控件钉到宿主条区几何 (0,0,宿主宽,条高)。
 *  宽度随宿主 resize 重钉的挂点核实（resize 派发链）：本框架的 RESIZE
 *  事件只派发给顶层控件自身——控件侧 XWidget_setGeometry →
 *  XWidget_recomputeGeometry（XWidget.c sendEvent(RESIZE)）、平台侧
 *  XWidgetWindow_event → XWidget_applyWindowGeometry（同样 sendEvent
 *  到顶层），两路同经 VXWidget_event 的 RESIZE 唯一漏斗，主挂点=漏斗
 *  尾的 XWindowDecoration_syncBarGeometry（槽派发后调用），任何来源
 *  （WM 拖边框/xdotool/程序化/装饰自身）同帧重钉；装饰驱动的几何变化
 *  （拖拽/改尺寸/最大化切换/卷起）在 setGeometry 之后仍立即自重钉——
 *  漏斗重钉先于此发生，同值幂等，保底不依赖外部挂点（昆仑通态定版
 *  主路径同帧正确）；handlePointer 每次事件先经 xwd_ensureBar 兜底
 *  重钉（同值幂等，无变化零开销）。
 *  高度取条控件当前高度（用户自定条可显式定高），未定尺寸（新建控件
 *  0 高）回退 XTitleBar_defaultHeight。 */
static void xwd_pinBarGeometry(XWindowDecorationState* st)
{
    XWidget* top = st->m_top;
    XWidget* bar = st->m_bar;
    int w;
    int h;
    if (!top || !bar) return;
    w = XWidget_width(top);
    h = XWidget_height(bar);
    if (h <= 0) h = XTitleBar_defaultHeight();
    XWidget_setGeometry(bar, 0, 0, w, h);
}

/** @brief 确保被装饰顶层挂有生效标题条控件（syncWindow 与 handlePointer
 *  的 ensureState 后调用）。
 *  承载优先级：用户经 XWidget_setTitleBarWidget 挂到顶层的自定义条
 *  （借用槽，摆位归本模块）优先；否则用/建本模块的默认 XTitleBar 实例
 *  （父=顶层，本状态只存借用指针）。槽位不限定类型：非 XTitleBar 派生
 *  的自定义条照常接管摆位与纯子控件放行，但类型化操作（命中/状态注
 *  入/选项组装）经 xwd_typedBar（XTitleBar_isBar 动态校验）降级为空白
 *  条口径，杜绝未知布局控件按 XTitleBar 强转的越界读写。装饰保持激活
 *  期间，自定义条生效时隐藏默认条防双条叠画、摘除自定义条后恢复默认
 *  条显形（两者都不销毁，避免活跃期内反复建条）；默认条的销毁只发生
 *  在装饰失活与宿主注销两个时点，一律经 XObject_deleteLater 异步释放
 *  （见 syncWindow 与 xwd_dropDefaultBar），本函数在此前指针均已清空，
 *  重激活即重建新条。 */
static void xwd_ensureBar(XWindowDecorationState* st)
{
    XWidget* custom = NULL;
    if (!st || !st->m_top || !XWindowDecoration_activeFor(st->m_top)) return;
#if XGUI_CUSTOM_TITLEBAR_ON
    custom = XWidget_titleBarWidget(st->m_top);
#endif
    if (custom) {
        /* 自定义条接管：默认条让位隐藏（未创建过则无副作用）。 */
        if (st->m_defaultBar)
            XWidget_setVisible(st->m_defaultBar, false);
        st->m_bar = custom;
    } else {
        if (!st->m_defaultBar) {
            /* flags 传 0：标题栏恒为子控件，不得作为顶层窗口。 */
            st->m_defaultBar =
                (XWidget*)XTitleBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                              st->m_top, 0);
            if (st->m_defaultBar)
                XWidget_show(st->m_defaultBar);
        } else if (XWidget_testAttribute(st->m_defaultBar,
                                         XWidgetAttribute_WState_Hidden)) {
            /* 自定义条摘除后恢复默认条显形。 */
            XWidget_setVisible(st->m_defaultBar, true);
        }
        st->m_bar = st->m_defaultBar;
    }
    xwd_pinBarGeometry(st);
}

/** @brief 装饰失活/宿主注销时释放本模块创建的默认条（自定义条为借用，
 *  调用方只清指针，不经此处）。
 *  释放约定：XObject 派生控件一律 XObject_deleteLater 异步释放（禁止
 *  同步 XClassDelete 直调——条控件可能仍被事件派发/绘制队列
 *  引用，同步删会造成悬垂回调）。先隐藏防异步窗口期残像，再置异步删；
 *  XObject 析构会自行从父 children 摘链并撤销未派发的挂起事件
 *  （XObject.c VXObject_deinit），宿主先于延迟删被销毁亦无双重释放。
 *  生命周期随装饰：本状态指针立即清空，重激活由 xwd_ensureBar 重建。 */
static void xwd_dropDefaultBar(XWindowDecorationState* st)
{
    if (!st || !st->m_defaultBar) return;
    XWidget_setVisible(st->m_defaultBar, false);
    XObject_deleteLater((XObject*)st->m_defaultBar);
    st->m_defaultBar = NULL;
}

/** @brief 无窗口管理器环境的面板矩形（fbdev 显示驱动探测；屏幕兜底）。 */
static bool xwd_panelRect(const XWindowDecorationState* st, XRect* out)
{
    XRect g;
#if XGUI_ON && XPLATFORM_FBDEV_ON
    const XPlatformDisplayDriverOps* ops = XPlatformDisplayDriver_active();
    XPlatformDisplayInfo info;
    if (ops && ops->probe && ops->probe(&info) &&
        info.m_width > 0 && info.m_height > 0) {
        XRect_init(out, 0, 0, info.m_width, info.m_height);
        return true;
    }
#endif /* XGUI_ON && XPLATFORM_FBDEV_ON */
#if XSCREEN_ON
    {
        XScreen* screen = st ? XWindow_screen(st->m_window) : NULL;
        if (screen) {
            g = XScreen_geometry(screen);
            if (g.width > 0 && g.height > 0) {
                *out = g;
                return true;
            }
        }
    }
#else
    (void)st;
    (void)g;
#endif
    return false;
}

/** @brief 取当前生效条控件的 XTitleBar 类型化指针（动态类型守卫）。
 *  自定义条经 XWidget_setTitleBarWidget 的控件级通用槽挂载，槽位不限
 *  定类型（XDockWidget 等消费方接受任意控件）；仅装饰路径的类型化操作
 *  （命中测试虚槽 EXTitleBar_HitTest、活动子控件注入、CC_TitleBar 选
 *  项组装访问本类布局）要求条派生自 XTitleBar——否则虚槽越界读/结构
 *  体字段越界写（评审返修：挂载未校验的强转即崩溃根因）。非派生条在
 *  此降级：钉位摆位、树内自绘与子控件放行照常（纯 XWidget 级操作），
 *  类型化操作返回 NULL 短路（条内空白=无装饰按钮，等同空白拖拽面）。
 *  @param st 装饰状态借用指针；可为 NULL。
 *  @return 条控件确为 XTitleBar 或其派生时返回类型化借用指针；否则
 *          NULL（st/条为空或 XTitleBar_isBar 校验不过）。 */
static XTitleBar* xwd_typedBar(const XWindowDecorationState* st)
{
    if (!st || !st->m_bar) return NULL;
    if (!XTitleBar_isBar(st->m_bar)) return NULL;
    return (XTitleBar*)st->m_bar;
}

/** @brief 标题栏命中测试：转派条控件虚槽 XTitleBar_hitTest_base（样式
 *  hitTestComplexControl 的默认实现随绘制一并迁入条控件；条内空白=
 *  SC_TitleBarLabel/None，按钮返回对应子控件位）。坐标沿用顶层本地系
 *  ——条控件钉位在 (0,0)，两系数值相同（hitTest_base 契约即按条控件
 *  局部坐标取点）。经 xwd_typedBar 校验：非 XTitleBar 派生条恒返回 0
 *  （不得对未知虚表按 EXTitleBar_HitTest 槽位裸分派）。 */
static int xwd_hitTest(XWindowDecorationState* st, const XPoint* pos)
{
    XTitleBar* bar = xwd_typedBar(st);
    if (!bar) return 0;
    return XTitleBar_hitTest_base(bar, pos);
}

/** @brief 判断按钮子控件位（SysMenu/Min/Max/Normal/Close）。 */
static bool xwd_isButtonSc(int sc)
{
    return sc == XStyleSC_TitleBarSysMenu ||
           sc == XStyleSC_TitleBarMinButton ||
           sc == XStyleSC_TitleBarMaxButton ||
           sc == XStyleSC_TitleBarNormalButton ||
           sc == XStyleSC_TitleBarCloseButton;
}

/** @brief 判断命中控件是否为生效标题条的子控件（输入三分流的放行对象：
 *  树派发给用户按钮）。条控件自身与其祖先不算（空白区归装饰接管）。 */
static bool xwd_isBarChild(const XWindowDecorationState* st, XWidget* hit)
{
    XWidget* w;
    if (!st || !st->m_bar || !hit) return false;
    for (w = XWidget_parentWidget(hit); w; w = XWidget_parentWidget(w)) {
        if (w == st->m_bar) return true;
        if (w == st->m_top) break;
    }
    return false;
}

/** @brief 命中窗口边缘改尺寸带，返回方向位掩码（0=非边缘）。
 *  最大化/全屏禁用；W/E/S 宽带、顶边窄带（N 向只认条行内窄带）。 */
static int xwd_resizeZoneAt(XWidget* top, const XPoint* pos, int barH)
{
    int w;
    int h;
    int zone = 0;
    XWindow* win = top->m_windowHandle;
    if (!win) return 0;
    {
        XWindowState s = XWindow_windowState(win);
        if (s == XWindowState_Maximized || s == XWindowState_FullScreen)
            return 0;
    }
    /* 桌面（WM 管理、鼠标精度）8px 窄带：24px 触屏带宽侵入内容区会
     * 与状态栏/容器重叠形成改尺寸死区；fbdev 保持 24px 触屏口径。 */
    int band = XWD_RESIZE_ZONE;
    if (XWindow_isNativeWindowAttached(win))
        band = 8;
    w = XWidget_width(top);
    h = XWidget_height(top);
    if (pos->x < band) zone |= XWD_RZ_W;
    else if (pos->x >= w - band) zone |= XWD_RZ_E;
    if (pos->y >= barH) {
        if (pos->y >= h - band) zone |= XWD_RZ_S;
    } else {
        if (pos->y < XWD_RESIZE_ZONE_N) zone |= XWD_RZ_N;
    }
    return zone;
}

/** @brief 改尺寸方向位掩码 → 标准光标形状（同 Qt 约定：
 *  BDiag="/"=NE/SW 角、FDiag="\"=NW/SE 角，与 posix 字形映射一致）。 */
static XCursorShape xwd_resizeCursorShape(int zone)
{
    if ((zone & XWD_RZ_W) && (zone & XWD_RZ_N)) return XCursor_SizeFDiag;
    if ((zone & XWD_RZ_E) && (zone & XWD_RZ_S)) return XCursor_SizeFDiag;
    if ((zone & XWD_RZ_E) && (zone & XWD_RZ_N)) return XCursor_SizeBDiag;
    if ((zone & XWD_RZ_W) && (zone & XWD_RZ_S)) return XCursor_SizeBDiag;
    if (zone & (XWD_RZ_N | XWD_RZ_S)) return XCursor_SizeVer;
    return XCursor_SizeHor;
}

/** @brief 悬停边缘改尺寸光标反馈（仅原生窗；fbdev 无系统光标）。
 *  命中边缘带切换对应尺寸光标，离带还原 Arrow；形状键去重，同形状
 *  零开销。手势中不调用（保持手势起始形状，对标原生 WM）。 */
static void xwd_updateEdgeCursor(XWindowDecorationState* st, int zone)
{
    XCursor cursor;
    XCursorShape shape;
    int key;
    if (!st || !st->m_top || !st->m_window) return;
    if (!XWindow_isNativeWindowAttached(st->m_window)) return;
    shape = zone ? xwd_resizeCursorShape(zone) : XCursor_Arrow;
    key = (int)shape + 1;
    if (st->m_edgeCursor == key) return;
    st->m_edgeCursor = key;
    XCursor_init(&cursor);
    XCursor_setShape(&cursor, shape);
    XWidget_setCursor(st->m_top, &cursor);
    XClassDeinit(&cursor);
}

/* ==================== 内部辅助：重绘与几何 ==================== */

/** @brief 重绘标题条条带（轻量脏区）。
 *  绘制状态单一事实源归条控件：重绘前先把装饰状态机的 armed/hot 子控
 *  件位经 XTitleBar_setActiveSubControls 注入（条控件 paintEvent 按
 *  注入值选形），再刷条带脏区。 */
static void xwd_repaintStrip(XWidget* top)
{
    XRegion region;
    XRect strip;
    XWindowDecorationState* st = xwd_stateFor(top);
    XTitleBar* bar;
    int barH = xwd_stripHeight(st);
    if (!top || barH <= 0) return;
    if (st) {
        /* 活动子控件位注入仅对 XTitleBar 派生条执行（xwd_typedBar 守
         * 卫：向未知布局控件按 XTitleBar 偏移写 m_activeSubControls 会
         * 越界写坏宿主控件字段）。 */
        bar = xwd_typedBar(st);
        if (bar)
            XTitleBar_setActiveSubControls(bar,
                                           st->m_armed ? st->m_armed
                                                       : st->m_hot);
    }
    XRect_init(&strip, 0, 0, XWidget_width(top), barH);
    XRegion_init(&region);
    XRegion_addRect(&region, &strip);
    XWidget_repaintRegion(top, &region);
    XRegion_deinit(&region);
}

/** @brief 计算几何从 oldG 变到 newG 后暴露的差带（最多 4 条），返回条数。 */
static int xwd_exposeStrips(const XRect* oldG, const XRect* newG,
                            XRect out[4])
{
    int n = 0;
    if (!oldG || !newG || !out) return 0;
    if (newG->x > oldG->x)
        XRect_init(&out[n++], oldG->x, oldG->y, newG->x - oldG->x,
                   oldG->height);
    if (newG->x + newG->width < oldG->x + oldG->width)
        XRect_init(&out[n++], newG->x + newG->width, oldG->y,
                   oldG->x + oldG->width - (newG->x + newG->width),
                   oldG->height);
    if (newG->y > oldG->y)
        XRect_init(&out[n++], oldG->x, oldG->y, oldG->width,
                   newG->y - oldG->y);
    if (newG->y + newG->height < oldG->y + oldG->height)
        XRect_init(&out[n++], newG->x, newG->y + newG->height, newG->width,
                   oldG->y + oldG->height - (newG->y + newG->height));
    return n;
}

/** @brief fbdev 直写面板判定：显示驱动已注册且探测成功（框架即合成器
 *  的无 WM 环境）。桌面（无驱动注册）为 false——条带恢复整体不启用，
 *  行为与旧实现逐位一致（fill 在桌面本就 no-op，WM 自管重铺）。 */
static bool xwd_fbdevPanelDirect(void)
{
    XRect panel;
    return xwd_panelRect(NULL, &panel);
}

/** @brief 把 work[0..wn) 逐矩形扣除 cut（上下左右四条带切分，与
 *  xwd_clearOutsideWindow 同款切分语义），结果回写 work 并返回新条数。
 *  容量不足返回 -1（调用方降级，work 内容不再可信）；cap 上限 12。 */
static int xwd_rectListSubtract(XRect* work, int wn, int cap,
                                const XRect* cut)
{
    XRect next[12];
    int nn = 0;
    int i;
    int cutR = cut->x + cut->width;
    int cutB = cut->y + cut->height;
    for (i = 0; i < wn; ++i)
    {
        const XRect* r = &work[i];
        int rR = r->x + r->width;
        int rB = r->y + r->height;
        int ix0 = r->x > cut->x ? r->x : cut->x;
        int iy0 = r->y > cut->y ? r->y : cut->y;
        int ix1 = cutR < rR ? cutR : rR;
        int iy1 = cutB < rB ? cutB : rB;
        if (ix1 <= ix0 || iy1 <= iy0)
        {
            /* 不相交：整段保留。 */
            if (nn >= cap) return -1;
            next[nn++] = *r;
            continue;
        }
        /* 相交：保留上下左右四条带（均不含扣除矩形）。 */
        if (iy0 > r->y)
        {
            if (nn >= cap) return -1;
            XRect_init(&next[nn], r->x, r->y, r->width, iy0 - r->y);
            ++nn;
        }
        if (iy1 < rB)
        {
            if (nn >= cap) return -1;
            XRect_init(&next[nn], r->x, iy1, r->width, rB - iy1);
            ++nn;
        }
        if (ix0 > r->x)
        {
            if (nn >= cap) return -1;
            XRect_init(&next[nn], r->x, iy0, ix0 - r->x, iy1 - iy0);
            ++nn;
        }
        if (ix1 < rR)
        {
            if (nn >= cap) return -1;
            XRect_init(&next[nn], ix1, iy0, rR - ix1, iy1 - iy0);
            ++nn;
        }
    }
    XMemcpy(work, next, (size_t)nn * sizeof(XRect));
    return nn;
}

/** @brief 拖拽移动/改尺寸让位条带归位还原（拖动对话框白块根修
 *  2026-10-05）：本窗从 oldG 让出到 newG 的条带按「谁住着还谁」还原
 *  ——条带与其他可见顶层几何的交集自各归属顶层的后备缓冲直搬两缓冲
 *  （Z 序低→高遍历，高层覆写，与 present 遮挡剔除同 Z 约定），裸露
 *  面板余部（真桌面暴露）维持旧实现填桌面底色。
 *  @details 旧实现无条件把让位条带填桌面底色：弹层拖动场景条带住着
 *  父窗内容，每步被洗成桌面色（真机白块，松手 clearOutsideWindow 的
 *  扣除+整窗 expose 才擦平）。归属顶层还原不走 present/翻页（blit
 *  PanelRects 与 fill 同款双缓冲直写），单步仍只一次本窗整窗直提、
 *  无翻页等待；也不逐帧注入 expose（EXPOSE 处理恒整窗重合成，60 档
 *  拖拽烧穿 A33 单核）。源未合成（归属顶层无后备图像）退回整窗
 *  expose 兜底（罕见：可见但从未画过的顶层）；桌面无 fbdev 显示驱动
 *  时整体短路（调用方分流），X11 行为不变。工作集（12 槽）溢出降
 *  级：不填余部、已处理归属保留（绝不洗其他窗），残带留待松手清扫
 *  自愈。 */
static void xwd_restoreExposeStrips(const XRect* oldG, const XRect* newG,
                                    XWindow* self)
{
    XRect strips[4];
    XRect hits[12];
    XRect work[12];
    int wn = 0;
    int i;
    int n;
    bool truncated = false;
    XVector* tops;
    if (!oldG || !newG || !self) return;
    n = xwd_exposeStrips(oldG, newG, strips);
    for (i = 0; i < n && wn < 12; ++i) work[wn++] = strips[i];
    tops = XGuiApplication_topLevelWindows();
    if (tops)
    {
        size_t ti;
        size_t topCount = XVector_size_base(tops);
        for (ti = 0; ti < topCount && !truncated; ++ti)
        {
            XWindow* other = *(XWindow**)XVector_at_base(tops, (int64_t)ti);
            XRect og;
            int hitCount = 0;
            int wi;
            if (!other || other == self || !XWindow_isVisible(other))
                continue;
            og = XWindow_geometry(other);
            /* 收集本顶层与余部条带的交集（面板坐标）。 */
            for (wi = 0; wi < wn && hitCount < 12; ++wi)
            {
                const XRect* r = &work[wi];
                int ix0 = r->x > og.x ? r->x : og.x;
                int iy0 = r->y > og.y ? r->y : og.y;
                int ix1 = r->x + r->width < og.x + og.width
                              ? r->x + r->width : og.x + og.width;
                int iy1 = r->y + r->height < og.y + og.height
                              ? r->y + r->height : og.y + og.height;
                if (ix1 > ix0 && iy1 > iy0)
                {
                    XRect_init(&hits[hitCount], ix0, iy0, ix1 - ix0,
                               iy1 - iy0);
                    ++hitCount;
                }
            }
            if (hitCount > 0)
            {
                /* 归位还原：自归属顶层后备缓冲直搬（图像坐标=条带-窗
                 * 全局原点，与 present fbOrigin 同口径）。 */
                /* 桥接窗口→归属顶层控件直查（虚表同一性）：此前经
                 * XApplication_topLevelWidgets 反查，在仅创建
                 * XGuiApplication（XApplication 基层未初始化，g_xapp
                 * 空）的进程恒不命中——让位条带归位每次都退化为整窗
                 * expose 兜底（EXPOSE 处理恒整窗重合成，逐帧拖拽烧穿
                 * A33 单核；真机 demo 与离屏测试 2026-10-06 同链实证，
                 * 对话框/标题栏/键盘拖拽三路同缺陷）。 */
                XWidget* ownerTop = XWidget_widgetForWindow(other);
                {
                    XBackingStore* bs =
                        ownerTop ? XWidget_backingStore(ownerTop) : NULL;
                    XPlatformBackingStore* pbs =
                        bs ? XBackingStore_handle(bs) : NULL;
                    XImage* img =
                        pbs ? XPlatformBackingStore_paintDevice(pbs) : NULL;
                    if (pbs && img)
                    {
                        XPoint origin;
                        XPoint_init(&origin, og.x, og.y);
                        XPlatformBackingStore_blitPanelRects(pbs, hits,
                                                             hitCount,
                                                             &origin);
                    }
                    else
                    {
                        /* 兜底：源未合成，整窗 expose 让归属顶层自绘
                         * （present 遮挡剔除保证更高层不被覆盖）。 */
                        XRegion expose;
                        XRect local;
                        XRegion_init(&expose);
                        XRect_init(&local, 0, 0, og.width, og.height);
                        XRegion_addRect(&expose, &local);
                        XWindowSystemInterface_handleExposeEvent(other,
                                                                 &expose);
                        XRegion_deinit(&expose);
                    }
                }
            }
            /* 从余部扣除本顶层矩形（后续归属与桌面余部不再含它）。 */
            {
                int sub = xwd_rectListSubtract(work, wn, 12, &og);
                if (sub < 0)
                {
                    truncated = true;
                    break;
                }
                wn = sub;
            }
        }
        XClassDelete(tops);
    }
    /* 裸露面板余部=真桌面暴露：维持旧实现填桌面底色两缓冲。截断降级
     * 不填色（绝不洗其他窗），已处理归属的还原已落地，残带留待松手
     * clearOutsideWindow 清扫自愈。 */
    if (!truncated)
        XPlatformBackingStore_fillPanelRects(work, wn, XWD_DESKTOP_PIXEL);
}

bool XWindowDecoration_fbdevPanelRect(XRect* outPanel)
{
    XRect panel;
    /* 与装饰内部 xwd_panelRect(NULL,·) 同语义：仅 fbdev 显示驱动在位时
     * 成立（桌面 WM 环境 XScreen 回退需窗口上下文，NULL 恒 false）。 */
    if (!xwd_panelRect(NULL, &panel)) return false;
    if (outPanel) *outPanel = panel;
    return true;
}

void XWindowDecoration_restoreExposeStrips(const XRect* oldG,
                                           const XRect* newG,
                                           XWindow* selfWindow)
{
    /* 让位条带按归属归位还原（装饰拖拽同款机制；消费方=非装饰拖移路径
     * 屏幕键盘紧凑悬浮拖移）。调用契约见头注：先落几何后调用。 */
    xwd_restoreExposeStrips(oldG, newG, selfWindow);
}

/** @brief 拖拽/改尺寸结束的兜底清扫：窗口外全部面板区域填桌面底色
 *  两缓冲（逐帧差带只覆盖增量；松手一次清干净=桌面 WM 每次几何变化
 *  整屏重铺）。
 *  2026-10-04 增「扣除其他可见顶层」：本窗之外的面积里还住着父窗/
 *  其他弹层（拖动对话框场景 panel\对话框 含主窗全区），整片直填会把
 *  它们洗成桌面底色（真机拖动对话框全屏白屏实证）。改为逐顶层矩形
 *  扣除：裸露面板余部填底色，被占部分注入 expose 由归属顶层自行
 *  重绘（expose 处理=整窗直提，一次即可）。工作集溢出降级为「不填
 *  色、已见顶层走 expose」，绝不洗别的窗。 */
static void xwd_clearOutsideWindow(XWindowDecorationState* st)
{
    XRect panel;
    XRect g;
    XRect bands[4];
    XRect work[12];
    int wn = 0;
    int i;
    bool truncated = false;
    XVector* tops;
    if (!xwd_panelRect(st, &panel)) return;
    g = XWidget_geometry(st->m_top);
    XRect_init(&bands[0], panel.x, panel.y, g.x - panel.x, panel.height);
    XRect_init(&bands[1], g.x + g.width, panel.y,
               panel.x + panel.width - (g.x + g.width), panel.height);
    XRect_init(&bands[2], g.x, panel.y, g.width, g.y - panel.y);
    XRect_init(&bands[3], g.x, g.y + g.height, g.width,
               panel.y + panel.height - (g.y + g.height));
    for (i = 0; i < 4; ++i)
    {
        if (bands[i].width > 0 && bands[i].height > 0 && wn < 12)
            work[wn++] = bands[i];
    }
    tops = XGuiApplication_topLevelWindows();
    if (tops)
    {
        size_t ti;
        size_t topCount = XVector_size_base(tops);
        for (ti = 0; ti < topCount && !truncated; ++ti)
        {
            XWindow* other = *(XWindow**)XVector_at_base(tops, (int64_t)ti);
            XRect og;
            int oi;
            bool anyHit = false;
            if (!other || other == st->m_window || !XWindow_isVisible(other))
                continue;
            og = XWindow_geometry(other);
            /* 扣除：work 各矩形 \ 本顶层矩形（上下左右四条带切分）。 */
            for (oi = 0; oi < wn && !truncated; ++oi)
            {
                XRect next[12];
                int nn = 0;
                const XRect* r = &work[oi];
                int rx1 = r->x + r->width;
                int ry1 = r->y + r->height;
                int ix0 = r->x > og.x ? r->x : og.x;
                int iy0 = r->y > og.y ? r->y : og.y;
                int ix1 = rx1 < og.x + og.width ? rx1 : og.x + og.width;
                int iy1 = ry1 < og.y + og.height ? ry1 : og.y + og.height;
                if (ix1 <= ix0 || iy1 <= iy0)
                {
                    /* 不相交：整段保留。 */
                    if (nn < 12) next[nn++] = *r;
                    else truncated = true;
                }
                else
                {
                    anyHit = true;
                    if (iy0 > r->y)
                    {
                        if (nn < 12)
                        {
                            XRect_init(&next[nn], r->x, r->y, r->width,
                                       iy0 - r->y);
                            ++nn;
                        }
                        else truncated = true;
                    }
                    if (iy1 < ry1)
                    {
                        if (nn < 12)
                        {
                            XRect_init(&next[nn], r->x, iy1, r->width,
                                       ry1 - iy1);
                            ++nn;
                        }
                        else truncated = true;
                    }
                    if (ix0 > r->x)
                    {
                        if (nn < 12)
                        {
                            XRect_init(&next[nn], r->x, iy0, ix0 - r->x,
                                       iy1 - iy0);
                            ++nn;
                        }
                        else truncated = true;
                    }
                    if (ix1 < rx1)
                    {
                        if (nn < 12)
                        {
                            XRect_init(&next[nn], ix1, iy0, rx1 - ix1,
                                       iy1 - iy0);
                            ++nn;
                        }
                        else truncated = true;
                    }
                }
                if (!truncated)
                {
                    int ci;
                    for (ci = 0; ci < nn; ++ci) work[ci] = next[ci];
                    wn = nn;
                }
            }
            /* 被本顶层占据的部分交还它自己重绘（expose 处理恒整窗
             * 直提，一次即可；present 遮挡剔除保证更高层不被覆盖）。 */
            if (anyHit && !truncated)
            {
                XRegion expose;
                XRect local;
                XRegion_init(&expose);
                XRect_init(&local, 0, 0, og.width, og.height);
                XRegion_addRect(&expose, &local);
                XWindowSystemInterface_handleExposeEvent(other, &expose);
                XRegion_deinit(&expose);
            }
        }
        XClassDelete(tops);
    }
    /* 截断降级不填色（绝不洗其他窗）：被占顶层已 expose 重绘，裸露
     * 面板余部留待后续帧差带自愈。 */
    if (!truncated)
        XPlatformBackingStore_fillPanelRects(work, wn, XWD_DESKTOP_PIXEL);
}

/* ==================== 拖动快照 blit 模式（Qt4 QWS 同款语义） ==================== */

/** @brief [gesture] 分相探针累计（env XGUI_GESTURE_PROF 门控，关闭恒
 *  零成本：开关静态缓存，结构体只在 begin 时触碰）。 */
typedef struct XwdGestureProf
{
    bool m_active;       /**< 手势探针进行中（begin 置位，report 清零）。 */
    bool m_snapshot;     /**< 本手势启用快照 blit 模式。 */
    int m_steps;         /**< 落地步数（增量非零的 applyMove）。 */
    int m_snapSteps;     /**< 其中快照直写步数（拒绝降级步归 legacy）。 */
    int m_geomMs;        /**< 几何相累计（setGeometry+条重钉）。 */
    int m_stripMs;       /**< 条带相累计（归属还原/桌面填色）。 */
    int m_submitMs;      /**< 提交相累计（快照 blit 或整窗 flush）。 */
    int m_totalMs;       /**< 步耗时累计。 */
    int m_minMs;         /**< 单步最小耗时。 */
    int m_maxMs;         /**< 单步最大耗时。 */
} XwdGestureProf;
static XwdGestureProf g_xwdProf;

/** @brief XGUI_DRAG_SNAPSHOT_BLIT env 门控（每次手势开始现读：A/B 对照
 *  可在拖动间隙 setenv 生效，手势内不翻转）。默认开；值恰为 "0" 时回退
 *  既有 flush 路径。 */
static bool xwd_dragSnapshotEnvOn(void)
{
    const char* env = getenv("XGUI_DRAG_SNAPSHOT_BLIT");
    return !(env && env[0] == '0' && env[1] == '\0');
}

/** @brief XGUI_GESTURE_PROF env 门控（开关静态缓存：运行期不改）。 */
static bool xwd_gestureProfOn(void)
{
    static int on = -1;
    if (on < 0) on = getenv("XGUI_GESTURE_PROF") ? 1 : 0;
    return on != 0;
}

/** @brief 手势探针开工（上一次报告残留不续账：整体清零）。 */
static void xwd_gestureProfBegin(bool snapshotMode)
{
    if (!xwd_gestureProfOn()) return;
    XMemset(&g_xwdProf, 0, sizeof(g_xwdProf));
    g_xwdProf.m_active = true;
    g_xwdProf.m_snapshot = snapshotMode;
}

/** @brief 记一步分相（探针关零成本：调用方已在 prof 分支内取好戳）。 */
static void xwd_gestureProfStep(bool prof, bool snapshot, int geomMs,
                                int stripMs, int submitMs, int totalMs)
{
    if (!prof) return;
    ++g_xwdProf.m_steps;
    if (snapshot) ++g_xwdProf.m_snapSteps;
    g_xwdProf.m_geomMs += geomMs;
    g_xwdProf.m_stripMs += stripMs;
    g_xwdProf.m_submitMs += submitMs;
    g_xwdProf.m_totalMs += totalMs;
    if (g_xwdProf.m_steps == 1 || totalMs < g_xwdProf.m_minMs)
        g_xwdProf.m_minMs = totalMs;
    if (totalMs > g_xwdProf.m_maxMs) g_xwdProf.m_maxMs = totalMs;
}

/** @brief 手势探针收工报告（stderr 一行；无落地步不输出）。 */
static void xwd_gestureProfReport(void)
{
    if (!g_xwdProf.m_active) return;
    g_xwdProf.m_active = false;
    if (g_xwdProf.m_steps <= 0) return;
    fprintf(stderr,
            "[xgesture] drag mode=%s steps=%d snap=%d total=%dms "
            "avg=%.1fms min=%dms max=%dms | geom avg=%.2fms "
            "strips avg=%.2fms submit avg=%.2fms\n",
            g_xwdProf.m_snapshot ? "snapshot" : "legacy",
            g_xwdProf.m_steps, g_xwdProf.m_snapSteps,
            g_xwdProf.m_totalMs,
            (double)g_xwdProf.m_totalMs / (double)g_xwdProf.m_steps,
            g_xwdProf.m_minMs, g_xwdProf.m_maxMs,
            (double)g_xwdProf.m_geomMs / (double)g_xwdProf.m_steps,
            (double)g_xwdProf.m_stripMs / (double)g_xwdProf.m_steps,
            (double)g_xwdProf.m_submitMs / (double)g_xwdProf.m_steps);
}

/** @brief 拖动快照预备（PRESS 臂拖拽认领时调用）：满足条件时把窗口
 *  当前像素自后备缓冲已合成内容整幅拷入窗口大小离屏快照。
 *  @details 快照源=后备缓冲 paintDevice 图像而非 fb 本体：DIRECT 路径
 *  上一帧 flush 的提交源即本图像，与 fb 可见像素同源同值；比读 fb 免去
 *  高层弹层像素污染（弹层只改 fb 不改本窗后备缓冲）。启用条件（任一
 *  不满足即整手势回退既有 flush 路径，逐位旧行为）：env
 *  XGUI_DRAG_SNAPSHOT_BLIT 非 "0"（默认开）；fbdev 直写面板（框架即
 *  合成器；桌面 WM 环境零参与）；后备存储/绘制图像在；快照格式与面板
 *  扫描格式协商一致（直写零转换）；快照离屏分配成功（A33 内存紧张，
 *  失败即回退=红线）。PRESS 时条带重绘（requestActivate 触发）已在
 *  repaintRegion 同步语义下落进后备缓冲，快照不会捕到旧条带色。 */
static void xwd_dragSnapshotBegin(XWindowDecorationState* st)
{
    XWidget* top;
    XBackingStore* bs;
    XPlatformBackingStore* pbs;
    XImage* img;
    XImageFormat fmt;
    int w;
    int h;
    int y;
    int srcBpl;
    int dstBpl;
    size_t pixelBytes;
    size_t rowBytes;
    const uint8_t* s;
    uint8_t* d;
    st->m_dragSnapActive = false;
    if (!st || !st->m_window || !st->m_top) return;
    if (!xwd_dragSnapshotEnvOn()) return;
    if (!xwd_fbdevPanelDirect()) return; /* 桌面 WM 环境：整手势旧路径。 */
    top = st->m_top;
    bs = XWidget_backingStore(top);
    pbs = bs ? XBackingStore_handle(bs) : NULL;
    img = pbs ? XPlatformBackingStore_paintDevice(pbs) : NULL;
    if (!img || !img->m_data || XImage_isNull(img)) return;
#if XGUI_ON && XPLATFORM_FBDEV_ON
    {
        /* 显示驱动在位 + 快照格式可直写（与 present 直写同款协商）；
         * 任一不满足整手势回退旧路径。 */
        const XPlatformDisplayDriverOps* ops;
        XPlatformDisplayInfo info;
        XImageFormat panel = XImageFormat_Invalid;
        ops = XPlatformDisplayDriver_active();
        if (!ops || !ops->probe || !ops->probe(&info)) return;
        fmt = XImage_format(img);
        if (!ops->formatNegotiate(fmt, &panel) || panel != fmt) return;
    }
#else
    return; /* 非 fbdev 构建不可达（上方 fbdevPanelDirect 已短路）。 */
#endif
    w = XImage_width(img);
    h = XImage_height(img);
    if (w <= 0 || h <= 0) return;
    /* 快照分配失败=整手势回退旧路径（A33 内存紧张红线）。 */
    if (!XImage_reinit_ex(&st->m_dragSnap, w, h, fmt)) return;
    srcBpl = XImage_bytesPerLine(img);
    dstBpl = XImage_bytesPerLine(&st->m_dragSnap);
    pixelBytes = (size_t)((XImageFormat_bitDepth(fmt) + 7) / 8);
    rowBytes = (size_t)w * pixelBytes;
    s = XImage_constBits(img);
    d = XImage_bits(&st->m_dragSnap);
    if (!s || !d || srcBpl <= 0 || dstBpl <= 0)
    {
        XClassDeinit(&st->m_dragSnap);
        XMemset(&st->m_dragSnap, 0, sizeof(st->m_dragSnap));
        return;
    }
    for (y = 0; y < h; ++y)
        XMemcpy(d + (size_t)y * (size_t)dstBpl, s + (size_t)y * (size_t)srcBpl,
                rowBytes);
    st->m_dragSnapActive = true;
}

/** @brief 拖动结束：快照释放 + 结算（settle）+ 探针报告。
 *  @details settle=true 时经 XWidget_update 挂整窗脏区——下一事件循环
 *  周期 PAINT→paintTree 整窗重绘→flush→present（差带账本把拖动期写入
 *  可见面的行带补进后台缓冲并恢复轮换翻页），即「拖动结束必须真实落
 *  定」红线；纯移动内容零变化，结算帧与快照帧逐像素同值，无闪变。
 *  settle=false 用于残手势清理（PRESS 重臂）与宿主注销（窗口将亡，
 *  无可结算）：fb 在快照模式下逐步保持正确，无需补偿。快照无论路径
 *  立即释放（A33 内存红线）。 */
static void xwd_dragSnapshotEnd(XWindowDecorationState* st, bool settle)
{
    if (!st) return;
    if (st->m_dragSnapActive)
    {
        st->m_dragSnapActive = false;
        /* 真实落定=同步【整窗】重绘（2026-10-06 键盘拖移战役根修）：
         * 原为 XWidget_update（异步 PAINT，区域=m_dirty 快照）——手势
         * 期 m_dirty 残留按压条带矩形时，落定 present 只盖标题条，内
         * 容区欠账留给「另一缓冲恰好还住着拖动前完整帧」的运气（旧路
         * 每步归属 expose 翻缓冲+往返回到原点恰好掩盖；归属反查根修
         * 后翻停了，离屏 DM 测试 release sweep 即现半窗残缺）。repaint
         * 同步整窗 flush，把「快照 blit 只写可见面、另一缓冲欠账由落
         * 定整窗差带同步补齐」的契约真正落死。 */
        if (settle && st->m_top) XWidget_repaint(st->m_top);
        XClassDeinit(&st->m_dragSnap);
        XMemset(&st->m_dragSnap, 0, sizeof(st->m_dragSnap));
    }
    xwd_gestureProfReport();
}

/** @brief 拖动手势开工（PRESS 臂）：探针开工 + 快照预备。 */
static void xwd_dragGestureBegin(XWindowDecorationState* st)
{
    xwd_dragSnapshotBegin(st);
    xwd_gestureProfBegin(st ? st->m_dragSnapActive : false);
}

/** @brief 拖动手势收工：快照释放 + （可选）真实落定 + 探针报告。 */
static void xwd_dragGestureEnd(XWindowDecorationState* st, bool settle)
{
    xwd_dragSnapshotEnd(st, settle);
}

/** @brief 平台指针抓取开关（仅原生窗；fbdev 无原生窗 no-op）。
 *  @details 软件抓取（g_mouseGrabWidget 重路由）只在事件抵达本窗口时
 *           生效——收缩拖拽中窗口边缘向指针「逃逸」，指针出窗后 MOVE
 *           断流、改尺寸冻结在中途宽度（dde 实测）。平台 XGrabPointer
 *           owner_events=True 使指针在窗外仍向本窗投递（坐标可为窗外
 *           值，装饰增量按全局锚点计算天然兼容），松手即解。 */
static void xwd_platformGrab(XWindowDecorationState* st, bool grab)
{
    if (!st || !st->m_window) return;
    if (!XWindow_isNativeWindowAttached(st->m_window)) return;
    XPlatformNativeWindow_setMouseGrabEnabled(st->m_window, grab);
}

/** @brief 终止拖拽/改尺寸状态机并解除鼠标抓取。
 *  真机教训（昆仑通态 2026-09-28）：双击序列 DBL_CLICK 在按住中途到
 *  来，抓取与增量锚若穿越几何切换，后续 MOVE 按旧锚点产生伪增量把窗
 *  口平移到非预期位置——任何几何切换前必须先走这里。 */
static void xwd_endInteractions(XWindowDecorationState* st)
{
    if (st->m_dragging || st->m_resizing) {
        XWidget_releaseMouse(st->m_top);
        xwd_platformGrab(st, false);
    }
    if (st->m_resizing)
        XPlatformNativeWindow_deferGeometry(st->m_window, false);
    if (st->m_dragging)
        xwd_dragGestureEnd(st, true); /* 快照释放+真实落定+探针报告。 */
    st->m_dragging = false;
    st->m_resizing = false;
    st->m_armed = 0;
    st->m_hot = 0;
}

#if XGUI_RESIZE_REPAINT_MAX_FPS > 0
/** @brief 改尺寸限帧闸（XGUI_RESIZE_REPAINT_MAX_FPS）：返回 1=本步跳过。
 *  @details 节流戳全局连续、【不在手势 PRESS 清零】——TSC2007 电阻屏
 *           幻触（press/release 抖动）会在拖拽中段插入 PRESS，清戳等于
 *           每次幻触都把闸打穿回手指速率（真机实测 HUD 100+ 的根因）；
 *           跨手势首步至多等一个间隔（≤16.7ms，无感）。间隔取
 *           1000.0/fps（double 除，60fps=16.67ms 真口径；整数地板
 *           1000/60=16ms 会给出 62.5fps 超标）；宏 >0 时编译期常量，
 *           运行期无除零。跳过的整步不落地不重绘——xwd_applyResize
 *           无状态（几何恒由按下锚 m_resizeGeometry + 距按下点总位移
 *           重算），被跳的中间位移天然合并，下一放行帧一次落到位；
 *           RELEASE 分支补一次无条件落地保尾帧（见 handlePointer）。
 *           整闸（落地+重绘一起限）：只限重绘不限落地时，桌面 X11 每
 *           MOVE 仍 syncWindowGeometry→Expose→整窗 flush，帧率限不住
 *           反而多烧重排与缓冲重建。 */
static int xwd_resizeThrottleSkip(XWindowDecorationState* st)
{
    int64_t nowMs = XDateTime_currentMSecsSinceEpoch();
    if (st->m_lastGestureStepMs != 0 &&
        (double)(nowMs - st->m_lastGestureStepMs) <
            1000.0 / (double)XGUI_RESIZE_REPAINT_MAX_FPS)
        return 1;
    st->m_lastGestureStepMs = nowMs;
    return 0;
}
#endif

#if XGUI_RESIZE_REPAINT_MAX_FPS > 0
/** @brief 拖拽移动直提限帧闸（XGUI_RESIZE_REPAINT_MAX_FPS）：返回 1=本
 *  步跳过。
 *  @details 用户裁定 2026-09-29：拖拽移动与改尺寸同归手势宏——applyMove
 *           免重绘直提后备缓冲（绕过 flushBackingStore 的呈现限频闸），
 *           PRESENT 宏管不到，移动限帧必须在 MOVE 源头。跳帧时调用方
 *           【不得推进 m_dragLast】——增量恒=g−m_dragLast 逐事件累计，
 *           闸与锚推进同进退，下一放行帧一次搬到位（自然合并零丢失），
 *           写错即位移丢失；RELEASE 分支补一次无条件搬运收尾。纯移动
 *           内容零变化（后备缓冲仍是上一帧合法合成结果），跳帧零损失。 */
static int xwd_moveThrottleSkip(XWindowDecorationState* st)
{
    int64_t nowMs = XDateTime_currentMSecsSinceEpoch();
    if (st->m_lastGestureStepMs != 0 &&
        (double)(nowMs - st->m_lastGestureStepMs) <
            1000.0 / (double)XGUI_RESIZE_REPAINT_MAX_FPS)
        return 1;
    st->m_lastGestureStepMs = nowMs;
    return 0;
}
#endif

/** @brief 最小化=卷起/展开（Shade）：嵌入式无 shell/任务栏，「最小化」
 *  旧 demo 仅按压反馈（如实降级）。现做成真行为：卷起=窗口收起只剩
 *  标题条（高度=条高），展开=恢复原几何；再点 — 或系统菜单「最小化」
 *  往返切换。卷起暴露的下方区域铺桌面底色（同复原路径，禁
 *  requestPanelClear——延迟清零会打黑刚铺的桌面色）。桌面 WM 管理下
 *  不进本函数（见 xwd_minimizeAction 分流）。 */
static void xwd_setShade(XWindowDecorationState* st, bool on)
{
    XRect g;
    int barH;
    if (!st || !st->m_top || on == st->m_shaded) return;
    xwd_endInteractions(st);
    barH = xwd_stripHeight(st);
    if (on) {
        g = XWidget_geometry(st->m_top);
        if (g.height <= barH) return; /* 已是条态，防重入。 */
        st->m_preShadeGeometry = g;
        st->m_shaded = true;
        XWidget_setGeometry(st->m_top, g.x, g.y, g.width, barH);
        xwd_pinBarGeometry(st); /* 卷起后宿主宽未变，重钉幂等保底。 */
        xwd_clearOutsideWindow(st);
        XWidget_repaint(st->m_top);
    } else {
        st->m_shaded = false;
        XWidget_setGeometryRect(st->m_top, &st->m_preShadeGeometry);
        xwd_pinBarGeometry(st); /* 展开恢复后同上保底。 */
        XWidget_repaint(st->m_top);
    }
}

/** @brief 最小化动作（按宿主环境分流语义）：窗口挂有原生窗（桌面
 *  WM 管理）时走 XWidget_showMinimized——ICCCM/EWMH 图标化进任务栏，
 *  与系统标题栏语义对齐（用户裁定 2026-09-29：桌面不做卷起）；无原
 *  生窗（fbdev 直写面板、无 shell/任务栏）保持卷起/展开（Shade）定
 *  版口径：窗口收起只剩标题条，再点往返，暴露区铺桌面底色。 */
static void xwd_minimizeAction(XWindowDecorationState* st)
{
    if (!st || !st->m_top) return;
    if (st->m_window && XWindow_isNativeWindowAttached(st->m_window)) {
        xwd_endInteractions(st);
        XWidget_showMinimized(st->m_top);
        return;
    }
    xwd_setShade(st, !st->m_shaded);
}

/** @brief 拖拽移动：按增量平移窗口并钳到面板内；暴露差带先填两缓冲
 *  （逐帧路径禁 requestPanelClear——整屏 memset 含可见缓冲会整体频闪，
 *  真机 2026-09-28 实测教训）；末尾不经 paintTree 重新合成，把既有后
 *  备缓冲整窗直提（纯移动免重绘，见函数尾注）。
 *  快照 blit 模式（XGUI_DRAG_SNAPSHOT_BLIT 默认开）：差带账本重搬/软件
 *  双缓冲互同步/整窗 flush 全部旁路——拖动开始时已快照窗口像素，每步
 *  只做「条带归位 + 窗口快照整块直写 fb 可见面 + 收窄 cacheSync + pan
 *  收敛」（Qt4 QWS moveWindow→blit 同款语义），cacheSync 只碰窗口一块
 *  矩形；镜像帧同步经 notifyPresentRegion 与旧 flush 同形通知。拖动结
 *  束由 xwd_dragSnapshotEnd 真实整窗 PAINT 落定并恢复轮换写。 */
static void xwd_applyMove(XWindowDecorationState* st, int dx, int dy)
{
    XWidget* top = st->m_top;
    XRect g = XWidget_geometry(top);
    XRect panel;
    XRect newG;
    XRect exposed[4];
    XBackingStore* store;
    XRegion region;
    XRect whole;
    int nx;
    int ny;
    int stripCount;
    /* [gesture] 分相戳（探针关恒 0 开销：xwd_gestureProfOn 静态缓存）。 */
    bool prof = xwd_gestureProfOn();
    int64_t tGeom0 = 0;
    int64_t tSub0 = 0;
    int geomMs = 0;
    int stripMs = 0;
    int submitMs = 0;
    int totalMs = 0;
    bool snapBlit = false;
    if (prof) tGeom0 = XDateTime_currentMSecsSinceEpoch();
    if (!dx && !dy) return;
    nx = g.x + dx;
    ny = g.y + dy;
    if (xwd_panelRect(st, &panel)) {
        if (nx < panel.x) nx = panel.x;
        if (ny < panel.y) ny = panel.y;
        if (nx + g.width > panel.x + panel.width)
            nx = panel.x + panel.width - g.width;
        if (ny + g.height > panel.y + panel.height)
            ny = panel.y + panel.height - g.height;
    }
    if (nx == g.x && ny == g.y) return;
    XRect_init(&newG, nx, ny, g.width, g.height);
    stripCount = xwd_exposeStrips(&g, &newG, exposed);
    /* 先落几何再还原条带（拖动对话框白块根修 2026-10-05，原顺序为
     * 「先填桌面色后移窗」）：归属顶层的条带还原与 expose 兜底都以
     * 「本窗已离开旧位」为前提——present 遮挡剔除按现几何收集弹层矩
     * 形，移动前直提会被旧位弹层矩形整条剔空（等于没还原）。 */
    XWidget_setGeometry(top, nx, ny, g.width, g.height);
    xwd_pinBarGeometry(st); /* 纯移动不改宿主宽，重钉同值幂等零开销。 */
    if (prof) geomMs = (int)(XDateTime_currentMSecsSinceEpoch() - tGeom0);
    tGeom0 = XDateTime_currentMSecsSinceEpoch();
    if (stripCount > 0)
    {
        if (xwd_fbdevPanelDirect()) /* fbdev 无 WM：条带按归属归位。 */
            xwd_restoreExposeStrips(&g, &newG, st->m_window);
        else /* 桌面 WM 环境：旧实现口径（fill 桌面本就 no-op）。 */
            XPlatformBackingStore_fillPanelRects(exposed, stripCount,
                                           XWD_DESKTOP_PIXEL);
    }
    if (prof)
    {
        stripMs = (int)(XDateTime_currentMSecsSinceEpoch() - tGeom0);
        tSub0 = XDateTime_currentMSecsSinceEpoch();
    }
    /* 快照 blit 步：窗口快照整块直写 fb 可见面（含弹层遮挡剔除），
     * 零 flush/零账本/零重绘；直写被拒（驱动/格式/首帧前异常）时落
     * 入下方既有 flush 路径，本步按 legacy 计。 */
    if (st->m_dragSnapActive)
    {
        XBackingStore* snapStore;
        XPlatformBackingStore* snapPbs;
        XPoint origin;
        bool ok;
        XPoint_init(&origin, newG.x, newG.y);
        ok = XPlatformBackingStore_blitSnapshotPanelRects(
            &st->m_dragSnap, &newG, 1, &origin, st->m_window);
        if (ok)
        {
            ++g_xwdDragSnapStepCount;
            snapBlit = true;
            /* 镜像帧同步：与旧 flush 路径同形区域通知（窗口坐标全幅、
             * offset 零点）。未挂镜像会话时回调内部即刻短路，开销为
             * 一次区域深拷贝。 */
            snapStore = XWidget_backingStore(top);
            snapPbs = snapStore ? XBackingStore_handle(snapStore) : NULL;
            if (snapPbs)
            {
                XRegion_init(&region);
                XRect_init(&whole, 0, 0, g.width, g.height);
                XRegion_addRect(&region, &whole);
                XPlatformBackingStore_notifyPresentRegion(snapPbs, &region,
                                                          NULL);
                XRegion_deinit(&region);
            }
        }
        if (prof)
        {
            submitMs = (int)(XDateTime_currentMSecsSinceEpoch() - tSub0);
            if (ok)
            {
                totalMs = geomMs + stripMs + submitMs;
                xwd_gestureProfStep(prof, true, geomMs, stripMs, submitMs,
                                    totalMs);
            }
            /* 直写拒绝：不在此记步，落入下方 flush 路径统一记一次。 */
        }
        if (ok) return;
        submitMs = 0; /* 直写拒绝：本步提交相归 flush 重计。 */
    }
    /* 纯移动免重绘：拖拽只改位置不改内容，后备缓冲仍是上一帧的合法合
     * 成结果——不再 paintTree 逐事件同步整窗重绘，直接把既有内容整窗
     * 重提交（对标 Qt 移动窗口不走 repaint 的合成语义）。直提按提交时
     * 刻几何搬运：fbdev 直写路径的 fbOrigin 由 present 内
     * xpbs_windowFbOrigin 按 XWindow_geometry 现算
     * （XPlatformBackingStore_posix.c），双缓冲差带账本
     * g_xpbsFbBackDirty 只承担「写前把上一帧落笔行带从可见缓冲补进写
     * 入缓冲」，不参与拷贝主循环的执行与否——账本为空仅补齐步空转，
     * 整窗矩形照常 memcpy 落到新位置，账本结论=直提可行。暴露条带已在
     * 上方填两缓冲，直提无旧位残影；高层弹层遮挡剔除在同一直写路径内
     * 生效。store 缺席退回 update 合帧保底：位置变化经 update 合帧——
     * 同一事件循环周期内多步移动只挂脏区投递一次 PAINT（对标 Qt
     * move→update 语义）。 */
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON
    store = XWidget_backingStore(top);
    if (store) {
        XRect_init(&whole, 0, 0, g.width, g.height);
        XRegion_init(&region);
        XRegion_addRect(&region, &whole);
        XBackingStore_flush(store, &region, st->m_window, NULL);
        XRegion_deinit(&region);
    } else
#endif /* 平台后备存储裁剪时直提不可用，落下方保底。 */
    {
        XWidget_update(top);
    }
    if (prof)
    {
        submitMs += (int)(XDateTime_currentMSecsSinceEpoch() - tSub0);
        totalMs = geomMs + stripMs + submitMs;
        xwd_gestureProfStep(prof, snapBlit, geomMs, stripMs, submitMs,
                            totalMs);
    }
}

/** @brief 边缘改尺寸：按方向位掩码应用「距按下点总位移」（逐帧刷新
 *  锚点会使增量回摆、边框只跟一小段——真机实测教训），钳最小尺寸与
 *  面板边界；暴露差带先填两缓冲。 */
static void xwd_applyResize(XWindowDecorationState* st, int dx, int dy)
{
    XWidget* top = st->m_top;
    XRect g = st->m_resizeGeometry;
    XRect newG = g;
    XRect panel;
    XRect exposed[4];
    int stripCount;
    int zone = st->m_resizeMask;
    XSize minSize = XWindow_minimumSize(st->m_window);
    if (!zone || (!dx && !dy)) return;
    if (zone & XWD_RZ_E) newG.width = g.width + dx;
    if (zone & XWD_RZ_S) newG.height = g.height + dy;
    if (zone & XWD_RZ_W) {
        newG.x = g.x + dx;
        newG.width = g.width - dx;
    }
    if (zone & XWD_RZ_N) {
        newG.y = g.y + dy;
        newG.height = g.height - dy;
    }
    /* 最小尺寸钳制（W/N 向同时收位移防几何翻转；未设最小尺寸时按 1px
       底限，禁零/负几何）。 */
    if (minSize.width <= 0) minSize.width = 1;
    if (minSize.height <= 0) minSize.height = 1;
    if (newG.width < minSize.width) {
        if (zone & XWD_RZ_W) newG.x = g.x + g.width - minSize.width;
        newG.width = minSize.width;
    }
    if (newG.height < minSize.height) {
        if (zone & XWD_RZ_N) newG.y = g.y + g.height - minSize.height;
        newG.height = minSize.height;
    }
    if (xwd_panelRect(st, &panel)) {
        if (newG.x < panel.x) {
            newG.width += newG.x - panel.x;
            newG.x = panel.x;
        }
        if (newG.y < panel.y) {
            newG.height += newG.y - panel.y;
            newG.y = panel.y;
        }
        if (newG.x + newG.width > panel.x + panel.width)
            newG.width = panel.x + panel.width - newG.x;
        if (newG.y + newG.height > panel.y + panel.height)
            newG.height = panel.y + panel.height - newG.y;
    }
    if (newG.width < 1) newG.width = 1;
    if (newG.height < 1) newG.height = 1;
    if (newG.width == g.width && newG.height == g.height &&
        newG.x == g.x && newG.y == g.y)
        return;
    stripCount = xwd_exposeStrips(&g, &newG, exposed);
    /* 先落几何再还原条带：同 xwd_applyMove 的顺序论证（归属顶层还原
     * 与 expose 兜底都要求本窗已离开旧位，遮挡剔除按现几何收集）。 */
    XWidget_setGeometry(top, newG.x, newG.y, newG.width, newG.height);
    xwd_pinBarGeometry(st); /* 宿主宽变化→条宽同步重钉（挂点核实见该函数注）。 */
    if (stripCount > 0)
    {
        if (xwd_fbdevPanelDirect()) /* fbdev 无 WM：条带按归属归位。 */
            xwd_restoreExposeStrips(&g, &newG, st->m_window);
        else /* 桌面 WM 环境：旧实现口径（fill 桌面本就 no-op）。 */
            XPlatformBackingStore_fillPanelRects(exposed, stripCount,
                                           XWD_DESKTOP_PIXEL);
    }
    /* resizeEvent 已随 setGeometry 派发；W/N 向钳制后尺寸未变的纯位移
       无 resizeEvent，显式补一帧保底。位置/尺寸变化经 update 合帧——
       同一事件循环周期内多步缩放只挂脏区并投递一次 PAINT（对标 Qt
       move→update 语义），替代逐事件同步整窗重绘（改尺寸内容随几何
       变化，须走 paintTree 重新合成，无直提可省）。 */
    XWidget_update(top);
}

#if XMENU_ON

/* ==================== 内部辅助：系统菜单 ==================== */

/** @brief 系统菜单动作槽：还原。接收者=菜单本体（父挂顶层控件）。 */
static void xwd_restoreSlot(XObject* receiver, XVarList* args)
{
    XWidget* top = receiver ? (XWidget*)XObject_parent(receiver) : NULL;
    (void)args;
    if (top) XWidget_showNormal(top);
}

/** @brief 系统菜单动作槽：最小化。 */
static void xwd_minimizeSlot(XObject* receiver, XVarList* args)
{
    XWidget* top = receiver ? (XWidget*)XObject_parent(receiver) : NULL;
    (void)args;
    {
        XWindowDecorationState* st = top ? xwd_ensureState(top) : NULL;
        /* 与标题条 — 钮同分流：WM 管理下图标化，无 WM 卷起。 */
        if (st) xwd_minimizeAction(st);
    }
}

/** @brief 系统菜单动作槽：最大化（已最大化时还原）。 */
static void xwd_maximizeSlot(XObject* receiver, XVarList* args)
{
    XWidget* top = receiver ? (XWidget*)XObject_parent(receiver) : NULL;
    XWindow* win;
    (void)args;
    if (!top) return;
    win = top->m_windowHandle;
    if (win && (XWindow_windowState(win) == XWindowState_Maximized ||
                XWindow_windowState(win) == XWindowState_FullScreen))
        XWidget_showNormal(top);
    else
        XWidget_showMaximized(top);
}

/** @brief 系统菜单动作槽：关闭（标准 CloseEvent 链，同 WM ✕/WM_DELETE）。 */
static void xwd_closeSlot(XObject* receiver, XVarList* args)
{
    XWidget* top = receiver ? (XWidget*)XObject_parent(receiver) : NULL;
    (void)args;
    if (top) XWidget_close(top);
}

/** @brief 在指定条内局部坐标弹出系统菜单（还原/最小化/最大化/关闭，
 *  按当前状态与提示位灰化；DeleteOnClose 关闭自删，与全库菜单同构）。
 *  提示位/状态驱动的子控件位经条控件 buildOption 取得（原
 *  xwd_subControlsFor 组装逻辑随绘制一并迁入条控件，消除冗余）。 */
static void xwd_openSystemMenu(XWindowDecorationState* st,
                               const XPoint* localPos)
{
    XMenu* menu;
    XAction* action;
    XPoint global;
    XWindowState s;
    XStyleOptionTitleBar opt;
    XTitleBar* bar;
    uint32_t sc;
    if (!st->m_top || !st->m_window) return;
    menu = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, st->m_top, NULL);
    if (!menu) return;
    XWidget_setAttribute((XWidget*)menu, XWidgetAttribute_DeleteOnClose,
                         true);
    s = XWindow_windowState(st->m_window);
    /* 子控件位组装仅对 XTitleBar 派生条执行（xwd_typedBar 守卫）；非派
     * 生条 sc=0：系统菜单仍可弹（还原/关闭按状态可用，最小化/最大化灰
     * 化），等同无按钮的空白条口径。 */
    bar = xwd_typedBar(st);
    sc = (bar && XTitleBar_buildOption(bar, &opt, NULL))
             ? opt.m_base.m_subControls : 0;
    action = XMenu_addAction_2(menu, "还原(&R)");
    if (action) {
        XAction_setEnabled(action, s == XWindowState_Maximized ||
                                       s == XWindowState_FullScreen);
        XObject_connect_1((XObject*)action,
                          XSignal(XAction_triggered_signal),
                          (XObject*)menu, xwd_restoreSlot,
                          XConnectionType_Direct);
    }
    action = XMenu_addAction_2(menu, "最小化(&N)");
    if (action) {
        XAction_setEnabled(action,
                           (sc & XStyleSC_TitleBarMinButton) != 0 &&
                               s != XWindowState_Minimized);
        XObject_connect_1((XObject*)action,
                          XSignal(XAction_triggered_signal),
                          (XObject*)menu, xwd_minimizeSlot,
                          XConnectionType_Direct);
    }
    action = XMenu_addAction_2(menu, "最大化(&O)");
    if (action) {
        XAction_setEnabled(action,
                           (sc & (XStyleSC_TitleBarMaxButton |
                                  XStyleSC_TitleBarNormalButton)) != 0 &&
                               s != XWindowState_Maximized &&
                               s != XWindowState_FullScreen);
        XObject_connect_1((XObject*)action,
                          XSignal(XAction_triggered_signal),
                          (XObject*)menu, xwd_maximizeSlot,
                          XConnectionType_Direct);
    }
    XMenu_addSeparator(menu);
    action = XMenu_addAction_2(menu, "关闭(&C)");
    if (action) {
        XObject_connect_1((XObject*)action,
                          XSignal(XAction_triggered_signal),
                          (XObject*)menu, xwd_closeSlot,
                          XConnectionType_Direct);
    }
    global = XWidget_mapToGlobal(st->m_top, localPos);
    XMenu_popup(menu, &global);
}

#else /* !XMENU_ON */

/** @brief 无菜单模块时系统菜单入口退化为空（按钮也不出现：条控件
 *  buildOption 组装的子控件位在无 XMenu 模块时不含 SysMenu）。 */
static void xwd_openSystemMenu(XWindowDecorationState* st,
                               const XPoint* localPos)
{
    (void)st;
    (void)localPos;
}

#endif /* XMENU_ON */

/* ==================== 内部辅助：按钮动作 ==================== */

/** @brief 标题栏最大化/还原切换（几何切换前先终止拖拽/改尺寸状态机，
 *  见 xwd_endInteractions 的真机教训注）。 */
static void xwd_toggleMaximize(XWindowDecorationState* st)
{
    XWidget* top = st->m_top;
    XWindowState s;
    bool wasZoom;
    xwd_setShade(st, false); /* 展开卷起态，防条态几何进还原基准。 */
    s = XWindow_windowState(st->m_window);
    wasZoom = s == XWindowState_Maximized || s == XWindowState_FullScreen;
    xwd_endInteractions(st);
    if (wasZoom)
        XWidget_showNormal(top);
    else
        XWidget_showMaximized(top);
    xwd_pinBarGeometry(st); /* 最大化/还原改宿主宽→条宽同帧重钉。 */
    if (XWindow_isNativeWindowAttached(st->m_window)) return; /* WM 重铺。 */
    if (wasZoom) {
        /* 复原：窗口缩回，暴露的窗外区域立即铺桌面底色（不能走
         * requestPanelClear——其清零延迟到下次 present 才执行，会把
         * 刚铺的桌面色再次打黑）。 */
        xwd_clearOutsideWindow(st);
    }
    /* 进最大化不清屏：整屏即被全幅重绘覆盖（软件状态路径经
     * handleGeometryChange 回推控件层，resize 链强制整窗更新）。曾在此
     * 调 requestPanelClear——其整段清零延迟到「下一次 present」才执
     * 行，而最大化后的下一次 present 常是 HUD 秒级小区域：整屏清黑
     * 只画小悬浮窗=99.6% 黑屏且纯脏区 present 永不恢复（昆仑通态真
     * 机 2026-09-28 工作流量化定位）。 */
}

/** @brief 触发标题栏按钮动作（松开时在钮内才调用）。 */
static void xwd_activateButton(XWindowDecorationState* st, int sc)
{
    switch (sc) {
    case XStyleSC_TitleBarSysMenu: {
        XRect r;
        XStyle* style = XStyle_defaultStyle();
        XStyleOptionTitleBar opt;
        XTitleBar* bar;
        XPoint pos;
        /* 选项组装仅对 XTitleBar 派生条执行（xwd_typedBar 守卫；本函数
         * 只在 armed 命中按钮后可达，armed 又只能来自经守卫的 hitTest，
         * 此处守卫为纵深防御）。 */
        bar = xwd_typedBar(st);
        if (style && bar && XTitleBar_buildOption(bar, &opt, NULL)) {
            r = XStyle_subControlRect(style, XStyleCC_TitleBar,
                                      &opt.m_base.m_base,
                                      XStyleSC_TitleBarSysMenu, st->m_top);
            XPoint_init(&pos, r.x + r.width / 2, r.y + r.height / 2);
        } else {
            XPoint_init(&pos, 0, 0);
        }
        xwd_openSystemMenu(st, &pos);
        break;
    }
    case XStyleSC_TitleBarMinButton:
        xwd_minimizeAction(st);
        break;
    case XStyleSC_TitleBarMaxButton:
    case XStyleSC_TitleBarNormalButton:
        xwd_toggleMaximize(st);
        break;
    case XStyleSC_TitleBarCloseButton:
        XWidget_close(st->m_top);
        break;
    default:
        break;
    }
}

/* ==================== 公共 API ==================== */

bool XWindowDecoration_activeFor(const XWidget* top)
{
    XWindow* win;
    XWindowFlags flags;
    bool nativeAttached;
    XPlatformThemeDecorationMode mode;
    int type;
    if (!top || !top->m_isWindow) return false;
    /* 句柄未建（显示前的布局期）也能预测：flags 取控件侧副本（建窗
     * 时原样同步），未创建恒视为未挂原生窗——否则 marginsFor 在布局
     * 期返回 0，子控件按零边距排布后 show 再变 24 也不重排（CSD 下
     * autotest 页签点击落空根因）。 */
    win = top->m_windowHandle;
    flags = win ? XWindow_flags(win) : (XWindowFlags)top->m_windowFlags;
    nativeAttached = win ? XWindow_isNativeWindowAttached(win) : false;
    /* 装饰归属问平台策略抽象（原 xwd_forceMode 的 XGUI_CSD 环境变量
     * 解析与优先级逻辑迁入 XPlatformThemeDecoration_effectiveMode，
     * 消除双份事实源）：Framework=强制框架自绘、System=交 WM、
     * Auto=保留 fbdev 探测分支。 */
    mode = XPlatformThemeDecoration_effectiveMode();
    if (mode == XPlatformThemeDecoration_System) return false;
    type = (int)(flags & XWindowType_TypeMask);
    switch (type) {
    case XWindowType_Window:
    case XWindowType_Dialog:
    case XWindowType_Tool:
        break;
    default:
        /* Popup/ToolTip/SplashScreen/Desktop 等瞬态类型永不装饰。 */
        return false;
    }
    if (flags & XWindowType_FramelessWindowHint) return false;
    if (mode == XPlatformThemeDecoration_Framework)
        return true; /* XGUI_CSD=1/运行时 setMode：桌面目验/调试强制自绘。 */
    /* Auto：仅在框架确为显示合成方时接管标题栏：fbdev 显示驱动已注册
     * 并探测成功（真正的无窗口管理器环境）。不能以「无原生窗」作判据
     * ——无显示的测试环境（回归/自测二进制，X11 连接失败回落虚拟
     * winId）同样无原生窗，合成事件直投控件树的用例会被装饰拦截（鼠
     * 标派发三连败教训）；桌面 X11 原生窗交 WM（flags 走
     * _MOTIF_WM_HINTS）。 */
#if XGUI_ON && XPLATFORM_FBDEV_ON
    if (!nativeAttached) {
        const XPlatformDisplayDriverOps* ops = XPlatformDisplayDriver_active();
        XPlatformDisplayInfo info;
        if (ops && ops->probe && ops->probe(&info) &&
            info.m_width > 0 && info.m_height > 0)
            return true;
    }
#endif
    return false;
}

XMargins XWindowDecoration_marginsFor(const XWidget* top)
{
    XMargins margins;
    XMargins_init(&margins, 0, 0, 0, 0);
    if (XWindowDecoration_activeFor(top))
        margins.top = xwd_stripHeight(xwd_stateFor(top));
    return margins;
}

void XWindowDecoration_syncWindow(XWidget* top)
{
    XWindow* win;
    XMargins old;
    XMargins now;
    XWindowDecorationState* st;
    if (!top || !top->m_isWindow) return;
    win = top->m_windowHandle;
    if (!win) return;
    /* CSD 抑制位兜底刷新：建窗前首次预置由 XWidget_createWindow 负责
     * （原生平台窗在其函数体内创建，早于本调用；平台后端首次组装
     * _MOTIF_WM_HINTS 依赖该预置位），此处承接动态 flags 变化
     * （XWidget_setWindowFlags 路径）与自定义条挂载/摘除
     * （XWidget_setTitleBarWidget 路径）后的重算。 */
    XWindow_setCsdFrameSuppressed(win, XWindowDecoration_activeFor(top));
    old = XWindow_frameMargins(win);
    now = XWindowDecoration_marginsFor(top);
    XWindow_setFrameMargins(win, &now);
    if (old.top != now.top || old.left != now.left ||
        old.right != now.right || old.bottom != now.bottom) {
        /* 保留边距变化（如 FramelessWindowHint 动态切换）：条带出现/
           消失区必须整窗重绘，单刷条带会留下旧标题栏残影。 */
        XWidget_update(top);
    }
    /* 承载标题条控件（ensureState 后）：装饰仍激活时确保默认条/自定义
     * 条就位并重钉条区几何；装饰停用（如动态切 FramelessWindowHint/
     * 切系统条模式）时默认条随装饰失活异步释放（先隐藏防残像，条带出/
     * 消失区的重绘由上方保留边距变化分支的整窗 update 承担），状态指针
     * 立即清空；自定义条为借用，只解挂清指针不释放（所有权归创建方，
     * 挂载/摘除语义归 XWidget_setTitleBarWidget）。 */
    if (XWindowDecoration_activeFor(top)) {
        st = xwd_ensureState(top);
        if (st) xwd_ensureBar(st);
    } else {
        st = xwd_stateFor(top);
        if (st) {
            xwd_dropDefaultBar(st);
            st->m_bar = NULL;
        }
    }
}

void XWindowDecoration_syncBarGeometry(XWidget* top)
{
    XWindowDecorationState* st;
    if (!top || !top->m_isWindow) return; /* 非顶层：子控件 resize 热路径单位测试即短路。 */
    if (!XWindowDecoration_activeFor(top)) return; /* 未装饰零开销（env 缓存+类型/flags 判定）。 */
    st = xwd_stateFor(top);
    /* 只重钉既有条，不反向造状态/造条（承载语义归 syncWindow，
     * 见头文件注）；RESIZE 到达时句柄必已建，缺条只可能是状态未登记
     * 或装饰刚失活，静默返回即可。 */
    if (!st || !st->m_bar) return;
    xwd_pinBarGeometry(st); /* 同值幂等：XWidget_setGeometry 短路；变宽随双失效触发条重绘。 */
}

void XWindowDecoration_draw(XWidget* top, XPainter* painter,
                            const XRegion* region)
{
    (void)top;
    (void)painter;
    (void)region;
    /* 兼容空壳：条带绘制已迁为树内标题条控件 XTitleBar 的 paintEvent
     * 自绘（随 XWidget_paintTree 按子控件 Z 序正常绘制，原 CC_TitleBar
     * 样式绘制实现单一事实源迁入该控件），本函数仅保留签名兼容既有
     * 调用点（XWidget_flushBackingStore 尾部 xwidget_drawWindowDecoration），
     * 函数体短路零开销；首帧补拍（原 m_stripKick）语义一并迁入条控件
     * 的 paintEvent 默认实现。 */
}

bool XWindowDecoration_handlePointer(XWidget* top, XEvent* event)
{
    XWindowDecorationState* st;
    XMouseEvent* mouse;
    int barH;
    XPoint pos;
    if (!top || !event || !top->m_isWindow) return false;
    if (!XWindowDecoration_activeFor(top)) return false;
    if (XTitleBar_defaultHeight() <= 0) return false;
    st = xwd_ensureState(top);
    if (!st) return false;
    /* 承载条控件确保（ensureState 后）：每次指针事件先兜底重钉条区几
     * 何（宽度随宿主 resize 自愈，挂点核实见 xwd_pinBarGeometry 注；
     * 同值幂等，常态零开销）。 */
    xwd_ensureBar(st);
    barH = xwd_stripHeight(st); /* 条控件实际高度（自定义条可异高）。 */
    if (barH <= 0) return false;
    switch (XEvent_type(event)) {
    case XEVENT_TYPE_TOUCH_DRAG: {
        const XTouchEvent* drag = (const XTouchEvent*)event;
        /* 触摸按住拖动手势判定（DragBegin）：起点落在条内空白→接受认领，
         * 框架转左键按住拖动仿真（press 续持+MOVE 随行），本装饰既有的
         * 标题拖拽状态机（PRESS 已起拖+显式抓取）直接驱动移窗；用户按钮
         * 上放行（交按钮树）；最大化/全屏禁拖（对标桌面，同 PRESS 臂）。
         * 判定域=手势序列起点（touch 侧保证），越阈指尖滑出条区不误判。
         * 其余手势种类（Tap/DoubleTap/LongPress 通知）不认领。 */
        if (XTouchEvent_gesture(drag) != (int)XTouchGesture_DragBegin)
            return false;
        pos = drag->m_position;
        if (pos.y < 0 || pos.y >= barH) return false;
        if (xwd_isBarChild(st, XWidget_childAt(top, &pos))) return false;
        if (XWindow_windowState(st->m_window) == XWindowState_Maximized ||
            XWindow_windowState(st->m_window) == XWindowState_FullScreen)
            return false;
        XEvent_accept(event);
        return true;
    }
    case XEVENT_TYPE_WHEEL: {
        XWheelEvent* wheel = (XWheelEvent*)event;
        /* 输入三分流（条带内先 childAt）：命中条控件子控件（用户按钮
         * /自定滚动区）放行树派发；条内空白吞掉（对标 WM：标题栏不响
         * 应滚轮，也不漏给条下控件）。默认条无子控件，全部吞掉与历史
         * 口径等价。 */
        if (wheel->m_position.y < 0 || wheel->m_position.y >= barH)
            return false;
        if (xwd_isBarChild(st, XWidget_childAt(top, &wheel->m_position)))
            return false;
        return true;
    }
    case XEVENT_TYPE_CONTEXT_MENU: {
        XContextMenuEvent* ctx = (XContextMenuEvent*)event;
        if (ctx->m_position.y < 0 || ctx->m_position.y >= barH) return false;
        /* 右键落在用户按钮上：放行交按钮自身上下文菜单语义。 */
        if (xwd_isBarChild(st, XWidget_childAt(top, &ctx->m_position)))
            return false;
        XPoint_init(&pos, ctx->m_position.x, ctx->m_position.y);
        xwd_openSystemMenu(st, &pos);
        XEvent_accept(event);
        return true;
    }
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
        mouse = (XMouseEvent*)event;
        pos = XMouseEvent_position(mouse);
        if (XMouseEvent_button(mouse) == XMouseButton_LeftButton) {
            int sc;
            /* 新按压=新手势：先清上一手势残留（松开事件在注入/裁剪边
               界偶发丢失时，m_resizing/m_dragging 卡真会吞掉后续全部
               标题拖拽而改尺寸假活——真机第二轮工作流实测）。 */
            if (st->m_dragging || st->m_resizing) {
                st->m_dragging = false;
                st->m_resizing = false;
                XPlatformNativeWindow_deferGeometry(st->m_window, false);
                xwd_dragGestureEnd(st, false); /* 残手势快照释放（fb 已
                                                * 逐步收敛，无需结算）。 */
                XWidget_releaseMouse(top);
                xwd_platformGrab(st, false);
            }
            /* 边缘改尺寸带（四边+四角）先于一切条带命中判定：桌面 WM
             * 的改尺寸框在帧外、恒胜标题栏按钮与内容命中——CSD 带若让
             * 位于条子控件/装饰按钮，N 向角带（W+N/E+N）与 min/close
             * 按钮几何重叠处按钮先赢，角点拉伸永不可用（真机实测：左
             * 上/右上角按下变按钮按压）。原生窗（桌面 WM 口径）zone 非
             * 零直接接管；无原生窗（fbdev 直写触屏）保持既有顺序——
             * 24px 触屏带宽侵入内容区，条子控件/内容可点优先，仅空白区
             * 接管（带与按钮重叠处触屏用户点按钮意图优先，原口径保
             * 留）。增量锚取全局坐标：本地系随窗口移动自指（见拖拽处
             * 注释）。
             * 例外（2026-10-03 带内死区修复）：命中声明了
             * XWidgetAttribute_HitEdgeBand 的贴边交互件（分割条细条/
             * 把手类）时让位树派发——贴边停靠控件在 WM parity 带内曾
             * 永不可点；未声明属性的内容区维持接管口径不变（门禁
             * resize_follow_gate 的 E/NE 带拖拽锚定依赖该口径）。 */
            {
                int zone = xwd_resizeZoneAt(top, &pos, barH);
                if (zone && XWindow_isNativeWindowAttached(st->m_window)) {
                    XWidget* hitChild = XWidget_childAt(top, &pos);
                    if (hitChild && hitChild != (XWidget*)top &&
                        hitChild != st->m_bar &&
                        XWidget_testAttribute(hitChild,
                                              XWidgetAttribute_HitEdgeBand))
                        return false; /* 贴边交互件优先，放行树派发。 */
                    st->m_resizing = true;
                    st->m_resizeMask = zone;
                    st->m_resizeAnchor =
                        XMouseEvent_globalPosition(mouse);
                    st->m_resizeGeometry = XWidget_geometry(top);
                    /* 几何挂起至 present 批内落地：拖拽每步若立即
                       XClassMoveResizeWindow，服务器按 background_pixel=0
                       （ForgetGravity 口径整窗）当场填黑扩区，盖黑帧要
                       等下一轮 PAINT——桌面 X11 四方位同源的单帧黑闪
                       根因；挂起后几何与整窗内容同一请求批生效，黑态
                       无窗口期。 */
                    XPlatformNativeWindow_deferGeometry(st->m_window, true);
                    /* 节流戳不清零（全局连续间隔）：幻触 PRESS 清戳
                       会打穿限帧闸，见 xwd_resizeThrottleSkip 注。 */
                    XWidget_grabMouse(top);
                    xwd_platformGrab(st, true);
                    XEvent_accept(event);
                    return true;
                }
            }
            /* 三分流（fbdev 口径及条内非边缘区）：条带内命中条控件子
             * 控件（用户按钮）放行树派发（子控件优先）。 */
            if (pos.y >= 0 && pos.y < barH &&
                xwd_isBarChild(st, XWidget_childAt(top, &pos)))
                return false;
            if (pos.y >= 0 && pos.y < barH)
                XWindow_requestActivate(st->m_window); /* 点条亦移焦。 */
            /* 按钮几何只存在于条带内，条外 hitTest 自然落空。 */
            sc = xwd_hitTest(st, &pos);
            if (xwd_isButtonSc(sc)) {
                if (st->m_armed != sc) {
                    st->m_armed = sc;
                    st->m_hot = sc;
                    xwd_repaintStrip(top);
                }
                XEvent_accept(event);
                return true;
            }
            /* fbdev 双口径残余：边缘带命中内容子控件时放行（上方原生
             * 窗分支已接管全部 zone；此处仅无原生窗且条外贴边命中）。 */
            {
                int zone = xwd_resizeZoneAt(top, &pos, barH);
                if (zone) {
                    XWidget* hitChild = XWidget_childAt(top, &pos);
                    bool childTakes = hitChild && hitChild != (XWidget*)top &&
                                      hitChild != st->m_bar;
                    if (childTakes)
                        return false; /* 边缘命中子控件（fbdev 口径，条内外一致放行）。 */
                    st->m_resizing = true;
                    st->m_resizeMask = zone;
                    st->m_resizeAnchor =
                        XMouseEvent_globalPosition(mouse);
                    st->m_resizeGeometry = XWidget_geometry(top);
                    XWidget_grabMouse(top);
                    xwd_platformGrab(st, true);
                    XEvent_accept(event);
                    return true;
                }
            }
            /* 条内空白区：开始拖拽移动（须显式抓取鼠标——框架对 MOVE
               逐事件重命中，斜向拖出条区即断流，真机教训；最大化/全
               屏禁拖，对标桌面）。增量锚取全局坐标：窗口本地系会随窗
               口自身移动而平移，按本地增量拖拽自指减半（Xvfb 目验实
               测 +200 指针只走 +100，dx_k=d−dx_{k−1} 交替归零）。 */
            if (pos.y >= 0 && pos.y < barH) {
                if (XWindow_windowState(st->m_window) !=
                        XWindowState_Maximized &&
                    XWindow_windowState(st->m_window) !=
                        XWindowState_FullScreen) {
                    st->m_dragging = true;
                    st->m_dragLast = XMouseEvent_globalPosition(mouse);
                    xwd_dragGestureBegin(st); /* 探针开工+快照预备（条件
                                               * 不满足整手势走旧路径）。 */
                    /* 节流戳不清零，同改尺寸臂（幻触免役）。 */
                    XWidget_grabMouse(top);
                        xwd_platformGrab(st, true);
                }
                XEvent_accept(event);
                return true;
            }
            return false; /* 条外非边缘：交控件树。 */
        }
        if (XMouseEvent_button(mouse) == XMouseButton_RightButton) {
            if (pos.y < 0 || pos.y >= barH) return false;
            if (xwd_isBarChild(st, XWidget_childAt(top, &pos)))
                return false;
            xwd_openSystemMenu(st, &pos);
            XEvent_accept(event);
            return true;
        }
        if (pos.y >= 0 && pos.y < barH) {
            if (xwd_isBarChild(st, XWidget_childAt(top, &pos)))
                return false; /* 其余按键落用户按钮：放行。 */
            XEvent_accept(event); /* 条内其余按键吞掉。 */
            return true;
        }
        return false;
    case XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK: {
        mouse = (XMouseEvent*)event;
        pos = XMouseEvent_position(mouse);
        if (pos.y < 0 || pos.y >= barH) return false;
        /* 双击落在用户按钮上：放行交按钮自身双击语义。 */
        if (xwd_isBarChild(st, XWidget_childAt(top, &pos))) return false;
        /* 双击条内空白区切换最大化/还原（WM 标题栏双击语义）；按钮上
           双击维持无动作。 */
        if (XMouseEvent_button(mouse) == XMouseButton_LeftButton &&
            !xwd_isButtonSc(xwd_hitTest(st, &pos)))
            xwd_toggleMaximize(st);
        XEvent_accept(event);
        return true;
    }
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
        mouse = (XMouseEvent*)event;
        pos = XMouseEvent_position(mouse);
#if XGUI_PRESENT_MAX_FPS > 0
        if (st->m_dragging) {
            /* 尾帧必达（限帧零丢失收尾）：限帧闸跳过的移动由本补齐——
               release 自带全局坐标，零新增状态；未跳帧时增量恒 0，
               applyMove 头部空增量早退，幂等零开销。 */
            XPoint rg = XMouseEvent_globalPosition(mouse);
            xwd_applyMove(st, rg.x - st->m_dragLast.x,
                          rg.y - st->m_dragLast.y);
        }
#endif
#if XGUI_RESIZE_REPAINT_MAX_FPS > 0
        if (st->m_resizing) {
            /* 尾帧必达（限帧零丢失收尾）：限帧闸跳过的落地由本补齐——
               release 自带全局坐标，零新增状态；未跳帧时与末次 MOVE 同
               增量（几何恒由按下锚+总位移重算），applyResize 同值早退，
               幂等零开销。 */
            XPoint rg = XMouseEvent_globalPosition(mouse);
            xwd_applyResize(st, rg.x - st->m_resizeAnchor.x,
                            rg.y - st->m_resizeAnchor.y);
        }
#endif
        if (st->m_dragging) {
            st->m_dragging = false;
            XWidget_releaseMouse(top);
            xwd_platformGrab(st, false);
            xwd_clearOutsideWindow(st);
            /* 快照释放 + 真实落定（红线）：整窗 PAINT→paintTree→flush→
               present 收尾——差带账本把拖动期直写可见面的行带补进后台
               缓冲并恢复轮换翻页，条带清扫与快照帧在结算帧收口。 */
            xwd_dragGestureEnd(st, true);
            XEvent_accept(event);
            return true;
        }
        if (st->m_resizing) {
            st->m_resizing = false;
            /* 尾帧挂起几何由松手后的整窗 PAINT present 消费（release 末次
               applyResize 已重挂同值），此处仅恢复立即落窗。 */
            XPlatformNativeWindow_deferGeometry(st->m_window, false);
            XWidget_releaseMouse(top);
            xwd_platformGrab(st, false);
            xwd_clearOutsideWindow(st);
            XEvent_accept(event);
            return true;
        }
        if (st->m_armed) {
            int armed = st->m_armed;
            int sc = xwd_hitTest(st, &pos);
            st->m_armed = 0;
            xwd_repaintStrip(top);
            XEvent_accept(event);
            if (sc == armed) xwd_activateButton(st, armed);
            return true;
        }
        if (pos.y >= 0 && pos.y < barH) {
            /* 无主释放：落条控件子控件必须放行——用户按钮的 press 已
               由树派发，release 不可达会让按钮卡在按住态。 */
            if (xwd_isBarChild(st, XWidget_childAt(top, &pos)))
                return false;
            XEvent_accept(event); /* 条内无主释放吞掉。 */
            return true;
        }
        return false;
    case XEVENT_TYPE_MOUSE_MOVE:
        mouse = (XMouseEvent*)event;
        pos = XMouseEvent_position(mouse);
        if (st->m_resizing) {
            /* 全局坐标口径：增量=距按下点总位移（本地系随窗口移动自
               指，见 PRESS 处注释）。 */
            XPoint g = XMouseEvent_globalPosition(mouse);
#if XGUI_RESIZE_REPAINT_MAX_FPS > 0
            /* 改尺寸限帧整闸（默认 60fps，见 XGuiConfig.h 与本函数
               xwd_resizeThrottleSkip 注）：跳过的中间位移由「按下锚+
               总位移」无状态重算天然合并，RELEASE 补尾帧零丢失。 */
            if (xwd_resizeThrottleSkip(st)) {
                XEvent_accept(event);
                return true;
            }
#endif
            xwd_applyResize(st, g.x - st->m_resizeAnchor.x,
                            g.y - st->m_resizeAnchor.y);
            XEvent_accept(event);
            return true;
        }
        if (st->m_dragging) {
            XPoint g = XMouseEvent_globalPosition(mouse);
#if XGUI_RESIZE_REPAINT_MAX_FPS > 0
            /* 拖拽移动直提限频闸（默认 60fps，用户裁定移动同归手势宏；
               见 xwd_moveThrottleSkip 注）：跳帧时不同步推进 m_dragLast
               （增量逐事件累计，下一放行帧一次搬到位），RELEASE 补尾帧
               零丢失。 */
            if (xwd_moveThrottleSkip(st)) {
                XEvent_accept(event);
                return true;
            }
#endif
            {
                int dx = g.x - st->m_dragLast.x;
                int dy = g.y - st->m_dragLast.y;
                if (dx || dy)
                {
                    xwd_applyMove(st, dx, dy);
                }
                st->m_dragLast = g;
            }
            XEvent_accept(event);
            return true;
        }
        /* 边缘改尺寸光标反馈先于按钮放行：press 臂按桌面 WM 口径让角/
         * 边带恒胜按钮命中，光标必须同口径——悬停进角带（含按钮几何重
         * 叠区）即切尺寸形状，否则 press 是缩放而光标仍是箭头的反馈错
         * 位（真机实测左上/右上角）。CSD 无原生帧，装饰层自补（原生窗
         * 口径；fbdev 无系统光标内部早退）。 */
        xwd_updateEdgeCursor(st, xwd_resizeZoneAt(top, &pos, barH));
        /* 三分流（MOVE）：移入/移经用户按钮放行，让按钮收到自身 MOVE
         *（按住态跟随/悬停效果）；装饰侧悬停/武装位同步清零防残像。 */
        if (pos.y >= 0 && pos.y < barH &&
            xwd_isBarChild(st, XWidget_childAt(top, &pos))) {
            if (st->m_armed || st->m_hot) {
                st->m_armed = 0;
                st->m_hot = 0;
                xwd_repaintStrip(top);
            }
            return false;
        }
        if (!(XMouseEvent_buttons(mouse) &
              (XMouseButton_LeftButton | XMouseButton_RightButton |
               XMouseButton_MiddleButton))) {
            /* 悬停热跟踪：钮形翻底色；按住后移出钮外解除武装（松开不
               再触发，桌面按钮同语义）。 */
            int hot = pos.y >= 0 && pos.y < barH ? xwd_hitTest(st, &pos) : 0;
            if (!xwd_isButtonSc(hot)) hot = 0;
            if (hot != st->m_hot) {
                st->m_hot = hot;
                xwd_repaintStrip(top);
            }
            if (st->m_armed && st->m_armed != hot) {
                st->m_armed = 0;
                xwd_repaintStrip(top);
            }
        }
        /* 条内移动吞掉，防悬停/滚轮语义漏给条下子控件。 */
        return pos.y >= 0 && pos.y < barH;
    default:
        return false;
    }
}

void XWindowDecoration_handleLeave(XWidget* top)
{
    XWindowDecorationState* st;
    if (!top) return;
    st = xwd_stateFor(top);
    if (st && st->m_hot) {
        st->m_hot = 0;
        xwd_repaintStrip(top);
    }
}

void XWindowDecoration_notifyWindowDestroyed(XWindow* win)
{
    int i;
    if (!win) return;
    for (i = 0; i < g_xwdCount; ++i) {
        XWidget* top = g_xwdStates[i].m_top;
        if (g_xwdStates[i].m_window == win ||
            (top && top->m_windowHandle == win)) {
            /* 宿主注销清理：本模块创建的默认条随装饰状态一并异步释放
             * （XObject_deleteLater；顶层控件未随窗口一起销毁时条控件
             * 不能残留叠画，已销毁则经析构撤销挂起事件，无双重释放）：
             * 注册表项随即摘除，之后若同顶层重建窗口，ensureBar 会重建
             * 新条。自定义条为借用，条所有权归创建方，此处只随状态丢弃
             * 借用指针。 */
            xwd_dropDefaultBar(&g_xwdStates[i]);
            /* 拖动快照随宿主注销释放（若手势中被销毁）：快照模式未启用
             * 时零开销，探针一并收口。 */
            xwd_dragGestureEnd(&g_xwdStates[i], false);
            /* 尾交换摘除（注册表无序，O(1) 删除）。 */
            g_xwdStates[i] = g_xwdStates[g_xwdCount - 1];
            --g_xwdCount;
            return;
        }
    }
}

void XWindowDecoration_notifyTopDestroyed(XWidget* top)
{
    int i;
    if (!top) return;
    for (i = 0; i < g_xwdCount; ++i) {
        if (g_xwdStates[i].m_top == top) {
            /* 控件先于其窗口消亡的摘项路径（VXWidget_deinit 调用，见
               XWindowDecoration_Protected.h）：默认条 deleteLater 契约
               同 notifyWindowDestroyed——子树析构随后撤销挂起事件，无
               双重释放；借用自定义条只随状态丢弃指针。 */
            xwd_dropDefaultBar(&g_xwdStates[i]);
            /* 拖动快照随顶层消亡释放（快照模式未启用时零开销）。 */
            xwd_dragGestureEnd(&g_xwdStates[i], false);
            /* 尾交换摘除（注册表无序，O(1) 删除）。 */
            g_xwdStates[i] = g_xwdStates[g_xwdCount - 1];
            --g_xwdCount;
            if (g_xwdCount == 0) {
                /* 全部摘除后释放块并复位，避免峰值容量长期占住；
                 * 置空指针防悬挂（ensureState 按 NULL 重新懒分配）。 */
                XMemory_free(g_xwdStates, XCLASS_DEFAULT_MEMORY_TYPE);
                g_xwdStates = NULL;
                g_xwdCapacity = 0;
            }
            return;
        }
    }
}

void XWindowDecoration_notifyActivation(XWindow* win)
{
    XWidget* top = xwd_topForWindow(win);
    if (top && XWindowDecoration_activeFor(top)) xwd_repaintStrip(top);
}

void XWindowDecoration_notifyAppearanceChanged(XWidget* top)
{
    if (!top) return;
    if (XWindowDecoration_activeFor(top)) xwd_repaintStrip(top);
}

#endif /* XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON */
