#include"XMaze.h"
#include"XPoint.h"
#include"XAlgorithm.h"
#include<string.h>
#ifdef _WIN32
#include<Windows.h>
void gotoxy(short x, short y) {
	COORD coord = { x, y };
	SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}
#endif // _Win32



//打印路径点
void XMazePathPrintPoint(XVector* Path)
{
	for (XVector_iterator* it = XVector_begin(Path); it != XVector_end(Path); it = XVector_iterator_add(Path, it))
	{
		XPoint CurPoint = *(XPoint*)it;//获取点
		printf("(%d,%d) ", CurPoint.x, CurPoint.y);
	}
	printf("\n");
}
//打印迷宫
static void Print(const struct XVector* maze, const char* Wall, const char* Route, const char* Path)
{
	for (XVector_iterator* it = XVector_begin(maze); it != XVector_end(maze); it = XVector_iterator_add(maze, it))
	{
		struct XVector* RowMaze = *((struct XVector**)it);
		for (XVector_iterator* Lit = XVector_begin(RowMaze); Lit != XVector_end(RowMaze); Lit = XVector_iterator_add(RowMaze, Lit))
		{
			int* Sign = Lit;
			char str[10];
			switch (*Sign)
			{
			case XMazeWall:
				strcpy(str, Wall);
				break;
			case XMazeRoute:
				strcpy(str, Route);
				break;
			case XMazePath:
				strcpy(str, Path);
				break;
			default:
				strcpy(str, " ");
				break;
			}
			printf("%s", str);
		}
		printf("\n");
	}
}
//初始化迷宫
struct XVector* XMaze_init(const size_t r, const size_t l)
{
	struct XVector* maze = XVector_init("struct XVector*", sizeof(struct XVector*));
	for (size_t i = 0; i < r; i++)
	{
		struct XVector* Lmaze = XVector_init("int", sizeof(int));
		for (size_t j = 0; j < l; j++)
		{
			int Sign = XMazeWall;
			XVector_Push_Back(Lmaze, &Sign);
		}
		XVector_Push_Back(maze, &Lmaze);
	}
	return maze;
}
//打印迷宫 wall墙(替换的字符) Route道路(替换的字符)  Path路径(替换的字符) 
void XMazePrint(const struct XVector* maze, const char* Wall, const char* Route)
{
	Print(maze, Wall, Route, "  ");
}

void XMazePathPrint(const XVector* maze, XVector* mazePath, const char* Wall, const char* Route, const char* Path)
{
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	for (XVector_iterator* it = XVector_begin(mazePath); it != XVector_end(mazePath); it = XVector_iterator_add(mazePath, it))
	{
		XPoint CurPoint = *(XPoint*)it;//获取点
		*(int*)XVectorTwo_at_XPoint(tempMaze, CurPoint)= XMazePath;
	}
	Print(tempMaze, Wall, Route, Path);
	XVectorTwo_free(tempMaze);
}

void XMazePathPrintSleep(const XVector* maze, XVector* mazePath, const char* Wall, const char* Route, const char* Path, const size_t msec)
{
	system("mode con cols=110 lines=55"); //cols为控制台的宽度，lines则代表控制台的高度。
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	for (XVector_iterator* it = XVector_begin(mazePath); it != XVector_end(mazePath); it = XVector_iterator_add(mazePath, it))
	{
		XPoint CurPoint = *(XPoint*)it;//获取点
		*(int*)XVectorTwo_at_XPoint(tempMaze, CurPoint) = XMazePath;
		//清屏
#ifdef _WIN32
		gotoxy(0, 0);
#else
		system("clear");
#endif
		//打印
		Print(tempMaze, Wall, Route, Path);
		//延迟
		XDelay(msec);
	}
	XVectorTwo_free(tempMaze);
}

void XMazeFree(const struct XVector* maze)
{
	XVectorTwo_free(maze);
}

const int XMazeRow(const XVector* maze)
{
	return XVectorTwo_Row(maze);//行
}

const int XMazeList(const XVector* maze)
{
	return XVectorTwo_List(maze,0);//列
}
