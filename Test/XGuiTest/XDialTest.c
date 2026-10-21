#include "XDialTest.h"
#include "XDial.h"
#include "XMemory.h"
#include "XEvent.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int dl_failures = 0;
static void dl_expect(bool cond, const char* what)
{
    if (!cond) {
        fprintf(stderr, "[DL-FAIL] %s\n", what ? what : "");
        ++dl_failures;
    }
}

bool XDialTest_runAll(void)
{
    XDial* dial = XDial_create(NULL, 0);

    /* 1. 默认状态（继承基类 0..99）。 */
    dl_expect(XDial_minimum(dial) == 0 && XDial_maximum(dial) == 99,
              "默认范围 0..99");
    dl_expect(XDial_value(dial) == 0, "默认值 0");
    dl_expect(XDial_notchTarget(dial) == 3.7, "默认 notchTarget 3.7");
    dl_expect(!XDial_notchesVisible(dial), "默认刻度隐藏");
    dl_expect(!XDial_wrapping(dial), "默认不环绕");

    /* 2. 值/范围（父类转发链）。 */
    XDial_setValue(dial, 50);
    dl_expect(XDial_value(dial) == 50, "setValue(50) 生效");
    XDial_setValue(dial, 200);
    dl_expect(XDial_value(dial) == 99, "越界钳位到 max");
    XDial_setRange(dial, 0, 200);
    XDial_setValue(dial, 150);
    dl_expect(XDial_value(dial) == 150, "新范围内值保持");

    /* 3. 专属属性。 */
    XDial_setNotchTarget(dial, 10.0);
    dl_expect(XDial_notchTarget(dial) == 10.0, "setNotchTarget 生效");
    XDial_setNotchesVisible(dial, true);
    dl_expect(XDial_notchesVisible(dial), "setNotchesVisible 生效");
    XDial_setWrapping(dial, true);
    dl_expect(XDial_wrapping(dial), "setWrapping 生效");
    XDial_setWrapping(dial, false);
    XDial_setNotchesVisible(dial, false);

    /* 4. notchSize（对标 Qt 6.8.3 qdial.cpp:346-363：notchSize=刻度间隔
     *    值，恒为 singleStep 的非零倍数；notchTarget 越小 → 间隔越小 →
     *    刻度数越多。9da82dcc 已按 Qt 将量纲自「刻度数」改为「间隔值」
     *    （刻度数语义对非 0 起点量程全错位），本段随之迁移。 */
    {
        int n1 = XDial_notchSize(dial);
        XDial_setNotchTarget(dial, 1.0);
        dl_expect(XDial_notchSize(dial) < n1,
                  "notchTarget 越小刻度间隔越小（刻度数越多，Qt 间隔量纲）");
        XDial_setNotchTarget(dial, 10.0);
    }

    /* 5. 键盘步进（基类 ProcessKeyEvent 语义经 triggerAction 验证）。 */
    XDial_setRange(dial, 0, 99);
    XDial_setValue(dial, 10);
    XAbstractSlider_triggerAction((XAbstractSlider*)dial,
                                  XAbstractSliderSliderAction_SingleStepAdd);
    dl_expect(XDial_value(dial) == 11, "SingleStepAdd 步进");

    XClassDelete(dial);

    {
        int failures = dl_failures;
        dl_failures = 0;
        if (failures == 0) {
            fprintf(stderr, "XDial test: PASS\n");
            return true;
        }
        fprintf(stderr, "XDial test: %d assertion(s) failed\n", failures);
        return false;
    }
}
