/**
 * @file       XAbstractScrollArea.h
 * @brief      XAbstractScrollArea 滚动区域基类（对标 Qt 6.8
 *             QAbstractScrollArea 全部公共 API）。
 * @details    功能范围：
 *             - viewport 子控件（内容承载区，调用方在 viewport 内放置
 *               内容或派生类重用）；
 *             - 垂直/水平滚动条（内部 XScrollBar，随策略显示/隐藏）；
 *             - 滚动条策略：XScrollBarPolicy（AsNeeded/AlwaysOff/
 *               AlwaysOn，数值对齐 Qt::ScrollBarPolicy）；
 *             - cornerWidget（右下角控件，第一版仅存储/挂接）；
 *             - 滚动条 value 变化驱动 viewport 内容滚动
 *               （scrollContentsBy 虚槽，派生类可重载）。
 * @note       模块总开关 XABSTRACTSCROLLAREA_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 */
#ifndef XABSTRACTSCROLLAREA_H
#define XABSTRACTSCROLLAREA_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XGeometry.h" /* XMargins（viewportMargins 返回类型）。 */
#include "XFrame.h"
#if XSCROLLBAR_ON
#include "XScrollBar.h"
#endif

#if XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON

/** @brief 滚动条显示策略（对标 Qt::ScrollBarPolicy，数值一致）。 */
typedef enum XScrollBarPolicy
{
    XScrollBarPolicy_AsNeeded = 0,   /**< 按需显示。 */
    XScrollBarPolicy_AlwaysOff = 1,  /**< 永不显示。 */
    XScrollBarPolicy_AlwaysOn = 2    /**< 永远显示。 */
} XScrollBarPolicy;

/** @brief 尺寸自适应策略（对标 QAbstractScrollArea::SizeAdjustPolicy，数值一致）。 */
typedef enum XAbstractScrollAreaSizeAdjustPolicy
{
    XAbstractScrollAreaSizeAdjustPolicy_AdjustIgnored = 0, /**< 不随视口内容调整（用控件尺寸提示）。 */
    XAbstractScrollAreaSizeAdjustPolicy_AdjustToContentsOnFirstShow = 1, /**< 首次显示时按内容调整。 */
    XAbstractScrollAreaSizeAdjustPolicy_AdjustToContents = 2 /**< 每次查询都按内容调整。 */
} XAbstractScrollAreaSizeAdjustPolicy;

XCLASS_DEFINE_BEGING(XAbstractScrollArea)
XCLASS_DEFINE_ENUM(XAbstractScrollArea, ScrollContentsBy) = XCLASS_VTABLE_GET_SIZE(XFrame),
/* [Qt 对齐 2026-11-11] 保护虚函数槽位（对标 qabstractscrollarea.h:64/75/102
 * 的 setupViewport / viewportEvent / viewportSizeHint）。追加在既有槽之后，
 * 既有槽号不变。 */
XCLASS_DEFINE_ENUM(XAbstractScrollArea, SetupViewport),
XCLASS_DEFINE_ENUM(XAbstractScrollArea, ViewportSizeHint),
XCLASS_DEFINE_ENUM(XAbstractScrollArea, ViewportEvent),
XCLASS_DEFINE_END(XAbstractScrollArea)

/**
 * @brief      XAbstractScrollArea 控件对象；m_base 必须是第一个成员。
 */
