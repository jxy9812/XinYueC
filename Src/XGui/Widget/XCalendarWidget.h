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
 *             - horizontalHeaderFormat/verticalHeaderFormat（枚举数值对齐
 *               Qt：垂直表头 0=NoVerticalHeader、1=ISOWeekNumbers，默认 1）、
 *               isDateEditEnabled（默认 true，对齐 Qt 6.8 探针）；
 *             - 月份下拉菜单（对标 Qt monthButton+monthMenu）：点导航条
 *               月份文本弹出 12 个月份条目，范围外月份置灰，选中即翻页
 *               （菜单为懒创建子对象 m_monthMenu，Qt 同款以 QMenu 挂在
 *               monthButton 下）；
 *             - ISO 周数列（verticalHeaderFormat=ISOWeekNumbers 时网格
 *               增列绘制 ISO 8601 周号，行取该行周一的周号——对齐
 *               QCalendarModel::data 的 `date.weekNumber()`）；
 *             - 日期范围默认 [儒略日 1, 9999-12-31]（对齐 Qt 6.8
 *               QCalendarWidgetPrivate 的 m_minimumDate/m_maximumDate）；
 *             - sizeHint/minimumSizeHint 由字体度量推导且两者相等
 *               （对齐 Qt `sizeHint() == minimumSizeHint()`）；
 *             - 信号：clicked(XDate)/activated(XDate)/selectionChanged/
 *               currentPageChanged(year, month)；clicked 在**鼠标释放**
 *               时发射（对齐 QCalendarView::mouseReleaseEvent）；
 *             - 绘制：导航栏 + 星期头 + 日期网格（7 列日期 + 可选周号列，
 *               6 行，点击选择，鼠标点击非当前月日期切换页面）。
 * @note       模块总开关 XCALENDARWIDGET_ON 定义于 XGuiConfig.h。
 *             [deferred] ① dateEditEnabled/dateEditAcceptDelay 对应 Qt 的
 *             QCalendarTextNavigator（按键就地弹出日期文本编辑层并按延迟
 *             确认），需要日期文本输入子系统，本轮只保留属性存取并更正
 *             注释（此前误记为「年份就地编辑开关」，Qt 的 yearEdit 无
 *             开关）；② calendar()/setCalendar(QCalendar) 无 C 承载
 *             （本库无 XCalendar 类型）；③ weekdayTextFormat/
 *             dateTextFormat/paintCell/updateCell 需要 QTextCharFormat 与
 *             QMap<QDate,…> 承载，本库无对应类型。
 * @author     XinYueC 团队
 */
#ifndef XCALENDARWIDGET_H
#define XCALENDARWIDGET_H

#if XGUI_ON
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

/** @brief 水平头格式（对标 QCalendarWidget::HorizontalHeaderFormat，数值一致）。 */
typedef enum XCalendarHorizontalHeaderFormat
{
    XCalendarHorizontalHeaderFormat_NoHorizontalHeader = 0,
    XCalendarHorizontalHeaderFormat_SingleLetterDayNames = 1,
    XCalendarHorizontalHeaderFormat_ShortDayNames = 2,
    XCalendarHorizontalHeaderFormat_LongDayNames = 3
} XCalendarHorizontalHeaderFormat;

typedef XCalendarHorizontalHeaderFormat XCalendarHorizontalHeaderFormat;
#define XCalendarHorizontalHeaderFormat_NoHorizontalHeader \
    XCalendarHorizontalHeaderFormat_NoHorizontalHeader
#define XCalendarHorizontalHeaderFormat_SingleLetterDayNames \
    XCalendarHorizontalHeaderFormat_SingleLetterDayNames
#define XCalendarHorizontalHeaderFormat_ShortDayNames \
    XCalendarHorizontalHeaderFormat_ShortDayNames
#define XCalendarHorizontalHeaderFormat_LongDayNames \
    XCalendarHorizontalHeaderFormat_LongDayNames

