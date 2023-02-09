#include"XMazePathfindingBFS.h"
#include"XContainerObject.h"
#include"XMazePathfindingObject.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
//创建一个节点
static XBinaryTreeNode* CreationBFSNode_XPoint(XPoint pos)
{
	XBinaryTreeNode* node = XBinaryTreeObject_creationInsertData(&pos,1,sizeof(XPoint));
	if (isNULL(isNULLInfo(node, "")))
		return NULL;
	return node;
}
//创建一个节点
static XBinaryTreeNode* CreationBFSNode(const int x,const int y)
{
	XPoint pos = { x,y };
	return CreationBFSNode_XPoint(pos);
}
//获取迷宫路径
static XVector* GetXMazePath(const XBinaryTreeNode* child)
{
	XVector* Path = XVector_init("XPoint*",sizeof(XPoint));
	XBinaryTreeNode* current = child;
	while (current!=NULL)
	{
		XVector_push_front(Path, current->data);
		current = *XBinaryTreeObject_GetTreeNode(current,XBTreeParent);
	}
	return Path;
}
//插入孩子
static size_t insertChild(const XVector* maze, XBinaryTreeNode* node, XVector* NextNodeArray)
{
	int* pMazePos = (int*)XVectorTwo_at_XPoint(maze, *(XPoint*)node->data);
	if (*pMazePos != XMazeRoute)
		return 0;
	else
		*pMazePos = XMazePath;//标记走过了
	XPointStep pos = { (* (XPoint*)node->data).x,(*(XPoint*)node->data).y,1};
	XStack* ChildAll = XStack_init("XPointStep",sizeof(XPointStep));
	Pathfinder(ChildAll,maze, pos);//获取周围能走的点位
	while (!XStack_empty(ChildAll))
	{
		XPointStep* pCurrentPos = (XPointStep*)XStack_top(ChildAll);
		XBinaryTreeNode* childBFSNode = CreationBFSNode(pCurrentPos->x,pCurrentPos ->y);
		*XBinaryTreeObject_GetTreeNode(childBFSNode, XBTreeParent) = node;
		XVector_push_back(node->node, &childBFSNode);
		XVector_push_back(NextNodeArray, &childBFSNode);
		XStack_pop(ChildAll);
	}
	XStack_free(ChildAll);
	return XVector_size(node->node)>1? XVector_size(node->node) -1:0;
}

XVector* XMazePathfindingBFS(const XVector* maze, const XPoint start, const XPoint dest)
{
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	XBinaryTreeNode* root = CreationBFSNode_XPoint(start);//根节点
	XVector* CurrentNodeArray = XVector_init("BFSNode*", sizeof(XBinaryTreeNode*));//当前节点数组
	XVector* NextNodeArray = XVector_init("BFSNode*", sizeof(XBinaryTreeNode*));//下一个节点数组
	XVector_push_back(CurrentNodeArray,&root);//入根节点

	XBinaryTreeNode* CurrentNode = NULL;//当前遍历的节点
	bool isFindEnd = false;//找到终点标记
	while (!isFindEnd&&!XVector_empty(CurrentNodeArray))
	{
		for (XVector_iterator* it=XVector_begin(CurrentNodeArray);it!= XVector_end(CurrentNodeArray); it= XVector_iterator_add(CurrentNodeArray,it))
		{
			CurrentNode = *(XBinaryTreeNode**)it;
			if ((*(XPoint*)CurrentNode->data).x == dest.x&& (* (XPoint*)CurrentNode->data).y == dest.y)//判断是否到终点了
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
	XBinaryTreeObject_freeNodeAll(root);
	return Path;
}