/**
 * @file       HsfMonitorValve.c
 * @brief      电磁阀监视/操作页实现：6 路卡片看板 + 控制闸 + 顶部横幅。
 * @details    [层次]（TopToBottom）
 *               1) 页标题行（粗体大标题 + 副标题说明）
 *               2) 控制横幅（控制被禁用原因 / N 路点位未配置；可隐藏）
 *               3) 卡片网格（宽屏按视口宽自适应 1~3 列 / 窄屏 1 列）
 *               4) 底部提示
 *
 *             [卡片结构] 每路一张卡（白底软描边）：
 *                 ● 阀名                [状态徽标]
 *                 开到位：是/否      ●
 *                 关到位：是/否      ○
 *                 [ 开阀 ]  [ 关阀 ]
 *                 提示行（未配置/故障原因，小字灰）
 *
 *             [为什么状态点与反馈点都自绘] 板端 256 色渲染字符圆点会出
 *             条纹（本项目已在数字量页踩过），XPainter 画圆与字体/色深
 *             全部无关。
 *
 *             [数据模型] 模块级 s_valve 静态模型 + 页面借用指针：
 *             页未创建时 API 只存模型（安全空操作），页创建时用模型初值
 *             填充——这样"先建链路后建页"与"先建页后到数据"两种时序都成立。
 */
#include "HsfMonitorValve.h"

#include "HsfTheme.h"
#include "HsfUi.h"   /* hsfUi_pageFontPx（本地设置·按页字号） */
#include "XhsfLinkProto.h"
#include "XLabel.h"
#include "XPushButton.h"
#include "XBoxLayout.h"
#include "XGridLayout.h"
#include "XObject.h"
#include "XEvent.h"
#include "XTimer.h"
#include "XPainter.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XFont.h"
#include "XString.h"
#include "XSystem.h"   /* XSystem_environment（形态诊断开关）。 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/** @brief 本页字号折算（本地设置 FontScale/Page7）。 */
static int valvePx(int px)
{
    return hsfUi_pageFontPx(HSF_PAGE_MONITOR_VALVE, px);
}

/* ==================== 常量 ==================== */

/** @brief 卡片最小宽（宽屏 3 列时每卡约 300px）。 */
#define VALVE_CARD_MIN_W      240
/** @brief 卡片高度（内容固定 5 行，定高避免各行高度不一）。 */
#define VALVE_CARD_H          172
/** @brief 卡片圆角。 */
#define VALVE_CARD_RADIUS     5
/** @brief 卡片内边距。 */
#define VALVE_CARD_PAD        12
/** @brief 状态点直径（卡头）。 */
#define VALVE_DOT_MAIN        16
/** @brief 反馈点直径。 */
#define VALVE_DOT_FB          12
/** @brief 宽屏列数。 */
#define VALVE_COLS_WIDE       3
/** @brief 网格行/列间距（建网格 setSpacing 与 adapt 估列、网格宿主最小高
 *         同取此处，三处绑死防漂移）。 */
#define VALVE_GRID_SPACING    12
/** @brief 页根左右边距（adapt 的 setContentsMargins 与估列减宽绑死）。 */
#define VALVE_MARGIN_SIDE_WIDE     24
/** @brief 页根左右边距（紧凑形态）。 */
#define VALVE_MARGIN_SIDE_COMPACT  12

/* ---- 状态色（与 Python 侧 VALVE_STATE_TEXT 逐值一致） ---- */
#define VALVE_CLR_UNKNOWN     0xFF9E9E9Eu  /**< 状态未知（浅灰）。 */
#define VALVE_CLR_CLOSED      0xFF37474Fu  /**< 已关（灰蓝）。 */
#define VALVE_CLR_OPEN        0xFF2E7D32u  /**< 已开（绿）。 */
#define VALVE_CLR_MOVING      0xFFED6C02u  /**< 动作中（橙）。 */
#define VALVE_CLR_FAULT       0xFFC62828u  /**< 故障（红）。 */

/* ---- 其他配色 ---- */
#define VALVE_CLR_CARD_BG     0xFFFFFFFFu  /**< 卡片底。 */
#define VALVE_CLR_CARD_LINE   0xFFD5DBE2u  /**< 卡片描边（软）。 */
#define VALVE_CLR_TEXT        0xFF1A1A1Au  /**< 正文。 */
#define VALVE_CLR_TEXT_DIM    0xFF5A6572u  /**< 弱文本。 */
#define VALVE_CLR_FB_OFF      0xFFB0BEC5u  /**< 反馈未到位（空心灰）。 */
#define VALVE_CLR_BANNER_BG   0xFFFFF4E5u  /**< 提示横幅底（浅橙）。 */
#define VALVE_CLR_BANNER_LINE 0xFFFFD8A8u  /**< 提示横幅描边。 */
#define VALVE_CLR_BANNER_TEXT 0xFFB26500u  /**< 提示横幅字。 */

/* [随主题取套 2026-11-11 双主题走查] 页面自绘卡面按当前主题取亮/深套
 * （亮套=上表现行值零改动；深套见 VValveCard_paintEvent 注）。 */
static uint32_t valveThemeClr(uint32_t light, uint32_t dark)
{
    return HsfTheme_isDarkSkin() ? dark : light;
}

/* ==================== 数据模型（模块级） ==================== */

/** @brief 单路模型（与页面解耦；页未建时也可写）。 */
typedef struct HsfValveModelItem {
    char m_name[48];      /**< 阀名（UTF-8）。 */
    int  m_state;         /**< HsfValveState。 */
    bool m_openFb;        /**< 开到位反馈。 */
    bool m_closeFb;       /**< 关到位反馈。 */
    bool m_ready;         /**< 点位配置齐全。 */
    bool m_valid;         /**< 数据有效。 */
    char m_text[96];      /**< 提示文本。 */
} HsfValveModelItem;

/** @brief 页面/模型共享状态。 */
static struct
{
    HsfValveModelItem m_items[HSF_VALVE_COUNT]; /**< 各路模型。 */
    int  m_count;                     /**< 路数。 */
    bool m_controlEnabled;            /**< 是否允许操作。 */
    char m_blockedReason[128];        /**< 禁止原因（UTF-8；空=可操作）。 */
    char m_alarmText[128];            /**< 最近告警横幅（服务端原文；空=无）。 */
    HsfValveWriteFn m_writeFn;        /**< 控制出口。 */
    void* m_writeUser;                /**< 出口令牌。 */
    void (*m_setStatus)(void*, const char*); /**< 状态栏反馈。 */
    void* m_statusUser;               /**< 状态栏令牌。 */
    char m_lastError[96];             /**< 最近一次被拒原因。 */
    bool m_compact;                   /**< 窄屏形态。 */
    XWidget* m_page;                  /**< 页根借用（未建 NULL）。 */
    XWidget* m_banner;                /**< 横幅控件借用。 */
    XLabel*  m_bannerLabel;           /**< 横幅文本借用。 */
    XGridLayout* m_grid;              /**< 卡片网格借用。 */
} s_valve;

