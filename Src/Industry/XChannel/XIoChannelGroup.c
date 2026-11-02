/**
 * @file       XIoChannelGroup.c
 * @brief      工业通道组实现（role 作下标的 O(1) 点表 + 链路挂接）。
 * @details    [2026-11-02 XChannel 重写] 本文件内**没有任何按点查找的循环**：
 *             点的身份是 m_role，存放与读取都是 m_points[role] 一次数组取址。
 *             唯一的遍历在析构里（生命周期终点的一次性开销，不在数据路径上）。
 *
 *             数据流：链路子类数据到达 → feedRole(role, raw) → 标度换算
 *             → 存值 → 发射 inputChanged(role, eng)。写落定反向同理。
 */
#include "XIoChannelGroup.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

#include "XMemory.h"
#include "XEvent.h"
#include <string.h>

/* ==================== 内部实现 ==================== */

/**
 * @brief 按 role 取点（内部；不做类型校验）。
 * @param self 目标通道组；可为 NULL。
 * @param role 语义槽号。
 * @return 点指针；越界或无点返回 NULL。
 */
static XIoPoint* groupPoint(const XIoChannelGroup* self, uint16_t role)
{
    if (!self || role >= (uint16_t)XIOCHANNELGROUP_ROLE_CAP) return NULL;
    return self->m_points[role];
}

/**
 * @brief 析构：按 autoDelete 释放全部点，然后走父类链。
 * @note 这是全文件唯一的遍历，且只在对象生命周期终点发生一次。
 */
static void VXIoChannelGroup_deinit(XIoChannelGroup* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < (int)XIOCHANNELGROUP_ROLE_CAP; ++i) {
        XIoPoint* pt = self->m_points[i];
        bool autoDel;
        if (!pt) continue;
        /* 先读后清：clear 之后就读不到了（旧实现曾有这个顺序坑）。 */
        autoDel = (pt->m_flags.bits.m_autoDelete != 0);
        self->m_points[i] = NULL;
        pt->m_flags.bits.m_autoDelete = 0;
        if (autoDel) {
            XClassDelete((XClass*)pt);
        }
    }
    self->m_link = NULL;
    self->m_pointCount = 0;
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

/* ==================== 类初始化 ==================== */

XVtable* XIoChannelGroup_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XIoChannelGroup)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIoChannelGroup_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XIoChannelGroup);
    return XVTABLE_DEFAULT;
}

void XIoChannelGroup_init(XIoChannelGroup* obj)
{
    if (!obj) return;
    XObject_init(&obj->m_class);
    XClassGetVtable(obj) = XIoChannelGroup_class_init();
    memset(obj->m_points, 0, sizeof(obj->m_points));
    obj->m_link = NULL;
    obj->m_pointCount = 0;
}

XIoChannelGroup* XIoChannelGroup_create(void)
{
    XIoChannelGroup* obj = (XIoChannelGroup*)XMemory_malloc(
        sizeof(XIoChannelGroup), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!obj) return NULL;
    XIoChannelGroup_init(obj);
    Set_Class_Memory(obj, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ==================== 链路挂接 ==================== */

bool XIoChannelGroup_setLink(XIoChannelGroup* self, XIoLink* link)
{
    if (!self) return false;
    if (self->m_link && self->m_link != link) {
        /* 解绑旧链路：只清反向指针，不调 stop（生命周期归调用方）。 */
        XIoLink_setGroup(self->m_link, NULL);
    }
    self->m_link = link;
    if (link) XIoLink_setGroup(link, self);
    return true;
}

XIoLink* XIoChannelGroup_link(const XIoChannelGroup* self)
{
    return self ? self->m_link : NULL;
}

/* ==================== 点管理 ==================== */

bool XIoChannelGroup_addPoint(XIoChannelGroup* self, XIoPoint* point,
                              bool autoDelete)
{
    uint16_t role;
    if (!self || !point) return false;
    role = point->m_role;
    if (role >= (uint16_t)XIOCHANNELGROUP_ROLE_CAP) return false;
    if (self->m_points[role]) return false;   /* role 位已占用：装配错误必须暴露。 */
    self->m_points[role] = point;
    point->m_flags.bits.m_autoDelete = autoDelete ? 1u : 0u;
    ++self->m_pointCount;
    return true;
}

XIoPoint* XIoChannelGroup_takePoint(XIoChannelGroup* self, XIoPoint* point)
{
    uint16_t role;
    if (!self || !point) return NULL;
    role = point->m_role;
    if (role >= (uint16_t)XIOCHANNELGROUP_ROLE_CAP) return NULL;
    if (self->m_points[role] != point) return NULL;
    self->m_points[role] = NULL;
    point->m_flags.bits.m_autoDelete = 0;     /* 所有权移交调用方。 */
    --self->m_pointCount;
    return point;
}

bool XIoChannelGroup_removeRole(XIoChannelGroup* self, uint16_t role)
{
    XIoPoint* pt;
    bool autoDel;
    if (!self || role >= (uint16_t)XIOCHANNELGROUP_ROLE_CAP) return false;
    pt = self->m_points[role];
    if (!pt) return false;
    autoDel = (pt->m_flags.bits.m_autoDelete != 0);
    self->m_points[role] = NULL;
    --self->m_pointCount;
    if (autoDel) {
        pt->m_flags.bits.m_autoDelete = 0;
        XClassDelete((XClass*)pt);
    }
    return true;
}

int XIoChannelGroup_recordCount(const XIoChannelGroup* self)
{
    return self ? self->m_pointCount : 0;
}

XIoPoint* XIoChannelGroup_pointAt(const XIoChannelGroup* self, uint16_t role)
{
    return groupPoint(self, role);
}

bool XIoChannelGroup_contains(const XIoChannelGroup* self, const XIoPoint* point)
{
    uint16_t role;
    if (!self || !point) return false;
    role = point->m_role;
    if (role >= (uint16_t)XIOCHANNELGROUP_ROLE_CAP) return false;
    return self->m_points[role] == point;
}

/* ==================== 读写 ==================== */

bool XIoChannelGroup_readDigitalInput(XIoChannelGroup* self, uint16_t role, bool* active)
{
    const XIoPoint* pt;
    if (!self || !active) return false;
    pt = groupPoint(self, role);
    if (!pt || pt->m_type != (uint16_t)XIoPoint_DigitalInput) return false;
    if (!pt->m_flags.bits.m_valid && !pt->m_flags.bits.m_hasValue) return false;
    *active = (pt->m_flags.bits.m_valueB != 0);
    return true;
}

bool XIoChannelGroup_writeDigitalOutput(XIoChannelGroup* self, uint16_t role, bool active)
{
    XIoPoint* pt;
    if (!self) return false;
    pt = groupPoint(self, role);
    if (!pt || pt->m_type != (uint16_t)XIoPoint_DigitalOutput) return false;
    if (!self->m_link) return false;
    return XIoLink_writeRole_base(self->m_link, role, active ? 1 : 0);
}

bool XIoChannelGroup_readAnalogInput(XIoChannelGroup* self, uint16_t role, int32_t* value)
{
    const XIoPoint* pt;
    if (!self || !value) return false;
    pt = groupPoint(self, role);
    if (!pt || pt->m_type != (uint16_t)XIoPoint_AnalogInput) return false;
    if (!pt->m_flags.bits.m_valid && !pt->m_flags.bits.m_hasValue) return false;
    *value = pt->m_value;
    return true;
}

bool XIoChannelGroup_writeAnalogOutput(XIoChannelGroup* self, uint16_t role, int32_t value)
{
    XIoPoint* pt;
    uint16_t raw;
    if (!self) return false;
    pt = groupPoint(self, role);
    if (!pt || pt->m_type != (uint16_t)XIoPoint_AnalogOutput) return false;
    if (!self->m_link) return false;
    raw = XIoPoint_toRaw(pt, value);
    return XIoLink_writeRole_base(self->m_link, role, (int32_t)raw);
}

bool XIoChannelGroup_isValid(const XIoChannelGroup* self, uint16_t role)
{
    const XIoPoint* pt = groupPoint(self, role);
    return (pt && pt->m_flags.bits.m_valid) ? true : false;
}

/* ==================== 链路 → 组的数据入口 ==================== */

bool XIoChannelGroup_feedRole(XIoChannelGroup* self, uint16_t role, int32_t raw)
{
    XIoPoint* pt;
    int32_t eng;
    if (!self) return false;
    pt = groupPoint(self, role);
    if (!pt) return false;
    eng = XIoPoint_toEngineering(pt, raw);
    pt->m_flags.bits.m_valid = 1;
    if (pt->m_flags.bits.m_hasValue && pt->m_value == eng) {
        return false;                    /* 值未变：静默（不发射）。 */
    }
    pt->m_value = eng;
    pt->m_flags.bits.m_hasValue = 1;
    if (pt->m_type == (uint16_t)XIoPoint_DigitalInput ||
        pt->m_type == (uint16_t)XIoPoint_DigitalOutput) {
        pt->m_flags.bits.m_valueB = (eng != 0) ? 1u : 0u;
    }
    XIoChannelGroup_inputChanged_signal(self, role, eng);
    return true;
}

bool XIoChannelGroup_completeWrite(XIoChannelGroup* self, uint16_t role, bool ok)
{
    XIoPoint* pt;
    if (!self) return false;
    pt = groupPoint(self, role);
    if (!pt) return false;
    if (ok) pt->m_flags.bits.m_valid = 1;
    XIoChannelGroup_writeDone_signal(self, role, ok);
    return true;
}

/* ==================== 信号 ==================== */

void* XIoChannelGroup_inputChanged_signal(XIoChannelGroup* self, uint16_t role,
                                          int32_t value)
{
    XEmitSignal(self, XIoChannelGroup_inputChanged_signal,
        XVarList_Create(XVar(uint16_t, role), XVar(int32_t, value)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XIoChannelGroup_writeDone_signal(XIoChannelGroup* self, uint16_t role, bool ok)
{
    XEmitSignal(self, XIoChannelGroup_writeDone_signal,
        XVarList_Create(XVar(uint16_t, role), XVar(bool, ok)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */
