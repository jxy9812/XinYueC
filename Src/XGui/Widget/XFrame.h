/******************************************************************************
 * @file       XFrame.h
 * @brief      XFrame 框架控件（对标 Qt 6.8 QFrame，实现全部公开 API 的 C 适配）。
 * @details    XFrame 继承 XWidget，在控件矩形内绘制一组样式化的边框：
 *             - 框架样式由「形状 XFrameShape」与「阴影 XFrameShadow」两个
 *               掩码组合而成（FrameStyle = Shape | Shadow，数值与 Qt 6.8
 *               QFrame 完全一致，frameStyle() 返回组合值）；
 *             - 支持的形状：NoFrame（无边框）、Box（立体盒）、Panel（面板）、
 *               WinPanel（Windows 风格面板）、HLine（水平分隔线）、VLine
 *               （垂直分隔线）、StyledPanel（样式面板）；
 *             - 支持的阴影：Plain（平面前景单色）、Raised（凸起）、Sunken
 *               （凹陷）；Raised/Sunken 用调色板 Light/Dark/Mid/Shadow 系列
 *               颜色绘制立体效果，Plain 用控件前景角色（默认 WindowText）；
 *             - lineWidth 控制边框线宽、midLineWidth 控制 Box/HLine/VLine
 *               立体边框的中间过渡线宽；frameWidth() 返回综合边框宽度，
 *               随形状/阴影按 Qt 规则自动更新（Box/Raised 为 2*lw+mlw、
 *               StyledPanel/WinPanel 为 2、Panel 为 lw、NoFrame 为 0);
 *             - frameRect() 以 contentsRect 与四周边框宽度反推外形矩形，
 *               setFrameRect() 通过重写 contentsMargins 保持外形矩形稳定；
 *             - HLine/VLine 形状会按 Qt 语义接管尺寸策略（HLine 为
 *               Minimum/Fixed+Line，VLine 为 Fixed/Minimum+Line，其余形状
 *               为 Preferred/Preferred+Frame）；用户显式 setSizePolicy 后
 *               通过 WState_OwnSizePolicy 属性保留用户策略（Qt 同名语义）；
 *             - sizeHint：HLine 返回 (-1,3)、VLine 返回 (3,-1)、其余形状
 *               沿用基类 sizeHint；
 *             - 绘制语义与 Qt 的 qDrawPlainRect/qDrawShadeRect/
 *               qDrawShadePanel/qDrawShadeLine/qDrawWinPanel 完全一致，
 *               直接经 XPainter 的软件光栅后端输出（CE_ShapedFrame 的
 *               QCommonStyle 基类行为），不引入任何平台/后端 API。
 *             本模块不依赖任何平台 API，嵌入式可用；绘制全部走 XPainter。
 * @note       模块总开关 XFRAME_ON 定义于 XGuiConfig.h；XFRAME_ON=0 时
 *             裁剪整个 XFrame 公共 API。XFrame 依赖 XWIDGET_ON（示例控件
 *             之外的 XWidget 能力）与 XPALETTE_ON（立体阴影着色；关闭时
 *             绘制退化为无操作、frameWidth 计算仍可用）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XFRAME_H
#define XFRAME_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XPainter.h"

#if XWIDGET_ON && XFRAME_ON

/* ==================== 形状、阴影与掩码（对标 Qt 6.8 QFrame） ==================== */

/** @brief 框架形状（对标 QFrame::Shape，数值完全一致）。 */
typedef enum XFrameShape
{
    XFrameShape_NoFrame = 0x00,      /**< 无边框。 */
    XFrameShape_Box = 0x01,          /**< 盒状边框（Plain 单色 / 立体阴影）。 */
    XFrameShape_Panel = 0x02,        /**< 面板边框（Plain 单色 / 立体阴影）。 */
    XFrameShape_WinPanel = 0x03,     /**< Windows 经典面板边框。 */
    XFrameShape_HLine = 0x04,        /**< 水平分隔线（可拉伸一条边）。 */
    XFrameShape_VLine = 0x05,        /**< 垂直分隔线（可拉伸一条边）。 */
    XFrameShape_StyledPanel = 0x06   /**< 样式面板边框（默认线宽 1）。 */
} XFrameShape;

