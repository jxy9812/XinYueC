#ifndef XSTYLEOPTION_H
#define XSTYLEOPTION_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XGeometry.h"
#include "XPalette.h"

#if XSTYLE_ON

/**
 * @brief      状态位（对标 Qt 6.8 QStyle::StateFlag）。
 */
typedef enum XStyleStateFlag
{
    XStyleState_None =         0x00000000, /**< 无状态。 */
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
    XStyleState_Editing =      0x00200000, /**< 编辑中（对标 State_Editing）。 */
    XStyleState_KeyboardFocusChange = 0x00400000, /**< 键盘焦点变化（对标 State_KeyboardFocusChange）。 */
    XStyleState_Sibling =      0x00800000  /**< 有兄弟项（对标 State_Sibling）。 */
} XStyleStateFlag;

/**
 * @brief      基元元素（对标 Qt 6.8 QStyle::PrimitiveElement 子集）。
 */
typedef enum XStylePrimitiveElement
{
    XStylePE_Frame = 0,          /**< 普通边框（对标 PE_Frame）。 */
    XStylePE_FrameFocusRect,     /**< 焦点虚线框（对标 PE_FrameFocusRect）。 */
    XStylePE_FrameLineEdit,      /**< 输入框边框（对标 PE_FrameLineEdit）。 */
    XStylePE_FrameGroupBox,      /**< 分组框边框（对标 PE_FrameGroupBox）。 */
    XStylePE_FrameTabWidget,     /**< 页签容器边框（对标 PE_FrameTabWidget）。 */
    XStylePE_FrameButtonBevel,   /**< 按钮斜面边框（对标 PE_FrameButtonBevel）。 */
    XStylePE_FrameTabBarBase,    /**< 页签条基座（对标 PE_FrameTabBarBase）。 */
    XStylePE_PanelButtonCommand, /**< 命令按钮面板（对标 PE_PanelButtonCommand）。 */
    XStylePE_PanelButtonBevel,   /**< 斜面按钮面板（对标 PE_PanelButtonBevel）。 */
    XStylePE_PanelButtonTool,    /**< 工具按钮面板（对标 PE_PanelButtonTool）。 */
    XStylePE_PanelMenuBar,       /**< 菜单栏面板（对标 PE_PanelMenuBar）。 */
    XStylePE_PanelMenu,          /**< 弹出菜单面板（对标 PE_PanelMenu）。 */
    XStylePE_PanelToolBar,       /**< 工具栏面板（对标 PE_PanelToolBar）。 */
    XStylePE_PanelLineEdit,      /**< 输入框面板（对标 PE_PanelLineEdit）。 */
    XStylePE_IndicatorArrowDown, /**< 下箭头（对标 PE_IndicatorArrowDown）。 */
    XStylePE_IndicatorArrowUp,   /**< 上箭头（对标 PE_IndicatorArrowUp）。 */
    XStylePE_IndicatorArrowLeft, /**< 左箭头（对标 PE_IndicatorArrowLeft）。 */
    XStylePE_IndicatorArrowRight, /**< 右箭头（对标 PE_IndicatorArrowRight）。 */
    XStylePE_IndicatorCheckBox,  /**< 复选框（对标 PE_IndicatorCheckBox）。 */
    XStylePE_IndicatorRadioButton, /**< 单选钮（对标 PE_IndicatorRadioButton）。 */
    XStylePE_IndicatorSpinUp,    /**< 微调上按钮（对标 PE_IndicatorSpinUp）。 */
    XStylePE_IndicatorSpinDown,  /**< 微调下按钮（对标 PE_IndicatorSpinDown）。 */
    XStylePE_IndicatorSpinPlus,  /**< 微调加号（对标 PE_IndicatorSpinPlus）。 */
    XStylePE_IndicatorSpinMinus, /**< 微调减号（对标 PE_IndicatorSpinMinus）。 */
    XStylePE_IndicatorProgressChunk, /**< 进度块（对标 PE_IndicatorProgressChunk）。 */
    XStylePE_IndicatorToolBarHandle, /**< 工具栏把手（对标 PE_IndicatorToolBarHandle）。 */
    XStylePE_IndicatorToolBarSeparator, /**< 工具栏分隔线（对标 PE_IndicatorToolBarSeparator）。 */
    XStylePE_PanelTipLabel,      /**< 提示标签面板（对标 PE_PanelTipLabel）。 */
    XStylePE_IndicatorBranch,    /**< 树分支指示（对标 PE_IndicatorBranch）。 */
    XStylePE_PanelItemViewItem,  /**< 条目视图项（对标 PE_PanelItemViewItem）。 */
    XStylePE_IndicatorItemViewItemCheck /**< 条目勾选框（对标 PE_IndicatorItemViewItemCheck）。 */
} XStylePrimitiveElement;

