#include"XMaze.h"
#include<string.h>
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
//打印迷宫 wall墙(替换的字符) Route道路(替换的字符)
void XMazePrint(const struct XVector* maze, const char* Wall, const char* Route)
{
	//int r = XVector_size(maze);//行
	//int l = XVector_size(*(struct XVector**)XVector_begin(maze));//列
	for (XVector_iterator* it = XVector_begin(maze); it != XVector_end(maze); it = XVector_iterator_add(maze, it))
	{
		struct XVector* Lmaze = *((struct XVector**)it);
		for (XVector_iterator* Lit = XVector_begin(Lmaze); Lit != XVector_end(Lmaze); Lit = XVector_iterator_add(Lmaze, Lit))
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
			default:
				strcpy(str, " ");
				break;
			}
			printf("%s", str);
		}
		printf("\n");
	}
}

void XMazeFree(const struct XVector* maze)
{
	for (XVector_iterator* it = XVector_begin(maze); it != XVector_end(maze); it = XVector_iterator_add(maze, it))
	{
		struct XVector* Lmaze = *(struct XVector**)it;
		XVector_free(Lmaze);
	}
	XVector_free(maze);
}

const int XMazeRow(const XVector* maze)
{
	return XVector_size(maze);//行
}

const int XMazeList(const XVector* maze)
{
	return XVector_size(*(struct XVector**)XVector_begin(maze));//列
}
