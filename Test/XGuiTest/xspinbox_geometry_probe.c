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
    return 0;
}
#else
int main(void) { printf("disabled\n"); return 0; }
#endif
