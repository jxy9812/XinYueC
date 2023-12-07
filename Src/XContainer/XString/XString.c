#include"XString.h"
XString* XString_new()
{
	XString* this_string = malloc(sizeof(XString));
	XString_init(this_string);
	return this_string;
}
void XString_init(XString* this_string)
{
	if (ISNULL(this_string, "") )
		return;
	XVector_init(this_string, sizeof(char));
	ObjectVtable(this_string) = XVectorVtable;
}