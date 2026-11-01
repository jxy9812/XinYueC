/**
 * @file       XUpgradeEngine.c
 * @brief      升级引擎内部实现：状态机载体、update/state.json 持久化、
 *             后台线程流水（检查/下载/校验/staging）、启动自检与回滚。
 * @details    设计文档 XUpgrade.md §6/§8.3/§10/§16 的 M1 实现。状态机采用
 *             enum XUpgradeState + 工作线程内流水（不引入 XStateMachine：
 *             引擎跑在阻塞工作线程上，下载/校验/解包为长阻塞段，且
 *             XStateMachine 为内存对象图、无持久化 API，帮不上 state.json
 *             断电收敛）。后台线程为 XThread_create_func 裸工作线程（XTask
 *             是任务诊断快照契约，无执行 API）。HTTP 下载经
 *             XNetworkAccessManager，在引擎工作线程内创建并 processEvents
 *             自旋同步等待（事务 socket 与等待同线程，先例
 *             XServerChan_sendBlocking/XHttpTest xhttp_test_manager_local）。
 *             持久写一律 XSaveFile 原子替换（崩溃不变式 2）；staged 标记只在
 *             全部校验通过后写（不变式 1）；旧版本目录自检通过前不删
 *             （不变式 3）；update/lock 心跳锁防双实例（不变式 6）。
 *             组件模型（§3）调度在本文件收口：升级目标表按组件 id 去重
 *             （精确 os 优先，findComponent 同口径）且不超出 state.json 组件
 *             表容量；下载/校验/staging 按 requires 依赖序推进（fontres 先于
 *             依赖它的 app）；staging 全部完成后 requires 配套校验整单放行或
 *             作废；state.json 组件级 staged 标记与版本目录保持断电一致；自检
 *             对从未激活组件线（fontres 等资源线首装）按指针收编并做 .verify
 *             抽查，GC 按组件线各自保留 2 版。
 */
#include "XUpgradeEngine.h"

#include "xupgrade_shared.h" /* 模块内部共享小工具（xups_*：串/版本/路径/摘要） */
#include "XUpgradeStore.h"
#include "XUpgradeStoreDir.h"
#include "XUpgradeIdentity.h"

#include "XObject.h"
#include "XThread.h"
#include "XMutex.h"
#include "XDateTime.h"
#include "XString.h"
#include "XByteArray.h"
#include "XFile.h"
#include "XDir.h"
#include "XFileInfo.h"
#include "XSaveFile.h"
#include "XJsonDocument.h"
#include "XJsonObject.h"
#include "XJsonValue.h"
#include "XCrc.h"
#include "XCryptographicHash.h"
#include "XStringUtils.h"
#include "XSystem.h"
#include "XPrintf.h"

#if XUPGRADE_HTTP_ON
#include "XCoreApplication.h"
#include "XNetworkAccessManager.h"
#include "XHttpRequest.h"
#include "XHttpReply.h"
#endif

#include "zlib.h"

#include <limits.h>
#include <stdarg.h>

#if XUPGRADE_ON

/* ========================================================================== */
/*                            内部前置声明                                     */
/* ========================================================================== */

/** @brief 流水内锁心跳刷新（限频；定义见引擎锁一节，长循环多处前置调用）。 */
static void xup_lock_heartbeat(XUpgradeEngine* e);

/* ========================================================================== */
/*                                  内部常量                                   */
/* ========================================================================== */

/** @brief 工作线程空闲轮询周期（毫秒）。 */
#define XUPGRADE_TICK_MS 200u
/** @brief 引擎锁心跳落盘周期（毫秒）。 */
#define XUPGRADE_LOCK_HEARTBEAT_WRITE_MS 5000u
/** @brief 引擎锁陈旧判定阈值（毫秒；单调钟跨进程可比）。 */
#define XUPGRADE_LOCK_STALE_MS 15000u
/** @brief 下载分块大小（字节）；分块哈希游标粒度。 */
#define XUPGRADE_CHUNK_SIZE (1024ull * 1024ull)
/** @brief 工作线程 join 超时（毫秒；须大于一次 HTTP 同步超时，防运行中删除）。 */
#define XUPGRADE_JOIN_TIMEOUT_MS 35000u
/** @brief HTTP 同步等待超时（毫秒）。 */
#define XUPGRADE_HTTP_TIMEOUT_MS 30000
/** @brief HTTP 结束后等待管理器清理事务的兜底时限（毫秒）。 */
#define XUPGRADE_HTTP_SETTLE_MS 5000
/** @brief 文件哈希读块大小（字节）。 */
#define XUPGRADE_HASH_BUF_SIZE (64u * 1024u)
/** @brief 同一版本回滚计数上限（超过即停用自动升级，§8.3）。 */
#define XUPGRADE_ROLLBACK_LIMIT 2u
/** @brief GC 版本枚举缓冲条数上限。 */
#define XUPGRADE_VERSION_LIST_MAX 64
/** @brief .verify 清单条目上限（防异常包撑爆内存）。 */
#define XUPGRADE_VERIFY_LIST_MAX 256u
/** @brief update.log 保留尾部字节数。 */
#define XUPGRADE_LOG_TAIL_BYTES (32u * 1024u)
/** @brief 解包单条目解压前后大小上限（防御异常包，512MiB）。 */
#define XUPGRADE_ENTRY_MAX_RAW (512ull * 1024ull * 1024ull)
/** @brief ".part" 后缀长度（不含 NUL）。 */
#define XUPGRADE_PART_SUFFIX_LEN 5

/** @brief 包文件名 → ".part" 下载名（含越界守卫；下载/校验/staging 三处
 *         消费同口径，2026-10-08 评审收敛——原三处手拼仅下载处有守卫）。 */
static bool xup_part_name(const char* file, char* out, size_t cap)
{
    size_t len = 0;
    if (!file || !out || cap <= XUPGRADE_PART_SUFFIX_LEN) return false;
    while (file[len] != '\0') ++len;
    if (len + XUPGRADE_PART_SUFFIX_LEN >= cap) return false;
    XMemcpy(out, file, len);
    XMemcpy(out + len, ".part", XUPGRADE_PART_SUFFIX_LEN + 1);
    return true;
}

/** @brief 工作线程请求类型。 */
typedef enum XUpgradeRequest {
    XUpgradeRequest_None = 0,       /**< 无请求。 */
    XUpgradeRequest_Check = 1,      /**< 手动检查。 */
    XUpgradeRequest_Upgrade = 2     /**< 启动下载→staging 流水。 */
} XUpgradeRequest;

/* ========================================================================== */
/*                            引擎对象结构（实现私有）                          */
/* ========================================================================== */

/**
 * @brief 升级目标（一次检查命中的待升级组件，内存态，不持久化）。
 */
typedef struct XUpgradeTarget {
    char m_componentId[XUPGRADE_COMPONENT_ID_MAX]; /**< 组件 id。 */
    char m_version[XUPGRADE_VERSION_MAX];          /**< 目标版本串。 */
    char m_file[XUPGRADE_FILENAME_MAX];            /**< 包文件名（预留 ".part" 后缀空间）。 */
    char m_sha256[XUPGRADE_SHA256_HEX_MAX];        /**< 包文件 SHA-256 小写十六进制。 */
    uint64_t m_size;                               /**< 包文件总字节数。 */
    bool m_osExact;                                /**< 条目来自精确 os 匹配（同 id 多条目时精确优先于 "any"，findComponent 同口径）。 */
    XUpgradeManifestRequires m_requires;           /**< manifest 声明的依赖约束（检查时快照）：下载依赖序排序与 staging 完成后配套校验共用。 */
} XUpgradeTarget;

/**
 * @brief 升级引擎对象（结构体定义于实现文件，模式同 XMySqlDriver.c:54-58）。
 */
struct XUpgradeEngine {
    XObject m_class;             /**< 第一个成员，由 XObject 管理；禁止手工修改。 */
    XMutex* m_mutex;             /**< 保护下列全部字段（XLock_NonRecursive）。 */
    XMutex* m_saveMutex;         /**< 落盘串行化锁：state.json/update.log 等原子
                                      写盘段互斥（进程内 worker 分块确认与用户
                                      线程 skipVersion 等多写者并发时，同一目标
                                      文件的临时写-替换序列不再交错；不与
                                      m_mutex 嵌套获取）。 */
    XString* m_manifestUrl;      /**< 渠道替换后的 manifest URL（UTF-8）。 */
    XString* m_baseUrl;          /**< 包文件基础目录 URL（manifest URL 去掉文件名）。 */
    XString* m_channel;          /**< 更新渠道名。 */
    XString* m_targetOs;         /**< 本机目标平台标识。 */
    XString* m_installRoot;      /**< 安装根目录完整路径。 */
    uint32_t m_checkIntervalHours; /**< 自动检查周期（小时）；0=不按周期。 */
    bool m_autoCheck;            /**< 是否允许自动检查。 */
    bool m_started;              /**< start 成功后为 true。 */
    bool m_busy;                 /**< 工作线程流水执行中（checkNow/startUpgrade 据此拒忙）。 */
    bool m_stop;                 /**< 工作线程停机标志。 */
    bool m_manualCheck;          /**< 最近一次检查是否手动发起（事件文案用）。 */
    int m_request;               /**< 待处理请求（XUpgradeRequest）。 */
    int64_t m_lastCheckMs;       /**< 最近一次检查发起时刻（单调毫秒）。 */
    XUpgradeListener m_listener; /**< 事件观察者；可为 NULL。 */
    void* m_listenerData;        /**< 观察者上下文；借用。 */
    XUpgradeStateFile m_state;   /**< state.json 内存镜像（保存前重算 CRC）。 */
    XUpgradeManifest m_manifest; /**< 最近一次解析成功的 manifest（staging 数据源）。 */
    bool m_hasManifest;          /**< m_manifest 是否有效。 */
    XUpgradeTarget m_targets[XUPGRADE_COMPONENT_MAX]; /**< 命中的升级目标表。 */
    uint32_t m_targetCount;      /**< 有效目标数。 */
    XUpgradeCatalog m_catalog;   /**< 多版本目录缓存（schema 2 检查时填充，§4 扩展）。 */
    bool m_hasCatalog;           /**< m_catalog 是否有效。 */
    char m_pinnedVersion[XUPGRADE_VERSION_MAX]; /**< 显式安装目标版本（installVersion 设置；空=无）。 */
    XUpgradePolicy m_policy;     /**< 最近一次检查的生效策略（§10 组合判定收口；Optional=未判定/未检查）。 */
    XUpgradeGrayDecision m_grayDecision; /**< 最近一次检查的灰度判定（§11；NotEvaluated=未判定，XUpgrade_grayDecision 查询值）。 */
    XUpgradeApplyWindow m_applyWindow;   /**< silent 维护窗口配置副本（init m_applyWindow/XUpgrade_setApplyWindow，§10；引擎不持久化）。 */
    uint32_t m_forcedGraceHoursOverride; /**< forced 宽限期本地覆盖值（小时；不持久化，产品设置层每次启动重设）。 */
    bool m_hasForcedGraceOverride;       /**< 是否存在宽限期本地覆盖（false=跟随 manifest 的 forced_grace_hours）。 */
    bool m_readyAutoApplyDone;           /**< 本次 Ready 周期已发过自动 apply（forced/silent 窗口防周期重发；新检查周期复位）。 */
    XUpgradeStore* m_store;      /**< 存储后端（目录后端实例）。 */
    XThread* m_thread;           /**< 后台工作线程（func 模式）。 */
    XVarList* m_threadArgs;      /**< 线程参数块（engine 指针）。 */
};

/* ========================================================================== */
/*                        前向声明（class_init 需要）                          */
/* ========================================================================== */

static void VXUpgradeEngine_deinit(XUpgradeEngine* e);
static bool xup_stop_requested(XUpgradeEngine* e);
static XString* xup_package_url(XUpgradeEngine* e, const char* fileName);

/* ========================================================================== */
/*              策略引擎 TU 契约（XUpgradeGray.c 定义，签名以其为准）           */
/* ========================================================================== */
/* 模块内部契约：XUpgradeGray.c 承载 §10 策略组合/§11 灰度判定纯逻辑，无独立
 * 内部头；引擎侧以本前置声明消费（链接期校验签名一致）。 */

bool XUpgradeGray_manifestHasGray(const XUpgradeManifest* manifest);
XUpgradeGrayDecision XUpgradeGray_evaluate(const XUpgradeManifest* manifest);
bool XUpgradeGray_decisionPass(XUpgradeGrayDecision decision);
bool XUpgradeGray_forcedAnchored(const XUpgradeManifest* manifest, const char* localVersion);
uint32_t XUpgradeGray_resolveGraceHours(bool hasOverride, uint32_t overrideHours,
                                        bool hasManifest, const XUpgradeManifest* manifest);
bool XUpgradeGray_windowContains(const XUpgradeApplyWindow* window, int hour, int minute);

/* ========================================================================== */
/*                          基础工具（char/格式化/哈希）                        */
/* ========================================================================== */

/* 基础小工具（str_eq/str_copy/str_len/version_valid/trim/derive_root/
 * sha256_bytes）已收敛至 xupgrade_shared.h（xups_* 前缀），本 TU 直接消费。 */

/** @brief 自实现 atoll（不引 stdlib 转换函数口径）。 */
static long long xup_atoll(const char* s)
{
    long long v = 0;
    bool neg = false;
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') ++s;
    if (*s == '-') { neg = true; ++s; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); ++s; }
    return neg ? -v : v;
}

/** @brief 格式化到定长缓冲（变参转发宏，XString_create_fmt_utf8 包装）。 */
#define xup_fmt(dst, cap, ...)                                                  \
    do {                                                                        \
        XString* xup_fmt_s_ = XString_create_fmt_utf8(__VA_ARGS__);             \
        if (xup_fmt_s_) {                                                       \
            xups_str_copy((dst), (cap), XString_toUtf8(xup_fmt_s_));             \
            XClassDelete(xup_fmt_s_);                                           \
        } else {                                                                \
            (dst)[0] = '\0';                                                    \
        }                                                                       \
    } while (0)

/** @brief 单个十六进制字符转数值；非法返回 -1。 */
static int xup_hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** @brief 二进制转小写十六进制；out 容量至少 2*len+1。 */
static void xup_bin2hex(const uint8_t* bin, size_t len, char* out)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    if (!bin || !out) return;
    for (i = 0; i < len; ++i) {
        out[i * 2] = digits[bin[i] >> 4];
        out[i * 2 + 1] = digits[bin[i] & 0x0f];
    }
    out[len * 2] = '\0';
}

