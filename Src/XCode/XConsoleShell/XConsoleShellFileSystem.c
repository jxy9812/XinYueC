/**
 * @file XConsoleShellFileSystem.c
 * @brief XConsoleShell 内建 fs 命令。
 * @details
 * 所有文件、目录、重命名、复制和链接操作均通过 XFileSystem 公共 API；本文件
 * 不包含 open/read/write/stat/opendir 等平台接口。输出使用 Shell I/O 分块，
 * 目录迭代器、文件描述符和临时 XString 在所有返回路径成对释放。
 */

#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_FILESYSTEM_ON

#include "XFileSystem.h"
#include "XString.h"
#include "XMemory.h"
#include "XDateTime.h"
#include <stdint.h>
#include <string.h>

static bool xfs_write(XConsoleShell* shell, const char* text)
{
    return XConsoleShell_writeUtf8(shell, text ? text : "");
}

/* 拼接逻辑路径组件，统一使用单个 '/'，避免 C://name 之类的路径。 */
static bool xfs_append_path_component(XString* base, const char* component)
{
    const char* text = component ? component : "";
    const char* current;
    size_t length;
    bool hasSeparator;

    if (!base) return false;
    current = XString_toUtf8(base);
    length = current ? strlen(current) : 0;
    if (length == 0) return XString_append_utf8(base, text);

    hasSeparator = current[length - 1] == '/' || current[length - 1] == '\\';
    while (*text == '/' || *text == '\\') ++text;
    if (!hasSeparator && *text && !XString_append_utf8(base, "/")) return false;
    return XString_append_utf8(base, text);
}

static bool xfs_make_path(const XConsoleShellSession* session, const char* input,
                          XString* output)
{
    XString* raw;
    const char* value = input && input[0] ? input : ".";
    bool absolute;
    if (!session || !output) return false;
    raw = XString_create_utf8(value);
    if (!raw) return false;
    absolute = value[0] == '/' || (value[0] && value[1] == ':');
    if (!absolute) {
        XString* prefix = XString_create_utf8(session->currentPath[0] ?
                                               session->currentPath : "/");
        if (!prefix) {
            XString_delete_base(raw);
            return false;
        }
        if (strcmp(value, ".") != 0 &&
            !xfs_append_path_component(prefix, XString_toUtf8(raw))) {
            XString_delete_base(prefix);
            XString_delete_base(raw);
            return false;
        }
        XString_delete_base(raw);
        raw = prefix;
    }
    if (!XFileSystem_resolvePath(raw, output, XPathStyle_Absolute))
        XString_assign(output, raw);
    XString_delete_base(raw);
    return XString_size_base(output) < XCONSOLE_SHELL_MAX_PATH;
}

static void xfs_number(int64_t value, char* buffer, size_t capacity);

