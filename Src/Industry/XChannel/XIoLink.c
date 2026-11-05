/**
 * @file       XIoLink.c
 * @brief      链路连接抽象基类实现（虚槽缺省实现）。
 * @details    缺省实现刻意保守：
 *             - BindRole/ReadRole/WriteRole 返回 false（未实现）；
 *             - IsAsync 返回 true（未知链路按「需要等待落定」处理，宁可等超时
 *               也不要误判一个其实还没写完的动作已完成）；
 *             - Start 返回 true、Stop 空（无资源可管）。
 */
#include "XIoLink.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

#include <string.h>

/* ==================== 缺省虚槽实现 ==================== */

/** @brief BindRole 缺省：未实现。 */
static bool VXIoLink_bindRole(XIoLink* self, uint16_t role, uint32_t nativeAddr,
                              uint32_t aux)
{
    (void)self;
    (void)role;
    (void)nativeAddr;
    (void)aux;
    return false;
}

/** @brief ReadRole 缺省：未实现。 */
static bool VXIoLink_readRole(XIoLink* self, uint16_t role, int32_t* outRaw)
{
    (void)self;
    (void)role;
    (void)outRaw;
    return false;
}

/** @brief WriteRole 缺省：未实现。 */
static bool VXIoLink_writeRole(XIoLink* self, uint16_t role, int32_t raw)
{
    (void)self;
    (void)role;
    (void)raw;
    return false;
}

/**
 * @brief IsAsync 缺省：true。
 * @note 保守取真：未知链路一律按「写要等落定」处理，让上层 arm 动作监督
 *       定时器。宁可多等一次超时，也不要把未完成的写当成已完成。
 */
static bool VXIoLink_isAsync(const XIoLink* self)
{
    (void)self;
    return true;
}

/** @brief Start 缺省：无需启动，直接成功。 */
static bool VXIoLink_start(XIoLink* self)
{
    (void)self;
    return true;
}

/** @brief Stop 缺省：无资源可释放。 */
static void VXIoLink_stop(XIoLink* self)
{
    (void)self;
}

/**
 * @brief RoleFresh 缺省：true。
 * @note 保守取真：未知链路不擅自判陈旧。宁可让上层多用一次可能过期的值，
 *       也不要把"我不了解这条链路"表达成"数据已失效"——后者会让正常
 *       设备被误判离线。
 */
static bool VXIoLink_roleFresh(const XIoLink* self, uint16_t role)
{
    (void)self;
    (void)role;
    return true;
}

/** @brief 析构：仅走父类链（子类必须在自己的 deinit 里释放自己的资源）。 */
static void VXIoLink_deinit(XIoLink* self)
{
    if (!self) return;
    self->m_group = NULL;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

/* ==================== 类初始化 ==================== */

XVtable* XIoLink_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XIoLink)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_BindRole, VXIoLink_bindRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_ReadRole, VXIoLink_readRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_WriteRole, VXIoLink_writeRole);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_IsAsync, VXIoLink_isAsync);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_Start, VXIoLink_start);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_Stop, VXIoLink_stop);
    XVTABLE_OVERLOAD_DEFAULT(EXIoLink_RoleFresh, VXIoLink_roleFresh);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIoLink_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XIoLink);
    return XVTABLE_DEFAULT;
}

void XIoLink_init(XIoLink* link)
{
    if (!link) return;
    XObject_init(&link->m_class);
    XClassGetVtable(link) = XIoLink_class_init();
    link->m_group = NULL;
}

/* ==================== 虚槽直调入口 ==================== */

bool XIoLink_bindRole_base(XIoLink* link, uint16_t role, uint32_t nativeAddr,
                           uint32_t aux)
{
    if (ISNULL(link, "XIoLink") || ISNULL(XClassGetVtable(link), "Vtable"))
        return false;
    return XClassGetVirtualFunc(link, EXIoLink_BindRole,
        bool (*)(XIoLink*, uint16_t, uint32_t, uint32_t))(link, role, nativeAddr, aux);
}

bool XIoLink_readRole_base(XIoLink* link, uint16_t role, int32_t* outRaw)
{
    if (ISNULL(link, "XIoLink") || ISNULL(XClassGetVtable(link), "Vtable"))
        return false;
    return XClassGetVirtualFunc(link, EXIoLink_ReadRole,
        bool (*)(XIoLink*, uint16_t, int32_t*))(link, role, outRaw);
}

bool XIoLink_writeRole_base(XIoLink* link, uint16_t role, int32_t raw)
{
    if (ISNULL(link, "XIoLink") || ISNULL(XClassGetVtable(link), "Vtable"))
        return false;
    return XClassGetVirtualFunc(link, EXIoLink_WriteRole,
        bool (*)(XIoLink*, uint16_t, int32_t))(link, role, raw);
}

bool XIoLink_isAsync_base(const XIoLink* link)
{
    if (ISNULL(link, "XIoLink") || ISNULL(XClassGetVtable(link), "Vtable"))
        return true;
    return XClassGetVirtualFunc(link, EXIoLink_IsAsync,
        bool (*)(const XIoLink*))(link);
}

bool XIoLink_roleFresh_base(const XIoLink* link, uint16_t role)
{
    if (ISNULL(link, "XIoLink") || ISNULL(XClassGetVtable(link), "Vtable"))
        return true;
    return XClassGetVirtualFunc(link, EXIoLink_RoleFresh,
        bool (*)(const XIoLink*, uint16_t))(link, role);
}

bool XIoLink_start_base(XIoLink* link)
{
    if (ISNULL(link, "XIoLink") || ISNULL(XClassGetVtable(link), "Vtable"))
        return false;
    return XClassGetVirtualFunc(link, EXIoLink_Start,
        bool (*)(XIoLink*))(link);
}

void XIoLink_stop_base(XIoLink* link)
{
    if (!link || !XClassGetVtable(link)) return;
    XClassGetVirtualFunc(link, EXIoLink_Stop, void (*)(XIoLink*))(link);
}

/* ==================== 组绑定 ==================== */

void XIoLink_setGroup(XIoLink* link, void* group)
{
    if (!link) return;
    link->m_group = group;
}

void* XIoLink_group(const XIoLink* link)
{
    return link ? link->m_group : NULL;
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */
