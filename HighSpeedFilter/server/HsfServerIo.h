
/**
 * @file       HsfServerIo.h
 * @brief      服务端工业 IO 门面：多路 485 总线 + 模拟量站的统一编排与查询。
 * @details    [两路 485] 现场两路相互独立的 485 各自采集一侧，模块化拆分：
 *               io/HsfServerBus       —— 一路串口 = 一个 Hub + RTU 客户端
 *               io/HsfServerIoVfd     —— 变频器站（品牌装配 + 通道组 + 设备）
 *               io/HsfServerIoTempHum —— 温湿度站（FC04 ×2 + 负温解码）
 *               io/HsfServerIoAnalog  —— 模拟量站（TCP 8 路 4-20mA）
 *             本文件只做**编排**：按配置宏决定启哪几路、每路挂哪些站、
 *             装配顺序（open → 各站 mount → start）与反向拆除顺序。
 *
 *             [为什么每路一个 Hub] XModbusHub 内部按「统一虚拟地址空间」
 *             编址。两路若共用一个 Hub，两边的输入寄存器 0x0000 段必然
 *             撞车（XModbusMapping 拒绝重叠段），且单飞轮询互相排队。
 *             每路一个 Hub ⇒ 每路一套独立地址空间，同型号站点的装配参数
 *             可以完全镜像。
 *
 *             [单路内多从站] 同一条 485 上变频器（站 2）与温湿度（站 4）
 *             共用该路的 Hub——各自独立的映射段 + 通道组 + 链路，靠站号
 *             与虚拟地址段区分：变频器用品牌恒等映射（0x1000/0x3000/0xF0xx
 *             等段），温湿度用低段 HSF_TEMPHUM_VADDR（0x0100 起），互不重叠。
 *
 *             线程模型：单线程事件循环；全部 API 须在 XCoreApplication
 *             事件循环内调用。
 */
#ifndef HSFSERVERIO_H
#define HSFSERVERIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>
#include <stdint.h>

#include "HsfServerConfig.h"
#include "HsfServerBus.h"
#include "HsfServerIoVfd.h"
#include "HsfServerIoTempHum.h"
#include "HsfServerIoFlowMeter.h"
#include "HsfServerIoAnalog.h"

/** @brief 现场侧标识（两路 485 各管一侧；遥测/命令按侧寻址）。 */
typedef enum HsfSide {
    HsfSide_A = 0,      /**< A 侧（路 A 总线）。 */
    HsfSide_B = 1,      /**< B 侧（路 B 总线，预留）。 */
    HsfSide_Count = 2   /**< 侧数（数组边界，不是合法侧号）。 */
} HsfSide;

/**
 * @brief 启动 IO 装配（按配置宏逐路建总线 + 各站 + 轮询）。
 * @return 至少一路装配成功返回 true；全部失败返回 false（错误经日志输出）。
 * @note  幂等保护：已启动时直接返回 true。串口打不开（适配器缺失/被占用）
 *        只跳过该路，不阻断其它路与模拟量站。
 */
bool HsfServerIo_start(void);

/**
 * @brief 停止并拆除全部装配（幂等；之后可重新 start）。
 */
void HsfServerIo_stop(void);

/* ==================== 按侧查询 ==================== */

/**
 * @brief 某侧是否已装配（该路总线打开且至少一个站挂上）。
 * @param side 侧号。
 */
bool HsfServerIo_sideReady(HsfSide side);

/**
 * @brief 取某侧的变频器设备（借用；控制经 XVfdDevice 动作 API 或 XVfdCodec）。
 * @param side 侧号。
 * @return 设备指针；该侧未装配或未挂变频器返回 NULL。@warning 禁释放。
 */
struct XVfdDevice* HsfServerIo_vfdDevice(HsfSide side);

/**
 * @brief 某侧变频器链路是否在线（Hub 层设备状态口径）。
 * @param side 侧号。
 */
bool HsfServerIo_vfdOnline(HsfSide side);

/**
 * @brief 取某侧温湿度站（借用；遥测取值/编码走它）。
 * @param side 侧号。
 * @return 站指针；未装配返回 NULL。@warning 禁释放。
 */
const HsfServerIoTempHum* HsfServerIo_tempHum(HsfSide side);

/**
 * @brief 取某侧电磁流量计站（借用；遥测取值/编码走它）。
 * @param side 侧号。
 * @return 站指针；未装配返回 NULL。@warning 禁释放。
 */
const HsfServerIoFlowMeter* HsfServerIo_flowMeter(HsfSide side);

/* ==================== 兼容单侧查询（A 侧） ==================== */

/**
 * @brief 查变频器故障码的品牌中文文本（与具体侧无关，品牌函数）。
 * @param code 故障码（品牌原值）。
 * @return 静态常量文本（禁释放）；未知码返回 NULL。
 */
const char* HsfServerIo_vfdFaultText(uint16_t code);

/* ==================== 模拟量站（与 485 无关） ==================== */

/**
 * @brief 读全部模拟量通道（工程值换算后）。
 * @param outMA 输出数组（调用方提供，至少 HSF_AI_CHANNELS 个；mA）。
 * @param outValid 输出各通道有效标志（调用方提供；可为 NULL）。
 * @param maxCount 输出数组容量。
 * @return 实际写入的通道数（0=未装配）。
 */
int HsfServerIo_aiValues(double* outMA, bool* outValid, int maxCount);

/**
 * @brief 读全部模拟量通道原始寄存器值（毫伏口径，排查真相用）。
 * @param outRaw 输出数组（调用方提供）。
 * @param outValid 输出各通道有效标志（可为 NULL）。
 * @param maxCount 输出数组容量。
 * @return 实际写入的通道数（0=未装配）。
 */
int HsfServerIo_aiRawValues(int32_t* outRaw, bool* outValid, int maxCount);

/* ==================== 诊断 ==================== */

/**
 * @brief 取某路总线的 Hub（借用；缓存读/抓帧排查用）。@warning 禁释放。
 */
struct XModbusHub* HsfServerIo_busHub(HsfSide side);

/**
 * @brief 某侧 485 总线是否通讯在线（该路 Hub 的串口设备处于 Connected）。
 * @param side 侧号。
 * @return 在线返回 true；未装配/串口断开返回 false。
 * @note 用总线自己的设备句柄（不是硬编码 0），避免误报"串口断开"。
 */
bool HsfServerIo_busOnline(HsfSide side);

/**
 * @brief 取模拟量站的 Hub（借用）。@warning 禁释放。
 */
struct XModbusHub* HsfServerIo_hub(void);

/**
 * @brief 直读某侧总线 Hub 缓存的保持寄存器字（诊断用）。
 * @return 值；未装配或未命中返回 -1。
 */
int HsfServerIo_debugHubHr(HsfSide side, uint16_t vaddr);

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERIO_H */
