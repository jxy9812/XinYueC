/**
 * @file       XTabBar.h
 * @brief      XTabBar 选项卡条控件（对标 Qt 6.8 QTabBar）。
 * @details    水平选项卡条：addTab/insertTab/removeTab/moveTab/setTabText/
 *             setTabEnabled/setTabVisible/setCurrentIndex + 点击切换、拖拽
 *             换位（movable）、角按钮槽、当前项与点击/双击/关闭/换位信号；
 *             页签布局对齐 Qt（qtabbar.cpp layoutTabs）：**单行**按
 *             tabSizeHint 的自然宽排布，expanding 时把多余宽按「自然宽超过
 *             均分者保自然宽、其余均分剩余」的迭代口径摊平（本机 Qt 6.8
 *             探针：400 宽 3 签自然宽 53/163/39 ⇒ 119/163/118）；页签总宽
 *             超出条宽且 usesScrollButtons 时进入溢出滚动（两端按钮 +
 *             偏移）。
 *             sizeHint 取全部可见页签矩形并集；minimumSizeHint 按 Qt
 *             （qtabbar.cpp:1502-1516）分 usesScrollButtons 两档。
 *             缺省值对齐 Qt 6.8 实测：elideMode=ElideNone(3)、
 *             selectionBehaviorOnRemove=SelectRightTab(1)、
 *             iconSize=16×16（样式 PM_TabBarIconSize；QSize 口径，
 *             宽高可不同，见 XTabBar_setIconSize_2）、expanding=true、
 *             usesScrollButtons=true。
 *             tabsClosable 为**每页真实关闭按钮子控件**（对标 Qt 经
 *             setTabButton(RightSide, CloseButton) 挂按钮：本机探针
 *             tabButton(0,RightSide) 在 closable 时返回 CloseButton 类、
 *             关闭开关后返回空）；tabButton() 可读到该按钮。
 * @note       模块总开关 XTABBAR_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTABBAR_H
#define XTABBAR_H

#if XGUI_ON
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
/* [Qt 对齐 2026-11-11] QTabBar 的 protected virtual（qtabbar.h:159-163）：
 * 页签尺寸/最小尺寸可被子类定制，插入/移除/布局变化回调。追加在父类槽之后
 * （同 XTabWidget 的 TabInserted/TabRemoved 先例）。 */
XCLASS_DEFINE_ENUM(XTabBar, TabSizeHint) = XCLASS_VTABLE_GET_SIZE(XWidget),
XCLASS_DEFINE_ENUM(XTabBar, MinimumTabSizeHint),
XCLASS_DEFINE_ENUM(XTabBar, TabInserted),
XCLASS_DEFINE_ENUM(XTabBar, TabRemoved),
XCLASS_DEFINE_ENUM(XTabBar, TabLayoutChange),
XCLASS_DEFINE_END(XTabBar)

