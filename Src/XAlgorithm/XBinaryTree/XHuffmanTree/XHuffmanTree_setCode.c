#include"XHuffmanTree.h"
#include"XSort.h"
#if XMap_ON
//遍历下编码
static printCode(char* ch,void* args)
{
	printf("%d ", *ch);
}
//设置哈夫曼编码
static void setCode(XHfmNode* root,XVector*code)
{
	XHfmNode* curentNode = root;
	XHfmNode* parent = XBTree_GetParent(curentNode);
	char ch = 0;//哈夫曼编码
	//循环找父
	while (parent !=NULL)
	{
		//当前是其父节点的左孩子
		if (curentNode == XBTree_GetLChild(parent))
		{
			ch = 0;
		}
		else
		{
			ch = 1;
		}
		XVector_push_back(code, &ch);
		curentNode = parent;
		parent= XBTree_GetParent(curentNode);
	}
	//将编码逆序
	XReversed(XVector_front(code),XVector_size(code),sizeof(char));
}
//获取哈夫曼编码数组以及对应的节点
static void getCode(XHfmNode** LProot,void* args)
{
	XHfmNodeData* LPdata = &XHfmTree_GetNodeData(*LProot);//根据二叉树节点获取其数据的指针
	if (LPdata->code != NULL)
	{
		//printf("data:%d count:%d\n", LPdata->ch, LPdata->count);
		setCode(*LProot, LPdata->code);
		//XVector_iterator_for_each(LPdata->code, printCode, NULL);
		//printf("\n");
	}
}
void XHfmTree_setCode(XHfmNode* root)
{
	if (ISNULL(root, "传入的根节点不能为NULL"))
		return;
	XVector* nodeList = XBTree_TraversingToXVector(root, XBTreeInorder);
	XVector_iterator_for_each(nodeList, getCode,NULL);
	XVector_free(nodeList);
}
#endif