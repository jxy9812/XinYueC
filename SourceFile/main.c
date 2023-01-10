#include"list.h"
#include"vector.h"
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
ListTest()
{
	list* li = Newlist(sizeof(int));
	int num[] = {1,5,7,9,10,100,456,97,123,45,12,34,56};
	for (size_t i = 0; i <sizeof(num)/sizeof(num[0]); i++)
	{
		li->push_front(li,num+i);//头插
	}
	/*int x = 100;
	li->insert_front_int(li,0, &x,10);*/
	/*int x = 100;
	li->insert_front_p(li, li->find(li,num+2), &x, 4);*/
	/*int arr[5] = { 123,12,1,4,9 };
	li->insert(li,li->find(li,num+10), &arr[0], &arr[4]);*/
	printf("元素遍历\n");
	for (size_t i = 0; i < li->size(li); i++)
	{
		printf("%d\n", *(int*)li->at(li, i)->date);
	}
	printf("头元素为：%d\n", *(int*)li->front(li)->date);
	printf("尾元素为：%d\n", *(int*)li->back(li)->date);
	/*int findn = 10;
	printf("找到的元素为：%d\n", *(int*)li->find(li,&findn)->date);*/
	
	li->sort(li, mysort);
	printf("排序后元素后遍历\n");
	for (size_t i = 0; i < li->size(li); i++)
	{
		printf("%d\n", *(int*)li->at(li, i)->date);
	}
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
	li->clear(li);
	free(li);
}
test02()//交换函数测试
{
	list* li1 = Newlist(sizeof(int));
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

	list* li2 = Newlist(sizeof(int));

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
	vector* v=NewVector("people",sizeof(struct people));
	struct people p1 ={22, "男", "琦神","大佬"};
	Vector_Push_Back(v, &p1);
	struct people p2 = {19, "男", "小白","大佬"};
	Vector_Push_Back(v, &p2);
	for (size_t i = 0; i < Vector_size(v); i++)
	{
		struct people* p = Vector_at(v,i);
		printf("%s %s %d岁 是%s\n", p->name, p->gender, p->age,p->achievement);
	}
		
}
int main()
{
	//ListTest();
	VectorTest();

	return 0;
}