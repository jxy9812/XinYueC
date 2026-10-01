/**
 * @file       XDateEdit.h
 * @brief      XDateEdit 日期编辑控件（对标 Qt 6.8 QDateEdit）。
 * @details    Qt 中 QDateEdit 是 QDateTimeEdit 的便捷子类：除构造/析构
 *             与 userDateChanged 信号转发外不重载任何行为（纯别名，
 *             Qt 亦无 QDateEditPrivate）。本类对齐该继承关系：
 *             XDateEdit→XDateTimeEdit（对标 QDateEdit→QDateTimeEdit），
 *             构造语义逐项对齐 QDateEdit(QWidget*)（qdatetimeedit.cpp:
 *             1703-1707）：解析类型置 Date（仅日期段参与编辑，时间
 *             记号按 Qt parserType 滤段拒绝/退化为字面）、初始值
 *             2000-01-01（对标 QDATETIMEEDIT_DATE_INITIAL 口径）、
 *             范围 1752-09-14..9999-12-31（对标 COMPAT_DATE_MIN/
 *             DATE_MAX 口径）、displayFormat="yyyy/M/d"（zh_CN 短日期
 *             格式口径），并连接 dateChanged→userDateChanged（对标
 *             构造函数体内 connect，程序性 setDate 同样触发）。弹层
 *             内容按分段构成自动判定为纯日历（三态机制见
 *             XDateTimeEdit.c「日历弹层」节）；日期值/范围/分段编辑/
 *             日历弹层等其余全部公共 API 均继承自 XDateTimeEdit（对标
 *             QDateEdit 的公共槽与属性全部由 QDateTimeEdit 供给的关
 *             系），本类不新增字段。
 * @note       模块总开关 XDATEEDIT_ON 定义于 XGuiConfig.h；依赖
 *             XWIDGET_ON、XABSTRACTSPINBOX_ON、XDATETIMEEDIT_ON。
 * @author     XinYueC 团队
 */
#ifndef XDATEEDIT_H
#define XDATEEDIT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XDateTimeEdit.h"

#if XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON && XDATEEDIT_ON

/* ==================== 类定义（仅继承，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XDateEdit)
XCLASS_DEFINE_EXTEND_END(XDateEdit, XDateTimeEdit)

/** @brief XDateEdit 对象；m_base 必须是第一个成员。
 * @note  无自有字段：QDateEdit 相对 QDateTimeEdit 无新增数据，日期
 *        值/范围等全部承载于基类 XDateTimeEdit。 */
typedef struct XDateEdit
{
    XDateTimeEdit m_base; /**< 基类成员（嵌 XDateTimeEdit）；必须是第一个。 */
} XDateEdit;

/* ==================== 生命周期 ==================== */

/** @brief XDateEdit 类虚函数表初始化（仅继承 XDateTimeEdit 虚表）。
 * @return 共享虚函数表指针。 */
XVtable* XDateEdit_class_init(void);
/**
 * @brief      构造 XDateEdit（对标 QDateEdit::QDateEdit(parent)）。
 * @details    先完成基类 XDateTimeEdit 构造，随后逐项对齐 Qt：
 *             parserType=Date（仅日期段可编辑，setDisplayFormat 混入
 *             时间记号被拒/退化为字面）、初始值=2000-01-01 00:00:00
 *             （QDateEdit 无参构造回落 QDATETIMEEDIT_DATE_INITIAL 的
 *             口径）、范围=1752-09-14..9999-12-31、displayFormat=
 *             "yyyy/M/d"（纯日期段构造触发基类的范围/值收窄），最后
 *             连接 dateChanged→userDateChanged（值/日期部分任何变化
 *             路径均发射 userDateChanged，对标 Qt 全变更口径）。弹层
 *             按分段构成自动呈现纯日历（calendarPopup 开启后点下拉
 *             箭头弹出）。
 * @param      self 目标控件指针；传入 NULL 时函数不执行任何操作。
 * @param      parent 父控件指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     无返回值。
 */
void XDateEdit_init(XDateEdit* self, XWidget* parent, XWidgetFlags flags);
#define XDateEdit_create(parent, flags) XDateEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      堆构造 XDateEdit（XMalloc+Set_Class_Memory+Set_Class_IsHeap
 *             成对；对标 QDateEdit 的 new 构造）。
 * @param      memory XMemory 内存类型。
 * @param      parent 父控件指针；可为 NULL。
 * @param      flags 窗口标志位组合。
 * @return     新对象指针；内存分配失败返回 NULL。
 */
XDateEdit* XDateEdit_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags);
/** @brief 析构调度入口宏（复用基类 XDateTimeEdit 的 deinit 链）。 */
#define XDateEdit_deinit_base(self) XDateTimeEdit_deinit_base((XDateTimeEdit*)(self))
/** @brief 删除堆对象入口宏（XClass_delete_base 转发）。 */
#define XDateEdit_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 信号 ==================== */

/**
 * @brief      用户改期信号（对标 QDateEdit::userDateChanged；真发射）。
 * @details    本信号仅为完整实现 XDateEdit 的 userDateChanged 属性
 *             存在（对标 Qt 文档「only exists to fully implement the
 *             Q_PROPERTY」）：由构造函数连接基类 dateChanged 转发发射，
 *             日期部分任何变化路径（步进/键入提交/程序性 setDate/
 *             setDateTime/范围钳位）均随之触发；正常应优先使用继承的
 *             dateChanged。载荷为新日期（借用内部存储）。
 * @param      self 目标控件；NULL 仅作信号标识占位。
 * @param      date 新日期；借用。
 * @return     信号标识（函数自身地址）。
 */
void* XDateEdit_userDateChanged_signal(XDateEdit* self, const XDate* date);

#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON && XDATEEDIT_ON */

#endif /* XDATEEDIT_H */
