#include"XMemoryTest.h"
#include"XTestMenu.h"
void XTestMenu_XMemoryTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XMemory");
	XTestMenu_addMenu(root, menu);
	XTestMenu_XMultiPoolTest(menu);
	XTestMenu_XVariablePoolTest(menu);
}
