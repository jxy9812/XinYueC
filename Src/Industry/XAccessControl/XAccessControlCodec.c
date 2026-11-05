/**
 * @file       XAccessControlCodec.c
 * @brief      访问控制编解码实现：账户/权限策略/登录策略/事件 ↔ JSON/BSON。
 * @details    [实现捷径] 与 XAutoControlCodec 同款：编码先建 BSON 文档再经
 *             XBsonDocument_toJson 出 JSON；解码 JSON 先归一到 BSON 再走
 *             同一读取路径。凭据材料（盐/摘要/迭代数）在本文件中**无键位**
 *             ——序列化侧的不可泄露由结构保证，不靠调用方自觉。
 */
#include "XAccessControlCodec.h"

#if XINDUSTRY_ON && XACCESS_CONTROL_ON

#include "XBson.h"
#include "XBsonDocument.h"
#include "XBsonValue.h"
#include "XJson.h"
#include "XJsonDocument.h"
#include "XJsonObject.h"
#include "XMemory.h"
#include "XString.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================== */
/*                      内部工具                                              */
/* ========================================================================== */

/** @brief JSON 文本字节 → BSON 文档（解码归一捷径）。 */
static XBsonDocument* accessCodecJsonToDoc(const uint8_t* data, size_t size)
{
    XByteArray* bytes;
    XJsonDocument* json;
    XJsonObject* obj;
    XBsonDocument* doc = NULL;
    bytes = XByteArray_create();
    if (!bytes) return NULL;
    if (!XByteArray_push_back_2(bytes, data, size)) {
        XClassDelete((XClass*)bytes);
        return NULL;
    }
    json = XJsonDocument_fromJson(bytes);
    XClassDelete((XClass*)bytes);
    if (!json) return NULL;
    obj = XJsonDocument_object(json);
    if (obj)
        doc = XBsonDocument_fromJsonObject(obj);
    XClassDelete((XClass*)json);
    return doc;
}

/** @brief 两格式出口公共尾（doc → BSON 或紧凑 JSON 字节）。 */
static XByteArray* accessCodecDocToBytes(XBsonDocument* doc, bool json)
{
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = json ? XBsonDocument_toJson(doc, XJsonDocument_Compact)
                 : XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/* ========================================================================== */
/*                      账户帧                                                */
/* ========================================================================== */

/* 账户帧键（凭据材料无键位——见文件头说明）。 */
#define ACCESS_CODEC_ACC_NAME     "name"
#define ACCESS_CODEC_ACC_ROLE     "role"
#define ACCESS_CODEC_ACC_LOCKED   "locked"
#define ACCESS_CODEC_ACC_DEFAULT  "defaultPass"

static XBsonDocument* accessCodecBuildAccountDoc(const XAccountSnapshot* snap)
{
    XBsonDocument* doc = XBsonDocument_create();
    if (!doc) return NULL;
    if (!XBsonDocument_insert_keyUtf8_utf8(doc, ACCESS_CODEC_ACC_NAME,
                                           snap ? snap->m_name : "") ||
        !XBsonDocument_insert_keyUtf8_int32(doc, ACCESS_CODEC_ACC_ROLE,
                                            snap ? snap->m_role : 0) ||
        !XBsonDocument_insert_keyUtf8_bool(doc, ACCESS_CODEC_ACC_LOCKED,
                                           snap ? snap->m_locked : false) ||
        !XBsonDocument_insert_keyUtf8_bool(doc, ACCESS_CODEC_ACC_DEFAULT,
                                           snap ? snap->m_defaultPass : false)) {
        XClassDelete((XClass*)doc);
        return NULL;
    }
    return doc;
}

static void accessCodecFillAccount(XAccountSnapshot* out, const XBsonDocument* doc)
{
    XBsonValue* v;
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, ACCESS_CODEC_ACC_NAME);
    if (v) {
        const XString* s = XBsonValue_toString(v);
        const char* utf8 = s ? XString_toUtf8(s) : NULL;
        if (utf8)
            snprintf(out->m_name, sizeof(out->m_name), "%s", utf8);
    }
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, ACCESS_CODEC_ACC_ROLE);
    if (v) out->m_role = XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, ACCESS_CODEC_ACC_LOCKED);
    if (v) out->m_locked = XBsonValue_toBool(v, false);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, ACCESS_CODEC_ACC_DEFAULT);
    if (v) out->m_defaultPass = XBsonValue_toBool(v, false);
}

