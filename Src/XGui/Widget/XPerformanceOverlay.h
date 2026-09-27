/******************************************************************************
 * @file       XPerformanceOverlay.h
 * @brief      XGui 性能悬浮层控件。
 * @details    XPerformanceOverlay 继承 XLabel，提供可裁剪的 FPS、帧耗时、
 *             网络下载/上传速率与 CPU/GPU 系统负载显示；支持 Qt 日期时间
 *             自定义格式思想的输出模板（setFormat：占位符 + \n 转义）。
 *             控件本身不创建独立窗口，draw() 将其绘制到调用方提供的
 *             XPainter 后备缓冲中，适合桌面和嵌入式使用。
 *             位置保存在控件几何中，可由调用方指定，也可通过拖拽会话更新。
 ******************************************************************************/
#ifndef XPERFORMANCEOVERLAY_H
#define XPERFORMANCEOVERLAY_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "XGuiConfig.h"
#include "XLabel.h"
#include "XPainter.h"

#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON

/** @brief 性能悬浮层的九宫格常用位置。 */
typedef enum XPerformanceOverlayPosition
{
    XPerformanceOverlayPosition_TopLeft = 0,
    XPerformanceOverlayPosition_TopCenter,
    XPerformanceOverlayPosition_TopRight,
    XPerformanceOverlayPosition_CenterLeft,
    XPerformanceOverlayPosition_Center,
    XPerformanceOverlayPosition_CenterRight,
    XPerformanceOverlayPosition_BottomLeft,
    XPerformanceOverlayPosition_BottomCenter,
    XPerformanceOverlayPosition_BottomRight,
    XPerformanceOverlayPosition_Top = XPerformanceOverlayPosition_TopCenter,
    XPerformanceOverlayPosition_Bottom = XPerformanceOverlayPosition_BottomCenter,
    XPerformanceOverlayPosition_Left = XPerformanceOverlayPosition_CenterLeft,
    XPerformanceOverlayPosition_Right = XPerformanceOverlayPosition_CenterRight
} XPerformanceOverlayPosition;

XCLASS_DEFINE_BEGING(XPerformanceOverlay)
XCLASS_DEFINE_EXTEND_END(XPerformanceOverlay, XLabel)

/** @brief 性能悬浮层控件对象；首成员为 XLabel 基类。 */
typedef struct XPerformanceOverlay
{
    XLabel  m_base;                    /**< XLabel 基类。 */
    int64_t m_sampleStartUsecs;        /**< 当前统计窗口起点。 */
    int64_t m_sampleUsecs;             /**< 统计窗口内帧耗时总和。 */
    int64_t m_maxUsecs;                /**< 统计窗口内最长帧耗时。 */
    unsigned m_sampleFrames;           /**< 统计窗口内帧数。 */
    double  m_fps;                     /**< 最近统计窗口 FPS。 */
    double  m_frameMs;                 /**< 最近统计窗口平均帧耗时。 */
    double  m_maxFrameMs;              /**< 最近统计窗口最长帧耗时。 */
    bool    m_networkAvailable;        /**< 最近一次网络计数是否有效。 */
    int64_t m_networkSampleUsecs;      /**< 网络计数上一次采样时刻。 */
    uint64_t m_networkRxBytes;         /**< 网络计数上一次接收总字节数。 */
    uint64_t m_networkTxBytes;         /**< 网络计数上一次发送总字节数。 */
    double  m_networkRxKbps;           /**< 最近采样周期的下载速率（KiB/s）。 */
    double  m_networkTxKbps;           /**< 最近采样周期的上传速率（KiB/s）。 */
    double  m_cpuPercent;              /**< 最近采样的系统 CPU 使用率（[0,100]，-1=无基线/不支持）。 */
    double  m_gpuPercent;              /**< 最近采样的系统 GPU 使用率（[0,100]，-1=无基线/不支持）。 */
    XString* m_format;                 /**< 自定义输出格式模板；NULL=默认逐行拼装；对象拥有。 */
    uint32_t m_backgroundColor;        /**< 背景 ARGB 颜色。 */
    bool    m_fpsVisible;              /**< 是否显示 FPS。 */
    bool    m_frameTimeVisible;        /**< 是否显示帧耗时。 */
    bool    m_networkVisible;          /**< 是否显示下载/上传速率。 */
    bool    m_sysStatVisible;          /**< 是否显示 CPU/GPU 系统负载。 */
    bool    m_autoFitSize;             /**< 是否按文本自然尺寸收框（宽取最宽行、高贴底）。 */
    bool    m_movable;                 /**< 是否允许开始拖拽。 */
    bool    m_fixed;                   /**< 是否锁定当前位置。 */
    bool    m_dragging;                /**< 是否处于拖拽会话。 */
    int     m_dragOffsetX;             /**< 拖拽点相对左边缘的偏移。 */
    int     m_dragOffsetY;             /**< 拖拽点相对上边缘的偏移。 */
} XPerformanceOverlay;

