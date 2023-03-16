#include"Test.h"
#include"XHuffmanTree.h"
static void for_each(void* LPVal, void* args)
{
	XPair* pair = *(XPair**)LPVal;
	printf("key:%d count:%d\n", XPair_First(pair, char), XPair_Second(pair,size_t));
}
void XHuffmanTreeTest()
{
	//数据
	char data[] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,3,3,5,8,8,5,78,54,66,66,66};
	size_t count = sizeof(data) / sizeof(data[0]);//数据大小字节
	XHuffmanTree* tree = XHfmTree_init();//创建一个哈夫曼树

	XVector* gzipData=XHfmTree_gzip(tree, data, count);//压缩后的数据
	XMap_iterator_for_each(tree->dictionaries, for_each, NULL);
	XVector* unzipData = XHfmTree_unzip(tree,XVector_begin(gzipData),XVector_size(gzipData));
	printf("\n");
	XMap_iterator_for_each(tree->dictionaries, for_each,NULL);



	XVector_free(gzipData);//释放返回的压缩数据
	XVector_free(unzipData);//释放解压后的压缩数据
	XHfmTree_free(tree);//释放哈夫曼树
}