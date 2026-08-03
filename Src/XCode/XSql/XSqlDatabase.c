/**
 * @file       XSqlDatabase.c
 * @brief      SQL 数据库连接、静态驱动注册和驱动创建器实现。
 */
#include "XSqlDatabase.h"
#include "XSqlQuery.h"
#include "XMySqlDriver.h"
#include "XSqliteDriver.h"
#include "XThread.h"
#include "XMutex.h"
#include "XAtomic.h"

#include <string.h>

const char* XSqlDatabase_defaultConnection = "qt_sql_default_connection";

static XAtomic_uintptr_t g_sqlRegistryMutex = { 0 };

static XMutex* xsql_registry_mutex(void)
{
    XMutex* mutex = (XMutex*)XAtomic_load_uintptr_t(&g_sqlRegistryMutex,
                                                    XAtomic_MemoryOrder_Acquire);
    if (mutex) return mutex;
    mutex = XMutex_create(XLock_Recursive);
    if (!mutex) return NULL;
    uintptr_t expected = 0;
    if (XAtomic_compare_exchange_strong_uintptr_t(&g_sqlRegistryMutex, &expected,
                                                  (uintptr_t)mutex,
                                                  XAtomic_MemoryOrder_AcqRel,
                                                  XAtomic_MemoryOrder_Acquire)) {
        return mutex;
    }
    XMutex_delete(mutex);
    return (XMutex*)expected;
}

static XMutex* xsql_registry_lock(void)
{
    XMutex* mutex = xsql_registry_mutex();
    if (mutex) XMutex_lock(mutex);
    return mutex;
}

static void xsql_registry_unlock(XMutex* mutex)
{
    if (mutex) XMutex_unlock(mutex);
}

static void xsql_register_builtin_drivers(void)
{
    static bool initialized;
    XMutex* mutex = xsql_registry_lock();
    if (!initialized) {
        initialized = true;
        XMySqlDriver_register();
        XSqliteDriver_register();
    }
    xsql_registry_unlock(mutex);
}

static XSqlDriver* VXSqlDriverCreatorBase_createObject(const XSqlDriverCreatorBase* creator);
static void VXSqlDriverCreatorBase_deinit(XSqlDriverCreatorBase* creator);
static void VXSqlDriverCreator_deinit(XSqlDriverCreator* creator);
static void VXSqlDatabase_copy(XSqlDatabase* dest, const XSqlDatabase* src);
static void VXSqlDatabase_move(XSqlDatabase* dest, XSqlDatabase* src);
static void VXSqlDatabase_deinit(XSqlDatabase* database);

XVtable* XSqlDriverCreatorBase_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XSqlDriverCreatorBase)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(((void*[]){ VXSqlDriverCreatorBase_createObject }));
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqlDriverCreatorBase_deinit);
    return XVTABLE_DEFAULT;
}

XVtable* XSqlDriverCreator_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XSqlDriverCreator)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XSqlDriverCreatorBase);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqlDriverCreator_deinit);
    return XVTABLE_DEFAULT;
}

void XSqlDriverCreator_init(XSqlDriverCreator* creator, XSqlDriverCreateMethod createMethod)
{
    if (!creator) return;
    memset(creator, 0, sizeof(*creator));
    XClass_init((XClass*)&creator->m_parent);
    XClassSetVtable(&creator->m_parent, XSqlDriverCreator);
    creator->m_parent.m_create = createMethod;
}

XSqlDriverCreator* XSqlDriverCreator_create(XSqlDriverCreateMethod createMethod)
{
    XSqlDriverCreator* creator = (XSqlDriverCreator*)XMalloc_System(sizeof(XSqlDriverCreator));
    if (!creator) return NULL;
    XSqlDriverCreator_init(creator, createMethod);
    Set_Class_MemoryFree(creator, XFree_System);
    return creator;
}

XSqlDriver* XSqlDriverCreatorBase_createObject_base(const XSqlDriverCreatorBase* creator)
{
    if (!creator || XClassIsVtableNull(creator)) return NULL;
    return XClassGetVirtualFunc(creator, EXSqlDriverCreatorBase_CreateObject,
                                XSqlDriver*(*)(const XSqlDriverCreatorBase*))(creator);
}

static XSqlDriver* VXSqlDriverCreatorBase_createObject(const XSqlDriverCreatorBase* creator)
{
    return creator && creator->m_create ? creator->m_create() : NULL;
}

static void VXSqlDriverCreatorBase_deinit(XSqlDriverCreatorBase* creator)
{
    if (creator) {
        creator->m_create = NULL;
        XClass_Deinit_Parent(XClass, creator);
    }
}

static void VXSqlDriverCreator_deinit(XSqlDriverCreator* creator)
{
    if (!creator) return;
    XClass_Deinit_Parent(XSqlDriverCreatorBase, creator);
}

struct XSqlDatabasePrivate {
    size_t m_refs;
    XSqlDriver* m_driver;
    XString* m_databaseName;
    XString* m_userName;
    XString* m_password;
    XString* m_hostName;
    XString* m_driverName;
    XString* m_connectOptions;
    XString* m_connectionName;
    int m_port;
    XSqlNumericalPrecisionPolicy m_precisionPolicy;
};

typedef struct XSqlDriverEntry {
    XString* m_name;
    XSqlDriverCreatorBase* m_creator;
    XSqlDriverType m_type;
} XSqlDriverEntry;

typedef struct XSqlConnectionEntry {
    XString* m_name;
    XSqlDatabasePrivate* m_private;
} XSqlConnectionEntry;

#define XSQL_MAX_REGISTERED_DRIVERS 64
#define XSQL_MAX_CONNECTIONS 64
static XSqlDriverEntry g_driverEntries[XSQL_MAX_REGISTERED_DRIVERS];
static size_t g_driverEntryCount = 0;
static XSqlConnectionEntry g_connectionEntries[XSQL_MAX_CONNECTIONS];
static size_t g_connectionEntryCount = 0;

/* QSqlDatabase keeps the driver as the thread-affine owner of a connection.
 * Keep every operation that can touch the driver on that same thread. */
static bool xsql_database_thread_allowed(const XSqlDatabasePrivate* privateData)
{
    XThread* owner;
    XThread* current;
    if (!privateData || !privateData->m_driver) return false;
    owner = XObject_thread((const XObject*)privateData->m_driver);
    current = XThread_currentThread();
    return !owner || owner == current;
}

XVtable* XSqlDatabase_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XSqlDatabase)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSqlDatabase_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSqlDatabase_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqlDatabase_deinit);
    return XVTABLE_DEFAULT;
}

void XSqlDatabase_init(XSqlDatabase* database)
{
    if (!database) return;
    memset(((unsigned char*)database) + sizeof(XClass), 0, sizeof(*database) - sizeof(XClass));
    XClass_init((XClass*)database);
    XClassSetVtable(database, XSqlDatabase);
}

XSqlDatabase* XSqlDatabase_create(void)
{
    XSqlDatabase* database = (XSqlDatabase*)XMalloc_System(sizeof(XSqlDatabase));
    if (!database) return NULL;
    memset(database, 0, sizeof(*database));
    XSqlDatabase_init(database);
    Set_Class_MemoryFree(database, XFree_System);
    return database;
}

static void xsql_database_private_ref(XSqlDatabasePrivate* privateData)
{
    if (privateData) ++privateData->m_refs;
}

static void xsql_database_private_delete(XSqlDatabasePrivate* privateData)
{
    if (!privateData) return;
    if (privateData->m_driver) XSqlDriver_delete_base(privateData->m_driver);
    if (privateData->m_databaseName) XString_delete_base(privateData->m_databaseName);
    if (privateData->m_userName) XString_delete_base(privateData->m_userName);
    if (privateData->m_password) XString_delete_base(privateData->m_password);
    if (privateData->m_hostName) XString_delete_base(privateData->m_hostName);
    if (privateData->m_driverName) XString_delete_base(privateData->m_driverName);
    if (privateData->m_connectOptions) XString_delete_base(privateData->m_connectOptions);
    if (privateData->m_connectionName) XString_delete_base(privateData->m_connectionName);
    XFree_System(privateData);
}

static void xsql_database_private_release(XSqlDatabasePrivate* privateData)
{
    if (!privateData || privateData->m_refs == 0) return;
    if (--privateData->m_refs == 0) xsql_database_private_delete(privateData);
}

static XSqlDatabasePrivate* xsql_database_private_create(XSqlDriver* driver, const char* name)
{
    XSqlDatabasePrivate* privateData = (XSqlDatabasePrivate*)XCalloc_System(1, sizeof(XSqlDatabasePrivate));
    if (!privateData) return NULL;
    privateData->m_refs = 1;
    privateData->m_driver = driver;
    privateData->m_port = -1;
    privateData->m_precisionPolicy = XSqlNumericalPrecisionPolicy_LowPrecisionDouble;
    privateData->m_connectionName = XString_create_utf8(name ? name : XSqlDatabase_defaultConnection);
    privateData->m_driverName = XString_create_utf8(driver ? XSqlDriverType_name(XSqlDriver_driverType(driver)) : "");
    if (!privateData->m_connectionName || !privateData->m_driverName) {
        xsql_database_private_delete(privateData);
        return NULL;
    }
    return privateData;
}

static void xsql_database_assign_string(XString** target, const XString* source)
{
    if (*target) { XString_delete_base(*target); *target = NULL; }
    if (source) *target = XString_create_copy(source);
}

static void VXSqlDatabase_deinit(XSqlDatabase* database)
{
    if (!database) return;
    xsql_database_private_release(database->m_d);
    database->m_d = NULL;
    XClass_Deinit_Parent(XClass, database);
}

static void VXSqlDatabase_copy(XSqlDatabase* dest, const XSqlDatabase* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlDatabase_init(dest);
    xsql_database_private_release(dest->m_d);
    dest->m_d = src->m_d;
    xsql_database_private_ref(dest->m_d);
}

static void VXSqlDatabase_move(XSqlDatabase* dest, XSqlDatabase* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(dest)) XSqlDatabase_init(dest);
    xsql_database_private_release(dest->m_d);
    dest->m_d = src->m_d;
    src->m_d = NULL;
}

XSqlDatabase* XSqlDatabase_create_copy(const XSqlDatabase* other)
{
    if (!other) return NULL;
    XSqlDatabase* result = XSqlDatabase_create();
    if (result) XSqlDatabase_copy_base(result, other);
    return result;
}

XSqlDatabase* XSqlDatabase_create_move(XSqlDatabase* other)
{
    if (!other) return NULL;
    XSqlDatabase* result = XSqlDatabase_create();
    if (result) XSqlDatabase_move_base(result, other);
    return result;
}

static int xsql_find_driver_entry(const char* name)
{
    if (!name) return -1;
    for (size_t i = 0; i < g_driverEntryCount; ++i)
        if (g_driverEntries[i].m_name && XString_equals_utf8(g_driverEntries[i].m_name, name, XChar_CaseInsensitive)) return (int)i;
    return -1;
}

static int xsql_find_driver_entry_type(XSqlDriverType type)
{
    for (size_t i = 0; i < g_driverEntryCount; ++i) if (g_driverEntries[i].m_type == type) return (int)i;
    return -1;
}

static int xsql_find_connection_entry(const char* name)
{
    const char* actual = name ? name : XSqlDatabase_defaultConnection;
    for (size_t i = 0; i < g_connectionEntryCount; ++i)
        if (g_connectionEntries[i].m_name && XString_equals_utf8(g_connectionEntries[i].m_name, actual, XChar_CaseSensitive)) return (int)i;
    return -1;
}

static void xsql_remove_connection_entry(size_t index)
{
    if (index >= g_connectionEntryCount) return;
    if (g_connectionEntries[index].m_name) XString_delete_base(g_connectionEntries[index].m_name);
    xsql_database_private_release(g_connectionEntries[index].m_private);
    memmove(&g_connectionEntries[index], &g_connectionEntries[index + 1], (g_connectionEntryCount - index - 1) * sizeof(XSqlConnectionEntry));
    --g_connectionEntryCount;
}

bool XSqlDatabase_registerSqlDriver(const char* name, XSqlDriverCreatorBase* creator)
{
    XMutex* mutex = xsql_registry_lock();
    if (!name || !creator) {
        xsql_registry_unlock(mutex);
        return false;
    }
    int index = xsql_find_driver_entry(name);
    if (index < 0) {
        if (g_driverEntryCount >= XSQL_MAX_REGISTERED_DRIVERS) {
            xsql_registry_unlock(mutex);
            return false;
        }
        index = (int)g_driverEntryCount++;
        g_driverEntries[index].m_name = XString_create_utf8(name);
        if (!g_driverEntries[index].m_name) {
            --g_driverEntryCount;
            xsql_registry_unlock(mutex);
            return false;
        }
    } else if (g_driverEntries[index].m_creator) XSqlDriverCreatorBase_delete_base(g_driverEntries[index].m_creator);
    g_driverEntries[index].m_creator = creator;
    g_driverEntries[index].m_type = XSqlDriverType_fromName_utf8(name);
    xsql_registry_unlock(mutex);
    return true;
}

bool XSqlDatabase_registerSqlDriver_type(XSqlDriverType type, XSqlDriverCreatorBase* creator)
{
    return XSqlDatabase_registerSqlDriver(XSqlDriverType_name(type), creator);
}

bool XSqlDatabase_isDriverAvailable(const char* name)
{
    XMutex* mutex;
    xsql_register_builtin_drivers();
    mutex = xsql_registry_lock();
    int index = xsql_find_driver_entry(name);
    bool result = index >= 0 && g_driverEntries[index].m_creator != NULL;
    xsql_registry_unlock(mutex);
    return result;
}

static XSqlDriver* xsql_create_driver_type(XSqlDriverType type)
{
    XMutex* mutex = xsql_registry_lock();
    int index = xsql_find_driver_entry_type(type);
    XSqlDriver* result = index >= 0
        ? XSqlDriverCreatorBase_createObject_base(g_driverEntries[index].m_creator) : NULL;
    xsql_registry_unlock(mutex);
    return result;
}

XSqlDatabase* XSqlDatabase_addDatabase(XSqlDriverType type, const char* connectionName)
{
    xsql_register_builtin_drivers();
    XSqlDriver* driver = xsql_create_driver_type(type);
    return driver ? XSqlDatabase_addDatabase_driver(driver, connectionName) : NULL;
}

XSqlDatabase* XSqlDatabase_addDatabase_driver(XSqlDriver* driver, const char* connectionName)
{
    XMutex* mutex;
    if (!driver) return NULL;
    mutex = xsql_registry_lock();
    const char* name = connectionName ? connectionName : XSqlDatabase_defaultConnection;
    int old = xsql_find_connection_entry(name);
    if (old >= 0) xsql_remove_connection_entry((size_t)old);
    if (g_connectionEntryCount >= XSQL_MAX_CONNECTIONS) {
        XSqlDriver_delete_base(driver);
        xsql_registry_unlock(mutex);
        return NULL;
    }
    XSqlDatabasePrivate* privateData = xsql_database_private_create(driver, name);
    if (!privateData) {
        XSqlDriver_delete_base(driver);
        xsql_registry_unlock(mutex);
        return NULL;
    }
    XSqlDatabase* result = XSqlDatabase_create();
    if (!result) {
        xsql_database_private_release(privateData);
        xsql_registry_unlock(mutex);
        return NULL;
    }
    result->m_d = privateData;
    g_connectionEntries[g_connectionEntryCount].m_name = XString_create_utf8(name);
    g_connectionEntries[g_connectionEntryCount].m_private = privateData;
    if (!g_connectionEntries[g_connectionEntryCount].m_name) {
        XSqlDatabase_delete_base(result);
        xsql_registry_unlock(mutex);
        return NULL;
    }
    xsql_database_private_ref(privateData);
    ++g_connectionEntryCount;
    xsql_registry_unlock(mutex);
    return result;
}

