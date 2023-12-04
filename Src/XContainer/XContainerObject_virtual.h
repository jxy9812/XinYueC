#ifndef XCONTAINEROBJECT_VIRTUAL_H
#define XCONTAINEROBJECT_VIRTUAL_H
#include<stdio.h>
#include<stdbool.h>
typedef struct XContainerObject XContainerObject;
bool VXContainerObject_empty(const XContainerObject* Object);
size_t VXContainerObject_size(const XContainerObject* Object);
size_t VXContainerObject_capacity(const  XContainerObject* Object);
size_t VXContainerObject_type(const XContainerObject* Object);
void VXContainerObject_swap(XContainerObject* ObjectOne, XContainerObject* ObjectTwo);
void VXContainerObject_clear(XContainerObject* Object);
void VXContainerObject_free(XContainerObject* Object);
#endif // !XCONTAINEROBJECT_VIRTUAL_H
