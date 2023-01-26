#include"Test.h"
#include"XMazePathfindingDepthFirst.h"
#include"XMazeGeneratedDepthFirst.h"
#include"XStack.h"
void XMazePathfinding()
{
	struct XVector* maze = XMazeGenerated(10, 10);
	XMazePrint(maze, "■", "  ","*");
	XPoint start = { 1,1 };
	XPoint dest = { 8,8 };
	XStack* PathAll = XMazePathfindingAll(maze, start, dest);
	while (!XStack_empty(PathAll))
	{
		XPoint CurPoint = *(XPoint*)XStack_top(PathAll);//获取栈顶保存的点
		XStack_pop(PathAll);
		printf("(%d,%d)\n", CurPoint.x, CurPoint.y);
	}
	//XMazePrint(maze, "■", "  ", "**");
	XMazeFree(maze);
}