#include"XDataStructTest.h"
#if DEMOTEST
#include"XVariantList.h"
#include"XString.h"

void XVariantListTest()
{
	//while (true)
	{
		XVariantList* list = XVariantList_create();
		XVariant* var = XVariant_create_int(8);
		XVariantList_push_back_base(list,var);

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
		printf("%d\n", XVariant_toInt(var));


		//XVariant_delete(var);

		XVariantList_delete_base(list);
	}
}

#endif