/**
 * @brief      控件元素（对标 Qt 6.8 QStyle::ControlElement 子集）。
 */
typedef enum XStyleControlElement
{
    XStyleCE_PushButton = 0,     /**< 推按钮（对标 CE_PushButton）。 */
    XStyleCE_PushButtonBevel,    /**< 推按钮斜面（对标 CE_PushButtonBevel）。 */
    XStyleCE_PushButtonLabel,    /**< 推按钮标签（对标 CE_PushButtonLabel）。 */
    XStyleCE_CheckBox,           /**< 复选框（对标 CE_CheckBox）。 */
    XStyleCE_CheckBoxLabel,      /**< 复选框标签（对标 CE_CheckBoxLabel）。 */
    XStyleCE_RadioButton,        /**< 单选钮（对标 CE_RadioButton）。 */
    XStyleCE_RadioButtonLabel,   /**< 单选钮标签（对标 CE_RadioButtonLabel）。 */
    XStyleCE_TabBarTab,          /**< 页签（对标 CE_TabBarTab）。 */
    XStyleCE_TabBarTabShape,     /**< 页签形状（对标 CE_TabBarTabShape）。 */
    XStyleCE_TabBarTabLabel,     /**< 页签标签（对标 CE_TabBarTabLabel）。 */
    XStyleCE_ProgressBar,        /**< 进度条（对标 CE_ProgressBar）。 */
    XStyleCE_ProgressBarGroove,  /**< 进度槽（对标 CE_ProgressBarGroove）。 */
    XStyleCE_ProgressBarContents, /**< 进度内容（对标 CE_ProgressBarContents）。 */
    XStyleCE_ProgressBarLabel,   /**< 进度标签（对标 CE_ProgressBarLabel）。 */
    XStyleCE_MenuBarItem,        /**< 菜单栏项（对标 CE_MenuBarItem）。 */
    XStyleCE_MenuBarEmptyArea,   /**< 菜单栏空区（对标 CE_MenuBarEmptyArea）。 */
    XStyleCE_MenuItem,           /**< 菜单项（对标 CE_MenuItem）。 */
    XStyleCE_ToolButtonLabel,    /**< 工具按钮标签（对标 CE_ToolButtonLabel）。 */
    XStyleCE_Header,             /**< 表头（对标 CE_Header）。 */
    XStyleCE_HeaderSection,      /**< 表头段（对标 CE_HeaderSection）。 */
    XStyleCE_HeaderLabel,        /**< 表头标签（对标 CE_HeaderLabel）。 */
    XStyleCE_ToolBoxTab,         /**< 工具箱页（对标 CE_ToolBoxTab）。 */
    XStyleCE_SizeGrip,           /**< 尺寸手柄（对标 CE_SizeGrip）。 */
    XStyleCE_Splitter,           /**< 分隔条（对标 CE_Splitter）。 */
    XStyleCE_RubberBand,         /**< 橡皮筋框（对标 CE_RubberBand）。 */
    XStyleCE_DockWidgetTitle,    /**< 停靠窗标题（对标 CE_DockWidgetTitle）。 */
    XStyleCE_ScrollBarSlider,    /**< 滚动条滑块（对标 CE_ScrollBarSlider）。 */
    XStyleCE_ScrollBarAddLine,   /**< 滚动条加行（对标 CE_ScrollBarAddLine）。 */
    XStyleCE_ScrollBarSubLine,   /**< 滚动条减行（对标 CE_ScrollBarSubLine）。 */
    XStyleCE_ScrollBarAddPage,   /**< 滚动条加页（对标 CE_ScrollBarAddPage）。 */
    XStyleCE_ScrollBarSubPage,   /**< 滚动条减页（对标 CE_ScrollBarSubPage）。 */
    XStyleCE_ComboBoxLabel,      /**< 组合框标签（对标 CE_ComboBoxLabel）。 */
    XStyleCE_ToolBar,            /**< 工具栏（对标 CE_ToolBar）。 */
    XStyleCE_ShapedFrame,        /**< 形状边框（对标 CE_ShapedFrame）。 */
    XStyleCE_ItemViewItem        /**< 条目视图项（对标 CE_ItemViewItem）。 */
} XStyleControlElement;

