/**
 * @file       XChannelAssembly.c
 * @brief      通道装配器实现（点绑定表 → 通道组 + 链路）。
 * @details    装配是一次性启动动作，按绑定表顺序处理；运行期数据路径
 *             全程 O(1)（点按 role 下标存放，链路按地址/role 双表映射）。
 */
#include "XChannelAssembly.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

#include "XIoChannelGroup.h"
#include "XIoAnalogPoint.h"
#include "XIoLink.h"
#include "XMemory.h"
#include <string.h>

/* ==================== 内部实现 ==================== */

/**
 * @brief 按一条绑定创建并登记一个点。
 * @param group 目标通道组。
 * @param link 目标链路。
 * @param b 绑定条目。
 * @return 成功 true。
 * @note [2026-11-03 拆分] 按绑定类型选点类：AI/AO 用 XIoAnalogPoint
 *       （承载标度），DI/DO 用基类 XIoPoint（瘦身无标度载荷）。
 */
static bool assemblyMountOne(XIoChannelGroup* group, XIoLink* link,
                             const XChannelPointBinding* b)
{
    XIoPoint* pt;
    XIoPointType type;

    if (!group || !link || !b) return false;
    if (b->role >= (uint16_t)XIOCHANNELGROUP_ROLE_CAP) return false;
    /* role 位已占用：装配错误（同一 role 两个点），必须暴露。 */
    if (XIoChannelGroup_pointAt(group, b->role)) return false;

    type = (XIoPointType)b->type;
    if (type == XIoPoint_AnalogInput || type == XIoPoint_AnalogOutput) {
        XIoAnalogPoint* apt = XIoAnalogPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!apt) return false;
        pt = &apt->m_base;
    } else {
        pt = XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!pt) return false;
    }
    if (!XIoPoint_setRole(pt, b->role)) {
        XClassDelete((XClass*)pt);
        return false;
    }
    XIoPoint_setType(pt, type);
    XIoPoint_setTag(pt, b->tag);
    if (type == XIoPoint_AnalogInput || type == XIoPoint_AnalogOutput) {
        XIoAnalogPoint_setScale((XIoAnalogPoint*)pt,
                                b->scaleNum, b->scaleDen, b->offset, 0);
    }
    XIoPoint_setSigned(pt, b->signedValue != 0);

    /* 先绑链路：链路登记失败就不必进组（避免留下"在组里但链路不认识"的点）。 */
    if (!XIoLink_bindRole_base(link, b->role, (uint32_t)b->nativeAddr, b->aux)) {
        XClassDelete((XClass*)pt);
        return false;
    }
    if (!XIoChannelGroup_addPoint(group, pt, true)) {
        XClassDelete((XClass*)pt);
        return false;
    }
    return true;
}

/* ==================== 公开 API ==================== */

bool XChannelAssembly_mount(XIoChannelGroup* group, XIoLink* link,
                            const XChannelAssemblySpec* spec,
                            XChannelAssemblyResult* outResult)
{
    int i;
    int okCount = 0;
    int failRole = -1;

    if (outResult) {
        outResult->m_boundCount = 0;
        outResult->m_failedRole = -1;
    }
    if (!group || !link || !spec || !spec->m_bindings) return false;
    if (spec->m_bindingCount <= 0) return true;
    if (!XIoChannelGroup_link(group)) {
        /* 调用方忘了挂链路：明确失败，不要静默什么都不做。 */
        if (outResult) outResult->m_failedRole = spec->m_bindings[0].role;
        return false;
    }

    /* 顺序装配（一次性启动动作；不是数据路径）。 */
    for (i = 0; i < spec->m_bindingCount; ++i) {
        const XChannelPointBinding* b = &spec->m_bindings[i];
        if (!assemblyMountOne(group, link, b)) {
            failRole = (int)b->role;
            break;
        }
        ++okCount;
    }

    if (outResult) {
        outResult->m_boundCount = okCount;
        outResult->m_failedRole = failRole;
    }
    return (failRole < 0) ? true : false;
}

bool XChannelAssembly_mountOne(XIoChannelGroup* group, XIoLink* link,
                               const XChannelPointBinding* binding)
{
    if (!group || !link || !binding) return false;
    if (!XIoChannelGroup_link(group)) return false;
    return assemblyMountOne(group, link, binding);
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */
