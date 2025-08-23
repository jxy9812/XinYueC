#include"XLibraryTest.h"
#include"XString.h"
#include"zlib.h"
#include"XVector.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include<string.h>
void zlibTest()
{
    XPrint_utf8("zlib分块压缩测试\n");
	const char* str = "aaaaaaaaaaaaaa6666666";
	char compress_buff[100] = {0};
	char decompress_buff[100] = { 0 };
	int out_len=zlib_compress(str,strlen(str)+1,compress_buff,sizeof(compress_buff));
	XPrint_utf8_fmt("压缩后大小:%d\n",out_len);
	out_len = zlib_decompress(compress_buff, out_len, decompress_buff, sizeof(decompress_buff));
	XPrint_utf8_fmt("解压后大小:%d\t数据:%s\n", out_len, decompress_buff);
	XCoreApplication_requestQuit();
}
void XMenu_zlibTest(XMenu* root)
{
	XMenu* menu = XMenu_create("zlib(zlibTest)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, zlibTest);
	}
}