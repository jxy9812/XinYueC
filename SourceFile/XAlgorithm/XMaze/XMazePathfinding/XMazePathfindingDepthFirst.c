#include"XMazePathfindingDepthFirst.h"
#include"XStack.h"
//随机探路四个方向-栈
static void Pathfinder(struct XStack* stack, struct XVector* maze, XPoint CurPoint)
{
	int row = XMazeRow(maze);//行
	int list = XMazeList(maze);//列
	for (size_t i = 0; i < 4; i++) 
	{
		switch (i) {
		case Left:
		{
			if (CurPoint.x >0)
			{
				XPoint Point = { CurPoint.x-1,CurPoint .y};
				stack->push(stack,&Point);
			}
			break;
		}
		case Right:
		{
			if (CurPoint.x + 1 < list)
			{
				XPoint Point = { CurPoint.x + 1,CurPoint.y };
				stack->push(stack, &Point);
			}
			break;
		}
		case Up:
		{
			if (CurPoint.y > 0)
			{
				XPoint Point = { CurPoint.x,CurPoint.y-1 };
				stack->push(stack, &Point);
			}
			break;
		}
		case Down:
		{
			if (CurPoint.y + 1 < row)
			{
				XPoint Point = { CurPoint.x,CurPoint.y + 1 };
				stack->push(stack, &Point);
			}
			break;
		}
		default:
			break;
		}
	}
}

XVector* XMazePathfindingShort(const XVector* maze, const XPoint start, const XPoint dest)
{
	XStack* stack = XStack_init("XPoint", sizeof(XPoint));

	return stack;
}

XVector* XMazePathfindingAll(const XVector* maze, const XPoint start, const XPoint dest)
{
	XStack* stack = XStack_init("XPoint", sizeof(XPoint));
	XStack* SPath = XStack_init("XPoint", sizeof(XPoint));
	XStack_Push(stack, &start);
	while (!XStack_empty(stack))
	{
		//printf("栈内元素%d\n", XStack_size(stack));
		XPoint CurPoint = *(XPoint*)XStack_top(stack);//获取栈顶保存的点
		
		//获取当前位置
		struct XVector* LMaze = *(struct XVector**)XVector_at(maze, CurPoint.y);
		int Sign = *((int*)XVector_at(LMaze, CurPoint.x));

		//如果当前是墙，返回
		if (Sign == XMazeWall)
		{
			XStack_pop(stack);
			continue;
		}
		else if (Sign == XMazePath)//如果是踩过的
		{
			while (1)
			{
				XPoint PathPoint = *(XPoint*)XStack_top(SPath);//获取栈顶保存的点
				if (PathPoint.x != CurPoint.x || PathPoint.y != CurPoint.y)
				{
					//struct XVector* LMaze = *(struct XVector**)XVector_at(maze, PathPoint.y);
					//*((int*)XVector_at(LMaze, PathPoint.x)) = XMazeRoute;//回退
					XStack_pop(SPath);
				}
				else
				{
					break;
				}
			}
			XStack_pop(stack);
			continue;
		}
		if (Sign == XMazeRoute)
		{
			*((int*)XVector_at(LMaze, CurPoint.x)) = XMazePath;//标记当前位置
			XStack_Push(SPath, &CurPoint);//保存坐标
			if (CurPoint.x == dest.x && CurPoint.y == dest.y)//找到终点了
			{
				return stack;
			}
			Pathfinder(stack, maze, CurPoint);
		}
		
	}
	return stack;
}
