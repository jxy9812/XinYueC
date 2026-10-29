/**
 * @file       XSolenoidValve.h
 * @brief      电磁阀设备抽象公开 API。
 * @details    对齐 Qt 无直接对应类（语义对标工业现场电磁阀/气动阀设备）。
 *             XSolenoidValve 是纯业务层设备：只依赖通道接口族
 *             （XDigitalOutput 动作出口 + XDigitalInput 开/关到位反馈），
 *             **不关心动作发往何处、信号来自何处**——GPIO、Modbus 或未来
 *             任何传输都经 Industry/XChannel 的适配器在装配期注入
 *             （attach，借用不拥有）。类内实现阀位状态机、动作超时监督
 *             （XObject 自带定时器）与双限位矛盾检测，结果经信号对外
 *             发布。本类永不 include 任何具体传输头文件。
 *             反馈两种口径：DualLimit（双限位开关真实到位）与 None（无
 *             限位开关的普通电磁阀，以动作出口 writeDone(true) 推断到位）。
 *             设备对象为句柄性质：禁止拷贝/移动语义；对象可能在自身信号
 *             调用栈上被删除时，调用方必须使用 XObject_deleteLater。
 */
#ifndef XSOLENOIDVALVE_H
#define XSOLENOIDVALVE_H

#ifdef __cplusplus
extern "C" {
#endif


#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XObject.h"
#include "XChannel/XDigitalInput.h"
#include "XChannel/XDigitalOutput.h"
#if XINDUSTRY_ON && XSOLENOID_VALVE_ON

/**
 * @brief 电磁阀阀位状态。
 * @details 状态机：Unknown --open--> Opening --到位/确认--> Open --close-->
 *          Closing --到位/确认--> Closed；任一在途状态超时、通道写入失败
 *          或双限位矛盾进入 Fault；Fault 后可直接发起新命令（作为复位）。
 */
typedef enum XSolenoidValveState {
    XSolenoidValveState_Unknown = 0, /**< 上电未定位/位置丢失：两个限位均无效且无在途动作。 */
    XSolenoidValveState_Opening,     /**< 开命令已受理，等待开到位。 */
    XSolenoidValveState_Open,        /**< 开到位。 */
    XSolenoidValveState_Closing,     /**< 关命令已受理，等待关到位。 */
    XSolenoidValveState_Closed,      /**< 关到位。 */
    XSolenoidValveState_Fault        /**< 故障：动作超时/写入失败/双限位矛盾。 */
} XSolenoidValveState;

/**
 * @brief 电磁阀到位反馈口径。
 */
typedef enum XSolenoidValveFeedbackMode {
    XSolenoidValveFeedbackMode_None = 0,     /**< 无限位开关：动作出口 writeDone(true) 即推断到位（普通电磁阀）。 */
    XSolenoidValveFeedbackMode_DualLimit     /**< 双限位开关：开/关到位输入有效才认定到位。 */
} XSolenoidValveFeedbackMode;

/**
 * @brief 电磁阀错误码。
 */
typedef enum XSolenoidValveError {
    XSolenoidValveError_None = 0,             /**< 无错误。 */
    XSolenoidValveError_NotAttached,          /**< 动作出口未挂接，命令被拒绝。 */
    XSolenoidValveError_WriteRejected,        /**< 动作出口同步拒绝写入（通道未实现/未挂接/忙碌）。 */
    XSolenoidValveError_WriteFailed,          /**< 动作出口异步确认失败（writeDone(false)，仅在途状态判故障）。 */
    XSolenoidValveError_Timeout,              /**< 在途动作超时未到位。 */
    XSolenoidValveError_LimitContradiction,   /**< 开/关到位同时有效，传感器矛盾。 */
    XSolenoidValveError_Internal              /**< 内部资源失败（如监督定时器启动失败）。 */
} XSolenoidValveError;

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XSolenoidValve)
XCLASS_DEFINE_EXTEND_END(XSolenoidValve, XObject)

/**
 * @brief XSolenoidValve 电磁阀设备。
 * @details 成员禁止直接访问，一律经 API 操作。三个通道指针均为借用，
 *          不拥有：调用方保证通道生命周期覆盖电磁阀，并在通道销毁前
 *          detach 或先删阀。
 */
typedef struct XSolenoidValve {
    XObject m_class;                              /**< 第一个成员，基类 XObject，由 XClass 管理，禁止手工修改。 */
    XDigitalOutput* m_output;                     /**< 动作出口（借用，可为 NULL=未挂接）；true=开阀，false=关阀。 */
    XDigitalInput* m_openLimit;                   /**< 开到位反馈（借用，可为 NULL）；DualLimit 模式参与到位判定。 */
    XDigitalInput* m_closeLimit;                  /**< 关到位反馈（借用，可为 NULL）；DualLimit 模式参与到位判定。 */
    XSolenoidValveFeedbackMode m_feedbackMode;    /**< 到位反馈口径，默认 None。 */
    XSolenoidValveState m_state;                  /**< 当前阀位状态，默认 Unknown。 */
    uint32_t m_timeoutMs;                         /**< 在途动作监督超时（毫秒）；0=不监督。默认 5000。 */
    XTimerId m_guardTimer;                        /**< 监督定时器 id；无效时为 XTIMER_INVALID_ID。 */
} XSolenoidValve;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XSolenoidValve 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XSolenoidValve_class_init(void);

/**
 * @brief 栈上初始化电磁阀。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；初始状态 Unknown、反馈口径 None、超时 5000ms。
 */
void XSolenoidValve_init(XSolenoidValve* obj);

/**
 * @brief 堆上创建电磁阀。
 * @return 新电磁阀对象；内存分配失败返回 NULL。不再使用时必须调用
 *         XClassDelete 释放；对象可能仍被信号链引用时改用 XObject_deleteLater。
 */
XSolenoidValve* XSolenoidValve_create(void);

/* ==================== 装配（借用注入，解耦点） ==================== */

/**
 * @brief 挂接动作出口。
 * @param valve 目标电磁阀；不能为 NULL。
 * @param output 动作出口通道；借用，不取得所有权，不能为 NULL（解挂用
 *               XSolenoidValve_detachOutput）。true 写入=开阀，false=关阀。
 * @return 挂接并完成信号订阅返回 true；参数非法或信号订阅失败返回 false，
 *         阀保持未挂接状态。
 * @note 重复挂接先自动解挂旧出口。挂接后阀位状态复位为 Unknown。
 */
bool XSolenoidValve_attachOutput(XSolenoidValve* valve, XDigitalOutput* output);

/**
 * @brief 挂接到位反馈（开/关到位两个输入，可任一为 NULL）。
 * @param valve 目标电磁阀；不能为 NULL。
 * @param openLimit 开到位输入通道；借用，可为 NULL（无该限位）。
 * @param closeLimit 关到位输入通道；借用，可为 NULL（无该限位）。
 * @return 至少一个通道非 NULL 且信号订阅全部成功返回 true；两通道均为
 *         NULL 或任一订阅失败返回 false，订阅失败的通道不生效。
 * @note 重复挂接先自动解挂旧反馈。挂接成功后立即同步读取各限位播种阀位
 *       状态（DualLimit 模式下可直接落 Open/Closed）。
 */
bool XSolenoidValve_attachFeedback_2(XSolenoidValve* valve,
                                     XDigitalInput* openLimit,
                                     XDigitalInput* closeLimit);

/**
 * @brief 解挂动作出口并断开其信号订阅。
 * @param valve 目标电磁阀；不能为 NULL。
 * @return 解挂了已挂接的出口返回 true；原本未挂接返回 false。
 * @note 解挂不改变当前阀位状态，也不再推进在途动作（在途监督超时会照常
 *       判故障）。
 */
bool XSolenoidValve_detachOutput(XSolenoidValve* valve);

/**
 * @brief 解挂全部到位反馈并断开其信号订阅。
 * @param valve 目标电磁阀；不能为 NULL。
 * @return 解挂了任一已挂接的反馈返回 true；原本未挂接返回 false。
 */
bool XSolenoidValve_detachFeedback(XSolenoidValve* valve);

/* ==================== 配置 ==================== */

/**
 * @brief 设置到位反馈口径。
 * @param valve 目标电磁阀；不能为 NULL。
 * @param mode 反馈口径；见 XSolenoidValveFeedbackMode。
 * @return 设置成功返回 true；参数非法返回 false，口径保持不变。
 * @note 默认 None。切回 DualLimit 前应先挂接反馈；DualLimit 模式下一次
 *       反馈变化会重读双限位并重定位阀位。
 */
bool XSolenoidValve_setFeedbackMode(XSolenoidValve* valve,
                                    XSolenoidValveFeedbackMode mode);

/**
 * @brief 读取到位反馈口径。
 * @param valve 目标电磁阀；传入 NULL 返回 XSolenoidValveFeedbackMode_None。
 * @return 当前反馈口径。
 */
XSolenoidValveFeedbackMode XSolenoidValve_feedbackMode(const XSolenoidValve* valve);

/**
 * @brief 设置在途动作监督超时。
 * @param valve 目标电磁阀；不能为 NULL。
 * @param timeoutMs 超时毫秒数；0=不监督（不推荐），已挂接命令后生效。
 * @return 设置成功返回 true；参数非法返回 false，超时值保持不变。
 * @note 默认 5000ms。只影响之后发起的动作，不改变已在途动作的剩余时间。
 */
bool XSolenoidValve_setTimeoutMs(XSolenoidValve* valve, uint32_t timeoutMs);

/**
 * @brief 读取在途动作监督超时。
 * @param valve 目标电磁阀；传入 NULL 返回 0。
 * @return 当前超时毫秒数；0 表示不监督。
 */
uint32_t XSolenoidValve_timeoutMs(const XSolenoidValve* valve);

/* ==================== 动作 ==================== */

/**
 * @brief 开阀（异步语义：返回只表示命令受理，到位经信号发布）。
 * @param valve 目标电磁阀；不能为 NULL。
 * @return 命令已被动作出口受理返回 true（随后经 opened/stateChanged 或
 *         commandTimeout 发布结果）；出口未挂接、写入被同步拒绝或参数
 *         非法返回 false（此时阀进入 Fault 并发 errorOccurred）。
 * @note 已在开位（Open）时幂等返回 true，不重复驱动出口；Fault 状态下
 *       发起本命令视为复位重试。None 口径下 writeDone(true) 即判开到位；
 *       DualLimit 口径下开到位输入有效才判开到位。
 */
bool XSolenoidValve_open(XSolenoidValve* valve);

/**
 * @brief 关阀（异步语义：返回只表示命令受理，到位经信号发布）。
 * @param valve 目标电磁阀；不能为 NULL。
 * @return 命令已被动作出口受理返回 true；失败路径同 XSolenoidValve_open。
 * @note 已在关位（Closed）时幂等返回 true；其余语义与 open 对称。
 */
bool XSolenoidValve_close(XSolenoidValve* valve);

/* ==================== 查询 ==================== */

/**
 * @brief 读取当前阀位状态。
 * @param valve 目标电磁阀；传入 NULL 返回 XSolenoidValveState_Unknown。
 * @return 当前状态；状态每次变化都会先发 stateChanged。
 */
XSolenoidValveState XSolenoidValve_state(const XSolenoidValve* valve);

/**
 * @brief 判断是否开到位。
 * @param valve 目标电磁阀；传入 NULL 返回 false。
 * @return 状态为 Open 返回 true，其余返回 false。
 */
bool XSolenoidValve_isFullyOpen(const XSolenoidValve* valve);

/**
 * @brief 判断是否关到位。
 * @param valve 目标电磁阀；传入 NULL 返回 false。
 * @return 状态为 Closed 返回 true，其余返回 false。
 */
bool XSolenoidValve_isFullyClosed(const XSolenoidValve* valve);

/* ==================== 信号 ==================== */

/**
 * @brief stateChanged 信号：阀位状态变化。
 * @param valve 发射信号的电磁阀；经 XSignal() 宏取地址连接，禁止直调。
 * @param state 变化后的状态。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XSolenoidValve_stateChanged_signal(XSolenoidValve* valve,
                                         XSolenoidValveState state);

/**
 * @brief opened 信号：进入开到位状态。
 * @param valve 发射信号的电磁阀；经 XSignal() 宏取地址连接，禁止直调。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 * @note 无论到位由命令推进还是外部因素（手动作动）造成，进入 Open 均发射。
 */
void* XSolenoidValve_opened_signal(XSolenoidValve* valve);

/**
 * @brief closed 信号：进入关到位状态。
 * @param valve 发射信号的电磁阀；经 XSignal() 宏取地址连接，禁止直调。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 * @note 无论到位由命令推进还是外部因素造成，进入 Closed 均发射。
 */
void* XSolenoidValve_closed_signal(XSolenoidValve* valve);

/**
 * @brief commandTimeout 信号：在途动作超时未到位。
 * @param valve 发射信号的电磁阀；经 XSignal() 宏取地址连接，禁止直调。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 * @note 发射后阀进入 Fault；timeoutMs 为 0 时永不发射。
 */
void* XSolenoidValve_commandTimeout_signal(XSolenoidValve* valve);

/**
 * @brief errorOccurred 信号：电磁阀发生错误。
 * @param valve 发射信号的电磁阀；经 XSignal() 宏取地址连接，禁止直调。
 * @param error 错误码（XSolenoidValveError 枚举值）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XSolenoidValve_errorOccurred_signal(XSolenoidValve* valve, int error);

#endif /* XINDUSTRY_ON && XSOLENOID_VALVE_ON */

#ifdef __cplusplus
}
#endif
#endif /* XSOLENOIDVALVE_H */
