/******************************************************************************
 * @file       XWidget.h
 * @brief      XWidget 控件基类（对标 Qt 6.8 QWidget，实现全部公开 API 的 C 适配）。
 * @details    XWidget 继承 XObject，是整个控件体系的基类，提供：
 *             - 控件属性（XWidgetAttribute，数值与 Qt 6.8 Qt::WidgetAttribute
 *               完全一致）与窗口标志/类型（复用 XWindowType/XWindowFlags，
 *               数值与 Qt 6.8 Qt::WindowFlags 完全一致）；
 *             - 几何体系：pos/size/rect/geometry/frameGeometry、move/resize/
 *               setGeometry/setFixedSize、normalGeometry；
 *             - 尺寸约束：minimumSize/maximumSize/baseSize/sizeIncrement/
 *               sizeHint/minimumSizeHint 与 XWidgetSizePolicy（对标 QSizePolicy，
 *               支持水平/垂直策略、控件类型、双向拉伸与 heightForWidth）；
 *             - 控件树：parentWidget/setParent/childAt/childrenRect/
 *               childrenRegion/isAncestorOf/window/窗口句柄；
 *             - 可见性与窗口状态：setVisible/show/hide/showNormal/
 *               showMinimized/showMaximized/showFullScreen、windowState、
 *               模态 windowModality、激活 activateWindow、close；
 *             - 可用性与焦点：setEnabled/isEnabledTo、focusPolicy/setFocus/
 *               clearFocus/hasFocus/focusWidget；
 *             - 光标/提示/调色板：cursor/setCursor/unsetCursor、
 *               toolTip/setToolTip、palette/setPalette；
 *             - 绘制闭环：update/updateRect/updateRegion -> PAINT 事件 ->
 *               paintEvent 虚函数 -> XBackingStore 离屏缓冲递归合成 -> flush；
 *             - 17 个事件虚函数（对标 QWidget protected 事件处理函数）：
 *               paintEvent/resizeEvent/moveEvent/closeEvent/focusInEvent/
 *               focusOutEvent/enterEvent/leaveEvent/keyPressEvent/
 *               keyReleaseEvent/mousePressEvent/mouseReleaseEvent/
 *               mouseDoubleClickEvent/mouseMoveEvent/wheelEvent/showEvent/
 *               hideEvent，由 XWidget_event_base 统一按事件类型分派；
 *             - 顶层控件内嵌 XWidgetWindow（XWindow 子类，内部实现）桥接
 *               平台窗口与事件分发；子控件事件经命中测试自动换算局部坐标。
 *             本模块不依赖任何平台 API；窗口/后备存储/平台差异全部由
 *             XWindow/XBackingStore 与 Drive 后端隔离，嵌入式可用。
 * @note       模块总开关 XWIDGET_ON 定义于 XGuiConfig.h；置 0 时裁剪
 *             整个 XWidget 公共 API。XWidget 依赖 XWINDOW_ON；子能力
 *             XCURSOR_ON/XBACKINGSTORE_ON/XGUIAPPLICATION_ON/XAPPLICATION_ON
 *             关闭时对应接口按回退语义退化为空实现或空指针。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XWIDGET_H
#define XWIDGET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XObject.h"
#include "XMemory.h"
#include "XTypes.h"
#include "XGeometry.h"
#include "XEvent.h"
#include "XString.h"
#if XWINDOW_ON
#include "XWindow.h"
#endif /* XWINDOW_ON */
#if XWINDOW_ON && XACCESSIBLE_ON
#include "XAccessible.h"
#endif /* XWINDOW_ON && XACCESSIBLE_ON */

#if XWINDOWEVENT_ON
#include "XWindowEvent.h"
#endif /* XWINDOWEVENT_ON */

#if XBACKINGSTORE_ON
#include "XBackingStore.h"
#endif /* XBACKINGSTORE_ON */

/* XBackingStore.h 裁剪时仍保留指针型 API 的不透明类型，避免
 * XWidget 结构体和 backingStore()/paintDevice() 声明失去类型。 */
#if !(XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON && XPLATFORMINTEGRATION_ON)
typedef struct XBackingStore XBackingStore;
#endif

#include "XPalette.h"
#include "XIcon.h"
#include "XCursor.h"

/* ==================== 依赖类型前向声明（避免循环包含） ==================== */
/** @brief XApplication 前向声明；XWidget.c 内部完成焦点登记联动。 */
typedef struct XApplication XApplication;
/** @brief XImage 前向声明（paintDevice 返回值；具体 API 由 XImage.h 提供）。 */
typedef struct XImage XImage;
/** @brief XWidget 控件对象前向声明（XWidgetEventSlot / 虚表枚举早于完整定义）。 */
typedef struct XWidget XWidget;
/** @brief XWidgetWindow 顶层桥接窗口前向声明（XWindow 子类，内部实现）。 */
typedef struct XWidgetWindow XWidgetWindow;

/** @brief 控件按宽度计算高度的回调（对标 QWidget::heightForWidth）。 */
typedef int (*XWidgetHeightForWidthHandler)(XWidget* widget, int width,
                                            void* userData);
/** @brief XLayout 抽象布局前向声明（XLAYOUT_ON=1 时 m_layout 字段与
 *  布局 API 需要该类型；完整定义见 XLayout.h，避免 XWidget/XLayout 循环包含）。 */
#if XLAYOUT_ON
typedef struct XLayout XLayout;
#endif /* XLAYOUT_ON */

#if XWIDGET_ON

/* ==================== 尺寸上限常量（对标 Qt QWINDOWSIZE_MAX） ==================== */

/**
 * @brief 控件/布局尺寸上限（16777215 像素，与 Qt QWINDOWSIZE_MAX 一致）。
 * @details 布局系统（XLayoutItem 的最大尺寸默认值、盒式/网格的最大尺寸
 *          累加上限）与 XWidget 的 setMaximumSize 钳位共用该常量；任何
 *          尺寸值都不应超过此上限。
 */
#define XWIDGET_MAX_SIZE 16777215

/* ==================== 依赖子模块回退定义（保持裁剪后可编译） ==================== */

/** @brief 窗口标志位组合（对标 Qt::WindowFlags）。
 * @details XWINDOW_ON 打开时直接复用 XWindowFlags；XWINDOW_ON 关闭时
 *          XWidget 一并裁剪，仅留别名保证头文件语法完整。 */
typedef uint32_t XWidgetFlags;

#if !XWINDOW_ON
/** @brief 窗口类型枚举回退定义（XWINDOW_ON=0 时的最小子集，保证编译）。 */
typedef enum XWindowType
{
    XWindowType_Widget = 0x00000000, /**< Widget 类型（无窗口外壳）。 */
    XWindowType_Window = 0x00000001  /**< 普通顶层窗口。 */
} XWindowType;
#endif /* !XWINDOW_ON */

#if !XWINDOWEVENT_ON
/** @brief 焦点原因枚举回退定义（XWINDOWEVENT_ON=0 时的最小子集，保证编译）。 */
typedef enum XFocusReason
{
    XFocusReason_Mouse = 0,   /**< 鼠标点击引起的焦点变化。 */
    XFocusReason_Tab,         /**< Tab 键向前导航引起的焦点变化。 */
    XFocusReason_Backtab,     /**< 反向 Tab（Shift+Tab）导航引起的焦点变化。 */
    XFocusReason_ActiveWindow,/**< 活动窗口切换引起的焦点变化。 */
    XFocusReason_Shortcut,    /**< 快捷键引起的焦点变化。 */
    XFocusReason_MenuBar,     /**< 菜单栏导航引起的焦点变化。 */
    XFocusReason_Other,       /**< 其他程序原因。 */
    XFocusReason_NoReason     /**< 无原因。 */
} XFocusReason;
#endif /* !XWINDOWEVENT_ON */

/* ==================== 枚举类型（对标 Qt 6.8 qnamespace.h） ==================== */

/** @brief 控件属性位集合（对标 Qt 6.8 Qt::WidgetAttribute，数值完全一致）。
 * @details 属性用 192 位位集（3×uint64_t）保存：Qt::WidgetAttribute 最大位值
 *          达 129（TabletTracking），32 位不够容纳，故按位段划分——位 0..63 存
 *          m_bits[0]、64..127 存 m_bits[1]、128..191 存 m_bits[2]；属性访问统一
 *          走 XWidget_setAttribute/XWidget_testAttribute，内部常量与 XWidget.c
 *          的 XWidget_attrSet/XWidget_attrTest 配套。公开属性与 Qt 一一对应，
 *          WState 内部状态位一并保留以对齐数值。 */
typedef struct XWidgetAttributes
{
    uint64_t m_bits[3]; /**< 位段数组；m_bits[i] 覆盖位 [i*64, i*64+63]。 */
} XWidgetAttributes;

/** @brief 控件属性（对标 Qt 6.8 Qt::WidgetAttribute）。
 * @note  仅列出对嵌入式/桌面控件有直接语义的属性；其余平台专用属性
 *        （Mac/Windows 窗口类型提示等）暂不展开，位保留。 */
typedef enum XWidgetAttribute
{
    XWidgetAttribute_Disabled                = 0,   /**< 控件被禁用（对标 WA_Disabled）。 */
    XWidgetAttribute_UnderMouse              = 1,   /**< 指针位于控件上（对标 WA_UnderMouse）。 */
    XWidgetAttribute_MouseTracking           = 2,   /**< 无需按下按键即接收鼠标移动（对标 WA_MouseTracking）。 */
    XWidgetAttribute_OpaquePaintEvent        = 4,   /**< 绘制事件完全覆盖背景（对标 WA_OpaquePaintEvent）。 */
    XWidgetAttribute_StaticContents          = 5,   /**< 内容为静态位图（对标 WA_StaticContents）。 */
    XWidgetAttribute_LaidOut                 = 7,   /**< 已按布局排布（对标 WA_LaidOut）。 */
    XWidgetAttribute_PaintOnScreen           = 8,   /**< 直接绘制到屏幕（对标 WA_PaintOnScreen）。 */
    XWidgetAttribute_NoSystemBackground      = 9,   /**< 不绘制系统背景（对标 WA_NoSystemBackground）。 */
    XWidgetAttribute_UpdatesDisabled         = 10,  /**< 更新被禁用（对标 WA_UpdatesDisabled）。 */
    XWidgetAttribute_Mapped                  = 11,  /**< 控件已映射（对标 WA_Mapped）。 */
    XWidgetAttribute_InputMethodEnabled      = 14,  /**< 输入法已启用（对标 WA_InputMethodEnabled）。 */
    XWidgetAttribute_WState_Visible          = 15,  /**< 内部：可见状态位。 */
    XWidgetAttribute_WState_Hidden           = 16,  /**< 内部：隐藏状态位。 */
    XWidgetAttribute_ForceDisabled           = 32,  /**< 强制禁用（对标 WA_ForceDisabled）。 */
    XWidgetAttribute_SetCursor               = 38,  /**< 已设置自定义光标（对标 WA_SetCursor）。 */
    XWidgetAttribute_WindowModified          = 41,  /**< 窗口内容已修改（对标 WA_WindowModified）。 */
    XWidgetAttribute_Resized                 = 42,  /**< 内部：尺寸已变化。 */
    XWidgetAttribute_Moved                   = 43,  /**< 内部：位置已变化。 */
    XWidgetAttribute_PendingUpdate           = 44,  /**< 内部：有待处理更新。 */
    XWidgetAttribute_TransparentForMouseEvents = 51,/**< 鼠标事件穿透（对标 WA_TransparentForMouseEvents）。 */
    XWidgetAttribute_PaintUnclipped          = 52,  /**< 绘制不裁剪（对标 WA_PaintUnclipped）。 */
    XWidgetAttribute_DeleteOnClose           = 55,  /**< 关闭后删除对象（对标 WA_DeleteOnClose）。 */
    XWidgetAttribute_RightToLeft             = 56,  /**< 从右到左布局（对标 WA_RightToLeft）。 */
    XWidgetAttribute_SetLayoutDirection      = 57,  /**< 已设置布局方向（对标 WA_SetLayoutDirection）。 */
    XWidgetAttribute_ShowModal               = 70,  /**< 显示为模态（对标 WA_ShowModal，历史属性）。 */
    XWidgetAttribute_NoMousePropagation      = 73,  /**< 不向父传播鼠标事件（对标 WA_NoMousePropagation）。 */
    XWidgetAttribute_Hover                   = 74,  /**< 悬停时接收 paint（对标 WA_Hover）。 */
    XWidgetAttribute_QuitOnClose             = 76,  /**< 关闭时退出应用（对标 WA_QuitOnClose）。 */
    XWidgetAttribute_KeyboardFocusChange     = 77,  /**< 键盘可改变焦点（对标 WA_KeyboardFocusChange）。 */
    XWidgetAttribute_AcceptDrops             = 78,  /**< 接受拖放（对标 WA_AcceptDrops）。 */
    XWidgetAttribute_WindowPropagation       = 80,  /**< 窗口级属性传播（对标 WA_WindowPropagation）。 */
    XWidgetAttribute_AlwaysShowToolTips      = 84,  /**< 始终显示工具提示（对标 WA_AlwaysShowToolTips）。 */
    XWidgetAttribute_LayoutUsesWidgetRect    = 92,  /**< 布局使用控件矩形（对标 WA_LayoutUsesWidgetRect）。 */
    XWidgetAttribute_ShowWithoutActivating   = 98,  /**< 显示但不激活（对标 WA_ShowWithoutActivating）。 */
    XWidgetAttribute_NativeWindow            = 100, /**< 强制本地原生窗口（对标 WA_NativeWindow）。 */
    XWidgetAttribute_DontShowOnScreen        = 103, /**< 仅逻辑显示不画屏（对标 WA_DontShowOnScreen）。 */
    XWidgetAttribute_SetWindowModality       = 118, /**< 窗口模态已设置（对标 WA_SetWindowModality）。 */
    XWidgetAttribute_TranslucentBackground   = 120, /**< 半透明背景（对标 WA_TranslucentBackground）。 */
    XWidgetAttribute_AcceptTouchEvents       = 121, /**< 接受触摸事件（对标 WA_AcceptTouchEvents）。 */
    XWidgetAttribute_TabletTracking          = 129, /**< 板绘跟踪（对标 WA_TabletTracking）。 */
    XWidgetAttribute_AttributeCount          = 132  /**< 属性计数（保留位上限）。 */
} XWidgetAttribute;

/** @brief 键盘焦点策略（对标 Qt 6.8 Qt::FocusPolicy，数值一致）。 */
typedef enum XWidgetFocusPolicy
{
    XWidgetFocusPolicy_NoFocus     = 0x00, /**< 不接受焦点。 */
    XWidgetFocusPolicy_TabFocus    = 0x01, /**< Tab 键可聚焦。 */
    XWidgetFocusPolicy_ClickFocus  = 0x02, /**< 点击可聚焦。 */
    XWidgetFocusPolicy_StrongFocus = 0x0b, /**< Tab + 点击 + 方向键可聚焦。 */
    XWidgetFocusPolicy_WheelFocus  = 0x0f  /**< 强焦点 + 滚轮可聚焦。 */
} XWidgetFocusPolicy;

/** @brief 右键菜单策略（对标 Qt 6.8 Qt::ContextMenuPolicy）。 */
typedef enum XWidgetContextMenuPolicy
{
    XWidgetContextMenuPolicy_NoContextMenu   = 0, /**< 无上下文菜单。 */
    XWidgetContextMenuPolicy_PreferActions   = 1, /**< 优先使用动作（对标 Qt6 PreferActions）。 */
    XWidgetContextMenuPolicy_ActionsContextMenu = 1, /**< 动作上下文菜单（历史别名）。 */
    XWidgetContextMenuPolicy_CustomContextMenu  = 2, /**< 发射 customContextMenuRequested。 */
    XWidgetContextMenuPolicy_DefaultContextMenu = 3, /**< 默认上下文菜单。 */
    XWidgetContextMenuPolicy_PreventContextMenu = 4  /**< 禁止上下文菜单。 */
} XWidgetContextMenuPolicy;

/** @brief 布局方向（对标 Qt 6.8 Qt::LayoutDirection）。 */
typedef enum XWidgetLayoutDirection
{
    XWidgetLayoutDirection_LeftToRight = 0, /**< 从左到右。 */
    XWidgetLayoutDirection_RightToLeft = 1  /**< 从右到左。 */
} XWidgetLayoutDirection;

/** @brief 渲染标志位组合（对标 QWidget::RenderFlags）。 */
typedef enum XWidgetRenderFlag
{
    XWidgetRenderFlag_DrawWindowBackground = 0x01, /**< 绘制窗口背景。 */
    XWidgetRenderFlag_DrawChildren         = 0x02, /**< 绘制子控件。 */
    XWidgetRenderFlag_IgnoreMask           = 0x04  /**< 忽略遮罩。 */
} XWidgetRenderFlag;
typedef uint32_t XWidgetRenderFlags;

/* ==================== 尺寸策略（对标 Qt 6.8 QSizePolicy） ==================== */

/** @brief 尺寸策略（对标 QSizePolicy::Policy，数值一致）。 */
typedef enum XWidgetSizePolicyPolicy
{
    XWidgetSizePolicy_Fixed             = 0x00, /**< 固定尺寸。 */
    XWidgetSizePolicy_Minimum           = 0x01, /**< 最小尺寸即最优。 */
    XWidgetSizePolicy_ExpandingBase     = 0x02, /**< 位值：可扩展（内部）。 */
    XWidgetSizePolicy_MinimumExpanding  = 0x03, /**< 最小且可扩展。 */
    XWidgetSizePolicy_Maximum           = 0x04, /**< 最大尺寸即最优。 */
    XWidgetSizePolicy_Preferred         = 0x05, /**< 首选尺寸。 */
    XWidgetSizePolicy_Expanding         = 0x07, /**< 可扩展。 */
    XWidgetSizePolicy_IgnoreFlag        = 0x08, /**< 位值：忽略尺寸提示（内部）。 */
    XWidgetSizePolicy_Ignored           = 0x0d  /**< 忽略尺寸提示。 */
} XWidgetSizePolicyPolicy;

/** @brief 尺寸策略控件类型位组合（对标 QSizePolicy::ControlType）。 */
typedef enum XWidgetSizePolicyControlType
{
    XWidgetSizePolicyControl_DefaultType = 0x00000001, /**< 默认类型。 */
    XWidgetSizePolicyControl_ButtonBox   = 0x00000002, /**< 按钮盒。 */
    XWidgetSizePolicyControl_CheckBox    = 0x00000004, /**< 复选框。 */
    XWidgetSizePolicyControl_ComboBox    = 0x00000008, /**< 组合框。 */
    XWidgetSizePolicyControl_Frame       = 0x00000010, /**< 框架。 */
    XWidgetSizePolicyControl_GroupBox    = 0x00000020, /**< 分组框。 */
    XWidgetSizePolicyControl_Label       = 0x00000040, /**< 标签。 */
    XWidgetSizePolicyControl_Line        = 0x00000080, /**< 线条。 */
    XWidgetSizePolicyControl_LineEdit    = 0x00000100, /**< 单行编辑框。 */
    XWidgetSizePolicyControl_PushButton  = 0x00000200, /**< 推送按钮。 */
    XWidgetSizePolicyControl_RadioButton = 0x00000400, /**< 单选按钮。 */
    XWidgetSizePolicyControl_Slider      = 0x00000800, /**< 滑块。 */
    XWidgetSizePolicyControl_SpinBox     = 0x00001000, /**< 数值框。 */
    XWidgetSizePolicyControl_TabWidget   = 0x00002000, /**< 页签控件。 */
    XWidgetSizePolicyControl_ToolButton  = 0x00004000  /**< 工具按钮。 */
} XWidgetSizePolicyControlType;
typedef uint32_t XWidgetSizePolicyControlTypes;

/**
 * @brief      尺寸策略值类型（对标 QSizePolicy）。
 * @details    与 QSizePolicy 对应字段一致：水平/垂直策略各 4 位、控件类型
 *             5 位编码、双向拉伸各 8 位、heightForWidth/widthForHeight/
 *             retainSizeWhenHidden 各 1 位。控件默认 Preferred/Preferred、
 *             DefaultType、拉伸 0。 */
typedef struct XWidgetSizePolicy
{
    uint8_t m_horizontalPolicy;     /**< 水平策略（XWidgetSizePolicyPolicy）。 */
    uint8_t m_verticalPolicy;       /**< 垂直策略（XWidgetSizePolicyPolicy）。 */
    uint8_t m_controlType;          /**< 控件类型编码索引（0~14，对应 ControlType 位）。 */
    uint8_t m_horizontalStretch;    /**< 水平拉伸因子 [0,255]。 */
    uint8_t m_verticalStretch;      /**< 垂直拉伸因子 [0,255]。 */
    bool    m_hasHeightForWidth;    /**< 支持高度随宽度计算。 */
    bool    m_hasWidthForHeight;    /**< 支持宽度随高度计算。 */
    bool    m_retainSizeWhenHidden; /**< 隐藏时保留尺寸。 */
} XWidgetSizePolicy;

/** @brief 使用默认策略创建尺寸策略（Preferred/Preferred）。 */
XWidgetSizePolicy XWidgetSizePolicy_create(void);
/**
 * @brief      使用指定策略创建尺寸策略（对标 QSizePolicy(Policy, Policy, ControlType)）。
 * @param      horizontal 水平策略。
 * @param      vertical 垂直策略。
 * @param      controlType 控件类型；可传 0 表示 DefaultType。
 */
XWidgetSizePolicy XWidgetSizePolicy_create_ex(
    XWidgetSizePolicyPolicy horizontal, XWidgetSizePolicyPolicy vertical,
    XWidgetSizePolicyControlType controlType);
/** @brief 水平策略查询。 */
XWidgetSizePolicyPolicy XWidgetSizePolicy_horizontalPolicy(const XWidgetSizePolicy* self);
/** @brief 垂直策略查询。 */
XWidgetSizePolicyPolicy XWidgetSizePolicy_verticalPolicy(const XWidgetSizePolicy* self);
/** @brief 设置水平策略。 */
void XWidgetSizePolicy_setHorizontalPolicy(XWidgetSizePolicy* self, XWidgetSizePolicyPolicy policy);
/** @brief 设置垂直策略。 */
void XWidgetSizePolicy_setVerticalPolicy(XWidgetSizePolicy* self, XWidgetSizePolicyPolicy policy);
/** @brief 控件类型查询（返回位标志，对标 QSizePolicy::controlType）。 */
XWidgetSizePolicyControlType XWidgetSizePolicy_controlType(const XWidgetSizePolicy* self);
/** @brief 设置控件类型（对标 QSizePolicy::setControlType）。 */
void XWidgetSizePolicy_setControlType(XWidgetSizePolicy* self, XWidgetSizePolicyControlType type);
/** @brief 可扩展方向位（0=无；1=水平；2=垂直；3=双向），对标 expandingDirections。 */
int XWidgetSizePolicy_expandingDirections(const XWidgetSizePolicy* self);
/** @brief 查询 heightForWidth（对标 hasHeightForWidth）。 */
bool XWidgetSizePolicy_hasHeightForWidth(const XWidgetSizePolicy* self);
/** @brief 设置 heightForWidth（对标 setHeightForWidth）。 */
void XWidgetSizePolicy_setHeightForWidth(XWidgetSizePolicy* self, bool enabled);
/** @brief 查询 widthForHeight（对标 hasWidthForHeight）。 */
bool XWidgetSizePolicy_hasWidthForHeight(const XWidgetSizePolicy* self);
/** @brief 设置 widthForHeight（对标 setWidthForHeight）。 */
void XWidgetSizePolicy_setWidthForHeight(XWidgetSizePolicy* self, bool enabled);
/** @brief 水平拉伸因子查询（对标 horizontalStretch）。 */
int XWidgetSizePolicy_horizontalStretch(const XWidgetSizePolicy* self);
/** @brief 垂直拉伸因子查询（对标 verticalStretch）。 */
int XWidgetSizePolicy_verticalStretch(const XWidgetSizePolicy* self);
/** @brief 设置水平拉伸因子（钳位 [0,255]，对标 setHorizontalStretch）。 */
void XWidgetSizePolicy_setHorizontalStretch(XWidgetSizePolicy* self, int stretch);
/** @brief 设置垂直拉伸因子（钳位 [0,255]，对标 setVerticalStretch）。 */
void XWidgetSizePolicy_setVerticalStretch(XWidgetSizePolicy* self, int stretch);
/** @brief 隐藏时保留尺寸（对标 retainSizeWhenHidden）。 */
bool XWidgetSizePolicy_retainSizeWhenHidden(const XWidgetSizePolicy* self);
/** @brief 设置隐藏时保留尺寸（对标 setRetainSizeWhenHidden）。 */
void XWidgetSizePolicy_setRetainSizeWhenHidden(XWidgetSizePolicy* self, bool retain);
/** @brief 转置策略（横纵互换，对标 QSizePolicy::transposed）。 */
XWidgetSizePolicy XWidgetSizePolicy_transposed(const XWidgetSizePolicy* self);
/** @brief 判断两个策略是否相等。 */
bool XWidgetSizePolicy_isEqual(const XWidgetSizePolicy* a, const XWidgetSizePolicy* b);

/* ==================== 虚函数表（对标 QWidget protected 事件虚函数） ==================== */

/** @brief 控件事件槽函数签名。 */
typedef void (*XWidgetEventSlot)(XWidget* self, XEvent* event);

/**
 * @brief XWidget 虚函数表枚举。
 * @details 前 3 个槽位继承自 XClass（Copy/Move/Deinit）；XObject 的
 *          Event/EventFilter/ChildEvent/... 槽位由 XVTABLE_INHERIT_XCLASS
 *          (XObject) 继承；下述 17 个新槽位从 XCLASS_VTABLE_GET_SIZE(XObject)
 *          开始追加，分别对标 QWidget 的 paintEvent / resizeEvent / ... /
 *          hideEvent。 */
XCLASS_DEFINE_BEGING(XWidget)
XCLASS_DEFINE_ENUM(XWidget, PaintEvent) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XWidget, ResizeEvent),
XCLASS_DEFINE_ENUM(XWidget, MoveEvent),
XCLASS_DEFINE_ENUM(XWidget, CloseEvent),
XCLASS_DEFINE_ENUM(XWidget, FocusInEvent),
XCLASS_DEFINE_ENUM(XWidget, FocusOutEvent),
XCLASS_DEFINE_ENUM(XWidget, EnterEvent),
XCLASS_DEFINE_ENUM(XWidget, LeaveEvent),
XCLASS_DEFINE_ENUM(XWidget, KeyPressEvent),
XCLASS_DEFINE_ENUM(XWidget, KeyReleaseEvent),
XCLASS_DEFINE_ENUM(XWidget, InputMethodEvent),
XCLASS_DEFINE_ENUM(XWidget, DragEnterEvent),
XCLASS_DEFINE_ENUM(XWidget, DragMoveEvent),
XCLASS_DEFINE_ENUM(XWidget, DragLeaveEvent),
XCLASS_DEFINE_ENUM(XWidget, DropEvent),
XCLASS_DEFINE_ENUM(XWidget, MousePressEvent),
XCLASS_DEFINE_ENUM(XWidget, MouseReleaseEvent),
XCLASS_DEFINE_ENUM(XWidget, MouseDoubleClickEvent),
XCLASS_DEFINE_ENUM(XWidget, MouseMoveEvent),
XCLASS_DEFINE_ENUM(XWidget, WheelEvent),
XCLASS_DEFINE_ENUM(XWidget, ShowEvent),
XCLASS_DEFINE_ENUM(XWidget, HideEvent),
XCLASS_DEFINE_END(XWidget)

/**
 * @brief      XWidget 控件对象；m_class 必须是第一个成员（嵌 XObject）。
 * @details    字段含义：
 *             - m_windowFlags：窗口标志（XWindowFlags，默认 Widget=0）；
 *             - m_attributes：控件属性位集（XWidgetAttributes）；
 *             - m_focusPolicy：键盘焦点策略（默认 NoFocus）；
 *             - m_isWindow：顶层控件标志（无父对象或 Window 位）；
 *             - m_visible / m_explicitShow：生效可见状态与 setVisible 显式状态；
 *             - m_enabled / m_updatesEnabled：启用与更新使能；
 *             - m_windowRect：几何矩形（父坐标系，对标 QWidgetData::wrect）；
 *             - m_contentsRect：客户区（自身坐标，对标 QWidgetData::crect）；
 *             - m_minimumSize/m_maximumSize/m_baseSize/m_sizeIncrement：
 *               尺寸约束（QT 语义下最大默认 XWINDOW_MAX_SIZE）；
 *             - m_sizePolicy：尺寸策略（默认 Preferred/Preferred）；
 *             - m_normalGeometry：正常态几何（最小化/最大化时保存，对标
 *               QWidget::normalGeometry；未进入特殊状态前与 m_windowRect 一致）；
 *             - m_toolTipDuration：工具提示时长（毫秒，0=系统默认）；
 *             - m_windowOpacity：窗口不透明度（默认 1.0，同步到桥接窗口）；
 *             - m_mouseTracking / m_tabletTracking / m_acceptDrops：
 *               鼠标跟踪/板绘跟踪/接受拖放状态位；
 *             - m_toolTip：工具提示文本（拥有）；
 *             - m_windowTitle：顶层控件标题（同步到桥接窗口；拥有）；
 *             - m_icon：窗口图标（值类型，refcount 共享，对标 windowIcon）；
 *             - m_palette：控件调色板（值类型；仅 m_paletteSet=1 时覆盖应用调色板）；
 *             - m_dirty：待重绘区域；m_staticContents：静态内容区域；
 *             - m_windowHandle：顶层控件内嵌 XWidgetWindow（拥有，内部类）；
 *             - m_backingStore：顶层控件离屏后备存储（拥有）。
 *             调用方不得手工修改任何字段；几何/属性读写一律走公开 API。
 */
typedef struct XWidget
{
    XObject                 m_class;          /**< 基类成员；必须是第一个。 */
    XWidgetFlags            m_windowFlags;    /**< 窗口标志位组合。 */
    XWidgetAttributes       m_attributes;     /**< 控件属性位集。 */
    XWidgetFocusPolicy      m_focusPolicy;    /**< 键盘焦点策略。 */
    uint32_t                m_contextMenuPolicy : 4; /**< 右键菜单策略（XWidgetContextMenuPolicy）。 */
    uint32_t                m_layoutDirection : 2;   /**< 布局方向（XWidgetLayoutDirection）。 */
    uint32_t                m_isWindow : 1;          /**< 顶层控件标志。 */
    uint32_t                m_isClosing : 1;         /**< 正在执行 close()。 */
    uint32_t                m_inShow : 1;            /**< show 递归保护。 */
    uint32_t                m_inPaintEvent : 1;      /**< 绘制重入保护。 */
    uint32_t                m_visible : 1;           /**< 生效可见状态（含父链）。 */
    uint32_t                m_explicitShow : 1;      /**< setVisible 显式状态。 */
    uint32_t                m_enabled : 1;           /**< 启用状态。 */
    uint32_t                m_updatesEnabled : 1;    /**< 更新使能（默认 true）。 */
    uint32_t                m_autoFillBackground : 1;/**< 自动填充背景。 */
    uint32_t                m_paletteSet : 1;        /**< 是否已设置控件级调色板。 */
    uint32_t                m_mouseTracking : 1;     /**< 鼠标跟踪（无需按键即发移动事件）。 */
    uint32_t                m_tabletTracking : 1;    /**< 板绘跟踪。 */
    uint32_t                m_acceptDrops : 1;       /**< 接受拖放。 */
    XRect                   m_windowRect;      /**< 几何矩形（父坐标系）。 */
    XRect                   m_contentsRect;    /**< 客户区（自身坐标）。 */
    XSize                   m_minimumSize;     /**< 最小尺寸。 */
    XSize                   m_maximumSize;     /**< 最大尺寸。 */
    XSize                   m_baseSize;        /**< 基准尺寸。 */
    XSize                   m_sizeIncrement;   /**< 尺寸步进。 */
    XSize                   m_sizeHint;        /**< 尺寸提示（布局子系统接入前的存储位）。 */
    XSize                   m_minimumSizeHint; /**< 最小尺寸提示（存储位）。 */
    XWidgetSizePolicy       m_sizePolicy;      /**< 尺寸策略。 */
    XRect                   m_normalGeometry;  /**< 正常态几何（无边框矩形的正常位置尺寸）。 */
    XWindowStates           m_windowState;     /**< 窗口状态位（XWindowStates；顶层控件同步到桥接窗口）。 */
    XWindowModality         m_windowModality;  /**< 窗口模态（XWindowModality；默认 NonModal）。 */
    int                     m_toolTipDuration; /**< 工具提示时长（毫秒；0=系统默认）。 */
    float                   m_windowOpacity;   /**< 窗口不透明度（默认 1.0）。 */
    XString*                m_toolTip;         /**< 工具提示文本（拥有）。 */
    XString*                m_windowTitle;     /**< 顶层标题（拥有）。 */
    XString*                m_windowFilePath;  /**< 窗口文件路径（拥有）。 */
    XCursor*                m_cursor;          /**< 自定义光标（拥有；仅 XCURSOR_ON）。 */
    XIcon                   m_icon;            /**< 窗口图标（值类型 refcount 共享）。 */
    XPalette                m_palette;         /**< 控件调色板（值类型）。 */
    XRegion                 m_dirty;           /**< 待重绘区域（拥有）。 */
    XRegion                 m_staticContents;  /**< 静态内容区域（拥有）。 */
    XWidgetWindow*          m_windowHandle;    /**< 顶层桥接窗口（拥有；内部类）。 */
#if XWINDOW_ON && XACCESSIBLE_ON
    XAccessible*            m_accessible;      /**< 控件可访问节点（拥有）。 */
#endif /* XWINDOW_ON && XACCESSIBLE_ON */
    XBackingStore*          m_backingStore;    /**< 顶层后备存储（拥有）。 */
    XWidgetHeightForWidthHandler m_heightForWidthHandler; /**< hfw 回调（借用）。 */
    void*                   m_heightForWidthUserData; /**< hfw 回调上下文（借用）。 */
#if XLAYOUT_ON
    XLayout*                m_layout;          /**< 当前挂接的顶层布局（借用；
                                                *   对标 QWidgetPrivate::layout；
                                                *   控件销毁不释放布局对象）。 */
#endif /* XLAYOUT_ON */
} XWidget;

/* ==================== 类与实例生命周期 ==================== */

/**
 * @brief      初始化 XWidget 类虚函数表并返回共享表指针。
 * @return     XWidget 类的共享 XVtable 指针。
 */
XVtable* XWidget_class_init(void);

/**
 * @brief      初始化 XWidget（对标 QWidget 构造）。
 * @details    初始化 XObject 基类并登记父子关系；parent 为 NULL 且 flags
 *             未含 Window 位时自动补 Window 位形成顶层控件（Qt 语义）。
 *             调用方负责生命周期结束后的 deinit_base。
 * @param      self   待初始化对象；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志（可传 0 表示 Widget 类型）。
 */
void XWidget_init(XWidget* self, XWidget* parent, XWidgetFlags flags);

/** @brief 使用默认内存类型创建控件（headless 父对象语义同 init）。 */
#define XWidget_create(parent, flags) \
    XWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建控件。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志。
 * @return     新对象指针；失败返回 NULL。
 */
XWidget* XWidget_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);

/** @brief 通过 XClass 虚表释放 XWidget 资源（栈/外部存储对象使用）。 */
#define XWidget_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XWidget 对象。 */
#define XWidget_delete_base(self) XClass_delete_base((XClass*)(self))
/** @brief 深拷贝（对标 QWidget 拷贝构造语义的子集；不复制父/窗口句柄）。 */
#define XWidget_copy_base(self, other) XClass_copy_base((XClass*)(self), (const XClass*)(other))
/** @brief 移动语义（转移资源所有权；源对象归零）。 */
#define XWidget_move_base(self, other) XClass_move_base((XClass*)(self), (XClass*)(other))

/* ==================== 属性与窗口标志（对标 QWidget） ==================== */

/**
 * @brief      设置控件属性（对标 QWidget::setAttribute）。
 * @param      self 目标控件；可为 NULL。
 * @param      attribute 属性。
 * @param      on 置 1 打开、置 0 关闭。默认 on=true。
 */
void XWidget_setAttribute(XWidget* self, XWidgetAttribute attribute, bool on);
/**
 * @brief      查询控件属性（对标 QWidget::testAttribute）。
 * @return     属性已置位返回 true；控件为 NULL 返回 false。
 */
bool XWidget_testAttribute(const XWidget* self, XWidgetAttribute attribute);

/** @brief 查询是否顶层控件（对标 QWidget::isWindow）。 */
bool XWidget_isWindow(const XWidget* self);
/** @brief 查询窗口类型（对标 QWidget::windowType，低 8 位）。 */
XWindowType XWidget_windowType(const XWidget* self);
/** @brief 查询窗口标志（对标 QWidget::windowFlags）。 */
XWidgetFlags XWidget_windowFlags(const XWidget* self);
/** @brief 设置窗口标志（对标 QWidget::setWindowFlags）。 */
void XWidget_setWindowFlags(XWidget* self, XWidgetFlags flags);
/** @brief 覆盖窗口标志且不重建窗口（对标 QWidget::overrideWindowFlags）。 */
void XWidget_overrideWindowFlags(XWidget* self, XWidgetFlags flags);

/* ==================== 几何（对标 QWidget 几何体系） ==================== */

/** @brief 返回 X 坐标（对标 QWidget::x）。 */
int XWidget_x(const XWidget* self);
/** @brief 返回 Y 坐标（对标 QWidget::y）。 */
int XWidget_y(const XWidget* self);
/** @brief 返回位置（对标 QWidget::pos）。 */
XPoint XWidget_pos(const XWidget* self);
/** @brief 返回宽度（对标 QWidget::width）。 */
int XWidget_width(const XWidget* self);
/** @brief 返回高度（对标 QWidget::height）。 */
int XWidget_height(const XWidget* self);
/** @brief 返回尺寸（对标 QWidget::size）。 */
XSize XWidget_size(const XWidget* self);
/** @brief 返回客户区矩形 0,0,w,h（对标 QWidget::rect）。 */
XRect XWidget_rect(const XWidget* self);
/** @brief 返回几何矩形（父坐标系，对标 QWidget::geometry）。 */
XRect XWidget_geometry(const XWidget* self);
/** @brief 返回框架几何（顶层控件=geometry；子控件=geometry，对标 frameGeometry）。 */
XRect XWidget_frameGeometry(const XWidget* self);
/** @brief 返回框架尺寸（对标 QWidget::frameSize）。 */
XSize XWidget_frameSize(const XWidget* self);
/** @brief 返回框架位置（对标 QWidget::framePos）。 */
XPoint XWidget_framePos(const XWidget* self);
/** @brief 返回正常几何（最小化/最大化时保存的几何，对标 normalGeometry）。 */
XRect XWidget_normalGeometry(const XWidget* self);

/**
 * @brief      设置几何矩形（对标 QWidget::setGeometry(int,int,int,int)）。
 * @details    尺寸按最小/最大约束钳位；顶层控件同步到桥接窗口并触发
 *             resize/move 事件；子控件直接更新并在变化时派发事件。
 */
void XWidget_setGeometry(XWidget* self, int x, int y, int w, int h);
/** @brief 设置几何矩形（对标 QWidget::setGeometry(const QRect&)）。 */
void XWidget_setGeometryRect(XWidget* self, const XRect* rect);
/** @brief 移动位置（对标 QWidget::move(int,int)）。 */
void XWidget_move(XWidget* self, int x, int y);
/** @brief 移动位置（对标 QWidget::move(const QPoint&)）。 */
void XWidget_movePoint(XWidget* self, const XPoint* pos);
/** @brief 调整尺寸（对标 QWidget::resize(int,int)）。 */
void XWidget_resize(XWidget* self, int w, int h);
/** @brief 调整尺寸（对标 QWidget::resize(const QSize&)）。 */
void XWidget_resizeSize(XWidget* self, const XSize* size);
/** @brief 固定为指定尺寸（min=max=size，对标 QWidget::setFixedSize）。 */
void XWidget_setFixedSize(XWidget* self, int w, int h);
/** @brief 固定为指定尺寸（尺寸对象版本）。 */
void XWidget_setFixedSizeSize(XWidget* self, const XSize* size);
/** @brief 调整内容到尺寸提示（对标 QWidget::adjustSize；无布局时按 heightForWidth 规则）。 */
void XWidget_adjustSize(XWidget* self);

/* ==================== 尺寸约束与提示（对标 QWidget） ==================== */

/** @brief 查询最小尺寸（对标 QWidget::minimumSize）。 */
XSize XWidget_minimumSize(const XWidget* self);
/** @brief 设置最小尺寸（对标 QWidget::setMinimumSize）。 */
void XWidget_setMinimumSize(XWidget* self, int w, int h);
/** @brief 设置最小尺寸（尺寸对象版本）。 */
void XWidget_setMinimumSizeSize(XWidget* self, const XSize* size);
/** @brief 查询最小宽度（对标 minimumWidth）。 */
int XWidget_minimumWidth(const XWidget* self);
/** @brief 查询最小高度（对标 minimumHeight）。 */
int XWidget_minimumHeight(const XWidget* self);
/** @brief 设置最小宽度（对标 setMinimumWidth）。 */
void XWidget_setMinimumWidth(XWidget* self, int w);
/** @brief 设置最小高度（对标 setMinimumHeight）。 */
void XWidget_setMinimumHeight(XWidget* self, int h);
/** @brief 查询最大尺寸（对标 QWidget::maximumSize）。 */
XSize XWidget_maximumSize(const XWidget* self);
/** @brief 设置最大尺寸（对标 QWidget::setMaximumSize）。 */
void XWidget_setMaximumSize(XWidget* self, int w, int h);
/** @brief 设置最大尺寸（尺寸对象版本）。 */
void XWidget_setMaximumSizeSize(XWidget* self, const XSize* size);
/** @brief 查询最大宽度（对标 maximumWidth）。 */
int XWidget_maximumWidth(const XWidget* self);
/** @brief 查询最大高度（对标 maximumHeight）。 */
int XWidget_maximumHeight(const XWidget* self);
/** @brief 设置最大宽度（对标 setMaximumWidth）。 */
void XWidget_setMaximumWidth(XWidget* self, int w);
/** @brief 设置最大高度（对标 setMaximumHeight）。 */
void XWidget_setMaximumHeight(XWidget* self, int h);
/** @brief 查询基准尺寸（对标 baseSize）。 */
XSize XWidget_baseSize(const XWidget* self);
/** @brief 设置基准尺寸（对标 setBaseSize）。 */
void XWidget_setBaseSize(XWidget* self, int w, int h);
/** @brief 设置基准尺寸（尺寸对象版本）。 */
void XWidget_setBaseSizeSize(XWidget* self, const XSize* size);
/** @brief 查询尺寸步进（对标 sizeIncrement）。 */
XSize XWidget_sizeIncrement(const XWidget* self);
/** @brief 设置尺寸步进（对标 setSizeIncrement）。 */
void XWidget_setSizeIncrement(XWidget* self, int w, int h);
/** @brief 设置尺寸步进（尺寸对象版本）。 */
void XWidget_setSizeIncrementSize(XWidget* self, const XSize* size);
/** @brief 查询尺寸提示（对标 QWidget::sizeHint；无布局时返回存储提示，默认 -1x-1 无效）。 */
XSize XWidget_sizeHint(const XWidget* self);
/** @brief 查询最小尺寸提示（对标 QWidget::minimumSizeHint；默认 -1x-1 无效）。 */
XSize XWidget_minimumSizeHint(const XWidget* self);
/**
 * @brief      设置尺寸提示（布局子系统接入前的存储位；QWidget 子类通过
 *             override sizeHint() 提供，本实现以存储位等价提供）。
 */
void XWidget_setSizeHint(XWidget* self, const XSize* hint);
/** @brief 设置最小尺寸提示（同上，存储位）。 */
void XWidget_setMinimumSizeHint(XWidget* self, const XSize* hint);

/** @brief 查询控件按宽度计算的高度（对标 QWidget::heightForWidth）。 */
int XWidget_heightForWidth(const XWidget* self, int width);
/** @brief 设置控件按宽度计算的高度回调；NULL 清除回调。 */
void XWidget_setHeightForWidthHandler(XWidget* self,
                                      XWidgetHeightForWidthHandler handler,
                                      void* userData);

/** @brief 查询尺寸策略（对标 QWidget::sizePolicy）。 */
XWidgetSizePolicy XWidget_sizePolicy(const XWidget* self);
/** @brief 设置尺寸策略（对标 QWidget::setSizePolicy(Policy,Policy)）。 */
void XWidget_setSizePolicy(XWidget* self,
                           XWidgetSizePolicyPolicy horizontal,
                           XWidgetSizePolicyPolicy vertical);
/** @brief 设置完整尺寸策略（对标 QWidget::setSizePolicy(const QSizePolicy&)）。 */
void XWidget_setSizePolicyFull(XWidget* self, const XWidgetSizePolicy* policy);

/* ==================== 控件树与命中测试（对标 QWidget） ==================== */

/** @brief 返回父控件（对标 QWidget::parentWidget）。 */
XWidget* XWidget_parentWidget(const XWidget* self);
/**
 * @brief      设置父控件（对标 QWidget::setParent(QWidget*, WindowFlags)）。
 * @details    parent 为 NULL 时自动补 Window 位形成顶层控件；保留显式可见
 *             状态并在新父可见后恢复显示。
 */
void XWidget_setParent(XWidget* self, XWidget* parent, XWidgetFlags flags);
/** @brief 设置父控件（保留现有窗口标志；对标 setParent(QWidget*)）。 */
void XWidget_setParentPlain(XWidget* self, XWidget* parent);
/** @brief 返回子坐标命中控件（对标 QWidget::childAt(const QPoint&)）。 */
XWidget* XWidget_childAt(const XWidget* self, const XPoint* point);
/** @brief 返回子坐标命中控件（整数坐标版本）。 */
XWidget* XWidget_childAt_2(const XWidget* self, int x, int y);
/** @brief 返回全局坐标命中控件（遍历顶层控件，对标 QWidget::childAt 全局语义）。 */
XWidget* XWidget_childAtGlobal(const XWidget* self, const XPoint* globalPoint);
/** @brief 返回所有子控件外接矩形（对标 QWidget::childrenRect）。 */
XRect XWidget_childrenRect(const XWidget* self);
/** @brief 返回所有子控件区域并集（对标 QWidget::childrenRegion；返回深拷贝需 XRegion_deinit）。 */
XRegion XWidget_childrenRegion(const XWidget* self);
/** @brief 判断 child 是否为本控件的后代（对标 QWidget::isAncestorOf）。 */
bool XWidget_isAncestorOf(const XWidget* self, const XWidget* child);
/** @brief 返回所在顶层控件（对标 QWidget::window）。 */
XWidget* XWidget_window(const XWidget* self);
/** @brief 返回顶层控件窗口句柄（对标 QWidget::windowHandle；未显示前可为 NULL）。 */
XWindow* XWidget_nativeWindow(const XWidget* self);
/** @brief 返回顶层控件窗口句柄（别名，与 Qt 命名一致的 C 入口）。 */
XWindow* XWidget_windowHandle(const XWidget* self);

/* ==================== 坐标映射（对标 QWidget mapTo/mapFrom） ==================== */

/** @brief 映射本地坐标到全局（对标 QWidget::mapToGlobal）。 */
XPoint XWidget_mapToGlobal(const XWidget* self, const XPoint* local);
/** @brief 映射全局坐标到本地（对标 QWidget::mapFromGlobal）。 */
XPoint XWidget_mapFromGlobal(const XWidget* self, const XPoint* global);
/** @brief 映射本控件坐标到 target 控件（对标 QWidget::mapTo）。 */
XPoint XWidget_mapTo(const XWidget* self, const XWidget* target, const XPoint* local);
/** @brief 从 target 控件坐标映射到本控件（对标 QWidget::mapFrom）。 */
XPoint XWidget_mapFrom(const XWidget* self, const XWidget* source, const XPoint* local);

/* ==================== 可见性与窗口状态（对标 QWidget） ==================== */

/** @brief 查询生效可见状态（对标 QWidget::isVisible）。 */
bool XWidget_isVisible(const XWidget* self);
/** @brief 查询显式隐藏状态（对标 QWidget::isHidden）。 */
bool XWidget_isHidden(const XWidget* self);
/** @brief 相对指定祖先判断可见（对标 QWidget::isVisibleTo）。 */
bool XWidget_isVisibleTo(const XWidget* self, const XWidget* ancestor);
/** @brief 设置显式可见状态（对标 QWidget::setVisible）。 */
void XWidget_setVisible(XWidget* self, bool visible);
/** @brief 显示控件（对标 QWidget::show）。 */
void XWidget_show(XWidget* self);
/** @brief 隐藏控件（对标 QWidget::hide）。 */
void XWidget_hide(XWidget* self);
/** @brief 显示并恢复窗口化（对标 QWidget::showNormal）。 */
void XWidget_showNormal(XWidget* self);
/** @brief 显示并最小化（对标 QWidget::showMinimized）。 */
void XWidget_showMinimized(XWidget* self);
/** @brief 显示并最大化（对标 QWidget::showMaximized）。 */
void XWidget_showMaximized(XWidget* self);
/** @brief 显示并全屏（对标 QWidget::showFullScreen）。 */
void XWidget_showFullScreen(XWidget* self);
/** @brief 查询是否最小化（对标 QWidget::isMinimized）。 */
bool XWidget_isMinimized(const XWidget* self);
/** @brief 查询是否最大化（对标 QWidget::isMaximized）。 */
bool XWidget_isMaximized(const XWidget* self);
/** @brief 查询是否全屏（对标 QWidget::isFullScreen）。 */
bool XWidget_isFullScreen(const XWidget* self);
/** @brief 查询是否活动窗口（对标 QWidget::isActiveWindow）。 */
bool XWidget_isActiveWindow(const XWidget* self);
/** @brief 查询是否模态（对标 QWidget::isModal）。 */
bool XWidget_isModal(const XWidget* self);
/** @brief 查询窗口状态（对标 QWidget::windowState）。 */
XWindowStates XWidget_windowState(const XWidget* self);
/** @brief 设置窗口状态（对标 QWidget::setWindowState）。 */
void XWidget_setWindowState(XWidget* self, XWindowStates state);
/** @brief 查询窗口模态（对标 QWidget::windowModality）。 */
XWindowModality XWidget_windowModality(const XWidget* self);
/** @brief 设置窗口模态（对标 QWidget::setWindowModality）。 */
void XWidget_setWindowModality(XWidget* self, XWindowModality modality);
/** @brief 请求激活顶层窗口（对标 QWidget::activateWindow）。 */
void XWidget_activateWindow(XWidget* self);
/** @brief 关闭控件（对标 QWidget::close；closeEvent 未接受时返回 false）。 */
bool XWidget_close(XWidget* self);

/* ==================== 标题/图标/透明度（对标 QWidget 窗口装饰） ==================== */

/** @brief 查询窗口标题（对标 QWidget::windowTitle）。 */
const XString* XWidget_windowTitle(const XWidget* self);
/** @brief 设置窗口标题（对标 QWidget::setWindowTitle；顶层同步到桥接窗口）。 */
void XWidget_setWindowTitle(XWidget* self, const XString* title);
/** @brief 查询窗口图标（对标 QWidget::windowIcon）。 */
XIcon XWidget_windowIcon(const XWidget* self);
/** @brief 设置窗口图标（对标 QWidget::setWindowIcon）。 */
void XWidget_setWindowIcon(XWidget* self, const XIcon* icon);
/** @brief 查询窗口文件路径（对标 QWidget::windowFilePath）。 */
const XString* XWidget_windowFilePath(const XWidget* self);
/** @brief 设置窗口文件路径（对标 QWidget::setWindowFilePath）。 */
void XWidget_setWindowFilePath(XWidget* self, const XString* path);
/** @brief 查询窗口不透明度（对标 QWidget::windowOpacity）。 */
double XWidget_windowOpacity(const XWidget* self);
/** @brief 设置窗口不透明度（对标 QWidget::setWindowOpacity）。 */
void XWidget_setWindowOpacity(XWidget* self, double opacity);
/** @brief 查询窗口已修改标志（对标 QWidget::isWindowModified）。 */
bool XWidget_isWindowModified(const XWidget* self);
/** @brief 设置窗口已修改标志（对标 QWidget::setWindowModified）。 */
void XWidget_setWindowModified(XWidget* self, bool modified);

/* ==================== 可用性与焦点（对标 QWidget） ==================== */

/** @brief 查询启用状态（对标 QWidget::isEnabled）。 */
bool XWidget_isEnabled(const XWidget* self);
/** @brief 设置启用状态（对标 QWidget::setEnabled）。 */
void XWidget_setEnabled(XWidget* self, bool enabled);
/** @brief 相对指定祖先判断启用（对标 QWidget::isEnabledTo）。 */
bool XWidget_isEnabledTo(const XWidget* self, const XWidget* ancestor);
/** @brief 查询焦点策略（对标 QWidget::focusPolicy）。 */
XWidgetFocusPolicy XWidget_focusPolicy(const XWidget* self);
/** @brief 设置焦点策略（对标 QWidget::setFocusPolicy）。 */
void XWidget_setFocusPolicy(XWidget* self, XWidgetFocusPolicy policy);
/** @brief 查询是否持有焦点（对标 QWidget::hasFocus）。 */
bool XWidget_hasFocus(const XWidget* self);
/** @brief 返回顶层控件的焦点控件（对标 QWidget::focusWidget）。 */
XWidget* XWidget_focusWidget(const XWidget* self);
/** @brief 请求焦点（对标 QWidget::setFocus()，原因 NoReason）。 */
void XWidget_setFocus(XWidget* self);
/** @brief 请求焦点并指定原因（对标 QWidget::setFocus(Qt::FocusReason)）。 */
void XWidget_setFocusReason(XWidget* self, XFocusReason reason);
/** @brief 清除焦点（对标 QWidget::clearFocus）。 */
void XWidget_clearFocus(XWidget* self);
/** @brief 焦点前进（对标 QWidget::focusNextChild 的保护入口；返回是否成功）。 */
bool XWidget_focusNextChild(XWidget* self);
/** @brief 焦点后退（对标 QWidget::focusPreviousChild；返回是否成功）。 */
bool XWidget_focusPreviousChild(XWidget* self);

/** @brief 查询鼠标跟踪（对标 QWidget::hasMouseTracking）。 */
bool XWidget_hasMouseTracking(const XWidget* self);
/** @brief 设置鼠标跟踪（对标 QWidget::setMouseTracking）。 */
void XWidget_setMouseTracking(XWidget* self, bool enable);
/** @brief 查询板绘跟踪（对标 QWidget::hasTabletTracking）。 */
bool XWidget_hasTabletTracking(const XWidget* self);
/** @brief 设置板绘跟踪（对标 QWidget::setTabletTracking）。 */
void XWidget_setTabletTracking(XWidget* self, bool enable);
/** @brief 查询接受拖放（对标 QWidget::acceptDrops）。 */
bool XWidget_acceptDrops(const XWidget* self);
/** @brief 设置接受拖放（对标 QWidget::setAcceptDrops）。 */
void XWidget_setAcceptDrops(XWidget* self, bool enable);
/** @brief 查询右键菜单策略（对标 QWidget::contextMenuPolicy）。 */
XWidgetContextMenuPolicy XWidget_contextMenuPolicy(const XWidget* self);
/** @brief 设置右键菜单策略（对标 QWidget::setContextMenuPolicy）。 */
void XWidget_setContextMenuPolicy(XWidget* self, XWidgetContextMenuPolicy policy);
/** @brief 查询布局方向（对标 QWidget::layoutDirection）。 */
XWidgetLayoutDirection XWidget_layoutDirection(const XWidget* self);
/** @brief 设置布局方向（对标 QWidget::setLayoutDirection）。 */
void XWidget_setLayoutDirection(XWidget* self, XWidgetLayoutDirection direction);
/** @brief 取消显式布局方向（对标 QWidget::unsetLayoutDirection）。 */
void XWidget_unsetLayoutDirection(XWidget* self);

/* ==================== 光标/提示/调色板（对标 QWidget） ==================== */

/** @brief 查询光标（对标 QWidget::cursor；未设置时返回 ArrowCursor）。 */
XCursor XWidget_cursor(const XWidget* self);
/** @brief 设置光标（对标 QWidget::setCursor；仅在 XCURSOR_ON 时有效）。 */
void XWidget_setCursor(XWidget* self, const XCursor* cursor);
/** @brief 清除自定义光标（对标 QWidget::unsetCursor）。 */
void XWidget_unsetCursor(XWidget* self);
/** @brief 查询工具提示（对标 QWidget::toolTip）。 */
const XString* XWidget_toolTip(const XWidget* self);
/** @brief 设置工具提示（对标 QWidget::setToolTip）。 */
void XWidget_setToolTip(XWidget* self, const XString* tip);
/** @brief 查询工具提示时长（毫秒；0=系统默认，对标 QWidget::toolTipDuration）。 */
int XWidget_toolTipDuration(const XWidget* self);
/** @brief 设置工具提示时长（对标 QWidget::setToolTipDuration）。 */
void XWidget_setToolTipDuration(XWidget* self, int msec);
/** @brief 查询调色板（对标 QWidget::palette；未设置时返回应用调色板）。 */
XPalette XWidget_palette(const XWidget* self);
/** @brief 设置调色板（对标 QWidget::setPalette）。 */
void XWidget_setPalette(XWidget* self, const XPalette* palette);

/* ==================== 绘制闭环（对标 QWidget update/repaint） ==================== */

/** @brief 查询更新使能（对标 QWidget::updatesEnabled）。 */
bool XWidget_updatesEnabled(const XWidget* self);
/** @brief 设置更新使能（对标 QWidget::setUpdatesEnabled）。 */
void XWidget_setUpdatesEnabled(XWidget* self, bool enable);
/** @brief 查询自动填充背景（对标 QWidget::autoFillBackground）。 */
bool XWidget_autoFillBackground(const XWidget* self);
/** @brief 设置自动填充背景（对标 QWidget::setAutoFillBackground）。 */
void XWidget_setAutoFillBackground(XWidget* self, bool enable);

/**
 * @brief      调度整控件重绘（对标 QWidget::update()）。
 * @details    updatesEnabled=0 时忽略；区域并入脏区并折算到顶层控件，
 *             随后向顶层桥接窗口投递 PAINT 事件；若未初始化应用/窗口，
 *             仅记录脏区（下次显示时一次性重绘）。
 */
