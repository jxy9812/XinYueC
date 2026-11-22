/**
 * @file       HsfServerAutoMode.h
 * @brief      服务端自动模式序列（产水/反洗/顺洗三模式状态机 + 全停 + 急停/复位）。
 * @details     与 Python 事实源 server_py/hsfsrv/autocontrol.py 的
 *             AutoController 三模式序列逐条同口径（2026-11-11 用户定案）：
 *               · 相位机 idle → valves（开选中阀/关未选阀，等全部到位）→
 *                 pumps（按配置启动搅拌泵/进水泵）→ running（保持运行，
 *                 等停止/急停）；estop 为急停锁存态（复位回 idle）。
 *               · 启动四闸顺序固定：手动拒 → 报警锁存拒 → 急停锁存拒 →
 *                 急停回路闸（未配置放行 / False 拒 / None 拒）→ 模式配置
 *                 校验（mode_config_error）。远程 0x0115 与现场按钮**同源
 *                 同一判据函数**（startPreconditions + modeConfigError）。
 *               · 阀相：逐路 HsfServerIo_valveSet，失败按 2000ms 节流重发；
 *                 判位只认 HsfServerIoValve_item().m_state（反馈），绝不认
 *                 命令受理值；Fault 或超 move_timeout+2000ms 宽限 ⇒ 报警
 *                 5 号（HSF_AUTO_ALARM_VALVE_TIMEOUT）+ 报警锁存 + 停控制器
 *                 + 回 idle 且**不动设备**。
 *               · 停止（op=2/现场停止/切模式）＝停控制器 + **全停设备**
 *                 （变频器减速停 → 逐台停泵 → 逐路关阀；逐台独立，部分失败
 *                 补 1 号告警「全停部分失败（#N）：…」）。这是 G4 修复：
 *                 原 C 端 op=2 只停控制器不停设备。
 *               · 急停（op=3）：框架锁存（XAutoController_eStop，首次自动入队
 *                 4 号告警）+ 全停 + 相位 estop；重复幂等不重复报警。
 *                 复位（op=4）：一把钥匙解急停锁存（框架 reset）+ 报警锁存
 *                 （本模块），回 idle **不重启**。
 *               · 每拍双入口：tick() 无条件被服务端 tick 调用——pollIdle
 *                 （现场按钮沿 + 急停回路闸，不依赖运行位）+ 仅运行中放行
 *                 step（框架 50ms 最小步进节流）；在队报警由调用方每拍
 *                 XAutoController_takeAlarm 逐条取走推送/入库。
 *               · 累计自动运行时长（2026-11-12）：服务端计（唯一事实源）——
 *                 只在自动运行真的跑起来期间累加，停止/急停/异常中止
 *                 （阀超时等）时结算这一段，跨多次运行**累计**保留；正在跑
 *                 时查询算到当前（runMs）。键 = HSF_KEY_AUTO_RUN_MS，与
 *                 server_py/hsfsrv/proto.py:471 的 KEY_AUTO_RUN_MS 逐值同步。
 *             [框架组合] 框架件 Src/Industry/XAutoControl（XAutoController）
 *             承担运行闸门/急停锁存/步进节流/报警队列/动作记账；工艺序列、
 *             模式粘性、报警锁存（第二把锁）与设备访问归本模块（框架对
 *             "设备"零认知，见 XAutoControl.h 设计约束 3）。
 *             [配置] 三模式配置（设备清单/搅拌转速/方向）存
 *             HsfServerSettings 的 auto/produce、auto/backwash、auto/rinse
 *             三个 JSON 串键；loadConfig 启动恢复，applyModeDoc 运行期
 *             立即生效并落盘。
 *             线程模型：单线程事件循环；全部 API 须在 XCoreApplication
 *             事件循环内调用。
 */
#ifndef HSFSERVERAUTOMODE_H
#define HSFSERVERAUTOMODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XhsfLinkProto.h"
#include <stdbool.h>
#include <stdint.h>

struct XBsonDocument;

/* ==================== 运维日志钩子（装配期注入） ==================== */

