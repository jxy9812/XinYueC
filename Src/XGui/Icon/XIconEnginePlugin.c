/******************************************************************************
 * @file       XIconEnginePlugin.c
 * @brief      XIconEnginePlugin 图标引擎插件工厂实现。
 ******************************************************************************/
#include "XIconEnginePlugin.h"

#include "XAlgorithm.h"
#include "XMemory.h"
#include "XContainer.h"
#include "XStringList.h"
#include <string.h>

static XIconEngine* VXIconEnginePlugin_create(XIconEnginePlugin* self,
                                               const XString* fileName)
{ (void)self; (void)fileName; return NULL; }

static void VXIconEnginePlugin_deinit(XIconEnginePlugin* self)
{ if (self) XClass_Deinit_Parent(XObject, (XObject*)self); }

static XStringList* VXIconEnginePlugin_keys(const XIconEnginePlugin* self);

XVtable* XIconEnginePlugin_class_init(void)
{
    void* table[] = { (void*)VXIconEnginePlugin_create,
                      (void*)VXIconEnginePlugin_keys };
    XVTABLE_INIT_DEFAULT(XIconEnginePlugin)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIconEnginePlugin_deinit);
    return XVTABLE_DEFAULT;
}

XIconEnginePlugin* XIconEnginePlugin_create_ex(XMemoryType memory)
{
    XIconEnginePlugin* self = (XIconEnginePlugin*)XMemory_malloc(
        sizeof(XIconEnginePlugin), memory);
    if (!self) return NULL;
    XIconEnginePlugin_init(self);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

void XIconEnginePlugin_init(XIconEnginePlugin* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XIconEnginePlugin);
}

XIconEngine* XIconEnginePlugin_createEngine_base(XIconEnginePlugin* self,
                                                  const XString* fileName)
{
    if (!self || XClassIsVtableNull(self)) return NULL;
    return XClassGetVirtualFunc(self, EXIconEnginePlugin_Create,
        XIconEngine*(*)(XIconEnginePlugin*, const XString*))(self, fileName);
}

XIconEngine* XIconEnginePlugin_createEngine_2_base(XIconEnginePlugin* self,
                                                    const char* fileName)
{
    XIconEngine* result;
    XString* value = fileName ? XString_create_utf8(fileName) : XString_create();
    result = XIconEnginePlugin_createEngine_base(self, value);
    if (value) XString_delete_base((XClass*)value);
    return result;
}

XIconEngine* XIconEnginePlugin_create_base(XIconEnginePlugin* self,
                                           const XString* fileName)
{ return XIconEnginePlugin_createEngine_base(self, fileName); }

XIconEngine* XIconEnginePlugin_create_2_base(XIconEnginePlugin* self,
                                             const char* fileName)
{ return XIconEnginePlugin_createEngine_2_base(self, fileName); }

/* ==================== Keys 虚槽与插件注册表（Task 2.15） ==================== */

static XStringList* VXIconEnginePlugin_keys(const XIconEnginePlugin* self)
{
    (void)self;
    return XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
}

static XVector* g_iconPlugins = NULL;

void XIconEnginePlugin_registerPlugin(XIconEnginePlugin* plugin)
{
    if (!plugin) return;
    if (!g_iconPlugins)
        g_iconPlugins = XVector_Create(XIconEnginePlugin*);
    if (!g_iconPlugins) return;
    {
        int64_t i;
        int64_t n = XVector_size_base((const XContainer*)g_iconPlugins);
        for (i = 0; i < n; ++i) {
            XIconEnginePlugin** item =
                (XIconEnginePlugin**)XVector_at_base(
                    (const XContainer*)g_iconPlugins, i);
            if (item && *item == plugin) return; /* 去重 */
        }
    }
    XVector_push_back_1_base(g_iconPlugins, &plugin);
}

void XIconEnginePlugin_unregisterPlugin(XIconEnginePlugin* plugin)
{
    int64_t i;
    int64_t n;
    if (!plugin || !g_iconPlugins) return;
    n = XVector_size_base((const XContainer*)g_iconPlugins);
    for (i = 0; i < n; ++i) {
        XIconEnginePlugin** item =
            (XIconEnginePlugin**)XVector_at_base(
                (const XContainer*)g_iconPlugins, i);
        if (item && *item == plugin) {
            XVector_remove_base((XContainer*)g_iconPlugins, i, 1);
            return;
        }
    }
}

static bool iconSuffixMatch(const char* fileName, const char* key)
{
    const char* dot;
    size_t keyLen;
    size_t nameLen;
    size_t i;
    if (!fileName || !key || !key[0]) return false;
    dot = strrchr(fileName, '.');
    if (!dot || !dot[1]) return false;
    ++dot;
    keyLen = strlen(key);
    nameLen = strlen(dot);
    if (nameLen != keyLen) return false;
    for (i = 0; i < nameLen; ++i) {
        char a = dot[i];
        char b = key[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

XIconEngine* XIconEnginePlugin_createEngineForFile(const char* fileName)
{
    int64_t i;
    int64_t n;
    if (!fileName || !g_iconPlugins) return NULL;
    n = XVector_size_base((const XContainer*)g_iconPlugins);
    for (i = 0; i < n; ++i) {
        XIconEnginePlugin** item =
            (XIconEnginePlugin**)XVector_at_base(
                (const XContainer*)g_iconPlugins, i);
        XStringList* keys;
        int64_t k;
        int64_t kn;
        if (!item || !*item) continue;
        keys = XIconEnginePlugin_keys_base(*item);
        if (!keys) continue;
        kn = XStringList_size_base((const XStringList*)keys);
        for (k = 0; k < kn; ++k) {
            const XString* key = XStringList_at_base(
                (const XStringList*)keys, k);
            if (key && iconSuffixMatch(fileName, XString_toUtf8(key))) {
                XIconEngine* engine =
                    XIconEnginePlugin_createEngine_2_base(*item, fileName);
                XStringList_delete_base(keys);
                return engine;
            }
        }
        XStringList_delete_base(keys);
    }
    return NULL;
}

XStringList* XIconEnginePlugin_keys_base(const XIconEnginePlugin* self)
{
    if (!self || XClassIsVtableNull(self))
        return XStringList_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    return XClassGetVirtualFunc(self, EXIconEnginePlugin_Keys,
        XStringList*(*)(const XIconEnginePlugin*))(self);
}
