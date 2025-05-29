#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
void XStringTest()
{
#if XString_ON
	printf("XString 测试\n");
	XString* str = XString_new("你好");
	XString_append_base(str, "111");
	XString_push_front_base(str, '#');
	XString_push_back_base(str, '!');
	XString_insert_base(str,2,"12121ni_");
	printf("%s\t char:%c\n", XString_data(str),XString_at(str,0));
	XString_pop_front_base(str);
	XString_pop_back_base(str);
	XString_assign_base(str,"你好吗！");
	XString_clear_base(str);
	//XString_append_base(str, "  666\r\n");
	//printf("字符数量%d\n", XString_size(str));
	XString_assign_base(str, "草泥马");
	printf("字符数量%d\n", XString_getSize_base(str));
	XString_append_base(str, "你好呀");

	//XString_erase_base(str, 3, 3);
	printf("字符数量%d\n", XString_getSize_base(str));
	//XString_erase_base(str, 0, 4);
	printf("%s\n", XString_data(str));
	XString_free_base(str);
#endif
}

#endif