/**
 * @brief 运维事件日志函数原型（category/text 与 hsfAppEventLog 同口径）。
 * @param category 类别（"auto"/"alarm"…；UTF-8）。
 * @param text 事件文本（UTF-8；末尾不带换行）。
 */
typedef void (*HsfServerAutoModeLogFn)(const char* category, const char* text);

/* ==================== 装配 / 拆卸 ==================== */

/**
 * @brief 装配自动模式序列模块（幂等；HsfServerApp_start 调用一次）。
 * @return 就绪返回 true（含重复调用）；内部初始化失败返回 false。
 * @details 初始化框架控制器 + 注册步进回调 + 加载三模式配置（INI 缺键 =
 *          该模式保持缺省：不勾设备、搅拌电机满转速正转）。
 */
bool HsfServerAutoMode_setup(void);

/**
 * @brief 拆卸（停控制器 + 撤步进回调；HsfServerApp_stop 调用）。
 * @note  不动设备（进程关闭不替人做收尾动作，与框架 stop 契约一致）。
 */
void HsfServerAutoMode_shutdown(void);

/**
 * @brief 注入运维日志钩子（XPrintf 终端留痕 + logs 表入库由实现方做）。
 * @param fn 日志函数；NULL = 静默（测试用）。
 */
void HsfServerAutoMode_setLog(HsfServerAutoModeLogFn fn);

/* ==================== 查询（应答包络用） ==================== */

/** @brief 模块是否已装配。 */
bool HsfServerAutoMode_ready(void);

/**
 * @brief 框架控制器（借用；告警抽取 XAutoController_takeAlarm 用）。
 * @return 控制器指针；未装配返回 NULL。@warning 禁释放。
 */
struct XAutoController* HsfServerAutoMode_controller(void);

/** @brief 自动运行是否真的在跑（与"是不是自动模式"是两件事）。 */
bool HsfServerAutoMode_running(void);

/**
 * @brief 当前（选中/正在运行的）自动模式 id（粘性；从未选过 = 空串）。
 * @param buf 输出缓冲（不能为 NULL）；cap 缓冲容量。
 * @note  [与 reportedMode 的分工] 本函数给**内部粘性选择**（启动时未带
 *        mode 就沿用它，可能是组合模式 "auto"）；协议回显要用
 *        HsfServerAutoMode_reportedMode（循环时报**子阶段**，
 *        与 Python 控制器 self.mode 同口径）。
 */
void HsfServerAutoMode_modeId(char* buf, int cap);

/**
 * @brief 协议回显用的模式 id（对齐 Python `ctl.mode`）。
 * @details 组合模式运行中回**当前子阶段**（produce/backwash/rinse）；
 *          停止后（未复位）回**记住的子阶段**（客户端据此显示「将从X继续」）；
 *          单模式/未选时与 modeId 同值。
 */
void HsfServerAutoMode_reportedMode(char* buf, int cap);

/**
 * @brief 控制器相位（idle/valves/pumps/running/estop；字符串原样上线）。
 * @param buf 输出缓冲（不能为 NULL）；cap 缓冲容量。
 */
void HsfServerAutoMode_phase(char* buf, int cap);

/** @brief 报警锁存（模式序列报警后必须复位才能再启动）。 */
bool HsfServerAutoMode_faultLatched(void);

/**
 * @brief 设备级故障：报警（进报警历史表）+ **锁存** + 自动停设备。
 * @param text 告警文字（UTF-8）。
 * @param nowMs 发生时刻（单调毫秒）。
 * @param stopDevices true=顺带停设备（VFD/进水泵/电动球阀）；false=只
 *        锁存+报警（保护动作类告警已经自己停了，别重复停）。
 * @param enqueueAlarm true=把告警排进自动告警队列（统一发布+入库）；
 *        false=调用方自己已经发过这条告警（要带 dev 键单独发）⇒ 别重复排。
 * @return 受理返回 true（未装配返回 false）。
 * @details 2026-11-15 用户定案：「进了报警表的报警要锁存，并自动停止
 *          设备，必须手动复位才可以启动设备」。对应 Python 侧
 *          AutoController.raise_fault。
 */
bool HsfServerAutoMode_raiseFault(const char* text, uint64_t nowMs,
                                  bool stopDevices, bool enqueueAlarm);