typedef struct XAbstractScrollArea
{
    XFrame m_base;             /**< 基类成员；必须是第一个。 */
    XWidget* m_viewport;       /**< 内容承载区子控件（拥有）。 */
    XScrollBar* m_vScrollBar;  /**< 垂直滚动条（拥有）。 */
    XScrollBar* m_hScrollBar;  /**< 水平滚动条（拥有）。 */
    XWidget* m_cornerWidget;   /**< 右下角控件（借用）。 */
    XObject* m_viewportFilter; /**< 视口事件过滤器（拥有；本类私有）。
                                    对标 Qt 的 QAbstractScrollAreaFilter：
                                    视口收到的每个事件先经本对象转到
                                    viewportEvent 虚槽。 */
    XWidget* m_sbWidgets[8];   /**< 附加滚动条控件（对标 addScrollBarWidget；借用）。 */
    XWidget* m_sbWidgetQuery[9]; /**< scrollBarWidgets 的过滤结果暂存（借用，
                                      末尾 NULL 结尾；下次调用/容器变更即失效）。 */
    int m_contentWidth;        /**< 内容宽度（驱动水平滚动范围；内容尺寸可能超出
                                    16 位，保持 int）。 */
    int m_contentHeight;       /**< 内容高度（驱动垂直滚动范围；内容尺寸可能超出
                                    16 位，保持 int）。 */
    uint16_t m_sbWidgetAligns[8]; /**< 附加控件对齐位（XAlignment 位掩码；
                                       掩码上限 0x1ff，uint16 全值往返）。 */
    uint8_t m_vPolicy;         /**< 垂直滚动条策略（XScrollBarPolicy；默认 AsNeeded）。 */
    uint8_t m_hPolicy;         /**< 水平滚动条策略（XScrollBarPolicy；默认 AsNeeded）。 */
    uint8_t m_sbWidgetCount;   /**< 附加控件数量（硬上限 8，见 addScrollBarWidget）。 */
    uint8_t m_sizeAdjustPolicy; /**< 尺寸自适应策略（XAbstractScrollAreaSizeAdjustPolicy；
                                    默认 AdjustIgnored）。 */
    /* [内存治理 2026-10-15] 视口边距四向 px（setViewportMargins 承载；
       滚动区量级小 px 值收 int16_t）。 */
    int16_t m_marginLeft;      /**< 视口左边距 px（默认 0）。 */
    int16_t m_marginTop;       /**< 视口上边距 px（默认 0）。 */
    int16_t m_marginRight;     /**< 视口右边距 px（默认 0）。 */
    int16_t m_marginBottom;    /**< 视口下边距 px（默认 0）。 */
    XSize m_sizeHintCache;     /**< AdjustToContentsOnFirstShow 的尺寸提示缓存（内部）。 */
    bool m_sizeHintCached;     /**< 尺寸提示缓存是否有效（内部）。 */
    /* [触摸滚动 2026-10-29] 触摸跟手滚动+惯性状态：DragBegin 认领时标
       XGestureDragMode_Track，随后 DragUpdate/DragEnd/DragCancel 原始位移
       流按帧差分驱动滚动条；DragEnd 按近期采样速度启动惯性定时衰减
       （对标移动端 fling）。XTimerId 经 XObject.h 链入。 */
    bool m_touchScrollEnabled; /**< 触摸拖动滚动开关（默认 true；关=DragBegin
                                    不认领，回落历史合成滚轮语义）。 */
    bool m_touchScrolling;     /**< 跟手拖动进行中（本区已认领 Track）。 */
    bool m_flingActive;        /**< 惯性滚动进行中（m_flingTimerId 有效）。 */
    int  m_dragLastX;          /**< 跟手帧最近主点位置（本控件局部坐标）。 */
    int  m_dragLastY;
    XTimerId m_flingTimerId;   /**< 惯性定时器 id（XTIMER_INVALID_ID=未布防）。 */
    int  m_flingVelX;          /**< 惯性速度 px/s（+x=内容随指右移方向）。 */
    int  m_flingVelY;          /**< 惯性速度 px/s（+y=内容随指下移方向）。 */
    int  m_flingRemX;          /**< 惯性位移累加余数（1e-3 px 域；亚像素
                                    节拍不丢步，尾速平滑到停机阈值）。 */
    int  m_flingRemY;
    int64_t m_flingLastMs;     /**< 惯性上一拍时刻（单调毫秒）。 */
    int  m_velSampleX[4];      /**< 跟手速度采样环（DragUpdate 主点位置）。 */
    int  m_velSampleY[4];
    int64_t m_velSampleMs[4];  /**< 采样时刻（单调毫秒；>150ms 旧样不计速）。 */
    uint8_t m_velSampleCount;  /**< 有效采样数（0..4）。 */
    uint8_t m_velSampleHead;   /**< 环写指针（满后覆盖最旧）。 */
} XAbstractScrollArea;

/* ==================== 生命周期 ==================== */

XVtable* XAbstractScrollArea_class_init(void);
void XAbstractScrollArea_init(XAbstractScrollArea* self, XWidget* parent,
                              XWidgetFlags flags);
#define XAbstractScrollArea_create(parent, flags) XAbstractScrollArea_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XAbstractScrollArea* XAbstractScrollArea_create_ex(
    XMemoryType memory, XWidget* parent, XWidgetFlags flags);

/* ==================== 视口与滚动条（对标公共 API） ==================== */

/** @brief 返回内容承载视口子控件（对标 viewport()）。 */
/**
 * @brief      获取视口。
 */
XWidget* XAbstractScrollArea_viewport(const XAbstractScrollArea* self);
/** @brief 返回垂直滚动条（对标 verticalScrollBar()）。 */
/**
 * @brief      获取垂直滚动条。
 */
