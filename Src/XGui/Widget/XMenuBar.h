/**
 * @file       XMenuBar.h
 * @brief      XMenuBar 菜单栏控件（对标 Qt 6.8 QMenuBar 全部公共 API）。
 * @details    功能范围：
 *             - addMenu(XMenu*)/addMenu(title)：注册菜单并创建关联动作
 *               （动作文本=菜单标题；动作触发时弹出对应菜单）；
 *             - addSeparator/insertSeparator/insertMenu/clear；
 *             - addAction(text)/addAction_2(utf8)/insertAction/removeAction：
 *               便捷动作族（对标 QMenuBar::addAction/insertAction/
 *               removeAction）；
 *             - activeAction/setActiveAction、setDefaultUp/isDefaultUp；
 *             - 信号 triggered(action)/hovered(action)（转发出组内动作）；
 *             - 绘制：横排菜单标题文本（简化实现，无快捷键下划线）；
 *             - cornerWidget/left/right 角落控件占位 API（第一版仅存储）。
 *             菜单栏为顶层窗口/主窗口的组成部分，不拥有 XMenu 对象
 *             （菜单归调用方，与 Qt 相同）。
 * @note       动作所有权（对标 Qt 父子所有权语义）：由 addMenu/
 *             addSeparator/addAction/addAction_2/insertMenu/
 *             insertSeparator 创建的动作归菜单栏所有并随析构销毁；
 *             insertAction 注入的外部动作仅为借用，菜单栏不取得所有权；
 *             removeAction 仅把动作从菜单栏摘除、不释放对象，所有权
 *             归还调用方（与 QWidget::removeAction 一致）。逐项所有权
 *             由 m_actionOwned 平行标记记录。
 * @note       模块总开关 XMENUBAR_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XMENU_ON、XACTION_ON。
 * @author     XinYueC 团队
 */
#ifndef XMENUBAR_H
#define XMENUBAR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#if XMENU_ON
#include "XMenu.h"
#endif

#if XWIDGET_ON && XMENU_ON && XMENUBAR_ON

XCLASS_DEFINE_BEGING(XMenuBar)
XCLASS_DEFINE_EXTEND_END(XMenuBar, XWidget)

/**
 * @brief      XMenuBar 控件对象；m_base 必须是第一个成员。
 */
typedef struct XMenuBar
{
    XWidget m_base;         /**< 基类成员；必须是第一个。 */
    XVector* m_actions;     /**< 菜单栏动作数组（XAction*）；逐项所有权由
                                 m_actionOwned 标记（addAction 系创建的为
                                 拥有，insertAction 注入的为借用）。 */
    XVector* m_menus;       /**< 与动作顺序关联的菜单（XMenu*，借用）。 */
    XVector* m_actionOwned; /**< 与 m_actions 平行的所有权标记（bool）：
                                 true=菜单栏创建并拥有（析构/clear 时销毁），
                                 false=借用（insertAction 注入，析构/clear
                                 时仅摘除）。 */
    XAction* m_activeAction;/**< 当前激活动作（借用）。 */
    bool m_defaultUp;       /**< 弹出菜单默认向上（默认 false）。 */
    bool m_nativeMenuBar;   /**< 原生菜单栏（对标 isNativeMenuBar）。 */
    XWidget* m_cornerWidgetL; /**< 左上角控件（对标 cornerWidget(TopLeft)）。 */
    XWidget* m_cornerWidgetR; /**< 右上角控件（对标 cornerWidget(TopRight)）。 */
    XSize m_cachedHint;     /**< 尺寸提示缓存。 */
} XMenuBar;

/** @brief 菜单栏角落位置（对标 Qt::Corner，数值一致）。 */
typedef enum XMenuBarCorner
{
    XMenuBarCorner_TopLeft = 0x1,      /**< 左上角。 */
    XMenuBarCorner_TopRight = 0x2,     /**< 右上角。 */
    XMenuBarCorner_BottomLeft = 0x3,   /**< 左下角。 */
    XMenuBarCorner_BottomRight = 0x4   /**< 右下角。 */
} XMenuBarCorner;

/* ==================== 生命周期 ==================== */

XVtable* XMenuBar_class_init(void);
void XMenuBar_init(XMenuBar* self, XWidget* parent, XWidgetFlags flags);
#define XMenuBar_create(parent, flags) XMenuBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XMenuBar* XMenuBar_create_ex(XMemoryType memory, XWidget* parent,
                             XWidgetFlags flags);
#define XMenuBar_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XMenuBar_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 菜单管理（对标 QMenuBar public API） ==================== */

/** @brief 注册菜单并创建关联动作（对标 addMenu(QMenu*)；返回动作归
 *         菜单栏所有）。 */
XAction* XMenuBar_addMenu(XMenuBar* self, XMenu* menu);
/** @brief 以标题创建空菜单并注册（对标 addMenu(const QString&)；返回
 *         的菜单归调用方所有）。 */
XMenu* XMenuBar_addMenu_2(XMenuBar* self, const char* utf8Title);
/** @brief 追加分隔条动作（对标 addSeparator）。 */
XAction* XMenuBar_addSeparator(XMenuBar* self);
/**
 * @brief      以文本创建动作并追加（对标 QMenuBar::addAction(const QString&)）。
 * @details    Qt 6.8 中 QMenuBar 重载了 QWidget::addAction：创建带文本的
 *             新动作、加入菜单栏并返回该动作；XGui 中动作归菜单栏所有，
 *             该动作不关联子菜单（m_menus 不含对应项，与 addSeparator
 *             同一约定）。
 * @param      self 目标菜单栏。
 * @param      text 动作文本；可为 NULL（空文本）。
 * @return     新建动作借用指针（归菜单栏所有）；失败返回 NULL。
 */
XAction* XMenuBar_addAction(XMenuBar* self, const XString* text);
/**
 * @brief      以 UTF-8 文本创建动作并追加（对标 addAction 的字符串重载）。
 * @param      self 目标菜单栏。
 * @param      utf8Text UTF-8 动作文本；可为 NULL（空文本）。
 * @return     新建动作借用指针（归菜单栏所有）；失败返回 NULL。
 */
XAction* XMenuBar_addAction_2(XMenuBar* self, const char* utf8Text);
/** @brief 在 before 动作之前插入菜单（对标 insertMenu；before 为 NULL
 *         时等价追加）。 */
XAction* XMenuBar_insertMenu(XMenuBar* self, XAction* before, XMenu* menu);
/** @brief 在 before 动作之前插入分隔条（对标 insertSeparator）。 */
XAction* XMenuBar_insertSeparator(XMenuBar* self, XAction* before);
/**
 * @brief      在 before 动作之前插入既有动作（对标 QMenuBar::insertAction，
 *             即 QWidget::insertAction）。
 * @details    借用语义：菜单栏仅保存动作指针，不取得所有权（与 Qt 一致，
 *             除非动作以菜单栏为父对象，否则由调用方负责释放）；本函数
 *             创建的挂接关系随 removeAction 摘除。若 action 已在菜单栏中，
 *             先摘除再插入（移动语义，对标 Qt 的 contains+removeAction）。
 *             before 为 NULL 或不在菜单栏中时等价于追加到末尾。不关联
 *             子菜单（m_menus 不新增对应项）。
 * @param      self 目标菜单栏。
 * @param      before 参照动作借用指针；可为 NULL（等价追加）。
 * @param      action 待插入的动作借用指针；不可为 NULL。
 * @return     返回 action 本身便于链式使用；参数无效返回 NULL。
 */
XAction* XMenuBar_insertAction(XMenuBar* self, XAction* before,
                               XAction* action);
/**
 * @brief      从菜单栏摘除动作但不释放（对标 QMenuBar::removeAction，即
 *             QWidget::removeAction）。
 * @details    仅解除菜单栏对该动作的持有关系（从 m_actions/m_menus/
 *             m_actionOwned 同步移除，若为当前激活动作则一并清空），
 *             不调用任何释放接口；所有权归还调用方：
 *             - addAction/addMenu 等创建的动作：移除后归调用方负责
 *               （此后菜单栏不再销毁它，也不再弹出其关联菜单）；
 *             - insertAction 注入的动作：移除即恢复原有借用关系。
 *             action 不在菜单栏中时为无操作。
 * @param      self 目标菜单栏。
 * @param      action 待摘除的动作；可为 NULL（无操作）。
 * @return     无返回值。
 */
void XMenuBar_removeAction(XMenuBar* self, XAction* action);
/** @brief 清空全部动作（对标 clear；不销毁外部菜单对象）。 */
/**
 * @brief      清空内容（对标 Qt 同名槽）。
 */