/** @brief 状态 → 中文/颜色（与 Python VALVE_STATE_TEXT 逐项对齐）。 */
static void valveStateStyle(int state, const char** text, uint32_t* color)
{
    switch (state) {
    case HsfValveState_Open:
        *text = "已开";              /* 已开 */
        *color = VALVE_CLR_OPEN;
        break;
    case HsfValveState_Closed:
        *text = "已关";              /* 已关 */
        *color = VALVE_CLR_CLOSED;
        break;
    case HsfValveState_Moving:
        *text = "动作中";  /* 动作中 */
        *color = VALVE_CLR_MOVING;
        break;
    case HsfValveState_Fault:
        *text = "故障";              /* 故障 */
        *color = VALVE_CLR_FAULT;
        break;
    default:
        *text = "状态未知"; /* 状态未知 */
        *color = VALVE_CLR_UNKNOWN;
        break;
    }
}

/* ==================== 自绘圆点控件 ==================== */

/** @brief 圆点用途（决定颜色口径）。 */
typedef enum ValveDotKind {
    ValveDotKind_Status = 0,   /**< 卡头状态点（按状态上色，未配置=灰）。 */
    ValveDotKind_Feedback      /**< 到位反馈点（实心绿=到位，空心灰=未到）。 */
} ValveDotKind;

XCLASS_DEFINE_BEGING(ValveDot)
XCLASS_DEFINE_EXTEND_END(ValveDot, XWidget)

/**
 * @brief 自绘圆点。
 * @details [关抗锯齿] 256 色屏上 AA 会在边缘混出第三种颜色（实测：红/绿
 *          点边缘混进蓝，整排像彩色条纹）。关掉后边缘硬切，量化只影响
 *          一两个像素，通断一眼可辨。
 */
typedef struct ValveDot
{
    XWidget      m_base;   /**< 基类；必须是第一个成员。 */
    ValveDotKind m_kind;   /**< 用途。 */
    bool         m_on;     /**< 反馈点：是否到位。 */
    int          m_state;  /**< 状态点：HsfValveState。 */
    bool         m_ready;  /**< 状态点：点位是否配置齐全。 */
    int          m_diam;   /**< 直径。 */
} ValveDot;

static void VValveDot_paintEvent(XWidget* self, XEvent* event);

XVtable* ValveDot_class_init(void)
{
    XVTABLE_INIT_DEFAULT(ValveDot)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VValveDot_paintEvent);
    return XVTABLE_DEFAULT;
}

static void valveDotInit(ValveDot* dot, XWidget* parent, ValveDotKind kind,
                         int diam)
{
    memset(dot, 0, sizeof(*dot));
    XWidget_init(&dot->m_base, parent, 0);
    XClassGetVtable(dot) = ValveDot_class_init();
    dot->m_kind = kind;
    dot->m_diam = diam;
    dot->m_state = HsfValveState_Unknown;
    XWidget_setFixedSize(&dot->m_base, diam + 6, diam + 6);
    XWidget_show(&dot->m_base);
}

