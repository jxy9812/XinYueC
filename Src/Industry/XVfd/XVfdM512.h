/**
 * @file       XVfdM512.h
 * @brief      511M/512M/513M 系列高性能矢量变频器子类公开 API。
 * @details    [2026-11-01 通道化重构] 品牌子类只承担"协议知识"：
 *               - 点规格表（XVfdM512_pointSpecs）：语义角色 → 品牌地址
 *                 （2000H 命令字/3000H 状态/8000H 故障/1000H 设定值/7000H 监视）
 *                 + 原生单位除率；
 *               - 参数描述表（XVfdM512_paramDescs）：P/A/C/d 组核心参数，
 *                 装配期按 m_addr 建参数点后经 bindParamPoint 绑定；
 *               - 状态/故障解码、命令字编码、频率百分比换算（经最大频率 P0.10）。
 *             数据来源与写通道全部经基类下沉到通道点，本文件不含传输符号。
 *             手册依据：《511M/512M/513M 高性能矢量变频器使用说明书》
 *             2020-05-03 附录1（仅 FC03/FC06；地址编排 F0xxH=写 EEPROM、
 *             0xxxH=仅 RAM、AxH/4xH、U 组=7000H+序号）。
 */
#ifndef XVFDM512_H
#define XVFDM512_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XVfd_config.h"

#if XVFD_ON
#if XVFD_M512_ON

#include <stdint.h>
#include <stdbool.h>
#include "XVfdDevice.h"

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
#define XVfdM512_AddrMaxFreq        0xF00A  /**< P0.10 最大频率（Hz，两位小数） */
#define XVfdM512_AddrAccelTime      0xF011  /**< P0.17 加速时间 1 */
#define XVfdM512_AddrDecelTime      0xF012  /**< P0.18 减速时间 1 */
#define XVfdM512_AddrPresetBase     0xFC00  /**< PC.00 多段指令 0（-10000~10000 = ±100%） */
#define XVfdM512_AddrPidTarget      0xFA01  /**< PA.01 PID 数字给定（0.01%） */

/** @brief 通讯设定值原生单位除率（±10000 = ±100.00%）。 */
#define XVfdM512_FreqPercentDiv     100
/** @brief 最大频率原生单位除率（5000 = 50.00Hz）。 */
#define XVfdM512_MaxFreqDiv         100
/** @brief 加减速时间原生单位除率（P0.19 缺省 1 = 0.1s）。 */
#define XVfdM512_TimeDiv            10

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
 * @brief 511M/512M/513M 子类结构体（继承 XVfdDevice）。
 */
XCLASS_DEFINE_BEGING(XVfdM512)
XCLASS_DEFINE_EXTEND_END(XVfdM512, XVfdDevice)

typedef struct XVfdM512 {
    XVfdDevice m_base;              /**< 基类子对象（第一成员） */
    double m_maxFreqHz;             /**< 最大频率缓存（P0.10；0=未读取，缺省按 50Hz） */
    bool m_maxFreqKnown;            /**< m_maxFreqHz 是否来自设备读取 */
    int m_stage;                    /**< 多步动作阶段（0=首步 1=第二步） */
    uint16_t m_pendingRaw;          /**< 第二步待写原始码 */
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
 * @note 能力位图：反转/点动/自由停车/多段速/输出端子强制/密码/恢复出厂。
 */
XVfdM512* XVfdM512_create_ex(XMemoryType memory);

/**
 * @brief 初始化实例（栈上使用；与 XClassDeinit 成对）。
 * @param dev 实例（非 NULL）
 */
void XVfdM512_init(XVfdM512* dev);

/* ==================== 点规格表与参数表（装配期用） ==================== */

/**
 * @brief 取 511M 族点规格表（静态 const，禁释放）。
 * @param count 输出条目数；可为 NULL。
 * @return 规格表首地址。
 */
const XVfdPointSpec* XVfdM512_pointSpecs(int* count);

/**
 * @brief 取 511M 族参数描述表（静态 const，禁释放）。
 * @param count 输出条目数；可为 NULL。
 * @return 描述表首地址；装配期按 m_addr 建参数点后经 bindParamPoint 绑定。
 */
const XVfdParamDesc* XVfdM512_paramDescs(int* count);

/* ==================== 配置 ==================== */

/**
 * @brief 设置最大频率缓存（百分比换算基准；跳过 P0.10 读取）。
 * @param dev 实例（非 NULL）
 * @param hz 最大频率（Hz；<=0 忽略）
 */
void XVfdM512_setMaxFrequency(XVfdM512* dev, double hz);

/**
 * @brief 取最大频率缓存。
 * @param dev 实例（可 NULL，返回 0）
 * @return 最大频率（Hz）；未读取时为 0（换算按 50Hz 兜底）
 */
double XVfdM512_maxFrequency(const XVfdM512* dev);

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
