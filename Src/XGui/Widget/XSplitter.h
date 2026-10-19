/**
 * @file       XSplitter.h
 * @brief      XSplitter 分割器控件（对标 Qt 6.8 QSplitter 全部公共 API）。
 * @details    功能范围：
 *             - 页面管理：addWidget/insertWidget/widget/count/indexOf；
 *               m_pages 权威序表（页序与子件表解耦，修 XVector_move 不
 *               平移并行数组的错位隐患）；把手增删走 childEvent
 *               （CHILD_ADDED 自动收录/CHILD_REMOVED 摘槽，m_blockChildAdd
 *               护栏覆盖 insertWidget/replaceWidget/setRubberBand 三处，
 *               对标 qsplitter.cpp:809/:1149/:1314）；
 *             - 方向：Horizontal（默认）/Vertical；setOrientation 遍历
 *               把手传播+STYLE_CHANGE updateHandles（qsplitter.cpp:
 *               987-990/:1634-1640）；
 *             - 尺寸：setSizes/sizes、setStretchFactor、handleWidth；
 *               尚未定尺寸（未 setSizes/拖动）时各页均分，定尺寸后
 *               容器缩放按页尺寸意图比例保持（对标 Qt sizer）；
 *             - 折叠：setChildrenCollapsible（默认 true）、
 *               setCollapsible/isCollapsible（逐页覆写）；farMin/farMax
 *               折叠迟滞（Threshold=40，qsplitter.cpp:541-739）+
 *               m_collapsed 位（0/1 两态，由 setSizes 0/拖 far/restore
 *               重建，saveState 不扩格式）；
 *             - opaqueResize（未显式设置时回落样式
 *               SH_Splitter_OpaqueResize）；非透明拖动经 XRubberBand
 *               子件呈现（setRubberBand，pos<0 deleteLater）；
 *             - 把手：每对相邻页之间一把手部件（XSplitterHandle，
 *               工厂=EXSplitter_CreateHandle 虚槽，对标 createHandle），
 *               handle(i) 取把手对象、几何经 XWidget_geometry(把手) 读回；
 *             - 状态序列化：saveState/restoreState（restoreState 按实际
 *               页数钳位消费，对标 qsplitter.cpp:1712-1732）；
 *               getRange 查询近区间（含 +hw·B_left 修正）；
 *             - 交互：把手部件自收拖动（水平左右/垂直上下），落位经
 *               moveSplitter 发射 splitterMoved(pos, index)；
 *             - 子控件所有权：addWidget 后归分割器（Qt 语义）。
 * @note       模块总开关 XSPLITTER_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XFRAME_ON、XByteArray_ON。
 * @author     XinYueC 团队
 */
#ifndef XSPLITTER_H
#define XSPLITTER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XFrame.h"
#if XByteArray_ON
#include "XByteArray.h"
#endif
#if XWIDGET_ON && XFRAME_ON && XSPLITTER_ON
/* [对齐 2026-10-18] XOrientation（XGridLayout.h:82-86，XLAYOUT_GRID
 * 门内；配置联动保证分割器开启时布局模块必开）。missingPlan#8 登记
 * 该 typedef 终将迁 XGuiConfig.h，迁移后此处包含可删。 */
#include "XGridLayout.h"

XCLASS_DEFINE_BEGING(XSplitter)
XCLASS_DEFINE_ENUM(XSplitter, CreateHandle) = XCLASS_VTABLE_GET_SIZE(XFrame), /**< 把手工厂虚槽（对标 Qt createHandle；类槽先例 XAbstractSocket.h）。 */
XCLASS_DEFINE_END(XSplitter)

/* [对齐 2026-10-18] 把手部件类完整定义于 XSplitterHandle.h（其反向
 * 包含本头），此处前置类型名供 handle() 原型使用（对标 Qt
 * qsplitter.h:67 的 QSplitterHandle* 返回形态）。 */
typedef struct XSplitterHandle XSplitterHandle;

/**
 * @brief      XSplitter 控件对象；m_base 必须是第一个成员。
 */
typedef struct XSplitter
{
    XFrame m_base;              /**< 基类成员；必须是第一个。 */
    XWidget** m_pages;          /**< 页权威序表（页序与 children 解耦：
                                     children 表仅供父子链/绘制，页序
                                     由 xsp_insertSlot/xsp_removeSlot/
                                     xsp_moveSlot 单点维护）。 */
    XSplitterHandle** m_handles; /**< 把手部件数组（对标 Qt m_handles；
                                     把手 j 位于页 j 与页 j+1 之间；
                                     m_index 由槽位操作单点回写）。 */
    int8_t* m_collapsed;        /**< 折叠位（**仅 0=展开/1=折叠两态**，创建
                                     /扩容补 0；与 m_collapsible 的 -1=
                                     未覆写哨兵不同域不互混）。由 setSizes
                                     0/拖 far 落 0/restore 按位重建；不落
                                     快照（Qt saveState 无 collapsed，
                                     qsplitter.cpp:1655-1678 实读）。 */
    int* m_collapsible;         /**< 逐页折叠覆写（-1 未设置）。 */
    int* m_sizes;               /**< 页尺寸意图（对标 Qt sizer；0=未设定
                                     或折叠，全零时布局均分；拖动/setSizes
                                     落位后写入，容器缩放按此比例重排）。 */
    int* m_stretch;             /**< 逐页拉伸因子（0=未设定默认，未设定页
                                     参与布局时按此加权）。 */
    void* m_rubberBand;         /**< 非透明拖动橡皮筋（XRubberBand(Line)
                                     子件，对标 Qt rubberBand；void* 承载
                                     以免 XRUBBERBAND_ON 裁剪下类型缺失）。 */
    /* [内存治理] 短整型容量/状态收 int16_t。 */
    int16_t m_collapsibleCap;   /**< 折叠覆写数组容量。 */
    int16_t m_pagesCap;         /**< 页序表容量。 */
    int16_t m_pagesCount;       /**< 现役页数（权威计数）。 */
    int16_t m_collapsedCap;     /**< 折叠位数组容量。 */
    int16_t m_sizesCap;         /**< 页尺寸意图数组容量。 */
    int16_t m_stretchCap;       /**< 拉伸因子数组容量。 */
    int16_t m_handlesCap;       /**< 把手指针数组容量（现役把手数=
                                     pagesCount>0 ? pagesCount-1 : 0 推导）。 */
    int16_t m_handleWidth;      /**< 分隔条宽度（>=0 存储值；-1=样式哨
                                     兵，回落 PM_SplitterWidth；构造显式 5）。 */
    uint8_t m_orientation;      /**< 方向（XOrientation，默认水平）。 */
    /* 标志位字。 */
    uint32_t m_childrenCollapsible : 1; /**< 子页可折叠（默认 true）。 */
    uint32_t m_opaqueResize : 1; /**< 不透明拖动存储值（默认 true）。 */
    uint32_t m_opaqueResizeSet : 1; /**< 已显式设置位（getter 回落样式）。 */
    uint32_t m_blockChildAdd : 1; /**< childEvent 增删护栏（insertWidget/
                                     replaceWidget/setRubberBand 三处，
                                     对标 Qt m_blockChildAdd）。 */
    uint32_t m_inLayout : 1;    /**< 布局重入防护（LAYOUT_REQUEST 直返）。 */
    uint32_t m_splitterFlagsUnused : 27; /**< 保留位。 */
} XSplitter;

/** @brief 把手工厂槽函数型（EXSplitter_CreateHandle）：为分隔点 index
 *         创建把手部件并回写 m_index；默认实现 new XSplitterHandle，
 *         子类可重载定制把手。 */
typedef XSplitterHandle* (*EXSplitter_CreateHandle_t)(XSplitter* self,
                                                      int index);

/* ==================== 生命周期 ==================== */

/**
 * @brief      初始化类虚函数表（对标 Qt 的 metaObject 构建过程）。
 */
XVtable* XSplitter_class_init(void);
/**
 * @brief      初始化控件（对标构造函数）。
 */
void XSplitter_init(XSplitter* self, XWidget* parent, XWidgetFlags flags);
/** @brief 以指定方向初始化（对标 QSplitter(Qt::Orientation, parent*)）。 */
/**
 * @brief      带参初始化控件（对标带参构造函数）。
 */
void XSplitter_init_2(XSplitter* self, XOrientation orientation,
                      XWidget* parent, XWidgetFlags flags);
#define XSplitter_create(parent, flags) XSplitter_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      按指定内存类型创建控件实例。
 */
XSplitter* XSplitter_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags);
#define XSplitter_create_2(orientation, parent, flags) \
    XSplitter_create_ex_2(XCLASS_DEFAULT_MEMORY_TYPE, (orientation), (parent), (flags))
/**
 * @brief      按指定内存类型和参数创建控件实例。
 */
XSplitter* XSplitter_create_ex_2(XMemoryType memory, XOrientation orientation,
                                 XWidget* parent, XWidgetFlags flags);

/* ==================== 页面管理 ==================== */

/**
 * @brief      添加控件。
 */