XScrollBar* XAbstractScrollArea_verticalScrollBar(
    const XAbstractScrollArea* self);
/** @brief 返回水平滚动条（对标 horizontalScrollBar()）。 */
/**
 * @brief      获取水平滚动条。
 */
XScrollBar* XAbstractScrollArea_horizontalScrollBar(
    const XAbstractScrollArea* self);
/** @brief 设置垂直滚动条策略（对标 setVerticalScrollBarPolicy）。 */
/**
 * @brief      设置垂直滚动条策略。
 */
void XAbstractScrollArea_setVerticalScrollBarPolicy(
    XAbstractScrollArea* self, XScrollBarPolicy policy);
/** @brief 查询垂直滚动条策略。 */
/**
 * @brief      获取垂直滚动条策略。
 */
XScrollBarPolicy XAbstractScrollArea_verticalScrollBarPolicy(
    const XAbstractScrollArea* self);
/** @brief 设置水平滚动条策略（对标 setHorizontalScrollBarPolicy）。 */
/**
 * @brief      设置水平滚动条策略。
 */
void XAbstractScrollArea_setHorizontalScrollBarPolicy(
    XAbstractScrollArea* self, XScrollBarPolicy policy);
/** @brief 查询水平滚动条策略。 */
/**
 * @brief      获取水平滚动条策略。
 */
XScrollBarPolicy XAbstractScrollArea_horizontalScrollBarPolicy(
    const XAbstractScrollArea* self);
/** @brief 设置右下角控件（对标 setCornerWidget；Qt 同接管语义：新件
 *         reparent 归本控件并 show，同槽旧件隐藏不销毁，随后重排）。
 */
void XAbstractScrollArea_setCornerWidget(XAbstractScrollArea* self,
                                         XWidget* widget);
/** @brief 设置视口边距（对标 QAbstractScrollArea::setViewportMargins，
 *         qabstractscrollarea.h:71-72）。
 * @details 视口/滚动条/角件在收边后的矩形内排布；负值钳 0，变更即
 *          立即重排。轻量等价物：Qt 侧为 protected 面并配 QMargins
 *          重载，本框架单 API 承载四向 px。
 * @param self 目标滚动区域；NULL 无操作。
 * @param left/top/right/bottom 四向边距 px。
 * @return 无返回值。
 */
void XAbstractScrollArea_setViewportMargins(XAbstractScrollArea* self,
                                            int left, int top,
                                            int right, int bottom);
/** @brief 查询右下角控件（对标 cornerWidget()）。 */
/**
 * @brief      获取角落控件。
 */
XWidget* XAbstractScrollArea_cornerWidget(const XAbstractScrollArea* self);
/** @brief 设置内容尺寸（驱动滚动范围；内容经 viewport 承载时由派生类
 *         或调用方维护）。 */
/**
 * @brief      设置内容尺寸。
 */
void XAbstractScrollArea_setContentSize(XAbstractScrollArea* self,
                                        int width, int height);
/** @brief 替换垂直滚动条（对标 setVerticalScrollBar；接管新条所有权并
 *         删除旧条；NULL 表示删除现有条）。 */
/**
 * @brief      替换垂直滚动条。
 */
void XAbstractScrollArea_setVerticalScrollBar(XAbstractScrollArea* self,
                                              XScrollBar* scrollbar);
/** @brief 替换水平滚动条（对标 setHorizontalScrollBar；语义同上）。 */
/**
 * @brief      替换水平滚动条。
 */
void XAbstractScrollArea_setHorizontalScrollBar(XAbstractScrollArea* self,
                                                XScrollBar* scrollbar);
/** @brief 在滚动条旁附加控件（对标 addScrollBarWidget；控件归调用方）。
 * @details 归槽口径按 Qt（qabstractscrollarea.cpp:762-785）：对齐含
 *          Left/Right 的归「水平条带」（Left=条左侧、Right=条右侧），
 *          含 Top/Bottom 的归「垂直条带」（Top=条上方、Bottom=条下方）；
 *          四类之外的位忽略（缺省按 Right 处理，与首版一致）。
 *          控件随条带排布，不参与视口尺寸计算（同 Qt：容器与视口并列）。
 * @param self 目标滚动区域。
 * @param widget 附加控件；NULL 忽略。
 * @param alignment XAlignment 位掩码（Left/Right/Top/Bottom 决定条带与端点）。
 * @return 无返回值。
 */
