/******************************************************************************
 * @file       XPerformanceOverlay.h
 * @brief      XGui 性能悬浮层控件。
 * @details    XPerformanceOverlay 继承 XLabel，提供可裁剪的 FPS、帧耗时和
 *             网络占位信息显示。控件本身不创建独立窗口，draw() 将其绘制
 *             到调用方提供的 XPainter 后备缓冲中，适合桌面和嵌入式使用。
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
    double  m_networkRxKbps;           /**< 最近采样周期的接收速率。 */
    double  m_networkTxKbps;           /**< 最近采样周期的发送速率。 */
    uint32_t m_backgroundColor;        /**< 背景 ARGB 颜色。 */
    int     m_overlayWidth;            /**< 悬浮层宽度。 */
    int     m_overlayHeight;           /**< 悬浮层高度。 */
    int     m_overlayMargin;           /**< 窗口边缘间距。 */
} XPerformanceOverlay;

/** @brief 初始化性能悬浮层；parent 可为 NULL，控件默认不参与事件绘制。 */
void XPerformanceOverlay_init(XPerformanceOverlay* self, XWidget* parent,
                              XWidgetFlags flags);

/** @brief 使用默认内存类型创建性能悬浮层。 */
#define XPerformanceOverlay_create(parent, flags) \
    XPerformanceOverlay_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))

/** @brief 使用指定内存类型创建性能悬浮层。 */
XPerformanceOverlay* XPerformanceOverlay_create_ex(XMemoryType memory,
                                                    XWidget* parent,
                                                    XWidgetFlags flags);

/** @brief 通过 XClass 入口反初始化/删除/复制/移动性能悬浮层。 */
#define XPerformanceOverlay_deinit_base XClass_deinit_base
#define XPerformanceOverlay_delete_base XClass_delete_base
#define XPerformanceOverlay_copy_base XClass_copy_base
#define XPerformanceOverlay_move_base XClass_move_base

/** @brief 设置悬浮层使用的字体家族；字符串会被复制。 */
void XPerformanceOverlay_setFontFamily(XPerformanceOverlay* self,
                                       const char* family);

/** @brief 设置悬浮层文字像素高度。 */
void XPerformanceOverlay_setTextPixelSize(XPerformanceOverlay* self,
                                          int pixelHeight);

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
 * @param available 计数是否有效；读取失败时传 false，显示 n/a。
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
 * @brief 将悬浮层绘制到窗口后备缓冲右上角。
 * @param windowWidth 窗口客户区宽度。
 * @param windowHeight 窗口客户区高度。
 */
void XPerformanceOverlay_draw(XPerformanceOverlay* self, XPainter* painter,
                              int windowWidth, int windowHeight);

#endif /* XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XPERFORMANCEOVERLAY_H */
