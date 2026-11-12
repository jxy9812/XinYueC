/**
 * @file       XProgressBarAlignTest.c
 * @brief      XProgressBar × Qt 6.8 QProgressBar 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /usr/include/x86_64-linux-gnu/qt6/QtWidgets/qprogressbar.h
 *               （minimum/maximum/value/text/textVisible/alignment/
 *               orientation/invertedAppearance/textDirection/
 *               format+resetFormat、Direction 枚举、initStyleOption）；
 *             - Qt 6.8.3 源码 qtbase/src/widgets/widgets/qprogressbar.cpp：
 *               QProgressBarPrivate 构造 value(-1)/alignment(AlignLeft)
 *               /textVisible(true)/textDirection(TopToBottom)（:40-53）、
 *               init 的 QSizePolicy(Expanding, Fixed) 垂直转置（:60-70）、
 *               setValue 越界**忽略**（非 busy 时，:277-291）、
 *               setOrientation 的 sizePolicy 转置（:467-481）、
 *               reset 置 minimum-1（INT_MIN 时 INT_MIN，:220-232）、
 *               text 的 busy/复位空串与 %p/%v/%m 替换（:424-450）、
 *               sizeHint = qMax(9,cw)*7 + '0'宽*4，字高+8（:383-397）、
 *               minimumSizeHint 高 = 字高+2（:399-414）、
 *               initStyleOption 字段（:78-100）；
 *             - 本机 Qt 6.8.0 + Fusion + offscreen 实测：
 *               缺省 value=-1、text=""、alignment=1(AlignLeft)、
 *               sizePolicy=(7,0)、controlType=DefaultType、
 *               sizeHint 91x25、minimumSizeHint 91x19；
 *               setValue(200)/( -50) 后 value 仍 -1（越界忽略）；
 *               setRange(0,0) 后 setValue(50) 生效（busy 例外）、text=""；
 *               setRange(1,5)+setValue(3) → "50%"；resetFormat 回 "%p%"。
 *
 *             覆盖：缺省属性（含构造复位哨兵）、越界忽略 vs busy 例外、
 *             文本模板/reset/busy、方向与 sizePolicy 转置、尺寸提示、
 *             initStyleOption、valueChanged 次数与信号函数纯度、父子所有权。
 *             [2026-11-11] 追加：
 *             - P0-66：`setFormat("")` 的**空文本**语义（format()/text()
 *               都原样返回空串），NULL 仍是「恢复缺省」；
 *             - P3-03：text() 的虚派发——派生类覆写 EXProgressBar_Text
 *               后 `XProgressBar_text` / `XProgressBar_text_base` 都走覆写；
 *             - P0-65/P0-E9：CE_ProgressBarContents 的 invertedAppearance
 *               反转填充起点 + setRange(0,0) 的 4px busy 游动块（ARGB32
 *               离屏像素探针）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XPROGRESSBAR_ON

#include "XProgressBar.h"
#include "XWidget.h"
#include "XObject.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XCommonStyle.h"
#include "XImage.h"
#include "XPainter.h"
#include "XPalette.h"
#include "XColor.h"
#include "XVarList.h"

/* ==================== 信号探针 ==================== */

static int pb_valueChangedCount = 0;
static int pb_lastValue = -1;

static void pb_valueChangedSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    XVarList_args_1(args, int, v);
    pb_lastValue = v;
    ++pb_valueChangedCount;
}

/* ==================== 用例 ==================== */

