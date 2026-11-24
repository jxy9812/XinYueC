/**
 * @file       HsfHomePage.c
 * @brief      主界面页实现：自动模式卡（首行）+ 三张总览卡片 + 设备状态表。
 * @details    层次（对标 Python 版 pages_other.py 的 HomePage）：
 *               1) **自动模式卡（首行）**（2026-11-11 三模式；2026-11-12
 *                  用户定案「删掉页头那行字、首行放自动模式卡」——原先的
 *                  页头行（大标题「主界面」+ 灰色副标题「高速过滤器监控
 *                  系统」）**整行已删**，操作员最常用的入口放最上面）：
 *                    模式下拉（产水/反洗/顺洗/自动·流量循环）+ 启动/停止/
 *                    复位钮 + 状态行（服务端 autoMode/autoPhase/autoFault
 *                    三键回显，1s 节流拉一次 RUN_MODE；序列逻辑全在服务端，
 *                    客户端只发意图 + 回显真实相位，不自行推断）
 *               2) 总览卡片行（横排三张，各占 1/3）：
 *                    · 电箱温度卡：电箱温度(℃) / 电箱湿度(%RH)
 *                    · 变频器卡  ：运行状态 / 输出频率(Hz) / 转速(rpm)
 *                    · 设备健康卡：设备总数 / 故障 / 离线
 *               3) 第二行三卡：压差 / 流量计 / 电动球阀（六路合一）
 *               4) 第三行两卡：浊度仪（两台 + 进水泵状态）/ 按钮组系统状态
 *               5) 设备状态表：点健康卡弹浮层（设备/类型/状态/地址/错误信息）
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
#include "HsfPages.h"            /* HsfPages_setStatus（模式卡状态栏反馈）。 */
#include "XhsfLinkProto.h"
#include "XLabel.h"
#include "XPushButton.h"
#include "XComboBox.h"           /* 自动模式卡：模式下拉。 */
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
#include "XString.h"             /* 模式下拉 itemData 的 UTF-8 取值。 */
#include "XDateTime.h"           /* 压差卡新鲜度（自记 lastMs 口径）。 */
#include "XSystem.h"             /* 环境变量自测钩子（XHSF_HOME_AUTOTEST）。 */
#if XVFD_ON
#include "XVfdDevice.h"
#endif
#include <stdarg.h>              /* 模式卡状态栏提示（vsnprintf 可变参）。 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>                /* 表盘弧带 cos/sin（漏含曾致 C4013「假设
                                 * 外部返回 int」——弧坐标是隐式声明的
                                 * 错误返回值，HsfMonitorVfd.c 同源代码
                                 * 有含此头）。 */

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/** @brief 本页字号折算（本地设置 FontScale/Page0）。 */
static int homePx(int px)
{
    return hsfUi_pageFontPx(HSF_PAGE_HOME, px);
}

/* ==================== 配色（与主题同源） ==================== */

/* [深色主题令牌化 2026-11-11 双主题走查] 主页卡片/读数是**页面自绘面 +
 * 文本**，旗舰深色盘挂上后白卡面不变、而跟随调色板的卡标题翻亮 ⇒
 * (232,234,237) 落白底 1.21:1（8 张卡标题+「无数据」「0 台」等全中，
 * 走查实锅）。现按 HsfTheme_isDarkSkin() 取亮/深两套：亮套字面量维持
 * 现行值零改动（亮色走查达标，不为改而改）；深套取 HsfTheme.h 深色
 * 令牌（面板/正文/次级/四态语义深套，取值登记见 HsfTheme.h）。宏展开
 * 为函数调用：刷新每拍按当前主题取色，主题切换后下一拍即换套。 */
static uint32_t homeThemeClr(uint32_t light, uint32_t dark)
{
    return HsfTheme_isDarkSkin() ? dark : light;
}

#define HOME_CLR_TEXT        homeThemeClr(0xFF1A1A1Au, HSF_COLOR_TEXT_DARK)
#define HOME_CLR_TEXT_DIM    homeThemeClr(0xFF5A6572u, \
                                          HSF_COLOR_TEXT_SECONDARY_DARK)
#define HOME_CLR_OK          homeThemeClr(0xFF2E7D32u, \
                                          HSF_COLOR_STATE_OK_DARK)
#define HOME_CLR_WARN        homeThemeClr(0xFFED6C02u, \
                                          HSF_COLOR_STATE_WARN_DARK)
#define HOME_CLR_FAULT       homeThemeClr(0xFFC62828u, \
                                          HSF_COLOR_STATE_FAULT_DARK)
#define HOME_CLR_OFFLINE     homeThemeClr(0xFF757575u, \
                                          HSF_COLOR_STATE_OFFLINE_DARK)
/* [倒计时表盘 2026-11-12] 剩余 >60s 的「信息蓝」（Python CLR_INFO 同位）。
 * [逐值对齐 2026-11-13] Python theme.CLR_INFO=#1A66D0，亮套原写 0x1976D2
 * 是 Material Blue 700——注释自称同位、值没抄对（G 通道差 0x10），按
 * Python 逐值订正；深套 0x64B5F6 与 Python 无对应（Python 无深色主题）。 */
#define HOME_CLR_INFO        homeThemeClr(0xFF1A66D0u, \
                                          0xFF64B5F6u)

/* 设备表观感（对齐 Python theme.TABLE_ROW_H 与 DataTable 的做法）。 */
#define HOME_ROW_H           32   /**< 行高（px；Python theme.TABLE_ROW_H）。 */
#define HOME_HEAD_H          34   /**< 表头高（px；行高 + 2px 分隔）。 */
#define HOME_ALIGN_CENTER    XPAINTER_TEXT_ALIGN_CENTER  /**< 居中（Qt AlignCenter 口径）。 */

/* [详情浮层手机档断点 2026-11-09] 页根宽 < 520（手机竖屏内容区）⇒ 浮层
 * 贴满内容区（页根矩形内缩 8px、跳过定宽居中）；>=520 走原定宽居中，
 * 宽档行为零变化。 */
#define HOME_FLOAT_FILL_MAX_W 520

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
    /* 次级文本令牌（原 Mid 角色：深色盘 Mid=#2E353D 是斜面色，落深底
     * 不可读——走查实锅，全页说明小字同口径）。 */
    HsfTheme_applySecondaryText((XWidget*)&sb->m_caption);
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
    HsfTheme_applySecondaryText((XWidget*)&sb->m_unit);
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
    HOME_GLYPH_VALVE,      /* 电磁阀：阀体蝶形 + 线圈块 */
    HOME_GLYPH_TURBIDITY,  /* 浊度仪：水滴 + 水面波纹（2026-11-09） */
    HOME_GLYPH_BUTTONS     /* 按钮组：立柱 + 三钮 + 顶灯（2026-11-09） */
} HsfHomeGlyph;

/* 卡角水印简笔三色：亮套=软蓝系（浅填充在白卡上成水印）；深套换暗色
 * 系（浅填充落深面板会成刺眼亮块，走查同源口径）。描边/强调用中性灰蓝，
 * 在深面板上 ≥3:1（#8A98A6 vs #23282F ≈ 3.5）。 */
#define HOME_GLYPH_STROKE  homeThemeClr(0xFF9DBCE8u, 0xFF8A98A6u)
#define HOME_GLYPH_FILL    homeThemeClr(0xFFE3EDFBu, 0xFF2A313Au)
#define HOME_GLYPH_ACCENT  homeThemeClr(0xFF7EA6E0u, 0xFF8A98A6u)

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

/* ---- 浊度仪：水滴 + 内部波纹（2026-11-09 新增卡）---- */
static void hsfGlyphTurbidity(XPainter* p, int gx, int gy, int s)
{
    XRect r;
    XPoint tri[3];
    int cx = gx + s * 50 / 100;
    int cy = gy + s * 60 / 100;      /* 圆心（水滴下半） */
    int rad = s * 22 / 100;
    int k;
    hsfGlyphPen(p, HOME_GLYPH_STROKE);
    /* 水滴：上尖三角 + 下圆。 */
    tri[0].x = cx;              tri[0].y = gy + s * 10 / 100;
    tri[1].x = cx + rad;        tri[1].y = cy;
    tri[2].x = cx - rad;        tri[2].y = cy;
    XPainter_drawPolyline(p, tri, 3);
    XRect_init(&r, cx - rad, cy - rad, rad * 2, rad * 2);
    XPainter_drawEllipse(p, &r);
    /* 水面波纹（强调色）：两道横线。 */
    XPainter_setPen(p, HOME_GLYPH_ACCENT);
    for (k = 0; k < 2; ++k) {
        int wy = cy - rad / 2 + k * (rad * 3 / 4);
        XPainter_drawLine(p, cx - rad * 3 / 5, wy, cx + rad * 3 / 5, wy);
    }
    XPainter_setPen(p, HOME_GLYPH_STROKE);
}

