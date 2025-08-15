#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
#include"XByteArray.h"
#include"XBsonArray.h"
#include"XBsonValue.h"
#include"XBsonDocument.h"
#include"XMenu.h"
#include"XAction.h"
#include"XCoreApplication.h"
void XBsonDocumentTest()
{
	//while (true)
	{
		XBsonValue* value = XBsonValue_create_null();
		XBsonArray* array = XBsonArray_create();
		XBsonDocument* object = XBsonDocument_create();

		//XBsonValue_setInt(value, 100);
		//XBsonArray_append_move_base(array, value);

		//XBsonValue_setDouble(value, 100.9999);
		//XBsonArray_append_move_base(array, value);

		//XBsonValue_setString_utf8(value, "100.9999");
		//XBsonArray_append_move_base(array, value);

		//XBsonValue_setNull(value);
		//XBsonArray_append_move_base(array, value);

		//XBsonValue_setBool(value, true);
		//XBsonArray_append_move_base(array, value);


		//XBsonDocument_insert_keyUtf8_int(object, "数字", 66666);
		////XBsonDocument_insert_keyUtf8_object(object,"对象", object);

		//XBsonValue_setObject(value, object);
		//XBsonArray_append_move_base(array, value);

		//XBsonDocument_insert_keyUtf8_array(object, "数组", array);
		//XBsonDocument_insert_keyUtf8_utf8(object, "字符串", "测试");

		//XBsonDocument_insert_keyUtf8_object(object, "嵌套", object);
		/*XBsonDocument_insert_keyUtf8_object(object, "嵌套1", object);
		XBsonDocument_insert_keyUtf8_object(object, "嵌套3", object);*/


		
		//printf("\n\n\n\n\n\n");
		//XBsonDocument* doc = XBsonDocument_create_object(object);
		/*XByteArray* json = XBsonDocument_toJson(doc, XBsonDocument_Indented);
		XBsonDocument_delete(doc);
		XPrint_utf8(XContainerDataPtr(json));
		XPrint_utf8("\n开始从json文本转json对象\n");
		doc = XBsonDocument_fromJson(json);
		XByteArray_delete_base(json);*/

		/*XString* str = XBsonDocument_toString(doc, XBsonDocument_Indented);
		XPrint(str);
		printf("\n");
		XString_delete_base(str);
		XBsonDocument_delete(doc);*/

		XBsonArray_delete_base(array);
		XBsonValue_delete(value);
		XBsonDocument_delete_base(object);
	}
	XCoreApplication_requestQuit();
}
void XBsonArrayTest()
{
	//while (true)
	{
		XBsonValue* value = XBsonValue_create_null();
		XBsonArray* array = XBsonArray_create();

		XBsonValue_setDouble(value, 100.0);
		XBsonArray_append_move_base(array, value);

		XBsonValue_setDouble(value, 100.9999);
		XBsonArray_append_move_base(array, value);

		//XBsonValue_setString_utf8(value, "100.9999");
		XBsonArray_append_move_base(array, value);

		XBsonValue_setNull(value);
		XBsonArray_append_move_base(array, value);

		XBsonValue_setBool(value, true);
		XBsonArray_append_move_base(array, value);

		{
			/*XBsonArray* a = XBsonArray_create();
			XBsonValue_setBool(value, false);
			XBsonArray_append_move_base(a, value);
			XBsonValue_setString_utf8(value, "123\n");
			XBsonArray_append_move_base(a, value);*/


			XBsonValue_setArray(value, array);
			XBsonArray_append_move_base(array, value);
		}


		/*XString* str = XBsonArray_toString(array, XBsonDocument_Indented);
		XPrint(str);
		printf("\n");
		XString_delete_base(str);*/

		XBsonArray_delete_base(array);
		XBsonValue_delete(value);
	}
	XCoreApplication_requestQuit();
}
void XMenu_XBsonTest(XMenu* root)
{
	XMenu* menu = XMenu_create("XBson(Bson)");
	XMenu_addMenu(root, menu);
	{
		XAction* action = XMenu_addAction(menu, "XBsonDocument");
		XAction_setAction(action, XBsonDocumentTest);
	}
	{
		XAction* action = XMenu_addAction(menu, "XBsonArray");
		XAction_setAction(action, XBsonArrayTest);
	}
}
#endif