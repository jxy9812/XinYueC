#ifndef XSTYLEOPTION_H
#define XSTYLEOPTION_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGeometry.h"
#include "XPalette.h"

#if XSTYLE_ON

typedef struct XIcon XIcon;   /**< 前向声明（对标 QIcon；借用指针）。 */
typedef struct XPixmap XPixmap; /**< 前向声明（对标 QPixmap）。 */
typedef struct XObject XObject; /**< 前向声明（对标 QObject）。 */
typedef struct XFont XFont;   /**< 前向声明（对标 QFont；借用指针）。 */
typedef struct XWidget XWidget; /**< 前向声明（对标 QWidget；借用指针）。 */

/**
 * @brief      状态位（对标 Qt 6.8 QStyle::StateFlag，数值一致）。
 */
typedef enum XStyleStateFlag
{
    XStyleState_None =         0x00000000, /**< 无状态（对标 State_None）。 */
    XStyleState_Enabled =      0x00000001, /**< 控件可用（对标 State_Enabled）。 */
    XStyleState_Raised =       0x00000002, /**< 凸起（对标 State_Raised）。 */
    XStyleState_Sunken =       0x00000004, /**< 凹陷（对标 State_Sunken）。 */
    XStyleState_Off =          0x00000008, /**< 关（对标 State_Off）。 */
    XStyleState_NoChange =     0x00000010, /**< 部分选中（对标 State_NoChange）。 */
    XStyleState_On =           0x00000020, /**< 开（对标 State_On）。 */
    XStyleState_DownArrow =    0x00000040, /**< 下箭头（对标 State_DownArrow）。 */
    XStyleState_Horizontal =   0x00000080, /**< 水平方向（对标 State_Horizontal）。 */
    XStyleState_HasFocus =     0x00000100, /**< 有焦点（对标 State_HasFocus）。 */
    XStyleState_Top =          0x00000200, /**< 顶部（对标 State_Top）。 */
    XStyleState_Bottom =       0x00000400, /**< 底部（对标 State_Bottom）。 */
    XStyleState_FocusAtBorder = 0x00000800, /**< 焦点在边缘（对标 State_FocusAtBorder）。 */
    XStyleState_AutoRaise =    0x00001000, /**< 自动凸起（对标 State_AutoRaise）。 */
    XStyleState_MouseOver =    0x00002000, /**< 鼠标悬停（对标 State_MouseOver）。 */
    XStyleState_UpArrow =      0x00004000, /**< 上箭头（对标 State_UpArrow）。 */
    XStyleState_Selected =     0x00008000, /**< 选中（对标 State_Selected）。 */
    XStyleState_Active =       0x00010000, /**< 窗口活动（对标 State_Active）。 */
    XStyleState_Window =       0x00020000, /**< 顶层窗口（对标 State_Window）。 */
    XStyleState_Open =         0x00040000, /**< 展开（对标 State_Open）。 */
    XStyleState_Children =     0x00080000, /**< 有子项（对标 State_Children）。 */
    XStyleState_Item =         0x00100000, /**< 是条目（对标 State_Item）。 */
    XStyleState_Sibling =      0x00200000, /**< 有兄弟项（对标 State_Sibling）。 */
    XStyleState_Editing =      0x00400000, /**< 编辑中（对标 State_Editing）。 */
    XStyleState_KeyboardFocusChange = 0x00800000, /**< 键盘焦点变化（对标 State_KeyboardFocusChange）。 */
    XStyleState_ReadOnly =     0x02000000  /**< 只读（对标 State_ReadOnly）。 */
} XStyleStateFlag;

/**
 * @brief      基元元素（对标 Qt 6.8 QStyle::PrimitiveElement；数值 = Qt 枚举序号）。
 */
typedef enum XStylePrimitiveElement
{
    XStylePE_Frame = 0,          /**< 普通边框（PE_Frame）。 */
    XStylePE_FrameDefaultButton = 1, /**< 默认按钮边框（PE_FrameDefaultButton）。 */
    XStylePE_FrameDockWidget = 2, /**< 停靠窗边框（PE_FrameDockWidget）。 */
    XStylePE_FrameFocusRect = 3, /**< 焦点虚线框（PE_FrameFocusRect）。 */
    XStylePE_FrameGroupBox = 4,  /**< 分组框边框（PE_FrameGroupBox）。 */
    XStylePE_FrameLineEdit = 5,  /**< 输入框边框（PE_FrameLineEdit）。 */
    XStylePE_FrameMenu = 6,      /**< 菜单边框（PE_FrameMenu）。 */
    XStylePE_FrameStatusBarItem = 7, /**< 状态栏项边框（PE_FrameStatusBarItem）。 */
    XStylePE_FrameTabWidget = 8, /**< 页签容器边框（PE_FrameTabWidget）。 */
    XStylePE_FrameWindow = 9,    /**< 窗口边框（PE_FrameWindow）。 */
    XStylePE_FrameButtonBevel = 10, /**< 按钮斜面边框（PE_FrameButtonBevel）。 */
    XStylePE_FrameButtonTool = 11, /**< 工具按钮边框（PE_FrameButtonTool）。 */
    XStylePE_FrameTabBarBase = 12, /**< 页签条基座（PE_FrameTabBarBase）。 */
    XStylePE_PanelButtonCommand = 13, /**< 命令按钮面板（PE_PanelButtonCommand）。 */
    XStylePE_PanelButtonBevel = 14, /**< 斜面按钮面板（PE_PanelButtonBevel）。 */
    XStylePE_PanelButtonTool = 15, /**< 工具按钮面板（PE_PanelButtonTool）。 */
    XStylePE_PanelMenuBar = 16,  /**< 菜单栏面板（PE_PanelMenuBar）。 */
    XStylePE_PanelToolBar = 17,  /**< 工具栏面板（PE_PanelToolBar）。 */
    XStylePE_PanelLineEdit = 18, /**< 输入框面板（PE_PanelLineEdit）。 */
    XStylePE_IndicatorArrowDown = 19, /**< 下箭头（PE_IndicatorArrowDown）。 */
    XStylePE_IndicatorArrowLeft = 20, /**< 左箭头（PE_IndicatorArrowLeft）。 */
    XStylePE_IndicatorArrowRight = 21, /**< 右箭头（PE_IndicatorArrowRight）。 */
    XStylePE_IndicatorArrowUp = 22, /**< 上箭头（PE_IndicatorArrowUp）。 */
    XStylePE_IndicatorBranch = 23, /**< 树分支指示（PE_IndicatorBranch）。 */
    XStylePE_IndicatorButtonDropDown = 24, /**< 下拉按钮指示（PE_IndicatorButtonDropDown）。 */
    XStylePE_IndicatorItemViewItemCheck = 25, /**< 条目勾选框（PE_IndicatorItemViewItemCheck）。 */
    XStylePE_IndicatorCheckBox = 26, /**< 复选框（PE_IndicatorCheckBox）。 */
    XStylePE_IndicatorDockWidgetResizeHandle = 27, /**< 停靠窗尺寸手柄（PE_IndicatorDockWidgetResizeHandle）。 */
    XStylePE_IndicatorHeaderArrow = 28, /**< 表头箭头（PE_IndicatorHeaderArrow）。 */
    XStylePE_IndicatorMenuCheckMark = 29, /**< 菜单对勾（PE_IndicatorMenuCheckMark）。 */
    XStylePE_IndicatorProgressChunk = 30, /**< 进度块（PE_IndicatorProgressChunk）。 */
    XStylePE_IndicatorRadioButton = 31, /**< 单选钮（PE_IndicatorRadioButton）。 */
    XStylePE_IndicatorSpinDown = 32, /**< 微调下按钮（PE_IndicatorSpinDown）。 */
    XStylePE_IndicatorSpinMinus = 33, /**< 微调减号（PE_IndicatorSpinMinus）。 */
    XStylePE_IndicatorSpinPlus = 34, /**< 微调加号（PE_IndicatorSpinPlus）。 */
    XStylePE_IndicatorSpinUp = 35, /**< 微调上按钮（PE_IndicatorSpinUp）。 */
    XStylePE_IndicatorToolBarHandle = 36, /**< 工具栏把手（PE_IndicatorToolBarHandle）。 */
    XStylePE_IndicatorToolBarSeparator = 37, /**< 工具栏分隔线（PE_IndicatorToolBarSeparator）。 */
    XStylePE_PanelTipLabel = 38, /**< 提示标签面板（PE_PanelTipLabel）。 */
    XStylePE_IndicatorTabTear = 39, /**< 页签撕裂指示（PE_IndicatorTabTear）。 */
    XStylePE_PanelScrollAreaCorner = 40, /**< 滚动区角落面板（PE_PanelScrollAreaCorner）。 */
    XStylePE_Widget = 41,        /**< 控件基元（PE_Widget）。 */
    XStylePE_IndicatorColumnViewArrow = 42, /**< 列视图箭头（PE_IndicatorColumnViewArrow）。 */
    XStylePE_IndicatorItemViewItemDrop = 43, /**< 条目拖放指示（PE_IndicatorItemViewItemDrop）。 */
    XStylePE_PanelItemViewItem = 44, /**< 条目视图项（PE_PanelItemViewItem）。 */
    XStylePE_PanelItemViewRow = 45, /**< 条目视图行（PE_PanelItemViewRow）。 */
    XStylePE_PanelStatusBar = 46, /**< 状态栏面板（PE_PanelStatusBar）。 */
    XStylePE_IndicatorTabClose = 47, /**< 页签关闭指示（PE_IndicatorTabClose）。 */
    XStylePE_PanelMenu = 48,     /**< 弹出菜单面板（PE_PanelMenu）。 */
    XStylePE_IndicatorTabTearRight = 49 /**< 右侧页签撕裂指示（PE_IndicatorTabTearRight）。 */
} XStylePrimitiveElement;

/**
 * @brief      控件元素（对标 Qt 6.8 QStyle::ControlElement；数值 = Qt 枚举序号）。
 */
typedef enum XStyleControlElement
{
    XStyleCE_PushButton = 0,     /**< 推按钮（CE_PushButton）。 */
    XStyleCE_PushButtonBevel = 1, /**< 推按钮斜面（CE_PushButtonBevel）。 */
    XStyleCE_PushButtonLabel = 2, /**< 推按钮标签（CE_PushButtonLabel）。 */
    XStyleCE_CheckBox = 3,       /**< 复选框（CE_CheckBox）。 */
    XStyleCE_CheckBoxLabel = 4,  /**< 复选框标签（CE_CheckBoxLabel）。 */
    XStyleCE_RadioButton = 5,    /**< 单选钮（CE_RadioButton）。 */
    XStyleCE_RadioButtonLabel = 6, /**< 单选钮标签（CE_RadioButtonLabel）。 */
    XStyleCE_TabBarTab = 7,      /**< 页签（CE_TabBarTab）。 */
    XStyleCE_TabBarTabShape = 8, /**< 页签形状（CE_TabBarTabShape）。 */
    XStyleCE_TabBarTabLabel = 9, /**< 页签标签（CE_TabBarTabLabel）。 */
    XStyleCE_ProgressBar = 10,   /**< 进度条（CE_ProgressBar）。 */
    XStyleCE_ProgressBarGroove = 11, /**< 进度槽（CE_ProgressBarGroove）。 */
    XStyleCE_ProgressBarContents = 12, /**< 进度内容（CE_ProgressBarContents）。 */
    XStyleCE_ProgressBarLabel = 13, /**< 进度标签（CE_ProgressBarLabel）。 */
    XStyleCE_MenuItem = 14,      /**< 菜单项（CE_MenuItem）。 */
    XStyleCE_MenuScroller = 15,  /**< 菜单滚动器（CE_MenuScroller）。 */
    XStyleCE_MenuVMargin = 16,   /**< 菜单垂直边距（CE_MenuVMargin）。 */
    XStyleCE_MenuHMargin = 17,   /**< 菜单水平边距（CE_MenuHMargin）。 */
    XStyleCE_MenuTearoff = 18,   /**< 菜单撕裂线（CE_MenuTearoff）。 */
    XStyleCE_MenuEmptyArea = 19, /**< 菜单空区（CE_MenuEmptyArea）。 */
    XStyleCE_MenuBarItem = 20,   /**< 菜单栏项（CE_MenuBarItem）。 */
    XStyleCE_MenuBarEmptyArea = 21, /**< 菜单栏空区（CE_MenuBarEmptyArea）。 */
    XStyleCE_ToolButtonLabel = 22, /**< 工具按钮标签（CE_ToolButtonLabel）。 */
    XStyleCE_Header = 23,        /**< 表头（CE_Header）。 */
    XStyleCE_HeaderSection = 24, /**< 表头段（CE_HeaderSection）。 */
    XStyleCE_HeaderLabel = 25,   /**< 表头标签（CE_HeaderLabel）。 */
    XStyleCE_ToolBoxTab = 26,    /**< 工具箱页（CE_ToolBoxTab）。 */
    XStyleCE_SizeGrip = 27,      /**< 尺寸手柄（CE_SizeGrip）。 */
    XStyleCE_Splitter = 28,      /**< 分隔条（CE_Splitter）。 */
    XStyleCE_RubberBand = 29,    /**< 橡皮筋框（CE_RubberBand）。 */
    XStyleCE_DockWidgetTitle = 30, /**< 停靠窗标题（CE_DockWidgetTitle）。 */
    XStyleCE_ScrollBarAddLine = 31, /**< 滚动条加行（CE_ScrollBarAddLine）。 */
    XStyleCE_ScrollBarSubLine = 32, /**< 滚动条减行（CE_ScrollBarSubLine）。 */
    XStyleCE_ScrollBarAddPage = 33, /**< 滚动条加页（CE_ScrollBarAddPage）。 */
    XStyleCE_ScrollBarSubPage = 34, /**< 滚动条减页（CE_ScrollBarSubPage）。 */
    XStyleCE_ScrollBarSlider = 35, /**< 滚动条滑块（CE_ScrollBarSlider）。 */
    XStyleCE_ScrollBarFirst = 36, /**< 滚动条起始（CE_ScrollBarFirst）。 */
    XStyleCE_ScrollBarLast = 37,  /**< 滚动条末尾（CE_ScrollBarLast）。 */
    XStyleCE_FocusFrame = 38,    /**< 焦点框（CE_FocusFrame）。 */
    XStyleCE_ComboBoxLabel = 39, /**< 组合框标签（CE_ComboBoxLabel）。 */
    XStyleCE_ToolBar = 40,       /**< 工具栏（CE_ToolBar）。 */
    XStyleCE_ToolBoxTabShape = 41, /**< 工具箱页形状（CE_ToolBoxTabShape）。 */
    XStyleCE_ToolBoxTabLabel = 42, /**< 工具箱页标签（CE_ToolBoxTabLabel）。 */
    XStyleCE_HeaderEmptyArea = 43, /**< 表头空区（CE_HeaderEmptyArea）。 */
    XStyleCE_ColumnViewGrip = 44, /**< 列视图手柄（CE_ColumnViewGrip）。 */
    XStyleCE_ItemViewItem = 45,  /**< 条目视图项（CE_ItemViewItem）。 */
    XStyleCE_ShapedFrame = 46    /**< 形状边框（CE_ShapedFrame）。 */
} XStyleControlElement;

