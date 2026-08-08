/**
 * @file XConsoleShellLogin.c
 * @brief XConsoleShell 登录、账户持久化和 Linux 风格用户命令实现。
 * @details
 * 用户库只通过 XFileSystem 公共 API 操作本地 JSON 文件；密码不保存明文，
 * 使用随机盐和迭代 HMAC-SHA256 摘要。首个 useradd 在账户库不存在时创建
 * UID/GID 为 0 的管理员账户，之后的账户变更必须由管理员执行。
 */

#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_LOGIN_ON

#include "XByteArray.h"
#include "XCryptographicHash.h"
#include "XFileSystem.h"
#include "XJsonArray.h"
#include "XJsonDocument.h"
#include "XJsonObject.h"
#include "XJsonValue.h"
#include "XMemory.h"
#include "XRandomGenerator.h"
#include "XString.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XLOGIN_SALT_SIZE 16u
#define XLOGIN_DIGEST_SIZE 32u

typedef struct XConsoleShellLoginRecord {
    char name[XCONSOLE_SHELL_LOGIN_NAME_SIZE];
    uint32_t uid;
    uint32_t gid;
    uint32_t groups[XCONSOLE_SHELL_LOGIN_GROUP_CAPACITY];
    size_t groupCount;
    uint32_t permissions;
    uint32_t iterations;
    uint8_t salt[XLOGIN_SALT_SIZE];
    uint8_t digest[XLOGIN_DIGEST_SIZE];
    bool passwordSet;
    bool locked;
} XConsoleShellLoginRecord;

static const char g_hex[] = "0123456789abcdef";

static void xlogin_secure_zero(void* data, size_t size)
{
    volatile uint8_t* bytes = (volatile uint8_t*)data;
    if (!bytes) return;
    while (size--) *bytes++ = 0;
}

static const char* xlogin_path(const XConsoleShell* shell)
{
    if (!shell || !shell->m_loginDatabasePath[0])
        return XCONSOLE_SHELL_LOGIN_CONFIG_PATH;
    return shell->m_loginDatabasePath;
}

static bool xlogin_emit(XConsoleShell* shell, const char* text)
{
    return shell && text && XConsoleShell_writeUtf8(shell, text);
}

static void xlogin_set_echo(XConsoleShell* shell, bool enabled)
{
    if (shell && shell->m_io.inputEcho)
        (void)shell->m_io.inputEcho(shell->m_io.userData, enabled);
}

/* 清理交互认证状态，避免密码在会话对象中长期残留。 */
static void xlogin_clear_input(XConsoleShellSession* session)
{
    if (!session) return;
    xlogin_secure_zero(session->loginInputUser, sizeof(session->loginInputUser));
    xlogin_secure_zero(session->loginInputPassword, sizeof(session->loginInputPassword));
    session->loginInputMode = XConsoleShellLoginInput_None;
    session->suppressPrompt = false;
}

static bool xlogin_begin_input(XConsoleShell* shell,
                               XConsoleShellSession* session,
                               XConsoleShellLoginInputMode mode,
                               const char* user, const char* prompt)
{
    if (!shell || !session || !user || !prompt ||
        strlen(user) >= sizeof(session->loginInputUser)) return false;
    xlogin_clear_input(session);
    strcpy(session->loginInputUser, user);
    session->loginInputMode = mode;
    session->suppressPrompt = true;
    xlogin_set_echo(shell, mode == XConsoleShellLoginInput_LoginUser);
    if (!xlogin_emit(shell, prompt)) {
        xlogin_set_echo(shell, true);
        xlogin_clear_input(session);
        return false;
    }
    return true;
}

static bool xlogin_name_valid(const char* name)
{
    size_t i;
    if (!name || !name[0] || strlen(name) >= XCONSOLE_SHELL_LOGIN_NAME_SIZE)
        return false;
    if (!(isalpha((unsigned char)name[0]) || name[0] == '_')) return false;
    for (i = 1; name[i]; ++i) {
        if (!(isalnum((unsigned char)name[i]) || name[i] == '_' || name[i] == '-'))
            return false;
    }
    return true;
}

static bool xlogin_password_valid(const char* password)
{
    return password && password[0] && strlen(password) <= XCONSOLE_SHELL_LOGIN_PASSWORD_SIZE;
}

static bool xlogin_hex_encode(const uint8_t* data, size_t size,
                              char* output, size_t outputSize)
{
    size_t i;
    if ((!data && size) || !output || outputSize < size * 2u + 1u) return false;
    for (i = 0; i < size; ++i) {
        output[i * 2u] = g_hex[data[i] >> 4u];
        output[i * 2u + 1u] = g_hex[data[i] & 0x0fu];
    }
    output[size * 2u] = '\0';
    return true;
}

static int xlogin_hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool xlogin_hex_decode(const char* text, uint8_t* output, size_t size)
{
    size_t i;
    if (!text || !output || strlen(text) != size * 2u) return false;
    for (i = 0; i < size; ++i) {
        int high = xlogin_hex_value(text[i * 2u]);
        int low = xlogin_hex_value(text[i * 2u + 1u]);
        if (high < 0 || low < 0) return false;
        output[i] = (uint8_t)((high << 4) | low);
    }
    return true;
}

static bool xlogin_const_equal(const uint8_t* left, const uint8_t* right, size_t size)
{
    size_t i;
    uint8_t diff = 0;
    if ((!left && size) || (!right && size)) return false;
    for (i = 0; i < size; ++i) diff |= (uint8_t)(left[i] ^ right[i]);
    return diff == 0;
}

static bool xlogin_password_digest(const char* password,
                                   const uint8_t* salt,
                                   uint32_t iterations,
                                   uint8_t output[XLOGIN_DIGEST_SIZE])
{
    uint8_t previous[XLOGIN_DIGEST_SIZE];
    XByteArrayView view;
    uint32_t i;
    size_t passwordSize;
    if (!xlogin_password_valid(password) || !salt || !output || iterations == 0)
        return false;
    passwordSize = strlen(password);
    view = XCryptographicHash_hmacInto((char*)previous, sizeof(previous),
                                       password, passwordSize,
                                       (const char*)salt, XLOGIN_SALT_SIZE,
                                       XCryptographicHash_Sha256);
    if (view.m_size != XLOGIN_DIGEST_SIZE) return false;
    for (i = 1; i < iterations; ++i) {
        view = XCryptographicHash_hmacInto((char*)output, XLOGIN_DIGEST_SIZE,
                                           password, passwordSize,
                                           (const char*)previous,
                                           XLOGIN_DIGEST_SIZE,
                                           XCryptographicHash_Sha256);
        if (view.m_size != XLOGIN_DIGEST_SIZE) return false;
        memcpy(previous, output, sizeof(previous));
    }
    memcpy(output, previous, XLOGIN_DIGEST_SIZE);
    return true;
}

static bool xlogin_set_password(XConsoleShellLoginRecord* record, const char* password)
{
    if (!record || !xlogin_password_valid(password) ||
        !XRandomGenerator_fillSecure(record->salt, sizeof(record->salt)))
        return false;
    record->iterations = XCONSOLE_SHELL_LOGIN_HASH_ITERATIONS;
    if (!xlogin_password_digest(password, record->salt, record->iterations,
                                record->digest))
        return false;
    record->passwordSet = true;
    record->locked = false;
    return true;
}

