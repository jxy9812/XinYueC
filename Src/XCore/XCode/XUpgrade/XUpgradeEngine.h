/**
 * @file       XUpgradeEngine.h
 * @brief      升级引擎内部接口：状态机载体、update/state.json 持久化结构与引擎操作。
 * @details    仅供 XUpgrade 模块实现与 apply 桩使用，不对外公开（对外入口是
 *             XUpgrade.h 的 XUpgrade_* 模块 API，本类是其内部单例载体）。
 *             XUpgradeEngine 为 XObject 派生具体类：结构体与
 *             static XVtable* XUpgradeEngine_class_init(void) 定义在
 *             XUpgradeEngine.c（模式同 XMySqlDriver.c），不新增虚槽位，
 *             枚举用 XCLASS_DEFINE_EXTEND_END(XUpgradeEngine, XObject)。
 *             状态机复用 Src/XCode/XStateMachine，全程经 XTask 后台执行；
 *             update/state.json 读写一律走 XSaveFile 原子替换（崩溃不变式 2）。
 */

#ifndef XUPGRADEENGINE_H
#define XUPGRADEENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XUpgrade_config.h"

#if XUPGRADE_ON

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "XMemory.h"
#include "XUpgrade.h"
#include "XUpgradeManifest.h"
#include "XUpgradePackage.h"

/* ========================================================================== */
/*                        state.json 结构常量（均含结尾 NUL）                  */
/* ========================================================================== */

/** @brief state.json schema 版本；解析只接受本值。 */
#define XUPGRADE_STATE_SCHEMA 1u
/** @brief 组件状态表容量；覆盖 app/fontres/image + 预留。 */
#define XUPGRADE_STATE_COMPONENT_MAX 4u
/** @brief 跳过版本表容量；下一版本重置由引擎维护。 */
#define XUPGRADE_STATE_SKIP_MAX 8u

/* ========================================================================== */
/*                        update/state.json 持久化结构                         */
/* ========================================================================== */

/**
 * @brief 单组件版本线状态（state.json 的 components[] 元素）。
 * @details 字段即设计文档 §2/§4.2/§8.3/§10 定死的持久化口径；组件在表中的
 *           稳定性以 m_id 匹配，首次出现该组件时由引擎追加。
 */
typedef struct XUpgradeStateComponent {
    char m_id[XUPGRADE_COMPONENT_ID_MAX];      /**< 组件 id（"app"/"fontres"/"image"），本条记录的主键。 */
    char m_activeVersion[XUPGRADE_VERSION_MAX];/**< 当前激活版本线：pointers/<comp> 指向的版本；空串=从未激活。 */
    char m_stagedVersion[XUPGRADE_VERSION_MAX];/**< staged 未激活版本：整包校验+解包+requires 全过后才写入；激活成功后清空。 */
    char m_maxVersion[XUPGRADE_VERSION_MAX];   /**< 历史最高已激活版本（防降级表）：目标版本 < 该值且 > rollback_to 时拒绝（§4.2）；状态文件被清视为全新设备，以当前运行版本重建基线。 */
    uint32_t m_rollbackCount;                  /**< 回滚计数：同一 m_stagedVersion 连续自检失败回滚次数；staged 版本变更时清零；超过 2 次停用自动升级并强报故障（§8.3）。 */
    bool m_staged;                             /**< staged 标记（不变式 1）：true=该组件存在已校验就绪的 stagedVersion，未见标记=无事发生。 */
} XUpgradeStateComponent;

/**
 * @brief 跳过版本表条目（state.json 的 skips[] 元素，§10）。
 * @details optional 策略下用户选择"跳过此版本"时写入；命中即不提示，仅对
 *           记录的版本生效，forced 策略不受影响（M2）。
 */
typedef struct XUpgradeStateSkip {
    char m_componentId[XUPGRADE_COMPONENT_ID_MAX]; /**< 被跳过的组件 id。 */
    char m_version[XUPGRADE_VERSION_MAX];          /**< 被跳过的版本串。 */
} XUpgradeStateSkip;

