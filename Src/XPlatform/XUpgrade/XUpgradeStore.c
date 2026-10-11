/**
 * @file       XUpgradeStore.c
 * @brief      升级存储后端抽象基类实现：虚表初始化 + NotSupported 默认槽 + *_base 分派。
 * @details    XUpgradeStore.h 契约的实现侧（模式同 Src/XPlatform/XSql/XSqlDriver.c）：
 *             class_init 按"标准五步"建共享虚表（继承 XObject 后追加 8 个业务槽，
 *             Deinit 槽重载为基类析构）；全部默认槽返回 XUpgradeError_NotSupported
 *           /false（契约规定：抽象基类对象不可直接使用，产品代码应使用
 *             XUpgradeStoreDir_create_ex 等具体后端）。各 *_base 分派函数经对象
 *             虚表取槽调用（XClassGetVirtualFunc），对象为 NULL 或未绑表时按
 *             契约返回 NotSupported/false，不产生任何副作用。
 */
#include "XUpgradeStore.h"

#include "XObject.h"
#include "XMemory.h"

#include <string.h>

#if XUPGRADE_ON

/* ========================================================================== */
/*                     默认虚函数实现（static，仅供 class_init 注册）           */
/* ========================================================================== */

static XUpgradeError VXUpgradeStore_stageBegin(XUpgradeStore* self, const char* componentId,
                                               const char* version, uint64_t expectedSize,
                                               const char** errText);
static XUpgradeError VXUpgradeStore_stageWrite(XUpgradeStore* self, const void* data, uint32_t len);
static XUpgradeError VXUpgradeStore_stageEnd(XUpgradeStore* self, const char** errText);
static XUpgradeError VXUpgradeStore_activate(XUpgradeStore* self, const char* componentId,
                                             const char* version, const char** errText);
static bool VXUpgradeStore_currentVersion(XUpgradeStore* self, const char* componentId,
                                          char* out, uint32_t cap);
static XUpgradeError VXUpgradeStore_rollback(XUpgradeStore* self, const char* componentId,
                                             const char* version, const char** errText);
static XUpgradeError VXUpgradeStore_listVersions(XUpgradeStore* self, const char* componentId,
                                                 XUpgradeStoreEmitVersion emit, void* userData);
static XUpgradeError VXUpgradeStore_confirmBoot(XUpgradeStore* self, const char* componentId,
                                                const char* version, const char** errText);
static void VXUpgradeStore_deinit(XUpgradeStore* store);

XVtable* XUpgradeStore_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XUpgradeStore)
    XVTABLE_INHERIT_XCLASS(XObject);
    {
        void* table[] = {
            VXUpgradeStore_stageBegin, VXUpgradeStore_stageWrite, VXUpgradeStore_stageEnd,
            VXUpgradeStore_activate, VXUpgradeStore_currentVersion, VXUpgradeStore_rollback,
            VXUpgradeStore_listVersions, VXUpgradeStore_confirmBoot
        };
        XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    }
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXUpgradeStore_deinit);
    return XVTABLE_DEFAULT;
}

void XUpgradeStore_init(XUpgradeStore* store)
{
    if (!store) return;
    memset(((unsigned char*)store) + sizeof(XObject), 0, sizeof(*store) - sizeof(XObject));
    XObject_init((XObject*)store);
    XClassSetVtable(store, XUpgradeStore);
}

XUpgradeStore* XUpgradeStore_create_ex(XMemoryType memory)
{
    XUpgradeStore* store = (XUpgradeStore*)XMemory_malloc(sizeof(XUpgradeStore), memory);
    if (!store) return NULL;
    memset(store, 0, sizeof(*store));
    XUpgradeStore_init(store);
    Set_Class_Memory(store, memory); Set_Class_IsHeap(store, true);
    return store;
}

/* ========================================================================== */
/*                        虚函数分派入口（*_base）                             */
/* ========================================================================== */

XUpgradeError XUpgradeStore_stageBegin_base(XUpgradeStore* self, const char* componentId,
                                            const char* version, uint64_t expectedSize,
                                            const char** errText)
{
    if (errText) *errText = NULL;
    if (!self || XClassIsVtableNull(self)) return XUpgradeError_NotSupported;
    return XClassGetVirtualFunc(self, EXUpgradeStore_StageBegin,
                                XUpgradeError(*)(XUpgradeStore*, const char*, const char*,
                                                 uint64_t, const char**))
           (self, componentId, version, expectedSize, errText);
}

XUpgradeError XUpgradeStore_stageWrite_base(XUpgradeStore* self, const void* data, uint32_t len)
{
    if (!self || XClassIsVtableNull(self)) return XUpgradeError_NotSupported;
    return XClassGetVirtualFunc(self, EXUpgradeStore_StageWrite,
                                XUpgradeError(*)(XUpgradeStore*, const void*, uint32_t))
           (self, data, len);
}

XUpgradeError XUpgradeStore_stageEnd_base(XUpgradeStore* self, const char** errText)
{
    if (errText) *errText = NULL;
    if (!self || XClassIsVtableNull(self)) return XUpgradeError_NotSupported;
    return XClassGetVirtualFunc(self, EXUpgradeStore_StageEnd,
                                XUpgradeError(*)(XUpgradeStore*, const char**))
           (self, errText);
}

