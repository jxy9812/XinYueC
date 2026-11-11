/**
 * @file       xspinbox_geometry_probe.c
 * @brief      XSpinBox 步进按钮列几何四方口径对照探针（临时诊断工具）。
 * @details    XSpinBox 步进按钮列几何在本库有四处独立实现：
 *              ① 样式几何 XStyle_subControlRect(CC_SpinBox, SC_SpinBoxUp/Down)
 *                 （XCommonStyle.c:3898，Qt 口径）；
 *              ② 底层绘制 xcs_drawSpinBox（XCommonStyle.c:1205，恒 16px）；
 *              ③ QSS 子控件镜像 xsss2_subControlRect（XStyleSheetStyle.c:2946，
 *                 随 ②）；
 *              ④ 命中判定 spinbox_hitButton（XSpinBox.c:420，恒 16px）。
 *              本探针在给定控件宽高下打印四方数值，量化其是否一致；并
 *              探 QSS 代理样式是否把 pixelMetric 转发给源样式（Qt 的
 *              QStyleSheetStyle::pixelMetric 会转发）。
 *              [2026-11-10 增补] sizeHint↔步进列几何闭环段：带后缀
 *              " rpm"、量程 0..1440 的实控件按自身 sizeHint 定几何后，
 *              打印 sizeHint/minimumSizeHint、SC_SpinBoxEditField 实宽、
 *              「最大值文本+后缀」度量——量化「预留 ≥ 实绘」（现场
 *              「三位能显示、四位被裁」的判据）。
 *              用法：xspinbox_geometry_probe [width] [height]
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"
#include <stdio.h>

#if XGUI_ON && XSTYLE_ON && XSPINBOX_ON && XWIDGET_ON
#include "XStyle.h"
#include "XStyleOption.h"
#include "XSpinBox.h"
#include "XWidget.h"
#include "XLineEdit.h"
#include "XPainter.h"
#include "XStyleSheetStyle.h"
#include "XApplication.h"
#include "XFusionStyle.h"

/** @brief 打印某样式对象对 CC_SpinBox 的几何口径。 */
static void dump(const char* tag, XStyle* style, int w, int h)
{
    XStyleOption opt;
    XRect up;
    XRect edit;
    XStyleOption_init(&opt, XStyleCC_SpinBox);
    XRect_init(&opt.m_rect, 0, 0, w, h);
    opt.m_spinFrame = 1;
    opt.m_spinSymbols = 0; /* UpDownArrows */
    opt.m_state = (uint32_t)XStyleState_Enabled;
    up = XStyle_subControlRect(style, XStyleCC_SpinBox, &opt,
                                XStyleSC_SpinBoxUp, NULL);
    edit = XStyle_subControlRect(style, XStyleCC_SpinBox, &opt,
                                 XStyleSC_SpinBoxEditField, NULL);
    printf("  %-14s fw=%d  Up x=%d w=%d   Edit x=%d w=%d (right=%d)\n",
           tag,
           XStyle_pixelMetric(style, XStylePM_SpinBoxFrameWidth, &opt),
           up.x, up.width, edit.x, edit.width, edit.x + edit.width);
}

/** @brief sizeHint↔步进列几何闭环：按自身 sizeHint 定几何，对照
 *         「最大值文本+后缀」度量与编辑区实宽（预留 ≥ 实绘判据）。 */
static void dumpSizeHintLoop(void)
{
    XSpinBox* spin = XSpinBox_create(NULL, 0);
    XSize hint;
    XSize minHint;
    XRect field;
    XRect up;
    XStyleOption opt;
    XFont font;
    int textW;
    if (!spin) { printf("  (create failed)\n"); return; }
    XSpinBox_setRange(spin, 0, 1440);
    XSpinBox_setSuffix(spin, " rpm");
    hint = XWidget_sizeHint((XWidget*)spin);
    minHint = XWidget_minimumSizeHint((XWidget*)spin);
    printf("  sizeHint=%dx%d  minimumSizeHint=%dx%d"
           " (min probe excludes suffix, Qt 口径)\n",
           hint.width, hint.height, minHint.width, minHint.height);
    /* 按建议尺寸定几何（触发 RESIZE → 内嵌编辑框几何同步）。RESIZE 只对
       可见控件当拍派发（不可见挂 PendingResize），故先 show 再定几何。 */
    XWidget_show((XWidget*)spin);
    {
        XRect g;
        g.x = 0; g.y = 0;
        g.width = hint.width > 0 ? hint.width : 80;
        g.height = hint.height > 0 ? hint.height : 24;
        XWidget_setGeometryRect((XWidget*)spin, &g);
    }
    font = XWidget_font((XWidget*)spin);
    textW = XPainter_textWidth(&font, "1440 rpm");
    XClassDeinit(&font);
    XStyleOption_init(&opt, XStyleCC_SpinBox);
    XRect_init(&opt.m_rect, 0, 0, hint.width, hint.height);
    opt.m_spinFrame = 1;
    opt.m_spinSymbols = 0;
    opt.m_state = (uint32_t)XStyleState_Enabled;
    field = XStyle_subControlRect(XStyle_defaultStyle(), XStyleCC_SpinBox,
                                  &opt, XStyleSC_SpinBoxEditField, NULL);
    up = XStyle_subControlRect(XStyle_defaultStyle(), XStyleCC_SpinBox,
                               &opt, XStyleSC_SpinBoxUp, NULL);
    {
        XLineEdit* edit = XSpinBox_lineEdit(spin);
        XRect eg = edit ? XWidget_rect((XWidget*)edit) : field;
        printf("  text(\"1440 rpm\")=%d  EditField(style) x=%d w=%d"
               "  edit(w)=%d  Up w=%d\n",
               textW, field.x, field.width, eg.width, up.width);
        printf("  预留≥实绘: %s  (EditField %d >= text %d + 余量)",
               (field.width >= textW) ? "OK" : "FAIL",
               field.width, textW);
        if (edit)
            printf("   inner-edit %s  (edit %d >= text %d)",
                   (eg.width >= textW) ? "OK" : "FAIL", eg.width, textW);
        printf("\n");
    }
    XClassDelete(spin);
}

int main(int argc, char** argv)
{
    int w = (argc > 1) ? atoi(argv[1]) : 60;
    int h = (argc > 2) ? atoi(argv[2]) : 24;
    XStyle* style;

    printf("widget %dx%d\n", w, h);

    /* 未装样式表：缺省样式（demo 装 Fusion）。 */
    XFusionStyle_installDefault();
    style = XStyle_defaultStyle();
    dump("fusion", style, w, h);

    /* 装样式表后：默认样式变为 QSS 代理（源样式 = 上面的 Fusion）。 */
    XApplication_setStyleSheet_2("XSpinBox { border: 1px solid #5A6572; }");
    style = XStyle_defaultStyle();
    dump("qss-proxy", style, w, h);
    printf("  (qss-proxy is now the default; its fw is what the LAYOUT path sees)\n");

    /* sizeHint↔步进列几何闭环（2026-11-10 增补；在 QSS 代理缺省态下跑，
     * 与 HSF 客户端主题同构：Fusion 源样式 + XSpinBox 描边规则）。 */
    printf("sizeHint loop (range 0..1440, suffix \" rpm\"):\n");
    dumpSizeHintLoop();
    return 0;
}
#else
int main(void) { printf("disabled\n"); return 0; }
#endif
