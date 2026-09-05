/**
 * @file       XAbstractButton.h
 * @brief      XAbstractButton 抽象按钮基类（对标 Qt 6.8 QAbstractButton）。
 * @details    XAbstractButton 继承 XWidget，集中提供 QPushButton、QCheckBox、
 *             QRadioButton 等按钮控件共有的状态管理、激活行为和信号：
 *             - 文本与图标：text/setText、icon/setIcon、iconSize/setIconSize；
 *             - 状态属性：checkable/checked、down、autoRepeat、
 *               autoExclusive；
 *             - 激活行为：click、animateClick，以及鼠标/键盘按下与释放期间
 *               的状态和信号联动；
 *             - 保护虚函数：checkStateSet、nextCheckState、hitButton、以及
 *               内容变更钩子 contentChanged（供具体按钮刷新尺寸提示），
 *               声明见 XAbstractButton_Protected.h；
 *             - 信号：pressed、released、clicked(bool)、toggled(bool)。
 *             具体按钮子类负责安装自己的绘制事件和特有属性；本类不直接
 *             依赖 Win32、POSIX、Qt 或其他平台 API，平台事件由 XWidget
 *             事件体系转发。
 * @note       模块总开关 XABSTRACTBUTTON_ON 定义于 XGuiConfig.h；关闭时
 *             裁剪本头文件中的全部公共声明。XAbstractButton 依赖
 *             XWIDGET_ON 与 XSTRING_ON；XIcon 关闭或为空时，图标接口仍
 *             使用空 XIcon 作为回退值。对象字段由 XClass 生命周期管理，
 *             不保证线程安全，调用方应在所属 GUI 线程访问。
 * @author     XinYueC 团队
 */
#ifndef XABSTRACTBUTTON_H
#define XABSTRACTBUTTON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "XGuiConfig.h"
#include "XWidget.h"
#include "XString.h"
#include "XIcon.h"
#include "XGeometry.h"

#if XWIDGET_ON && XABSTRACTBUTTON_ON

/* ==================== 虚函数表（对标 Qt 6.8 QAbstractButton protected API） ==================== */

/**
 * @brief      XAbstractButton 虚函数表枚举。
 * @details    继承 XWidget 的全部事件槽位，并追加 QAbstractButton 的
 *             checkStateSet、nextCheckState、hitButton 保护槽位，以及
 *             本实现特有的 contentChanged 内容变更槽位（供具体按钮在
 *             文本/图标等内容变化后刷新自身尺寸提示或外观缓存；Qt 中
 *             没有同名机制，QWidget::updateGeometry 承担该职责）。
 *             槽位数值由 XClass 宏管理，具体子类只能在自身虚表中覆盖
 *             或追加。
 */
XCLASS_DEFINE_BEGING(XAbstractButton)
XCLASS_DEFINE_ENUM(XAbstractButton, CheckStateSet) = XCLASS_VTABLE_GET_SIZE(XWidget), /**< 选中状态设置后的保护槽位。 */
XCLASS_DEFINE_ENUM(XAbstractButton, NextCheckState),                                  /**< 计算下一个选中状态的保护槽位。 */
XCLASS_DEFINE_ENUM(XAbstractButton, HitButton),                                       /**< 局部坐标命中测试的保护槽位。 */
XCLASS_DEFINE_ENUM(XAbstractButton, ContentChanged),                                  /**< 文本/图标等内容变更后的保护槽位。 */
XCLASS_DEFINE_END(XAbstractButton)

/* ==================== 对象前向声明 ==================== */

/**
 * @brief      XAbstractButton 按钮对象前向声明。
 * @details    完整对象定义位于本文件后文；具体按钮子类必须把
 *             XAbstractButton 作为第一个成员以保持继承布局。
 */
typedef struct XAbstractButton XAbstractButton;

/* ==================== 控件对象（对标 Qt 6.8 QAbstractButton） ==================== */

/**
 * @brief      XAbstractButton 按钮控件对象。
 * @details    m_base 是第一个成员，因此该对象可向上转换为 XWidget 和
 *             XObject；m_text 由对象拥有，m_icon 使用 XIcon 值语义，
 *             定时器字段由按钮内部维护。所有成员均属于实现状态，调用方
 *             不得直接修改，应通过本文件声明的 API 访问。
 */
