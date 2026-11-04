
/**
 * @file       XSolenoidValveCodec.h
 * @brief      电磁阀状态/控制编解码公开 API：阀位快照与开/关命令 ↔ JSON/BSON。
 * @details    对齐 Qt 无直接对应类（服务网络传输的序列化胶水层），与
 *             XVfdCodec（变频器侧）同一套约定：
 *               - 键列表裁剪：调用方给出关心的 XSolenoidValveField 数组，
 *                 输出只含这些键；
 *               - JSON 与 BSON 键名一致（同一张映射表），格式可互转；
 *               - 控制只编码「意图」（开/关），动作由
 *                 XSolenoidValveCodec_applyControl 分派到阀对象，
 *                 受理 ≠ 到位（到位经 stateChanged/opened/closed 信号）。
 *             快照解码产出 XSolenoidValveSnapshot 值结构（零分配），
 *             供远端镜像/记录回放使用。
 */
#ifndef XSOLENOIDVALVECODEC_H
#define XSOLENOIDVALVECODEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"

#if XINDUSTRY_ON && XSOLENOID_VALVE_ON

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "XByteArray.h"
#include "XSolenoidValve.h"

/* ========================================================================== */
/*                      快照字段键                                            */
/* ========================================================================== */

/**
 * @brief 阀位快照字段键（键列表元素；数值稳定，线上键名由实现映射表决定）。
 */
typedef enum XSolenoidValveField {
    XSolenoidValveField_None = 0,           /**< 占位（禁用）。 */
    XSolenoidValveField_State = 1 << 0,     /**< 阀位状态（"state"；int，XSolenoidValveState）。 */
    XSolenoidValveField_Open = 1 << 1,      /**< 是否开到位（"open"；bool）。 */
    XSolenoidValveField_Closed = 1 << 2,    /**< 是否关到位（"closed"；bool）。 */
    XSolenoidValveField_FeedbackMode = 1 << 3, /**< 到位反馈口径（"mode"；int）。 */
    XSolenoidValveField_TimeoutMs = 1 << 4  /**< 监督超时（"timeoutMs"；int32 毫秒）。 */
} XSolenoidValveField;

/**
 * @brief 阀位快照（值语义，零分配；解码产物）。
 * @details 字段有效性由解码方按需判断（状态恒有值；未提供的键保持 0）。
 */
typedef struct XSolenoidValveSnapshot {
    uint8_t m_state;                /**< 阀位状态（XSolenoidValveState）。 */
    bool m_open;                    /**< 开到位（等价 state==Open）。 */
    bool m_closed;                  /**< 关到位（等价 state==Closed）。 */
    uint8_t m_feedbackMode;         /**< 到位反馈口径（XSolenoidValveFeedbackMode）。 */
    uint32_t m_timeoutMs;           /**< 在途动作监督超时（毫秒；0=不监督）。 */
} XSolenoidValveSnapshot;

/* ========================================================================== */
/*                      状态编码（阀 → 载荷）                                  */
/* ========================================================================== */

/**
 * @brief 阀位快照 → BSON 载荷（按键列表裁剪）。
 * @param valve 源阀（借用；NULL 视为全零快照）。
 * @param keys 键列表（借用；NULL 时输出空文档）。
 * @param count 键数量；负数按 0 处理。
 * @return 新建 XByteArray（BSON 文档字节）；分配失败返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 */
XByteArray* XSolenoidValveCodec_stateToBson(const XSolenoidValve* valve,
                                            const XSolenoidValveField* keys, int count);

/**
 * @brief 阀位快照 → JSON 载荷（紧凑 UTF-8；键名与 BSON 一致）。
 * @param valve 源阀（借用；可为 NULL）。
 * @param keys 键列表（借用；可为 NULL）。
 * @param count 键数量。
 * @return 新建 XByteArray；分配失败返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 */
XByteArray* XSolenoidValveCodec_stateToJson(const XSolenoidValve* valve,
                                            const XSolenoidValveField* keys, int count);

/**
 * @brief 全部快照键的现成列表（state/open/closed/mode/timeoutMs）。
 * @param outCount 输出键数量（可为 NULL——此时返回 NULL）。
 * @return 静态常量数组（借用，禁释放）。
 */
const XSolenoidValveField* XSolenoidValveCodec_allKeys(int* outCount);

/* ========================================================================== */
/*                      状态解码（载荷 → 快照）                                */
/* ========================================================================== */

/**
 * @brief BSON 载荷 → 阀位快照（就地填充）。
 * @param out 输出快照（调用方提供存储；先整体清零再填充）。
 * @param data BSON 文档字节（借用；调用期间有效）。
 * @param size 字节数。
 * @return 解析成功返回 true；data 为 NULL、size 为 0 或非 BSON 文档返回
 *         false（out 已清零）。
 */
bool XSolenoidValveCodec_stateFromBson(XSolenoidValveSnapshot* out,
                                       const uint8_t* data, size_t size);

/**
 * @brief JSON 载荷 → 阀位快照（就地填充）。
 * @param out 输出快照。
 * @param data UTF-8 JSON 文本字节（借用）。
 * @param size 字节数。
 * @return 解析成功返回 true；非法 JSON / 顶层非对象返回 false。
 */
bool XSolenoidValveCodec_stateFromJson(XSolenoidValveSnapshot* out,
                                       const uint8_t* data, size_t size);

/* ========================================================================== */
/*                      控制编解码与执行                                       */
/* ========================================================================== */

/**
 * @brief 开/关命令 → BSON 载荷（"op" = "open"/"close"）。
 * @param open true=开阀，false=关阀。
 * @return 新建 XByteArray；分配失败返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 */
XByteArray* XSolenoidValveCodec_controlToBson(bool open);

/**
 * @brief 开/关命令 → JSON 载荷（紧凑 UTF-8；键名与 BSON 一致）。
 * @param open true=开阀，false=关阀。
 * @return 新建 XByteArray；分配失败返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 */
XByteArray* XSolenoidValveCodec_controlToJson(bool open);

/**
 * @brief 把控制载荷分派到阀执行（同步受理口径）。
 * @param valve 目标阀（借用；不能为 NULL）。
 * @param data 控制载荷字节（BSON 或 JSON 均可，自动识别失败即拒绝；借用）。
 * @param size 字节数。
 * @param outOpen 输出实际执行的方向（true=开；可为 NULL）。
 * @return 阀受理（open/close 返回 true）返回 true；载荷非法或方向未知
 *         返回 false。
 * @note 受理 ≠ 到位：到位经阀的 stateChanged/opened/closed 信号发布。
 */
bool XSolenoidValveCodec_applyControl(XSolenoidValve* valve,
                                      const uint8_t* data, size_t size,
                                      bool* outOpen);

#endif /* XINDUSTRY_ON && XSOLENOID_VALVE_ON */

#ifdef __cplusplus
}
#endif
#endif /* XSOLENOIDVALVECODEC_H */
