/**
 * @file xupgrade_e2e_test.c
 * @brief XUpgrade 自升级模块 M1 端到端联调测试（独立程序，进程内 XHttpServer 静态服务）。
 * @details 覆盖四层（设计文档 XUpgrade.md §18 M1 范围 + §16 断电矩阵 1-7 的
 *          进程内近似）：
 *          1. 单测：版本比较全边界；manifest 解析（含字段缺失/坏版本串/路径
 *             逃逸拒绝）；.pak 写→读→逐条目校验回环（含压缩条目与损坏拒绝）；
 *             DirStore 在 scratch 根的 stage→activate→current→rollback→
 *             listVersions；state.json 原子写回环（CRC/schema 防护）；
 *             身份提供者回调契约。
 *          2. 端到端：init→checkNow→startUpgrade→轮询终态（单调钟，超时
 *             30s 判失败）→applyNow（引擎内切指针：Applying→BootConfirming
 *             落盘）→XUpgradeApplyStub_main 幂等无操作→断言指针已切、引擎
 *             selfCheck 通过（BootConfirming→Active）→再检查无更新。
 *          3. 断电/损坏模拟：下载中断（矩阵 1，partial 保留后整包重下）、
 *             校验中断（矩阵 2）、解包中断（矩阵 3）、staged 标记缺失时
 *             apply 拒绝（矩阵 4/不变式 1）、Applying 中断引擎启动续做切换
 *             （矩阵 5）、重复 apply 幂等（矩阵 7）、指针指向被删目录自检
 *             回滚、损坏新版本关键文件自检回滚、回滚计数超限停用自动检查、
 *             引擎锁防双实例。
 *          断言风格对齐仓库回归测试：XPrintf 进度+失败计数，退出码=失败用
 *          例数（上限 250），全过返回 0。
 * @note    跨平台：仅依赖 XinYueC 自带 API，零平台 API。工作目录必须是仓库
 *          bin/（scratch 目录为相对路径 xupgrade_e2e_scratch_XXXX，跑前清空
 *          跑后删除）。服务器与引擎工作线程各自泵事件：主线程等待循环里的
 *          XCoreApplication_processEvents 驱动进程内 XHttpServer（同线程
 *          e2e 闭环，先例 XHttpTest.c:1997-2042）。
 *          实现口径备注：引擎 staging 按设计 §6 逐条目解包落盘（条目文件 +
 *          manifest.version + .verify 随 stageEnd 整目录原子落位），selfCheck
 *          按 .verify 清单抽查版本目录内的条目文件——两口径已对齐，真实多
 *          条目包即可走通。夹具仍取单条目 "manifest.version"（内容=版本串）：
 *          与引擎兜底补写的同名标记同内容，.verify 抽查哈希吻合，流水自洽。
 *          执行模型：默认（父进程模式）逐用例以 XProcess_execute_static 重
 *          入自身（--only N 子进程模式）隔离执行——每用例仍是完整的"进程内
 *          XHttpServer + 引擎"闭环，但单用例内的实现层崩溃只判该用例失败，
 *          不中止整套测试（历史缺陷"XUpgradeEngine_deinit 对 XMutex 误用
 *          XClassDelete 导致任何引擎析构即崩溃"已修复：VXUpgradeEngine_deinit
 *          经 XMutex_delete 释放堆锁，引擎 XClassDelete 可正常走完析构）。
 *          --only N 可单独运行第 N 个用例（0 基）。
 *          浸泡模式（门禁口径）：--xupgrade-soak <循环数> 进入独立入口——
 *          每轮完整引擎生命周期（XUpgrade_init→进程内 HTTP 拉 manifest+.pak→
 *          checkNow→startUpgrade→apply（--xupgrade-apply 桩直调）→自检确认
 *          →人为损坏新版本→自检回滚→XUpgrade_deinit），每 10 轮穿插一次
 *          init/deinit 快速启停（停机路径泄漏覆盖）；逐轮采样进程工作集与
 *          XMemory 池统计，轮次结束打印固定格式汇总行（最小二乘拟合判定，
 *          纪律见 xup_soak_run 头注）。该模式经门禁特许在本测试文件直用
 *          win32 PSAPI 采样（豁免理由见浸泡段首注释）；Src 生产代码仍零
 *          平台 API，常规（无参/--only）模式行为不变。
 */

#include "XUpgrade.h"
#include "XUpgradeManifest.h"
#include "XUpgradePackage.h"
#include "XUpgradeStore.h"
#include "XUpgradeStoreDir.h"
#include "XUpgradeEngine.h"

#include "XCoreApplication.h"
#include "XHttpServer.h"
#include "XUrl.h"
#include "XByteArray.h"
#include "XByteArrayView.h"
#include "XString.h"
#include "XCryptographicHash.h"
#include "XFile.h"
#include "XFileInfo.h"
#include "XDir.h"
#include "XStringList.h"
#include "XIODevice.h"
#include "XMemory.h"
#include "XClass.h"
#include "XObject.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XProcess.h"
#include "XSystem.h"
#include "XPrintf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ========================================================================== */
/*        无公开契约头的模块内导出函数（本文件内的局部原型声明）                */
/* ========================================================================== */

/**
 * apply 桩主入口（XUpgradeApplyStub.c:134，无契约头，集成方以 extern 原型
 * 调用；XUpgradeApplyStub.c:13-14 注释明示）。argv 须含 "--xupgrade-apply"，
 * 可含 "--root <路径>"；返回值即进程退出码。
 */
extern int XUpgradeApplyStub_main(int argc, char* argv[]);

/**
 * .pak 写侧序列化/压缩/校验原语（XUpgradePackage.c:228-537 导出，文件头
 * 注释明示"供测试与服务端打包工具组装 .pak"，M2 才统一收口进契约头）。
 */
extern size_t XUpgradePackage_entryWireSize(const XUpgradePackageEntry* entry);
extern bool XUpgradePackage_writeHeader(uint8_t* out, size_t cap, size_t* used,
                                        const XUpgradePackageHeader* header);
extern bool XUpgradePackage_writeEntry(uint8_t* out, size_t cap, size_t* used,
                                       const XUpgradePackageEntry* entry);
extern bool XUpgradePackage_compress(uint8_t* dest, size_t destCap, size_t* destLen,
                                     const uint8_t* src, size_t srcLen);
extern XUpgradeError XUpgradePackage_verify(const uint8_t* bytes, size_t size);

/**
 * XUpgradeVerify 实现侧入口与造签原语（XUpgradeVerify.c/XSsl_mbedtls_p256.c
 * 导出，无契约头，按模块先例本地 extern 声明连接，两侧签名必须一致；
 * XUPGRADE_VERIFY_ON=0 时 XUpgradeVerify.c 整 TU 裁空，故本组声明随宏裁剪）。
 */
#if XUPGRADE_VERIFY_ON
extern XUpgradeError XUpgradeVerify_verifyP256(const uint8_t* content, size_t contentLen,
                                               const uint8_t* signature, size_t signatureLen,
                                               const uint8_t* publicKey, size_t publicKeyLen);
extern XUpgradeError XUpgradeVerify_verifyManifest(const XUpgradeManifest* manifest);
extern bool XUpgradeVerify_setTrustKey(const uint8_t* publicKey, uint32_t len);
extern bool XUpgradeVerify_antiDowngradeAllowed(const char* candidateVersion,
                                                const char* maxVersion,
                                                const char* rollbackTo);
#endif /* XUPGRADE_VERIFY_ON */

/** §10 强制锚判定（模块内契约位，唯一实现在 XUpgradeGray.c；无契约头，
 *  按本文件局部原型口径连接——Verify 侧重复副本随去重删除，判定表改测
 *  本入口；不受 XUPGRADE_VERIFY_ON 门控，故置于 VERIFY_ON 块之外）。 */
extern bool XUpgradeGray_forcedAnchored(const XUpgradeManifest* manifest,
                                        const char* localVersion);

/**
 * mbedtls 造签原语（Library/mbedtls/platform/XSsl_mbedtls_p256.h:80；该目录
 * 不在本测试目标的 include 路径上，按模块先例以本地 extern 声明连接，两侧
 * 签名必须一致）。仅测试造签使用：契约明示其定位即"建包工具/e2e 测试造签"。
 */
#define XUP_TEST_P256_DER_SIG_MAX 72u  /* = XSsl_p256 DER 签名上界（本测试内自洽） */
extern bool XSsl_p256SignHashDer(const uint8_t* privateKeyBe, size_t privateKeyLen,
                                 const uint8_t* sha256Digest, size_t sha256Len,
                                 uint8_t* outDerSignature, size_t cap, size_t* outLen);

/** 测试钥对（XUpgrade_test_keys.h：仅测试 TU 包含，static 数组）。 */
#include "XUpgrade_test_keys.h"

/** base64 编解码（m_sig 造签编码；XBase64.h 于 XAlgorithm，返回 0=成功）。 */
#include "XBase64.h"

/* ========================================================================== */
/*                            测试场景 scratch 根                              */
/* ========================================================================== */

#define XUP_TEST_SCRATCH_UNIT   "xupgrade_e2e_scratch_unit"
#define XUP_TEST_SCRATCH_STORE  "xupgrade_e2e_scratch_store"
#define XUP_TEST_SCRATCH_E2E    "xupgrade_e2e_scratch_e2e"
#define XUP_TEST_SCRATCH_SKIP   "xupgrade_e2e_scratch_skip"
#define XUP_TEST_SCRATCH_M1     "xupgrade_e2e_scratch_m1"
#define XUP_TEST_SCRATCH_M2     "xupgrade_e2e_scratch_m2"
#define XUP_TEST_SCRATCH_M3     "xupgrade_e2e_scratch_m3"
#define XUP_TEST_SCRATCH_M4     "xupgrade_e2e_scratch_m4"
#define XUP_TEST_SCRATCH_M5     "xupgrade_e2e_scratch_m5"
#define XUP_TEST_SCRATCH_CKA    "xupgrade_e2e_scratch_cka"
#define XUP_TEST_SCRATCH_CKB    "xupgrade_e2e_scratch_ckb"
#define XUP_TEST_SCRATCH_FRSH   "xupgrade_e2e_scratch_frsh"
#define XUP_TEST_SCRATCH_LIMIT  "xupgrade_e2e_scratch_limit"
#define XUP_TEST_SCRATCH_LOCK   "xupgrade_e2e_scratch_lock"

/** @brief 单调钟等待超时（毫秒；设计文档要求 30s 判失败）。 */
#define XUP_TEST_WAIT_MS 30000

/* ========================================================================== */
/*                          基础工具（串/格式化/哈希）                          */
/* ========================================================================== */

/** @brief 比较 NUL 结尾串是否相等（string.h 比较函数被禁，自实现）。 */
static bool xtest_str_eq(const char* a, const char* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    while (*a != '\0' && *a == *b) { ++a; ++b; }
    return *a == *b;
}

/** @brief 取 NUL 结尾串长度。 */
static size_t xtest_str_len(const char* s)
{
    size_t n = 0;
    if (!s) return 0;
    while (s[n] != '\0') ++n;
    return n;
}

/** @brief 复制 NUL 结尾串到定长缓冲（超长截断保证 NUL）。 */
static bool xtest_str_copy(char* dst, size_t cap, const char* src)
{
    size_t i;
    if (!dst || cap == 0) return false;
    if (!src) { dst[0] = '\0'; return true; }
    for (i = 0; i + 1 < cap && src[i] != '\0'; ++i) dst[i] = src[i];
    dst[i] = '\0';
    return src[i] == '\0';
}

/** @brief 格式化到定长缓冲（XString_create_fmt_utf8 转发，先例 xup_fmt）。 */
#define XTEST_FMT(dst, cap, ...)                                               \
    do {                                                                        \
        XString* xtest_fmt_s_ = XString_create_fmt_utf8(__VA_ARGS__);           \
        if (xtest_fmt_s_) {                                                     \
            xtest_str_copy((dst), (cap), XString_toUtf8(xtest_fmt_s_));         \
            XClassDelete(xtest_fmt_s_);                                         \
        } else {                                                                \
            (dst)[0] = '\0';                                                    \
        }                                                                       \
    } while (0)

/** @brief 二进制转小写十六进制；out 容量至少 2*len+1。 */
static void xtest_bin2hex(const uint8_t* bin, size_t len, char* out)
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

/** @brief 单段内存 SHA-256（先例 XUpgradeEngine.c:283 xup_sha256_bytes）。 */
static bool xtest_sha256(const uint8_t* data, size_t len, uint8_t out[XUPGRADE_SHA256_BIN_SIZE])
{
    XCryptographicHash hash;
    XByteArrayView view;
    if (!out || (!data && len > 0)) return false;
    XCryptographicHash_init(&hash, XCryptographicHash_Sha256);
    view = XByteArrayView_create_data(data, (int64_t)len);
    XCryptographicHash_addData_2(&hash, view);
    view = XCryptographicHash_resultView(&hash);
    if (XByteArrayView_size(&view) != XUPGRADE_SHA256_BIN_SIZE) {
        XCryptographicHash_deinit(&hash);
        return false;
    }
    XMemcpy(out, XByteArrayView_data(&view), XUPGRADE_SHA256_BIN_SIZE);
    XCryptographicHash_deinit(&hash);
    return true;
}

/** @brief 字节数组长度（XByteArray_size_base 的显式下行转换包装）。 */
static size_t xtest_bsize(const XByteArray* bytes)
{
    return bytes ? XByteArray_size_base((const XContainer*)bytes) : 0;
}

/** @brief 字节数组只读数据（XByteArray_constData 的显式转换包装）。 */
static const uint8_t* xtest_bdata(const XByteArray* bytes)
{
    return bytes ? (const uint8_t*)XByteArray_constData((XByteArray*)bytes) : NULL;
}

/* ========================================================================== */
/*                        文件/目录小工具（XFile/XDir 族）                      */
/* ========================================================================== */

/** @brief 递归创建目录（先例 XUpgradeEngine.c:396 xup_make_dirs）。 */
static bool xtest_make_dirs(const char* pathUtf8)
{
    XDir* dir;
    XString* path;
    XString* dot;
    bool ok;
    if (!pathUtf8) return false;
    path = XString_create_utf8(pathUtf8); /* XDir_create_2 仅借用，不接管所有权 */
    dir = path ? XDir_create_2(path) : NULL;
    XClassDelete(path);
    if (!dir) return false;
    dot = XString_create_utf8(".");
    ok = dot && XDir_mkpath(dir, dot);
    if (dot) XClassDelete(dot);
    XClassDelete((XClass*)dir);
    return ok;
}

/** @brief 整读文件；成功时返回新 XByteArray（调用者释放），失败返回 NULL。 */
static XByteArray* xtest_read_file(const char* pathUtf8)
{
    XFile* file;
    XByteArray* bytes;
    XString* path;
    if (!pathUtf8) return NULL;
    path = XString_create_utf8(pathUtf8);
    file = path ? XFile_create_2(path) : NULL;
    XClassDelete(path);
    if (!file) return NULL;
    if (!XFile_open_2(file, XIODevice_ReadOnly, 0)) {
        XClassDelete((XClass*)file);
        return NULL;
    }
    bytes = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    XClassDelete((XClass*)file);
    return bytes;
}

/** @brief 写整文件（父目录按需创建；不原子，测试夹具用）。 */
static bool xtest_write_file(const char* pathUtf8, const char* data, size_t len)
{
    XFile* file;
    XString* path;
    char dir[1200];
    size_t i;
    size_t lastSlash = 0;
    bool hasSlash = false;
    bool ok;
    if (!pathUtf8 || (!data && len > 0)) return false;
    for (i = 0; pathUtf8[i] != '\0'; ++i) {
        if (pathUtf8[i] == '/' || pathUtf8[i] == '\\') {
            lastSlash = i;
            hasSlash = true;
        }
    }
    if (hasSlash) {
        if (lastSlash + 1 >= sizeof(dir)) return false;
        XMemcpy(dir, pathUtf8, lastSlash);
        dir[lastSlash] = '\0';
        if (!xtest_make_dirs(dir)) return false;
    }
    path = XString_create_utf8(pathUtf8);
    file = path ? XFile_create_2(path) : NULL;
    XClassDelete(path);
    if (!file) return false;
    ok = XFile_open_2(file, XIODevice_WriteOnly | XIODevice_Truncate | XIODevice_Create, 0);
    if (ok && len > 0)
        ok = XIODevice_write_1((XIODevice*)file, data, (int64_t)len) == (int64_t)len;
    XIODevice_close_base((XIODevice*)file);
    XClassDelete((XClass*)file);
    return ok;
}

/** @brief 读整文件到定长缓冲并取长度（多出的内容截断）。 */
static bool xtest_read_into(const char* pathUtf8, char* buf, size_t cap, size_t* lenOut)
{
    XByteArray* bytes = xtest_read_file(pathUtf8);
    size_t n;
    if (!bytes || !buf || cap == 0) { if (bytes) XClassDelete(bytes); return false; }
    n = xtest_bsize(bytes);
    if (n > cap - 1) n = cap - 1;
    if (n > 0) XMemcpy(buf, XByteArray_constData(bytes), n);
    buf[n] = '\0';
    XClassDelete(bytes);
    if (lenOut) *lenOut = n;
    return true;
}

/** @brief 路径是否存在（文件或目录，先例 XUpgradeStoreDir.c:246）。 */
static bool xtest_path_exists(const char* pathUtf8)
{
    XString* path;
    bool exists;
    if (!pathUtf8) return false;
    path = XString_create_utf8(pathUtf8);
    exists = path && XFileInfo_exists_static(path);
    XClassDelete(path);
    return exists;
}

/** @brief 列出目录下的文件数（不存在返回 -1）。 */
static int xtest_file_count(const char* dirUtf8)
{
    XDir* dir;
    XStringList* entries;
    XString* path;
    int count;
    if (!dirUtf8) return -1;
    path = XString_create_utf8(dirUtf8);
    dir = path ? XDir_create_2(path) : NULL;
    XClassDelete(path);
    if (!dir) return -1;
    if (!XDir_exists_1(dir)) { XClassDelete((XClass*)dir); return -1; }
    entries = XDir_entryList_1(dir, XDir_Files, XDir_NoSort);
    count = entries ? (int)XStringList_size_base((const XContainer*)entries) : -1;
    if (entries) XClassDelete(entries);
    XClassDelete((XClass*)dir);
    return count;
}

/** @brief 递归删除 scratch 目录（不存在视作成功）。 */
static void xtest_scratch_wipe(const char* rootUtf8)
{
    XDir* dir;
    XString* path;
    if (!rootUtf8) return;
    path = XString_create_utf8(rootUtf8);
    dir = path ? XDir_create_2(path) : NULL;
    XClassDelete(path);
    if (!dir) return;
    if (XDir_exists_1(dir)) (void)XDir_removeRecursively(dir);
    XClassDelete((XClass*)dir);
}

/* ========================================================================== */
/*                     state.json / 指针/版本目录夹具工具                       */
/* ========================================================================== */

/** @brief 拼 <root>/update 工作区路径。 */
static void xtest_update_dir(const char* rootUtf8, char* out, size_t cap)
{
    XTEST_FMT(out, cap, "%s/" XUPGRADE_DIR_UPDATE, rootUtf8);
}

/** @brief 保存状态结构到 <root>/update/state.json（原子写，引擎入口）。 */
static bool xtest_save_state(const char* rootUtf8, const XUpgradeStateFile* state)
{
    char updateDir[1200];
    xtest_update_dir(rootUtf8, updateDir, sizeof(updateDir));
    return XUpgradeStateFile_save(state, updateDir);
}

/** @brief 从 <root>/update/state.json 加载（引擎入口）。 */
static bool xtest_load_state(const char* rootUtf8, XUpgradeStateFile* out)
{
    char updateDir[1200];
    xtest_update_dir(rootUtf8, updateDir, sizeof(updateDir));
    return XUpgradeStateFile_load(out, updateDir);
}

/** @brief 写引擎锁口径的部分下载文件 update/partial/<fileName>。 */
static bool xtest_write_partial(const char* rootUtf8, const char* fileName,
                                const char* data, size_t len)
{
    char path[1400];
    XTEST_FMT(path, sizeof(path), "%s/" XUPGRADE_DIR_UPDATE "/" XUPGRADE_DIR_PARTIAL "/%s",
              rootUtf8, fileName);
    return xtest_write_file(path, data, len);
}

/** @brief 断言版本目录完整标记：写 versions/<comp>/<ver>/manifest.version。 */
static bool xtest_seed_version_dir(const char* rootUtf8, const char* comp, const char* version)
{
    char path[1400];
    XTEST_FMT(path, sizeof(path), "%s/" XUPGRADE_DIR_VERSIONS "/%s/%s/" XUPGRADE_FILE_VERSION_MARK,
              rootUtf8, comp, version);
    return xtest_write_file(path, version, xtest_str_len(version));
}

/** @brief 写指针文件（内容与 DirStore 口径一致：版本串+换行）。 */
static bool xtest_write_pointer(const char* rootUtf8, const char* comp, const char* version)
{
    char path[1200];
    char content[64];
    XTEST_FMT(path, sizeof(path), "%s/" XUPGRADE_DIR_POINTERS "/%s", rootUtf8, comp);
    XTEST_FMT(content, sizeof(content), "%s\n", version);
    return xtest_write_file(path, content, xtest_str_len(content));
}

/** @brief 读指针文件并去尾部换行；读不到返回 false。 */
static bool xtest_read_pointer(const char* rootUtf8, const char* comp, char* out, size_t cap)
{
    char path[1200];
    XByteArray* bytes;
    size_t n;
    XTEST_FMT(path, sizeof(path), "%s/" XUPGRADE_DIR_POINTERS "/%s", rootUtf8, comp);
    bytes = xtest_read_file(path);
    if (!bytes) return false;
    n = xtest_bsize(bytes);
    if (n > cap - 1) n = cap - 1;
    if (n > 0) XMemcpy(out, XByteArray_constData(bytes), n);
    out[n] = '\0';
    XClassDelete(bytes);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' ')) {
        out[--n] = '\0';
    }
    return true;
}

/* ========================================================================== */
/*                        .pak 构建器（写侧原语组装）                           */
/* ========================================================================== */

/** @brief 待打包条目（内容为借用指针）。 */
typedef struct XTestPakEntry {
    const char* m_name;        /**< 条目相对路径。 */
    uint8_t m_flags;           /**< 0=stored；XUPGRADE_PACKAGE_FLAG_ZLIB=zlib。 */
    const uint8_t* m_data;     /**< 解压后内容；借用。 */
    size_t m_len;              /**< 解压后字节数。 */
} XTestPakEntry;

/**
 * @brief 按 §5 磁盘布局组装 .pak（头+条目表+数据区，小端）。
 * @details 三遍式：先逐条目拿真实压缩长度（zlib 确定性，两次压缩同长）定
 *          headerSize/payloadSize；再分配缓冲回填条目表与数据区；最后整段
 *          数据区哈希写头。
 * @return 成功时 *outBytes 为新 XByteArray（调用者释放），同时回填 payloadSize。
 */
static bool xtest_build_pak(const XTestPakEntry* entries, uint32_t count,
                            XByteArray** outBytes, uint64_t* payloadSizeOut)
{
    size_t compLen[16];
    size_t headerSize = XUPGRADE_PACKAGE_FIXED_HEADER_SIZE;
    size_t payloadLen = 0;
    size_t entryPos = 0;
    size_t dataPos = 0;
    uint64_t running = 0;
    uint8_t* buffer = NULL;
    uint8_t payloadSha[XUPGRADE_SHA256_BIN_SIZE];
    XUpgradePackageHeader header;
    uint32_t i;
    *outBytes = NULL;
    if (!entries || count == 0 || count > 16 || !outBytes) return false;
    /* 第一遍：真实压缩长度 → headerSize/payloadSize。 */
    for (i = 0; i < count; ++i) {
        XUpgradePackageEntry probe;
        XMemset(&probe, 0, sizeof(probe));
        xtest_str_copy(probe.m_name, XUPGRADE_PACKAGE_NAME_MAX, entries[i].m_name);
        probe.m_flags = entries[i].m_flags;
        probe.m_rawSize = (uint64_t)entries[i].m_len;
        if (entries[i].m_flags & XUPGRADE_PACKAGE_FLAG_ZLIB) {
            size_t compCap = entries[i].m_len * 2u + 1024u;
            uint8_t* tmp = (uint8_t*)XMalloc_System(compCap);
            if (!tmp) { XPrintf("[pak] 压缩缓冲分配失败\n"); return false; }
            if (!XUpgradePackage_compress(tmp, compCap, &compLen[i],
                                          entries[i].m_data, entries[i].m_len)) {
                XFree_System(tmp);
                XPrintf("[pak] XUpgradePackage_compress 失败\n");
                return false;
            }
            XFree_System(tmp);
            probe.m_compSize = (uint64_t)compLen[i];
        } else {
            compLen[i] = entries[i].m_len;
            probe.m_compSize = probe.m_rawSize;
        }
        {
            size_t w = XUpgradePackage_entryWireSize(&probe);
            if (w == 0) { XPrintf("[pak] entryWireSize 拒绝条目\n"); return false; }
            headerSize += w;
        }
        payloadLen += compLen[i];
    }
    buffer = (uint8_t*)XMalloc_System(headerSize + payloadLen);
    if (!buffer) return false;
    /* 第二遍：条目表+数据区（offset 相对数据区起点）。 */
    entryPos = XUPGRADE_PACKAGE_FIXED_HEADER_SIZE;
    dataPos = headerSize;
    for (i = 0; i < count; ++i) {
        XUpgradePackageEntry entry;
        size_t used = 0;
        XMemset(&entry, 0, sizeof(entry));
        xtest_str_copy(entry.m_name, XUPGRADE_PACKAGE_NAME_MAX, entries[i].m_name);
        entry.m_flags = entries[i].m_flags;
        entry.m_rawSize = (uint64_t)entries[i].m_len;
        entry.m_compSize = (uint64_t)compLen[i];
        entry.m_offset = running;
        if (!xtest_sha256(entries[i].m_data, entries[i].m_len, entry.m_sha256) ||
            !XUpgradePackage_writeEntry(buffer + entryPos, headerSize - entryPos,
                                        &used, &entry)) {
            XFree_System(buffer);
            XPrintf("[pak] 条目摘要/序列化失败\n");
            return false;
        }
        entryPos += used;
        if (entries[i].m_flags & XUPGRADE_PACKAGE_FLAG_ZLIB) {
            /* zlib compressBound 下限检查与实际输出无关：压缩到宽裕临时缓冲
             * （与第一遍同容量，输出定长一致）再拷入数据区。 */
            size_t compCap = entries[i].m_len * 2u + 1024u;
            size_t actualLen = 0;
            uint8_t* tmp = (uint8_t*)XMalloc_System(compCap);
            if (!tmp) { XFree_System(buffer); return false; }
            if (!XUpgradePackage_compress(tmp, compCap, &actualLen,
                                          entries[i].m_data, entries[i].m_len) ||
                actualLen != compLen[i]) { /* zlib 确定性：两次压缩同长 */
                XFree_System(tmp);
                XFree_System(buffer);
                XPrintf("[pak] 数据区压缩失败\n");
                return false;
            }
            XMemcpy(buffer + dataPos, tmp, compLen[i]);
            XFree_System(tmp);
        } else {
            XMemcpy(buffer + dataPos, entries[i].m_data, entries[i].m_len);
        }
        dataPos += compLen[i];
        running += (uint64_t)compLen[i];
    }
    /* 第三遍：数据区整段 SHA-256 写头。 */
    if (!xtest_sha256(buffer + headerSize, payloadLen, payloadSha)) {
        XFree_System(buffer);
        return false;
    }
    XMemset(&header, 0, sizeof(header));
    header.m_format = XUPGRADE_PACKAGE_FORMAT;
    header.m_entryCount = count;
    header.m_headerSize = (uint32_t)headerSize;
    header.m_payloadSize = (uint64_t)payloadLen;
    XMemcpy(header.m_payloadSha256, payloadSha, XUPGRADE_SHA256_BIN_SIZE);
    {
        size_t used = 0;
        if (!XUpgradePackage_writeHeader(buffer, headerSize, &used, &header)) {
            XFree_System(buffer);
            return false;
        }
    }
    {
        XByteArray* bytes = XByteArray_create_with_data((const char*)buffer,
                                                        headerSize + payloadLen);
        XFree_System(buffer);
        if (!bytes) return false;
        *outBytes = bytes;
        /* manifest size 契约口径=包文件总字节数（XUpgradeManifest.h:96），
         * 供下载进度与磁盘预检；引擎按同口径强校验。 */
        if (payloadSizeOut) *payloadSizeOut = (uint64_t)(headerSize + payloadLen);
    }
    return true;
}

/** @brief .pak 整文件 SHA-256 十六进制（manifest sha256 字段口径）。 */
static bool xtest_pak_sha256_hex(const XByteArray* pak, char* hexOut, size_t cap)
{
    uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
    if (!pak || !hexOut || cap < XUPGRADE_SHA256_HEX_MAX) return false;
    if (!xtest_sha256(xtest_bdata(pak), xtest_bsize(pak), digest)) return false;
    xtest_bin2hex(digest, XUPGRADE_SHA256_BIN_SIZE, hexOut);
    return true;
}

/* ========================================================================== */
/*                    进程内 XHttpServer 静态服务（manifest+pak）               */
/* ========================================================================== */

/** @brief 静态服务上下文（内存态 manifest/pak 借用指针 + 命中计数）。 */
typedef struct XTestServerContext {
    const XByteArray* m_manifest;   /**< manifest.json 字节；借用。 */
    const XByteArray* m_pak;        /**< .pak 字节；借用。 */
    const char* m_pakName;          /**< 包文件名（URL 尾段匹配）。 */
    int m_manifestHits;             /**< manifest 请求命中数。 */
    int m_pakHits;                  /**< 包请求命中数（含 206 分段）。 */
    int m_rangeHits;                /**< Range 请求命中数。 */
    int m_misses;                   /**< 404 数。 */
} XTestServerContext;

/** @brief 请求头取值（新 XByteArray，调用者释放；缺失返回 NULL）。 */
static XByteArray* xtest_header_value(const XHttpServerRequest* request, const char* name)
{
    XByteArray* key = XByteArray_create_utf8(name);
    XByteArray* value = key ? XHttpServerRequest_value(request, key) : NULL;
    if (key) XClassDelete(key);
    return value;
}

/** @brief 解析 "bytes=N-" 起始偏移；非 Range 语义返回 false。 */
static bool xtest_parse_range_from(const char* range, size_t len, uint64_t* fromOut)
{
    uint64_t value = 0;
    size_t i;
    if (!range || !fromOut || len < 7) return false;
    while (len > 0 && *range == ' ') { ++range; --len; }
    if (len < 7 || XMemcmp(range, "bytes=", 6) != 0) return false;
    range += 6;
    len -= 6;
    if (*range < '0' || *range > '9') return false;
    for (i = 0; i < len && range[i] >= '0' && range[i] <= '9'; ++i) {
        value = value * 10u + (uint64_t)(range[i] - '0');
    }
    for (; i < len; ++i) {
        if (range[i] == '-') break;
        if (range[i] != ' ') return false;
    }
    *fromOut = value;
    return true;
}

