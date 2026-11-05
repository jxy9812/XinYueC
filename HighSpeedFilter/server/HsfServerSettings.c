
/**
 * @file       HsfServerSettings.c
 * @brief      服务端设置持久化门面实现（XSettings 单例 + 程序根目录回落）。
 * @details    目录口径与客户端 HsfSettingsStore 一致（cfg/ → 程序目录根），
 *             不做用户主目录第三级（服务端无人值守场景，宁可明说失败）。
 */
#include "HsfServerSettings.h"

#include "XCoreApplication.h"
#include "XSettings.h"
#include "XDir.h"
#include "XString.h"
#include "XVariant.h"
#include "XPrintf.h"
#include <stdio.h>
#include <string.h>

/** @brief 配置子目录名（与客户端 cfg/ 同口径）。 */
#define HSF_SRV_CFG_DIR_NAME   "cfg"
/** @brief 配置文件名。 */
#define HSF_SRV_CFG_FILE_NAME  "hsf_server.ini"

/** @brief 唯一 XSettings 实例（XSettings 无锁，多实例内存态互不可见）。 */
static XSettings* s_store = NULL;
/** @brief 生效配置文件路径（诊断/日志用）。 */
static char s_path[512] = { 0 };
/** @brief 数据目录（配置文件所在目录；未打开时=程序目录）。 */
static char s_dataDir[512] = { 0 };

/* ==================== 内部工具 ==================== */

/**
 * @brief 确保目录存在（递归建；已存在视为成功）。
 * @param dirPath 目录绝对路径。
 */
static bool hsfSrvEnsureDir(const char* dirPath)
{
    XString* dirName = NULL;
    XDir* dir = NULL;
    bool ok = false;

    if (!dirPath || !dirPath[0]) return false;
    dirName = XString_create_utf8(dirPath);
    if (!dirName) return false;
    dir = XDir_create_2(dirName);
    if (dir) {
        ok = XDir_mkpath(dir, dirName);
        XClassDelete(dir);
    }
    XClassDelete(dirName);
    return ok;
}

/**
 * @brief 用显式路径建实例（文件不存在=空配置，合法）。
 * @param path 配置文件绝对路径。
 * @return 实例指针；NULL=创建失败。
 */
static XSettings* hsfSrvCreateAt(const char* path)
{
    XSettings* store;
    if (!path || !path[0]) return NULL;
    store = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    return store;
}

/* ==================== 公开 API ==================== */

bool HsfServerSettings_open(void)
{
    const XString* appDir = NULL;
    const char* dirUtf8 = NULL;
    char dirBuf[400];
    char fileBuf[512];

    if (s_store) return true;

    /* 程序运行根目录（对齐客户端数据目录口径）。 */
    appDir = XCoreApplication_applicationDirPath();
    dirUtf8 = appDir ? XString_toUtf8(appDir) : NULL;
    if (!dirUtf8) dirUtf8 = "";
    snprintf(s_dataDir, sizeof(s_dataDir), "%s", dirUtf8);

    /* ---- 第一级：<程序目录>/cfg/ ---- */
    if (dirUtf8[0]) {
        snprintf(dirBuf, sizeof(dirBuf), "%s/%s", dirUtf8, HSF_SRV_CFG_DIR_NAME);
        if (hsfSrvEnsureDir(dirBuf)) {
            snprintf(fileBuf, sizeof(fileBuf), "%s/%s", dirBuf,
                     HSF_SRV_CFG_FILE_NAME);
            s_store = hsfSrvCreateAt(fileBuf);
            if (s_store) {
                snprintf(s_path, sizeof(s_path), "%s", fileBuf);
                snprintf(s_dataDir, sizeof(s_dataDir), "%s", dirBuf);
                XPrintf("HsfServer: 设置文件 %s\n", s_path);
            }
        }
    }

    /* ---- 第二级：<程序目录>/（建不了子目录时） ---- */
    if (!s_store && dirUtf8[0]) {
        snprintf(fileBuf, sizeof(fileBuf), "%s/%s", dirUtf8,
                 HSF_SRV_CFG_FILE_NAME);
        s_store = hsfSrvCreateAt(fileBuf);
        if (s_store) {
            snprintf(s_path, sizeof(s_path), "%s", fileBuf);
            XPrintf("HsfServer: 设置文件 %s（程序目录根）\n", s_path);
        }
    }

    if (!s_store) {
        XPrintf("HsfServer: 设置文件不可用，本次按默认值运行\n");
    }
    if (appDir)
        XClassDelete((XClass*)appDir);
    return s_store != NULL;
}

void HsfServerSettings_close(void)
{
    if (s_store) {
        XSettings_sync(s_store);
        XClassDelete((XClass*)s_store);
        s_store = NULL;
    }
}

bool HsfServerSettings_isOpen(void)
{
    return s_store != NULL;
}

const char* HsfServerSettings_path(void)
{
    return s_path;
}

