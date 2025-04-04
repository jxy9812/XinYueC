#include "XStringVector_Iterator.h"
#if XStringVector_ON
#include"XString.h"
#include<stdio.h>
XContainer_begin(XStringVector)
{
	return XVector_begin(this_XStringVector);
}

XContainer_end(XStringVector)
{
	return XVector_end(this_XStringVector);
}

XContainer_iterator_add(XStringVector)
{
	return XVector_iterator_add(this_XStringVector,it);
}

XContainer_iterator_for_each(XStringVector)
{
	for (XStringVector_iterator* it = XStringVector_begin(this_XStringVector); it != XStringVector_end(this_XStringVector); it = XStringVector_iterator_add(this_XStringVector, it))
	{
		ForFunction(XContainerValue(it, XString*), args);
	}
}
#endif


