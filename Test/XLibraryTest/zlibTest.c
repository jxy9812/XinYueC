#include"XLibraryTest.h"
#include"XString.h"
#include"zlib.h"
#include"XByteArray.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include<string.h>
#include"XPrintf.h"
char compress_buff[100] = { 0 };
void zlibTest()
{
    XPrintf_utf8("zlib 压缩测试\n");
	const char* str = "aaaaaaaaaaaaaa6666666";

	char decompress_buff[100] = { 0 };
	int out_len=zlib_compress(str,strlen(str)+1,compress_buff,sizeof(compress_buff));
	XPrintf("压缩后大小:%d\n",out_len);
	out_len = zlib_decompress(compress_buff, out_len, decompress_buff, sizeof(decompress_buff));
	XPrintf("解压后大小:%d\t数据:%s\n", out_len, decompress_buff);
	XCoreApplication_quit();
}
void zlibByteArrayTest()
{
	XPrintf_utf8("zlib XByteArray压缩测试\n");
	const char* str = "aaaaaaaaaaaaaa6666666";
	XByteArray* data = XByteArray_create();
	XByteArray_append_array_base(data, str, strlen(str) + 1);
	XByteArray* compress_buff = XByteArray_toCompress(data);
	XPrintf("压缩后大小:%d\n", XByteArray_size_base(compress_buff));
	XByteArray* decompress_buff = XByteArray_toDecompress(compress_buff);
	XPrintf("解压后大小:%d\t数据:%s\n", XByteArray_size_base(decompress_buff), XContainerDataPtr(decompress_buff));
	XByteArray_delete_base(data);
	XByteArray_delete_base(compress_buff);
	XByteArray_delete_base(decompress_buff);
	XCoreApplication_quit();
}
void XMenu_zlibTest(XMenu* root)
{
	XMenu* menu = XMenu_create("zlib(zlibTest)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, zlibTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "XByteArray");
		XAction_setAction(action, zlibByteArrayTest);
	}
}