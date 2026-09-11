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
} XCalendarWidget;

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

#ifdef __cplusplus
}
#endif
void XCalendarWidget_setDateEditEnabled(XCalendarWidget* self, bool enable);
bool XCalendarWidget_isDateEditEnabled(const XCalendarWidget* self);
void XCalendarWidget_setDateEditAcceptDelay(XCalendarWidget* self, int delay);
int XCalendarWidget_dateEditAcceptDelay(const XCalendarWidget* self);
int XCalendarWidget_weekNumber(const XCalendarWidget* self, const XDate* date);
void XCalendarWidget_setHeaderTextFormat(XCalendarWidget* self, int format);
int XCalendarWidget_headerTextFormat(const XCalendarWidget* self);
void XCalendarWidget_setWeekdayTextFormat(XCalendarWidget* self, int day, int format);
int XCalendarWidget_weekdayTextFormat(const XCalendarWidget* self, int day);
void XCalendarWidget_setFirstDayOfWeek_2(XCalendarWidget* self, int day);
bool XCalendarWidget_isDateSelected(const XCalendarWidget* self);
void XCalendarWidget_setShowTodayDate(XCalendarWidget* self, bool show);
bool XCalendarWidget_isShowTodayDate(const XCalendarWidget* self);
XDate XCalendarWidget_todayDate(const XCalendarWidget* self);
void XCalendarWidget_setVerticalHeaderFormat(XCalendarWidget* self, int format);
#endif /* XCALENDARWIDGET_H */