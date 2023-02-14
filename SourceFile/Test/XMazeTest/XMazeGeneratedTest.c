#include"Test.h"
#include"XMazeGeneratedDepthFirst.h"
void XMazeGeneratedTest()
{
	struct XVector* maze= XMazeGenerated(50, 50,1,1,true);
	XMazePrint(maze,"■","  ");
	XMazeFree(maze);
}