/**
 * @file       XVfdM512.h
 * @brief      511M/512M/513M 系列高性能矢量变频器子类公开 API。
 * @details    XVfdDevice/XVfdModbusBase 的 511M/512M/513M 实现（依据
 *             《511M/512M/513M 高性能矢量变频器使用说明书》2020-05-03 附录1）：
 *             标准 Modbus-RTU 从站，仅 FC03/FC06；地址编排 P0~PE 组=F0xxH(写
 *             EEPROM)/0xxxH(仅 RAM)、A0~AC 组=Ax xxH/4x xxH、U 组监视=7000H+序号；
 *             特殊地址 2000H 命令字（1正转/2反转/3正点动/4反点动/5自由停机/
 *             6减速停机/7故障复位）、3000H 运行状态、8000H 故障码、1000H 通讯
 *             设定值（±10000=±100%，经最大频率 P0.10 换算 Hz）、2001H 数字输出
 *             强制位图、1F00H 密码校验（回 8888H=通过）、1F01H 参数初始化。
 *             非标准应答模式（Pd.05=0，应答多 1 字节）以双长度解析兼容。
 */
#ifndef XVFDM512_H
#define XVFDM512_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XVfd_config.h"

#if XVFD_ON
#if XVFD_M512_ON

#include <stdint.h>
#include <stdbool.h>
#include "XVfdModbusBase.h"

/* ========================================================================== */
/*                特殊地址（附录1：非功能码数据 / 快捷遥测）                   */
/* ========================================================================== */

#define XVfdM512_AddrCmdWord        0x2000  /**< 控制命令（写 1/2/3/4/5/6/7） */
#define XVfdM512_AddrRunState       0x3000  /**< 运行状态（1正转 2反转 3停机） */
#define XVfdM512_AddrFault          0x8000  /**< 故障码（Err01~51 → 0001~0033…） */
#define XVfdM512_AddrFreqSet        0x1000  /**< 通讯设定值（±10000 = ±100.00%） */
#define XVfdM512_AddrDoForce        0x2001  /**< 数字输出端子强制位图（BIT0=DO1…） */
#define XVfdM512_AddrPassword       0x1F00  /**< 密码校验（写入后回 8888H=通过） */
#define XVfdM512_AddrParamInit      0x1F01  /**< 参数初始化（1/2 恢复出厂 3清记录 4备份 501 恢复备份） */
#define XVfdM512_AddrTelemetry      0x1001  /**< 快捷遥测块起点：运行频率(0.01Hz) */
#define XVfdM512_TelemetryCount     6       /**< 1001~1006：频率/母线电压/输出电压/电流/功率/转矩 */
#define XVfdM512_AddrJogFreq        0xF800  /**< P8.00 点动运行频率（写 EEPROM） */
#define XVfdM512_AddrMaxFreq        0xF00A  /**< P0.10 最大频率（Hz） */
#define XVfdM512_AddrAccelTime      0xF011  /**< P0.17 加速时间 1 */
#define XVfdM512_AddrDecelTime      0xF012  /**< P0.18 减速时间 1 */
#define XVfdM512_AddrPresetBase     0xFC00  /**< PC.00 多段指令 0（-10000~10000 = ±100%） */

/**
 * @brief 控制命令字（2000H 写入值，附录1 控制命令通讯地址表）。
 */
typedef enum XVfdM512_Command {
    XVfdM512_CmdRunForward = 1,     /**< 正转运行 */
    XVfdM512_CmdRunReverse = 2,     /**< 反转运行 */
    XVfdM512_CmdJogForward = 3,     /**< 正转点动 */
    XVfdM512_CmdJogReverse = 4,     /**< 反转点动 */
    XVfdM512_CmdFreeStop = 5,       /**< 自由停机 */
    XVfdM512_CmdDecelStop = 6,      /**< 减速停机 */
    XVfdM512_CmdFaultReset = 7      /**< 故障复位 */
} XVfdM512_Command;

/**
 * @brief 511M/512M/513M 子类结构体（继承 XVfdModbusBase）。
 */
XCLASS_DEFINE_BEGING(XVfdM512)
XCLASS_DEFINE_EXTEND_END(XVfdM512, XVfdModbusBase)

typedef struct XVfdM512 {
    XVfdModbusBase m_base;          /**< 基类子对象（第一成员） */
    bool m_nonStdReply;             /**< Pd.05=0 非标准应答模式（应答多 1 字节，双长度解析） */
    double m_maxFreqHz;             /**< 最大频率缓存（P0.10；0=未读取，缺省按 50Hz） */
    bool m_maxFreqKnown;            /**< m_maxFreqHz 是否来自设备读取 */
    int m_freqStage;                /**< 设定频率两阶段：0=直接写 1=先读 P0.10 */
    double m_pendingFreqHz;         /**< 待写频率（Hz） */
    int m_accelStage;               /**< 加减速两步：0=写加速 1=写减速 */
    double m_pendingDecelS;         /**< 待写减速时间（秒） */
    int m_pendingAccelIdx;          /**< 待写加减速组号 */
    uint16_t m_lastRaw;             /**< 最近一次原始应答寄存器（非标准模式解析用） */
} XVfdM512;

/* ==================== 类生命周期 ==================== */

/**
 * @brief 获取 XVfdM512 类虚函数表。
 * @return 指向初始化完成的 XVtable 的指针
 */
XVtable* XVfdM512_class_init(void);

/**
 * @brief 在堆上创建并初始化 511M/512M/513M 设备实例。
 * @param memory 内存类型（XCLASS_DEFAULT_MEMORY_TYPE 使用系统默认内存）
 * @return 成功返回新对象（用 XClassDelete/XObject_deleteLater 释放），失败返回 NULL
 * @note 能力位图：反转/点动/自由停车/多段速值/输出端子强制/密码/恢复出厂。
 */
XVfdM512* XVfdM512_create_ex(XMemoryType memory);

/**
 * @brief 初始化实例（栈上使用；与 XClassDeinit 成对）。
 * @param dev 实例（非 NULL）
 */
void XVfdM512_init(XVfdM512* dev);

/* ==================== 配置 ==================== */

/**
 * @brief 设置非标准应答模式（Pd.05=0 出厂缺省：应答比标准 Modbus 多 1 字节）。
 * @param dev 实例（非 NULL）
 * @param nonStd true=非标准模式（双长度解析），false=标准 Modbus-RTU
 */
void XVfdM512_setNonStdReply(XVfdM512* dev, bool nonStd);

/**
 * @brief 查询非标准应答模式。
 * @param dev 实例（可 NULL，返回 false）
 * @return 非标准模式返回 true
 */
bool XVfdM512_nonStdReply(const XVfdM512* dev);

/**
 * @brief 查故障码对应名称（面板字母码，如 E.tIGbt；8000H 数值键）。
 * @param code 8000H 寄存器原始值
 * @return 名称（静态常量，禁释放）；未知返回 NULL
 */
const char* XVfdM512_faultName(uint16_t code);

/**
 * @brief 查故障码对应中文说明。
 * @param code 8000H 寄存器原始值
 * @return 说明（静态常量，禁释放）；未知返回 NULL
 */
const char* XVfdM512_faultText(uint16_t code);

#endif /* XVFD_M512_ON */
#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XVfdM512_create
#define XVfdM512_create() XVfdM512_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XVFDM512_H */
