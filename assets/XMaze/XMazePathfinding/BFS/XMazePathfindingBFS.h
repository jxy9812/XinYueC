//迷宫广度优先搜索寻路算法
#ifndef XMAZEPATHFINDINGBFS_H
#define XMAZEPATHFINDINGBFS_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XMaze.h"
#include"XPoint.h"
#include"XBinaryTree.h"
//typedef struct BFSNode 
//{
//	XPoint pos;//坐标
//	//struct BFSNode* parent;//父节点
//	//XVector* child;//子节点
//}BFSNode;
//迷宫寻路-广度优先搜索寻路
XVector* XMazePathfindingBFS(const XVector* maze, const XPoint start, const XPoint dest);
#ifdef __cplusplus
}
#endif
#endif// !XMAZEPATHFINDINGBFS_H
