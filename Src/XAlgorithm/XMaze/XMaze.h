#ifndef XMAZEGENERATED_H
#define XMAZEGENERATED_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XVector.h"
//迷宫地图元素
enum XMazeSign
{
	XMazeWall,//迷宫墙壁
	XMazeRoute,//迷宫道路
	XMazePath//可行路径
};
//方向
enum XMazeDirection
{
	Up,//上
	Right,//右
	Down,//下
	Left,//左
	UpLeft,//上左
	UpRight,//上右
	DownRight,//下右
	DownLeft,//下左
};
//初始化迷宫
struct XVector* XMaze_init(const size_t r, const size_t l);
//打印路径点
void XMazePathPrintPoint(XVector* Path);
//打印迷宫 wall墙(替换的字符) Route道路(替换的字符)
void XMazePrint(const struct XVector* maze, const char* Wall, const char* Route);
//打印迷宫路径 wall墙(替换的字符) Route道路(替换的字符) Path路径
void XMazePathPrint(const struct XVector* maze, XVector* mazePath,const char* Wall, const char* Route, const char* Path);
//打印迷宫路径 wall墙(替换的字符) Route道路(替换的字符) Path路径 毫秒延迟动画方式
void XMazePathPrintSleep(const struct XVector* maze, XVector* mazePath, const char* Wall, const char* Route, const char* Path,const size_t msec);
//释放迷宫
void XMazeFree(const struct XVector* maze);
//返回迷宫行数
const int XMazeRow(const struct XVector* maze);
//返回迷宫列数
const int XMazeList(const struct XVector* maze);
#ifdef __cplusplus
}
#endif
#endif// !XMAZE_H
