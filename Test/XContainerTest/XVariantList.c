#include"XDataStructTest.h"
#if DEMOTEST
#include"XVariant.h"
#include"XString.h"
void XVariantList()
{
	//while (true)
	{
		XVariant* var = XVariant_create_int(8);

		XVariant_setValue_double(var, 100.0);
		XVariant_setValue_bool(var, true);
		printf("%d\n", XVariant_toInt(var));

		XVariant_setValue_str(var,"你好");
		XString* str= XVariant_toString(var);
		if (str)
		{
			printf("%s\n",XString_c_str(str));
			XString_delete_base(str);
		}

		XVariant_setValue_str(var,"1000");
		printf("%d\n", XVariant_toSize_t(var));


		XVariant_delete(var);
	}
}

#endif