/** @brief 初始化性能悬浮层；parent 可为 NULL，控件默认不参与事件绘制。 */
void XPerformanceOverlay_init(XPerformanceOverlay* self, XWidget* parent,
                              XWidgetFlags flags);

/** @brief 使用默认内存类型创建性能悬浮层。 */
#define XPerformanceOverlay_create(parent, flags)  XPerformanceOverlay_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))

/** @brief 使用指定内存类型创建性能悬浮层。 */
XPerformanceOverlay* XPerformanceOverlay_create_ex(XMemoryType memory,
                                                    XWidget* parent,
                                                    XWidgetFlags flags);

/** @brief 通过 XClass 入口反初始化/删除/复制/移动性能悬浮层。 */
#define XPerformanceOverlay_deinit_base(self)  XClass_deinit_base((XClass*)(self))
#define XPerformanceOverlay_delete_base(self)  XClass_delete_base((XClass*)(self))

/* 几何 API 属于 XWidget，悬浮层只提供类型安全的继承宏，不重复声明函数。 */
#define XPerformanceOverlay_geometry(self)  XWidget_geometry((const XWidget*)&((self)->m_base))
#define XPerformanceOverlay_setPosition(self, x, y)  XWidget_move((XWidget*)&((self)->m_base), (x), (y))
#define XPerformanceOverlay_position(self)  XWidget_pos((const XWidget*)&((self)->m_base))
#define XPerformanceOverlay_setSize(self, width, height)  XWidget_resize((XWidget*)&((self)->m_base), (width), (height))
#define XPerformanceOverlay_size(self)  XWidget_size((const XWidget*)&((self)->m_base))

/** @brief 设置悬浮层使用的字体家族；字符串会被复制。 */
void XPerformanceOverlay_setFontFamily(XPerformanceOverlay* self,
                                       const char* family);

/** @brief 设置悬浮层文字像素高度；变更后自动重算贴底高度并刷新文本。 */
void XPerformanceOverlay_setTextPixelSize(XPerformanceOverlay* self,
                                          int pixelHeight);

/** @brief 设置是否显示 FPS；编译时裁剪 FPS 时该调用为 no-op。 */
void XPerformanceOverlay_setFpsVisible(XPerformanceOverlay* self,
                                       bool visible);

/** @brief 查询 FPS 是否处于显示状态。 */
bool XPerformanceOverlay_isFpsVisible(const XPerformanceOverlay* self);

/** @brief 设置是否显示平均/最长帧耗时；裁剪帧耗时指标时该调用为 no-op。 */
void XPerformanceOverlay_setFrameTimeVisible(XPerformanceOverlay* self,
                                             bool visible);

/** @brief 查询帧耗时是否处于显示状态。 */
bool XPerformanceOverlay_isFrameTimeVisible(
    const XPerformanceOverlay* self);

/** @brief 设置是否显示下载/上传速率；裁剪网络指标时该调用为 no-op。 */
void XPerformanceOverlay_setNetworkVisible(XPerformanceOverlay* self,
                                           bool visible);

/** @brief 查询下载/上传速率是否处于显示状态。 */
bool XPerformanceOverlay_isNetworkVisible(const XPerformanceOverlay* self);

/** @brief 设置是否显示 CPU/GPU 系统负载行；宏裁剪该能力时为 no-op。 */
void XPerformanceOverlay_setSysStatVisible(XPerformanceOverlay* self,
                                           bool visible);

/** @brief 查询 CPU/GPU 系统负载是否处于显示状态；宏裁剪时恒 false。 */
bool XPerformanceOverlay_isSysStatVisible(const XPerformanceOverlay* self);

/**
 * @brief 开关按文本自然尺寸自动收框（宽=最宽行文本宽+左右边距，
 *        高=文字底边贴住下边框线）。
 * @details 默认关闭（调用方显式 setSize 的尺寸不被改写）；开启后每次
 *          文本刷新重算宽高，位置跟随由调用方负责（尺寸变化后需自行
 *          重新锚定）。
 */
void XPerformanceOverlay_setAutoFitSize(XPerformanceOverlay* self,
                                        bool enabled);

/** @brief 查询是否开启文本尺寸自适应收框。 */
bool XPerformanceOverlay_isAutoFitSize(const XPerformanceOverlay* self);

/**
 * @brief 设置自定义输出格式模板（对标 QDateTime::toString 的自定义格式思想）。
 * @details 模板串复制保存；传 NULL 恢复默认逐行拼装（受各 isVisible 开关
 *          控制）。占位符：{fps}、{framems}、{maxframems}、{cpu}、{gpu}、
 *          {net}；字面 \n 转义为换行（模板串内真实换行字符亦可）；未知
 *          占位符原样保留；对应指标不可见或被宏裁剪时占位符替换为空串。
 *          例："FPS {fps}\nCPU {cpu} GPU {gpu}\n{net}"。
 * @param self 悬浮层；NULL 时函数不执行任何操作。
 * @param format 格式模板；NULL 恢复默认逐行拼装。
 */
void XPerformanceOverlay_setFormat(XPerformanceOverlay* self,
                                   const char* format);

/**
 * @brief 返回当前格式模板的借用指针。
 * @return 模板 UTF-8 借用指针（生存期至 setFormat/deinit）；未设置
 *         （默认逐行拼装）返回 NULL。
 */
const char* XPerformanceOverlay_format(const XPerformanceOverlay* self);

/**
 * @brief 按九宫格常用位置设置悬浮层。
 * @param position 常用位置枚举。
 * @param windowWidth 绘制目标客户区宽度。
 * @param windowHeight 绘制目标客户区高度。
 * @param margin 悬浮层与客户区边缘的间距，负数按 0 处理。
 */
void XPerformanceOverlay_setPresetPosition(XPerformanceOverlay* self,
                                           XPerformanceOverlayPosition position,
                                           int windowWidth, int windowHeight,
                                           int margin);

/** @brief 设置是否允许拖拽；关闭时会结束正在进行的拖拽会话。 */
void XPerformanceOverlay_setMovable(XPerformanceOverlay* self, bool movable);

/** @brief 查询是否允许拖拽。 */
bool XPerformanceOverlay_isMovable(const XPerformanceOverlay* self);

/** @brief 锁定/解锁当前位置；锁定后拖拽不会改变位置。 */
void XPerformanceOverlay_setFixed(XPerformanceOverlay* self, bool fixed);

/** @brief 查询当前位置是否已锁定。 */
bool XPerformanceOverlay_isFixed(const XPerformanceOverlay* self);

/**
 * @brief 开始拖拽会话。
 * @param x 鼠标在绘制目标客户区中的横坐标。
 * @param y 鼠标在绘制目标客户区中的纵坐标。
 * @return 命中且允许拖拽时返回 true。
 */
bool XPerformanceOverlay_beginDrag(XPerformanceOverlay* self, int x, int y);

/**
 * @brief 更新拖拽位置并限制在目标客户区内。
 * @param x 鼠标在绘制目标客户区中的横坐标。
 * @param y 鼠标在绘制目标客户区中的纵坐标。
 * @param windowWidth 绘制目标客户区宽度。
 * @param windowHeight 绘制目标客户区高度。
 * @return 位置发生变化时返回 true。
 */
bool XPerformanceOverlay_dragTo(XPerformanceOverlay* self, int x, int y,
                                int windowWidth, int windowHeight);

/** @brief 结束拖拽会话。 */
void XPerformanceOverlay_endDrag(XPerformanceOverlay* self);

/** @brief 查询是否处于拖拽会话。 */
bool XPerformanceOverlay_isDragging(const XPerformanceOverlay* self);

/** @brief 重置统计窗口和显示文本。 */
void XPerformanceOverlay_reset(XPerformanceOverlay* self);

/**
 * @brief 记录一帧并按配置周期刷新文本。
 * @param frameStartUsecs 帧开始的单调时钟微秒值。
 * @param nowUsecs 当前单调时钟微秒值。
 */
void XPerformanceOverlay_updateFrame(XPerformanceOverlay* self,
                                     int64_t frameStartUsecs,
                                     int64_t nowUsecs);

/**
 * @brief 更新主机网络收发计数并计算速率。
 * @param available 计数是否有效；读取失败时传 false，显示“下载 无 上传 无”。
 * @param rxBytes 当前累计接收字节数。
 * @param txBytes 当前累计发送字节数。
 * @param nowUsecs 与帧统计相同的单调时钟微秒值。
 */
void XPerformanceOverlay_updateNetwork(XPerformanceOverlay* self,
                                       bool available,
                                       uint64_t rxBytes,
                                       uint64_t txBytes,
                                       int64_t nowUsecs);

/**
 * @brief 将悬浮层绘制到窗口后备缓冲。
 * @details 位置和尺寸来自当前控件几何；请先通过 setPosition/setSize
 *          指定显示区域。绘制不会再次计算或覆盖调用方的位置。
 */
void XPerformanceOverlay_draw(XPerformanceOverlay* self, XPainter* painter);

#endif /* XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPERFORMANCEOVERLAY_H */
