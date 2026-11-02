/**
 * @file       XIoChannelGroup.c
 * @brief      工业通道组实现：纯「点管理 + 信号分发」。
 * @details    [2026-11-01 轻量化] 全组仅本对象是 XObject。分发两路：
 *               - 推（点子类）：推送型数据源（如 Hub 点自订阅缓存信号）
 *                 数据到达时自行调 xIoPointFeedValue（组内发射 inputChanged）；
 *               - 写落定（点子类）：经 xIoPointWriteDone 转发组 writeDone。
 *             寻址一律 XIoPoint*：登记校验 point->m_group==self（O(1)），
 *             无 id 遍历。所有权：点由调用方创建/释放；组只维护注册表。
 */
#include "XIoChannelGroup.h"

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

#include "XMemory.h"
#include "XEvent.h"
#include "XVector.h"

/* ==================== 内部实现（static，仅供本文件使用） ==================== */

/**
 * @brief 校验点已登记本组（指针即身份：m_group 反查 O(1)）。
 * @param self 目标通道组；可为 NULL。
 * @param point 待查点；可为 NULL。
 * @return 已登记 true。
 */
static bool groupOwns(const XIoChannelGroup* self, const XIoPoint* point)
{
    return (self && point && point->m_group == self) ? true : false;
}

/* ==================== 虚函数实现（static，仅供 class_init 注册） ==================== */

/**
 * @brief 组析构：清注册表。autoDelete 点一并 XClassDelete 释放；其余点为
 *        调用方所有，仅摘除登记。
 * @param self 目标通道组；可为 NULL。
 */
static void VXIoChannelGroup_deinit(XIoChannelGroup* self)
{
    size_t i;
    if (!self) return;
    if (self->m_records) {
        for (i = 0; i < XVector_size_base(self->m_records); ++i) {
            XIoPoint* p = *(XIoPoint**)XVector_at_base(self->m_records, (int64_t)i);
            if (!p) continue;
            p->m_group = NULL;
            if (p->m_flags.bits.m_autoDelete) XClassDelete((XClass*)p);
        }
        XClassDelete((XClass*)self->m_records);
        self->m_records = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

/* ==================== 虚函数表初始化（单例） ==================== */

XVtable* XIoChannelGroup_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XIoChannelGroup)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIoChannelGroup_deinit);
    return XVTABLE_DEFAULT;
}

/* ==================== 构造函数 ==================== */

void XIoChannelGroup_init(XIoChannelGroup* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(XIoChannelGroup));
    XObject_init(&obj->m_class);
    XClassSetVtable(obj, XIoChannelGroup);
    obj->m_records = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       sizeof(XIoPoint*), false);
}

