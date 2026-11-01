/**
 * @file       XUpgrade.h
 * @brief      跨平台自升级模块公共入口：策略/状态/阶段/错误枚举、事件、配置与引擎控制 API。
 * @details    模块负责主程序与资源包（字库等）的检查、下载、校验、staging、
 *             激活与自检回滚；核心原则是整版本目录落盘、激活=切指针，绝不
 *             原地覆盖运行中的文件（设计文档 §1）。引擎全程经 XTask 后台
 *             执行，UI 只通过 XUpgrade_setListener 观察事件，不反向依赖；
 *             引擎单实例由 update/lock 文件锁保证（含持有者 PID，启动时清
 *             陈旧锁）。本文件只依赖 XinYueC 抽象层，禁止直接调用 Win32、
 *             POSIX 等平台 API。存储后端契约见 XUpgradeStore.h，目录布局
 *             常量亦在该头定义。契约扩展（本阶段）收口策略 API：forced
 *             宽限期与"staged 完成自动 apply"事件、silent 维护窗口、信任
 *             公钥注入（§4.2）与灰度结果查询/事件携带（§11）；forced 遮罩
 *             UI 只消费事件，不新增引擎 API（设计文档 §10/§12）。
 */

#ifndef XUPGRADE_H
#define XUPGRADE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XUpgrade_config.h"

#if XUPGRADE_ON

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "XUpgradeIdentity.h"

/* ========================================================================== */
/*                        串容量常量（均含结尾 NUL，单位字节）                  */
/* ========================================================================== */

/** @brief 组件 id 容量（"app"/"fontres"/"image"；含结尾 NUL）。 */
#define XUPGRADE_COMPONENT_ID_MAX 32
/** @brief 版本串容量（白名单 [0-9A-Za-z._-]，不含路径分隔符；含结尾 NUL）。 */
#define XUPGRADE_VERSION_MAX 32
/** @brief 目标平台标识容量（"win64"/"linux64"/"emb-arm" 等；含结尾 NUL）。 */
#define XUPGRADE_OS_MAX 16
/** @brief 更新渠道名容量（"stable"/"beta" 等；含结尾 NUL）。 */
#define XUPGRADE_CHANNEL_MAX 16
/** @brief 事件说明文本容量（错误文本/回滚说明；含结尾 NUL）。 */
#define XUPGRADE_DETAIL_MAX 256
/** @brief 信任公钥 SEC1 未压缩点长度（0x04||X||Y，字节；XUpgrade_setTrustKey 校验值）。 */
#define XUPGRADE_TRUST_KEY_SEC1_SIZE 65u

/** @brief 宽限期特殊值：跟随 manifest 的 forced_grace_hours 字段（未设置为 0）。 */
#define XUPGRADE_FORCED_GRACE_FROM_MANIFEST 0xFFFFFFFFu

/* ========================================================================== */
/*                                  枚举定义                                   */
/* ========================================================================== */

/**
 * @brief 升级策略。
 * @details 枚举值互斥、不可按位组合。实际生效策略按设计文档 §10 组合判定
 *          （优先级从高到低）：本地版本低于 min_version/policy_forced_below
 *          时强制 forced；用户跳过表命中时不提示（仅此版本）；其余取
 *          manifest 声明；本地总开关关闭时仅手动检查可用。
 */
typedef enum XUpgradePolicy {
    XUpgradePolicy_Forced = 0,     /**< 强制：UI 全屏遮罩+staged 完成即自动进入 apply。遮罩页只消费事件（UpdateAvailable/Progress/ReadyAutoApply）展示进度与结果，无独立引擎 API；宽限期内（XUpgrade_forcedGraceHours）允许点"稍后"，每次启动重新提示，宽限期过后不再提供"稍后"（§10）。嵌入式无 UI 构建等价于 silent+立即重启。 */
    XUpgradePolicy_Optional = 1,   /**< 可跳过：普通弹窗"立即升级/稍后/跳过此版本"；引擎停在 UpdateAvailable 等待 XUpgrade_startUpgrade，staged 完成后推 ReadyNeedRestart。 */
    XUpgradePolicy_Silent = 2      /**< 静默后台：仅通知一次，后台完成下载+staging；应用时机按维护窗口（XUpgrade_setApplyWindow）：窗口内自动 apply，错过顺延，不 7x24 抢重启（§10）。 */
} XUpgradePolicy;

