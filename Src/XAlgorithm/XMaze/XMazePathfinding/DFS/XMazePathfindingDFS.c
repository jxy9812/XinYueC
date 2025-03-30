#include"XMazePathfindingDFS.h"
#include"XAlgorithm.h"
#include"XStack.h"
#include"XAlgorithm.h"
#include"XMazePathfindingObject.h"
#include<math.h>
//回撤记录的点
static void XMazeRetracement(const XVector* maze, XStack* StackPointAll, XStack* StackPath)
{
#if XStack_ON
	XPointStep CurPoint = *(XPointStep*)XStack_top(StackPointAll);//获取栈顶保存的点
	while (!XStack_empty(StackPath))
	{
		XPointStep PathCurPoint = *(XPointStep*)XStack_top(StackPath);//获取栈顶保存的点
		if (PathCurPoint.cur < CurPoint.cur)
		{
			break;
		}
		else
		{
			int* pSign = ((int*)XVectorTwo_at(maze, PathCurPoint.y, PathCurPoint.x));
			*pSign = XMazeRoute;//回撤
			XStack_pop(StackPath);
		}
	}
#else
	IS_ON_DEBUG(XStack_ON);
#endif
}
XVector* XMazePathfindingOneDFS(const XVector* maze, const XPoint start, const XPoint dest)
{
#if XStack_ON
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	XStack* StackPointAll = XStack_new( sizeof(XPointStep));//记录所有的点
	XStack* StackPath = XStack_new(sizeof(XPointStep));//记录路径
	XPointStep p = { start.x,start.y,1 };
	XStack_push(StackPointAll, &p);
	while (!XStack_empty(StackPointAll))
	{
		XPointStep CurPoint = *(XPointStep*)XStack_top(StackPointAll);//获取栈顶保存的点
		XStack_pop(StackPointAll);
		//printf("%d\n", XStack_size(stack));
		//获取当前位置
		int* pSign = ((int*)XVectorTwo_at(tempMaze, CurPoint.y, CurPoint.x));

		if (*pSign == XMazeRoute)
		{
			*pSign = XMazePath;//标记当前位置
			XStack_push(StackPath, &CurPoint);//保存坐标
			if (CurPoint.x == dest.x && CurPoint.y == dest.y)//找到终点了
			{
				break;
			}
			if (Pathfinder(StackPointAll, tempMaze, CurPoint))
			{
				XMazeRetracement(tempMaze, StackPointAll, StackPath);
			}
		}

	}
	XStack_free(StackPointAll);
	XVector* vector = XVector_new(sizeof(XPoint));
	XStackRCopyXVector(StackPath, vector);//将栈内的数据逆序拷贝到数组
	XStack_free(StackPath);
	XVectorTwo_free(tempMaze);
	return vector;
#else
	IS_ON_DEBUG(XStack_ON);
	return NULL;
#endif
}

XVector* XMazePathfindingShortDFS(const XVector* maze, const XPoint start, const XPoint dest)
{
#if XStack_ON
	XVector* PathShortAll = XVector_new(sizeof(XVector*));//返回的二维数组保存所有的最短可行路径
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	XStack* StackPointAll = XStack_new(sizeof(XPointStep));//记录所有的点
	XStack* StackPath = XStack_new(sizeof(XPointStep));//记录路径
	XPointStep PointStart = { start.x,start.y,1 };
	XStack_push(StackPointAll, &PointStart);
	size_t CurSize = 0;//当前最短的节点数量
	while (!XStack_empty(StackPointAll))
	{
		XPointStep CurPoint = *(XPointStep*)XStack_top(StackPointAll);//获取栈顶保存的点
		XStack_pop(StackPointAll);
		//获取当前位置
		int* pSign = ((int*)XVectorTwo_at(tempMaze, CurPoint.y, CurPoint.x));

		if (*pSign == XMazeRoute)
		{
			*pSign = XMazePath;//标记当前位置
			XStack_push(StackPath, &CurPoint);//保存坐标
			if (CurPoint.x == dest.x && CurPoint.y == dest.y)//找到终点了
			{
				if(CurSize==0|| CurSize>=XStack_size(StackPath))//初始 找到相同或更短路径
				{
					if (CurSize > XStack_size(StackPath))//找到更短路径
					{
						XVectorTwo_clear(PathShortAll);
					}
					CurSize = XStack_size(StackPath);
					XVector* path = XVector_new( sizeof(XPoint));
					XStackRCopyXVector(StackPath, path);//将栈内的数据逆序拷贝到数组,获得一条路径
					XVector_push_back(PathShortAll, &path);
				}

				//开始回撤
				XMazeRetracement(tempMaze, StackPointAll, StackPath);
				continue;
			}
			if (Pathfinder(StackPointAll, tempMaze, CurPoint))
			{
				XMazeRetracement(tempMaze, StackPointAll, StackPath);//开始回撤
			}
		}

	}
	XStack_free(StackPointAll);
	XStack_free(StackPath);
	XVectorTwo_free(tempMaze);
	return PathShortAll;
#else
	IS_ON_DEBUG(XStack_ON);
	return NULL;
#endif
}

XVector* XMazePathfindingAllDFS(const XVector* maze, const XPoint start, const XPoint dest)
{
#if XStack_ON
	XVector* PathAll = XVector_new( sizeof(XVector*));//返回的二维数组保存所有的可行路径
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	XStack* StackPointAll = XStack_new(sizeof(XPointStep));//记录所有的点
	XStack* StackPath = XStack_new(sizeof(XPointStep));//记录路径
	XPointStep PointStart = {start.x,start.y,1};
	XStack_push(StackPointAll, &PointStart);
	while (!XStack_empty(StackPointAll))
	{
		XPointStep CurPoint = *(XPointStep*)XStack_top(StackPointAll);//获取栈顶保存的点
		XStack_pop(StackPointAll);
		//printf("%d\n", XStack_size(StackPointAll));
		//gotoxy(0, 0);
		//获取当前位置
		int* pSign = ((int*)XVectorTwo_at(tempMaze, CurPoint.y, CurPoint.x));

		if (*pSign == XMazeRoute)
		{
			*pSign = XMazePath;//标记当前位置
			XStack_push(StackPath, &CurPoint);//保存坐标
			if (CurPoint.x == dest.x && CurPoint.y == dest.y)//找到终点了
			{
				XVector* path = XVector_new( sizeof(XPoint));
				XStackRCopyXVector(StackPath, path);//将栈内的数据逆序拷贝到数组,获得一条路径
				XVector_push_back(PathAll, &path);
				/*printf("走到终点\n");
				XMazePrint(tempMaze, "■", "  ", "★");*/
				//开始回撤
				XMazeRetracement(tempMaze, StackPointAll, StackPath);
				continue;
			}
			if (Pathfinder(StackPointAll, tempMaze, CurPoint))
			{
				XMazeRetracement(tempMaze,StackPointAll, StackPath);//开始回撤
			}
		}

	}
	XStack_free(StackPointAll);
	XStack_free(StackPath);
	XVectorTwo_free(tempMaze);
	return PathAll;
#else
	IS_ON_DEBUG(XStack_ON);
	return NULL;
#endif
}
