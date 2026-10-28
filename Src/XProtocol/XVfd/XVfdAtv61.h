/**
 * @file       XVfdAtv61.h
 * @brief      施耐德 Altivar 61 (ATV61) 变频器子类公开 API。
 * @details    XVfdDevice/XVfdModbusBase 的 ATV61 实现：内置 Modbus（RJ45/RS485
 *             两线制）采用施耐德"逻辑地址"体系，手册 W 号**直接就是 Modbus PDU
 *             地址，不做 -1 偏移**（例：写 ACC=9001 → PDU 地址 0x2329=9001）。
 *             启停按 CiA402/Drivecom 状态机推进（读 ETA → 0x0006→0x0007→0x000F
 *             逐级下发，最多 6 步）；频率给定缺省写 LFR1(8521，0.1Hz)，可切换
 *             LFR(8502)/转速 LFRD(8602)。
 *             挂接：XVfdModbusBase_setLink（XVfdModbusLink，RTU 主站或 TCP）。
 * @note       现场前置条件见 docs/ATV61_485变频器面板改造与调试指南.md §2
 *             （Add=1~247、tbr=38.4k、tFO=8E1、CHCF=SIM 时 Fr1=Mdb；改完重新上电）。
 */
#ifndef XVFDATV61_H
#define XVFDATV61_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XVfd_config.h"

#if XVFD_ON
#if XVFD_ATV61_ON

#include <stdint.h>
#include <stdbool.h>
#include "XVfdModbusBase.h"

/* ========================================================================== */
/*                寄存器逻辑地址（= Modbus PDU 地址，不减 1）                  */
/* ========================================================================== */

#define XVfdAtv61_AddrCmd            8501   /**< 命令字 CMD（R/W，CiA402 位图） */
#define XVfdAtv61_AddrLfr            8502   /**< 频率给定 LFR（R/W，0.1 Hz） */
#define XVfdAtv61_AddrEta            3201   /**< 状态字 ETA（R） */
#define XVfdAtv61_AddrRfr            3202   /**< 输出频率 RFR（R，0.1 Hz） */
#define XVfdAtv61_AddrFrh            3203   /**< 斜坡前给定 FRH（R，0.1 Hz） */
#define XVfdAtv61_AddrLcr            3204   /**< 电机电流 LCR（R，量纲见编程手册） */
#define XVfdAtv61_AddrOtr            3205   /**< 电机转矩 OTR（R，0.1 %） */
#define XVfdAtv61_AddrEti            3206   /**< 内部状态字 ETI（R） */
#define XVfdAtv61_AddrUln            3207   /**< 电网电压 ULN（R，0.1 V） */
#define XVfdAtv61_AddrUop            3208   /**< 电机电压 UOP（R，1 V） */
#define XVfdAtv61_AddrThd            3209   /**< 变频器热状态 THD（R，1 %） */
#define XVfdAtv61_AddrVbus           3243   /**< 直流母线电压 VBUS（R，0.1 V） */
#define XVfdAtv61_AddrLfrd           8602   /**< 转速给定 LFRD（R/W，1 rpm） */
#define XVfdAtv61_AddrRfrd           8604   /**< 输出转速 RFRD（R，1 rpm） */
#define XVfdAtv61_AddrLfr1           8521   /**< Modbus 频率给定 LFR1（R/W，0.1 Hz） */
#define XVfdAtv61_AddrLft            7121   /**< 最近故障代码 LFT（R） */
#define XVfdAtv61_AddrThr            9630   /**< 电机热状态 THR（R，1 %） */
#define XVfdAtv61_AddrAcc            9001   /**< 加速时间 ACC（R/W，0.01 s，现场标定） */
#define XVfdAtv61_AddrDec            9002   /**< 减速时间 DEC（R/W，0.01 s，现场标定） */

/* ========================================================================== */
/*                CMD 命令字位定义（CiA402 / Drivecom）                        */
/* ========================================================================== */

#define XVfdAtv61_CmdShutdown          0x0006 /**< Shutdown → 状态 3 Ready */
#define XVfdAtv61_CmdSwitchOnState     0x0007 /**< Switch on → 状态 4 Switched on */
#define XVfdAtv61_CmdRun               0x000F /**< Enable operation → 状态 5 运行 */
#define XVfdAtv61_CmdDisableOperation  0x0007 /**< Disable operation（受控停机） */
#define XVfdAtv61_CmdDisableVoltage    0x0000 /**< Disable voltage（自由停车） */
#define XVfdAtv61_CmdQuickStopCmd      0x0002 /**< Quick stop */
#define XVfdAtv61_CmdFaultResetCmd     0x0080 /**< Fault reset（0→1 上升沿） */
#define XVfdAtv61_CmdReverse           0x0800 /**< 方向位（1 = 反转） */

/* ========================================================================== */
/*                ETA 状态字位定义                                            */
/* ========================================================================== */

#define XVfdAtv61_EtaMask               0x006F /**< CiA402 状态掩码 */
#define XVfdAtv61_EtaStateDisabled      0x0040 /**< 状态 2：Switch on disabled */
#define XVfdAtv61_EtaStateReady         0x0021 /**< 状态 3：Ready to switch on */
#define XVfdAtv61_EtaStateSwitched      0x0023 /**< 状态 4：Switched on */
#define XVfdAtv61_EtaStateRunning       0x0027 /**< 状态 5：Operation enabled */
#define XVfdAtv61_EtaStateQuickStop     0x0007 /**< 状态 6：Quick stop active */
#define XVfdAtv61_EtaStateFault         0x0008 /**< 状态 8：Fault */
#define XVfdAtv61_EtaOperationEnabled   0x0004 /**< 运行中 */
#define XVfdAtv61_EtaFault              0x0008 /**< 故障 */
#define XVfdAtv61_EtaWarning            0x0080 /**< 警告 */
#define XVfdAtv61_EtaRemote             0x0200 /**< 命令来自总线 */
#define XVfdAtv61_EtaTargetReached      0x0400 /**< 达到给定 */
#define XVfdAtv61_EtaReverse            0x4000 /**< 反转 */
#define XVfdAtv61_EtiAccel              0x0200 /**< ETI bit9：加速中 */
#define XVfdAtv61_EtiDecel              0x0400 /**< ETI bit10：减速中 */
#define XVfdAtv61_EtiDcBrake            0x0020 /**< ETI bit5：直流制动 */

/* ========================================================================== */
/*                类型定义                                                    */
/* ========================================================================== */

/**
 * @brief 频率/转速给定的写入通道。
 */
typedef enum XVfdAtv61_ReferenceMode {
    XVfdAtv61_RefMode_ModbusFreq = 0,   /**< 写 LFR1(8521)，0.1 Hz（推荐缺省） */
    XVfdAtv61_RefMode_LfrFreq,          /**< 写 LFR(8502)，0.1 Hz */
    XVfdAtv61_RefMode_SpeedRpm          /**< 写 LFRD(8602)，1 rpm（变速模式） */
} XVfdAtv61_ReferenceMode;

/**
 * @brief ATV61 子类结构体（继承 XVfdModbusBase）。
 */
XCLASS_DEFINE_BEGING(XVfdAtv61)
XCLASS_DEFINE_EXTEND_END(XVfdAtv61, XVfdModbusBase)

typedef struct XVfdAtv61 {
    XVfdModbusBase m_base;          /**< 基类子对象（第一成员） */
    XVfdAtv61_ReferenceMode m_refMode; /**< 给定写入通道（同步 m_referenceAddress） */
    uint16_t m_referenceAddress;    /**< 给定字地址（随通道变化，可手工覆盖） */
    int m_stage;                    /**< CiA402 序列内部阶段（STAGE_*） */
    int m_step;                     /**< 启动序列步进计数 */
    bool m_reverse;                 /**< 启动方向（true=反转） */
    bool m_pendingReset;            /**< 故障复位序列：待写 Disable voltage 归位 */
    uint16_t m_pendingCmd;          /**< 待写命令字 */
    XTimerId m_stepTimer;           /**< 步进延时定时器（XObject 自带定时器） */
} XVfdAtv61;

/* ==================== 类生命周期 ==================== */

/**
 * @brief 获取 XVfdAtv61 类虚函数表。
 * @return 指向初始化完成的 XVtable 的指针
 */
XVtable* XVfdAtv61_class_init(void);

/**
 * @brief 在堆上创建并初始化 ATV61 设备实例。
 * @param memory 内存类型（XCLASS_DEFAULT_MEMORY_TYPE 使用系统默认内存）
 * @return 成功返回新对象（用 XClassDelete/XObject_deleteLater 释放），失败返回 NULL
 * @note 能力位图：反转/自由停车/快速停机/转速给定/转矩/功率/热状态。
 */
XVfdAtv61* XVfdAtv61_create_ex(XMemoryType memory);

/**
 * @brief 初始化实例（栈上使用；与 XClassDeinit 成对）。
 * @param dev 实例（非 NULL）
 */
void XVfdAtv61_init(XVfdAtv61* dev);

/* ==================== 配置 ==================== */

/**
 * @brief 设置给定写入通道（同步 m_referenceAddress）。
 * @param dev 实例（非 NULL）
 * @param mode XVfdAtv61_ReferenceMode
 */
void XVfdAtv61_setReferenceMode(XVfdAtv61* dev, XVfdAtv61_ReferenceMode mode);

/**
 * @brief 获取给定写入通道。
 * @param dev 实例（可 NULL，返回 ModbusFreq）
 * @return 通道枚举
 */
XVfdAtv61_ReferenceMode XVfdAtv61_referenceMode(const XVfdAtv61* dev);

/**
 * @brief 直接指定给定字地址（覆盖通道默认；现场标定 8521/8502/8602 用）。
 * @param dev 实例（非 NULL）
 * @param address 给定字逻辑地址
 */
void XVfdAtv61_setReferenceAddress(XVfdAtv61* dev, uint16_t address);

/**
 * @brief 查故障码对应名称（ATV61 字母码，如 OCF）。
 * @param code LFT 寄存器原始值
 * @return 名称（静态常量，禁释放）；未知返回 NULL
 */
const char* XVfdAtv61_faultName(uint16_t code);

/**
 * @brief 查故障码对应中文说明。
 * @param code LFT 寄存器原始值
 * @return 说明（静态常量，禁释放）；未知返回 NULL
 */
const char* XVfdAtv61_faultText(uint16_t code);

#endif /* XVFD_ATV61_ON */
#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XVfdAtv61_create
#define XVfdAtv61_create() XVfdAtv61_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XVFDATV61_H */
