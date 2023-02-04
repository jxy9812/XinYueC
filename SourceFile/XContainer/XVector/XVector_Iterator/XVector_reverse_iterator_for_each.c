#include "XVector_Iterator/XVector_reverse_iterator.h"
void XVector_reverse_iterator_for_each(struct XVector* this_vector, XFor_each ForFunction)
{
	for (XVector_reverse_iterator* it = XVector_rbegin(this_vector); it != XVector_rend(this_vector); it = XVector_reverse_iterator_add(this_vector, it))
	{
		ForFunction(it);
	}
}