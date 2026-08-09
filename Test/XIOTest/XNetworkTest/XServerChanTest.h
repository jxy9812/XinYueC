/**
 * @file       XServerChanTest.h
 * @brief      Server酱客户端本地 HTTP 行为和真实发送测试注册。
 */

#ifndef XSERVERCHANTEST_H
#define XSERVERCHANTEST_H

#include "XMenu.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * - @brief 将 Server酱客户端测试注册到网络测试菜单。
 * - @param root 父菜单；不能为 NULL。
 * - @return 无。
 */
void XServerChanTest_registerAll(XMenu* root);

#ifdef __cplusplus
}
#endif

#endif /* XSERVERCHANTEST_H */
