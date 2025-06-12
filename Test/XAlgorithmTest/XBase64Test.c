#include"XAlgorithmTest.h"
#include"XBase64.h"
#include"XVector.h"
void XBase64Test()
{
	printf("XBase64测试\n");
	char buff[] = "adss12313212345555555555456456";
	XVector* sour = XVector_Create(uint8_t), *toBase=NULL,* fromBase64=NULL;
	XVector_append_array_base(sour,buff,sizeof(buff));
	if (sour)
	{
		toBase = XVector_toBase64(sour);
		if (toBase)
			printf("转Base64:%s\n", XContainerDataPtr(toBase));
	}
	if (toBase)
	{
		fromBase64 = XVector_fromBase64(toBase);
		if (fromBase64)
			printf("还原Base64:%s\n", XContainerDataPtr(fromBase64));
	}

	if (sour)
		XVector_delete_base(sour);
	if (toBase)
		XVector_delete_base(toBase);
	if (fromBase64)
		XVector_delete_base(fromBase64);
}