#include "XProgressBarTest.h"
#include "XProgressBar.h"
#include "XMemory.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/** @brief 简单断言：失败打印用例名并置失败标志。 */
static int xp_failures = 0;
static void xp_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[XP-FAIL] %s\n", what ? what : "");
        ++xp_failures;
    }
}

bool XProgressBarTest_runAll(void)
{
    XProgressBar* bar = XProgressBar_create(NULL, 0);
    char text[64];
    if (!bar) { fprintf(stderr, "[XP-FAIL] create\n"); return false; }

    /* 1. 默认值（对标 QProgressBar 默认构造）。 */
    xp_expect(XProgressBar_minimum(bar) == 0 && XProgressBar_maximum(bar) == 100,
              "默认范围 0..100");
    xp_expect(XProgressBar_value(bar) == 0, "默认值 0");
    xp_expect(XProgressBar_orientation(bar) == (int)XProgressBarOrientation_Horizontal,
              "默认水平");
    xp_expect(XProgressBar_isTextVisible(bar), "默认文本可见");
    xp_expect(XProgressBar_textDirection(bar) == (int)XProgressBarDirection_TopToBottom,
              "默认文本方向 TopToBottom");

    /* 2. setValue 钳位 + valueChanged（信号发射经内部 emitInt 路径）。 */
    XProgressBar_setValue(bar, 50);
    xp_expect(XProgressBar_value(bar) == 50, "setValue(50) 生效");
    XProgressBar_setValue(bar, 150); /* 越界钳位到 100 */
    xp_expect(XProgressBar_value(bar) == 100, "越界钳位到 max");
    XProgressBar_setValue(bar, -10);
    xp_expect(XProgressBar_value(bar) == 0, "负值钳位到 min");

    /* 3. setRange：min>max 自动交换 + 值重钳位。 */
    XProgressBar_setRange(bar, 10, 0);
    xp_expect(XProgressBar_minimum(bar) == 0 && XProgressBar_maximum(bar) == 10,
              "setRange 交换 min>max");
    XProgressBar_setRange(bar, 0, 200);
    XProgressBar_setValue(bar, 150);
    xp_expect(XProgressBar_value(bar) == 150, "新范围内值保持");

    /* 3b. setMinimum/setMaximum（对标 Qt 槽：联动另一端端点）。 */
    XProgressBar_setMinimum(bar, 30);
    xp_expect(XProgressBar_minimum(bar) == 30 && XProgressBar_maximum(bar) == 200,
              "setMinimum(30) 下限生效、上限保持");
    XProgressBar_setMaximum(bar, 80);
    xp_expect(XProgressBar_minimum(bar) == 30 && XProgressBar_maximum(bar) == 80,
              "setMaximum(80) 上限生效、下限保持");
    xp_expect(XProgressBar_value(bar) == 80, "setMaximum 后值钳位到新上限");
    XProgressBar_setMinimum(bar, 300); /* 新下限 > 上限：上限抬升 */
    xp_expect(XProgressBar_minimum(bar) == 300 && XProgressBar_maximum(bar) == 300,
              "setMinimum 高于上限时上限抬升");
    XProgressBar_setMaximum(bar, 100); /* 新上限 < 下限：下限下压 */
    xp_expect(XProgressBar_minimum(bar) == 100 && XProgressBar_maximum(bar) == 100,
              "setMaximum 低于下限时下限下压");
    XProgressBar_setRange(bar, 0, 200); /* 恢复标准范围 */
    XProgressBar_setValue(bar, 150);

    /* 4. reset：回到 minimum。 */
    XProgressBar_reset(bar);
    xp_expect(XProgressBar_value(bar) == 0, "reset 回到 minimum");

    /* 5. 方向与翻转。 */
    XProgressBar_setOrientation(bar, (int)XProgressBarOrientation_Vertical);
    xp_expect(XProgressBar_orientation(bar) == (int)XProgressBarOrientation_Vertical,
              "setOrientation 生效");
    XProgressBar_setOrientation(bar, (int)XProgressBarOrientation_Horizontal);
    XProgressBar_setInvertedAppearance(bar, true);
    xp_expect(XProgressBar_invertedAppearance(bar), "setInvertedAppearance 生效");
    XProgressBar_setInvertedAppearance(bar, false);

    /* 5b. 文本方向（textDirection，对标 Qt 枚举值 0/1）。 */
    XProgressBar_setTextDirection(bar, (int)XProgressBarDirection_BottomToTop);
    xp_expect(XProgressBar_textDirection(bar) == (int)XProgressBarDirection_BottomToTop,
              "setTextDirection(BottomToTop) 生效");
    XProgressBar_setTextDirection(bar, 99); /* 非法值忽略 */
    xp_expect(XProgressBar_textDirection(bar) == (int)XProgressBarDirection_BottomToTop,
              "非法文本方向忽略");
    XProgressBar_setTextDirection(bar, (int)XProgressBarDirection_TopToBottom);

    /* 6. 文本可见性 / 对齐 / 格式串。 */
    XProgressBar_setTextVisible(bar, false);
    xp_expect(!XProgressBar_isTextVisible(bar), "setTextVisible(false) 生效");
    XProgressBar_setTextVisible(bar, true);
    XProgressBar_setAlignment(bar, (int)(XAlignment_Left | XAlignment_VCenter));
    xp_expect(XProgressBar_alignment(bar) == (int)(XAlignment_Left | XAlignment_VCenter),
              "setAlignment 生效");
    XProgressBar_setAlignment(bar, (int)(XAlignment_HCenter | XAlignment_VCenter));
    XProgressBar_setFormat(bar, "%v/%m (%p%%)");
    xp_expect(strcmp(XProgressBar_format(bar), "%v/%m (%p%%)") == 0,
              "setFormat 生效");
    XProgressBar_setRange(bar, 0, 100); /* 恢复标准范围再验证格式化 */
    XProgressBar_setValue(bar, 50);
    XProgressBar_text(bar, text, (int)sizeof(text));
    xp_expect(strcmp(text, "50/100 (50%)") == 0, "格式化 %v/%m/%p");
    XProgressBar_setFormat(bar, NULL); /* 恢复默认 %p% */
    XProgressBar_text(bar, text, (int)sizeof(text));
    xp_expect(strcmp(text, "50%") == 0, "默认格式 %p%");

    /* 6b. resetFormat：恢复默认格式串。 */
    XProgressBar_setFormat(bar, "%v/%m");
    xp_expect(strcmp(XProgressBar_format(bar), "%v/%m") == 0,
              "resetFormat 前置：自定义格式生效");
    XProgressBar_resetFormat(bar);
    xp_expect(strcmp(XProgressBar_format(bar), "%p%") == 0,
              "resetFormat 恢复默认 %p%");
    XProgressBar_text(bar, text, (int)sizeof(text));
    xp_expect(strcmp(text, "50%") == 0, "resetFormat 后文本为百分比");


    XProgressBar_delete_base(bar);

    {
        int failures = xp_failures;
        xp_failures = 0;
        if (failures == 0) {
            fprintf(stderr, "XProgressBar test: PASS\n");
            return true;
        }
        fprintf(stderr, "XProgressBar test: %d assertion(s) failed\n", failures);
        return false;
    }
}
