/**
 * @file       HsfHomePage.c
 * @brief      主界面页实现：三张总览卡片 + 设备状态表。
 * @details    层次（对标 Python 版 pages_other.py 的 HomePage）：
 *               1) 页头：大标题「主界面」+ 副标题「高速过滤器监控系统」
 *               2) 总览卡片行（横排三张，各占 1/3）：
 *                    · 电箱温度卡：电箱温度(℃) / 电箱湿度(%RH)
 *                    · 变频器卡  ：运行状态 / 输出频率(Hz) / 设定频率(Hz)
 *                    · 设备健康卡：设备总数 / 故障 / 离线
 *               3) 「设备状态」小节 + 4 列表格（设备/类型/状态/说明）
 *
 *             [为什么不做快捷入口按钮] 2026-11-05 用户决定：快捷导航只由
 *             Python 版提供，C 版主界面不再重复实现（左侧导航树已覆盖
 *             同一跳转能力，再做一遍是重复入口）。
 *
 *             [现场口径] 温湿度是**电箱内**读数（变送器装在电控柜里），
 *             不是环境温湿度；标题与字段名都带"电箱"，避免现场误读。
 *
 *             [数值块口径] 每块 = 上方灰色小字标题 + 下方大字号读数 + 单位
 *             （对标 Python StatBlock）。无数据一律显示 "--"，不显示 0——
 *             0 与"没数据"在工业界面上是两种含义，混起来会误导判断。
 */
#include "HsfHomePage.h"

#include "HsfTheme.h"
#include "HsfLinkClient.h"
#include "XhsfLinkProto.h"
#include "XLabel.h"
#include "XPushButton.h"
#include "XTableWidget.h"
#include "XHeaderView.h"
#include "XBoxLayout.h"
#include "XGridLayout.h"
#include "XObject.h"
#include "XTimer.h"
#include "XEvent.h"              /* XEVENT_TYPE_PAINT 等事件常量。 */
#include "XPainter.h"
#include "XWidget_Protected.h"   /* XWidget_paintImage/paintOffset（自绘平移）。 */
#include "XMemory.h"
#include "XFont.h"
#if XVFD_ON
#include "XVfdDevice.h"
#endif
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/* ==================== 配色（与主题同源） ==================== */

#define HOME_CLR_TEXT        0xFF1A1A1Au
#define HOME_CLR_TEXT_DIM    0xFF5A6572u
#define HOME_CLR_TITLE       0xFF1F4E79u
#define HOME_CLR_OK          0xFF2E7D32u
#define HOME_CLR_WARN        0xFFED6C02u
#define HOME_CLR_FAULT       0xFFC62828u
#define HOME_CLR_OFFLINE     0xFF757575u

/* ==================== 数值块 ==================== */

/**
 * @brief 数值块：灰色小标题 + 大字号读数 + 单位。
 * @details 对标 Python 侧 widgets.StatBlock。读数字号分两档（26/20/16），
 *          由创建时给定；改色走 setValue 的第二参。
 */
typedef struct HsfStatBlock
{
    XWidget m_base;      /**< 容器；第一个成员。 */
    XBoxLayout m_layout; /**< 竖排：标题 / 读数+单位。 */
    XLabel  m_caption;   /**< 小标题（灰）。 */
    XBoxLayout m_row;    /**< 读数行：读数 + 单位。 */
    XLabel  m_value;     /**< 大字号读数。 */
    XLabel  m_unit;      /**< 单位（小字，贴读数基线）。 */
    int     m_valueSize; /**< 读数字号（像素）。 */
} HsfStatBlock;