XSqlDatabase* XSqlDatabase_addDatabase_utf8(const char* type, const char* connectionName) { return XSqlDatabase_addDatabase(XSqlDriverType_fromName_utf8(type), connectionName); }
XSqlDatabase* XSqlDatabase_addDatabase_2(const XString* type, const XString* connectionName) { return XSqlDatabase_addDatabase_utf8(type ? XString_toUtf8(type) : NULL, connectionName ? XString_toUtf8(connectionName) : NULL); }

XSqlDatabase* XSqlDatabase_database(const char* connectionName, bool open)
{
    XMutex* mutex = xsql_registry_lock();
    int index = xsql_find_connection_entry(connectionName);
    if (index < 0) {
        xsql_registry_unlock(mutex);
        return NULL;
    }
    if (!xsql_database_thread_allowed(g_connectionEntries[index].m_private)) {
        xsql_registry_unlock(mutex);
        return NULL;
    }
    XSqlDatabase* result = XSqlDatabase_create();
    if (!result) {
        xsql_registry_unlock(mutex);
        return NULL;
    }
    result->m_d = g_connectionEntries[index].m_private;
    xsql_database_private_ref(result->m_d);
    xsql_registry_unlock(mutex);
    if (open && !XSqlDatabase_isOpen(result) && !XSqlDatabase_open(result)) { XSqlDatabase_delete_base(result); return NULL; }
    return result;
}

XSqlDatabase* XSqlDatabase_cloneDatabase(const XSqlDatabase* other, const char* connectionName)
{
    if (!other || !other->m_d || !other->m_d->m_driver || !xsql_database_thread_allowed(other->m_d)) return NULL;
    XSqlDriver* driver = xsql_create_driver_type(XSqlDriver_driverType(other->m_d->m_driver));
    if (!driver) return NULL;
    XSqlDatabase* result = XSqlDatabase_addDatabase_driver(driver, connectionName);
    if (!result || !result->m_d) return result;
    xsql_database_assign_string(&result->m_d->m_databaseName, other->m_d->m_databaseName);
    xsql_database_assign_string(&result->m_d->m_userName, other->m_d->m_userName);
    xsql_database_assign_string(&result->m_d->m_password, other->m_d->m_password);
    xsql_database_assign_string(&result->m_d->m_hostName, other->m_d->m_hostName);
    xsql_database_assign_string(&result->m_d->m_connectOptions, other->m_d->m_connectOptions);
    result->m_d->m_port = other->m_d->m_port;
    result->m_d->m_precisionPolicy = other->m_d->m_precisionPolicy;
    XSqlDriver_setNumericalPrecisionPolicy(result->m_d->m_driver, result->m_d->m_precisionPolicy);
    return result;
}

XSqlDatabase* XSqlDatabase_cloneDatabase_name(const char* otherConnectionName, const char* connectionName)
{
    XSqlDatabase* other = XSqlDatabase_database(otherConnectionName, false);
    if (!other) return NULL;
    XSqlDatabase* result = XSqlDatabase_cloneDatabase(other, connectionName);
    XSqlDatabase_delete_base(other);
    return result;
}

XSqlDatabase* XSqlDatabase_cloneDatabase_2(const XSqlDatabase* other, const XString* connectionName) { return XSqlDatabase_cloneDatabase(other, connectionName ? XString_toUtf8(connectionName) : NULL); }
XSqlDatabase* XSqlDatabase_cloneDatabase_name_2(const XString* otherConnectionName, const XString* connectionName) { return XSqlDatabase_cloneDatabase_name(otherConnectionName ? XString_toUtf8(otherConnectionName) : NULL, connectionName ? XString_toUtf8(connectionName) : NULL); }
XSqlDatabase* XSqlDatabase_database_2(const XString* connectionName, bool open) { return XSqlDatabase_database(connectionName ? XString_toUtf8(connectionName) : NULL, open); }

