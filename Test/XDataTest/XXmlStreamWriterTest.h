#ifndef XXMLSTREAMWRITERTEST_H
#define XXMLSTREAMWRITERTEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XClass.h"
#include <stdbool.h>
#if DEMOTEST
	/* 直接运行全部 Writer 测试，不经过交互式菜单。 */
	bool XXmlStreamWriterTest_runAll(void);

	void XMenu_XXmlStreamWriterTest(XMenu* root);
#endif
#ifdef __cplusplus
}
#endif	
#endif