/** @brief 建数值块（堆分配，挂 parent）。 */
static HsfStatBlock* hsfStatCreate(XWidget* parent, const char* caption,
                                   const char* unit, const char* value,
                                   int valueSize)
{
    HsfStatBlock* sb;
    if (!parent) return NULL;
    sb = (HsfStatBlock*)XMemory_malloc(sizeof(HsfStatBlock),
                                       XCLASS_DEFAULT_MEMORY_TYPE);
    if (!sb) return NULL;
    memset(sb, 0, sizeof(*sb));
    XWidget_init(&sb->m_base, parent, 0);
    Set_Class_Memory(sb, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(sb, true);
    sb->m_valueSize = valueSize > 0 ? valueSize : 20;
    HsfTheme_applyDefaultFont(&sb->m_base);

    XBoxLayout_init(&sb->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&sb->m_layout, 0, 2, 0, 2);
    XLayout_setSpacing((XLayout*)&sb->m_layout, 2);
    XWidget_setLayout(&sb->m_base, (XLayout*)&sb->m_layout);

    XLabel_init(&sb->m_caption, &sb->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&sb->m_caption);
    XLabel_setTextPixelSize(&sb->m_caption, 12);
    XWidget_setForegroundRole((XWidget*)&sb->m_caption,
                              XPaletteColorRole_Mid);
    XLabel_setText_2(&sb->m_caption, caption ? caption : "");
    XWidget_show((XWidget*)&sb->m_caption);
    XBoxLayout_addWidget(&sb->m_layout, (XWidget*)&sb->m_caption);

    XBoxLayout_init(&sb->m_row, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&sb->m_row, 3);

    XLabel_init(&sb->m_value, &sb->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&sb->m_value);
    {
        XFont f = XWidget_font((XWidget*)&sb->m_value);
        XFont_setBold(&f, true);
        XWidget_setFont((XWidget*)&sb->m_value, &f);
        XClassDeinit(&f);
    }
    XLabel_setTextPixelSize(&sb->m_value, sb->m_valueSize);
    XLabel_setAlignment(&sb->m_value, XAlignment_Left | XAlignment_Bottom);
    XLabel_setText_2(&sb->m_value, value ? value : "--");
    XWidget_show((XWidget*)&sb->m_value);
    XBoxLayout_addWidget(&sb->m_row, (XWidget*)&sb->m_value);

    XLabel_init(&sb->m_unit, &sb->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&sb->m_unit);
    XLabel_setTextPixelSize(&sb->m_unit, 12);
    XLabel_setAlignment(&sb->m_unit, XAlignment_Left | XAlignment_Bottom);
    XWidget_setForegroundRole((XWidget*)&sb->m_unit, XPaletteColorRole_Mid);
    XLabel_setText_2(&sb->m_unit, unit ? unit : "");
    XWidget_show((XWidget*)&sb->m_unit);
    XBoxLayout_addWidget(&sb->m_row, (XWidget*)&sb->m_unit);
    XBoxLayout_addStretch(&sb->m_row, 1);
    XBoxLayout_addLayout(&sb->m_layout, (XLayout*)&sb->m_row);

    XWidget_show(&sb->m_base);
    return sb;
}

/** @brief 设读数文本（+ 可选强调色）。
 *  @note  XLabel 没有直接设色接口（只有调色板角色），故"正常/故障/离线"
 *         的强调色通过角色近似：故障/离线走 BrightText 之上另行处理成本高，
 *         这里统一用正文色，颜色语义由标题文字（正常/故障/离线）承担——
 *         与 Python 版"颜色+文字双编码"里的文字编码一致，不牺牲可读性。 */
static void hsfStatSet(HsfStatBlock* sb, const char* text, uint32_t color,
                       bool hasColor)
{
    (void)color;
    (void)hasColor;
    if (!sb) return;
    XLabel_setText_2(&sb->m_value, text ? text : "--");
    /* 文本变了但几何可能不变（定宽标签）⇒ 显式标脏，否则屏幕仍是旧值。 */
    XWidget_update((XWidget*)&sb->m_value);
}

/* ==================== 卡片容器 ==================== */

/** @brief 白底软描边卡片（自绘；内含竖向布局）。 */
typedef struct HsfHomeCard
{
    XWidget    m_base;   /**< 容器；第一个成员。 */
    XBoxLayout m_layout; /**< 竖排：标题 / 内容。 */
    XLabel     m_title;  /**< 卡片标题（品牌蓝粗体）。 */
    XBoxLayout m_body;   /**< 内容区布局。 */
} HsfHomeCard;

static void VHsfHomeCard_paintEvent(XWidget* self, XEvent* event);

XCLASS_DEFINE_BEGING(HsfHomeCardWidget)
XCLASS_DEFINE_EXTEND_END(HsfHomeCardWidget, XWidget)

XVtable* HsfHomeCard_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfHomeCardWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VHsfHomeCard_paintEvent);
    return XVTABLE_DEFAULT;
}

static void VHsfHomeCard_paintEvent(XWidget* self, XEvent* event)
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
    XPainter_setPen(&painter, 0xFFD5DBE2u);          /* 软描边。 */
    XPainter_setPenWidth(&painter, 1);
    XPainter_setBrush(&painter, 0xFFFFFFFFu);        /* 白底。 */
    XPainter_drawRoundedRect(&painter, &r, 5, 5);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 建卡片（title 为空则不建标题行）。 */
