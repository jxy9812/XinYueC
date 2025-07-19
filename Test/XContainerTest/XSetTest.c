#include"XDataStructTest.h"
#if DEMOTEST
#include"XSet.h"
#include"XEquality.h"
#include"XLess.h"
#include"XBalancedBinaryTree.h"

static void XFor_each_pair(void* LPVal, void* args)
{
	int val = *((int*)LPVal);
	printf("key:%d\n", val);
}
void XSetTest()
{
#if XHashMap_ON
	printf("XSet 测试\n");
	//while (true)
	{
		int arrayint[] = { 1,23,456,5,23 };
		XSet* set = XSet_Create(int, XEquality_int, XLess_int);

		for (size_t i = 0; i < 5; i++)
		{
			XSet_insert_base(set, arrayint + i);
		}
		for (int i = 0; i < 5; i++)
		{
			XSet_insert_base(set, &i);
		}
		printf("当前Set容器内数据数量:%d\n", XSet_getSize_base(set));

		XSet_iterator_for_each(set, XFor_each_pair, NULL);

		XSet_remove_base(set, arrayint + 2);
		printf("当前Set容器内数据数量:%d\n", XSet_getSize_base(set));
		XSet_iterator_for_each(set, XFor_each_pair, NULL);

		if (XSet_find_base(set, arrayint + 1))
			printf("查询到:key:%d \n", arrayint[1]);
		XSet_clear_base(set);
		XSet_delete_base(set);
	}

#endif
}
#endif