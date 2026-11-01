/**
 * @file       XUpgradeManifest.h
 * @brief      升级 manifest 结构定义与解析契约（schema v1，设计文档 §4）。
 * @details    服务端只需静态文件服务：HTTP 目录 + manifest.json + 若干 .pak。
 *             设备只取 os 字段匹配自身（XUpgradeConfig.m_targetOs）或为
 *             "any" 的组件条目。本头文件定义 manifest 的内存表示（定长容量
 *             结构，嵌入式免堆，解析超限即整单拒绝）、JSON 解析入口和版本
 *             比较。签名验证链（§4.2）=XUpgradeManifest_canonicalBytes
 *             （canonical 重序列化，键名字典序递归排序、无空白、UTF-8、
 *             数字十进制无前导零）+ XUpgradeVerify_verifyP256（ECDSA
 *             P-256/SHA-256）；灰度判定契约=XUpgradeManifest_grayHit（§11，
 *             算法入契约保证跨版本判定一致）。
 */

#ifndef XUPGRADEMANIFEST_H
#define XUPGRADEMANIFEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XUpgrade_config.h"

#if XUPGRADE_ON

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "XUpgrade.h"

/* ========================================================================== */
/*                        manifest 容量常量（均含结尾 NUL）                    */
/* ========================================================================== */

/** @brief 构建号容量（如 "2026.10.06-01"；含结尾 NUL）。 */
#define XUPGRADE_BUILD_MAX 32
/** @brief 组件包文件名容量（相对更新服务器 URL 的文件名；含结尾 NUL）。 */
#define XUPGRADE_FILENAME_MAX 96
/** @brief requires 约束串容量（如 ">=2.0"；含结尾 NUL）。 */
#define XUPGRADE_CONSTRAINT_MAX 16
/** @brief SHA-256 十六进制串长度（64 个十六进制字符 + 结尾 NUL）。 */
#define XUPGRADE_SHA256_HEX_MAX 65
/** @brief base64 签名串容量（P-256 签名 base64 约 88 字符；含结尾 NUL）。 */
#define XUPGRADE_SIGNATURE_MAX 128
/** @brief 单条 manifest 组件条目上限；超出即解析拒绝。 */
#define XUPGRADE_COMPONENT_MAX 8
/** @brief 单组件 requires 配套条目上限；超出即解析拒绝。 */
#define XUPGRADE_REQUIRES_MAX 4
/** @brief 灰度 allow/deny 名单各自身量上限（超出即解析拒绝）。 */
#define XUPGRADE_GRAY_LIST_MAX 8
/** @brief 设备序列号/灰度名单条目容量（含结尾 NUL）。 */
#define XUPGRADE_SERIAL_MAX 32
/** @brief canonical 序列化输出缓冲区上限（字节；契约容量 8192，覆盖
 *         XUPGRADE_COMPONENT_MAX×全字段 + 满额灰度名单的 canonical 形态，
 *         超出返回 false）。 */
#define XUPGRADE_MANIFEST_CANONICAL_MAX 8192u
/** @brief 多版本目录（schema 2）单目录 release 指针条目上限；超出即解析拒绝。 */
#define XUPGRADE_CATALOG_MAX 8

/* ========================================================================== */
/*                                  结构体定义                                 */
/* ========================================================================== */

/**
 * @brief 组件依赖约束表（manifest 组件的 requires 字段）。
 * @details 表达如 "app 1.2.3 requires fontres >=2.0" 的配套关系；staging
 *           完成时校验，不满足则整单不激活（设计文档 §3）。约束语义
 *           （">=" 等操作符与版本比较）的强制执行属 staging 校验实现。
 */
typedef struct XUpgradeManifestRequires {
    uint32_t m_count;    /**< 有效约束条数，[0, XUPGRADE_REQUIRES_MAX]。 */
    char m_component[XUPGRADE_REQUIRES_MAX][XUPGRADE_COMPONENT_ID_MAX]; /**< 被依赖组件 id。 */
    char m_constraint[XUPGRADE_REQUIRES_MAX][XUPGRADE_CONSTRAINT_MAX];  /**< 版本约束串，如 ">=2.0"；操作符为 ">="。 */
} XUpgradeManifestRequires;

/**
 * @brief 灰度发布规则（manifest 的 gray 字段，§11）。
 * @details 端上判定经 XUpgradeManifest_grayHit（算法入契约）：allow 名单
 *           命中即放行；deny 名单命中即拒绝；否则
 *           SHA256(salt 的 UTF-8 字节 || deviceKey 的 UTF-8 字节，无分隔符)
 *           摘要前 4 字节按大端无符号整数 mod 100 < percent。判定顺序与
 *           哈希算法不得随版本变化，保证多阶段放量（服务端改 percent 重签）
 *           与跨版本升级后判定一致（§11）。
 */