void XSqlDatabase_removeDatabase(const char* connectionName) { XMutex* mutex = xsql_registry_lock(); int index = xsql_find_connection_entry(connectionName); if (index >= 0) xsql_remove_connection_entry((size_t)index); xsql_registry_unlock(mutex); }
bool XSqlDatabase_contains(const char* connectionName) { XMutex* mutex = xsql_registry_lock(); bool result = xsql_find_connection_entry(connectionName) >= 0; xsql_registry_unlock(mutex); return result; }
void XSqlDatabase_removeDatabase_2(const XString* connectionName) { XSqlDatabase_removeDatabase(connectionName ? XString_toUtf8(connectionName) : NULL); }
bool XSqlDatabase_contains_2(const XString* connectionName) { return XSqlDatabase_contains(connectionName ? XString_toUtf8(connectionName) : NULL); }
bool XSqlDatabase_registerSqlDriver_2(const XString* name, XSqlDriverCreatorBase* creator) { return XSqlDatabase_registerSqlDriver(name ? XString_toUtf8(name) : NULL, creator); }
bool XSqlDatabase_isDriverAvailable_2(const XString* name) { return XSqlDatabase_isDriverAvailable(name ? XString_toUtf8(name) : NULL); }
XStringList* XSqlDatabase_drivers(void) { xsql_register_builtin_drivers(); XMutex* mutex = xsql_registry_lock(); XStringList* result = XStringList_create(); if (result) for (size_t i = 0; i < g_driverEntryCount; ++i) if (g_driverEntries[i].m_name) XStringList_push_back_base(result, g_driverEntries[i].m_name); xsql_registry_unlock(mutex); return result; }
XStringList* XSqlDatabase_connectionNames(void) { XMutex* mutex = xsql_registry_lock(); XStringList* result = XStringList_create(); if (result) for (size_t i = 0; i < g_connectionEntryCount; ++i) if (g_connectionEntries[i].m_name) XStringList_push_back_base(result, g_connectionEntries[i].m_name); xsql_registry_unlock(mutex); return result; }

static bool xsql_database_open_with(XSqlDatabase* database, const XString* user, const XString* password)
{
    if (!database || !database->m_d || !database->m_d->m_driver || !xsql_database_thread_allowed(database->m_d)) return false;
    if (user) xsql_database_assign_string(&database->m_d->m_userName, user);
    return XSqlDriver_open_base(database->m_d->m_driver, database->m_d->m_databaseName,
                                user ? user : database->m_d->m_userName,
                                password ? password : database->m_d->m_password,
                                database->m_d->m_hostName, database->m_d->m_port, database->m_d->m_connectOptions);
}

bool XSqlDatabase_open(XSqlDatabase* database) { return xsql_database_open_with(database, NULL, NULL); }
bool XSqlDatabase_open_2(XSqlDatabase* database, const XString* user, const XString* password) { return xsql_database_open_with(database, user, password); }
bool XSqlDatabase_open_utf8(XSqlDatabase* database, const char* user, const char* password) { XString* u = user ? XString_create_utf8(user) : NULL; XString* p = password ? XString_create_utf8(password) : NULL; bool result = XSqlDatabase_open_2(database, u, p); if (u) XString_delete_base(u); if (p) XString_delete_base(p); return result; }
void XSqlDatabase_close(XSqlDatabase* database) { if (database && database->m_d && database->m_d->m_driver && xsql_database_thread_allowed(database->m_d)) XSqlDriver_close_base(database->m_d->m_driver); }
bool XSqlDatabase_isOpen(const XSqlDatabase* database) { return database && database->m_d && database->m_d->m_driver && xsql_database_thread_allowed(database->m_d) && XSqlDriver_isOpen(database->m_d->m_driver); }
bool XSqlDatabase_isOpenError(const XSqlDatabase* database) { return !database || !database->m_d || !database->m_d->m_driver || !xsql_database_thread_allowed(database->m_d) || XSqlDriver_isOpenError(database->m_d->m_driver); }
XStringList* XSqlDatabase_tables(const XSqlDatabase* database, XSqlTableType type) { return database && database->m_d && database->m_d->m_driver && xsql_database_thread_allowed(database->m_d) ? XSqlDriver_tables_base(database->m_d->m_driver, type) : XStringList_create(); }
XSqlIndex* XSqlDatabase_primaryIndex(const XSqlDatabase* database, const XString* tableName) { return database && database->m_d && database->m_d->m_driver && xsql_database_thread_allowed(database->m_d) ? XSqlDriver_primaryIndex_base(database->m_d->m_driver, tableName) : XSqlIndex_create(); }
XSqlIndex* XSqlDatabase_primaryIndex_utf8(const XSqlDatabase* database, const char* tableName) { XString* name = tableName ? XString_create_utf8(tableName) : NULL; XSqlIndex* result = XSqlDatabase_primaryIndex(database, name); if (name) XString_delete_base(name); return result; }
XSqlRecord* XSqlDatabase_record(const XSqlDatabase* database, const XString* tableName) { return database && database->m_d && database->m_d->m_driver && xsql_database_thread_allowed(database->m_d) ? XSqlDriver_record_base(database->m_d->m_driver, tableName) : XSqlRecord_create(); }
XSqlRecord* XSqlDatabase_record_utf8(const XSqlDatabase* database, const char* tableName) { XString* name = tableName ? XString_create_utf8(tableName) : NULL; XSqlRecord* result = XSqlDatabase_record(database, name); if (name) XString_delete_base(name); return result; }
XSqlQuery* XSqlDatabase_exec(const XSqlDatabase* database, const XString* query) { XSqlQuery* result = xsql_database_thread_allowed(database ? database->m_d : NULL) ? XSqlQuery_create_database(database) : NULL; if (result && query) XSqlQuery_exec_query(result, query); return result; }
XSqlQuery* XSqlDatabase_exec_utf8(const XSqlDatabase* database, const char* query) { XString* text = query ? XString_create_utf8(query) : NULL; XSqlQuery* result = XSqlDatabase_exec(database, text); if (text) XString_delete_base(text); return result; }
XSqlError* XSqlDatabase_lastError(const XSqlDatabase* database) { return database && database->m_d && database->m_d->m_driver && xsql_database_thread_allowed(database->m_d) ? XSqlDriver_lastError(database->m_d->m_driver) : XSqlError_create(NULL, NULL, XSqlErrorType_UnknownError, NULL); }
bool XSqlDatabase_isValid(const XSqlDatabase* database) { return database && database->m_d && database->m_d->m_driver; }
bool XSqlDatabase_transaction(XSqlDatabase* database) { return database && database->m_d && xsql_database_thread_allowed(database->m_d) && XSqlDriver_hasFeature_base(database->m_d->m_driver, XSqlDriverFeature_Transactions) && XSqlDriver_beginTransaction_base(database->m_d->m_driver); }
bool XSqlDatabase_commit(XSqlDatabase* database) { return database && database->m_d && xsql_database_thread_allowed(database->m_d) && XSqlDriver_hasFeature_base(database->m_d->m_driver, XSqlDriverFeature_Transactions) && XSqlDriver_commitTransaction_base(database->m_d->m_driver); }
bool XSqlDatabase_rollback(XSqlDatabase* database) { return database && database->m_d && xsql_database_thread_allowed(database->m_d) && XSqlDriver_hasFeature_base(database->m_d->m_driver, XSqlDriverFeature_Transactions) && XSqlDriver_rollbackTransaction_base(database->m_d->m_driver); }

void XSqlDatabase_setDatabaseName(XSqlDatabase* database, const XString* name) { if (database && database->m_d) xsql_database_assign_string(&database->m_d->m_databaseName, name); }
void XSqlDatabase_setUserName(XSqlDatabase* database, const XString* name) { if (database && database->m_d) xsql_database_assign_string(&database->m_d->m_userName, name); }
void XSqlDatabase_setPassword(XSqlDatabase* database, const XString* password) { if (database && database->m_d) xsql_database_assign_string(&database->m_d->m_password, password); }
void XSqlDatabase_setHostName(XSqlDatabase* database, const XString* host) { if (database && database->m_d) xsql_database_assign_string(&database->m_d->m_hostName, host); }
void XSqlDatabase_setPort(XSqlDatabase* database, int port) { if (database && database->m_d) database->m_d->m_port = port; }
void XSqlDatabase_setConnectOptions(XSqlDatabase* database, const XString* options) { if (database && database->m_d) xsql_database_assign_string(&database->m_d->m_connectOptions, options); }
XString* XSqlDatabase_databaseName(const XSqlDatabase* database) { return database && database->m_d && database->m_d->m_databaseName ? XString_create_copy(database->m_d->m_databaseName) : XString_create(); }
XString* XSqlDatabase_userName(const XSqlDatabase* database) { return database && database->m_d && database->m_d->m_userName ? XString_create_copy(database->m_d->m_userName) : XString_create(); }
XString* XSqlDatabase_password(const XSqlDatabase* database) { return database && database->m_d && database->m_d->m_password ? XString_create_copy(database->m_d->m_password) : XString_create(); }
XString* XSqlDatabase_hostName(const XSqlDatabase* database) { return database && database->m_d && database->m_d->m_hostName ? XString_create_copy(database->m_d->m_hostName) : XString_create(); }
XString* XSqlDatabase_driverName(const XSqlDatabase* database) { return database && database->m_d && database->m_d->m_driverName ? XString_create_copy(database->m_d->m_driverName) : XString_create(); }
int XSqlDatabase_port(const XSqlDatabase* database) { return database && database->m_d ? database->m_d->m_port : -1; }
XString* XSqlDatabase_connectOptions(const XSqlDatabase* database) { return database && database->m_d && database->m_d->m_connectOptions ? XString_create_copy(database->m_d->m_connectOptions) : XString_create(); }
XString* XSqlDatabase_connectionName(const XSqlDatabase* database) { return database && database->m_d && database->m_d->m_connectionName ? XString_create_copy(database->m_d->m_connectionName) : XString_create(); }
void XSqlDatabase_setNumericalPrecisionPolicy(XSqlDatabase* database, XSqlNumericalPrecisionPolicy policy) { if (database && database->m_d) { database->m_d->m_precisionPolicy = policy; if (database->m_d->m_driver) XSqlDriver_setNumericalPrecisionPolicy(database->m_d->m_driver, policy); } }
XSqlNumericalPrecisionPolicy XSqlDatabase_numericalPrecisionPolicy(const XSqlDatabase* database) { return database && database->m_d ? database->m_d->m_precisionPolicy : XSqlNumericalPrecisionPolicy_HighPrecision; }
bool XSqlDatabase_moveToThread(XSqlDatabase* database, XThread* targetThread)
{
    XSqlDriver* driver;
    if (!database || !database->m_d || !database->m_d->m_driver) return false;
    /* The registry and the caller are the two normal references. Additional
     * database handles indicate that this connection is still shared. */
    if (database->m_d->m_refs > 2) return false;
    driver = database->m_d->m_driver;
    return XObject_moveToThread((XObject*)driver, targetThread);
}

XThread* XSqlDatabase_thread(const XSqlDatabase* database)
{
    return database && database->m_d && database->m_d->m_driver
        ? XObject_thread((const XObject*)database->m_d->m_driver) : NULL;
}
XSqlDriver* XSqlDatabase_driver(const XSqlDatabase* database) { return database && database->m_d && xsql_database_thread_allowed(database->m_d) ? database->m_d->m_driver : NULL; }