/** @brief 选择模式（对标 SelectionMode，数值一致）。 */
typedef enum XCalendarSelectionMode
{
    XCalendarSelectionMode_NoSelection = 0,
    XCalendarSelectionMode_SingleSelection = 1
} XCalendarSelectionMode;

/** @brief 垂直表头格式（对标 QCalendarWidget::VerticalHeaderFormat，数值一致：
 *         0=NoVerticalHeader、1=ISOWeekNumbers）。
 * @note  此前本库把两值语义写反（0 当 ISO 周数、1 当无表头），与 Qt 数值
 *        不一致；现按 Qt 枚举顺序定义。Qt 6.8 缺省为 ISOWeekNumbers
 *        （本机 Qt 6.8.0 探针 `verticalHeaderFormat()==1`）。
 *        ISOWeekNumbers 时网格左侧绘制 ISO 8601 周号列（行取该行周一的
 *        周号，对齐 QCalendarModel::data 的 `date.weekNumber()`）。 */
typedef enum XCalendarVerticalHeaderFormat
{
    XCalendarVerticalHeaderFormat_NoVerticalHeader = 0,
    XCalendarVerticalHeaderFormat_ISOWeekNumbers = 1
} XCalendarVerticalHeaderFormat;

XCLASS_DEFINE_BEGING(XCalendarWidget)
XCLASS_DEFINE_EXTEND_END(XCalendarWidget, XWidget)

typedef struct XCalendarWidget
{
    XWidget m_base;        /**< 基类成员；必须是第一个。 */
    XDate m_selected;      /**< 选中日期。 */
    XDate m_min;           /**< 最小日期（构造默认=儒略日 1，对齐 Qt）。 */
    XDate m_max;           /**< 最大日期（构造默认=9999-12-31，对齐 Qt）。 */
    XDate m_pressDate;     /**< 按下命中的候选日期（释放时才落选中）。 */
    struct XLineEdit* m_yearEdit; /**< 年份就地编辑器（对标 Qt yearEdit 的
                                       真实编辑控件：WA14+焦点接入输入法/
                                       屏幕键盘链路；懒创建、归属本控件，
                                       deinit 释放；可见即编辑态）。 */
    struct XMenu* m_monthMenu; /**< 月份下拉菜单（懒创建、父控件=本控件，
                                   对标 Qt `monthMenu = new QMenu(monthButton)`；
                                   子对象随本控件析构）。 */
    struct XAction* m_monthActions[12]; /**< 12 个月份条目（借用；菜单拥有）。 */
    int m_shownYear;       /**< 显示年。 */
    int m_firstDayOfWeek;  /**< 每周首日（1=Monday..7=Sunday）。 */
    int m_selectionMode;   /**< 选择模式。 */
    int m_verticalHeaderFormat; /**< 垂直表头格式（XCalendarVerticalHeaderFormat：
                                    0=NoVerticalHeader、1=ISOWeekNumbers；默认 1，
                                    对标 Qt 6.8 缺省）。 */
    int m_headerTextFormat; /**< 表头文本格式位（0=默认；预留 QTextCharFormat 简化）。 */
    int m_weekdayTextFormat; /**< 周几文本格式位（预留）。 */
    int m_horizontalHeaderFormat; /**< 水平表头格式（XCalendarHorizontalHeaderFormat；默认 2=ShortDayNames）。 */
    int m_dateEditAcceptDelay;  /**< 日期编辑确认延迟 ms（对标 dateEditAcceptDelay；默认 1500；仅存储）。 */
    int m_shownMonth;      /**< 显示月（1..12；setCurrentPage 内钳位到 1..12；
                                    注意 XVar 宏按地址取值，保持 int 供信号参数直接借用）。 */
    uint8_t m_minSet : 1;  /**< 最小日期已设置（构造即 true；clearMinimumDate
                                复位为儒略日 1 后仍为 true）。 */
    uint8_t m_maxSet : 1;  /**< 最大日期已设置（构造即 true）。 */
    uint8_t m_gridVisible : 1; /**< 网格线（默认 false，对标 Qt 6.8 探针）。 */
    uint8_t m_navBarVisible : 1; /**< 导航栏（默认 true）。 */
    uint8_t m_dateEditEnabled : 1; /**< 日期编辑框（默认 true，对标 Qt 6.8 探针
                                        `isDateEditEnabled()==1`；仅存储——见文件头
                                        deferred ①）。 */
    uint8_t m_pressValid : 1; /**< 本次按下命中了范围内日期（释放时据此发
                                   clicked/改选中，对标 QCalendarView
                                   validDateClicked）。 */
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
 * @brief      结束年份就地编辑会话（不提交；幂等，无会话时零操作）。
 * @details    终结 beginYearEdit 开启的会话：收起年份编辑器、解除屏幕
 *             键盘确认接线并收起键盘（对标 yearEdit 的 Escape 语义），
 *             不翻页、不改选中值。供日历弹层宿主（XDateTimeEdit）在弹
 *             层收起/重开时调用——年份编辑器以 explicitShow 显形，弹层
 *             隐藏/重现的可见性传播不收回它，残留会话会把旧缓冲文本
 *             （如 "2000"）覆在标题年份上并吞掉后续物理键入（r2 项4
 *             「2000年1月」残留态根因）。
 * @param      self 日历控件；NULL 零操作。
 * @return     无返回值。
 */
void XCalendarWidget_endYearEdit(XCalendarWidget* self);
/**
 * @brief      获取最小日期。
 */
XDate XCalendarWidget_minimumDate(const XCalendarWidget* self);
/**
 * @brief      设置最小日期（对标 QCalendarWidget::setMinimumDate）。
 * @details    Qt 6.8 语义（qcalendarwidget.cpp:2512-2531 +
 *             QCalendarModel::setMinimumDate:1200-1211）：
 *             ① 无效日期（isValid()==false）直接忽略；
 *             ② 最小值越过最大值时把最大值抬到最小值（范围保持合法）；
 *             ③ 选中日期低于新下界时钳到新下界，并按钳位结果翻页 +
 *                发 selectionChanged。
 * @param      self 目标控件；NULL 零操作。
 * @param      date 最小日期借用指针；NULL/无效忽略。
 * @return     无返回值。
 */
void XCalendarWidget_setMinimumDate(XCalendarWidget* self, const XDate* date);
/**
 * @brief      清除最小日期限制（对标 QCalendarWidget::clearMinimumDate）。
 * @details    Qt 6.6+ 把最小值复位为儒略日 1（qcalendarwidget.cpp:2531-2534），
 *             本库同样存 `XDate_fromJulianDay(1)`——与 Qt 的
 *             `minimumDate().toJulianDay()==1` 同值（XDate 只承诺公历
 *             1..9999 的字段读写，年份字段为负值但比较/钳位仍按儒略日）。
 */
void XCalendarWidget_clearMinimumDate(XCalendarWidget* self);
/**
 * @brief      获取最大日期。
 */
XDate XCalendarWidget_maximumDate(const XCalendarWidget* self);
/**
 * @brief      设置最大日期（对标 QCalendarWidget::setMaximumDate）。
 * @details    Qt 6.8 语义（qcalendarwidget.cpp:2568-2587 +
 *             QCalendarModel::setMaximumDate:1213-1224）：
 *             ① 无效日期直接忽略；
 *             ② 最大值低于最小值时把最小值降到最大值；
 *             ③ 选中日期高于新上界时钳到新上界，并按钳位结果翻页 +
 *                发 selectionChanged。
 * @param      self 目标控件；NULL 零操作。
 * @param      date 最大日期借用指针；NULL/无效忽略。
 * @return     无返回值。
 */
void XCalendarWidget_setMaximumDate(XCalendarWidget* self, const XDate* date);
/**
 * @brief      清除最大日期限制（对标 QCalendarWidget::clearMaximumDate）。
 * @details    Qt 复位为 9999-12-31；本库同样存 9999-12-31
 *             （clear后 `maximumDate()` 与 Qt 等值）。
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
 * @details Qt 的 dateEditEnabled 控制的是 **QCalendarTextNavigator**——
 *          日历持焦时按非修饰键会弹出按当前区域短格式编辑日期的文本层，
 *          停手 dateEditAcceptDelay 毫秒后确认（qcalendarwidget.cpp:
 *          2919-2967 setNavigatorEnabled）。它**不是**导航栏年份就地编辑
 *          的开关（yearButton 的 yearEdit 在 Qt 里没有开关，本库恒可点，
 *          与 Qt 一致）。该文本层需要日期文本输入子系统，本轮 deferred，
 *          本属性保持存取语义（见文件头 deferred ①）。
 * @param self 目标控件。
 * @param enable true 启用。
 * @return 无返回值。
 */
void XCalendarWidget_setDateEditEnabled(XCalendarWidget* self, bool enable);
/** @brief 查询日期编辑框。 @param self 目标控件。 @return 启用返回 true。 */
bool XCalendarWidget_isDateEditEnabled(const XCalendarWidget* self);
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
/** @brief 设置垂直表头格式（对标 setVerticalHeaderFormat）。
 * @details 枚举值按 Qt 6.8 数值顺序（0=NoVerticalHeader、
 *          1=ISOWeekNumbers）。[已登记偏差] 本控件网格固定 7 列，尚未
 *          绘制 ISO 周数列，本属性仅存取。
 * @param self 目标控件。
 * @param format 格式码（XCalendarVerticalHeaderFormat）。
 * @return 无返回值。
 */
void XCalendarWidget_setVerticalHeaderFormat(XCalendarWidget* self,
                                             int format);
/** @brief 查询垂直表头格式（默认 1=ISOWeekNumbers，对标 Qt 6.8 探针）。
 * @param self 目标控件；传入 NULL 时返回 0（NoVerticalHeader）。
 * @return 格式码（XCalendarVerticalHeaderFormat）。
 */
int XCalendarWidget_verticalHeaderFormat(const XCalendarWidget* self);
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
 * @details 渲染层按格式切换表头文案（0=不显示，1=单字，2=短名
 *          「周一」，3=长名「星期一」）。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param format 格式码（XCalendarHorizontalHeaderFormat：0=不显示，
 *        1=单字母，2=短名，3=长名）。
 * @return 无返回值。
 */
void XCalendarWidget_setHorizontalHeaderFormat(XCalendarWidget* self,
                                               int format);
/** @brief 查询水平表头格式（对标 QCalendarWidget::horizontalHeaderFormat）。
 * @param self 目标控件；传入 NULL 时返回 2（ShortDayNames 默认值）。
 * @return 格式码。
 */
int XCalendarWidget_horizontalHeaderFormat(const XCalendarWidget* self);
/** @brief 设置日期编辑确认延迟（对标 QCalendarWidget::setDateEditAcceptDelay）。
 * @details 仅存储状态；对应 Qt QCalendarTextNavigator 的确认计时器，
 *          随 dateEditEnabled 一起 deferred（见文件头 deferred ①）。
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
 * @note       载荷为 `XDate*`（借用，指向发射期有效存储，槽内只读），
 *             与 XDateTimeEdit_dateChanged_signal / XDateEdit 的日期载荷
 *             同口径（Qt 形参是 `const QDate&`）。队列连接跨线程时槽应
 *             自行复制日期。
 */
void* XCalendarWidget_clicked_signal(XCalendarWidget* self, const XDate* date);
/**
 * @brief      激活信号（真发射）。
 * @note       载荷口径同 clicked（`XDate*` 借用指针）。
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

#endif /* XGUI_ON */

#endif /* XCALENDARWIDGET_H */
