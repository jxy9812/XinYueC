//迷宫深度寻路算法
#ifndef XMAZEPATHFINDINGDEPTHFIRST_H
#define XMAZEPATHFINDINGDEPTHFIRST_H
#include"XMaze.h"
#include"XPoint.h"
//带步数
typedef struct XPointStep
{
	int x;
	int y;
	int cur;
}XPointStep;
//迷宫寻路-程序时间最少不保证最短，找到一条返回
XVector* XMazePathfindingOne(const XVector* maze, const XPoint start, const XPoint dest);
//迷宫寻路-最短路径-单条
XVector* XMazePathfindingShort(const XVector* maze,const XPoint start,const XPoint dest);
//迷宫寻路-全部方案
XVector* XMazePathfindingAll(const XVector* maze, const XPoint start, const XPoint dest);
#endif // !XMazePathfindingDepthFirst_H
