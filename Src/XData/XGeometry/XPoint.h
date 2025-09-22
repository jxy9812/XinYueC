#ifndef XPOINT_H
#define XPOINT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include<stdio.h>
typedef struct XPoint
{
	int x;
	int y;
}XPoint;

int32_t XPoint_compare(const XPoint* lhs, const XPoint* rhs);

#ifdef __cplusplus
}
#endif
#endif // ! XPOINT_H