/** @brief 缺省值（qprogressbar.cpp:40-70）。 */
XQ_TEST(XProgressBar, defaults)
{
    XProgressBar* pb = XProgressBar_create(NULL, 0);
    char text[64];
    XWidgetSizePolicy policy;
    XQ_REQUIRE(pb != NULL);

    XQ_EQ_I64(XProgressBar_minimum(pb), 0);
    XQ_EQ_I64(XProgressBar_maximum(pb), 100);
    XQ_EQ_I64(XProgressBar_orientation(pb),
              (int)XGuiOrientation_Horizontal);
    XQ_CHECK(XProgressBar_isTextVisible(pb));
    XQ_EQ_I64(XProgressBar_textDirection(pb),
              (int)XProgressBarDirection_TopToBottom);
    XQ_CHECK(!XProgressBar_invertedAppearance(pb));
    /* 缺省对齐 = AlignLeft（qprogressbar.cpp:48；Qt 实测 alignment()==1）。 */
    XQ_EQ_I64(XProgressBar_alignment(pb), (int)XAlignment_Left);
    XQ_EQ_STR(XProgressBar_format(pb), "%p%");

    /* 构造 value = minimum-1 = -1（qprogressbar.cpp:48 的 value(-1)），
     * 复位态 text() 为空串（:429-434；Qt 实测）。 */
    XQ_EQ_I64(XProgressBar_value(pb), -1);
    XProgressBar_text(pb, text, (int)sizeof(text));
    XQ_EQ_STR(text, "");

    /* sizePolicy(Expanding, Fixed, DefaultType)，水平（qprogressbar.cpp:
     * 60-70；Qt 实测 7/0 + controlType 1）。 */
    policy = XWidget_sizePolicy((XWidget*)pb);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Expanding);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);
    XQ_EQ_I64(XWidgetSizePolicy_controlType(&policy),
              (int)XWidgetSizePolicyControl_DefaultType);

    XClassDelete((XClass*)pb);
}

/** @brief 越界忽略 / busy 例外（qprogressbar.cpp:277-291）。 */
XQ_TEST(XProgressBar, valueBounds)
{
    XProgressBar* pb = XProgressBar_create(NULL, 0);
    XQ_REQUIRE(pb != NULL);
    pb_valueChangedCount = 0;
    pb_lastValue = -1;
    XObject_connect_2((XObject*)pb,
                      (size_t)XProgressBar_valueChanged_signal(pb, 0),
                      pb_valueChangedSlot);

    /* 越界值直接忽略：value 保持 -1、不发信号（不钳位）。 */
    XProgressBar_setValue(pb, 200);
    XQ_EQ_I64(XProgressBar_value(pb), -1);
    XQ_EQ_I64(pb_valueChangedCount, 0);
    XProgressBar_setValue(pb, -50);
    XQ_EQ_I64(XProgressBar_value(pb), -1);
    XQ_EQ_I64(pb_valueChangedCount, 0);

    /* 范围内写入生效并发一次。 */
    XProgressBar_setValue(pb, 40);
    XQ_EQ_I64(XProgressBar_value(pb), 40);
    XQ_EQ_I64(pb_valueChangedCount, 1);
    XQ_EQ_I64(pb_lastValue, 40);
    /* 再越界仍忽略（不改变 40、不再发）。 */
    XProgressBar_setValue(pb, 200);
    XProgressBar_setValue(pb, -10);
    XQ_EQ_I64(XProgressBar_value(pb), 40);
    XQ_EQ_I64(pb_valueChangedCount, 1);

    /* busy（setRange(0,0)）：接受任意值（Qt 的 max==min==0 例外）。 */
    XProgressBar_setRange(pb, 0, 0);
    XProgressBar_setValue(pb, 42);
    XQ_EQ_I64(XProgressBar_value(pb), 42);
    XProgressBar_setValue(pb, -7);
    XQ_EQ_I64(XProgressBar_value(pb), -7);

    XClassDelete((XClass*)pb);
}

