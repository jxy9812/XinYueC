#include"XDataStructTest.h"
#if DEMOTEST
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
#if XVector_ON
	printf("XVector 测试\n");
	XVector* v = XVector_New(int);
	v->m_equality = XEquality_int;
	//XVector_resize(v,11);
	int arr[]={100,123,456,4,8496,3,321,23,3,132,0};
	
	
	for (size_t i = 0; i < sizeof(arr)/sizeof(arr[0]); i++)
	{
		int n = arr[i];
		XVector_push_front(v,arr+i);
	}
	XVector_append_array(v, arr, sizeof(arr) / sizeof(arr[0]));
	printf("插入数据\t"); XVector_iterator_for_each(v, XFor_each_int, NULL); printf("\n");
	XVector_remove(v, 2, 1);
	printf("删除数据\t"); XVector_iterator_for_each(v, XFor_each_int, NULL); printf("\n");
	XVector_sort(v, XLess_int);
	printf("排序数据\t"); XVector_iterator_for_each(v, XFor_each_int, NULL); printf("\n");
	int findVal = 100;
	int* findRet=XVector_find(v, &findVal);
	if(findRet!=NULL)
	printf("找到的数字:%d", *findRet);
	XVector_free(v);
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}

#endif