/**
 * @brief 升级事件类型。
 * @details XUpgradeEvent.m_type 取值。StateChanged=0 保持既有"按状态观察"
 *          语义（迁移事件只带 m_state/m_detail），语义事件在不改变引擎状态
 *          的前提下细分 UI 行为（设计文档 §12 事件表）。同一状态迁移可能
 *          连带一条语义事件（先 StateChanged 后语义事件）。
 */
typedef enum XUpgradeEventType {
    XUpgradeEventType_StateChanged = 0,   /**< 状态迁移事件：m_state/m_detail 有意义，其余字段按状态取值。 */
    XUpgradeEventType_UpdateAvailable,    /**< 检查命中可用更新：m_policy 为本次生效策略、m_gray 为灰度判定；optional 策略引擎停在等待 XUpgrade_startUpgrade，forced/silent 引擎自动继续下载（§10/§11）。 */
    XUpgradeEventType_Progress,           /**< 阶段进度：m_phase/m_percent/m_component 有意义，四段进度条（§12）。 */
    XUpgradeEventType_ReadyNeedRestart,   /**< staged 完成，等待重启应用：optional/silent 的"重启以完成升级"提示（§12）。 */
    XUpgradeEventType_ReadyAutoApply,     /**< staged 完成，引擎按策略即将自动应用：forced=staged 即自动进入 apply（宽限期已过或用户点过"稍后"后重新触发同样走本事件）；silent=维护窗口到达自动应用。随后经 Progress(Activate) 与 StateChanged(Applying) 推进。 */
    XUpgradeEventType_RolledBack,         /**< 自检失败已回滚：m_detail 含回滚前后版本说明（§8.3）。 */
    XUpgradeEventType_CheckFailed         /**< 检查失败或无更新：含灰度未命中（Miss/DenyList）的"暂无更新"语义；手动检查时 UI 才提示（§11）。 */
} XUpgradeEventType;

/**
 * @brief 灰度判定结果（§11）。
 * @details 判定顺序 allow 名单 → deny 名单 → 分流哈希（算法契约
 *          XUpgradeManifest_grayHit）；XUpgradeEvent.m_gray 与
 *          XUpgrade_grayDecision 携带最近一次检查的结果。未命中按"暂无更新"
 *          处理：本地记录本次判定，不重复请求，下次检查周期再判。
 */
typedef enum XUpgradeGrayDecision {
    XUpgradeGrayDecision_NotEvaluated = 0, /**< 未判定：灰度裁剪关闭（XUPGRADE_GRAY_ON=0）、目标 manifest 无 gray 字段或尚未检查。 */
    XUpgradeGrayDecision_AllowList,        /**< allow 名单命中，放行。 */
    XUpgradeGrayDecision_Hit,              /**< 名单未命中，分流哈希命中（大端前 4 字节 mod 100 < percent），放行。 */
    XUpgradeGrayDecision_Miss,             /**< 名单未命中，分流哈希未命中：按"暂无更新"处理。 */
    XUpgradeGrayDecision_DenyList          /**< deny 名单命中，拒绝：按"暂无更新"处理。 */
} XUpgradeGrayDecision;

/**
 * @brief 引擎状态机状态。
 * @details 与设计文档 §6 状态图一一对应；状态可持久化到 update/state.json，
 *          任意一步断电/崩溃后重启收敛。
 */
typedef enum XUpgradeState {
    XUpgradeState_Idle = 0,          /**< 空闲：无升级活动。 */
    XUpgradeState_Checking,          /**< 正在检查更新（网络/解析/签名/灰度判定）。 */
    XUpgradeState_Downloading,       /**< 正在下载（断点续传，state.json 记游标）。 */
    XUpgradeState_Verifying,         /**< 正在校验（整包 SHA-256 + P-256 验签）。 */
    XUpgradeState_Staging,           /**< 正在解包 .pak 到 versions/<comp>/<ver>/ 并逐条目校验。 */
    XUpgradeState_Ready,             /**< 待应用：全部组件就绪且 requires 校验通过，staged 标记已写入。 */
    XUpgradeState_Applying,          /**< 正在应用（桌面：退出后由 apply 桩切指针；A/B 构建：双写元数据块后重启，§9.4）。 */
    XUpgradeState_BootConfirming,    /**< 启动自检中（版本+关键文件哈希抽查）。 */
    XUpgradeState_Active,            /**< 新版本确认生效（A/B 场景经 XUpgradeStore_confirmBoot_base 回写 boot 成功标记）。 */
    XUpgradeState_RolledBack,        /**< 自检失败已回滚：指针写回旧版本并上报事件。 */
    XUpgradeState_CheckFailed,       /**< 检查失败（含灰度未命中的"暂无更新"语义）；按周期退避重试。 */
    XUpgradeState_DownloadFailed     /**< 下载/校验失败：保留 partial 供续传。 */
} XUpgradeState;

