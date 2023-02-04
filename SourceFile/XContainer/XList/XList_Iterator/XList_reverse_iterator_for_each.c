#include "XList_Iterator/XList_reverse_iterator.h"
void XList_reverse_iterator_for_each(struct XList* this_list, XFor_each ForFunction)
{
	for (XList_reverse_iterator* it = XList_rbegin(this_list); it != XList_rend(this_list); it = XList_reverse_iterator_add(this_list, it))
	{
		ForFunction(it);
	}
}