/** @brief 十六进制串与二进制摘要比较（大小写不敏感）。 */
static bool xup_hex_eq_digest(const char* hex, const uint8_t* digest, size_t len)
{
    size_t i;
    if (!hex || !digest) return false;
    for (i = 0; i < len; ++i) {
        int hi = xup_hex_digit(hex[i * 2]);
        int lo = xup_hex_digit(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        if ((uint8_t)((hi << 4) | lo) != digest[i]) return false;
    }
    return hex[len * 2] == '\0';
}

/** @brief 流式计算整文件 SHA-256；heartbeat 非空时长读期间刷新引擎锁心跳。 */
static bool xup_sha256_file(XString* path, uint8_t out[XUPGRADE_SHA256_BIN_SIZE],
                            XUpgradeEngine* heartbeat)
{
    XFile* file;
    XCryptographicHash hash;
    uint8_t* buf;
    bool ok = false;
    if (!path || !out) return false;
    file = XFile_create_2(path);
    if (!file) return false;
    if (!XFile_open_2(file, XIODevice_ReadOnly, 0)) {
        XClassDelete((XClass*)file);
        return false;
    }
    buf = (uint8_t*)XMalloc_System(XUPGRADE_HASH_BUF_SIZE);
    if (!buf) {
        XIODevice_close_base((XIODevice*)file);
        XClassDelete((XClass*)file);
        return false;
    }
    XCryptographicHash_init(&hash, XCryptographicHash_Sha256);
    for (;;) {
        int64_t n = XIODevice_read_1((XIODevice*)file, (char*)buf, (int64_t)XUPGRADE_HASH_BUF_SIZE);
        if (n < 0) break;
        if (n > 0)
            XCryptographicHash_addData_2(&hash, XByteArrayView_create_data(buf, n));
        if (heartbeat) xup_lock_heartbeat(heartbeat);
        if (n == 0) { ok = true; break; }
    }
    if (ok) {
        XByteArrayView result = XCryptographicHash_resultView(&hash);
        if (XByteArrayView_size(&result) == XUPGRADE_SHA256_BIN_SIZE) {
            XMemcpy(out, XByteArrayView_data(&result), XUPGRADE_SHA256_BIN_SIZE);
        } else {
            ok = false;
        }
    }
    XCryptographicHash_deinit(&hash);
    XFree_System(buf);
    XIODevice_close_base((XIODevice*)file);
    XClassDelete((XClass*)file);
    return ok;
}

/* ========================================================================== */
/*                              路径与文件小工具                                */
/* ========================================================================== */

/** @brief 拼路径：root[/a][/b]；返回新 XString，失败返回 NULL。 */
static XString* xup_path3(const XString* root, const char* a, const char* b)
{
    XString* path;
    XString* rootCopy;
    if (!root) return NULL;
    path = XString_create_utf8("");
    if (!path) return NULL;
    rootCopy = XString_create_copy(root);
    if (!rootCopy) { XClassDelete(path); return NULL; }
    if (!XString_append(path, rootCopy)) { XClassDelete(path); XClassDelete(rootCopy); return NULL; }
    XClassDelete(rootCopy);
    if (a && a[0] != '\0') {
        if (!XString_append_utf8(path, "/") || !XString_append_utf8(path, a)) { XClassDelete(path); return NULL; }
    }
    if (b && b[0] != '\0') {
        if (!XString_append_utf8(path, "/") || !XString_append_utf8(path, b)) { XClassDelete(path); return NULL; }
    }
    return path;
}

/** @brief 取引擎 update 工作区路径（加锁读根；新 XString）。 */
static XString* xup_update_dir(XUpgradeEngine* e)
{
    XString* root;
    XString* dir;
    if (!e) return NULL;
    XMutex_lock(e->m_mutex);
    root = e->m_installRoot ? XString_create_copy(e->m_installRoot) : NULL;
    XMutex_unlock(e->m_mutex);
    if (!root) return NULL;
    dir = xup_path3(root, XUPGRADE_DIR_UPDATE, NULL);
    XClassDelete(root);
    return dir;
}

/** @brief 加锁拷贝安装根；无根返回 NULL。 */
static XString* xup_root_copy(XUpgradeEngine* e)
{
    XString* root;
    if (!e) return NULL;
    XMutex_lock(e->m_mutex);
    root = e->m_installRoot ? XString_create_copy(e->m_installRoot) : NULL;
    XMutex_unlock(e->m_mutex);
    return root;
}

/** @brief 递归创建目录。 */
static bool xup_make_dirs(const XString* path)
{
    XDir* dir;
    bool ok;
    XString* dot;
    if (!path) return false;
    dir = XDir_create_2(path);
    if (!dir) return false;
    dot = XString_create_utf8(".");
    ok = dot && XDir_mkpath(dir, dot);
    if (dot) XClassDelete(dot);
    XClassDelete((XClass*)dir);
    return ok;
}

/** @brief 整读文件；成功时 *out 为新 XByteArray（调用者释放）。 */
static bool xup_read_file(XString* path, XByteArray** out)
{
    XFile* file;
    XByteArray* bytes;
    if (!path || !out) return false;
    *out = NULL;
    file = XFile_create_2(path);
    if (!file) return false;
    if (!XFile_open_2(file, XIODevice_ReadOnly, 0)) {
        XClassDelete((XClass*)file);
        return false;
    }
    bytes = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClassDelete((XClass*)file);
    if (!bytes) return false;
    *out = bytes;
    return true;
}

/** @brief 原子写整文件（XSaveFile 临时替换，崩溃不变式 2；commit 走
 *         XDeviceFile_renameReplace 替换语义，目标已存在即原子覆盖）。 */
static bool xup_write_file_atomic(XString* path, const char* data, size_t len)
{
    XSaveFile* save;
    bool ok;
    if (!path || (!data && len > 0)) return false;
    save = XSaveFile_create_2(path);
    if (!save) return false;
    ok = XIODevice_open_base((XIODevice*)save, XIODevice_WriteOnly);
    if (ok && len > 0)
        ok = XIODevice_write_1((XIODevice*)save, data, (int64_t)len) == (int64_t)len;
    if (ok) ok = XSaveFile_commit(save);
    else XSaveFile_cancelWriting(save);
    XClassDelete((XClass*)save);
    return ok;
}

/** @brief 删除文件（不存在视作成功）。 */
static bool xup_remove_file(const XString* path)
{
    XString* s;
    if (!path) return false;
    s = XString_create_copy(path);
    if (!s) return false;
    (void)XDir_removePath_static(s);
    XClassDelete(s);
    return true;
}

/** @brief 递归删除目录。 */
static void xup_remove_dir_recursively(const XString* path)
{
    XDir* dir;
    if (!path) return;
    dir = XDir_create_2(path);
    if (!dir) return;
    (void)XDir_removeRecursively(dir);
    XClassDelete((XClass*)dir);
}

/* ========================================================================== */
/*                          日志（update/update.log）                          */
/* ========================================================================== */

/** @brief 追加一行升级日志（原子重写，超限保留尾部）。 */
static void xup_log(XUpgradeEngine* e, const char* message)
{
    XString* path;
    XByteArray* old = NULL;
    XString* line;
    size_t base = 0;
    if (!e || !message) return;
    path = xup_path3(e->m_installRoot, XUPGRADE_DIR_UPDATE, XUPGRADE_FILE_LOG);
    if (!path) return;
    line = XString_create_fmt_utf8("[%lld] %s",
                                   (long long)XDateTime_currentMSecsSinceEpoch(),
                                   message);
    if (!line) { XClassDelete(path); return; }
    /* 读-并-写全程持保存锁：日志是整文件重写，两个线程并发追加（worker 保存
     * 失败告警 vs 用户线程 skip 记录）会共用同一临时文件名互相截断。 */
    XMutex_lock(e->m_saveMutex);
    if (xup_read_file(path, &old) && old) {
        size_t oldSize = XByteArray_size_base(old);
        if (oldSize > XUPGRADE_LOG_TAIL_BYTES) base = oldSize - XUPGRADE_LOG_TAIL_BYTES;
    }
    {
        size_t lineLen = XString_toUtf8_length(line);
        size_t oldLen = old ? XByteArray_size_base(old) - base : 0;
        char* merged = (char*)XMalloc_System(oldLen + lineLen + 2);
        if (merged) {
            if (oldLen > 0) XMemcpy(merged, XByteArray_constData(old) + base, oldLen);
            if (lineLen > 0) XMemcpy(merged + oldLen, XString_toUtf8(line), lineLen);
            merged[oldLen + lineLen] = '\n';
            merged[oldLen + lineLen + 1] = '\0';
            (void)xup_write_file_atomic(path, merged, oldLen + lineLen + 1);
            XFree_System(merged);
        }
    }
    XMutex_unlock(e->m_saveMutex);
    if (old) XClassDelete(old);
    XClassDelete(line);
    XClassDelete(path);
}

/** @brief 格式化并追加一行升级日志（变参转发宏）。 */
#define xup_logf(e, ...)                                                        \
    do {                                                                        \
        XString* xup_logf_s_ = XString_create_fmt_utf8(__VA_ARGS__);            \
        if (xup_logf_s_) {                                                      \
            xup_log((e), XString_toUtf8(xup_logf_s_));                          \
            XClassDelete(xup_logf_s_);                                          \
        }                                                                       \
    } while (0)

/* ========================================================================== */
/*                          state.json 序列化/反序列化                          */
/* ========================================================================== */

/**
 * @brief 将状态结构序列化为 JSON 文档（不含 crc32 字段）。
 * @details 全部使用根对象扁平键（c<N>/s<N>/dl 前缀），键序固定，保证
 *          save 与 load 重建的字节序列一致（CRC 校验的前提）；不引入嵌套
 *          数组以规避 XJsonArray 元素所有权歧义。
 */
static XJsonDocument* xup_state_to_json(const XUpgradeStateFile* state);

/** @brief 将状态结构序列化为 JSON 根对象（不含 crc32；键序固定）。 */
static XJsonObject* xup_state_to_root(const XUpgradeStateFile* state)
{
    XJsonObject* root;
    char key[48];
    char hex[XUPGRADE_SHA256_HEX_MAX];
    uint32_t i;
    if (!state) return NULL;
    root = XJsonObject_create();
    if (!root) return NULL;
    (void)XJsonObject_insert_keyUtf8_int(root, "schema", (int64_t)state->m_schema);
    (void)XJsonObject_insert_keyUtf8_int(root, "state", (int64_t)state->m_state);
    (void)XJsonObject_insert_keyUtf8_int(root, "compCount", (int64_t)state->m_componentCount);
    for (i = 0; i < state->m_componentCount && i < XUPGRADE_STATE_COMPONENT_MAX; ++i) {
        const XUpgradeStateComponent* c = &state->m_components[i];
        xup_fmt(key, sizeof(key), "c%uid", (unsigned)i);
        (void)XJsonObject_insert_keyUtf8_utf8(root, key, c->m_id);
        xup_fmt(key, sizeof(key), "c%uactive", (unsigned)i);
        (void)XJsonObject_insert_keyUtf8_utf8(root, key, c->m_activeVersion);
        xup_fmt(key, sizeof(key), "c%ustagedVersion", (unsigned)i);
        (void)XJsonObject_insert_keyUtf8_utf8(root, key, c->m_stagedVersion);
        xup_fmt(key, sizeof(key), "c%umax", (unsigned)i);
        (void)XJsonObject_insert_keyUtf8_utf8(root, key, c->m_maxVersion);
        xup_fmt(key, sizeof(key), "c%urollback", (unsigned)i);
        (void)XJsonObject_insert_keyUtf8_int(root, key, (int64_t)c->m_rollbackCount);
        xup_fmt(key, sizeof(key), "c%ustaged", (unsigned)i);
        (void)XJsonObject_insert_keyUtf8_bool(root, key, c->m_staged);
    }
    (void)XJsonObject_insert_keyUtf8_int(root, "skipCount", (int64_t)state->m_skipCount);
    for (i = 0; i < state->m_skipCount && i < XUPGRADE_STATE_SKIP_MAX; ++i) {
        xup_fmt(key, sizeof(key), "s%ucomponent", (unsigned)i);
        (void)XJsonObject_insert_keyUtf8_utf8(root, key, state->m_skips[i].m_componentId);
        xup_fmt(key, sizeof(key), "s%uversion", (unsigned)i);
        (void)XJsonObject_insert_keyUtf8_utf8(root, key, state->m_skips[i].m_version);
    }
    (void)XJsonObject_insert_keyUtf8_bool(root, "dlActive", state->m_download.m_active);
    (void)XJsonObject_insert_keyUtf8_utf8(root, "dlComponent", state->m_download.m_componentId);
    (void)XJsonObject_insert_keyUtf8_utf8(root, "dlFile", state->m_download.m_fileName);
    (void)XJsonObject_insert_keyUtf8_int(root, "dlOffset", (int64_t)state->m_download.m_offset);
    xup_bin2hex(state->m_download.m_chunkSha256, XUPGRADE_SHA256_BIN_SIZE, hex);
    (void)XJsonObject_insert_keyUtf8_utf8(root, "dlChunkSha256", hex);
    return root;
}

/** @brief 包装 xup_state_to_root 为 JSON 文档。 */
static XJsonDocument* xup_state_to_json(const XUpgradeStateFile* state)
{
    XJsonObject* root = xup_state_to_root(state);
    XJsonDocument* doc;
    if (!root) return NULL;
    doc = XJsonDocument_create_object(root); /* 深拷贝，root 归调用方释放 */
    XClassDelete(root);
    return doc;
}

/** @brief 从 JSON 根对象读取 UTF-8 字符串字段到定长缓冲；缺键清空。 */
static void xup_json_str(const XJsonObject* o, const char* key, char* dst, size_t cap)
{
    XJsonValue* v;
    if (!o || !key) { xups_str_copy(dst, cap, ""); return; }
    v = XJsonObject_value_keyUtf8((XJsonObject*)o, key); /* 深拷贝，调用者释放 */
    if (!v) { xups_str_copy(dst, cap, ""); return; }
    if (XJsonValue_type(v) == XJsonValue_String) {
        const XString* s = XJsonValue_toString(v);
        xups_str_copy(dst, cap, s ? XString_toUtf8(s) : "");
    } else {
        xups_str_copy(dst, cap, "");
    }
    XJsonValue_delete(v);
}

/** @brief 从 JSON 根对象读取整数字段；缺键返回 def。 */
static int64_t xup_json_int(const XJsonObject* o, const char* key, int64_t def)
{
    XJsonValue* v;
    int64_t r;
    if (!o || !key) return def;
    v = XJsonObject_value_keyUtf8((XJsonObject*)o, key);
    if (!v) return def;
    r = XJsonValue_toInt(v, def);
    XJsonValue_delete(v);
    return r;
}

/** @brief 从 JSON 根对象读取布尔字段；缺键返回 def。 */
static bool xup_json_bool(const XJsonObject* o, const char* key, bool def)
{
    XJsonValue* v;
    bool r;
    if (!o || !key) return def;
    v = XJsonObject_value_keyUtf8((XJsonObject*)o, key);
    if (!v) return def;
    r = XJsonValue_toBool(v, def);
    XJsonValue_delete(v);
    return r;
}

/** @brief 从扁平键重建组件表/跳过表（带越界与白名单防护）。 */
static void xup_json_fill_tables(const XJsonObject* root, XUpgradeStateFile* out)
{
    char key[48];
    int64_t i;
    int64_t compCount = xup_json_int(root, "compCount", 0);
    int64_t skipCount = xup_json_int(root, "skipCount", 0);
    if (compCount > (int64_t)XUPGRADE_STATE_COMPONENT_MAX) compCount = XUPGRADE_STATE_COMPONENT_MAX;
    if (skipCount > (int64_t)XUPGRADE_STATE_SKIP_MAX) skipCount = XUPGRADE_STATE_SKIP_MAX;
    for (i = 0; i < compCount; ++i) {
        XUpgradeStateComponent* c = &out->m_components[out->m_componentCount];
        int64_t rc;
        xup_fmt(key, sizeof(key), "c%uid", (unsigned)i);
        xup_json_str(root, key, c->m_id, XUPGRADE_COMPONENT_ID_MAX);
        if (c->m_id[0] == '\0') continue;
        xup_fmt(key, sizeof(key), "c%uactive", (unsigned)i);
        xup_json_str(root, key, c->m_activeVersion, XUPGRADE_VERSION_MAX);
        xup_fmt(key, sizeof(key), "c%ustagedVersion", (unsigned)i);
        xup_json_str(root, key, c->m_stagedVersion, XUPGRADE_VERSION_MAX);
        xup_fmt(key, sizeof(key), "c%umax", (unsigned)i);
        xup_json_str(root, key, c->m_maxVersion, XUPGRADE_VERSION_MAX);
        xup_fmt(key, sizeof(key), "c%urollback", (unsigned)i);
        rc = xup_json_int(root, key, 0);
        c->m_rollbackCount = rc > 0 ? (uint32_t)rc : 0u;
        xup_fmt(key, sizeof(key), "c%ustaged", (unsigned)i);
        c->m_staged = xup_json_bool(root, key, false);
        ++out->m_componentCount;
    }
    for (i = 0; i < skipCount; ++i) {
        XUpgradeStateSkip* s = &out->m_skips[out->m_skipCount];
        xup_fmt(key, sizeof(key), "s%ucomponent", (unsigned)i);
        xup_json_str(root, key, s->m_componentId, XUPGRADE_COMPONENT_ID_MAX);
        xup_fmt(key, sizeof(key), "s%uversion", (unsigned)i);
        xup_json_str(root, key, s->m_version, XUPGRADE_VERSION_MAX);
        if (s->m_componentId[0] == '\0' || s->m_version[0] == '\0') continue;
        ++out->m_skipCount;
    }
}

/** @brief 从扁平键重建下载游标。 */
static void xup_json_fill_download(const XJsonObject* root, XUpgradeStateFile* out)
{
    XUpgradeStateDownload* d = &out->m_download;
    const char* hex;
    int64_t i;
    int64_t off;
    d->m_active = xup_json_bool(root, "dlActive", false);
    xup_json_str(root, "dlComponent", d->m_componentId, XUPGRADE_COMPONENT_ID_MAX);
    xup_json_str(root, "dlFile", d->m_fileName, XUPGRADE_FILENAME_MAX);
    off = xup_json_int(root, "dlOffset", 0);
    d->m_offset = off > 0 ? (uint64_t)off : 0ull;
    XMemset(d->m_chunkSha256, 0, XUPGRADE_SHA256_BIN_SIZE);
    {
        XJsonValue* hv = XJsonObject_value_keyUtf8((XJsonObject*)root, "dlChunkSha256");
        if (hv) {
            if (XJsonValue_type(hv) == XJsonValue_String) {
                const XString* hs = XJsonValue_toString(hv);
                hex = hs ? XString_toUtf8(hs) : NULL;
                for (i = 0; hex && i < XUPGRADE_SHA256_BIN_SIZE; ++i) {
                    int hi = xup_hex_digit(hex[i * 2]);
                    int lo = xup_hex_digit(hex[i * 2 + 1]);
                    if (hi < 0 || lo < 0) break;
                    d->m_chunkSha256[i] = (uint8_t)((hi << 4) | lo);
                }
            }
            XJsonValue_delete(hv);
        }
    }
}

void XUpgradeStateFile_clear(XUpgradeStateFile* state)
{
    if (!state) return;
    XMemset(state, 0, sizeof(*state));
    state->m_schema = XUPGRADE_STATE_SCHEMA;
    state->m_state = XUpgradeState_Idle;
}

bool XUpgradeStateFile_load(XUpgradeStateFile* out, const char* updateDirUtf8)
{
    XString* dir;
    XString* path;
    XByteArray* bytes = NULL;
    XJsonDocument* doc;
    XJsonObject* root;
    int64_t storedCrc;
    XJsonDocument* verifyDoc;
    XByteArray* verifyBytes;
    uint32_t crc2;
    bool ok = false;
    if (!out || !updateDirUtf8 || updateDirUtf8[0] == '\0') return false;
    XUpgradeStateFile_clear(out);
    dir = XString_create_utf8(updateDirUtf8);
    if (!dir) return false;
    path = xup_path3(dir, XUPGRADE_FILE_STATE, NULL);
    XClassDelete(dir);
    if (!path) return false;
    if (!xup_read_file(path, &bytes) || !bytes) { XClassDelete(path); return false; }
    XClassDelete(path);
    doc = XJsonDocument_fromJson(bytes);
    XClassDelete(bytes);
    if (!doc) return false;
    root = XJsonDocument_object(doc); /* 借用指针，归 doc 所有 */
    if (!root || xup_json_int(root, "schema", -1) != (int64_t)XUPGRADE_STATE_SCHEMA) {
        XClassDelete(doc);
        return false;
    }
    storedCrc = xup_json_int(root, "crc32", -1);
    {
        int64_t st = xup_json_int(root, "state", (int64_t)XUpgradeState_Idle);
        if (st < (int64_t)XUpgradeState_Idle || st > (int64_t)XUpgradeState_DownloadFailed)
            st = (int64_t)XUpgradeState_Idle;
        out->m_state = (XUpgradeState)st;
    }
    xup_json_fill_tables(root, out);
    xup_json_fill_download(root, out);
    XClassDelete(doc);
    /* CRC 校验：按固定键序重建（不含 crc32）再比对，防篡改/截断。 */
    verifyDoc = xup_state_to_json(out);
    if (!verifyDoc) return false;
    verifyBytes = XJsonDocument_toJson(verifyDoc, XJsonDocument_Compact);
    XClassDelete(verifyDoc);
    if (!verifyBytes) return false;
    crc2 = XCrc32_calculate(XCrc32_Algorithm_IsoHdlc,
                            XByteArray_constData(verifyBytes),
                            XByteArray_size_base(verifyBytes));
    XClassDelete(verifyBytes);
    ok = (storedCrc >= 0) && (crc2 == (uint32_t)storedCrc);
    if (!ok) XUpgradeStateFile_clear(out);
    return ok;
}

bool XUpgradeStateFile_save(const XUpgradeStateFile* state, const char* updateDirUtf8)
{
    XUpgradeStateFile copy;
    XJsonObject* root;
    XJsonDocument* doc;
    XByteArray* bytes;
    uint32_t crc;
    XJsonValue* crcValue;
    XByteArray* finalBytes;
    XString* dir;
    XString* path;
    bool ok;
    if (!state || !updateDirUtf8 || updateDirUtf8[0] == '\0') return false;
    XMemcpy(&copy, state, sizeof(copy));
    copy.m_crc32 = 0;
    root = xup_state_to_root(&copy);
    if (!root) return false;
    doc = XJsonDocument_create_object(root); /* 深拷贝，root 仍归本函数 */
    if (!doc) { XClassDelete(root); return false; }
    bytes = XJsonDocument_toJson(doc, XJsonDocument_Compact);
    XClassDelete(doc);
    if (!bytes) { XClassDelete(root); return false; }
    crc = XCrc32_calculate(XCrc32_Algorithm_IsoHdlc,
                           XByteArray_constData(bytes), XByteArray_size_base(bytes));
    XClassDelete(bytes);
    /* crc32 覆盖除自身外全部字段的序列化字节：先序列化无 crc 版本取摘要，
     * 再把 crc 写入根对象重新序列化落盘（load 侧按同序重建比对）。 */
    crcValue = XJsonValue_create_int((int64_t)crc);
    if (!crcValue) { XClassDelete(root); return false; }
    if (!XJsonObject_insert_keyUtf8_value_move(root, "crc32", crcValue)) {
        XJsonValue_delete(crcValue);
        XClassDelete(root);
        return false;
    }
    /* move 插入只搬走负载并把源置 Null（XJsonValue.c:152-159），堆上源壳
     * 仍归本函数——成功路径同样要释放（对齐本文件 insert_keyUtf8_double/
     * int 插入后 deinit 的口径），否则每次 save_state 落盘泄漏一个 16B
     * XJsonValue 壳（浸泡实测每轮 state.json 多次保存线性累积）。 */
    XJsonValue_delete(crcValue);
    doc = XJsonDocument_create_object(root);
    XClassDelete(root);
    if (!doc) return false;
    finalBytes = XJsonDocument_toJson(doc, XJsonDocument_Compact);
    XClassDelete(doc);
    if (!finalBytes) return false;
    dir = XString_create_utf8(updateDirUtf8);
    path = dir ? xup_path3(dir, XUPGRADE_FILE_STATE, NULL) : NULL;
    ok = false;
    if (path) {
        if (xup_make_dirs(dir))
            ok = xup_write_file_atomic(path,
                                       (const char*)XByteArray_constData(finalBytes),
                                       XByteArray_size_base(finalBytes));
        XClassDelete(path);
    }
    if (dir) XClassDelete(dir);
    XClassDelete(finalBytes);
    return ok;
}

/* ========================================================================== */
/*                          事件推送与状态迁移                                  */
/* ========================================================================== */

/**
 * @brief 事件投递：取观察者快照、UI 提示门控（XUPGRADE_UI_HINTS_ON）后回调。
 * @details 事件契约（XUpgrade.h）：m_type 为分支判断第一级；m_policy 仅
 *          UpdateAvailable 事件有意义（其余为 Optional），m_gray 仅
 *          UpdateAvailable 事件有意义（其余为 NotEvaluated）。五类 UI 提示
 *          语义事件（UpdateAvailable/ReadyNeedRestart/ReadyAutoApply/
 *          RolledBack/CheckFailed）经 uiHint=true 进入本门控。
 */
static void xup_event_dispatch(XUpgradeEngine* e, const XUpgradeEvent* event, bool uiHint)
{
    XUpgradeListener listener;
    void* data;
    if (!e || !event) return;
    XMutex_lock(e->m_mutex);
    listener = e->m_listener;
    data = e->m_listenerData;
    XMutex_unlock(e->m_mutex);
#if !XUPGRADE_UI_HINTS_ON
    if (uiHint) return;
#else
    (void)uiHint;
#endif
    if (listener) listener(data, event);
}

/** @brief 推送事件给观察者（核心：填充契约缺省字段后投递）。 */
static void xup_emit_evt(XUpgradeEngine* e, XUpgradeEventType type, XUpgradeState state,
                         XUpgradePhase phase, int percent, const char* component,
                         const char* version, const char* detail, bool uiHint)
{
    XUpgradeEvent event;
    if (!e) return;
    XMemset(&event, 0, sizeof(event));
    event.m_type = type;
    event.m_state = state;
    event.m_phase = phase;
    event.m_percent = percent;
    event.m_policy = XUpgradePolicy_Optional;            /* 契约缺省：非 UpdateAvailable 无意义 */
    event.m_gray = XUpgradeGrayDecision_NotEvaluated;    /* 契约缺省：非 UpdateAvailable 无意义 */
    xups_str_copy(event.m_component, sizeof(event.m_component), component ? component : "");
    xups_str_copy(event.m_version, sizeof(event.m_version), version ? version : "");
    xups_str_copy(event.m_detail, sizeof(event.m_detail), detail ? detail : "");
    xup_event_dispatch(e, &event, uiHint);
}

/**
 * @brief UpdateAvailable 语义事件：额外携带生效策略与灰度判定（§10/§11）。
 */
static void xup_emit_update_available(XUpgradeEngine* e, const char* component,
                                      const char* version, XUpgradePolicy policy,
                                      XUpgradeGrayDecision gray)
{
    XUpgradeEvent event;
    if (!e) return;
    XMemset(&event, 0, sizeof(event));
    event.m_type = XUpgradeEventType_UpdateAvailable;
    event.m_state = XUpgradeState_Checking;
    event.m_phase = XUpgradePhase_Download;
    event.m_percent = -1;
    event.m_policy = policy;
    event.m_gray = gray;
    xups_str_copy(event.m_component, sizeof(event.m_component), component ? component : "");
    xups_str_copy(event.m_version, sizeof(event.m_version), version ? version : "");
    xup_event_dispatch(e, &event, true);
}

/** @brief Progress 语义事件：携带阶段+百分比+组件（§12 四段进度条数据源）。
 *  @details 下载/校验/解包的百分比进度必须以 Progress 类型发射——UI 进度条
 *           只认本类型；StateChanged 承载状态行语义（2026-10-07 修复：原
 *           xup_emit 硬编码 StateChanged 导致进度条全程 0%）。 */
static void xup_emit_progress(XUpgradeEngine* e, XUpgradePhase phase, int percent,
                              const char* component, const char* version)
{
    XUpgradeEvent event;
    if (!e) return;
    XMemset(&event, 0, sizeof(event));
    event.m_type = XUpgradeEventType_Progress;
    /* 契约口径（XUpgrade.h）：m_policy 仅 UpdateAvailable 有意义，其余事件
     * 一律填 Optional——Forced=0，零初始化会误携带 Forced。 */
    event.m_policy = XUpgradePolicy_Optional;
    event.m_phase = phase;
    event.m_percent = percent;
    xups_str_copy(event.m_component, sizeof(event.m_component), component ? component : "");
    xups_str_copy(event.m_version, sizeof(event.m_version), version ? version : "");
    xup_event_dispatch(e, &event, true);
}

/** @brief 按状态观察事件（StateChanged 语义；保持既有调用形态不变）。 */
static void xup_emit(XUpgradeEngine* e, XUpgradeState state, XUpgradePhase phase,
                     int percent, const char* component, const char* version,
                     const char* detail, bool uiHint)
{
    xup_emit_evt(e, XUpgradeEventType_StateChanged, state, phase, percent,
                 component, version, detail, uiHint);
}

/** @brief 快照状态镜像（加锁）并以调用方目录持久化。 */
static bool xup_save_state_to(XUpgradeEngine* e, const XString* updateDir)
{
    XUpgradeStateFile snapshot;
    bool ok;
    if (!e || !updateDir) return false;
    /* 落盘段全程持保存锁，且快照在保存锁内取（2026-10-12 竞态根修）：
     * 进程内多写者（worker 分块确认/状态迁移与用户线程 skipVersion）并发
     * 保存时，旧实现"先取快照再进保存锁"会让晚进锁的写者把早进锁写者的
     * 新状态用旧快照覆盖回去（并发 skipVersion 丢 skip 记录）。现在
     * saveMutex 先行串行化写者，快照在锁内取——后落盘者快照必含此前全部
     * 已提交变更，写序=提交序。锁序纪律：saveMutex→m_mutex 单向（全库
     * 无 m_mutex→saveMutex 反序点，无 ABBA）。 */
    XMutex_lock(e->m_saveMutex);
    XMutex_lock(e->m_mutex);
    snapshot = e->m_state;
    XMutex_unlock(e->m_mutex);
    ok = XUpgradeStateFile_save(&snapshot, XString_toUtf8(updateDir));
    XMutex_unlock(e->m_saveMutex);
    return ok;
}

/** @brief 迁移状态并立即持久化（保存失败仅记日志，内存状态已更新）。 */
static void xup_set_state(XUpgradeEngine* e, XUpgradeState state)
{
    XString* dir;
    if (!e) return;
    XMutex_lock(e->m_mutex);
    e->m_state.m_state = state;
    XMutex_unlock(e->m_mutex);
    dir = xup_update_dir(e);
    if (dir) {
        if (!xup_save_state_to(e, dir)) xup_log(e, "state.json 保存失败");
        XClassDelete(dir);
    }
}

/** @brief 持久化当前状态镜像（字段修改后调用）。 */
static void xup_save_state(XUpgradeEngine* e)
{
    XString* dir;
    if (!e) return;
    dir = xup_update_dir(e);
    if (dir) {
        if (!xup_save_state_to(e, dir)) xup_log(e, "state.json 保存失败");
        XClassDelete(dir);
    }
}

/* ========================================================================== */
/*                          组件记录/跳过表工具                                 */
/* ========================================================================== */

/** @brief 按组件 id 查找状态记录；create 时末尾追加（表满返回 NULL）。 */
static XUpgradeStateComponent* xup_state_component(XUpgradeStateFile* state,
                                                   const char* componentId, bool create)
{
    uint32_t i;
    if (!state || !componentId || componentId[0] == '\0') return NULL;
    for (i = 0; i < state->m_componentCount && i < XUPGRADE_STATE_COMPONENT_MAX; ++i)
        if (xups_str_eq(state->m_components[i].m_id, componentId)) return &state->m_components[i];
    if (!create || state->m_componentCount >= XUPGRADE_STATE_COMPONENT_MAX) return NULL;
    {
        XUpgradeStateComponent* c = &state->m_components[state->m_componentCount++];
        XMemset(c, 0, sizeof(*c));
        xups_str_copy(c->m_id, XUPGRADE_COMPONENT_ID_MAX, componentId);
        return c;
    }
}

/** @brief (component,version) 是否在跳过表中。 */
static bool xup_skip_hit(const XUpgradeStateFile* state, const char* componentId, const char* version)
{
    uint32_t i;
    if (!state) return false;
    for (i = 0; i < state->m_skipCount && i < XUPGRADE_STATE_SKIP_MAX; ++i)
        if (xups_str_eq(state->m_skips[i].m_componentId, componentId) &&
            xups_str_eq(state->m_skips[i].m_version, version))
            return true;
    return false;
}

/** @brief 记录跳过表（去重；表满淘汰最旧）。返回是否发生变更。 */
static bool xup_skip_add(XUpgradeStateFile* state, const char* componentId, const char* version)
{
    uint32_t i;
    if (!state || !componentId || componentId[0] == '\0' || !version || version[0] == '\0')
        return false;
    if (xup_skip_hit(state, componentId, version)) return false;
    if (state->m_skipCount >= XUPGRADE_STATE_SKIP_MAX) {
        XMemmove(&state->m_skips[0], &state->m_skips[1],
                 sizeof(XUpgradeStateSkip) * (XUPGRADE_STATE_SKIP_MAX - 1));
        state->m_skipCount = XUPGRADE_STATE_SKIP_MAX - 1;
    }
    i = state->m_skipCount++;
    xups_str_copy(state->m_skips[i].m_componentId, XUPGRADE_COMPONENT_ID_MAX, componentId);
    xups_str_copy(state->m_skips[i].m_version, XUPGRADE_VERSION_MAX, version);
    return true;
}

/** @brief 清除指定 (component,version) 跳过记录。 */
static void xup_skip_remove(XUpgradeStateFile* state, const char* componentId, const char* version)
{
    uint32_t i;
    uint32_t w = 0;
    if (!state) return;
    for (i = 0; i < state->m_skipCount && i < XUPGRADE_STATE_SKIP_MAX; ++i) {
        if (xups_str_eq(state->m_skips[i].m_componentId, componentId) &&
            xups_str_eq(state->m_skips[i].m_version, version))
            continue;
        if (w != i) state->m_skips[w] = state->m_skips[i];
        ++w;
    }
    state->m_skipCount = w;
}

/** @brief 修剪不高于本地版本的跳过记录（下一版本自动重置，§10；每次至多一条）。 */
static void xup_skip_prune_one(XUpgradeStateFile* state, const char* componentId,
                               const char* localVersion)
{
    uint32_t i;
    if (!state || localVersion[0] == '\0') return;
    for (i = 0; i < state->m_skipCount && i < XUPGRADE_STATE_SKIP_MAX; ++i) {
        if (xups_str_eq(state->m_skips[i].m_componentId, componentId) &&
            XUpgradeManifest_compareVersions(state->m_skips[i].m_version, localVersion) <= 0) {
            xup_skip_remove(state, state->m_skips[i].m_componentId, state->m_skips[i].m_version);
            return;
        }
    }
}

/* ========================================================================== */
/*                          指针文件/版本目录工具                               */
/* ========================================================================== */

/** @brief 读 pointers/<comp>；成功且内容为合法版本串时写入 out。 */
static bool xup_read_pointer(XUpgradeEngine* e, const char* componentId, char* out, uint32_t cap)
{
    XString* root;
    XString* path;
    XByteArray* bytes = NULL;
    char buf[XUPGRADE_VERSION_MAX];
    bool ok = false;
    if (!e || !componentId || !out || cap == 0) return false;
    out[0] = '\0';
    root = xup_root_copy(e);
    if (!root) return false;
    path = xup_path3(root, XUPGRADE_DIR_POINTERS, componentId);
    XClassDelete(root);
    if (!path) return false;
    if (xup_read_file(path, &bytes) && bytes) {
        size_t n = XByteArray_size_base(bytes);
        if (n < sizeof(buf)) {
            XMemcpy(buf, XByteArray_constData(bytes), n);
            buf[n] = '\0';
            xups_trim(buf);
            if (xups_version_valid(buf)) {
                xups_str_copy(out, cap, buf);
                ok = true;
            }
        }
        XClassDelete(bytes);
    }
    XClassDelete(path);
    return ok;
}

/** @brief 版本目录是否完整（manifest.version 标记存在且内容吻合=完整）。 */
static bool xup_version_dir_complete(XUpgradeEngine* e, XString* root, const char* componentId, const char* version)
{
    XString* mark;
    XByteArray* bytes = NULL;
    char buf[XUPGRADE_VERSION_MAX];
    bool ok = false;
    if (!e || !root || !componentId || !version) return false;
    /* root 由调用方锁外预取（安装根 start 后只读）：本函数可能被持有
     * m_mutex 的路径调用（recover 尾部/staging 清理），内部再走
     * xup_root_copy 的非递归锁即同线程重锁自死锁（2026-10-11 检查更新
     * 卡死根因）。 */
    mark = xup_path3(root, XUPGRADE_DIR_VERSIONS, componentId);
    if (!mark) return false;
    if (!XString_append_utf8(mark, "/") || !XString_append_utf8(mark, version) ||
        !XString_append_utf8(mark, "/" XUPGRADE_FILE_VERSION_MARK)) {
        XClassDelete(mark);
        return false;
    }
    if (xup_read_file(mark, &bytes) && bytes) {
        size_t n = XByteArray_size_base(bytes);
        if (n < sizeof(buf)) {
            XMemcpy(buf, XByteArray_constData(bytes), n);
            buf[n] = '\0';
            xups_trim(buf);
            ok = xups_str_eq(buf, version);
        }
        XClassDelete(bytes);
    }
    XClassDelete(mark);
    return ok;
}

/* ========================================================================== */
/*                          引擎锁（update/lock，不变式 6）                     */
/* ========================================================================== */

/** @brief 写锁文件：pid + 心跳（单调毫秒，跨进程可比）。 */
static bool xup_lock_write(XUpgradeEngine* e)
{
    XString* dir;
    XString* path;
    char content[96];
    bool ok = false;
    if (!e) return false;
    dir = xup_update_dir(e);
    if (!dir) return false;
    path = xup_path3(dir, XUPGRADE_FILE_LOCK, NULL);
    if (path) {
        XString* line = XString_create_fmt_utf8("pid=%lld\nheartbeat=%lld",
                                                (long long)XSystem_pid(),
                                                (long long)XDateTime_currentMSecsSinceEpoch());
        if (line) {
            xups_str_copy(content, sizeof(content), XString_toUtf8(line));
            if (xup_make_dirs(dir))
                ok = xup_write_file_atomic(path, content, XString_toUtf8_length(line));
            XClassDelete(line);
        }
        XClassDelete(path);
    }
    XClassDelete(dir);
    return ok;
}

/** @brief 尝试接管引擎锁；本进程其他实例或心跳新鲜的实例占用时返回 false。 */
static bool xup_lock_acquire(XUpgradeEngine* e)
{
    XString* dir;
    XString* path;
    XByteArray* bytes = NULL;
    bool ok = true;
    if (!e) return false;
    dir = xup_update_dir(e);
    if (!dir) return false;
    path = xup_path3(dir, XUPGRADE_FILE_LOCK, NULL);
    if (path && xup_read_file(path, &bytes) && bytes) {
        char buf[128];
        long long pid = 0;
        long long heartbeat = 0;
        size_t n = XByteArray_size_base(bytes);
        if (n < sizeof(buf)) {
            const char* p;
            const char* h;
            XMemcpy(buf, XByteArray_constData(bytes), n);
            buf[n] = '\0';
            xups_trim(buf);
            p = XStrstr(buf, "pid=");
            h = XStrstr(buf, "heartbeat=");
            if (p) pid = xup_atoll(p + 4);
            if (h) heartbeat = xup_atoll(h + 10);
        }
        if (pid == (long long)XSystem_pid()) {
            ok = false; /* 本进程内已有实例持有 */
        } else if (heartbeat > 0 &&
                   XDateTime_currentMSecsSinceEpoch() - heartbeat < (int64_t)XUPGRADE_LOCK_STALE_MS &&
                   XSystem_processAlive(pid)) {
            /* 心跳新鲜且持有者存活=真占用。持有者已死（崩溃/被杀未清锁）时
             * 心跳文件残留 15s 内仍"新鲜"，纯 mtime 判活会误拒——按 PID 存活
             * 接管（2026-10-07：强关重开 15s 内"初始化失败(占用)"根因）。 */
            ok = false; /* 他进程心跳仍新鲜且持有者存活 */
        }
        XClassDelete(bytes);
    }
    if (ok) ok = xup_lock_write(e);
    if (path) XClassDelete(path);
    XClassDelete(dir);
    return ok;
}

/** @brief 移除锁文件（干净停机/启动失败回滚时调用）。 */
static void xup_lock_remove(XUpgradeEngine* e)
{
    XString* dir;
    XString* path;
    if (!e) return;
    dir = xup_update_dir(e);
    if (!dir) return;
    path = xup_path3(dir, XUPGRADE_FILE_LOCK, NULL);
    if (path) { (void)xup_remove_file(path); XClassDelete(path); }
    XClassDelete(dir);
}

/**
 * @brief 流水内锁心跳刷新（内部限频：距上次写满 HEARTBEAT_WRITE_MS 才落盘）。
 * @details 下载/校验/staging 联跑期间 worker 空闲循环被阻塞、其底部心跳停写，
 *          而他进程按"心跳距今 <15s"判占用——不在长事务内补心跳，不变式 6
 *          （防双实例）恰在最长窗口失效。各长循环（HTTP 等待/下载泵/校验/
 *          解包/staging）周期调用本函数；限频避免高频泵循环写盘放大。
 *          进程内静态节拍即可：同进程双实例本就被 pid 检查拒绝（不变式 6）。
 */
static void xup_lock_heartbeat(XUpgradeEngine* e)
{
    static int64_t s_lastWriteMs = 0;
    int64_t now;
    if (!e) return;
    now = XDateTime_currentMSecsSinceEpoch();
    if (now - s_lastWriteMs < (int64_t)XUPGRADE_LOCK_HEARTBEAT_WRITE_MS) return;
    s_lastWriteMs = now;
    (void)xup_lock_write(e);
}

/* ========================================================================== */
/*              引擎内 staged 指针切换（applyNow 受理后/recover 续做共用）        */
/* ========================================================================== */

#if !XUPGRADE_AB_ON
/**
 * @brief 引擎内直接完成 staged 指针切换（桌面目录后端；applyNow 与 recover 续做
 *        共用，动作顺序与 apply 桩 §8.1 一致）。
 * @details 调用方先把状态置 Applying 并落盘（"切换待续做"语义，staged 标记保留
 *          作续做依据），本函数对每条 staged 组件调 XUpgradeStore_activate_base
 *          原子写 pointers/<comp>；全部成功后清 staged 标记并置
 *          BootConfirming 落盘（下次启动自检完成确认/回滚）；任一 activate 失败
 *          回退 Ready 落盘返回 false（apply 桩可幂等续做/重试）。目录方案引擎内
 *          切换安全：版本目录互不覆盖，运行中进程仍执行旧目录；无 staged 组件时
 *          与桩同款收敛为 BootConfirming（无事发生口径）。A/B 构建不编入：槽
 *          切换须 bootloader 重启（§9.4），维持"受理后退出码 74 交桩"既有行为。
 * @param reason 日志前缀（"applyNow"/"恢复：Applying 中断续做切换"）。
 * @return 切换完成（含无事可做收敛）返回 true；activate 失败返回 false。
 */
static bool xup_apply_activate_staged(XUpgradeEngine* e, const char* reason)
{
    XUpgradeStateComponent staged[XUPGRADE_STATE_COMPONENT_MAX];
    uint32_t count = 0;
    uint32_t i;
    const char* errText = NULL;
    if (!e) return false;
    /* staged 组件快照（加锁）：切换期间标记不改写，落盘口径与桩一致
     * （Applying 已先行落盘，崩溃窗口内 staged 标记即续做依据）。 */
    XMutex_lock(e->m_mutex);
    for (i = 0; i < e->m_state.m_componentCount && i < XUPGRADE_STATE_COMPONENT_MAX; ++i) {
        if (e->m_state.m_components[i].m_staged &&
            xups_version_valid(e->m_state.m_components[i].m_stagedVersion))
            staged[count++] = e->m_state.m_components[i];
    }
    XMutex_unlock(e->m_mutex);
    if (!e->m_store) {
        xup_logf(e, "%s：存储后端未就绪，回退 Ready", reason ? reason : "apply");
        XMutex_lock(e->m_mutex);
        e->m_state.m_state = XUpgradeState_Ready;
        XMutex_unlock(e->m_mutex);
        xup_save_state(e);
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (XUpgradeStore_activate_base(e->m_store, staged[i].m_id,
                                        staged[i].m_stagedVersion, &errText)
            == XUpgradeError_Ok)
            continue;
        xup_logf(e, "%s：activate %s@%s 失败（%s），回退 Ready",
                 reason ? reason : "apply", staged[i].m_id,
                 staged[i].m_stagedVersion, errText ? errText : "存储后端错误");
        XMutex_lock(e->m_mutex);
        e->m_state.m_state = XUpgradeState_Ready;
        XMutex_unlock(e->m_mutex);
        xup_save_state(e);
        return false;
    }
    /* 全部成功：同步状态记账（active/max 随切换抬升）→ 清 staged 标记 →
     * BootConfirming（§8.1 步骤 3 同款，一次落盘）。缺此步则 state 的
     * active/max 停留旧值，重启后引擎把已装版本再次当成更新（2026-10-07
     * 实弹验收根因）。 */
    XMutex_lock(e->m_mutex);
    for (i = 0; i < e->m_state.m_componentCount && i < XUPGRADE_STATE_COMPONENT_MAX; ++i) {
        XUpgradeStateComponent* rec = &e->m_state.m_components[i];
        uint32_t k;
        for (k = 0; k < count; ++k) {
            if (!xups_str_eq(rec->m_id, staged[k].m_id)) continue;
            if (xups_version_valid(staged[k].m_stagedVersion))
                xups_str_copy(rec->m_activeVersion, sizeof(rec->m_activeVersion),
                             staged[k].m_stagedVersion);
            if (XUpgradeManifest_compareVersions(staged[k].m_stagedVersion,
                                                 rec->m_maxVersion) > 0)
                xups_str_copy(rec->m_maxVersion, sizeof(rec->m_maxVersion),
                             staged[k].m_stagedVersion);
            break;
        }
        rec->m_staged = false;
    }
    e->m_state.m_state = XUpgradeState_BootConfirming;
    XMutex_unlock(e->m_mutex);
    xup_save_state(e);
    xup_logf(e, "%s：staged 指针切换完成（BootConfirming，%u 条）",
             reason ? reason : "apply", (unsigned)count);
    return true;
}
#endif /* !XUPGRADE_AB_ON */

/* ========================================================================== */
/*                          断电恢复收敛（§16 矩阵 1-7）                        */
/* ========================================================================== */

/** @brief 清空 update/partial 目录内容（目录本身保留）。 */
static void xup_clear_partial(XUpgradeEngine* e)
{
    XString* root;
    XString* dir;
    XDir* d;
    XStringList* entries;
    int64_t i;
    if (!e) return;
    root = xup_root_copy(e);
    if (!root) return;
    dir = xup_path3(root, XUPGRADE_DIR_UPDATE "/" XUPGRADE_DIR_PARTIAL, NULL);
    XClassDelete(root);
    if (!dir) return;
    d = XDir_create_2(dir);
    if (d) {
        if (XDir_exists_1(d)) {
            entries = XDir_entryList_1(d, XDir_Files, XDir_NoSort);
            if (entries) {
                for (i = 0; i < (int64_t)XStringList_size_base(entries); ++i) {
                    XString* name = (XString*)XStringList_at_base(entries, i);
                    XString* full = name ? xup_path3(dir, XString_toUtf8(name), NULL) : NULL;
                    if (full) { (void)xup_remove_file(full); XClassDelete(full); }
                }
                XClassDelete(entries);
            }
        }
        XClassDelete((XClass*)d);
    }
    XClassDelete(dir);
}

/** @brief 删除组件下不完整的版本目录（无 manifest.version 标记且非 keep 版本）。 */
static void xup_remove_half_version_dirs(XUpgradeEngine* e, XString* root, const char* componentId,
                                         const char* keepVersion)
{
    XString* compDir;
    XDir* d;
    XStringList* entries;
    int64_t i;
    if (!e || !root || !componentId) return;
    /* root 由调用方锁外预取：调用方多处持 m_mutex 遍历组件表（recover
     * 尾部/Staging 恢复/staging 失败清理），内部 root_copy 的非递归锁
     * 同线程重入即死锁（同 version_dir_complete 口径，2026-10-11）。 */
    compDir = xup_path3(root, XUPGRADE_DIR_VERSIONS, componentId);
    if (!compDir) return;
    d = XDir_create_2(compDir);
    if (!d) { XClassDelete(compDir); return; }
    if (XDir_exists_1(d)) {
        entries = XDir_entryList_1(d, XDir_Dirs | XDir_NoDotAndDotDot, XDir_NoSort);
        if (entries) {
            for (i = 0; i < (int64_t)XStringList_size_base(entries); ++i) {
                XString* name = (XString*)XStringList_at_base(entries, i);
                const char* ver = name ? XString_toUtf8(name) : NULL;
                if (!ver || !xups_version_valid(ver)) continue;
                if (keepVersion && keepVersion[0] != '\0' && xups_str_eq(ver, keepVersion)) continue;
                if (xup_version_dir_complete(e, root, componentId, ver)) continue;
                {
                    XString* full = xup_path3(compDir, ver, NULL);
                    if (full) { xup_remove_dir_recursively(full); XClassDelete(full); }
                }
            }
            XClassDelete(entries);
        }
    }
    XClassDelete((XClass*)d);
    XClassDelete(compDir);
}

/** @brief 启动时断电恢复收敛（§16 矩阵 1-7；BootConfirming 留给 selfCheck）。 */
static void xup_recover(XUpgradeEngine* e)
{
    XUpgradeState st;
    uint32_t i;
    XMutex_lock(e->m_mutex);
    st = e->m_state.m_state;
    XMutex_unlock(e->m_mutex);
    if (st == XUpgradeState_Verifying) {
        /* 矩阵 2：整包校验中断 → partial 作废重下。 */
        xup_clear_partial(e);
        XMutex_lock(e->m_mutex);
        XMemset(&e->m_state.m_download, 0, sizeof(e->m_state.m_download));
        e->m_state.m_state = XUpgradeState_Idle;
        XMutex_unlock(e->m_mutex);
        xup_save_state(e);
        xup_log(e, "恢复：校验中断，partial 已作废（矩阵2）");
    } else if (st == XUpgradeState_Staging) {
        /* 矩阵 3：解包中断 → 半成品版本目录 + partial 清除后重 stage。 */
        xup_clear_partial(e);
        {
            /* root 锁外预取（remove_half 内部不再取锁，防同线程重锁自死锁）。 */
            XString* root = xup_root_copy(e);
            XMutex_lock(e->m_mutex);
            for (i = 0; i < e->m_state.m_componentCount && i < XUPGRADE_STATE_COMPONENT_MAX; ++i)
                xup_remove_half_version_dirs(e, root, e->m_state.m_components[i].m_id,
                                             e->m_state.m_components[i].m_activeVersion);
            XMemset(&e->m_state.m_download, 0, sizeof(e->m_state.m_download));
            e->m_state.m_state = XUpgradeState_Idle;
            XMutex_unlock(e->m_mutex);
            if (root) XClassDelete(root);
        }
        xup_save_state(e);
        xup_log(e, "恢复：staging 中断，半成品已清理（矩阵3）");
    } else if (st == XUpgradeState_Downloading) {
        /* 矩阵 1：保留 partial 与续传游标，状态回 Idle 等待用户。 */
        XMutex_lock(e->m_mutex);
        e->m_state.m_state = XUpgradeState_Idle;
        XMutex_unlock(e->m_mutex);
        xup_save_state(e);
        xup_log(e, "恢复：下载中断，partial 保留可续传（矩阵1）");
    } else if (st == XUpgradeState_Applying) {
#if XUPGRADE_AB_ON
        /* 矩阵 5/7：回退 Ready，apply 桩可幂等续做（A/B 构建维持既有行为：
         * 槽切换须 bootloader 重启，引擎内不续做）。 */
        XMutex_lock(e->m_mutex);
        e->m_state.m_state = XUpgradeState_Ready;
        XMutex_unlock(e->m_mutex);
        xup_save_state(e);
        xup_log(e, "恢复：Applying 中断，回退 Ready（矩阵5/7）");
#else
        /* 矩阵 5/7（桌面目录后端）：上次引擎内切换未完成的残留——按 applyNow
         * 同款逻辑续做 staged 指针切换（staged 标记保留作续做依据，全部成功
         * 才清）→ BootConfirming 落盘；activate 失败回退 Ready 落盘（本函数内
         * 已记日志），apply 桩可幂等续做。 */
        (void)xup_apply_activate_staged(e, "恢复：Applying 中断续做切换");
#endif
    } else if (st == XUpgradeState_Ready) {
        xup_log(e, "恢复：Ready 态保持，等待 apply（矩阵4）");
    }
    /* 通用兜底：任意状态下清理孤儿半成品目录。 */
    {
        /* root 锁外预取（remove_half 内部不再取锁，防同线程重锁自死锁：
         * 2026-10-11 检查更新卡死根因——持 m_mutex 调 root_copy 非递归
         * 锁重入，init 永不返回，UI 整体冻结）。 */
        XString* root = xup_root_copy(e);
        XMutex_lock(e->m_mutex);
        for (i = 0; i < e->m_state.m_componentCount && i < XUPGRADE_STATE_COMPONENT_MAX; ++i)
            xup_remove_half_version_dirs(e, root, e->m_state.m_components[i].m_id,
                                         e->m_state.m_components[i].m_activeVersion);
        XMutex_unlock(e->m_mutex);
        if (root) XClassDelete(root);
    }
}

/* ========================================================================== */
/*                          GC（保留最近 2 版，§2；仅 Active 态，不变式 3）      */
/* ========================================================================== */

/** @brief 版本枚举回调上下文。 */
typedef struct XUpVersionList {
    char m_items[XUPGRADE_VERSION_LIST_MAX][XUPGRADE_VERSION_MAX];
    uint32_t m_count;
} XUpVersionList;

/** @brief listVersions 回调：收版本串，超限中止。 */
static int xup_gc_emit(void* userData, const char* version)
{
    XUpVersionList* list = (XUpVersionList*)userData;
    if (!list || !version) return 0;
    if (list->m_count >= XUPGRADE_VERSION_LIST_MAX) return 1;
    xups_str_copy(list->m_items[list->m_count++], XUPGRADE_VERSION_MAX, version);
    return 0;
}

/** @brief GC：仅保留激活版本与最近 KEEP-1 个历史版本。 */
static void xup_gc_component(XUpgradeEngine* e, const char* componentId, const char* keepVersion)
{
    XUpVersionList list;
    uint32_t i;
    uint32_t kept = 0;
    XString* root;
    XString* compDir;
    if (!e || !e->m_store || !componentId) return;
    XMemset(&list, 0, sizeof(list));
    if (XUpgradeStore_listVersions_base(e->m_store, componentId, xup_gc_emit, &list) != XUpgradeError_Ok)
        return;
    /* 按版本降序插入排序（条目少，O(n^2) 足够）。 */
    for (i = 1; i < list.m_count; ++i) {
        char cur[XUPGRADE_VERSION_MAX];
        uint32_t j = i;
        xups_str_copy(cur, XUPGRADE_VERSION_MAX, list.m_items[i]);
        while (j > 0 && XUpgradeManifest_compareVersions(list.m_items[j - 1], cur) < 0) {
            xups_str_copy(list.m_items[j], XUPGRADE_VERSION_MAX, list.m_items[j - 1]);
            --j;
        }
        xups_str_copy(list.m_items[j], XUPGRADE_VERSION_MAX, cur);
    }
    root = xup_root_copy(e);
    if (!root) return;
    compDir = xup_path3(root, XUPGRADE_DIR_VERSIONS, componentId);
    XClassDelete(root);
    if (!compDir) return;
    for (i = 0; i < list.m_count; ++i) {
        const char* ver = list.m_items[i];
        XString* full;
        if (keepVersion && keepVersion[0] != '\0' && xups_str_eq(ver, keepVersion)) continue;
        if (kept < XUPGRADE_STORE_DIR_KEEP_VERSIONS - 1u) { ++kept; continue; }
        full = xup_path3(compDir, ver, NULL);
        if (full) {
            xup_remove_dir_recursively(full);
            xup_logf(e, "GC：清理 %s/%s", componentId, ver);
            XClassDelete(full);
        }
    }
    XClassDelete(compDir);
}

/* ========================================================================== */
/*                          HTTP 下载（XUPGRADE_HTTP_ON）                       */
/* ========================================================================== */

#if XUPGRADE_HTTP_ON

/** @brief HTTP 同步结果。 */
typedef enum XUpGetResult {
    XUpGet_Ok = 0,        /**< 成功。 */
    XUpGet_Network,       /**< 网络错误/超时。 */
    XUpGet_Status,        /**< HTTP 状态非 2xx。 */
    XUpGet_Aborted        /**< 引擎停机被中止。 */
} XUpGetResult;

/** @brief 在当前（工作）线程创建管理器并设默认重定向策略与传输超时。 */
static XNetworkAccessManager* xup_http_mgr_create(void)
{
    XNetworkAccessManager* mgr = XNetworkAccessManager_create();
    if (mgr) {
    XNetworkAccessManager_setRedirectPolicy(mgr, XHttpRequest_NoLessSafeRedirectPolicy);
    /* 整包传输超时必须禁用（0=底层不启计时器）：setTransferTimeout(30s) 是
     * "整个事务 30s 未完成即 abort"——慢链路（如 40KB/s 下 16MB 包需 7 分钟）
     * 永远跑不完 30s 预算，重试轮烧尽后 DownloadFailed，UI 表现为进度冻结
     * "卡住"。真正的停滞检测由下载循环的 30s 无数据 deadline 承担（abort 后
     * 从游标续传）。 */
    XNetworkAccessManager_setTransferTimeout(mgr, 0);
    }
    return mgr;
}

/** @brief 结束前等待管理器事务清理归零再删除（借用指针失效保障，XServerChan 口径）。 */
static void xup_http_mgr_destroy(XNetworkAccessManager* mgr)
{
    int64_t settleDeadline;
    if (!mgr) return;
    settleDeadline = XDateTime_currentMSecsSinceEpoch() + XUPGRADE_HTTP_SETTLE_MS;
    while (XNetworkAccessManager_activeReplyCount(mgr) != 0 &&
           XDateTime_currentMSecsSinceEpoch() < settleDeadline) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (XNetworkAccessManager_activeReplyCount(mgr) != 0) XThread_msleep(1);
    }
    XClassDelete((XClass*)mgr);
}