/**
 * @brief 解除**报警锁存**（不动急停锁存）。
 * @details 人工复位（自动模式的复位按钮 / 设备侧「故障复位」）才解锁；
 *          急停锁存只能由现场物理复位解除。
 */
bool HsfServerAutoMode_clearFault(void);

/**
 * @brief 按键沿判据（**纯函数**；供回归锁直接验证，2026-11-15）。
 * @param prev  上一次电平（-1 = 未知/还没采到）。
 * @param level 本次电平（-1 = 读不到）。
 * @param nc    true=常闭（按下=0，沿是 1→0）；false=常开（按下=1）。
 * @return true = 本次是"按下沿"。
 * @details [安全语义] 常开沿要求「上一次电平**已知**且为 0」：第一帧读到
 *          1（开机即按住 / 输入浮空 / 模块首帧全 1）**不算按下** ⇒ 服务
 *          端一启动就自动启动（现场安全事故）。常闭沿要求「上一次已知且为
 *          1」（断线=按下，安全侧）。
 *          Python 侧 buttons.py 的沿判据同口径（两侧都要有回归锁）。
 */
bool HsfServerAutoMode_buttonEdgeRule(int prev, int level, bool nc);

/** @brief 急停锁存（框架 EmergencyStop 态）。 */
bool HsfServerAutoMode_estopLatched(void);

/**
 * @brief 急停回路三态（Python auto_ctx.estop_ok()/estop_configured() 的
 *        C 端单源；2026-11-12 VFD 控制急停前置闸也走这里）。
 */
typedef enum HsfServerAutoModeEstopState {
    HSF_ESTOP_CIRCUIT_OK = 0,     /**< 回路正常（有信号）。 */
    HSF_ESTOP_CIRCUIT_NO_SIGNAL,  /**< 无信号（按下或断线）⇒ 拦启动类。 */
    HSF_ESTOP_CIRCUIT_UNKNOWN,    /**< 状态未知（数字量模块离线）⇒ 拦。 */
    HSF_ESTOP_CIRCUIT_UNCONFIGURED /**< 未配置（无数字量站）⇒ 不拦。 */
} HsfServerAutoModeEstopState;

/**
 * @brief 读急停回路三态（判定与 HsfServerAutoMode_startPreconditions 同源）。
 * @details [C 端"配置"的定义] 点位是编译期宏（HSF_BTN_ESTOP_DI=12）——
 *          "配置了" = 数字量站已装配；无数字量站的现场回 UNCONFIGURED，
 *          与 Python estop_configured=False 同效果（不拦启动类命令）。
 */
HsfServerAutoModeEstopState HsfServerAutoMode_estopState(void);

/**
 * @brief **累计**自动运行时长（毫秒；正在跑时算到当前这一刻）。
 * @return 累计运行毫秒数；模块未装配返回 0。
 * @details [语义：跨多次运行累计] 只在自动运行**真的跑起来**期间累加；
 *          停止 / 急停 / 异常中止（阀超时等）时把本次的这一段结算进累计并
 *          冻结，**多次运行是累计而非单次**；正在跑时本函数算到当前。
 *          [持久化 2026-11-12] 结算即落盘 + 运行中每 60s 周期落盘 +
 *          setup() 启动恢复（键 run/auto_run_ms，与 Python cfg 的
 *          runtime.auto_run_ms 同口径）——服务端重启不归零。
 *          [服务端是唯一事实源] 客户端计时会因断线/重启/换客户端归零，两端
 *          数字对不上——服务端计，客户端只显示（Python 同款：
 *          server_py/hsfsrv/autocontrol.py 的 _run_accum_ms/_settle_run_time/
 *          run_ms 三件套）。
 * @note 时间口径与全站一致：单调毫秒（XDateTime_currentMSecsSinceEpoch）。
 */
uint64_t HsfServerAutoMode_runMs(void);

/* ==================== 启动判据（两入口同源） ==================== */

/**
 * @brief 启动四闸（顺序固定）：手动拒 → 报警锁存拒 → 急停锁存拒 → 急停
 *        回路闸（未配置放行 / False 拒「急停无信号（按下或断线）」/ None
 *        拒「急停状态未知（数字量模块离线）」）。
 * @param autoMode 当前是否自动模式。
 * @param outErr 拒绝原因输出（UTF-8；可为 NULL）；cap 容量。
 * @return 可启动返回 true；被拒返回 false 并写 outErr。
 * @note  [两入口同源] 远程 0x0115（逐条回错因）与现场启动按钮（只记日志）
 *        都调本函数——没有"现场能启动、远程不能启动"的分歧空间。模式配置
 *        校验由 modeConfigError 单独承担（Python 同款拆分：条件判定与
 *        模式校验报错方式不同）。
 */
bool HsfServerAutoMode_startPreconditions(bool autoMode, char* outErr, int cap);

/**
 * @brief 校验一个模式的配置能否跑（mode_config_error）。
 * @param modeId 模式 id（"" = 未选）。
 * @param outErr 拒绝原因输出（可为 NULL；空串 = 可跑）；cap 容量。
 * @return 可跑返回 true；被拒返回 false 并写 outErr。
 * @details 拒绝口径与 Python 逐字同源（_cycle_config_error +
 *          mode_config_error，2026-11-12 循环落地后口径）：「自动」组合模式
 *          按循环配置校验——未启用回「「自动」循环模式未启用（远端设置 →
 *          自动循环配置）」、子模式缺设备回「「自动」循环需要三个子模式都
 *          配设备，缺：…」；未选/未知 id 回「未选择自动模式（先在客户端
 *          主界面选模式）」；空配置回「模式[x]未配置设备（远端设置 → 自动
 *          模式配置）」；选中阀无点位/泵未装配点名拒。
 */
bool HsfServerAutoMode_modeConfigError(const char* modeId, char* outErr, int cap);

/* ==================== 四板斧（远程 0x0115 与现场按钮共用执行体） ==================== */

/**
 * @brief 启动自动运行（幂等；条件判定由调用方先过两个判据函数）。
 * @param modeId 本轮要跑的自动模式 id；NULL/空串 = 沿用粘性模式。
 * @param nowMs 当前单调毫秒。
 * @return 由停止转运行返回 true；已在运行/被内部闸拦住返回 false。
 * @details 阀相目标在此定型：选中阀开、未选阀关（未配置点位的阀不进目标
 *          表——不能自动操作，也不等它到位）。
 */
bool HsfServerAutoMode_start(const char* modeId, uint64_t nowMs);

/**
 * @brief **只选自动模式**（0x0115 op=5 的执行体；Python ctl.mode=+persist）。
 * @param modeId 模式 id（三子模式 + 组合循环 "auto"）。
 * @return 选成功（粘性已记 + 已落盘）返回 true；id 未知/未装配返回 false。
 * @details [语义] **不启动、不动设备、不判启动条件**——启动条件是启动
 *          时刻的判定（startPreconditions / modeConfigError），选模式只是
 *          记住「下次按启动跑哪个」。现场物理启动按钮用这份粘性（Python
 *          同款），所以它必须**落盘**（键 "run/auto_mode"）：只存内存时
 *          服务端重启即清空 ⇒ 现场按键被拒「未选择自动模式」
 *          （2026-11-13 板端实锅）。
 * @note  合法 id 集合 = 三子模式（produce/backwash/rinse）+ 组合循环
 *        （HSF_AUTO_MODE_ID_AUTO）；组合循环的**配置**校验留到启动时刻。
 */
bool HsfServerAutoMode_selectMode(const char* modeId);

/**
 * @brief 模式 id 是否合法（三子模式 + 组合循环 "auto"）。
 * @param modeId 模式 id（NULL/空串 = 不合法）。
 * @return 合法返回 true。
 * @details 合法集合 = produce/backwash/rinse + HSF_AUTO_MODE_ID_AUTO；
 *          **组合循环的配置**校验不在这里（留到启动时刻的
 *          modeConfigError）——选模式只是记粘性。
 */
bool HsfServerAutoMode_isKnownMode(const char* modeId);

