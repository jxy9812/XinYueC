// XHostInfo.c
#include "XHostInfo.h"
#include "XHostInfo_p.h"
#include "XMemory.h"
#include "XString.h"
#include "XListSLinked.h"
#include <string.h>

static XHostInfoPrivate* XHostInfoPrivate_create(void) {
    return (XHostInfoPrivate*)XMemory_calloc(1, sizeof(XHostInfoPrivate));
}

static void XHostInfoPrivate_destroy(XHostInfoPrivate* d) {
    if (!d) return;
    XMemory_free(d->hostName);
    if (d->addresses) {
        for (int i = 0; i < d->addressCount; i++) {
            XHostAddress_delete_base(&d->addresses[i]);
        }
        XMemory_free(d->addresses);
    }
    XMemory_free(d->errorString);
    XMemory_free(d);
}

// Virtual functions
static void VXHostInfo_copy(XHostInfo* self, const XHostInfo* other) {
    if (!self || !other) return;
    if (self->d) XHostInfoPrivate_destroy(self->d);
    if (other->d) {
        self->d = XHostInfoPrivate_create();
        if (other->d->hostName)
            self->d->hostName = XStrdup(other->d->hostName);
        self->d->error = other->d->error;
        if (other->d->errorString)
            self->d->errorString = XStrdup(other->d->errorString);
        if (other->d->addresses && other->d->addressCount > 0) {
            self->d->addresses = (XHostAddress*)XMemory_malloc(other->d->addressCount * sizeof(XHostAddress));
            for (int i = 0; i < other->d->addressCount; i++) {
                self->d->addresses[i] = *XHostAddress_create_copy(&other->d->addresses[i]);
            }
            self->d->addressCount = other->d->addressCount;
        }
        self->d->lookupId = other->d->lookupId;
    }
}

static void VXHostInfo_deinit(XHostInfo* self) {
    if (self && self->d) {
        XHostInfoPrivate_destroy(self->d);
        self->d = NULL;
    }
}

XVtable* XHostInfo_class_init(void) {
    static XVtable* vtable = NULL;
    if (!vtable) {
        XVTABLE_CREAT_DEFAULT
            XVTABLE_HEAP_INIT_DEFAULT
            XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHostInfo_copy);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHostInfo_deinit);
        vtable = XVTABLE_DEFAULT;
    }
    return vtable;
}

// Public API
XHostInfo* XHostInfo_create(void) {
    XHostInfo* info = (XHostInfo*)XMemory_malloc(sizeof(XHostInfo));
    XHostInfo_init(info);
    return info;
}

XHostInfo* XHostInfo_create_copy(const XHostInfo* other) {
    XHostInfo* info = XHostInfo_create();
    if (info && other) XClass_copy_base(info, other);
    return info;
}

void XHostInfo_init(XHostInfo* info)
{
    if (info) {
        memset(info, 0, sizeof(XHostInfo));
        XClass_init(info);
        XClassGetVtable(info) = XHostInfo_class_init();
        info->d = XHostInfoPrivate_create();
    }
}

// --- 属性实现 ---
const char* XHostInfo_hostName(const XHostInfo* info) {
    return (info && info->d && info->d->hostName) ? info->d->hostName : "";
}

void XHostInfo_setHostName(XHostInfo* info, const char* name) {
    if (!info || !info->d) return;
    XMemory_free(info->d->hostName);
    info->d->hostName = name ? XStrdup(name) : NULL;
}

const XHostAddress* XHostInfo_addresses(const XHostInfo* info, int* count) {
    if (count) *count = 0;
    if (!info || !info->d) return NULL;
    if (count) *count = info->d->addressCount;
    return info->d->addresses;
}

