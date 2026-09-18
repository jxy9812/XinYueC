/**
 * @file       XSplitter.h
 * @brief      XSplitter 分割器控件（对标 Qt 6.8 QSplitter 全部公共 API）。
 * @details    功能范围：
 *             - 页面管理：addWidget/insertWidget/widget/count/indexOf；
 *             - 方向：Horizontal（默认）/Vertical；
 *             - 尺寸：setSizes/sizes、setStretchFactor、handleWidth；
 *             - 折叠：setChildrenCollapsible（默认 true）、
 *               setCollapsible/isCollapsible（逐页覆写）；
 *             - opaqueResize（默认 true）；refresh 重算布局；
 *             - 状态序列化：saveState/restoreState（XByteArray 承载，
 *               对标 QByteArray 版本化状态）；getRange 查询拖动范围；
 *             - 交互：分隔条拖动（水平左右/垂直上下），拖动结束发射
 *               splitterMoved(pos, index)；
 *             - 子控件所有权：addWidget 后归分割器（Qt 语义）。
 * @note       模块总开关 XSPLITTER_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。依赖 XWIDGET_ON、XFRAME_ON、XBYTEARRAY_ON。
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

XCLASS_DEFINE_BEGING(XSplitter)
XCLASS_DEFINE_EXTEND_END(XSplitter, XFrame)

/**
 * @brief      XSplitter 控件对象；m_base 必须是第一个成员。
 */
typedef struct XSplitter
{
    XFrame m_base;              /**< 基类成员；必须是第一个。 */
    int m_orientation;          /**< 方向（1=水平 2=垂直，默认水平）。 */
    int m_handleWidth;          /**< 分隔条宽度（默认 5）。 */
    bool m_childrenCollapsible; /**< 子页可折叠（默认 true）。 */
    bool m_opaqueResize;        /**< 不透明拖动（默认 true）。 */
    int* m_collapsible;         /**< 逐页折叠覆写（-1 未设置）。 */
    int m_collapsibleCap;       /**< 折叠数组容量。 */
    int m_dragIndex;            /**< 正在拖动的分隔条索引（-1 无）。 */
} XSplitter;

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
void XSplitter_init_2(XSplitter* self, int orientation, XWidget* parent,
                      XWidgetFlags flags);
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
XSplitter* XSplitter_create_ex_2(XMemoryType memory, int orientation,
                                 XWidget* parent, XWidgetFlags flags);
#define XSplitter_deinit_base(self) XFrame_deinit_base((XFrame*)(self))
#define XSplitter_delete_base(self) XClass_delete_base((XClass*)(self))

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
/** @brief XSplittercount（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XSplitter_count(const XSplitter* self);
/** @brief XSplitterindexOf（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param widget 子控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XSplitter_indexOf(const XSplitter* self, const XWidget* widget);
/** @brief 替换既有子控件（对标 QSplitter::replaceWidget）。
 * @details 所有权语义（对标 Qt）：新控件经 reparent 挂为分割器子控
 *          件（与 addWidget 一致，归分割器父子链管理）；被替换的旧
 *          控件解除父子关系交还调用方（不销毁，对标 Qt 的
 *          setParent(nullptr)），并被隐藏。新控件继承旧控件的几何
 *          与可见状态，随后重算布局。
 * @param self 目标控件；传入 NULL 时返回 NULL。
 * @param index 页序号（0 起；越界返回 NULL，不发生替换）。
 * @param widget 新控件指针；为 NULL、与被替换控件相同、或已是本分
 *        割器子控件时返回 NULL（Qt 同护栏语义），不发生替换。
 * @return 被替换的旧控件指针（调用方接管）；失败时返回 NULL。
 */
XWidget* XSplitter_replaceWidget(XSplitter* self, int index,
                                 XWidget* widget);
/** @brief 查询分隔点几何矩形（对标 QSplitter::handle）。
 * @details 分隔点 index 位于页 index 与页 index+1 之间（0 起，有效
 *          范围 0..count-2，对标“handle 在页 index 右/下方”语义）。
 * @note    内部无把手部件对象（Qt 为 QSplitterHandle*），故以几何
 *          矩形承载：把手矩形取自页 index 几何之后、宽/高为
 *          handleWidth 的条带；把手拖动事件路径为预留项。
 * @param self 目标控件；传入 NULL 时返回 false。
 * @param index 分隔点序号（0 起；越界返回 false）。
 * @param out 输出把手矩形（本控件局部坐标）；可为 NULL（仅校验）。
 * @return 成功返回 true；参数无效或该分隔点不存在返回 false。
 */
