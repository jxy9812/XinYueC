/**
 * @file       HsfTheme.c
 * @brief      客户端 UI 共享主题实现（缺省字体装配 + 旗舰双主题安装）。
 */
#include "HsfTheme.h"
#include "HsfThemeCss.h"

#include "XFusionStyle.h" /* Fusion 缺省样式安装 */
#include "XStyle.h"       /* 全局样式表装载 */
#include "XFont.h"
#include "XCoreApplication.h" /* applicationDirPath（exe 同级字库定位） */
#include "XString.h"
#include "XPalette.h" /* 次级文本令牌（per-widget 盘） */
#include "XColor.h"   /* XColor_create_rgb */
#include "XObject.h"  /* 次级文本重刷子树游走（children/m_is_widget） */
#include "XVector.h"
#if XCHARTS_ON
#include "XChartView.h" /* 图表主题随应用主题（类名识别） */
#include "XChart.h"
#endif
#include <string.h>
#include <stdio.h>

/* 旗舰双主题的调色板/方案/应用级样式表入口（2026-11-11）。相关框架段
 * 任一被裁剪时 applyFlagshipSkin 降级为「返回 false、维持已装主题」——
 * 缺省亮色不受影响（与窗口页规格表被裁剪时的契约桩同口径）。 */
#if defined(XGUI_ON) && XGUI_ON && XPALETTE_ON && XSTYLEHINTS_ON && \
    XAPPLICATION_ON && XGUIAPPLICATION_ON && XSTYLE_ON
#define HSF_THEME_FLAGSHIP_ON 1
#include "XGuiApplication.h" /* setPalette（显式盘，冻结方案联动）。 */
#include "XApplication.h"    /* setStyleSheet_2（安装+全顶层重绘）。 */
#include "XStyleHints.h"     /* 深浅方案联动（XGuiApplication_styleHints）。 */
#include "XPalette.h"        /* 调色板值类型与角色枚举。 */
#include "XColor.h"          /* XColor_create_rgb。 */
#else
#define HSF_THEME_FLAGSHIP_ON 0
#endif

/** @brief 套缺省字体：exeDir/../Library/XFont 直连轮廓字库，失败回落内置。
 *
 *  @details 安卓走**家族名**而非 exe 同级直连路径（2026-11-09）：
 *   桌面上 applicationDirPath() 是 exe 目录，故 "<exeDir>/../Library/XFont/
 *   XFontOutlineCommon" 能命中工作副本的字库；安卓上 argv[0] 无目录 →
 *   applicationDirPath() 落到 XSystem_executableFilePath（读 /proc/self/exe）
 *   → 得到的是 /system/bin（安卓进程的 exe 是 app_process），拼出的
 *   "/system/Library/XFont/..." 永远不存在 → FT 探测落空 → **静默**回退
 *   位图字库 XFont8x16（只有 96 个汉字）→ 中文整片空白。
 *   安卓上字库的真源是编译期目录 XFONT_EXTERNAL_FT_FONT_DIR
 *   （根 CMakeLists 的安卓分支注入 = /data/data/<pkg>/files/fonts，由安卓
 *   壳启动时从 APK assets 释放），故此处用**家族名**让 FT 走目录探测链
 *   ——与 XGuiWindowDemo 的安卓分支同款口径（xgui_window_demo.c 的
 *   XGUI_DEMO_DEFAULT_FONT_FAMILY）。
 */
void HsfTheme_applyDefaultFont(XWidget* widget)
{
    XFont font;
    if (!widget)
        return;
#if defined(__ANDROID__)
    /* 安卓：家族名 → FT 按编译期目录探测 <dir>/<family>.{ttf,otf,ttc}。 */
    XFont_init(&font);
    XFont_setFamily(&font, XFONT_DEFAULT_FAMILY);
    XWidget_setFont(widget, &font);
    XClassDeinit(&font);
#else
    {
        const XString* exeDir;
        char fontPath[512];
        exeDir = XCoreApplication_applicationDirPath();
        if (exeDir &&
            snprintf(fontPath, sizeof(fontPath),
                     "%s/../Library/XFont/XFontOutlineCommon",
                     XString_toUtf8(exeDir)) > 0 &&
            strlen(fontPath) < sizeof(fontPath)) {
            XFont_init(&font);
            XFont_setFamily(&font, fontPath); /* setFamily 深拷贝路径串。 */
            XWidget_setFont(widget, &font);
            XClassDeinit(&font);
            XClassDelete((XClass*)exeDir);
            return;
        }
        if (exeDir)
            XClassDelete((XClass*)exeDir);
    }
    XFont_init(&font);
    XFont_setFamily(&font, XFONT_DEFAULT_FAMILY);
    XWidget_setFont(widget, &font);
    XClassDeinit(&font);
#endif
}

