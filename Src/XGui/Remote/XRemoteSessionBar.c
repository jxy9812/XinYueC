/**
 * @file       XRemoteSessionBar.c
 * @brief      XRemoteSessionBar 向日葵式悬浮会话工具条实现(悬浮条 +
 *             半透明悬浮球 + 本地消费的拖拽/展开收起交互)。
 * @details    契约见 XRemoteSessionBar.h; 设计要点:
 *               - 全部远端控制走 XGuiClient 既有公开 API(冻结头零新增):
 *                 断开 = disconnectFromServer(BYE(NORMAL), 不触发自动
 *                 重连——「连上后怎么退出」的用户语义); 档位 =
 *                 requestProfileId(结果以远端 FB_META 为准, §5.3);
 *                 统计 = statistics 滑动采样; 状态 = connected/
 *                 disconnected/errorOccurred 信号自管理可见性;
 *               - 交互红线: 本控件是 XGuiClient 之外的独立子树, 命中
 *                 测试(XWidget_childAt 矩形链)与绘制裁剪(XWidget_
 *                 paintTree 祖先矩形交集)天然把它与远端视图隔离;
 *                 所有鼠标事件一律 XEvent_accept 本地消费, 结构上
 *                 不产生任何 INPUT_* 转发帧;
 *               - 与 XGuiClient grabMouse 抓取互不干扰: 按下即经
 *                 XWidget_grabMouse 置顶接管(单抓取者语义), 拖拽
 *                 越界事件仍派发到本控件直至释放;
 *               - 键盘钮(2026-10-03): 展开态显式弹出/收起应用虚拟
 *                 键盘单例(XGuiApplication_virtualKeyboard), 弹出
 *                 目标=会话视图顶层窗口; 与远程客户端页关断 autoPopup
 *                 的「RC 页全权接管」口径配套——点击页面不再自动弹
 *                 键盘, 唯本钮是显式入口; 断开(disconnected)时 toggle
 *                 复位并顺带 closePopup; XVIRTUALKEYBOARD_ON=0 时
 *                 按钮降级为静默空操作;
 *               - 半透明绘制: 控件画布为 ARGB32_Premultiplied 且每帧
 *                 清零(XWidget.c 重渲染从全透明画布开始), flush 经
 *                 XPainter_drawImage SourceOver 合成——alpha 填充即
 *                 与下层内容混色; 未用 XPAINTER_SHAPE_ON(XGuiConfig.h
 *                 1012 处裁 0), 悬浮球以叠矩形圆角近似绘制。
 * @author     XinYueC 团队
 */
#include "XRemoteSessionBar.h"
#if XWIDGET_ON && XGUI_REMOTE_ON

#include "XWidget_Protected.h" /* XWidget_paintImage/paintOffset(绘制基元)。 */
#include "XEvent.h"
#include "XWindowEvent.h"
#include "XPainter.h"
#include "XPushButton.h"
#include "XComboBox.h"
#include "XLabel.h"
#include "XTimer.h"
#include "XVarList.h"
#include "XVirtualKeyboard.h" /* 键盘钮: 应用虚拟键盘单例 popup/closePopup。 */
#include "XGuiApplication.h"  /* 键盘钮: XGuiApplication_virtualKeyboard 单例。 */
#include "XMemory.h"
#include "XString.h"
#include <stdio.h>
#include <string.h>

/* ==================== 私有常量 ==================== */

/** @brief 展开工具条尺寸(px)。2026-10-03 500→576: 追加键盘切换钮
 *         (64px), 「收起」右移让位(72px), 统计标签 196→200 让位 4px。 */
#define XRB_BAR_W 576
#define XRB_BAR_H 34
/** @brief 悬浮球边长(px)。 */
#define XRB_BALL_SIZE 44
/** @brief 吸附边距(条/球顶边距视图区上缘)。 */
#define XRB_SNAP_MARGIN 4
/** @brief 鼠标离开自动收起延时(毫秒, 需求口径「约 2 秒」)。 */
#define XRB_COLLAPSE_MS 2000u
/** @brief 悬停展开延时(毫秒; 留出拖动意图窗口, 按下即取消)。 */
#define XRB_HOVER_EXPAND_MS 600u
/** @brief 统计实时刷新周期(毫秒)。 */
#define XRB_STATS_MS 500u
/** @brief 按下-释放位移阈值(小于该值判点击展开, 否则判拖动)。 */
#define XRB_DRAG_THRESHOLD 4
/** @brief 条面板填充(ARGB32_Premultiplied)。2026-10-05 二改 a=0xE8 深底→
 *         0xF0 浅底不透明+深色文字：用户实测深底叠在黑镜像上「全是黑
 *         的看不清」，改浅色面板任意远端内容上都可读；悬浮球无文字
 *         维持 a=0x80 装饰性半透明。 */
#define XRB_PANEL_COLOR 0xF0ECECEAu
/** @brief 条面板边框色（浅底配中灰）。 */
#define XRB_PANEL_BORDER 0xFF8A8A96u
/** @brief 悬浮球填充(a=0x80 半透明)。 */
#define XRB_BALL_COLOR 0x80202028u
/** @brief 悬浮球边框色。 */
#define XRB_BALL_BORDER 0xFF6A6A78u
/** @brief 面板文字色（浅底配深字，2026-10-05 随面板改浅同步）。 */
#define XRB_TEXT_COLOR 0xFF1A1A22u

/* ==================== 私有实现块 ==================== */

/** @brief 工具条私有状态(契约不暴露, 见 XRemoteSessionBar.h m_d)。 */
typedef struct XRemoteSessionBarPrivate {
    XGuiClient* m_client;        /**< 绑定的客户端(借用)。 */
    XRect     m_viewRect;        /**< 吸附参照视图区(parent 坐标)。 */
    bool      m_expanded;        /**< true=展开条, false=悬浮球。 */
    XPoint    m_ballPos;         /**< 悬浮球左上角(parent 坐标)。 */
    bool      m_ballUserPlaced;  /**< 用户拖动换位过(双击回位依据)。 */

    bool      m_dragging;        /**< 球拖动进行中。 */
    XPoint    m_pressLocal;      /**< 按下点(控件局部)。 */
    XPoint    m_grabOffset;      /**< 按下点-球左上角(拖动保形)。 */
    bool      m_dragMoved;       /**< 本次按下已越过点击阈值。 */

    XGuiRemoteProfileId m_profile; /**< 界面档位选择(协商结果以 FB_META 为准)。 */
    uint32_t  m_dropEvents;      /**< 丢帧口径计数(帧类传输错误事件)。 */
    char      m_lastStats[128];  /**< 上次统计文本(去重刷新)。 */
    bool      m_keyboardPopped;  /**< 键盘钮 toggle 态(true=已请求弹出)。 */

    XPushButton* m_disconnectBtn; /**< [断开连接](子控件, 自持于树)。 */
    XComboBox*   m_profileCombo;  /**< 档位 性能|资源。 */
    XLabel*      m_statsLabel;    /**< 统计: 帧数/RTT/丢帧。 */
    XPushButton* m_keyboardBtn;   /**< [键盘] 显式弹收应用虚拟键盘(私有)。 */
    XPushButton* m_collapseBtn;   /**< [收起]。 */

    XTimer* m_collapseTimer;     /**< 离开 2s 收起(单次, 拥有)。 */
    XTimer* m_hoverTimer;        /**< 球悬停展开(单次, 拥有)。 */
    XTimer* m_statsTimer;        /**< 统计刷新(周期, 拥有)。 */
} XRemoteSessionBarPrivate;

/* ==================== 前置声明 ==================== */

static void xrb_expandInternal(XRemoteSessionBar* self);
static void xrb_collapseInternal(XRemoteSessionBar* self);
static void xrb_snapBarTopCenter(XRemoteSessionBar* self);
static void xrb_defaultBallPos(XRemoteSessionBarPrivate* d);
static void xrb_refreshStatsText(XRemoteSessionBar* self);
static void xrb_syncKeyboardBtnText(XRemoteSessionBar* self);

/* ==================== 定时器回调 ==================== */

static void xrb_collapseFire(void* userData, XTimerData* timer)
{
    XRemoteSessionBar* self = (XRemoteSessionBar*)userData;
    (void)timer;
    if (self && self->m_d) xrb_collapseInternal(self);
}

static void xrb_hoverFire(void* userData, XTimerData* timer)
{
    XRemoteSessionBar* self = (XRemoteSessionBar*)userData;
    (void)timer;
    if (self && self->m_d) xrb_expandInternal(self);
}

static void xrb_statsFire(void* userData, XTimerData* timer)
{
    XRemoteSessionBar* self = (XRemoteSessionBar*)userData;
    (void)timer;
    if (self && self->m_d) xrb_refreshStatsText(self);
}

/** @brief 启动单次定时器(已运行则重排)。 */
static void xrb_startOnce(XTimer* timer, uint32_t ms, void* userData,
                          void (*cb)(void*, XTimerData*))
{
    if (!timer) return;
    XTimer_setTimeout(timer, ms);
    XTimer_setSingleShot(timer, true);
    XTimer_setTimerCallback(timer, cb);
    XTimer_setUserData(timer, userData);
    XTimer_start_base(timer);
}

static void xrb_stopTimer(XTimer* timer)
{
    if (timer) XTimer_stop_base(timer);
}

/* ==================== 几何 ==================== */

/** @brief 悬浮球默认位 = 视图区顶部居中。 */
static void xrb_defaultBallPos(XRemoteSessionBarPrivate* d)
{
    int x = d->m_viewRect.x + (d->m_viewRect.width - XRB_BALL_SIZE) / 2;
    int y = d->m_viewRect.y + XRB_SNAP_MARGIN;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    XPoint_init(&d->m_ballPos, x, y);
}

/** @brief 展开条摆到视图区顶部居中。 */
static void xrb_snapBarTopCenter(XRemoteSessionBar* self)
{
    XRemoteSessionBarPrivate* d;
    int x, y;
    if (!self || !self->m_d) return;
    d = (XRemoteSessionBarPrivate*)self->m_d;
    x = d->m_viewRect.x + (d->m_viewRect.width - XRB_BAR_W) / 2;
    y = d->m_viewRect.y + XRB_SNAP_MARGIN;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    XWidget_setGeometry((XWidget*)self, x, y, XRB_BAR_W, XRB_BAR_H);
}

/* ==================== 展开/收起 ==================== */