/**
 * @brief 下载断点续传游标（state.json 的 download 字段，§6）。
 * @details 下载中周期性（每确认一个分块）原子更新；下载完成/失败作废时由
 *           引擎清空。分块哈希游标用于续传前复核已落盘前缀未被篡改。
 */
typedef struct XUpgradeStateDownload {
    bool m_active;                                 /**< 是否存在进行中的下载；false 时其余字段无意义。 */
    char m_componentId[XUPGRADE_COMPONENT_ID_MAX]; /**< 正在下载的组件 id。 */
    char m_fileName[XUPGRADE_FILENAME_MAX];        /**< partial 内的临时文件名（如 "app-1.2.3-win64.pak.part"）。 */
    uint64_t m_offset;                             /**< 已安全落盘字节数（Range 续传起点）。 */
    uint8_t m_chunkSha256[XUPGRADE_SHA256_BIN_SIZE];/**< 分块哈希游标：最近一个已确认分块的 SHA-256；续传时先复核该值再追加下载。 */
} XUpgradeStateDownload;

/**
 * @brief update/state.json 的完整内存表示。
 * @details 定长结构、嵌入式免堆；引擎单实例持有，apply 桩只读。序列化为
 *           JSON 后经 XSaveFile 原子写，m_crc32 覆盖除自身外全部字段的
 *           序列化字节（CRC32，"原子写+CRC"，§2）；CRC 不符按整文件损坏
 *           处理（视同全新设备，§4.2.4）。
 */
typedef struct XUpgradeStateFile {
    uint32_t m_schema;                                      /**< 结构版本，恒为 XUPGRADE_STATE_SCHEMA。 */
    XUpgradeState m_state;                                  /**< 引擎状态机当前状态（§6 状态图）。 */
    uint32_t m_componentCount;                              /**< 有效组件条数，[0, XUPGRADE_STATE_COMPONENT_MAX]。 */
    XUpgradeStateComponent m_components[XUPGRADE_STATE_COMPONENT_MAX]; /**< 各组件版本线：激活/staged/maxVersion/回滚计数（防降级表）。 */
    uint32_t m_skipCount;                                   /**< 有效跳过条数，[0, XUPGRADE_STATE_SKIP_MAX]。 */
    XUpgradeStateSkip m_skips[XUPGRADE_STATE_SKIP_MAX];     /**< 跳过版本表。 */
    XUpgradeStateDownload m_download;                       /**< 下载断点续传游标。 */
    uint32_t m_crc32;                                       /**< 除本字段外的 CRC32 校验值，位于结构尾部。 */
} XUpgradeStateFile;

/* ========================================================================== */
/*                        state.json 读写入口（apply 桩共用）                  */
/* ========================================================================== */

/**
 * @brief 将状态结构整体清零并写入结构缺省值。
 * @param state 目标结构；不能为 NULL。
 * @return 无。清零后 m_schema=XUPGRADE_STATE_SCHEMA、m_state=Idle、各计数
 *         为 0、m_crc32 为 0。
 */
void XUpgradeStateFile_clear(XUpgradeStateFile* state);

/**
 * @brief 从 update 目录加载 state.json。
 * @param out 输出结构体；调用方提供存储空间。无论成败，函数先
 *            XUpgradeStateFile_clear 再填充。
 * @param updateDirUtf8 引擎工作区完整路径（&lt;安装根&gt;/update）；UTF-8
 *                      借用，不能为 NULL。
 * @return 加载成功返回 true；文件不存在、JSON 非法、schema 不符或 CRC 不符
 *         返回 false（此时 out 为清零态=全新设备口径，调用方以当前运行
 *         版本重建基线，§4.2.4）。
 * @note 本入口供 apply 桩与引擎共用；读到的 Ready 态是桩续做 apply 的依据
 *       （§8.3.1）。
 */
bool XUpgradeStateFile_load(XUpgradeStateFile* out, const char* updateDirUtf8);

