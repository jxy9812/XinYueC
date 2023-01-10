#include"algorithm.h"
#include<stdio.h>
#include<string.h>
void swap(void* x, void* y, const int n)//交换任意数据类型的函数
{
	void* p = malloc(n);
	if (p == NULL)
	{
		perror("交换函数创建p临时空间失败");
		exit(-1);
	}
	memcpy(p, x, n);
	memcpy(x, y, n);
	memcpy(y, p, n);
	free(p);
}