static void VValveDot_paintEvent(XWidget* self, XEvent* event)
{
    ValveDot* dot = (ValveDot*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;
    uint32_t fill;
    int w;

    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
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

    w = XWidget_width(self);
    if (w > XWidget_height(self)) w = XWidget_height(self);
    /* 背景透明：不填底，圆点直接落在卡片白底上。 */

    if (dot->m_kind == ValveDotKind_Feedback) {
        if (dot->m_on) {
            fill = VALVE_CLR_OPEN;
            XPainter_setPen(&painter, 0xFF1B5E20u);   /* 深绿描边。 */
            XPainter_setPenWidth(&painter, 1);
            XPainter_setBrush(&painter, fill);
            XRect_init(&r, 3, 3, dot->m_diam, dot->m_diam);
            XPainter_drawEllipse(&painter, &r);
        } else {
            /* 空心：只描边不填充（NoBrush）。 */
            XPainter_setPen(&painter, VALVE_CLR_FB_OFF);
            XPainter_setPenWidth(&painter, 1);
            XPainter_setBrush_2(&painter, XPainterBrushStyle_NoBrush);
            XRect_init(&r, 3, 3, dot->m_diam, dot->m_diam);
            XPainter_drawEllipse(&painter, &r);
        }
    } else {
        const char* txt = NULL;
        uint32_t color;
        if (!dot->m_ready) {
            fill = VALVE_CLR_UNKNOWN;
        } else {
            valveStateStyle(dot->m_state, &txt, &color);
            fill = color;
        }
        XPainter_setPen(&painter, fill);
        XPainter_setPenWidth(&painter, 2);
        XPainter_setBrush(&painter, fill);
        XRect_init(&r, 3, 3, dot->m_diam, dot->m_diam);
        XPainter_drawEllipse(&painter, &r);
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/* ==================== 单卡片对象 ==================== */

/** @brief 单路卡片（自绘卡片底 + 子控件）。 */
typedef struct ValveCard
{
    XWidget     m_base;      /**< 卡容器；第一个成员。 */
    int         m_index;     /**< 路序。 */
    ValveDot    m_dot;       /**< 卡头状态点。 */
    XLabel      m_name;      /**< 阀名。 */
    XLabel      m_badge;     /**< 状态徽标（文字）。 */
    ValveDot    m_openDot;   /**< 开到位反馈点。 */
    ValveDot    m_closeDot;  /**< 关到位反馈点。 */
    XLabel      m_openText;  /**< 开到位文字。 */
    XLabel      m_closeText; /**< 关到位文字。 */
    XPushButton m_openBtn;   /**< 开阀。 */
    XPushButton m_closeBtn;  /**< 关阀。 */
    XLabel      m_hint;      /**< 提示行。 */
} ValveCard;

static void VValveCard_paintEvent(XWidget* self, XEvent* event);

XCLASS_DEFINE_BEGING(ValveCardWidget)
XCLASS_DEFINE_EXTEND_END(ValveCardWidget, XWidget)

XVtable* ValveCard_class_init(void)
{
    XVTABLE_INIT_DEFAULT(ValveCardWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VValveCard_paintEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 卡片底色 + 圆角描边（自绘；样式表引擎对裸容器支持有限）。 */
static void VValveCard_paintEvent(XWidget* self, XEvent* event)
{
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;

    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
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

    XRect_init(&r, 0, 0, XWidget_width(self), XWidget_height(self));
    /* [卡面随主题 2026-11-11 双主题走查] 白卡常量在深色盘下成整片白块
     * （主页卡同款实锅）。亮套维持原值；深套=面板 #23282F + 软描边
     * #39414C（HsfTheme.h 深色令牌），卡内标签走调色板随盘。 */
    XPainter_setPen(&painter,
                    valveThemeClr(VALVE_CLR_CARD_LINE,
                                  HSF_COLOR_SOFT_BORDER_DARK));
    XPainter_setPenWidth(&painter, 1);
    XPainter_setBrush(&painter,
                      valveThemeClr(VALVE_CLR_CARD_BG, HSF_COLOR_PANEL_DARK));
    XPainter_drawRoundedRect(&painter, &r, VALVE_CARD_RADIUS, VALVE_CARD_RADIUS);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/* ---- 卡片内容刷新 ---- */

static void valveCardApply(ValveCard* card)
{
    HsfValveModelItem* it;
    const char* stext = NULL;
    uint32_t scolor;
    char buf[64];
    bool canControl;

    if (!card) return;
    if (card->m_index < 0 || card->m_index >= s_valve.m_count) return;
    it = &s_valve.m_items[card->m_index];

    /* ---- 名称 ---- */
    XLabel_setText_2(&card->m_name,
                     it->m_name[0] ? it->m_name : " ");

    /* ---- 状态徽标 ---- */
    if (!it->m_ready) {
        stext = "未配置";  /* 未配置 */
        scolor = VALVE_CLR_UNKNOWN;
    } else {
        valveStateStyle(it->m_state, &stext, &scolor);
    }
    /* [同值早退 2026-11-08] 本函数由**每次阀快照推送**驱动（服务端 100ms
     * ×2 侧 ⇒ 20 次/秒），六张卡各 4 个自绘件。原实现无条件
     * XWidget_update ⇒ 6×4×20 = 480 笔脏区/秒，实测阀页 FPS 95.7。
     * 三个自绘件（徽标/状态点/两个到位点）的值没变就不必重画：
     *   · 徽标：XLabel_setText 同值已早退，这里只补 update —— 比较文本；
     *   · 圆点：比较 m_state/m_ready/m_on 三元组。 */
    if (XLabel_text(&card->m_badge) == NULL ||
        strcmp(XString_toUtf8(XLabel_text(&card->m_badge)),
               stext ? stext : "") != 0) {
        XLabel_setText_2(&card->m_badge, stext);
        XWidget_update((XWidget*)&card->m_badge);
    }
    /* [双编码] 颜色由卡头圆点承担（XLabel 无设色接口），文字承担语义
     * ——色弱/黑白屏也能读出状态。 */
    (void)scolor;

    /* ---- 卡头状态点 ---- */
    if (card->m_dot.m_state != it->m_state ||
        card->m_dot.m_ready != it->m_ready) {
        card->m_dot.m_state = it->m_state;
        card->m_dot.m_ready = it->m_ready;
        XWidget_update((XWidget*)&card->m_dot);
    }

    /* ---- 反馈点与文字 ---- */
    if (card->m_openDot.m_on != it->m_openFb) {
        card->m_openDot.m_on = it->m_openFb;
        XWidget_update((XWidget*)&card->m_openDot);
    }
    if (card->m_closeDot.m_on != it->m_closeFb) {
        card->m_closeDot.m_on = it->m_closeFb;
        XWidget_update((XWidget*)&card->m_closeDot);
    }
    if (it->m_ready && it->m_valid) {
        /* [2026-11-08 修重复文案损伤] 此前被批量去重改成了
         *   "开到位：%s", it->m_openFb ? "开到位：是/否" : "开到位：是/否"
         * ——两个分支完全相同（永远显示"是/否"字样，不反映反馈值），
         * 且参数里又拼了一遍"开到位："，界面显示成
         * 「开到位：开到位：是/否」（用户实测截图）。正确语义：格式串带
         * 名称、参数只给"是"/"否"。 */
        snprintf(buf, sizeof(buf), "开到位：%s",
                 it->m_openFb ? "是" : "否");
        XLabel_setText_2(&card->m_openText, buf);
        snprintf(buf, sizeof(buf), "关到位：%s",
                 it->m_closeFb ? "是" : "否");
        XLabel_setText_2(&card->m_closeText, buf);
    } else {
        XLabel_setText_2(&card->m_openText,
                         "开到位：--"); /* 开到位：-- */
        XLabel_setText_2(&card->m_closeText,
                         "关到位：--"); /* 关到位：-- */
    }

    /* ---- 按钮可用性 ----
     * [已到位那侧置灰] 避免重复下发同一个动作（Python 同款口径）。
     * 未配置/闸门拦截 ⇒ 全灰，让"点不动"有明确原因。
     * [先算最终态再下发 2026-11-08 实锅] 原实现先
     * `setEnabled(close, canControl)` 再对已关阀 `setEnabled(close, false)`：
     * 每拍推一次 = 关阀钮**启用→禁用两次真实状态翻转**（setEnabled 只在
     * 状态真变时才标脏/发事件）。6 卡 × 20Hz ⇒ 开/关钮 240 笔脏区/秒、
     * 阀页 60fps（帧耗时 0.7ms 看着不贵，但全是白画）。最终态一次算清。 */
    canControl = it->m_ready && s_valve.m_controlEnabled;
    {
        const bool openEnabled = canControl && it->m_state != HsfValveState_Open;
        const bool closeEnabled = canControl && it->m_state != HsfValveState_Closed;
        XWidget_setEnabled((XWidget*)&card->m_openBtn, openEnabled);
        XWidget_setEnabled((XWidget*)&card->m_closeBtn, closeEnabled);
    }

    /* ---- 提示行 ---- */
    {
        const char* hint = it->m_text;
        /* [与状态徽标重复就不显示] 服务端在阀动作期间把 m_text 置成"动作中"
         * （HsfServerIoValve.c 的两处 snprintf），而卡片右上角的状态徽标
         * 已经写着"动作中" ⇒ 同一个信息显示两遍，看着像多余的字段
         * （2026-11-06 用户实锅）。**只压掉与状态文案完全相同的**那条，
         * 其它文案（外部改动接管、冲突、点位未配置等）照常显示。 */
        if (hint[0]) {
            const char* stText = NULL;
            uint32_t stColor = 0;
            valveStateStyle(it->m_state, &stText, &stColor);
            if (stText && strcmp(hint, stText) == 0) hint = "";
        }
        if (!hint[0] && !it->m_ready)
            hint = "点位未配置"
                   "（服务端 HsfServerConfig.h"
                   "的 HSF_VALVE_DO/OPEN_DI/CLOSE_DI 里"
                   "填通道号）";
        /* 点位未配置（在服务端 HsfServerConfig.h 的 HSF_VALVE_DO/OPEN_DI/CLOSE_DI 里填通道号） */
        XLabel_setText_2(&card->m_hint, hint);
        /* [避免重复 setVisible 2026-11-08 实锅] 本函数由每次阀快照推送驱动
         * （20 次/秒 ×6 卡）。框架的 setVisible **刻意不做同值早退**（显隐
         * 联动布局激活），重复调用会每拍 XLayout_invalidate 卡片布局 ⇒
         * 布局风暴（实测 662 次失效/秒、卡片里的开阀/关阀钮 248 笔脏区/秒、
         * 阀页 60fps）。状态没变就不要调：判据用 isHidden()（显式隐藏位，
         * 对标 Qt QWidget::isHidden），隐藏页里也稳定。 */
        {
            const bool wantVisible = hint[0] != '\0';
            if (XWidget_isHidden((XWidget*)&card->m_hint) == wantVisible)
                XWidget_setVisible((XWidget*)&card->m_hint, wantVisible);
        }
    }
}

/* ---- 按钮槽（receiver = 按钮自身；offsetof 反推卡片基址） ---- */

static void valveCardRequest(ValveCard* card, bool wantOpen)
{
    HsfValveModelItem* it;
    if (!card || card->m_index < 0 || card->m_index >= s_valve.m_count) return;
    it = &s_valve.m_items[card->m_index];

    /* 三重前置校验：闸门 → 点位 → 出口。任一不满足都给出明确原因，
     * 绝不让"点了没反应"。 */
    if (!s_valve.m_controlEnabled) {
        snprintf(s_valve.m_lastError, sizeof(s_valve.m_lastError), "%s",
                 s_valve.m_blockedReason[0] ? s_valve.m_blockedReason
                                            : "控制已禁用");
        if (s_valve.m_setStatus)
            s_valve.m_setStatus(s_valve.m_statusUser, s_valve.m_lastError);
        return;
    }
    if (!it->m_ready) {
        snprintf(s_valve.m_lastError, sizeof(s_valve.m_lastError), "%s",
                 "点位未配置"
                 "，无法控制");
        if (s_valve.m_setStatus)
            s_valve.m_setStatus(s_valve.m_statusUser, s_valve.m_lastError);
        return;
    }
    s_valve.m_lastError[0] = '\0';
    if (s_valve.m_setStatus) {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s → 正在%s…",
                 it->m_name,
                 wantOpen ? "开阀" : "关阀");
        /* "阀名 → 正在开阀…" */
        s_valve.m_setStatus(s_valve.m_statusUser, msg);
    }
    if (s_valve.m_writeFn)
        s_valve.m_writeFn(s_valve.m_writeUser, card->m_index, wantOpen);
}

static void valveOpenSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    valveCardRequest((ValveCard*)((char*)receiver - offsetof(ValveCard, m_openBtn)),
                     true);
}

static void valveCloseSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    valveCardRequest((ValveCard*)((char*)receiver - offsetof(ValveCard, m_closeBtn)),
                     false);
}

/* ---- 卡片装配 ---- */

static ValveCard* valveCardCreate(XWidget* parent, int index)
{
    ValveCard* card;
    XBoxLayout* lay;
    XBoxLayout* head;
    XBoxLayout* fbRow;
    XBoxLayout* btnRow;

    card = (ValveCard*)XMemory_malloc(sizeof(ValveCard),
                                      XCLASS_DEFAULT_MEMORY_TYPE);
    if (!card) return NULL;
    memset(card, 0, sizeof(ValveCard));
    XWidget_init(&card->m_base, parent, 0);
    XClassGetVtable(card) = ValveCard_class_init();
    Set_Class_Memory(card, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(card, true);      /* 整块随页级联释放。 */
    card->m_index = index;
    XWidget_setMinimumWidth(&card->m_base, VALVE_CARD_MIN_W);
    XWidget_setFixedHeight(&card->m_base, VALVE_CARD_H);
    HsfTheme_applyDefaultFont(&card->m_base);

    lay = (XBoxLayout*)XMemory_malloc(sizeof(XBoxLayout),
                                      XCLASS_DEFAULT_MEMORY_TYPE);
    /* [布局对象必须成员化或堆化] 栈上局部布局对象会成悬垂指针（项目已踩）。 */
    if (!lay) return card;
    XBoxLayout_init(lay, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)lay, VALVE_CARD_PAD, 10,
                               VALVE_CARD_PAD, 10);
    XLayout_setSpacing((XLayout*)lay, 6);
    XWidget_setLayout(&card->m_base, (XLayout*)lay);

    /* ---- 卡头：状态点 + 阀名 + 状态徽标 ---- */
    head = (XBoxLayout*)XMemory_malloc(sizeof(XBoxLayout),
                                       XCLASS_DEFAULT_MEMORY_TYPE);
    XBoxLayout_init(head, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)head, 8);
    valveDotInit(&card->m_dot, &card->m_base, ValveDotKind_Status,
                 VALVE_DOT_MAIN);
    XBoxLayout_addWidget(head, (XWidget*)&card->m_dot);

    XLabel_init(&card->m_name, &card->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&card->m_name);
    {
        XFont f = XWidget_font((XWidget*)&card->m_name);
        XFont_setBold(&f, true);
        XWidget_setFont((XWidget*)&card->m_name, &f);
        XClassDeinit(&f);
    }
    XLabel_setTextPixelSize(&card->m_name, valvePx(15));
    XLabel_setAlignment(&card->m_name, XAlignment_Left | XAlignment_VCenter);
    XLabel_setText_2(&card->m_name, " ");
    XWidget_show((XWidget*)&card->m_name);
    XBoxLayout_addWidgetEx(head, (XWidget*)&card->m_name, 1, 0);

    XLabel_init(&card->m_badge, &card->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&card->m_badge);
    XLabel_setTextPixelSize(&card->m_badge, valvePx(12));
    XLabel_setAlignment(&card->m_badge, XAlignment_HCenter | XAlignment_VCenter);
    XWidget_setMinimumWidth((XWidget*)&card->m_badge, 64);
    XLabel_setText_2(&card->m_badge,
                     "未配置");  /* 未配置 */
    XWidget_show((XWidget*)&card->m_badge);
    XBoxLayout_addWidget(head, (XWidget*)&card->m_badge);
    XBoxLayout_addLayout(lay, (XLayout*)head);

    /* ---- 开到位行 ---- */
    fbRow = (XBoxLayout*)XMemory_malloc(sizeof(XBoxLayout),
                                        XCLASS_DEFAULT_MEMORY_TYPE);
    XBoxLayout_init(fbRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)fbRow, 6);
    valveDotInit(&card->m_openDot, &card->m_base, ValveDotKind_Feedback,
                 VALVE_DOT_FB);
    XBoxLayout_addWidget(fbRow, (XWidget*)&card->m_openDot);
    XLabel_init(&card->m_openText, &card->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&card->m_openText);
    XLabel_setTextPixelSize(&card->m_openText, valvePx(12));
    XLabel_setAlignment(&card->m_openText,
                        XAlignment_Left | XAlignment_VCenter);
    HsfTheme_applySecondaryText((XWidget*)&card->m_openText);
    XLabel_setText_2(&card->m_openText,
                     "开到位：--");
    XWidget_show((XWidget*)&card->m_openText);
    XBoxLayout_addWidgetEx(fbRow, (XWidget*)&card->m_openText, 1, 0);
    XBoxLayout_addLayout(lay, (XLayout*)fbRow);

    /* ---- 关到位行 ---- */
    fbRow = (XBoxLayout*)XMemory_malloc(sizeof(XBoxLayout),
                                        XCLASS_DEFAULT_MEMORY_TYPE);
    XBoxLayout_init(fbRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)fbRow, 6);
    valveDotInit(&card->m_closeDot, &card->m_base, ValveDotKind_Feedback,
                 VALVE_DOT_FB);
    XBoxLayout_addWidget(fbRow, (XWidget*)&card->m_closeDot);
    XLabel_init(&card->m_closeText, &card->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&card->m_closeText);
    XLabel_setTextPixelSize(&card->m_closeText, valvePx(12));
    XLabel_setAlignment(&card->m_closeText,
                        XAlignment_Left | XAlignment_VCenter);
    HsfTheme_applySecondaryText((XWidget*)&card->m_closeText);
    XLabel_setText_2(&card->m_closeText,
                     "关到位：--");
    XWidget_show((XWidget*)&card->m_closeText);
    XBoxLayout_addWidgetEx(fbRow, (XWidget*)&card->m_closeText, 1, 0);
    XBoxLayout_addLayout(lay, (XLayout*)fbRow);

    /* ---- 操作按钮行 ---- */
    btnRow = (XBoxLayout*)XMemory_malloc(sizeof(XBoxLayout),
                                         XCLASS_DEFAULT_MEMORY_TYPE);
    XBoxLayout_init(btnRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)btnRow, 8);
    XPushButton_init(&card->m_openBtn, &card->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&card->m_openBtn);
    XPushButton_setText_2(&card->m_openBtn,
                          "开阀");   /* 开阀 */
    XWidget_setFixedHeight((XWidget*)&card->m_openBtn, 34);
    XObject_connect((XObject*)&card->m_openBtn,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)&card->m_openBtn, valveOpenSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&card->m_openBtn);
    XBoxLayout_addWidgetEx(btnRow, (XWidget*)&card->m_openBtn, 1, 0);

    XPushButton_init(&card->m_closeBtn, &card->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&card->m_closeBtn);
    XPushButton_setText_2(&card->m_closeBtn,
                          "关阀");   /* 关阀 */
    XWidget_setFixedHeight((XWidget*)&card->m_closeBtn, 34);
    XObject_connect((XObject*)&card->m_closeBtn,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)&card->m_closeBtn, valveCloseSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&card->m_closeBtn);
    XBoxLayout_addWidgetEx(btnRow, (XWidget*)&card->m_closeBtn, 1, 0);
    XBoxLayout_addLayout(lay, (XLayout*)btnRow);

    /* ---- 提示行 ---- */
    XLabel_init(&card->m_hint, &card->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&card->m_hint);
    XLabel_setTextPixelSize(&card->m_hint, valvePx(11));
    XLabel_setAlignment(&card->m_hint, XAlignment_Left | XAlignment_VCenter);
    /* [长提示必须换行 2026-11-11 多尺寸走查] 「点位未配置（服务端
     * HsfServerConfig.h 的 HSF_VALVE_DO/OPEN_DI/CLOSE_DI 里填通道号）」
     * 单行 ~500px：XLabel 不换行时 minimumSizeHint=整行文本宽（Qt 同
     * 口径）⇒ 卡片布局最小宽被它顶到 ~494，比 VALVE_CARD_MIN_W=240
     * 大一倍 ⇒ 网格 3 列最小合计 ~1506 > 视口 1092，滚动壳按内容最小
     * 宽撑开页面 ⇒ 第 3 列只剩 ~86px 被窗缘硬裁（1280x800 实测；滚壳
     * 横向条被 HsfUi 设 AlwaysOff，无路可滚）。Qt 口径 wordWrap=true
     * 时 minimumSizeHint 走 sizeForWidth(0)（XLabel_minimumSizeHint 已
     * 对齐 qlabel.cpp），最小宽回落到"最宽单词"，卡片最小宽重新由
     * 240 兜底。与 Python 客户端同位（pages_valve.py:180
     * hint_label.setWordWrap(True)）同口径。 */
    XLabel_setWordWrap(&card->m_hint, true);
    HsfTheme_applySecondaryText((XWidget*)&card->m_hint);
    XLabel_setText_2(&card->m_hint, " ");
    XWidget_show((XWidget*)&card->m_hint);
    XBoxLayout_addWidget(lay, (XWidget*)&card->m_hint);

    XWidget_show(&card->m_base);
    return card;
}