static void xrb_expandInternal(XRemoteSessionBar* self)
{
    XRemoteSessionBarPrivate* d;
    if (!self || !self->m_d) return;
    d = (XRemoteSessionBarPrivate*)self->m_d;
    xrb_stopTimer(d->m_hoverTimer);
    if (!d->m_expanded) {
        d->m_expanded = true;
        /* 条几何: 吸附视图区顶部居中; 收起前若在拖位, 展开仍回吸附位
         * (吸附语义; 用户球位仅收起态生效, 双击亦可显式回位)。 */
        xrb_snapBarTopCenter(self);
        XWidget_raise((XWidget*)self); /* 置顶优先接收(交互红线)。 */
        if (d->m_disconnectBtn)
            XWidget_setVisible((XWidget*)d->m_disconnectBtn, true);
        if (d->m_profileCombo)
            XWidget_setVisible((XWidget*)d->m_profileCombo, true);
        if (d->m_statsLabel)
            XWidget_setVisible((XWidget*)d->m_statsLabel, true);
        if (d->m_keyboardBtn)
            XWidget_setVisible((XWidget*)d->m_keyboardBtn, true);
        if (d->m_collapseBtn)
            XWidget_setVisible((XWidget*)d->m_collapseBtn, true);
        xrb_refreshStatsText(self);
    }
    XWidget_update((XWidget*)self);
}

static void xrb_collapseInternal(XRemoteSessionBar* self)
{
    XRemoteSessionBarPrivate* d;
    if (!self || !self->m_d) return;
    d = (XRemoteSessionBarPrivate*)self->m_d;
    xrb_stopTimer(d->m_collapseTimer);
    if (d->m_expanded) {
        d->m_expanded = false;
        if (d->m_disconnectBtn)
            XWidget_setVisible((XWidget*)d->m_disconnectBtn, false);
        if (d->m_profileCombo)
            XWidget_setVisible((XWidget*)d->m_profileCombo, false);
        if (d->m_statsLabel)
            XWidget_setVisible((XWidget*)d->m_statsLabel, false);
        if (d->m_keyboardBtn)
            XWidget_setVisible((XWidget*)d->m_keyboardBtn, false);
        if (d->m_collapseBtn)
            XWidget_setVisible((XWidget*)d->m_collapseBtn, false);
        if (!d->m_ballUserPlaced) xrb_defaultBallPos(d);
        XWidget_setGeometry((XWidget*)self, d->m_ballPos.x, d->m_ballPos.y,
                            XRB_BALL_SIZE, XRB_BALL_SIZE);
        XWidget_raise((XWidget*)self);
    }
    XWidget_update((XWidget*)self);
}

/* ==================== 统计刷新 ==================== */

static void xrb_refreshStatsText(XRemoteSessionBar* self)
{
    XRemoteSessionBarPrivate* d;
    XGuiRemoteStats st;
    char buf[128];
    if (!self || !self->m_d) return;
    d = (XRemoteSessionBarPrivate*)self->m_d;
    if (!d->m_statsLabel) return;
    /* 未连接/未绑定: 面板仍展示占位口径(条只在连接期可见, 此处兜底)。 */
    XMemset(&st, 0, sizeof(st));
    if (d->m_client) XGuiClient_statistics(d->m_client, &st);
    snprintf(buf, sizeof(buf), "帧 %u | RTT %u ms | 丢帧 %u",
             (unsigned)st.updateCount, (unsigned)st.rttMs,
             (unsigned)d->m_dropEvents);
    if (strcmp(buf, d->m_lastStats) == 0) return; /* 无变化不刷屏。 */
    snprintf(d->m_lastStats, sizeof(d->m_lastStats), "%s", buf);
    XLabel_setText_2(d->m_statsLabel, buf);
}

/* ==================== 客户端信号槽(receiver=self) ==================== */

static void xrb_onClientConnected(XObject* receiver, XVarList* args)
{
    XRemoteSessionBar* self = (XRemoteSessionBar*)receiver;
    XRemoteSessionBarPrivate* d;
    (void)args;
    if (!self || !self->m_d) return;
    d = (XRemoteSessionBarPrivate*)self->m_d;
    XWidget_show((XWidget*)self);
    xrb_expandInternal(self);
    if (d->m_statsTimer) {
        XTimer_setInterval(d->m_statsTimer, XRB_STATS_MS);
        XTimer_setSingleShot(d->m_statsTimer, false);
        XTimer_setTimerCallback(d->m_statsTimer, xrb_statsFire);
        XTimer_setUserData(d->m_statsTimer, self);
        XTimer_start_base(d->m_statsTimer);
    }
    xrb_refreshStatsText(self);
}

static void xrb_onClientDisconnected(XObject* receiver, XVarList* args)
{
    XRemoteSessionBar* self = (XRemoteSessionBar*)receiver;
    XRemoteSessionBarPrivate* d;
    (void)args;
    if (!self || !self->m_d) return;
    d = (XRemoteSessionBarPrivate*)self->m_d;
    xrb_stopTimer(d->m_collapseTimer);
    xrb_stopTimer(d->m_hoverTimer);
    xrb_stopTimer(d->m_statsTimer);
    /* 会话终了: 键盘钮 toggle 复位; 键盘仍弹出则顺带收起(键盘目标=
     * 会话视图顶层, 断开即失去会话语境, 不留孤儿弹层; 键盘未建/
     * 弹层未开均无操作)。 */
    d->m_keyboardPopped = false;
    xrb_syncKeyboardBtnText(self);
#if XVIRTUALKEYBOARD_ON
    if (XVirtualKeyboard_popupVisible(XGuiApplication_virtualKeyboard()))
        XVirtualKeyboard_closePopup(XGuiApplication_virtualKeyboard());
#endif
    XWidget_hide((XWidget*)self); /* 断开=悬浮条隐藏(「未连接」态语义)。 */
}

