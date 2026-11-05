/**
 * @file       XAutoControlCodec.h
 * @brief      自动运行控制器状态/报警编解码公开 API：运行快照与告警 ↔ JSON/BSON。
 * @details    对齐 Qt 无直接对应类（服务网络传输的序列化胶水层），与
 *             XSolenoidValveCodec / XVfdCodec 同一套约定：
 *               - 键列表裁剪：调用方给出关心的 XAutoControlField 数组，
 *                 输出只含这些键；
 *               - JSON 与 BSON 键名一致（同一张映射表），格式可互转；
 *               - 报警是事件帧：一条一帧（kind/atMs/text 三键恒全量），
 *                 无键裁剪——远端消费一条报警不需要"只要一半"。
 *             解码产出值结构（XAutoControlStatus / XAutoControlAlarm，
 *             零分配），供远端镜像/记录回放使用。典型用途：自动运行的
 *             状态与告警经 XLink 推送，远端客户端远程管理（订阅告警、
 *             展示运行态、启停自动运行）。
 */
#ifndef XAUTOCONTROLCODEC_H
#define XAUTOCONTROLCODEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"

#if XINDUSTRY_ON && XAUTO_CONTROL_ON

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "XByteArray.h"
#include "XAutoControl.h"

/* ========================================================================== */
/*                      状态字段键                                            */
/* ========================================================================== */

/**
 * @brief 运行快照字段键（键列表元素；数值稳定，线上键名由实现映射表决定）。
 */
typedef enum XAutoControlField {
    XAutoControlField_None = 0,             /**< 占位（禁用）。 */
    XAutoControlField_Running = 1 << 0,     /**< 是否运行（"running"；bool）。 */
    XAutoControlField_Phase = 1 << 1,       /**< 相位名（"phase"；utf8）。 */
    XAutoControlField_Actions = 1 << 2,     /**< 累计动作数（"actions"；int64）。 */
    XAutoControlField_LastError = 1 << 3,   /**< 最近失败原因（"lastError"；utf8）。 */
    XAutoControlField_StepIntervalMs = 1 << 4, /**< 步进间隔（"stepIntervalMs"；int32 毫秒）。 */
    XAutoControlField_PendingAlarms = 1 << 5,  /**< 待取报警数（"pendingAlarms"；int32）。 */
    XAutoControlField_State = 1 << 6        /**< 状态（"state"；int，XAutoControlState，含急停锁存）。 */
} XAutoControlField;

/* ========================================================================== */
/*                      状态编码（控制器 → 载荷）                              */
/* ========================================================================== */

/**
 * @brief 运行快照 → BSON 载荷（按键列表裁剪）。
 * @param st 源快照（借用；NULL 视为全零快照）。
 * @param keys 键列表（借用；NULL 时输出空文档）。
 * @param count 键数量；负数按 0 处理。
 * @return 新建 XByteArray（BSON 文档字节）；分配失败返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 */
XByteArray* XAutoControlCodec_statusToBson(const XAutoControlStatus* st,
                                           const XAutoControlField* keys, int count);

/**
 * @brief 运行快照 → JSON 载荷（紧凑 UTF-8；键名与 BSON 一致）。
 * @return 新建 XByteArray；分配失败返回 NULL。@warning 调用方释放。
 */
XByteArray* XAutoControlCodec_statusToJson(const XAutoControlStatus* st,
                                           const XAutoControlField* keys, int count);

/**
 * @brief 全部快照键的现成列表。
 * @param outCount 输出键数量（可为 NULL——此时返回 NULL）。
 * @return 静态常量数组（借用，禁释放）。
 */
const XAutoControlField* XAutoControlCodec_allKeys(int* outCount);

/* ========================================================================== */
/*                      状态解码（载荷 → 快照）                                */
/* ========================================================================== */

/**
 * @brief BSON 载荷 → 运行快照（就地填充）。
 * @param out 输出快照（调用方提供存储；先整体清零再填充）。
 * @param data BSON 文档字节（借用；调用期间有效）。
 * @param size 字节数。
 * @return 解析成功返回 true；data 为 NULL、size 为 0 或非 BSON 文档返回
 *         false（out 已清零）。未提供的键保持零值。
 */
bool XAutoControlCodec_statusFromBson(XAutoControlStatus* out,
                                      const uint8_t* data, size_t size);

/**
 * @brief JSON 载荷 → 运行快照（就地填充）。
 * @param out 输出快照。
 * @param data UTF-8 JSON 文本字节（借用）。
 * @param size 字节数。
 * @return 解析成功返回 true；非法 JSON / 顶层非对象返回 false。
 */
bool XAutoControlCodec_statusFromJson(XAutoControlStatus* out,
                                      const uint8_t* data, size_t size);

/* ========================================================================== */
/*                      报警编解码（事件帧；一条一帧恒全量）                    */
/* ========================================================================== */

/**
 * @brief 报警 → BSON 事件帧（kind/atMs/text 三键恒全量）。
 * @param alarm 源报警（借用；NULL 视为全零报警）。
 * @return 新建 XByteArray；分配失败返回 NULL。@warning 调用方释放。
 */
XByteArray* XAutoControlCodec_alarmToBson(const XAutoControlAlarm* alarm);

/**
 * @brief 报警 → JSON 事件帧（紧凑 UTF-8；键名与 BSON 一致）。
 * @return 新建 XByteArray；分配失败返回 NULL。@warning 调用方释放。
 */
XByteArray* XAutoControlCodec_alarmToJson(const XAutoControlAlarm* alarm);

/**
 * @brief BSON 事件帧 → 报警（就地填充）。
 * @param out 输出报警（调用方提供存储）。
 * @param data BSON 文档字节。
 * @param size 字节数。
 * @return 解析成功返回 true；非法输入返回 false（out 已清零）。
 */
bool XAutoControlCodec_alarmFromBson(XAutoControlAlarm* out,
                                     const uint8_t* data, size_t size);

/**
 * @brief JSON 事件帧 → 报警（就地填充）。
 * @return 解析成功返回 true；非法输入返回 false（out 已清零）。
 */
bool XAutoControlCodec_alarmFromJson(XAutoControlAlarm* out,
                                     const uint8_t* data, size_t size);

#endif /* XINDUSTRY_ON && XAUTO_CONTROL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XAUTOCONTROLCODEC_H */
