/**
 * @file       HsfDataTable.h
 * @brief      数据域通用表格页骨架：XTableWidget 铺满页根、随窗口拉伸。
 * @details    页根（QWidget 口径容器）+ 垂直布局，表格为唯一子件
 *             （stretch=1 吃满剩余空间），几何全交布局器——窗口/滚动
 *             壳尺寸变化时表格随之伸缩。历史数据/日志/报警三页共用。
 */
#ifndef HSFDATATABLE_H
#define HSFDATATABLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "XBoxLayout.h" /* 守卫宏 XLAYOUT_ON/XLAYOUT_BOX_ON 的定义源头：
                           未先包含则 #if 按未定义=0 裁掉声明 → 调用点
                           隐式声明按 int 截断返回指针（页指针高 32 位
                           符号扩展致 setWidget 解引用崩溃）。 */

#if XWIDGET_ON && XTABLEWIDGET_ON && XLAYOUT_ON && XLAYOUT_BOX_ON

/**
 * @brief      创建表格铺满型数据页（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（堆栈宿主）。
 * @param title 页标题（粗体居中；UTF-8；NULL 显示空行占位）。
 * @param headers 水平表头标签数组（UTF-8；内部直存指针，须静态存活）。
 * @param columnCount 列数（=headers 条目数）。
 * @param rowCount 初始空行数（示例数据由各页自行回填）。
 * @return 页根控件借用指针；失败 NULL。
 */
XWidget* HsfDataTablePage_create(XWidget* parent,
                                 const char* title,
                                 const char* const* headers,
                                 int columnCount, int rowCount);

/**
 * @brief      页自适应（窄屏/竖屏形态；接 HsfUi kHsfPageAdapt 表）。
 * @param page 页根控件（HsfDataTablePage_create 返回值）。
 * @param compact true=紧凑/竖屏形态（页根逻辑宽 < 600）。
 * @details    竖屏收窄页边距 24→12 腾内容宽（表格列多时窄屏尤需）；
 *             表格铺满/拉伸由布局器与框架缺省 Expanding 承担，无需
 *             额外处理。
 */
void HsfDataTablePage_adapt(XWidget* page, bool compact);

#endif /* XWIDGET_ON && XTABLEWIDGET_ON && XLAYOUT_ON && XLAYOUT_BOX_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFDATATABLE_H */