static HsfHomeCard* hsfCardCreate(XWidget* parent, const char* title)
{
    HsfHomeCard* card;
    if (!parent) return NULL;
    card = (HsfHomeCard*)XMemory_malloc(sizeof(HsfHomeCard),
                                        XCLASS_DEFAULT_MEMORY_TYPE);
    if (!card) return NULL;
    memset(card, 0, sizeof(*card));
    XWidget_init(&card->m_base, parent, 0);
    XClassGetVtable(card) = HsfHomeCard_class_init();
    Set_Class_Memory(card, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(card, true);
    HsfTheme_applyDefaultFont(&card->m_base);

    XBoxLayout_init(&card->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&card->m_layout, 14, 10, 14, 12);
    XLayout_setSpacing((XLayout*)&card->m_layout, 8);
    XWidget_setLayout(&card->m_base, (XLayout*)&card->m_layout);

    if (title && title[0]) {
        XLabel_init(&card->m_title, &card->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&card->m_title);
        {
            XFont f = XWidget_font((XWidget*)&card->m_title);
            XFont_setBold(&f, true);
            XWidget_setFont((XWidget*)&card->m_title, &f);
            XClassDeinit(&f);
        }
        XLabel_setTextPixelSize(&card->m_title, 14);
        XLabel_setText_2(&card->m_title, title);
        XWidget_show((XWidget*)&card->m_title);
        XBoxLayout_addWidget(&card->m_layout, (XWidget*)&card->m_title);
    }

    XBoxLayout_init(&card->m_body, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&card->m_body, 6);
    XBoxLayout_addLayout(&card->m_layout, (XLayout*)&card->m_body);
    XBoxLayout_addStretch(&card->m_layout, 1);

    XWidget_show(&card->m_base);
    return card;
}

/* ==================== 页对象 ==================== */

/** @brief 主界面页对象；m_base 必须是第一个成员。 */
typedef struct HsfHomePage
{
    XWidget      m_base;       /**< 页容器；第一个成员。 */
    XBoxLayout   m_layout;     /**< 页内垂直布局。 */
    XBoxLayout   m_headerRow;  /**< 页头行：大标题 + 副标题。 */
    XLabel       m_title;      /**< 大标题。 */
    XLabel       m_subtitle;   /**< 副标题。 */
    XBoxLayout   m_cardsRow;   /**< 三卡横排。 */
    HsfStatBlock* m_thTemp;    /**< 电箱温度。 */
    HsfStatBlock* m_thHum;     /**< 电箱湿度。 */
    HsfStatBlock* m_vfdState;  /**< 变频器运行状态。 */
    HsfStatBlock* m_vfdFreq;   /**< 输出频率。 */
    HsfStatBlock* m_vfdSet;    /**< 设定频率。 */
    HsfStatBlock* m_devTotal;  /**< 设备总数。 */
    HsfStatBlock* m_devFault;  /**< 故障数。 */
    HsfStatBlock* m_devOff;    /**< 离线数。 */
    XLabel       m_devTitle;   /**< 「设备状态」小节标题。 */
    XTableWidget m_table;      /**< 设备状态表（设备/类型/状态/说明）。 */
    int          m_tableRows;  /**< 当前表格行数（避免每拍重建）。 */
    HsfMenuNavFn m_navFn;      /**< 导航回调。 */
    void*        m_navUserData;/**< 回调令牌。 */
} HsfHomePage;

/* ---- 中文常量（UTF-8 转义；源文件按既有口径避免编码问题） ---- */

#define HOME_TXT_TITLE      "\xE4\xB8\xBB\xE7\x95\x8C\xE9\x9D\xA2"            /* 主界面 */
#define HOME_TXT_SUBTITLE   "\xE9\xAB\x98\xE9\x80\x9F\xE6\xBB\xA4\xE8\xBF\x87" \
                            "\xE5\x99\xA8\xE7\x9B\x91\xE6\x8E\xA7\xE7\xB3\xBB" \
                            "\xE7\xBB\x9F"                                      /* 高速过滤器监控系统 */
#define HOME_TXT_CARD_TH    "\xE7\x94\xB5\xE7\xAE\xB1\xE6\xB8\xA9\xE6\xB9\xBF\xE5\xBA\xA6"  /* 电箱温湿度 */
#define HOME_TXT_TH_TEMP    "\xE7\x94\xB5\xE7\xAE\xB1\xE6\xB8\xA9\xE5\xBA\xA6"  /* 电箱温度 */
#define HOME_TXT_TH_HUM     "\xE7\x94\xB5\xE7\xAE\xB1\xE6\xB9\xBF\xE5\xBA\xA6"  /* 电箱湿度 */
/* [2026-11-05 现场口径] 这路变频器驱动的是搅拌泵，卡片名按工艺用途叫。
 * 设备**类别**仍是 Vfd（kind=0），"类型"列照旧显示"变频器" —— 名字
 * 给操作员找设备、类别给维修判断该查什么，两者不混。 */
#define HOME_TXT_CARD_VFD   "\xE6\x90\x85\xE6\x8B\x8C\xE6\xB3\xB5"              /* 搅拌泵 */
#define HOME_TXT_VFD_STATE  "\xE8\xBF\x90\xE8\xA1\x8C\xE7\x8A\xB6\xE6\x80\x81"  /* 运行状态 */
#define HOME_TXT_VFD_OUT    "\xE8\xBE\x93\xE5\x87\xBA\xE9\xA2\x91\xE7\x8E\x87"  /* 输出频率 */
#define HOME_TXT_VFD_SET    "\xE8\xAE\xBE\xE5\xAE\x9A\xE9\xA2\x91\xE7\x8E\x87"  /* 设定频率 */
#define HOME_TXT_CARD_HEALTH "\xE8\xAE\xBE\xE5\xA4\x87\xE5\x81\xA5\xE5\xBA\xB7" /* 设备健康 */
#define HOME_TXT_DEV_TOTAL  "\xE8\xAE\xBE\xE5\xA4\x87\xE6\x80\xBB\xE6\x95\xB0"  /* 设备总数 */
#define HOME_TXT_DEV_FAULT  "\xE6\x95\x85\xE9\x9A\x9C"                          /* 故障 */
#define HOME_TXT_DEV_OFF    "\xE7\xA6\xBB\xE7\xBA\xBF"                          /* 离线 */
#define HOME_TXT_UNIT_TAI   "\xE5\x8F\xB0"                                      /* 台 */
#define HOME_TXT_ENTRY      "\xE5\xBF\xAB\xE6\x8D\xB7\xE5\x85\xA5\xE5\x8F\xA3"  /* 快捷入口 */
#define HOME_TXT_DEVSTATUS  "\xE8\xAE\xBE\xE5\xA4\x87\xE7\x8A\xB6\xE6\x80\x81"  /* 设备状态 */
#define HOME_TXT_NO_DATA    "\xE6\x97\xA0\xE6\x95\xB0\xE6\x8D\xAE"              /* 无数据 */
#define HOME_TXT_DASH       "--"

/* ---- 设备健康枚举中文（与服务端 HsfDeviceHealth 对齐） ---- */
static const char* hsfHomeHealthText(int health)
{
    switch (health) {
    case 0: return "\xE6\xAD\xA3\xE5\xB8\xB8";                     /* 正常 */
    case 1: return "\xE5\x91\x8A\xE8\xAD\xA6";                     /* 告警 */
    case 2: return "\xE6\x95\xB0\xE6\x8D\xAE\xE8\xBF\x87\xE6\x9C\x9F"; /* 数据过期 */
    case 3: return "\xE7\xA6\xBB\xE7\xBA\xBF";                     /* 离线 */
    case 4: return "\xE6\x95\x85\xE9\x9A\x9C";                     /* 故障 */
    default: return "\xE6\x9C\xAA\xE7\x9F\xA5";                    /* 未知 */
    }
}

/**
 * @brief 设备类别中文（与服务端 HsfDeviceKind / Python KIND_TEXT 对齐）。
 * @note  [实锅] 加电磁阀组时忘同步这张表，首页"类型"列直接显示"未知"。
 */
static const char* hsfHomeKindText(int kind)
{
    switch (kind) {
    case 0: return "\xE5\x8F\x98\xE9\xA2\x91\xE5\x99\xA8";        /* 变频器 */
    case 1: return "\xE6\xB8\xA9\xE6\xB9\xBF\xE5\xBA\xA6";        /* 温湿度 */
    case 2: return "\xE6\xB5\x81\xE9\x87\x8F\xE8\xAE\xA1";        /* 流量计 */
    case 3: return "\xE6\xA8\xA1\xE6\x8B\x9F\xE9\x87\x8F";        /* 模拟量 */
    case 4: return "485\xE6\x80\xBB\xE7\xBA\xBF";                  /* 485总线 */
    case 5: return "\xE6\x95\xB0\xE5\xAD\x97\xE9\x87\x8F";        /* 数字量 */
    case 6: return "\xE7\x94\xB5\xE7\xA3\x81\xE9\x98\x80";        /* 电磁阀 */
    case 7: return "\xE6\xB0\xB4\xE8\xB4\xA8";                  /* 水质 */
    default: return "\xE6\x9C\xAA\xE7\x9F\xA5";                    /* 未知 */
    }
}

#if XVFD_ON
/** @brief 变频器运行状态中文（与 Python RUN_STATE_TEXT 对齐）。 */
static const char* hsfHomeRunStateText(int st)
{
    switch (st) {
    case XVfdRunState_Stopped:         return "\xE5\x81\x9C\xE6\x9C\xBA";           /* 停机 */
    case XVfdRunState_Ready:           return "\xE5\xB0\xB1\xE7\xBB\xAA";           /* 就绪 */
    case XVfdRunState_RunningForward:  return "\xE6\xAD\xA3\xE8\xBD\xAC\xE8\xBF\x90\xE8\xA1\x8C"; /* 正转运行 */
    case XVfdRunState_RunningReverse:  return "\xE5\x8F\x8D\xE8\xBD\xAC\xE8\xBF\x90\xE8\xA1\x8C"; /* 反转运行 */
    case XVfdRunState_JogForward:      return "\xE6\xAD\xA3\xE8\xBD\xAC\xE7\x82\xB9\xE5\x8A\xA8"; /* 正转点动 */
    case XVfdRunState_JogReverse:      return "\xE5\x8F\x8D\xE8\xBD\xAC\xE7\x82\xB9\xE5\x8A\xA8"; /* 反转点动 */
    case XVfdRunState_Accelerating:    return "\xE5\x8A\xA0\xE9\x80\x9F\xE4\xB8\xAD"; /* 加速中 */
    case XVfdRunState_Decelerating:    return "\xE5\x87\x8F\xE9\x80\x9F\xE4\xB8\xAD"; /* 减速中 */
    case XVfdRunState_ConstantSpeed:   return "\xE6\x81\x92\xE9\x80\x9F\xE8\xBF\x90\xE8\xA1\x8C"; /* 恒速运行 */
    case XVfdRunState_DcBraking:       return "\xE7\x9B\xB4\xE6\xB5\x81\xE5\x88\xB6\xE5\x8A\xA8"; /* 直流制动 */
    case XVfdRunState_FreeStopping:    return "\xE8\x87\xAA\xE7\x94\xB1\xE5\x81\x9C\xE8\xBD\xA6"; /* 自由停车 */
    case XVfdRunState_QuickStopping:   return "\xE5\xBF\xAB\xE9\x80\x9F\xE5\x81\x9C\xE6\x9C\xBA"; /* 快速停机 */
    case XVfdRunState_Fault:           return "\xE6\x95\x85\xE9\x9A\x9C";           /* 故障 */
    default:                           return "\xE6\x9C\xAA\xE7\x9F\xA5";           /* 未知 */
    }
}
#endif

/* ==================== 刷新 ==================== */

/**
 * @brief 周期刷新三卡 + 设备表（遥控每拍调用）。
 * @details 无数据一律 "--"：0 与"没数据"是两种含义，不能混。
 */
static void hsfHomeRefresh(HsfHomePage* self)
{
    char buf[64];
    int i;

    if (!self) return;

    /* ---------- 电箱温湿度（A 侧） ---------- */
    {
        const HsfLinkTempHum* th = HsfLinkClient_tempHum(HSF_SIDE_A);
        bool fresh = HsfLinkClient_tempHumFresh(HSF_SIDE_A);
        if (th && fresh && th->m_tempValid) {
            snprintf(buf, sizeof(buf), "%.1f",
                     (double)th->m_tempC10 / 10.0);
            hsfStatSet(self->m_thTemp, buf, HOME_CLR_TEXT, false);
        } else {
            hsfStatSet(self->m_thTemp, HOME_TXT_DASH, HOME_CLR_TEXT, false);
        }
        if (th && fresh && th->m_humValid) {
            snprintf(buf, sizeof(buf), "%.1f",
                     (double)th->m_humPct10 / 10.0);
            hsfStatSet(self->m_thHum, buf, HOME_CLR_TEXT, false);
        } else {
            hsfStatSet(self->m_thHum, HOME_TXT_DASH, HOME_CLR_TEXT, false);
        }
    }

    /* ---------- 变频器 ---------- */
    {
#if XVFD_ON
        const XVfdStatus* st = HsfLinkClient_vfdStatus();
        if (st && st->m_valid && HsfLinkClient_vfdValid()) {
            uint32_t color = HOME_CLR_TEXT;
            if (st->m_faulted) color = HOME_CLR_FAULT;
            else if (st->m_running) color = HOME_CLR_OK;
            hsfStatSet(self->m_vfdState, hsfHomeRunStateText((int)st->m_runState),
                       color, true);
            if (st->m_validMask & XVfdField_OutputFreqHz) {
                snprintf(buf, sizeof(buf), "%.2f", st->m_outputFreqHz);
                hsfStatSet(self->m_vfdFreq, buf, HOME_CLR_TEXT, false);
            } else {
                hsfStatSet(self->m_vfdFreq, HOME_TXT_DASH, HOME_CLR_TEXT, false);
            }
            if (st->m_validMask & XVfdField_SetFreqHz) {
                snprintf(buf, sizeof(buf), "%.2f", st->m_setFreqHz);
                hsfStatSet(self->m_vfdSet, buf, HOME_CLR_TEXT, false);
            } else {
                hsfStatSet(self->m_vfdSet, HOME_TXT_DASH, HOME_CLR_TEXT, false);
            }
        } else
#endif
        {
            hsfStatSet(self->m_vfdState, HOME_TXT_NO_DATA, HOME_CLR_OFFLINE, true);
            hsfStatSet(self->m_vfdFreq, HOME_TXT_DASH, HOME_CLR_TEXT, false);
            hsfStatSet(self->m_vfdSet, HOME_TXT_DASH, HOME_CLR_TEXT, false);
        }
    }

    /* ---------- 设备健康 + 设备表 ---------- */
    {
        HsfLinkDeviceHealth devs[HSF_LINK_DEV_MAX];
        int n = HsfLinkClient_deviceHealth(devs, HSF_LINK_DEV_MAX, NULL);
        int fault = 0;
        int offline = 0;

        for (i = 0; i < n; ++i) {
            if (devs[i].m_health == 4) ++fault;
            else if (devs[i].m_health == 3) ++offline;
        }
        snprintf(buf, sizeof(buf), "%d", n > 0 ? n : 0);
        hsfStatSet(self->m_devTotal, buf, HOME_CLR_TEXT, false);
        snprintf(buf, sizeof(buf), "%d", fault);
        hsfStatSet(self->m_devFault, buf,
                   fault ? HOME_CLR_FAULT : HOME_CLR_OK, true);
        snprintf(buf, sizeof(buf), "%d", offline);
        hsfStatSet(self->m_devOff, buf,
                   offline ? HOME_CLR_WARN : HOME_CLR_OK, true);

        /* ---- 表格：行数变了才重建，否则只改文本（避免每拍重建闪）。 ---- */
        if (self->m_tableRows != n) {
            int c;
            XTableWidget_setRowCount(&self->m_table, n);
            self->m_tableRows = n;
            /* [必须补 item] setRowCount 只增行不加格；不建 item 直接写文本会
             * 静默失败（表格空白）。行数变化时一次性建满，之后只改文本。 */
            for (i = 0; i < n; ++i) {
                for (c = 0; c < 4; ++c) {
                    XTableWidgetItem* it = XTableWidgetItem_create_utf8("", 0);
                    if (it) XTableWidget_setItem(&self->m_table, i, c, it);
                }
            }
        }
        for (i = 0; i < n; ++i) {
            XTableWidgetItem* it;
            it = XTableWidget_item(&self->m_table, i, 0);
            XTableWidgetItem_setText_utf8(it, devs[i].m_name[0] ? devs[i].m_name : "-");
            it = XTableWidget_item(&self->m_table, i, 1);
            XTableWidgetItem_setText_utf8(it, hsfHomeKindText(devs[i].m_kind));
            it = XTableWidget_item(&self->m_table, i, 2);
            XTableWidgetItem_setText_utf8(it, hsfHomeHealthText(devs[i].m_health));
            it = XTableWidget_item(&self->m_table, i, 3);
            XTableWidgetItem_setText_utf8(it, devs[i].m_text[0] ? devs[i].m_text : "-");
        }
        XWidget_update((XWidget*)&self->m_table);
    }
}

/* ==================== 创建 ==================== */

XWidget* HsfHomePage_create(XWidget* parent, HsfMenuNavFn navFn,
                            void* userData)
{
    HsfHomePage* self;
    int i;
    static uint32_t const kActionSlots[3] = { 0, 0, 0 };
    (void)kActionSlots;

    if (!parent) return NULL;
    self = (HsfHomePage*)XMemory_malloc(sizeof(HsfHomePage),
                                        XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(HsfHomePage));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);   /* 整块随主窗级联释放。 */
    self->m_navFn = navFn;
    self->m_navUserData = userData;
    HsfTheme_applyDefaultFont(&self->m_base);

    /* ---- 页内垂直布局 ---- */
    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 20, 16, 20, 12);
    XLayout_setSpacing((XLayout*)&self->m_layout, 10);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);

    /* ---- 页头：大标题 + 副标题 ---- */
    XBoxLayout_init(&self->m_headerRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&self->m_headerRow, 10);
    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    {
        XFont f = XWidget_font((XWidget*)&self->m_title);
        XFont_setBold(&f, true);
        XWidget_setFont((XWidget*)&self->m_title, &f);
        XClassDeinit(&f);
    }
    XLabel_setTextPixelSize(&self->m_title, 17);
    XLabel_setText_2(&self->m_title, HOME_TXT_TITLE);
    XWidget_show((XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_headerRow, (XWidget*)&self->m_title);

    XLabel_init(&self->m_subtitle, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_subtitle);
    XLabel_setTextPixelSize(&self->m_subtitle, 12);
    XLabel_setAlignment(&self->m_subtitle,
                        XAlignment_Left | XAlignment_Bottom);
    XWidget_setForegroundRole((XWidget*)&self->m_subtitle,
                              XPaletteColorRole_Mid);
    XLabel_setText_2(&self->m_subtitle, HOME_TXT_SUBTITLE);
    XWidget_show((XWidget*)&self->m_subtitle);
    XBoxLayout_addWidget(&self->m_headerRow, (XWidget*)&self->m_subtitle);
    XBoxLayout_addStretch(&self->m_headerRow, 1);
    XBoxLayout_addLayout(&self->m_layout, (XLayout*)&self->m_headerRow);

    /* ---- 三卡横排 ---- */
    XBoxLayout_init(&self->m_cardsRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&self->m_cardsRow, 10);
    {
        /* 卡 1：电箱温度（两列数值块） */
        HsfHomeCard* thCard = hsfCardCreate(&self->m_base, HOME_TXT_CARD_TH);
        HsfHomeCard* vfdCard = hsfCardCreate(&self->m_base, HOME_TXT_CARD_VFD);
        HsfHomeCard* hCard = hsfCardCreate(&self->m_base, HOME_TXT_CARD_HEALTH);
        XBoxLayout* thRow;
        XBoxLayout* vfdRow;
        XBoxLayout* hRow;

        /* --- 电箱温度卡：温度 / 湿度 横排 --- */
        if (thCard) {
            thRow = (XBoxLayout*)XMemory_malloc(sizeof(XBoxLayout),
                                                XCLASS_DEFAULT_MEMORY_TYPE);
            if (thRow) {
                XBoxLayout_init(thRow, XBoxLayoutDirection_LeftToRight);
                XLayout_setSpacing((XLayout*)thRow, 16);
                self->m_thTemp = hsfStatCreate(&thCard->m_base, HOME_TXT_TH_TEMP,
                                               "\xE2\x84\x83", HOME_TXT_DASH, 26);
                self->m_thHum = hsfStatCreate(&thCard->m_base, HOME_TXT_TH_HUM,
                                              "%RH", HOME_TXT_DASH, 26);
                if (self->m_thTemp)
                    XBoxLayout_addWidgetEx(thRow, (XWidget*)&self->m_thTemp->m_base, 1, 0);
                if (self->m_thHum)
                    XBoxLayout_addWidgetEx(thRow, (XWidget*)&self->m_thHum->m_base, 1, 0);
                XBoxLayout_addLayout(&thCard->m_body, (XLayout*)thRow);
            }
            XBoxLayout_addWidgetEx(&self->m_cardsRow, &thCard->m_base, 1, 0);
        }

        /* --- 变频器卡：状态行 + 频率/设定两列 --- */
        if (vfdCard) {
            self->m_vfdState = hsfStatCreate(&vfdCard->m_base, HOME_TXT_VFD_STATE,
                                             "", HOME_TXT_DASH, 20);
            if (self->m_vfdState)
                XBoxLayout_addWidget(&vfdCard->m_body, (XWidget*)&self->m_vfdState->m_base);
            vfdRow = (XBoxLayout*)XMemory_malloc(sizeof(XBoxLayout),
                                                 XCLASS_DEFAULT_MEMORY_TYPE);
            if (vfdRow) {
                XBoxLayout_init(vfdRow, XBoxLayoutDirection_LeftToRight);
                XLayout_setSpacing((XLayout*)vfdRow, 16);
                self->m_vfdFreq = hsfStatCreate(&vfdCard->m_base, HOME_TXT_VFD_OUT,
                                                "Hz", HOME_TXT_DASH, 26);
                self->m_vfdSet = hsfStatCreate(&vfdCard->m_base, HOME_TXT_VFD_SET,
                                               "Hz", HOME_TXT_DASH, 16);
                if (self->m_vfdFreq)
                    XBoxLayout_addWidgetEx(vfdRow, (XWidget*)&self->m_vfdFreq->m_base, 1, 0);
                if (self->m_vfdSet)
                    XBoxLayout_addWidgetEx(vfdRow, (XWidget*)&self->m_vfdSet->m_base, 1, 0);
                XBoxLayout_addLayout(&vfdCard->m_body, (XLayout*)vfdRow);
            }
            XBoxLayout_addWidgetEx(&self->m_cardsRow, &vfdCard->m_base, 1, 0);
        }

        /* --- 设备健康卡：总数 / 故障 / 离线 --- */
        if (hCard) {
            hRow = (XBoxLayout*)XMemory_malloc(sizeof(XBoxLayout),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
            if (hRow) {
                XBoxLayout_init(hRow, XBoxLayoutDirection_LeftToRight);
                XLayout_setSpacing((XLayout*)hRow, 16);
                self->m_devTotal = hsfStatCreate(&hCard->m_base, HOME_TXT_DEV_TOTAL,
                                                 HOME_TXT_UNIT_TAI, HOME_TXT_DASH, 22);
                self->m_devFault = hsfStatCreate(&hCard->m_base, HOME_TXT_DEV_FAULT,
                                                 HOME_TXT_UNIT_TAI, "0", 22);
                self->m_devOff = hsfStatCreate(&hCard->m_base, HOME_TXT_DEV_OFF,
                                               HOME_TXT_UNIT_TAI, "0", 22);
                if (self->m_devTotal)
                    XBoxLayout_addWidgetEx(hRow, (XWidget*)&self->m_devTotal->m_base, 1, 0);
                if (self->m_devFault)
                    XBoxLayout_addWidgetEx(hRow, (XWidget*)&self->m_devFault->m_base, 1, 0);
                if (self->m_devOff)
                    XBoxLayout_addWidgetEx(hRow, (XWidget*)&self->m_devOff->m_base, 1, 0);
                XBoxLayout_addLayout(&hCard->m_body, (XLayout*)hRow);
            }
            XBoxLayout_addWidgetEx(&self->m_cardsRow, &hCard->m_base, 1, 0);
        }
    }
    XBoxLayout_addLayout(&self->m_layout, (XLayout*)&self->m_cardsRow);

    /* ---- 设备状态表 ---- */
    XLabel_init(&self->m_devTitle, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_devTitle);
    {
        XFont f = XWidget_font((XWidget*)&self->m_devTitle);
        XFont_setBold(&f, true);
        XWidget_setFont((XWidget*)&self->m_devTitle, &f);
        XClassDeinit(&f);
    }
    XLabel_setTextPixelSize(&self->m_devTitle, 14);
    XLabel_setText_2(&self->m_devTitle, HOME_TXT_DEVSTATUS);
    XWidget_show((XWidget*)&self->m_devTitle);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_devTitle);

    XTableWidget_init(&self->m_table, &self->m_base, 0);
    XTableWidget_setColumnCount(&self->m_table, 4);
    {
        static const char* const kHeaders[4] = {
            "\xE8\xAE\xBE\xE5\xA4\x87",   /* 设备 */
            "\xE7\xB1\xBB\xE5\x9E\x8B",   /* 类型 */
            "\xE7\x8A\xB6\xE6\x80\x81",   /* 状态 */
            "\xE8\xAF\xB4\xE6\x98\x8E"    /* 说明 */
        };
        XTableWidget_setHorizontalHeaderLabels(&self->m_table, kHeaders, 4);
    }
    XTableWidget_setRowCount(&self->m_table, 0);
    self->m_tableRows = 0;
    {
        /* 列宽 Stretch：各列均分、随窗口拉伸（对标 Qt QHeaderView::Stretch）。 */
        XHeaderView* hh = XTableView_horizontalHeader(&self->m_table.m_base);
        if (hh) XHeaderView_setSectionResizeMode(hh, XHeaderViewResizeMode_Stretch);
    }
    XWidget_show((XWidget*)&self->m_table);
    XBoxLayout_addWidgetEx(&self->m_layout, (XWidget*)&self->m_table, 1, 0);
    XBoxLayout_addStretch(&self->m_layout, 0);

    /* ---- 首拍刷新（避免开局全 "--" 到下一拍） ---- */
    hsfHomeRefresh(self);

    XWidget_show(&self->m_base);
    return &self->m_base;
}

/* ==================== 公开刷新入口 ==================== */

void HsfHomePage_refresh(XWidget* page)
{
    hsfHomeRefresh((HsfHomePage*)page);
}

/* ==================== 形态自适应 ==================== */

void HsfHomePage_adapt(XWidget* page, bool compact)
{
    HsfHomePage* self = (HsfHomePage*)page;
    int i;
    if (!self) return;

    /* 三卡：宽屏横排 / 窄屏竖排。 */
    XBoxLayout_setDirection(&self->m_cardsRow,
                            compact ? XBoxLayoutDirection_TopToBottom
                                    : XBoxLayoutDirection_LeftToRight);
    /* 页边距：窄屏收窄腾内容宽。 */
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               compact ? 12 : 20, compact ? 12 : 16,
                               compact ? 12 : 20, compact ? 8 : 12);
    XLayout_activate((XLayout*)&self->m_layout);
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
