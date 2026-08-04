#include "XCanBus.h"
#include "XMemory.h"
#include "XString.h"
#include <string.h>

// =============== 全局单例 ===============
static XCanBus* g_canBusInstance = NULL;

// =============== 虚函数前置声明 ===============
static void VXCanBus_deinit(XCanBus* canBus);

// =============== 类初始化 ===============
XVtable* XCanBus_class_init()
{
    XVTABLE_INIT_DEFAULT_SIZE(XCLASS_VTABLE_GET_SIZE(XObject) + 1)
	XCLASS_SET_CLASS_NAME_DEFAULT("XCanBus");
    // 继承 XObject
    XVTABLE_INHERIT_XCLASS(XObject);
    void* table[] = { NULL };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    // 重载析构函数
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXCanBus_deinit);

    XCLASS_SHOW_SIZE_DEFAULT(XCanBus);
    return XVTABLE_DEFAULT;
}

// =============== 初始化/析构 ===============
void XCanBus_init(XCanBus* canBus)
{
    if (!canBus) return;

    XObject_init((XObject*)canBus);
    XClassGetVtable(canBus) = XCanBus_class_init();

    /* 创建插件映射表：键为 XString，值为 XCanBus_PluginEntry */
    canBus->m_plugins = XMap_create(sizeof(XString), sizeof(XCanBus_PluginEntry), XString_compare);
    XMapBaseSetKeyCopyMethod(canBus->m_plugins, XString_copy_base);
    XMapBaseSetKeyMoveMethod(canBus->m_plugins, XString_move_base);
    XMapBaseSetKeyDeinitMethod(canBus->m_plugins, XClass_deinit_base);
}

static void VXCanBus_deinit(XCanBus* canBus)
{
    if (!canBus) return;

    // 释放所有插件
    if (canBus->m_plugins) {
        // 遍历释放每个插件的工厂
        XMap_iterator it = XMap_begin(canBus->m_plugins);
        while (!XMap_iterator_isEnd(&it)) {
            XPair* pair = XMap_iterator_data(&it);
            if (pair) {
                XCanBus_PluginEntry* entry = (XCanBus_PluginEntry*)XPair_second(pair);
                if (entry) {
                    if (entry->m_key) {
                        XString_delete_base(entry->m_key);
                    }
                    if (entry->m_factory) {
                        XCanBusFactory_destroy(entry->m_factory);
                    }
                }
            }
            XMap_iterator_add(canBus->m_plugins, &it);
        }
        XMap_delete_base(canBus->m_plugins);
        canBus->m_plugins = NULL;
    }

    XClass_Deinit_Parent(XObject, canBus);
}

// =============== 单例访问 ===============
XCanBus* XCanBus_instance(void)
{
    if (!g_canBusInstance) {
        g_canBusInstance = (XCanBus*)XMalloc_System(sizeof(XCanBus));
        if (g_canBusInstance) {
            XCanBus_init(g_canBusInstance);
            Set_Class_MemoryFree(g_canBusInstance, XFree_System);
        }
    }
    return g_canBusInstance;
}

// =============== 插件管理 API ===============
XStringList* XCanBus_plugins(const XCanBus* canBus)
{
    if (!canBus || !canBus->m_plugins) return XStringList_create();

    XStringList* list = XStringList_create();
    XMap_iterator it = XMap_begin(canBus->m_plugins);
    while (!XMap_iterator_isEnd(&it)) {
        XPair* pair = XMap_iterator_data(&it);
        if (pair) {
            XCanBus_PluginEntry* entry = (XCanBus_PluginEntry*)XPair_second(pair);
            if (entry && entry->m_key) {
                XStringList_push_back_base(list, entry->m_key);
            }
        }
        XMap_iterator_add(canBus->m_plugins, &it);
    }
    return list;
}

bool XCanBus_registerPlugin(XCanBus* canBus, const char* plugin, XCanBusFactory* factory)
{
    if (!canBus || !plugin || !factory) return false;

    XString key;
    XString_init(&key);
    XString_assign_utf8(&key, plugin);

    XCanBus_PluginEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.m_key = XString_create_copy(&key);
    entry.m_factory = factory;
    entry.m_loaded = true;

    XMapBase_insert_base((XMapBase*)canBus->m_plugins, &key, &entry);

    XClass_deinit_base((XClass*)&key);
    return true;
}

XVector* XCanBus_availableDevices(const XCanBus* canBus, const char* plugin, char** errorMessage)
{
    if (!canBus || !canBus->m_plugins || !plugin) {
        if (errorMessage) *errorMessage = XMemory_strdup("Invalid arguments");
        return NULL;
    }

    XString key;
    XString_init(&key);
    XString_assign_utf8(&key, plugin);

    XCanBus_PluginEntry* entry = (XCanBus_PluginEntry*)XMap_value_base(canBus->m_plugins, &key);
    XClass_deinit_base((XClass*)&key);

    if (!entry || !entry->m_factory) {
        if (errorMessage) {
            XString* err = XString_create_fmt_utf8("No such plugin: '%s'", plugin);
            *errorMessage = XMemory_strdup(XString_toUtf8(err));
            XString_delete_base(err);
        }
        return NULL;
    }

    return XCanBusFactory_availableDevices(entry->m_factory, errorMessage);
}

XVector* XCanBus_availableDevices_all(const XCanBus* canBus, char** errorMessage)
{
    if (!canBus || !canBus->m_plugins) {
        if (errorMessage) *errorMessage = XMemory_strdup("No plugins registered");
        return NULL;
    }

    XVector* result = XVector_create(sizeof(XCanBusDeviceInfo));

    XMap_iterator it = XMap_begin(canBus->m_plugins);
    while (!XMap_iterator_isEnd(&it)) {
        XPair* pair = XMap_iterator_data(&it);
        if (pair) {
            XCanBus_PluginEntry* entry = (XCanBus_PluginEntry*)XPair_second(pair);
            if (entry && entry->m_factory) {
                char* pluginError = NULL;
                XVector* devices = XCanBusFactory_availableDevices(entry->m_factory, &pluginError);
                if (devices) {
                    // 合并到结果列表
                    size_t count = XVector_size_base(devices);
                    for (size_t i = 0; i < count; i++) {
                        XCanBusDeviceInfo* info = (XCanBusDeviceInfo*)XVector_at_base(devices, i);
                        if (info) {
                            XVector_push_back_1_base(result, info);
                        }
                    }
                    XVector_delete_base(devices);
                }
                if (pluginError) XFree_System(pluginError);
            }
        }
        XMap_iterator_add(canBus->m_plugins, &it);
    }

    return result;
}

XCanBusDevice* XCanBus_createDevice(const XCanBus* canBus,
    const char* plugin, const char* interfaceName, char** errorMessage)
{
    if (!canBus || !canBus->m_plugins || !plugin || !interfaceName) {
        if (errorMessage) *errorMessage = XMemory_strdup("Invalid arguments");
        return NULL;
    }

    XString key;
    XString_init(&key);
    XString_assign_utf8(&key, plugin);

    XCanBus_PluginEntry* entry = (XCanBus_PluginEntry*)XMap_value_base(canBus->m_plugins, &key);
    XClass_deinit_base((XClass*)&key);

    if (!entry || !entry->m_factory) {
        if (errorMessage) {
            XString* err = XString_create_fmt_utf8("No such plugin: '%s'", plugin);
            *errorMessage = XMemory_strdup(XString_toUtf8(err));
            XString_delete_base(err);
        }
        return NULL;
    }

    return XCanBusFactory_createDevice(entry->m_factory, interfaceName, errorMessage);
}
