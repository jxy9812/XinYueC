//迷宫深度寻路算法
#ifndef XMAZEPATHFINDINGDEPTHFIRST_H
#define XMAZEPATHFINDINGDEPTHFIRST_H
#include"XMaze.h"
#include"XPoint.h"
//迷宫寻路-最短路径
XVector* XMazePathfindingShort(const XVector* maze,const XPoint start,const XPoint dest);
//迷宫寻路-全部方案
XVector* XMazePathfindingAll(const XVector* maze, const XPoint start, const XPoint dest);
#endif // !XMazePathfindingDepthFirst_H
