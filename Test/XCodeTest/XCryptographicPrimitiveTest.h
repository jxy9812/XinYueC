/**
 * @file XCryptographicPrimitiveTest.h
 * @brief XCryptographic 独立密码原语回归测试。
 */

#ifndef XCRYPTOGRAPHICPRIMITIVETEST_H
#define XCRYPTOGRAPHICPRIMITIVETEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XTestMenu.h"
#include <stdbool.h>

typedef struct XTestMenu XTestMenu;

/** @brief 运行 XCryptographic 独立密码原语标准向量测试。 */
bool XCryptographicPrimitiveTest_runAll(void);

#if DEMOTEST
void XTestMenu_XCryptographicPrimitiveTest(XTestMenu* root);
#endif

#ifdef __cplusplus
}
#endif

#endif /* XCRYPTOGRAPHICPRIMITIVETEST_H */
