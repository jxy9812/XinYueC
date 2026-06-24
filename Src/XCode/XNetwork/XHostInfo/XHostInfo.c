// XHostInfo.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
#include "XHostInfo.h"
#include "XNetwork_platform.h"
#include "XMemory.h"
#include "XString.h"
#include "XVector.h"
#include "XMutex.h"
#include "XThread.h"
#include "XThreadPool.h"
#include "XTypes.h"
#include "XVarList.h"
#include "XAtomic.h"
#include <string.h>
#include <stdlib.h>

// ======================== 异步查询管理 ========================

typedef struct XHostInfoLookupRequest {
    bool aborted;
    int id;
    XString* hostName;
    XObject* receiver;
    union {
        void* (*signal)(XObject* receiver);
        XHostInfo_Callback callback;
    };
    void* userData;
    XMutex* mutex;
} XHostInfoLookupRequest;

static XVector* g_lookupRequests = NULL;
static XMutex* g_lookupMutex = NULL;
static XAtomic_int32_t g_lookupInitialized = {0};
static int g_nextLookupId = 1;

// ======================== 虚函数 ========================

static void VXHostInfo_copy(XHostInfo* self, const XHostInfo* src) {
    if (!self || !src) return;
    // 检查目标对象是否已初始化，如果未初始化则先初始化
    XClassEnsureVtable(self, XHostInfo);
    
    // 先释放旧资源
    if (self->hostName) {
        XString_delete_base(self->hostName);
        self->hostName = NULL;
    }
    if (self->addresses) {
        XVector_delete_base(self->addresses);
        self->addresses = NULL;
    }
    if (self->errorString) {
        XString_delete_base(self->errorString);
        self->errorString = NULL;
    }
    
    // 拷贝新资源
    if (src->hostName) {
        self->hostName = XString_create_copy(src->hostName);
    }
    
    self->addresses = XVector_create_copy(src->addresses);
    self->error = src->error;
    
    if (src->errorString) {
        self->errorString = XString_create_copy(src->errorString);
    }
    
    self->lookupId = src->lookupId;
}

static void VXHostInfo_move(XHostInfo* self, XHostInfo* src) {
    if (!self || !src) return;
    // 检查目标对象是否已初始化，如果未初始化则先初始化
    XClassEnsureVtable(self, XHostInfo);
    
    // 先释放旧资源
    if (self->hostName) {
        XString_delete_base(self->hostName);
    }
    if (self->addresses) {
        XVector_delete_base(self->addresses);
    }
    if (self->errorString) {
        XString_delete_base(self->errorString);
    }
    
    // 移动资源
    self->hostName = src->hostName;
    src->hostName = NULL;
    
    self->addresses = src->addresses;
    src->addresses = NULL;
    
    self->error = src->error;
    
    self->errorString = src->errorString;
    src->errorString = NULL;
    
    self->lookupId = src->lookupId;
    src->lookupId = -1;
}

static void VXHostInfo_deinit(XHostInfo* self) {
    if (!self) return;
    
    if (self->hostName) {
        XString_delete_base(self->hostName);
        self->hostName = NULL;
    }
    
    if (self->errorString) {
        XString_delete_base(self->errorString);
        self->errorString = NULL;
    }
    
    if (self->addresses) {
        XVector_delete_base(self->addresses);
        self->addresses = NULL;
    }
}

XVtable* XHostInfo_class_init(void) 
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XHostInfo))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_XCLASS(XClass);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHostInfo_copy);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHostInfo_move);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHostInfo_deinit);
#if SHOWCONTAINERSIZE
        printf("XHostInfo size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
        return XVTABLE_DEFAULT;
}

// ======================== 构造与析构 ====================

void XHostInfo_init(XHostInfo* info) {
    if (!info) return;
    memset(((XClass*)info) + 1, 0, sizeof(XHostInfo) - sizeof(XClass));
    XClass_init((XClass*)info);
    XClassGetVtable(info) = XHostInfo_class_init();
    info->addresses = XVector_create(sizeof(XHostAddress));
    info->error = XHostInfo_NoError;
    info->lookupId = -1;
}

XHostInfo* XHostInfo_create(void) {
    XHostInfo* info = XMalloc_System(sizeof(XHostInfo));
    if (!info) return NULL;
    XHostInfo_init(info);
    return info;
}

XHostInfo* XHostInfo_create_copy(const XHostInfo* other) {
    if (!other) return NULL;
    
    XHostInfo* info = XMalloc_System(sizeof(XHostInfo));
    if (!info) return NULL;
    
    XHostInfo_init(info);
    XHostInfo_copy_base(info, other);
    
    return info;
}

// ==================== 属性访问器 ====================

XString* XHostInfo_hostName(const XHostInfo* info) {
    if (!info) return NULL;
    return info->hostName;
}

void XHostInfo_setHostName(XHostInfo* info, const XString* name) {
    if (!info) return;
    
    if (info->hostName) {
        XString_delete_base(info->hostName);
    }
    
    if (name) {
        info->hostName = XString_create_copy(name);
    } else {
        info->hostName = NULL;
    }
}