static void xrb_onClientError(XObject* receiver, XVarList* args)
{
    XRemoteSessionBar* self = (XRemoteSessionBar*)receiver;
    XRemoteSessionBarPrivate* d;
    int code = -1;
    if (!self || !self->m_d) return;
    d = (XRemoteSessionBarPrivate*)self->m_d;
    if (args) {
        XVarList_args_1(args, int, c); /* 局部变量声明宏: 必须语句位。 */
        code = c;
    }
    /* 丢帧口径(头注): 帧类传输错误事件——队列溢出丢批/超长帧。 */
    if (code == (int)XGUI_REMOTE_ERR_BUFFER_OVERFLOW ||
        code == (int)XGUI_REMOTE_ERR_FRAME_TOO_LARGE) {
        ++d->m_dropEvents;
        xrb_refreshStatsText(self);
    }
}

static void xrb_onClientMeta(XObject* receiver, XVarList* args)
{
    (void)args;
    /* FB_META(切档生效等)后即时刷新一次统计展示。 */
    xrb_refreshStatsText((XRemoteSessionBar*)receiver);
}

/* ==================== 子控件槽(receiver=self) ==================== */

static void xrb_disconnectClicked(XObject* receiver, XVarList* args)
{
    XRemoteSessionBar* self = (XRemoteSessionBar*)receiver;
    XRemoteSessionBarPrivate* d;
    (void)args;
    if (!self || !self->m_d) return;
    d = (XRemoteSessionBarPrivate*)self->m_d;
    if (d->m_client)
        XGuiClient_disconnectFromServer(d->m_client); /* BYE(NORMAL), 不重连。 */
}

static void xrb_collapseClicked(XObject* receiver, XVarList* args)
{
    (void)args;
    xrb_collapseInternal((XRemoteSessionBar*)receiver);
}

/* ==================== 键盘钮(显式弹收应用虚拟键盘) ==================== */

/** @brief 按钮文本随 toggle 态同步(「键盘」= 可弹出,「收键盘」= 已弹出)。
 *  @details kb 为 NULL(XVIRTUALKEYBOARD_ON=0 / 未初始化应用)同样如实
 *           走文本同步——XPushButton_setText_2 自带判空无操作。 */
static void xrb_syncKeyboardBtnText(XRemoteSessionBar* self)
{
    XRemoteSessionBarPrivate* d;
    if (!self || !self->m_d) return;
    d = (XRemoteSessionBarPrivate*)self->m_d;
    if (!d->m_keyboardBtn) return;
    XPushButton_setText_2(d->m_keyboardBtn,
                          d->m_keyboardPopped ? "收键盘" : "键盘");
}

/** @brief 键盘钮点击: 对会话目标顶层显式 popup / closePopup 全权接管。
 *  @details 弹出目标口径: XRemoteSessionBar_client(self) 的会话视图
 *           (XGuiClient, XGuiClient.c init 即置 WA_InputMethodEnabled,
 *           过 XVirtualKeyboard_setTextArea 的 xkb_supportedTarget 校验)
 *           的顶层窗口——键盘以子控件浮层挂宿主顶层底部; 顶层会话视图
 *           关断 autoPopup 后, 这是 RC 页唯一的屏幕键盘入口。 */
static void xrb_keyboardClicked(XObject* receiver, XVarList* args)
{
    XRemoteSessionBar* self = (XRemoteSessionBar*)receiver;
    XRemoteSessionBarPrivate* d;
    XVirtualKeyboard* kb;
    (void)args;
    if (!self || !self->m_d) return;
    d = (XRemoteSessionBarPrivate*)self->m_d;
#if XVIRTUALKEYBOARD_ON
    kb = XGuiApplication_virtualKeyboard();
    if (kb) {
        if (!d->m_keyboardPopped) {
            XGuiClient* cli = XRemoteSessionBar_client(self);
            if (cli) {
                XVirtualKeyboard_popup(
                    kb, XWidget_topLevelWidget((XWidget*)cli));
                d->m_keyboardPopped =
                    XVirtualKeyboard_popupVisible(kb);
            }
        }
        else {
            XVirtualKeyboard_closePopup(kb);
            d->m_keyboardPopped = false;
        }
    }
#else
    (void)d; /* 键盘族被裁剪: 静默无操作(按钮形同虚设)。 */
#endif
    xrb_syncKeyboardBtnText(self);
}

/** @brief 键盘钮装配+接线(XVIRTUALKEYBOARD_ON=0 时仅建空壳按钮)。 */
static void xrb_setupKeyboardButton(XRemoteSessionBar* self,
                                    XRemoteSessionBarPrivate* d)
{
    if (!self || !d) return;
    d->m_keyboardBtn = XPushButton_create((XWidget*)self, 0);
    if (!d->m_keyboardBtn) return;
    XPushButton_setText_2(d->m_keyboardBtn, "键盘");
    XWidget_setGeometry((XWidget*)d->m_keyboardBtn, 424, 4, 64, 26);
    XObject_connect_1((XObject*)d->m_keyboardBtn,
                      (size_t)XAbstractButton_clicked_signal,
                      (XObject*)self, xrb_keyboardClicked,
                      XConnectionType_Direct);
    XWidget_setVisible((XWidget*)d->m_keyboardBtn, false);
}