void XAbstractScrollArea_addScrollBarWidget(XAbstractScrollArea* self,
                                            XWidget* widget, int alignment);
/** @brief 查询附加滚动条控件（对标 scrollBarWidgets）。
 * @details 按 Qt 口径**按对齐分类过滤**（qabstractscrollarea.cpp:
 *          787-803）：alignment 含 Left 时取水平条带左侧控件、含 Right
 *          取水平条带右侧、含 Top 取垂直条带上侧、含 Bottom 取垂直条带
 *          下侧，四类按 Left→Right→Top→Bottom 顺序拼接。alignment 为 0
 *          时返回空集合。
 * @param self 目标滚动区域。
 * @param alignment XAlignment 位掩码（可为多类组合）。
 * @return 借用指针数组（XWidget*，末尾 NULL；最多 8 项）；结果存放在
 *         本控件内部暂存区，**下次调用或容器变更即失效**；无效参数返回 NULL。
 */
const XWidget** XAbstractScrollArea_scrollBarWidgets(
    const XAbstractScrollArea* self, int alignment);
/** @brief 替换视口控件（对标 setViewport；接管新视口所有权并删除旧视口）。
 * @details Qt 口径（qabstractscrollarea.cpp:491-514）：widget 为 NULL 时
 *          新建一个缺省视口；新视口 reparent 到本控件、设为焦点代理、
 *          安装事件过滤器，随后调 setupViewport(新视口)，**最后**才删除
 *          旧视口。
 * @param self 目标滚动区域。
 * @param widget 新视口；NULL 表示新建缺省视口。
 * @return 无返回值。
 */
void XAbstractScrollArea_setViewport(XAbstractScrollArea* self,
                                     XWidget* widget);
/** @brief 读取视口边距（对标 QAbstractScrollArea::viewportMargins，
 *         qabstractscrollarea.h:73）。
 * @param self 目标滚动区域；为 NULL 时返回四向 0。
 * @return 视口边距（XMargins，四向 px）。
 */
XMargins XAbstractScrollArea_viewportMargins(const XAbstractScrollArea* self);
/** @brief 视口建议尺寸（对标 QAbstractScrollArea::viewportSizeHint，
 *         qabstractscrollarea.cpp:1450-1462）。
 * @details 缺省实现：视口自身的 sizeHint（有效时）；否则按字体高 h=
 *          max(10, 字高) 返回 6h × 4h。虚函数（虚槽
 *          EXAbstractScrollArea_ViewportSizeHint），子类可重载。
 * @param self 目标滚动区域；为 NULL 时返回 (0,0)。
 * @return 视口建议尺寸。
 */
XSize XAbstractScrollArea_viewportSizeHint(const XAbstractScrollArea* self);
/** @brief 视口换装钩子（对标 QAbstractScrollArea::setupViewport，
 *         qabstractscrollarea.h:64 / .cpp:1494-1500）。
 * @details setViewport 在换装后、删除旧视口前调用；缺省实现为空操作。
 *          虚函数（虚槽 EXAbstractScrollArea_SetupViewport），子类重载以
 *          初始化新视口（如安装布局/滚动策略）。
 * @param self 目标滚动区域。
 * @param viewport 新视口（已 reparent 到本控件）。
 * @return 无返回值。
 */
void XAbstractScrollArea_setupViewport(XAbstractScrollArea* self,
                                      XWidget* viewport);
/** @brief 视口事件入口（对标 QAbstractScrollArea::viewportEvent，
 *         qabstractscrollarea.h:75 的 protected virtual）。
 * @details 视口收到的**每一个**事件都先经本入口（本类在视口上装了私有
 *          事件过滤器，对标 Qt 的 QAbstractScrollAreaFilter，见
 *          qabstractscrollarea.cpp:1036-1077）：返回 true = 已处理、不再
 *          投递给视口自身；返回 false = 继续按视口常规路径分发。
 *          虚函数（虚槽 EXAbstractScrollArea_ViewportEvent），子类可重载以
 *          拦截视口事件（如拖放/鼠标/滚轮策略）。
 *
 *          @note 与 Qt 基类实现的一处**有意偏差**：Qt 的缺省实现对
 *          Paint/Resize/鼠标/滚轮/上下文菜单/拖放等事件返回 true，因为它把
 *          这些视口事件**重映射**给滚动区自己的处理器（Qt 的派生类在
 *          viewport 上绘制）。本框架的绘制模型相反——视口是透明子控件、
 *          滚动区本体自绘（见 XTextEdit.c 的 VX_textEdit_paintEvent 在
 *          本体上铺底），且视口未处理的事件本就沿父子链上抛到滚动区
 *          （XCoreApplication_notify 的 recvParent 分支），再做一次重映射
 *          会重复投递。故本类缺省实现返回 false（不消费），只提供钩子。
 * @param self 目标滚动区域。
 * @param event 视口事件（借用，不得持有）。
 * @return true=事件已被本入口消费；false=继续投递给视口。
 */