/** @brief 安装缺省主题（Fusion 样式 + 主题样式表）。 */
void HsfTheme_install(void)
{
#if XSTYLE_ON
    XFusionStyle_installDefault();
    XStyle_installStyleSheet(hsf_theme_css);
#endif
}

/* ==================== 旗舰双主题（2026-11-11 深色·控制室 墨青） ==================== */

/** @brief 最近一次经旗舰入口落定的主题索引（0=亮缺省/6=深）。
 *  @note  放在裁剪守卫之外：HsfTheme_currentFlagshipSkin 无条件读它，
 *         框架段被裁剪时回读恒为缺省亮色（applyFlagshipSkin 桩不改它）。 */
static int s_flagshipSkin = HSF_THEME_SKIN_LIGHT;

#if HSF_THEME_FLAGSHIP_ON

/** @brief ARGB 常量 → XColor（调色板装盘用；全主题色值只登记在 HsfTheme.c
 *         本文件的装盘点与 HsfThemeCss.h 的样式表，改主题两处同改）。 */
static XColor hsfThemeColor(uint32_t argb)
{
    return XColor_create_rgb((int)((argb >> 16) & 0xFFu),
                             (int)((argb >> 8) & 0xFFu),
                             (int)(argb & 0xFFu),
                             (int)((argb >> 24) & 0xFFu));
}

/**
 * @brief 定制深色调色板（深色·控制室 墨青；对比度登记表见
 *        HsfThemeCss.h 深色段头）。
 * @details 打底用框架 XPalette_init_dark（未点名的角色沿用其值，如
 *          LinkVisited/Disabled 组 Link），再逐角色覆盖设计令牌。路径
 *          对标 XGuiApplication.c 的方案联动槽：内置深色盘
 *          （XPalette_init_dark）正文固定 #F0F0F0、Highlight 固定
 *          #308CC6 不可定制，且 HighlightedText 3.24:1、ToolTip 1.12:1
 *          两处不达标（本会话实跑核算），故旗舰主题必须走定制盘而非
 *          方案联动旁路。衍生角色（喂 Fusion 凹槽/斜面）：Button/Base
 *          =#23282F（与 QSS 控件底同值——XTabBar 页签底回落 Button/
 *          Base，不同值页签错色）、Dark=#14181D（凹槽描边）、Light=
 *          #3A424D、Mid=#2E353D；Midlight=#343B45/Shadow=#101318 为
 *          上述登记值的同色相插值衍生（框架深色盘同款 lighter/darker
 *          手法），非新色相。
 */