/**
 * @brief Progress 事件的阶段。
 * @details 枚举值互斥；一个升级流程依次经历四个阶段，UI 据此画四段进度条
 *          （设计文档 §12）。
 */
typedef enum XUpgradePhase {
    XUpgradePhase_Download = 0,      /**< 下载阶段。 */
    XUpgradePhase_Verify,            /**< 校验阶段。 */
    XUpgradePhase_Stage,             /**< 解包/落盘阶段。 */
    XUpgradePhase_Activate           /**< 激活/切换阶段。 */
} XUpgradePhase;

/**
 * @brief 升级错误码。
 * @details 成功恒为 0，失败为负值；调用方不得依赖未列出的数值。存储后端
 *          vtable 与引擎公共 API 的错误语义统一使用本枚举。
 */
typedef enum XUpgradeError {
    XUpgradeError_Ok = 0,                /**< 成功。 */
    XUpgradeError_InvalidArgument = -1,  /**< 参数无效（空指针、版本串含非法字符、公钥长度/首字节不符等）。 */
    XUpgradeError_NotSupported = -2,     /**< 当前构建未编入该能力（FTP/A-B/灰度/delta 被宏裁剪）。 */
    XUpgradeError_Io = -3,               /**< 文件/目录/分区读写失败（磁盘满、权限不足、flash 擦写失败等）。 */
    XUpgradeError_Network = -4,          /**< 网络错误（连接失败、超时、HTTP 状态非 2xx）。 */
    XUpgradeError_ManifestInvalid = -5,  /**< manifest 缺失、JSON 非法、schema 不符或字段越限。 */
    XUpgradeError_VerifyFailed = -6,     /**< 完整性/签名校验失败（SHA-256 不符、ECDSA P-256 验签不通过）。 */
    XUpgradeError_NoUpdate = -7,         /**< 无更新（服务器版本不高于本地，或灰度未命中）。 */
    XUpgradeError_Skipped = -8,          /**< 目标版本在用户跳过表中（仅此版本不提示）。 */
    XUpgradeError_DowngradeRejected = -9,/**< 防降级拦截：目标版本低于历史最高已激活版本。 */
    XUpgradeError_StageIncomplete = -10, /**< staging 未完成：requires 配套校验不满足或版本目录半成品。 */
    XUpgradeError_Busy = -11,            /**< 引擎忙（上一流程未结束）或双实例（update/lock 被占用）。 */
    XUpgradeError_NotReady = -12,        /**< 当前状态不允许该操作（如非 Ready 态请求 apply）。 */
    XUpgradeError_SelfCheckFailed = -13, /**< 启动自检失败且已回滚到旧版本。 */
    XUpgradeError_RollbackLimit = -14    /**< 同一版本回滚计数超限，自动升级已停用，需强报故障（设计文档 §8.3）。 */
} XUpgradeError;

/* ========================================================================== */
/*                                  结构体定义                                 */
/* ========================================================================== */

/**
 * @brief 升级事件，经 XUpgrade_setListener 注册的回调推送给 UI。
 * @details 事件对象由引擎所有，仅在回调执行期间有效，回调返回后即失效；
 *          UI 需要留存时自行拷贝。所有字符串成员为 UTF-8。m_type 为 0
 *          （StateChanged）的事件即既有"按状态观察"事件，老观察者迁移时把
 *          原 m_state 分支判断改为"先 m_type 后 m_state"即可。
 */
