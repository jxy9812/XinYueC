/**
 * @file       XDateTimeEdit.h
 * @brief      XDateTimeEdit 日期时间编辑控件（对标 Qt 6.8
 *             QDateTimeEdit 核心公共 API）。
 * @details    功能范围：
 *             - 继承 XAbstractSpinBox（对标 QDateTimeEdit 继承
 *               QAbstractSpinBox），复用上下步进/键盘编辑框架；
 *             - 值：dateTime/setDateTime、date/setDate、time/setTime、
 *               minimumDateTime/maximumDateTime 与范围钳位；
 *             - 分段：Section 枚举（NoSection/AmPm/MSec/Second/Minute/
 *               Hour/Day/Month/Year，数值对齐 QDateTimeEdit::Section）、
 *               currentSection/setCurrentSection、sections() 掩码；
 *             - displayFormat/setDisplayFormat（默认
 *               "yyyy-MM-dd HH:mm:ss"；绘制/文本按格式串的 yyyy/MM/
 *               dd/dddd/ddd/HH/h/hh/mm/ss/zzz/zz/z/AP(A) 占位符展开，
 *               字面字符原样输出；ddd/dddd 星期文案周一..周日/星期一..
 *               星期日，AP/A 固定中文「上午/下午」，详见实现注释）；
 *             - stepBy：按当前分段增减（年/月/日/时/分/秒/毫秒，含进位；
 *               上下午段 ±12 小时翻转）；键盘 Left/Right 跨段导航并整段
 *               选中（对标 QDateTimeEdit 方向键分段导航）；
 *             - 信号：dateTimeChanged(QDateTime*)/dateChanged/
 *               timeChanged（携带内部 XDateTime 指针，借用）；
 *             - calendarWidget 族：calendarWidget/setCalendarWidget
 *               （内置日历懒创建、外部日历接管与 selectionChanged →
 *               setDate 信号联动）。
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

/** @brief 编辑分段（对标 QDateTimeEdit::Section，数值一致）。
 * @note  ddd/dddd 星期记号与 dd 同挂 DaySection，h/hh 与 HH 同挂
 *        HourSection（对标 Qt：星期/12 小时制为内部记号档位，公共
 *        Section 枚举不单列；stepBy/导航按枚举码分派）。 */
typedef enum XDateTimeEditSection
{
    XDateTimeEditSection_NoSection = 0x0000,
    XDateTimeEditSection_AmPmSection = 0x0001,   /**< 上下午段。 */
    XDateTimeEditSection_MSecSection = 0x0002,   /**< 毫秒段。 */
    XDateTimeEditSection_SecondSection = 0x0004,
    XDateTimeEditSection_MinuteSection = 0x0008,
    XDateTimeEditSection_HourSection = 0x0010,
    XDateTimeEditSection_DaySection = 0x0100,
    XDateTimeEditSection_MonthSection = 0x0200,
    XDateTimeEditSection_YearSection = 0x0400
} XDateTimeEditSection;

XCLASS_DEFINE_BEGING(XDateTimeEdit)
XCLASS_DEFINE_EXTEND_END(XDateTimeEdit, XAbstractSpinBox)

/** @brief XCalendarWidget 前向声明（内置日历弹出控件；完整定义见
 *         XCalendarWidget.h）。 */
typedef struct XCalendarWidget XCalendarWidget;

typedef struct XDateTimeEdit
{
    XAbstractSpinBox m_base;   /**< 基类成员；必须是第一个。 */
    XDateTime m_dateTime;      /**< 当前值。 */
    XDateTime m_minimum;       /**< 最小值。 */
    XDateTime m_maximum;       /**< 最大值。 */
    XString* m_displayFormat;  /**< 显示格式串（对象拥有）。 */
    int m_currentSection;      /**< 当前编辑分段。 */
    bool m_calendarPopup;      /**< 日历弹出（默认 false，对标
                                    QDateTimeEdit::calendarPopup）。 */
    int m_timeSpec;            /**< 时区规格（Qt::TimeSpec；默认 0=LocalTime）。 */
#if XCALENDARWIDGET_ON
    XCalendarWidget* m_calendar; /**< 内置日历（懒创建；对象拥有，
                                     setCalendarWidget 可整体接管）。 */
#endif
} XDateTimeEdit;

XVtable* XDateTimeEdit_class_init(void);
void XDateTimeEdit_init(XDateTimeEdit* self, XWidget* parent,
                        XWidgetFlags flags);
