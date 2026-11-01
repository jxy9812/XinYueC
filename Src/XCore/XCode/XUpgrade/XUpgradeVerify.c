/**
 * @file       XUpgradeVerify.c
 * @brief      升级完整性校验与信任链：SHA-256 摘要/流式上下文、ECDSA P-256
 *             验签、信任公钥注入与防降级判定（设计文档 §4.1/§4.2）。
 * @details    能力连接点全模块各只有一个：SHA-256 走库内 XCryptographicHash
 *             门面（引擎侧 XUpgradeEngine.c 同口径）；canonical 字节串复用
 *             XUpgradeManifest_canonicalBytes（与 manifest 序列化同一函数，
 *             不在本模块复制）；ECDSA P-256 经 Library/mbedtls/platform 的
 *             XSsl 风格薄适配（XSsl_mbedtls_p256，PSA 客户端 API，本模块不
 *             直连 mbedtls 头，连接方式同 XUpgradePackage.c 对本文件
 *             sha256Digest 的本地 extern 声明先例）。信任公钥为模块级单一
 *             状态：缺省取编译期内嵌 XUpgrade_trust_pubkey.h（测试钥占位，
 *             生产替换点见该头），运行期经 XUpgradeVerify_setTrustKey 注入
 *             （XUpgrade.h 契约入口 XUpgrade_setTrustKey 透传本入口），验签
 *             时一律取 XUpgradeVerify_trustKey 当前值。防降级（§4.1）以纯
 *             判定入口收口：maxVersion 硬门 + rollback_to 窗口豁免 +
 *             min_version/policy_forced_below 强制锚；maxVersion 状态表与
 *             拦截时的事件/日志属引擎（XUpgradeEngine）职责。
 *
 *             测试侧造签配方（e2e，全部真实路径）：解析 manifest 得结构体 →
 *             XUpgradeManifest_canonicalBytes 取被签字节串 →
 *             XUpgradeVerify_sha256Digest 摘要 → 适配层造签原语
 *             XSsl_p256SignHashDer（以 XUpgrade_test_keys.h 的
 *             XUpgrade_testPrivateKey 私钥，本地 extern 声明，两侧签名一致）
 *             得 DER 签名 → XBase64_encode 填入 manifest 的 "sig" 字段；设备
 *             侧 XUpgrade_setTrustKey(XUpgrade_testPublicKeySec1, 65) 注入
 *             对应公钥后走引擎验签链路。
 */

#include "XUpgrade.h"        /* XUpgrade_config.h + XUpgradeError 错误语义 */
#include "xupgrade_shared.h" /* 模块内部共享小工具（xups_*） */
#include "XUpgradePackage.h" /* XUPGRADE_SHA256_BIN_SIZE：摘要长度常量 */
#include "XUpgradeManifest.h" /* manifest 结构、canonical 序列化与版本比较契约 */
#include "XUpgrade_trust_pubkey.h" /* 编译期内嵌缺省信任公钥（§4.2） */
#include "XMemory.h"         /* XMalloc_System/XFree_System/XMemcpy：统一分配器 */
#include "XCryptographicHash.h" /* SHA-256 门面：init/addData/resultView/reset */
#include "XBase64.h"         /* m_sig base64 解码门面（XUpgrade.h 先引入配置） */

#if XUPGRADE_ON

#if XUPGRADE_VERIFY_ON

/* ========================================================================== */
/*                     SHA-256 一次性摘要与流式上下文                          */
/* ========================================================================== */

/**
 * @brief SHA-256 流式校验上下文（对调用方不透明）。
 * @details 结构定义仅在本文件内可见；调用方经
 *           XUpgradeVerify_sha256Create/Delete 管理生命周期，经
 *           Begin/Update/End 驱动计算。m_hash 为门面 POD 值对象，由
 *           XCryptographicHash_* API 独占管理，禁止触碰其内部字段。
 */
typedef struct XUpgradeVerifySha256 {
    XCryptographicHash m_hash; /**< 门面流式哈希上下文；Create 时 init。 */
    bool m_active;             /**< 是否处于可 Update 的活动状态；End/Delete 后复位。 */
} XUpgradeVerifySha256;