/**
 * @brief      子元素（对标 Qt 6.8 QStyle::SubElement；数值 = Qt 枚举序号）。
 */
typedef enum XStyleSubElement
{
    XStyleSE_PushButtonContents = 0, /**< 按钮内容（SE_PushButtonContents）。 */
    XStyleSE_PushButtonFocusRect = 1, /**< 按钮焦点矩形（SE_PushButtonFocusRect）。 */
    XStyleSE_CheckBoxIndicator = 2, /**< 复选指示器（SE_CheckBoxIndicator）。 */
    XStyleSE_CheckBoxContents = 3, /**< 复选内容（SE_CheckBoxContents）。 */
    XStyleSE_CheckBoxFocusRect = 4, /**< 复选焦点矩形（SE_CheckBoxFocusRect）。 */
    XStyleSE_CheckBoxClickRect = 5, /**< 复选点击矩形（SE_CheckBoxClickRect）。 */
    XStyleSE_RadioButtonIndicator = 6, /**< 单选指示器（SE_RadioButtonIndicator）。 */
    XStyleSE_RadioButtonContents = 7, /**< 单选内容（SE_RadioButtonContents）。 */
    XStyleSE_RadioButtonFocusRect = 8, /**< 单选焦点矩形（SE_RadioButtonFocusRect）。 */
    XStyleSE_RadioButtonClickRect = 9, /**< 单选点击矩形（SE_RadioButtonClickRect）。 */
    XStyleSE_ComboBoxFocusRect = 10, /**< 组合框焦点矩形（SE_ComboBoxFocusRect）。 */
    XStyleSE_SliderFocusRect = 11, /**< 滑块焦点矩形（SE_SliderFocusRect）。 */
    XStyleSE_ProgressBarGroove = 12, /**< 进度槽（SE_ProgressBarGroove）。 */
    XStyleSE_ProgressBarContents = 13, /**< 进度内容（SE_ProgressBarContents）。 */
    XStyleSE_ProgressBarLabel = 14, /**< 进度标签（SE_ProgressBarLabel）。 */
    XStyleSE_ToolBoxTabContents = 15, /**< 工具箱页内容（SE_ToolBoxTabContents）。 */
    XStyleSE_HeaderLabel = 16,    /**< 表头标签（SE_HeaderLabel）。 */
    XStyleSE_HeaderArrow = 17,    /**< 表头箭头（SE_HeaderArrow）。 */
    XStyleSE_TabWidgetTabBar = 18, /**< 页签容器页签条（SE_TabWidgetTabBar）。 */
    XStyleSE_TabWidgetTabPane = 19, /**< 页签容器窗格（SE_TabWidgetTabPane）。 */
    XStyleSE_TabWidgetTabContents = 20, /**< 页签容器内容（SE_TabWidgetTabContents）。 */
    XStyleSE_TabWidgetLeftCorner = 21, /**< 页签容器左角（SE_TabWidgetLeftCorner）。 */
    XStyleSE_TabWidgetRightCorner = 22, /**< 页签容器右角（SE_TabWidgetRightCorner）。 */
    XStyleSE_ItemViewItemCheckIndicator = 23, /**< 条目勾选指示（SE_ItemViewItemCheckIndicator）。 */
    XStyleSE_TabBarTearIndicator = 24, /**< 页签撕裂指示（SE_TabBarTearIndicator）。 */
    XStyleSE_TreeViewDisclosureItem = 25, /**< 树视图展开项（SE_TreeViewDisclosureItem）。 */
    XStyleSE_LineEditContents = 26, /**< 输入框内容（SE_LineEditContents）。 */
    XStyleSE_FrameContents = 27,  /**< 边框内容（SE_FrameContents）。 */
    XStyleSE_DockWidgetCloseButton = 28, /**< 停靠关闭按钮（SE_DockWidgetCloseButton）。 */
    XStyleSE_DockWidgetFloatButton = 29, /**< 停靠浮动按钮（SE_DockWidgetFloatButton）。 */
    XStyleSE_DockWidgetTitleBarText = 30, /**< 停靠标题文本（SE_DockWidgetTitleBarText）。 */
    XStyleSE_DockWidgetIcon = 31, /**< 停靠图标（SE_DockWidgetIcon）。 */
    XStyleSE_CheckBoxLayoutItem = 32, /**< 复选布局项（SE_CheckBoxLayoutItem）。 */
    XStyleSE_ComboBoxLayoutItem = 33, /**< 组合框布局项（SE_ComboBoxLayoutItem）。 */
    XStyleSE_DateTimeEditLayoutItem = 34, /**< 日期时间编辑布局项（SE_DateTimeEditLayoutItem）。 */
    XStyleSE_LabelLayoutItem = 35, /**< 标签布局项（SE_LabelLayoutItem）。 */
    XStyleSE_ProgressBarLayoutItem = 36, /**< 进度条布局项（SE_ProgressBarLayoutItem）。 */
    XStyleSE_PushButtonLayoutItem = 37, /**< 按钮布局项（SE_PushButtonLayoutItem）。 */
    XStyleSE_RadioButtonLayoutItem = 38, /**< 单选布局项（SE_RadioButtonLayoutItem）。 */
    XStyleSE_SliderLayoutItem = 39, /**< 滑块布局项（SE_SliderLayoutItem）。 */
    XStyleSE_SpinBoxLayoutItem = 40, /**< 微调框布局项（SE_SpinBoxLayoutItem）。 */
    XStyleSE_ToolButtonLayoutItem = 41, /**< 工具按钮布局项（SE_ToolButtonLayoutItem）。 */
    XStyleSE_FrameLayoutItem = 42, /**< 边框布局项（SE_FrameLayoutItem）。 */
    XStyleSE_GroupBoxLayoutItem = 43, /**< 分组框布局项（SE_GroupBoxLayoutItem）。 */
    XStyleSE_TabWidgetLayoutItem = 44, /**< 页签容器布局项（SE_TabWidgetLayoutItem）。 */
    XStyleSE_ItemViewItemDecoration = 45, /**< 条目装饰（SE_ItemViewItemDecoration）。 */
    XStyleSE_ItemViewItemText = 46, /**< 条目文本（SE_ItemViewItemText）。 */
    XStyleSE_ItemViewItemFocusRect = 47, /**< 条目焦点矩形（SE_ItemViewItemFocusRect）。 */
    XStyleSE_TabBarTabLeftButton = 48, /**< 页签左按钮（SE_TabBarTabLeftButton）。 */
    XStyleSE_TabBarTabRightButton = 49, /**< 页签右按钮（SE_TabBarTabRightButton）。 */
    XStyleSE_TabBarTabText = 50,  /**< 页签文本（SE_TabBarTabText）。 */
    XStyleSE_ShapedFrameContents = 51, /**< 形状边框内容（SE_ShapedFrameContents）。 */
    XStyleSE_ToolBarHandle = 52,  /**< 工具栏把手（SE_ToolBarHandle）。 */
    XStyleSE_TabBarScrollLeftButton = 53, /**< 页签左滚动按钮（SE_TabBarScrollLeftButton）。 */
    XStyleSE_TabBarScrollRightButton = 54, /**< 页签右滚动按钮（SE_TabBarScrollRightButton）。 */
    XStyleSE_TabBarTearIndicatorRight = 55, /**< 右侧页签撕裂指示（SE_TabBarTearIndicatorRight）。 */
    XStyleSE_PushButtonBevel = 56 /**< 按钮斜面（SE_PushButtonBevel）。 */
} XStyleSubElement;

/**
 * @brief      子控件位标志（对标 Qt 6.8 QStyle::SubControl，数值一致）。
 */
typedef enum XStyleSubControl
{
    XStyleSC_None =               0x00000000, /**< 无（SC_None）。 */
    XStyleSC_ScrollBarAddLine =   0x00000001, /**< 滚动条加行（SC_ScrollBarAddLine）。 */
    XStyleSC_ScrollBarSubLine =   0x00000002, /**< 滚动条减行（SC_ScrollBarSubLine）。 */
    XStyleSC_ScrollBarAddPage =   0x00000004, /**< 滚动条加页（SC_ScrollBarAddPage）。 */
    XStyleSC_ScrollBarSubPage =   0x00000008, /**< 滚动条减页（SC_ScrollBarSubPage）。 */
    XStyleSC_ScrollBarFirst =     0x00000010, /**< 滚动条起始（SC_ScrollBarFirst）。 */
    XStyleSC_ScrollBarLast =      0x00000020, /**< 滚动条末尾（SC_ScrollBarLast）。 */
    XStyleSC_ScrollBarSlider =    0x00000040, /**< 滚动条滑块（SC_ScrollBarSlider）。 */
    XStyleSC_ScrollBarGroove =    0x00000080, /**< 滚动条槽（SC_ScrollBarGroove）。 */
    XStyleSC_SpinBoxUp =          0x00000001, /**< 微调上按钮（SC_SpinBoxUp）。 */
    XStyleSC_SpinBoxDown =        0x00000002, /**< 微调下按钮（SC_SpinBoxDown）。 */
    XStyleSC_SpinBoxFrame =       0x00000004, /**< 微调边框（SC_SpinBoxFrame）。 */
    XStyleSC_SpinBoxEditField =   0x00000008, /**< 微调编辑区（SC_SpinBoxEditField）。 */
    XStyleSC_ComboBoxFrame =      0x00000001, /**< 组合框边框（SC_ComboBoxFrame）。 */
    XStyleSC_ComboBoxEditField =  0x00000002, /**< 组合框编辑区（SC_ComboBoxEditField）。 */
    XStyleSC_ComboBoxArrow =      0x00000004, /**< 组合框箭头（SC_ComboBoxArrow）。 */
    XStyleSC_ComboBoxListBoxPopup = 0x00000008, /**< 组合框弹出列表（SC_ComboBoxListBoxPopup）。 */
    XStyleSC_SliderGroove =       0x00000001, /**< 滑块槽（SC_SliderGroove）。 */
    XStyleSC_SliderHandle =       0x00000002, /**< 滑块把手（SC_SliderHandle）。 */
    XStyleSC_SliderTickmarks =    0x00000004, /**< 滑块刻度（SC_SliderTickmarks）。 */
    XStyleSC_ToolButton =         0x00000001, /**< 工具按钮本体（SC_ToolButton）。 */
    XStyleSC_ToolButtonMenu =     0x00000002, /**< 工具按钮菜单箭头（SC_ToolButtonMenu）。 */
    XStyleSC_TitleBarSysMenu =    0x00000001, /**< 标题栏系统菜单（SC_TitleBarSysMenu）。 */
    XStyleSC_TitleBarMinButton =  0x00000002, /**< 标题栏最小化（SC_TitleBarMinButton）。 */
    XStyleSC_TitleBarMaxButton =  0x00000004, /**< 标题栏最大化（SC_TitleBarMaxButton）。 */
    XStyleSC_TitleBarCloseButton = 0x00000008, /**< 标题栏关闭（SC_TitleBarCloseButton）。 */
    XStyleSC_TitleBarNormalButton = 0x00000010, /**< 标题栏还原（SC_TitleBarNormalButton）。 */
    XStyleSC_TitleBarShadeButton = 0x00000020, /**< 标题栏遮蔽（SC_TitleBarShadeButton）。 */
    XStyleSC_TitleBarUnshadeButton = 0x00000040, /**< 标题栏取消遮蔽（SC_TitleBarUnshadeButton）。 */
    XStyleSC_TitleBarContextHelpButton = 0x00000080, /**< 标题栏帮助（SC_TitleBarContextHelpButton）。 */
    XStyleSC_TitleBarLabel =      0x00000100, /**< 标题栏标签（SC_TitleBarLabel）。 */
    XStyleSC_DialGroove =         0x00000001, /**< 表盘槽（SC_DialGroove）。 */
    XStyleSC_DialHandle =         0x00000002, /**< 表盘把手（SC_DialHandle）。 */
    XStyleSC_DialTickmarks =      0x00000004, /**< 表盘刻度（SC_DialTickmarks）。 */
    XStyleSC_GroupBoxCheckBox =   0x00000001, /**< 分组框勾选框（SC_GroupBoxCheckBox）。 */
    XStyleSC_GroupBoxLabel =      0x00000002, /**< 分组框标签（SC_GroupBoxLabel）。 */
    XStyleSC_GroupBoxContents =   0x00000004, /**< 分组框内容（SC_GroupBoxContents）。 */
    XStyleSC_GroupBoxFrame =      0x00000008, /**< 分组框边框（SC_GroupBoxFrame）。 */
    XStyleSC_MdiMinButton =       0x00000001, /**< MDI 最小化（SC_MdiMinButton）。 */
    XStyleSC_MdiNormalButton =    0x00000002, /**< MDI 还原（SC_MdiNormalButton）。 */
    XStyleSC_MdiCloseButton =     0x00000004, /**< MDI 关闭（SC_MdiCloseButton）。 */
    XStyleSC_CustomBase =         0xf0000000, /**< 自定义起点（SC_CustomBase）。 */
    XStyleSC_All =                0xffffffff  /**< 全部（SC_All）。 */
} XStyleSubControl;

/**
 * @brief      复杂控件（对标 Qt 6.8 QStyle::ComplexControl；数值 = Qt 枚举序号）。
 */