static bool xlogin_json_string(const XJsonObject* object, const char* key,
                               char* output, size_t outputSize)
{
    XJsonValue* value;
    const XString* string;
    const char* utf8;
    if (!object || !key || !output || outputSize == 0) return false;
    value = XJsonObject_value_keyUtf8(object, key);
    string = value ? XJsonValue_toString(value) : NULL;
    utf8 = string ? XString_toUtf8(string) : NULL;
    if (!utf8 || strlen(utf8) >= outputSize) {
        if (value) XJsonValue_delete(value);
        return false;
    }
    strcpy(output, utf8);
    XJsonValue_delete(value);
    return true;
}

static bool xlogin_json_int(const XJsonObject* object, const char* key,
                            uint32_t* output, bool required)
{
    XJsonValue* value;
    if (!object || !key || !output) return false;
    value = XJsonObject_value_keyUtf8(object, key);
    if (!value || !XJsonValue_isInt(value)) {
        if (value) XJsonValue_delete(value);
        return !required;
    }
    {
        int64_t number = XJsonValue_toInt(value, -1);
        XJsonValue_delete(value);
        if (number < 0 || (uint64_t)number > UINT32_MAX) return false;
        *output = (uint32_t)number;
    }
    return true;
}

static bool xlogin_json_bool(const XJsonObject* object, const char* key,
                             bool* output)
{
    XJsonValue* value;
    if (!object || !key || !output) return false;
    value = XJsonObject_value_keyUtf8(object, key);
    if (!value) return false;
    if (!XJsonValue_isBool(value)) {
        XJsonValue_delete(value);
        return false;
    }
    *output = XJsonValue_toBool(value, false);
    XJsonValue_delete(value);
    return true;
}

static bool xlogin_read_groups(const XJsonObject* object,
                               XConsoleShellLoginRecord* record)
{
    XJsonValue* value;
    const XJsonArray* array;
    size_t i;
    if (!object || !record) return false;
    record->groupCount = 0;
    value = XJsonObject_value_keyUtf8(object, "groups");
    if (!value) return false;
    array = XJsonValue_toArray(value);
    if (!array || XJsonArray_size_base(array) > XCONSOLE_SHELL_LOGIN_GROUP_CAPACITY) {
        XJsonValue_delete(value);
        return false;
    }
    for (i = 0; i < XJsonArray_size_base(array); ++i) {
        const XJsonValue* item = XJsonArray_at_const(array, (int64_t)i);
        int64_t group = item && XJsonValue_isInt(item) ?
            XJsonValue_toInt(item, -1) : -1;
        if (group < 0 || (uint64_t)group > UINT32_MAX) {
            XJsonValue_delete(value);
            return false;
        }
        record->groups[record->groupCount++] = (uint32_t)group;
    }
    XJsonValue_delete(value);
    return true;
}

static bool xlogin_read_record(const XJsonObject* object,
                               XConsoleShellLoginRecord* record)
{
    char salt[2u * XLOGIN_SALT_SIZE + 1u];
    char digest[2u * XLOGIN_DIGEST_SIZE + 1u];
    uint32_t iterations = XCONSOLE_SHELL_LOGIN_HASH_ITERATIONS;
    bool passwordSet = true;
    if (!object || !record) return false;
    memset(record, 0, sizeof(*record));
    if (!xlogin_json_string(object, "name", record->name, sizeof(record->name)) ||
        !xlogin_name_valid(record->name) ||
        !xlogin_json_int(object, "uid", &record->uid, true) ||
        !xlogin_json_int(object, "gid", &record->gid, true) ||
        !xlogin_json_int(object, "permissions", &record->permissions, true) ||
        !xlogin_read_groups(object, record))
        return false;
    if (XJsonObject_contains_keyUtf8(object, "passwordSet") &&
        !xlogin_json_bool(object, "passwordSet", &passwordSet))
        return false;
    record->passwordSet = passwordSet;
    if (!xlogin_json_string(object, "salt", salt, sizeof(salt)) ||
        !xlogin_json_string(object, "hash", digest, sizeof(digest)) ||
        !xlogin_hex_decode(salt, record->salt, sizeof(record->salt)) ||
        !xlogin_hex_decode(digest, record->digest, sizeof(record->digest)))
        return false;
    if (XJsonObject_contains_keyUtf8(object, "iterations") &&
        !xlogin_json_int(object, "iterations", &iterations, true))
        return false;
    if (passwordSet && iterations == 0) return false;
    record->iterations = iterations;
    if (XJsonObject_contains_keyUtf8(object, "locked") &&
        !xlogin_json_bool(object, "locked", &record->locked))
        return false;
    return true;
}

static bool xlogin_load(XConsoleShell* shell,
                        XConsoleShellLoginRecord* records,
                        size_t* count, bool* exists)
{
    XString* path;
    XFileStat stat;
    XFd fd = XFD_INVALID;
    XString* text = NULL;
    XJsonDocument* document = NULL;
    XJsonObject* root;
    XJsonValue* usersValue = NULL;
    const XJsonArray* users;
    char buffer[XCONSOLE_SHELL_LOGIN_CONFIG_MAX_BYTES + 1u];
    size_t readSize = 0;
    int error = 0;
    size_t i;
    if (!shell || !records || !count || !exists) return false;
    *count = 0;
    *exists = false;
    path = XString_create_utf8(xlogin_path(shell));
    if (!path) return false;
    if (!XFileSystem_stat(path, &stat) || !stat.exists) {
        XString_delete_base(path);
        return true;
    }
    *exists = true;
    if (!stat.isFile || stat.size < 0 ||
        (uint64_t)stat.size > XCONSOLE_SHELL_LOGIN_CONFIG_MAX_BYTES) {
        XString_delete_base(path);
        return false;
    }
    fd = XFileSystem_open(path, XFileSystem_ReadOnly, &error);
    XString_delete_base(path);
    if (fd == XFD_INVALID) return false;
    while (readSize < (size_t)stat.size) {
        int64_t got = XFileSystem_read(fd, buffer + readSize,
                                       (int64_t)((size_t)stat.size - readSize));
        if (got <= 0) {
            XFileSystem_close(fd);
            return false;
        }
        readSize += (size_t)got;
    }
    XFileSystem_close(fd);
    buffer[readSize] = '\0';
    text = XString_create_with_length_utf8(buffer, readSize);
    document = text ? XJsonDocument_fromString(text) : NULL;
    if (text) XString_delete_base(text);
    root = document ? XJsonDocument_object(document) : NULL;
    usersValue = root ? XJsonObject_value_keyUtf8(root, "users") : NULL;
    users = usersValue ? XJsonValue_toArray(usersValue) : NULL;
    if (!document || !root || !users ||
        XJsonArray_size_base(users) > XCONSOLE_SHELL_LOGIN_USER_CAPACITY) {
        if (usersValue) XJsonValue_delete(usersValue);
        if (document) XJsonDocument_delete(document);
        return false;
    }
    for (i = 0; i < XJsonArray_size_base(users); ++i) {
        const XJsonValue* value = XJsonArray_at_const(users, (int64_t)i);
        const XJsonObject* object = value ? XJsonValue_toObject(value) : NULL;
        if (!object || !xlogin_read_record(object, &records[*count])) {
            XJsonValue_delete(usersValue);
            XJsonDocument_delete(document);
            return false;
        }
        ++*count;
    }
    XJsonValue_delete(usersValue);
    XJsonDocument_delete(document);
    return true;
}