bool XAbstractScrollArea_viewportEvent(XAbstractScrollArea* self,
                                       XEvent* event);
/** @brief 视口最大可用尺寸（对标 maximumViewportSize）。
 * @details 尺寸 = 控件内容矩形（已扣 QFrame 帧宽，qabstractscrollarea.cpp:
 *          535-541 `size() - QSize(f + left + right, f + top + bottom)`）
 *          − 视口边距 −（策略为 AlwaysOn 时的条厚）。条厚取条自身
 *          sizeHint（与排布同源）。
 * @param self 目标滚动区域。
 * @return 建议最大视口尺寸。
 */
XSize XAbstractScrollArea_maximumViewportSize(
    const XAbstractScrollArea* self);
/** @brief 尺寸提示（对标 sizeHint）。
 * @param self 目标滚动区域。
 * @return 建议尺寸。
 */
XSize XAbstractScrollArea_sizeHint(const XAbstractScrollArea* self);
/** @brief 最小尺寸提示（对标 minimumSizeHint）。
 * @param self 目标滚动区域。
 * @return 建议最小尺寸。
 */
XSize XAbstractScrollArea_minimumSizeHint(const XAbstractScrollArea* self);

/* ==================== 尺寸自适应策略（对标 SizeAdjustPolicy） ========== */

/**
 * @brief 读取尺寸自适应策略（对标 sizeAdjustPolicy）。
 *
 *        @note AdjustToContents 的自适应布局（内容变化自动重设控件
 *        尺寸）未接：策略当前仅承载状态，仅影响 sizeHint 查询。
 *
 * @param self 目标滚动区域；可为 NULL。
 * @return 当前策略；self 为 NULL 时返回 AdjustIgnored。
 */
XAbstractScrollAreaSizeAdjustPolicy XAbstractScrollArea_sizeAdjustPolicy(
    const XAbstractScrollArea* self);

/**
 * @brief 设置尺寸自适应策略（对标 setSizeAdjustPolicy）。
 *
 *        策略变化后清除尺寸提示缓存并请求重新布局（对标 Qt 的
 *        `d->sizeHint = QSize(); updateGeometry();`）。
 *
 *        @note AdjustToContents 的自适应布局（内容变化自动重设控件
 *        尺寸）未接：策略当前仅承载状态，仅影响 sizeHint 查询。
 *
 * @param self 目标滚动区域。
 * @param policy 新策略（XAbstractScrollAreaSizeAdjustPolicy）。
 * @return 无返回值。
 */
void XAbstractScrollArea_setSizeAdjustPolicy(
    XAbstractScrollArea* self, XAbstractScrollAreaSizeAdjustPolicy policy);

/* ==================== 触摸滚动（XGui 扩展） ==================== */

/**
 * @brief  设置触摸拖动滚动开关（XGui 扩展，默认开启）。
 * @details 开启时触摸拖动由本区认领（XGestureDragMode_Track）：按住视口
 *          拖动内容随手跟移（手指方向=内容移动方向，移动端操作习惯），
 *          抬手按近期位移采样给惯性衰减滚动；无滚动范围（内容未超高）
 *          或开关关闭时不认领，回落历史合成滚轮语义（行为不变）。触摸
 *          滚动条把手不受影响（把手按下即 grabMouse，DragBegin 不会
 *          询问到本区）。
 * @param self 目标滚动区域；NULL 无操作。
 * @param enabled true=开启（默认）；false=关闭。
 */
void XAbstractScrollArea_setTouchScrollEnabled(XAbstractScrollArea* self,
                                               bool enabled);
/** @brief 查询触摸拖动滚动开关（默认 true；self 为 NULL 返回 false）。 */
bool XAbstractScrollArea_touchScrollEnabled(const XAbstractScrollArea* self);

/* 保护槽入口（scrollContentsBy_base/resizeEvent_base）声明于
 * XAbstractScrollArea_Protected.h；本公共头仅保留公开 API。 */

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_ON && XFRAME_ON && XSCROLLBAR_ON && XABSTRACTSCROLLAREA_ON */

#endif /* XGUI_ON */

#endif /* XABSTRACTSCROLLAREA_H */
