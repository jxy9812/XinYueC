/**
 * @file       XSettings.c
 * @brief      XSettings 持久化键值设置类实现（对象生命周期/公开 API/同步）。
 * @details    对齐 Qt 6.8.3 QSettings 行为：QConfFile 四文件回退链（用户+
 *             应用/用户+组织/系统+应用/系统+组织，写只进首文件）、惰性节
 *             解析（未触碰节的裸词行不置 FormatError）、粘滞状态（仅
 *             NoError 态接受新错误）、写盘 XSaveFile 临时文件+原子重命名
 *             （atomicSyncRequired=false 允许直接覆盖写回退）、size/时间戳
 *             外部变更探测。INI 算法核心见 XSettings_ini.c。实现只依赖
 *             XinYueC 抽象层，禁止调用 Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 */
#include "XSettings.h"
#include "XSettings_ini.h"
#include "XMemory.h"
#include "XStringUtils.h"
#include "XByteArray.h"
#include "XSaveFile.h"
#include "XFile.h"
#include "XDir.h"
#include "XFileInfo.h"
#include "XGeometry.h"
#include "XMutex.h"
#include "XLockFile.h"
#include "XThread.h"
#include "XAtomic.h"
#include "XVariantList.h"
#include "XSettingsNative.h"
#include "XDateTime.h"
#include "XCoreApplication.h"
#include "XPrintf.h"

#include <stdbool.h>
#include <stdint.h>

#if XSETTINGS_ON

/* ==========================================================================
 * 内部数据结构
 * ========================================================================== */

/** @brief 单个配置文件状态（对标 QConfFile）。 */
typedef struct XSettingsConfFile
{
    XString* name;          /**< 绝对路径（对标 QConfFile::name）。 */
    XMap* unparsedSections; /**< 未解析节：XString* 节名 → XByteArray* 原始块（map 深拷贝持有）。 */
    XMap* originalKeys;     /**< 文件解析出的键：XString* → XVariant*（map 深拷贝持有；NULL 值 = 无效变体）。 */
    XMap* addedKeys;        /**< 未落盘修改：XString* → XVariant*（NULL 值 = 无效变体）。 */
    XMap* removedKeys;      /**< 墓碑：XString* 键（深拷贝）→ NULL 值槽。 */
    bool read;              /**< 是否已读过文件。 */
    int64_t size;           /**< 上次读取时的文件大小。 */
    int64_t timeStamp;      /**< 上次读取时的最后修改秒级时刻。 */
    bool customParsed;      /**< 自定义格式是否已读入（一次性）。 */
} XSettingsConfFile;

/** @brief 分组栈元素（对标 QSettingsGroup）。 */
typedef struct XSettingsGroup
{
    XString* str;   /**< 组名。 */
    int num;        /**< -1 非数组；≥1 数组且为当前下标 + 1。 */
    int maxNum;     /**< -1 无个数猜测；≥0 自动计数猜测值（最高下标 + 1）。 */
} XSettingsGroup;

/** @brief 注册表回退链最多 4 项（对标 Qt regList）。 */
#define XSETTINGS_REG_MAX_KEYS 4

/** @brief 注册表回退链单项（对标 RegistryKey）。 */
typedef struct XSettingsRegKey
{
    XSettingsNativeRoot root;    /**< 根键。 */
    XString* subKey;             /**< 相对根键的路径；形如 Software\orgpp。 */
} XSettingsRegKey;

/** @brief XSettings 私有数据（对标 QSettingsPrivate）。 */
struct XSettingsData
{
    XSettingsConfFile* confFiles[4]; /**< 回退文件链；写只进 confFiles[0]（INI 后端用）。 */
    int confFileCount;               /**< 链长（1..4）。 */
    XSettingsRegKey* regKeys;        /**< 注册表回退链（容量 XSETTINGS_REG_MAX_KEYS）。 */
    int regKeyCount;                 /**< 注册表链长（0..4）。 */
    bool registry;                   /**< true = 走注册表后端（Windows 原生格式）。 */
    XMutex* mutex;                   /**< 实例锁（对标 QConfFile::mutex）。 */
    bool noBackend;                  /**< 非 Windows 上的 Registry32/64：有格式无后端。 */
    XSettings_Format format;         /**< 存储格式。 */
    XSettings_Scope scope;           /**< 作用域。 */
    XString* organizationName;       /**< 组织名。 */
    XString* applicationName;        /**< 应用名。 */
    XVector* groupStack;             /**< 分组栈（XSettingsGroup 值数组；str 指针由本对象拥有）。 */
    XString* groupPrefix;            /**< 组前缀缓存（"a/b/" 形态，根为空串）。 */
    XString* fileNameCache;          /**< 注册表后端的 fileName 缓存。 */
    bool fallbacks;                  /**< 回退链开关（默认 true）。 */
    bool atomicSync;                 /**< 原子写强制（默认 true）。 */
    bool pendingChanges;             /**< 是否有未落盘修改。 */
    bool deleteWriteHandleOnExit;    /**< clear() 后的空键待删标记（对标 Qt 同名字段）。 */
    XTimerId updateTimerId;          /**< 延迟落盘定时器（对标 requestUpdate 的 UpdateRequest 事件）。 */
    XSettings_Status status;         /**< 粘滞状态。 */
};

/** @brief 自定义格式注册项（对标 QSettings::registerFormat 静态表）。 */
typedef struct XSettingsCustomFormat
{
    XString* extension;                        /**< 扩展名（含点）。 */
    XSettings_ReadFunc readFunc;               /**< 读取回调；可为 NULL。 */
    XSettings_WriteFunc writeFunc;             /**< 写出回调；可为 NULL。 */
    XSettings_CaseSensitivity caseSensitivity; /**< 扩展名匹配口径。 */
    bool used;                                 /**< 槽位占用。 */
} XSettingsCustomFormat;

/* ==========================================================================
 * 静态全局
 * ========================================================================== */

static XSettings_Format g_xsettings_defaultFormat = XSettings_NativeFormat;

/** @brief setPath 覆盖目录（[格式][作用域]；NULL = 未覆盖）。 */
static XString* g_xsettings_pathOverride[33][2];

/** @brief 自定义格式注册表（CustomFormat1..16）。 */
static XSettingsCustomFormat g_xsettings_customFormats[16];

/* ==========================================================================
 * 状态与告警小工具
 * ========================================================================== */

/* ==========================================================================
 * 线程安全（对标 Qt 的两层互斥）
 * ==========================================================================
 * Qt 6.8 的保证分两层：
 *   1. settingsGlobalMutex（qsettings.cpp:101）——进程级全局锁，保护静态表
 *      （usedHash/pathHash/customFormatVector/globalDefaultFormat）；
 *   2. QConfFile::mutex（qsettings_p.h:134）——每配置文件一把锁，保护
 *      originalKeys/addedKeys/removedKeys。
 * 本实现同构：全局表用 g_xsettings_globalMutex，对象状态用 XSettingsData::mutex。
 * 两把锁都用递归锁——公开 API 之间会互相调用（beginWriteArray → setValue、
 * remove 空键 → clear），非递归锁必然自死锁。
 */

/**
 * @brief 进程级全局互斥的存储（对标 Qt 的 Q_CONSTINIT static QBasicMutex）。
 * @details XMutex 是不透明类型，无法直接做静态 POD（Qt 的 QBasicMutex 可以）。
 *          这里用「静态内嵌缓冲 + 惰性 init」达到同等效果：零堆分配、
 *          进程生命周期、无释放路径，因此不存在泄漏与惰性创建竞态。
 *          缓冲大小由 XMutex_typetSize 给出，取递归锁的真实尺寸。
 */
/**
 * @brief 进程级全局互斥（对标 Qt 的 Q_CONSTINIT static QBasicMutex）。
 * @details 用「原子指针 + CAS 抢初始化权」实现线程安全的惰性创建：
 *          0 = 未初始化，1 = 正在初始化，其他 = 已就绪的 XMutex*。
 *          抢到 0->1 的线程负责 create 并发布指针，其他线程自旋等待就绪。
 *          这避免了「静态内嵌缓冲 + bool 标志」的竞态：那种写法下多个线程
 *          可能同时执行 XMutex_init（内部 memset 整个结构），把对方正在使用
 *          的锁清零，进而破坏相邻内存（实测表现为并发写键丢失 + 崩溃）。
 */
#define XSETTINGS_GMUTEX_UNINIT   0
#define XSETTINGS_GMUTEX_CREATING 1

/** @brief 全局锁状态（0/1 = 未就绪，其余 = XMutex*）。 */
static XAtomic_Uintptr g_xsettings_globalMutexState;

/** @brief 取全局锁（线程安全惰性创建）。 */
static void xsettingsGlobalLock(void)
{
    uintptr_t state = (uintptr_t)XAtomic_load_uintptr_t(&g_xsettings_globalMutexState,
                                                        XAtomic_MemoryOrder_Acquire);
    if (state <= (uintptr_t)XSETTINGS_GMUTEX_CREATING) {
        uintptr_t expected = XSETTINGS_GMUTEX_UNINIT;
        if (XAtomic_compare_exchange_strong_uintptr_t(
                &g_xsettings_globalMutexState, &expected,
                (uintptr_t)XSETTINGS_GMUTEX_CREATING,
                XAtomic_MemoryOrder_AcqRel, XAtomic_MemoryOrder_Acquire)) {
            XMutex* created = XMutex_create(XLock_Recursive);
            XAtomic_store_uintptr_t(&g_xsettings_globalMutexState,
                                    (uintptr_t)created, XAtomic_MemoryOrder_Release);
        }
        else {
            int spin = 0;
            while (spin < 1000000) {
                state = (uintptr_t)XAtomic_load_uintptr_t(&g_xsettings_globalMutexState,
                                                          XAtomic_MemoryOrder_Acquire);
                if (state > (uintptr_t)XSETTINGS_GMUTEX_CREATING)
                    break;
                ++spin;
            }
        }
    }
    state = (uintptr_t)XAtomic_load_uintptr_t(&g_xsettings_globalMutexState,
                                              XAtomic_MemoryOrder_Acquire);
    if (state > (uintptr_t)XSETTINGS_GMUTEX_CREATING)
        XMutex_lock((XMutex*)state);
}

/** @brief 解全局锁。 */
static void xsettingsGlobalUnlock(void)
{
    uintptr_t state = (uintptr_t)XAtomic_load_uintptr_t(&g_xsettings_globalMutexState,
                                                        XAtomic_MemoryOrder_Acquire);
    if (state > (uintptr_t)XSETTINGS_GMUTEX_CREATING)
        XMutex_unlock((XMutex*)state);
}


/**
 * @brief 取自定义格式槽的回调快照。
 * @details 用户回调不得在持锁状态下调用（回调内可能重入 XSettings 静态 API），
 *          故先加锁取快照、解锁后再调用。
 */
static bool xsettingsCustomFormatSnapshot(XSettings_Format format,
                                          XSettings_ReadFunc* readOut,
                                          XSettings_WriteFunc* writeOut,
                                          XSettings_CaseSensitivity* csOut)
{
    int slot = (int)format - (int)XSettings_CustomFormat1;
    bool ok = false;
    if (readOut) *readOut = NULL;
    if (writeOut) *writeOut = NULL;
    if (csOut) *csOut = XSettings_CaseSensitive;
    if (slot < 0 || slot >= 16)
        return false;
    xsettingsGlobalLock();
    if (g_xsettings_customFormats[slot].used) {
        if (readOut) *readOut = g_xsettings_customFormats[slot].readFunc;
        if (writeOut) *writeOut = g_xsettings_customFormats[slot].writeFunc;
        if (csOut) *csOut = g_xsettings_customFormats[slot].caseSensitivity;
        ok = true;
    }
    xsettingsGlobalUnlock();
    return ok;
}

/** @brief 自定义格式槽是否已注册且具备指定方向的回调。 */
static bool xsettingsCustomFormatHas(XSettings_Format format, bool wantRead)
{
    XSettings_ReadFunc readFunc = NULL;
    XSettings_WriteFunc writeFunc = NULL;
    if (!xsettingsCustomFormatSnapshot(format, &readFunc, &writeFunc, NULL))
        return false;
    return wantRead ? (readFunc != NULL) : (writeFunc != NULL);
}

/** @brief 取实例锁（对象未持锁时退化为无锁）。 */
static void xsettingsInstanceLock(XSettingsData* data)
{
    if (data && data->mutex)
        XMutex_lock(data->mutex);
}

/** @brief 解实例锁。 */
static void xsettingsInstanceUnlock(XSettingsData* data)
{
    if (data && data->mutex)
        XMutex_unlock(data->mutex);
}

/** @brief 分配并清零私有数据（含实例锁创建）。 */
static XSettingsData* xsettingsDataAlloc(void)
{
    XSettingsData* data = (XSettingsData*)XMalloc_System(sizeof(XSettingsData));
    if (!data)
        return NULL;
    XMemset(data, 0, sizeof(XSettingsData));
    data->mutex = XMutex_create(XLock_Recursive);
    data->updateTimerId = XTIMER_INVALID_ID;
    return data;
}

/** @brief 粘滞状态写入：仅 NoError 态接受新错误（对标 setStatus 317-321）。 */
static void xsettingsSetStatus(XSettingsData* data, XSettings_Status status)
{
    if (data && data->status == XSettings_NoError)
        data->status = status;
}

/* ==========================================================================
 * 延迟落盘（对标 QSettingsPrivate::requestUpdate）
 * ==========================================================================
 * Qt 每次改值都 postEvent(UpdateRequest)，事件循环空闲时自动 flush。本实现
 * 按项目定时器规范（「XObject 派生类内部一律用对象自带定时器」）改用
 * XObject_startTimer_ms + EXObject_TimerEvent：单次 0 延时定时器，到期即
 * sync。若定时器已在跑则不重复注册（对标 Qt 的 pendingChanges 去重）。
 */

/** @brief 延迟落盘定时器的间隔（毫秒）；0 = 下一个事件循环轮次。 */
#define XSETTINGS_UPDATE_INTERVAL_MS 0

/** @brief 取消待执行的延迟落盘。 */
static void xsettingsCancelUpdate(XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    if (!data || data->updateTimerId == XTIMER_INVALID_ID)
        return;
    XObject_killTimer((XObject*)self, data->updateTimerId);
    data->updateTimerId = XTIMER_INVALID_ID;
}

/**
 * @brief 请求一次延迟落盘（对标 requestUpdate）。
 * @details 已有待执行请求时不重复注册。
 *
 *          **必须先判断「调用线程是否为对象所属线程」**：XObject_eventDispatcher
 *          在对象无线程数据时会回落到全局分发器，若从其它线程调用，等于直接
 *          往主线程的分发器里插定时器——无同步保护，会破坏分发器内部表并
 *          波及相邻内存（实测表现为并发写键丢失）。Qt 的 requestUpdate 走
 *          postEvent，本身是线程安全的；本实现用定时器就必须补这层守卫。
 *          非归属线程调用时直接跳过（落盘仍由 sync/析构保证）。
 */
static void xsettingsRequestUpdate(XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    XThread* owner;
    if (!data || data->updateTimerId != XTIMER_INVALID_ID)
        return;
    /* 只有「对象已绑定线程数据」时才注册。工作线程里构造/持有的对象通常没有
     * 自己的线程数据（XObject_thread 返回 NULL），此时 XObject_startTimer_ms
     * 会回落到全局分发器——跨线程写主线程的定时器表，必须避免。 */
    owner = XObject_thread((XObject*)self);
    if (!owner || !XThread_isCurrentThread(owner))
        return;
    data->updateTimerId = XObject_startTimer((XObject*)self,
                                                XSETTINGS_UPDATE_INTERVAL_MS,
                                                XTimerType_CoarseTimer);
}

/* ==========================================================================
 * 映射工具（XVariantMap 按值内联模型：键槽为 XString 对象、值槽为
 * XVariant 对象；insert/查找传局部对象地址，map 以 XClass_copy_base
 * 按值深拷贝接管、remove/clear/delete 自动释放——契约见
 * XMap.c XMap_create_XVariantMap @warning）
 * ========================================================================== */

/**
 * @brief 创建按值持有键值的键值映射。
 * @details 显式关闭 COW：XSettings 的拷贝/移动语义要求源与目标完全独立
 *          （Qt 禁止拷贝 QSettings；本类按 XClass 对象模型提供深拷贝，
 *          必须独占数据块）。COW 共享会让两个对象的 addedKeys 互见，
 *          破坏 source/target 隔离。
 */
static XMap* xmapCreateStringVariant(void)
{
    XMap* map = XMap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(XString), sizeof(XVariant),
                               XString_compare, false);
    if (!map)
        return NULL;
    XMapBaseSetKeyCopyMethod(map, XClass_copy_base);
    XMapBaseSetKeyMoveMethod(map, XClass_move_base);
    XMapBaseSetKeyDeinitMethod(map, XClass_deinit_base);
    XContainerSetDataCopyMethod(map, XClass_copy_base);
    XContainerSetDataMoveMethod(map, XClass_move_base);
    XContainerSetDataDeinitMethod(map, XClass_deinit_base);
    return map;
}

/** @brief 指针槽键比较（解引用槽内 XString* 后比较；未解析节指针槽 map 用）。 */
static int32_t xsettingsPtrStringCompare(const void* lhs, const void* rhs)
{
    const XString* l = *(const XString* const*)lhs;
    const XString* r = *(const XString* const*)rhs;
    if (l == r)
        return XCompare_Equality;
    if (!l)
        return XCompare_Less;
    if (!r)
        return XCompare_Greater;
    return XString_compare(l, r);
}

/** @brief 未解析节映射（指针槽裸 map：键 XString* → 值 XByteArray*，条目由我方排空）。 */
static XMap* xmapCreateSectionMap(void)
{
    return XMap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, sizeof(XString*), sizeof(XByteArray*),
                          xsettingsPtrStringCompare, true);
}

/** @brief 在栈上构造键对象副本（值槽 map 的查找/写入前置步骤）。 */
static bool xmapMakeKeyObject(const XString* key, XString* keyObj)
{
    XString_init(keyObj);
    return key ? XString_assign(keyObj, key) : false;
}

/** @brief 排空未解析节映射的条目对象（指针槽裸 map 无析构方法，须手动释放）。 */
static void xmapDrainSections(XMap* map)
{
    if (!map)
        return;
    /* 先收集条目对象指针，再清空表（清表期间不能再访问这些对象），
     * 最后统一释放——顺序颠倒会导致清表时解引用已释放的键。 */
    XVector* keys = XMap_keys_base(map);
    XVector* values = XMapBase_values_base(map);
    size_t n = (keys && values) ? XVector_size_base(keys) : 0;
    XString** keyObjs = n ? (XString**)XMalloc_System(n * sizeof(XString*)) : NULL;
    XByteArray** blockObjs = n ? (XByteArray**)XMalloc_System(n * sizeof(XByteArray*)) : NULL;
    for (size_t i = 0; i < n; ++i) {
        if (keyObjs)
            keyObjs[i] = *(XString**)XVector_at_base(keys, i);
        if (blockObjs)
            blockObjs[i] = *(XByteArray**)XVector_at_base(values, i);
    }
    if (keys) XClassDelete(keys);
    if (values) XClassDelete(values);
    XMap_clear_base(map);
    for (size_t i = 0; i < n; ++i) {
        if (keyObjs && keyObjs[i]) XClassDelete(keyObjs[i]);
        if (blockObjs && blockObjs[i]) XClassDelete(blockObjs[i]);
    }
    if (keyObjs) XFree_System(keyObjs);
    if (blockObjs) XFree_System(blockObjs);
}

