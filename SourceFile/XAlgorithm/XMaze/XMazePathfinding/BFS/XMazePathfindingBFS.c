#include"XMazePathfindingBFS.h"
#include"XContainerObject.h"
#include"XMazePathfindingObject.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
//创建一个节点
static BFSNode* CreationBFSNode_XPoint(XPoint pos)
{
	BFSNode* node = (BFSNode*)malloc(sizeof(BFSNode));
	if (isObjectNULL(node,"CreationBFSNode"))
		return NULL;
	node->pos = pos;
	node->parent = NULL;
	node->child=XVector_init("BFSNode*",sizeof(BFSNode*));
	return node;
}
static BFSNode* CreationBFSNode(const int x,const int y)
{
	XPoint pos = { x,y };
	return CreationBFSNode_XPoint(pos);
}
//插入孩子
static size_t insertChild(const XVector* maze, BFSNode* node)
{
	XPointStep pos = { node->pos.x,node->pos.y,1 };
	XStack* ChildAll = XStack_init("XPointStep",sizeof(XPointStep));
	Pathfinder(ChildAll,maze, pos);//获取周围能走的点位
	while (!XStack_empty(ChildAll))
	{
		XPointStep* pCurrentPos = (XPointStep*)XStack_top(ChildAll);
		BFSNode* childBFSNode = CreationBFSNode(pCurrentPos->x,pCurrentPos ->y);
		XVector_Push_Back(node->child, &childBFSNode);
		XStack_pop(ChildAll);
	}
	XStack_free(ChildAll);
	return XVector_size(node->child);
}
XVector* XMazePathfindingBFS(const XVector* maze, const XPoint start, const XPoint dest)
{
	BFSNode* root = CreationBFSNode_XPoint(start);//根节点
	XVector* CurrentNode = root->child;//当前节点数组
	XVector* NextNode = NULL;//下一个节点数组
	insertChild(maze,root);
	return NULL;
}