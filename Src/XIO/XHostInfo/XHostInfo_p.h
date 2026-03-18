// XHostInfo_p.h
// Internal header for XHostInfo implementation.
#ifndef XHOSTINFO_P_H
#define XHOSTINFO_P_H

#include "XHostInfo.h"
#include "XMutex.h"
#include "XListSLinked.h"
#include "XThread.h"
// PIMPL 结构
typedef struct XHostInfoPrivate {
    char* hostName;
    XHostAddress* addresses;
    int addressCount;
    XHostInfo_Error error;
    char* errorString;
    int lookupId;
} XHostInfoPrivate;

// DNS 查询任务结构
typedef struct XHostInfo_LookupTask {
    char* name;
    XHostInfo_Callback callback;
    void* userData;
    XObject* receiver;
    size_t member; // e.g., "hostResolved(XHostInfo*)"  接收者调用响应的槽函数
    int lookupId;
    bool aborted;
} XHostInfo_LookupTask;

// Platform-specific DNS resolution function
XHostInfo* XHostInfo_platform_fromName(const char* name);

// Global DNS thread management
extern XThread* g_XHostInfo_dns_thread;
extern XMutex* g_XHostInfo_task_mutex;
extern XListSLinked* g_XHostInfo_pending_tasks;
extern volatile bool g_XHostInfo_dns_thread_quit;

void XHostInfo_dns_worker(void* arg);
XHostInfo_LookupTask* XHostInfo_task_create(const char* name, int id);
void XHostInfo_task_destroy(XHostInfo_LookupTask* task);

#endif // XHOSTINFO_P_H