#include "XSetBase.h"
#if XSet_ON
XVtable* XSetBase_class_init()
{
    return XContainerObject_class_init();
}

void XSetBase_init(XSetBase* this_set, const size_t keyTypeSize, XEquality KeyEquality, XLess KeyLess)
{
    if (ISNULL(this_set, ""))
        return;
    if (keyTypeSize == 0)
    {
        printf("类型参数不能为0");
        return;
    }
    if (KeyEquality == NULL)
    {
        printf("KeyEquality相等比较函数NULL");
        return;
    }
    XContainerObject_init(&this_set->m_parent, keyTypeSize);
    XClassGetVtable(this_set) = XSetBase_class_init();
    XContainerTypeSize(this_set)= keyTypeSize;
    this_set->m_KeyEquality = KeyEquality; 
    this_set->m_KeyLess = KeyLess;
}

bool XSetBase_insert_base(XSetBase* this_set, const void* pvKey)
{
    if (ISNULL(this_set, "") || ISNULL(pvKey, "") || ISNULL(XClassGetVtable(this_set), ""))
        return false;
    return XClassGetVirtualFunc(this_set, EXSetBase_Insert, bool(*)(XSetBase*, const void*, XCDataCreatMethod))(this_set, pvKey, XContainerDataCopyMethod(this_set));
}

bool XSetBase_insert_move_base(XSetBase* this_set, const void* pvKey)
{
    if (ISNULL(this_set, "") || ISNULL(pvKey, "") || ISNULL(XClassGetVtable(this_set), ""))
        return false;
    return XClassGetVirtualFunc(this_set, EXSetBase_Insert, bool(*)(XSetBase*, const void*, XCDataCreatMethod))(this_set, pvKey, XContainerDataMoveMethod(this_set));
}

void XSetBase_erase_base(XSetBase* this_set, const void* pvKey)
{
    if (ISNULL(this_set, "") || ISNULL(pvKey, "") || ISNULL(XClassGetVtable(this_set), ""))
        return;
    XClassGetVirtualFunc(this_set, EXSetBase_Erase, void(*)(XSetBase*, const void*))(this_set, pvKey);
}

bool XSetBase_remove_base(XSetBase* this_set, const void* pvKey)
{
    if (ISNULL(this_set, "") || ISNULL(pvKey, "") || ISNULL(XClassGetVtable(this_set), ""))
        return false;
    return XClassGetVirtualFunc(this_set, EXSetBase_Remove, bool(*)(XSetBase*, const void*))(this_set, pvKey);
}

bool XSetBase_find_base(XSetBase* this_set, const void* pvKey)
{
    if (ISNULL(this_set, "") || ISNULL(pvKey, "") || ISNULL(XClassGetVtable(this_set), ""))
        return false;
    return XClassGetVirtualFunc(this_set, EXSetBase_Find, bool(*)(XSetBase*, const void*))(this_set, pvKey);
}

bool XSetBase_contains(XSetBase* this_set, const void* pvKey)
{
    return XSetBase_find_base(this_set,pvKey);
}

XVector* XSetBase_keys_base(const XSetBase* this_set)
{
    if (ISNULL(this_set, "") || ISNULL(XClassGetVtable(this_set), ""))
        return NULL;
    return XClassGetVirtualFunc(this_set, EXSetBase_Keys, void* (*)(const XSetBase*))(this_set);
}

#endif