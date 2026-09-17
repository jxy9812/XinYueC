/**
 * @file       XCalendarWidget.h
 * @brief      XCalendarWidget 日历控件（对标 Qt 6.8 QCalendarWidget
 *             核心公共 API）。
 * @details    功能范围：
 *             - selectedDate/setSelectedDate（选中日期）；
 *             - yearShown/monthShown（当前显示年月）；
 *             - setCurrentPage(year, month)；
 *             - setMinimumDate/setMaximumDate/clearMinimumDate/
 *               clearMaximumDate（日期范围钳位）；
 *             - setFirstDayOfWeek/firstDayOfWeek（默认 Monday=1）；
 *             - setGridVisible/isGridVisible（默认 false）；
 *             - setNavigationBarVisible/isNavigationBarVisible（默认 true）；
 *             - setSelectionMode/selectionMode（NoSelection/
 *               SingleSelection，数值对齐）；
 *             - 信号：clicked(XDate)/activated(XDate)/selectionChanged/
 *               currentPageChanged(year, month)；
 *             - 绘制：导航栏 + 星期头 + 日期网格（7 列 x 6 行，点击选择，
 *               鼠标点击非当前月日期切换页面）。
 * @note       模块总开关 XCALENDARWIDGET_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XCALENDARWIDGET_H
#define XCALENDARWIDGET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XDate.h"

#if XWIDGET_ON && XCALENDARWIDGET_ON

/** @brief 水平头格式（对标 HorizontalHeaderFormat，数值一致）。 */
typedef enum XCalendarHeaderFormat
{
    XCalendarHeaderFormat_NoHorizontalHeader = 0,
    XCalendarHeaderFormat_SingleLetterDayNames = 1,
    XCalendarHeaderFormat_ShortDayNames = 2,
    XCalendarHeaderFormat_LongDayNames = 3
} XCalendarHeaderFormat;

/** @brief 选择模式（对标 SelectionMode，数值一致）。 */
typedef enum XCalendarSelectionMode
{
    XCalendarSelectionMode_NoSelection = 0,
    XCalendarSelectionMode_SingleSelection = 1
} XCalendarSelectionMode;

XCLASS_DEFINE_BEGING(XCalendarWidget)
XCLASS_DEFINE_EXTEND_END(XCalendarWidget, XWidget)

typedef struct XCalendarWidget
{
    XWidget m_base;        /**< 基类成员；必须是第一个。 */
    XDate m_selected;      /**< 选中日期。 */
    int m_shownYear;       /**< 显示年。 */
    int m_shownMonth;      /**< 显示月（1..12）。 */
    XDate m_min;           /**< 最小日期。 */
    XDate m_max;           /**< 最大日期。 */
    bool m_minSet;         /**< 最小日期已设置。 */
    bool m_maxSet;         /**< 最大日期已设置。 */
    int m_firstDayOfWeek;  /**< 每周首日（1=Monday..7=Sunday）。 */
    bool m_gridVisible;    /**< 网格线（默认 false）。 */
    bool m_navBarVisible;  /**< 导航栏（默认 true）。 */
    int m_selectionMode;   /**< 选择模式。 */
    bool m_dateEditEnabled; /**< 日期编辑框（默认 false；存储）。 */
    bool m_showTodayDate;   /**< 今日按钮（默认 true）。 */
    int m_verticalHeaderFormat; /**< 垂直表头格式（0=ISO 周数，1=无）。 */
    int m_headerTextFormat; /**< 表头文本格式位（0=默认；预留 QTextCharFormat 简化）。 */
    int m_weekdayTextFormat; /**< 周几文本格式位（预留）。 */
    int m_horizontalHeaderFormat; /**< 水平表头格式（对标 HorizontalHeaderFormat；默认 1=短名；仅存储）。 */
    int m_dateEditAcceptDelay;  /**< 日期编辑确认延迟 ms（对标 dateEditAcceptDelay；默认 1500；仅存储）。 */
} XCalendarWidget;

/** @brief X日历控件classinit（对标 Qt 同名接口）。
 * @return 返回对象指针；无效时返回 NULL。
 */
XVtable* XCalendarWidget_class_init(void);
void XCalendarWidget_init(XCalendarWidget* self, XWidget* parent,
                          XWidgetFlags flags);
#define XCalendarWidget_create(parent, flags) XCalendarWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XCalendarWidget* XCalendarWidget_create_ex(XMemoryType memory,
                                           XWidget* parent, XWidgetFlags flags);
#define XCalendarWidget_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XCalendarWidget_delete_base(self) XClass_delete_base((XClass*)(self))

/**
 * @brief      获取选中日期。
 */
XDate XCalendarWidget_selectedDate(const XCalendarWidget* self);
/**
 * @brief      设置选中日期。
 */
void XCalendarWidget_setSelectedDate(XCalendarWidget* self, const XDate* date);
/**
 * @brief      获取显示年份。
 */
int XCalendarWidget_yearShown(const XCalendarWidget* self);
/**
 * @brief      获取显示月份。
 */
int XCalendarWidget_monthShown(const XCalendarWidget* self);
/**
 * @brief      设置当前页（年/月）。
 */
void XCalendarWidget_setCurrentPage(XCalendarWidget* self, int year, int month);
/**
 * @brief      获取最小日期。
 */
XDate XCalendarWidget_minimumDate(const XCalendarWidget* self);
/**
 * @brief      设置最小日期。
 */
void XCalendarWidget_setMinimumDate(XCalendarWidget* self, const XDate* date);
/**
 * @brief      清除最小日期限制。
 */
void XCalendarWidget_clearMinimumDate(XCalendarWidget* self);
/**
 * @brief      获取最大日期。
 */
XDate XCalendarWidget_maximumDate(const XCalendarWidget* self);
/**
 * @brief      设置最大日期。
 */
void XCalendarWidget_setMaximumDate(XCalendarWidget* self, const XDate* date);
/**
 * @brief      清除最大日期限制。
 */
void XCalendarWidget_clearMaximumDate(XCalendarWidget* self);
/**
 * @brief      获取每周首日。
 */
int XCalendarWidget_firstDayOfWeek(const XCalendarWidget* self);
/**
 * @brief      设置每周首日。
 */
void XCalendarWidget_setFirstDayOfWeek(XCalendarWidget* self, int dayOfWeek);
/**
 * @brief      获取网格可见。
 */
bool XCalendarWidget_isGridVisible(const XCalendarWidget* self);
/**
 * @brief      设置网格可见。
 */
void XCalendarWidget_setGridVisible(XCalendarWidget* self, bool visible);
/**
 * @brief      获取导航栏可见。
 */
bool XCalendarWidget_isNavigationBarVisible(const XCalendarWidget* self);
/**
 * @brief      设置导航栏可见。
 */
void XCalendarWidget_setNavigationBarVisible(XCalendarWidget* self, bool visible);
/** @brief 设置日期编辑框（存储；对标 setDateEditEnabled）。
 * @param self 目标控件。
 * @param enable true 启用。
 * @return 无返回值。
 */
void XCalendarWidget_setDateEditEnabled(XCalendarWidget* self, bool enable);
/** @brief 查询日期编辑框。 @param self 目标控件。 @return 启用返回 true。 */
bool XCalendarWidget_isDateEditEnabled(const XCalendarWidget* self);
/** @brief 计算日期所在周数（对标 QCalendarWidget::weekNumber；简化：ISO 周 1..53 估算）。
 * @param self 目标控件。
 * @param date 日期借用指针。
 * @return 周数；参数无效返回 -1。
 */
int XCalendarWidget_weekNumber(const XCalendarWidget* self,
                               const XDate* date);
/** @brief 设置表头文本格式位（QTextCharFormat 简化为位集）。
 * @param self 目标控件。
 * @param fmt 格式位。
 * @return 无返回值。
 */
void XCalendarWidget_setHeaderTextFormat(XCalendarWidget* self, int fmt);
/** @brief 设置周几文本格式位。
 * @param self 目标控件。
 * @param fmt 格式位。
 * @return 无返回值。
 */
void XCalendarWidget_setWeekdayTextFormat(XCalendarWidget* self, int fmt);
/** @brief 查询日期是否被选中（对标 isDateSelected）。
 * @param self 目标控件。
 * @param date 日期借用指针。
 * @return 选中返回 true。
 */
bool XCalendarWidget_isDateSelected(const XCalendarWidget* self,
                                    const XDate* date);
/** @brief 设置今日按钮可见（对标 setShowTodayDate）。
 * @param self 目标控件。
 * @param show true 显示。
 * @return 无返回值。
 */
void XCalendarWidget_setShowTodayDate(XCalendarWidget* self, bool show);
/** @brief 查询今日按钮可见。 @param self 目标控件。 @return 显示返回 true。 */
bool XCalendarWidget_isShowTodayDate(const XCalendarWidget* self);
/** @brief 设置垂直表头格式（对标 setVerticalHeaderFormat）。
 * @param self 目标控件。
 * @param format 格式码（0=ISO 周数，1=无）。
 * @return 无返回值。
 */
void XCalendarWidget_setVerticalHeaderFormat(XCalendarWidget* self,
                                             int format);
/** @brief 查询垂直表头格式。 @param self 目标控件。 @return 格式码。 */
int XCalendarWidget_verticalHeaderFormat(const XCalendarWidget* self);
/** @brief 今日日期（对标 QCalendarWidget::todayDate）。
 * @param self 目标控件。
 * @param out 输出日期。
 * @return 成功返回 true。
 */
bool XCalendarWidget_todayDate(const XCalendarWidget* self, XDate* out);
/** @brief 跳转今日页（对标 showToday）。
 * @param self 目标控件。
 * @return 无返回值。
 */
void XCalendarWidget_showTodayPage(XCalendarWidget* self);
/** @brief 查询表头文本格式位（对标 QCalendarWidget::headerTextFormat）。
 * @param self 目标控件；传入 NULL 时返回 0。
 * @return 格式位（QTextCharFormat 简化为位集；0=默认）。
 */
int XCalendarWidget_headerTextFormat(const XCalendarWidget* self);
/** @brief 查询周几文本格式位（对标 QCalendarWidget::weekdayTextFormat）。
 * @param self 目标控件；传入 NULL 时返回 0。
 * @return 格式位（0=默认）。
 */
int XCalendarWidget_weekdayTextFormat(const XCalendarWidget* self);
/** @brief 设置水平表头格式（对标 QCalendarWidget::setHorizontalHeaderFormat）。
 * @details 仅存储状态；渲染层表头文本当前为固定样式。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param format 格式码（对标 HorizontalHeaderFormat：0=单字母，1=短名，
 *        2=长名，3=不显示）。
 * @return 无返回值。
 */
void XCalendarWidget_setHorizontalHeaderFormat(XCalendarWidget* self,
                                               int format);
/** @brief 查询水平表头格式（对标 QCalendarWidget::horizontalHeaderFormat）。
 * @param self 目标控件；传入 NULL 时返回 1（ShortDayNames 默认值）。
 * @return 格式码。
 */
int XCalendarWidget_horizontalHeaderFormat(const XCalendarWidget* self);
/** @brief 设置日期编辑确认延迟（对标 QCalendarWidget::setDateEditAcceptDelay）。
 * @details 仅存储状态；编辑确认定时行为未接入。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param delay 延迟毫秒数。
 * @return 无返回值。
 */
void XCalendarWidget_setDateEditAcceptDelay(XCalendarWidget* self, int delay);
/** @brief 查询日期编辑确认延迟（对标 QCalendarWidget::dateEditAcceptDelay）。
 * @param self 目标控件；传入 NULL 时返回 0。
 * @return 延迟毫秒数（默认 1500）。
 */
int XCalendarWidget_dateEditAcceptDelay(const XCalendarWidget* self);
/** @brief 同时设置日期上下界（对标 QCalendarWidget::setDateRange）。
 * @details 等价于依次调用 setMinimumDate/setMaximumDate；NULL 参数
 *          的处理与对应单值函数一致。
 * @param self 目标控件。
 * @param min 最小日期借用指针；NULL 忽略。
 * @param max 最大日期借用指针；NULL 忽略。
 * @return 无返回值。
 */
void XCalendarWidget_setDateRange(XCalendarWidget* self, const XDate* min,
                                  const XDate* max);
/** @brief 显示下一个月（对标 QCalendarWidget::showNextMonth；不改选中）。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @return 无返回值。
 */
void XCalendarWidget_showNextMonth(XCalendarWidget* self);
/** @brief 显示上一个月（对标 QCalendarWidget::showPreviousMonth；不改选中）。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @return 无返回值。
 */
void XCalendarWidget_showPreviousMonth(XCalendarWidget* self);
/** @brief 显示下一年（对标 QCalendarWidget::showNextYear；不改选中）。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @return 无返回值。
 */
void XCalendarWidget_showNextYear(XCalendarWidget* self);
/** @brief 显示上一年（对标 QCalendarWidget::showPreviousYear；不改选中）。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @return 无返回值。
 */
void XCalendarWidget_showPreviousYear(XCalendarWidget* self);
/** @brief 显示选中日期所在月（对标 QCalendarWidget::showSelectedDate）。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @return 无返回值。
 */
void XCalendarWidget_showSelectedDate(XCalendarWidget* self);
/** @brief 显示今日所在月（对标 QCalendarWidget::showToday；不改选中）。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @return 无返回值。
 */
void XCalendarWidget_showToday(XCalendarWidget* self);
/**
 * @brief      获取选择模式。
 */
int XCalendarWidget_selectionMode(const XCalendarWidget* self);
/**
 * @brief      设置选择模式。
 */
void XCalendarWidget_setSelectionMode(XCalendarWidget* self, int mode);

/* ==================== 信号 ==================== */

/**
 * @brief      点击信号（真发射）。
 */
void* XCalendarWidget_clicked_signal(XCalendarWidget* self, const XDate* date);
/**
 * @brief      激活信号（真发射）。
 */
void* XCalendarWidget_activated_signal(XCalendarWidget* self, const XDate* date);
/**
 * @brief      选区变化信号（真发射）。
 */
void* XCalendarWidget_selectionChanged_signal(XCalendarWidget* self);
/**
 * @brief      当前页变化信号（真发射）。
 */
void* XCalendarWidget_currentPageChanged_signal(XCalendarWidget* self, int year, int month);

#endif /* XWIDGET_ON && XCALENDARWIDGET_ON */

#endif /* XCALENDARWIDGET_H */