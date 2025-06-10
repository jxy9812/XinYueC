#include "XStringVector_Iterator.h"
#if XStringVector_ON
#include"XString.h"
#include<stdio.h>
XStringVector_iterator XStringVector_begin(XStringVector* this_XStringVector)
{
	return XVector_begin(this_XStringVector);
}

XStringVector_iterator XStringVector_end(XStringVector* this_XStringVector)
{
	return XVector_end(this_XStringVector);
}

void XStringVector_iterator_add(XStringVector* this_XStringVector, XStringVector_iterator* it)
{
	XVector_iterator_add(this_XStringVector,it);
}

bool XStringVector_iterator_equality(XStringVector_iterator* itFirst, XStringVector_iterator* itSecond)
{
	return itFirst->data == itSecond->data;
}

void XStringVector_iterator_for_each(XStringVector* this_XStringVector, XFor_each ForFunction, void* args)
{
	if (this_XStringVector == NULL || ForFunction == NULL)
		return;
	For_Each_Iterator(this_XStringVector, XVector, it)
	{
		ForFunction(*((XString**)XStringVector_iterator_data(&it)), args);
	}
}
void* XStringVector_iterator_data(XStringVector_iterator* it)
{
	if (it == NULL || it->data == NULL)
		return NULL;
	return it->data;
}
#endif


