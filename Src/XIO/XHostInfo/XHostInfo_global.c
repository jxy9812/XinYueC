// XHostInfo_global.c
#include "XHostInfo_p.h"
#include "XMemory.h"
#include "XEventLoop.h"
#include "XVariant.h"
#include "XString.h"
XThread* g_XHostInfo_dns_thread = NULL;
XMutex* g_XHostInfo_task_mutex = NULL;
XListSLinked* g_XHostInfo_pending_tasks = NULL;
volatile bool g_XHostInfo_dns_thread_quit = false;

// Call this during application initialization
void XHostInfo_init_module(void) {
    static bool initialized = false;
    if (initialized) return;
    g_XHostInfo_task_mutex = XMutex_create();
    g_XHostInfo_pending_tasks = XListSLinked_create(sizeof(XHostInfo_LookupTask*));
    g_XHostInfo_dns_thread_quit = false;
    g_XHostInfo_dns_thread = XThread_create(XHostInfo_dns_worker, NULL);
    XThread_start_base(g_XHostInfo_dns_thread);
    initialized = true;
}

void XHostInfo_cleanup_module(void) {
    if (!g_XHostInfo_dns_thread) return;
    g_XHostInfo_dns_thread_quit = true;
    XThread_wait_base(g_XHostInfo_dns_thread, UINT32_MAX);
    XThread_delete_base(g_XHostInfo_dns_thread);
    XMutex_delete(g_XHostInfo_task_mutex);
    while (!XListSLinked_isEmpty_base(g_XHostInfo_pending_tasks)) {
        XHostInfo_LookupTask* task = NULL;
        task=XListSLinked_Front_Base(g_XHostInfo_pending_tasks, XHostInfo_LookupTask*);
        XListSLinked_pop_front_base(g_XHostInfo_pending_tasks);
        XHostInfo_task_destroy(task);
    }
    XListSLinked_delete_base(g_XHostInfo_pending_tasks);
}

// DNS worker thread
void XHostInfo_dns_worker(void* arg) {
    (void)arg;
    while (!g_XHostInfo_dns_thread_quit) {
        XHostInfo_LookupTask* task = NULL;
        XMutex_lock(g_XHostInfo_task_mutex);
        if (!XListSLinked_isEmpty_base(g_XHostInfo_pending_tasks)) {
            task = XListSLinked_Front_Base(g_XHostInfo_pending_tasks, XHostInfo_LookupTask*);
            XListSLinked_pop_front_base(g_XHostInfo_pending_tasks);
            XHostInfo_task_destroy(task);
        }
        XMutex_unlock(g_XHostInfo_task_mutex);
        if (!task) {
            //XThread_sleep(10);
            continue;
        }
        if (task->aborted) {
            XHostInfo_task_destroy(task);
            continue;
        }
        XHostInfo* result = XHostInfo_platform_fromName(task->name);
        if (result) XHostInfo_setLookupId(result, task->lookupId);
        // Post result to main thread event loop
        if (task->callback) {
            // Create a custom event to carry 'result' and 'userData'
            // This requires a new event type, but for brevity, we assume a helper exists
            // XEventLoop_postCall(main_loop, (void(*)(void*))task->callback, result, task->userData);
        }
        else if (task->receiver && task->member) {
            //XVariant var= XVariant
            //XVariant_setValue_ptr(&argList[0], result);
            XObject_emitSignal(task->receiver, task->member, result, NULL,NULL,XEVENT_PRIORITY_NORMAL);
            //XVariant_deinit_base(&argList[0]);
        }
        XHostInfo_task_destroy(task);
    }
}

XHostInfo_LookupTask* XHostInfo_task_create(const char* name, int id) {
    XHostInfo_LookupTask* task = (XHostInfo_LookupTask*)XMemory_calloc(1, sizeof(XHostInfo_LookupTask));
    task->name = XStrdup(name);
    task->lookupId = id;
    return task;
}

void XHostInfo_task_destroy(XHostInfo_LookupTask* task) {
    if (!task) return;
    XMemory_free(task->name);
    XMemory_free(task->member);
    XMemory_free(task);
}