/**
 * @brief 停止自动运行（op=2/现场停止）：停控制器 + **全停设备**（G4 修复）。
 * @param reason 日志原因（"远程停止自动运行"/"现场停止按钮"…）。
 * @param nowMs 当前单调毫秒。
 * @return 本次真停了控制器返回 true；急停锁存中/本就停着返回 false
 *         （但**全停照做**——安全场景不因状态机口径漏停设备）。
 * @note  停止后粘性模式保留（现场启动按钮/再次启动仍指向上次的模式）。
 */
bool HsfServerAutoMode_requestStop(const char* reason, uint64_t nowMs);

/**
 * @brief 只停控制器（**不动设备**；切回手动的单向安全动作用）。
 * @param reason 日志原因（"切回手动"…）。
 * @param nowMs 当前单调毫秒。
 * @return 本次真停了控制器返回 true；本就停着/急停锁存中返回 false。
 * @details 与 requestStop 的分工：退出自动模式时控制器必须停（否则手动
 *          模式下设备被偷偷驱动），但设备全停只在**真切换**时做（幂等重放
 *          不动设备——手动模式下设备正由人操作）。作废模式序列中间状态并
 *          回 idle，让相位回显与真实状态一致。
 */
bool HsfServerAutoMode_stopController(const char* reason, uint64_t nowMs);

/**
 * @brief 急停（op=3）：框架锁存（首次自动入队 4 号告警）+ 全停 + 相位 estop。
 * @param reason 日志原因（"远程急停"/"急停无信号…"）。
 * @param nowMs 触发时刻。
 * @return 首次进入急停返回 true；已锁存返回 false（幂等，不重复报警）。
 */
bool HsfServerAutoMode_requestEstop(const char* reason, uint64_t nowMs);

/**
 * @brief 复位（op=4）：一把钥匙解急停锁存（框架）+ 报警锁存（本模块），
 *        回 idle **不重启**。
 * @param nowMs 复位时刻。
 * @return 确实解除了某把锁返回 true；无事可复位返回 false。
 */
bool HsfServerAutoMode_requestReset(uint64_t nowMs);

/* ==================== 全停（切模式也走这里） ==================== */

/**
 * @brief 停全部受控设备：变频器减速停 → 逐台停泵 → 逐路关阀。
 * @param reason 日志原因（"切到自动"/"切回手动"/"急停"…）。
 * @param resetValves true=阀也关回原位；false=**阀保持现状**（不发送关
 *        阀命令——急停默认口径：泵与变频器停，阀可能停在中间位，叠一条
 *        “关”是叠加未知动作）。
 * @return 本次全停批次序号（≥1；从 1 起每次递增）。
 * @details 逐台独立：一台失败不挡停其它台；**只发命令不等到位**
 *          （停机是"尽力而为且必须尝试"）；部分失败入队 1 号告警
 *          「全停部分失败（#N）：…」（取走推送/入库由调用方每拍做）。
 */
int HsfServerAutoMode_stopAllDevicesEx(const char* reason, bool resetValves);

/**
 * @brief 全停（阀一定关回）——既有调用面的wrapper（切模式/未装配兼容）。
 * @param reason 日志原因。
 * @return 本次全停批次序号。
 */
int HsfServerAutoMode_stopAllDevices(const char* reason);

/**
 * @brief 当前子阶段计时（组合模式）：已运行毫秒 / 本阶段总毫秒。
 * @param outElapsed 输出已运行毫秒；**-1** = 不在自动运行 / 无阶段（不可知）。
 * @param outTotal 输出本阶段总毫秒；**0** = 本阶段没有定时（剩余不可知）。
 * @details 与 Python stage_timing 同口径：计时起点是**阀相**（阀到位、泵
 *          启动都占用该模式的运行时间）；单模式启动没有换相逻辑 ⇒ (-1,0)；
 *          产水只有定时兜底开启时才有总时长（否则按压差切换）。
 */
void HsfServerAutoMode_stageTiming(int64_t* outElapsed, int64_t* outTotal);

/**
 * @brief 注入压差取数回调（MPa；模拟量通道 0 经标定链换算）。
 * @param fn 取数函数；返回 true 且写 outMpa 才算读到；NULL = 永远读不到。
 * @details [为什么要注入] 标定表/越限文案在 HsfServerApp（hsfAppAiEngineering），
 *          自动模块不复制第二份换算链（否则「界面压差」与「切相反洗用的
 *          压差」会漂移）。装配期由 App 注入，与 Python ctx.press_mpa() 同义。
 */
