#include"XDataStructTest.h"
#if DEMOTEST
#include"XHuffmanTree.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#if XMap_ON
static void for_each(void* LPVal, void* args)
{
	XPair* pair = *(XPair**)LPVal;
	XPrintf("key:%d count:%d\n", XPair_First(pair, char), XPair_Second(pair, size_t));
}
//遍历解压后的数据
static void UnZipFor(char* ch, void* args)
{
	XPrintf("%d ", *ch);
}
#endif // XMap_ON


void XHuffmanTreeTest()
{
#if XMap_ON&&XVector_ON
	//数据
	char data[] = {1,1,1,3,3,5,8,8,5,78,54,66,66,66,1,123,32,3,3,3};
	size_t count = sizeof(data) / sizeof(data[0]);//数据大小字节
	XHuffmanTree* tree = XHfmTree_init();//创建一个哈夫曼树

	XVector* gzipData=XHfmTree_gzip(tree, data, count);//压缩后的数据
	//遍历压缩后的数据
	//XVector_iterator_for_each(gzipData, UnZipFor, NULL); XPrintf("\n");
	//遍历压缩数据后产生的字典
	XMap_iterator_for_each(tree->dictionaries, for_each, NULL); XPrintf("\n");
	XVector* unzipData = XHfmTree_unzip(tree, XContainerDataPtr(gzipData),XVector_size_base(gzipData));//获得解压后的数据
	
	//遍历读取到的压缩数据中的字典
	XMap_iterator_for_each(tree->dictionaries, for_each,NULL);XPrintf("\n");
	//遍历解压后的数据
	XVector_iterator_for_each(unzipData, UnZipFor, NULL); XPrintf("\n");
	


	XVector_delete_base(gzipData);//释放返回的压缩数据
	XVector_delete_base(unzipData);//释放解压后的压缩数据
	XHfmTree_delete(tree);//释放哈夫曼树


#endif
	XCoreApplication_quit();
}
void XMenu_XHuffmanTreeTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XHuffmanTree(哈夫曼树)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XHuffmanTreeTest);
	}
}
#endif