/**
 * @file       XUpgradeIdentity.c
 * @brief      升级模块设备标识与当前版本提供者回调契约实现。
 * @details    XUpgradeIdentity.h 契约的实现侧：进程内静态保存版本/序列号两个
 *             提供者回调（借用指针，不复制 userData，不加锁——契约规定回调
 *             应在单线程初始化阶段注册，注册完成后才允许并发调用，模式同
 *             XUpgrade.c 的监听器暂存）。未注册版本提供者时回退编译期缺省值
 *             XUPGRADE_PRODUCT_VERSION（可由 CMake -DXUPGRADE_PRODUCT_VERSION=
 *             "x.y.z" 覆盖）。灰度设备键（§11）按缺省回退链取值：SN → 平台
 *             主网卡 MAC（12 位小写十六进制，经 XNetworkInterface 库内抽象）
 *             → 持久 UUID（首次生成经 XSaveFile 原子写入安装根下
 *             XUPGRADE_FILE_DEVICE_KEY，安装根=可执行文件所在目录，与引擎
 *             xup_derive_root 同口径）；键串白名单 [0-9A-Za-z-]。
 */
#include "XUpgradeIdentity.h"

#include "XSystem.h"           /* XSystem_executableFilePath：安装根推导。 */
#include "XString.h"           /* 路径组装与 XNetworkInterface 硬件地址访问。 */
#include "XFile.h"             /* device.key 读取（XFile 只读打开）。 */
#include "XSaveFile.h"         /* device.key 原子写入（契约 §11 明示入口）。 */
#include "XIODevice.h"         /* XIODevice_ReadOnly/size/read。 */
#include "XNetworkInterface.h" /* MAC 级：主网卡硬件地址（Drive 适配在上层）。 */
#include "XVector.h"           /* allInterfaces 返回容器访问。 */
#include "XRandomGenerator.h"  /* 持久 UUID 生成（fillSecure）。 */

#if XUPGRADE_ON

/* ========================================================================== */
/*                          提供者回调注册表（进程内单份）                      */
/* ========================================================================== */

/** @brief 当前版本提供者回调；NULL=未注册（走编译期缺省值）。 */
static XUpgradeVersionProvider g_xupgrade_versionProvider = NULL;
/** @brief 版本提供者回调的用户上下文（借用，原样回传）。 */
static void* g_xupgrade_versionData = NULL;
/** @brief 设备序列号提供者回调；NULL=未注册（M1 视作无序列号）。 */
static XUpgradeSerialProvider g_xupgrade_serialProvider = NULL;
/** @brief 序列号提供者回调的用户上下文（借用，原样回传）。 */
static void* g_xupgrade_serialData = NULL;

void XUpgrade_setVersionProvider(XUpgradeVersionProvider handler, void* userData)
{
    if (!handler) {
        /* 传 NULL 清除注册并恢复编译期缺省值语义（version 走 fallback 分支）。 */
        g_xupgrade_versionProvider = NULL;
        g_xupgrade_versionData = NULL;
        return;
    }
    g_xupgrade_versionProvider = handler;
    g_xupgrade_versionData = userData;
}

void XUpgrade_setSerialProvider(XUpgradeSerialProvider handler, void* userData)
{
    if (!handler) {
        g_xupgrade_serialProvider = NULL;
        g_xupgrade_serialData = NULL;
        return;
    }
    g_xupgrade_serialProvider = handler;
    g_xupgrade_serialData = userData;
}

bool XUpgradeIdentity_version(char* out, uint32_t cap)
{
    uint32_t i;
    const char* fallback;
    if (!out || cap == 0) return false;
    if (g_xupgrade_versionProvider) {
        /* 已注册提供者：以回调结果为准。回调可能违约不写缓冲——先清空再调
         * （2026-10-08 评审：原实现仅末字节兜底，违约时放行未定义内容）。 */
        out[0] = '\0';
        g_xupgrade_versionProvider(g_xupgrade_versionData, out, cap);
        out[cap - 1] = '\0';
        if (out[0] == '\0') return false; /* 回调违约写空=版本未知 */
        return true;
    }
    /* 未注册：回退编译期缺省值；缺省值被覆盖为空串视作"版本未知"。 */
    fallback = XUPGRADE_PRODUCT_VERSION;
    if (!fallback || !fallback[0]) return false;
    for (i = 0; i + 1 < cap && fallback[i] != '\0'; ++i) out[i] = fallback[i];
    out[i] = '\0';
    return true;
}

bool XUpgradeIdentity_serial(char* out, uint32_t cap)
{
    if (!out || cap == 0) return false;
    if (!g_xupgrade_serialProvider) return false;   /* 未注册不回退推导。 */
    g_xupgrade_serialProvider(g_xupgrade_serialData, out, cap);
    out[cap - 1] = '\0';
    /* 契约（XUpgradeIdentity.h:119-121）：未注册或回调返回空串均返回
     * false——deviceKey 回退链据此跳过 SN 级，不落"空键"分桶。 */
    return out[0] != '\0';
}

/* ========================================================================== */
/*                       灰度设备键（§11 回退链实现）                           */
/* ========================================================================== */

