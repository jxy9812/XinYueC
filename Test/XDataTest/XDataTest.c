#include"XDataTest.h"
#include"XCharTest.h"
#include"XXmlStreamWriterTest.h"
#include"XXmlStreamReaderTest.h"
#include"XExcelTest.h"
#include"XDomTest.h"
#include"XMenu.h"
void XMenu_XDataTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XDataTest");
	XMenu_addMenu(root, menu);
	XMenu_XCharTest(menu);
	XMenu_XXmlStreamWriterTest(menu);
	XMenu_XXmlStreamReaderTest(menu);
	XMenu_XExcelTest(menu);
	XMenu_XDomTest(menu);
}
