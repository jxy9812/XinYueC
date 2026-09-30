/**
 * @file       XKeyboardTest.h
 * @brief      XVirtualKeyboard 屏幕虚拟键盘控件回归测试（XGuiDemo 统一测试入口）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XKEYBOARDTEST_H
#define XKEYBOARDTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>

/** @brief 运行 XVirtualKeyboard 全部回归用例。
 *  @return 全部通过返回 true；任一断言失败返回 false。
 *  @note   XKEYBOARD_ON=0 时无断言可跑，恒返回 true（零回归语义）。 */
bool XKeyboardTest_runAll(void);

#ifdef __cplusplus
}
#endif
#endif /* XKEYBOARDTEST_H */
