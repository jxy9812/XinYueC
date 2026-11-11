#include "XScrollBarTest.h"
#include "XScrollBar.h"
#if XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON
#include "XMemory.h"
#include "XEvent.h"
#include "XWidget_Protected.h"
#include "XMenu.h"
#include "XGuiApplication.h"
#include "XVector.h"
#if XSTYLE_ON
#include "XStyle.h"       /* PM_ScrollBarExtent/SliderMin（sizeHint 公式度量） */
#include "XStyleOption.h"
#endif
#include <stdio.h>
#include <string.h>

static int sb_failures = 0;

static void sb_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[SB-FAIL] %s\n", what ? what : "");
        ++sb_failures;
    }
}

static int sb_valueChangedCount = 0;
static void sb_valueChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++sb_valueChangedCount;
}

bool XScrollBarTest_runAll(void)
{
    char argv0[] = "xscrollbar_test";
    char* argv[] = { argv0, NULL };
    XGuiApplication* app = NULL;
    XScrollBar* sb = NULL;
    /* 菜单/调色板路径依赖 GUI 应用单例。 */
    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 1, argv);
    sb = app ? XScrollBar_create(NULL, 0) : NULL;

    /* 1. 默认状态（对标文档：Vertical、0..99、step 1/10、value 0）。 */
    sb_expect(sb != NULL, "XScrollBar 创建");
    sb_expect(XScrollBar_orientation(sb) ==
              (int)XAbstractSliderOrientation_Vertical, "默认垂直");
    sb_expect(XScrollBar_minimum(sb) == 0 && XScrollBar_maximum(sb) == 99,
              "默认范围 0..99");
    sb_expect(XScrollBar_singleStep(sb) == 1, "默认 singleStep 1");
    sb_expect(XScrollBar_pageStep(sb) == 10, "默认 pageStep 10");
    sb_expect(XScrollBar_value(sb) == 0, "默认值 0");
    {
        /* Qt 6.8 QScrollBar::sizeHint（qscrollbar.cpp:411-425）：
         * 竖直 = PM_ScrollBarExtent × (2*PM_ScrollBarExtent +
         * PM_ScrollBarSliderMin)。度量随当前样式：XCommonStyle 16/9 ⇒
         * 16x41；Fusion 14/26 ⇒ 14x54（水平则横竖互换）。本用例可能跑在
         * 装了 Fusion 的宿主里（xgui_regression_test 在控件族之前调
         * XFusionStyle_installDefault），故按当前样式度量算期望，不写死
         * 某一风格的数值。minimumSizeHint 不覆写，基类给 (-1,-1)。 */
        int extent = 16;    /* 无样式层兜底 = XSCROLLBAR_BUTTON_W */
        int sliderMin = 16; /* 无样式层兜底 = XSCROLLBAR_SLIDER_MIN */
#if XSTYLE_ON
        {
            XStyle* st = XWidget_style((const XWidget*)sb);
            if (st) {
                int m = XStyle_pixelMetric(st, XStylePM_ScrollBarExtent,
                                           NULL);
                if (m > 0) extent = m;
                m = XStyle_pixelMetric(st, XStylePM_ScrollBarSliderMin, NULL);
                if (m > 0) sliderMin = m;
            }
        }
#endif
        {
            XSize hint = XScrollBar_sizeHint(sb);
            sb_expect(hint.width == extent &&
                      hint.height == extent * 2 + sliderMin,
                      "sizeHint=extent x (2*extent+sliderMin)（竖直，Qt 公式）");
        }
    }

    /* 2. 方向构造重载与切换。 */
    {
        XScrollBar* h = XScrollBar_create_2(
            (int)XAbstractSliderOrientation_Horizontal, NULL, 0);
        sb_expect(h != NULL && XScrollBar_orientation(h) ==
                  (int)XAbstractSliderOrientation_Horizontal,
                  "create_2 指定水平");
        XScrollBar_setOrientation(h,
            (int)XAbstractSliderOrientation_Vertical);
        sb_expect(XScrollBar_orientation(h) ==
                  (int)XAbstractSliderOrientation_Vertical,
                  "setOrientation 切换");
        XClassDelete(h);
    }

    /* 3. 标准右键菜单条目（对标 contextMenuEvent：8 动作 + 3 分隔）。 */
    {
        XMenu* menu = XScrollBar_createStandardContextMenu(sb);
        sb_expect(menu != NULL, "标准右键菜单创建");
        sb_expect(XMenu_actions(menu) != NULL &&
                  XVector_size_base(XMenu_actions(menu)) == 10,
                  "右键菜单 10 个条目（7 动作 + 3 分隔）");
        XClassDelete(menu);
    }

    /* 4. valueChanged 信号：值变化触发一次，同值不触发。 */
    {
        sb_valueChangedCount = 0;
        XObject_connect_2((XObject*)sb,
            (size_t)XScrollBar_valueChanged_signal(sb, 0), sb_valueChangedSlot);
        XScrollBar_setValue(sb, 50);
        sb_expect(sb_valueChangedCount == 1, "setValue(50) 触发一次");
        XScrollBar_setValue(sb, 50);
        sb_expect(sb_valueChangedCount == 1, "同值不重复触发");
    }

    /* 5. 轨道翻页动作（基类 PageStep 语义）。 */
    XAbstractSlider_triggerAction((XAbstractSlider*)sb,
                                  XAbstractSliderSliderAction_PageStepAdd);
    sb_expect(XScrollBar_value(sb) == 60, "PageStepAdd 50→60");
    XAbstractSlider_triggerAction((XAbstractSlider*)sb,
                                  XAbstractSliderSliderAction_PageStepSub);
    sb_expect(XScrollBar_value(sb) == 50, "PageStepSub 60→50");

    XClassDelete(sb);
    if (app) XClassDelete(app);

    {
        int failures = sb_failures;
        sb_failures = 0;
        if (failures == 0) {
            fprintf(stderr, "XScrollBar test: PASS\n");
            return true;
        }
        fprintf(stderr, "XScrollBar test: %d assertion(s) failed\n",
                failures);
        return false;
    }
}
#else
bool XScrollBarTest_runAll(void)
{
    fprintf(stderr, "XScrollBarTest: skipped (module off)\n");
    return true;
}
#endif /* XWIDGET_ON && XABSTRACTSLIDER_ON && XSCROLLBAR_ON */