/**
 * @brief 原子保存状态结构到 update/state.json。
 * @param state 待保存状态；不能为 NULL，保存前内部重算 m_crc32。
 * @param updateDirUtf8 引擎工作区完整路径（&lt;安装根&gt;/update）；UTF-8
 *                      借用，不能为 NULL，目录不存在时自动创建。
 * @return 保存成功返回 true；序列化失败或写盘失败返回 false（旧文件保持
 *         不变，原子替换语义）。
 * @note 必须经 XSaveFile（临时文件+原子替换）落盘（不变式 2）；任意时刻
 *       断电要么旧状态要么新状态，不存在半文件。
 */
bool XUpgradeStateFile_save(const XUpgradeStateFile* state, const char* updateDirUtf8);

/* ========================================================================== */
/*                        引擎对象（模块内部单例载体）                          */
/* ========================================================================== */

/**
 * @brief 升级引擎对象类型（实现私有，不透明句柄）。
 * @details 结构体定义于 XUpgradeEngine.c（模式同 XMySqlDriver.c:54-58），
 *           首成员为 XObject 基类；class_init 以 static 形式同文件定义
 *           （XMySqlDriver.c:69 先例），不新增虚槽位，枚举在 .c 内以
 *           XCLASS_DEFINE_EXTEND_END(XUpgradeEngine, XObject) 收口。调用方
 *           只经本头文件的函数操作引擎，不得触碰内部成员。
 */
typedef struct XUpgradeEngine XUpgradeEngine;

/**
 * @brief 创建升级引擎对象。
 * @param memory 内存池类型；决定对象分配与释放所用的分配器。
 * @return 新引擎对象，调用者必须使用 XClassDelete 释放；分配失败返回 NULL。
 * @note 进程内单例：XUpgrade_init 负责创建并持有，重复创建用于测试场景，
 *       多实例并发运行受 update/lock 文件锁约束。
 */
XUpgradeEngine* XUpgradeEngine_create_ex(XMemoryType memory);

/**
 * @brief 启动引擎（加载 state.json、接管锁、按配置启动自动检查周期）。
 * @param engine 引擎对象；不能为 NULL。
 * @param config 启动配置；仅本调用期间借用，引擎内部复制（语义同
 *               XUpgrade_init）。
 * @return 成功返回 true；配置非法、update/lock 被其他进程占用或内部状态
 *         修复失败返回 false。
 * @note 幂等：已启动时重复调用返回 true；断电恢复收敛（§16 矩阵 1-7）在
 *       本调用内完成。
 */
bool XUpgradeEngine_start(XUpgradeEngine* engine, const XUpgradeConfig* config);

/**
 * @brief 停止引擎并释放后台任务。
 * @param engine 引擎对象；可为 NULL，为 NULL 时空操作。
 * @return 无。partial 保留供续传；stop 后可重新 start。
 */
void XUpgradeEngine_stop(XUpgradeEngine* engine);

/**
 * @brief 发起一次更新检查（手动或周期到期，后台执行）。
 * @param engine 引擎对象；不能为 NULL。
 * @return 请求受理返回 true；忙（正在检查/下载/staging）返回 false。
 */
bool XUpgradeEngine_checkNow(XUpgradeEngine* engine);

/**
 * @brief 启动"下载→校验→staging"流程（用户确认后调用）。
 * @param engine 引擎对象；不能为 NULL。
 * @return 流程已启动返回 true；无可升级内容、已在流程中或被跳过表拦截返回
 *         false。
 */
bool XUpgradeEngine_startUpgrade(XUpgradeEngine* engine);

/**
 * @brief 多版本目录缓存中的远程版本条目数（最近一次 schema 2 检查填充）。
 * @param engine 引擎对象；不能为 NULL。
 * @return 有效条目数；未检查过或目录未加载返回 0。
 */
uint32_t XUpgradeEngine_remoteVersionCount(XUpgradeEngine* engine);

