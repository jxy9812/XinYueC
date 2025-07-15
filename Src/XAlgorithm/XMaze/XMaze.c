#include"XMaze.h"
#include"XPoint.h"
#include"XAlgorithm.h"
#include"XClass.h"
#include<string.h>
//打印路径点
void XMazePathPrintPoint(XVector* Path)
{
#if XVector_ON
	//for (XVector_iterator* it = XVector_begin(Path); it != XVector_end(Path); it = XVector_iterator_add(Path, it))
	for_each_iterator(Path, XVector, it)
	{
		XPoint CurPoint = *(XPoint*)XVector_iterator_data(&it);//获取点
		printf("(%d,%d) ", CurPoint.x, CurPoint.y);
	}
	printf("\n");
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
//打印迷宫
static void Print(const struct XVector* maze, const char* Wall, const char* Route, const char* Path)
{
#if XVector_ON
	//for (XVector_iterator* it = XVector_begin(maze); it != XVector_end(maze); it = XVector_iterator_add(maze, it))
	for_each_iterator(maze, XVector, it)
	{
		struct XVector* RowMaze = *((struct XVector**)XVector_iterator_data(&it));
		//for (XVector_iterator* Lit = XVector_begin(RowMaze); Lit != XVector_end(RowMaze); Lit = XVector_iterator_add(RowMaze, Lit))
		for_each_iterator(RowMaze, XVector, Lit)
		{
			int* Sign = XVector_iterator_data(&Lit);
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
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
//初始化迷宫
XVector* XMaze_init(const size_t r, const size_t l)
{
#if XVector_ON
	struct XVector* maze = XVector_create( sizeof(struct XVector*));
	for (size_t i = 0; i < r; i++)
	{
		struct XVector* Lmaze = XVector_create( sizeof(int));
		for (size_t j = 0; j < l; j++)
		{
			int Sign = XMazeWall;
			XVector_push_back_base(Lmaze, &Sign);
		}
		XVector_push_back_base(maze, &Lmaze);
	}
	return maze;
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif
}
//打印迷宫 wall墙(替换的字符) Route道路(替换的字符)  Path路径(替换的字符) 
void XMazePrint(const struct XVector* maze, const char* Wall, const char* Route)
{
	Print(maze, Wall, Route, "  ");
}

void XMazePathPrint(const XVector* maze, XVector* mazePath, const char* Wall, const char* Route, const char* Path)
{
#if XVector_ON
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	//for (XVector_iterator* it = XVector_begin(mazePath); it != XVector_end(mazePath); it = XVector_iterator_add(mazePath, it))
	for_each_iterator(mazePath, XVector, it)
	{
		XPoint CurPoint = *(XPoint*)XVector_iterator_data(&it);//获取点
		*(int*)XVectorTwo_at_XPoint(tempMaze, CurPoint)= XMazePath;
	}
	Print(tempMaze, Wall, Route, Path);
	XVectorTwo_delete(tempMaze);
#else
		IS_ON_DEBUG(XVector_ON);
#endif
}

void XMazePathPrintSleep(const XVector* maze, XVector* mazePath, const char* Wall, const char* Route, const char* Path, const size_t msec)
{
#if XVector_ON
	//system("mode con cols=110 lines=55"); //cols为控制台的宽度，lines则代表控制台的高度。
	XVector* tempMaze = XVectorTwo_copy(maze);//备份
	//for (XVector_iterator* it = XVector_begin(mazePath); it != XVector_end(mazePath); it = XVector_iterator_add(mazePath, it))
	for_each_iterator(mazePath, XVector, it)
	{
		XPoint CurPoint = *(XPoint*)XVector_iterator_data(&it);//获取点
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
	XVectorTwo_delete(tempMaze);
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}

void XMazeDelete(const struct XVector* maze)
{
#if XVector_ON
	XVectorTwo_delete(maze);
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}

const int XMazeRow(const XVector* maze)
{
#if XVector_ON
	return XVectorTwo_Row(maze);//行
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}

const int XMazeList(const XVector* maze)
{
#if XVector_ON
	return XVectorTwo_List(maze,0);//列
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
