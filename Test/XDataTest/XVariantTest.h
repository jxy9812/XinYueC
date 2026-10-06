#ifndef XVariantTest_H
#define XVariantTest_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include "XTestMenu.h"
#include"XClass.h"
#include <stdbool.h>
/** @brief 执行 XVariant 生命周期/未清零栈结构防呆专项测试。 @return 成功返回 0，失败返回非 0。 */
int XVariantTest_run(void);
#if DEMOTEST
	void XTestMenu_XVariantTest(XTestMenu* root);
#endif // DEMOTEST

#ifdef __cplusplus
}
#endif
#endif
