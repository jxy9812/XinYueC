/**
 * @file       XLoginPolicy.c
 * @brief      登录策略实现：准入判定（总数上限→匿名→同账并发）+ 独立 JSON 持久化。
 * @details    判定路径三条闸顺序固定（见头文件 admitSession）；持久化走
 *             XJsonDocument（策略格式自由，与账户库分文件——见头文件说明）。
 */
#include "XLoginPolicy.h"

#if XINDUSTRY_ON && XACCESS_CONTROL_ON

#include "XJson.h"
#include "XJsonDocument.h"
#include "XJsonObject.h"
#include "XJsonValue.h"
#include "XJsonArray.h"
#include "XDeviceFile.h"
#include "XIODevice.h"
#include "XString.h"
#include "XMemory.h"
#include <string.h>
#include <stdio.h>

/* ==================== 内部辅助 ==================== */

static void policyCopyReason(char* out, int cap, const char* text)
{
    if (!out || cap <= 0) return;
    snprintf(out, (size_t)cap, "%s", text ? text : "");
}

/* ==================== 生命周期 ==================== */

static void VXLoginPolicy_deinit(XObject* obj)
{
    XLoginPolicy* lp = (XLoginPolicy*)obj;
    if (!lp) return;
    XClass_Deinit_Parent(XObject, obj);
}

XVtable* XLoginPolicy_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLoginPolicy)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLoginPolicy_deinit);
    XCLASS_SHOW_SIZE_DEFAULT(XLoginPolicy);
    return XVTABLE_DEFAULT;
}

void XLoginPolicy_init(XLoginPolicy* obj)
{
    if (!obj) return;
    memset(obj, 0, sizeof(*obj));
    XObject_init((XObject*)obj);
    XClassGetVtable(obj) = XLoginPolicy_class_init();
    obj->m_allowAnonymous = false;
    obj->m_allowConcurrent = false;
    obj->m_concurrencyMode = (int)XLoginConcurrency_DenyNew;
    obj->m_maxSessions = XLOGIN_POLICY_DEFAULT_MAX_SESSIONS;
}

XLoginPolicy* XLoginPolicy_create(void)
{
    XLoginPolicy* obj = (XLoginPolicy*)XMemory_malloc(
        sizeof(XLoginPolicy), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!obj) return NULL;
    XLoginPolicy_init(obj);
    Set_Class_IsHeap(obj, true);
    return obj;
}

/* ==================== 策略项 ==================== */

bool XLoginPolicy_setAllowAnonymous(XLoginPolicy* lp, bool allow)
{
    if (!lp) return false;
    if (lp->m_allowAnonymous == allow) return true;
    lp->m_allowAnonymous = allow;
    XLoginPolicy_policyChanged_signal(lp);
    return true;
}

bool XLoginPolicy_anonymousAllowed(const XLoginPolicy* lp)
{
    return lp ? lp->m_allowAnonymous : false;
}

bool XLoginPolicy_setAllowConcurrent(XLoginPolicy* lp, bool allow)
{
    if (!lp) return false;
    if (lp->m_allowConcurrent == allow) return true;
    lp->m_allowConcurrent = allow;
    XLoginPolicy_policyChanged_signal(lp);
    return true;
}

bool XLoginPolicy_concurrentAllowed(const XLoginPolicy* lp)
{
    return lp ? lp->m_allowConcurrent : false;
}

bool XLoginPolicy_setConcurrencyMode(XLoginPolicy* lp, XLoginConcurrencyMode mode)
{
    if (!lp) return false;
    if (mode != XLoginConcurrency_DenyNew && mode != XLoginConcurrency_KickOld)
        return false;
    if (lp->m_concurrencyMode == (int)mode) return true;
    lp->m_concurrencyMode = (int)mode;
    XLoginPolicy_policyChanged_signal(lp);
    return true;
}

XLoginConcurrencyMode XLoginPolicy_concurrencyMode(const XLoginPolicy* lp)
{
    return lp ? (XLoginConcurrencyMode)lp->m_concurrencyMode
              : XLoginConcurrency_DenyNew;
}

bool XLoginPolicy_setMaxSessions(XLoginPolicy* lp, uint32_t maxSessions)
{
    if (!lp) return false;
    if (lp->m_maxSessions == maxSessions) return true;
    lp->m_maxSessions = maxSessions;
    XLoginPolicy_policyChanged_signal(lp);
    return true;
}

uint32_t XLoginPolicy_maxSessions(const XLoginPolicy* lp)
{
    return lp ? lp->m_maxSessions : 0u;
}

/* ==================== 准入判定 ==================== */

