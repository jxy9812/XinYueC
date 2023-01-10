#include"queue.h"
#if PATT==LIST
#include"queue_head.h"
#include<stdlib.h>
#include<string.h>
static struct List
{
	void* date;
	struct List* prev;//指向上一个
	struct List* next;//指向下一个
};
typedef struct List List;

//开辟新的节点
static List* open(QUEUE* que)
{
	List* p = NULL;
	if (que->_date == NULL && que->_size == 0)//无元素
	{
		p = malloc(sizeof(List));//新节点
		que->_date = p;
		if (que->_date == NULL)
		{
			perror("初始化queue失败");
			exit(-1);
		}
		else
		{
			p->next = p;//头节点均指向自己
			p->prev = p;
			p->date = malloc(que->_type);//为节点数据开辟空间储存
			if (p->date == NULL)
				printf("开辟数据空间的时候失败\n");
			que->_size++;
		}
	}
	else
	{
		p = malloc(sizeof(List));//新节点
		List* head = que->_date;//头节点
		List* tail = head->prev;//原尾节点
		p->next = head;//新节点下一个指向头节点
		p->prev = tail;//新节点上一个指向原尾节点
		head->prev = p;//头节点上一个指向新节点
		tail->next = p;//原尾节点下一个指向新节点
		p->date = malloc(que->_type);//为节点数据开辟空间储存
		if (p->date == NULL)
			printf("开辟数据空间的时候失败\n");
		que->_size++;
	}
	return p;
}
void Queue_clear(QUEUE* que)//清空queue的队列，释放内存
{
	if (que->_date != NULL && que->_size != 0)//无元素
	{
		List* p = que->_date;//开始指向头节点
		List* pnext = NULL;
		for (size_t i = 0; i < que->_size; i++)
		{
			pnext = p->next;//临时保存下一个节点地址
			free(p->date);//释放节点的数据空间
			free(p);//释放节点
			p = pnext;//p指向下一个节点
		}
		que->_date = NULL;
		que->_current = 0;
		que->_size = 0;
	}
}

void Queue_Push(QUEUE* que, void* x)//插入到队列的队尾
{
	List* p = open(que);
	memcpy(p->date, x, que->_type);
	que->_current++;
}
void Queue_pop(struct QUEUE* que)//删除queue的队头元素
{
	if (que->_current > 1)
	{
		List* head = que->_date;
		List* next = head->next;
		free(head->date);
		free(head);
		que->_date = next;
		que->_current--;
	}
	else if (que->_current == 1)
	{
		List* head = que->_date;
		free(head->date);
		free(head);
		que->_date = NULL;
		que->_current--;
	}
}
void* Queue_front(struct QUEUE* que)// 返回队列的队头元素指针，但不删除该元素
{
	return ((List*)que->_date)->date;
}
void* Queue_back(struct QUEUE* que)// 返回队列的队尾元素指针，但不删除该元素
{
	return ((List*)que->_date)->prev->date;
}
bool Queue_empty(struct QUEUE* que)//检测队内是否为空，空为真 O(1)
{
	return !que->_current;
}
int Queue_size(struct QUEUE* que)//返回queue内元素的个数 O(1)
{
	return que->_current;
}
#endif


