/**
 * @file XConsoleShellVi.c
 * @brief XConsoleShell 的 vi/vim 风格行编辑命令实现。
 * @details
 * 编辑器使用会话状态机复用 Shell 的“下一行输入”路由：`vi <path>` 打开文件
 * 后进入命令模式，后续输入行由 Shell 分流到 XConsoleShellVi_submitLine。
 * 命令模式支持 `:w`、`:q`、`:q!`、`:wq`/`:x` 保存退出，以及 `i`/`a` 插入、
 * `d` 删除、`r` 替换和 `p` 重新显示。文件读写只通过 XFileSystem 公共 API。
 */

#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_EDITOR_ON

#include "XConsoleShellVi.h"
#include "XFileSystem.h"
#include "XString.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool xvi_write_line(XConsoleShell* shell, const char* text)
{
    return shell && text && XConsoleShell_writeUtf8(shell, text) &&
           XConsoleShell_writeUtf8(shell, "\n");
}

static bool xvi_make_path(const XConsoleShellSession* session, const char* input,
                          XString* output)
{
    const char* value = input && input[0] ? input : ".";
    bool absolute;
    XString* raw;
    if (!session || !output) return false;
    raw = XString_create_utf8(value);
    if (!raw) return false;
    absolute = value[0] == '/' || (value[0] && value[1] == ':');
    if (!absolute) {
        XString* prefix = XString_create_utf8(
            session->currentPath[0] ? session->currentPath : "/");
        if (!prefix) {
            XString_delete_base(raw);
            return false;
        }
        if (strcmp(value, ".") != 0) {
            const char* text = XString_toUtf8(raw);
            if (text[0]) {
                if (!XString_append_utf8(prefix, "/") ||
                    !XString_append_utf8(prefix, text)) {
                    XString_delete_base(prefix);
                    XString_delete_base(raw);
                    return false;
                }
            }
        }
        XString_delete_base(raw);
        raw = prefix;
    }
    if (!XFileSystem_resolvePath(raw, output, XPathStyle_Absolute))
        XString_assign(output, raw);
    XString_delete_base(raw);
    return XString_size_base(output) < XCONSOLE_SHELL_MAX_PATH;
}

static void xvi_clear(XConsoleShellSession* session)
{
    if (!session) return;
    session->editorActive = false;
    session->editorInsertMode = false;
    session->editorModified = false;
    session->editorInsertAfter = false;
    session->editorLineCount = 0;
    session->editorCursorLine = 0;
    session->editorCursorColumn = 0;
    session->editorPath[0] = '\0';
    memset(session->editorLines, 0, sizeof(session->editorLines));
}

static bool xvi_load_lines(XConsoleShell* shell, XConsoleShellSession* session,
                           const XString* path)
{
    XFd fd;
    int error = 0;
    char buffer[128];
    char pending[XCONSOLE_SHELL_LINE_BUFFER_SIZE];
    size_t pendingLength = 0;
    int64_t count;
    size_t i;
    (void)shell;
    session->editorLineCount = 0;
    fd = XFileSystem_open(path, XFileSystem_ReadOnly, &error);
    if (fd == XFD_INVALID) return true; /* 新文件：空缓冲。 */
    while ((count = XFileSystem_read(fd, buffer, sizeof(buffer))) > 0) {
        for (i = 0; i < (size_t)count; ++i) {
            char c = buffer[i];
            if (c == '\n') {
                if (session->editorLineCount >= XCONSOLE_SHELL_EDITOR_MAX_LINES) {
                    XFileSystem_close(fd);
                    return false;
                }
                pending[pendingLength] = '\0';
                memcpy(session->editorLines[session->editorLineCount],
                       pending, pendingLength + 1u);
                ++session->editorLineCount;
                pendingLength = 0;
            } else if (pendingLength < sizeof(pending) - 1u) {
                pending[pendingLength++] = c;
            }
        }
    }
    XFileSystem_close(fd);
    if (count < 0) return false;
    if (session->editorLineCount >= XCONSOLE_SHELL_EDITOR_MAX_LINES)
        return false;
    pending[pendingLength] = '\0';
    memcpy(session->editorLines[session->editorLineCount], pending,
           pendingLength + 1u);
    ++session->editorLineCount;
    return true;
}

