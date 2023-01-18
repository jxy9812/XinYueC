#include"XList.h"
#include"XVector.h"
#include"XStack.h"
#include"XString.h"
#include"XAlgorithm.h"
#include<stdlib.h>
#include<time.h>
struct people
{
	int age;
	char gender[10];
	char name[20];
	char achievement[20];
};
bool mysort(void* x, void* y)//自定义的排序回调函数
{
	return  *(int*)x < *(int*)y;
}
bool findListInt(const struct Node* node, const void* val)
{
	return (*(int*)node->date) == (*(int*)val);
}
ListTest()
{
	XList* li = List_init(sizeof(int));
	int size = 1000000;
	srand((unsigned int)time(NULL));
	int* p1 = malloc(sizeof(int) * size);
	for (size_t i = 0; i < size; i++)
	{
		int num = rand()%1000;
		p1[i] = num;
	}
	for (size_t i = 0; i < size; i++)
	{
		li->push_front(li,p1+i);//头插
	}
	/*int x = 100;
	li->insert_front_int(li,0, &x,10);*/
	/*int x = 100;
	li->insert_front_p(li, li->find(li,num+2), &x, 4);*/
	/*int arr[5] = { 123,12,1,4,9 };
	li->insert(li,li->find(li,num+10), &arr[0], &arr[4]);*/
	/*printf("元素遍历\n");
	for (size_t i = 0; i < li->size(li); i++)
	{
		printf("%d\n", *(int*)li->at(li, i)->date);
	}*/
	/*printf("头元素为：%d\n", *(int*)li->front(li)->date);
	printf("尾元素为：%d\n", *(int*)li->back(li)->date);*/

	/*struct Node*findNode=List_find(li, findListInt, num + 5);
	printf("找到的数字%d\n", *(int*)findNode->date);*/

	/*int findn = 10;
	printf("找到的元素为：%d\n", *(int*)li->find(li,&findn)->date);*/
	clock_t  time_front = clock();
	List_sort(li, mysort);
	clock_t time_after = clock();
	printf("%d随机数，链表排序运行了%dms\n",size, time_after - time_front);
	/*printf("排序后元素后遍历\n");
	for (size_t i = 0; i < li->size(li); i++)
	{
		printf("%d\n", *(int*)li->at(li, i)->date);
	}*/

	/*li->pop_back(li);
	li->pop_back(li);
	li->pop_front(li);
	li->pop_front(li);*/
	//li->erase_p(li,li->find(li,num+1), li->find(li,num+5));
	////li->erase_int(li,1, 8);
	//printf("删除元素后遍历\n");
	//for (size_t i = 0; i < li->size(li); i++)
	//{
	//	printf("%d\n", *(int*)li->at(li, i)->date);
	//}
	li->free(li);
}
test02()//交换函数测试
{
	XList* li1 = List_init(sizeof(int));
	int num;
	
	for (size_t i = 0; i < 10; i++)
	{
		num = i;
		li1->push_front(li1, &num);
	}
	printf("li1元素遍历\n");
	for (size_t i = 0; i < li1->size(li1); i++)
	{
		printf("%d\n", *(int*)li1->at(li1, i));
	}

	XList* li2 = List_init(sizeof(int));

	for (size_t i = 0; i < 20; i++)
	{
		num = 20-i;
		li1->push_front(li2, &num);
	}
	printf("li2元素遍历\n");
	for (size_t i = 0; i < li1->size(li2); i++)
	{
		printf("%d\n", *(int*)li1->at(li2, i));
	}

	li1->swap(li1, li2);

	printf("交换后li1元素遍历\n");
	for (size_t i = 0; i < li1->size(li1); i++)
	{
		printf("%d\n", *(int*)li1->at(li1, i));
	}

	printf("交换后li2元素遍历\n");
	for (size_t i = 0; i < li1->size(li2); i++)
	{
		printf("%d\n", *(int*)li1->at(li2, i));
	}
	li1->clear(li1);
	li1->clear(li2);
}
VectorTest()
{
	XVector* v=XVector_init(" people ",sizeof(struct people));
	struct people p1 ={22, "男", "琦神","大佬"};
	XVector_Push_Back(v, &p1);
	struct people p2 = {19, "男", "小白","大佬"};
	XVector_Push_Back(v, &p2);
	int n = v->size(v);
	for (size_t i = 0; i <v->size(v); i++)
	{
		struct people* p = XVector_at(v,i);
		printf("%s %s %d岁 是%s\n", p->name, p->gender, p->age,p->achievement);
	}
	v->free(v);
}
stackTest()
{
	XStack* sInt = XStack_init("int");
	sInt->push(sInt, 1);
	sInt->push(sInt, 100);
	sInt->push(sInt, 65);
	sInt->push(sInt, 77);
	while (!sInt->empty(sInt))
	{
		printf("%d\n",sInt->top(sInt));
		sInt->pop(sInt);
	}
	XStack* string = XStack_init("char[100]");
	string->push(string, "琦神");
	string->push(string, "小白");
	string->push(string, "皮皮");
	string->push(string, "蛇蛇");
	while (!string->empty(string))
	{
		printf("%s\n", string->top(string));
		string->pop(string);
	}
}

XStringTest()
{
	XString* str = XString_init();
	XString_append(str, " ");
	XString_clear(str);
	XString_append(str, "  666\r\n");
	XString_assign(str, "草泥马");
	XString_append(str, "你好呀1");
	XString_pop_back(str);
	//XString_erase(str, 0, 4);
	printf("%s", XString_data(str));
}
int main(int argc, char* args[])
{
	//ListTest();
	//VectorTest();
	//stackTest();
	/*const char* str = "sSDSA564DSA";
	const char* fchar = "1234";
	printf("%s\n", string_find_last_of(str, fchar));
	char buf[100] = " 12113 1131 13 ";
	Unblank(buf, right);
	printf("%s\n", buf);*/
	XStringTest();
	return 0;
}