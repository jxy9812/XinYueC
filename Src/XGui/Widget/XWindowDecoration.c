/**
 * @file       XWindowDecoration.c
 * @brief      框架级窗口装饰实现（自绘系统标题栏，与桌面 WM 标题栏同语义）。
 * @details    与同名头文件的公共 API 一一对应。输入拦截先于控件命中
 *             （XWidget_dispatchPointerEvent 头部调用），绘制在顶层子
 *             树之后（XWidget_flushBackingStore 尾部调用）；几何操作
 *             只走 XWidget/XWindow 公共 API，无平台 API 残留。
 * @author     XinYueC 团队
 */

#include "XWindowDecoration.h"
#include "XMemory.h"
#include "XStringUtils.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XGuiApplication.h"
#include "XScreen.h"
#include "XPlatformBackingStore.h"
#include "XSystem.h"
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
    int m_armed;           /**< 按住中的按钮子控件位（XStyleSC_TitleBar*；0 无）。 */
    int m_hot;             /**< 悬停中的按钮子控件位（0 无）。 */
    bool m_dragging;       /**< 标题栏拖拽移动进行中。 */
    XPoint m_dragLast;     /**< 拖拽上一采样点（全局坐标；窗口自身移动
                                不改变全局系，增量才不自指）。 */
    bool m_resizing;       /**< 边缘改尺寸进行中。 */
    bool m_shaded;         /**< 最小化=卷起态（窗口只剩标题条）。 */
    bool m_stripKick;      /**< 首帧装饰补拍已排（见 draw 尾注）。 */
    XRect m_preShadeGeometry; /**< 卷起前的完整几何（展开恢复）。 */
    int m_resizeMask;      /**< 改尺寸方向位掩码（XWD_RZ_* 组合）。 */
    XPoint m_resizeAnchor; /**< 按下点（全局坐标；改尺寸增量=距按下点
                                总位移）。 */
    XRect m_resizeGeometry;/**< 按下时窗口几何（改尺寸基准）。 */
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

/* ==================== 内部辅助：判定与度量 ==================== */

/** @brief XGUI_CSD 环境变量解析（读一次缓存）：1 强制开/0 强制关/其余自动。 */
static int xwd_forceMode(void)
{
    static int cached = -2; /* -2=未读取，-1=自动。 */
    if (cached == -2) {
        const char* env = XSystem_environment("XGUI_CSD");
        cached = -1;
        if (env && env[0] && !env[1]) {
            if (env[0] == '1') cached = 1;
            else if (env[0] == '0') cached = 0;
        }
    }
    return cached;
}