typedef bool (*HsfServerAutoModePressFn)(double* outMpa);
void HsfServerAutoMode_setPressureFn(HsfServerAutoModePressFn fn);

/* ==================== 每拍入口（服务端 tick 无条件调用） ==================== */

/**
 * @brief 每拍入口：pollIdle（按钮沿 + 急停回路闸，**无条件**）+ step
 *        （仅运行中放行；框架自带 50ms 最小步进节流）。
 * @param nowMs 当前单调毫秒。
 * @param autoMode 当前是否自动模式（手动模式下启动请求一律不生效）。
 * @details 停止/急停必须在任何时刻都有人接（控制器没跑时按停止也得把
 *          设备停下来）——故按键受理与急停闸不依赖运行位。在队报警的
 *          抽取/推送/入库归调用方（XAutoController_takeAlarm），同样
 *          与模式/运行态无关。
 */
void HsfServerAutoMode_tick(uint64_t nowMs, bool autoMode);

/* ==================== auto 配置节（CONFIG GET/SET） ==================== */

/** @brief 单模式配置（值语义；设备清单按下标 + 搅拌电机参数）。 */
typedef struct HsfServerAutoModeCfg {
    bool m_devices[HSF_AUTO_DEVICE_COUNT]; /**< 设备勾选（下标见设备 id 表）。 */
    int  m_stirRpm;                        /**< 搅拌电机转速（rpm；夹 0..1440）。 */
    bool m_stirReverse;                    /**< 搅拌方向（true=反转）。 */
    /** [逐模式阀复位 2026-11-12] 停止时是否关阀复位（缺省 true）。 */
    bool m_resetValvesOnStop;
    /** [逐模式阀复位 2026-11-12] 急停时是否关阀复位（缺省 false）。 */
    bool m_resetValvesOnEstop;
    /** [逐模式换相策略 2026-11-14] 搅拌泵在换相窗口内怎么处理
     *  （stopRestart / carry / direct；缺省 stopRestart）。 */
    char m_stirSwitch[16];
    /** [逐模式换相策略 2026-11-14] 进水泵在换相窗口内怎么处理（同上）。 */
    char m_inletSwitch[16];
} HsfServerAutoModeCfg;

/**
 * @brief 自动循环（组合模式「自动」）配置（值语义；auto 节的 cycle 子节）。
 * @details 与 Python `cfg.auto_cycle` 同键同义：循环顺序固定
 *          产水 →（压差达标 或 定时兜底）→ 反洗 →（定时）→ 顺洗 →（定时）
 *          → 产水，直到人按停止。
 *          [压差读不到不切] pressMpa 未知（模拟量站离线/未标定）时不按缺
 *          数据误判，只走定时兜底（若开启）。
 */
typedef struct HsfServerAutoModeCycleCfg {
    bool   m_enabled;         /**< 循环启用（关=组合模式拒启动）。 */
    double m_pressThreshold;  /**< 压差阈值（MPa；0..10）。 */
    char   m_pressCompare[8]; /**< 比较方向（"gt"/"lt"）。 */
    bool   m_timerFallback;   /**< 产水定时兜底（缺省关）。 */
    int    m_timerMinutes;    /**< 产水兜底分钟（1..1440）。 */
    int    m_backwashMinutes; /**< 反洗分钟（1..1440）。 */
    int    m_rinseMinutes;    /**< 顺洗分钟（1..1440）。 */
} HsfServerAutoModeCycleCfg;

/**
 * @brief 模式 id → 下标（0..HSF_AUTO_MODE_COUNT-1）；组合模式 "auto" 与
 *        未知 id 返回 -1。
 */
int HsfServerAutoMode_modeIndex(const char* modeId);

/** @brief 模式下标 → 显示名（借用静态常量；越界返回 ""）。 */
const char* HsfServerAutoMode_modeName(int index);

/**
 * @brief 设备 id → 下标（0..HSF_AUTO_DEVICE_COUNT-1）；未知返回 -1。
 * @details id 表与 proto.py AUTO_DEVICES 同序：0..5 = valve1..valve6、
 *          6 = stirPump、7 = inletPump。
 */