static int xfs_pwd(XConsoleShell* shell, XConsoleShellSession* session,
                   int argc, const char* const* argv, void* userData)
{
    (void)argc;
    (void)argv;
    (void)userData;
    return XConsoleShell_writeUtf8(shell, session->currentPath) &&
           XConsoleShell_writeUtf8(shell, "\n") ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xfs_cd(XConsoleShell* shell, XConsoleShellSession* session,
                  int argc, const char* const* argv, void* userData)
{
    XString* path = XString_create();
    XFileStat stat;
    char savedPath[XCONSOLE_SHELL_MAX_PATH];
    bool ok = true;
    (void)shell;
    (void)userData;
    if (argc > 1 || !path) ok = false;
    if (ok) {
        if (argc == 0) {
            ok = XFileSystem_getSpecialPath(XSpecialPath_Home, path);
        } else if (strcmp(argv[0], "-") == 0) {
            if (session->previousPath[0] == '\0') ok = false;
            else ok = XString_assign_utf8(path, session->previousPath);
        } else {
            ok = xfs_make_path(session, argv[0], path);
        }
    }
    if (ok && XString_size_base(path) >= sizeof(session->currentPath)) ok = false;
    if (ok && !XFileSystem_stat(path, &stat)) ok = false;
    if (!ok || !stat.isDir) {
        if (path) XString_delete_base(path);
        return XConsoleResult_InvalidArgument;
    }
    strncpy(savedPath, session->currentPath, sizeof(savedPath) - 1u);
    savedPath[sizeof(savedPath) - 1u] = '\0';
    strncpy(session->currentPath, XString_toUtf8(path), sizeof(session->currentPath) - 1u);
    session->currentPath[sizeof(session->currentPath) - 1u] = '\0';
    strncpy(session->previousPath, savedPath, sizeof(session->previousPath) - 1u);
    session->previousPath[sizeof(session->previousPath) - 1u] = '\0';
    XString_delete_base(path);
    return XConsoleResult_Ok;
}

typedef struct XConsoleShellLsOptions {
    bool all;
    bool almostAll;
    bool longFormat;
    bool classify;
    bool directory;
    bool recursive;
    bool human;
} XConsoleShellLsOptions;

static void xfs_ls_size(int64_t value, bool human, char* output, size_t capacity)
{
    static const char units[] = "KMGTP";
    double number = (double)(value < 0 ? 0 : value);
    size_t unit = 0;
    if (!output || capacity == 0) return;
    if (human) {
        while (number >= 1024.0 && unit + 1u < sizeof(units)) {
            number /= 1024.0;
            ++unit;
        }
        if (unit) snprintf(output, capacity, "%.1f%c", number, units[unit - 1u]);
        else snprintf(output, capacity, "%lld", (long long)number);
    } else {
        snprintf(output, capacity, "%lld", (long long)value);
    }
}

static void xfs_ls_permissions(XFilePermissions permissions, bool directory,
                               char* output, size_t capacity)
{
    const XFilePermissions bits[] = {
        XFile_ReadOwner, XFile_WriteOwner, XFile_ExeOwner,
        XFile_ReadGroup, XFile_WriteGroup, XFile_ExeGroup,
        XFile_ReadOther, XFile_WriteOther, XFile_ExeOther
    };
    const char marks[] = "rwxrwxrwx";
    size_t i;
    if (!output || capacity < 11u) return;
    output[0] = directory ? 'd' : '-';
    for (i = 0; i < 9u; ++i) output[i + 1u] = (permissions & bits[i]) ? marks[i] : '-';
    output[10] = '\0';
}
static bool xfs_ls_time(int64_t secs, char* output, size_t capacity)
{
    XDateTime datetime;
    int written;

    if (!output || capacity == 0u) return false;
    output[0] = '\0';
    if (secs <= 0) return false;

    memset(&datetime, 0, sizeof(datetime));
    if (!XDateTime_setSecsSinceEpoch(&datetime, secs)) return false;
    written = snprintf(output, capacity, "%04d-%02d-%02d %02d:%02d:%02d",
                       XDate_year(&datetime.m_date),
                       XDate_month(&datetime.m_date),
                       XDate_day(&datetime.m_date),
                       XTime_hour(&datetime.m_time),
                       XTime_minute(&datetime.m_time),
                       XTime_second(&datetime.m_time));
    return written > 0 && (size_t)written < capacity;
}

static bool xfs_ls_emit(XConsoleShell* shell, const XString* fullPath,
                        const char* displayName, const XDirEntry* entry,
                        const XConsoleShellLsOptions* options)
{
    XFileStat stat;
    char permissions[16];
    char size[32];
    char timeText[32];
    char* line;
    size_t lineCapacity;
    const char* suffix = "";
    int written;
    if (!shell || !fullPath || !displayName || !entry || !options) return false;
    /* 文件名长度受后端长文件名配置控制，不能用固定的小栈缓冲区截断。 */
    lineCapacity = strlen(displayName) + 128u;
    line = (char*)XMalloc_System(lineCapacity);
    if (!line) return false;
    memset(&stat, 0, sizeof(stat));
    if (!XFileSystem_stat(fullPath, &stat)) {
        XFree_System(line);
        return false;
    }
    if (options->longFormat) {
        if (!xfs_ls_time(stat.modificationTime, timeText, sizeof(timeText))) {
            timeText[0] = '-';
            timeText[1] = '\0';
        }
        xfs_ls_permissions(stat.permissions, stat.isDir, permissions, sizeof(permissions));
        xfs_ls_size(stat.size, options->human, size, sizeof(size));
        /* 固定列宽便于串口终端逐行比较；大小右对齐，时间显示为可读日期时刻。 */
        written = snprintf(line, lineCapacity, "%-10s %10s %-9s %s %s",
                           permissions, size, stat.isSymLink ? "链接" :
                           (stat.isDir ? "目录" : "文件"),
                           timeText, displayName);
    } else {
        if (options->classify) {
            if (stat.isDir) suffix = "/";
            else if (stat.isSymLink) suffix = "@";
            else if (stat.isExecutable) suffix = "*";
        }
        written = snprintf(line, lineCapacity, "%s%s", displayName, suffix);
    }
    {
        bool result = written > 0 && (size_t)written < lineCapacity &&
                      XConsoleShell_write(shell, line, (size_t)written) &&
                      XConsoleShell_writeUtf8(shell, "\n");
        XFree_System(line);
        return result;
    }
}

static int xfs_ls_directory(XConsoleShell* shell, const XString* path,
                            const XConsoleShellLsOptions* options, size_t depth)
{
    XString* name = XString_create();
    XString* child = XString_create();
    XDirEntry entry;
    XDirIterator iterator;
    int result = XConsoleResult_Ok;
    if (!name || !child) result = XConsoleResult_Failed;
    if (result == XConsoleResult_Ok) iterator = XFileSystem_opendir(path);
    else iterator = NULL;
    if (!iterator) result = XConsoleResult_Failed;
    if (result == XConsoleResult_Ok) {
        memset(&entry, 0, sizeof(entry));
        entry.name = name;
        while (XFileSystem_readdir(iterator, &entry)) {
            const char* text = XString_toUtf8(name);
            if (entry.isHidden && !options->all &&
                (!options->almostAll || strcmp(text, ".") == 0 ||
                 strcmp(text, "..") == 0)) continue;
            if (!XString_assign_utf8(child, XString_toUtf8(path)) ||
                !xfs_append_path_component(child, text) ||
                !xfs_ls_emit(shell, child, text, &entry, options)) {
                result = XConsoleResult_IoError;
                break;
            }
            if (options->recursive && entry.isDir && depth < 32u &&
                strcmp(text, ".") != 0 && strcmp(text, "..") != 0) {
                result = xfs_ls_directory(shell, child, options, depth + 1u);
                if (result < 0) break;
            }
            if (XConsoleShell_isCancelled(shell)) {
                result = XConsoleResult_Cancelled;
                break;
            }
        }
        XFileSystem_closedir(iterator);
    }
    if (name) XString_delete_base(name);
    if (child) XString_delete_base(child);
    return result;
}

static int xfs_ls(XConsoleShell* shell, XConsoleShellSession* session,
                  int argc, const char* const* argv, void* userData)
{
    XConsoleShellLsOptions options;
    const char* paths[XCONSOLE_SHELL_MAX_ARGUMENTS];
    size_t pathCount = 0;
    size_t i;
    bool endOptions = false;
    (void)userData;
    memset(&options, 0, sizeof(options));
    for (i = 0; i < (size_t)argc; ++i) {
        const char* argument = argv[i];
        size_t j;
        if (!endOptions && strcmp(argument, "--") == 0) {
            endOptions = true;
            continue;
        }
        if (!endOptions && argument[0] == '-' && argument[1]) {
            if (strcmp(argument, "--all") == 0) options.all = true;
            else if (strcmp(argument, "--almost-all") == 0) options.almostAll = true;
            else if (strcmp(argument, "--long") == 0) options.longFormat = true;
            else if (strcmp(argument, "--classify") == 0) options.classify = true;
            else if (strcmp(argument, "--directory") == 0) options.directory = true;
            else if (strcmp(argument, "--recursive") == 0) options.recursive = true;
            else if (strcmp(argument, "--human-readable") == 0) options.human = true;
            else {
                for (j = 1; argument[j]; ++j) {
                    switch (argument[j]) {
                    case 'a': options.all = true; break;
                    case 'A': options.almostAll = true; break;
                    case 'l': options.longFormat = true; break;
                    case 'F': options.classify = true; break;
                    case 'd': options.directory = true; break;
                    case 'R': options.recursive = true; break;
                    case 'h': options.human = true; break;
                    case '1': break;
                    default: return XConsoleResult_InvalidArgument;
                    }
                }
            }
        } else if (pathCount < XCONSOLE_SHELL_MAX_ARGUMENTS) {
            paths[pathCount++] = argument;
        } else return XConsoleResult_ResourceLimit;
    }
    if (pathCount == 0) paths[pathCount++] = ".";
    for (i = 0; i < pathCount; ++i) {
        XString* path = XString_create();
        XFileStat stat;
        XDirEntry entry;
        const char* name;
        if (!path || !xfs_make_path(session, paths[i], path) ||
            !XFileSystem_stat(path, &stat)) {
            if (path) XString_delete_base(path);
            return XConsoleResult_Failed;
        }
        memset(&entry, 0, sizeof(entry));
        name = XString_toUtf8(path);
        if (stat.isDir && !options.directory) {
            int result = xfs_ls_directory(shell, path, &options, 0);
            XString_delete_base(path);
            if (result < 0) return result;
        } else {
            const char* slash = strrchr(name, '/');
            entry.isDir = stat.isDir;
            entry.isFile = stat.isFile;
            entry.isSymLink = stat.isSymLink;
            entry.isHidden = name[0] == '.';
            if (!xfs_ls_emit(shell, path, slash ? slash + 1 : name, &entry, &options)) {
                XString_delete_base(path);
                return XConsoleResult_IoError;
            }
            XString_delete_base(path);
        }
    }
    return XConsoleResult_Ok;
}

static bool xfs_parse_nonnegative(const char* text, int64_t* value)
{
    uint64_t result = 0;
    size_t i;
    if (!text || !text[0] || !value) return false;
    for (i = 0; text[i]; ++i) {
        unsigned digit;
        if (text[i] < '0' || text[i] > '9') return false;
        digit = (unsigned)(text[i] - '0');
        if (result > ((uint64_t)INT64_MAX - digit) / 10u) return false;
        result = result * 10u + digit;
    }
    *value = (int64_t)result;
    return true;
}

static bool xfs_parse_octal(const char* text, XFilePermissions* permissions)
{
    unsigned value = 0;
    size_t i;
    if (!text || !text[0] || !permissions) return false;
    for (i = 0; text[i]; ++i) {
        if (text[i] < '0' || text[i] > '7' || value > 07777u / 8u) return false;
        value = value * 8u + (unsigned)(text[i] - '0');
        if (value > 07777u) return false;
    }
    *permissions = 0;
    if (value & 0400u) *permissions |= XFile_ReadOwner;
    if (value & 0200u) *permissions |= XFile_WriteOwner;
    if (value & 0100u) *permissions |= XFile_ExeOwner;
    if (value & 0040u) *permissions |= XFile_ReadGroup;
    if (value & 0020u) *permissions |= XFile_WriteGroup;
    if (value & 0010u) *permissions |= XFile_ExeGroup;
    if (value & 0004u) *permissions |= XFile_ReadOther;
    if (value & 0002u) *permissions |= XFile_WriteOther;
    if (value & 0001u) *permissions |= XFile_ExeOther;
    return true;
}

static int xfs_touch(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XString* path = NULL;
    XFd fd = XFD_INVALID;
    XDateTime now;
    int64_t timestamp;
    int error = 0;
    bool noCreate = false;
    bool ok;
    (void)shell;
    (void)userData;
    {
        int i;
        int pathCount = 0;
        for (i = 0; i < argc; ++i)
            if (strcmp(argv[i], "--no-create") == 0 || strcmp(argv[i], "-c") == 0) noCreate = true;
        for (i = 0; i < argc; ++i) {
            if (strcmp(argv[i], "--no-create") == 0 || strcmp(argv[i], "-c") == 0) continue;
            ++pathCount;
            path = XString_create();
            if (!path || !xfs_make_path(session, argv[i], path)) {
                if (path) XString_delete_base(path);
                return XConsoleResult_InvalidArgument;
            }
            if (noCreate && !XFileSystem_exists(path)) {
                XString_delete_base(path);
                continue;
            }
            fd = XFileSystem_open(path, XFileSystem_WriteOnly | (noCreate ? 0 : XFileSystem_Create), &error);
            if (fd == XFD_INVALID) {
                XString_delete_base(path);
                return XConsoleResult_Failed;
            }
            now = XDateTime_currentDateTime();
            timestamp = XDateTime_toSecsSinceEpoch(&now);
            ok = XFileSystem_setFileTime(fd, XFile_AccessTime, timestamp) &&
                 XFileSystem_setFileTime(fd, XFile_ModificationTime, timestamp);
            XFileSystem_close(fd);
            XString_delete_base(path);
            if (!ok) return XConsoleResult_Failed;
        }
        if (pathCount == 0) return XConsoleResult_InvalidArgument;
    }
    return XConsoleResult_Ok;
}

static bool xfs_chmod_symbolic(const char* text, XFilePermissions current,
                                bool canExecute, XFilePermissions* result)
{
    XFilePermissions perms = current;
    const char* p = text;
    size_t i;
    if (!text || !text[0] || !result) return false;
    while (*p) {
        unsigned who = 0u;
        char op;
        const char* permStart;
        size_t permLen;
        bool hasWho = false;
        while (*p == 'u' || *p == 'g' || *p == 'o' || *p == 'a') {
            if (*p == 'u') who |= 1u;
            else if (*p == 'g') who |= 2u;
            else if (*p == 'o') who |= 4u;
            else who |= 7u;
            ++p;
            hasWho = true;
        }
        if (!hasWho) who = 7u;
        op = *p;
        if (op != '+' && op != '-' && op != '=') return false;
        ++p;
        permStart = p;
        while (*p == 'r' || *p == 'w' || *p == 'x' || *p == 'X') ++p;
        permLen = (size_t)(p - permStart);
        if (op == '=') {
            if (who & 1u) perms &= ~(XFile_ReadOwner | XFile_WriteOwner | XFile_ExeOwner |
                                     XFile_ReadUser | XFile_WriteUser | XFile_ExeUser);
            if (who & 2u) perms &= ~(XFile_ReadGroup | XFile_WriteGroup | XFile_ExeGroup);
            if (who & 4u) perms &= ~(XFile_ReadOther | XFile_WriteOther | XFile_ExeOther);
        }
        for (i = 0; i < permLen; ++i) {
            char c = permStart[i];
            bool add = op != '-';
            if (c == 'r') {
                if (who & 1u) perms = add ? (perms | XFile_ReadOwner | XFile_ReadUser)
                                          : (perms & ~(XFile_ReadOwner | XFile_ReadUser));
                if (who & 2u) perms = add ? (perms | XFile_ReadGroup) : (perms & ~XFile_ReadGroup);
                if (who & 4u) perms = add ? (perms | XFile_ReadOther) : (perms & ~XFile_ReadOther);
            } else if (c == 'w') {
                if (who & 1u) perms = add ? (perms | XFile_WriteOwner | XFile_WriteUser)
                                          : (perms & ~(XFile_WriteOwner | XFile_WriteUser));
                if (who & 2u) perms = add ? (perms | XFile_WriteGroup) : (perms & ~XFile_WriteGroup);
                if (who & 4u) perms = add ? (perms | XFile_WriteOther) : (perms & ~XFile_WriteOther);
            } else if (c == 'x' || c == 'X') {
                if (c == 'X' && add && !canExecute) continue;
                if (who & 1u) perms = add ? (perms | XFile_ExeOwner | XFile_ExeUser)
                                          : (perms & ~(XFile_ExeOwner | XFile_ExeUser));
                if (who & 2u) perms = add ? (perms | XFile_ExeGroup) : (perms & ~XFile_ExeGroup);
                if (who & 4u) perms = add ? (perms | XFile_ExeOther) : (perms & ~XFile_ExeOther);
            } else {
                return false;
            }
        }
        if (*p == ',') ++p;
        else if (*p) return false;
    }
    *result = perms;
    return true;
}

static int xfs_chmod(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XString* path;
    XFilePermissions permissions;
    bool symbolic;
    bool ok;
    (void)shell;
    (void)userData;
    if (argc < 2) return XConsoleResult_InvalidArgument;
    symbolic = argv[0][0] < '0' || argv[0][0] > '7';
    if (symbolic) {
        if (!xfs_chmod_symbolic(argv[0], 0, false, &permissions))
            return XConsoleResult_InvalidArgument;
    } else if (!xfs_parse_octal(argv[0], &permissions)) {
        return XConsoleResult_InvalidArgument;
    }
    for (int i = 1; i < argc; ++i) {
        XFilePermissions target = permissions;
        XFileStat stat;
        path = XString_create();
        if (!path || !xfs_make_path(session, argv[i], path)) {
            if (path) XString_delete_base(path);
            return XConsoleResult_InvalidArgument;
        }
        if (symbolic) {
            bool hasStat = XFileSystem_stat(path, &stat);
            bool canExecute = hasStat && (stat.isDir ||
                (stat.permissions & (XFile_ExeOwner | XFile_ExeGroup |
                                     XFile_ExeOther)) != 0);
            if (hasStat) target = stat.permissions;
            if (!xfs_chmod_symbolic(argv[0], target, canExecute, &target)) {
                XString_delete_base(path);
                return XConsoleResult_InvalidArgument;
            }
        }
        ok = XFileSystem_setPermissions(path, target);
        XString_delete_base(path);
        if (!ok) return XConsoleResult_Failed;
    }
    return XConsoleResult_Ok;
}

static int xfs_readlink(XConsoleShell* shell, XConsoleShellSession* session,
                        int argc, const char* const* argv, void* userData)
{
    const char* paths[XCONSOLE_SHELL_MAX_ARGUMENTS];
    int pathCount = 0;
    bool canonical = false;
    bool endOptions = false;
    int i;
    bool ok = true;
    (void)userData;
    for (i = 0; i < argc; ++i) {
        const char* argument = argv[i];
        if (!endOptions && strcmp(argument, "--") == 0) {
            endOptions = true;
        } else if (!endOptions && (strcmp(argument, "-f") == 0 ||
                                   strcmp(argument, "--canonicalize") == 0)) {
            canonical = true;
        } else if (!endOptions && argument[0] == '-' && argument[1]) {
            return XConsoleResult_InvalidArgument;
        } else {
            if (pathCount >= XCONSOLE_SHELL_MAX_ARGUMENTS)
                return XConsoleResult_ResourceLimit;
            paths[pathCount++] = argument;
        }
    }
    if (pathCount == 0) return XConsoleResult_InvalidArgument;
    for (i = 0; i < pathCount; ++i) {
        XString* path = XString_create();
        XString* result = XString_create();
        XFileStat stat;
        bool ok2;
        if (!path || !result || !xfs_make_path(session, paths[i], path)) {
            if (path) XString_delete_base(path);
            if (result) XString_delete_base(result);
            return XConsoleResult_InvalidArgument;
        }
        if (canonical) {
            ok2 = XFileSystem_resolvePath(path, result, XPathStyle_Canonical);
        } else {
            ok2 = XFileSystem_stat(path, &stat) && stat.isSymLink &&
                  XFileSystem_readLink(path, result);
        }
        if (ok2) {
            ok2 = XConsoleShell_writeUtf8(shell, XString_toUtf8(result)) &&
                  XConsoleShell_writeUtf8(shell, "\n");
        }
        XString_delete_base(path);
        XString_delete_base(result);
        if (!ok2) return XConsoleResult_Failed;
    }
    return XConsoleResult_Ok;
}

static int xfs_realpath(XConsoleShell* shell, XConsoleShellSession* session,
                        int argc, const char* const* argv, void* userData)
{
    XString* path = XString_create();
    XString* result = XString_create();
    bool ok;
    (void)userData;
    if (argc != 1 || !path || !result || !xfs_make_path(session, argv[0], path)) {
        if (path) XString_delete_base(path);
        if (result) XString_delete_base(result);
        return XConsoleResult_InvalidArgument;
    }
    ok = XFileSystem_resolvePath(path, result, XPathStyle_Canonical) &&
         XConsoleShell_writeUtf8(shell, XString_toUtf8(result)) &&
         XConsoleShell_writeUtf8(shell, "\n");
    XString_delete_base(path);
    XString_delete_base(result);
    return ok ? XConsoleResult_Ok : XConsoleResult_Failed;
}

static int xfs_truncate(XConsoleShell* shell, XConsoleShellSession* session,
                        int argc, const char* const* argv, void* userData)
{
    const char* paths[XCONSOLE_SHELL_MAX_ARGUMENTS];
    int pathCount = 0;
    int64_t size = -1;
    bool noCreate = false;
    bool endOptions = false;
    int i;
    (void)shell;
    (void)userData;
    for (i = 0; i < argc; ++i) {
        const char* argument = argv[i];
        int64_t value;
        if (!endOptions && strcmp(argument, "--") == 0) {
            endOptions = true;
        } else if (!endOptions && (strcmp(argument, "-s") == 0 ||
                                   strcmp(argument, "--size") == 0)) {
            if (i + 1 >= argc || size >= 0 ||
                !xfs_parse_nonnegative(argv[++i], &value))
                return XConsoleResult_InvalidArgument;
            size = value;
        } else if (!endOptions && (strcmp(argument, "-c") == 0 ||
                                   strcmp(argument, "--no-create") == 0)) {
            noCreate = true;
        } else if (!endOptions && argument[0] == '-' && argument[1]) {
            return XConsoleResult_InvalidArgument;
        } else {
            if (pathCount >= XCONSOLE_SHELL_MAX_ARGUMENTS)
                return XConsoleResult_ResourceLimit;
            paths[pathCount++] = argument;
        }
    }
    if (size < 0 || pathCount == 0) return XConsoleResult_InvalidArgument;
    for (i = 0; i < pathCount; ++i) {
        XString* path = XString_create();
        XFd fd;
        int error = 0;
        bool ok;
        if (!path || !xfs_make_path(session, paths[i], path)) {
            if (path) XString_delete_base(path);
            return XConsoleResult_InvalidArgument;
        }
        if (noCreate && !XFileSystem_exists(path)) {
            XString_delete_base(path);
            continue;
        }
        fd = XFileSystem_open(path, XFileSystem_WriteOnly | XFileSystem_Truncate |
                              (noCreate ? 0 : XFileSystem_Create), &error);
        XString_delete_base(path);
        if (fd == XFD_INVALID) return XConsoleResult_Failed;
        ok = XFileSystem_resize(fd, size);
        XFileSystem_close(fd);
        if (!ok) return XConsoleResult_Failed;
    }
    return XConsoleResult_Ok;
}

static int xfs_df(XConsoleShell* shell, XConsoleShellSession* session,
                  int argc, const char* const* argv, void* userData)
{
    XString* path = XString_create();
    XStorageInfoData info;
    char line[192];
    const char* input = argc ? argv[0] : ".";
    int written;
    (void)userData;
    if (argc > 1 || !path || !xfs_make_path(session, input, path)) {
        if (path) XString_delete_base(path);
        return XConsoleResult_InvalidArgument;
    }
    memset(&info, 0, sizeof(info));
    if (!XFileSystem_getStorageInfo(path, &info)) {
        XString_delete_base(path);
        return XConsoleResult_Failed;
    }
    written = snprintf(line, sizeof(line), "总计=%-16lld 空闲=%-16lld 可用=%-16lld 块=%-8d\n",
                       (long long)info.bytesTotal, (long long)info.bytesFree,
                       (long long)info.bytesAvailable, info.blockSize);
    XString_delete_base(path);
    return written > 0 && (size_t)written < sizeof(line) &&
           XConsoleShell_write(shell, line, (size_t)written)
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static bool xfs_du_size(const XString* path, int64_t* total)
{
    XFileStat stat;
    XString* name = NULL;
    XString* child = NULL;
    XDirEntry entry;
    XDirIterator iterator = NULL;
    bool ok = true;
    if (!path || !total || !XFileSystem_stat(path, &stat)) return false;
    *total += stat.size > 0 ? stat.size : 0;
    if (!stat.isDir) return true;
    name = XString_create();
    child = XString_create();
    if (!name || !child) ok = false;
    if (ok) iterator = XFileSystem_opendir(path);
    if (ok && !iterator) ok = false;
    if (ok) {
        memset(&entry, 0, sizeof(entry));
        entry.name = name;
        while (XFileSystem_readdir(iterator, &entry)) {
            if (entry.isSymLink) continue;
            if (strcmp(XString_toUtf8(name), ".") == 0 ||
                strcmp(XString_toUtf8(name), "..") == 0) continue;
            if (!XString_assign_utf8(child, XString_toUtf8(path)) ||
                !xfs_append_path_component(child, XString_toUtf8(name)) ||
                !xfs_du_size(child, total)) {
                ok = false;
                break;
            }
        }
        XFileSystem_closedir(iterator);
    }
    if (name) XString_delete_base(name);
    if (child) XString_delete_base(child);
    return ok;
}

static int xfs_du(XConsoleShell* shell, XConsoleShellSession* session,
                  int argc, const char* const* argv, void* userData)
{
    XString* path = XString_create();
    int64_t total = 0;
    char number[32];
    char line[384];
    int written;
    (void)userData;
    if (argc > 1 || !path || !xfs_make_path(session, argc ? argv[0] : ".", path)) {
        if (path) XString_delete_base(path);
        return XConsoleResult_InvalidArgument;
    }
    if (!xfs_du_size(path, &total)) {
        XString_delete_base(path);
        return XConsoleResult_Failed;
    }
    xfs_number(total, number, sizeof(number));
    written = snprintf(line, sizeof(line), "%12s  %s\n", number, XString_toUtf8(path));
    XString_delete_base(path);
    return written > 0 && (size_t)written < sizeof(line) &&
           XConsoleShell_write(shell, line, (size_t)written)
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xfs_wc(XConsoleShell* shell, XConsoleShellSession* session,
                 int argc, const char* const* argv, void* userData)
{
    const char* paths[XCONSOLE_SHELL_MAX_ARGUMENTS];
    int pathCount = 0;
    bool showLines = true;
    bool showWords = true;
    bool showBytes = true;
    bool endOptions = false;
    int64_t totalLines = 0;
    int64_t totalWords = 0;
    int64_t totalBytes = 0;
    int i;
    (void)userData;
    for (i = 0; i < argc; ++i) {
        const char* argument = argv[i];
        if (!endOptions && strcmp(argument, "--") == 0) {
            endOptions = true;
        } else if (!endOptions && (strcmp(argument, "-l") == 0 ||
                                   strcmp(argument, "--lines") == 0)) {
            showLines = true; showWords = false; showBytes = false;
        } else if (!endOptions && (strcmp(argument, "-w") == 0 ||
                                   strcmp(argument, "--words") == 0)) {
            showLines = false; showWords = true; showBytes = false;
        } else if (!endOptions && (strcmp(argument, "-c") == 0 ||
                                   strcmp(argument, "--bytes") == 0)) {
            showLines = false; showWords = false; showBytes = true;
        } else if (!endOptions && (strcmp(argument, "-m") == 0 ||
                                   strcmp(argument, "--chars") == 0)) {
            showLines = false; showWords = false; showBytes = true;
        } else if (!endOptions && argument[0] == '-' && argument[1]) {
            return XConsoleResult_InvalidArgument;
        } else {
            if (pathCount >= XCONSOLE_SHELL_MAX_ARGUMENTS)
                return XConsoleResult_ResourceLimit;
            paths[pathCount++] = argument;
        }
    }
    if (pathCount == 0) return XConsoleResult_InvalidArgument;
    for (i = 0; i < pathCount; ++i) {
        XString* path = XString_create();
        XFd fd = XFD_INVALID;
        char buffer[XCONSOLE_SHELL_OUTPUT_CHUNK_SIZE];
        int64_t bytes = 0;
        int64_t lines = 0;
        int64_t words = 0;
        bool inWord = false;
        int error = 0;
        int64_t count = 0;
        char line[256];
        int written;
        int column;
        size_t j;
        if (!path || !xfs_make_path(session, paths[i], path)) {
            if (path) XString_delete_base(path);
            return XConsoleResult_InvalidArgument;
        }
        fd = XFileSystem_open(path, XFileSystem_ReadOnly, &error);
        XString_delete_base(path);
        if (fd == XFD_INVALID) return XConsoleResult_Failed;
        while ((count = XFileSystem_read(fd, buffer, sizeof(buffer))) > 0) {
            bytes += count;
            for (j = 0; j < (size_t)count; ++j) {
                if (buffer[j] == '\n') ++lines;
                if (buffer[j] == ' ' || buffer[j] == '\t' || buffer[j] == '\r' ||
                    buffer[j] == '\n') {
                    inWord = false;
                } else if (!inWord) {
                    inWord = true;
                    ++words;
                }
            }
        }
        XFileSystem_close(fd);
        if (count < 0) return XConsoleResult_IoError;
        written = 0;
        column = 0;
        if (showLines) {
            int n = snprintf(line + written, sizeof(line) - (size_t)written,
                             "%7lld ", (long long)lines);
            if (n < 0 || (size_t)n >= sizeof(line) - (size_t)written)
                return XConsoleResult_IoError;
            written += n; ++column;
        }
        if (showWords) {
            int n = snprintf(line + written, sizeof(line) - (size_t)written,
                             "%7lld ", (long long)words);
            if (n < 0 || (size_t)n >= sizeof(line) - (size_t)written)
                return XConsoleResult_IoError;
            written += n; ++column;
        }
        if (showBytes) {
            int n = snprintf(line + written, sizeof(line) - (size_t)written,
                             "%7lld ", (long long)bytes);
            if (n < 0 || (size_t)n >= sizeof(line) - (size_t)written)
                return XConsoleResult_IoError;
            written += n; ++column;
        }
        if (column == 0) {
            int n = snprintf(line + written, sizeof(line) - (size_t)written,
                             "%7lld ", (long long)lines);
            if (n < 0 || (size_t)n >= sizeof(line) - (size_t)written)
                return XConsoleResult_IoError;
            written += n;
        }
        {
            int n = snprintf(line + written, sizeof(line) - (size_t)written,
                             "%s\n", paths[i]);
            if (n < 0 || (size_t)n >= sizeof(line) - (size_t)written)
                return XConsoleResult_IoError;
            written += n;
        }
        if (!XConsoleShell_write(shell, line, (size_t)written))
            return XConsoleResult_IoError;
        totalLines += lines;
        totalWords += words;
        totalBytes += bytes;
    }
    if (pathCount > 1) {
        char line[256];
        int written = 0;
        int column = 0;
        if (showLines) {
            int n = snprintf(line + written, sizeof(line) - (size_t)written,
                             "%7lld ", (long long)totalLines);
            if (n < 0 || (size_t)n >= sizeof(line) - (size_t)written)
                return XConsoleResult_IoError;
            written += n; ++column;
        }
        if (showWords) {
            int n = snprintf(line + written, sizeof(line) - (size_t)written,
                             "%7lld ", (long long)totalWords);
            if (n < 0 || (size_t)n >= sizeof(line) - (size_t)written)
                return XConsoleResult_IoError;
            written += n; ++column;
        }
        if (showBytes) {
            int n = snprintf(line + written, sizeof(line) - (size_t)written,
                             "%7lld ", (long long)totalBytes);
            if (n < 0 || (size_t)n >= sizeof(line) - (size_t)written)
                return XConsoleResult_IoError;
            written += n; ++column;
        }
        if (column == 0) {
            int n = snprintf(line + written, sizeof(line) - (size_t)written,
                             "%7lld ", (long long)totalLines);
            if (n < 0 || (size_t)n >= sizeof(line) - (size_t)written)
                return XConsoleResult_IoError;
            written += n;
        }
        if (snprintf(line + written, sizeof(line) - (size_t)written, "总计\n") < 0)
            return XConsoleResult_IoError;
        if (!XConsoleShell_write(shell, line, (size_t)written))
            return XConsoleResult_IoError;
    }
    return XConsoleResult_Ok;
}


static int xfs_head(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    const char* paths[XCONSOLE_SHELL_MAX_ARGUMENTS];
    int pathCount = 0;
    int64_t lines = 10;
    bool endOptions = false;
    int i;
    (void)userData;
    for (i = 0; i < argc; ++i) {
        const char* argument = argv[i];
        if (!endOptions && strcmp(argument, "--") == 0) {
            endOptions = true;
        } else if (!endOptions && (strcmp(argument, "-n") == 0 ||
                                   strcmp(argument, "--lines") == 0)) {
            if (i + 1 >= argc || !xfs_parse_nonnegative(argv[++i], &lines))
                return XConsoleResult_InvalidArgument;
        } else if (!endOptions && argument[0] == '-' && argument[1]) {
            return XConsoleResult_InvalidArgument;
        } else {
            if (pathCount >= XCONSOLE_SHELL_MAX_ARGUMENTS)
                return XConsoleResult_ResourceLimit;
            paths[pathCount++] = argument;
        }
    }
    if (pathCount == 0) return XConsoleResult_InvalidArgument;
    for (i = 0; i < pathCount; ++i) {
        XString* path = XString_create();
        XFd fd;
        int64_t current = 0;
        char buffer[XCONSOLE_SHELL_OUTPUT_CHUNK_SIZE];
        int64_t count = 0;
        int error = 0;
        if (!path || !xfs_make_path(session, paths[i], path)) {
            if (path) XString_delete_base(path);
            return XConsoleResult_InvalidArgument;
        }
        fd = XFileSystem_open(path, XFileSystem_ReadOnly, &error);
        XString_delete_base(path);
        if (fd == XFD_INVALID) return XConsoleResult_Failed;
        if (pathCount > 1) {
            if (!XConsoleShell_writeUtf8(shell, "==> ") ||
                !XConsoleShell_writeUtf8(shell, paths[i]) ||
                !XConsoleShell_writeUtf8(shell, " <==\n")) {
                XFileSystem_close(fd);
                return XConsoleResult_IoError;
            }
        }
        while (current < lines &&
               (count = XFileSystem_read(fd, buffer, sizeof(buffer))) > 0) {
            size_t j;
            for (j = 0; j < (size_t)count && current < lines; ++j) {
                if (!XConsoleShell_write(shell, buffer + j, 1)) {
                    XFileSystem_close(fd);
                    return XConsoleResult_IoError;
                }
                if (buffer[j] == '\n') ++current;
            }
        }
        XFileSystem_close(fd);
        if (count < 0) return XConsoleResult_IoError;
    }
    return XConsoleResult_Ok;
}


static int xfs_tail(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    const char* paths[XCONSOLE_SHELL_MAX_ARGUMENTS];
    int pathCount = 0;
    int64_t lines = 10;
    bool endOptions = false;
    int i;
    (void)userData;
    for (i = 0; i < argc; ++i) {
        const char* argument = argv[i];
        if (!endOptions && strcmp(argument, "--") == 0) {
            endOptions = true;
        } else if (!endOptions && (strcmp(argument, "-n") == 0 ||
                                   strcmp(argument, "--lines") == 0)) {
            if (i + 1 >= argc || !xfs_parse_nonnegative(argv[++i], &lines))
                return XConsoleResult_InvalidArgument;
        } else if (!endOptions && argument[0] == '-' && argument[1]) {
            return XConsoleResult_InvalidArgument;
        } else {
            if (pathCount >= XCONSOLE_SHELL_MAX_ARGUMENTS)
                return XConsoleResult_ResourceLimit;
            paths[pathCount++] = argument;
        }
    }
    if (pathCount == 0 || lines > 127) return XConsoleResult_InvalidArgument;
    for (i = 0; i < pathCount; ++i) {
        XString* path = XString_create();
        XFd fd;
        int64_t offsets[128];
        size_t offsetCount = 0;
        int64_t position = 0;
        int64_t start = 0;
        char buffer[XCONSOLE_SHELL_OUTPUT_CHUNK_SIZE];
        int64_t count;
        int error = 0;
        size_t j;
        if (!path || !xfs_make_path(session, paths[i], path)) {
            if (path) XString_delete_base(path);
            return XConsoleResult_InvalidArgument;
        }
        fd = XFileSystem_open(path, XFileSystem_ReadOnly, &error);
        XString_delete_base(path);
        if (fd == XFD_INVALID) return XConsoleResult_Failed;
        offsets[offsetCount++] = 0;
        while ((count = XFileSystem_read(fd, buffer, sizeof(buffer))) > 0) {
            for (j = 0; j < (size_t)count; ++j, ++position) {
                if (buffer[j] == '\n') {
                    if (offsetCount < 128) offsets[offsetCount++] = position + 1;
                    else {
                        memmove(offsets, offsets + 1,
                                sizeof(offsets) - sizeof(offsets[0]));
                        offsets[127] = position + 1;
                    }
                }
            }
        }
        if (count < 0) {
            XFileSystem_close(fd);
            return XConsoleResult_IoError;
        }
        if (lines > 0) {
            if (pathCount > 1) {
                if (!XConsoleShell_writeUtf8(shell, "==> ") ||
                    !XConsoleShell_writeUtf8(shell, paths[i]) ||
                    !XConsoleShell_writeUtf8(shell, " <==\n")) {
                    XFileSystem_close(fd);
                    return XConsoleResult_IoError;
                }
            }
            if (lines < (int64_t)offsetCount)
                start = offsets[offsetCount - 1u - (size_t)lines];
            if (!XFileSystem_seek(fd, start)) {
                XFileSystem_close(fd);
                return XConsoleResult_IoError;
            }
            while ((count = XFileSystem_read(fd, buffer, sizeof(buffer))) > 0) {
                if (!XConsoleShell_write(shell, buffer, (size_t)count)) {
                    XFileSystem_close(fd);
                    return XConsoleResult_IoError;
                }
            }
            if (count < 0) {
                XFileSystem_close(fd);
                return XConsoleResult_IoError;
            }
        }
        XFileSystem_close(fd);
    }
    return XConsoleResult_Ok;
}

static int xfs_cmp(XConsoleShell* shell, XConsoleShellSession* session,
                   int argc, const char* const* argv, void* userData)
{
    XString* left = XString_create();
    XString* right = XString_create();
    XFd a = XFD_INVALID;
    XFd b = XFD_INVALID;
    char leftBuffer[XCONSOLE_SHELL_OUTPUT_CHUNK_SIZE];
    char rightBuffer[XCONSOLE_SHELL_OUTPUT_CHUNK_SIZE];
    int error = 0;
    int64_t acount;
    int64_t bcount;
    bool equal = true;
    (void)userData;
    if (argc != 2 || !left || !right || !xfs_make_path(session, argv[0], left) ||
        !xfs_make_path(session, argv[1], right)) {
        if (left) XString_delete_base(left);
        if (right) XString_delete_base(right);
        return XConsoleResult_InvalidArgument;
    }
    a = XFileSystem_open(left, XFileSystem_ReadOnly, &error);
    b = XFileSystem_open(right, XFileSystem_ReadOnly, &error);
    XString_delete_base(left);
    XString_delete_base(right);
    if (a == XFD_INVALID || b == XFD_INVALID) {
        if (a != XFD_INVALID) XFileSystem_close(a);
        if (b != XFD_INVALID) XFileSystem_close(b);
        return XConsoleResult_Failed;
    }
    do {
        acount = XFileSystem_read(a, leftBuffer, sizeof(leftBuffer));
        bcount = XFileSystem_read(b, rightBuffer, sizeof(rightBuffer));
        if (acount != bcount || acount < 0 || (acount > 0 &&
            memcmp(leftBuffer, rightBuffer, (size_t)acount) != 0)) equal = false;
    } while (equal && acount > 0);
    XFileSystem_close(a);
    XFileSystem_close(b);
    if (equal) return XConsoleResult_Ok;
    if (!XConsoleShell_writeUtf8(shell, "不同\n")) return XConsoleResult_IoError;
    return XConsoleResult_Failed;
}

static bool xfs_glob_match_local(const char* pattern, const char* text)
{
    if (!pattern || !text) return false;
    while (*pattern) {
        if (*pattern == '*') {
            ++pattern;
            if (!*pattern) return true;
            while (*text) {
                if (xfs_glob_match_local(pattern, text)) return true;
                ++text;
            }
            return xfs_glob_match_local(pattern, text);
        }
        if (!*text || (*pattern != '?' && *pattern != *text)) return false;
        ++pattern;
        ++text;
    }
    return *text == '\0';
}

static int xfs_find_walk(XConsoleShell* shell, const XString* path,
                         const char* pattern, char type, int depth, int maxDepth,
                         int64_t mtimeDays, int64_t sizeBytes)
{
    XFileStat stat;
    XString* name = XString_create();
    XString* child = XString_create();
    XDirEntry entry;
    XDirIterator iterator = NULL;
    int result = XConsoleResult_Ok;
    const char* base;
    if (!path || !name || !child || !XFileSystem_stat(path, &stat)) result = XConsoleResult_Failed;
    base = path ? XString_toUtf8(path) : NULL;
    if (result == XConsoleResult_Ok && (!pattern || xfs_glob_match_local(pattern, base +
        (strrchr(base, '/') ? (strrchr(base, '/') - base + 1) : 0)) &&
        (type == 0 || (type == 'f' && stat.isFile) || (type == 'd' && stat.isDir)))) {
        bool matched = true;
        if (mtimeDays >= 0) {
            int64_t nowSecs = XDateTime_currentSecsSinceEpoch();
            int64_t start = nowSecs - (mtimeDays + 1) * 86400;
            int64_t end = nowSecs - mtimeDays * 86400;
            if (stat.modificationTime < start || stat.modificationTime >= end)
                matched = false;
        }
        if (matched && sizeBytes >= 0 && stat.size != sizeBytes) matched = false;
        if (matched) {
            if (!XConsoleShell_writeUtf8(shell, base) ||
                !XConsoleShell_writeUtf8(shell, "\n"))
                result = XConsoleResult_IoError;
        }
    }
    if (result == XConsoleResult_Ok && stat.isDir && (maxDepth < 0 || depth < maxDepth)) {
        iterator = XFileSystem_opendir(path);
        if (!iterator) result = XConsoleResult_Failed;
        if (result == XConsoleResult_Ok) {
            memset(&entry, 0, sizeof(entry));
            entry.name = name;
            while (XFileSystem_readdir(iterator, &entry)) {
                const char* item = XString_toUtf8(name);
                if (strcmp(item, ".") == 0 || strcmp(item, "..") == 0) continue;
                if (!XString_assign_utf8(child, base) ||
                    !xfs_append_path_component(child, item)) {
                    result = XConsoleResult_Failed;
                    break;
                }
                result = xfs_find_walk(shell, child, pattern, type, depth + 1,
                                       maxDepth, mtimeDays, sizeBytes);
                if (result < 0 || XConsoleShell_isCancelled(shell)) break;
            }
            XFileSystem_closedir(iterator);
        }
    }
    if (name) XString_delete_base(name);
    if (child) XString_delete_base(child);
    return result;
}

static int xfs_find(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XString* path;
    const char* pattern = NULL;
    char type = 0;
    int maxDepth = -1;
    int64_t mtimeDays = -1;
    int64_t sizeBytes = -1;
    int i;
    (void)userData;
    if (argc < 1) return XConsoleResult_InvalidArgument;
    path = XString_create();
    if (!path || !xfs_make_path(session, argv[0], path)) {
        if (path) XString_delete_base(path);
        return XConsoleResult_InvalidArgument;
    }
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-name") == 0 && i + 1 < argc) pattern = argv[++i];
        else if (strcmp(argv[i], "-type") == 0 && i + 1 < argc &&
                 (argv[i + 1][0] == 'f' || argv[i + 1][0] == 'd') && !argv[i + 1][1])
            type = argv[++i][0];
        else if (strcmp(argv[i], "-maxdepth") == 0 && i + 1 < argc) {
            int64_t value;
            if (!xfs_parse_nonnegative(argv[++i], &value) || value > 64) {
                XString_delete_base(path);
                return XConsoleResult_InvalidArgument;
            }
            maxDepth = (int)value;
        } else if (strcmp(argv[i], "-mtime") == 0 && i + 1 < argc) {
            int64_t value;
            if (!xfs_parse_nonnegative(argv[++i], &value)) {
                XString_delete_base(path);
                return XConsoleResult_InvalidArgument;
            }
            mtimeDays = value;
        } else if (strcmp(argv[i], "-size") == 0 && i + 1 < argc) {
            const char* text = argv[++i];
            size_t len = strlen(text);
            char copy[64];
            if (len == 0) {
                XString_delete_base(path);
                return XConsoleResult_InvalidArgument;
            }
            if (text[len - 1] == 'c' || text[len - 1] == 'C') {
                if (len - 1 >= sizeof(copy)) {
                    XString_delete_base(path);
                    return XConsoleResult_InvalidArgument;
                }
                memcpy(copy, text, len - 1);
                copy[len - 1] = '\0';
                text = copy;
            }
            if (!xfs_parse_nonnegative(text, &sizeBytes)) {
                XString_delete_base(path);
                return XConsoleResult_InvalidArgument;
            }
        } else {
            XString_delete_base(path);
            return XConsoleResult_InvalidArgument;
        }
    }
    i = xfs_find_walk(shell, path, pattern, type, 0, maxDepth,
                      mtimeDays, sizeBytes);
    XString_delete_base(path);
    return i;
}

static int xfs_tree_walk(XConsoleShell* shell, const XString* path, size_t depth)
{
    XString* name = XString_create();
    XString* child = XString_create();
    XDirEntry entry;
    XDirIterator iterator;
    const char* base = path ? XString_toUtf8(path) : NULL;
    int result = XConsoleResult_Ok;
    if (!name || !child || !base) result = XConsoleResult_Failed;
    if (result == XConsoleResult_Ok) {
        iterator = XFileSystem_opendir(path);
        if (!iterator) result = XConsoleResult_Failed;
        if (result == XConsoleResult_Ok) {
            memset(&entry, 0, sizeof(entry));
            entry.name = name;
            while (XFileSystem_readdir(iterator, &entry)) {
                const char* item = XString_toUtf8(name);
                size_t i;
                if (strcmp(item, ".") == 0 || strcmp(item, "..") == 0 || entry.isHidden) continue;
                for (i = 0; i < depth; ++i)
                    if (!XConsoleShell_writeUtf8(shell, "  ")) { result = XConsoleResult_IoError; break; }
                if (result < 0 || !XConsoleShell_writeUtf8(shell, item) ||
                    !XConsoleShell_writeUtf8(shell, entry.isDir ? "/\n" : "\n") ||
                    !XString_assign_utf8(child, base) ||
                    !xfs_append_path_component(child, item)) {
                    result = XConsoleResult_IoError;
                    break;
                }
                if (entry.isDir) {
                    result = xfs_tree_walk(shell, child, depth + 1u);
                    if (result < 0) break;
                }
            }
            XFileSystem_closedir(iterator);
        }
    }
    if (name) XString_delete_base(name);
    if (child) XString_delete_base(child);
    return result;
}

static int xfs_tree(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XString* path = XString_create();
    int result;
    (void)userData;
    if (argc > 1 || !path || !xfs_make_path(session, argc ? argv[0] : ".", path)) {
        if (path) XString_delete_base(path);
        return XConsoleResult_InvalidArgument;
    }
    if (!XConsoleShell_writeUtf8(shell, XString_toUtf8(path)) || !XConsoleShell_writeUtf8(shell, "/\n")) {
        XString_delete_base(path);
        return XConsoleResult_IoError;
    }
    result = xfs_tree_walk(shell, path, 1u);
    XString_delete_base(path);
    return result;
}

static int xfs_file(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XString* path = XString_create();
    XFileStat stat;
    const char* type;
    char line[96];
    int written;
    (void)userData;
    if (argc != 1 || !path || !xfs_make_path(session, argv[0], path) ||
        !XFileSystem_stat(path, &stat)) {
        if (path) XString_delete_base(path);
        return XConsoleResult_Failed;
    }
    type = stat.isDir ? "目录" : (stat.isSymLink ? "符号链接" :
           (stat.isFile ? "常规文件" : "其他"));
    written = snprintf(line, sizeof(line), "%s: %s\n", XString_toUtf8(path), type);
    XString_delete_base(path);
    return written > 0 && (size_t)written < sizeof(line) &&
           XConsoleShell_write(shell, line, (size_t)written)
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xfs_basename(XConsoleShell* shell, XConsoleShellSession* session,
                        int argc, const char* const* argv, void* userData)
{
    const char* text;
    size_t length;
    size_t start = 0;
    size_t i;
    (void)session;
    (void)userData;
    if (!shell || argc != 1) return XConsoleResult_InvalidArgument;
    text = (argv[0] && argv[0][0]) ? argv[0] : ".";
    length = strlen(text);
    while (length > 1u && (text[length - 1u] == '/' || text[length - 1u] == '\\')) --length;
    if (!(length == 1u && (text[0] == '/' || text[0] == '\\'))) {
        for (i = 0; i < length; ++i)
            if (text[i] == '/' || text[i] == '\\') start = i + 1u;
    }
    return XConsoleShell_write(shell, text + start, length - start) &&
           XConsoleShell_writeUtf8(shell, "\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xfs_dirname(XConsoleShell* shell, XConsoleShellSession* session,
                       int argc, const char* const* argv, void* userData)
{
    const char* text;
    size_t length;
    size_t slash = SIZE_MAX;
    size_t i;
    (void)session;
    (void)userData;
    if (!shell || argc != 1) return XConsoleResult_InvalidArgument;
    text = (argv[0] && argv[0][0]) ? argv[0] : ".";
    length = strlen(text);
    while (length > 1u && (text[length - 1u] == '/' || text[length - 1u] == '\\')) --length;
    for (i = 0; i < length; ++i)
        if (text[i] == '/' || text[i] == '\\') slash = i;
    if (slash == SIZE_MAX) {
        return XConsoleShell_writeUtf8(shell, ".\n") ? XConsoleResult_Ok : XConsoleResult_IoError;
    }
    if (slash == 0u) {
        return XConsoleShell_writeUtf8(shell, "/\n") ? XConsoleResult_Ok : XConsoleResult_IoError;
    }
    return XConsoleShell_write(shell, text, slash) &&
           XConsoleShell_writeUtf8(shell, "\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xfs_cat(XConsoleShell* shell, XConsoleShellSession* session,
                   int argc, const char* const* argv, void* userData)
{
    XString* path;
    XFd fd;
    int error = 0;
    char buffer[XCONSOLE_SHELL_OUTPUT_CHUNK_SIZE];
    int64_t count;
    int64_t offset = 0;
    int64_t remaining = INT64_MAX;
    int64_t length = INT64_MAX;
    bool offsetSet = false;
    bool lengthSet = false;
    bool numberAll = false;
    bool numberNonblank = false;
    bool squeezeBlank = false;
    bool showEnds = false;
    bool showTabs = false;
    bool atLineStart = true;
    bool sawBlankLine = false;
    int64_t lineNumber = 0;
    const char* paths[XCONSOLE_SHELL_MAX_ARGUMENTS];
    int pathCount = 0;
    int i;
    (void)userData;
    if (argc < 1) return XConsoleResult_InvalidArgument;
    for (i = 0; i < argc; ++i) {
        int64_t value;
        if (strcmp(argv[i], "--offset") == 0) {
            if (offsetSet || i + 1 >= argc ||
                !xfs_parse_nonnegative(argv[++i], &value))
                return XConsoleResult_InvalidArgument;
            offset = value;
            offsetSet = true;
        } else if (strcmp(argv[i], "--length") == 0) {
            if (lengthSet || i + 1 >= argc ||
                !xfs_parse_nonnegative(argv[++i], &value))
                return XConsoleResult_InvalidArgument;
            remaining = value;
            length = value;
            lengthSet = true;
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--number") == 0) {
            numberAll = true;
        } else if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--number-nonblank") == 0) {
            numberNonblank = true;
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--squeeze-blank") == 0) {
            squeezeBlank = true;
        } else if (strcmp(argv[i], "-E") == 0 || strcmp(argv[i], "--show-ends") == 0) {
            showEnds = true;
        } else if (strcmp(argv[i], "-T") == 0 || strcmp(argv[i], "--show-tabs") == 0) {
            showTabs = true;
        } else if (argv[i][0] == '-' && argv[i][1]) {
            return XConsoleResult_InvalidArgument;
        } else {
            if (pathCount >= XCONSOLE_SHELL_MAX_ARGUMENTS) return XConsoleResult_ResourceLimit;
            paths[pathCount++] = argv[i];
        }
    }
    if (pathCount == 0) return XConsoleResult_InvalidArgument;
    for (i = 0; i < pathCount; ++i) {
        path = XString_create();
        if (!path || !xfs_make_path(session, paths[i], path)) {
            if (path) XString_delete_base(path);
            return XConsoleResult_InvalidArgument;
        }
        fd = XFileSystem_open(path, XFileSystem_ReadOnly, &error);
        XString_delete_base(path);
        if (fd == XFD_INVALID) return XConsoleResult_Failed;
        if (offset > 0 && !XFileSystem_seek(fd, offset)) {
            XFileSystem_close(fd);
            return XConsoleResult_IoError;
        }
        while (remaining > 0) {
            int64_t request = (int64_t)sizeof(buffer);
            size_t j;
            if (remaining < request) request = remaining;
            count = XFileSystem_read(fd, buffer, request);
            if (count < 0) {
                XFileSystem_close(fd);
                return XConsoleResult_IoError;
            }
            if (count == 0) break;
            for (j = 0; j < (size_t)count; ++j) {
                unsigned char ch = (unsigned char)buffer[j];
                if (atLineStart) {
                    bool blank = (ch == '\n');
                    if (squeezeBlank && blank && sawBlankLine) continue;
                    if (!(numberAll || numberNonblank) ||
                        (numberNonblank && blank)) {
                        /* no line number */
                    } else {
                        char num[24];
                        int written = snprintf(num, sizeof(num), "%6lld\t",
                                               (long long)(++lineNumber));
                        if (written < 0 || (size_t)written >= sizeof(num) ||
                            !XConsoleShell_write(shell, num, (size_t)written)) {
                            XFileSystem_close(fd);
                            return XConsoleResult_IoError;
                        }
                    }
                    atLineStart = false;
                    if (blank) sawBlankLine = true;
                    else sawBlankLine = false;
                }
                if (showTabs && ch == '\t') {
                    if (!XConsoleShell_writeUtf8(shell, "^I")) {
                        XFileSystem_close(fd);
                        return XConsoleResult_IoError;
                    }
                } else if (!XConsoleShell_write(shell, (const char*)&ch, 1)) {
                    XFileSystem_close(fd);
                    return XConsoleResult_IoError;
                }
                if (ch == '\n') {
                    if (showEnds && !XConsoleShell_writeUtf8(shell, "$")) {
                        XFileSystem_close(fd);
                        return XConsoleResult_IoError;
                    }
                    atLineStart = true;
                }
            }
            remaining -= count;
            if (XConsoleShell_isCancelled(shell)) {
                XFileSystem_close(fd);
                return XConsoleResult_Cancelled;
            }
        }
        XFileSystem_close(fd);
        remaining = length;
        atLineStart = true;
        sawBlankLine = false;
    }
    return XConsoleResult_Ok;
}

#if XCONSOLE_SHELL_FS_HEXDUMP_ON
/**
 * @brief 以 hexdump -C 风格分块输出文件内容。
 * @details
 * 每次只读取 16 字节，支持跳过起始偏移和限制输出长度；不会把整个文件
 * 载入内存。所有文件访问和输出均通过 XinYueC 公共 API 完成。
 */
static int xfs_hexdump(XConsoleShell* shell, XConsoleShellSession* session,
                       int argc, const char* const* argv, void* userData)
{
    XString* path = NULL;
    XFd fd = XFD_INVALID;
    const char* pathText = NULL;
    int error = 0;
    int64_t offset = 0;
    int64_t remaining = INT64_MAX;
    bool offsetSet = false;
    bool lengthSet = false;
    uint8_t buffer[16];
    (void)userData;
    if (!shell || !session || argc < 1) return XConsoleResult_InvalidArgument;
    for (int i = 0; i < argc; ++i) {
        int64_t value;
        if (strcmp(argv[i], "-C") == 0 || strcmp(argv[i], "--canonical") == 0) {
            continue;
        }
        if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--skip") == 0 ||
            strcmp(argv[i], "--offset") == 0) {
            if (offsetSet || i + 1 >= argc ||
                !xfs_parse_nonnegative(argv[++i], &value))
                return XConsoleResult_InvalidArgument;
            offset = value;
            offsetSet = true;
            continue;
        }
        if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--length") == 0) {
            if (lengthSet || i + 1 >= argc ||
                !xfs_parse_nonnegative(argv[++i], &value))
                return XConsoleResult_InvalidArgument;
            remaining = value;
            lengthSet = true;
            continue;
        }
        if (pathText) return XConsoleResult_InvalidArgument;
        pathText = argv[i];
    }
    if (!pathText) return XConsoleResult_InvalidArgument;
    path = XString_create();
    if (!path || !xfs_make_path(session, pathText, path)) goto failed;
    fd = XFileSystem_open(path, XFileSystem_ReadOnly, &error);
    if (fd == XFD_INVALID) goto failed;
    if (offset > 0 && !XFileSystem_seek(fd, offset)) goto io_error;
    while (remaining > 0) {
        int64_t request = (int64_t)sizeof(buffer);
        int64_t count;
        char line[128];
        int written;
        int i;
        if (remaining < request) request = remaining;
        count = XFileSystem_read(fd, buffer, request);
        if (count < 0) goto io_error;
        if (count == 0) break;
        written = snprintf(line, sizeof(line), "%08llx  ", (unsigned long long)offset);
        if (written < 0 || (size_t)written >= sizeof(line)) goto io_error;
        for (i = 0; i < 16; ++i) {
            int part = i < count
                           ? snprintf(line + written, sizeof(line) - (size_t)written,
                                       "%02x ", buffer[i])
                           : snprintf(line + written, sizeof(line) - (size_t)written, "   ");
            if (part < 0 || (size_t)part >= sizeof(line) - (size_t)written) goto io_error;
            written += part;
            if (i == 7) {
                if (written + 1 >= (int)sizeof(line)) goto io_error;
                line[written++] = ' ';
                line[written] = '\0';
            }
        }
        if (written + 2 >= (int)sizeof(line)) goto io_error;
        line[written++] = '|';
        for (i = 0; i < count; ++i) {
            unsigned char byte = buffer[i];
            if (written + 1 >= (int)sizeof(line)) goto io_error;
            line[written++] = (byte >= 0x20u && byte <= 0x7eu) ? (char)byte : '.';
        }
        if (written + 3 >= (int)sizeof(line)) goto io_error;
        while (i++ < 16) line[written++] = ' ';
        line[written++] = '|';
        line[written++] = '\n';
        line[written] = '\0';
        if (!XConsoleShell_write(shell, line, (size_t)written)) goto io_error;
        if (lengthSet) remaining -= count;
        if (offset > INT64_MAX - count) break;
        offset += count;
        if (XConsoleShell_isCancelled(shell)) {
            XFileSystem_close(fd);
            XString_delete_base(path);
            return XConsoleResult_Cancelled;
        }
    }
    XFileSystem_close(fd);
    XString_delete_base(path);
    return XConsoleResult_Ok;
io_error:
    if (fd != XFD_INVALID) XFileSystem_close(fd);
    if (path) XString_delete_base(path);
    return XConsoleResult_IoError;
failed:
    if (fd != XFD_INVALID) XFileSystem_close(fd);
    if (path) XString_delete_base(path);
    return XConsoleResult_Failed;
}
#endif

static void xfs_number(int64_t value, char* buffer, size_t capacity)
{
    char reverse[32];
    size_t count = 0;
    size_t i;
    bool negative = value < 0;
    uint64_t absolute = negative ? (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;
    if (!buffer || capacity < 2) return;
    do {
        reverse[count++] = (char)('0' + absolute % 10u);
        absolute /= 10u;
    } while (absolute && count < sizeof(reverse));
    if (negative && count < sizeof(reverse)) reverse[count++] = '-';
    if (count + 1 > capacity) count = capacity - 1;
    for (i = 0; i < count; ++i) buffer[i] = reverse[count - i - 1];
    buffer[count] = '\0';
}

static int xfs_stat(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XString* path = XString_create();
    XFileStat stat;
    char number[32];
    (void)userData;
    if (argc != 1 || !path || !xfs_make_path(session, argv[0], path) ||
        !XFileSystem_stat(path, &stat)) {
        if (path) XString_delete_base(path);
        return XConsoleResult_Failed;
    }
    xfs_number(stat.size, number, sizeof(number));
    XString_delete_base(path);
    if (!xfs_write(shell, "大小=") || !xfs_write(shell, number) ||
        !xfs_write(shell, stat.isDir ? "\n类型=目录\n" : "\n类型=文件\n"))
        return XConsoleResult_IoError;
    return XConsoleResult_Ok;
}

static int xfs_remove(XConsoleShell* shell, XConsoleShellSession* session,
                      int argc, const char* const* argv, void* userData)
{
    XString* path;
    bool ok = true;
    bool force = false;
    bool recursive = false;
    bool endOptions = false;
    const char* paths[XCONSOLE_SHELL_MAX_ARGUMENTS];
    int pathCount = 0;
    int i;
    (void)shell;
    (void)userData;
    for (i = 0; i < argc; ++i) {
        const char* argument = argv[i];
        size_t j;
        if (!endOptions && strcmp(argument, "--") == 0) {
            endOptions = true;
            continue;
        }
        if (!endOptions && argument[0] == '-' && argument[1]) {
            if (strcmp(argument, "--force") == 0) {
                force = true;
            } else if (strcmp(argument, "--recursive") == 0) {
                recursive = true;
            } else {
                for (j = 1; argument[j]; ++j) {
                    if (argument[j] == 'f') force = true;
                    else if (argument[j] == 'r' || argument[j] == 'R') recursive = true;
                    else return XConsoleResult_InvalidArgument;
                }
            }
            continue;
        }
        if (pathCount >= XCONSOLE_SHELL_MAX_ARGUMENTS) return XConsoleResult_ResourceLimit;
        paths[pathCount++] = argument;
    }
    if (pathCount == 0) return XConsoleResult_InvalidArgument;
    for (i = 0; i < pathCount; ++i) {
        path = XString_create();
        if (!path || !xfs_make_path(session, paths[i], path)) {
            if (path) XString_delete_base(path);
            return XConsoleResult_InvalidArgument;
        }
        {
            XFileStat stat;
            if (XFileSystem_stat(path, &stat) && stat.isDir && recursive)
                ok = XFileSystem_rmdir(path, true) && ok;
            else
                ok = XFileSystem_remove(path) && ok;
        }
        XString_delete_base(path);
    }
    if (!ok && force) return XConsoleResult_Ok;
    return ok ? XConsoleResult_Ok : XConsoleResult_Failed;
}

static int xfs_mkdir(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XString* path;
    bool recursive = false;
    bool ok = true;
    int pathCount = 0;
    int i;
    (void)shell;
    (void)userData;
    for (i = 0; i < argc; ++i)
        if (strcmp(argv[i], "--parents") == 0 || strcmp(argv[i], "-p") == 0) recursive = true;
    for (i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "--parents") == 0 || strcmp(argv[i], "-p") == 0) {
            continue;
        }
        ++pathCount;
        path = XString_create();
        if (!path || !xfs_make_path(session, argv[i], path)) {
            if (path) XString_delete_base(path);
            return XConsoleResult_InvalidArgument;
        }
        ok = XFileSystem_mkdir(path, recursive) && ok;
        XString_delete_base(path);
    }
    if (pathCount == 0) return XConsoleResult_InvalidArgument;
    return ok ? XConsoleResult_Ok : XConsoleResult_Failed;
}

static int xfs_rmdir(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XString* path;
    bool parents = false;
    bool ok = true;
    int pathCount = 0;
    int i;
    (void)shell;
    (void)userData;
    for (i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--parents") == 0) {
            parents = true;
        } else if (argv[i][0] == '-') {
            return XConsoleResult_InvalidArgument;
        }
    }
    for (i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-p") == 0 || strcmp(argv[i], "--parents") == 0) continue;
        ++pathCount;
        path = XString_create();
        if (!path || !xfs_make_path(session, argv[i], path)) {
            if (path) XString_delete_base(path);
            return XConsoleResult_InvalidArgument;
        }
        ok = XFileSystem_rmdir(path, false) && ok;
        if (parents && ok) {
            for (;;) {
                const char* text = XString_toUtf8(path);
                const char* slash;
                if (!text || !*text || strcmp(text, "/") == 0) break;
                slash = strrchr(text, '/');
                if (!slash || slash == text) break;
                XString_truncate(path, (size_t)(slash - text));
                if (!XFileSystem_rmdir(path, false)) break;
            }
        }
        XString_delete_base(path);
    }
    if (pathCount == 0) return XConsoleResult_InvalidArgument;
    return ok ? XConsoleResult_Ok : XConsoleResult_Failed;
}

