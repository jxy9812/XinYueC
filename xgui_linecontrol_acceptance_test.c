/**
 * @file       xgui_linecontrol_acceptance_test.c
 * @brief      XLineControl 控制器验收测试入口（纯逻辑，无窗口依赖）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGuiTest/XLineControlAcceptance.h"
#include <stdio.h>

int main(void)
{
    int failures = 0;
    if (!XLineControlAcceptance_runAll()) ++failures;
    if (failures != 0) {
        fprintf(stderr, "%d acceptance suite(s) failed\n", failures);
        return 1;
    }
    puts("XLineControl acceptance tests passed");
    return 0;
}
