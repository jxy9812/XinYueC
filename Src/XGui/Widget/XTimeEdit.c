/**
 * @file       XTimeEdit.c
 * @brief      XTimeEdit 时间编辑控件实现（对标 Qt 6.8 QTimeEdit）。
 * @details    与同名头文件的公共 API 一一对应。QTimeEdit 在 Qt 中不
 *             重载 QDateTimeEdit 任何虚行为，仅在构造函数里委托基类
 *             受保护构造（parserType=QTime + init(TIME_MIN)）并连接
 *             timeChanged→userTimeChanged；本实现保持同一形状：仅继
 *             承 XDateTimeEdit 虚表，构造置解析类型/初始值/"HH:mm:ss"
 *             显示格式（基类纯时间段收窄随之生效）并挂接信号转发，
 *             分段编辑/步进进位/范围钳位/纯时间设定行弹层（三态机制）
 *             全部复用基类既有实现。
 * @author     XinYueC 团队
 */

#include "XTimeEdit.h"
#include "XMemory.h"
#include "XObject.h"
#include "XVarList.h"

#include "XAlgorithm.h"

#if XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON && XTIMEEDIT_ON

/* ==================== 虚函数表 ==================== */

XVtable* XTimeEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTimeEdit)
    /* 对标 QTimeEdit→QDateTimeEdit：子类不重载任何虚槽，仅继承虚表
     * （分段步进/键入提交/绘制/弹层机器全部走 XDateTimeEdit 实现）。 */
    XVTABLE_INHERIT_XCLASS(XDateTimeEdit);
    return XVTABLE_DEFAULT;
}

/* ==================== 信号转发（对标 QTimeEdit 构造函数体内的
 *                     connect(this, &QTimeEdit::timeChanged,
 *                               this, &QTimeEdit::userTimeChanged)） ==================== */

/** @brief timeChanged→userTimeChanged 转发槽：载荷原样透传（内部
 *         存储借用指针在发射期间有效），发射语义=时间部分任何变化
 *         路径（步进/键入提交/程序性 setTime 等）都触发，对齐 Qt。 */
static void xte_userTimeChangedForward(XObject* receiver, XVarList* args)
{
    XTimeEdit* edit = (XTimeEdit*)receiver;
    XVarList* out;
    if (!edit || !args) return;
    XVarList_args_1(args, XTime*, time);
    out = XVarList_Create(XVar(XTime*, time));
    if (!out) return;
    if (((XObject*)edit)->m_signalSlot) {
        XObject_emitSignal((XObject*)edit,
                           (size_t)XTimeEdit_userTimeChanged_signal,
                           out, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(out);
    }
}

/* ==================== 生命周期 ==================== */

void XTimeEdit_init(XTimeEdit* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XDateTimeEdit_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XTimeEdit);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    /* 对标 QTimeEdit(QWidget*) → QDateTimeEdit(variant, QMetaType::QTime,
     * parent)（qdatetimeedit.cpp:1633-1637 受保护构造先设 parserType
     * 再 init）：仅时间段参与编辑，日期记号经基类 parserType 滤段
     * 拒绝/退化为字面。 */
    self->m_base.m_parserType = (int)XDateTimeEditParserType_Time;
    /* 初始值=QDATETIMEEDIT_TIME_MIN 落在 QDATETIMEEDIT_DATE_INITIAL
     * 载体上（2000-01-01 00:00:00.000；对标 init 的
     * value=dateTimeValue(DATE_INITIAL, TIME_MIN) 分支；构造期直赋
     * 不发射）。 */
    XDate_setDate(&self->m_base.m_dateTime.m_date, 2000, 1, 1);
    XTime_setHMS(&self->m_base.m_dateTime.m_time, 0, 0, 0, 0);
    /* 对标 init 的 setDisplayFormat(defaultTimeFormat)：纯时间段格式
     * 触发基类收窄——日期范围收到当前值日期（2000-01-01），时间范围
     * 保持 00:00:00.000..23:59:59.999（同一天）。 */
    XDateTimeEdit_setDisplayFormat(&self->m_base, "HH:mm:ss");
    /* 对标构造函数体内的 connect（值变化全路径转发发射）。 */
    XObject_connect_1((XObject*)self,
                      (size_t)XDateTimeEdit_timeChanged_signal(NULL, NULL),
                      (XObject*)self, xte_userTimeChangedForward,
                      XConnectionType_Direct);
}

XTimeEdit* XTimeEdit_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags)
{
    XTimeEdit* self = (XTimeEdit*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTimeEdit_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 信号 ==================== */

void* XTimeEdit_userTimeChanged_signal(XTimeEdit* self, const XTime* time)
{
    (void)self; (void)time;
    return (void*)(size_t)XTimeEdit_userTimeChanged_signal;
}

#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON && XTIMEEDIT_ON */
