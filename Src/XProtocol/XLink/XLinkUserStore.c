/** @file       XLinkUserStore.c
 * @brief      XLink 登录账户库实现（JSON 持久化，XString 存用户名）。
 * @details    只依赖 XinYueC 抽象层（XDeviceFile / XJsonDocument / XString /
 *             XCryptographic / XRandomGenerator / XMemory），禁止直接使用
 *             stdio 或平台 API。口令明文只在派生瞬间存在于调用方栈上，
 *             本文件不保存、不打印。
 *             布局：每个账户 = 一个 XString（用户名）+ 定长盐/摘要 + 位域
 *             状态，避免定长名字截断与布尔成员各自占位。
 * @author     XinYueC 团队
 */
#include "XLinkUserStore.h"

#if XLINK_ON && XLINK_AUTH_ON

#include "XLinkAuth.h"
#include "XDeviceFile.h"
#include "XFileInfo.h"
#include "XJsonArray.h"
#include "XJsonDocument.h"
#include "XJsonObject.h"
#include "XJsonValue.h"
#include "XMemory.h"
#include "XStringUtils.h"
#include <string.h>

/** @brief 账户库文件读取上限（字节）。 */
#define XLINK_USERSTORE_MAX_BYTES (64u * 1024u)

/** @brief 十六进制输出缓冲（盐/摘要）。 */
#define XLINK_USERSTORE_HEX_SALT (2u * XLINK_AUTH_SALT_BYTES + 1u)
#define XLINK_USERSTORE_HEX_HASH (2u * XLINK_AUTH_NONCE_BYTES + 1u)

/* ========================================================================== */
/*                        内部工具                                           */
/* ========================================================================== */

/** @brief 十六进制编码（小写）。 */
static void xus_hexEncode(const uint8_t* data, size_t size, char* out,
                          size_t cap)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;
    if (!data || !out || cap < 2u * size + 1u)
        return;
    for (i = 0; i < size; ++i) {
        out[2u * i] = digits[(data[i] >> 4) & 0x0Fu];
        out[2u * i + 1u] = digits[data[i] & 0x0Fu];
    }
    out[2u * size] = '\0';
}

/** @brief 单个十六进制字符值；非法返回 -1。 */
static int xus_hexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/** @brief 十六进制解码；长度必须精确匹配 2*size。 */
static bool xus_hexDecode(const char* text, uint8_t* out, size_t size)
{
    size_t i;
    if (!text || !out || XStrlen(text) != 2u * size)
        return false;
    for (i = 0; i < size; ++i) {
        int high = xus_hexValue(text[2u * i]);
        int low = xus_hexValue(text[2u * i + 1u]);
        if (high < 0 || low < 0)
            return false;
        out[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

/** @brief 打开账户库文件（统一封装 XDevice 打开选项）。 */
static XFd xus_open(const char* path, int mode, int* error)
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

/** @brief 查找账户下标；未找到返回 -1。 */
static int xus_indexOf(const XLinkUserStore* store, const char* user)
{
    uint32_t i;
    if (!store || !user)
        return -1;
    for (i = 0; i < store->m_count; ++i) {
        const char* name = XLinkUserStore_name_const(&store->m_users[i]);
        if (name && XStrcmp(name, user) == 0)
            return (int)i;
    }
    return -1;
}

/** @brief 重置一条记录（释放名字、零化敏感字段）。 */
static void xus_resetRecord(XLinkUserRecord* record)
{
    if (!record)
        return;
    if (record->m_name) {
        XClassDelete((XClass*)record->m_name);
        record->m_name = NULL;
    }
    XLinkAuth_secureZero(record->m_hash, sizeof(record->m_hash));
    XLinkAuth_secureZero(record->m_salt, sizeof(record->m_salt));
    record->m_iterations = 0;
    record->m_role = XLinkAuthRole_Anonymous;
    record->m_flags = 0;
}

/** @brief 为记录派生并写入口令摘要（盐随机、迭代默认）。 */
static bool xus_setPassword(XLinkUserRecord* record, const char* password)
{
    if (!record || !password || !password[0])
        return false;
    if (!XLinkAuth_randomFill(record->m_salt, XLINK_AUTH_SALT_BYTES))
        return false;
    record->m_iterations = XLINK_AUTH_HASH_ITERATIONS;
    memset(record->m_hash, 0, sizeof(record->m_hash));
    if (!XLinkAuth_deriveHash(password, record->m_salt, record->m_iterations,
                              record->m_hash)) {
        XLinkAuth_secureZero(record->m_hash, sizeof(record->m_hash));
        return false;
    }
    record->m_flags |= XLINK_USER_FLAG_HAS_PASSWORD;
    return true;
}

/* ========================================================================== */
/*                        查询                                               */
/* ========================================================================== */

const char* XLinkUserStore_name_const(const XLinkUserRecord* record)
{
    if (!record || !record->m_name)
        return NULL;
    return XString_toUtf8(record->m_name);
}

bool XLinkUserStore_isLocked(const XLinkUserRecord* record)
{
    return record ? ((record->m_flags & XLINK_USER_FLAG_LOCKED) != 0) : false;
}

bool XLinkUserStore_hasPassword(const XLinkUserRecord* record)
{
    return record ? ((record->m_flags & XLINK_USER_FLAG_HAS_PASSWORD) != 0)
                  : false;
}

/* ========================================================================== */
/*                        生命周期                                           */
/* ========================================================================== */

void XLinkUserStore_init(XLinkUserStore* store)
{
    if (!store)
        return;
    memset(store, 0, sizeof(*store));
}

void XLinkUserStore_clear(XLinkUserStore* store)
{
    uint32_t i;
    if (!store)
        return;
    for (i = 0; i < store->m_count && i < XLINK_AUTH_USER_SLOTS; ++i)
        xus_resetRecord(&store->m_users[i]);
    XLinkAuth_secureZero(store->m_users, sizeof(store->m_users));
    store->m_count = 0;
}

/* ========================================================================== */
/*                        持久化                                             */
/* ========================================================================== */

bool XLinkUserStore_load(XLinkUserStore* store, const char* path,
                         bool* outExists)
{
    XFileStat stat;
    XString* target;
    XFd fd = XFD_INVALID;
    char* buffer = NULL;
    XString* text = NULL;
    XJsonDocument* document = NULL;
    XJsonObject* root;
    XJsonValue* usersValue = NULL;
    const XJsonArray* users;
    int error = 0;
    size_t readSize = 0;
    size_t i;
    bool ok = false;

    if (!store || !path || !path[0])
        return false;
    if (outExists)
        *outExists = false;
    XLinkUserStore_clear(store);

    target = XString_create_utf8(path);
    if (!target)
        return false;
    if (!XDeviceFile_stat(target, &stat) || !stat.exists) {
        XClassDelete(target);
        return true;
    }
    if (outExists)
        *outExists = true;
    if (!stat.isFile || stat.size < 0
        || (uint64_t)stat.size > XLINK_USERSTORE_MAX_BYTES) {
        XClassDelete(target);
        return false;
    }

    fd = xus_open(path, XIODevice_ReadOnly, &error);
    XClassDelete(target);
    if (fd == XFD_INVALID)
        return false;

    buffer = (char*)XMalloc_System((size_t)stat.size + 1u);
    if (!buffer) {
        XDeviceFile_close(fd);
        return false;
    }
    while (readSize < (size_t)stat.size) {
        int64_t got = XDeviceFile_read(fd, buffer + readSize,
                                       (int64_t)((size_t)stat.size - readSize));
        if (got <= 0)
            goto cleanup;
        readSize += (size_t)got;
    }
    buffer[readSize] = '\0';

    text = XString_create_with_length_utf8(buffer, readSize);
    document = text ? XJsonDocument_fromString(text) : NULL;
    if (text) {
        XClassDelete(text);
        text = NULL;
    }
    root = document ? XJsonDocument_object(document) : NULL;
    usersValue = root ? XJsonObject_value_keyUtf8(root, "users") : NULL;
    users = usersValue ? XJsonValue_toArray(usersValue) : NULL;
    if (!document || !root || !users
        || XJsonArray_size_base(users) > (int64_t)XLINK_AUTH_USER_SLOTS)
        goto cleanup;

    for (i = 0; i < (size_t)XJsonArray_size_base(users); ++i) {
        const XJsonValue* value = XJsonArray_at_const(users, (int64_t)i);
        const XJsonObject* object = value ? XJsonValue_toObject(value) : NULL;
        const XJsonValue* nameValue;
        const XJsonValue* roleValue;
        const XJsonValue* saltValue;
        const XJsonValue* hashValue;
        const XJsonValue* iterationsValue;
        const XJsonValue* lockedValue;
        XLinkUserRecord* record = &store->m_users[store->m_count];
        char saltHex[XLINK_USERSTORE_HEX_SALT];
        char hashHex[XLINK_USERSTORE_HEX_HASH];
        const char* name;
        const char* salt;
        const char* hash;
        int64_t role = 0;
        int64_t iterations = (int64_t)XLINK_AUTH_HASH_ITERATIONS;

        if (!object)
            goto cleanup;
        nameValue = XJsonObject_value_keyUtf8(object, "name");
        saltValue = XJsonObject_value_keyUtf8(object, "salt");
        hashValue = XJsonObject_value_keyUtf8(object, "hash");
        if (!nameValue || !saltValue || !hashValue
            || !XJsonValue_isString(nameValue)
            || !XJsonValue_isString(saltValue)
            || !XJsonValue_isString(hashValue))
            goto cleanup;
        name = XString_toUtf8(XJsonValue_toString(nameValue));
        salt = XString_toUtf8(XJsonValue_toString(saltValue));
        hash = XString_toUtf8(XJsonValue_toString(hashValue));
        if (!name || !salt || !hash || !name[0]
            || XStrlen(name) >= XLINK_AUTH_NAME_BYTES)
            goto cleanup;
        if (!xus_hexDecode(salt, record->m_salt, XLINK_AUTH_SALT_BYTES)
            || !xus_hexDecode(hash, record->m_hash, XLINK_AUTH_NONCE_BYTES))
            goto cleanup;

        record->m_name = XString_create_utf8(name);
        if (!record->m_name)
            goto cleanup;

        roleValue = XJsonObject_value_keyUtf8(object, "role");
        if (roleValue) {
            role = XJsonValue_toInt(roleValue, -1);
            if (role < 0)
                goto cleanup;
        }
        if (role < XLinkAuthRole_Anonymous || role > XLinkAuthRole_Admin)
            goto cleanup;
        record->m_role = (uint8_t)role;

        iterationsValue = XJsonObject_value_keyUtf8(object, "iterations");
        if (iterationsValue) {
            iterations = XJsonValue_toInt(iterationsValue, -1);
            if (iterations <= 0 || iterations > 10000000)
                goto cleanup;
        }
        record->m_iterations = (uint32_t)iterations;

        lockedValue = XJsonObject_value_keyUtf8(object, "locked");
        if (lockedValue && XJsonValue_toBool(lockedValue, false))
            record->m_flags |= XLINK_USER_FLAG_LOCKED;
        record->m_flags |= XLINK_USER_FLAG_HAS_PASSWORD;
        store->m_count++;
    }

    ok = true;

cleanup:
    if (buffer) {
        XLinkAuth_secureZero(buffer, readSize);
        XFree_System(buffer);
    }
    if (usersValue) XJsonValue_delete(usersValue);
    if (document) XClassDelete(document);
    if (fd != XFD_INVALID) XDeviceFile_close(fd);
    if (!ok)
        XLinkUserStore_clear(store);
    return ok;
}

bool XLinkUserStore_save(XLinkUserStore* store, const char* path)
{
    XJsonObject* root = NULL;
    XJsonArray* users = NULL;
    XJsonDocument* document = NULL;
    XString* text = NULL;
    XFd fd = XFD_INVALID;
    int error = 0;
    bool ok = false;
    uint32_t i;

    if (!store || !path || !path[0])
        return false;

    root = XJsonObject_create();
    users = XJsonArray_create();
    if (!root || !users || !XJsonObject_insert_keyUtf8_int(root, "version", 1))
        goto cleanup;

    for (i = 0; i < store->m_count; ++i) {
        const XLinkUserRecord* record = &store->m_users[i];
        XJsonObject* object = XJsonObject_create();
        char saltHex[XLINK_USERSTORE_HEX_SALT];
        char hashHex[XLINK_USERSTORE_HEX_HASH];

        xus_hexEncode(record->m_salt, XLINK_AUTH_SALT_BYTES, saltHex,
                      sizeof(saltHex));
        xus_hexEncode(record->m_hash, XLINK_AUTH_NONCE_BYTES, hashHex,
                      sizeof(hashHex));
        if (!object
            || !XJsonObject_insert_keyUtf8_utf8(
                object, "name", XLinkUserStore_name_const(record))
            || !XJsonObject_insert_keyUtf8_int(object, "role", record->m_role)
            || !XJsonObject_insert_keyUtf8_int(object, "iterations",
                                               record->m_iterations)
            || !XJsonObject_insert_keyUtf8_bool(
                object, "locked", XLinkUserStore_isLocked(record))
            || !XJsonObject_insert_keyUtf8_utf8(object, "salt", saltHex)
            || !XJsonObject_insert_keyUtf8_utf8(object, "hash", hashHex)) {
            if (object) XClassDelete(object);
            goto cleanup;
        }
        {
            XJsonValue item;
            XJsonValue_init(&item, XJsonValue_Object);
            item.data.object = object;
            if (!XJsonArray_append_base(users, &item)) {
                XJsonValue_deinit(&item);
                XClassDelete(object);
                goto cleanup;
            }
        }
    }

    if (!XJsonObject_insert_keyUtf8_array(root, "users", users))
        goto cleanup;
    document = XJsonDocument_create_object(root);
    if (!document)
        goto cleanup;
    text = XJsonDocument_toString(document, XJsonDocument_Indented);
    if (!text)
        goto cleanup;

    fd = xus_open(path, XIODevice_WriteOnly | XIODevice_Create
                            | XIODevice_Truncate, &error);
    if (fd == XFD_INVALID)
        goto cleanup;
    {
        const char* utf8 = XString_toUtf8(text);
        size_t total = XString_toUtf8_length(text);
        size_t written = 0;
        while (written < total) {
            int64_t part = XDeviceFile_write(fd, utf8 + written,
                                             (int64_t)(total - written));
            if (part <= 0)
                goto cleanup;
            written += (size_t)part;
        }
    }
    if (!XDeviceFile_flush(fd))
        goto cleanup;
    XDeviceFile_close(fd);
    fd = XFD_INVALID;
    (void)XDeviceFile_setPermissions(path, XFile_ReadOwner | XFile_WriteOwner);
    ok = true;

cleanup:
    if (fd != XFD_INVALID) XDeviceFile_close(fd);
    if (text) XClassDelete(text);
    if (document) XClassDelete(document);
    if (users) XClassDelete(users);
    if (root) XClassDelete(root);
    return ok;
}

/* ========================================================================== */
/*                        账户操作                                           */
/* ========================================================================== */

bool XLinkUserStore_upsert(XLinkUserStore* store, const char* user,
                           const char* password, uint8_t role)
{
    int index;
    XLinkUserRecord* record;

    if (!store || !user || !user[0]
        || XStrlen(user) >= XLINK_AUTH_NAME_BYTES)
        return false;
    if (role > XLinkAuthRole_Admin)
        return false;
    if (store->m_count >= XLINK_AUTH_USER_SLOTS
        && xus_indexOf(store, user) < 0)
        return false;

    index = xus_indexOf(store, user);
    if (index < 0) {
        /* 新建账户必须给口令。 */
        if (!password || !password[0])
            return false;
        record = &store->m_users[store->m_count];
        xus_resetRecord(record);
        record->m_name = XString_create_utf8(user);
        if (!record->m_name)
            return false;
        if (!xus_setPassword(record, password)) {
            xus_resetRecord(record);
            return false;
        }
        record->m_role = role;
        record->m_flags &= (uint8_t)~XLINK_USER_FLAG_LOCKED;
        store->m_count++;
    }
    else {
        record = &store->m_users[index];
        if (password && password[0]) {
            if (!xus_setPassword(record, password))
                return false;
        }
        record->m_role = role;
    }
    return true;
}

bool XLinkUserStore_remove(XLinkUserStore* store, const char* user)
{
    int index;
    if (!store || !user)
        return false;
    index = xus_indexOf(store, user);
    if (index < 0)
        return false;
    xus_resetRecord(&store->m_users[index]);
    if ((uint32_t)index + 1u < store->m_count) {
        memmove(&store->m_users[index], &store->m_users[index + 1],
                (size_t)(store->m_count - (uint32_t)index - 1u)
                    * sizeof(store->m_users[0]));
    }
    store->m_count--;
    if (store->m_count < XLINK_AUTH_USER_SLOTS)
        memset(&store->m_users[store->m_count], 0, sizeof(store->m_users[0]));
    return true;
}

bool XLinkUserStore_setLocked(XLinkUserStore* store, const char* user,
                              bool locked)
{
    int index;
    if (!store || !user)
        return false;
    index = xus_indexOf(store, user);
    if (index < 0)
        return false;
    if (locked)
        store->m_users[index].m_flags |= XLINK_USER_FLAG_LOCKED;
    else
        store->m_users[index].m_flags &= (uint8_t)~XLINK_USER_FLAG_LOCKED;
    return true;
}

const XLinkUserRecord* XLinkUserStore_find(const XLinkUserStore* store,
                                           const char* user)
{
    int index = xus_indexOf(store, user);
    if (index < 0)
        return NULL;
    return &store->m_users[index];
}

uint32_t XLinkUserStore_count(const XLinkUserStore* store)
{
    return store ? store->m_count : 0;
}

const XLinkUserRecord* XLinkUserStore_at(const XLinkUserStore* store,
                                         uint32_t index)
{
    if (!store || index >= store->m_count)
        return NULL;
    return &store->m_users[index];
}

int XLinkUserStore_verify(XLinkUserStore* store, const char* user,
                          const char* password, uint8_t* outRole)
{
    const XLinkUserRecord* record;
    uint8_t computed[XLINK_AUTH_NONCE_BYTES];
    bool matched;

    if (!store || !user || !password)
        return 1;
    record = XLinkUserStore_find(store, user);
    if (!record)
        return 1; /* 用户不存在 */
    if (XLinkUserStore_isLocked(record) || !XLinkUserStore_hasPassword(record))
        return 2; /* 锁定/未设口令 */
    if (!password[0])
        return 3;

    memset(computed, 0, sizeof(computed));
    if (!XLinkAuth_deriveHash(password, record->m_salt, record->m_iterations,
                              computed)) {
        XLinkAuth_secureZero(computed, sizeof(computed));
        return 3;
    }
    matched = XLinkAuth_constantTimeEqual(computed, record->m_hash,
                                          XLINK_AUTH_NONCE_BYTES);
    XLinkAuth_secureZero(computed, sizeof(computed));
    if (!matched)
        return 3; /* 口令错误 */
    if (outRole)
        *outRole = record->m_role;
    return 0;
}

#endif /* XLINK_ON && XLINK_AUTH_ON */