/**
 * @brief 同步 GET 整段响应体（一次性小响应：manifest）。
 * @param heartbeat 引擎对象；等待期间周期刷新引擎锁心跳（不变式 6），可 NULL。
 * @return 结果码；成功时 *bodyOut 为新 XByteArray（调用者释放）。
 */
static XUpGetResult xup_http_get_bytes(XNetworkAccessManager* mgr, const char* urlUtf8,
                                       XByteArray** bodyOut, int* statusCodeOut,
                                       XUpgradeEngine* heartbeat)
{
    XHttpRequest* request;
    XHttpReply* reply;
    int64_t deadline;
    XUpGetResult result = XUpGet_Ok;
    if (!mgr || !urlUtf8 || !bodyOut) return XUpGet_Network;
    *bodyOut = NULL;
    if (statusCodeOut) *statusCodeOut = 0;
    if (!XCoreApplication_instance()) return XUpGet_Network; /* 同步等待依赖事件泵 */
    request = XHttpRequest_create();
    if (!request) return XUpGet_Network;
    if (!XHttpRequest_setUrl_utf8(request, urlUtf8)) {
        XClassDelete((XClass*)request);
        return XUpGet_Network;
    }
    reply = XNetworkAccessManager_get(mgr, request);
    XClassDelete((XClass*)request);
    if (!reply) return XUpGet_Network;
    deadline = XDateTime_currentMSecsSinceEpoch() + XUPGRADE_HTTP_TIMEOUT_MS;
    while (!XHttpReply_isFinished(reply)) {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        if (!XHttpReply_isFinished(reply)) {
            if (XDateTime_currentMSecsSinceEpoch() >= deadline) {
                XHttpReply_abort(reply);
                result = XUpGet_Network;
            }
            xup_lock_heartbeat(heartbeat);
            XThread_msleep(1);
        }
    }
    if (XHttpReply_error(reply) != XHttpReply_NoError) result = XUpGet_Network;
    else if (XHttpReply_statusCode(reply) < 200 || XHttpReply_statusCode(reply) >= 300)
        result = XUpGet_Status;
    if (statusCodeOut) *statusCodeOut = XHttpReply_statusCode(reply);
    if (result == XUpGet_Ok) {
        *bodyOut = XHttpReply_readAll(reply);
        if (!*bodyOut) result = XUpGet_Network;
    }
    /* 管理器把事务清理投递到事件队列：释放 reply 前先等借用指针失效。 */
    {
        int64_t settle = XDateTime_currentMSecsSinceEpoch() + XUPGRADE_HTTP_SETTLE_MS;
        while (XNetworkAccessManager_activeReplyCount(mgr) != 0 &&
               XDateTime_currentMSecsSinceEpoch() < settle) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            if (XNetworkAccessManager_activeReplyCount(mgr) != 0) XThread_msleep(1);
        }
    }
    XClassDelete((XClass*)reply);
    return result;
}

/** @brief 下载结果。 */
typedef enum XUpDownloadResult {
    XUpDl_Ok = 0,
    XUpDl_Network,     /**< 失败：保留 partial 供续传。 */
    XUpDl_Aborted      /**< 引擎停机。 */
} XUpDownloadResult;

/**
 * @brief 下载一个目标包到 update/partial/<file>.part（Range 断点续传）。
 * @details 续传协议（state.m_download 游标）：续传前复核文件实际大小与
 *          offset 一致、且最后一个完整分块的哈希与游标一致，否则整包重下；
 *          服务端忽略 Range 返回 200 或 416 时同样整包重下一次。
 */
static XUpDownloadResult xup_download_target(XUpgradeEngine* e, XNetworkAccessManager* mgr,
                                             const XUpgradeTarget* t)
{
    XUpgradeStateDownload* cur = &e->m_state.m_download;
    XString* partialDir = NULL;
    XString* partPath = NULL;
    XString* url = NULL;
    char partName[XUPGRADE_FILENAME_MAX];
    int attempt;
    if (!xup_part_name(t->m_file, partName, sizeof(partName))) return XUpDl_Network;
    {
        XString* root = xup_root_copy(e);
        if (root) {
            partialDir = xup_path3(root, XUPGRADE_DIR_UPDATE "/" XUPGRADE_DIR_PARTIAL, NULL);
            XClassDelete(root);
        }
    }
    if (!partialDir) return XUpDl_Network;
    if (!xup_make_dirs(partialDir)) { XClassDelete(partialDir); return XUpDl_Network; }
    partPath = xup_path3(partialDir, partName, NULL);
    XClassDelete(partialDir);
    if (!partPath) return XUpDl_Network;
    url = xup_package_url(e, t->m_file);
    if (!url) { XClassDelete(partPath); return XUpDl_Network; }
    /* 逐组件进度事件（§12 多组件调度）：每个组件的 Download 阶段至少上报
     * 一笔（Progress 携带组件 id）；数据到达进度与收尾补发的 100% 之上，
     * 保证 0% 起点，UI 可按组件分列四段进度。 */
    xup_emit_progress(e, XUpgradePhase_Download, 0,
                      t->m_componentId, t->m_version);

    for (attempt = 0; attempt < 8; ++attempt) {
        XFile* file = NULL;
        XHttpRequest* request = NULL;
        XHttpReply* reply = NULL;
        XCryptographicHash chunkHash;
        bool chunkHashActive = false;
        uint64_t offset = 0;
        uint64_t chunkStart = 0;
        bool resumed = false;
        bool retry = false;
        bool writeFailed = false;
        bool progressEmitted = false;
        XUpDownloadResult result = XUpDl_Network;
        int64_t deadline;

        XMutex_lock(e->m_mutex);
        resumed = cur->m_active &&
                  xups_str_eq(cur->m_componentId, t->m_componentId) &&
                  xups_str_eq(cur->m_fileName, partName) &&
                  cur->m_offset > 0;
        if (resumed) offset = cur->m_offset;
        XMutex_unlock(e->m_mutex);
        /* 复核实际文件大小与末块哈希（篡改/换包防护）。abort 常落在块界之
         * 内：文件比游标多出块尾余数属正常——容忍 fileSize>=offset（哈希
         * 只锚到游标），多余尾部在追加打开后按游标截断作废。 */
        if (resumed) {
            XFile* pf = XFile_create_2(partPath);
            if (pf && XFile_open_2(pf, XIODevice_ReadOnly, 0)) {
                uint64_t fileSize = (uint64_t)XIODevice_size_base((XIODevice*)pf);
                if (fileSize < offset) resumed = false;
                else if (offset >= XUPGRADE_CHUNK_SIZE) {
                    uint8_t expected[XUPGRADE_SHA256_BIN_SIZE];
                    uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
                    uint8_t* buf = (uint8_t*)XMalloc_System(XUPGRADE_CHUNK_SIZE);
                    bool hashOk = false;
                    XMutex_lock(e->m_mutex);
                    XMemcpy(expected, cur->m_chunkSha256, XUPGRADE_SHA256_BIN_SIZE);
                    XMutex_unlock(e->m_mutex);
                    if (buf &&
                        XIODevice_seek_base((XIODevice*)pf,
                                            (int64_t)(offset - XUPGRADE_CHUNK_SIZE))) {
                        XCryptographicHash vh;
                        uint64_t remain = XUPGRADE_CHUNK_SIZE;
                        XCryptographicHash_init(&vh, XCryptographicHash_Sha256);
                        while (remain > 0) {
                            int64_t n = XIODevice_read_1((XIODevice*)pf, (char*)buf,
                                                         (int64_t)remain);
                            if (n <= 0) break;
                            XCryptographicHash_addData_2(&vh,
                                XByteArrayView_create_data(buf, n));
                            remain -= (uint64_t)n;
                        }
                        {
                            XByteArrayView rv = XCryptographicHash_resultView(&vh);
                            if (XByteArrayView_size(&rv) == XUPGRADE_SHA256_BIN_SIZE && remain == 0) {
                                XMemcpy(digest, XByteArrayView_data(&rv), XUPGRADE_SHA256_BIN_SIZE);
                                hashOk = XMemcmp(digest, expected, XUPGRADE_SHA256_BIN_SIZE) == 0;
                            }
                        }
                        XCryptographicHash_deinit(&vh);
                    }
                    if (buf) XFree_System(buf);
                    if (!hashOk) resumed = false;
                }
            } else {
                resumed = false;
            }
            if (pf) {
                XIODevice_close_base((XIODevice*)pf);
                XClassDelete((XClass*)pf);
            }
        }
        /* 断点续传游标激活（§6/矩阵 1）：下载目标即游标身份，自此刻起分块
         * 确认持久化的 offset/分块哈希可被下一次启动/下一轮重试续传。整包
         * 重下时游标归零（offset=0 使续传复核走全量路径，分块哈希随首轮
         * 确认覆盖）。 */
        XMutex_lock(e->m_mutex);
        cur->m_active = true;
        xups_str_copy(cur->m_componentId, XUPGRADE_COMPONENT_ID_MAX, t->m_componentId);
        xups_str_copy(cur->m_fileName, XUPGRADE_FILENAME_MAX, partName);
        if (!resumed) {
            cur->m_offset = 0;
            XMemset(cur->m_chunkSha256, 0, XUPGRADE_SHA256_BIN_SIZE);
        }
        XMutex_unlock(e->m_mutex);
        xup_save_state(e);
        /* 打开输出文件：续传=追加；否则截断重写。块尾余数在打开前按游标
         * 截断（按路径 resize）：追加句柄的写位置语义各后端不一（有的在
         * open 时锚定旧 EOF，resize 后不跟随，会留下陈旧字节缺口），关着
         * 句柄截断则两端行为一致。 */
        if (resumed && !XFile_resize_static(partPath, (int64_t)offset)) break;
        file = XFile_create_2(partPath);
        if (!file || !XFile_open_2(file,
                                   resumed ? (XIODevice_WriteOnly | XIODevice_Append)
                                           : (XIODevice_WriteOnly | XIODevice_Truncate),
                                   0)) {
            if (file) XClassDelete((XClass*)file);
            break;
        }
        request = XHttpRequest_create();
        if (!request || !XHttpRequest_setUrl_utf8(request, XString_toUtf8(url))) {
            XIODevice_close_base((XIODevice*)file);
            XClassDelete((XClass*)file);
            if (request) XClassDelete((XClass*)request);
            break;
        }
        if (resumed) {
            char range[48];
            xup_fmt(range, sizeof(range), "bytes=%llu-", (unsigned long long)offset);
            if (!XHttpRequest_setRawHeader(request, "Range", range)) {
                XIODevice_close_base((XIODevice*)file);
                XClassDelete((XClass*)file);
                XClassDelete((XClass*)request);
                break;
            }
        }
        reply = XNetworkAccessManager_get(mgr, request);
        if (!reply) {
            XIODevice_close_base((XIODevice*)file);
            XClassDelete((XClass*)file);
            XClassDelete((XClass*)request);
            break;
        }
        XCryptographicHash_init(&chunkHash, XCryptographicHash_Sha256);
        chunkHashActive = true;
        chunkStart = (offset / XUPGRADE_CHUNK_SIZE) * XUPGRADE_CHUNK_SIZE;
        deadline = XDateTime_currentMSecsSinceEpoch() + XUPGRADE_HTTP_TIMEOUT_MS;
        /* 主循环：泵事件 + 边下边落盘（readAll 消费，内存有界）。 */
        while (!XHttpReply_isFinished(reply)) {
            XCoreApplication_processEvents(XEventLoop_AllEvents);
            if (XHttpReply_isFinished(reply)) break;
            if (xup_stop_requested(e)) {
                XHttpReply_abort(reply);
                result = XUpDl_Aborted;
                break;
            }
            {
                const XByteArray* body = XHttpReply_body_const(reply);
                int64_t pending = body ? (int64_t)XByteArray_size_base(body) : 0;
                if (pending <= 0) {
                    if (XDateTime_currentMSecsSinceEpoch() >= deadline) {
                        XHttpReply_abort(reply);
                        result = XUpDl_Network;
                        break;
                    }
                    xup_lock_heartbeat(e); /* 分钟级下载期间保住引擎锁心跳 */
                    XThread_msleep(10);
                    continue;
                }
                deadline = XDateTime_currentMSecsSinceEpoch() + XUPGRADE_HTTP_TIMEOUT_MS;
            }
            {
                XByteArray* drained = XHttpReply_readAll(reply);
                int64_t n = drained ? (int64_t)XByteArray_size_base(drained) : 0;
                if (!drained || n <= 0 ||
                    XIODevice_write_1((XIODevice*)file,
                                      (const char*)XByteArray_constData(drained), n) != n) {
                    if (drained) XClassDelete(drained);
                    result = XUpDl_Network;
                    writeFailed = true;
                    break;
                }
                /* 分块哈希游标：每满一块记一次并原子落盘（§6 游标语义）。 */
                {
                    const uint8_t* data = XByteArray_constData(drained);
                    int64_t consumed = 0;
                    while (consumed < n) {
                        uint64_t room = XUPGRADE_CHUNK_SIZE - (offset - chunkStart);
                        int64_t take = (int64_t)(room < (uint64_t)(n - consumed)
                                                     ? room : (uint64_t)(n - consumed));
                        XCryptographicHash_addData_2(&chunkHash,
                            XByteArrayView_create_data(data + consumed, take));
                        consumed += take;
                        offset += (uint64_t)take;
                        if (offset - chunkStart >= XUPGRADE_CHUNK_SIZE) {
                            XByteArrayView rv = XCryptographicHash_resultView(&chunkHash);
                            if (XByteArrayView_size(&rv) == XUPGRADE_SHA256_BIN_SIZE) {
                                XMutex_lock(e->m_mutex);
                                XMemcpy(cur->m_chunkSha256,
                                        XByteArrayView_data(&rv), XUPGRADE_SHA256_BIN_SIZE);
                                cur->m_offset = offset;
                                XMutex_unlock(e->m_mutex);
                                xup_save_state(e);
                            }
                            XCryptographicHash_reset(&chunkHash);
                            chunkStart += XUPGRADE_CHUNK_SIZE;
                        }
                    }
                }
                XClassDelete(drained);
                if (t->m_size > 0) {
                    int percent = (int)((offset * 100) / t->m_size);
                    if (percent > 100) percent = 100;
                    progressEmitted = true;
                    xup_emit_progress(e, XUpgradePhase_Download, percent,
                  t->m_componentId, t->m_version);
                }
            }
        }
        /* 收尾：清空残余 body + 状态判定。 */
        while (result == XUpDl_Network || result == XUpDl_Ok) {
            XByteArray* tail = XHttpReply_readAll(reply);
            int64_t n = tail ? (int64_t)XByteArray_size_base(tail) : 0;
            if (!tail) break;
            if (n > 0) {
                if (XIODevice_write_1((XIODevice*)file,
                                      (const char*)XByteArray_constData(tail), n) == n) {
                    XCryptographicHash_addData_2(&chunkHash,
                        XByteArrayView_create_data(XByteArray_constData(tail), n));
                    offset += (uint64_t)n;
                }
            }
            XClassDelete(tail);
            if (n == 0) break;
        }
        if (result != XUpDl_Aborted) {
            /* 引擎停机中止保留 Aborted 语义（abort 也会置回复错误码/状态码 0）。 */
            if (XHttpReply_error(reply) != XHttpReply_NoError) {
                result = XUpDl_Network;
            } else {
                int code = XHttpReply_statusCode(reply);
                if (code == 416) retry = true;                                   /* Range 越界 → 重下 */
                else if (code == 200 && resumed && offset > 0) retry = true;     /* 忽略 Range → 重下 */
                /* 200/206 且落盘无失败才算成功：result 初值即 Network，成功
                 * 必须显式置 Ok（否则成功响应也被判下载失败、partial 误保留）。 */
                else if ((code == 206 || code == 200) && !writeFailed) result = XUpDl_Ok;
                else result = XUpDl_Network;
            }
        }
        {
            int64_t settle = XDateTime_currentMSecsSinceEpoch() + XUPGRADE_HTTP_SETTLE_MS;
            while (XNetworkAccessManager_activeReplyCount(mgr) != 0 &&
                   XDateTime_currentMSecsSinceEpoch() < settle) {
                XCoreApplication_processEvents(XEventLoop_AllEvents);
                if (XNetworkAccessManager_activeReplyCount(mgr) != 0) XThread_msleep(1);
            }
        }
        if (chunkHashActive) XCryptographicHash_deinit(&chunkHash);
        XIODevice_close_base((XIODevice*)file);
        XClassDelete((XClass*)file);
        XClassDelete((XClass*)reply);
        XClassDelete((XClass*)request);
        if (result == XUpDl_Ok) {
            XMutex_lock(e->m_mutex);
            cur->m_offset = offset;
            XMutex_unlock(e->m_mutex);
            xup_save_state(e);
            /* 下载成功收尾必发 100%：逐块进度按 chunk 边界取整，最后一块
             * 确认点通常落在 size 之前（floor 到 99%），无条件补发 100% 收口
             * （2026-10-07 修复：原 !progressEmitted 守卫使逐块进度发过后
             * 100% 被跳过，UI 下载条永久卡 99%）。 */
            (void)progressEmitted;
            if (t->m_size > 0) {
                progressEmitted = true;
                xup_emit_progress(e, XUpgradePhase_Download, 100,
                                  t->m_componentId, t->m_version);
            }
        }
        if (!retry) {
            /* 网络类失败自动退避续传（§6 部分保留口径）：partial 游标仍在，
             * 下一轮 Range 从游标真续传（块尾余数按游标截断）；退避等待可
             * 被停机打断。非网络失败（写失败/停机）立即返回。重试上限=8 轮，
             * 超限 DownloadFailed 交上层，partial/游标保留、再次发起升级即
             * 从断点继续。 */
            if (result == XUpDl_Network && !xup_stop_requested(e) && attempt < 7) {
                int64_t backoff = XDateTime_currentMSecsSinceEpoch() +
                                  (int64_t)(3000u << attempt); /* 3s/6s/12s */
                xup_logf(e, "下载网络失败（第 %u 轮），%us 后从 %llu 字节处续传",
                         (unsigned)(attempt + 1), (unsigned)(3u << attempt),
                         (unsigned long long)offset);
                while (XDateTime_currentMSecsSinceEpoch() < backoff &&
                       !xup_stop_requested(e))
                    XThread_msleep(100);
                if (xup_stop_requested(e)) return XUpDl_Aborted;
                continue; /* 续传：游标仍在，下一轮 Range 接着下 */
            }
            XClassDelete(partPath);
            XClassDelete(url);
            return result;
        }
        /* 整包重下：清游标，第二轮从头拉。 */
        XMutex_lock(e->m_mutex);
        XMemset(cur, 0, sizeof(*cur));
        xups_str_copy(cur->m_componentId, XUPGRADE_COMPONENT_ID_MAX, t->m_componentId);
        xups_str_copy(cur->m_fileName, XUPGRADE_FILENAME_MAX, partName);
        cur->m_active = true;
        XMutex_unlock(e->m_mutex);
    }
    XClassDelete(partPath);
    XClassDelete(url);
    return XUpDl_Network;
}

#endif /* XUPGRADE_HTTP_ON */

/* ========================================================================== */
/*                     策略时机调度（§10，判定逻辑在 XUpgradeGray.c）           */
/* ========================================================================== */

/** @brief silent 维护窗口对当前墙钟时刻的判定（墙钟 XDateTime_currentDateTime，
 *         真纪元场景；风格指南 2026-09-27 裁定，禁用单调毫秒拼时刻）。 */
static bool xup_policy_window_now(const XUpgradeApplyWindow* window)
{
    XDateTime now = XDateTime_currentDateTime();
    XTime t = XDateTime_time(&now);
    return XUpgradeGray_windowContains(window, XTime_hour(&t), XTime_minute(&t));
}

/**
 * @brief staged 完成后的策略自动 apply：推 ReadyAutoApply 并受理 apply 请求。
 * @details forced=staged 即自动进入 apply；silent=维护窗口到达自动应用。桌面
 *          目录后端"应用"=受理请求（调用方以退出码 74 退出，由
 *          --xupgrade-apply 完成指针切换，§8.1）；事件流给 UI 遮罩消费，
 *          引擎不新增遮罩 API（§10/§12）。宽限期解析值随事件 detail 携带，
 *          可断言（宽限期内 UI 允许"稍后"，每次启动重新提示由下一检查周期
 *          的 forced 流程再触发）。
 */
static void xup_policy_auto_apply(XUpgradeEngine* e, const char* reason)
{
    char detail[XUPGRADE_DETAIL_MAX];
    char comp[XUPGRADE_COMPONENT_ID_MAX];
    char ver[XUPGRADE_VERSION_MAX];
    uint32_t graceHours;
    if (!e || !reason) return;
    XMutex_lock(e->m_mutex);
    if (e->m_readyAutoApplyDone) { XMutex_unlock(e->m_mutex); return; }
    e->m_readyAutoApplyDone = true; /* 一次 Ready 周期只受理一次（防工作线程周期重发） */
    if (e->m_targetCount > 0 && e->m_targetCount <= XUPGRADE_COMPONENT_MAX) {
        xups_str_copy(comp, sizeof(comp), e->m_targets[0].m_componentId);
        xups_str_copy(ver, sizeof(ver), e->m_targets[0].m_version);
    } else {
        comp[0] = '\0';
        ver[0] = '\0';
    }
    graceHours = XUpgradeGray_resolveGraceHours(e->m_hasForcedGraceOverride,
                                                e->m_forcedGraceHoursOverride,
                                                e->m_hasManifest, &e->m_manifest);
    XMutex_unlock(e->m_mutex);
    xup_fmt(detail, sizeof(detail), "%s：即将自动应用（宽限期 %u 小时）",
            reason, (unsigned)graceHours);
    xup_emit_evt(e, XUpgradeEventType_ReadyAutoApply, XUpgradeState_Ready,
                 XUpgradePhase_Activate, -1, comp, ver, detail, true);
    xup_logf(e, "自动 apply 受理：%s；调用方以退出码 74 退出，由 --xupgrade-apply 完成切换",
             reason);
}

/**
 * @brief 工作线程周期策略时机判定（§10）：Ready 态下 forced 立即自动 apply，
 *        silent 等维护窗口（墙钟判定，错过顺延到下一窗口，不 7x24 抢重启）。
 * @details 仅在本次检查已缓存 manifest（m_hasManifest）且尚未受理过自动
 *          apply 时判定；optional 永不自动 apply。窗口未启用时 silent 保持
 *          ReadyNeedRestart（桌面"退出时应用"缺省）。
 */
static void xup_policy_tick(XUpgradeEngine* e)
{
    bool due = false;
    const char* reason = NULL;
    if (!e) return;
    XMutex_lock(e->m_mutex);
    if (e->m_started && !e->m_stop && e->m_hasManifest && !e->m_readyAutoApplyDone &&
        e->m_state.m_state == XUpgradeState_Ready &&
        e->m_policy != XUpgradePolicy_Optional) {
        if (e->m_policy == XUpgradePolicy_Forced) {
            due = true;
            reason = "forced：Ready 态自动应用";
        } else if (e->m_applyWindow.m_enabled) {
            if (xup_policy_window_now(&e->m_applyWindow)) { /* 持锁调用：纯计算不落盘 */
                due = true;
                reason = "silent：维护窗口到达自动应用";
            }
        }
    }
    XMutex_unlock(e->m_mutex);
    if (due) xup_policy_auto_apply(e, reason);
}

/* ========================================================================== */
/*                          检查流水（Checking，§6/§10）                        */
/* ========================================================================== */

/** @brief 用 URL 模板构造 manifest/base URL（替换 {channel} 占位）。 */
static bool xup_build_urls(XUpgradeEngine* e)
{
    XString* url;
    const char* channel = "";
    if (!e) return false;
    XMutex_lock(e->m_mutex);
    if (!e->m_manifestUrl) { XMutex_unlock(e->m_mutex); return false; }
    if (e->m_channel) channel = XString_toUtf8(e->m_channel);
    url = XString_create_copy(e->m_manifestUrl);
    XMutex_unlock(e->m_mutex);
    if (!url) return false;
    if (channel[0] != '\0' &&
        !XString_replace_utf8(url, "{channel}", channel, XChar_CaseSensitive)) {
        XClassDelete(url);
        return false;
    }
    XMutex_lock(e->m_mutex);
    if (e->m_manifestUrl) XClassDelete(e->m_manifestUrl);
    e->m_manifestUrl = url;
    if (e->m_baseUrl) { XClassDelete(e->m_baseUrl); e->m_baseUrl = NULL; }
    XMutex_unlock(e->m_mutex);
    /* base = manifest URL 去掉结尾 "manifest.json"（保留斜杠）。 */
    {
        const char* data = XString_toUtf8(url);
        size_t len = XString_toUtf8_length(url);
        static const char suffix[] = "manifest.json";
        size_t suffixLen = sizeof(suffix) - 1;
        if (len > suffixLen && xups_str_eq(data + (len - suffixLen), suffix)) {
            XString* base = XString_create_with_length_utf8(data, len - suffixLen);
            if (base) {
                XMutex_lock(e->m_mutex);
                e->m_baseUrl = base;
                XMutex_unlock(e->m_mutex);
            }
        } else if (len > 0) {
            XString* base = XString_create_copy(url);
            if (base) {
                if (!XString_endsWith_utf8(base, "/", XChar_CaseSensitive))
                    (void)XString_append_utf8(base, "/");
                XMutex_lock(e->m_mutex);
                e->m_baseUrl = base;
                XMutex_unlock(e->m_mutex);
            }
        }
    }
    return e->m_baseUrl != NULL;
}

/** @brief 构造包文件完整 URL（新 XString，调用者释放）。 */
static XString* xup_package_url(XUpgradeEngine* e, const char* fileName)
{
    XString* url;
    XMutex_lock(e->m_mutex);
    url = e->m_baseUrl ? XString_create_copy(e->m_baseUrl) : NULL;
    XMutex_unlock(e->m_mutex);
    if (!url) return NULL;
    if (!XString_append_utf8(url, fileName ? fileName : "")) { XClassDelete(url); return NULL; }
    return url;
}

/** @brief 清空目标表与 manifest 缓存。 */
static void xup_reset_targets(XUpgradeEngine* e)
{
    e->m_hasManifest = false;
    e->m_targetCount = 0;
    XMemset(e->m_targets, 0, sizeof(e->m_targets));
}

/**
 * @brief 计算目标表的下载依赖序（组件调度，设计文档 §6）。
 * @param targets 目标表；不能为 NULL。
 * @param count 有效目标数；0 直接返回。
 * @param order 输出下标序列；调用方提供存储空间，容量不少于 count。
 * @return 无。
 * @details 稳定拓扑序：被依赖组件（如 fontres）排在依赖它的组件（如 app 之
 *          requires fontres>=2.0）之前，同轮次保持 manifest 声明序；仅约束
 *          本轮同时升级目标之间的依赖，指向未升级组件的 requires 不参与排序
 *          （其配套校验在 staging 完成后由 xup_check_requires 按已激活/已
 *          staged 版本执行）。成环时剩余目标按声明序兜底追加，最终由
 *          xup_check_requires 拒绝。不改动目标表本身：UpdateAvailable/Ready
 *          等事件的组件字段仍取 manifest 声明序首个命中项，事件语义不变。
 */
static void xup_targets_download_order(const XUpgradeTarget* targets, uint32_t count,
                                       uint32_t* order)
{
    bool placed[XUPGRADE_COMPONENT_MAX];
    uint32_t out;
    if (!targets || !order || count == 0 || count > XUPGRADE_COMPONENT_MAX) return;
    XMemset(placed, 0, sizeof(placed));
    for (out = 0; out < count; ++out) {
        uint32_t pick = count;
        uint32_t i;
        for (i = 0; i < count && pick == count; ++i) {
            uint32_t r;
            bool ready = true;
            if (placed[i]) continue;
            for (r = 0; ready && r < targets[i].m_requires.m_count && r < XUPGRADE_REQUIRES_MAX; ++r) {
                uint32_t k;
                for (k = 0; k < count; ++k) {
                    if (k == i || placed[k]) continue;
                    if (xups_str_eq(targets[k].m_componentId,
                                   targets[i].m_requires.m_component[r])) {
                        ready = false; /* 依赖的目标尚未排入：本条不就绪 */
                        break;
                    }
                }
            }
            if (ready) pick = i;
        }
        if (pick == count) break; /* 成环兜底：剩余按声明序追加 */
        placed[pick] = true;
        order[out] = pick;
    }
    for (; out < count; ++out) {
        uint32_t i;
        for (i = 0; i < count; ++i) {
            if (!placed[i]) {
                placed[i] = true;
                order[out] = i;
                break;
            }
        }
    }
}

/**
 * @brief 检查流水：拉取并解析 manifest，产出升级目标表；命中推
 *        UpdateAvailable（携带生效策略 m_policy 与灰度判定 m_gray，§10/§11；
 *        optional 暂停在 Checking 等待 startUpgrade，forced/silent 自动续跑
 *        下载流水），未命中/失败/灰度拒绝推 CheckFailed 语义事件并落
 *        CheckFailed 态。
 */
static void xup_run_check(XUpgradeEngine* e, bool manual)
{
    XUpgradeStateFile* st = &e->m_state;
    XUpgradeManifest manifest;
    bool parsed = false;
    bool autoUpgradeDisabled = false;
    bool channelOk = true;
    bool haveUpdate = false;
    bool targetsOverflow = false;
    bool anyForced = false;
    bool downgradeRejected = false;
    char targetOs[XUPGRADE_OS_MAX];
    char firstComp[XUPGRADE_COMPONENT_ID_MAX] = "";
    char firstVer[XUPGRADE_VERSION_MAX] = "";
    char pinnedVersion[XUPGRADE_VERSION_MAX];
    bool pinnedInstall = false;
    uint32_t i;
    XMutex_lock(e->m_mutex);
    e->m_manualCheck = manual;
    e->m_lastCheckMs = XDateTime_currentMSecsSinceEpoch();
    xups_str_copy(targetOs, sizeof(targetOs),
                 e->m_targetOs ? XString_toUtf8(e->m_targetOs) : "");
    xups_str_copy(pinnedVersion, sizeof(pinnedVersion), e->m_pinnedVersion);
    pinnedInstall = pinnedVersion[0] != '\0'; /* installVersion 显式指定：绕过仅更新门槛/灰度/跳过表，防降级仍受 rollback_to 约束 */
    /* 回滚计数超限 → 停用自动升级（§8.3）。 */
    for (i = 0; i < st->m_componentCount && i < XUPGRADE_STATE_COMPONENT_MAX; ++i)
        if (st->m_components[i].m_rollbackCount > XUPGRADE_ROLLBACK_LIMIT)
            autoUpgradeDisabled = true;
    XMutex_unlock(e->m_mutex);
    if (autoUpgradeDisabled) {
        xup_set_state(e, XUpgradeState_CheckFailed);
        xup_emit_evt(e, XUpgradeEventType_CheckFailed, XUpgradeState_CheckFailed,
                     XUpgradePhase_Download, -1, "", "",
                     "回滚计数超限，自动升级已停用", true);
        xup_log(e, "检查被拒：回滚计数超限（§8.3）");
        return;
    }
    xup_set_state(e, XUpgradeState_Checking);
#if XUPGRADE_HTTP_ON
    {
        XNetworkAccessManager* mgr = NULL;
        XByteArray* body = NULL;
        XUpGetResult r;
        if (!XCoreApplication_instance()) {
            xup_set_state(e, XUpgradeState_CheckFailed);
            xup_emit_evt(e, XUpgradeEventType_CheckFailed, XUpgradeState_CheckFailed,
                         XUpgradePhase_Download, -1, "", "",
                         "检查失败：进程未创建 XCoreApplication（同步等待依赖事件泵）",
                         true);
            xup_log(e, "检查失败：无 XCoreApplication");
            return;
        }
        if (!xup_build_urls(e)) {
            xup_set_state(e, XUpgradeState_CheckFailed);
            xup_emit_evt(e, XUpgradeEventType_CheckFailed, XUpgradeState_CheckFailed,
                         XUpgradePhase_Download, -1, "", "", "检查失败：URL 模板无效", true);
            xup_log(e, "检查失败：URL 模板无效");
            return;
        }
        mgr = xup_http_mgr_create();
        if (!mgr) {
            xup_set_state(e, XUpgradeState_CheckFailed);
            xup_emit_evt(e, XUpgradeEventType_CheckFailed, XUpgradeState_CheckFailed,
                         XUpgradePhase_Download, -1, "", "",
                         "检查失败：HTTP 管理器创建失败", true);
            return;
        }
        {
            XString* url;
            XUpgradeCatalog catalog;
            XMutex_lock(e->m_mutex);
            url = e->m_manifestUrl ? XString_create_copy(e->m_manifestUrl) : NULL;
            XMutex_unlock(e->m_mutex);
            if (!url) { xup_http_mgr_destroy(mgr); xup_set_state(e, XUpgradeState_CheckFailed); return; }
            r = xup_http_get_bytes(mgr, XString_toUtf8(url), &body, NULL, e);
            XClassDelete(url);
            /* 多版本目录（schema 2，§4 扩展）：目录本体无签名——信任链锚在
             * 各 release manifest 的签名（安装任一条目时整链验签）。命中目录
             * 则存缓存（installVersion/UI 枚举）并自动选用最新（首个）release
             * 的 manifest 二次拉取后走 v1 流程；installVersion 的显式指定
             * 条目优先于"最新"。 */
            if (r == XUpGet_Ok && body &&
                XUpgradeManifest_parseCatalog(&catalog,
                    (const char*)XByteArray_constData(body),
                    XByteArray_size_base(body))) {
                uint32_t pick = 0;
                XString* url2 = NULL;
                if (catalog.m_releaseCount == 0) {
                    XClassDelete(body);
                    xup_http_mgr_destroy(mgr);
                    xup_set_state(e, XUpgradeState_CheckFailed);
                    xup_emit_evt(e, XUpgradeEventType_CheckFailed,
                                 XUpgradeState_CheckFailed, XUpgradePhase_Download,
                                 -1, "", "", "检查失败：目录无可用版本", true);
                    xup_log(e, "检查失败：目录无 release");
                    return;
                }
                XMutex_lock(e->m_mutex);
                e->m_catalog = catalog;
                e->m_hasCatalog = true;
                /* 显式指定版本优先（installVersion），否则取最新（首个）。
                 * 指定版本不在目录=永久性失败：清 pinned 并拒绝（不得静默
                 * 回退最新版——用户显式选择不得被偷换）。 */
                if (e->m_pinnedVersion[0] != '\0') {
                    bool pinnedFound = false;
                    for (pick = 0; pick < catalog.m_releaseCount; ++pick) {
                        if (xups_str_eq(catalog.m_releases[pick].m_version,
                                       e->m_pinnedVersion)) {
                            pinnedFound = true;
                            break;
                        }
                    }
                    if (!pinnedFound) {
                        char miss[XUPGRADE_DETAIL_MAX];
                        XClassDelete(body);
                        body = NULL;
                        e->m_pinnedVersion[0] = '\0';
                        XMutex_unlock(e->m_mutex);
                        xup_http_mgr_destroy(mgr);
                        xup_fmt(miss, sizeof(miss),
                                "目录无指定版本 %s（显式安装被拒）",
                                pinnedVersion);
                        xup_set_state(e, XUpgradeState_CheckFailed);
                        xup_emit_evt(e, XUpgradeEventType_CheckFailed,
                                     XUpgradeState_CheckFailed, XUpgradePhase_Download,
                                     -1, "", "", miss, true);
                        xup_log(e, miss);
                        return;
                    }
                }
                XMutex_unlock(e->m_mutex);
                XClassDelete(body); /* 目录体已解析入缓存：复用前先释放防泄漏 */
                body = NULL;
                url2 = xup_package_url(e, catalog.m_releases[pick].m_manifest);
                if (!url2) {
                    xup_http_mgr_destroy(mgr);
                    xup_set_state(e, XUpgradeState_CheckFailed);
                    return;
                }
                r = xup_http_get_bytes(mgr, XString_toUtf8(url2), &body, NULL, e);
                XClassDelete(url2);
                if (r != XUpGet_Ok) {
                    if (body) XClassDelete(body);
                    xup_http_mgr_destroy(mgr);
                    xup_set_state(e, XUpgradeState_CheckFailed);
                    xup_emit_evt(e, XUpgradeEventType_CheckFailed,
                                 XUpgradeState_CheckFailed, XUpgradePhase_Download,
                                 -1, "", "", "检查失败：release manifest 拉取失败", true);
                    xup_log(e, "检查失败：release manifest 拉取失败");
                    return;
                }
            }
        }
        xup_http_mgr_destroy(mgr);
        if (r != XUpGet_Ok) {
            xup_set_state(e, XUpgradeState_CheckFailed);
            xup_emit_evt(e, XUpgradeEventType_CheckFailed, XUpgradeState_CheckFailed,
                         XUpgradePhase_Download, -1, "", "",
                         r == XUpGet_Status ? "检查失败：HTTP 状态非 2xx"
                                            : "检查失败：网络错误/超时",
                         true);
            xup_log(e, r == XUpGet_Status ? "检查失败：HTTP 状态非 2xx" : "检查失败：网络错误/超时");
            return;
        }
        parsed = XUpgradeManifest_parse(&manifest,
                                        (const char*)XByteArray_constData(body),
                                        XByteArray_size_base(body));
        XClassDelete(body);
    }
#else
    (void)parsed;
    xup_set_state(e, XUpgradeState_CheckFailed);
    xup_emit_evt(e, XUpgradeEventType_CheckFailed, XUpgradeState_CheckFailed,
                 XUpgradePhase_Download, -1, "", "",
                 "当前构建未编入 HTTP 下载通道（XUPGRADE_HTTP_ON=0）", true);
    return;
#endif
    if (!parsed) {
        /* 永久性失败（同字节每轮必败）：清 pinned 防 installVersion 显式
         * 安装随周期检查无限重试（2026-10-08 评审）。 */
        XMutex_lock(e->m_mutex);
        e->m_pinnedVersion[0] = '\0';
        XMutex_unlock(e->m_mutex);
        xup_set_state(e, XUpgradeState_CheckFailed);
        xup_emit_evt(e, XUpgradeEventType_CheckFailed, XUpgradeState_CheckFailed,
                     XUpgradePhase_Download, -1, "", "",
                     "检查失败：manifest 缺失/非法/越限", true);
        xup_log(e, "检查失败：manifest 解析被拒");
        return;
    }
    /* §4.2 签名信任链：manifest 真实性在 HTTP 通道上的唯一锚（"纯 HTTP 内网
     * 也成立，因为信任锚是签名"）。解析成功即整链验签（canonical → SHA-256
     * → P-256；信任锚=XUpgrade_setTrustKey 注入或编译期内嵌钥），验签不通过
     * 整单拒绝。P-256 能力被裁剪的调试构建（XUPGRADE_VERIFY_P256_ON=0，
     * verifyManifest 恒 NotSupported）不拦截，发布口径（默认 1）强制生效。 */
    {
        extern XUpgradeError XUpgradeVerify_verifyManifest(const XUpgradeManifest* manifest);
        XUpgradeError verified = XUpgradeVerify_verifyManifest(&manifest);
        if (verified != XUpgradeError_Ok && verified != XUpgradeError_NotSupported) {
            /* 签名不通过=永久性失败（同包每轮必败）：清 pinned；Io（验签器
             * 暂不可用）为瞬时态保留 pinned 待自动重试。 */
            if (verified != XUpgradeError_Io) {
                XMutex_lock(e->m_mutex);
                e->m_pinnedVersion[0] = '\0';
                XMutex_unlock(e->m_mutex);
            }
            xup_set_state(e, XUpgradeState_CheckFailed);
            xup_emit_evt(e, XUpgradeEventType_CheckFailed, XUpgradeState_CheckFailed,
                         XUpgradePhase_Download, -1, "", "",
                         verified == XUpgradeError_Io ? "检查失败：验签器暂不可用"
                                                      : "检查失败：manifest 签名校验不通过",
                         true);
            xup_logf(e, "检查失败：manifest 验签未通过（错误=%d），整单拒绝",
                     (int)verified);
            return;
        }
    }
    /* 显式安装（installVersion）：目标 release 与本地激活版本完全一致 →
     * 已安装，前置拒绝（否则 staging 阶段才因版本目录已存在失败，白耗
     * 16MB 级下载）。逐组件比对（components[0] 单点比对在多组件/组件序
     * 变化时口径失真）。已安装=永久性失败：清 pinned 防周期重试。 */
    if (pinnedInstall) {
        bool alreadyInstalled = manifest.m_componentCount > 0;
        uint32_t ci;
        XMutex_lock(e->m_mutex);
        for (ci = 0; ci < manifest.m_componentCount && ci < XUPGRADE_COMPONENT_MAX; ++ci) {
            XUpgradeStateComponent* rec0 =
                xup_state_component(st, manifest.m_components[ci].m_id, false);
            if (!rec0 ||
                !xups_str_eq(rec0->m_activeVersion, manifest.m_components[ci].m_version)) {
                alreadyInstalled = false;
                break;
            }
        }
        if (alreadyInstalled) e->m_pinnedVersion[0] = '\0';
        XMutex_unlock(e->m_mutex);
        if (alreadyInstalled) {
            xup_set_state(e, XUpgradeState_CheckFailed);
            xup_emit_evt(e, XUpgradeEventType_CheckFailed, XUpgradeState_CheckFailed,
                         XUpgradePhase_Download, -1, "", "",
                         "目标版本与本地相同（已安装）", true);
            xup_logf(e, "检查失败：显式安装 %s 与本地激活版本相同（已安装）",
                     manifest.m_version);
            return;
        }
    }
    /* 渠道一致性：本地渠道非空且与 manifest 不符按无更新处理。
     * 渠道配置本地锁定，重试不可变更=永久性失败：清 pinned。 */
    XMutex_lock(e->m_mutex);
    if (e->m_channel) {
        const char* ch = XString_toUtf8(e->m_channel);
        channelOk = ch[0] == '\0' || xups_str_eq(ch, manifest.m_channel);
    }
    if (channelOk) {
        xup_reset_targets(e);
        e->m_manifest = manifest;
        e->m_hasManifest = true;
    }
    else if (pinnedInstall) e->m_pinnedVersion[0] = '\0';
    XMutex_unlock(e->m_mutex);
    if (!channelOk) {
        xup_set_state(e, XUpgradeState_CheckFailed);
        xup_emit_evt(e, XUpgradeEventType_CheckFailed, XUpgradeState_CheckFailed,
                     XUpgradePhase_Download, -1, "", "", "检查失败：渠道不匹配", true);
        xup_log(e, "检查失败：manifest 渠道不匹配");
        return;
    }
        /* 灰度判定（§11）：allow 名单→deny 名单→分流哈希（算法契约
         * XUpgradeManifest_grayHit，实现在 XUpgradeGray.c）。未命中（Miss/
         * DenyList）按"暂无更新"处理：记录本次判定（XUpgrade_grayDecision 可查，
         * 不重复请求），下次检查周期再判。installVersion 显式指定时绕过灰度
         * （用户已明确选择目标版本）。 */
        {
            XUpgradeGrayDecision gray = XUpgradeGray_evaluate(&manifest);
            XMutex_lock(e->m_mutex);
            e->m_grayDecision = gray;
            XMutex_unlock(e->m_mutex);
            if (!pinnedInstall && !XUpgradeGray_decisionPass(gray)) {
            xup_set_state(e, XUpgradeState_CheckFailed);
            xup_emit_evt(e, XUpgradeEventType_CheckFailed, XUpgradeState_CheckFailed,
                         XUpgradePhase_Download, -1, "", "",
                         gray == XUpgradeGrayDecision_DenyList
                             ? "暂无更新（灰度名单拒绝）"
                             : "暂无更新（灰度未命中）",
                         true);
            xup_logf(e, "检查完成：灰度判定未放行（判定=%d），本地记录本次判定，下次检查周期再判",
                     (int)gray);
            return;
        }
    }
        /* 产出目标表：os 匹配（同 id 多条目时精确优先于 any，findComponent 同
         * 口径）、版本高于本地、未被跳过（§10 表行 2：仅非 forced 生效）；顺手
         * 修剪过期跳过记录（下一版本自动重置，§10）。installVersion 显式指定
         * 版本（pinned）时绕过"仅更新"门槛与跳过表——降级/重装均为用户显式
         * 选择；防降级硬门（rollback_to 窗口）仍生效。 */
        for (i = 0; i < manifest.m_componentCount && i < XUPGRADE_COMPONENT_MAX; ++i) {
            const XUpgradeManifestComponent* c = &manifest.m_components[i];
            char local[XUPGRADE_VERSION_MAX];
            size_t fileLen = 0;
            bool exact;
            bool pinnedActive;
            if (!xups_str_eq(c->m_os, targetOs) && !xups_str_eq(c->m_os, "any")) continue;
            if (c->m_file[0] == '\0' || !xups_version_valid(c->m_version)) continue;
            while (c->m_file[fileLen] != '\0') ++fileLen;
            if (fileLen + XUPGRADE_PART_SUFFIX_LEN >= XUPGRADE_FILENAME_MAX) continue;
            pinnedActive = pinnedInstall &&
                           xups_str_eq(c->m_version, pinnedVersion);
            local[0] = '\0';
            XMutex_lock(e->m_mutex);
            {
                XUpgradeStateComponent* rec = xup_state_component(st, c->m_id, true);
                if (rec) xups_str_copy(local, XUPGRADE_VERSION_MAX, rec->m_activeVersion);
            }
            xup_skip_prune_one(st, c->m_id, local);
            XMutex_unlock(e->m_mutex);
            if (XUpgradeManifest_compareVersions(c->m_version, local) <= 0 &&
                !pinnedActive)
                continue;
        /* §4.2.4 防降级硬门：目标 < 本地历史最高已激活版本（maxVersion）且
         * 不落在签名的回滚窗口（rollback_to）内 → 拒绝该组件（引擎此前只比
         * activeVersion，回滚后的设备可被诱导降级穿越）。判定纯函数为
         * XUpgradeVerify_antiDowngradeAllowed 契约位；拒绝记日志并映射
         * DowngradeRejected 语义（检查链以 CheckFailed detail 体现）。 */
        {
            extern bool XUpgradeVerify_antiDowngradeAllowed(const char* candidateVersion,
                                                            const char* maxVersion,
                                                            const char* rollbackTo);
            char maxVersion[XUPGRADE_VERSION_MAX];
            maxVersion[0] = '\0';
            XMutex_lock(e->m_mutex);
            {
                XUpgradeStateComponent* rec2 = xup_state_component(st, c->m_id, false);
                if (rec2) xups_str_copy(maxVersion, XUPGRADE_VERSION_MAX, rec2->m_maxVersion);
            }
            XMutex_unlock(e->m_mutex);
            if (!XUpgradeVerify_antiDowngradeAllowed(c->m_version, maxVersion,
                                                     manifest.m_rollbackTo)) {
                downgradeRejected = true;
                xup_logf(e, "防降级：拒绝 %s@%s（maxVersion=%s，rollback_to=%s）",
                         c->m_id, c->m_version,
                         maxVersion[0] != '\0' ? maxVersion : "<无基线>",
                         manifest.m_rollbackTo[0] != '\0' ? manifest.m_rollbackTo : "<无>");
                continue;
            }
        }
        /* §10 组合判定（优先级从高到低）：本地版本低于 min_version/
         * policy_forced_below → forced（无视组件策略与用户跳过）；跳过表仅在
         * 非 forced 时拦截（表行 2"不提示仅此版本，forced 仍生效"）。
         * skip_hit 读 state 跳过表：与用户线程 skipVersion 的写并发须持锁
         * （表满时 xup_skip_add 会整表平移，无锁读可见撕裂状态）。 */
        if (XUpgradeGray_forcedAnchored(&manifest, local)) anyForced = true;
        else if (!pinnedActive) {
            bool skipped;
            XMutex_lock(e->m_mutex);
            skipped = xup_skip_hit(st, c->m_id, c->m_version);
            XMutex_unlock(e->m_mutex);
            if (skipped) continue;
        }
        exact = xups_str_eq(c->m_os, targetOs);
        XMutex_lock(e->m_mutex);
        {
            XUpgradeTarget* existing = NULL;
            uint32_t k;
            for (k = 0; k < e->m_targetCount && k < XUPGRADE_COMPONENT_MAX; ++k) {
                if (xups_str_eq(e->m_targets[k].m_componentId, c->m_id)) {
                    existing = &e->m_targets[k];
                    break;
                }
            }
            if (existing) {
                /* 同 id 多 os 条目（如 win64 精确与 any 回退并存）：取精确
                 * os 条目（findComponent 同口径），any 条目不重复入表——
                 * 否则同组件双目标会双下载且 staged 标记互相覆盖。 */
                if (!existing->m_osExact && exact) {
                    xups_str_copy(existing->m_version, XUPGRADE_VERSION_MAX, c->m_version);
                    xups_str_copy(existing->m_file, XUPGRADE_FILENAME_MAX, c->m_file);
                    xups_str_copy(existing->m_sha256, XUPGRADE_SHA256_HEX_MAX, c->m_sha256);
                    existing->m_size = c->m_size;
                    existing->m_requires = c->m_requires;
                    existing->m_osExact = true;
                }
            } else if (e->m_targetCount >= XUPGRADE_STATE_COMPONENT_MAX) {
                /* 唯一组件目标数超出 state.json 组件表容量：组件级 staged 标记
                 * 将无处落盘，整单拒绝而非静默丢标记（不变式 1）。 */
                targetsOverflow = true;
            } else if (e->m_targetCount < XUPGRADE_COMPONENT_MAX) {
                XUpgradeTarget* t = &e->m_targets[e->m_targetCount++];
                XMemset(t, 0, sizeof(*t));
                xups_str_copy(t->m_componentId, XUPGRADE_COMPONENT_ID_MAX, c->m_id);
                xups_str_copy(t->m_version, XUPGRADE_VERSION_MAX, c->m_version);
                xups_str_copy(t->m_file, XUPGRADE_FILENAME_MAX, c->m_file);
                xups_str_copy(t->m_sha256, XUPGRADE_SHA256_HEX_MAX, c->m_sha256);
                t->m_size = c->m_size;
                t->m_requires = c->m_requires;
                t->m_osExact = exact;
                if (firstComp[0] == '\0') {
                    xups_str_copy(firstComp, sizeof(firstComp), c->m_id);
                    xups_str_copy(firstVer, sizeof(firstVer), c->m_version);
                }
                haveUpdate = true;
            }
        }
        XMutex_unlock(e->m_mutex);
    }
    if (targetsOverflow) {
        XMutex_lock(e->m_mutex);
        xup_reset_targets(e);
        XMutex_unlock(e->m_mutex);
        xup_set_state(e, XUpgradeState_CheckFailed);
        xup_emit_evt(e, XUpgradeEventType_CheckFailed, XUpgradeState_CheckFailed,
                     XUpgradePhase_Download, -1, "", "",
                     "检查失败：升级目标组件数超出引擎状态容量", true);
        xup_log(e, "检查失败：升级目标组件数超出状态表容量");
        return;
    }
    if (!haveUpdate) {
        xup_set_state(e, XUpgradeState_CheckFailed);
        xup_emit_evt(e, XUpgradeEventType_CheckFailed, XUpgradeState_CheckFailed,
                     XUpgradePhase_Download, -1, "", "",
                     downgradeRejected ? "暂无更新（目标版本低于防降级基线，已拒绝）"
                                       : "暂无更新",
                     true);
        xup_log(e, downgradeRejected ? "检查完成：候选版本被防降级硬门拒绝（DowngradeRejected）"
                                     : "检查完成：暂无更新");
        return;
    }
    /* §10 组合判定收口：任一组件被 forced 锚定 → 整单 forced（无视组件策略
     * 与用户跳过）；否则取 manifest 声明策略。同时复位自动 apply 一次标志
     * （新检查周期=新 Ready 周期）。 */
    XMutex_lock(e->m_mutex);
    e->m_policy = anyForced ? XUpgradePolicy_Forced : manifest.m_policy;
    e->m_readyAutoApplyDone = false;
    {
        XUpgradePolicy policy = e->m_policy;
        XUpgradeGrayDecision gray = e->m_grayDecision;
        /* forced/silent 引擎自动续跑（§10）：直接排队升级请求，复用工作线程
         * 调度（optional 停在 Checking 等待 XUpgrade_startUpgrade）。
         * installVersion 显式安装同口径自动续跑（用户已明确选择目标版本）。 */
        if ((policy != XUpgradePolicy_Optional || pinnedInstall) &&
            e->m_request == XUpgradeRequest_None)
            e->m_request = XUpgradeRequest_Upgrade;
        /* pinned 消费完毕：后续周期检查回到无指定语义。 */
        e->m_pinnedVersion[0] = '\0';
        XMutex_unlock(e->m_mutex);
        xup_logf(e, "发现更新 %s（%u 个组件待升级，生效策略=%d）",
                 manifest.m_version, (unsigned)e->m_targetCount, (int)policy);
        /* UpdateAvailable 语义事件：m_policy=生效策略、m_gray=灰度判定。 */
        xup_emit_update_available(e, firstComp, firstVer, policy, gray);
    }
}

/* ========================================================================== */
/*                          Verifying / Staging（§6）                           */
/* ========================================================================== */

/** @brief 整包 SHA-256 校验（失败删 partial 回 Idle，矩阵 2 口径）。 */
static bool xup_verify_target(XUpgradeEngine* e, const XUpgradeTarget* t)
{
    XString* root;
    XString* partialDir;
    XString* partPath;
    uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
    char partName[XUPGRADE_FILENAME_MAX];
    bool ok;
    root = xup_root_copy(e);
    if (!root) return false;
    partialDir = xup_path3(root, XUPGRADE_DIR_UPDATE "/" XUPGRADE_DIR_PARTIAL, NULL);
    XClassDelete(root);
    if (!partialDir) return false;
    if (!xup_part_name(t->m_file, partName, sizeof(partName))) return false;
    partPath = xup_path3(partialDir, partName, NULL);
    XClassDelete(partialDir);
    if (!partPath) return false;
    xup_emit_progress(e, XUpgradePhase_Verify, 0,
                      t->m_componentId, t->m_version);
    ok = xup_sha256_file(partPath, digest, e) &&
         xup_hex_eq_digest(t->m_sha256, digest, XUPGRADE_SHA256_BIN_SIZE);
    XClassDelete(partPath);
    if (ok) {
        xup_emit_progress(e, XUpgradePhase_Verify, 100,
                          t->m_componentId, t->m_version);
    } else {
        xup_log(e, "整包 SHA-256 校验失败：partial 作废（矩阵2）");
        xup_clear_partial(e);
        XMutex_lock(e->m_mutex);
        XMemset(&e->m_state.m_download, 0, sizeof(e->m_state.m_download));
        e->m_state.m_state = XUpgradeState_Idle;
        XMutex_unlock(e->m_mutex);
        xup_save_state(e);
    }
    return ok;
}

/** @brief .verify 清单条目。 */
typedef struct XUpVerifyEntry {
    char m_name[XUPGRADE_PACKAGE_NAME_MAX];
    char m_sha256[XUPGRADE_SHA256_HEX_MAX];
} XUpVerifyEntry;

/**
 * @brief 解包前对 .pak 做完整引擎层校验并产出 .verify 清单。
 * @details 头/条目表用 XUpgradePackage_parseHeader/parseEntry 流式契约解析；
 *          数据区整段 SHA-256 与逐条目解压后 SHA-256 双校验（§5 安全规则）。
 *          头+条目表按 headerSize 有界整读（上限见下），数据区只经 64KB 分块
 *          流式哈希、从不整包驻留内存（§5 流式口径；manifest size 无总量
 *          上限，嵌入端全量读入有 OOM 风险）。全部通过后引擎才允许 staging。
 * @param e 引擎对象；仅用于长流水内的锁心跳刷新，可为 NULL（不刷心跳）。
 */
