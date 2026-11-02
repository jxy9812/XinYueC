/**
 * @file       XVfdM512.h
 * @brief      511M/512M/513M 系列高性能矢量变频器子类公开 API。
 * @details    [2026-11-02 说明书核对重构] 品牌子类只承担"协议知识"：
 *               - 通信组表（XVfdM512_commGroups）：手册条目级的远端地址区间。
 *                 该系列只支持 FC03/FC06 且「一帧读不得跨过本功能码组的最后
 *                 一个功能码」——组边界是**手册条目**，不是地址连续性（例：
 *                 1000H「通讯设定值」与 1001H~1020H「运行/停机参数」是两条
 *                 条目，虽地址相邻仍不可同帧）。装配层按组切映射段，读帧由
 *                 此天然不跨组。
 *               - 点规格表（XVfdM512_pointSpecs）：语义角色 → 品牌地址 +
 *                 原生单位除率 + 有符号标志（1000H 通讯设定值是 16 位补码）。
 *               - 参数描述表（XVfdM512_paramDescs）：P/A/C/d/PP 组核心参数，
 *                 含「只改 RAM 不落 EEPROM」地址变体（手册附录1：P 组写
 *                 EEPROM 用 FxxxH、只改 RAM 用 0xxxH；A 组 AxxxH/4xxxH）。
 *               - 从站协议约束（XVfdM512_unitProtocol）：单帧读上限 12 字、
 *                 不跨组、只允许 FC06 单寄存器写。
 *               - 故障表：手册附录1 的 8000H 故障码表（41 条）+ 面板显示码
 *                 附表（两套编号不可互推）。
 *             数据来源与写通道全部经基类下沉到通道点，本文件不含传输符号。
 *             手册依据：《511M/512M/513M 高性能矢量变频器使用说明书》
 *             2020-05-03 附录1（通信）与第三/四/五章（参数与故障）。
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
#include "XVfdCommandEnum.h"

/* ========================================================================== */
/*                特殊地址（附录1：非功能码数据 / 快捷遥测）                   */
/* ========================================================================== */

#define XVfdM512_AddrFreqSet        0x1000  /**< 通讯设定值（±10000 = ±100.00%，int16 补码） */
#define XVfdM512_AddrTelemetry      0x1001  /**< 快捷遥测块起点：运行频率(0.01Hz) */
#define XVfdM512_TelemetryCount     32      /**< 1001H~1020H 运行/停机参数条（手册一条） */
#define XVfdM512_AddrPassword       0x1F00  /**< 用户密码校验（写入后回 8888H=通过） */
#define XVfdM512_AddrParamInit      0x1F01  /**< 参数初始化（1/2 恢复出厂 3清记录 4备份 501 恢复备份） */
#define XVfdM512_AddrCmdWord        0x2000  /**< 控制命令（写 1/2/3/4/5/6/7） */
#define XVfdM512_AddrDoForce        0x2001  /**< 数字输出端子强制位图（BIT0=DO1…BIT9=VDO5） */
#define XVfdM512_AddrAoForce        0x2002  /**< AO1 模拟输出强制（0~0x7FFF = 0~100%） */
#define XVfdM512_AddrAoForce2       0x2003  /**< AO2 模拟输出强制（0~0x7FFF） */
#define XVfdM512_AddrPulseForce     0x2004  /**< PULSE(FMP) 输出强制（0~0x7FFF） */
#define XVfdM512_AddrRunState       0x3000  /**< 运行状态（1正转 2反转 3停机） */
#define XVfdM512_AddrFault          0x8000  /**< 故障码（附录1 通信故障表） */
#define XVfdM512_AddrMonitorBase    0x7000  /**< U0 组监视参数起点（7000H=运行频率） */
#define XVfdM512_AddrMonitorEnd     0x7046  /**< U0 组监视参数末址（U0.74） */
#define XVfdM512_AddrMonitorHole1   0x7030  /**< U0 组内空洞（手册未印该址） */
#define XVfdM512_AddrMonitorHole2   0x7033  /**< U0 组内空洞起点（手册未印 U0.50~U0.57） */
#define XVfdM512_AddrMonitorHole2End 0x7039 /**< U0 组内空洞末址 */

