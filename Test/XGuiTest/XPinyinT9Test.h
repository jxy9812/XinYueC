/**
 * @file       XPinyinT9Test.h
 * @brief      九键（T9）消歧拼音输入回归测试（XGuiDemo 统一测试入口）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPINYINT9TEST_H
#define XPINYINT9TEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>

/** @brief 运行九键（T9）拼音输入全部回归用例（XPinyinEngine 数字通道
 *         + XPinyinTable 数字组查询；纯模块级，无控件依赖）。
 *  @return 全部通过返回 true；任一断言失败返回 false。
 *  @note   XKEYBOARD_IME_ON=0 时无断言可跑，恒返回 true（零回归语义）。 */
bool XPinyinT9Test_runAll(void);

#ifdef __cplusplus
}
#endif
#endif /* XPINYINT9TEST_H */