typedef struct XUpgradeManifestGray {
    uint32_t m_percent;  /**< 放行百分比 [0,100]；0 表示对名单外设备全部拒绝，100 表示全部放行。 */
    char m_salt[XUPGRADE_SERIAL_MAX];       /**< 分流哈希盐值；服务端改盐即可重新洗牌。 */
    uint32_t m_allowCount;                  /**< allow 名单条数，[0, XUPGRADE_GRAY_LIST_MAX]。 */
    char m_allow[XUPGRADE_GRAY_LIST_MAX][XUPGRADE_SERIAL_MAX];  /**< 强制放行的设备序列号名单。 */
    uint32_t m_denyCount;                   /**< deny 名单条数，[0, XUPGRADE_GRAY_LIST_MAX]。 */
    char m_deny[XUPGRADE_GRAY_LIST_MAX][XUPGRADE_SERIAL_MAX];   /**< 强制拒绝的设备序列号名单。 */
} XUpgradeManifestGray;

/**
 * @brief manifest 组件条目（schema v1 的 components[] 元素）。
 * @details 组件 id 与存储后端的关系见设计文档 §3：app（dir，主程序）、
 *           fontres（resources，字库等大资源）、image（image，嵌入式整镜像，
 *           与前两者互斥）。sha256 为下载/解包两次校验与断点续传分块校验的
 *           依据（设计文档 §4.2）。
 */
typedef struct XUpgradeManifestComponent {
    char m_id[XUPGRADE_COMPONENT_ID_MAX];        /**< 组件 id："app"/"fontres"/"image"。 */
    char m_os[XUPGRADE_OS_MAX];                  /**< 目标平台："win64"/"linux64"/"emb-arm" 或 "any"。 */
    char m_version[XUPGRADE_VERSION_MAX];        /**< 该组件的版本串。 */
    char m_file[XUPGRADE_FILENAME_MAX];          /**< 包文件名（相对 m_updateUrl 解析后的目录）；image 组件为 ".img" 裸镜像。 */
    uint64_t m_size;                             /**< 包文件总字节数；下载进度与磁盘余量预检使用。 */
    char m_sha256[XUPGRADE_SHA256_HEX_MAX];      /**< 包文件 SHA-256，小写十六进制。 */
    XUpgradeManifestRequires m_requires;         /**< 依赖的其它组件及版本约束；无依赖时 m_count 为 0。 */
} XUpgradeManifestComponent;

/**
 * @brief manifest schema v1 的完整内存表示。
 * @details 定长结构（约数 KB），可整体置于引擎静态存储；解析时任何数组
 *           越限、字段缺失或数值非法都导致整单拒绝
 *           （XUpgradeError_ManifestInvalid）。防降级与策略配套字段：
 *           m_rollbackTo 为签名的回滚窗口地板版本——本地记录的历史最高
 *           版本低于该值时不再拦截降级（"新版有问题批量回退"场景，§4.1）；
 *           m_minVersion 与 m_policyForcedBelow 以版本为锚触发 forced
 *           （无视组件策略与用户跳过，§10）。
 */
typedef struct XUpgradeManifest {
    uint32_t m_schema;                                  /**< schema 版本，恒为 1；其它值解析拒绝。 */
    char m_channel[XUPGRADE_CHANNEL_MAX];               /**< 发布渠道（"stable"/"beta"…）；与本地配置渠道一致才接受。 */
    char m_version[XUPGRADE_VERSION_MAX];               /**< 整包版本串（app 组件版本的冗余声明，以组件条目为准校验一致性）。 */
    char m_build[XUPGRADE_BUILD_MAX];                   /**< 构建号（如 "2026.10.06-01"），仅展示与日志。 */
    char m_minVersion[XUPGRADE_VERSION_MAX];            /**< 最低可升级版本线：本地组件版本低于该值时按 forced 处理；空串表示未设置。 */
    char m_rollbackTo[XUPGRADE_VERSION_MAX];            /**< 签名的回滚窗口地板版本；空串表示不允许任何降级。 */
    XUpgradePolicy m_policy;                            /**< 升级策略（forced/optional/silent，§10）。 */
    char m_policyForcedBelow[XUPGRADE_VERSION_MAX];     /**< 低于该版本无视组件策略按 forced 处理；空串表示未设置。 */
    uint32_t m_forcedGraceHours;                        /**< forced 宽限期（小时，§10）：期内遮罩允许"稍后"，每次启动重新提示；JSON 键 "forced_grace_hours"，缺省 0（staged 完成即自动 apply）。可被 XUpgrade_setForcedGraceHours 本地覆盖。 */
    XUpgradeManifestGray m_gray;                        /**< 灰度规则；判定经 XUpgradeManifest_grayHit（§11）。 */
    uint32_t m_componentCount;                          /**< 有效组件条数，[1, XUPGRADE_COMPONENT_MAX]。 */
    XUpgradeManifestComponent m_components[XUPGRADE_COMPONENT_MAX]; /**< 组件条目表，按 manifest 声明顺序。 */
    char m_sig[XUPGRADE_SIGNATURE_MAX];                 /**< base64(ECDSA-P256-SHA256(canonical(除本字段外全部字段)))；验签经 XUpgradeManifest_canonicalBytes + XUpgradeVerify_verifyP256。 */
} XUpgradeManifest;

