/******************************************************************************
 * @file       XPlatformPower.h
 * @brief      平台电源管理契约（屏幕电源 + 系统电源 + 空闲策略）。
 * @details    对标 Qt 6.8 的 QScreenSaver（屏保/熄屏策略面）与
 *             QPlatformScreen（屏幕能力面）的组合，但只承载【电源动作】
 *             这一条线，不含屏保程序、不含亮度调节：
 *
 *             - 屏幕电源（XPlatformPowerScreenState）：只有「正常 On」与
 *               「熄屏 Off」两档。熄屏的语义是【关屏但系统继续运行】——
 *               事件循环、渲染、远程会话全部照常，只有屏幕不再输出画面。
 *               刻意不设「降亮」中间档：降亮在 fbdev 上要背光 PWM、在
 *               X11 上无对应语义，跨平台语义不齐反而制造假能力。
 *             - 系统电源（XPlatformPowerSystemState）：Active 与 Suspend
 *               两档。Suspend 是【整机挂起】（内存保持、CPU 停转），
 *               与屏幕电源是**两个独立维度**：先熄屏再挂起是常见链路，
 *               但两者能力各自独立探测（很多平台能关屏却不能挂起）。
 *             - 空闲策略（XPlatformPowerPolicy）：一组「空闲多久做什么」
 *               的阈值，由应用一次性设置。框架提供「多久没动」这一原语
 *               （noteActivity），**不内置任何默认超时**——阈值是应用
 *               策略，默认「从不」。
 *
 *             平台 API 只允许出现在 Drive（见 Drive/README.md）；本头
 *             是公共层唯一可依赖的电源面，公共层不出现任何平台调用。
 *
 *             API 形状说明：公共面 10 个入口，超时阈值统一收进一个
 *             策略结构体——避免出现「一组 set/get 配对函数」把契约面
 *             撑大。驱动面 6 个钩子。**本头只放类型与声明**（仓库头
 *             文件结构规范），实现一律在同名 .c 与 Drive 侧。
 * @note       本契约只受 XGUI_ON 门控（对标 XPlatformScreen），**不受
 *             XPLATFORMINTEGRATION_ON 门控**——桌面集成面在嵌入式裁剪
 *             构建里整体消失，而熄屏恰恰是嵌入式场景的主诉求。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XPLATFORMPOWER_H
#define XPLATFORMPOWER_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"

/** @brief 屏幕电源状态（两档，无降亮中间档）。 */
typedef enum {
    XPlatformPowerScreenState_On  = 0, /**< 正常显示。 */
    XPlatformPowerScreenState_Off = 1  /**< 熄屏：屏幕关闭，系统继续运行。 */
} XPlatformPowerScreenState;

/** @brief 屏幕**硬件实况**（对账用，与影子状态 XPlatformPowerScreenState
 *          分属两个维度：影子=「本契约最近一次动作的目标态」，实况=
 *          「硬件此刻的真实态」）。 */
typedef enum {
    XPlatformPowerScreenActual_Unknown = 0, /**< 平台无读回原语或读数失败。 */
    XPlatformPowerScreenActual_On      = 1, /**< 硬件确认在输出（背光>0 等）。 */
    XPlatformPowerScreenActual_Off     = 2  /**< 硬件确认已熄（背光=0 等）。 */
} XPlatformPowerScreenActual;

/** @brief 系统电源状态（两档）。 */
typedef enum {
    XPlatformPowerSystemState_Active  = 0, /**< 系统正常运行。 */
    XPlatformPowerSystemState_Suspend = 1  /**< 系统挂起（内存保持、CPU 停转）。 */
} XPlatformPowerSystemState;

/** @brief 电源管理能力位（各维度独立探测，位与位之间不互斥）。
 *  @note  「主动关屏」与「主动唤醒」合为一位——两者在本契约覆盖的全部
 *         平台上总是同时具备（能关屏的平台都能点亮），分列只会让调用
 *         方写出「能关不能唤醒」这种现实中不存在的组合。 */
