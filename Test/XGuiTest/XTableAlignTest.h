/**
 * @file       XTableAlignTest.h
 * @brief      XTableView / XTableWidget 对齐专项测试入口。
 * @details    覆盖本轮对齐引入的三类回归面：
 *             ① copy/move 深拷贝与泄漏（逐行行高表曾漏接，move 曾漏释放
 *                目标旧数组）；
 *             ② 边界矩阵（空模型 / 删光 / 越界 / 无效索引）；
 *             ③ 多组合参数化（span / 隐藏 / 排序 / 省略模式）。
 *             实现只依赖 XinYueC 抽象层。
 * @author     XinYueC 团队
 */
#ifndef XTABLEALIGNTEST_H
#define XTABLEALIGNTEST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 运行表格对齐专项测试。
 * @return 失败断言数；0 表示全部通过（失败项以 `[TBL-FAIL]` 打印）。
 */
int XTableAlignTest_runAll(void);

#ifdef __cplusplus
}
#endif

#endif /* XTABLEALIGNTEST_H */