/** @brief XTabBar 选项卡条对象。 */
typedef struct XTabBar
{
    XWidget m_base;                  /**< 基类成员；必须是第一个。 */
    /* 指针组（8B 对齐）。 */
    XString** m_titles;              /**< 项标题数组（每项 XString* 拥有）。 */
    bool*   m_enabled;               /**< 各项启用状态。 */
    XTimerId m_scrollRepeatTimer;    /**< §8.0g11 连发定时器（intptr_t；XTIMER_INVALID_ID=无）。 */
    uint32_t* m_tabTextColors;       /**< 各项文本颜色（0=默认；平行数组）。 */
    XString** m_tabToolTips;         /**< 各项提示（平行数组；对象拥有）。 */
    XString** m_tabWhatsThis;        /**< 各项帮助文本（平行数组；对象拥有）。 */
    XString** m_tabAccessibleNames;  /**< 各项无障碍名称（平行数组；对象拥有）。 */
    XString** m_tabIcons;            /**< 各项图标路径（平行数组；对象拥有）。 */
    XString** m_tabData;             /**< 各项数据（平行数组；对象拥有）。 */
    XAbstractButton** m_tabButtons;  /**< 各项右角按钮（RightSide 槽；平行
                                          数组；reparent 归本控件）。 */
    XAbstractButton** m_tabLeftButtons; /**< 各项左角按钮（LeftSide 槽；
                                             平行数组；reparent 归本控件）。 */
    bool*   m_tabVisible;            /**< 各项可见（平行数组；默认 true）。 */
    bool*   m_tabCloseOwned;         /**< 右角按钮中由 tabsClosable 自动创建
                                         的关闭按钮标记（平行数组；析构/
                                         关闭开关时只回收这些，不误删用户经
                                         setTabButton 挂的自定义按钮）。 */
    uint16_t* m_tabWidths;           /**< 各页签布局宽缓存（平行数组；按
                                         tabSizeHint/expanding/溢出重算）。 */
    int16_t* m_lastTabs;             /**< 各页签上次停留页（平行数组；对标
                                         QTabBarPrivate::Tab::lastTab，供
                                         SelectPreviousTab 精确改选）。 */
    int16_t m_layoutBarW;            /**< 布局宽缓存对应的条宽（-1=失效）。 */
    /* [内存治理 2026-10-11] 标志位字（对标 XObject uint32_t 位域风格）：
       9 个 bool + 3 个小值域枚举（elide 0..3 / 移除行为 0..2 / 形状 0..1）
       原分散占 9B bool + 3×4B int（含对齐洞），合并后 1 字。 */
    uint32_t m_tabsClosable : 1;     /**< 可关闭（字段保留）。 */
    uint32_t m_movable : 1;          /**< 可拖动（拖拽换位 + tabMoved）。 */
    uint32_t m_dragActive : 1;       /**< 拖拽换位进行中。 */
    uint32_t m_autoHide : 1;         /**< 自动隐藏。 */
    uint32_t m_expanding : 1;        /**< 扩展。 */
    uint32_t m_changeCurrentOnDrag : 1; /**< 拖拽悬停切换（仅存状态；拖放切换未实现）。 */
    uint32_t m_usesScrollButtons : 1;/**< 滚动按钮（默认 true，对标 Qt）。 */
    uint32_t m_documentMode : 1;     /**< 文档模式（无边框）。 */
    uint32_t m_drawBase : 1;         /**< 绘制基底（默认 true）。 */
    uint32_t m_elideMode : 3;        /**< 省略模式（Qt::TextElideMode 0..3）。 */
    uint32_t m_selectionBehavior : 3;/**< 移除行为（0..2）。 */
    uint32_t m_shape : 3;            /**< 页签形状（Qt::TabShape 0..7：
                                          RoundedNorth=0..TriangularEast=7；
                                          渲染层暂用同一外观，仅存取与校验）。 */
    uint32_t m_tabBarFlagsUnused : 14; /**< 保留位。 */
    int     m_dragPressX;            /**< 按下时鼠标 x（本地坐标；保守保留 int）。 */
    /* [内存治理 2026-10-11] 短整型值：项数/索引页签量级 ≤32767；-1 哨兵
       （currentIndex/dragIndex）用有符号 int16_t；scrollOffset 经
       xtabbar_scrollBy clamp 恒 ≥0，uint16_t 承载 px；连发方向 -1/0/+1
       用 int8_t，连发跳计数 ≤3 用 uint8_t。 */
    int16_t m_count;                 /**< 项数。 */
    int16_t m_capacity;              /**< 容量。 */
    int16_t m_currentIndex;          /**< 当前项（-1 无）。 */
    int16_t m_dragIndex;             /**< 拖拽起始页签索引（-1 无）。 */
    uint16_t m_iconSizeW;            /**< 页签图标宽（px；0=默认 16）——
                                      *   对标 QTabBar::iconSize() 的 QSize.width。 */
    uint16_t m_iconSizeH;            /**< 页签图标高（px；0=默认 16）——
                                      *   对标 QTabBar::iconSize() 的 QSize.height。 */
    uint16_t m_scrollOffset;         /**< 滚动偏移（px；恒 ≥0，溢出态下最左可见内容 x）。 */
    int8_t  m_scrollRepeatDir;       /**< §8.0g11 按住连发方向（-1 左/+1 右/0 无）。 */
    uint8_t m_repeatSkip;            /**< §8.0g11 连发首段延迟计数（350ms≈3 跳）。 */
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
 * @param index 索引（0 起；负数/越界一律追加到末尾，对标 Qt
 *        validIndex 判据 qtabbar.cpp:957-959）。
 * @param text 借用 XString*；不能为 NULL。
 * @return 插入位置索引；参数无效时返回 -1。
 */
int XTabBar_insertTab(XTabBar* self, int index, const XString* text);
/** @brief 插入页签（UTF-8 兼容重载，转发主版本）。
 * @param self 目标控件指针。
 * @param index 索引（0 起；负数/越界一律追加到末尾，对标 Qt）。
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
/** @brief 设置省略模式（对标 QTabBar::setElideMode）。
 * @details 取值 Qt::TextElideMode 0..3；绘制时按本值对页签文本省略
 *          （对标 QTabBar::initStyleOption 用 d->elideMode，qtabbar.cpp:202-210）。
 * @param self 目标控件。 @param mode 省略模式码。 */
void XTabBar_setElideMode(XTabBar* self, int mode);
/** @brief 查询省略模式（默认 ElideNone=3）。 @param self 目标控件。 @return 模式码。 */
int XTabBar_elideMode(const XTabBar* self);
/** @brief 设置扩展模式。 @param self 目标控件。 @param enable true 扩展。 */
void XTabBar_setExpanding(XTabBar* self, bool enable);
/** @brief 查询扩展模式。 @param self 目标控件。 @return 扩展返回 true。 */
bool XTabBar_expanding(const XTabBar* self);
/** @brief 设置滚动按钮（对标 QTabBar::setUsesScrollButtons；Qt 默认 true）。
 * @details 开启且页签总宽超出选项卡条时进入溢出态：单行布局 +
 *          两端滚动按钮 + 偏移滚动；关闭时维持旧换行挤压布局。
 * @param self 目标控件。 @param enable true 启用。
 */
void XTabBar_setUsesScrollButtons(XTabBar* self, bool enable);
/** @brief 查询滚动按钮（默认 true）。 @param self 目标控件。 @return 启用返回 true。 */
bool XTabBar_usesScrollButtons(const XTabBar* self);
/** @brief 溢出态查询（页签总宽 > 选项卡条宽；与开关无关）。
 * @param self 目标控件；NULL 返回 false。
 * @return 溢出返回 true。
 */
bool XTabBar_isOverflowed(const XTabBar* self);
/** @brief 有效滚动偏移（clamp 后 px；非溢出态恒 0）。
 * @param self 目标控件；NULL 返回 0。
 * @return 当前滚动偏移。
 */
int XTabBar_scrollOffset(const XTabBar* self);
/** @brief 选项卡条布局高度提示（供容器按宽预留高度；非 Qt 对标项）。
 * @details 溢出滚动模式恒单行；否则按换行布局行数计。单行高 = 页签高
 *          （缺省 24；图标高超过默认 16 时随之撑高，见
 *          XTabBar_setIconSize_2 / XTabBar_tabHeight）。
 * @param self 目标控件；NULL 返回 0。
 * @param forWidth 假定宽度（px）。
 * @return 建议高度（行数 × 页签高）。
 */
int XTabBar_barHeightHint(const XTabBar* self, int forWidth);
/** @brief 设置基底绘制。 @param self 目标控件。 @param enable true 绘制。 */
void XTabBar_setDrawBase(XTabBar* self, bool enable);
/** @brief 查询基底绘制。 @param self 目标控件。 @return 绘制返回 true。 */
bool XTabBar_drawBase(const XTabBar* self);
/** @brief 设置页签形状（对标 QTabBar::setShape）。
 * @details 取值域为 Qt::TabShape 全 8 值（RoundedNorth=0、RoundedSouth=1、
 *          RoundedWest=2、RoundedEast=3、TriangularNorth=4、TriangularSouth=5、
 *          TriangularWest=6、TriangularEast=7）；越界值忽略，同值早退。
 *          当前渲染层对全部形状采用同一外观（仅存取与校验，qtabbar.cpp:878）。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param shape 形状码（0..7）。
 * @return 无返回值。
 */
void XTabBar_setShape(XTabBar* self, int shape);
/** @brief 查询页签形状（对标 QTabBar::shape）。
 * @param self 目标控件；传入 NULL 时返回 0。
 * @return 形状码（0..7；缺省 0=RoundedNorth）。
 */
int XTabBar_shape(const XTabBar* self);
/** @brief 设置页签图标尺寸（方边便捷重载，对标 QTabBar::setIconSize）。
 * @details 旧单值入口：宽=高 委托给 XTabBar_setIconSize_2（方边语义）。
 *          0 表示使用默认尺寸（默认 = 样式 PM_TabBarIconSize，Qt 6.8
 *          Fusion 实测 16×16），负值忽略。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param size 方边像素值；0=默认，<0 忽略。
 * @return 无返回值。
 */
void XTabBar_setIconSize(XTabBar* self, int size);
/** @brief 设置页签图标尺寸（二维重载，对标
 *         QTabBar::setIconSize(const QSize&)：宽高可不同）。
 * @details 宽高分别保存（0 表示该轴取默认 16）。图标高超过默认页签
 *          留白时按 Qt 口径撑高页签（qtabbar.cpp:1595-1599 的
 *          `qMax(fm.height(), iconSize.height()) + vframe`），页签条
 *          sizeHint/barHeightHint 随动。任一轴为负忽略整次调用（旧单值
 *          入口的负值忽略语义）；宽高均未变时早退。
 * @note 与 Qt 的差异（本机 Qt 6.8.3 探针：setIconSize(30,22) ⇒
 *       tabSizeHint 由 77×29 变 91×34；setIconSize(32,40) ⇒ 93×52；
 *       iconSize(-1,30) ⇒ iconSize() 回落 (16,16)；iconSize(0,0) ⇒
 *       合法零尺寸、图标不占位 61×29）：Qt 的「0」是合法 QSize、负值
 *       使整体失效回落样式缺省；本库沿用旧单值入口的「0=缺省」口径，
 *       以便 XTabBar_setIconSize(0) 继续表示复位。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param size 图标尺寸（宽高可不同）；任一轴为负忽略整次调用，0 表示
 *             该轴取默认 16。
 * @return 无返回值。
 */
void XTabBar_setIconSize_2(XTabBar* self, XSize size);
/** @brief 查询页签图标尺寸的宽（方边便捷视图；对标 QTabBar::iconSize）。
 * @note Qt 只有 QSize 版；非方形尺寸用 XTabBar_iconSize_2 读取。
 * @param self 目标控件；传入 NULL 时返回 16。
 * @return 图标宽像素值；未显式设置时返回默认 16（样式口径）。
 */
int XTabBar_iconSize(const XTabBar* self);
/** @brief 查询页签图标尺寸（二维重载，对标 QTabBar::iconSize 的 QSize）。
 * @details 与 XTabBar_iconSize 读同一份记录：旧单值入口设置时宽=高；
 *          XTabBar_setIconSize_2 设置时按给定宽高分别读回。
 * @param self 目标控件；传入 NULL 时返回 (16,16)。
 * @return (width,height)；未显式设置的轴取默认 16。
 */
XSize XTabBar_iconSize_2(const XTabBar* self);
/** @brief 设置自动隐藏（对标 QTabBar::setAutoHide）。
 * @details 仅存储状态；单页自动隐藏行为尚未接入渲染/布局层。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param enable true 时仅一个页签自动隐藏页签条。
 * @return 无返回值。
 */
void XTabBar_setAutoHide(XTabBar* self, bool enable);
/** @brief 查询自动隐藏（对标 QTabBar::autoHide）。
 * @param self 目标控件；传入 NULL 时返回 false。
 * @return 自动隐藏返回 true。
 */
bool XTabBar_autoHide(const XTabBar* self);
/** @brief 设置移除页签时的选择行为（对标
 *         QTabBar::setSelectionBehaviorOnRemove）。
 * @details 移除当前页后：SelectLeftTab 取左侧首个可见页签；SelectRightTab
 *          取右侧首个可见（右侧无则左侧）；SelectPreviousTab 取该页的
 *          **lastTab**（上次停留页，逐页跟踪，qtabbar.cpp:1046-1073；本机
 *          Qt 探针：访问 0→2→3→2 后移除 2 选中原签 3），lastTab 失效时
 *          回退 SelectRightTab 口径。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param behavior 行为码（对标 SelectionBehavior：0=SelectLeftTab，
 *        1=SelectRightTab（缺省），2=SelectPreviousTab）。
 * @return 无返回值。
 */
void XTabBar_setSelectionBehaviorOnRemove(XTabBar* self, int behavior);
/** @brief 查询移除页签时的选择行为（对标
 *         QTabBar::selectionBehaviorOnRemove）。
 * @param self 目标控件；传入 NULL 时返回 1。
 * @return 行为码（0=SelectLeftTab，1=SelectRightTab，2=SelectPreviousTab）。
 */
int XTabBar_selectionBehaviorOnRemove(const XTabBar* self);
/** @brief 设置拖拽悬停切换当前页签（对标
 *         QTabBar::setChangeCurrentOnDrag）。
 * @details 仅存储状态；拖放切换行为尚未实现。
 * @param self 目标控件；传入 NULL 时函数不执行任何操作。
 * @param enable true 时拖拽悬停可切换当前页签。
 * @return 无返回值。
 */
void XTabBar_setChangeCurrentOnDrag(XTabBar* self, bool enable);
/** @brief 查询拖拽悬停切换（对标 QTabBar::changeCurrentOnDrag）。
 * @param self 目标控件；传入 NULL 时返回 false。
 * @return 开启返回 true。
 */
bool XTabBar_changeCurrentOnDrag(const XTabBar* self);
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
/** @brief 首签宽（X 扩展便捷查询；Qt 6.8 无 tabWidth 公共 API）。
 * @details 页签宽按各页 tabSizeHint 自然宽 + expanding 均分（Qt 口径），
 *          **不再等宽**；本函数返回页签 0 的布局宽（空条返回 0），需要逐签
 *          几何请用 XTabBar_tabRect。
 * @param self 目标控件。 @return 首签宽（px）。 */
int XTabBar_tabWidth(const XTabBar* self);
/** @brief 页签高（对标 QTabBar::tabHeight）。
 * @details 缺省 24；图标高超过默认图标(16)时按图标高撑高（Qt 口径
 *          `qMax(字体高, 图标高) + 垂直留白`，留白恒 8），未显式设置
 *          图标时恒 24。
 * @param self 目标控件。
 * @return 页签高（px）；NULL 时返回缺省 24。
 */
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
/** @brief 设置页签帮助文本（XString 主版本；对标 QTabBar::setTabWhatsThis）。
 * @details What's This 为逐项独立存储（平行数组 m_tabWhatsThis），
 *          不与提示（tabToolTip）共用槽位。
 * @param self 目标控件。
 * @param index 页签号。
 * @param text 借用 XString*；可为 NULL（清除）。
 * @return 无返回值。
 */
void XTabBar_setTabWhatsThis(XTabBar* self, int index, const XString* text);
/** @brief 设置页签帮助文本（UTF-8 兼容重载，转发主版本）。 */
void XTabBar_setTabWhatsThis_2(XTabBar* self, int index, const char* text);
/** @brief 读取页签帮助文本（内部借用 XString*；不得释放）。
 * @param self 目标控件；传入 NULL 或页签号无效时返回 NULL。
 * @param index 页签号。
 * @return 借用内部 XString 指针；无内容或参数无效时返回 NULL。
 */
const XString* XTabBar_tabWhatsThis(const XTabBar* self, int index);
/** @brief 读取页签帮助文本（UTF-8 借用；无内容时返回空串）。 */
const char* XTabBar_tabWhatsThis_2(const XTabBar* self, int index);
/** @brief 设置页签无障碍名称（XString 主版本；对标
 *         QTabBar::setAccessibleTabName）。
 * @details 每页签独立的 accessibleName 承载（平行数组
 *          m_tabAccessibleNames）；辅助功能桥接层未接入前仅作状态
 *          存储（对标 Qt 的 NameChanged 无障碍事件暂不派发）。
 * @param self 目标控件。
 * @param index 页签号。
 * @param name 借用 XString*；可为 NULL（清除）。
 * @return 无返回值。
 */
void XTabBar_setAccessibleTabName(XTabBar* self, int index,
                                  const XString* name);
/** @brief 设置页签无障碍名称（UTF-8 兼容重载，转发主版本）。 */
void XTabBar_setAccessibleTabName_2(XTabBar* self, int index,
                                    const char* name);
/** @brief 读取页签无障碍名称（内部借用 XString*；不得释放）。
 * @param self 目标控件；传入 NULL 或页签号无效时返回 NULL。
 * @param index 页签号。
 * @return 借用内部 XString 指针；无内容或参数无效时返回 NULL。
 */
const XString* XTabBar_accessibleTabName(const XTabBar* self, int index);
/** @brief 读取页签无障碍名称（UTF-8 借用；无内容时返回空串）。 */
const char* XTabBar_accessibleTabName_2(const XTabBar* self, int index);
/** @brief 角按钮槽位枚举码（对标 QTabBar::ButtonPosition）。
 * @details 0=LeftSide（页签左角），1=RightSide（页签右角）。
 */
#define XTABBAR_BUTTON_LEFTSIDE   0
#define XTABBAR_BUTTON_RIGHTSIDE  1

/** @brief 设置页签角按钮（对标 QTabBar::setTabButton(int,ButtonPosition,
 *         QWidget*)，qtabbar.cpp:2791-2815）。
 * @details 接管语义（Qt 同）：按钮 reparent 为本控件子控件并 show，
 *          同槽位旧按钮隐藏（不销毁，仍挂本控件父子链）；removeTab
 *          移除页签时该页签的两槽位按钮经 deleteLater 延迟销毁。本
 *          控件销毁时按钮随父子链级联释放。
 * @param self 目标控件；传入 NULL 或页签号越界时函数不执行任何操作。
 * @param index 页签号。
 * @param position 槽位（XTABBAR_BUTTON_LEFTSIDE/RIGHTSIDE）。
 * @param button 按钮指针；可为 NULL（隐藏并清空该槽位）。
 * @return 无返回值。
 */
void XTabBar_setTabButtonAt(XTabBar* self, int index, int position,
                            XAbstractButton* button);
/** @brief 读取页签角按钮（对标 QTabBar::tabButton(int,ButtonPosition)）。
 * @param self 目标控件；NULL 或页签号越界返回 NULL。
 * @param index 页签号。
 * @param position 槽位（XTABBAR_BUTTON_LEFTSIDE/RIGHTSIDE）。
 * @return 按钮借用指针；该槽位未设置时返回 NULL，不得释放。
 */
XAbstractButton* XTabBar_tabButtonAt(const XTabBar* self, int index,
                                     int position);
/** @brief 设置页签右角按钮（RightSide 槽便捷版；语义同
 *         setTabButtonAt(RightSide)，对标 Qt 关闭按钮默认槽位）。
 * @param self 目标控件。
 * @param index 页签号。
 * @param button 按钮指针；可为 NULL（隐藏并清空该槽位）。
 * @return 无返回值。
 */
void XTabBar_setTabButton(XTabBar* self, int index,
                          XAbstractButton* button);
/** @brief 读取页签右角按钮（RightSide 槽便捷版）。
 *  @param self 目标控件。 @param index 页签号。 @return 借用指针。 */
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
/** @brief 设置页签可见（对标 QTabBar::setTabVisible）。
 * @details 同值早退并重算尺寸提示（隐藏页签不参与 sizeHint 并集）。
 *          隐藏**当前页**时按 Qt 改选可见页（qtabbar.cpp:1149-1166 本机
 *          探针：tabs 0..3 当前 1 隐藏 1 ⇒ current=2；仅剩 0/1 时 ⇒ 0；
 *          全部隐藏时 Qt 不改选、也不发 currentChanged(-1)——探针 current
 *          仍为 0 且无信号，本实现同口径）。
 * @param self 目标控件。
 * @param index 页签号。
 * @param visible true 显示。
 * @return 无返回值。
 */
void XTabBar_setTabVisible(XTabBar* self, int index, bool visible);
/** @brief 查询页签可见（对标 QTabBar::isTabVisible）。
 * @param self 目标控件。
 * @param index 页签号。
 * @return 可见返回 true；**越界返回 false**（qtabbar.cpp:1136-1140）。
 */
bool XTabBar_isTabVisible(const XTabBar* self, int index);
/** @brief 移动页签（对标 QTabBar::moveTab；发射 tabMoved(from,to)）。
 * @param self 目标控件。
 * @param from 源页签号。
 * @param to 目标页签号。
 * @return 无返回值。
 */
void XTabBar_moveTab(XTabBar* self, int from, int to);

/* ==================== protected 虚函数入口（对标 qtabbar.h:159-163） ====================
 * Qt 的 QTabBar 把下列函数声明为 protected virtual，子类重载后可定制页签
 * 尺寸并在插入/移除/重排时收到回调。C 侧以本类自有虚槽承载（不侵入
 * XWidget 虚表），子类在 class_init 里
 * `XVTABLE_OVERLOAD_DEFAULT(EXTabBar_TabSizeHint, ...)` 重载；需要基类口径
 * 时经 `_base` 入口直调（不经虚表，重载无递归）。 */

/** @brief 单个页签的首选尺寸（对标 QTabBar::tabSizeHint）。
 * @details 自然宽口径取自 Qt Fusion 实测比例：文字宽 + 左右留白
 *          （`XTABBAR_TAB_PADDING`），有图标加图标宽 + 间距，tabsClosable
 *          再加关闭按钮位；高取条高。
 * @param self 目标控件；NULL 返回 (0,0)。
 * @param index 页签号。
 * @return 首选尺寸（XSize）。
 */
XSize XTabBar_tabSizeHint(const XTabBar* self, int index);
/** @brief tabSizeHint 的基类实现（虚表缺省挂载；子类重载后取基类口径）。 */
XSize XTabBar_tabSizeHint_base(const XTabBar* self, int index);
/** @brief 单个页签的最小尺寸（对标 QTabBar::minimumTabSizeHint）。
 * @details Qt 6.8 Fusion 实测与 tabSizeHint 相等（空文本 24x29、带图标
 *          53x29）；本实现同口径（自然宽即最小宽）。
 * @param self 目标控件；NULL 返回 (0,0)。
 * @param index 页签号。
 * @return 最小尺寸。
 */
XSize XTabBar_minimumTabSizeHint(const XTabBar* self, int index);
/** @brief minimumTabSizeHint 的基类实现。 */
XSize XTabBar_minimumTabSizeHint_base(const XTabBar* self, int index);
/** @brief 页签插入回调（对标 QTabBar::tabInserted；缺省空操作）。
 * @param self 目标控件。
 * @param index 新页签索引。
 * @return 无返回值。
 */
void XTabBar_tabInserted(XTabBar* self, int index);
/** @brief tabInserted 的基类缺省实现（空操作）。 */
void XTabBar_tabInserted_base(XTabBar* self, int index);
/** @brief 页签移除回调（对标 QTabBar::tabRemoved；缺省空操作）。
 * @param self 目标控件。
 * @param index 被移除页签的原索引。
 * @return 无返回值。
 */
void XTabBar_tabRemoved(XTabBar* self, int index);
/** @brief tabRemoved 的基类缺省实现（空操作）。 */
void XTabBar_tabRemoved_base(XTabBar* self, int index);
/** @brief 页签布局变化回调（对标 QTabBar::tabLayoutChange；缺省空操作）。
 * @details 页签尺寸/顺序/可见集合/宽度变化导致重排时调用（Qt 在
 *          layoutTabs 后 dispatch）。
 * @param self 目标控件。
 * @return 无返回值。
 */
void XTabBar_tabLayoutChange(XTabBar* self);
/** @brief tabLayoutChange 的基类缺省实现（空操作）。 */
void XTabBar_tabLayoutChange_base(XTabBar* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABBAR_ON */

#endif /* XGUI_ON */

#endif /* XTABBAR_H */
