/**
 * @file       XVfdAtv61.h
 * @brief      施耐德 Altivar 61 (ATV61) 变频器子类公开 API。
 * @details    [2026-11-01 通道化重构] 品牌子类只承担"协议知识"，不再碰传输：
 *               - 点规格表（XVfdAtv61_pointSpecs）：语义角色 → ATV61 逻辑地址
 *                 + 原生单位除率。装配期由调用方按表建通道点（Modbus/其它
 *                 传输均可），再经 XVfdDevice_bindPoint 绑定；
 *               - 状态字解码的**品牌专有**部分（ETI 的加速/减速/直流制动位）；
 *                 CiA402/Drivecom 状态机骨架（读 ETA → 0x0006/0x0007/0x000F
 *                 逐级推进，最多 6 步，每步经动作监督定时器节拍）已上提为
 *                 通用基类 XVfdCiA402，本类只给一张 XVfdCiA402Profile
 *                 （ETA/CMD 常量 + 时序常量）；
 *               - 故障表（LFT 数值 → 字母码/中文说明），字母码以《ATV61 编程
 *                 手册》p.242–247 为准。
 *             地址体系：施耐德"逻辑地址"就是 Modbus PDU 地址，**不做 -1 偏移**
 *             （例：写 ACC=9001 → PDU 地址 0x2329=9001）。
 * @note       现场前置条件见 docs/ATV61_485变频器面板改造与调试指南.md §2
 *             （Add=1~247、tbr=38.4k、tFO=8E1、CHCF=SIM 时 Fr1=Mdb；改完重新上电）。
 */
#ifndef XVFDATV61_H
#define XVFDATV61_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XVfd_config.h"

#if XVFD_ON
#if XVFD_ATV61_ON

#include <stdint.h>
#include <stdbool.h>
#include "XVfdCiA402.h"
#include "XVfdBind.h"

/* ========================================================================== */
/*                寄存器逻辑地址（= Modbus PDU 地址，不减 1）                  */
/* ========================================================================== */

#define XVfdAtv61_AddrCmd            8501   /**< 命令字 CMD（R/W，CiA402 位图） */
#define XVfdAtv61_AddrLfr            8502   /**< 频率给定 LFR（R/W，0.1 Hz） */
#define XVfdAtv61_AddrEta            3201   /**< 状态字 ETA（R） */
#define XVfdAtv61_AddrRfr            3202   /**< 输出频率 RFR（R，0.1 Hz） */
#define XVfdAtv61_AddrFrh            3203   /**< 斜坡前给定 FRH（R，0.1 Hz） */
#define XVfdAtv61_AddrLcr            3204   /**< 电机电流 LCR（R，1 A） */
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

/** @brief 启动序列每步等待缺省值（毫秒）。 */
#define XVfdAtv61_DefaultStepDelayMs    120u
/** @brief 启动序列最大步数。 */
#define XVfdAtv61_StartMaxStep          6
/** @brief 每步等待状态迁移的最大轮询拍数（60ms/拍 → 9s 上限；读组轮转
 *  周期 = 组数×轮询周期，等待上限须覆盖最坏排程）。 */
#define XVfdAtv61_StartWaitPolls        150
/** @brief 频率给定原生单位除率（0.1 Hz）。 */
#define XVfdAtv61_FreqDiv               10
/** @brief 加减速时间原生单位除率（0.01 s）。 */
#define XVfdAtv61_TimeDiv               100

/* ========================================================================== */
/*                类型定义                                                    */
/* ========================================================================== */

/**
 * @brief ATV61 子类结构体（继承 XVfdCiA402 → XVfdDevice）。
 */
XCLASS_DEFINE_BEGING(XVfdAtv61)
XCLASS_DEFINE_EXTEND_END(XVfdAtv61, XVfdCiA402)

typedef struct XVfdAtv61 {
    XVfdCiA402 m_base;              /**< 基类子对象（第一成员） */
    int16_t m_pendingDecel;         /**< 加减速两步写：暂存待写减速值 */
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
 * @note 能力位图：反转/自由停车/快速停机/转速给定/转矩/热状态（点规格再补齐）。
 */
XVfdAtv61* XVfdAtv61_create_ex(XMemoryType memory);

/**
 * @brief 初始化实例（栈上使用；与 XClassDeinit 成对）。
 * @param dev 实例（非 NULL）
 */
void XVfdAtv61_init(XVfdAtv61* dev);

/* ==================== 点规格表（装配期用） ==================== */

/**
 * @brief 取 ATV61 点规格表（静态 const，禁释放）。
 * @param count 输出条目数；可为 NULL。
 * @return 规格表首地址；装配期按 m_address 建点、按 m_role 绑定。
 * @note 表内含读点（AI）与写点（AO）：AO 的角色为 CommandWord/FreqSet/
 *       SpeedSet/AccelTime/DecelTime。给定通道选择靠"绑定哪个点"表达：
 *       默认 FreqSet→LFR1(8521)；若现场用 LFR(8502)，装配期把 FreqSet 点
 *       建在 8502 即可，设备层零改动。
 */
const XVfdPointSpec* XVfdAtv61_pointSpecs(int* count);

/**
 * @brief 取 ATV61 的 Modbus 绑定表（地址知识，定义在 XVfdAtv61_Modbus.c）。
 * @return 静态常量绑定表指针；不可释放。
 */
const XVfdBindTable* XVfdAtv61_modbusBinding(void);

/* ==================== 配置 ==================== */

/**
 * @brief 设置启动序列步进延时（每写一条命令字后的等待）。
 * @param dev 实例（非 NULL）
 * @param delayMs 延时毫秒（>=10；0 按缺省 120）
 */
void XVfdAtv61_setStepDelay(XVfdAtv61* dev, uint32_t delayMs);

/**
 * @brief 取启动序列步进延时。
 * @param dev 实例（可 NULL，返回 0）
 * @return 延时毫秒
 */
uint32_t XVfdAtv61_stepDelay(const XVfdAtv61* dev);

/**
 * @brief 查故障码对应名称（ATV61 字母码，如 OCF）。
 * @param code LFT 寄存器原始值
 * @return 名称（静态常量，禁释放）；未知返回 NULL
 * @note 字母码以《ATV61 编程手册》p.242–247 的写法为准（混合大小写）；
 *       只查手册列出的故障；其它产品族条目见 XVfdAtv61_otherFaultEntry。
 */
const char* XVfdAtv61_faultName(uint16_t code);

/**
 * @brief 查故障码对应中文说明。
 * @param code LFT 寄存器原始值
 * @return 说明（静态常量，禁释放）；未知返回 NULL
 * @note 只查 ATV61 编程手册 p.242–247 列出的故障；其它产品族条目见
 *       XVfdAtv61_otherFaultEntry。
 */
const char* XVfdAtv61_faultText(uint16_t code);

/**
 * @brief 查「其它产品族」故障条目（ATV610 / AFE / 泵控；ATV61 手册未列）。
 * @param code LFT 寄存器原始值
 * @return 条目指针（静态常量，禁释放）；未命中返回 NULL
 * @note 这些条目出自《ATV610 通信参数表》EAV64394，**不是 ATV61 的故障**，
 *       不参与 XVfdAtv61_faultName/faultText 的显示；本接口只供现场对照与
 *       后续移植查阅。
 */
const XVfdFaultEntry* XVfdAtv61_otherFaultEntry(uint16_t code);

#endif /* XVFD_ATV61_ON */
#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XVfdAtv61_create
#define XVfdAtv61_create() XVfdAtv61_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XVFDATV61_H */
