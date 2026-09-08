/**
 * @file       XProgressBarTest.h
 * @brief      XProgressBar 进度条控件回归测试（XGuiDemo 统一测试入口）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPROGRESSBARTEST_H
#define XPROGRESSBARTEST_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

/** @brief 运行 XProgressBar 全部回归用例。
 *  @return 全部通过返回 true；任一断言失败返回 false。 */
bool XProgressBarTest_runAll(void);

#ifdef __cplusplus
}
#endif
#endif /* XPROGRESSBARTEST_H */
