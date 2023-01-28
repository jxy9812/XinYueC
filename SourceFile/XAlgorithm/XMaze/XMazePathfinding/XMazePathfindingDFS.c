#include"XMazePathfindingDFS.h"
#include"XAlgorithm.h"
#include"XStack.h"
#include"XAlgorithm.h"
#include<math.h>

//判断当前点周围是否还有通道可以经过
static bool isPass(const XVector* maze, XPoint CurPoint)
{
	int row = XVectorTwo_Row(maze);//行
	int list = XVectorTwo_List(maze,0);//列
	if (CurPoint.x >= 0 && CurPoint.x < list && CurPoint.y >= 0 && CurPoint.y < row)
	{
		//获取当前位置
		int Sign = *((int*)XVectorTwo_at_XPoint(maze, CurPoint));
		if (Sign == XMazeRoute)
			return true;
	}	
	return false;
}
//探路四个方向-栈(周围是否已经无路可走了)
static bool Pathfinder(struct XStack* stack, struct XVector* maze, struct XPointStep CurPoint)
{
	int sum = 0;
	for (size_t i = 0; i < 4; i++) 
	{
		switch (i) {
		case Left:
		{
			XPoint Point = { CurPoint.x - 1,CurPoint.y };
			if (isPass(maze, Point))
			{
				XPointStep p = { CurPoint.x - 1,CurPoint.y,CurPoint.cur+1};
				stack->push(stack, &p);
				++sum;
			}
			break;
		}
		case Right:
		{
			XPoint Point = { CurPoint.x + 1,CurPoint.y };
			if (isPass(maze, Point))
			{
				XPointStep p = { CurPoint.x + 1,CurPoint.y,CurPoint.cur + 1 };
				stack->push(stack, &p);
				++sum;
			}
			break;
		}
		case Up:
		{
			XPoint Point = { CurPoint.x,CurPoint.y - 1 };
			if (isPass(maze, Point))
			{
				XPointStep p = { CurPoint.x,CurPoint.y - 1 ,CurPoint.cur + 1 };
				stack->push(stack, &p);
				++sum;
			}
			break;
		}
		case Down:
		{
			XPoint Point = { CurPoint.x,CurPoint.y + 1 };
			if (isPass(maze, Point))
			{
				XPointStep p = { CurPoint.x,CurPoint.y + 1,CurPoint.cur + 1 };
				stack->push(stack, &p);
				++sum;
			}
			break;
		}
		default:
			break;
		}
	}
	return sum == 0 ? true : false;
}
//回撤记录的点
static void XMazeRetracement(const XVector* maze, XStack* StackPointAll, XStack* StackPath)
{
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
}
XVector* XMazePathfindingOneDFS(const XVector* maze, const XPoint start, const XPoint dest)
{
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	XStack* StackPointAll = XStack_init("XPointStep", sizeof(XPointStep));//记录所有的点
	XStack* StackPath = XStack_init("XPointStep", sizeof(XPointStep));//记录路径
	XPointStep p = { start.x,start.y,1 };
	XStack_Push(StackPointAll, &p);
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
			XStack_Push(StackPath, &CurPoint);//保存坐标
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
	XVector* vector = XVector_init("XPoint", sizeof(XPoint));
	XStackRCopyXVector(StackPath, vector);//将栈内的数据逆序拷贝到数组
	XStack_free(StackPath);
	XVectorTwo_free(tempMaze);
	return vector;
}

XVector* XMazePathfindingShortDFS(const XVector* maze, const XPoint start, const XPoint dest)
{
	XVector* PathAll = XMazePathfindingAllDFS(maze, start, dest);
	XVector* PathShort = XVector_init("XPoint",sizeof(XPoint));
	for (XVector_iterator* itAll = XVector_begin(PathAll); itAll != XVector_end(PathAll); itAll = XVector_iterator_add(PathAll, itAll))
	{
		XVector* CurPath = *(XVector**)itAll;//获取路径
		size_t PathShortSize = XVector_size(PathShort);//当前最短路径数量
		size_t CurPathSize = XVector_size(CurPath);//当前路径数量
		if (PathShortSize == 0 || PathShortSize > CurPathSize)
		{
			XVector_swap(PathShort, CurPath);
		}
	}
	XVectorTwo_free(PathAll);
	return PathShort;
}

XVector* XMazePathfindingAllDFS(const XVector* maze, const XPoint start, const XPoint dest)
{
	XVector* PathAll = XVector_init("XVector*", sizeof(XVector*));//返回的二维数组保存所有的可行路径
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	XStack* StackPointAll = XStack_init("XPointStep", sizeof(XPointStep));//记录所有的点
	XStack* StackPath = XStack_init("XPointStep", sizeof(XPointStep));//记录路径
	XPointStep PointStart = {start.x,start.y,1};
	XStack_Push(StackPointAll, &PointStart);
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
			XStack_Push(StackPath, &CurPoint);//保存坐标
			if (CurPoint.x == dest.x && CurPoint.y == dest.y)//找到终点了
			{
				XVector* path = XVector_init("XPoint", sizeof(XPoint));
				XStackRCopyXVector(StackPath, path);//将栈内的数据逆序拷贝到数组,获得一条路径
				XVector_Push_Back(PathAll, &path);
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
}