XUpgradeError XUpgradeStore_activate_base(XUpgradeStore* self, const char* componentId,
                                          const char* version, const char** errText)
{
    if (errText) *errText = NULL;
    if (!self || XClassIsVtableNull(self)) return XUpgradeError_NotSupported;
    return XClassGetVirtualFunc(self, EXUpgradeStore_Activate,
                                XUpgradeError(*)(XUpgradeStore*, const char*, const char*,
                                                 const char**))
           (self, componentId, version, errText);
}

bool XUpgradeStore_currentVersion_base(XUpgradeStore* self, const char* componentId,
                                       char* out, uint32_t cap)
{
    if (!self || XClassIsVtableNull(self)) return false;
    return XClassGetVirtualFunc(self, EXUpgradeStore_CurrentVersion,
                                bool(*)(XUpgradeStore*, const char*, char*, uint32_t))
           (self, componentId, out, cap);
}

XUpgradeError XUpgradeStore_rollback_base(XUpgradeStore* self, const char* componentId,
                                          const char* version, const char** errText)
{
    if (errText) *errText = NULL;
    if (!self || XClassIsVtableNull(self)) return XUpgradeError_NotSupported;
    return XClassGetVirtualFunc(self, EXUpgradeStore_Rollback,
                                XUpgradeError(*)(XUpgradeStore*, const char*, const char*,
                                                 const char**))
           (self, componentId, version, errText);
}

XUpgradeError XUpgradeStore_listVersions_base(XUpgradeStore* self, const char* componentId,
                                              XUpgradeStoreEmitVersion emit, void* userData)
{
    if (!self || XClassIsVtableNull(self)) return XUpgradeError_NotSupported;
    return XClassGetVirtualFunc(self, EXUpgradeStore_ListVersions,
                                XUpgradeError(*)(XUpgradeStore*, const char*,
                                                 XUpgradeStoreEmitVersion, void*))
           (self, componentId, emit, userData);
}

XUpgradeError XUpgradeStore_confirmBoot_base(XUpgradeStore* self, const char* componentId,
                                             const char* version, const char** errText)
{
    if (errText) *errText = NULL;
    if (!self || XClassIsVtableNull(self)) return XUpgradeError_NotSupported;
    return XClassGetVirtualFunc(self, EXUpgradeStore_ConfirmBoot,
                                XUpgradeError(*)(XUpgradeStore*, const char*, const char*,
                                                 const char**))
           (self, componentId, version, errText);
}

/* ========================================================================== */
/*                        默认槽位（抽象基类：NotSupported）                    */
/* ========================================================================== */

static XUpgradeError VXUpgradeStore_stageBegin(XUpgradeStore* self, const char* componentId,
                                               const char* version, uint64_t expectedSize,
                                               const char** errText)
{
    (void)self; (void)componentId; (void)version; (void)expectedSize;
    if (errText) *errText = "no concrete storage backend";
    return XUpgradeError_NotSupported;
}

static XUpgradeError VXUpgradeStore_stageWrite(XUpgradeStore* self, const void* data, uint32_t len)
{
    (void)self; (void)data; (void)len;
    return XUpgradeError_NotSupported;
}

static XUpgradeError VXUpgradeStore_stageEnd(XUpgradeStore* self, const char** errText)
{
    (void)self;
    if (errText) *errText = "no concrete storage backend";
    return XUpgradeError_NotSupported;
}

static XUpgradeError VXUpgradeStore_activate(XUpgradeStore* self, const char* componentId,
                                             const char* version, const char** errText)
{
    (void)self; (void)componentId; (void)version;
    if (errText) *errText = "no concrete storage backend";
    return XUpgradeError_NotSupported;
}

static bool VXUpgradeStore_currentVersion(XUpgradeStore* self, const char* componentId,
                                          char* out, uint32_t cap)
{
    (void)self; (void)componentId; (void)out; (void)cap;
    return false;
}

static XUpgradeError VXUpgradeStore_rollback(XUpgradeStore* self, const char* componentId,
                                             const char* version, const char** errText)
{
    (void)self; (void)componentId; (void)version;
    if (errText) *errText = "no concrete storage backend";
    return XUpgradeError_NotSupported;
}

static XUpgradeError VXUpgradeStore_listVersions(XUpgradeStore* self, const char* componentId,
                                                 XUpgradeStoreEmitVersion emit, void* userData)
{
    (void)self; (void)componentId; (void)emit; (void)userData;
    return XUpgradeError_NotSupported;
}

static XUpgradeError VXUpgradeStore_confirmBoot(XUpgradeStore* self, const char* componentId,
                                                const char* version, const char** errText)
{
    /* 基类默认=幂等 Ok（契约口径：无独立确认步的后端"确认"即无操作成功；
     * A/B 后端在派生类重载为 §9.4 双写）。 */
    (void)self; (void)componentId; (void)version;
    if (errText) *errText = NULL;
    return XUpgradeError_Ok;
}

static void VXUpgradeStore_deinit(XUpgradeStore* store)
{
    if (!store) return;
    /* 抽象基类不持有后端状态，仅需析构 XObject 基类部分（信号槽/子对象表等）。 */
    XClass_Deinit_Parent(XObject, store);
}

#endif /* XUPGRADE_ON */
