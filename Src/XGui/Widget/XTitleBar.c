/**
 * @file       XTitleBar.c
 * @brief      XTitleBar 标题栏控件类实现（原 XWindowDecoration 绘制实现
 *             迁入的默认实现）。
 * @details    与同名头文件的公共 API 及 XTitleBar_Protected.h 的保护接口
 *             一一对应。默认实现只画：paintEvent 默认槽经样式系统
 *             CC_TitleBar 绘制条带（宿主顶层 = XWidget_parentWidget），
 *             hitTest 默认槽经样式 hitTestComplexControl 命中；几何钉位
 *             与输入拦截归 XWindowDecoration 装饰模块。几何操作只走
 *             XWidget/XWindow 公共 API，无平台 API 残留。
 * @author     XinYueC 团队
 */

#include "XTitleBar.h"
#include "XTitleBar_Protected.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XWindowEvent.h"
#include "XWindow.h"
#include "XPainter.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XGuiApplication.h"
#include "XSystem.h"

#if XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON

/** @brief 松手校验帧延迟（毫秒）：RELEASE flush 之后的一次性
 *  CoarseTimer 间隔。取值大于 16ms 拖拽合帧门控与 WM 异步几何回注的
 *  常规节奏（避免与在途正常帧重复合成），又远小于空闲周期定时器的
 *  秒级补发（用户观感「松手后 1~2s 才重绘」的停滞上限由此封顶）。 */
#define XTB_RELEASE_VERIFY_DELAY_MS 100u

/* ==================== 内部辅助：子控件位组装 ==================== */

/**
 * @brief 按窗口 flags 组装标题栏子控件位（对齐 Qt 窗口提示语义；原
 *        XWindowDecoration xwd_subControlsFor 原样迁入）。
 * @note  不做「定尺寸窗剥 □」——min>=max 判定在窗口几何动态变化的无
 *        WM 环境下不可靠（改尺寸后曾误剥 □ 致按钮失联+菜单项灰化，
 *        昆仑通态真机第二轮工作流实证）；最大化对定尺寸窗表现为几何钳
 *        制（软件路径 setGeometry 同值幂等），无副作用。
 */
static uint32_t xtb_subControlsFor(const XWindow* win)
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

/* ==================== 虚函数实现（static，仅供 class_init 注册） ==================== */

/**
 * @brief 命中测试默认实现：样式 hitTestComplexControl（原
 *        XWindowDecoration xwd_hitTest 迁入）。
 * @details 条内空白=SC_TitleBarLabel/None，按钮返回对应子控件位；宿主
 *          不可装饰（选项组装失败）返回 0。
 */
static int VXTitleBar_hitTest(XTitleBar* self, const XPoint* pos)
{
    XStyle* style = XStyle_defaultStyle();
    XStyleOptionTitleBar opt;
    if (!style || !pos) return 0;
    if (!XTitleBar_buildOption(self, &opt, NULL)) return 0;
    return XStyle_hitTestComplexControl(style, XStyleCC_TitleBar,
                                        &opt.m_base.m_base, pos->x, pos->y,
                                        (XWidget*)self);
}

/**
 * @brief 绘制默认实现：CC_TitleBar 条带（原 XWindowDecoration draw 核心
 *        迁入）+ 首帧装饰补拍。
 * @details 原绘制路径的区域短路（刷新区域与条带不相交时零绘制）在控件
 *          化后由事件体系结构性承接：PAINT 事件只按本控件脏区派发，且
 *          本控件即条带，故不再单设短路；绘制前按 PAINT 矩形裁剪，等价
 *          实现高频局部脏区只画增量。宿主缺席（未挂载）零绘制。
 */
static void VXTitleBar_paintEvent(XWidget* self, XEvent* event)
{
    XTitleBar* bar = (XTitleBar*)self;
    XWidget* host;
    XStyle* style;
    XStyleOptionTitleBar opt;
    XImage* image;
    XPainter painter;
    XFont font;
    XPoint offset;
    int barH;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    style = XStyle_defaultStyle();
    if (!style) return;
    barH = XTitleBar_defaultHeight();
    if (barH <= 0) return;
    if (!XTitleBar_buildOption(bar, &opt, NULL)) return;
    image = XWidget_paintImage(self);
    if (!image) return; /* 未上屏/未创建后备存储时无绘制目标。 */
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
#if XPAINTER_CLIP_ON
    {
        XRect clip = XPaintEvent_rect((XPaintEvent*)event);
        XPainter_setClipRect(&painter, &clip,
                             XPainterClipOperation_ReplaceClip);
    }
#endif
    /* 字体取宿主（原 draw 以顶层字体直绘条带；XWidget_font 为深拷贝值，
     * 用毕 deinit——原实现漏配对，此处按契约修正）。 */
    host = XWidget_parentWidget(self);
    font = XWidget_font(host ? host : self);
    XPainter_setFont(&painter, &font);
    XStyle_drawComplexControl(style, XStyleCC_TitleBar, &opt.m_base.m_base,
                              &painter, self);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    XClassDeinit((XClass*)&font);
    /* 首帧装饰补拍（原 XWindowDecoration.c draw 尾注迁入）：真机实测首
     * 帧条带只有底色、字形（文本/图标）缺席，交互触发的重绘即自愈、
     * 时间不自愈、偶发首帧即齐（惰性初始化竞态）。按钮字形已改图元直
     * 绘消图标依赖；文本侧以本补拍兜底（字体初始化完成后的首次重绘必
     * 带齐字形）。 */
    if (!bar->m_stripKick) {
        bar->m_stripKick = true;
        XWidget_update(self);
    }
}

/**
 * @brief 定时器事件：松手校验帧到时——停表并对宿主顶层补一帧全窗重绘。
 * @details 一次性定时器触发即自毁（id 先归位再停表，重入/失败路径均
 *          幂等）；宿主顶层 XWidget_update 以 contentsRect 入脏区，净窗
 *          上必产全窗 PAINT（帧耗毫秒级），内容已一致时屏幕零变化无
 *          可见副作用。非本校验帧的定时器事件按 ignore 交父类槽，保持
 *          XObject 继承语义（对标 XComboBox/XTabBar 同款重载口径）。
 */
static void VXTitleBar_timerEvent(XObject* object, XTimerEvent* event)
{
    XTitleBar* bar = (XTitleBar*)object;
    XWidget* host;
    XTimerId id;
    if (!bar || !event) return;
    if (XTimerEvent_timerId(event) != bar->m_releaseVerifyTimer) {
        XEvent_ignore((XEvent*)event);
        XClass_Parent(XObject, EXObject_TimerEvent,
                      void (*)(XObject*, XTimerEvent*))(object, event);
        return;
    }
    id = bar->m_releaseVerifyTimer;
    bar->m_releaseVerifyTimer = XTIMER_INVALID_ID;
    XObject_killTimer(object, id);
    /* 宿主顶层 = 父控件（与 buildOption 同口径）；armReleaseVerify 已
     * 保证挂载后才排程，此处宿主缺席属防御分支，落自身兜底。 */
    host = XWidget_parentWidget((XWidget*)bar);
    XWidget_update(host ? host : (XWidget*)bar);
    XEvent_accept((XEvent*)event);
}

/**
 * @brief 反初始化：回收挂起的松手校验帧定时器后交父类 XWidget 槽。
 * @details 定时器登记在事件派发器（XObject 销毁不自动注销登记项），
 *          析构不回收会在条控件释放后向悬空对象投递定时器事件（先例：
 *          XTabBar 析构停连发定时器）。本类无自有堆资源，其余清理仍
 *          由 XWidget 槽完整承接。
 */
static void VXTitleBar_deinit(XTitleBar* self)
{
    if (!self) return;
    if (self->m_releaseVerifyTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_releaseVerifyTimer);
        self->m_releaseVerifyTimer = XTIMER_INVALID_ID;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/** @brief 深拷贝：父类字段经 XWidget 拷贝槽，本类两个标量随拷。 */
static void VXTitleBar_copy(XTitleBar* self, const XTitleBar* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self))
        XTitleBar_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    self->m_activeSubControls = other->m_activeSubControls;
    self->m_stripKick = other->m_stripKick;
    /* 松手校验帧定时器不随拷贝转移（先释放旧值契约）：目标已有挂起
     * 定时器先回收再置未排程；源对象的定时器归源对象自身析构回收。 */
    if (self->m_releaseVerifyTimer != XTIMER_INVALID_ID)
        XObject_killTimer((XObject*)self, self->m_releaseVerifyTimer);
    self->m_releaseVerifyTimer = XTIMER_INVALID_ID;
}

/** @brief 移动：父类字段经 XWidget 移动槽，本类两个标量转移后清空源。 */
static void VXTitleBar_move(XTitleBar* self, XTitleBar* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self))
        XTitleBar_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    self->m_activeSubControls = other->m_activeSubControls;
    self->m_stripKick = other->m_stripKick;
    other->m_activeSubControls = 0;
    other->m_stripKick = false;
    /* 松手校验帧定时器不随移动转移：目标已有挂起定时器先回收再置未
     * 排程；源对象保留自身 id，由创建者随源析构回收（接收方不代清理，
     * 见「move 后源对象的清理责任」）。 */
    if (self->m_releaseVerifyTimer != XTIMER_INVALID_ID)
        XObject_killTimer((XObject*)self, self->m_releaseVerifyTimer);
    self->m_releaseVerifyTimer = XTIMER_INVALID_ID;
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XTitleBar_class_init(void)
{
    void* titleBarSlots[] = {
        VXTitleBar_hitTest /* EXTitleBar_HitTest：默认样式命中测试。 */
    };

    XVTABLE_INIT_DEFAULT(XTitleBar)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(titleBarSlots);
    /* 无自有堆资源；Deinit 仅额外回收挂起的松手校验帧定时器（登记在
       事件派发器，销毁不自动注销，见 VXTitleBar_deinit 注）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXTitleBar_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXTitleBar_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTitleBar_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXTitleBar_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXTitleBar_move);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XTitleBar_init(XTitleBar* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(XTitleBar));
    XWidget_init(&self->m_class, parent, flags);
    XClassSetVtable(self, XTitleBar);
    self->m_activeSubControls = 0;
    self->m_stripKick = false;
    self->m_releaseVerifyTimer = XTIMER_INVALID_ID;
}

XTitleBar* XTitleBar_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags)
{
    XTitleBar* self = (XTitleBar*)XMemory_malloc(sizeof(XTitleBar), memory);
    if (!self) return NULL;
    XMemset(self, 0, sizeof(XTitleBar));
    XTitleBar_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 虚函数调度函数 ==================== */

int XTitleBar_hitTest_base(XTitleBar* self, const XPoint* pos)
{
    if (ISNULL(self, "XTitleBar") || ISNULL(XClassGetVtable(self), "Vtable"))
        return 0;
    return XClassGetVirtualFunc(self, EXTitleBar_HitTest,
                                int(*)(XTitleBar*, const XPoint*))(self,
                                                                   pos);
}

/* ==================== 类型守卫（装饰路径动态类型校验） ==================== */

bool XTitleBar_isBar(const XWidget* candidate)
{
    XVtable* vt;
    XVtable* self_vt;
    if (!candidate) return false;
    /* XWidget 嵌 XObject、XObject 嵌 XClass（m_class 恒为第一成员），
     * 任意控件对象首字节即 XClass 头，读虚表指针无越界。 */
    vt = ((const XClass*)candidate)->m_vtable;
    if (!vt) return false;
    /* 三槽指纹（经 XVtable_at 越界安全读取，异常表返回 NULL 不相等）：
     * Copy/Move 为本类注册的专属实现，派生类经 XVTABLE_INHERIT_XCLASS
     * 原样继承、未继承本类虚表的类不可能持有；HitTest 槽非空同时证明
     * 命中虚槽可安全分派（该槽位于 XClass 头三个槽位之外的新增区）。 */
    self_vt = XTitleBar_class_init();
    return XVtable_at(vt, (size_t)EXClass_Copy) ==
               XVtable_at(self_vt, (size_t)EXClass_Copy) &&
           XVtable_at(vt, (size_t)EXClass_Move) ==
               XVtable_at(self_vt, (size_t)EXClass_Move) &&
           XVtable_at(vt, (size_t)EXTitleBar_HitTest) != NULL;
}

/* ==================== 保护接口（见 XTitleBar_Protected.h） ==================== */

int XTitleBar_defaultHeight(void)
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

bool XTitleBar_buildOption(XTitleBar* self, XStyleOptionTitleBar* opt,
                           int* outBarH)
{
    XWidget* host;
    XWindow* win;
    int barH;
    int rectH;
    uint32_t state;
    const XString* title;
    if (!self || !opt) return false;
    /* 宿主顶层 = 父控件：标题栏作为子控件挂载（XWidget_setTitleBarWidget），
     * 挂载即激活信号——原 XWindowDecoration 的 activeFor 判定由装饰模块
     * 的挂载决策承担，本类不回头依赖装饰头文件。 */
    host = XWidget_parentWidget((XWidget*)self);
    if (!host || !host->m_isWindow) return false;
    win = XWidget_nativeWindow(host);
    if (!win) return false;
    barH = XTitleBar_defaultHeight();
    if (barH <= 0) return false;
    XStyleOptionTitleBar_init(opt, XStyleCC_TitleBar);
    /* 整条矩形高 = 本控件真实几何高（样式度量 defaultHeight 仅在几何
     * 零高时兜底）：绘制/子控件排版/命中三方同源此矩形，若取度量值而
     * 控件几何更高（如 XWidget 对子控件预置 100x30、度量 28），样式画
     * 不满条带几何、底部两行外露宿主底色成白缝——「控件画满自身几何」
     * 为类不变式，任意条高（含派生自定义条显式定高）恒自洽。 */
    rectH = XWidget_height((XWidget*)self);
    if (rectH <= 0) rectH = barH;
    XRect_init(&opt->m_base.m_base.m_rect, 0, 0,
               XWidget_width((XWidget*)self), rectH);
    opt->m_base.m_base.m_palette = XWidget_palette(host);
    state = XStyleState_Enabled;
    /* 活动窗口配色：焦点窗口即活动（对标 WM 活动标题栏着色）。 */
    if (XGuiApplication_focusWindow() == win)
        state |= XStyleState_Active;
    opt->m_base.m_subControls = xtb_subControlsFor(win);
    opt->m_base.m_base.m_text = NULL;
    title = XWidget_windowTitle(host);
    if (title) opt->m_base.m_base.m_text = XString_toUtf8(title);
    if (!XIcon_isNull(&host->m_icon))
        opt->m_base.m_base.m_icon = &host->m_icon;
    opt->m_titleBarState = (uint32_t)XWindow_windowState(win);
    opt->m_titleBarFlags = (uint32_t)XWindow_flags(win);
    /* 活动子控件位注入（原 armed?armed:hot 合并口径）：本类仅存单值，
     * 非零统一按 Sunken+MouseOver 双置位——样式按下态优先绘制，无悬停
     * 的触屏设备观感与原实现一致（见保护头 setActiveSubControls 注）。 */
    if (self->m_activeSubControls) {
        opt->m_base.m_activeSubControls =
            (uint32_t)self->m_activeSubControls;
        state |= XStyleState_MouseOver | XStyleState_Sunken;
    }
    opt->m_base.m_base.m_state = state;
    if (outBarH) *outBarH = barH;
    return true;
}

void XTitleBar_setActiveSubControls(XTitleBar* self, int subControls)
{
    if (!self) return;
    if (self->m_activeSubControls == subControls) return;
    self->m_activeSubControls = subControls;
    XWidget_update((XWidget*)self);
}

int XTitleBar_activeSubControls(const XTitleBar* self)
{
    return self ? self->m_activeSubControls : 0;
}

/* ==================== 功能函数 ==================== */

void XTitleBar_armReleaseVerify(XTitleBar* self)
{
    XWidget* host;
    /* 排障回退开关（沿 XGUI_FLUSH_FULLFALLBACK 惯例：默认启用，环境
     * 变量 XWD_RELEASE_VERIFY=0 显式禁用）；进程级读一次缓存。 */
    static int verifyOn = -1;
    if (!self || XClassIsVtableNull(self)) return;
    if (self->m_releaseVerifyTimer != XTIMER_INVALID_ID) return; /* 幂等。 */
    if (verifyOn < 0) {
        const char* rv = XSystem_environment("XWD_RELEASE_VERIFY");
        verifyOn = rv && *rv && rv[0] == '0' && rv[1] == 0 ? 0 : 1;
    }
    if (!verifyOn) return;
    /* 宿主缺席（未挂载）不排程：校验帧以宿主顶层为对象，与装饰模块
     * 仅在原生窗已挂的 RELEASE 分支调用的前提互为冗余防线。 */
    host = XWidget_parentWidget((XWidget*)self);
    if (!host || !host->m_isWindow) return;
    self->m_releaseVerifyTimer = XObject_startTimer_ms(
        (XObject*)self, XTB_RELEASE_VERIFY_DELAY_MS,
        XTimerType_CoarseTimer);
}

#endif /* XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON */
