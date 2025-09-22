#include"XDataStructTest.h"
#if DEMOTEST
#include"XVariantList.h"
#include"XString.h"
#include"XStringList.h"
#include"XMap.h"
#include"XHashMap.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
void XVariantListTest()
{
	XPrintf_utf8("--------------------------XVariantList测试-----------------------\n");
	//while (true)
	{
		XVariantList* list = XVariantList_create();
		XVariant* var = XVariant_create_int(8);
		XVariantList_push_back_base(list,var);
		XVariant_setValue_int(var,80);
		XVariantList_push_back_base(list, var);
		XVariant_setValue_utf8_str(var, "9000");
		
		XVariantList_push_back_base(list, var);
		XPrintf("当前类型:%s\n",XVariant_typeName(var));

		XVariant* find = XVariant_create_int(8);
		int64_t index = XVector_indexOf(list,find,0);
		if (index!=-1)
			XPrintf("找到了index:%d\n",index);
		XVariant_delete_base(find);

		XVariant_setValue_double(var, 100.0);

		XPrintf("当前类型:%s\n", XVariant_typeName(var));
		XVariant_setValue_bool(var, true);
		XPrintf("当前类型:%s\n", XVariant_typeName(var));
		XPrintf("%d\n", XVariant_toInt(var));

		XVariant_setValue_utf8_str(var,"你好");
		XPrintf("当前类型:%s\n", XVariant_typeName(var));
		XString* str= XVariant_toString(var);
		if (str)
		{
			XPrintf("%s\n",XString_toUtf8(str));
			XString_delete_base(str);
		}

		XVariant_setValue_utf8_str(var,"1000");
	
		XPrintf("%d\n", XVariant_toInt(var));

		XVariant_delete_base(var);
		
		XVariantList_delete_base(list);
	}
	XCoreApplication_quit();
}
void XVariantListTest_list()
{
	//while(true)
	{
		XPrintf_utf8("--------------------------XVariant_toList测试-----------------------\n");
		XVariantList* list = XVariantList_create();
		XVariant* var = XVariant_create_int(8);
		XVariantList_push_back_base(list, var);
		XVariant_setValue_int(var, 80);
		XVariantList_push_back_base(list, var);
		XVariant_setValue_utf8_str(var, "9000");
		XVariantList_push_back_base(list, var);
		XVariant_delete_base(var);

		XVariant* varList = XVariant_create_list(list);
		XPrintf("当前类型:%s\n", XVariant_typeName(varList));
		XVariantList* toList = XVariant_toList(varList);
		if (toList)
		{
			XPrintf("有%d个元素\n", XVariantList_size_base(toList));
			XVariant* temp = NULL;
			for_each_iterator(toList, XVariantList, it)
			{
				temp = XVariantList_iterator_data(&it);
				XPrintf("%d\n", XVariant_toInt(temp));
			}
			XVariantList_delete_base(toList);
		}

		XVariant_delete_base(varList);
		XVariantList_delete_base(list);
	}
	XCoreApplication_quit();
}
void XVariantListTest_map()
{
	//while (true)
	{
		XPrintf_utf8("--------------------------XVariant_toMap测试-----------------------\n");
		//XMap* map = XMap_create_XVariantMap();
		XVariantHashMap* map = XHashMap_create_XVariantHashMap();
		{
			XString_Init_Utf8(str, "6666");
			XVariant* v = XVariant_create_int(9999);
			XMapBase_insert_move_base(map, str, v);
			XVariant_delete_base(v);
			XString_deinit_base(str);
		}
		{
			XString* str = XString_create_utf8("111");
			XVariant* v = XVariant_create_int(6666);
			XMapBase_insert_move_base(map, str, v);
			XString_delete_base(str);
			XVariant_delete_base(v);
		}
		XVariant* varMap = XVariant_create_hash(map);
		XPrintf("当前类型:%s\n", XVariant_typeName(varMap));
		XMapBase_delete_base(map);
		map = XVariant_toHash(varMap);
		for_each_iterator(map, XHashMap, it)
		{
			XPair* p = XHashMap_iterator_data(&it);
			XString* str = XPair_first(p);
			XVariant* var = XPair_second(p);
			XPrintf("key:%s val:%d\n", XString_c_str(str), XVariant_toInt(var));
		}
		XMap_delete_base(map);
		XVariant_delete_base(varMap);
	}
	XCoreApplication_quit();
}
void XVariantListTest_stringList()
{
	//while (true)
	{
		XPrintf_utf8("--------------------------XVariant_toStringList测试-----------------------\n");
		XStringList* list = XStringList_create();
		XStringList_push_back_utf8(list, "你好");
		XStringList_push_back_utf8(list, "非常好");
		XStringList_push_back_utf8(list, "世界");
		XStringList_insert_utf8(list, 0, "彩虹猫");
		XStringList_push_front_utf8(list, "星小白");
		XVariant* var = XVariant_create_StringList(list);
		XStringList_delete_base(list);
		list=XVariant_toStringList(var);
		XVariant_delete_base(var);
		for_each_iterator(list, XStringList, it)
		{
			printf("%s \n", XString_c_str(XStringList_iterator_data(&it)));
		}
		XStringList_delete_base(list);
	}
	XCoreApplication_quit();
}
void XMenu_XVariantListTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XVariantList(变体数组)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XVariantListTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "list测试");
		XAction_setAction(action, XVariantListTest_list);
	}
	{
		XAction* action = XMenu_addAction(menu, "map测试");
		XAction_setAction(action, XVariantListTest_map);
	}
	{
		XAction* action = XMenu_addAction(menu, "stringList测试");
		XAction_setAction(action, XVariantListTest_stringList);
	}
}
#endif