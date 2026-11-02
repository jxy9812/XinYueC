/**
 * @file       XVfdDevice.h
 * @brief      变频器抽象父类公开 API（工业设备层，通道点驱动）。
 * @details    [2026-11-01 通道化重构] 本类从"协议子系统"升格为"工业设备"，
 *             底层**只依赖 Industry/XChannel**（XIoChannelGroup + XIoPoint），
 *             不再持有总线链路、不 include 任何 XModbus 头、不自建轮询与编帧：
 *               - 数据进：装配期把"语义槽点"（XVfdPointSpec）与调用方创建的
 *                 通道点绑定（bindPoint，借用不拥有）；点值变化经组信号
 *                 inputChanged 推入本类，通用遥测按规格表的原生单位/除率
 *                 解码进状态快照，品牌特有字段（状态字位图/故障派生）
 *                 由品牌子类重写 DecodePoint 虚槽消化；
 *               - 控制出：动作一律经组写通道下沉到点（writeAnalogOutput /
 *                 writeDigitalOutput），落定经组 writeDone 回到本类，
 *                 由 ActionWriteDone 虚槽分派（默认=单写动作收口，品牌可
 *                 在写落定后推进多步序列，如 ATV61 的 CiA402 启动）；
 *               - 节拍：只有一个状态快照发布定时器与一个动作监督定时器，
 *                 均为 XObject 自带定时器（不内嵌独立 XTimer 实例）。
 *             **通用性/扩展性**：换传输（Modbus RTU/TCP、GPIO、CAN、虚拟从站）
 *             只换点的实现（XChannel 侧）与装配，本层与品牌子类零改动；
 *             新增品牌=新子类 + 一张点规格表 + 一张故障表 + 命令编解码。
 *             全部动作异步：接口返回 true 仅代表请求受理，结果经信号通知
 *             （statusUpdated / commandCompleted / paramReadDone /
 *             paramWriteDone / nameplateUpdated / faultHistoryUpdated /
 *             errorOccurred）。
 *             品牌子类：XVfdAtv61（施耐德 Altivar 61）、XVfdM512（511M/512M/513M）。
 */
#ifndef XVFDDEVICE_H
#define XVFDDEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XVfd_config.h"

#if XVFD_ON
#if XVFD_DEVICE_ON

#include <stdint.h>
#include <stdbool.h>
#include "XObject.h"
#include "XIoChannelGroup.h"
#include "XIoPoint.h"

/** @brief 参数点绑定表容量（同时可读写的参数点上限）。 */
#define XVFD_PARAM_BIND_CAP 64
/** @brief 语义槽数组容量（须 >= XVfdRole_Count）。 */
#define XVFD_ROLE_CAP 48

/* ========================================================================== */
/*                      错误码                                                */
/* ========================================================================== */

/**
 * @brief 变频器操作错误码。
 */
typedef enum XVfdError {
    XVfdError_OK = 0,               /**< 无错误 */
    XVfdError_NotAttached,          /**< 未挂接通道组或所需语义槽未绑定 */
    XVfdError_Busy,                 /**< 设备级单飞：上一动作在途 */
    XVfdError_WriteRejected,        /**< 通道同步拒绝写入（点未实现/未登记/类型不符） */
    XVfdError_WriteFailed,          /**< 写落定失败（writeDone(false)） */
    XVfdError_Timeout,              /**< 写落定监督超时 */
    XVfdError_NoData,               /**< 所需点尚无数据（未首拍/数据源离线） */
    XVfdError_ParamRejected,        /**< 参数被拒绝（无绑定/越界） */
    XVfdError_ParamRunLocked,       /**< 参数运行禁改（描述表 access 约束） */
    XVfdError_NotSupported,         /**< 该品牌不支持此操作（能力位图未置位） */
    XVfdError_InvalidParam          /**< 调用参数非法 */
} XVfdError;

/* ========================================================================== */
/*                      能力位图                                              */
/* ========================================================================== */

/**
 * @brief 品牌能力位（按位或组成能力位图；应用按位动态显隐 UI）。
 */
typedef enum XVfdCapability {
    XVfdCap_Reverse = 1 << 0,           /**< 支持反转运行 */
    XVfdCap_Jog = 1 << 1,               /**< 支持通讯点动 */
    XVfdCap_FreeStop = 1 << 2,          /**< 支持自由停车 */
    XVfdCap_QuickStop = 1 << 3,         /**< 支持快速停机 */
    XVfdCap_SpeedRpmGiven = 1 << 4,     /**< 支持转速给定（rpm） */
    XVfdCap_TorqueReport = 1 << 5,      /**< 上报输出转矩 */
    XVfdCap_PowerReport = 1 << 6,       /**< 上报输出功率 */
    XVfdCap_ThermalReport = 1 << 7,     /**< 上报热状态百分比 */
    XVfdCap_TempReport = 1 << 8,        /**< 上报温度（摄氏度） */
    XVfdCap_MotorTemp = 1 << 9,         /**< 上报电机温度 */
    XVfdCap_FaultHistory = 1 << 10,     /**< 支持故障历史读取 */
    XVfdCap_Nameplate = 1 << 11,        /**< 支持铭牌读取 */
    XVfdCap_DiDoStatus = 1 << 12,       /**< 上报 DI/DO 端子位图 */
    XVfdCap_AiMonitor = 1 << 13,        /**< 上报模拟量输入 */
    XVfdCap_PidMonitor = 1 << 14,       /**< 上报 PID 设定/反馈 */
    XVfdCap_RuntimeStats = 1 << 15,     /**< 上报累计运行/上电时间/耗电量 */
    XVfdCap_DoForce = 1 << 16,          /**< 支持输出端子通讯强制 */
    XVfdCap_MultiSpeed = 1 << 17,       /**< 支持多段速 */
    XVfdCap_MotorTuning = 1 << 18,      /**< 支持电机调谐 */
    XVfdCap_Password = 1 << 19,         /**< 支持密码解锁 */
    XVfdCap_FactoryReset = 1 << 20,     /**< 支持恢复出厂 */
    XVfdCap_BroadcastWrite = 1 << 21,   /**< 支持广播写（地址 0） */
    XVfdCap_ParamAccess = 1 << 22       /**< 支持参数描述表与参数点读写 */
} XVfdCapability;

/* ========================================================================== */
/*                      运行状态                                              */
/* ========================================================================== */

/**
 * @brief 运行状态枚举（各品牌原状态字映射到该公共枚举）。
 */
typedef enum XVfdRunState {
    XVfdRunState_Unknown = 0,       /**< 未知（未读到状态） */
    XVfdRunState_Stopped,           /**< 停机 */
    XVfdRunState_Ready,             /**< 就绪待机（已上电未运行） */
    XVfdRunState_RunningForward,    /**< 正转运行 */
    XVfdRunState_RunningReverse,    /**< 反转运行 */
    XVfdRunState_JogForward,        /**< 正转点动 */
    XVfdRunState_JogReverse,        /**< 反转点动 */
    XVfdRunState_Accelerating,      /**< 加速中 */
    XVfdRunState_Decelerating,      /**< 减速中 */
    XVfdRunState_ConstantSpeed,     /**< 恒速运行 */
    XVfdRunState_DcBraking,         /**< 直流制动中 */
    XVfdRunState_FreeStopping,      /**< 自由停车中 */
    XVfdRunState_QuickStopping,     /**< 快速停机中 */
    XVfdRunState_Fault              /**< 故障 */
} XVfdRunState;

/* ========================================================================== */
/*                      状态快照字段位                                        */
/* ========================================================================== */

/**
 * @brief 状态快照字段有效位（m_validMask 按位或；品牌未提供的字段保持 0 且位清零）。
 */
typedef enum XVfdStatusField {
    XVfdField_RunState = 1ULL << 0,             /**< m_runState 有效 */
    XVfdField_RawStateWord = 1ULL << 1,         /**< m_rawStateWord 有效 */
    XVfdField_Running = 1ULL << 2,              /**< m_running 有效 */
    XVfdField_Faulted = 1ULL << 3,              /**< m_faulted 有效 */
    XVfdField_Warning = 1ULL << 4,              /**< m_warning 有效 */
    XVfdField_Reverse = 1ULL << 5,              /**< m_reverse 有效 */
    XVfdField_TargetReached = 1ULL << 6,        /**< m_targetReached 有效 */
    XVfdField_Remote = 1ULL << 7,               /**< m_remote 有效 */
    XVfdField_JogActive = 1ULL << 8,            /**< m_jogActive 有效 */
    XVfdField_DcBraking = 1ULL << 9,            /**< m_dcBraking 有效 */
    XVfdField_SetFreqHz = 1ULL << 10,           /**< m_setFreqHz 有效 */
    XVfdField_OutputFreqHz = 1ULL << 11,        /**< m_outputFreqHz 有效 */
    XVfdField_SetFreqPercent = 1ULL << 12,      /**< m_setFreqPercent 有效 */
    XVfdField_OutputFreqPercent = 1ULL << 13,   /**< m_outputFreqPercent 有效 */
    XVfdField_OutputVoltage = 1ULL << 14,       /**< m_outputVoltageV 有效 */
    XVfdField_OutputCurrent = 1ULL << 15,       /**< m_outputCurrentA 有效 */
    XVfdField_BusVoltage = 1ULL << 16,          /**< m_busVoltageV 有效 */
    XVfdField_MainsVoltage = 1ULL << 17,        /**< m_mainsVoltageV 有效 */
    XVfdField_OutputPowerKw = 1ULL << 18,       /**< m_outputPowerKw 有效 */
    XVfdField_OutputPowerPercent = 1ULL << 19,  /**< m_outputPowerPercent 有效 */
    XVfdField_TorquePercent = 1ULL << 20,       /**< m_torquePercent 有效 */
    XVfdField_SpeedRpm = 1ULL << 21,            /**< m_speedRpm 有效 */
    XVfdField_FeedbackSpeedRpm = 1ULL << 22,    /**< m_feedbackSpeedRpm 有效 */
    XVfdField_DriveTempC = 1ULL << 23,          /**< m_driveTempC 有效 */
    XVfdField_MotorTempC = 1ULL << 24,          /**< m_motorTempC 有效 */
    XVfdField_DriveThermalPercent = 1ULL << 25, /**< m_driveThermalPercent 有效 */
    XVfdField_MotorThermalPercent = 1ULL << 26, /**< m_motorThermalPercent 有效 */
    XVfdField_DiBits = 1ULL << 27,              /**< m_diBits 有效 */
    XVfdField_DoBits = 1ULL << 28,              /**< m_doBits 有效 */
    XVfdField_Ai1 = 1ULL << 29,                 /**< m_ai1 有效 */
    XVfdField_Ai2 = 1ULL << 30,                 /**< m_ai2 有效 */
    XVfdField_Ai3 = 1ULL << 31,                 /**< m_ai3 有效 */
    XVfdField_PidSetpoint = 1ULL << 32,         /**< m_pidSetpoint 有效 */
    XVfdField_PidFeedback = 1ULL << 33,         /**< m_pidFeedback 有效 */
    XVfdField_CounterValue = 1ULL << 34,        /**< m_counterValue 有效 */
    XVfdField_LengthValue = 1ULL << 35,         /**< m_lengthValue 有效 */
    XVfdField_RunHours = 1ULL << 36,            /**< m_runHoursTotal 有效 */
    XVfdField_PowerOnHours = 1ULL << 37,        /**< m_powerOnHoursTotal 有效 */
    XVfdField_EnergyKwh = 1ULL << 38,           /**< m_energyKwhTotal 有效 */
    XVfdField_FaultCode = 1ULL << 39            /**< m_faultCode 有效 */
} XVfdStatusField;

/* ========================================================================== */
/*                      值类型                                                */
/* ========================================================================== */

/**
 * @brief 变频器状态快照（值语义，零分配；字段有效性见 m_validMask）。
 */