typedef enum XStyleComplexControl
{
    XStyleCC_SpinBox = 0,        /**< 微调框（CC_SpinBox）。 */
    XStyleCC_ComboBox = 1,       /**< 组合框（CC_ComboBox）。 */
    XStyleCC_ScrollBar = 2,      /**< 滚动条（CC_ScrollBar）。 */
    XStyleCC_Slider = 3,         /**< 滑块（CC_Slider）。 */
    XStyleCC_ToolButton = 4,     /**< 工具按钮（CC_ToolButton）。 */
    XStyleCC_TitleBar = 5,       /**< 标题栏（CC_TitleBar）。 */
    XStyleCC_Dial = 6,           /**< 表盘（CC_Dial）。 */
    XStyleCC_GroupBox = 7,       /**< 分组框（CC_GroupBox）。 */
    XStyleCC_MdiControls = 8     /**< MDI 控制（CC_MdiControls）。 */
} XStyleComplexControl;

/**
 * @brief      像素度量（对标 Qt 6.8 QStyle::PixelMetric；数值 = Qt 枚举序号）。
 */
typedef enum XStylePixelMetric
{
    XStylePM_ButtonMargin = 0,        /**< 按钮边距（PM_ButtonMargin）。 */
    XStylePM_ButtonDefaultIndicator = 1, /**< 默认按钮指示器（PM_ButtonDefaultIndicator）。 */
    XStylePM_MenuButtonIndicator = 2, /**< 菜单按钮指示器（PM_MenuButtonIndicator）。 */
    XStylePM_ButtonShiftHorizontal = 3, /**< 按钮水平位移（PM_ButtonShiftHorizontal）。 */
    XStylePM_ButtonShiftVertical = 4, /**< 按钮垂直位移（PM_ButtonShiftVertical）。 */
    XStylePM_DefaultFrameWidth = 5,   /**< 默认边框宽（PM_DefaultFrameWidth）。 */
    XStylePM_SpinBoxFrameWidth = 6,   /**< 微调框边框宽（PM_SpinBoxFrameWidth）。 */
    XStylePM_ComboBoxFrameWidth = 7,  /**< 组合框边框宽（PM_ComboBoxFrameWidth）。 */
    XStylePM_MaximumDragDistance = 8, /**< 最大拖动距离（PM_MaximumDragDistance）。 */
    XStylePM_ScrollBarExtent = 9,     /**< 滚动条范围（PM_ScrollBarExtent）。 */
    XStylePM_ScrollBarSliderMin = 10, /**< 滚动条滑块最小（PM_ScrollBarSliderMin）。 */
    XStylePM_SliderThickness = 11,    /**< 滑块总厚度（PM_SliderThickness）。 */
    XStylePM_SliderControlThickness = 12, /**< 滑块业务厚度（PM_SliderControlThickness）。 */
    XStylePM_SliderLength = 13,       /**< 滑块总长（PM_SliderLength）。 */
    XStylePM_SliderTickmarkOffset = 14, /**< 滑块刻度偏移（PM_SliderTickmarkOffset）。 */
    XStylePM_SliderSpaceAvailable = 15, /**< 滑块可用空间（PM_SliderSpaceAvailable）。 */
    XStylePM_DockWidgetSeparatorExtent = 16, /**< 停靠分隔范围（PM_DockWidgetSeparatorExtent）。 */
    XStylePM_DockWidgetHandleExtent = 17, /**< 停靠把手范围（PM_DockWidgetHandleExtent）。 */
    XStylePM_DockWidgetFrameWidth = 18, /**< 停靠边框宽（PM_DockWidgetFrameWidth）。 */
    XStylePM_TabBarTabOverlap = 19,   /**< 页签重叠（PM_TabBarTabOverlap）。 */
    XStylePM_TabBarTabHSpace = 20,    /**< 页签水平空间（PM_TabBarTabHSpace）。 */
    XStylePM_TabBarTabVSpace = 21,    /**< 页签垂直空间（PM_TabBarTabVSpace）。 */
    XStylePM_TabBarBaseHeight = 22,   /**< 页签基座高（PM_TabBarBaseHeight）。 */
    XStylePM_TabBarBaseOverlap = 23,  /**< 页签基座重叠（PM_TabBarBaseOverlap）。 */
    XStylePM_ProgressBarChunkWidth = 24, /**< 进度块宽（PM_ProgressBarChunkWidth）。 */
    XStylePM_SplitterWidth = 25,      /**< 分隔条宽（PM_SplitterWidth）。 */
    XStylePM_TitleBarHeight = 26,     /**< 标题栏高（PM_TitleBarHeight）。 */
    XStylePM_MenuScrollerHeight = 27, /**< 菜单滚动器高（PM_MenuScrollerHeight）。 */
    XStylePM_MenuHMargin = 28,        /**< 菜单水平边距（PM_MenuHMargin）。 */
    XStylePM_MenuVMargin = 29,        /**< 菜单垂直边距（PM_MenuVMargin）。 */
    XStylePM_MenuPanelWidth = 30,     /**< 菜单面板宽（PM_MenuPanelWidth）。 */
    XStylePM_MenuTearoffHeight = 31,  /**< 菜单撕裂高（PM_MenuTearoffHeight）。 */
    XStylePM_MenuDesktopFrameWidth = 32, /**< 菜单桌面边框宽（PM_MenuDesktopFrameWidth）。 */
    XStylePM_MenuBarPanelWidth = 33,  /**< 菜单栏面板宽（PM_MenuBarPanelWidth）。 */
    XStylePM_MenuBarItemSpacing = 34, /**< 菜单栏项间距（PM_MenuBarItemSpacing）。 */
    XStylePM_MenuBarVMargin = 35,     /**< 菜单栏垂直边距（PM_MenuBarVMargin）。 */
    XStylePM_MenuBarHMargin = 36,     /**< 菜单栏水平边距（PM_MenuBarHMargin）。 */
    XStylePM_IndicatorWidth = 37,     /**< 指示器宽（PM_IndicatorWidth）。 */
    XStylePM_IndicatorHeight = 38,    /**< 指示器高（PM_IndicatorHeight）。 */
    XStylePM_ExclusiveIndicatorWidth = 39, /**< 单选指示器宽（PM_ExclusiveIndicatorWidth）。 */
    XStylePM_ExclusiveIndicatorHeight = 40, /**< 单选指示器高（PM_ExclusiveIndicatorHeight）。 */
    XStylePM_MdiSubWindowFrameWidth = 44, /**< MDI 子窗边框宽（PM_MdiSubWindowFrameWidth）。 */
    XStylePM_MdiSubWindowMinimizedWidth = 45, /**< MDI 子窗最小宽（PM_MdiSubWindowMinimizedWidth）。 */
    XStylePM_HeaderMargin = 46,       /**< 表头边距（PM_HeaderMargin）。 */
    XStylePM_HeaderMarkSize = 47,     /**< 表头标记尺寸（PM_HeaderMarkSize）。 */
    XStylePM_HeaderGripMargin = 48,   /**< 表头手柄边距（PM_HeaderGripMargin）。 */
    XStylePM_TabBarTabShiftHorizontal = 49, /**< 页签水平位移（PM_TabBarTabShiftHorizontal）。 */
    XStylePM_TabBarTabShiftVertical = 50, /**< 页签垂直位移（PM_TabBarTabShiftVertical）。 */
    XStylePM_TabBarScrollButtonWidth = 51, /**< 页签滚动按钮宽（PM_TabBarScrollButtonWidth）。 */
    XStylePM_ToolBarFrameWidth = 52,  /**< 工具栏边框宽（PM_ToolBarFrameWidth）。 */
    XStylePM_ToolBarHandleExtent = 53, /**< 工具栏把手范围（PM_ToolBarHandleExtent）。 */
    XStylePM_ToolBarItemSpacing = 54, /**< 工具栏项间距（PM_ToolBarItemSpacing）。 */
    XStylePM_ToolBarItemMargin = 55,  /**< 工具栏项边距（PM_ToolBarItemMargin）。 */
    XStylePM_ToolBarSeparatorExtent = 56, /**< 工具栏分隔范围（PM_ToolBarSeparatorExtent）。 */
    XStylePM_ToolBarExtensionExtent = 57, /**< 工具栏扩展范围（PM_ToolBarExtensionExtent）。 */
    XStylePM_SpinBoxSliderHeight = 58, /**< 微调滑块高（PM_SpinBoxSliderHeight）。 */
    XStylePM_ToolBarIconSize = 59,    /**< 工具栏图标尺寸（PM_ToolBarIconSize）。 */
    XStylePM_ListViewIconSize = 60,   /**< 列表视图图标尺寸（PM_ListViewIconSize）。 */
    XStylePM_IconViewIconSize = 61,   /**< 图标视图图标尺寸（PM_IconViewIconSize）。 */
    XStylePM_SmallIconSize = 62,      /**< 小图标尺寸（PM_SmallIconSize）。 */
    XStylePM_LargeIconSize = 63,      /**< 大图标尺寸（PM_LargeIconSize）。 */
    XStylePM_FocusFrameVMargin = 64,  /**< 焦点框垂直边距（PM_FocusFrameVMargin）。 */
    XStylePM_FocusFrameHMargin = 65,  /**< 焦点框水平边距（PM_FocusFrameHMargin）。 */
    XStylePM_ToolTipLabelFrameWidth = 66, /**< 提示标签边框宽（PM_ToolTipLabelFrameWidth）。 */
    XStylePM_CheckBoxLabelSpacing = 67, /**< 复选框标签间距（PM_CheckBoxLabelSpacing）。 */
    XStylePM_TabBarIconSize = 68,     /**< 页签图标尺寸（PM_TabBarIconSize）。 */
    XStylePM_SizeGripSize = 69,       /**< 尺寸手柄尺寸（PM_SizeGripSize）。 */
    XStylePM_DockWidgetTitleMargin = 70, /**< 停靠标题边距（PM_DockWidgetTitleMargin）。 */
    XStylePM_MessageBoxIconSize = 71, /**< 消息框图标尺寸（PM_MessageBoxIconSize）。 */
    XStylePM_ButtonIconSize = 72,     /**< 按钮图标尺寸（PM_ButtonIconSize）。 */
    XStylePM_DockWidgetTitleBarButtonMargin = 73, /**< 停靠标题按钮边距（PM_DockWidgetTitleBarButtonMargin）。 */
    XStylePM_RadioButtonLabelSpacing = 74, /**< 单选钮标签间距（PM_RadioButtonLabelSpacing）。 */
    XStylePM_LayoutLeftMargin = 75,   /**< 布局左边距（PM_LayoutLeftMargin）。 */
    XStylePM_LayoutTopMargin = 76,    /**< 布局上边距（PM_LayoutTopMargin）。 */
    XStylePM_LayoutRightMargin = 77,  /**< 布局右边距（PM_LayoutRightMargin）。 */
    XStylePM_LayoutBottomMargin = 78, /**< 布局下边距（PM_LayoutBottomMargin）。 */
    XStylePM_LayoutHorizontalSpacing = 79, /**< 布局水平间距（PM_LayoutHorizontalSpacing）。 */
    XStylePM_LayoutVerticalSpacing = 80, /**< 布局垂直间距（PM_LayoutVerticalSpacing）。 */
    XStylePM_TabBar_ScrollButtonOverlap = 81, /**< 页签滚动按钮重叠（PM_TabBar_ScrollButtonOverlap）。 */
    XStylePM_TextCursorWidth = 82,    /**< 文本光标宽（PM_TextCursorWidth）。 */
    XStylePM_TabCloseIndicatorWidth = 83, /**< 页签关闭指示宽（PM_TabCloseIndicatorWidth）。 */
    XStylePM_TabCloseIndicatorHeight = 84, /**< 页签关闭指示高（PM_TabCloseIndicatorHeight）。 */
    XStylePM_ScrollView_ScrollBarSpacing = 85, /**< 滚动区滚动条间距（PM_ScrollView_ScrollBarSpacing）。 */
    XStylePM_ScrollView_ScrollBarOverlap = 86, /**< 滚动区滚动条重叠（PM_ScrollView_ScrollBarOverlap）。 */
    XStylePM_SubMenuOverlap = 87,     /**< 子菜单重叠（PM_SubMenuOverlap）。 */
    XStylePM_TreeViewIndentation = 88, /**< 树视图缩进（PM_TreeViewIndentation）。 */
    XStylePM_HeaderDefaultSectionSizeHorizontal = 89, /**< 表头默认水平段尺寸（PM_HeaderDefaultSectionSizeHorizontal）。 */
    XStylePM_HeaderDefaultSectionSizeVertical = 90, /**< 表头默认垂直段尺寸（PM_HeaderDefaultSectionSizeVertical）。 */
    XStylePM_TitleBarButtonIconSize = 91, /**< 标题栏按钮图标尺寸（PM_TitleBarButtonIconSize）。 */
    XStylePM_TitleBarButtonSize = 92, /**< 标题栏按钮尺寸（PM_TitleBarButtonSize）。 */
    XStylePM_LineEditIconSize = 93,   /**< 输入框图标尺寸（PM_LineEditIconSize）。 */
    XStylePM_LineEditIconMargin = 94  /**< 输入框图标边距（PM_LineEditIconMargin）。 */
} XStylePixelMetric;

/**
 * @brief      内容类型（对标 Qt 6.8 QStyle::ContentsType；数值 = Qt 枚举序号）。
 */
typedef enum XStyleContentsType
{
    XStyleCT_PushButton = 0,   /**< 按钮（CT_PushButton）。 */
    XStyleCT_CheckBox = 1,     /**< 复选框（CT_CheckBox）。 */
    XStyleCT_RadioButton = 2,  /**< 单选钮（CT_RadioButton）。 */
    XStyleCT_ToolButton = 3,   /**< 工具按钮（CT_ToolButton）。 */
    XStyleCT_ComboBox = 4,     /**< 组合框（CT_ComboBox）。 */
    XStyleCT_Splitter = 5,     /**< 分隔条（CT_Splitter）。 */
    XStyleCT_ProgressBar = 6,  /**< 进度条（CT_ProgressBar）。 */
    XStyleCT_MenuItem = 7,     /**< 菜单项（CT_MenuItem）。 */
    XStyleCT_MenuBarItem = 8,  /**< 菜单栏项（CT_MenuBarItem）。 */
    XStyleCT_MenuBar = 9,      /**< 菜单栏（CT_MenuBar）。 */
    XStyleCT_Menu = 10,        /**< 菜单（CT_Menu）。 */
    XStyleCT_TabBarTab = 11,   /**< 页签（CT_TabBarTab）。 */
    XStyleCT_Slider = 12,      /**< 滑块（CT_Slider）。 */
    XStyleCT_ScrollBar = 13,   /**< 滚动条（CT_ScrollBar）。 */
    XStyleCT_LineEdit = 14,    /**< 输入框（CT_LineEdit）。 */
    XStyleCT_SpinBox = 15,     /**< 微调框（CT_SpinBox）。 */
    XStyleCT_SizeGrip = 16,    /**< 尺寸手柄（CT_SizeGrip）。 */
    XStyleCT_TabWidget = 17,   /**< 页签容器（CT_TabWidget）。 */
    XStyleCT_DialogButtons = 18, /**< 对话框按钮（CT_DialogButtons）。 */
    XStyleCT_HeaderSection = 19, /**< 表头段（CT_HeaderSection）。 */
    XStyleCT_GroupBox = 20,    /**< 分组框（CT_GroupBox）。 */
    XStyleCT_MdiControls = 21, /**< MDI 控制（CT_MdiControls）。 */
    XStyleCT_ItemViewItem = 22 /**< 条目视图项（CT_ItemViewItem）。 */
} XStyleContentsType;

/**
 * @brief      样式提示（对标 Qt 6.8 QStyle::StyleHint；数值 = Qt 枚举序号）。
 */
typedef enum XStyleStyleHint
{
    XStyleSH_EtchDisabledText = 0,        /**< 蚀刻禁用文本（SH_EtchDisabledText）。 */
    XStyleSH_DitherDisabledText = 1,      /**< 抖动禁用文本（SH_DitherDisabledText）。 */
    XStyleSH_TabBar_SelectMouseType = 4,  /**< 页签选择鼠标事件（SH_TabBar_SelectMouseType）。 */
    XStyleSH_TabBar_Alignment = 5,        /**< 页签对齐（SH_TabBar_Alignment）。 */
    XStyleSH_Header_ArrowAlignment = 6,   /**< 表头箭头对齐（SH_Header_ArrowAlignment）。 */
    XStyleSH_Slider_SnapToValue = 7,      /**< 滑块吸附值（SH_Slider_SnapToValue）。 */
    XStyleSH_ProgressDialog_TextLabelAlignment = 10, /**< 进度对话框文本对齐（SH_ProgressDialog_TextLabelAlignment）。 */
    XStyleSH_PrintDialog_RightAlignButtons = 11, /**< 打印对话框右对齐按钮（SH_PrintDialog_RightAlignButtons）。 */
    XStyleSH_MainWindow_SpaceBelowMenuBar = 12, /**< 主窗菜单栏下方空间（SH_MainWindow_SpaceBelowMenuBar）。 */
    XStyleSH_FontDialog_SelectAssociatedText = 13, /**< 字体对话框关联文本（SH_FontDialog_SelectAssociatedText）。 */
    XStyleSH_Menu_AllowActiveAndDisabled = 14, /**< 菜单允许活动且禁用（SH_Menu_AllowActiveAndDisabled）。 */
    XStyleSH_Menu_SubMenuPopupDelay = 16, /**< 子菜单弹出延迟（SH_Menu_SubMenuPopupDelay）。 */
    XStyleSH_MenuBar_AltKeyNavigation = 18, /**< 菜单栏 Alt 键导航（SH_MenuBar_AltKeyNavigation）。 */
    XStyleSH_ComboBox_ListMouseTracking = 19, /**< 组合框列表鼠标跟踪（SH_ComboBox_ListMouseTracking）。 */
    XStyleSH_Menu_MouseTracking = 20,     /**< 菜单鼠标跟踪（SH_Menu_MouseTracking）。 */
    XStyleSH_MenuBar_MouseTracking = 21,  /**< 菜单栏鼠标跟踪（SH_MenuBar_MouseTracking）。 */
    XStyleSH_ItemView_ChangeHighlightOnFocus = 22, /**< 条目视图焦点高亮切换（SH_ItemView_ChangeHighlightOnFocus）。 */
    XStyleSH_ComboBox_Popup = 25,         /**< 组合框弹出（SH_ComboBox_Popup）。 */
    XStyleSH_Slider_StopMouseOverSlider = 27, /**< 滑块悬停停止（SH_Slider_StopMouseOverSlider）。 */
    XStyleSH_BlinkCursorWhenTextSelected = 28, /**< 文本选中时光标闪烁（SH_BlinkCursorWhenTextSelected）。 */
    XStyleSH_GroupBox_TextLabelVerticalAlignment = 31, /**< 分组框标签垂直对齐（SH_GroupBox_TextLabelVerticalAlignment）。 */
    XStyleSH_GroupBox_TextLabelColor = 32, /**< 分组框标签颜色（SH_GroupBox_TextLabelColor）。 */
    XStyleSH_Menu_SloppySubMenus = 33,    /**< 菜单模糊子菜单（SH_Menu_SloppySubMenus）。 */
    XStyleSH_Table_GridLineColor = 34,    /**< 表格网格线颜色（SH_Table_GridLineColor）。 */
    XStyleSH_LineEdit_PasswordCharacter = 35, /**< 输入框密码字符（SH_LineEdit_PasswordCharacter）。 */
    XStyleSH_DialogButtons_DefaultButton = 36, /**< 对话框按钮默认角色（SH_DialogButtons_DefaultButton）。 */
    XStyleSH_ToolBox_SelectedPageTitleBold = 37, /**< 工具箱选中页标题加粗（SH_ToolBox_SelectedPageTitleBold）。 */
    XStyleSH_ListViewExpand_SelectMouseType = 40, /**< 列表展开选择鼠标事件（SH_ListViewExpand_SelectMouseType）。 */
    XStyleSH_UnderlineShortcut = 41,      /**< 快捷键下划线（SH_UnderlineShortcut）。 */
    XStyleSH_SpinBox_ClickAutoRepeatRate = 44, /**< 微调点击重复率（SH_SpinBox_ClickAutoRepeatRate）。 */
    XStyleSH_ToolTipLabel_Opacity = 46,   /**< 提示标签不透明度（SH_ToolTipLabel_Opacity）。 */
    XStyleSH_TitleBar_AutoRaise = 51,     /**< 标题栏自动凸起（SH_TitleBar_AutoRaise）。 */
    XStyleSH_SpinControls_DisableOnBounds = 56, /**< 边界禁用微调控件（SH_SpinControls_DisableOnBounds）。 */
    XStyleSH_Dial_BackgroundRole = 57,    /**< 表盘背景角色（SH_Dial_BackgroundRole）。 */
    XStyleSH_ComboBox_LayoutDirection = 58, /**< 组合框布局方向（SH_ComboBox_LayoutDirection）。 */
    XStyleSH_ItemView_ShowDecorationSelected = 60, /**< 条目视图装饰选中（SH_ItemView_ShowDecorationSelected）。 */
    XStyleSH_ItemView_ActivateItemOnSingleClick = 61, /**< 条目单击激活（SH_ItemView_ActivateItemOnSingleClick）。 */
    XStyleSH_ScrollBar_ContextMenu = 62,  /**< 滚动条上下文菜单（SH_ScrollBar_ContextMenu）。 */
    XStyleSH_ScrollBar_RollBetweenButtons = 63, /**< 滚动条按钮间滚动（SH_ScrollBar_RollBetweenButtons）。 */
    XStyleSH_Slider_AbsoluteSetButtons = 64, /**< 滑块绝对设置键（SH_Slider_AbsoluteSetButtons）。 */
    XStyleSH_Slider_PageSetButtons = 65,  /**< 滑块页设置键（SH_Slider_PageSetButtons）。 */
    XStyleSH_Menu_KeyboardSearch = 66,    /**< 菜单键盘搜索（SH_Menu_KeyboardSearch）。 */
    XStyleSH_DialogButtonBox_ButtonsHaveIcons = 71, /**< 对话框按钮图标（SH_DialogButtonBox_ButtonsHaveIcons）。 */
    XStyleSH_Menu_SelectionWrap = 73,     /**< 菜单选择环绕（SH_Menu_SelectionWrap）。 */
    XStyleSH_WizardStyle = 78,            /**< 向导样式（SH_WizardStyle）。 */
    XStyleSH_ItemView_ArrowKeysNavigateIntoChildren = 79, /**< 条目视图方向键进入子项（SH_ItemView_ArrowKeysNavigateIntoChildren）。 */
    XStyleSH_Menu_Mask = 80,              /**< 菜单遮罩（SH_Menu_Mask）。 */
    XStyleSH_Menu_FlashTriggeredItem = 81, /**< 菜单触发项闪烁（SH_Menu_FlashTriggeredItem）。 */
    XStyleSH_Menu_FadeOutOnHide = 82,     /**< 菜单隐藏淡出（SH_Menu_FadeOutOnHide）。 */
    XStyleSH_SpinBox_ClickAutoRepeatThreshold = 83, /**< 微调点击重复阈值（SH_SpinBox_ClickAutoRepeatThreshold）。 */
    XStyleSH_ItemView_PaintAlternatingRowColorsForEmptyArea = 84, /**< 条目视图空区交替行色（SH_ItemView_PaintAlternatingRowColorsForEmptyArea）。 */
    XStyleSH_FormLayoutWrapPolicy = 85,   /**< 表单布局换行策略（SH_FormLayoutWrapPolicy）。 */
    XStyleSH_TabWidget_DefaultTabPosition = 86, /**< 页签容器默认页签位置（SH_TabWidget_DefaultTabPosition）。 */
    XStyleSH_ToolBar_Movable = 87,        /**< 工具栏可移动（SH_ToolBar_Movable）。 */
    XStyleSH_FormLayoutFieldGrowthPolicy = 88, /**< 表单布局字段增长策略（SH_FormLayoutFieldGrowthPolicy）。 */
    XStyleSH_FormLayoutFormAlignment = 89, /**< 表单布局表单对齐（SH_FormLayoutFormAlignment）。 */
    XStyleSH_FormLayoutLabelAlignment = 90, /**< 表单布局标签对齐（SH_FormLayoutLabelAlignment）。 */
    XStyleSH_ItemView_DrawDelegateFrame = 91, /**< 条目视图绘制委托边框（SH_ItemView_DrawDelegateFrame）。 */
    XStyleSH_TabBar_CloseButtonPosition = 92, /**< 页签关闭按钮位置（SH_TabBar_CloseButtonPosition）。 */
    XStyleSH_DockWidget_ButtonsHaveFrame = 93, /**< 停靠按钮有边框（SH_DockWidget_ButtonsHaveFrame）。 */
    XStyleSH_ToolButtonStyle = 94,        /**< 工具按钮样式（SH_ToolButtonStyle）。 */
    XStyleSH_RequestSoftwareInputPanel = 95, /**< 请求软输入面板（SH_RequestSoftwareInputPanel）。 */
    XStyleSH_ScrollBar_Transient = 96,    /**< 滚动条瞬态（SH_ScrollBar_Transient）。 */
    XStyleSH_Menu_SupportsSections = 97,  /**< 菜单支持分段（SH_Menu_SupportsSections）。 */
    XStyleSH_ToolTip_WakeUpDelay = 98,    /**< 提示唤醒延迟（SH_ToolTip_WakeUpDelay）。 */
    XStyleSH_ToolTip_FallAsleepDelay = 99, /**< 提示入睡延迟（SH_ToolTip_FallAsleepDelay）。 */
    XStyleSH_Widget_Animate = 100,        /**< 控件动画（SH_Widget_Animate）。 */
    XStyleSH_Splitter_OpaqueResize = 101, /**< 分隔条不透明调整（SH_Splitter_OpaqueResize）。 */
    XStyleSH_ComboBox_UseNativePopup = 102, /**< 组合框原生弹出（SH_ComboBox_UseNativePopup）。 */
    XStyleSH_LineEdit_PasswordMaskDelay = 103, /**< 输入框密码掩码延迟（SH_LineEdit_PasswordMaskDelay）。 */
    XStyleSH_TabBar_ChangeCurrentDelay = 104, /**< 页签切换延迟（SH_TabBar_ChangeCurrentDelay）。 */
    XStyleSH_Menu_SubMenuUniDirection = 105, /**< 子菜单单向（SH_Menu_SubMenuUniDirection）。 */
    XStyleSH_Menu_SubMenuUniDirectionFailCount = 106, /**< 子菜单单向失败计数（SH_Menu_SubMenuUniDirectionFailCount）。 */
    XStyleSH_Menu_SubMenuSloppySelectOtherActions = 107, /**< 子菜单模糊选择其他动作（SH_Menu_SubMenuSloppySelectOtherActions）。 */
    XStyleSH_Menu_SubMenuSloppyCloseTimeout = 108, /**< 子菜单模糊关闭超时（SH_Menu_SubMenuSloppyCloseTimeout）。 */
    XStyleSH_Menu_SubMenuResetWhenReenteringParent = 109, /**< 子菜单重入父级重置（SH_Menu_SubMenuResetWhenReenteringParent）。 */
    XStyleSH_Menu_SubMenuDontStartSloppyOnLeave = 110, /**< 子菜单离开不启动模糊（SH_Menu_SubMenuDontStartSloppyOnLeave）。 */
    XStyleSH_ItemView_ScrollMode = 111,   /**< 条目视图滚动模式（SH_ItemView_ScrollMode）。 */
    XStyleSH_Widget_Animation_Duration = 113, /**< 控件动画时长（SH_Widget_Animation_Duration）。 */
    XStyleSH_ComboBox_AllowWheelScrolling = 114, /**< 组合框允许滚轮（SH_ComboBox_AllowWheelScrolling）。 */
    XStyleSH_SpinBox_ButtonsInsideFrame = 115, /**< 微调按钮在框内（SH_SpinBox_ButtonsInsideFrame）。 */
    XStyleSH_SpinBox_StepModifier = 116,  /**< 微调步进修饰键（SH_SpinBox_StepModifier）。 */
    XStyleSH_TabBar_AllowWheelScrolling = 117, /**< 页签允许滚轮（SH_TabBar_AllowWheelScrolling）。 */
    XStyleSH_Table_AlwaysDrawLeftTopGridLines = 118, /**< 表格恒绘左上网格线（SH_Table_AlwaysDrawLeftTopGridLines）。 */
    XStyleSH_SpinBox_SelectOnStep = 119   /**< 微调步进时选中（SH_SpinBox_SelectOnStep）。 */
} XStyleStyleHint;

/**
 * @brief      标准图标/位图（对标 Qt 6.8 QStyle::StandardPixmap；数值 = Qt 枚举序号）。
 */
