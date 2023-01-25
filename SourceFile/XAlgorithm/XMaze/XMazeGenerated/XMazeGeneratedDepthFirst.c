#include"XMazeGeneratedDepthFirst.h"
#include"XAlgorithm.h"
#include<string.h>
#include<time.h>
#include<stdlib.h>
#include<math.h>
static void XMazeOpenCircuitRecursion(struct XVector* maze, const int x, const int y);
//随机开路四个方向-递归版
static void RandomOpenCircuitRecursion(struct XVector* maze, const int x, const int y)
{
	int row = XMazeRow(maze);//行
	int list = XMazeList(maze);//列
	int direction[4] = { Up,Right,Down,Left};
	for (int i = 4; i > 0; --i) {
		//随机选择一个方向
		int r = rand() % i;
		swap(&direction[r], &direction[i - 1], sizeof(int));
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
	struct XVector* LMaze = *(struct XVector**)XVector_at(maze, y);
	int Sign = *((int*)XVector_at(LMaze, x));
	if (Sign != XMazeWall)//如果当前不是墙壁，当前位置不需要开路
		return;
	int SignSum = 0;//判断上下左右一共有几个道路
	for (int i = -1; i < 2; i++)
	{
		for (int j = -1; j < 2; j++)
		{
			if (abs(i) == abs(j))
				continue;
			struct XVector* TLMaze = *(struct XVector**)XVector_at(maze, y+i);
			int TSign = *((int*)XVector_at(TLMaze, x+j));
			SignSum += TSign;
		}
	}
	if (SignSum <= XMazeRoute) //如果周围道路不超过一个，避免回到原点
	{
		*((int*)XVector_at(LMaze, x)) = XMazeRoute;
		RandomOpenCircuitRecursion(maze,x,y);
	}
}
//生成迷宫r行l列
struct XVector* XMazeGenerated(const size_t r, const size_t l)
{
	struct XVector* maze = XMaze_init(r, l);
	srand((unsigned)time(NULL));
	XMazeOpenCircuitRecursion(maze,1, 1);
	return maze;
}