static void xrb_profileActivated(XObject* receiver, XVarList* args)
{
    XRemoteSessionBar* self = (XRemoteSessionBar*)receiver;
    XRemoteSessionBarPrivate* d;
    int idx = -1;
    if (!self || !self->m_d) return;
    d = (XRemoteSessionBarPrivate*)self->m_d;
    if (args) {
        XVarList_args_1(args, int, i); /* 局部变量声明宏: 必须语句位。 */
        idx = i;
    }
    if (idx < 0 && d->m_profileCombo)
        idx = XComboBox_currentIndex(d->m_profileCombo);
    XRemoteSessionBar_requestProfile(self,
                   idx == 1 ? XGUI_REMOTE_PROFILE_RESOURCE
                            : XGUI_REMOTE_PROFILE_PERFORMANCE);
}

/* ==================== 虚槽: 绘制 ==================== */

/** @brief 1px 边框四条(填充条实现, 不依赖 XPAINTER_SHAPE_ON)。 */
static void xrb_paintBorder(XPainter* painter, int w, int h, uint32_t color)
{
    XRect rc;
    XRect_init(&rc, 0, 0, w, 1);
    XPainter_fillRect(painter, &rc, color);
    XRect_init(&rc, 0, h - 1, w, 1);
    XPainter_fillRect(painter, &rc, color);
    XRect_init(&rc, 0, 0, 1, h);
    XPainter_fillRect(painter, &rc, color);
    XRect_init(&rc, w - 1, 0, 1, h);
    XPainter_fillRect(painter, &rc, color);
}

static void VX_xrb_paintEvent(XWidget* self, XEvent* event)
{
    XRemoteSessionBar* b = (XRemoteSessionBar*)self;
    XRemoteSessionBarPrivate* d;
    XPainter painter;
    XImage* target;
    XPoint offset;
    XRect rc;
    int w, h;
    if (!b || !b->m_d || !event) return;
    d = (XRemoteSessionBarPrivate*)b->m_d;
    target = XWidget_paintImage(self);
    if (!target) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, target)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    w = XWidget_width(self);
    h = XWidget_height(self);
    if (d->m_expanded) {
        /* 半透明深色面板(与下层远端画面 SourceOver 混色)。 */
        XRect_init(&rc, 0, 0, w, h);
        XPainter_fillRect(&painter, &rc, XRB_PANEL_COLOR);
        xrb_paintBorder(&painter, w, h, XRB_PANEL_BORDER);
    } else {
        /* 悬浮球: 叠矩形圆角近似(8px 倒角), 半透明球面 + 边框。 */
        XRect_init(&rc, 3, 0, w - 6, h);
        XPainter_fillRect(&painter, &rc, XRB_BALL_COLOR);
        XRect_init(&rc, 0, 3, w, h - 6);
        XPainter_fillRect(&painter, &rc, XRB_BALL_COLOR);
        XRect_init(&rc, 6, 1, w - 12, h - 2);
        XPainter_fillRect(&painter, &rc, XRB_BALL_COLOR);
        XRect_init(&rc, 1, 6, w - 2, h - 12);
        XPainter_fillRect(&painter, &rc, XRB_BALL_COLOR);
        XRect_init(&rc, w / 2 - 8, h / 2 - 8, 16, 2);
        XPainter_fillRect(&painter, &rc, XRB_BALL_BORDER);
        XRect_init(&rc, w / 2 - 6, h / 2 + 2, 12, 2);
        XPainter_fillRect(&painter, &rc, XRB_BALL_BORDER);
    }
    XRect_init(&rc, 0, 0, w, h);
    XPainter_drawTextRect(&painter, &rc,
                          XPAINTER_TEXT_ALIGN_HCENTER |
                          XPAINTER_TEXT_ALIGN_VCENTER,
                          d->m_expanded ? "" : "远程", XRB_TEXT_COLOR);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/* ==================== 虚槽: 鼠标(全部本地消费, 零转发) ==================== */

static void VX_xrb_mousePressEvent(XWidget* self, XEvent* event)
{
    XRemoteSessionBar* b = (XRemoteSessionBar*)self;
    XRemoteSessionBarPrivate* d;
    XMouseEvent* me;
    if (!b || !b->m_d || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        return;
    }
    d = (XRemoteSessionBarPrivate*)b->m_d;
    me = (XMouseEvent*)event;
    xrb_stopTimer(d->m_collapseTimer); /* 按住/点击期间不自动收起。 */
    xrb_stopTimer(d->m_hoverTimer);
    d->m_pressLocal = me->m_position;
    d->m_dragMoved = false;
    if (!d->m_expanded) {
        /* 球拖动: 按下即置顶接管抓取(与视图拖拽抓取互不干扰)。 */
        d->m_dragging = true;
        d->m_grabOffset.x = me->m_position.x;
        d->m_grabOffset.y = me->m_position.y;
        XWidget_grabMouse(self);
    }
    XEvent_accept(event);
}

static void VX_xrb_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XRemoteSessionBar* b = (XRemoteSessionBar*)self;
    XRemoteSessionBarPrivate* d;
    XMouseEvent* me;
    int nx, ny, dx, dy;
    if (!b || !b->m_d || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) {
        return;
    }
    d = (XRemoteSessionBarPrivate*)b->m_d;
    me = (XMouseEvent*)event;
    if (d->m_dragging) {
        dx = me->m_position.x - d->m_pressLocal.x;
        dy = me->m_position.y - d->m_pressLocal.y;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx > XRB_DRAG_THRESHOLD || dy > XRB_DRAG_THRESHOLD)
            d->m_dragMoved = true;
        if (d->m_dragMoved) {
            int pw, ph;
            XWidget* p = (XWidget*)XObject_parent((XObject*)self);
            nx = XWidget_x(self) + me->m_position.x - d->m_grabOffset.x;
            ny = XWidget_y(self) + me->m_position.y - d->m_grabOffset.y;
            pw = p ? XWidget_width(p) : 0;
            ph = p ? XWidget_height(p) : 0;
            if (nx < 0) nx = 0;
            if (ny < 0) ny = 0;
            if (pw > XRB_BALL_SIZE && nx > pw - XRB_BALL_SIZE)
                nx = pw - XRB_BALL_SIZE;
            if (ph > XRB_BALL_SIZE && ny > ph - XRB_BALL_SIZE)
                ny = ph - XRB_BALL_SIZE;
            d->m_ballUserPlaced = true; /* 拖到哪停哪。 */
            XPoint_init(&d->m_ballPos, nx, ny);
            XWidget_setGeometry(self, nx, ny, XRB_BALL_SIZE, XRB_BALL_SIZE);
        }
    }
    XEvent_accept(event); /* 本地消费: 移动绝不转发远端。 */
}

