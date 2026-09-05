/**
 * @file       XHttpTest.h
 * @brief      HTTP 请求、头部和响应解析行为测试注册。
 */

#ifndef XHTTPTEST_H
#define XHTTPTEST_H

#include "XTestMenu.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 将 HTTP 本地行为测试注册到网络测试菜单。
 * @param root 父菜单；不能为 NULL。
 * @return 无。
 */
void XHttpTest_registerAll(XTestMenu* root);

#ifdef __cplusplus
}
#endif

#endif /* XHTTPTEST_H */
