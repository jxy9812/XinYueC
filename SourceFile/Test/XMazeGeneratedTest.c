#include"Test.h"
#include"XMazeGeneratedDepthFirst.h"
void XMazeGeneratedTest()
{
	struct XVector* maze= XMazeGenerated(50, 50);
	XMazePrint(maze,"■","  ");
	XMazeFree(maze);
}