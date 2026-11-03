/**
 * @file       HsfDataBar.h
 * @brief      数据域控制工具条：时间范围/通道/列筛选/导出/视图切换。
 * @details    历史数据/日志/报警三个表格页顶部的通用控制条（对标截图
 *             两行布局）：行1=起始/结束时间 + 通道 + 列筛选 + 筛选/清除
 *             筛选 + 行数统计；行2=小数位数 + 导出格式 + 导出/重新加载/
 *             生成折线图 + 表格|图表切换。本阶段只建 UI 与控制 API
 *             （取值/赋值/动作回调），数据接入层后续经
 *             HsfDataBar_setActionCallback() 接线。
 */
#ifndef HSFDATABAR_H
#define HSFDATABAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "XBoxLayout.h"   /* 守卫宏 XLAYOUT_ON/XLAYOUT_BOX_ON 的定义源头。 */
#include "XDateTime.h"
#include "XDateTimeEdit.h"
#include "XComboBox.h"
#include "XSpinBox.h"
#include "XPushButton.h"
#include "XLabel.h"

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_BOX_ON && \
    XDATETIMEEDIT_ON && XCOMBOBOX_ON && XSPINBOX_ON

/**
 * @brief 工具条动作码（HsfDataBarActionFn 回调第二参）。
 */
typedef enum HsfDataBarAction
{
    HsfDataBarAction_Filter = 0,   /**< 筛选（按当前时间/通道/列条件）。 */
    HsfDataBarAction_ClearFilter,  /**< 清除筛选。 */
    HsfDataBarAction_Export,       /**< 导出（格式见导出格式下拉）。 */
    HsfDataBarAction_Reload,       /**< 重新加载。 */
    HsfDataBarAction_GenerateChart,/**< 生成折线图。 */
    HsfDataBarAction_ViewTable,    /**< 切到表格视图。 */
    HsfDataBarAction_ViewChart     /**< 切到图表视图。 */
} HsfDataBarAction;

/**
 * @brief 工具条动作回调（userData, HsfDataBarAction）。
 */
typedef void (*HsfDataBarActionFn)(void* userData, int action);

/**
 * @brief      创建控制工具条（堆分配，挂 parent，随主窗级联释放）。
 * @param parent 父控件（页容器）。
 * @return 工具条控件借用指针（各 API 首参直传本值）；失败 NULL。
 */
XWidget* HsfDataBar_create(XWidget* parent);

/**
 * @brief      注册动作回调（筛选/清除筛选/导出/重新加载/生成折线图/
 *             视图切换统一走此口；NULL=仅维护内部状态）。
 */
void HsfDataBar_setActionCallback(XWidget* bar, HsfDataBarActionFn fn,
                                  void* userData);

/* ---- 通道 / 列筛选 ---- */

/** @brief 重设通道下拉项（UTF-8 数组；内部深拷贝；NULL/0=清空）。 */
void HsfDataBar_setChannelItems(XWidget* bar, const char* const* items,
                                int count);
/** @brief 当前通道索引（-1 空）。 */
int HsfDataBar_channelIndex(const XWidget* bar);
/** @brief 重设列筛选下拉项（同上）。 */
void HsfDataBar_setColumnFilterItems(XWidget* bar, const char* const* items,
                                     int count);
/** @brief 当前列筛选索引（-1 空）。 */
int HsfDataBar_columnFilterIndex(const XWidget* bar);

/* ---- 时间范围 ---- */

void HsfDataBar_setStartDateTime(XWidget* bar, const XDateTime* dt);
void HsfDataBar_setEndDateTime(XWidget* bar, const XDateTime* dt);
/** @brief 读取起始/结束时间（值返回；bar NULL 返回无效对象）。 */
XDateTime HsfDataBar_startDateTime(const XWidget* bar);
XDateTime HsfDataBar_endDateTime(const XWidget* bar);

/* ---- 小数位 / 导出格式 / 行数 ---- */

/** @brief 显示小数位数（SpinBox 当前值，0~6）。 */
int HsfDataBar_decimalDigits(const XWidget* bar);
void HsfDataBar_setDecimalDigits(XWidget* bar, int digits);
/** @brief 导出格式索引（0=Excel (.xlsx)，1=CSV (.csv)）。 */
int HsfDataBar_exportFormatIndex(const XWidget* bar);
void HsfDataBar_setExportFormatIndex(XWidget* bar, int index);
/** @brief 行数统计文本（右侧「共 N 行」；负数显示 0）。 */
void HsfDataBar_setTotalRows(XWidget* bar, long long rows);

/* ---- 视图切换（表格/图表） ---- */

/** @brief 当前视图（0=表格，1=图表；即右侧切换钮选中态）。 */
int HsfDataBar_viewMode(const XWidget* bar);
/** @brief 设置视图选中态（只翻按钮，不发动作——防回调环）。 */
void HsfDataBar_setViewMode(XWidget* bar, int mode);

/**
 * @brief      形态自适应（窄屏隐藏各段说明标签；帧泵每循环调用，幂等）。
 */
void HsfDataBar_adapt(XWidget* bar, bool compact);

#endif /* 守卫 */

#ifdef __cplusplus
}
#endif

#endif /* HSFDATABAR_H */
