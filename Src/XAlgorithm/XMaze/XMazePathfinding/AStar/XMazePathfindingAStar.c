#include"XMazePathfindingAStar.h"
#include"XClass.h"
#include"XContainerObject.h"
#include"XMazePathfindingObject.h"
#include<string.h>
#include<stdio.h>
#include<stdlib.h>
//创建一个节点
static AStarNode* CreationAStarNode(const int x, const int y)
{
#if XVector_ON
	AStarNode* nodes = (AStarNode*)XMemory_malloc(sizeof(AStarNode));
	if (ISNULL(nodes, ""))
		return NULL;
	nodes->pos.x = x;
	nodes->pos.y = y;
	nodes->parent = NULL;
	nodes->child = XVector_create(sizeof(AStarNode*));
	nodes->currentCosts = 0;
	nodes->estimateCosts = 0;
	return nodes;
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif
}
//创建一个节点
static AStarNode* CreationAStarNode_XPoint(const XPoint pos)
{
	return CreationAStarNode(pos.x,pos.y);
}
//设置代价
static void setCosts(const XVector* maze, const XPoint dest,AStarNode* nodes, AStarNode* parent)
{
	//设置当前代价
	size_t sum = abs(nodes->pos.x - parent->pos.x) + abs(nodes->pos.y - parent->pos.y);
	if (sum == 1)//直线
	{
		nodes->currentCosts += StraightLine;
	}
	else if (sum == 2)//斜线
	{
		nodes->currentCosts += ObliqueLine;
	}
	//设置预期代价
	nodes->estimateCosts= (abs(nodes->pos.x - dest.x) + abs(nodes->pos.y - dest.y))* StraightLine;
}
//获取迷宫路径
static XVector* GetXMazePath(const XVector* child)
{
	XVector* Path = XVector_create( sizeof(XPoint));
	AStarNode* current = child;
	while (current != NULL)
	{
		XVector_push_front_base(Path, &current->pos);
		current = current->parent;
	}
	return Path;
}
//插入孩子
static size_t insertChild(const XVector* maze, const XPoint dest, AStarNode* nodes, const XVector* NodeArray, bool Oblique)
{
#if XStack_ON
	int* pMazePos = (int*)XVectorTwo_at_XPoint(maze, nodes->pos);
	if (*pMazePos != XMazeRoute)
		return 0;
	else
		*pMazePos = XMazePath;//标记走过了
	XPointStep pos = { nodes->pos.x,nodes->pos.y,1 };
	XStack* ChildAll = XStack_create(sizeof(XPointStep));
	Pathfinder(ChildAll, maze, pos);//获取周围能走的点位
	if(Oblique)//能斜着走
		PathfinderOblique(ChildAll, maze, pos);//获取周围能走的点位，斜的
	while (!XStack_isEmpty_base(ChildAll))
	{
		XPointStep* pCurrentPos = (XPointStep*)XStack_top_base(ChildAll);
		AStarNode* childAStarNode = CreationAStarNode(pCurrentPos->x, pCurrentPos->y);//创建孩子节点
		childAStarNode->parent = nodes;//设置父节点
		setCosts(maze, dest, childAStarNode, nodes);//设置代价
		XVector_push_back_base(nodes->child, &childAStarNode);//将创建的孩子绑定到父节点下
		XVector_push_back_base(NodeArray, &childAStarNode);
		XStack_pop_base(ChildAll);
	}
	XStack_delete_base(ChildAll);
	return XVector_getSize_base(nodes->child);
#else
	IS_ON_DEBUG(XStack_ON);
	return 0;
#endif
}
//排序,根据总代价进行降序排序
static int sortDescendingtCosts(const void* LPrevValue, const void* LNextValue)
{
	AStarNode* NodeOne = *(AStarNode**)LPrevValue;
	AStarNode* NodeTwo = *(AStarNode**)LNextValue;
	return (NodeOne->currentCosts + NodeOne->estimateCosts) - (NodeTwo->currentCosts + NodeTwo->estimateCosts);
}
//释放树节点
static void XBinaryTreeObject_freeNode(AStarNode* root)
{
#if XStack_ON
	XStack* stack = XStack_create(sizeof(AStarNode*));
	XStack_push_base(stack, &root);
	while (!XStack_isEmpty_base(stack))
	{
		AStarNode* current = *(AStarNode**)XStack_top_base(stack);
		XStack_pop_base(stack);
		for (XVector_iterator* it = XVector_begin(current->child); it != XVector_end(current->child); it = XVector_iterator_add(current->child, it))
		{
			XStack_push_base(stack, it);
		}
		XVector_delete_base(current->child);
		XMemory_free(current);
	}
#else
	IS_ON_DEBUG(XStack_ON);
#endif
}
XVector* XMazePathfindingAStar(const XVector* maze, const XPoint start, const XPoint dest, bool Oblique)
{
#if XVectorTwo_ON&&XVector_ON
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	AStarNode* root = CreationAStarNode_XPoint(start);//根节点
	XVector* CurrentNodeArray = XVector_create( sizeof(AStarNode*));//当前节点数组
	XVector_push_back_base(CurrentNodeArray, &root);//入根节点

	AStarNode* CurrentNode = NULL;//当前遍历的节点
	bool isFindEnd = false;//找到终点标记
	while (!isFindEnd && !XVector_isEmpty_base(CurrentNodeArray))
	{
		XVector_sort_base(CurrentNodeArray, sortDescendingtCosts);
		AStarNode** back = XVector_back_base(CurrentNodeArray);
		CurrentNode = *back;
		int nSel = XVector_getSize_base(CurrentNodeArray)-1;
		if (CurrentNode->pos.x == dest.x && CurrentNode->pos.y == dest.y)//判断是否到终点了
		{
			isFindEnd = true;
			break;
		}
		size_t n = insertChild(tempMaze, dest, CurrentNode, CurrentNodeArray,Oblique);
		//XVector_erase_int(CurrentNodeArray, nSel, nSel);
		XVector_remove_base(CurrentNodeArray, nSel,1);
	}
	XVector* Path = NULL;
	if(isFindEnd)
		Path = GetXMazePath(CurrentNode);
	XBinaryTreeObject_freeNode(root);
	XVector_delete_base(CurrentNodeArray);
	XVectorTwo_free(tempMaze);
	return Path;
#else
	IS_ON_DEBUG(XVectorTwo_ON&& XVector_ON);
	return NULL;
#endif
}
