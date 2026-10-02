#ifndef XS7TYPES_H
#define XS7TYPES_H

#include "XPlc_config.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XPLC_ON
#if XS7_ON

/**
 * @file XS7Types.h
 * @brief S7 公共类型与常量（被所有 S7 头共享，避免循环 include）
 * @details 区域枚举、值类型枚举、运行模式枚举、ROSCTR/功能码/传输大小常量、
 *          协商默认值与 Run/Stop 应答状态码。
 *
 * @par 常量冻结状态（M0 接口冻结说明）
 * - 已核验可冻结：Setup(0xF0)/Read(0x04)/Write(0x05)/Run(0x28)/Stop(0x29)
 *   （依据 s-pms g_s7_hot_start / g_s7_stop 金样与 S7NetPlus 同构核对）
 * - 待实测项：块服务功能码（0x12/0x13/0x14/0x1A/0x1B/0x1C/0x1E/0x1F/0x20 候选）、
 *   T/C 位访问与字访问编码归属（0x1C/0x1D vs 0x1E/0x1F）、V 区编码（0x84 映射）
 *   —— 阶段 0 须用 Wireshark s7comm 解码器或 snap7 抓包核对后方可写入本表。
 */

/******************************************************************************************
 * 枚举类型定义
 ******************************************************************************************/

 /**
  * @brief S7 访问区域枚举（线码）
  * @details 数值即报文 area 字节：I=0x81、Q=0x82、M=0x83、DB=0x84、
  *          T/C 采用 s-pms 字访问编码 0x1F/0x1E；V 按 s-pms 方式映射为 DB
  *          （线码同 0x84，块号由 dbNumber 指定，S7-200/SMART 场景）。
  * @note 待实测项：0x1C 在 S7NetPlus/s-pms 两参考中均为 Counter，
  *       "0x1C=V" 无参考实现支持；V/T/C 最终编码以阶段 0 目标 PLC 实测定案，
  *       本枚举保留 XS7Area_V 独立枚举名以便调整，地址语法（V100/T100）不受影响。
  */
typedef enum {
    XS7Area_C = 0x1E,   ///< 计数器区（字访问；位/字节访问归属待实测）
    XS7Area_T = 0x1F,   ///< 计时器区（字访问；0x1C/0x1D 归属待实测）
    XS7Area_I = 0x81,   ///< 输入过程映像区
    XS7Area_Q = 0x82,   ///< 输出过程映像区
    XS7Area_M = 0x83,   ///< 位存储区
    XS7Area_DB = 0x84,  ///< 数据块区
    XS7Area_V = 0x84    ///< V 区（S7-200/SMART）：线码同 DB=0x84，由 dbNumber 指定块号（待实测定案）
} XS7Area;

/**
 * @brief S7 值类型枚举（首期基本类型全集）
 * @details 与地址后缀（X/B/W/D/STRING）或调用参数对应，
 *          决定 ANY 传输大小与 XS7Value 编解码分支
 */
typedef enum {
    XS7Value_Bool,      ///< 位（bool，位传输）
    XS7Value_Byte,      ///< 字节（uint8）
    XS7Value_Word,      ///< 字（uint16）
    XS7Value_DWord,     ///< 双字（uint32）
    XS7Value_Real,      ///< 浮点（IEEE754 float，大端位型中转）
    XS7Value_String     ///< S7 STRING(n)（[maxLen][curLen][data...]，总长 n+2 偶数补齐）
} XS7ValueType;

/**
 * @brief PLC 运行模式枚举
 * @details 用于 Run（0x28）启动请求：热启动 / 冷启动
 */
typedef enum {
    XS7RunMode_Hot,     ///< 热启动（Hot Start）
    XS7RunMode_Cold     ///< 冷启动（Cold Start）
} XS7RunMode;

/******************************************************************************************
 * ROSCTR / 功能码常量（已核验，可冻结）
 ******************************************************************************************/

#define XS7_ROSCTR_JOB        0x01   ///< ROSCTR：Job（请求）
#define XS7_ROSCTR_ACK        0x02   ///< ROSCTR：Ack
#define XS7_ROSCTR_ACK_DATA   0x03   ///< ROSCTR：Ack_Data（应答，头长12）
#define XS7_ROSCTR_USERDATA   0x07   ///< ROSCTR：UserData（扩展功能）

#define XS7_FUNC_SETUP        0xF0   ///< 功能码：SetupCommunication（协商 AmQ/PDU 长度）
#define XS7_FUNC_READ         0x04   ///< 功能码：Read Var（读请求）
#define XS7_FUNC_WRITE        0x05   ///< 功能码：Write Var（写请求）
#define XS7_FUNC_RUN          0x28   ///< 功能码：PLC Run（已由 s-pms g_s7_hot_start 参数首字节核实）
#define XS7_FUNC_STOP         0x29   ///< 功能码：PLC Stop（已由 s-pms g_s7_stop 参数首字节核实）

/*
 * 块服务功能码候选（待实测，M0 不冻结为宏）：
 *   0x12(Open?) / 0x13 / 0x14 / 0x1A(ListBlocks?) / 0x1B / 0x1C / 0x1E / 0x1F / 0x20
 * 取自 snap7/S7comm 通行实现，两参考库均无完整块传输实现；
 * 阶段 0 须用 Wireshark s7comm 解码器或 snap7 抓包核对后，
 * 方可在本头补入 #define（XS7_BLOCK_ON 区）。
 */

/******************************************************************************************
 * 传输大小 / 数据区返回码常量
 ******************************************************************************************/

#define XS7_TRANSPORT_BIT       0x01  ///< 请求 ANY transport：位
#define XS7_TRANSPORT_BYTE      0x02  ///< 请求 ANY transport：字节/字
#define XS7_TRANSPORT_WORD      0x04  ///< 请求 ANY transport：字/双字/字节串
#define XS7_TRANSPORT_DATA_BIT  0x03  ///< 数据区 transport（请求数据/应答位数据）
#define XS7_TRANSPORT_DATA_BYTE 0x04  ///< 数据区 transport：字节串
#define XS7_TRANSPORT_DATA_OCTET 0x09 ///< 数据区 transport：八位组（计时/计数结构、STRING）

#define XS7_RETURN_OK           0xFF  ///< 读写应答返回码：成功
#define XS7_RETURN_DATA_ERROR   0x05  ///< 数据区错误（如地址越界，细节待实测核对）
#define XS7_RETURN_ACCESS_ERROR 0x06  ///< 数据区错误：访问拒绝（细节待实测核对）
#define XS7_RETURN_RANGE_ERROR  0x0A  ///< 数据区错误：超出范围（细节待实测核对）

/******************************************************************************************
 * 协商默认值 / Run-Stop 应答状态码
 ******************************************************************************************/

#define XS7_REQUESTED_PDU_LEN   960   ///< Setup 请求的 PDU 长度（S7NetPlus 同值；s-pms 用 480）
#define XS7_FALLBACK_PDU_LEN    240   ///< 协商失败或异常小时的兜底 PDU 长度
#define XS7_AMQ                 1     ///< 单 IP 最大并行任务数（首期 AmQ=1，发送侧必须串行）

#define XS7_CONTROL_STATUS_ALREADY_RUNNING  0x02  ///< Run 应答：已处于运行状态（视为成功，s-pms g_pdu_already_started）
#define XS7_CONTROL_STATUS_ALREADY_STOPPED  0x07  ///< Stop 应答：已处于停止状态（视为成功，s-pms g_pdu_already_stopped）

#endif /* XS7_ON */
#endif /* XPLC_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

#endif // XS7TYPES_H
