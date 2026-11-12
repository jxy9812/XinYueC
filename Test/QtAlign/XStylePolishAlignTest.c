/**
 * @file       XStylePolishAlignTest.c
 * @brief      XStyle/QStyle 6.8 polish 族基类缺省空实现对齐契约测试
 *             （框架缺陷②：polish/unpolish NULL 段错误根治）。
 * @details    取证（Qt 6.8.3 原文）：
 *             - QStyle::polish(QWidget*) / unpolish(QWidget*) /
 *               polish(QPalette&) 三个虚函数都是**空实现**
 *               （qstyle.cpp:434-436 / 453-455 / 486-488）。
 *             - QCommonStyle 三个槽都只转调基类
 *               （qcommonstyle.cpp:6241-6244 / 6249-6252 / 6257-6260）。
 *             - QWindowsStyle 三个槽转调 QCommonStyle
 *               （qwindowsstyle.cpp:1063-1079），QFusionStyle::polish 亦然。
 *             结论：基类必须有真函数指针（缺省空实现），派生槽内
 *             XClass_Parent(XStyle, …) 的回落才永远安全。
 *
 *             缺陷实录（上轮复核员 gdb 实证）：XStyle_class_init 把
 *             EXStyle_Polish/Unpolish 两槽显式置 NULL（旧 XStyle.c:
 *             178-179），VXCommonStyle_polish/unpolish 走
 *             XClass_Parent(XStyle, …) 直调函数指针 ⇒ 对已显示控件
 *             XWidget_setStyle(控件, XCommonStyle 派生样式) 即段错误
 *             （栈：XWidget_setStyle → XStyle_polish_base →
 *             VXCommonStyle_polish → 0x0）。同族第三槽
 *             EXStyle_PolishPalette 亦为 NULL（旧 XStyle.c:193），
 *             VXCommonStyle_polishPalette 走同一条 NULL 回落路径。
 *
 * @note       本文件只覆盖 polish 族回落链；XCommonStyle/XStyleOption/
 *             XStyleSheetStyle 的既有回归由 --filter 分别跑既有套件。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XSTYLE_ON && XWIDGET_ON

#include "XStyle.h"
#include "XStyle_Protected.h"
#include "XCommonStyle.h"
#include "XStyleOption.h"
#include "XFusionStyle.h"
#include "XWindowsStyle.h"
#include "XStyleSheetStyle.h"
#include "XPalette.h"
#include "XWidget.h"
#include "XObject.h"
#include "XClass.h"
#include "XMemory.h"

/* ==================== 探针派生样式（重载 polish 族 + 回落计数） ====================
 *
 * 继承 XCommonStyle，重载 polish/unpolish/polishPalette：每个槽先自增计数，
 * 再 XClass_Parent(XCommonStyle, …) 回落 —— 该回落链一路经过
 * VXCommonStyle_polish → XClass_Parent(XStyle, …) → VXStyle_polish
 * （XStyle 基类缺省空实现）。修复前任一环取到 NULL 直接调用即段错误；
 * 修复后计数精确且全程不崩。 */

typedef struct XStylePolishProbeStyle
{
    XCommonStyle m_base; /**< 基类；必须是第一个成员。 */
} XStylePolishProbeStyle;

static int xsp_polishCount = 0;
static int xsp_unpolishCount = 0;
static int xsp_polishPaletteCount = 0;

/** @brief 复位三槽计数（每用例开头调用，隔离用例间状态）。 */
static void xsp_resetCounts(void)
{
    xsp_polishCount = 0;
    xsp_unpolishCount = 0;
    xsp_polishPaletteCount = 0;
}

static void xsp_polish(XStyle* self, XWidget* widget)
{
    ++xsp_polishCount;
    /* 派生槽内回落父类一律 XClass_Parent（§497 条款 4），不手搓查表。 */
    XClass_Parent(XCommonStyle, EXStyle_Polish, void (*)(XStyle*, XWidget*))
        ((XStyle*)self, widget);
}

static void xsp_unpolish(XStyle* self, XWidget* widget)
{
    ++xsp_unpolishCount;
    XClass_Parent(XCommonStyle, EXStyle_Unpolish, void (*)(XStyle*, XWidget*))
        ((XStyle*)self, widget);
}

static void xsp_polishPalette(XStyle* self, XPalette* palette)
{
    ++xsp_polishPaletteCount;
    XClass_Parent(XCommonStyle, EXStyle_PolishPalette,
                  void (*)(XStyle*, XPalette*))((XStyle*)self, palette);
}

XCLASS_DEFINE_BEGING(XStylePolishProbeStyle)
XCLASS_DEFINE_EXTEND_END(XStylePolishProbeStyle, XCommonStyle)

