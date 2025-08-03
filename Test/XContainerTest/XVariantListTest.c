#include"XDataStructTest.h"
#if DEMOTEST
#include"XVariantList.h"
#include"XString.h"
#include"XMap.h"
#include"XHashMap.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
void XVariantListTest()
{
	XPrint_utf8("--------------------------XVariantList测试-----------------------\n");
	//while (true)
	{
		XVariantList* list = XVariantList_create();
		XVariant* var = XVariant_create_int(8);
		XVariantList_push_back_base(list,var);
		XVariant_setValue_int(var,80);
		XVariantList_push_back_base(list, var);
		XVariant_setValue_utf8_str(var, "9000");
		XVariantList_push_back_base(list, var);
		XPrint_utf8_fmt("当前类型:%s\n",XVariant_typeName(var));

		XVariant* find = XVariant_create_int(8);
		XVariant* ret=XVariantList_find_base(list,find);
		if (ret)
			XPrint_utf8_fmt("找到了:%p\n",ret);
		XVariant_delete(find);

		XVariant_setValue_double(var, 100.0);
		XPrint_utf8_fmt("当前类型:%s\n", XVariant_typeName(var));
		XVariant_setValue_bool(var, true);
		XPrint_utf8_fmt("当前类型:%s\n", XVariant_typeName(var));
		XPrint_utf8_fmt("%d\n", XVariant_toInt(var));

		XVariant_setValue_utf8_str(var,"你好");
		XPrint_utf8_fmt("当前类型:%s\n", XVariant_typeName(var));
		XString* str= XVariant_toString(var);
		if (str)
		{
			XPrint_utf8_fmt("%s\n",XString_toUtf8(str));
			XString_delete_base(str);
		}

		XVariant_setValue_utf8_str(var,"1000");
	
		XPrint_utf8_fmt("%d\n", XVariant_toInt(var));
		XVariant_delete(var);
		
		XPrint_utf8("--------------------------XVariant_toList测试-----------------------\n");
		{
			XVariant* varList = XVariant_create_List(list);
			XPrint_utf8_fmt("当前类型:%s\n", XVariant_typeName(varList));
			XVariantList* l = XVariant_toList(varList);
			if (l)
			{
				XPrint_utf8_fmt("有%d个元素\n", XVariantList_size_base(l));
				XVariant* temp = NULL;
				for_each_iterator(l, XVariantList, it)
				{
					temp = XVariantList_iterator_data(&it);
					XPrint_utf8_fmt("%d\n", XVariant_toInt(temp));
				}
				XVariantList_delete_base(l);
			}

			XVariant_delete(varList);
		}
		/*XVariantList_delete_base(list);
		continue;*/
		XPrint_utf8("--------------------------XVariant_toMap测试-----------------------\n");
		{
			//XMap* map = XMap_create_XVariantMap();
			XVariantHashMap*  map=XHashMap_create_XVariantHashMap();
			{
				XString_Init_Utf8(str,"6666");
				XVariant* v = XVariant_create_int(9999);
				XMapBase_insert_move_base(map, str, v);
				XVariant_delete(v);
				XString_deinit_base(str);
			}
			{
				XString* str = XString_create_utf8("111");
				XVariant* v = XVariant_create_int(6666);
				XMapBase_insert_move_base(map, str, v);
				XString_delete_base(str);
				XVariant_delete(v);
			}
			XVariant* varMap = XVariant_create_Hash(map);
			XPrint_utf8_fmt("当前类型:%s\n", XVariant_typeName(varMap));
			XMapBase_delete_base(map);
			map=XVariant_toHash(varMap);
			for_each_iterator(map,XHashMap,it)
			{
				XPair* p= XHashMap_iterator_data(&it);
				XString* str=XPair_first(p);
				XVariant* var = XPair_second(p);
				XPrint_utf8_fmt("key:%s val:%d\n",XString_c_str(str),XVariant_toInt(var));
			}
			XMap_delete_base(map);
			XVariant_delete(varMap);
		}
		XVariantList_delete_base(list);
	}
	XCoreApplication_requestQuit();
}
void XMenu_XVariantListTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XVariantList(变体数组)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "主测试");
		XAction_setAction(action, XVariantListTest);
	}
}
#endif