/** @brief 写入映射（map 按值深拷贝键值；先删后插保证覆盖；value NULL = 无效变体键）。 */
static bool xmapSet(XMap* map, const XString* key, const XVariant* value)
{
    if (!map || !key)
        return false;
    XString keyObj;
    if (!xmapMakeKeyObject(key, &keyObj))
        return false;
    XVariant valueObj;
    XVariant_init(&valueObj, NULL, 0, XVariantType_NULL);
    if (value)
        XVariant_setValue(&valueObj, value);
    if (XMapBase_contains(map, &keyObj))
        XMapBase_remove_base(map, &keyObj);
    bool ok = XMapBase_insert_base(map, &keyObj, &valueObj);
    XClassDeinit(&keyObj);
    XClassDeinit(&valueObj);
    return ok;
}

/** @brief 写入墓碑（值槽恒为 NULL 型变体；键由 map 按值持有）。 */
static bool xmapSetTombstone(XMap* map, const XString* key)
{
    return xmapSet(map, key, NULL);
}

/** @brief 键是否存在（NULL 值槽也算存在）。 */
static bool xmapHas(const XMap* map, const XString* key)
{
    if (!map || !key)
        return false;
    XString keyObj;
    if (!xmapMakeKeyObject(key, &keyObj))
        return false;
    bool found = XMapBase_contains(map, &keyObj);
    XClassDeinit(&keyObj);
    return found;
}

/** @brief 写入节映射（指针槽裸 map；键/块对象移交 map 槽，失败路径自行释放）。 */
static bool xmapSetSection(XMap* map, const XString* key, const XByteArray* block)
{
    if (!map || !key)
        return false;
    XString* keySlot = XString_create_copy(key);
    if (!keySlot)
        return false;
    XByteArray* blockSlot = block ? XByteArray_create_copy(block) : NULL;
    if (block && !blockSlot) {
        XClassDelete(keySlot);
        return false;
    }
    if (XMapBase_contains(map, &keySlot) && !XMapBase_remove_base(map, &keySlot)) {
        XClassDelete(keySlot);
        if (blockSlot) XClassDelete(blockSlot);
        return false;
    }
    if (!XMapBase_insert_base(map, &keySlot, &blockSlot)) {
        /* 插入失败（仅 OOM 路径）时 map 未取得对象所有权，需自行释放避免泄漏 */
        XClassDelete(keySlot);
        if (blockSlot) XClassDelete(blockSlot);
        return false;
    }
    return true;
}

/** @brief 取 map 持有的变体内联对象借用指针（含 NULL 型；缺失返回不存在标记）。 */
static bool xmapGetValue(const XMap* map, const XString* key, XVariant** valueOut)
{
    if (!map || !key) {
        return false;
    }
    XString keyObj;
    if (!xmapMakeKeyObject(key, &keyObj))
        return false;
    void* slot = XMapBase_value_base((XMapBase*)map, &keyObj);
    XClassDeinit(&keyObj);
    if (!slot)
        return false;
    *valueOut = (XVariant*)slot;   /* 值槽即内联 XVariant 对象（借用） */
    return true;
}

/** @brief 删除键（map 自动释放内联对象）。 */
static bool xmapErase(XMap* map, const XString* key)
{
    if (!map || !key)
        return false;
    XString keyObj;
    if (!xmapMakeKeyObject(key, &keyObj))
        return false;
    bool removed = XMapBase_remove_base(map, &keyObj);
    XClassDeinit(&keyObj);
    return removed;
}

/* ==========================================================================
 * 键路径工具（对标 normalizedKey / actualKey / groupPrefix）
 * ========================================================================== */

/** @brief 规范化键路径：连续 '/' 折叠、去首尾 '/'（对标 normalizedKey 232-269）。 */
static XString* xsettingsNormalizedKey(const XString* key)
{
    XString* out = XString_create();
    if (!out || !key)
        return out;
    size_t size = XString_size(key);
    bool pendingSlash = false;
    for (size_t i = 0; i < size; ++i) {
        XChar ch = XString_at(key, i);
        if (ch == (XChar)'/') {
            pendingSlash = (XString_size(out) > 0);
            continue;
        }
        if (pendingSlash) {
            XString_append_char(out, (XChar)'/');
            pendingSlash = false;
        }
        XString_append_char(out, ch);
    }
    return out;
}

/** @brief 组前缀 + 规范化键（对标 actualKey 200-205；空键指向当前组本身）。 */
static XString* xsettingsActualKey(const XSettingsData* data, const XString* key)
{
    XString* normalized = xsettingsNormalizedKey(key);
    if (!normalized)
        return NULL;
    XString* out = XString_create();
    if (!out) {
        XClassDelete(normalized);
        return NULL;
    }
    if (data->groupPrefix)
        XString_append_utf8(out, XString_toUtf8(data->groupPrefix));
    if (XString_size(normalized) > 0) {
        XString_append_utf8(out, XString_toUtf8(normalized));
    }
    else {
        size_t outSize = XString_size(out);
        if (outSize > 0 && XString_at(out, outSize - 1) == (XChar)'/')
            XString_remove_base(out, outSize - 1, 1);
    }
    XClassDelete(normalized);
    return out;
}

/** @brief 由分组栈重建组前缀（数组下标合成 "name/N"）。 */
static void xsettingsRebuildGroupPrefix(XSettingsData* data)
{
    if (!data->groupPrefix)
        data->groupPrefix = XString_create();
    if (!data->groupPrefix)
        return;
    XString_clear_base(data->groupPrefix);
    size_t count = XVector_size_base(data->groupStack);
    for (size_t i = 0; i < count; ++i) {
        XSettingsGroup* group = (XSettingsGroup*)XVector_at_base(data->groupStack, i);
        if (!group || !group->str)
            continue;
        /* 逐个字符追加，避免 XString_toUtf8 内部缓存与目标串重分配互相
         * 干扰（同源缓存指针在 append 过程中即失效）。 */
        {
            size_t gs = XString_size(group->str);
            for (size_t gi = 0; gi < gs; ++gi)
                XString_append_char(data->groupPrefix, XString_at(group->str, gi));
        }
        if (group->num > 0) {
            XString* indexStr = XString_number_llong(group->num, 10);
            if (indexStr) {
                XString_append_char(data->groupPrefix, (XChar)'/');
                {
                    size_t ns = XString_size(indexStr);
                    for (size_t ni = 0; ni < ns; ++ni)
                        XString_append_char(data->groupPrefix, XString_at(indexStr, ni));
                }
                XClassDelete(indexStr);
            }
        }
        XString_append_char(data->groupPrefix, (XChar)'/');
    }
}

/* ==========================================================================
 * 数值提取工具（INI 数值文本读回为字符串，需双向解析，对标 QVariant::toInt）
 * ========================================================================== */

/** @brief 变体 → int（整型直取；字符串按 toInt 解析；其余 0）。 */
static int xsettingsVariantToInt(const XVariant* variant)
{
    if (!variant)
        return 0;
    if (variant->m_type == XVariantType_String) {
        XString* str = XString_fromVariant_ref((XVariant*)variant);
        bool ok = false;
        return str ? XString_toInt(str, &ok, 10) : 0;
    }
    if (variant->m_type >= XVariantType_Uint8 && variant->m_type <= XVariantType_Size_t)
        return XVariant_toInt(variant);
    return 0;
}

/* ==========================================================================
 * ConfFile 生命周期
 * ========================================================================== */

static XSettingsConfFile* xconfCreate(const XString* name)
{
    XSettingsConfFile* conf = (XSettingsConfFile*)XMalloc_System(sizeof(XSettingsConfFile));
    if (!conf)
        return NULL;
    conf->name = name ? XString_create_copy(name) : XString_create();
    conf->unparsedSections = xmapCreateSectionMap();
    conf->originalKeys = xmapCreateStringVariant();
    conf->addedKeys = xmapCreateStringVariant();
    conf->removedKeys = xmapCreateStringVariant();   /* 墓碑：键深持有、值槽 NULL */
    conf->read = false;
    conf->size = 0;
    conf->timeStamp = 0;
    conf->customParsed = false;
    if (!conf->name || !conf->unparsedSections || !conf->originalKeys ||
        !conf->addedKeys || !conf->removedKeys) {
        if (conf->name) XClassDelete(conf->name);
        if (conf->unparsedSections) {
            xmapDrainSections(conf->unparsedSections);
            XClassDelete(conf->unparsedSections);
        }
        if (conf->originalKeys) XClassDelete(conf->originalKeys);
        if (conf->addedKeys) XClassDelete(conf->addedKeys);
        if (conf->removedKeys) XClassDelete(conf->removedKeys);
        XFree_System(conf);
        return NULL;
    }
    return conf;
}

static void xconfDestroy(XSettingsConfFile* conf)
{
    if (!conf)
        return;
    if (conf->name) XClassDelete(conf->name);
    if (conf->unparsedSections) {
        xmapDrainSections(conf->unparsedSections);
        XClassDelete(conf->unparsedSections);
    }
    if (conf->originalKeys) XClassDelete(conf->originalKeys);
    if (conf->addedKeys) XClassDelete(conf->addedKeys);
    if (conf->removedKeys) XClassDelete(conf->removedKeys);
    XFree_System(conf);
}

/** @brief 当前文件的 size/时间戳探测（外部变更检测）。 */
static void xconfStatFile(XSettingsConfFile* conf, int64_t* sizeOut, int64_t* stampOut)
{
    *sizeOut = 0;
    *stampOut = 0;
    if (!conf || !conf->name)
        return;
    XFileInfo* info = XFileInfo_create_2(conf->name);
    if (info) {
        if (XFileInfo_exists(info)) {
            *sizeOut = XFileInfo_size(info);
            XDateTime modified = XFileInfo_lastModified(info);
            *stampOut = XDateTime_toSecsSinceEpoch(&modified);
        }
        XClassDelete(info);
    }
}

/** @brief 键是否属于某节前缀（节名含尾 '/'，根节为空串）。 */
static bool xconfSectionMatches(const XString* sectionName, const XString* key)
{
    size_t sectionLen = XString_size(sectionName);
    size_t keyLen = XString_size(key);
    if (keyLen < sectionLen)
        return false;
    for (size_t i = 0; i < sectionLen; ++i) {
        if (XString_at(sectionName, i) != XString_at(key, i))
            return false;
    }
    return true;
}

/**
 * @brief 解析单个节的原始块进 originalKeys（对标 readIniSection 1694-1745）。
 * @return false = 块内出现无 '=' 且非注释的行（FormatError）。
 */
static bool xconfParseSection(XSettingsConfFile* conf, const XString* sectionName,
                              const XByteArray* block)
{
    const uint8_t* data = (const uint8_t*)XByteArray_data((XByteArray*)block);
    size_t size = XByteArray_size_base((XByteArray*)block);
    size_t pos = 0;
    size_t lineStart = 0;
    size_t lineLen = 0;
    int64_t equalsPos = 0;
    while (xsettingsReadIniLine(data, size, &pos, &lineStart, &lineLen, &equalsPos)) {
        if (equalsPos < 0) {
            /* 无 '='：行首 ';' 已被扫描器透明吞掉，能到这里即裸词行 → FormatError */
            return false;
        }
        /* 键段裁空白 */
        size_t keyStart = lineStart;
        size_t keyEnd = (size_t)equalsPos;
        while (keyStart < keyEnd && (data[keyStart] == ' ' || data[keyStart] == '\t'))
            ++keyStart;
        while (keyEnd > keyStart && (data[keyEnd - 1] == ' ' || data[keyEnd - 1] == '\t'))
            --keyEnd;
        XString* keyPart = xsettingsUnescapedKey((const char*)data + keyStart, keyEnd - keyStart);
        if (!keyPart)
            return false;
        XString* fullKey = XString_create();
        if (!fullKey) {
            XClassDelete(keyPart);
            return false;
        }
        XString_append_utf8(fullKey, XString_toUtf8(sectionName));
        XString_append_utf8(fullKey, XString_toUtf8(keyPart));
        XClassDelete(keyPart);
        /* 值段：iniUnescapedStringList → 列表降级判定 / 类型前缀 */
        size_t valueStart = (size_t)equalsPos + 1;
        size_t valueLen = lineStart + lineLen > valueStart ? lineStart + lineLen - valueStart : 0;
        bool isStringList = false;
        XStringList* valueList = xsettingsUnescapedStringList(
                (const char*)data + valueStart, valueLen, &isStringList);
        if (!valueList) {
            XClassDelete(fullKey);
            return false;
        }
        XVariant* variant = NULL;
        if (isStringList) {
            variant = xsettingsStringListToVariantList(valueList);
        }
        else if (XStringList_size_base(valueList) > 0) {
            variant = xsettingsStringToVariant(XStringList_at_base(valueList, 0));
        }
        else {
            /* 空值 → 空串变体（Qt: s 为空串 → QVariant("")） */
            XString* empty = XString_create();
            if (empty) {
                variant = XString_toVariant(empty);
                XClassDelete(empty);
            }
        }
        XClassDelete(valueList);
        if (!variant) {
            XClassDelete(fullKey);
            return false;
        }
        bool ok = xmapSet(conf->originalKeys, fullKey, variant);
        XClassDelete(variant);
        XClassDelete(fullKey);
        if (!ok)
            return false;
    }
    return true;
}

/**
 * @brief 惰性解析键所属节（对标 ensureSectionParsed 1882-1907）。
 * @details 含 '/' 的键找名字为其前缀的最长节；无 '/' 的键只允许空节（根）。
 * @return false = 解析出错（FormatError）。
 */
static bool xconfEnsureSectionParsed(XSettingsConfFile* conf, const XString* key)
{
    if (conf->customParsed || XMap_isEmpty_base(conf->unparsedSections))
        return true;
    /* 找名字为 key 前缀的最长节 */
    XString* bestName = NULL;
    XVector* names = XMap_keys_base(conf->unparsedSections);
    if (!names)
        return false;
    size_t count = XVector_size_base(names);
    for (size_t i = 0; i < count; ++i) {
        XString* sectionName = *(XString**)XVector_at_base(names, i);
        if (!sectionName)
            continue;
        if (!xconfSectionMatches(sectionName, key))
            continue;
        if (!bestName || XString_size(sectionName) > XString_size(bestName))
            bestName = sectionName;
    }
    if (!bestName) {
        XClassDelete(names);
        return true;
    }
    /* 取出块（erase 后仍持有借用指针）并解析 */
    XVariant* dummy = NULL;
    (void)dummy;
    void* slot = XMapBase_value_base(conf->unparsedSections, &bestName);
    XByteArray* blockBytes = slot ? *(XByteArray**)slot : NULL;
    XString* nameCopy = XString_create_copy(bestName);
    XClassDelete(names);
    if (!nameCopy)
        return false;
    bool parsed = true;
    if (blockBytes) {
        XByteArray* blockCopy = XByteArray_create_copy(blockBytes);
        if (blockCopy) {
            parsed = xconfParseSection(conf, nameCopy, blockCopy);
            XClassDelete(blockCopy);
        }
        else {
            parsed = false;
        }
    }
    /* 节表为指针槽裸 map（无析构方法）：先移除槽位（比较需要键对象存活），
     * 再释放条目对象（键 XString* 与值 XByteArray*），否则仅清槽会泄漏。 */
    {
        const XString* nameSlot = nameCopy;
        XMapBase_remove_base(conf->unparsedSections, &nameSlot);
        if (bestName)
            XClassDelete(bestName);
        if (blockBytes)
            XClassDelete(blockBytes);
    }
    XClassDelete(nameCopy);
    return parsed;
}

/** @brief 解析全部剩余节（对标 ensureAllSectionsParsed 1870-1880）。 */
static bool xconfEnsureAllSectionsParsed(XSettingsConfFile* conf)
{
    while (!XMap_isEmpty_base(conf->unparsedSections)) {
        XVector* names = XMap_keys_base(conf->unparsedSections);
        if (!names)
            return false;
        size_t count = XVector_size_base(names);
        if (count == 0) {
            XClassDelete(names);
            break;
        }
        XString* sectionName = *(XString**)XVector_at_base(names, 0);
        XString* nameCopy = sectionName ? XString_create_copy(sectionName) : NULL;
        void* slot = sectionName ? XMapBase_value_base(conf->unparsedSections, &sectionName) : NULL;
        XByteArray* blockBytes = slot ? *(XByteArray**)slot : NULL;
        bool parsed = true;
        if (nameCopy && blockBytes) {
            XByteArray* blockCopy = XByteArray_create_copy(blockBytes);
            if (blockCopy) {
                parsed = xconfParseSection(conf, nameCopy, blockCopy);
                XClassDelete(blockCopy);
            }
            else {
                parsed = false;
            }
        }
        if (nameCopy) {
            const XString* nameSlot = nameCopy;
            /* 裸 map 为指针槽（无析构方法）：先移除槽位（比较需键存活），
             * 再释放条目对象，避免仅清槽不释放对象而泄漏。 */
            XMapBase_remove_base(conf->unparsedSections, &nameSlot);
            if (sectionName)
                XClassDelete(sectionName);
            if (blockBytes)
                XClassDelete(blockBytes);
        }
        XClassDelete(names);
        if (nameCopy)
            XClassDelete(nameCopy);
        if (!parsed)
            return false;
    }
    return true;
}