bool XLoginPolicy_admitSession(XLoginPolicy* lp, bool anonymous,
                               uint32_t activeSessions,
                               uint32_t sameAccountSessions,
                               char* outReason, int cap,
                               bool* outKickSameAccount)
{
    const char* reason = NULL;
    bool kick = false;
    bool admit;

    if (outKickSameAccount) *outKickSameAccount = false;
    if (outReason && cap > 0) outReason[0] = '\0';

    if (!lp) {
        reason = "登录策略未装配";
        admit = false;
    }
    /* 闸 1：会话总数上限（0=不限）。 */
    else if (lp->m_maxSessions != 0 && activeSessions >= lp->m_maxSessions) {
        reason = "会话数已达上限";
        admit = false;
    }
    /* 闸 2：匿名（双闸的第一道，权限面第二道在 XPermission，约束 8）。 */
    else if (anonymous && !lp->m_allowAnonymous) {
        reason = "匿名登录未开放";
        admit = false;
    }
    /* 闸 3：同账并发。 */
    else if (!anonymous && !lp->m_allowConcurrent && sameAccountSessions > 0) {
        if (lp->m_concurrencyMode == (int)XLoginConcurrency_KickOld) {
            /* 接管：准入 + 让调用方去踢同账户旧会话（约束 9：只踢同账）。 */
            kick = true;
            admit = true;
        } else {
            reason = "该账户已登录，不允许同时在线";
            admit = false;
        }
    }
    else {
        admit = true;
    }

    if (!admit) {
        policyCopyReason(outReason, cap, reason);
        XLoginPolicy_loginRejected_signal(lp, anonymous ? "" : "",
                                          reason ? reason : "", 0);
    } else if (kick && outKickSameAccount) {
        *outKickSameAccount = true;
    }
    return admit;
}

/* ==================== 持久化 ==================== */

/* 策略 JSON 键（独立小文件；与账户库分文件——见头文件说明）。 */
#define LOGIN_POLICY_KEY_ANONYMOUS    "allowAnonymous"
#define LOGIN_POLICY_KEY_CONCURRENT   "allowConcurrent"
#define LOGIN_POLICY_KEY_CONC_MODE    "concurrencyMode"
#define LOGIN_POLICY_KEY_MAX_SESSIONS "maxSessions"

/** @brief 打开文件（XLinkUserStore 同款薄封装：路径 → XDevice_open）。 */
static XFd policyOpen(const char* path, int mode, int* error)
{
    XString* target;
    XDeviceOpenOptions options;
    XFd fd;
    if (!path || !path[0])
        return XFD_INVALID;
    target = XString_create_utf8(path);
    if (!target)
        return XFD_INVALID;
    memset(&options, 0, sizeof(options));
    options.m_openMode = mode;
    options.m_target = target;
    fd = XDevice_open(XDeviceType_File, &options, error);
    XClassDelete(target);
    return fd;
}

bool XLoginPolicy_setPath(XLoginPolicy* lp, const char* path)
{
    size_t n;
    if (!lp) return false;
    if (!path || !path[0]) {
        lp->m_path[0] = '\0';
        return true;
    }
    n = strlen(path);
    if (n >= sizeof(lp->m_path)) return false;
    memcpy(lp->m_path, path, n + 1);
    return true;
}

bool XLoginPolicy_load(XLoginPolicy* lp)
{
    XString* pathStr;
    XFileStat stat;
    XFd fd;
    char* text = NULL;
    XString* textStr = NULL;
    size_t readSize = 0;
    XJsonDocument* document;
    XJsonObject* root;
    const XJsonValue* v;

    if (!lp || !lp->m_path[0]) return false;
    /* 文件不存在 = 保持当前值（首次运行常见，不算错）。 */
    pathStr = XString_create_utf8(lp->m_path);
    if (!pathStr) return false;
    if (!XDeviceFile_stat(pathStr, &stat) || !stat.exists) {
        XClassDelete((XClass*)pathStr);
        return true;
    }
    XClassDelete((XClass*)pathStr);
    fd = policyOpen(lp->m_path, XIODevice_ReadOnly, NULL);
    if (fd == XFD_INVALID) return true;
    if (stat.size <= 0) {
    XDeviceFile_close(fd);
        return true;
    }
    text = (char*)XMemory_malloc((size_t)stat.size + 1, XCLASS_DEFAULT_MEMORY_TYPE);
    if (!text) {
        XDeviceFile_close(fd);
        return false;
    }
    while (readSize < (size_t)stat.size) {
        int64_t got = XDeviceFile_read(fd, text + readSize,
                                       (int64_t)((size_t)stat.size - readSize));
        if (got <= 0) break;
        readSize += (size_t)got;
    }
    XDeviceFile_close(fd);
    text[readSize] = '\0';

    /* fromString 收 XString*（内码字符串对象）：裸 char* 直传会按对象
     * 布局误读直接崩——先包一层 UTF-8 XString（XLinkUserStore 同款）。 */
    textStr = XString_create_with_length_utf8(text, readSize);
    document = textStr ? XJsonDocument_fromString(textStr) : NULL;
    if (textStr) {
        XClassDelete((XClass*)textStr);
        textStr = NULL;
    }
    XMemory_free(text, XCLASS_DEFAULT_MEMORY_TYPE);
    if (!document) return false;                 /* 损坏：策略不变，不半加载。 */
    root = XJsonDocument_object(document);
    if (root) {
        v = XJsonObject_value_keyUtf8(root, LOGIN_POLICY_KEY_ANONYMOUS);
        if (v) lp->m_allowAnonymous = XJsonValue_toBool(v, false);
        v = XJsonObject_value_keyUtf8(root, LOGIN_POLICY_KEY_CONCURRENT);
        if (v) lp->m_allowConcurrent = XJsonValue_toBool(v, false);
        v = XJsonObject_value_keyUtf8(root, LOGIN_POLICY_KEY_CONC_MODE);
        if (v) {
            int64_t m = XJsonValue_toInt(v, 0);
            if (m == (int64_t)XLoginConcurrency_DenyNew ||
                m == (int64_t)XLoginConcurrency_KickOld)
                lp->m_concurrencyMode = (int)m;
        }
        v = XJsonObject_value_keyUtf8(root, LOGIN_POLICY_KEY_MAX_SESSIONS);
        if (v) lp->m_maxSessions = (uint32_t)XJsonValue_toInt(v, 0);
    }
    XClassDelete((XClass*)document);
    return true;
}

bool XLoginPolicy_save(XLoginPolicy* lp)
{
    XJsonObject* obj;
    XJsonDocument* doc;
    XString* text;
    XFd fd;
    int error = 0;
    const char* utf8;
    size_t total = 0;
    size_t written = 0;
    bool ok = false;

    if (!lp || !lp->m_path[0]) return false;
    obj = XJsonObject_create();
    if (!obj) return false;
    if (!XJsonObject_insert_keyUtf8_bool(obj, LOGIN_POLICY_KEY_ANONYMOUS,
                                         lp->m_allowAnonymous) ||
        !XJsonObject_insert_keyUtf8_bool(obj, LOGIN_POLICY_KEY_CONCURRENT,
                                         lp->m_allowConcurrent) ||
        !XJsonObject_insert_keyUtf8_int(obj, LOGIN_POLICY_KEY_CONC_MODE,
                                        (int64_t)lp->m_concurrencyMode) ||
        !XJsonObject_insert_keyUtf8_int(obj, LOGIN_POLICY_KEY_MAX_SESSIONS,
                                        (int64_t)lp->m_maxSessions)) {
        XClassDelete((XClass*)obj);
        return false;
    }
    doc = XJsonDocument_create_object(obj);   /* 拷贝语义：obj 仍需自释。 */
    XClassDelete((XClass*)obj);
    obj = NULL;
    if (!doc) {
        return false;
    }
    text = XJsonDocument_toString(doc, XJsonDocument_Compact);
    XClassDelete((XClass*)doc);
    if (!text) return false;

    fd = policyOpen(lp->m_path, XIODevice_WriteOnly | XIODevice_Create
                    | XIODevice_Truncate, &error);
    if (fd != XFD_INVALID) {
        utf8 = XString_toUtf8(text);
        total = XString_toUtf8_length(text);
        while (written < total) {
            int64_t part = XDeviceFile_write(fd, utf8 + written,
                                             (int64_t)(total - written));
            if (part <= 0) break;
            written += (size_t)part;
        }
        XDeviceFile_close(fd);
        ok = (written == total);
    }
    XClassDelete((XClass*)text);
    if (ok) XLoginPolicy_storeSaved_signal(lp, lp->m_path);
    return ok;
}

void XLoginPolicy_snapshot(const XLoginPolicy* lp, XLoginPolicySnapshot* out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!lp) return;
    out->m_allowAnonymous = lp->m_allowAnonymous;
    out->m_allowConcurrent = lp->m_allowConcurrent;
    out->m_concurrencyMode = lp->m_concurrencyMode;
    out->m_maxSessions = lp->m_maxSessions;
}

/* ==================== 信号 ==================== */

void* XLoginPolicy_loginRejected_signal(XLoginPolicy* lp, const char* name,
                                        const char* reason, int64_t atMs)
{
    /* [框架约定] NULL 时也返回自身地址（见 XPermPolicy 同款注释）。 */
    XEmitSignal(lp, XLoginPolicy_loginRejected_signal,
                XVarList_Create(XVar(const char*, name),
                                XVar(const char*, reason),
                                XVar(int64_t, atMs)),
                NULL, NULL, XEVENT_PRIORITY_NORMAL);
    return NULL;
}

void* XLoginPolicy_policyChanged_signal(XLoginPolicy* lp)
{
    XEmitSignal(lp, XLoginPolicy_policyChanged_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
    return NULL;
}

void* XLoginPolicy_storeSaved_signal(XLoginPolicy* lp, const char* path)
{
    XEmitSignal(lp, XLoginPolicy_storeSaved_signal,
                XVarList_Create(XVar(const char*, path)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
    return NULL;
}

#endif /* XINDUSTRY_ON && XACCESS_CONTROL_ON */
