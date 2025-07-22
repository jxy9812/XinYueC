#include"XDataStructTest.h"
#if DEMOTEST
#include"XVector.h"
#include"XFunctionCallback.h"
#include"XEquality.h"
#include"XLess.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
//动态数组测试
static void XVectorTest();

static void XFor_each_int(void* LPVal)
{
	printf("%d \n", *(int*)LPVal);
}
struct people
{
	int age;
	char gender[10];
	char name[20];
	char achievement[20];
};

void XVectorTest()
{
#if XVector_ON
	printf("XVector 测试\n");
	XVector* v = XVector_Create(int);
	v->m_equality = XEquality_int;
	//XVector_resize_base(v,11);
	int arr[]={100,123,456,4,8496,3,321,23,3,132,0};
	
	
	for (size_t i = 0; i < sizeof(arr)/sizeof(arr[0]); i++)
	{
		int n = arr[i];
		XVector_Push_Front_Base(v,int,arr[i]);
	}
	//printf("当前Size:%d\n",XVector_getSize_base(v)); 
	XVector_Push_Front_Base(v,int,9999);
	//XVector_append_array_base(v, arr, sizeof(arr) / sizeof(arr[0]));
	/*printf("插入数据\t"); XVector_iterator_for_each(v, XFor_each_int, NULL); printf("\n");
	XVector_remove_base(v, 2, 10);
	printf("删除数据\t"); XVector_iterator_for_each(v, XFor_each_int, NULL); printf("\n");
	XVector_sort_base(v, XLess_int);*/
	printf("排序数据\t"); XVector_iterator_for_each(v, XFor_each_int, NULL); printf("\n");
	int findVal = 100;
	int64_t index =XVector_find_base(v, &findVal);
	if(index !=-1)
		printf("找到的数字,索引号:%d\n", index);
	XVector_iterator_for_each(v, XFor_each_int, NULL); printf("\n");
	for (XVector_iterator it = XVector_begin(v), endIt = XVector_end(v); !XVector_iterator_equality(&it, &endIt);)
	{
		void* pValue=XVector_iterator_data(&it);
		//printf("%d \n", *(int*)pValue);
		if (*((int*)pValue) == 23)
			XVector_erase_base(v, &it,&it);
		else
			XVector_iterator_add(v, &it);
	}
	XVector_iterator_for_each(v, XFor_each_int, NULL); printf("\n");
	XVector_delete_base(v);


#else
	IS_ON_DEBUG(XVector_ON);
#endif
	XCoreApplication_requestQuit();
}
void XMenu_XVectorTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XVector(数组)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XVectorTest);
	}
}
#endif