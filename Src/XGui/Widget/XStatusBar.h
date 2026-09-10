/**
 * @file       XStatusBar.h
 * @brief      XStatusBar 状态栏控件（对标 Qt 6.8 QStatusBar 全部公共 API）。
 * @details    功能范围：
 *             - 消息：showMessage(text, timeout)/clearMessage/
 *               currentMessage；timeout>0 时毫秒后自动清除（内部定时器，
 *               对标 QStatusBar::showMessage 的超时语义）；消息变化发射
 *               messageChanged(text)；
 *             - 常驻控件：addWidget/insertWidget（普通区，消息显示时被
 *               隐藏，对标 Qt 语义）与 addPermanentWidget/
 *               insertPermanentWidget（永久区，永不被消息遮挡）；
 *             - removeWidget 移除控件（控件归调用方所有，不被销毁）；
 *             - sizeGrip：setSizeGripEnabled/isSizeGripEnabled（默认
 *               true，右下角尺寸手柄占位；手柄绘制为简化实现）；
 *             - 绘制：消息文本左对齐绘制，永久区控件右对齐排布；
 *               顶部 1px 分隔线（对标 QStatusBar 外观简化）。
 * @note       模块总开关 XSTATUSBAR_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON。
 * @author     XinYueC 团队
 */
#ifndef XSTATUSBAR_H
#define XSTATUSBAR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"

#if XWIDGET_ON && XSTATUSBAR_ON

XCLASS_DEFINE_BEGING(XStatusBar)
XCLASS_DEFINE_EXTEND_END(XStatusBar, XWidget)

/**
 * @brief      XStatusBar 控件对象；m_base 必须是第一个成员。
 */
typedef struct XStatusBar
{
    XWidget m_base;              /**< 基类成员；必须是第一个。 */
    XVector* m_items;            /**< 普通区条目（XStatusBarItem*，拥有）。 */
    XVector* m_permanents;       /**< 永久区条目（XStatusBarItem*，拥有）。 */
    char m_currentMessage[512];  /**< 当前临时消息（UTF-8）。 */
    int m_tempTimeout;           /**< 当前消息超时（毫秒；0=不自动清除）。 */
    XTimerId m_messageTimer;     /**< 消息超时定时器。 */
    bool m_sizeGripEnabled;      /**< 尺寸手柄开关（默认 true）。 */
} XStatusBar;

/* ==================== 生命周期 ==================== */

XVtable* XStatusBar_class_init(void);
void XStatusBar_init(XStatusBar* self, XWidget* parent, XWidgetFlags flags);
#define XStatusBar_create(parent, flags) XStatusBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XStatusBar* XStatusBar_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags);
#define XStatusBar_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XStatusBar_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 常驻控件（对标 QStatusBar public API） ==================== */

/** @brief 追加普通区控件（stretch 语义对标 addWidget，第一版仅存储）。 */
/**
 * @brief      添加控件。
 */
void XStatusBar_addWidget(XStatusBar* self, XWidget* widget, int stretch);
/** @brief 在 index 处插入普通区控件，返回实际索引。 */
/**
 * @brief      插入控件。
 */
int XStatusBar_insertWidget(XStatusBar* self, int index, XWidget* widget,
                            int stretch);
/** @brief 追加永久区控件（消息显示时不被遮挡）。 */
/**
 * @brief      添加永久控件。
 */
void XStatusBar_addPermanentWidget(XStatusBar* self, XWidget* widget,
                                   int stretch);
/** @brief 在 index 处插入永久区控件，返回实际索引。 */
/**
 * @brief      插入永久控件。
 */
int XStatusBar_insertPermanentWidget(XStatusBar* self, int index,
                                     XWidget* widget, int stretch);
/** @brief 移除控件（普通区/永久区都查找；控件归调用方，不销毁）。 */
/**
 * @brief      移除控件。
 */
void XStatusBar_removeWidget(XStatusBar* self, XWidget* widget);

/* ==================== 尺寸手柄 ==================== */

/** @brief 查询尺寸手柄开关（默认 true）。 */
/**
 * @brief      获取尺寸把手开关。
 */
bool XStatusBar_isSizeGripEnabled(const XStatusBar* self);
/** @brief 设置尺寸手柄开关并重绘。 */
/**
 * @brief      设置尺寸把手开关。
 */
void XStatusBar_setSizeGripEnabled(XStatusBar* self, bool on);

/* ==================== 消息槽 ==================== */

/** @brief 显示临时消息（timeout>0 毫秒后自动清除；0 表示保持到下次
 *         调用；对标 showMessage）。 */
/**
 * @brief      显示消息。
 */
void XStatusBar_showMessage(XStatusBar* self, const char* utf8,
                            int timeout);
/** @brief 清除临时消息并恢复普通区控件（对标 clearMessage）。 */
/**
 * @brief      清除消息。
 */
void XStatusBar_clearMessage(XStatusBar* self);
/** @brief 查询当前临时消息文本。 */
/**
 * @brief      获取当前临时消息。
 */
const char* XStatusBar_currentMessage(const XStatusBar* self);

/* ==================== 信号 ==================== */

/** @brief 临时消息文本变化信号（含清除；对标 messageChanged）。 */
/**
 * @brief      消息变化信号（真发射）。
 */
void* XStatusBar_messageChanged_signal(XStatusBar* self, const char* text);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XSTATUSBAR_ON */

#ifdef __cplusplus
}
#endif
#endif /* XSTATUSBAR_H */