typedef struct XUpgradeEvent {
    XUpgradeEventType m_type;                         /**< 事件类型（§12 事件表）；分支判断的第一级。 */
    XUpgradeState m_state;                            /**< 事件发生时的引擎状态。 */
    XUpgradePhase m_phase;                            /**< 进度阶段；仅 Progress 事件有意义。 */
    int m_percent;                                    /**< 当前阶段百分比 [0,100]；非进度事件为 -1。 */
    XUpgradePolicy m_policy;                          /**< 本次升级的生效策略；仅 UpdateAvailable 事件有意义，其余事件为 XUpgradePolicy_Optional。 */
    XUpgradeGrayDecision m_gray;                      /**< 灰度判定结果；仅 UpdateAvailable 事件有意义，其余事件为 XUpgradeGrayDecision_NotEvaluated。 */
    char m_component[XUPGRADE_COMPONENT_ID_MAX];      /**< 关联组件 id 的 UTF-8 串；无关联组件时为空串。 */
    char m_version[XUPGRADE_VERSION_MAX];             /**< 关联版本串的 UTF-8；无关联版本时为空串。 */
    char m_detail[XUPGRADE_DETAIL_MAX];               /**< 错误文本/回滚说明的 UTF-8；无说明时为空串。 */
} XUpgradeEvent;

/**
 * @brief 事件观察者回调。
 * @param userData 注册时传入的用户上下文；库原样回传，不取得所有权。
 * @param event 事件快照；仅在回调执行期间有效，不得释放，需要留存时自行拷贝。
 * @return 无。
 * @note 回调在引擎后台线程（XTask 工作线程）触发；UI 侧自行切换到界面线程，
 *       回调内不得调用 XUpgrade_deinit 等引擎控制 API（死锁风险）。
 */
typedef void (*XUpgradeListener)(void* userData, const XUpgradeEvent* event);

/**
 * @brief silent 策略维护窗口（本地配置项 applyWindow，§10）。
 * @details 窗口判定使用墙钟（XDateTime_currentDateTime，真纪元场景，风格
 *          指南 2026-09-27 裁定）；窗口内引擎自动应用 staged 升级（嵌入式
 *          即 reboot），错过窗口顺延到下一个窗口，不 7x24 抢重启。start
 *          晚于 end 表示跨午夜窗口（如 22:30-05:00）。桌面构建同样适用：
 *          窗口外 silent 保持 ReadyNeedRestart（退出时应用）。
 */
typedef struct XUpgradeApplyWindow {
    bool m_enabled;        /**< 是否启用维护窗口；false 时其余字段忽略（缺省：不启用，silent 无窗口限制）。 */
    uint8_t m_startHour;   /**< 窗口起始时 [0,23]。 */
    uint8_t m_startMinute; /**< 窗口起始分 [0,59]。 */
    uint8_t m_endHour;     /**< 窗口结束时 [0,23]；与起始组合支持跨午夜。 */
    uint8_t m_endMinute;   /**< 窗口结束分 [0,59]。 */
} XUpgradeApplyWindow;

/**
 * @brief 引擎启动配置。
 * @details 全部指针字段为调用方借用语义：XUpgrade_init 内部复制所需字符串，
 *          init 返回后调用方可立即释放 config 及其字符串。数值字段为 0 时
 *          的缺省行为见各字段注释。
 */
typedef struct XUpgradeConfig {
    const char* m_updateUrl;          /**< 更新服务器 URL 模板，含 {channel} 占位（如 "http://srv/{channel}/manifest.json"）；UTF-8 借用，不能为 NULL。 */
    const char* m_channel;            /**< 更新渠道名（"stable"/"beta"…），替换 URL 模板占位；UTF-8 借用，NULL 按空串处理。 */
    const char* m_targetOs;           /**< 本机目标平台标识（win64/linux64/emb-arm…），用于匹配 manifest 组件 os 字段；UTF-8 借用，不能为 NULL。 */
    const char* m_installRoot;        /**< 安装根目录；UTF-8 借用。NULL 时由 XSystem_executableFilePath 向上推导（嵌入式必须显式给定，设计文档 §2）。 */
    uint32_t m_checkIntervalHours;    /**< 自动检查周期（小时）；0 表示不按周期自动检查，仅手动。 */
    bool m_autoCheck;                 /**< 是否允许自动检查；false 时仅手动检查可用（设计文档 §10 本地总开关）。 */
    XUpgradeApplyWindow m_applyWindow; /**< silent 维护窗口初值（本地配置项 §10）；m_enabled=false 表示不限制。运行期可经 XUpgrade_setApplyWindow 修改（设置页）。 */
} XUpgradeConfig;

