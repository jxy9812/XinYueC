#include "XStringVector_reverse_iterator.h"
#if XStringVector_ON
#include"XString.h"
#include<stdio.h>
XContainer_rbegin(XStringVector)
{
	return XVector_rbegin(this_XStringVector);
}

XContainer_rend(XStringVector)
{
	return XVector_rend(this_XStringVector);
}

XContainer_reverse_iterator_add(XStringVector)
{
	return XVector_reverse_iterator_add(this_XStringVector, it);
}

XContainer_reverse_iterator_for_each(XStringVector)
{
	for (XStringVector_reverse_iterator* it = XStringVector_rbegin(this_XStringVector); it != XStringVector_rend(this_XStringVector); it = XStringVector_reverse_iterator_add(this_XStringVector, it))
	{
		ForFunction(XContainerValue(it, XString*), args);
	}
}

#endif