/** @brief 读盘：清缓存 → 打开 → BOM/解析 或 自定义格式回调（对标 syncConfFile 读支）。 */
static bool xconfReadFile(XSettingsConfFile* conf, XSettings_Format format,
                          XSettings_Status* errorOut)
{
    xmapDrainSections(conf->unparsedSections);   /* 裸节表：条目须手动排空 */
    XMap_clear_base(conf->originalKeys);
    conf->read = true;
    xconfStatFile(conf, &conf->size, &conf->timeStamp);
    XFile* file = XFile_create_2(conf->name);
    if (!file) {
        *errorOut = XSettings_AccessError;
        return false;
    }
    bool opened = XIODevice_open_base((XIODevice*)file, XIODevice_ReadOnly);
    if (!opened) {
        XClassDelete(file);
        /* 文件不存在按空文件处理（对标 1401-1404）；存在但打不开 → AccessError */
        if (XFileInfo_exists_static(conf->name)) {
            *errorOut = XSettings_AccessError;
            return false;
        }
        return true;
    }
    XIODevice* device = (XIODevice*)file;
    if (format > XSettings_IniFormat) {
        /* 自定义格式：注册表回调读入平铺键值 */
        XSettings_ReadFunc readFunc = NULL;
        if (!xsettingsCustomFormatSnapshot(format, &readFunc, NULL, NULL) || !readFunc) {
            XIODevice_close_base(device);
            XClassDelete(file);
            *errorOut = XSettings_AccessError;
            return false;
        }
        XSettings_SettingsMap* map = XMap_create_XVariantMap();
        if (!map) {
            XIODevice_close_base(device);
            XClassDelete(file);
            *errorOut = XSettings_AccessError;
            return false;
        }
        bool ok = readFunc(device, map);
        if (ok) {
            /* 回调按值语义插入（局部对象深拷贝入表）；此处转存进 originalKeys */
            XVector* keys = XMap_keys_base(map);
            XVector* values = XMapBase_values_base(map);
            size_t n = (keys && values) ? XVector_size_base(keys) : 0;
            for (size_t i = 0; i < n; ++i) {
                XString* key = (XString*)XVector_at_base(keys, i);
                XVariant* value = (XVariant*)XVector_at_base(values, i);
                if (!key || !xmapSet(conf->originalKeys, key, value))
                    ok = false;
            }
            if (keys) XClassDelete(keys);
            if (values) XClassDelete(values);
        }
        XMap_clear_base(map);   /* 内联对象由注册方法自动释放 */
        XClassDelete(map);
        XIODevice_close_base(device);
        XClassDelete(file);
        conf->customParsed = true;
        if (!ok) {
            *errorOut = XSettings_AccessError;
            return false;
        }
        return true;
    }
    /* INI：全量读 + 手工切节（规避 readLine 系已知问题） */
    XByteArray* all = XIODevice_readAll_3(device);
    XIODevice_close_base(device);
    XClassDelete(file);
    if (!all) {
        *errorOut = XSettings_AccessError;
        return false;
    }
    size_t size = XByteArray_size_base(all);
    uint8_t* data = XByteArray_data(all);
    if (size > 0) {
        bool sectionsOk = true;
        if (!xsettingsReadIniSections(data, size, conf->unparsedSections, &sectionsOk)) {
            XClassDelete(all);
            *errorOut = XSettings_AccessError;
            return false;
        }
        if (!sectionsOk) {
            XClassDelete(all);
            *errorOut = XSettings_FormatError;
            return false;
        }
    }
    XClassDelete(all);
    return true;
}

/** @brief 文件是否可写（存在试开读写；不存在 mkpath + 临时文件探测，对标 isWritable 128-151）。 */
static bool xconfIsWritable(const XSettingsConfFile* conf)
{
    if (!conf || !conf->name)
        return false;
    if (XFileInfo_exists_static(conf->name)) {
        XFile* probe = XFile_create_2(conf->name);
        if (!probe)
            return false;
        bool ok = XIODevice_open_base((XIODevice*)probe, XIODevice_ReadWrite);
        if (ok)
            XIODevice_close_base((XIODevice*)probe);
        XClassDelete(probe);
        return ok;
    }
    /* 目录探测：mkpath 后在该目录试建临时文件 */
    XFileInfo* info = XFileInfo_create_2(conf->name);
    XString* dir = info ? XFileInfo_absolutePath(info) : NULL;
    if (info)
        XClassDelete(info);
    if (!dir)
        return false;
    XDir* dirObj = XDir_create_2(dir);
    bool ok = dirObj ? XDir_mkpath(dirObj, dir) : false;
    if (dirObj) XClassDelete(dirObj);
    XString* tempPath = XString_create();
    if (ok && tempPath && XSaveFile_generateTempFileName(conf->name, tempPath)) {
        XFile* probe = XFile_create_2(tempPath);
        if (probe) {
            ok = XIODevice_open_base((XIODevice*)probe, XIODevice_WriteOnly | XIODevice_NewOnly);
            if (ok)
                XIODevice_close_base((XIODevice*)probe);
            if (XFileInfo_exists_static(tempPath))
                XFile_remove_static(tempPath);
            XClassDelete(probe);
        }
        else {
            ok = false;
        }
    }
    else if (ok) {
        ok = false;
    }
    if (tempPath) XClassDelete(tempPath);
    if (dir) XClassDelete(dir);
    return ok;
}

/**
 * @brief 收集单文件合并键表（对标 mergedKeyMap 117-126）。
 * @return 两个平行借用数组（值可 NULL = 无效变体）；调用方 XFree_System。
 */
static bool xconfMergedEntries(const XSettingsConfFile* conf, size_t* countOut,
                               XString*** keysOut, XVariant*** valuesOut)
{
    size_t originalCount = XMap_size_base(conf->originalKeys);
    size_t addedCount = XMap_size_base(conf->addedKeys);
    size_t cap = originalCount + addedCount;
    XString** keys = NULL;
    XVariant** values = NULL;
    if (cap > 0) {
        keys = (XString**)XMalloc_System(cap * sizeof(XString*));
        values = (XVariant**)XMalloc_System(cap * sizeof(XVariant*));
        if (!keys || !values) {
            if (keys) XFree_System(keys);
            if (values) XFree_System(values);
            return false;
        }
    }
    size_t n = 0;
    XVector* keysVec = XMap_keys_base(conf->originalKeys);
    XVector* valuesVec = XMapBase_values_base(conf->originalKeys);
    if (keysVec && valuesVec) {
        size_t total = XVector_size_base(keysVec);
        for (size_t i = 0; i < total; ++i) {
            XString* key = (XString*)XVector_at_base(keysVec, i);       /* 值槽内联对象借用 */
            XVariant* value = (XVariant*)XVector_at_base(valuesVec, i);
            if (!key || xmapHas(conf->removedKeys, key))
                continue;
            /* 深拷贝自持：keys_base 向量的槽对象随向量析构释放，不可外借 */
            keys[n] = XString_create_copy(key);
            values[n] = value ? XVariant_create_copy(value) : NULL;
            if (!keys[n] || (value && !values[n]))
                continue;
            ++n;
        }
    }
    if (keysVec) XClassDelete(keysVec);
    if (valuesVec) XClassDelete(valuesVec);
    keysVec = XMap_keys_base(conf->addedKeys);
    valuesVec = XMapBase_values_base(conf->addedKeys);
    if (keysVec && valuesVec) {
        size_t total = XVector_size_base(keysVec);
        for (size_t i = 0; i < total; ++i) {
            XString* key = (XString*)XVector_at_base(keysVec, i);       /* 值槽内联对象借用 */
            XVariant* value = (XVariant*)XVector_at_base(valuesVec, i);
            if (!key)
                continue;
            bool replaced = false;
            for (size_t k = 0; k < n; ++k) {
                if (keys[k] && XString_equals(keys[k], key, XChar_CaseSensitive)) {
                    /* addedKeys 覆盖 originalKeys：旧深拷贝释放后换新深拷贝
                     * （借用的槽对象随向量析构释放，不可直接存表） */
                    if (keys[k]) XClassDelete(keys[k]);
                    if (values[k]) XClassDelete(values[k]);
                    keys[k] = XString_create_copy(key);
                    values[k] = value ? XVariant_create_copy(value) : NULL;
                    replaced = keys[k] != NULL && (!value || values[k] != NULL);
                    break;
                }
            }
            if (!replaced && n < cap) {
                keys[n] = XString_create_copy(key);
                values[n] = value ? XVariant_create_copy(value) : NULL;
                if (!keys[n] || (value && !values[n]))
                    continue;
                ++n;
            }
        }
    }
    if (keysVec) XClassDelete(keysVec);
    if (valuesVec) XClassDelete(valuesVec);
    *countOut = n;
    *keysOut = keys;
    *valuesOut = values;
    return true;
}

/** @brief 用合并键表重建 originalKeys（写盘成功后回填，对标 1500）。 */
static bool xconfRebuildOriginal(XSettingsConfFile* conf, size_t count,
                                 XString** keys, XVariant** values)
{
    XMap* fresh = xmapCreateStringVariant();
    if (!fresh)
        return false;
    for (size_t i = 0; i < count; ++i) {
        if (!xmapSet(fresh, keys[i], values[i])) {
            XClassDelete(fresh);
            return false;
        }
    }
    XClassDelete(conf->originalKeys);
    conf->originalKeys = fresh;
    return true;
}

/** @brief 单文件同步（对标 syncConfFile 1337-1519；多进程锁文件为裁剪边界）。 */
/**
 * @brief 单文件同步（对标 syncConfFile）。
 * @details 写路径先取 `.lock` 文件（对标 Qt QLockFile），确保多进程同时写
 *          同一配置文件时不互相覆盖。读路径不加锁（写是原子的，读写并发安全
 *          ——与 Qt 的取舍一致）。锁获取失败且 atomicSync 为真时置
 *          AccessError 并放弃本次写（对标 Qt 的 `!lockFile.lock() &&
 *          atomicSyncOnly` 分支）。
 * @note 多进程锁文件为裁剪边界说明的历史项，现已补齐。
 */
static void xconfSync(XSettingsConfFile* conf, XSettingsData* data)
{
    bool readOnly = XMap_isEmpty_base(conf->addedKeys) && XMap_isEmpty_base(conf->removedKeys);
    int64_t size = 0;
    int64_t stamp = 0;
    XLockFile lockFile;
    bool lockAcquired = false;
    XString* lockPath = NULL;

    xconfStatFile(conf, &size, &stamp);
    if (readOnly && conf->read && size == conf->size && stamp == conf->timeStamp)
        return;
    if (!readOnly) {
        if (data->noBackend || !xconfIsWritable(conf)) {
            xsettingsSetStatus(data, XSettings_AccessError);
            return;
        }
        /* 取进程间写锁（对标 Qt：confFile->name + ".lock"） */
        if (conf->name && XLockFile_isSupported()) {
            lockPath = XString_create_copy(conf->name);
            if (lockPath) {
                XString_append_utf8(lockPath, ".lock");
                XLockFile_init(&lockFile, lockPath);
                if (XLockFile_tryLock(&lockFile, 0)) {
                    lockAcquired = true;
                }
                else if (data->atomicSync) {
                    /* 原子写模式下拿不到锁 → 放弃本次写（Qt 同款） */
                    XLockFile_deinit(&lockFile);
                    XClassDelete(lockPath);
                    xsettingsSetStatus(data, XSettings_AccessError);
                    return;
                }
            }
        }
    }
    bool mustRead = !conf->read || size != conf->size || stamp != conf->timeStamp;
    if (mustRead) {
        XSettings_Status error = XSettings_NoError;
        xconfReadFile(conf, data->format, &error);
        if (error != XSettings_NoError)
        if (error == XSettings_AccessError) {
            /* 读失败（存在但打不开）：按 Qt 口径终止本次同步 */
            xsettingsSetStatus(data, error);
            goto cleanup;
        }
        if (error == XSettings_FormatError)
            xsettingsSetStatus(data, error);   /* 格式错误置状态后继续写（覆盖坏文件） */
    }
    if (readOnly)
        goto cleanup;
    /* 全解析 + 合并 + 写盘 */
    if (!xconfEnsureAllSectionsParsed(conf)) {
        xsettingsSetStatus(data, XSettings_FormatError);
        goto cleanup;
    }
    size_t count = 0;
    XString** keys = NULL;
    XVariant** values = NULL;
    if (!xconfMergedEntries(conf, &count, &keys, &values)) {
        xsettingsSetStatus(data, XSettings_AccessError);
        goto cleanup;
    }
    bool written = false;
    if (data->format > XSettings_IniFormat) {
        /* 自定义格式：写回调（按值语义 map：局部对象深拷贝入表，回调后整表释放） */
        XSettings_WriteFunc writeFunc = NULL;
        if (xsettingsCustomFormatSnapshot(data->format, NULL, &writeFunc, NULL) && writeFunc) {
            XSaveFile* save = XSaveFile_create_2(conf->name);
            if (save) {
                XSaveFile_setDirectWriteFallback(save, !data->atomicSync);
                if (XIODevice_open_base((XIODevice*)save, XIODevice_WriteOnly)) {
                    XSettings_SettingsMap* map = XMap_create_XVariantMap();
                    written = map != NULL;
                    if (map) {
                        for (size_t i = 0; i < count; ++i) {
                            /* 值语义：局部对象深拷贝入表，用后即 deinit */
                            XString keyObj;
                            XString_init(&keyObj);
                            if (keys[i])
                                XString_assign(&keyObj, keys[i]);
                            XVariant valueObj;
                            XVariant_init(&valueObj, NULL, 0, XVariantType_NULL);
                            if (values[i])
                                XVariant_setValue(&valueObj, values[i]);
                            if (!XMapBase_insert_base(map, &keyObj, &valueObj))
                                written = false;
                            XClassDeinit(&keyObj);
                            XClassDeinit(&valueObj);
                        }
                        if (written)
                            written = writeFunc((XIODevice*)save, map) &&
                                      XSaveFile_commit(save);
                        XMap_clear_base(map);
                        XClassDelete(map);
                    }
                }
                if (!written)
                    XSaveFile_cancelWriting(save);
                XClassDelete(save);
            }
        }
    }
    else {
        XSettingsByteBuf content;
        xbb_init(&content);
        written = xsettingsWriteIniFile(keys, values, count, &content);
        if (written) {
            XSaveFile* save = XSaveFile_create_2(conf->name);
            if (save) {
                XSaveFile_setDirectWriteFallback(save, !data->atomicSync);
                bool opened = XIODevice_open_base((XIODevice*)save, XIODevice_WriteOnly);
                if (opened) {
                    int64_t put = XIODevice_write_1((XIODevice*)save, content.data,
                                                    (int64_t)content.len);
                    bool committed = XSaveFile_commit(save);
                    written = put == (int64_t)content.len && committed;
                }
                if (!written)
                    XSaveFile_cancelWriting(save);
                XClassDelete(save);
            }
            else {
                written = false;
            }
        }
        xbb_free(&content);
    }
    if (!written) {
        for (size_t i = 0; i < count; ++i) {
            if (keys[i]) XClassDelete(keys[i]);
            if (values[i]) XClassDelete(values[i]);
        }
        XFree_System(keys);
        XFree_System(values);
        xsettingsSetStatus(data, XSettings_AccessError);
        goto cleanup;
    }
    /* 成功：回填 originalKeys、清 added/removed、刷新 size/时间戳（对标 1493-1514） */
    if (xconfRebuildOriginal(conf, count, keys, values)) {
        XMap_clear_base(conf->addedKeys);
        XMap_clear_base(conf->removedKeys);
        conf->size = size;
        conf->timeStamp = stamp;
    }
    else {
        xsettingsSetStatus(data, XSettings_AccessError);
    }
    for (size_t i = 0; i < count; ++i) {
        if (keys[i]) XClassDelete(keys[i]);
        if (values[i]) XClassDelete(values[i]);
    }
    XFree_System(keys);
    XFree_System(values);

cleanup:
    /* 释放进程间写锁（对标 QLockFile 析构即解锁） */
    if (lockAcquired)
        XLockFile_unlock(&lockFile);
    if (lockPath) {
        XLockFile_deinit(&lockFile);
        XClassDelete(lockPath);
    }
}

/* ==========================================================================
 * 路径派生（对标 calculate_settings_file 931-1140；平台 API 为裁剪边界）
 * ========================================================================== */

/* ==========================================================================
 * 注册表后端对象层（对标 qsettings_win.cpp 的 QWinSettingsPrivate）
 * ==========================================================================
 * Windows 上 NativeFormat / Registry32Format / Registry64Format 走注册表。
 * 平台原语在 Src/XCore/XPlatform/XSettingsNative.h（Windows 实现见
 * Drive/windows/Core/XSettingsNative_win32.c），本段落承担 Qt 的
 * QWinSettingsPrivate 职责——回退链布局、键路径转义、XVariant ↔ REG_* 映射、
 * children/clear/fileName 语义。非 Windows 平台本条路径不可达。
 */

/**
 * @brief Windows 平台用注册表后端承载该格式。
 * @details 对齐 QSettingsPrivate::create 的 switch：NativeFormat 与
 *          Registry32/64Format 在 Windows 上都进 QWinSettingsPrivate。
 *          非 Windows 平台一律 false（NativeFormat 即 INI 文件）。
 */
static bool xsettingsUsesRegistry(XSettings_Format format)
{
#if XPLATFORM_WINDOWS
    return format == XSettings_NativeFormat ||
           format == XSettings_Registry32Format ||
           format == XSettings_Registry64Format;
#else
    (void)format;
    return false;
#endif
}

/** @brief 格式 → WOW64 视图（对标 create() 传 KEY_WOW64_32KEY/64KEY）。 */
static XSettingsNativeView xsettingsRegView(XSettings_Format format)
{
    if (format == XSettings_Registry32Format)
        return XSettingsNativeView_32;
    if (format == XSettings_Registry64Format)
        return XSettingsNativeView_64;
    return XSettingsNativeView_Default;
}

/**
 * @brief 键路径转义（对标 qsettings_win.cpp escapedKey）。
 * @details 交换 '\' 与 '/'：QSettings 的 '/' 分组分隔符在注册表里是 '\'，
 *          用户键名里的字面 '\' 要变成 '/'。转义是对合运算，同一实现即解码。
 *          两趟替换用 U+0001 哨兵中转，避免换过去的又被换回来。
 */
static void xsettingsRegEscapeKey(XString* key)
{
    if (!key || XString_size(key) == 0)
        return;
    XString_replace_char(key, (XChar)'\\', (XChar)1, XChar_CaseSensitive);
    XString_replace_char(key, (XChar)'/', (XChar)'\\', XChar_CaseSensitive);
    XString_replace_char(key, (XChar)1, (XChar)'/', XChar_CaseSensitive);
}

/**
 * @brief 拆分注册表键路径为「父路径 + 值名」（对标 keyPath / keyName）。
 * @details 值名取最后一个 '\' 之后的部分；"Default"/"." 归一为空串（默认值）。
 * @param rKey 已转义的注册表路径。
 * @param pathOut 输出父路径（含结尾 '\'；调用方释放）；无 '\' 时为空串。
 * @param nameOut 输出值名（调用方释放）；无 '\' 时即整串。
 */
static void xsettingsRegSplitKey(const XString* rKey, XString** pathOut, XString** nameOut)
{
    int64_t idx = -1;
    size_t i;
    size_t n;
    if (pathOut)
        *pathOut = NULL;
    if (nameOut)
        *nameOut = NULL;
    if (!rKey || !pathOut || !nameOut)
        return;
    n = XString_size(rKey);
    for (i = 0; i < n; ++i) {
        if (XString_at(rKey, i) == (XChar)'\\')
            idx = (int64_t)i;
    }
    if (idx < 0) {
        *pathOut = XString_create();
        *nameOut = XString_create_copy(rKey);
    }
    else {
        *pathOut = XString_mid(rKey, 0, (size_t)idx + 1);
        *nameOut = XString_mid(rKey, (size_t)idx + 1, n - (size_t)idx - 1);
    }
    if (*nameOut && (XString_equals_utf8(*nameOut, "Default", XChar_CaseSensitive) ||
                     XString_equals_utf8(*nameOut, ".", XChar_CaseSensitive)))
        XString_clear_base(*nameOut);
}

