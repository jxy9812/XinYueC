/**
 * @file       XAutoControl.h
 * @brief      自动运行控制器公开 API：纯框架件（运行闸门/急停锁存/步进节流/
 *             动作记账/报警队列/状态序列化）。
 * @details    对齐 Qt 无直接对应类（语义对标工业现场"自动运行"控制层：
 *             设备由服务端按工艺逻辑自主动作，人只授意启停，不看命令回显）。
 *
 *             ============================================================================
 *                               职责边界（框架件 vs 用户逻辑 vs 设备访问）
 *             ============================================================================
 *             系统有两种运行形态：
 *               [手动] 人在客户端点按钮，命令经链路下到设备——**自动运行期间
 *                      全部远程控制被拒**，这是既有安全契约（模式闸在调用方）；
 *               [自动] 本控制器每拍被调用（XAutoController_step），由注册的
 *                      stepHandler 读反馈 → 决策 → 经使用方自己的设备访问层
 *                      下发。
 *             本类是**纯框架件**：运行闸门、急停锁存、步进节流、动作记账
 *             （noteAction）、报警队列与事件、状态快照与 BSON/JSON 序列化
 *             （XAutoControlCodec）。**工艺逻辑与设备访问一概不碰**：
 *               - 工艺逻辑（阀门时序、液位联锁、按阈值启停泵……）由使用者
 *                 以 XAutoController_setStepHandler 注册；
 *               - 设备访问（读哪个通道、写哪个口、"开阀"落到哪个 DO）是
 *                 **装配知识**，归使用方的适配层——stepHandler 的 ud 上下文
 *                 自带整个"世界"，本类对"设备"二字零认知，头文件里连通道
 *                 字样都不出现（见设计约束 3）。
 *
 *             ============================================================================
 *                               设计约束（写自动逻辑时必须遵守）
 *             ============================================================================
 *             与 Python 参考实现（hsfsrv/autocontrol.py）逐条同口径：
 *              1. **只在运行中被调**。stop() 之后 stepHandler 根本不会被调用
 *                 （本类闸门拦住）；急停锁存中更是全面冻结。切停时在途动作
 *                 **直接停摆**且本类刻意不做任何"收尾动作"（不关阀不停泵）
 *                 ——那会在操作员手里抢控制权。自动逻辑如需收尾，应在
 *                 stop() 之前自己做。
 *              2. **动作必须节流**。step 每拍（~100ms）一次，每拍都重复下发
 *                 同一条命令会把总线刷爆（现场模块对高频连发敏感，实测无间隔
 *                 连发会吃到空应答）。用 XAutoController_throttle 按 key 节流。
 *              3. **不要阻塞**。step 在事件循环里，任何超过几十毫秒的等待都会
 *                 拖慢遥测推送。设备访问归使用方适配层，"缓存读不发 IO"由
 *                 适配层自行保证——本类不认识设备，也拦不住，只能靠这条
 *                 约束+适配层实现自律。
 *              4. **失败停在安全侧**。动作结果经 XAutoController_noteAction
 *                 回灌（框架记账 + 失败原因），逻辑层**不要重试到底把总线
 *                 打爆**，等下一拍再说。
 *              5. **决策只认反馈，不认命令回显**。写下去只代表"命令发出去了"，
 *                 阀有没有真动要看到位反馈（与 XSolenoidValve 状态机同一条
 *                 口径）。
 *              6. **安全默认 = 什么都不做**。未注册 stepHandler、未 start、
 *                 急停锁存——全部静默跳过。哪怕误装配，系统行为也只是
 *                 "不自动动作"，绝不会因为半成品逻辑乱动现场设备。
 *
 *             ============================================================================
 *                               报警面（事件，供 XLink 推送远端）
 *             ============================================================================
 *             报警是**事件**（发生一次推一次），不是状态：定容循环队列
 *             （框架件 XCircularQueue，容量 XAUTO_CONTROL_ALARM_CAPACITY）+
 *             XAutoController_takeAlarm
 *             （取走即消费）+ alarmRaised 信号。框架自动入队的只有急停
 *             （EmergencyStop）；动作失败由逻辑层 noteAction(false, 原因)
 *             记账，是否报警由逻辑层调 raiseAlarm(ActionFailed, …) 决定——
 *             设备语义不在本类，"什么算值得报警的失败"也归逻辑层。
 *             队列满时挤掉最旧一条——远端只可能丢最老的历史，不可能丢最新
 *             告警。序列化见 XAutoControlCodec（alarmToBson/alarmToJson，
 *             远端管理用）。
 *
 *             对象为句柄性质：禁止拷贝/移动语义；可能在自身信号调用栈上被
 *             删除时，调用方必须使用 XObject_deleteLater。
 */
#ifndef XAUTOCONTROL_H
#define XAUTOCONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XObject.h"
/* 裸名解析：各目标的 include 清单都含 XContainer/XQueue/XCircularQueue 目录
 * （含只链共享库、拿不到 XinYueCore PUBLIC 头目录的 XinYueC_Dynamic 目标）；
 * 长路径 "XContainer/..." 需要 -I Src/XCore，该目标没有 ⇒ 编译期 fatal error。 */
#include "XCircularQueue.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#if XINDUSTRY_ON && XAUTO_CONTROL_ON

/* ==================== 常量 ==================== */

/** @brief 报警环形队列容量（条）。满时挤掉最旧一条（见头文件报警面说明）。 */
#define XAUTO_CONTROL_ALARM_CAPACITY 8

/** @brief 节流表容量（个 key）。耗尽后 throttle 直接过放行（安全侧：宁可多动
 *         一次也不许把逻辑永久卡死），并记 lastError 提示装配不合理。 */
#define XAUTO_CONTROL_THROTTLE_SLOTS 8

/** @brief 相位名缓冲容量（含结尾 0；短词口径，如 "idle"/"drain_wait"）。 */
#define XAUTO_CONTROL_PHASE_MAX 24

/** @brief lastError 缓冲容量（UTF-8；超长截断）。 */
#define XAUTO_CONTROL_ERROR_MAX 128

/**
 * @brief 自动运行状态（框架件；step 是否放行由它决定）。
 * @details 状态机：Stopped --start--> Running --stop--> Stopped；
 *          任一状态 --eStop--> EmergencyStop（**锁存**）--reset--> Stopped。
 *          [急停为什么锁存] 急停是"人宣布现场有危险"，解除必须由人**显式
 *          复位**——任何自动/超时/重启路径都不得悄悄解除（与 XButtonStation
 *          的急停触点、XSolenoidValve 的 Fault 同一条工业安全口径）。
 */
typedef enum XAutoControlState {
    XAutoControlState_Stopped = 0,        /**< 停止：step 恒跳过（安全默认）。 */
    XAutoControlState_Running = 1,        /**< 运行：step 按 MIN_STEP_INTERVAL 节流放行。 */
    XAutoControlState_EmergencyStop = 2   /**< 急停锁存：最高优先级，复位前拒绝启动。 */
} XAutoControlState;

/**
 * @brief 自动运行报警类型。
 * @details 框架自动入队的只有 EmergencyStop；ActionFailed / DeviceUnavailable
 *          / Custom 由逻辑层经 raiseAlarm 自行上报（设备语义不在本类）。
 *          数值稳定，可直接上线。
 */
typedef enum XAutoControlAlarmKind {
    XAutoControlAlarm_None = 0,              /**< 占位（禁用）。 */
    XAutoControlAlarm_ActionFailed = 1,      /**< 动作下发失败（逻辑层上报）。 */
    XAutoControlAlarm_DeviceUnavailable = 2, /**< 依赖设备不可用（逻辑层上报）。 */
    XAutoControlAlarm_Custom = 3,            /**< 用户逻辑自定义上报。 */
    XAutoControlAlarm_EmergencyStop = 4      /**< 急停触发（框架在 eStop 时自动入队）。 */
} XAutoControlAlarmKind;

/**
 * @brief 单条报警（值语义；队列元素与序列化输入同构）。
 */
typedef struct XAutoControlAlarm {
    int      m_kind;   /**< 报警类型（XAutoControlAlarmKind）。 */
    uint64_t m_atMs;   /**< 发生时刻（单调毫秒；调用方口径透传）。 */
    char     m_text[128]; /**< 报警文字（UTF-8，给操作员看）。 */
} XAutoControlAlarm;

/**
 * @brief 运行状态快照（值语义，零分配；序列化输入，XAutoControlCodec 消费）。
 */
typedef struct XAutoControlStatus {
    bool     m_running;          /**< 是否运行中。 */
    int      m_state;            /**< 状态（XAutoControlState；含急停锁存）。 */
    char     m_phase[XAUTO_CONTROL_PHASE_MAX]; /**< 当前相位名（UTF-8）。 */
    int64_t  m_actions;          /**< 累计成功动作数（诊断/测试断言用）。 */
    char     m_lastError[128];   /**< 最近一次动作失败原因（空串=无）。 */
    uint32_t m_stepIntervalMs;   /**< 步进最小间隔（毫秒；防御性节流）。 */
    int      m_pendingAlarms;    /**< 待取走报警条数。 */
} XAutoControlStatus;

/**
 * @brief 用户逻辑步进回调原型。
 * @param ud 注册时透传的上下文（XAutoController_setStepHandler 首参）——
 *           **逻辑层的"世界"**：设备访问句柄、点位表、适配层函数，都在里面，
 *           本类对内容零假设。
 * @param ctl 控制器本体（逻辑层经节流/报警/记账 API 回灌框架件）。
 * @param nowMs 当前单调毫秒（调用方 tick 口径透传；节流/计时统一用它）。
 * @details 每拍至多一次、且仅运行中被调用（闸门见设计约束 1/2）。回调内部
 *          **不得阻塞**（设计约束 3）；设备访问走 ud 里的适配层，动作结果
 *          经 noteAction 回灌。
 */
typedef void (*XAutoControlStepFn)(void* ud, struct XAutoController* ctl,
                                   uint64_t nowMs);

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XAutoController)
XCLASS_DEFINE_EXTEND_END(XAutoController, XObject)

/**
 * @brief XAutoController 自动运行控制器。
 * @details 成员禁止直接访问，一律经 API 操作。
 */
typedef struct XAutoController {
    XObject m_class;             /**< 第一个成员，基类 XObject，由 XClass 管理，禁止手工修改。 */
    XAutoControlStepFn m_stepFn; /**< 用户逻辑步进回调；NULL=安全默认（不动作）。 */
    void* m_stepUd;              /**< 步进回调透传上下文（逻辑层的"世界"）。 */
    int m_state;                 /**< 运行状态（XAutoControlState）。 */
    char m_phase[XAUTO_CONTROL_PHASE_MAX]; /**< 相位名（逻辑层自定；缺省 "idle"）。 */
    uint32_t m_minStepMs;        /**< 步进最小间隔（毫秒；缺省 50，防御性节流）。 */
    uint64_t m_lastStepMs;       /**< 上次步进时刻；0=尚未步进。 */
    int64_t m_actions;           /**< 累计成功动作数（noteAction 回灌口径）。 */
    char m_lastError[XAUTO_CONTROL_ERROR_MAX]; /**< 最近一次失败原因（noteAction 回灌；空串=无）。 */
    /** @brief 节流表：key → 上次放行时刻（0=未放行过）。线性查找，槽位极少。 */
    struct {
        char m_key[24];
        uint64_t m_lastMs;
    } m_throttle[XAUTO_CONTROL_THROTTLE_SLOTS];
    XCircularQueue m_alarms;     /**< 报警队列（框架件 XCircularQueue，FIFO，
                                      *   容量 XAUTO_CONTROL_ALARM_CAPACITY，
                                      *   元素 XAutoControlAlarm 值语义）。 */
} XAutoController;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XAutoController 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XAutoController_class_init(void);

/**
 * @brief 栈上初始化自动运行控制器（停止态、无步进回调）。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；初始相位 "idle"、步进间隔 50ms。
 */
void XAutoController_init(XAutoController* obj);

/**
 * @brief 堆上创建自动运行控制器。
 * @return 新控制器对象；内存分配失败返回 NULL。不再使用时必须调用
 *         XClassDelete 释放；对象可能仍被信号链引用时改用 XObject_deleteLater。
 */
XAutoController* XAutoController_create(void);

/* ==================== 用户逻辑装配 ==================== */

/**
 * @brief 注册用户逻辑步进回调（重复注册覆盖旧回调）。
 * @param ctl 目标控制器；不能为 NULL。
 * @param stepFn 步进回调；NULL=撤销（安全默认：不动作）。
 * @param ud 透传上下文——**逻辑层的"世界"**（设备适配句柄等；原样回传，
 *           可为 NULL）。
 * @return 设置成功返回 true；参数非法返回 false。
 */
bool XAutoController_setStepHandler(XAutoController* ctl,
                                    XAutoControlStepFn stepFn, void* ud);

/* ==================== 配置 ==================== */

/**
 * @brief 设置步进最小间隔（防御性节流；防将来 tick 频率提高时逻辑跑得过密）。
 * @param ctl 目标控制器；不能为 NULL。
 * @param intervalMs 最小间隔毫秒；0=不限（每 tick 都步进，不推荐）。
 * @return 设置成功返回 true；参数非法返回 false。
 * @note 缺省 50ms。只影响之后的步进判定，不改变运行状态。
 */
bool XAutoController_setMinStepIntervalMs(XAutoController* ctl, uint32_t intervalMs);

/**
 * @brief 读取步进最小间隔。
 * @param ctl 目标控制器；传入 NULL 返回 0。
 * @return 当前最小间隔毫秒；0 表示不限。
 */
uint32_t XAutoController_minStepIntervalMs(const XAutoController* ctl);

/* ==================== 生命周期（运行闸门：启动/停止/急停/复位） ==================== */

/**
 * @brief 进入运行（幂等）。
 * @param ctl 目标控制器；不能为 NULL。
 * @param nowMs 当前单调毫秒（调用方口径；用于复位步进计时）。
 * @return 由停止转运行返回 true；已在运行或**急停锁存中**返回 false。
 * @note 相位复位为 "idle"、步进计时清零；节流表**不清**——节流语义是
 *       "两次动作的最小间距"，跨运行会话仍然成立（与 Python 参考一致）。
 *       急停锁存中启动被拒绝：解除急停必须先 XAutoController_reset（人显式
 *       复位，任何自动路径不得绕过）。发 started 信号（仅状态实际变化时）。
 */
bool XAutoController_start(XAutoController* ctl, uint64_t nowMs);

/**
 * @brief 退出运行（幂等）。
 * @param ctl 目标控制器；不能为 NULL。
 * @param nowMs 当前单调毫秒（透传给 stopped 信号时刻口径）。
 * @return 由运行转停止返回 true；已停止或急停锁存中返回 false（急停是
 *         "更强的停止"，普通停止不解除锁存、也不算一次新停止）。
 * @note **刻意不做收尾动作**（不关阀不停泵，见设计约束 1）；在队报警保留
 *       （停机不该吃掉未消费的事件）。发 stopped 信号（仅状态实际变化时）。
 */
bool XAutoController_stop(XAutoController* ctl, uint64_t nowMs);

/**
 * @brief 急停：立入锁存态，最高优先级（任一状态可触发，含运行中）。
 * @param ctl 目标控制器；不能为 NULL。
 * @param nowMs 触发时刻（单调毫秒；透传给报警与信号）。
 * @return 首次进入急停返回 true；已处急停返回 false（幂等）。
 * @details [急停做什么] ① 状态置 EmergencyStop（step 立即失去放行资格）；
 *          ② 自动入队一条 EmergencyStop 报警（远端经 XLink 收到，见报警面）；
 *          ③ 发 eStopTriggered + stateChanged 信号。
 *          [急停不做什么] **不做任何设备收尾**——急停的语义边界在控制器
 *          自己（不再发起任何新动作）。设备级的急停（切断动力回路）是
 *          硬件链的职责（XButtonStation 急停触点 → 硬接线），软件层冒充
 *          "急停了设备"等于在安全问题上撒谎。
 */
bool XAutoController_eStop(XAutoController* ctl, uint64_t nowMs);

/**
 * @brief 复位：解除急停锁存，回到停止态。
 * @param ctl 目标控制器；不能为 NULL。
 * @param nowMs 复位时刻（单调毫秒）。
 * @return 急停锁存中复位成功返回 true；非急停态返回 false（无事可复位）。
 * @details 复位**不等于**重新启动：回到 Stopped，要继续自动运行须再 start。
 *          不清 lastError、不清在队报警——它们是历史事实，复位抹掉排查
 *          线索。发 resetDone + stateChanged 信号。
 */
bool XAutoController_reset(XAutoController* ctl, uint64_t nowMs);

/**
 * @brief 是否处于急停锁存。
 * @param ctl 目标控制器；传入 NULL 返回 false。
 */
bool XAutoController_estopped(const XAutoController* ctl);

/**
 * @brief 当前状态（Stopped/Running/EmergencyStop）。
 * @param ctl 目标控制器；传入 NULL 返回 XAutoControlState_Stopped。
 */
XAutoControlState XAutoController_state(const XAutoController* ctl);

/**
 * @brief 是否运行中（仅 Running 为真；急停锁存中恒 false）。
 * @param ctl 目标控制器；传入 NULL 返回 false。
 */
bool XAutoController_running(const XAutoController* ctl);

/**
 * @brief 每拍入口（调用方 tick 周期调用；闸门全在本函数内）。
 * @param ctl 目标控制器；不能为 NULL。
 * @param nowMs 当前单调毫秒（调用方口径）。
 * @details 放行条件（任一不满足直接返回，无副作用）：
 *            - 运行中（XAutoController_running）；
 *            - 已注册步进回调；
 *            - 距上次步进 ≥ m_minStepMs（0 时刻恒放行首次）。
 *          放行即调 stepFn(ud, ctl, nowMs)。本函数**不做任何设备动作**，
 *          动作只由用户逻辑发起——框架件不替人做工艺决策。
 */
void XAutoController_step(XAutoController* ctl, uint64_t nowMs);

/* ==================== 相位（逻辑层状态机扩展位） ==================== */

/**
 * @brief 设置相位名（逻辑层状态机相位；超长截断）。
 * @param ctl 目标控制器；不能为 NULL。
 * @param phase 相位名（UTF-8；NULL 视为空串）。
 * @return 无。
 */
void XAutoController_setPhase(XAutoController* ctl, const char* phase);

/**
 * @brief 读取相位名（借用）。
 * @param ctl 目标控制器；传入 NULL 返回空串。
 * @return 内部缓冲指针（禁释放；停止态不自动清相位——最后相位是排查线索）。
 */
const char* XAutoController_phase(const XAutoController* ctl);

/* ==================== 节流 ==================== */

/**
 * @brief 按 key 节流：距上次放行不足 intervalMs 返回 false（本次该动作被挡）。
 * @param ctl 目标控制器；不能为 NULL。
 * @param key 节流键（如 "open_valve_1"；≤23 字节，超长截断；相同前缀不同的
 *            动作用不同 key）。
 * @param intervalMs 最小间隔毫秒。
 * @param nowMs 当前单调毫秒（透传，便于测试注入）。
 * @return 放行返回 true（并记录本次时刻）；被挡返回 false。
 * @note 节流表容量 XAUTO_CONTROL_THROTTLE_SLOTS：key 首次出现占一个空槽；
 *       表满时**直接放行**且不记录（安全侧：宁可多动一次也不许把逻辑永久
 *       卡死），同时记 lastError 提示装配不合理。
 */
bool XAutoController_throttle(XAutoController* ctl, const char* key,
                              uint32_t intervalMs, uint64_t nowMs);

/* ==================== 动作记账（回灌口径） ==================== */

/**
 * @brief 回灌一次动作结果（框架记账：成功计数 / 失败记原因）。
 * @param ctl 目标控制器；不能为 NULL。
 * @param ok 动作是否被受理。
 * @param errUtf8 失败原因（UTF-8；ok=false 时有意义，超长截断；可为 NULL）。
 * @return 无。
 * @details [为什么记账在框架件] 用户逻辑几十个动作点若各自记账，口径必然
 *          漂移；收敛到本入口后，"动作数/最近失败"是单一事实源（状态快照
 *          与远端诊断都读它）。是否额外 raiseAlarm(ActionFailed) 由逻辑层
 *          决定——"什么算值得报警的失败"是设备语义，不归本类。
 */
void XAutoController_noteAction(XAutoController* ctl, bool ok, const char* errUtf8);

/* ==================== 报警面 ==================== */

/**
 * @brief 上报一条报警（逻辑层用；急停报警由框架在 eStop 时自动入队）。
 * @param ctl 目标控制器；不能为 NULL。
 * @param kind 报警类型（XAutoControlAlarmKind；Custom 之外的语义值也允许）。
 * @param text 报警文字（UTF-8；NULL 视为空串；超长截断）。
 * @param nowMs 发生时刻（单调毫秒；调用方口径透传）。
 * @return 入队成功返回 true；队列满挤掉最旧一条后仍然返回 true（永不因满
 *         而丢新告警）。参数非法返回 false。
 * @note 入队即发 alarmRaised 信号（远端经 XLink 订阅推送，见头文件报警面）。
 */
bool XAutoController_raiseAlarm(XAutoController* ctl, int kind,
                                const char* text, uint64_t nowMs);

/**
 * @brief 取走最旧一条在队报警（取走即消费；一条只该推一次）。
 * @param ctl 目标控制器。
 * @param outAlarm 输出报警（调用方提供存储；可为 NULL=仅消费）。
 * @return 有报警返回 true（并已出队）；队列空/参数非法返回 false。
 */
bool XAutoController_takeAlarm(XAutoController* ctl, XAutoControlAlarm* outAlarm);

/**
 * @brief 在队报警条数。
 * @param ctl 目标控制器；传入 NULL 返回 0。
 */
int XAutoController_pendingAlarmCount(const XAutoController* ctl);

/* ==================== 查询 ==================== */

/**
 * @brief 累计成功动作数（noteAction 回灌口径；测试断言"确实没动过"也用它）。
 * @param ctl 目标控制器；传入 NULL 返回 0。
 */
int64_t XAutoController_actionCount(const XAutoController* ctl);

/**
 * @brief 最近一次动作失败原因（借用；空串=无错误）。
 * @param ctl 目标控制器；传入 NULL 返回空串。
 */
const char* XAutoController_lastError(const XAutoController* ctl);

/**
 * @brief 填充运行状态快照（序列化输入；XAutoControlCodec 消费）。
 * @param ctl 目标控制器；可为 NULL（按"全零+停止"填充）。
 * @param out 输出快照（调用方提供存储；不能为 NULL）。
 */
void XAutoController_status(const XAutoController* ctl, XAutoControlStatus* out);

/* ==================== 信号 ==================== */

/**
 * @brief started 信号：由停止转入运行。
 * @param ctl 发射信号的控制器；经 XSignal() 宏取地址连接，禁止直调。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XAutoController_started_signal(XAutoController* ctl);

/**
 * @brief stopped 信号：由运行转回停止。
 * @param ctl 发射信号的控制器；经 XSignal() 宏取地址连接，禁止直调。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XAutoController_stopped_signal(XAutoController* ctl);

/**
 * @brief eStopTriggered 信号：急停触发（首次进入锁存态时发射一次）。
 * @param ctl 发射信号的控制器；经 XSignal() 宏取地址连接，禁止直调。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 * @note 急停同时入队一条 EmergencyStop 报警（alarmRaised 也会发）——信号
 *       给"谁触发了急停"的即时路径，报警给远端推送与留痕。
 */
void* XAutoController_eStopTriggered_signal(XAutoController* ctl);

/**
 * @brief resetDone 信号：急停锁存被复位（回到 Stopped）。
 * @param ctl 发射信号的控制器；经 XSignal() 宏取地址连接，禁止直调。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XAutoController_resetDone_signal(XAutoController* ctl);

/**
 * @brief stateChanged 信号：状态迁移（Stopped/Running/EmergencyStop）。
 * @param ctl 发射信号的控制器；经 XSignal() 宏取地址连接，禁止直调。
 * @param state 变化后的状态（XAutoControlState）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 * @details 与 started/stopped/eStopTriggered/resetDone 并存：后者是语义
 *          事件（挂哪个响应哪个），本信号是全量状态镜像（界面/远端状态
 *          表只订它一条就够，不必自己拼四个事件的时序）。
 */
void* XAutoController_stateChanged_signal(XAutoController* ctl, int state);

/**
 * @brief alarmRaised 信号：一条报警入队（急停自动入队与 raiseAlarm 同路）。
 * @param ctl 发射信号的控制器；经 XSignal() 宏取地址连接，禁止直调。
 * @param kind 报警类型（XAutoControlAlarmKind）。
 * @param atMs 发生时刻（单调毫秒）。
 * @param text 报警文字（UTF-8；借用，槽同步调用期间有效）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XAutoController_alarmRaised_signal(XAutoController* ctl, int kind,
                                         int64_t atMs, const char* text);

#endif /* XINDUSTRY_ON && XAUTO_CONTROL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XAUTOCONTROL_H */