typedef struct XVfdStatus {
    bool m_valid;                   /**< 本快照是否来自至少一次成功读取 */
    uint64_t m_validMask;           /**< XVfdStatusField 位或：哪些字段有效 */
    uint64_t m_timestampMs;         /**< 快照发布时刻（单调毫秒） */
    XVfdRunState m_runState;        /**< 运行状态枚举 */
    uint16_t m_rawStateWord;        /**< 品牌原状态字（透传，供调试与扩展） */
    bool m_running;                 /**< 运行中 */
    bool m_faulted;                 /**< 故障中 */
    bool m_warning;                 /**< 警告中 */
    bool m_reverse;                 /**< 反转方向 */
    bool m_targetReached;           /**< 已达到给定频率 */
    bool m_remote;                  /**< 命令/给定来自通讯 */
    bool m_jogActive;               /**< 点动中 */
    bool m_dcBraking;               /**< 直流制动中 */
    double m_setFreqHz;             /**< 设定频率（Hz） */
    double m_outputFreqHz;          /**< 输出频率（Hz） */
    double m_setFreqPercent;        /**< 设定频率（% 最大频率） */
    double m_outputFreqPercent;     /**< 输出频率（% 最大频率） */
    double m_outputVoltageV;        /**< 输出电压（V） */
    double m_outputCurrentA;        /**< 输出电流（A） */
    double m_busVoltageV;           /**< 直流母线电压（V） */
    double m_mainsVoltageV;         /**< 电网/输入电压（V） */
    double m_outputPowerKw;         /**< 输出功率（kW） */
    double m_outputPowerPercent;    /**< 输出功率（% 额定） */
    double m_torquePercent;         /**< 输出转矩（%） */
    double m_speedRpm;              /**< 电机转速（rpm） */
    double m_feedbackSpeedRpm;      /**< 反馈转速（rpm，编码器） */
    double m_driveTempC;            /**< 变频器温度（°C） */
    double m_motorTempC;            /**< 电机温度（°C） */
    double m_driveThermalPercent;   /**< 变频器热状态（%） */
    double m_motorThermalPercent;   /**< 电机热状态（%） */
    uint16_t m_diBits;              /**< 数字输入端子位图 */
    uint16_t m_doBits;              /**< 数字输出端子位图 */
    double m_ai1;                   /**< AI1（品牌单位） */
    double m_ai2;                   /**< AI2（品牌单位） */
    double m_ai3;                   /**< AI3（品牌单位） */
    double m_pidSetpoint;           /**< PID 设定值 */
    double m_pidFeedback;           /**< PID 反馈值 */
    int32_t m_counterValue;         /**< 计数器值 */
    double m_lengthValue;           /**< 长度测量值 */
    double m_runHoursTotal;         /**< 累计运行时间（小时） */
    double m_powerOnHoursTotal;     /**< 累计上电时间（小时） */
    double m_energyKwhTotal;        /**< 累计耗电量（kWh） */
    uint16_t m_faultCode;           /**< 当前故障码（品牌原值；0=无故障） */
} XVfdStatus;

/**
 * @brief 铭牌信息快照（值语义；字符串为固定数组，禁止作为指针释放）。
 */
typedef struct XVfdNameplate {
    char m_model[32];               /**< 产品型号（UTF-8，0 结尾） */
    double m_ratedPowerKw;          /**< 额定功率（kW） */
    double m_ratedVoltageV;         /**< 额定电压（V） */
    double m_ratedCurrentA;         /**< 额定电流（A） */
    double m_ratedFreqHz;           /**< 额定频率（Hz） */
    double m_ratedSpeedRpm;         /**< 额定转速（rpm） */
    char m_swVersion[24];           /**< 软件版本（品牌原文本） */
    char m_fwVersion[24];           /**< 固件版本（品牌原文本） */
    char m_factoryDate[24];         /**< 出厂日期（品牌原文本） */
    char m_serialNumber[32];        /**< 出厂序号（品牌原文本） */
} XVfdNameplate;

/**
 * @brief 故障历史记录（值语义；随品牌能力提供附随信息子集）。
 */
typedef struct XVfdFaultRecord {
    uint16_t m_code;                /**< 故障码（品牌原值） */
    uint64_t m_timestampMs;         /**< 记录到的时刻（单调毫秒；历史记录为 0） */
    double m_currentA;              /**< 故障时电流（A；无该信息为 0） */
    double m_voltageV;              /**< 故障时电压（V；无该信息为 0） */
    double m_freqHz;                /**< 故障时输出频率（Hz；无该信息为 0） */
    uint16_t m_diBits;              /**< 故障时 DI 端子状态（无该信息为 0） */
} XVfdFaultRecord;

/**
 * @brief 故障表条目（品牌 const 数组；名称/说明均为静态常量）。
 */
typedef struct XVfdFaultEntry {
    uint16_t m_code;                /**< 故障码（品牌原值，与快照 m_faultCode 同域） */
    const char* m_name;             /**< 面板字母码（可为 NULL） */
    const char* m_text;             /**< 中文说明（静态常量，禁释放） */
} XVfdFaultEntry;

/**
 * @brief 参数存取约束。
 */
typedef enum XVfdParamAccess {
    XVfdParamAccess_RW = 0,         /**< 可读可写 */
    XVfdParamAccess_ReadOnly,       /**< 只读（监视值） */
    XVfdParamAccess_RunLocked,      /**< 运行状态禁改（手册 ★） */
    XVfdParamAccess_Factory         /**< 厂家参数（禁止用户操作，手册 ▲） */
} XVfdParamAccess;

/**
 * @brief 参数描述表条目（品牌提供 const 数组；支撑通用参数页自动生成）。
 */
typedef struct XVfdParamDesc {
    uint16_t m_addr;                /**< 参数通道地址（品牌原生地址；装配期换算虚拟地址） */
    const char* m_name;             /**< 参数中文名（静态常量，禁释放） */
    const char* m_unit;             /**< 单位文本（可 NULL） */
    uint8_t m_decimals;             /**< 小数位数（raw / 10^decimals 为工程值） */
    int32_t m_min;                  /**< 设定范围下限（原始码） */
    int32_t m_max;                  /**< 设定范围上限（原始码） */
    int32_t m_default;              /**< 出厂值（原始码） */
    uint8_t m_access;               /**< 存取约束（XVfdParamAccess） */
    uint8_t m_group;                /**< 分组编号（品牌自定义，UI 分组显示用） */
} XVfdParamDesc;

/**
 * @brief 动作类型（commandCompleted 信号区分；数值兼容旧 XModbusAtv61_Operation）。
 */
typedef enum XVfdOperation {
    XVfdOperation_None = 0,
    XVfdOperation_ReadStatus,       /**< 读状态 */
    XVfdOperation_SetFrequency,     /**< 写频率给定 */
    XVfdOperation_SetSpeed,         /**< 写转速给定 */
    XVfdOperation_Start,            /**< 启动 */
    XVfdOperation_Stop,             /**< 受控停机 */
    XVfdOperation_FreeStop,         /**< 自由停车 */
    XVfdOperation_QuickStop,        /**< 快速停机 */
    XVfdOperation_ResetFault,       /**< 故障复位 */
    XVfdOperation_Jog,              /**< 点动 */
    XVfdOperation_SelectPresetSpeed,/**< 多段速选择 */
    XVfdOperation_SetPresetSpeedHz, /**< 段速值设定 */
    XVfdOperation_SetAccelDecel,    /**< 加减速时间设定 */
    XVfdOperation_SetPidTarget,     /**< PID 目标值设定 */
    XVfdOperation_SetCommandSource, /**< 命令源切换 */
    XVfdOperation_SetFrequencySource, /**< 频率源切换 */
    XVfdOperation_SetDigitalOutput, /**< 输出端子强制 */
    XVfdOperation_StartMotorTuning, /**< 电机调谐 */
    XVfdOperation_FactoryReset,     /**< 恢复出厂 */
    XVfdOperation_UnlockPassword,   /**< 密码解锁 */
    XVfdOperation_SaveParameters,   /**< 参数保存 */
    XVfdOperation_ReadParam,        /**< 参数读取 */
    XVfdOperation_WriteParam,       /**< 参数写入 */
    XVfdOperation_ReadNameplate,    /**< 铭牌读取 */
    XVfdOperation_ReadFaultHistory  /**< 故障历史读取 */
} XVfdOperation;

/* ========================================================================== */
/*                      语义槽（角色）与点规格                                */
/* ========================================================================== */

/**
 * @brief 语义槽角色：设备层认识的全部"数据位"名字，品牌点表按角色编排地址。
 * @note 数值一经发布不得改动（点表是静态 const 数据，跨版本兼容依赖稳定值）。
 */
typedef enum XVfdPointRole {
    XVfdRole_None = 0,              /**< 未使用 */
    XVfdRole_CommandWord = 1,       /**< 命令字/命令值（写） */
    XVfdRole_FreqSet = 2,           /**< 频率给定（写；原生单位） */
    XVfdRole_SpeedSet = 3,          /**< 转速给定（写；rpm） */
    XVfdRole_StatusWord = 4,        /**< 状态字（读；原样位图） */
    XVfdRole_AuxStateWord = 5,      /**< 辅助状态字（读；ETI 等） */
    XVfdRole_FaultCode = 6,         /**< 故障码（读） */
    XVfdRole_OutputFreq = 7,        /**< 输出频率（读） */
    XVfdRole_SetFreq = 8,           /**< 设定频率（读） */
    XVfdRole_OutputCurrent = 9,     /**< 输出电流（读） */
    XVfdRole_OutputVoltage = 10,    /**< 输出电压（读） */
    XVfdRole_BusVoltage = 11,       /**< 直流母线电压（读） */
    XVfdRole_MainsVoltage = 12,     /**< 电网电压（读） */
    XVfdRole_OutputPower = 13,      /**< 输出功率（读；kW） */
    XVfdRole_OutputPowerPercent = 14, /**< 输出功率百分比（读） */
    XVfdRole_Torque = 15,           /**< 输出转矩（读；%） */
    XVfdRole_SpeedRpm = 16,         /**< 电机转速（读；rpm） */
    XVfdRole_DriveTemp = 17,        /**< 变频器温度（读；°C） */
    XVfdRole_MotorTemp = 18,        /**< 电机温度（读；°C） */
    XVfdRole_DriveThermal = 19,     /**< 变频器热状态（读；%） */
    XVfdRole_MotorThermal = 20,     /**< 电机热状态（读；%） */
    XVfdRole_DiBits = 21,           /**< 数字输入端子位图（读） */
    XVfdRole_DoBits = 22,           /**< 数字输出端子位图（读） */
    XVfdRole_Ai1 = 23,              /**< 模拟量输入 1（读） */
    XVfdRole_Ai2 = 24,              /**< 模拟量输入 2（读） */
    XVfdRole_Ai3 = 25,              /**< 模拟量输入 3（读） */
    XVfdRole_PidSetpoint = 26,      /**< PID 设定值（读） */
    XVfdRole_PidFeedback = 27,      /**< PID 反馈值（读） */
    XVfdRole_Counter = 28,          /**< 计数器（读） */
    XVfdRole_Length = 29,           /**< 长度计（读） */
    XVfdRole_RunHours = 30,         /**< 累计运行时间（读；h） */
    XVfdRole_PowerOnHours = 31,     /**< 累计上电时间（读；h） */
    XVfdRole_Energy = 32,           /**< 累计耗电量（读；kWh） */
    XVfdRole_MaxFreq = 33,          /**< 最大频率（读；百分比换算基准） */
    XVfdRole_AccelTime = 34,        /**< 加速时间（写；原生单位） */
    XVfdRole_DecelTime = 35,        /**< 减速时间（写；原生单位） */
    XVfdRole_PresetSpeed = 36,      /**< 多段速给定（写；原生单位） */
    XVfdRole_PidTarget = 37,        /**< PID 目标值（写；原生单位） */
    XVfdRole_CommandSource = 38,    /**< 命令源选择（写） */
    XVfdRole_FreqSource = 39,       /**< 频率源选择（写） */
    XVfdRole_DoForce = 40,          /**< 输出端子强制位图（写） */
    XVfdRole_Password = 41,         /**< 密码解锁（写） */
    XVfdRole_ParamInit = 42,        /**< 参数初始化/恢复出厂（写） */
    XVfdRole_SaveParams = 43,       /**< 参数保存/备份（写） */
    XVfdRole_JogCommand = 44,       /**< 独立点动命令（写；品牌可选） */
    XVfdRole_Count = 45             /**< 角色总数（数组边界，不是合法角色） */
} XVfdPointRole;

/**
 * @brief 语义槽点规格：品牌点表一行 = "语义名 → 品牌原生地址 + 原生单位"。
 * @details 本结构是"品牌知识"的唯一载体：装配期由调用方读取 m_type/m_address/
 *          m_tag 以创建通道点；绑定后由设备层读取 m_role/m_div/m_caps 做解码。
 *          **语义层不解析地址**——地址只是给装配期的说明，换传输不影响语义。
 */
typedef struct XVfdPointSpec {
    uint16_t m_role;            /**< 语义角色（XVfdPointRole） */
    uint8_t m_type;             /**< 建议点类型（XIoPointType；装配期建点用） */
    int32_t m_div;              /**< 原生单位除率：工程值 = 原生值 / div（0 视为 1） */
    uint16_t m_address;         /**< 品牌原生通道地址（装配期换算虚拟地址；语义层不用） */
    const char* m_tag;          /**< 点名（借用，静态常量；装配期建点用） */
    uint32_t m_caps;            /**< 绑定该点即置位的能力位（XVfdCapability 位或；0=无） */
} XVfdPointSpec;

/**
 * @brief 语义槽登记项（设备内部：规格 + 点，均为借用）。
 */
typedef struct XVfdSlot {
    const XVfdPointSpec* m_spec; /**< 规格（静态 const，借用）；NULL=该槽未绑定 */
    XIoPoint* m_point;           /**< 通道点（借用，不拥有） */
} XVfdSlot;

/**
 * @brief 参数点登记项（设备内部：参数描述表下标 ↔ 通道点）。
 */
typedef struct XVfdParamBinding {
    uint16_t m_id;               /**< 参数描述表下标（0 基） */
    XIoPoint* m_point;           /**< 参数通道点（借用；读/写均经它） */
} XVfdParamBinding;

/* ========================================================================== */
/*                      虚函数表                                              */
/* ========================================================================== */

XCLASS_DEFINE_BEGING(XVfdDevice)
XCLASS_DEFINE_ENUM(XVfdDevice, DecodePoint) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XVfdDevice, ActionWriteDone),
XCLASS_DEFINE_ENUM(XVfdDevice, ActionTimerExpired),
XCLASS_DEFINE_ENUM(XVfdDevice, Start),
XCLASS_DEFINE_ENUM(XVfdDevice, Stop),
XCLASS_DEFINE_ENUM(XVfdDevice, FreeStop),
XCLASS_DEFINE_ENUM(XVfdDevice, QuickStop),
XCLASS_DEFINE_ENUM(XVfdDevice, Jog),
XCLASS_DEFINE_ENUM(XVfdDevice, SetFrequency),
XCLASS_DEFINE_ENUM(XVfdDevice, SetSpeedRpm),
XCLASS_DEFINE_ENUM(XVfdDevice, SetAccelDecel),
XCLASS_DEFINE_ENUM(XVfdDevice, SelectPresetSpeed),
XCLASS_DEFINE_ENUM(XVfdDevice, SetPresetSpeedHz),
XCLASS_DEFINE_ENUM(XVfdDevice, SetPidTarget),
XCLASS_DEFINE_ENUM(XVfdDevice, SetCommandSource),
XCLASS_DEFINE_ENUM(XVfdDevice, SetFrequencySource),
XCLASS_DEFINE_ENUM(XVfdDevice, ResetFault),
XCLASS_DEFINE_ENUM(XVfdDevice, SetDigitalOutput),
XCLASS_DEFINE_ENUM(XVfdDevice, StartMotorTuning),
XCLASS_DEFINE_ENUM(XVfdDevice, FactoryReset),
XCLASS_DEFINE_ENUM(XVfdDevice, UnlockPassword),
XCLASS_DEFINE_ENUM(XVfdDevice, SaveParameters),
XCLASS_DEFINE_ENUM(XVfdDevice, ReadParam),
XCLASS_DEFINE_ENUM(XVfdDevice, WriteParam),
XCLASS_DEFINE_ENUM(XVfdDevice, ReadNameplate),
XCLASS_DEFINE_ENUM(XVfdDevice, ReadFaultHistory),
XCLASS_DEFINE_END(XVfdDevice)

/* ========================================================================== */
/*                      结构体                                                */
/* ========================================================================== */

/**
 * @brief 变频器抽象父类（XObject 派生；语义槽 + 状态快照 + 能力位图 + 信号）。
 * @details 成员禁止直接访问，一律经 API 操作。通道组与全部点为**借用**：
 *          调用方保证其生命周期覆盖设备，并在销毁前先 detach 或先删设备。
 */
typedef struct XVfdDevice {
    XObject m_class;                /**< 基类（第一成员，由 XClass 管理，禁止手工修改） */
    XIoChannelGroup* m_group;       /**< 挂接的通道组（借用；NULL=未挂接） */
    XVfdSlot m_slots[XVFD_ROLE_CAP];/**< 语义槽表（按角色下标；未绑定为 NULL） */
    XVfdParamBinding m_paramBinds[XVFD_PARAM_BIND_CAP]; /**< 参数点绑定表 */
    int m_paramBindCount;           /**< 参数点绑定数 */
    const XVfdParamDesc* m_params;  /**< 品牌参数描述表（const，借用；品牌 init 设置） */
    int m_paramCount;               /**< 描述表条目数（0=无参数通道） */
    XVfdStatus m_status;            /**< 最近状态快照（解码路径回填） */
    uint32_t m_caps;                /**< 能力位图（品牌 init + 点规格共同置位） */
    bool m_haveStatus;              /**< 是否已发布过至少一次快照 */
    bool m_dirty;                   /**< 快照脏标记（发布定时器消费） */
    bool m_busy;                    /**< 设备级单飞：动作在途 */
    XVfdOperation m_op;             /**< 在途动作类型（commandCompleted 携带） */
    uint16_t m_opParamId;           /**< 在途动作若为参数读写，对应描述表下标 */
    XTimerId m_statusTimer;         /**< 快照发布定时器 */
    XTimerId m_actionTimer;         /**< 动作监督定时器（写落定超时/品牌序列步进） */
    uint32_t m_statusIntervalMs;    /**< 快照发布周期（毫秒；默认 200） */
    uint32_t m_actionTimeoutMs;     /**< 写落定监督超时（毫秒；默认 2000） */
    XVfdError m_lastError;          /**< 最近一次失败原因 */
} XVfdDevice;

/* ========================================================================== */
/*                      类生命周期                                            */
/* ========================================================================== */

/**
 * @brief 获取 XVfdDevice 类虚函数表。
 * @return 指向初始化完成的 XVtable 的指针
 */
XVtable* XVfdDevice_class_init(void);

/**
 * @brief 在堆上创建并初始化变频器设备实例。
 * @param memory 内存类型（XCLASS_DEFAULT_MEMORY_TYPE 使用系统默认内存）
 * @return 成功返回新对象（用 XClassDelete/XObject_deleteLater 释放），失败返回 NULL
 */
XVfdDevice* XVfdDevice_create_ex(XMemoryType memory);

/**
 * @brief 初始化实例（栈上使用；与 XClassDeinit 成对）。
 * @param dev 待初始化对象（非 NULL）
 */
void XVfdDevice_init(XVfdDevice* dev);

/* ========================================================================== */
/*                      装配（借用注入，解耦点）                              */
/* ========================================================================== */

/**
 * @brief 挂接通道组（数据进与写通道都经它分发）。
 * @param dev 设备实例；不能为 NULL。
 * @param group 通道组（借用，不拥有）；不能为 NULL。
 * @return 挂接并订阅组信号成功返回 true；参数非法或订阅失败返回 false。
 * @note 重复挂接先自动解挂旧组。挂接后启动快照发布定时器。
 */
bool XVfdDevice_attach(XVfdDevice* dev, XIoChannelGroup* group);

/**
 * @brief 解挂通道组（退订组信号、停定时器；语义槽清空，点本身不释放）。
 * @param dev 设备实例；可为 NULL。
 * @return 无。
 */
void XVfdDevice_detach(XVfdDevice* dev);

/**
 * @brief 查询是否已挂接通道组。
 * @param dev 设备实例；NULL 返回 false。
 * @return 已挂接返回 true。
 */
bool XVfdDevice_isAttached(const XVfdDevice* dev);

/**
 * @brief 取挂接的通道组（借用）。
 * @param dev 设备实例；NULL 返回 NULL。
 * @return 通道组指针；未挂接返回 NULL。
 */
XIoChannelGroup* XVfdDevice_channelGroup(const XVfdDevice* dev);

/**
 * @brief 绑定一个语义槽点（角色 ← 通道点）。
 * @param dev 设备实例；不能为 NULL。
 * @param spec 点规格（品牌静态常量，借用，生命周期须覆盖设备）；不能为 NULL。
 * @param point 通道点（借用，不拥有；须已登记于挂接的通道组）；不能为 NULL。
 * @return 绑定成功返回 true；未挂接组/角色非法/点未登记返回 false。
 * @note 绑定立即用点的当前缓存值播种快照（有数据即入快照），并按规格置能力位。
 *       同一角色重复绑定以新点覆盖（旧点不退订，调用方自行解绑）。
 */
bool XVfdDevice_bindPoint(XVfdDevice* dev, const XVfdPointSpec* spec, XIoPoint* point);

/**
 * @brief 解绑一个语义槽。
 * @param dev 设备实例；不能为 NULL。
 * @param role 语义角色。
 * @return 原本已绑定并成功解绑返回 true。
 */
bool XVfdDevice_unbindPoint(XVfdDevice* dev, XVfdPointRole role);

/**
 * @brief 查询某语义槽是否已绑定。
 * @param dev 设备实例；NULL 返回 false。
 * @param role 语义角色。
 * @return 已绑定返回 true。
 */
bool XVfdDevice_hasPoint(const XVfdDevice* dev, XVfdPointRole role);

/**
 * @brief 取某语义槽的点（借用）。
 * @param dev 设备实例；NULL 返回 NULL。
 * @param role 语义角色。
 * @return 点指针；未绑定返回 NULL。
 */
XIoPoint* XVfdDevice_point(const XVfdDevice* dev, XVfdPointRole role);

/**
 * @brief 已绑定语义槽数量。
 * @param dev 设备实例；NULL 返回 0。
 * @return 数量。
 */
int XVfdDevice_boundCount(const XVfdDevice* dev);

/**
 * @brief 绑定参数通道点（参数描述表下标 ← 通道点）。
 * @param dev 设备实例；不能为 NULL。
 * @param paramId 参数描述表下标（0 基）。
 * @param point 参数点（借用；须登记于挂接的通道组）；不能为 NULL。
 * @return 绑定成功返回 true；表满/未挂接/点未登记返回 false。
 */
bool XVfdDevice_bindParamPoint(XVfdDevice* dev, uint16_t paramId, XIoPoint* point);

/**
 * @brief 取参数通道点（借用；供装配期校验或直接读写）。
 * @param dev 设备实例；NULL 返回 NULL。
 * @param paramId 参数描述表下标。
 * @return 点指针；未绑定返回 NULL。
 */
XIoPoint* XVfdDevice_paramPoint(const XVfdDevice* dev, uint16_t paramId);

/* ========================================================================== */
/*                      状态与配置                                            */
/* ========================================================================== */

/**
 * @brief 获取能力位图。
 * @param dev 设备实例（可 NULL，返回 0）
 * @return XVfdCapability 位或
 */
uint32_t XVfdDevice_caps(const XVfdDevice* dev);

/**
 * @brief 查询是否具备某能力。
 * @param dev 设备实例
 * @param cap 能力位
 * @return 具备返回 true
 */
bool XVfdDevice_hasCapability(const XVfdDevice* dev, uint32_t cap);

/**
 * @brief 获取最近一次状态快照（常量引用，禁释放）。
 * @param dev 设备实例（可 NULL，返回 NULL）
 * @return 内部快照指针；禁修改禁释放
 */
const XVfdStatus* XVfdDevice_status_const(const XVfdDevice* dev);

/**
 * @brief 查询是否有动作在途（设备级单飞）。
 * @param dev 设备实例（可 NULL，返回 false）
 * @return 在途返回 true
 */
bool XVfdDevice_isBusy(const XVfdDevice* dev);

/**
 * @brief 获取最近一次失败原因。
 * @param dev 设备实例（可 NULL，返回 XVfdError_OK）
 * @return XVfdError
 */
XVfdError XVfdDevice_lastError(const XVfdDevice* dev);

/**
 * @brief 设置状态快照发布周期。
 * @param dev 设备实例；不能为 NULL。
 * @param intervalMs 周期毫秒（>=1；0 视为缺省 200）。
 * @return 无。
 * @note 快照只在有变化（脏标记）时发布，周期只决定发布的最长延迟。
 */
void XVfdDevice_setStatusInterval(XVfdDevice* dev, uint32_t intervalMs);

/**
 * @brief 获取状态快照发布周期。
 * @param dev 设备实例（可 NULL，返回 0）
 * @return 周期毫秒
 */
uint32_t XVfdDevice_statusInterval(const XVfdDevice* dev);

/**
 * @brief 设置写落定监督超时。
 * @param dev 设备实例；不能为 NULL。
 * @param timeoutMs 超时毫秒（>=1；0 视为缺省 2000）。
 * @return 无。
 */
void XVfdDevice_setActionTimeout(XVfdDevice* dev, uint32_t timeoutMs);

/**
 * @brief 获取写落定监督超时。
 * @param dev 设备实例（可 NULL，返回 0）
 * @return 超时毫秒
 */
uint32_t XVfdDevice_actionTimeout(const XVfdDevice* dev);

/* ========================================================================== */
/*                      动作接口（全部异步；true=受理，结果见信号）            */
/* ========================================================================== */

/**
 * @brief 启动电机（正转或反转；品牌状态机差异由子类消化）。
 * @param dev 设备实例
 * @param reverse true=反转
 * @return 请求已受理返回 true；未挂接/在途/不支持返回 false
 */
bool XVfdDevice_start(XVfdDevice* dev, bool reverse);

/**
 * @brief 受控停机（按减速斜坡停机）。
 * @param dev 设备实例
 * @return 请求已受理返回 true
 */
bool XVfdDevice_stop(XVfdDevice* dev);

/**
 * @brief 自由停车（切断输出）。
 * @param dev 设备实例
 * @return 请求已受理返回 true
 */
bool XVfdDevice_freeStop(XVfdDevice* dev);

/**
 * @brief 快速停机。
 * @param dev 设备实例
 * @return 请求已受理返回 true
 */
bool XVfdDevice_quickStop(XVfdDevice* dev);

/**
 * @brief 点动运行（点动频率由品牌点动频率参数决定）。
 * @param dev 设备实例
 * @param reverse true=反转点动
 * @return 请求已受理返回 true
 */
bool XVfdDevice_jog(XVfdDevice* dev, bool reverse);

/**
 * @brief 写频率给定（工程单位 Hz；标度换算由子类按点规格除率完成）。
 * @param dev 设备实例
 * @param hz 目标频率（Hz）
 * @return 请求已受理返回 true
 */
bool XVfdDevice_setFrequency(XVfdDevice* dev, double hz);

/**
 * @brief 写转速给定（rpm）。
 * @param dev 设备实例
 * @param rpm 目标转速
 * @return 请求已受理返回 true
 */
bool XVfdDevice_setSpeedRpm(XVfdDevice* dev, int rpm);

/**
 * @brief 写加减速时间。
 * @param dev 设备实例
 * @param index 加减速组号（0=第一组；组数随品牌）
 * @param accelS 加速时间（秒）
 * @param decelS 减速时间（秒）
 * @return 请求已受理返回 true
 */
bool XVfdDevice_setAccelDecel(XVfdDevice* dev, int index, double accelS, double decelS);

/**
 * @brief 多段速选择（切换到第 index 段）。
 * @param dev 设备实例
 * @param index 段号（0 基；段数随品牌）
 * @return 请求已受理返回 true
 */
bool XVfdDevice_selectPresetSpeed(XVfdDevice* dev, int index);

/**
 * @brief 写第 index 段的速度值。
 * @param dev 设备实例
 * @param index 段号（0 基）
 * @param hz 段速（Hz）
 * @return 请求已受理返回 true
 */
bool XVfdDevice_setPresetSpeedHz(XVfdDevice* dev, int index, double hz);

/**
 * @brief 写 PID 目标值。
 * @param dev 设备实例
 * @param value 目标值（工程单位随品牌；百分比品牌为 %）
 * @return 请求已受理返回 true
 */
bool XVfdDevice_setPidTarget(XVfdDevice* dev, double value);

/**
 * @brief 切换命令源（面板/端子/通讯；多数品牌仅停机可改）。
 * @param dev 设备实例
 * @param source 源编号（品牌枚举原值）
 * @return 请求已受理返回 true
 */
bool XVfdDevice_setCommandSource(XVfdDevice* dev, int source);

/**
 * @brief 切换频率源（数字设定/AI/PID/通讯等）。
 * @param dev 设备实例
 * @param source 源编号（品牌枚举原值）
 * @return 请求已受理返回 true
 */
bool XVfdDevice_setFrequencySource(XVfdDevice* dev, int source);

/**
 * @brief 故障复位。
 * @param dev 设备实例
 * @return 请求已受理返回 true
 */
bool XVfdDevice_resetFault(XVfdDevice* dev);

/**
 * @brief 输出端子通讯强制。
 * @param dev 设备实例
 * @param index 端子序号（0 基）
 * @param on true=强制导通
 * @return 请求已受理返回 true
 */
bool XVfdDevice_setDigitalOutput(XVfdDevice* dev, int index, bool on);

/**
 * @brief 启动电机调谐（长时运行操作，结果经 commandCompleted）。
 * @param dev 设备实例
 * @param mode 调谐方式（品牌枚举原值）
 * @return 请求已受理返回 true
 */
bool XVfdDevice_startMotorTuning(XVfdDevice* dev, int mode);

/**
 * @brief 恢复出厂参数。
 * @param dev 设备实例
 * @param scope 范围（0=不含电机参数，1=含电机参数；品牌语义见手册）
 * @return 请求已受理返回 true
 */
bool XVfdDevice_factoryReset(XVfdDevice* dev, int scope);

/**
 * @brief 密码解锁（解锁后方能写受保护参数）。
 * @param dev 设备实例
 * @param password 密码原值
 * @return 请求已受理返回 true
 */
bool XVfdDevice_unlockPassword(XVfdDevice* dev, uint32_t password);

/**
 * @brief 保存参数（触发品牌 EEPROM 落盘/备份语义）。
 * @param dev 设备实例
 * @return 请求已受理返回 true
 */
bool XVfdDevice_saveParameters(XVfdDevice* dev);

/* ========================================================================== */
/*                      参数通道                                              */
/* ========================================================================== */

/**
 * @brief 参数描述表条目数（品牌提供）。
 * @param dev 设备实例（可 NULL，返回 0）
 * @return 条目数（0=该品牌无参数通道）
 */
int XVfdDevice_paramCount(const XVfdDevice* dev);

/**
 * @brief 取参数描述（常量借用，禁释放）。
 * @param dev 设备实例（可 NULL，返回 NULL）
 * @param paramId 参数描述表下标（0 基）
 * @return 描述指针；越界返回 NULL
 */
const XVfdParamDesc* XVfdDevice_paramDesc_const(const XVfdDevice* dev, uint16_t paramId);

/**
 * @brief 读任意参数（原始值域；结果经 paramReadDone 信号）。
 * @param dev 设备实例
 * @param paramId 参数描述表下标（0 基；越界或未绑定参数点拒绝）
 * @return 请求已受理返回 true
 * @note 读的是参数点的当前缓存值（点由数据源异步刷新）。
 */
bool XVfdDevice_readParam(XVfdDevice* dev, uint16_t paramId);

/**
 * @brief ReadParam 虚槽基类实现（参数点直读；品牌可重写为多步序列）。
 * @param dev 设备实例
 * @param paramId 参数描述表下标
 * @return 受理返回 true
 */
bool XVfdDevice_readParam_base(XVfdDevice* dev, uint16_t paramId);

/**
 * @brief 写任意参数（原始值域；受描述表 access 约束，运行禁改直接拒绝）。
 * @param dev 设备实例
 * @param paramId 参数描述表下标（0 基）
 * @param raw 原始码值
 * @return 请求已受理返回 true；约束拒绝返回 false（lastError=ParamRunLocked）
 */
bool XVfdDevice_writeParam(XVfdDevice* dev, uint16_t paramId, int32_t raw);

/**
 * @brief WriteParam 虚槽基类实现（存取约束 + 参数点写入 + 落定收口）。
 * @param dev 设备实例
 * @param paramId 参数描述表下标
 * @param raw 原始码值
 * @return 受理返回 true
 */
bool XVfdDevice_writeParam_base(XVfdDevice* dev, uint16_t paramId, int32_t raw);

/**
 * @brief 读铭牌信息（结果经 nameplateUpdated 信号）。
 * @param dev 设备实例
 * @return 请求已受理返回 true
 */
bool XVfdDevice_readNameplate(XVfdDevice* dev);

/**
 * @brief 读故障历史（结果经 faultHistoryUpdated 信号）。
 * @param dev 设备实例
 * @return 请求已受理返回 true
 */
bool XVfdDevice_readFaultHistory(XVfdDevice* dev);

/* ========================================================================== */
/*                      信号                                                  */
/* ========================================================================== */

/**
 * @brief 状态更新信号：statusUpdated(const XVfdStatus* status)。
 * @param dev 设备实例；以 NULL 调用仅取信号标识、不发射
 * @return 信号标识（内部使用）
 * @note 参数为设备内部快照的常量指针，只读。
 */
void* XVfdDevice_statusUpdated_signal(XVfdDevice* dev);

/**
 * @brief 动作完成信号：commandCompleted(XVfdOperation op, bool ok)。
 * @param dev 设备实例；以 NULL 调用仅取信号标识
 * @param op 动作类型（发射时携带）
 * @param ok 是否成功（发射时携带）
 * @return 信号标识（内部使用）
 */
void* XVfdDevice_commandCompleted_signal(XVfdDevice* dev, XVfdOperation op, bool ok);

/**
 * @brief 参数读取完成信号：paramReadDone(uint16_t paramId, int32_t raw, bool ok)。
 * @param dev 设备实例；以 NULL 调用仅取信号标识
 * @param paramId 参数下标（发射时携带）
 * @param raw 原始码值（发射时携带）
 * @param ok 是否成功（发射时携带）
 * @return 信号标识（内部使用）
 */
void* XVfdDevice_paramReadDone_signal(XVfdDevice* dev, uint16_t paramId, int32_t raw, bool ok);

/**
 * @brief 参数写入完成信号：paramWriteDone(uint16_t paramId, bool ok)。
 * @param dev 设备实例；以 NULL 调用仅取信号标识
 * @param paramId 参数下标（发射时携带）
 * @param ok 是否成功（发射时携带）
 * @return 信号标识（内部使用）
 */
void* XVfdDevice_paramWriteDone_signal(XVfdDevice* dev, uint16_t paramId, bool ok);

/**
 * @brief 铭牌读取完成信号：nameplateUpdated(const XVfdNameplate* nameplate)。
 * @param dev 设备实例；以 NULL 调用仅取信号标识
 * @return 信号标识（内部使用）
 */
void* XVfdDevice_nameplateUpdated_signal(XVfdDevice* dev);

/**
 * @brief 故障历史读取完成信号：faultHistoryUpdated(const XVfdFaultRecord* records, int count)。
 * @param dev 设备实例；以 NULL 调用仅取信号标识
 * @return 信号标识（内部使用）
 */
void* XVfdDevice_faultHistoryUpdated_signal(XVfdDevice* dev);

/**
 * @brief 错误信号：errorOccurred(const char* message)。
 * @param dev 设备实例；以 NULL 调用仅取信号标识
 * @param message 错误描述（发射时携带，静态常量）
 * @return 信号标识（内部使用）
 */
void* XVfdDevice_errorOccurred_signal(XVfdDevice* dev, const char* message);

#endif /* XVFD_DEVICE_ON */
#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XVfdDevice_create
#define XVfdDevice_create() XVfdDevice_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XVFDDEVICE_H */