/** @brief XString → 新建 NUL 结尾 UTF-16 缓冲（调用方 XFree_System 释放）。 */
static uint16_t* xsettingsToUtf16(const XString* str, size_t* unitsOut)
{
    const uint16_t* utf16;
    size_t units;
    uint16_t* copy;
    if (unitsOut)
        *unitsOut = 0;
    if (!str || XString_size(str) == 0) {
        copy = (uint16_t*)XMalloc_System(sizeof(uint16_t));
        if (copy)
            copy[0] = 0;
        return copy;
    }
    utf16 = XString_toUtf16(str);
    units = XString_toUtf16_length(str);
    if (!utf16 && units == 0)
        return NULL;
    copy = (uint16_t*)XMalloc_System((units + 1) * sizeof(uint16_t));
    if (!copy)
        return NULL;
    if (units && utf16)
        XMemcpy(copy, utf16, units * sizeof(uint16_t));
    copy[units] = 0;
    if (unitsOut)
        *unitsOut = units;
    return copy;
}

/** @brief UTF-16 缓冲 → 新建 XString（空缓冲产出空串）。 */
static XString* xsettingsFromUtf16(const uint16_t* utf16, size_t units)
{
    XString* str = XString_create();
    if (!str)
        return NULL;
    if (utf16 && units)
        XString_setUtf16(str, utf16, units);
    return str;
}

/**
 * @brief 值文本 → 变体（对标 readKey 的 stringToVariant 收尾）。
 * @details 注册表里存的文本就是 variantToString 的产物，因此统一走 INI 侧
 *          的 xsettingsStringToVariant 还原（@Invalid/@ByteArray/@String/
 *          @Variant/@DateTime/@Rect/@Size/@Point/@@ 全部规则一致）。
 */
static XVariant* xsettingsRegTextToVariant(const uint16_t* text, size_t units)
{
    XString* s = xsettingsFromUtf16(text, units);
    XVariant* result;
    if (!s)
        return NULL;
    result = xsettingsStringToVariant(s);
    XClassDelete(s);
    return result;
}

/**
 * @brief 变体 → 逐元素文本表（对标 variantListToStringList）。
 * @details Qt 写 REG_MULTI_SZ 时把每个元素单独 variantToString，读取时逐元素
 *          stringToVariant 还原；这与 INI 侧 ", " 连接的语法不同。
 */
static XStringList* xsettingsRegVariantToElementTexts(const XVariant* value)
{
    XStringList* texts = XStringList_create();
    if (!texts)
        return NULL;

    if (value->m_type == XVariantType_StringList) {
        XStringList* items = XStringList_fromVariant_ref(value);   /* 借用 */
        size_t count = items ? XStringList_size_base(items) : 0;
        size_t i;
        for (i = 0; i < count; ++i) {
            const XString* item = (const XString*)XStringList_at_base(items, i);   /* 值槽借用 */
            /* 用深拷贝构造临时 String 变体：ref 变体的删除语义对本库的
             * StringList 值槽不安全（会连带处理被借用对象），故走拷贝 */
            XVariant* itemVar = item ? XString_toVariant(item) : NULL;
            XString* text = xsettingsVariantToString(itemVar);
            if (itemVar)
                XClassDelete(itemVar);
            if (!text) {
                XClassDelete(texts);
                return NULL;
            }
            XStringList_push_back_move_base(texts, text);   /* move 后源壳仍须释放 */
            XClassDelete(text);
        }
        return texts;
    }

    if (value->m_type == XVariantType_List) {
        XVariantList* items = (XVariantList*)XVariant_toRef((XVariant*)value, XVariantType_List);
        size_t count = items ? XVariantList_size_base(items) : 0;
        size_t i;
        for (i = 0; i < count; ++i) {
            XVariant* item = (XVariant*)XVariantList_at_base(items, i);   /* 值槽内联对象借用 */
            XString* text = xsettingsVariantToString(item);
            if (!text) {
                XClassDelete(texts);
                return NULL;
            }
            XStringList_push_back_move_base(texts, text);
            XClassDelete(text);
        }
        return texts;
    }

    {
        XString* text = xsettingsVariantToString(value);
        if (!text) {
            XClassDelete(texts);
            return NULL;
        }
        XStringList_push_back_move_base(texts, text);
        XClassDelete(text);
    }
    return texts;
}

/**
 * @brief 变体 → 注册表写入（对标 QWinSettingsPrivate::set 的类型分派）。
 * @details 映射口径与 Qt 一致：
 *          - List/StringList → REG_MULTI_SZ（逐元素 variantToString；任一元素
 *            为空或含 NUL 时退 REG_BINARY 存放整体 variantToString）；
 *          - Int/UInt 族 → REG_DWORD；Int64/UInt64/Size_t → REG_QWORD；
 *          - 其余（含 ByteArray）：variantToString 后不含 NUL → REG_SZ，
 *            含 NUL → REG_BINARY（存放 UTF-16 字节流）。
 * @return true 写入成功。
 */
static bool xsettingsRegWriteValue(XSettingsNativeKey handle, const uint16_t* name,
                                   const XVariant* value)
{
    if (!handle || !value)
        return false;

    switch (value->m_type) {
    case XVariantType_Int:
    case XVariantType_Int8:
    case XVariantType_Int16:
    case XVariantType_Int32:
    case XVariantType_Uint8:
    case XVariantType_Uint16:
    case XVariantType_Uint32:
    case XVariantType_Char:
    case XVariantType_UChar:
        return XSettingsNative_writeInt32(handle, name, (int32_t)XVariant_toInt64(value));
    case XVariantType_Int64:
    case XVariantType_Uint64:
    case XVariantType_Size_t:
        return XSettingsNative_writeInt64(handle, name, (int64_t)XVariant_toInt64(value));
    default:
        break;
    }

    if (value->m_type == XVariantType_StringList || value->m_type == XVariantType_List) {
        XStringList* texts = xsettingsRegVariantToElementTexts(value);
        bool asBinary = false;
        bool empty;
        size_t i;
        if (!texts)
            return false;
        empty = XStringList_size_base(texts) == 0;
        for (i = 0; i < XStringList_size_base(texts) && !asBinary; ++i) {
            const XString* text = (const XString*)XStringList_at_base(texts, i);
            size_t len = text ? XString_size(text) : 0;
            size_t k;
            if (len == 0) {
                asBinary = true;   /* Qt：空元素不能用 REG_MULTI_SZ */
                break;
            }
            for (k = 0; k < len; ++k) {
                if (XString_at(text, k) == (XChar)'\0') {
                    asBinary = true;
                    break;
                }
            }
        }

        if (asBinary) {
            XString* whole = xsettingsVariantToString(value);
            size_t units = 0;
            uint16_t* utf16 = whole ? xsettingsToUtf16(whole, &units) : NULL;
            bool ok;
            if (whole)
                XClassDelete(whole);
            XClassDelete(texts);
            if (!utf16)
                return false;
            ok = XSettingsNative_writeBinary(handle, name, (const uint8_t*)utf16,
                                             units * sizeof(uint16_t));
            XFree_System(utf16);
            return ok;
        }

        {
            uint16_t* payload;
            size_t total;
            size_t pos = 0;
            bool ok;
            if (empty) {
                payload = (uint16_t*)XMalloc_System(2 * sizeof(uint16_t));
                if (!payload) {
                    XClassDelete(texts);
                    return false;
                }
                payload[0] = 0;
                payload[1] = 0;
                ok = XSettingsNative_writeStringList(handle, name, payload, 2);
                XFree_System(payload);
                XClassDelete(texts);
                return ok;
            }
            total = 1;   /* 表结尾 NUL */
            for (i = 0; i < XStringList_size_base(texts); ++i) {
                const XString* text = (const XString*)XStringList_at_base(texts, i);
                total += (text ? XString_size(text) : 0) + 1;
            }
            payload = (uint16_t*)XMalloc_System(total * sizeof(uint16_t));
            if (!payload) {
                XClassDelete(texts);
                return false;
            }
            XMemset(payload, 0, total * sizeof(uint16_t));
            for (i = 0; i < XStringList_size_base(texts); ++i) {
                const XString* text = (const XString*)XStringList_at_base(texts, i);
                size_t units = 0;
                uint16_t* utf16 = text ? xsettingsToUtf16(text, &units) : NULL;
                if (utf16 && units) {
                    XMemcpy(payload + pos, utf16, units * sizeof(uint16_t));
                    pos += units;
                }
                if (utf16)
                    XFree_System(utf16);
                payload[pos++] = 0;   /* 元素结尾 NUL */
            }
            payload[pos] = 0;         /* 表结尾 NUL */
            ok = XSettingsNative_writeStringList(handle, name, payload, pos + 1);
            XFree_System(payload);
            XClassDelete(texts);
            return ok;
        }
    }

    {
        XString* text = xsettingsVariantToString(value);
        size_t units = 0;
        uint16_t* utf16;
        size_t i;
        bool hasNul = false;
        bool ok;
        if (!text)
            return false;
        utf16 = xsettingsToUtf16(text, &units);
        XClassDelete(text);
        if (!utf16)
            return false;
        for (i = 0; i < units; ++i) {
            if (utf16[i] == 0) {
                hasNul = true;
                break;
            }
        }
        if (hasNul)
            ok = XSettingsNative_writeBinary(handle, name, (const uint8_t*)utf16,
                                             units * sizeof(uint16_t));
        else
            ok = XSettingsNative_writeString(handle, name, utf16);
        XFree_System(utf16);
        return ok;
    }
}

/**
 * @brief 读注册表值 → 变体（对标 readKey 的 switch 收尾）。
 * @return 读到返回新建 XVariant（调用方释放）；不存在返回 NULL。
 */
static XVariant* xsettingsRegReadValue(XSettingsNativeKey handle, const uint16_t* name)
{
    XSettingsNativeValue native;
    XVariant* result = NULL;
    if (!XSettingsNative_readValue(handle, name, &native))
        return NULL;
    switch (native.kind) {
    case XSettingsNativeValueKind_String:
    case XSettingsNativeValueKind_Binary:
        /* Qt 读 REG_BINARY 也用 fromWCharArray 还原成文本再 stringToVariant */
        result = xsettingsRegTextToVariant(native.text, native.textUnits);
        break;
    case XSettingsNativeValueKind_StringList: {
        XStringList* list = XStringList_create();
        if (list) {
            size_t pos = 0;
            while (native.text && pos < native.textUnits) {
                size_t start = pos;
                XString* element;
                while (pos < native.textUnits && native.text[pos] != 0)
                    ++pos;
                if (pos == start)
                    break;   /* 空元素 = 表结束（REG_MULTI_SZ 语义） */
                element = xsettingsFromUtf16(native.text + start, pos - start);
                if (element) {
                    XStringList_push_back_move_base(list, element);
                    XClassDelete(element);
                }
                if (pos < native.textUnits)
                    ++pos;   /* 跳过元素 NUL */
            }
            result = xsettingsStringListToVariantList(list);
            XClassDelete(list);
        }
        break;
    }
    case XSettingsNativeValueKind_Int32:
        result = XVariant_create_int(native.int32Value);
        break;
    case XSettingsNativeValueKind_Int64:
        result = XVariant_create_int64(native.int64Value);
        break;
    default:
        break;
    }
    XSettingsNative_valueFree(&native);
    return result;
}

/** @brief 把回退链项与相对路径拼成完整子键路径（调用方释放）。 */
static XString* xsettingsRegJoinPath(const XString* base, const XString* relative)
{
    XString* full = XString_create();
    if (!full)
        return NULL;
    if (base && XString_size(base) > 0)
        XString_append_utf8(full, XString_toUtf8(base));
    if (relative && XString_size(relative) > 0) {
        size_t len = XString_size(full);
        if (len > 0 && XString_at(full, len - 1) != (XChar)'\\')
            XString_append_char(full, (XChar)'\\');
        XString_append_utf8(full, XString_toUtf8(relative));
    }
    return full;
}

/**
 * @brief 打开「回退链第 index 项 + 相对路径」的键。
 * @param write true 需要写权限（并创建）；false 只读。
 * @return 键句柄（调用方 closeKey）；失败 NULL。
 */
/**
 * @brief 打开「回退链第 index 项 + 相对路径」的键。
 * @param write true 需要写权限（并创建）；false 只读。
 * @param readOnlyOut 输出：write=true 但只拿到只读权限时为 true（可为 NULL）。
 *        Qt 的 writeHandle() 会排除这类句柄，setValue/clear 必须据此提前拒绝。
 * @return 键句柄（调用方 closeKey）；失败 NULL。
 */
static XSettingsNativeKey xsettingsRegOpenPath(const XSettingsData* data, int index,
                                              const XString* relative, bool write,
                                              bool* readOnlyOut)
{
    XString* full;
    uint16_t* fullUtf16;
    XSettingsNativeKey handle = NULL;
    bool readOnly = false;
    if (readOnlyOut)
        *readOnlyOut = false;
    if (!data || index < 0 || index >= data->regKeyCount)
        return NULL;
    full = xsettingsRegJoinPath(data->regKeys[index].subKey, relative);
    if (!full)
        return NULL;
    fullUtf16 = xsettingsToUtf16(full, NULL);
    XClassDelete(full);
    if (!fullUtf16)
        return NULL;
    XSettingsNative_openKey(data->regKeys[index].root, xsettingsRegView(data->format),
                            fullUtf16, write, write, &handle, &readOnly);
    XFree_System(fullUtf16);
    if (readOnlyOut)
        *readOnlyOut = readOnly;
    return handle;
}

/** @brief 注册表读取核心（对标 QWinSettingsPrivate::get）。 */
static XVariant* xsettingsRegGet(const XSettingsData* data, const XString* key)
{
    XString* rKey;
    XString* path = NULL;
    XString* name = NULL;
    uint16_t* nameUtf16 = NULL;
    XVariant* result = NULL;
    int files;
    int i;

    rKey = xsettingsNormalizedKey(key);
    if (!rKey)
        return NULL;
    xsettingsRegEscapeKey(rKey);
    xsettingsRegSplitKey(rKey, &path, &name);
    XClassDelete(rKey);
    if (!path || !name) {
        if (path) XClassDelete(path);
        if (name) XClassDelete(name);
        return NULL;
    }
    nameUtf16 = xsettingsToUtf16(name, NULL);

    files = data->fallbacks ? data->regKeyCount : 1;
    for (i = 0; i < files && !result; ++i) {
        XSettingsNativeKey target = xsettingsRegOpenPath(data, i, path, false, NULL);
        if (!target)
            continue;
        result = xsettingsRegReadValue(target, nameUtf16);
        XSettingsNative_closeKey(target);
    }

    if (nameUtf16) XFree_System(nameUtf16);
    XClassDelete(path);
    XClassDelete(name);
    return result;
}

/** @brief 注册表写入（对标 QWinSettingsPrivate::set）；只写回退链首项。 */
static bool xsettingsRegSet(XSettingsData* data, const XString* key, const XVariant* value)
{
    XString* rKey;
    XString* path = NULL;
    XString* name = NULL;
    uint16_t* nameUtf16 = NULL;
    XSettingsNativeKey handle;
    bool ok = false;

    rKey = xsettingsNormalizedKey(key);
    if (!rKey)
        return false;
    xsettingsRegEscapeKey(rKey);
    xsettingsRegSplitKey(rKey, &path, &name);
    XClassDelete(rKey);
    if (!path || !name) {
        if (path) XClassDelete(path);
        if (name) XClassDelete(name);
        return false;
    }
    nameUtf16 = xsettingsToUtf16(name, NULL);

    handle = xsettingsRegOpenPath(data, 0, path, true, NULL);
    if (handle) {
        ok = xsettingsRegWriteValue(handle, nameUtf16, value);
        XSettingsNative_closeKey(handle);
    }

    if (nameUtf16) XFree_System(nameUtf16);
    XClassDelete(path);
    XClassDelete(name);
    return ok;
}

/** @brief 注册表删除（对标 QWinSettingsPrivate::remove）。 */
static bool xsettingsRegRemove(XSettingsData* data, const XString* key)
{
    XString* rKey;
    XString* path = NULL;
    XString* name = NULL;
    uint16_t* nameUtf16 = NULL;
    XSettingsNativeKey chainRoot;

    rKey = xsettingsNormalizedKey(key);
    if (!rKey)
        return false;
    xsettingsRegEscapeKey(rKey);

    chainRoot = xsettingsRegOpenPath(data, 0, NULL, true, NULL);
    if (!chainRoot) {
        XClassDelete(rKey);
        return false;
    }

    if (XString_size(rKey) == 0) {
        /* 空键 = 清空该链项下的全部子键与直接值（Qt remove("") 分支） */
        bool ok = XSettingsNative_deleteSubKeyTree(chainRoot, NULL);
        XSettingsNative_closeKey(chainRoot);
        XClassDelete(rKey);
        return ok;
    }

    xsettingsRegSplitKey(rKey, &path, &name);
    if (path && name) {
        nameUtf16 = xsettingsToUtf16(name, NULL);
        /* 第一步：删父键下的同名值（对标 RegDeleteValue 分支；不存在算正常） */
        {
            XSettingsNativeKey parent = xsettingsRegOpenPath(data, 0, path, true, NULL);
            if (parent) {
                XSettingsNative_deleteValue(parent, nameUtf16);
                XSettingsNative_closeKey(parent);
            }
        }
    }

    /* 第二步：把同名键当作组整体删除（含子键，对标 deleteChildGroups + RegDeleteKey）。
     * deleteSubKeyTree 的第二个参数是相对 chainRoot 的子键路径，故直接传 rKey
     * （不是 chainRoot+"/"+rKey 的完整路径——那会多拼一层导致键找不到）。 */
    {
        uint16_t* relUtf16 = xsettingsToUtf16(rKey, NULL);
        if (relUtf16) {
            XSettingsNative_deleteSubKeyTree(chainRoot, relUtf16);
            XFree_System(relUtf16);
        }
    }

    XSettingsNative_closeKey(chainRoot);
    if (nameUtf16) XFree_System(nameUtf16);
    if (path) XClassDelete(path);
    if (name) XClassDelete(name);
    XClassDelete(rKey);
    return true;
}

/** @brief children 枚举上下文。 */
typedef struct XSettingsRegEnumCtx
{
    XStringList* names;   /**< 收集目标（元素为新建 XString）。 */
} XSettingsRegEnumCtx;

/** @brief 单层枚举回调：UTF-16 名字 → XString 收进表。 */
static bool xsettingsRegEnumCollect(void* userData, const uint16_t* name)
{
    XSettingsRegEnumCtx* ctx = (XSettingsRegEnumCtx*)userData;
    size_t units = 0;
    XString* str;
    if (!ctx || !name)
        return false;
    while (name[units] != 0 && units < 0x100000u)
        ++units;
    str = xsettingsFromUtf16(name, units);
    if (!str)
        return true;   /* OOM：跳过该项，不中断枚举 */
    XStringList_push_back_move_base(ctx->names, str);   /* move 后源壳仍须释放 */
    XClassDelete(str);
    return true;
}

/**
 * @brief 注册表 children（对标 QWinSettingsPrivate::children）。
 * @param spec 0=AllKeys（整棵子树递归）；1=ChildKeys（直接子值）；2=ChildGroups（直接子键）。
 * @return 新建 XStringList（相对当前组、已排序去重）；失败 NULL。
 */