typedef enum {
    XPlatformPowerCap_ScreenControl = 1u << 0, /**< 支持**应用主动**熄屏与唤醒。 */
    XPlatformPowerCap_ScreenTimeout = 1u << 1, /**< 支持**设置自动熄屏时长**
                                                *   （委托平台/系统自己计时）。 */
    XPlatformPowerCap_SystemSuspend = 1u << 2  /**< 支持系统挂起。 */
} XPlatformPowerCapability;

/** @brief 输入来源掩码（决定熄屏后哪些输入能唤醒屏幕）。
 *  @note  滚轮归入「鼠标」类——用户预期「鼠标唤醒」含滚轮唤醒。 */
typedef enum {
    XPlatformPowerInputMouse    = 1u << 0, /**< 鼠标（含移动与滚轮）。 */
    XPlatformPowerInputKeyboard = 1u << 1, /**< 键盘。 */
    XPlatformPowerInputTouch    = 1u << 2, /**< 触摸。 */
    XPlatformPowerInputAll      = 0xFFu    /**< 全部（缺省）。 */
} XPlatformPowerInputMask;

/** @brief 电源策略（一组「空闲多久做什么」+「谁能唤醒」的设置）。
 *  @details 两个超时互相独立；0=该项「从不」。系统挂起阈值小于熄屏
 *           阈值时以挂起为准（框架在挂起前先熄屏）。
 *           熄屏后**只能靠被允许的输入源唤醒**——屏幕已灭，界面上的
 *           「立即唤醒」按钮物理上不可达，唤醒路径只能是输入设备。 */
typedef struct {
    int64_t m_screenOffMs;     /**< 空闲多久后熄屏（0=从不）。 */
    int64_t m_systemSuspendMs; /**< 空闲多久后系统挂起（0=从不）。 */
    unsigned m_wakeInputs;     /**< 熄屏后哪些输入可唤醒
                                 *   （XPlatformPowerInputMask 位集；0=无输入
                                 *   可唤醒——**调用方应避免**，否则熄屏后只能
                                 *   靠外部手段恢复）。 */
} XPlatformPowerPolicy;

/* ==================== 平台钩子（Drive 实现，Unsupported 哨兵兜底） ==================== */

/** @brief 本平台支持的电源管理能力位集合。
 *  @return XPlatformPowerCapability 的按位或组合；**0=本平台不具备任何
 *          电源管理能力**（含「Activity 未就绪」等未就绪态，调用方按
 *          不支持退化，无需另设 isAvailable 探针）。 */
unsigned XPlatformPowerDriver_capabilities(void);

/** @brief 设置屏幕电源状态。
 *  @param state 目标状态（On=点亮屏幕，Off=熄屏）。
 *  @return true=状态已切换；false=平台不支持或切换失败。 */
bool XPlatformPowerDriver_setScreenState(XPlatformPowerScreenState state);

/** @brief 读屏幕**硬件实况**（影子/硬件对账的数据源）。
 *  @return XPlatformPowerScreenActual 档位；**Unknown=平台无读回原语或
 *          本次读数失败**——公共层对 Unknown 不得做任何矫正（否则会把
 *          「读不到」当成「熄屏」误点亮）。
 *  @details 为什么需要本钩子：影子状态只记录「本契约最后一次动作的
 *           目标态」，而背光会被前任进程遗留熄灭（刷机/推新版场景）、
 *           被外部写黑（验证脚本/面板电源管理）、也可能被面板自行调整
 *           ——影子说亮、硬件已黑时，触摸进 noteActivity 判「未熄屏」
 *           不触发唤醒，屏幕永远黑着。fbdev 背光/blank 节点可读回，
 *           win32/安卓无读回原语，恒回 Unknown。 */
XPlatformPowerScreenActual XPlatformPowerDriver_queryScreenState(void);

