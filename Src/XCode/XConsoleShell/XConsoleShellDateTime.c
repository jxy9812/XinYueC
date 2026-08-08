/**
 * @file XConsoleShellDateTime.c
 * @brief XConsoleShell Linux 风格 `date` 命令实现。
 * @details
 * 日期时间获取和字段读取全部通过 XDateTime、XDate、XTime 公共 API 完成，
 * 不直接调用 time、clock_gettime、GetSystemTime 等平台接口。格式化使用
 * 固定栈缓冲，避免命令执行过程中产生堆分配；输出统一经过 Shell I/O。
 */

#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_DATETIME_ON && XCONSOLE_SHELL_DATE_ON

#include "XConsoleShellDateTime.h"
#include "XDateTime.h"
#include "XDate.h"
#include "XTime.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 输出缓冲覆盖 Shell 默认单行容量，并为自定义格式保留结尾 NUL。 */
#define XCSDT_OUTPUT_CAPACITY 256u

static bool xcsdt_write_line(XConsoleShell* shell, const char* text)
{
    return shell && text && XConsoleShell_writeUtf8(shell, text) &&
           XConsoleShell_writeUtf8(shell, "\n");
}

static bool xcsdt_append(char* output, size_t capacity, size_t* length,
                         const char* text, size_t textLength)
{
    if (!output || !length || *length >= capacity ||
        textLength >= capacity - *length) return false;
    if (textLength) memcpy(output + *length, text, textLength);
    *length += textLength;
    output[*length] = '\0';
    return true;
}

static bool xcsdt_append_char(char* output, size_t capacity, size_t* length, char value)
{
    return xcsdt_append(output, capacity, length, &value, 1u);
}

static bool xcsdt_append_number(char* output, size_t capacity, size_t* length,
                                int64_t value, unsigned width, char pad)
{
    char number[32];
    int written;
    size_t digitLength;
    if (!output || !length || width >= sizeof(number)) return false;
    written = snprintf(number, sizeof(number), "%lld", (long long)value);
    if (written < 0 || (size_t)written >= sizeof(number)) return false;
    digitLength = (size_t)written;
    while ((unsigned)written < width) {
        if (!xcsdt_append_char(output, capacity, length, pad)) return false;
        ++written;
    }
    return xcsdt_append(output, capacity, length, number, digitLength);
}

/**
 * @brief 使用 Linux `date` 的常用百分号控制符格式化时间。
 * @param datetime 已取得的日期时间对象。
 * @param format 不包含开头加号的格式字符串。
 * @param output 输出缓冲。
 * @param capacity 输出缓冲容量。
 * @return 格式化成功返回 true；未知控制符或缓冲不足返回 false。
 */
static bool xcsdt_format(const XDateTime* datetime, const char* format,
                         char* output, size_t capacity)
{
    XDate date;
    XTime time;
    size_t length = 0;
    size_t i;
    if (!datetime || !format || !output || capacity == 0 ||
        !XDateTime_isValid(datetime)) return false;
    date = XDateTime_date(datetime);
    time = XDateTime_time(datetime);
    output[0] = '\0';
    for (i = 0; format[i]; ++i) {
        if (format[i] != '%') {
            if (!xcsdt_append_char(output, capacity, &length, format[i])) return false;
            continue;
        }
        ++i;
        if (!format[i]) return false;
        switch (format[i]) {
        case '%':
            if (!xcsdt_append_char(output, capacity, &length, '%')) return false;
            break;
        case 'Y':
            if (!xcsdt_append_number(output, capacity, &length, XDate_year(&date), 4u, '0')) return false;
            break;
        case 'y':
            if (!xcsdt_append_number(output, capacity, &length, XDate_year(&date) % 100, 2u, '0')) return false;
            break;
        case 'm':
            if (!xcsdt_append_number(output, capacity, &length, XDate_month(&date), 2u, '0')) return false;
            break;
        case 'd':
            if (!xcsdt_append_number(output, capacity, &length, XDate_day(&date), 2u, '0')) return false;
            break;
        case 'e':
            if (!xcsdt_append_number(output, capacity, &length, XDate_day(&date), 2u, ' ')) return false;
            break;
        case 'H':
            if (!xcsdt_append_number(output, capacity, &length, XTime_hour(&time), 2u, '0')) return false;
            break;
        case 'M':
            if (!xcsdt_append_number(output, capacity, &length, XTime_minute(&time), 2u, '0')) return false;
            break;
        case 'S':
            if (!xcsdt_append_number(output, capacity, &length, XTime_second(&time), 2u, '0')) return false;
            break;
        case 'f':
            if (!xcsdt_append_number(output, capacity, &length, XTime_msec(&time), 3u, '0')) return false;
            break;
        case 'F':
            if (!xcsdt_format(datetime, "%Y-%m-%d", output + length, capacity - length)) return false;
            length = strlen(output);
            break;
        case 'T':
            if (!xcsdt_format(datetime, "%H:%M:%S", output + length, capacity - length)) return false;
            length = strlen(output);
            break;
        case 's':
            if (!xcsdt_append_number(output, capacity, &length,
                                     XDateTime_toSecsSinceEpoch(datetime), 1u, '0')) return false;
            break;
        default:
            return false;
        }
    }
    return true;
}

static int xcsdt_date(XConsoleShell* shell, XConsoleShellSession* session,
                      int argc, const char* const* argv, void* userData)
{
    XDateTime datetime;
    const char* format = "%Y-%m-%d %H:%M:%S";
    char output[XCSDT_OUTPUT_CAPACITY];
    bool utc = false;
    bool explicitFormat = false;
    size_t i;
    (void)session;
    (void)userData;
    for (i = 0; i < (size_t)argc; ++i) {
        const char* argument = argv[i];
        if (!argument) return XConsoleResult_InvalidArgument;
        if (strcmp(argument, "-u") == 0 || strcmp(argument, "--utc") == 0) {
            utc = true;
        } else if (strcmp(argument, "-I") == 0 || strcmp(argument, "--iso-8601") == 0) {
            format = "%Y-%m-%d";
            explicitFormat = true;
        } else if (strcmp(argument, "--help") == 0) {
            return xcsdt_write_line(shell, "用法: date [-u|--utc] [-I|--iso-8601] [+FORMAT]")
                       ? XConsoleResult_Ok : XConsoleResult_IoError;
        } else if (argument[0] == '+' && argument[1]) {
            format = argument + 1;
            explicitFormat = true;
        } else {
            return XConsoleResult_InvalidArgument;
        }
    }
    if (utc && !explicitFormat) format = "%Y-%m-%d %H:%M:%S UTC";
    datetime = utc ? XDateTime_currentDateTimeUtc() : XDateTime_currentDateTime();
    if (!XDateTime_isValid(&datetime) || !xcsdt_format(&datetime, format,
                                                        output, sizeof(output))) {
        (void)xcsdt_write_line(shell, "date: 不支持的格式控制符或输出过长");
        return XConsoleResult_Failed;
    }
    return xcsdt_write_line(shell, output) ? XConsoleResult_Ok : XConsoleResult_IoError;
}

const XConsoleCommand XConsoleShellDateTime_command = {
    "date", NULL, "显示当前本地或 UTC 日期时间",
    "date [-u|--utc] [-I|--iso-8601] [+FORMAT]", 0, 3, 0,
    xcsdt_date, NULL, 0, NULL
};

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
          XCONSOLE_SHELL_DATETIME_ON && XCONSOLE_SHELL_DATE_ON */