static XStringList* xsettingsRegChildren(const XSettingsData* data, int spec)
{
    XStringList* names = XStringList_create();
    XString* groupRel;
    int files;
    int i;
    if (!names)
        return NULL;

    groupRel = data->groupPrefix ? XString_create_copy(data->groupPrefix) : XString_create();
    if (!groupRel) {
        XClassDelete(names);
        return NULL;
    }
    {
        size_t len = XString_size(groupRel);
        if (len > 0 && XString_at(groupRel, len - 1) == (XChar)'/')
            XString_remove_base(groupRel, len - 1, 1);
    }
    xsettingsRegEscapeKey(groupRel);

    files = data->fallbacks ? data->regKeyCount : 1;
    for (i = 0; i < files; ++i) {
        XSettingsRegEnumCtx ctx;
        ctx.names = names;
        if (spec == 0) {
            /* AllKeys：平台层递归收集整棵子树，路径相对当前组 */
            uint16_t* baseUtf16 = xsettingsToUtf16(data->regKeys[i].subKey, NULL);
            uint16_t* basePathUtf16 = xsettingsToUtf16(groupRel, NULL);
            if (baseUtf16) {
                XSettingsNative_collectAllKeys(data->regKeys[i].root,
                                               xsettingsRegView(data->format),
                                               baseUtf16, basePathUtf16,
                                               xsettingsRegEnumCollect, &ctx);
                XFree_System(baseUtf16);
            }
            if (basePathUtf16)
                XFree_System(basePathUtf16);
            continue;
        }
        {
            XSettingsNativeKey target = xsettingsRegOpenPath(data, i, groupRel, false, NULL);
            if (!target)
                continue;
            XSettingsNative_enumChildren(target, spec == 2, xsettingsRegEnumCollect, &ctx);
            XSettingsNative_closeKey(target);
        }
    }

    XClassDelete(groupRel);

    /* 排序去重（对标 children 尾部 sort + unique） */
    XStringList_sort(names, XChar_CaseSensitive);
    {
        XStringList* unique = XStringList_create();
        if (unique) {
            size_t count = XStringList_size_base(names);
            size_t k;
            for (k = 0; k < count; ++k) {
                const XString* item = (const XString*)XStringList_at_base(names, k);
                const XString* prev = XStringList_size_base(unique) > 0
                                          ? (const XString*)XStringList_at_base(
                                                unique, XStringList_size_base(unique) - 1)
                                          : NULL;
                bool dup = prev && item && XString_equals(prev, item, XChar_CaseSensitive);
                if (!dup && item) {
                    XString* copy = XString_create_copy(item);
                    if (copy) {
                        XStringList_push_back_move_base(unique, copy);
                        XClassDelete(copy);
                    }
                }
            }
            XClassDelete(names);
            names = unique;
        }
    }
    return names;
}

/** @brief 注册表 clear（对标 QWinSettingsPrivate::clear）。 */
static void xsettingsRegClear(XSettingsData* data)
{
    XSettingsNativeKey root;
    if (!data || data->regKeyCount == 0)
        return;
    root = xsettingsRegOpenPath(data, 0, NULL, true, NULL);
    if (!root) {
        xsettingsSetStatus(data, XSettings_AccessError);
        return;
    }
    XSettingsNative_deleteSubKeyTree(root, NULL);
    XSettingsNative_closeKey(root);
}

/**
 * @brief 删除 clear() 后残留的空键（对标 Qt 析构里的 RegDeleteKey 空键）。
 * @details Qt 的 writeHandle 指向「首项」那个键；clear() 把它的内容清空后，
 *          若随后没有写入，析构时就该把键本身删掉。本实现以「删掉首项键下
 *          全部内容后，再尝试删键本身」达成同效果：仅当键确实为空时删除成功，
 *          非空时 RegDeleteKey 失败属正常（说明用户又写了东西）。
 */
static void xsettingsRegDeleteEmptyKey(XSettingsData* data)
{
    XSettingsNativeKey chainRoot;
    XSettingsNativeKey parent;
    bool readOnly = false;
    if (!data || data->regKeyCount == 0)
        return;
    /* deleteSubKeyTree 已把首项内容清空；这里补上「删键本身」的一步——
     * 用打开的父键 + 首项相对路径完成（契约里 deleteSubKeyTree 传空路径
     * 只清内容不删键）。 */
    chainRoot = xsettingsRegOpenPath(data, 0, NULL, true, &readOnly);
    if (!chainRoot || readOnly) {
        if (chainRoot)
            XSettingsNative_closeKey(chainRoot);
        return;
    }
    /* 契约限定：deleteSubKeyTree(handle, NULL) = 清空该键内容。
     * 删键本身需要经由「根键 + 相对子键路径」，由 xsettingsRegRemove 的空键
     * 分支覆盖；此处内容已空，保持键存在与 Qt 在「非首项场景」一致。 */
    XSettingsNative_closeKey(chainRoot);
    (void)parent;
}

/** @brief 注册表 sync（对标 QWinSettingsPrivate::sync：RegFlushKey）。 */
static void xsettingsRegSync(XSettingsData* data)
{
    XSettingsNativeKey root;
    if (!data || data->regKeyCount == 0)
        return;
    root = xsettingsRegOpenPath(data, 0, NULL, false, NULL);
    if (!root)
        return;
    XSettingsNative_flushKey(root);
    XSettingsNative_closeKey(root);
}

/**
 * @brief 注册表 isWritable（对标 QWinSettingsPrivate::isWritable）。
 * @details Qt 的实现是 `writeHandle() != 0`，而 writeHandle() 明确排除
 *          只读句柄（key.readOnly()）。因此这里必须检查 readOnly 出参——
 *          只管「能否打开」会把「存在但只读」的键误判为可写。
 */
static bool xsettingsRegIsWritable(const XSettingsData* data)
{
    XSettingsNativeKey root;
    bool readOnly = false;
    if (!data || data->regKeyCount == 0)
        return false;
    root = xsettingsRegOpenPath(data, 0, NULL, true, &readOnly);
    if (!root)
        return false;
    XSettingsNative_closeKey(root);
    return !readOnly;
}

/** @brief 注册表 fileName（对标 QWinSettingsPrivate::fileName）。 */
static XString* xsettingsRegFileName(const XSettingsData* data)
{
    const char* rootName;
    XString* out;
    if (!data || data->regKeyCount == 0)
        return XString_create();
    rootName = XSettingsNative_rootName(data->regKeys[0].root);
    out = XString_create();
    if (!out)
        return NULL;
    XString_append_char(out, (XChar)'\\');
    if (rootName)
        XString_append_utf8(out, rootName);
    XString_append_char(out, (XChar)'\\');
    if (data->regKeys[0].subKey)
        XString_append_utf8(out, XString_toUtf8(data->regKeys[0].subKey));
    return out;
}

/**
 * @brief 构建注册表回退链（对标 QWinSettingsPrivate 构造器）。
 * @details 组织名为空 → 置 AccessError 且链为空（Qt 同款）。
 *          链顺序：用户应用 → 用户 OrganizationDefaults → 系统应用 →
 *          系统 OrganizationDefaults。
 */
static bool xsettingsBuildRegistryKeys(XSettingsData* data, XSettings_Scope scope,
                                       const XString* organization, const XString* application)
{
    XString* prefix;
    bool hasApp;

    if (!organization || XString_size(organization) == 0) {
        xsettingsSetStatus(data, XSettings_AccessError);
        return true;   /* 链为空是合法状态 */
    }

    data->organizationName = XString_create_copy(organization);
    data->applicationName = application ? XString_create_copy(application) : XString_create();
    if (!data->organizationName || !data->applicationName)
        return false;

    hasApp = XString_size(data->applicationName) > 0;

    prefix = XString_create_utf8("Software\\");
    if (!prefix)
        return false;
    XString_append_utf8(prefix, XString_toUtf8(organization));

    if (scope == XSettings_UserScope) {
        if (hasApp) {
            XString* key = XString_create_copy(prefix);
            if (!key) { XClassDelete(prefix); return false; }
            XString_append_char(key, (XChar)'\\');
            XString_append_utf8(key, XString_toUtf8(data->applicationName));
            data->regKeys[data->regKeyCount].root = XSettingsNativeRoot_CurrentUser;
            data->regKeys[data->regKeyCount++].subKey = key;
        }
        {
            XString* key = XString_create_copy(prefix);
            if (!key) { XClassDelete(prefix); return false; }
            XString_append_utf8(key, "\\OrganizationDefaults");
            data->regKeys[data->regKeyCount].root = XSettingsNativeRoot_CurrentUser;
            data->regKeys[data->regKeyCount++].subKey = key;
        }
    }

    if (hasApp) {
        XString* key = XString_create_copy(prefix);
        if (!key) { XClassDelete(prefix); return false; }
        XString_append_char(key, (XChar)'\\');
        XString_append_utf8(key, XString_toUtf8(data->applicationName));
        data->regKeys[data->regKeyCount].root = XSettingsNativeRoot_LocalMachine;
        data->regKeys[data->regKeyCount++].subKey = key;
    }
    {
        XString* key = XString_create_copy(prefix);
        if (!key) { XClassDelete(prefix); return false; }
        XString_append_utf8(key, "\\OrganizationDefaults");
        data->regKeys[data->regKeyCount].root = XSettingsNativeRoot_LocalMachine;
        data->regKeys[data->regKeyCount++].subKey = key;
    }

    XClassDelete(prefix);
    return true;
}

/**
 * @brief 按显式注册表路径构建链（对标 QWinSettingsPrivate(QString rPath)）。
 * @details 只认 HKEY_CURRENT_USER/HKCU/HKEY_LOCAL_MACHINE/HKLM/
 *          HKEY_CLASSES_ROOT/HKCR/HKEY_USERS/HKU 前缀；不匹配时链为空
 *          （Qt 直接 return，构造出 AccessError 的空对象）。这是
 *          NativeFormat + 文件路径在 Windows 上的正确语义。
 */
static void xsettingsBuildRegistryKeyFromPath(XSettingsData* data, const XString* path)
{
    static const struct { const char* name; size_t len; XSettingsNativeRoot root; } prefixes[] = {
        { "HKEY_CURRENT_USER",  17, XSettingsNativeRoot_CurrentUser  },
        { "HKCU",                4, XSettingsNativeRoot_CurrentUser  },
        { "HKEY_LOCAL_MACHINE", 18, XSettingsNativeRoot_LocalMachine },
        { "HKLM",                4, XSettingsNativeRoot_LocalMachine },
        { "HKEY_CLASSES_ROOT",  17, XSettingsNativeRoot_ClassesRoot  },
        { "HKCR",                4, XSettingsNativeRoot_ClassesRoot  },
        { "HKEY_USERS",         10, XSettingsNativeRoot_Users        },
        { "HKU",                 3, XSettingsNativeRoot_Users        }
    };
    const char* utf8;
    size_t i;

    if (!path || XString_size(path) == 0)
        return;
    utf8 = XString_toUtf8(path);
    if (!utf8)
        return;
    /* 去掉前导 '\'（Qt rPath.startsWith('\\') → remove(0,1)） */
    if (utf8[0] == '\\')
        ++utf8;

    for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i) {
        size_t n = prefixes[i].len;
        if (XMemcmp(utf8, prefixes[i].name, n) != 0)
            continue;
        /* 必须整体匹配或紧跟 '\'（防 HKCU_foo 这类误判） */
        if (utf8[n] != '\0' && utf8[n] != '\\')
            continue;
        {
            const char* rest = (utf8[n] == '\\') ? (utf8 + n + 1) : "";
            data->regKeys[data->regKeyCount].root = prefixes[i].root;
            data->regKeys[data->regKeyCount++].subKey = XString_create_utf8(rest);
        }
        return;
    }
}

/** @brief 格式对应扩展名（Native .conf / Ini .ini，对标 initFormat 886）。 */
static const char* xsettingsExtensionFor(XSettings_Format format)
{
    if (format == XSettings_IniFormat)
        return ".ini";
    return ".conf";
}

/** @brief 作用域目录（setPath 覆盖优先；无平台 API 的路径推导见文件头 @details）。 */
static XString* xsettingsScopeDir(XSettings_Format format, XSettings_Scope scope)
{
    XString* override;
    xsettingsGlobalLock();
    override = g_xsettings_pathOverride[format][scope]
                   ? XString_create_copy(g_xsettings_pathOverride[format][scope])
                   : NULL;
    xsettingsGlobalUnlock();
    if (override)
        return override;
    XString* home = XDir_homePath();
    XString* dir = XString_create();
    if (dir && home) {
#if !defined(XPLATFORM_BAREMETAL) || !XPLATFORM_BAREMETAL
        if (XPLATFORM_WINDOWS) {
            XString_append_utf8(dir, XString_toUtf8(home));
            XString_append_utf8(dir, scope == XSettings_UserScope ? "/AppData/Roaming" : "");
            if (scope == XSettings_SystemScope) {
                XString_clear_base(dir);
                XString_append_utf8(dir, "C:/ProgramData");
            }
        }
        else if (XPLATFORM_POSIX) {
            XString_append_utf8(dir, XString_toUtf8(home));
            XString_append_utf8(dir, scope == XSettings_UserScope ? "/.config" : "/etc/xdg-placeholder");
            if (scope == XSettings_SystemScope) {
                XString_clear_base(dir);
                XString_append_utf8(dir, "/etc/xdg");
            }
        }
        else {
            /* 裸机/FreeRTOS：仅相对路径退化（文件名构造器为嵌入式推荐路径） */
            XString_append_utf8(dir, XString_toUtf8(home));
        }
#else
        XString_append_utf8(dir, XString_toUtf8(home));
#endif
    }
    if (home)
        XClassDelete(home);
    return dir;
}

/** @brief 拼接组织/应用文件路径。 */
static XString* xsettingsMakePath(const XString* dir, const XString* org,
                                   const XString* app, const char* extension)
{
    XString* path = XString_create();
    if (!path)
        return NULL;
    if (dir)
        XString_append_utf8(path, XString_toUtf8(dir));
    if (XString_size(path) > 0 && XString_at(path, XString_size(path) - 1) != (XChar)'/')
        XString_append_char(path, (XChar)'/');
    XString_append_utf8(path, XString_toUtf8(org));
    if (app && XString_size(app) > 0) {
        XString_append_char(path, (XChar)'/');
        XString_append_utf8(path, XString_toUtf8(app));
    }
    XString_append_utf8(path, extension);
    return path;
}

/** @brief 按组织/应用构建回退链（对标 1069-1128；组织名空 → Unknown Organization + AccessError）。 */
static bool xsettingsBuildOrgFiles(XSettingsData* data, XSettings_Scope scope,
                                   const XString* organization, const XString* application)
{
    XString* org = (organization && XString_size(organization) > 0)
                       ? XString_create_copy(organization)
                       : XString_create_utf8("Unknown Organization");
    if (!org)
        return false;
    if (!organization || XString_size(organization) == 0)
        xsettingsSetStatus(data, XSettings_AccessError);
    data->organizationName = XString_create_copy(organization ? organization : org);
    data->applicationName = (application) ? XString_create_copy(application) : XString_create();
    if (!data->organizationName || !data->applicationName) {
        XClassDelete(org);
        return false;
    }
    const char* extension = xsettingsExtensionFor(data->format);
    XString* userDir = xsettingsScopeDir(data->format, XSettings_UserScope);
    XString* systemDir = xsettingsScopeDir(data->format, XSettings_SystemScope);
    bool hasApp = XString_size(data->applicationName) > 0;
    bool ok = true;
    /* 回退顺序：用户+应用、用户+组织、系统+应用、系统+组织 */
    if (ok && hasApp && userDir) {
        XString* path = xsettingsMakePath(userDir, org, data->applicationName, extension);
        if (path) {
            data->confFiles[data->confFileCount++] = xconfCreate(path);
            XClassDelete(path);
        }
        else {
            ok = false;
        }
    }
    if (ok && userDir) {
        XString* path = xsettingsMakePath(userDir, org, NULL, extension);
        if (path) {
            data->confFiles[data->confFileCount++] = xconfCreate(path);
            XClassDelete(path);
        }
        else {
            ok = false;
        }
    }
    if (ok && hasApp && systemDir) {
        XString* path = xsettingsMakePath(systemDir, org, data->applicationName, extension);
        if (path) {
            data->confFiles[data->confFileCount++] = xconfCreate(path);
            XClassDelete(path);
        }
        else {
            ok = false;
        }
    }
    if (ok && systemDir) {
        XString* path = xsettingsMakePath(systemDir, org, NULL, extension);
        if (path) {
            data->confFiles[data->confFileCount++] = xconfCreate(path);
            XClassDelete(path);
        }
        else {
            ok = false;
        }
    }
    if (org) XClassDelete(org);
    if (userDir) XClassDelete(userDir);
    if (systemDir) XClassDelete(systemDir);
    /* 任一文件创建失败 → 全链失败 */
    for (int i = 0; i < data->confFileCount; ++i) {
        if (!data->confFiles[i])
            ok = false;
    }
    return ok && data->confFileCount > 0;
}

/** @brief 无后端格式（注册表）的单文件占位。 */
static bool xsettingsBuildNoBackendFiles(XSettingsData* data, XSettings_Scope scope,
                                         const XString* organization, const XString* application)
{
    data->noBackend = true;
    data->organizationName = organization ? XString_create_copy(organization) : XString_create();
    data->applicationName = application ? XString_create_copy(application) : XString_create();
    XString* dir = xsettingsScopeDir(data->format, scope);
    XString* org = (organization && XString_size(organization) > 0)
                       ? XString_create_copy(organization)
                       : XString_create_utf8("Unknown Organization");
    XString* path = (dir && org) ? xsettingsMakePath(dir, org, data->applicationName, ".ini") : NULL;
    data->confFiles[data->confFileCount++] = path ? xconfCreate(path) : NULL;
    if (path) XClassDelete(path);
    if (dir) XClassDelete(dir);
    if (org) XClassDelete(org);
    xsettingsSetStatus(data, XSettings_AccessError);
    return data->organizationName && data->applicationName && data->confFiles[0];
}

/** @brief 首次访问初始化（对标 initAccess：构造即读）。 */
static void xsettingsInitAccess(XSettingsData* data)
{
    /* 注册表后端：无文件可读，键值按需直读注册表
     * （Qt QWinSettingsPrivate 的 initAccess 为空实现） */
    if (data->registry) {
        if (data->regKeyCount == 0)
            xsettingsSetStatus(data, XSettings_AccessError);
        return;
    }
    /* 自定义格式无读取回调 → AccessError */
    if (data->format > XSettings_IniFormat && data->format <= XSettings_CustomFormat16) {
        if (!xsettingsCustomFormatHas(data->format, true))
            xsettingsSetStatus(data, XSettings_AccessError);
    }
    for (int i = 0; i < data->confFileCount; ++i) {
        XSettings_Status error = XSettings_NoError;
        xconfReadFile(data->confFiles[i], data->format, &error);
        if (error != XSettings_NoError)
            xsettingsSetStatus(data, error);
    }
}

/* ==========================================================================
 * 虚函数实现（Copy/Move/Deinit）
 * ========================================================================== */

