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
#include "HsfUi.h"   /* hsfUi_pageFontPx（本地设置·按页字号） */
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
#include "XColor.h"
#include "XDateTime.h"           /* 压差卡新鲜度（自记 lastMs 口径）。 */
#include "XSystem.h"             /* 环境变量自测钩子（XHSF_HOME_AUTOTEST）。 */
#if XVFD_ON
#include "XVfdDevice.h"
#endif
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/** @brief 本页字号折算（本地设置 FontScale/Page0）。 */
static int homePx(int px)
{
    return hsfUi_pageFontPx(HSF_PAGE_HOME, px);
}

/* ==================== 配色（与主题同源） ==================== */

#define HOME_CLR_TEXT        0xFF1A1A1Au
#define HOME_CLR_TEXT_DIM    0xFF5A6572u
#define HOME_CLR_TITLE       0xFF1F4E79u
#define HOME_CLR_OK          0xFF2E7D32u
#define HOME_CLR_WARN        0xFFED6C02u
#define HOME_CLR_FAULT       0xFFC62828u
#define HOME_CLR_OFFLINE     0xFF757575u

/* 设备表观感（对齐 Python theme.TABLE_ROW_H 与 DataTable 的做法）。 */
#define HOME_ROW_H           32   /**< 行高（px；Python theme.TABLE_ROW_H）。 */
#define HOME_HEAD_H          34   /**< 表头高（px；行高 + 2px 分隔）。 */
#define HOME_ALIGN_CENTER    XPAINTER_TEXT_ALIGN_CENTER  /**< 居中（Qt AlignCenter 口径）。 */

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
    XLabel_setTextPixelSize(&sb->m_caption, homePx(12));
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
    XLabel_setTextPixelSize(&sb->m_value, homePx(sb->m_valueSize));
    XLabel_setAlignment(&sb->m_value, XAlignment_Left | XAlignment_Bottom);
    XLabel_setText_2(&sb->m_value, value ? value : "--");
    XWidget_show((XWidget*)&sb->m_value);
    XBoxLayout_addWidget(&sb->m_row, (XWidget*)&sb->m_value);

    XLabel_init(&sb->m_unit, &sb->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&sb->m_unit);
    XLabel_setTextPixelSize(&sb->m_unit, homePx(12));
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
    /* [不再显式标脏 2026-11-08] 原实现末尾补一句 XWidget_update，理由写的是
     * 「文本变了但几何可能不变（定宽标签）⇒ 显式标脏」。这个前提不成立：
     * XLabel_setText → label_updateLabel 末尾**无条件**调了
     * XWidget_updateGeometry + XWidget_update（XLabel.c:2804-2805），文本变化
     * 时该标的脏已经标了；而文本**没变**时 XLabel_setText 在首行早退
     * （XLabel.c:2929-2931），这句话反而把"无变化"硬标成脏区。
     * 本函数由帧泵按 200ms 调用、每次刷 15 个数值块 ⇒ 无变化时也在 5Hz
     * 造脏，是主界面空转帧的主要来源之一（画像实测主界面稳态 15fps、
     * 单帧仅 8 千像素：画得又快又没内容，纯属被唤醒）。 */
    XLabel_setText_2(&sb->m_value, text ? text : "--");
}

/* ==================== 卡片容器 ==================== */

/** @brief 白底软描边卡片（自绘；内含竖向布局；可选整卡点击）。 */
typedef struct HsfHomeCard
{
    XWidget    m_base;   /**< 容器；第一个成员。 */
    XBoxLayout m_layout; /**< 竖排：标题 / 内容。 */
    XLabel     m_title;  /**< 卡片标题（品牌蓝粗体）。 */
    XBoxLayout m_body;   /**< 内容区布局。 */
    /* [2026-11-06 可点击卡片] 整卡点击（鼠标**按下**即回调，触控口径——
     * 与 HsfTitleBar ☰/模式区同款；Release+命中语义见 HsfMonitorAnalog
     * 卡片，两态按钮才需要）。fn 为空 = 不可点，事件透传父类。 */
    void (*m_clickFn)(void*);   /**< 点击回调（按下即触发）。 */
    void*      m_clickUserData; /**< 回调令牌。 */
    int        m_glyph;         /**< 设备图形（HsfHomeGlyph；0=无）。 */
} HsfHomeCard;

static void VHsfHomeCard_paintEvent(XWidget* self, XEvent* event);
static void VHsfHomeCard_mousePressEvent(XWidget* self, XEvent* event);

/* ==================== 设备矢量图形（卡片右上角水印式简笔） ==================== */

/** 设备图形种类（代码绘制的矢量简笔——无外部素材依赖，风格统一；
 *  后续如需更精细的图标可从矢量图库导 PNG 走 XImage_load_2 运行时加载）。 */
typedef enum HsfHomeGlyph {
    HOME_GLYPH_NONE = 0,
    HOME_GLYPH_TH_HUM,     /* 温湿度：温度计 + 湿度水滴波纹 */
    HOME_GLYPH_PUMP,       /* 搅拌泵：叶轮圆 + 出口管 */
    HOME_GLYPH_HEALTH,     /* 设备健康：盾牌 + 对勾 */
    HOME_GLYPH_PRESSURE,   /* 压差：表盘 + 指针 + 底接口 */
    HOME_GLYPH_FLOW,       /* 流量计：管路 + 叶轮 + 来流箭头 */
    HOME_GLYPH_VALVE       /* 电磁阀：阀体蝶形 + 线圈块 */
} HsfHomeGlyph;

#define HOME_GLYPH_STROKE  0xFF9DBCE8u   /* 图形描边（软蓝）。 */
#define HOME_GLYPH_FILL    0xFFE3EDFBu   /* 图形浅填充。 */
#define HOME_GLYPH_ACCENT  0xFF7EA6E0u   /* 图形强调（水银/指针等）。 */

/** @brief 统一笔触：软蓝 2px + 浅填充 + 抗锯齿（每个图形绘制前调用）。 */
static void hsfGlyphPen(XPainter* p, uint32_t color)
{
    XPainter_setPen(p, color);
    XPainter_setPenWidth(p, 2);
    XPainter_setBrush(p, HOME_GLYPH_FILL);
    XPainter_setRenderHint(p, XPainterRenderHint_Antialiasing, true);
}

/* ---- 温湿度：温度计（左）+ 湿度水滴与波纹（右）---- */
static void hsfGlyphThHum(XPainter* p, int gx, int gy, int s)
{
    XRect r;
    XPoint tri[3];
    int cx = gx + s * 24 / 100;
    int dx = gx + s * 68 / 100;
    int dy = gy + s * 58 / 100;
    int k;

    hsfGlyphPen(p, HOME_GLYPH_STROKE);
    /* 温度计：管 + 球泡。 */
    XRect_init(&r, cx - s * 9 / 100, gy + s * 6 / 100,
               s * 18 / 100, s * 48 / 100);
    XPainter_drawRoundedRect(p, &r, s * 9 / 100, s * 9 / 100);
    XRect_init(&r, cx - s * 16 / 100, gy + s * 50 / 100,
               s * 32 / 100, s * 32 / 100);
    XPainter_drawEllipse(p, &r);
    /* 水银柱（强调色实心）。 */
    XPainter_setPen(p, HOME_GLYPH_ACCENT);
    XPainter_setBrush(p, HOME_GLYPH_ACCENT);
    XRect_init(&r, cx - s * 3 / 100, gy + s * 34 / 100,
               s * 6 / 100, s * 32 / 100);
    XPainter_drawRoundedRect(p, &r, s * 3 / 100, s * 3 / 100);
    XPainter_drawEllipse(p, &r);
    /* 湿度：水滴（三角 + 圆）。 */
    hsfGlyphPen(p, HOME_GLYPH_STROKE);
    tri[0].x = dx;                tri[0].y = gy + s * 26 / 100;
    tri[1].x = dx + s * 14 / 100; tri[1].y = dy - s * 6 / 100;
    tri[2].x = dx - s * 14 / 100; tri[2].y = dy - s * 6 / 100;
    XPainter_drawPolyline(p, tri, 3);
    XRect_init(&r, dx - s * 11 / 100, dy - s * 8 / 100,
               s * 22 / 100, s * 20 / 100);
    XPainter_drawEllipse(p, &r);
    /* 波纹两条。 */
    for (k = 0; k < 2; ++k) {
        XPoint w[4];
        int y0 = dy + s * 22 / 100 + k * s * 9 / 100;
        w[0].x = dx - s * 14 / 100; w[0].y = y0;
        w[1].x = dx - s * 5 / 100;  w[1].y = y0 - s * 5 / 100;
        w[2].x = dx + s * 4 / 100;  w[2].y = y0;
        w[3].x = dx + s * 13 / 100; w[3].y = y0 - s * 5 / 100;
        XPainter_drawPolyline(p, w, 4);
    }
}

/* ---- 搅拌泵：叶轮圆 + 三叶片 + 出口管 + 底座 ---- */
static void hsfGlyphPump(XPainter* p, int gx, int gy, int s)
{
    XRect r;
    int cx = gx + s * 44 / 100;
    int cy = gy + s * 56 / 100;
    int rad = s * 30 / 100;
    hsfGlyphPen(p, HOME_GLYPH_STROKE);
    XRect_init(&r, cx - rad, cy - rad, rad * 2, rad * 2);
    XPainter_drawEllipse(p, &r);
    /* 三叶片：自 hub 向外三方向。 */
    XPainter_drawLine(p, cx, cy, cx + rad, cy);
    XPainter_drawLine(p, cx, cy, cx - rad / 2, cy - rad * 86 / 100);
    XPainter_drawLine(p, cx, cy, cx - rad / 2, cy + rad * 86 / 100);
    XRect_init(&r, cx - s * 5 / 100, cy - s * 5 / 100,
               s * 10 / 100, s * 10 / 100);
    XPainter_drawEllipse(p, &r);
    /* 出口管：顶部立管 + 弯头。 */
    XPainter_drawLine(p, cx, cy - rad, cx, gy + s * 8 / 100);
    XPainter_drawLine(p, cx, gy + s * 8 / 100,
                      cx + s * 18 / 100, gy + s * 8 / 100);
    /* 底座。 */
    XPainter_drawLine(p, cx - s * 22 / 100, cy + rad + s * 6 / 100,
                      cx + s * 22 / 100, cy + rad + s * 6 / 100);
}

/* ---- 设备健康：盾牌 + 对勾 ---- */
static void hsfGlyphHealth(XPainter* p, int gx, int gy, int s)
{
    XPoint shield[5];
    XPoint check[3];
    shield[0].x = gx + s * 50 / 100; shield[0].y = gy + s * 8 / 100;
    shield[1].x = gx + s * 84 / 100; shield[1].y = gy + s * 22 / 100;
    shield[2].x = gx + s * 84 / 100; shield[2].y = gy + s * 52 / 100;
    shield[3].x = gx + s * 50 / 100; shield[3].y = gy + s * 90 / 100;
    shield[4].x = gx + s * 16 / 100; shield[4].y = gy + s * 52 / 100;
    hsfGlyphPen(p, HOME_GLYPH_STROKE);
    XPainter_drawPolygon(p, shield, 5, true);
    /* 对勾（强调色粗一点）。 */
    XPainter_setPen(p, HOME_GLYPH_ACCENT);
    XPainter_setPenWidth(p, 3);
    check[0].x = gx + s * 34 / 100; check[0].y = gy + s * 46 / 100;
    check[1].x = gx + s * 46 / 100; check[1].y = gy + s * 60 / 100;
    check[2].x = gx + s * 70 / 100; check[2].y = gy + s * 32 / 100;
    XPainter_drawPolyline(p, check, 3);
}

/* ---- 压差：表盘 + 刻度 + 指针 + 底接口 ---- */
static void hsfGlyphPressure(XPainter* p, int gx, int gy, int s)
{
    XRect r;
    int cx = gx + s * 50 / 100;
    int cy = gy + s * 46 / 100;
    int rad = s * 32 / 100;
    hsfGlyphPen(p, HOME_GLYPH_STROKE);
    XRect_init(&r, cx - rad, cy - rad, rad * 2, rad * 2);
    XPainter_drawEllipse(p, &r);
    /* 刻度：三根短针（左/上/右）。 */
    XPainter_drawLine(p, cx - rad, cy, cx - rad + s * 8 / 100, cy);
    XPainter_drawLine(p, cx, cy - rad, cx, cy - rad + s * 8 / 100);
    XPainter_drawLine(p, cx + rad, cy, cx + rad - s * 8 / 100, cy);
    /* 指针（强调色）：指向左上 45°。 */
    XPainter_setPen(p, HOME_GLYPH_ACCENT);
    XPainter_setPenWidth(p, 3);
    XPainter_drawLine(p, cx, cy, cx - rad * 60 / 100, cy - rad * 60 / 100);
    XPainter_setPen(p, HOME_GLYPH_STROKE);
    XPainter_setPenWidth(p, 2);
    XRect_init(&r, cx - s * 4 / 100, cy - s * 4 / 100,
               s * 8 / 100, s * 8 / 100);
    XPainter_drawEllipse(p, &r);
    /* 底接口。 */
    XPainter_drawLine(p, cx, cy + rad, cx, gy + s * 92 / 100);
}

/* ---- 流量计：管路 + 叶轮 + 来流箭头 ---- */
static void hsfGlyphFlow(XPainter* p, int gx, int gy, int s)
{
    XRect r;
    XPoint a[3];
    int cx = gx + s * 58 / 100;
    int cy = gy + s * 50 / 100;
    int rad = s * 20 / 100;
    int k;
    hsfGlyphPen(p, HOME_GLYPH_STROKE);
    /* 管路：上下沿 + 两端法兰。 */
    XPainter_drawLine(p, gx + s * 6 / 100, gy + s * 32 / 100,
                      gx + s * 94 / 100, gy + s * 32 / 100);
    XPainter_drawLine(p, gx + s * 6 / 100, gy + s * 68 / 100,
                      gx + s * 94 / 100, gy + s * 68 / 100);
    XPainter_drawLine(p, gx + s * 6 / 100, gy + s * 26 / 100,
                      gx + s * 6 / 100, gy + s * 74 / 100);
    XPainter_drawLine(p, gx + s * 94 / 100, gy + s * 26 / 100,
                      gx + s * 94 / 100, gy + s * 74 / 100);
    /* 叶轮（管中圆 + 三辐条）。 */
    XRect_init(&r, cx - rad, cy - rad, rad * 2, rad * 2);
    XPainter_drawEllipse(p, &r);
    {
        /* 三向辐条（120° 间隔，端点按角度近似）。 */
        const int dxs[3] = { 0, -86, 86 };
        const int dys[3] = { -100, 50, 50 };
        for (k = 0; k < 3; ++k)
            XPainter_drawLine(p, cx, cy,
                              cx + rad * dxs[k] / 100,
                              cy + rad * dys[k] / 100);
    }
    /* 来流箭头：两道 ">" 折线。 */
    for (k = 0; k < 2; ++k) {
        int x0 = gx + s * 16 / 100 + k * s * 12 / 100;
        a[0].x = x0;               a[0].y = cy - s * 8 / 100;
        a[1].x = x0 + s * 8 / 100; a[1].y = cy;
        a[2].x = x0;               a[2].y = cy + s * 8 / 100;
        XPainter_drawPolyline(p, a, 3);
    }
}

/* ---- 电磁阀：管线 + 蝶形阀体 + 阀杆 + 线圈块 ---- */
/** @brief 阀简笔（颜色参数化版本——瓷贴小图随状态着色用）。 */
static void hsfGlyphValveColor(XPainter* p, int gx, int gy, int s,
                               uint32_t stroke, uint32_t fill)
{
    XPoint bodyL[3];
    XPoint bodyR[3];
    XRect r;
    int cx = gx + s * 50 / 100;
    int cy = gy + s * 62 / 100;
    int k;
    hsfGlyphPen(p, stroke);
    XPainter_setBrush(p, fill);
    /* 管线：左右两段水平线。 */
    XPainter_drawLine(p, gx + s * 6 / 100, cy, cx - s * 20 / 100, cy);
    XPainter_drawLine(p, cx + s * 20 / 100, cy, gx + s * 94 / 100, cy);
    /* 蝶形阀体（两三角相对）。 */
    bodyL[0].x = cx - s * 20 / 100; bodyL[0].y = cy - s * 16 / 100;
    bodyL[1].x = cx - s * 20 / 100; bodyL[1].y = cy + s * 16 / 100;
    bodyL[2].x = cx;                bodyL[2].y = cy;
    XPainter_drawPolygon(p, bodyL, 3, true);
    bodyR[0].x = cx + s * 20 / 100; bodyR[0].y = cy - s * 16 / 100;
    bodyR[1].x = cx + s * 20 / 100; bodyR[1].y = cy + s * 16 / 100;
    bodyR[2].x = cx;                bodyR[2].y = cy;
    XPainter_drawPolygon(p, bodyR, 3, true);
    /* 阀杆 + 线圈块（圆角矩形 + 三根绕线短线）。 */
    XPainter_drawLine(p, cx, cy, cx, gy + s * 30 / 100);
    XRect_init(&r, cx - s * 14 / 100, gy + s * 8 / 100,
               s * 28 / 100, s * 22 / 100);
    XPainter_drawRoundedRect(p, &r, s * 4 / 100, s * 4 / 100);
    for (k = 0; k < 3; ++k) {
        int lx = cx - s * 7 / 100 + k * s * 7 / 100;
        XPainter_drawLine(p, lx, gy + s * 12 / 100, lx, gy + s * 26 / 100);
    }
}

static void hsfGlyphValve(XPainter* p, int gx, int gy, int s)
{
    hsfGlyphValveColor(p, gx, gy, s, HOME_GLYPH_STROKE, HOME_GLYPH_FILL);
}

/** @brief 按种类分派绘制。 */
static void hsfGlyphDraw(XPainter* p, int glyph, int gx, int gy, int s)
{
    switch (glyph) {
    case HOME_GLYPH_TH_HUM:   hsfGlyphThHum(p, gx, gy, s); break;
    case HOME_GLYPH_PUMP:     hsfGlyphPump(p, gx, gy, s); break;
    case HOME_GLYPH_HEALTH:   hsfGlyphHealth(p, gx, gy, s); break;
    case HOME_GLYPH_PRESSURE: hsfGlyphPressure(p, gx, gy, s); break;
    case HOME_GLYPH_FLOW:     hsfGlyphFlow(p, gx, gy, s); break;
    case HOME_GLYPH_VALVE:    hsfGlyphValve(p, gx, gy, s); break;
    default: break;
    }
}

XCLASS_DEFINE_BEGING(HsfHomeCardWidget)
XCLASS_DEFINE_EXTEND_END(HsfHomeCardWidget, XWidget)

XVtable* HsfHomeCard_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfHomeCardWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VHsfHomeCard_paintEvent);
    /* [整卡点击] 重载鼠标按下（照 HsfTitleBar.c 的 ☰/模式区口径）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VHsfHomeCard_mousePressEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 鼠标按下：挂了点击回调的整卡触发（触控面=整卡）。 */
static void VHsfHomeCard_mousePressEvent(XWidget* self, XEvent* event)
{
    HsfHomeCard* card = (HsfHomeCard*)self;
    if (!self || !event) return;
    if (XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS &&
        card && card->m_clickFn) {
        card->m_clickFn(card->m_clickUserData);
        XEvent_accept(event);
        return;
    }
    XClass_Parent(XWidget, EXWidget_MousePressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 给卡片挂整卡点击回调（fn 空 = 摘除）。 */
static void hsfCardSetClicked(HsfHomeCard* card, void (*fn)(void*),
                              void* userData)
{
    if (!card) return;
    card->m_clickFn = fn;
    card->m_clickUserData = userData;
}

/** @brief 设卡片设备图形（右上角水印简笔；HOME_GLYPH_NONE=无）。 */
static void hsfCardSetGlyph(HsfHomeCard* card, int glyph)
{
    if (!card) return;
    card->m_glyph = glyph;
    XWidget_update(&card->m_base);
}

/* ==================== 小圆点（状态/到位指示） ==================== */

/**
 * @brief 自绘圆点：实心（状态/已到位）或空心（未到位），色可设。
 * @details 对齐电磁阀页的到位双点口径（HsfMonitorValve.c：实心绿=到位、
 *          空心灰=未到）。颜色变化走 set 后 XWidget_update。
 */
typedef struct HsfHomeDot
{
    XWidget  m_base;    /**< 容器；第一个成员。 */
    uint32_t m_color;   /**< 填充色（实心）/描边色仅空心时固定灰。 */
    bool     m_filled;  /**< true=实心；false=空心灰描边。 */
    int      m_diam;    /**< 直径（px）。 */
    bool     m_glyphValve; /**< true=画阀简笔小图（色随 m_color），不画圆。 */
} HsfHomeDot;

static void VHsfHomeDot_paintEvent(XWidget* self, XEvent* event)
{
    HsfHomeDot* dot = (HsfHomeDot*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;
    int side;

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
    side = XWidget_width(self);
    if (side > XWidget_height(self)) side = XWidget_height(self);
    /* [阀简笔模式 2026-11-07] 状态点画成小阀图形（色随状态），替代圆点。 */
    if (dot->m_glyphValve) {
        hsfGlyphValveColor(&painter,
                           (XWidget_width(self) - side) / 2,
                           (XWidget_height(self) - side) / 2, side,
                           dot->m_color, 0xFFE3EDFBu);
        XPainter_end(&painter);
        XPainter_deinit(&painter);
        return;
    }
    XRect_init(&r, (XWidget_width(self) - side) / 2,
               (XWidget_height(self) - side) / 2, side, side);
    if (dot->m_filled) {
        XPainter_setPen(&painter, dot->m_color);
        XPainter_setBrush(&painter, dot->m_color);
    } else {
        XPainter_setPen(&painter, 0xFF9E9E9Eu);   /* 空心：灰描边。 */
        XPainter_setBrush_2(&painter, XPainterBrushStyle_NoBrush);
    }
    XPainter_setPenWidth(&painter, 1);
    XPainter_drawEllipse(&painter, &r);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

XCLASS_DEFINE_BEGING(HsfHomeDotWidget)
XCLASS_DEFINE_EXTEND_END(HsfHomeDotWidget, XWidget)

XVtable* HsfHomeDot_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfHomeDotWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VHsfHomeDot_paintEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 建圆点（堆分配；filled=false 为空心灰）。 */
static HsfHomeDot* hsfHomeDotCreate(XWidget* parent, int diam,
                                    uint32_t color, bool filled)
{
    HsfHomeDot* dot;
    if (!parent) return NULL;
    dot = (HsfHomeDot*)XMemory_malloc(sizeof(HsfHomeDot),
                                      XCLASS_DEFAULT_MEMORY_TYPE);
    if (!dot) return NULL;
    memset(dot, 0, sizeof(*dot));
    XWidget_init(&dot->m_base, parent, 0);
    XClassGetVtable(dot) = HsfHomeDot_class_init();
    Set_Class_Memory(dot, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(dot, true);
    dot->m_color = color;
    dot->m_filled = filled;
    dot->m_diam = diam;
    XWidget_setFixedSize(&dot->m_base, diam + 6, diam + 6);
    XWidget_show(&dot->m_base);
    return dot;
}

static void hsfHomeDotSetColor(HsfHomeDot* dot, uint32_t color)
{
    if (!dot || dot->m_color == color) return;
    dot->m_color = color;
    XWidget_update(&dot->m_base);
}

static void hsfHomeDotSetFilled(HsfHomeDot* dot, bool filled)
{
    if (!dot || dot->m_filled == filled) return;
    dot->m_filled = filled;
    XWidget_update(&dot->m_base);
}

/* ==================== 透明遮罩（浮层的"点外面关"载体） ==================== */

/**
 * @brief 透明遮罩：浮层打开时垫在浮层之下、盖满页面根；**点它即关浮层**。
 * @details 模态口径（对标 Qt 的模态遮罩）：浮层外的任何按下都先落在遮罩上
 *          （它在除浮层外的一切子控件之上），回调里收起浮层——不用给页面
 *          每个子控件挨个接"点外关闭"。paintEvent 为空 = 完全透明。
 */
typedef struct HsfHomeShade
{
    XWidget m_base;             /**< 容器；第一个成员。 */
    void (*m_clickFn)(void*);   /**< 按下回调（= 收起浮层）。 */
    void*   m_clickUserData;    /**< 回调令牌。 */
} HsfHomeShade;

static void VHsfHomeShade_paintEvent(XWidget* self, XEvent* event);
static void VHsfHomeShade_mousePressEvent(XWidget* self, XEvent* event);

XCLASS_DEFINE_BEGING(HsfHomeShadeWidget)
XCLASS_DEFINE_EXTEND_END(HsfHomeShadeWidget, XWidget)

XVtable* HsfHomeShade_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfHomeShadeWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VHsfHomeShade_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VHsfHomeShade_mousePressEvent);
    return XVTABLE_DEFAULT;
}

/* [透明] 不画任何内容：遮罩之下是页面自身的观感，浮层之外无视觉变化。 */
static void VHsfHomeShade_paintEvent(XWidget* self, XEvent* event)
{
    (void)self;
    (void)event;
}

static void VHsfHomeShade_mousePressEvent(XWidget* self, XEvent* event)
{
    HsfHomeShade* shade = (HsfHomeShade*)self;
    if (!self || !event) return;
    if (XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS &&
        shade && shade->m_clickFn) {
        shade->m_clickFn(shade->m_clickUserData);
        XEvent_accept(event);
        return;
    }
    XClass_Parent(XWidget, EXWidget_MousePressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 建遮罩（堆分配；初始隐藏）。 */
static HsfHomeShade* hsfHomeShadeCreate(XWidget* parent, void (*fn)(void*),
                                        void* userData)
{
    HsfHomeShade* shade;
    if (!parent) return NULL;
    shade = (HsfHomeShade*)XMemory_malloc(sizeof(HsfHomeShade),
                                          XCLASS_DEFAULT_MEMORY_TYPE);
    if (!shade) return NULL;
    memset(shade, 0, sizeof(*shade));
    XWidget_init(&shade->m_base, parent, 0);
    XClassGetVtable(shade) = HsfHomeShade_class_init();
    Set_Class_Memory(shade, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(shade, true);
    shade->m_clickFn = fn;
    shade->m_clickUserData = userData;
    return shade;
}

static void VHsfHomeCard_paintEvent(XWidget* self, XEvent* event)
{
    HsfHomeCard* card = (HsfHomeCard*)self;
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
    /* [设备图形 2026-11-06] 卡片右上角水印式简笔（软蓝描边 + 浅填充），
     * 给每张卡一个"一眼认设备"的视觉锚点；在水印色下不与数据争夺注意力。 */
    if (card->m_glyph != HOME_GLYPH_NONE) {
        const int cw = XWidget_width(self);
        const int gs = cw >= 320 ? 46 : 38;
        hsfGlyphDraw(&painter, card->m_glyph, cw - gs - 12, 10, gs);
    }
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
        XLabel_setTextPixelSize(&card->m_title, homePx(14));
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

/* ==================== 阀详情瓷贴 ==================== */

/**
 * @brief 阀详情瓷贴（浮层网格一格）：状态圆点+阀名 / 状态文字 / 到位双点。
 * @details 视觉对齐电磁阀页卡片（状态色 + 实心绿=到位、空心灰=未到）。
 */
typedef struct HsfHomeValveTile
{
    XWidget      m_base;       /**< 容器；第一个成员（加入浮层网格）。 */
    XBoxLayout   m_box;        /**< 竖排（加入浮层网格）。 */
    HsfHomeDot   m_stateDot;   /**< 状态圆点（色随状态）。 */
    XLabel       m_name;       /**< 阀名（深色）。 */
    XLabel       m_state;      /**< 状态文字（着色加粗）。 */
    XLabel       m_fbOpenCap;  /**< 「开到位」灰字。 */
    HsfHomeDot   m_fbOpenDot;  /**< 开到位圆点。 */
    XLabel       m_fbCloseCap; /**< 「关到位」灰字。 */
    HsfHomeDot   m_fbCloseDot; /**< 关到位圆点。 */
} HsfHomeValveTile;

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
    XLabel       m_devTitle;   /**< 浮层标题（"设备状态"/"搅拌泵详情"随态切）。 */
    XTableWidget m_table;      /**< 设备状态表（设备/类型/状态/地址/错误信息）。 */
    int          m_tableRows;  /**< 当前表格行数（避免每拍重建）。 */
    /* ---- 第二行三卡（压差 / 流量计 / 电磁阀六路合一）---- */
    XBoxLayout   m_cardsRow2;  /**< 第二卡行横排。 */
    HsfStatBlock* m_dpValue;   /**< 压差（MPa，AI 通道 0 工程值）。 */
    XLabel       m_dpNote;     /**< 压差越限/断流说明（服务端 ew0，小字）。 */
    HsfStatBlock* m_fmFlow;    /**< 流量计瞬时流量。 */
    XLabel       m_fmSub;      /**< 流量计正/反向累计（小字行）。 */
    HsfHomeCard* m_valveCard;  /**< 电磁阀卡（六路合一）。 */
    XLabel       m_valveState[6]; /**< 六路状态行（●+文字，按状态着色）。 */
    XLabel       m_valveName[6];  /**< 六路阀名（服务端下发，小字灰）。 */
    /* ---- 详情浮层（点卡弹出、居中悬浮；0 收起 / 1 设备状态表 / 2 泵详情）---- */
    HsfHomeCard*  m_detailFloat;   /**< 悬浮详情卡（页面根子控件，不在流内）。 */
    struct HsfHomeShade* m_detailShade; /**< 透明遮罩：盖满页面根，点它关浮层。 */
    XPushButton   m_floatClose;    /**< 浮层右上 × 关闭钮。 */
    HsfStatBlock* m_pumpStat[10];  /**< 泵详情十项（顺序见创建处）。 */
    HsfHomeValveTile m_vTiles[6];  /**< 阀详情六瓷贴（2 列 × 3 行网格）。 */
    HsfHomeDot*   m_vSumDot[4];    /**< 摘要四色圆点（已开/已关/动作中/故障）。 */
    XLabel*       m_vSumCnt[4];    /**< 摘要计数标签（"已开 2" 等，heap）。 */
    int           m_detailMode;    /**< 详情槽当前态（默认 0 收起）。 */
    uint64_t      m_dpLastMs;      /**< 最近一拍 AI 帧到达时刻（压差新鲜度）。 */
    HsfMenuNavFn m_navFn;      /**< 导航回调。 */
    void*        m_navUserData;/**< 回调令牌。 */
} HsfHomePage;

/* ---- 中文常量（UTF-8 转义；源文件按既有口径避免编码问题） ---- */

#define HOME_TXT_TITLE      "主界面"            /* 主界面 */
#define HOME_TXT_SUBTITLE   "高速过滤器监控系统"                                      /* 高速过滤器监控系统 */
#define HOME_TXT_CARD_TH    "电箱温湿度"  /* 电箱温湿度 */
#define HOME_TXT_TH_TEMP    "电箱温度"  /* 电箱温度 */
#define HOME_TXT_TH_HUM     "电箱湿度"  /* 电箱湿度 */
/* [2026-11-05 现场口径] 这路变频器驱动的是搅拌泵，卡片名按工艺用途叫。
 * 设备**类别**仍是 Vfd（kind=0），"类型"列照旧显示"变频器" —— 名字
 * 给操作员找设备、类别给维修判断该查什么，两者不混。 */
#define HOME_TXT_CARD_VFD   "搅拌泵"              /* 搅拌泵 */
#define HOME_TXT_VFD_STATE  "运行状态"  /* 运行状态 */
#define HOME_TXT_VFD_OUT    "输出频率"  /* 输出频率 */
#define HOME_TXT_VFD_SET    "设定频率"  /* 设定频率 */
#define HOME_TXT_CARD_HEALTH "设备健康" /* 设备健康 */
#define HOME_TXT_DEV_TOTAL  "设备总数"  /* 设备总数 */
#define HOME_TXT_DEV_FAULT  "故障"                          /* 故障 */
#define HOME_TXT_DEV_OFF    "离线"                          /* 离线 */
#define HOME_TXT_UNIT_TAI   "台"                                      /* 台 */
#define HOME_TXT_ENTRY      "快捷入口"  /* 快捷入口 */
#define HOME_TXT_DEVSTATUS  "设备状态"  /* 设备状态 */
#define HOME_TXT_NO_DATA    "无数据"              /* 无数据 */
#define HOME_TXT_DASH       "--"
/* ---- 第二行三卡 + 泵详情（2026-11-06 新增）---- */
#define HOME_TXT_CARD_DP    "压差"                          /* 压差 */
#define HOME_TXT_DP_CAP     "压差计"                                      /* 压差计（通道名，服务端下发可覆盖显示） */
#define HOME_TXT_CARD_FM    "流量计"              /* 流量计 */
#define HOME_TXT_FM_CAP     "瞬时流量"  /* 瞬时流量 */
#define HOME_TXT_FM_FWD     "正向累计"  /* 正向累计 */
#define HOME_TXT_FM_REV     "反向累计"  /* 反向累计 */
#define HOME_TXT_CARD_VALVE "电磁阀 · 6 路"                              /* 电磁阀 · 6 路 */
#define HOME_TXT_PUMP_DETAIL "搅拌泵详情" /* 搅拌泵详情 */
#define HOME_TXT_DP_CURRENT "电流"                          /* 电流 */
#define HOME_TXT_DP_VOLT    "电压"                          /* 电压 */
#define HOME_TXT_DP_BUS     "母线电压"  /* 母线电压 */
#define HOME_TXT_DP_POWER   "功率"                          /* 功率 */
#define HOME_TXT_DP_SPEED   "转速"                          /* 转速 */
#define HOME_TXT_DP_TORQUE  "转矩"                          /* 转矩 */
#define HOME_TXT_DP_FCODE   "故障码"              /* 故障码 */
#define HOME_TXT_NO_FAULT   "无故障"              /* 无故障 */
#define HOME_TXT_VALVE_OPEN "已开"                          /* 已开 */
#define HOME_TXT_VALVE_CLOSE "已关"                         /* 已关 */
#define HOME_TXT_VALVE_MOVE "动作中"              /* 动作中 */
#define HOME_TXT_VALVE_UNCFG "未配置"             /* 未配置 */
#define HOME_TXT_VALVE_FAULT "故障"                         /* 故障 */
#define HOME_TXT_VALVE_UNKNOWN "未知"                       /* 未知 */
#define HOME_TXT_VALVE_DETAIL "电磁阀详情"                                      /* 电磁阀详情 */
#define HOME_TXT_FB_OPEN    "开到位"              /* 开到位 */
#define HOME_TXT_FB_CLOSE   "关到位"              /* 关到位 */
#define HOME_TXT_YES        "是"                                      /* 是 */
#define HOME_TXT_NO         "否"                                      /* 否 */
#define HOME_VALVE_N         6   /**< 电磁阀六路（现场口径，与 HsfServerIoValve 同值）。 */
#define HOME_DP_STALE_MS     5000u /* 压差卡断流判据（AI 无时间型 fresh，自记）。 */

/* 详情槽三态。 */
#define HOME_DETAIL_NONE    0   /**< 收起（默认）。 */
#define HOME_DETAIL_HEALTH  1   /**< 设备状态表展开。 */
#define HOME_DETAIL_PUMP    2   /**< 泵详情展开。 */
#define HOME_DETAIL_VALVE   3   /**< 电磁阀详情展开（2026-11-06 新增）。 */

/* [设备健康/类别文案已提到 HsfLinkClient（首页与报警页共用）]
 * HsfLinkClient_healthText() / HsfLinkClient_kindText() —— 原先这两张表是
 * 本页的 static 函数，报警页要用就得复制一份（两处漂移过一次）⇒ 归到
 * 链接层（协议枚举的文案与协议同源）。 */

#if XVFD_ON
/** @brief 变频器运行状态中文（与 Python RUN_STATE_TEXT 对齐）。 */
static const char* hsfHomeRunStateText(int st)
{
    switch (st) {
    case XVfdRunState_Stopped:         return "停机";           /* 停机 */
    case XVfdRunState_Ready:           return "就绪";           /* 就绪 */
    case XVfdRunState_RunningForward:  return "正转运行"; /* 正转运行 */
    case XVfdRunState_RunningReverse:  return "反转运行"; /* 反转运行 */
    case XVfdRunState_JogForward:      return "正转点动"; /* 正转点动 */
    case XVfdRunState_JogReverse:      return "反转点动"; /* 反转点动 */
    case XVfdRunState_Accelerating:    return "加速中"; /* 加速中 */
    case XVfdRunState_Decelerating:    return "减速中"; /* 减速中 */
    case XVfdRunState_ConstantSpeed:   return "恒速运行"; /* 恒速运行 */
    case XVfdRunState_DcBraking:       return "直流制动"; /* 直流制动 */
    case XVfdRunState_FreeStopping:    return "自由停车"; /* 自由停车 */
    case XVfdRunState_QuickStopping:   return "快速停机"; /* 快速停机 */
    case XVfdRunState_Fault:           return "故障";           /* 故障 */
    default:                           return "未知";           /* 未知 */
    }
}
#endif

/* ==================== 新卡辅助（压差/流量计/电磁阀/详情槽） ==================== */

/** @brief 电磁阀状态 → 文案与色（对齐 HsfMonitorValve.c valveStateStyle）。 */
static void hsfHomeValveStateInfo(int state, bool ready, bool valid,
                                  const char** text, uint32_t* color)
{
    if (!ready) {
        *text = HOME_TXT_VALVE_UNCFG;
        *color = 0xFF9E9E9Eu;                            /* 浅灰 */
        return;
    }
    switch (state) {
    case 1:  *text = HOME_TXT_VALVE_CLOSE; *color = 0xFF37474Fu; break; /* 已关 灰蓝 */
    case 2:  *text = HOME_TXT_VALVE_OPEN;  *color = HOME_CLR_OK;   break; /* 已开 绿 */
    case 3:  *text = HOME_TXT_VALVE_MOVE;  *color = HOME_CLR_WARN; break; /* 动作中 橙 */
    case 4:  *text = HOME_TXT_VALVE_FAULT; *color = HOME_CLR_FAULT; break; /* 故障 红 */
    default: *text = HOME_TXT_VALVE_UNKNOWN; *color = 0xFF9E9E9Eu; break; /* 未知 灰 */
    }
    (void)valid;
}

/** @brief 流量瞬时单位码 → 文本（对齐 HsfMonitorFlowMeter.c fmFlowUnitText）。 */
static const char* hsfHomeFmFlowUnit(int code)
{
    switch (code) {
    case 0:  return "L/s";
    case 1:  return "m3/s";
    case 2:  return "L/m";
    case 3:  return "m3/m";
    case 4:  return "L/h";
    case 5:  return "m3/h";
    case 6:  return "L/d";
    case 7:  return "m3/d";
    case 8:  return "t/h";
    case 9:  return "t/m";
    case 10: return "t/d";
    default: return "--";
    }
}

/** @brief 累计单位码 → 文本（对齐 fmTotalUnitText）。 */
static const char* hsfHomeFmTotalUnit(int code)
{
    switch (code) {
    case 0:  return "L";
    case 1:  return "m3";
    case 2:  return "t";
    case 3:  return "USG";
    default: return "--";
    }
}

/**
 * @brief 选「优先侧，其次另一侧」中当前新鲜的那一侧温湿度。
 * @return 命中侧的快照（借用）；两侧都不新鲜/无有效值返回 NULL。
 * @note  [为什么不是写死 A 侧] 主界面卡的职责是"把现场读数显示出来"，
 *        而温湿度站可能挂在任一侧（服务端按侧各推一份，订阅补发目前固定
 *        发 A 侧）。写死单侧时，只要现场把变送器接在另一侧、或该侧首帧
 *        补发缺失，卡片就永久 "-- ℃ / -- %RH"，与"设备没接"无法区分。
 *        优先侧仍是 A（现场口径），仅在 A 不新鲜时才去看 B。
 */
static const HsfLinkTempHum* hsfHomePickTempHum(void)
{
    static const int kPrefer[HSF_LINK_SIDE_COUNT] = { HSF_SIDE_A, HSF_SIDE_B };
    int i;
    for (i = 0; i < HSF_LINK_SIDE_COUNT; ++i) {
        const int side = kPrefer[i];
        if (HsfLinkClient_tempHumFresh(side)) {
            const HsfLinkTempHum* th = HsfLinkClient_tempHum(side);
            if (th && (th->m_tempValid || th->m_humValid)) return th;
        }
    }
    return NULL;
}

/**
 * @brief 选「优先侧，其次另一侧」中当前新鲜的流量计快照。
 * @return 命中侧的快照（借用）；两侧都不新鲜/无效返回 NULL。
 * @note  优先 B 侧（现场流量计挂 B 侧，标题栏副行口径同此），A 侧作兜底。
 */
static const HsfLinkFlowMeter* hsfHomePickFlowMeter(void)
{
    static const int kPrefer[HSF_LINK_SIDE_COUNT] = { HSF_SIDE_B, HSF_SIDE_A };
    int i;
    for (i = 0; i < HSF_LINK_SIDE_COUNT; ++i) {
        const int side = kPrefer[i];
        if (HsfLinkClient_flowMeterFresh(side)) {
            const HsfLinkFlowMeter* fm = HsfLinkClient_flowMeter(side);
            if (fm && fm->m_valid) return fm;
        }
    }
    return NULL;
}

/** @brief 标签文本着色（per-widget 调色板 WindowText，照 HsfDataBar 口径）。
 *  @note  [不再补 update 2026-11-08] 原实现 setPalette 之后再显式
 *         XWidget_update。两个方向都多余：
 *           · 颜色**变了**：XWidget_setPalette 内部已经 update（Qt
 *             QWidgetPrivate::setPalette_helper 同款尾步）；
 *           · 颜色**没变**：setPalette 已按 Qt 语义同值早退（qwidget.cpp:
 *             4632-4633），这句 update 反而把"没变"硬标成脏区。
 *         本函数由主页 200ms 刷新调用、覆盖 6 路阀状态标签 + 详情瓷贴
 *         标签 + 压差说明 ⇒ 稳态 28 笔/秒的无谓标脏（画像实测主页
 *         12fps 的主源，最大单笔 103×12 正对状态标签尺寸）。 */
static void hsfHomeLabelSetColor(XLabel* label, uint32_t clr)
{
    XPalette p;
    if (!label) return;
    p = XWidget_palette((XWidget*)label);
    XPalette_setColor(&p, XPaletteColorGroup_Active,
                      XPaletteColorRole_WindowText,
                      XColor_create_rgb((int)((clr >> 16) & 0xFFu),
                                        (int)((clr >> 8) & 0xFFu),
                                        (int)(clr & 0xFFu), 0xFF));
    XPalette_setColor(&p, XPaletteColorGroup_Inactive,
                      XPaletteColorRole_WindowText,
                      XColor_create_rgb((int)((clr >> 16) & 0xFFu),
                                        (int)((clr >> 8) & 0xFFu),
                                        (int)(clr & 0xFFu), 0xFF));
    XWidget_setPalette((XWidget*)label, &p);
}

static void hsfHomeHealthClicked(void* userData);
static void hsfHomePumpClicked(void* userData);

/**
 * @brief 设备状态表末列（错误信息）吃满表格可视宽，消除"末列右侧空表头"。
 * @param self 主界面页（m_detailFloat / m_table 已建）。
 * @details [根因：多出来的空白表头 = 表格宽 − Σ列宽 的那段空区]
 *          XTableWidget 画表头带时段带只铺到 Σ列宽 为止，其后的余量按
 *          CE_HeaderEmptyArea 铺成 window 色（Src/XGui/Widget/
 *          XTableWidget.c:3144-3149 的 bandEnd<w 分支），视觉上就是末列
 *          右侧多出来的一条空白表头。要消掉它，Σ列宽必须正好等于表格
 *          可视宽。
 *          [旧实现为何差一截] 旧式按"浮层宽 − 28 − 14"推算末列宽：28 是
 *          卡片左右内边距，14 是给竖向滚动条预留的条宽。但本表高度恒被
 *          setFixedHeight 钉成"表头 + 行数×行高"，内容永不高于控件
 *          （正常字号下视口还比内容高 10px），AsNeeded 竖条**从不显形**
 *          ⇒ 预留的 14px 每拍都变成末列右侧一条空白表头带（2026-11-08
 *          截图实测：表格 x=256..1207，Σ列宽=938，右侧 1194..1207 恰
 *          14px 的 window 色空区）。此外前四列宽写死 150/85/80/330，
 *          与按页字号缩放后的实际列宽（homeApplyFonts 按 pct 改宽）
 *          不符，放大字号后同样失配。
 *          [修法] 宽度的唯一事实源取表格自身：
 *            · 可用宽 = 滚动区视口宽。框架已按竖向滚动条显隐扣好条宽
 *              （无条时即控件宽），不再由本页自己预留 14px；
 *            · 前四列宽取 XTableView_columnWidth 的当前值求和，不再写死；
 *            · 末列 = 可用宽 − 前四列合计 ⇒ Σ列宽 恒等于可用宽，
 *              paintEvent 走不到 bandEnd<w 分支，右侧不再有空区。
 *          [调用时机] 必须在 hsfHomeCenterFloat 定尺之后调用：浮层
 *          setFixedSize/setGeometry 会同步激活卡片布局，把表格几何
 *          （连同滚动区视口）排到本拍最终值；提前读拿到的还是旧宽/0，
 *          末列就会算窄——这正是上一版修复要解决的时序问题，此次保留。
 */
static void hsfHomeFitTableColumns(HsfHomePage* self)
{
    XWidget* viewport;
    int avail;
    int others = 0;
    int c;
    if (!self || !self->m_detailFloat) return;
    if (self->m_detailMode != HOME_DETAIL_HEALTH) return;
    /* 视口宽=表格可视宽（滚动条显隐由框架扣，见 xasa_layout）。 */
    viewport = XAbstractScrollArea_viewport(
        (XAbstractScrollArea*)&self->m_table.m_base);
    avail = viewport ? (int)XWidget_width(viewport)
                     : (int)XWidget_width((XWidget*)&self->m_table);
    if (avail <= 0) return;      /* 布局尚未定尺：等下一拍刷新再算。 */
    for (c = 0; c < 4; ++c)      /* 前四列按当前实际宽累加（随字号缩放）。 */
        others += (int)XTableView_columnWidth(&self->m_table.m_base, c);
    /* 窗口过窄时保留固定列宽，余量交给横向滚动条，不把末列挤成一条线。 */
    if (avail - others < 60) return;
    XTableView_setColumnWidth(&self->m_table.m_base, 4, avail - others);
}

/** @brief 浮层居中：水平/垂直居中于页面根，宽随模式定、高度按内容算。 */
static void hsfHomeCenterFloat(HsfHomePage* self)
{
    int pw;
    int ph;
    int w;
    int h;
    /* [阀详情按内容定尺的下限] 100% 字号下已验收的宽/高：字号没放大时
     * 浮层观感与旧版一致，放大后才由布局首选尺寸顶上去（见下方说明）。 */
    const int valveMinW = 760;
    const int valveMinH = 56 + 3 * 96 + 24;
    if (!self || !self->m_detailFloat) return;
    pw = (int)XWidget_width(&self->m_base);
    ph = (int)XWidget_height(&self->m_base);
    if (self->m_detailMode == HOME_DETAIL_HEALTH) {
        w = 980;
        h = 108 + self->m_tableRows * HOME_ROW_H;  /* 标题行 + 边距 + 表(表头+行) */
    } else if (self->m_detailMode == HOME_DETAIL_VALVE) {
        /* [阀详情浮层按内容定尺 2026-11-08] 根因：宽高原先都是写死常量
         * （宽 760、高 56+3*96+24=368），而瓷贴里的文字用 homePx() 随本地
         * 设置「按页字号」（FontScale/Page0，50–200%）缩放、到位圆点是
         * 固定 18px——"三行瓷贴每行约 96px"这个假设只在 100% 字号下成立。
         * 字号一旦放大，浮层高度不够，第三行（阀5/阀6）的到位行就被裁到
         * 框外（200% 实测：状态文字整行消失、"开到位/关到位"只剩首字）。
         *   ⇒ 不再猜常量：直接读浮层自身布局的首选尺寸——XWidget_sizeHint
         *     对挂了布局的控件走 XLayout_totalSizeHint，已含卡片边距、
         *     瓷贴内边距、布局间距与缩放后的字号，是内容真正要的地方。
         *     旧常量保留作**下限**：100% 字号观感与已验收截图一致，字号
         *     放大时浮层自己长大，无需再逐档调数。
         *   ⇒ 另注：宽度同理取 max，避免超大字号下"开到位/关到位"两列
         *     被挤到换行/裁字。 */
        XSize hint = XWidget_sizeHint((XWidget*)&self->m_detailFloat->m_base);
        w = hint.width > valveMinW ? hint.width : valveMinW;
        h = hint.height > valveMinH ? hint.height : valveMinH;
    } else {
        w = 640;
        h = 170;                                   /* 标题行 + 两行读数 */
    }
    if (w > pw - 24) w = pw - 24 > 0 ? pw - 24 : 0;
    if (h > ph - 24) h = ph - 24 > 0 ? ph - 24 : 120;
    XWidget_setFixedSize((XWidget*)self->m_detailFloat, w, h);
    XWidget_setGeometry((XWidget*)self->m_detailFloat,
                        (pw - w) / 2, (ph - h) / 2, w, h);
    /* [定尺之后才许算列宽] 见 hsfHomeFitTableColumns 的说明。 */
    hsfHomeFitTableColumns(self);
}

static HsfHomePage* s_homePage = NULL;   /**< 活动主界面页（×关闭槽反推用；单实例页）。 */

/** @brief 应用详情浮层三态：收起 / 设备状态表 / 泵详情（居中弹出+抬升）。
 *  @note  遮罩与浮层同显同隐：遮罩盖满页面根（点它=点浮层外=关），浮层
 *         抬升遮罩之上。 */
static void hsfHomeApplyDetail(HsfHomePage* self)
{
    int k;
    if (!self) return;
    if (self->m_detailShade) {
        XWidget_setGeometry((XWidget*)self->m_detailShade, 0, 0,
                            (int)XWidget_width(&self->m_base),
                            (int)XWidget_height(&self->m_base));
    }
    if (self->m_detailMode == HOME_DETAIL_NONE) {
        if (self->m_detailShade)
            XWidget_hide((XWidget*)self->m_detailShade);
        if (self->m_detailFloat)
            XWidget_hide((XWidget*)self->m_detailFloat);
        return;
    }
    if (self->m_detailMode == HOME_DETAIL_HEALTH) {
        XLabel_setText_2(&self->m_devTitle, HOME_TXT_DEVSTATUS);
        /* 表随行数收放（浮层高度按当前行数计算）。 */
        XWidget_setFixedHeight((XWidget*)&self->m_table,
                               HOME_HEAD_H + self->m_tableRows * HOME_ROW_H);
        /* [末列吃余宽：改到定尺之后算 2026-11-08] 原来在这里就直接读
         * XWidget_width(m_detailFloat) 算末列宽——但此刻浮层尺寸还是
         * **上一轮的值或 0**（真正定尺在函数尾的 hsfHomeCenterFloat），
         * 算出的 tableW 偏小/为负，末列贴不到边 ⇒ 右侧余量被铺成表头灰，
         * 看着像"多了一列空列"（用户截图实测）。已移到
         * hsfHomeCenterFloat 之后调用 hsfHomeFitTableColumns。 */
        XWidget_show((XWidget*)&self->m_table);
        for (k = 0; k < 10; ++k)
            if (self->m_pumpStat[k])
                XWidget_hide((XWidget*)&self->m_pumpStat[k]->m_base);
        for (k = 0; k < HOME_VALVE_N; ++k)
            XWidget_hide((XWidget*)&self->m_vTiles[k].m_base);
    } else if (self->m_detailMode == HOME_DETAIL_VALVE) {
        XLabel_setText_2(&self->m_devTitle, HOME_TXT_VALVE_DETAIL);
        XWidget_hide((XWidget*)&self->m_table);
        for (k = 0; k < 10; ++k)
            if (self->m_pumpStat[k])
                XWidget_hide((XWidget*)&self->m_pumpStat[k]->m_base);
        for (k = 0; k < HOME_VALVE_N; ++k)
            XWidget_show((XWidget*)&self->m_vTiles[k].m_base);
    } else {
        XLabel_setText_2(&self->m_devTitle, HOME_TXT_PUMP_DETAIL);
        XWidget_hide((XWidget*)&self->m_table);
        for (k = 0; k < 10; ++k)
            if (self->m_pumpStat[k])
                XWidget_show((XWidget*)&self->m_pumpStat[k]->m_base);
        for (k = 0; k < HOME_VALVE_N; ++k)
            XWidget_hide((XWidget*)&self->m_vTiles[k].m_base);
    }
    hsfHomeCenterFloat(self);
    if (self->m_detailShade) {
        XWidget_show((XWidget*)self->m_detailShade);
        XWidget_raise((XWidget*)self->m_detailShade);
    }
    if (self->m_detailFloat) {
        XWidget_raise((XWidget*)self->m_detailFloat);
        XWidget_show((XWidget*)self->m_detailFloat);
    }
    XWidget_update(&self->m_base);
}

/** @brief 浮层 × 钮（XAbstractButton clicked 直连槽）：收起浮层。 */
static void hsfHomeFloatCloseSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    if (!s_homePage) return;
    s_homePage->m_detailMode = HOME_DETAIL_NONE;
    hsfHomeApplyDetail(s_homePage);
}

/** @brief 设备健康卡点击：健康表 ↔ 收起。 */
static void hsfHomeHealthClicked(void* userData)
{
    HsfHomePage* self = (HsfHomePage*)userData;
    if (!self) return;
    self->m_detailMode = (self->m_detailMode == HOME_DETAIL_HEALTH)
                             ? HOME_DETAIL_NONE
                             : HOME_DETAIL_HEALTH;
    hsfHomeApplyDetail(self);
}

/** @brief 遮罩点击（浮层外按下）：收起浮层。 */
static void hsfHomeShadeClicked(void* userData)
{
    HsfHomePage* self = (HsfHomePage*)userData;
    if (!self) return;
    self->m_detailMode = HOME_DETAIL_NONE;
    hsfHomeApplyDetail(self);
}

/** @brief 搅拌泵卡点击：泵详情 ↔ 收起。 */
static void hsfHomePumpClicked(void* userData)
{
    HsfHomePage* self = (HsfHomePage*)userData;
    if (!self) return;
    self->m_detailMode = (self->m_detailMode == HOME_DETAIL_PUMP)
                             ? HOME_DETAIL_NONE
                             : HOME_DETAIL_PUMP;
    hsfHomeApplyDetail(self);
}

/** @brief 电磁阀卡点击：阀详情 ↔ 收起。 */
static void hsfHomeValveClicked(void* userData)
{
    HsfHomePage* self = (HsfHomePage*)userData;
    if (!self) return;
    self->m_detailMode = (self->m_detailMode == HOME_DETAIL_VALVE)
                             ? HOME_DETAIL_NONE
                             : HOME_DETAIL_VALVE;
    hsfHomeApplyDetail(self);
}

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

    /* ---------- 电箱温湿度（优先 A 侧，A 不新鲜时看 B） ---------- */
    {
        const HsfLinkTempHum* th = hsfHomePickTempHum();
        bool fresh = (th != NULL);
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

    /* ---------- 压差（AI 通道 0"压差计"，MPa） ---------- */
    {
        uint32_t mask = 0;
        int n = HsfLinkClient_aiValues(NULL, &mask);
        uint64_t now = (uint64_t)XDateTime_currentMSecsSinceEpoch();
        /* [AI 无时间型 fresh] AI 访问器只有"收到过"位，没有断流判据（三路
         * 中唯一缺口）⇒ 页内自记 lastMs，超 5s 视为断流显示 "--"。 */
        if (n > 0 && (mask & 1u))
            self->m_dpLastMs = now;
        if (self->m_dpLastMs != 0 &&
            now - self->m_dpLastMs <= HOME_DP_STALE_MS) {
            int32_t v1000 = 0;
            char unit[16];
            const char* note = NULL;
            if (HsfLinkClient_aiEngineering(0, &v1000, unit,
                                            (int)sizeof(unit), &note)) {
                snprintf(buf, sizeof(buf), "%.3f", (double)v1000 / 1000.0);
                hsfStatSet(self->m_dpValue, buf, HOME_CLR_TEXT, false);
                /* 单位随服务端标定下发（现场 "MPa"），标签跟一拍。 */
                XLabel_setText_2(&self->m_dpValue->m_unit, unit);
            } else {
                hsfStatSet(self->m_dpValue, HOME_TXT_DASH, HOME_CLR_TEXT, false);
            }
            if (note && note[0]) {
                /* 越限说明（断线/超量程）服务端口径，警示色。 */
                XLabel_setText_2(&self->m_dpNote, note);
                hsfHomeLabelSetColor(&self->m_dpNote, HOME_CLR_WARN);
            } else {
                const char* nm = HsfLinkClient_aiName(0);
                XLabel_setText_2(&self->m_dpNote,
                                 nm ? nm : HOME_TXT_DP_CAP);
                hsfHomeLabelSetColor(&self->m_dpNote, HOME_CLR_TEXT_DIM);
            }
        } else {
            hsfStatSet(self->m_dpValue, HOME_TXT_DASH, HOME_CLR_TEXT, false);
            XLabel_setText_2(&self->m_dpNote, HOME_TXT_NO_DATA);
            hsfHomeLabelSetColor(&self->m_dpNote, HOME_CLR_OFFLINE);
        }
    }

    /* ---------- 流量计（优先 B 侧，B 不新鲜时看 A；瞬时流量 + 正/反向累计） ---------- */
    {
        const HsfLinkFlowMeter* fm = hsfHomePickFlowMeter();
        bool fresh = (fm != NULL);
        if (fm && fresh) {
            snprintf(buf, sizeof(buf), "%.2f", fm->m_flow);
            hsfStatSet(self->m_fmFlow, buf, HOME_CLR_TEXT, false);
            XLabel_setText_2(&self->m_fmFlow->m_unit,
                             hsfHomeFmFlowUnit(fm->m_flowUnit));
            /* 副行：正/反向累计（单位随服务端单位码，现场码 1=m³）。 */
            snprintf(buf, sizeof(buf),
                     "%s %.2f %s / %s %.2f %s",
                     HOME_TXT_FM_FWD, fm->m_forwardTotal,
                     hsfHomeFmTotalUnit(fm->m_totalUnit),
                     HOME_TXT_FM_REV, fm->m_reverseTotal,
                     hsfHomeFmTotalUnit(fm->m_totalUnit));
            XLabel_setText_2(&self->m_fmSub, buf);
        } else {
            hsfStatSet(self->m_fmFlow, HOME_TXT_DASH, HOME_CLR_TEXT, false);
            XLabel_setText_2(&self->m_fmSub, HOME_TXT_NO_DATA);
        }
    }

    /* ---------- 电磁阀（六路合一） ---------- */
    {
        const HsfLinkValves* vv = HsfLinkClient_valves();
        bool fresh = HsfLinkClient_valvesFresh() && vv && vv->m_count > 0;
        int k;
        for (k = 0; k < HOME_VALVE_N; ++k) {
            if (fresh && k < vv->m_count) {
                const char* text = HOME_TXT_VALVE_UNKNOWN;
                uint32_t color = 0xFF9E9E9Eu;
                char tile[96];
                hsfHomeValveStateInfo(vv->m_items[k].m_state,
                                      vv->m_items[k].m_ready,
                                      vv->m_items[k].m_valid, &text, &color);
                /* 圆点 + 状态文字（色随状态），阀名小字灰在下一行。 */
                snprintf(tile, sizeof(tile), "● %s", text);
                XLabel_setText_2(&self->m_valveState[k], tile);
                hsfHomeLabelSetColor(&self->m_valveState[k], color);
                XLabel_setText_2(&self->m_valveName[k],
                                 vv->m_items[k].m_name[0]
                                     ? vv->m_items[k].m_name : "-");
            } else {
                XLabel_setText_2(&self->m_valveState[k], HOME_TXT_DASH);
                hsfHomeLabelSetColor(&self->m_valveState[k], 0xFF9E9E9Eu);
                XLabel_setText_2(&self->m_valveName[k], HOME_TXT_DASH);
            }
        }
        /* [阀详情瓷贴 2026-11-07] VALVE 态刷新：阀名/状态文字/圆点三件套
         * 分开落位（不拼长行——旧拼行的全角空格分隔符被批量修复损坏成
         * "全角空格"字样，实测）；到位双点实心绿=到位、空心灰=未到。 */
        if (self->m_detailMode == HOME_DETAIL_VALVE) {
            for (k = 0; k < HOME_VALVE_N; ++k) {
                HsfHomeValveTile* t = &self->m_vTiles[k];
                if (fresh && k < vv->m_count) {
                    const char* text = HOME_TXT_VALVE_UNKNOWN;
                    uint32_t color = 0xFF9E9E9Eu;
                    hsfHomeValveStateInfo(vv->m_items[k].m_state,
                                          vv->m_items[k].m_ready,
                                          vv->m_items[k].m_valid,
                                          &text, &color);
                    XLabel_setText_2(&t->m_name,
                                     vv->m_items[k].m_name[0]
                                         ? vv->m_items[k].m_name : "-");
                    XLabel_setText_2(&t->m_state, text);
                    hsfHomeLabelSetColor(&t->m_state, color);
                    hsfHomeDotSetColor(&t->m_stateDot, color);
                    hsfHomeDotSetFilled(&t->m_stateDot, true);
                    hsfHomeDotSetColor(&t->m_fbOpenDot,
                                       vv->m_items[k].m_openFb
                                           ? HOME_CLR_OK : 0xFF9E9E9Eu);
                    hsfHomeDotSetFilled(&t->m_fbOpenDot,
                                        vv->m_items[k].m_openFb);
                    hsfHomeDotSetColor(&t->m_fbCloseDot,
                                       vv->m_items[k].m_closeFb
                                           ? HOME_CLR_OK : 0xFF9E9E9Eu);
                    hsfHomeDotSetFilled(&t->m_fbCloseDot,
                                        vv->m_items[k].m_closeFb);
                } else {
                    XLabel_setText_2(&t->m_name, HOME_TXT_DASH);
                    XLabel_setText_2(&t->m_state, HOME_TXT_NO_DATA);
                    hsfHomeLabelSetColor(&t->m_state, 0xFF9E9E9Eu);
                    hsfHomeDotSetColor(&t->m_stateDot, 0xFF9E9E9Eu);
                    hsfHomeDotSetFilled(&t->m_stateDot, true);
                    hsfHomeDotSetFilled(&t->m_fbOpenDot, false);
                    hsfHomeDotSetFilled(&t->m_fbCloseDot, false);
                }
            }
        }
    }

    /* ---------- 泵详情（详情槽 Pump 态才刷新） ---------- */
    if (self->m_detailMode == HOME_DETAIL_PUMP) {
        const XVfdStatus* st = NULL;
        bool ok = false;
#if XVFD_ON
        st = HsfLinkClient_vfdStatus();
#endif
        ok = st && st->m_valid && HsfLinkClient_vfdValid();
        if (ok) {
            hsfStatSet(self->m_pumpStat[0],
                       hsfHomeRunStateText((int)st->m_runState),
                       HOME_CLR_TEXT, true);
            /* [十项读数] 字段有效性逐项判（validMask），缺的给 "--"。 */
            if (st->m_validMask & XVfdField_OutputFreqHz) {
                snprintf(buf, sizeof(buf), "%.2f", st->m_outputFreqHz);
                hsfStatSet(self->m_pumpStat[1], buf, HOME_CLR_TEXT, false);
            } else hsfStatSet(self->m_pumpStat[1], HOME_TXT_DASH, 0, false);
            if (st->m_validMask & XVfdField_SetFreqHz) {
                snprintf(buf, sizeof(buf), "%.2f", st->m_setFreqHz);
                hsfStatSet(self->m_pumpStat[2], buf, HOME_CLR_TEXT, false);
            } else hsfStatSet(self->m_pumpStat[2], HOME_TXT_DASH, 0, false);
            if (st->m_validMask & XVfdField_OutputCurrent) {
                snprintf(buf, sizeof(buf), "%.2f", st->m_outputCurrentA);
                hsfStatSet(self->m_pumpStat[3], buf, HOME_CLR_TEXT, false);
            } else hsfStatSet(self->m_pumpStat[3], HOME_TXT_DASH, 0, false);
            if (st->m_validMask & XVfdField_OutputVoltage) {
                snprintf(buf, sizeof(buf), "%.1f", st->m_outputVoltageV);
                hsfStatSet(self->m_pumpStat[4], buf, HOME_CLR_TEXT, false);
            } else hsfStatSet(self->m_pumpStat[4], HOME_TXT_DASH, 0, false);
            if (st->m_validMask & XVfdField_BusVoltage) {
                snprintf(buf, sizeof(buf), "%.1f", st->m_busVoltageV);
                hsfStatSet(self->m_pumpStat[5], buf, HOME_CLR_TEXT, false);
            } else hsfStatSet(self->m_pumpStat[5], HOME_TXT_DASH, 0, false);
            if (st->m_validMask & XVfdField_OutputPowerKw) {
                snprintf(buf, sizeof(buf), "%.2f", st->m_outputPowerKw);
                hsfStatSet(self->m_pumpStat[6], buf, HOME_CLR_TEXT, false);
            } else hsfStatSet(self->m_pumpStat[6], HOME_TXT_DASH, 0, false);
            if (st->m_validMask & XVfdField_SpeedRpm) {
                snprintf(buf, sizeof(buf), "%.1f", st->m_speedRpm);
                hsfStatSet(self->m_pumpStat[7], buf, HOME_CLR_TEXT, false);
            } else hsfStatSet(self->m_pumpStat[7], HOME_TXT_DASH, 0, false);
            if (st->m_validMask & XVfdField_TorquePercent) {
                snprintf(buf, sizeof(buf), "%.1f", st->m_torquePercent);
                hsfStatSet(self->m_pumpStat[8], buf, HOME_CLR_TEXT, false);
            } else hsfStatSet(self->m_pumpStat[8], HOME_TXT_DASH, 0, false);
            if (st->m_faulted) {
                snprintf(buf, sizeof(buf), "0x%04X", st->m_faultCode);
                hsfStatSet(self->m_pumpStat[9], buf, HOME_CLR_FAULT, true);
            } else {
                hsfStatSet(self->m_pumpStat[9], HOME_TXT_NO_FAULT,
                           HOME_CLR_OK, true);
            }
        } else {
            for (i = 0; i < 10; ++i)
                hsfStatSet(self->m_pumpStat[i], HOME_TXT_NO_DATA,
                           HOME_CLR_OFFLINE, true);
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
                for (c = 0; c < 5; ++c) {
                    XTableWidgetItem* it = XTableWidgetItem_create_utf8("", 0);
                    if (it) {
                        XTableWidget_setItem(&self->m_table, i, c, it);
                        /* [居中] Python 侧第 1 列之外全部居中
                         * （item.setTextAlignment(Qt.AlignCenter)）。 */
                        if (c > 0)
                            XTableWidgetItem_setTextAlignment(it, HOME_ALIGN_CENTER);
                        /* [隔行浅底] Python setAlternatingRowColors(True)
                         * 的等价实现（框架无交替行色开关 ⇒ 逐格铺底色）。 */
                        if (i % 2 == 1) {
                            XColor bg;
                            XColor_init_rgb(&bg, 0xF5, 0xF7, 0xFA, 0xFF);
                            XTableWidgetItem_setBackground(it, &bg);
                        }
                    }
                }
                /* [行高 32] 对齐 Python theme.TABLE_ROW_H。 */
                XTableView_setRowHeight(&self->m_table.m_base, i, HOME_ROW_H);
            }
            /* [表格高度贴合行数] Python fit_to_content：表高跟内容走，
             * 否则表格下方拖出一大块空白（页面看着很空）。 */
            XWidget_setFixedHeight((XWidget*)&self->m_table,
                                   HOME_HEAD_H + n * HOME_ROW_H);
        }
        for (i = 0; i < n; ++i) {
            XTableWidgetItem* it;
            it = XTableWidget_item(&self->m_table, i, 0);
            XTableWidgetItem_setText_utf8(it, devs[i].m_name[0] ? devs[i].m_name : "-");
            it = XTableWidget_item(&self->m_table, i, 1);
            XTableWidgetItem_setText_utf8(it, HsfLinkClient_kindText(devs[i].m_kind));
            it = XTableWidget_item(&self->m_table, i, 2);
            XTableWidgetItem_setText_utf8(it, HsfLinkClient_healthText(devs[i].m_health));
            /* [状态上色] 对齐 Python：状态格按健康色着色
             * （0 正常绿 / 1,2 告警橙 / 3 离线灰 / 4 故障红）。 */
            {
                uint32_t clr = HOME_CLR_TEXT;
                if (devs[i].m_health == 0) clr = HOME_CLR_OK;
                else if (devs[i].m_health == 1 || devs[i].m_health == 2) clr = HOME_CLR_WARN;
                else if (devs[i].m_health == 3) clr = HOME_CLR_OFFLINE;
                else if (devs[i].m_health == 4) clr = HOME_CLR_FAULT;
                if (it) {
                    XColor fg;
                    /* [0xAARRGGBB 取色] 这批常量是 0xAARRGGBB（alpha 在最高
                     * 字节），取 rgb 要 >>16 / >>8 / >>0——早先按 >>24 取红，
                     * 结果"正常"显示成粉色（0xFF,0x2E,0x7D）。 */
                    XColor_init_rgb(&fg, (int)((clr >> 16) & 0xFFu),
                                    (int)((clr >> 8) & 0xFFu),
                                    (int)(clr & 0xFFu), 0xFF);
                    XTableWidgetItem_setForeground(it, &fg);
                }
            }
            it = XTableWidget_item(&self->m_table, i, 3);
            /* 地址（服务端下发；空显示 "-"）。 */
            XTableWidgetItem_setText_utf8(it, devs[i].m_addr[0] ? devs[i].m_addr : "-");
            it = XTableWidget_item(&self->m_table, i, 4);
            /* 错误信息（Python 同款：一列装"错误码 或 说明文字"）。 */
            if (devs[i].m_text[0]) {
                XTableWidgetItem_setText_utf8(it, devs[i].m_text);
            } else if (devs[i].m_error != 0) {
                char eb[32];
                snprintf(eb, sizeof(eb), "0x%04X", devs[i].m_error);
                XTableWidgetItem_setText_utf8(it, eb);
            } else {
                XTableWidgetItem_setText_utf8(it, "-");
            }
        }
        XWidget_update((XWidget*)&self->m_table);
    }

    /* [浮层跟随页面尺寸] 弹出期间每拍校正居中/遮罩覆盖（窗口缩放 200ms 内
     * 跟随；applyDetail 里的几何只算一次，缩放窗口后需要重算）。 */
    if (self->m_detailMode != HOME_DETAIL_NONE) {
        if (self->m_detailShade)
            XWidget_setGeometry((XWidget*)self->m_detailShade, 0, 0,
                                (int)XWidget_width(&self->m_base),
                                (int)XWidget_height(&self->m_base));
        hsfHomeCenterFloat(self);
    }
}

/* ==================== 创建 ==================== */

/** @brief 字号实时重放（本地设置改动时调用；只重设本页已知标签）。 */
static void homeApplyFonts(XWidget* page, int pct)
{
    HsfHomePage* self = (HsfHomePage*)page;
    HsfStatBlock* blocks[10];
    int i;
    if (!self || pct <= 0) return;
    XLabel_setTextPixelSize(&self->m_title, 17 * pct / 100);
    XLabel_setTextPixelSize(&self->m_subtitle, 12 * pct / 100);
    XLabel_setTextPixelSize(&self->m_devTitle, 14 * pct / 100);
    XLabel_setTextPixelSize(&self->m_dpNote, 12 * pct / 100);
    XLabel_setTextPixelSize(&self->m_fmSub, 12 * pct / 100);
    for (i = 0; i < 6; ++i) {
        XLabel_setTextPixelSize(&self->m_valveState[i], 12 * pct / 100);
        XLabel_setTextPixelSize(&self->m_valveName[i], 12 * pct / 100);
    }
    blocks[0] = self->m_thTemp;  blocks[1] = self->m_thHum;
    blocks[2] = self->m_vfdState; blocks[3] = self->m_vfdFreq;
    blocks[4] = self->m_vfdSet;  blocks[5] = self->m_devTotal;
    blocks[6] = self->m_devFault; blocks[7] = self->m_devOff;
    blocks[8] = self->m_dpValue; blocks[9] = self->m_fmFlow;
    for (i = 0; i < 10; ++i) {
        HsfStatBlock* sb = blocks[i];
        if (!sb) continue;
        XLabel_setTextPixelSize(&sb->m_caption, 12 * pct / 100);
        XLabel_setTextPixelSize(&sb->m_value, sb->m_valueSize * pct / 100);
        XLabel_setTextPixelSize(&sb->m_unit, 12 * pct / 100);
    }
    /* 行高/表头高随字号同步缩放（否则放大后数字被行高裁掉）。 */
    {
        const int rowH = HOME_ROW_H * pct / 100;
        const int headH = HOME_HEAD_H * pct / 100;
        const int n = self->m_tableRows;
        int r;
        for (r = 0; r < n; ++r)
            XTableView_setRowHeight(&self->m_table.m_base, r, rowH);
        XWidget_setFixedHeight((XWidget*)&self->m_table,
                               headH + n * rowH);
        /* 列宽随字号同步缩放。 */
        {
            static const int kW[5] = { 150, 85, 80, 330, 265 };
            int c;
            for (c = 0; c < 5; ++c)
                XTableView_setColumnWidth(&self->m_table.m_base, c,
                                          kW[c] * pct / 100);
        }
    }
}

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
    /* [单实例页] 浮层 × 关闭槽经 s_homePage 反推页对象——不赋值槽内
     * 直接 return，× 钮点了没反应（实测 2026-11-06）。 */
    s_homePage = self;
    HsfTheme_applyDefaultFont(&self->m_base);
    /* [页根横向铺满] 设备表要放下"COM17 · 115200 · 站 2"这类地址，页根若按
     * 内容宽度（Preferred）收缩，表格就被挤在左侧、右侧大片留白。 */
    XWidget_setSizePolicy((XWidget*)&self->m_base,
                          XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Preferred);

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
    XLabel_setTextPixelSize(&self->m_title, homePx(17));
    XLabel_setText_2(&self->m_title, HOME_TXT_TITLE);
    XWidget_show((XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_headerRow, (XWidget*)&self->m_title);

    XLabel_init(&self->m_subtitle, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_subtitle);
    XLabel_setTextPixelSize(&self->m_subtitle, homePx(12));
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

        /* [设备图形] 每卡右上角一枚矢量简笔水印（2026-11-06）。 */
        hsfCardSetGlyph(thCard, HOME_GLYPH_TH_HUM);
        hsfCardSetGlyph(vfdCard, HOME_GLYPH_PUMP);
        hsfCardSetGlyph(hCard, HOME_GLYPH_HEALTH);

        /* --- 电箱温度卡：温度 / 湿度 横排 --- */
        if (thCard) {
            thRow = (XBoxLayout*)XMemory_malloc(sizeof(XBoxLayout),
                                                XCLASS_DEFAULT_MEMORY_TYPE);
            if (thRow) {
                XBoxLayout_init(thRow, XBoxLayoutDirection_LeftToRight);
                XLayout_setSpacing((XLayout*)thRow, 16);
                self->m_thTemp = hsfStatCreate(&thCard->m_base, HOME_TXT_TH_TEMP,
                                               "℃", HOME_TXT_DASH, 26);
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
            /* [可点击卡 2026-11-06] 搅拌泵卡=下方详情槽切泵详情。 */
            hsfCardSetClicked(vfdCard, hsfHomePumpClicked, self);
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
            /* [可点击卡 2026-11-06] 健康卡=展开/收起下方设备状态表。 */
            hsfCardSetClicked(hCard, hsfHomeHealthClicked, self);
        }
    }
    XBoxLayout_addLayout(&self->m_layout, (XLayout*)&self->m_cardsRow);

    /* ---- 第二行三卡：压差 / 流量计 / 电磁阀（六路合一）---- */
    /* [2026-11-06] 压差=模拟量通道 0（现场"压差计"，4~20mA→0~1MPa，服务端
     * 换算下发 MPa 工程值）；流量计=B 侧瞬时流量+正/反向累计；电磁阀=六
     * 路状态一行六格（色随状态）。数据源全部现成访问器，刷新见 hsfHomeRefresh。 */
    XBoxLayout_init(&self->m_cardsRow2, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&self->m_cardsRow2, 10);
    {
        HsfHomeCard* dpCard = hsfCardCreate(&self->m_base, HOME_TXT_CARD_DP);
        HsfHomeCard* fmCard = hsfCardCreate(&self->m_base, HOME_TXT_CARD_FM);
        self->m_valveCard = hsfCardCreate(&self->m_base, HOME_TXT_CARD_VALVE);
        hsfCardSetGlyph(dpCard, HOME_GLYPH_PRESSURE);
        hsfCardSetGlyph(fmCard, HOME_GLYPH_FLOW);
        /* [阀卡图形恢复] 状态格已沉到卡下部（2×3），右上角还给图形。 */
        hsfCardSetGlyph(self->m_valveCard, HOME_GLYPH_VALVE);

        /* --- 压差卡：MPa 读数 + 通道名/越限说明小字 --- */
        if (dpCard) {
            /* [最小高 120] 第二行的行高=最高子卡提示；stat(48)+note(16) 的
             * 内容提示若不被计入，行高塌到 ~70px ⇒ stat 内部标题/大字重叠
             * （实测 2026-11-06）。显式托底。 */
            XWidget_setMinimumHeight((XWidget*)dpCard, 120);
            self->m_dpValue = hsfStatCreate(&dpCard->m_base, HOME_TXT_DP_CAP,
                                            "MPa", HOME_TXT_DASH, 26);
            /* [必须入布局] stat 块是卡片 base 的子控件，不进 m_body 就没有
             * 几何管理，落在 (0,0) 与标题重叠（实测 2026-11-06）。 */
            if (self->m_dpValue)
                XBoxLayout_addWidget(&dpCard->m_body,
                                     (XWidget*)&self->m_dpValue->m_base);
            XLabel_init(&self->m_dpNote, &dpCard->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_dpNote);
            XLabel_setTextPixelSize(&self->m_dpNote, homePx(12));
            XLabel_setText_2(&self->m_dpNote, HOME_TXT_DASH);
            XWidget_show((XWidget*)&self->m_dpNote);
            XBoxLayout_addWidget(&dpCard->m_body, (XWidget*)&self->m_dpNote);
            XBoxLayout_addWidgetEx(&self->m_cardsRow2, &dpCard->m_base, 1, 0);
        }

        /* --- 流量计卡：瞬时流量 + 正/反向累计副行 --- */
        if (fmCard) {
            XWidget_setMinimumHeight((XWidget*)fmCard, 120);
            self->m_fmFlow = hsfStatCreate(&fmCard->m_base, HOME_TXT_FM_CAP,
                                           "m3/h", HOME_TXT_DASH, 26);
            /* [必须入布局] 同压差卡：漏加则 stat 块与标题重叠。 */
            if (self->m_fmFlow)
                XBoxLayout_addWidget(&fmCard->m_body,
                                     (XWidget*)&self->m_fmFlow->m_base);
            XLabel_init(&self->m_fmSub, &fmCard->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_fmSub);
            XLabel_setTextPixelSize(&self->m_fmSub, homePx(12));
            XLabel_setText_2(&self->m_fmSub, HOME_TXT_DASH);
            XWidget_show((XWidget*)&self->m_fmSub);
            XBoxLayout_addWidget(&fmCard->m_body, (XWidget*)&self->m_fmSub);
            XBoxLayout_addWidgetEx(&self->m_cardsRow2, &fmCard->m_base, 1, 0);
        }

        /* --- 电磁阀卡：阀图形（右上）+ 两行三列状态格（卡下部）---
         *  [2026-11-07 改版] 一行六格会被右上图形盖住第六格（实测），改为
         *  2×3 网格沉到卡下部：图形区与状态区上下分离，互不遮挡。 */
        if (self->m_valveCard) {
            XWidget_setMinimumHeight((XWidget*)self->m_valveCard, 150);
            XGridLayout* vGrid = (XGridLayout*)XMemory_malloc(
                sizeof(XGridLayout), XCLASS_DEFAULT_MEMORY_TYPE);
            XBoxLayout_addStretch(&self->m_valveCard->m_body, 1);
            if (vGrid) {
                int k;
                XGridLayout_init(vGrid);
                XLayout_setContentsMargins((XLayout*)vGrid, 0, 0, 0, 0);
                XLayout_setSpacing((XLayout*)vGrid, 4);
                for (k = 0; k < HOME_VALVE_N; ++k) {
                    /* [每格容器 widget] 网格 addWidget 只收 widget——布局
                     * 指针强转传入是未定义行为（实测首格甩到标题区、其余
                     * 格子丢失）。每格先建容器再挂竖排布局。 */
                    XWidget* tileHost = XWidget_create(
                        &self->m_valveCard->m_base, 0);
                    XBoxLayout* tile = (XBoxLayout*)XMemory_malloc(
                        sizeof(XBoxLayout), XCLASS_DEFAULT_MEMORY_TYPE);
                    if (!tileHost || !tile) continue;
                    XBoxLayout_init(tile, XBoxLayoutDirection_TopToBottom);
                    XLayout_setSpacing((XLayout*)tile, 2);
                    XWidget_setLayout(tileHost, (XLayout*)tile);
                    XWidget_show(tileHost);
                    XLabel_init(&self->m_valveState[k], tileHost, 0);
                    HsfTheme_applyDefaultFont((XWidget*)&self->m_valveState[k]);
                    XLabel_setTextPixelSize(&self->m_valveState[k], homePx(12));
                    XLabel_setAlignment(&self->m_valveState[k],
                                        XAlignment_Left);
                    XLabel_setText_2(&self->m_valveState[k], HOME_TXT_DASH);
                    XWidget_show((XWidget*)&self->m_valveState[k]);
                    XBoxLayout_addWidget(tile, (XWidget*)&self->m_valveState[k]);
                    XLabel_init(&self->m_valveName[k], tileHost, 0);
                    HsfTheme_applyDefaultFont((XWidget*)&self->m_valveName[k]);
                    XLabel_setTextPixelSize(&self->m_valveName[k], homePx(12));
                    XWidget_setForegroundRole((XWidget*)&self->m_valveName[k],
                                              XPaletteColorRole_Mid);
                    XLabel_setText_2(&self->m_valveName[k], HOME_TXT_DASH);
                    XWidget_show((XWidget*)&self->m_valveName[k]);
                    XBoxLayout_addWidget(tile, (XWidget*)&self->m_valveName[k]);
                    XGridLayout_addWidget(vGrid, tileHost,
                                          k / 3, k % 3, 0);
                }
                XBoxLayout_addLayout(&self->m_valveCard->m_body,
                                     (XLayout*)vGrid);
            }
            XBoxLayout_addWidgetEx(&self->m_cardsRow2,
                                   &self->m_valveCard->m_base, 1, 0);
            /* [可点击卡 2026-11-06] 电磁阀卡=弹出阀详情浮层。 */
            hsfCardSetClicked(self->m_valveCard, hsfHomeValveClicked, self);
        }
    }
    XBoxLayout_addLayout(&self->m_layout, (XLayout*)&self->m_cardsRow2);
    /* [页尾弹性 2026-11-06] 设备状态表改浮层后页内容变矮，widgetResizable
     * 的滚动壳会把整页拉伸到视口高——无弹性项时多余高度全被卡片行/页头
     * 吃掉（卡内大片空白、标题下坠，实测）。尾部 stretch=1 收编全部剩余
     * 高度：卡片保持紧凑原高、多余空间沉到页底留白。 */
    XBoxLayout_addStretch(&self->m_layout, 1);

    /* ---- 详情浮层 + 触摸遮罩（点卡弹出、居中悬浮；点浮层外/×/再点同卡关）----
     *  替代布局流内的设备状态区：设备表与泵详情都装进这张**悬浮卡**，
     *  弹出时居中+抬升到页面最上，遮罩垫底捕获"点外面"。 */
    self->m_detailShade = (struct HsfHomeShade*)hsfHomeShadeCreate(
        &self->m_base, hsfHomeShadeClicked, self);

    self->m_detailFloat = hsfCardCreate(&self->m_base, NULL);
    if (self->m_detailFloat) {
        /* 标题行：浮层标题（随态切文字）+ × 关闭钮。 */
        XBoxLayout* titleRow = (XBoxLayout*)XMemory_malloc(
            sizeof(XBoxLayout), XCLASS_DEFAULT_MEMORY_TYPE);
        XLabel_init(&self->m_devTitle, &self->m_detailFloat->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_devTitle);
        {
            XFont f = XWidget_font((XWidget*)&self->m_devTitle);
            XFont_setBold(&f, true);
            XWidget_setFont((XWidget*)&self->m_devTitle, &f);
            XClassDeinit(&f);
        }
        XLabel_setTextPixelSize(&self->m_devTitle, homePx(14));
        XLabel_setText_2(&self->m_devTitle, HOME_TXT_DEVSTATUS);
        XWidget_show((XWidget*)&self->m_devTitle);

        XPushButton_init(&self->m_floatClose, &self->m_detailFloat->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_floatClose);
        XPushButton_setText_2(&self->m_floatClose, "×");   /* × 关闭 */
        XWidget_setFixedSize((XWidget*)&self->m_floatClose, 28, 28);
        XObject_connect_1((XObject*)&self->m_floatClose,
                          XSignal(XAbstractButton_clicked_signal),
                          (XObject*)&self->m_floatClose, hsfHomeFloatCloseSlot,
                          XConnectionType_Direct);
        XWidget_show((XWidget*)&self->m_floatClose);

        if (titleRow) {
            XBoxLayout_init(titleRow, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)titleRow, 8);
            XBoxLayout_addWidget(titleRow, (XWidget*)&self->m_devTitle);
            XBoxLayout_addStretch(titleRow, 1);
            XBoxLayout_addWidget(titleRow, (XWidget*)&self->m_floatClose);
            XBoxLayout_addLayout(&self->m_detailFloat->m_body,
                                 (XLayout*)titleRow);
        }
    }

    /* [表格建进浮层] 表的父=浮层 base；浮层收起时表随父隐藏。 */
    XTableWidget_init(&self->m_table,
                      self->m_detailFloat ? (XWidget*)&self->m_detailFloat->m_base
                                          : &self->m_base,
                      0);
    /* [表格横向铺满] 地址列要放得下"COM17 · 115200 · 站 2"，故表格本身
     * 必须吃掉整行宽度（否则各列按内容宽度排完、右侧留白，地址被挤成
     * 省略号）。 */
    XWidget_setSizePolicy((XWidget*)&self->m_table,
                          XWidgetSizePolicy_Expanding, XWidgetSizePolicy_Preferred);
    /* [5 列：设备/类型/状态/地址/错误信息] 与 Python 首页设备表**同列**
     * （Python 的 DataTable.setColumnCount(5) + 表头同款）。C 端原先多一
     * 列「说明」，把"错误码"与"说明"分了两列：Python 的「错误信息」列
     * 装的是**文字**（d.text），错误码根本没进表 ⇒ 两端列数/列义对不上。
     * 这里合并成一列：优先文字，无文字时退到错误码，都没有显示 "-"。 */
    XTableWidget_setColumnCount(&self->m_table, 5);
    {
        static const char* const kHeaders[5] = {
            "设备",   /* 设备 */
            "类型",   /* 类型 */
            "状态",   /* 状态 */
            "地址",   /* 地址 */
            "错误信息" /* 错误信息 */
        };
        XTableWidget_setHorizontalHeaderLabels(&self->m_table, kHeaders, 5);
    }
    XTableWidget_setRowCount(&self->m_table, 0);
    self->m_tableRows = 0;
    {
        /* 列宽策略：**固定像素宽**（对齐 Python 的列权重 4:2:2:7:5）。
         * [为什么不用 Stretch] 表格的 Stretch 模式按列数**均分**可视宽
         *   （xtw_stretchColumnsToViewport），5 列 × 178px 会把地址
         *   "COM17 · 115200 · 站 2" 挤成省略号——而地址恰是排障最需要的
         *   那一列；且列宽的事实源是 m_colWidths（须经
         *   XTableView_setColumnWidth 写），只改 header 不生效。
         *   Python 的权重也是按"可用宽 × 权重"算的（DataTable.
         *   set_column_weights），此处按同一比例给固定像素。 */
        XHeaderView* hh = XTableView_horizontalHeader(&self->m_table.m_base);
        if (hh)
            XHeaderView_setSectionResizeMode(hh, XHeaderViewResizeMode_Fixed);
        XTableView_setColumnWidth(&self->m_table.m_base, 0, 150);  /* 设备 */
        XTableView_setColumnWidth(&self->m_table.m_base, 1, 85);   /* 类型 */
        XTableView_setColumnWidth(&self->m_table.m_base, 2, 80);   /* 状态 */
        /* 地址：最宽（按钮组那行是"192.168.0.13:502 · DI12~DI15 · DO7~DO9"，
         * 38 字符；窄了就截成"DO…"，排障时正是这一段要抄给维修）。 */
        XTableView_setColumnWidth(&self->m_table.m_base, 3, 330);
        XTableView_setColumnWidth(&self->m_table.m_base, 4, 265);  /* 错误信息 */
    }
    /* [表格观感对齐 Python] ①无网格线（Python setShowGrid(False)）
     *   ②隔行浅底（Python setAlternatingRowColors(True)）
     *   ③行高 32px（Python theme.TABLE_ROW_H）
     *   ④不显示行号列（Python verticalHeader().setVisible(False)）
     * 四条都影响"看起来像不像同一套界面"，逐条对齐。 */
    XTableView_setShowGrid(&self->m_table.m_base, false);
    /* [不显示行号列] 对齐 Python verticalHeader().setVisible(False)。 */
    XTableWidget_setVerticalHeaderVisible(&self->m_table, false);
    XWidget_show((XWidget*)&self->m_table);
    if (self->m_detailFloat) {
        XBoxLayout_addWidget(&self->m_detailFloat->m_body,
                             (XWidget*)&self->m_table);
    }

    /* ---- 泵详情两行 × 5 项（浮层 body 内；Pump 态才显示）----
     *  完整读数（与变频器页同口径：字段有效性逐项判）。 */
    if (self->m_detailFloat) {
        static const char* const kPumpCaps[10] = {
            "运行状态",   /* 运行状态 */
            HOME_TXT_VFD_OUT,                                     /* 输出频率 */
            HOME_TXT_VFD_SET,                                     /* 设定频率 */
            HOME_TXT_DP_CURRENT,                                  /* 电流 */
            HOME_TXT_DP_VOLT,                                     /* 电压 */
            HOME_TXT_DP_BUS,                                      /* 母线电压 */
            HOME_TXT_DP_POWER,                                    /* 功率 */
            HOME_TXT_DP_SPEED,                                    /* 转速 */
            HOME_TXT_DP_TORQUE,                                   /* 转矩 */
            HOME_TXT_DP_FCODE                                     /* 故障码 */
        };
        static const char* const kPumpUnits[10] = {
            "", "Hz", "Hz", "A", "V", "V", "kW", "rpm", "%", ""
        };
        static const int kPumpSizes[10] = { 18, 20, 16, 18, 16, 16, 18, 18, 16, 16 };
        XBoxLayout* rows[2];
        int r2;
        int c2;
        rows[0] = (XBoxLayout*)XMemory_malloc(sizeof(XBoxLayout),
                                              XCLASS_DEFAULT_MEMORY_TYPE);
        rows[1] = (XBoxLayout*)XMemory_malloc(sizeof(XBoxLayout),
                                              XCLASS_DEFAULT_MEMORY_TYPE);
        for (r2 = 0; r2 < 2; ++r2) {
            if (!rows[r2]) continue;
            XBoxLayout_init(rows[r2], XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)rows[r2], 16);
            for (c2 = 0; c2 < 5; ++c2) {
                const int idx = r2 * 5 + c2;
                self->m_pumpStat[idx] = hsfStatCreate(
                    &self->m_detailFloat->m_base, kPumpCaps[idx],
                    kPumpUnits[idx], HOME_TXT_DASH, kPumpSizes[idx]);
                if (self->m_pumpStat[idx])
                    XBoxLayout_addWidgetEx(rows[r2],
                        (XWidget*)&self->m_pumpStat[idx]->m_base, 1, 0);
            }
            XBoxLayout_addLayout(&self->m_detailFloat->m_body,
                                 (XLayout*)rows[r2]);
        }
    }

    /* ---- 阀详情瓷贴 ×6（2 列 × 3 行网格；VALVE 态才显示）----
     *  每贴：状态圆点+阀名 / 状态文字（着色）/ 到位双点（实心=到位、
     *  空心=未到）——视觉对齐电磁阀页卡片。 */
    if (self->m_detailFloat) {
        XGridLayout* grid = (XGridLayout*)XMemory_malloc(
            sizeof(XGridLayout), XCLASS_DEFAULT_MEMORY_TYPE);
        int k;
        if (grid) {
            XGridLayout_init(grid);
            XLayout_setContentsMargins((XLayout*)grid, 0, 4, 0, 0);
            XLayout_setSpacing((XLayout*)grid, 10);
            XBoxLayout_addLayout(&self->m_detailFloat->m_body,
                                 (XLayout*)grid);
        }
        for (k = 0; k < HOME_VALVE_N; ++k) {
            HsfHomeValveTile* t = &self->m_vTiles[k];
            XBoxLayout* fb = (XBoxLayout*)XMemory_malloc(
                sizeof(XBoxLayout), XCLASS_DEFAULT_MEMORY_TYPE);
            /* [必须 init] malloc 出来的布局必须先 init（否则内部字段是
             * cdcd 未初始化堆模式，后续 addWidget/几何都会踩坏堆——实测
             * 启动即 _CrtIsValidHeapPointer 断言，2026-11-06）。 */
            XBoxLayout_init(fb, XBoxLayoutDirection_LeftToRight);
            memset(t, 0, sizeof(*t));
            XWidget_init(&t->m_base, &self->m_detailFloat->m_base, 0);
            HsfTheme_applyDefaultFont(&t->m_base);
            XBoxLayout_init(&t->m_box, XBoxLayoutDirection_TopToBottom);
            XLayout_setContentsMargins((XLayout*)&t->m_box, 8, 6, 8, 6);
            XLayout_setSpacing((XLayout*)&t->m_box, 4);
            XWidget_setLayout(&t->m_base, (XLayout*)&t->m_box);

            /* 行1：阀小图形（色随状态）+ 阀名。 */
            {
                XWidget_init(&t->m_stateDot.m_base, &t->m_base, 0);
                XClassGetVtable(&t->m_stateDot.m_base) =
                    HsfHomeDot_class_init();
                t->m_stateDot.m_color = 0xFF9E9E9Eu;
                t->m_stateDot.m_filled = true;
                t->m_stateDot.m_diam = 20;
                t->m_stateDot.m_glyphValve = true;
                XWidget_setFixedSize(&t->m_stateDot.m_base, 26, 22);
                XWidget_show(&t->m_stateDot.m_base);
            }
            XLabel_init(&t->m_name, &t->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&t->m_name);
            XLabel_setTextPixelSize(&t->m_name, homePx(12));
            XLabel_setText_2(&t->m_name, HOME_TXT_DASH);
            XWidget_show((XWidget*)&t->m_name);
            {
                XBoxLayout* r1 = (XBoxLayout*)XMemory_malloc(
                    sizeof(XBoxLayout), XCLASS_DEFAULT_MEMORY_TYPE);
                if (r1) {
                    XBoxLayout_init(r1, XBoxLayoutDirection_LeftToRight);
                    XLayout_setSpacing((XLayout*)r1, 6);
                    XBoxLayout_addWidget(r1, (XWidget*)&t->m_stateDot);
                    XBoxLayout_addWidget(r1, (XWidget*)&t->m_name);
                    XBoxLayout_addStretch(r1, 1);
                    XBoxLayout_addLayout(&t->m_box, (XLayout*)r1);
                }
            }

            /* 行2：状态文字（着色）。 */
            XLabel_init(&t->m_state, &t->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&t->m_state);
            {
                XFont f2 = XWidget_font((XWidget*)&t->m_state);
                XFont_setBold(&f2, true);
                XWidget_setFont((XWidget*)&t->m_state, &f2);
            }
            XLabel_setTextPixelSize(&t->m_state, homePx(14));
            XLabel_setText_2(&t->m_state, HOME_TXT_DASH);
            XWidget_show((XWidget*)&t->m_state);
            XBoxLayout_addWidget(&t->m_box, (XWidget*)&t->m_state);

            /* 行3：到位双点（实心绿=到位、空心灰=未到）。 */
            XLabel_init(&t->m_fbOpenCap, &t->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&t->m_fbOpenCap);
            XLabel_setTextPixelSize(&t->m_fbOpenCap, homePx(11));
            XWidget_setForegroundRole((XWidget*)&t->m_fbOpenCap,
                                      XPaletteColorRole_Mid);
            XLabel_setText_2(&t->m_fbOpenCap, HOME_TXT_FB_OPEN);
            XWidget_show((XWidget*)&t->m_fbOpenCap);
            {
                XWidget_init(&t->m_fbOpenDot.m_base, &t->m_base, 0);
                XClassGetVtable(&t->m_fbOpenDot.m_base) =
                    HsfHomeDot_class_init();
                t->m_fbOpenDot.m_color = 0xFF9E9E9Eu;
                t->m_fbOpenDot.m_filled = false;
                t->m_fbOpenDot.m_diam = 12;
                XWidget_setFixedSize(&t->m_fbOpenDot.m_base, 18, 18);
                XWidget_show(&t->m_fbOpenDot.m_base);
            }
            XLabel_init(&t->m_fbCloseCap, &t->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&t->m_fbCloseCap);
            XLabel_setTextPixelSize(&t->m_fbCloseCap, homePx(11));
            XWidget_setForegroundRole((XWidget*)&t->m_fbCloseCap,
                                      XPaletteColorRole_Mid);
            XLabel_setText_2(&t->m_fbCloseCap, HOME_TXT_FB_CLOSE);
            XWidget_show((XWidget*)&t->m_fbCloseCap);
            {
                XWidget_init(&t->m_fbCloseDot.m_base, &t->m_base, 0);
                XClassGetVtable(&t->m_fbCloseDot.m_base) =
                    HsfHomeDot_class_init();
                t->m_fbCloseDot.m_color = 0xFF9E9E9Eu;
                t->m_fbCloseDot.m_filled = false;
                t->m_fbCloseDot.m_diam = 12;
                XWidget_setFixedSize(&t->m_fbCloseDot.m_base, 18, 18);
                XWidget_show(&t->m_fbCloseDot.m_base);
            }
            if (fb) {
                XBoxLayout_addWidget(fb, (XWidget*)&t->m_fbOpenCap);
                XBoxLayout_addWidget(fb, (XWidget*)&t->m_fbOpenDot);
                XBoxLayout_addStretch(fb, 1);
                XBoxLayout_addWidget(fb, (XWidget*)&t->m_fbCloseCap);
                XBoxLayout_addWidget(fb, (XWidget*)&t->m_fbCloseDot);
                XBoxLayout_addLayout(&t->m_box, (XLayout*)fb);
            }

            /* 入浮层网格（2 列 × 3 行）。 */
            if (grid)
                XGridLayout_addWidget(grid, (XWidget*)&t->m_base,
                                      k / 2, k % 2, 0);
        }
    }


    /* [详情浮层初始态] 收起（浮层/遮罩都隐藏；点卡片弹出，点外关闭）。 */
    hsfHomeApplyDetail(self);

    /* 兼容自测钩子（env XHSF_HOME_AUTOTEST）：非空即弹浮层——值 2/valve/V
     * 置阀详情态，其余置泵详情态；截图闭环验证浮层弹出/居中/遮罩
     * （数字量/模拟量页 autotest 同款口径）。
     * [1/h/H 置设备状态表明细] 供截图核对末列贴边与空列问题。 */
    if (XSystem_environment("XHSF_HOME_AUTOTEST")) {
        const char* want = XSystem_environment("XHSF_HOME_AUTOTEST");
        if (want && (want[0] == '2' || want[0] == 'v' || want[0] == 'V'))
            self->m_detailMode = HOME_DETAIL_VALVE;
        else if (want && (want[0] == '1' || want[0] == 'h' || want[0] == 'H'))
            self->m_detailMode = HOME_DETAIL_HEALTH;
        else
            self->m_detailMode = HOME_DETAIL_PUMP;
        hsfHomeApplyDetail(self);
    }

    /* ---- 首拍刷新（避免开局全 "--" 到下一拍） ---- */
    hsfHomeRefresh(self);

    /* 字号实时重放登记（本地设置改动时由 HsfUi 调回）。 */
    hsfUi_registerPageFontApplier(HSF_PAGE_HOME, &self->m_base, homeApplyFonts);
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
