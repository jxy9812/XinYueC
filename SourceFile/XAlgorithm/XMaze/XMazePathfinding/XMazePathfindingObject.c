#include"XMazePathfindingObject.h"

//判断当前点周围是否还有通道可以经过
bool isPass(const XVector* maze, XPoint CurPoint)
{
	int row = XVectorTwo_Row(maze);//行
	int list = XVectorTwo_List(maze, 0);//列
	if (CurPoint.x >= 0 && CurPoint.x < list && CurPoint.y >= 0 && CurPoint.y < row)
	{
		//获取当前位置
		int Sign = *((int*)XVectorTwo_at_XPoint(maze, CurPoint));
		if (Sign == XMazeRoute)
			return true;
	}
	return false;
}
//探路四个方向-栈(周围是否已经无路可走了)
bool Pathfinder(struct XStack* stack, struct XVector* maze, struct XPointStep CurPoint)
{
	int sum = 0;
	for (size_t i = 0; i < 4; i++)
	{
		switch (i) {
		case Left:
		{
			XPoint Point = { CurPoint.x - 1,CurPoint.y };
			if (isPass(maze, Point))
			{
				XPointStep p = { Point.x,Point.y,CurPoint.cur + 1 };
				stack->push(stack, &p);
				++sum;
			}
			break;
		}
		case Right:
		{
			XPoint Point = { CurPoint.x + 1,CurPoint.y };
			if (isPass(maze, Point))
			{
				XPointStep p = { Point.x,Point.y,CurPoint.cur + 1 };
				stack->push(stack, &p);
				++sum;
			}
			break;
		}
		case Up:
		{
			XPoint Point = { CurPoint.x,CurPoint.y - 1 };
			if (isPass(maze, Point))
			{
				XPointStep p = { Point.x,Point.y,CurPoint.cur + 1 };
				stack->push(stack, &p);
				++sum;
			}
			break;
		}
		case Down:
		{
			XPoint Point = { CurPoint.x,CurPoint.y + 1 };
			if (isPass(maze, Point))
			{
				XPointStep p = { Point.x,Point.y,CurPoint.cur + 1 };
				stack->push(stack, &p);
				++sum;
			}
			break;
		}
		default:
			break;
		}
	}
	return sum == 0 ? true : false;
}
//探寻周围能斜着的点
size_t PathfinderOblique(struct XStack* stack, struct XVector* maze, struct XPointStep CurPoint)
{
	int sum = 0;
	for (size_t i = UpLeft; i < 4+ UpLeft; i++)
	{
		switch (i) {
		case UpLeft:
		{
			XPoint Point = { CurPoint.x - 1,CurPoint.y-1 };
			if (isPass(maze, Point))
			{
				XPointStep p = { Point.x,Point.y,CurPoint.cur + 1 };
				stack->push(stack, &p);
				++sum;
			}
			break;
		}
		case UpRight:
		{
			XPoint Point = { CurPoint.x + 1,CurPoint.y-1 };
			if (isPass(maze, Point))
			{
				XPointStep p = { Point.x,Point.y,CurPoint.cur + 1 };
				stack->push(stack, &p);
				++sum;
			}
			break;
		}
		case DownRight:
		{
			XPoint Point = { CurPoint.x+1,CurPoint.y + 1 };
			if (isPass(maze, Point))
			{
				XPointStep p = { Point.x,Point.y,CurPoint.cur + 1 };
				stack->push(stack, &p);
				++sum;
			}
			break;
		}
		case DownLeft:
		{
			XPoint Point = { CurPoint.x-1,CurPoint.y + 1 };
			if (isPass(maze, Point))
			{
				XPointStep p = { Point.x,Point.y,CurPoint.cur + 1 };
				stack->push(stack, &p);
				++sum;
			}
			break;
		}
		default:
			break;
		}
	}
	return sum;
}