/** @brief 文本模板 / reset 哨兵 / busy 模式（qprogressbar.cpp:220-232,424-450）。 */
XQ_TEST(XProgressBar, textAndFormat)
{
    XProgressBar* pb = XProgressBar_create(NULL, 0);
    char text[64];
    XQ_REQUIRE(pb != NULL);

    /* 单步量程（totalSteps==0）返回 100% 以避免除零（:439-443）。 */
    XProgressBar_setRange(pb, 5, 5);
    XProgressBar_setValue(pb, 5);
    XProgressBar_text(pb, text, (int)sizeof(text));
    XQ_EQ_STR(text, "100%");

    /* %m 是总步数 maximum-minimum（:436），不是 maximum。 */
    XProgressBar_setRange(pb, 1, 5);
    XProgressBar_setValue(pb, 3);
    XProgressBar_text(pb, text, (int)sizeof(text));
    XQ_EQ_STR(text, "50%");
    XProgressBar_setFormat(pb, "%v/%m (%p%%)");
    XProgressBar_text(pb, text, (int)sizeof(text));
    XQ_EQ_STR(text, "3/4 (50%)");
    XProgressBar_resetFormat(pb);
    XQ_EQ_STR(XProgressBar_format(pb), "%p%");

    /* reset：minimum-1 哨兵、text() 空串、不发 valueChanged（:220-232）。 */
    pb_valueChangedCount = 0;
    XObject_connect_2((XObject*)pb,
                      (size_t)XProgressBar_valueChanged_signal(pb, 0),
                      pb_valueChangedSlot);
    XProgressBar_reset(pb);
    XQ_EQ_I64(XProgressBar_value(pb), 0); /* min=1 ⇒ 哨兵 0。 */
    XQ_EQ_I64(pb_valueChangedCount, 0);
    XProgressBar_text(pb, text, (int)sizeof(text));
    XQ_EQ_STR(text, "");

    /* busy（setRange(0,0)）：text() 空串且 setValue 接受任意值
     * （qprogressbar.cpp:277-291 的 busy 例外；Qt 实测 50 生效）。 */
    XProgressBar_setRange(pb, 0, 0);
    XProgressBar_setValue(pb, 50);
    XQ_EQ_I64(XProgressBar_value(pb), 50);
    XProgressBar_text(pb, text, (int)sizeof(text));
    XQ_EQ_STR(text, "");

    XClassDelete((XClass*)pb);
}

/** @brief 方向/尺寸提示/sizePolicy 转置 + valueChanged 次数与信号函数纯度。 */
XQ_TEST(XProgressBar, orientationAndSignals)
{
    XProgressBar* pb = XProgressBar_create(NULL, 0);
    XSize horizontal;
    XSize vertical;
    XWidgetSizePolicy policy;
    void* id1;
    void* id2;
    XQ_REQUIRE(pb != NULL);

    horizontal = XProgressBar_sizeHint(pb);
    XQ_CHECK(horizontal.width > 0 && horizontal.height > 0);
    policy = XWidget_sizePolicy((XWidget*)pb);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Expanding);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);

    XProgressBar_setOrientation(pb, XGuiOrientation_Vertical);
    vertical = XProgressBar_sizeHint(pb);
    /* Qt 的 sizeHint 先按方向转置再 sizeFromContents（:396-400）。 */
    XQ_EQ_I64(vertical.width, horizontal.height);
    XQ_EQ_I64(vertical.height, horizontal.width);
    /* setOrientation 转置 sizePolicy（qprogressbar.cpp:467-481）：垂直 ⇒
     * (Fixed, Expanding)。 */
    policy = XWidget_sizePolicy((XWidget*)pb);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Expanding);
    /* 非法方向忽略（setOrientation 只接受 1/2）。 */
    XProgressBar_setOrientation(pb, 99);
    XQ_EQ_I64(XProgressBar_orientation(pb),
              (int)XGuiOrientation_Vertical);
    XProgressBar_setOrientation(pb, XGuiOrientation_Horizontal);
    policy = XWidget_sizePolicy((XWidget*)pb);
    XQ_EQ_I64(XWidgetSizePolicy_horizontalPolicy(&policy),
              (int)XWidgetSizePolicy_Expanding);
    XQ_EQ_I64(XWidgetSizePolicy_verticalPolicy(&policy),
              (int)XWidgetSizePolicy_Fixed);

    /* minimumSizeHint（:399-414）：水平 = sizeHint 宽 × (字高+2)。 */
    XQ_EQ_I64(XProgressBar_minimumSizeHint(pb).width,
              XProgressBar_sizeHint(pb).width);
    XQ_CHECK(XProgressBar_minimumSizeHint(pb).height <=
             XProgressBar_sizeHint(pb).height);

    pb_valueChangedCount = 0;
    XObject_connect_2((XObject*)pb,
                      (size_t)XProgressBar_valueChanged_signal(pb, 0),
                      pb_valueChangedSlot);
    XProgressBar_setRange(pb, 0, 100);
    XProgressBar_setValue(pb, 40);
    XQ_EQ_I64(pb_valueChangedCount, 1);
    XQ_EQ_I64(pb_lastValue, 40);
    XProgressBar_setValue(pb, 40);
    XQ_EQ_I64(pb_valueChangedCount, 1);

    /* 信号函数只返回标识、不发射（与 XAbstractSlider_valueChanged_signal
     * 同契约；旧实现 self 非 NULL 时会额外发一次）。 */
    id1 = XProgressBar_valueChanged_signal(pb, 0);
    id2 = XProgressBar_valueChanged_signal(NULL, 0);
    XQ_CHECK(id1 != NULL);
    XQ_CHECK(id1 == id2);
    XQ_EQ_I64(pb_valueChangedCount, 1);

    XClassDelete((XClass*)pb);
}

