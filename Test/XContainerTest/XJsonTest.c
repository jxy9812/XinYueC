#include"XDataStructTest.h"
#if DEMOTEST
#include"XString.h"
#include"XJsonArray.h"
#include"XJsonValue.h"
void XJsonTest()
{
	XJsonValue* value= XJsonValue_create_null();
	XJsonArray* array= XJsonArray_create();

	XJsonValue_setDouble(value,100.0);
	XJsonArray_append_move_base(array,value);

	XJsonValue_setDouble(value, 100.9999);
	XJsonArray_append_move_base(array, value);

	XJsonValue_setString_utf8(value, "100.9999");
	XJsonArray_append_move_base(array, value);
	
	XJsonValue_setNull(value);
	XJsonArray_append_move_base(array, value);

	XJsonValue_setBool(value,true);
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


	XString* str= XJsonArray_toString(array);
	XPrint(str);

	XJsonArray_delete_base(array);
	XJsonValue_delete(value);
}

#endif