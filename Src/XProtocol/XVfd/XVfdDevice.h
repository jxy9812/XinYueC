/**
 * @file       XVfdDevice.h
 * @brief      变频器抽象父类公开 API。
 * @details    品牌无关的变频器语义全集：启停/调速/点动/多段速/PID/故障复位/参数读写/
 *             状态遥测/铭牌，全部以虚函数暴露（*_base 入口查虚表分派，业务在子类
 *             VXxx 实现中）；品牌不支持的作業经能力位图（XVfdCapability）与缺省
 *             实现返回 NotSupported。
 *             传输无关：设备只挂总线链路（XVfdLink，借用），不持有 IO、不开轮询
 *             定时器——轮询节拍由链路计划表（XVfdPollScheduler）统一排程，485 总线
 *             竞争在链路的传输适配器内消解。
 *             全部动作异步：接口返回 true 仅代表请求受理，结果经信号通知
 *             （statusUpdated / commandCompleted / paramReadDone / paramWriteDone /
 *             nameplateUpdated / faultHistoryUpdated / errorOccurred）。
 *             品牌子类：XVfdAtv61（施耐德 Altivar 61）、XVfdM512（511M/512M/513M）。
 */
#ifndef XVFDDEVICE_H
#define XVFDDEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XVfd_config.h"

#if XVFD_ON
#if XVFD_DEVICE_ON

#include <stdint.h>
#include <stdbool.h>
#include "XObject.h"
#include "XDateTime.h"

typedef struct XVfdLink XVfdLink;

/* ========================================================================== */
/*                      错误码                                                */
/* ========================================================================== */

/**
 * @brief 变频器操作错误码。
 */
typedef enum XVfdError {
    XVfdError_OK = 0,               /**< 无错误 */
    XVfdError_NotAttached,          /**< 未绑定链路 */
    XVfdError_Busy,                 /**< 设备级单飞：上一请求在途 */
    XVfdError_Timeout,              /**< 请求超时 */
    XVfdError_CrcError,             /**< 应答 CRC 校验失败 */
    XVfdError_InvalidFrame,         /**< 应答帧格式非法 */
    XVfdError_SlaveError,           /**< 从站返回异常帧 */
    XVfdError_ParamRejected,        /**< 参数被从站拒绝 */
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
    XVfdCap_BroadcastWrite = 1 << 21    /**< 支持广播写（地址 0） */
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
    uint64_t m_timestampMs;         /**< 快照采集时刻（单调毫秒） */
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
 * @brief 参数描述表条目（品牌提供 const 数组；支撑通用参数页自动生成）。
 */
typedef struct XVfdParamDesc {
    uint16_t m_addr;                /**< 通讯地址（已按品牌编码规则换算，如 511M 族 F0xxH） */
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
 * @brief 参数存取约束。
 */
typedef enum XVfdParamAccess {
    XVfdParamAccess_RW = 0,         /**< 可读可写 */
    XVfdParamAccess_ReadOnly,       /**< 只读（监视值） */
    XVfdParamAccess_RunLocked,      /**< 运行状态禁改（手册 ★） */
    XVfdParamAccess_Factory         /**< 厂家参数（禁止用户操作，手册 ▲） */
} XVfdParamAccess;

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
/*                      轮询项掩码                                            */
/* ========================================================================== */

/**
 * @brief 轮询项掩码位（setPollItems 按位或；对应品牌注册的轮询读组）。
 */
typedef enum XVfdPollItem {
    XVfdPollItem_Status = 1 << 0,       /**< 运行状态字 */
    XVfdPollItem_Telemetry = 1 << 1,    /**< 电气遥测（频率/电压/电流/功率/转矩等） */
    XVfdPollItem_Fault = 1 << 2,        /**< 故障码 */
    XVfdPollItem_Counters = 1 << 3,     /**< 计数/长度/累计量 */
    XVfdPollItem_Extended = 1 << 4      /**< 品牌扩展组 */
} XVfdPollItem;

/* ========================================================================== */
/*                      虚函数表                                              */
/* ========================================================================== */

XCLASS_DEFINE_BEGING(XVfdDevice)
XCLASS_DEFINE_ENUM(XVfdDevice, PollDue) = XCLASS_VTABLE_GET_SIZE(XObject),
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
 * @brief 变频器抽象父类（XObject 派生；业务语义全集，品牌差异在子类虚表实现）。
 */
typedef struct XVfdDevice {
    XObject m_class;                /**< 基类（第一成员，由 XClass 管理，禁止手工修改） */
    XVfdLink* m_link;               /**< 绑定的总线链路（借用，不转移所有权；未绑定为 NULL） */
    int m_unitId;                   /**< 从站地址（1~247；0=广播） */
    uint32_t m_caps;                /**< 能力位图（XVfdCapability 位或；子类 init 时置位） */
    XVfdStatus m_status;            /**< 最近状态快照（由子类解析回填） */
    bool m_haveStatus;              /**< m_status 是否有效 */
    bool m_busy;                    /**< 设备级单飞：动作/轮询读共用，请求在途为 true */
    uint32_t m_pollItems;           /**< 轮询项掩码（XVfdPollItem 位或；默认 Status|Telemetry|Fault） */
    uint32_t m_pollIntervalMs;      /**< 轮询周期毫秒（默认 500；0=该设备不轮询） */
    XVfdError m_lastError;          /**< 最近一次失败原因（Busy/NotSupported 等） */
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
/*                      绑定与配置                                            */
/* ========================================================================== */

/**
 * @brief 绑定总线链路与从站地址。
 * @param dev 设备实例
 * @param link 总线链路（借用，不取得所有权；设备不负责释放）
 * @param unitId 从站地址（1~247；0=广播）
 * @return 成功返回 true；参数非法返回 false
 * @note 轮询计划登记由子类/中间基类在本调用内完成（XVfdModbusLink 的计划表）。
 */
bool XVfdDevice_attach(XVfdDevice* dev, XVfdLink* link, int unitId);

/**
 * @brief 解绑链路并停止该设备的轮询计划。
 * @param dev 设备实例
 */
void XVfdDevice_detach(XVfdDevice* dev);

/**
 * @brief 获取从站地址。
 * @param dev 设备实例（可 NULL，返回 0）
 * @return 从站地址
 */
int XVfdDevice_unitId(const XVfdDevice* dev);

/**
 * @brief 设置从站地址（未绑定链路时自由改；已绑定需先 detach）。
 * @param dev 设备实例
 * @param unitId 从站地址（1~247；0=广播）
 */
void XVfdDevice_setUnitId(XVfdDevice* dev, int unitId);

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
 * @brief 查询是否有设备级请求在途（动作/轮询读共用设备级单飞）。
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
 * @brief 设置轮询项掩码（对已登记计划即时生效）。
 * @param dev 设备实例
 * @param items XVfdPollItem 位或；0=该设备不产生轮询读
 */
void XVfdDevice_setPollItems(XVfdDevice* dev, uint32_t items);

/**
 * @brief 获取轮询项掩码。
 * @param dev 设备实例（可 NULL，返回 0）
 * @return XVfdPollItem 位或
 */
uint32_t XVfdDevice_pollItems(const XVfdDevice* dev);

/**
 * @brief 设置轮询周期毫秒（对已登记计划即时生效）。
 * @param dev 设备实例
 * @param intervalMs 周期毫秒；0=该设备不轮询
 */
void XVfdDevice_setPollInterval(XVfdDevice* dev, uint32_t intervalMs);

/**
 * @brief 获取轮询周期毫秒。
 * @param dev 设备实例（可 NULL，返回 0）
 * @return 周期毫秒
 */
uint32_t XVfdDevice_pollInterval(const XVfdDevice* dev);

/* ========================================================================== */
/*                      动作接口（全部异步；true=受理，结果见信号）            */
/* ========================================================================== */

/**
 * @brief 启动电机（正转或反转；品牌状态机差异由子类消化）。
 * @param dev 设备实例
 * @param reverse true=反转
 * @return 请求已受理返回 true；未绑定/在途/不支持返回 false
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
 * @brief 写频率给定（工程单位 Hz；负值表示反转给定，标度换算由子类完成）。
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

/**
 * @brief 读任意功能码参数（原始值域；结果经 paramReadDone 信号）。
 * @param dev 设备实例
 * @param paramId 参数描述表下标（0 基；越界拒绝）
 * @return 请求已受理返回 true
 */
bool XVfdDevice_readParam(XVfdDevice* dev, uint16_t paramId);

/**
 * @brief 写任意功能码参数（原始值域；受描述表 access 约束，运行禁改直接拒绝）。
 * @param dev 设备实例
 * @param paramId 参数描述表下标（0 基）
 * @param raw 原始码值
 * @return 请求已受理返回 true；约束拒绝返回 false（lastError=ParamRunLocked）
 */
bool XVfdDevice_writeParam(XVfdDevice* dev, uint16_t paramId, int32_t raw);

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
