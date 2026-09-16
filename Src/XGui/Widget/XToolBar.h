/**
 * @file       XToolBar.h
 * @brief      XToolBar 工具栏控件（对标 Qt 6.8 QToolBar 全部公共 API）。
 * @details    功能范围：
 *             - 动作管理：addAction(text)（内部创建动作与工具按钮）、
 *               addAction(action)（外部动作）、addSeparator、
 *               insertAction/insertWidget、removeAction、clear、actions；
 *             - movable/floatable/orientation/allowedAreas/iconSize/
 *               toolButtonStyle 存取（默认 movable=true、floatable=true、
 *               水平、AllAreas、图标 16x16、IconOnlyWhereText? 对标
 *               Qt 默认 ToolButtonIconOnly）；
 *             - toggleViewAction()（显示/隐藏工具栏的动作）；
 *             - 信号：actionTriggered(action)/actionHovered(action)/
 *               orientationChanged(bool)/movableChanged(bool)/
 *               visibilityChanged(bool)；
 *             - 布局：横排（水平）动作按钮与控件，竖排（垂直）竖列；
 *               按钮呈现复用 XToolButton + setDefaultAction。
 *             动作与按钮归工具栏所有；外部动作 add 后同样接管呈现，
 *             但动作对象所有权归调用方（对标 Qt QAction 所有权）。
 * @note       模块总开关 XTOOLBAR_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XACTION_ON、XTOOLBUTTON_ON。
 * @author     XinYueC 团队
 */
#ifndef XTOOLBAR_H
#define XTOOLBAR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XString.h"
#if XACTION_ON
#include "XAction.h"
#endif
#if XTOOLBUTTON_ON
#include "XToolButton.h"
#endif

#if XWIDGET_ON && XACTION_ON && XTOOLBUTTON_ON && XTOOLBAR_ON

/** @brief 工具栏允许停靠区域（对标 Qt::ToolBarArea，数值一致）。 */
typedef enum XToolBarArea
{
    XToolBarArea_Left = 0x1,   /**< 左侧停靠。 */
    XToolBarArea_Right = 0x2,  /**< 右侧停靠。 */
    XToolBarArea_Top = 0x4,    /**< 顶部停靠。 */
    XToolBarArea_Bottom = 0x8  /**< 底部停靠。 */
} XToolBarArea;

XCLASS_DEFINE_BEGING(XToolBar)
XCLASS_DEFINE_EXTEND_END(XToolBar, XWidget)

/**
 * @brief      XToolBar 控件对象；m_base 必须是第一个成员。
 */
typedef struct XToolBar
{
    XWidget m_base;          /**< 基类成员；必须是第一个。 */
    XVector* m_actions;      /**< 动作数组（XAction*；内部创建的归工具栏）。 */
    XVector* m_buttons;      /**< 与动作顺序对应的按钮（XToolButton*）。 */
    XVector* m_bridges;      /**< 与动作顺序对应的桥（XTBBridge*，拥有）。 */
    XVector* m_widgets;      /**< 与动作顺序对应的附加控件（XWidget*，借用；
                                  平行数组，无控件位为 NULL）。 */
    XAction* m_toggleAction; /**< 显示/隐藏工具栏的动作（对标 toggleViewAction）。 */
    bool m_movable;          /**< 可移动（默认 true）。 */
    bool m_floatable;        /**< 可浮动（默认 true）。 */
    int m_orientation;       /**< 方向（1=水平 2=垂直，默认水平）。 */
    int m_allowedAreas;      /**< 允许停靠区域（默认全部）。 */
    int m_iconSize;          /**< 图标尺寸（默认 16）。 */
    int m_buttonStyle;       /**< 按钮风格（默认 IconOnly；文本绘制用）。 */
} XToolBar;

/** @brief 设置工具栏标题（对标 QToolBar::setWindowTitle；存于 XWidget 窗口标题）。 @param self 目标工具栏指针。 @param utf8 标题（UTF-8）。 @return 无返回值。 */
void XToolBar_setTitle(XToolBar* self, const char* utf8);
/** @brief 读取工具栏标题。 @param self 目标工具栏指针。 @return 标题（UTF-8）。 */
const char* XToolBar_title(const XToolBar* self);

/* ==================== 生命周期 ==================== */

XVtable* XToolBar_class_init(void);
void XToolBar_init(XToolBar* self, XWidget* parent, XWidgetFlags flags);
#define XToolBar_create(parent, flags) XToolBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XToolBar* XToolBar_create_ex(XMemoryType memory, XWidget* parent,
                             XWidgetFlags flags);
#define XToolBar_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XToolBar_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 属性（对标 QToolBar public API） ==================== */

void XToolBar_setMovable(XToolBar* self, bool movable);
bool XToolBar_isMovable(const XToolBar* self);
void XToolBar_setFloatable(XToolBar* self, bool floatable);
bool XToolBar_isFloatable(const XToolBar* self);
void XToolBar_setOrientation(XToolBar* self, int orientation);
int XToolBar_orientation(const XToolBar* self);
/**
 * @brief      设置允许停靠区域。
 */
void XToolBar_setAllowedAreas(XToolBar* self, int areas);
/**
 * @brief      获取允许停靠区域。
 */
int XToolBar_allowedAreas(const XToolBar* self);
void XToolBar_setIconSize(XToolBar* self, int size);
int XToolBar_iconSize(const XToolBar* self);
void XToolBar_setToolButtonStyle(XToolBar* self, int style);
int XToolBar_toolButtonStyle(const XToolBar* self);

/* ==================== 动作与控件管理 ==================== */

/** @brief 追加动作（对标 addAction(QAction*)；动作与按钮归工具栏）。 */
void XToolBar_addAction(XToolBar* self, XAction* action);
/** @brief 以文本创建动作并追加（对标 addAction(const QString&)；
 *         返回的动作归工具栏所有）。 */
/**
 * @brief      添加动作。
 */
XAction* XToolBar_addAction_2(XToolBar* self, const char* utf8);
/** @brief 追加分隔条（对标 addSeparator；返回分隔动作）。 */
XAction* XToolBar_addSeparator(XToolBar* self);
/** @brief 在 before 动作之前插入分隔条（对标 insertSeparator；before 为
 *         NULL 时等价追加）。 */
/**
 * @brief      插入分隔条。
 */
XAction* XToolBar_insertSeparator(XToolBar* self, XAction* before);
/** @brief 追加控件（对标 addWidget；控件归调用方）。 */
/**
 * @brief      添加控件。
 */
void XToolBar_addWidget(XToolBar* self, XWidget* widget);
/** @brief 在 before 动作之前插入控件（对标 insertWidget；before 为 NULL
 *         时等价追加；控件归调用方，占位动作归工具栏）。 */
/**
 * @brief      插入控件。
 */
void XToolBar_insertWidget(XToolBar* self, XAction* before,
                           XWidget* widget);
/** @brief 查询与动作关联的附加控件（对标 widgetForAction；仅返回经
 *         addWidget/insertWidget 登记的控件，动作按钮无关联控件）。 */
/**
 * @brief      查询动作关联控件。
 */
XWidget* XToolBar_widgetForAction(XToolBar* self, XAction* action);
/** @brief 移除动作（对标 removeAction）。 */
void XToolBar_removeAction(XToolBar* self, XAction* action);
/** @brief 清空全部动作与控件（对标 clear）。 */
/**
 * @brief      清空内容（对标 Qt 同名槽）。
 */
void XToolBar_clear(XToolBar* self);
/** @brief 动作数量（分隔条计入）。 */
int XToolBar_actionCount(const XToolBar* self);
/** @brief 查询指定索引的动作；越界返回 NULL。 */
XAction* XToolBar_action(const XToolBar* self, int index);
/** @brief 返回局部坐标处的动作（对标 QToolBar::actionAt）。
 * @param self 目标工具栏；可为 NULL。
 * @param pos 局部坐标借用指针；可为 NULL。
 * @return 命中返回对应动作；未命中或参数无效返回 NULL。
 */
XAction* XToolBar_actionAt(const XToolBar* self, const XPoint* pos);
/** @brief 返回动作在局部坐标中的几何（对标 QToolBar::actionGeometry）。
 * @param self 目标工具栏；可为 NULL。
 * @param action 目标动作借用指针；可为 NULL。
 * @return 命中返回动作矩形；未命中返回空矩形 (0,0,0,0)。
 */
XRect XToolBar_actionGeometry(const XToolBar* self, XAction* action);
/** @brief 显示/隐藏工具栏的开关动作（对标 QToolBar::toggleViewAction；
 *         动作由工具栏拥有，触发时切换可见性）。 */
/**
 * @brief      获取切换可见性动作。
 */
XAction* XToolBar_toggleViewAction(XToolBar* self);

/* ==================== 信号 ==================== */

/**
 * @brief      动作触发信号（真发射）。
 */
void* XToolBar_actionTriggered_signal(XToolBar* self, XAction* action);
void* XToolBar_actionHovered_signal(XToolBar* self, XAction* action);
void* XToolBar_orientationChanged_signal(XToolBar* self, int orientation);
void* XToolBar_movableChanged_signal(XToolBar* self, bool movable);

/**
 * @brief      可见性变化信号（真发射；show/hide 事件驱动，对标
 *             QToolBar::visibilityChanged）。
 * @details    XToolBar 显示时发射 visibilityChanged(true)，隐藏时发射
 *             visibilityChanged(false)；self 非 NULL 且有已连接槽时经
 *             XObject_emitSignal 同步通知，否则只返回信号标识。
 * @param      self 目标工具栏指针；可为 NULL。
 * @param      visible true 表示已显示，false 表示已隐藏。
 * @return     不透明的 visibilityChanged 信号标识；返回值不指向可释放
 *             对象，也不得解引用。
 */
void* XToolBar_visibilityChanged_signal(XToolBar* self, bool visible);
/** @brief iconSizeChanged(int,int) 信号（对标 QToolBar::iconSizeChanged；
 *         载荷：宽,高；当前为方边尺寸 size×size）。 */
void* XToolBar_iconSizeChanged_signal(XToolBar* self, int width, int height);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XACTION_ON && XTOOLBUTTON_ON && XTOOLBAR_ON */

#endif /* XTOOLBAR_H */