static bool xlogin_save(XConsoleShell* shell,
                         const XConsoleShellLoginRecord* records,
                         size_t count)
{
    XJsonObject* root = NULL;
    XJsonArray* users = NULL;
    XJsonDocument* document = NULL;
    XString* text = NULL;
    XString* path = NULL;
    XFd fd = XFD_INVALID;
    size_t i;
    int error = 0;
    bool ok = false;
    if (!shell || (!records && count) || count > XCONSOLE_SHELL_LOGIN_USER_CAPACITY)
        return false;
    root = XJsonObject_create();
    users = XJsonArray_create();
    if (!root || !users || !XJsonObject_insert_keyUtf8_int(root, "version", 1))
        goto cleanup;
    for (i = 0; i < count; ++i) {
        XJsonObject* object = XJsonObject_create();
        XJsonArray* groups = XJsonArray_create();
        XJsonValue* value = NULL;
        char salt[2u * XLOGIN_SALT_SIZE + 1u];
        char digest[2u * XLOGIN_DIGEST_SIZE + 1u];
        size_t j;
        if (!object || !groups ||
            !xlogin_hex_encode(records[i].salt, sizeof(records[i].salt), salt, sizeof(salt)) ||
            !xlogin_hex_encode(records[i].digest, sizeof(records[i].digest), digest, sizeof(digest)) ||
            !XJsonObject_insert_keyUtf8_utf8(object, "name", records[i].name) ||
            !XJsonObject_insert_keyUtf8_int(object, "uid", records[i].uid) ||
            !XJsonObject_insert_keyUtf8_int(object, "gid", records[i].gid) ||
            !XJsonObject_insert_keyUtf8_int(object, "permissions", records[i].permissions) ||
            !XJsonObject_insert_keyUtf8_int(object, "iterations", records[i].iterations) ||
            !XJsonObject_insert_keyUtf8_bool(object, "passwordSet", records[i].passwordSet) ||
            !XJsonObject_insert_keyUtf8_utf8(object, "salt", salt) ||
            !XJsonObject_insert_keyUtf8_utf8(object, "hash", digest) ||
            !XJsonObject_insert_keyUtf8_bool(object, "locked", records[i].locked)) {
            if (groups) XJsonArray_delete_base(groups);
            if (object) XJsonObject_delete_base(object);
            goto cleanup;
        }
        for (j = 0; j < records[i].groupCount; ++j) {
            XJsonValue item;
            XJsonValue_init(&item, XJsonValue_Int);
            item.data.integer = records[i].groups[j];
            if (!XJsonArray_append_base(groups, &item)) {
                XJsonValue_deinit(&item);
                XJsonArray_delete_base(groups);
                XJsonObject_delete_base(object);
                goto cleanup;
            }
            XJsonValue_deinit(&item);
        }
        if (!XJsonObject_insert_keyUtf8_array(object, "groups", groups)) {
            XJsonArray_delete_base(groups);
            XJsonObject_delete_base(object);
            goto cleanup;
        }
        XJsonArray_delete_base(groups);
        value = XJsonValue_create_object(object);
        XJsonObject_delete_base(object);
        if (!value || !XJsonArray_append_base(users, value)) {
            if (value) XJsonValue_delete(value);
            goto cleanup;
        }
        XJsonValue_delete(value);
    }
    if (!XJsonObject_insert_keyUtf8_array(root, "users", users)) goto cleanup;
    document = XJsonDocument_create_object(root);
    if (!document) goto cleanup;
    text = XJsonDocument_toString(document, XJsonDocument_Indented);
    path = XString_create_utf8(xlogin_path(shell));
    if (!text || !path) goto cleanup;
    fd = XFileSystem_open(path, XFileSystem_WriteOnly | XFileSystem_Create |
                               XFileSystem_Truncate, &error);
    if (fd == XFD_INVALID) goto cleanup;
    {
        const char* utf8 = XString_toUtf8(text);
        size_t total = XString_toUtf8_length(text);
        size_t written = 0;
        while (written < total) {
            int64_t part = XFileSystem_write(fd, utf8 + written,
                                             (int64_t)(total - written));
            if (part <= 0) goto cleanup;
            written += (size_t)part;
        }
    }
    if (!XFileSystem_flush(fd)) goto cleanup;
    XFileSystem_close(fd);
    fd = XFD_INVALID;
    /* Linux 使用 0600；FatFs 等不支持权限的后端将忽略失败。 */
    (void)XFileSystem_setPermissions(path, XFile_ReadOwner | XFile_WriteOwner);
    ok = true;
cleanup:
    if (fd != XFD_INVALID) XFileSystem_close(fd);
    if (path) XString_delete_base(path);
    if (text) XString_delete_base(text);
    if (document) XJsonDocument_delete(document);
    if (users) XJsonArray_delete_base(users);
    if (root) XJsonObject_delete_base(root);
    return ok;
}

static XConsoleShellLoginRecord* xlogin_find(XConsoleShellLoginRecord* records,
                                             size_t count, const char* name)
{
    size_t i;
    if (!records || !name) return NULL;
    for (i = 0; i < count; ++i)
        if (strcmp(records[i].name, name) == 0) return &records[i];
    return NULL;
}

static bool xlogin_is_admin(const XConsoleShellSession* session)
{
    return session && session->authenticated &&
        ((session->uid == 0u) ||
         (session->permissionMask & XConsoleShellPermission_Administrator));
}

/* Linux 没有独立组名库时，用“UID == GID”的账户名作为组名提示。 */
static const char* xlogin_group_name(const XConsoleShellLoginRecord* records,
                                     size_t count, uint32_t gid)
{
    size_t i;
    if (!records) return NULL;
    for (i = 0; i < count; ++i)
        if (records[i].uid == gid) return records[i].name;
    return NULL;
}

static void xlogin_clear_session(XConsoleShell* shell,
                                 XConsoleShellSession* session)
{
    if (!session) return;
    xlogin_set_echo(shell, true);
    xlogin_clear_input(session);
    session->authenticated = XCONSOLE_SHELL_AUTH_ON ? false : true;
    session->permissionMask = XCONSOLE_SHELL_AUTH_ON ? 0u : UINT32_MAX;
#if XCONSOLE_SHELL_LOGIN_ON
    session->userName[0] = '\0';
    session->uid = UINT32_MAX;
    session->gid = UINT32_MAX;
    session->groupCount = 0;
    memset(session->groups, 0, sizeof(session->groups));
#endif
}

static void xlogin_apply_session(XConsoleShell* shell,
                                 XConsoleShellSession* session,
                                 const XConsoleShellLoginRecord* record)
{
    if (!session || !record) return;
    xlogin_set_echo(shell, true);
    xlogin_clear_input(session);
    session->authenticated = true;
    session->permissionMask = record->permissions;
    if (record->uid == 0u)
        session->permissionMask |= XConsoleShellPermission_Dangerous |
                                   XConsoleShellPermission_Administrator;
#if XCONSOLE_SHELL_LOGIN_ON
    strncpy(session->userName, record->name, sizeof(session->userName) - 1u);
    session->userName[sizeof(session->userName) - 1u] = '\0';
    session->uid = record->uid;
    session->gid = record->gid;
    session->groupCount = record->groupCount;
    memcpy(session->groups, record->groups, sizeof(session->groups));
#endif
}

static bool xlogin_parse_u32(const char* text, uint32_t* value)
{
    char* end = NULL;
    unsigned long parsed;
    if (!text || !text[0] || !value) return false;
    parsed = strtoul(text, &end, 0);
    if (!end || *end != '\0' || parsed > UINT32_MAX) return false;
    *value = (uint32_t)parsed;
    return true;
}

