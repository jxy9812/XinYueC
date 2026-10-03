/**
 * @file       XVirtualKeyboardDictionaryManager.c
 * @brief      XVirtualKeyboardDictionaryManager 词典管理器单例实现
 *             （注册表/列表管理面；availableDictionaries 恒空）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"

#if XVIRTUALKEYBOARD_ON

#include "XVirtualKeyboardDictionaryManager.h"
#include "XVirtualKeyboardDictionaryManager_Protected.h"
#include "XStringUtils.h"
#include "XMemory.h"

/** @brief 词典注册表容量上限。 */
#define XVKDM_DICT_MAX 16
/** @brief 集合列表容量上限（base/extra/active 共用）。 */
#define XVKDM_LIST_MAX 16

/** @brief 词典注册表条目。 */
typedef struct XvkdmDictEntry
{
    XVirtualKeyboardDictionary* m_dict; /**< 词典（管理器拥有）。 */
    bool m_isExtra;                     /**< true=extra 集；false=base 集。 */
    bool m_isActive;                    /**< 是否在激活集。 */
} XvkdmDictEntry;

/** @brief 私有数据块。 */
typedef struct XVirtualKeyboardDictionaryManagerPrivate
{
    XvkdmDictEntry m_dicts[XVKDM_DICT_MAX]; /**< 词典注册表。 */
    int m_dictCount;                        /**< 条目数。 */
} XVirtualKeyboardDictionaryManagerPrivate;

/** @brief 进程单例指针。 */
static XVirtualKeyboardDictionaryManager* s_xvkdmInstance = NULL;

/** @brief 内部取私有块。 */
static XVirtualKeyboardDictionaryManagerPrivate* xvkdm_priv(
        const XVirtualKeyboardDictionaryManager* self)
{
    return (self && self->m_data)
               ? (XVirtualKeyboardDictionaryManagerPrivate*)self->m_data
               : NULL;
}

/** @brief 发射信号。 */
static void xvkdm_emit0(XVirtualKeyboardDictionaryManager* self,
                        size_t signal)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
}

/** @brief 按名查条目；未找到返回 NULL。 */
static XvkdmDictEntry* xvkdm_find(XVirtualKeyboardDictionaryManagerPrivate* priv,
                                  const char* name)
{
    int i;
    if (!priv || !name) return NULL;
    for (i = 0; i < priv->m_dictCount; ++i) {
        if (priv->m_dicts[i].m_dict &&
            XStrcmp(XVirtualKeyboardDictionary_name(priv->m_dicts[i].m_dict),
                    name) == 0)
            return &priv->m_dicts[i];
    }
    return NULL;
}

/* 前向声明。 */
static void XVkdm_deinit(XVirtualKeyboardDictionaryManager* self);
void XVirtualKeyboardDictionaryManager_init(
        XVirtualKeyboardDictionaryManager* self);