void XWidget_update(XWidget* self);
/** @brief 调度指定矩形重绘（对标 QWidget::update(const QRect&)）。 */
void XWidget_updateRect(XWidget* self, const XRect* rect);
/** @brief 调度指定区域重绘（对标 QWidget::update(const QRegion&)）。 */
void XWidget_updateRegion(XWidget* self, const XRegion* region);
/** @brief 立即重绘整控件（对标 QWidget::repaint()；同步发送 PAINT 事件）。 */
void XWidget_repaint(XWidget* self);
/** @brief 立即重绘指定矩形（对标 QWidget::repaint(const QRect&)）。 */
void XWidget_repaintRect(XWidget* self, const XRect* rect);
/** @brief 立即重绘指定区域（对标 QWidget::repaint(const QRegion&)）。 */
void XWidget_repaintRegion(XWidget* self, const XRegion* region);

/** @brief 返回顶层控件的后备存储（对标 QWidget::backingStore；无则 NULL）。 */
XBackingStore* XWidget_backingStore(const XWidget* self);
/**
 * @brief      返回绘制设备（对标 QBackingStore::paintDevice 的控件入口）。
 * @details    顶层控件返回后备存储内部 XImage；子控件返回顶层后备存储
 *             XImage（配合 XWidget_paintOffset 完成平移）。未显示或无
 *             后备存储返回 NULL。只能在后备存储 beginPaint/endPaint
 *             之间使用。
 */
XImage* XWidget_paintDevice(const XWidget* self);
/**
 * @brief      返回控件在顶层后备存储中的原点偏移（像素）。
 * @details    供 paintEvent 内 XPainter_translate 使用，把子控件本地坐标
 *             平移到后备存储坐标。
 */
XPoint XWidget_paintOffset(const XWidget* self);
/** @brief 查询控件尺寸（内部便捷接口；同 XWidget_size）。 */
XSize XWidget_size_hw(const XWidget* self);

/* ==================== 布局挂接（对标 QWidget::layout/setLayout） ==================== */

/**
 * @brief      返回控件当前挂接的顶层布局（对标 QWidget::layout）。
 * @details    布局对象仅按借用指针持有，不参与所有权；无布局返回 NULL。
 * @param      self 目标控件；可为 NULL。
 * @return     挂接的 XLayout 借用指针；无布局或失败返回 NULL。
 */
#if XLAYOUT_ON
XLayout* XWidget_layout(const XWidget* self);
#endif /* XLAYOUT_ON */

/**
 * @brief      把顶层布局挂接到控件上（对标 QWidget::setLayout）。
 * @details    布局按借用指针保存，控件销毁/删除时不会释放布局对象
 *             （Qt 语义：QLayout 归用户所有）；控件已有布局时先解除
 *             旧布局挂接再挂接新布局。挂接后立即按控件当前几何执行
 *             一次布局解算，之后控件尺寸变化/显示时自动重新解算。
 * @param      self 目标控件；可为 NULL。
 * @param      layout 布局借用指针；可为 NULL（仅解除旧布局挂接）。
 */
#if XLAYOUT_ON
void XWidget_setLayout(XWidget* self, XLayout* layout);
#endif /* XLAYOUT_ON */

/* ==================== 事件分派（对标 QWidget protected 事件虚函数） ==================== */

/**
 * @brief      控件事件总入口（对标 QWidget::event）。
 * @details    按 XEventType 分派到 17 个事件虚函数；未识别事件回退
 *             XObject 默认 Event 实现。
 * @return     事件已处理返回 true。
 */
bool XWidget_event_base(XWidget* self, XEvent* event);

/** @brief 绘制事件槽（对标 QWidget::paintEvent）。 */
void XWidget_paintEvent_base(XWidget* self, XEvent* event);
/** @brief 调整大小事件槽（对标 QWidget::resizeEvent）。 */
void XWidget_resizeEvent_base(XWidget* self, XEvent* event);
/** @brief 移动事件槽（对标 QWidget::moveEvent）。 */
void XWidget_moveEvent_base(XWidget* self, XEvent* event);
/** @brief 关闭事件槽（对标 QWidget::closeEvent；默认接受）。 */
void XWidget_closeEvent_base(XWidget* self, XEvent* event);
/** @brief 焦点进入事件槽（对标 QWidget::focusInEvent）。 */
void XWidget_focusInEvent_base(XWidget* self, XEvent* event);
/** @brief 焦点离开事件槽（对标 QWidget::focusOutEvent）。 */
void XWidget_focusOutEvent_base(XWidget* self, XEvent* event);
/** @brief 指针进入事件槽（对标 QWidget::enterEvent）。 */
void XWidget_enterEvent_base(XWidget* self, XEvent* event);
/** @brief 指针离开事件槽（对标 QWidget::leaveEvent）。 */
void XWidget_leaveEvent_base(XWidget* self, XEvent* event);
/** @brief 键盘按下事件槽（对标 QWidget::keyPressEvent）。 */
void XWidget_keyPressEvent_base(XWidget* self, XEvent* event);
/** @brief 键盘释放事件槽（对标 QWidget::keyReleaseEvent）。 */
void XWidget_keyReleaseEvent_base(XWidget* self, XEvent* event);
/** @brief 输入法组合/提交事件槽（对标 QWidget::inputMethodEvent）。 */
void XWidget_inputMethodEvent_base(XWidget* self, XEvent* event);
void XWidget_dragEnterEvent_base(XWidget* self, XEvent* event);
void XWidget_dragMoveEvent_base(XWidget* self, XEvent* event);
void XWidget_dragLeaveEvent_base(XWidget* self, XEvent* event);
void XWidget_dropEvent_base(XWidget* self, XEvent* event);
/** @brief 鼠标按下事件槽（对标 QWidget::mousePressEvent）。 */
void XWidget_mousePressEvent_base(XWidget* self, XEvent* event);
/** @brief 鼠标释放事件槽（对标 QWidget::mouseReleaseEvent）。 */
void XWidget_mouseReleaseEvent_base(XWidget* self, XEvent* event);
/** @brief 鼠标双击事件槽（对标 QWidget::mouseDoubleClickEvent）。 */
void XWidget_mouseDoubleClickEvent_base(XWidget* self, XEvent* event);
/** @brief 鼠标移动事件槽（对标 QWidget::mouseMoveEvent）。 */
void XWidget_mouseMoveEvent_base(XWidget* self, XEvent* event);
/** @brief 滚轮事件槽（对标 QWidget::wheelEvent）。 */
void XWidget_wheelEvent_base(XWidget* self, XEvent* event);
/** @brief 显示事件槽（对标 QWidget::showEvent）。 */
void XWidget_showEvent_base(XWidget* self, XEvent* event);
/** @brief 隐藏事件槽（对标 QWidget::hideEvent）。 */
void XWidget_hideEvent_base(XWidget* self, XEvent* event);

/* ==================== 通知信号（对标 QWidget 信号） ==================== */

/** @brief 窗口标题变化信号（对标 QWidget::windowTitleChanged）。 */
void* XWidget_windowTitleChanged_signal(XWidget* self, const XString* title);
/** @brief 窗口图标变化信号（对标 QWidget::windowIconChanged）。 */
void* XWidget_windowIconChanged_signal(XWidget* self, XIcon* icon);
/** @brief 自定义上下文菜单请求信号（对标 QWidget::customContextMenuRequested）。 */
void* XWidget_customContextMenuRequested_signal(XWidget* self, const XPoint* pos);

/* ==================== 平台/测试接入钩子（内部） ==================== */

/**
 * @brief      顶层控件窗口几何变化钩子（XWidgetWindow 事件桥调用）。
 * @details    由平台 ConfigureNotify/GeometryChange 驱动，同步几何字段并
 *             派发 move/resize 事件。
 */
void XWidget_applyWindowGeometry(XWidget* self, const XRect* geometry,
                                 const XSize* oldSize);
/**
 * @brief      顶层控件可见状态变化钩子（XWidgetWindow 事件桥调用）。
 * @param      visible 新显式可见状态。
 */
void XWidget_applyWindowVisibility(XWidget* self, bool visible);
/**
 * @brief      顶层控件后备存储刷新入口：把脏区放到离屏缓冲并上屏。
 * @details    paintEvent 由平台/应用事件环触发后集中调用；子控件脏区
 *             已折算到顶层坐标。无后备存储时自动按控件尺寸创建。
 */
void XWidget_flushBackingStore(XWidget* self, const XRegion* region);

#endif /* XWIDGET_ON */

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_H */