static XVtable* XStylePolishProbeStyle_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStylePolishProbeStyle)
    XVTABLE_INHERIT_XCLASS(XCommonStyle);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_Polish, xsp_polish);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_Unpolish, xsp_unpolish);
    XVTABLE_OVERLOAD_DEFAULT(EXStyle_PolishPalette, xsp_polishPalette);
    return XVTABLE_DEFAULT;
}

/** @brief 堆上创建探针样式（XCommonStyle_init 打底 + 换表，借用语义同
 *         既有测试范式；控件不接管所有权，调用方 XClassDelete 释放）。 */
static XStylePolishProbeStyle* xsp_createProbe(void)
{
    XStylePolishProbeStyle* style = (XStylePolishProbeStyle*)XMemory_malloc(
        sizeof(XStylePolishProbeStyle), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!style) return NULL;
    XMemset(style, 0, sizeof(*style));
    XCommonStyle_init(&style->m_base);
    XClassSetVtable(style, XStylePolishProbeStyle);
    Set_Class_Memory(style, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(style, true);
    return style;
}

/* ==================== ① gdb 复现路径正向锁 ==================== */

/** @brief 对**已显示控件** setStyle(XCommonStyle 派生) 不崩且 polish 生效。
 *  @details 复现上轮 gdb 栈：XWidget_show 触发 ensurePolished 置
 *           m_polished=1（XWidget.c:8650-8651），随后 XWidget_setStyle
 *           对已 polished 控件走 XStyle_unpolish_base(旧) →
 *           XStyle_polish_base(新)（XWidget.c:11818/11824）。修复前新样式
 *           VXCommonStyle_polish 回落 NULL ⇒ 段错误；修复后计数=1、
 *           控件 m_polished 与 m_styleSet 保持正确。 */
XQ_TEST(XStylePolish, shownWidgetSetStyleNoCrash)
{
    XWidget* host;
    XStylePolishProbeStyle* style;

    xsp_resetCounts();
    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 80, 40);
    XWidget_show(host);                 /* ensurePolished：m_polished=1 */
    XQ_EQ_I64(host->m_polished, 1);

    style = xsp_createProbe();
    XQ_REQUIRE(style != NULL);

    /* 正向锁：这一行在修复前是段错误（VXCommonStyle_polish → 0x0）。 */
    XWidget_setStyle(host, (XStyle*)style);
    XQ_EQ_I64(xsp_polishCount, 1);       /* 新样式 polish 被调用一次 */
    XQ_EQ_I64(xsp_unpolishCount, 0);     /* 控件未显式设过样式，旧样式不 unpolish */
    XQ_EQ_I64(host->m_styleSet, 1);
    XQ_EQ_I64(host->m_polished, 1);
    XQ_CHECK(XWidget_style((const XWidget*)host) == (XStyle*)style);

    /* 借用语义：控件不接管样式所有权，先卸再删（与既有测试范式一致）。 */
    XWidget_setStyle(host, NULL);
    XQ_EQ_I64(xsp_unpolishCount, 1);     /* 卸载走 unpolish */
    XQ_EQ_I64(xsp_polishCount, 1);       /* setStyle(NULL) 不再 polish */
    XClassDelete((XClass*)style);
    XClassDelete((XClass*)host);
}

/* ==================== ② 换样式 unpolish→polish 幂等 ==================== */

/** @brief 连续换样式 unpolish→polish 全链不崩且计数精确、状态幂等。
 *  @details XWidget_setStyle 每次换样式：旧样式 unpolish（若 polished）
 *           + 新样式 polish（XWidget.c:11818/11824），并保留 m_polished
 *           位；同值重复调用早退（:11817）。验证 A→B→A→B→NULL 五步。 */
XQ_TEST(XStylePolish, switchStyleUnpolishPolishIdempotent)
{
    XWidget* host;
    XStylePolishProbeStyle* a;
    XStylePolishProbeStyle* b;
    XCommonStyle* common;

    xsp_resetCounts();
    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_show(host);
    a = xsp_createProbe();
    b = xsp_createProbe();
    common = XCommonStyle_create();
    XQ_REQUIRE(a != NULL && b != NULL && common != NULL);

    /* ① 装 A：polish=1。 */
    XWidget_setStyle(host, (XStyle*)a);
    XQ_EQ_I64(xsp_polishCount, 1);
    XQ_EQ_I64(xsp_unpolishCount, 0);

    /* ② A→B：A unpolish + B polish。 */
    XWidget_setStyle(host, (XStyle*)b);
    XQ_EQ_I64(xsp_unpolishCount, 1);
    XQ_EQ_I64(xsp_polishCount, 2);

    /* ③ 同值重复 setStyle(B)：早退，计数不动（幂等）。 */
    XWidget_setStyle(host, (XStyle*)b);
    XQ_EQ_I64(xsp_unpolishCount, 1);
    XQ_EQ_I64(xsp_polishCount, 2);

    /* ④ B→XCommonStyle（未重载探针槽）：B unpolish，基类回落空实现。 */
    XWidget_setStyle(host, (XStyle*)common);
    XQ_EQ_I64(xsp_unpolishCount, 2);
    XQ_EQ_I64(xsp_polishCount, 2);
    XQ_EQ_I64(host->m_polished, 1);      /* polish 位全程保持 */

    /* ⑤ XCommonStyle→A：XCommonStyle unpolish 回落空实现 + A polish。 */
    XWidget_setStyle(host, (XStyle*)a);
    XQ_EQ_I64(xsp_polishCount, 3);
    XQ_EQ_I64(xsp_unpolishCount, 2);

    /* ⑥ 卸载：A unpolish，不再 polish。 */
    XWidget_setStyle(host, NULL);
    XQ_EQ_I64(xsp_unpolishCount, 3);
    XQ_EQ_I64(xsp_polishCount, 3);
    XQ_EQ_I64(host->m_styleSet, 0);
    XQ_EQ_I64(host->m_polished, 1);

    /* 未 polished 控件装样式不应触发 polish（XWidget.c:11821-11824）：
     * 幂等锚 m_polished=0 时由后续 ensurePolished 统一落地。 */
    {
        XWidget* fresh = XWidget_create(NULL, 0);
        XQ_REQUIRE(fresh != NULL);
        XQ_EQ_I64(fresh->m_polished, 0);
        XWidget_setStyle(fresh, (XStyle*)b);
        XQ_EQ_I64(xsp_polishCount, 3);   /* 尚未 polish，计数不动 */
        XWidget_show(fresh);             /* 首显 ensurePolished */
        XQ_EQ_I64(fresh->m_polished, 1);
        XWidget_setStyle(fresh, NULL);
        XClassDelete((XClass*)fresh);
    }

    XClassDelete((XClass*)common);
    XClassDelete((XClass*)b);
    XClassDelete((XClass*)a);
    XClassDelete((XClass*)host);
}

/* ==================== ③ 派生重载回落链正确 ==================== */

/** @brief 派生样式重载 polish 时 XClass_Parent 到基类空实现不崩。
 *  @details 直接对**裸 XStyle 基类实例**与派生实例调用三个 _base 分派
 *           入口：基类槽必须挂真函数指针（VXStyle_polish 等），
 *           XVtableGetFunc 取到非 NULL；派生链逐层回落后抵达基类空
 *           实现。同时覆盖 XFusionStyle / XWindowsStyle / XStyleSheetStyle
 *           三个既有派生样式族的 polish/unpolish/polishPalette 全链，
 *           确保挂表后所有派生回落链都通。 */