/** @brief 静态文件处理器：manifest/pak 命中应答，Range 单段 206，其余 404。 */
static void xtest_server_handler(const XHttpServerRequest* request,
                                 XHttpServerResponder* responder,
                                 void* context)
{
    XTestServerContext* ctx = (XTestServerContext*)context;
    const XUrl* url = XHttpServerRequest_url_const(request);
    const XString* path = url ? XUrl_path_const(url) : NULL;
    const char* pathUtf8 = path ? XString_toUtf8(path) : NULL;
    const char* mimeUtf8 = "application/octet-stream";
    XByteArray* mime = XByteArray_create_utf8(mimeUtf8);
    if (!ctx || !mime) {
        if (mime) XClassDelete(mime);
        (void)XHttpServerResponder_writeStatus(responder, XHttpServerResponse_InternalServerError);
        return;
    }
    if (pathUtf8 && xtest_str_len(pathUtf8) >= xtest_str_len("manifest.json") &&
        xtest_str_eq(pathUtf8 + xtest_str_len(pathUtf8) - xtest_str_len("manifest.json"),
                     "manifest.json") &&
        ctx->m_manifest) {
        ++ctx->m_manifestHits;
        (void)XHttpServerResponder_write(responder, ctx->m_manifest, mime,
                                         XHttpServerResponse_Ok);
    } else if (pathUtf8 && ctx->m_pak && ctx->m_pakName &&
               xtest_str_len(pathUtf8) >= xtest_str_len(ctx->m_pakName) &&
               xtest_str_eq(pathUtf8 + xtest_str_len(pathUtf8) - xtest_str_len(ctx->m_pakName),
                            ctx->m_pakName)) {
        size_t total = xtest_bsize(ctx->m_pak);
        const uint8_t* data = xtest_bdata(ctx->m_pak);
        XByteArray* range = xtest_header_value(request, "Range");
        uint64_t from = 0;
        bool isRange = false;
        if (range) {
            /* 头值不保证 NUL 结尾：拷入定长缓冲再解析。 */
            char rangeBuf[64];
            size_t rangeLen = xtest_bsize(range);
            if (rangeLen > sizeof(rangeBuf) - 1) rangeLen = sizeof(rangeBuf) - 1;
            XMemcpy(rangeBuf, xtest_bdata(range), rangeLen);
            rangeBuf[rangeLen] = '\0';
            isRange = xtest_parse_range_from(rangeBuf, rangeLen, &from);
        }
        if (isRange && from > 0 && from < (uint64_t)total) {
            /* 单段 206：Content-Range + 余量正文（引擎续传口径）。 */
            XHttpServerResponse* response;
            XByteArray* slice = XByteArray_create_with_data((const char*)(data + from),
                                                            total - (size_t)from);
            char contentRange[96];
            ++ctx->m_pakHits;
            ++ctx->m_rangeHits;
            XTEST_FMT(contentRange, sizeof(contentRange), "bytes %llu-%llu/%llu",
                      (unsigned long long)from, (unsigned long long)(total - 1),
                      (unsigned long long)total);
            response = slice ? XHttpServerResponse_create_mime(mime, slice,
                                                               XHttpServerResponse_PartialContent)
                             : NULL;
            if (response)
                XHttpHeaders_append_utf8(XHttpServerResponse_headers(response),
                                         "Content-Range", contentRange);
            (void)XHttpServerResponder_sendResponse(responder, response);
            if (response) XClassDelete((XClass*)response);
            if (slice) XClassDelete(slice);
        } else if (isRange && from >= (uint64_t)total) {
            ++ctx->m_pakHits;
            (void)XHttpServerResponder_writeStatus(responder,
                                                   XHttpServerResponse_RequestRangeNotSatisfiable);
        } else {
            ++ctx->m_pakHits;
            (void)XHttpServerResponder_write(responder, ctx->m_pak, mime,
                                             XHttpServerResponse_Ok);
        }
        if (range) XClassDelete(range);
    } else {
        ++ctx->m_misses;
        (void)XHttpServerResponder_writeStatus(responder, XHttpServerResponse_NotFound);
    }
    XClassDelete(mime);
}

/* ========================================================================== */
/*                        全局服务/事件/身份提供者                              */
/* ========================================================================== */

/** @brief 进程内静态服务器与上下文（main 创建，全用例共享）。 */
static XHttpServer* g_xup_server = NULL;
static XTestServerContext g_xup_serverCtx;
static char g_xup_manifestUrl[256];   /**< 含 {channel} 占位的 manifest URL。 */

/** @brief 版本提供者：返回固定 "1.0.0"（e2e 本地基线）。 */
static void xtest_version_provider(void* userData, char* out, uint32_t cap)
{
    (void)userData;
    xtest_str_copy(out, cap, "1.0.0");
}

/** @brief 序列号提供者：返回固定测试序列号。 */
static void xtest_serial_provider(void* userData, char* out, uint32_t cap)
{
    (void)userData;
    xtest_str_copy(out, cap, "SN-XUP-E2E-001");
}

/** @brief 事件统计（引擎工作线程写、主线程读；单写单读计数即可）。 */
typedef struct XTestEvents {
    volatile int m_total;              /**< 全部事件数。 */
    volatile int m_updateAvailable;    /**< "发现可用更新"。 */
    volatile int m_readyNeedRestart;   /**< "升级就绪，重启以应用"。 */
    volatile int m_noUpdate;           /**< CheckFailed"暂无更新"。 */
    volatile int m_checkFailedOther;   /**< 其余 CheckFailed。 */
    volatile int m_downloadFailed;     /**< DownloadFailed。 */
    volatile int m_rolledBack;         /**< RolledBack。 */
    volatile int m_dlProgress;         /**< Download 阶段进度事件数。 */
    volatile int m_dlMaxPercent;       /**< Download 进度最大百分比。 */
    volatile int m_verifyDone;         /**< Verify percent==100 次数。 */
    volatile int m_stageDone;          /**< Stage percent==100 次数。 */
    char m_lastDetail[XUPGRADE_DETAIL_MAX]; /**< 最近一条事件说明。 */
} XTestEvents;

static XTestEvents g_xup_events;

/** @brief 清零事件统计。 */
static void xtest_events_reset(void)
{
    XMemset((void*)&g_xup_events, 0, sizeof(g_xup_events));
}

/** @brief 引擎事件监听器（仅统计，回调期对象不保存指针）。 */
static void xtest_listener(void* userData, const XUpgradeEvent* event)
{
    (void)userData;
    if (!event) return;
    ++g_xup_events.m_total;
    xtest_str_copy(g_xup_events.m_lastDetail,
                   sizeof(g_xup_events.m_lastDetail), event->m_detail);
    /* M2/M3：UpdateAvailable 升级为类型化事件（XUpgradeEventType_UpdateAvailable
     * 携带 m_policy/m_gray，detail 为空），与 demo 更新页同按 m_type 匹配——
     * 旧 m_state+detail 串口径随 xup_emit_update_available 收口而失效。 */
    if (event->m_type == XUpgradeEventType_UpdateAvailable) {
        ++g_xup_events.m_updateAvailable;
    } else if (event->m_state == XUpgradeState_Ready &&
               xtest_str_eq(event->m_detail, "升级就绪，重启以应用")) {
        ++g_xup_events.m_readyNeedRestart;
    } else if (event->m_state == XUpgradeState_CheckFailed) {
        if (xtest_str_eq(event->m_detail, "暂无更新")) ++g_xup_events.m_noUpdate;
        else ++g_xup_events.m_checkFailedOther;
    } else if (event->m_state == XUpgradeState_DownloadFailed) {
        ++g_xup_events.m_downloadFailed;
    } else if (event->m_state == XUpgradeState_RolledBack) {
        ++g_xup_events.m_rolledBack;
    }
    /* 进度事件（2026-10-07 语义修正后）：百分比进度以 Progress 类型携带
     * m_phase/m_percent（原 StateChanged 携带口径=进度条全程 0% 根因）。 */
    if (event->m_type == XUpgradeEventType_Progress) {
        if (event->m_phase == XUpgradePhase_Download && event->m_percent >= 0) {
            ++g_xup_events.m_dlProgress;
            if (event->m_percent > g_xup_events.m_dlMaxPercent)
                g_xup_events.m_dlMaxPercent = event->m_percent;
        }
        if (event->m_phase == XUpgradePhase_Verify && event->m_percent == 100)
            ++g_xup_events.m_verifyDone;
        if (event->m_phase == XUpgradePhase_Stage && event->m_percent == 100)
            ++g_xup_events.m_stageDone;
    }
}

/** @brief 轮询等待谓词成立（主线程泵事件驱动服务端；单调钟超时）。 */
static bool xtest_wait_until(bool (*predicate)(void*), void* userData)
{
    int64_t deadline = XDateTime_currentMSecsSinceEpoch() + XUP_TEST_WAIT_MS;
    while (!predicate(userData)) {
        if (XDateTime_currentMSecsSinceEpoch() >= deadline) return false;
        XCoreApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(5);
    }
    return true;
}

/**
 * @brief 用例内断言：失败打印行号与表达式并置 ok=false（不中断，便于一次
 *        看全失败点）。要求使用处作用域内存在 bool ok 变量。
 */
#define XTEST_CHECK(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            XPrintf("    [断言失败 行%d] %s\n", __LINE__, #cond);              \
            ok = false;                                                          \
        }                                                                        \
    } while (0)

/* --- 等待谓词（闭包用全局计数器） --- */

static bool xup_wait_update_available(void* ud)
{
    (void)ud;
    return g_xup_events.m_updateAvailable > 0;
}

static bool xup_wait_no_update(void* ud)
{
    int* baseline = (int*)ud;
    return g_xup_events.m_noUpdate > *baseline;
}

static bool xup_wait_upgrade_done(void* ud)
{
    (void)ud;
    return g_xup_events.m_readyNeedRestart > 0 || g_xup_events.m_downloadFailed > 0 ||
           g_xup_events.m_checkFailedOther > 0;
}

/** @brief 构造引擎启动配置（root=scratch；URL 指向进程内服务器）。 */
static void xtest_fill_config(XUpgradeConfig* config, const char* rootUtf8)
{
    XMemset(config, 0, sizeof(*config));
    config->m_updateUrl = g_xup_manifestUrl;
    config->m_channel = "stable";
    config->m_targetOs = "win64";
    config->m_installRoot = rootUtf8;
    config->m_checkIntervalHours = 0;
    config->m_autoCheck = false;
}

/* ========================================================================== */
/*                        e2e/升级流共享夹具                                    */
/* ========================================================================== */

/** @brief 共享 .pak/manifest 字节（main 构建一次）。 */
static XByteArray* g_xup_pak = NULL;
static XByteArray* g_xup_manifest = NULL;
static const char* g_xup_pakName = "app-2.0.0-win64.pak";
static const char* g_xup_version = "2.0.0";
static uint64_t g_xup_pakPayloadSize = 0; /**< .pak 包文件总字节数（manifest size 口径）。 */

/** @brief 构建共享 manifest+pak；失败返回 false（main 判致命）。 */
/**
 * @brief 以测试私钥为引擎级夹具 manifest 造真签（M2 验签门已接入检查链）。
 * @param jsonUtf8 夹具 manifest JSON 文本（sig 字段为占位值）；借用。
 * @param sigFieldLiteral 待替换的 "sig":"<占位>" 完整字段字面量；借用。
 * @return 签名后的新 manifest XByteArray（调用者释放）；任一步失败返回 NULL
 *         （夹具构造失败即用例失败）。
 * @details 流程即 §4.2：parse → canonicalBytes → SHA-256 → 测试私钥 P-256
 *          DER 造签（XSsl_p256SignHashDer）→ base64 回填 sig。默认信任钥
 *          （XUpgrade_trust_pubkey.h 占位=测试钥公钥）验证通过。签名覆盖除
 *          sig 外全部字段：夹具任何字段改动都必须重新造签。
 */
#if XUPGRADE_VERIFY_ON
static XByteArray* xtest_sign_manifest_json(const char* jsonUtf8,
                                            const char* sigFieldLiteral)
{
    XUpgradeManifest parsed;
    static uint8_t canonical[XUPGRADE_MANIFEST_CANONICAL_MAX];
    uint32_t canonLen = 0;
    uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
    uint8_t der[XUP_TEST_P256_DER_SIG_MAX];
    size_t derLen = 0;
    char sigB64[XUPGRADE_SIGNATURE_MAX];
    size_t sigB64Len = 0;
    char signedField[XUPGRADE_SIGNATURE_MAX + 16];
    XString* text;
    XString* replacement;
    XByteArray* bytes;
    if (!jsonUtf8 || !sigFieldLiteral) return NULL;
    if (!XUpgradeManifest_parse(&parsed, jsonUtf8, xtest_str_len(jsonUtf8))) return NULL;
    if (!XUpgradeManifest_canonicalBytes(&parsed, canonical,
                                         (uint32_t)sizeof(canonical), &canonLen)) return NULL;
    if (!xtest_sha256(canonical, canonLen, digest)) return NULL;
    if (!XSsl_p256SignHashDer(XUpgrade_testPrivateKey, sizeof(XUpgrade_testPrivateKey),
                              digest, sizeof(digest), der, sizeof(der), &derLen)) return NULL;
    sigB64Len = sizeof(sigB64);
    if (XBase64_encode(der, derLen, sigB64, &sigB64Len) != 0) return NULL;
    if (xtest_str_len(sigB64) + 12 >= sizeof(signedField)) return NULL;
    XTEST_FMT(signedField, sizeof(signedField), "\"sig\":\"%s\"", sigB64);
    text = XString_create_utf8(jsonUtf8);
    if (!text) return NULL;
    replacement = XString_create_utf8(signedField);
    if (!replacement) { XClassDelete(text); return NULL; }
    if (!XString_replace_utf8(text, sigFieldLiteral, XString_toUtf8(replacement),
                              XChar_CaseSensitive) ||
        !XString_contains_utf8(text, XString_toUtf8(replacement), XChar_CaseSensitive)) {
        XPrintf("    [夹具造签] sig 占位替换失败\n");
        XClassDelete(replacement);
        XClassDelete(text);
        return NULL;
    }
    XClassDelete(replacement);
    bytes = XByteArray_create_with_data(XString_toUtf8(text), XString_toUtf8_length(text));
    XClassDelete(text);
    return bytes;
}
#endif /* XUPGRADE_VERIFY_ON */

static bool xtest_build_fixtures(void)
{
    static const uint8_t markContent[] = "2.0.0";
    /* 第二条目：让引擎级流水（staging 校验/解包/抽查）跑在多条目包上——
     * 2026-10-07 parseEntry consumed 错位（第 2 条起表错位）正是被单条目
     * 夹具掩盖的真包事故，此条目为回归守卫。 */
    static const uint8_t markerContent[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    XTestPakEntry entries[2];
    char shaHex[XUPGRADE_SHA256_HEX_MAX];
    XMemset(entries, 0, sizeof(entries));
    entries[0].m_name = XUPGRADE_FILE_VERSION_MARK; /* "manifest.version"：见文件头口径备注 */
    entries[0].m_flags = XUPGRADE_PACKAGE_FLAG_ZLIB;
    entries[0].m_data = markContent;
    entries[0].m_len = 5;
    entries[1].m_name = "res/marker.bin";
    entries[1].m_flags = XUPGRADE_PACKAGE_FLAG_ZLIB;
    entries[1].m_data = markerContent;
    entries[1].m_len = sizeof(markerContent);
    if (!xtest_build_pak(entries, 2, &g_xup_pak, &g_xup_pakPayloadSize)) {
        XPrintf("[fixture] xtest_build_pak 失败\n");
        return false;
    }
    if (!xtest_pak_sha256_hex(g_xup_pak, shaHex, sizeof(shaHex))) {
        XPrintf("[fixture] pak sha256 失败\n");
        return false;
    }
    {
        XString* json = XString_create_fmt_utf8(
            "{\"schema\":1,\"channel\":\"stable\",\"version\":\"2.0.0\","
            "\"build\":\"2026.10.06-01\",\"min_version\":\"1.0.0\","
            "\"rollback_to\":\"1.0.0\",\"policy\":\"optional\","
            /* M2/M3 灰度契约已生效（XUpgradeManifest_grayHit 算法入契约）：
             * percent=0 + 空名单 = 名单外设备全部拒绝，本套流水线用例会被
             * 判"暂无更新（灰度未命中）"而失步。取 percent=100（全量放行），
             * 用例跨机器确定性命中（deviceKey 随机器而异，名单法不可移植），
             * 且端到端真实走过 Hit 判定路径。 */
            "\"gray\":{\"percent\":100,\"salt\":\"e2e\",\"allow\":[],\"deny\":[]},"
            "\"components\":[{\"id\":\"app\",\"os\":\"win64\",\"version\":\"2.0.0\","
            "\"file\":\"%s\",\"size\":%llu,\"sha256\":\"%s\"}],"
            "\"sig\":\"e2e-test-unsigned\"}",
            g_xup_pakName,
            (unsigned long long)g_xup_pakPayloadSize,
            shaHex);
        if (!json) {
            XPrintf("[fixture] manifest JSON 格式化失败\n");
            return false;
        }
#if XUPGRADE_VERIFY_ON
        /* M2 验签门已接入引擎检查链（§4.2）：夹具以测试私钥造真签。 */
        g_xup_manifest = xtest_sign_manifest_json(XString_toUtf8(json),
                                                  "\"sig\":\"e2e-test-unsigned\"");
        XClassDelete(json);
        if (!g_xup_manifest) {
            XPrintf("[fixture] manifest 造签失败\n");
            return false;
        }
#else
        g_xup_manifest = XByteArray_create_with_data(XString_toUtf8(json),
                                                     XString_toUtf8_length(json));
        XClassDelete(json);
#endif
    }
    if (!g_xup_manifest) XPrintf("[fixture] manifest XByteArray 失败\n");
    return g_xup_manifest != NULL;
}

/* ========================================================================== */
/*                          单测：版本比较全边界                                */
/* ========================================================================== */

static bool t_version_compare(void)
{
    static const struct {
        const char* m_lhs;
        const char* m_rhs;
        int m_sign;              /**< 期望符号：-1/0/+1。 */
    } cases[] = {
        { "1.2.3", "1.2.3", 0 },
        { "2.0", "2.0.0", 0 },        /* 缺段补 0 */
        { "1.02.3", "1.2.3", 0 },     /* 前导零 */
        { "1.0.0.0", "1.0.0", 0 },
        { "0", "0.0", 0 },
        { "", "", 0 },
        { NULL, NULL, 0 },
        { "1.2.3", "1.2.4", -1 },
        { "1.2.9", "1.2.10", -1 },    /* 数值段不按字典序 */
        { "1.2", "1.2.1", -1 },
        { "0.9.9", "1.0.0", -1 },
        { NULL, "0.0.1", -1 },
        { "0.0.0", "0.0.1", -1 },
        { "2.0.0", "1.9.9", 1 },
        { "2.0", "1.99.0", 1 },
        { "10.0", "9.0", 1 },
        { "1.0.0-a", "1.0.0", 1 },    /* 非数字尾缀 > 缺省 0 段 */
        { "1.0.0a", "1.0.0b", -1 },   /* 尾缀按字节序 */
        { "3.0.0-rc2", "3.0.0-rc10", 1 }, /* 字节序而非数值序（契约口径） */
        { "1.0.0-a", "1.0.0-b", -1 }
    };
    size_t i;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        int r = XUpgradeManifest_compareVersions(cases[i].m_lhs, cases[i].m_rhs);
        int sign = r < 0 ? -1 : (r > 0 ? 1 : 0);
        if (sign != cases[i].m_sign) {
            XPrintf("    [版本比较 #%d: %s vs %s 期望 %d 实得 %d]\n", (int)i,
                    cases[i].m_lhs ? cases[i].m_lhs : "(null)",
                    cases[i].m_rhs ? cases[i].m_rhs : "(null)",
                    cases[i].m_sign, sign);
            return false;
        }
    }
    return true;
}

/* ========================================================================== */
/*                          单测：manifest 解析                                 */
/* ========================================================================== */

/** @brief 一段合法 manifest（含 requires/gray，全字段覆盖）。 */
static const char* k_manifestValid =
    "{\"schema\":1,\"channel\":\"stable\",\"version\":\"2.0.0\","
    "\"build\":\"2026.10.06-01\",\"min_version\":\"1.0.0\","
    "\"rollback_to\":\"1.0.0\",\"policy\":\"optional\","
    "\"gray\":{\"percent\":10,\"salt\":\"r1\",\"allow\":[\"SN001\"],\"deny\":[]},"
    "\"components\":[{"
    "\"id\":\"app\",\"os\":\"win64\",\"version\":\"2.0.0\","
    "\"file\":\"app-2.0.0-win64.pak\",\"size\":18483211,"
    "\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\","
    "\"requires\":{\"fontres\":\">=2.0\"}},"
    "{\"id\":\"fontres\",\"os\":\"any\",\"version\":\"2.0\","
    "\"file\":\"fontres-2.0.pak\",\"size\":1024,"
    "\"sha256\":\"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\"}"
    "],\"sig\":\"QVNDSUk=\"}";

/** @brief 断言 out 处于失败清零态（整单拒绝口径）。 */
static bool xtest_manifest_cleared(const XUpgradeManifest* m)
{
    return m && m->m_schema == 0 && m->m_componentCount == 0 &&
           m->m_version[0] == '\0' && m->m_sig[0] == '\0';
}

/** @brief 构造 N 组件的 manifest 文本（组件越限拒绝用例）。 */
static XByteArray* xtest_manifest_n_components(int n)
{
    XString* json = XString_create_utf8(
        "{\"schema\":1,\"channel\":\"stable\",\"version\":\"2.0.0\",\"components\":[");
    int i;
    for (i = 0; json && i < n; ++i) {
        XString* item = XString_create_fmt_utf8(
            "%s{\"id\":\"c%d\",\"os\":\"any\",\"version\":\"1.0\","
            "\"file\":\"c%d.pak\",\"size\":1,\"sha256\":"
            "\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}",
            i > 0 ? "," : "", i, i);
        if (!item || !XString_append(json, item)) {
            if (item) XClassDelete(item);
            XClassDelete(json);
            return NULL;
        }
        XClassDelete(item);
    }
    if (!json || !XString_append_utf8(json, "]}")) {
        if (json) XClassDelete(json);
        return NULL;
    }
    {
        XByteArray* bytes = XByteArray_create_with_data(XString_toUtf8(json),
                                                        XString_toUtf8_length(json));
        XClassDelete(json);
        return bytes;
    }
}

static bool t_manifest_parse(void)
{
    XUpgradeManifest m;
    const XUpgradeManifestComponent* c;
    bool ok = true;
    /* 合法 manifest：字段逐项核对。 */
    ok = ok && XUpgradeManifest_parse(&m, k_manifestValid, xtest_str_len(k_manifestValid));
    ok = ok && m.m_schema == 1;
    ok = ok && xtest_str_eq(m.m_channel, "stable");
    ok = ok && xtest_str_eq(m.m_version, "2.0.0");
    ok = ok && xtest_str_eq(m.m_build, "2026.10.06-01");
    ok = ok && xtest_str_eq(m.m_minVersion, "1.0.0");
    ok = ok && xtest_str_eq(m.m_rollbackTo, "1.0.0");
    ok = ok && m.m_policy == XUpgradePolicy_Optional;
    ok = ok && m.m_policyForcedBelow[0] == '\0';
    ok = ok && m.m_gray.m_percent == 10 && xtest_str_eq(m.m_gray.m_salt, "r1");
    ok = ok && m.m_gray.m_allowCount == 1 && xtest_str_eq(m.m_gray.m_allow[0], "SN001");
    ok = ok && m.m_gray.m_denyCount == 0;
    ok = ok && m.m_componentCount == 2;
    ok = ok && xtest_str_eq(m.m_sig, "QVNDSUk=");
    if (m.m_componentCount == 2) {
        c = &m.m_components[0];
        ok = ok && xtest_str_eq(c->m_id, "app") && xtest_str_eq(c->m_os, "win64");
        ok = ok && xtest_str_eq(c->m_version, "2.0.0") && c->m_size == 18483211ull;
        ok = ok && xtest_str_eq(c->m_sha256,
             "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");
        ok = ok && c->m_requires.m_count == 1;
        ok = ok && c->m_requires.m_count == 1 &&
             xtest_str_eq(c->m_requires.m_component[0], "fontres") &&
             xtest_str_eq(c->m_requires.m_constraint[0], ">=2.0");
    } else {
        ok = false;
    }
    /* findComponent：精确 os 优先，回退 any，未命中 NULL。 */
    c = XUpgradeManifest_findComponent(&m, "app", "win64");
    ok = ok && c && xtest_str_eq(c->m_os, "win64");
    c = XUpgradeManifest_findComponent(&m, "app", "linux64");
    ok = ok && !c; /* app 无 any 条目：linux64 未命中 */
    c = XUpgradeManifest_findComponent(&m, "fontres", "win64");
    ok = ok && c && xtest_str_eq(c->m_os, "any");
    c = XUpgradeManifest_findComponent(&m, "nope", "win64");
    ok = ok && !c;
    c = XUpgradeManifest_findComponent(&m, NULL, "win64");
    ok = ok && !c;
    /* 拒绝矩阵：失败且 out 保持清零态。 */
    {
        static const char* bad[] = {
            "{\"schema\":2,\"channel\":\"stable\",\"version\":\"1\",\"components\":[{\"id\":\"a\",\"os\":\"any\",\"version\":\"1\",\"file\":\"a.pak\",\"size\":1,\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}",
            "{\"channel\":\"stable\",\"version\":\"1\",\"components\":[{\"id\":\"a\",\"os\":\"any\",\"version\":\"1\",\"file\":\"a.pak\",\"size\":1,\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}",
            "{\"schema\":1,\"version\":\"1\",\"components\":[{\"id\":\"a\",\"os\":\"any\",\"version\":\"1\",\"file\":\"a.pak\",\"size\":1,\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}",
            "{\"schema\":1,\"channel\":\"stable\",\"components\":[{\"id\":\"a\",\"os\":\"any\",\"version\":\"1\",\"file\":\"a.pak\",\"size\":1,\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}",
            "{\"schema\":1,\"channel\":\"stable\",\"version\":\"1\",\"components\":[]}",
            "{\"schema\":1,\"channel\":\"stable\",\"version\":\"1\",\"components\":[{\"id\":\"a\",\"os\":\"any\",\"version\":\"1\",\"file\":\"a.pak\",\"size\":1}]}",
            "{\"schema\":1,\"channel\":\"stable\",\"version\":\"1\",\"components\":[{\"id\":\"a\",\"os\":\"any\",\"version\":\"1\",\"file\":\"a.pak\",\"size\":1,\"sha256\":\"0123456789ABCDEF0123456789abcdef0123456789abcdef0123456789abcdef\"}]}",
            "{\"schema\":1,\"channel\":\"stable\",\"version\":\"1\",\"components\":[{\"id\":\"a\",\"os\":\"any\",\"version\":\"2.0.0/..\",\"file\":\"a.pak\",\"size\":1,\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}",
            "{\"schema\":1,\"channel\":\"stable\",\"version\":\"..\",\"components\":[{\"id\":\"a\",\"os\":\"any\",\"version\":\"1\",\"file\":\"a.pak\",\"size\":1,\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}",
            "{\"schema\":1,\"channel\":\"stable\",\"version\":\"1\",\"components\":[{\"id\":\"a\",\"os\":\"any\",\"version\":\"1\",\"file\":\"../evil.pak\",\"size\":1,\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}",
            "{\"schema\":1,\"channel\":\"stable\",\"version\":\"1\",\"components\":[{\"id\":\"a\",\"os\":\"any\",\"version\":\"1\",\"file\":\"a.pak\",\"size\":-1,\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}",
            "{\"schema\":1,\"channel\":\"stable\",\"version\":\"1\",\"policy\":\"bogus\",\"components\":[{\"id\":\"a\",\"os\":\"any\",\"version\":\"1\",\"file\":\"a.pak\",\"size\":1,\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}",
            "{\"schema\":1,\"channel\":\"stable\",\"version\":\"1\",\"gray\":{\"percent\":101,\"salt\":\"x\"},\"components\":[{\"id\":\"a\",\"os\":\"any\",\"version\":\"1\",\"file\":\"a.pak\",\"size\":1,\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\"}]}",
            "{\"schema\":1,\"channel\":\"stable\",\"version\":\"1\",\"components\":[{\"id\":\"a\",\"os\":\"any\",\"version\":\"1\",\"file\":\"a.pak\",\"size\":1,\"sha256\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"requires\":{\"a\":\">=1\",\"b\":\">=1\",\"c\":\">=1\",\"d\":\">=1\",\"e\":\">=1\"}}]}"
        };
        size_t i;
        for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            bool rejected = !XUpgradeManifest_parse(&m, bad[i], xtest_str_len(bad[i]));
            if (!rejected || !xtest_manifest_cleared(&m)) {
                XPrintf("    [manifest 拒绝矩阵 #%d 未按清零态拒绝]\n", (int)i);
                ok = false;
                break;
            }
        }
        /* 9 组件越限与 8 组件临界。 */
        {
            XByteArray* nine = xtest_manifest_n_components(9);
            XByteArray* eight = xtest_manifest_n_components(8);
            if (!nine || XUpgradeManifest_parse(&m, (const char*)xtest_bdata(nine),
                                                xtest_bsize(nine))) {
                XPrintf("    [manifest 9 组件未拒绝]\n");
                ok = false;
            }
            if (nine) XClassDelete(nine);
            if (!eight || !XUpgradeManifest_parse(&m, (const char*)xtest_bdata(eight),
                                                  xtest_bsize(eight))) {
                XPrintf("    [manifest 8 组件临界未通过]\n");
                ok = false;
            }
            if (eight) XClassDelete(eight);
        }
        /* 空/非法输入。 */
        ok = ok && !XUpgradeManifest_parse(&m, k_manifestValid, 0);
        ok = ok && !XUpgradeManifest_parse(&m, "not json", 8);
        ok = ok && !XUpgradeManifest_parse(&m, NULL, 5);
        ok = ok && !XUpgradeManifest_parse(NULL, k_manifestValid, 10);
        ok = ok && xtest_manifest_cleared(&m);
    }
    return ok;
}

/* ========================================================================== */
/*                     单测：.pak 写→读→逐条目校验回环                          */
/* ========================================================================== */

/** @brief 条目二进制摘要与期望摘要比较（等价 xupgrade_digestEqual）。 */
static bool xtest_hex_entry_matches(const uint8_t* entrySha, const uint8_t* digest)
{
    return entrySha && digest && XMemcmp(entrySha, digest, XUPGRADE_SHA256_BIN_SIZE) == 0;
}