/**
 * @brief 一次性计算 SHA-256 摘要。
 * @param data 输入数据；借用，len 为 0 时可为 NULL（按空内容计算）。
 * @param len 输入字节数。
 * @param outDigest 输出摘要缓冲区；调用方提供存储空间，容量不得小于
 *                  XUPGRADE_SHA256_BIN_SIZE（32 字节），不能为 NULL。
 * @return 成功返回 true；outDigest 为 NULL、data 为 NULL 且 len 非 0、或
 *         SHA-256 不可用/计算失败返回 false。
 * @note 整包/条目哈希比对（XUpgradePackage_verify）、P-256 验签被签字节串
 *       与本封装共用摘要口径；十六进制串转换属 manifest 层职责，不在本封
 *       装内。
 */
bool XUpgradeVerify_sha256Digest(const uint8_t* data, size_t len, uint8_t* outDigest)
{
    static const uint8_t empty = 0;
    XCryptographicHash hash;
    XByteArrayView view;

    if (!outDigest || (!data && len != 0)) {
        return false;
    }
    if (!XCryptographicHash_supportsAlgorithm(XCryptographicHash_Sha256)) {
        return false;                       /* 与流式入口同口径的可用性关卡 */
    }
    if (!data) {
        data = &empty;                      /* 门面 addData 要求非 NULL 输入指针 */
    }
    XCryptographicHash_init(&hash, XCryptographicHash_Sha256);
    XCryptographicHash_addData(&hash, (const char*)data, len);
    view = XCryptographicHash_resultView(&hash);
    if (XByteArrayView_size(&view) != XUPGRADE_SHA256_BIN_SIZE) {
        XCryptographicHash_deinit(&hash);
        return false;
    }
    XMemcpy(outDigest, XByteArrayView_data(&view), XUPGRADE_SHA256_BIN_SIZE);
    XCryptographicHash_deinit(&hash);
    return true;
}

/**
 * @brief 创建 SHA-256 流式校验上下文。
 * @return 新上下文句柄，调用者必须使用 XUpgradeVerify_sha256Delete 释放；
 *         内存分配失败或 SHA-256 不可用返回 NULL。
 * @note 句柄为不透明类型（struct 定义仅本文件可见），调用方以指针持有；
 *       创建后即可调用 XUpgradeVerify_sha256Begin 开始一轮计算。
 */
XUpgradeVerifySha256* XUpgradeVerify_sha256Create(void)
{
    XUpgradeVerifySha256* ctx;
    if (!XCryptographicHash_supportsAlgorithm(XCryptographicHash_Sha256)) {
        return NULL;
    }
    ctx = (XUpgradeVerifySha256*)XMalloc_System(sizeof(XUpgradeVerifySha256));
    if (!ctx) {
        return NULL;
    }
    XMemset(ctx, 0, sizeof(*ctx));
    XCryptographicHash_init(&ctx->m_hash, XCryptographicHash_Sha256);
    return ctx;
}

/**
 * @brief 释放 SHA-256 流式校验上下文。
 * @param ctx 待释放句柄；可为 NULL，为 NULL 时空操作。
 * @note 释放门面上下文缓存与结构体内存；End 之后调用同样安全。
 */
void XUpgradeVerify_sha256Delete(XUpgradeVerifySha256* ctx)
{
    if (!ctx) {
        return;
    }
    XCryptographicHash_deinit(&ctx->m_hash);
    ctx->m_active = false;
    XFree_System(ctx);
}

/**
 * @brief 开始一轮 SHA-256 流式计算。
 * @param ctx 流式上下文；不能为 NULL。
 * @return 成功返回 true；ctx 为 NULL 返回 false（上下文回到非活动态）。
 * @note 可重复调用：上下文已处于活动态（或上一轮已 End）时先复位再起一轮；
 *       一轮结束后（End）可再次 Begin 复用同一上下文，无需销毁重建。
 */
bool XUpgradeVerify_sha256Begin(XUpgradeVerifySha256* ctx)
{
    if (!ctx) {
        return false;
    }
    /* reset 清空已追加数据与结果缓存、保留算法设置（门面口径），活动态
     * 复用与 End 后重起一轮同一路径。 */
    XCryptographicHash_reset(&ctx->m_hash);
    ctx->m_active = true;
    return true;
}

/**
 * @brief 向流式计算追加一段数据。
 * @param ctx 流式上下文；不能为 NULL，且必须处于活动态（Begin 之后、End
 *            之前）。
 * @param data 数据块；借用，len 为 0 时可为 NULL。
 * @param len 数据块字节数；0 合法（空更新）。
 * @return 成功返回 true；ctx 为 NULL、非活动态或 data 为 NULL 且 len 非 0
 *         返回 false。
 * @note 分块口径与断点续传分块哈希游标（XUpgradeStateDownload.m_chunkSha256）
 *       一致：游标值取"截至当前已确认分块"的累计 SHA-256，而非单块哈希。
 */
