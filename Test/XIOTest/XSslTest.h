/**
 * @file XSslTest.h
 * @brief XSsl、XSslSocket 与 mbedTLS XCryptographic 后端回归测试。
 */

#ifndef XSSLTEST_H
#define XSSLTEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XTestMenu.h"
#include <stdbool.h>

typedef struct XTestMenu XTestMenu;

/** @brief 运行离线 XSsl、XSslSocket 与 PSA AEAD 回归测试。 */
bool XSslTest_runAll(void);

#if DEMOTEST
void XTestMenu_XSslTest(XTestMenu* root);
#endif

#ifdef __cplusplus
}
#endif

#endif /* XSSLTEST_H */
