#include"XContainerObject.h"
#if !defined(XLISTNODE_H)&& XList_ON
#define XLISTNODE_H
#ifdef __cplusplus
extern "C" {
#endif
//List的一个节点
typedef struct XListNode
{
	struct XListNode* prev;//指向上一个
	struct XListNode* next;//指向下一个
	void* date;//储存的数据指针
}XListNode;
#ifdef __cplusplus
}
#endif
#endif // !XLISTNODE_H
