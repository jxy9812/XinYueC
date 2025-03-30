#include"XDataStructTest.h"
#if DEMOTEST
typedef struct XVector XVector;
#include"XMazeGeneratedDF.h"
void XMazeGeneratedTest()
{
	XVector* maze= XMazeGenerated(50, 50,1,1,true);
	XMazePrint(maze,"■","  ");
	XMazeFree(maze);
}
#endif