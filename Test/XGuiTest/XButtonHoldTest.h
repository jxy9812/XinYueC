/**
 * @file       XButtonHoldTest.h
 * @brief      按钮"按住"语义回归（Qt 口径）：长按拖出不提前松手。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XBUTTONHOLDTEST_H
#define XBUTTONHOLDTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>

/** @brief 运行按钮按住语义回归用例。
 *  @return 全部通过返回 true；任一断言失败返回 false。 */
bool XButtonHoldTest_runAll(void);

#ifdef __cplusplus
}
#endif
#endif /* XBUTTONHOLDTEST_H */
