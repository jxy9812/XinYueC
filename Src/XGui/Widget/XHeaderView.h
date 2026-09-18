/**
 * @file       XHeaderView.h
 * @brief      XHeaderView 表头视图（对标 Qt 6.8 QHeaderView）。
 * @details    管理水平（列）或垂直（行）表头区间的尺寸布局；表头文本由
 *             关联模型（XAbstractItemModel）提供，渲染由 XTableView 统一
 *             完成（本类只负责几何：count/sectionSize/sectionPosition）。
 * @note       模块总开关 XTABLEWIDGET_ON；XHeaderView 为 XWidget 派生。
 * @note       视口归属：Qt 中表头视口（viewport）由 QAbstractScrollArea
 *             基类承载；本项目表头渲染由 XTableView 统一完成，本类不持有
 *             视口，视口相关接口以 @note 收敛（见 sectionViewportPosition
 *             与 offset——当前视口坐标系与表头坐标系恒一致）；viewport()
 *             查询（XHeaderView_viewport）收敛为返回自身。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XHEADERVIEW_H
#define XHEADERVIEW_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XAlignment.h"
#if XByteArray_ON
#include "XByteArray.h"
#endif

#if XWIDGET_ON && XTABLEWIDGET_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XHeaderView)
XCLASS_DEFINE_EXTEND_END(XHeaderView, XWidget)

/** @brief 表头视图对象；m_base 必须是第一个成员（嵌 XWidget）。 */
typedef struct XHeaderView
{
    XWidget   m_base;      /**< 基类成员；必须是第一个。 */
    int       m_orientation; /**< 方向（0=水平列头，1=垂直行头）。 */
    int       m_count;     /**< 区间数。 */
    int       m_defaultSize; /**< 默认区间尺寸（默认 30）。 */
    XVector*  m_sections;  /**< 区间尺寸表（int；对象拥有）。 */
    bool      m_stretchLast; /**< 末尾拉伸（默认 false）。 */
    int       m_sectionMovedFrom; /**< moveSection 记录（预留）。 */
    bool*     m_hidden;    /**< 区间隐藏表（平行数组；对象拥有）。 */
    int       m_hiddenCount; /**< 隐藏区间数。 */
    XVector*  m_sectionModes; /**< 单段调整模式表（int；-1=跟随全局模式；与 m_sections 平行等长；对象拥有）。 */
    bool      m_sectionsClickable; /**< 区间可点击（对标 sectionsClickable）。 */
    bool      m_sectionsMovable;   /**< 区间可拖动（对标 sectionsMovable）。 */
    bool      m_sortIndicatorShown; /**< 排序指示器可见（默认 false）。 */
    int       m_sortIndicatorSection; /**< 排序指示器区间（-1 无）。 */
    int       m_sortIndicatorOrder;   /**< 排序方向（0=升序，1=降序）。 */
    bool      m_sortIndicatorClearable; /**< 排序指示器可点击清除（对标 sortIndicatorClearable；缺省 false）。 */
    int       m_minimumSectionSize; /**< 最小区间尺寸（默认 20；对标 minimumSectionSize）。 */
    int       m_maximumSectionSize; /**< 最大区间尺寸（默认 1048575；对标 maximumSectionSize）。 */
    int       m_resizeMode;         /**< 全局调整模式（XHeaderViewResizeMode；默认 Interactive）。 */
    bool      m_firstSectionMovable; /**< 首段可动状态（实际生效还需 m_sectionsMovable）。 */
    int       m_defaultAlignment;   /**< 缺省文本对齐（XAlignment 位标志组合）。 */
    bool      m_highlightSections;  /**< 选中段高亮开关（默认 false）。 */
    bool      m_cascadingResizes;   /**< 级联调整开关（默认 false；级联行为未接）。 */
    int       m_resizeContentsPrecision; /**< ResizeToContents 测算精度（默认 1000）。 */
    int       m_offset;    /**< 表头滚动偏移（像素；仅存储承载，滚动联动未接入）。 */
    void*     m_model;     /**< 关联模型不透明承载指针（借用；表头不拥有、不依赖）。 */
} XHeaderView;

/** @brief 排序方向（对标 Qt::SortOrder，数值一致）。 */
typedef enum XHeaderViewSortOrder
{
    XHeaderViewSortOrder_Ascending = 0,  /**< 升序。 */
    XHeaderViewSortOrder_Descending = 1  /**< 降序。 */
} XHeaderViewSortOrder;

/** @brief 区间调整模式（对标 Qt::HeaderViewResizeMode，数值一致）。 */
typedef enum XHeaderViewResizeMode
{
    XHeaderViewResizeMode_Interactive = 0,      /**< 用户可交互调整（缺省）。 */
    XHeaderViewResizeMode_Fixed = 1,            /**< 固定尺寸，用户不可调整。 */
    XHeaderViewResizeMode_Stretch = 2,          /**< 随可用空间自动拉伸均分。 */
    XHeaderViewResizeMode_ResizeToContents = 3  /**< 按内容自适应（内容测算未接）。 */
} XHeaderViewResizeMode;

/* ==================== 生命周期 ==================== */

XVtable* XHeaderView_class_init(void);
/** @brief 初始化表头。
 * @param self 目标表头；不可为 NULL。
 * @param parent 父控件借用指针；可为 NULL。
 * @param flags 窗口标志。
 * @param orientation 方向（0=水平，1=垂直）。
 * @return 无返回值。
 */