static bool xfs_copy_recursive(const XString* source, const XString* target)
{
    XFileStat stat;
    XString* name = NULL;
    XString* childSource = NULL;
    XString* childTarget = NULL;
    XDirEntry entry;
    XDirIterator iterator = NULL;
    bool ok = true;
    if (!source || !target || !XFileSystem_stat(source, &stat)) return false;
    if (!stat.isDir) return XFileSystem_copy(source, target);
    if (!XFileSystem_mkdir(target, false)) return false;
    name = XString_create();
    childSource = XString_create();
    childTarget = XString_create();
    if (!name || !childSource || !childTarget) ok = false;
    if (ok) iterator = XFileSystem_opendir(source);
    if (ok && !iterator) ok = false;
    if (ok) {
        memset(&entry, 0, sizeof(entry));
        entry.name = name;
        while (XFileSystem_readdir(iterator, &entry)) {
            const char* item = XString_toUtf8(name);
            if (strcmp(item, ".") == 0 || strcmp(item, "..") == 0) continue;
            if (!XString_assign(childSource, source) ||
                !xfs_append_path_component(childSource, item) ||
                !XString_assign(childTarget, target) ||
                !xfs_append_path_component(childTarget, item) ||
                !xfs_copy_recursive(childSource, childTarget)) {
                ok = false;
                break;
            }
        }
        XFileSystem_closedir(iterator);
    }
    if (name) XString_delete_base(name);
    if (childSource) XString_delete_base(childSource);
    if (childTarget) XString_delete_base(childTarget);
    return ok;
}

