/**
 * @file       HsfMultiSelect.h
 * @brief      公共「多选下拉」组件：按钮（文字=勾选汇总）+ 勾选浮层。
 * @details    【全库唯一一份多选下拉实现】原是 HsfDataBar.c 的私有实现
 *             （浮层 HsfColPopup + 组件 HsfBarMultiSel），2026-11-12 抽成
 *             公共模块：数据工具条的「列筛选 / 图例」与远端设置页共用同
 *             一份机制，不再各写一份（项目铁律：框架/仓库已有的绝不手搓
 *             第二份）。
 *             结构照搬 Python 客户端 data_toolbar.py 的 MultiCheckCombo
 *             （QPushButton + QFrame(Qt.Popup) + QScrollArea + 每行
 *             QCheckBox；cb.toggled 只改勾选、**不收浮层**，点浮层外自动收）。
 *             C 侧同构：XPushButton + XFrame(XWindowType_Popup) +
 *             XScrollArea + 每行 XCheckBox。
 *             **不用 XListView**（其 viewport 会吞掉本体的鼠标事件 ⇒ 点行
 *             没反应；grabMouse 又会让子滚动条收不到点击）。
 *
 * 所有权契约（调用方必读）：
 *   - **组件结构体由调用方持有**：可内嵌进调用方自己的结构体（随其一起
 *     分配/释放），也可单独分配后自行管理；create() 内部会 memset 清零该
 *     结构体，故调用方**不需要**事先清零。
 *   - **按钮本体归父控件**：按钮是 create() 时传入的 parent 的子控件，随
 *     parent 的控件树**级联销毁**；组件**不负责**删除按钮，destroy() 里也
 *     不删（按钮可能被调用方放进布局，提前删会在布局里留下悬垂条目）。
 *   - **浮层与项名表归组件**：勾选浮层是无父顶层的 Popup 窗口（不随控件
 *     树级联）、项名是堆上的 XString ⇒ 两样都由 destroy() 释放。
 *   - **文案与回调上下文是借用**：cfg 里的 const char* 与 userData 由调用
 *     方保证存活到组件销毁（推荐用字符串字面量或长于组件生命周期的对象）。
 *   - **回调签名**：void (*HsfMultiSelectChangedFn)(void* userData,
 *     uint32_t mask)；mask = 勾选位掩码（位 i = 第 i 项；0 = 一个都没勾）。
 *     触发时机：① 用户在浮层里勾选；② 编程式 setMask 改了值（同值早退，
 *     不触发）。setItems() 重设项名表**不触发**回调（初始化口径）。
 *
 * 按钮尺寸/字号**由调用方按需设置**（组件内不写死按钮的任何像素）：拿到
 * 按钮指针（HsfMultiSelect_button）后自行 XWidget_setFixedHeight /
 * setMinimumWidth / XWidget_setFont，组件只负责行为与文案口径。
 */
#ifndef HSFMULTISELECT_H
#define HSFMULTISELECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XWidget.h"
#include "XPushButton.h"   /* 组件内嵌按钮（结构体公开，调用方可内嵌本组件）。 */
#include "XBoxLayout.h"    /* 守卫宏 XLAYOUT_ON/XLAYOUT_BOX_ON 的定义源头。 */
#include "XString.h"       /* 项名表（堆 XString，组件拥有）。 */
#include <stdint.h>
#include <stdbool.h>

/* [守卫与实现同口径] 本模块的实现（HsfMultiSelect.c）依赖 PushButton/
 * CheckBox/ScrollArea/Frame/BoxLayout —— 头文件的声明守卫必须与实现一致，
 * 否则会出现「声明在、符号不在」的链接错误。 */
#if XWIDGET_ON && XPUSHBUTTON_ON && XCHECKBOX_ON && XSCROLLAREA_ON && \
    XABSTRACTSCROLLAREA_ON && XFRAME_ON && XLAYOUT_ON && XLAYOUT_BOX_ON

/**
 * @brief 项数上限（勾选浮层的行数上限；历史页 16 列、报警页 4 列）。
 */
#define HSF_MULTISELECT_MAX_ITEMS 32

/**
 * @brief 勾选浮层（不透明类型；实现见 HsfMultiSelect.c）。
 * @details  XFrame(XWindowType_Popup) + XScrollArea + 每行一个 XCheckBox；
 *           浮层只有**一份机制**，各组件实例弹出时挂到谁身上就读写谁的
 *           掩码/名字表。
 */
typedef struct HsfMultiSelectPopup HsfMultiSelectPopup;

/**
 * @brief 多选组件的变更回调（userData, mask）。
 * @param userData 调用方上下文（create 时注入，原样回传）。
 * @param mask     变更后的勾选位掩码（位 i = 第 i 项）。
 */
typedef void (*HsfMultiSelectChangedFn)(void* userData, uint32_t mask);

/**
 * @brief 多选组件的创建参数（一次性注入；其中字符串均为**借用**）。
 */
