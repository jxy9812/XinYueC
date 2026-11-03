/**
 * @file       XDataStreamTest.h
 * @brief      XDataStream 专项测试入口（对标 Qt tst_QDataStream）。
 * @details    覆盖线格式、字节序、浮点精度、流版本差异、长度前缀的
 *             null/扩展标记、事务、以及各类错误路径。实现只依赖
 *             XinYueC 抽象层。
 * @author     XinYueC 团队
 */
#ifndef XDATASTREAMTEST_H
#define XDATASTREAMTEST_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 运行 XDataStream 全部专项测试。
 *
 * @return 失败断言数；0 表示全部通过。
 * @note  失败项以 `[XDS-FAIL] <描述>` 打印到 stderr，便于与既有测试套件
 *        （`[BG-FAIL]`/`[P32-FAIL]` 等）同口径检索。
 */
int XDataStreamTest_runAll(void);

#ifdef __cplusplus
}
#endif

#endif /* XDATASTREAMTEST_H */
