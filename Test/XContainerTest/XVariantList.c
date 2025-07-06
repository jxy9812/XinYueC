#include"XDataStructTest.h"
#if DEMOTEST
#include"XVariant.h"
void XVariantList()
{
	XVariant* var = XVariant_create_int(8);

	printf("%d\n",XVariant_toInt(var));
}

#endif