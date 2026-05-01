#include"XMazePathfindingBFS.h"
#include"XClass.h"
#include"XContainer.h"
#include"XMazePathfindingObject.h"
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
typedef struct XVector XVector;
#define GetXPoint(nodes) (*(XPoint*)XTreeNode_GetDataPtr(nodes))
//创建一个节点
static XTreeNode* CreationBFSNode_XPoint(XPoint pos)
{
	XTreeNode* nodes = XBTreeNode_create(&pos,sizeof(XPoint));
	if (ISNULL(nodes, ""))
		return NULL;
	return nodes;
}
//创建一个节点
static XTreeNode* CreationBFSNode(const int x,const int y)
{
	XPoint pos = { x,y };
	return CreationBFSNode_XPoint(pos);
}

//获取迷宫路径
static XVector* GetXMazePath(const XTreeNode* child)
{
	XVector* Path = XVector_create(sizeof(XPoint));
	XTreeNode* current = child;
	while (current!=NULL)
	{
		XVector_push_front_base(Path, XTreeNode_GetDataPtr(current));
		//current = *XTreeNode_getNodeRef(current,XTreeParent);
		current = XTreeNode_GetParent(current);
	}
	return Path;
}
//插入孩子
static size_t insertChild(const XVector* maze, XTreeNode* node, XVector* NextNodeArray)
{
#if XStack_ON
	int* pMazePos = (int*)XVectorTwo_at_XPoint(maze, GetXPoint(node));
	if (*pMazePos != XMazeRoute)
		return 0;
	else
		*pMazePos = XMazePath;//标记走过了
	XPointStep pos = { GetXPoint(node).x,GetXPoint(node).y,1};
	XStack* ChildAll = XStack_create(sizeof(XPointStep));
	Pathfinder(ChildAll,maze, pos);//获取周围能走的点位
	while (!XStack_isEmpty_base(ChildAll))
	{
		XPointStep* pCurrentPos = (XPointStep*)XStack_top_base(ChildAll);
		XTreeNode* childBFSNode = CreationBFSNode(pCurrentPos->x,pCurrentPos ->y);
		//*XTreeNode_getNodeRef(childBFSNode, XTreeParent) = nodes;
		XTreeNode_SetParent(childBFSNode, node);
		XVector_push_back_base(XTreeNode_GetNodes(node), &childBFSNode);
		XVector_push_back_base(NextNodeArray, &childBFSNode);
		XStack_pop_base(ChildAll);
	}
	XStack_delete_base(ChildAll);
	return XVector_size_base(XTreeNode_GetNodes(node))>1? XVector_size_base(XTreeNode_GetNodes(node)) -1:0;
#else
	IS_ON_DEBUG(XStack_ON);
#endif
}

XVector* XMazePathfindingBFS(const XVector* maze, const XPoint start, const XPoint dest)
{
#if XVector_ON
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	XTreeNode* root = CreationBFSNode_XPoint(start);//根节点
	XVector* CurrentNodeArray = XVector_create( sizeof(XTreeNode*));//当前节点数组
	XVector* NextNodeArray = XVector_create(sizeof(XTreeNode*));//下一个节点数组
	XVector_push_back_base(CurrentNodeArray,&root);//入根节点

	XTreeNode* CurrentNode = NULL;//当前遍历的节点
	bool isFindEnd = false;//找到终点标记
	while (!isFindEnd&&!XVector_isEmpty_base(CurrentNodeArray))
	{
		//for (XVector_iterator* it=XVector_begin(CurrentNodeArray);it!= XVector_end(CurrentNodeArray); it= XVector_iterator_add(CurrentNodeArray,it))
		for_each_iterator(CurrentNodeArray, XVector, it)
		{
			CurrentNode = *(XTreeNode**)XVector_iterator_data(&it);
			if (GetXPoint(CurrentNode).x == dest.x && GetXPoint(CurrentNode).y == dest.y)//判断是否到终点了
			{
				isFindEnd = true;
				break;
			}
			size_t n=insertChild(tempMaze, CurrentNode, NextNodeArray);
		}
		XVector_clear_base(CurrentNodeArray);
		XVector_swap_base(CurrentNodeArray, NextNodeArray);
	}
	XVector* Path = GetXMazePath(CurrentNode);
	XVector_delete_base(CurrentNodeArray);
	XVector_delete_base(NextNodeArray);
	XVectorTwo_delete(tempMaze);
	XTree_delete(root,NULL,NULL);
	return Path;
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif
}