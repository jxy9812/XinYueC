/**
 * @file       XVirtualKeyboardTrace.h
 * @brief      XVirtualKeyboardTrace 手写轨迹容器公开 API（对标 Qt 6.8
 *             QVirtualKeyboardTrace，全 API 照抄 qvirtualkeyboardtrace.h
 *             :32-69）。
 * @details    一笔手写轨迹（QML_UNCREATABLE 口径：只能经
 *             XVirtualKeyboardInputEngine_traceBegin 创建，公共头不提
 *             供 create/init）。开源树无识别引擎——本轮实现容器与引擎
 *             分派面（架构预留，cerence/myscript=商业 N-A）。延时隐藏
 *             （startHideTimer）落 XObject 定时器（timerEvent 到期发
 *             canceledChanged? 否——Qt 语义为隐藏墨迹，本实现到期置
 *             m_hideExpired 供渲染侧查询，不改数据）。
 * @note       模块总开关 XVIRTUALKEYBOARD_ON；实现只依赖 XinYueC 抽象层。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDTRACE_H
#define XVIRTUALKEYBOARDTRACE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XMemory.h"

#if XVIRTUALKEYBOARD_ON

/** @brief 轨迹采样点（对标 QPointF 坐标；本类自带定义避免几何头依赖）。 */
typedef struct XVirtualKeyboardTracePoint
{
    float x;   /**< x 坐标（面板局部）。 */
    float y;   /**< y 坐标（面板局部）。 */
} XVirtualKeyboardTracePoint;

/** @brief 声明 XVirtualKeyboardTrace 虚函数枚举：继承 XObject（无新
 *         增槽位；TimerEvent 重载沿用基类槽位）。 */
XCLASS_DEFINE_BEGING(XVirtualKeyboardTrace)
XCLASS_DEFINE_EXTEND_END(XVirtualKeyboardTrace, XObject)

/**
 * @brief      手写轨迹对象；m_class 必须为第一个成员。
 * @details    点与通道数据由对象拥有；实例只能经引擎 traceBegin 获得。
 */
typedef struct XVirtualKeyboardTrace
{
    XObject m_class;   /**< 第一个成员，由 XObject 管理。 */
    void* m_data;      /**< 私有数据块（XVirtualKeyboardTracePrivate*），
                            由对象拥有；仅供实现使用。 */
} XVirtualKeyboardTrace;

/**
 * @brief      初始化类虚函数表并返回共享表指针。
 * @return     类共享虚函数表指针（进程期常驻，借用）；不失败。
 */
XVtable* XVirtualKeyboardTrace_class_init(void);

/* ==================== 属性（对标 Qt 全 API） ==================== */

/**
 * @brief      返回轨迹 id（对标 traceId）。
 * @param      self 轨迹对象借用指针；可为 NULL。
 * @return     轨迹 id；self 为 NULL 返回 0。
 */
int XVirtualKeyboardTrace_traceId(const XVirtualKeyboardTrace* self);
/**
 * @brief      设置轨迹 id（对标 setTraceId；变化时发 traceIdChanged）。
 * @param      self 轨迹对象；可为 NULL，NULL 时不执行操作。
 * @param      traceId 新轨迹 id。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardTrace_setTraceId(XVirtualKeyboardTrace* self,
                                      int traceId);

/**
 * @brief      返回通道数（对标 channels().count()）。
 * @details    Qt 返回 QStringList；本实现为通道名 UTF-8 串数组，逐条
 *             经 XVirtualKeyboardTrace_channelAt 取用。
 * @param      self 轨迹对象借用指针；可为 NULL。
 * @return     通道数；self 为 NULL 返回 0。
 */
int XVirtualKeyboardTrace_channelCount(const XVirtualKeyboardTrace* self);
/**
 * @brief      返回第 index 条通道名。
 * @param      self 轨迹对象借用指针；可为 NULL。
 * @param      index 通道索引（0 基）。
 * @return     UTF-8 借用指针（内部存储，不得释放或修改）；越界或
 *             self 为 NULL 返回 NULL。
 */
const char* XVirtualKeyboardTrace_channelAt(
        const XVirtualKeyboardTrace* self, int index);
/**
 * @brief      重设通道集（对标 setChannels；清空原通道及其数据）。
 * @param      self 目标对象；可为 NULL。
 * @param      channels 通道名 UTF-8 数组（借用，调用期间有效）；NULL 等价清空。
 * @param      count 条数；<0 按 0。
 * @return     无返回值；self 为 NULL 时保持原状态（分配失败同样保持
 *             原状态）。
 */
void XVirtualKeyboardTrace_setChannels(XVirtualKeyboardTrace* self,
                                       const char* const* channels,
                                       int count);

/**
 * @brief      返回点数（对标 length）。
 * @param      self 轨迹对象借用指针；可为 NULL。
 * @return     已采集点数；self 为 NULL 返回 0。
 */
int XVirtualKeyboardTrace_length(const XVirtualKeyboardTrace* self);

/**
 * @brief      取坐标段（对标 points(pos, count)）。
 * @param      self 目标对象；可为 NULL。
 * @param      pos 起始下标（<0 按 0）。
 * @param      count 取出条数（<0 表示到末尾）。
 * @param      outPoints 输出数组（调用方提供存储空间；可为 NULL 仅询数）。
 * @param      maxCount 输出数组容量。
 * @return     实际写出的点数。
 */
int XVirtualKeyboardTrace_points(const XVirtualKeyboardTrace* self, int pos,
                                 int count,
                                 XVirtualKeyboardTracePoint* outPoints,
                                 int maxCount);

/**
 * @brief      追加一点（对标 addPoint）。
 * @param      self 轨迹对象；可为 NULL。
 * @param      point 新采样点（面板局部坐标借用；不能为 NULL）。
 * @return     新点的下标；对象非法或分配失败返回 -1。
 */
int XVirtualKeyboardTrace_addPoint(XVirtualKeyboardTrace* self,
                                   const XVirtualKeyboardTracePoint* point);

/**
 * @brief      写通道数据（对标 setChannelData）。
 * @details    channel 未在通道集中时忽略；index 超过当前通道长度时中
 *             间补 0（与 Qt 稀疏通道语义等价的按需扩展）。
 * @param      self 轨迹对象；可为 NULL。
 * @param      channel 通道名（UTF-8 借用；不能为 NULL）。
 * @param      index 写入下标（0 基）。
 * @param      data 写入值。
 * @return     写入成功返回 true；通道不存在/入参非法/self 为 NULL 返
 *             回 false。
 */
bool XVirtualKeyboardTrace_setChannelData(XVirtualKeyboardTrace* self,
                                          const char* channel, int index,
                                          double data);

/**
 * @brief      读通道数据（对标 channelData(channel, pos, count)）。
 * @param      self 轨迹对象借用指针；可为 NULL。
 * @param      channel 通道名（UTF-8 借用；不能为 NULL）。
 * @param      pos 起始下标（<0 按 0）。
 * @param      count 取出条数（<0 表示到末尾）。
 * @param      outData 输出数组（调用方提供存储空间；可为 NULL 仅询数）。
 * @param      maxCount outData 容量上限。
 * @return     实际写出的数据条数；通道不存在或 self 为 NULL 返回 0。
 */
int XVirtualKeyboardTrace_channelData(const XVirtualKeyboardTrace* self,
                                      const char* channel, int pos,
                                      int count, double* outData,
                                      int maxCount);

/**
 * @brief      返回一笔是否结束（对标 isFinal）。
 * @param      self 轨迹对象借用指针；可为 NULL。
 * @return     已结束返回 true；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardTrace_isFinal(const XVirtualKeyboardTrace* self);
/**
 * @brief      设置一笔结束（对标 setFinal；变化发 finalChanged）。
 * @param      self 轨迹对象；可为 NULL，NULL 时不执行操作。
 * @param      isFinal true=本笔结束。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardTrace_setFinal(XVirtualKeyboardTrace* self,
                                    bool isFinal);

/**
 * @brief      返回轨迹是否作废（对标 isCanceled）。
 * @param      self 轨迹对象借用指针；可为 NULL。
 * @return     已作废返回 true；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardTrace_isCanceled(const XVirtualKeyboardTrace* self);
/**
 * @brief      设置轨迹作废（对标 setCanceled；变化发 canceledChanged）。
 * @param      self 轨迹对象；可为 NULL，NULL 时不执行操作。
 * @param      isCanceled true=轨迹作废。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardTrace_setCanceled(XVirtualKeyboardTrace* self,
                                       bool isCanceled);

/**
 * @brief      返回渲染透明度（对标 opacity）。
 * @param      self 轨迹对象借用指针；可为 NULL。
 * @return     透明度（[0,1]；默认 1）；self 为 NULL 返回 0。
 */
float XVirtualKeyboardTrace_opacity(const XVirtualKeyboardTrace* self);
/**
 * @brief      设置渲染透明度（对标 setOpacity；变化发 opacityChanged）。
 * @param      self 轨迹对象；可为 NULL，NULL 时不执行操作。
 * @param      opacity 新透明度（[0,1]，越界按边界截断）。
 * @return     无返回值；self 为 NULL 时保持原状态。
 */
void XVirtualKeyboardTrace_setOpacity(XVirtualKeyboardTrace* self,
                                      float opacity);

/**
 * @brief      启动延时隐藏（对标 startHideTimer，Qt REVISION(6,1)）。
 * @details    到期仅置内部隐藏过期标志（本实现无墨迹渲染层可通知）；
 *             再次调用重启计时。
 * @param      self 目标对象；可为 NULL。
 * @param      delayMs 延时毫秒；<=0 立即到期。
 */
void XVirtualKeyboardTrace_startHideTimer(XVirtualKeyboardTrace* self,
                                          int delayMs);
/**
 * @brief      查询延时隐藏是否已到期（XGui 扩展读数）。
 * @param      self 轨迹对象借用指针；可为 NULL。
 * @return     已到期返回 true；未启动或 self 为 NULL 返回 false。
 */
bool XVirtualKeyboardTrace_isHideExpired(const XVirtualKeyboardTrace* self);

/* ==================== 信号（6 个，对标 Qt 全部信号） ==================== */

/**
 * @brief      traceIdChanged(int traceId) 信号标识。
 * @param      self 轨迹对象借用指针；可为 NULL。
 * @param      traceId 仅占位（本 getter 不读取）。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardTrace_traceIdChanged_signal(
        XVirtualKeyboardTrace* self, int traceId);
/**
 * @brief      channelsChanged() 信号标识。
 * @param      self 轨迹对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardTrace_channelsChanged_signal(
        XVirtualKeyboardTrace* self);
/**
 * @brief      lengthChanged(int length) 信号标识。
 * @param      self 轨迹对象借用指针；可为 NULL。
 * @param      length 仅占位（本 getter 不读取）。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardTrace_lengthChanged_signal(
        XVirtualKeyboardTrace* self, int length);
/**
 * @brief      finalChanged(bool isFinal) 信号标识。
 * @param      self 轨迹对象借用指针；可为 NULL。
 * @param      isFinal 仅占位（本 getter 不读取）。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardTrace_finalChanged_signal(
        XVirtualKeyboardTrace* self, bool isFinal);
/**
 * @brief      canceledChanged(bool isCanceled) 信号标识。
 * @param      self 轨迹对象借用指针；可为 NULL。
 * @param      isCanceled 仅占位（本 getter 不读取）。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardTrace_canceledChanged_signal(
        XVirtualKeyboardTrace* self, bool isCanceled);
/**
 * @brief      opacityChanged(qreal opacity) 信号标识。
 * @param      self 轨迹对象借用指针；可为 NULL。
 * @param      opacity 仅占位（本 getter 不读取）。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardTrace_opacityChanged_signal(
        XVirtualKeyboardTrace* self, float opacity);

#endif /* XVIRTUALKEYBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDTRACE_H */
