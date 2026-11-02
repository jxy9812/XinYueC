/**
 * @file       HsfPages.h
 * @brief      页面注册表：页索引枚举 / 页名查询 / 导航树 / 导航契约。
 * @details    堆叠下标=页索引（HsfUi.c builders 表同序）。菜单树：
 *             主界面 / 菜单（主界面 监视(数字量 模拟量 变频器)
 *             数据(历史数据 日志 报警) 设置(本地设置 远端设置
 *             网络设置 悬浮窗设置 窗口设置)）。
 *             各页模块只依赖本头文件的枚举与导航类型，不感知主窗。
 */
#ifndef HSFPAGES_H
#define HSFPAGES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"

/* ---- 页面索引（堆叠下标=页索引；HsfUi.c builders 表同序） ---- */
typedef enum HsfPageIndex
{
    HSF_PAGE_HOME = 0,        /**< 主界面。 */
    HSF_PAGE_MENU,            /**< 主菜单（紧凑形态导航承载页）。 */
    HSF_PAGE_MONITOR_MENU,    /**< 监视 子菜单。 */
    HSF_PAGE_MONITOR_DIGITAL, /**< 监视-数字量。 */
    HSF_PAGE_MONITOR_ANALOG,  /**< 监视-模拟量。 */
    HSF_PAGE_MONITOR_VFD,     /**< 监视-变频器。 */
    HSF_PAGE_DATA_MENU,       /**< 数据 子菜单。 */
    HSF_PAGE_DATA_HISTORY,    /**< 数据-历史数据。 */
    HSF_PAGE_DATA_LOG,        /**< 数据-日志。 */
    HSF_PAGE_DATA_ALARM,      /**< 数据-报警。 */
    HSF_PAGE_SETTINGS_MENU,   /**< 设置 子菜单。 */
    HSF_PAGE_SETTINGS_LOCAL,  /**< 设置-本地设置。 */
    HSF_PAGE_SETTINGS_REMOTE, /**< 设置-远端设置。 */
    HSF_PAGE_SETTINGS_NETWORK,/**< 设置-网络设置（自 XGuiDemo 移植）。 */
    HSF_PAGE_SETTINGS_OVERLAY,/**< 设置-悬浮窗设置（自 XGuiDemo 移植）。 */
    HSF_PAGE_SETTINGS_WINDOW, /**< 设置-窗口设置（自 XGuiDemo 移植）。 */
    HSF_PAGE_COUNT            /**< 页总数（哨兵）。 */
} HsfPageIndex;

/** @brief 页面创建函数契约：建页挂 parent，返回页根（堆块随窗级联释放）。 */
typedef XWidget* (*HsfPageCreateFn)(XWidget* parent,
                                    void (*navFn)(void*, int),
                                    void* userData);

/** @brief 页自适应（窄宽分档/形态重排）。
 *  @param page 页根控件。
 *  @param compact true=紧凑/竖屏形态（false=宽屏）。 */
typedef void (*HsfPageAdaptFn)(XWidget* page, bool compact);

/** @brief 导航回调（userData=归属主窗，index=目标页索引）。 */
typedef void (*HsfMenuNavFn)(void* userData, int index);

/** @brief 菜单条目（按钮文本 + 目标页索引）。 */
typedef struct HsfMenuEntry
{
    const char* name;      /**< 按钮文本（UTF-8，借用指针）。 */
    int         pageIndex; /**< 目标页索引（透传导航回调）。 */
} HsfMenuEntry;

/** @brief 导航树节点（左侧停靠导航面板与紧凑菜单共用同一棵树）。 */
typedef struct HsfNavNode
{
    const char* caption;  /**< 显示文本（UTF-8，借用指针）。 */
    int         pageIndex;/**< 目标页索引；<0=纯分组标题（不可点）。 */
    int         childOf;  /**< 父节点下标；-1=顶层。 */
    bool        expandable;/**< 是否有子项（可展开/折叠）。 */
} HsfNavNode;

/**
 * @brief      取页名（标题栏/状态栏文本同源）。
 * @param index 页索引；越界返回 "?"。
 * @return     页名借用指针（UTF-8）。
 */
const char* HsfPages_name(int index);

/**
 * @brief      取导航树（左侧停靠面板/紧凑菜单共用）。
 * @param countOut 节点数输出（可为 NULL）。
 * @return     节点数组借用指针（静态表，恒有效）。
 */
const HsfNavNode* HsfPages_navTree(int* countOut);

/**
 * @brief      取节点所属分类下标（顶层节点自身为分类）。
 * @param tree  节点数组。
 * @param count 节点数。
 * @param index 目标节点下标。
 * @return     分类节点下标；无归属返回 -1。
 */
int HsfPages_categoryOf(const HsfNavNode* tree, int count, int index);

/**
 * @brief      全局导航（切页到目标索引；由 HsfUi.c 实现）。
 * @details    移植自 XGuiDemo 的页面内部需要切页时走此入口（经
 *             HsfDemoCompat 收敛），避免页面直接依赖主窗实现。
 * @param user 归属主窗指针。
 * @param index 目标页索引。
 */
void HsfPages_gotoPage(void* user, int index);

/**
 * @brief      状态栏文本反馈（移植页用；由 HsfUi.c 实现）。
 * @param user 归属主窗指针。
 * @param text 状态文本（UTF-8）。
 */
void HsfPages_setStatus(void* user, const char* text);

#ifdef __cplusplus
}
#endif

#endif /* HSFPAGES_H */