/** @brief initStyleOption 字段（qprogressbar.cpp:78-100）。 */
XQ_TEST(XProgressBar, initStyleOption)
{
    XProgressBar* pb = XProgressBar_create(NULL, 0);
    XStyleOption opt;
    XQ_REQUIRE(pb != NULL);
    XProgressBar_setRange(pb, 10, 30);
    XProgressBar_setValue(pb, 20);
    XProgressBar_setInvertedAppearance(pb, true);
    XProgressBar_setTextVisible(pb, false);

    XProgressBar_initStyleOption(pb, &opt);
    XQ_EQ_I64(opt.m_progressMin, 10);
    XQ_EQ_I64(opt.m_progressMax, 30);
    XQ_EQ_I64(opt.m_progressValue, 20);
    XQ_CHECK(opt.m_horizontal);
    XQ_CHECK((opt.m_state & XStyleState_Horizontal) != 0);
    XQ_CHECK((opt.m_state & XStyleState_Enabled) != 0);
    XQ_CHECK(!opt.m_progressTextVisible);
    XQ_CHECK(opt.m_progressInverted);

    XProgressBar_setOrientation(pb, XGuiOrientation_Vertical);
    XProgressBar_initStyleOption(pb, &opt);
    XQ_CHECK(!opt.m_horizontal);
    XQ_CHECK((opt.m_state & XStyleState_Horizontal) == 0);

    XClassDelete((XClass*)pb);
}

/** @brief 父子所有权：进度条作为子控件随父析构释放。 */
XQ_TEST(XProgressBar, parentOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XProgressBar* pb = NULL;
    XQ_REQUIRE(host != NULL);
    pb = XProgressBar_create(host, 0);
    XQ_REQUIRE(pb != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)pb) == host);
    XQ_CHECK(XWidget_isAncestorOf(host, (XWidget*)pb));
    XClassDelete((XClass*)host);
}

/* ==================== P0-66：空格式串 ==================== */

/** @brief `setFormat("")` 是**空文本模板**（Qt 的 format()/text() 原样返回
 *         空串，qprogressbar.cpp:424-450/560-573）；`setFormat(NULL)` 仍是
 *         C 侧便利语义「恢复缺省」（保留表 K-24）。 */
XQ_TEST(XProgressBar, emptyFormatIsEmptyText)
{
    XProgressBar* pb = XProgressBar_create(NULL, 0);
    char text[64];
    XQ_REQUIRE(pb != NULL);
    XProgressBar_setRange(pb, 0, 100);
    XProgressBar_setValue(pb, 40);
    XProgressBar_text(pb, text, (int)sizeof(text));
    XQ_EQ_STR(text, "40%");

    /* 空串：可表达、text() 为空（旧实现静默回落到 "%p%" ⇒ "40%"）。 */
    XProgressBar_setFormat(pb, "");
    XQ_EQ_STR(XProgressBar_format(pb), "");
    XProgressBar_text(pb, text, (int)sizeof(text));
    XQ_EQ_STR(text, "");

    /* NULL = 恢复缺省 "%p%"。 */
    XProgressBar_setFormat(pb, NULL);
    XQ_EQ_STR(XProgressBar_format(pb), "%p%");
    XProgressBar_text(pb, text, (int)sizeof(text));
    XQ_EQ_STR(text, "40%");

    /* resetFormat 同缺省。 */
    XProgressBar_setFormat(pb, "%v");
    XQ_EQ_STR(XProgressBar_format(pb), "%v");
    XProgressBar_resetFormat(pb);
    XQ_EQ_STR(XProgressBar_format(pb), "%p%");
    XProgressBar_text(pb, text, (int)sizeof(text));
    XQ_EQ_STR(text, "40%");

    XClassDelete((XClass*)pb);
}