typedef enum XStyleStandardPixmap
{
    XStyleSP_TitleBarMenuButton = 0,      /**< 标题栏菜单按钮（SP_TitleBarMenuButton）。 */
    XStyleSP_TitleBarMinButton = 1,       /**< 标题栏最小化（SP_TitleBarMinButton）。 */
    XStyleSP_TitleBarMaxButton = 2,       /**< 标题栏最大化（SP_TitleBarMaxButton）。 */
    XStyleSP_TitleBarCloseButton = 3,     /**< 标题栏关闭（SP_TitleBarCloseButton）。 */
    XStyleSP_TitleBarNormalButton = 4,    /**< 标题栏还原（SP_TitleBarNormalButton）。 */
    XStyleSP_TitleBarShadeButton = 5,     /**< 标题栏遮蔽（SP_TitleBarShadeButton）。 */
    XStyleSP_TitleBarUnshadeButton = 6,   /**< 标题栏取消遮蔽（SP_TitleBarUnshadeButton）。 */
    XStyleSP_TitleBarContextHelpButton = 7, /**< 标题栏帮助（SP_TitleBarContextHelpButton）。 */
    XStyleSP_DockWidgetCloseButton = 8,   /**< 停靠关闭按钮（SP_DockWidgetCloseButton）。 */
    XStyleSP_MessageBoxInformation = 9,   /**< 消息框信息（SP_MessageBoxInformation）。 */
    XStyleSP_MessageBoxWarning = 10,      /**< 消息框警告（SP_MessageBoxWarning）。 */
    XStyleSP_MessageBoxCritical = 11,     /**< 消息框严重（SP_MessageBoxCritical）。 */
    XStyleSP_MessageBoxQuestion = 12,     /**< 消息框问题（SP_MessageBoxQuestion）。 */
    XStyleSP_DesktopIcon = 13,            /**< 桌面图标（SP_DesktopIcon）。 */
    XStyleSP_TrashIcon = 14,              /**< 回收站图标（SP_TrashIcon）。 */
    XStyleSP_ComputerIcon = 15,           /**< 电脑图标（SP_ComputerIcon）。 */
    XStyleSP_DriveFDIcon = 16,            /**< 软驱图标（SP_DriveFDIcon）。 */
    XStyleSP_DriveHDIcon = 17,            /**< 硬盘图标（SP_DriveHDIcon）。 */
    XStyleSP_DriveCDIcon = 18,            /**< 光驱图标（SP_DriveCDIcon）。 */
    XStyleSP_DriveDVDIcon = 19,           /**< DVD 图标（SP_DriveDVDIcon）。 */
    XStyleSP_DriveNetIcon = 20,           /**< 网络盘图标（SP_DriveNetIcon）。 */
    XStyleSP_DirOpenIcon = 21,            /**< 打开目录图标（SP_DirOpenIcon）。 */
    XStyleSP_DirClosedIcon = 22,          /**< 关闭目录图标（SP_DirClosedIcon）。 */
    XStyleSP_DirLinkIcon = 23,            /**< 目录链接图标（SP_DirLinkIcon）。 */
    XStyleSP_DirLinkOpenIcon = 24,        /**< 目录打开链接图标（SP_DirLinkOpenIcon）。 */
    XStyleSP_FileIcon = 25,               /**< 文件图标（SP_FileIcon）。 */
    XStyleSP_FileLinkIcon = 26,           /**< 文件链接图标（SP_FileLinkIcon）。 */
    XStyleSP_ToolBarHorizontalExtensionButton = 27, /**< 工具栏水平扩展按钮（SP_ToolBarHorizontalExtensionButton）。 */
    XStyleSP_ToolBarVerticalExtensionButton = 28, /**< 工具栏垂直扩展按钮（SP_ToolBarVerticalExtensionButton）。 */
    XStyleSP_FileDialogStart = 29,        /**< 文件对话框开始（SP_FileDialogStart）。 */
    XStyleSP_FileDialogEnd = 30,          /**< 文件对话框结束（SP_FileDialogEnd）。 */
    XStyleSP_FileDialogToParent = 31,     /**< 文件对话框上级（SP_FileDialogToParent）。 */
    XStyleSP_FileDialogNewFolder = 32,    /**< 文件对话框新文件夹（SP_FileDialogNewFolder）。 */
    XStyleSP_FileDialogDetailedView = 33, /**< 文件对话框详细视图（SP_FileDialogDetailedView）。 */
    XStyleSP_FileDialogInfoView = 34,     /**< 文件对话框信息视图（SP_FileDialogInfoView）。 */
    XStyleSP_FileDialogContentsView = 35, /**< 文件对话框内容视图（SP_FileDialogContentsView）。 */
    XStyleSP_FileDialogListView = 36,     /**< 文件对话框列表视图（SP_FileDialogListView）。 */
    XStyleSP_FileDialogBack = 37,         /**< 文件对话框返回（SP_FileDialogBack）。 */
    XStyleSP_DirIcon = 38,                /**< 目录图标（SP_DirIcon）。 */
    XStyleSP_DialogOkButton = 39,         /**< 对话框确定（SP_DialogOkButton）。 */
    XStyleSP_DialogCancelButton = 40,     /**< 对话框取消（SP_DialogCancelButton）。 */
    XStyleSP_DialogHelpButton = 41,       /**< 对话框帮助（SP_DialogHelpButton）。 */
    XStyleSP_DialogOpenButton = 42,       /**< 对话框打开（SP_DialogOpenButton）。 */
    XStyleSP_DialogSaveButton = 43,       /**< 对话框保存（SP_DialogSaveButton）。 */
    XStyleSP_DialogCloseButton = 44,      /**< 对话框关闭（SP_DialogCloseButton）。 */
    XStyleSP_DialogApplyButton = 45,      /**< 对话框应用（SP_DialogApplyButton）。 */
    XStyleSP_DialogResetButton = 46,      /**< 对话框重置（SP_DialogResetButton）。 */
    XStyleSP_DialogDiscardButton = 47,    /**< 对话框放弃（SP_DialogDiscardButton）。 */
    XStyleSP_DialogYesButton = 48,        /**< 对话框是（SP_DialogYesButton）。 */
    XStyleSP_DialogNoButton = 49,         /**< 对话框否（SP_DialogNoButton）。 */
    XStyleSP_ArrowUp = 50,                /**< 上箭头（SP_ArrowUp）。 */
    XStyleSP_ArrowDown = 51,              /**< 下箭头（SP_ArrowDown）。 */
    XStyleSP_ArrowLeft = 52,              /**< 左箭头（SP_ArrowLeft）。 */
    XStyleSP_ArrowRight = 53,             /**< 右箭头（SP_ArrowRight）。 */
    XStyleSP_ArrowBack = 54,              /**< 返回箭头（SP_ArrowBack）。 */
    XStyleSP_ArrowForward = 55,           /**< 前进箭头（SP_ArrowForward）。 */
    XStyleSP_DirHomeIcon = 56,            /**< 主目录图标（SP_DirHomeIcon）。 */
    XStyleSP_CommandLink = 57,            /**< 命令链接（SP_CommandLink）。 */
    XStyleSP_VistaShield = 58,            /**< UAC 盾牌（SP_VistaShield）。 */
    XStyleSP_BrowserReload = 59,          /**< 浏览器刷新（SP_BrowserReload）。 */
    XStyleSP_BrowserStop = 60,            /**< 浏览器停止（SP_BrowserStop）。 */
    XStyleSP_MediaPlay = 61,              /**< 媒体播放（SP_MediaPlay）。 */
    XStyleSP_MediaStop = 62,              /**< 媒体停止（SP_MediaStop）。 */
    XStyleSP_MediaPause = 63,             /**< 媒体暂停（SP_MediaPause）。 */
    XStyleSP_MediaSkipForward = 64,       /**< 媒体快进（SP_MediaSkipForward）。 */
    XStyleSP_MediaSkipBackward = 65,      /**< 媒体快退（SP_MediaSkipBackward）。 */
    XStyleSP_MediaSeekForward = 66,       /**< 媒体前进（SP_MediaSeekForward）。 */
    XStyleSP_MediaSeekBackward = 67,      /**< 媒体后退（SP_MediaSeekBackward）。 */
    XStyleSP_MediaVolume = 68,            /**< 媒体音量（SP_MediaVolume）。 */
    XStyleSP_MediaVolumeMuted = 69,       /**< 媒体静音（SP_MediaVolumeMuted）。 */
    XStyleSP_LineEditClearButton = 70,    /**< 输入框清除按钮（SP_LineEditClearButton）。 */
    XStyleSP_DialogYesToAllButton = 71,   /**< 对话框全部是（SP_DialogYesToAllButton）。 */
    XStyleSP_DialogNoToAllButton = 72,    /**< 对话框全部否（SP_DialogNoToAllButton）。 */
    XStyleSP_DialogSaveAllButton = 73,    /**< 对话框全部保存（SP_DialogSaveAllButton）。 */
    XStyleSP_DialogAbortButton = 74,      /**< 对话框中止（SP_DialogAbortButton）。 */
    XStyleSP_DialogRetryButton = 75,      /**< 对话框重试（SP_DialogRetryButton）。 */
    XStyleSP_DialogIgnoreButton = 76,     /**< 对话框忽略（SP_DialogIgnoreButton）。 */
    XStyleSP_RestoreDefaultsButton = 77,  /**< 恢复默认按钮（SP_RestoreDefaultsButton）。 */
    XStyleSP_TabCloseButton = 78,         /**< 页签关闭按钮（SP_TabCloseButton）。 */
    XStyleSP_NStandardPixmap = 79         /**< 标准位图计数（NStandardPixmap）。 */
} XStyleStandardPixmap;

/* ==================== QStyleOption 族补齐（对标 Qt 6.8 qstyleoption.h） ==================== */

/**
 * @brief      样式选项类型（对标 Qt 6.8 QStyleOption::OptionType）。
 * @details    数值与 Qt 逐位一致（qstyleoption.h:41-52）：前 16 项为
 *             普通选项类，SO_Complex 起为「复杂控件」族（0xf0000 起
 *             连续），SO_CustomBase=0xf00 / SO_ComplexCustomBase=0xf000000
 *             为自定义起点。X 侧承载于 XStyleOption::m_optionType。
 */
typedef enum XStyleOptionType
{
    XStyleOptionType_Default = 0,          /**< 默认（SO_Default）。 */
    XStyleOptionType_FocusRect = 1,        /**< 焦点矩形（SO_FocusRect）。 */
    XStyleOptionType_Button = 2,           /**< 按钮（SO_Button）。 */
    XStyleOptionType_Tab = 3,              /**< 页签（SO_Tab）。 */
    XStyleOptionType_MenuItem = 4,         /**< 菜单项（SO_MenuItem）。 */
    XStyleOptionType_Frame = 5,            /**< 边框（SO_Frame）。 */
    XStyleOptionType_ProgressBar = 6,      /**< 进度条（SO_ProgressBar）。 */
    XStyleOptionType_ToolBox = 7,          /**< 工具箱（SO_ToolBox）。 */
    XStyleOptionType_Header = 8,           /**< 表头（SO_Header）。 */
    XStyleOptionType_DockWidget = 9,       /**< 停靠窗（SO_DockWidget）。 */
    XStyleOptionType_ViewItem = 10,        /**< 条目视图项（SO_ViewItem）。 */
    XStyleOptionType_TabWidgetFrame = 11,  /**< 页签容器边框（SO_TabWidgetFrame）。 */
    XStyleOptionType_TabBarBase = 12,      /**< 页签条基座（SO_TabBarBase）。 */
    XStyleOptionType_RubberBand = 13,      /**< 橡皮筋（SO_RubberBand）。 */
    XStyleOptionType_ToolBar = 14,         /**< 工具栏（SO_ToolBar）。 */
    XStyleOptionType_GraphicsItem = 15,    /**< 图形项（SO_GraphicsItem）。 */
    XStyleOptionType_Complex = 0xf0000,    /**< 复杂控件基类（SO_Complex）。 */
    XStyleOptionType_Slider = 0xf0001,     /**< 滑块（SO_Slider）。 */
    XStyleOptionType_SpinBox = 0xf0002,    /**< 微调框（SO_SpinBox）。 */
    XStyleOptionType_ToolButton = 0xf0003, /**< 工具按钮（SO_ToolButton）。 */
    XStyleOptionType_ComboBox = 0xf0004,   /**< 组合框（SO_ComboBox）。 */
    XStyleOptionType_TitleBar = 0xf0005,   /**< 标题栏（SO_TitleBar）。 */
    XStyleOptionType_GroupBox = 0xf0006,   /**< 分组框（SO_GroupBox）。 */
    XStyleOptionType_SizeGrip = 0xf0007,   /**< 尺寸手柄（SO_SizeGrip）。 */
    XStyleOptionType_CustomBase = 0xf00,        /**< 自定义起点（SO_CustomBase）。 */
    XStyleOptionType_ComplexCustomBase = 0xf000000 /**< 复杂控件自定义起点（SO_ComplexCustomBase）。 */
} XStyleOptionType;

/**
 * @brief      字体度量（对标 Qt 6.8 QFontMetrics）。
 * @details    本仓库未建立 QFontMetrics 等价类（见 XWidget_fontMetrics
 *             文档），度量以值结构承载：行高/上伸/下伸/行距取自
 *             XPainter_textHeight/textAscent/textDescent，平均字宽与
 *             空格宽取自 XPainter_textWidth。
 */
typedef struct XStyleFontMetrics
{
    int m_height;           /**< 行高（对标 QFontMetrics::height）。 */
    int m_ascent;           /**< 上伸部（对标 ascent）。 */
    int m_descent;          /**< 下伸部（对标 descent）。 */
    int m_lineSpacing;      /**< 行间距（对标 lineSpacing）。 */
    int m_averageCharWidth; /**< 平均字符宽（对标 averageCharWidth）。 */
    int m_spaceWidth;       /**< 空格宽（对标 spaceWidth）。 */
} XStyleFontMetrics;

/**
 * @brief      边框特征位（对标 QStyleOptionFrame::FrameFeature）。
 */
typedef enum XStyleOptionFrameFeature
{
    XStyleOptionFrameFeature_None = 0x00,    /**< 无（None）。 */
    XStyleOptionFrameFeature_Flat = 0x01,    /**< 扁平（Flat）。 */
    XStyleOptionFrameFeature_Rounded = 0x02  /**< 圆角（Rounded）。 */
} XStyleOptionFrameFeature;

/**
 * @brief      边框形状（对标 QFrame::Shape；数值与 XFrameShape 一致）。
 */