void XHeaderView_init(XHeaderView* self, XWidget* parent, XWidgetFlags flags,
                      int orientation);
/** @brief 使用指定内存类型创建表头。
 * @param memory 内存类型。
 * @param parent 父控件借用指针。
 * @param flags 窗口标志。
 * @param orientation 方向（0=水平，1=垂直）。
 * @return 新建对象；失败 NULL。
 */
XHeaderView* XHeaderView_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags, int orientation);
#define XHeaderView_create(parent, flags, orientation) \
    XHeaderView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags), \
                          (orientation))
#define XHeaderView_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
#define XHeaderView_delete_base(self) XWidget_delete_base((XWidget*)(self))

/* ==================== 几何 ==================== */

/** @brief 查询方向。 @param self 目标表头。 @return 0=水平，1=垂直。 */
int XHeaderView_orientation(const XHeaderView* self);
/** @brief 查询区间数。 @param self 目标表头。 @return 区间数。 */
int XHeaderView_count(const XHeaderView* self);
/** @brief 设置区间数（新增用默认尺寸；缩小编排尾部）。
 * @param self 目标表头。
 * @param count 区间数（>=0）。
 * @return 无返回值。
 * @note 段数实际变化时发射 sectionCountChanged 与 geometriesChanged。
 */
void XHeaderView_setCount(XHeaderView* self, int count);
/** @brief 读取区间尺寸。
 * @param self 目标表头。
 * @param section 区间号（越界返回默认尺寸）。
 * @return 区间尺寸（像素）。
 */
int XHeaderView_sectionSize(const XHeaderView* self, int section);
/** @brief 设置区间尺寸。
 * @param self 目标表头。
 * @param section 区间号（越界忽略）。
 * @param size 尺寸（像素；<=0 忽略）。
 * @note 全局模式为 Stretch 时 Qt 会重分配拉伸宽度；本项目仍直接存值
 *       （模式承载范围见 setSectionResizeMode）。
 * @note 尺寸实际变化时发射 sectionResized 与 geometriesChanged。
 * @return 无返回值。
 */
void XHeaderView_setSectionSize(XHeaderView* self, int section, int size);
/**
 * @brief 调整区间尺寸（对标 QHeaderView::resizeSection；程序化调整入口）。
 *
 *        @note 与 setSectionSize 的分工：resizeSection 预先按
 *        minimumSectionSize/maximumSectionSize 钳制目标尺寸（Qt 亦同），
 *        随后转发 setSectionSize，由后者发射 sectionResized 与
 *        geometriesChanged。差异点：Qt 允许 size==0（并建议改用
 *        hideSection），本实现沿用 setSectionSize 的 >0 约束拒绝 0；
 *        Qt 对隐藏段将尺寸记入独立隐藏表，本实现隐藏段仍持有原尺寸表
 *        项，故直接改写该表项即可。Qt 中 Stretch 模式触发的重分配与
 *        级联调整未接入（同 setSectionSize 的简化说明）。
 *
 * @param self 目标表头。
 * @param logicalIndex 区间号（越界忽略）。
 * @param size 尺寸（像素；先钳制到 [min,max]；<=0 忽略）。
 * @return 无返回值。
 */
void XHeaderView_resizeSection(XHeaderView* self, int logicalIndex, int size);
/** @brief 查询默认区间尺寸。 @param self 目标表头。 @return 默认尺寸。 */
int XHeaderView_defaultSectionSize(const XHeaderView* self);
/** @brief 设置默认区间尺寸。 @param self 目标表头。 @param size 尺寸（像素）。 */
void XHeaderView_setDefaultSectionSize(XHeaderView* self, int size);
/**
 * @brief 恢复默认区间尺寸缺省值（对标 QHeaderView::resetDefaultSectionSize）。
 *
 *        @note Qt 恢复为样式/字体测算的缺省值；项目缺省值为固定常量
 *        30（见 XHEADERVIEW_DEFAULT_SECTION_SIZE），本函数将
 *        defaultSectionSize 复位为该值。已有段尺寸不受影响。
 *
 * @param self 目标表头；NULL 忽略。
 * @return 无返回值。
 */
void XHeaderView_resetDefaultSectionSize(XHeaderView* self);
/** @brief 查询区间起始位置（对标 sectionPosition）。
 * @param self 目标表头。
 * @param section 区间号。
 * @return 起始坐标（像素）；越界返回 -1。
 */
int XHeaderView_sectionPosition(const XHeaderView* self, int section);
/**
 * @brief 查询区间在视口中的像素位置（对标 QHeaderView::sectionViewportPosition）。
 *
 *        @note offset 已有存储承载但滚动联动未接入（写入值不参与位置
 *        计算），视口坐标系与 sectionPosition 的表头坐标系保持一致，
 *        返回值恒等。
 *
 * @param self 目标表头。
 * @param section 区间号。
 * @return 起始坐标（像素）；越界返回 -1。
 */
int XHeaderView_sectionViewportPosition(const XHeaderView* self, int section);
/** @brief 末尾拉伸开关。
 * @param self 目标表头。
 * @param stretch true 拉伸。
 * @return 无返回值。
 */
void XHeaderView_setStretchLastSection(XHeaderView* self, bool stretch);
/** @brief 查询末尾拉伸开关（对标 Qt 属性读取器 stretchLastSection；isStretchLastSection 的别名）。 @param self 目标表头。 @return 拉伸返回 true。 */
bool XHeaderView_stretchLastSection(const XHeaderView* self);
/** @brief 查询末尾拉伸开关（旧式命名，同 stretchLastSection）。 @param self 目标表头。 @return 拉伸返回 true。 */
bool XHeaderView_isStretchLastSection(const XHeaderView* self);

/* ==================== 段管理（对标 QHeaderView 段族） ==================== */

/**
 * @brief 设置区间隐藏状态（对标 QHeaderView::setSectionHidden）。
 *
 *        @note Qt 中 setSectionHidden 为原始入口，hideSection/showSection
 *        是其便捷内联封装；本实现相反——本函数转发 hideSection(true) 或
 *        showSection(false)，信号发射语义与二者完全一致。
 *
 * @param self 目标表头。
 * @param section 区间号（越界忽略）。
 * @param hide true 隐藏，false 显示。
 * @return 无返回值。
 */
void XHeaderView_setSectionHidden(XHeaderView* self, int section, bool hide);
/** @brief 隐藏区间（对标 hideSection；状态实际变化时发射 geometriesChanged）。 @param self 目标表头。 @param section 区间号（越界忽略）。 @return 无返回值。 */
void XHeaderView_hideSection(XHeaderView* self, int section);
/** @brief 显示区间（对标 showSection；状态实际变化时发射 geometriesChanged）。 @param self 目标表头。 @param section 区间号（越界忽略）。 @return 无返回值。 */
void XHeaderView_showSection(XHeaderView* self, int section);
/** @brief 查询区间是否隐藏（对标 isSectionHidden）。 @param self 目标表头。 @param section 区间号。 @return 隐藏返回 true；越界返回 false。 */
bool XHeaderView_isSectionHidden(const XHeaderView* self, int section);
/** @brief 查询隐藏区间数（对标 hiddenSectionCount）。 @param self 目标表头。 @return 隐藏数。 */
int XHeaderView_hiddenSectionCount(const XHeaderView* self);
/** @brief 设置区间可点击（对标 setSectionsClickable）。 @param self 目标表头。 @param clickable true 可点击。 @return 无返回值。 */
void XHeaderView_setSectionsClickable(XHeaderView* self, bool clickable);
/** @brief 查询区间可点击（对标 sectionsClickable）。 @param self 目标表头。 @return 可点击返回 true。 */
bool XHeaderView_sectionsClickable(const XHeaderView* self);
/** @brief 设置区间可拖动（对标 setSectionsMovable）。 @param self 目标表头。 @param movable true 可拖动。 @return 无返回值。 */
void XHeaderView_setSectionsMovable(XHeaderView* self, bool movable);
/** @brief 查询区间可拖动（对标 sectionsMovable）。 @param self 目标表头。 @return 可拖动返回 true。 */
bool XHeaderView_sectionsMovable(const XHeaderView* self);
/**
 * @brief 移动区间（对标 moveSection；from 位置移到 to）。
 * @param self 目标表头。
 * @param from 源区间号。
 * @param to 目标区间号。
 * @return 无返回值。
 * @note 实际移动时发射 sectionMoved(to, from, to)（逻辑序与视觉序恒
 *       一致，移动后该段逻辑号即新位置 to）与 geometriesChanged。
 */
void XHeaderView_moveSection(XHeaderView* self, int from, int to);
/**
 * @brief 交换两个区间（对标 swapSections）。
 * @param self 目标表头。
 * @param first 区间号。
 * @param second 区间号。
 * @return 无返回值。
 * @note 实际交换时发射两次 sectionMoved（分别描述 first→second 与
 *       second→first 两个方向的移动）与 geometriesChanged。
 */
void XHeaderView_swapSections(XHeaderView* self, int first, int second);
/** @brief 设置排序指示器（对标 setSortIndicator；发射 sortIndicatorChanged）。
 * @param self 目标表头。 @param section 区间号。 @param order 排序方向。 @return 无返回值。 */
void XHeaderView_setSortIndicator(XHeaderView* self, int section, int order);
/** @brief 查询排序指示器区间（对标 sortIndicatorSection）。 @param self 目标表头。 @return 区间号（-1 无）。 */
int XHeaderView_sortIndicatorSection(const XHeaderView* self);
/** @brief 查询排序方向（对标 sortIndicatorOrder）。 @param self 目标表头。 @return 排序方向。 */
int XHeaderView_sortIndicatorOrder(const XHeaderView* self);
/** @brief 设置排序指示器可见（对标 setSortIndicatorShown）。 @param self 目标表头。 @param shown true 显示。 @return 无返回值。 */
void XHeaderView_setSortIndicatorShown(XHeaderView* self, bool shown);
/** @brief 查询排序指示器可见（对标 isSortIndicatorShown）。 @param self 目标表头。 @return 显示返回 true。 */
bool XHeaderView_isSortIndicatorShown(const XHeaderView* self);
/**
 * @brief 设置排序指示器可点击清除（对标 setSortIndicatorClearable；Qt 6.1 起）。
 *
 *        @note 仅存状态：为 true 时用户对同段在升序/降序间再次点击应
 *        清除指示器（区间置 -1、恢复模型原始排序）；本实现尚无表头点击
 *        交互路径，翻转/清除行为未接入。状态实际变化时发射
 *        sortIndicatorClearableChanged（对标 Qt NOTIFY 语义）。
 *
 * @param self 目标表头。 @param clearable true 允许点击清除。 @return 无返回值。
 */
void XHeaderView_setSortIndicatorClearable(XHeaderView* self, bool clearable);
/** @brief 查询排序指示器可点击清除（对标 isSortIndicatorClearable；缺省 false）。 @param self 目标表头。 @return 可清除返回 true。 */
bool XHeaderView_isSortIndicatorClearable(const XHeaderView* self);

/* ==================== 尺寸上下限/模式与状态（对标 QHeaderView min/max/resizeMode 族） ==================== */

/**
 * @brief 设置最大区间尺寸（对标 setMaximumSectionSize）。
 *
 *        @note 设置时对超出上限的既有段立即钳制（Qt 亦同；差异点：
 *        Qt 跳过隐藏段，本实现统一钳制全部段，保证 show 后仍合规）。
 *        传入 -1 表示重置为硬上限 1048575（Qt 语义）。
 *
 * @param self 目标表头。
 * @param size 最大尺寸（像素；-1 重置为上限；其余 <0 或超上限值忽略）。
 * @return 无返回值。
 */
void XHeaderView_setMaximumSectionSize(XHeaderView* self, int size);
/** @brief 查询最大区间尺寸（对标 maximumSectionSize）。 @param self 目标表头。 @return 最大尺寸（像素；缺省 1048575）。 */
int XHeaderView_maximumSectionSize(const XHeaderView* self);
/**
 * @brief 设置最小区间尺寸（对标 setMinimumSectionSize）。
 *
 *        @note Qt 缺省按字体测算，项目简化为固定缺省值 20；设置时对
 *        不足下限的既有段立即抬升（Qt 亦同；隐藏段处理差异同上）。
 *
 * @param self 目标表头。
 * @param size 最小尺寸（像素；<0 或超上限忽略）。
 * @return 无返回值。
 */
void XHeaderView_setMinimumSectionSize(XHeaderView* self, int size);
/** @brief 查询最小区间尺寸（对标 minimumSectionSize）。 @param self 目标表头。 @return 最小尺寸（像素；缺省 20）。 */
int XHeaderView_minimumSectionSize(const XHeaderView* self);
/**
 * @brief 设置全局区间调整模式（对标 setSectionResizeMode 全局重载）。
 *
 *        @note 对标 Qt 6.8：该重载设置全局模式后，当模式为 Stretch 或
 *        ResizeToContents（hasAutoResizeSections）时调度
 *        doDelayedResizeSections（等效 resizeSections(mode)）；本实现
 *        收敛为同步调用 resizeSections(mode)——Stretch 模式以当前
 *        length 充当可用空间在可见段间均分（发射点见 resizeSections
 *        @note），其余模式内部仅调度全量重绘。Qt 的 ResizeToContents
 *        内容测算仍未接入。
 *        @note 对标 Qt setGlobalHeaderResizeMode：全局模式会覆写全部段
 *        的单段模式；本实现以清除全部单段覆写（复位为跟随全局）达成
 *        等价语义。
 *
 * @param self 目标表头。
 * @param mode 调整模式（XHeaderViewResizeMode 取值；越界值忽略）。
 * @return 无返回值。
 */
void XHeaderView_setSectionResizeMode(XHeaderView* self, int mode);
/** @brief 查询全局区间调整模式（对标 sectionResizeMode 全局语义）。 @param self 目标表头。 @return 调整模式（缺省 Interactive）。 */
int XHeaderView_sectionResizeMode(const XHeaderView* self);
/**
 * @brief 按给定模式批量重设全部区间尺寸（对标 QHeaderView::resizeSections）。
 *
 *        @note 简化：Qt 按给定模式立即重排（Stretch 将视口剩余空间均分
 *        到各段、ResizeToContents 按内容测算、Interactive/Fixed 钳制到
 *        现尺寸），且不改动段的模式存储；本实现 Stretch 模式以当前
 *        length 充当可用空间在可见段间均分（总长不变，逐段钳制到
 *        [min,max]，有实际变化时发射一次 geometriesChanged，不逐段发
 *        sectionResized，同 restoreState 的批量路径先例）；其余模式
 *        （内容测算/视口宽度未接）仅调度全量重绘（update）。本函数
 *        不修改全局/单段模式存储（Qt 亦同）。
 *
 * @param self 目标表头；NULL 忽略。
 * @param mode 批量重设采用的调整模式（XHeaderViewResizeMode 取值；越界值忽略）。
 * @return 无返回值。
 */
void XHeaderView_resizeSections(XHeaderView* self, int mode);
/**
 * @brief 设置单段区间调整模式（对标 setSectionResizeMode(int,int) 单段
 *        重载；C 无重载，以 At 后缀区分）。
 *
 *        @note 仅存状态：单段模式暂不驱动重排（与全局模式的简化说明
 *        一致）；-1 哨兵表示清除覆写、恢复跟随全局模式。Qt 中
 *        stretchLastSection 为真时末段模式被布局忽略，本实现照常存储。
 *
 * @param self 目标表头。
 * @param section 区间号（越界忽略）。
 * @param mode 调整模式（XHeaderViewResizeMode 取值；越界值忽略）。
 * @return 无返回值。
 */
