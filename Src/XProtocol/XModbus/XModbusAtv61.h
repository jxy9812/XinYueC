/**
 * @file XModbusAtv61.h
 * @brief 施耐德 Altivar 61 (ATV61) 变频器 Modbus 驱动公开 API。
 * @details ATV61 内置 Modbus（RJ45/RS485 两线制）采用施耐德 "逻辑地址" 体系：
 *          手册里的 W 号（如 W8501）**直接就是 Modbus PDU 地址，不做 -1 偏移**
 *          （例：写 ACC=9001 → PDU 地址 0x2329=9001）。
 *          本驱动把「启停 / 调速 / 读状态 / 读报警 / 故障复位」封装成异步请求链，
 *          结果通过信号返回；不直接依赖 RTU 或 TCP，可挂在任意 XModbusClient 上。
 *
 * @note 现场使用前置条件（ATV61 侧，详见 docs/ATV61_485变频器面板改造与调试指南.md）：
 *   - [1.9 通信] → [Modbus 地址] (Add) 设为 1~247（出厂 OFF = 未启用）
 *   - [1.9 通信] → [Modbus 波特率] (tbr) 设为 38.4 kbps
 *   - [1.9 通信] → [Modbus 格式] (tFO) 设为 8E1（出厂即 8E1）
 *   - [1.6 命令] → [组合模式] (CHCF)=SIM 时把 [给定1 通道] (Fr1) 设为 Mdb；
 *                 CHCF=SEP/IO 时另需 [命令通道1] (Cd1)=Mdb
 */
#ifndef XMODBUSATV61_H
#define XMODBUSATV61_H

#include "XModbus_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_ATV61_ON

#include <stdint.h>
#include <stdbool.h>
#include "XObject.h"
#include "XModbusClient.h"

/* ========================================================================== */
/*                      寄存器逻辑地址（= Modbus PDU 地址）                    */
/* ========================================================================== */

/** @brief 命令字 CMD（R/W，CiA402/Drivecom 位图） */
#define XModbusAtv61_AddrCmd            8501   /* 0x2135 */
/** @brief 频率给定 LFR（R/W，0.1 Hz，整数，范围 ±5000 → ±500.0 Hz） */
#define XModbusAtv61_AddrLfr            8502   /* 0x2136 */
/** @brief 状态字 ETA（R） */
#define XModbusAtv61_AddrEta            3201   /* 0x0C81 */
/** @brief 输出频率 RFR（R，0.1 Hz） */
#define XModbusAtv61_AddrRfr            3202   /* 0x0C82 */
/** @brief 斜坡前频率给定 FRH（R，0.1 Hz） */
#define XModbusAtv61_AddrFrh            3203   /* 0x0C83 */
/** @brief 电机电流 LCR（R，单位见编程手册） */
#define XModbusAtv61_AddrLcr            3204   /* 0x0C84 */
/** @brief 电机转矩 OTR（R，0.1 %） */
#define XModbusAtv61_AddrOtr            3205   /* 0x0C85 */
/** @brief 内部状态字 ETI（R） */
#define XModbusAtv61_AddrEti            3206   /* 0x0C86 */
/** @brief 电网电压 ULN（R，0.1 V） */
#define XModbusAtv61_AddrUln            3207   /* 0x0C87 */
/** @brief 电机电压 UOP（R，1 V） */
#define XModbusAtv61_AddrUop            3208   /* 0x0C88 */
/** @brief 变频器热状态 THD（R，1 %） */
#define XModbusAtv61_AddrThd            3209   /* 0x0C89 */
/** @brief 电机功率 OPR（R，1 %） */
#define XModbusAtv61_AddrOpr            3211   /* 0x0C8B */
/** @brief 直流母线电压 VBUS（R，0.1 V） */
#define XModbusAtv61_AddrVbus           3243   /* 0x0CAB */
/** @brief 转速给定 LFRD（R/W，1 rpm） */
#define XModbusAtv61_AddrLfrd           8602   /* 0x219A */
/** @brief 输出转速 RFRD（R，1 rpm） */
#define XModbusAtv61_AddrRfrd           8604   /* 0x219C */
/** @brief DRIVECOM 状态字 ETAD（R） */
#define XModbusAtv61_AddrEtad           8603   /* 0x219B */
/** @brief Modbus 频率给定 LFR1（R/W，0.1 Hz，集成 Modbus 通道专用） */
#define XModbusAtv61_AddrLfr1           8521   /* 0x2149 */
/** @brief 通信模块频率给定 LFR3（R/W，0.1 Hz，通信卡通道专用） */
#define XModbusAtv61_AddrLfr3           8523   /* 0x214B */
/** @brief 最近一次故障代码 LFT（R，枚举值见 XModbusAtv61_faultText） */
#define XModbusAtv61_AddrLft            7121   /* 0x1BD1 */
/** @brief 电机热状态 THR（R，1 %） */
#define XModbusAtv61_AddrThr            9630   /* 0x259E */

/* ========================================================================== */
/*                      CMD 命令字位定义（CiA402 / Drivecom）                 */
/* ========================================================================== */

/** @brief CMD bit0：Switch on（上电允许） */
#define XModbusAtv61_CmdSwitchOn        0x0001
/** @brief CMD bit1：Enable voltage（主接触器允许） */
#define XModbusAtv61_CmdEnableVoltage   0x0002
/** @brief CMD bit2：Quick stop（0 = 快速停机激活） */
#define XModbusAtv61_CmdQuickStop       0x0004
/** @brief CMD bit3：Enable operation（运行允许） */
#define XModbusAtv61_CmdEnableOperation 0x0008
/** @brief CMD bit7：Fault reset（0→1 上升沿复位） */
#define XModbusAtv61_CmdFaultReset      0x0080
/** @brief CMD bit8：Halt（按 [停机类型] STT 停机，不离开 Operation enabled） */
#define XModbusAtv61_CmdHalt            0x0100
/** @brief CMD bit11：方向（1 = 反转） */
#define XModbusAtv61_CmdReverse         0x0800
/** @brief CMD：Shutdown（→ 状态 3 Ready to switch on） */
#define XModbusAtv61_CmdShutdown        0x0006
/** @brief CMD：Switch on（→ 状态 4 Switched on） */
#define XModbusAtv61_CmdSwitchOnState   0x0007
/** @brief CMD：Enable operation（→ 状态 5 Operation enabled） */
#define XModbusAtv61_CmdRun             0x000F
/** @brief CMD：Disable operation（→ 状态 4 Switched on，受控停机） */
#define XModbusAtv61_CmdDisableOperation 0x0007
/** @brief CMD：Disable voltage（→ 状态 2 Switch on disabled，自由停车） */
#define XModbusAtv61_CmdDisableVoltage  0x0000
/** @brief CMD：Quick stop（→ 状态 6 Quick stop active） */
#define XModbusAtv61_CmdQuickStopCmd    0x0002
/** @brief CMD：Fault reset（→ 状态 2 Switch on disabled） */
#define XModbusAtv61_CmdFaultResetCmd   0x0080

/* ========================================================================== */
/*                      ETA 状态字位定义                                     */
/* ========================================================================== */

/** @brief ETA bit0：Ready to switch on */
#define XModbusAtv61_EtaReadyToSwitchOn    0x0001
/** @brief ETA bit1：Switched on */
#define XModbusAtv61_EtaSwitchedOn         0x0002
/** @brief ETA bit2：Operation enabled（运行中） */
#define XModbusAtv61_EtaOperationEnabled   0x0004
/** @brief ETA bit3：Fault（故障） */
#define XModbusAtv61_EtaFault              0x0008
/** @brief ETA bit4：Voltage enabled */
#define XModbusAtv61_EtaVoltageEnabled     0x0010
/** @brief ETA bit5：Quick stop（0 = 快速停机激活） */
#define XModbusAtv61_EtaQuickStopActive    0x0020
/** @brief ETA bit6：Switch on disabled */
#define XModbusAtv61_EtaSwitchOnDisabled   0x0040
/** @brief ETA bit7：Warning（警告） */
#define XModbusAtv61_EtaWarning            0x0080
/** @brief ETA bit9：Remote（命令/给定来自现场总线） */
#define XModbusAtv61_EtaRemote             0x0200
/** @brief ETA bit10：Target reached（达到给定） */
#define XModbusAtv61_EtaTargetReached      0x0400
/** @brief ETA bit11：Reference value outside limits（给定越限） */
#define XModbusAtv61_EtaRefOutOfLimits     0x0800
/** @brief ETA bit14：方向（1 = 反转） */
#define XModbusAtv61_EtaReverse            0x4000
/** @brief ETA bit15：STOP 键停机 */
#define XModbusAtv61_EtaStopKey            0x8000