static bool xlogin_parse_groups(const char* text, uint32_t* groups, size_t* count)
{
    char buffer[128];
    size_t length;
    size_t offset = 0;
    size_t used = 0;
    if (!text || !groups || !count || !text[0] || strlen(text) >= sizeof(buffer))
        return false;
    strcpy(buffer, text);
    length = strlen(buffer);
    while (offset < length) {
        char* token = buffer + offset;
        char* separator = strchr(token, ',');
        if (separator) {
            *separator = '\0';
            offset = (size_t)(separator - buffer) + 1u;
            if (offset >= length) return false;
        } else {
            offset = length;
        }
        if (used >= XCONSOLE_SHELL_LOGIN_GROUP_CAPACITY ||
            !xlogin_parse_u32(token, &groups[used])) return false;
        ++used;
    }
    *count = used;
    return true;
}

typedef struct XConsoleShellLoginOptionState {
    bool appendGroups;
    bool systemUser;
    bool uidSet;
    bool gidSet;
    bool lockUsed;
    bool unlockUsed;
    char newName[XCONSOLE_SHELL_LOGIN_NAME_SIZE];
} XConsoleShellLoginOptionState;

static bool xlogin_option_requires_value(const char* option)
{
    return option && (strcmp(option, "-u") == 0 || strcmp(option, "--uid") == 0 ||
        strcmp(option, "-g") == 0 || strcmp(option, "--gid") == 0 ||
        strcmp(option, "-G") == 0 || strcmp(option, "--groups") == 0 ||
        strcmp(option, "-aG") == 0 ||
        strcmp(option, "-l") == 0 || strcmp(option, "--login") == 0 ||
        strcmp(option, "--permissions") == 0);
}

static bool xlogin_find_login_argument(int argc, const char* const* argv,
                                       int* loginIndex)
{
    int i;
    int found = -1;
    if (!argv || !loginIndex) return false;
    for (i = 0; i < argc; ++i) {
        if (argv[i][0] == '-') {
            if (strcmp(argv[i], "--") == 0 ||
                (!xlogin_option_requires_value(argv[i]) &&
                 strcmp(argv[i], "-a") != 0 && strcmp(argv[i], "--append") != 0 &&
                 strcmp(argv[i], "-L") != 0 && strcmp(argv[i], "--lock") != 0 &&
                 strcmp(argv[i], "-U") != 0 && strcmp(argv[i], "--unlock") != 0 &&
                 strcmp(argv[i], "-r") != 0 && strcmp(argv[i], "--system") != 0 &&
                 strcmp(argv[i], "--admin") != 0))
                return false;
            if (xlogin_option_requires_value(argv[i]) && ++i >= argc) return false;
            continue;
        }
        if (found >= 0) return false;
        found = i;
    }
    if (found < 0 || !xlogin_name_valid(argv[found])) return false;
    *loginIndex = found;
    return true;
}

static bool xlogin_append_groups(XConsoleShellLoginRecord* record,
                                 const uint32_t* groups, size_t count)
{
    size_t i;
    if (!record || (!groups && count)) return false;
    for (i = 0; i < count; ++i) {
        size_t j;
        bool exists = false;
        for (j = 0; j < record->groupCount; ++j)
            if (record->groups[j] == groups[i]) exists = true;
        if (!exists) {
            if (record->groupCount >= XCONSOLE_SHELL_LOGIN_GROUP_CAPACITY) return false;
            record->groups[record->groupCount++] = groups[i];
        }
    }
    return true;
}

static bool xlogin_parse_user_options(XConsoleShellLoginRecord* record,
                                      int argc, const char* const* argv,
                                      int loginIndex,
                                      XConsoleShellLoginOptionState* state)
{
    int i;
    uint32_t oldGid;
    bool gidChanged = false;
    if (!record || !argv || loginIndex < 0 || loginIndex >= argc || !state) return false;
    memset(state, 0, sizeof(*state));
    oldGid = record->gid;
    for (i = 0; i < argc; ++i) {
        const char* option;
        uint32_t number;
        if (i == loginIndex) continue;
        option = argv[i];
        if (strcmp(option, "-u") == 0 || strcmp(option, "--uid") == 0 ||
            strcmp(option, "-g") == 0 || strcmp(option, "--gid") == 0 ||
            strcmp(option, "--permissions") == 0) {
            if (++i >= argc || !xlogin_parse_u32(argv[i], &number)) return false;
            if (strcmp(option, "-u") == 0 || strcmp(option, "--uid") == 0)
                record->uid = number, state->uidSet = true;
            else if (strcmp(option, "-g") == 0 || strcmp(option, "--gid") == 0) {
                record->gid = number, state->gidSet = true;
                gidChanged = true;
            } else {
                record->permissions = number;
            }
        } else if (strcmp(option, "-G") == 0 || strcmp(option, "--groups") == 0 ||
                   strcmp(option, "-aG") == 0) {
            uint32_t groups[XCONSOLE_SHELL_LOGIN_GROUP_CAPACITY];
            size_t count = 0;
            if (++i >= argc || !xlogin_parse_groups(argv[i], groups, &count)) return false;
            if (strcmp(option, "-aG") == 0) state->appendGroups = true;
            if (state->appendGroups) {
                if (!xlogin_append_groups(record, groups, count)) return false;
            } else {
                memcpy(record->groups, groups, count * sizeof(groups[0]));
                record->groupCount = count;
            }
        } else if (strcmp(option, "-a") == 0 || strcmp(option, "--append") == 0) {
            state->appendGroups = true;
        } else if (strcmp(option, "-l") == 0 || strcmp(option, "--login") == 0) {
            if (++i >= argc || !xlogin_name_valid(argv[i]) ||
                strlen(argv[i]) >= sizeof(state->newName)) return false;
            strcpy(state->newName, argv[i]);
        } else if (strcmp(option, "-L") == 0 || strcmp(option, "--lock") == 0) {
            record->locked = true;
            state->lockUsed = true;
        } else if (strcmp(option, "-U") == 0 || strcmp(option, "--unlock") == 0) {
            record->locked = false;
            state->unlockUsed = true;
        } else if (strcmp(option, "-r") == 0 || strcmp(option, "--system") == 0) {
            state->systemUser = true;
        } else if (strcmp(option, "--admin") == 0) {
            record->permissions |= XConsoleShellPermission_Dangerous |
                                   XConsoleShellPermission_Administrator;
        } else {
            return false;
        }
    }
    if (gidChanged && record->groupCount > 0 && record->groups[0] == oldGid)
        record->groups[0] = record->gid;
    return true;
}

static int xlogin_begin_login_password(XConsoleShell* shell,
                                       XConsoleShellSession* session,
                                       const char* name)
{
    XConsoleShellLoginRecord records[XCONSOLE_SHELL_LOGIN_USER_CAPACITY];
    XConsoleShellLoginRecord* record;
    size_t count;
    bool exists;
    if (!shell || !session || !xlogin_name_valid(name))
        return XConsoleResult_InvalidArgument;
    if (!xlogin_load(shell, records, &count, &exists))
        return xlogin_emit(shell, "login: 用户配置文件无效\n") ?
            XConsoleResult_Failed : XConsoleResult_IoError;
    if (!exists) return xlogin_emit(shell, "login: 用户配置文件不存在，请先执行 useradd\n") ?
        XConsoleResult_NotSupported : XConsoleResult_IoError;
    record = xlogin_find(records, count, name);
    if (!record || !record->passwordSet)
        return xlogin_emit(shell, "login: 账户尚未设置密码，请先执行 passwd\n") ?
            XConsoleResult_PermissionDenied : XConsoleResult_IoError;
    if (record->locked)
        return xlogin_emit(shell, "login: 账户已锁定\n") ?
            XConsoleResult_PermissionDenied : XConsoleResult_IoError;
    return xlogin_begin_input(shell, session,
                              XConsoleShellLoginInput_LoginPassword,
                              record->name, "密码: ") ?
        XConsoleResult_MoreOutput : XConsoleResult_IoError;
}

