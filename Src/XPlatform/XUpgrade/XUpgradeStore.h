/**
 * @file       XUpgradeStore.h
 * @brief      升级存储后端契约：staging/激活/回滚/版本枚举/启动确认的 vtable 驱动抽象类与 A/B 分区原语。
 * @details    按仓库驱动契约惯例（先例 XSqlDriver + XMySqlDriver）定义：契约
 *             在 Src/XPlatform/XUpgrade，实现按构建目标静态选型——
 *             XUpgradeStoreDir（目录后端，三平台统一基线）与
 *             XUpgradeStoreAb（A/B 后端，XUPGRADE_AB_ON 时编入）。调用方
 *             一律经本文件公开的 *_base 分派函数走对象虚表，不得直接访问
 *             槽位；释放统一用 XClassDelete（堆对象）/XClassDeinit（栈对象）。
 *             崩溃安全不变式（设计文档 §16）：staged 标记只在全部校验通过
 *             后写入；指针/state.json/元数据块等持久写全部原子（XSaveFile
 *             临时替换 / 双块 sequence）；旧版本目录在自检通过前永不删除，
 *             A/B 从不覆写 activeSlot。
 *             A/B 契约（§9）：分区原语经 XUpgradeStore_setPartitionBackend
 *             由 Drive 注册（模式同 XSystem_setRebootHandler），流程编排全
 *             部在 XUpgradeStoreAb；元数据块布局、双写规则与 sequence 仲裁
 *             见 XUpgradeBootMeta 与 XUpgradeStoreAb_create_ex 注释。
 */

#ifndef XUPGRADESTORE_H
#define XUPGRADESTORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XUpgrade_config.h"

#if XUPGRADE_ON

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "XObject.h"
#include "XUpgrade.h"
#include "XUpgradePackage.h" /* XUPGRADE_SHA256_BIN_SIZE：XUpgradeBootMeta.m_imageSha256 容量（自包含，禁止依赖包含顺序） */

/* ========================================================================== */
/*                        三平台统一目录布局常量（相对安装根）                  */
/* ========================================================================== */

/** @brief 版本目录名：versions/&lt;comp&gt;/&lt;ver&gt;/，旧版本目录即回滚保底。 */
#define XUPGRADE_DIR_VERSIONS "versions"
/** @brief 指针目录名：pointers/&lt;comp&gt; 单行文本记录激活版本（无扩展名）。 */
#define XUPGRADE_DIR_POINTERS "pointers"
/** @brief 引擎工作区目录名：可整目录删除重来，不损坏已安装版本。 */
#define XUPGRADE_DIR_UPDATE "update"
/** @brief 下载中目录名：update/partial/（.pak.part + .part.meta）。 */
#define XUPGRADE_DIR_PARTIAL "partial"
/** @brief 状态机持久化文件名：update/state.json（原子写+CRC）。 */
#define XUPGRADE_FILE_STATE "state.json"
/** @brief 引擎互斥锁文件名：update/lock（含持有者 PID，启动时清死锁）。 */
#define XUPGRADE_FILE_LOCK "lock"
/** @brief 升级专用日志文件名：update/update.log（与主日志分流）。 */
#define XUPGRADE_FILE_LOG "update.log"
/** @brief 版本目录内版本标记文件名：versions/&lt;comp&gt;/&lt;ver&gt;/manifest.version（自检比对依据）。 */
#define XUPGRADE_FILE_VERSION_MARK "manifest.version"

/* ========================================================================== */
/*            嵌入式 A/B 契约（XUPGRADE_AB_ON 时编入；设计文档 §9）            */
/* ========================================================================== */

#if XUPGRADE_AB_ON

/** @brief 元数据块魔数：小端 u32 值，即 ASCII "XUBM"（§9.2）。 */
#define XUPGRADE_BOOT_META_MAGIC 0x4D425558u
/** @brief 元数据块序列化定长（字节）：6×u32 + 32 + u32 = 60，成员小端、无填充。 */
#define XUPGRADE_BOOT_META_SIZE 60u
/** @brief 元数据块份数：双份冗余、扇区对齐、写前擦除（元数据块A=0、B=1；§9.2）。 */
#define XUPGRADE_BOOT_META_COPIES 2u
/** @brief boot 尝试次数上限缺省值：超过即由 bootloader 回切 activeSlot（§9.3 建议 3）。 */
#define XUPGRADE_BOOT_ATTEMPTS_DEFAULT 3u
/** @brief 镜像槽索引上限（后端可报告更少；引擎仅使用 [0, slotCount)）。 */
#define XUPGRADE_AB_SLOT_MAX 4u

/**
 * @brief bootloader 元数据块内存表示（§9.2；需与 bootloader 团队评审签字）。
 * @details 序列化布局=成员声明顺序、小端、无填充，共 XUPGRADE_BOOT_META_SIZE
 *           字节：偏移 0 magic、4 crc32、8 activeSlot、12 requestedSlot、
 *           16 bootAttempts、20 maxAttempts、24 imageSha256[32]、56
 *           sequence。crc32=整块序列化字节中除偏移 4..7（本字段自身）外的
 *           全部字节（0..3 与 8..59 两段）的 CRC32；序列化/解析与 CRC 校验
 *           都在 XUpgradeStoreAb 侧完成，分区原语不解释内容。实现文件必须
 *           以编译期断言固定 sizeof(XUpgradeBootMeta)==60（C99 用数组越界
 *           技巧或枚举断言）。
 */
typedef struct XUpgradeBootMeta {
    uint32_t m_magic;         /**< 魔数，恒为 XUPGRADE_BOOT_META_MAGIC（"XUBM"）；不符视作空块。 */
    uint32_t m_crc32;         /**< 除本字段外整块序列化字节的 CRC32（偏移 4..7 不参与）。 */
    uint32_t m_activeSlot;    /**< 上次确认成功的槽索引：confirmBoot 后等于 requestedSlot；bootloader 的回退锚点。 */
    uint32_t m_requestedSlot; /**< 本次要求启动的槽索引；activate 写入，confirmBoot 收敛回 activeSlot。 */
    uint32_t m_bootAttempts;  /**< requestedSlot 的已尝试启动次数；bootloader 每次尝试启动前自增写回（双写规则同 activate）。 */
    uint32_t m_maxAttempts;   /**< 尝试上限：bootAttempts >= m_maxAttempts 时 bootloader 置 requestedSlot=activeSlot 并回切（缺省 XUPGRADE_BOOT_ATTEMPTS_DEFAULT）。 */
    uint8_t m_imageSha256[XUPGRADE_SHA256_BIN_SIZE]; /**< requestedSlot 镜像 SHA-256（二进制摘要）；bootloader 可选校验，应用侧在 activate 前整槽回读得到。 */
    uint32_t m_sequence;      /**< 单调递增序号：双块取舍依据——CRC 合法且 sequence 大者胜（§9.3）；每次合法更新 +1，u32 绕回前由维护流程整体重置双块。 */
} XUpgradeBootMeta;

/**
 * @brief A/B 分区原语回调集（Drive 注册，模式同 XSystem_setRebootHandler；§7/§9.1）。
 * @details 平台侧实现分区定位、槽块读写、擦除与元数据块擦写
 *           （Drive/&lt;Platform&gt;/xupgrade_partition_&lt;platform&gt;）；
 *           流程编排（流式写非活动槽、整槽回读哈希、双写元数据块、仲裁）
 *           全部在 XUpgradeStoreAb。回调返回 XUpgradeError：Ok=成功；
 *           Io=读写/擦除失败（含 ECC 双位错、写前擦除失败）；
 *           InvalidArgument=槽索引/偏移越界；NotSupported=该原语未实现。
 *           槽内偏移按字节计，读写长度不要求扇区对齐（对齐由实现内部
 *           处理，read 可读出整扇区后裁剪）；回调可能在慢速总线上执行，
 *           实现不得无限阻塞。
 */
typedef struct XUpgradePartitionBackend {
    void* m_userData; /**< 原样回传给各回调的用户上下文；库不取得所有权，注册后不得释放。 */
    /** @brief 查询镜像槽数；outCount 写入 [2, XUPGRADE_AB_SLOT_MAX] 才可启用 A/B，少于 2 视作配置错误。 */
    XUpgradeError (*m_slotCount)(void* userData, uint32_t* outCount);
    /** @brief 查询指定槽容量（字节）；镜像 m_size 超过最小槽容量时整单拒绝。 */
    XUpgradeError (*m_slotSize)(void* userData, uint32_t slotIndex, uint64_t* outSize);
    /** @brief 从槽内 offset 起读 len 字节到 out；out 由调用方提供，len 为 0 直接返回 Ok。 */
    XUpgradeError (*m_read)(void* userData, uint32_t slotIndex, uint64_t offset, uint8_t* out, uint32_t len);
    /** @brief 向槽内 offset 起写 len 字节；写前擦除责任在实现（未擦区域写失败返回 Io）。 */
    XUpgradeError (*m_write)(void* userData, uint32_t slotIndex, uint64_t offset, const uint8_t* data, uint32_t len);
    /** @brief 擦除槽内覆盖 [offset, offset+len) 的扇区区间（实现可扩整扇区）；len 为 0 直接返回 Ok。 */
    XUpgradeError (*m_erase)(void* userData, uint32_t slotIndex, uint64_t offset, uint32_t len);
    /** @brief 读指定元数据块（[0, XUPGRADE_BOOT_META_COPIES)）原始内容并按 §9.2 布局反序列化到 out；不校验 CRC（仲裁与 CRC 在 XUpgradeStoreAb）。 */
    XUpgradeError (*m_metaRead)(void* userData, uint32_t metaIndex, XUpgradeBootMeta* out);
    /** @brief 将 meta 按 §9.2 布局序列化写入指定元数据块；实现内部先擦后写，写中途断电该块 CRC 必不合法（由双块另一块兜底）。 */
    XUpgradeError (*m_metaWrite)(void* userData, uint32_t metaIndex, const XUpgradeBootMeta* meta);
} XUpgradePartitionBackend;

/**
 * @brief 注册或清除 A/B 分区原语后端。
 * @param backend 回调集；借用指针，库不复制不释放，注册后在 AbStore 生命周期
 *                内必须保持有效。传 NULL 清除注册。
 * @return 无。
 * @note 必须在创建 XUpgradeStoreAb 或 XUpgrade_init 之前完成注册；不支持
 *       引擎运行期替换。与 XUpgradeStoreAb_installSimBackend 互斥：显式
 *       注册了后端后再装模拟后端返回失败。XUPGRADE_AB_ON=0 构建下本入口
 *       不存在。
 */
void XUpgradeStore_setPartitionBackend(const XUpgradePartitionBackend* backend);

#endif /* XUPGRADE_AB_ON */

/* ========================================================================== */
/*                                  抽象后端类                                 */
/* ========================================================================== */

/**
 * @brief 历史版本枚举回调。
 * @param userData 枚举发起方传入的上下文；后端原样回传，不取得所有权。
 * @param version 版本串；UTF-8 借用，仅本次回调期间有效。
 * @return 0 继续枚举；非 0 中止枚举并由 XUpgradeStore_listVersions_base
 *         原样返回该值。
 */
typedef int (*XUpgradeStoreEmitVersion)(void* userData, const char* version);

/**
 * @brief XUpgradeStore 的虚函数槽位定义。
 * @details 槽位为目录/A-B 后端提供 ABI；新槽位从 XObject 总槽数起编号，
 *          只允许尾部追加（ConfirmBoot 为扩展槽，目录后端幂等返回 Ok）。
 *          实现方在 class_init 中先 XVTABLE_INHERIT_XCLASS(XUpgradeStore)
 *          再逐槽 XVTABLE_OVERLOAD_DEFAULT 重载（deinit 走 EXClass_Deinit）。
 */
XCLASS_DEFINE_BEGING(XUpgradeStore)
XCLASS_DEFINE_ENUM(XUpgradeStore, StageBegin) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XUpgradeStore, StageWrite),
XCLASS_DEFINE_ENUM(XUpgradeStore, StageEnd),
XCLASS_DEFINE_ENUM(XUpgradeStore, Activate),
XCLASS_DEFINE_ENUM(XUpgradeStore, CurrentVersion),
XCLASS_DEFINE_ENUM(XUpgradeStore, Rollback),
XCLASS_DEFINE_ENUM(XUpgradeStore, ListVersions),
XCLASS_DEFINE_ENUM(XUpgradeStore, ConfirmBoot),
XCLASS_DEFINE_END(XUpgradeStore)

/**
 * @brief 升级存储后端抽象对象。
 * @details 本抽象类不持有后端状态；staging 上下文（当前组件/版本/写入位置）
 *          由具体后端在派生结构中维护。一个后端实例同一时间只允许一路
 *          staging 流（stageBegin→stageWrite*→stageEnd 顺序执行）。
 */
typedef struct XUpgradeStore {
    XObject m_class;   /**< 第一个成员，由 XObject 管理；禁止手工修改。 */
} XUpgradeStore;

/* ========================================================================== */
/*                     虚函数表初始化 / 构造与析构                             */
/* ========================================================================== */

/**
 * @brief 初始化 XUpgradeStore 虚函数表。
 * @return 共享虚函数表；初始化失败返回 NULL。
 * @note 公开抽象类入口（非 static）；基类槽位默认实现返回
 *       XUpgradeError_NotSupported（ConfirmBoot 槽目录后端重载为幂等 Ok）。
 */
XVtable* XUpgradeStore_class_init(void);

/**
 * @brief 初始化抽象存储后端（栈对象入口）。
 * @param store 待初始化对象；不能为 NULL，vtable 非空（已初始化）时行为未定义。
 * @return 无；失败时对象保持未初始化状态。
 * @note 内部完成 XObject 基类初始化并绑定 XUpgradeStore 虚表；派生实现
 *       先调用本函数再初始化自己的成员。与 XClassDeinit 成对出现。
 */
void XUpgradeStore_init(XUpgradeStore* store);

/**
 * @brief 创建抽象存储后端对象（派生类 create 的基类构造步骤）。
 * @param memory 内存池类型；决定对象分配与释放所用的分配器。
 * @return 新对象，调用者必须使用 XClassDelete 释放；分配失败返回 NULL。
 * @note 抽象基类对象的全部分派入口返回 XUpgradeError_NotSupported；产品代码
 *       应使用具体后端的 create（如 XUpgradeStoreDir_create_ex/
 *       XUpgradeStoreAb_create_ex）。
 */
XUpgradeStore* XUpgradeStore_create_ex(XMemoryType memory);

/* ========================================================================== */
/*                        虚函数分派入口（*_base）                             */
/* ========================================================================== */

/**
 * @brief 开始为组件 staging 一个新版本。
 * @param self 后端对象；不能为 NULL。
 * @param componentId 组件 id（"app"/"fontres"/"image"）；UTF-8 借用，不能为 NULL。
 * @param version 目标版本串；UTF-8 借用，不能为 NULL。
 * @param expectedSize 预期写入总字节数（manifest 组件 m_size），供后端做
 *                     余量预检（目录=磁盘余量；A/B=槽容量）；0 表示未知。
 * @param errText 输出错误说明；可为 NULL。非 NULL 时失败时写入静态借用文本
 *                （仅本次调用有效，调用方不得释放），成功时写入 NULL。
 * @return 成功返回 XUpgradeError_Ok；参数非法、余量不足、同版本目录已存在
 *         半成品未清理或后端不支持返回对应错误码。
 * @note 目录后端返回写入槽=新版本目录 versions/&lt;comp&gt;/&lt;ver&gt;/；
 *       A/B 后端返回写入槽=非活动槽。校验职责在引擎层，后端只管落盘。
 */
XUpgradeError XUpgradeStore_stageBegin_base(XUpgradeStore* self, const char* componentId,
                                            const char* version, uint64_t expectedSize,
                                            const char** errText);

/**
 * @brief 流式写入一段 staging 数据（下载/解包共用此入口）。
 * @param self 后端对象；不能为 NULL，且必须处于 staging 流中。
 * @param data 数据块；借用，不得为 NULL。
 * @param len 数据块字节数；0 直接返回 Ok。
 * @return 成功返回 XUpgradeError_Ok；未 stageBegin、写入失败或介质满返回
 *         对应错误码，staging 流作废（需重新 stageBegin）。
 * @note 后端不做校验，校验在引擎层；后端保证数据先落临时位置（目录=staging
 *       临时目录；A/B=非活动槽内 staging 区），stageEnd 才对外可见。
 */
XUpgradeError XUpgradeStore_stageWrite_base(XUpgradeStore* self, const void* data, uint32_t len);

/**
 * @brief staging 完成：持久落盘并做后端级校验。
 * @param self 后端对象；不能为 NULL，且必须处于 staging 流中。
 * @param errText 输出错误说明；语义同 XUpgradeStore_stageBegin_base。
 * @return 成功返回 XUpgradeError_Ok；落盘失败或后端级校验失败返回对应错
 *         误码（A/B=整槽回读哈希，可配默认开；目录=目录改名落位）。
 * @note 目录后端在此把临时数据定稿为版本目录；逐条目内容校验在引擎层
 *       完成后才允许调用本入口。staged 标记由引擎写 state.json（不变式 1）。
 */
XUpgradeError XUpgradeStore_stageEnd_base(XUpgradeStore* self, const char** errText);

/**
 * @brief 原子激活指定版本。
 * @param self 后端对象；不能为 NULL。
 * @param componentId 组件 id；UTF-8 借用，不能为 NULL。
 * @param version 要激活的版本串（必须已 staging 完成）；UTF-8 借用，不能为 NULL。
 * @param errText 输出错误说明；语义同 XUpgradeStore_stageBegin_base。
 * @return 成功返回 XUpgradeError_Ok；目标版本不存在、指针/元数据写入失败
 *         或后端不支持返回对应错误码。
 * @note 返回 Ok 后新版本必须在下次启动生效（或由 apply 桩/bootloader 完
 *       成）。目录后端=经 XSaveFile 原子写 pointers/&lt;comp&gt;；A/B 后端=
 *       整槽校验通过后按双写规则置 requestedSlot=新槽再重启（规则全文见
 *       XUpgradeStoreAb_create_ex 注释，§9.4）。
 */
XUpgradeError XUpgradeStore_activate_base(XUpgradeStore* self, const char* componentId,
                                          const char* version, const char** errText);

/**
 * @brief 查询组件当前激活版本。
 * @param self 后端对象；不能为 NULL。
 * @param componentId 组件 id；UTF-8 借用，不能为 NULL。
 * @param out 输出缓冲区；成功时写入以 NUL 结尾的 UTF-8 版本串。
 * @param cap 缓冲区容量（字节，含结尾 NUL）；0 返回 false。
 * @return 存在激活版本返回 true；无激活、参数非法或缓冲区不足返回 false
 *         （此时 out 内容未定义，调用方不得读取）。
 * @note 读失败/内容非法视作未激活（设计文档 §2），调用方据此走启动自检
 *       回滚路径。A/B 后端返回当前 activeSlot 对应镜像的版本（镜像头内嵌
 *       版本，§17.3）。
 */
