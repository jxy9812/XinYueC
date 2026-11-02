/**
 * @file XSettingsTest.h
 * @brief XSettings Qt 6.8 对齐测试声明。
 */
#ifndef XSETTINGSTEST_H
#define XSETTINGSTEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XTestMenu.h"
#include "XClass.h"

#if DEMOTEST_ON && XSETTINGS_ON
/**
 * @brief 将 XSettings Qt 6.8 对齐测试菜单添加到测试根菜单。
 * @param root 测试根菜单对象；不能传入 NULL。
 */
void XTestMenu_XSettingsTest(XTestMenu* root);

/**
 * @brief 运行 XSettings 全量测试（供 Test 命令 `Test xsettings` 自动化调用）。
 * @return 全部通过返回 true；任一失败返回 false。
 */
bool XSettingsTest_runAll(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* XSETTINGSTEST_H */