typedef enum XStyleOptionFrameShape
{
    XStyleOptionFrameShape_NoFrame = 0x00,
    XStyleOptionFrameShape_Box = 0x01,
    XStyleOptionFrameShape_Panel = 0x02,
    XStyleOptionFrameShape_WinPanel = 0x03,
    XStyleOptionFrameShape_HLine = 0x04,
    XStyleOptionFrameShape_VLine = 0x05,
    XStyleOptionFrameShape_StyledPanel = 0x06
} XStyleOptionFrameShape;

/**
 * @brief      页签条形状（对标 QTabBar::Shape）。
 */
typedef enum XStyleOptionTabBarShape
{
    XStyleOptionTabBarShape_RoundedNorth = 0,
    XStyleOptionTabBarShape_RoundedSouth = 1,
    XStyleOptionTabBarShape_TriangularNorth = 2,
    XStyleOptionTabBarShape_TriangularSouth = 3
} XStyleOptionTabBarShape;

/**
 * @brief      尺寸手柄角位（对标 Qt::Corner）。
 */
typedef enum XStyleOptionCorner
{
    XStyleOptionCorner_TopLeft = 0x00,
    XStyleOptionCorner_TopRight = 0x01,
    XStyleOptionCorner_BottomLeft = 0x02,
    XStyleOptionCorner_BottomRight = 0x03
} XStyleOptionCorner;

/**
 * @brief      表头段位置（对标 QStyleOptionHeader::SectionPosition）。
 */
typedef enum XStyleOptionHeaderSectionPosition
{
    XStyleOptionHeaderSectionPosition_Beginning = 0,
    XStyleOptionHeaderSectionPosition_Middle = 1,
    XStyleOptionHeaderSectionPosition_End = 2,
    XStyleOptionHeaderSectionPosition_OnlyOneSection = 3
} XStyleOptionHeaderSectionPosition;

/**
 * @brief      表头相邻选中位置（对标 QStyleOptionHeader::SelectedPosition）。
 */
typedef enum XStyleOptionHeaderSelectedPosition
{
    XStyleOptionHeaderSelectedPosition_NotAdjacent = 0,
    XStyleOptionHeaderSelectedPosition_NextIsSelected = 1,
    XStyleOptionHeaderSelectedPosition_PreviousIsSelected = 2,
    XStyleOptionHeaderSelectedPosition_NextAndPreviousAreSelected = 3
} XStyleOptionHeaderSelectedPosition;

/**
 * @brief      表头排序指示（对标 QStyleOptionHeader::SortIndicator）。
 */
typedef enum XStyleOptionHeaderSortIndicator
{
    XStyleOptionHeaderSortIndicator_None = 0,
    XStyleOptionHeaderSortIndicator_SortUp = 1,
    XStyleOptionHeaderSortIndicator_SortDown = 2
} XStyleOptionHeaderSortIndicator;

/**
 * @brief      页签位置（对标 QStyleOptionTab::TabPosition）。
 */
typedef enum XStyleOptionTabPosition
{
    XStyleOptionTabPosition_Beginning = 0,
    XStyleOptionTabPosition_Middle = 1,
    XStyleOptionTabPosition_End = 2,
    XStyleOptionTabPosition_OnlyOneTab = 3,
    XStyleOptionTabPosition_Moving = 4
} XStyleOptionTabPosition;

/**
 * @brief      页签相邻选中位置（对标 QStyleOptionTab::SelectedPosition）。
 */
typedef enum XStyleOptionTabSelectedPosition
{
    XStyleOptionTabSelectedPosition_NotAdjacent = 0,
    XStyleOptionTabSelectedPosition_NextIsSelected = 1,
    XStyleOptionTabSelectedPosition_PreviousIsSelected = 2
} XStyleOptionTabSelectedPosition;

/**
 * @brief      页签角控件位（对标 QStyleOptionTab::CornerWidget）。
 */
typedef enum XStyleOptionTabCornerWidget
{
    XStyleOptionTabCornerWidget_None = 0x00,
    XStyleOptionTabCornerWidget_Left = 0x01,
    XStyleOptionTabCornerWidget_Right = 0x02
} XStyleOptionTabCornerWidget;

/**
 * @brief      页签特征位（对标 QStyleOptionTab::TabFeature）。
 */
typedef enum XStyleOptionTabFeature
{
    XStyleOptionTabFeature_None = 0x00,
    XStyleOptionTabFeature_HasFrame = 0x01
} XStyleOptionTabFeature;

/**
 * @brief      工具栏位置（对标 QStyleOptionToolBar::ToolBarPosition）。
 */
typedef enum XStyleOptionToolBarPosition
{
    XStyleOptionToolBarPosition_Beginning = 0,
    XStyleOptionToolBarPosition_Middle = 1,
    XStyleOptionToolBarPosition_End = 2,
    XStyleOptionToolBarPosition_OnlyOne = 3
} XStyleOptionToolBarPosition;

/**
 * @brief      工具栏特征位（对标 QStyleOptionToolBar::ToolBarFeature）。
 */
typedef enum XStyleOptionToolBarFeature
{
    XStyleOptionToolBarFeature_None = 0x0,
    XStyleOptionToolBarFeature_Movable = 0x1
} XStyleOptionToolBarFeature;

/**
 * @brief      菜单项类型（对标 QStyleOptionMenuItem::MenuItemType）。
 */
typedef enum XStyleOptionMenuItemType
{
    XStyleOptionMenuItemType_Normal = 0,
    XStyleOptionMenuItemType_DefaultItem = 1,
    XStyleOptionMenuItemType_Separator = 2,
    XStyleOptionMenuItemType_SubMenu = 3,
    XStyleOptionMenuItemType_Scroller = 4,
    XStyleOptionMenuItemType_TearOff = 5,
    XStyleOptionMenuItemType_Margin = 6,
    XStyleOptionMenuItemType_EmptyArea = 7
} XStyleOptionMenuItemType;

/**
 * @brief      菜单项勾选类型（对标 QStyleOptionMenuItem::CheckType）。
 */
typedef enum XStyleOptionMenuItemCheckType
{
    XStyleOptionMenuItemCheckType_NotCheckable = 0,
    XStyleOptionMenuItemCheckType_Exclusive = 1,
    XStyleOptionMenuItemCheckType_NonExclusive = 2
} XStyleOptionMenuItemCheckType;

/**
 * @brief      条目视图装饰位置（对标 QStyleOptionViewItem::Position）。
 */
typedef enum XStyleOptionViewItemPosition
{
    XStyleOptionViewItemPosition_Left = 0,
    XStyleOptionViewItemPosition_Right = 1,
    XStyleOptionViewItemPosition_Top = 2,
    XStyleOptionViewItemPosition_Bottom = 3
} XStyleOptionViewItemPosition;

/**
 * @brief      条目视图特性位（对标 QStyleOptionViewItem::ViewItemFeature）。
 */
typedef enum XStyleOptionViewItemFeature
{
    XStyleOptionViewItemFeature_None = 0x00,
    XStyleOptionViewItemFeature_WrapText = 0x01,
    XStyleOptionViewItemFeature_Alternate = 0x02,
    XStyleOptionViewItemFeature_HasCheckIndicator = 0x04,
    XStyleOptionViewItemFeature_HasDisplay = 0x08,
    XStyleOptionViewItemFeature_HasDecoration = 0x10
} XStyleOptionViewItemFeature;

/**
 * @brief      条目视图项位置（对标 QStyleOptionViewItem::ViewItemPosition）。
 */
typedef enum XStyleOptionViewItemViewItemPosition
{
    XStyleOptionViewItemViewItemPosition_Invalid = 0,
    XStyleOptionViewItemViewItemPosition_Beginning = 1,
    XStyleOptionViewItemViewItemPosition_Middle = 2,
    XStyleOptionViewItemViewItemPosition_End = 3,
    XStyleOptionViewItemViewItemPosition_OnlyOne = 4
} XStyleOptionViewItemViewItemPosition;

/**
 * @brief      工具箱页位置（对标 QStyleOptionToolBox::TabPosition）。
 */
typedef enum XStyleOptionToolBoxTabPosition
{
    XStyleOptionToolBoxTabPosition_Beginning = 0,
    XStyleOptionToolBoxTabPosition_Middle = 1,
    XStyleOptionToolBoxTabPosition_End = 2,
    XStyleOptionToolBoxTabPosition_OnlyOneTab = 3
} XStyleOptionToolBoxTabPosition;

/**
 * @brief      工具箱相邻选中位置（对标 QStyleOptionToolBox::SelectedPosition）。
 */
typedef enum XStyleOptionToolBoxSelectedPosition
{
    XStyleOptionToolBoxSelectedPosition_NotAdjacent = 0,
    XStyleOptionToolBoxSelectedPosition_NextIsSelected = 1,
    XStyleOptionToolBoxSelectedPosition_PreviousIsSelected = 2
} XStyleOptionToolBoxSelectedPosition;

/**
 * @brief      工具按钮特征位（对标 QStyleOptionToolButton::ToolButtonFeature）。
 */
typedef enum XStyleOptionToolButtonFeature
{
    XStyleOptionToolButtonFeature_None = 0x00,
    XStyleOptionToolButtonFeature_Arrow = 0x01,
    XStyleOptionToolButtonFeature_Menu = 0x04,
    XStyleOptionToolButtonFeature_MenuButtonPopup = 0x04,
    XStyleOptionToolButtonFeature_PopupDelay = 0x08,
    XStyleOptionToolButtonFeature_HasMenu = 0x10
} XStyleOptionToolButtonFeature;

/**
 * @brief      样式选项（对标 QStyleOption 核心字段）。
 * @details    [内存治理 2026-10-11] 成员按对齐降序重排（指针→4B 值组→
 *             uint16→bool/uint8/位域组），并收窄文档化小值域整型：
 *             m_version/m_type/m_direction/m_checkState/
 *             m_sliderTickPosition/m_spinStepEnabled/m_spinSymbols/
 *             m_checkState2/m_tabElideMode → uint8_t（值域均 ≤255，
 *             已核 .c 全部赋值/比较无越界无 -1 哨兵），m_tabIndex/
 *             m_decorationAlignment → uint16_t；m_progressValue 保留
 *             int（init 置 -1 哨兵），进度/滑块 min/max/value 保留
 *             int（外部原样值，值域不受限）。m_tabPosition 收 int8_t
 *             （非 uint8_t：承载 -1 未指定哨兵）。初始化一律走
 *             XStyleOption_init 的 XMemset 全量清零，无位置式初始化
 *             依赖（全库 grep 核实）。
 */
typedef struct XStyleOption
{
    XObject* m_styleObject; /**< 样式对象（对标 styleObject；QSS 匹配用，可空）。 */
    const char* m_text;   /**< 文本（借用；可空）。 */
    XIcon* m_icon;        /**< 条目图标（借用；可空）。 */
    /* ---- 通用控件字段 ---- */
    uint32_t m_state;     /**< XStyleStateFlag 位组合。 */
    XRect m_rect;         /**< 目标矩形（控件局部坐标）。 */
    XPalette m_palette;   /**< 调色板（按需填充）。 */
    uint32_t m_textColor; /**< 文本色（0=调色板决定）。 */
    XStyleFontMetrics m_fontMetrics; /**< 字体度量（对标 fontMetrics）。 */
    int32_t m_optionType; /**< 选项类型（对标 QStyleOption::type；
                             XStyleOptionType 值；Qt 的 OptionType 语义）。 */
    /* ---- 进度条 ---- */
    int m_progressMin;    /**< 进度最小值。 */
    int m_progressMax;    /**< 进度最大值。 */
    int m_progressValue;  /**< 进度当前值（-1=无值哨兵，保持有符号）。 */
    /* ---- 滚动条/滑块 ---- */
    int m_sliderMin;      /**< 滑块最小值。 */
    int m_sliderMax;      /**< 滑块最大值。 */
    int m_sliderValue;    /**< 滑块当前值。 */
    int m_sliderSingleStep; /**< 滑块单步。 */
    int m_sliderPageStep; /**< 滑块页步。 */
    int m_sliderTickInterval; /**< 刻度间隔值（0=自动，对标 tickInterval）。 */
    /* ---- 表盘（对标 QStyleOptionSlider 的 dial 子集） ---- */
    int m_notchSize;      /**< 刻度间隔值（对标 tickInterval）。 */
    int m_pageStep;       /**< 大刻度步长（对标 pageStep）。 */
    /* ---- 条目视图（对标 QStyleOptionViewItem 子集） ---- */
    uint32_t m_viewFeatures; /**< 条目特性位：0x1 勾选指示/0x2 装饰/0x4 文本。 */
    uint16_t m_tabIndex;       /**< 页签下标（≤65535；无 -1 哨兵）。 */
    uint16_t m_decorationAlignment; /**< 装饰对齐（XAlignment 位 ≤0x1ff）。 */
    uint8_t m_version;    /**< 版本（保留）。 */
    uint8_t m_type;       /**< 基元/控件/复杂控件元素枚举（≤255）。X 专有：
                             承载「本次绘制针对哪个元素」；Qt 无此概念，
                             Qt 的 OptionType 由 m_optionType 承载。 */
    uint8_t m_direction;  /**< 布局方向：0 左到右/1 右到左（对标 direction）。 */
    /* 标志位域组：无取址使用的 17 个 bool 连续合并为 1 位位域（每次绘制
     * 在栈上填充的临时选项对象，逐字段语义不变，仅存储位合拢；
     * m_checkable/m_checked/m_progressTextVisible/m_selected 按方案
     * 保持 bool 不合并，与本组相邻排布）。 */
    uint8_t m_flat : 1;   /**< 扁平（对标 flat）。 */
    uint8_t m_progressInverted : 1; /**< 进度反向。 */
    uint8_t m_tabSelected : 1; /**< 页签选中。 */
    uint8_t m_horizontal : 1; /**< 水平方向。 */
    uint8_t m_scrollSubLine : 1; /**< 绘制起始按钮（对标 SC_ScrollBarSubLine）。 */
    uint8_t m_scrollAddLine : 1; /**< 绘制结束按钮（对标 SC_ScrollBarAddLine）。 */
    uint8_t m_scrollActiveSub : 1; /**< 按下按钮（对标 activeSubControls 非空）。 */
    uint8_t m_mouseOver : 1; /**< 悬停（冗余便捷）。 */
    uint8_t m_spinFrame : 1; /**< 是否有边框（对标 frame）。 */
    uint8_t m_spinActiveUp : 1; /**< 上按钮为活动子控件。 */
    uint8_t m_spinActiveDown : 1; /**< 下按钮为活动子控件。 */
    uint8_t m_dialWrapping : 1; /**< 环绕（对标 dialWrapping）。 */
    uint8_t m_notchesVisible : 1; /**< 刻度可见（对标 subControls SC_DialTickmarks）。 */
    uint8_t m_closable : 1; /**< 可关闭。 */
    uint8_t m_movable : 1; /**< 可移动。 */
    uint8_t m_floatable : 1; /**< 可浮动。 */
    uint8_t m_showDecorationSelected : 1; /**< 装饰随选中高亮。 */
    bool m_checkable;     /**< 可勾选（GroupBox/按钮）。 */
    bool m_checked;       /**< 已勾选。 */
    /* ---- 按钮/复选/单选 ---- */
    uint8_t m_checkState; /**< 0 未选/1 部分/2 选中（对标 checkState；
                               另被复用传箭头方向 0-3，见 XCommonStyle）。 */
    bool m_progressTextVisible; /**< 进度文本可见。 */
    /* ---- 页签 ---- */
    int8_t  m_tabPosition;    /**< 页签位置（0 上/1 下/2 左/3 右；-1=未指定哨兵，
                                  故保留有符号：XCommonStyle 的 TabBarTabVSpace
                                  以 >=2 判 Triangular，-1 必须落回「其余 2」，
                                  收 uint8_t 会回绕成 255 而误判为 Triangular）。 */
    uint8_t m_sliderTickPosition; /**< 刻度位置：0 无/1 上/2 下/3 双侧。 */
    /* ---- 菜单/表头 ---- */
    bool m_selected;      /**< 选中。 */
    /* ---- 微调框（对标 QStyleOptionSpinBox） ---- */
    uint8_t m_spinStepEnabled; /**< 步进使能位：0x1 上/0x2 下（对标 stepEnabled）。 */
    uint8_t m_spinSymbols;    /**< 按钮符号：0 上下箭头/1 加减号/2 无按钮（对标 buttonSymbols）。 */
    uint8_t m_checkState2;    /**< 条目勾选状态（0 未选/1 部分/2 选中）。 */
    uint8_t m_tabElideMode;   /**< 页签省略模式（§8.0g11：0 无/1 右省略/2 左
                               省略/3 中省略，对标 Qt::ElideRight 等）。 */
} XStyleOption;