void XSplitter_addWidget(XSplitter* self, XWidget* widget);
/**
 * @brief      插入控件。
 */
void XSplitter_insertWidget(XSplitter* self, int index, XWidget* widget);
/**
 * @brief      获取内容控件（对标 Qt 同名方法）。
 */
XWidget* XSplitter_widget(const XSplitter* self, int index);
/** @brief XSplittercount（对标 Qt 同名接口）。 */
int XSplitter_count(const XSplitter* self);
/** @brief XSplitterindexOf（对标 Qt 同名接口）。
 * @details 扩把手匹配（对标 qsplitter.cpp:1183-1186）：命中把手 i
 *          返回分隔点 i；页在 m_pages 权威序表线性查。
 */
int XSplitter_indexOf(const XSplitter* self, const XWidget* widget);
/** @brief 替换既有子控件（对标 QSplitter::replaceWidget）。
 * @details 序=护栏→读 geom/wasHidden→先换 m_pages[i]→current 摘父
 *          （CHILD_REMOVED 到达时查无→把手存活）→新页挂父（CHILD_ADDED
 *          被护栏吞）→解除→重排。所有权：新控件归分割器父子链，被
 *          替换旧控件交还调用方（不销毁）并被隐藏。
 */
XWidget* XSplitter_replaceWidget(XSplitter* self, int index,
                                 XWidget* widget);
/** @brief 查询分隔点把手部件（对标 QSplitter::handle，qsplitter.h:67）。
 * @details 分隔点 index 介于页 index 与页 index+1 之间（0..count-2）。
 *          把手为真实子部件（几何布局直配）；矩形经
 *          XWidget_geometry(把手) 读回。可见性随相邻两页显隐（防死态
 *          设计：相邻两页全隐藏才隐，保留收起态可点可展开入口）。
 */
XSplitterHandle* XSplitter_handle(const XSplitter* self, int index);
/** @brief 查询分隔点拖动范围（对标 QSplitter::getRange，近区间）。
 * @details nearMin'(b)=Σ_{i≤b,可见}smartMin(i)+hw·B_left(b)；
 *          nearMax'(b)=contentLen−Σ_{i>b,可见}smartMin(i)+hw·B_left(b)
 *          （B_left=分隔点 b 之前的条带数；+hw·B_left=把手贡献修正，
 *          对标 Qt addContribution 把手宽计入，qsplitter.cpp:545-548；
 *          index≥1 漏计中间条带的公开数值已修正，index0 数值不变）。
 *          far 端（迟滞）由 adjustPos 内部消费，不经本接口。
 * @param self 目标控件；传入 NULL 时返回 false。
 * @param index 分隔点序号（0 起；越界或页数不足 2 返回 false）。
 * @param min 输出最小位置；可为 NULL（忽略）。
 * @param max 输出最大位置；可为 NULL（忽略）。
 * @return 成功返回 true；参数无效返回 false。
 */
bool XSplitter_getRange(const XSplitter* self, int index, int* min, int* max);

/* ==================== 属性 ==================== */

void XSplitter_setOrientation(XSplitter* self, XOrientation orientation);
/** @brief XSplitterorientation（对标 Qt 同名接口）。 */
XOrientation XSplitter_orientation(const XSplitter* self);
/** @brief XSplittersetChildren可折叠（对标 Qt 同名接口）。 */
void XSplitter_setChildrenCollapsible(XSplitter* self, bool collapsible);
/** @brief XSplitterchildren可折叠（对标 Qt 同名接口）。 */
bool XSplitter_childrenCollapsible(const XSplitter* self);
/**
 * @brief      设置可折叠（越界索引告警并早退，对标 Qt
 *             qsplitter.cpp:1040-1043）。
 */
void XSplitter_setCollapsible(XSplitter* self, int index, bool collapsible);
/**
 * @brief      获取可折叠。
 */
bool XSplitter_isCollapsible(const XSplitter* self, int index);
/**
 * @brief      设置不透明调整并置「已显式设置」位（默认回落样式
 *             SH_Splitter_OpaqueResize）。false=非透明拖动：拖动中
 *             只移动 XRubberBand，页几何在释放时落位（对标 Qt 把手
 *             非 opaqueResize 路径）。
 */
void XSplitter_setOpaqueResize(XSplitter* self, bool opaque);
/**
 * @brief      获取不透明调整（未显式设置时回落
 *             SH_Splitter_OpaqueResize，对标 qsplitter.cpp:1472-1484）。
 */
bool XSplitter_opaqueResize(const XSplitter* self);
/**
 * @brief      获取把手宽度（-1 哨兵时回落
 *             PM_SplitterWidth，对标 qsplitter.cpp:1614-1629）。
 */