static bool xup_verify_package(XUpgradeEngine* e, XString* pakPath,
                               const XUpgradeManifestComponent* mc,
                               XUpVerifyEntry* verifyList, uint32_t* verifyCount)
{
    XFile* file;
    XByteArray* table = NULL;
    XUpgradePackageHeader header;
    XUpgradePackageEntry entry;
    uint64_t* entryOffsets = NULL;
    size_t pos;
    uint32_t i;
    uint64_t fileSize;
    uint64_t payloadOffset;
    bool ok = false;
    if (!pakPath || !mc || !verifyList || !verifyCount) return false;
    *verifyCount = 0;
    file = XFile_create_2(pakPath);
    if (!file) return false;
    if (!XFile_open_2(file, XIODevice_ReadOnly, 0)) { XClassDelete((XClass*)file); return false; }
    fileSize = (uint64_t)XIODevice_size_base((XIODevice*)file);
    /* 流式读头：先读定长头取 headerSize，再按其有界读条目表；数据区不驻留。 */
    if (!XIODevice_seek_base((XIODevice*)file, 0)) goto done;
    {
        uint8_t fixed[XUPGRADE_PACKAGE_FIXED_HEADER_SIZE];
        if (XIODevice_read_1((XIODevice*)file, (char*)fixed, (int64_t)sizeof(fixed))
            != (int64_t)sizeof(fixed)) goto done;
        if (!XUpgradePackage_parseHeader(&header, fixed, sizeof(fixed))) goto done;
    }
    if (header.m_entryCount == 0 || header.m_entryCount > 65535) goto done;
    /* 条目表容量上限：65535 条 × (nameMax-1+59) wire 字节（parseEntry 口径）。 */
    if ((uint64_t)header.m_headerSize >
        XUPGRADE_PACKAGE_FIXED_HEADER_SIZE +
        65535ull * ((uint64_t)XUPGRADE_PACKAGE_NAME_MAX - 1ull + 59ull)) goto done;
    if ((uint64_t)header.m_headerSize > fileSize) goto done;
    {
        uint8_t* headBuf = (uint8_t*)XMalloc_System((size_t)header.m_headerSize);
        if (!headBuf) goto done;
        if (!XIODevice_seek_base((XIODevice*)file, 0) ||
            XIODevice_read_1((XIODevice*)file, (char*)headBuf,
                             (int64_t)header.m_headerSize) != (int64_t)header.m_headerSize) {
            XFree_System(headBuf);
            goto done;
        }
        table = XByteArray_create_with_data((const char*)headBuf,
                                            (int64_t)header.m_headerSize);
        XFree_System(headBuf);
        if (!table) goto done;
    }
    if (!XUpgradePackage_parseHeader(&header, XByteArray_constData(table),
                                     XByteArray_size_base(table)))
        goto done;
    if ((uint64_t)header.m_headerSize > XByteArray_size_base(table)) goto done;
    if ((uint64_t)header.m_headerSize + header.m_payloadSize != fileSize) goto done;
    /* manifest size=包文件总字节数（契约口径 XUpgradeManifest.h:96；
     * 下载进度与磁盘余量预检同口径消费）。 */
    if (fileSize != (uint64_t)mc->m_size) goto done;
    /* 第一遍：流式解析条目表，登记 .verify 清单（上限截断不影响校验）；
     * 同时记录各条目起始偏移（65535×8B≈512KB 上限），供第二遍直达解析——
     * 否则第二遍逐条目从包头重扫，parseEntry 调用数为 n(n+1)/2 量级。 */
    entryOffsets = (uint64_t*)XMalloc_System(sizeof(uint64_t) * header.m_entryCount);
    if (!entryOffsets) goto done;
    pos = XUPGRADE_PACKAGE_FIXED_HEADER_SIZE;
    for (i = 0; i < header.m_entryCount; ++i) {
        /* parseEntry 的 consumed 是"本次消耗"（赋值语义），必须显式累加——
         * 把累计游标直接当 consumed 指针会被覆盖，多条目表自第 2 条起全部
         * 错位（2026-10-07 真包 staging 校验失败根因：单条目夹具掩盖）。 */
        size_t consumed = 0;
        entryOffsets[i] = (uint64_t)pos;
        if (!XUpgradePackage_parseEntry(&entry, XByteArray_constData(table) + pos,
                                        XByteArray_size_base(table) - pos, &consumed))
            goto done;
        pos += consumed;
        if (entry.m_flags & (uint8_t)~XUPGRADE_PACKAGE_FLAG_ZLIB) goto done;
        if (entry.m_rawSize > XUPGRADE_ENTRY_MAX_RAW || entry.m_compSize > XUPGRADE_ENTRY_MAX_RAW)
            goto done;
        if (entry.m_flags & XUPGRADE_PACKAGE_FLAG_ZLIB) {
            if (entry.m_compSize == 0 || entry.m_rawSize == 0) goto done;
        } else if (entry.m_compSize != entry.m_rawSize) {
            goto done;
        }
        if (entry.m_offset + entry.m_compSize > header.m_payloadSize) goto done;
        if (*verifyCount < XUPGRADE_VERIFY_LIST_MAX) {
            xups_str_copy(verifyList[*verifyCount].m_name, XUPGRADE_PACKAGE_NAME_MAX, entry.m_name);
            xup_bin2hex(entry.m_sha256, XUPGRADE_SHA256_BIN_SIZE,
                        verifyList[*verifyCount].m_sha256);
            ++*verifyCount;
        }
    }
    payloadOffset = XUpgradePackage_payloadOffset(&header);
    if (payloadOffset != header.m_headerSize) goto done;
    /* 数据区整段 SHA-256（下载校验是整文件哈希，此处复核容器数据区）。 */
    {
        XCryptographicHash hash;
        uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
        uint8_t* buf = (uint8_t*)XMalloc_System(XUPGRADE_HASH_BUF_SIZE);
        uint64_t remain = header.m_payloadSize;
        bool payloadOk = false;
        if (!buf) goto done;
        if (XIODevice_seek_base((XIODevice*)file, (int64_t)payloadOffset)) {
            XCryptographicHash_init(&hash, XCryptographicHash_Sha256);
            while (remain > 0) {
                int64_t n = XIODevice_read_1((XIODevice*)file, (char*)buf,
                                             (int64_t)(remain < XUPGRADE_HASH_BUF_SIZE
                                                           ? remain : XUPGRADE_HASH_BUF_SIZE));
                if (n <= 0) break;
                XCryptographicHash_addData_2(&hash, XByteArrayView_create_data(buf, n));
                remain -= (uint64_t)n;
                xup_lock_heartbeat(e); /* 长流水内刷新引擎锁心跳（不变式 6） */
            }
            {
                XByteArrayView rv = XCryptographicHash_resultView(&hash);
                if (XByteArrayView_size(&rv) == XUPGRADE_SHA256_BIN_SIZE && remain == 0) {
                    XMemcpy(digest, XByteArrayView_data(&rv), XUPGRADE_SHA256_BIN_SIZE);
                    payloadOk = XMemcmp(digest, header.m_payloadSha256,
                                        XUPGRADE_SHA256_BIN_SIZE) == 0;
                }
            }
            XCryptographicHash_deinit(&hash);
        }
        XFree_System(buf);
        if (!payloadOk) goto done;
    }
    /* 第二遍：逐条目读出→（按需解压）→内容 SHA-256。
     * 条目起始偏移取自第一遍登记表，每条目仅 parseEntry 一次（总计 2n 次）；
     * 长流水内照例刷新引擎锁心跳（不变式 6）。 */
    {
        uint8_t* compBuf = NULL;
        uint8_t* rawBuf = NULL;
        size_t compCap = 0;
        size_t rawCap = 0;
        uint32_t k;
        for (k = 0; k < header.m_entryCount; ++k) {
            size_t scan = (size_t)entryOffsets[k];
            if (!XUpgradePackage_parseEntry(&entry, XByteArray_constData(table) + scan,
                                            XByteArray_size_base(table) - scan, &scan))
                goto entry_fail;
            if (compCap < entry.m_compSize) {
                if (compBuf) XFree_System(compBuf);
                compBuf = (uint8_t*)XMalloc_System((size_t)entry.m_compSize);
                compCap = compBuf ? (size_t)entry.m_compSize : 0;
            }
            if (rawCap < entry.m_rawSize) {
                if (rawBuf) XFree_System(rawBuf);
                rawBuf = (uint8_t*)XMalloc_System((size_t)entry.m_rawSize);
                rawCap = rawBuf ? (size_t)entry.m_rawSize : 0;
            }
            if (!compBuf || !rawBuf || compCap < entry.m_compSize || rawCap < entry.m_rawSize)
                goto entry_fail;
            if (!XIODevice_seek_base((XIODevice*)file,
                                     (int64_t)(payloadOffset + entry.m_offset)))
                goto entry_fail;
            if (XIODevice_read_1((XIODevice*)file, (char*)compBuf,
                                 (int64_t)entry.m_compSize) != (int64_t)entry.m_compSize)
                goto entry_fail;
            if (entry.m_flags & XUPGRADE_PACKAGE_FLAG_ZLIB) {
                uLongf destLen = (uLongf)entry.m_rawSize;
                if (uncompress(rawBuf, &destLen, compBuf, (uLong)entry.m_compSize) != Z_OK ||
                    destLen != (uLongf)entry.m_rawSize)
                    goto entry_fail;
            } else {
                XMemcpy(rawBuf, compBuf, (size_t)entry.m_compSize);
            }
            {
                uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
                if (!xups_sha256_bytes(rawBuf, (size_t)entry.m_rawSize, digest)) goto entry_fail;
                if (XMemcmp(digest, entry.m_sha256, XUPGRADE_SHA256_BIN_SIZE) != 0) goto entry_fail;
            }
            xup_lock_heartbeat(e); /* 逐条目解压/哈希期间刷新引擎锁心跳（不变式 6） */
            continue;
        entry_fail:
            if (compBuf) { XFree_System(compBuf); compBuf = NULL; }
            if (rawBuf) { XFree_System(rawBuf); rawBuf = NULL; }
            goto done;
        }
        if (compBuf) XFree_System(compBuf);
        if (rawBuf) XFree_System(rawBuf);
    }
    ok = true;
done:
    if (entryOffsets) XFree_System(entryOffsets);
    if (table) XClassDelete(table);
    XIODevice_close_base((XIODevice*)file);
    XClassDelete((XClass*)file);
    return ok;
}

/**
 * @brief 把已校验的 .pak 逐条目解包写入 staging 临时目录（设计 §6 Staging 口径）。
 * @details 条目表解析与 xup_verify_package 同源；逐条目读出→按需 zlib 解压→
 *          写 <stagingDir>/<entryName>（含子目录条目，父目录按需创建）。写入
 *          失败返回 false，由调用方走 stageEnd 作废 + 半成品清理。目录随
 *          stageEnd 整体改名落位，条目内容哈希已在校验遍完成，此处不再重算。
 */
static bool xup_stage_unpack(XUpgradeEngine* e, XString* pakPath, XString* stagingDir,
                             const char* componentId, const char* version)
{
    XFile* file;
    XUpgradePackageHeader header;
    XUpgradePackageEntry entry;
    uint8_t fixed[XUPGRADE_PACKAGE_FIXED_HEADER_SIZE];
    uint8_t* table = NULL;
    uint8_t* compBuf = NULL;
    uint8_t* rawBuf = NULL;
    size_t compCap = 0;
    size_t rawCap = 0;
    size_t pos;
    uint32_t i;
    uint64_t payloadOffset;
    bool ok = false;
    if (!pakPath || !stagingDir) return false;
    file = XFile_create_2(pakPath);
    if (!file) return false;
    if (!XFile_open_2(file, XIODevice_ReadOnly, 0)) goto done;
    if (XIODevice_read_1((XIODevice*)file, (char*)fixed, (int64_t)sizeof(fixed))
        != (int64_t)sizeof(fixed)) goto done;
    if (!XUpgradePackage_parseHeader(&header, fixed, sizeof(fixed))) goto done;
    if (header.m_entryCount == 0 || header.m_entryCount > 65535) goto done;
    if ((uint64_t)header.m_headerSize >
        XUPGRADE_PACKAGE_FIXED_HEADER_SIZE +
        65535ull * ((uint64_t)XUPGRADE_PACKAGE_NAME_MAX - 1ull + 59ull)) goto done;
    table = (uint8_t*)XMalloc_System((size_t)header.m_headerSize);
    if (!table) goto done;
    if (!XIODevice_seek_base((XIODevice*)file, 0) ||
        XIODevice_read_1((XIODevice*)file, (char*)table,
                         (int64_t)header.m_headerSize) != (int64_t)header.m_headerSize)
        goto done;
    payloadOffset = XUpgradePackage_payloadOffset(&header);
    pos = XUPGRADE_PACKAGE_FIXED_HEADER_SIZE;
    ok = true;
    for (i = 0; ok && i < header.m_entryCount; ++i) {
        XString* entryPath;
        /* consumed 赋值语义：显式累加（同 verify_package 第一遍，多条目错位
         * 修复 2026-10-07）。 */
        size_t consumed = 0;
        /* 解包进度（§12 Staging 段）：逐条目上报百分比。 */
        xup_emit_progress(e, XUpgradePhase_Stage,
                          (int)(((uint64_t)i * 100u) / header.m_entryCount),
                          componentId, version);
        if (!XUpgradePackage_parseEntry(&entry, table + pos,
                                        (size_t)header.m_headerSize - pos, &consumed)) {
            ok = false;
            break;
        }
        pos += consumed;
        if (compCap < entry.m_compSize) {
            if (compBuf) XFree_System(compBuf);
            compBuf = (uint8_t*)XMalloc_System((size_t)entry.m_compSize);
            compCap = compBuf ? (size_t)entry.m_compSize : 0;
        }
        if (rawCap < entry.m_rawSize) {
            if (rawBuf) XFree_System(rawBuf);
            rawBuf = (uint8_t*)XMalloc_System((size_t)entry.m_rawSize);
            rawCap = rawBuf ? (size_t)entry.m_rawSize : 0;
        }
        if (!compBuf || !rawBuf || compCap < entry.m_compSize || rawCap < entry.m_rawSize) {
            ok = false;
            break;
        }
        if (!XIODevice_seek_base((XIODevice*)file,
                                 (int64_t)(payloadOffset + entry.m_offset)) ||
            XIODevice_read_1((XIODevice*)file, (char*)compBuf,
                             (int64_t)entry.m_compSize) != (int64_t)entry.m_compSize) {
            ok = false;
            break;
        }
        if (entry.m_flags & XUPGRADE_PACKAGE_FLAG_ZLIB) {
            uLongf destLen = (uLongf)entry.m_rawSize;
            if (uncompress(rawBuf, &destLen, compBuf, (uLong)entry.m_compSize) != Z_OK ||
                destLen != (uLongf)entry.m_rawSize) {
                ok = false;
                break;
            }
        } else {
            XMemcpy(rawBuf, compBuf, (size_t)entry.m_compSize);
        }
        /* 子目录条目（如 "docs/abc"）：先按需创建父目录再写文件。 */
        {
            size_t len = xups_cstr_len(entry.m_name);
            size_t slash = len;
            while (slash > 0 && entry.m_name[slash - 1] != '/') --slash;
            if (slash > 0) {
                char parentRel[XUPGRADE_PACKAGE_NAME_MAX];
                XString* parentAbs;
                if (slash - 1 >= sizeof(parentRel)) { ok = false; break; }
                XMemcpy(parentRel, entry.m_name, slash - 1);
                parentRel[slash - 1] = '\0';
                parentAbs = xup_path3(stagingDir, parentRel, NULL);
                ok = parentAbs != NULL && xup_make_dirs(parentAbs);
                if (parentAbs) XClassDelete(parentAbs);
                if (!ok) break;
            }
        }
        entryPath = xup_path3(stagingDir, entry.m_name, NULL);
        if (!entryPath) { ok = false; break; }
        ok = xup_write_file_atomic(entryPath, (const char*)rawBuf,
                                   (size_t)entry.m_rawSize);
        XClassDelete(entryPath);
        xup_lock_heartbeat(e); /* 长条目解包期间刷新引擎锁心跳（不变式 6） */
    }
done:
    if (compBuf) XFree_System(compBuf);
    if (rawBuf) XFree_System(rawBuf);
    if (table) XFree_System(table);
    if (file) {
        XIODevice_close_base((XIODevice*)file);
        XClassDelete((XClass*)file);
    }
    return ok;
}

/**
 * @brief staging 一个组件：逐条目解包落盘 + 标记文件 + 后端原子落位。
 * @details 流程（设计 §6/§7）：stageBegin（磁盘余量预检）→ 引擎按确定性路径
 *          （<root>/update/staging/<comp>/<ver>/，XUPGRADE_STOREDIR_DIR_STAGING）
 *          把已校验 .pak 的条目逐个写入临时目录并补写 manifest.version/.verify
 *          → stageEnd 把临时目录整体原子改名为版本目录。条目文件随目录落位，
 *          自检 .verify 抽查打开的即真实条目文件（真实多条目包可用，单流
 *          stageWrite 通道保留为后端后备，本流程不使用）。
 *          不变式 1 收敛（评审#1）：stageEnd 落位后、staged 标志落盘前断电
 *          （或自检回滚后服务端重发同版本）会留下完整孤儿目录令 stageBegin
 *          永久拒绝——本函数入口先清理"无 staged 标志指向且非当前激活版本"
 *          的同名目录，使该版本号可安全重装。
 */
static bool xup_stage_target(XUpgradeEngine* e, const XUpgradeTarget* t)
{
    XString* root;
    XString* partialDir;
    XString* pakPath;
    XUpVerifyEntry* verifyList;
    uint32_t verifyCount = 0;
    const XUpgradeManifestComponent* mc;
    char targetOs[XUPGRADE_OS_MAX];
    char partName[XUPGRADE_FILENAME_MAX];
    bool ok = false;
    XMutex_lock(e->m_mutex);
    xups_str_copy(targetOs, sizeof(targetOs),
                 e->m_targetOs ? XString_toUtf8(e->m_targetOs) : "");
    mc = e->m_hasManifest
             ? XUpgradeManifest_findComponent(&e->m_manifest, t->m_componentId, targetOs)
             : NULL;
    XMutex_unlock(e->m_mutex);
    if (!mc) return false;
    verifyList = (XUpVerifyEntry*)XMalloc_System(sizeof(XUpVerifyEntry) * XUPGRADE_VERIFY_LIST_MAX);
    if (!verifyList) return false;
    root = xup_root_copy(e);
    if (!root) { XFree_System(verifyList); return false; }
    partialDir = xup_path3(root, XUPGRADE_DIR_UPDATE "/" XUPGRADE_DIR_PARTIAL, NULL);
    if (!xup_part_name(t->m_file, partName, sizeof(partName))) {
        XClassDelete(root);
        XFree_System(verifyList);
        return false;
    }
    pakPath = partialDir ? xup_path3(partialDir, partName, NULL) : NULL;
    if (partialDir) XClassDelete(partialDir);
    if (!pakPath) { XClassDelete(root); XFree_System(verifyList); return false; }
    /* 不变式 1 收敛：孤儿版本目录清理。能走到 staging 的版本必非当前激活
     * 版本（check 按 compareVersions>0 产出目标）；仍防御性复核指针后才删。
     * 多组件断电语义（state.json 组件级 staged 标记）：孤儿目录可能正被
     * 组件级 staged 标记指向（上一轮 staged 后未及 apply 断电、本轮重发同
     * 版本）——先清标记再删目录，保持"标记在⇔目录在"的镜像关系，防 apply
     * 桩对着已删目录 activate；本次 staging 成功后标记随 Ready 原子重写。 */
    {
        XString* finalDir = xup_path3(root, XUPGRADE_DIR_VERSIONS, t->m_componentId);
        if (finalDir) {
            if (XString_append_utf8(finalDir, "/") &&
                XString_append_utf8(finalDir, t->m_version) &&
                XFileInfo_exists_static(finalDir)) {
                char active[XUPGRADE_VERSION_MAX];
                bool isActive = xup_read_pointer(e, t->m_componentId,
                                                 active, sizeof(active)) &&
                                xups_str_eq(active, t->m_version);
                if (!isActive) {
                    bool stagedCleared = false;
                    XMutex_lock(e->m_mutex);
                    {
                        XUpgradeStateComponent* rec =
                            xup_state_component(&e->m_state, t->m_componentId, false);
                        if (rec && rec->m_staged &&
                            xups_str_eq(rec->m_stagedVersion, t->m_version)) {
                            rec->m_staged = false;
                            rec->m_stagedVersion[0] = '\0';
                            stagedCleared = true;
                        }
                    }
                    XMutex_unlock(e->m_mutex);
                    if (stagedCleared) xup_save_state(e);
                    xup_logf(e, "staging：清理孤儿版本目录 %s@%s（激活指针与 staged 标记均未指向）",
                             t->m_componentId, t->m_version);
                    xup_remove_dir_recursively(finalDir);
                }
            }
            XClassDelete(finalDir);
        }
    }
    if (!xup_verify_package(e, pakPath, mc, verifyList, &verifyCount)) {
        xup_logf(e, "staging：%s 包校验失败", t->m_componentId);
        XClassDelete(pakPath);
        XClassDelete(root);
        XFree_System(verifyList);
        return false;
    }
    /* stage 流：begin → 引擎解包写条目+标记 → end 原子落位。失败路径调
     * stageEnd 复位后端 staging 状态（后端随即将临时目录改名落位为不完整
     * 版本目录，由流水层半成品清理收敛），中途不做落盘级 stop 检查。 */
    {
        const char* errText = NULL;
        XString* stagingDir = NULL;
        ok = XUpgradeStore_stageBegin_base(e->m_store, t->m_componentId, t->m_version,
                                           mc->m_size, &errText) == XUpgradeError_Ok;
        if (!ok && errText) xup_logf(e, "staging 后端错误：%s", errText);
        if (ok) {
            stagingDir = xup_path3(root,
                                   XUPGRADE_DIR_UPDATE "/" XUPGRADE_STOREDIR_DIR_STAGING,
                                   t->m_componentId);
            if (stagingDir) {
                if (!XString_append_utf8(stagingDir, "/") ||
                    !XString_append_utf8(stagingDir, t->m_version)) {
                    XClassDelete(stagingDir);
                    stagingDir = NULL;
                }
            }
            ok = stagingDir != NULL;
        }
        if (ok) ok = xup_stage_unpack(e, pakPath, stagingDir,
                                      t->m_componentId, t->m_version);
        /* 标记文件写入 staging 临时目录，随 stageEnd 改名一起落位：目录一旦
         * 落位即"标记+条目清单+条目"齐备（manifest.version 兜底覆盖写，包内
         * 未含该条目时亦保证自检标记口径）。 */
        if (ok) {
            XString* markPath = xup_path3(stagingDir, XUPGRADE_FILE_VERSION_MARK, NULL);
            if (markPath) {
                ok = xup_write_file_atomic(markPath, t->m_version, xups_cstr_len(t->m_version));
                XClassDelete(markPath);
            } else ok = false;
        }
        if (ok) {
            XString* verifyPath = xup_path3(stagingDir, XUPGRADE_STORE_DIR_VERIFY_FILE, NULL);
            if (verifyPath) {
                XString* content = XString_create_utf8("");
                uint32_t v;
                for (v = 0; content && v < verifyCount; ++v) {
                    XString* line = XString_create_fmt_utf8("%s %s\n",
                                                            verifyList[v].m_name,
                                                            verifyList[v].m_sha256);
                    if (!line || !XString_append(content, line)) {
                        if (line) XClassDelete(line);
                        XClassDelete(content);
                        content = NULL;
                        break;
                    }
                    XClassDelete(line);
                }
                if (content) {
                    ok = xup_write_file_atomic(verifyPath, XString_toUtf8(content),
                                               XString_toUtf8_length(content));
                    XClassDelete(content);
                } else ok = false;
            } else ok = false;
        }
        if (ok) {
            ok = XUpgradeStore_stageEnd_base(e->m_store, &errText) == XUpgradeError_Ok;
        } else {
            /* 作废半成品流：stageEnd 复位后端 staging 状态（契约口径）。 */
            (void)XUpgradeStore_stageEnd_base(e->m_store, &errText);
        }
        if (stagingDir) XClassDelete(stagingDir);
        if (!ok && errText) xup_logf(e, "staging 后端错误：%s", errText);
    }
    XClassDelete(root);
    XClassDelete(pakPath);
    XFree_System(verifyList);
    return ok;
}

/**
 * @brief requires 配套校验（staging 完成时、staged 标记落盘前；不变式 1 前置）。
 * @details 逐目标检查 manifest 声明的依赖约束（契约口径：操作符 ">="，其余
 *          一律不满足，§3）。被依赖组件版本取值顺序：本轮同升目标（apply 时
 *          一起激活）→ state.json 记录的当前激活版本 → 记录已 staged 的版本
 *          （apply 桩会把全部 staged 记录与本次一起激活，配套关系在激活后
 *          成立）。任一约束不满足即整单不激活，由调用方作废 staging 半成品。
 * @note 约束串取自检查时的目标快照（XUpgradeTarget.m_requires），不再回查
 *       manifest；state.json 记录按结构体契约经 m_mutex 读取（用户线程
 *       skipVersion 等并发改写同一镜像）。
 */
static bool xup_check_requires(XUpgradeEngine* e)
{
    uint32_t i;
    for (i = 0; i < e->m_targetCount && i < XUPGRADE_COMPONENT_MAX; ++i) {
        const XUpgradeTarget* t = &e->m_targets[i];
        uint32_t r;
        for (r = 0; r < t->m_requires.m_count && r < XUPGRADE_REQUIRES_MAX; ++r) {
            const char* dep = t->m_requires.m_component[r];
            const char* constraint = t->m_requires.m_constraint[r];
            char have[XUPGRADE_VERSION_MAX];
            char need[XUPGRADE_CONSTRAINT_MAX];
            size_t len;
            uint32_t k;
            bool found = false;
            have[0] = '\0';
            xups_str_copy(need, sizeof(need), constraint);
            len = xups_cstr_len(need);
            /* 仅支持 ">=X"（契约：操作符为 ">="）；其余一律不满足。 */
            if (need[0] != '>' || need[1] != '=' || len < 3) return false;
            XMemmove(need, need + 2, len - 1); /* 连 NUL 一起前移 */
            if (!xups_version_valid(need)) return false;
            /* 被依赖组件版本：本轮同升目标优先。 */
            for (k = 0; k < e->m_targetCount && k < XUPGRADE_COMPONENT_MAX; ++k) {
                if (xups_str_eq(e->m_targets[k].m_componentId, dep)) {
                    xups_str_copy(have, sizeof(have), e->m_targets[k].m_version);
                    found = true;
                    break;
                }
            }
            if (!found) {
                XUpgradeStateComponent* rec;
                XMutex_lock(e->m_mutex);
                rec = xup_state_component(&e->m_state, dep, false);
                if (rec) {
                    xups_str_copy(have, sizeof(have), rec->m_activeVersion);
                    if (have[0] == '\0' && rec->m_staged)
                        xups_str_copy(have, sizeof(have), rec->m_stagedVersion);
                }
                XMutex_unlock(e->m_mutex);
                found = have[0] != '\0';
            }
            if (!found) return false;
            if (XUpgradeManifest_compareVersions(have, need) < 0) return false;
        }
    }
    return true;
}

/** @brief staging 失败清理：半成品目录 + partial + 游标，回 Idle（矩阵 3）。 */
static void xup_upgrade_abort_staging(XUpgradeEngine* e, const char* reason)
{
    uint32_t k;
    /* root 锁外预取（remove_half 内部不再取锁，防同线程重锁自死锁）。 */
    XString* root = xup_root_copy(e);
    XMutex_lock(e->m_mutex);
    for (k = 0; k < e->m_targetCount && k < XUPGRADE_COMPONENT_MAX; ++k)
        xup_remove_half_version_dirs(e, root, e->m_targets[k].m_componentId, NULL);
    XMemset(&e->m_state.m_download, 0, sizeof(e->m_state.m_download));
    e->m_state.m_state = XUpgradeState_Idle;
    XMutex_unlock(e->m_mutex);
    if (root) XClassDelete(root);
    xup_save_state(e);
    xup_emit(e, XUpgradeState_Idle, XUpgradePhase_Stage, -1, "", "", reason, false);
    xup_logf(e, "%s：半成品已清理（矩阵3）", reason);
}

/**
 * @brief 升级流水：Downloading → Verifying → Staging → Ready（§6）。
 *        失败分支：下载网络错=DownloadFailed 保留 partial；整包校验错=
 *        partial 作废回 Idle；staging/requires 错=半成品清理回 Idle。
 */
