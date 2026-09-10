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
int XSplitter_count(const XSplitter* self);
int XSplitter_indexOf(const XSplitter* self, const XWidget* widget);

/* ==================== 属性 ==================== */

void XSplitter_setOrientation(XSplitter* self, int orientation);
int XSplitter_orientation(const XSplitter* self);
void XSplitter_setChildrenCollapsible(XSplitter* self, bool collapsible);
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
void XSplitter_setStretchFactor(XSplitter* self, int index, int stretch);
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

#ifdef __cplusplus
}
#endif
#endif /* XSPLITTER_H */