bool XUpgradeVerify_sha256Update(XUpgradeVerifySha256* ctx, const uint8_t* data, size_t len)
{
    static const uint8_t empty = 0;

    if (!ctx || !ctx->m_active || (!data && len != 0)) {
        return false;
    }
    if (len == 0) {
        return true;                        /* 空更新为合法空操作 */
    }
    if (!data) {
        data = &empty;
    }
    XCryptographicHash_addData(&ctx->m_hash, (const char*)data, len);
    return true;
}

/**
 * @brief 结束流式计算并输出摘要。
 * @param ctx 流式上下文；不能为 NULL，且必须处于活动态。
 * @param outDigest 输出摘要缓冲区；调用方提供存储空间，容量不得小于
 *                  XUPGRADE_SHA256_BIN_SIZE（32 字节），不能为 NULL。
 * @return 成功返回 true；参数非法或门面计算失败返回 false。
 * @note 无论成败，本调用都会终结当前轮次并复位上下文，回到非活动态；再次
 *       计算须重新 Begin。
 */
bool XUpgradeVerify_sha256End(XUpgradeVerifySha256* ctx, uint8_t* outDigest)
{
    bool ok = false;

    if (!ctx || !ctx->m_active || !outDigest) {
        return false;
    }
    {
        XByteArrayView view = XCryptographicHash_resultView(&ctx->m_hash);
        if (XByteArrayView_size(&view) == XUPGRADE_SHA256_BIN_SIZE) {
            XMemcpy(outDigest, XByteArrayView_data(&view), XUPGRADE_SHA256_BIN_SIZE);
            ok = true;
        }
    }
    XCryptographicHash_reset(&ctx->m_hash);
    ctx->m_active = false;
    return ok;
}

#endif /* XUPGRADE_VERIFY_ON */

/* ========================================================================== */
/*                     信任公钥状态（§4.2 信任锚，模块单点）                    */
/* ========================================================================== */

/** @brief 当前信任公钥（SEC1 未压缩 65 字节）；未注入时持内嵌缺省钥副本。 */
static uint8_t s_xupgrade_verify_trustKey[XUPGRADE_TRUST_KEY_SEC1_SIZE];
/** @brief 缺省钥是否已拷入当前钥缓冲（避免非常量静态初始化）。 */
static bool s_xupgrade_verify_trustKeyLoaded = false;

/* 内嵌缺省钥与契约公钥长度必须一致（编译期断言，防两处常量漂移）。 */
typedef char xupgrade_verify_trustKeySizeCheck[
    (XUPGRADE_TRUST_PUBKEY_SEC1_SIZE == XUPGRADE_TRUST_KEY_SEC1_SIZE) ? 1 : -1];

/**
 * @brief 确保信任钥缓冲已初始化（首次访问时拷入内嵌缺省钥）。
 * @return 无。
 * @note 模块级懒初始化；引擎链路先 setTrustKey/init 后才进入验签，竞态
 *       窗口仅限"未初始化即并发验签"的错误用法，行为仍是确定值（缺省钥）。
 */
static void xupgrade_verify_trustKeyEnsure(void)
{
    if (!s_xupgrade_verify_trustKeyLoaded) {
        XMemcpy(s_xupgrade_verify_trustKey, XUpgrade_trustPubkeySec1,
                XUPGRADE_TRUST_PUBKEY_SEC1_SIZE);
        s_xupgrade_verify_trustKeyLoaded = true;
    }
}

/**
 * @brief 注入或复位 manifest 验签信任公钥（XUpgrade_setTrustKey 的实现位）。
 * @param publicKey 公钥字节串：SEC1 未压缩点 0x04||X||Y 共 65 字节，大端；
 *                  借用，仅本调用期间读取（内部复制）。传 NULL 清除注入、
 *                  恢复编译期内嵌缺省钥（XUpgrade_trust_pubkey.h）。
 * @param len 字节数；publicKey 非 NULL 时必须为 XUPGRADE_TRUST_KEY_SEC1_SIZE。
 * @return 注入成功返回 true；publicKey 非 NULL 但 len 不符或首字节非 0x04
 *         返回 false（原注入保持不变）。
 * @note 与 XUpgradeVerify_verifyP256 的 publicKey 参数同编码。注入自下一次
 *       验签生效，不影响进行中流程；引擎单工作线程执行验签，本入口应在
 *       XUpgrade_init 前或与验签串行的线程上调用。
 */