XVtable* XVirtualKeyboardDictionaryManager_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVirtualKeyboardDictionaryManager)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XVkdm_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief 反初始化：销毁全部词典与私有块后调父类。 */
static void XVkdm_deinit(XVirtualKeyboardDictionaryManager* self)
{
    XVirtualKeyboardDictionaryManagerPrivate* priv;
    int i;
    if (!self) return;
    priv = xvkdm_priv(self);
    if (priv) {
        for (i = 0; i < priv->m_dictCount; ++i) {
            if (priv->m_dicts[i].m_dict) {
                XClassDelete(
                    priv->m_dicts[i].m_dict);
                priv->m_dicts[i].m_dict = NULL;
            }
        }
        priv->m_dictCount = 0;
        XFree_System(priv);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

void XVirtualKeyboardDictionaryManager_init(
        XVirtualKeyboardDictionaryManager* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XVirtualKeyboardDictionaryManager);
    self->m_data =
        XMalloc_System(sizeof(XVirtualKeyboardDictionaryManagerPrivate));
    if (self->m_data)
        XMemset(self->m_data, 0,
                sizeof(XVirtualKeyboardDictionaryManagerPrivate));
}

XVirtualKeyboardDictionaryManager*
XVirtualKeyboardDictionaryManager_instance(void)
{
    if (!s_xvkdmInstance) {
        XVirtualKeyboardDictionaryManager* self =
            (XVirtualKeyboardDictionaryManager*)XMalloc_System(
                sizeof(XVirtualKeyboardDictionaryManager));
        if (!self) return NULL;
        XVirtualKeyboardDictionaryManager_init(self);
        Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
        Set_Class_IsHeap(self, true);
        s_xvkdmInstance = self;
    }
    return s_xvkdmInstance;
}

XVirtualKeyboardDictionary*
XVirtualKeyboardDictionaryManager_createDictionary(
        XVirtualKeyboardDictionaryManager* self, const char* name)
{
    XVirtualKeyboardDictionaryManagerPrivate* priv = xvkdm_priv(self);
    XvkdmDictEntry* existing;
    if (!priv || !name || !name[0]) return NULL;
    existing = xvkdm_find(priv, name);
    if (existing) return existing->m_dict; /* 同名已存在返回既有。 */
    if (priv->m_dictCount >= XVKDM_DICT_MAX) return NULL;
    {
        XVirtualKeyboardDictionary* dict =
            XVirtualKeyboardDictionary_create_named(name);
        if (!dict) return NULL;
        priv->m_dicts[priv->m_dictCount].m_dict = dict;
        priv->m_dicts[priv->m_dictCount].m_isExtra = false;
        priv->m_dicts[priv->m_dictCount].m_isActive = false;
        ++priv->m_dictCount;
        /* 创建即落 base 集（Qt createDictionary 落 base 口径）。 */
        xvkdm_emit0(self, (size_t)
                    XVirtualKeyboardDictionaryManager_baseDictionariesChanged_signal(
                        NULL));
        return dict;
    }
}

XVirtualKeyboardDictionary* XVirtualKeyboardDictionaryManager_dictionary(
        const XVirtualKeyboardDictionaryManager* self, const char* name)
{
    XVirtualKeyboardDictionaryManagerPrivate* priv = xvkdm_priv(self);
    XvkdmDictEntry* entry = xvkdm_find(priv, name);
    return entry ? entry->m_dict : NULL;
}

bool XVirtualKeyboardDictionaryManager_removeDictionary(
        XVirtualKeyboardDictionaryManager* self, const char* name)
{
    XVirtualKeyboardDictionaryManagerPrivate* priv = xvkdm_priv(self);
    int i;
    int index = -1;
    if (!priv || !name) return false;
    for (i = 0; i < priv->m_dictCount; ++i) {
        if (priv->m_dicts[i].m_dict &&
            XStrcmp(XVirtualKeyboardDictionary_name(priv->m_dicts[i].m_dict),
                    name) == 0) {
            index = i;
            break;
        }
    }
    if (index < 0) return false;
    XClassDelete(priv->m_dicts[index].m_dict);
    for (i = index; i + 1 < priv->m_dictCount; ++i)
        priv->m_dicts[i] = priv->m_dicts[i + 1];
    --priv->m_dictCount;
    xvkdm_emit0(self, (size_t)
                XVirtualKeyboardDictionaryManager_baseDictionariesChanged_signal(
                    NULL));
    xvkdm_emit0(self, (size_t)
                XVirtualKeyboardDictionaryManager_activeDictionariesChanged_signal(
                    NULL));
    return true;
}

/** @brief 收集集合名列表（extra/base/active 三态过滤共用）。 */
static int xvkdm_collect(const XVirtualKeyboardDictionaryManagerPrivate* priv,
                         bool extraOnly, bool activeOnly,
                         const char** outNames, int maxCount)
{
    int i;
    int written = 0;
    for (i = 0; i < priv->m_dictCount; ++i) {
        const XvkdmDictEntry* e = &priv->m_dicts[i];
        if (extraOnly && !e->m_isExtra) continue;
        if (activeOnly && !e->m_isActive) continue;
        if (outNames && written < maxCount)
            outNames[written] =
                XVirtualKeyboardDictionary_name(e->m_dict);
        ++written;
    }
    return written;
}

int XVirtualKeyboardDictionaryManager_availableDictionaries(
        const XVirtualKeyboardDictionaryManager* self, const char** outNames,
        int maxCount)
{
    (void)outNames;
    (void)maxCount;
    (void)self;
    return 0; /* 本轮恒空（无内容源；如实标注）。 */
}

int XVirtualKeyboardDictionaryManager_baseDictionaries(
        const XVirtualKeyboardDictionaryManager* self, const char** outNames,
        int maxCount)
{
    XVirtualKeyboardDictionaryManagerPrivate* priv = xvkdm_priv(self);
    if (!priv) return 0;
    return xvkdm_collect(priv, false, false, outNames, maxCount);
}

int XVirtualKeyboardDictionaryManager_extraDictionaries(
        const XVirtualKeyboardDictionaryManager* self, const char** outNames,
        int maxCount)
{
    XVirtualKeyboardDictionaryManagerPrivate* priv = xvkdm_priv(self);
    if (!priv) return 0;
    return xvkdm_collect(priv, true, false, outNames, maxCount);
}

int XVirtualKeyboardDictionaryManager_activeDictionaries(
        const XVirtualKeyboardDictionaryManager* self, const char** outNames,
        int maxCount)
{
    XVirtualKeyboardDictionaryManagerPrivate* priv = xvkdm_priv(self);
    if (!priv) return 0;
    return xvkdm_collect(priv, false, true, outNames, maxCount);
}

/** @brief 集合设置共用体（名字须已创建；flags=extra/active 位）。 */
static bool xvkdm_setSet(XVirtualKeyboardDictionaryManager* self,
                         const char* const* names, int count, bool extra,
                         bool active)
{
    XVirtualKeyboardDictionaryManagerPrivate* priv = xvkdm_priv(self);
    int i;
    if (!priv) return false;
    if (count > XVKDM_LIST_MAX) return false;
    for (i = 0; names && i < count; ++i) {
        XvkdmDictEntry* entry = xvkdm_find(priv, names[i]);
        if (!entry) return false; /* 未创建的名字：整体拒绝。 */
    }
    for (i = 0; i < priv->m_dictCount; ++i) {
        if (extra) priv->m_dicts[i].m_isExtra = false;
        if (active) priv->m_dicts[i].m_isActive = false;
    }
    for (i = 0; names && i < count; ++i) {
        XvkdmDictEntry* entry = xvkdm_find(priv, names[i]);
        if (extra) entry->m_isExtra = true;
        if (active) entry->m_isActive = true;
    }
    return true;
}

bool XVirtualKeyboardDictionaryManager_setBaseDictionaries(
        XVirtualKeyboardDictionaryManager* self, const char* const* names,
        int count)
{
    if (!xvkdm_setSet(self, names, count, false, false)) return false;
    /* base 集=全部非 extra（本轮简化：setBase 不支持裁剪既有 base，
       语义对标「重置 base 集为全量注册表」）。 */
    xvkdm_emit0(self, (size_t)
                XVirtualKeyboardDictionaryManager_baseDictionariesChanged_signal(
                    NULL));
    return true;
}

bool XVirtualKeyboardDictionaryManager_setExtraDictionaries(
        XVirtualKeyboardDictionaryManager* self, const char* const* names,
        int count)
{
    if (!xvkdm_setSet(self, names, count, true, false)) return false;
    xvkdm_emit0(self, (size_t)
                XVirtualKeyboardDictionaryManager_extraDictionariesChanged_signal(
                    NULL));
    return true;
}

bool XVirtualKeyboardDictionaryManager_setActiveDictionaries(
        XVirtualKeyboardDictionaryManager* self, const char* const* names,
        int count)
{
    if (!xvkdm_setSet(self, names, count, false, true)) return false;
    xvkdm_emit0(self, (size_t)
                XVirtualKeyboardDictionaryManager_activeDictionariesChanged_signal(
                    NULL));
    return true;
}

/* ==================== 信号（纯 ID getter） ==================== */

void* XVirtualKeyboardDictionaryManager_availableDictionariesChanged_signal(
        XVirtualKeyboardDictionaryManager* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardDictionaryManager_availableDictionariesChanged_signal;
}

void* XVirtualKeyboardDictionaryManager_baseDictionariesChanged_signal(
        XVirtualKeyboardDictionaryManager* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardDictionaryManager_baseDictionariesChanged_signal;
}

void* XVirtualKeyboardDictionaryManager_extraDictionariesChanged_signal(
        XVirtualKeyboardDictionaryManager* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardDictionaryManager_extraDictionariesChanged_signal;
}

void* XVirtualKeyboardDictionaryManager_activeDictionariesChanged_signal(
        XVirtualKeyboardDictionaryManager* self)
{
    (void)self;
    return (void*)(size_t)
        XVirtualKeyboardDictionaryManager_activeDictionariesChanged_signal;
}

#endif /* XVIRTUALKEYBOARD_ON */
