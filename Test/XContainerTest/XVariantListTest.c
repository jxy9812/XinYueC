#include"XDataStructTest.h"
#if DEMOTEST
#include"XVariantList.h"
#include"XString.h"
#include"XMap.h"
void XVariantListTest()
{
	printf("--------------------------XVariantList测试-----------------------\n");
	while (true)
	{
		XVariantList* list = XVariantList_create();
		XVariant* var = XVariant_create_int(8);
		XVariantList_push_back_base(list,var);
		XVariantList_push_back_base(list, XVariant_create_int(80));
		XVariantList_push_back_base(list, XVariant_create_str("9000"));
		printf("当前类型:%s\n",XVariant_typeName(var));

		XVariant* find = XVariant_create_int(8);
		XVariant* ret=XVariantList_find_base(list,find);
		if (ret)
			printf("找到了:%p\n",ret);
		XVariant_delete(find);

		XVariant_setValue_double(var, 100.0);
		printf("当前类型:%s\n", XVariant_typeName(var));
		XVariant_setValue_bool(var, true);
		printf("当前类型:%s\n", XVariant_typeName(var));
		printf("%d\n", XVariant_toInt(var));

		XVariant_setValue_str(var,"你好");
		printf("当前类型:%s\n", XVariant_typeName(var));
		XString* str= XVariant_toString(var);
		if (str)
		{
			printf("%s\n",XString_c_str(str));
			XString_delete_base(str);
		}

		XVariant_setValue_str(var,"1000");
	
		printf("%d\n", XVariant_toInt(var));

		printf("--------------------------XVariant_toList测试-----------------------\n");
		{
			XVariant* varList = XVariant_create_list(list);
			printf("当前类型:%s\n", XVariant_typeName(varList));
			XVariantList* l = XVariant_toList(varList);
			if (l)
			{
				printf("有%d个元素\n", XVariantList_getSize_base(l));
				XVariant* temp = NULL;
				for_each_iterator(l, XVariantList, it)
				{
					temp = XVariantList_iterator_data(&it);
					printf("%d\n", XVariant_toInt(temp));
				}
				XVariantList_delete_base(l);
			}

			XVariant_delete(varList);
		}
		printf("--------------------------XVariant_toMap测试-----------------------\n");
		{
			XMap* map = XMap_create_XStringVariant();
			{
				XString* str = XString_create("6666");
				XVariant* v = XVariant_create_int(9999);
				XMap_insert_base(map, &str, &v);
			}
			{
				XString* str = XString_create("111");
				XVariant* v = XVariant_create_int(6666);
				XMap_insert_base(map, &str, &v);
			}
			XVariant* varMap = XVariant_create_XMap(map);
			printf("当前类型:%s\n", XVariant_typeName(varMap));
			XMap_delete_base(map);
			map=XVariant_toMap(varMap);
			for_each_iterator(map,XMap,it)
			{
				XPair* p=XMap_iterator_data(&it);
				XString* str=XPair_First(p,XString*);
				XVariant* var = XPair_Second(p, XVariant*);
				printf("key:%s val:%d\n",XString_c_str(str),XVariant_toInt(var));
			}
			XMap_delete_base(map);
			XVariant_delete(varMap);
		}
		XVariantList_delete_base(list);
	}
}

#endif