void XHeaderView_setSectionResizeModeAt(XHeaderView* self, int section,
                                        int mode);
/**
 * @brief 查询单段生效的区间调整模式（对标 sectionResizeMode(int) 单段
 *        重载）。
 * @param self 目标表头。
 * @param logicalIndex 区间号。
 * @return 该段显式设置的模式；无覆写或 self 为 NULL 时返回全局模式
 *         （Qt 对越界返回 Fixed，本实现收敛为全局模式以免歧义）。
 */
int XHeaderView_sectionResizeModeAt(const XHeaderView* self, int logicalIndex);
/**
 * @brief 设置首段可动（对标 setFirstSectionMovable）。
 *
 *        @note 仅存状态：首段实际可拖动还需 setSectionsMovable(true)
 *        配合（isFirstSectionMovable 亦要求两者同时为真）。
 *
 * @param self 目标表头。 @param movable true 允许首段拖动。 @return 无返回值。
 */
void XHeaderView_setFirstSectionMovable(XHeaderView* self, bool movable);
/** @brief 查询首段是否可动（对标 isFirstSectionMovable；需 sectionsMovable 同时为真）。 @param self 目标表头。 @return 可动返回 true。 */
bool XHeaderView_isFirstSectionMovable(const XHeaderView* self);
/**
 * @brief 复位表头段状态（对标 reset）。
 *
 *        @note 简化：保留段数，将全部段尺寸恢复为 defaultSectionSize、
 *        取消全部隐藏、单段调整模式覆写复位为跟随全局、清空排序指示器
 *        （区间 -1、升序、不显示）；不改动 min/max/全局模式/对齐等
 *        表头级配置。Qt 的 reset 依据模型重建段，与本实现的"就地恢复
 *        缺省"行为不同。存在段时发射 geometriesChanged（尺寸与隐藏
 *        状态的恢复影响表头几何）。
 *
 * @param self 目标表头。 @return 无返回值。
 */
void XHeaderView_reset(XHeaderView* self);
/**
 * @brief 设置表头文本缺省对齐（对标 setDefaultAlignment）。
 * @param self 目标表头。
 * @param alignment 对齐位标志（取值域同 XAlignment/Qt::Alignment；
 *        缺省水平头为 XAlignment_Center，垂直头为 Left|VCenter）。
 * @return 无返回值。
 */
void XHeaderView_setDefaultAlignment(XHeaderView* self, int alignment);
/** @brief 查询表头文本缺省对齐（对标 defaultAlignment）。 @param self 目标表头。 @return 对齐位标志（XAlignment 组合；NULL 返回 0）。 */
int XHeaderView_defaultAlignment(const XHeaderView* self);
/**
 * @brief 查询参与拉伸的区间数（对标 stretchSectionCount）。
 *
 *        @note 简化：Qt 统计 Stretch 模式段数并叠加末段拉伸；本项目
 *        模式未参与布局，stretchLastSection 生效即计 1，否则 0。
 *
 * @param self 目标表头。 @return 拉伸区间数（0 或 1）。
 */
int XHeaderView_stretchSectionCount(const XHeaderView* self);
/** @brief 查询是否存在隐藏区间（对标 sectionsHidden）。 @param self 目标表头。 @return 任一区间隐藏返回 true。 */
bool XHeaderView_sectionsHidden(const XHeaderView* self);
/** @brief 设置选中段高亮开关（对标 setHighlightSections）。 @param self 目标表头。 @param highlight true 高亮。 @return 无返回值。 */
void XHeaderView_setHighlightSections(XHeaderView* self, bool highlight);
/** @brief 查询选中段高亮开关（对标 highlightSections；缺省 false）。 @param self 目标表头。 @return 高亮返回 true。 */
bool XHeaderView_highlightSections(const XHeaderView* self);
/**
 * @brief 设置级联调整开关（对标 setCascadingSectionResizes）。
 *
 *        @note 仅存状态：交互调整触及最小尺寸后向后续段级联收缩的
 *        行为未接入。核对 Qt：cascadingSectionResizes 仅作用于交互式
 *        段落调整（QHeaderViewPrivate::cascade），与 setSectionResizeMode
 *        切换全局模式触发的批量重排（已接入，见 setSectionResizeMode
 *        全局重载的 @note）无关。
 *
 * @param self 目标表头。 @param enable true 级联。 @return 无返回值。
 */
void XHeaderView_setCascadingSectionResizes(XHeaderView* self, bool enable);
/** @brief 查询级联调整开关（对标 cascadingSectionResizes；缺省 false）。 @param self 目标表头。 @return 级联返回 true。 */
bool XHeaderView_cascadingSectionResizes(const XHeaderView* self);
/**
 * @brief 设置 ResizeToContents 测算精度（对标 setResizeContentsPrecision）。
 *
 *        @note 仅存状态：按精度扫描内容测算尺寸的行为未接入（与
 *        sectionSizeHint 的无内容感知一致）。
 *
 * @param self 目标表头。
 * @param precision 参与测算的段数（0=仅可见区，-1=全部；缺省 1000）。
 * @return 无返回值。
 */
