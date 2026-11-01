#include "XCan_config.h"
#if XPROTOCOL_ON
#if XCAN_ON
#if XCAN_BUS_ON
#include "XCanBus.h"
#include <string.h>
#include "XMemory.h"
#include "XStringUtils.h"
#include "XString.h"

// =============== 全局单例 ===============
static XCanBus* g_canBusInstance = NULL;

// =============== 虚函数前置声明 ===============
static void VXCanBus_deinit(XCanBus* canBus);

// =============== 容器值侧回调 ===============
/* m_plugins 的值为 XCanBus_PluginEntry（持有堆上 m_key XString 与
 * 所有权移交的 m_factory）。工厂是插件唯一实例、无法克隆，故值侧只注册
 * 移动/析构回调，并以 useCow=false 创建映射，杜绝 COW 分离时按位浅拷
 * 共享指针导致的双重释放；条目释放统一走容器析构契约，不再手工遍历。 */
static void pluginEntryMove(void* dest, const void* src)
{
    XCanBus_PluginEntry* d = (XCanBus_PluginEntry*)dest;
    XCanBus_PluginEntry* s = (XCanBus_PluginEntry*)src;
    d->m_key = s->m_key;
    s->m_key = NULL;
    d->m_factory = s->m_factory;
    s->m_factory = NULL;
    d->m_loaded = s->m_loaded;
}

static void pluginEntryDeinit(void* data)
{
    XCanBus_PluginEntry* entry = (XCanBus_PluginEntry*)data;
    if (!entry) return;
    if (entry->m_key) {
        XClassDelete((XClass*)entry->m_key);
        entry->m_key = NULL;
    }
    if (entry->m_factory) {
        XCanBusFactory_destroy(entry->m_factory);
        entry->m_factory = NULL;
    }
    entry->m_loaded = false;
}

/* 可用设备列表的元素回调：XCanBusDeviceInfo 持有 5 个堆上 XString*，
 * 禁止按位 memcpy 转移属主（内存管理注意事项：不能用 memcpy 复制含
 * 所有权元素），插入走深拷贝、删除走元素析构。 */
static void canBusDeviceInfoCopyMethod(void* dest, const void* src)
{
    XCanBusDeviceInfo_copy((XCanBusDeviceInfo*)dest, (const XCanBusDeviceInfo*)src);
}

static void canBusDeviceInfoDeinitMethod(void* data)
{
    XCanBusDeviceInfo_deinit((XCanBusDeviceInfo*)data);
}

// =============== 析构 ===============
static void VXCanBus_deinit(XCanBus* canBus)
{
    if (!canBus) return;

    // 插件条目（键串与工厂）由 m_plugins 注册的值侧回调统一释放
    if (canBus->m_plugins) {
        XClassDelete(canBus->m_plugins);
        canBus->m_plugins = NULL;
    }

    XClass_Deinit_Parent(XObject, canBus);
}

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

// =============== 初始化 ===============
void XCanBus_init(XCanBus* canBus)
{
    if (!canBus) return;

    XObject_init((XObject*)canBus);
    XClassGetVtable(canBus) = XCanBus_class_init();

    /* 创建插件映射表：键为 XString，值为 XCanBus_PluginEntry。
     * 值含不可克隆的所有权指针，关闭 COW 并注册值侧移动/析构回调。 */
    canBus->m_plugins = XMap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(XString), sizeof(XCanBus_PluginEntry), XString_compare, false);
    XMapBaseSetKeyCopyMethod(canBus->m_plugins, XClass_copy_base);
    XMapBaseSetKeyMoveMethod(canBus->m_plugins, XClass_move_base);
    XMapBaseSetKeyDeinitMethod(canBus->m_plugins, XClass_deinit_base);
    XContainerSetDataMoveMethod(canBus->m_plugins, pluginEntryMove);
    XContainerSetDataDeinitMethod(canBus->m_plugins, pluginEntryDeinit);
}

// =============== 单例访问 ===============
XCanBus* XCanBus_instance(void)
{
    if (!g_canBusInstance) {
        g_canBusInstance = (XCanBus*)XClass_Malloc(XCanBus);
        if (g_canBusInstance) {
            XCanBus_init(g_canBusInstance);
            Set_Class_IsHeap(g_canBusInstance, true);
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

    /* 重复键拒绝注册：同名插件重复注册会覆盖旧条目（工厂被直接丢弃），
     * 故此处先行拒绝；条目释放统一走 m_plugins 的值侧析构回调 */
    if (XMap_value_base(canBus->m_plugins, &key)) {
        XCanBusFactory_destroy(factory);   /* 所有权已转移，注册失败由本函数销毁 */
        XClassDeinit((XClass*)&key);
        return false;
    }

    XCanBus_PluginEntry entry;
    memset(&entry, 0, sizeof(entry));
    entry.m_key = XString_create_copy(&key);
    entry.m_factory = factory;
    entry.m_loaded = true;

    /* 移动语义插入：值侧回调把 entry 所有权转移给容器。插入失败后
     * entry.m_key 非 NULL 即容器尚未接管（缓冲分配失败）：本函数创建的
     * 键串就地回收，已按契约（XCanBus.h：所有权转移给 XCanBus）转移的
     * 工厂同重复键拒绝路径一并销毁，未入表不得悬空交还调用者；
     * entry.m_key 为 NULL 则容器已接管并在其失败清理中释放，
     * 此处不得二次释放 */
    bool ok = XMapBase_insert_move_base((XMapBase*)canBus->m_plugins, &key, &entry);

    XClassDeinit((XClass*)&key);
    if (!ok && entry.m_key) {
        XClassDelete((XClass*)entry.m_key);
        XCanBusFactory_destroy(factory);   /* 契约所有权已转移，未入表由本函数销毁 */
        return false;
    }
    return ok;
}

XVector* XCanBus_availableDevices(const XCanBus* canBus, const char* plugin, char** errorMessage)
{
    if (!canBus || !canBus->m_plugins || !plugin) {
        if (errorMessage) *errorMessage = XStrdup("Invalid arguments");
        return NULL;
    }

    XString key;
    XString_init(&key);
    XString_assign_utf8(&key, plugin);

    XCanBus_PluginEntry* entry = (XCanBus_PluginEntry*)XMap_value_base(canBus->m_plugins, &key);
    XClassDeinit((XClass*)&key);

    if (!entry || !entry->m_factory) {
        if (errorMessage) {
            XString* err = XString_create_fmt_utf8("No such plugin: '%s'", plugin);
            *errorMessage = XStrdup(XString_toUtf8(err));
            XClassDelete(err);
        }
        return NULL;
    }

    return XCanBusFactory_availableDevices(entry->m_factory, errorMessage);
}

XVector* XCanBus_availableDevices_all(const XCanBus* canBus, char** errorMessage)
{
    if (!canBus || !canBus->m_plugins) {
        if (errorMessage) *errorMessage = XStrdup("No plugins registered");
        return NULL;
    }

    XVector* result = XVector_Create(XCanBusDeviceInfo);
    /* 注册元素深拷贝/析构：XCanBusDeviceInfo 含 5 个堆 XString* 成员，禁止按
     * 字节浅拷（memcpy 复制对象会使属主断裂：源表删除后本表悬垂，或调用者
     * 释放本表时字符串整批泄漏）。合并时经 XCanBusDeviceInfo_copy 深拷贝，
     * 调用者释放列表时逐元素经 XCanBusDeviceInfo_deinit 释放字符串成员 */
    XContainerSetDataCopyMethod(result, canBusDeviceInfoCopyMethod);
    XContainerSetDataDeinitMethod(result, canBusDeviceInfoDeinitMethod);

    XMap_iterator it = XMap_begin(canBus->m_plugins);
    while (!XMap_iterator_isEnd(&it)) {
        XPair* pair = XMap_iterator_data(&it);
        if (pair) {
            XCanBus_PluginEntry* entry = (XCanBus_PluginEntry*)XPair_second(pair);
            if (entry && entry->m_factory) {
                char* pluginError = NULL;
                XVector* devices = XCanBusFactory_availableDevices(entry->m_factory, &pluginError);
                if (devices) {
                    /* 源表元素析构登记：devices 由本函数创建者身份释放，
                     * 逐元素析构后属主才算交割完整 */
                    XContainerSetDataDeinitMethod(devices, canBusDeviceInfoDeinitMethod);
                    // 合并到结果列表
                    size_t count = XVector_size_base(devices);
                    for (size_t i = 0; i < count; i++) {
                        XCanBusDeviceInfo* info = (XCanBusDeviceInfo*)XVector_at_base(devices, i);
                        if (info) {
                            XVector_push_back_1_base(result, info);
                        }
                    }
                    XClassDelete(devices);
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
        if (errorMessage) *errorMessage = XStrdup("Invalid arguments");
        return NULL;
    }

    XString key;
    XString_init(&key);
    XString_assign_utf8(&key, plugin);

    XCanBus_PluginEntry* entry = (XCanBus_PluginEntry*)XMap_value_base(canBus->m_plugins, &key);
    XClassDeinit((XClass*)&key);

    if (!entry || !entry->m_factory) {
        if (errorMessage) {
            XString* err = XString_create_fmt_utf8("No such plugin: '%s'", plugin);
            *errorMessage = XStrdup(XString_toUtf8(err));
            XClassDelete(err);
        }
        return NULL;
    }

    return XCanBusFactory_createDevice(entry->m_factory, interfaceName, errorMessage);
}

#endif /* XCAN_BUS_ON */
#endif /* XCAN_ON */
#endif /* XPROTOCOL_ON */
