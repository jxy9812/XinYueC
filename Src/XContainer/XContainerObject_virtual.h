#ifndef XCONTAINEROBJECT_VIRTUAL_H
#define XCONTAINEROBJECT_VIRTUAL_H
#include<stdio.h>
#include<stdbool.h>
typedef struct XContainerObject XContainerObject;
bool XVContainerObject_empty(const XContainerObject* Object);
size_t XVContainerObject_size(const XContainerObject* Object);
size_t XVContainerObject_capacity(const  XContainerObject* Object);
size_t XVContainerObject_type(const XContainerObject* Object);
void XVContainerObject_swap(XContainerObject* ObjectOne, XContainerObject* ObjectTwo);
void XVContainerObject_free(XContainerObject* Object);
#endif // !XCONTAINEROBJECT_VIRTUAL_H