bool XUpgradeVerify_setTrustKey(const uint8_t* publicKey, uint32_t len)
{
    xupgrade_verify_trustKeyEnsure();
    if (!publicKey) {
        /* 复位：恢复编译期内嵌缺省信任钥 */
        XMemcpy(s_xupgrade_verify_trustKey, XUpgrade_trustPubkeySec1,
                XUPGRADE_TRUST_PUBKEY_SEC1_SIZE);
        return true;
    }
    if ((size_t)len != (size_t)XUPGRADE_TRUST_KEY_SEC1_SIZE || publicKey[0] != 0x04u) {
        return false;                       /* 非法注入：保持原信任钥不变 */
    }
    XMemcpy(s_xupgrade_verify_trustKey, publicKey, (size_t)len);
    return true;
}

/**
 * @brief 取当前生效的信任公钥（验签内部统一从这里取锚）。
 * @return 当前信任公钥的借用指针（SEC1 未压缩 65 字节，模块级存储，生命周期
 *         与进程相同）；调用方不得释放或修改，注入/复位操作会改写其内容。
 * @note 首次调用时以 XUpgrade_trust_pubkey.h 内嵌缺省钥初始化。
 */
const uint8_t* XUpgradeVerify_trustKey(void)
{
    xupgrade_verify_trustKeyEnsure();
    return s_xupgrade_verify_trustKey;
}

/* ========================================================================== */
/*                        ECDSA P-256 验签（§4.2 签名链）                      */
/* ========================================================================== */

/** @brief P-256 能力是否编入：需要 SHA-256 封装与适配层验签原语同时在场。 */
#if XUPGRADE_VERIFY_ON && XUPGRADE_VERIFY_P256_ON
#define XUPGRADE_VERIFY_P256_AVAILABLE 1
#else
#define XUPGRADE_VERIFY_P256_AVAILABLE 0
#endif

#if XUPGRADE_VERIFY_P256_AVAILABLE

/**
 * @brief 适配层连接点：ECDSA P-256/SHA-256 验签（DER 签名）。
 * @details 实现在 Library/mbedtls/platform/XSsl_mbedtls_p256.c（XSsl 风格薄
 *          适配，PSA 客户端 API；接口契约见同目录 XSsl_mbedtls_p256.h）。
 *          本模块禁止直连 mbedtls 头，按模块先例以本地 extern 声明连接
 *          （同 XUpgradePackage.c 对 XUpgradeVerify_sha256Digest 的声明方式），
 *          两侧签名必须保持一致。
 */
extern bool XSsl_p256VerifySha256Der(const uint8_t* publicKeySec1, size_t publicKeyLen,
                                     const uint8_t* sha256Digest, size_t sha256Len,
                                     const uint8_t* derSignature, size_t signatureLen);

#endif /* XUPGRADE_VERIFY_P256_AVAILABLE */

/**
 * @brief 对字节串执行 ECDSA P-256/SHA-256 验签。
 * @param content 被签字节串（canonical(manifest) 序列化结果，经
 *                XUpgradeManifest_canonicalBytes 取得）；借用，contentLen 为
 *                0 时可为 NULL。
 * @param contentLen content 字节数。
 * @param signature 签名字节串；借用，编码为 base64 解码后的 DER
 *                  ECDSA-Signature（SEQUENCE{r,s}，不超过 72 字节）。
 * @param signatureLen 签名字节数；[1, 72]。
 * @param publicKey 信任公钥字节串；借用，SEC1 未压缩点 0x04||X||Y 共 65 字
 *                  节（XUpgrade_setTrustKey 同编码；一般传
 *                  XUpgradeVerify_trustKey() 当前值）。
 * @param publicKeyLen 公钥字节数；必须为 XUPGRADE_TRUST_KEY_SEC1_SIZE。
 * @return 验签通过返回 XUpgradeError_Ok；参数/编码非法返回
 *         XUpgradeError_InvalidArgument；SHA-256 不可用返回
 *         XUpgradeError_Io；签名不符、DER/公钥点非法返回
 *         XUpgradeError_VerifyFailed；能力被宏裁剪（XUPGRADE_VERIFY_ON=0 或
 *         XUPGRADE_VERIFY_P256_ON=0）返回 XUpgradeError_NotSupported。
 * @note 步骤即 §4.2：content → SHA-256（XCryptographicHash 门面）→ P-256
 *       验签（适配层）。验签失败包含密码学不符与编码非法两类，调用方一律
 *       按"签名校验不通过"拒绝 manifest，不得区分放行。
 */
