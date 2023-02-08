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
	if (isNULL(isNULLInfo(node, "")))
		return NULL;
	node->pos = pos;
	node->parent = NULL;
	node->child=XVector_init("BFSNode*",sizeof(BFSNode*));
	return node;
}
//创建一个节点
static BFSNode* CreationBFSNode(const int x,const int y)
{
	XPoint pos = { x,y };
	return CreationBFSNode_XPoint(pos);
}
//获取迷宫路径
static XVector* GetXMazePath(const BFSNode* child)
{
	XVector* Path = XVector_init("XPoint*",sizeof(XPoint));
	BFSNode* current = child;
	while (current!=NULL)
	{
		XVector_push_front(Path, &current->pos);
		current = current->parent;
	}
	return Path;
}
//插入孩子
static size_t insertChild(const XVector* maze, BFSNode* node, XVector* NextNodeArray)
{
	int* pMazePos = (int*)XVectorTwo_at_XPoint(maze, node->pos);
	if (*pMazePos != XMazeRoute)
		return 0;
	else
		*pMazePos = XMazePath;//标记走过了
	XPointStep pos = { node->pos.x,node->pos.y,1 };
	XStack* ChildAll = XStack_init("XPointStep",sizeof(XPointStep));
	Pathfinder(ChildAll,maze, pos);//获取周围能走的点位
	while (!XStack_empty(ChildAll))
	{
		XPointStep* pCurrentPos = (XPointStep*)XStack_top(ChildAll);
		BFSNode* childBFSNode = CreationBFSNode(pCurrentPos->x,pCurrentPos ->y);
		childBFSNode->parent = node;
		XVector_push_back(node->child, &childBFSNode);
		XVector_push_back(NextNodeArray, &childBFSNode);
		XStack_pop(ChildAll);
	}
	XStack_free(ChildAll);
	return XVector_size(node->child);
}
//释放树节点
static void XBinaryTreeObject_freeNode(BFSNode* root)
{
	XStack* stack=XStack_init("BFSNode*",sizeof(BFSNode*));
	XStack_Push(stack,&root);
	while (!XStack_empty(stack))
	{
		BFSNode* current = *(BFSNode**)XStack_top(stack);
		XStack_pop(stack);
		for (XVector_iterator* it = XVector_begin(current->child); it != XVector_end(current->child); it = XVector_iterator_add(current->child, it))
		{
			XStack_Push(stack,it);
		}
		XVector_free(current->child);
		free(current);
	}
}
XVector* XMazePathfindingBFS(const XVector* maze, const XPoint start, const XPoint dest)
{
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	BFSNode* root = CreationBFSNode_XPoint(start);//根节点
	XVector* CurrentNodeArray = XVector_init("BFSNode*", sizeof(BFSNode*));//当前节点数组
	XVector* NextNodeArray = XVector_init("BFSNode*", sizeof(BFSNode*));//下一个节点数组
	XVector_push_back(CurrentNodeArray,&root);//入根节点

	BFSNode* CurrentNode = NULL;//当前遍历的节点
	bool isFindEnd = false;//找到终点标记
	while (!isFindEnd&&!XVector_empty(CurrentNodeArray))
	{
		for (XVector_iterator* it=XVector_begin(CurrentNodeArray);it!= XVector_end(CurrentNodeArray); it= XVector_iterator_add(CurrentNodeArray,it))
		{
			CurrentNode = *(BFSNode**)it;
			if (CurrentNode->pos.x== dest.x&& CurrentNode->pos.y == dest.y)//判断是否到终点了
			{
				isFindEnd = true;
				break;
			}
			size_t n=insertChild(tempMaze, CurrentNode, NextNodeArray);
		}
		XVector_clear(CurrentNodeArray);
		XVector_swap(CurrentNodeArray, NextNodeArray);
	}
	XVector* Path = GetXMazePath(CurrentNode);
	XVector_free(CurrentNodeArray);
	XVector_free(NextNodeArray);
	XVectorTwo_free(tempMaze);
	XBinaryTreeObject_freeNode(root);
	return Path;
}