#include"XDataStructTest.h"
#if DEMOTEST
#include"XHashSet.h"
#include"XEquality.h"
#include"XLess.h"
#include"XBalancedBinaryTree.h"

static void XFor_each_pair(void* LPVal, void* args)
{
	int val = *((int*)LPVal);
	printf("key:%d\n", val);
}
void XHashSetTest()
{
#if XHash_ON
	printf("XHashSet 测试\n");
	//while (true)
	{
		int arrayint[] = { 1,23,456,5,23 };
		XHashSet* set = XHashSet_Create(int,XHash_murmur3_32, XEquality_int);

		for (size_t i = 0; i < 5; i++)
		{
			XHashSet_insert_base(set, arrayint + i);
		}
		for (int i = 0; i < 5; i++)
		{
			XHashSet_insert_base(set, &i);
		}
		printf("当前Set容器内数据数量:%d\n", XHashSet_getSize_base(set));

		XHashSet_iterator_for_each(set, XFor_each_pair, NULL);

		XHashSet_remove_base(set, arrayint + 2);
		printf("当前Map容器内数据数量:%d\n", XHashSet_getSize_base(set));
		XHashSet_iterator_for_each(set, XFor_each_pair, NULL);

		if(XHashSet_find_base(set, arrayint + 1))
			printf("查询到:key:%d \n", arrayint[1]);
		XHashSet_clear_base(set);
		XHashSet_delete_base(set);
	}

#endif
}
#endif