/**
 * @file       XSqlDriverPlugin.c
 * @brief      SQL 驱动插件抽象类实现。
 */
#include "XSqlDriverPlugin.h"

#include <string.h>

static XSqlDriver* VXSqlDriverPlugin_create(XSqlDriverPlugin* plugin, const char* key);
static void VXSqlDriverPlugin_deinit(XSqlDriverPlugin* plugin);

XVtable* XSqlDriverPlugin_class_init(void)
{
    XVTABLE_CREAT_DEFAULT
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XSqlDriverPlugin))
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(((void*[]){ VXSqlDriverPlugin_create }));
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSqlDriverPlugin_deinit);
    return XVTABLE_DEFAULT;
}

void XSqlDriverPlugin_init(XSqlDriverPlugin* plugin)
{
    if (!plugin) return;
    memset(plugin, 0, sizeof(*plugin));
    XObject_init((XObject*)plugin);
    XClassSetVtable(plugin, XSqlDriverPlugin);
}
XSqlDriverPlugin* XSqlDriverPlugin_create(void) { XSqlDriverPlugin* plugin = (XSqlDriverPlugin*)XMalloc_System(sizeof(XSqlDriverPlugin)); if (!plugin) return NULL; XSqlDriverPlugin_init(plugin); Set_Class_MemoryFree(plugin, XFree_System); return plugin; }
XSqlDriver* XSqlDriverPlugin_create_base(XSqlDriverPlugin* plugin, const char* key) { return plugin && !XClassIsVtableNull(plugin) ? XClassGetVirtualFunc(plugin, EXSqlDriverPlugin_Create, XSqlDriver*(*)(XSqlDriverPlugin*, const char*))(plugin, key) : NULL; }
static XSqlDriver* VXSqlDriverPlugin_create(XSqlDriverPlugin* plugin, const char* key) { (void)plugin; (void)key; return NULL; }
static void VXSqlDriverPlugin_deinit(XSqlDriverPlugin* plugin) { if (plugin) XClass_Deinit_Parent(XObject, plugin); }
