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
	XHfmTree_clear(tree);//哈夫曼树清空测试
	XHfmTree_readData(tree, data, count);//读取数据构建哈夫曼树
	//使用XMap迭代器遍历哈夫曼的字典
	//XMap_iterator_for_each(tree->dictionaries, for_each,NULL);
	XVector* compressData=XHfmTree_compress(tree, data, count);//压缩后的数据

	XVector_free(compressData);//释放返回的压缩数据
	XHfmTree_free(tree);//释放哈夫曼树
}