/** @brief 标题栏条高（样式 PM 度量；钳到容得下按钮的最小高度）。 */
static int xwd_titleBarHeight(void)
{
    XStyle* style = XStyle_defaultStyle();
    int barH;
    int btn;
    int need;
    if (!style) return 0;
    barH = XStyle_pixelMetric(style, XStylePM_TitleBarHeight, NULL);
    btn = XStyle_pixelMetric(style, XStylePM_TitleBarButtonSize, NULL);
    need = btn + 4;
    if (barH < need) barH = need;
    if (barH < 8) barH = 8;
    return barH;
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

/** @brief 按窗口 flags 组装标题栏子控件位（对齐 Qt 窗口提示语义）。 */
static uint32_t xwd_subControlsFor(const XWindow* win)
{
    XWindowFlags flags = XWindow_flags(win);
    uint32_t sc = 0;
    bool maxAllowed;
    if (flags & XWindowType_CustomizeWindowHint) {
        /* 定制外观：只认显式给出的提示位。 */
        if (flags & XWindowType_WindowTitleHint)
            sc |= XStyleSC_TitleBarLabel;
        if (flags & XWindowType_WindowSystemMenuHint)
            sc |= XStyleSC_TitleBarSysMenu;
        if (flags & XWindowType_WindowMinimizeButtonHint)
            sc |= XStyleSC_TitleBarMinButton;
        if (flags & XWindowType_WindowMaximizeButtonHint)
            sc |= XStyleSC_TitleBarMaxButton;
        if (flags & XWindowType_WindowCloseButtonHint)
            sc |= XStyleSC_TitleBarCloseButton;
    } else {
        /* 未定制时对齐 Qt 默认：标题 + 系统菜单 + 最小/最大/关闭。 */
        sc = XStyleSC_TitleBarLabel | XStyleSC_TitleBarSysMenu |
             XStyleSC_TitleBarMinButton | XStyleSC_TitleBarMaxButton |
             XStyleSC_TitleBarCloseButton;
    }
#if !XMENU_ON
    sc &= ~(uint32_t)XStyleSC_TitleBarSysMenu;
#endif
    /* 注：不做「定尺寸窗剥 □」——min>=max 判定在窗口几何动态变化的
     * 无 WM 环境下不可靠（改尺寸后曾误剥 □ 致按钮失联+菜单项灰化，
     * 昆仑通态真机第二轮工作流实证）；最大化对定尺寸窗表现为几何钳
     * 制（软件路径 setGeometry 同值幂等），无副作用。 */
    /* 状态驱动：最大化/全屏时 □ 形换还原形（同一位，样式按位选形）。 */
    maxAllowed = (sc & (XStyleSC_TitleBarMaxButton |
                        XStyleSC_TitleBarNormalButton)) != 0;
    if (maxAllowed) {
        XWindowState s = XWindow_windowState(win);
        sc &= ~(uint32_t)(XStyleSC_TitleBarMaxButton |
                          XStyleSC_TitleBarNormalButton);
        if (s == XWindowState_Maximized || s == XWindowState_FullScreen)
            sc |= XStyleSC_TitleBarNormalButton;
        else
            sc |= XStyleSC_TitleBarMaxButton;
    }
    return sc;
}

/** @brief 组装 CC_TitleBar 选项（绘制/命中共用，一次事实源）。
 *  @param outBarH 输出条高；可为 NULL。
 *  @return 顶层可装饰返回 true（选项已填充）。 */
static bool xwd_buildOption(XWidget* top, XStyleOptionTitleBar* opt,
                            int* outBarH)
{
    XWindow* win;
    int barH;
    uint32_t state;
    const XString* title;
    if (!top || !opt || !top->m_isWindow) return false;
    win = top->m_windowHandle;
    if (!win || !XWindowDecoration_activeFor(top)) return false;
    barH = xwd_titleBarHeight();
    if (barH <= 0) return false;
    XStyleOptionTitleBar_init(opt, XStyleCC_TitleBar);
    XRect_init(&opt->m_base.m_base.m_rect, 0, 0, XWidget_width(top), barH);
    opt->m_base.m_base.m_palette = XWidget_palette(top);
    state = XStyleState_Enabled;
    /* 活动窗口配色：焦点窗口即活动（对标 WM 活动标题栏着色）。 */
    if (XGuiApplication_focusWindow() == win)
        state |= XStyleState_Active;
    opt->m_base.m_subControls = xwd_subControlsFor(win);
    opt->m_base.m_base.m_text = NULL;
    title = XWidget_windowTitle(top);
    if (title) opt->m_base.m_base.m_text = XString_toUtf8(title);
    if (!XIcon_isNull(&top->m_icon))
        opt->m_base.m_base.m_icon = &top->m_icon;
    opt->m_titleBarState = (uint32_t)XWindow_windowState(win);
    opt->m_titleBarFlags = (uint32_t)XWindow_flags(win);
    {
        XWindowDecorationState* st = xwd_stateFor(top);
        if (st) {
            opt->m_base.m_activeSubControls =
                (uint32_t)(st->m_armed ? st->m_armed : st->m_hot);
            if (st->m_hot) state |= XStyleState_MouseOver;
            if (st->m_armed) state |= XStyleState_Sunken;
        }
    }
    opt->m_base.m_base.m_state = state;
    if (outBarH) *outBarH = barH;
    return true;
}

/** @brief 标题栏命中测试（样式 hitTestComplexControl，条内空白=
 *  SC_TitleBarLabel/None，按钮返回对应子控件位）。 */
static int xwd_hitTest(XWidget* top, const XPoint* pos)
{
    XStyle* style = XStyle_defaultStyle();
    XStyleOptionTitleBar opt;
    if (!style || !xwd_buildOption(top, &opt, NULL)) return 0;
    return XStyle_hitTestComplexControl(style, XStyleCC_TitleBar,
                                        &opt.m_base.m_base, pos->x, pos->y,
                                        top);
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
    w = XWidget_width(top);
    h = XWidget_height(top);
    if (pos->x < XWD_RESIZE_ZONE) zone |= XWD_RZ_W;
    else if (pos->x >= w - XWD_RESIZE_ZONE) zone |= XWD_RZ_E;
    if (pos->y >= barH) {
        if (pos->y >= h - XWD_RESIZE_ZONE) zone |= XWD_RZ_S;
    } else {
        if (pos->y < XWD_RESIZE_ZONE_N) zone |= XWD_RZ_N;
    }
    return zone;
}

/* ==================== 内部辅助：重绘与几何 ==================== */

/** @brief 重绘标题栏条带（轻量脏区）。 */
static void xwd_repaintStrip(XWidget* top)
{
    XRegion region;
    XRect strip;
    int barH = XWindowDecoration_marginsFor(top).top;
    if (barH <= 0) return;
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

/** @brief 终止拖拽/改尺寸状态机并解除鼠标抓取。
 *  真机教训（昆仑通态 2026-09-28）：双击序列 DBL_CLICK 在按住中途到
 *  来，抓取与增量锚若穿越几何切换，后续 MOVE 按旧锚点产生伪增量把窗
 *  口平移到非预期位置——任何几何切换前必须先走这里。 */
static void xwd_endInteractions(XWindowDecorationState* st)
{
    if (st->m_dragging || st->m_resizing)
        XWidget_releaseMouse(st->m_top);
    st->m_dragging = false;
    st->m_resizing = false;
    st->m_armed = 0;
    st->m_hot = 0;
}

/** @brief 最小化=卷起/展开（Shade）：嵌入式无 shell/任务栏，「最小化」
 *  旧 demo 仅按压反馈（如实降级）。现做成真行为：卷起=窗口收起只剩
 *  标题条（高度=条高），展开=恢复原几何；再点 — 或系统菜单「最小化」
 *  往返切换。卷起暴露的下方区域铺桌面底色（同复原路径，禁
 *  requestPanelClear——延迟清零会打黑刚铺的桌面色）。 */
static void xwd_setShade(XWindowDecorationState* st, bool on)
{
    XRect g;
    if (!st || !st->m_top || on == st->m_shaded) return;
    xwd_endInteractions(st);
    if (on) {
        g = XWidget_geometry(st->m_top);
        if (g.height <= xwd_titleBarHeight()) return; /* 已是条态，防重入。 */
        st->m_preShadeGeometry = g;
        st->m_shaded = true;
        XWidget_setGeometry(st->m_top, g.x, g.y, g.width,
                            xwd_titleBarHeight());
        xwd_clearOutsideWindow(st);
        XWidget_repaint(st->m_top);
    } else {
        st->m_shaded = false;
        XWidget_setGeometryRect(st->m_top, &st->m_preShadeGeometry);
        XWidget_repaint(st->m_top);
    }
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
        if (st) xwd_setShade(st, !st->m_shaded);
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
 *  按当前状态与提示位灰化；DeleteOnClose 关闭自删，与全库菜单同构）。 */
static void xwd_openSystemMenu(XWindowDecorationState* st,
                               const XPoint* localPos)
{
    XMenu* menu;
    XAction* action;
    XPoint global;
    XWindowState s;
    uint32_t sc;
    if (!st->m_top || !st->m_window) return;
    menu = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, st->m_top, NULL);
    if (!menu) return;
    XWidget_setAttribute((XWidget*)menu, XWidgetAttribute_DeleteOnClose,
                         true);
    s = XWindow_windowState(st->m_window);
    sc = xwd_subControlsFor(st->m_window);
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

/** @brief 无菜单模块时系统菜单入口退化为空（按钮也不出现，见
 *  xwd_subControlsFor 的 SysMenu 收窄）。 */
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
        XPoint pos;
        if (style && xwd_buildOption(st->m_top, &opt, NULL)) {
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
        xwd_setShade(st, !st->m_shaded);
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
    int force;
    int type;
    if (!top || !top->m_isWindow) return false;
    /* 句柄未建（显示前的布局期）也能预测：flags 取控件侧副本（建窗
     * 时原样同步），未创建恒视为未挂原生窗——否则 marginsFor 在布局
     * 期返回 0，子控件按零边距排布后 show 再变 24 也不重排（CSD 下
     * autotest 页签点击落空根因）。 */
    win = top->m_windowHandle;
    flags = win ? XWindow_flags(win) : (XWindowFlags)top->m_windowFlags;
    nativeAttached = win ? XWindow_isNativeWindowAttached(win) : false;
    force = xwd_forceMode();
    if (force == 0) return false;
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
    if (force == 1) return true; /* XGUI_CSD=1：桌面目验/调试强制自绘。 */
    /* 仅在框架确为显示合成方时接管标题栏：fbdev 显示驱动已注册并探测
     * 成功（真正的无窗口管理器环境）。不能以「无原生窗」作判据——无
     * 显示的测试环境（回归/自测二进制，X11 连接失败回落虚拟 winId）
     * 同样无原生窗，合成事件直投控件树的用例会被装饰拦截（鼠标派发
     * 三连败教训）；桌面 X11 原生窗交 WM（flags 走 _MOTIF_WM_HINTS）。 */
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
        margins.top = xwd_titleBarHeight();
    return margins;
}

void XWindowDecoration_syncWindow(XWidget* top)
{
    XWindow* win;
    XMargins old;
    XMargins now;
    if (!top || !top->m_isWindow) return;
    win = top->m_windowHandle;
    if (!win) return;
    old = XWindow_frameMargins(win);
    now = XWindowDecoration_marginsFor(top);
    XWindow_setFrameMargins(win, &now);
    if (old.top != now.top || old.left != now.left ||
        old.right != now.right || old.bottom != now.bottom) {
        /* 保留边距变化（如 FramelessWindowHint 动态切换）：条带出现/
           消失区必须整窗重绘，单刷条带会留下旧标题栏残影。 */
        XWidget_update(top);
    }
}

void XWindowDecoration_draw(XWidget* top, XPainter* painter,
                            const XRegion* region)
{
    XStyle* style;
    XStyleOptionTitleBar opt;
    XFont font;
    XRect strip;
    int i;
    int barH;
    if (!top || !painter) return;
    if (!XWindowDecoration_activeFor(top)) return;
    style = XStyle_defaultStyle();
    if (!style) return;
    barH = XWindowDecoration_marginsFor(top).top;
    if (barH <= 0) return;
    /* 区域短路：刷新区域与条带不相交时零绘制（高频局部脏区路径）。 */
    if (region && region->count > 0) {
        XRect_init(&strip, 0, 0, XWidget_width(top), barH);
        for (i = 0; i < region->count; ++i) {
            XRect hit = XRect_intersected(&region->rects[i], &strip);
            if (!XRect_isEmpty(&hit)) break;
        }
        if (i >= region->count) return;
    }
    if (!xwd_buildOption(top, &opt, NULL)) return;
    font = XWidget_font(top);
    XPainter_setFont(painter, &font);
    XStyle_drawComplexControl(style, XStyleCC_TitleBar, &opt.m_base.m_base,
                              painter, top);
    /* 首帧装饰补拍：真机实测首帧条带只有底色、字形（文本/图标）缺席，
       交互触发的重绘即自愈、时间不自愈、偶发首帧即齐（惰性初始化竞态）。
       按钮字形已改图元直绘消图标依赖；文本侧以本补拍兜底（字体初始
       化完成后的首个全幅重绘必带齐字形）。 */
    {
        XWindowDecorationState* st = xwd_stateFor(top);
        if (st && !st->m_stripKick) {
            st->m_stripKick = true;
            XWidget_update(top);
        }
    }
}

bool XWindowDecoration_handlePointer(XWidget* top, XEvent* event)
{
    XWindowDecorationState* st;
    XMouseEvent* mouse;
    int barH;
    XPoint pos;
    if (!top || !event || !top->m_isWindow) return false;
    if (!XWindowDecoration_activeFor(top)) return false;
    barH = xwd_titleBarHeight();
    if (barH <= 0) return false;
    st = xwd_ensureState(top);
    if (!st) return false;
    switch (XEvent_type(event)) {
    case XEVENT_TYPE_WHEEL: {
        XWheelEvent* wheel = (XWheelEvent*)event;
        /* 条带上滚轮吞掉（对标 WM：标题栏不响应滚轮，也不漏给条下控件）。 */
        return wheel->m_position.y >= 0 && wheel->m_position.y < barH;
    }
    case XEVENT_TYPE_CONTEXT_MENU: {
        XContextMenuEvent* ctx = (XContextMenuEvent*)event;
        if (ctx->m_position.y < 0 || ctx->m_position.y >= barH) return false;
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
            }
            if (pos.y >= 0 && pos.y < barH)
                XWindow_requestActivate(st->m_window); /* 点条亦移焦。 */
            /* 按钮几何只存在于条带内，条外 hitTest 自然落空。 */
            sc = xwd_hitTest(top, &pos);
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
             * 行，子控件优先与桌面 WM 帧外命中等效，demo 真机口径）。
             * 增量锚取全局坐标：本地系随窗口移动自指（见拖拽处注释）。 */
            {
                int zone = xwd_resizeZoneAt(top, &pos, barH);
                if (zone) {
                    XWidget* hitChild = XWidget_childAt(top, &pos);
                    if (!hitChild || hitChild == (XWidget*)top) {
                        st->m_resizing = true;
                        st->m_resizeMask = zone;
                        st->m_resizeAnchor =
                            XMouseEvent_globalPosition(mouse);
                        st->m_resizeGeometry = XWidget_geometry(top);
                        XWidget_grabMouse(top);
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
                    XWidget_grabMouse(top);
                }
                XEvent_accept(event);
                return true;
            }
            return false; /* 条外非边缘：交控件树。 */
        }
        if (XMouseEvent_button(mouse) == XMouseButton_RightButton) {
            if (pos.y < 0 || pos.y >= barH) return false;
            xwd_openSystemMenu(st, &pos);
            XEvent_accept(event);
            return true;
        }
        if (pos.y >= 0 && pos.y < barH) {
            XEvent_accept(event); /* 条内其余按键吞掉。 */
            return true;
        }
        return false;
    case XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK: {
        mouse = (XMouseEvent*)event;
        pos = XMouseEvent_position(mouse);
        if (pos.y < 0 || pos.y >= barH) return false;
        /* 双击条内空白区切换最大化/还原（WM 标题栏双击语义）；按钮上
           双击维持无动作。 */
        if (XMouseEvent_button(mouse) == XMouseButton_LeftButton &&
            !xwd_isButtonSc(xwd_hitTest(top, &pos)))
            xwd_toggleMaximize(st);
        XEvent_accept(event);
        return true;
    }
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
        mouse = (XMouseEvent*)event;
        pos = XMouseEvent_position(mouse);
        if (st->m_dragging) {
            st->m_dragging = false;
            XWidget_releaseMouse(top);
            xwd_clearOutsideWindow(st);
            XEvent_accept(event);
            return true;
        }
        if (st->m_resizing) {
            st->m_resizing = false;
            XWidget_releaseMouse(top);
            xwd_clearOutsideWindow(st);
            XEvent_accept(event);
            return true;
        }
        if (st->m_armed) {
            int armed = st->m_armed;
            int sc = xwd_hitTest(top, &pos);
            st->m_armed = 0;
            xwd_repaintStrip(top);
            XEvent_accept(event);
            if (sc == armed) xwd_activateButton(st, armed);
            return true;
        }
        if (pos.y >= 0 && pos.y < barH) {
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
            xwd_applyResize(st, g.x - st->m_resizeAnchor.x,
                            g.y - st->m_resizeAnchor.y);
            XEvent_accept(event);
            return true;
        }
        if (st->m_dragging) {
            XPoint g = XMouseEvent_globalPosition(mouse);
            int dx = g.x - st->m_dragLast.x;
            int dy = g.y - st->m_dragLast.y;
            if (dx || dy)
                xwd_applyMove(st, dx, dy);
            st->m_dragLast = g;
            XEvent_accept(event);
            return true;
        }
        if (!(XMouseEvent_buttons(mouse) &
              (XMouseButton_LeftButton | XMouseButton_RightButton |
               XMouseButton_MiddleButton))) {
            /* 悬停热跟踪：钮形翻底色；按住后移出钮外解除武装（松开不
               再触发，桌面按钮同语义）。 */
            int hot = pos.y >= 0 && pos.y < barH ? xwd_hitTest(top, &pos) : 0;
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
            /* 尾交换摘除（注册表无序，O(1) 删除）。 */
            g_xwdStates[i] = g_xwdStates[g_xwdCount - 1];
            --g_xwdCount;
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
