#include"XVector_iterator.h"
void XVector_iterator_for_each(struct XVector* this_vector, void (*forFund)(void* LPVal))
{
	for (XVector_iterator* it = XVector_begin(this_vector); it != XVector_end(this_vector); it = XVector_iterator_add(this_vector, it))
	{
		forFund(it);
	}
}