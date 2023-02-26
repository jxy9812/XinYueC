//随机迷宫生成算法——深度优先算法
#ifndef XMAZEGENERATEDDEPTHFIRST_H
#define XMAZEGENERATEDDEPTHFIRST_H
#include"XMaze.h"
//生成迷宫r行l列,x,y起点，单出口
struct XVector* XMazeGenerated(const size_t r,const size_t l, const int x, const int y,bool oneExit);

#endif

