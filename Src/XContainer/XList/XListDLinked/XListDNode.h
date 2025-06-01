#include"XDataStructConfig.h"
#if !defined(XLISTDNODE_H)&& XListDLinked_ON
#define XLISTDNODE_H
#ifdef __cplusplus
extern "C" {
#endif
//List的一个节点
typedef struct XListDNode
{
	struct XListDNode* prev;//指向上一个
	struct XListDNode* next;//指向下一个
	void* data;//储存的数据指针
}XListDNode;
#ifdef __cplusplus
}
#endif
#endif // !XLISTNODE_H
