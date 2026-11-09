/**
 * @file       HsfDataBar.h
 * @brief      数据域控制工具条：时间范围/通道/列筛选/导出/视图切换。
 * @details    历史数据/日志/报警三个表格页顶部的通用控制条（对标截图
 *             两行布局）：行1=起始/结束时间 + 通道 + 列筛选 + 筛选/清除
 *             筛选 + 行数统计；行2=小数位数 + 导出格式 + 导出/重新加载/
 *             生成折线图 + 表格|图表切换。三个表格页各建一套（同一份
 *             实现、各自一份实例），互不影响；数据接入经
 *             HsfDataBar_setActionCallback() 接线。
 *             下拉/微调框改动也会回调（ChannelChanged/ColumnFilter
 *             Changed/DecimalsChanged），页面据此即时重排显示；
 *             编程式 set* 期间不发回调（m_suppress 防回调环）。
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
    HsfDataBarAction_ViewChart,    /**< 切到图表视图。 */
    HsfDataBarAction_ChannelChanged,     /**< 通道下拉改了（值=新索引）。 */
    HsfDataBarAction_ColumnFilterChanged,/**< 列筛选下拉改了。 */
    HsfDataBarAction_DecimalsChanged,    /**< 小数位数改了。 */
    HsfDataBarAction_MaxPointsChanged    /**< 每图例最多点数改了（图表降采样上限）。 */
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
/** @brief 重设通道索引（编程式不发变更回调）。 */
void HsfDataBar_setChannelIndex(XWidget* bar, int index);
/** @brief 重设列筛选下拉项（同上）。 */
void HsfDataBar_setColumnFilterItems(XWidget* bar, const char* const* items,
                                     int count);
/** @brief 当前列筛选索引（-1 空）。 */
int HsfDataBar_columnFilterIndex(const XWidget* bar);
/** @brief 重设列筛选索引（-1/0=全部；编程式不发变更回调）。 */
void HsfDataBar_setColumnFilterIndex(XWidget* bar, int index);

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

/* ---- 图表降采样上限（每图例最多可见点数） ---- */

/** @brief 每图例最多点数的缺省值（超限等距抽样；对齐"趋势可见"口径）。 */
#define HSFDATABAR_MAX_POINTS_DEFAULT  500
/** @brief 每图例最多点数的可选下限（低于此值趋势会失真）。 */
#define HSFDATABAR_MAX_POINTS_MIN      50
/** @brief 每图例最多点数的可选上限。 */
#define HSFDATABAR_MAX_POINTS_MAX      5000

/** @brief 当前「每图例最多点数」（SpinBox 值；bar 为 NULL 返回缺省）。 */
int HsfDataBar_maxPoints(const XWidget* bar);
/** @brief 重设「每图例最多点数」（夹到 MIN..MAX；编程式不发回调）。 */
void HsfDataBar_setMaxPoints(XWidget* bar, int points);

/* ---- 分页控件（并入本工具条行 2；页对象接线，工具条只承载） ---- */

/**
 * @brief      把页面的分页/关键字控件挂进工具条行 2。
 * @details    [为什么放在工具条里] 这排控件原先由各页在表格下方自建一条
 *             XHBoxLayout，表格高度被压掉一整行。并入顶部工具条第二行后
 *             表格独占剩余高度。
 *             [父子关系：挂接后归工具条] 控件建页时以页根为父，但
 *             insertWidget → XLayout_linkItem 会沿布局链上溯到工具条宿主
 *             并 XWidget_setParent_2 到它（对标 Qt addWidget 的 reparent
 *             语义）——**挂接之后控件的父对象是工具条，不是页根**，随
 *             工具条（页的子对象）级联析构。页仍按自己结构体里的成员
 *             指针访问这些控件（内嵌对象，不随父链改变地址）。
 *             [信号 receiver] 因控件对象地址不变，信号仍接在控件自身/页
 *             对象上；本函数不碰任何信号连接。
 * @param bar    工具条（HsfDataBar_create 返回值）。
 * @param widgets 控件数组（NULL 项跳过），插在**伸展条之后、两个视图
 *                切换钮之前**（插入点 = 行 2 条目数-2）：伸展条吃掉左侧
 *                剩余宽度，这排控件与「表格/图表」钮连成右靠段。
 * @param count  数组长度。
 * @param rightWidgets 右靠控件数组（可 NULL/0），紧跟 widgets 之后插入。
 * @param rightCount   右靠数组长度。
 * @note  幂等：重复调用先用 XLayout_removeWidget 摘掉旧条目再插，不会
 *        出现重复条目或悬空父链。
 */
void HsfDataBar_attachRow2(XWidget* bar, XWidget** widgets, int count,
                           XWidget** rightWidgets, int rightCount);

/* [无 detachRow2] 曾有的 HsfDataBar_detachRow2 全库零调用，已删（详见
 * HsfDataBar.c 同位置注记）。挂接经 XLayout_linkItem 把控件 reparent 成
 * 工具条的子对象，之后随工具条级联销毁，不存在"控件还在、需先摘条目"
 * 的生命周期；重新挂接（attachRow2 再次调用）本身已幂等，先摘旧条目
 * 再插。 */

/* ---- 视图切换（表格/图表） ---- */

/** @brief 当前视图（0=表格，1=图表；即右侧切换钮选中态）。 */
int HsfDataBar_viewMode(const XWidget* bar);
/** @brief 设置视图选中态（只翻按钮，不发动作——防回调环）。 */
void HsfDataBar_setViewMode(XWidget* bar, int mode);

/**
 * @brief      形态自适应（按实际可用宽分五档逐级收窄：⓪常规 → ①收说明
 *             标签 → ②收控件最小宽+行距 → ③隐「共 N 行」→ ④行1/行2 翻
 *             竖排；帧泵每循环调用，幂等）。
 */
void HsfDataBar_adapt(XWidget* bar, bool compact);

/**
 * @brief 按百分比缩放工具条字号与控件高（随页字号实时重放）。
 * @param bar 工具条（HsfDataBar_create/attach 返回值）。
 * @param pct 百分比（50–200；100=原始）。
 */
void HsfDataBar_applyFont(XWidget* bar, int pct);

#endif /* 守卫 */

#ifdef __cplusplus
}
#endif

#endif /* HSFDATABAR_H */