bool XUpgradeStore_currentVersion_base(XUpgradeStore* self, const char* componentId,
                                       char* out, uint32_t cap);

/**
 * @brief 回滚到指定版本（自检失败路径）。
 * @param self 后端对象；不能为 NULL。
 * @param componentId 组件 id；UTF-8 借用，不能为 NULL。
 * @param version 回滚目标版本；要求其版本目录/槽位仍完好，UTF-8 借用。
 * @param errText 输出错误说明；语义同 XUpgradeStore_stageBegin_base。
 * @return 成功返回 XUpgradeError_Ok；目标版本缺失或写回失败返回对应错误
 *         码（引擎继续上报 RolledBack 并按 §8.3 计数保护）。
 * @note 回滚=指针/槽位写回，旧版本数据从不删除，天然免费。A/B 后端=按
 *       双写规则置 requestedSlot=activeSlot 旧槽（元数据写失败属 Io，由
 *       bootloader 计数回退兜底）。
 */
XUpgradeError XUpgradeStore_rollback_base(XUpgradeStore* self, const char* componentId,
                                          const char* version, const char** errText);

/**
 * @brief 枚举后端保留的历史版本（GC 用）。
 * @param self 后端对象；不能为 NULL。
 * @param componentId 组件 id；UTF-8 借用，不能为 NULL。
 * @param emit 逐版本回调；不能为 NULL。返回非 0 时中止枚举。
 * @param userData 原样传给 emit 的上下文；可为 NULL。
 * @return 全部枚举完成返回 XUpgradeError_Ok；emit 中止时原样返回其返回值；
 *         后端枚举失败返回对应错误码。
 * @note 枚举顺序未定义（引擎自行按 XUpgradeManifest_compareVersions 排序
 *       决定 GC 目标）；引擎策略为默认保留最近 2 个版本（设计文档 §2）。
 *       A/B 后端仅有 activeSlot 一个可用版本，枚举至多一条。
 */
XUpgradeError XUpgradeStore_listVersions_base(XUpgradeStore* self, const char* componentId,
                                              XUpgradeStoreEmitVersion emit, void* userData);

/**
 * @brief 启动自检通过后确认新版本生效（§9.4，扩展槽）。
 * @param self 后端对象；不能为 NULL。
 * @param componentId 组件 id；UTF-8 借用，不能为 NULL。
 * @param version 已确认生效的版本串；UTF-8 借用，不能为 NULL。
 * @param errText 输出错误说明；语义同 XUpgradeStore_stageBegin_base。
 * @return 成功返回 XUpgradeError_Ok；A/B 元数据双写失败返回 Io（此时
 *         bootloader 计数回退机制仍兜底，引擎按 §8.3 记回滚保护计数）。
 * @note A/B 后端动作=按双写规则置 activeSlot=requestedSlot、bootAttempts=0、
 *       sequence+1（规则全文见 XUpgradeStoreAb_create_ex 注释）；目录后端
 *       重载为幂等 Ok（指针即生效标记，无独立确认步）。引擎的
 *       XUpgrade_confirmBoot（XUpgrade.h）透传本入口。
 */
XUpgradeError XUpgradeStore_confirmBoot_base(XUpgradeStore* self, const char* componentId,
                                             const char* version, const char** errText);

/* ========================================================================== */
/*                    A/B 后端工厂与宿主模拟（XUPGRADE_AB_ON）                 */
/* ========================================================================== */

#if XUPGRADE_AB_ON