/* ========================================================================== */
/*                                  公共 API                                   */
/* ========================================================================== */

/**
 * @brief 初始化并启动升级引擎。
 * @param config 启动配置；不能为 NULL，m_updateUrl/m_targetOs 不能为 NULL，
 *               其余字段按缺省规则处理。仅在本调用期间借用，内部会复制。
 * @return 成功返回 true；参数非法、安装根不可用、引擎锁被其他进程占用或
 *         资源分配失败返回 false。
 * @note 幂等：已初始化时重复调用直接返回 true 且不重复启动。成功后引擎经
 *       XTask 后台运行；m_autoCheck 为 true 时按 m_checkIntervalHours 周期
 *       自动检查（周期为 0 时视同 false）。信任公钥、维护窗口与宽限期覆盖
 *       建议在本调用之前设置。
 */
bool XUpgrade_init(const XUpgradeConfig* config);

/**
 * @brief 停止引擎并释放其资源。
 * @return 无。未初始化时为空操作；进行中的后台任务被取消，partial 保留供
 *         下次续传。
 * @note 调用后所有 XUpgrade_* 控制接口（除 XUpgrade_selfCheck/
 *       XUpgrade_confirmBoot）返回失败；可再次 XUpgrade_init 重新启动。
 */
void XUpgrade_deinit(void);

/**
 * @brief 手动发起一次更新检查。
 * @return 请求已受理返回 true；引擎未初始化、正在检查或下载中（忙）返回
 *         false。
 * @note 检查在后台执行，结果经 listener 事件返回：命中更新且灰度放行推
 *       XUpgradeEventType_UpdateAvailable（携带 m_policy 与 m_gray；
 *       optional 策略停在等待 XUpgrade_startUpgrade，forced/silent 自动
 *       继续），失败、无更新或灰度未命中推 XUpgradeEventType_CheckFailed
 *       （手动检查时 UI 才提示）。
 */
bool XUpgrade_checkNow(void);

/**
 * @brief 用户确认后启动"下载→校验→staging"流程。
 * @return 流程已启动返回 true；引擎未初始化、无待升级内容、已在流程中或
 *         目标版本被跳过表/防降级表拦截返回 false。
 * @note 到达 Ready 后按策略决定 apply 时机：optional/silent 推
 *       XUpgradeEventType_ReadyNeedRestart 等待用户重启或
 *       XUpgrade_applyNow；forced 推 XUpgradeEventType_ReadyAutoApply 后
 *       自动进入 apply，silent 在维护窗口到达时同样推 ReadyAutoApply。
 */
bool XUpgrade_startUpgrade(void);

/**
 * @brief 将指定组件的指定版本记入"跳过此版本"表并持久化。
 * @param componentId 组件 id（如 "app"）；UTF-8 借用，NULL 或空串时忽略。
 * @param version 要跳过的版本串；UTF-8 借用，NULL 或空串时忽略。
 * @return 无。
 * @note 跳过仅对该版本生效，下一版本自动重置（设计文档 §10）；forced 策略
 *       不受跳过表影响。记录即时写 state.json（原子写+CRC）。
 */
void XUpgrade_skipVersion(const char* componentId, const char* version);

/**
 * @brief 设置 silent 策略维护窗口（本地配置项 applyWindow，§10）。
 * @param window 窗口配置；借用，不能为 NULL 的字段见结构体注释。传 NULL
 *               视同关闭窗口（等价 m_enabled=false）。
 * @return 无。
 * @note 建议在 XUpgrade_init 之前调用（init 先装 m_applyWindow，本入口随后
 *       覆盖，供设置页运行期修改）；窗口到达时 silent 自动应用 staged 升级，
 *       错过顺延。引擎不持久化该配置：产品设置层负责保存并在每次启动重新
 *       设置（引擎只保存 state.json 升级状态，不保存用户偏好）。
 */
void XUpgrade_setApplyWindow(const XUpgradeApplyWindow* window);