static void VX_xrb_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XRemoteSessionBar* b = (XRemoteSessionBar*)self;
    XRemoteSessionBarPrivate* d;
    XMouseEvent* me;
    if (!b || !b->m_d || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) {
        return;
    }
    d = (XRemoteSessionBarPrivate*)b->m_d;
    me = (XMouseEvent*)event;
    if (d->m_dragging) {
        d->m_dragging = false;
        if (XWidget_mouseGrabber() == self) XWidget_releaseMouse(self);
        if (!d->m_dragMoved) {
            /* 点击(非拖动)悬浮球 → 展开。收起定时仅在释放点落在展开
             * 条矩形之外时挂起: 球→条是同一控件, 指针已在条内不会补发
             * EnterEvent(无人取消定时 → 悬停下方 2s 误收起, Xvfb
             * 实测 2026-10-03); 指针在外时挂起, 真正移入经 Enter 取消。 */
            int relX = XWidget_x(self) + me->m_position.x;
            int relY = XWidget_y(self) + me->m_position.y;
            xrb_expandInternal(b);
            if (relX < XWidget_x(self) || relX >= XWidget_x(self) + XRB_BAR_W ||
                relY < XWidget_y(self) || relY >= XWidget_y(self) + XRB_BAR_H) {
                xrb_startOnce(d->m_collapseTimer, XRB_COLLAPSE_MS, b,
                              xrb_collapseFire);
            }
        }
    }
    XEvent_accept(event);
}

static void VX_xrb_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XRemoteSessionBar* b = (XRemoteSessionBar*)self;
    XRemoteSessionBarPrivate* d;
    if (!b || !b->m_d || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) {
        return;
    }
    d = (XRemoteSessionBarPrivate*)b->m_d;
    xrb_stopTimer(d->m_hoverTimer);
    if (!d->m_expanded) {
        d->m_ballUserPlaced = false; /* 双击回顶部居中。 */
        xrb_defaultBallPos(d);
        xrb_expandInternal(b);
    }
    XEvent_accept(event);
}

static void VX_xrb_enterEvent(XWidget* self, XEvent* event)
{
    XRemoteSessionBar* b = (XRemoteSessionBar*)self;
    XRemoteSessionBarPrivate* d;
    if (!b || !b->m_d || !event) return;
    d = (XRemoteSessionBarPrivate*)b->m_d;
    xrb_stopTimer(d->m_collapseTimer); /* 悬停条/球: 不收起。 */
    if (!d->m_expanded) /* 悬停悬浮球 → 展开(留拖动意图窗口)。 */
        xrb_startOnce(d->m_hoverTimer, XRB_HOVER_EXPAND_MS, b, xrb_hoverFire);
    XEvent_accept(event);
}

static void VX_xrb_leaveEvent(XWidget* self, XEvent* event)
{
    XRemoteSessionBar* b = (XRemoteSessionBar*)self;
    XRemoteSessionBarPrivate* d;
    if (!b || !b->m_d || !event) return;
    d = (XRemoteSessionBarPrivate*)b->m_d;
    xrb_stopTimer(d->m_hoverTimer);
    if (d->m_expanded && !d->m_dragging) /* 离开约 2s 自动收起。 */
        xrb_startOnce(d->m_collapseTimer, XRB_COLLAPSE_MS, b, xrb_collapseFire);
    XEvent_accept(event);
}

static void VX_xrb_wheelEvent(XWidget* self, XEvent* event)
{
    if (!self || !event) return;
    XEvent_accept(event); /* 本地控件: 滚轮消费不上传。 */
}

/* ==================== 生命周期 ==================== */

