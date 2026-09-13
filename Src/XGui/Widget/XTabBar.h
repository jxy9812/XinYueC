/**
 * @file       XTabBar.h
 * @brief      XTabBar 选项卡条控件（对标 Qt 6.8 QTabBar）。
 * @details    水平选项卡条：addTab/insertTab/removeTab/setTabText/
 *             setTabEnabled/setCurrentIndex + currentIndexBar 显示与
 *             点击切换；tabsClosable/movable 为字段保留项。
 * @note       模块总开关 XTABBAR_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTABBAR_H
#define XTABBAR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"

#if XWIDGET_ON && XTABBAR_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XTabBar)
XCLASS_DEFINE_EXTEND_END(XTabBar, XWidget)

/** @brief XTabBar 选项卡条对象。 */
typedef struct XTabBar
{
    XWidget m_base;                  /**< 基类成员；必须是第一个。 */
    char**  m_titles;                /**< 项标题数组（拥有）。 */
    int     m_count;                 /**< 项数。 */
    int     m_capacity;              /**< 容量。 */
    int     m_currentIndex;          /**< 当前项。 */
    bool*   m_enabled;               /**< 各项启用状态。 */
    bool    m_tabsClosable;          /**< 可关闭（字段保留）。 */
    bool    m_movable;               /**< 可拖动（字段保留）。 */
bool    m_autoHide;              /**< 自动隐藏。 */
    bool    m_expanding;             /**< 扩展。 */
    int     m_elideMode;             /**< 省略模式。 */
    int     m_selectionBehavior;     /**< 移除行为。 */
    bool    m_usesScrollButtons;     /**< 滚动按钮。 */
    bool    m_documentMode;
} XTabBar;

/* ==================== 生命周期 ==================== */

XVtable* XTabBar_class_init(void);
/** @brief X页签条init（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 无返回值。
 */
void XTabBar_init(XTabBar* self, XWidget* parent, XWidgetFlags flags);
#define XTabBar_create(parent, flags) XTabBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/** @brief X页签条createex（对标 Qt 同名接口）。
 * @param memory XMemoryType 参数。
 * @param parent 父控件指针；可为 NULL。
 * @param flags 窗口标志位组合。
 * @return 返回对象指针；无效时返回 NULL。
 */
XTabBar* XTabBar_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);
#define XTabBar_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XTabBar_delete_base(self) XWidget_delete_base((XWidget*)(self))

/* ==================== API（对标 QTabBar public API 子集） ==================== */

int XTabBar_addTab(XTabBar* self, const char* text);
/** @brief X页签条insert页签（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param text UTF-8 文本。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XTabBar_insertTab(XTabBar* self, int index, const char* text);
/** @brief X页签条remove页签（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 无返回值。
 */
void XTabBar_removeTab(XTabBar* self, int index);
/** @brief X页签条count（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XTabBar_count(const XTabBar* self);
/** @brief X页签条current索引（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XTabBar_currentIndex(const XTabBar* self);
/** @brief X页签条set当前索引（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 无返回值。
 */
void XTabBar_setCurrentIndex(XTabBar* self, int index);
/** @brief X页签条tab文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XTabBar_tabText(const XTabBar* self, int index);
/** @brief X页签条set页签文本（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param text UTF-8 文本。
 * @return 无返回值。
 */
void XTabBar_setTabText(XTabBar* self, int index, const char* text);
/** @brief X页签条is页签启用（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTabBar_isTabEnabled(const XTabBar* self, int index);
/** @brief X页签条set页签启用（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param enabled bool 开关：true 启用。
 * @return 无返回值。
 */
void XTabBar_setTabEnabled(XTabBar* self, int index, bool enabled);
/** @brief X页签条tabs可关闭（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTabBar_tabsClosable(const XTabBar* self);
/** @brief X页签条setTabs可关闭（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param closable bool 参数。
 * @return 无返回值。
 */
void XTabBar_setTabsClosable(XTabBar* self, bool closable);
/** @brief X页签条is可移动（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTabBar_isMovable(const XTabBar* self);
/** @brief X页签条set可移动（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param movable bool 参数。
 * @return 无返回值。
 */
void XTabBar_setMovable(XTabBar* self, bool movable);

/* ==================== 信号 ==================== */

void* XTabBar_currentChanged_signal(XTabBar* self);
/** @brief X页签条tab点击 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XTabBar_tabClicked_signal(XTabBar* self);
/** @brief X页签条tabCloseRequested 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XTabBar_tabCloseRequested_signal(XTabBar* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABBAR_ON */

#ifdef __cplusplus
}
#endif

/** @brief X页签条tab条点击 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XTabBar_tabBarClicked_signal(XTabBar* self);
/** @brief X页签条tab条双击点击 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XTabBar_tabBarDoubleClicked_signal(XTabBar* self);
/** @brief X页签条tabMoved 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XTabBar_tabMoved_signal(XTabBar* self);
/** @brief X页签条set自动Hide（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param hide bool 参数。
 * @return 无返回值。
 */
void XTabBar_setAutoHide(XTabBar* self, bool hide);
/** @brief X页签条autoHide（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTabBar_autoHide(const XTabBar* self);
/** @brief X页签条set文档模式（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param mode bool 模式开关。
 * @return 无返回值。
 */