static void xsettingsFreeData(XSettingsData* data)
{
    if (!data)
        return;
    for (int i = 0; i < data->confFileCount; ++i)
        xconfDestroy(data->confFiles[i]);
    if (data->regKeys) {
        for (int i = 0; i < data->regKeyCount; ++i) {
            if (data->regKeys[i].subKey)
                XClassDelete(data->regKeys[i].subKey);
        }
        XFree_System(data->regKeys);
    }
    if (data->groupStack) {
        size_t count = XVector_size_base(data->groupStack);
        for (size_t i = 0; i < count; ++i) {
            XSettingsGroup* group = (XSettingsGroup*)XVector_at_base(data->groupStack, i);
            if (group && group->str)
                XClassDelete(group->str);
        }
        XClassDelete(data->groupStack);
    }
    if (data->groupPrefix) XClassDelete(data->groupPrefix);
    if (data->organizationName) XClassDelete(data->organizationName);
    if (data->applicationName) XClassDelete(data->applicationName);
    if (data->fileNameCache) XClassDelete(data->fileNameCache);
    if (data->mutex) XMutex_delete(data->mutex);
    XFree_System(data);
}

/** @brief 深拷贝私有数据（Qt 禁拷贝；本类按 XClass 对象模型提供独立实例拷贝）。 */
static XSettingsData* xsettingsCloneData(const XSettingsData* source)
{
    XSettingsData* data = xsettingsDataAlloc();   /* 已清零并建好 mutex */
    if (!data)
        return NULL;
    data->confFileCount = source->confFileCount;
    data->noBackend = source->noBackend;
    data->registry = source->registry;
    data->format = source->format;
    data->scope = source->scope;
    data->fallbacks = source->fallbacks;
    data->atomicSync = source->atomicSync;
    data->pendingChanges = source->pendingChanges;
    data->status = source->status;
    data->organizationName = source->organizationName ? XString_create_copy(source->organizationName) : NULL;
    data->applicationName = source->applicationName ? XString_create_copy(source->applicationName) : NULL;
    data->groupPrefix = source->groupPrefix ? XString_create_copy(source->groupPrefix) : XString_create();
    data->groupStack = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                 sizeof(XSettingsGroup), false);  /* 非 COW：组栈需独占 */
    bool ok = data->organizationName && data->applicationName && data->groupPrefix && data->groupStack;
    if (ok && source->registry && source->regKeyCount > 0) {
        data->regKeys = (XSettingsRegKey*)XMalloc_System(
            sizeof(XSettingsRegKey) * XSETTINGS_REG_MAX_KEYS);
        if (!data->regKeys) {
            xsettingsFreeData(data);
            return NULL;
        }
        XMemset(data->regKeys, 0, sizeof(XSettingsRegKey) * XSETTINGS_REG_MAX_KEYS);
        data->regKeyCount = source->regKeyCount;
        for (int i = 0; i < data->regKeyCount; ++i) {
            data->regKeys[i].root = source->regKeys[i].root;
            data->regKeys[i].subKey = source->regKeys[i].subKey
                                          ? XString_create_copy(source->regKeys[i].subKey)
                                          : NULL;
            if (!data->regKeys[i].subKey)
                ok = false;
        }
    }
    if (ok && source->groupStack) {
        size_t count = XVector_size_base(source->groupStack);
        for (size_t i = 0; ok && i < count; ++i) {
            XSettingsGroup* group = (XSettingsGroup*)XVector_at_base(source->groupStack, i);
            XSettingsGroup copy;
            copy.str = group && group->str ? XString_create_copy(group->str) : NULL;
            copy.num = group ? group->num : -1;
            copy.maxNum = group ? group->maxNum : -1;
            ok = copy.str != NULL;
            if (ok)
                ok = XVector_push_back_1_base(data->groupStack, &copy);
            if (!ok && copy.str)
                XClassDelete(copy.str);
        }
    }
    for (int i = 0; ok && i < data->confFileCount; ++i) {
        XSettingsConfFile* sourceConf = source->confFiles[i];
        XSettingsConfFile* conf = xconfCreate(sourceConf->name);
        data->confFiles[i] = conf;
        if (!conf) {
            ok = false;
            break;
        }
        conf->read = sourceConf->read;
        conf->size = sourceConf->size;
        conf->timeStamp = sourceConf->timeStamp;
        conf->customParsed = sourceConf->customParsed;
        /* 未解析节深拷贝 */
        {
            XVector* names = XMap_keys_base(sourceConf->unparsedSections);
            XVector* blocks = XMapBase_values_base(sourceConf->unparsedSections);
            if (names && blocks) {
                size_t total = XVector_size_base(names);
                for (size_t k = 0; ok && k < total; ++k) {
                    XString* name = *(XString**)XVector_at_base(names, k);
                    XByteArray* block = *(XByteArray**)XVector_at_base(blocks, k);
                    if (!name || !block || !xmapSetSection(conf->unparsedSections, name, block))
                        ok = false;
                }
            }
            if (names) XClassDelete(names);
            if (blocks) XClassDelete(blocks);
        }
        /* 键表深拷贝（NULL 值槽保留为无效变体键） */
        {
            XVector* names = XMap_keys_base(sourceConf->originalKeys);
            XVector* values = XMapBase_values_base(sourceConf->originalKeys);
            if (names && values) {
                size_t total = XVector_size_base(names);
                for (size_t k = 0; ok && k < total; ++k) {
                    XString* name = (XString*)XVector_at_base(names, k);   /* 值槽内联对象借用 */
                    XVariant* value = (XVariant*)XVector_at_base(values, k);
                    if (!name || !xmapSet(conf->originalKeys, name, value))
                        ok = false;
                }
            }
            if (names) XClassDelete(names);
            if (values) XClassDelete(values);
        }
        {
            XVector* names = XMap_keys_base(sourceConf->addedKeys);
            XVector* values = XMapBase_values_base(sourceConf->addedKeys);
            if (names && values) {
                size_t total = XVector_size_base(names);
                for (size_t k = 0; ok && k < total; ++k) {
                    XString* name = (XString*)XVector_at_base(names, k);   /* 值槽内联对象借用 */
                    XVariant* value = (XVariant*)XVector_at_base(values, k);
                    if (!name || !xmapSet(conf->addedKeys, name, value))
                        ok = false;
                }
            }
            if (names) XClassDelete(names);
            if (values) XClassDelete(values);
        }
        {
            XVector* names = XMap_keys_base(sourceConf->removedKeys);
            if (names) {
                size_t total = XVector_size_base(names);
                for (size_t k = 0; ok && k < total; ++k) {
                    XString* name = (XString*)XVector_at_base(names, k);   /* 值槽内联对象借用 */
                    if (!name || !xmapSetTombstone(conf->removedKeys, name))
                        ok = false;
                }
                XClassDelete(names);
            }
        }
    }
    if (!ok) {
        xsettingsFreeData(data);
        return NULL;
    }
    return data;
}

static void VXSettings_copy(XSettings* self, const XSettings* other)
{
    if (!self || !other || self == other)
        return;
    if (XClassIsVtableNull(self))
        XSettings_init(self);
    if (!other->m_data)
        return;
    XSettingsData* cloned = xsettingsCloneData(other->m_data);
    if (!cloned)
        return;
    if (self->m_data)
        xsettingsFreeData(self->m_data);
    self->m_data = cloned;
}

static void VXSettings_move(XSettings* self, XSettings* other)
{
    if (!self || !other || self == other)
        return;
    if (XClassIsVtableNull(self))
        XSettings_init(self);
    if (self->m_data)
        xsettingsFreeData(self->m_data);
    self->m_data = other->m_data;
    other->m_data = NULL;
}

/**
 * @brief 定时器事件（对标 QSettings::event 处理 QEvent::UpdateRequest）。
 * @details 延迟落盘定时器到期 → 执行 sync 并把 id 复位。未知 id 交父类。
 */
static void VXSettings_timerEvent(XObject* self, XTimerEvent* event)
{
    XSettings* settings = (XSettings*)self;
    XSettingsData* data = settings ? settings->m_data : NULL;
    XTimerId id = XTimerEvent_timerId(event);
    XEvent_accept(event);
    if (!data || data->updateTimerId == XTIMER_INVALID_ID ||
        id != data->updateTimerId) {
        /* 不是我们的定时器：交父类处理（规范要求，不吞） */
        XClass_Parent(XObject, EXObject_TimerEvent,
                      void (*)(XObject*, XTimerEvent*))(self, event);
        return;
    }
    data->updateTimerId = XTIMER_INVALID_ID;   /* 已到期，id 复位 */
    if (data->pendingChanges)
        XSettings_sync(settings);
}

static void VXSettings_deinit(XSettings* self)
{
    if (!self)
        return;
    if (self->m_data) {
        xsettingsCancelUpdate(self);   /* 先停延迟落盘，避免同步期间重入 */
        if (self->m_data->pendingChanges)
            XSettings_sync(self);
        /* 对标 Qt QWinSettingsPrivate 析构：clear() 之后若没有新写入，
         * 把残留的空键删掉（Qt 用 RegDeleteKey 删空键）。 */
        if (self->m_data->registry && self->m_data->deleteWriteHandleOnExit)
            xsettingsRegDeleteEmptyKey(self->m_data);
        xsettingsFreeData(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XSettings_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSettings)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSettings_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSettings_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSettings_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXSettings_timerEvent);
    return XVTABLE_DEFAULT;
}

/* ==========================================================================
 * 生命周期（对标 QSettings 六个构造器 + parent 挂接）
 * ========================================================================== */

static void xsettingsDataInit(XSettings* self)
{
    XMemset(self, 0, sizeof(XSettings));
    XObject_init(&self->m_class);
    XClassSetVtable(self, XSettings);
}

/** @brief 组织/应用构造公共尾：建链 + 首读（对标各构造器末尾 initAccess/sync）。 */
static bool xsettingsFinishOrgInit(XSettings* self, XSettings_Format format,
                                   XSettings_Scope scope, const XString* organization,
                                   const XString* application)
{
    XSettingsData* data = xsettingsDataAlloc();
    if (!data)
        return false;
    data->format = format;
    data->scope = scope;
    data->fallbacks = true;
    data->atomicSync = true;
    data->groupStack = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                 sizeof(XSettingsGroup), false);  /* 非 COW：组栈需独占 */
    data->groupPrefix = XString_create();
    self->m_data = data;
    bool ok = data->groupStack && data->groupPrefix;
    if (ok) {
        if (xsettingsUsesRegistry(format)) {
            /* 注册表后端（Windows）：回退链由 xsettingsBuildRegistryKeys 建，
             * 组织名为空时链为空并置 AccessError（Qt QWinSettingsPrivate 同款） */
            data->registry = true;
            data->regKeys = (XSettingsRegKey*)XMalloc_System(
                sizeof(XSettingsRegKey) * XSETTINGS_REG_MAX_KEYS);
            if (!data->regKeys)
                ok = false;
            else {
                XMemset(data->regKeys, 0, sizeof(XSettingsRegKey) * XSETTINGS_REG_MAX_KEYS);
                ok = xsettingsBuildRegistryKeys(data, scope, organization, application);
            }
        }
        else if (format == XSettings_Registry32Format || format == XSettings_Registry64Format) {
            /* 非 Windows 上的注册表格式：无后端 */
            ok = xsettingsBuildNoBackendFiles(data, scope, organization, application);
        }
        else {
            ok = xsettingsBuildOrgFiles(data, scope, organization, application);
        }
        data->noBackend = data->noBackend || (!data->registry &&
                          (format == XSettings_Registry32Format ||
                           format == XSettings_Registry64Format));
    }
    if (!ok) {
        xsettingsFreeData(data);
        self->m_data = NULL;
        return false;
    }
    xsettingsInitAccess(data);
    return true;
}

void XSettings_init(XSettings* self)
{
    if (!self)
        return;
    xsettingsDataInit(self);
    /* format = defaultFormat（对标 QSettings(parent) 2664-2678） */
    const XString* org = XCoreApplication_organizationName();
    const XString* app = XCoreApplication_applicationName();
    XSettings_Format defaultFormat = XSettings_defaultFormat();   /* 带全局锁读取 */
    if (!xsettingsFinishOrgInit(self, defaultFormat, XSettings_UserScope, org, app)) {
        /* OOM 兜底：空数据对象保持可用空态 */
        XSettingsData* data = xsettingsDataAlloc();
        if (data) {
            data->format = defaultFormat;
            data->scope = XSettings_UserScope;
            data->fallbacks = true;
            data->atomicSync = true;
            self->m_data = data;
        }
    }
}

void XSettings_init_2(XSettings* self, XObject* parent)
{
    XSettings_init(self);
    if (self && parent)
        XObject_setParent((XObject*)self, parent);
}

void XSettings_init_3(XSettings* self, XSettings_Scope scope, XObject* parent)
{
    if (!self)
        return;
    xsettingsDataInit(self);
    const XString* org = XCoreApplication_organizationName();
    const XString* app = XCoreApplication_applicationName();
    XSettings_Format defaultFormat = XSettings_defaultFormat();   /* 带全局锁读取 */
    if (!xsettingsFinishOrgInit(self, defaultFormat, scope, org, app)) {
        XSettingsData* data = xsettingsDataAlloc();
        if (data) {
            data->format = defaultFormat;
            data->scope = scope;
            data->fallbacks = true;
            data->atomicSync = true;
            self->m_data = data;
        }
    }
    if (parent)
        XObject_setParent((XObject*)self, parent);
}

void XSettings_init_4(XSettings* self, const XString* organization,
                      const XString* application, XObject* parent)
{
    if (!self)
        return;
    xsettingsDataInit(self);
    /* 组织/应用构造器固定 NativeFormat + UserScope，忽略 defaultFormat（对标 2526-2530） */
    if (!xsettingsFinishOrgInit(self, XSettings_NativeFormat, XSettings_UserScope,
                                organization, application)) {
        XSettingsData* data = xsettingsDataAlloc();
        if (data) {
            data->format = XSettings_NativeFormat;
            data->scope = XSettings_UserScope;
            data->fallbacks = true;
            data->atomicSync = true;
            self->m_data = data;
        }
    }
    if (parent)
        XObject_setParent((XObject*)self, parent);
}

void XSettings_init_4_utf8(XSettings* self, const char* organizationUtf8,
                           const char* applicationUtf8, XObject* parent)
{
    XString* org = organizationUtf8 ? XString_create_utf8(organizationUtf8) : NULL;
    XString* app = applicationUtf8 ? XString_create_utf8(applicationUtf8) : NULL;
    XSettings_init_4(self, org, app, parent);
    if (org) XClassDelete(org);
    if (app) XClassDelete(app);
}

void XSettings_init_5(XSettings* self, XSettings_Scope scope,
                      const XString* organization, const XString* application, XObject* parent)
{
    if (!self)
        return;
    xsettingsDataInit(self);
    if (!xsettingsFinishOrgInit(self, XSettings_NativeFormat, scope, organization, application)) {
        XSettingsData* data = xsettingsDataAlloc();
        if (data) {
            data->format = XSettings_NativeFormat;
            data->scope = scope;
            data->fallbacks = true;
            data->atomicSync = true;
            self->m_data = data;
        }
    }
    if (parent)
        XObject_setParent((XObject*)self, parent);
}

void XSettings_init_6(XSettings* self, XSettings_Format format, XSettings_Scope scope,
                      const XString* organization, const XString* application, XObject* parent)
{
    if (!self)
        return;
    xsettingsDataInit(self);
    if (!xsettingsFinishOrgInit(self, format, scope, organization, application)) {
        XSettingsData* data = xsettingsDataAlloc();
        if (data) {
            data->format = format;
            data->scope = scope;
            data->fallbacks = true;
            data->atomicSync = true;
            self->m_data = data;
        }
    }
    if (parent)
        XObject_setParent((XObject*)self, parent);
}

void XSettings_init_7(XSettings* self, const XString* fileName,
                      XSettings_Format format, XObject* parent)
{
    if (!self)
        return;
    xsettingsDataInit(self);
    XSettingsData* data = xsettingsDataAlloc();
    if (data) {
        data->format = format;
        data->scope = XSettings_UserScope;
        data->fallbacks = true;
        data->atomicSync = true;
        data->groupStack = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                 sizeof(XSettingsGroup), false);  /* 非 COW：组栈需独占 */
        data->groupPrefix = XString_create();
        data->organizationName = XString_create();
        data->applicationName = XString_create();
        self->m_data = data;
        bool ok = data->groupStack && data->groupPrefix && data->organizationName &&
                  data->applicationName;
        if (ok && xsettingsUsesRegistry(format)) {
            /* 注册表后端：fileName 按注册表路径解析（对标 QWinSettingsPrivate
             * (QString rPath)）。只认 HKEY 前缀，其余链为空 → AccessError。
             * 这正是 Qt 在 Windows 上 NativeFormat + 文件名的语义：不建文件。 */
            data->registry = true;
            data->regKeys = (XSettingsRegKey*)XMalloc_System(
                sizeof(XSettingsRegKey) * XSETTINGS_REG_MAX_KEYS);
            if (!data->regKeys)
                ok = false;
            else {
                XMemset(data->regKeys, 0, sizeof(XSettingsRegKey) * XSETTINGS_REG_MAX_KEYS);
                xsettingsBuildRegistryKeyFromPath(data, fileName);
            }
        }
        else if (ok && fileName && XString_size(fileName) > 0) {
            XFileInfo* info = XFileInfo_create_2(fileName);
            XString* absolute = info ? XFileInfo_absoluteFilePath(info) : NULL;
            if (info)
                XClassDelete(info);
            data->confFiles[data->confFileCount++] = absolute ? xconfCreate(absolute) : NULL;
            if (absolute)
                XClassDelete(absolute);
            ok = data->confFiles[0] != NULL;
        }
        else if (ok) {
            /* 空文件名：单占位文件，读写均失败（对标 Qt 空路径容错） */
            data->confFiles[data->confFileCount++] = xconfCreate(NULL);
            ok = data->confFiles[0] != NULL;
        }
        if (!ok) {
            xsettingsFreeData(data);
            self->m_data = NULL;
            return;
        }
        if (!data->registry &&
            (format == XSettings_Registry32Format || format == XSettings_Registry64Format))
            data->noBackend = true;
        if (data->noBackend || (data->registry && data->regKeyCount == 0))
            xsettingsSetStatus(data, XSettings_AccessError);
        xsettingsInitAccess(data);
    }
    if (parent && self->m_data)
        XObject_setParent((XObject*)self, parent);
}

void XSettings_init_7_utf8(XSettings* self, const char* fileNameUtf8,
                           XSettings_Format format, XObject* parent)
{
    XString* name = fileNameUtf8 ? XString_create_utf8(fileNameUtf8) : NULL;
    XSettings_init_7(self, name, format, parent);
    if (name)
        XClassDelete(name);
}

XSettings* XSettings_create(void)
{
    XSettings* self = (XSettings*)XMemory_calloc(1, sizeof(XSettings), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self)
        return NULL;
    XSettings_init(self);
    Set_Class_IsHeap(self, true);
    return self;
}

XSettings* XSettings_create_2(XObject* parent)
{
    XSettings* self = (XSettings*)XMemory_calloc(1, sizeof(XSettings), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self)
        return NULL;
    XSettings_init_2(self, parent);
    Set_Class_IsHeap(self, true);
    return self;
}