/** @brief 写出小端 u16（负例条目手工组装用）。 */
static void xtest_put16(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

/** @brief 写出小端 u64。 */
static void xtest_put64(uint8_t* p, uint64_t v)
{
    int i;
    for (i = 0; i < 8; ++i) p[i] = (uint8_t)((v >> (i * 8)) & 0xFFu);
}

static bool t_pak_roundtrip(void)
{
    static const uint8_t blobRaw[4096]; /* 全零，可压缩 */
    static const uint8_t textRaw[] = "hello xupgrade pak e2e";
    static const uint8_t markRaw[] = "2.0.0";
    XTestPakEntry entries[3];
    XByteArray* pak = NULL;
    bool ok = true;
    entries[0].m_name = "docs/readme.txt";
    entries[0].m_flags = 0;
    entries[0].m_data = textRaw;
    entries[0].m_len = sizeof(textRaw) - 1;
    entries[1].m_name = "data/blob.bin";
    entries[1].m_flags = XUPGRADE_PACKAGE_FLAG_ZLIB;
    entries[1].m_data = blobRaw;
    entries[1].m_len = sizeof(blobRaw);
    entries[2].m_name = XUPGRADE_FILE_VERSION_MARK;
    entries[2].m_flags = 0;
    entries[2].m_data = markRaw;
    entries[2].m_len = sizeof(markRaw) - 1;
    ok = ok && xtest_build_pak(entries, 3, &pak, NULL);
    if (!ok || !pak) return false;
    {
        const uint8_t* bytes = xtest_bdata(pak);
        size_t size = xtest_bsize(pak);
        XUpgradePackageHeader header;
        XUpgradePackageEntry entry;
        size_t consumed = 0;
        size_t pos = XUPGRADE_PACKAGE_FIXED_HEADER_SIZE;
        uint32_t i;
        /* 容器级一次性双哈希校验。 */
        ok = ok && XUpgradePackage_verify(bytes, size) == XUpgradeError_Ok;
        /* 头回环。 */
        ok = ok && XUpgradePackage_parseHeader(&header, bytes, size);
        ok = ok && header.m_format == XUPGRADE_PACKAGE_FORMAT;
        ok = ok && header.m_entryCount == 3;
        ok = ok && header.m_headerSize >= XUPGRADE_PACKAGE_FIXED_HEADER_SIZE;
        ok = ok && (uint64_t)size == header.m_headerSize + header.m_payloadSize;
        ok = ok && XUpgradePackage_payloadOffset(&header) == header.m_headerSize;
        ok = ok && XUpgradePackage_payloadOffset(NULL) == 0;
        /* 逐条目流式回环：字段+摘要。 */
        for (i = 0; ok && i < 3; ++i) {
            uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
            char hex[XUPGRADE_SHA256_HEX_MAX];
            ok = ok && XUpgradePackage_parseEntry(&entry, bytes + pos, size - pos, &consumed);
            ok = ok && xtest_str_eq(entry.m_name, entries[i].m_name);
            ok = ok && entry.m_flags == entries[i].m_flags;
            ok = ok && entry.m_rawSize == (uint64_t)entries[i].m_len;
            ok = ok && xtest_sha256(entries[i].m_data, entries[i].m_len, digest);
            xtest_bin2hex(digest, XUPGRADE_SHA256_BIN_SIZE, hex);
            ok = ok && xtest_hex_entry_matches(entry.m_sha256, digest);
            pos += consumed;
        }
        ok = ok && pos == header.m_headerSize; /* 条目表恰好终止于数据区起点 */
        /* 损坏拒绝：数据区单字节翻转 → 容器校验失败。 */
        {
            uint8_t* broken = (uint8_t*)XMalloc_System(size);
            if (broken) {
                XMemcpy(broken, bytes, size);
                broken[header.m_headerSize] = (uint8_t)(broken[header.m_headerSize] ^ 0xFFu);
                ok = ok && XUpgradePackage_verify(broken, size) == XUpgradeError_VerifyFailed;
                /* 头损坏：魔数。 */
                XMemcpy(broken, bytes, size);
                broken[0] = 'X'; broken[1] = 'U'; broken[2] = 'P'; broken[3] = 'X';
                ok = ok && !XUpgradePackage_parseHeader(&header, broken, size);
                /* 截断。 */
                XMemcpy(broken, bytes, size);
                ok = ok && !XUpgradePackage_parseHeader(&header, broken, 32);
                XFree_System(broken);
            } else {
                ok = false;
            }
        }
        /* format/entryCount/headerSize 拒绝（写侧出口即拒绝）。 */
        {
            XUpgradePackageHeader badHeader;
            uint8_t buf[XUPGRADE_PACKAGE_FIXED_HEADER_SIZE];
            size_t used = 0;
            XMemset(&badHeader, 0, sizeof(badHeader));
            badHeader = header;
            badHeader.m_format = 2;
            ok = ok && !XUpgradePackage_writeHeader(buf, sizeof(buf), &used, &badHeader);
            badHeader = header;
            badHeader.m_entryCount = 0;
            ok = ok && !XUpgradePackage_writeHeader(buf, sizeof(buf), &used, &badHeader);
            badHeader = header;
            badHeader.m_headerSize = XUPGRADE_PACKAGE_FIXED_HEADER_SIZE - 1;
            ok = ok && !XUpgradePackage_writeHeader(buf, sizeof(buf), &used, &badHeader);
        }
        /* 条目表负例：非法名/保留 flags/未压缩尺寸矛盾（写侧拒绝）+
         * parseEntry 对改名字节流的拒绝。 */
        {
            XUpgradePackageEntry bad;
            uint8_t buf[256];
            size_t used = 0;
            XMemset(&bad, 0, sizeof(bad));
            xtest_str_copy(bad.m_name, sizeof(bad.m_name), "a//b");
            bad.m_rawSize = 4;
            bad.m_compSize = 4;
            ok = ok && XUpgradePackage_entryWireSize(&bad) == 0;
            ok = ok && !XUpgradePackage_writeEntry(buf, sizeof(buf), &used, &bad);
            xtest_str_copy(bad.m_name, sizeof(bad.m_name), "ok.bin");
            bad.m_flags = 0x02; /* 保留位 */
            ok = ok && !XUpgradePackage_writeEntry(buf, sizeof(buf), &used, &bad);
            bad.m_flags = 0;
            bad.m_rawSize = 4;
            bad.m_compSize = 5;
            ok = ok && !XUpgradePackage_writeEntry(buf, sizeof(buf), &used, &bad);
            /* 合法条目写出后原地改名 "../evil"，parseEntry 必须拒绝。 */
            {
                XUpgradePackageEntry good;
                XMemset(&good, 0, sizeof(good));
                xtest_str_copy(good.m_name, sizeof(good.m_name), "docs/abcdefxyz");
                good.m_rawSize = 4;
                good.m_compSize = 4;
                used = 0;
                ok = ok && XUpgradePackage_writeEntry(buf, sizeof(buf), &used, &good);
                XMemcpy(buf + 2, "docs/../xxxxxx", 14); /* 同长度路径逃逸名 */
                ok = ok && !XUpgradePackage_parseEntry(&entry, buf, used, &consumed);
            }
        }
    }
    XClassDelete((XClass*)pak);
    /* checkEntryName 白名单矩阵。 */
    {
        static const struct { const char* m_name; bool m_ok; } nameCases[] = {
            { "bin/app.exe", true },
            { "ok.txt", true },
            { "a-b_c.d", true },
            { ".verify", true },          /* 隐藏名合法（仅 "."/".." 段被拒） */
            { "manifest.version", true },
            { "", false },
            { NULL, false },
            { "/abs", false },
            { "\\\\srv", false },
            { "C:/x", false },
            { "a\\b", false },
            { "a//b", false },
            { "a/", false },
            { "./a", false },
            { "..", false },
            { ".", false },
            { "a/../b", false },
            { "a/./b", false },
            { "a b", false }
        };
        size_t i;
        for (i = 0; i < sizeof(nameCases) / sizeof(nameCases[0]); ++i) {
            if (XUpgradePackage_checkEntryName(nameCases[i].m_name) != nameCases[i].m_ok) {
                XPrintf("    [checkEntryName #%d (%s) 期望 %d]\n", (int)i,
                        nameCases[i].m_name ? nameCases[i].m_name : "(null)",
                        nameCases[i].m_ok ? 1 : 0);
                ok = false;
            }
        }
    }
    return ok;
}

/* ========================================================================== */
/*                          单测：state.json 原子写回环                          */
/* ========================================================================== */

static bool t_state_roundtrip(void)
{
    XUpgradeStateFile st;
    XUpgradeStateFile out;
    char path[1400];
    bool ok = true;
    xtest_scratch_wipe(XUP_TEST_SCRATCH_UNIT);
    /* clear 缺省值。 */
    XUpgradeStateFile_clear(&st);
    XTEST_CHECK(st.m_schema == XUPGRADE_STATE_SCHEMA);
    XTEST_CHECK(st.m_state == XUpgradeState_Idle);
    XTEST_CHECK(st.m_componentCount == 0 && st.m_skipCount == 0);
    XTEST_CHECK(st.m_crc32 == 0 && !st.m_download.m_active);
    /* 填充组件/跳过/下载游标并保存。 */
    {
        XUpgradeStateComponent* c = &st.m_components[0];
        XUpgradeStateSkip* s = &st.m_skips[0];
        xtest_str_copy(c->m_id, XUPGRADE_COMPONENT_ID_MAX, "app");
        xtest_str_copy(c->m_activeVersion, XUPGRADE_VERSION_MAX, "1.0.0");
        xtest_str_copy(c->m_stagedVersion, XUPGRADE_VERSION_MAX, "2.0.0");
        xtest_str_copy(c->m_maxVersion, XUPGRADE_VERSION_MAX, "1.9.0");
        c->m_rollbackCount = 1;
        c->m_staged = true;
        st.m_componentCount = 1;
        xtest_str_copy(s->m_componentId, XUPGRADE_COMPONENT_ID_MAX, "app");
        xtest_str_copy(s->m_version, XUPGRADE_VERSION_MAX, "1.5.0");
        st.m_skipCount = 1;
        st.m_download.m_active = true;
        xtest_str_copy(st.m_download.m_componentId, XUPGRADE_COMPONENT_ID_MAX, "app");
        xtest_str_copy(st.m_download.m_fileName, XUPGRADE_FILENAME_MAX, "app-2.0.0-win64.pak.part");
        st.m_download.m_offset = 12345;
        XMemset(st.m_download.m_chunkSha256, 0xAB, XUPGRADE_SHA256_BIN_SIZE);
        st.m_state = XUpgradeState_Downloading;
    }
    XTEST_CHECK(xtest_save_state(XUP_TEST_SCRATCH_UNIT, &st));
    /* 回环：字段逐项一致。 */
    XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_UNIT, &out));
    XTEST_CHECK(out.m_schema == XUPGRADE_STATE_SCHEMA);
    XTEST_CHECK(out.m_state == XUpgradeState_Downloading);
    XTEST_CHECK(out.m_componentCount == 1 && out.m_skipCount == 1);
    XTEST_CHECK(xtest_str_eq(out.m_components[0].m_id, "app"));
    XTEST_CHECK(xtest_str_eq(out.m_components[0].m_activeVersion, "1.0.0"));
    XTEST_CHECK(xtest_str_eq(out.m_components[0].m_stagedVersion, "2.0.0"));
    XTEST_CHECK(xtest_str_eq(out.m_components[0].m_maxVersion, "1.9.0"));
    XTEST_CHECK(out.m_components[0].m_rollbackCount == 1 && out.m_components[0].m_staged);
    XTEST_CHECK(xtest_str_eq(out.m_skips[0].m_componentId, "app"));
    XTEST_CHECK(xtest_str_eq(out.m_skips[0].m_version, "1.5.0"));
    XTEST_CHECK(out.m_download.m_active && out.m_download.m_offset == 12345);
    XTEST_CHECK(XMemcmp(out.m_download.m_chunkSha256, st.m_download.m_chunkSha256,
                        XUPGRADE_SHA256_BIN_SIZE) == 0);
    /* 字段变更后再存：CRC 自动重算，读到新值。 */
    xtest_str_copy(out.m_components[0].m_activeVersion, XUPGRADE_VERSION_MAX, "1.0.1");
    XTEST_CHECK(xtest_save_state(XUP_TEST_SCRATCH_UNIT, &out));
    XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_UNIT, &st));
    XTEST_CHECK(xtest_str_eq(st.m_components[0].m_activeVersion, "1.0.1"));
    /* 文件缺失：false + 清零态（全新设备口径）。 */
    xtest_scratch_wipe(XUP_TEST_SCRATCH_UNIT);
    XUpgradeStateFile_clear(&out);
    XTEST_CHECK(!xtest_load_state(XUP_TEST_SCRATCH_UNIT, &out));
    XTEST_CHECK(out.m_schema == XUPGRADE_STATE_SCHEMA && out.m_componentCount == 0 &&
                !out.m_download.m_active);
    /* schema 不符：false。 */
    XTEST_CHECK(xtest_save_state(XUP_TEST_SCRATCH_UNIT, &st));
    XUpgradeStateFile_clear(&st);
    st.m_schema = 2;
    XTEST_CHECK(xtest_save_state(XUP_TEST_SCRATCH_UNIT, &st));
    XUpgradeStateFile_clear(&out);
    XTEST_CHECK(!xtest_load_state(XUP_TEST_SCRATCH_UNIT, &out));
    /* 篡改字节：CRC 不符（或 JSON 损坏）→ false + 清零态。 */
    {
        XUpgradeStateFile good;
        XByteArray* bytes;
        XUpgradeStateFile_clear(&good);
        xtest_str_copy(good.m_components[0].m_id, XUPGRADE_COMPONENT_ID_MAX, "app");
        xtest_str_copy(good.m_components[0].m_activeVersion, XUPGRADE_VERSION_MAX, "1.0.0");
        good.m_componentCount = 1;
        XTEST_CHECK(xtest_save_state(XUP_TEST_SCRATCH_UNIT, &good));
        XTEST_FMT(path, sizeof(path),
                  "%s/" XUPGRADE_DIR_UPDATE "/" XUPGRADE_FILE_STATE, XUP_TEST_SCRATCH_UNIT);
        bytes = xtest_read_file(path);
        if (bytes && xtest_bsize(bytes) > 16) {
            uint8_t* mutableBytes = (uint8_t*)XMalloc_System(xtest_bsize(bytes));
            if (mutableBytes) {
                size_t n = xtest_bsize(bytes);
                XMemcpy(mutableBytes, xtest_bdata(bytes), n);
                mutableBytes[n / 2] = (uint8_t)(mutableBytes[n / 2] ^ 0x7Fu);
                XTEST_CHECK(xtest_write_file(path, (const char*)mutableBytes, n));
                XFree_System(mutableBytes);
                XUpgradeStateFile_clear(&out);
                XTEST_CHECK(!xtest_load_state(XUP_TEST_SCRATCH_UNIT, &out));
                XTEST_CHECK(out.m_componentCount == 0);
            } else {
                ok = false;
            }
        } else {
            ok = false;
        }
        if (bytes) XClassDelete(bytes);
    }
    xtest_scratch_wipe(XUP_TEST_SCRATCH_UNIT);
    return ok;
}

/* ========================================================================== */
/*                单测：DirStore stage→activate→rollback→listVersions           */
/* ========================================================================== */

/** @brief listVersions 收集回调上下文。 */
typedef struct XTestVersionCollector {
    char m_items[8][XUPGRADE_VERSION_MAX];
    uint32_t m_count;
    int m_abortAfter;   /**< >=0 时第 N+1 条返回非 0（中止语义）。 */
    int m_abortValue;   /**< 中止返回值。 */
} XTestVersionCollector;

static int xtest_collect_version(void* userData, const char* version)
{
    XTestVersionCollector* c = (XTestVersionCollector*)userData;
    if (!c || !version) return 0;
    if (c->m_abortAfter >= 0 && (int)c->m_count >= c->m_abortAfter) return c->m_abortValue;
    if (c->m_count < 8) {
        xtest_str_copy(c->m_items[c->m_count], XUPGRADE_VERSION_MAX, version);
        ++c->m_count;
    }
    return 0;
}

static bool t_storedir_flow(void)
{
    static uint8_t chunkA[1000];
    static uint8_t chunkB[700];
    XUpgradeStore* base = NULL;
    XUpgradeStore* store = NULL;
    XTestVersionCollector collector;
    const char* err = NULL;
    char current[XUPGRADE_VERSION_MAX];
    size_t i;
    bool ok = true;
    for (i = 0; i < sizeof(chunkA); ++i) chunkA[i] = (uint8_t)(i & 0xFF);
    for (i = 0; i < sizeof(chunkB); ++i) chunkB[i] = (uint8_t)(0xC0 - (i & 0x3F));
    xtest_scratch_wipe(XUP_TEST_SCRATCH_STORE);
    /* 抽象基类：全部分派入口 NotSupported（M1 不实现接口的返回值口径）。
       2026-10-11：create 包装宏要求至少一参（库惯例 __VA_ARGS__ 无 ##，
       空参调用留下悬空逗号 GCC 报错），零参直呼 create_ex。 */
    base = XUpgradeStore_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XTEST_CHECK(base != NULL);
    if (base) {
        XTEST_CHECK(XUpgradeStore_stageBegin_base(base, "app", "1.0.0", 0, &err)
                    == XUpgradeError_NotSupported);
        XTEST_CHECK(XUpgradeStore_stageWrite_base(base, "x", 1) == XUpgradeError_NotSupported);
        XTEST_CHECK(XUpgradeStore_stageEnd_base(base, &err) == XUpgradeError_NotSupported);
        XTEST_CHECK(XUpgradeStore_activate_base(base, "app", "1.0.0", &err)
                    == XUpgradeError_NotSupported);
        XTEST_CHECK(XUpgradeStore_rollback_base(base, "app", "1.0.0", &err)
                    == XUpgradeError_NotSupported);
        XTEST_CHECK(XUpgradeStore_listVersions_base(base, "app", xtest_collect_version,
                                                    &collector) == XUpgradeError_NotSupported);
        XTEST_CHECK(!XUpgradeStore_currentVersion_base(base, "app", current, sizeof(current)));
        XClassDelete((XClass*)base);
    }
    /* 栈上抽象对象：init/分派/XClassDeinit 成对。 */
    {
        XUpgradeStore stackStore;
        XUpgradeStore_init(&stackStore);
        XTEST_CHECK(XUpgradeStore_activate_base(&stackStore, "app", "1.0.0", &err)
                    == XUpgradeError_NotSupported);
        XClassDeinit(&stackStore);
    }
    /* 目录后端完整流。 */
    store = XUpgradeStoreDir_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XUP_TEST_SCRATCH_STORE);
    XTEST_CHECK(store != NULL);
    if (!store) return false;
    /* staging：3 块写入 + 空块。 */
    XTEST_CHECK(XUpgradeStore_stageBegin_base(store, "app", "2.0.0", 0, &err)
                == XUpgradeError_Ok);
    XTEST_CHECK(XUpgradeStore_stageWrite_base(store, chunkA, sizeof(chunkA)) == XUpgradeError_Ok);
    XTEST_CHECK(XUpgradeStore_stageWrite_base(store, chunkB, sizeof(chunkB)) == XUpgradeError_Ok);
    XTEST_CHECK(XUpgradeStore_stageWrite_base(store, chunkA, 0) == XUpgradeError_Ok);
    XTEST_CHECK(XUpgradeStore_stageEnd_base(store, &err) == XUpgradeError_Ok);
    /* 定稿内容回读核对。 */
    {
        char path[1400];
        XByteArray* payload;
        XTEST_FMT(path, sizeof(path),
                  "%s/" XUPGRADE_DIR_VERSIONS "/app/2.0.0/payload.staged",
                  XUP_TEST_SCRATCH_STORE);
        payload = xtest_read_file(path);
        XTEST_CHECK(payload != NULL);
        if (payload) {
            XTEST_CHECK(xtest_bsize(payload) == sizeof(chunkA) + sizeof(chunkB));
            XTEST_CHECK(XMemcmp(xtest_bdata(payload), chunkA, sizeof(chunkA)) == 0);
            XTEST_CHECK(XMemcmp(xtest_bdata(payload) + sizeof(chunkA),
                                chunkB, sizeof(chunkB)) == 0);
            XClassDelete(payload);
        }
    }
    /* 未激活：currentVersion=false；activate 后可查。 */
    XTEST_CHECK(!XUpgradeStore_currentVersion_base(store, "app", current, sizeof(current)));
    XTEST_CHECK(XUpgradeStore_activate_base(store, "app", "2.0.0", &err) == XUpgradeError_Ok);
    XTEST_CHECK(XUpgradeStore_currentVersion_base(store, "app", current, sizeof(current)));
    XTEST_CHECK(xtest_str_eq(current, "2.0.0"));
    /* 第二版本 + 回滚。 */
    XTEST_CHECK(XUpgradeStore_stageBegin_base(store, "app", "1.0.0", 0, &err)
                == XUpgradeError_Ok);
    XTEST_CHECK(XUpgradeStore_stageWrite_base(store, "old", 3) == XUpgradeError_Ok);
    XTEST_CHECK(XUpgradeStore_stageEnd_base(store, &err) == XUpgradeError_Ok);
    XTEST_CHECK(XUpgradeStore_activate_base(store, "app", "1.0.0", &err) == XUpgradeError_Ok);
    XTEST_CHECK(XUpgradeStore_currentVersion_base(store, "app", current, sizeof(current)));
    XTEST_CHECK(xtest_str_eq(current, "1.0.0"));
    XTEST_CHECK(XUpgradeStore_rollback_base(store, "app", "2.0.0", &err) == XUpgradeError_Ok);
    XTEST_CHECK(XUpgradeStore_currentVersion_base(store, "app", current, sizeof(current)));
    XTEST_CHECK(xtest_str_eq(current, "2.0.0"));
    /* 枚举：两版本齐；中止语义透传。 */
    XMemset(&collector, 0, sizeof(collector));
    collector.m_abortAfter = -1; /* 负值=不中止（0 会与合法首条冲突） */
    XTEST_CHECK(XUpgradeStore_listVersions_base(store, "app", xtest_collect_version,
                                                &collector) == XUpgradeError_Ok);
    XTEST_CHECK(collector.m_count == 2);
    {
        bool has2 = false;
        bool has1 = false;
        uint32_t k;
        for (k = 0; k < collector.m_count; ++k) {
            if (xtest_str_eq(collector.m_items[k], "2.0.0")) has2 = true;
            if (xtest_str_eq(collector.m_items[k], "1.0.0")) has1 = true;
        }
        XTEST_CHECK(has1 && has2);
    }
    XMemset(&collector, 0, sizeof(collector));
    collector.m_abortAfter = 0;
    collector.m_abortValue = 7;
    XTEST_CHECK(XUpgradeStore_listVersions_base(store, "app", xtest_collect_version,
                                                &collector) == 7);
    /* 负例：流中重复 begin=Busy；无流 write/end=NotReady；缺目录=Io；非法名=InvalidArgument。 */
    XTEST_CHECK(XUpgradeStore_stageBegin_base(store, "app", "3.0.0", 0, &err)
                == XUpgradeError_Ok);
    XTEST_CHECK(XUpgradeStore_stageBegin_base(store, "app", "3.1.0", 0, &err)
                == XUpgradeError_Busy);
    XTEST_CHECK(XUpgradeStore_stageEnd_base(store, &err) == XUpgradeError_Ok);
    XTEST_CHECK(XUpgradeStore_stageWrite_base(store, "x", 1) == XUpgradeError_NotReady);
    XTEST_CHECK(XUpgradeStore_stageEnd_base(store, &err) == XUpgradeError_NotReady);
    XTEST_CHECK(XUpgradeStore_activate_base(store, "app", "9.9.9", &err) == XUpgradeError_Io);
    XTEST_CHECK(XUpgradeStore_activate_base(store, "app", "../x", &err)
                == XUpgradeError_InvalidArgument);
    XTEST_CHECK(XUpgradeStore_activate_base(store, "a/b", "1.0.0", &err)
                == XUpgradeError_InvalidArgument);
    XTEST_CHECK(!XUpgradeStore_currentVersion_base(store, "app", current, 0));
    XClassDelete((XClass*)store);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_STORE);
    return ok;
}

/* ========================================================================== */
/*                          单测：身份提供者契约                                */
/* ========================================================================== */

static bool t_identity(void)
{
    char buf[XUPGRADE_VERSION_MAX + 4];
    bool ok = true;
    /* 已注册：回调结果为准。 */
    XUpgrade_setVersionProvider(xtest_version_provider, NULL);
    ok = ok && XUpgradeIdentity_version(buf, sizeof(buf));
    ok = ok && xtest_str_eq(buf, "1.0.0");
    /* 截断安全：容量不足仍 NUL 结尾。 */
    ok = ok && XUpgradeIdentity_version(buf, 3);
    ok = ok && xtest_str_eq(buf, "1.");
    /* 序列号：注册/清除。 */
    ok = ok && !XUpgradeIdentity_serial(buf, sizeof(buf)); /* 未注册不回退推导 */
    XUpgrade_setSerialProvider(xtest_serial_provider, NULL);
    ok = ok && XUpgradeIdentity_serial(buf, sizeof(buf));
    ok = ok && xtest_str_eq(buf, "SN-XUP-E2E-001");
    XUpgrade_setSerialProvider(NULL, NULL);
    ok = ok && !XUpgradeIdentity_serial(buf, sizeof(buf));
    /* 参数非法。 */
    ok = ok && !XUpgradeIdentity_version(NULL, sizeof(buf));
    ok = ok && !XUpgradeIdentity_version(buf, 0);
    /* 清除版本提供者：回落编译期缺省（非空即成功；不锁定具体值）。 */
    XUpgrade_setVersionProvider(NULL, NULL);
    ok = ok && XUpgradeIdentity_version(buf, sizeof(buf));
    ok = ok && buf[0] != '\0';
    /* 恢复测试期固定提供者（后续 e2e 依赖 1.0.0 基线）。 */
    XUpgrade_setVersionProvider(xtest_version_provider, NULL);
    return ok;
}

/* ========================================================================== */
/*                端到端：init→check→upgrade→apply(74)→stub→selfCheck           */
/* ========================================================================== */

static bool t_e2e_http_flow(void)
{
    XUpgradeConfig config;
    XUpgradeStateFile state;
    char pointer[XUPGRADE_VERSION_MAX + 4];
    char path[1400];
    bool ok = true;
    xtest_scratch_wipe(XUP_TEST_SCRATCH_E2E);
    xtest_events_reset();
    /* 未初始化时的契约口径。 */
    ok = ok && !XUpgrade_checkNow();
    ok = ok && !XUpgrade_applyNow();
    XUpgrade_skipVersion("app", "2.0.0"); /* 未初始化：空操作 */
    ok = ok && !XUpgrade_init(NULL);
    {
        XUpgradeConfig bad;
        xtest_fill_config(&bad, XUP_TEST_SCRATCH_E2E);
        bad.m_targetOs = NULL;
        ok = ok && !XUpgrade_init(&bad);
    }
    XUpgrade_setListener(xtest_listener, NULL);
    xtest_fill_config(&config, XUP_TEST_SCRATCH_E2E);
    ok = ok && XUpgrade_init(&config);
    if (!ok) {
        XPrintf("    [XUpgrade_init 失败]\n");
        XUpgrade_deinit();
        return false;
    }
    ok = ok && XUpgrade_init(&config); /* 幂等 */
    /* 检查：命中 2.0.0 → UpdateAvailable，暂停在 Checking。 */
    ok = ok && XUpgrade_checkNow();
    ok = ok && xtest_wait_until(xup_wait_update_available, NULL);
    if (!g_xup_events.m_updateAvailable) {
        XPrintf("    [未收到 UpdateAvailable：lastDetail=%s]\n", g_xup_events.m_lastDetail);
        XUpgrade_deinit();
        return false;
    }
    /* 升级：Downloading→Verifying→Staging→Ready。 */
    XTEST_CHECK(XUpgrade_startUpgrade());
    XTEST_CHECK(xtest_wait_until(xup_wait_upgrade_done, NULL));
    XTEST_CHECK(g_xup_events.m_readyNeedRestart > 0);
    XTEST_CHECK(g_xup_events.m_downloadFailed == 0);
    XTEST_CHECK(g_xup_events.m_dlProgress > 0);
    XTEST_CHECK(g_xup_events.m_dlMaxPercent <= 100);
    XTEST_CHECK(g_xup_events.m_verifyDone > 0);
    XTEST_CHECK(g_xup_events.m_stageDone > 0);
    if (!g_xup_events.m_readyNeedRestart) {
        XPrintf("    [未到达 Ready：lastDetail=%s dlFailed=%d]\n",
                g_xup_events.m_lastDetail, g_xup_events.m_downloadFailed);
        XUpgrade_deinit();
        return false;
    }
    /* 落盘口径：state=Ready + staged 标记；版本目录三件套；partial 已清。 */
    XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_E2E, &state));
    XTEST_CHECK(state.m_state == XUpgradeState_Ready);
    XTEST_CHECK(state.m_componentCount == 1);
    XTEST_CHECK(xtest_str_eq(state.m_components[0].m_id, "app"));
    XTEST_CHECK(state.m_components[0].m_staged);
    XTEST_CHECK(xtest_str_eq(state.m_components[0].m_stagedVersion, "2.0.0"));
    XTEST_CHECK(xtest_str_eq(state.m_components[0].m_activeVersion, "1.0.0"));
    XTEST_CHECK(!state.m_download.m_active);
    XTEST_FMT(path, sizeof(path), "%s/" XUPGRADE_DIR_VERSIONS "/app/2.0.0/"
              XUPGRADE_FILE_VERSION_MARK, XUP_TEST_SCRATCH_E2E);
    XTEST_CHECK(xtest_path_exists(path));
    XTEST_FMT(path, sizeof(path), "%s/" XUPGRADE_DIR_VERSIONS "/app/2.0.0/"
              XUPGRADE_STORE_DIR_VERIFY_FILE, XUP_TEST_SCRATCH_E2E);
    XTEST_CHECK(xtest_path_exists(path));
    /* staging 逐条目解包落盘（设计 §6）：夹具单条目 manifest.version，落位
     * 版本目录恰为该条目 + .verify 清单两个文件（不再有整包 payload.staged）。 */
    XTEST_FMT(path, sizeof(path), "%s/" XUPGRADE_DIR_VERSIONS "/app/2.0.0",
              XUP_TEST_SCRATCH_E2E);
    XTEST_CHECK(xtest_file_count(path) == 2);
    XTEST_FMT(path, sizeof(path), "%s/" XUPGRADE_DIR_UPDATE "/" XUPGRADE_DIR_PARTIAL,
              XUP_TEST_SCRATCH_E2E);
    XTEST_CHECK(xtest_file_count(path) == 0);
    /* 服务端命中口径：manifest ≥1 次、pak ≥1 次。 */
    XTEST_CHECK(g_xup_serverCtx.m_manifestHits >= 1 && g_xup_serverCtx.m_pakHits >= 1);
    /* apply：新语义——受理即置 Applying 落盘并在引擎内切指针（BootConfirming
     * 已落盘）；随后的桩直调为幂等无操作（A/B 构建下才由桩完成切换）。 */
    XTEST_CHECK(XUpgrade_applyNow());
    XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_E2E, &state));
    XTEST_CHECK(state.m_state == XUpgradeState_BootConfirming);
    XTEST_CHECK(xtest_read_pointer(XUP_TEST_SCRATCH_E2E, "app", pointer, sizeof(pointer)));
    XTEST_CHECK(xtest_str_eq(pointer, "2.0.0"));
    XUpgrade_deinit();
    {
        char argv0[16];
        char* stubArgv[4];
        int rc;
        xtest_str_copy(argv0, sizeof(argv0), "xupgrade_e2e_test");
        stubArgv[0] = argv0;
        stubArgv[1] = (char*)"--xupgrade-apply";
        stubArgv[2] = (char*)"--root";
        stubArgv[3] = (char*)XUP_TEST_SCRATCH_E2E;
        rc = XUpgradeApplyStub_main(4, stubArgv);
        ok = ok && rc == 0;
        if (rc != 0) XPrintf("    [stub 退出码 %d]\n", rc);
    }
    XTEST_CHECK(xtest_read_pointer(XUP_TEST_SCRATCH_E2E, "app", pointer, sizeof(pointer)));
    XTEST_CHECK(xtest_str_eq(pointer, "2.0.0"));
    XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_E2E, &state));
    XTEST_CHECK(state.m_state == XUpgradeState_BootConfirming);
    XTEST_CHECK(!state.m_components[0].m_staged);
    /* 确认启动：selfCheck 通过 → Active；maxVersion 前移；confirmBoot 幂等 true。 */
    {
        XUpgradeEngine* confirm = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XTEST_CHECK(confirm != NULL);
        if (confirm) {
            XUpgradeConfig cfg;
            xtest_fill_config(&cfg, XUP_TEST_SCRATCH_E2E);
            /* 裸建引擎不走模块单例接线，事件统计须显式注册监听者。 */
            XUpgradeEngine_setListener(confirm, xtest_listener, NULL);
            XTEST_CHECK(XUpgradeEngine_start(confirm, &cfg));
            XTEST_CHECK(XUpgradeEngine_selfCheck(confirm));
            XTEST_CHECK(XUpgradeEngine_state(confirm) == XUpgradeState_Active);
            XTEST_CHECK(XUpgradeEngine_confirmBoot(confirm));
            XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_E2E, &state));
            XTEST_CHECK(state.m_state == XUpgradeState_Active);
            XTEST_CHECK(xtest_str_eq(state.m_components[0].m_activeVersion, "2.0.0"));
            XTEST_CHECK(xtest_str_eq(state.m_components[0].m_maxVersion, "2.0.0"));
            XTEST_CHECK(state.m_components[0].m_rollbackCount == 0);
            /* 升级后再查：同版本无更新。 */
            xtest_events_reset();
            XTEST_CHECK(XUpgradeEngine_checkNow(confirm));
            {
                int baseline = 0;
                XTEST_CHECK(xtest_wait_until(xup_wait_no_update, &baseline));
                XTEST_CHECK(g_xup_events.m_noUpdate == 1);
            }
            XUpgradeEngine_stop(confirm);
            XClassDelete((XClass*)confirm);
        } else {
            ok = false;
        }
    }
    xtest_scratch_wipe(XUP_TEST_SCRATCH_E2E);
    return ok;
}

/* ========================================================================== */
/*                     端到端：跳过此版本（optional 策略）                       */
/* ========================================================================== */

static bool t_skip_flow(void)
{
    XUpgradeConfig config;
    XUpgradeStateFile state;
    int baseline = 0;
    bool ok = true;
    xtest_scratch_wipe(XUP_TEST_SCRATCH_SKIP);
    xtest_events_reset();
    xtest_fill_config(&config, XUP_TEST_SCRATCH_SKIP);
    /* 事件统计依赖监听者：必须在 XUpgrade_init 之前注册（模块契约：
     * XUpgrade_setListener 可先于 init 暂存，init 时落到引擎）。 */
    XUpgrade_setListener(xtest_listener, NULL);
    XTEST_CHECK(XUpgrade_init(&config));
    XTEST_CHECK(XUpgrade_checkNow());
    XTEST_CHECK(xtest_wait_until(xup_wait_update_available, NULL));
    XUpgrade_skipVersion("app", "2.0.0");
    /* 跳过后再查：目标被过滤 → 暂无更新。 */
    XTEST_CHECK(XUpgrade_checkNow());
    XTEST_CHECK(xtest_wait_until(xup_wait_no_update, &baseline));
    XTEST_CHECK(g_xup_events.m_noUpdate == 1);
    XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_SKIP, &state));
    XTEST_CHECK(state.m_skipCount == 1);
    XTEST_CHECK(xtest_str_eq(state.m_skips[0].m_componentId, "app"));
    XTEST_CHECK(xtest_str_eq(state.m_skips[0].m_version, "2.0.0"));
    XUpgrade_deinit();
    xtest_scratch_wipe(XUP_TEST_SCRATCH_SKIP);
    return ok;
}

/* ========================================================================== */
/*            断电矩阵 1：下载中断 → partial 保留 → 整包重下收敛                */
/* ========================================================================== */

static bool t_matrix1_download_interrupt(void)
{
    XUpgradeEngine* engine = NULL;
    XUpgradeConfig config;
    XUpgradeStateFile state;
    char path[1400];
    char partial[64];
    bool ok = true;
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M1);
    /* 夹具：Downloading 态 + 续传游标（offset=5）+ 实际 4 字节的 partial（尺寸不符）。 */
    XUpgradeStateFile_clear(&state);
    state.m_state = XUpgradeState_Downloading;
    xtest_str_copy(state.m_components[0].m_id, XUPGRADE_COMPONENT_ID_MAX, "app");
    xtest_str_copy(state.m_components[0].m_activeVersion, XUPGRADE_VERSION_MAX, "1.0.0");
    xtest_str_copy(state.m_components[0].m_maxVersion, XUPGRADE_VERSION_MAX, "1.0.0");
    state.m_componentCount = 1;
    state.m_download.m_active = true;
    xtest_str_copy(state.m_download.m_componentId, XUPGRADE_COMPONENT_ID_MAX, "app");
    XTEST_FMT(partial, sizeof(partial), "%s.part", g_xup_pakName);
    xtest_str_copy(state.m_download.m_fileName, XUPGRADE_FILENAME_MAX, partial);
    state.m_download.m_offset = 5;
    ok = ok && xtest_save_state(XUP_TEST_SCRATCH_M1, &state);
    ok = ok && xtest_write_partial(XUP_TEST_SCRATCH_M1, partial, "abcd", 4);
    /* 启动收敛：矩阵 1 → Idle，partial 保留。 */
    xtest_events_reset();
    engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XTEST_CHECK(engine != NULL);
    if (!engine) return false;
    XUpgradeEngine_setListener(engine, xtest_listener, NULL);
    xtest_fill_config(&config, XUP_TEST_SCRATCH_M1);
    XTEST_CHECK(XUpgradeEngine_start(engine, &config));
    XTEST_CHECK(XUpgradeEngine_state(engine) == XUpgradeState_Idle);
    XTEST_FMT(path, sizeof(path), "%s/" XUPGRADE_DIR_UPDATE "/" XUPGRADE_DIR_PARTIAL "/%s",
              XUP_TEST_SCRATCH_M1, partial);
    XTEST_CHECK(xtest_path_exists(path));
    XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_M1, &state));
    XTEST_CHECK(state.m_state == XUpgradeState_Idle);
    XTEST_CHECK(state.m_download.m_active); /* 游标保留，续传校验失败后整包重下 */
    /* 继续：检查+升级，尺寸复核失败自动整包重下 → Ready。 */
    XTEST_CHECK(XUpgradeEngine_checkNow(engine));
    XTEST_CHECK(xtest_wait_until(xup_wait_update_available, NULL));
    XTEST_CHECK(XUpgradeEngine_startUpgrade(engine));
    /* 等终态"事件"而非内存状态：Ready 事件在 state.json 落盘、partial 清理
     * 全部完成后才发，事件后断言落盘口径才是确定性的（内存态先于落盘翻转）。 */
    XTEST_CHECK(xtest_wait_until(xup_wait_upgrade_done, NULL));
    XTEST_CHECK(g_xup_events.m_readyNeedRestart > 0);
    XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_M1, &state));
    XTEST_CHECK(state.m_state == XUpgradeState_Ready);
    XTEST_CHECK(state.m_components[0].m_staged);
    XTEST_CHECK(!state.m_download.m_active);
    XTEST_FMT(path, sizeof(path), "%s/" XUPGRADE_DIR_UPDATE "/" XUPGRADE_DIR_PARTIAL,
              XUP_TEST_SCRATCH_M1);
    XTEST_CHECK(xtest_file_count(path) == 0);
    XUpgradeEngine_stop(engine);
    XClassDelete((XClass*)engine);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M1);
    return ok;
}

/* ========================================================================== */
/*                  断电矩阵 2/3：校验中断、解包中断的启动收敛                   */
/* ========================================================================== */

static bool t_matrix23_verify_stage_interrupt(void)
{
    XUpgradeStateFile state;
    char path[1400];
    char halfJunk[16];
    bool ok = true;
    /* 矩阵 2：Verifying 中断 → partial 作废 → Idle。 */
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M2);
    XUpgradeStateFile_clear(&state);
    state.m_state = XUpgradeState_Verifying;
    xtest_str_copy(state.m_components[0].m_id, XUPGRADE_COMPONENT_ID_MAX, "app");
    xtest_str_copy(state.m_components[0].m_activeVersion, XUPGRADE_VERSION_MAX, "1.0.0");
    state.m_componentCount = 1;
    state.m_download.m_active = true;
    xtest_str_copy(state.m_download.m_fileName, XUPGRADE_FILENAME_MAX, "junk.pak.part");
    state.m_download.m_offset = 99;
    ok = ok && xtest_save_state(XUP_TEST_SCRATCH_M2, &state);
    ok = ok && xtest_write_partial(XUP_TEST_SCRATCH_M2, "junk.pak.part", "junk", 4);
    {
        XUpgradeEngine* engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XUpgradeConfig config;
        ok = ok && engine != NULL;
        if (engine) {
            xtest_fill_config(&config, XUP_TEST_SCRATCH_M2);
            ok = ok && XUpgradeEngine_start(engine, &config);
            ok = ok && XUpgradeEngine_state(engine) == XUpgradeState_Idle;
            XTEST_FMT(path, sizeof(path),
                      "%s/" XUPGRADE_DIR_UPDATE "/" XUPGRADE_DIR_PARTIAL, XUP_TEST_SCRATCH_M2);
            ok = ok && xtest_file_count(path) == 0;
            ok = ok && xtest_load_state(XUP_TEST_SCRATCH_M2, &state);
            ok = ok && state.m_state == XUpgradeState_Idle && !state.m_download.m_active;
            XUpgradeEngine_stop(engine);
            XClassDelete((XClass*)engine);
        } else {
            ok = false;
        }
    }
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M2);
    /* 矩阵 3：Staging 中断 → 半成品版本目录清理 → Idle。 */
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M3);
    xtest_str_copy(halfJunk, sizeof(halfJunk), "half");
    XUpgradeStateFile_clear(&state);
    state.m_state = XUpgradeState_Staging;
    xtest_str_copy(state.m_components[0].m_id, XUPGRADE_COMPONENT_ID_MAX, "app");
    xtest_str_copy(state.m_components[0].m_activeVersion, XUPGRADE_VERSION_MAX, "1.0.0");
    state.m_componentCount = 1;
    ok = ok && xtest_save_state(XUP_TEST_SCRATCH_M3, &state);
    XTEST_FMT(path, sizeof(path),
              "%s/" XUPGRADE_DIR_VERSIONS "/app/9.9.9/half.txt", XUP_TEST_SCRATCH_M3);
    ok = ok && xtest_write_file(path, halfJunk, 4);
    {
        XUpgradeEngine* engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XUpgradeConfig config;
        ok = ok && engine != NULL;
        if (engine) {
            xtest_fill_config(&config, XUP_TEST_SCRATCH_M3);
            ok = ok && XUpgradeEngine_start(engine, &config);
            ok = ok && XUpgradeEngine_state(engine) == XUpgradeState_Idle;
            ok = ok && !xtest_path_exists(path); /* 半成品目录已清理 */
            ok = ok && xtest_path_exists(path) == false;
            ok = ok && xtest_load_state(XUP_TEST_SCRATCH_M3, &state);
            ok = ok && state.m_state == XUpgradeState_Idle;
            XUpgradeEngine_stop(engine);
            XClassDelete((XClass*)engine);
        } else {
            ok = false;
        }
    }
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M3);
    return ok;
}

/* ========================================================================== */
/*       断电矩阵 4/5/7：staged 标记缺失拒绝、Applying 续做、重复 apply 幂等      */
/* ========================================================================== */

static bool xup_run_stub(const char* root, int* rcOut)
{
    char argv0[16];
    char* stubArgv[4];
    xtest_str_copy(argv0, sizeof(argv0), "xupgrade_e2e_test");
    stubArgv[0] = argv0;
    stubArgv[1] = (char*)"--xupgrade-apply";
    stubArgv[2] = (char*)"--root";
    stubArgv[3] = (char*)root;
    *rcOut = XUpgradeApplyStub_main(4, stubArgv);
    return true;
}

static bool t_matrix457_stub_semantics(void)
{
    XUpgradeStateFile state;
    char pointer[XUPGRADE_VERSION_MAX + 4];
    char* stubArgv[2];
    char argv0[16];
    int rc = -1;
    bool ok = true;
    /* 矩阵 4：Ready 但 staged 标记缺失 → 桩无事发生，不写指针。 */
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M4);
    XUpgradeStateFile_clear(&state);
    state.m_state = XUpgradeState_Ready;
    xtest_str_copy(state.m_components[0].m_id, XUPGRADE_COMPONENT_ID_MAX, "app");
    xtest_str_copy(state.m_components[0].m_activeVersion, XUPGRADE_VERSION_MAX, "1.0.0");
    xtest_str_copy(state.m_components[0].m_maxVersion, XUPGRADE_VERSION_MAX, "1.0.0");
    state.m_componentCount = 1;
    ok = ok && xtest_save_state(XUP_TEST_SCRATCH_M4, &state);
    ok = ok && xtest_seed_version_dir(XUP_TEST_SCRATCH_M4, "app", "2.0.0"); /* 目录在但无标记 */
    ok = ok && xup_run_stub(XUP_TEST_SCRATCH_M4, &rc);
    ok = ok && rc == 0;
    ok = ok && !xtest_path_exists(XUP_TEST_SCRATCH_M4 "/" XUPGRADE_DIR_POINTERS "/app");
    ok = ok && xtest_load_state(XUP_TEST_SCRATCH_M4, &state);
    ok = ok && state.m_state == XUpgradeState_BootConfirming;
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M4);
    /* 矩阵 5：Applying 中断 → 引擎启动按 applyNow 同款逻辑续做切换（staged
     * 标记保留作续做依据，全部成功清标记）→ BootConfirming；指针已切，桩
     * 后续幂等无操作。 */
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M5);
    XUpgradeStateFile_clear(&state);
    state.m_state = XUpgradeState_Applying;
    xtest_str_copy(state.m_components[0].m_id, XUPGRADE_COMPONENT_ID_MAX, "app");
    xtest_str_copy(state.m_components[0].m_activeVersion, XUPGRADE_VERSION_MAX, "1.0.0");
    xtest_str_copy(state.m_components[0].m_stagedVersion, XUPGRADE_VERSION_MAX, "2.0.0");
    xtest_str_copy(state.m_components[0].m_maxVersion, XUPGRADE_VERSION_MAX, "1.0.0");
    state.m_components[0].m_staged = true;
    state.m_componentCount = 1;
    ok = ok && xtest_save_state(XUP_TEST_SCRATCH_M5, &state);
    ok = ok && xtest_seed_version_dir(XUP_TEST_SCRATCH_M5, "app", "2.0.0");
    {
        XUpgradeEngine* engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XUpgradeConfig config;
        ok = ok && engine != NULL;
        if (engine) {
            xtest_fill_config(&config, XUP_TEST_SCRATCH_M5);
            ok = ok && XUpgradeEngine_start(engine, &config);
            /* 新语义：recover 续做切换 → BootConfirming（原回退 Ready 由桩续做）。 */
            ok = ok && XUpgradeEngine_state(engine) == XUpgradeState_BootConfirming;
            XUpgradeEngine_stop(engine);
            XClassDelete((XClass*)engine);
        } else {
            ok = false;
        }
    }
    ok = ok && xtest_read_pointer(XUP_TEST_SCRATCH_M5, "app", pointer, sizeof(pointer));
    ok = ok && xtest_str_eq(pointer, "2.0.0"); /* 引擎启动内已切指针 */
    ok = ok && xtest_load_state(XUP_TEST_SCRATCH_M5, &state);
    ok = ok && state.m_state == XUpgradeState_BootConfirming;
    ok = ok && !state.m_components[0].m_staged; /* 切换成功后才清 staged 标记 */
    /* 矩阵 7：重复 apply 幂等（BootConfirming 态再跑桩，指针不变、返回 0）。 */
    ok = ok && xup_run_stub(XUP_TEST_SCRATCH_M5, &rc);
    ok = ok && rc == 0;
    ok = ok && xtest_read_pointer(XUP_TEST_SCRATCH_M5, "app", pointer, sizeof(pointer));
    ok = ok && xtest_str_eq(pointer, "2.0.0");
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M5);
    /* 桩用法负例：缺 --xupgrade-apply → 退出码 2。 */
    xtest_str_copy(argv0, sizeof(argv0), "xupgrade_e2e_test");
    stubArgv[0] = argv0;
    stubArgv[1] = (char*)"--root";
    rc = XUpgradeApplyStub_main(2, stubArgv);
    ok = ok && rc == 2;
    return ok;
}

/* ========================================================================== */
/*            损坏自检 A：指针指向被删目录 → 回滚写回旧版本                      */
/* ========================================================================== */

static bool t_selfcheck_pointer_missing_dir(void)
{
    XUpgradeEngine* engine = NULL;
    XUpgradeConfig config;
    XUpgradeStateFile state;
    char pointer[XUPGRADE_VERSION_MAX + 4];
    bool ok = true;
    xtest_scratch_wipe(XUP_TEST_SCRATCH_CKA);
    /* 夹具：Active 设备（app@1.0.0），外部把指针改成不存在的 9.9.9。 */
    ok = ok && xtest_seed_version_dir(XUP_TEST_SCRATCH_CKA, "app", "1.0.0");
    ok = ok && xtest_seed_version_dir(XUP_TEST_SCRATCH_CKA, "app", "2.0.0");
    XUpgradeStateFile_clear(&state);
    state.m_state = XUpgradeState_Active;
    xtest_str_copy(state.m_components[0].m_id, XUPGRADE_COMPONENT_ID_MAX, "app");
    xtest_str_copy(state.m_components[0].m_activeVersion, XUPGRADE_VERSION_MAX, "1.0.0");
    xtest_str_copy(state.m_components[0].m_maxVersion, XUPGRADE_VERSION_MAX, "1.0.0");
    state.m_componentCount = 1;
    ok = ok && xtest_save_state(XUP_TEST_SCRATCH_CKA, &state);
    ok = ok && xtest_write_pointer(XUP_TEST_SCRATCH_CKA, "app", "9.9.9");
    xtest_events_reset();
    engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    ok = ok && engine != NULL;
    if (!engine) return false;
    XUpgradeEngine_setListener(engine, xtest_listener, NULL);
    xtest_fill_config(&config, XUP_TEST_SCRATCH_CKA);
    ok = ok && XUpgradeEngine_start(engine, &config);
    /* 自检：指针与记录不符（非确认窗口）→ 回滚写回 1.0.0，上报 RolledBack。 */
    ok = ok && XUpgradeEngine_selfCheck(engine);
    ok = ok && xtest_read_pointer(XUP_TEST_SCRATCH_CKA, "app", pointer, sizeof(pointer));
    ok = ok && xtest_str_eq(pointer, "1.0.0");
    ok = ok && g_xup_events.m_rolledBack >= 1;
    ok = ok && xtest_load_state(XUP_TEST_SCRATCH_CKA, &state);
    ok = ok && xtest_str_eq(state.m_components[0].m_activeVersion, "1.0.0");
    ok = ok && state.m_components[0].m_rollbackCount == 1;
    /* 再自检：回滚后设备健康，通过且不再回滚。 */
    xtest_events_reset();
    ok = ok && XUpgradeEngine_selfCheck(engine);
    ok = ok && g_xup_events.m_rolledBack == 0;
    XUpgradeEngine_stop(engine);
    XClassDelete((XClass*)engine);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_CKA);
    return ok;
}

/* ========================================================================== */
/*      全新设备自检：重建基线后无指针/无版本目录 → 按"无事发生"收编            */
/* ========================================================================== */

static bool t_selfcheck_fresh_device(void)
{
    XUpgradeEngine* engine = NULL;
    XUpgradeConfig config;
    XUpgradeStateFile state;
    char expectVer[XUPGRADE_VERSION_MAX];
    bool ok = true;
    xtest_scratch_wipe(XUP_TEST_SCRATCH_FRSH);
    /* 夹具：裸 scratch——无 state.json/pointers/versions（引擎 start 时按
     * 全新设备口径重建基线，activeVersion=编译期产品版本）。 */
    expectVer[0] = '\0';
    (void)XUpgradeIdentity_version(expectVer, sizeof(expectVer));
    ok = ok && expectVer[0] != '\0';
    xtest_events_reset();
    engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    ok = ok && engine != NULL;
    if (!engine) return false;
    XUpgradeEngine_setListener(engine, xtest_listener, NULL);
    xtest_fill_config(&config, XUP_TEST_SCRATCH_FRSH);
    ok = ok && XUpgradeEngine_start(engine, &config);
    /* 修复回归锁（2026-10-08 P1）：首装自检曾必败回滚——版本目录不存在时
     * 回滚必失败，rollbackCount 逐次累积，3 次启动后自动检查被永久停用。
     * 现语义：指针缺失且版本目录不存在=无事发生，自检通过零回滚。 */
    ok = ok && XUpgradeEngine_selfCheck(engine);
    ok = ok && g_xup_events.m_rolledBack == 0;
    ok = ok && xtest_load_state(XUP_TEST_SCRATCH_FRSH, &state);
    ok = ok && state.m_componentCount == 1;
    ok = ok && xtest_str_eq(state.m_components[0].m_activeVersion, expectVer);
    ok = ok && state.m_components[0].m_rollbackCount == 0;
    /* 再自检：仍通过、零回滚（引擎未被停用，后续检查链可用）。 */
    xtest_events_reset();
    ok = ok && XUpgradeEngine_selfCheck(engine);
    ok = ok && g_xup_events.m_rolledBack == 0;
    XUpgradeEngine_stop(engine);
    XClassDelete((XClass*)engine);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_FRSH);
    return ok;
}

/* ========================================================================== */
/*      损坏自检 B：BootConfirming 窗口损坏新版本关键文件 → 回滚到旧版本         */
/* ========================================================================== */

static bool t_selfcheck_corrupt_new_version(void)
{
    XUpgradeEngine* engine = NULL;
    XUpgradeConfig config;
    XUpgradeStateFile state;
    char pointer[XUPGRADE_VERSION_MAX + 4];
    char path[1400];
    char verifyBody[160];
    char payload[64];
    uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
    char hex[XUPGRADE_SHA256_HEX_MAX];
    uint8_t i;
    bool ok = true;
    xtest_scratch_wipe(XUP_TEST_SCRATCH_CKB);
    /* 夹具：旧版 1.0.0 完好；新版 2.0.0 已 staged+指针已切（BootConfirming），
     * 新版目录含 .verify 清单抽查 payload.bin。 */
    ok = ok && xtest_seed_version_dir(XUP_TEST_SCRATCH_CKB, "app", "1.0.0");
    for (i = 0; i < sizeof(payload); ++i) payload[i] = (char)('A' + (i % 26));
    ok = ok && xtest_sha256((const uint8_t*)payload, sizeof(payload), digest);
    xtest_bin2hex(digest, XUPGRADE_SHA256_BIN_SIZE, hex);
    XTEST_FMT(verifyBody, sizeof(verifyBody), "payload.bin %s\n", hex);
    XTEST_FMT(path, sizeof(path),
              "%s/" XUPGRADE_DIR_VERSIONS "/app/2.0.0/payload.bin", XUP_TEST_SCRATCH_CKB);
    ok = ok && xtest_write_file(path, payload, sizeof(payload));
    ok = ok && xtest_seed_version_dir(XUP_TEST_SCRATCH_CKB, "app", "2.0.0");
    XTEST_FMT(path, sizeof(path),
              "%s/" XUPGRADE_DIR_VERSIONS "/app/2.0.0/" XUPGRADE_STORE_DIR_VERIFY_FILE,
              XUP_TEST_SCRATCH_CKB);
    ok = ok && xtest_write_file(path, verifyBody, xtest_str_len(verifyBody));
    XUpgradeStateFile_clear(&state);
    state.m_state = XUpgradeState_BootConfirming;
    xtest_str_copy(state.m_components[0].m_id, XUPGRADE_COMPONENT_ID_MAX, "app");
    xtest_str_copy(state.m_components[0].m_activeVersion, XUPGRADE_VERSION_MAX, "1.0.0");
    xtest_str_copy(state.m_components[0].m_stagedVersion, XUPGRADE_VERSION_MAX, "2.0.0");
    xtest_str_copy(state.m_components[0].m_maxVersion, XUPGRADE_VERSION_MAX, "1.0.0");
    state.m_components[0].m_staged = true;
    state.m_componentCount = 1;
    ok = ok && xtest_save_state(XUP_TEST_SCRATCH_CKB, &state);
    ok = ok && xtest_write_pointer(XUP_TEST_SCRATCH_CKB, "app", "2.0.0");
    /* 损坏新版本关键文件。 */
    XTEST_FMT(path, sizeof(path),
              "%s/" XUPGRADE_DIR_VERSIONS "/app/2.0.0/payload.bin", XUP_TEST_SCRATCH_CKB);
    ok = ok && xtest_write_file(path, "CORRUPTED-PAYLOAD-BYTES--", 25);
    /* 自检（设计文档 §8.3：失败 → rollback 写回上一版本 1.0.0 → 事件上报）。 */
    xtest_events_reset();
    engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    ok = ok && engine != NULL;
    if (!engine) return false;
    XUpgradeEngine_setListener(engine, xtest_listener, NULL);
    xtest_fill_config(&config, XUP_TEST_SCRATCH_CKB);
    ok = ok && XUpgradeEngine_start(engine, &config);
    ok = ok && XUpgradeEngine_selfCheck(engine);
    /* 设计口径：指针写回旧版本 1.0.0， RolledBack 上报，计数 +1。 */
    ok = ok && xtest_read_pointer(XUP_TEST_SCRATCH_CKB, "app", pointer, sizeof(pointer));
    if (!xtest_str_eq(pointer, "1.0.0")) {
        XPrintf("    [损坏新版自检：指针=%s，设计期望回滚到 1.0.0]\n", pointer);
    }
    ok = ok && xtest_str_eq(pointer, "1.0.0");
    ok = ok && g_xup_events.m_rolledBack >= 1;
    /* 再自检：旧版本设备健康 → 通过。 */
    xtest_events_reset();
    ok = ok && XUpgradeEngine_selfCheck(engine);
    ok = ok && g_xup_events.m_rolledBack == 0;
    ok = ok && xtest_load_state(XUP_TEST_SCRATCH_CKB, &state);
    ok = ok && xtest_str_eq(state.m_components[0].m_activeVersion, "1.0.0");
    XUpgradeEngine_stop(engine);
    XClassDelete((XClass*)engine);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_CKB);
    return ok;
}

/* ========================================================================== */
/*              回滚计数超限：自动升级停用（§8.3 不变式 4）                      */
/* ========================================================================== */

static bool t_rollback_limit_disables_check(void)
{
    XUpgradeEngine* engine = NULL;
    XUpgradeConfig config;
    XUpgradeStateFile state;
    bool ok = true;
    xtest_scratch_wipe(XUP_TEST_SCRATCH_LIMIT);
    ok = ok && xtest_seed_version_dir(XUP_TEST_SCRATCH_LIMIT, "app", "1.0.0");
    XUpgradeStateFile_clear(&state);
    state.m_state = XUpgradeState_Active;
    xtest_str_copy(state.m_components[0].m_id, XUPGRADE_COMPONENT_ID_MAX, "app");
    xtest_str_copy(state.m_components[0].m_activeVersion, XUPGRADE_VERSION_MAX, "1.0.0");
    xtest_str_copy(state.m_components[0].m_maxVersion, XUPGRADE_VERSION_MAX, "1.0.0");
    state.m_components[0].m_rollbackCount = 3; /* > XUPGRADE_ROLLBACK_LIMIT */
    state.m_componentCount = 1;
    ok = ok && xtest_save_state(XUP_TEST_SCRATCH_LIMIT, &state);
    ok = ok && xtest_write_pointer(XUP_TEST_SCRATCH_LIMIT, "app", "1.0.0");
    engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    ok = ok && engine != NULL;
    if (!engine) return false;
    xtest_fill_config(&config, XUP_TEST_SCRATCH_LIMIT);
    ok = ok && XUpgradeEngine_start(engine, &config);
    ok = ok && XUpgradeEngine_selfCheck(engine); /* 自检本身可通过 */
    ok = ok && !XUpgradeEngine_checkNow(engine); /* 手动检查被拒：自动升级停用 */
    XUpgradeEngine_stop(engine);
    XClassDelete((XClass*)engine);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_LIMIT);
    return ok;
}

/* ========================================================================== */
/*                      引擎锁：同进程双实例互斥（不变式 6）                     */
/* ========================================================================== */

static bool t_engine_lock_double_instance(void)
{
    XUpgradeEngine* first = NULL;
    XUpgradeEngine* second = NULL;
    XUpgradeConfig config;
    bool ok = true;
    xtest_scratch_wipe(XUP_TEST_SCRATCH_LOCK);
    xtest_fill_config(&config, XUP_TEST_SCRATCH_LOCK);
    first = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    second = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    ok = ok && first != NULL && second != NULL;
    if (first && second) {
        ok = ok && XUpgradeEngine_start(first, &config);
        ok = ok && xtest_path_exists(XUP_TEST_SCRATCH_LOCK "/" XUPGRADE_DIR_UPDATE
                                     "/" XUPGRADE_FILE_LOCK);
        ok = ok && !XUpgradeEngine_start(second, &config); /* 锁被本进程实例持有 */
        ok = ok && !XUpgradeEngine_checkNow(second);       /* 未启动：拒绝 */
        XUpgradeEngine_stop(first);
        ok = ok && !xtest_path_exists(XUP_TEST_SCRATCH_LOCK "/" XUPGRADE_DIR_UPDATE
                                      "/" XUPGRADE_FILE_LOCK); /* 停机移除锁 */
        /* 停机后锁可重取。 */
        ok = ok && XUpgradeEngine_start(second, &config);
        XUpgradeEngine_stop(second);
    } else {
        ok = false;
    }
    if (first) XClassDelete((XClass*)first);
    if (second) XClassDelete((XClass*)second);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_LOCK);
    return ok;
}

/* ========================================================================== */
/*    M2/M3 契约扩展用例（验签/防降级/灰度/策略时机/组件线/A-B/遗留回归）。        */
/*    本块为纯追加：不改动任何既有用例与既有共享夹具的既有行为；引擎级用例经      */
/*    xup_case_serve 把进程内服务器的 manifest 切换为用例专属内容，用毕恢复，     */
/*    后跑用例不受污染（父进程逐用例子进程隔离执行，见文件头执行模型）。          */
/* ========================================================================== */

#define XUP_TEST_SCRATCH_ADG    "xupgrade_e2e_scratch_adg"
#define XUP_TEST_SCRATCH_GRAY   "xupgrade_e2e_scratch_gray"
#define XUP_TEST_SCRATCH_POL    "xupgrade_e2e_scratch_pol"
#define XUP_TEST_SCRATCH_COMP   "xupgrade_e2e_scratch_comp"
#define XUP_TEST_SCRATCH_AB     "xupgrade_e2e_scratch_ab"
#define XUP_TEST_SCRATCH_REG    "xupgrade_e2e_scratch_reg"

/* --- 无公开契约头的实现侧入口：本组声明已上移至文件头顶部原型区
 *     （XUpgradeVerify_verifyManifest / antiDowngradeAllowed /
 *      XSsl_p256SignHashDer 等，供引擎级夹具造签与纯函数表共用）。 --- */

/** 网卡抽象（deviceKey MAC 级准绳；XUpgradeIdentity.c 同款包含）。 */
#include "XNetworkInterface.h"

/** CRC32（A/B 元数据块构造用，与 XUpgradeStoreAb.c 同源 XCrc32 门面）。 */
#include "XCrc.h"

/** @brief 子串包含判断（string.h 比较函数被禁，自实现）。 */
static bool xtest_str_contains(const char* haystack, const char* needle)
{
    size_t nLen;
    size_t i;
    if (!haystack || !needle) return false;
    nLen = xtest_str_len(needle);
    if (nLen == 0) return true;
    for (i = 0; haystack[i] != '\0'; ++i) {
        size_t k;
        for (k = 0; k < nLen; ++k) {
            if (haystack[i + k] != needle[k]) break;
        }
        if (k == nLen) return true;
        if (haystack[i + k] == '\0') break;
    }
    return false;
}

/* --- 用例级事件记录器：既有 xtest_listener 不区分 m_type/m_policy/m_gray，
 *     追加独立记录器承载新断言（避免改动既有用例的统计语义）。 --- */

/** @brief 用例级事件统计（引擎工作线程写、主线程等待后读；单写单读）。 */
typedef struct XUpCaseEvents {
    volatile int m_byType[7];              /**< 按 XUpgradeEventType 计数。 */
    volatile int m_policySeen;             /**< 已收到 UpdateAvailable（带策略）。 */
    volatile int m_lastPolicy;             /**< 最近 UpdateAvailable 的 m_policy。 */
    volatile int m_lastGray;               /**< 最近 UpdateAvailable 的 m_gray。 */
    volatile int m_dlOrderCount;           /**< Download 0% 事件的组件到达序（去重）。 */
    char m_dlOrder[8][XUPGRADE_COMPONENT_ID_MAX]; /**< 组件下载顺序。 */
    char m_lastDetail[XUPGRADE_DETAIL_MAX];/**< 最近事件说明。 */
} XUpCaseEvents;

static XUpCaseEvents g_xup_case_events;

/** @brief 清零用例级事件统计。 */
static void xup_case_events_reset(void)
{
    XMemset((void*)&g_xup_case_events, 0, sizeof(g_xup_case_events));
}

/** @brief 用例级监听器（记录类型计数/策略灰度/下载组件序/最近说明）。 */
static void xup_case_listener(void* userData, const XUpgradeEvent* event)
{
    XUpCaseEvents* ev = (XUpCaseEvents*)userData;
    if (!ev || !event) return;
    if ((int)event->m_type >= 0 && (int)event->m_type < 7)
        ++ev->m_byType[(int)event->m_type];
    xtest_str_copy(ev->m_lastDetail, sizeof(ev->m_lastDetail), event->m_detail);
    if (event->m_type == XUpgradeEventType_UpdateAvailable) {
        ev->m_lastPolicy = (int)event->m_policy;
        ev->m_lastGray = (int)event->m_gray;
        ev->m_policySeen = 1;
    }
    /* 下载顺序记录（2026-10-07 语义修正后）：Download 0% 进度以 Progress
     * 类型携带组件 id（原 StateChanged 口径=进度条 0% 根因），据此记录
     * 多组件下载顺序（同组件续传重试去重）。 */
    if (event->m_type == XUpgradeEventType_Progress &&
        event->m_phase == XUpgradePhase_Download && event->m_percent == 0 &&
        event->m_component[0] != '\0' && ev->m_dlOrderCount < 8) {
        int i;
        bool dup = false;
        for (i = 0; i < ev->m_dlOrderCount; ++i)
            if (xtest_str_eq(ev->m_dlOrder[i], event->m_component)) { dup = true; break; }
        if (!dup) {
            xtest_str_copy(ev->m_dlOrder[ev->m_dlOrderCount],
                           XUPGRADE_COMPONENT_ID_MAX, event->m_component);
            ++ev->m_dlOrderCount;
        }
    }
}

/* --- 用例级等待谓词 --- */

static bool xup_case_wait_update_available(void* ud)
{
    (void)ud;
    return g_xup_case_events.m_policySeen > 0;
}

static bool xup_case_wait_check_failed(void* ud)
{
    (void)ud;
    return g_xup_case_events.m_byType[XUpgradeEventType_CheckFailed] > 0;
}

static bool xup_case_wait_ready_auto_apply(void* ud)
{
    (void)ud;
    return g_xup_case_events.m_byType[XUpgradeEventType_ReadyAutoApply] > 0;
}

static bool xup_case_wait_need_restart(void* ud)
{
    (void)ud;
    return g_xup_case_events.m_byType[XUpgradeEventType_ReadyNeedRestart] > 0;
}

static bool xup_case_wait_detail_contains(void* ud)
{
    return xtest_str_contains(g_xup_case_events.m_lastDetail, (const char*)ud);
}

/* --- 进程内服务器内容切换（共享 g_xup_serverCtx，用毕必须恢复） --- */

static void xup_case_serve(const XByteArray* manifest, const XByteArray* pak,
                           const char* pakName)
{
    g_xup_serverCtx.m_manifest = manifest;
    g_xup_serverCtx.m_pak = pak;
    g_xup_serverCtx.m_pakName = pakName;
}

static void xup_case_serve_restore(void)
{
    xup_case_serve(g_xup_manifest, g_xup_pak, g_xup_pakName);
}

/**
 * @brief 组装用例专属 manifest JSON（真静态服务载体，非夹具特例：内容仍经
 *        进程内服务器按真实 HTTP 流程下发给引擎）。
 * @param version 整包版本串。
 * @param policy "forced"/"optional"/"silent"。
 * @param middleFields 插在 build 之后的字段文本（各字段自带尾逗号，如
 *                     "\"min_version\":\"2.0.0\","；NULL=无）。
 * @param componentsJson components[] 数组元素文本（逗号分隔）。
 * @return 新 XByteArray（调用者释放）；格式化/造签失败返回 NULL。
 * @note sig 经测试私钥造真签（M2 验签门已接入引擎检查链，§4.2）；验签链
 *       纯函数面另在用例 16 覆盖。
 */
static XByteArray* xup_case_manifest(const char* version, const char* policy,
                                     const char* middleFields, const char* componentsJson)
{
    XString* json = XString_create_fmt_utf8(
        "{\"schema\":1,\"channel\":\"stable\",\"version\":\"%s\","
        "\"build\":\"2026.10.07-01\",%s"
        "\"policy\":\"%s\",\"components\":[%s],"
        "\"sig\":\"e2e-case-unsigned\"}",
        version, middleFields ? middleFields : "", policy,
        componentsJson ? componentsJson : "");
    XByteArray* bytes;
    if (!json) return NULL;
#if XUPGRADE_VERIFY_ON
    /* M2 验签门已接入引擎检查链（§4.2）：夹具以测试私钥造真签。 */
    bytes = xtest_sign_manifest_json(XString_toUtf8(json), "\"sig\":\"e2e-case-unsigned\"");
    XClassDelete(json);
#else
    bytes = XByteArray_create_with_data(XString_toUtf8(json), XString_toUtf8_length(json));
    XClassDelete(json);
#endif
    return bytes;
}

/** @brief app 组件条目文本（共享 .pak 载体；requiresJson 形如
 *         ",\"requires\":{\"fontres\":\">=2.0\"}"，NULL=无依赖）。 */
static void xup_case_app_component(char* out, size_t cap, const char* version,
                                   const char* requiresJson)
{
    char shaHex[XUPGRADE_SHA256_HEX_MAX];
    (void)xtest_pak_sha256_hex(g_xup_pak, shaHex, sizeof(shaHex));
    XTEST_FMT(out, cap,
              "{\"id\":\"app\",\"os\":\"win64\",\"version\":\"%s\",\"file\":\"%s\","
              "\"size\":%llu,\"sha256\":\"%s\"%s}",
              version, g_xup_pakName, (unsigned long long)g_xup_pakPayloadSize,
              shaHex, requiresJson ? requiresJson : "");
}

/** @brief fontres 组件条目文本（os=any，与 app 共用同一 .pak；版本须同
 *         manifest.version 才能复用单条目包的 manifest.version 标记内容）。 */
static void xup_case_fontres_component(char* out, size_t cap, const char* version)
{
    char shaHex[XUPGRADE_SHA256_HEX_MAX];
    (void)xtest_pak_sha256_hex(g_xup_pak, shaHex, sizeof(shaHex));
    XTEST_FMT(out, cap,
              "{\"id\":\"fontres\",\"os\":\"any\",\"version\":\"%s\",\"file\":\"%s\","
              "\"size\":%llu,\"sha256\":\"%s\"}",
              version, g_xup_pakName, (unsigned long long)g_xup_pakPayloadSize, shaHex);
}

/* ========================================================================== */
/*          用例 16：P-256 验签链与 canonical 一致性（真实签名/验签原语）         */
/* ========================================================================== */

static bool t_p256_verify_chain(void)
{
    bool ok = true;
#if XUPGRADE_VERIFY_ON
    XUpgradeManifest m;
    static uint8_t canonical[XUPGRADE_MANIFEST_CANONICAL_MAX];
    static uint8_t canonical2[XUPGRADE_MANIFEST_CANONICAL_MAX];
    uint32_t len1 = 0;
    uint32_t len2 = 0;
    uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
    uint8_t der[XUP_TEST_P256_DER_SIG_MAX];
    size_t derLen = 0;
    char sigB64[XUPGRADE_SIGNATURE_MAX];
    size_t b64Len = 0;

    /* 解析合法 manifest → canonical（真实序列化原语）。 */
    ok = ok && XUpgradeManifest_parse(&m, k_manifestValid, xtest_str_len(k_manifestValid));
    ok = ok && XUpgradeManifest_canonicalBytes(&m, canonical,
                                               (uint32_t)sizeof(canonical), &len1);
    if (!ok) {
        XPrintf("    [canonical 序列化失败]\n");
        return false;
    }
    ok = ok && len1 > 0 && len1 <= XUPGRADE_MANIFEST_CANONICAL_MAX;
    /* canonical 与序列化一致性：同一 manifest 解析两次、序列化两次字节相同。 */
    ok = ok && XUpgradeManifest_parse(&m, k_manifestValid, xtest_str_len(k_manifestValid));
    ok = ok && XUpgradeManifest_canonicalBytes(&m, canonical2,
                                               (uint32_t)sizeof(canonical2), &len2);
    ok = ok && len1 == len2 && XMemcmp(canonical, canonical2, len1) == 0;
    /* 无空白：canonical 为紧凑 JSON（不含空格/换行）。 */
    {
        uint32_t i;
        for (i = 0; ok && i < len1; ++i)
            if (canonical[i] == ' ' || canonical[i] == '\n' || canonical[i] == '\r' ||
                canonical[i] == '\t') {
                XPrintf("    [canonical 含空白字符 偏移%u]\n", (unsigned)i);
                ok = false;
            }
    }
    /* 容量不足：false 且 outLen 不写入（哨兵保持）。 */
    {
        uint32_t probe = 0x5A5A5A5Au;
        ok = ok && !XUpgradeManifest_canonicalBytes(&m, canonical2, 16, &probe);
        ok = ok && probe == 0x5A5A5A5Au;
    }
    /* 真实签名：测试私钥对 canonical 摘要造 DER 签名 → base64 → 注入 m_sig。 */
    ok = ok && xtest_sha256(canonical, len1, digest);
    ok = ok && XSsl_p256SignHashDer(XUpgrade_testPrivateKey, sizeof(XUpgrade_testPrivateKey),
                                    digest, sizeof(digest), der, sizeof(der), &derLen);
    ok = ok && derLen >= 8 && derLen <= XUP_TEST_P256_DER_SIG_MAX;
    b64Len = sizeof(sigB64);
    ok = ok && XBase64_encode(der, derLen, sigB64, &b64Len) == 0;
    ok = ok && xtest_str_copy(m.m_sig, XUPGRADE_SIGNATURE_MAX, sigB64);
    /* 验签过（缺省信任钥=测试钥占位，XUpgrade_trust_pubkey.h）。 */
    ok = ok && XUpgradeVerify_verifyManifest(&m) == XUpgradeError_Ok;
    /* 篡改任一字段 → 拒（防降级字段与组件版本各验一处）。 */
    {
        char keep[XUPGRADE_VERSION_MAX];
        xtest_str_copy(keep, sizeof(keep), m.m_version);
        xtest_str_copy(m.m_version, XUPGRADE_VERSION_MAX, "2.0.1");
        ok = ok && XUpgradeVerify_verifyManifest(&m) == XUpgradeError_VerifyFailed;
        xtest_str_copy(m.m_version, XUPGRADE_VERSION_MAX, keep);
        ok = ok && XUpgradeVerify_verifyManifest(&m) == XUpgradeError_Ok;
        xtest_str_copy(keep, sizeof(keep), m.m_minVersion);
        xtest_str_copy(m.m_minVersion, XUPGRADE_VERSION_MAX, "9.9.9");
        ok = ok && XUpgradeVerify_verifyManifest(&m) == XUpgradeError_VerifyFailed;
        xtest_str_copy(m.m_minVersion, XUPGRADE_VERSION_MAX, keep);
    }
    /* 无签名/签名非法 → 拒。 */
    {
        char keep[XUPGRADE_SIGNATURE_MAX];
        xtest_str_copy(keep, sizeof(keep), m.m_sig);
        m.m_sig[0] = '\0';
        ok = ok && XUpgradeVerify_verifyManifest(&m) == XUpgradeError_VerifyFailed;
        xtest_str_copy(m.m_sig, XUPGRADE_SIGNATURE_MAX, "!!!not-base64!!!");
        ok = ok && XUpgradeVerify_verifyManifest(&m) == XUpgradeError_VerifyFailed;
        xtest_str_copy(m.m_sig, XUPGRADE_SIGNATURE_MAX, keep);
    }
    /* 换钥注入生效：坏钥注入 → 拒；NULL 复位内嵌缺省 → 过；显式测试钥 → 过；
     * 非法注入（长度/首字节）拒绝且原钥保持。 */
    {
        uint8_t badKey[XUPGRADE_TRUST_KEY_SEC1_SIZE];
        XMemset(badKey, 0xAA, sizeof(badKey));
        badKey[0] = 0x04u;
        ok = ok && XUpgradeVerify_setTrustKey(badKey, (uint32_t)sizeof(badKey));
        ok = ok && XUpgradeVerify_verifyManifest(&m) == XUpgradeError_VerifyFailed;
        ok = ok && XUpgradeVerify_setTrustKey(NULL, 0);
        ok = ok && XUpgradeVerify_verifyManifest(&m) == XUpgradeError_Ok;
        ok = ok && XUpgradeVerify_setTrustKey(XUpgrade_testPublicKeySec1,
                                              XUPGRADE_TRUST_KEY_SEC1_SIZE);
        ok = ok && XUpgradeVerify_verifyManifest(&m) == XUpgradeError_Ok;
        ok = ok && !XUpgradeVerify_setTrustKey(badKey, 64);   /* 长度不符 */
        ok = ok && XUpgradeVerify_verifyManifest(&m) == XUpgradeError_Ok;
        badKey[0] = 0x03u;                                    /* 首字节非 0x04 */
        ok = ok && !XUpgradeVerify_setTrustKey(badKey, XUPGRADE_TRUST_KEY_SEC1_SIZE);
        ok = ok && XUpgradeVerify_verifyManifest(&m) == XUpgradeError_Ok;
        XUpgradeVerify_setTrustKey(NULL, 0);                  /* 恢复缺省信任锚 */
    }
    /* verifyP256 直入负例：公钥长度/签名篡改。 */
    {
        uint8_t broken[XUP_TEST_P256_DER_SIG_MAX];
        XMemcpy(broken, der, derLen);
        broken[derLen - 1] = (uint8_t)(broken[derLen - 1] ^ 0x01u);
        ok = ok && XUpgradeVerify_verifyP256(canonical, len1, der, derLen,
                                             XUpgrade_testPublicKeySec1,
                                             64) == XUpgradeError_InvalidArgument;
        ok = ok && XUpgradeVerify_verifyP256(canonical, len1, der, derLen,
                                             XUpgrade_testPublicKeySec1,
                                             XUPGRADE_TRUST_KEY_SEC1_SIZE) == XUpgradeError_Ok;
        ok = ok && XUpgradeVerify_verifyP256(canonical, len1, broken, derLen,
                                             XUpgrade_testPublicKeySec1,
                                             XUPGRADE_TRUST_KEY_SEC1_SIZE)
                  == XUpgradeError_VerifyFailed;
    }
#else
    XPrintf("    [XUPGRADE_VERIFY_ON=0：验签链用例按能力裁剪空转]\n");
#endif /* XUPGRADE_VERIFY_ON */
    return ok;
}

/* ========================================================================== */
/*        用例 17：防降级判定 + min_version 强制锚事件链（§4.1/§10）             */
/* ========================================================================== */

static bool t_policy_anti_downgrade(void)
{
    XUpgradeEngine* engine = NULL;
    XUpgradeConfig config;
    XUpgradeStateFile state;
    XByteArray* manifest = NULL;
    char comp[768];
    char pointer[XUPGRADE_VERSION_MAX + 4];
    bool ok = true;
#if XUPGRADE_VERIFY_ON
    /* 纯判定表：antiDowngradeAllowed（XUpgradeVerify.c:476 判定规则 1-5）。 */
    {
        static const struct {
            const char* m_candidate;
            const char* m_max;
            const char* m_rollbackTo;
            bool m_allowed;
        } cases[] = {
            { "2.0.0", "2.0.0", "",       true },  /* 等于基线：非降级 */
            { "2.1.0", "2.0.0", "",       true },  /* 高于基线 */
            { "1.5.0", "2.0.0", "",       false }, /* 降级且无回滚窗口：硬门 */
            { "1.0.0", "2.0.0", "1.0.0",  true },  /* 窗口内（<=地板）豁免 */
            { "1.5.0", "2.0.0", "1.0.0",  false }, /* 高于地板的降级仍拦截 */
            { "1.5.0", "",      "",       true },  /* 无基线放行 */
            { "1.5.0", "1.0.0", "2.0.0",  true },  /* 基线低于地板：不再拦截 */
            { "1.5.0", "2.0.0", "0.5.0",  false }  /* 地板过旧：窗口不含候选 */
        };
        size_t i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            bool allowed = XUpgradeVerify_antiDowngradeAllowed(
                cases[i].m_candidate, cases[i].m_max, cases[i].m_rollbackTo);
            if (allowed != cases[i].m_allowed) {
                XPrintf("    [防降级判定 #%d 期望 %d 实得 %d]\n", (int)i,
                        cases[i].m_allowed ? 1 : 0, allowed ? 1 : 0);
                ok = false;
            }
        }
    }
    /* 纯判定表：forcedAnchorActive（min_version / policy_forced_below 双锚）。 */
    {
        static const struct {
            const char* m_local;
            const char* m_min;
            const char* m_below;
            bool m_forced;
        } cases[] = {
            { "1.0.0", "2.0.0", "",      true },
            { "2.0.0", "2.0.0", "",      false },
            { "2.1.0", "2.0.0", "",      false },
            { "1.0.0", "",      "1.0.5", true },
            { "1.0.6", "",      "1.0.5", false },
            { "1.0.0", "2.0.0", "0.5.0", true },  /* 任一锚命中即强制 */
            { "3.0.0", "",      "",      false },
            { NULL,    "0.0.1", "",      true }   /* NULL 按空串：低于锚线 */
        };
        size_t i;
        for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            /* 契约位 XUpgradeGray_forcedAnchored（Verify 侧逐行等价副本已按
             * 去重裁定删除）；锚字段从零值 manifest 直填，local 传原串
             * （NULL 由 compareVersions 归一为空串，与旧副本语义一致）。 */
            XUpgradeManifest anchorManifest;
            bool forced;
            XMemset(&anchorManifest, 0, sizeof(anchorManifest));
            if (cases[i].m_min)
                xtest_str_copy(anchorManifest.m_minVersion, XUPGRADE_VERSION_MAX,
                               cases[i].m_min);
            if (cases[i].m_below)
                xtest_str_copy(anchorManifest.m_policyForcedBelow, XUPGRADE_VERSION_MAX,
                               cases[i].m_below);
            forced = XUpgradeGray_forcedAnchored(&anchorManifest, cases[i].m_local);
            if (forced != cases[i].m_forced) {
                XPrintf("    [强制锚判定 #%d 期望 %d 实得 %d]\n", (int)i,
                        cases[i].m_forced ? 1 : 0, forced ? 1 : 0);
                ok = false;
            }
        }
    }
    /* 引擎级：min_version 强制锚触发 forced 事件链（optional manifest 被锚
     * 升格为 forced → UpdateAvailable 携带 Forced → 自动续跑 → ReadyAutoApply，
     * 宽限期取 manifest 未设置的缺省 0）。 */
    xtest_scratch_wipe(XUP_TEST_SCRATCH_ADG);
    xup_case_events_reset();
    xup_case_app_component(comp, sizeof(comp), "2.0.0", NULL);
    manifest = xup_case_manifest("2.0.0", "optional", "\"min_version\":\"2.0.0\",", comp);
    ok = ok && manifest != NULL;
    xup_case_serve(manifest, g_xup_pak, g_xup_pakName);
    engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    ok = ok && engine != NULL;
    if (engine) {
        XUpgradeEngine_setListener(engine, xup_case_listener, &g_xup_case_events);
        xtest_fill_config(&config, XUP_TEST_SCRATCH_ADG);
        ok = ok && XUpgradeEngine_start(engine, &config);
        ok = ok && XUpgradeEngine_checkNow(engine);
        ok = ok && xtest_wait_until(xup_case_wait_update_available, NULL);
        ok = ok && g_xup_case_events.m_lastPolicy == (int)XUpgradePolicy_Forced;
        /* forced 自动续跑到 staged 完成 → ReadyAutoApply（宽限期 0）。 */
        ok = ok && xtest_wait_until(xup_case_wait_ready_auto_apply, NULL);
        ok = ok && xtest_str_contains(g_xup_case_events.m_lastDetail, "宽限期 0 小时");
        ok = ok && g_xup_case_events.m_byType[XUpgradeEventType_ReadyNeedRestart] == 0;
        ok = ok && xtest_load_state(XUP_TEST_SCRATCH_ADG, &state);
        ok = ok && state.m_state == XUpgradeState_Ready && state.m_components[0].m_staged;
        XUpgradeEngine_stop(engine);
        XClassDelete((XClass*)engine);
        /* 桩切指针 + 自检收敛 Active（forced 链全程落地）。 */
        {
            int rc = -1;
            ok = ok && xup_run_stub(XUP_TEST_SCRATCH_ADG, &rc);
            ok = ok && rc == 0;
            ok = ok && xtest_read_pointer(XUP_TEST_SCRATCH_ADG, "app", pointer,
                                          sizeof(pointer));
            ok = ok && xtest_str_eq(pointer, "2.0.0");
        }
        xup_case_events_reset();
        engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        ok = ok && engine != NULL;
        if (engine) {
            XUpgradeEngine_setListener(engine, xup_case_listener, &g_xup_case_events);
            ok = ok && XUpgradeEngine_start(engine, &config);
            ok = ok && XUpgradeEngine_selfCheck(engine);
            ok = ok && XUpgradeEngine_state(engine) == XUpgradeState_Active;
            ok = ok && xtest_load_state(XUP_TEST_SCRATCH_ADG, &state);
            ok = ok && xtest_str_eq(state.m_components[0].m_activeVersion, "2.0.0");
            ok = ok && xtest_str_eq(state.m_components[0].m_maxVersion, "2.0.0");
            XUpgradeEngine_stop(engine);
            XClassDelete((XClass*)engine);
        }
    }
    if (manifest) XClassDelete(manifest);
    xup_case_serve_restore();
    xtest_scratch_wipe(XUP_TEST_SCRATCH_ADG);
#else
    XPrintf("    [XUPGRADE_VERIFY_ON=0：防降级用例按能力裁剪空转]\n");
#endif /* XUPGRADE_VERIFY_ON */
    return ok;
}

/* ========================================================================== */
/*          用例 18：灰度三态判定（allow/deny/percent 边界，§11）                */
/* ========================================================================== */

/** @brief 固定 deviceKey 下搜索使 hash%100 恰为 bucketWanted 的盐值。 */
static bool xup_gray_find_salt(const char* deviceKey, uint32_t bucketWanted,
                               char* saltOut, size_t cap)
{
    int i;
    for (i = 0; i < 4096; ++i) {
        char salt[24];
        char joined[64];
        uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
        uint32_t bucket;
        XTEST_FMT(salt, sizeof(salt), "gs%d", i);
        if (xtest_str_len(salt) + xtest_str_len(deviceKey) + 1 > sizeof(joined)) return false;
        XMemcpy(joined, salt, xtest_str_len(salt) + 1);
        XMemcpy(joined + xtest_str_len(salt), deviceKey, xtest_str_len(deviceKey) + 1);
        if (!xtest_sha256((const uint8_t*)joined,
                          xtest_str_len(salt) + xtest_str_len(deviceKey), digest)) {
            return false;
        }
        bucket = ((uint32_t)digest[0] << 24) | ((uint32_t)digest[1] << 16) |
                 ((uint32_t)digest[2] << 8) | (uint32_t)digest[3];
        bucket %= 100u;
        if (bucket == bucketWanted) {
            return xtest_str_copy(saltOut, cap, salt);
        }
    }
    return false;
}

static bool t_gray_three_states(void)
{
    XUpgradeEngine* engine = NULL;
    XUpgradeConfig config;
    XUpgradeStateFile state;
    XByteArray* manifest = NULL;
    XUpgradeManifestGray gray;
    char comp[768];
    char deviceKey[XUPGRADE_DEVICE_KEY_MAX];
    char saltLo[24];
    char saltHi[24];
    char midJson[256];
    char exeDir[1024];
    char keyPath[1100];
    size_t i;
    bool keyFileExisted;
    bool ok = true;

    /* 灰度判定键确定性：注册序列号提供者 → deviceKey=SN（回退链第一级）。 */
    XUpgrade_setSerialProvider(xtest_serial_provider, NULL);
    XTEST_CHECK(XUpgradeIdentity_deviceKey(deviceKey, sizeof(deviceKey)));
    XTEST_CHECK(xtest_str_eq(deviceKey, "SN-XUP-E2E-001"));
    /* percent 边界盐：构造使 SHA256(salt||SN) 前 4 字节 mod 100 恰在 percent
     * 两侧（percent=50：bucket 49<50 为命中侧、bucket 50 为未命中侧）。 */
    XTEST_CHECK(xup_gray_find_salt(deviceKey, 50, saltLo, sizeof(saltLo)));
    XTEST_CHECK(xup_gray_find_salt(deviceKey, 49, saltHi, sizeof(saltHi)));
    if (!ok) {
        XPrintf("    [灰度边界盐搜索失败]\n");
        XUpgrade_setSerialProvider(NULL, NULL);
        return false;
    }
#if XUPGRADE_GRAY_ON
    /* 契约函数三态：allow 名单命中（percent=0 仍放行）→ deny 命中（percent=100
     * 仍拒绝）→ 分流哈希恰在 percent=50 两侧。 */
    XMemset(&gray, 0, sizeof(gray));
    gray.m_percent = 0;
    xtest_str_copy(gray.m_salt, XUPGRADE_SERIAL_MAX, "x");
    xtest_str_copy(gray.m_allow[0], XUPGRADE_SERIAL_MAX, deviceKey);
    gray.m_allowCount = 1;
    XTEST_CHECK(XUpgradeManifest_grayHit(&gray, deviceKey));
    XMemset(&gray, 0, sizeof(gray));
    gray.m_percent = 100;
    xtest_str_copy(gray.m_salt, XUPGRADE_SERIAL_MAX, "x");
    xtest_str_copy(gray.m_deny[0], XUPGRADE_SERIAL_MAX, deviceKey);
    gray.m_denyCount = 1;
    XTEST_CHECK(!XUpgradeManifest_grayHit(&gray, deviceKey));
    XMemset(&gray, 0, sizeof(gray));
    gray.m_percent = 50;
    xtest_str_copy(gray.m_salt, XUPGRADE_SERIAL_MAX, saltLo);
    XTEST_CHECK(!XUpgradeManifest_grayHit(&gray, deviceKey));   /* bucket 50：50<50 不成立 */
    xtest_str_copy(gray.m_salt, XUPGRADE_SERIAL_MAX, saltHi);
    XTEST_CHECK(XUpgradeManifest_grayHit(&gray, deviceKey));    /* bucket 49：49<50 命中 */
    gray.m_percent = 0;
    XTEST_CHECK(!XUpgradeManifest_grayHit(&gray, deviceKey));   /* percent=0 全拒 */
    XTEST_CHECK(!XUpgradeManifest_grayHit(&gray, NULL));        /* NULL 键按空串判定 */
    gray.m_percent = 100;
    XTEST_CHECK(XUpgradeManifest_grayHit(&gray, deviceKey));    /* percent=100 全放 */
    /* 引擎级：灰度未命中/名单拒绝 → CheckFailed（NoUpdate 语义）且判定可查。 */
    xtest_scratch_wipe(XUP_TEST_SCRATCH_GRAY);
    xup_case_app_component(comp, sizeof(comp), "2.0.0", NULL);
    XTEST_FMT(midJson, sizeof(midJson),
              "\"gray\":{\"percent\":50,\"salt\":\"%s\",\"allow\":[],\"deny\":[]},", saltLo);
    manifest = xup_case_manifest("2.0.0", "optional", midJson, comp);
    ok = ok && manifest != NULL;
    xup_case_serve(manifest, g_xup_pak, g_xup_pakName);
    engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    ok = ok && engine != NULL;
    if (engine) {
        XUpgradeEngine_setListener(engine, xup_case_listener, &g_xup_case_events);
        xtest_fill_config(&config, XUP_TEST_SCRATCH_GRAY);
        ok = ok && XUpgradeEngine_start(engine, &config);
        /* (a) percent 边界未命中侧 → 灰度 Miss。 */
        xup_case_events_reset();
        ok = ok && XUpgradeEngine_checkNow(engine);
        ok = ok && xtest_wait_until(xup_case_wait_check_failed, NULL);
        ok = ok && xtest_str_contains(g_xup_case_events.m_lastDetail, "灰度未命中");
        ok = ok && XUpgradeEngine_grayDecision(engine) == XUpgradeGrayDecision_Miss;
        ok = ok && g_xup_case_events.m_policySeen == 0;
        /* (b) 命中侧盐重签 → Hit（服务端改盐/percent 重传即放量，§11）。 */
        XClassDelete(manifest);
        XTEST_FMT(midJson, sizeof(midJson),
                  "\"gray\":{\"percent\":50,\"salt\":\"%s\",\"allow\":[],\"deny\":[]},",
                  saltHi);
        manifest = xup_case_manifest("2.0.0", "optional", midJson, comp);
        ok = ok && manifest != NULL;
        xup_case_serve(manifest, g_xup_pak, g_xup_pakName);
        xup_case_events_reset();
        ok = ok && XUpgradeEngine_checkNow(engine);
        ok = ok && xtest_wait_until(xup_case_wait_update_available, NULL);
        ok = ok && g_xup_case_events.m_lastGray == (int)XUpgradeGrayDecision_Hit;
        ok = ok && XUpgradeEngine_grayDecision(engine) == XUpgradeGrayDecision_Hit;
        /* (c) allow 名单命中：percent=0 仍放行（allow 优先于分流）。 */
        XClassDelete(manifest);
        XTEST_FMT(midJson, sizeof(midJson),
                  "\"gray\":{\"percent\":0,\"salt\":\"x\",\"allow\":[\"%s\"],\"deny\":[]},",
                  deviceKey);
        manifest = xup_case_manifest("2.0.0", "optional", midJson, comp);
        ok = ok && manifest != NULL;
        xup_case_serve(manifest, g_xup_pak, g_xup_pakName);
        xup_case_events_reset();
        ok = ok && XUpgradeEngine_checkNow(engine);
        ok = ok && xtest_wait_until(xup_case_wait_update_available, NULL);
        ok = ok && g_xup_case_events.m_lastGray == (int)XUpgradeGrayDecision_AllowList;
        /* (d) deny 名单拒绝：percent=100 仍拒绝 → "暂无更新（灰度名单拒绝）"。 */
        XClassDelete(manifest);
        XTEST_FMT(midJson, sizeof(midJson),
                  "\"gray\":{\"percent\":100,\"salt\":\"x\",\"allow\":[],\"deny\":[\"%s\"]},",
                  deviceKey);
        manifest = xup_case_manifest("2.0.0", "optional", midJson, comp);
        ok = ok && manifest != NULL;
        xup_case_serve(manifest, g_xup_pak, g_xup_pakName);
        xup_case_events_reset();
        ok = ok && XUpgradeEngine_checkNow(engine);
        ok = ok && xtest_wait_until(xup_case_wait_check_failed, NULL);
        ok = ok && xtest_str_contains(g_xup_case_events.m_lastDetail, "灰度名单拒绝");
        ok = ok && XUpgradeEngine_grayDecision(engine) == XUpgradeGrayDecision_DenyList;
        /* 未命中不重复请求：判定落盘语义=本地记录，引擎停在 CheckFailed。 */
        ok = ok && xtest_load_state(XUP_TEST_SCRATCH_GRAY, &state);
        ok = ok && state.m_state == XUpgradeState_CheckFailed;
        XUpgradeEngine_stop(engine);
        XClassDelete((XClass*)engine);
    }
    if (manifest) XClassDelete(manifest);
    manifest = NULL;
    xup_case_serve_restore();
    xtest_scratch_wipe(XUP_TEST_SCRATCH_GRAY);
#else
    XPrintf("    [XUPGRADE_GRAY_ON=0：灰度用例按能力裁剪空转]\n");
#endif /* XUPGRADE_GRAY_ON */

    /* deviceKey 回退链（XUpgradeIdentity.c:317）：SN 注销 → 主网卡 MAC
     * （12 位小写 hex）→ 持久 UUID（device.key）。MAC 级以库自身接口枚举
     * 为准绳做真实路径断言（存在可用网卡时 deviceKey 必须等于其规范化
     * MAC）；无可用网卡时按全链不可用口径断言稳定返回 false。 */
    XUpgrade_setSerialProvider(NULL, NULL);
    {
        size_t lastSlash = 0;
        bool hasSlash = false;
        char k1[XUPGRADE_DEVICE_KEY_MAX];
        char k2[XUPGRADE_DEVICE_KEY_MAX];
        bool keyOk;
        bool macEligible = false;
        char expectMac[16];
        (void)xtest_str_copy(exeDir, sizeof(exeDir), "");
        {
            char exeFile[1024];
            ok = ok && XSystem_executableFilePath(exeFile, sizeof(exeFile));
            for (i = 0; exeFile[i] != '\0'; ++i)
                if (exeFile[i] == '/' || exeFile[i] == '\\') {
                    lastSlash = (size_t)i;
                    hasSlash = true;
                }
            ok = ok && hasSlash;
            if (ok) {
                XMemcpy(exeDir, exeFile, lastSlash);
                exeDir[lastSlash] = '\0';
            }
        }
        /* 以库内抽象枚举网卡（Drive 平台适配在其下），预判 MAC 级可用性。 */
        {
            XVector* interfaces = XNetworkInterface_allInterfaces();
            int64_t count = interfaces ? (int64_t)XVector_size_base((const XContainer*)interfaces) : 0;
            int64_t idx;
            for (idx = 0; idx < count && !macEligible; ++idx) {
                const XNetworkInterface* iface =
                    (const XNetworkInterface*)XVector_at_base(interfaces, idx);
                const XString* hw = iface ? XNetworkInterface_hardwareAddress_const(iface) : NULL;
                const char* utf8 = hw ? XString_toUtf8(hw) : NULL;
                uint32_t written = 0;
                size_t j;
                if (!iface || !XNetworkInterface_isValid(iface) ||
                    XNetworkInterface_isLoopBack(iface) ||
                    !XNetworkInterface_isUp(iface) ||
                    !XNetworkInterface_isRunning(iface) || !utf8) {
                    continue;
                }
                for (j = 0; utf8[j] != '\0'; ++j) {
                    char c = utf8[j];
                    int value;
                    if (c >= '0' && c <= '9') value = c - '0';
                    else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
                    else continue;
                    if (written >= 12u) { written = 0; break; }
                    expectMac[written++] = "0123456789abcdef"[value];
                }
                if (written == 12u) {
                    expectMac[12] = '\0';
                    macEligible = true;
                }
            }
            if (interfaces) XClassDelete(interfaces);
        }
        XTEST_FMT(keyPath, sizeof(keyPath), "%s/" XUPGRADE_FILE_DEVICE_KEY, exeDir);
        keyFileExisted = xtest_path_exists(keyPath);
        keyOk = XUpgradeIdentity_deviceKey(k1, sizeof(k1));
        if (macEligible) {
            /* 存在可用网卡：MAC 级必须命中（回退链第二级，真实平台适配）。 */
            ok = ok && keyOk;
            ok = ok && XUpgradeIdentity_deviceKey(k2, sizeof(k2));
            ok = ok && xtest_str_eq(k1, expectMac) && xtest_str_eq(k1, k2);
            if (!xtest_str_eq(k1, expectMac))
                XPrintf("    [deviceKey=%s 期望主网卡 MAC=%s]\n", k1, expectMac);
        } else {
            /* 无可用网卡：落 UUID 级（device.key 持久化）或全链不可用口径；
             * 两级都不硬造，仅断言契约面（成功=键稳定且白名单内；失败=稳定
             * 失败，灰度按未命中，XUpgradeIdentity.h:133）。 */
            bool secondOk = XUpgradeIdentity_deviceKey(k2, sizeof(k2));
            ok = ok && secondOk == keyOk; /* 两次调用结论一致（键稳定语义） */
            if (keyOk) {
                bool shapeOk = true;
                size_t n = xtest_str_len(k1);
                if (n != 36) shapeOk = false; /* UUID 形态 8-4-4-4-12 */
                else {
                    static const size_t dashAt[4] = { 8, 13, 18, 23 };
                    for (i = 0; i < 4; ++i)
                        if (k1[dashAt[i]] != '-') shapeOk = false;
                    for (i = 0; i < n; ++i) {
                        char c = k1[i];
                        if (c == '-') continue;
                        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) shapeOk = false;
                    }
                }
                ok = ok && xtest_str_eq(k1, k2) && shapeOk;
                if (!shapeOk) XPrintf("    [deviceKey UUID 形态异常：%s]\n", k1);
            } else {
                XPrintf("    [deviceKey 全链不可用（本宿主无可用网卡且 UUID 级不可得），按未命中口径]\n");
            }
        }
        /* device.key 属安装根直下（非 update/ 工作区）：本用例新产生则清理。 */
        if (!keyFileExisted && xtest_path_exists(keyPath)) {
            XString* p = XString_create_utf8(keyPath);
            if (p) {
                (void)XDir_removePath_static(p); /* 仅删本用例首次生成的 device.key */
                XClassDelete(p);
            }
        }
    }
    /* 恢复进程内身份基线：serial 未注册（与既有用例结束时一致）。 */
    XUpgrade_setSerialProvider(NULL, NULL);
    return ok;
}

