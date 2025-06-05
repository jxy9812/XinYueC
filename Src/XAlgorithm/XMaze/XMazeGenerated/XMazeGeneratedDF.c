#include"XMazeGeneratedDF.h"
#include"XClass.h"
#include"XAlgorithm.h"
#include"XStack.h"
#include<string.h>
#include<time.h>
#include<stdlib.h>
#include<math.h>
static void XMazeOpenCircuitRecursion(struct XVector* maze, const int x, const int y);
//随机探路四个方向-递归版
static void RandomOpenCircuitRecursion(struct XVector* maze, const int x, const int y)
{
	int row = XMazeRow(maze);//行
	int list = XMazeList(maze);//列
	int direction[4] = { Up,Right,Down,Left};
	for (int i = 4; i > 0; --i) {
		//随机选择一个方向
		int r = rand() % i;
		XSwap(&direction[r], &direction[i - 1], sizeof(int));
		switch (direction[i - 1]) {
		case Left:
		{
			if(x-1>0)
			XMazeOpenCircuitRecursion(maze, x - 1, y);
			break;
		}
		case Right:
		{
			if (x + 1 <list-1)
			XMazeOpenCircuitRecursion(maze, x + 1, y);
			break;
		}
		case Up:
		{
			if (y - 1 > 0)
			XMazeOpenCircuitRecursion(maze, x, y - 1);
			break;
		}
		case Down:
		{
			if (y + 1 < row - 1)
			XMazeOpenCircuitRecursion(maze, x, y + 1);
			break;
		}
		default:
			break;
		}
	}
}
//砸墙开路-递归版
static void XMazeOpenCircuitRecursion(struct XVector* maze, const int x, const int y)
{    
	struct XVector* LMaze = *(struct XVector**)XVector_at_base(maze, y);
	int Sign = *((int*)XVector_at_base(LMaze, x));
	if (Sign != XMazeWall)//如果当前不是墙壁，当前位置不需要开路
		return;
	int SignSum = 0;//判断上下左右一共有几个道路
	for (int i = -1; i < 2; i++)
	{
		for (int j = -1; j < 2; j++)
		{
			if (abs(i) == abs(j))
				continue;
			struct XVector* TLMaze = *(struct XVector**)XVector_at_base(maze, y+i);
			int TSign = *((int*)XVector_at_base(TLMaze, x+j));
			SignSum += TSign;
		}
	}
	if (SignSum <= XMazeRoute) //如果周围道路不超过一个，避免回到原点
	{
		*((int*)XVector_at_base(LMaze, x)) = XMazeRoute;
		RandomOpenCircuitRecursion(maze,x,y);
	}
}

//随机探路四个方向-栈
static void RandomOpenCircuitStack(struct XStack* stack,struct XVector* maze, const int x, const int y)
{
	int row = XMazeRow(maze);//行
	int list = XMazeList(maze);//列
	int direction[4] = { Up,Right,Down,Left };
	for (int i = 4; i > 0; --i) {
		//随机选择一个方向
		int r = rand() % i;
		XSwap(&direction[r], &direction[i - 1], sizeof(int));
		int temp_x = x, temp_y = y;
		switch (direction[i - 1]) {
		case Left:
		{
			if (x - 1 > 0)
			{
				temp_y = y;
				temp_x = x - 1;
				/*XStack_push_base(stack,y);
				XStack_push_base(stack,x - 1);*/
			}
			break;
		}
		case Right:
		{
			if (x + 1 < list - 1)
			{
				temp_y = y;
				temp_x = x + 1;
				/*XStack_push_base(stack, y);
				XStack_push_base(stack, x + 1);*/
			}
			break;
		}
		case Up:
		{
			if (y - 1 > 0)
			{
				temp_y = y-1;
				temp_x = x;
				/*XStack_push_base(stack, y-1);
				XStack_push_base(stack, x);*/
			}
			break;
		}
		case Down:
		{
			if (y + 1 < row - 1)
			{
				temp_y = y + 1;
				temp_x = x;
				/*XStack_push_base(stack, y+1);
				XStack_push_base(stack, x);*/
			}
			break;
		}
		default:
			break;
		}
		XStack_push_base(stack, &temp_y);
		XStack_push_base(stack, &temp_x);
	}
}
//砸墙开路-栈
static void XMazeOpenCircuitStack(struct XVector* maze, const int x, const int y, bool oneExit)
{
#if XStack_ON
	XStack* stack=XStack_Create(int);
	XStack_push_base(stack, &y);
	XStack_push_base(stack, &x);
	while (!XStack_isEmpty_base(stack))
	{
		int x = XStack_Top_Base(stack,int);
		XStack_pop_base(stack);
		int y = XStack_Top_Base(stack,int);
		XStack_pop_base(stack);

		struct XVector* LMaze = *(struct XVector**)XVector_at_base(maze, y);
		int Sign = *((int*)XVector_at_base(LMaze, x));//获取当前位置
		if (Sign != XMazeWall)//如果当前不是墙壁，当前位置不需要开路
			continue;
		int SignSum = 0;//判断上下左右一共有几个道路
		for (int i = -1; i < 2; i++)
		{
			for (int j = -1; j < 2; j++)
			{
				if (abs(i) == abs(j))
					continue;
				 XVector* TLMaze = *(XVector**)XVector_at_base(maze, y + i);
				int TSign = *((int*)XVector_at_base(TLMaze, x + j));
				SignSum += TSign;
			}
		}
		if (SignSum <= (XMazeRoute+ !oneExit)) //如果周围道路不超过一个，避免回到原点
		{
			*((int*)XVector_at_base(LMaze, x)) = XMazeRoute;
			RandomOpenCircuitStack(stack,maze, x, y);
		}
	}
	XStack_free_base(stack);
#else
	IS_ON_DEBUG(XStack_ON);
#endif
}
//生成迷宫r行l列
XVector* XMazeGenerated(const size_t r, const size_t l, const int x, const int y, bool oneExit)
{
#if XVectorTwo_ON
	int sign = XMazeWall;
	XVector* maze = XVectorTwoMatrix_create(sizeof(int),r, l,&sign);
	srand((unsigned)time(NULL));
	XMazeOpenCircuitStack(maze,x, y,oneExit);
	return maze;

#else
	IS_ON_DEBUG(XVectorTwo_ON);
#endif

}