static void VX_xrb_deinit(XRemoteSessionBar* self)
{
    XRemoteSessionBarPrivate* d;
    if (!self || !self->m_d) {
        XClass_Deinit_Parent(XWidget, (XWidget*)self);
        return;
    }
    d = (XRemoteSessionBarPrivate*)self->m_d;
    if (d->m_client) {
        /* 解绑信号(防析构后悬垂回调; 对齐 XGuiClient 析构纪律)。 */
        XObject_disconnect_1((XObject*)d->m_client,
                             (size_t)XGuiClient_connected_signal,
                             (XObject*)self, xrb_onClientConnected);
        XObject_disconnect_1((XObject*)d->m_client,
                             (size_t)XGuiClient_disconnected_signal,
                             (XObject*)self, xrb_onClientDisconnected);
        XObject_disconnect_1((XObject*)d->m_client,
                             (size_t)XGuiClient_errorOccurred_signal,
                             (XObject*)self, xrb_onClientError);
        XObject_disconnect_1((XObject*)d->m_client,
                             (size_t)XGuiClient_remoteMetaChanged_signal,
                             (XObject*)self, xrb_onClientMeta);
        d->m_client = NULL;
    }
    if (d->m_collapseTimer) {
        XTimer_stop_base(d->m_collapseTimer);
        XObject_deleteLater((XObject*)d->m_collapseTimer);
        d->m_collapseTimer = NULL;
    }
    if (d->m_hoverTimer) {
        XTimer_stop_base(d->m_hoverTimer);
        XObject_deleteLater((XObject*)d->m_hoverTimer);
        d->m_hoverTimer = NULL;
    }
    if (d->m_statsTimer) {
        XTimer_stop_base(d->m_statsTimer);
        XObject_deleteLater((XObject*)d->m_statsTimer);
        d->m_statsTimer = NULL;
    }
    /* 子控件/按钮经控件树级联析构(XWidget_deinit), 此处仅释放私有块。 */
    XFree_System(d);
    self->m_d = NULL;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XRemoteSessionBar_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XRemoteSessionBar)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_xrb_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_xrb_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VX_xrb_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VX_xrb_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VX_xrb_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_EnterEvent, VX_xrb_enterEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_LeaveEvent, VX_xrb_leaveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VX_xrb_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_xrb_deinit);
    return XVTABLE_DEFAULT;
}

void XRemoteSessionBar_init(XRemoteSessionBar* self, XWidget* parent,
                            XWidgetFlags flags)
{
    XRemoteSessionBarPrivate* d;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XRemoteSessionBar);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);

    d = (XRemoteSessionBarPrivate*)XCalloc_System(1,
                                                  sizeof(XRemoteSessionBarPrivate));
    self->m_d = d;
    if (!d) return;

    XRect_init(&d->m_viewRect, 0, 0, 0, 0);
    XPoint_init(&d->m_ballPos, 0, 0);
    d->m_profile = XGUI_REMOTE_PROFILE_PERFORMANCE;

    /* 初始收起为悬浮球(缺省 0×0 视图区, attachView 后摆位)。 */
    XWidget_resize((XWidget*)self, XRB_BALL_SIZE, XRB_BALL_SIZE);

    /* ---- 展开态子控件(初始隐藏, expand 时统一显出) ---- */
    d->m_disconnectBtn = XPushButton_create((XWidget*)self, 0);
    if (d->m_disconnectBtn) {
        XPushButton_setText_2(d->m_disconnectBtn, "断开连接");
        XWidget_setGeometry((XWidget*)d->m_disconnectBtn, 8, 4, 80, 26);
        XObject_connect_1((XObject*)d->m_disconnectBtn,
                          (size_t)XAbstractButton_clicked_signal,
                          (XObject*)self, xrb_disconnectClicked,
                          XConnectionType_Direct);
        XWidget_setVisible((XWidget*)d->m_disconnectBtn, false);
    }
    d->m_profileCombo = XComboBox_create((XWidget*)self, 0);
    if (d->m_profileCombo) {
        XComboBox_addItem_2(d->m_profileCombo, "性能");
        XComboBox_addItem_2(d->m_profileCombo, "资源");
        XComboBox_setCurrentIndex(d->m_profileCombo, 0);
        XWidget_setGeometry((XWidget*)d->m_profileCombo, 100, 4, 104, 26);
        XObject_connect_1((XObject*)d->m_profileCombo,
                          (size_t)XComboBox_activated_signal,
                          (XObject*)self, xrb_profileActivated,
                          XConnectionType_Direct);
        XWidget_setVisible((XWidget*)d->m_profileCombo, false);
    }
    d->m_statsLabel = XLabel_create((XWidget*)self, 0);
    if (d->m_statsLabel) {
        XLabel_setText_2(d->m_statsLabel, "帧 0 | RTT 0 ms | 丢帧 0");
        XLabel_setAlignment(d->m_statsLabel,
                            XAlignment_Left | XAlignment_VCenter);
        XWidget_setGeometry((XWidget*)d->m_statsLabel, 216, 4, 200, 26);
        XWidget_setVisible((XWidget*)d->m_statsLabel, false);
    }
    xrb_setupKeyboardButton(self, d); /* [键盘] (424,4,64), 初始隐藏。 */
    d->m_collapseBtn = XPushButton_create((XWidget*)self, 0);
    if (d->m_collapseBtn) {
        XPushButton_setText_2(d->m_collapseBtn, "收起");
        XWidget_setGeometry((XWidget*)d->m_collapseBtn, 496, 4, 72, 26);
        XObject_connect_1((XObject*)d->m_collapseBtn,
                          (size_t)XAbstractButton_clicked_signal,
                          (XObject*)self, xrb_collapseClicked,
                          XConnectionType_Direct);
        XWidget_setVisible((XWidget*)d->m_collapseBtn, false);
    }

    /* ---- 定时器(无父, deinit 显式回收)。 ---- */
    d->m_collapseTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    d->m_hoverTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    d->m_statsTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
}

XRemoteSessionBar* XRemoteSessionBar_create_ex(XMemoryType memory,
                                               XWidget* parent,
                                               XWidgetFlags flags)
{
    XRemoteSessionBar* self =
        (XRemoteSessionBar*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XRemoteSessionBar_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 公共 API ==================== */

void XRemoteSessionBar_setClient(XRemoteSessionBar* self, XGuiClient* client)
{
    XRemoteSessionBarPrivate* d;
    if (!self || !self->m_d || self->m_d->m_client == client) return;
    d = (XRemoteSessionBarPrivate*)self->m_d;
    if (d->m_client) {
        XObject_disconnect_1((XObject*)d->m_client,
                             (size_t)XGuiClient_connected_signal,
                             (XObject*)self, xrb_onClientConnected);
        XObject_disconnect_1((XObject*)d->m_client,
                             (size_t)XGuiClient_disconnected_signal,
                             (XObject*)self, xrb_onClientDisconnected);
        XObject_disconnect_1((XObject*)d->m_client,
                             (size_t)XGuiClient_errorOccurred_signal,
                             (XObject*)self, xrb_onClientError);
        XObject_disconnect_1((XObject*)d->m_client,
                             (size_t)XGuiClient_remoteMetaChanged_signal,
                             (XObject*)self, xrb_onClientMeta);
    }
    d->m_client = client;
    d->m_dropEvents = 0;
    d->m_lastStats[0] = '\0';
    if (client) {
        XObject_connect_1((XObject*)client,
                          (size_t)XGuiClient_connected_signal,
                          (XObject*)self, xrb_onClientConnected,
                          XConnectionType_Direct);
        XObject_connect_1((XObject*)client,
                          (size_t)XGuiClient_disconnected_signal,
                          (XObject*)self, xrb_onClientDisconnected,
                          XConnectionType_Direct);
        XObject_connect_1((XObject*)client,
                          (size_t)XGuiClient_errorOccurred_signal,
                          (XObject*)self, xrb_onClientError,
                          XConnectionType_Direct);
        XObject_connect_1((XObject*)client,
                          (size_t)XGuiClient_remoteMetaChanged_signal,
                          (XObject*)self, xrb_onClientMeta,
                          XConnectionType_Direct);
        /* 已在流式态(晚绑定): 对齐当前状态立即可见。 */
        if (XGuiClient_state(client) == XGUI_REMOTE_STATE_STREAMING)
            xrb_onClientConnected((XObject*)self, NULL);
        else
            XWidget_hide((XWidget*)self);
    }
    else {
        XWidget_hide((XWidget*)self);
    }
}

XGuiClient* XRemoteSessionBar_client(const XRemoteSessionBar* self)
{
    if (!self || !self->m_d) return NULL;
    return ((const XRemoteSessionBarPrivate*)self->m_d)->m_client;
}

void XRemoteSessionBar_attachView(XRemoteSessionBar* self,
                                  const XRect* viewRect)
{
    XRemoteSessionBarPrivate* d;
    XRect zero;
    if (!self || !self->m_d) return;
    d = (XRemoteSessionBarPrivate*)self->m_d;
    XRect_init(&zero, 0, 0, 0, 0);
    d->m_viewRect = viewRect ? *viewRect : zero;
    if (d->m_expanded)
        xrb_snapBarTopCenter(self);
    else if (!d->m_ballUserPlaced) {
        xrb_defaultBallPos(d);
        XWidget_setGeometry((XWidget*)self, d->m_ballPos.x, d->m_ballPos.y,
                            XRB_BALL_SIZE, XRB_BALL_SIZE);
    }
}

void XRemoteSessionBar_expand(XRemoteSessionBar* self)
{
    xrb_expandInternal(self);
}

void XRemoteSessionBar_collapse(XRemoteSessionBar* self)
{
    xrb_collapseInternal(self);
}

bool XRemoteSessionBar_isExpanded(const XRemoteSessionBar* self)
{
    if (!self || !self->m_d) return false;
    return ((const XRemoteSessionBarPrivate*)self->m_d)->m_expanded;
}

bool XRemoteSessionBar_isBallUserPlaced(const XRemoteSessionBar* self)
{
    if (!self || !self->m_d) return false;
    return ((const XRemoteSessionBarPrivate*)self->m_d)->m_ballUserPlaced;
}

void XRemoteSessionBar_requestProfile(XRemoteSessionBar* self,
                                      XGuiRemoteProfileId id)
{
    XRemoteSessionBarPrivate* d;
    int idx;
    if (!self || !self->m_d) return;
    d = (XRemoteSessionBarPrivate*)self->m_d;
    if (id != XGUI_REMOTE_PROFILE_RESOURCE) id = XGUI_REMOTE_PROFILE_PERFORMANCE;
    d->m_profile = id;
    idx = (id == XGUI_REMOTE_PROFILE_RESOURCE) ? 1 : 0;
    if (d->m_profileCombo &&
        XComboBox_currentIndex(d->m_profileCombo) != idx)
        XComboBox_setCurrentIndex(d->m_profileCombo, idx);
    if (d->m_client) XGuiClient_requestProfileId(d->m_client, id);
}

XGuiRemoteProfileId XRemoteSessionBar_profile(const XRemoteSessionBar* self)
{
    if (!self || !self->m_d)
        return XGUI_REMOTE_PROFILE_PERFORMANCE;
    return ((const XRemoteSessionBarPrivate*)self->m_d)->m_profile;
}

struct XPushButton* XRemoteSessionBar_disconnectButton(
    const XRemoteSessionBar* self)
{
    if (!self || !self->m_d) return NULL;
    return (struct XPushButton*)
        ((const XRemoteSessionBarPrivate*)self->m_d)->m_disconnectBtn;
}

struct XLabel* XRemoteSessionBar_statsLabel(const XRemoteSessionBar* self)
{
    if (!self || !self->m_d) return NULL;
    return (struct XLabel*)
        ((const XRemoteSessionBarPrivate*)self->m_d)->m_statsLabel;
}

#endif /* XWIDGET_ON && XGUI_REMOTE_ON */
