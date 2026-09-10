/**
 * @file       XDateTimeEdit.h
 * @brief      XDateTimeEdit 日期时间编辑控件（对标 Qt 6.8
 *             QDateTimeEdit 核心公共 API）。
 * @details    功能范围：
 *             - 继承 XAbstractSpinBox（对标 QDateTimeEdit 继承
 *               QAbstractSpinBox），复用上下步进/键盘编辑框架；
 *             - 值：dateTime/setDateTime、date/setDate、time/setTime、
 *               minimumDateTime/maximumDateTime 与范围钳位；
 *             - 分段：Section 枚举（NoSection..YearSection，数值对齐
 *               QDateTimeEdit::Section）、currentSection/
 *               setCurrentSection、sections() 掩码；
 *             - displayFormat/setDisplayFormat（默认
 *               "yyyy-MM-dd HH:mm:ss"；绘制/文本按格式串的 yyyy/MM/dd/
 *               HH/mm/ss 占位符展开）；
 *             - stepBy：按当前分段增减（年/月/日/时/分/秒，含进位）；
 *             - 信号：dateTimeChanged(QDateTime*)/dateChanged/
 *               timeChanged（携带内部 XDateTime 指针，借用）。
 * @note       模块总开关 XDATETIMEEDIT_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XDATETIMEEDIT_H
#define XDATETIMEEDIT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XAbstractSpinBox.h"
#include "XDateTime.h"

#if XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON

/** @brief 编辑分段（对标 QDateTimeEdit::Section，数值一致）。 */
typedef enum XDateTimeEditSection
{
    XDateTimeEditSection_NoSection = 0x0000,
    XDateTimeEditSection_SecondSection = 0x0004,
    XDateTimeEditSection_MinuteSection = 0x0008,
    XDateTimeEditSection_HourSection = 0x0010,
    XDateTimeEditSection_DaySection = 0x0100,
    XDateTimeEditSection_MonthSection = 0x0200,
    XDateTimeEditSection_YearSection = 0x0400
} XDateTimeEditSection;

XCLASS_DEFINE_BEGING(XDateTimeEdit)
XCLASS_DEFINE_EXTEND_END(XDateTimeEdit, XAbstractSpinBox)

typedef struct XDateTimeEdit
{
    XAbstractSpinBox m_base;   /**< 基类成员；必须是第一个。 */
    XDateTime m_dateTime;      /**< 当前值。 */
    XDateTime m_minimum;       /**< 最小值。 */
    XDateTime m_maximum;       /**< 最大值。 */
    char m_displayFormat[64];  /**< 显示格式串。 */
    int m_currentSection;      /**< 当前编辑分段。 */
} XDateTimeEdit;

XVtable* XDateTimeEdit_class_init(void);
void XDateTimeEdit_init(XDateTimeEdit* self, XWidget* parent,
                        XWidgetFlags flags);
#define XDateTimeEdit_create(parent, flags) XDateTimeEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XDateTimeEdit* XDateTimeEdit_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags);
#define XDateTimeEdit_deinit_base(self) XAbstractSpinBox_deinit_base((XAbstractSpinBox*)(self))
#define XDateTimeEdit_delete_base(self) XClass_delete_base((XClass*)(self))

/**
 * @brief      设置日期时间。
 */
void XDateTimeEdit_setDateTime(XDateTimeEdit* self,
                               const XDateTime* dateTime);
/**
 * @brief      获取日期时间。
 */
const XDateTime* XDateTimeEdit_dateTime(const XDateTimeEdit* self);
/**
 * @brief      设置日期。
 */
void XDateTimeEdit_setDate(XDateTimeEdit* self, const XDate* date);
/**
 * @brief      获取日期。
 */
XDate XDateTimeEdit_date(const XDateTimeEdit* self);
/**
 * @brief      设置时间。
 */
void XDateTimeEdit_setTime(XDateTimeEdit* self, const XTime* time);
/**
 * @brief      获取时间。
 */
XTime XDateTimeEdit_time(const XDateTimeEdit* self);
/**
 * @brief      获取最小日期时间。
 */
const XDateTime* XDateTimeEdit_minimumDateTime(const XDateTimeEdit* self);
/**
 * @brief      设置最小日期时间。
 */
void XDateTimeEdit_setMinimumDateTime(XDateTimeEdit* self,
                                      const XDateTime* dateTime);
/**
 * @brief      获取最大日期时间。
 */
const XDateTime* XDateTimeEdit_maximumDateTime(const XDateTimeEdit* self);
/**
 * @brief      设置最大日期时间。
 */
void XDateTimeEdit_setMaximumDateTime(XDateTimeEdit* self,
                                      const XDateTime* dateTime);
/**
 * @brief      设置显示格式（yyyy-MM-dd HH:mm:ss）。
 */
void XDateTimeEdit_setDisplayFormat(XDateTimeEdit* self,
                                    const char* utf8);
/**
 * @brief      获取显示格式。
 */
const char* XDateTimeEdit_displayFormat(const XDateTimeEdit* self);
/**
 * @brief      获取当前编辑分段。
 */
int XDateTimeEdit_currentSection(const XDateTimeEdit* self);
/**
 * @brief      设置当前编辑分段。
 */
void XDateTimeEdit_setCurrentSection(XDateTimeEdit* self, int section);
/**
 * @brief      获取分段掩码。
 */
int XDateTimeEdit_sections(const XDateTimeEdit* self);

/* ==================== 信号 ==================== */

/**
 * @brief      日期时间变化信号（真发射）。
 */
void* XDateTimeEdit_dateTimeChanged_signal(XDateTimeEdit* self,
                                           const XDateTime* dateTime);
/**
 * @brief      日期变化信号（真发射）。
 */
void* XDateTimeEdit_dateChanged_signal(XDateTimeEdit* self,
                                       const XDate* date);
/**
 * @brief      时间变化信号（真发射）。
 */
void* XDateTimeEdit_timeChanged_signal(XDateTimeEdit* self,
                                       const XTime* time);

#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XDATETIMEEDIT_H */