/**
 * @file       XDomTest.h
 * @brief      XDom XML DOM 模块回归测试声明。
 * @details    覆盖 Qt 6.8 对齐的节点树、工厂、列表、属性、字符数据、DTD 和解析行为。
 */
#ifndef XDOMTEST_H
#define XDOMTEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XTestMenu.h"
#include "XClass.h"
#include <stdbool.h>

/**
 * @brief      运行全部 XDom 回归测试。
 * @return     所有测试通过返回 true；任意断言失败返回 false。
 * @note       测试只使用 XinYueC DOM 和 XML Reader 抽象，不调用平台 API。
 */
bool XDomTest_runAll(void);

#if DEMOTEST
/**
 * @brief      将 XDom 测试注册到 XDataTest 菜单。
 * @param      root 根菜单；函数只借用该菜单，不负责释放。
 * @return     无。
 */
void XTestMenu_XDomTest(XTestMenu* root);
#endif

#ifdef __cplusplus
}
#endif

#endif