void XHeaderView_setResizeContentsPrecision(XHeaderView* self, int precision);
/** @brief 查询 ResizeToContents 测算精度（对标 resizeContentsPrecision；缺省 1000）。 @param self 目标表头。 @return 精度值。 */
int XHeaderView_resizeContentsPrecision(const XHeaderView* self);

/* ==================== 布局与状态持久化（对标 doItemsLayout/saveState/restoreState） ==================== */

/**
 * @brief 触发一次全量布局更新（对标 QHeaderView::doItemsLayout）。
 *
 *        @note 简化：Qt 该入口会重算段几何并驱动一次模型布局
 *        （QAbstractItemView::doItemsLayout）；本项目段几何即时维护、
 *        文本渲染由 XTableView 统一完成，此处仅调度整控件全量重绘
 *        （XWidget_update），等效于"全量 update"。
 *
 * @param self 目标表头；NULL 忽略。
 * @return 无返回值。
 */
void XHeaderView_doItemsLayout(XHeaderView* self);
#if XByteArray_ON
/**
 * @brief 保存表头段状态（对标 QHeaderView::saveState；返回值归调用者
 *        持有并释放）。
 *
 *        @note 序列化格式：XHV 文本格式（固定位宽十进制拼接，全部 ASCII），
 *        v1 布局依次为：
 *        - 魔数 "XHV"（3 字节）+ 版本 "001"（3 字节）；
 *        - 方向（1 位数字）、段数、默认尺寸、最小区间尺寸、最大区间尺寸、
 *          排序指示器区间（均可为 7 位十进制，负数含符号位）；
 *        - 布尔字段各 1 位数字，顺序为：stretchLastSection、
 *          sortIndicatorShown、sortIndicatorOrder（0/1）、
 *          sortIndicatorClearable、sectionsClickable、sectionsMovable、
 *          firstSectionMovable、highlightSections、cascadingSectionResizes；
 *        - 7 位十进制：resizeContentsPrecision、defaultAlignment；
 *        - 每段 3 字段：尺寸（7 位）、隐藏（1 位）、单段模式（1 字符，
 *          '0'..'3' 为显式模式，'g' 为跟随全局）。
 *        覆盖字段与 Qt QHeaderViewPrivate::write 的状态集合对齐（视觉
 *        序映射表除外——本实现移动即重排，无独立视觉序）。
 *
 * @param self 目标表头；NULL 返回 NULL。
 * @return 状态字节串；分配失败返回 NULL。调用者负责释放。
 */
XByteArray* XHeaderView_saveState(const XHeaderView* self);
/**
 * @brief 恢复 saveState 保存的段状态（对标 QHeaderView::restoreState）。
 *
 *        @note 校验先行：魔数/版本不匹配、方向与当前表头不符或任何
 *        字段非法时整体拒绝并返回 false（不做部分恢复）。恢复成功时：
 *        段数变化经 setCount 发射 sectionCountChanged/geometriesChanged；
 *        排序指示器经 setSortIndicator 恢复（状态实际变化时发射
 *        sortIndicatorChanged，与 Qt 恢复后无条件发射不同）；尺寸/隐藏
 *        状态有实际变化时追加发射一次 geometriesChanged（不逐段发
 *        sectionResized，对标 Qt 恢复路径仅做整体刷新的行为）。
 *
 * @param self 目标表头；NULL 忽略。
 * @param state saveState 产生的状态字节串（借用；NULL 视为失败）。
 * @return 恢复成功返回 true。
 */
bool XHeaderView_restoreState(XHeaderView* self, const XByteArray* state);
#endif /* XByteArray_ON */

/* ==================== 视觉序与位置反查（对标 QHeaderView visualIndex 族） ==================== */

/**
 * @brief 查询区间的视觉序号（对标 QHeaderView::visualIndex）。
 *
 *        @note 当前实现中 moveSection/swapSections 直接重排尺寸表，
 *        "移动即重排"，逻辑序与视觉序恒一致，故恒等返回 logicalIndex；
 *        若后续引入独立视觉映射表则替换此实现。
 *
 * @param self 目标表头；可为 NULL。
 * @param logicalIndex 逻辑区间号。
 * @return 视觉序号；越界或 self 为 NULL 时返回 -1。
 */
int XHeaderView_visualIndex(const XHeaderView* self, int logicalIndex);
/**
 * @brief 查询视觉序号对应的逻辑区间号（对标 QHeaderView::visualIndexAt）。
 *
 *        @note 同 visualIndex：移动即重排，视觉序与逻辑序恒一致，
 *        故恒等返回 visualIndex；若后续引入独立视觉映射表则替换此实现。
 *
 * @param self 目标表头；可为 NULL。
 * @param visualIndex 视觉序号。
 * @return 逻辑区间号；越界或 self 为 NULL 时返回 -1。
 */
int XHeaderView_visualIndexAt(const XHeaderView* self, int visualIndex);
/**
 * @brief 查询视觉序号对应的逻辑区间号（对标 QHeaderView::logicalIndex）。
 *
 *        @note 同 visualIndexAt/visualIndex：移动即重排，视觉序与逻辑序
 *        恒一致，故恒等返回 visualIndex；若后续引入独立视觉映射表则
 *        替换此实现。视觉序不受隐藏区间影响（Qt 亦同）。
 *
 * @param self 目标表头；可为 NULL。
 * @param visualIndex 视觉序号。
 * @return 逻辑区间号；越界或 self 为 NULL 时返回 -1。
 */
