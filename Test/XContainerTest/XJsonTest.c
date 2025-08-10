#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
#include"XJsonObject.h"
#include"XJsonArray.h"
#include"XJsonValue.h"
void XJsonObjectTest()
{
	//while (true)
	{
		XJsonValue* value = XJsonValue_create_null();
		XJsonArray* array = XJsonArray_create();
		XJsonObject* object = XJsonObject_create();

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


		XJsonObject_insert_keyUtf8_double(object, "数字", 66666);
		XJsonObject_insert_keyUtf8_object(object,"对象", object);
		XJsonObject_insert_keyUtf8_array(object, "数组", array);
		XJsonObject_insert_keyUtf8_utf8(object, "字符串", "测试");




		XString* str = XJsonObject_toString(object, XJsonDocument_Indented);
		XPrint(str);
		printf("\n");
		XString_delete_base(str);

		XJsonArray_delete_base(array);
		XJsonValue_delete(value);
		XJsonObject_delete_base(object);
	}
}
void XJsonArrayTest()
{
	while (true)
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
		XPrint(str);
		printf("\n");
		XString_delete_base(str);

		XJsonArray_delete_base(array);
		XJsonValue_delete(value);
	}
}

#endif