const XVector* XHostInfo_addresses_const(const XHostInfo* info) {
    if (!info || !info->addresses) {
        return NULL;
    }
    if (XVector_isEmpty_base(info->addresses))
        return NULL;
    return info->addresses;
}

void XHostInfo_setAddresses(XHostInfo* info, const XHostAddress* addrs, int count) {
    if (!info || !info->addresses) return;
    
    XVector_clear_base(info->addresses);
    
    if (!addrs || count <= 0) return;
    
    for (int i = 0; i < count; i++) {
        XVector_push_back_1_base(info->addresses, &addrs[i]);
    }
}

XHostInfo_Error XHostInfo_error(const XHostInfo* info) {
    if (!info) return XHostInfo_UnknownError;
    return info->error;
}

void XHostInfo_setError(XHostInfo* info, XHostInfo_Error error) {
    if (!info) return;
    info->error = error;
}

XString* XHostInfo_errorString(const XHostInfo* info) {
    if (!info) return NULL;
    return info->errorString;
}

void XHostInfo_setErrorString(XHostInfo* info, const XString* str) {
    if (!info) return;
    
    if (info->errorString) {
        XString_delete_base(info->errorString);
    }
    
    if (str) {
        info->errorString = XString_create_copy(str);
    } else {
        info->errorString = NULL;
    }
}

int XHostInfo_lookupId(const XHostInfo* info) {
    if (!info) return -1;
    return info->lookupId;
}

void XHostInfo_setLookupId(XHostInfo* info, int id) {
    if (!info) return;
    info->lookupId = id;
}

// ==================== 静态工具函数 ====================

XHostInfo* XHostInfo_fromName1(const XString* name) {
    if (!name || XString_size_base(name) == 0) {
        XHostInfo* info = XHostInfo_create();
        XHostInfo_setError(info, XHostInfo_HostNotFound);
        XString* errStr = XString_create_fmt_utf8("Empty hostname");
        XHostInfo_setErrorString(info, errStr);
        XString_delete_base(errStr);
        return info;
    }
    
    XHostInfo* info = XHostInfo_create();
    XHostInfo_setHostName(info, name);
    
    // 调用 XNetwork 平台函数进行 DNS 查询
    XNetwork_ensureInit();
    
    XHostAddress* addrs = NULL;
    int count = 0;
    
    if (!XNetwork_lookupName(XString_toUtf8(name), &addrs, &count)) {
        XHostInfo_setError(info, XHostInfo_HostNotFound);
        int errCode = XNetwork_lastError();
        char* errStr = XNetwork_errorString(errCode);
        if (errStr) {
            info->errorString = XString_create_utf8(errStr);
            XFree_System(errStr);
        }
        return info;
    }
    
    // 将结果添加到 addresses 向量
    for (int i = 0; i < count; i++) {
        XVector_push_back_1_base(info->addresses, &addrs[i]);
    }
    
    // 释放 XNetwork 分配的内存
    for (int i = 0; i < count; i++) {
        XHostAddress_deinit_base(&addrs[i]);
    }
    if (addrs) {
        XFree_System(addrs);
    }
    
    return info;
}

XHostInfo* XHostInfo_fromName2(const char* name)
{
    if (!name || *name == 0) return NULL;
    XString* str = XString_create_utf8(name);
    XHostInfo* info = XHostInfo_fromName1(str);
    XString_delete_base(str);
    return info;
}

XString* XHostInfo_localHostName(void) {
    XNetwork_ensureInit();
    char* hostname = XNetwork_localHostName();
    if (hostname) {
        XString* result = XString_create_utf8(hostname);
        XFree_System(hostname);
        return result;
    }
    return NULL;
}

XString* XHostInfo_localDomainName(void) {
    /* 应用层实现：从本地主机名提取域名 */
    XNetwork_ensureInit();
    char* hostname = XNetwork_localHostName();
    if (!hostname) return NULL;
    
    /* 查找第一个点号，提取域名部分 */
    char* dot = strchr(hostname, '.');
    XString* result = NULL;
    if (dot && dot[1] != '\0') {
        /* 跳过点号，返回域名部分 */
        result = XString_create_utf8(dot + 1);
    }
    
    XFree_System(hostname);
    return result;
}

// ==================== 异步查询实现 ====================

