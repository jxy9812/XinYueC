#include"XMazePathfindingBFS.h"
#include"XContainerObject.h"
#include"XMazePathfindingObject.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#define GetXPoint(nodes) (*(XPoint*)nodes->values)
//创建一个节点
static XBTreeNode* CreationBFSNode_XPoint(XPoint pos)
{
	XBTreeNode* nodes = XBTree_creationInsertData(&pos,1,sizeof(XPoint));
	if (isNULL(isNULLInfo(nodes, "")))
		return NULL;
	return nodes;
}
//创建一个节点
static XBTreeNode* CreationBFSNode(const int x,const int y)
{
	XPoint pos = { x,y };
	return CreationBFSNode_XPoint(pos);
}
//获取迷宫路径
static XVector* GetXMazePath(const XBTreeNode* child)
{
	XVector* Path = XVector_init(sizeof(XPoint));
	XBTreeNode* current = child;
	while (current!=NULL)
	{
		XVector_push_front(Path, current->values);
		current = *XBTree_GetTreeNode(current,XBTreeParent);
	}
	return Path;
}
//插入孩子
static size_t insertChild(const XVector* maze, XBTreeNode* nodes, XVector* NextNodeArray)
{
	int* pMazePos = (int*)XVectorTwo_at_XPoint(maze, GetXPoint(nodes));
	if (*pMazePos != XMazeRoute)
		return 0;
	else
		*pMazePos = XMazePath;//标记走过了
	XPointStep pos = { GetXPoint(nodes).x,GetXPoint(nodes).y,1};
	XStack* ChildAll = XStack_init("XPointStep",sizeof(XPointStep));
	Pathfinder(ChildAll,maze, pos);//获取周围能走的点位
	while (!XStack_empty(ChildAll))
	{
		XPointStep* pCurrentPos = (XPointStep*)XStack_top(ChildAll);
		XBTreeNode* childBFSNode = CreationBFSNode(pCurrentPos->x,pCurrentPos ->y);
		*XBTree_GetTreeNode(childBFSNode, XBTreeParent) = nodes;
		XVector_push_back(nodes->nodes, &childBFSNode);
		XVector_push_back(NextNodeArray, &childBFSNode);
		XStack_pop(ChildAll);
	}
	XStack_free(ChildAll);
	return XVector_size(nodes->nodes)>1? XVector_size(nodes->nodes) -1:0;
}

XVector* XMazePathfindingBFS(const XVector* maze, const XPoint start, const XPoint dest)
{
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	XBTreeNode* root = CreationBFSNode_XPoint(start);//根节点
	XVector* CurrentNodeArray = XVector_init( sizeof(XBTreeNode*));//当前节点数组
	XVector* NextNodeArray = XVector_init(sizeof(XBTreeNode*));//下一个节点数组
	XVector_push_back(CurrentNodeArray,&root);//入根节点

	XBTreeNode* CurrentNode = NULL;//当前遍历的节点
	bool isFindEnd = false;//找到终点标记
	while (!isFindEnd&&!XVector_empty(CurrentNodeArray))
	{
		for (XVector_iterator* it=XVector_begin(CurrentNodeArray);it!= XVector_end(CurrentNodeArray); it= XVector_iterator_add(CurrentNodeArray,it))
		{
			CurrentNode = *(XBTreeNode**)it;
			if (GetXPoint(CurrentNode).x == dest.x && GetXPoint(CurrentNode).y == dest.y)//判断是否到终点了
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
	XBTree_freeNodeAll(root);
	return Path;
}