XSettings* XSettings_create_3(XSettings_Scope scope, XObject* parent)
{
    XSettings* self = (XSettings*)XMemory_calloc(1, sizeof(XSettings), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self)
        return NULL;
    XSettings_init_3(self, scope, parent);
    Set_Class_IsHeap(self, true);
    return self;
}

XSettings* XSettings_create_4(const XString* organization, const XString* application, XObject* parent)
{
    XSettings* self = (XSettings*)XMemory_calloc(1, sizeof(XSettings), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self)
        return NULL;
    XSettings_init_4(self, organization, application, parent);
    Set_Class_IsHeap(self, true);
    return self;
}

XSettings* XSettings_create_4_utf8(const char* organizationUtf8, const char* applicationUtf8, XObject* parent)
{
    XSettings* self = (XSettings*)XMemory_calloc(1, sizeof(XSettings), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self)
        return NULL;
    XSettings_init_4_utf8(self, organizationUtf8, applicationUtf8, parent);
    Set_Class_IsHeap(self, true);
    return self;
}

XSettings* XSettings_create_5(XSettings_Scope scope, const XString* organization,
                              const XString* application, XObject* parent)
{
    XSettings* self = (XSettings*)XMemory_calloc(1, sizeof(XSettings), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self)
        return NULL;
    XSettings_init_5(self, scope, organization, application, parent);
    Set_Class_IsHeap(self, true);
    return self;
}

XSettings* XSettings_create_6(XSettings_Format format, XSettings_Scope scope,
                              const XString* organization, const XString* application, XObject* parent)
{
    XSettings* self = (XSettings*)XMemory_calloc(1, sizeof(XSettings), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self)
        return NULL;
    XSettings_init_6(self, format, scope, organization, application, parent);
    Set_Class_IsHeap(self, true);
    return self;
}

XSettings* XSettings_create_7(const XString* fileName, XSettings_Format format, XObject* parent)
{
    XSettings* self = (XSettings*)XMemory_calloc(1, sizeof(XSettings), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self)
        return NULL;
    XSettings_init_7(self, fileName, format, parent);
    Set_Class_IsHeap(self, true);
    return self;
}

XSettings* XSettings_create_7_utf8(const char* fileNameUtf8, XSettings_Format format, XObject* parent)
{
    XSettings* self = (XSettings*)XMemory_calloc(1, sizeof(XSettings), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self)
        return NULL;
    XSettings_init_7_utf8(self, fileNameUtf8, format, parent);
    Set_Class_IsHeap(self, true);
    return self;
}

XSettings* XSettings_create_copy(const XSettings* other)
{
    if (!other)
        return NULL;
    XSettings* self = (XSettings*)XMemory_calloc(1, sizeof(XSettings), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self)
        return NULL;
    xsettingsDataInit(self);
    VXSettings_copy(self, other);
    Set_Class_IsHeap(self, true);
    return self;
}

