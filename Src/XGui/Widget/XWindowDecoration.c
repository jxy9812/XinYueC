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
#include "XWindow_Protected.h"
#include "XPlatformTheme.h"
#include "XPlatformNativeWindow.h" /* 拖拽平台指针抓取：指针出窗仍持续投递 MOVE（软件重路由的出窗盲区兜底） */
#include "XDateTime.h" /* 交互限帧计时：单调毫秒（时间源约束同 XWidget.c present 限频） */
#include "XTitleBar.h"
#include "XTitleBar_Protected.h"
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
 *  2026-09-28 教训：6px→24px）；带与内容控件重叠无冲突——改尺寸只在
 *  无子控件接住按下的空白区接管（子控件优先，等价桌面 WM 帧外命中）。 */
#define XWD_RESIZE_ZONE 24
/** @brief 顶边改尺寸命中带宽度（窄带：标题栏大部分区域归拖拽移动）。 */
#define XWD_RESIZE_ZONE_N 8

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
} XWindowDecorationState;

/** @brief 装饰状态注册表（懒扩容；容量以 2 的幂增长）。 */
static XWindowDecorationState* g_xwdStates = NULL;
static int g_xwdCount = 0;
static int g_xwdCapacity = 0;

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
 *  同步 XTitleBar_delete_base 直调——条控件可能仍被事件派发/绘制队列
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

/** @brief 拖拽/改尺寸结束的兜底清扫：窗口外全部面板区域填黑两缓冲
 *  （逐帧差带只覆盖增量；松手一次清干净=桌面 WM 每次几何变化整屏重铺）。 */
static void xwd_clearOutsideWindow(XWindowDecorationState* st)
{
    XRect panel;
    XRect g;
    XRect bands[4];
    if (!xwd_panelRect(st, &panel)) return;
    g = XWidget_geometry(st->m_top);
    XRect_init(&bands[0], panel.x, panel.y, g.x - panel.x, panel.height);
    XRect_init(&bands[1], g.x + g.width, panel.y,
               panel.x + panel.width - (g.x + g.width), panel.height);
    XRect_init(&bands[2], g.x, panel.y, g.width, g.y - panel.y);
    XRect_init(&bands[3], g.x, g.y + g.height, g.width,
               panel.y + panel.height - (g.y + g.height));
    XPlatformBackingStore_fillPanelRects(bands, 4, XWD_DESKTOP_PIXEL);
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
 *  备缓冲整窗直提（纯移动免重绘，见函数尾注）。 */
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
    if (stripCount > 0)
        XPlatformBackingStore_fillPanelRects(exposed, stripCount,
                                       XWD_DESKTOP_PIXEL);
    XWidget_setGeometry(top, nx, ny, g.width, g.height);
    xwd_pinBarGeometry(st); /* 纯移动不改宿主宽，重钉同值幂等零开销。 */
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
    if (stripCount > 0)
        XPlatformBackingStore_fillPanelRects(exposed, stripCount,
                                       XWD_DESKTOP_PIXEL);
    XWidget_setGeometry(top, newG.x, newG.y, newG.width, newG.height);
    xwd_pinBarGeometry(st); /* 宿主宽变化→条宽同步重钉（挂点核实见该函数注）。 */
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
             * 界偶发丢失时，m_resizing/m_dragging 卡真会吞掉后续全部
             * 标题拖拽而改尺寸假活——真机第二轮工作流实测）。 */
            if (st->m_dragging || st->m_resizing) {
                st->m_dragging = false;
                st->m_resizing = false;
                XWidget_releaseMouse(top);
                xwd_platformGrab(st, false);
            }
            /* 三分流第一路：条带内命中条控件子控件（用户按钮）放行树
             * 派发（子控件优先，与桌面 WM 帧外命中等效）。 */
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
            /* 边缘改尺寸带（四边+四角；空白区才接管——命中子控件时放
             * 行，子控件优先与桌面 WM 帧外命中等效，demo 真机口径；
             * 条控件自身视作空白——它是装饰承载物而非内容子控件，N 向
             * 窄带照常接管）。增量锚取全局坐标：本地系随窗口移动自指
             * （见拖拽处注释）。 */
            {
                int zone = xwd_resizeZoneAt(top, &pos, barH);
                if (zone) {
                    XWidget* hitChild = XWidget_childAt(top, &pos);
                    if (!hitChild || hitChild == (XWidget*)top ||
                        hitChild == st->m_bar) {
                        st->m_resizing = true;
                        st->m_resizeMask = zone;
                        st->m_resizeAnchor =
                            XMouseEvent_globalPosition(mouse);
                        st->m_resizeGeometry = XWidget_geometry(top);
                        /* 节流戳不清零（全局连续间隔）：幻触 PRESS 清戳
                           会打穿限帧闸，见 xwd_resizeThrottleSkip 注。 */
                        XWidget_grabMouse(top);
                        xwd_platformGrab(st, true);
                        XEvent_accept(event);
                        return true;
                    }
                    if (pos.y >= barH) return false; /* 边缘命中子控件。 */
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
            XEvent_accept(event);
            return true;
        }
        if (st->m_resizing) {
            st->m_resizing = false;
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