static void ensureLookupInit(void) {
    int32_t expected = 0;
    if (XAtomic_compare_exchange_strong_int32(&g_lookupInitialized, &expected, 1,
            XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {
        g_lookupMutex = XMutex_create(XLock_NonRecursive);
        g_lookupRequests = XVector_create(sizeof(XHostInfoLookupRequest*));
        XAtomic_store_int32(&g_lookupInitialized, 2, XAtomic_MemoryOrder_Release);
    } else if (expected == 1) {
        while (XAtomic_load_int32(&g_lookupInitialized, XAtomic_MemoryOrder_Acquire) != 2) {
        }
    }
}

static XHostInfoLookupRequest* findRequest(int id) {
    if (!g_lookupRequests) return NULL;
    
    size_t size = XVector_size_base(g_lookupRequests);
    for (size_t i = 0; i < size; i++) {
        XHostInfoLookupRequest** reqPtr = (XHostInfoLookupRequest**)XVector_at_base(g_lookupRequests, i);
        if ((*reqPtr)->id == id) {
            return *reqPtr;
        }
    }
    return NULL;
}

static void removeRequest(int id) {
    if (!g_lookupRequests) return;
    
    size_t size = XVector_size_base(g_lookupRequests);
    for (size_t i = 0; i < size; i++) {
        XHostInfoLookupRequest** reqPtr = (XHostInfoLookupRequest**)XVector_at_base(g_lookupRequests, i);
        if ((*reqPtr)->id == id) {
            XHostInfoLookupRequest* req = *reqPtr;
            
            if (req->hostName) XString_delete_base(req->hostName);
            XMutex_delete(req->mutex);
            XFree_System(req);
            
            XVector_remove_base(g_lookupRequests, i, 1);
            return;
        }
    }
}

static void lookupWorker(XVarList* varlist) {
    XVarList_args_1(varlist, XHostInfoLookupRequest*, req);
    if (!req) return;
    
    XMutex_lock(req->mutex);
    bool aborted = req->aborted;
    XMutex_unlock(req->mutex);
    
    if (aborted) return;
    
    XHostInfo* result = XHostInfo_fromName1(req->hostName);
    XHostInfo_setLookupId(result, req->id);
    
    XMutex_lock(req->mutex);
    aborted = req->aborted;
    XMutex_unlock(req->mutex);
    
    if (aborted) {
        XHostInfo_delete_base(result);
        return;
    }
    
    if (req->receiver) {
        req->signal(req->receiver);
    } else if (req->callback) {
        req->callback(result, req->userData);
    } else {
        XHostInfo_delete_base(result);
    }
    
    XMutex_lock(g_lookupMutex);
    removeRequest(req->id);
    XMutex_unlock(g_lookupMutex);
}

int XHostInfo_lookupHost(const XString* name, XHostInfo_Callback callback, void* userData) {
    if (!name || XString_size_base(name) == 0) return -1;
    
    ensureLookupInit();
    
    XHostInfoLookupRequest* req = XMalloc_System(sizeof(XHostInfoLookupRequest));
    if (!req) return -1;
    
    req->hostName = XString_create_copy(name);
    req->receiver = NULL;
    req->callback = callback;
    req->userData = userData;
    req->aborted = false;
    req->mutex = XMutex_create(XLock_NonRecursive);
    
    XVarList* varlist = XVarList_Create(XVar(XHostInfoLookupRequest*, req));
    if (!varlist) {
        if (req->hostName) XString_delete_base(req->hostName);
        if (req->mutex) XMutex_delete(req->mutex);
        XFree_System(req);
        return -1;
    }
    
    XMutex_lock(g_lookupMutex);
    req->id = g_nextLookupId++;
    XVector_push_back_1_base(g_lookupRequests, &req);
    XMutex_unlock(g_lookupMutex);
    
    XThreadPool_start2(XThreadPool_globalInstance(), lookupWorker, varlist, 0);
    
    return req->id;
}

int XHostInfo_lookupHost_toObject(const XString* name, XObject* receiver, size_t member) {
    if (!name || XString_size_base(name) == 0 || !receiver || !member) return -1;
    
    ensureLookupInit();

    XHostInfoLookupRequest* req = XMalloc_System(sizeof(XHostInfoLookupRequest));
    if (!req) return -1;

    req->hostName = XString_create_copy(name);
    req->receiver = receiver;
    req->signal = member;
    req->userData = NULL;
    req->aborted = false;
    req->mutex = XMutex_create(XLock_NonRecursive);

    XVarList* varlist = XVarList_Create(XVar(XHostInfoLookupRequest*, req));
    if (!varlist) {
        if (req->hostName) XString_delete_base(req->hostName);
        if (req->mutex) XMutex_delete(req->mutex);
        XFree_System(req);
        return -1;
    }

    XMutex_lock(g_lookupMutex);
    req->id = g_nextLookupId++;
    XVector_push_back_1_base(g_lookupRequests, &req);
    XMutex_unlock(g_lookupMutex);

    XThreadPool_start2(XThreadPool_globalInstance(), lookupWorker, varlist, 0);

    return req->id;
}

void XHostInfo_abortHostLookup(int lookupId) {
    if (lookupId <= 0) return;
    if (!g_lookupMutex) return;
    
    XMutex_lock(g_lookupMutex);
    XHostInfoLookupRequest* req = findRequest(lookupId);
    if (req) {
        XMutex_lock(req->mutex);
        req->aborted = true;
        XMutex_unlock(req->mutex);
    }
    XMutex_unlock(g_lookupMutex);
}