int HsfServerAutoMode_deviceIndex(const char* deviceId);

/**
 * @brief 应用一个模式的配置（CONFIG SET section=auto 的执行体）。
 * @param modeId 模式 id（未知 id 拒）。
 * @param modeDoc 该模式的配置对象（devices/stirRpm/stirReverse 三键可缺——
 *                缺键 = 该项保持现值；与 Python 逐键可选同口径）。
 * @param outErr 拒绝原因输出（可为 NULL）；cap 容量。
 * @return 受理并已生效返回 true（含落盘）；被拒返回 false（配置不动）。
 * @details 校验与 Python _h_config_set 同口径：devices 非数组拒；未知设备
 *          id 丢弃（前向兼容），但「有内容且全未知」拒（客户端键形错了要
 *          让人看见）；stirRpm 夹取 0..1440、非整数拒。
 */
bool HsfServerAutoMode_applyModeDoc(const char* modeId,
                                    const struct XBsonDocument* modeDoc,
                                    char* outErr, int cap);

/**
 * @brief 构建 auto 配置节应答文档 {"modes":{…},"cycle":{…},"buttons":{…}}。
 * @return 新建 XBsonDocument；分配失败返回 NULL。@warning 调用方 XClassDelete。
 * @details CONFIG GET/SET 应答共用（modes/cycle 键形 = Python
 *          _config_auto_doc，客户端两端零改动消费；buttons 为 C 端扩展子节，
 *          Python 服务端/客户端不含它、应答缺键不误改）。modes 每模式含
 *          resetValvesOnStop/OnEstop 两键。
 */
struct XBsonDocument* HsfServerAutoMode_configDoc(void);

/**
 * @brief 应用 cycle 子节（CONFIG SET section=auto 且带 cycle 时）。
 * @param cycleDoc cycle 对象（逐键可选——缺键 = 该项保持现值）。
 * @param outErr 拒绝原因输出（可为 NULL）；cap 容量。
 * @return 受理并已生效（含落盘）返回 true；被拒返回 false。
 * @details [逐键 fail-closed] 与 Python _cfg_set_auto_cycle 同口径：
 *          pressThreshold 非数字拒（合法值夹 0..10）；pressCompare 只收
 *          gt/lt（写错一个键会把「压差大于才切」变成「小于才切」＝流程
 *          反了）；分钟数为整数且夹 1..1440。
 */
bool HsfServerAutoMode_applyCycleDoc(const struct XBsonDocument* cycleDoc,
                                     char* outErr, int cap);

/**
 * @brief 应用 buttons 子节（按键触点类型与去抖；2026-11-13·C 端扩展）。
 * @param buttonsDoc buttons 对象（键可选：debounceMs / normallyClosed）。
 * @param outErr 拒绝原因输出（可为 NULL）；cap 容量。
 * @return 受理并已生效（含落盘）返回 true；被拒返回 false。
 * @details [对齐 Python] 触点类型与去抖由**现场接线**决定，不是软件常量
 *          （buttons.py 同口径）。键名与 Python buttons.py KEY_* 同字面：
 *          normallyClosed 收 "start_di"/"stop_di"/"reset_di"（未知 id 丢弃、
 *          空数组=三键全常开）；debounceMs 非负整数、非法值明确拒。
 *          缺省：300ms + 启动/复位常开、停止常闭。SET 立即生效。
 */
bool HsfServerAutoMode_applyButtonsDoc(const struct XBsonDocument* buttonsDoc,
                                       char* outErr, int cap);

/**
 * @brief 当前循环配置快照（借用；未装配也返回缺省结构）。
 * @return 静态存储的配置拷贝指针。@warning 禁释放。
 */
const HsfServerAutoModeCycleCfg* HsfServerAutoMode_cycleConfig(void);

/**
 * @brief 启动恢复：从 HsfServerSettings 读回三模式配置（缺键 = 缺省）。
 * @note  setup() 已调用一次；公开给"改 INI 后手动重载"与测试。
 */
void HsfServerAutoMode_loadConfig(void);

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERAUTOMODE_H */