#define XDateTimeEdit_create(parent, flags) XDateTimeEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XDateTimeEdit* XDateTimeEdit_create_ex(XMemoryType memory, XWidget* parent,
                                       XWidgetFlags flags);
#define XDateTimeEdit_deinit_base(self) XAbstractSpinBox_deinit_base((XAbstractSpinBox*)(self))
/** @brief 设置日历弹出（对标 setCalendarPopup）。
 * @param self 目标控件。
 * @param popup true 弹出日历。
 * @return 无返回值。
 */
void XDateTimeEdit_setCalendarPopup(XDateTimeEdit* self, bool popup);
/** @brief 查询日历弹出。 @param self 目标控件。 @return 弹出返回 true。 */
bool XDateTimeEdit_calendarPopup(const XDateTimeEdit* self);
#if XCALENDARWIDGET_ON
/**
 * @brief      获取内置日历控件（对标 QDateTimeEdit::calendarWidget）。
 * @details    首次访问懒创建内置 XCalendarWidget（保持 NULL 父控件，
 *             对象由本控件持有），并以当前日期初始化其选中态；随后把
 *             日历的 selectionChanged 信号连接到本控件 setDate 联动槽
 *             （对标 QComboBox::setView 的聚合思路）。注意：本函数为
 *             const 接口但含懒创建副作用，与 Qt 的 const 语义对齐。
 * @param      self 目标控件；传入 NULL 时返回 NULL。
 * @return     内置日历控件指针（借用；所有权仍在 XDateTimeEdit，
 *             调用方不得释放）。
 */
XCalendarWidget* XDateTimeEdit_calendarWidget(const XDateTimeEdit* self);
/**
 * @brief      挂接外部日历控件（对标 QDateTimeEdit::setCalendarWidget）。
 * @details    取得所有权：先释放旧的内置/接管日历，再接管 calendar；
 *             若 XCalendarWidget 提供选中信号（selectionChanged），则
 *             将其连接到本控件的 setDate 联动槽，实现日历选区 → 编辑框
 *             值的单向同步；传入 NULL 仅释放并清空当前日历（与 Qt 中
 *             setCalendarWidget(NULL) 语义一致）。@note calendar 传入
 *             后所有权归 XDateTimeEdit，调用方此后不得再释放该对象；
 *             同一对象重复传入为幂等操作（不释放也不重连）。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param      calendar 日历控件；取得所有权，可为 NULL。
 * @return     无。
 */
void XDateTimeEdit_setCalendarWidget(XDateTimeEdit* self,
                                     XCalendarWidget* calendar);
#endif /* XCALENDARWIDGET_ON */
/** @brief 设置时区规格（对标 setTimeSpec）。
 * @param self 目标控件。
 * @param spec 时区规格码（Qt::TimeSpec：0=LocalTime，1=UTC，2=OffsetFromUTC，3=TimeZone）。
 * @return 无返回值。
 */
void XDateTimeEdit_setTimeSpec(XDateTimeEdit* self, int spec);
/** @brief 查询时区规格。 @param self 目标控件。 @return 规格码。 */
int XDateTimeEdit_timeSpec(const XDateTimeEdit* self);
/** @brief 设置当前编辑分段（对标 setCurrentSectionIndex）。
 * @param self 目标控件。
 * @param index 分段序号。
 * @return 无返回值。
 */
void XDateTimeEdit_setCurrentSectionIndex(XDateTimeEdit* self, int index);
/** @brief 查询当前分段序号（对标 Q_PROPERTY currentSectionIndex READ；
 *         序号为分段在显示格式中的 0 基位置，与分段枚举码分离）。 */
int XDateTimeEdit_currentSectionIndex(const XDateTimeEdit* self);
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
 * @brief      获取最小日期（对标 QDateTimeEdit::minimumDate）。
 * @details    返回最小值 m_minimum 的日期部分；时间部分由
 *             minimumTime 查询。
 * @param      self 目标控件；传入 NULL 时返回零值日期。
 * @return     按值返回 XDate；self 为 NULL 时返回全零（无效）日期。
 */
XDate XDateTimeEdit_minimumDate(const XDateTimeEdit* self);
/**
 * @brief      设置最小日期（对标 QDateTimeEdit::setMinimumDate）。
 * @details    保留当前最小时间部分，仅替换日期部分后经
 *             setMinimumDateTime 应用并钳位当前值。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param      date 最小日期；NULL 时保持原范围不变。
 * @return     无。
 */
