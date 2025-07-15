#include"XDataStructTest.h"
#if DEMOTEST
#include"XMazePathfindingDFS.h"
#include"XMazePathfindingBFS.h"
#include"XMazePathfindingAStar.h"
#include"XMazeGeneratedDF.h"
#include"XStack.h"
#include"XAlgorithm.h"
#include<stdlib.h>
void XMazePathfinding()
{
#if XVectorTwo_ON
	int XMazeSize = 50;//迷宫大小
	XPoint start = { 1,1 };//迷宫起点
	XPoint dest = { XMazeSize-2,XMazeSize-2 };//迷宫终点
	struct XVector* maze = NULL;
	//随机出可用的迷宫
	while (true)
	{
		maze = XMazeGenerated(XMazeSize, XMazeSize, start.x, start.y,true);
		int Sign = *((int*)XVectorTwo_at_XPoint(maze, dest));
		if (Sign == XMazeRoute)
			break;
		else
		{
			XVectorTwo_delete(maze);
		}
	}
	
	XMazePrint(maze, "■", "  ");

	/*XVector* Path = XMazePathfindingOneDFS(maze, start, dest);
	printf("测试迷宫寻路-程序时间最少不保证最短-有%d个点\n", XVector_size(Path));
	system("pause");
	system("cls");
	XMazePathPrintSleep(maze, Path, "■", "  ", "★", 100);
	XVector_delete_base(Path);
	system("pause");
	system("cls");*/

	//XVector* PathAll = XMazePathfindingShortDFS(maze, start, dest);
	//printf("测试迷宫寻路-最短路径-单条-有%d个方案\n", XVector_size(PathAll));
	//system("pause");
	//system("cls");

	//for (XVector_iterator* itAll = XVector_begin(PathAll); itAll != XVector_end(PathAll); itAll = XVector_iterator_add(PathAll, itAll))
	//{
	//	XVector* path = *(XVector**)itAll;//获取路径
	//	gotoxy(0, 0);
	//	XMazePrint(maze, "■", "  ");
	//	printf("当前方案有%d个点\n", XVector_size(path));
	//	XDelay(1000);
	//	XMazePathPrintSleep(maze, path, "■", "  ", "★", 100);
	//	//printf("当前方案有%d个点\n", XVector_size(path));
	//}
	//XVectorTwo_delete(PathAll);
	//system("pause");
	//system("cls");

	//PathAll = XMazePathfindingAllDFS(maze, start, dest);
	//printf("测试迷宫寻路-全部方案一共找到%d种方案\n",XVector_size(PathAll));
	//system("pause");
	//system("cls");
	//for (XVector_iterator* itAll = XVector_begin(PathAll); itAll != XVector_end(PathAll); itAll = XVector_iterator_add(PathAll, itAll))
	//{
	//	XVector* path = *(XVector**)itAll;//获取路径
	//	gotoxy(0, 0);
	//	XMazePrint(maze, "■", "  ");
	//	printf("当前方案有%d个点\n", XVector_size(path));
	//	XDelay(1000);
	//	XMazePathPrintSleep(maze, path, "■", "  ", "★", 100);
	//}
	//XVectorTwo_delete(PathAll);
	
	//XVector* Path = XMazePathfindingBFS(maze, start, dest);
	XVector* Path = XMazePathfindingAStar(maze, start, dest,true);
	system("pause");
	XMazePathPrintSleep(maze, Path, "■", "  ", "★", 100);
	XVector_delete_base(Path);
	XMazeDelete(maze);
#else
	IS_ON_DEBUG(XVectorTwo_ON);
#endif
}
#endif