#include"XDataStructTest.h"
#if DEMOTEST
#include"XStringVector.h"
#include"XFunctionCallback.h"
#include"XEquality.h"
#include"XLess.h"
static void XFor_each_XString(void* LPVal, void* args)
{
	XString* string = LPVal;
	printf("%s \n",XString_c_str(string) );
}
void XStringVectorTest()
{
#if XVector_ON
	XStringVector* stringList = XStringVector_new();
	XStringVector_push_back_c_str(stringList,"你好");
	XStringVector_push_back_c_str(stringList, "非常好");
	XStringVector_push_back_c_str(stringList, "世界");
	XStringVector_iterator_for_each(stringList, XFor_each_XString,NULL);
	XStringVector_free(stringList);
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
#endif