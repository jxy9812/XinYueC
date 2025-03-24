#ifndef XMAZEPATHFINDINGOBJECT_H
#define XMAZEPATHFINDINGOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XMaze.h"
#include"XVector.h"
#include"XPoint.h"
#include"XStack.h"
//带步数
typedef struct XPointStep
{
	int x;//列
	int y;//行
	int cur;//当前步数
}XPointStep;
//判断当前点周围是否还有通道可以经过
bool isPass(const XVector* maze, XPoint CurPoint);
//探路四个方向-栈(周围是否已经无路可走了),并把能走的点保存到栈
bool Pathfinder(struct XStack* stack, struct XVector* maze, struct XPointStep CurPoint);
//探寻周围能斜着的点
size_t PathfinderOblique(struct XStack* stack, struct XVector* maze, struct XPointStep CurPoint);
#ifdef __cplusplus
}
#endif
#endif// !XMAZEPATHFINDINGOBJECT_H