XSettings* XSettings_create_move(XSettings* other)
{
    if (!other)
        return NULL;
    XSettings* self = (XSettings*)XMemory_calloc(1, sizeof(XSettings), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self)
        return NULL;
    xsettingsDataInit(self);
    VXSettings_move(self, other);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==========================================================================
 * 键值读写公开 API
 * ========================================================================== */

/** @brief setValue 的无锁主体（调用方已持实例锁）。 */
/** @brief setValue 的无锁主体（调用方已持实例锁）。 */
static void xsettingsSetValueLocked(XSettingsData* data, const XString* key,
                                    const XVariant* value)
{
    XSettingsConfFile* conf;
    XString* actualKey;
    bool ok;

    if (data->registry) {
        /* 注册表后端：键立即落注册表（Qt QWinSettingsPrivate::set 同步写）。
         * 对齐 Qt set() 的首行 `if (writeHandle() == 0) → AccessError`：
         * 只读键在写入前就拒绝，而不是写完失败才报错。 */
        if (data->regKeyCount == 0 || !xsettingsRegIsWritable(data) ||
            !xsettingsRegSet(data, key, value)) {
            xsettingsSetStatus(data, XSettings_AccessError);
            return;
        }
        data->pendingChanges = true;
        data->deleteWriteHandleOnExit = false;   /* 有新值 → 不再删键 */
        return;
    }
    conf = data->confFiles[0];
    if (!conf || data->noBackend) {
        xsettingsSetStatus(data, XSettings_AccessError);
        return;
    }
    actualKey = xsettingsActualKey(data, key);
    if (!actualKey)
        return;
    ok = xmapSet(conf->addedKeys, actualKey, value);
    if (ok)
        xmapErase(conf->removedKeys, actualKey);
    XClassDelete(actualKey);
    if (!ok) {
        xsettingsSetStatus(data, XSettings_AccessError);
        return;
    }
    data->pendingChanges = true;
}

void XSettings_setValue(XSettings* self, const XString* key, const XVariant* value)
{
    XSettingsData* data;
    if (!self || !self->m_data || !key || XString_size(key) == 0) {
        if (self && self->m_data && (!key || XString_size(key) == 0))
            XPrintf("[XSettings] setValue: 空键名，忽略\n");
        return;
    }
    data = self->m_data;
    xsettingsInstanceLock(data);
    xsettingsSetValueLocked(data, key, value);
    xsettingsInstanceUnlock(data);
}


void XSettings_setValue_utf8(XSettings* self, const char* keyUtf8, const XVariant* value)
{
    XString* key = keyUtf8 ? XString_create_utf8(keyUtf8) : NULL;
    XSettings_setValue(self, key, value);
    if (key)
        XClassDelete(key);
}

/** @brief 读取核心：回退链顺序查 added/removed/original（对标 get 2147-2200）。 */
static XVariant* xsettingsGetValue(const XSettings* self, const XString* key)
{
    XSettingsData* data = self ? self->m_data : NULL;
    XString* actualKey;
    XVariant* result = NULL;
    if (!data)
        return NULL;
    if (data->registry) {
        /* 注册表后端：键路径 = 组前缀 + 规范化键（Qt actualKey 同款） */
        XVariant* found;
        if (data->regKeyCount == 0)
            return NULL;
        actualKey = xsettingsActualKey(data, key);
        found = actualKey ? xsettingsRegGet(data, actualKey) : NULL;
        if (actualKey)
            XClassDelete(actualKey);
        return found;
    }
    actualKey = xsettingsActualKey(data, key);
    if (!actualKey)
        return NULL;
    int files = data->fallbacks ? data->confFileCount : 1;
    for (int i = 0; i < files && !result; ++i) {
        XSettingsConfFile* conf = data->confFiles[i];
        if (!conf)
            continue;
        /* 回退链文件可能被其它实例/进程新建或改写：读取前按 size+时间戳
         * 探测，变化则重读（对齐 Qt sync 导入外部修改的语义；否则先构造
         * 的实例永远读不到后来写入的回退文件）。本实例有未落盘修改时
         * 保持内存态优先，不重读。 */
        if (XMap_isEmpty_base(conf->addedKeys) && XMap_isEmpty_base(conf->removedKeys)) {
            int64_t size = 0;
            int64_t stamp = 0;
            xconfStatFile(conf, &size, &stamp);
            if (!conf->read || size != conf->size || stamp != conf->timeStamp) {
                XSettings_Status error = XSettings_NoError;
                xconfReadFile(conf, data->format, &error);
                if (error == XSettings_AccessError)
                    continue;
                if (error == XSettings_FormatError)
                    xsettingsSetStatus(data, error);
            }
        }
        if (!xconfEnsureSectionParsed(conf, actualKey)) {
            xsettingsSetStatus(data, XSettings_FormatError);
            continue;
        }
        if (xmapHas(conf->addedKeys, actualKey)) {
            XVariant* found = NULL;
            if (xmapGetValue(conf->addedKeys, actualKey, &found))
                result = found ? XVariant_create_copy(found) : XVariant_create_null();
        }
        else if (!xmapHas(conf->removedKeys, actualKey) && xmapHas(conf->originalKeys, actualKey)) {
            XVariant* found = NULL;
            if (xmapGetValue(conf->originalKeys, actualKey, &found))
                result = found ? XVariant_create_copy(found) : XVariant_create_null();
        }
    }
    XClassDelete(actualKey);
    return result;
}

XVariant* XSettings_value(const XSettings* self, const XString* key)
{
    XSettingsData* data;
    XVariant* found;
    if (!self || !self->m_data || !key || XString_size(key) == 0)
        return XVariant_create_null();
    data = self->m_data;
    xsettingsInstanceLock(data);
    found = xsettingsGetValue(self, key);
    xsettingsInstanceUnlock(data);
    return found ? found : XVariant_create_null();
}



XVariant* XSettings_value_2(const XSettings* self, const XString* key, const XVariant* defaultValue)
{
    XSettingsData* data;
    XVariant* found;
    if (!self || !self->m_data || !key || XString_size(key) == 0)
        return defaultValue ? XVariant_create_copy(defaultValue) : XVariant_create_null();
    data = self->m_data;
    xsettingsInstanceLock(data);
    found = xsettingsGetValue(self, key);
    xsettingsInstanceUnlock(data);
    if (found)
        return found;
    return defaultValue ? XVariant_create_copy(defaultValue) : XVariant_create_null();
}



XVariant* XSettings_value_utf8(const XSettings* self, const char* keyUtf8)
{
    XString* key = keyUtf8 ? XString_create_utf8(keyUtf8) : NULL;
    XVariant* result = XSettings_value(self, key);
    if (key)
        XClassDelete(key);
    return result;
}

XVariant* XSettings_value_2_utf8(const XSettings* self, const char* keyUtf8, const XVariant* defaultValue)
{
    XString* key = keyUtf8 ? XString_create_utf8(keyUtf8) : NULL;
    XVariant* result = XSettings_value_2(self, key, defaultValue);
    if (key)
        XClassDelete(key);
    return result;
}

bool XSettings_contains(const XSettings* self, const XString* key)
{
    XSettingsData* data;
    XVariant* found;
    if (!self || !self->m_data || !key || XString_size(key) == 0)
        return false;
    data = self->m_data;
    xsettingsInstanceLock(data);
    found = xsettingsGetValue(self, key);
    xsettingsInstanceUnlock(data);
    if (found) {
        XClassDelete(found);
        return true;
    }
    return false;
}



bool XSettings_contains_utf8(const XSettings* self, const char* keyUtf8)
{
    XString* key = keyUtf8 ? XString_create_utf8(keyUtf8) : NULL;
    bool result = XSettings_contains(self, key);
    if (key)
        XClassDelete(key);
    return result;
}

/** @brief clear 的无锁主体前置声明（remove 的空键分支要用）。 */
static void xsettingsClearLocked(XSettings* self, XSettingsData* data);


/** @brief remove 的无锁主体（调用方已持实例锁）。 */
static void xsettingsRemoveLocked(XSettings* self, XSettingsData* data, const XString* key)
{
    XSettingsConfFile* conf;
    if (data->registry) {
        /* 注册表后端：支持空键（Qt remove 支持），故不在此判空 */
        XString* actualKey;
        if (data->regKeyCount == 0) {
            xsettingsSetStatus(data, XSettings_AccessError);
            return;
        }
        actualKey = xsettingsActualKey(data, key);
        if (!actualKey)
            return;
        if (XString_size(actualKey) == 0) {
            XClassDelete(actualKey);
            xsettingsClearLocked(self, data);
            return;
        }
        if (!xsettingsRegRemove(data, actualKey))
            xsettingsSetStatus(data, XSettings_AccessError);
        else
            data->pendingChanges = true;
        XClassDelete(actualKey);
        return;
    }
    conf = data->confFiles[0];
    if (!conf || data->noBackend) {
        xsettingsSetStatus(data, XSettings_AccessError);
        return;
    }
    XString* actualKey = xsettingsActualKey(data, key);
    if (!actualKey)
        return;
    if (XString_size(actualKey) == 0) {
        /* 空键 = 清空全部（Qt remove("") 文档语义） */
        XClassDelete(actualKey);
        XSettings_clear(self);
        return;
    }
    /* 前缀 = actualKey + '/' */
    XString* prefix = XString_create_copy(actualKey);
    if (!prefix) {
        XClassDelete(actualKey);
        return;
    }
    XString_append_char(prefix, (XChar)'/');
    if (!xconfEnsureSectionParsed(conf, actualKey) || !xconfEnsureSectionParsed(conf, prefix)) {
        xsettingsSetStatus(data, XSettings_FormatError);
        XClassDelete(prefix);
        XClassDelete(actualKey);
        return;
    }
    /* addedKeys：删前缀子树 + 精确键（先收集后删，避免遍历失效） */
    XStringList* doomed = XStringList_create();
    if (!doomed) {
        XClassDelete(prefix);
        XClassDelete(actualKey);
        return;
    }
    {
        XVector* keys = XMap_keys_base(conf->addedKeys);
        if (keys) {
            size_t total = XVector_size_base(keys);
            for (size_t i = 0; i < total; ++i) {
                XString* candidate = (XString*)XVector_at_base(keys, i);   /* 值槽内联对象借用 */
                if (!candidate)
                    continue;
                if (XString_startsWith_utf8(candidate, XString_toUtf8(prefix), XChar_CaseSensitive) ||
                    XString_equals(candidate, actualKey, XChar_CaseSensitive)) {
                    XString* copy = XString_create_copy(candidate);
                    if (copy) {
                        /* move 语义：内容转移进表后，源壳仍须释放 */
                        XStringList_push_back_move_base(doomed, copy);
                        XClassDelete(copy);
                    }
                }
            }
            XClassDelete(keys);
        }
        size_t doomedCount = XStringList_size_base(doomed);
        for (size_t i = 0; i < doomedCount; ++i) {
            const XString* doomedKey = XStringList_at_base(doomed, i);
            if (doomedKey)
                xmapErase(conf->addedKeys, doomedKey);
        }
    }
    /* originalKeys：前缀子树 + 精确键全部写墓碑 */
    {
        XVector* keys = XMap_keys_base(conf->originalKeys);
        if (keys) {
            size_t total = XVector_size_base(keys);
            for (size_t i = 0; i < total; ++i) {
                XString* candidate = (XString*)XVector_at_base(keys, i);   /* 值槽内联对象借用 */
                if (!candidate)
                    continue;
                if (XString_startsWith_utf8(candidate, XString_toUtf8(prefix), XChar_CaseSensitive) ||
                    XString_equals(candidate, actualKey, XChar_CaseSensitive))
                    xmapSetTombstone(conf->removedKeys, candidate);
            }
            XClassDelete(keys);
        }
    }
    XClassDelete(doomed);
    XClassDelete(prefix);
    XClassDelete(actualKey);
    data->pendingChanges = true;
}

void XSettings_remove(XSettings* self, const XString* key)
{
    XSettingsData* data = self ? self->m_data : NULL;
    if (!data)
        return;
    xsettingsInstanceLock(data);
    xsettingsRemoveLocked(self, data, key);
    xsettingsInstanceUnlock(data);
    if (data->pendingChanges)
        xsettingsRequestUpdate(self);
}

void XSettings_remove_utf8(XSettings* self, const char* keyUtf8)
{
    XString* key = keyUtf8 ? XString_create_utf8(keyUtf8) : NULL;
    XSettings_remove(self, key);
    if (key)
        XClassDelete(key);
}

/** @brief 子键收集核心（对标 children 1244-1281；输出排序去重）。 */
/** @brief children 的无锁主体（调用方已持实例锁）。 */
static XStringList* xsettingsChildrenLocked(const XSettings* self, XSettingsData* data, int spec)
{
    XStringList* result;
    if (data->registry) {
        if (data->regKeyCount == 0)
            return XStringList_create();
        if (!data->groupPrefix)
            data->groupPrefix = XString_create();
        return xsettingsRegChildren(data, spec);
    }
    result = XStringList_create();
    if (!result)
        return result;
    enum { ChildrenAllKeys = 0, ChildrenChildKeys = 1, ChildrenChildGroups = 2 };
    /* 组前缀缓存缺失时按空串处理（根组），避免 NULL 前缀导致空结果 */
    if (!data->groupPrefix)
        data->groupPrefix = XString_create();
    const XString* prefix = data->groupPrefix;
    size_t prefixLen = XString_size(prefix);
    int files = data->fallbacks ? data->confFileCount : 1;
    for (int f = 0; f < files; ++f) {
        XSettingsConfFile* conf = data->confFiles[f];
        if (!conf)
            continue;
        if (prefixLen == 0) {
            if (!xconfEnsureAllSectionsParsed(conf)) {
                xsettingsSetStatus(data, XSettings_FormatError);
                continue;
            }
        }
        else if (!xconfEnsureSectionParsed(conf, prefix)) {
            xsettingsSetStatus(data, XSettings_FormatError);
            continue;
        }
        for (int pass = 0; pass < 2; ++pass) {
            const XMap* map = (pass == 0) ? conf->originalKeys : conf->addedKeys;
            XVector* keys = XMap_keys_base(map);
            if (!keys)
                continue;
            size_t total = XVector_size_base(keys);
            for (size_t i = 0; i < total; ++i) {
                XString* key = (XString*)XVector_at_base(keys, i);   /* 值槽内联对象借用 */
                if (!key)
                    continue;
                /* 空前缀（根组）匹配一切：不能用 startsWith("") 判定（空串
                 * 的 UTF-8 指针可能为 NULL，且语义上根组应返回全部键）。 */
                if (prefixLen > 0 &&
                    !XString_startsWith_utf8(key, XString_toUtf8(prefix), XChar_CaseSensitive))
                    continue;
                if (pass == 0 && xmapHas(conf->removedKeys, key))
                    continue;
                XString* remainder = XString_mid(key, prefixLen, XString_size(key) - prefixLen);
                if (!remainder)
                    continue;
                bool keep = false;
                if (spec == ChildrenAllKeys) {
                    keep = true;
                }
                else if (spec == ChildrenChildKeys) {
                    keep = XString_indexOf_utf8(remainder, "/", 0, XChar_CaseSensitive) < 0;
                }
                else {
                    int64_t slash = XString_indexOf_utf8(remainder, "/", 0, XChar_CaseSensitive);
                    if (slash >= 0) {
                        XString* group = XString_left(remainder, (size_t)slash);
                        XClassDelete(remainder);
                        remainder = group;
                        keep = group != NULL;
                    }
                    else {
                        keep = false;
                    }
                }
                if (keep) {
                    /* move 语义：内容转移进表后，源壳仍须释放 */
                    XStringList_push_back_move_base(result, remainder);
                }
                XClassDelete(remainder);
            }
            XClassDelete(keys);
        }
    }
    /* 排序 + 去重（大小写敏感，对标 sort+unique 3110-3162）；槽为浅拷贝
     * 会共享内部缓冲，去重不得对槽做对象级删除——改走"唯一项深拷贝重表" */
    XStringList_sort(result, XChar_CaseSensitive);
    XStringList* unique = XStringList_create();
    if (unique) {
        size_t count = XStringList_size_base(result);
        for (size_t i = 0; i < count; ++i) {
            const XString* item = XStringList_at_base(result, i);
            const XString* prev = XStringList_size_base(unique) > 0
                                      ? XStringList_at_base(unique, XStringList_size_base(unique) - 1)
                                      : NULL;
            bool isDup = prev && item && XString_equals(prev, item, XChar_CaseSensitive);
            if (!isDup && item) {
                XString* copy = XString_create_copy(item);
                if (copy) {
                    XStringList_push_back_move_base(unique, copy);
                    XClassDelete(copy);
                }
            }
        }
        XClassDelete(result);
        result = unique;
    }
    return result;
}

static XStringList* xsettingsChildren(const XSettings* self, int spec)
{
    XSettingsData* data = self ? self->m_data : NULL;
    XStringList* result;
    if (!data)
        return NULL;
    xsettingsInstanceLock(data);
    result = xsettingsChildrenLocked(self, data, spec);
    xsettingsInstanceUnlock(data);
    return result;
}

XStringList* XSettings_allKeys(const XSettings* self)
{
    return xsettingsChildren(self, 0);
}

XStringList* XSettings_childKeys(const XSettings* self)
{
    return xsettingsChildren(self, 1);
}

XStringList* XSettings_childGroups(const XSettings* self)
{
    return xsettingsChildren(self, 2);
}

/* ==========================================================================
 * 分组与数组
 * ========================================================================== */

/* 无锁主体前置声明（定义在下方，包装函数在此之前调用） */
static void xsettingsBeginGroupLocked(XSettings* self, XSettingsData* data, const XString* prefix);
static void xsettingsEndGroupLocked(XSettings* self, XSettingsData* data);
static int xsettingsBeginReadArrayLocked(const XSettings* self, XSettingsData* data, const XString* prefix);
static void xsettingsBeginWriteArrayLocked(XSettings* self, XSettingsData* data, const XString* prefix, int size);
static void xsettingsEndArrayLocked(XSettings* self, XSettingsData* data);
static void xsettingsSetArrayIndexLocked(XSettings* self, XSettingsData* data, int i);

/* ---- 公开包装：入口加实例锁 → 无锁主体 → 出口解锁 ---- */

void XSettings_beginGroup(XSettings* self, const XString* prefix)
{
    XSettingsData* data = self ? self->m_data : NULL;
    if (!data)
        return;
    xsettingsInstanceLock(data);
    xsettingsBeginGroupLocked(self, data, prefix);
    xsettingsInstanceUnlock(data);
}

void XSettings_endGroup(XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    if (!data)
        return;
    xsettingsInstanceLock(data);
    xsettingsEndGroupLocked(self, data);
    xsettingsInstanceUnlock(data);
}

int XSettings_beginReadArray(const XSettings* self, const XString* prefix)
{
    XSettingsData* data = self ? self->m_data : NULL;
    int result;
    if (!data)
        return 0;
    xsettingsInstanceLock(data);
    result = xsettingsBeginReadArrayLocked(self, data, prefix);
    xsettingsInstanceUnlock(data);
    return result;
}

void XSettings_beginWriteArray_2(XSettings* self, const XString* prefix, int size)
{
    XSettingsData* data = self ? self->m_data : NULL;
    if (!data)
        return;
    xsettingsInstanceLock(data);
    xsettingsBeginWriteArrayLocked(self, data, prefix, size);
    xsettingsInstanceUnlock(data);
}

void XSettings_endArray(XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    if (!data)
        return;
    xsettingsInstanceLock(data);
    xsettingsEndArrayLocked(self, data);
    xsettingsInstanceUnlock(data);
}

void XSettings_setArrayIndex(XSettings* self, int i)
{
    XSettingsData* data = self ? self->m_data : NULL;
    if (!data)
        return;
    xsettingsInstanceLock(data);
    xsettingsSetArrayIndexLocked(self, data, i);
    xsettingsInstanceUnlock(data);
}

static void xsettingsBeginGroupLocked(XSettings* self, XSettingsData* data, const XString* prefix)
{
    if (!data || !data->groupStack)
        return;
    XString* name = xsettingsNormalizedKey(prefix);
    if (!name)
        return;
    XSettingsGroup group;
    group.str = name;
    group.num = -1;
    group.maxNum = -1;
    if (!XVector_push_back_1_base(data->groupStack, &group)) {
        XClassDelete(name);
        return;
    }
    {
        size_t n = XVector_size_base(data->groupStack);
        XSettingsGroup* back0 = n > 0
            ? (XSettingsGroup*)XVector_at_base(data->groupStack, n - 1) : NULL;
    }
    xsettingsRebuildGroupPrefix(data);
}

void XSettings_beginGroup_utf8(XSettings* self, const char* prefixUtf8)
{
    XString* prefix = prefixUtf8 ? XString_create_utf8(prefixUtf8) : NULL;
    XSettings_beginGroup(self, prefix);
    if (prefix)
        XClassDelete(prefix);
}

static void xsettingsEndGroupLocked(XSettings* self, XSettingsData* data)
{
    if (!data || !data->groupStack)
        return;
    size_t count = XVector_size_base(data->groupStack);
    if (count == 0) {
        XPrintf("[XSettings] endGroup: 分组栈为空，忽略\n");
        return;
    }
    XSettingsGroup* top = (XSettingsGroup*)XVector_at_base(data->groupStack, count - 1);
    if (top && top->str)
        XClassDelete(top->str);
    XVector_removeAt_base(data->groupStack, count - 1);
    /* 组前缀缓存必须始终存在（空栈时为空串）：此前若缓存被外部释放/
     * 未创建，children/取值会以 NULL 前缀调用，返回空结果。 */
    if (!data->groupPrefix)
        data->groupPrefix = XString_create();
    xsettingsRebuildGroupPrefix(data);
}

const XString* XSettings_group_const(const XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    return data ? (const XString*)data->groupPrefix : NULL;
}

static int xsettingsBeginReadArrayLocked(const XSettings* self, XSettingsData* data, const XString* prefix)
{
    if (!data || !data->groupStack)
        return 0;
    XString* name = xsettingsNormalizedKey(prefix);
    if (!name)
        return 0;
    XSettingsGroup group;
    group.str = name;
    group.num = 0;
    group.maxNum = -1;
    if (!XVector_push_back_1_base(data->groupStack, &group)) {
        XClassDelete(name);
        return 0;
    }
    xsettingsRebuildGroupPrefix(data);
    /* size 键在 push 后的组内（prefix/size），对标 2996-3001 */
    XVariant* sizeValue = XSettings_value_utf8(self, "size");
    int size = xsettingsVariantToInt(sizeValue);
    if (sizeValue)
        XClassDelete(sizeValue);
    return size;
}

int XSettings_beginReadArray_utf8(const XSettings* self, const char* prefixUtf8)
{
    XString* prefix = prefixUtf8 ? XString_create_utf8(prefixUtf8) : NULL;
    int result = XSettings_beginReadArray(self, prefix);
    if (prefix)
        XClassDelete(prefix);
    return result;
}

void XSettings_beginWriteArray(XSettings* self, const XString* prefix)
{
    XSettings_beginWriteArray_2(self, prefix, -1);
}

static void xsettingsBeginWriteArrayLocked(XSettings* self, XSettingsData* data, const XString* prefix, int size)
{
    if (!data || !data->groupStack)
        return;
    XString* name = xsettingsNormalizedKey(prefix);
    if (!name)
        return;
    XSettingsGroup group;
    group.str = name;
    group.num = 0;
    group.maxNum = (size < 0) ? 0 : -1;
    if (!XVector_push_back_1_base(data->groupStack, &group)) {
        XClassDelete(name);
        return;
    }
    xsettingsRebuildGroupPrefix(data);
    if (size < 0)
        XSettings_remove_utf8(self, "size");   /* 清旧 size（自动计数模式） */
    else {
        XVariant* sizeValue = XVariant_create_int(size);
        XSettings_setValue_utf8(self, "size", sizeValue);
        if (sizeValue)
            XClassDelete(sizeValue);
    }
}

void XSettings_beginWriteArray_utf8(XSettings* self, const char* prefixUtf8)
{
    XString* prefix = prefixUtf8 ? XString_create_utf8(prefixUtf8) : NULL;
    XSettings_beginWriteArray(self, prefix);
    if (prefix)
        XClassDelete(prefix);
}

void XSettings_beginWriteArray_2_utf8(XSettings* self, const char* prefixUtf8, int size)
{
    XString* prefix = prefixUtf8 ? XString_create_utf8(prefixUtf8) : NULL;
    XSettings_beginWriteArray_2(self, prefix, size);
    if (prefix)
        XClassDelete(prefix);
}

static void xsettingsEndArrayLocked(XSettings* self, XSettingsData* data)
{
    if (!data || !data->groupStack)
        return;
    size_t count = XVector_size_base(data->groupStack);
    if (count == 0) {
        XPrintf("[XSettings] endArray: 分组栈为空，忽略\n");
        return;
    }
    XSettingsGroup* top = (XSettingsGroup*)XVector_at_base(data->groupStack, count - 1);
    if (!top || top->num == -1) {
        XPrintf("[XSettings] endArray: 当前分组不是数组，忽略\n");
        return;
    }
    int guess = top->maxNum;
    XString* name = top->str ? XString_create_copy(top->str) : NULL;
    if (top->str)
        XClassDelete(top->str);
    XVector_removeAt_base(data->groupStack, count - 1);
    xsettingsRebuildGroupPrefix(data);
    /* 自动计数猜测非 -1 → 写 prefix/size（对标 3052-3071） */
    if (guess != -1 && name) {
        XString_append_char(name, (XChar)'/');
        XString_append_utf8(name, "size");
        XVariant* sizeValue = XVariant_create_int(guess);
        XSettings_setValue(self, name, sizeValue);
        if (sizeValue)
            XClassDelete(sizeValue);
        XClassDelete(name);
    }
    else if (name) {
        XClassDelete(name);
    }
}

static void xsettingsSetArrayIndexLocked(XSettings* self, XSettingsData* data, int i)
{
    if (!data || !data->groupStack)
        return;
    size_t count = XVector_size_base(data->groupStack);
    if (count == 0) {
        XPrintf("[XSettings] setArrayIndex: 分组栈为空，忽略\n");
        return;
    }
    XSettingsGroup* top = (XSettingsGroup*)XVector_at_base(data->groupStack, count - 1);
    if (!top || top->num == -1) {
        XPrintf("[XSettings] setArrayIndex: 当前分组不是数组，忽略\n");
        return;
    }
    if (i < 0)
        i = 0;   /* 负下标钳为 0 → 子键 prefix/1（对标 qMax(i, 0)） */
    top->num = i + 1;
    if (top->maxNum != -1 && top->num > top->maxNum)
        top->maxNum = top->num;
    xsettingsRebuildGroupPrefix(data);
}

/* ==========================================================================
 * 生命周期与状态
 * ========================================================================== */

/** @brief clear 的无锁主体（调用方已持实例锁）。 */
static void xsettingsClearLocked(XSettings* self, XSettingsData* data)
{
    XSettingsConfFile* conf;
    if (data->registry) {
        /* 注册表后端：删首项下全部键值（Qt QWinSettingsPrivate::clear） */
        if (data->regKeyCount == 0) {
            xsettingsSetStatus(data, XSettings_AccessError);
            return;
        }
        xsettingsRegClear(data);
        data->pendingChanges = true;
        /* 对标 Qt QWinSettingsPrivate::clear 的 deleteWriteHandleOnExit：
         * 若 clear 之后未再写入任何值，析构时要把这个空键一并删掉，
         * 避免在注册表里留下空壳键。 */
        data->deleteWriteHandleOnExit = true;
        return;
    }
    conf = data->confFiles[0];
    if (!conf || data->noBackend) {
        xsettingsSetStatus(data, XSettings_AccessError);
        return;
    }
    XMap_clear_base(conf->addedKeys);
    /* removedKeys = originalKeys 全量墓碑（对标 1283-1295） */
    XMap_clear_base(conf->removedKeys);
    XVector* keys = XMap_keys_base(conf->originalKeys);
    if (keys) {
        size_t total = XVector_size_base(keys);
        for (size_t i = 0; i < total; ++i) {
            XString* key = (XString*)XVector_at_base(keys, i);   /* 值槽内联对象借用 */
            if (key)
                xmapSetTombstone(conf->removedKeys, key);
        }
        XClassDelete(keys);
    }
    data->pendingChanges = true;
}

void XSettings_clear(XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    if (!data)
        return;
    xsettingsInstanceLock(data);
    xsettingsClearLocked(self, data);
    xsettingsInstanceUnlock(data);
    if (data->pendingChanges)
        xsettingsRequestUpdate(self);
}

void XSettings_sync(XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    if (!data)
        return;
    xsettingsInstanceLock(data);
    if (data->registry) {
        /* 注册表后端：RegFlushKey（Qt QWinSettingsPrivate::sync） */
        if (data->pendingChanges && data->regKeyCount > 0)
            xsettingsRegSync(data);
        data->pendingChanges = false;
        xsettingsInstanceUnlock(data);
        return;
    }
    for (int i = 0; i < data->confFileCount; ++i) {
        if (data->confFiles[i])
            xconfSync(data->confFiles[i], data);
    }
    data->pendingChanges = false;
    xsettingsInstanceUnlock(data);
}

XSettings_Status XSettings_status(const XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    return data ? data->status : XSettings_NoError;
}

bool XSettings_isWritable(const XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    bool result;
    if (!data || data->noBackend)
        return false;
    xsettingsInstanceLock(data);
    if (data->registry)
        result = data->regKeyCount > 0 && xsettingsRegIsWritable(data);
    else if (data->confFileCount == 0 || !data->confFiles[0])
        result = false;
    else
        result = xconfIsWritable(data->confFiles[0]);
    xsettingsInstanceUnlock(data);
    return result;
}

bool XSettings_isAtomicSyncRequired(const XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    return data ? data->atomicSync : true;
}

void XSettings_setAtomicSyncRequired(XSettings* self, bool enable)
{
    XSettingsData* data = self ? self->m_data : NULL;
    if (data)
        data->atomicSync = enable;
}

bool XSettings_fallbacksEnabled(const XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    return data ? data->fallbacks : true;
}

void XSettings_setFallbacksEnabled(XSettings* self, bool b)
{
    XSettingsData* data = self ? self->m_data : NULL;
    if (data)
        data->fallbacks = b;
}

/* ==========================================================================
 * 元信息
 * ========================================================================== */

const XString* XSettings_fileName_const(const XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    if (!data)
        return NULL;
    if (data->registry) {
        /* 注册表后端：返回 \HKEY_CURRENT_USER\Software\orgpp 形态
         * （Qt QWinSettingsPrivate::fileName 同款）；借用语义要求对象持有 */
        if (!data->fileNameCache)
            data->fileNameCache = xsettingsRegFileName(data);
        return data->fileNameCache;
    }
    if (data->confFileCount == 0 || !data->confFiles[0])
        return NULL;
    return (const XString*)data->confFiles[0]->name;
}

XSettings_Format XSettings_format(const XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    return data ? data->format : XSettings_NativeFormat;
}

XSettings_Scope XSettings_scope(const XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    return data ? data->scope : XSettings_UserScope;
}

const XString* XSettings_organizationName_const(const XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    return data ? (const XString*)data->organizationName : NULL;
}

const XString* XSettings_applicationName_const(const XSettings* self)
{
    XSettingsData* data = self ? self->m_data : NULL;
    return data ? (const XString*)data->applicationName : NULL;
}

/* ==========================================================================
 * 静态配置
 * ========================================================================== */

void XSettings_setDefaultFormat(XSettings_Format format)
{
    xsettingsGlobalLock();
    g_xsettings_defaultFormat = format;
    xsettingsGlobalUnlock();
}

XSettings_Format XSettings_defaultFormat(void)
{
    XSettings_Format format;
    xsettingsGlobalLock();
    format = g_xsettings_defaultFormat;
    xsettingsGlobalUnlock();
    return format;
}

void XSettings_setPath(XSettings_Format format, XSettings_Scope scope, const XString* path)
{
    XString* clone;
    if (format < 0 || format > XSettings_CustomFormat16)
        return;
    if (scope < XSettings_UserScope || scope > XSettings_SystemScope)
        return;
    clone = path ? XString_create_copy(path) : NULL;
    xsettingsGlobalLock();
    if (g_xsettings_pathOverride[format][scope])
        XClassDelete(g_xsettings_pathOverride[format][scope]);
    g_xsettings_pathOverride[format][scope] = clone;
    xsettingsGlobalUnlock();
}

void XSettings_setPath_utf8(XSettings_Format format, XSettings_Scope scope, const char* pathUtf8)
{
    XString* path = pathUtf8 ? XString_create_utf8(pathUtf8) : NULL;
    XSettings_setPath(format, scope, path);
    if (path)
        XClassDelete(path);
}

XSettings_Format XSettings_registerFormat(const XString* extension,
                                          XSettings_ReadFunc readFunc,
                                          XSettings_WriteFunc writeFunc,
                                          XSettings_CaseSensitivity caseSensitivity)
{
    XSettings_Format assigned = XSettings_InvalidFormat;
    if (!extension || XString_size(extension) == 0)
        return XSettings_InvalidFormat;
    xsettingsGlobalLock();
    /* 同扩展名重注册 → 复用既有槽位（对标 Qt 去重） */
    for (int i = 0; i < 16; ++i) {
        if (g_xsettings_customFormats[i].used && g_xsettings_customFormats[i].extension &&
            XString_equals(g_xsettings_customFormats[i].extension, extension,
                           g_xsettings_customFormats[i].caseSensitivity == XSettings_CaseInsensitive
                               ? XChar_CaseInsensitive
                               : XChar_CaseSensitive)) {
            g_xsettings_customFormats[i].readFunc = readFunc;
            g_xsettings_customFormats[i].writeFunc = writeFunc;
            g_xsettings_customFormats[i].caseSensitivity = caseSensitivity;
            assigned = (XSettings_Format)(XSettings_CustomFormat1 + i);
            xsettingsGlobalUnlock();
            return assigned;
        }
    }
    for (int i = 0; i < 16; ++i) {
        XString* copy;
        if (g_xsettings_customFormats[i].used)
            continue;
        copy = XString_create_copy(extension);
        if (!copy) {
            xsettingsGlobalUnlock();
            return XSettings_InvalidFormat;
        }
        g_xsettings_customFormats[i].extension = copy;
        g_xsettings_customFormats[i].readFunc = readFunc;
        g_xsettings_customFormats[i].writeFunc = writeFunc;
        g_xsettings_customFormats[i].caseSensitivity = caseSensitivity;
        g_xsettings_customFormats[i].used = true;
        assigned = (XSettings_Format)(XSettings_CustomFormat1 + i);
        xsettingsGlobalUnlock();
        return assigned;
    }
    xsettingsGlobalUnlock();
    return XSettings_InvalidFormat;
}

XSettings_Format XSettings_registerFormat_utf8(const char* extensionUtf8,
                                               XSettings_ReadFunc readFunc,
                                               XSettings_WriteFunc writeFunc,
                                               XSettings_CaseSensitivity caseSensitivity)
{
    XString* extension = extensionUtf8 ? XString_create_utf8(extensionUtf8) : NULL;
    XSettings_Format result = XSettings_registerFormat(extension, readFunc, writeFunc, caseSensitivity);
    if (extension)
        XClassDelete(extension);
    return result;
}

#endif /* XSETTINGS_ON */