struct XAbstractButton
{
    XWidget      m_base;               /**< 基类成员；必须是第一个，由 XClass 管理，禁止手工修改。 */
    XString*     m_text;               /**< 按钮文本；对象拥有，按 XString 的 UTF-16 代码单元保存。 */
    XIcon        m_icon;               /**< 按钮图标；值类型并按 XIcon 的隐式共享规则管理。 */
    XSize        m_iconSize;           /**< 显式图标尺寸，单位为像素；宽高均为 0 表示使用默认尺寸。 */
    bool         m_checkable;          /**< 是否允许选中；false 时 setChecked/toggle 不改变选中状态。 */
    bool         m_checked;            /**< 当前选中状态；只有 m_checkable 为 true 时具有按钮语义。 */
    bool         m_down;               /**< 当前视觉按下状态；由程序化调用或输入事件维护。 */
    bool         m_pressed;            /**< 鼠标/键盘按压跟踪状态；仅供内部事件处理使用。 */
    bool         m_autoRepeat;         /**< 是否允许按住后自动重复点击。 */
    bool         m_autoExclusive;      /**< 是否与同一父控件下的可选中按钮自动互斥。 */
    int          m_autoRepeatDelay;    /**< 首次自动重复前的延迟，单位为毫秒；由调用方整数保存。 */
    int          m_autoRepeatInterval; /**< 自动重复间隔，单位为毫秒；由调用方整数保存。 */
    XTimerId     m_repeatTimer;        /**< 自动重复定时器；无效时为 XTIMER_INVALID_ID，仅供内部使用。 */
    XTimerId     m_animateTimer;       /**< animateClick 释放定时器；无效时为 XTIMER_INVALID_ID，仅供内部使用。 */
};

/* ==================== 类初始化与生命周期 ==================== */

/**
 * @brief      初始化并返回 XAbstractButton 类的共享虚函数表。
 * @return     类共享的 XVtable 指针；虚表创建或注册失败时返回 NULL。
 * @note       返回指针具有静态生命周期，调用方不得释放、修改或保存为
 *             可写的派生虚表。
 */
XVtable* XAbstractButton_class_init(void);

/**
 * @brief      初始化嵌入式 XAbstractButton 对象（对标 QAbstractButton 构造）。
 * @details    初始化 XWidget 基类、建立父控件借用关系并设置按钮默认值：
 *             文本为空、图标为空、checkable/checked/down/pressed=false、
 *             autoRepeat/autoExclusive=false、重复延迟 300 毫秒、重复间隔
 *             100 毫秒。具体子类完成自身字段初始化后必须安装自己的虚表。
 * @param      self 待初始化的可写对象存储；不可为 NULL，且必须尚未初始化。
 * @param      parent 父控件借用指针；可为 NULL，按钮不取得其所有权。
 * @param      flags 窗口标志；可传 0 表示普通 Widget 类型。
 * @return     无返回值；self 不满足初始化前提时调用方不得继续使用对象。
 * @note       init 不分配 self，也不负责释放 parent；初始化后的对象应使用
 *             XAbstractButton_deinit_base，堆对象应使用
 *             XAbstractButton_delete_base。
 */
void XAbstractButton_init(XAbstractButton* self, XWidget* parent,
                          XWidgetFlags flags);

/**
 * @brief      使用默认内存类型创建并初始化按钮对象。
 * @param      parent 父控件借用指针；可为 NULL，宏不取得其所有权。
 * @param      flags 窗口标志；可传 0 表示普通 Widget 类型。
 * @return     新建的已初始化对象指针；分配失败返回 NULL。成功返回的对象
 *             由调用方拥有，必须使用 XAbstractButton_delete_base 释放。
 */
#define XAbstractButton_create(parent, flags) \
    XAbstractButton_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))

/**
 * @brief      使用指定内存类型创建并初始化按钮对象。
 * @param      memory 对象分配所使用的 XMemoryType；只影响对象分配与释放。
 * @param      parent 父控件借用指针；可为 NULL，函数不取得其所有权。
 * @param      flags 窗口标志；可传 0 表示普通 Widget 类型。
 * @return     新建的已初始化对象指针；分配或初始化失败返回 NULL。成功
 *             返回的堆对象由调用方拥有，必须使用
 *             XAbstractButton_delete_base 释放。
 */
XAbstractButton* XAbstractButton_create_ex(XMemoryType memory,
                                           XWidget* parent,
                                           XWidgetFlags flags);

/**
 * @brief      通过当前 XClass 虚表释放按钮对象所拥有的资源。
 * @param      self 已初始化的栈对象或外部存储对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值；函数不会释放 self 指向的存储空间，堆对象必须使用
 *             XAbstractButton_delete_base。
 * @note       调用后 self 不再是可用的已初始化对象；不得重复调用，也不得
 *             在未初始化存储上调用。
 */
#define XAbstractButton_deinit_base(self) \
    XClass_deinit_base((XClass*)(self))

/**
 * @brief      释放按钮对象资源并按对象所有权删除其存储空间。
 * @param      self 由 XAbstractButton_create 或 XAbstractButton_create_ex
 *             返回的堆对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值；堆对象会先执行虚表析构，再由创建时的内存方法释放；
 *             栈对象不应使用此宏，栈对象请使用 XAbstractButton_deinit_base。
 */
#define XAbstractButton_delete_base(self) \
    XClass_delete_base((XClass*)(self))

/* ==================== 文本与图标（对标 QAbstractButton） ==================== */

/**
 * @brief      查询按钮文本（对标 QAbstractButton::text）。
 * @param      self 按钮对象的借用指针；可为 NULL。
 * @return     对象内部文本的借用指针；self 为 NULL、未初始化或文本存储
 *             不可用时返回 NULL。返回指针不能释放、不能修改，生命周期
 *             与 self 及下一次文本修改前的对象状态相关。
 */
const XString* XAbstractButton_text(const XAbstractButton* self);

/**
 * @brief      设置按钮文本（对标 QAbstractButton::setText）。
 * @details    函数深拷贝 text，不取得调用方字符串所有权；新文本生效后
 *             刷新尺寸提示、几何和绘制。XString 内容按 UTF-16 代码单元
 *             处理，传入 NULL 等价于空字符串。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      text 源文本借用指针；可为 NULL 表示空字符串，函数返回后
 *             调用方仍可释放或修改源对象。
 * @return     无返回值；内存分配失败或 self 为 NULL 时保持原按钮文本和
 *             其他状态不变。
 */
void XAbstractButton_setText(XAbstractButton* self, const XString* text);

/**
 * @brief      使用 UTF-8 字符串设置按钮文本。
 * @details    utf8 按 UTF-8 解码为 XString；函数复制解码结果，返回后不
 *             保留调用方字符缓冲区。NULL 按空字符串处理。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      utf8 以 '\0' 结尾的 UTF-8 字符串；可为 NULL 表示空字符串，
 *             由调用方借用且函数不会修改。
 * @return     无返回值；解码或内存分配失败时保持原文本不变。
 */
void XAbstractButton_setText_2(XAbstractButton* self, const char* utf8);

/**
 * @brief      返回按钮图标副本（对标 QAbstractButton::icon）。
 * @param      self 按钮对象的借用指针；可为 NULL。
 * @return     按 XIcon 值语义返回的图标副本；self 为 NULL 或无图标时返回
 *             已初始化的空 XIcon。返回值由调用方拥有，使用完成后必须
 *             调用 XIcon_deinit_base 释放其内部资源。
 */
XIcon XAbstractButton_icon(const XAbstractButton* self);

/**
 * @brief      设置按钮图标（对标 QAbstractButton::setIcon）。
 * @details    复制 icon 的 XIcon 值，不转移调用方所有权；设置后刷新尺寸
 *             提示、几何和绘制。NULL 或空图标清除当前图标。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      icon 源图标借用指针；可为 NULL，函数不会修改或释放源图标。
 * @return     无返回值；源图标无效或复制失败时保持原图标和相关状态不变。
 */
void XAbstractButton_setIcon(XAbstractButton* self, const XIcon* icon);

/**
 * @brief      查询按钮显式图标尺寸（对标 QAbstractButton::iconSize）。
 * @param      self 按钮对象的借用指针；可为 NULL。
 * @return     显式图标尺寸，单位为像素；(0, 0) 表示未设置并使用默认
 *             尺寸，self 为 NULL 时也返回 (0, 0)。
 */
XSize XAbstractButton_iconSize(const XAbstractButton* self);

/**
 * @brief      设置按钮显式图标尺寸（对标 QAbstractButton::setIconSize）。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      size 目标尺寸借用指针；可为 NULL，或宽高任一不大于 0，
 *             此时清除显式尺寸并恢复默认值。
 * @return     无返回值；尺寸生效后刷新尺寸提示、几何和绘制。
 */
void XAbstractButton_setIconSize(XAbstractButton* self, const XSize* size);

/* ==================== 选中状态（对标 QAbstractButton） ==================== */

/**
 * @brief      查询按钮是否可选中（对标 QAbstractButton::isCheckable）。
 * @param      self 按钮对象的借用指针；可为 NULL。
 * @return     按钮可选中返回 true；self 为 NULL 或不可选中返回 false。
 */
bool XAbstractButton_isCheckable(const XAbstractButton* self);

/**
 * @brief      设置按钮是否可选中（对标 QAbstractButton::setCheckable）。
 * @details    从可选中切换为不可选中时清除 checked 状态，但按 Qt 语义不
 *             发射 toggled 信号；重新启用可选中不会恢复之前状态。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      checkable true 表示允许选中，false 表示禁止选中。
 * @return     无返回值；self 为 NULL 时保持所有对象状态不变。
 */
void XAbstractButton_setCheckable(XAbstractButton* self, bool checkable);

/**
 * @brief      查询按钮当前是否选中（对标 QAbstractButton::isChecked）。
 * @param      self 按钮对象的借用指针；可为 NULL。
 * @return     当前 checked 状态；self 为 NULL 或未初始化时返回 false。
 */
bool XAbstractButton_isChecked(const XAbstractButton* self);

/**
 * @brief      设置按钮选中状态（对标 QAbstractButton::setChecked）。
 * @details    只有可选中按钮接受状态变更；状态真正变化时发射 toggled
 *             信号，并按 autoExclusive 规则取消同组其他按钮的选中状态。
 *             自动互斥组中的唯一选中按钮不能被直接取消。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      checked 目标选中状态。
 * @return     无返回值；按钮不可选中、状态未变化或违反唯一选中约束时，
 *             原状态保持不变且不重复发射信号。
 */
void XAbstractButton_setChecked(XAbstractButton* self, bool checked);

/**
 * @brief      反转按钮选中状态（对标 QAbstractButton::toggle）。
 * @param      self 待操作的按钮对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值；不可选中按钮不改变状态；状态变化时等价于调用
 *             XAbstractButton_setChecked 的反向值并发射 toggled。
 */
void XAbstractButton_toggle(XAbstractButton* self);

/* ==================== 按下状态、自动重复与互斥 ==================== */

/**
 * @brief      查询按钮当前是否处于按下状态（对标 QAbstractButton::isDown）。
 * @param      self 按钮对象的借用指针；可为 NULL。
 * @return     当前 down 状态；self 为 NULL 或未初始化时返回 false。
 */
bool XAbstractButton_isDown(const XAbstractButton* self);

/**
 * @brief      设置按钮按下状态（对标 QAbstractButton::setDown）。
 * @details    该接口只改变视觉/内部 down 状态并刷新绘制，不直接发射
 *             pressed、released 或 clicked 信号；进入按下状态且启用
 *             autoRepeat 时启动自动重复定时器。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      down true 表示按下，false 表示释放。
 * @return     无返回值；self 为 NULL 时保持对象状态不变。
 */
void XAbstractButton_setDown(XAbstractButton* self, bool down);

/**
 * @brief      查询是否允许按住按钮自动重复（对标 QAbstractButton::autoRepeat）。
 * @param      self 按钮对象的借用指针；可为 NULL。
 * @return     已启用自动重复返回 true；self 为 NULL 时返回 false。
 */
bool XAbstractButton_autoRepeat(const XAbstractButton* self);

/**
 * @brief      设置是否允许按住按钮自动重复（对标 QAbstractButton::setAutoRepeat）。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      repeat true 启用自动重复，false 停止自动重复。
 * @return     无返回值；关闭自动重复时会停止当前重复定时器。
 */
void XAbstractButton_setAutoRepeat(XAbstractButton* self, bool repeat);

/**
 * @brief      查询自动重复开始延迟（对标 QAbstractButton::autoRepeatDelay）。
 * @param      self 按钮对象的借用指针；可为 NULL。
 * @return     首次重复前的延迟，单位为毫秒；self 为 NULL 时返回 0。
 */
int XAbstractButton_autoRepeatDelay(const XAbstractButton* self);

/**
 * @brief      设置自动重复开始延迟（对标 QAbstractButton::setAutoRepeatDelay）。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      delay 延迟毫秒数；调用方整数原样保存，实际注册定时器时
 *             非正值按调度器要求降级。
 * @return     无返回值；self 为 NULL 时保持原延迟不变。
 */
void XAbstractButton_setAutoRepeatDelay(XAbstractButton* self, int delay);

/**
 * @brief      查询自动重复间隔（对标 QAbstractButton::autoRepeatInterval）。
 * @param      self 按钮对象的借用指针；可为 NULL。
 * @return     重复间隔，单位为毫秒；self 为 NULL 时返回 0。
 */
int XAbstractButton_autoRepeatInterval(const XAbstractButton* self);