static void xup_run_upgrade(XUpgradeEngine* e)
{
    uint32_t order[XUPGRADE_COMPONENT_MAX];
    uint32_t i;
    bool ok = true;
    bool downloadFailed = false;
    char detail[XUPGRADE_DETAIL_MAX];
#if XUPGRADE_HTTP_ON
    XNetworkAccessManager* mgr = NULL;
#endif
    if (e->m_targetCount == 0 || e->m_targetCount > XUPGRADE_COMPONENT_MAX) return;
    /* 多组件下载顺序（§6）：按 requires 依赖序推进（fontres 先于依赖它的
     * app）；目标表本身不改序，UpdateAvailable/Ready 事件的组件字段仍取
     * manifest 声明序首项。 */
    xup_targets_download_order(e->m_targets, e->m_targetCount, order);
    xup_set_state(e, XUpgradeState_Downloading);
#if XUPGRADE_HTTP_ON
    mgr = xup_http_mgr_create();
    ok = mgr != NULL;
    if (!ok) xup_log(e, "HTTP 管理器创建失败");
    for (i = 0; ok && i < e->m_targetCount; ++i) {
        const XUpgradeTarget* t = &e->m_targets[order[i]];
        XUpDownloadResult r = xup_download_target(e, mgr, t);
        if (r == XUpDl_Ok) continue;
        ok = false;
        if (r == XUpDl_Aborted) goto cleanup; /* 停机：partial/游标保留，状态停在 Downloading */
        downloadFailed = true;
        xup_logf(e, "下载失败：%s@%s（partial 保留供续传）",
                 t->m_componentId, t->m_version);
    }
    /* Verifying：逐包整包 SHA-256（失败者自行收敛回 Idle）。 */
    if (ok) {
        xup_set_state(e, XUpgradeState_Verifying);
        for (i = 0; ok && i < e->m_targetCount; ++i) {
            const XUpgradeTarget* t = &e->m_targets[order[i]];
            xup_emit_progress(e, XUpgradePhase_Verify, 0,
                              t->m_componentId, t->m_version);
            if (!xup_verify_target(e, t)) ok = false;
        }
    }
cleanup:
    if (mgr) xup_http_mgr_destroy(mgr); /* 停机中止路径也必须释放（评审#6 泄漏） */
#else
    ok = false;
    downloadFailed = false;
    xup_log(e, "升级被拒：当前构建未编入 HTTP 下载通道");
#endif
    if (!ok) {
        if (downloadFailed) {
            /* DownloadFailed：保留 partial 供续传。 */
            XMutex_lock(e->m_mutex);
            e->m_state.m_state = XUpgradeState_DownloadFailed;
            XMutex_unlock(e->m_mutex);
            xup_save_state(e);
            xups_str_copy(detail, sizeof(detail), "下载失败：partial 已保留供续传");
            xup_emit(e, XUpgradeState_DownloadFailed, XUpgradePhase_Download, -1,
                     "", "", detail, false);
        }
        return;
    }
    /* Staging：逐组件解包落盘（依赖序与下载一致）。 */
    xup_set_state(e, XUpgradeState_Staging);
    for (i = 0; ok && i < e->m_targetCount; ++i) {
        const XUpgradeTarget* t = &e->m_targets[order[i]];
        xup_emit_progress(e, XUpgradePhase_Stage, 0,
                          t->m_componentId, t->m_version);
        if (!xup_stage_target(e, t)) ok = false;
        else {
            xup_emit_progress(e, XUpgradePhase_Stage, 100,
                              t->m_componentId, t->m_version);
        }
    }
    if (!ok) {
        xup_upgrade_abort_staging(e, "staging 失败");
        return;
    }
    /* requires 配套校验（staging 完成时；不变式 1 前置）：app 对 fontres 等
     * 版本约束不满足即整单不激活（§3）。 */
    if (!xup_check_requires(e)) {
        xup_upgrade_abort_staging(e, "requires 配套校验不满足，staging 作废");
        xup_log(e, "requires 校验失败：整单不激活");
        return;
    }
    /* 不变式 1：全部校验通过后才写 staged 标记（一次落盘）。 */
    {
        uint32_t k;
        XMutex_lock(e->m_mutex);
        for (k = 0; k < e->m_targetCount; ++k) {
            XUpgradeStateComponent* rec =
                xup_state_component(&e->m_state, e->m_targets[k].m_componentId, true);
            if (rec) {
                xups_str_copy(rec->m_stagedVersion, XUPGRADE_VERSION_MAX, e->m_targets[k].m_version);
                rec->m_staged = true;
            }
        }
        XMemset(&e->m_state.m_download, 0, sizeof(e->m_state.m_download));
        e->m_state.m_state = XUpgradeState_Ready;
        XMutex_unlock(e->m_mutex);
        xup_save_state(e);
    }
    xup_clear_partial(e);
    xup_log(e, "staging 完成：staged 标记已写入（Ready）");
    /* §10 策略时机收口：forced=staged 即自动进入 apply（ReadyAutoApply）；
     * silent=维护窗口内自动应用、窗口外（含未启用窗口=桌面"退出时应用"
     * 缺省）推 ReadyNeedRestart，窗口到达由工作线程周期判定顺延触发；
     * optional=ReadyNeedRestart 等待用户重启/applyNow。 */
    {
        XUpgradePolicy policy;
        XUpgradeApplyWindow window;
        XMutex_lock(e->m_mutex);
        policy = e->m_policy;
        window = e->m_applyWindow; /* 快照：与 setApplyWindow/tick 同锁纪律 */
        XMutex_unlock(e->m_mutex);
        if (policy == XUpgradePolicy_Forced ||
            (policy == XUpgradePolicy_Silent && window.m_enabled &&
             xup_policy_window_now(&window))) {
            xup_policy_auto_apply(e, policy == XUpgradePolicy_Forced
                                         ? "forced：staged 完成自动应用"
                                         : "silent：维护窗口到达自动应用");
        } else {
            xup_emit_evt(e, XUpgradeEventType_ReadyNeedRestart, XUpgradeState_Ready,
                         XUpgradePhase_Activate, -1,
                         e->m_targets[0].m_componentId, e->m_targets[0].m_version,
                         "升级就绪，重启以应用", true);
        }
    }
}

/* ========================================================================== */
/*                          启动自检（§8.3）                                    */
/* ========================================================================== */

/** @brief .verify 哈希抽查：逐条比对版本目录内文件摘要（§8.3 步骤 2）。 */
static bool xup_verify_spot_check(XUpgradeEngine* e, const char* componentId, const char* version)
{
    XString* root;
    XString* verifyPath;
    XByteArray* bytes = NULL;
    char* text = NULL;
    size_t pos = 0;
    size_t len;
    uint32_t lines = 0;
    bool ok = true;
    root = xup_root_copy(e);
    if (!root) return false;
    verifyPath = xup_path3(root, XUPGRADE_DIR_VERSIONS, componentId);
    XClassDelete(root);
    if (!verifyPath) return false;
    if (!XString_append_utf8(verifyPath, "/") || !XString_append_utf8(verifyPath, version) ||
        !XString_append_utf8(verifyPath, "/" XUPGRADE_STORE_DIR_VERIFY_FILE)) {
        XClassDelete(verifyPath);
        return false;
    }
    if (!xup_read_file(verifyPath, &bytes) || !bytes) {
        /* 清单缺失（旧版本/被清理）：指针+版本标记已校验，抽查跳过。 */
        XClassDelete(verifyPath);
        return true;
    }
    XClassDelete(verifyPath);
    len = XByteArray_size_base(bytes);
    text = (char*)XMalloc_System(len + 1);
    if (!text) { XClassDelete(bytes); return false; }
    XMemcpy(text, XByteArray_constData(bytes), len);
    text[len] = '\0';
    XClassDelete(bytes);
    while (text[pos] != '\0' && lines < XUPGRADE_VERIFY_LIST_MAX) {
        char* line = text + pos;
        char* nl = (char*)XStrchr(line, '\n');
        char* sp;
        XString* root2;
        XString* filePath;
        uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
        if (nl) *nl = '\0';
        pos += xups_cstr_len(line) + 1;
        xups_trim(line);
        if (line[0] == '\0') continue;
        sp = (char*)XStrchr(line, ' ');
        if (!sp) { ok = false; break; }
        *sp = '\0';
        ++sp;
        /* 路径复用包条目白名单关卡（防逃逸）。 */
        if (!XUpgradePackage_checkEntryName(line)) { ok = false; break; }
        root2 = xup_root_copy(e);
        if (!root2) { ok = false; break; }
        filePath = xup_path3(root2, XUPGRADE_DIR_VERSIONS, componentId);
        XClassDelete(root2);
        if (!filePath) { ok = false; break; }
        if (!XString_append_utf8(filePath, "/") || !XString_append_utf8(filePath, version) ||
            !XString_append_utf8(filePath, "/") || !XString_append_utf8(filePath, line)) {
            XClassDelete(filePath);
            ok = false;
            break;
        }
        if (!xup_sha256_file(filePath, digest, NULL) ||
            !xup_hex_eq_digest(sp, digest, XUPGRADE_SHA256_BIN_SIZE)) {
            XClassDelete(filePath);
            ok = false;
            break;
        }
        XClassDelete(filePath);
        ++lines;
    }
    XFree_System(text);
    return ok;
}

/** @brief 回滚一个组件到上一激活版本并计数（§8.3 步骤 4 + 不变式 4）。 */
static bool xup_rollback_component(XUpgradeEngine* e, XUpgradeStateComponent* rec,
                                   const char* badVersion, const char** errText)
{
    bool ok;
    char detail[XUPGRADE_DETAIL_MAX];
    char activeVersion[XUPGRADE_VERSION_MAX];
    char id[XUPGRADE_COMPONENT_ID_MAX];
    XUpgradeStore* store;
    if (!e || !rec) return false;
    /* 记录读取/改写按结构体契约走 m_mutex（自检可能在引擎已 start 时运行，
     * 工作线程流水并发改写同一镜像）。 */
    XMutex_lock(e->m_mutex);
    xups_str_copy(activeVersion, sizeof(activeVersion), rec->m_activeVersion);
    xups_str_copy(id, sizeof(id), rec->m_id);
    store = e->m_store;
    XMutex_unlock(e->m_mutex);
    if (activeVersion[0] == '\0') return false;
    ok = XUpgradeStore_rollback_base(store, id, activeVersion, errText)
         == XUpgradeError_Ok;
    XMutex_lock(e->m_mutex);
    if (rec->m_staged && xups_str_eq(rec->m_stagedVersion, badVersion)) {
        rec->m_staged = false;
        xups_str_copy(rec->m_stagedVersion, XUPGRADE_VERSION_MAX, "");
    }
    rec->m_rollbackCount++;
    e->m_state.m_state = XUpgradeState_RolledBack;
    XMutex_unlock(e->m_mutex);
    xup_save_state(e);
    xup_fmt(detail, sizeof(detail), "自检失败回滚 %s：新版本 %s → %s%s",
            id, badVersion ? badVersion : "", activeVersion,
            ok ? "" : "（回滚写回失败）");
    /* RolledBack 属五类 UI 提示语义事件（XUpgrade.h 事件门控口径）。 */
    xup_emit_evt(e, XUpgradeEventType_RolledBack, XUpgradeState_RolledBack,
                 XUpgradePhase_Activate, -1, id, badVersion, detail, true);
    xup_log(e, detail);
    XMutex_lock(e->m_mutex);
    if (rec->m_rollbackCount > XUPGRADE_ROLLBACK_LIMIT) {
        XMutex_unlock(e->m_mutex);
        xup_fmt(detail, sizeof(detail),
                "同一版本回滚超过 %u 次，自动升级已停用，需强报故障",
                (unsigned)XUPGRADE_ROLLBACK_LIMIT);
        xup_emit_evt(e, XUpgradeEventType_RolledBack, XUpgradeState_RolledBack,
                     XUpgradePhase_Activate, -1, id, badVersion, detail, true);
        xup_log(e, detail);
        return ok;
    }
    XMutex_unlock(e->m_mutex);
    return ok;
}

/**
 * @brief 首装完整性失败收容：撤组件指针并清指向该版本的 staged 标记（§8.3）。
 * @details 适用于从未激活的组件线（fontres 等资源线首装，activeVersion 为
 *          空）：没有已激活版本可作回滚锚点（xup_rollback_component 直接
 *          失败），安全收敛面是"该组件线保持未激活"——撤 pointers/<comp>、
 *          清组件级 staged 标记，防坏包被 apply 桩每轮重装成死循环；整机
 *          allOk 由调用方按自检失败上报。
 */
static void xup_first_activation_fail(XUpgradeEngine* e, const char* componentId,
                                      const char* version)
{
    XString* root;
    XString* path;
    bool stagedCleared = false;
    if (!e || !componentId) return;
    XMutex_lock(e->m_mutex);
    {
        XUpgradeStateComponent* rec = xup_state_component(&e->m_state, componentId, false);
        if (rec && rec->m_staged && xups_str_eq(rec->m_stagedVersion, version)) {
            rec->m_staged = false;
            rec->m_stagedVersion[0] = '\0';
            stagedCleared = true;
        }
    }
    XMutex_unlock(e->m_mutex);
    root = xup_root_copy(e);
    if (root) {
        path = xup_path3(root, XUPGRADE_DIR_POINTERS, componentId);
        XClassDelete(root);
        if (path) {
            (void)xup_remove_file(path);
            XClassDelete(path);
        }
    }
    if (stagedCleared) xup_save_state(e);
    xup_logf(e, "自检：%s@%s 首装校验失败，撤指针保持未激活（无回滚锚点）",
             componentId, version ? version : "");
    xup_emit_evt(e, XUpgradeEventType_RolledBack, XUpgradeState_RolledBack,
                 XUpgradePhase_Activate, -1,
                 componentId, version ? version : "",
                 "首装校验失败：组件线保持未激活（无回滚锚点）", true);
}

bool XUpgradeEngine_selfCheck(XUpgradeEngine* e)
{
    XUpgradeStateFile* st;
    bool allOk = true;
    bool anyRollback = false;
    bool bootConfirming = false;
    bool freshDevice = false;
    uint32_t i;
    uint32_t componentCount;
    const char* errText = NULL;
    char detail[XUPGRADE_DETAIL_MAX];
    if (!e) return false;
    st = &e->m_state;
    /* 引擎未 start 时补齐安装根与后端（仅依赖根/state.json/指针文件）。
     * 字段访问按结构体契约走 m_mutex：本函数亦可在引擎已 start（工作线程
     * 运行中）调用，installRoot/store/state 镜像与流水并发改写不得裸读写。 */
    XMutex_lock(e->m_mutex);
    {
        bool missingRoot = e->m_installRoot == NULL;
        XMutex_unlock(e->m_mutex);
        if (missingRoot) {
            char root[1024];
            XString* created;
            if (!xups_derive_root(root, sizeof(root))) return false;
            created = XString_create_utf8(root);
            if (!created) return false;
            XMutex_lock(e->m_mutex);
            if (e->m_installRoot) XClassDelete((XClass*)created); /* 并发已补齐：弃新留旧 */
            else e->m_installRoot = created;
            XMutex_unlock(e->m_mutex);
        }
    }
    XMutex_lock(e->m_mutex);
    {
        bool missingStore = e->m_store == NULL;
        XString* root = e->m_installRoot ? XString_create_copy(e->m_installRoot) : NULL;
        XMutex_unlock(e->m_mutex);
        if (missingStore) {
            XUpgradeStore* created;
            if (!root) return false;
            created = XUpgradeStoreDir_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                 XString_toUtf8(root));
            XClassDelete(root);
            if (!created) return false;
            XMutex_lock(e->m_mutex);
            if (e->m_store) XClassDelete((XClass*)created); /* 并发已补齐：弃新留旧 */
            else e->m_store = created;
            XMutex_unlock(e->m_mutex);
        } else if (root) {
            XClassDelete(root);
        }
    }
    /* state.json 加载；缺失/损坏=全新设备口径（§4.2.4）。 */
    {
        XString* dir = xup_update_dir(e);
        bool loaded = false;
        if (dir) {
            XMutex_lock(e->m_mutex);
            loaded = XUpgradeStateFile_load(st, XString_toUtf8(dir));
            XMutex_unlock(e->m_mutex);
            XClassDelete(dir);
        }
        if (!loaded) {
            char cur[XUPGRADE_VERSION_MAX];
            cur[0] = '\0';
            (void)XUpgradeIdentity_version(cur, XUPGRADE_VERSION_MAX);
            XMutex_lock(e->m_mutex);
            XUpgradeStateFile_clear(st);
            if (xups_version_valid(cur)) {
                XUpgradeStateComponent* rec = xup_state_component(st, "app", true);
                if (rec) {
                    xups_str_copy(rec->m_activeVersion, XUPGRADE_VERSION_MAX, cur);
                    xups_str_copy(rec->m_maxVersion, XUPGRADE_VERSION_MAX, cur);
                }
            }
            XMutex_unlock(e->m_mutex);
            xup_save_state(e);
            freshDevice = true;
        }
    }
    if (freshDevice) xup_log(e, "自检：state.json 缺失/损坏，按全新设备重建基线");
    XMutex_lock(e->m_mutex);
    bootConfirming = st->m_state == XUpgradeState_BootConfirming ||
                     st->m_state == XUpgradeState_Applying;
    componentCount = st->m_componentCount;
    XMutex_unlock(e->m_mutex);
    /* 组件记录：判定用快照、落盘前改写集中加锁（工作线程并发改写同一镜像时
     * 不撕裂；快照点之间记录只被本函数改写，语义与逐字段访问一致）。 */
    /* root 锁外预取：循环内 version_dir_complete 消费（其内部不再取锁，
     * 防持 m_mutex 路径同线程重锁自死锁，2026-10-11）。 */
    XString* selfCheckRoot = xup_root_copy(e);
    for (i = 0; i < componentCount && i < XUPGRADE_STATE_COMPONENT_MAX; ++i) {
        XUpgradeStateComponent* rec = &st->m_components[i];
        char ptr[XUPGRADE_VERSION_MAX];
        char id[XUPGRADE_COMPONENT_ID_MAX];
        char activeVersion[XUPGRADE_VERSION_MAX];
        bool staged;
        XMutex_lock(e->m_mutex);
        xups_str_copy(id, sizeof(id), rec->m_id);
        xups_str_copy(activeVersion, sizeof(activeVersion), rec->m_activeVersion);
        staged = rec->m_staged;
        XMutex_unlock(e->m_mutex);
        if (activeVersion[0] == '\0' && !staged) {
            /* 组件线首装（§3 多组件：fontres 等从未激活线，M1 仅 app 线未覆盖
             * 的断电语义）：apply 桩已切指针并清 staged 后，记录 activeVersion
             * 仍为空——指针为权威，落入下方完整性校验与确认路径完成首装收编；
             * 无指针=staged 待装（activeVersion 空但 staged 时走下一分支）或
             * 无事发生。 */
            if (!xup_read_pointer(e, id, ptr, sizeof(ptr))) continue;
        } else if (!xup_read_pointer(e, id, ptr, sizeof(ptr))) {
            if (activeVersion[0] == '\0') continue; /* 从未激活（staged 待装） */
            /* 指针读失败=未激活口径（§2）→ 写回上一激活版本。但版本目录
             * 不存在（全新设备：重建基线写入出厂版本、从无指针与版本目录）
             * 时无物可回滚——按"无事发生"收编；否则必败回滚逐次烧毁回滚
             * 限额（rollbackLimit=2），3 次启动后自动升级被永久停用
             * （2026-10-08 评审 P1：e2e 夹具全预置已装目录故漏测）。 */
            {
                XString* rootDir = xup_root_copy(e);
                XString* verDir = NULL;
                bool haveDir = false;
                if (rootDir) {
                    verDir = xup_path3(rootDir, XUPGRADE_DIR_VERSIONS, id);
                    if (verDir && XString_append_utf8(verDir, "/"))
                        XString_append_utf8(verDir, activeVersion);
                    haveDir = verDir && XFileInfo_exists_static(verDir);
                    if (verDir) XClassDelete(verDir);
                    XClassDelete(rootDir);
                }
                if (!haveDir) continue;
            }
            xup_fmt(detail, sizeof(detail), "自检：pointers/%s 不可读，写回 %s",
                    id, activeVersion);
            xup_log(e, detail);
            if (!xup_rollback_component(e, rec, ptr, &errText)) allOk = false;
            else anyRollback = true;
            continue;
        }
        if (!xups_str_eq(ptr, activeVersion) && activeVersion[0] != '\0') {
            /* 指针指向非记录版本：BootConfirming 窗口=stub 已激活 staged，指针
             * 为权威跟走校验；非确认窗口=外部改动，回写记录版本。 */
            if (!bootConfirming) {
                xup_fmt(detail, sizeof(detail),
                        "自检：%s 指针 %s 与记录 %s 不符且非确认窗口",
                        id, ptr, activeVersion);
                xup_log(e, detail);
                if (!xup_rollback_component(e, rec, ptr, &errText)) allOk = false;
                else anyRollback = true;
                continue;
            }
            /* 确认窗口：不得在此把 ptr 收编进 m_activeVersion——下方完整性
             * 校验失败时 xup_rollback_component 按 m_activeVersion 写回指针，
             * 提前覆盖会把回滚目标冲成坏版本自己（指针原样不动，§8.3 失效）。
             * 校验通过后的收口处（Active 分支）再行赋值。 */
        }
        /* §8.3 步骤 1：版本目录存在且 manifest.version 吻合。 */
        if (!xup_version_dir_complete(e, selfCheckRoot, id, ptr)) {
            xup_fmt(detail, sizeof(detail), "自检：%s@%s 版本目录缺失或标记不符",
                    id, ptr);
            xup_log(e, detail);
            if (activeVersion[0] == '\0') {
                /* 首装失败：无已激活版本可作回滚锚点，收容为该组件线保持
                 * 未激活（撤指针+清 staged 标记），防坏包每轮重装。 */
                xup_first_activation_fail(e, id, ptr);
                allOk = false;
                continue;
            }
            if (!xup_rollback_component(e, rec, ptr, &errText)) allOk = false;
            else anyRollback = true;
            continue;
        }
        /* §8.3 步骤 2：关键文件哈希抽查（res 线首装同样覆盖）。 */
        if (!xup_verify_spot_check(e, id, ptr)) {
            xup_fmt(detail, sizeof(detail), "自检：%s@%s 关键文件哈希抽查失败",
                    id, ptr);
            xup_log(e, detail);
            if (activeVersion[0] == '\0') {
                xup_first_activation_fail(e, id, ptr);
                allOk = false;
                continue;
            }
            if (!xup_rollback_component(e, rec, ptr, &errText)) allOk = false;
            else anyRollback = true;
            continue;
        }
        if (!bootConfirming && activeVersion[0] == '\0') {
            /* 首装收编（错过确认窗口：桩写指针后 BootConfirming 落盘前断电
             * 等，多组件线断电语义的收口面）：完整性已过（版本标记+抽查），
             * 按 §8.3 指针权威收编为激活版本，同步建立防降级基线（§4.2）、
             * 清指向它的 staged 标记并按本组件线 GC。 */
            XMutex_lock(e->m_mutex);
            if (XUpgradeManifest_compareVersions(ptr, rec->m_maxVersion) > 0)
                xups_str_copy(rec->m_maxVersion, XUPGRADE_VERSION_MAX, ptr);
            if (rec->m_staged && xups_str_eq(rec->m_stagedVersion, ptr)) {
                rec->m_staged = false;
                xups_str_copy(rec->m_stagedVersion, XUPGRADE_VERSION_MAX, "");
            }
            xups_str_copy(rec->m_activeVersion, XUPGRADE_VERSION_MAX, ptr);
            XMutex_unlock(e->m_mutex);
            xup_save_state(e);
            xup_logf(e, "自检收编：%s@%s 首装激活（非确认窗口）", id, ptr);
            xup_gc_component(e, id, ptr);
            continue;
        }
        if (bootConfirming) {
            /* §8.3 步骤 3：通过 → Active；更新防降级基线、清计数（M2 字段 M1 持久化）。 */
            char maxVersion[XUPGRADE_VERSION_MAX];
            char stagedVersion[XUPGRADE_VERSION_MAX];
            bool maxBumped;
            XMutex_lock(e->m_mutex);
            xups_str_copy(maxVersion, sizeof(maxVersion), rec->m_maxVersion);
            xups_str_copy(stagedVersion, sizeof(stagedVersion), rec->m_stagedVersion);
            staged = rec->m_staged;
            XMutex_unlock(e->m_mutex);
            maxBumped = XUpgradeManifest_compareVersions(ptr, maxVersion) > 0;
            XMutex_lock(e->m_mutex);
            if (maxBumped)
                xups_str_copy(rec->m_maxVersion, XUPGRADE_VERSION_MAX, ptr);
            if (staged && xups_str_eq(stagedVersion, ptr)) {
                rec->m_staged = false;
                xups_str_copy(rec->m_stagedVersion, XUPGRADE_VERSION_MAX, "");
            }
            rec->m_rollbackCount = 0;
            xups_str_copy(rec->m_activeVersion, XUPGRADE_VERSION_MAX, ptr);
            e->m_state.m_state = XUpgradeState_Active;
            XMutex_unlock(e->m_mutex);
            xup_save_state(e);
            xup_logf(e, "自检通过：%s@%s → Active", id, ptr);
            xup_gc_component(e, id, ptr);
        }
    }
    if (allOk && !bootConfirming) {
        XMutex_lock(e->m_mutex);
        if (e->m_state.m_state == XUpgradeState_BootConfirming)
            e->m_state.m_state = XUpgradeState_Active;
        XMutex_unlock(e->m_mutex);
    }
    if (!allOk) {
        xup_fmt(detail, sizeof(detail), "自检失败且回滚失败：%s",
                errText ? errText : "存储后端错误");
        xup_log(e, detail);
        /* RolledBack 属五类 UI 提示语义事件（XUpgrade.h 事件门控口径）。 */
        xup_emit_evt(e, XUpgradeEventType_RolledBack, XUpgradeState_RolledBack,
                     XUpgradePhase_Activate, -1, "", "", detail, true);
    }
    if (selfCheckRoot) XClassDelete(selfCheckRoot);
    return allOk;
}

/* ========================================================================== */
/*                          工作线程与流水调度                                  */
/* ========================================================================== */

/** @brief 停机协作检查（流水内长循环调用）。 */
static bool xup_stop_requested(XUpgradeEngine* e)
{
    bool stop;
    if (!e) return true;
    XMutex_lock(e->m_mutex);
    stop = e->m_stop;
    XMutex_unlock(e->m_mutex);
    return stop;
}

/**
 * @brief 产品版本基线是否为"版本未知"哨兵（XUpgradeIdentity.h 契约口径）。
 * @details 未注入真实版本（CMake -DXUPGRADE_PRODUCT_VERSION 或运行期提供者）
 *          时版本回退缺省 "0.0.0"，契约语义为"版本未知，引擎只允许手动检查"；
 *          有效版本恰为 "0.0.0" 的产品与此不可区分，按未知处理（防降级基线
 *          亦无从谈起）。该门仅拦自动周期检查，手动 checkNow 不受限。
 */
static bool xup_product_version_unknown(void)
{
    char cur[XUPGRADE_VERSION_MAX];
    cur[0] = '\0';
    (void)XUpgradeIdentity_version(cur, XUPGRADE_VERSION_MAX);
    return xups_str_eq(cur, "0.0.0");
}

/** @brief 工作线程主循环（先例 XGuiServer.c xgs_workerMain）。 */
static void xup_worker_main(XThread* thread, XVarList* arguments)
{
    XUpgradeEngine* e = NULL;
    uint32_t heartbeatClock = 0;
    (void)thread;
    if (!arguments) return;
    {
        XVarList_args_1(arguments, void*, enginePtr);
        e = (XUpgradeEngine*)enginePtr;
    }
    if (!e) return;
    for (;;) {
        int request = XUpgradeRequest_None;
        bool autoDue = false;
        bool run = false;
        XMutex_lock(e->m_mutex);
        if (e->m_stop) { XMutex_unlock(e->m_mutex); break; }
        request = e->m_request;
        e->m_request = XUpgradeRequest_None;
        /* 版本未知（未注入产品版本基线）时只允许手动检查：自动周期检查被门禁
         * 跳过（契约口径 XUpgradeIdentity.h:29-35），任意 manifest 版本都会被
         * 判为可升级，自动升级决策不可信。 */
        if (request == XUpgradeRequest_None && e->m_autoCheck &&
            e->m_checkIntervalHours > 0 && !xup_product_version_unknown()) {
            int64_t due = e->m_lastCheckMs +
                          (int64_t)e->m_checkIntervalHours * 3600000ll;
            if (XDateTime_currentMSecsSinceEpoch() >= due) autoDue = true;
        }
        if (request != XUpgradeRequest_None || autoDue) {
            if (e->m_busy) {
                request = XUpgradeRequest_None;
                autoDue = false;
            } else {
                e->m_busy = true;
                run = true;
            }
        }
        XMutex_unlock(e->m_mutex);
        if (run && !xup_stop_requested(e)) {
            if (request == XUpgradeRequest_Check) xup_run_check(e, true);
            else if (request == XUpgradeRequest_Upgrade) xup_run_upgrade(e);
            else if (autoDue) xup_run_check(e, false);
        }
        XMutex_lock(e->m_mutex);
        e->m_busy = false;
        XMutex_unlock(e->m_mutex);
        xup_policy_tick(e); /* §10 策略时机：forced 立即 / silent 维护窗口（墙钟，错过顺延） */
        heartbeatClock += XUPGRADE_TICK_MS;
        if (heartbeatClock >= XUPGRADE_LOCK_HEARTBEAT_WRITE_MS) {
            heartbeatClock = 0;
            (void)xup_lock_write(e);
        }
        XThread_msleep(XUPGRADE_TICK_MS);
    }
    (void)xup_lock_write(e); /* 末次心跳；锁文件随后由 stop 移除 */
}

