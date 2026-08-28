/******************************************************************************
 * @file       XPushButton.h
 * @brief      XPushButton 按钮控件（对标 Qt 6.8 QPushButton / QAbstractButton）。
 * @details    XPushButton 直接继承 XWidget，把 Qt 的 QAbstractButton 公共
 *             按钮行为与 QPushButton 外观/默认按钮属性折叠进单类：
 *             - 文本：text/setText（XString 与 UTF-8 C 字符串版本）；
 *             - 图标：icon/setIcon、iconSize/setIconSize，使用 XIcon 值类型
 *               存储并走现有图标引擎渲染；
 *             - 按钮状态：checkable/checked/toggle、down/setDown、
 *               autoRepeat/autoRepeatDelay/autoRepeatInterval、
 *               autoExclusive/setAutoExclusive；
 *             - 点击行为：click/animateClick 与 hitButton 命中测试；
 *             - QPushButton 属性：autoDefault/setAutoDefault、
 *               isDefault/setDefault、flat/setFlat；
 *             - 信号：pressed/released/clicked(bool)/toggled(bool)，复用
 *               XObject 信号槽机制；
 *             - 事件：鼠标左键按下/释放/移动按命中切换 down 并发射信号；
 *               键盘 Space 模仿按钮按下/释放，Return/Enter 在默认或
 *               autoDefault 时触发点击；
 *             - 绘制：XPainter 输出简单 raised/sunken/flat 外观（含
 *               选中凹陷态），文本使用内置 8x16 点阵字体，图标按
 *               XIcon_paint 图标引擎渲染；不依赖任何平台 API。
 *             嵌入式裁剪由 XGuiConfig.h 的 XPUSHBUTTON_ON 控制；关闭时
 *             整个公共 API 裁剪。XPushButton 依赖 XWIDGET_ON（父类能力）
 *             与 XSTRING_ON（文本存储）；XICON_ON 关闭时图标接口保持
 *             空图标回退语义。
 * @note       近似边界：QAbstractButton 的按钮组（autoExclusive 仅保存
 *             标志，按钮互斥登记未实现）、快捷键、样式表/主题 bevel 与
 *             自动重复定时器不实现；autoDefault 已实现父对话框链自动
 *             解析（对标 Qt 6.8 QPushButtonPrivate::dialogParent）；菜单
 *             关联（setMenu/menu/showMenu）已实现但真实平台弹层未接入；
 *             hitButton 按控件矩形命中（Qt bevel 区域在无主题时可视为
 *             控件矩形）。详细说明见 XGui.md。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPUSHBUTTON_H
#define XPUSHBUTTON_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XString.h"
#include "XIcon.h"
#include "XPainter.h"
#include "XGeometry.h"
#include "XEvent.h"
#include "XMenu.h"

#if XWIDGET_ON && XPUSHBUTTON_ON

/* ==================== 虚函数表（覆盖 XWidget 派生槽位） ==================== */

/**
 * @brief XPushButton 虚函数表枚举。
 * @details 槽位数量与 XWidget 完全一致；XPushButton 不新增槽位，仅重载
 *          XClass 的 Copy/Move/Deinit 与 XObject 的 Event、XWidget 的
 *          PaintEvent/ChangeEvent/Mouse*Event/Key*Event/Focus*Event。
 */
XCLASS_DEFINE_BEGING(XPushButton)
XCLASS_DEFINE_EXTEND_END(XPushButton, XWidget)

/* ==================== AutoDefault 内部三态（对标 QPushButtonPrivate） ==================== */

/** @brief 自动默认按钮内部三态（对标 Qt 6.8 QPushButtonPrivate::AutoDefault）。 */
typedef enum XPushButtonAutoDefault
{
    XPushButtonAutoDefault_Auto = 0, /**< 由父对话框是否默认按钮自动推断。 */
    XPushButtonAutoDefault_Off = 1,  /**< 不启用自动默认。 */
    XPushButtonAutoDefault_On = 2    /**< 启用自动默认。 */
} XPushButtonAutoDefault;

/* ==================== 控件对象（对标 QPushButton） ==================== */

/**
 * @brief XPushButton 按钮控件对象。
 * @details 首成员 m_base 必须是 XWidget（继承）；m_text 拥有一个 XString，
 *          m_icon 为 XIcon 值类型，m_iconSize 保存图标渲染尺寸；其余为
 *          QAbstractButton/QPushButton 状态位与重复按键参数。
 */
typedef struct XPushButton
{
    XWidget             m_base;            /**< XWidget 基类成员。 */
    XString*            m_text;            /**< 按钮文本（拥有）。 */
    XIcon               m_icon;            /**< 按钮图标（值类型，共享数据）。 */
    XSize               m_iconSize;        /**< 图标渲染尺寸；无效值表示默认。 */
    bool                m_checkable;       /**< 是否可选中。 */
    bool                m_checked;         /**< 是否选中。 */
    bool                m_down;            /**< 是否处于按下面板状态。 */
    bool                m_pressed;         /**< 内部按下状态（鼠标/键盘命中期间）。 */
    bool                m_autoRepeat;      /**< 是否允许按住自动重复。 */
    bool                m_autoExclusive;   /**< 是否自动互斥（仅保存标志）。 */
    bool                m_flat;            /**< 是否为扁平按钮。 */
    bool                m_defaultButton;   /**< 是否为对话框默认按钮。 */
    XPushButtonAutoDefault m_autoDefault;  /**< 自动默认三态。 */
    int                 m_autoRepeatDelay; /**< 自动重复开始延迟（毫秒；默认 300）。 */
    int                 m_autoRepeatInterval; /**< 自动重复间隔（毫秒；默认 100）。 */
    XMenu*              m_menu;            /**< 关联弹出菜单（借用指针；不拥有）。 */
} XPushButton;

/* ==================== 生命周期（对标 QPushButton/QAbstractButton 构造析构） ==================== */

/** @brief XPushButton 类虚函数表初始化（重载 Event/Paint/Change/Mouse/Key/Focus/Copy/Move/Deinit）。 */
XVtable* XPushButton_class_init(void);

/**
 * @brief      初始化 XPushButton（对标 QPushButton(parent) 构造）。
 * @details    先初始化 XWidget 基类（parent/flags 语义同 XWidget_init），
 *             再挂 XPushButton 虚表并设置 Qt 默认值：文本空、图标空、
 *             checkable/checked/down/pressed=false、autoRepeat=false、
 *             autoExclusive=false、autoDefault=Auto、defaultButton=false、
 *             flat=false、autoRepeatDelay=300、autoRepeatInterval=100、
 *             尺寸策略 Minimum/Fixed + PushButton、前景角色 0（绘制时
 *             按背景角色 Button 推断为 ButtonText）、焦点策略 StrongFocus。
 * @param      self   待初始化对象；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志（可传 0 表示 Widget 类型）。
 */
void XPushButton_init(XPushButton* self, XWidget* parent, XWidgetFlags flags);

/** @brief 使用默认内存类型创建按钮控件（语义同 XWidget_create）。 */
#define XPushButton_create(parent, flags) XPushButton_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建按钮控件。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志。
 * @return     新对象指针；失败返回 NULL。
 */
XPushButton* XPushButton_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags);

/** @brief 通过 XClass 虚表释放 XPushButton 资源（栈/外部存储对象使用）。 */
#define XPushButton_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XPushButton 对象。 */
#define XPushButton_delete_base(self) XClass_delete_base((XClass*)(self))
/** @brief 深拷贝（文本深拷、图标按值拷贝；不复制父/窗口句柄）。 */
#define XPushButton_copy_base(self, other) XClass_copy_base((XClass*)(self), (const XClass*)(other))
/** @brief 移动语义（转移资源所有权；源对象内容字段归构造默认值）。 */
#define XPushButton_move_base(self, other) XClass_move_base((XClass*)(self), (XClass*)(other))

/* ==================== 文本（对标 QAbstractButton text/setText） ==================== */

/** @brief 查询按钮文本（对标 QAbstractButton::text；返回借用指针，生命周期同对象）。 */
const XString* XPushButton_text(const XPushButton* self);
/**
 * @brief      设置按钮文本（对标 QAbstractButton::setText）。
 * @details    与当前文本相同则为无操作；否则深拷贝新文本，刷新尺寸提示
 *             与几何并重绘。
 * @param      text 新文本；NULL 视为空。
 */
void XPushButton_setText(XPushButton* self, const XString* text);
/** @brief 设置按钮文本（UTF-8 C 字符串便利版本，含 '\0' 结尾）。 */
void XPushButton_setText_2(XPushButton* self, const char* utf8);

/* ==================== 图标（对标 QAbstractButton icon/setIcon/iconSize） ==================== */

/** @brief 返回按钮图标（对标 QAbstractButton::icon；按值返回共享数据）。 */
XIcon XPushButton_icon(const XPushButton* self);
/**
 * @brief      设置按钮图标（对标 QAbstractButton::setIcon）。
 * @details    复制图标值；随后刷新尺寸提示并重绘。
 * @param      icon 源图标指针；NULL 或空图标会清空按钮图标。
 */
void XPushButton_setIcon(XPushButton* self, const XIcon* icon);

/** @brief 查询按钮图标渲染尺寸（对标 QAbstractButton::iconSize）。 */
XSize XPushButton_iconSize(const XPushButton* self);
/**
 * @brief      设置按钮图标渲染尺寸（对标 QAbstractButton::setIconSize）。
 * @param      size 目标尺寸；NULL 或宽高不大于 0 表示恢复默认。
 */
void XPushButton_setIconSize(XPushButton* self, const XSize* size);

/* ==================== 选中状态（对标 QAbstractButton checkable/checked） ==================== */

/** @brief 查询按钮是否可选中（对标 QAbstractButton::isCheckable，默认 false）。 */
bool XPushButton_isCheckable(const XPushButton* self);
/**
 * @brief      设置按钮是否可选中（对标 QAbstractButton::setCheckable）。
 * @details    由可选中切回非可选中时，Qt 静默清除选中位且不发射
 *             toggled；本实现保持同语义。
 */
void XPushButton_setCheckable(XPushButton* self, bool checkable);
/** @brief 查询按钮是否选中（对标 QAbstractButton::isChecked，默认 false）。 */
bool XPushButton_isChecked(const XPushButton* self);
/**
 * @brief      设置按钮选中状态（对标 QAbstractButton::setChecked）。
 * @details    仅当 checkable 且新状态与当前不同时生效，完成后发射
 *             toggled(checked)。
 */
void XPushButton_setChecked(XPushButton* self, bool checked);
/**
 * @brief      切换选中状态并发射 toggled（对标 QAbstractButton::toggle）。
 * @details    非可选中按钮为无操作；使用 setChecked 的反向值。
 */
void XPushButton_toggle(XPushButton* self);

/* ==================== 按下/自动重复/互斥（对标 QAbstractButton） ==================== */

/** @brief 查询按下面板状态（对标 QAbstractButton::isDown，默认 false）。 */
bool XPushButton_isDown(const XPushButton* self);
/**
 * @brief      设置按下面板状态（对标 QAbstractButton::setDown）。
 * @details    只改变 m_down 并重绘，不直接发射 pressed/clicked 信号。
 */
void XPushButton_setDown(XPushButton* self, bool down);

/** @brief 查询是否允许按住自动重复（对标 QAbstractButton::autoRepeat，默认 false）。 */
bool XPushButton_autoRepeat(const XPushButton* self);
/** @brief 设置是否允许按住自动重复（对标 QAbstractButton::setAutoRepeat）。 */
void XPushButton_setAutoRepeat(XPushButton* self, bool repeat);
/** @brief 查询自动重复开始延迟（毫秒；默认 300）。 */
int XPushButton_autoRepeatDelay(const XPushButton* self);
/** @brief 设置自动重复开始延迟（毫秒；按 Qt 语义保存调用方整数）。 */
void XPushButton_setAutoRepeatDelay(XPushButton* self, int delay);
/** @brief 查询自动重复间隔（毫秒；默认 100）。 */
int XPushButton_autoRepeatInterval(const XPushButton* self);
/** @brief 设置自动重复间隔（毫秒；按 Qt 语义保存调用方整数）。 */
void XPushButton_setAutoRepeatInterval(XPushButton* self, int interval);
/** @brief 查询自动互斥标志（对标 QAbstractButton::autoExclusive；默认 false）。 */
bool XPushButton_autoExclusive(const XPushButton* self);
/** @brief 设置自动互斥标志（当前仅保存状态，不登记按钮组）。 */
void XPushButton_setAutoExclusive(XPushButton* self, bool exclusive);

/* ==================== 点击/命中（对标 QAbstractButton） ==================== */

/**
 * @brief      程序化点击按钮（对标 QAbstractButton::click）。
 * @details    禁用时直接返回；启用时内部 setDown(true)、发射 pressed、
 *             setDown(false)、切换 checkable 选中位、发射 released、
 *             clicked(checked)；选中位变化时发射 toggled(checked)。
 */
void XPushButton_click(XPushButton* self);
/**
 * @brief      动画点击（对标 QAbstractButton::animateClick）。
 * @details    本实现无动画定时器，语义为立即 click()；嵌入式裁剪下不
 *             占用事件循环。
 */
void XPushButton_animateClick(XPushButton* self);
/**
 * @brief      判断局部坐标是否命中按钮（对标 QPushButton::hitButton）。
 * @details    Qt 的命中基准是 bevel 区域，无主题资源时按控件矩形；
 *             本实现为控件矩形包含。
 * @param      self 按钮对象。
 * @param      pos 控件局部坐标；NULL 返回 false。
 * @return     命中返回 true。
 */
bool XPushButton_hitButton(const XPushButton* self, const XPoint* pos);

/* ==================== 默认/扁平（对标 QPushButton） ==================== */

/** @brief 查询自动默认按钮是否生效（对标 QPushButton::autoDefault）。 */
bool XPushButton_autoDefault(const XPushButton* self);
/**
 * @brief      设置自动默认按钮（对标 QPushButton::setAutoDefault）。
 * @param      enable true 设为 On，false 设为 Off。
 */
void XPushButton_setAutoDefault(XPushButton* self, bool enable);
/** @brief 查询是否为默认按钮（对标 QPushButton::isDefault，默认 false）。 */
bool XPushButton_isDefault(const XPushButton* self);
/**
 * @brief      设置是否为默认按钮（对标 QPushButton::setDefault）。
 * @details    设置为 true 时 autoDefault 同步为 On；false 恢复 Auto 三态。
 */
void XPushButton_setDefault(XPushButton* self, bool enable);
/** @brief 查询是否为扁平按钮（对标 QPushButton::isFlat，默认 false）。 */
bool XPushButton_isFlat(const XPushButton* self);
/** @brief 设置是否为扁平按钮（对标 QPushButton::setFlat）；变化时重绘。 */
void XPushButton_setFlat(XPushButton* self, bool flat);

/* ==================== 菜单（对标 QPushButton setMenu/menu/showMenu） ==================== */

/**
 * @brief      关联弹出菜单（对标 QPushButton::setMenu，Qt 6.8 qpushbutton.h:43）。
 * @details    把按钮变为菜单按钮；菜单所有权不转移，按钮只借用指针。与
 *             当前菜单相同或 self/menu 为 NULL 时按 Qt 语义处理：
 *             - self 为 NULL 直接无操作；
 *             - menu 为 NULL 表示解除当前菜单关联；
 *             - 替换后刷新几何与重绘，供绘制路径显示菜单箭头。
 * @param      self 按钮对象。
 * @param      menu 要关联的弹出菜单（借用指针）；NULL 解除关联。
 */
void XPushButton_setMenu(XPushButton* self, XMenu* menu);
/** @brief 返回当前关联的弹出菜单（对标 QPushButton::menu，qpushbutton.h:44）。 */
XMenu* XPushButton_menu(const XPushButton* self);
/**
 * @brief      显示关联菜单（对标 QPushButton::showMenu，qpushbutton.h:51）。
 * @details    无菜单或无有效按下上下文字段时直接返回；当前无平台弹层
 *             实现，设置为按下状态并重绘以表达弹层前的视觉状态，平台
 *             菜单层接入后应在此处打开菜单并在关闭时恢复 m_down。
 * @param      self 按钮对象。
 */
void XPushButton_showMenu(XPushButton* self);

/* ==================== 尺寸（对标 QPushButton sizeHint/minimumSizeHint） ==================== */

/**
 * @brief      查询按钮的建议尺寸（对标 QPushButton::sizeHint）。
 * @details    按 Qt 6.8 QPushButton::sizeHint 语义计算并保持到基类
 *             XWidget 存储位：空文本使用 "XXXX" 占位宽度；图标按显式
 *             iconSize（未设置时按 PM_ButtonIconSize=16）加上图标间距；
 *             文本高度取当前字体的点阵行高；关联菜单追加
 *             PM_MenuButtonIndicator=12；最后加 QCommonStyle
 *             CT_PushButton 的 buttonMargin(6)+defaultFrameWidth*2(4)。
 * @param      self 按钮对象；NULL 返回无效尺寸 (-1,-1)。
 * @return     建议尺寸。
 */
XSize XPushButton_sizeHint(const XPushButton* self);
/**
 * @brief      查询按钮的最小建议尺寸（对标 QPushButton::minimumSizeHint）。
 * @details    Qt 6.8 中 QPushButton::minimumSizeHint 直接返回 sizeHint()，
 *             本接口保持相同语义。
 * @param      self 按钮对象；NULL 返回无效尺寸 (-1,-1)。
 * @return     最小建议尺寸。
 */
XSize XPushButton_minimumSizeHint(const XPushButton* self);

/* ==================== 离屏绘制入口（对标 QPushButton::paintEvent 内容） ==================== */

/**
 * @brief      把按钮内容绘制到给定绘制器。
 * @details    与重绘事件中的绘制路径一致：按尺寸/禁用/扁平/按下/选中
 *             状态绘制背景与立体边框，然后画图标和文本。供离屏渲染与
 *             回归测试直接调用；不设置任何平台资源。
 * @param      self 按钮对象；NULL 无操作。
 * @param      painter 目标绘制器；NULL 无操作。
 */
void XPushButton_drawContents(XPushButton* self, XPainter* painter);

/* ==================== 信号（对标 QAbstractButton/QPushButton） ==================== */

/**
 * @brief      按下信号（对标 QAbstractButton::pressed）。
 * @details    按下（鼠标命中或键盘 Space 按下）时发射；self 为 NULL
 *             时返回信号函数地址（用于连接）。
 */
void* XPushButton_pressed_signal(XPushButton* self);
/**
 * @brief      释放信号（对标 QAbstractButton::released）。
 * @details    释放（鼠标丢失命中、键盘 Space 释放或 click 流程）时发射；
 *             self 为 NULL 时返回信号函数地址（用于连接）。
 */
void* XPushButton_released_signal(XPushButton* self);
/**
 * @brief      点击信号（对标 QAbstractButton::clicked(bool)）。
 * @details    参数为点击后的 checked 状态。self 为 NULL 时返回信号函数
 *             地址（用于连接）。
 */
void* XPushButton_clicked_signal(XPushButton* self, bool checked);
/**
 * @brief      选中变化信号（对标 QAbstractButton::toggled(bool)）。
 * @details    参数为变化后的 checked 状态。self 为 NULL 时返回信号函数
 *             地址（用于连接）。
 */
void* XPushButton_toggled_signal(XPushButton* self, bool checked);

#endif /* XWIDGET_ON && XPUSHBUTTON_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPUSHBUTTON_H */
