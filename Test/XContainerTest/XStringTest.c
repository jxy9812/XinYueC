#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
void XStringTest()
{
	printf("XString 测试\n");
	XString* str = XString_new();
	XString_append(str, "nihao ");
	XString_push_front(str, '#');
	XString_push_back(str, '!');
	XString_insert(str,2,"12121ni_");
	printf("%s\t char:%c\n", XString_data(str),XString_at(str,0));
	XString_pop_front(str);
	XString_pop_back(str);
	//XString_assign(str,"你好吗！");
	//XString_clear(str);
	////XString_append(str, "  666\r\n");
	////printf("字符数量%d\n", XString_size(str));
	//XString_assign(str, "草泥马");
	//printf("字符数量%d\n", XString_size(str));
	//XString_append(str, "你好呀");

	//XString_erase(str, 3, 3);
	//printf("字符数量%d\n", XString_size(str));
	////XString_erase(str, 0, 4);
	printf("%s\n", XString_data(str));
}

#endif