/**
 * @brief 读取目录缓存中第 index 条远程版本串。
 * @param engine 引擎对象；不能为 NULL。
 * @param index 条目下标，[0, XUpgradeEngine_remoteVersionCount)。
 * @param versionOut 输出缓冲区；调用方提供存储空间。
 * @param cap 缓冲区容量（字节）。
 * @return 成功返回 true；下标越界或参数无效返回 false。
 */
bool XUpgradeEngine_remoteVersionAt(XUpgradeEngine* engine, uint32_t index,
                                    char* versionOut, size_t cap);

/**
 * @brief 显式安装目录中的指定版本（含降级；防降级 rollback_to 窗口仍生效）。
 * @param engine 引擎对象；不能为 NULL。
 * @param version 目录中列出的目标版本串；UTF-8 借用，不能为 NULL 或空串。
 * @return 已受理返回 true（异步走下载→校验→staging，完成后 Ready）；引擎忙
 *         或版本不在目录中返回 false。
 */
bool XUpgradeEngine_installVersion(XUpgradeEngine* engine, const char* version);

/**
 * @brief 记录"跳过此版本"并持久化。
 * @param engine 引擎对象；不能为 NULL。
 * @param componentId 组件 id；UTF-8 借用，NULL 或空串忽略。
 * @param version 版本串；UTF-8 借用，NULL 或空串忽略。
 * @return 无。
 */
void XUpgradeEngine_skipVersion(XUpgradeEngine* engine, const char* componentId, const char* version);

/**
 * @brief 请求立即应用已 staged 的升级（绕过时机窗口）。
 * @param engine 引擎对象；不能为 NULL。
 * @return 请求受理且切换完成返回 true；非 Ready 态或 activate 失败返回 false
 *         （失败时已回退 Ready 落盘，可重试）。
 * @note 桌面目录后端（XUPGRADE_AB_ON=0）：受理即置 Applying 落盘（"切换待续
 *       做"语义，进程即死后重启可续做），并在引擎内直接完成 staged 指针切换，
 *       全部成功置 BootConfirming 落盘（下次启动自检确认/回滚）。A/B 构建
 *       （XUPGRADE_AB_ON=1）维持既有行为：受理返回 true 后调用方按 §8.1 以
 *       退出码 74 退出，由 --xupgrade-apply 分支完成槽切换。
 */
bool XUpgradeEngine_applyNow(XUpgradeEngine* engine);

/**
 * @brief 启动自检（GUI 之前调用，§8.3；含失败回滚与计数保护）。
 * @param engine 引擎对象；不能为 NULL。
 * @return 自检通过或已成功回滚返回 true；回滚也失败返回 false（强报故障）。
 * @note 可在 XUpgradeEngine_start 之前调用：仅依赖安装根、state.json 与
 *       指针文件。start 之后调用同样合法（全部状态访问经引擎 m_mutex，与
 *       工作线程流水并发安全）；模块级封装 XUpgrade_selfCheck 在已 init 时
 *       拒绝透传运行中引擎（见 XUpgrade.h）。
 */
bool XUpgradeEngine_selfCheck(XUpgradeEngine* engine);

/**
 * @brief 自检通过后确认新版本生效（A/B 回写 boot 标记；目录后端幂等）。
 * @param engine 引擎对象；不能为 NULL。
 * @return 确认完成返回 true；XUPGRADE_AB_ON=0 时幂等返回 true。
 */
bool XUpgradeEngine_confirmBoot(XUpgradeEngine* engine);

/**
 * @brief 注册或清除事件观察者（透传 XUpgradeListener 语义）。
 * @param engine 引擎对象；不能为 NULL。
 * @param listener 事件回调；可为 NULL，传 NULL 时清除注册。
 * @param userData 原样传给 listener 的上下文；可为 NULL，库不取得所有权。
 * @return 无。
 */
void XUpgradeEngine_setListener(XUpgradeEngine* engine, XUpgradeListener listener, void* userData);

