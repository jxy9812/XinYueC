#include"Test.h"
#include"XMazePathfindingDepthFirst.h"
#include"XMazeGeneratedDepthFirst.h"
#include"XStack.h"
#include"XAlgorithm.h"
void XMazePathfinding()
{
	int XMazeSize = 20;//迷宫大小
	XPoint start = { 1,1 };//迷宫起点
	XPoint dest = { XMazeSize-2,XMazeSize-2 };//迷宫终点
	struct XVector* maze = NULL;
	//随机出可用的迷宫
	while (true)
	{
		maze = XMazeGenerated(XMazeSize, XMazeSize);
		int Sign = *((int*)XVectorTwo_at_XPoint(maze, dest));
		if (Sign == XMazeRoute)
			break;
		else
		{
			XVectorTwo_free(maze);
		}
	}
	
	XMazePrint(maze, "■", "  ");

	XVector* Path = XMazePathfindingOne(maze, start, dest);
	printf("测试迷宫寻路-程序时间最少不保证最短-有%d个点\n", XVector_size(Path));
	XDelay(1000);
	XMazePathPrintSleep(maze, Path, "■", "  ", "★", 100);
	XVector_free(Path);
	XDelay(1000);
	
	XVector* PathAll = XMazePathfindingAll(maze, start, dest);
	printf("测试迷宫寻路-全部方案一共找到%d种方案\n",XVector_size(PathAll));
	XDelay(1000);
	for (XVector_iterator* itAll = XVector_begin(PathAll); itAll != XVector_end(PathAll); itAll = XVector_iterator_add(PathAll, itAll))
	{
		XVector * path= *(XVector**)itAll;//获取路径
		printf("当前方案有%d个点\n", XVector_size(path));
		XDelay(1000);
		XMazePathPrintSleep(maze, path, "■", "  ", "★", 100);
		//printf("当前路径%d个点\n",XVector_size(path));
		//for (XVector_iterator* it = XVector_begin(path); it != XVector_end(path); it = XVector_iterator_add(path, it))
		//{
		//	XPoint CurPoint = *(XPoint*)it;//获取点
		//	printf("(%d,%d) ", CurPoint.x, CurPoint.y);
		//}
		//printf("\n");
	}
	XVectorTwo_free(PathAll);
	 
	Path = XMazePathfindingShort(maze, start, dest);
	printf("测试迷宫寻路-最短路径-单条-有%d个点\n", XVector_size(Path));
	XDelay(1000);
	XMazePathPrintSleep(maze, Path, "■", "  ", "★",100);
	//XMazePathPrintPoint(Path);
	XVector_free(Path);
	XMazeFree(maze);
}