/**
 * @brief 多版本目录条目（schema 2 目录的单条 release 指针）。
 * @details 目录（index.json，schema 2）本身不签名——信任链仍锚在各版本
 *          manifest 的签名上：安装任意条目时按目录给出的文件名拉取对应
 *          manifest 并整链验签后使用（设计文档 §4.2 口径不变）。
 */
typedef struct XUpgradeCatalogEntry {
    char m_version[XUPGRADE_VERSION_MAX];        /**< 该条目的发布版本串。 */
    char m_manifest[XUPGRADE_FILENAME_MAX];      /**< 该版本的 manifest 文件名（相对目录 URL；对应 .pak 相对同目录解析）。 */
} XUpgradeCatalogEntry;

/**
 * @brief 多版本目录（index.json，schema 2）的解析后表示。
 */
typedef struct XUpgradeCatalog {
    uint32_t m_schema;                                  /**< 目录 schema 版本，恒为 2；其它值解析拒绝。 */
    char m_channel[XUPGRADE_CHANNEL_MAX];               /**< 发布渠道；与本地配置渠道一致性语义同 v1 manifest。 */
    uint32_t m_releaseCount;                            /**< 有效 release 条数，[0, XUPGRADE_CATALOG_MAX]。 */
    XUpgradeCatalogEntry m_releases[XUPGRADE_CATALOG_MAX]; /**< release 指针表，按声明顺序（建议服务端新版本在前）。 */
} XUpgradeCatalog;

/* ========================================================================== */
/*                                  公共 API                                   */
/* ========================================================================== */

/**
 * @brief 解析多版本目录 JSON（schema 2）并填充结构体。
 * @param out 输出结构体；调用方提供存储空间。无论成败，函数先整体清零再
 *            填充，失败时 out 保持清零态。
 * @param jsonUtf8 目录 JSON 文本；UTF-8 借用，不必以 NUL 结尾，不得为 NULL。
 * @param lengthBytes jsonUtf8 的字节数；0 返回 false。
 * @return 解析成功返回 true；JSON 非法、schema 不为 2、数组越限或字段非法
 *         返回 false。目录不承载签名与版本号语义（信任链在每份 manifest）。
 * @note 与 v1 manifest 的区分由调用方先做（v1 顶层必有 "components" 数组、
 *       目录顶层必有 "releases" 数组）。
 */
bool XUpgradeManifest_parseCatalog(XUpgradeCatalog* out, const char* jsonUtf8, size_t lengthBytes);

/**
 * @brief 解析 manifest JSON 文本并填充结构体（schema v1，单版本发布）。
 * @param out 输出结构体；调用方提供存储空间。无论成败，函数先整体清零再
 *            填充，失败时 out 保持清零态。
 * @param jsonUtf8 manifest JSON 文本；UTF-8 借用，不必以 NUL 结尾，不得为
 *                 NULL。
 * @param lengthBytes jsonUtf8 的字节数；0 返回 false。
 * @return 解析成功返回 true；JSON 非法、schema 不为 1、必填字段缺失、数组
 *         越限或数值非法返回 false（XUpgradeError_ManifestInvalid 语义）。
 * @note 本函数只做结构解析，不做签名校验（XUpgradeManifest_canonicalBytes
 *       + XUpgradeVerify_verifyP256）与灰度判定（XUpgradeManifest_grayHit）；
 *       不做本机 os 匹配过滤，匹配过滤经 XUpgradeManifest_findComponent
 *       进行。
 */