static int xfs_copy(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    bool recursive = false;
    bool force = false;
    bool endOptions = false;
    const char* sourceArgs[XCONSOLE_SHELL_MAX_ARGUMENTS];
    int sourceCount = 0;
    XString* target = NULL;
    bool targetIsDir = false;
    bool ok = true;
    int i;
    (void)shell;
    (void)userData;
    for (i = 0; i < argc; ++i) {
        const char* argument = argv[i];
        size_t j;
        if (!endOptions && strcmp(argument, "--") == 0) {
            endOptions = true;
            continue;
        }
        if (!endOptions && argument[0] == '-' && argument[1]) {
            if (strcmp(argument, "--recursive") == 0) recursive = true;
            else if (strcmp(argument, "--force") == 0) force = true;
            else {
                for (j = 1; argument[j]; ++j) {
                    if (argument[j] == 'r' || argument[j] == 'R') recursive = true;
                    else if (argument[j] == 'f') force = true;
                    else return XConsoleResult_InvalidArgument;
                }
            }
            continue;
        }
        if (sourceCount < XCONSOLE_SHELL_MAX_ARGUMENTS) sourceArgs[sourceCount++] = argument;
        else return XConsoleResult_ResourceLimit;
    }
    if (sourceCount < 2) return XConsoleResult_InvalidArgument;
    target = XString_create();
    if (!target || !xfs_make_path(session, sourceArgs[sourceCount - 1], target)) {
        if (target) XString_delete_base(target);
        return XConsoleResult_InvalidArgument;
    }
    {
        XFileStat targetStat;
        targetIsDir = XFileSystem_stat(target, &targetStat) && targetStat.isDir;
    }
    if (sourceCount > 2 && !targetIsDir) {
        XString_delete_base(target);
        return XConsoleResult_Failed;
    }
    for (i = 0; i < sourceCount - 1; ++i) {
        XString* source = XString_create();
        XString* destination = XString_create();
        XFileStat stat;
        const char* sourceName;
        if (!source || !destination || !xfs_make_path(session, sourceArgs[i], source) ||
            !xfs_make_path(session, sourceArgs[sourceCount - 1], destination) ||
            !XFileSystem_stat(source, &stat)) {
            if (source) XString_delete_base(source);
            if (destination) XString_delete_base(destination);
            ok = false;
            break;
        }
        if (stat.isDir && !recursive) ok = false;
        sourceName = strrchr(XString_toUtf8(source), '/');
        if (ok && targetIsDir && sourceName) {
            ok = xfs_append_path_component(destination, sourceName + 1);
        }
        if (ok && force) {
            XFileStat destinationStat;
            if (XFileSystem_stat(destination, &destinationStat))
                ok = destinationStat.isDir ? XFileSystem_rmdir(destination, true) :
                     XFileSystem_remove(destination);
        }
        if (ok) ok = xfs_copy_recursive(source, destination);
        XString_delete_base(source);
        XString_delete_base(destination);
        if (!ok) break;
    }
    XString_delete_base(target);
    return ok ? XConsoleResult_Ok : XConsoleResult_Failed;
}

static int xfs_move(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    bool force = false;
    const char* sourceArgs[XCONSOLE_SHELL_MAX_ARGUMENTS];
    int sourceCount = 0;
    XString* target = NULL;
    bool targetIsDir = false;
    bool ok = true;
    int i;
    (void)shell;
    (void)userData;
    for (i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--force") == 0) force = true;
        else if (sourceCount < XCONSOLE_SHELL_MAX_ARGUMENTS) sourceArgs[sourceCount++] = argv[i];
        else return XConsoleResult_ResourceLimit;
    }
    if (sourceCount < 2) return XConsoleResult_InvalidArgument;
    target = XString_create();
    if (!target || !xfs_make_path(session, sourceArgs[sourceCount - 1], target)) {
        if (target) XString_delete_base(target);
        return XConsoleResult_InvalidArgument;
    }
    {
        XFileStat targetStat;
        targetIsDir = XFileSystem_stat(target, &targetStat) && targetStat.isDir;
    }
    if (sourceCount > 2 && !targetIsDir) {
        XString_delete_base(target);
        return XConsoleResult_Failed;
    }
    for (i = 0; i < sourceCount - 1; ++i) {
        XString* source = XString_create();
        XString* destination = XString_create();
        const char* sourceName;
        if (!source || !destination || !xfs_make_path(session, sourceArgs[i], source) ||
            !xfs_make_path(session, sourceArgs[sourceCount - 1], destination)) {
            if (source) XString_delete_base(source);
            if (destination) XString_delete_base(destination);
            ok = false;
            break;
        }
        sourceName = strrchr(XString_toUtf8(source), '/');
        if (targetIsDir && sourceName)
            ok = xfs_append_path_component(destination, sourceName + 1);
        if (ok && force) {
            XFileStat targetStat;
            if (XFileSystem_stat(destination, &targetStat))
                ok = targetStat.isDir ? XFileSystem_rmdir(destination, true) : XFileSystem_remove(destination);
        }
        if (ok) ok = XFileSystem_rename(source, destination);
        XString_delete_base(source);
        XString_delete_base(destination);
        if (!ok) break;
    }
    XString_delete_base(target);
    return ok ? XConsoleResult_Ok : XConsoleResult_Failed;
}

static int xfs_write_file(XConsoleShell* shell, XConsoleShellSession* session,
                          int argc, const char* const* argv, void* userData)
{
    XString* path = XString_create();
    XFd fd;
    int error = 0;
    int i;
    bool ok = true;
    (void)shell;
    (void)userData;
    if (argc < 2 || !path || !xfs_make_path(session, argv[0], path)) {
        if (path) XString_delete_base(path);
        return XConsoleResult_InvalidArgument;
    }
    fd = XFileSystem_open(path, XFileSystem_WriteOnly | XFileSystem_Create |
                          XFileSystem_Truncate, &error);
    XString_delete_base(path);
    if (fd == XFD_INVALID) return XConsoleResult_Failed;
    for (i = 1; i < argc && ok; ++i) {
        if (i > 1) ok = XFileSystem_write(fd, " ", 1) == 1;
        if (ok) ok = XFileSystem_write(fd, argv[i], (int64_t)strlen(argv[i])) ==
                         (int64_t)strlen(argv[i]);
    }
    if (ok) ok = XFileSystem_flush(fd);
    XFileSystem_close(fd);
    return ok ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xfs_link(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XString* target = XString_create();
    XString* link = XString_create();
    bool ok;
    (void)shell;
    (void)userData;
    if (argc != 2 || !target || !link || !xfs_make_path(session, argv[0], target) ||
        !xfs_make_path(session, argv[1], link)) {
        if (target) XString_delete_base(target);
        if (link) XString_delete_base(link);
        return XConsoleResult_InvalidArgument;
    }
    ok = XFileSystem_hardLink(target, link);
    XString_delete_base(target);
    XString_delete_base(link);
    return ok ? XConsoleResult_Ok : XConsoleResult_Failed;
}

#if XCONSOLE_SHELL_FS_LN_ON
/* POSIX ln 的硬链接语义没有对应的 XFileSystem 公共 API；这里仅实现明确的 ln -s。 */
static int xfs_ln(XConsoleShell* shell, XConsoleShellSession* session,
                  int argc, const char* const* argv, void* userData)
{
    XString* target = NULL;
    XString* link = NULL;
    bool symbolic = false;
    bool endOptions = false;
    const char* operands[2];
    int operandCount = 0;
    int i;
    bool ok;
    (void)shell;
    (void)userData;
    for (i = 0; i < argc; ++i) {
        const char* argument = argv[i];
        size_t j;
        if (!endOptions && strcmp(argument, "--") == 0) {
            endOptions = true;
            continue;
        }
        if (!endOptions && argument[0] == '-' && argument[1]) {
            if (strcmp(argument, "--symbolic") == 0) {
                symbolic = true;
                continue;
            }
            for (j = 1; argument[j]; ++j) {
                if (argument[j] != 's') return XConsoleResult_InvalidArgument;
                symbolic = true;
            }
            continue;
        }
        if (operandCount >= 2) return XConsoleResult_InvalidArgument;
        operands[operandCount++] = argument;
    }
    if (operandCount != 2) return XConsoleResult_InvalidArgument;
    target = XString_create();
    link = XString_create();
    if (!target || !link || !xfs_make_path(session, operands[0], target) ||
        !xfs_make_path(session, operands[1], link)) {
        if (target) XString_delete_base(target);
        if (link) XString_delete_base(link);
        return XConsoleResult_InvalidArgument;
    }
    ok = symbolic ? XFileSystem_link(target, link) :
                    XFileSystem_hardLink(target, link);
    XString_delete_base(target);
    XString_delete_base(link);
    return ok ? XConsoleResult_Ok : XConsoleResult_Failed;
}
#endif

#if XCONSOLE_SHELL_FS_UNLINK_ON
static int xfs_unlink(XConsoleShell* shell, XConsoleShellSession* session,
                      int argc, const char* const* argv, void* userData)
{
    bool ok = true;
    bool endOptions = false;
    int pathCount = 0;
    int i;
    (void)shell;
    (void)userData;
    for (i = 0; i < argc; ++i) {
        XString* path;
        if (!endOptions && strcmp(argv[i], "--") == 0) {
            endOptions = true;
            continue;
        }
        if (!endOptions && argv[i][0] == '-') return XConsoleResult_InvalidArgument;
        ++pathCount;
        path = XString_create();
        if (!path || !xfs_make_path(session, argv[i], path)) {
            if (path) XString_delete_base(path);
            return XConsoleResult_InvalidArgument;
        }
        ok = XFileSystem_remove(path) && ok;
        XString_delete_base(path);
    }
    if (pathCount == 0) return XConsoleResult_InvalidArgument;
    return ok ? XConsoleResult_Ok : XConsoleResult_Failed;
}
#endif

#if XCONSOLE_SHELL_FS_FORMAT_ON
static bool xfs_format_progress(int progress, void* userData)
{
    XConsoleShell* shell = (XConsoleShell*)userData;
    (void)progress;
    return shell && !XConsoleShell_isCancelled(shell);
}

static int xfs_format(XConsoleShell* shell, XConsoleShellSession* session,
                      int argc, const char* const* argv, void* userData)
{
    XString* drive = XString_create();
    bool ok;
    (void)userData;
    if ((argc != 1 && argc != 2) || (argc == 2 && strcmp(argv[1], "--force") != 0) ||
        !drive || !xfs_make_path(session, argv[0], drive)) {
        if (drive) XString_delete_base(drive);
        return XConsoleResult_InvalidArgument;
    }
    ok = XFileSystem_format(drive, XFileSystemType_Auto, NULL,
                            argc == 2 ? XFileSystemFormat_Force : XFileSystemFormat_None,
                            0, xfs_format_progress, shell);
    XString_delete_base(drive);
    return ok ? XConsoleResult_Ok : XConsoleResult_Failed;
}
#endif

static const XConsoleCommand g_fsCommands[] = {
#if XCONSOLE_SHELL_FS_PWD_ON
    { "pwd", NULL, "显示当前会话目录", "fs pwd", 0, 0, 0, xfs_pwd, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_CD_ON
    { "cd", NULL, "切换当前会话目录", "fs cd <path>", 1, 1, 0, xfs_cd, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_LS_ON
    { "ls", NULL, "列出目录", "fs ls [-alRFdh] [path...]", 0, -1, 0, xfs_ls, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_CAT_ON
    { "cat", NULL, "分块输出文件", "fs cat [-nbEsT] [--offset N] [--length N] <path...>", 1, -1, 0, xfs_cat, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_HEXDUMP_ON
    { "hexdump", NULL, "十六进制查看文件", "fs hexdump [-C] [-s offset] [-n length] <path>", 1, -1, 0, xfs_hexdump, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_STAT_ON
    { "stat", NULL, "显示文件属性", "fs stat <path>", 1, 1, 0, xfs_stat, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_RM_ON
    { "rm", NULL, "删除文件", "fs rm [-fr] <path...>", 1, -1, XConsoleCommandFlag_Dangerous, xfs_remove, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_MKDIR_ON
    { "mkdir", NULL, "创建目录", "fs mkdir [-p] <path...>", 1, -1, 0, xfs_mkdir, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_RMDIR_ON
    { "rmdir", NULL, "删除目录", "fs rmdir [-p] <path...>", 1, -1, XConsoleCommandFlag_Dangerous, xfs_rmdir, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_CP_ON
    { "cp", NULL, "复制文件", "fs cp [-rf] <source...> <target>", 2, -1, 0, xfs_copy, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_MV_ON
    { "mv", NULL, "移动文件", "fs mv [-f] <source...> <target>", 2, -1, 0, xfs_move, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_WRITE_ON
    { "write", NULL, "写入文件", "fs write <path> <data...>", 2, -1, XConsoleCommandFlag_Dangerous, xfs_write_file, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_LINK_ON
    { "link", NULL, "创建硬链接", "fs link <target> <link>", 2, 2, XConsoleCommandFlag_Dangerous, xfs_link, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_LN_ON
    { "ln", NULL, "创建链接（默认硬链接）", "fs ln [-s] <target> <link>", 2, -1, XConsoleCommandFlag_Dangerous, xfs_ln, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_UNLINK_ON
    { "unlink", NULL, "删除文件链接", "fs unlink <path...>", 1, -1, XConsoleCommandFlag_Dangerous, xfs_unlink, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_FORMAT_ON
    { "format", NULL, "格式化存储设备", "fs format <volume> [--force]", 1, 2, XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator, xfs_format, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_TOUCH_ON
    { "touch", NULL, "创建文件或更新时间", "fs touch [-c] <path...>", 1, -1, XConsoleCommandFlag_Dangerous, xfs_touch, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_CHMOD_ON
    { "chmod", NULL, "设置文件权限", "fs chmod [ugoa]*[+-=][rwxX]*|<octal> <path...>", 2, -1, XConsoleCommandFlag_Dangerous, xfs_chmod, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_READLINK_ON
    { "readlink", NULL, "读取符号链接目标", "fs readlink [-f] <path...>", 1, -1, 0, xfs_readlink, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_REALPATH_ON
    { "realpath", NULL, "输出规范路径", "fs realpath <path>", 1, 1, 0, xfs_realpath, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_TRUNCATE_ON
    { "truncate", NULL, "调整文件大小", "fs truncate -s <size> [-c] <path...>", 2, -1, XConsoleCommandFlag_Dangerous, xfs_truncate, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_DF_ON
    { "df", NULL, "显示存储空间", "fs df [path]", 0, 1, 0, xfs_df, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_DU_ON
    { "du", NULL, "统计目录大小", "fs du [path]", 0, 1, 0, xfs_du, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_WC_ON
    { "wc", NULL, "统计文件行词节数", "fs wc [-l|-w|-c|-m] <path...>", 1, -1, 0, xfs_wc, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_HEAD_ON
    { "head", NULL, "输出文件前若干行", "fs head [-n lines] <path...>", 1, -1, 0, xfs_head, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_TAIL_ON
    { "tail", NULL, "输出文件后若干行", "fs tail [-n lines] <path...>", 1, -1, 0, xfs_tail, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_FIND_ON
    { "find", NULL, "递归查找文件", "fs find <path> [-name pattern] [-type f|d] [-maxdepth N] [-mtime N] [-size N]", 1, -1, 0, xfs_find, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_TREE_ON
    { "tree", NULL, "树状列出目录", "fs tree [path]", 0, 1, 0, xfs_tree, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_CMP_ON
    { "cmp", NULL, "比较两个文件", "fs cmp <left> <right>", 2, 2, 0, xfs_cmp, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_FILE_ON
    { "file", NULL, "显示文件类型", "fs file <path>", 1, 1, 0, xfs_file, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_BASENAME_ON
    { "basename", NULL, "输出路径最后一段", "fs basename <path>", 1, 1, 0, xfs_basename, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_DIRNAME_ON
    { "dirname", NULL, "输出路径目录部分", "fs dirname <path>", 1, 1, 0, xfs_dirname, NULL, 0, NULL },
#endif
    /* 末尾哨兵使所有 FS 子命令关闭时仍保持有效的 C99 初始化。 */
    { NULL, NULL, NULL, NULL, 0, 0, 0, NULL, NULL, 0, NULL }
};

#define XCONSOLE_SHELL_FS_COMMAND_COUNT \
    (sizeof(g_fsCommands) / sizeof(g_fsCommands[0]) - 1u)

const XConsoleCommand XConsoleShellFileSystem_command = {
    "fs", NULL, "文件系统命令",
    "fs <pwd|cd|ls|cat|hexdump|stat|rm|mkdir|rmdir|cp|mv|write|link|ln|unlink|touch|chmod|readlink|realpath|truncate|df|du|wc|head|tail|find|tree|cmp|file|basename|dirname|format>", 1, -1, 0,
    NULL, g_fsCommands, XCONSOLE_SHELL_FS_COMMAND_COUNT, NULL
};

#if XCONSOLE_SHELL_FS_LS_ON
/* 根级 ls 与 fs ls 共用同一处理函数，避免维护两份目录遍历逻辑。 */
const XConsoleCommand XConsoleShellFileSystem_ls_command = {
    "ls", NULL, "列出当前目录或指定目录", "ls [-alRFdh] [path...]", 0, -1, 0,
    xfs_ls, NULL, 0, NULL
};
#endif

static const XConsoleCommand g_rootFileCommands[] = {
#if XCONSOLE_SHELL_FS_PWD_ON
    { "pwd", NULL, "显示当前会话目录", "pwd", 0, 0, 0, xfs_pwd, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_CD_ON
    { "cd", NULL, "切换当前会话目录", "cd <path>", 1, 1, 0, xfs_cd, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_CAT_ON
    { "cat", NULL, "分块输出文件", "cat [-nbEsT] [--offset N] [--length N] <path...>", 1, -1, 0, xfs_cat, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_HEXDUMP_ON
    { "hexdump", NULL, "十六进制查看文件", "hexdump [-C] [-s offset] [-n length] <path>", 1, -1, 0, xfs_hexdump, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_STAT_ON
    { "stat", NULL, "显示文件属性", "stat <path>", 1, 1, 0, xfs_stat, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_RM_ON
    { "rm", NULL, "删除文件", "rm [-fr] <path...>", 1, -1, XConsoleCommandFlag_Dangerous, xfs_remove, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_MKDIR_ON
    { "mkdir", NULL, "创建目录", "mkdir [-p] <path...>", 1, -1, 0, xfs_mkdir, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_RMDIR_ON
    { "rmdir", NULL, "删除目录", "rmdir [-p] <path...>", 1, -1, XConsoleCommandFlag_Dangerous, xfs_rmdir, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_CP_ON
    { "cp", NULL, "复制文件", "cp [-rf] <source...> <target>", 2, -1, 0, xfs_copy, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_MV_ON
    { "mv", NULL, "移动文件", "mv [-f] <source...> <target>", 2, -1, 0, xfs_move, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_WRITE_ON
    { "write", NULL, "写入文件", "write <path> <data...>", 2, -1, XConsoleCommandFlag_Dangerous, xfs_write_file, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_LINK_ON
    { "link", NULL, "创建硬链接", "link <target> <link>", 2, 2, XConsoleCommandFlag_Dangerous, xfs_link, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_LN_ON
    { "ln", NULL, "创建链接（默认硬链接）", "ln [-s] <target> <link>", 2, -1, XConsoleCommandFlag_Dangerous, xfs_ln, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_UNLINK_ON
    { "unlink", NULL, "删除文件链接", "unlink <path...>", 1, -1, XConsoleCommandFlag_Dangerous, xfs_unlink, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_TOUCH_ON
    { "touch", NULL, "创建文件或更新时间", "touch [-c] <path...>", 1, -1, XConsoleCommandFlag_Dangerous, xfs_touch, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_CHMOD_ON
    { "chmod", NULL, "设置文件权限", "chmod [ugoa]*[+-=][rwxX]*|<octal> <path...>", 2, -1, XConsoleCommandFlag_Dangerous, xfs_chmod, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_READLINK_ON
    { "readlink", NULL, "读取符号链接目标", "readlink [-f] <path...>", 1, -1, 0, xfs_readlink, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_REALPATH_ON
    { "realpath", NULL, "输出规范路径", "realpath <path>", 1, 1, 0, xfs_realpath, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_TRUNCATE_ON
    { "truncate", NULL, "调整文件大小", "truncate -s <size> [-c] <path...>", 2, -1, XConsoleCommandFlag_Dangerous, xfs_truncate, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_DF_ON
    { "df", NULL, "显示存储空间", "df [path]", 0, 1, 0, xfs_df, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_DU_ON
    { "du", NULL, "统计目录大小", "du [path]", 0, 1, 0, xfs_du, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_WC_ON
    { "wc", NULL, "统计文件行词和字节", "wc [-l|-w|-c|-m] <path...>", 1, -1, 0, xfs_wc, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_HEAD_ON
    { "head", NULL, "输出文件开头", "head [-n count] <path...>", 1, -1, 0, xfs_head, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_TAIL_ON
    { "tail", NULL, "输出文件结尾", "tail [-n count] <path...>", 1, -1, 0, xfs_tail, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_FIND_ON
    { "find", NULL, "递归查找文件", "find <path> [-name pattern] [-type f|d] [-maxdepth N] [-mtime N] [-size N]", 1, -1, 0, xfs_find, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_TREE_ON
    { "tree", NULL, "树状列出目录", "tree [path]", 0, 1, 0, xfs_tree, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_CMP_ON
    { "cmp", NULL, "比较两个文件", "cmp <left> <right>", 2, 2, 0, xfs_cmp, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_FILE_ON
    { "file", NULL, "显示文件类型", "file <path>", 1, 1, 0, xfs_file, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_BASENAME_ON
    { "basename", NULL, "输出路径最后一段", "basename <path>", 1, 1, 0, xfs_basename, NULL, 0, NULL },
#endif
#if XCONSOLE_SHELL_FS_DIRNAME_ON
    { "dirname", NULL, "输出路径目录部分", "dirname <path>", 1, 1, 0, xfs_dirname, NULL, 0, NULL },
#endif
    { NULL, NULL, NULL, NULL, 0, 0, 0, NULL, NULL, 0, NULL }
};

bool XConsoleShellFileSystem_registerRootCommands(XConsoleShell* shell)
{
    size_t count = sizeof(g_rootFileCommands) / sizeof(g_rootFileCommands[0]) - 1u;
    return shell ? XConsoleShell_registerStaticCommands(shell, g_rootFileCommands, count) : false;
}

int XConsoleShellFileSystem_execute(XConsoleShell* shell,
                                    XConsoleShellSession* session,
                                    int argc, const char* const* argv,
                                    void* userData)
{
    (void)userData;
    if (!shell || !session || argc < 1 || !argv || !argv[0])
        return XConsoleResult_InvalidArgument;
    {
        size_t i;
        for (i = 0; i < XCONSOLE_SHELL_FS_COMMAND_COUNT; ++i) {
            const XConsoleCommand* command = &g_fsCommands[i];
            int subArgc;
            if (strcmp(command->name, argv[0]) != 0) continue;
            subArgc = argc - 1;
            if (subArgc < command->minArgs ||
                (command->maxArgs >= 0 && subArgc > command->maxArgs))
                return XConsoleResult_InvalidArgument;
            if ((command->flags & XConsoleCommandFlag_Dangerous) &&
                (!session->authenticated
#if XCONSOLE_SHELL_PERMISSION_ON
                 || !(session->permissionMask & XConsoleShellPermission_Dangerous)
#endif
                ))
                return XConsoleResult_PermissionDenied;
            if ((command->flags & XConsoleCommandFlag_Administrator) &&
                (!session->authenticated
#if XCONSOLE_SHELL_PERMISSION_ON
                 || !(session->permissionMask & XConsoleShellPermission_Administrator)
#endif
                ))
                return XConsoleResult_PermissionDenied;
            if (!command->handler) return XConsoleResult_NotSupported;
            return command->handler(shell, session, subArgc, argv + 1,
                                    command->userData);
        }
    }
    return XConsoleResult_UnknownCommand;
}

#undef XCONSOLE_SHELL_FS_COMMAND_COUNT

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_FILESYSTEM_ON */