XUpgradeError XUpgradeVerify_verifyP256(const uint8_t* content, size_t contentLen,
                                        const uint8_t* signature, size_t signatureLen,
                                        const uint8_t* publicKey, size_t publicKeyLen)
{
#if XUPGRADE_VERIFY_P256_AVAILABLE
    uint8_t digest[XUPGRADE_SHA256_BIN_SIZE];

    if (!publicKey || publicKeyLen != (size_t)XUPGRADE_TRUST_KEY_SEC1_SIZE ||
        publicKey[0] != 0x04u ||
        !signature || signatureLen == 0u || signatureLen > 72u ||
        (!content && contentLen != 0u)) {
        return XUpgradeError_InvalidArgument;
    }
    if (!XUpgradeVerify_sha256Digest(content, contentLen, digest)) {
        return XUpgradeError_Io;            /* 摘要不可用属环境问题，非签名结论 */
    }
    return XSsl_p256VerifySha256Der(publicKey, publicKeyLen,
                                    digest, (size_t)XUPGRADE_SHA256_BIN_SIZE,
                                    signature, signatureLen)
               ? XUpgradeError_Ok : XUpgradeError_VerifyFailed;
#else
    /* 能力被裁剪（仅内部调试配置）；参数即 ABI 预留，恒返回 NotSupported。 */
    (void)content;
    (void)contentLen;
    (void)signature;
    (void)signatureLen;
    (void)publicKey;
    (void)publicKeyLen;
    return XUpgradeError_NotSupported;
#endif
}

#if XUPGRADE_VERIFY_P256_AVAILABLE

/* cstrLen 已收敛至 xupgrade_shared.h（xups_cstr_len）。 */

/**
 * @brief 对已解析的 manifest 执行整链验签（canonical → SHA-256 → P-256）。
 * @param manifest 已解析的 manifest；不能为 NULL。
 * @return 验签通过返回 XUpgradeError_Ok；manifest 为 NULL 返回
 *         XUpgradeError_InvalidArgument；m_sig 为空或 base64 非法返回
 *         XUpgradeError_VerifyFailed；canonical 序列化失败（含缓冲不足）
 *         返回 XUpgradeError_ManifestInvalid；内存分配失败返回
 *         XUpgradeError_Io；其余透传 XUpgradeVerify_verifyP256。
 * @note canonical 字节串经 XUpgradeManifest_canonicalBytes 取得——与引擎/
 *       服务端序列化同一函数，杜绝两份实现漂移（§4.2：被签内容=除 sig 外
 *       全部字段的 canonical 形态）。信任锚取 XUpgradeVerify_trustKey 当前
 *       值（注入或内嵌缺省）。本入口供引擎在 manifest 解析成功后调用；签名
 *       校验不通过时 manifest 必须整单拒绝。
 */
XUpgradeError XUpgradeVerify_verifyManifest(const XUpgradeManifest* manifest)
{
    uint8_t* canonical;
    uint8_t sigBin[96];                     /* 127 字符 base64 解码上限 95 字节 */
    size_t sigLen = 0;
    uint32_t canonicalLen = 0;
    size_t sigTextLen;
    XUpgradeError result;

    if (!manifest) {
        return XUpgradeError_InvalidArgument;
    }
    sigTextLen = xups_cstr_len(manifest->m_sig);
    if (sigTextLen == 0) {
        return XUpgradeError_VerifyFailed;  /* 无签名=整单拒绝（§4.2 信任链） */
    }
    canonical = (uint8_t*)XMalloc_System(XUPGRADE_MANIFEST_CANONICAL_MAX);
    if (!canonical) {
        return XUpgradeError_Io;
    }
    if (!XUpgradeManifest_canonicalBytes(manifest, canonical,
                                         (uint32_t)XUPGRADE_MANIFEST_CANONICAL_MAX,
                                         &canonicalLen)) {
        XFree_System(canonical);
        return XUpgradeError_ManifestInvalid;
    }
    sigLen = sizeof(sigBin);
    if (XBase64_decode(manifest->m_sig, sigTextLen, sigBin, &sigLen) != 0) {
        XFree_System(canonical);
        return XUpgradeError_VerifyFailed;  /* base64 非法（含容量不足） */
    }
    result = XUpgradeVerify_verifyP256(canonical, (size_t)canonicalLen,
                                       sigBin, sigLen,
                                       XUpgradeVerify_trustKey(),
                                       (size_t)XUPGRADE_TRUST_KEY_SEC1_SIZE);
    XFree_System(canonical);
    return result;
}

#else /* XUPGRADE_VERIFY_P256_AVAILABLE */

/**
 * @brief 对已解析的 manifest 执行整链验签（能力裁剪配置下恒 NotSupported）。
 * @param manifest manifest 结构体；借用，可为 NULL（裁剪配置下不使用）。
 * @return 恒返回 XUpgradeError_NotSupported。
 * @note XUPGRADE_VERIFY_ON=0 或 XUPGRADE_VERIFY_P256_ON=0（仅内部调试配置）
 *       时验签能力整体缺席；发布配置两者恒为 1。
 */
XUpgradeError XUpgradeVerify_verifyManifest(const XUpgradeManifest* manifest)
{
    (void)manifest;
    return XUpgradeError_NotSupported;
}

#endif /* XUPGRADE_VERIFY_P256_AVAILABLE */

/* ========================================================================== */
/*                     防降级与强制锚判定（§4.1，纯判定）                      */
/* ========================================================================== */

/**
 * @brief 防降级判定：目标版本是否允许越过 maxVersion 门槛（§4.1/§4.2.4）。
 * @param candidateVersion 待升级目标版本串；UTF-8 借用，NULL 按空串处理。
 * @param maxVersion 本地记录的历史最高已激活版本（引擎 state.json 防降级
 *                   表）；UTF-8 借用，NULL 或空串表示无基线（状态文件被清
 *                   除后由引擎以当前运行版本重建，本判定放行）。
 * @param rollbackTo 签名的回滚窗口地板版本（manifest.rollback_to）；UTF-8
 *                   借用，NULL 或空串表示不允许任何降级豁免。
 * @return 允许返回 true（非降级、无基线或落在豁免范围）；拦截返回 false
 *         （引擎映射为 XUpgradeError_DowngradeRejected 并记日志/事件）。
 * @note 判定规则（与设计文档 §4.2.4、XUpgradeManifest.h rollback_to 注释、
 *       XUpgradeEngine.h m_maxVersion 注释三处口径一致，跨版本不得变化）：
 *       1) maxVersion 为空 → 放行（全新设备由引擎重建基线）；
 *       2) candidate >= maxVersion → 放行（非降级）；
 *       3) candidate < maxVersion 且 rollbackTo 为空 → 拦截（硬门）；
 *       4) candidate < maxVersion 且 maxVersion < rollbackTo → 放行（本地
 *          记录低于签名的回滚地板时不再拦截）；
 *       5) 其余（candidate < maxVersion 且 candidate > rollbackTo）→ 拦截；
 *          candidate <= rollbackTo 视为签名明确允许的回滚窗口内，放行。
 *       纯函数无副作用；maxVersion 表的维护（激活成功后抬升）属引擎职责。
 */
bool XUpgradeVerify_antiDowngradeAllowed(const char* candidateVersion,
                                         const char* maxVersion,
                                         const char* rollbackTo)
{
    if (!maxVersion || maxVersion[0] == '\0') {
        return true;                        /* 无基线：引擎重建后自然放行 */
    }
    if (!candidateVersion ||
        XUpgradeManifest_compareVersions(candidateVersion, maxVersion) >= 0) {
        return true;                        /* 非降级 */
    }
    if (!rollbackTo || rollbackTo[0] == '\0') {
        return false;                       /* 硬门：无签名回滚窗口 */
    }
    if (XUpgradeManifest_compareVersions(maxVersion, rollbackTo) < 0) {
        return true;                        /* 基线低于回滚地板：不再拦截 */
    }
    /* 签名回滚窗口：目标不高于地板版本时豁免，高于地板的降级一律拦截 */
    return XUpgradeManifest_compareVersions(candidateVersion, rollbackTo) <= 0;
}

/* §10 强制锚判定（min_version/policy_forced_below 双锚）的唯一实现位于
 * XUpgradeGray.c 的 XUpgradeGray_forcedAnchored（契约位，引擎消费）；本
 * 文件曾有一份逐行等价副本（XUpgradeVerify_forcedAnchorActive），因双实
 * 现口径漂移风险已按去重裁定删除，纯函数判定表改测契约位入口。 */

#endif /* XUPGRADE_ON */
