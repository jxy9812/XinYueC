/******************************************************************************
 * @file       XExcelTest.h
 * @brief      XExcel 全面测试头文件（对标 QXlsx 全部模块）
 * @author     XinYueC 团队
 * @note       中文输出，覆盖 XExcel 所有模块的公开 API，统计分配/释放计数并
 *             检查内存泄露。所有 XMap_create_ex 均使用 sizeof(...) 而非写死 64。
 ******************************************************************************/
#ifndef XEXCELTEST_H
#define XEXCELTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include "CXinYueConfig.h"
#include "XClass.h"
#if DEMOTEST
	void XMenu_XExcelTest(XMenu* root);
#endif
#ifdef __cplusplus
}
#endif
#endif
