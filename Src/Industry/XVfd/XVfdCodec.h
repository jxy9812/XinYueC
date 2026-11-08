
/**
 * @file       XVfdCodec.h
 * @brief      变频器状态/控制编解码公开 API：XVfdStatus 与控制请求 ↔ JSON/BSON。
 * @details    对齐 Qt 无直接对应类（服务网络传输的序列化胶水层）。
 *             本模块把 XVfdDevice 的状态快照（XVfdStatus）与控制意图
 *             （XVfdControlRequest）编码为 JSON 文本或 BSON 二进制
 *             （XByteArray 载荷，可直接交给 XLink 的
 *             XLinkDataType_Json / XLinkDataType_Bson 通道），并提供
 *             反向解码。编码按**键列表**裁剪：调用方给出关心的
 *             XVfdStatusField 数组，输出只含这些键（BSON/JSON 文档体积
 *             与轮询带宽都按需收敛）。
 *
 *             [键的口径] 键 = XVfdStatusField 的**位值**（uint64_t，
 *             即 m_validMask 里的位；MSVC C 模式下高位枚举常量会截断，
 *             故键类型用 uint64_t，调用方可用 (uint64_t)XVfdField_X
 *             或 (1ULL << n) 表达），线上的字符串键名由本模块的静态
 *             映射表唯一决定（如位 11 ↔ "outputFreqHz"）。有效掩码随
 *             载荷以 "vm" 键传输，解码方据它判断字段有效性。
 *
 *             [控制口径] 控制请求只编码「意图」，动作在设备上由
 *             XVfdCodec_applyControl 分派；applyControl 返回 true 仅代表
 *             设备受理（XVfdDevice 全异步口径），结果经状态遥测回传。
 *
 *             线程模型：纯函数、零共享状态，任意线程可用；但 XVfdDevice
 *             本身保持单线程事件循环口径（applyControl 须在设备线程调）。
 */
#ifndef XVFDCODEC_H
#define XVFDCODEC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"

#if XVFD_ON

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "XVfdDevice.h"

/* ========================================================================== */
/*                      控制请求                                              */
/* ========================================================================== */

/**
 * @brief 控制请求（值语义，零分配；编码/解码与 applyControl 的公共载荷）。
 * @details 字段是全部可编码动作的超集：编码时只写与 m_op 相关的字段，
 *          其余保持 0；解码时未提供的键保持 0。
 */
typedef struct XVfdControlRequest {
    XVfdOperation m_op;             /**< 动作类型（XVfdOperation；支持子集见 XVfdCodec_applyControl）。 */
    bool m_reverse;                 /**< 方向（Start/Jog：true=反转）。 */
    double m_valueHz;               /**< 频率给定（Hz；SetFrequency）。 */
    double m_valueRpm;              /**< 转速给定（rpm；SetSpeed）。 */
    double m_value;                 /**< 通用数值（SetPidTarget 等）。 */
    int32_t m_index;                /**< 序号（段速/加减速组等）。 */
} XVfdControlRequest;

/* ========================================================================== */
/*                      状态编码（设备 → 载荷）                                */
/* ========================================================================== */

/**
 * @brief 状态快照 → BSON 载荷（按键列表裁剪）。
 * @param status 状态快照（借用；可为 NULL——输出仅含 "vm"=0 的空文档）。
 * @param keys 键列表（validMask 位值数组，借用；NULL 时输出仅含 "vm"）。
 * @param count 键数量；负数按 0 处理。
 * @return 新建 XByteArray（BSON 文档字节）；内存分配失败返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 * @note 文档恒含 "vm"（有效掩码，int64）；仅当 (m_validMask & key) 非 0
 *       的键才写入对应条目——品牌未提供的字段不上线，解码方按掩码判无效。
 */
XByteArray* XVfdCodec_statusToBson(const XVfdStatus* status,
                                   const uint64_t* keys, int count);

/**
 * @brief 状态快照 → JSON 载荷（按键列表裁剪；紧凑 UTF-8 文本）。
 * @param status 状态快照（借用；可为 NULL）。
 * @param keys 键列表（借用；可为 NULL）。
 * @param count 键数量。
 * @return 新建 XByteArray（不含终止符的 UTF-8 JSON 文本字节）；分配失败返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 * @note 键名与 BSON 版本完全一致（同一张映射表），两格式载荷可互转。
 */
XByteArray* XVfdCodec_statusToJson(const XVfdStatus* status,
                                   const uint64_t* keys, int count);

/* ========================================================================== */
/*                      状态解码（载荷 → 快照）                                */
/* ========================================================================== */

/**
 * @brief BSON 载荷 → 状态快照（就地填充）。
 * @param out 输出快照（调用方提供存储；先整体清零再填充）。
 * @param data BSON 文档字节（借用；只要求在调用期间有效）。
 * @param size 字节数。
 * @return 解析并填充成功返回 true；data 为 NULL、size 不足或非 BSON 文档
 *         返回 false（out 已被清零）。
 * @note m_validMask 取自 "vm"，并与载荷中实际出现的字段位取并——即使发送
 *       方漏置位，解码方也按「载荷里有」认定有效。m_valid = (掩码 != 0)。
 */