void XHostInfo_setAddresses(XHostInfo* info, const XHostAddress* addrs, int count) {
    if (!info || !info->d) return;
    if (info->d->addresses) {
        for (int i = 0; i < info->d->addressCount; i++) {
            XHostInfo_delete_base((XHostInfo*)&info->d->addresses[i]); // Actually XHostAddress_delete_base
        }
        XMemory_free(info->d->addresses);
        info->d->addresses = NULL;
        info->d->addressCount = 0;
    }
    if (addrs && count > 0) {
        info->d->addresses = (XHostAddress*)XMemory_malloc(count * sizeof(XHostAddress));
        for (int i = 0; i < count; i++) {
            info->d->addresses[i] = *XHostAddress_create_copy(&addrs[i]);
        }
        info->d->addressCount = count;
    }
}

XHostInfo_Error XHostInfo_error(const XHostInfo* info) {
    return (info && info->d) ? info->d->error : XHostInfo_UnknownError;
}

void XHostInfo_setError(XHostInfo* info, XHostInfo_Error error) {
    if (info && info->d) info->d->error = error;
}

const char* XHostInfo_errorString(const XHostInfo* info) {
    return (info && info->d && info->d->errorString) ? info->d->errorString : "";
}

void XHostInfo_setErrorString(XHostInfo* info, const char* str) {
    if (!info || !info->d) return;
    XMemory_free(info->d->errorString);
    info->d->errorString = str ? XStrdup(str) : NULL;
}

int XHostInfo_lookupId(const XHostInfo* info) {
    return (info && info->d) ? info->d->lookupId : -1;
}

void XHostInfo_setLookupId(XHostInfo* info, int id) {
    if (info && info->d) info->d->lookupId = id;
}

// --- Static functions ---
XHostInfo* XHostInfo_fromName(const char* name) {
    return XHostInfo_platform_fromName(name);
}

char* XHostInfo_localDomainName(void) {
    // Standard C does not provide a reliable way to get domain name
    return NULL;
}

// --- Async lookup ---
static int global_lookup_id = 1;

int XHostInfo_lookupHost(const char* name, XHostInfo_Callback callback, void* userData) {
    if (!name) return -1;
    XHostInfo_LookupTask* task = XHostInfo_task_create(name, global_lookup_id++);
    task->callback = callback;
    task->userData = userData;
    XMutex_lock(g_XHostInfo_task_mutex);
    XListSLinked_push_back_base(g_XHostInfo_pending_tasks, &task);
    XMutex_unlock(g_XHostInfo_task_mutex);
    return task->lookupId;
}

int XHostInfo_lookupHost_toObject(const char* name, XObject* receiver, size_t member) {
    if (!name || !receiver || !member) return -1;
    XHostInfo_LookupTask* task = XHostInfo_task_create(name, global_lookup_id++);
    task->receiver = receiver;
    task->member = member;
    XMutex_lock(g_XHostInfo_task_mutex);
    XListSLinked_push_back_base(g_XHostInfo_pending_tasks, &task);
    XMutex_unlock(g_XHostInfo_task_mutex);
    return task->lookupId;
}

void XHostInfo_abortHostLookup(int lookupId) {
    XMutex_lock(g_XHostInfo_task_mutex);
    XListSNode* node = XContainerDataPtr(g_XHostInfo_pending_tasks);
    while (node) {
        XHostInfo_LookupTask** ptask = (XHostInfo_LookupTask**)XListSNode_DataPtr(node);
        if (*ptask && (*ptask)->lookupId == lookupId) {
            (*ptask)->aborted = true;
            break;
        }
        node = node->next;
    }
    XMutex_unlock(g_XHostInfo_task_mutex);
}
#if defined(_WIN32)
//#include <ws2tcpip.h>
#endif
char* XHostInfo_localHostName(void) {
#if defined(_WIN32)
    //char name[256] = { 0 };
    //DWORD size = sizeof(name);
    //if (GetComputerNameA(name, &size)) {
    //    return XStrdup(name);
    //}
#elif defined(__unix__) || defined(__APPLE__)
    char name[256] = { 0 };
    if (gethostname(name, sizeof(name)) == 0) {
        return XStrdup(name);
    }
#endif
    return NULL;
}