bool XUpgradeManifest_parse(XUpgradeManifest* out, const char* jsonUtf8, size_t lengthBytes);

/**
 * @brief 按组件 id 与目标平台查找组件条目。
 * @param manifest 已解析的 manifest；不能为 NULL。
 * @param componentId 组件 id；UTF-8 借用，不能为 NULL。
 * @param targetOs 本机目标平台标识；UTF-8 借用，不能为 NULL。
 * @return 命中的组件条目的借用指针，生命周期与 manifest 相同，调用方不得
 *         释放；无精确 os 匹配时回退 os=="any" 条目；都不存在返回 NULL。
 * @note 精确 os 匹配优先于 "any"；同 id 同 os 出现多条时取第一条。
 */
const XUpgradeManifestComponent* XUpgradeManifest_findComponent(const XUpgradeManifest* manifest,
                                                                const char* componentId,
                                                                const char* targetOs);

/**
 * @brief 比较两个版本串的大小。
 * @param lhs 左版本串；UTF-8 借用，NULL 按空串处理。
 * @param rhs 右版本串；UTF-8 借用，NULL 按空串处理。
 * @return lhs 低于 rhs 返回负值；相等返回 0；lhs 高于 rhs 返回正值。
 * @note 比较规则：按 '.' 分段，数字段按无符号整数比较（"1.2.10" 高于
 *       "1.2.9"），非数字尾缀段按 UTF-8 字节序比较；段数不足的版本以 0 段
 *       补齐（"2.0" 等价 "2.0.0"）。版本串白名单 [0-9A-Za-z._-]，含其它
 *       字符时行为未定义（解析层已拒绝）。检查、防降级（maxVersion）与
 *       requires 校验统一使用本函数，保证跨版本判定一致。
 */
int XUpgradeManifest_compareVersions(const char* lhs, const char* rhs);

/**
 * @brief 按契约算法执行灰度命中判定（§11）。
 * @param gray 灰度规则；借用，不能为 NULL（m_percent 越界 [0,100] 时行为
 *             未定义，解析层已拒绝）。
 * @param deviceKey 灰度设备键（XUpgradeIdentity_deviceKey 的结果）；UTF-8
 *                  借用，NULL 按空串处理。
 * @return 判定放行返回 true（allow 命中或分流命中）；拒绝返回 false（deny
 *         命中或分流未命中）。
 * @note 判定顺序（不得随版本变化）：allow 名单逐条精确匹配命中 → true；
 *       deny 名单逐条精确匹配命中 → false；否则对 salt 的 UTF-8 字节与
 *       deviceKey 的 UTF-8 字节直接拼接（无分隔符）计算 SHA-256，取摘要
 *       前 4 字节按大端无符号整数 mod 100 < m_percent。名单为空视为不
 *       命中；m_percent=0 时名单外全部拒绝，100 时全部放行。调用方负责
 *       先决条件（灰度开关、gray 字段存在性），未判定语义
 *       （XUpgradeGrayDecision_NotEvaluated）由调用方（引擎）维护。
 */
bool XUpgradeManifest_grayHit(const XUpgradeManifestGray* gray, const char* deviceKey);

/**
 * @brief 按 canonical 规则重序列化 manifest（验签被签内容，§4.2）。
 * @param manifest 已解析的 manifest；不能为 NULL。
 * @param outBuffer 输出缓冲区；调用方提供存储空间，不能为 NULL。
 * @param cap 缓冲区容量（字节）；不足 XUPGRADE_MANIFEST_CANONICAL_MAX 时
 *            建议直接按该常量分配。
 * @param outLen 输出 canonical 字节串实际长度；不能为 NULL，失败时不写入。
 * @return 成功返回 true；参数非法或 cap 不足返回 false（outLen 不写入）。
 * @note canonical 规则（§4.2，跨版本不得变化）：全部对象键名（含 requires
 *       的组件键）字典序递归排序，数组元素保持 manifest 声明顺序；无空白、
 *       UTF-8、数字十进制无前导零；m_sig 不参与序列化（签名对象=除 sig 外
 *       全部字段）。服务端以同一规则对待签字段签名；设备侧 base64 解码
 *       m_sig 后连同信任公钥（XUpgrade_setTrustKey）交
 *       XUpgradeVerify_verifyP256 验签。
 */
bool XUpgradeManifest_canonicalBytes(const XUpgradeManifest* manifest,
                                     uint8_t* outBuffer, uint32_t cap, uint32_t* outLen);

#endif /* XUPGRADE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XUPGRADEMANIFEST_H */