/* ========================================================================== */
/*              类实现（XObject 派生具体类，无新增虚槽位）                       */
/* ========================================================================== */

XCLASS_DEFINE_BEGING(XUpgradeEngine)
XCLASS_DEFINE_EXTEND_END(XUpgradeEngine, XObject)

static XVtable* XUpgradeEngine_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XUpgradeEngine)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXUpgradeEngine_deinit);
    return XVTABLE_DEFAULT;
}

XUpgradeEngine* XUpgradeEngine_create_ex(XMemoryType memory)
{
    XUpgradeEngine* e = (XUpgradeEngine*)XMemory_calloc(1, sizeof(XUpgradeEngine), memory);
    if (!e) return NULL;
    XObject_init((XObject*)e);
    XClassSetVtable(e, XUpgradeEngine);
    e->m_mutex = XMutex_create(XLock_NonRecursive);
    if (!e->m_mutex) {
        XClass_Deinit_Parent(XObject, e);
        XMemory_method(memory)->free(e);
        return NULL;
    }
    e->m_saveMutex = XMutex_create(XLock_NonRecursive);
    if (!e->m_saveMutex) {
        XMutex_delete(e->m_mutex);
        XClass_Deinit_Parent(XObject, e);
        XMemory_method(memory)->free(e);
        return NULL;
    }
    XUpgradeStateFile_clear(&e->m_state);
    Set_Class_Memory(e, memory);
    Set_Class_IsHeap(e, true);
    return e;
}

static void VXUpgradeEngine_deinit(XUpgradeEngine* e)
{
    XMutex* mutex;
    XMutex* saveMutex;
    if (!e) return;
    XUpgradeEngine_stop(e); /* 停工作线程（幂等） */
    mutex = e->m_mutex;
    saveMutex = e->m_saveMutex;
    if (mutex) XMutex_lock(mutex);
    if (e->m_manifestUrl) { XClassDelete(e->m_manifestUrl); e->m_manifestUrl = NULL; }
    if (e->m_baseUrl) { XClassDelete(e->m_baseUrl); e->m_baseUrl = NULL; }
    if (e->m_channel) { XClassDelete(e->m_channel); e->m_channel = NULL; }
    if (e->m_targetOs) { XClassDelete(e->m_targetOs); e->m_targetOs = NULL; }
    if (e->m_installRoot) { XClassDelete(e->m_installRoot); e->m_installRoot = NULL; }
    if (e->m_store) { XClassDelete((XClass*)e->m_store); e->m_store = NULL; }
    e->m_mutex = NULL;
    e->m_saveMutex = NULL;
    if (mutex) {
        XMutex_unlock(mutex);
        /* XMutex 非 XClass 派生（首成员是 type，XMutex_create 另按含平台私有块
         * 的扩展尺寸分配，XMutex.c:109-116），XClassDelete 会把 type 位域读成
         * 野 vtable 并按错误尺寸释放——任何引擎析构即崩溃；堆锁的正确释放入口
         * 是 XMutex_delete（XMutex.h:45）。 */
        XMutex_delete(mutex);
    }
    if (saveMutex) XMutex_delete(saveMutex); /* 同 XMutex 释放口径（XMutex.h:45） */
    XClass_Deinit_Parent(XObject, e);
}

/* ========================================================================== */
/*                          引擎生命周期与公共操作                              */
/* ========================================================================== */

/* derive_root 已收敛至 xups_derive_root（原注释宣称"上级目录"与实现不符、
 * prevSlash 分支不可达——随收敛一并消除，见 xupgrade_shared.h）。 */

bool XUpgradeEngine_start(XUpgradeEngine* e, const XUpgradeConfig* config)
{
    XString* dir;
    XString* root = NULL;
    bool loaded;
    if (!e || !config || !config->m_updateUrl || !config->m_targetOs) return false;
    XMutex_lock(e->m_mutex);
    if (e->m_started) { XMutex_unlock(e->m_mutex); return true; }
    /* 配置拷贝（借用语义：init 返回后调用方可释放 config）。字段段全程持
     * 锁：与 selfCheck 的"并发已补齐：弃新留旧"持锁防御同一纪律（结构体
     * 契约 m_mutex 保护下列全部字段），消除 start 裸写 vs 持锁写竞态。 */
    if (e->m_manifestUrl) { XClassDelete(e->m_manifestUrl); e->m_manifestUrl = NULL; }
    if (e->m_baseUrl) { XClassDelete(e->m_baseUrl); e->m_baseUrl = NULL; }
    if (e->m_channel) { XClassDelete(e->m_channel); e->m_channel = NULL; }
    if (e->m_targetOs) { XClassDelete(e->m_targetOs); e->m_targetOs = NULL; }
    if (e->m_installRoot) { XClassDelete(e->m_installRoot); e->m_installRoot = NULL; }
    if (config->m_channel && config->m_channel[0] != '\0') {
        e->m_channel = XString_create_utf8(config->m_channel);
        if (!e->m_channel) { XMutex_unlock(e->m_mutex); return false; }
    }
    e->m_targetOs = XString_create_utf8(config->m_targetOs);
    if (!e->m_targetOs) { XMutex_unlock(e->m_mutex); return false; }
    if (config->m_installRoot && config->m_installRoot[0] != '\0') {
        e->m_installRoot = XString_create_utf8(config->m_installRoot);
    } else {
        char derived[1024];
        if (xups_derive_root(derived, sizeof(derived)))
            e->m_installRoot = XString_create_utf8(derived);
    }
    if (!e->m_installRoot) { XMutex_unlock(e->m_mutex); return false; }
    e->m_manifestUrl = XString_create_utf8(config->m_updateUrl);
    if (!e->m_manifestUrl) { XMutex_unlock(e->m_mutex); return false; }
    e->m_checkIntervalHours = config->m_checkIntervalHours;
    e->m_autoCheck = config->m_autoCheck && config->m_checkIntervalHours > 0;
    /* 策略配置副本（§10）：silent 维护窗口初值取 config（运行期可经
     * XUpgrade_setApplyWindow 修改）；策略/灰度判定与自动 apply 标志按
     * "本次启动尚未检查"复位（XUpgrade_grayDecision 契约：从未检查返回
     * NotEvaluated）。注入类配置（信任公钥/宽限期覆盖）不在 start 重置，
     * 保留对象上的既有值（建议于 init 前经模块入口设置）。 */
    e->m_applyWindow = config->m_applyWindow;
    e->m_policy = XUpgradePolicy_Optional;
    e->m_grayDecision = XUpgradeGrayDecision_NotEvaluated;
    e->m_readyAutoApplyDone = false;
    XMutex_unlock(e->m_mutex);
    /* state.json 加载（失败=全新设备口径，§4.2.4）。 */
    dir = xup_update_dir(e);
    if (!dir) return false;
    XMutex_lock(e->m_mutex);
    loaded = XUpgradeStateFile_load(&e->m_state, XString_toUtf8(dir));
    XMutex_unlock(e->m_mutex);
    XClassDelete(dir);
    if (!loaded) {
        char cur[XUPGRADE_VERSION_MAX];
        XUpgradeStateComponent* rec;
        XUpgradeStateFile_clear(&e->m_state);
        cur[0] = '\0';
        (void)XUpgradeIdentity_version(cur, XUPGRADE_VERSION_MAX);
        if (xups_version_valid(cur)) {
            rec = xup_state_component(&e->m_state, "app", true);
            if (rec) {
                xups_str_copy(rec->m_activeVersion, XUPGRADE_VERSION_MAX, cur);
                xups_str_copy(rec->m_maxVersion, XUPGRADE_VERSION_MAX, cur);
            }
        }
    }
    /* 存储后端（M1 固定目录后端）。 */
    if (!e->m_store) {
        root = xup_root_copy(e);
        if (!root) return false;
        e->m_store = XUpgradeStoreDir_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                XString_toUtf8(root));
        XClassDelete(root);
        if (!e->m_store) return false;
    }
    /* 引擎锁：防双实例 + 陈旧接管（不变式 6）。 */
    if (!xup_lock_acquire(e)) {
        xup_log(e, "启动失败：引擎被其他实例占用（update/lock 心跳新鲜）");
        return false;
    }
    /* 断电恢复收敛（§16 矩阵 1-7）。 */
    xup_recover(e);
    /* 启动工作线程（样板同 XGuiServer.c xgs_sessionStartWorker）。 */
    {
        XUpgradeEngine* ep = e;
        e->m_threadArgs = XVarList_Create(XVar(void*, ep));
        if (!e->m_threadArgs) {
            xup_lock_remove(e);
            return false;
        }
        /* 契约（XUpgradeEngine.h）：stop 后允许重新 start——先清停机标志
         * 再起线程，否则新工作线程首轮即退出（2026-10-08 评审发现）。 */
        XMutex_lock(e->m_mutex);
        e->m_stop = false;
        XMutex_unlock(e->m_mutex);
        e->m_thread = XThread_create_func(xup_worker_main, e->m_threadArgs);
        if (!e->m_thread) {
            XVarList_delete(e->m_threadArgs);
            e->m_threadArgs = NULL;
            xup_lock_remove(e);
            return false;
        }
        if (!XThread_start(e->m_thread)) {
            XClassDelete((XClass*)e->m_thread);
            e->m_thread = NULL;
            XVarList_delete(e->m_threadArgs);
            e->m_threadArgs = NULL;
            xup_lock_remove(e);
            return false;
        }
    }
    e->m_started = true;
    xup_log(e, "引擎已启动");
    /* 重启恢复：上次会话停在 Ready（已 staged 待应用）时重发 ReadyNeedRestart
     * ——刚启动才接入的观察者（UI）需要知道待应用升级仍在，否则「立即应用」
     * 永远停灰、用户重开后无法完成升级（2026-10-07 实弹缺口）。 */
    {
        bool pendingReady = false;
        uint32_t k;
        XMutex_lock(e->m_mutex);
        for (k = 0; k < e->m_state.m_componentCount && k < XUPGRADE_STATE_COMPONENT_MAX; ++k) {
            if (e->m_state.m_components[k].m_staged &&
                e->m_state.m_components[k].m_stagedVersion[0] != '\0') {
                pendingReady = true;
                break;
            }
        }
        XMutex_unlock(e->m_mutex);
        if (pendingReady)
            xup_emit_evt(e, XUpgradeEventType_ReadyNeedRestart,
                         XUpgradeState_Ready, XUpgradePhase_Activate, -1, "", "",
                         "检测到待应用的升级（上次会话遗留）",
                         true);
    }
    return true;
}

void XUpgradeEngine_stop(XUpgradeEngine* e)
{
    XThread* thread;
    XVarList* args;
    if (!e) return;
    XMutex_lock(e->m_mutex);
    if (!e->m_thread) { XMutex_unlock(e->m_mutex); return; }
    e->m_stop = true;
    thread = e->m_thread;
    args = e->m_threadArgs;
    e->m_thread = NULL;
    e->m_threadArgs = NULL;
    e->m_started = false;
    e->m_busy = false;
    e->m_request = XUpgradeRequest_None;
    XMutex_unlock(e->m_mutex);
    if (thread) {
        XThread_requestInterruption(thread);
        (void)XThread_wait(thread, XUPGRADE_JOIN_TIMEOUT_MS);
        XClassDelete((XClass*)thread);
    }
    if (args) XVarList_delete(args);
    xup_lock_remove(e);
    xup_log(e, "引擎已停止");
}

bool XUpgradeEngine_checkNow(XUpgradeEngine* e)
{
    bool accepted = false;
    bool limited = false;
    uint32_t i;
    if (!e) return false;
    XMutex_lock(e->m_mutex);
    if (e->m_started && !e->m_busy && e->m_request == XUpgradeRequest_None &&
        e->m_state.m_state != XUpgradeState_Downloading &&
        e->m_state.m_state != XUpgradeState_Verifying &&
        e->m_state.m_state != XUpgradeState_Staging) {
        /* 回滚计数超限 → 自动升级停用（§8.3）。 */
        for (i = 0; i < e->m_state.m_componentCount && i < XUPGRADE_STATE_COMPONENT_MAX; ++i)
            if (e->m_state.m_components[i].m_rollbackCount > XUPGRADE_ROLLBACK_LIMIT)
                limited = true;
        if (!limited) {
            e->m_request = XUpgradeRequest_Check;
            accepted = true;
        }
    }
    XMutex_unlock(e->m_mutex);
    return accepted;
}

bool XUpgradeEngine_startUpgrade(XUpgradeEngine* e)
{
    bool accepted = false;
    if (!e) return false;
    XMutex_lock(e->m_mutex);
    if (e->m_started && !e->m_busy && e->m_request == XUpgradeRequest_None &&
        e->m_hasManifest && e->m_targetCount > 0 && e->m_targetCount <= XUPGRADE_COMPONENT_MAX &&
        e->m_state.m_state != XUpgradeState_Ready) {
        uint32_t i;
        uint32_t w = 0;
        bool limited = false;
        for (i = 0; i < e->m_state.m_componentCount && i < XUPGRADE_STATE_COMPONENT_MAX; ++i)
            if (e->m_state.m_components[i].m_rollbackCount > XUPGRADE_ROLLBACK_LIMIT)
                limited = true;
        if (!limited) {
            /* 逐目标复核跳过表（用户可能在确认前跳过）。 */
            for (i = 0; i < e->m_targetCount; ++i) {
                if (xup_skip_hit(&e->m_state, e->m_targets[i].m_componentId,
                                 e->m_targets[i].m_version))
                    continue;
                if (w != i) e->m_targets[w] = e->m_targets[i];
                ++w;
            }
            e->m_targetCount = w;
            if (w > 0) {
                e->m_request = XUpgradeRequest_Upgrade;
                accepted = true;
            }
        }
    }
    XMutex_unlock(e->m_mutex);
    return accepted;
}

uint32_t XUpgradeEngine_remoteVersionCount(XUpgradeEngine* e)
{
    uint32_t count = 0;
    if (!e) return 0;
    XMutex_lock(e->m_mutex);
    if (e->m_hasCatalog) count = e->m_catalog.m_releaseCount;
    XMutex_unlock(e->m_mutex);
    return count;
}

bool XUpgradeEngine_remoteVersionAt(XUpgradeEngine* e, uint32_t index,
                                    char* versionOut, size_t cap)
{
    bool ok = false;
    if (!e || !versionOut || cap == 0) return false;
    versionOut[0] = '\0';
    XMutex_lock(e->m_mutex);
    if (e->m_hasCatalog && index < e->m_catalog.m_releaseCount &&
        index < XUPGRADE_CATALOG_MAX)
    {
        xups_str_copy(versionOut, cap, e->m_catalog.m_releases[index].m_version);
        ok = true;
    }
    XMutex_unlock(e->m_mutex);
    return ok;
}

bool XUpgradeEngine_installVersion(XUpgradeEngine* e, const char* version)
{
    bool accepted = false;
    if (!e || !version || version[0] == '\0' || !xups_version_valid(version)) return false;
    XMutex_lock(e->m_mutex);
    if (e->m_started && !e->m_busy && e->m_request == XUpgradeRequest_None &&
        e->m_hasCatalog && e->m_catalog.m_releaseCount > 0) {
        uint32_t k;
        for (k = 0; k < e->m_catalog.m_releaseCount; ++k) {
            if (xups_str_eq(e->m_catalog.m_releases[k].m_version, version)) {
                /* pinned 由检查流水消费：目录分支按 pinned 条目二次拉取其
                 * manifest，目标构建绕过"仅更新"门槛（显式降级/重装），完成后
                 * 自动续跑下载→staging（§4 扩展：多版本选择安装）。 */
                xups_str_copy(e->m_pinnedVersion, sizeof(e->m_pinnedVersion), version);
                e->m_request = XUpgradeRequest_Check;
                accepted = true;
                break;
            }
        }
    }
    XMutex_unlock(e->m_mutex);
    return accepted;
}

void XUpgradeEngine_skipVersion(XUpgradeEngine* e, const char* componentId, const char* version)
{
    bool changed = false;
    XString* dir;
    if (!e || !componentId || componentId[0] == '\0' || !version || version[0] == '\0') return;
    if (!xups_version_valid(version)) return;
    XMutex_lock(e->m_mutex);
    changed = xup_skip_add(&e->m_state, componentId, version);
    XMutex_unlock(e->m_mutex);
    if (!changed) return;
    dir = xup_update_dir(e);
    if (dir) {
        if (!xup_save_state_to(e, dir)) xup_log(e, "state.json 保存失败");
        XClassDelete(dir);
    }
    xup_logf(e, "跳过版本 %s@%s", componentId, version);
}

bool XUpgradeEngine_applyNow(XUpgradeEngine* e)
{
    bool accepted = false;
    bool done = true;
    if (!e) return false;
    XMutex_lock(e->m_mutex);
    if (e->m_started && e->m_state.m_state == XUpgradeState_Ready) {
        accepted = true;
        e->m_readyAutoApplyDone = true; /* 用户已请求应用：策略时机调度不再重发 ReadyAutoApply */
        /* 受理即置 Applying 并落盘（"切换待续做"语义，与 apply 桩同一状态）：
         * 此后进程即死（崩溃/被关）也不回 Ready——重启经 recover 续做切换或
         * 桩幂等续做，升级不再"永远差最后一步"。 */
        e->m_state.m_state = XUpgradeState_Applying;
    }
    XMutex_unlock(e->m_mutex);
    if (accepted) {
        xup_save_state(e); /* Applying 先行落盘（矩阵 6 修正同款顺序，staged 标记保留） */
        xup_log(e, "applyNow 受理：Applying 已落盘，引擎内切换 staged 指针");
        /* 激活段进度（§12 第四段）：0=受理，100=切换完成。 */
        xup_emit_progress(e, XUpgradePhase_Activate, 0, "", "");
#if XUPGRADE_AB_ON
        /* A/B 构建（既有行为不变）：受理即引擎侧职责完成，调用方以退出码 74
         * 退出，由 --xupgrade-apply 完成槽切换（bootloader 重启语义，§9.4）。 */
        xup_emit_progress(e, XUpgradePhase_Activate, 100, "", "");
        xup_emit(e, XUpgradeState_Ready, XUpgradePhase_Activate, -1, "", "",
                 "等待进程退出以应用升级", true);
#else
        /* 桌面目录后端：引擎内直接完成切换（Applying→BootConfirming 落盘；
         * 任一 activate 失败在助手内回退 Ready 落盘并返回 false）。 */
        done = xup_apply_activate_staged(e, "applyNow");
        if (done) {
            xup_emit_progress(e, XUpgradePhase_Activate, 100, "", "");
            xup_emit(e, XUpgradeState_BootConfirming, XUpgradePhase_Activate, -1,
                     "", "", "指针切换完成，重启后自检确认", true);
        }
#endif
    }
    return accepted && done;
}

bool XUpgradeEngine_confirmBoot(XUpgradeEngine* e)
{
    if (!e) return false;
#if XUPGRADE_AB_ON
    /* A/B 构建：确认动作透传存储后端（契约 XUpgradeStore.h:331 "引擎的
     * XUpgrade_confirmBoot 透传本入口"）——对每条已激活组件记录按其
     * id/版本调 confirmBoot_base；目录后端重载为幂等 Ok（当前 AB 构建引擎
     * 仍按构建选型用 Dir 后端，切 AbStore 属 M3，届时本透传即达 §9.4 双写；
     * 后端不管理的组件返回 NotSupported，跳过不视为失败）。后端未就绪
     * （start 前）无确认可做。 */
    if (!e->m_store) return false;
    {
        const char* errText = NULL;
        XUpgradeError result = XUpgradeError_Ok;
        char ids[XUPGRADE_STATE_COMPONENT_MAX][XUPGRADE_COMPONENT_ID_MAX];
        char vers[XUPGRADE_STATE_COMPONENT_MAX][XUPGRADE_VERSION_MAX];
        uint32_t count = 0;
        uint32_t i;
        XMutex_lock(e->m_mutex);
        for (i = 0; i < e->m_state.m_componentCount && i < XUPGRADE_STATE_COMPONENT_MAX; ++i) {
            if (e->m_state.m_components[i].m_activeVersion[0] == '\0') continue;
            xups_str_copy(ids[count], XUPGRADE_COMPONENT_ID_MAX,
                         e->m_state.m_components[i].m_id);
            xups_str_copy(vers[count], XUPGRADE_VERSION_MAX,
                         e->m_state.m_components[i].m_activeVersion);
            ++count;
        }
        XMutex_unlock(e->m_mutex);
        if (count == 0) return true; /* 无已激活组件：无确认可做，幂等成功 */
        for (i = 0; i < count; ++i) {
            XUpgradeError r = XUpgradeStore_confirmBoot_base(e->m_store, ids[i],
                                                             vers[i], &errText);
            if (r == XUpgradeError_Ok || r == XUpgradeError_NotSupported) continue;
            if (errText) xup_logf(e, "confirmBoot 失败：%s（%s@%s）", errText,
                                  ids[i], vers[i]);
            result = r;
            break;
        }
        return result == XUpgradeError_Ok;
    }
#else
    (void)e;
    return true; /* 目录后端无 boot 标记可回写，幂等成功（契约口径）。 */
#endif
}

void XUpgradeEngine_setListener(XUpgradeEngine* e, XUpgradeListener listener, void* userData)
{
    if (!e) return;
    XMutex_lock(e->m_mutex);
    e->m_listener = listener;
    e->m_listenerData = userData;
    XMutex_unlock(e->m_mutex);
}

XUpgradeState XUpgradeEngine_state(const XUpgradeEngine* engine)
{
    XUpgradeState st;
    XUpgradeEngine* e = (XUpgradeEngine*)engine; /* 锁操作需要非 const 句柄 */
    if (!e) return XUpgradeState_Idle;
    XMutex_lock(e->m_mutex);
    st = e->m_state.m_state;
    XMutex_unlock(e->m_mutex);
    return st;
}

/* ========================================================================== */
/*                     策略配置接口（XUpgrade_* 模块入口透传位）                */
/* ========================================================================== */
/* 模块入口（XUpgrade.c）经 XUpgradeEngine.h 声明透传以下六入口（契约扩展
 * §10/§11/§4.2）；XUpgradeEngine.h 归入口接线方维护，本文件实现签名与其
 * 契约一一对应。 */

void XUpgradeEngine_setApplyWindow(XUpgradeEngine* e, const XUpgradeApplyWindow* window)
{
    if (!e) return;
    XMutex_lock(e->m_mutex);
    if (window) {
        e->m_applyWindow = *window; /* 值拷贝：调用方借用语义，返回后即可释放 */
    } else {
        XMemset(&e->m_applyWindow, 0, sizeof(e->m_applyWindow)); /* NULL=关闭窗口 */
    }
    XMutex_unlock(e->m_mutex);
}

XUpgradeApplyWindow XUpgradeEngine_applyWindow(const XUpgradeEngine* engine)
{
    XUpgradeApplyWindow w;
    XUpgradeEngine* e = (XUpgradeEngine*)engine; /* 锁操作需要非 const 句柄 */
    if (!e) {
        XMemset(&w, 0, sizeof(w));
        return w;
    }
    XMutex_lock(e->m_mutex);
    w = e->m_applyWindow;
    XMutex_unlock(e->m_mutex);
    return w;
}

void XUpgradeEngine_setForcedGraceHours(XUpgradeEngine* e, uint32_t hours)
{
    if (!e) return;
    XMutex_lock(e->m_mutex);
    if (hours == XUPGRADE_FORCED_GRACE_FROM_MANIFEST) {
        e->m_hasForcedGraceOverride = false; /* 哨兵值=清除覆盖，跟随 manifest */
    } else {
        e->m_hasForcedGraceOverride = true;
        e->m_forcedGraceHoursOverride = hours;
    }
    XMutex_unlock(e->m_mutex);
}

uint32_t XUpgradeEngine_forcedGraceHours(const XUpgradeEngine* engine)
{
    uint32_t hours;
    bool hasOverride;
    uint32_t overrideHours;
    bool hasManifest;
    XUpgradeManifest* manifest;
    XUpgradeEngine* e = (XUpgradeEngine*)engine; /* 锁操作需要非 const 句柄 */
    if (!e) return 0;
    XMutex_lock(e->m_mutex);
    hasOverride = e->m_hasForcedGraceOverride;
    overrideHours = e->m_forcedGraceHoursOverride;
    hasManifest = e->m_hasManifest;
    manifest = e->m_hasManifest ? &e->m_manifest : NULL;
    hours = XUpgradeGray_resolveGraceHours(hasOverride, overrideHours,
                                           hasManifest, manifest);
    XMutex_unlock(e->m_mutex);
    return hours;
}

bool XUpgradeEngine_setTrustKey(XUpgradeEngine* e, const uint8_t* publicKey, uint32_t len)
{
    /* 信任锚为验签模块级单一状态（XUpgradeVerify.c 的
     * XUpgradeVerify_setTrustKey 即 XUpgrade_setTrustKey 实现位，注入/复位
     * 语义与校验规则在该处收口）：引擎入口只做转发，不持第二份钥副本，
     * 防止引擎/验签两处状态漂移。验签 TU 无独立头，按模块先例本地 extern
     * 声明连接（同 XUpgradePackage.c 对 XUpgradeVerify_sha256Digest 的方式），
     * 两侧签名必须保持一致。 */
    extern bool XUpgradeVerify_setTrustKey(const uint8_t* publicKey, uint32_t len);
    (void)e;
    return XUpgradeVerify_setTrustKey(publicKey, len);
}

XUpgradeGrayDecision XUpgradeEngine_grayDecision(const XUpgradeEngine* engine)
{
    XUpgradeGrayDecision decision;
    XUpgradeEngine* e = (XUpgradeEngine*)engine; /* 锁操作需要非 const 句柄 */
    if (!e) return XUpgradeGrayDecision_NotEvaluated;
    XMutex_lock(e->m_mutex);
    decision = e->m_grayDecision;
    XMutex_unlock(e->m_mutex);
    return decision;
}

#endif /* XUPGRADE_ON */
