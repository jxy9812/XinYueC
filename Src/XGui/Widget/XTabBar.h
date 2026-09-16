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
#include "XString.h"
/** @brief XAbstractButton 前向声明（tabButton 借用指针）。 */
typedef struct XAbstractButton XAbstractButton;

#if XWIDGET_ON && XTABBAR_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XTabBar)
XCLASS_DEFINE_EXTEND_END(XTabBar, XWidget)

/** @brief XTabBar 选项卡条对象。 */
typedef struct XTabBar
{
    XWidget m_base;                  /**< 基类成员；必须是第一个。 */
    XString** m_titles;              /**< 项标题数组（每项 XString* 拥有）。 */
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
    bool    m_documentMode;          /**< 文档模式（无边框）。 */
    bool    m_drawBase;              /**< 绘制基底（默认 true）。 */
    uint32_t* m_tabTextColors;       /**< 各项文本颜色（0=默认；平行数组）。 */
    XString** m_tabToolTips;         /**< 各项提示（平行数组；对象拥有）。 */
    XString** m_tabIcons;            /**< 各项图标路径（平行数组；对象拥有）。 */
    XString** m_tabData;             /**< 各项数据（平行数组；对象拥有）。 */
    XAbstractButton** m_tabButtons;  /**< 各项角按钮（平行数组；借用）。 */
    bool*   m_tabVisible;            /**< 各项可见（平行数组；默认 true）。 */
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

/** @brief 追加页签（XString 主版本；对标 QTabBar::addTab）。
 * @param self 目标控件指针。
 * @param text 借用 XString*；不能为 NULL。
 * @return 新页签索引；参数无效时返回 -1。
 */
int XTabBar_addTab(XTabBar* self, const XString* text);
/** @brief 追加页签（UTF-8 兼容重载，转发主版本）。
 * @param self 目标控件指针。
 * @param text UTF-8 文本；不能为 NULL。
 * @return 新页签索引；参数无效时返回 -1。
 */
int XTabBar_addTab_2(XTabBar* self, const char* text);
/** @brief 插入页签（XString 主版本；对标 QTabBar::insertTab）。
 * @param self 目标控件指针。
 * @param index 索引（0 起，负数插最前、超出追加）。
 * @param text 借用 XString*；不能为 NULL。
 * @return 插入位置索引；参数无效时返回 -1。
 */
int XTabBar_insertTab(XTabBar* self, int index, const XString* text);
/** @brief 插入页签（UTF-8 兼容重载，转发主版本）。
 * @param self 目标控件指针。
 * @param index 索引（0 起，负数插最前、超出追加）。
 * @param text UTF-8 文本；不能为 NULL。
 * @return 插入位置索引；参数无效时返回 -1。
 */
int XTabBar_insertTab_2(XTabBar* self, int index, const char* text);
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
/** @brief 读取页签文本（返回新建 XString*，调用方负责 delete_base；对标 QTabBar::tabText）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 新建 XString*；参数无效时返回 NULL。
 */
XString* XTabBar_tabText(const XTabBar* self, int index);
/** @brief 读取页签文本（UTF-8 借用；对标 QTabBar::tabText）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @return 内部 UTF-8 借用指针；参数无效时返回空串，不得释放或修改。
 */
const char* XTabBar_tabText_2(const XTabBar* self, int index);
/** @brief 设置页签文本（XString 主版本；对标 QTabBar::setTabText）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param text 借用 XString*；不能为 NULL。
 * @return 无返回值。
 */
void XTabBar_setTabText(XTabBar* self, int index, const XString* text);
/** @brief 设置页签文本（UTF-8 兼容重载，转发主版本）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param text UTF-8 文本；不能为 NULL。
 * @return 无返回值。
 */
void XTabBar_setTabText_2(XTabBar* self, int index, const char* text);
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

void* XTabBar_currentChanged_signal(XTabBar* self, int index);
/** @brief X页签条tabCloseRequested 信号地址（发射经 XObject_emitSignal）。
 * @param self 目标控件指针。
 * @return 返回对象指针；无效时返回 NULL。
 */
void* XTabBar_tabCloseRequested_signal(XTabBar* self);
/** @brief 页签点击信号（对标 QTabBar::tabBarClicked(int)；载荷：页签索引）。 */
void* XTabBar_tabBarClicked_signal(XTabBar* self, int index);
/** @brief 页签双击信号（对标 QTabBar::tabBarDoubleClicked(int)；载荷：页签索引）。 */
void* XTabBar_tabBarDoubleClicked_signal(XTabBar* self, int index);
/** @brief tabMoved(int,int) 信号（对标 QTabBar::tabMoved；载荷：from,to）。 */
void* XTabBar_tabMoved_signal(XTabBar* self, int from, int to);

/* ==================== Task 2.2：QTabBar 外观/几何/项属性 ==================== */

/** @brief 设置文档模式。 @param self 目标控件。 @param enable true 开启。 */
void XTabBar_setDocumentMode(XTabBar* self, bool enable);
/** @brief 查询文档模式。 @param self 目标控件。 @return 开启返回 true。 */
bool XTabBar_documentMode(const XTabBar* self);
/** @brief 设置省略模式。 @param self 目标控件。 @param mode 省略模式码。 */
void XTabBar_setElideMode(XTabBar* self, int mode);
/** @brief 查询省略模式。 @param self 目标控件。 @return 模式码。 */
int XTabBar_elideMode(const XTabBar* self);
/** @brief 设置扩展模式。 @param self 目标控件。 @param enable true 扩展。 */
void XTabBar_setExpanding(XTabBar* self, bool enable);
/** @brief 查询扩展模式。 @param self 目标控件。 @return 扩展返回 true。 */
bool XTabBar_expanding(const XTabBar* self);
/** @brief 设置滚动按钮。 @param self 目标控件。 @param enable true 显示。 */
void XTabBar_setUsesScrollButtons(XTabBar* self, bool enable);
/** @brief 查询滚动按钮。 @param self 目标控件。 @return 显示返回 true。 */
bool XTabBar_usesScrollButtons(const XTabBar* self);
/** @brief 设置基底绘制。 @param self 目标控件。 @param enable true 绘制。 */
void XTabBar_setDrawBase(XTabBar* self, bool enable);
/** @brief 查询基底绘制。 @param self 目标控件。 @return 绘制返回 true。 */
bool XTabBar_drawBase(const XTabBar* self);
/** @brief 页签矩形（对标 QTabBar::tabRect）。
 * @param self 目标控件。
 * @param index 页签号。
 * @param out 输出矩形。
 * @return 成功返回 true。
 */
bool XTabBar_tabRect(const XTabBar* self, int index, XRect* out);
/** @brief 位置命中页签（对标 QTabBar::tabAt）。
 * @param self 目标控件。
 * @param pos 局部坐标点。
 * @return 页签号；未命中 -1。
 */
int XTabBar_tabAt(const XTabBar* self, const XPoint* pos);
/** @brief 页签宽（对标 QTabBar::tabWidth）。 @param self 目标控件。 @return 宽。 */
int XTabBar_tabWidth(const XTabBar* self);
/** @brief 页签高（对标 QTabBar::tabHeight）。 @param self 目标控件。 @return 高。 */
int XTabBar_tabHeight(const XTabBar* self);
/** @brief 位置命中页签索引（对标 QTabBar::tabIndexAt）。
 * @param self 目标控件。
 * @param x 局部 X。
 * @param y 局部 Y。
 * @return 页签号；未命中 -1。
 */
int XTabBar_tabIndexAt(const XTabBar* self, int x, int y);
/** @brief 是否为空。 @param self 目标控件。 @return 空返回 true。 */
bool XTabBar_isEmpty(const XTabBar* self);
/** @brief 设置页签图标路径（XString 主版本；嵌入式以路径表达）。
 * @param self 目标控件。
 * @param index 页签号。
 * @param path 借用 XString*；可为 NULL（清除）。
 * @return 无返回值。
 */
void XTabBar_setTabIcon(XTabBar* self, int index, const XString* path);
/** @brief 设置页签图标路径（UTF-8 兼容重载）。 */
void XTabBar_setTabIcon_2(XTabBar* self, int index, const char* path);
/** @brief 读取页签图标路径（内部借用 XString*；不得释放）。 */
const XString* XTabBar_tabIcon(const XTabBar* self, int index);
/** @brief 读取页签图标路径（UTF-8 借用）。 */
const char* XTabBar_tabIcon_2(const XTabBar* self, int index);
/** @brief 设置页签文本颜色。
 * @param self 目标控件。
 * @param index 页签号。
 * @param color ARGB；0=默认。
 * @return 无返回值。
 */
void XTabBar_setTabTextColor(XTabBar* self, int index, uint32_t color);
/** @brief 读取页签文本颜色。 @param self 目标控件。 @param index 页签号。 @return ARGB。 */
uint32_t XTabBar_tabTextColor(const XTabBar* self, int index);
/** @brief 设置页签提示（XString 主版本；对标 setTabToolTip）。
 * @param self 目标控件。
 * @param index 页签号。
 * @param tip 借用 XString*；可为 NULL（清除）。
 * @return 无返回值。
 */
void XTabBar_setTabToolTip(XTabBar* self, int index, const XString* tip);
/** @brief 设置页签提示（UTF-8 兼容重载）。 */
void XTabBar_setTabToolTip_2(XTabBar* self, int index, const char* tip);
/** @brief 读取页签提示（内部借用 XString*；不得释放）。 */
const XString* XTabBar_tabToolTip(const XTabBar* self, int index);
/** @brief 读取页签提示（UTF-8 借用）。 */
const char* XTabBar_tabToolTip_2(const XTabBar* self, int index);
/** @brief 设置页签角按钮（对标 setTabButton；借用，不拥有）。
 * @param self 目标控件。
 * @param index 页签号。
 * @param button 按钮借用指针；可为 NULL（清除）。
 * @return 无返回值。
 */
void XTabBar_setTabButton(XTabBar* self, int index,
                          XAbstractButton* button);
/** @brief 读取页签角按钮。 @param self 目标控件。 @param index 页签号。 @return 借用指针。 */
XAbstractButton* XTabBar_tabButton(const XTabBar* self, int index);
/** @brief 设置页签数据（XString 主版本；对标 setTabData）。
 * @param self 目标控件。
 * @param index 页签号。
 * @param data 借用 XString*；可为 NULL（清除）。
 * @return 无返回值。
 */
void XTabBar_setTabData(XTabBar* self, int index, const XString* data);
/** @brief 设置页签数据（UTF-8 兼容重载）。 */
void XTabBar_setTabData_2(XTabBar* self, int index, const char* data);
/** @brief 读取页签数据（内部借用 XString*；不得释放）。 */
const XString* XTabBar_tabData(const XTabBar* self, int index);
/** @brief 读取页签数据（UTF-8 借用）。 */
const char* XTabBar_tabData_2(const XTabBar* self, int index);
/** @brief 设置页签可见。
 * @param self 目标控件。
 * @param index 页签号。
 * @param visible true 显示。
 * @return 无返回值。
 */
void XTabBar_setTabVisible(XTabBar* self, int index, bool visible);
/** @brief 查询页签可见。 @param self 目标控件。 @param index 页签号。 @return 可见返回 true。 */
bool XTabBar_isTabVisible(const XTabBar* self, int index);
/** @brief 移动页签（对标 QTabBar::moveTab；发射 tabMoved(from,to)）。
 * @param self 目标控件。
 * @param from 源页签号。
 * @param to 目标页签号。
 * @return 无返回值。
 */
void XTabBar_moveTab(XTabBar* self, int from, int to);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABBAR_ON */

#endif /* XTABBAR_H */