/* ========================================================================== */
/*     用例 19：策略时机（forced 自动应用/silent 维护窗口/optional 跳过重置）      */
/* ========================================================================== */

/** @brief 以当日分钟数填充维护窗口配置（支持跨午夜形态）。 */
static void xup_case_fill_window(XUpgradeApplyWindow* w, int startTotalMin, int endTotalMin)
{
    XMemset(w, 0, sizeof(*w));
    w->m_enabled = true;
    w->m_startHour = (uint8_t)(((startTotalMin % 1440) + 1440) % 1440 / 60);
    w->m_startMinute = (uint8_t)(((startTotalMin % 1440) + 1440) % 1440 % 60);
    w->m_endHour = (uint8_t)(((endTotalMin % 1440) + 1440) % 1440 / 60);
    w->m_endMinute = (uint8_t)(((endTotalMin % 1440) + 1440) % 1440 % 60);
}

/** @brief 当前墙钟的当日分钟数（XDateTime_currentDateTime，禁单调毫秒拼时刻）。 */
static int xup_case_now_minutes(void)
{
    XDateTime now = XDateTime_currentDateTime();
    XTime t = XDateTime_time(&now);
    return XTime_hour(&t) * 60 + XTime_minute(&t);
}

static bool t_policy_timing_flows(void)
{
    XUpgradeEngine* engine = NULL;
    XUpgradeConfig config;
    XUpgradeApplyWindow window;
    XUpgradeStateFile state;
    XByteArray* manifest = NULL;
    char comp[768];
    bool ok = true;

    /* 模块入口（init 前）缺省口径：窗口关闭/宽限 0/灰度未判定。 */
    XTEST_CHECK(!XUpgrade_applyWindow().m_enabled);
    XTEST_CHECK(XUpgrade_forcedGraceHours() == 0);
    XTEST_CHECK(XUpgrade_grayDecision() == XUpgradeGrayDecision_NotEvaluated);

    xtest_scratch_wipe(XUP_TEST_SCRATCH_POL);
    xup_case_app_component(comp, sizeof(comp), "2.0.0", NULL);

    /* (a) forced：staged 完成即 ReadyAutoApply，宽限期=manifest forced_grace_hours；
     *     本地覆盖经引擎入口生效（模块级 setTrustKey/setForcedGraceHours 六入口
     *     中三个 setter 未接线，见交付说明，此处走透传位）。
     *     契约点直测：forced_grace_hours 必须入解析（uint32，缺省 0）。 */
    xup_case_events_reset();
    manifest = xup_case_manifest("2.0.0", "forced",
                                 "\"forced_grace_hours\":7,", comp);
    XTEST_CHECK(manifest != NULL);
    {
        XUpgradeManifest parsed;
        XTEST_CHECK(XUpgradeManifest_parse(&parsed, (const char*)xtest_bdata(manifest),
                                           xtest_bsize(manifest)));
        XTEST_CHECK(parsed.m_forcedGraceHours == 7); /* 解析契约（见交付说明缺陷） */
    }
    xup_case_serve(manifest, g_xup_pak, g_xup_pakName);
    engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XTEST_CHECK(engine != NULL);
    if (engine) {
        XUpgradeEngine_setListener(engine, xup_case_listener, &g_xup_case_events);
        xtest_fill_config(&config, XUP_TEST_SCRATCH_POL);
        XTEST_CHECK(XUpgradeEngine_start(engine, &config));
        XTEST_CHECK(XUpgradeEngine_checkNow(engine));
        XTEST_CHECK(xtest_wait_until(xup_case_wait_update_available, NULL));
        XTEST_CHECK(g_xup_case_events.m_lastPolicy == (int)XUpgradePolicy_Forced);
        XTEST_CHECK(xtest_wait_until(xup_case_wait_ready_auto_apply, NULL));
        XTEST_CHECK(xtest_str_contains(g_xup_case_events.m_lastDetail, "宽限期 7 小时"));
        XTEST_CHECK(g_xup_case_events.m_byType[XUpgradeEventType_ReadyNeedRestart] == 0);
        /* 宽限期本地覆盖：覆盖值即查即生效，下一检查周期随事件携带。 */
        XUpgradeEngine_setForcedGraceHours(engine, 2);
        XTEST_CHECK(XUpgradeEngine_forcedGraceHours(engine) == 2);
        xup_case_events_reset();
        XTEST_CHECK(XUpgradeEngine_checkNow(engine));
        XTEST_CHECK(xtest_wait_until(xup_case_wait_ready_auto_apply, NULL));
        XTEST_CHECK(xtest_str_contains(g_xup_case_events.m_lastDetail, "宽限期 2 小时"));
        /* 哨兵复位：恢复跟随 manifest。 */
        XUpgradeEngine_setForcedGraceHours(engine, XUPGRADE_FORCED_GRACE_FROM_MANIFEST);
        XTEST_CHECK(XUpgradeEngine_forcedGraceHours(engine) == 7);
        XUpgradeEngine_stop(engine);
        XClassDelete((XClass*)engine);
    }
    if (manifest) XClassDelete(manifest);
    manifest = NULL;
    xtest_scratch_wipe(XUP_TEST_SCRATCH_POL);

    /* (b) silent 在维护窗口内：ReadyAutoApply（窗口判定用墙钟）。 */
    xup_case_events_reset();
    manifest = xup_case_manifest("2.0.0", "silent", NULL, comp);
    XTEST_CHECK(manifest != NULL);
    xup_case_serve(manifest, g_xup_pak, g_xup_pakName);
    engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XTEST_CHECK(engine != NULL);
    if (engine) {
        int now = xup_case_now_minutes();
        XUpgradeEngine_setListener(engine, xup_case_listener, &g_xup_case_events);
        xtest_fill_config(&config, XUP_TEST_SCRATCH_POL);
        /* [now-1min, now+30min) 必含当前分钟（跨午夜形态自然成立）。运行期
         * setter 在 start 之后调用：引擎级入口无 init 前暂存（模块级六入口
         * 尚未接线，见交付说明），start 会以 config.m_applyWindow 覆盖。 */
        xup_case_fill_window(&window, now - 1, now + 30);
        XTEST_CHECK(XUpgradeEngine_start(engine, &config));
        XUpgradeEngine_setApplyWindow(engine, &window);
        {
            XUpgradeApplyWindow back = XUpgradeEngine_applyWindow(engine);
            XTEST_CHECK(back.m_enabled && back.m_startHour == window.m_startHour &&
                        back.m_startMinute == window.m_startMinute &&
                        back.m_endHour == window.m_endHour &&
                        back.m_endMinute == window.m_endMinute); /* 值拷贝回读 */
        }
        XTEST_CHECK(XUpgradeEngine_checkNow(engine));
        XTEST_CHECK(xtest_wait_until(xup_case_wait_update_available, NULL));
        XTEST_CHECK(g_xup_case_events.m_lastPolicy == (int)XUpgradePolicy_Silent);
        XTEST_CHECK(xtest_wait_until(xup_case_wait_ready_auto_apply, NULL));
        XTEST_CHECK(xtest_str_contains(g_xup_case_events.m_lastDetail, "silent"));
        XTEST_CHECK(g_xup_case_events.m_byType[XUpgradeEventType_ReadyNeedRestart] == 0);
        /* NULL=关闭窗口。 */
        XUpgradeEngine_setApplyWindow(engine, NULL);
        XTEST_CHECK(!XUpgradeEngine_applyWindow(engine).m_enabled);
        XUpgradeEngine_stop(engine);
        XClassDelete((XClass*)engine);
    }
    if (manifest) XClassDelete(manifest);
    manifest = NULL;
    xtest_scratch_wipe(XUP_TEST_SCRATCH_POL);

    /* (c) silent 窗口外：ReadyNeedRestart（错过顺延，不抢重启）。窗口把未来
     *     5 分钟排除在外，流水秒级完成，断言窗口外不触发自动应用。 */
    xup_case_events_reset();
    manifest = xup_case_manifest("2.0.0", "silent", NULL, comp);
    XTEST_CHECK(manifest != NULL);
    xup_case_serve(manifest, g_xup_pak, g_xup_pakName);
    engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XTEST_CHECK(engine != NULL);
    if (engine) {
        int now = xup_case_now_minutes();
        XUpgradeEngine_setListener(engine, xup_case_listener, &g_xup_case_events);
        xtest_fill_config(&config, XUP_TEST_SCRATCH_POL);
        xup_case_fill_window(&window, now + 5, now); /* 排除 [now, now+5min) */
        config.m_applyWindow = window; /* init 装载路径（XUpgradeConfig 契约字段） */
        XTEST_CHECK(XUpgradeEngine_start(engine, &config));
        XTEST_CHECK(XUpgradeEngine_checkNow(engine));
        XTEST_CHECK(xtest_wait_until(xup_case_wait_update_available, NULL));
        XTEST_CHECK(xtest_wait_until(xup_case_wait_need_restart, NULL));
        XTEST_CHECK(g_xup_case_events.m_byType[XUpgradeEventType_ReadyAutoApply] == 0);
        XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_POL, &state));
        ok = ok && state.m_state == XUpgradeState_Ready; /* staged 待窗口 */
        XUpgradeEngine_stop(engine);
        XClassDelete((XClass*)engine);
    }
    if (manifest) XClassDelete(manifest);
    manifest = NULL;
    xtest_scratch_wipe(XUP_TEST_SCRATCH_POL);

    /* (d) optional 跳过表跨版本重置：本地 3.0.0（含 skip app@2.0.0）→ 检查
     *     2.0.0 → 暂无更新且过期 skip 记录被修剪（下一版本自动重置，§10）。 */
    {
        XUpgradeStateSkip* s;
        XUpgradeStateFile seed;
        xup_case_events_reset();
        manifest = xup_case_manifest("2.0.0", "optional", NULL, comp);
        XTEST_CHECK(manifest != NULL);
        xup_case_serve(manifest, g_xup_pak, g_xup_pakName);
        XUpgradeStateFile_clear(&seed);
        seed.m_state = XUpgradeState_Active;
        xtest_str_copy(seed.m_components[0].m_id, XUPGRADE_COMPONENT_ID_MAX, "app");
        xtest_str_copy(seed.m_components[0].m_activeVersion, XUPGRADE_VERSION_MAX, "3.0.0");
        xtest_str_copy(seed.m_components[0].m_maxVersion, XUPGRADE_VERSION_MAX, "3.0.0");
        seed.m_componentCount = 1;
        s = &seed.m_skips[0];
        xtest_str_copy(s->m_componentId, XUPGRADE_COMPONENT_ID_MAX, "app");
        xtest_str_copy(s->m_version, XUPGRADE_VERSION_MAX, "2.0.0");
        seed.m_skipCount = 1;
        XTEST_CHECK(xtest_save_state(XUP_TEST_SCRATCH_POL, &seed));
        XTEST_CHECK(xtest_seed_version_dir(XUP_TEST_SCRATCH_POL, "app", "3.0.0"));
        XTEST_CHECK(xtest_write_pointer(XUP_TEST_SCRATCH_POL, "app", "3.0.0"));
        engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XTEST_CHECK(engine != NULL);
        if (engine) {
            XUpgradeEngine_setListener(engine, xup_case_listener, &g_xup_case_events);
            xtest_fill_config(&config, XUP_TEST_SCRATCH_POL);
            XTEST_CHECK(XUpgradeEngine_start(engine, &config));
            xup_case_events_reset();
            XTEST_CHECK(XUpgradeEngine_checkNow(engine));
            XTEST_CHECK(xtest_wait_until(xup_case_wait_check_failed, NULL));
            XTEST_CHECK(xtest_str_eq(g_xup_case_events.m_lastDetail, "暂无更新"));
            XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_POL, &state));
            ok = ok && state.m_skipCount == 0; /* 跳过记录已跨版本重置 */
            XUpgradeEngine_stop(engine);
            XClassDelete((XClass*)engine);
        }
        if (manifest) XClassDelete(manifest);
        manifest = NULL;
        xtest_scratch_wipe(XUP_TEST_SCRATCH_POL);
        /* (e) 跳过仅对被跳过版本生效：3.0.1 在列时 2.0.0 的旧 skip 不拦新版本。 */
        {
            static const uint8_t mark331[] = "3.0.1";
            XTestPakEntry entry;
            XByteArray* pak301 = NULL;
            char shaHex[XUPGRADE_SHA256_HEX_MAX];
            XMemset(&entry, 0, sizeof(entry));
            entry.m_name = XUPGRADE_FILE_VERSION_MARK;
            entry.m_flags = 0;
            entry.m_data = mark331;
            entry.m_len = 5;
            XTEST_CHECK(xtest_build_pak(&entry, 1, &pak301, NULL));
            XTEST_CHECK(xtest_pak_sha256_hex(pak301, shaHex, sizeof(shaHex)));
            XTEST_FMT(comp, sizeof(comp),
                      "{\"id\":\"app\",\"os\":\"win64\",\"version\":\"3.0.1\","
                      "\"file\":\"app-3.0.1-win64.pak\",\"size\":%llu,\"sha256\":\"%s\"}",
                      (unsigned long long)(pak301 ? xtest_bsize(pak301) : 0), shaHex);
            manifest = xup_case_manifest("3.0.1", "optional", NULL, comp);
            XTEST_CHECK(manifest != NULL);
            xup_case_serve(manifest, pak301, "app-3.0.1-win64.pak");
            /* (e) 自备种子：active=3.0.0 + 旧 skip{2.0.0}。(d) 收尾清空了
             *     scratch，不重播种子则引擎按全新设备（运行版本 1.0.0 基线）
             *     检查，"旧 skip 保留"断言失去前提。检查入口的首次落盘在
             *     修剪循环之前（set_state(Checking)），断言点旧 skip 仍在。 */
            {
                XUpgradeStateSkip* es;
                XUpgradeStateFile seed;
                XUpgradeStateFile_clear(&seed);
                seed.m_state = XUpgradeState_Active;
                xtest_str_copy(seed.m_components[0].m_id, XUPGRADE_COMPONENT_ID_MAX, "app");
                xtest_str_copy(seed.m_components[0].m_activeVersion, XUPGRADE_VERSION_MAX, "3.0.0");
                xtest_str_copy(seed.m_components[0].m_maxVersion, XUPGRADE_VERSION_MAX, "3.0.0");
                seed.m_componentCount = 1;
                es = &seed.m_skips[0];
                xtest_str_copy(es->m_componentId, XUPGRADE_COMPONENT_ID_MAX, "app");
                xtest_str_copy(es->m_version, XUPGRADE_VERSION_MAX, "2.0.0");
                seed.m_skipCount = 1;
                XTEST_CHECK(xtest_save_state(XUP_TEST_SCRATCH_POL, &seed));
            }
            xup_case_events_reset();
            engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
            XTEST_CHECK(engine != NULL);
            if (engine) {
                XUpgradeEngine_setListener(engine, xup_case_listener,
                                           &g_xup_case_events);
                xtest_fill_config(&config, XUP_TEST_SCRATCH_POL);
                XTEST_CHECK(XUpgradeEngine_start(engine, &config));
                XTEST_CHECK(XUpgradeEngine_checkNow(engine));
                XTEST_CHECK(xtest_wait_until(xup_case_wait_update_available, NULL));
                XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_POL, &state));
                ok = ok && state.m_skipCount == 1; /* 旧 skip 对新版本无效且保留 */
                XUpgradeEngine_stop(engine);
                XClassDelete((XClass*)engine);
            }
            if (pak301) XClassDelete(pak301);
        }
        if (manifest) XClassDelete(manifest);
        manifest = NULL;
    }
    xup_case_serve_restore();
    xtest_scratch_wipe(XUP_TEST_SCRATCH_POL);
    return ok;
}

/* ========================================================================== */
/*      用例 20：fontres 组件线（双组件依赖序/requires 整单拒绝/res 自检抽查）    */
/* ========================================================================== */

static bool t_component_line_flows(void)
{
    XUpgradeEngine* engine = NULL;
    XUpgradeConfig config;
    XUpgradeStateFile state;
    XByteArray* manifest = NULL;
    char comp[1024];
    char fres[768];
    char pointer[XUPGRADE_VERSION_MAX + 4];
    bool ok = true;

    xtest_scratch_wipe(XUP_TEST_SCRATCH_COMP);
    xup_case_app_component(comp, sizeof(comp), "2.0.0", ",\"requires\":{\"fontres\":\">=2.0\"}");
    xup_case_fontres_component(fres, sizeof(fres), "2.0.0");
    XTEST_FMT(comp, sizeof(comp), "%s,%s", comp, fres);

    /* (a) 双组件顺序下载激活：下载按依赖序（fontres 先于 app），双双 staged →
     *     桩逐组件切指针 → 自检 Active（fontres 首装收编）。 */
    xup_case_events_reset();
    manifest = xup_case_manifest("2.0.0", "optional", NULL, comp);
    XTEST_CHECK(manifest != NULL);
    xup_case_serve(manifest, g_xup_pak, g_xup_pakName);
    engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XTEST_CHECK(engine != NULL);
    if (engine) {
        XUpgradeEngine_setListener(engine, xup_case_listener, &g_xup_case_events);
        xtest_fill_config(&config, XUP_TEST_SCRATCH_COMP);
        XTEST_CHECK(XUpgradeEngine_start(engine, &config));
        XTEST_CHECK(XUpgradeEngine_checkNow(engine));
        XTEST_CHECK(xtest_wait_until(xup_case_wait_update_available, NULL));
        XTEST_CHECK(XUpgradeEngine_startUpgrade(engine));
        XTEST_CHECK(xtest_wait_until(xup_case_wait_need_restart, NULL));
        /* 下载依赖序：fontres（被依赖）先于 app。 */
        XTEST_CHECK(g_xup_case_events.m_dlOrderCount == 2);
        if (g_xup_case_events.m_dlOrderCount == 2) {
            XTEST_CHECK(xtest_str_eq(g_xup_case_events.m_dlOrder[0], "fontres"));
            XTEST_CHECK(xtest_str_eq(g_xup_case_events.m_dlOrder[1], "app"));
        } else {
            XPrintf("    [下载组件序数量=%d]\n", g_xup_case_events.m_dlOrderCount);
        }
        XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_COMP, &state));
        XTEST_CHECK(state.m_componentCount == 2 && state.m_components[0].m_staged &&
                    state.m_components[1].m_staged);
        XUpgradeEngine_stop(engine);
        XClassDelete((XClass*)engine);
        /* 桩逐组件激活（app+fontres）。 */
        {
            int rc = -1;
            XTEST_CHECK(xup_run_stub(XUP_TEST_SCRATCH_COMP, &rc));
            XTEST_CHECK(rc == 0);
            XTEST_CHECK(xtest_read_pointer(XUP_TEST_SCRATCH_COMP, "app", pointer,
                                           sizeof(pointer)));
            XTEST_CHECK(xtest_str_eq(pointer, "2.0.0"));
            XTEST_CHECK(xtest_read_pointer(XUP_TEST_SCRATCH_COMP, "fontres", pointer,
                                           sizeof(pointer)));
            XTEST_CHECK(xtest_str_eq(pointer, "2.0.0"));
        }
        /* 自检：双线过 → Active；fontres 首装收编为激活版本。 */
        xup_case_events_reset();
        engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XTEST_CHECK(engine != NULL);
        if (engine) {
            XUpgradeEngine_setListener(engine, xup_case_listener, &g_xup_case_events);
            XTEST_CHECK(XUpgradeEngine_start(engine, &config));
            XTEST_CHECK(XUpgradeEngine_selfCheck(engine));
            XTEST_CHECK(XUpgradeEngine_state(engine) == XUpgradeState_Active);
            XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_COMP, &state));
            XTEST_CHECK(xtest_str_eq(state.m_components[0].m_activeVersion, "2.0.0"));
            XTEST_CHECK(xtest_str_eq(state.m_components[1].m_activeVersion, "2.0.0"));
            /* (c) res 线自检抽查：破坏 fontres 版本标记 → 该线回滚计数+1 且
             *     上报 RolledBack（证明 fontres 目录确在抽查范围）。 */
            {
                char markPath[1400];
                XTEST_FMT(markPath, sizeof(markPath),
                          "%s/" XUPGRADE_DIR_VERSIONS "/fontres/2.0.0/"
                          XUPGRADE_FILE_VERSION_MARK, XUP_TEST_SCRATCH_COMP);
                XTEST_CHECK(xtest_write_file(markPath, "9.9.9", 5));
            }
            xup_case_events_reset();
            XTEST_CHECK(XUpgradeEngine_selfCheck(engine));
            XTEST_CHECK(g_xup_case_events.m_byType[XUpgradeEventType_RolledBack] >= 1);
            XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_COMP, &state));
            XTEST_CHECK(state.m_components[1].m_rollbackCount == 1);
            XUpgradeEngine_stop(engine);
            XClassDelete((XClass*)engine);
        }
    }
    if (manifest) XClassDelete(manifest);
    manifest = NULL;
    xup_case_serve_restore();
    xtest_scratch_wipe(XUP_TEST_SCRATCH_COMP);

    /* (b) requires 不满足 → 整单拒绝：staging 完成后配套校验失败，半成品清理
     *     回 Idle，staged 标记不落盘（不变式 1）。 */
    xup_case_events_reset();
    xup_case_app_component(comp, sizeof(comp), "2.0.0",
                           ",\"requires\":{\"fontres\":\">=99.0\"}");
    xup_case_fontres_component(fres, sizeof(fres), "2.0.0");
    XTEST_FMT(comp, sizeof(comp), "%s,%s", comp, fres);
    manifest = xup_case_manifest("2.0.0", "optional", NULL, comp);
    XTEST_CHECK(manifest != NULL);
    xup_case_serve(manifest, g_xup_pak, g_xup_pakName);
    engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XTEST_CHECK(engine != NULL);
    if (engine) {
        XUpgradeEngine_setListener(engine, xup_case_listener, &g_xup_case_events);
        xtest_fill_config(&config, XUP_TEST_SCRATCH_COMP);
        XTEST_CHECK(XUpgradeEngine_start(engine, &config));
        XTEST_CHECK(XUpgradeEngine_checkNow(engine));
        XTEST_CHECK(xtest_wait_until(xup_case_wait_update_available, NULL));
        XTEST_CHECK(XUpgradeEngine_startUpgrade(engine));
        XTEST_CHECK(xtest_wait_until(xup_case_wait_detail_contains,
                                     (void*)"requires"));
        XTEST_CHECK(XUpgradeEngine_state(engine) == XUpgradeState_Idle);
        XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_COMP, &state));
        XTEST_CHECK(state.m_state == XUpgradeState_Idle);
        XTEST_CHECK(!(state.m_components[0].m_staged || state.m_components[1].m_staged));
        /* 整单不激活的真路径收口：桩读不到 staged 标记 → 不切任何指针。 */
        {
            int rc = -1;
            XTEST_CHECK(xup_run_stub(XUP_TEST_SCRATCH_COMP, &rc));
            XTEST_CHECK(rc == 0);
            XTEST_CHECK(!xtest_path_exists(XUP_TEST_SCRATCH_COMP "/"
                                           XUPGRADE_DIR_POINTERS "/app"));
            XTEST_CHECK(!xtest_path_exists(XUP_TEST_SCRATCH_COMP "/"
                                           XUPGRADE_DIR_POINTERS "/fontres"));
        }
        XUpgradeEngine_stop(engine);
        XClassDelete((XClass*)engine);
    }
    if (manifest) XClassDelete(manifest);
    xup_case_serve_restore();
    xtest_scratch_wipe(XUP_TEST_SCRATCH_COMP);
    return ok;
}

/* ========================================================================== */
/*   用例 21：A/B 后端宿主模拟（写槽/仲裁/confirmBoot/矩阵 8-11 进程内近似）      */
/* ========================================================================== */

#if XUPGRADE_AB_ON

#define XUP_AB_RAM_SLOT_BYTES (256u * 1024u)
#define XUP_AB_RAM_IMG_BYTES  (64u * 1024u + 7u)   /* 非整块尺寸覆盖尾块回读 */

/** @brief RAM 分区后端存储（用例私有；仅本用例生命周期内使用）。 */
static uint8_t xup_ab_ramSlot[2][XUP_AB_RAM_SLOT_BYTES];
static uint8_t xup_ab_ramMeta[2][XUPGRADE_BOOT_META_SIZE];
static volatile int xup_ab_ramReboots;
static volatile int xup_ab_ramLastRebootMode;

static void xup_ab_ram_reset(void)
{
    XMemset(xup_ab_ramSlot, 0xFF, sizeof(xup_ab_ramSlot));
    XMemset(xup_ab_ramMeta, 0xFF, sizeof(xup_ab_ramMeta));
    xup_ab_ramReboots = 0;
    xup_ab_ramLastRebootMode = -1;
}

static XUpgradeError xup_ab_ram_slotCount(void* userData, uint32_t* outCount)
{
    (void)userData;
    if (!outCount) return XUpgradeError_InvalidArgument;
    *outCount = 2;
    return XUpgradeError_Ok;
}

static XUpgradeError xup_ab_ram_slotSize(void* userData, uint32_t slotIndex,
                                         uint64_t* outSize)
{
    (void)userData;
    if (!outSize || slotIndex >= 2u) return XUpgradeError_InvalidArgument;
    *outSize = XUP_AB_RAM_SLOT_BYTES;
    return XUpgradeError_Ok;
}

static XUpgradeError xup_ab_ram_read(void* userData, uint32_t slotIndex, uint64_t offset,
                                     uint8_t* out, uint32_t len)
{
    (void)userData;
    if (slotIndex >= 2u || offset > XUP_AB_RAM_SLOT_BYTES) return XUpgradeError_InvalidArgument;
    if (len == 0) return XUpgradeError_Ok;
    if (!out || offset + (uint64_t)len > XUP_AB_RAM_SLOT_BYTES) return XUpgradeError_InvalidArgument;
    XMemcpy(out, &xup_ab_ramSlot[slotIndex][offset], len);
    return XUpgradeError_Ok;
}

static XUpgradeError xup_ab_ram_write(void* userData, uint32_t slotIndex, uint64_t offset,
                                      const uint8_t* data, uint32_t len)
{
    (void)userData;
    if (slotIndex >= 2u || offset > XUP_AB_RAM_SLOT_BYTES) return XUpgradeError_InvalidArgument;
    if (len == 0) return XUpgradeError_Ok;
    if (!data || offset + (uint64_t)len > XUP_AB_RAM_SLOT_BYTES) return XUpgradeError_InvalidArgument;
    XMemcpy(&xup_ab_ramSlot[slotIndex][offset], data, len);
    return XUpgradeError_Ok;
}

static XUpgradeError xup_ab_ram_erase(void* userData, uint32_t slotIndex, uint64_t offset,
                                      uint32_t len)
{
    (void)userData;
    if (slotIndex >= 2u || offset > XUP_AB_RAM_SLOT_BYTES) return XUpgradeError_InvalidArgument;
    if (len == 0) return XUpgradeError_Ok;
    if (offset + (uint64_t)len > XUP_AB_RAM_SLOT_BYTES) return XUpgradeError_InvalidArgument;
    XMemset(&xup_ab_ramSlot[slotIndex][offset], 0xFF, len);
    return XUpgradeError_Ok;
}