/* ---- 按钮组（现场操作柱）：立柱 + 三个按钮 + 顶部指示灯（2026-11-09）---- */
static void hsfGlyphButtons(XPainter* p, int gx, int gy, int s)
{
    XRect r;
    int k;
    hsfGlyphPen(p, HOME_GLYPH_STROKE);
    /* 底座 + 立柱。 */
    XPainter_drawLine(p, gx + s * 14 / 100, gy + s * 92 / 100,
                      gx + s * 72 / 100, gy + s * 92 / 100);
    XPainter_drawLine(p, gx + s * 44 / 100, gy + s * 92 / 100,
                      gx + s * 44 / 100, gy + s * 34 / 100);
    /* 三个按钮（圆），斜向排布。 */
    for (k = 0; k < 3; ++k) {
        int cx = gx + s * (26 + k * 17) / 100;
        int cy = gy + s * (44 + k * 14) / 100;
        int rad = s * 7 / 100;
        XRect_init(&r, cx - rad, cy - rad, rad * 2, rad * 2);
        XPainter_drawEllipse(p, &r);
    }
    /* 顶部指示灯（强调色）。 */
    XPainter_setPen(p, HOME_GLYPH_ACCENT);
    XPainter_setPenWidth(p, 3);
    XRect_init(&r, gx + s * 62 / 100, gy + s * 10 / 100,
               s * 16 / 100, s * 16 / 100);
    XPainter_drawEllipse(p, &r);
    XPainter_setPen(p, HOME_GLYPH_STROKE);
    XPainter_setPenWidth(p, 2);
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
    case HOME_GLYPH_TURBIDITY: hsfGlyphTurbidity(p, gx, gy, s); break;
    case HOME_GLYPH_BUTTONS:  hsfGlyphButtons(p, gx, gy, s); break;
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
    /* [阀简笔模式 2026-11-07] 状态点画成小阀图形（色随状态），替代圆点。
     * 填充第二参走 HOME_GLYPH_FILL（随主题，原浅蓝常量落深面板成亮块）。 */
    if (dot->m_glyphValve) {
        hsfGlyphValveColor(&painter,
                           (XWidget_width(self) - side) / 2,
                           (XWidget_height(self) - side) / 2, side,
                           dot->m_color, HOME_GLYPH_FILL);
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

/* ==================== 倒计时表盘（2026-11-12·对齐 Python RemainDial） ====================
 * [形态] 270° 缺口圆环（缺口朝下，起 225° 顺时针）+ 中央两行（小字
 * 「剩余/已运行」+ mm:ss 读数）。Python widgets.py RemainDial 同规则：
 *   · total>0（反洗/顺洗/产水定时兜底）：进度弧 = **剩余/总**（满环起步、
 *     逐步消耗的燃油表读法）；剩余>60s 信息蓝、>0 橙、归零红。
 *   · total=0（循环产水按压差切换）：无定时可画 ⇒ 只灰环，中央
 *     「已运行」mm:ss（不编造倒计时）。
 *   · elapsed<0（单模式启动/不在运行/未收到键）：无阶段 ⇒ 整件画空
 *     （见下「槽恒占位」）。
 * [槽恒占位 2026-11-13 用户口径] 表盘不再 show/hide——旧口径「无阶段隐藏」
 * 会让控制行高 88↔34 回流（卡高跳、下方卡片整体挪动）。槽位恒
 * dialPx×dialPx：无阶段（elapsed<0）时 paintEvent 画空（整件清透明），
 * 「自动模式以外不该显示表盘」的观感不变（看不见任何环/字/提示），布局
 * 零跳动。与 Python D13c「无阶段隐藏」用户观感等价（Python 藏件塌列、
 * C 端占位空画，两端都是「非自动看不见表盘」）。
 * [弧端形状 2026-11-13·对齐 Python] Python 底轨/进度弧都是 QPen + RoundCap
 * ⇒ 弧端圆头；本端走扇环路径填充（细线 drawArc 版被用户否——「画的超级
 * 难看」），端帽由 hsfDialFillArcBand 在两端各补一枚端点圆实现（见该函数）。 */

typedef struct HsfHomeDial {
    XWidget   m_base;      /**< 第一成员。 */
    int64_t   m_elapsedMs; /**< 已运行毫秒（-1 = 不在运行/无阶段）。 */
    int64_t   m_totalMs;   /**< 阶段总毫秒（0 = 本阶段无定时）。 */
    XImage*   m_staticCache; /**< 静态层缓存（底盘+底轨带；尺寸/主题变才重建）。 */
    int       m_cacheW;    /**< 缓存宽。 */
    int       m_cacheH;    /**< 缓存高。 */
    bool      m_cacheDark; /**< 缓存渲染时的主题（深=1）。 */
} HsfHomeDial;

/* [缺口方位 2026-11-13·对齐 Python ARC_START_DEG=225] 角度→屏幕映射与 Qt
 * 同制（x=cx+r·cos a、y=cy−r·sin a）：225°=7点半起步、顺时针（角度递减）
 * 扫 270° 到 4:30 ⇒ 缺口 4:30~7:30、中心正下方 6:00。原值 135 落在 10:30
 * ⇒ 缺口 7:30~10:30（中心左侧 9:00），与 Python 意图顺时针偏 90°——旧
 * 注释「缺口朝下」是按「y 向下坐标系里 135°=左下」心算写的，与实现映射
 * （y 向上）不符。HsfMonitorVfd.c 三仪表同批订正。 */
#define HOME_DIAL_ARC_START_DEG  225  /**< 缺口朝下（225° 起顺时针扫 270°）。 */
#define HOME_DIAL_ARC_SPAN_DEG   270

/** @brief 毫秒 → 「mm:ss」/「H:mm:ss」（表盘中央短读数；Python fmt_ms）。 */
static void hsfHomeDialFmtMs(int64_t ms, char* buf, int cap)
{
    int64_t secs = ms > 0 ? ms / 1000 : 0;
    int64_t hours = secs / 3600;
    int64_t rem = secs % 3600;
    int64_t minutes = rem / 60;
    int64_t seconds = rem % 60;
    if (hours > 0)
        snprintf(buf, (size_t)cap, "%lld:%02lld:%02lld",
                 (long long)hours, (long long)minutes, (long long)seconds);
    else
        snprintf(buf, (size_t)cap, "%02lld:%02lld",
                 (long long)minutes, (long long)seconds);
}

/* [倒计时表盘工艺 2026-11-12·对齐变频器仪表弧带（HsfMonitorVfd.c
 * vfdFillArcBand/vfdGaugePaint 同款）] 底盘整圆（主题白/深盘）+ 270° 底轨带
 * （软描边）+ 进度带（同带宽同轨，2026-11-13 对齐 Python）+ 静态层缓存
 * （底盘/底轨对尺寸与主题是常量 → 渲染进离屏图一次，每帧只 blit + 画进度
 * 带；变频器仪表实测 66ms → 6ms 的同款收口）。此前细线 drawArc 版本被用户
 * 否（「画的超级难看，不如变频器界面那个表盘」）；弧端圆头由端帽圆补齐
 * （对齐 Python QPen RoundCap，见 hsfDialFillArcBand）。 */

/** @brief 亚像素坐标取整（四舍五入；负值对称，不向零截断）。 */
static int hsfDialRoundI(double v)
{
    return (int)(v + (v >= 0.0 ? 0.5 : -0.5));
}

static void hsfDialFillCircle(XPainter* painter, int cx, int cy, int r,
                              uint32_t color);   /* 前置声明（端帽用）。 */

/** @brief 扇环填充（外弧→内弧→closeSubpath→fillPath 一次光栅化；抄
 *         HsfMonitorVfd.c vfdFillArcBand）。
 *  @details [首命令必须 moveTo 2026-11-13 实锅修复] 此前路径首命令是
 *           lineTo——框架对空路径 lineTo 隐式补 moveTo(0,0)（Qt 同语义，
 *           XPainter.c XPainterPath_lineTo），closeSubpath 再补线回 (0,0)
 *           ⇒ 填充多边形被硬塞进「原点→弧起点→…→圆心→原点」的额外环边，
 *           OddEven 规则下盘内绕偶数次的区域被挖空（盘心半透明）+ 原点
 *           方向拉出楔形伪影（探针实测不透明像素少 524）。照
 *           vfdFillArcBand 同款：先 moveTo 外弧起点、外弧从 i=1 起。
 *           [圆头弧端 2026-11-13·对齐 Python RoundCap] Python RemainDial
 *           底轨/进度弧都是 QPen+RoundCap ⇒ 弧端圆头；路径填充天然平头，
 *           这里在两端各补一枚以端点为圆心、带半宽为半径的整圆（重叠区
 *           同色无视觉差）。底盘（rInner=0）不补——全圆无「端」，也避免
 *           端帽递归。 */
static void hsfDialFillArcBand(XPainter* painter, int cx, int cy,
                               int rInner, int rOuter,
                               double startDeg, double sweepDeg,
                               uint32_t color)
{
    XPainterPath path;
    int i;
    int segments;
    double a0 = startDeg * 3.14159265358979323846 / 180.0;
    double sweepRad = sweepDeg * 3.14159265358979323846 / 180.0;
    double step;
    if (!painter || rOuter < 1 || sweepDeg == 0.0 || rOuter <= rInner)
        return;
    segments = (int)(2.0 * 3.14159265358979323846 * rOuter *
                     ((sweepDeg > 0 ? sweepDeg : -sweepDeg) / 360.0));
    if (segments < 24) segments = 24;
    if (segments > 720) segments = 720;
    step = sweepRad / (double)segments;
    XPainterPath_init(&path);
    XPainterPath_setFillRule(&path, XPainterFillRule_OddEven);
    XPainterPath_moveTo(&path,
                        (float)(cx + rOuter * cos(a0)),
                        (float)(cy - rOuter * sin(a0)));
    for (i = 1; i <= segments; ++i) {
        double a = a0 + step * (double)i;
        XPainterPath_lineTo(&path,
                            (float)(cx + rOuter * cos(a)),
                            (float)(cy - rOuter * sin(a)));
    }
    for (i = segments; i >= 0; --i) {
        double a = a0 + step * (double)i;
        XPainterPath_lineTo(&path,
                            (float)(cx + rInner * cos(a)),
                            (float)(cy - rInner * sin(a)));
    }
    XPainterPath_closeSubpath(&path);
    XPainter_setBrush(painter, color);
    XPainter_fillPath(painter, &path);
    XPainterPath_deinit(&path);
    if (rInner > 0) {
        const double capR = ((double)rOuter - (double)rInner) * 0.5;
        if (capR >= 0.5) {
            const double rMid = ((double)rInner + (double)rOuter) * 0.5;
            const double aEnd = a0 + sweepRad;
            hsfDialFillCircle(painter,
                              hsfDialRoundI((double)cx + rMid * cos(a0)),
                              hsfDialRoundI((double)cy - rMid * sin(a0)),
                              hsfDialRoundI(capR), color);
            hsfDialFillCircle(painter,
                              hsfDialRoundI((double)cx + rMid * cos(aEnd)),
                              hsfDialRoundI((double)cy - rMid * sin(aEnd)),
                              hsfDialRoundI(capR), color);
        }
    }
}

/** @brief 360° 全圆填充（表盘底盘）。 */
static void hsfDialFillCircle(XPainter* painter, int cx, int cy, int r,
                              uint32_t color)
{
    hsfDialFillArcBand(painter, cx, cy, 0, r, 0.0, 360.0, color);
}

/** @brief 表盘几何（2026-11-13·对齐 Python RemainDial.paintEvent 同公式）。
 *  @details side=min(w,h)；pad（贴边）=max(3, side*9%)、带宽=max(3,
 *           side*9%)；弧路径半径 rp=(side-2*pad)/2，带体以 rp 为**中心线**
 *           （外缘 rp+带宽/2、内缘 rp-带宽/2——QPen 描边沿路径两侧对称
 *           伸展的同款口径）。原 C 几何（d=min-8、带外缘 r-2、带宽
 *           w*11%≥4 且用 w 不是 min）比 Python 更粗更贴边，且非方窗时
 *           用 w 与 Python 的 side 口径分叉，已收口。 */
static void hsfHomeDialGeom(int w, int h, int* bandInner, int* bandOuter)
{
    double side = (double)((w < h) ? w : h);
    double pad = side * 0.09;
    double ringW = side * 0.09;
    double rp;
    double outer;
    double inner;
    if (pad < 3.0) pad = 3.0;
    if (ringW < 3.0) ringW = 3.0;
    rp = (side - 2.0 * pad) / 2.0;
    outer = rp + ringW * 0.5;
    inner = rp - ringW * 0.5;
    if (outer < 1.0) outer = 1.0;
    if (inner < 0.0) inner = 0.0;
    if (bandOuter) *bandOuter = hsfDialRoundI(outer);
    if (bandInner) *bandInner = hsfDialRoundI(inner);
}

/** @brief 渲染表盘静态部分（底盘整圆 + 270° 底轨带）进离屏缓存。
 *  @details 只与「尺寸+主题」有关；背景全透明（圆外透出页面底色）。
 *           抄 vfdGaugeRenderStatic（Source 合成整幅清零的口径同款）。 */
static void hsfHomeDialRenderStatic(HsfHomeDial* dial, int w, int h)
{
    int cx = w / 2;
    int cy = h / 2;
    int bandOuter = 0;
    int bandInner = 0;
    XPainter painter;
    if (!dial) return;
    hsfHomeDialGeom(w, h, &bandInner, &bandOuter);
    if (!dial->m_staticCache) {
        dial->m_staticCache = XImage_create();
        if (!dial->m_staticCache) return;
    }
    if (dial->m_cacheW != w || dial->m_cacheH != h) {
        XImage_reinit_ex(dial->m_staticCache, w, h, XImageFormat_ARGB32);
        dial->m_cacheW = w;
        dial->m_cacheH = h;
    }
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, dial->m_staticCache)) {
        XPainter_deinit(&painter);
        return;
    }
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_Source);
    XPainter_fillRect(&painter,
                      (const XRect*)&(XRect){0, 0, w, h}, 0x00000000u);
    XPainter_setCompositionMode(&painter, XPainterCompositionMode_SourceOver);
    XPainter_setRenderHint(&painter, XPainterRenderHint_Antialiasing, true);
    /* 底盘（主题白/深盘，与变频器仪表同源；Python RemainDial 同款补画
     * ——2026-11-13 两端对齐，缺口与环内透盘面色而非卡片底色）+
     * 底轨带（软描边；半径=带外缘，与 Python 底盘=环外接圆同口径）。 */
    hsfDialFillCircle(&painter, cx, cy, bandOuter,
                      homeThemeClr(0xFFF7F9FBu, HSF_COLOR_PANEL_DARK));
    hsfDialFillArcBand(&painter, cx, cy, bandInner, bandOuter,
                       (double)HOME_DIAL_ARC_START_DEG,
                       -(double)HOME_DIAL_ARC_SPAN_DEG,
                       homeThemeClr(0xFFD5DBE2u, HSF_COLOR_SOFT_BORDER_DARK));
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    dial->m_cacheDark = HsfTheme_isDarkSkin();
}

static void VHsfHomeDial_paintEvent(XWidget* self, XEvent* event)
{
    HsfHomeDial* dial = (HsfHomeDial*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    int w = XWidget_width(self);
    int h = XWidget_height(self);
    int cx, cy, bandOuter, bandInner;
    uint32_t arcClr = 0;
    uint32_t textClr;
    const char* caption = "";
    char bigText[24];
    int64_t remain = -1;

    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    if (w <= 0 || h <= 0) return;
    image = XWidget_paintImage(self);
    if (!image) return;

    /* [槽恒占位 2026-11-13] 无阶段（elapsed<0）= 空态：不画环不画字
     * （表盘不再 show/hide——那会让控制行高 88↔34 回流；槽位恒占，
     * 空态视觉=隐藏，「自动模式以外不显示表盘」口径不变）。空态连
     * 静态层都不建（底盘/底轨对空态毫无意义）。**直接 return**：
     * paintImage 后备由框架在 beginPaint 时预清（搅拌泵仪表从不主动
     * 清底也无残影即证）；此前用 Source 填 0x00000000「清透明」是错
     * 的——paintImage 是 RGB32 无 alpha 通道，填 0 即纯黑（板端实见
     * 黑方块）。 */
    if (dial->m_elapsedMs < 0) {
        return;
    }

    /* 静态层按「尺寸+主题」惰性重建（两者都不变才复用）。 */
    if (!dial->m_staticCache || dial->m_cacheW != w || dial->m_cacheH != h ||
        dial->m_cacheDark != HsfTheme_isDarkSkin())
        hsfHomeDialRenderStatic(dial, w, h);

    /* 状态 → 进度带色/中央文案（Python set_state 同表）。[2026-11-13]
     * elapsed<0 的空态已在上方画空并返回，此处只剩「有阶段」两态。 */
    bigText[0] = '\0';
    if (dial->m_totalMs > 0) {
        remain = dial->m_totalMs - dial->m_elapsedMs;
        if (remain < 0) remain = 0;
        caption = "剩余";
        if (remain > 60000)       arcClr = HOME_CLR_INFO;
        else if (remain > 0)      arcClr = HOME_CLR_WARN;
        else                      arcClr = HOME_CLR_FAULT;
        hsfHomeDialFmtMs(remain, bigText, (int)sizeof(bigText));
    } else {
        caption = "已运行";
        hsfHomeDialFmtMs(dial->m_elapsedMs, bigText, (int)sizeof(bigText));
    }

    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    XPainter_setRenderHint(&painter, XPainterRenderHint_Antialiasing, true);
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);

    /* 静态层 blit（圆外透明，页面底色透出）。 */
    if (dial->m_staticCache)
        XPainter_drawImage(&painter, dial->m_staticCache, 0, 0);

    /* 进度带（剩余/总）。[同带宽同轨 2026-11-13·对齐 Python] Python 底轨
     * 与进度弧共用同一 ring_w 与同一 rect（同宽同轨）；原「略厚外扩 1px
     * 盖底轨」是抄变频器页的画法，与 Python 分叉，已收口。 */
    if (dial->m_totalMs > 0 && remain > 0) {
        cx = w / 2;
        cy = h / 2;
        hsfHomeDialGeom(w, h, &bandInner, &bandOuter);
        hsfDialFillArcBand(&painter, cx, cy, bandInner, bandOuter,
                           (double)HOME_DIAL_ARC_START_DEG,
                           -(double)HOME_DIAL_ARC_SPAN_DEG * ((double)remain /
                                                              (double)dial->m_totalMs),
                           arcClr);
    }

    /* 中央两行：小字 caption + 粗体读数（painter 直画；颜色随主题）。
     * [字号与锚点 2026-11-13·对齐 Python] side=min(w,h)、caption=
     * max(9, side*12.5%)、读数=max(10, side*24%) 加粗；两行画在固定波段
     * 矩形内居中（Python QRectF+AlignCenter 同制）——原「整控件高 h 的
     * 12%/22% + 手算基线」锚点体系与 Python 分叉（且基线 x 用 fSmall/fBig
     * 实测宽、绘制却走 painter 当前字体，宽与字不符）。[2026-11-13] 空态
     * 「--」离线灰分支已随空态画空提前返回一并拆除。 */
    {
        XFont fSmall;
        XFont fBig;
        const int side = (w < h) ? w : h;
        int smallPx = side * 125 / 1000;
        int bigPx = side * 24 / 100;
        XRect rc;
        if (smallPx < 9) smallPx = 9;
        if (bigPx < 10) bigPx = 10;
        textClr = homeThemeClr(0xFF1A1A1Au, HSF_COLOR_TEXT_DARK);
        fSmall = XWidget_font(self);
        XFont_setPixelSize(&fSmall, smallPx);
        fBig = XWidget_font(self);
        XFont_setPixelSize(&fBig, bigPx);
        XFont_setBold(&fBig, true);
        if (caption[0]) {
            XRect_init(&rc, 0, side * 235 / 1000, side, side * 20 / 100);
            XPainter_setFont(&painter, &fSmall);
            XPainter_drawTextRect(&painter, &rc, XPAINTER_TEXT_ALIGN_CENTER,
                                  caption, HOME_CLR_TEXT_DIM);
        }
        XRect_init(&rc, 0, side * 40 / 100, side, side * 34 / 100);
        XPainter_setFont(&painter, &fBig);
        XPainter_drawTextRect(&painter, &rc, XPAINTER_TEXT_ALIGN_CENTER,
                              bigText[0] ? bigText : "--", textClr);
        XClassDeinit(&fSmall);
        XClassDeinit(&fBig);
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

XCLASS_DEFINE_BEGING(HsfHomeDialWidget)
XCLASS_DEFINE_EXTEND_END(HsfHomeDialWidget, XWidget)

/** @brief 析构：释放静态层缓存（照 HsfVfdGaugeWidget_deinit；随后框架
 *         Deinit_Parent 链走默认子树回收——该 Overload 专司缓存释放）。
 *  @note  [2026-11-24 退出段 UAF 根因] 此前**漏了**
 *        `XClass_Deinit_Parent(XWidget, obj)` ⇒ VXWidget/VXObject 的基础
 *        析构（从父 m_children 摘除、销毁 destroyed 信号、回收子对象、
 *        deref threadData）全部被跳过 ⇒ 对象被 free 后仍留在父的
 *        m_children 里（内存复用后 vtable 已换成别的类）⇒ HsfClient 退出
 *        恒 AV（cdb：XClass_deinit_base 读 vtable=0xFEEEFEEE）。自定义
 *        Deinit 重载**必须**在末尾链到基类，HsfFmGauge/HsfVfdGauge 都是
 *        这么写的。 */
static void VHsfHomeDialWidget_deinit(XObject* obj)
{
    HsfHomeDial* dial = (HsfHomeDial*)obj;
    if (dial && dial->m_staticCache) {
        XClassDelete((XClass*)dial->m_staticCache);
        dial->m_staticCache = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)obj);
}

XVtable* HsfHomeDial_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfHomeDialWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VHsfHomeDial_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VHsfHomeDialWidget_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief 建倒计时表盘（堆分配；side=直径 px；恒显示占位——无阶段
 *         （elapsed<0）由 paintEvent 画空（2026-11-13 用户口径，布局零
 *         跳动），不再 show/hide）。 */
static HsfHomeDial* hsfHomeDialCreate(XWidget* parent, int side)
{
    HsfHomeDial* dial;
    if (!parent) return NULL;
    dial = (HsfHomeDial*)XMemory_malloc(sizeof(HsfHomeDial),
                                        XCLASS_DEFAULT_MEMORY_TYPE);
    if (!dial) return NULL;
    memset(dial, 0, sizeof(*dial));
    XWidget_init(&dial->m_base, parent, 0);
    XClassGetVtable(dial) = HsfHomeDial_class_init();
    Set_Class_Memory(dial, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(dial, true);
    dial->m_elapsedMs = -1;
    dial->m_totalMs = 0;
    XWidget_setFixedSize(&dial->m_base, side, side);
    XWidget_show(&dial->m_base);
    return dial;
}

/** @brief 落状态（同值早退；elapsed<0 = 不在自动运行）。 */
static void hsfHomeDialSetState(HsfHomeDial* dial, int64_t elapsedMs,
                                int64_t totalMs)
{
    char tip[96];
    if (!dial) return;
    if (dial->m_elapsedMs == elapsedMs && dial->m_totalMs == totalMs) return;
    dial->m_elapsedMs = elapsedMs;
    dial->m_totalMs = totalMs;
    /* [tooltip 2026-11-13·对齐 Python set_state] Python 悬浮提示随状态给
     * 「已运行 X（本阶段无定时）/ 剩余 X（本阶段共 Y）」；空态（elapsed<0）
     * 不给提示（见下）。C 端框架有 XWidget_setToolTip 但此前从不调用。
     * 跟 Python 一样**只在数据变时写**（放进 paintEvent 会每帧改属性）。 */
    if (elapsedMs < 0) {
        /* [空态无提示 2026-11-13] 槽恒占位后空态整件画空（透明），悬浮
         * 提示一并清空——空白区域冒「不在自动运行」反而误导（Python 隐藏
         * 整件同样无提示）。 */
        tip[0] = '\0';
    } else if (totalMs > 0) {
        int64_t remain = totalMs - elapsedMs;
        char rt[16];
        char tt[16];
        if (remain < 0) remain = 0;
        hsfHomeDialFmtMs(remain, rt, (int)sizeof(rt));
        hsfHomeDialFmtMs(totalMs, tt, (int)sizeof(tt));
        snprintf(tip, sizeof(tip), "剩余 %s（本阶段共 %s）", rt, tt);
    } else {
        char et[16];
        hsfHomeDialFmtMs(elapsedMs, et, (int)sizeof(et));
        snprintf(tip, sizeof(tip), "已运行 %s（本阶段无定时）", et);
    }
    {
        XString* s = XString_create_utf8(tip);
        if (s) {
            XWidget_setToolTip(&dial->m_base, s);
            XClassDelete((XClass*)s);
        }
    }
    XWidget_update(&dial->m_base);
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
    /* [卡面随主题 2026-11-11 双主题走查] 白卡原先是常量：深色盘下标题
     * 翻亮落白底 1.21:1。亮套白底/软描边维持原值；深套=面板 #23282F +
     * 软描边 #39414C（HsfTheme.h 深色令牌，与 QSS 控件底/软描边同源），
     * 卡标题走调色板 WindowText（#E8EAED）在深面板上 12.3:1。 */
    XPainter_setPen(&painter, homeThemeClr(0xFFD5DBE2u,
                                           HSF_COLOR_SOFT_BORDER_DARK));
    XPainter_setPenWidth(&painter, 1);
    XPainter_setBrush(&painter, homeThemeClr(0xFFFFFFFFu,
                                             HSF_COLOR_PANEL_DARK));
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
    XBoxLayout   m_cardsRow;   /**< 三卡横排。 */
    HsfStatBlock* m_thTemp;    /**< 电箱温度。 */
    HsfStatBlock* m_thHum;     /**< 电箱湿度。 */
    HsfStatBlock* m_vfdState;  /**< 变频器运行状态。 */
    HsfStatBlock* m_vfdFreq;   /**< 输出频率。 */
    HsfStatBlock* m_vfdSet;    /**< 转速（rpm）。 */
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
    XLabel       m_fmAlarm;    /**< 流量计报警行（中文；2026-11-10）。 */
    HsfHomeCard* m_valveCard;  /**< 电磁阀卡（六路合一）。 */
    XLabel       m_valveState[6]; /**< 六路状态行（●+文字，按状态着色）。 */
    XLabel       m_valveName[6];  /**< 六路阀名（服务端下发，小字灰）。 */
    /* ---- 第三行两卡（浊度仪 / 按钮组系统状态，2026-11-09）---- */
    XBoxLayout   m_cardsRow3;  /**< 第三卡行横排。 */
    HsfStatBlock* m_turbValue[2]; /**< 两台浊度仪读数（NTU）。 */
    XLabel       m_turbTemp[2];  /**< 两台浊度仪水温（℃；无温度的机型空）。 */
    XLabel       m_turbNote;     /**< 浊度说明（未收到数据等，小字）。 */
    XLabel       m_turbPump;     /**< 进水泵状态行（浊度仪卡内；Python 同款）。 */
    HsfHomeCard* m_btnCard;      /**< 按钮组系统状态卡。 */
    XLabel       m_btnState[7];  /**< 4 触点 + 3 灯的状态行（●+文字，按状态着色）。 */
    XLabel       m_btnName[7];   /**< 7 格的名字小字（急停/启动/…）。 */
    /* ---- 自动模式卡（2026-11-11 三模式；2026-11-12 挪到首行）----
     * 序列逻辑全在服务端（阀相→到位→泵相→运行），客户端只发意图 +
     * 按 1s 节流回显真实相位（KEY_AUTO_MODE/PHASE/FAULT），不自行推断
     * 「启动了所以应该在阀相」——推断出来的状态在序列被急停/报警打断
     * 时会撒谎（Python 首页 _refresh_auto_card 同口径）。 */
    HsfHomeCard* m_autoCard;     /**< 自动模式卡（单卡独行，挂 m_layout 首行）。 */
    XComboBox    m_autoCombo;    /**< 模式下拉（产水/反洗/顺洗/自动·流量循环）。 */
    HsfHomeDial* m_autoDial;     /**< 倒计时表盘（阶段剩余；对齐 Python RemainDial；槽恒占位，空态画空）。 */
    XPushButton  m_autoStart;    /**< 启动钮（带所选模式发 0x0115 op=1）。 */
    XPushButton  m_autoStop;     /**< 停止钮（op=2）。 */
    XPushButton  m_autoReset;    /**< 复位钮（op=4；清报警/急停锁存，常态灰）。 */
    XLabel       m_autoHint;     /**< 状态行（服务端三键回显，分态文案/着色）。 */
    XLabel       m_autoRunTitle; /**< 「自动运行」标题标签（与时长标签分开）。 */
    XLabel       m_autoRunText;  /**< 累计运行时长（定宽，防每秒跳动重排）。 */
    int          m_autoRunWidth; /**< 时长标签定宽（像素；按当前字体实测）。 */
    /* [阶段剩余旁文已删 2026-11-13·对齐 Python] 用户 2026-11-12 在 Python
     * 端定案「表盘中间已经有倒计时了，旁边就不用其他倒计时了」并删除页面
     * 级旁文标签（pages_other.py [旁文已删] 注）；C 端 m_autoStageText 整链
     * （字段/格式化/定宽/刷新分支/创建/布局行）随之拆除，只留表盘。 */
    uint64_t     m_autoPollMs;   /**< 最近一次状态轮询时刻（1s 节流，自记）。 */
    int          m_autoOpKind;   /**< 在途自动操作（0=无 1=启动 2=停止 3=复位
                                   * 4=只选模式 op=5）。 */
    char         m_autoOpName[32]; /**< 在途启动的模式显示名（发送时定格，
                                      *   应答期间用户改下拉不串文案）。 */
    int          m_seenCmdSeq;   /**< 已提示的控制应答序号（0=从未收到）。 */
    bool         m_autoBold;     /**< 状态行加粗态（只在变化时 setFont）。 */
    bool         m_autoCycleWasRunning; /**< 循环回显的沿状态（上一拍是否
                                         *   在循环运行；上升沿把下拉切到
                                         *   「自动」，Python 同款）。 */
    void*        m_statusUser;   /**< 状态栏宿主（HsfPages_setStatus 的 user）。 */
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

#define HOME_TXT_CARD_TH    "电箱温湿度"  /* 电箱温湿度 */
#define HOME_TXT_TH_TEMP    "电箱温度"  /* 电箱温度 */
#define HOME_TXT_TH_HUM     "电箱湿度"  /* 电箱湿度 */
/* [2026-11-05 现场口径] 这路变频器驱动的是搅拌泵，卡片名按工艺用途叫。
 * 设备**类别**仍是 Vfd（kind=0），"类型"列照旧显示"变频器" —— 名字
 * 给操作员找设备、类别给维修判断该查什么，两者不混。 */
#define HOME_TXT_CARD_VFD   "搅拌泵"              /* 搅拌泵 */
#define HOME_TXT_VFD_STATE  "运行状态"  /* 运行状态 */
#define HOME_TXT_VFD_OUT    "输出频率"  /* 输出频率 */
/* [第三项=转速 2026-11-17 用户实报] 原"设定频率" ⇒ 主页一览卡显示**实际
 * 转速**（与 Python 客户端主页、网页端主页同口径）；"设定频率"仍在泵详情
 * 悬浮卡里。
 * [转速=按输出频率换算] 现场铭牌 50Hz≈1440rpm ⇒ 28.8 rpm/Hz（用户口径
 * 「就跟着输出频率换算转速」）。 */
#define HOME_TXT_VFD_SET    "转速"      /* 转速（rpm） */
/* 转速换算系数（rpm/Hz）：现场铭牌 50Hz≈1440rpm ⇒ 28.8（三端同口径）。 */
#define HOME_RPM_PER_HZ      (1440.0 / 50.0)
#define HOME_TXT_CARD_HEALTH "设备健康" /* 设备健康 */
#define HOME_TXT_DEV_TOTAL  "设备总数"  /* 设备总数 */
#define HOME_TXT_DEV_FAULT  "故障"                          /* 故障 */
#define HOME_TXT_DEV_OFF    "离线"                          /* 离线 */
#define HOME_TXT_UNIT_TAI   "台"                                      /* 台 */
/* [快捷入口文案已删] 2026-11-05 用户定案首页不做快捷入口按钮 ⇒ 原
 * HOME_TXT_ENTRY（"快捷入口"）随之作废，不留死常量。 */
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
/* [流量计报警中文 2026-11-10] 报警码直接说中文（说明书口径），不显示
 * "0x0004"。口径照水质页 fmAlarmText（空管/上限/下限/系统）。 */
#define HOME_TXT_FM_ALARM   "报警"              /* 报警 */
#define HOME_TXT_FM_NORMAL  "正常"              /* 正常 */
#define HOME_TXT_FM_EMPTY   "空管"              /* 空管 */
#define HOME_TXT_FM_UPPER   "上限"              /* 上限 */
#define HOME_TXT_FM_LOWER   "下限"              /* 下限 */
#define HOME_TXT_FM_SYSTEM  "系统"              /* 系统 */
#define HOME_TXT_CARD_VALVE "电动球阀 · 6 路"                              /* 电动球阀 · 6 路 */
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
#define HOME_TXT_VALVE_DETAIL "电动球阀详情"                                      /* 电动球阀详情 */
#define HOME_TXT_FB_OPEN    "开到位"              /* 开到位 */
#define HOME_TXT_FB_CLOSE   "关到位"              /* 关到位 */
#define HOME_TXT_YES        "是"                                      /* 是 */
#define HOME_TXT_NO         "否"                                      /* 否 */
/* ---- 第三行两卡：浊度仪 / 按钮组系统状态（2026-11-09 新增）---- */
#define HOME_TXT_CARD_TURB  "浊度仪"                 /* 浊度仪 */
#define HOME_TXT_TURB_UNIT  "NTU"                                  /* NTU */
#define HOME_TXT_CARD_BTN   "按钮组系统状态" /* 按钮组系统状态 */
#define HOME_TXT_BTN_ESTOP  "急停"                                  /* 急停 */
#define HOME_TXT_BTN_START  "启动"                                  /* 启动 */
#define HOME_TXT_BTN_STOP   "停止"                                  /* 停止 */
#define HOME_TXT_BTN_RESET  "复位"                                  /* 复位 */
#define HOME_TXT_BTN_LAMP_START "启动灯"                      /* 启动灯 */
#define HOME_TXT_BTN_LAMP_STOP  "停止灯"                       /* 停止灯 */
#define HOME_TXT_BTN_LAMP_RESET "复位灯"                      /* 复位灯 */
#define HOME_TXT_BTN_NORMAL "正常"                                  /* 急停回路正常 */
#define HOME_TXT_BTN_TRIGGER "急停"              /* 急停已触发（按下或断线） */
#define HOME_TXT_BTN_PRESSED "按下"               /* 按键按下 */
#define HOME_TXT_BTN_RELEASED "未按"              /* 按键未按 */
#define HOME_TXT_BTN_LAMP_ON "亮"                                  /* 灯亮 */
#define HOME_TXT_BTN_LAMP_OFF "灭"                                 /* 灯灭 */
#define HOME_VALVE_N         6   /**< 电磁阀六路（现场口径，与 HsfServerIoValve 同值）。 */
#define HOME_DP_STALE_MS     5000u /* 压差卡断流判据（AI 无时间型 fresh，自记）。 */
#define HOME_TURBIDITY_N     2   /**< 浊度仪台数（现场两台：站号 1 / 站号 2，2026-11-09 板端普查定案）。 */
#define HOME_TURBIDITY_WARN_NTU 20.0 /**< 浊度标红阈值（NTU；仅显示口径，非报警判据）。 */
#define HOME_BTN_SLOT_N      7   /**< 按钮组卡格子数：4 触点 + 3 灯。 */
#define HOME_BTN_COLS        4   /**< 按钮组卡每行格数（4 触点一行、3 灯一行）。 */
/* ---- 自动模式卡（2026-11-11 三模式；2026-11-12 挪到首行；Python 首页同款）---- */
#define HOME_TXT_CARD_AUTO   "自动模式"                          /* 卡标题 */
/* 组合模式「自动」（HSF_AUTO_MODE_ID_AUTO）：已实现（产水→反洗→顺洗循环
 * + 阶段记忆）；启动按循环配置校验，未启用/子模式缺设备会明确拒并回原因
 * ——入口先摆出来（Python 首页同款口径）。[2026-11-13 注释订正] 组合
 * 循环 2026-11-12 落地，旧「暂未实现」注释已过期。 */
#define HOME_TXT_MODE_AUTO   "自动（流量循环）"                  /* 下拉第 4 项 */
#define HOME_TXT_MODE_UNSET  "未选择"                            /* 服务端回空串 */
#define HOME_TXT_AUTO_CONN   "连接后显示服务端状态"              /* 未收到模式应答 */
#define HOME_TXT_AUTO_MANUAL "手动模式（切到自动后可按启动）"    /* 非自动模式 */
/* [六态文案逐字对齐 Python 首页 _refresh_auto_card 2026-11-12]
 * 「按「复位」」的直角引号也照抄——两端同一句操作指引，操作员不必
 * 在两种措辞之间换脑子。 */
#define HOME_TXT_AUTO_FAULT  "报警锁存：按「复位」清除后再启动"  /* autoFault=true */
#define HOME_TXT_AUTO_ESTOP  "急停锁存：按「复位」后再启动"      /* phase=estop */
#define HOME_TXT_AUTO_IDLE   "已停止"                            /* 停止态（无选中） */
#define HOME_TXT_AUTO_READY  "已停止 · 待启动（选中：%s）"       /* 停止态（有选中） */
/** [循环中断的“从哪继续” 2026-11-12·对齐 Python] 服务端停止后 autoMode
 *  仍是记住的子阶段（复位才清空）⇒ 如实显示，别让人以为从头再来。 */
#define HOME_TXT_AUTO_RESUME "已停止 · 待启动（将从%s继续；按复位则从头）"
#define HOME_TXT_AUTO_RUN    "运行中 · %s%s"                     /* 运行态（模式[ · 相位]） */
/* 相位界面文案（服务端 HSF_KEY_AUTO_PHASE 值 → 状态行；与 Python
 * AUTO_PHASE_TEXT 同值同义）。 */
/* [相位短语 2026-11-12·逐字对齐 Python AUTO_PHASE_TEXT（pages_other.py:668-674）]
 * 短语化：长句解释在按钮 tooltip 里讲过了，状态行只报相位本身。 */
#define HOME_TXT_PHASE_VALVES  "阀动作中…"
#define HOME_TXT_PHASE_PUMPS   "启动泵…"
#define HOME_TXT_PHASE_RUNNING "运行中"
#define HOME_AUTO_POLL_MS    1000u /**< 服务端状态轮询节流（毫秒；Python 同值）。 */
#define HOME_AUTO_CTRL_H     34    /**< 下拉/按钮定高基准（applyFonts 按 pct 重放）。 */
/* ---- 累计自动运行时长（2026-11-12 用户定案「显示成天 时 分 秒」）----
 * [标题与时长**分成两个标签**] 合成一串再右对齐时，宽度不足会从**左边**
 * 溢出，「自动」两字被裁（Python 同款实锅，见 pages_other.py 构造处
 * 注释）⇒ 标题恒在，只有时长段是定宽的。 */
/* [文案 2026-11-12 用户口径] 「自动运行」→「累计」（Python 首页同批改，
 * pages_other.py:261-264）：这串是天/时/分/秒的**累计**时长（含多次
 * 运行），不是本次运行；「自动运行」四字反而像"本次跑了多久"。 */
#define HOME_TXT_AUTO_RUN_TITLE "累计"                           /* 时长标签的标题 */
/* [定宽按此串实测] 最长可能串（天数封顶 999+ ⇒ 与 3 位天同宽）；宽度按
 * 当前字体实测 + 余量，不写死像素。 */
#define HOME_AUTO_RUN_MAX_TEXT "999+天 23时 59分 59秒"
/* ---- 进水泵状态（浊度仪卡内；2026-11-12 与 Python 首页同款）---- */
#define HOME_TXT_PUMP_INLET  "进水泵"                            /* 行标题 */
#define HOME_TXT_PUMP_RUN    "运行"                              /* 输出已开 */
#define HOME_TXT_PUMP_STOP   "停止"                              /* 输出已关 */

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
    case 1:  *text = HOME_TXT_VALVE_CLOSE;
             *color = homeThemeClr(0xFF37474Fu,
                                   HSF_COLOR_TEXT_SECONDARY_DARK);
             break; /* 已关 灰蓝（深套换次级令牌：#37474F 落深面板 1.54:1
                       不可读，2026-11-11 走查；#A9B2BC vs 深面板 6.91）。 */
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
 * @brief 流量计报警位图 → 中文文案（对齐水质页 fmAlarmText）。
 * @param bits 报警位（0=上限 1=下限 2=空管 3=系统）。
 * @param buf  目标缓冲。
 * @param cap  缓冲容量。
 * @note [为什么要有这层] 报警码直接说中文（说明书口径：0x1022 上限 /
 *       0x1023 下限 / 0x1024 空管 / 0x1025 系统），不显示 "0x0004"——
 *       操作员看不懂十六进制。顺序与 C 端 fmAlarmText 一致：
 *       空管 → 上限 → 下限 → 系统。
 */
static void hsfHomeFmAlarmText(int bits, char* buf, size_t cap)
{
    size_t n = 0;
    if (!buf || cap == 0) return;
    buf[0] = '\0';
    if (bits == 0) {
        snprintf(buf, cap, "%s", HOME_TXT_FM_NORMAL);
        return;
    }
#define HOME_FM_APPEND(txt) \
    do { if (n) { buf[n++] = ' '; buf[n] = '\0'; } \
         n += (size_t)snprintf(buf + n, (n < cap) ? cap - n : 0, "%s", (txt)); \
         if (n >= cap) n = cap - 1; } while (0)
    if (bits & (1 << 2)) HOME_FM_APPEND(HOME_TXT_FM_EMPTY);   /* 空管 */
    if (bits & (1 << 0)) HOME_FM_APPEND(HOME_TXT_FM_UPPER);   /* 上限 */
    if (bits & (1 << 1)) HOME_FM_APPEND(HOME_TXT_FM_LOWER);   /* 下限 */
    if (bits & (1 << 3)) HOME_FM_APPEND(HOME_TXT_FM_SYSTEM);  /* 系统 */
    /* 说明书外的未知位（bit4 及以上）：留十六进制尾巴，避免把新报警类型
     * 说没了（Python 端 flow_alarm_text 同一口径）。 */
    if (bits & ~0x0F) {
        char hex[16];
        snprintf(hex, sizeof(hex), "0x%04X", (unsigned)(bits & ~0x0F));
        HOME_FM_APPEND(hex);
    }
#undef HOME_FM_APPEND
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
 * @brief 取当前新鲜的流量计快照（优先服务端报告的活动侧）。
 * @return 命中侧的快照（借用）；两侧都不新鲜/无效返回 NULL。
 * @note  [活动侧优先 2026-11-10] 流量计挂在哪条 485 上是**服务端的装配
 *       事实**，客户端不写死「B 优先」；先看活动侧（服务端告知），再兜底
 *       另一侧（历史遗留的按侧缓存仍在，防服务端换侧瞬间断档）。
 */
static const HsfLinkFlowMeter* hsfHomePickFlowMeter(void)
{
    int i;
    const int active = HsfLinkClient_flowMeterSide();
    for (i = 0; i < HSF_LINK_SIDE_COUNT; ++i) {
        const int side = (i == 0) ? active
                                  : ((active == HSF_SIDE_A) ? HSF_SIDE_B
                                                           : HSF_SIDE_A);
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
    /* [前景角色钉死 WindowText] 标签缺省按背景角色**推断**前景色
     * （XLabel label_foregroundRole）：推断出来恰是 WindowText 时同色，
     * 但推断路径依赖背景角色 ⇒ 主题换了背景角色就可能取到别的角色，
     * 上面钉的 WindowText 就不生效。显式钉死（同 HsfTheme_applySecondaryText
     * 的做法）：setForegroundRole 自带同值早退，稳态零开销。 */
    XWidget_setForegroundRole((XWidget*)label, XPaletteColorRole_WindowText);
}

static void hsfHomeHealthClicked(void* userData);static void hsfHomePumpClicked(void* userData);

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
    /* [手机档贴满内容区 2026-11-09] 页根 <520（手机竖屏档）时不再按定宽
     * 居中：设备状态 980 / 阀浮层 760 / 泵浮层 640 的定宽会被下方 pw-24
     * 钳成细条、浮层内容挤压（勘察缺口）。改为直接贴满内容区——几何=
     * 页根矩形内缩 8px，居中逻辑与定宽下限全部跳过（表/读数交给滚动区
     * 与自身布局收放）。幂等：几何由当前页根尺寸唯一决定，同尺寸重入
     * 结果不变（与宽档同一口径的纯重算，无累积状态）。宽档（>=520）
     * 不进此分支，原钳口居中行为零变化。 */
    if (pw < HOME_FLOAT_FILL_MAX_W) {
        int fw = pw - 16 > 0 ? pw - 16 : 0;   /* 内缩 8px × 2 */
        int fh = ph - 16 > 0 ? ph - 16 : 0;
        XWidget_setFixedSize((XWidget*)self->m_detailFloat, fw, fh);
        XWidget_setGeometry((XWidget*)self->m_detailFloat, 8, 8, fw, fh);
        /* [定尺之后才许算列宽] 与宽档同口径（见 hsfHomeFitTableColumns
         * 说明）；窄档表格挤不下时该函数自留 60px 托底改横滚，不裁列。 */
        hsfHomeFitTableColumns(self);
        return;
    }
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
        XSize hint = XWidget_sizeHint_base((XWidget*)&self->m_detailFloat->m_base);
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
 * @brief 在数字量通道名里按前缀找通道号；找不到返回 -1。
 * @details [为什么按名字找而不写死通道号] 现场接线按图纸走，服务端点位
 *          可配（Python 端 `buttons.items`）；客户端写死 12~15 / 7~9 就是
 *          "接线一改、界面全错"。两个服务端下发的语义名逐字一致
 *          （C 端 HsfServerApp.c 的按钮组登记链 / Python 端 app.py），
 *          按名字匹配则两端同口径。
 */
static int hsfHomeFindChannel(const HsfLinkDigital* dg, const char* prefix,
                              bool isDo)
{
    int i;
    size_t n;
    if (!dg || !prefix || !prefix[0]) return -1;
    n = strlen(prefix);
    for (i = 0; i < HSF_DIGITAL_CHANNELS; ++i) {
        const char* nm = HsfLinkClient_digitalName(i, isDo);
        if (nm && nm[0] && strncmp(nm, prefix, n) == 0) return i;
    }
    return -1;
}

/* ==================== 自动模式卡（2026-11-11 三模式；操作员日常入口） ==================== */

/** @brief 状态栏提示（契约同 HsfSettingsRemote 的 srStatus；宿主 NULL 忽略）。 */
static void hsfHomeStatus(HsfHomePage* self, const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    if (!self || !self->m_statusUser) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    HsfPages_setStatus(self->m_statusUser, buf);
}

/** @brief 模式 id → 显示名（HSF_AUTO_MODE_NAME_* 同源；未知回原 id；空=未选择）。 */
static const char* hsfHomeAutoModeName(const char* id)
{
    if (!id || !id[0]) return HOME_TXT_MODE_UNSET;
    if (strcmp(id, HSF_AUTO_MODE_ID_PRODUCE) == 0)
        return HSF_AUTO_MODE_NAME_PRODUCE;
    if (strcmp(id, HSF_AUTO_MODE_ID_BACKWASH) == 0)
        return HSF_AUTO_MODE_NAME_BACKWASH;
    if (strcmp(id, HSF_AUTO_MODE_ID_RINSE) == 0)
        return HSF_AUTO_MODE_NAME_RINSE;
    if (strcmp(id, HSF_AUTO_MODE_ID_AUTO) == 0)
        return HOME_TXT_MODE_AUTO;
    return id;
}

/** @brief 相位 → 界面文案（Python AUTO_PHASE_TEXT 同值；未知相位回空串）。 */
static const char* hsfHomeAutoPhaseText(const char* phase)
{
    if (!phase || !phase[0]) return "";
    if (strcmp(phase, "valves") == 0)  return HOME_TXT_PHASE_VALVES;
    if (strcmp(phase, "pumps") == 0)   return HOME_TXT_PHASE_PUMPS;
    if (strcmp(phase, "running") == 0) return HOME_TXT_PHASE_RUNNING;
    return "";
}

/** @brief 模式下拉当前选中的模式 id（UTF-8 借用；NULL/空=未选中）。 */
static const char* hsfHomeAutoSelectedMode(HsfHomePage* self)
{
    const XString* s;
    if (!self) return NULL;
    s = XComboBox_currentData(&self->m_autoCombo);
    return s ? XString_toUtf8(s) : NULL;
}

/**
 * @brief 累计运行时长格式化：毫秒 → 「N天 HH时 MM分 SS秒」。
 * @param ms  累计毫秒（服务端计；客户端只格式化）。
 * @param buf 目标缓冲。
 * @param cap 缓冲容量。
 * @details [为什么自己格式化] 这是**时长**不是时刻，没有日期时间类型；
 *          且要能显示超过 24h（天位数不封顶）。
 *          [天数封顶 999+] 时长标签是**定宽**的（每秒变一次，宽度必须
 *          恒定否则整行重排抖动）⇒ 天数不能无限长，否则宽度算不准、
 *          文字被裁。超过 999 天（2.7 年）显示「999+天」——现场不会有
 *          更长的机龄，且宽度与 3 位天一致（Python 同款口径）。
 */
static void hsfHomeFmtAutoRun(uint64_t ms, char* buf, size_t cap)
{
    uint64_t secs;
    uint64_t days;
    uint64_t hours;
    uint64_t minutes;
    uint64_t seconds;
    if (!buf || cap == 0) return;
    buf[0] = '\0';
    secs = ms / 1000u;
    days = secs / 86400u;
    secs = secs % 86400u;
    hours = secs / 3600u;
    secs = secs % 3600u;
    minutes = secs / 60u;
    seconds = secs % 60u;
    if (days > 999u)
        snprintf(buf, cap, "999+天 %02llu时 %02llu分 %02llu秒",
                 (unsigned long long)hours, (unsigned long long)minutes,
                 (unsigned long long)seconds);
    else
        snprintf(buf, cap, "%llu天 %02llu时 %02llu分 %02llu秒",
                 (unsigned long long)days, (unsigned long long)hours,
                 (unsigned long long)minutes, (unsigned long long)seconds);
}

/**
 * @brief 时长标签定宽：按**当前字体**实测最长串宽度（不写死像素）。
 * @details [为什么必须实测] 标签字号随本地设置（按页字号 50~200%）缩放，
 *          写死像素会在放大后裁字（Python 同款实锅：拿窗口字体测 ⇒ 窄
 *          17px ⇒「自动」两字消失）。这里用标签**自己的**字体实测
 *          XPainter_textWidth + 8px 余量，宽度变化时只写一次（不抖）。
 */
static void hsfHomeAutoRunFitWidth(HsfHomePage* self)
{
    XFont f;
    int w;
    if (!self) return;
    f = XWidget_font((XWidget*)&self->m_autoRunText);
    w = XPainter_textWidth(&f, HOME_AUTO_RUN_MAX_TEXT) + 8;
    XClassDeinit(&f);
    if (w > 0 && w != self->m_autoRunWidth) {
        self->m_autoRunWidth = w;
        /* [必须用固定宽] 最小宽约束在布局里会被 sizeHint 压回去（标签
         * 初始文本「--」的 sizeHint 很小）⇒ 布局压不动它，被压缩的只会
         * 是左边的状态文字。 */
        XWidget_setFixedWidth((XWidget*)&self->m_autoRunText, w);
    }
}

/** @brief 启动所选模式（序列在服务端跑；这里只发意图并给状态栏过程态）。 */
static void hsfHomeAutoStartSlot(XObject* receiver, XVarList* args)
{
    HsfHomePage* self = (HsfHomePage*)((char*)receiver -
                                       offsetof(HsfHomePage, m_autoStart));
    const char* mode;
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_can(HSF_PERM_AUTO_CONTROL)) {
        hsfHomeStatus(self, "权限不足：自动运行控制需要管理员账户");
        return;
    }
    if (!HsfLinkClient_isReady()) {
        hsfHomeStatus(self, "未连接服务端");
        return;
    }
    mode = hsfHomeAutoSelectedMode(self);
    /* [带模式启动] 服务端校验模式配置（未配置设备/组合模式未实现都回
     * 明确错因），客户端只发意图；终态经 commandSeq 应答提示（见刷新）。
     * 模式显示名发送时定格（Python 闭包捕获同款——应答期间改下拉不串文案）。 */
    snprintf(self->m_autoOpName, sizeof(self->m_autoOpName), "%s",
             hsfHomeAutoModeName(mode));
    if (HsfLinkClient_autoControlMode(HSF_AUTO_OP_START,
                                      (mode && mode[0]) ? mode : NULL)) {
        self->m_autoOpKind = 1;
        hsfHomeStatus(self, "正在启动自动运行（%s）…", self->m_autoOpName);
    } else {
        hsfHomeStatus(self, "启动失败：服务端未连接");
    }
}

/** @brief 停止自动运行并停全部设备（服务端永远受理停止类动作）。 */
static void hsfHomeAutoStopSlot(XObject* receiver, XVarList* args)
{
    HsfHomePage* self = (HsfHomePage*)((char*)receiver -
                                       offsetof(HsfHomePage, m_autoStop));
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_can(HSF_PERM_AUTO_CONTROL)) {
        hsfHomeStatus(self, "权限不足：自动运行控制需要管理员账户");
        return;
    }
    if (!HsfLinkClient_isReady()) {
        hsfHomeStatus(self, "未连接服务端");
        return;
    }
    if (HsfLinkClient_autoControl(HSF_AUTO_OP_STOP)) {
        self->m_autoOpKind = 2;
        hsfHomeStatus(self, "正在停止自动运行…");
    } else {
        hsfHomeStatus(self, "停止失败：服务端未连接");
    }
}

/**
 * @brief 复位：解除**两把锁**（急停锁存 + 模式序列报警锁存）。
 * @details [语义] 回停止态但**不启动**——要再跑须再按「启动」。走既有
 *          0x0115 op=4（与远端设置页同一 API，服务端自动控制器分派）。
 */
static void hsfHomeAutoResetSlot(XObject* receiver, XVarList* args)
{
    HsfHomePage* self = (HsfHomePage*)((char*)receiver -
                                       offsetof(HsfHomePage, m_autoReset));
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_can(HSF_PERM_AUTO_CONTROL)) {
        hsfHomeStatus(self, "权限不足：自动运行控制需要管理员账户");
        return;
    }
    if (!HsfLinkClient_isReady()) {
        hsfHomeStatus(self, "未连接服务端");
        return;
    }
    if (HsfLinkClient_autoControl(HSF_AUTO_OP_RESET)) {
        self->m_autoOpKind = 3;
        hsfHomeStatus(self, "正在复位（清除报警/急停锁存）…");
    } else {
        hsfHomeStatus(self, "复位失败：服务端未连接");
    }
}

/**
 * @brief 模式下拉改选 ⇒ 发 0x0115 op=5 **只选模式**（记粘性，不启动不动设备）。
 * @details [为什么必须有] 现场物理启动按钮按「服务端上次选中的模式」启动，
 *          客户端下拉若不同步 ⇒ 服务端粘性为空/过期 ⇒ 现场按键被拒
 *          「未选择自动模式」（2026-11-13 板端实锅；Python 首页同款
 *          bridge.send_auto_select）。
 * @note  [信号选 activated 而不是 currentIndexChanged] activated 只在
 *        **用户改选**时发（框架里由弹层行激活路径发出）；程序化
 *        setCurrentIndex（如组合循环运行时把下拉切到「自动」）只发
 *        currentIndexChanged ⇒ 无回环。与 Python 的
 *        `auto_mode_combo.activated` 同口径。
 */
static void hsfHomeAutoModeSelectSlot(XObject* receiver, XVarList* args)
{
    HsfHomePage* self = (HsfHomePage*)((char*)receiver -
                                       offsetof(HsfHomePage, m_autoCombo));
    const char* mode;
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_isReady()) {
        hsfHomeStatus(self,
            "未连接服务端：模式选择未同步（物理启动按钮将用服务端上次记住的模式）");
        return;
    }
    mode = hsfHomeAutoSelectedMode(self);
    if (!mode || !mode[0]) return;
    /* 模式显示名发送时定格（Python 闭包捕获同款——应答期间改下拉不串文案）。 */
    snprintf(self->m_autoOpName, sizeof(self->m_autoOpName), "%s",
             hsfHomeAutoModeName(mode));
    /* [不做本地权限闸] 选模式是轻操作，服务端与启动一样走 AutoControl 闸；
     * 权限不足时服务端拒绝，这里如实报失败（Python 首页同口径）。 */
    if (HsfLinkClient_autoControlMode(HSF_AUTO_OP_SELECT, mode)) {
        self->m_autoOpKind = 4;
        hsfHomeStatus(self, "已选模式：%s（现场启动按钮将按此模式启动）",
                      self->m_autoOpName);
    } else {
        hsfHomeStatus(self, "模式选择失败：服务端未连接");
    }
}

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
            /* [转速=按输出频率换算 2026-11-17 用户口径] 现场铭牌
             * 50Hz≈1440rpm ⇒ ×28.8（与 Python 客户端主页、网页端主页同
             * 口径；滑差随负载变化，这是现场可读口径）。 */
            if (st->m_validMask & XVfdField_OutputFreqHz) {
                snprintf(buf, sizeof(buf), "%.0f",
                         st->m_outputFreqHz * HOME_RPM_PER_HZ);
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
        /* [报警行 2026-11-10] 报警码直接说中文（说明书口径：空管/上限/
         * 下限/系统），不显示 "0x0004"；正常时写「正常」绿字。 */
        {
            char abuf[64];
            int bits = (fm && fresh) ? fm->m_alarms : 0;
            hsfHomeFmAlarmText(bits, abuf, sizeof(abuf));
            if (bits) {
                snprintf(buf, sizeof(buf), "%s: %s", HOME_TXT_FM_ALARM, abuf);
                XLabel_setText_2(&self->m_fmAlarm, buf);
                hsfHomeLabelSetColor(&self->m_fmAlarm, HOME_CLR_FAULT);
            } else {
                XLabel_setText_2(&self->m_fmAlarm, abuf);
                hsfHomeLabelSetColor(&self->m_fmAlarm,
                                     (fm && fresh) ? HOME_CLR_OK
                                                   : HOME_CLR_OFFLINE);
            }
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

    /* ---------- 浊度仪（两台；TURBIDITY 主题） ----------
     * [数值口径] 服务端下发的是 NTU×100 的定点整数，显示 ÷100 两位小数。
     * [标红阈值只是显示口径] 阈值在客户端（各端各算），不是报警判据。
     * [仪器名由服务端给] 客户端不猜"原水/出水"。 */
    {
        const HsfLinkTurbiditySet* ts = HsfLinkClient_turbidity();
        bool fresh = HsfLinkClient_turbidityFresh() && ts && ts->m_count > 0;
        int k;
        for (k = 0; k < HOME_TURBIDITY_N; ++k) {
            if (fresh && k < ts->m_count && ts->m_items[k].m_online) {
                char nb[64];
                double ntu = (double)ts->m_items[k].m_ntu100 / 100.0;
                snprintf(nb, sizeof(nb), "%.2f", ntu);
                hsfStatSet(self->m_turbValue[k], nb, HOME_CLR_TEXT, false);
                /* 读数超阈值标红（hsfStatSet 的 color 参数当前被忽略，
                 * 数值块要着色只能走调色板，见 hsfHomeLabelSetColor）。 */
                hsfHomeLabelSetColor(&self->m_turbValue[k]->m_value,
                                     (ntu >= HOME_TURBIDITY_WARN_NTU)
                                         ? HOME_CLR_FAULT : HOME_CLR_TEXT);
                if (ts->m_items[k].m_name[0]) {
                    XLabel_setText_2(&self->m_turbValue[k]->m_caption,
                                     ts->m_items[k].m_name);
                }
                /* [水温 2026-11-10] 只有带温度的机型才有（站3 ZHEYI-5100ZDY
                 * 一帧 0x0000×4 同刻带出；站1 WZT-701A 无）——缺键就是
                 * 没有，不是 0℃，故空着不显示。 */
                if (ts->m_items[k].m_tempValid) {
                    char tb[48];
                    snprintf(tb, sizeof(tb), "%.2f ℃",
                             (double)ts->m_items[k].m_temp100 / 100.0);
                    XLabel_setText_2(&self->m_turbTemp[k], tb);
                } else {
                    XLabel_setText_2(&self->m_turbTemp[k], "");
                }
            } else {
                hsfStatSet(self->m_turbValue[k], HOME_TXT_DASH,
                           HOME_CLR_TEXT, false);
                hsfHomeLabelSetColor(&self->m_turbValue[k]->m_value,
                                     0xFF9E9E9Eu);
                XLabel_setText_2(&self->m_turbTemp[k], "");
            }
        }
        if (!fresh) {
            XLabel_setText_2(&self->m_turbNote, HOME_TXT_NO_DATA);
            hsfHomeLabelSetColor(&self->m_turbNote, HOME_CLR_OFFLINE);
        } else {
            XLabel_setText_2(&self->m_turbNote, "");
            hsfHomeLabelSetColor(&self->m_turbNote, HOME_CLR_TEXT_DIM);
        }
        /* [进水泵状态 2026-11-12] 并进浊度仪卡（Python 首页同款）：进水泵
         * 是产水链上的一环，与浊度同属水质一路。
         * [口径：输出点，不是实测] 现场进水泵**只有输出没有反馈** ⇒
         * m_on 只表示"DO 已开"，不是"泵真的在转"（HsfLinkPump 注释）。
         * [不订/不新鲜 ⇒ 「--」] 没收到快照、服务端未装配、点位未配、
         * 数据无效——一律显示「--」，**不编造**（Python 同口径）。 */
        {
            const HsfLinkPumps* pm = HsfLinkClient_pumps();
            bool pmFresh = HsfLinkClient_pumpsFresh() && pm && pm->m_count > 0;
            const char* ptext = HOME_TXT_DASH;
            uint32_t pclr = HOME_CLR_OFFLINE;
            if (pmFresh && pm->m_items[0].m_valid && pm->m_items[0].m_ready) {
                if (pm->m_items[0].m_on) {
                    ptext = HOME_TXT_PUMP_RUN;
                    pclr = HOME_CLR_OK;
                } else {
                    ptext = HOME_TXT_PUMP_STOP;
                    pclr = HOME_CLR_OFFLINE;
                }
            }
            snprintf(buf, sizeof(buf), "%s：%s", HOME_TXT_PUMP_INLET, ptext);
            XLabel_setText_2(&self->m_turbPump, buf);
            hsfHomeLabelSetColor(&self->m_turbPump, pclr);
        }
    }

    /* ---------- 按钮组（4 触点 + 3 灯；DIGITAL 主题） ----------
     * [通道按名字找] 见 hsfHomeFindChannel 的说明。
     * [触点极性] 急停/停止 = 常闭（1=回路通=未按）、启动/复位 = 常开
     * （1=按下），与服务端 buttons.py 的现场定义一致；急停的"按下"
     * 显示为红色「急停」（回路断了也是这个态）。 */
    {
        const HsfLinkDigital* dg = HsfLinkClient_digital();
        bool fresh = HsfLinkClient_digitalFresh() && dg && dg->m_valid;
        char tile[96];
        int chTouch[4];
        int chLamp[3];
        int k;
        static const char* kTouchPrefix[4] = { "急停", "启动按钮",
                                               "停止按钮", "复位按钮" };
        static const char* kLampPrefix[3] = { "启动按钮灯", "停止按钮灯",
                                             "复位按钮灯" };
        static const char* kSlotName[HOME_BTN_SLOT_N] = {
            HOME_TXT_BTN_ESTOP, HOME_TXT_BTN_START, HOME_TXT_BTN_STOP,
            HOME_TXT_BTN_RESET, HOME_TXT_BTN_LAMP_START,
            HOME_TXT_BTN_LAMP_STOP, HOME_TXT_BTN_LAMP_RESET };
        /* 常闭触点：急停(0) / 停止(2)；常开：启动(1) / 复位(3) */
        static const bool kNormallyClosed[4] = { true, false, true, false };
        for (k = 0; k < 4; ++k)
            chTouch[k] = hsfHomeFindChannel(dg, kTouchPrefix[k], false);
        for (k = 0; k < 3; ++k)
            chLamp[k] = hsfHomeFindChannel(dg, kLampPrefix[k], true);
        for (k = 0; k < HOME_BTN_SLOT_N; ++k) {
            const char* text = HOME_TXT_DASH;
            uint32_t color = 0xFF9E9E9Eu;
            if (fresh) {
                if (k < 4) {
                    if (chTouch[k] >= 0) {
                        bool level = (dg->m_di & (1u << chTouch[k])) != 0;
                        bool pressed = kNormallyClosed[k] ? !level : level;
                        if (k == 0) {
                            if (pressed) {
                                text = HOME_TXT_BTN_TRIGGER;
                                color = HOME_CLR_FAULT;
                            } else {
                                text = HOME_TXT_BTN_NORMAL;
                                color = HOME_CLR_OK;
                            }
                        } else {
                            if (pressed) {
                                text = HOME_TXT_BTN_PRESSED;
                                color = HOME_CLR_OK;
                            } else {
                                text = HOME_TXT_BTN_RELEASED;
                                color = 0xFF9E9E9Eu;
                            }
                        }
                    }
                } else {
                    int li = k - 4;
                    if (chLamp[li] >= 0) {
                        if (dg->m_do & (1u << chLamp[li])) {
                            text = HOME_TXT_BTN_LAMP_ON;
                            color = HOME_CLR_OK;
                        } else {
                            text = HOME_TXT_BTN_LAMP_OFF;
                            color = 0xFF9E9E9Eu;
                        }
                    }
                }
            }
            snprintf(tile, sizeof(tile), "● %s", text);
            XLabel_setText_2(&self->m_btnState[k], tile);
            hsfHomeLabelSetColor(&self->m_btnState[k], color);
            XLabel_setText_2(&self->m_btnName[k], kSlotName[k]);
        }
    }

    /* ---------- 自动模式卡（1s 节流拉服务端状态 + 分态回显） ---------- */
    /* [建卡失败的兜底] 卡没建出来（malloc 失败等极端路径）时下拉/按钮是
     * 零初始化内存，不能拿去 currentData/setEnabled —— 整段跳过。 */
    if (self->m_autoCard) {
        const bool connected = HsfLinkClient_isReady();
        /* [终态闸=权限×连接×服务端闸] 与 Python 首页 can_ctrl 同口径。 */
        const bool canCtrl = connected && HsfLinkClient_can(HSF_PERM_AUTO_CONTROL);
        int autoOn = 0;
        int running = 0;
        const bool modeKnown = HsfLinkClient_runMode(&autoOn);
        const bool runKnown = HsfLinkClient_autoRunning(&running);
        const char* phase = HsfLinkClient_autoPhase();
        const bool fault = HsfLinkClient_autoFault();
        const uint64_t now = (uint64_t)XDateTime_currentMSecsSinceEpoch();
        /* [启动使能] 可控制 ∧ 已知处于自动模式 ∧ 未在跑 ∧ **无报警锁存**
         * ——服务端反正会拒（有报警未复位），置灰让操作员先进复位，而不是
         * 点了才吃一条失败（Python 首页同口径）。 */
        const bool startOn = canCtrl && modeKnown && autoOn &&
                             !fault && running != 1;
        const bool stopOn = canCtrl && runKnown && running == 1;
        /* [急停锁存判据] 相位 = estop（服务端急停后锁存，复位前拒绝启动）。 */
        const bool estopLatched = (phase && phase[0] &&
                                   strcmp(phase, "estop") == 0);
        /* [复位使能] **常态灰**：复位是"解除锁存"的动作，不是日常操作，
         * 亮着会招误触（按了会清掉正在看的故障现场）。锁存判据=报警锁存
         * （autoFault）**或**急停态（phase=estop）。 */
        const bool resetOn = canCtrl && (fault || estopLatched);
        char big[192];
        const char* text;
        uint32_t clr = HOME_CLR_TEXT_DIM;
        bool bold = false;

        /* 启停/复位应答终态（成功/失败原文都给状态栏；在途登记防止把别的页
         * 发的命令应答错认成自己的）。 */
        {
            const int cseq = HsfLinkClient_commandSeq();
            if (cseq != self->m_seenCmdSeq) {
                self->m_seenCmdSeq = cseq;
                if (self->m_autoOpKind == 1) {
                    const char* err = HsfLinkClient_lastCommandError();
                    if (err && err[0])
                        hsfHomeStatus(self, "启动失败：%s", err);
                    else
                        hsfHomeStatus(self,
                                      "自动运行已受理（%s），设备按序列动作中",
                                      self->m_autoOpName);
                    self->m_autoOpKind = 0;
                } else if (self->m_autoOpKind == 2) {
                    const char* err = HsfLinkClient_lastCommandError();
                    if (err && err[0])
                        hsfHomeStatus(self, "停止失败：%s", err);
                    else
                        hsfHomeStatus(self, "自动运行已停止（设备已全停）");
                    self->m_autoOpKind = 0;
                } else if (self->m_autoOpKind == 3) {
                    const char* err = HsfLinkClient_lastCommandError();
                    if (err && err[0])
                        hsfHomeStatus(self, "复位失败：%s", err);
                    else
                        hsfHomeStatus(self,
                                      "已复位：报警与急停锁存已解除，可按启动");
                    self->m_autoOpKind = 0;
                } else if (self->m_autoOpKind == 4) {
                    /* [op=5 只选模式] 成功文案已在发送时给过（轻操作不
                     * 刷屏）；这里只报失败（未连接/权限不足/未知 id）。 */
                    const char* err = HsfLinkClient_lastCommandError();
                    if (err && err[0])
                        hsfHomeStatus(self, "模式选择失败：%s", err);
                    self->m_autoOpKind = 0;
                }
            }
        }

        /* [1s 节流轮询] 服务端运行态不在推送里（RUN_MODE 拉取式），按
         * Python 首页同款 1s 一次；断开不拉（查询无意义还占会话）。 */
        if (connected && now - self->m_autoPollMs >= HOME_AUTO_POLL_MS) {
            self->m_autoPollMs = now;
            HsfLinkClient_queryRunMode();
        }

        /* [状态文本只认服务端回的三键] 不在客户端推断"启动了所以应该
         * 在阀相"——推断出来的状态在序列被急停/报警打断时会撒谎。 */
        if (!modeKnown) {
            text = HOME_TXT_AUTO_CONN;
        } else if (!autoOn) {
            text = HOME_TXT_AUTO_MANUAL;
        } else if (fault) {
            /* [报警锁存 ⇒ 启动灰] 服务端反正会拒（"有报警未复位"），置灰
             * 让操作员先进复位，而不是点了才吃一条失败。 */
            text = HOME_TXT_AUTO_FAULT;
            clr = HOME_CLR_FAULT;
        } else if (runKnown && running == 1) {
            /* [循环运行标明「自动循环」2026-11-12·对齐 Python] 服务端在组合
             * 模式下 autoMode 报**子阶段**；时间（剩余/已运行）只在循环里
             * 有意义，状态行要写清楚，否则会被看成「选中了单模式」。 */
            const char* pt = hsfHomeAutoPhaseText(phase);
            char ph[160];
            char head[96];
            int64_t stElapsed = -1;
            int64_t stTotal = 0;
            const bool cycleStage =
                HsfLinkClient_autoStage(&stElapsed, &stTotal) && stElapsed >= 0;
            ph[0] = '\0';
            if (pt[0]) snprintf(ph, sizeof(ph), " · %s", pt);
            if (cycleStage)
                snprintf(head, sizeof(head), "自动循环（%s）",
                         hsfHomeAutoModeName(HsfLinkClient_autoMode()));
            else
                snprintf(head, sizeof(head), "%s",
                         hsfHomeAutoModeName(HsfLinkClient_autoMode()));
            snprintf(big, sizeof(big), HOME_TXT_AUTO_RUN, head, ph);
            text = big;
            clr = HOME_CLR_WARN;
            bold = true;
        } else if (estopLatched) {
            text = HOME_TXT_AUTO_ESTOP;
            clr = HOME_CLR_FAULT;
        } else {
            /* [停止态给选中提示] Python：有选中显示「已停止 · 待启动
             * （选中：X）」，未知/无选中只写「已停止」。 */
            const char* sel = hsfHomeAutoSelectedMode(self);
            const char* selName = (sel && sel[0])
                                      ? hsfHomeAutoModeName(sel) : "";
            const char* rep = HsfLinkClient_autoMode();
            /* [组合模式：显示“将从哪一步继续”2026-11-12·对齐 Python]
             * 服务端停止后 autoMode 仍是记住的子阶段 ⇒ 如实告知。 */
            if (sel && strcmp(sel, HSF_AUTO_MODE_ID_AUTO) == 0 && rep &&
                (strcmp(rep, HSF_AUTO_MODE_ID_PRODUCE) == 0 ||
                 strcmp(rep, HSF_AUTO_MODE_ID_BACKWASH) == 0 ||
                 strcmp(rep, HSF_AUTO_MODE_ID_RINSE) == 0)) {
                snprintf(big, sizeof(big), HOME_TXT_AUTO_RESUME,
                         hsfHomeAutoModeName(rep));
                text = big;
            } else if (selName[0] && strcmp(selName, sel) != 0) {
                snprintf(big, sizeof(big), HOME_TXT_AUTO_READY, selName);
                text = big;
            } else {
                text = HOME_TXT_AUTO_IDLE;
            }
        }
        XLabel_setText_2(&self->m_autoHint, text);   /* 自带同值早退。 */
        hsfHomeLabelSetColor(&self->m_autoHint, clr); /* setPalette 同值早退。 */
        /* [加粗只在变化时写] setFont 无同值早退，每拍写会造布局失效。 */
        if (bold != self->m_autoBold) {
            XFont f = XWidget_font((XWidget*)&self->m_autoHint);
            XFont_setBold(&f, bold);
            XWidget_setFont((XWidget*)&self->m_autoHint, &f);
            XClassDeinit(&f);
            self->m_autoBold = bold;
        }
        /* [累计自动运行时长 2026-11-12] 服务端计（唯一事实源），客户端只
         * 格式化。缺键=「未知」⇒ 显示「--」；**有数据就恒显示（0 也显示
         * 「0天 00时 00分 00秒」）**——0 是"没跑过"的有效值，藏掉它会让
         * "有数据"看起来像"没数据"（2026-11-12 用户实见「累计是空的」；
         * Python 定案⑪同口径：known ⇒ 永远显示格式化时长）。 */
        {
            uint64_t runMs = 0;
            char rbuf[64];
            if (HsfLinkClient_autoRunMs(&runMs)) {
                hsfHomeFmtAutoRun(runMs, rbuf, sizeof(rbuf));
                XLabel_setText_2(&self->m_autoRunText, rbuf);
            } else {
                XLabel_setText_2(&self->m_autoRunText, HOME_TXT_DASH);
            }
        }
        /* [倒计时表盘落态 2026-11-12·对齐 Python RemainDial] 表盘吃服务端
         * 的 (elapsed, total)：autoStage 未知（未收到两键）或无阶段时
         * elapsed=-1 ⇒ 同值早退。
         * [槽恒占位 2026-11-13 用户口径] 表盘不再 show/hide——显隐会让
         * 控制行高 88↔34 回流（卡高跳、下方卡片整体挪动）。槽位恒
         * dialPx×dialPx：无阶段（elapsed<0：单模式/不在运行/未知）由
         * paintEvent 画空（透明，视觉=隐藏，「自动模式以外不显示表盘」
         * 口径不变），有阶段正常画表盘，布局零跳动。 */
        {
            int64_t elapsed = -1;
            int64_t total = 0;
            const bool stageKnown = HsfLinkClient_autoStage(&elapsed, &total);
            if (self->m_autoDial) {
                const bool active = stageKnown && elapsed >= 0;
                hsfHomeDialSetState(self->m_autoDial,
                                    active ? elapsed : -1,
                                    active ? total : 0);
            }
            /* [循环运行下拉回显 2026-11-13·对齐 Python _refresh_auto_card]
             * cycle_stage（运行中 ∧ 阶段已知 ∧ elapsed>=0，Python 同式）
             * 上升沿把模式下拉切到「自动（流量循环）」——避免「下拉写产水、
             * 状态行写反洗/倒计时」的矛盾。沿状态每拍落存（下降沿复位，
             * 下次循环再回显）；只改选中不改数据，运行中启动钮已禁用，
             * 不会误发命令。 */
            {
                const bool cycleStage = (runKnown && running == 1) &&
                                        stageKnown && elapsed >= 0;
                if (cycleStage && !self->m_autoCycleWasRunning) {
                    const int autoIdx = XComboBox_findData_2(
                        &self->m_autoCombo, HSF_AUTO_MODE_ID_AUTO);
                    if (autoIdx >= 0 &&
                        XComboBox_currentIndex(&self->m_autoCombo) != autoIdx)
                        XComboBox_setCurrentIndex(&self->m_autoCombo, autoIdx);
                }
                self->m_autoCycleWasRunning = cycleStage;
            }
        }
        /* [按钮终态一次算好再下发] 先启用再禁用两步写=两步真翻转。 */
        if (XWidget_isEnabled((XWidget*)&self->m_autoStart) != startOn)
            XWidget_setEnabled((XWidget*)&self->m_autoStart, startOn);
        if (XWidget_isEnabled((XWidget*)&self->m_autoStop) != stopOn)
            XWidget_setEnabled((XWidget*)&self->m_autoStop, stopOn);
        if (XWidget_isEnabled((XWidget*)&self->m_autoReset) != resetOn)
            XWidget_setEnabled((XWidget*)&self->m_autoReset, resetOn);
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
    HsfStatBlock* blocks[12];
    int i;
    if (!self || pct <= 0) return;
    XLabel_setTextPixelSize(&self->m_devTitle, 14 * pct / 100);
    XLabel_setTextPixelSize(&self->m_dpNote, 12 * pct / 100);
    XLabel_setTextPixelSize(&self->m_fmSub, 12 * pct / 100);
    /* [报警行 2026-11-10] 漏登记则本地设置改字号时报警行不跟着缩放。 */
    XLabel_setTextPixelSize(&self->m_fmAlarm, 12 * pct / 100);
    XLabel_setTextPixelSize(&self->m_turbNote, 12 * pct / 100);
    /* [进水泵状态行 2026-11-12] 同上：漏登记则改字号时不跟着缩放。 */
    XLabel_setTextPixelSize(&self->m_turbPump, 12 * pct / 100);
    for (i = 0; i < HOME_TURBIDITY_N; ++i) {
        XLabel_setTextPixelSize(&self->m_turbTemp[i], 12 * pct / 100);
    }
    for (i = 0; i < 6; ++i) {
        XLabel_setTextPixelSize(&self->m_valveState[i], 12 * pct / 100);
        XLabel_setTextPixelSize(&self->m_valveName[i], 12 * pct / 100);
    }
    /* [按钮组 7 格：状态行 + 名字小字] 漏登记则本地设置改字号时新卡
     * 文字不跟着缩放（与阀卡同类坑）。 */
    for (i = 0; i < HOME_BTN_SLOT_N; ++i) {
        XLabel_setTextPixelSize(&self->m_btnState[i], 12 * pct / 100);
        XLabel_setTextPixelSize(&self->m_btnName[i], 12 * pct / 100);
    }
    /* [自动模式卡 2026-11-11] 下拉/按钮字号与定高、状态行小字随字号重放
     * （漏登记则改字号时新卡不缩放——同按钮组卡同类坑）。 */
    if (self->m_autoCard) {
        XFont f;
        f = XWidget_font((XWidget*)&self->m_autoCombo);
        XFont_setPixelSize(&f, 14 * pct / 100);
        XWidget_setFont((XWidget*)&self->m_autoCombo, &f);
        XClassDeinit(&f);
        f = XWidget_font((XWidget*)&self->m_autoStart);
        XFont_setPixelSize(&f, 14 * pct / 100);
        XWidget_setFont((XWidget*)&self->m_autoStart, &f);
        XClassDeinit(&f);
        f = XWidget_font((XWidget*)&self->m_autoStop);
        XFont_setPixelSize(&f, 14 * pct / 100);
        XWidget_setFont((XWidget*)&self->m_autoStop, &f);
        XClassDeinit(&f);
        f = XWidget_font((XWidget*)&self->m_autoReset);
        XFont_setPixelSize(&f, 14 * pct / 100);
        XWidget_setFont((XWidget*)&self->m_autoReset, &f);
        XClassDeinit(&f);
        XWidget_setFixedHeight((XWidget*)&self->m_autoCombo,
                               HOME_AUTO_CTRL_H * pct / 100);
        XWidget_setFixedHeight((XWidget*)&self->m_autoStart,
                               HOME_AUTO_CTRL_H * pct / 100);
        XWidget_setFixedHeight((XWidget*)&self->m_autoStop,
                               HOME_AUTO_CTRL_H * pct / 100);
        XWidget_setFixedHeight((XWidget*)&self->m_autoReset,
                               HOME_AUTO_CTRL_H * pct / 100);
        XLabel_setTextPixelSize(&self->m_autoHint, 12 * pct / 100);
        /* [累计运行时长 2026-11-12] 标题 + 时长两个标签都随字号重放；
         * 时长标签是**定宽**的 ⇒ 字号变了必须按新字体重新实测（写死像素
         * 会在放大后裁字，Python 同款实锅）。[2026-11-13] 阶段剩余旁文
         * 已删（对齐 Python），其字号重放与定宽随之移除。 */
        XLabel_setTextPixelSize(&self->m_autoRunTitle, 12 * pct / 100);
        XLabel_setTextPixelSize(&self->m_autoRunText, 12 * pct / 100);
        hsfHomeAutoRunFitWidth(self);
    }
    blocks[0] = self->m_thTemp;  blocks[1] = self->m_thHum;
    blocks[2] = self->m_vfdState; blocks[3] = self->m_vfdFreq;
    blocks[4] = self->m_vfdSet;  blocks[5] = self->m_devTotal;
    blocks[6] = self->m_devFault; blocks[7] = self->m_devOff;
    blocks[8] = self->m_dpValue; blocks[9] = self->m_fmFlow;
    blocks[10] = self->m_turbValue[0]; blocks[11] = self->m_turbValue[1];
    for (i = 0; i < 12; ++i) {
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
    self->m_statusUser = userData;   /* 状态栏宿主=归属主窗（同 HsfSettingsRemote）。 */
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

    /* ---- 首行：自动模式卡（2026-11-11 三模式；2026-11-12 用户定案
     *      「删掉页头那行字，首行放自动模式卡」——操作员最常用的入口放
     *      最上面）----
     * [页头整行已删] 原先页首有一行大标题「主界面」+ 灰色副标题「高速
     * 过滤器监控系统」，2026-11-12 用户定案整行删掉 ⇒ 首行直接是这张卡。
     * [与远端设置页分工] 那边是授权切换/序列配置/急停复位；这张卡是操作
     * 员日常入口：选模式 → 启动 →（出故障时）复位。单卡独行挂 m_layout
     * （不与别卡拼行），必须在页尾弹性项之前（放后面会被 stretch 收编的
     * 剩余高度压底）。 */
    self->m_autoCard = hsfCardCreate(&self->m_base, HOME_TXT_CARD_AUTO);
    if (self->m_autoCard) {
        /* [网格布局 2026-11-13 用户口径「这一行有空间浪费，用网格布局」]
         * 对齐 Python 首页自动模式卡的网格：左列=模式下拉/状态行（上下两
         * 行，弹性吸收余宽）、中列=表盘（跨两行居中）、右列=按钮组/累计
         * （上下两行右对齐）——原单行 HBox 把状态下拉孤零零放左、表盘
         * 悬中间，行下方 60% 空白全浪费。 */
        XGridLayout* grid = (XGridLayout*)XMemory_malloc(
            sizeof(XGridLayout), XCLASS_DEFAULT_MEMORY_TYPE);
        XBoxLayout* opRow = (XBoxLayout*)XMemory_malloc(
            sizeof(XBoxLayout), XCLASS_DEFAULT_MEMORY_TYPE);
        XBoxLayout* runRow = (XBoxLayout*)XMemory_malloc(
            sizeof(XBoxLayout), XCLASS_DEFAULT_MEMORY_TYPE);
        if (grid) {
            XGridLayout_init(grid);
            XLayout_setSpacing((XLayout*)grid, 10);
            /* [列伸缩 2026-11-13] **左右两列都弹性**（1:1 平分余宽）⇒ 表盘
             * 列（固定 88px）夹正中；只给左列弹性会把中列+右列一起推到
             * 最右（表盘叠进按钮区、环被卡缘裁——本机真窗实测复现）。 */
            XGridLayout_setColumnStretch(grid, 0, 1);
            XGridLayout_setColumnStretch(grid, 1, 0);
            XGridLayout_setColumnStretch(grid, 2, 1);
            /* 模式下拉：项顺序=协议稳定顺序（proto.AUTO_MODES 同序），
             * itemData=稳定 id——发送与回显都走 id，不依赖下拉下标。 */
            XComboBox_init(&self->m_autoCombo, &self->m_autoCard->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_autoCombo);
            {
                XFont f = XWidget_font((XWidget*)&self->m_autoCombo);
                XFont_setPixelSize(&f, homePx(14));
                XWidget_setFont((XWidget*)&self->m_autoCombo, &f);
                XClassDeinit(&f);
            }
            XComboBox_addItem_2(&self->m_autoCombo, HSF_AUTO_MODE_NAME_PRODUCE);
            XComboBox_setItemData_2(&self->m_autoCombo, 0,
                                    HSF_AUTO_MODE_ID_PRODUCE);
            XComboBox_addItem_2(&self->m_autoCombo, HSF_AUTO_MODE_NAME_BACKWASH);
            XComboBox_setItemData_2(&self->m_autoCombo, 1,
                                    HSF_AUTO_MODE_ID_BACKWASH);
            XComboBox_addItem_2(&self->m_autoCombo, HSF_AUTO_MODE_NAME_RINSE);
            XComboBox_setItemData_2(&self->m_autoCombo, 2,
                                    HSF_AUTO_MODE_ID_RINSE);
            /* 「自动」组合模式（流量循环）：已实现——产水→（压差/定时兜底）
             * →反洗→顺洗→产水循环；启动按循环配置校验（未启用/子模式缺
             * 设备会明确拒并回原因）。入口不静默藏项（Python 首页同款）。 */
            XComboBox_addItem_2(&self->m_autoCombo, HOME_TXT_MODE_AUTO);
            XComboBox_setItemData_2(&self->m_autoCombo, 3,
                                    HSF_AUTO_MODE_ID_AUTO);
            XComboBox_setCurrentIndex(&self->m_autoCombo, 0);
            /* [下拉同步选择 2026-11-14·对齐 Python 首页] 走 activated
             * 信号（**用户改选**才发；程序化回显只发 currentIndexChanged
             * ⇒ 无回环）：改选即发 0x0115 op=5 记服务端粘性，供现场物理
             * 启动按钮使用。 */
            XObject_connect((XObject*)&self->m_autoCombo,
                              XSignal(XComboBox_activated_signal),
                              (XObject*)&self->m_autoCombo,
                              hsfHomeAutoModeSelectSlot,
                              XConnectionType_Direct);
            XWidget_setFixedHeight((XWidget*)&self->m_autoCombo,
                                   homePx(HOME_AUTO_CTRL_H));
            XWidget_show((XWidget*)&self->m_autoCombo);
            XGridLayout_addWidget(grid, (XWidget*)&self->m_autoCombo, 0, 0,
                                  XLayoutAlignment_Left |
                                      XLayoutAlignment_VCenter);
            /* [倒计时表盘 2026-11-12·对齐 Python RemainDial] 阶段剩余
             * 环形表盘（直径基准 88 与 Python DIAL_PX 同值）。
             * [槽恒占位 2026-11-13 用户口径] 表盘恒显示、不再 show/hide
             * （显隐曾让控制行高 88↔34 回流、卡高跳动）：无阶段时
             * paintEvent 画空（透明，「自动模式以外不显示表盘」观感
             * 不变），槽位恒占、布局零跳动。
             * [尺寸下限 2026-11-13·对齐 Python font_px(minimum=24)] 小比例
             * （全局%×页% 双合乘）下防缩没。 */
            {
                int dialPx = homePx(88);
                if (dialPx < 24) dialPx = 24;
                self->m_autoDial = hsfHomeDialCreate(&self->m_autoCard->m_base,
                                                     dialPx);
            }
            /* [位置 2026-11-13 用户截图口径] 表盘挂网格**中列单格**
             * （0,1）——环在模式下拉与按钮组之间居中，88px 自撑行 0 高；
             * 不用跨行 span：addWidgetSpan 的跨行解算实测定位置错（文字
             * 叠到按钮区、环被裁——2026-11-13 本机真窗复现），规避之，
             * 语义不变（表盘仍占中列视觉中心）。 */
            if (self->m_autoDial) {
                XGridLayout_addWidget(grid, (XWidget*)self->m_autoDial,
                                      0, 1,
                                      XLayoutAlignment_HCenter |
                                          XLayoutAlignment_VCenter);
            }
            if (opRow) {
                XBoxLayout_init(opRow, XBoxLayoutDirection_LeftToRight);
                XLayout_setSpacing((XLayout*)opRow, 8);
            }

            XPushButton_init(&self->m_autoStart, &self->m_autoCard->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_autoStart);
            {
                XFont f = XWidget_font((XWidget*)&self->m_autoStart);
                XFont_setPixelSize(&f, homePx(14));
                XWidget_setFont((XWidget*)&self->m_autoStart, &f);
                XClassDeinit(&f);
            }
            XPushButton_setText_2(&self->m_autoStart, HOME_TXT_BTN_START);
            XWidget_setFixedHeight((XWidget*)&self->m_autoStart,
                                   homePx(HOME_AUTO_CTRL_H));
            XObject_connect((XObject*)&self->m_autoStart,
                              XSignal(XAbstractButton_clicked_signal),
                              (XObject*)&self->m_autoStart,
                              hsfHomeAutoStartSlot, XConnectionType_Direct);
            XWidget_show((XWidget*)&self->m_autoStart);
            if (opRow) XBoxLayout_addWidget(opRow, (XWidget*)&self->m_autoStart);

            XPushButton_init(&self->m_autoStop, &self->m_autoCard->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_autoStop);
            {
                XFont f = XWidget_font((XWidget*)&self->m_autoStop);
                XFont_setPixelSize(&f, homePx(14));
                XWidget_setFont((XWidget*)&self->m_autoStop, &f);
                XClassDeinit(&f);
            }
            XPushButton_setText_2(&self->m_autoStop, HOME_TXT_BTN_STOP);
            XWidget_setFixedHeight((XWidget*)&self->m_autoStop,
                                   homePx(HOME_AUTO_CTRL_H));
            XObject_connect((XObject*)&self->m_autoStop,
                              XSignal(XAbstractButton_clicked_signal),
                              (XObject*)&self->m_autoStop,
                              hsfHomeAutoStopSlot, XConnectionType_Direct);
            XWidget_show((XWidget*)&self->m_autoStop);
            if (opRow) XBoxLayout_addWidget(opRow, (XWidget*)&self->m_autoStop);

            /* [复位钮 2026-11-12] 解除**两把锁**（急停锁存 + 模式序列
             * 报警锁存），回停止态但**不启动**——没有它，操作员在报警
             * 锁死时只能去按现场按钮组的物理复位（远程无解）。
             * 走既有 0x0115 op=4（与远端设置页同一 API）。 */
            XPushButton_init(&self->m_autoReset, &self->m_autoCard->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_autoReset);
            {
                XFont f = XWidget_font((XWidget*)&self->m_autoReset);
                XFont_setPixelSize(&f, homePx(14));
                XWidget_setFont((XWidget*)&self->m_autoReset, &f);
                XClassDeinit(&f);
            }
            XPushButton_setText_2(&self->m_autoReset, HOME_TXT_BTN_RESET);
            XWidget_setFixedHeight((XWidget*)&self->m_autoReset,
                                   homePx(HOME_AUTO_CTRL_H));
            XObject_connect((XObject*)&self->m_autoReset,
                              XSignal(XAbstractButton_clicked_signal),
                              (XObject*)&self->m_autoReset,
                              hsfHomeAutoResetSlot, XConnectionType_Direct);
            XWidget_show((XWidget*)&self->m_autoReset);
            if (opRow) XBoxLayout_addWidget(opRow, (XWidget*)&self->m_autoReset);
            if (grid) {
                XGridLayout_addLayout(grid, (XLayout*)opRow, 0, 2,
                                      XLayoutAlignment_Right |
                                          XLayoutAlignment_VCenter);
            }
        }
        /* 状态行：恒占位（不做按状态显隐——几何不可变铁律），分态文案
         * 与着色在 hsfHomeRefresh。 */
        XLabel_init(&self->m_autoHint, &self->m_autoCard->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_autoHint);
        XLabel_setTextPixelSize(&self->m_autoHint, homePx(12));
        HsfTheme_applySecondaryText((XWidget*)&self->m_autoHint);
        XLabel_setText_2(&self->m_autoHint, HOME_TXT_AUTO_CONN);
        XWidget_show((XWidget*)&self->m_autoHint);

        /* [累计运行时长 2026-11-12] 与状态行**同一行**（状态左、时长右）——
         * 首页余量只剩几 px，再加一行就溢出。标题与时长是两个标签（见
         * HOME_TXT_AUTO_RUN_TITLE 处的坑注）。 */
        XLabel_init(&self->m_autoRunTitle, &self->m_autoCard->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_autoRunTitle);
        XLabel_setTextPixelSize(&self->m_autoRunTitle, homePx(12));
        HsfTheme_applySecondaryText((XWidget*)&self->m_autoRunTitle);
        XLabel_setText_2(&self->m_autoRunTitle, HOME_TXT_AUTO_RUN_TITLE);
        XWidget_show((XWidget*)&self->m_autoRunTitle);

        XLabel_init(&self->m_autoRunText, &self->m_autoCard->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_autoRunText);
        XLabel_setTextPixelSize(&self->m_autoRunText, homePx(12));
        HsfTheme_applySecondaryText((XWidget*)&self->m_autoRunText);
        XLabel_setAlignment(&self->m_autoRunText,
                            XAlignment_Right | XAlignment_VCenter);
        /* [缺键不编造] 老服务端不报 autoRunMs ⇒ 显示「--」，不显示"0 天"
         * （那会把"没数据"说成"没跑过"）。 */
        XLabel_setText_2(&self->m_autoRunText, HOME_TXT_DASH);
        XWidget_show((XWidget*)&self->m_autoRunText);
        /* [定宽防跳动] 时长每秒变一次，不定宽则文字宽度变化会把整行重排
         * （每跳一次抖一下）⇒ 按最长可能串实测当前字体宽度定死。 */
        hsfHomeAutoRunFitWidth(self);

        /* [阶段剩余旁文已删 2026-11-13·对齐 Python] 原先这行右侧还有
         * m_autoStageText（「剩余 mm:ss」旁文）；用户 2026-11-12 在 Python
         * 端定案删除（表盘中央已有倒计时），C 端整链拆除。 */
        /* [网格挂载 2026-11-13] 状态行 → 左下格 (1,0)；累计标题+时长 →
         * 右下格 (1,2)（Python 截图同款：状态左下、累计右下）。 */
        if (grid) {
            XGridLayout_addWidget(grid, (XWidget*)&self->m_autoHint, 1, 0,
                                  XLayoutAlignment_Left |
                                      XLayoutAlignment_VCenter);
            if (runRow) {
                XBoxLayout_init(runRow, XBoxLayoutDirection_LeftToRight);
                XLayout_setSpacing((XLayout*)runRow, 8);
                XBoxLayout_addWidget(runRow,
                                     (XWidget*)&self->m_autoRunTitle);
                XBoxLayout_addWidget(runRow,
                                     (XWidget*)&self->m_autoRunText);
                XGridLayout_addLayout(grid, (XLayout*)runRow, 1, 2,
                                      XLayoutAlignment_Right |
                                          XLayoutAlignment_VCenter);
            } else {
                /* [兜底] runRow 分配失败：时长仍要进卡（别丢累计）。 */
                XGridLayout_addWidget(grid, (XWidget*)&self->m_autoRunText,
                                      1, 2, XLayoutAlignment_Right |
                                                XLayoutAlignment_VCenter);
            }
            /* grid 挂卡体（row/旧 hintRow 已由网格取代）。 */
            XBoxLayout_addLayout(&self->m_autoCard->m_body, (XLayout*)grid);
        } else {
            /* [兜底] grid 分配失败：状态+累计保底进卡（别丢状态）。 */
            XBoxLayout_addWidget(&self->m_autoCard->m_body,
                                 (XWidget*)&self->m_autoHint);
            XBoxLayout_addWidget(&self->m_autoCard->m_body,
                                 (XWidget*)&self->m_autoRunText);
        }
        /* [首行] 这张卡是 m_layout 的**第一个** addWidget（用户定案：
         * 页头删掉后首行放自动模式卡）。 */
        XBoxLayout_addWidgetEx(&self->m_layout, &self->m_autoCard->m_base,
                               0, 0);
    }

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
                                               "rpm", HOME_TXT_DASH, 16);
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
            /* [报警行 2026-11-10] 报警码直接说中文（空管/上限/下限/系统），
             * 不显示 "0x0004"。口径照水质页 fmAlarmText。 */
            XLabel_init(&self->m_fmAlarm, &fmCard->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_fmAlarm);
            XLabel_setTextPixelSize(&self->m_fmAlarm, homePx(12));
            XLabel_setText_2(&self->m_fmAlarm, HOME_TXT_DASH);
            XWidget_show((XWidget*)&self->m_fmAlarm);
            XBoxLayout_addWidget(&fmCard->m_body, (XWidget*)&self->m_fmAlarm);
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
                    HsfTheme_applySecondaryText(
                        (XWidget*)&self->m_valveName[k]);
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

    /* ---- 第三行两卡：浊度仪 / 按钮组系统状态（2026-11-09 新增）----
     * [为什么单开一行] 第二行已三张卡，再塞两张会让每张只剩 ~240px：
     * 浊度读数与按钮组 4+3 格都挤不下（触屏现场更糟）。首页在滚动壳
     * 里，加行只是多滚一屏，不会裁切。
     * [位置纪律] 必须加在页尾弹性项**之前**——放后面会被 stretch 收编的
     * 剩余高度压到页面最底且行高塌陷。 */
    XBoxLayout_init(&self->m_cardsRow3, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&self->m_cardsRow3, 10);
    {
        HsfHomeCard* turbCard = hsfCardCreate(&self->m_base, HOME_TXT_CARD_TURB);
        HsfHomeCard* btnCard = hsfCardCreate(&self->m_base, HOME_TXT_CARD_BTN);
        hsfCardSetGlyph(turbCard, HOME_GLYPH_TURBIDITY);
        hsfCardSetGlyph(btnCard, HOME_GLYPH_BUTTONS);

        /* --- 浊度仪卡：两台读数横排 + 说明小字 --- */
        if (turbCard) {
            XWidget_setMinimumHeight((XWidget*)turbCard, 120);
            XBoxLayout* tRow = (XBoxLayout*)XMemory_malloc(sizeof(XBoxLayout),
                                                          XCLASS_DEFAULT_MEMORY_TYPE);
            if (tRow) {
                int k;
                XBoxLayout_init(tRow, XBoxLayoutDirection_LeftToRight);
                XLayout_setSpacing((XLayout*)tRow, 16);
                for (k = 0; k < HOME_TURBIDITY_N; ++k) {
                    /* [每台一个容器 widget] 布局指针强转当 widget 塞进
                     * 另一个布局是未定义行为——实测两台读数全甩到卡片
                     * 左上角与标题叠成一团（板端截图 2026-11-10）。照本页
                     * 按钮组卡的套路：先建容器 widget，再把竖排布局挂上去，
                     * 最后把**容器**加进行布局。 */
                    XWidget* colHost = XWidget_create(&turbCard->m_base, 0);
                    XBoxLayout* tCol = (XBoxLayout*)XMemory_malloc(
                        sizeof(XBoxLayout), XCLASS_DEFAULT_MEMORY_TYPE);
                    if (!colHost || !tCol) continue;
                    XBoxLayout_init(tCol, XBoxLayoutDirection_LeftToRight);
                    XLayout_setSpacing((XLayout*)tCol, 6);
                    XWidget_setLayout(colHost, (XLayout*)tCol);
                    XWidget_show(colHost);
                    /* [stat 块必须入布局] stat 块是卡片 base 的子控件，不进
                     * 布局就没有几何管理，落在 (0,0) 与标题重叠。 */
                    self->m_turbValue[k] = hsfStatCreate(
                        colHost, HOME_TXT_CARD_TURB,
                        HOME_TXT_TURB_UNIT, HOME_TXT_DASH, 26);
                    if (self->m_turbValue[k]) {
                        XBoxLayout_addWidgetEx(tCol,
                            (XWidget*)&self->m_turbValue[k]->m_base, 1, 0);
                        /* [水温 2026-11-10] 每台一个水温小字（带温度的机型
                         * 才有；无温度的机型留空，不是 0℃）。 */
                        XLabel_init(&self->m_turbTemp[k], colHost, 0);
                        HsfTheme_applyDefaultFont(
                            (XWidget*)&self->m_turbTemp[k]);
                        XLabel_setTextPixelSize(&self->m_turbTemp[k],
                                               homePx(12));
                        XLabel_setText_2(&self->m_turbTemp[k], "");
                        XWidget_show((XWidget*)&self->m_turbTemp[k]);
                        XBoxLayout_addWidgetEx(tCol,
                            (XWidget*)&self->m_turbTemp[k], 0, 0);
                    }
                    XBoxLayout_addWidgetEx(tRow, colHost, 1, 0);
                }
                XBoxLayout_addLayout(&turbCard->m_body, (XLayout*)tRow);
            }
            XLabel_init(&self->m_turbNote, &turbCard->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_turbNote);
            XLabel_setTextPixelSize(&self->m_turbNote, homePx(12));
            XLabel_setText_2(&self->m_turbNote, HOME_TXT_DASH);
            XWidget_show((XWidget*)&self->m_turbNote);
            XBoxLayout_addWidget(&turbCard->m_body, (XWidget*)&self->m_turbNote);
            /* [进水泵状态行 2026-11-12] 恒占位（不做按状态显隐——几何不可变
             * 铁律）：无数据时写「进水泵：--」，不写空串（空串=没这行）。 */
            XLabel_init(&self->m_turbPump, &turbCard->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_turbPump);
            XLabel_setTextPixelSize(&self->m_turbPump, homePx(12));
            XLabel_setText_2(&self->m_turbPump, HOME_TXT_DASH);
            XWidget_show((XWidget*)&self->m_turbPump);
            XBoxLayout_addWidget(&turbCard->m_body, (XWidget*)&self->m_turbPump);
            XBoxLayout_addWidgetEx(&self->m_cardsRow3, &turbCard->m_base, 1, 0);
        }

        /* --- 按钮组卡：4 触点 + 3 灯（2 行 4 列，第 8 格留空）---
         *  格子套路照抄电磁阀卡：每格先建容器 widget 再挂竖排布局。 */
        if (btnCard) {
            XWidget_setMinimumHeight((XWidget*)btnCard, 150);
            XGridLayout* bGrid = (XGridLayout*)XMemory_malloc(
                sizeof(XGridLayout), XCLASS_DEFAULT_MEMORY_TYPE);
            XBoxLayout_addStretch(&btnCard->m_body, 1);
            if (bGrid) {
                int k;
                XGridLayout_init(bGrid);
                XLayout_setContentsMargins((XLayout*)bGrid, 0, 0, 0, 0);
                XLayout_setSpacing((XLayout*)bGrid, 4);
                for (k = 0; k < HOME_BTN_SLOT_N; ++k) {
                    /* [每格容器 widget] 网格 addWidget 只收 widget——布局
                     * 指针强转传入是未定义行为（实测首格甩到标题区、其余
                     * 格子丢失）。每格先建容器再挂竖排布局。 */
                    XWidget* tileHost = XWidget_create(&btnCard->m_base, 0);
                    XBoxLayout* tile = (XBoxLayout*)XMemory_malloc(
                        sizeof(XBoxLayout), XCLASS_DEFAULT_MEMORY_TYPE);
                    if (!tileHost || !tile) continue;
                    XBoxLayout_init(tile, XBoxLayoutDirection_TopToBottom);
                    XLayout_setSpacing((XLayout*)tile, 2);
                    XWidget_setLayout(tileHost, (XLayout*)tile);
                    XWidget_show(tileHost);
                    XLabel_init(&self->m_btnState[k], tileHost, 0);
                    HsfTheme_applyDefaultFont((XWidget*)&self->m_btnState[k]);
                    XLabel_setTextPixelSize(&self->m_btnState[k], homePx(12));
                    XLabel_setAlignment(&self->m_btnState[k], XAlignment_Left);
                    XLabel_setText_2(&self->m_btnState[k], HOME_TXT_DASH);
                    XWidget_show((XWidget*)&self->m_btnState[k]);
                    XBoxLayout_addWidget(tile, (XWidget*)&self->m_btnState[k]);
                    XLabel_init(&self->m_btnName[k], tileHost, 0);
                    HsfTheme_applyDefaultFont((XWidget*)&self->m_btnName[k]);
                    XLabel_setTextPixelSize(&self->m_btnName[k], homePx(12));
                    HsfTheme_applySecondaryText(
                        (XWidget*)&self->m_btnName[k]);
                    XLabel_setText_2(&self->m_btnName[k], HOME_TXT_DASH);
                    XWidget_show((XWidget*)&self->m_btnName[k]);
                    XBoxLayout_addWidget(tile, (XWidget*)&self->m_btnName[k]);
                    /* 行 0 = 4 个触点；行 1 = 3 盏灯（第 4 格留空） */
                    XGridLayout_addWidget(bGrid, tileHost,
                                          k / HOME_BTN_COLS, k % HOME_BTN_COLS, 0);
                }
                XBoxLayout_addLayout(&btnCard->m_body, (XLayout*)bGrid);
            }
            XBoxLayout_addWidgetEx(&self->m_cardsRow3, &btnCard->m_base, 1, 0);
        }
    }
    XBoxLayout_addLayout(&self->m_layout, (XLayout*)&self->m_cardsRow3);

    /* [自动模式卡已挪到首行 2026-11-12] 原先它挂在第四行（页尾弹性项
     * 之前），现按用户定案移到页首——建卡代码见本函数前段。 */

    /* [页尾弹性 2026-11-06] 设备状态表改浮层后页内容变矮，widgetResizable
     * 的滚动壳会把整页拉伸到视口高——无弹性项时多余高度全被卡片行吃掉
     * （卡内大片空白，实测）。尾部 stretch=1 收编全部剩余高度：卡片保持
     * 紧凑原高、多余空间沉到页底留白。 */
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
        XObject_connect((XObject*)&self->m_floatClose,
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
            HOME_TXT_VFD_SET,                                     /* 转速 */
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
            HsfTheme_applySecondaryText((XWidget*)&t->m_fbOpenCap);
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
            HsfTheme_applySecondaryText((XWidget*)&t->m_fbCloseCap);
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
    /* 控制应答序号先对齐再首刷：建页前的历史应答不属于本页，不得弹「已受理」。 */
    self->m_seenCmdSeq = HsfLinkClient_commandSeq();
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
    if (!self) return;

    /* 三卡：宽屏横排 / 窄屏竖排。 */
    XBoxLayout_setDirection(&self->m_cardsRow,
                            compact ? XBoxLayoutDirection_TopToBottom
                                    : XBoxLayoutDirection_LeftToRight);
    /* [第二行同款翻排 2026-11-09] 压差/流量计/电磁阀三卡窄屏也竖排：
     * 此前只翻 m_cardsRow，第二行 3 卡仍横排硬挤一行（勘察缺口）。照第
     * 一行同款写法补齐——同方向翻转、宽档统一复原 LeftToRight；幂等由
     * XBoxLayout_setDirection 同值早退保证（方向未变直接 return，每拍
     * 重入无副作用），重排激活走下方 m_layout activate（子布局 activate
     * 回归父布局统一重解算）。 */
    XBoxLayout_setDirection(&self->m_cardsRow2,
                            compact ? XBoxLayoutDirection_TopToBottom
                                    : XBoxLayoutDirection_LeftToRight);
    /* [第三行同款翻排 2026-11-09] 浊度仪/按钮组两卡窄屏也竖排（同第二行）。 */
    XBoxLayout_setDirection(&self->m_cardsRow3,
                            compact ? XBoxLayoutDirection_TopToBottom
                                    : XBoxLayoutDirection_LeftToRight);
    /* 页边距：窄屏收窄腾内容宽。 */
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               compact ? 12 : 20, compact ? 12 : 16,
                               compact ? 12 : 20, compact ? 8 : 12);
    XLayout_activate((XLayout*)&self->m_layout);
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
