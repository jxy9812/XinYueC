#include"XMemoryTest.h"
#include"XMenu.h"
void XMenu_XMemoryTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XMemory");
	XMenu_addMenu(root, menu);
	XMenu_XMultiPoolTest(menu);
	XMenu_XVariablePoolTest(menu);
}
