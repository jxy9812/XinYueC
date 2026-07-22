#ifndef XMAZEPATHFINDINGASTAR_H
#define XMAZEPATHFINDINGASTAR_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct XVector XVector;
#include<stdbool.h>
#include"XMaze.h"
#include"XGui/XGuiTypes.h"

#define StraightLine  10//直线
#define ObliqueLine  14//斜线
typedef struct AStarNode
{
	XPoint pos;//坐标
	struct BFSNode* parent;//父节点
	XVector* child;//子节点
	size_t currentCosts;//当前代价 
	size_t estimateCosts;//预估代价
}AStarNode;
//迷宫寻路-A星寻路
XVector* XMazePathfindingAStar(const XVector* maze, const XPoint start, const XPoint dest,bool Oblique);
#ifdef __cplusplus
}
#endif
#endif // !XMazePathfindingAStar_H