XByteArray* XAccessControlCodec_accountToBson(const XAccountSnapshot* snap)
{
    return accessCodecDocToBytes(accessCodecBuildAccountDoc(snap), false);
}

XByteArray* XAccessControlCodec_accountToJson(const XAccountSnapshot* snap)
{
    return accessCodecDocToBytes(accessCodecBuildAccountDoc(snap), true);
}

bool XAccessControlCodec_accountFromBson(XAccountSnapshot* out,
                                         const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    if (!XBsonDocument_from_bytes(doc, data, size)) {
        XClassDelete((XClass*)doc);
        return false;
    }
    accessCodecFillAccount(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

bool XAccessControlCodec_accountFromJson(XAccountSnapshot* out,
                                         const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = accessCodecJsonToDoc(data, size);
    if (!doc) return false;
    accessCodecFillAccount(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

/**
 * @brief 全量账户列表帧公共体（前缀风格与阀/泵快照一致）。
 */
static XBsonDocument* accessCodecBuildAccountListDoc(const XAccountManager* mgr)
{
    XBsonDocument* doc = XBsonDocument_create();
    uint32_t count;
    uint32_t i;
    if (!doc) return NULL;
    count = mgr ? XAccountManager_count(mgr) : 0;
    if (!XBsonDocument_insert_keyUtf8_int32(doc, "count", (int32_t)count)) {
        XClassDelete((XClass*)doc);
        return NULL;
    }
    for (i = 0; mgr && i < count; ++i) {
        const XLinkUserRecord* r = XLinkUserStore_at(&mgr->m_store, i);
        const char* name;
        XAccountSnapshot snap;
        char key[64];
        char prefix[8];
        if (!r) continue;
        name = XLinkUserStore_name_const(r);
        if (!name) continue;
        XAccountManager_snapshot(mgr, name, &snap);
        snprintf(prefix, sizeof(prefix), "v%u_", (unsigned)i);
        snprintf(key, sizeof(key), "%s%s", prefix, ACCESS_CODEC_ACC_NAME);
        if (!XBsonDocument_insert_keyUtf8_utf8(doc, key, snap.m_name)) break;
        snprintf(key, sizeof(key), "%s%s", prefix, ACCESS_CODEC_ACC_ROLE);
        if (!XBsonDocument_insert_keyUtf8_int32(doc, key, snap.m_role)) break;
        snprintf(key, sizeof(key), "%s%s", prefix, ACCESS_CODEC_ACC_LOCKED);
        if (!XBsonDocument_insert_keyUtf8_bool(doc, key, snap.m_locked)) break;
        snprintf(key, sizeof(key), "%s%s", prefix, ACCESS_CODEC_ACC_DEFAULT);
        if (!XBsonDocument_insert_keyUtf8_bool(doc, key, snap.m_defaultPass)) break;
    }
    return doc;
}

XByteArray* XAccessControlCodec_accountListToBson(const XAccountManager* mgr)
{
    return accessCodecDocToBytes(accessCodecBuildAccountListDoc(mgr), false);
}

XByteArray* XAccessControlCodec_accountListToJson(const XAccountManager* mgr)
{
    return accessCodecDocToBytes(accessCodecBuildAccountListDoc(mgr), true);
}

/* ========================================================================== */
/*                      权限策略帧                                            */
/* ========================================================================== */

#define ACCESS_CODEC_PERM_NONE   "nonePerms"
#define ACCESS_CODEC_PERM_USER   "userPerms"
#define ACCESS_CODEC_PERM_ADMIN  "adminPerms"

static XBsonDocument* accessCodecBuildPermDoc(const XPermPolicySnapshot* snap)
{
    XBsonDocument* doc = XBsonDocument_create();
    if (!doc) return NULL;
    if (!XBsonDocument_insert_keyUtf8_int32(doc, ACCESS_CODEC_PERM_NONE,
            snap ? (int32_t)snap->m_rolePerms[0] : 0) ||
        !XBsonDocument_insert_keyUtf8_int32(doc, ACCESS_CODEC_PERM_USER,
            snap ? (int32_t)snap->m_rolePerms[1] : 0) ||
        !XBsonDocument_insert_keyUtf8_int32(doc, ACCESS_CODEC_PERM_ADMIN,
            snap ? (int32_t)snap->m_rolePerms[2] : 0)) {
        XClassDelete((XClass*)doc);
        return NULL;
    }
    return doc;
}

static void accessCodecFillPerm(XPermPolicySnapshot* out, const XBsonDocument* doc)
{
    XBsonValue* v;
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, ACCESS_CODEC_PERM_NONE);
    if (v) out->m_rolePerms[0] = (uint32_t)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, ACCESS_CODEC_PERM_USER);
    if (v) out->m_rolePerms[1] = (uint32_t)XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, ACCESS_CODEC_PERM_ADMIN);
    if (v) out->m_rolePerms[2] = (uint32_t)XBsonValue_toInt32(v, 0);
}

XByteArray* XAccessControlCodec_permPolicyToBson(const XPermPolicySnapshot* snap)
{
    return accessCodecDocToBytes(accessCodecBuildPermDoc(snap), false);
}

XByteArray* XAccessControlCodec_permPolicyToJson(const XPermPolicySnapshot* snap)
{
    return accessCodecDocToBytes(accessCodecBuildPermDoc(snap), true);
}

bool XAccessControlCodec_permPolicyFromBson(XPermPolicySnapshot* out,
                                            const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    if (!XBsonDocument_from_bytes(doc, data, size)) {
        XClassDelete((XClass*)doc);
        return false;
    }
    accessCodecFillPerm(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

bool XAccessControlCodec_permPolicyFromJson(XPermPolicySnapshot* out,
                                            const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = accessCodecJsonToDoc(data, size);
    if (!doc) return false;
    accessCodecFillPerm(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

/* ========================================================================== */
/*                      登录策略帧                                            */
/* ========================================================================== */

#define ACCESS_CODEC_LP_ANON    "allowAnonymous"
#define ACCESS_CODEC_LP_CONCUR  "allowConcurrent"
#define ACCESS_CODEC_LP_MODE    "concurrencyMode"
#define ACCESS_CODEC_LP_MAX     "maxSessions"

static XBsonDocument* accessCodecBuildLoginDoc(const XLoginPolicySnapshot* snap)
{
    XBsonDocument* doc = XBsonDocument_create();
    if (!doc) return NULL;
    if (!XBsonDocument_insert_keyUtf8_bool(doc, ACCESS_CODEC_LP_ANON,
            snap ? snap->m_allowAnonymous : false) ||
        !XBsonDocument_insert_keyUtf8_bool(doc, ACCESS_CODEC_LP_CONCUR,
            snap ? snap->m_allowConcurrent : false) ||
        !XBsonDocument_insert_keyUtf8_int32(doc, ACCESS_CODEC_LP_MODE,
            snap ? snap->m_concurrencyMode : 0) ||
        !XBsonDocument_insert_keyUtf8_int32(doc, ACCESS_CODEC_LP_MAX,
            snap ? (int32_t)snap->m_maxSessions : 0)) {
        XClassDelete((XClass*)doc);
        return NULL;
    }
    return doc;
}

static void accessCodecFillLogin(XLoginPolicySnapshot* out, const XBsonDocument* doc)
{
    XBsonValue* v;
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, ACCESS_CODEC_LP_ANON);
    if (v) out->m_allowAnonymous = XBsonValue_toBool(v, false);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, ACCESS_CODEC_LP_CONCUR);
    if (v) out->m_allowConcurrent = XBsonValue_toBool(v, false);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, ACCESS_CODEC_LP_MODE);
    if (v) out->m_concurrencyMode = XBsonValue_toInt32(v, 0);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)doc, ACCESS_CODEC_LP_MAX);
    if (v) out->m_maxSessions = (uint32_t)XBsonValue_toInt32(v, 0);
}

XByteArray* XAccessControlCodec_loginPolicyToBson(const XLoginPolicySnapshot* snap)
{
    return accessCodecDocToBytes(accessCodecBuildLoginDoc(snap), false);
}

XByteArray* XAccessControlCodec_loginPolicyToJson(const XLoginPolicySnapshot* snap)
{
    return accessCodecDocToBytes(accessCodecBuildLoginDoc(snap), true);
}

bool XAccessControlCodec_loginPolicyFromBson(XLoginPolicySnapshot* out,
                                             const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = XBsonDocument_create();
    if (!doc) return false;
    if (!XBsonDocument_from_bytes(doc, data, size)) {
        XClassDelete((XClass*)doc);
        return false;
    }
    accessCodecFillLogin(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

bool XAccessControlCodec_loginPolicyFromJson(XLoginPolicySnapshot* out,
                                             const uint8_t* data, size_t size)
{
    XBsonDocument* doc;
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!data || size == 0) return false;
    doc = accessCodecJsonToDoc(data, size);
    if (!doc) return false;
    accessCodecFillLogin(out, doc);
    XClassDelete((XClass*)doc);
    return true;
}

/* ========================================================================== */
/*                      事件帧                                                */
/* ========================================================================== */

XByteArray* XAccessControlCodec_permDeniedToBson(int role, int32_t permMask,
                                                 int64_t atMs)
{
    XBsonDocument* doc = XBsonDocument_create();
    if (!doc) return NULL;
    if (!XBsonDocument_insert_keyUtf8_int32(doc, "role", role) ||
        !XBsonDocument_insert_keyUtf8_int32(doc, "perm", permMask) ||
        !XBsonDocument_insert_keyUtf8_int64(doc, "atMs", atMs)) {
        XClassDelete((XClass*)doc);
        return NULL;
    }
    return accessCodecDocToBytes(doc, false);
}

XByteArray* XAccessControlCodec_permDeniedToJson(int role, int32_t permMask,
                                                 int64_t atMs)
{
    XBsonDocument* doc = XBsonDocument_create();
    if (!doc) return NULL;
    if (!XBsonDocument_insert_keyUtf8_int32(doc, "role", role) ||
        !XBsonDocument_insert_keyUtf8_int32(doc, "perm", permMask) ||
        !XBsonDocument_insert_keyUtf8_int64(doc, "atMs", atMs)) {
        XClassDelete((XClass*)doc);
        return NULL;
    }
    return accessCodecDocToBytes(doc, true);
}

XByteArray* XAccessControlCodec_loginRejectedToBson(const char* name,
                                                    const char* reason,
                                                    int64_t atMs)
{
    XBsonDocument* doc = XBsonDocument_create();
    if (!doc) return NULL;
    if (!XBsonDocument_insert_keyUtf8_utf8(doc, "name", name ? name : "") ||
        !XBsonDocument_insert_keyUtf8_utf8(doc, "reason", reason ? reason : "") ||
        !XBsonDocument_insert_keyUtf8_int64(doc, "atMs", atMs)) {
        XClassDelete((XClass*)doc);
        return NULL;
    }
    return accessCodecDocToBytes(doc, false);
}

XByteArray* XAccessControlCodec_loginRejectedToJson(const char* name,
                                                    const char* reason,
                                                    int64_t atMs)
{
    XBsonDocument* doc = XBsonDocument_create();
    if (!doc) return NULL;
    if (!XBsonDocument_insert_keyUtf8_utf8(doc, "name", name ? name : "") ||
        !XBsonDocument_insert_keyUtf8_utf8(doc, "reason", reason ? reason : "") ||
        !XBsonDocument_insert_keyUtf8_int64(doc, "atMs", atMs)) {
        XClassDelete((XClass*)doc);
        return NULL;
    }
    return accessCodecDocToBytes(doc, true);
}

#endif /* XINDUSTRY_ON && XACCESS_CONTROL_ON */
