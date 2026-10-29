/**
 * @file       XIoChannel.c
 * @brief      工业通道抽象基类实现。
 * @details    只依赖 XinYueC 抽象层；瘦基类无自有虚槽，仅承载 tag/valid/
 *             lastError 三件公共状态与 errorOccurred 信号。
 */
#include "XIoChannel.h"
#include "XIoChannel_Protected.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

#include <string.h>
#include "XMemory.h"

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XIoChannel_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XIoChannel)
    XVTABLE_INHERIT_XCLASS(XObject);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XIoChannel_init(XIoChannel* point)
{
    if (!point) return;
    memset(point, 0, sizeof(XIoChannel));
    XObject_init((XObject*)point);
    XClassSetVtable(point, XIoChannel);
    point->m_tag = NULL;
    point->m_valid = 0;
    point->m_reserved = 0;
    point->m_lastError = XIoChannelError_None;
}

XIoChannel* XIoChannel_create(void)
{
    XIoChannel* point = (XIoChannel*)XMemory_calloc(1, sizeof(XIoChannel), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!point) return NULL;
    XIoChannel_init(point);
    Set_Class_IsHeap(point, true);
    return point;
}

/* ==================== 功能函数 ==================== */

void XIoChannel_setTag(XIoChannel* point, const char* tag)
{
    if (!point) return;
    point->m_tag = tag;
}

const char* XIoChannel_tag_const(const XIoChannel* point)
{
    if (!point) return NULL;
    return point->m_tag;
}

bool XIoChannel_isValid(const XIoChannel* point)
{
    if (!point) return false;
    return point->m_valid ? true : false;
}

int XIoChannel_lastError(const XIoChannel* point)
{
    if (!point) return XIoChannelError_InvalidArgument;
    return point->m_lastError;
}

/* ==================== 信号 ==================== */

void* XIoChannel_errorOccurred_signal(XIoChannel* point, int error)
{
    XEmitSignal(point, XIoChannel_errorOccurred_signal,
                XVarList_Create(XVar(int, error)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

/* ==================== 保护接口实现 ==================== */

void XIoChannel_setValid(XIoChannel* point, bool valid)
{
    if (!point) return;
    point->m_valid = valid ? 1u : 0u;
}

void XIoChannel_setError(XIoChannel* point, int error)
{
    if (!point) return;
    point->m_lastError = error;
    if (error != XIoChannelError_None) {
        point->m_valid = 0;
        XIoChannel_errorOccurred_signal(point, error);
    }
}

void XIoChannel_clearError(XIoChannel* point)
{
    if (!point) return;
    point->m_lastError = XIoChannelError_None;
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */
