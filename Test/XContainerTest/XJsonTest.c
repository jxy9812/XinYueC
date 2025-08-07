#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
#include"XJsonObject.h"
#include"XJsonArray.h"
#include"XJsonValue.h"
void XJsonObjectTest()
{
	while (true)
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

		/*{
			XJsonValue_setArray(value, array);
			XJsonArray_append_move_base(array, value);
		}*/
		XJsonValue_setDouble(value, 6666);
		XJsonObject_insert_utf8_move(object, "数字", value);

		XJsonValue_setArray(value, array);
		XJsonObject_insert_utf8_move(object, "数组", value);

		XJsonValue_setString_utf8(value, "测试");
		XJsonObject_insert_utf8_move(object, "字符串", value);




		XString* str = XJsonObject_toString(object);
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


		XString* str = XJsonArray_toString(array);
		XPrint(str);
		printf("\n");
		XString_delete_base(str);

		XJsonArray_delete_base(array);
		XJsonValue_delete(value);
	}
}

#endif