int XHeaderView_logicalIndex(const XHeaderView* self, int visualIndex);
/**
 * @brief 像素位置反查逻辑区间号（对标 QHeaderView::logicalIndexAt）。
 *
 *        @note 位置坐标与 sectionPosition 同一坐标系（逐段累加
 *        sectionSize 反查；当前模型下隐藏区间仍占位，不跳过）。
 *
 * @param self 目标表头；可为 NULL。
 * @param position 相对表头起点的像素位置。
 * @return 覆盖该位置的区间号；位置越界（<0 或超出总长）或 self 为
 *         NULL 时返回 -1。
 */
int XHeaderView_logicalIndexAt(const XHeaderView* self, int position);
/**
 * @brief 查询区间尺寸提示（对标 QHeaderView::sectionSizeHint）。
 *
 *        @note 无内容感知：恒返回 defaultSectionSize，不依据区间
 *        内容测算（与 Qt 按内容提示的行为不同）。
 *
 * @param self 目标表头；可为 NULL。
 * @param logicalIndex 逻辑区间号（当前不参与计算）。
 * @return 建议尺寸（像素）；self 为 NULL 时返回 0。
 */
int XHeaderView_sectionSizeHint(const XHeaderView* self, int logicalIndex);
/**
 * @brief 查询表头总长（对标 QHeaderView::length）：全部可见段尺寸之和。
 *
 *        @note 跳过隐藏区间（依据现有 m_hidden 表），与 Qt 中隐藏段
 *        长度按 0 计的语义一致。
 *
 * @param self 目标表头；可为 NULL。
 * @return 可见段尺寸总和（像素）；self 为 NULL 时返回 0。
 */
int XHeaderView_length(const XHeaderView* self);
/**
 * @brief 设置表头滚动偏移（对标 QHeaderView::setOffset）。
 *
 *        @note 仅存储承载：Qt 中该偏移驱动视口滚动与可见段平移；本项目
 *        表头渲染由 XTableView 统一完成，滚动联动未接入，存储值不参与
 *        sectionViewportPosition 等位置计算。
 *
 * @param self 目标表头；NULL 忽略。
 * @param offset 滚动偏移（像素）。
 * @return 无返回值。
 */
void XHeaderView_setOffset(XHeaderView* self, int offset);
/**
 * @brief 查询表头滚动偏移（对标 QHeaderView::offset）。
 *
 *        @note 返回 setOffset/setOffsetToLastSection/setOffsetToSectionPosition
 *        存储的值；滚动联动未接入，该值当前不影响渲染与位置计算
 *        （初值 0）。
 *
 * @param self 目标表头；可为 NULL。
 * @return 存储的滚动偏移；self 为 NULL 时返回 0。
 */
int XHeaderView_offset(const XHeaderView* self);
/**
 * @brief 滚动到最后一个区间（对标 QHeaderView::setOffsetToLastSection）。
 *
 *        @note 简化：Qt 取 offset = length − 视口宽度；本项目视口宽度
 *        未接入（按 0 参与），偏移退化为 length（再钳制到 >=0）。
 *
 * @param self 目标表头；NULL 忽略。
 * @return 无返回值。
 */
void XHeaderView_setOffsetToLastSection(XHeaderView* self);
/**
 * @brief 将偏移置为某区间的起始位置（对标 QHeaderView::setOffsetToSectionPosition）。
 *
 *        @note 简化：Qt 参数为"不计隐藏段的视觉序号"；本项目逻辑序与
 *        视觉序恒一致，直接以 sectionPosition(index) 为偏移；越界忽略。
 *
 * @param self 目标表头；NULL 忽略。
 * @param index 区间号（越界忽略）。
 * @return 无返回值。
 */
void XHeaderView_setOffsetToSectionPosition(XHeaderView* self, int index);
/**
 * @brief 关联模型（不透明承载；C 无虚函数，以 void* 承载 XAbstractItemModel）。
 *
 *        @note 表头独立于模型：Qt 中 QHeaderView 继承自
 *        QAbstractItemView、经模型取 headerData；本项目表头文本由
 *        XTableView 统一取用，本类仅保存该指针（借用、不拥有），设置
 *        后不触发任何重建或信号。
 *
 * @param self 目标表头；NULL 忽略。
 * @param model 模型指针（借用；可为 NULL 表示解除关联）。
 * @return 无返回值。
 */
void XHeaderView_setModel(XHeaderView* self, void* model);

/* ==================== 视口查询（对标 QAbstractScrollArea::viewport） ==================== */

/**
 * @brief 查询表头视口（对标 QHeaderView::viewport；实际为
 *        QAbstractScrollArea::viewport 的收敛实现）。
 *
 *        @note Qt 中表头视口由 QAbstractScrollArea 基类持有；本项目表头
 *        自绘、渲染由 XTableView 统一完成，无独立视口控件，故收敛为返回
 *        自身（(XWidget*)self），等效于"整个表头即视口"。对返回值调用
 *        XWidget_update 等价于刷新表头整体。
 *
 * @param self 目标表头；NULL 返回 NULL。
 * @return 自身 XWidget* 借用指针；self 为 NULL 时返回 NULL。
 */
XWidget* XHeaderView_viewport(XHeaderView* self);