int XSplitter_handleWidth(const XSplitter* self);
/**
 * @brief      设置把手宽度（-1=恢复样式回落；<-1 拒绝；置后重排）。
 */
void XSplitter_setHandleWidth(XSplitter* self, int width);
/** @brief 把分隔条 index 移到 pos（对标 Qt QSplitterPrivate::moveSplitter
 *         公开化，qsplitter.cpp:1382-1425+doMove :757-791）：pos 经
 *         adjustPos 迟滞（近区间原样；出近区间 delta>width/2 且
 *         delta>=min(40,width) 吸附 far 端折叠，否则弹回近端）→按页
 *         分配位移（隐藏页跳传、钳 smartMin、mayCollapse 折叠）→落
 *         几何→storeSizes→splitterMoved。 */
void XSplitter_moveSplitter(XSplitter* self, int pos, int index);
/** @brief XSplitterset拉伸Factor（对标 QSplitter::setStretchFactor）。 */
void XSplitter_setStretchFactor(XSplitter* self, int index, int stretch);
/** @brief XSplitterrefresh（对标 Qt 同名接口）。 */
void XSplitter_refresh(XSplitter* self);

/* ==================== 尺寸提示（对标 QSplitter::sizeHint/
 *             minimumSizeHint，qsplitter.cpp:1490-1541） ==================== */

/** @brief 分割器尺寸提示：Σ可见页 sizeHint 主轴 + 可见把手宽、横向取
 *         各页最大（隐藏/折叠页 skip；对标逐式计算）。XWidget vtable
 *         暂无 SizeHint 槽（missingPlan#4 登记待框架级落地），先行以
 *         直接查询函数暴露。 */
XSize XSplitter_sizeHint(const XSplitter* self);
/** @brief 分割器最小尺寸提示：Σ可见页 xsp_smartMin + 可见把手宽、横向
 *         取各页 minimumSizeHint 最大（对标同上）。 */
XSize XSplitter_minimumSizeHint(const XSplitter* self);

/* ==================== 尺寸与状态 ==================== */

/** @brief 查询各页当前尺寸（count 超页数完全不写、不足只写 count 个，
 *         对标 QSplitter::sizes(int*,int)）。 */
void XSplitter_sizes(const XSplitter* self, int* outSizes, int count);
/** @brief 设置各页尺寸（Qt 对齐：sizer=像素意图；sizer==0→可折叠页
 *         置 collapsed=1，不可折叠页钳 smartMin（smartMin=0 落未设定
 *         池，登记偏差）；sizer>0 清 collapsed。隐藏页空间回收、意图
 *         保留，show 后重设 sizes 或 refresh 恢复）。 */
void XSplitter_setSizes(XSplitter* self, const int* sizes, int count);
#if XByteArray_ON
/** @brief 序列化当前布局状态（对标 saveState；落方向/页数/把手宽/
 *         子页可折叠/逐页尺寸，qsplitter.cpp:1642-1679。不落 collapsed
 *         （Qt 同不落）与 opaqueResize/opaqueResizeSet 两字段（登记
 *         偏差）；collapsed 由 restore 按位重建）。 */
XByteArray* XSplitter_saveState(const XSplitter* self);
/** @brief 恢复序列化布局（对标 restoreState，qsplitter.cpp:1697-1734）。
 *         按实际页数钳位消费：逐实际页 sizer=(k<保存页数?保存值:0)
 *         （value() 默认 0）→0 走 collapsed/smartMin 分支；方向/
 *         handleWidth（有符号解析）/childrenCollapsible 无条件恢复；
 *         魔数/长度/格式校验不合法返回 false。 */
bool XSplitter_restoreState(XSplitter* self, const XByteArray* state);
#endif /* XByteArray_ON */

/* ==================== 信号 ==================== */

void* XSplitter_splitterMoved_signal(XSplitter* self, int pos, int index);

/** @brief 手柄点击信号（XGui 扩展，Qt 无对应）：空闲点击（按下释放、
 *         位移 <=2px）发 doubleClick=0；双击发 doubleClick=1。index=
 *         手柄索引（位于页 index 与 index+1 之间）。发射点在把手部件
 *         事件回调（XSplitterHandle.c）。典型用途：把手双击收起相邻
 *         页/空闲点击展开（导航面板细条把手交互）。 */
void* XSplitter_handleClicked_signal(XSplitter* self, int index,
                                     int doubleClick);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XFRAME_ON && XSPLITTER_ON */

#endif /* XSPLITTER_H */
