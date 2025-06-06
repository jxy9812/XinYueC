#include"XMazePathfindingDFS.h"
#include"XAlgorithm.h"
#include"XStack.h"
#include"XMazePathfindingObject.h"
#include"XClass.h"
#include<math.h>
//回撤记录的点
static void XMazeRetracement(const XVector* maze, XStack* StackPointAll, XStack* StackPath)
{
#if XStack_ON
	XPointStep CurPoint = *(XPointStep*)XStack_top_base(StackPointAll);//获取栈顶保存的点
	while (!XStack_isEmpty_base(StackPath))
	{
		XPointStep PathCurPoint = *(XPointStep*)XStack_top_base(StackPath);//获取栈顶保存的点
		if (PathCurPoint.cur < CurPoint.cur)
		{
			break;
		}
		else
		{
			int* pSign = ((int*)XVectorTwo_at(maze, PathCurPoint.y, PathCurPoint.x));
			*pSign = XMazeRoute;//回撤
			XStack_pop_base(StackPath);
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
	XStack* StackPointAll = XStack_create( sizeof(XPointStep));//记录所有的点
	XStack* StackPath = XStack_create(sizeof(XPointStep));//记录路径
	XPointStep p = { start.x,start.y,1 };
	XStack_push_base(StackPointAll, &p);
	while (!XStack_isEmpty_base(StackPointAll))
	{
		XPointStep CurPoint = *(XPointStep*)XStack_top_base(StackPointAll);//获取栈顶保存的点
		XStack_pop_base(StackPointAll);
		//printf("%d\n", XStack_getSize_base(stack));
		//获取当前位置
		int* pSign = ((int*)XVectorTwo_at(tempMaze, CurPoint.y, CurPoint.x));

		if (*pSign == XMazeRoute)
		{
			*pSign = XMazePath;//标记当前位置
			XStack_push_base(StackPath, &CurPoint);//保存坐标
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
	XStack_delete_base(StackPointAll);
	XVector* vector = XVector_create(sizeof(XPoint));
	XStackRCopyXVector(StackPath, vector);//将栈内的数据逆序拷贝到数组
	XStack_delete_base(StackPath);
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
	XVector* PathShortAll = XVector_create(sizeof(XVector*));//返回的二维数组保存所有的最短可行路径
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	XStack* StackPointAll = XStack_create(sizeof(XPointStep));//记录所有的点
	XStack* StackPath = XStack_create(sizeof(XPointStep));//记录路径
	XPointStep PointStart = { start.x,start.y,1 };
	XStack_push_base(StackPointAll, &PointStart);
	size_t CurSize = 0;//当前最短的节点数量
	while (!XStack_isEmpty_base(StackPointAll))
	{
		XPointStep CurPoint = *(XPointStep*)XStack_top_base(StackPointAll);//获取栈顶保存的点
		XStack_pop_base(StackPointAll);
		//获取当前位置
		int* pSign = ((int*)XVectorTwo_at(tempMaze, CurPoint.y, CurPoint.x));

		if (*pSign == XMazeRoute)
		{
			*pSign = XMazePath;//标记当前位置
			XStack_push_base(StackPath, &CurPoint);//保存坐标
			if (CurPoint.x == dest.x && CurPoint.y == dest.y)//找到终点了
			{
				if(CurSize==0|| CurSize>=XStack_getSize_base(StackPath))//初始 找到相同或更短路径
				{
					if (CurSize > XStack_getSize_base(StackPath))//找到更短路径
					{
						XVectorTwo_clear(PathShortAll);
					}
					CurSize = XStack_getSize_base(StackPath);
					XVector* path = XVector_create( sizeof(XPoint));
					XStackRCopyXVector(StackPath, path);//将栈内的数据逆序拷贝到数组,获得一条路径
					XVector_push_back_base(PathShortAll, &path);
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
	XStack_delete_base(StackPointAll);
	XStack_delete_base(StackPath);
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
	XVector* PathAll = XVector_create( sizeof(XVector*));//返回的二维数组保存所有的可行路径
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	XStack* StackPointAll = XStack_create(sizeof(XPointStep));//记录所有的点
	XStack* StackPath = XStack_create(sizeof(XPointStep));//记录路径
	XPointStep PointStart = {start.x,start.y,1};
	XStack_push_base(StackPointAll, &PointStart);
	while (!XStack_isEmpty_base(StackPointAll))
	{
		XPointStep CurPoint = *(XPointStep*)XStack_top_base(StackPointAll);//获取栈顶保存的点
		XStack_pop_base(StackPointAll);
		//printf("%d\n", XStack_getSize_base(StackPointAll));
		//gotoxy(0, 0);
		//获取当前位置
		int* pSign = ((int*)XVectorTwo_at(tempMaze, CurPoint.y, CurPoint.x));

		if (*pSign == XMazeRoute)
		{
			*pSign = XMazePath;//标记当前位置
			XStack_push_base(StackPath, &CurPoint);//保存坐标
			if (CurPoint.x == dest.x && CurPoint.y == dest.y)//找到终点了
			{
				XVector* path = XVector_create( sizeof(XPoint));
				XStackRCopyXVector(StackPath, path);//将栈内的数据逆序拷贝到数组,获得一条路径
				XVector_push_back_base(PathAll, &path);
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
	XStack_delete_base(StackPointAll);
	XStack_delete_base(StackPath);
	XVectorTwo_free(tempMaze);
	return PathAll;
#else
	IS_ON_DEBUG(XStack_ON);
	return NULL;
#endif
}
