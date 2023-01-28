//迷宫深度优先搜索寻路算法
#ifndef XMAZEPATHFINDINGDEPTHFIRST_H
#define XMAZEPATHFINDINGDEPTHFIRST_H
#include"XMaze.h"
#include"XPoint.h"
//带步数
typedef struct XPointStep
{
	int x;//列
	int y;//行
	int cur;//当前步数
}XPointStep;
//迷宫寻路-程序时间最少不保证最短，找到一条返回
XVector* XMazePathfindingOneDFS(const XVector* maze, const XPoint start, const XPoint dest);
//迷宫寻路-最短路径-单条
XVector* XMazePathfindingShortDFS(const XVector* maze,const XPoint start,const XPoint dest);
//迷宫寻路-全部方案
XVector* XMazePathfindingAllDFS(const XVector* maze, const XPoint start, const XPoint dest);
#endif // !XMazePathfindingDepthFirst_H