static void hsfThemeInitDarkPalette(XPalette* pal)
{
    XPaletteColorGroup g;
    if (!pal) return;

    XPalette_init_dark(pal); /* 未点名角色打底（含禁用组框架口径）。 */

    /* ---- 正常三组（Active/Inactive/Current；setColor 把 Current
     *      归一到 Active，三组都写与框架 init 同口径） ---- */
    for (g = XPaletteColorGroup_Active; g <= XPaletteColorGroup_Current; ++g) {
        XPalette_setColor(pal, g, XPaletteColorRole_Window,      hsfThemeColor(0xFF1B1F24u)); /* 窗底 */
        XPalette_setColor(pal, g, XPaletteColorRole_Button,      hsfThemeColor(0xFF23282Fu)); /* 面板/控件底 */
        XPalette_setColor(pal, g, XPaletteColorRole_Base,        hsfThemeColor(0xFF23282Fu)); /* 同 Button：页签底一致 */
        XPalette_setColor(pal, g, XPaletteColorRole_AlternateBase, hsfThemeColor(0xFF2A313Au)); /* 斑马纹（正文 10.90） */
        XPalette_setColor(pal, g, XPaletteColorRole_WindowText,  hsfThemeColor(0xFFE8EAEDu)); /* 正文 #E8EAED */
        XPalette_setColor(pal, g, XPaletteColorRole_Text,        hsfThemeColor(0xFFE8EAEDu));
        XPalette_setColor(pal, g, XPaletteColorRole_ButtonText,  hsfThemeColor(0xFFE8EAEDu));
        XPalette_setColor(pal, g, XPaletteColorRole_ToolTipText, hsfThemeColor(0xFFE8EAEDu));
        XPalette_setColor(pal, g, XPaletteColorRole_PlaceholderText, hsfThemeColor(0x80E8EAEDu)); /* 正文 50% alpha（框架构造口径） */
        XPalette_setColor(pal, g, XPaletteColorRole_Highlight,   hsfThemeColor(0xFF4DA3FFu)); /* 强调线兼主色 */
        XPalette_setColor(pal, g, XPaletteColorRole_HighlightedText, hsfThemeColor(0xFF101820u)); /* vs Highlight 6.82 ≥4.5 */
        XPalette_setColor(pal, g, XPaletteColorRole_Accent,      hsfThemeColor(0xFF4DA3FFu)); /* 镜像 Highlight（框架口径） */
        XPalette_setColor(pal, g, XPaletteColorRole_Link,        hsfThemeColor(0xFF4DA3FFu)); /* vs 窗底 6.31 ≥4.5 */
        XPalette_setColor(pal, g, XPaletteColorRole_ToolTipBase, hsfThemeColor(0xFF2A313Au)); /* 修正内置盘 1.12:1 缺陷 */
        XPalette_setColor(pal, g, XPaletteColorRole_Dark,        hsfThemeColor(0xFF14181Du)); /* 凹槽描边 */
        XPalette_setColor(pal, g, XPaletteColorRole_Light,       hsfThemeColor(0xFF3A424Du));
        XPalette_setColor(pal, g, XPaletteColorRole_Mid,         hsfThemeColor(0xFF2E353Du));
        XPalette_setColor(pal, g, XPaletteColorRole_Midlight,    hsfThemeColor(0xFF343B45u)); /* Mid/Light 同色相插值 */
        XPalette_setColor(pal, g, XPaletteColorRole_Shadow,      hsfThemeColor(0xFF101318u)); /* Dark 再压暗一档 */
    }

    /* ---- 禁用组（禁用态对比豁免；较框架深色 #828282 提亮一档） ----
     * 底=#1E242B、文本=#8E96A0（vs 禁用底 5.23 ≥4.5）；斜面沿用正常组
     * 的登记衍生值（框架禁用组是浅灰斜面，深色下会画出刺眼亮边）；
     * Highlight 复用 Light=#3A424D（中性暗灰蓝，不抢眼）。 */
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_Window,      hsfThemeColor(0xFF1E242Bu));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_Button,      hsfThemeColor(0xFF1E242Bu));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_Base,        hsfThemeColor(0xFF1E242Bu));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_AlternateBase, hsfThemeColor(0xFF1E242Bu));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_WindowText,  hsfThemeColor(0xFF8E96A0u));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_Text,        hsfThemeColor(0xFF8E96A0u));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_ButtonText,  hsfThemeColor(0xFF8E96A0u));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_PlaceholderText, hsfThemeColor(0xFF8E96A0u));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_ToolTipText, hsfThemeColor(0xFF8E96A0u));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_ToolTipBase, hsfThemeColor(0xFF2A313Au));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_Highlight,   hsfThemeColor(0xFF3A424Du));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_HighlightedText, hsfThemeColor(0xFF101820u));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_Dark,        hsfThemeColor(0xFF14181Du));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_Light,       hsfThemeColor(0xFF3A424Du));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_Mid,         hsfThemeColor(0xFF2E353Du));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_Midlight,    hsfThemeColor(0xFF343B45u));
    XPalette_setColor(pal, XPaletteColorGroup_Disabled, XPaletteColorRole_Shadow,      hsfThemeColor(0xFF101318u));
}

#endif /* HSF_THEME_FLAGSHIP_ON */

bool HsfTheme_isDarkSkin(void)
{
    return HsfTheme_currentFlagshipSkin() == HSF_THEME_SKIN_DARK;
}

void HsfTheme_applySecondaryText(XWidget* widget)
{
#if XPALETTE_ON
    XPalette p;
    XColor c;
    uint32_t argb = HsfTheme_isDarkSkin() ? HSF_COLOR_TEXT_SECONDARY_DARK
                                          : HSF_COLOR_TEXT_SECONDARY;
    if (!widget) return;
    c = XColor_create_rgb((int)((argb >> 16) & 0xFFu),
                          (int)((argb >> 8) & 0xFFu),
                          (int)(argb & 0xFFu), 0xFF);
    p = XWidget_palette(widget);
    XPalette_setColor(&p, XPaletteColorGroup_Active,
                      XPaletteColorRole_WindowText, c);
    XPalette_setColor(&p, XPaletteColorGroup_Inactive,
                      XPaletteColorRole_WindowText, c);
    XWidget_setPalette(widget, &p);
    /* 前景角色切 WindowText：标签缺省推断大多就是它（见 XLabel
     * label_foregroundRole），显式钉死避免背景角色联动改推断。 */
    XWidget_setForegroundRole(widget, XPaletteColorRole_WindowText);
#else
    (void)widget;
#endif /* XPALETTE_ON */
}

