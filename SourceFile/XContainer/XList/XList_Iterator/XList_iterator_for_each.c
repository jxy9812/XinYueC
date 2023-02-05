#include "XList_iterator.h"
#include"XListNode.h"
void XList_iterator_for_each(struct XList* this_list, XFor_each ForFunction)
{
	for (XList_iterator* it = XList_begin(this_list); it != XList_end(this_list); it = XList_iterator_add(this_list, it))
	{
		ForFunction(((XListNode*)it)->date);
	}
}