void XMenuBar_clear(XMenuBar* self);
/** @brief 查询激活动作。 */
XAction* XMenuBar_activeAction(const XMenuBar* self);
/** @brief 设置激活动作。 */
void XMenuBar_setActiveAction(XMenuBar* self, XAction* action);
/** @brief 查询弹出方向默认向上（默认 false）。 */
bool XMenuBar_isDefaultUp(const XMenuBar* self);
/** @brief 设置弹出方向默认向上。 */
void XMenuBar_setDefaultUp(XMenuBar* self, bool up);
/** @brief 查询动作总数（动作+分隔条）。 */
int XMenuBar_actionCount(const XMenuBar* self);

/* ==================== 几何/角落/尺寸（对标 QMenuBar public API） ==== */

/** @brief 返回局部坐标处的动作（对标 QMenuBar::actionAt）。
 * @param self 目标菜单栏；可为 NULL。
 * @param pos 局部坐标借用指针；可为 NULL。
 * @return 命中返回对应动作；未命中或参数无效返回 NULL。
 */
XAction* XMenuBar_actionAt(const XMenuBar* self, const XPoint* pos);
/** @brief 返回动作在局部坐标中的几何（对标 QMenuBar::actionGeometry）。
 * @note 几何按统一布局模型（文本宽+16 或固定 60px 间距）计算，与无样式
 *       绘制的固定间距存在轻微视觉偏差，头文件已注明简化。
 * @param self 目标菜单栏；可为 NULL。
 * @param action 目标动作借用指针；可为 NULL。
 * @return 命中返回动作矩形；未命中返回空矩形 (0,0,0,0)。
 */
XRect XMenuBar_actionGeometry(const XMenuBar* self, XAction* action);
/** @brief 查询角落控件（对标 QMenuBar::cornerWidget）。
 * @param self 目标菜单栏；可为 NULL。
 * @param corner 角落位置（XMenuBarCorner）。
 * @return 借用指针；未设置返回 NULL。
 */
XWidget* XMenuBar_cornerWidget(const XMenuBar* self, int corner);
/** @brief 设置角落控件（对标 QMenuBar::setCornerWidget；控件归调用方）。
 * @param self 目标菜单栏；可为 NULL。
 * @param widget 角落控件；可为 NULL 表示清除。
 * @param corner 角落位置（XMenuBarCorner）。
 * @return 无返回值。
 */
void XMenuBar_setCornerWidget(XMenuBar* self, XWidget* widget, int corner);
/** @brief 给定宽度下的期望高度（对标 QMenuBar::heightForWidth）。
 * @param self 目标菜单栏；可为 NULL。
 * @param width 宽度（忽略，菜单栏高度固定）。
 * @return 期望高度。
 */
int XMenuBar_heightForWidth(const XMenuBar* self, int width);
/** @brief 尺寸提示（对标 QMenuBar::sizeHint）。
 * @param self 目标菜单栏；可为 NULL。
 * @return 建议尺寸。
 */
XSize XMenuBar_sizeHint(const XMenuBar* self);
/** @brief 最小尺寸提示（对标 QMenuBar::minimumSizeHint）。
 * @param self 目标菜单栏；可为 NULL。
 * @return 建议最小尺寸。
 */
XSize XMenuBar_minimumSizeHint(const XMenuBar* self);
/** @brief 查询原生菜单栏开关（对标 QMenuBar::isNativeMenuBar）。
 * @param self 目标菜单栏；可为 NULL。
 * @return 启用返回 true。
 */
bool XMenuBar_isNativeMenuBar(const XMenuBar* self);
/** @brief 设置原生菜单栏（对标 QMenuBar::setNativeMenuBar）。
 * @note 本项目为软件渲染控件树，原生菜单栏仅作存储位。
 * @param self 目标菜单栏；可为 NULL。
 * @param nativeMenuBar true 启用。
 * @return 无返回值。
 */
void XMenuBar_setNativeMenuBar(XMenuBar* self, bool nativeMenuBar);
/** @brief 查询平台菜单栏句柄（对标 QMenuBar::platformMenuBar）。
 * @note 本项目无平台菜单栏概念，恒返回 NULL。
 * @param self 目标菜单栏；可为 NULL。
 * @return NULL。
 */
void* XMenuBar_platformMenuBar(const XMenuBar* self);

/* ==================== 信号 ==================== */

void* XMenuBar_triggered_signal(XMenuBar* self, XAction* action);
void* XMenuBar_hovered_signal(XMenuBar* self, XAction* action);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XMENU_ON && XMENUBAR_ON */

#ifdef __cplusplus
}
#endif
#endif /* XMENUBAR_H */