/* ==================== P3-03：text() 虚派发 ==================== */

/** @brief 派生测试类：只覆写 EXProgressBar_Text 槽。 */
typedef struct XPBDerivedText
{
    XProgressBar m_base;
} XPBDerivedText;

XCLASS_DEFINE_BEGING(XPBDerivedText)
XCLASS_DEFINE_EXTEND_END(XPBDerivedText, XProgressBar)

static void xpb_derivedText(const XProgressBar* self, char* out, int outSize)
{
    (void)self;
    if (!out || outSize <= 0) return;
    out[0] = 'V';
    if (outSize > 1) out[1] = '\0';
}

XVtable* XPBDerivedText_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPBDerivedText)
    XVTABLE_INHERIT_XCLASS(XProgressBar);
    XVTABLE_OVERLOAD_DEFAULT(EXProgressBar_Text, xpb_derivedText);
    return XVTABLE_DEFAULT;
}

/** @brief `XProgressBar_text` / `XProgressBar_text_base` 都是多态入口：
 *         派生类覆写槽后两者都返回覆写结果，基类实例仍用默认实现。 */
XQ_TEST(XProgressBar, textIsVirtual)
{
    XProgressBar* pb = XProgressBar_create(NULL, 0);
    XPBDerivedText derived;
    char text[64];
    XQ_REQUIRE(pb != NULL);
    XProgressBar_setRange(pb, 0, 100);
    XProgressBar_setValue(pb, 40);
    XProgressBar_text(pb, text, (int)sizeof(text));
    XQ_EQ_STR(text, "40%");

    XProgressBar_init(&derived.m_base, NULL, 0);
    /* 覆写虚表（XProgressBar_init 把表设成基类表）。 */
    XClassSetVtable(&derived, XPBDerivedText);
    XProgressBar_setValue(&derived.m_base, 70);
    XProgressBar_text(&derived.m_base, text, (int)sizeof(text));
    XQ_EQ_STR(text, "V");
    XProgressBar_text_base(&derived.m_base, text, (int)sizeof(text));
    XQ_EQ_STR(text, "V");

    /* 默认实现仍可经基类实例取回（槽位未丢）。 */
    XProgressBar_text_base(pb, text, (int)sizeof(text));
    XQ_EQ_STR(text, "40%");

    XClassDeinit((XClass*)&derived.m_base);
    XClassDelete((XClass*)pb);
}

/* ==================== P0-65/P0-E9：进度块反向 + busy ==================== */

/** @brief 只画 CE_ProgressBarContents 到 100x20 ARGB32 图，返回探针像素；
 *         highlightCount 非 NULL 时统计该行高亮色像素个数。 */
static uint32_t pb_renderContents(XCommonStyle* style, XStyleOption* opt,
                                  int x, int y, int* highlightCount)
{
    XImage image;
    XPainter painter;
    uint32_t px = 0u;
    XImage_init_ex(&image, 100, 20, XImageFormat_ARGB32);
    XImage_fill(&image, 0u);
    XPainter_init(&painter, NULL);
    if (XPainter_begin_image(&painter, &image)) {
        XStyle_drawControl((XStyle*)style, XStyleCE_ProgressBarContents,
                           opt, &painter, NULL);
        XPainter_end(&painter);
        px = XImage_pixel(&image, x, y);
        if (highlightCount) {
            int i;
            *highlightCount = 0;
            for (i = 0; i < 100; ++i) {
                if (XImage_pixel(&image, i, y) == 0xFFFF0000u)
                    ++(*highlightCount);
            }
        }
    }
    XPainter_deinit(&painter);
    XClassDeinit(&image);
    return px;
}

/** @brief CE_ProgressBarContents：invertedAppearance 反转填充起点（P0-65），
 *         且 min==max==0 时画 4px busy 游动块（P0-E9；旧实现 frac 恒 0、
 *         什么都不画）。颜色用 Base=黑 / Highlight=红 分离。 */
XQ_TEST(XProgressBar, contentsInvertedAndBusy)
{
    XCommonStyle* style = XCommonStyle_create();
    XStyleOption opt;
    XPalette palette;
    XColor base;
    XColor hl;
    uint32_t px;
    int count = 0;
    XQ_REQUIRE(style != NULL);

    palette = XPalette_create();
    base = XColor_create_rgb(0, 0, 0, 255);
    hl = XColor_create_rgb(255, 0, 0, 255);
    XPalette_setColor(&palette, XPaletteColorGroup_Active,
                      XPaletteColorRole_Base, base);
    XPalette_setColor(&palette, XPaletteColorGroup_Active,
                      XPaletteColorRole_Highlight, hl);

    XStyleOption_init(&opt, XStyleCE_ProgressBarContents);
    opt.m_rect = (XRect){0, 0, 100, 20};
    opt.m_horizontal = 1;
    opt.m_state = XStyleState_Enabled;
    opt.m_palette = palette;
    opt.m_progressMin = 0;
    opt.m_progressMax = 100;
    opt.m_progressValue = 50;

    /* 水平、未反向：块在左半（x=1..49）。 */
    px = pb_renderContents(style, &opt, 5, 10, NULL);
    XQ_EQ_I64(px, 0xFFFF0000u);
    px = pb_renderContents(style, &opt, 95, 10, NULL);
    XQ_EQ_I64(px, 0u);

    /* 反向（P0-65）：块移到右半（x=50..98）。 */
    opt.m_progressInverted = 1;
    px = pb_renderContents(style, &opt, 5, 10, NULL);
    XQ_EQ_I64(px, 0u);
    px = pb_renderContents(style, &opt, 95, 10, NULL);
    XQ_EQ_I64(px, 0xFFFF0000u);

    /* 垂直：缺省自下而上（y=10..18），反向自上而下（y=1..9）。 */
    opt.m_horizontal = 0;
    opt.m_progressInverted = 0;
    px = pb_renderContents(style, &opt, 50, 15, NULL);
    XQ_EQ_I64(px, 0xFFFF0000u);
    px = pb_renderContents(style, &opt, 50, 5, NULL);
    XQ_EQ_I64(px, 0u);
    opt.m_progressInverted = 1;
    px = pb_renderContents(style, &opt, 50, 5, NULL);
    XQ_EQ_I64(px, 0xFFFF0000u);
    px = pb_renderContents(style, &opt, 50, 15, NULL);
    XQ_EQ_I64(px, 0u);

    /* busy（P0-E9）：min==max==0、progress=10 ⇒ 4px 游动块在 x=11..14。 */
    opt.m_horizontal = 1;
    opt.m_progressInverted = 0;
    opt.m_progressMin = 0;
    opt.m_progressMax = 0;
    opt.m_progressValue = 10;
    px = pb_renderContents(style, &opt, 12, 10, &count);
    XQ_EQ_I64(px, 0xFFFF0000u);
    XQ_CHECK_MSG(count >= 1 && count <= 8,
                 "busy 应为 4px 游动块而非整条：count=%d", count);
    /* 反向：位置从另一端数（span=98 ⇒ 98-10=88 ⇒ x=89..92）。 */
    opt.m_progressInverted = 1;
    px = pb_renderContents(style, &opt, 90, 10, NULL);
    XQ_EQ_I64(px, 0xFFFF0000u);
    px = pb_renderContents(style, &opt, 12, 10, NULL);
    XQ_EQ_I64(px, 0u);

    XClassDelete((XClass*)style);
}

#endif /* XGUI_ON && XWIDGET_ON && XPROGRESSBAR_ON */