/**
 * @brief 查询当前生效的 silent 维护窗口配置（设置页回显）。
 * @return 当前窗口配置的值拷贝；未设置时 m_enabled=false。
 */
XUpgradeApplyWindow XUpgrade_applyWindow(void);

/**
 * @brief 覆盖 forced 策略宽限期（小时，§10）。
 * @param hours 宽限期小时数；XUPGRADE_FORCED_GRACE_FROM_MANIFEST 表示跟随
 *              manifest 的 forced_grace_hours 字段（缺省行为，未设置为 0），
 *              0 表示无宽限期（staged 完成即自动 apply）。
 * @return 无。
 * @note 宽限期内 forced 遮罩允许用户点"稍后"，每次启动重新提示；宽限期过
 *       后不再提供"稍后"。建议在 XUpgrade_init 之前调用；运行期修改自下一
 *       次 apply 时机判定生效。引擎不持久化该覆盖值（manifest 值随签名
 *       manifest 下发，本地覆盖由产品设置层保存）。
 */
void XUpgrade_setForcedGraceHours(uint32_t hours);

/**
 * @brief 查询当前生效的 forced 宽限期（小时）。
 * @return 本地覆盖值（XUpgrade_setForcedGraceHours 设置）；未覆盖时返回
 *         manifest 的 forced_grace_hours，无 manifest 上下文返回 0。
 * @note 遮罩页展示剩余宽限与"稍后"按钮的可用性以本值与升级起始时间为准。
 */
uint32_t XUpgrade_forcedGraceHours(void);

/**
 * @brief 注入 manifest 验签信任公钥（测试/产品注入点，设计文档 §4.2）。
 * @param publicKey 公钥字节串：SEC1 未压缩点 0x04||X||Y 共 65 字节，大端；
 *                  借用，仅本调用期间读取（内部复制）。传 NULL 清除注入、
 *                  恢复编译期内嵌缺省钥（XUpgrade_trust_pubkey.h）。
 * @param len 字节数；publicKey 非 NULL 时必须为 XUPGRADE_TRUST_KEY_SEC1_SIZE。
 * @return 注入成功返回 true；publicKey 非 NULL 但 len 不符或首字节非 0x04
 *         返回 false（原注入保持不变）。
 * @note 与 XUpgradeVerify_verifyP256 的 publicKey 参数同编码。测试链路：
 *       XUpgrade_test_keys.h 的私钥在服务端/测试工具侧签 canonical manifest，
 *       本入口注入对应公钥；生产链路：编译期替换 XUpgrade_trust_pubkey.h
 *       或产品初始化时注入产品钥（二选一，缺省内嵌）。建议在 XUpgrade_init
 *       之前调用；运行期替换自下一次检查生效，不影响进行中流程。
 */
bool XUpgrade_setTrustKey(const uint8_t* publicKey, uint32_t len);

/**
 * @brief 查询最近一次检查的灰度判定结果（§11）。
 * @return 最近一次检查对目标 manifest 的灰度判定；从未检查、灰度裁剪关闭
 *         （XUPGRADE_GRAY_ON=0）或目标 manifest 无 gray 字段时返回
 *         XUpgradeGrayDecision_NotEvaluated。
 * @note 与最近一次 XUpgradeEventType_UpdateAvailable 事件的 m_gray 一致；
 *       灰度未命中（Miss/DenyList）按"暂无更新"处理并记录本次判定，下次
 *       检查周期再判。多阶段放量：服务端改 percent 重新签名上传即可，端上
 *       下次检查自然生效（§11）。
 */
XUpgradeGrayDecision XUpgrade_grayDecision(void);

/**
 * @brief 绕过时机窗口立即请求应用已 staged 的升级。
 * @return 请求已受理返回 true；引擎未初始化、当前非 Ready 态或激活失败
 *         返回 false。
 * @note 返回 true 后调用方应尽快保存现场并以约定退出码 74 退出进程，由
 *       apply 桩/重启流程完成指针切换（设计文档 §8.1，采用自重启参数
 *       --xupgrade-apply 形态）。
 */
bool XUpgrade_applyNow(void);

/**
 * @brief 启动早期自检（正式程序 GUI 之前调用，设计文档 §8.3）。
 * @return 自检通过或已成功回滚返回 true，进程可正常继续启动；自检失败且
 *         回滚也失败返回 false，调用方应强报故障。
 * @note 动作顺序：解析 pointers/<comp> → 版本目录与 manifest.version 吻合
 *       → 关键文件哈希抽查（.verify 清单）→ 通过则进入 Active（A/B 场景
 *       回写 boot 标记）；失败则 rollback 写回上一版本并上报 RolledBack
 *       事件。同一版本连续回滚超过 2 次停用自动升级并强报故障。可在
 *       XUpgrade_init 之前调用（仅依赖安装根与 state.json）；已 init（引擎
 *       运行中）时调用返回 false——不透传运行中引擎（自检与后台流水并发
 *       改写状态镜像属数据竞争），需重新自检请先 XUpgrade_deinit。
 */
bool XUpgrade_selfCheck(void);

/**
 * @brief 自检通过后确认新版本生效（A/B 场景回写 boot 成功标记，设计文档 §9.4）。
 * @return 确认动作完成返回 true；引擎未初始化且无自检上下文时返回 false。
 * @note A/B 构建下确认动作=activeSlot=requestedSlot、bootAttempts=0、
 *       sequence 递增双写元数据块（规则详见 XUpgradeStore.h 的
 *       XUpgradeStore_confirmBoot_base）；确认前由 bootloader 的启动计数
 *       回退机制兜底（§9.3）。目录后端构建下无 boot 标记可回写，自检通过
 *       后调用本函数幂等返回 true。
 */
bool XUpgrade_confirmBoot(void);

/**
 * @brief 注册或清除升级事件观察者。
 * @param listener 事件回调；可为 NULL，传 NULL 时清除注册。
 * @param userData 原样传给 listener 的用户上下文；可为 NULL。listener 非
 *                 NULL 时库保存该借用指针直至覆盖或清除，但不释放所指资源。
 * @return 无。
 * @note 可在 XUpgrade_init 之前注册；运行期替换立即生效，切换瞬间的事件
 *       可能丢失。XUPGRADE_UI_HINTS_ON=0 时 UI 提示语义事件（UpdateAvailable/
 *       ReadyNeedRestart/ReadyAutoApply/RolledBack/CheckFailed）不投递，
 *       StateChanged 与 Progress 照常。
 */
void XUpgrade_setListener(XUpgradeListener listener, void* userData);

/**
 * @brief 多版本目录缓存中的远程版本条目数（最近一次检查更新填充）。
 * @return 有效条目数；未检查过、目录未加载或引擎未初始化返回 0。
 * @note 服务器以 schema 2 目录（index.json）同时发布多个版本时，本接口
 *       返回目录内全部 release 条数；单版本 manifest（schema v1）返回 0。
 */
uint32_t XUpgrade_remoteVersionCount(void);

/**
 * @brief 读取目录缓存中第 index 条远程版本串。
 * @param index 条目下标，[0, XUpgrade_remoteVersionCount)。
 * @param versionOut 输出缓冲区；调用方提供存储空间。
 * @param cap 缓冲区容量（字节）。
 * @return 成功写入以 NUL 结尾的版本串返回 true；下标越界或参数无效返回
 *         false。
 * @note 条目顺序=服务端目录声明顺序（建议新版本在前）。
 */
bool XUpgrade_remoteVersionAt(uint32_t index, char* versionOut, size_t cap);

/**
 * @brief 显式安装目录中的指定版本（支持降级；防降级 rollback_to 窗口仍生效）。
 * @param version 目录中列出的目标版本串；UTF-8 借用，不能为 NULL 或空串。
 * @return 已受理返回 true（异步走检查→下载→校验→staging，完成后 Ready 交
 *         「立即应用」）；引擎忙、版本不在目录中或未初始化返回 false。
 * @note 与 XUpgrade_startUpgrade 的差异：startUpgrade 只升级"高于本地"的
 *       自动判定目标；本接口显式指定任意目录版本——降级安装（目标低于本地）
 *       为用户显式选择，仍须落在该版本 manifest 签名的 rollback_to 窗口内，
 *       且防降级基线（c0max）只升不降。
 */
bool XUpgrade_installVersion(const char* version);

#endif /* XUPGRADE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XUPGRADE_H */