#if XPALETTE_ON
/** @brief 判定控件是否本入口钉过的次级文本（两套令牌色值精确匹配）。 */
static bool hsfThemeIsSecondaryPinned(const XWidget* widget)
{
    XPalette p;
    XColor c;
    uint32_t v;
    if (!widget) return false;
    if (XWidget_foregroundRole((XWidget*)widget) !=
        XPaletteColorRole_WindowText)
        return false;
    p = XWidget_palette((XWidget*)widget);
    c = XPalette_color(&p, XPaletteColorGroup_Active,
                       XPaletteColorRole_WindowText);
    v = XColor_rgba(&c);
    return v == HSF_COLOR_TEXT_SECONDARY ||
           v == HSF_COLOR_TEXT_SECONDARY_DARK;
}

/** @brief 图表视图随应用主题换图表主题（缺省亮表 ⇄ Dark 表）。
 *  @details 深色页面上缺省 Light 图表=白底块+图例正文（调色板
 *           WindowText=#E8EAED）落白底 1.2:1（走查实锅）；Dark 表的
 *           背景/网格/轴标签按暗底设计。类名识别走 XVTABLE_GET_NAME
 *           （QSS 元素名匹配同源）。 */
static void hsfThemeRefreshChartTheme(XWidget* widget)
{
#if XCHARTS_ON
    XVtable* vt = XClassGetVtable((const XClass*)widget);
    if (vt && XVTABLE_GET_NAME(vt) &&
        strcmp(XVTABLE_GET_NAME(vt), "XChartView") == 0) {
        XChart* chart = XChartView_chart((const XChartView*)widget);
        if (chart)
            XChart_setTheme(chart, HsfTheme_isDarkSkin()
                                       ? XChart_ChartTheme_Dark
                                       : XChart_ChartTheme_Light);
    }
#else
    (void)widget;
#endif /* XCHARTS_ON */
}

/** @brief 先钉根、再递归子树（XObject_children 口径照
 *         XWidget.c xwidget_collectOpaqueChildren）。 */
static void hsfThemeRefreshSecondaryWalk(XWidget* widget)
{
    const XVector* kids;
    size_t n;
    size_t i;
    if (!widget) return;
    if (hsfThemeIsSecondaryPinned(widget))
        HsfTheme_applySecondaryText(widget);
    hsfThemeRefreshChartTheme(widget);
    kids = XObject_children((XObject*)widget);
    n = kids ? XVector_size_base((const XContainer*)kids) : 0;
    for (i = 0; i < n; ++i) {
        XObject* c = *(XObject**)XVector_at_base(kids, (int64_t)i);
        if (c && c->m_is_widget)
            hsfThemeRefreshSecondaryWalk((XWidget*)c);
    }
}
#endif /* XPALETTE_ON */

void HsfTheme_refreshSecondaryTexts(XWidget* root)
{
#if XPALETTE_ON
    hsfThemeRefreshSecondaryWalk(root);
#else
    (void)root;
#endif /* XPALETTE_ON */
}

/**
 * @brief 应用旗舰主题（0=旗舰亮色 / 6=旗舰深色·控制室），实现见
 *        HsfTheme.h 契约。
 */
bool HsfTheme_applyFlagshipSkin(int skinIndex)
{
#if HSF_THEME_FLAGSHIP_ON
    XStyleHints* hints;
    if (skinIndex != HSF_THEME_SKIN_LIGHT && skinIndex != HSF_THEME_SKIN_DARK)
        return false;
    hints = XGuiApplication_styleHints();
    if (skinIndex == HSF_THEME_SKIN_DARK) {
        XPalette pal;
        hsfThemeInitDarkPalette(&pal);
        /* 顺序=先盘后方案：setPalette 置位显式标志（对标 Qt AA_SetPalette，
         * XGuiApplication.c m_paletteExplicitlySet），随后的 setColorScheme
         * 触发的方案联动槽在守卫处早退——定制盘不会被内置深色盘覆盖。 */
        XGuiApplication_setPalette(&pal);
        if (hints)
            XStyleHints_setColorScheme(hints, XStyleHintsColorScheme_Dark);
        XApplication_setStyleSheet_2(hsf_theme_dark_css);
    } else {
        /* 切回亮色：显式重置标准浅色盘（NULL=重置并冻结联动，与深色侧
         * 同一显式语义）+ 浅色方案 + 旗舰亮色表——复位定制盘三件套。 */
        XGuiApplication_setPalette(NULL);
        if (hints)
            XStyleHints_setColorScheme(hints, XStyleHintsColorScheme_Light);
        XApplication_setStyleSheet_2(hsf_theme_css);
    }
    s_flagshipSkin = skinIndex;
    return true;
#else
    (void)skinIndex;
    return false;
#endif
}

int HsfTheme_currentFlagshipSkin(void)
{
    return s_flagshipSkin;
}