static int xlogin_login(XConsoleShell* shell, XConsoleShellSession* session,
                        int argc, const char* const* argv, void* userData)
{
    (void)userData;
    if (!shell || !session || (argc != 0 && argc != 1) || (argc && !argv))
        return XConsoleResult_InvalidArgument;
    if (argc == 0)
        return xlogin_begin_input(shell, session, XConsoleShellLoginInput_LoginUser,
                                  "", "用户名: ") ?
            XConsoleResult_MoreOutput : XConsoleResult_IoError;
    return xlogin_begin_login_password(shell, session, argv[0]);
}

static int xlogin_logout(XConsoleShell* shell, XConsoleShellSession* session,
                         int argc, const char* const* argv, void* userData)
{
    (void)argv;
    (void)userData;
    if (!shell || !session || argc != 0) return XConsoleResult_InvalidArgument;
    xlogin_clear_session(shell, session);
    return xlogin_emit(shell, "logout: 已注销\n") ?
        XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xlogin_whoami(XConsoleShell* shell, XConsoleShellSession* session,
                         int argc, const char* const* argv, void* userData)
{
    const char* name;
    (void)argv;
    (void)userData;
    if (!shell || !session || argc != 0) return XConsoleResult_InvalidArgument;
    name = session->authenticated && session->userName[0] ? session->userName : "匿名";
    return XConsoleShell_writeUtf8(shell, name) &&
           XConsoleShell_writeUtf8(shell, "\n") ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xlogin_id(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XConsoleShellLoginRecord records[XCONSOLE_SHELL_LOGIN_USER_CAPACITY];
    XConsoleShellLoginRecord* record = NULL;
    const char* name;
    const uint32_t* groups;
    size_t groupCount;
    size_t count = 0;
    size_t i;
    bool exists = false;
    char line[128];
    uint32_t uid;
    uint32_t gid;
    (void)userData;
    if (!shell || !session || argc > 1 || (argc && !argv))
        return XConsoleResult_InvalidArgument;
    if (!session->authenticated) return XConsoleResult_PermissionDenied;
    if (argc == 1) {
        if (!xlogin_name_valid(argv[0]) ||
            !xlogin_load(shell, records, &count, &exists) || !exists)
            return XConsoleResult_PermissionDenied;
        record = xlogin_find(records, count, argv[0]);
        if (!record) return XConsoleResult_PermissionDenied;
        name = record->name;
        uid = record->uid;
        gid = record->gid;
        groups = record->groups;
        groupCount = record->groupCount;
    } else {
        name = session->userName;
        uid = session->uid;
        gid = session->gid;
        groups = session->groups;
        groupCount = session->groupCount;
        (void)xlogin_load(shell, records, &count, &exists);
    }
    snprintf(line, sizeof(line), "uid=%u(%s) gid=%u groups=", uid,
             name && name[0] ? name : "?", gid);
    if (!XConsoleShell_writeUtf8(shell, line)) return XConsoleResult_IoError;
    for (i = 0; i < groupCount; ++i) {
        const char* gname = xlogin_group_name(records, count, groups[i]);
        if (i && !XConsoleShell_writeUtf8(shell, ",")) return XConsoleResult_IoError;
        if (gname)
            snprintf(line, sizeof(line), "%u(%s)", groups[i], gname);
        else
            snprintf(line, sizeof(line), "%u", groups[i]);
        if (!XConsoleShell_writeUtf8(shell, line)) return XConsoleResult_IoError;
    }
    return XConsoleShell_writeUtf8(shell, "\n") ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xlogin_groups(XConsoleShell* shell, XConsoleShellSession* session,
                         int argc, const char* const* argv, void* userData)
{
    XConsoleShellLoginRecord records[XCONSOLE_SHELL_LOGIN_USER_CAPACITY];
    XConsoleShellLoginRecord* record = NULL;
    const char* name;
    const uint32_t* groups;
    size_t groupCount;
    size_t count = 0;
    size_t i;
    bool exists = false;
    char line[32];
    (void)userData;
    if (!shell || !session || argc > 1 || (argc && !argv))
        return XConsoleResult_InvalidArgument;
    if (!session->authenticated) return XConsoleResult_PermissionDenied;
    if (argc == 1) {
        if (!xlogin_name_valid(argv[0]) ||
            !xlogin_load(shell, records, &count, &exists) || !exists)
            return XConsoleResult_PermissionDenied;
        record = xlogin_find(records, count, argv[0]);
        if (!record) return XConsoleResult_PermissionDenied;
        groups = record->groups;
        groupCount = record->groupCount;
    } else {
        (void)xlogin_load(shell, records, &count, &exists);
        groups = session->groups;
        groupCount = session->groupCount;
    }
    name = record ? record->name : session->userName;
    if (name && name[0]) {
        if (!XConsoleShell_writeUtf8(shell, name)) return XConsoleResult_IoError;
        if (!XConsoleShell_writeUtf8(shell, " :")) return XConsoleResult_IoError;
    }
    for (i = 0; i < groupCount; ++i) {
        const char* gname = xlogin_group_name(records, count, groups[i]);
        if (!XConsoleShell_writeUtf8(shell, " ")) return XConsoleResult_IoError;
        if (gname)
            snprintf(line, sizeof(line), "%s", gname);
        else
            snprintf(line, sizeof(line), "%u", groups[i]);
        if (!XConsoleShell_writeUtf8(shell, line)) return XConsoleResult_IoError;
    }
    return XConsoleShell_writeUtf8(shell, "\n") ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xlogin_userlist(XConsoleShell* shell, XConsoleShellSession* session,
                           int argc, const char* const* argv, void* userData)
{
    XConsoleShellLoginRecord records[XCONSOLE_SHELL_LOGIN_USER_CAPACITY];
    size_t count, i;
    bool exists;
    char line[128];
    (void)argv;
    (void)userData;
    if (!shell || !session || argc != 0) return XConsoleResult_InvalidArgument;
    if (!xlogin_load(shell, records, &count, &exists)) return XConsoleResult_Failed;
    if (!exists) return xlogin_emit(shell, "users: 用户配置文件不存在\n") ?
        XConsoleResult_NotSupported : XConsoleResult_IoError;
    for (i = 0; i < count; ++i) {
        snprintf(line, sizeof(line), "%s uid=%u gid=%u permissions=%u%s%s\n",
                 records[i].name, records[i].uid, records[i].gid,
                 records[i].permissions, records[i].passwordSet ? "" : " 未设密码",
                 records[i].locked ? " 已锁定" : "");
        if (!XConsoleShell_writeUtf8(shell, line)) return XConsoleResult_IoError;
    }
    return XConsoleResult_Ok;
}

static int xlogin_users(XConsoleShell* shell, XConsoleShellSession* session,
                        int argc, const char* const* argv, void* userData)
{
    (void)argv;
    (void)userData;
    if (!shell || !session || argc != 0) return XConsoleResult_InvalidArgument;
    if (!session->authenticated) return XConsoleResult_PermissionDenied;
    if (!XConsoleShell_writeUtf8(shell, session->userName[0] ? session->userName : "\n"))
        return XConsoleResult_IoError;
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    {
        size_t i;
        for (i = 0; i < XCONSOLE_SHELL_MAX_SESSIONS - 1u; ++i) {
            XConsoleShellSession* other = &shell->m_sessions[i];
            if (other->m_open && other->authenticated && other->userName[0]) {
                if (!XConsoleShell_writeUtf8(shell, " ") ||
                    !XConsoleShell_writeUtf8(shell, other->userName))
                    return XConsoleResult_IoError;
            }
        }
    }
#endif
    return XConsoleShell_writeUtf8(shell, "\n") ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xlogin_useradd(XConsoleShell* shell, XConsoleShellSession* session,
                          int argc, const char* const* argv, void* userData)
{
    XConsoleShellLoginRecord records[XCONSOLE_SHELL_LOGIN_USER_CAPACITY];
    XConsoleShellLoginRecord* record;
    XConsoleShellLoginOptionState options;
    size_t count, i;
    int loginIndex;
    bool exists;
    uint32_t candidate;
    (void)userData;
    if (!shell || !session || argc < 1 || !argv ||
        !xlogin_find_login_argument(argc, argv, &loginIndex))
        return XConsoleResult_InvalidArgument;
    if (!xlogin_load(shell, records, &count, &exists) ||
        count >= XCONSOLE_SHELL_LOGIN_USER_CAPACITY)
        return XConsoleResult_Failed;
    if (exists && count > 0 && !xlogin_is_admin(session))
        return XConsoleResult_PermissionDenied;
    if (xlogin_find(records, count, argv[loginIndex])) return XConsoleResult_Failed;
    memset(&records[count], 0, sizeof(records[count]));
    strcpy(records[count].name, argv[loginIndex]);
    records[count].permissions = exists ? 0u :
        (XConsoleShellPermission_Dangerous | XConsoleShellPermission_Administrator);
    records[count].passwordSet = false;
    records[count].locked = true;
    if (!xlogin_parse_user_options(&records[count], argc, argv, loginIndex, &options))
        return XConsoleResult_InvalidArgument;
    /* useradd 只接受 Linux useradd 的选项；-a/-aG/-L/-U/-l 是 usermod 专用。 */
    if (options.appendGroups || options.lockUsed || options.unlockUsed ||
        options.newName[0])
        return XConsoleResult_InvalidArgument;
    if (!exists) {
        records[count].uid = 0u;
        records[count].gid = 0u;
    } else if (options.uidSet && !options.gidSet) {
        records[count].gid = records[count].uid;
    } else if (!options.uidSet) {
        candidate = options.systemUser ? 999u : 1000u;
        if (options.systemUser) {
            while (candidate > 0u) {
                bool used = false;
                for (i = 0; i < count; ++i) if (records[i].uid == candidate) used = true;
                if (!used) break;
                --candidate;
            }
        } else {
            for (;;) {
                bool used = false;
                for (i = 0; i < count; ++i) if (records[i].uid == candidate) used = true;
                if (!used) break;
                if (candidate == UINT32_MAX) return XConsoleResult_ResourceLimit;
                ++candidate;
            }
        }
        if (options.systemUser && candidate == 0u) return XConsoleResult_ResourceLimit;
        records[count].uid = candidate;
        if (!options.gidSet) records[count].gid = candidate;
    }
    if (records[count].groupCount == 0u) {
        records[count].groups[0] = records[count].gid;
        records[count].groupCount = 1u;
    }
    ++count;
    if (!xlogin_save(shell, records, count)) return XConsoleResult_IoError;
    return XConsoleShell_writeUtf8(shell, "useradd: 用户已创建，请使用 passwd 设置密码\n") ?
        XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xlogin_userdel(XConsoleShell* shell, XConsoleShellSession* session,
                          int argc, const char* const* argv, void* userData)
{
    XConsoleShellLoginRecord records[XCONSOLE_SHELL_LOGIN_USER_CAPACITY];
    XConsoleShellLoginRecord* target;
    size_t count, i;
    bool exists;
    size_t adminCount = 0;
    int loginIndex = -1;
    bool removeHome = false;
    (void)userData;
    if (!shell || !session || argc < 1 || !argv)
        return XConsoleResult_InvalidArgument;
    for (i = 0; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--remove") == 0) {
            removeHome = true;
        } else if (argv[i][0] == '-') {
            return XConsoleResult_InvalidArgument;
        } else if (loginIndex >= 0) {
            return XConsoleResult_InvalidArgument;
        } else {
            loginIndex = (int)i;
        }
    }
    if (loginIndex < 0 || !xlogin_name_valid(argv[loginIndex]))
        return XConsoleResult_InvalidArgument;
    if (removeHome)
        return xlogin_emit(shell, "userdel: 主目录抽象未启用，-r 不支持\n") ?
            XConsoleResult_NotSupported : XConsoleResult_IoError;
    if (!xlogin_load(shell, records, &count, &exists) || !exists ||
        !xlogin_is_admin(session)) return XConsoleResult_PermissionDenied;
    target = xlogin_find(records, count, argv[loginIndex]);
    if (!target || (session->userName[0] && strcmp(session->userName, argv[loginIndex]) == 0))
        return XConsoleResult_PermissionDenied;
    for (i = 0; i < count; ++i)
        if (records[i].permissions & XConsoleShellPermission_Administrator)
            ++adminCount;
    if ((target->permissions & XConsoleShellPermission_Administrator) && adminCount <= 1u)
        return XConsoleResult_PermissionDenied;
    memmove(target, target + 1, (size_t)(&records[count] - target - 1) * sizeof(*target));
    --count;
    if (!xlogin_save(shell, records, count)) return XConsoleResult_IoError;
    return XConsoleShell_writeUtf8(shell, "userdel: 用户已删除\n") ?
        XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xlogin_usermod(XConsoleShell* shell, XConsoleShellSession* session,
                          int argc, const char* const* argv, void* userData)
{
    XConsoleShellLoginRecord records[XCONSOLE_SHELL_LOGIN_USER_CAPACITY];
    XConsoleShellLoginRecord* target;
    XConsoleShellLoginOptionState options;
    int loginIndex;
    char oldName[XCONSOLE_SHELL_LOGIN_NAME_SIZE];
    size_t count;
    bool exists;
    (void)userData;
    if (!shell || !session || argc < 1 || !argv ||
        !xlogin_find_login_argument(argc, argv, &loginIndex))
        return XConsoleResult_InvalidArgument;
    if (!xlogin_load(shell, records, &count, &exists) || !exists ||
        !xlogin_is_admin(session)) return XConsoleResult_PermissionDenied;
    target = xlogin_find(records, count, argv[loginIndex]);
    if (!target || !xlogin_parse_user_options(target, argc, argv, loginIndex, &options) ||
        options.systemUser)
        return XConsoleResult_InvalidArgument;
    strcpy(oldName, target->name);
    if (options.newName[0]) {
        if (xlogin_find(records, count, options.newName) &&
            strcmp(options.newName, oldName) != 0)
            return XConsoleResult_Failed;
        strcpy(target->name, options.newName);
    }
    if (!xlogin_save(shell, records, count)) return XConsoleResult_IoError;
    if (session->userName[0] && strcmp(session->userName, oldName) == 0) {
        xlogin_apply_session(shell, session, target);
        if (options.newName[0]) {
            strncpy(session->userName, target->name, sizeof(session->userName) - 1u);
            session->userName[sizeof(session->userName) - 1u] = '\0';
        }
    }
    return XConsoleShell_writeUtf8(shell, "usermod: 用户已更新\n") ?
        XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xlogin_passwd(XConsoleShell* shell, XConsoleShellSession* session,
                         int argc, const char* const* argv, void* userData)
{
    XConsoleShellLoginRecord records[XCONSOLE_SHELL_LOGIN_USER_CAPACITY];
    XConsoleShellLoginRecord* target;
    const char* name;
    size_t count;
    bool exists;
    bool bootstrap = false;
    (void)userData;
    if (!shell || !session || (argc != 0 && argc != 1) || (argc && !argv))
        return XConsoleResult_InvalidArgument;
    name = argc == 0 ? session->userName : argv[0];
    if (!name || !name[0] || !xlogin_name_valid(name))
        return XConsoleResult_PermissionDenied;
    if (!xlogin_load(shell, records, &count, &exists) || !exists) return XConsoleResult_Failed;
    target = xlogin_find(records, count, name);
    if (target && count == 1u && target->uid == 0u &&
        !target->passwordSet &&
        (target->permissions & XConsoleShellPermission_Administrator))
        bootstrap = true;
    if (!target || (!session->authenticated && !bootstrap) ||
        (session->authenticated && strcmp(session->userName, name) != 0 &&
         !xlogin_is_admin(session)))
        return XConsoleResult_PermissionDenied;
    /* 非管理员改自己的密码先验证当前密码，与 Linux passwd 一致；
       管理员与其他用户、bootstrap 初始化不要求旧密码。 */
    if (session->authenticated && strcmp(session->userName, name) == 0 &&
        !xlogin_is_admin(session))
        return xlogin_begin_input(shell, session,
                                  XConsoleShellLoginInput_PasswdOldPassword,
                                  target->name, "当前密码: ") ?
            XConsoleResult_MoreOutput : XConsoleResult_IoError;
    return xlogin_begin_input(shell, session,
                              XConsoleShellLoginInput_PasswdNewPassword,
                              target->name, "新密码: ") ?
        XConsoleResult_MoreOutput : XConsoleResult_IoError;
}

static bool xlogin_copy_input_password(const char* line, size_t length,
                                       char* output, size_t outputSize)
{
    if (!line || !output || outputSize == 0 || length == 0 ||
        length >= outputSize) return false;
    memcpy(output, line, length);
    output[length] = '\0';
    return xlogin_password_valid(output);
}

bool XConsoleShellLogin_isInputPending(const XConsoleShellSession* session)
{
    return session && session->loginInputMode != XConsoleShellLoginInput_None;
}

void XConsoleShellLogin_cancelInput(XConsoleShell* shell,
                                    XConsoleShellSession* session)
{
    xlogin_set_echo(shell, true);
    xlogin_clear_input(session);
}

XConsoleResult XConsoleShellLogin_submitInput(XConsoleShell* shell,
                                              XConsoleShellSession* session,
                                              const char* line, size_t length)
{
    char password[XCONSOLE_SHELL_LOGIN_PASSWORD_SIZE + 1u];
    char userName[XCONSOLE_SHELL_LOGIN_NAME_SIZE];
    XConsoleShellLoginInputMode mode;
    XConsoleResult result;
    XConsoleShellLoginRecord records[XCONSOLE_SHELL_LOGIN_USER_CAPACITY];
    XConsoleShellLoginRecord* record;
    size_t count;
    bool exists;
    uint8_t digest[XLOGIN_DIGEST_SIZE];
    if (!shell || !session || !XConsoleShellLogin_isInputPending(session)) {
        xlogin_set_echo(shell, true);
        xlogin_clear_input(session);
        xlogin_secure_zero(password, sizeof(password));
        return XConsoleResult_InvalidArgument;
    }
    mode = session->loginInputMode;
    if (mode == XConsoleShellLoginInput_LoginUser) {
        if (!line || length == 0 || length >= sizeof(userName)) {
            xlogin_set_echo(shell, true);
            xlogin_clear_input(session);
            return XConsoleResult_InvalidArgument;
        }
        memcpy(userName, line, length);
        userName[length] = '\0';
        xlogin_clear_input(session);
        result = (XConsoleResult)xlogin_begin_login_password(shell, session, userName);
        xlogin_secure_zero(userName, sizeof(userName));
        return result;
    }
    /* 密码输入期间终端回显已关闭，用户按 Enter 不会显示换行；提交后先补一个
       换行，避免下一个密码提示、结果或 Shell 提示紧跟在上一个提示后面。 */
    if (!xlogin_emit(shell, "\n")) return XConsoleResult_IoError;
    if (!xlogin_copy_input_password(line, length, password, sizeof(password))) {
        xlogin_set_echo(shell, true);
        xlogin_clear_input(session);
        xlogin_secure_zero(password, sizeof(password));
        return XConsoleResult_InvalidArgument;
    }
    if (mode == XConsoleShellLoginInput_LoginPassword) {
        if (!xlogin_load(shell, records, &count, &exists) || !exists) {
            xlogin_set_echo(shell, true);
            xlogin_clear_input(session);
            xlogin_secure_zero(password, sizeof(password));
            return XConsoleResult_Failed;
        }
        record = xlogin_find(records, count, session->loginInputUser);
        if (!record || record->locked || !record->passwordSet ||
            !xlogin_password_digest(password, record->salt, record->iterations, digest) ||
            !xlogin_const_equal(digest, record->digest, sizeof(digest))) {
            xlogin_secure_zero(digest, sizeof(digest));
            xlogin_set_echo(shell, true);
            xlogin_clear_input(session);
            xlogin_secure_zero(password, sizeof(password));
            return xlogin_emit(shell, "login: 用户名或密码错误\n") ?
                XConsoleResult_PermissionDenied : XConsoleResult_IoError;
        }
        xlogin_secure_zero(digest, sizeof(digest));
        xlogin_apply_session(shell, session, record);
        xlogin_secure_zero(password, sizeof(password));
        if (!XConsoleShell_writeUtf8(shell, "login: ") ||
            !XConsoleShell_writeUtf8(shell, record->name) ||
            !XConsoleShell_writeUtf8(shell, " 成功\n"))
            return XConsoleResult_IoError;
        return XConsoleResult_Ok;
    }
    if (mode == XConsoleShellLoginInput_PasswdOldPassword) {
        if (!xlogin_load(shell, records, &count, &exists) || !exists) {
            xlogin_set_echo(shell, true);
            xlogin_clear_input(session);
            xlogin_secure_zero(password, sizeof(password));
            return XConsoleResult_Failed;
        }
        record = xlogin_find(records, count, session->loginInputUser);
        if (!record || !record->passwordSet || record->locked ||
            !xlogin_password_digest(password, record->salt, record->iterations, digest) ||
            !xlogin_const_equal(digest, record->digest, sizeof(digest))) {
            xlogin_secure_zero(digest, sizeof(digest));
            xlogin_set_echo(shell, true);
            xlogin_clear_input(session);
            xlogin_secure_zero(password, sizeof(password));
            return xlogin_emit(shell, "passwd: 当前密码错误\n") ?
                XConsoleResult_PermissionDenied : XConsoleResult_IoError;
        }
        xlogin_secure_zero(digest, sizeof(digest));
        session->loginInputMode = XConsoleShellLoginInput_PasswdNewPassword;
        session->suppressPrompt = true;
        xlogin_secure_zero(password, sizeof(password));
        return xlogin_emit(shell, "新密码: ") ?
            XConsoleResult_MoreOutput : XConsoleResult_IoError;
    }
    if (mode == XConsoleShellLoginInput_PasswdNewPassword) {
        strcpy(session->loginInputPassword, password);
        session->loginInputMode = XConsoleShellLoginInput_PasswdConfirm;
        session->suppressPrompt = true;
        xlogin_secure_zero(password, sizeof(password));
        return xlogin_emit(shell, "重新输入新密码: ") ?
            XConsoleResult_MoreOutput : XConsoleResult_IoError;
    }
    if (mode != XConsoleShellLoginInput_PasswdConfirm ||
        strcmp(session->loginInputPassword, password) != 0) {
        xlogin_set_echo(shell, true);
        xlogin_clear_input(session);
        xlogin_secure_zero(password, sizeof(password));
        return xlogin_emit(shell, "passwd: 密码不匹配\n") ?
            XConsoleResult_PermissionDenied : XConsoleResult_IoError;
    }
    if (!xlogin_load(shell, records, &count, &exists) || !exists) {
        xlogin_set_echo(shell, true);
        xlogin_clear_input(session);
        xlogin_secure_zero(password, sizeof(password));
        return XConsoleResult_Failed;
    }
    record = xlogin_find(records, count, session->loginInputUser);
    if (!record || (!session->authenticated &&
        !(count == 1u && record->uid == 0u && !record->passwordSet &&
          (record->permissions & XConsoleShellPermission_Administrator))) ||
        (session->authenticated && strcmp(session->userName, record->name) != 0 &&
         !xlogin_is_admin(session)) ||
        !xlogin_set_password(record, password) || !xlogin_save(shell, records, count)) {
        xlogin_set_echo(shell, true);
        xlogin_clear_input(session);
        xlogin_secure_zero(password, sizeof(password));
        return XConsoleResult_PermissionDenied;
    }
    xlogin_set_echo(shell, true);
    xlogin_clear_input(session);
    xlogin_secure_zero(password, sizeof(password));
    return xlogin_emit(shell, "passwd: 密码已更新\n") ?
        XConsoleResult_Ok : XConsoleResult_IoError;
}

const XConsoleCommand XConsoleShellLogin_command = {
    "login", NULL, "登录本地 JSON 用户", "login [user]", 0, 1,
    XConsoleCommandFlag_Sensitive | XConsoleCommandFlag_AllowUnauthenticated,
    xlogin_login, NULL, 0, NULL
};
const XConsoleCommand XConsoleShellLogout_command = {
    "logout", NULL, "注销当前用户", "logout", 0, 0,
    XConsoleCommandFlag_AllowUnauthenticated, xlogin_logout, NULL, 0, NULL
};
const XConsoleCommand XConsoleShellWhoami_command = {
    "whoami", NULL, "显示当前用户", "whoami", 0, 0,
    XConsoleCommandFlag_None, xlogin_whoami, NULL, 0, NULL
};
const XConsoleCommand XConsoleShellId_command = {
    "id", NULL, "显示 UID、GID 和附加组", "id [user]", 0, 1,
    XConsoleCommandFlag_None, xlogin_id, NULL, 0, NULL
};
const XConsoleCommand XConsoleShellGroups_command = {
    "groups", NULL, "显示用户组", "groups [user]", 0, 1,
    XConsoleCommandFlag_None, xlogin_groups, NULL, 0, NULL
};
const XConsoleCommand XConsoleShellUsers_command = {
    "users", NULL, "列出当前登录会话用户", "users", 0, 0,
    XConsoleCommandFlag_None, xlogin_users, NULL, 0, NULL
};
const XConsoleCommand XConsoleShellUserList_command = {
    "userlist", NULL, "列出本地 JSON 用户", "userlist", 0, 0,
    XConsoleCommandFlag_None, xlogin_userlist, NULL, 0, NULL
};
const XConsoleCommand XConsoleShellUserAdd_command = {
    "useradd", NULL, "添加用户；首次执行创建管理员", "useradd [-r] [-u UID] [-g GID] [-G GID,...] [--permissions MASK] [--admin] LOGIN", 1, -1,
    XConsoleCommandFlag_AllowUnauthenticated,
    xlogin_useradd, NULL, 0, NULL
};
const XConsoleCommand XConsoleShellUserDel_command = {
    "userdel", NULL, "删除用户", "userdel [-r] LOGIN", 1, 2,
    XConsoleCommandFlag_None, xlogin_userdel, NULL, 0, NULL
};
const XConsoleCommand XConsoleShellUserMod_command = {
    "usermod", NULL, "修改 UID、GID、组和权限", "usermod [-u UID] [-g GID] [-G GID,...] [-aG GID,...] [-L|-U] [-l LOGIN] [--permissions MASK] [--admin] LOGIN", 1, -1,
    XConsoleCommandFlag_None, xlogin_usermod, NULL, 0, NULL
};
const XConsoleCommand XConsoleShellPasswd_command = {
    "passwd", "password", "修改当前或指定用户密码", "passwd [user]", 0, 1,
    XConsoleCommandFlag_Sensitive | XConsoleCommandFlag_AllowUnauthenticated,
    xlogin_passwd, NULL, 0, NULL
};

const char* XConsoleShellLogin_userName(const XConsoleShell* shell)
{
    if (!shell || !shell->m_session.authenticated || !shell->m_session.userName[0])
        return NULL;
    return shell->m_session.userName;
}

const char* XConsoleShellLogin_databasePath(const XConsoleShell* shell)
{
    return shell ? xlogin_path(shell) : NULL;
}

bool XConsoleShellLogin_setDatabasePath(XConsoleShell* shell, const char* path)
{
    if (!shell || !path || !path[0] || strlen(path) >= sizeof(shell->m_loginDatabasePath))
        return false;
    strcpy(shell->m_loginDatabasePath, path);
    return true;
}

bool XConsoleShellLogin_authenticateSession(XConsoleShell* shell,
                                              XConsoleShellSession* session,
                                              const char* user, const char* password)
{
    XConsoleShellLoginRecord records[XCONSOLE_SHELL_LOGIN_USER_CAPACITY];
    XConsoleShellLoginRecord* record;
    size_t count = 0;
    bool exists = false;
    uint8_t digest[XLOGIN_DIGEST_SIZE];
    bool ok = false;
    if (!shell || !session || !xlogin_name_valid(user) ||
        !xlogin_password_valid(password)) return false;
    if (!xlogin_load(shell, records, &count, &exists) || !exists) return false;
    record = xlogin_find(records, count, user);
    if (!record || record->locked || !record->passwordSet) return false;
    if (!xlogin_password_digest(password, record->salt, record->iterations, digest))
        return false;
    ok = xlogin_const_equal(digest, record->digest, XLOGIN_DIGEST_SIZE);
    xlogin_secure_zero(digest, sizeof(digest));
    if (!ok) return false;
    xlogin_apply_session(shell, session, record);
    return true;
}

bool XConsoleShellLogin_registerCommands(XConsoleShell* shell)
{
    static const XConsoleCommand* commands[] = {
        &XConsoleShellLogin_command, &XConsoleShellLogout_command,
        &XConsoleShellWhoami_command, &XConsoleShellId_command,
        &XConsoleShellGroups_command, &XConsoleShellUsers_command,
        &XConsoleShellUserList_command,
        &XConsoleShellUserAdd_command, &XConsoleShellUserDel_command,
        &XConsoleShellUserMod_command, &XConsoleShellPasswd_command
    };
    size_t i;
    if (!shell) return false;
    for (i = 0; i < sizeof(commands) / sizeof(commands[0]); ++i)
        if (!XConsoleShell_registerStaticCommands(shell, commands[i], 1)) return false;
    return true;
}

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_LOGIN_ON */