void XDateTimeEdit_setMinimumDate(XDateTimeEdit* self, const XDate* date);
/**
 * @brief      获取最大日期（对标 QDateTimeEdit::maximumDate）。
 * @param      self 目标控件；传入 NULL 时返回零值日期。
 * @return     按值返回 XDate；self 为 NULL 时返回全零（无效）日期。
 */
XDate XDateTimeEdit_maximumDate(const XDateTimeEdit* self);
/**
 * @brief      设置最大日期（对标 QDateTimeEdit::setMaximumDate）。
 * @details    保留当前最大时间部分，仅替换日期部分后经
 *             setMaximumDateTime 应用并钳位当前值。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param      date 最大日期；NULL 时保持原范围不变。
 * @return     无。
 */
void XDateTimeEdit_setMaximumDate(XDateTimeEdit* self, const XDate* date);
/**
 * @brief      获取最小时间（对标 QDateTimeEdit::minimumTime）。
 * @param      self 目标控件；传入 NULL 时返回零值时间。
 * @return     按值返回 XTime；self 为 NULL 时返回全零（无效）时间。
 */
XTime XDateTimeEdit_minimumTime(const XDateTimeEdit* self);
/**
 * @brief      设置最小时间（对标 QDateTimeEdit::setMinimumTime）。
 * @details    保留当前最小日期部分，仅替换时间部分后经
 *             setMinimumDateTime 应用并钳位当前值。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param      time 最小时间；NULL 时保持原范围不变。
 * @return     无。
 */
void XDateTimeEdit_setMinimumTime(XDateTimeEdit* self, const XTime* time);
/**
 * @brief      获取最大时间（对标 QDateTimeEdit::maximumTime）。
 * @param      self 目标控件；传入 NULL 时返回零值时间。
 * @return     按值返回 XTime；self 为 NULL 时返回全零（无效）时间。
 */
XTime XDateTimeEdit_maximumTime(const XDateTimeEdit* self);
/**
 * @brief      设置最大时间（对标 QDateTimeEdit::setMaximumTime）。
 * @details    保留当前最大日期部分，仅替换时间部分后经
 *             setMaximumDateTime 应用并钳位当前值。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param      time 最大时间；NULL 时保持原范围不变。
 * @return     无。
 */
void XDateTimeEdit_setMaximumTime(XDateTimeEdit* self, const XTime* time);
/**
 * @brief      复位最小日期为默认下界（对标 QDateTimeEdit::clearMinimumDate）。
 * @details    恢复为本类 init 的默认最小日期 1900-01-01（时间部分不变）；
 *             与 Qt 的 1752-09-14 下界不同，属 XGui 裁剪默认值。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @return     无。
 */
void XDateTimeEdit_clearMinimumDate(XDateTimeEdit* self);
/**
 * @brief      复位最大日期为默认上界（对标 QDateTimeEdit::clearMaximumDate）。
 * @details    恢复为本类 init 的默认最大日期 2999-12-31（时间部分不变）。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @return     无。
 */
void XDateTimeEdit_clearMaximumDate(XDateTimeEdit* self);
/**
 * @brief      复位最小时间为 00:00:00.000（对标 QDateTimeEdit::clearMinimumTime）。
 * @details    日期部分保持不变。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @return     无。
 */
void XDateTimeEdit_clearMinimumTime(XDateTimeEdit* self);
/**
 * @brief      复位最大时间为 23:59:59.999（对标 QDateTimeEdit::clearMaximumTime）。
 * @details    日期部分保持不变。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @return     无。
 */
void XDateTimeEdit_clearMaximumTime(XDateTimeEdit* self);
/**
 * @brief      复位最小值为 init 默认（对标 QDateTimeEdit::clearMinimumDateTime）。
 * @details    恢复为 1900-01-01 00:00:00.000 并钳位当前值。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @return     无。
 */
void XDateTimeEdit_clearMinimumDateTime(XDateTimeEdit* self);
/**
 * @brief      复位最大值为 init 默认（对标 QDateTimeEdit::clearMaximumDateTime）。
 * @details    恢复为 2999-12-31 23:59:59.999 并钳位当前值。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @return     无。
 */
void XDateTimeEdit_clearMaximumDateTime(XDateTimeEdit* self);
/**
 * @brief      同时设置日期上下界（对标 QDateTimeEdit::setDateRange）。
 * @details    等价于依次调用 setMinimumDate/setMaximumDate；NULL 参数的
 *             处理与对应单值函数一致。
 * @param      self 目标控件。
 * @param      min 最小日期；借用，不取得所有权。
 * @param      max 最大日期；借用，不取得所有权。
 * @return     无。
 */
void XDateTimeEdit_setDateRange(XDateTimeEdit* self, const XDate* min,
                                const XDate* max);
/**
 * @brief      同时设置时间上下界（对标 QDateTimeEdit::setTimeRange）。
 * @param      self 目标控件。
 * @param      min 最小时间；借用，不取得所有权。
 * @param      max 最大时间；借用，不取得所有权。
 * @return     无。
 */
void XDateTimeEdit_setTimeRange(XDateTimeEdit* self, const XTime* min,
                                const XTime* max);
/**
 * @brief      同时设置日期时间上下界（对标 QDateTimeEdit::setDateTimeRange）。
 * @param      self 目标控件。
 * @param      min 最小日期时间；借用，不取得所有权。
 * @param      max 最大日期时间；借用，不取得所有权。
 * @return     无。
 */
void XDateTimeEdit_setDateTimeRange(XDateTimeEdit* self,
                                    const XDateTime* min,
                                    const XDateTime* max);
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

/* ==================== 分段查询族（对标 sectionCount、sectionAt、
 *                     sectionText、displayedSections、setSelectedSection） ==================== */

/** @brief displayedSections 别名：与 sections() 同一承载（显示分段掩码）。 */
#define XDateTimeEdit_displayedSections(self) XDateTimeEdit_sections((self))
/**
 * @brief      查询显示分段数（对标 QDateTimeEdit::sectionCount）。
 * @details    按 displayFormat 中可识别分段记号（yyyy/MM/dd/dddd/ddd/
 *             HH/h/hh/mm/ss/zzz/zz/z/AP(A)）出现次数计数；格式为 NULL
 *             时按默认格式 "yyyy-MM-dd HH:mm:ss"。
 * @param      self 目标控件；NULL 返回 0。
 * @return     分段个数（0~16）。
 */
int XDateTimeEdit_sectionCount(const XDateTimeEdit* self);
/**
 * @brief      查询指定位置的分段（对标 QDateTimeEdit::sectionAt）。
 * @details    index 为分段在格式串中的出现序号（0 起，与 sectionCount
 *             配套）；越界返回 NoSection。
 * @param      self 目标控件；NULL 返回 NoSection。
 * @param      index 分段位置序号（0 起）。
 * @return     分段枚举值（XDateTimeEditSection）；越界为 NoSection(0)。
 */
int XDateTimeEdit_sectionAt(const XDateTimeEdit* self, int index);
/**
 * @brief      查询指定分段的显示文本（对标 QDateTimeEdit::sectionText）。
 * @details    按分段记号位宽渲染当前值（年份 4 位、其余数字段 2 位、
 *             h/z 按 1~3 位档位）；ddd/dddd 返回星期文案（周一..周日/
 *             星期一..星期日）、AmPmSection 返回「上午/下午」；分段未
 *             在格式中出现时返回空文本。同码多档（如 "dd ddd"）取格式
 *             中先出现者。
 * @param      self 目标控件；NULL 返回空文本对象。
 * @param      section 分段枚举值（XDateTimeEditSection）。
 * @return     新建 XString*；调用方负责 XString_delete_base 释放。
 */
XString* XDateTimeEdit_sectionText(const XDateTimeEdit* self, int section);
/**
 * @brief      设置选中分段（对标 QDateTimeEdit::setSelectedSection）。
 * @details    仅当该分段确实出现在显示格式中才生效，否则保持原分段
 *             不变（对齐 Qt 的有效性检查）。
 * @param      self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param      section 分段枚举值（XDateTimeEditSection）。
 * @return     无返回值。
 */
void XDateTimeEdit_setSelectedSection(XDateTimeEdit* self, int section);

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
/** @brief      用户改期信号（对标 QDateEdit::userDateChanged；真发射）。
 * @details    仅用户经步进（方向键/箭头点击）修改日期部分时发射，
 *             程序性 setDateTime/setDate 不发射；载荷为新日期。
 */
void* XDateTimeEdit_userDateChanged_signal(XDateTimeEdit* self,
                                           const XDate* date);
/** @brief      用户改时信号（对标 QTimeEdit::userTimeChanged；真发射）。
 * @details    仅用户经步进修改时间部分时发射，程序性设置不发射；
 *             载荷为新时间。
 */
void* XDateTimeEdit_userTimeChanged_signal(XDateTimeEdit* self,
                                           const XTime* time);

#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XDATETIMEEDIT_ON */

#endif /* XDATETIMEEDIT_H */