/* ==================== 页对象 ==================== */

/** @brief 电磁阀页对象；m_base 必须是第一个成员。 */
typedef struct HsfMonitorValvePage
{
    XWidget     m_base;      /**< 页容器；第一个成员。 */
    XBoxLayout  m_layout;    /**< 页内垂直布局（成员化，禁止栈上）。 */
    XBoxLayout  m_titleRow;  /**< 标题行。 */
    XLabel      m_title;     /**< 页标题。 */
    XLabel      m_subtitle;  /**< 副标题。 */
    XWidget     m_banner;    /**< 控制横幅容器（值成员；m_bannerBuilt 标志已建）。 */
    bool        m_bannerBuilt; /**< 横幅是否已建（未建时刷新函数直接跳过）。 */
    XLabel      m_bannerText;/**< 横幅文本。 */
    XGridLayout m_grid;      /**< 卡片网格（成员化）。 */
    XWidget*    m_gridHost;  /**< 网格宿主（堆、随页释放；adapt 要按行数给最小高）。 */
    ValveCard*  m_cards[HSF_VALVE_COUNT]; /**< 卡片借用指针。 */
    XLabel      m_footHint;  /**< 底部提示。 */
} HsfMonitorValvePage;

/** @brief 已建页借用指针（模块单例里保存；未建 NULL）。 */
static HsfMonitorValvePage* s_valvePage = NULL;

/** @brief 按当前模型重建卡片网格（路数变化/首次建页时调用）。 */
static void valveRebuildCards(HsfMonitorValvePage* page)
{
    int i;
    int cols;
    if (!page) return;
    /* 旧卡片由框架随父级释放；这里只清网格引用并重建。
     * [注意] XGridLayout 没有"清空"公共 API 时，用隐藏旧卡片 + 重新
     * addWidget 会导致重复条目，故路数变化时**整页重建**成本更低：
     * 由调用方（setCount）把页销毁交给主窗重新建页。实践中路数在
     * 建页前就由服务端配置定死，运行期极少变化。 */
    cols = s_valve.m_compact ? 1 : VALVE_COLS_WIDE;
    for (i = 0; i < s_valve.m_count; ++i) {
        if (page->m_cards[i]) continue;
        page->m_cards[i] = valveCardCreate(&page->m_base, i);
        if (page->m_cards[i]) {
            XGridLayout_addWidget(&page->m_grid, &page->m_cards[i]->m_base,
                                  i / cols, i % cols, 0);
            valveCardApply(page->m_cards[i]);
        }
    }
}

/** @brief 刷新横幅（闸门原因优先，其次未配置路数）。 */
static void valveRefreshBanner(HsfMonitorValvePage* page)
{
    char buf[192];
    int unconfigured = 0;
    int i;

    if (!page) return;
    /* [崩溃防线] 横幅控件在页装配过程中才建；建页期早期调到这里时
     * m_bannerText 还没 init，setText/ setVisible 会走未初始化对象的
     * 字符串拷贝（2026-11-05 实测：valveRefreshAll → valveRefreshBanner
     * → XWidget_setVisible → VXWidget_copy → XString_create_copy 崩溃）。 */
    if (!page->m_bannerBuilt) return;
    if (s_valve.m_blockedReason[0]) {
        /* [格式串必须带 %s] 原先字面量既重复两遍又**没有格式符**，
         * m_blockedReason 作为多余实参被直接丢弃 ⇒ 横幅永远只显示前缀，
         * 用户看不到禁用原因（未连接/自动模式/权限不足）。 */
        snprintf(buf, sizeof(buf), "控制已禁用：%s", s_valve.m_blockedReason);
    } else if (s_valve.m_alarmText[0]) {
        /* [最近告警常驻] 与 Python pages_valve._drain_alarms 同口径：横幅
         * 留一条最近的告警（服务端原文），直到下一条告警覆盖或出现禁用
         * 原因；Cleared（恢复）只走状态栏，不清横幅。 */
        snprintf(buf, sizeof(buf), "%s", s_valve.m_alarmText);
    } else {
        for (i = 0; i < s_valve.m_count; ++i)
            if (!s_valve.m_items[i].m_ready) ++unconfigured;
        if (unconfigured > 0) {
            snprintf(buf, sizeof(buf),
                     "%d 路点位未"
                     "配置（服务"
                     "端 HSF_VALVE_DO/OPEN_DI/CLOSE_DI "
                     "指定通道号"
                     "）",
                     unconfigured);
            /* N 路点位未配置（服务端 HSF_VALVE_DO/OPEN_DI/CLOSE_DI 指定通道号） */
        } else {
            buf[0] = '\0';
        }
    }
    XLabel_setText_2(&page->m_bannerText, buf[0] ? buf : " ");
    /* [避免重复 setVisible] 同卡内 hint：每拍推一次显隐会持续失效布局。 */
    {
        const bool wantVisible = buf[0] != '\0';
        if (XWidget_isHidden((XWidget*)&page->m_banner) == wantVisible)
            XWidget_setVisible((XWidget*)&page->m_banner, wantVisible);
    }
}

/** @brief 重刷全部卡片 + 横幅（幂等、廉价）。 */
static void valveRefreshAll(void)
{
    HsfMonitorValvePage* page = s_valvePage;
    int i;
    if (!page) return;
    for (i = 0; i < s_valve.m_count; ++i) {
        if (page->m_cards[i]) valveCardApply(page->m_cards[i]);
    }
    valveRefreshBanner(page);
}

/* ==================== 公开 API ==================== */

/** @brief 字号实时重放（本地设置改动时由 HsfUi 调回）。 */
static void valveApplyFonts(XWidget* page, int pct)
{
    HsfMonitorValvePage* self = (HsfMonitorValvePage*)page;
    int i;
    if (!self || pct <= 0) return;
    XLabel_setTextPixelSize(&self->m_title, 17 * pct / 100);
    XLabel_setTextPixelSize(&self->m_subtitle, 12 * pct / 100);
    XLabel_setTextPixelSize(&self->m_bannerText, 12 * pct / 100);
    XLabel_setTextPixelSize(&self->m_footHint, 11 * pct / 100);
    for (i = 0; i < HSF_VALVE_COUNT; ++i) {
        ValveCard* c = self->m_cards[i];
        if (!c) continue;
        XWidget_setFixedHeight(&c->m_base, VALVE_CARD_H * pct / 100);
        XLabel_setTextPixelSize(&c->m_name, 15 * pct / 100);
        XLabel_setTextPixelSize(&c->m_badge, 12 * pct / 100);
        XLabel_setTextPixelSize(&c->m_openText, 12 * pct / 100);
        XLabel_setTextPixelSize(&c->m_closeText, 12 * pct / 100);
        XLabel_setTextPixelSize(&c->m_hint, 11 * pct / 100);
    }
}

XWidget* HsfMonitorValve_create(XWidget* parent, HsfMenuNavFn navFn,
                                void* userData)
{
    HsfMonitorValvePage* self;
    int i;

    (void)navFn;
    (void)userData;
    if (!parent) return NULL;

    self = (HsfMonitorValvePage*)XMemory_malloc(sizeof(HsfMonitorValvePage),
                                                XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(HsfMonitorValvePage));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);   /* 整块随主窗级联释放。 */
    HsfTheme_applyDefaultFont(&self->m_base);
    /* [2026-11-05 窄屏裁切根修] 页根纵向必须是 Preferred（按内容高拑开）。
     * 外层是 setWidgetResizable(true) 的滚动壳：内容控件会被**拉伸到视口大小**，
     * 除非它自身上报的 sizeHint 更大。原先页根沿用框架缺省
     * Expanding → 内容被压回一屏高，窄屏（compact，6 张卡片竖排）
     * 溢出的 5 张直接被裁掉且**不出现滚动条**
     * （实测 480x700 只看得到「电磁阀7」。数字量页为何正常：
     * 它自带 MinimumSizeHint 虚槽上报看板高度）。
     * 横向保持 Expanding（随视口展宽）。 */
    XWidget_setSizePolicy(&self->m_base, XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Preferred);
    s_valvePage = self;

    /* ---- 页内垂直布局 ---- */
    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 24, 20, 24, 16);
    XLayout_setSpacing((XLayout*)&self->m_layout, 10);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);

    /* ---- 标题行 ---- */
    XBoxLayout_init(&self->m_titleRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&self->m_titleRow, 10);
    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    {
        XFont f = XWidget_font((XWidget*)&self->m_title);
        XFont_setBold(&f, true);
        XWidget_setFont((XWidget*)&self->m_title, &f);
        XClassDeinit(&f);
    }
    XLabel_setTextPixelSize(&self->m_title, valvePx(17));
    XLabel_setText_2(&self->m_title,
                     "电动球阀");  /* 电动球阀 */
    XWidget_show((XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_titleRow, (XWidget*)&self->m_title);

    XLabel_init(&self->m_subtitle, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_subtitle);
    XLabel_setTextPixelSize(&self->m_subtitle, valvePx(12));
    XLabel_setAlignment(&self->m_subtitle,
                        XAlignment_Left | XAlignment_Bottom);
    HsfTheme_applySecondaryText((XWidget*)&self->m_subtitle);
    {
        char sub[96];
        snprintf(sub, sizeof(sub),
                 "%d 路 · 1 路输出"
                 " + 2 路到位反馈",
                 HSF_VALVE_COUNT);   /* N 路 · 1 路输出 + 2 路到位反馈 */
        XLabel_setText_2(&self->m_subtitle, sub);
    }
    XWidget_show((XWidget*)&self->m_subtitle);
    XBoxLayout_addWidget(&self->m_titleRow, (XWidget*)&self->m_subtitle);
    XBoxLayout_addStretch(&self->m_titleRow, 1);
    XBoxLayout_addLayout(&self->m_layout, (XLayout*)&self->m_titleRow);

    /* ---- 控制横幅（默认隐藏） ---- */
    {
        XWidget_init(&self->m_banner, &self->m_base, 0);
        self->m_bannerBuilt = true;
        {
            XBoxLayout* bl = (XBoxLayout*)XMemory_malloc(sizeof(XBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
            if (bl) {
                XBoxLayout_init(bl, XBoxLayoutDirection_LeftToRight);
                XLayout_setContentsMargins((XLayout*)bl, 10, 6, 10, 6);
                XWidget_setLayout(&self->m_banner, (XLayout*)bl);
            }
            XLabel_init(&self->m_bannerText, &self->m_banner, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_bannerText);
            XLabel_setTextPixelSize(&self->m_bannerText, valvePx(12));
            XLabel_setText_2(&self->m_bannerText, " ");
            XWidget_show((XWidget*)&self->m_bannerText);
            if (bl) XBoxLayout_addWidgetEx(bl, (XWidget*)&self->m_bannerText, 1, 0);
        }
        XWidget_show(&self->m_banner);
        XWidget_setVisible(&self->m_banner, false);
        XBoxLayout_addWidget(&self->m_layout, &self->m_banner);
    }

    /* ---- 卡片网格 ---- */
    XGridLayout_init(&self->m_grid);
    XGridLayout_setSpacing(&self->m_grid, VALVE_GRID_SPACING);
    {
        int cols = s_valve.m_compact ? 1 : VALVE_COLS_WIDE;
        for (i = 0; i < VALVE_COLS_WIDE; ++i)
            XGridLayout_setColumnStretch(&self->m_grid, i, 1);
        (void)cols;
    }
    {
        /* 网格需要一个宿主控件才能进 BoxLayout（XBoxLayout_addLayout 也可，
         * 但网格高度需由卡片撑开，用带布局的宿主更直观）。 */
        XWidget* gridHost = XWidget_create(&self->m_base, 0);
        if (gridHost) {
            XWidget_setLayout(gridHost, (XLayout*)&self->m_grid);
            XWidget_show(gridHost);
            self->m_gridHost = gridHost;
            XBoxLayout_addWidget(&self->m_layout, gridHost);
        }
    }

    /* ---- 底部提示 ---- */
    XLabel_init(&self->m_footHint, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_footHint);
    XLabel_setTextPixelSize(&self->m_footHint, valvePx(11));
    XLabel_setAlignment(&self->m_footHint, XAlignment_Left | XAlignment_VCenter);
    HsfTheme_applySecondaryText((XWidget*)&self->m_footHint);
    XLabel_setText_2(&self->m_footHint,
                     "阀位状态只"
                     "由到位反馈"
                     "推导；命令"
                     "回显不代表"
                     "阀已动作");
    /* 阀位状态只由到位反馈推导；命令回显不代表阀已动作 */
    XWidget_show((XWidget*)&self->m_footHint);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_footHint);
    XBoxLayout_addStretch(&self->m_layout, 1);

    /* ---- 初次填充 ---- */
    if (s_valve.m_count <= 0) {
        /* 模型还没被喂过：按上限先建满卡片（显示"未配置"），
         * 服务端快照到达后由 setCount 收敛。 */
        s_valve.m_count = HSF_VALVE_COUNT;
        for (i = 0; i < s_valve.m_count; ++i) {
            if (!s_valve.m_items[i].m_name[0])
                snprintf(s_valve.m_items[i].m_name,
                         sizeof(s_valve.m_items[i].m_name),
                         "电动球阀%d", i + 1);
        }
    }
    valveRebuildCards(self);
    valveRefreshAll();

    hsfUi_registerPageFontApplier(HSF_PAGE_MONITOR_VALVE, &self->m_base,
                                  valveApplyFonts);
    XWidget_show(&self->m_base);
    return &self->m_base;
}

void HsfMonitorValve_adapt(XWidget* page, bool compact)
{
    HsfMonitorValvePage* self = (HsfMonitorValvePage*)page;
    int i;
    int cols;

    s_valve.m_compact = compact;
    if (!self) return;
    /* 页边距：窄屏收窄腾内容宽。 */
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               compact ? 12 : 24, compact ? 12 : 20,
                               compact ? 12 : 24, compact ? 12 : 16);
    /* [2026-11-05 中间宽度横向裁切根修] 列数不能写死为 3：
     * 每张卡片有 240px 最小宽，3 列 + 间距 + 页边距 需约 968px 窗口宽；
     * 窄于此时网格不缩、窗口又把横向滚动条关掉了
     * （HsfUi 对滚动壳设 AlwaysOff）→ 第 3 列被**硬裁**、文字只剩一半
     * （实测 900x600：「未配置」只显示一半）。
     * [2026-11-11 按卡片实测最小宽估列] 上版按常量 VALVE_CARD_MIN_W=240
     * 估「放得下」，但网格真正认的条目最小宽是
     * max(显式最小宽, 内容最小宽)（XLayoutItem.c
     * VXWidgetItem_minimumSize）：卡内提示行不换行时整行文本 ~494px，
     * 内容最小宽把 240 顶穿 → 3 列按 ~494 合计远超视口 1092，页根被
     * 滚动壳按最小宽撑开（XScrollArea.c xsa_updateWidgetGeometry 只按
     * minimumSizeHint 顶宽），第 3 列整列落在视口外被窗缘硬裁
     * （1280x800 实测电动球阀3/6 两卡不可达；1024x768 第 2 列「关阀」
     * 裁半）。字号 50~200% 缩放（valveApplyFonts）同样会推高内容最小宽。
     * 修法：perCol 改按**卡片实测 minimumSizeHint**（内容驱动，随字号/
     * 文案联动；基类虚槽走 XLayout_totalMinimumSize，不含卡片显式最小
     * 宽，正是网格条目最小宽里「内容那一半」的真值）与 240 兜底取大，
     * 估出几列放几列——宁少列不溢出；多列时再把显式最小宽设回 240，
     * 与估列同一口径。availW 同步改读**父容器宽**：滚动壳把页根挂在
     * viewport 下（XScrollArea_setWidget 的 XWidget_setParent(widget,
     * viewport)），页根被内容撑开时页根宽=撑开后宽（污染值，拿它估列
     * 会把溢出态当正常态维持住），viewport 宽才是真实显示宽；无滚动壳
     * （XScrollArea_create 失败直挂栈宿主，HsfUi 建页循环兜底）时父宽
     * 同样是真实可用宽。 */
    {
        XWidget* shellHost = XWidget_parentWidget(&self->m_base);
        int availW = shellHost ? XWidget_width(shellHost) : 0;
        int margins = compact ? (VALVE_MARGIN_SIDE_COMPACT * 2)
                              : (VALVE_MARGIN_SIDE_WIDE * 2);
        int cardMinW = 0;
        int perCol;
        int fit;
        if (availW <= 0)
            availW = XWidget_width(&self->m_base); /* 壳未定：回落页宽。 */
        for (i = 0; i < HSF_VALVE_COUNT; ++i) {
            XSize msh;
            if (!self->m_cards[i]) continue;
            msh = XWidget_minimumSizeHint_base(&self->m_cards[i]->m_base);
            if (msh.width > cardMinW) cardMinW = msh.width;
        }
        if (cardMinW < VALVE_CARD_MIN_W) cardMinW = VALVE_CARD_MIN_W;
        perCol = cardMinW + VALVE_GRID_SPACING;   /* 卡宽 + 一份间距。 */
        if (availW <= 0) {
            cols = compact ? 1 : VALVE_COLS_WIDE; /* 几何未定：用缺省。 */
        } else {
            fit = (availW - margins + VALVE_GRID_SPACING) / perCol;
            if (fit < 1) fit = 1;
            if (fit > VALVE_COLS_WIDE) fit = VALVE_COLS_WIDE;
            if (compact && fit > 1) fit = 1;      /* 紧凑形态强制单列。 */
            cols = fit;
        }
    }
    for (i = 0; i < s_valve.m_count; ++i) {
        if (!self->m_cards[i]) continue;
        XGridLayout_addWidget(&self->m_grid, &self->m_cards[i]->m_base,
                              i / cols, i % cols, 0);
    }
    if (cols == 1) {
        /* 单列：卡片铺满容器宽（避免 240px 卡在宽屏上空一半）。 */
        for (i = 0; i < s_valve.m_count; ++i) {
            if (self->m_cards[i])
                XWidget_setMinimumWidth(&self->m_cards[i]->m_base, 0);
        }
    } else {
        for (i = 0; i < s_valve.m_count; ++i) {
            if (self->m_cards[i])
                XWidget_setMinimumWidth(&self->m_cards[i]->m_base,
                                        VALVE_CARD_MIN_W);
        }
    }
    /* [2026-11-05 窄屏裁切根修] 网格宿主**不会自己上报内容高**：
     * 层布局只按"整数加权均分"给它分配剩余高度，而外层是
     * setWidgetResizable 的滚动壳 → 页面被拉成**一屏高**，
     * compact 下 6 张卡片竖排的 5 张超出部分被裁掉且无滚动条
     * （实测 480x700 只显示「电磁阀7」）。
     * 修法：按当前列数算出行数，直接给宿主一个**最小高**
     * （行数 × 卡高 + 行间距），滚动壳就能算出真实内容高。 */
    if (self->m_gridHost && s_valve.m_count > 0) {
        int rows = (s_valve.m_count + cols - 1) / cols;
        int needH = rows * VALVE_CARD_H
                    + (rows > 0 ? (rows - 1) * VALVE_GRID_SPACING : 0);
        XWidget_setMinimumHeight(self->m_gridHost, needH);
    }
    XLayout_activate((XLayout*)&self->m_layout);
}

/* ---- 数据喂入 ---- */

void HsfMonitorValve_setCount(int count)
{
    if (count < 0) count = 0;
    if (count > HSF_VALVE_COUNT) count = HSF_VALVE_COUNT;
    if (count == s_valve.m_count) return;
    s_valve.m_count = count;
    /* 路数变化：卡片数量要变。运行期极少发生（服务端配置定死），
     * 这里只刷已有卡片 + 标记新增卡片在下次建页时补齐。 */
    valveRefreshAll();
}

void HsfMonitorValve_setName(int index, const char* name)
{
    if (index < 0 || index >= HSF_VALVE_COUNT) return;
    if (!name || !name[0]) return;
    snprintf(s_valve.m_items[index].m_name,
             sizeof(s_valve.m_items[index].m_name), "%s", name);
}

void HsfMonitorValve_setFeedback(int index, bool openFb, bool closeFb)
{
    if (index < 0 || index >= HSF_VALVE_COUNT) return;
    s_valve.m_items[index].m_openFb = openFb;
    s_valve.m_items[index].m_closeFb = closeFb;
}

void HsfMonitorValve_setState(int index, int state)
{
    if (index < 0 || index >= HSF_VALVE_COUNT) return;
    s_valve.m_items[index].m_state = state;
}

void HsfMonitorValve_setReady(int index, bool ready)
{
    if (index < 0 || index >= HSF_VALVE_COUNT) return;
    s_valve.m_items[index].m_ready = ready;
}

void HsfMonitorValve_setValid(int index, bool valid)
{
    if (index < 0 || index >= HSF_VALVE_COUNT) return;
    s_valve.m_items[index].m_valid = valid;
}

void HsfMonitorValve_setText(int index, const char* text)
{
    if (index < 0 || index >= HSF_VALVE_COUNT) return;
    if (!text) text = "";
    snprintf(s_valve.m_items[index].m_text,
             sizeof(s_valve.m_items[index].m_text), "%s", text);
}

void HsfMonitorValve_refresh(void)
{
    valveRefreshAll();
}

/* ---- 控制闸 ---- */

void HsfMonitorValve_setControlEnabled(bool enabled, const char* blockedReason)
{
    /* [同值早退 2026-11-08] 闸门由 hsfWinApplyControlGates 在**每次阀推送**
     * 前重算并下发（20 次/秒）；值没变就不必重刷全部卡片。 */
    if (s_valve.m_controlEnabled == enabled &&
        strcmp(s_valve.m_blockedReason, blockedReason ? blockedReason : "") == 0)
        return;
    s_valve.m_controlEnabled = enabled;
    if (enabled || !blockedReason) {
        s_valve.m_blockedReason[0] = '\0';
    } else {
        snprintf(s_valve.m_blockedReason, sizeof(s_valve.m_blockedReason),
                 "%s", blockedReason);
    }
    valveRefreshAll();
}

bool HsfMonitorValve_controlEnabled(void)
{
    return s_valve.m_controlEnabled;
}

void HsfMonitorValve_setAlarmBanner(const char* text)
{
    /* [常驻横幅] text=NULL/空 清除；否则存原文（服务端文案直显，客户端
     * 不拼装）。禁用原因出现时由 valveRefreshBanner 的优先级压过它。 */
    if (!text || !text[0]) {
        s_valve.m_alarmText[0] = '\0';
    } else {
        snprintf(s_valve.m_alarmText, sizeof(s_valve.m_alarmText), "%s", text);
    }
    valveRefreshAll();
}

void HsfMonitorValve_setWriteHandler(HsfValveWriteFn fn, void* userData)
{
    s_valve.m_writeFn = fn;
    s_valve.m_writeUser = userData;
}

void HsfMonitorValve_attachStatus(void (*setStatus)(void*, const char*),
                                  void* userData)
{
    s_valve.m_setStatus = setStatus;
    s_valve.m_statusUser = userData;
}

const char* HsfMonitorValve_lastError(void)
{
    return s_valve.m_lastError;
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
