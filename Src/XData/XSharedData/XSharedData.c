/**
 * @file XSharedData.c
 * @brief 隐式共享（Copy-On-Write）数据块实现
 */
#include "XSharedData.h"
#include "XVtable.h"
#include <string.h>
XSharedData* XSharedData_create(void* dataPtr, size_t  dataSize)
{
    XSharedData* sd = (XSharedData*)XMalloc_System(ALIGN_UP(sizeof(XSharedData)+dataSize,sizeof(void*)));
    if (sd) 
    {
        XAtomic_store_int32(&sd->refCount, 1, XAtomic_MemoryOrder_Relaxed);
        if (dataPtr)
            memcpy(sd->data, dataPtr, dataSize);
        else
            memset(sd->data,0, dataSize);
    }
    return sd;
}

void XSharedData_addRef(XSharedData* sd)
{
    if (sd)
        XAtomic_fetch_add_int32(&sd->refCount, 1, XAtomic_MemoryOrder_Relaxed);
}

bool XSharedData_release(XSharedData* sd)
{
    if (!sd) return false;
    int32_t old = XAtomic_fetch_sub_int32(&sd->refCount, 1, XAtomic_MemoryOrder_Relaxed);
    if (old <= 1) {
        XFree_System(sd);
        return true;
    }
    return false;
}

bool XSharedData_release_with(XSharedData* sd, void (*dataDeleter)(void* data, void* arg), void* arg)
{
    if (!sd) return false;
    int32_t old = XAtomic_fetch_sub_int32(&sd->refCount, 1, XAtomic_MemoryOrder_Relaxed);
    if (old <= 1) {
        if (dataDeleter && sd->data)
            dataDeleter(sd->data,arg);
        XFree_System(sd);
        return true;
    }
    return false;
}

bool XSharedData_isShared(const XSharedData* sd)
{
    if (!sd) return false;
    return XAtomic_load_int32(&sd->refCount, XAtomic_MemoryOrder_Relaxed) > 1;
}

int32_t XSharedData_refCount(const XSharedData* sd)
{
    if (!sd) return 0;
    return XAtomic_load_int32(&sd->refCount, XAtomic_MemoryOrder_Relaxed);
}