void XTabBar_setDocumentMode(XTabBar* self, bool mode);
/** @brief X页签条document模式（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTabBar_documentMode(const XTabBar* self);
/** @brief X页签条set省略模式（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param mode bool 模式开关。
 * @return 无返回值。
 */
void XTabBar_setElideMode(XTabBar* self, int mode);
/** @brief X页签条elide模式（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XTabBar_elideMode(const XTabBar* self);
/** @brief X页签条setExpanding（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param expanding bool 参数。
 * @return 无返回值。
 */
void XTabBar_setExpanding(XTabBar* self, bool expanding);
/** @brief X页签条isExpanding（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTabBar_isExpanding(const XTabBar* self);
/** @brief X页签条setSelectionBehaviorOn移除（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param behavior int 参数。
 * @return 无返回值。
 */
void XTabBar_setSelectionBehaviorOnRemove(XTabBar* self, int behavior);
/** @brief X页签条selectionBehaviorOn移除（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XTabBar_selectionBehaviorOnRemove(const XTabBar* self);
/** @brief X页签条setUses滚动Buttons（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param useButtons bool 参数。
 * @return 无返回值。
 */
void XTabBar_setUsesScrollButtons(XTabBar* self, bool useButtons);
/** @brief X页签条uses滚动Buttons（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTabBar_usesScrollButtons(const XTabBar* self);
/** @brief X页签条set页签按钮（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param position int 参数。
 * @param widget 子控件指针。
 * @return 无返回值。
 */
void XTabBar_setTabButton(XTabBar* self, int index, int position, XWidget* widget);
/** @brief X页签条tab按钮（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param position int 参数。
 * @return 返回对象指针；无效时返回 NULL。
 */
XWidget* XTabBar_tabButton(const XTabBar* self, int index, int position);
/** @brief X页签条set页签文本颜色（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param color ARGB 颜色值。
 * @return 无返回值。
 */
void XTabBar_setTabTextColor(XTabBar* self, int index, uint32_t color);
/** @brief X页签条tab文本颜色（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 返回对应值。
 */
uint32_t XTabBar_tabTextColor(const XTabBar* self, int index);
/** @brief X页签条set页签工具提示（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param tip const char 参数。
 * @return 无返回值。
 */
void XTabBar_setTabToolTip(XTabBar* self, int index, const char* tip);
/** @brief X页签条tab工具提示（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XTabBar_tabToolTip(const XTabBar* self, int index);
/** @brief X页签条set页签WhatsThis（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param text UTF-8 文本。
 * @return 无返回值。
 */
void XTabBar_setTabWhatsThis(XTabBar* self, int index, const char* text);
/** @brief X页签条tabWhatsThis（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 返回 UTF-8 文本；无效时返回空串。
 */
const char* XTabBar_tabWhatsThis(const XTabBar* self, int index);
/** @brief X页签条set页签图标（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param icon 图标路径。
 * @return 无返回值。
 */
void XTabBar_setTabIcon(XTabBar* self, int index, const char* icon);
/** @brief X页签条expanding（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTabBar_expanding(const XTabBar* self);
/** @brief X页签条drawBase（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XTabBar_drawBase(const XTabBar* self);
/** @brief X页签条set绘制Base（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param drawBase bool 参数。
 * @return 无返回值。
 */
void XTabBar_setDrawBase(XTabBar* self, bool drawBase);
/** @brief X页签条accessible页签Name（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_accessibleTabName(XTabBar* self);
/** @brief X页签条set变更当前On拖动（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_setChangeCurrentOnDrag(XTabBar* self);
/** @brief X页签条change当前On拖动（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_changeCurrentOnDrag(XTabBar* self);
/** @brief X页签条tab于2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_tabAt_2(XTabBar* self);
/** @brief X页签条tab矩形（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_tabRect(XTabBar* self);
/** @brief X页签条tab宽（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_tabWidth(XTabBar* self);
/** @brief X页签条tab高（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_tabHeight(XTabBar* self);
/** @brief X页签条tab位置2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_tabPosition_2(XTabBar* self);
/** @brief X页签条tab索引于（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_tabIndexAt(XTabBar* self);
/** @brief X页签条is页签可见（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_isTabVisible(XTabBar* self);
/** @brief X页签条isEmpty（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_isEmpty(XTabBar* self);
/** @brief X页签条move页签（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_moveTab(XTabBar* self);
/** @brief X页签条remove页签2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_removeTab_2(XTabBar* self);
/** @brief X页签条is页签启用2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_isTabEnabled_2(XTabBar* self);
/** @brief X页签条set页签启用2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_setTabEnabled_2(XTabBar* self);
/** @brief X页签条set当前索引2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_setCurrentIndex_2(XTabBar* self);
/** @brief X页签条current索引2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_currentIndex_2(XTabBar* self);
/** @brief X页签条tab文本2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_tabText_2(XTabBar* self);
/** @brief X页签条set页签文本2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_setTabText_2(XTabBar* self);
/** @brief X页签条set页签图标2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_setTabIcon_2(XTabBar* self);
/** @brief X页签条shape2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_shape_2(XTabBar* self);
/** @brief X页签条set形状2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_setShape_2(XTabBar* self);
/** @brief X页签条set图标尺寸2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_setIconSize_2(XTabBar* self);
/** @brief X页签条icon尺寸2（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XTabBar_iconSize_2(XTabBar* self);
#endif /* XTABBAR_H */
