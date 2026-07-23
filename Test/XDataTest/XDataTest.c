#include"XDataTest.h"
#include"XCharTest.h"
#include"XXmlStreamWriterTest.h"
#include"XMenu.h"
void XMenu_XDataTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XDataTest");
	XMenu_addMenu(root, menu);
	XMenu_XCharTest(menu);
	XMenu_XXmlStreamWriterTest(menu);
}
