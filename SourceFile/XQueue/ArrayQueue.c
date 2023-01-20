#include"XQueue.h"
#if PATT==ARRAY
#include"XQueue_head.h"
#include<stdlib.h>
#include<string.h>
#define VECTORNUM 4//初始数组大小
//开辟队列数组
static void open(XQUEUE* que)
{
	if (que->_data == NULL && que->_size == 0)//无元素
	{
		que->_data = malloc(que->_type * VECTORNUM);
		if (que->_data == NULL)
		{
			perror("初始化queue失败");
			exit(-1);
		}
		else
		{
			que->_size = VECTORNUM;
		}
	}
	else if (que->_size == que->_current)//空间已满需要扩容
	{
		void* _data = realloc(que->_data, que->_size * que->_type * 2);
		if (_data == NULL)
		{
			perror("扩容失败queue");
			exit(-1);
		}
		else
		{
			que->_data = _data;
			que->_size *= 2;
		}
	}
}
void XQueue_clear(XQUEUE* que)//清空queue的队列，释放内存
{
	if (que->_data != NULL && que->_size != 0)//无元素
	{
		free(que->_data);
		que->_data = NULL;
		que->_current = 0;
		que->_size = 0;
	}
}

void XQueue_Push(XQUEUE* que, void*LValue)//插入到队列的队尾
{
	open(que);
	char* str1 = (char*)que->_data + que->_type * que->_current;
	memcpy(str1, x, que->_type);
	que->_current++;
}
void XQueue_pop(struct XQUEUE* que)//删除queue的队头元素
{
	if (que->_current > 1)
	{
		char* str1 = (char*)que->_data;
		char* str2 = (char*)que->_data + que->_type * 1;
		for (size_t i = 0; i < que->_current - 1; i++)
		{
			memcpy(str1, str2, que->_type);
			str1 += que->_type;
			str2 += que->_type;
		}
		que->_current--;
	}
	else if (que->_current == 1)
	{
		que->_current--;
	}
}
void* XQueue_front(struct XQUEUE* que)// 返回队列的队头元素指针，但不删除该元素
{
	return que->_data;
}
void* XQueue_back(struct XQUEUE* que)// 返回队列的队尾元素指针，但不删除该元素
{
	char* _data = (char*)que->_data + que->_type * (que->_current - 1);
	return _data;
}
bool XQueue_empty(struct XQUEUE* que)//检测队内是否为空，空为真 O(1)
{
	return !que->_current;
}
int XQueue_size(struct XQUEUE* que)//返回queue内元素的个数 O(1)
{
	return que->_current;
}
#endif