/**
 * @brief 复杂控件选项（对标 QStyleOptionComplex：subControls/activeSubControls）。
 */
typedef struct XStyleOptionComplex
{
    XStyleOption m_base;          /**< 基类选项；必须是第一个。 */
    uint32_t m_subControls;       /**< 子控件位组合（对标 subControls）。 */
    uint32_t m_activeSubControls; /**< 活动子控件位组合（对标 activeSubControls）。 */
} XStyleOptionComplex;

/**
 * @brief 按钮选项（对标 QStyleOptionButton）。
 */
typedef struct XStyleOptionButton
{
    XStyleOption m_base;          /**< 基类选项；必须是第一个。 */
    uint32_t m_features;          /**< 特性位：0x1 默认/0x2 扁平/0x4 有菜单/0x8 自动默认。 */
    XIcon* m_icon;                /**< 按钮图标（借用；可空）。 */
    XSize m_iconSize;             /**< 图标尺寸。 */
} XStyleOptionButton;

/**
 * @brief 进度条选项（对标 QStyleOptionProgressBar）。
 */
typedef struct XStyleOptionProgressBar
{
    XStyleOption m_base;          /**< 基类选项；必须是第一个。 */
    int m_orientation;            /**< 方向：0 水平/1 垂直（对标 orientation）。 */
    bool m_invertedAppearance;    /**< 反向外观（对标 invertedAppearance）。 */
    bool m_bottomToTop;           /**< 自底向上（对标 bottomToTop）。 */
    const char* m_text;           /**< 文本（借用；可空；对标 text）。 */
    uint32_t m_textAlignment;     /**< 文本对齐（XAlignment 位；对标 textAlignment）。 */
} XStyleOptionProgressBar;

/**
 * @brief 滑块选项（对标 QStyleOptionSlider）。
 */
typedef struct XStyleOptionSlider
{
    XStyleOption m_base;          /**< 基类选项；必须是第一个。 */
    int m_orientation;            /**< 方向：0 水平/1 垂直（对标 orientation）。 */
    uint32_t m_subControls;       /**< 子控件位组合（对标 subControls）。 */
    uint32_t m_activeSubControls; /**< 活动子控件位组合（对标 activeSubControls）。 */
    bool m_upsideDown;            /**< 反向（对标 upsideDown）。 */
    int m_sliderPosition;         /**< 滑块像素位置（对标 sliderPosition）。 */
    float m_notchTarget;          /**< 刻度吸附目标值（对标 notchTarget）。 */
    uint32_t m_keyboardModifiers; /**< 键盘修饰键位组合（对标 keyboardModifiers）。 */
} XStyleOptionSlider;

/**
 * @brief 微调框选项（对标 QStyleOptionSpinBox : QStyleOptionComplex）。
 */
typedef struct XStyleOptionSpinBox
{
    XStyleOptionComplex m_base;   /**< 基类选项；必须是第一个。 */
} XStyleOptionSpinBox;

/**
 * @brief 组合框选项（对标 QStyleOptionComboBox : QStyleOptionComplex）。
 */
typedef struct XStyleOptionComboBox
{
    XStyleOptionComplex m_base;   /**< 基类选项；必须是第一个。 */
    bool m_editable;              /**< 可编辑（对标 editable）。 */
    bool m_popupOpen;             /**< 弹出打开（对标 popupOpen）。 */
    XIcon* m_currentIcon;         /**< 当前项图标（借用；可空）。 */
    XSize m_iconSize;             /**< 图标尺寸。 */
    XRect m_popupRect;            /**< 弹出矩形（对标 popupRect）。 */
    bool m_frame;                 /**< 有边框（对标 frame）。 */
    const char* m_currentText;    /**< 当前文本（借用；可空；对标 currentText）。 */
    uint32_t m_textAlignment;     /**< 文本对齐（XAlignment 位；对标 textAlignment）。 */
} XStyleOptionComboBox;

/**
 * @brief 工具按钮选项（对标 QStyleOptionToolButton : QStyleOptionComplex）。
 */
typedef struct XStyleOptionToolButton
{
    XStyleOptionComplex m_base;   /**< 基类选项；必须是第一个。 */
    uint32_t m_features;          /**< 特性位（对标 features；XStyleOptionToolButtonFeature 值）。 */
    int m_toolButtonStyle;        /**< 样式：0 仅图标/1 仅文本/2 文本旁图标/3 文本下图标/4 跟随风格。 */
    int m_arrowType;              /**< 箭头类型：0 无/1 上/2 下/3 左/4 右。 */
    bool m_defaultButton;         /**< 默认按钮（对标 defaultButton）。 */
    XIcon* m_icon;                /**< 工具按钮图标（借用；可空）。 */
    XSize m_iconSize;             /**< 图标尺寸。 */
    const char* m_text;           /**< 文本（借用；可空；对标 text）。 */
    XPoint m_pos;                 /**< 位置（对标 pos）。 */
    const XFont* m_font;          /**< 字体（借用；可空；对标 font）。 */
} XStyleOptionToolButton;

/**
 * @brief 标题栏选项（对标 QStyleOptionTitleBar : QStyleOptionComplex）。
 * @details 由窗口装饰层（XWindowDecoration）在无窗口管理器环境下组装，
 *          经 CC_TitleBar 交样式绘制；标题文本/窗口图标复用基类的
 *          m_text/m_icon 借用字段，活动子控件位表达按住中的按钮。
 */
typedef struct XStyleOptionTitleBar
{
    XStyleOptionComplex m_base;   /**< 基类选项；必须是第一个。 */
    uint32_t m_titleBarState;     /**< 窗口状态位（XWindowState 位组合；
                                       仅承载语义，样式按 m_state 绘制）。 */
    uint32_t m_titleBarFlags;     /**< 窗口 flags（XWindowType 位组合；
                                       仅承载语义，按钮集合由 m_subControls
                                       表达）。 */
} XStyleOptionTitleBar;

/* ---- QStyleOption 族补齐：以下 14 个结构体对标 Qt 6.8 qstyleoption.h
        的对应类（A1-A5/A7-A15 档 1 缺失项）。各结构体首成员恒为
        XStyleOption/XStyleOptionComplex m_base（对标 Qt 的继承基类），
        派生字段一律为**值语义**（Qt 的 QString/QIcon/QFont/QBrush 在
        X 侧分别以 const char*（借用）/XIcon*（借用）/const XFont*（借用）
        /uint32_t 颜色承载），避免引入拥有型字段带来的析构义务。 ---- */

/**
 * @brief 焦点矩形选项（对标 QStyleOptionFocusRect）。
 */
typedef struct XStyleOptionFocusRect
{
    XStyleOption m_base;   /**< 基类选项；必须是第一个。 */
    uint32_t m_backgroundColor; /**< 背景色（ARGB32；对标 backgroundColor）。 */
} XStyleOptionFocusRect;

/**
 * @brief 边框选项（对标 QStyleOptionFrame）。
 */
typedef struct XStyleOptionFrame
{
    XStyleOption m_base;   /**< 基类选项；必须是第一个。 */
    int m_lineWidth;        /**< 边框线宽（对标 lineWidth）。 */
    int m_midLineWidth;     /**< 中线宽（对标 midLineWidth）。 */
    uint32_t m_features;    /**< 特征位（XStyleOptionFrameFeature 值）。 */
    int m_frameShape;       /**< 边框形状（XStyleOptionFrameShape 值）。 */
} XStyleOptionFrame;

/**
 * @brief 页签容器边框选项（对标 QStyleOptionTabWidgetFrame）。
 */
typedef struct XStyleOptionTabWidgetFrame
{
    XStyleOption m_base;   /**< 基类选项；必须是第一个。 */
    int m_lineWidth;        /**< 边框线宽（对标 lineWidth）。 */
    int m_midLineWidth;     /**< 中线宽（对标 midLineWidth）。 */
    int m_shape;            /**< 页签形状（XStyleOptionTabBarShape 值；对标 shape）。 */
    XSize m_tabBarSize;     /**< 页签条尺寸（对标 tabBarSize）。 */
    XSize m_rightCornerWidgetSize; /**< 右角控件尺寸（对标 rightCornerWidgetSize）。 */
    XSize m_leftCornerWidgetSize;  /**< 左角控件尺寸（对标 leftCornerWidgetSize）。 */
    XRect m_tabBarRect;     /**< 页签条矩形（对标 tabBarRect）。 */
    XRect m_selectedTabRect; /**< 选中页签矩形（对标 selectedTabRect）。 */
} XStyleOptionTabWidgetFrame;

/**
 * @brief 页签条基座选项（对标 QStyleOptionTabBarBase）。
 */
typedef struct XStyleOptionTabBarBase
{
    XStyleOption m_base;   /**< 基类选项；必须是第一个。 */
    int m_shape;            /**< 页签形状（XStyleOptionTabBarShape 值；对标 shape）。 */
    XRect m_tabBarRect;     /**< 页签条矩形（对标 tabBarRect）。 */
    XRect m_selectedTabRect; /**< 选中页签矩形（对标 selectedTabRect）。 */
    bool m_documentMode;    /**< 文档模式（对标 documentMode）。 */
} XStyleOptionTabBarBase;

/**
 * @brief 表头选项（对标 QStyleOptionHeader）。
 */
typedef struct XStyleOptionHeader
{
    XStyleOption m_base;   /**< 基类选项；必须是第一个。 */
    int m_section;          /**< 段号（对标 section）。 */
    const char* m_text;     /**< 段文本（借用；可空；对标 text）。 */
    uint32_t m_textAlignment; /**< 文本对齐（XAlignment 位；对标 textAlignment）。 */
    XIcon* m_icon;          /**< 段图标（借用；可空；对标 icon）。 */
    uint32_t m_iconAlignment; /**< 图标对齐（XAlignment 位；对标 iconAlignment）。 */
    int m_position;         /**< 段位置（XStyleOptionHeaderSectionPosition 值）。 */
    int m_selectedPosition; /**< 相邻选中位置（XStyleOptionHeaderSelectedPosition 值）。 */
    int m_sortIndicator;    /**< 排序指示（XStyleOptionHeaderSortIndicator 值）。 */
    int m_orientation;      /**< 方向：0 水平/1 垂直（对标 orientation）。 */
} XStyleOptionHeader;

/**
 * @brief 页签选项（对标 QStyleOptionTab）。
 */
typedef struct XStyleOptionTab
{
    XStyleOption m_base;   /**< 基类选项；必须是第一个。 */
    int m_shape;            /**< 页签形状（XStyleOptionTabBarShape 值；对标 shape）。 */
    const char* m_text;     /**< 页签文本（借用；可空；对标 text）。 */
    XIcon* m_icon;          /**< 页签图标（借用；可空；对标 icon）。 */
    int m_row;              /**< 行号（对标 row）。 */
    int m_position;         /**< 页签位置（XStyleOptionTabPosition 值）。 */
    int m_selectedPosition; /**< 相邻选中位置（XStyleOptionTabSelectedPosition 值）。 */
    uint32_t m_cornerWidgets; /**< 角控件位（XStyleOptionTabCornerWidget 值）。 */
    XSize m_iconSize;       /**< 图标尺寸（对标 iconSize）。 */
    bool m_documentMode;    /**< 文档模式（对标 documentMode）。 */
    XSize m_leftButtonSize; /**< 左按钮尺寸（对标 leftButtonSize）。 */
    XSize m_rightButtonSize;/**< 右按钮尺寸（对标 rightButtonSize）。 */
    uint32_t m_features;    /**< 特征位（XStyleOptionTabFeature 值）。 */
    int m_tabIndex;         /**< 页签下标（-1=未指定；对标 tabIndex）。 */
} XStyleOptionTab;

/**
 * @brief 工具栏选项（对标 QStyleOptionToolBar）。
 */
typedef struct XStyleOptionToolBar
{
    XStyleOption m_base;   /**< 基类选项；必须是第一个。 */
    int m_positionOfLine;   /**< 工具栏行位置（XStyleOptionToolBarPosition 值）。 */
    int m_positionWithinLine; /**< 行内位置（XStyleOptionToolBarPosition 值）。 */
    uint32_t m_toolBarArea; /**< 停靠区域位（对标 toolBarArea）。 */
    uint32_t m_features;    /**< 特征位（XStyleOptionToolBarFeature 值）。 */
    int m_lineWidth;        /**< 边框线宽（对标 lineWidth）。 */
    int m_midLineWidth;     /**< 中线宽（对标 midLineWidth）。 */
} XStyleOptionToolBar;

