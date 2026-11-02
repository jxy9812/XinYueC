/**
 * @file       XVfdBind.c
 * @brief      绑定层实现（按 role / paramId 查绑定条目）。
 * @details    只在装配期使用：一次装配把整张表过一遍，运行期数据路径
 *             走 XIoLink 的 O(1) 映射，不经过本文件。
 */
#include "XVfdBind.h"

#if XVFD_ON

#include <stddef.h>

bool XVfdBind_findPoint(const XVfdBindTable* bind, uint16_t role,
                        XVfdPointBinding* outBinding)
{
    int i;
    if (!bind || !outBinding || !bind->m_points) return false;
    for (i = 0; i < bind->m_pointCount; ++i) {
        if (bind->m_points[i].m_role != role) continue;
        *outBinding = bind->m_points[i];
        return true;
    }
    return false;
}

bool XVfdBind_findParam(const XVfdBindTable* bind, uint16_t paramId,
                        XVfdParamBinding* outBinding)
{
    int i;
    if (!bind || !outBinding || !bind->m_params) return false;
    for (i = 0; i < bind->m_paramCount; ++i) {
        if (bind->m_params[i].m_paramId != paramId) continue;
        *outBinding = bind->m_params[i];
        return true;
    }
    return false;
}

#endif /* XVFD_ON */