/**
 * @brief      复杂控件（对标 Qt 6.8 QStyle::ComplexControl 子集）。
 */
typedef enum XStyleComplexControl
{
    XStyleCC_SpinBox = 0,        /**< 微调框（对标 CC_SpinBox）。 */
    XStyleCC_ComboBox,           /**< 组合框（对标 CC_ComboBox）。 */
    XStyleCC_ScrollBar,          /**< 滚动条（对标 CC_ScrollBar）。 */
    XStyleCC_Slider,             /**< 滑块（对标 CC_Slider）。 */
    XStyleCC_ToolButton,         /**< 工具按钮（对标 CC_ToolButton）。 */
    XStyleCC_Dial,               /**< 表盘（对标 CC_Dial）。 */
    XStyleCC_GroupBox            /**< 分组框（对标 CC_GroupBox）。 */
} XStyleComplexControl;

/**
 * @brief      像素度量（对标 Qt 6.8 QStyle::PixelMetric 子集）。
 */
typedef enum XStylePixelMetric
{
    XStylePM_ButtonMargin = 0,       /**< 按钮边距（对标 PM_ButtonMargin）。 */
    XStylePM_ButtonIconSize,         /**< 按钮图标尺寸（对标 PM_ButtonIconSize）。 */
    XStylePM_ButtonShiftHorizontal,  /**< 按钮水平位移（对标 PM_ButtonShiftHorizontal）。 */
    XStylePM_ButtonShiftVertical,    /**< 按钮垂直位移（对标 PM_ButtonShiftVertical）。 */
    XStylePM_CheckBoxLabelSpacing,   /**< 复选框标签间距（对标 PM_CheckBoxLabelSpacing）。 */
    XStylePM_RadioButtonLabelSpacing,/**< 单选钮标签间距（对标 PM_RadioButtonLabelSpacing）。 */
    XStylePM_IndicatorWidth,         /**< 指示器宽（对标 PM_IndicatorWidth）。 */
    XStylePM_IndicatorHeight,        /**< 指示器高（对标 PM_IndicatorHeight）。 */
    XStylePM_DefaultFrameWidth,      /**< 默认边框宽（对标 PM_DefaultFrameWidth）。 */
    XStylePM_ProgressBarChunkWidth,  /**< 进度块宽（对标 PM_ProgressBarChunkWidth）。 */
    XStylePM_MenuBarItemSpacing,     /**< 菜单栏项间距（对标 PM_MenuBarItemSpacing）。 */
    XStylePM_ToolBarHandleExtent,    /**< 工具栏把手范围（对标 PM_ToolBarHandleExtent）。 */
    XStylePM_ToolBarSeparatorExtent, /**< 工具栏分隔范围（对标 PM_ToolBarSeparatorExtent）。 */
    XStylePM_ToolBarItemSpacing,     /**< 工具栏项间距（对标 PM_ToolBarItemSpacing）。 */
    XStylePM_TabBarTabOverlap,       /**< 页签重叠（对标 PM_TabBarTabOverlap）。 */
    XStylePM_TabBarBaseHeight,       /**< 页签基座高（对标 PM_TabBarBaseHeight）。 */
    XStylePM_TabBarTabHSpace,        /**< 页签水平空间（对标 PM_TabBarTabHSpace）。 */
    XStylePM_TabBarTabVSpace,        /**< 页签垂直空间（对标 PM_TabBarTabVSpace）。 */
    XStylePM_ScrollBarExtent,        /**< 滚动条范围（对标 PM_ScrollBarExtent）。 */
    XStylePM_SplitterWidth,          /**< 分隔条宽（对标 PM_SplitterWidth）。 */
    XStylePM_DockWidgetTitleBarButtonMargin /**< 停靠标题按钮边距（对标 PM_DockWidgetTitleBarButtonMargin）。 */
} XStylePixelMetric;

/**
 * @brief      样式选项（对标 QStyleOption 核心字段）。
 */
typedef struct XStyleOption
{
    int m_version;        /**< 版本（保留）。 */
    int m_type;           /**< 基元/控件元素枚举。 */
    uint32_t m_state;     /**< XStyleStateFlag 位组合。 */
    XRect m_rect;         /**< 目标矩形（控件局部坐标）。 */
    XPalette m_palette;   /**< 调色板（按需填充）。 */
    /* ---- 通用控件字段 ---- */
    const char* m_text;   /**< 文本（借用；可空）。 */
    uint32_t m_textColor; /**< 文本色（0=调色板决定）。 */
    bool m_flat;          /**< 扁平（对标 flat）。 */
    bool m_checkable;     /**< 可勾选（GroupBox/按钮）。 */
    bool m_checked;       /**< 已勾选。 */
    /* ---- 按钮/复选/单选 ---- */
    int m_checkState;     /**< 0 未选/1 部分/2 选中（对标 checkState）。 */
    /* ---- 进度条 ---- */
    int m_progressMin;    /**< 进度最小值。 */
    int m_progressMax;    /**< 进度最大值。 */
    int m_progressValue;  /**< 进度当前值。 */
    bool m_progressInverted; /**< 进度反向。 */
    bool m_progressTextVisible; /**< 进度文本可见。 */
    /* ---- 页签 ---- */
    int m_tabPosition;    /**< 页签位置（0 上/1 下/2 左/3 右）。 */
    int m_tabIndex;       /**< 页签下标。 */
    bool m_tabSelected;   /**< 页签选中。 */
    /* ---- 滚动条/滑块 ---- */
    bool m_horizontal;    /**< 水平方向。 */
    int m_sliderMin;      /**< 滑块最小值。 */
    int m_sliderMax;      /**< 滑块最大值。 */
    int m_sliderValue;    /**< 滑块当前值。 */
    int m_sliderSingleStep; /**< 滑块单步。 */
    int m_sliderPageStep; /**< 滑块页步。 */
    int m_sliderTickInterval; /**< 刻度间隔值（0=自动，对标 tickInterval）。 */
    int m_sliderTickPosition; /**< 刻度位置：0 无/1 上/2 下/3 双侧。 */
    bool m_scrollSubLine;     /**< 绘制起始按钮（对标 SC_ScrollBarSubLine）。 */
    bool m_scrollAddLine;     /**< 绘制结束按钮（对标 SC_ScrollBarAddLine）。 */
    bool m_scrollActiveSub;   /**< 按下按钮（对标 activeSubControls 非空）。 */
    /* ---- 菜单/表头 ---- */
    bool m_selected;      /**< 选中。 */
    bool m_mouseOver;     /**< 悬停（冗余便捷）。 */
    /* ---- 微调框（对标 QStyleOptionSpinBox） ---- */
    bool m_spinFrame;     /**< 是否有边框（对标 frame）。 */
    int m_spinStepEnabled; /**< 步进使能位：0x1 上/0x2 下（对标 stepEnabled）。 */
    int m_spinSymbols;    /**< 按钮符号：0 上下箭头/1 加减号（对标 buttonSymbols）。 */
    bool m_spinActiveUp;  /**< 上按钮为活动子控件。 */
    bool m_spinActiveDown; /**< 下按钮为活动子控件。 */
    /* ---- 表盘（对标 QStyleOptionSlider 的 dial 子集） ---- */
    bool m_dialWrapping;  /**< 环绕（对标 dialWrapping）。 */
    bool m_notchesVisible; /**< 刻度可见（对标 subControls SC_DialTickmarks）。 */
    int m_notchSize;      /**< 刻度间隔值（对标 tickInterval）。 */
    int m_pageStep;       /**< 大刻度步长（对标 pageStep）。 */
    /* ---- 停靠/工具 ---- */
    bool m_closable;      /**< 可关闭。 */
    bool m_movable;       /**< 可移动。 */
    bool m_floatable;     /**< 可浮动。 */
} XStyleOption;

/**
 * @brief 初始化样式选项。
 *
 * @param option 目标选项指针，不能为空。
 * @param type 元素枚举（XStylePrimitiveElement/XStyleControlElement）。
 * @return 无返回值。
 */
void XStyleOption_init(XStyleOption* option, int type);

#endif /* XSTYLE_ON */
#ifdef __cplusplus
}
#endif
#endif /* XSTYLEOPTION_H */
