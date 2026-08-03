// XHostInfo.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only
#include "XHostInfo.h"
#include "XNetwork.h"
#include "XMemory.h"
#include "XString.h"
#include "XVector.h"
#include "XMutex.h"
#include "XThread.h"
#include "XThreadPool.h"
#include "XTypes.h"
#include "XVarList.h"
#include "XAtomic.h"
#include "XHashMap.h"
//#include "XHashFunc.h"
#include "XDateTime.h"
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

// ======================== DNS缓存前向声明 ========================
struct XString;
struct XHostInfo;

static XHostInfo* getFromCache(const XString* hostName);
static void putToCache(const XString* hostName, const XHostInfo* info);

// ======================== 虚函数 ========================

static void VXHostInfo_copy(XHostInfo* self, const XHostInfo* src) {
    if (!self || !src) return;
    // 检查目标对象是否已初始化，如果未初始化则先初始化
    if (XClassIsVtableNull(self))
        XHostInfo_init(self);
    
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
    if (XClassIsVtableNull(self))
        XHostInfo_init(self);
    
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

void XHostInfo_setErrorString(XHostInfo* info, const XString* str) 
{
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
    
    /* 尝试从缓存获取 */
    XHostInfo* cached = getFromCache(name);
    if (cached) {
        return cached;
    }
    
    XHostInfo* info = XHostInfo_create();
    XHostInfo_setHostName(info, name);
    
    /* 调用 XNetwork 平台函数进行 DNS 查询 */
    XNetwork_ensureInit();
    
    XVector* addrVec = XNetwork_lookupName(name);
    if (!addrVec) {
        XHostInfo_setError(info, XHostInfo_HostNotFound);
        int errCode = XNetwork_lastError();
        char* errStr = XNetwork_errorString(errCode);
        if (errStr) {
            info->errorString = XString_create_utf8(errStr);
            XFree_System(errStr);
        }
        return info;
    }
    
    /* 将结果添加到 addresses 向量 */
    size_t addrCount = XVector_size_base(addrVec);
    for (size_t i = 0; i < addrCount; i++) {
        XHostAddress* addr = (XHostAddress*)XVector_at_base(addrVec, (int64_t)i);
        XVector_push_back_1_base(info->addresses, addr);
    }
    
    /* 释放 XVector */
    XVector_delete_base(addrVec);
    
    /* 存入缓存 */
    putToCache(name, info);
    
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
    return XNetwork_localHostName();
}

XString* XHostInfo_localDomainName(void) {
    /* 应用层实现：从本地主机名提取域名 */
    XNetwork_ensureInit();
    XString* hostname = XNetwork_localHostName();
    if (!hostname) return NULL;
    
    /* 查找第一个点号，提取域名部分 */
    const char* utf8 = XString_toUtf8(hostname);
    const char* dot = strchr(utf8, '.');
    XString* result = NULL;
    if (dot && dot[1] != '\0') {
        /* 跳过点号，返回域名部分 */
        result = XString_create_utf8(dot + 1);
    }
    
    XString_delete_base(hostname);
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

// ======================== DNS缓存实现 ========================

/**
 * @brief DNS缓存条目结构
 */
typedef struct XHostInfoCacheEntry {
    XHostInfo* info;           /**< 缓存的主机信息 */
    int64_t timestamp;         /**< 缓存时间戳（秒） */
} XHostInfoCacheEntry;

/**
 * @brief DNS缓存管理器结构
 */
typedef struct XHostInfoCacheManager {
    XHashMap* cache;           /**< 缓存哈希表 */
    XMutex* mutex;             /**< 缓存互斥锁 */
    XAtomic_int32_t initialized; /**< 初始化状态 */
    bool enabled : 1;          /**< 是否启用缓存（位域） */
    unsigned int ttl : 31;     /**< 缓存TTL（秒），最大约68年（位域） */
} XHostInfoCacheManager;

/* 全局缓存管理器 */
static XHostInfoCacheManager g_cacheManager = {
    .cache = NULL,
    .mutex = NULL,
    .initialized = {0},
    .enabled = true,
    .ttl = 60  /* 默认60秒 */
};

/**
 * @brief 初始化DNS缓存
 */
static void ensureCacheInit(void) {
    int32_t expected = 0;
    if (XAtomic_compare_exchange_strong_int32(&g_cacheManager.initialized, &expected, 1,
            XAtomic_MemoryOrder_Acquire, XAtomic_MemoryOrder_Relaxed)) {
        g_cacheManager.mutex = XMutex_create(XLock_NonRecursive);
        /* 使用XHashMap_Create宏创建缓存，键为XString，值为XHostInfoCacheEntry */
        g_cacheManager.cache = XHashMap_Create(XString, XHostInfoCacheEntry, XString_compare);
        XAtomic_store_int32(&g_cacheManager.initialized, 2, XAtomic_MemoryOrder_Release);
    } else if (expected == 1) {
        while (XAtomic_load_int32(&g_cacheManager.initialized, XAtomic_MemoryOrder_Acquire) != 2) {
            /* 等待初始化完成 */
        }
    }
}

/**
 * @brief 检查缓存条目是否过期
 */
static bool isCacheEntryExpired(const XHostInfoCacheEntry* entry) {
    if (!entry || g_cacheManager.ttl <= 0) return false;  /* TTL为0表示永不过期 */
    
    int64_t currentTime = XDateTime_currentSecsSinceEpoch();
    return (currentTime - entry->timestamp) > g_cacheManager.ttl;
}

/**
 * @brief 从缓存获取主机信息
 * @return 如果缓存命中且未过期，返回拷贝的XHostInfo；否则返回NULL
 */
static XHostInfo* getFromCache(const XString* hostName) {
    if (!g_cacheManager.enabled || !hostName) return NULL;
    
    ensureCacheInit();
    
    XMutex_lock(g_cacheManager.mutex);
    
    XHashMap_iterator it;
    if (XHashMap_find_base(g_cacheManager.cache, hostName, &it)) {
        XPair* pair = XHashMap_iterator_data(&it);
        XHostInfoCacheEntry* entry = (XHostInfoCacheEntry*)XPair_second(pair);
        if (entry && entry->info) {
            /* 检查是否过期 */
            if (!isCacheEntryExpired(entry)) {
                /* 缓存命中，返回拷贝 */
                XHostInfo* result = XHostInfo_create_copy(entry->info);
                XMutex_unlock(g_cacheManager.mutex);
                return result;
            }
            /* 已过期，移除缓存 */
            XHostInfo_delete_base(entry->info);
            XHashMap_erase_base(g_cacheManager.cache, &it, NULL);
        }
    }
    
    XMutex_unlock(g_cacheManager.mutex);
    return NULL;
}

/**
 * @brief 将主机信息存入缓存
 */
static void putToCache(const XString* hostName, const XHostInfo* info) {
    if (!g_cacheManager.enabled || !hostName || !info) return;
    if (info->error != XHostInfo_NoError) return;  /* 不缓存失败的查询 */
    
    ensureCacheInit();
    
    XMutex_lock(g_cacheManager.mutex);
    
    /* 检查是否已存在 */
    XHashMap_iterator it;
    if (XHashMap_find_base(g_cacheManager.cache, hostName, &it)) {
        /* 更新现有条目 */
        XPair* pair = XHashMap_iterator_data(&it);
        XHostInfoCacheEntry* existing = (XHostInfoCacheEntry*)XPair_second(pair);
        if (existing) {
            if (existing->info) {
                XHostInfo_delete_base(existing->info);
            }
            existing->info = XHostInfo_create_copy(info);
            existing->timestamp = XDateTime_currentSecsSinceEpoch();
        }
    } else {
        /* 创建新条目 */
        XHostInfoCacheEntry newEntry;
        newEntry.info = XHostInfo_create_copy(info);
        newEntry.timestamp = XDateTime_currentSecsSinceEpoch();
        XHashMap_insert_base(g_cacheManager.cache, hostName, &newEntry);
    }
    
    XMutex_unlock(g_cacheManager.mutex);
}

/* 预取回调函数 */
static void prefetchCallback(XHostInfo* result, void* userData) {
    (void)userData;  /* 未使用 */
    if (result && result->error == XHostInfo_NoError) {
        putToCache(result->hostName, result);
    }
    if (result) {
        XHostInfo_delete_base(result);
    }
}

/* ==================== DNS缓存管理API ==================== */

void XHostInfo_setCacheEnabled(bool enabled) {
    ensureCacheInit();
    XMutex_lock(g_cacheManager.mutex);
    g_cacheManager.enabled = enabled;
    if (!enabled) {
        /* 禁用时清空缓存 */
        XHashMap_clear_base(g_cacheManager.cache);
    }
    XMutex_unlock(g_cacheManager.mutex);
}

bool XHostInfo_isCacheEnabled(void) {
    return g_cacheManager.enabled;
}

void XHostInfo_setCacheTtl(int ttlSeconds) {
    ensureCacheInit();
    XMutex_lock(g_cacheManager.mutex);
    g_cacheManager.ttl = ttlSeconds;
    XMutex_unlock(g_cacheManager.mutex);
}

int XHostInfo_cacheTtl(void) {
    return g_cacheManager.ttl;
}

void XHostInfo_clearCache(void) {
    if (!g_cacheManager.mutex) return;
    
    XMutex_lock(g_cacheManager.mutex);
    
    /* 遍历并释放所有缓存条目 */
    XHashMap_iterator it = XHashMap_begin(g_cacheManager.cache);
    while (!XHashMap_iterator_isEnd(&it)) {
        XPair* pair = XHashMap_iterator_data(&it);
        XHostInfoCacheEntry* entry = (XHostInfoCacheEntry*)XPair_second(pair);
        if (entry && entry->info) {
            XHostInfo_delete_base(entry->info);
            entry->info = NULL;
        }
        XHashMap_iterator_add(g_cacheManager.cache, &it);
    }
    
    XHashMap_clear_base(g_cacheManager.cache);
    XMutex_unlock(g_cacheManager.mutex);
}

void XHostInfo_removeFromCache(const XString* hostName) {
    if (!hostName || !g_cacheManager.mutex) return;
    
    XMutex_lock(g_cacheManager.mutex);
    
    /* 使用迭代器查找并删除 */
    XHashMap_iterator it;
    if (XHashMap_find_base(g_cacheManager.cache, hostName, &it)) {
        XPair* pair = XHashMap_iterator_data(&it);
        XHostInfoCacheEntry* entry = (XHostInfoCacheEntry*)XPair_second(pair);
        if (entry && entry->info) {
            XHostInfo_delete_base(entry->info);
        }
        XHashMap_erase_base(g_cacheManager.cache, &it, NULL);
    }
    
    XMutex_unlock(g_cacheManager.mutex);
}

void XHostInfo_prefetchName(const XString* hostName) {
    if (!hostName || XString_size_base(hostName) == 0) return;
    
    /* 检查缓存是否已存在 */
    XHostInfo* cached = getFromCache(hostName);
    if (cached) {
        XHostInfo_delete_base(cached);
        return;  /* 已缓存，无需预取 */
    }
    
    /* 异步查询并存入缓存 */
    XHostInfo_lookupHost(hostName, prefetchCallback, NULL);
}