/** @brief 设置系统电源状态。
 *  @param state 目标状态（Active=继续运行，Suspend=整机挂起）。
 *  @return true=已切换；false=平台不支持或切换失败。
 *  @note   挂起成功后本进程被冻结直到系统被唤醒，函数在系统恢复后才返回。 */
bool XPlatformPowerDriver_setSystemState(XPlatformPowerSystemState state);

/** @brief 设置自动熄屏时长（**委托平台/系统自己计时**）。
 *  @param millis 超时毫秒；<=0 表示【从不自动熄屏】。
 *  @return true=已设置；false=平台不支持或设置失败。
 *  @details 安卓语义：写系统 Settings.System.SCREEN_OFF_TIMEOUT，把
 *           「屏幕超时」交给系统，应用不自己计时（安卓无主动关屏原语，
 *           息屏是系统行为）。未获 WRITE_SETTINGS 授权时返回 false。 */
bool XPlatformPowerDriver_setScreenTimeout(int64_t millis);

/** @brief 请求「设置自动熄屏时长」所需的平台授权。
 *  @return true=已发起授权流程；false=本平台无需授权或发起失败。
 *  @details 安卓：写系统「屏幕超时」需 WRITE_SETTINGS 特殊权限，本钩子
 *           引导用户到系统设置页为本应用授权。其他平台恒 false。 */
bool XPlatformPowerDriver_requestScreenTimeoutPermission(void);

/* ==================== 公共契约（应用层唯一可依赖面） ==================== */

/** @brief 本平台支持的电源管理能力位集合（公共转发；0=无任何能力）。 */
unsigned XPlatformPower_capabilities(void);

/** @brief 查询单项能力是否受支持。
 *  @param cap 能力项。
 *  @return true=支持；false=不支持（调用方据此把控件置灰）。
 *  @note   头文件只放声明（仓库头文件结构规范），实现见同名 .c。 */
bool XPlatformPower_supports(XPlatformPowerCapability cap);

/** @brief 设置屏幕电源状态（立即熄屏 / 立即唤醒）。
 *  @param state 目标状态。
 *  @return true=已切换；false=平台不支持或切换失败。
 *  @note   切到 On 时重置空闲计时基线——否则手动唤醒后会因「空闲时长
 *           早已超阈值」而被下一轮巡检立刻再次熄屏。 */
bool XPlatformPower_setScreenState(XPlatformPowerScreenState state);

/** @brief 当前屏幕是否处于熄屏状态（影子状态）。
 *  @note   影子=「本契约最近一次动作的目标态」，非硬件实况；硬件被外部
 *          改写时两者可能短暂失配（巡检对账会拉齐）。 */
bool XPlatformPower_isScreenOff(void);

/** @brief 读屏幕硬件实况并做影子/硬件对账。
 *  @details 与 isScreenOff 的差异：本函数直询平台实况。发现「影子说
 *           亮、硬件已熄」（前任遗留/外部写黑）时立即点亮自愈——这是
 *           「息屏后推送新版本自启，触摸唤不醒」的运行期兜底通道
 *           （fbdev 平台有效；无读回原语的平台恒 false）。
 *  @note   发现「影子说熄、硬件已亮」（外部唤醒：VNC/xset/面板电源
 *          管理把屏点亮，且不经应用输入通道）时**采信硬件**：影子归位
 *          为亮并记一次活动（空闲基线重置）。否则影子会永久卡在「熄」，
 *          自动熄屏分支被 !m_screenOff 门死 ⇒ 应用之后再也无法休眠。 */
bool XPlatformPower_reconcile(void);

/** @brief 平台上报入口：屏幕电源态被平台侧观测到变化（外部唤醒/外部熄灭）。
 *  @param actual 平台观测到的硬件实况（Unknown=平台读不到，不动作）。
 *  @details 这是「平台 → 框架」的唯一上行口（沿用仓库 X*_report* 上报
 *           约定，对标 XWindow_reportWindowStateChanged）。平台驱动观测到
 *           屏幕电源态变化时调用本函数，公共层据此：
 *           1. 把影子态拉齐到硬件真值（外部唤醒 ⇒ 影子=亮 + 记一次活动；
 *              外部熄灭 ⇒ 按既有自愈规则点亮）；
 *           2. 把这次变化**转成框架事件**（XEVENT_TYPE_SCREEN_POWER_
 *              STATE_CHANGED）投进事件队列 ⇒ 应用层转发为屏幕对象的
 *              powerStateChanged 信号 ⇒ 任何继承自 XObject 的类都可
 *              自主选择是否监听。
 *  @note   幂等：影子与硬件一致时什么都不做（不产生事件、不打扰订阅者）。 */
void XPlatformPower_reportScreenStateChanged(XPlatformPowerScreenActual actual);

/** @brief 设置电源策略（一次性提交两个阈值）。
 *  @param policy 策略（NULL 视为「两项都从不」）。
 *  @return true=已生效；false=设置失败。
 *  @details 熄屏阈值的落地机制按平台能力分流：
 *           - 平台支持【委托计时】（安卓把「屏幕超时」写进系统设置、
 *             由系统自己熄屏）→ 交平台设置；**失败即失败**，如实返回
 *             false——安卓无主动关屏原语，本地计时到点也无处可关，回落
 *             等于把失效静默吞掉（界面显示「已设置 5 分钟」而系统从不
 *             熄屏，是最坏的静默失效）。
 *           - 平台不支持 → 落到本地空闲计时，由框架巡检驱动熄屏。
 *           系统挂起阈值统一走本地计时（挂起总是「应用请求平台执行」）。 */
bool XPlatformPower_setPolicy(const XPlatformPowerPolicy* policy);

/** @brief 回读当前电源策略（影子值）。
 *  @param out 出参（不可为 NULL）。
 *  @note 熄屏阈值回读的是**本应用设定值**（影子），不是平台/系统的实时
 *        值——委托计时时系统侧可能被别的途径改过，本应用仍以自己的
 *        设定为准回显。 */
void XPlatformPower_policy(XPlatformPowerPolicy* out);

/** @brief 打点一次用户活动（外部输入）。
 *  @param inputMask 输入来源（XPlatformPowerInputMask 位集；可同时含多位，
 *                   如触摸合成鼠标事件）。
 *  @details 全部输入路径（鼠标/键盘/滚轮/触摸）都应调用本函数。两种
 *           语义要分清：
 *           1. **空闲计时**：任何输入都算「用户仍在活动」，重置计时——
 *              否则鼠标一直动却因「没碰键盘」而熄屏，与用户直觉相悖；
 *           2. **唤醒**：熄屏状态下**只有被允许的输入源**（策略里的
 *              m_wakeInputs）才能唤醒屏幕——屏幕已灭，界面上的按钮不可
 *              达，唤醒路径只能是输入设备本身。
 *  @note   非用户活动（定时器、重绘、远程帧）不应调用本函数。 */
void XPlatformPower_noteActivity(unsigned inputMask);

/** @brief 电源策略周期推进（由应用层的周期定时器调用）。
 *  @details 检查空闲时长：达到熄屏阈值则熄屏，达到挂起阈值则先熄屏再
 *           整机挂起。本函数不做输入打点（打点由输入路径负责）。
 *  @note   幂等：熄屏/挂起失败不改变影子状态，下一轮重试。 */
void XPlatformPower_tick(void);

/** @brief 请求「设置自动熄屏时长」所需的平台授权（安卓引导到系统设置）。
 *  @return true=已发起授权流程；false=无需授权或发起失败。 */
bool XPlatformPower_requestScreenTimeoutPermission(void);

#ifdef __cplusplus
}
#endif
#endif /* XGUI_ON */

#endif /* XPLATFORMPOWER_H */