/**
 * @brief      设置自动重复间隔（对标 QAbstractButton::setAutoRepeatInterval）。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      interval 重复间隔毫秒数；调用方整数原样保存，实际注册
 *             定时器时非正值按调度器要求降级。
 * @return     无返回值；self 为 NULL 时保持原间隔不变。
 */
void XAbstractButton_setAutoRepeatInterval(XAbstractButton* self, int interval);

/**
 * @brief      查询自动互斥标志（对标 QAbstractButton::autoExclusive）。
 * @param      self 按钮对象的借用指针；可为 NULL。
 * @return     已启用自动互斥返回 true；self 为 NULL 时返回 false。
 */
bool XAbstractButton_autoExclusive(const XAbstractButton* self);

/**
 * @brief      设置自动互斥标志（对标 QAbstractButton::setAutoExclusive）。
 * @details    启用后，可选中按钮在同一父控件范围内与已登记的同类按钮
 *             联动；按钮不取得兄弟控件的所有权。
 * @param      self 待修改的按钮对象；可为 NULL，NULL 时不执行操作。
 * @param      exclusive true 启用自动互斥，false 禁用自动互斥。
 * @return     无返回值；self 为 NULL 时保持原标志不变。
 */
void XAbstractButton_setAutoExclusive(XAbstractButton* self, bool exclusive);

/* ==================== 程序化点击（对标 QAbstractButton） ==================== */

/**
 * @brief      程序化执行一次按钮点击（对标 QAbstractButton::click）。
 * @details    按钮启用时按 pressed、down、nextCheckState、released、
 *             clicked 的顺序执行，并在选中状态变化时发射 toggled；禁用
 *             或 NULL 按钮不执行任何操作。
 * @param      self 待点击的按钮对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值；点击过程中出现状态限制时保留已经提交的状态，
 *             不提供回滚机制。
 */
void XAbstractButton_click(XAbstractButton* self);

/**
 * @brief      以短暂按下状态执行动画点击（对标 QAbstractButton::animateClick）。
 * @details    启用按钮会立即进入 down 状态并发射一次 pressed，随后由事件
 *             循环在约 100 毫秒后完成释放、状态切换和 clicked；重复调用
 *             会重置释放定时器，不会重复发射 pressed。
 * @param      self 待点击的按钮对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值；按钮禁用或定时器无法创建时不启动动画。
 */
void XAbstractButton_animateClick(XAbstractButton* self);

/* ==================== 信号（对标 QAbstractButton signals） ==================== */

/**
 * @brief      发射 pressed 信号（对标 QAbstractButton::pressed）。
 * @details    self 非 NULL 时同步通知已连接槽；self 为 NULL 时只返回信号
 *             标识，便于连接信号与槽，不发射任何通知。
 * @param      self 发射信号的按钮对象；可为 NULL。
 * @return     不透明的 pressed 信号标识；返回值不指向可释放对象，也不得
 *             解引用。
 */
void* XAbstractButton_pressed_signal(XAbstractButton* self);

/**
 * @brief      发射 released 信号（对标 QAbstractButton::released）。
 * @details    self 非 NULL 时同步通知已连接槽；self 为 NULL 时只返回信号
 *             标识，不发射任何通知。
 * @param      self 发射信号的按钮对象；可为 NULL。
 * @return     不透明的 released 信号标识；返回值不指向可释放对象，也不得
 *             解引用。
 */
void* XAbstractButton_released_signal(XAbstractButton* self);

/**
 * @brief      发射 clicked(bool) 信号（对标 QAbstractButton::clicked）。
 * @details    self 非 NULL 时把 checked 作为信号参数同步通知已连接槽；
 *             self 为 NULL 时只返回信号标识，不发射任何通知。
 * @param      self 发射信号的按钮对象；可为 NULL。
 * @param      checked 点击完成后的选中状态。
 * @return     不透明的 clicked 信号标识；返回值不指向可释放对象，也不得
 *             解引用。
 */
void* XAbstractButton_clicked_signal(XAbstractButton* self, bool checked);

/**
 * @brief      发射 toggled(bool) 信号（对标 QAbstractButton::toggled）。
 * @details    self 非 NULL 时把 checked 作为新的选中状态同步通知已连接槽；
 *             self 为 NULL 时只返回信号标识，不发射任何通知。
 * @param      self 发射信号的按钮对象；可为 NULL。
 * @param      checked 已提交的选中状态。
 * @return     不透明的 toggled 信号标识；返回值不指向可释放对象，也不得
 *             解引用。
 */
void* XAbstractButton_toggled_signal(XAbstractButton* self, bool checked);

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON */

#ifdef __cplusplus
}
#endif

#endif /* XABSTRACTBUTTON_H */
