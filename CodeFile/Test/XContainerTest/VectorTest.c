#include"Test.h"
#include"XVector.h"
#include"XFunctionCallback.h"
#include"XEquality.h"
#include"XLess.h"
void XFor_each_int(void* LPVal)
{
	printf("%d ", *(int*)LPVal);
}
struct people
{
	int age;
	char gender[10];
	char name[20];
	char achievement[20];
};

void VectorTest()
{
	/*XVector* v = XVector_init(" people ", sizeof(struct people));
	struct people p1 = { 22, "男", "琦神","大佬" };
	XVector_push_back(v, &p1);
	struct people p2 = { 19, "男", "小白","大佬" };
	XVector_push_back(v, &p2);
	int n = v->size(v);
	printf("开始正向遍历\n");
	for (XVector_iterator* it=XVector_begin(v);it!= XVector_end(v);it=XVector_iterator_add(v,it))
	{
		struct people* p = it;
		printf("%s %s %d岁 是%s\n", p->name, p->gender, p->age, p->achievement);
	}
	printf("开始反向遍历\n");
	for (XVector_reverse_iterator* it = XVector_rbegin(v); it != XVector_rend(v); it = XVector_reverse_iterator_add(v, it))
	{
		struct people* p = it;
		printf("%s %s %d岁 是%s\n", p->name, p->gender, p->age, p->achievement);
	}
	v->free(v);*/

	XVector* v = XVector_init("int", sizeof(int*));
	int arr[]={100,123,456,4,8496,3,321,23,3,132,0};
	for (size_t i = 0; i < sizeof(arr)/sizeof(arr[0]); i++)
	{
		int n = arr[i];
		XVector_push_front(v,arr+i);
	}
	XVector_iterator_for_each(v,XFor_each_int,NULL);
	printf("\n");
	XVector_sort(v, XLess_int);
	XVector_iterator_for_each(v, XFor_each_int,NULL);
	printf("\n");
	int findVal = 100;
	int* findRet=XVector_find(v, XEquality_int, &findVal);
	if(findRet!=NULL)
	printf("找到的数字:%d", *findRet);
}