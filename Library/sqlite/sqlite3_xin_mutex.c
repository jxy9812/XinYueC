/******************************************************************************
 * @file       sqlite3_xin_mutex.c
 * @brief      SQLite 互斥层统一桥接——SQLITE_MUTEX_APPDEF + XMutex。
 * @details    取代 mutex_unix（pthread）/mutex_w（Win32）两套平台自调
 *             API：全平台经 sqlite3_config(SQLITE_CONFIG_MUTEX) 安装
 *             本桥接，SQLite 的动态/静态互斥统一落在库内跨平台
 *             XMutex 上（桌面/裸机同一代码路径，裸机由 Drive/FreeRTOS
 *             的 XMutexFreeRTOS 后端承接）。
 *
 *             必须在 sqlite3_initialize() 之前调用 XSqliteMutex_install()
 *             ——见 sqlite3_xin_memory.c 的统一初始化链。
 *
 *             SQLITE_MUTEX_FAST     → XMutex_create(XLock_NonRecursive)
 *             SQLITE_MUTEX_RECURSIVE→ XMutex_create(XLock_Recursive)
 *             SQLITE_MUTEX_STATIC_* → xMutexInit 时一次性创建的静态实例
 *             xMutexHeld/xMutexNoitemHeld 恒返回 1（不提供持有断言；
 *             与多数精简移植口径一致，持有违规由 XMutex 自身的等待/
 *             递归语义兜底）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "sqlite3.h"

#include "XMutex.h"
#include <string.h>

/* SQLITE_MUTEX_STATIC_MASTER..SQLITE_MUTEX_STATIC_APP3 = id 2..10。 */
#define XSQLITE_MUTEX_STATIC_FIRST 2
#define XSQLITE_MUTEX_STATIC_LAST  10
#define XSQLITE_MUTEX_STATIC_COUNT (XSQLITE_MUTEX_STATIC_LAST - XSQLITE_MUTEX_STATIC_FIRST + 1)

static XMutex* g_xsqlite_staticMutexes[XSQLITE_MUTEX_STATIC_COUNT];

static int xsqlite_mutex_init(void)
{
    size_t index;
    for (index = 0; index < XSQLITE_MUTEX_STATIC_COUNT; ++index) {
        g_xsqlite_staticMutexes[index] = XMutex_create(XLock_NonRecursive);
        if (!g_xsqlite_staticMutexes[index]) {
            /* 失败回滚已建实例，下次 init 重试。 */
            while (index > 0) {
                --index;
                XMutex_delete(g_xsqlite_staticMutexes[index]);
                g_xsqlite_staticMutexes[index] = NULL;
            }
            return SQLITE_NOMEM;
        }
    }
    return SQLITE_OK;
}

static int xsqlite_mutex_end(void)
{
    size_t index;
    for (index = 0; index < XSQLITE_MUTEX_STATIC_COUNT; ++index) {
        if (g_xsqlite_staticMutexes[index]) {
            XMutex_delete(g_xsqlite_staticMutexes[index]);
            g_xsqlite_staticMutexes[index] = NULL;
        }
    }
    return SQLITE_OK;
}

static int xsqlite_mutex_isStatic(sqlite3_mutex* mutex)
{
    size_t index;
    for (index = 0; index < XSQLITE_MUTEX_STATIC_COUNT; ++index) {
        if ((sqlite3_mutex*) g_xsqlite_staticMutexes[index] == mutex) return 1;
    }
    return 0;
}

static sqlite3_mutex* xsqlite_mutex_alloc(int id)
{
    if (id <= SQLITE_MUTEX_RECURSIVE) {
        XMutex* mutex = XMutex_create(
            id == SQLITE_MUTEX_RECURSIVE ? XLock_Recursive : XLock_NonRecursive);
        return (sqlite3_mutex*) mutex;
    }
    /* 静态互斥：由 xMutexInit 预建；init 未跑（配置缺失）时返回 NULL，
     * sqlite3MutexInit 的调用方按 SQLITE_ERROR 处理。 */
    if (id <= XSQLITE_MUTEX_STATIC_LAST) {
        return (sqlite3_mutex*)
            g_xsqlite_staticMutexes[id - XSQLITE_MUTEX_STATIC_FIRST];
    }
    return NULL;
}

static void xsqlite_mutex_free(sqlite3_mutex* mutex)
{
    if (mutex && !xsqlite_mutex_isStatic(mutex)) {
        XMutex_delete((XMutex*) mutex);
    }
}

static void xsqlite_mutex_enter(sqlite3_mutex* mutex)
{
    if (mutex) XMutex_lock((XMutex*) mutex);
}

static int xsqlite_mutex_try(sqlite3_mutex* mutex)
{
    if (!mutex) return SQLITE_OK;
    return XMutex_tryLock((XMutex*) mutex) ? SQLITE_OK : SQLITE_BUSY;
}

static void xsqlite_mutex_leave(sqlite3_mutex* mutex)
{
    if (mutex) XMutex_unlock((XMutex*) mutex);
}

static int xsqlite_mutex_held(sqlite3_mutex* mutex)
{
    (void)mutex;
    return 1; /* 不做持有断言，见文件头说明。 */
}

static int xsqlite_mutex_noitem(sqlite3_mutex* mutex)
{
    (void)mutex;
    return 1;
}

int XSqliteMutex_install(void)
{
    static const sqlite3_mutex_methods methods = {
        xsqlite_mutex_init,
        xsqlite_mutex_end,
        xsqlite_mutex_alloc,
        xsqlite_mutex_free,
        xsqlite_mutex_enter,
        xsqlite_mutex_try,
        xsqlite_mutex_leave,
        xsqlite_mutex_held,
        xsqlite_mutex_noitem
    };
    int result = sqlite3_config(SQLITE_CONFIG_MUTEX, &methods);
    /* 重复安装（MISUSE）视为成功：幂等语义，与内存配置同口径。 */
    return (result == SQLITE_OK || result == SQLITE_MISUSE) ? SQLITE_OK
                                                            : result;
}
