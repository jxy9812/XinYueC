/**
 * @file       XFontFtTest.h
 * @brief      XFontFt（FT 文件后端）专项测试入口声明。
 * @details    覆盖设计 §8 P2.2 门禁用例：px 闭合公式（upem 两档）、
 *             缺字形 false、家族槽满回退、损坏字体家族锁存、
 *             blob 先 Face 后 Free 序。
 *             [已移除 2026-10-07] 原 env XFONT_PROVIDER 运行档位分叉
 *             （缺省 xfo1 档跳过 face 用例）随 XFO1 实现删除——FT 为
 *             唯一轮廓字实现，用例无条件全跑。
 */
#ifndef XFontFtTest_H
#define XFontFtTest_H
#ifdef __cplusplus
extern "C" {
#endif
#include "CXinYueConfig.h"
#include "XTestMenu.h"
#include <stdbool.h>
/** @brief 执行 XFontFt 专项测试。 @return 成功返回 0，失败返回非 0。 */
int XFontFtTest_run(void);
#if DEMOTEST
	void XTestMenu_XFontFtTest(XTestMenu* root);
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif	
#endif