/**
 * @brief 查询引擎当前状态。
 * @param engine 引擎对象；可为 NULL。
 * @return 当前状态；engine 为 NULL 返回 XUpgradeState_Idle。
 * @note 关于页展示与测试断言使用；状态迁移细节以 listener 事件为准。
 */
XUpgradeState XUpgradeEngine_state(const XUpgradeEngine* engine);

/* ========================================================================== */
/*          策略/灰度/信任锚扩展入口（§10/§11/§4.2，模块入口经此透传）          */
/* ========================================================================== */

/**
 * @brief 设置 silent 策略维护窗口（本地配置项，§10）。
 * @param engine 引擎对象；不能为 NULL。
 * @param window 窗口配置；借用，传 NULL 视同关闭窗口（m_enabled=false）。
 * @return 无。
 * @note 值拷贝借用语义；引擎不持久化（产品设置层每次启动重新设置）。与
 *       XUpgradeConfig.m_applyWindow（init 装载）同字段，本入口供运行期覆盖。
 */
void XUpgradeEngine_setApplyWindow(XUpgradeEngine* engine, const XUpgradeApplyWindow* window);

/**
 * @brief 查询当前生效的 silent 维护窗口配置（设置页回显）。
 * @param engine 引擎对象；可为 NULL。
 * @return 当前窗口配置值拷贝；未设置或 engine 为 NULL 时 m_enabled=false。
 */
XUpgradeApplyWindow XUpgradeEngine_applyWindow(const XUpgradeEngine* engine);

/**
 * @brief 覆盖 forced 策略宽限期（小时，§10）。
 * @param engine 引擎对象；不能为 NULL。
 * @param hours 传 XUPGRADE_FORCED_GRACE_FROM_MANIFEST 清除覆盖（跟随
 *              manifest 的 forced_grace_hours）；其余值即本地宽限小时数。
 * @return 无。引擎不持久化该覆盖值。
 */
void XUpgradeEngine_setForcedGraceHours(XUpgradeEngine* engine, uint32_t hours);

/**
 * @brief 查询当前生效的 forced 宽限期（小时）。
 * @param engine 引擎对象；可为 NULL。
 * @return 本地覆盖值；未覆盖时返回 manifest 的 forced_grace_hours，无
 *         manifest 上下文或 engine 为 NULL 返回 0。
 */
uint32_t XUpgradeEngine_forcedGraceHours(const XUpgradeEngine* engine);

/**
 * @brief 注入 manifest 验签信任公钥（§4.2，测试/产品注入点）。
 * @param engine 引擎对象；可为 NULL（信任锚为验签模块级状态，本入口仅转发）。
 * @param publicKey SEC1 未压缩点 0x04||X||Y 共 65 字节；借用，传 NULL 复位
 *                  为编译期内嵌缺省钥（XUpgrade_trust_pubkey.h）。
 * @param len publicKey 非 NULL 时必须为 XUPGRADE_TRUST_KEY_SEC1_SIZE。
 * @return 注入成功返回 true；len 不符或首字节非 0x04 返回 false（原注入保持）。
 * @note 实现位为 XUpgradeVerify_setTrustKey（验签模块级单一状态，防双份漂移）。
 */
bool XUpgradeEngine_setTrustKey(XUpgradeEngine* engine, const uint8_t* publicKey, uint32_t len);

/**
 * @brief 查询最近一次检查的灰度判定结果（§11）。
 * @param engine 引擎对象；可为 NULL。
 * @return 最近一次检查对目标 manifest 的判定；从未检查或目标无 gray 字段时
 *         返回 XUpgradeGrayDecision_NotEvaluated。
 * @note 与最近一次 UpdateAvailable 事件的 m_gray 一致；灰度未命中按"暂无
 *       更新"处理并记录本次判定。
 */
XUpgradeGrayDecision XUpgradeEngine_grayDecision(const XUpgradeEngine* engine);

#endif /* XUPGRADE_ON */

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XUpgradeEngine_create
#define XUpgradeEngine_create() XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XUPGRADEENGINE_H */