/* ==================== 功能码地址（附录1：P0~PE=FxxxH，PP=FFxxH，A=AxH） ==================== */

#define XVfdM512_AddrControlMode    0xF001  /**< P0.01 第1电机控制方式 */
#define XVfdM512_AddrCmdSource      0xF002  /**< P0.02 命令源选择（2=通讯） */
#define XVfdM512_AddrFreqSource     0xF003  /**< P0.03 主频率源X选择（9=通讯给定） */
#define XVfdM512_AddrAuxFreqSource  0xF004  /**< P0.04 辅助频率源Y选择 */
#define XVfdM512_AddrPresetFreq     0xF008  /**< P0.08 预置频率 */
#define XVfdM512_AddrMaxFreq        0xF00A  /**< P0.10 最大频率（Hz，两位小数） */
#define XVfdM512_AddrUpperFreq      0xF00C  /**< P0.12 上限频率 */
#define XVfdM512_AddrLowerFreq      0xF00E  /**< P0.14 下限频率 */
#define XVfdM512_AddrCarrier        0xF00F  /**< P0.15 载波频率 */
#define XVfdM512_AddrAccelTime      0xF011  /**< P0.17 加速时间 1 */
#define XVfdM512_AddrDecelTime      0xF012  /**< P0.18 减速时间 1 */
#define XVfdM512_AddrTimeUnit       0xF013  /**< P0.19 加减速时间单位（0=1s 1=0.1s 2=0.01s） */
#define XVfdM512_AddrPidSource      0xFA00  /**< PA.00 PID 给定源（5=通讯给定） */
#define XVfdM512_AddrPidTarget      0xFA01  /**< PA.01 PID 数字给定（0~PA.04 量程单位） */
#define XVfdM512_AddrPidRange       0xFA04  /**< PA.04 PID 给定反馈量程 */
#define XVfdM512_AddrJogFreq        0xF800  /**< P8.00 点动运行频率（Hz，两位小数） */
#define XVfdM512_AddrPresetBase     0xFC00  /**< PC.00 多段指令 0（-10000~10000 = ±100%） */
#define XVfdM512_AddrPresetCount    16      /**< PC.00~PC.15 共 16 段 */
#define XVfdM512_AddrBaudRate       0xFD00  /**< Pd.00 通讯波特率（复合值：千位CANlink/十位Profibus/个位MODBUS） */
#define XVfdM512_AddrDataFormat     0xFD01  /**< Pd.01 MODBUS 数据格式（0=8N2 1=8E1 2=8O1 3=8N1） */
#define XVfdM512_AddrSlaveAddr      0xFD02  /**< Pd.02 本机地址（1~247，0=广播） */
#define XVfdM512_AddrReplyDelay     0xFD03  /**< Pd.03 MODBUS 应答延时（0~20ms） */
#define XVfdM512_AddrCommTimeout    0xFD04  /**< Pd.04 串口通讯超时时间（0.1~60.0s） */
#define XVfdM512_AddrCommFormat     0xFD05  /**< Pd.05 通讯数据格式（个位 0=非标准 1=标准 MODBUS-RTU） */
#define XVfdM512_AddrUserPassword   0xFF00  /**< PP.00 用户密码 */
#define XVfdM512_AddrParamInitFunc  0xFF01  /**< PP.01 参数初始化 */

/** @brief 通讯设定值原生单位除率（±10000 = ±100.00%）。 */
#define XVfdM512_FreqPercentDiv     100
/** @brief 最大频率原生单位除率（5000 = 50.00Hz）。 */
#define XVfdM512_MaxFreqDiv         100
/** @brief 加减速时间原生单位除率（P0.19 缺省 1 = 0.1s）。 */
#define XVfdM512_TimeDiv            10
/** @brief 单次读帧寄存器数上限（附录1：n 最大 12）。 */
#define XVfdM512_MaxReadRegisters   12

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
 * @brief 命令源枚举（P0.02）中的通讯通道值。
 */
#define XVfdM512_CmdSourceComm      2   /**< P0.02 = 2（通讯命令通道；面板 LED 闪烁） */
/** @brief 主频率源枚举（P0.03）中的通讯给定值。 */
#define XVfdM512_FreqSourceComm     9   /**< P0.03 = 9（通讯给定） */
/** @brief PID 给定源枚举（PA.00）中的通讯给定值。 */
#define XVfdM512_PidSourceComm      5   /**< PA.00 = 5（通讯给定） */

/**
 * @brief 511M/512M/513M 子类结构体（继承 XVfdCommandEnum → XVfdDevice）。
 */
XCLASS_DEFINE_BEGING(XVfdM512)
XCLASS_DEFINE_EXTEND_END(XVfdM512, XVfdCommandEnum)

typedef struct XVfdM512 {
    XVfdCommandEnum m_base;         /**< 基类子对象（第一成员） */
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

/* ==================== 点规格表 / 组表 / 参数表（装配期用） ==================== */

/**
 * @brief 取 511M 族点规格表（静态 const，禁释放）。
 * @param count 输出条目数；可为 NULL。
 * @return 规格表首地址。
 */
const XVfdPointSpec* XVfdM512_pointSpecs(int* count);

/**
 * @brief 取 511M 族通信组表（静态 const，禁释放）。
 * @param count 输出条目数；可为 NULL。
 * @return 组表首地址；装配期按组切映射段（每组一段），读帧因此不跨组。
 */
const XVfdCommGroup* XVfdM512_commGroups(int* count);

/**
 * @brief 取 511M 族参数描述表（静态 const，禁释放）。
 * @param count 输出条目数；可为 NULL。
 * @return 描述表首地址；装配期按 m_addr 建参数点后经 bindParamPoint 绑定。
 */
const XVfdParamDesc* XVfdM512_paramDescs(int* count);

/**
 * @brief 取 511M 族从站协议约束（静态 const，禁释放）。
 * @return 约束内容指针；装配层按字段翻译成传输层配置后登记到该从站。
 * @note 作用域是**从站**：一条 RTU 总线上的其它从站不受本约束影响。
 */
const XVfdUnitProtocol* XVfdM512_unitProtocol(void);

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

/* ==================== 通讯通道前置（附录1 的功能可用性前提） ==================== */

/**
 * @brief 把命令源切到通讯通道（P0.02 = 2）。
 * @param dev 实例（不能为 NULL）。
 * @return 受理返回 true；命令源槽未绑定或写被拒返回 false。
 * @note **前置条件**：2000H 控制命令仅在「命令源=通讯」时生效（附录1）。
 *       未配置时本系列会静默忽略命令字——本接口让应用层在初始化时一次性配好。
 */
bool XVfdM512_setCommandSourceComm(XVfdM512* dev);

/**
 * @brief 把主频率源切到通讯给定（P0.03 = 9）。
 * @param dev 实例（不能为 NULL）。
 * @return 受理返回 true；频率源槽未绑定或写被拒返回 false。
 * @note **前置条件**：1000H 通讯设定值仅在「主频率源=通讯给定」时生效。
 */
bool XVfdM512_setFrequencySourceComm(XVfdM512* dev);

/**
 * @brief 把 PID 给定源切到通讯给定（PA.00 = 5）。
 * @param dev 实例（不能为 NULL）。
 * @return 受理返回 true；PID 源槽未绑定或写被拒返回 false。
 * @note **前置条件**：PA.01 PID 数字给定仅在「PID 给定源=通讯给定」时生效。
 */
bool XVfdM512_setPidSourceComm(XVfdM512* dev);

/**
 * @brief 模拟输出通讯强制（附录1：2002H=AO1、2003H=AO2）。
 * @param dev 实例（不能为 NULL）。
 * @param which 0=AO1 1=AO2。
 * @param percent 百分比（0~100）。
 * @return 受理返回 true；参数非法返回 false。
 * @note 对应输出端子的功能选择须为「通讯控制」时才生效。
 */
bool XVfdM512_setAnalogOutputForce(XVfdM512* dev, int which, double percent);

/* ==================== 故障码查询 ==================== */

/**
 * @brief 查故障码对应名称（面板显示码，如 E.oCAC；8000H 数值键）。
 * @param code 8000H 寄存器原始值
 * @return 名称（静态常量，禁释放）；未知返回 NULL
 * @note 面板显示码与 8000H 数值是**两套编号**，不可互推（手册第五章与
 *       附录1 各自成表）：本表按 8000H 数值索引，返回的字母码仅供现场核对。
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
