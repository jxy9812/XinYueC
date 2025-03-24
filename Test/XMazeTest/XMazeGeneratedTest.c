#include"Test.h"
#if DemoTest
#include"XMazeGeneratedDF.h"
void XMazeGeneratedTest()
{
	struct XVector* maze= XMazeGenerated(50, 50,1,1,true);
	XMazePrint(maze,"■","  ");
	XMazeFree(maze);
}
#endif