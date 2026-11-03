/**
 * @file       HsfSettingsStore.c
 * @brief      高速过滤器客户端界面设置持久化门面（XSettings 单例封装）。
 * @details    路径口径（逐级回落，取第一个可写的）：
 *               1) <程序目录>/cfg/hsf_client.ini —— 主用。沿用仓库既有
 *                  部署约定（docs/cross-compile-rk3506.md:114 已为 demo
 *                  预留 cfg/ 目录）；板端 /data/cfg 与 exe 同级，便于随包
 *                  升级/备份。
 *               2) <程序目录>/hsf_client.ini —— 程序目录可写但不能建子目录
 *                  时（只读介质挂载点的常见形态）。
 *               3) XSettings 组织/应用名派生路径 —— Windows
 *                  %APPDATA%/XinYueC/HsfClient.ini、Linux
 *                  ~/.config/XinYueC/HsfClient.ini（需先设组织/应用名，
 *                  否则 XSettings 构造即 AccessError）。
 *             键名带 '/' 由框架自动落成 INI 分节（XSettings.c:556：
 *             「含 '/' 的键找名字为其前缀的最长节」）→ 形如 [Window] / [Overlay]。
 */
#include "HsfSettingsStore.h"

#include "XSettings.h"
#include "XVariant.h"
#include "XString.h"
#include "XClass.h"
#include "XCoreApplication.h"
#include "XFile.h"
#include "XFileInfo.h"
#include "XDir.h"
#include "XPrintf.h"
#include <stdio.h>
#include <string.h>

/** @brief 配置文件名（放在 cfg/ 子目录下）。 */
#define HSF_CFG_FILE_NAME "hsf_client.ini"

/** @brief 配置子目录名（程序目录下的同级目录，便于随包备份/清理）。 */
#define HSF_CFG_DIR_NAME "cfg"

/** @brief 回落路径用的组织名/应用名（XSettings 派生路径的输入）。 */
#define HSF_CFG_ORG_NAME "XinYueC"
#define HSF_CFG_APP_NAME "HsfClient"

/* ==================== 模块内状态 ==================== */

static XSettings* s_store = NULL;   /**< 唯一实例（无内部锁，多实例写入互不可见）。 */
static char       s_path[512] = {0}; /**< 当前生效路径（诊断用）。 */
static bool       s_enabled = true;  /**< 全局开关（--no-settings 时置 false）。 */

/* ==================== 内部辅助 ==================== */

/**
 * @brief 变体类型是否可当整数读。
 * @details 键不存在时 XSettings 返回 XVariantType_NULL 变体（不是 NULL
 *          指针）——若不判类型，XVariant_toInt 会把它读成 0，**默认值得
 *          到的值被静默吞掉**（0 对多数开关即「关」→ 界面被静默改成
 *          另一种状态）。故显式判类型：非数值类型一律回落默认值。
 */
static bool hsf_storeIsNumeric(int type)
{
    switch (type) {
    case XVariantType_Uint8:
    case XVariantType_Uint16:
    case XVariantType_Uint32:
    case XVariantType_Uint64:
    case XVariantType_Int8:
    case XVariantType_Int16:
    case XVariantType_Int32:
    case XVariantType_Int64:
    case XVariantType_Bool:
    case XVariantType_Char:
    case XVariantType_UChar:
    case XVariantType_Int:
    case XVariantType_Size_t:
    case XVariantType_Float:
    case XVariantType_Double:
        return true;
    default:
        return false;
    }
}

/**
 * @brief 探测目录是否可写（在目标目录建一个临时文件再删掉）。
 * @param dirPath 目录绝对路径。
 * @return 可写返回 true。
 */
static bool hsf_storeProbeWritable(const char* dirPath)
{
    char tmpPath[600];
    XString* name = NULL;
    XFile* probe = NULL;
    bool ok = false;

    if (!dirPath || !dirPath[0]) return false;
    snprintf(tmpPath, sizeof(tmpPath), "%s/.hsf_probe_%d", dirPath,
             (int)XCoreApplication_applicationPid());
    name = XString_create_utf8(tmpPath);
    if (!name) return false;
    probe = XFile_create_2(name);
    if (probe) {
        if (XFile_open_2(probe,
                         (XIODeviceBaseMode)(XIODevice_WriteOnly
                                             | XIODevice_NewOnly),
                         XFile_PermsOwner)) {
            XFile_close_base(probe);
            ok = true;
        }
        XClassDelete(probe);
    }
    XClassDelete(name);
    if (ok) {
        XString* rm = XString_create_utf8(tmpPath);
        if (rm) {
            XFile_remove_static(rm);
            XClassDelete(rm);
        }
    }
    return ok;
}

