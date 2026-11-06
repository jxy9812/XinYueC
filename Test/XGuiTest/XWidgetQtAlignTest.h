/**
 * @file       XWidgetQtAlignTest.h
 * @brief      XWidget × Qt 6.8.3 对齐专项回归（XGuiDemo 统一测试入口）。
 * @details    覆盖本轮 XWidget 对齐战役新增/修正的语义面（增量套件，
 *             不与 test_widget_contract 的通用断言重复），按 Qt 语义
 *             分七组：几何与尺寸约束 / 可见性与窗口状态 / 焦点与可用性 /
 *             控件树与坐标映射 / 绘制闭环 / 外观与动作 / 命名契约
 *             （_const 借用、_utf8 后缀、_2 重载）。断言逐条标注对应
 *             Qt 6.8.3 源码位置，详见 docs/xgui/XWidget_Qt683_对齐测试说明.md。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XWIDGETQTALIGNTEST_H
#define XWIDGETQTALIGNTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>

/** @brief 运行 XWidget × Qt 6.8.3 对齐专项全部回归用例。
 *  @return 全部通过返回 true；任一断言失败返回 false。 */
bool XWidgetQtAlignTest_runAll(void);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGETQTALIGNTEST_H */
