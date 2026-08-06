/**
 * @file XConsoleShellParser.c
 * @brief XConsoleShell 固定缓冲 tokenizer 和命令树查找实现。
 * @details
 * 解析只使用 Shell 内嵌的行缓冲和 token 指针，不为每个命令分配内存。
 * 支持 ASCII 空白、单/双引号和反斜杠转义；不实现变量、通配符或命令替换。
 */

#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_PARSER_ON

#include <string.h>

static bool xcs_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

static bool xcs_name_equals(const char* name, const char* candidate)
{
    const char* begin;
    const char* end;
    if (!name || !candidate) return false;
    if (strcmp(name, candidate) == 0) return true;
#if XCONSOLE_SHELL_ALIAS_ON
    if (!strchr(candidate, ',')) return false;
    begin = candidate;
    while (*begin) {
        while (*begin == ' ' || *begin == '\t' || *begin == ',') ++begin;
        end = begin;
        while (*end && *end != ',') ++end;
        if ((size_t)(end - begin) == strlen(name) &&
            strncmp(begin, name, (size_t)(end - begin)) == 0) return true;
        begin = end;
    }
#else
    (void)begin;
    (void)end;
#endif
    return false;
}

static const XConsoleCommand* xcs_find_level(const XConsoleCommand* commands,
                                             size_t count,
                                             const char* const* tokens,
                                             size_t tokenCount,
                                             size_t* consumed)
{
    size_t i;
    if (!commands || !tokens || !tokenCount) return NULL;
    for (i = 0; i < count; ++i) {
        const XConsoleCommand* command = &commands[i];
        if (!command->name ||
            (!xcs_name_equals(tokens[0], command->name) &&
             !xcs_name_equals(tokens[0], command->aliases ? command->aliases : ""))) continue;
#if XCONSOLE_SHELL_SUBCOMMAND_ON
        if (command->subcommands && command->subcommandCount && tokenCount > 1) {
            size_t childConsumed = 0;
            const XConsoleCommand* child = xcs_find_level(command->subcommands,
                command->subcommandCount, tokens + 1, tokenCount - 1, &childConsumed);
            if (child) {
                if (consumed) *consumed = childConsumed + 1;
                return child;
            }
        }
#endif
        if (consumed) *consumed = 1;
        return command;
    }
    return NULL;
}

XConsoleResult XConsoleShellParser_tokenizeBuffer(const char* line, size_t length,
                                                  char* buffer, size_t bufferSize,
                                                  const char** arguments,
                                                  size_t maxArguments,
                                                  size_t* argumentCount)
{
    size_t read = 0;
    size_t write = 0;
    size_t tokenStart = 0;
    char quote = 0;
    bool inToken = false;
    bool escaping = false;
    if (argumentCount) *argumentCount = 0;
    if ((!line && length) || !buffer || !arguments || !maxArguments)
        return XConsoleResult_InvalidArgument;
    if (length >= bufferSize) return XConsoleResult_ResourceLimit;
    memmove(buffer, line ? line : "", length);
    buffer[length] = '\0';
    for (; read < length; ++read) {
        char c = buffer[read];
        if (escaping) {
            if (!inToken) {
                inToken = true;
                tokenStart = write;
            }
            buffer[write++] = c;
            escaping = false;
            continue;
        }
        if (c == '\\') {
            if (!inToken) {
                inToken = true;
                tokenStart = write;
            }
            escaping = true;
            continue;
        }
        if (quote) {
            if (c == quote) {
                quote = 0;
            } else {
                buffer[write++] = c;
            }
            continue;
        }
        if (c == '\'' || c == '"') {
            if (!inToken) {
                inToken = true;
                tokenStart = write;
            }
            quote = c;
            continue;
        }
        if (xcs_is_space(c)) {
            if (inToken) {
                buffer[write++] = '\0';
                if (argumentCount && *argumentCount >= maxArguments)
                    return XConsoleResult_ResourceLimit;
                if (!argumentCount) return XConsoleResult_InvalidArgument;
                arguments[(*argumentCount)++] = buffer + tokenStart;
                inToken = false;
            }
            continue;
        }
        if (!inToken) {
            inToken = true;
            tokenStart = write;
        }
        buffer[write++] = c;
    }
    if (escaping || quote) return XConsoleResult_InvalidSyntax;
    if (inToken) {
        buffer[write++] = '\0';
        if (argumentCount && *argumentCount >= maxArguments)
            return XConsoleResult_ResourceLimit;
        if (!argumentCount) return XConsoleResult_InvalidArgument;
        arguments[(*argumentCount)++] = buffer + tokenStart;
    }
    return XConsoleResult_Ok;
}

XConsoleResult XConsoleShellParser_tokenize(XConsoleShell* shell,
                                            const char* line, size_t length)
{
    XConsoleResult result;
    if (!shell) return XConsoleResult_InvalidArgument;
    result = XConsoleShellParser_tokenizeBuffer(line, length,
                                                 shell->m_lineBuffer,
                                                 sizeof(shell->m_lineBuffer),
                                                 shell->m_arguments,
                                                 XCONSOLE_SHELL_MAX_ARGUMENTS,
                                                 &shell->m_argumentCount);
    if (result == XConsoleResult_Ok || result == XConsoleResult_InvalidSyntax)
        shell->m_lineLength = length < sizeof(shell->m_lineBuffer) ? length : 0;
    return result;
}

const XConsoleCommand* XConsoleShellParser_find(const XConsoleShell* shell,
                                                const char* const* tokens,
                                                size_t tokenCount,
                                                size_t* consumed)
{
    size_t i;
    const XConsoleCommand* command;
    if (consumed) *consumed = 0;
    if (!shell || !tokens || tokenCount == 0) return NULL;
    for (i = 0; i < shell->m_commandCount; ++i) {
        command = xcs_find_level(shell->m_commands[i], 1, tokens, tokenCount, consumed);
        if (command) return command;
    }
    return NULL;
}

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_PARSER_ON */