/**
 * @brief 创建 A/B 存储后端对象。
 * @param memory 内存池类型；决定对象分配与释放所用的分配器。
 * @param installRootUtf8 安装根目录完整路径；UTF-8 借用，不能为 NULL，内部
 *                        复制。仅用于 update/ 工作区（state.json/lock/
 *                        partial）；镜像数据不经文件系统，流式直写槽。
 * @return 新后端对象（以基类 XUpgradeStore 指针返回），调用者必须使用
 *         XClassDelete 释放；未注册 XUpgradePartitionBackend、槽数少于 2
 *         或分配失败返回 NULL。
 * @note 组件线仅接受 "image"（§3 与 app/fontres 互斥）；其它组件 id 的
 *       stageBegin/activate 返回 XUpgradeError_NotSupported。
 *
 *       <b>元数据块双写规则与 sequence 仲裁（§9.3/§9.4，实现与 bootloader
 *       两侧必须一致）：</b>
 *       1. 仲裁：读 A/B 两块，CRC 合法且 sequence 大者为胜出块；sequence
 *          相等且均合法（异常场景）取元数据块 A（metaIndex 0）为胜出，
 *          AbStore 下一次合法更新恢复双块一致；两块皆坏属存储级故障，
 *          应用侧报 XUpgradeError_Io 并停用自动升级，bootloader 侧回退
 *          板级缺省槽（永不两个都动）。
 *       2. 更新（activate/confirmBoot/bootAttempts 自增同规则）：新
 *          sequence = 胜出块 sequence + 1；先写非胜出块（影子块）为新
 *          状态，再写胜出块为同一新状态。任意断电时刻至少一块合法：
 *          写影子块中断电→胜出块仍为旧状态（本次更新丢失，设备安全回
 *          旧槽）；写胜出块中断电→影子块已为新状态（更新生效）。
 *       3. activate：写完非活动槽并整槽回读校验通过后，按规则 2 双写置
 *          requestedSlot=新槽、bootAttempts=0、sequence+1，再
 *          XSystem_reboot（§9.4）。
 *       4. confirmBoot：自检通过后按规则 2 双写置 activeSlot=requestedSlot、
 *          bootAttempts=0、sequence+1；确认前 bootloader 计数回退兜底
 *          （§9.3：bootAttempts >= maxAttempts → 回切 activeSlot）。
 *       5. 不变式：从不覆写 activeSlot 指向槽的镜像数据；任意时刻断电，
 *          设备要么启动旧槽，要么启动新槽并仍可回旧槽（§16 矩阵 8-11）。
 */
XUpgradeStore* XUpgradeStoreAb_create_ex(XMemoryType memory, const char* installRootUtf8);

#if XUPGRADE_AB_SIM_ON

/**
 * @brief 安装/卸载宿主模拟分区后端（仅测试与桌面演练；XUPGRADE_AB_SIM_ON）。
 * @param workDirUtf8 模拟分区持久化目录；UTF-8 借用，不能为 NULL（卸载时
 *                    传 NULL）。目录内以文件模拟双槽与双元数据块，每次写
 *                    后即落盘；测试直接破坏/截断对应文件即可注入断电矩阵
 *                    8-11 的故障（§16）。
 * @param slotSizeBytes 单槽容量（字节）；0 取缺省 4 MiB。模拟镜像不得大于
 *                      该值。
 * @return 安装成功返回 true；目录不可创建、slotSizeBytes 非 0 且小于 64 KiB、
 *         或已有后端注册（含真实 Drive 后端）返回 false。
 * @note 内置 XUpgradePartitionBackend 实现（XCode/XUpgrade 侧）：宿主文件
 *       无擦除概念，erase 原语直接返回 Ok；metaWrite 原样覆写文件。仅供
 *       测试——生产固件置 XUPGRADE_AB_SIM_ON=0 裁出。传 NULL 卸载。
 */
bool XUpgradeStoreAb_installSimBackend(const char* workDirUtf8, uint32_t slotSizeBytes);

#endif /* XUPGRADE_AB_SIM_ON */

#endif /* XUPGRADE_AB_ON */

#endif /* XUPGRADE_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XUpgradeStore_create
#define XUpgradeStore_create(...) XUpgradeStore_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)
#if XUPGRADE_ON && XUPGRADE_AB_ON
#undef XUpgradeStoreAb_create
#define XUpgradeStoreAb_create(...) XUpgradeStoreAb_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)
#endif

#endif /* XUPGRADESTORE_H */