/**
 * @brief sectionClicked(int) 信号（对标 QHeaderView::sectionClicked；
 *        载荷：区间号）。
 * @note 核对 Qt 6.8：sectionClicked 在鼠标释放事件中发射
 *       （sectionsClickable 为真且释放在按压段上，随后
 *       flipSortIndicator→setSortIndicator），并非由 setSortIndicator
 *       直接触发；程序化调用 setSortIndicator 只发
 *       sortIndicatorChanged、不发本信号。本实现暂无表头鼠标事件路径，
 *       句柄预留（尚未有发射点）。
 */
void* XHeaderView_sectionClicked_signal(XHeaderView* self, int section);
/**
 * @brief sectionPressed(int) 信号（对标 QHeaderView::sectionPressed；
 *        载荷：区间号）。
 * @note 本实现暂无表头鼠标事件路径，句柄预留（尚未有发射点）。
 */
void* XHeaderView_sectionPressed_signal(XHeaderView* self, int section);
/** @brief sortIndicatorChanged(int,int) 信号（对标 QHeaderView::sortIndicatorChanged；载荷：区间号、方向）。 */
void* XHeaderView_sortIndicatorChanged_signal(XHeaderView* self, int logicalIndex, int order);
/**
 * @brief sortIndicatorClearableChanged(bool) 信号（对标
 *        QHeaderView::sortIndicatorClearableChanged；载荷：可清除状态）。
 * @note 发射点：setSortIndicatorClearable（状态实际变化时）。
 */
void* XHeaderView_sortIndicatorClearableChanged_signal(XHeaderView* self,
                                                      bool clearable);
/**
 * @brief sectionDoubleClicked(int) 信号（对标 QHeaderView::sectionDoubleClicked；
 *        载荷：区间号）。
 * @note 本实现暂无表头鼠标事件路径，句柄预留（尚未有发射点）。
 */
void* XHeaderView_sectionDoubleClicked_signal(XHeaderView* self, int section);
/**
 * @brief sectionEntered(int) 信号（对标 QHeaderView::sectionEntered；
 *        载荷：区间号）。
 * @note 本实现暂无悬停事件路径，句柄预留（尚未有发射点）。
 */
void* XHeaderView_sectionEntered_signal(XHeaderView* self, int section);
/**
 * @brief sectionHandleDoubleClicked(int) 信号（对标
 *        QHeaderView::sectionHandleDoubleClicked；载荷：区间号）。
 * @note 本实现暂无排序指示器把手交互路径，句柄预留（尚未有发射点）。
 */
void* XHeaderView_sectionHandleDoubleClicked_signal(XHeaderView* self, int section);
/**
 * @brief sectionMoved(int,int,int) 信号（对标 QHeaderView::sectionMoved；
 *        载荷：逻辑区间号、原视觉序、新视觉序）。
 * @note 发射点：moveSection/swapSections（当前逻辑序与视觉序恒一致，
 *       载荷的"逻辑号"即该段移动后的位置）。
 */
void* XHeaderView_sectionMoved_signal(XHeaderView* self, int logicalIndex,
                                      int oldVisualIndex, int newVisualIndex);
/**
 * @brief sectionsMoved(int,int,int) 信号句柄（sectionMoved 的别名句柄；
 *        载荷：逻辑区间号、原视觉序、新视觉序）。
 * @note 与 sectionMoved 共用同一信号令牌（函数地址）：经本句柄建立的
 *       连接同样接收 moveSection/swapSections 的既有发射（发射语义与
 *       载荷口径见 sectionMoved——逻辑号取移动后的新位置）。
 */
void* XHeaderView_sectionsMoved_signal(XHeaderView* self, int logicalIndex,
                                       int oldVisualIndex, int newVisualIndex);
/**
 * @brief sectionResized(int,int,int) 信号（对标 QHeaderView::sectionResized；
 *        载荷：逻辑区间号、原尺寸、新尺寸）。
 * @note 发射点：setSectionSize（尺寸实际变化时）。
 */
void* XHeaderView_sectionResized_signal(XHeaderView* self, int logicalIndex,
                                        int oldSize, int newSize);
/**
 * @brief sectionCountChanged(int,int) 信号（对标
 *        QHeaderView::sectionCountChanged；载荷：原段数、新段数）。
 * @note 发射点：setCount（段数实际变化时）。
 */
void* XHeaderView_sectionCountChanged_signal(XHeaderView* self, int oldCount,
                                             int newCount);
/**
 * @brief geometriesChanged() 信号（对标 QHeaderView::geometriesChanged；
 *        无载荷）。
 * @note 发射点：影响表头几何的操作——setSectionSize/moveSection/
 *       swapSections/hideSection/showSection/setCount/reset。
 */
void* XHeaderView_geometriesChanged_signal(XHeaderView* self);
/**
 * @brief headerDataChanged(int) 信号句柄（对标 QHeaderView::headerDataChanged；
 *        载荷：区间号）。
 * @note Qt 中为公开槽（签名含方向与首末逻辑号，由模型 headerDataChanged
 *       触发）；本项目按列模型表头数据变化场景收敛为单 int 载荷，由
 *       XTableWidget 转发调用。本轮仅加句柄，转发接线尚未完成（暂无
 *       发射点）。
 */
void* XHeaderView_headerDataChanged_signal(XHeaderView* self, int section);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XTABLEWIDGET_ON */
#endif /* XHEADERVIEW_H */
