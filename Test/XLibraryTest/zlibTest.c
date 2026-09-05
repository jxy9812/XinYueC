#include"XLibraryTest.h"
#include"XString.h"
#include"zlib.h"
#include"XByteArray.h"
#include"XTestMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include<string.h>
#include"XPrintf.h"
char compress_buff[100] = { 0 };
void zlibTest()
{
    XPrintf_3("zlib 压缩测试\n");
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
	XPrintf_3("zlib XByteArray压缩测试\n");
	const char* str = "aaaaaaaaaaaaaa6666666";
	XByteArray* data = XByteArray_create();
	XByteArray_push_back_2(data, str, strlen(str) + 1);
	XByteArray* compress_buff = XByteArray_toCompress(data);
	XPrintf("压缩后大小:%d\n", XByteArray_size_base(compress_buff));
	XByteArray* decompress_buff = XByteArray_toDecompress(compress_buff);
	XPrintf("解压后大小:%d\t数据:%s\n", XByteArray_size_base(decompress_buff), XByteArray_data(decompress_buff));
	XByteArray_delete_base(data);
	XByteArray_delete_base(compress_buff);
	XByteArray_delete_base(decompress_buff);
	XCoreApplication_quit();
}
void XTestMenu_zlibTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("zlib(zlibTest)");
	XTestMenu_addMenu(root, menu);
	{
		XAction* action = XTestMenu_addAction(menu, "主测试");
		XTestMenu_setActionFunction(action, zlibTest);
	}
	{
		XAction* action = XTestMenu_addAction(menu, "XByteArray");
		XTestMenu_setActionFunction(action, zlibByteArrayTest);
	}
}
