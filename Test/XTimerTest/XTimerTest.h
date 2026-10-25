#ifndef XTIMERTEST_H
#define XTIMERTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include "XTestMenu.h"
#include"XClass.h"
#if DEMOTEST_ON
	void XTestMenu_XTimerTest(XTestMenu* root);
	void XTestMenu_XTimerTimeWheelTest(XTestMenu* root);
	void XTestMenu_XHrTimerTest(XTestMenu* root);
	void XTimerTimeWheelTest();
	/* [两孩子删除句柄身份压测 2026-10-25] 压测菜单入口：把「压测(随机
	 * 增删+句柄回收)」项挂到 XHrTimer 菜单（实现见 XHrTimerTest.c）。 */
	void XTestMenu_XHrTimerStressTest(XTestMenu* root);
	void XHrTimerStressMenuAction(XVariant* data);
#endif // DEMOTEST_ON

/* [两孩子删除句柄身份压测 2026-10-25] 独立入口（无 DEMOTEST 菜单依赖）：
 * 随机增删 + 全量句柄回收的定时器组压力测试。返回失败断言数（0=通过）。 */
int XHrTimerStressTest(void);

#ifdef __cplusplus
}
#endif	
#endif