/**
 * @brief 菜单项选项（对标 QStyleOptionMenuItem）。
 */
typedef struct XStyleOptionMenuItem
{
    XStyleOption m_base;   /**< 基类选项；必须是第一个。 */
    int m_menuItemType;     /**< 菜单项类型（XStyleOptionMenuItemType 值）。 */
    int m_checkType;        /**< 勾选类型（XStyleOptionMenuItemCheckType 值）。 */
    bool m_checked;         /**< 已勾选（对标 checked）。 */
    bool m_menuHasCheckableItems; /**< 菜单含可勾选项（对标 menuHasCheckableItems）。 */
    XRect m_menuRect;       /**< 菜单矩形（对标 menuRect）。 */
    const char* m_text;     /**< 文本（借用；可空；对标 text）。 */
    XIcon* m_icon;          /**< 图标（借用；可空；对标 icon）。 */
    int m_maxIconWidth;     /**< 图标最大宽（对标 maxIconWidth）。 */
    int m_reservedShortcutWidth; /**< 快捷键预留宽（对标 reservedShortcutWidth）。 */
    const XFont* m_font;    /**< 字体（借用；可空；对标 font）。 */
} XStyleOptionMenuItem;

/**
 * @brief 停靠窗选项（对标 QStyleOptionDockWidget）。
 */
typedef struct XStyleOptionDockWidget
{
    XStyleOption m_base;   /**< 基类选项；必须是第一个。 */
    const char* m_title;    /**< 标题（借用；可空；对标 title）。 */
    bool m_closable;        /**< 可关闭（对标 closable）。 */
    bool m_movable;         /**< 可移动（对标 movable）。 */
    bool m_floatable;       /**< 可浮动（对标 floatable）。 */
    bool m_verticalTitleBar;/**< 垂直标题栏（对标 verticalTitleBar）。 */
} XStyleOptionDockWidget;

/**
 * @brief 条目视图项选项（对标 QStyleOptionViewItem）。
 */
typedef struct XStyleOptionViewItem
{
    XStyleOption m_base;   /**< 基类选项；必须是第一个。 */
    uint32_t m_displayAlignment;  /**< 文本对齐（XAlignment 位；对标 displayAlignment）。 */
    uint32_t m_decorationAlignment; /**< 装饰对齐（XAlignment 位；对标 decorationAlignment）。 */
    uint8_t m_textElideMode; /**< 文本省略模式（对标 textElideMode）。 */
    int m_decorationPosition; /**< 装饰位置（XStyleOptionViewItemPosition 值）。 */
    XSize m_decorationSize; /**< 装饰尺寸（对标 decorationSize）。 */
    const XFont* m_font;    /**< 字体（借用；可空；对标 font）。 */
    bool m_showDecorationSelected; /**< 装饰随选中高亮（对标 showDecorationSelected）。 */
    uint32_t m_features;    /**< 特性位（XStyleOptionViewItemFeature 值）。 */
    const XObject* m_widget; /**< 源控件（借用；可空；对标 widget）。 */
    uint8_t m_checkState;   /**< 勾选状态（0 未选/1 部分/2 选中；对标 checkState）。 */
    XIcon* m_icon;          /**< 图标（借用；可空；对标 icon）。 */
    const char* m_text;     /**< 文本（借用；可空；对标 text）。 */
    int m_viewItemPosition; /**< 条目位置（XStyleOptionViewItemViewItemPosition 值）。 */
    uint32_t m_backgroundColor; /**< 背景色（ARGB32；对标 backgroundBrush 纯色）。 */
} XStyleOptionViewItem;

/**
 * @brief 工具箱选项（对标 QStyleOptionToolBox）。
 */
typedef struct XStyleOptionToolBox
{
    XStyleOption m_base;   /**< 基类选项；必须是第一个。 */
    const char* m_text;     /**< 页文本（借用；可空；对标 text）。 */
    XIcon* m_icon;          /**< 页图标（借用；可空；对标 icon）。 */
    int m_position;         /**< 页位置（XStyleOptionToolBoxTabPosition 值）。 */
    int m_selectedPosition; /**< 相邻选中位置（XStyleOptionToolBoxSelectedPosition 值）。 */
} XStyleOptionToolBox;

/**
 * @brief 橡皮筋选项（对标 QStyleOptionRubberBand）。
 */
typedef struct XStyleOptionRubberBand
{
    XStyleOption m_base;   /**< 基类选项；必须是第一个。 */
    int m_shape;            /**< 形状（XRubberBandShape 值；对标 shape）。 */
    bool m_opaque;          /**< 不透明（对标 opaque）。 */
} XStyleOptionRubberBand;

/**
 * @brief 分组框选项（对标 QStyleOptionGroupBox : QStyleOptionComplex）。
 */
typedef struct XStyleOptionGroupBox
{
    XStyleOptionComplex m_base; /**< 基类选项；必须是第一个。 */
    uint32_t m_features;    /**< 边框特征位（XStyleOptionFrameFeature 值）。 */
    const char* m_text;     /**< 标题文本（借用；可空；对标 text）。 */
    uint32_t m_textAlignment; /**< 标题对齐（XAlignment 位；对标 textAlignment）。 */
    uint32_t m_textColor;   /**< 标题色（ARGB32；对标 textColor）。 */
    int m_lineWidth;        /**< 边框线宽（对标 lineWidth）。 */
    int m_midLineWidth;     /**< 中线宽（对标 midLineWidth）。 */
} XStyleOptionGroupBox;

/**
 * @brief 尺寸手柄选项（对标 QStyleOptionSizeGrip : QStyleOptionComplex）。
 */
typedef struct XStyleOptionSizeGrip
{
    XStyleOptionComplex m_base; /**< 基类选项；必须是第一个。 */
    int m_corner;           /**< 角位（XStyleOptionCorner 值；对标 corner）。 */
} XStyleOptionSizeGrip;

/**
 * @brief 初始化样式选项（QStyleOption 等价默认）。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（XStylePrimitiveElement/XStyleControlElement/
 *             XStyleComplexControl）。
 * @return 无返回值。
 */
void XStyleOption_init(XStyleOption* option, int type);

/**
 * @brief 初始化复杂控件选项。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 复杂控件枚举。
 * @return 无返回值。
 */
void XStyleOptionComplex_init(XStyleOptionComplex* option, int type);

/**
 * @brief 初始化按钮选项。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（通常 XStyleCE_PushButton）。
 * @return 无返回值。
 */
void XStyleOptionButton_init(XStyleOptionButton* option, int type);

/**
 * @brief 初始化进度条选项。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（通常 XStyleCE_ProgressBar）。
 * @return 无返回值。
 */
void XStyleOptionProgressBar_init(XStyleOptionProgressBar* option, int type);

/**
 * @brief 初始化滑块选项。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 复杂控件枚举（通常 XStyleCC_Slider）。
 * @return 无返回值。
 */
void XStyleOptionSlider_init(XStyleOptionSlider* option, int type);

/**
 * @brief 初始化微调框选项。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 复杂控件枚举（通常 XStyleCC_SpinBox）。
 * @return 无返回值。
 */
void XStyleOptionSpinBox_init(XStyleOptionSpinBox* option, int type);

/**
 * @brief 初始化组合框选项。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 复杂控件枚举（通常 XStyleCC_ComboBox）。
 * @return 无返回值。
 */
void XStyleOptionComboBox_init(XStyleOptionComboBox* option, int type);

/**
 * @brief 初始化工具按钮选项。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 复杂控件枚举（通常 XStyleCC_ToolButton）。
 * @return 无返回值。
 */
void XStyleOptionToolButton_init(XStyleOptionToolButton* option, int type);

/**
 * @brief 初始化标题栏选项。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 标题栏枚举（通常 XStyleCC_TitleBar）。
 * @return 无返回值。
 */
void XStyleOptionTitleBar_init(XStyleOptionTitleBar* option, int type);

/**
 * @brief 初始化焦点矩形选项（对标 QStyleOptionFocusRect 构造）。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（对标 Qt 的 type 形参；Qt 默认 SO_FocusRect）。
 * @return 无返回值。
 */
void XStyleOptionFocusRect_init(XStyleOptionFocusRect* option, int type);

/**
 * @brief 初始化边框选项（对标 QStyleOptionFrame 构造）。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（对标 Qt 的 type 形参）。
 * @return 无返回值。
 */
void XStyleOptionFrame_init(XStyleOptionFrame* option, int type);

/**
 * @brief 初始化页签容器边框选项（对标 QStyleOptionTabWidgetFrame 构造）。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（对标 Qt 的 type 形参）。
 * @return 无返回值。
 */
void XStyleOptionTabWidgetFrame_init(XStyleOptionTabWidgetFrame* option, int type);

/**
 * @brief 初始化页签条基座选项（对标 QStyleOptionTabBarBase 构造）。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（对标 Qt 的 type 形参）。
 * @return 无返回值。
 */
void XStyleOptionTabBarBase_init(XStyleOptionTabBarBase* option, int type);

/**
 * @brief 初始化表头选项（对标 QStyleOptionHeader 构造）。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（对标 Qt 的 type 形参）。
 * @return 无返回值。
 */
void XStyleOptionHeader_init(XStyleOptionHeader* option, int type);

/**
 * @brief 初始化页签选项（对标 QStyleOptionTab 构造）。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（对标 Qt 的 type 形参）。
 * @return 无返回值。
 */
void XStyleOptionTab_init(XStyleOptionTab* option, int type);

/**
 * @brief 初始化工具栏选项（对标 QStyleOptionToolBar 构造）。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（对标 Qt 的 type 形参）。
 * @return 无返回值。
 */
void XStyleOptionToolBar_init(XStyleOptionToolBar* option, int type);

/**
 * @brief 初始化菜单项选项（对标 QStyleOptionMenuItem 构造）。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（对标 Qt 的 type 形参）。
 * @return 无返回值。
 */
void XStyleOptionMenuItem_init(XStyleOptionMenuItem* option, int type);

/**
 * @brief 初始化停靠窗选项（对标 QStyleOptionDockWidget 构造）。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（对标 Qt 的 type 形参）。
 * @return 无返回值。
 */
void XStyleOptionDockWidget_init(XStyleOptionDockWidget* option, int type);

/**
 * @brief 初始化条目视图项选项（对标 QStyleOptionViewItem 构造）。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（对标 Qt 的 type 形参）。
 * @return 无返回值。
 */
void XStyleOptionViewItem_init(XStyleOptionViewItem* option, int type);

/**
 * @brief 初始化工具箱选项（对标 QStyleOptionToolBox 构造）。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（对标 Qt 的 type 形参）。
 * @return 无返回值。
 */
void XStyleOptionToolBox_init(XStyleOptionToolBox* option, int type);

/**
 * @brief 初始化橡皮筋选项（对标 QStyleOptionRubberBand 构造）。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（对标 Qt 的 type 形参）。
 * @return 无返回值。
 */
void XStyleOptionRubberBand_init(XStyleOptionRubberBand* option, int type);

/**
 * @brief 初始化分组框选项（对标 QStyleOptionGroupBox 构造）。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（对标 Qt 的 type 形参）。
 * @return 无返回值。
 */
void XStyleOptionGroupBox_init(XStyleOptionGroupBox* option, int type);

/**
 * @brief 初始化尺寸手柄选项（对标 QStyleOptionSizeGrip 构造）。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（对标 Qt 的 type 形参）。
 * @return 无返回值。
 */
void XStyleOptionSizeGrip_init(XStyleOptionSizeGrip* option, int type);

/**
 * @brief 按控件填充样式选项（对标 QStyleOption::initFrom）。
 *
 *        Qt 6.8 的 initFrom（qstyleoption.cpp:38-72）按控件填 state /
 *        direction / rect / palette / fontMetrics / styleObject 六项；
 *        X 侧同口径实现（XStyleStateFlag 覆盖 Enabled/HasFocus/
 *        MouseOver/Active/Window 五位——Qt 另有 KeyboardFocusChange 与
 *        Small/Mini，X 侧无对应判据，未纳入）。
 *
 * @param option 目标选项指针，不能为空。
 * @param widget 源控件（可空；NULL 时仅按默认值初始化）。
 * @return 无返回值。
 */
void XStyleOption_initFrom(XStyleOption* option, const XWidget* widget);

/**
 * @brief 读取选项类型（对标 QStyleOption::type）。
 *
 * @param option 目标选项指针；可为 NULL。
 * @return XStyleOptionType 值；option 为 NULL 时返回
 *         XStyleOptionType_Default。
 */
int XStyleOption_optionType(const XStyleOption* option);

/**
 * @brief 写入选项类型（对标 QStyleOption::type 字段）。
 *
 * @param option 目标选项指针；可为 NULL（无操作）。
 * @param optionType XStyleOptionType 值。
 * @return 无返回值。
 */
void XStyleOption_setOptionType(XStyleOption* option, int optionType);

/**
 * @brief 判定选项是否为复杂控件选项（对标 Qt 的 qstyleoption_cast
 *        &lt;QStyleOptionComplex&gt; 守卫）。
 *
 *        Qt 6.8 的判定式（qstyleoption.h:661-672）：`opt->type ==
 *        SO_Complex || opt->type > SO_Complex`。X 侧按同一口径以
 *        m_optionType 判定——普通选项（0..15）与自定义起点（0xf00）
 *        均小于 SO_Complex（0xf0000），复杂族（0xf0000 起连续）为真。
 *
 * @param option 目标选项指针；可为 NULL。
 * @return true=复杂控件选项；false=普通选项或空指针。
 */
bool XStyleOption_isComplex(const XStyleOption* option);

/**
 * @brief 读取字体度量（对标 QStyleOption::fontMetrics）。
 *
 * @param option 目标选项指针；可为 NULL。
 * @return 字体度量值；option 为 NULL 时返回全零度量。
 */
XStyleFontMetrics XStyleOption_fontMetrics(const XStyleOption* option);

/**
 * @brief 写入字体度量（对标 QStyleOption::fontMetrics 字段）。
 *
 * @param option 目标选项指针；可为 NULL（无操作）。
 * @param metrics 字体度量值。
 * @return 无返回值。
 */
void XStyleOption_setFontMetrics(XStyleOption* option,
                                  const XStyleFontMetrics* metrics);

#endif /* XSTYLE_ON */
#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XSTYLEOPTION_H */