/**
 * @brief 确保目录存在（递归建；已存在视为成功）。
 * @param dirPath 目录绝对路径。
 */
static bool hsf_storeEnsureDir(const char* dirPath)
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
 * @brief 用显式路径建实例（读不到文件不算失败——空配置也是合法配置）。
 * @param path 配置文件绝对路径。
 * @return 实例指针；NULL=创建失败。
 */
static XSettings* hsf_storeCreateAt(const char* path)
{
    XSettings* store;
    if (!path || !path[0]) return NULL;
    store = XSettings_create_7_utf8(path, XSettings_IniFormat, NULL);
    if (!store) return NULL;
    /* 构造期只读：文件不存在→空配置（正常）；路径非法/不可读→AccessError。
       此处不因状态判失败——真正的可写性由落盘时判定并在此前的探测中保证。 */
    return store;
}

/**
 * @brief 第三级回落：XSettings 组织/应用名派生路径（Qt QSettings 默认口径）。
 */
static XSettings* hsf_storeCreateByOrgApp(void)
{
    XString* org = XString_create_utf8(HSF_CFG_ORG_NAME);
    XString* app = XString_create_utf8(HSF_CFG_APP_NAME);
    XSettings* store = NULL;

    if (org)
        XCoreApplication_setOrganizationName(org);
    if (app)
        XCoreApplication_setApplicationName(app);
    if (org)
        XClassDelete(org);
    if (app)
        XClassDelete(app);
    store = XSettings_create_4_utf8(HSF_CFG_ORG_NAME, HSF_CFG_APP_NAME, NULL);
    if (store) {
        const char* name = XSettings_fileName_const(store);
        const char* utf8 = name ? XString_toUtf8(name) : NULL;
        if (utf8)
            snprintf(s_path, sizeof(s_path), "%s", utf8);
    }
    return store;
}

/* ==================== 公开 API ==================== */

bool HsfSettingsStore_open(void)
{
    const XString* appDir = NULL;
    const char* dirUtf8 = NULL;
    char dirBuf[400];
    char fileBuf[512];

    if (!s_enabled) return false;
    if (s_store) return true;

    /* 程序目录（本次调用内一次性持有，末尾统一释放——中途 goto 不漏释放）。 */
    appDir = XCoreApplication_applicationDirPath();
    dirUtf8 = appDir ? XString_toUtf8(appDir) : NULL;
    if (!dirUtf8) dirUtf8 = "";

    /* ---- 第一级：<程序目录>/cfg/ ---- */
    if (dirUtf8[0]) {
        snprintf(dirBuf, sizeof(dirBuf), "%s/%s", dirUtf8, HSF_CFG_DIR_NAME);
        if (hsf_storeEnsureDir(dirBuf) && hsf_storeProbeWritable(dirBuf)) {
            snprintf(fileBuf, sizeof(fileBuf), "%s/%s", dirBuf,
                     HSF_CFG_FILE_NAME);
            s_store = hsf_storeCreateAt(fileBuf);
            if (s_store) {
                snprintf(s_path, sizeof(s_path), "%s", fileBuf);
                XPrintf("HsfClient: 设置文件 %s\n", s_path);
                goto done;
            }
        }
    }

    /* ---- 第二级：<程序目录>/（不能建子目录时） ---- */
    if (dirUtf8[0] && hsf_storeProbeWritable(dirUtf8)) {
        snprintf(fileBuf, sizeof(fileBuf), "%s/%s", dirUtf8,
                 HSF_CFG_FILE_NAME);
        s_store = hsf_storeCreateAt(fileBuf);
        if (s_store) {
            snprintf(s_path, sizeof(s_path), "%s", fileBuf);
            XPrintf("HsfClient: 设置文件 %s（程序目录根）\n", s_path);
            goto done;
        }
    }

    /* ---- 第三级：组织/应用名派生路径（用户主目录） ---- */
    s_store = hsf_storeCreateByOrgApp();
    if (s_store) {
        XPrintf("HsfClient: 设置文件 %s（用户目录）\n", s_path);
        goto done;
    }

    XPrintf("HsfClient: 设置文件不可用，本次按默认设置运行\n");

done:
    if (appDir)
        XClassDelete((XClass*)appDir);
    return s_store != NULL;
}

void HsfSettingsStore_close(void)
{
    if (s_store) {
        XSettings_sync(s_store);
        XClassDelete((XClass*)s_store);
        s_store = NULL;
    }
}

bool HsfSettingsStore_isOpen(void)
{
    return s_store != NULL;
}

const char* HsfSettingsStore_path(void)
{
    return s_path;
}

int HsfSettingsStore_getInt(const char* key, int def)
{
    XVariant* v;
    int out = def;

    if (!s_store || !key) return def;
    v = XSettings_value_utf8(s_store, key);
    if (v) {
        if (v->m_type == XVariantType_String) {
            /* INI 无类型：落盘后是文本。显式解析并校验——脏值（手改 ini、
               旧版本遗留）必须回落默认值，不能静默变成 0（0 对多数开关是
               「关」，脏值会把界面静默改成另一种状态）。 */
            const XString* text = XString_fromVariant_const(v);
            bool ok = false;
            int parsed = text ? XString_toInt(text, &ok, 10) : 0;
            if (ok)
                out = parsed;
        } else if (hsf_storeIsNumeric(v->m_type)) {
            out = XVariant_toInt(v);
        }
        /* 其余（含键不存在 → XVariantType_NULL 变体）保持默认值。 */
        XClassDelete((XClass*)v);
    }
    return out;
}

int64_t HsfSettingsStore_getInt64(const char* key, int64_t def)
{
    XVariant* v;
    int64_t out = def;

    if (!s_store || !key) return def;
    v = XSettings_value_utf8(s_store, key);
    if (v) {
        if (v->m_type == XVariantType_String) {
            const XString* text = XString_fromVariant_const(v);
            bool ok = false;
            long long parsed = text ? XString_toLongLong(text, &ok, 10) : 0;
            if (ok)
                out = (int64_t)parsed;
        } else if (hsf_storeIsNumeric(v->m_type)) {
            out = XVariant_toInt64(v);
        }
        XClassDelete((XClass*)v);
    }
    return out;
}

bool HsfSettingsStore_getBool(const char* key, bool def)
{
    /* 一律按 int 0/1 存取：XVariant_toBool 对 String 无回退（读回恒 false）。 */
    return HsfSettingsStore_getInt(key, def ? 1 : 0) != 0;
}

void HsfSettingsStore_setInt(const char* key, int value)
{
    XVariant* v;

    if (!s_store || !key) return;
    v = XVariant_create_int(value);
    if (v) {
        XSettings_setValue_utf8(s_store, key, v);
        XClassDelete((XClass*)v);
    }
}

void HsfSettingsStore_setInt64(const char* key, int64_t value)
{
    XVariant* v;

    if (!s_store || !key) return;
    v = XVariant_create_int64(value);
    if (v) {
        XSettings_setValue_utf8(s_store, key, v);
        XClassDelete((XClass*)v);
    }
}

void HsfSettingsStore_setBool(const char* key, bool value)
{
    HsfSettingsStore_setInt(key, value ? 1 : 0);
}

void HsfSettingsStore_sync(void)
{
    if (!s_store) return;
    XSettings_sync(s_store);
    if (XSettings_status(s_store) != XSettings_NoError) {
        XPrintf("HsfClient: 设置落盘失败（状态=%d），路径 %s\n",
                (int)XSettings_status(s_store), s_path);
    }
}

void HsfSettingsStore_setEnabled(bool enabled)
{
    s_enabled = enabled;
    if (!enabled && s_store) {
        XClassDelete((XClass*)s_store);
        s_store = NULL;
        s_path[0] = '\0';
    }
}
