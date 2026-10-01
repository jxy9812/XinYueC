/**
 * @file       XDateEdit.c
 * @brief      XDateEdit 日期编辑控件实现（对标 Qt 6.8 QDateEdit）。
 * @details    与同名头文件的公共 API 一一对应。QDateEdit 在 Qt 中不
 *             重载 QDateTimeEdit 任何虚行为，仅在构造函数里委托基类
 *             受保护构造（parserType=QDate + init(DATE_INITIAL)）并
 *             连接 dateChanged→userDateChanged；本实现保持同一形状：
 *             仅继承 XDateTimeEdit 虚表，构造置解析类型/初始值/范围/
 *             "yyyy/M/d" 显示格式并挂接信号转发，分段编辑/步进进位/
 *             范围钳位/日历弹层三态机制全部复用基类既有实现。
 * @author     XinYueC 团队
 */

#include "XDateEdit.h"
#include "XMemory.h"
#include "XObject.h"
#include "XVarList.h"

#include "XAlgorithm.h"

#if XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON && XDATEEDIT_ON

/* ==================== 虚函数表 ==================== */

XVtable* XDateEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDateEdit)
    /* 对标 QDateEdit→QDateTimeEdit：子类不重载任何虚槽，仅继承虚表
     * （分段步进/键入提交/绘制/弹层机器全部走 XDateTimeEdit 实现）。 */
    XVTABLE_INHERIT_XCLASS(XDateTimeEdit);
    return XVTABLE_DEFAULT;
}

/* ==================== 信号转发（对标 QDateEdit 构造函数体内的
 *                     connect(this, &QDateEdit::dateChanged,
 *                               this, &QDateEdit::userDateChanged)） ==================== */

/** @brief dateChanged→userDateChanged 转发槽：载荷原样透传（内部
 *         存储借用指针在发射期间有效），发射语义=日期部分任何变化
 *         路径（步进/键入提交/程序性 setDate 等）都触发，对齐 Qt。 */
static void xde_userDateChangedForward(XObject* receiver, XVarList* args)
{
    XDateEdit* edit = (XDateEdit*)receiver;
    XVarList* out;
    if (!edit || !args) return;
    XVarList_args_1(args, XDate*, date);
    out = XVarList_Create(XVar(XDate*, date));
    if (!out) return;
    if (((XObject*)edit)->m_signalSlot) {
        XObject_emitSignal((XObject*)edit,
                           (size_t)XDateEdit_userDateChanged_signal,
                           out, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(out);
    }
}

/* ==================== 生命周期 ==================== */

void XDateEdit_init(XDateEdit* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XDateTimeEdit_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XDateEdit);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    /* 对标 QDateEdit(QWidget*) → QDateTimeEdit(variant, QMetaType::QDate,
     * parent)（qdatetimeedit.cpp:1703-1707 受保护构造先设 parserType
     * 再 init）：仅日期段参与编辑，时间记号经基类 parserType 滤段
     * 拒绝/退化为字面。 */
    self->m_base.m_parserType = (int)XDateTimeEditParserType_Date;
    /* 初始值=QDATETIMEEDIT_DATE_INITIAL（2000-01-01，时间部分清零；
     * 对标 init 的 value=startOfDay 分支；构造期直赋不发射）。 */
    XDate_setDate(&self->m_base.m_dateTime.m_date, 2000, 1, 1);
    XTime_setHMS(&self->m_base.m_dateTime.m_time, 0, 0, 0, 0);
    /* 范围=COMPAT_DATE_MIN..DATE_MAX（1752-09-14..9999-12-31，对标
     * QDateTimeEditPrivate 构造的默认 minimum/maximum）。 */
    {
        XDate dmin;
        XDate dmax;
        XDate_setDate(&dmin, 1752, 9, 14);
        XDate_setDate(&dmax, 9999, 12, 31);
        XDateTimeEdit_setMinimumDate(&self->m_base, &dmin);
        XDateTimeEdit_setMaximumDate(&self->m_base, &dmax);
    }
    /* 对标 init 的 setDisplayFormat(defaultDateFormat)：纯日期段格式
     * 触发基类收窄（复位时间范围+值时间清零）；zh_CN 短日期格式
     * 口径 yyyy/M/d。 */
    XDateTimeEdit_setDisplayFormat(&self->m_base, "yyyy/M/d");
    /* 对标构造函数体内的 connect（值变化全路径转发发射）。 */
    XObject_connect_1((XObject*)self,
                      (size_t)XDateTimeEdit_dateChanged_signal(NULL, NULL),
                      (XObject*)self, xde_userDateChangedForward,
                      XConnectionType_Direct);
}

XDateEdit* XDateEdit_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags)
{
    XDateEdit* self = (XDateEdit*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XDateEdit_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 信号 ==================== */

void* XDateEdit_userDateChanged_signal(XDateEdit* self, const XDate* date)
{
    (void)self; (void)date;
    return (void*)(size_t)XDateEdit_userDateChanged_signal;
}

#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON && XDATEEDIT_ON */