bool XVfdCodec_statusFromBson(XVfdStatus* out, const uint8_t* data, size_t size);

/**
 * @brief BSON 载荷 → 状态快照，并回传**侧别**（多侧聚合场景专用）。
 * @param out 输出快照（调用方提供存储；先整体清零再填充）。
 * @param outSide 输出侧别（`HSF_SIDE_A`/`HSF_SIDE_B`…）；可为 NULL。
 *                载荷缺 "side" 键时按 `HSF_SIDE_A` 处理（与协议缺省一致）。
 * @param data BSON 文档字节（借用；只要求在调用期间有效）。
 * @param size 字节数。
 * @return 解析并填充成功返回 true。
 * @details [为什么需要它] 服务端按「主题 × 侧」各推一帧，而客户端把同一主题
 *          的所有侧都写进**同一个** XVfdStatus 并逐帧通知订阅者 ⇒
 *          (a) 界面显示的数值在两侧之间每 100ms 交替（两侧读数不同时肉眼
 *              可见跳变）；
 *          (b) 推送帧数翻倍、重绘帧数随之翻倍（实测 VFD 页 28fps）。
 *          调用方拿到侧别后即可「只接受正在显示的那一侧」，两个问题一并解决。
 *          旧入口 `XVfdCodec_statusFromBson` 语义不变（内部转调本函数，丢弃侧别）。
 */
bool XVfdCodec_statusFromBson_2(XVfdStatus* out, int* outSide,
                                const uint8_t* data, size_t size);

/**
 * @brief JSON 载荷 → 状态快照（就地填充；键名与 BSON 版本一致）。
 * @param out 输出快照。
 * @param data UTF-8 JSON 文本字节（借用；不含终止符也可，含亦容忍）。
 * @param size 字节数。
 * @return 解析并填充成功返回 true；非法 JSON / 顶层非对象返回 false。
 */
bool XVfdCodec_statusFromJson(XVfdStatus* out, const uint8_t* data, size_t size);

/* ========================================================================== */
/*                      控制编解码与执行                                       */
/* ========================================================================== */

/**
 * @brief 控制请求 → BSON 载荷。
 * @param req 控制请求（借用；不能为 NULL）。
 * @return 新建 XByteArray；分配失败或 req 为 NULL 返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 * @note 文档恒含 "op"（XVfdOperation，int32）；其余键只在与 m_op 相关时
 *       写入（如 SetFrequency 写 "hz"）。
 */
XByteArray* XVfdCodec_controlToBson(const XVfdControlRequest* req);

/**
 * @brief 控制请求 → JSON 载荷（紧凑 UTF-8；键名与 BSON 一致）。
 * @param req 控制请求（借用；不能为 NULL）。
 * @return 新建 XByteArray；分配失败或 req 为 NULL 返回 NULL。
 *         @warning 返回值归调用方所有，用 XClassDelete 释放。
 */
XByteArray* XVfdCodec_controlToJson(const XVfdControlRequest* req);

/**
 * @brief BSON 载荷 → 控制请求（就地填充）。
 * @param out 输出请求（调用方提供存储；先整体清零再填充）。
 * @param data BSON 文档字节（借用）。
 * @param size 字节数。
 * @return 解析成功且 "op" 为已知动作返回 true；否则返回 false（out 已清零）。
 */
bool XVfdCodec_controlFromBson(XVfdControlRequest* out, const uint8_t* data, size_t size);

/**
 * @brief JSON 载荷 → 控制请求（就地填充）。
 * @param out 输出请求。
 * @param data UTF-8 JSON 文本字节（借用）。
 * @param size 字节数。
 * @return 解析成功且 "op" 为已知动作返回 true；否则返回 false。
 */
bool XVfdCodec_controlFromJson(XVfdControlRequest* out, const uint8_t* data, size_t size);

/**
 * @brief 把控制请求分派到设备执行（同步受理口径）。
 * @param dev 目标设备（借用；不能为 NULL）。
 * @param req 控制请求（借用；不能为 NULL）。
 * @return 设备受理（动作已下发/入队）返回 true；动作不支持、控制闸关闭
 *         或设备拒绝返回 false。
 * @note 受理 ≠ 完成：XVfdDevice 全异步，结果经状态遥测与 commandCompleted
 *       信号回传。支持的动作：Start/Stop/FreeStop/QuickStop/Jog/ResetFault/
 *       SetFrequency/SetSpeed；其余动作返回 false。
 */
bool XVfdCodec_applyControl(XVfdDevice* dev, const XVfdControlRequest* req);

/* ========================================================================== */
/*                      键列表助手                                            */
/* ========================================================================== */

/**
 * @brief 取「页面遥测」缺省键列表（监视页渲染所需的字段子集）。
 * @param outCount 输出键数量（调用方提供 int 存储；可为 NULL——此时返回 NULL）。
 * @return 静态常量数组（借用，禁释放）；outCount 为 NULL 返回 NULL。
 * @note 覆盖：运行状态族（状态/运行/故障/方向/点动/通讯）、频率族（给定/
 *       输出）、电压/电流/母线/功率/转矩/转速、故障码。
 */
const uint64_t* XVfdCodec_telemetryKeys(int* outCount);

#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVFDCODEC_H */