static bool xvi_save_lines(XConsoleShell* shell, XConsoleShellSession* session,
                           const XString* path)
{
    XFd fd;
    int error = 0;
    size_t i;
    bool ok = true;
    (void)shell;
    fd = XFileSystem_open(path, XFileSystem_WriteOnly | XFileSystem_Create |
                          XFileSystem_Truncate, &error);
    if (fd == XFD_INVALID) return false;
    for (i = 0; i < session->editorLineCount && ok; ++i) {
        const char* line = session->editorLines[i];
        size_t length = strlen(line);
        if (length &&
            XFileSystem_write(fd, line, (int64_t)length) != (int64_t)length)
            ok = false;
        if (ok && XFileSystem_write(fd, "\n", 1) != 1) ok = false;
    }
    if (ok) ok = XFileSystem_flush(fd);
    XFileSystem_close(fd);
    return ok;
}

static bool xvi_print_lines(XConsoleShell* shell, XConsoleShellSession* session)
{
    char line[24];
    size_t i;
    bool ok = true;
    if (session->editorLineCount == 0)
        return xvi_write_line(shell, "空文件");
    for (i = 0; i < session->editorLineCount && ok; ++i) {
        int written = snprintf(line, sizeof(line), "%zu:\t", i + 1u);
        if (written < 0 || (size_t)written >= sizeof(line)) return false;
        ok = XConsoleShell_writeUtf8(shell, line) &&
             XConsoleShell_writeUtf8(shell, session->editorLines[i]) &&
             XConsoleShell_writeUtf8(shell, "\n");
    }
    return ok;
}

static bool xvi_parse_number(const char* text, size_t* value)
{
    char* end = NULL;
    unsigned long long parsed;
    if (!text || !text[0] || !value) return false;
    parsed = strtoull(text, &end, 10);
    if (!end || *end != '\0') return false;
    *value = (size_t)parsed;
    return parsed <= (unsigned long long)SIZE_MAX;
}

static bool xvi_enter_insert(XConsoleShell* shell, XConsoleShellSession* session,
                             size_t line, bool after)
{
    if (!session) return false;
    if (line > session->editorLineCount) return false;
    session->editorCursorLine = line;
    if (line < session->editorLineCount)
        session->editorCursorColumn = strlen(session->editorLines[line]);
    else if (session->editorLineCount > 0)
        session->editorCursorColumn =
            strlen(session->editorLines[session->editorLineCount - 1u]);
    else
        session->editorCursorColumn = 0;
    session->editorInsertMode = true;
    session->editorInsertAfter = after;
    return xvi_write_line(shell, after ? "-- 插入模式（当前行后，输入 . 结束）--"
                                      : "-- 插入模式（输入 . 结束）--");
}

static bool xvi_insert_line(XConsoleShell* shell, XConsoleShellSession* session,
                            const char* text, size_t length)
{
    size_t target;
    size_t i;
    if (!session || !text) return false;
    if (session->editorLineCount >= XCONSOLE_SHELL_EDITOR_MAX_LINES) {
        (void)xvi_write_line(shell, "vi: 编辑缓冲行数已满");
        return false;
    }
    target = session->editorCursorLine;
    if (session->editorInsertAfter) ++target;
    if (target > session->editorLineCount) target = session->editorLineCount;
    for (i = session->editorLineCount; i > target; --i)
        memcpy(session->editorLines[i], session->editorLines[i - 1u],
               sizeof(session->editorLines[0]));
    if (length >= XCONSOLE_SHELL_LINE_BUFFER_SIZE)
        length = XCONSOLE_SHELL_LINE_BUFFER_SIZE - 1u;
    memcpy(session->editorLines[target], text, length);
    session->editorLines[target][length] = '\0';
    ++session->editorLineCount;
    session->editorCursorLine = target;
    session->editorCursorColumn = length;
    /* 插入模式中下一行默认追加到刚插入的行之后，符合 vi 连续输入多行的习惯。 */
    session->editorInsertAfter = true;
    session->editorModified = true;
    return true;
}

static bool xvi_delete_line(XConsoleShell* shell, XConsoleShellSession* session,
                            size_t line)
{
    size_t i;
    (void)shell;
    if (line >= session->editorLineCount) return false;
    for (i = line; i + 1u < session->editorLineCount; ++i)
        memcpy(session->editorLines[i], session->editorLines[i + 1u],
               sizeof(session->editorLines[0]));
    --session->editorLineCount;
    memset(session->editorLines[session->editorLineCount], 0,
           sizeof(session->editorLines[0]));
    session->editorModified = true;
    return true;
}

static bool xvi_replace_line(XConsoleShell* shell, XConsoleShellSession* session,
                             size_t line, const char* text, size_t length)
{
    (void)shell;
    if (line >= session->editorLineCount) return false;
    if (length >= XCONSOLE_SHELL_LINE_BUFFER_SIZE)
        length = XCONSOLE_SHELL_LINE_BUFFER_SIZE - 1u;
    memcpy(session->editorLines[line], text, length);
    session->editorLines[line][length] = '\0';
    session->editorModified = true;
    return true;
}

static XConsoleResult xvi_finish(XConsoleShell* shell,
                                 XConsoleShellSession* session,
                                 bool save)
{
    XString* path;
    bool ok = true;
    if (save) {
        path = XString_create();
        if (!path || !xvi_make_path(session, session->editorPath, path)) {
            if (path) XString_delete_base(path);
            return XConsoleResult_Failed;
        }
        ok = xvi_save_lines(shell, session, path);
        XString_delete_base(path);
        if (!ok) {
            (void)xvi_write_line(shell, "vi: 保存失败");
            return XConsoleResult_Failed;
        }
        (void)xvi_write_line(shell, "vi: 已保存");
        session->editorModified = false;
    }
    session->suppressPrompt = false;
    xvi_clear(session);
    return XConsoleResult_Ok;
}

static XConsoleResult xvi_command(XConsoleShell* shell,
                                  XConsoleShellSession* session,
                                  const char* line, size_t length)
{
    char command[64];
    size_t commandLength = length;
    const char* rest;
    size_t restLength = 0;
    char* token;
    if (commandLength >= sizeof(command)) commandLength = sizeof(command) - 1u;
    memcpy(command, line, commandLength);
    command[commandLength] = '\0';
    token = command;
    while (*token == ' ' || *token == '\t') ++token;
    rest = token;
    while (*rest && *rest != ' ' && *rest != '\t') ++rest;
    restLength = length - (size_t)(rest - line);
    {
        size_t tokenLength = (size_t)(rest - token);
        while (restLength && (rest[0] == ' ' || rest[0] == '\t')) {
            ++rest;
            --restLength;
        }
        if (tokenLength == 0) {
            return xvi_print_lines(shell, session) ? XConsoleResult_MoreOutput
                                                   : XConsoleResult_IoError;
        }
        if (token[0] == ':') {
            if (tokenLength == 2 && memcmp(token, ":w", 2) == 0) {
                XString* path = XString_create();
                bool saved;
                if (!path || !xvi_make_path(session, session->editorPath, path)) {
                    if (path) XString_delete_base(path);
                    return XConsoleResult_Failed;
                }
                saved = xvi_save_lines(shell, session, path);
                XString_delete_base(path);
                if (!saved) return XConsoleResult_Failed;
                session->editorModified = false;
                (void)xvi_write_line(shell, "vi: 已保存");
                return XConsoleResult_MoreOutput;
            }
            if (tokenLength == 2 && memcmp(token, ":q", 2) == 0) {
                if (session->editorModified) {
                    (void)xvi_write_line(shell, "vi: 有未保存修改，使用 :wq 保存退出或 :q! 放弃");
                    return XConsoleResult_MoreOutput;
                }
                return xvi_finish(shell, session, false);
            }
            if (tokenLength == 3 && memcmp(token, ":wq", 3) == 0)
                return xvi_finish(shell, session, true);
            if (tokenLength == 3 && memcmp(token, ":x", 3) == 0)
                return xvi_finish(shell, session, true);
            if (tokenLength == 3 && memcmp(token, ":q!", 3) == 0)
                return xvi_finish(shell, session, false);
            (void)xvi_write_line(shell, "vi: 无效命令");
            return XConsoleResult_MoreOutput;
        }
        if (tokenLength == 1 && token[0] == 'p') {
            return xvi_print_lines(shell, session) ? XConsoleResult_MoreOutput
                                                   : XConsoleResult_IoError;
        }
        if (tokenLength == 1 && (token[0] == 'i' || token[0] == 'a')) {
            size_t line = session->editorCursorLine;
            if (restLength) {
                if (!xvi_parse_number(rest, &line)) {
                    (void)xvi_write_line(shell, "vi: 行号无效");
                    return XConsoleResult_MoreOutput;
                }
                if (line > 0) --line;
            }
            if (!xvi_enter_insert(shell, session, line, token[0] == 'a'))
                return XConsoleResult_IoError;
            return XConsoleResult_MoreOutput;
        }
        if (tokenLength == 1 && token[0] == 'd') {
            size_t line = session->editorCursorLine;
            if (restLength) {
                if (!xvi_parse_number(rest, &line)) {
                    (void)xvi_write_line(shell, "vi: 行号无效");
                    return XConsoleResult_MoreOutput;
                }
                if (line > 0) --line;
            }
            if (!xvi_delete_line(shell, session, line)) {
                (void)xvi_write_line(shell, "vi: 行号越界");
                return XConsoleResult_MoreOutput;
            }
            return xvi_print_lines(shell, session) ? XConsoleResult_MoreOutput
                                                   : XConsoleResult_IoError;
        }
        if (tokenLength == 1 && token[0] == 'r') {
            size_t line = session->editorCursorLine;
            if (!restLength) {
                (void)xvi_write_line(shell, "vi: 用法: r <行号> <新文本>");
                return XConsoleResult_MoreOutput;
            }
            if (!xvi_parse_number(rest, &line)) {
                (void)xvi_write_line(shell, "vi: 行号无效");
                return XConsoleResult_MoreOutput;
            }
            if (line > 0) --line;
            {
                const char* text = rest;
                size_t textLength = restLength;
                while (*text == ' ' || *text == '\t') { ++text; --textLength; }
                if (!xvi_replace_line(shell, session, line, text, textLength)) {
                    (void)xvi_write_line(shell, "vi: 行号越界");
                    return XConsoleResult_MoreOutput;
                }
            }
            return xvi_print_lines(shell, session) ? XConsoleResult_MoreOutput
                                                   : XConsoleResult_IoError;
        }
    }
    (void)xvi_write_line(shell, "vi: 无效命令");
    return XConsoleResult_MoreOutput;
}

static XConsoleResult xvi_insert(XConsoleShell* shell,
                                 XConsoleShellSession* session,
                                 const char* line, size_t length)
{
    if (length == 0 || (length == 1 && line[0] == '.')) {
        session->editorInsertMode = false;
        session->editorInsertAfter = false;
        (void)xvi_write_line(shell, "-- 命令模式 --");
        return XConsoleResult_MoreOutput;
    }
    if (!xvi_insert_line(shell, session, line, length))
        return XConsoleResult_MoreOutput;
    return xvi_print_lines(shell, session) ? XConsoleResult_MoreOutput
                                           : XConsoleResult_IoError;
}

static int xvi_open(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    XString* path;
    (void)userData;
    if (argc != 1 || !shell || !session) return XConsoleResult_InvalidArgument;
    path = XString_create();
    if (!path || !xvi_make_path(session, argv[0], path)) {
        if (path) XString_delete_base(path);
        return XConsoleResult_InvalidArgument;
    }
    xvi_clear(session);
    if (!xvi_load_lines(shell, session, path)) {
        XString_delete_base(path);
        (void)xvi_write_line(shell, "vi: 文件行数超过编辑上限");
        return XConsoleResult_Failed;
    }
    if (XString_size_base(path) >= sizeof(session->editorPath)) {
        XString_delete_base(path);
        return XConsoleResult_InvalidArgument;
    }
    memcpy(session->editorPath, XString_toUtf8(path), XString_size_base(path) + 1u);
    XString_delete_base(path);
    session->editorActive = true;
    session->editorInsertMode = false;
    session->editorModified = false;
    session->editorInsertAfter = false;
    session->editorCursorLine = 0;
    session->editorCursorColumn = 0;
    session->suppressPrompt = true;
    (void)xvi_write_line(shell, "-- vi 命令模式 --");
    if (!xvi_print_lines(shell, session)) return XConsoleResult_IoError;
    (void)xvi_write_line(shell,
        "输入 :w 保存、:q 退出、:wq 保存退出、:q! 放弃；i/a 进入插入，d 删除，r 替换");
    return XConsoleResult_MoreOutput;
}

const XConsoleCommand XConsoleShellVi_command = {
    "vi", NULL, "用 vi 风格行编辑修改文件", "vi <path>", 1, 1,
    XConsoleCommandFlag_Dangerous, xvi_open, NULL, 0, NULL
};
const XConsoleCommand XConsoleShellVim_command = {
    "vim", NULL, "用 vi 风格行编辑修改文件", "vim <path>", 1, 1,
    XConsoleCommandFlag_Dangerous, xvi_open, NULL, 0, NULL
};

bool XConsoleShellVi_isActive(const XConsoleShellSession* session)
{
    return session && session->editorActive;
}

void XConsoleShellVi_cancel(XConsoleShell* shell, XConsoleShellSession* session)
{
    if (!shell || !session) return;
    session->suppressPrompt = false;
    xvi_clear(session);
}

XConsoleResult XConsoleShellVi_submitLine(XConsoleShell* shell,
                                          XConsoleShellSession* session,
                                          const char* line, size_t length)
{
    if (!shell || !session || !XConsoleShellVi_isActive(session))
        return XConsoleResult_InvalidArgument;
    if (session->editorInsertMode)
        return xvi_insert(shell, session, line ? line : "", length ? length : 0u);
    return xvi_command(shell, session, line ? line : "", length ? length : 0u);
}

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_EDITOR_ON */