/** @brief ETA 掩码值（用于判定 CiA402 运行状态） */
#define XModbusAtv61_EtaMask               0x006F
/** @brief 运行状态 2：Switch on disabled */
#define XModbusAtv61_EtaStateDisabled      0x0040
/** @brief 运行状态 3：Ready to switch on */
#define XModbusAtv61_EtaStateReady         0x0021
/** @brief 运行状态 4：Switched on */
#define XModbusAtv61_EtaStateSwitched      0x0023
/** @brief 运行状态 5：Operation enabled */
#define XModbusAtv61_EtaStateRunning       0x0027
/** @brief 运行状态 6：Quick stop active */
#define XModbusAtv61_EtaStateQuickStop     0x0007
/** @brief 运行状态 7：Fault reaction active */
#define XModbusAtv61_EtaStateFaultReaction 0x002F
/** @brief 运行状态 8：Fault */
#define XModbusAtv61_EtaStateFault         0x0008

/* ========================================================================== */
/*                      类型定义                                              */
/* ========================================================================== */

/**
 * @brief 频率给定的写入方式
 */
typedef enum {
    /** @brief 写 LFR1(8521)，0.1 Hz（ATV61 集成 Modbus 通道的频率给定，推荐） */
    XModbusAtv61_RefMode_ModbusFreq = 0,
    /** @brief 写 LFR(8502)，0.1 Hz（通用频率给定字） */
    XModbusAtv61_RefMode_LfrFreq,
    /** @brief 写 LFRD(8602)，1 rpm（转速给定，变速模式） */
    XModbusAtv61_RefMode_SpeedRpm
} XModbusAtv61_ReferenceMode;

/**
 * @brief 驱动支持的动作类型（用于命令完成信号区分）
 */
typedef enum {
    XModbusAtv61_Op_None = 0,
    XModbusAtv61_Op_ReadStatus,   /**< 读状态 */
    XModbusAtv61_Op_SetFrequency, /**< 写频率给定 */
    XModbusAtv61_Op_SetSpeed,     /**< 写转速给定 */
    XModbusAtv61_Op_Start,        /**< 启动（CiA402 状态机推进） */
    XModbusAtv61_Op_Stop,         /**< 受控停机 */
    XModbusAtv61_Op_FreeStop,     /**< 自由停车 */
    XModbusAtv61_Op_QuickStop,    /**< 快速停机 */
    XModbusAtv61_Op_ResetFault    /**< 故障复位 */
} XModbusAtv61_Operation;

/**
 * @brief 变频器状态快照
 */
typedef struct XModbusAtv61Status {
    bool     valid;             /**< 本快照是否来自一次成功读取 */
    uint16_t eta;               /**< 状态字 ETA 原值 */
    uint16_t eti;               /**< 内部状态字 ETI 原值 */
    double   outputFreqHz;      /**< 输出频率（RFR，0.1 Hz 定点） */
    double   referenceHz;       /**< 斜坡前给定频率（FRH，0.1 Hz 定点） */
    double   motorCurrent;      /**< 电机电流（LCR，量纲见编程手册） */
    double   motorTorquePercent;/**< 电机转矩（OTR，0.1 % 定点） */
    double   mainsVoltage;      /**< 电网电压（ULN，0.1 V 定点） */
    double   motorVoltage;      /**< 电机电压（UOP，1 V） */
    double   driveThermalPercent;/**< 变频器热状态（THD，1 %） */
    double   motorThermalPercent;/**< 电机热状态（THR，1 %） */
    double   motorPowerPercent; /**< 电机功率（OPR，1 %） */
    int      outputSpeedRpm;    /**< 输出转速（RFRD，1 rpm） */
    uint16_t lastFault;         /**< 最近一次故障代码（LFT） */
    bool     running;           /**< ETA bit2：运行中 */
    bool     faulted;           /**< ETA bit3：有故障 */
    bool     warning;           /**< ETA bit7：有警告 */
    bool     reverse;           /**< ETA bit14：反转 */
    bool     targetReached;     /**< ETA bit10：达到给定 */
    bool     remoteControl;     /**< ETA bit9：命令来自总线 */
    bool     quickStopActive;   /**< ETA bit5==0：快速停机中 */
    bool     referenceOutOfRange;/**< ETA bit11：给定越限 */
    uint8_t  driveState;        /**< CiA402 运行状态（ETA & 0x006F） */
} XModbusAtv61Status;

XCLASS_DEFINE_BEGING(XModbusAtv61)
XCLASS_DEFINE_EXTEND_END(XModbusAtv61, XObject)

/**
 * @brief ATV61 驱动对象
 */
typedef struct XModbusAtv61 {
    XObject m_class;                /**< 基类子对象；第一个成员，禁止手工修改 */
    XModbusClient* m_client;        /**< 借用的底层 Modbus 客户端（不转移所有权） */
    int m_unitId;                   /**< 从站地址（ATV61 的 Add 参数，1~247） */
    XModbusAtv61_ReferenceMode m_refMode; /**< 频率给定写入方式 */
    uint16_t m_referenceAddress;    /**< 频率给定字地址（随 m_refMode 变化，也可手工改） */
    XModbusAtv61Status m_status;    /**< 最近一次状态快照 */
    uint16_t m_lastCmd;             /**< 最近一次写出的 CMD（用于边沿检测） */
    bool m_busy;                    /**< 是否有请求在途 */
    bool m_haveStatus;              /**< m_status 是否有效 */
    XModbusAtv61_Operation m_op;    /**< 当前正在执行的动作 */
    int m_stage;                    /**< 内部阶段机（见实现文件 STAGE_* 常量） */
    int m_step;                     /**< 启动序列步进计数 */
    bool m_reverse;                 /**< 启动方向（true = 反转） */
    bool m_pendingReset;            /**< 故障复位序列：下一步写 Disable voltage 归位 */
    int m_pendingSpeedValue;        /**< 待写入的转速给定（rpm） */
    double m_pendingFreq;           /**< 待写入的频率给定（Hz） */
    uint16_t m_pendingCmd;          /**< 待写出的命令字 */
    XModbusReply* m_pendingReply;   /**< 在途应答（完成后 deleteLater） */
    XTimerId m_stepTimer;           /**< 步进延时定时器 */
} XModbusAtv61;

/* ========================================================================== */
/*                      类生命周期                                            */
/* ========================================================================== */

/**
 * @brief 获取类虚函数表
 */
XVtable* XModbusAtv61_class_init(void);
/**
 * @brief 创建并初始化实例
 * @param memory 内存类型
 * @return 新对象（用 XClassDelete 释放），失败返回 NULL
 */
XModbusAtv61* XModbusAtv61_create_ex(XMemoryType memory);
/**
 * @brief 初始化实例
 */
void XModbusAtv61_init(XModbusAtv61* driver);

/* ========================================================================== */
/*                      绑定与配置                                            */
/* ========================================================================== */

/**
 * @brief 绑定底层 Modbus 客户端与从站地址
 * @param driver 驱动实例
 * @param client 底层客户端（借用，不取得所有权；驱动不负责释放）
 * @param unitId 从站地址（ATV61 的 [Modbus 地址] Add，1~247）
 * @return 成功返回 true
 */
bool XModbusAtv61_attach(XModbusAtv61* driver, XModbusClient* client, int unitId);
/**
 * @brief 解绑底层客户端
 */
void XModbusAtv61_detach(XModbusAtv61* driver);
/**
 * @brief 获取从站地址
 */
int XModbusAtv61_unitId(const XModbusAtv61* driver);
/**
 * @brief 设置从站地址
 */
void XModbusAtv61_setUnitId(XModbusAtv61* driver, int unitId);
/**
 * @brief 设置频率给定写入方式
 * @note 会同步更新 m_referenceAddress（8521 / 8502 / 8602）
 */
void XModbusAtv61_setReferenceMode(XModbusAtv61* driver, XModbusAtv61_ReferenceMode mode);
/**
 * @brief 获取频率给定写入方式
 */
XModbusAtv61_ReferenceMode XModbusAtv61_referenceMode(const XModbusAtv61* driver);
/**
 * @brief 直接指定频率给定字地址（覆盖写入方式对应的默认地址）
 */
void XModbusAtv61_setReferenceAddress(XModbusAtv61* driver, uint16_t address);
/**
 * @brief 获取当前频率给定字地址
 */
uint16_t XModbusAtv61_referenceAddress(const XModbusAtv61* driver);
/**
 * @brief 获取最近一次状态快照（常量引用，禁释放）
 */
const XModbusAtv61Status* XModbusAtv61_status_const(const XModbusAtv61* driver);
/**
 * @brief 是否有请求在途
 */
bool XModbusAtv61_isBusy(const XModbusAtv61* driver);

/* ========================================================================== */
/*                      动作接口（异步，结果见信号）                          */
/* ========================================================================== */

/**
 * @brief 读取变频器状态（ETA/频率/电流/转矩/电压/热状态 + 故障代码）
 * @return 请求已发出返回 true；参数非法或总线忙返回 false
 * @note 结果通过 XModbusAtv61_statusUpdated_signal 通知
 */
bool XModbusAtv61_readStatus(XModbusAtv61* driver);
/**
 * @brief 写频率给定（Hz，内部按 0.1 Hz 定点换算）
 * @param driver 驱动实例
 * @param hz 目标频率（Hz，负值表示反转给定）
 * @return 请求已发出返回 true
 */
bool XModbusAtv61_setFrequency(XModbusAtv61* driver, double hz);
/**
 * @brief 写转速给定（rpm）
 */
bool XModbusAtv61_setSpeedRpm(XModbusAtv61* driver, int rpm);
/**
 * @brief 启动电机（按 CiA402 状态机推进 Shutdown→Switch on→Enable operation）
 * @param reverse true = 反转
 * @return 请求已发出返回 true
 * @note 内部会先读 ETA 判定当前状态，再逐级下发命令；结果见命令完成信号
 */
bool XModbusAtv61_start(XModbusAtv61* driver, bool reverse);
/**
 * @brief 受控停机（Disable operation，按减速斜坡停机，保持使能）
 */
bool XModbusAtv61_stop(XModbusAtv61* driver);
/**
 * @brief 自由停车（Disable voltage，切断主接触器）
 */
bool XModbusAtv61_freeStop(XModbusAtv61* driver);
/**
 * @brief 快速停机（Quick stop）
 */
bool XModbusAtv61_quickStop(XModbusAtv61* driver);
/**
 * @brief 故障复位（Fault reset 上升沿，随后写 Disable voltage 归位）
 */
bool XModbusAtv61_resetFault(XModbusAtv61* driver);

/* ========================================================================== */
/*                      故障码                                                */
/* ========================================================================== */

/**
 * @brief 查故障码对应名称（ATV61 故障缩写，如 OCF / OLF / SLF1）
 * @param code LFT 寄存器原始值
 * @return 名称字符串（静态常量，禁释放）；未知故障返回 NULL
 */
const char* XModbusAtv61_faultName(uint16_t code);
/**
 * @brief 查故障码对应英文说明
 * @param code LFT 寄存器原始值
 * @return 说明字符串（静态常量，禁释放）；未知故障返回 NULL
 */
const char* XModbusAtv61_faultText(uint16_t code);

/* ========================================================================== */
/*                      信号                                                  */
/* ========================================================================== */

/**
 * @brief 状态更新信号：statusUpdated(const XModbusAtv61Status* status)
 * @note 参数为驱动内部快照的常量指针，只读
 */
void* XModbusAtv61_statusUpdated_signal(XModbusAtv61* driver);
/**
 * @brief 动作完成信号：commandCompleted(XModbusAtv61_Operation op, bool ok)
 */
void* XModbusAtv61_commandCompleted_signal(XModbusAtv61* driver, XModbusAtv61_Operation op, bool ok);
/**
 * @brief 错误信号：errorOccurred(const char* message)
 */
void* XModbusAtv61_errorOccurred_signal(XModbusAtv61* driver, const char* message);

#endif /* XMODBUS_ATV61_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XModbusAtv61_create
#define XModbusAtv61_create() XModbusAtv61_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XMODBUSATV61_H */
