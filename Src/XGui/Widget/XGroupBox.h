/**
 * @file       XGroupBox.h
 * @brief      XGroupBox 分组框控件（对标 Qt 6.8 QGroupBox）。
 * @details    带可选标题的分组容器：1px 凹陷边框环绕三边，顶部为
 *             标题区（标题文本以 Base 底色遮挡边框线，左/中/右对
 *             齐）；子控件通常摆放在 contentsRect 区域内。flat 模式
 *             只在标题两侧绘制短边框线（对标 QGroupBox::setFlat）。
 *             checkable（可勾选分组）：标题左侧自绘小勾选框，点击标题
 *             区切换 checked 并发射 clicked(bool)/toggled(bool)；勾选
 *             关闭时递归禁用全部子控件、开启时恢复（对标 QGroupBox::
 *             isCheckable/isChecked/setChecked 与 _q_setChildrenEnabled）。
 * @note       模块总开关 XGROUPBOX_ON 定义于 XGuiConfig.h；
 *             XGROUPBOX_ON=0 时裁剪整个 XGroupBox 公共 API。
 *             依赖 XWIDGET_ON、XPALETTE_ON、XPAINTER_ON。
 *             对标 QGroupBox::initStyleOption 的样式选项结构体
 *             （QStyleOptionGroupBox）本项目未提供，标注后续扩展；
 *             Qt 仅勾选框区域响应点击，本实现第一版放宽为整个标题区
 *             （见 mousePressEvent 说明）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGROUPBOX_H
#define XGROUPBOX_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XPainter.h"
#include "XAlignment.h"

#if XWIDGET_ON && XGROUPBOX_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XGroupBox)
XCLASS_DEFINE_EXTEND_END(XGroupBox, XWidget)

/**
 * @brief      XGroupBox 分组框控件对象；m_base 必须是第一个成员
 *             （嵌 XWidget）。
 * @details    字段含义：
 *             - m_title：标题文本（NUL 结尾；空串表示无标题——无标
 *               题时退化为四边完整边框、无标题区）；
 *             - m_alignment：标题水平对齐（默认 Left，支持 Left/
 *               HCenter/Right）；
 *             - m_flat：扁平样式（true 只在标题两侧画短边框线，
 *               对标 QGroupBox::setFlat）；
 *             - m_checkable：是否可勾选（默认 false；true 时标题左侧
 *               绘制勾选框并允许点击切换，对标 isCheckable）；
 *             - m_checked：勾选状态（默认 false；仅 m_checkable 为
 *               true 时生效，对标 isChecked）。
 *             调用者不得手工修改字段；一律走公开 API。
 */
typedef struct XGroupBox
{
    XWidget m_base;                 /**< 基类成员；必须是第一个。 */
    char    m_title[64];            /**< 标题文本（NUL 结尾）。 */
    int     m_alignment;            /**< 标题水平对齐（XAlignment 组合）。 */
    bool    m_flat;                 /**< 扁平样式。 */
    bool    m_checkable;            /**< 是否可勾选。 */
    bool    m_checked;              /**< 勾选状态。 */
} XGroupBox;

/* ==================== 生命周期（对标 QGroupBox 构造/析构） ==================== */

/**
 * @brief      初始化 XGroupBox 类虚函数表并返回共享表指针。
 * @return     XGroupBox 类共享的虚函数表指针；初始化失败时返回 NULL。
 */
XVtable* XGroupBox_class_init(void);

/**
 * @brief      初始化 XGroupBox（对标 QGroupBox(parent) 构造）。
 * @details    先初始化 XWidget 基类，再挂 XGroupBox 虚表并设置默认
 *             值：标题为空、标题左对齐、非 flat。
 * @param      self   待初始化对象；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志（可传 0 表示 Widget 类型）。
 * @return     无返回值。
 */
void XGroupBox_init(XGroupBox* self, XWidget* parent, XWidgetFlags flags);

/** @brief 使用默认内存类型创建分组框（语义同 XWidget_create）。 */
#define XGroupBox_create(parent, flags) XGroupBox_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建分组框。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志。
 * @return     新对象指针；失败返回 NULL。
 */
XGroupBox* XGroupBox_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);

/** @brief 通过 XClass 虚表释放栈上/外部存储的 XGroupBox。 */
#define XGroupBox_deinit_base(self) XWidget_deinit_base((XWidget*)(self))
/** @brief 删除堆上的 XGroupBox 对象。 */
#define XGroupBox_delete_base(self) XWidget_delete_base((XWidget*)(self))

/* ==================== 父类 XWidget API 宏转发（对齐库内 XRadioButton 惯例） ==================== */

#define XGroupBox_setEnabled(self, enabled) XWidget_setEnabled((XWidget*)(self), (enabled))
#define XGroupBox_isEnabled(self) XWidget_isEnabled((const XWidget*)(self))
#define XGroupBox_setVisible(self, visible) XWidget_setVisible((XWidget*)(self), (visible))
#define XGroupBox_isVisible(self) XWidget_isVisible((const XWidget*)(self))
#define XGroupBox_show(self) XWidget_show((XWidget*)(self))
#define XGroupBox_hide(self) XWidget_hide((XWidget*)(self))
#define XGroupBox_raise(self) XWidget_raise((XWidget*)(self))
#define XGroupBox_lower(self) XWidget_lower((XWidget*)(self))
#define XGroupBox_setGeometry(self, x, y, w, h) XWidget_setGeometry((XWidget*)(self), (x), (y), (w), (h))
#define XGroupBox_setGeometryRect(self, rect) XWidget_setGeometryRect((XWidget*)(self), (rect))
#define XGroupBox_x(self) XWidget_x((const XWidget*)(self))
#define XGroupBox_y(self) XWidget_y((const XWidget*)(self))
#define XGroupBox_width(self) XWidget_width((const XWidget*)(self))
#define XGroupBox_height(self) XWidget_height((const XWidget*)(self))
#define XGroupBox_resize(self, w, h) XWidget_resize((XWidget*)(self), (w), (h))
#define XGroupBox_move(self, x, y) XWidget_move((XWidget*)(self), (x), (y))
#define XGroupBox_update(self) XWidget_update((XWidget*)(self))
#define XGroupBox_updateRect(self, rect) XWidget_updateRect((XWidget*)(self), (rect))
#define XGroupBox_setParent(self, parent, flags) XWidget_setParent((XWidget*)(self), (parent), (flags))
#define XGroupBox_parentWidget(self) XWidget_parentWidget((const XWidget*)(self))
#define XGroupBox_setFocus(self) XWidget_setFocus((XWidget*)(self))
#define XGroupBox_clearFocus(self) XWidget_clearFocus((XWidget*)(self))
#define XGroupBox_hasFocus(self) XWidget_hasFocus((const XWidget*)(self))
#define XGroupBox_setFocusPolicy(self, policy) XWidget_setFocusPolicy((XWidget*)(self), (policy))
#define XGroupBox_setAttribute(self, attribute, on) XWidget_setAttribute((XWidget*)(self), (attribute), (on))
#define XGroupBox_testAttribute(self, attribute) XWidget_testAttribute((const XWidget*)(self), (attribute))
#define XGroupBox_setContentsMargins(self, l, t, r, b) XWidget_setContentsMargins((XWidget*)(self), (l), (t), (r), (b))
#define XGroupBox_contentsMargins(self) XWidget_contentsMargins((const XWidget*)(self))
#define XGroupBox_setWindowFlags(self, flags) XWidget_setWindowFlags((XWidget*)(self), (flags))
#define XGroupBox_updateGeometry(self) XWidget_updateGeometry((XWidget*)(self))


/* ==================== 标题与样式（对标 QGroupBox public API） ==================== */

/**
 * @brief      查询标题文本（内部缓冲借用指针）。
 * @param      self 分组框对象；可为 NULL。
 * @return     标题借用指针（空串表示无标题）；self 为 NULL 时返回空串。
 */
const char* XGroupBox_title(const XGroupBox* self);

/**
 * @brief      设置标题文本并重绘（对标 QGroupBox::setTitle）。
 * @details    超长截断到内部缓冲上限（63 字符 + NUL）；空串/NULL 表
 *             示无标题。
 * @param      self  分组框对象；可为 NULL。
 * @param      title 标题文本（UTF-8，NUL 结尾）。
 * @return     无返回值。
 */
void XGroupBox_setTitle(XGroupBox* self, const char* title);

/**
 * @brief      查询标题水平对齐（XAlignment 组合）。
 * @param      self 分组框对象；可为 NULL。
 * @return     当前对齐；self 为 NULL 时返回 Left。
 */
int XGroupBox_alignment(const XGroupBox* self);

/**
 * @brief      设置标题水平对齐并重绘（对标 setAlignment）。
 * @details    仅水平分量（Left/HCenter/Right）生效。
 * @param      self      分组框对象；可为 NULL。
 * @param      alignment XAlignment 组合。
 * @return     无返回值。
 */