static XUpgradeError xup_ab_ram_metaRead(void* userData, uint32_t metaIndex,
                                         XUpgradeBootMeta* out)
{
    const uint8_t* buf;
    (void)userData;
    if (!out || metaIndex >= XUPGRADE_BOOT_META_COPIES) return XUpgradeError_InvalidArgument;
    buf = xup_ab_ramMeta[metaIndex];
    out->m_magic = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                   ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
    out->m_crc32 = (uint32_t)buf[4] | ((uint32_t)buf[5] << 8) |
                   ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 24);
    out->m_activeSlot = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) |
                        ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);
    out->m_requestedSlot = (uint32_t)buf[12] | ((uint32_t)buf[13] << 8) |
                           ((uint32_t)buf[14] << 16) | ((uint32_t)buf[15] << 24);
    out->m_bootAttempts = (uint32_t)buf[16] | ((uint32_t)buf[17] << 8) |
                          ((uint32_t)buf[18] << 16) | ((uint32_t)buf[19] << 24);
    out->m_maxAttempts = (uint32_t)buf[20] | ((uint32_t)buf[21] << 8) |
                         ((uint32_t)buf[22] << 16) | ((uint32_t)buf[23] << 24);
    XMemcpy(out->m_imageSha256, buf + 24, XUPGRADE_SHA256_BIN_SIZE);
    out->m_sequence = (uint32_t)buf[56] | ((uint32_t)buf[57] << 8) |
                      ((uint32_t)buf[58] << 16) | ((uint32_t)buf[59] << 24);
    return XUpgradeError_Ok;
}

static XUpgradeError xup_ab_ram_metaWrite(void* userData, uint32_t metaIndex,
                                          const XUpgradeBootMeta* meta)
{
    uint8_t* buf;
    (void)userData;
    if (!meta || metaIndex >= XUPGRADE_BOOT_META_COPIES) return XUpgradeError_InvalidArgument;
    buf = xup_ab_ramMeta[metaIndex];
    XMemset(buf, 0, XUPGRADE_BOOT_META_SIZE);
    buf[0] = (uint8_t)(meta->m_magic & 0xFFu);
    buf[1] = (uint8_t)((meta->m_magic >> 8) & 0xFFu);
    buf[2] = (uint8_t)((meta->m_magic >> 16) & 0xFFu);
    buf[3] = (uint8_t)((meta->m_magic >> 24) & 0xFFu);
    buf[4] = (uint8_t)(meta->m_crc32 & 0xFFu);
    buf[5] = (uint8_t)((meta->m_crc32 >> 8) & 0xFFu);
    buf[6] = (uint8_t)((meta->m_crc32 >> 16) & 0xFFu);
    buf[7] = (uint8_t)((meta->m_crc32 >> 24) & 0xFFu);
    buf[8] = (uint8_t)(meta->m_activeSlot & 0xFFu);
    buf[9] = (uint8_t)((meta->m_activeSlot >> 8) & 0xFFu);
    buf[10] = (uint8_t)((meta->m_activeSlot >> 16) & 0xFFu);
    buf[11] = (uint8_t)((meta->m_activeSlot >> 24) & 0xFFu);
    buf[12] = (uint8_t)(meta->m_requestedSlot & 0xFFu);
    buf[13] = (uint8_t)((meta->m_requestedSlot >> 8) & 0xFFu);
    buf[14] = (uint8_t)((meta->m_requestedSlot >> 16) & 0xFFu);
    buf[15] = (uint8_t)((meta->m_requestedSlot >> 24) & 0xFFu);
    buf[16] = (uint8_t)(meta->m_bootAttempts & 0xFFu);
    buf[17] = (uint8_t)((meta->m_bootAttempts >> 8) & 0xFFu);
    buf[18] = (uint8_t)((meta->m_bootAttempts >> 16) & 0xFFu);
    buf[19] = (uint8_t)((meta->m_bootAttempts >> 24) & 0xFFu);
    buf[20] = (uint8_t)(meta->m_maxAttempts & 0xFFu);
    buf[21] = (uint8_t)((meta->m_maxAttempts >> 8) & 0xFFu);
    buf[22] = (uint8_t)((meta->m_maxAttempts >> 16) & 0xFFu);
    buf[23] = (uint8_t)((meta->m_maxAttempts >> 24) & 0xFFu);
    XMemcpy(buf + 24, meta->m_imageSha256, XUPGRADE_SHA256_BIN_SIZE);
    buf[56] = (uint8_t)(meta->m_sequence & 0xFFu);
    buf[57] = (uint8_t)((meta->m_sequence >> 8) & 0xFFu);
    buf[58] = (uint8_t)((meta->m_sequence >> 16) & 0xFFu);
    buf[59] = (uint8_t)((meta->m_sequence >> 24) & 0xFFu);
    return XUpgradeError_Ok;
}

/** @brief 模拟 reboot 回调：只计数不真重启（宿主演练口径，§9.4）。 */
static XSystemResult xup_ab_ram_rebootHandler(void* userData, XSystemRebootMode mode)
{
    (void)userData;
    ++xup_ab_ramReboots;
    xup_ab_ramLastRebootMode = (int)mode;
    return XSystemResult_Ok;
}

/** @brief 就地构造元数据块字节（小端 60B；corrupt=true 时破坏 CRC）。 */
static void xup_ab_craftMeta(uint8_t* buf, uint32_t activeSlot, uint32_t requestedSlot,
                             uint32_t bootAttempts, uint32_t maxAttempts, uint32_t sequence,
                             bool corrupt)
{
    uint32_t crc;
    uint8_t i;
    XMemset(buf, 0, XUPGRADE_BOOT_META_SIZE);
    buf[0] = (uint8_t)(XUPGRADE_BOOT_META_MAGIC & 0xFFu);
    buf[1] = (uint8_t)((XUPGRADE_BOOT_META_MAGIC >> 8) & 0xFFu);
    buf[2] = (uint8_t)((XUPGRADE_BOOT_META_MAGIC >> 16) & 0xFFu);
    buf[3] = (uint8_t)((XUPGRADE_BOOT_META_MAGIC >> 24) & 0xFFu);
    buf[8] = (uint8_t)(activeSlot & 0xFFu);
    buf[12] = (uint8_t)(requestedSlot & 0xFFu);
    buf[16] = (uint8_t)(bootAttempts & 0xFFu);
    buf[20] = (uint8_t)(maxAttempts & 0xFFu);
    buf[56] = (uint8_t)(sequence & 0xFFu);
    crc = XCrc32_initialize(XCrc32_Algorithm_IsoHdlc);
    crc = XCrc32_update(XCrc32_Algorithm_IsoHdlc, crc, buf, 4);
    crc = XCrc32_update(XCrc32_Algorithm_IsoHdlc, crc, buf + 8, XUPGRADE_BOOT_META_SIZE - 8);
    buf[4] = (uint8_t)(crc & 0xFFu);
    buf[5] = (uint8_t)((crc >> 8) & 0xFFu);
    buf[6] = (uint8_t)((crc >> 16) & 0xFFu);
    buf[7] = (uint8_t)((crc >> 24) & 0xFFu);
    if (corrupt) buf[20] = (uint8_t)(buf[20] ^ 0xFFu); /* CRC 必不自洽 */
    (void)i;
}

/** @brief RAM 元数据块反序列化为内存表示（读断言用）。 */
static void xup_ab_loadMeta(uint32_t metaIndex, XUpgradeBootMeta* out)
{
    (void)xup_ab_ram_metaRead(NULL, metaIndex, out);
}

/** @brief 内存表示封 CRC（写回模拟 bootloader 落盘用；按 §9.2 全布局序列化
 *        到本地缓冲取 CRC，含 imageSha256 段，与实现侧序列化逐字节一致）。 */
static void xup_ab_sealMeta(XUpgradeBootMeta* meta)
{
    uint8_t buf[XUPGRADE_BOOT_META_SIZE];
    uint32_t crc;
    if (!meta) return;
    XMemset(buf, 0, sizeof(buf));
    buf[0] = (uint8_t)(meta->m_magic & 0xFFu);
    buf[1] = (uint8_t)((meta->m_magic >> 8) & 0xFFu);
    buf[2] = (uint8_t)((meta->m_magic >> 16) & 0xFFu);
    buf[3] = (uint8_t)((meta->m_magic >> 24) & 0xFFu);
    /* crc 字段（4..7）不参与；activeSlot/requested/bootAttempts/maxAttempts
     * 取值远小于 256，仅低字节非零。 */
    buf[8] = (uint8_t)(meta->m_activeSlot & 0xFFu);
    buf[12] = (uint8_t)(meta->m_requestedSlot & 0xFFu);
    buf[16] = (uint8_t)(meta->m_bootAttempts & 0xFFu);
    buf[20] = (uint8_t)(meta->m_maxAttempts & 0xFFu);
    XMemcpy(buf + 24, meta->m_imageSha256, XUPGRADE_SHA256_BIN_SIZE);
    buf[56] = (uint8_t)(meta->m_sequence & 0xFFu);
    crc = XCrc32_initialize(XCrc32_Algorithm_IsoHdlc);
    crc = XCrc32_update(XCrc32_Algorithm_IsoHdlc, crc, buf, 4);
    crc = XCrc32_update(XCrc32_Algorithm_IsoHdlc, crc, buf + 8,
                        XUPGRADE_BOOT_META_SIZE - 8);
    meta->m_crc32 = crc;
}

/** @brief ConfirmBoot 槽直调（XUpgradeStore_confirmBoot_base 尚未实现——见
 *        交付说明；此处按基类分派同款虚表取槽调用真实 Ab 虚函数）。 */
static XUpgradeError xup_ab_confirmBoot_slot(XUpgradeStore* self, const char* componentId,
                                             const char* version, const char** errText)
{
    if (errText) *errText = NULL;
    if (!self || XClassIsVtableNull(self)) return XUpgradeError_NotSupported;
    return XClassGetVirtualFunc(self, EXUpgradeStore_ConfirmBoot,
                                XUpgradeError(*)(XUpgradeStore*, const char*,
                                                 const char*, const char**))
           (self, componentId, version, errText);
}


static bool t_ab_store_flows(void)
{
    XUpgradePartitionBackend ram;
    XUpgradeStore* store = NULL;
    const char* err = NULL;
    static uint8_t img[XUP_AB_RAM_IMG_BYTES];
    XUpgradeBootMeta meta;
    uint32_t i;
    bool ok = true;

    xtest_scratch_wipe(XUP_TEST_SCRATCH_AB);
    xup_ab_ram_reset();
    for (i = 0; i < sizeof(img); ++i) img[i] = (uint8_t)(i * 7u + 3u);
    XMemset(&ram, 0, sizeof(ram));
    ram.m_userData = NULL;
    ram.m_slotCount = xup_ab_ram_slotCount;
    ram.m_slotSize = xup_ab_ram_slotSize;
    ram.m_read = xup_ab_ram_read;
    ram.m_write = xup_ab_ram_write;
    ram.m_erase = xup_ab_ram_erase;
    ram.m_metaRead = xup_ab_ram_metaRead;
    ram.m_metaWrite = xup_ab_ram_metaWrite;
    XUpgradeStore_setPartitionBackend(&ram);
    XSystem_setRebootHandler(xup_ab_ram_rebootHandler, NULL);

    /* 出厂双块（activeSlot=0，sequence=1）。 */
    xup_ab_craftMeta(xup_ab_ramMeta[0], 0, 0, 0, XUPGRADE_BOOT_ATTEMPTS_DEFAULT, 1, false);
    xup_ab_craftMeta(xup_ab_ramMeta[1], 0, 0, 0, XUPGRADE_BOOT_ATTEMPTS_DEFAULT, 1, false);

    /* 组件线互斥：A/B 后端仅接受 image。 */
    store = XUpgradeStoreAb_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XUP_TEST_SCRATCH_AB);
    XTEST_CHECK(store != NULL);
    if (store) {
        XTEST_CHECK(XUpgradeStore_stageBegin_base(store, "app", "2.0.0", 0, &err)
                  == XUpgradeError_NotSupported);
        /* staging：写非活动槽（出厂 requested=active=0 → 写槽 1）。 */
        XTEST_CHECK(XUpgradeStore_stageBegin_base(store, "image", "2.0.0",
                                                 sizeof(img), &err) == XUpgradeError_Ok);
        XTEST_CHECK(XUpgradeStore_stageWrite_base(store, img, sizeof(img))
                  == XUpgradeError_Ok);
        XTEST_CHECK(XUpgradeStore_stageEnd_base(store, &err) == XUpgradeError_Ok);
        /* 整槽回读校验的落点：槽 1 内容与写入一致；槽 0（active）未被覆写。 */
        {
            static uint8_t probe[XUP_AB_RAM_IMG_BYTES];
            XTEST_CHECK(XUpgradeStore_currentVersion_base(store, "image", probe, 0) == false);
            XTEST_CHECK(xup_ab_ram_read(NULL, 1, 0, probe, sizeof(probe)) == XUpgradeError_Ok);
            XTEST_CHECK(XMemcmp(probe, img, sizeof(img)) == 0);
            XTEST_CHECK(xup_ab_ramSlot[0][0] == 0xFFu); /* 不变式：不覆写 active 槽 */
        }
        /* 槽版本标记落 update/ 工作区。 */
        XTEST_CHECK(xtest_path_exists(XUP_TEST_SCRATCH_AB "/" XUPGRADE_DIR_UPDATE
                                     "/abslot/slot1.ver"));
        /* activate：双写置 requested=1、attempts=0、seq+1，随后 reboot。 */
        XTEST_CHECK(XUpgradeStore_activate_base(store, "image", "2.0.0", &err)
                  == XUpgradeError_Ok);
        XTEST_CHECK(xup_ab_ramReboots == 1 &&
             xup_ab_ramLastRebootMode == (int)XSystemRebootMode_Bootloader);
        xup_ab_loadMeta(0, &meta);
        XTEST_CHECK(meta.m_activeSlot == 0 && meta.m_requestedSlot == 1 &&
             meta.m_bootAttempts == 0 && meta.m_sequence == 2);
        /* 同版本不可重复 activate（staging 结果已消费）。 */
        XTEST_CHECK(XUpgradeStore_activate_base(store, "image", "2.0.0", &err)
                  == XUpgradeError_NotReady);
        /* store 保持存活：下一段模拟 bootloader 后经同一对象 confirmBoot。 */
    }

    /* 模拟 bootloader：计数未超 → 自增步进启动新槽（纯函数）。 */
    {
        XUpgradeBootMeta out;
        uint32_t bootSlot = 99;
        bool writeBack = false;
        xup_ab_loadMeta(0, &meta);
        XTEST_CHECK(XUpgradeStoreAb_bootPolicyStep(&meta, 2, &out, &bootSlot,
                                                  &writeBack) == XUpgradeError_Ok);
        XTEST_CHECK(bootSlot == 1 && out.m_bootAttempts == 1 && writeBack);
        out.m_sequence = meta.m_sequence + 1;
        xup_ab_sealMeta(&out);
        XTEST_CHECK(xup_ab_ram_metaWrite(NULL, 0, &out) == XUpgradeError_Ok);
        XTEST_CHECK(xup_ab_ram_metaWrite(NULL, 1, &out) == XUpgradeError_Ok);
        /* confirmBoot：activeSlot=requestedSlot、attempts=0、seq+1 双写。 */
        XTEST_CHECK(xup_ab_confirmBoot_slot(store, "image", "2.0.0", &err)
                  == XUpgradeError_Ok);
        xup_ab_loadMeta(0, &meta);
        XTEST_CHECK(meta.m_activeSlot == 1 && meta.m_requestedSlot == 1 &&
             meta.m_bootAttempts == 0 && meta.m_sequence == 4);
        /* confirmBoot 后 currentVersion 取新槽版本标记。 */
        {
            char version[XUPGRADE_VERSION_MAX];
            XTEST_CHECK(XUpgradeStore_currentVersion_base(store, "image", version,
                                                         sizeof(version)));
            XTEST_CHECK(xtest_str_eq(version, "2.0.0"));
        }
        /* rollback：requested 写回 active 槽（矩阵 11 的 A/B 侧）。 */
        XTEST_CHECK(XUpgradeStore_rollback_base(store, "image", "2.0.0", &err)
                  == XUpgradeError_Ok);
        xup_ab_loadMeta(0, &meta);
        XTEST_CHECK(meta.m_requestedSlot == 1 && meta.m_activeSlot == 1); /* 回锚自身 */
        XClassDelete((XClass*)store);
    }

    /* 纯函数三例之外两例：计数未超已验（上），此处超限回切 + 非法参数。 */
    {
        XUpgradeBootMeta out;
        uint32_t bootSlot = 99;
        bool writeBack = false;
        xup_ab_craftMeta(xup_ab_ramMeta[0], 0, 1, XUPGRADE_BOOT_ATTEMPTS_DEFAULT,
                         XUPGRADE_BOOT_ATTEMPTS_DEFAULT, 10, false);
        xup_ab_loadMeta(0, &meta);
        XTEST_CHECK(XUpgradeStoreAb_bootPolicyStep(&meta, 2, &out, &bootSlot,
                                                  &writeBack) == XUpgradeError_Ok);
        XTEST_CHECK(bootSlot == 0 && out.m_requestedSlot == 0 && /* 超限回切旧槽 */
             out.m_bootAttempts == 0 && writeBack);
        xup_ab_loadMeta(0, &meta);
        XTEST_CHECK(XUpgradeStoreAb_bootPolicyStep(&meta, 1, &out, &bootSlot,
                                                  &writeBack)
                  == XUpgradeError_InvalidArgument); /* 槽数越界 */
        XTEST_CHECK(XUpgradeStoreAb_bootPolicyStep(NULL, 2, &out, &bootSlot,
                                                  &writeBack)
                  == XUpgradeError_InvalidArgument);
    }

    /* 矩阵 9（元数据单块写坏=另一块接管）+ 双块皆坏报 Io。 */
    {
        store = XUpgradeStoreAb_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XUP_TEST_SCRATCH_AB);
        XTEST_CHECK(store != NULL);
        if (store) {
            xup_ab_craftMeta(xup_ab_ramMeta[0], 0, 0, 0,
                             XUPGRADE_BOOT_ATTEMPTS_DEFAULT, 9, true); /* A 坏 */
            xup_ab_craftMeta(xup_ab_ramMeta[1], 0, 0, 0,
                             XUPGRADE_BOOT_ATTEMPTS_DEFAULT, 3, false); /* B 好 */
            XTEST_CHECK(XUpgradeStore_stageBegin_base(store, "image", "2.0.0",
                                                     sizeof(img), &err)
                      == XUpgradeError_Ok); /* B 接管仲裁 */
            (void)XUpgradeStore_stageWrite_base(store, img, sizeof(img));
            XTEST_CHECK(XUpgradeStore_stageEnd_base(store, &err) == XUpgradeError_Ok);
            XTEST_CHECK(XUpgradeStore_activate_base(store, "image", "2.0.0", &err)
                      == XUpgradeError_Ok); /* 双写恢复两块一致 */
            xup_ab_loadMeta(0, &meta);
            XTEST_CHECK(meta.m_sequence == 4 && meta.m_requestedSlot == 1);
            XClassDelete((XClass*)store);
        }
        store = XUpgradeStoreAb_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XUP_TEST_SCRATCH_AB);
        XTEST_CHECK(store != NULL);
        if (store) {
            xup_ab_craftMeta(xup_ab_ramMeta[0], 0, 0, 0,
                             XUPGRADE_BOOT_ATTEMPTS_DEFAULT, 9, true);
            xup_ab_craftMeta(xup_ab_ramMeta[1], 0, 0, 0,
                             XUPGRADE_BOOT_ATTEMPTS_DEFAULT, 3, true); /* 双坏 */
            XTEST_CHECK(XUpgradeStore_stageBegin_base(store, "image", "2.0.0",
                                                     sizeof(img), &err)
                      == XUpgradeError_Io); /* 两块皆坏：存储级故障 */
            XClassDelete((XClass*)store);
        }
        xup_ab_ram_reset();
        xup_ab_craftMeta(xup_ab_ramMeta[0], 0, 0, 0, XUPGRADE_BOOT_ATTEMPTS_DEFAULT, 1, false);
        xup_ab_craftMeta(xup_ab_ramMeta[1], 0, 0, 0, XUPGRADE_BOOT_ATTEMPTS_DEFAULT, 1, false);
    }

    /* 矩阵 8（写槽中断电）：流半途对象销毁 → 槽内容不完整且 requested 未置。 */
    {
        store = XUpgradeStoreAb_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XUP_TEST_SCRATCH_AB);
        XTEST_CHECK(store != NULL);
        if (store) {
            XTEST_CHECK(XUpgradeStore_stageBegin_base(store, "image", "3.0.0",
                                                     sizeof(img), &err) == XUpgradeError_Ok);
            XTEST_CHECK(XUpgradeStore_stageWrite_base(store, img, sizeof(img) / 2)
                      == XUpgradeError_Ok);
            XClassDelete((XClass*)store); /* 断电：无 stageEnd */
            xup_ab_loadMeta(0, &meta);
            XTEST_CHECK(meta.m_requestedSlot == 0 && meta.m_activeSlot == 0);
            XTEST_CHECK(xup_ab_ramSlot[1][0] == img[0]);              /* 半截内容在 */
            XTEST_CHECK(xup_ab_ramSlot[1][sizeof(img) - 1] == 0xFFu); /* 尾部仍擦除态 */
            /* 槽版本标记不被中断流污染：仍是上一完整流（2.0.0）的内容
             * （标记含结尾换行，按包含判断）。 */
            {
                char mark[XUPGRADE_VERSION_MAX];
                size_t markLen = 0;
                XTEST_CHECK(xtest_read_into(XUP_TEST_SCRATCH_AB "/" XUPGRADE_DIR_UPDATE
                                            "/abslot/slot1.ver",
                                            mark, sizeof(mark), &markLen));
                XTEST_CHECK(xtest_str_contains(mark, "2.0.0"));
                XTEST_CHECK(!xtest_str_contains(mark, "3.0.0"));
            }
            /* 重新 staging 完整收敛（半成品不阻断）。 */
            store = XUpgradeStoreAb_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                              XUP_TEST_SCRATCH_AB);
            XTEST_CHECK(store != NULL);
            if (store) {
                XTEST_CHECK(XUpgradeStore_stageBegin_base(store, "image", "3.0.0",
                                                         sizeof(img), &err)
                          == XUpgradeError_Ok);
                XTEST_CHECK(XUpgradeStore_stageWrite_base(store, img, sizeof(img))
                          == XUpgradeError_Ok);
                XTEST_CHECK(XUpgradeStore_stageEnd_base(store, &err) == XUpgradeError_Ok);
                XTEST_CHECK(XUpgradeStore_activate_base(store, "image", "3.0.0", &err)
                          == XUpgradeError_Ok);
                xup_ab_loadMeta(0, &meta);
                XTEST_CHECK(meta.m_requestedSlot == 1);
                XClassDelete((XClass*)store);
            }
        }
    }

    /* 矩阵 10（confirm 前断电=计数超限回旧槽）：activate 后连续失败启动，
     * bootPolicyStep 达上限即 requested 写回 active（bootloader 兜底）。 */
    {
        XUpgradeBootMeta out;
        uint32_t bootSlot;
        bool writeBack;
        int step;
        xup_ab_loadMeta(0, &meta);
        for (step = 0; step < 3; ++step) { /* 3 次失败启动计数自增 */
            XTEST_CHECK(XUpgradeStoreAb_bootPolicyStep(&meta, 2, &out, &bootSlot,
                                                      &writeBack) == XUpgradeError_Ok);
            XTEST_CHECK(bootSlot == 1);
            out.m_sequence = meta.m_sequence + 1;
            xup_ab_sealMeta(&out);
            xup_ab_ram_metaWrite(NULL, 0, &out);
            xup_ab_ram_metaWrite(NULL, 1, &out);
            meta = out;
        }
        XTEST_CHECK(XUpgradeStoreAb_bootPolicyStep(&meta, 2, &out, &bootSlot,
                                                  &writeBack) == XUpgradeError_Ok);
        XTEST_CHECK(bootSlot == 0 && out.m_requestedSlot == 0); /* 超限回旧槽 */
    }

#if XUPGRADE_AB_SIM_ON
    /* 宿主模拟后端：与显式注册互斥 → 卸载显式后可装 → 缺省 4MiB → 卸载。 */
    XTEST_CHECK(!XUpgradeStoreAb_installSimBackend(XUP_TEST_SCRATCH_AB "/sim", 0));
    XUpgradeStore_setPartitionBackend(NULL);
    XTEST_CHECK(XUpgradeStoreAb_installSimBackend(XUP_TEST_SCRATCH_AB "/sim", 0));
    XTEST_CHECK(xtest_path_exists(XUP_TEST_SCRATCH_AB "/sim/slot_a.img"));
    XTEST_CHECK(xtest_path_exists(XUP_TEST_SCRATCH_AB "/sim/slot_b.img"));
    XTEST_CHECK(xtest_path_exists(XUP_TEST_SCRATCH_AB "/sim/meta_a.bin"));
    XTEST_CHECK(xtest_path_exists(XUP_TEST_SCRATCH_AB "/sim/meta_b.bin"));
    store = XUpgradeStoreAb_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XUP_TEST_SCRATCH_AB);
    XTEST_CHECK(store != NULL);
    if (store) {
        XTEST_CHECK(XUpgradeStore_stageBegin_base(store, "image", "2.0.0",
                                                 XUP_AB_RAM_IMG_BYTES, &err)
                  == XUpgradeError_Ok);
        XTEST_CHECK(XUpgradeStore_stageWrite_base(store, img, sizeof(img))
                  == XUpgradeError_Ok);
        XTEST_CHECK(XUpgradeStore_stageEnd_base(store, &err) == XUpgradeError_Ok);
        XTEST_CHECK(XUpgradeStore_activate_base(store, "image", "2.0.0", &err)
                  == XUpgradeError_Ok); /* reboot 经宿主回调计数 */
        XTEST_CHECK(xup_ab_ramReboots >= 1);
        XTEST_CHECK(xtest_path_exists(XUP_TEST_SCRATCH_AB "/" XUPGRADE_DIR_UPDATE
                                     "/abslot/slot1.ver"));
        XClassDelete((XClass*)store);
    }
    XTEST_CHECK(XUpgradeStoreAb_installSimBackend(NULL, 0)); /* 卸载 */
    XTEST_CHECK(XUpgradeStoreAb_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                         XUP_TEST_SCRATCH_AB) == NULL); /* 无后端 */
    XTEST_CHECK(XUpgradeStoreAb_installSimBackend(XUP_TEST_SCRATCH_AB "/sim",
                                                 1024) == false); /* <64KiB 拒绝 */
    XTEST_CHECK(XUpgradeStoreAb_installSimBackend(NULL, 0));
#endif /* XUPGRADE_AB_SIM_ON */

    XSystem_setRebootHandler(NULL, NULL);
    XUpgradeStore_setPartitionBackend(NULL);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_AB);
    return ok;
}

#else /* !XUPGRADE_AB_ON */

static bool t_ab_store_flows(void)
{
    /* 默认构建（XUPGRADE_AB_ON=0）A/B 契约整体缺席：用例按配置裁剪空转；
     * 真实路径验证在 XUPGRADE_AB_ON=1 独立构建中执行（交付说明记录命令）。 */
    XPrintf("    [XUPGRADE_AB_ON=0：A/B 用例按构建配置裁剪]\n");
    return true;
}

#endif /* XUPGRADE_AB_ON */

/* ========================================================================== */
/*  用例 22：遗留修复回归（指针原子写收敛/并发 save_state/自检持锁烟囱）          */
/* ========================================================================== */

/** @brief 并发 save 线程入参。 */
typedef struct XUpSaveThreadArgs {
    XUpgradeEngine* m_engine;   /**< 目标引擎。 */
    int m_index;                /**< 线程序号（决定组件与版本）。 */
} XUpSaveThreadArgs;

/** @brief 并发 save 线程体：各写一条互不相同的跳过记录（每条真实落盘一次）。 */
static void xup_save_thread_main(XThread* thread, XVarList* arguments)
{
    (void)thread;
    if (!arguments) return;
    XVarList_args_1(arguments, XUpSaveThreadArgs*, args); /* 宏内声明 const 指针 */
    if (!args || !args->m_engine) return;
    {
        char version[XUPGRADE_VERSION_MAX];
        XTEST_FMT(version, sizeof(version), "9.9.%d", args->m_index);
        XUpgradeEngine_skipVersion(args->m_engine,
                                   (args->m_index % 2) ? "fontres" : "app", version);
    }
}

/** @brief update/ 目录是否残留原子写临时文件（".XXXXXX" 工作临时名）。 */
static bool xup_update_dir_has_temp_leftover(const char* rootUtf8)
{
    XDir* dir;
    XStringList* entries;
    XString* path;
    int64_t i;
    int64_t count;
    bool found = false;
    path = XString_create_utf8(rootUtf8);
    dir = path ? XDir_create_2(path) : NULL;
    XClassDelete(path);
    if (!dir) return false;
    path = XString_create_fmt_utf8("%s/" XUPGRADE_DIR_UPDATE, rootUtf8);
    entries = path ? XDir_entryList_1(dir, XDir_Files, XDir_NoSort) : NULL;
    XClassDelete(path);
    count = entries ? XStringList_size_base((const XContainer*)entries) : 0;
    for (i = 0; i < count && !found; ++i) {
        XString* item = (XString*)XStringList_at_base((const XVector*)entries, i);
        if (item && xtest_str_contains(XString_toUtf8(item), ".XXXXXX")) found = true;
    }
    if (entries) XClassDelete(entries);
    XClassDelete((XClass*)dir);
    return found;
}

static bool t_legacy_atomic_regression(void)
{
    XUpgradeEngine* engine = NULL;
    XUpgradeConfig config;
    XUpgradeStateFile state;
    char pointer[XUPGRADE_VERSION_MAX + 4];
    bool ok = true;

    xtest_scratch_wipe(XUP_TEST_SCRATCH_REG);

    /* (a) 指针原子写中断电近似：指针处于"旧/新/缺失（非法内容）"三态均收敛。
     *     原子替换本身由 XSaveFile 保证（矩阵 6），进程内以三态终态断言收敛面。 */
    {
        int variant;
        for (variant = 0; variant < 3; ++variant) {
            char verifyBody[160];
            char payload[64];
            uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];
            char hex[XUPGRADE_SHA256_HEX_MAX];
            char path[1400];
            uint8_t k;
            xtest_scratch_wipe(XUP_TEST_SCRATCH_REG);
            XTEST_CHECK(xtest_seed_version_dir(XUP_TEST_SCRATCH_REG, "app", "1.0.0"));
            /* 新版目录完整（payload + .verify 清单 + 标记）。 */
            for (k = 0; k < sizeof(payload); ++k) payload[k] = (char)('A' + (k % 26));
            XTEST_CHECK(xtest_sha256((const uint8_t*)payload, sizeof(payload), digest));
            xtest_bin2hex(digest, XUPGRADE_SHA256_BIN_SIZE, hex);
            XTEST_FMT(verifyBody, sizeof(verifyBody), "payload.bin %s\n", hex);
            XTEST_FMT(path, sizeof(path), "%s/" XUPGRADE_DIR_VERSIONS "/app/2.0.0/payload.bin",
                      XUP_TEST_SCRATCH_REG);
            XTEST_CHECK(xtest_write_file(path, payload, sizeof(payload)));
            XTEST_CHECK(xtest_seed_version_dir(XUP_TEST_SCRATCH_REG, "app", "2.0.0"));
            XTEST_FMT(path, sizeof(path), "%s/" XUPGRADE_DIR_VERSIONS "/app/2.0.0/"
                      XUPGRADE_STORE_DIR_VERIFY_FILE, XUP_TEST_SCRATCH_REG);
            XTEST_CHECK(xtest_write_file(path, verifyBody, xtest_str_len(verifyBody)));
            XUpgradeStateFile_clear(&state);
            xtest_str_copy(state.m_components[0].m_id, XUPGRADE_COMPONENT_ID_MAX, "app");
            xtest_str_copy(state.m_components[0].m_activeVersion,
                           XUPGRADE_VERSION_MAX, "1.0.0");
            xtest_str_copy(state.m_components[0].m_stagedVersion,
                           XUPGRADE_VERSION_MAX, "2.0.0");
            xtest_str_copy(state.m_components[0].m_maxVersion, XUPGRADE_VERSION_MAX, "1.0.0");
            state.m_components[0].m_staged = true;
            state.m_componentCount = 1;
            /* variant 0=Active（非确认窗口的外部改动）；1/2=BootConfirming
             * （桩切换窗口：指针旧/新均合法的矩阵 6 近似）。 */
            state.m_state = (variant == 0) ? XUpgradeState_Active
                                           : XUpgradeState_BootConfirming;
            XTEST_CHECK(xtest_save_state(XUP_TEST_SCRATCH_REG, &state));
            /* 指针三态注入：0=非法内容（=读失败口径）、1=旧版本、2=新版本。 */
            if (variant == 0) XTEST_CHECK(xtest_write_pointer(XUP_TEST_SCRATCH_REG, "app", "@@@"));
            else if (variant == 1) XTEST_CHECK(xtest_write_pointer(XUP_TEST_SCRATCH_REG, "app", "1.0.0"));
            else XTEST_CHECK(xtest_write_pointer(XUP_TEST_SCRATCH_REG, "app", "2.0.0"));
            xup_case_events_reset();
            engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
            XTEST_CHECK(engine != NULL);
            if (!engine) break;
            XUpgradeEngine_setListener(engine, xup_case_listener, &g_xup_case_events);
            xtest_fill_config(&config, XUP_TEST_SCRATCH_REG);
            XTEST_CHECK(XUpgradeEngine_start(engine, &config));
            XTEST_CHECK(XUpgradeEngine_selfCheck(engine));
            XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_REG, &state));
            if (variant == 0) {
                /* 非法指针=未激活口径 → 写回上一激活版本（Active 态外部改动回滚）。 */
                XTEST_CHECK(g_xup_case_events.m_byType[XUpgradeEventType_RolledBack] >= 1);
                XTEST_CHECK(xtest_read_pointer(XUP_TEST_SCRATCH_REG, "app", pointer,
                                               sizeof(pointer)));
                XTEST_CHECK(xtest_str_eq(pointer, "1.0.0"));
                XTEST_CHECK(xtest_str_eq(state.m_components[0].m_activeVersion, "1.0.0"));
                XTEST_CHECK(state.m_components[0].m_rollbackCount == 1);
            } else if (variant == 1) {
                /* 确认窗口+旧指针合法 → 收敛 Active（版本保持旧）。 */
                XTEST_CHECK(XUpgradeEngine_state(engine) == XUpgradeState_Active);
                XTEST_CHECK(xtest_str_eq(state.m_components[0].m_activeVersion, "1.0.0"));
            } else {
                /* 确认窗口+新指针合法且抽查过 → 收敛 Active、防降级基线抬升。 */
                XTEST_CHECK(XUpgradeEngine_state(engine) == XUpgradeState_Active);
                XTEST_CHECK(xtest_str_eq(state.m_components[0].m_activeVersion, "2.0.0"));
                XTEST_CHECK(xtest_str_eq(state.m_components[0].m_maxVersion, "2.0.0"));
                XTEST_CHECK(!state.m_components[0].m_staged);
            }
            XUpgradeEngine_stop(engine);
            XClassDelete((XClass*)engine);
            engine = NULL;
        }
    }
    xtest_scratch_wipe(XUP_TEST_SCRATCH_REG);

    /* (b) 并发 save_state 不再共享临时名：8 线程并发经引擎入口各写一条跳过
     *     记录（每条真实触发一次原子落盘），收口断言：state.json 可加载
     *     （CRC 自洽=未发生交错混写）、8 条记录齐全、无 ".XXXXXX" 残留。 */
    {
        XThread* threads[8];
        XUpSaveThreadArgs args[8];
        XVarList* threadArgs[8];
        int i;
        XTEST_CHECK(xtest_seed_version_dir(XUP_TEST_SCRATCH_REG, "app", "1.0.0"));
        XUpgradeStateFile_clear(&state);
        state.m_state = XUpgradeState_Active;
        xtest_str_copy(state.m_components[0].m_id, XUPGRADE_COMPONENT_ID_MAX, "app");
        xtest_str_copy(state.m_components[0].m_activeVersion, XUPGRADE_VERSION_MAX, "1.0.0");
        xtest_str_copy(state.m_components[0].m_maxVersion, XUPGRADE_VERSION_MAX, "1.0.0");
        state.m_componentCount = 1;
        XTEST_CHECK(xtest_save_state(XUP_TEST_SCRATCH_REG, &state));
        engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XTEST_CHECK(engine != NULL);
        if (engine) {
            XUpgradeEngine_setListener(engine, xup_case_listener, &g_xup_case_events);
            xtest_fill_config(&config, XUP_TEST_SCRATCH_REG);
            XTEST_CHECK(XUpgradeEngine_start(engine, &config));
            for (i = 0; i < 8; ++i) {
                XUpSaveThreadArgs* argPtr = &args[i];
                args[i].m_engine = engine;
                args[i].m_index = i;
                threadArgs[i] = XVarList_Create(XVar(XUpSaveThreadArgs*, argPtr));
                threads[i] = threadArgs[i]
                                 ? XThread_create_func(xup_save_thread_main, threadArgs[i])
                                 : NULL;
                XTEST_CHECK(threads[i] != NULL);
                if (threads[i]) XTEST_CHECK(XThread_start(threads[i]));
            }
            for (i = 0; i < 8; ++i) {
                if (threads[i]) {
                    XTEST_CHECK(XThread_wait(threads[i], XUP_TEST_WAIT_MS));
                    XClassDelete((XClass*)threads[i]);
                }
                if (threadArgs[i]) XVarList_delete(threadArgs[i]);
            }
            XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_REG, &state));
            ok = ok && state.m_skipCount == 8; /* 8 条并发写全部落盘且未混写 */
            XTEST_CHECK(!xup_update_dir_has_temp_leftover(XUP_TEST_SCRATCH_REG));

            /* (c) 自检与运行中引擎并发（全程持锁的进程内烟囱验证）：契约允许
             *     start 后调 selfCheck（XUpgradeEngine.h:216-225），跑通且状态
             *     镜像未被并发撕裂（跳过表仍在、CRC 可加载）即冒烟通过；
             *     "每一步持锁"本身属代码评审口径，不以行为硬造（见交付说明）。 */
            XTEST_CHECK(XUpgradeEngine_selfCheck(engine));
            XTEST_CHECK(xtest_load_state(XUP_TEST_SCRATCH_REG, &state));
            XTEST_CHECK(state.m_skipCount == 8);
            XUpgradeEngine_stop(engine);
            XClassDelete((XClass*)engine);
        }
    }
    xtest_scratch_wipe(XUP_TEST_SCRATCH_REG);
    return ok;
}

/* ========================================================================== */
/*      浸泡模式（--xupgrade-soak <循环数>）：生命周期循环 + 工作集泄漏判定       */
/* ========================================================================== */

/*
 * 平台 API 豁免说明（门禁特许，仅限本测试文件）：Src 生产代码禁直用平台 API
 * （跨平台纪律，见本文件头"零平台 API"备注）。浸泡泄漏判定需要与库内分配器
 * 无关的进程工作集真值：XMemory 统计只覆盖库内分配器口径，覆盖不了运行时/
 * 第三方堆驻留，库内亦无工作集抽象——故本测试文件直用 win32 PSAPI
 * GetProcessMemoryInfo 采样；Src/ 生产源码的禁令不受影响（豁免面仅本文件）。
 */
#if defined(_WIN32)
#if !defined(PSAPI_VERSION)
#define PSAPI_VERSION 2 /* GetProcessMemoryInfo→K32 前缀：kernel32 内置导入，免链 psapi.lib */
#endif
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#if defined(_MSC_VER)
#pragma comment(lib, "psapi") /* 旧 SDK（PSAPI_VERSION=1）兜底；=2 时无副作用 */
#endif
#endif /* _WIN32 */

#define XUP_TEST_SCRATCH_SOAK "xupgrade_e2e_scratch_soak"

/** @brief 泄漏判定阈值（门禁纪律固定值）：slope 字节/轮；thirdDelta 字节。 */
#define XUP_SOAK_SLOPE_LIMIT       2048.0
#define XUP_SOAK_THIRD_DELTA_LIMIT 65536.0
#define XUP_SOAK_R2_LIMIT          0.7

/** @brief 单轮浸泡采样点（进程工作集 + 可取到的 XMemory 统计口径）。 */
typedef struct XUpSoakSample {
    uint64_t m_workingSet;  /**< 进程工作集字节；PSAPI 不可得（非 win32）=0。 */
    uint64_t m_systemBytes; /**< XMemory 系统分配器在用字节（即时口径）。 */
    uint64_t m_poolUsed;    /**< 内存池已分配给用户字节（多级池+可变池合计）。 */
    uint64_t m_poolTotal;   /**< 内存池总容量字节；0=池未启用（桌面全系统堆）。 */
} XUpSoakSample;

/** @brief 采样一次：PSAPI 进程工作集 + XMemory 三池统计（_2=即时口径，不
 *         受统计使能开关门控，XMemory.h 契约；测试直读当前真值）。 */
static void xup_soak_sample(XUpSoakSample* out)
{
    XMemoryStatistics sys;
    XMemoryStatistics multi;
    XMemoryStatistics variable;
    XMemset(out, 0, sizeof(*out));
#if defined(_WIN32)
    {
        PROCESS_MEMORY_COUNTERS pmc;
        XMemset(&pmc, 0, sizeof(pmc));
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
            out->m_workingSet = (uint64_t)pmc.WorkingSetSize;
    }
#endif
    sys = XMemory_statistics_2(XMEMORY_TYPE_SYSTEM);
    multi = XMemory_statistics_2(XMEMORY_TYPE_MULTIPOOL);
    variable = XMemory_statistics_2(XMEMORY_TYPE_VARIABLEPOOL);
    out->m_systemBytes = (uint64_t)sys.systemBytes;
    out->m_poolUsed = (uint64_t)multi.poolUsedBytes + (uint64_t)variable.poolUsedBytes;
    out->m_poolTotal = (uint64_t)multi.poolTotalBytes + (uint64_t)variable.poolTotalBytes;
}

/**
 * @brief 单轮浸泡：完整引擎生命周期（XUpgrade_init→进程内 HTTP 拉
 *        manifest+.pak→checkNow→startUpgrade→apply（--xupgrade-apply 桩
 *        直调）→自检确认（BootConfirming→Active）→人为损坏新版本→自检
 *        回滚→XUpgrade_deinit），每 10 轮穿插一次 init/deinit 快速启停。
 * @param index 轮次（1 基；%10==0 时穿插快速启停）。
 * @return 本轮全部断言通过返回 true。
 * @note 全程真实路径：manifest/.pak 经进程内 XHttpServer 真实下发（共享
 *       夹具与既有 e2e 同源）；apply 走 XUpgrade_applyNow→deinit→桩直调；
 *       自检走 XUpgradeEngine_selfCheck——模块级 XUpgrade_selfCheck 无根
 *       参数（临时引擎按 exe 目录推根，XUpgrade.c:246），指向浸泡 scratch
 *       须用同一实现的引擎入口（t_e2e_http_flow:1791 确认引擎同款），
 *       非夹具特例。回滚写回目标=记录的激活版本（xup_rollback_component
 *       契约），确认后即 2.0.0 自身，故自检回滚断言"回滚路径已执行"
 *       （RolledBack 事件+计数），不断言指针回到 1.0.0。
 */
static bool xup_soak_cycle_once(int index)
{
    XUpgradeConfig config;
    XUpgradeStateFile state;
    XUpgradeEngine* engine = NULL;
    char pointer[XUPGRADE_VERSION_MAX + 4];
    char path[1400];
    int rolledBaseline;
    bool ok = true;
    xtest_scratch_wipe(XUP_TEST_SCRATCH_SOAK);
    xtest_events_reset();
    /* 每 10 轮穿插一次 init/deinit 快速启停：空根即开即停，专测停机路径
     * （工作线程起停/引擎析构/锁文件清理）的重复分配释放。 */
    if (index % 10 == 0) {
        xtest_fill_config(&config, XUP_TEST_SCRATCH_SOAK);
        ok = ok && XUpgrade_init(&config);
        XUpgrade_deinit();
    }
    /* 主流程：模块单例全生命周期（与 t_e2e_http_flow 同款真实 HTTP 流）。 */
    XUpgrade_setListener(xtest_listener, NULL);
    xtest_fill_config(&config, XUP_TEST_SCRATCH_SOAK);
    ok = ok && XUpgrade_init(&config);
    ok = ok && XUpgrade_checkNow();
    ok = ok && xtest_wait_until(xup_wait_update_available, NULL);
    ok = ok && XUpgrade_startUpgrade();
    ok = ok && xtest_wait_until(xup_wait_upgrade_done, NULL);
    ok = ok && g_xup_events.m_readyNeedRestart > 0 && g_xup_events.m_downloadFailed == 0;
    /* apply：引擎内切指针（Applying→BootConfirming）→ deinit（主程序退出
     * 近似）→ 桩直调幂等无操作（目录方案下切换已在 applyNow 内完成）。 */
    ok = ok && XUpgrade_applyNow();
    XUpgrade_deinit();
    {
        int rc = -1;
        ok = ok && xup_run_stub(XUP_TEST_SCRATCH_SOAK, &rc);
        ok = ok && rc == 0;
    }
    ok = ok && xtest_read_pointer(XUP_TEST_SCRATCH_SOAK, "app", pointer, sizeof(pointer));
    ok = ok && xtest_str_eq(pointer, "2.0.0");
    ok = ok && xtest_load_state(XUP_TEST_SCRATCH_SOAK, &state);
    ok = ok && state.m_state == XUpgradeState_BootConfirming;
    /* 自检（确认窗口，新版本完好）：BootConfirming→Active。 */
    engine = XUpgradeEngine_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    ok = ok && engine != NULL;
    if (engine) {
        /* 裸建引擎不走模块单例接线，事件统计须显式注册监听者。 */
        XUpgradeEngine_setListener(engine, xtest_listener, NULL);
        xtest_fill_config(&config, XUP_TEST_SCRATCH_SOAK);
        ok = ok && XUpgradeEngine_start(engine, &config);
        ok = ok && XUpgradeEngine_selfCheck(engine);
        ok = ok && XUpgradeEngine_selfCheck(engine);
        ok = ok && XUpgradeEngine_state(engine) == XUpgradeState_Active;
        ok = ok && xtest_load_state(XUP_TEST_SCRATCH_SOAK, &state);
        ok = ok && xtest_str_eq(state.m_components[0].m_activeVersion, "2.0.0");
        /* 人为损坏新版本：激活版本目录关键标记改写（真实文件破坏）。 */
        XTEST_FMT(path, sizeof(path), "%s/" XUPGRADE_DIR_VERSIONS "/app/2.0.0/"
                  XUPGRADE_FILE_VERSION_MARK, XUP_TEST_SCRATCH_SOAK);
        ok = ok && xtest_write_file(path, "9.9.9", 5);
        /* 自检（损坏后）：完整性失败走回滚路径（RolledBack 事件+计数+1）。 */
        rolledBaseline = g_xup_events.m_rolledBack;
        ok = ok && XUpgradeEngine_selfCheck(engine);
        ok = ok && g_xup_events.m_rolledBack > rolledBaseline;
        ok = ok && xtest_load_state(XUP_TEST_SCRATCH_SOAK, &state);
        ok = ok && state.m_components[0].m_rollbackCount == 1;
        XUpgradeEngine_stop(engine);
        XClassDelete((XClass*)engine);
    } else {
        ok = false;
    }
    XUpgrade_deinit(); /* 幂等收口（apply 段已 deinit，此为契约空操作） */
    xtest_scratch_wipe(XUP_TEST_SCRATCH_SOAK);
    return ok;
}

/**
 * @brief 浸泡主入口：N 轮完整生命周期，逐轮采样一次，末行固定格式汇总。
 * @param cycles 循环数（命令行 --xupgrade-soak 的值）。
 * @return 判定 FAIL 返回 1，否则 0（进程退出码口径）。
 * @details 判定纪律（照此实现，禁平坦平台启发式）：对每轮进程工作集做
 *          最小二乘拟合（y=工作集字节，x=轮次）得 slope 与 r2；仅当
 *          slope>2048（字节/轮）且 r2>0.7 且 thirdDelta>65536 才 FAIL，
 *          否则 PASS。thirdDelta=末 1/3 均值−首 1/3 均值。拟合无效
 *          （有效样本<2 或退化分母=0，如全平直线）按 0 计——无证据不判
 *          泄漏：平坦本身不是泄漏证据，故不做平坦平台启发式。工作集不可
 *          得（非 win32 平台，采样恒 0）的轮次不入拟合。XMemory 池统计
 *          逐轮打印供人工核对，不参与判定（判定口径=进程工作集）。
 */
static int xup_soak_run(int cycles)
{
    /* 最小二乘流式累加量（双精度，免动态数组）：Σx/Σy/Σxy/Σx²/Σy²。 */
    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumX2 = 0.0, sumY2 = 0.0;
    double firstSum = 0.0, lastSum = 0.0;
    double slope = 0.0, r2 = 0.0, thirdDelta = 0.0;
    int firstN = 0, lastN = 0, fitN = 0, failedCycles = 0;
    int third;
    int i;
    if (cycles < 1) cycles = 1;
    third = cycles / 3;
    if (third == 0) third = 1; /* N<3 时取首末各 1 轮（N>=2 有比较意义） */
    for (i = 1; i <= cycles; ++i) {
        XUpSoakSample s;
        bool cycleOk = xup_soak_cycle_once(i);
        if (!cycleOk) ++failedCycles;
        xup_soak_sample(&s); /* 采样点固定=轮末（teardown 后），逐轮同位 */
        if (s.m_workingSet > 0) {
            double x = (double)i;
            double y = (double)s.m_workingSet;
            ++fitN;
            sumX += x;
            sumY += y;
            sumXY += x * y;
            sumX2 += x * x;
            sumY2 += y * y;
            if (i <= third) { firstSum += y; ++firstN; }
            if (i > cycles - third) { lastSum += y; ++lastN; }
        }
        /* poolUsed 按 XMemory size_t 原值有符号显示：统计漂移（外部直接
         * free 池后备缓冲等）可为小幅负数，回绕成巨大无符号值反失真。 */
        XPrintf("SOAK cycle=%d/%d ok=%s ws=%llu sysHeap=%llu poolUsed=%lld poolTotal=%llu\n",
                i, cycles, cycleOk ? "1" : "0",
                (unsigned long long)s.m_workingSet,
                (unsigned long long)s.m_systemBytes,
                (long long)s.m_poolUsed,
                (unsigned long long)s.m_poolTotal);
    }
    /* 最小二乘：slope=(nΣxy−ΣxΣy)/(nΣx²−(Σx)²)；
     * r2=相关系数平方=分子²/((nΣx²−(Σx)²)(nΣy²−(Σy)²))。 */
    if (fitN >= 2) {
        double denomX = (double)fitN * sumX2 - sumX * sumX;
        double numer = (double)fitN * sumXY - sumX * sumY;
        double denomY = (double)fitN * sumY2 - sumY * sumY;
        if (denomX > 0.0) slope = numer / denomX;
        if (denomX > 0.0 && denomY > 0.0) r2 = (numer * numer) / (denomX * denomY);
    }
    if (firstN > 0 && lastN > 0)
        thirdDelta = (lastSum / (double)lastN) - (firstSum / (double)firstN);
    if (failedCycles > 0)
        XPrintf("SOAK note: %d/%d 轮生命周期断言失败（判定仍按泄漏纪律口径）\n",
                failedCycles, cycles);
    XPrintf("SOAK cycles=%d slopeBytesPerCycle=%.1f r2=%.3f thirdDelta=%.1f verdict=%s\n",
            cycles, slope, r2, thirdDelta,
            (slope > XUP_SOAK_SLOPE_LIMIT && r2 > XUP_SOAK_R2_LIMIT &&
             thirdDelta > XUP_SOAK_THIRD_DELTA_LIMIT) ? "FAIL" : "PASS");
    return (slope > XUP_SOAK_SLOPE_LIMIT && r2 > XUP_SOAK_R2_LIMIT &&
            thirdDelta > XUP_SOAK_THIRD_DELTA_LIMIT) ? 1 : 0;
}

/* ========================================================================== */
/*                              用例表与入口                                    */
/* ========================================================================== */

typedef struct XUpgradeTestCase {
    const char* m_name;
    bool (*m_run)(void);
    const char* m_scratch;   /**< 用例专属 scratch 根（子进程跑后清理）；NULL=无。 */
    bool m_passed;
} XUpgradeTestCase;

static XUpgradeTestCase k_tests[] = {
    { "单测：版本比较全边界",                     t_version_compare, NULL, false },
    { "单测：manifest 解析与拒绝矩阵",            t_manifest_parse, NULL, false },
    { "单测：.pak 写读回环与损坏拒绝",            t_pak_roundtrip, NULL, false },
    { "单测：state.json 原子写回环",              t_state_roundtrip, XUP_TEST_SCRATCH_UNIT, false },
    { "单测：DirStore staging/激活/回滚/枚举",    t_storedir_flow, XUP_TEST_SCRATCH_STORE, false },
    { "单测：身份提供者契约",                     t_identity, NULL, false },
    { "端到端：HTTP 检查→升级→apply 桩→自检 Active", t_e2e_http_flow, XUP_TEST_SCRATCH_E2E, false },
    { "端到端：跳过此版本过滤",                   t_skip_flow, XUP_TEST_SCRATCH_SKIP, false },
    { "矩阵 1：下载中断 partial 保留后重下",       t_matrix1_download_interrupt, XUP_TEST_SCRATCH_M1, false },
    { "矩阵 2/3：校验/解包中断启动收敛",           t_matrix23_verify_stage_interrupt, XUP_TEST_SCRATCH_M2, false },
    { "矩阵 4/5/7：staged 缺失拒绝/Applying 续做/幂等", t_matrix457_stub_semantics, XUP_TEST_SCRATCH_M4, false },
    { "自检：指针指向被删目录回滚",               t_selfcheck_pointer_missing_dir, XUP_TEST_SCRATCH_CKA, false },
    { "自检：全新设备无指针/无版本目录收编",       t_selfcheck_fresh_device, XUP_TEST_SCRATCH_FRSH, false },
    { "自检：损坏新版本关键文件回滚",             t_selfcheck_corrupt_new_version, XUP_TEST_SCRATCH_CKB, false },
    { "回滚计数超限停用自动检查",                 t_rollback_limit_disables_check, XUP_TEST_SCRATCH_LIMIT, false },
    { "引擎锁：同进程双实例互斥",                 t_engine_lock_double_instance, XUP_TEST_SCRATCH_LOCK, false },
    { "契约：P-256 验签链与 canonical 一致性",    t_p256_verify_chain, NULL, false },
    { "契约：防降级判定与 min_version 强制锚",     t_policy_anti_downgrade, XUP_TEST_SCRATCH_ADG, false },
    { "契约：灰度三态判定（allow/deny/percent 边界）", t_gray_three_states, XUP_TEST_SCRATCH_GRAY, false },
    { "契约：策略时机（forced/silent 窗口/跳过重置）", t_policy_timing_flows, XUP_TEST_SCRATCH_POL, false },
    { "契约：fontres 组件线与 requires 整单拒绝",  t_component_line_flows, XUP_TEST_SCRATCH_COMP, false },
    { "契约：A/B 后端宿主模拟与断电矩阵 8-11 近似", t_ab_store_flows, XUP_TEST_SCRATCH_AB, false },
    { "回归：指针原子写收敛/并发 save_state/自检持锁", t_legacy_atomic_regression, XUP_TEST_SCRATCH_REG, false }
};

int main(int argc, char* argv[])
{
    int total = (int)(sizeof(k_tests) / sizeof(k_tests[0]));
    int passed = 0;
    int ran = 0;
    int only = -1; /* --only N：只跑第 N 个用例（0 基）；父进程用它隔离用例 */
    int soakCycles = 0; /* --xupgrade-soak N：>0=浸泡模式循环数；0=未指定（常规模式） */
    int i;
    for (i = 1; i < argc; ++i) {
        if (argv[i] && xtest_str_eq(argv[i], "--only") && i + 1 < argc && argv[i + 1]) {
            const char* p = argv[i + 1];
            only = 0;
            while (*p >= '0' && *p <= '9') { only = only * 10 + (*p - '0'); ++p; }
            ++i;
        } else if (argv[i] && xtest_str_eq(argv[i], "--xupgrade-soak")) {
            /* 浸泡模式：拼写固定（门禁按它调用）。带值按值（显式 0/非法值=
             * 不进浸泡分支，落回常规模式；上限钳 1000000 防溢出）；缺值取
             * 容错缺省 30。解析风格与上方 --only 分支一致。 */
            const char* p = (i + 1 < argc && argv[i + 1]) ? argv[i + 1] : NULL;
            if (p) {
                int v = 0;
                while (*p >= '0' && *p <= '9') {
                    if (v <= 100000) v = v * 10 + (*p - '0'); /* 途中钳制防溢出 */
                    ++p;
                }
                ++i;
                soakCycles = (v > 1000000) ? 1000000 : v;
            } else {
                soakCycles = 30;
            }
        }
    }
    XCoreApplication_create(argc, argv);
    XPrintf("=== XUpgrade M1 端到端联调测试 ===\n");
    XPrintf("工作目录: 仓库 bin/（scratch=xupgrade_e2e_scratch_*，跑前清空跑后删除）\n\n");
    /* 全局夹具：版本提供者 + 进程内静态服务器（父/子进程各自一套）。 */
    XUpgrade_setVersionProvider(xtest_version_provider, NULL);
    if (!xtest_build_fixtures()) {
        XPrintf("[致命] .pak/manifest 夹具构建失败\n");
        XClassDelete((XClass*)g_xup_pak); /* 夹具半途失败时 .pak 可能已建成 */
        XClassDelete((XClass*)g_xup_manifest);
        XClassDelete(XCoreApplication_instance());
        return 250;
    }
    g_xup_server = XHttpServer_create();
    if (!g_xup_server) {
        XPrintf("[致命] XHttpServer_create 失败\n");
        XClassDelete((XClass*)g_xup_pak);
        XClassDelete((XClass*)g_xup_manifest);
        XClassDelete(XCoreApplication_instance());
        return 250;
    }
    XMemset(&g_xup_serverCtx, 0, sizeof(g_xup_serverCtx));
    g_xup_serverCtx.m_manifest = g_xup_manifest;
    g_xup_serverCtx.m_pak = g_xup_pak;
    g_xup_serverCtx.m_pakName = g_xup_pakName;
    XHttpServer_setHandler(g_xup_server, xtest_server_handler, &g_xup_serverCtx);
    if (!XHttpServer_listen(g_xup_server, NULL, 0) || XHttpServer_serverPort(g_xup_server) == 0) {
        XPrintf("[致命] 进程内服务器监听失败\n");
        XClassDelete((XClass*)g_xup_server);
        XClassDelete((XClass*)g_xup_pak);
        XClassDelete((XClass*)g_xup_manifest);
        XClassDelete(XCoreApplication_instance());
        return 250;
    }
    XTEST_FMT(g_xup_manifestUrl, sizeof(g_xup_manifestUrl),
              "http://127.0.0.1:%u/xup/{channel}/manifest.json",
              (unsigned)XHttpServer_serverPort(g_xup_server));
    XPrintf("静态服务: %s（.pak=%llu B 数据区）\n\n", g_xup_manifestUrl,
            (unsigned long long)g_xup_pakPayloadSize);
    if (soakCycles > 0) {
        /* 浸泡模式（--xupgrade-soak <循环数>）：逐轮完整引擎生命周期+采样，
         * 末行固定格式汇总；退出码=泄漏判定 FAIL?1:0。常规模式行为不变。 */
        int soakRc;
        XPrintf("=== 浸泡模式：--xupgrade-soak %d ===\n\n", soakCycles);
        soakRc = xup_soak_run(soakCycles);
        XUpgrade_deinit(); /* 幂等兜底 */
        XHttpServer_close(g_xup_server);
        XClassDelete((XClass*)g_xup_server);
        XClassDelete((XClass*)g_xup_pak);
        XClassDelete((XClass*)g_xup_manifest);
        xtest_scratch_wipe(XUP_TEST_SCRATCH_SOAK);
        XClassDelete(XCoreApplication_instance());
        return soakRc;
    }
    /* 跑前清空全部 scratch。 */
    xtest_scratch_wipe(XUP_TEST_SCRATCH_UNIT);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_STORE);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_E2E);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_SKIP);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M1);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M2);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M3);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M4);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M5);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_CKA);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_CKB);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_LIMIT);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_LOCK);
    if (only >= 0 && only < total) {
        /* 子进程模式：单用例执行，退出码 0/1（崩溃由父进程按退出码判定）。 */
        bool ok;
        XPrintf("[%d/%d] %s ... \n", only + 1, total, k_tests[only].m_name);
        ok = k_tests[only].m_run();
        XPrintf("%s\n", ok ? "[通过]" : "[失败]");
        XUpgrade_deinit(); /* 幂等兜底 */
        XHttpServer_close(g_xup_server);
        XClassDelete((XClass*)g_xup_server);
        XClassDelete((XClass*)g_xup_pak);
        XClassDelete((XClass*)g_xup_manifest);
        if (k_tests[only].m_scratch) xtest_scratch_wipe(k_tests[only].m_scratch);
        XClassDelete(XCoreApplication_instance());
        return ok ? 0 : 1;
    }
    /* 父进程模式：逐用例以自身子进程隔离执行——引擎对象析构在当前实现下
     * 会崩溃（见 XUpgradeEngine_deinit 对 XMutex 误用 XClassDelete），隔离
     * 保证单用例故障不被放大为整套测试中止；子进程内仍是完整的进程内
     * XHttpServer + 引擎闭环。 */
    {
        char exe[1024];
        char index[16];
        if (!XSystem_executableFilePath(exe, sizeof(exe))) {
            XPrintf("[致命] 无法获取自身可执行路径（子进程隔离不可用）\n");
            XClassDelete((XClass*)g_xup_server);
            XClassDelete((XClass*)g_xup_pak);
            XClassDelete((XClass*)g_xup_manifest);
            XClassDelete(XCoreApplication_instance());
            return 250;
        }
        for (i = 0; i < total; ++i) {
            XString* program;
            XStringList* args;
            int rc;
            ++ran;
            XPrintf("[%d/%d] %s\n", i + 1, total, k_tests[i].m_name);
            XTEST_FMT(index, sizeof(index), "%d", i);
            program = XString_create_utf8(exe);
            args = XStringList_create();
            if (args) {
                XStringList_push_back_utf8(args, "--only");
                XStringList_push_back_utf8(args, index);
            }
            rc = (program && args) ? XProcess_execute_static(program, args) : -2;
            if (program) XClassDelete(program);
            if (args) XClassDelete((XClass*)args);
            k_tests[i].m_passed = (rc == 0);
            if (rc == 0) {
                ++passed;
                XPrintf("  [通过]（子进程退出码 0）\n");
            } else if (rc == -1) {
                XPrintf("  [失败]（子进程崩溃——进程被实现层错误终止，用例判失败）\n");
            } else if (rc == -2) {
                XPrintf("  [失败]（子进程启动失败）\n");
            } else {
                XPrintf("  [失败]（子进程退出码 %d）\n", rc);
            }
        }
    }
    XUpgrade_deinit(); /* 幂等兜底（父进程未初始化引擎，空操作） */
    XHttpServer_close(g_xup_server);
    XClassDelete((XClass*)g_xup_server);
    XClassDelete((XClass*)g_xup_pak);
    XClassDelete((XClass*)g_xup_manifest);
    /* 跑后删除全部 scratch。 */
    xtest_scratch_wipe(XUP_TEST_SCRATCH_UNIT);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_STORE);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_E2E);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_SKIP);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M1);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M2);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M3);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M4);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_M5);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_CKA);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_CKB);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_LIMIT);
    xtest_scratch_wipe(XUP_TEST_SCRATCH_LOCK);
    XPrintf("\n=== 结果汇总 ===\n");
    for (i = 0; i < total; ++i) {
        XPrintf("  %s %s\n", k_tests[i].m_passed ? "[通过]" : "[失败]", k_tests[i].m_name);
    }
    XPrintf("\n通过率: %d / %d（退出码=失败用例数，上限 250）\n", passed, ran);
    XClassDelete(XCoreApplication_instance());
    {
        int failures = ran - passed;
        return failures > 250 ? 250 : failures;
    }
}