XIoChannelGroup* XIoChannelGroup_create(void)
{
    XIoChannelGroup* obj =
        (XIoChannelGroup*)XMalloc_System(sizeof(XIoChannelGroup));
    if (!obj) return NULL;
    XIoChannelGroup_init(obj);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ==================== 点管理 ==================== */

bool XIoChannelGroup_addPoint(XIoChannelGroup* self, XIoPoint* point,
                              bool autoDelete)
{
    if (!self || !self->m_records || !point) return false;
    if (point->m_group != NULL) {
        /* 已登记于某组：一通道一会一组，防串信号。 */
        return false;
    }
    if (!XVector_push_back_1_base(self->m_records, &point)) return false;
    point->m_group = self;
    point->m_flags.bits.m_autoDelete = autoDelete ? 1u : 0u;
    return true;
}

XIoPoint* XIoChannelGroup_takePoint(XIoChannelGroup* self, XIoPoint* point)
{
    size_t i;
    if (!self || !self->m_records || !point || point->m_group != self) return NULL;
    for (i = 0; i < XVector_size_base(self->m_records); ++i) {
        if (*(XIoPoint**)XVector_at_base(self->m_records, (int64_t)i) == point) {
            XVector_remove_base(self->m_records, (int64_t)i, 1);
            point->m_group = NULL;
            return point;
        }
    }
    return NULL;
}

bool XIoChannelGroup_remove(XIoChannelGroup* self, XIoPoint* point)
{
    XIoPoint* taken = XIoChannelGroup_takePoint(self, point);
    if (!taken) return false;
    if (taken->m_flags.bits.m_autoDelete) {
        /* 自动内存管理：点交由组释放，摘除即销毁。
         * [所有权陷阱] 点已被销毁，**调用方手里那份指针随即悬垂**——本函数
         * 按值收参，无法置空调用方的变量（C 里要影响调用方只能传二级指针）。
         * 调用方必须自己把指针置 NULL，之后不得再解引用。 */
        XClassDelete((XClass*)taken);
    }
    return true;
}

int XIoChannelGroup_recordCount(const XIoChannelGroup* self)
{
    if (!self || !self->m_records) return 0;
    return (int)XVector_size_base(self->m_records);
}

const XIoPoint* XIoChannelGroup_pointAt(const XIoChannelGroup* self, int index)
{
    if (!self || !self->m_records || index < 0 ||
        (size_t)index >= XVector_size_base(self->m_records)) return NULL;
    return *(XIoPoint**)XVector_at_base(self->m_records, (int64_t)index);
}

bool XIoChannelGroup_contains(const XIoChannelGroup* self, const XIoPoint* point)
{
    return groupOwns(self, point);
}

/* ==================== 读写 ==================== */

bool XIoChannelGroup_readDigitalInput(XIoChannelGroup* self, XIoPoint* point, bool* active)
{
    if (!groupOwns(self, point) ||
        point->m_type != (uint16_t)XIoPoint_DigitalInput) return false;
    if (!point->m_flags.bits.m_valid && !point->m_flags.bits.m_hasValue) return false;
    if (active) *active = (point->m_flags.bits.m_valueB != 0);
    return true;
}

bool XIoChannelGroup_writeDigitalOutput(XIoChannelGroup* self, XIoPoint* point, bool active)
{
    if (!groupOwns(self, point) ||
        point->m_type != (uint16_t)XIoPoint_DigitalOutput) return false;
    /* 经点虚槽写受理；落定由点子类经 xIoPointWriteDone 转发组 writeDone。 */
    return XIoPoint_writeRaw_base(point, active ? 1 : 0);
}

bool XIoChannelGroup_readAnalogInput(XIoChannelGroup* self, XIoPoint* point, int32_t* value)
{
    if (!groupOwns(self, point) ||
        point->m_type != (uint16_t)XIoPoint_AnalogInput) return false;
    if (!point->m_flags.bits.m_valid && !point->m_flags.bits.m_hasValue) return false;
    if (value) *value = point->m_value;
    return true;
}

bool XIoChannelGroup_writeAnalogOutput(XIoChannelGroup* self, XIoPoint* point, int32_t value)
{
    if (!groupOwns(self, point) ||
        point->m_type != (uint16_t)XIoPoint_AnalogOutput) return false;
    /* 点值口径=工程值；标度换算由点子类（XIoAnalogPoint 派生）在 WriteRaw
     * 内完成。落定由点子类经 xIoPointWriteDone 转发组 writeDone。 */
    return XIoPoint_writeRaw_base(point, value);
}

bool XIoChannelGroup_isValid(const XIoChannelGroup* self, const XIoPoint* point)
{
    return (groupOwns(self, point) && point->m_flags.bits.m_valid) ? true : false;
}

/* ==================== 信号 ==================== */

void* XIoChannelGroup_inputChanged_signal(XIoChannelGroup* self, XIoPoint* point, int32_t value)
{
    XEmitSignal(self, XIoChannelGroup_inputChanged_signal,
        XVarList_Create(XVar(XIoPoint*, point), XVar(int, value)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XIoChannelGroup_writeDone_signal(XIoChannelGroup* self, XIoPoint* point, bool ok)
{
    XEmitSignal(self, XIoChannelGroup_writeDone_signal,
        XVarList_Create(XVar(XIoPoint*, point), XVar(bool, ok)),
        NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */
