#ifdef _WIN32
#include "XRecursiveMutex.h"
#include "XMemory.h"
#include <windows.h>
typedef struct PlatformPrivate
{
    CRITICAL_SECTION cs;       // 临界区
}PlatformPrivate;
PlatformPrivate* XMutex_getPlatformPrivate(XMutex* mutex);
CRITICAL_SECTION* XMutex_get_critical_section(XMutex* mutex)
{
    if (!mutex||mutex->type& XMutex_Spin)return NULL;
    PlatformPrivate* p=XMutex_getPlatformPrivate(mutex);
    return p ? &p->cs : NULL;
}
size_t XMutex_PlatformPrivate_size()
{
    return sizeof(PlatformPrivate);
}
void XMutex_platform_init(PlatformPrivate* p)
{
    InitializeCriticalSection(&p->cs);
}
void XMutex_platform_deinit(PlatformPrivate* p)
{
    DeleteCriticalSection(&p->cs);
}
void XMutex_platform_lock(PlatformPrivate* p)
{
    EnterCriticalSection(&p->cs);
}
bool XMutex_platform_tryLock(PlatformPrivate* p)
{
    return TryEnterCriticalSection(&p->cs);
}

void XMutex_platform_unlock(PlatformPrivate* p)
{
    LeaveCriticalSection(&p->cs);
}

#endif