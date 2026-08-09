/**
 * @file       XDataTest.h
 * @brief      XData 模块测试入口声明。
 * @details    仅包含测试调度接口；DEMOTEST 菜单接口由测试可执行程序使用，
 *             不属于库的运行时公共 API。
 */
#ifndef XDataTest_H
#define XDataTest_H
#ifdef __cplusplus
extern "C" {
#endif
#include "CXinYueConfig.h"
#include "XClass.h"
/** @brief 执行 JSON 与 Qt 行为对齐测试。 @return 成功返回 0，失败返回非 0。 */
int XJsonQtAlignmentTest(void);
/** @brief 执行 SQL 公共 API 回归测试。 @return 成功返回 0，失败返回非 0。 */
int XSqlTest_run(void);
#if DEMOTEST
	void XMenu_XJsonQtAlignmentTest(XMenu* root);
	void XMenu_XDataTest(XMenu* root);
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif
