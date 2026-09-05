#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
#include"XByteArray.h"
#include"XJsonObject.h"
#include"XJsonArray.h"
#include"XJsonValue.h"
#include"XJsonDocument.h"
#include"XTestMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
void XJsonObjectTest()
{
	//while (true)
	{
		XJsonValue* value = XJsonValue_create_null();
		XJsonArray* array = XJsonArray_create();
		XJsonObject* object = XJsonObject_create();

		XJsonValue_setInt(value, 100);
		XJsonArray_append_move_base(array, value);

		XJsonValue_setDouble(value, 100.9999);
		XJsonArray_append_move_base(array, value);

		XJsonValue_setString_utf8(value, "100.9999");
		XJsonArray_append_move_base(array, value);

		XJsonValue_setNull(value);
		XJsonArray_append_move_base(array, value);

		XJsonValue_setBool(value, true);
		XJsonArray_append_move_base(array, value);


		XJsonObject_insert_keyUtf8_int(object, "数字", 66666);
		XJsonObject_insert_keyUtf8_object(object,"对象", object);

		XJsonValue_setObject(value, object);
		XJsonArray_append_move_base(array, value);

		XJsonObject_insert_keyUtf8_array(object, "数组", array);
		XJsonObject_insert_keyUtf8_utf8(object, "字符串", "测试");

		XJsonObject_insert_keyUtf8_object(object, "嵌套", object);
		XJsonObject_insert_keyUtf8_object(object, "嵌套1", object);
		XJsonObject_insert_keyUtf8_object(object, "嵌套3", object);

		/*XVector* keys=XMapBase_keys_base(object);
		
		for_each_iterator(keys, XVector, it)
		{
			XString* key = XVector_iterator_data(&it);
			XPrintf_2(key);
			printf("\n");
		}
		XVector_delete_base(keys);*/

		//printf("\n\n\n\n\n\n");
		XJsonDocument* doc = XJsonDocument_create_object(object);
		XByteArray* json = XJsonDocument_toJson(doc, XJsonDocument_Indented);
		XJsonDocument_delete(doc);
		XPrintf_3(XByteArray_data(json));
		XPrintf_3("\n开始从json文本转json对象\n");
		doc = XJsonDocument_fromJson(json);
		XByteArray_delete_base(json);

		XString* str = XJsonDocument_toString(doc, XJsonDocument_Indented);
		XPrintf_2(str);
		XPrintf("\n");
		XString_delete_base(str);
		XJsonDocument_delete(doc);

		XJsonArray_delete_base(array);
		XJsonValue_delete(value);
		XJsonObject_delete_base(object);
	}
	//XCoreApplication_quit();
}
void XJsonArrayTest()
{
	//while (true)
	{
		XJsonValue* value = XJsonValue_create_null();
		XJsonArray* array = XJsonArray_create();

		XJsonValue_setDouble(value, 100.0);
		XJsonArray_append_move_base(array, value);

		XJsonValue_setDouble(value, 100.9999);
		XJsonArray_append_move_base(array, value);

		XJsonValue_setString_utf8(value, "100.9999");
		XJsonArray_append_move_base(array, value);

		XJsonValue_setNull(value);
		XJsonArray_append_move_base(array, value);

		XJsonValue_setBool(value, true);
		XJsonArray_append_move_base(array, value);

		{
			/*XJsonArray* a = XJsonArray_create();
			XJsonValue_setBool(value, false);
			XJsonArray_append_move_base(a, value);
			XJsonValue_setString_utf8(value, "123\n");
			XJsonArray_append_move_base(a, value);*/


			XJsonValue_setArray(value, array);
			XJsonArray_append_move_base(array, value);
		}


		XString* str = XJsonArray_toString(array, XJsonDocument_Indented);
		XPrintf_2(str);
		XPrintf("\n");
		XString_delete_base(str);

		XJsonArray_delete_base(array);
		XJsonValue_delete(value);
	}
	//XCoreApplication_quit();
}
void XTestMenu_XJsonTest(XTestMenu* root)
{
	XTestMenu* menu = XTestMenu_create("XJson(json)");
	XTestMenu_addMenu(root, menu);
	{
		XAction* action = XTestMenu_addAction(menu, "XJsonObject");
		XTestMenu_setActionFunction(action, XJsonObjectTest);
	}
	{
		XAction* action = XTestMenu_addAction(menu, "XJsonArray");
		XTestMenu_setActionFunction(action, XJsonArrayTest);
	}
}
#endif