/** @brief 框架阴影风格（对标 QFrame::Shadow，数值完全一致）。 */
typedef enum XFrameShadow
{
    XFrameShadow_Plain = 0x10,       /**< 平面前景单色（WindowText/前景角色）。 */
    XFrameShadow_Raised = 0x20,      /**< 凸起（Light/Dark 立体着色）。 */
    XFrameShadow_Sunken = 0x30       /**< 凹陷（Dark/Light 立体着色）。 */
} XFrameShadow;

/** @brief 框架样式掩码（对标 QFrame::Shape_Mask / Shadow_Mask）。 */
typedef enum XFrameStyleMask
{
    XFrameStyleMask_Shape = 0x0f,    /**< 形状掩码。 */
    XFrameStyleMask_Shadow = 0xf0    /**< 阴影掩码。 */
} XFrameStyleMask;

/** @brief 框架样式绘制选项（对标 QStyleOptionFrame 的 XGui 子集）。 */
typedef struct XFrameStyleOption
{
    XPalette           m_palette;      /**< 绘制调色板（当前取 XFrame 的生效调色板）。 */
    XRect              m_rect;         /**< 边框绘制矩形（frameRect()）。 */
    XFrameShape        m_frameShape;   /**< 形状。 */
    XFrameShadow       m_frameShadow;  /**< 阴影。 */
    int                m_lineWidth;    /**< 线宽。 */
    int                m_midLineWidth; /**< 中间线宽。 */
    bool               m_raised;       /**< 置真表示 Raised 阴影态。 */
    bool               m_sunken;       /**< 置真表示 Sunken 阴影态。 */
} XFrameStyleOption;

/* ==================== 虚函数表（覆盖 XWidget 派生槽位） ==================== */

/**
 * @brief XFrame 虚函数表枚举。
 * @details 槽位数量与 XWidget 完全一致；XFrame 不新增槽位，仅重载
 *          XClass 的 Copy/Move/Deinit 与 XWidget 的 PaintEvent/ChangeEvent。
 */
XCLASS_DEFINE_BEGING(XFrame)
XCLASS_DEFINE_EXTEND_END(XFrame, XWidget)

/**
 * @brief      XFrame 框架控件对象；m_base 必须是第一个成员（嵌 XWidget）。
 * @details    字段含义：
 *             - m_frameStyle：形状|阴影组合值（默认 NoFrame|Plain=16）；
 *             - m_lineWidth/m_midLineWidth：边框线宽与中间线宽
 *               （默认 1/0，对标 QFrame::lineWidth/midLineWidth）；
 *             - m_frameWidth：综合边框宽度（由 updateFrameWidth 按形状/
 *               阴影规则刷新，对标 QFrame::frameWidth）；
 *             - m_leftFrameWidth/m_topFrameWidth/m_rightFrameWidth/
 *               m_bottomFrameWidth：四周独立边框宽度（updateFrameWidth
 *               同步，setFrameRect/frameRect 通过它换算 contentsMargins）。
 *             调用方不得手工修改任何字段；边框属性读写一律走公开 API。
 */
typedef struct XFrame
{
    XWidget m_base;                 /**< 基类成员；必须是第一个。 */
    int     m_frameStyle;           /**< 框架样式组合（XFrameShape|XFrameShadow）。 */
    short   m_lineWidth;            /**< 边框线宽。 */
    short   m_midLineWidth;         /**< 中间线宽。 */
    short   m_frameWidth;           /**< 综合边框宽度。 */
    short   m_leftFrameWidth;       /**< 左边框宽度。 */
    short   m_topFrameWidth;        /**< 上边框宽度。 */
    short   m_rightFrameWidth;      /**< 右边框宽度。 */
    short   m_bottomFrameWidth;     /**< 下边框宽度。 */
} XFrame;

/* ==================== 生命周期（对标 QFrame 构造/析构） ==================== */

/**
 * @brief      初始化 XFrame 类虚函数表并返回共享表指针。
 * @return     XFrame 类共享的虚函数表指针；初始化失败时返回 NULL。
 */
XVtable* XFrame_class_init(void);

/**
 * @brief      初始化 XFrame（对标 QFrame 构造）。
 * @details    先初始化 XWidget 基类（parent/flags 语义同 XWidget_init），
 *             再挂 XFrame 虚表并设置默认边框值：frameStyle=16
 *             （NoFrame|Plain）、lineWidth=1、midLineWidth=0、frameWidth=0。
 * @param      self   待初始化对象；不可为 NULL。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志（可传 0 表示 Widget 类型）。
 * @return     无返回值。
 */
void XFrame_init(XFrame* self, XWidget* parent, XWidgetFlags flags);

/** @brief 使用默认内存类型创建框架控件（语义同 XWidget_create）。 */
#define XFrame_create(parent, flags) XFrame_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief      使用指定内存类型创建框架控件。
 * @param      memory 对象内存类型。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags  窗口标志。
 * @return     新对象指针；失败返回 NULL。
 */
XFrame* XFrame_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);

/**
 * @brief      通过 XClass 虚表释放 XFrame 资源。
 * @param      self 待释放的栈对象或外部存储对象；可为 NULL。
 * @return     无返回值；堆对象应使用 XFrame_delete_base。
 */
#define XFrame_deinit_base(self) XClass_deinit_base((XClass*)(self))
/**
 * @brief      删除堆上的 XFrame 对象。
 * @param      self 由 XFrame_create 或 XFrame_create_ex 返回的对象；可为 NULL。
 * @return     无返回值。
 */
#define XFrame_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 样式属性（对标 QFrame public API） ==================== */

/**
 * @brief      查询框架样式组合值（Shape|Shadow，对标 QFrame::frameStyle）。
 * @param      self 框架对象；可为 NULL。
 * @return     当前样式值；self 为 NULL 时返回 NoFrame|Plain。
 */
int XFrame_frameStyle(const XFrame* self);
/**
 * @brief      设置框架样式组合值（对标 QFrame::setFrameStyle）。
 * @details    未显式调用过 XWidget_setSizePolicy（无 OwnSizePolicy 属性）
 *             时按形状接管尺寸策略：HLine→(Minimum,Fixed,Line)、
 *             VLine→(Fixed,Minimum,Line)、其余→(Preferred,Preferred,Frame)，
 *             随后清除 OwnSizePolicy；写入样式、刷新 sizeHint/几何并重算
 *             边框宽度、触发重绘；样式值按 Qt 的 short 存储语义保存。
 * @param      self 框架对象；可为 NULL。
 * @param      style 形状与阴影按位或组成的样式值。
 * @return     无返回值；self 为 NULL 时不执行任何操作。
 */
void XFrame_setFrameStyle(XFrame* self, int style);
/**
 * @brief      查询框架形状（frameStyle 的 Shape 掩码）。
 * @param      self 框架对象；可为 NULL。
 * @return     当前形状；self 为 NULL 时返回 NoFrame。
 */
XFrameShape XFrame_frameShape(const XFrame* self);
/**
 * @brief      设置框架形状（保留阴影掩码，对标 QFrame::setFrameShape）。
 * @param      self 框架对象；可为 NULL。
 * @param      shape 形状；同时触发与 setFrameStyle 相同的策略/宽度刷新。
 * @return     无返回值。
 */
void XFrame_setFrameShape(XFrame* self, XFrameShape shape);
/**
 * @brief      查询框架阴影（frameStyle 的 Shadow 掩码）。
 * @param      self 框架对象；可为 NULL。
 * @return     当前阴影；self 为 NULL 时返回 Plain。
 */
XFrameShadow XFrame_frameShadow(const XFrame* self);
/**
 * @brief      设置框架阴影（保留形状掩码，对标 QFrame::setFrameShadow）。
 * @param      self 框架对象；可为 NULL。
 * @param      shadow 阴影；同时触发与 setFrameStyle 相同的策略/宽度刷新。
 * @return     无返回值。
 */
void XFrame_setFrameShadow(XFrame* self, XFrameShadow shadow);

/**
 * @brief      查询边框线宽（对标 QFrame::lineWidth）。
 * @param      self 框架对象；可为 NULL。
 * @return     以 short 存储后的线宽；self 为 NULL 时返回默认值 1。
 */
int XFrame_lineWidth(const XFrame* self);
/**
 * @brief      设置边框线宽（对标 QFrame::setLineWidth）。
 * @details    按 Qt 的 short 存储语义转换后，仅当值变化时写入并重算
 *             frameWidth；Qt 语义下不立即触发重绘。
 * @param      self 框架对象；可为 NULL。
 * @param      width 要保存的线宽，按 short 转换。
 * @return     无返回值。
 */
void XFrame_setLineWidth(XFrame* self, int width);
/**
 * @brief      查询中间线宽（对标 QFrame::midLineWidth）。
 * @param      self 框架对象；可为 NULL。
 * @return     以 short 存储后的中间线宽；self 为 NULL 时返回默认值 0。
 */
int XFrame_midLineWidth(const XFrame* self);
/**
 * @brief      设置中间线宽（对标 QFrame::setMidLineWidth）。
 * @details    按 Qt 的 short 存储语义转换后，仅当值变化时写入并重算
 *             frameWidth；Qt 语义下不立即触发重绘。
 * @param      self 框架对象；可为 NULL。
 * @param      width 要保存的中间线宽，按 short 转换。
 * @return     无返回值。
 */
void XFrame_setMidLineWidth(XFrame* self, int width);

/**
 * @brief      查询综合边框宽度（对标 QFrame::frameWidth）。
 * @param      self 框架对象；可为 NULL。
 * @return     当前综合边框宽度；self 为 NULL 时返回 0。
 * @details    由形状/阴影/线宽按 Qt 规则计算：NoFrame=0、Box/HLine/VLine
 *             Plain=lineWidth 否则 2*lineWidth+midLineWidth、StyledPanel=2、
 *             WinPanel=2、Panel=lineWidth。
 */
int XFrame_frameWidth(const XFrame* self);

/**
 * @brief      查询外形矩形（contentsRect 按四周边框宽度外扩）。
 * @param      self 框架对象；可为 NULL。
 * @return     框架绘制矩形；self 为 NULL 时返回零矩形。
 */
XRect XFrame_frameRect(const XFrame* self);
/**
 * @brief      设置外形矩形（对标 QFrame::setFrameRect）。
 * @details    矩形先按四周边框宽度内缩得到客户区，再以客户区为基准重写
 *             contentsMargins，使重新读取的 frameRect() 与设置值一致；
 *             rect 参数可传 NULL/无效矩形，此时使用整个控件矩形。右侧和
 *             下侧边距按自身坐标系的 QWidget::rect() 边界计算。
 * @param      self 框架对象；可为 NULL。
 * @param      rect 要设置的外形矩形；可为 NULL 或无效矩形。
 * @return     无返回值。
 */
void XFrame_setFrameRect(XFrame* self, const XRect* rect);

/**
 * @brief      查询框架尺寸提示（对标 QFrame::sizeHint）。
 * @param      self 框架对象；可为 NULL。
 * @return     HLine 返回 (-1,3)，VLine 返回 (3,-1)，其他形状返回基类
 *             尺寸提示；self 为 NULL 时返回 (-1,-1)。
 */
XSize XFrame_sizeHint(const XFrame* self);

/**
 * @brief      直接绘制框架（对标 QFrame::drawFrame(QPainter*)）。
 * @param      self 框架对象；可为 NULL。
 * @param      painter 目标绘制器；可为 NULL。
 * @return     无返回值；任一参数无效时不绘制。
 * @details    用给定 XPainter 按当前样式绘制边框；内部保存/恢复绘制器
 *             状态（画笔颜色/宽度/裁剪/变换不受调用方影响）。无调色板
 *             能力（XPALETTE_ON=0）时为无操作。
 */
void XFrame_drawFrame(XFrame* self, XPainter* painter);

/**
 * @brief      填充框架样式选项（对标 QFrame::initStyleOption）。
 * @param      self 框架对象；可为 NULL。
 * @param      option 输出的框架样式选项；可为 NULL。
 * @return     无返回值；任一参数无效时不修改 option。
 * @details    填充绘制矩形（frameRect）、形状/阴影、线宽/中间线宽
 *             （WinPanel/NoFrame 使用综合 frameWidth）与生效调色板。
 */
void XFrame_initStyleOption(XFrame* self, XFrameStyleOption* option);

#endif /* XWIDGET_ON && XFRAME_ON */

#ifdef __cplusplus
}
#endif
#endif /* XFRAME_H */
