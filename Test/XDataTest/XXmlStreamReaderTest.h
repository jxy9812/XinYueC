/******************************************************************************
 * @file       XXmlStreamReaderTest.h
 * @brief      XXmlStreamReader XML读取器全面测试头文件
 * @author     XinYueC 团队
 * @note       覆盖所有公开API，包括新增的DTD相关功能
 ******************************************************************************/
#ifndef XXMLSTREAMREADERTEST_H
#define XXMLSTREAMREADERTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include "CXinYueConfig.h"
#include "XClass.h"
#include <stdbool.h>

/* 直接运行全部 Reader 测试，不经过交互式菜单。 */
bool XXmlStreamReaderTest_runAll(void);

#if DEMOTEST
void XMenu_XXmlStreamReaderTest(XMenu* root);
#endif
#ifdef __cplusplus
}
#endif
#endif
