#include"XQueue.h"
#if PATT==LIST
#include"XQueue_head.h"
#include<stdlib.h>
#include<string.h>
static struct XListNode
{
	void* date;
	struct XListNode* prev;//指向上一个
	struct XListNode* next;//指向下一个
};
typedef struct XListNode XListNode;

//开辟新的节点
static XListNode* open(XQueue* que)
{
	XQUEUE* queue=(XQUEUE*)que;
	XListNode* newNode = NULL;
	if (queue->_data == NULL && queue->_size == 0)//无元素
	{
		newNode = malloc(sizeof(XListNode));//新节点
		queue->_data = newNode;
		if (queue->_data == NULL)
		{
			perror("初始化queue失败");
			exit(-1);
		}
		else
		{
			newNode->next = newNode;//头节点均指向自己
			newNode->prev = newNode;
			newNode->date = malloc(queue->_type);//为节点数据开辟空间储存
			if (newNode->date == NULL)
				printf("开辟数据空间的时候失败\n");
			queue->_size++;
		}
	}
	else
	{
		newNode = malloc(sizeof(XListNode));//新节点
		XListNode* head = queue->_data;//头节点
		XListNode* tail = head->prev;//原尾节点
		newNode->next = head;//新节点下一个指向头节点
		newNode->prev = tail;//新节点上一个指向原尾节点
		head->prev = newNode;//头节点上一个指向新节点
		tail->next = newNode;//原尾节点下一个指向新节点
		newNode->date = malloc(queue->_type);//为节点数据开辟空间储存
		if (newNode->date == NULL)
			printf("开辟数据空间的时候失败\n");
		queue->_size++;
	}
	return newNode;
}
void XQueue_clear(XQueue* que)//清空queue的队列，释放内存
{
	XQUEUE* queue=(XQUEUE*)que;
	if (queue->_data != NULL && queue->_size != 0)//无元素
	{
		XListNode* p = queue->_data;//开始指向头节点
		XListNode* pnext = NULL;
		for (size_t i = 0; i < queue->_size; i++)
		{
			pnext = p->next;//临时保存下一个节点地址
			free(p->date);//释放节点的数据空间
			free(p);//释放节点
			p = pnext;//p指向下一个节点
		}
		queue->_data = NULL;
		queue->_current = 0;
		queue->_size = 0;
	}
}

void XQueue_Push(XQueue* que, void*LpValue)//插入到队列的队尾
{
	XQUEUE* queue=(XQUEUE*)que;
	XListNode* p = open(que);
	memcpy(p->date, LpValue, queue->_type);
	queue->_current++;
}
void XQueue_pop(struct XQueue* que)//删除queue的队头元素
{
	XQUEUE* queue=(XQUEUE*)que;
	if (queue->_current > 1)
	{
		XListNode* head = queue->_data;
		XListNode* next = head->next;
		free(head->date);
		free(head);
		queue->_data = next;
		queue->_current--;
	}
	else if (queue->_current == 1)
	{
		XListNode* head = queue->_data;
		free(head->date);
		free(head);
		queue->_data = NULL;
		queue->_current--;
	}
}
void* XQueue_front(struct XQueue* que)// 返回队列的队头元素指针，但不删除该元素
{
	XQUEUE* queue=(XQUEUE*)que;
	return ((XListNode*)queue->_data)->date;
}
void* XQueue_back(struct XQueue* que)// 返回队列的队尾元素指针，但不删除该元素
{
	XQUEUE* queue=(XQUEUE*)que;
	return ((XListNode*)queue->_data)->prev->date;
}
bool XQueue_empty(struct XQueue* que)//检测队内是否为空，空为真 O(1)
{
	XQUEUE* queue=(XQUEUE*)que;
	return !queue->_current;
}
int XQueue_size(struct XQueue* que)//返回queue内元素的个数 O(1)
{
	XQUEUE* queue=(XQUEUE*)que;
	return queue->_current;
}
//释放队列
void XQueue_free(struct XQueue* this_queue)
{
	XQueue_clear(this_queue);
	free(this_queue);
}
#endif