typedef struct HsfMultiSelectConfig
{
    const char* const* items;   /**< 项名表（UTF-8；create 时深拷贝；NULL/0=无项）。 */
    int         count;           /**< 项数（超过 HSF_MULTISELECT_MAX_ITEMS 截断）。 */
    uint32_t    initialMask;     /**< 初始勾选掩码（位 i = 第 i 项）。 */
    bool        allowNone;       /**< 允许全不勾（false = 至少留一项）。 */
    int         minChecked;      /**< 至少留几项（allowNone=false 时下限抬到 1）。 */
    const char* emptyText;       /**< 无项占位文案（按钮初始文字；不含箭头）。 */
    const char* allText;         /**< 全勾汇总文案（含「  ∨」下拉符；字库无 U+25BE）。 */
    const char* noneText;        /**< 全不勾汇总文案（含箭头）；兼作「有掩码但名表
                                     全空」（页面传空名表的病态输入）时的回落。 */
    HsfMultiSelectChangedFn fn;  /**< 变更回调（可 NULL = 不回调）。 */
    void*       userData;        /**< 回调上下文（借用；原样回传给 fn）。 */
} HsfMultiSelectConfig;

/**
 * @brief 多选组件（按钮 + 勾选浮层 + 掩码 + 项名表）。
 * @details 结构体公开，是为了让调用方**内嵌**本组件（免掉堆分配与释放
 *         次序）；字段语义见各行注记。除 m_btn 外的字段均为组件内部状态，
 *          请通过 HsfMultiSelect_* 接口读写。
 */
typedef struct HsfMultiSelect
{
    XPushButton m_btn;                          /**< 按钮（**父控件的子对象**；随父控件级联销毁）。 */
    HsfMultiSelectPopup* m_popup;               /**< 当前勾选浮层（拥有；点外面/析构时收）。 */
    uint32_t    m_mask;                         /**< 勾选掩码（位 i = 第 i 项）。 */
    int         m_count;                        /**< 项数（浮层行数）。 */
    XString*    m_names[HSF_MULTISELECT_MAX_ITEMS]; /**< 项名（拥有；汇总文字用）。 */
    const char* m_emptyText;                    /**< 无项占位文案（借用）。 */
    const char* m_allText;                      /**< 全勾汇总文案（借用）。 */
    const char* m_noneText;                     /**< 全不勾汇总文案（借用）。 */
    int         m_minChecked;                   /**< 至少留几项（0 = 允许全不勾）。 */
    HsfMultiSelectChangedFn m_fn;               /**< 变更回调（借用）。 */
    void*       m_userData;                     /**< 回调上下文（借用）。 */
    bool        m_created;                      /**< create 成功位（destroy 幂等依据：
                                                    第二次 destroy 不再解信号连接）。 */
} HsfMultiSelect;

/**
 * @brief      创建多选组件（在调用方提供的结构体上初始化）。
 * @param sel   组件结构体（调用方持有；函数内部会 memset 清零）。
 * @param parent 按钮的父控件（按钮是它的子控件，随它级联销毁）。
 * @param cfg   创建参数（字符串与回调上下文均为借用）。
 * @return 成功 true；参数非法或按钮创建失败 false。
 * @note  按钮的**尺寸/字号不在组件内设定**（不写死像素）：create 之后由
 *        调用方拿 HsfMultiSelect_button() 自行设置（高度/最小宽/字体）。
 *        按钮的**初始文字**= cfg.emptyText（无项占位文案），项名表非空时
 *        立即按掩码刷新成汇总文案。
 */
bool HsfMultiSelect_create(HsfMultiSelect* sel, XWidget* parent,
                           const HsfMultiSelectConfig* cfg);

/**
 * @brief 销毁多选组件（释放组件自有资源：勾选浮层 + 项名表）。
 * @note  **不删按钮**：按钮归父控件的控件树，随父控件级联销毁（按钮可能
 *        在调用方的布局里，提前删会留下悬垂布局条目）。
 *        调用方应在**自己的销毁路径**里调用本函数（父控件析构时的
 *        destroyed 槽是合适的时机——此时浮层尚未随控件树级联，而项名表
 *        已无人引用）。
 */
void HsfMultiSelect_destroy(HsfMultiSelect* sel);

/**
 * @brief 取组件的按钮控件（借用指针；归父控件所有，**不要释放**）。
 * @note  按钮的宽/高/字体由调用方按需设置。
 */
XWidget* HsfMultiSelect_button(const HsfMultiSelect* sel);

/**
 * @brief 当前勾选掩码（位 i = 第 i 项；0 = 一个都没勾）。
 */
uint32_t HsfMultiSelect_mask(const HsfMultiSelect* sel);

/**
 * @brief 重设勾选掩码（同值早退；不同值 ⇒ 刷新汇总文案 + 触发变更回调）。
 */
void HsfMultiSelect_setMask(HsfMultiSelect* sel, uint32_t mask);

/**
 * @brief 重设项名表（UTF-8 数组；内部深拷贝；NULL/0 = 清空）。
 * @param defaultAll true = 重设后默认全勾；false = 默认全不勾。
 * @note  重设后立即按新掩码刷新按钮汇总文案；**不发**变更回调
 *        （初始化口径，调用方随后自行读取掩码）。
 */
void HsfMultiSelect_setItems(HsfMultiSelect* sel, const char* const* items,
                             int count, bool defaultAll);

/**
 * @brief 当前项数（0 = 无项，浮层不弹、按钮显示占位文案）。
 */
int HsfMultiSelect_itemCount(const HsfMultiSelect* sel);

/**
 * @brief 换变更回调（可随时替换；userData 原样回传）。
 */
void HsfMultiSelect_setChangedCallback(HsfMultiSelect* sel,
                                       HsfMultiSelectChangedFn fn,
                                       void* userData);

#endif /* 守卫 */

#ifdef __cplusplus
}
#endif

#endif /* HSFMULTISELECT_H */
