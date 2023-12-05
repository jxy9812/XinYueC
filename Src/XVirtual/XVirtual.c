#include"XVirtual.h"
#include"XVtable.h"
XVirtual_init()
{
	void* array[] = {1,2,3,4};
	XVtable* vtable= XVtable_new();
	XVtable_insertArray(vtable,0,array,4);
	XVtable_insertArray(vtable, 0, array, 4);
	for (size_t i = 0; i < XVtable_size(vtable); i++)
	{
		printf("%d\t", XVtable_at(vtable, i));
	}
	printf("\n");
}

XContainerObject_XVirtual_init()
{

}
