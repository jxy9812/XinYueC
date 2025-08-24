#ifndef XDEBUG_H
#define XDEBUG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include "XDataStructConfig.h"
#include "XMemory.h"
#include "XVector.h"
#include "XString.h"
#include "XListSLinked.h"

#if DEBUG_ON || defined(_DEBUG)

// 输出目标类型
typedef enum {
    XDEBUG_TARGET_STDOUT,   // 标准输出
    XDEBUG_TARGET_STDERR,   // 标准错误
    XDEBUG_TARGET_FILE,     // 文件
    XDEBUG_TARGET_BUFFER    // 内存缓冲区
} XDebugTarget;

// XDebug核心结构体
typedef struct XDebug {
    XVector buffer;             // 输出缓冲区（存储char）
    XDebugTarget target;        // 输出目标
    int file_fd;                // 文件描述符（当target为FILE时）
    bool auto_newline;          // 自动换行
    bool is_active;             // 是否激活
    bool show_location;         // 显示文件位置信息
    const char* file;           // 当前文件
    const char* function;       // 当前函数
    int line;                   // 当前行号
} XDebug;

// 创建XDebug实例（带位置信息）
#define XDebug_create() XDebug_create_with_location(__FILE__, __FUNCTION__, __LINE__)
XDebug* XDebug_create_with_location(const char* file, const char* function, int line);

// 销毁XDebug实例
void XDebug_destroy(XDebug* debug);

// 设置输出目标
XDebug* XDebug_setTarget(XDebug* debug, XDebugTarget target, int fd);

// 启用/禁用自动换行
XDebug* XDebug_setAutoNewline(XDebug* debug, bool enable);

// 启用/禁用位置信息显示
XDebug* XDebug_setShowLocation(XDebug* debug, bool enable);

// 基础输出函数
XDebug* XDebug_write(XDebug* debug, const char* data, size_t len);
XDebug* XDebug_puts(XDebug* debug, const char* str);
XDebug* XDebug_putc(XDebug* debug, char c);
XDebug* XDebug_printf(XDebug* debug, const char* format, ...);
XDebug* XDebug_vprintf(XDebug* debug, const char* format, va_list args);

// 标准类型输出
XDebug* XDebug_bool(XDebug* debug, bool value);
XDebug* XDebug_char(XDebug* debug, char value);
XDebug* XDebug_int8(XDebug* debug, int8_t value);
XDebug* XDebug_uint8(XDebug* debug, uint8_t value);
XDebug* XDebug_int16(XDebug* debug, int16_t value);
XDebug* XDebug_uint16(XDebug* debug, uint16_t value);
XDebug* XDebug_int32(XDebug* debug, int32_t value);
XDebug* XDebug_uint32(XDebug* debug, uint32_t value);
XDebug* XDebug_int64(XDebug* debug, int64_t value);
XDebug* XDebug_uint64(XDebug* debug, uint64_t value);
XDebug* XDebug_float(XDebug* debug, float value);
XDebug* XDebug_double(XDebug* debug, double value);
XDebug* XDebug_ptr(XDebug* debug, const void* ptr);
XDebug* XDebug_hex(XDebug* debug, uint64_t value);  // 十六进制输出
XDebug* XDebug_hex8(XDebug* debug, uint8_t value);  // 8位十六进制
XDebug* XDebug_hex16(XDebug* debug, uint16_t value); // 16位十六进制
XDebug* XDebug_hex32(XDebug* debug, uint32_t value); // 32位十六进制
XDebug* XDebug_hex64(XDebug* debug, uint64_t value); // 64位十六进制

// 自定义类型输出（支持库内容器）
XDebug* XDebug_XString(XDebug* debug, const XString* str);
XDebug* XDebug_XVector(XDebug* debug, const XVector* vec,
    void (*print_elem)(XDebug*, const void*));
XDebug* XDebug_XListSLinked(XDebug* debug, const XListSLinked* list,
    void (*print_elem)(XDebug*, const void*));

// 特殊操作
XDebug* XDebug_space(XDebug* debug);          // 输出空格
XDebug* XDebug_nospace(XDebug* debug);        // 取消后续空格
XDebug* XDebug_newline(XDebug* debug);        // 输出换行
XDebug* XDebug_flush(XDebug* debug);          // 刷新输出
XDebug* XDebug_reset(XDebug* debug);          // 重置缓冲区

// 结束调试输出（自动刷新并添加换行）
#define XDebug_end(debug) do { \
    if (debug) { \
        XDebug_flush(debug); \
        XDebug_destroy(debug); \
    } \
} while(0)

// 简化调用宏（模拟QDebug的流操作）
#define xdebug() XDebug_create()

// 条件调试宏
#define xdebug_if(condition) \
    (condition) ? XDebug_create() : NULL

// 带位置信息的调试宏（模拟QDebug的调试上下文）
#define xdebug_loc() \
    XDebug_create_with_location(__FILE__, __FUNCTION__, __LINE__)

#else  // DEBUG_ON || _DEBUG

// 非调试模式下禁用所有功能
typedef struct XDebug XDebug;

#define XDebug_create() NULL
#define XDebug_create_with_location(...) NULL
#define XDebug_destroy(debug) do {} while(0)
#define XDebug_setTarget(debug, ...) (debug)
#define XDebug_setAutoNewline(debug, ...) (debug)
#define XDebug_setShowLocation(debug, ...) (debug)
#define XDebug_write(debug, ...) (debug)
#define XDebug_puts(debug, ...) (debug)
#define XDebug_putc(debug, ...) (debug)
#define XDebug_printf(debug, ...) (debug)
#define XDebug_vprintf(debug, ...) (debug)
#define XDebug_bool(debug, ...) (debug)
#define XDebug_char(debug, ...) (debug)
#define XDebug_int8(debug, ...) (debug)
#define XDebug_uint8(debug, ...) (debug)
#define XDebug_int16(debug, ...) (debug)
#define XDebug_uint16(debug, ...) (debug)
#define XDebug_int32(debug, ...) (debug)
#define XDebug_uint32(debug, ...) (debug)
#define XDebug_int64(debug, ...) (debug)
#define XDebug_uint64(debug, ...) (debug)
#define XDebug_float(debug, ...) (debug)
#define XDebug_double(debug, ...) (debug)
#define XDebug_ptr(debug, ...) (debug)
#define XDebug_hex(debug, ...) (debug)
#define XDebug_hex8(debug, ...) (debug)
#define XDebug_hex16(debug, ...) (debug)
#define XDebug_hex32(debug, ...) (debug)
#define XDebug_hex64(debug, ...) (debug)
#define XDebug_XString(debug, ...) (debug)
#define XDebug_XVector(debug, ...) (debug)
#define XDebug_XListSLinked(debug, ...) (debug)
#define XDebug_space(debug) (debug)
#define XDebug_nospace(debug) (debug)
#define XDebug_newline(debug) (debug)
#define XDebug_flush(debug) (debug)
#define XDebug_reset(debug) (debug)
#define XDebug_end(debug) do {} while(0)
#define xdebug() NULL
#define xdebug_if(condition) NULL
#define xdebug_loc() NULL

#endif  // DEBUG_ON || _DEBUG

#endif  // XDEBUG_H