/** @brief 判断字符是否在设备键白名单 [0-9A-Za-z-] 内。 */
static bool xupidentity_keyCharAllowed(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') || c == '-';
}

/**
 * @brief 校验候选设备键：非空、不超容量且全部字符在白名单内。
 * @param key 候选键；借用。
 * @return 合法返回 true。
 * @note 键串白名单 [0-9A-Za-z-]（XUpgradeIdentity.h:138）；盘上文件内容
 *       同样经此把关，防被外部改写为异形键后污染灰度分桶。
 */
static bool xupidentity_keyValid(const char* key)
{
    size_t i;
    size_t n = 0;
    if (!key) return false;
    while (key[n] != '\0') ++n;
    if (n == 0 || n >= XUPGRADE_DEVICE_KEY_MAX) return false;
    for (i = 0; i < n; ++i) {
        if (!xupidentity_keyCharAllowed(key[i])) return false;
    }
    return true;
}

/** @brief 把已取得的设备键拷入调用方缓冲（安全截断），返回是否取得非空键。 */
static bool xupidentity_emitKey(char* out, uint32_t cap, const char* key)
{
    uint32_t i;
    if (!out || cap == 0) return false;
    for (i = 0; i + 1 < cap && key[i] != '\0'; ++i) out[i] = key[i];
    out[i] = '\0';
    return out[0] != '\0';
}

/**
 * @brief 安装根推导：可执行文件所在目录（引擎 xup_derive_root 同口径）。
 * @details device.key 与 pointers/ 同级、位于安装根直下（不属于 update/
 *          工作区，§16 不变式 5）；身份层先于引擎存在，无法借用引擎内部
 *          推导，按同一规则独立求值——桌面部署形态两者一致。
 */
static bool xupidentity_rootDir(char* root, size_t cap)
{
    char exe[1024];
    size_t i;
    size_t lastSlash = 0;
    bool hasSlash = false;
    if (!root || cap == 0) return false;
    if (!XSystem_executableFilePath(exe, sizeof(exe))) return false;
    for (i = 0; exe[i] != '\0'; ++i) {
        if (exe[i] == '/' || exe[i] == '\\') {
            lastSlash = i;
            hasSlash = true;
        }
    }
    if (!hasSlash || lastSlash == 0) return false;
    if (lastSlash >= cap) return false;
    for (i = 0; i < lastSlash; ++i) root[i] = exe[i];
    root[lastSlash] = '\0';
    return true;
}

/** @brief 组装 root/key 文件路径到调用方缓冲。 */
static bool xupidentity_keyFilePath(char* path, size_t cap, const char* root)
{
    size_t i = 0;
    size_t j;
    size_t rootLength = 0;
    size_t nameLength = 0;
    if (!path || cap == 0 || !root) return false;
    while (root[rootLength] != '\0') ++rootLength;
    while (XUPGRADE_FILE_DEVICE_KEY[nameLength] != '\0') ++nameLength;
    if (rootLength == 0) return false;
    if (rootLength + 1 + nameLength + 1 > cap) return false;
    for (j = 0; j < rootLength; ++j) path[i++] = root[j];
    path[i++] = '/';
    for (j = 0; j < nameLength; ++j) path[i++] = XUPGRADE_FILE_DEVICE_KEY[j];
    path[i] = '\0';
    return true;
}

/** @brief 读盘上已持久化的设备键（不存在或异形内容视作未持久化）。 */
static bool xupidentity_readStoredKey(char* key, size_t cap)
{
    char root[1024];
    char path[1100];
    XString* pathString;
    XFile* file;
    int64_t size;
    int64_t got;
    bool ok = false;
    if (cap < XUPGRADE_DEVICE_KEY_MAX) return false;
    key[0] = '\0';
    if (!xupidentity_rootDir(root, sizeof(root))) return false;
    if (!xupidentity_keyFilePath(path, sizeof(path), root)) return false;
    pathString = XString_create_utf8(path);
    if (!pathString) return false;
    file = XFile_create_2(pathString);
    XClassDelete((XClass*)pathString);
    if (!file) return false;
    if (XFile_open_2(file, XIODevice_ReadOnly, 0)) {
        size = XIODevice_size_base((XIODevice*)file);
        if (size >= 0 && size < (int64_t)cap) {
            got = size > 0 ? XIODevice_read_1((XIODevice*)file, key, size) : 0;
            if (got == size) key[size] = '\0';
            else key[0] = '\0';
        }
        XIODevice_close_base((XIODevice*)file);
    }
    XClassDelete((XClass*)file);
    ok = xupidentity_keyValid(key);
    if (!ok) key[0] = '\0';
    return ok;
}

/** @brief 经 XSaveFile 原子写入设备键（§11 契约明示入口；崩溃不变式）。 */
static bool xupidentity_writeStoredKey(const char* key)
{
    char root[1024];
    char path[1100];
    size_t length = 0;
    XString* pathString;
    XSaveFile* save;
    bool ok;
    if (!xupidentity_keyValid(key)) return false;
    if (!xupidentity_rootDir(root, sizeof(root))) return false;
    if (!xupidentity_keyFilePath(path, sizeof(path), root)) return false;
    while (key[length] != '\0') ++length;
    pathString = XString_create_utf8(path);
    if (!pathString) return false;
    save = XSaveFile_create_2(pathString);
    XClassDelete((XClass*)pathString);
    if (!save) return false;
    ok = XIODevice_open_base((XIODevice*)save, XIODevice_WriteOnly);
    if (ok && length > 0)
        ok = XIODevice_write_1((XIODevice*)save, key, (int64_t)length) == (int64_t)length;
    if (ok) ok = XSaveFile_commit(save);
    else XSaveFile_cancelWriting(save);
    XClassDelete((XClass*)save);
    return ok;
}

/**
 * @brief 生成 RFC 4122 v4 形态 UUID（8-4-4-4-12，连字符在键白名单内）。
 * @details 随机源 XRandomGenerator_fillSecure（安全种子填充）。
 */
static bool xupidentity_generateUuid(char* key, size_t cap)
{
    static const char hexDigits[] = "0123456789abcdef";
    uint8_t bytes[16];
    size_t i;
    size_t pos = 0;
    if (cap < 37) return false;
    if (!XRandomGenerator_fillSecure(bytes, sizeof(bytes))) return false;
    bytes[6] = (uint8_t)((bytes[6] & 0x0Fu) | 0x40u); /* 版本 4 */
    bytes[8] = (uint8_t)((bytes[8] & 0x3Fu) | 0x80u); /* 变体 10 */
    for (i = 0; i < sizeof(bytes); ++i) {
        if (i == 4u || i == 6u || i == 8u || i == 10u) key[pos++] = '-';
        key[pos++] = hexDigits[bytes[i] >> 4];
        key[pos++] = hexDigits[bytes[i] & 0x0Fu];
    }
    key[pos] = '\0';
    return true;
}

/**
 * @brief MAC 级：首个 up+running 非 loopback 接口的硬件地址。
 * @details 经 XNetworkInterface 库内抽象取值（Drive 平台适配在其下），
 *          身份层不直连平台 API。格式 "aa:bb:.." 滤除分隔符折小写 →
 *          12 位十六进制；地址缺失或形态异常（非 12 个十六进制位）跳过
 *          该接口。
 */
static bool xupidentity_macKey(char* key, size_t cap)
{
    XVector* interfaces;
    int64_t count;
    int64_t i;
    if (!key || cap < 13u) return false;
    key[0] = '\0';
    interfaces = XNetworkInterface_allInterfaces();
    if (!interfaces) return false;
    count = (int64_t)XVector_size_base((const XContainer*)interfaces);
    for (i = 0; i < count; ++i) {
        const XNetworkInterface* iface = (const XNetworkInterface*)XVector_at_base(interfaces, i);
        const XString* hardware;
        const char* utf8;
        uint32_t written = 0;
        size_t j;
        if (!iface || !XNetworkInterface_isValid(iface) ||
            XNetworkInterface_isLoopBack(iface) ||
            !XNetworkInterface_isUp(iface) ||
            !XNetworkInterface_isRunning(iface))
            continue;
        hardware = XNetworkInterface_hardwareAddress_const(iface);
        utf8 = hardware ? XString_toUtf8(hardware) : NULL;
        if (!utf8) continue;
        for (j = 0; utf8[j] != '\0'; ++j) {
            char c = utf8[j];
            int value;
            if (c >= '0' && c <= '9') value = c - '0';
            else if (c >= 'a' && c <= 'f') value = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') value = c - 'A' + 10;
            else continue;                  /* 分隔符 ':' 等 */
            if (written >= 12u) { written = 0; break; } /* 形态异常：>12 hex */
            key[written++] = "0123456789abcdef"[value];
        }
        if (written == 12u) {
            key[written] = '\0';
            XClassDelete(interfaces);
            return true;
        }
        key[0] = '\0';
    }
    XClassDelete(interfaces);
    return false;
}

bool XUpgradeIdentity_deviceKey(char* out, uint32_t cap)
{
    char key[XUPGRADE_DEVICE_KEY_MAX];
    if (!out || cap == 0) return false;
    /* 回退链每次调用按序求值（契约 XUpgradeIdentity.h:134-138）：
     * 1) SN（已注册提供者且返回非空）；2) 平台主网卡 MAC；3) 持久 UUID
     * （读 device.key，无则生成并原子写入）。键串白名单 [0-9A-Za-z-]。 */
    if (XUpgradeIdentity_serial(key, (uint32_t)sizeof(key)) && xupidentity_keyValid(key))
        return xupidentity_emitKey(out, cap, key);
    if (xupidentity_macKey(key, sizeof(key)))
        return xupidentity_emitKey(out, cap, key);
    if (xupidentity_readStoredKey(key, sizeof(key)))
        return xupidentity_emitKey(out, cap, key);
    if (xupidentity_generateUuid(key, sizeof(key)) &&
        xupidentity_writeStoredKey(key))
        return xupidentity_emitKey(out, cap, key);
    return false;                           /* 回退链全部不可用：灰度按未命中 */
}

#endif /* XUPGRADE_ON */