const char* HsfServerSettings_dataDir(void)
{
    return s_dataDir;
}

bool HsfServerSettings_contains(const char* key)
{
    if (!s_store || !key) return false;
    return XSettings_contains_utf8(s_store, key);
}

int HsfServerSettings_getInt(const char* key, int def)
{
    XVariant* v;
    int out = def;

    if (!s_store || !key) return def;
    v = XSettings_value_utf8(s_store, key);
    if (v) {
        /* INI 后端读回多为 String：XVariant_toInt 对 String 有解析回退。 */
        out = XVariant_toInt(v);
        XClassDelete((XClass*)v);
    }
    return out;
}

void HsfServerSettings_setInt(const char* key, int value)
{
    XVariant* v;
    if (!s_store || !key) return;
    v = XVariant_create_int(value);
    if (v) {
        XSettings_setValue_utf8(s_store, key, v);
        XClassDelete((XClass*)v);
        /* 立即落盘：本门面没有"空闲自动 sync"钩子，写入后不 sync 就只在
         * 内存里，进程退出即丢（2026-11-04 实锅：运行模式重启后回手动）。 */
        XSettings_sync(s_store);
    }
}

/* ==================== 字符串键 ==================== */

void HsfServerSettings_getString(const char* key, const char* def,
                                 char* buf, int cap)
{
    XVariant* v;
    const char* out = NULL;

    if (!buf || cap <= 0) return;
    buf[0] = '\0';
    if (!def) def = "";

    if (!s_store || !key) {
        snprintf(buf, (size_t)cap, "%s", def);
        return;
    }
    v = XSettings_value_utf8(s_store, key);
    if (!v) {
        snprintf(buf, (size_t)cap, "%s", def);
        return;
    }
    /* [类型退化] 正常读回是 String；现场把 host 写成数字也会读到数值类型。
     * 非 String 时按数值转文本，不能当"没配置"处理（否则配置写了却不生效）。 */
    if (XVariant_type(v) == XVariantType_String) {
        /* fromVariant_ref 返回**变体内部**的借用指针（不新建对象，禁删）。 */
        XString* s = XString_fromVariant_ref(v);
        out = s ? XString_toUtf8(s) : NULL;
    }
    if (out && out[0]) {
        snprintf(buf, (size_t)cap, "%s", out);
    } else if (XVariant_type(v) == XVariantType_String) {
        snprintf(buf, (size_t)cap, "%s", def);
    } else {
        snprintf(buf, (size_t)cap, "%d", XVariant_toInt(v));
    }
    XClassDelete((XClass*)v);
}

void HsfServerSettings_setString(const char* key, const char* value)
{
    XVariant* v;
    if (!s_store || !key) return;
    v = XString_toVariant_utf8(value ? value : "");
    if (v) {
        XSettings_setValue_utf8(s_store, key, v);
        XClassDelete((XClass*)v);
        XSettings_sync(s_store);   /* 与 setInt 同款：写完立即落盘。 */
    }
}

bool HsfServerSettings_seedInt(const char* key, int def)
{
    if (!s_store || !key) return false;
    if (XSettings_contains_utf8(s_store, key)) return false;
    HsfServerSettings_setInt(key, def);
    return true;
}

bool HsfServerSettings_seedString(const char* key, const char* def)
{
    if (!s_store || !key) return false;
    if (XSettings_contains_utf8(s_store, key)) return false;
    HsfServerSettings_setString(key, def);
    return true;
}

/* ==================== 监听端口 ==================== */
/**
 * @brief 端口合法性（1~65535；0 是「用缺省」的哨兵值，不落盘）。
 */
static bool hsfSrvPortValid(int port)
{
    return port > 0 && port <= 65535;
}

uint16_t HsfServerSettings_resolvePort(void)
{
    int port;

    if (!s_store) return (uint16_t)HSF_XLINK_DEFAULT_PORT;

    /* 键不存在 = 首次运行：把缺省端口落盘，现场直接改这一个键。
     * 已存在但值非法（手工改成 0/负数/越界）→ 回落缺省并在下次写入时纠正。 */
    if (!HsfServerSettings_contains(HSF_SRV_KEY_PORT)) {
        HsfServerSettings_setInt(HSF_SRV_KEY_PORT,
                                 (int)HSF_XLINK_DEFAULT_PORT);
        return (uint16_t)HSF_XLINK_DEFAULT_PORT;
    }
    port = HsfServerSettings_getInt(HSF_SRV_KEY_PORT,
                                    (int)HSF_XLINK_DEFAULT_PORT);
    if (!hsfSrvPortValid(port)) {
        XPrintf("HsfServer: 配置端口非法（%d），回落 %u\n",
                port, (unsigned)HSF_XLINK_DEFAULT_PORT);
        return (uint16_t)HSF_XLINK_DEFAULT_PORT;
    }
    return (uint16_t)port;
}
