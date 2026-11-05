
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
#include "HsfServerIoTurbidity.h"
#include "HsfServerIoAnalog.h"
#include "HsfServerIoDigital.h"
#include "HsfServerIoValve.h"
#include "HsfServerIoPump.h"

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
 * @brief 设置是否走模拟设备（运行期开关；须在 HsfServerIo_start 之前调用）。
 * @param enable true=全部从站换成进程内寄存器模型。
 * @details [用途] 设备拆走/没接线/CI 环境里也能把整条 C 链路跑通。
 *          上层代码一行不改：Hub 的缓存块/读组合并/写重发、链路 role 映射、
 *          设备层状态字与故障表、电磁阀超时与离线判定全部跑真实实现，
 *          变的只有"电信号从哪来"。
 * @note  默认值取编译期 HSF_SIM_MODE。命令行 `--sim` 会转调本函数。
 * @warning 模拟模式下所有写操作只写内存模型，**不会驱动真实继电器**。
 */
void HsfServerIo_setSimMode(bool enable);

/**
 * @brief 当前是否处于模拟模式。
 */
bool HsfServerIo_simMode(void);

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

/**
 * @brief 取某侧第 index 台浊度仪站（借用；遥测取值/编码走它）。
 * @param side 侧号。
 * @param index 台号（0=浊度仪1，1=浊度仪2；范围 HSF_TURBIDITY_COUNT）。
 * @return 站指针；未启动/该台未挂返回 NULL。@warning 禁释放。
 * @details 逐台查询而不是一次给两台：现场两台是独立接线（站号 1/3），
 *          某台没接时另一台必须照常显示——调用方按 NULL 单独降级。
 */
const HsfServerIoTurbidity* HsfServerIo_turbidity(HsfSide side, int index);

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

/* ==================== 数字量模块（16DI/16DO，Modbus-TCP） ==================== */

/**
 * @brief 数字量站是否已装配。
 */
bool HsfServerIo_digitalReady(void);

/**
 * @brief 读 DI / DO 位图（bit n = 通道 n）。
 * @param outDi 输出 DI 位图（可为 NULL）；outDo 输出 DO 位图（可为 NULL）。
 * @param outValid 输出数据是否有效（可为 NULL）。
 * @return 至少一个位图有有效数据返回 true。
 */
bool HsfServerIo_digitalMasks(uint32_t* outDi, uint32_t* outDo, bool* outValid);

/**
 * @brief 写单路 DO（走 FC05 单点写；通道号按配置的起始地址换算）。
 * @param channel 通道号（0..channels-1）。
 * @param on 目标值。
 * @return 已受理返回 true。
 */
bool HsfServerIo_digitalWrite(int channel, bool on);

/** @brief 数字量站点数（未装配返回 0）。 */
int HsfServerIo_digitalCount(void);

/**
 * @brief 用当前 DI 刷新电磁阀组状态（含动作超时判定）。
 * @note  由服务端 tick 每拍调用；内部读 DI 位图 → poll →（可选）写动作。
 */
void HsfServerIo_valvePoll(uint64_t nowMs);

/**
 * @brief 电磁阀组读取入口（借用；未装配返回 NULL）。
 */
const HsfServerIoValve* HsfServerIo_valves(void);

/**
 * @brief 电磁阀控制：查配置 → 校验 → 写 DO。
 * @param index 路序。
 * @param wantOpen true=开阀。
 * @param outErr 失败原因（UTF-8；容量 cap）。
 * @return 已下发返回 true。
 * @note  动作出口是"单点写对应 DO"：开=写 1，关=写 0。
 *        反馈到位由 HsfServerIo_valvePoll 判读，本函数不阻塞等待。
 */
bool HsfServerIo_valveSet(int index, bool wantOpen, char* outErr, int cap);

/**
 * @brief 取走某一路**本拍新产生**的阀告警（取走即消费）。
 * @param index 路序（0..HSF_VALVE_COUNT-1）。
 * @param outKind 输出告警类型（HsfValveAlarmKind）。
 * @param outText 输出告警文字缓冲；可为 NULL。
 * @param cap outText 容量。
 * @return 有告警返回 true（并已消费该条）。
 * @note 告警是**事件**：一条只该推一次。调用方在每拍 valvePoll 之后遍历
 *       各路取一遍，取到就推 HSF_TOPIC_VALVE_ALARM。取走语义天然防重复，
 *       故这里无需调用方另记"已推过"。
 */
bool HsfServerIo_valveTakeAlarm(int index, int* outKind, char* outText, int cap);

/* ==================== 进水泵（1 台，只有输出、无反馈） ==================== */

/**
 * @brief 进水泵组读取入口（借用；未装配返回 NULL）。
 */
const HsfServerIoPump* HsfServerIo_pumps(void);

/**
 * @brief 用当前 DO 回显刷新泵组命令状态（含外部改动接管）。
 * @note  由服务端 tick 每拍调用（先 poll 再编码，同一拍数据自洽）。
 */
void HsfServerIo_pumpPoll(void);

/**
 * @brief 进水泵控制：查配置 → 校验（幂等/离线/未配置）→ 写 DO。
 * @param index 泵号（0 起）。
 * @param on true=启动（接触器吸合）。
 * @param outErr 失败原因（UTF-8；容量 cap；"已处于该状态"属幂等成功，
 *               本函数返回 true 且不再写）。
 * @param cap outErr 容量。
 * @return 受理返回 true（含幂等"已是该状态"）。
 */
bool HsfServerIo_pumpSet(int index, bool on, char* outErr, int cap);

/* ==================== 诊断 ==================== */
/**
 * @brief 取某路总线的 Hub（借用；缓存读/抓帧排查用）。@warning 禁释放。
 */
struct XModbusHub* HsfServerIo_busHub(HsfSide side);

/**
 * @brief 取某路总线在 Hub 里的设备句柄（统计口径用；未装配/未就绪返回 -1）。
 */
int HsfServerIo_busDeviceId(HsfSide side);

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
