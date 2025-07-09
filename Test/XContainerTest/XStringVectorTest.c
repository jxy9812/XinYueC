#include"XDataStructTest.h"
#if DEMOTEST
#include"XStringList.h"
#include"XFunctionCallback.h"
#include"XEquality.h"
#include"XLess.h"
static void XFor_each_XString(void* LPVal, void* args)
{
	XString* string = LPVal;
	//printf("测试\n");
	printf("%s \n",XString_c_str(string) );
}
void XStringListTest()
{
#if XVector_ON
	//while (true)
	{
		XStringList* stringList = XStringList_create();
		XStringList_push_back_c_str(stringList, "你好");
		XStringList_push_back_c_str(stringList, "非常好");
		XStringList_push_back_c_str(stringList, "世界");
		XString* str = XStringList_join(stringList,"-");
		if (str)
		{
			printf("连接:%s \n", XString_c_str(str));
			XString_delete_base(str);
		}
		XStringList_iterator_for_each(stringList, XFor_each_XString, NULL);
		XStringList_delete_base(stringList);
	}
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
#endif