void XGroupBox_setAlignment(XGroupBox* self, int alignment);

/**
 * @brief      查询扁平样式（对标 QGroupBox::isFlat）。
 * @param      self 分组框对象；可为 NULL。
 * @return     true 表示扁平样式；self 为 NULL 时返回 false。
 */
bool XGroupBox_isFlat(const XGroupBox* self);

/**
 * @brief      设置扁平样式并重绘（对标 QGroupBox::setFlat）。
 * @param      self 分组框对象；可为 NULL。
 * @param      flat true 只在标题两侧画短边框线。
 * @return     无返回值。
 */
void XGroupBox_setFlat(XGroupBox* self, bool flat);

/**
 * @brief      查询内容区矩形（去除标题区与边框后的子控件可用区域）。
 * @details    有标题时 contentsRect 从标题高度下方开始；无标题时从
 *             边框内侧开始（对标 QGroupBox::contentsRect）。
 * @param      self 分组框对象；可为 NULL。
 * @return     内容区矩形（控件本地坐标）；self 为 NULL 时返回零矩形。
 */
XRect XGroupBox_contentsRect(const XGroupBox* self);

/* ==================== 勾选状态（对标 QGroupBox checkable/checked） ==================== */

/**
 * @brief      查询分组框是否可勾选（对标 QGroupBox::isCheckable）。
 * @param      self 分组框对象；可为 NULL。
 * @return     true 表示可勾选；self 为 NULL 时返回 false。
 */
bool XGroupBox_isCheckable(const XGroupBox* self);

/**
 * @brief      设置分组框是否可勾选并重绘（对标 setCheckable）。
 * @details    true 时标题左侧绘制小勾选框，点击标题区切换勾选状态；
 *             false 时取消勾选框（对标 Qt：不清除已设置的 checked
 *             状态，也不发射 toggled）。
 * @param      self      分组框对象；可为 NULL。
 * @param      checkable true 允许勾选，false 禁止勾选。
 * @return     无返回值。
 */
void XGroupBox_setCheckable(XGroupBox* self, bool checkable);

/**
 * @brief      查询分组框勾选状态（对标 QGroupBox::isChecked）。
 * @param      self 分组框对象；可为 NULL。
 * @return     true 表示已勾选；self 为 NULL 时返回 false。
 */
bool XGroupBox_isChecked(const XGroupBox* self);

/**
 * @brief      设置分组框勾选状态（对标 QGroupBox::setChecked 槽）。
 * @details    仅可勾选分组接受状态变更；状态真正变化时发射
 *             toggled(bool) 并重绘，同时把子控件可用性同步为勾选
 *             状态（未勾选递归禁用全部子控件，勾选恢复）。
 * @param      self    分组框对象；可为 NULL。
 * @param      checked 目标勾选状态。
 * @return     无返回值；分组框不可勾选或状态未变化时保持原状态。
 */
void XGroupBox_setChecked(XGroupBox* self, bool checked);

/* ==================== 信号（对标 QGroupBox signals） ==================== */

/**
 * @brief      发射 clicked(bool) 信号（对标 QGroupBox::clicked）。
 * @details    self 非 NULL 时把切换后的勾选状态作为参数同步通知已连接
 *             槽；self 为 NULL 时只返回信号标识，不发射任何通知。
 *             第一版在标题区鼠标按下完成切换后立即发射（Qt 为按下+
 *             释放完整点击语义，后续扩展）。
 * @param      self    发射信号的分组框对象；可为 NULL。
 * @param      checked 点击完成后的勾选状态。
 * @return     不透明的 clicked 信号标识；返回值不指向可释放对象，也
 *             不得解引用。
 */
void* XGroupBox_clicked_signal(XGroupBox* self, bool checked);

/**
 * @brief      发射 toggled(bool) 信号（对标 QGroupBox::toggled）。
 * @details    self 非 NULL 时把新勾选状态同步通知已连接槽；self 为
 *             NULL 时只返回信号标识，不发射任何通知。
 * @param      self    发射信号的分组框对象；可为 NULL。
 * @param      checked 已提交的勾选状态。
 * @return     不透明的 toggled 信号标识；返回值不指向可释放对象，也
 *             不得解引用。
 */
void* XGroupBox_toggled_signal(XGroupBox* self, bool checked);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XGROUPBOX_ON */

#ifdef __cplusplus
}
#endif
#endif /* XGROUPBOX_H */
