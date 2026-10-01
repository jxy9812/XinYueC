/**
 * @file       XTimeEdit.h
 * @brief      XTimeEdit 时间编辑控件（对标 Qt 6.8 QTimeEdit）。
 * @details    Qt 中 QTimeEdit 是 QDateTimeEdit 的便捷子类：除构造/析构
 *             与 userTimeChanged 信号转发外不重载任何行为（纯别名，
 *             Qt 亦无 QTimeEditPrivate）。本类对齐该继承关系：
 *             XTimeEdit→XDateTimeEdit（对标 QTimeEdit→QDateTimeEdit），
 *             构造语义逐项对齐 QTimeEdit(QWidget*)（qdatetimeedit.cpp:
 *             1633-1637）：解析类型置 Time（仅时间段参与编辑，日期
 *             记号按 Qt parserType 滤段拒绝/退化为字面）、初始值
 *             2000-01-01 00:00:00.000（对标 DATE_INITIAL 载体上的
 *             QDATETIMEEDIT_TIME_MIN 口径）、displayFormat="HH:mm:ss"
 *             并触发基类纯时间段收窄（日期范围收窄到初始值日期，故
 *             minimumDate==maximumDate==2000-01-01，时间范围
 *             00:00:00.000..23:59:59.999），连接 timeChanged→
 *             userTimeChanged（对标构造函数体内 connect，程序性
 *             setTime 同样触发）。calendarPopup 开启后点下拉箭头弹出
 *             纯时间设定行（无日历；三态机制见 XDateTimeEdit.c「日历
 *             弹层」节，一排「[时] ▲ 值 ▼」组，点箭头/滚轮步进、点值
 *             切编辑焦点，调整不关层）。时间值/范围/分段编辑等其余全
 *             部公共 API 均继承自 XDateTimeEdit，本类不新增字段。
 * @note       模块总开关 XTIMEEDIT_ON 定义于 XGuiConfig.h；依赖
 *             XWIDGET_ON、XABSTRACTSPINBOX_ON、XDATETIMEEDIT_ON。
 * @author     XinYueC 团队
 */
#ifndef XTIMEEDIT_H
#define XTIMEEDIT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XDateTimeEdit.h"

#if XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON && XTIMEEDIT_ON

/* ==================== 类定义（仅继承，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XTimeEdit)
XCLASS_DEFINE_EXTEND_END(XTimeEdit, XDateTimeEdit)

/** @brief XTimeEdit 对象；m_base 必须是第一个成员。
 * @note  无自有字段：QTimeEdit 相对 QDateTimeEdit 无新增数据，时间
 *        值/范围等全部承载于基类 XDateTimeEdit。 */
typedef struct XTimeEdit
{
    XDateTimeEdit m_base; /**< 基类成员（嵌 XDateTimeEdit）；必须是第一个。 */
} XTimeEdit;

/* ==================== 生命周期 ==================== */

/** @brief XTimeEdit 类虚函数表初始化（仅继承 XDateTimeEdit 虚表）。
 * @return 共享虚函数表指针。 */
XVtable* XTimeEdit_class_init(void);
/**
 * @brief      构造 XTimeEdit（对标 QTimeEdit::QTimeEdit(parent)）。
 * @details    先完成基类 XDateTimeEdit 构造，随后逐项对齐 Qt：
 *             parserType=Time（仅时间段可编辑，setDisplayFormat 混入
 *             日期记号被拒/退化为字面）、初始值=2000-01-01 00:00:00.000、
 *             displayFormat="HH:mm:ss"（纯时间段构造触发基类收窄：
 *             日期范围收到 2000-01-01 当天、时间范围 00:00:00.000..
 *             23:59:59.999），最后连接 timeChanged→userTimeChanged
 *             （时间部分任何变化路径均发射 userTimeChanged，对标 Qt
 *             全变更口径）。
 * @param      self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param      parent 父控件指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     无返回值。
 */
void XTimeEdit_init(XTimeEdit* self, XWidget* parent, XWidgetFlags flags);
#define XTimeEdit_create(parent, flags) XTimeEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      堆构造 XTimeEdit（XMalloc+Set_Class_Memory+Set_Class_IsHeap
 *             成对；对标 QTimeEdit 的 new 构造）。
 * @param      memory XMemory 内存类型。
 * @param      parent 父控件指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     新对象指针；内存分配失败返回 NULL。
 */
XTimeEdit* XTimeEdit_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags);
/** @brief 析构调度入口宏（复用基类 XDateTimeEdit 的 deinit 链）。 */
#define XTimeEdit_deinit_base(self) XDateTimeEdit_deinit_base((XDateTimeEdit*)(self))
/** @brief 删除堆对象入口宏（XClass_delete_base 转发）。 */
#define XTimeEdit_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 信号 ==================== */

/**
 * @brief      用户改时信号（对标 QTimeEdit::userTimeChanged；真发射）。
 * @details    本信号仅为完整实现 XTimeEdit 的 userTimeChanged 属性
 *             存在（对标 Qt 文档「only exists to fully implement the
 *             Q_PROPERTY」）：由构造函数连接基类 timeChanged 转发发射，
 *             时间部分任何变化路径（步进/键入提交/程序性 setTime/
 *             setDateTime/范围钳位）均随之触发；正常应优先使用继承的
 *             timeChanged。载荷为新时间（借用内部存储）。
 * @param      self 目标控件；NULL 仅作信号标识占位。
 * @param      time 新时间；借用。
 * @return     信号标识（函数自身地址）。
 */
void* XTimeEdit_userTimeChanged_signal(XTimeEdit* self, const XTime* time);

#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON && XTIMEEDIT_ON */

#endif /* XTIMEEDIT_H */
