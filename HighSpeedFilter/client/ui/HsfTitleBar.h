/**
 * @file       HsfTitleBar.h
 * @brief      顶部标题栏：☰ 菜单图标 + 居中标题 + 右侧时钟/温度/湿度。
 * @details    仿 XGuiWindowDemo 品牌条：深蓝底 + 左端 ☰ 触控区；右侧
 *             依次为**时钟**、**温度**、**湿度**（温度/湿度对外提供
 *             设置与获取 API，默认值 0）；标题在 ☰ 区与右侧块之间**居中**。
 *             时钟由 1s 定时器驱动刷新（格式随栏宽自适应：宽栏显示
 *             「日期+时间」，窄栏只显示「时分秒」）。
 */
#ifndef HSFTITLEBAR_H
#define HSFTITLEBAR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "XLabel.h"

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON

/** @brief 标题栏高度（chrome 度量单一事实源：主窗布局/悬浮层锚定共用）。 */
#define HSF_TITLE_BAR_H 40

/** @brief ☰ 点击回调（userData=归属主窗指针）。 */
typedef void (*HsfTitleBarMenuFn)(void* userData);

/** @brief 全局运行模式钮点击回调（userData=注册令牌）。 */
typedef void (*HsfTitleBarModeFn)(void* userData);

XCLASS_DEFINE_BEGING(HsfTitleBar)
XCLASS_DEFINE_EXTEND_END(HsfTitleBar, XWidget)

/** @brief 顶部标题栏对象；m_base 必须是第一个成员。 */
typedef struct HsfTitleBar
{
    XWidget       m_base;      /**< 标题栏容器；必须是第一个成员。 */
    XLabel*       m_title;     /**< 居中标题标签（子控件，随栏析构）。 */
    XLabel*       m_date;      /**< 右上角日期行（年月日）。 */
    XLabel*       m_clock;     /**< 右上角时间行（时分秒）。 */
    XLabel*       m_modeBtn;   /**< 全局运行模式钮（手动/自动，右侧常驻）。 */
    XLabel*       m_autoChip;  /**< 自动运行状态片（自动·运行中/已停止；手动隐藏）。 */
    XLabel*       m_healthLbl; /**< 设备健康指示（正常/故障N/离线N）。 */
    XLabel*       m_temp;      /**< 温度标签。 */
    XLabel*       m_humid;     /**< 湿度标签。 */
    HsfTitleBarMenuFn m_menuFn;/**< ☰ 点击回调。 */
    void*        m_menuUserData; /**< 回调用户数据（归属主窗）。 */
    double       m_temperature; /**< 温度（℃），默认 0。 */
    double       m_humidity;    /**< 湿度（%），默认 0。 */
    bool         m_tempValid;   /**< 温度数据有效（false 时显示 "--"）。 */
    bool         m_humValid;    /**< 湿度数据有效（false 时显示 "--"）。 */
    bool         m_narrowTime;  /**< 窄栏时间格式（只时分秒）。 */
    /* ---- 全局运行模式（服务端模式；控制全部设备） ---- */
    bool         m_modeKnown;   /**< 是否已从服务端拿到模式。 */
    bool         m_modeAuto;    /**< 服务端是否自动模式。 */
    HsfTitleBarModeFn m_modeFn; /**< 模式钮点击回调。 */
    void*        m_modeUserData;/**< 回调用户数据。 */
} HsfTitleBar;

/**
 * @brief      初始化标题栏（蓝底 + ☰ + 居中标题 + 右侧时钟/温湿度）。
 * @param self 待初始化对象；不可为 NULL。
 * @param parent 父控件（主窗根）。
 */
void HsfTitleBar_init(HsfTitleBar* self, XWidget* parent);

/**
 * @brief      刷新时钟文本（供主窗 1s 定时器驱动）。
 * @param self 目标标题栏。
 */
void HsfTitleBar_refreshClock(HsfTitleBar* self);

/**
 * @brief      设置标题文本（UTF-8，居中显示）。
 * @param self 目标标题栏。
 * @param utf8 标题文本（借用指针）。
 */
void HsfTitleBar_setTitle(HsfTitleBar* self, const char* utf8);

/**
 * @brief      注册 ☰ 点击回调。
 * @param self 目标标题栏。
 * @param fn   点击回调；NULL=图标不可点。
 * @param userData 透传给回调的用户数据。
 */
void HsfTitleBar_setMenuHandler(HsfTitleBar* self, HsfTitleBarMenuFn fn,
                                void* userData);

/**
 * @brief      设置温度（℃）并刷新显示。
 * @param self 目标标题栏。
 * @param celsius 温度值（默认 0）。
 */
void HsfTitleBar_setTemperature(HsfTitleBar* self, double celsius);

/**
 * @brief      取温度（℃）。
 * @param self 目标标题栏。
 * @return 温度值（默认 0）。
 */
double HsfTitleBar_temperature(const HsfTitleBar* self);

/**
 * @brief      设置湿度（%）并刷新显示。
 * @param self 目标标题栏。
 * @param percent 湿度值（默认 0）。
 */
void HsfTitleBar_setHumidity(HsfTitleBar* self, double percent);

/**
 * @brief      取湿度（%）。
 * @param self 目标标题栏。
 * @return 湿度值（默认 0）。
 */
double HsfTitleBar_humidity(const HsfTitleBar* self);

/**
 * @brief 一次设置温湿度并重排右侧列（推荐入口）。
 * @param self 目标标题栏；NULL 时无操作。
 * @param celsius 温度（℃）。
 * @param percent 湿度（%）。
 * @param tempValid 温度有效（false 时该格显示 "--"）。
 * @param humValid 湿度有效（false 时该格显示 "--"）。
 * @note  数值位数变化会改右侧列宽，单独调 setTemperature/setHumidity 不重排
 *        会与居中标题重叠；本函数刷文本后统一重排，一帧只触发布局一次。
 */
/**
 * @brief 注册全局运行模式钮点击回调。
 * @param self 标题栏；fn 回调；userData 令牌。
 * @note  手动/自动是**全局状态**（服务端一档管控全部设备），故置于顶栏。
 */
void HsfTitleBar_setModeHandler(HsfTitleBar* self, HsfTitleBarModeFn fn,
                               void* userData);

/**
 * @brief 更新顶栏设备健康指示。
 * @param self 标题栏。
 * @param known 是否已收到健康数据（false 显示占位）。
 * @param fault 故障设备数；offline 离线设备数。
 */
void HsfTitleBar_setHealth(HsfTitleBar* self, bool known, int fault, int offline);

/**
 * @brief 更新顶栏显示的运行模式。
 * @param self 标题栏。
 * @param known 是否已知（false 时显示占位）。
 * @param autoMode true=自动（拒绝远程控制）false=手动。
 */
void HsfTitleBar_setRunMode(HsfTitleBar* self, bool known, bool autoMode);

/**
 * @brief 设自动运行状态片（"自动模式"与"控制器在跑"是两件事）。
 * @param known 服务端是否上报了 autoRunning（false=未知）。
 * @param running 控制器是否在跑（known=true 时有效）。
 * @param autoMode 当前是否自动模式（手动模式隐藏该片）。
 * @details 文案：自动·运行中 / 自动·已停止 / 自动·未知（对齐 Python
 *          客户端标题栏的 autoRunning 状态片）。
 */
void HsfTitleBar_setAutoRunning(HsfTitleBar* self, bool known, bool running,
                                 bool autoMode);

void HsfTitleBar_setClimate(HsfTitleBar* self, double celsius, double percent,
                            bool tempValid, bool humValid);

#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif

#endif /* HSFTITLEBAR_H */