XQ_TEST(XStylePolish, parentFallbackToBaseNoOp)
{
    XStyle* base;
    XCommonStyle* common;
    XFusionStyle* fusion;
    XWindowsStyle* windows;
    XStyleSheetStyle* sheet;
    XStylePolishProbeStyle* probe;
    XWidget* host;
    XPalette pal;

    xsp_resetCounts();
    XPalette_init_default(&pal);
    base = XStyle_create();
    common = XCommonStyle_create();
    fusion = XFusionStyle_create();
    windows = XWindowsStyle_create();
    sheet = XStyleSheetStyle_create();
    probe = xsp_createProbe();
    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(base != NULL && common != NULL && fusion != NULL &&
               windows != NULL && sheet != NULL && probe != NULL &&
               host != NULL);
    XWidget_show(host);

    /* ① 基类三槽挂的是真函数指针（不是 NULL）——缺陷根因的直接锁。 */
    XQ_CHECK(XVtableGetFunc(XClassGetVtable((XClass*)base), EXStyle_Polish,
                            void (*)(XStyle*, XWidget*)) != NULL);
    XQ_CHECK(XVtableGetFunc(XClassGetVtable((XClass*)base), EXStyle_Unpolish,
                            void (*)(XStyle*, XWidget*)) != NULL);
    XQ_CHECK(XVtableGetFunc(XClassGetVtable((XClass*)base),
                            EXStyle_PolishPalette,
                            void (*)(XStyle*, XPalette*)) != NULL);

    /* ② 直接分派基类三个 _base 入口：空实现，不崩、不改计数。 */
    XStyle_polish_base(base, host);
    XStyle_unpolish_base(base, host);
    XStyle_polishPalette_base(base, &pal);
    XQ_EQ_I64(xsp_polishCount, 0);
    XQ_EQ_I64(xsp_unpolishCount, 0);
    XQ_EQ_I64(xsp_polishPaletteCount, 0);

    /* ③ XCommonStyle 三个 _base 入口：回落基类空实现，不崩。 */
    XStyle_polish_base((XStyle*)common, host);
    XStyle_unpolish_base((XStyle*)common, host);
    XStyle_polishPalette_base((XStyle*)common, &pal);

    /* ④ 既有派生样式族全链（Fusion/Windows/SheetStyle 的 polish 槽
     *    都 XClass_Parent 回落 XCommonStyle → XStyle，缺一即段错误）。 */
    XStyle_polish_base((XStyle*)fusion, host);
    XStyle_unpolish_base((XStyle*)fusion, host);
    XStyle_polishPalette_base((XStyle*)fusion, &pal);
    XStyle_polish_base((XStyle*)windows, host);
    XStyle_unpolish_base((XStyle*)windows, host);
    XStyle_polishPalette_base((XStyle*)windows, &pal);
    XStyle_polish_base((XStyle*)sheet, host);
    XStyle_unpolish_base((XStyle*)sheet, host);
    XStyle_polishPalette_base((XStyle*)sheet, &pal);

    /* ⑤ 探针派生重载：每个槽自增 + XClass_Parent 逐层回落到基类空实现，
     *    计数各 +1 且不崩（回落链贯通的正向证据）。 */
    XStyle_polish_base((XStyle*)probe, host);
    XStyle_unpolish_base((XStyle*)probe, host);
    XStyle_polishPalette_base((XStyle*)probe, &pal);
    XQ_EQ_I64(xsp_polishCount, 1);
    XQ_EQ_I64(xsp_unpolishCount, 1);
    XQ_EQ_I64(xsp_polishPaletteCount, 1);

    /* ⑥ NULL 守卫：_base 入口对 NULL self/widget/palette 静默返回
     *    （XStyle.c:273-301 的既有契约，挂表后不改变）。 */
    XStyle_polish_base(NULL, host);
    XStyle_polish_base(base, NULL);
    XStyle_unpolish_base(NULL, host);
    XStyle_polishPalette_base(NULL, &pal);
    XStyle_polishPalette_base(base, NULL);
    XQ_EQ_I64(xsp_polishCount, 1);
    XQ_EQ_I64(xsp_unpolishCount, 1);
    XQ_EQ_I64(xsp_polishPaletteCount, 1);

    XClassDelete((XClass*)host);
    XClassDelete((XClass*)probe);
    XClassDelete((XClass*)sheet);
    XClassDelete((XClass*)windows);
    XClassDelete((XClass*)fusion);
    XClassDelete((XClass*)common);
    XClassDelete((XClass*)base);
}

/* ==================== ④ 既有样式族回归桥 ==================== */

/** @brief 修复后既有样式族（XCommonStyle/XStyleOption/XStyleSheetStyle）
 *         的 polish 族行为不被改变：XCommonStyle 派生样式对已显示控件
 *         装样式后仍能正常 sizeFromContents/pixelMetric，且换回不崩。
 *  @details 既有用例套件由本域验收命令以
 *           --filter=XCommonStyle / XStyleOption / XStyleSheetStyle
 *           分别跑（本域不重复实现）；本用例只锁「修复没有让既有
 *           样式族的常规分派退化」这一最小回归面。 */
XQ_TEST(XStylePolish, existingStyleFamiliesIntact)
{
    XWidget* host;
    XCommonStyle* common;
    XStyleOption opt;
    XSize sz;
    XSize out;
    int pm;

    host = XWidget_create(NULL, 0);
    XQ_REQUIRE(host != NULL);
    XWidget_show(host);
    common = XCommonStyle_create();
    XQ_REQUIRE(common != NULL);

    XWidget_setStyle(host, (XStyle*)common);
    XQ_CHECK(XWidget_style((const XWidget*)host) == (XStyle*)common);

    /* 常规分派仍可用（polish 挂表不得扰动其它 18 个槽的下标）。 */
    XStyleOption_init(&opt, XStyleCT_PushButton);
    XSize_init(&sz, 40, 20);
    out = XStyle_sizeFromContents_base((XStyle*)common, XStyleCT_PushButton,
                                       &opt, sz);
    XQ_CHECK(out.width > 0 && out.height > 0);
    pm = XStyle_pixelMetric_base((XStyle*)common,
                                 XStylePM_DefaultFrameWidth, &opt);
    XQ_CHECK(pm >= 0);

    XWidget_setStyle(host, NULL);
    XClassDelete((XClass*)common);
    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XSTYLE_ON && XWIDGET_ON */