bool XSplitter_handle(const XSplitter* self, int index, XRect* out);
/** @brief 查询分隔点拖动范围（对标 QSplitter::getRange）。
 * @details 分隔点 index 介于页 index 与页 index+1 之间（有效范围
 *          0..count-2）。min/max 为把手位置可达区间，以页内容坐标
 *          （0..可用总长）表达（Qt 以 contentsRect 绝对坐标表达，
 *          项目简化为 0 基相对坐标）；可折叠页最小贡献 0，不可折叠
 *          页以当前尺寸作为最小值代理（项目无 minimumSizeHint 承
 *          载，Qt 以 qSmartMinSize 计算）。
 * @param self 目标控件；传入 NULL 时返回 false。
 * @param index 分隔点序号（0 起；越界或页数不足 2 返回 false）。
 * @param min 输出最小位置；可为 NULL（忽略）。
 * @param max 输出最大位置；可为 NULL（忽略）。
 * @return 成功返回 true；参数无效返回 false。
 */
bool XSplitter_getRange(const XSplitter* self, int index, int* min, int* max);

/* ==================== 属性 ==================== */

void XSplitter_setOrientation(XSplitter* self, int orientation);
/** @brief XSplitterorientation（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 返回对应数值；无效时返回 0 或 -1（视接口语义）。
 */
int XSplitter_orientation(const XSplitter* self);
/** @brief XSplittersetChildren可折叠（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param collapsible bool 参数。
 * @return 无返回值。
 */
void XSplitter_setChildrenCollapsible(XSplitter* self, bool collapsible);
/** @brief XSplitterchildren可折叠（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 条件成立返回 true，否则返回 false。
 */
bool XSplitter_childrenCollapsible(const XSplitter* self);
/**
 * @brief      设置可折叠。
 */
void XSplitter_setCollapsible(XSplitter* self, int index, bool collapsible);
/**
 * @brief      获取可折叠。
 */
bool XSplitter_isCollapsible(const XSplitter* self, int index);
/**
 * @brief      设置不透明调整。
 */
void XSplitter_setOpaqueResize(XSplitter* self, bool opaque);
/**
 * @brief      获取不透明调整。
 */
bool XSplitter_opaqueResize(const XSplitter* self);
/**
 * @brief      获取把手宽度。
 */
int XSplitter_handleWidth(const XSplitter* self);
/**
 * @brief      设置把手宽度。
 */
void XSplitter_setHandleWidth(XSplitter* self, int width);
/** @brief XSplitterset拉伸Factor（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @param index 索引（0 起）。
 * @param stretch 拉伸因子。
 * @return 无返回值。
 */
void XSplitter_setStretchFactor(XSplitter* self, int index, int stretch);
/** @brief XSplitterrefresh（对标 Qt 同名接口）。
 * @param self 目标控件指针。
 * @return 无返回值。
 */
void XSplitter_refresh(XSplitter* self);

/* ==================== 尺寸与状态 ==================== */

/** @brief 查询各页当前尺寸（写入调用方数组；对标 sizes()）。 */
/**
 * @brief      获取子控件尺寸。
 */
void XSplitter_sizes(const XSplitter* self, int* outSizes, int count);
/** @brief 设置各页尺寸（按比例归一化；对标 setSizes）。 */
/**
 * @brief      设置子控件尺寸。
 */
void XSplitter_setSizes(XSplitter* self, const int* sizes, int count);
#if XByteArray_ON
/** @brief 序列化当前布局状态（对标 saveState）。 */
/**
 * @brief      保存状态。
 */
XByteArray* XSplitter_saveState(const XSplitter* self);
/** @brief 恢复序列化布局（对标 restoreState；成功返回 true）。 */
/**
 * @brief      恢复分割器状态。
 */
bool XSplitter_restoreState(XSplitter* self, const XByteArray* state);
#endif /* XByteArray_ON */

/* ==================== 信号 ==================== */

void* XSplitter_splitterMoved_signal(XSplitter* self, int pos, int index);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XFRAME_ON && XSPLITTER_ON */

#endif /* XSPLITTER_H */
