#ifndef XDEBUG_H
#define XDEBUG_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include "XDataStructConfig.h"
#include "XMemory.h"
#include "XVector.h"
#include "XString.h"
#include "XListSLinked.h"

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
    bool auto_space;            // 自动空格区分变量（新增）
    const char* file;           // 当前文件
    const char* function;       // 当前函数
    int line;                   // 当前行号
} XDebug;

// 创建XDebug实例（带位置信息）
#define XDebug_create() XDebug_create_with_location_(__FILE__, __FUNCTION__, __LINE__)
XDebug* XDebug_create_with_location_(const char* file, const char* function, int line);

// 销毁XDebug实例
void XDebug_delete_(XDebug* debug);

// 设置输出目标
XDebug* XDebug_setTarget_(XDebug* debug, XDebugTarget target, int fd);

// 启用/禁用自动换行
XDebug* XDebug_setAutoNewline_(XDebug* debug, bool enable);

// 启用/禁用位置信息显示
XDebug* XDebug_setShowLocation_(XDebug* debug, bool enable);

// 新增：设置是否自动添加空格
XDebug* XDebug_setAutoSpace_(XDebug* debug, bool enable);

// 基础输出函数
XDebug* XDebug_write_(XDebug* debug, const char* data, size_t len);
XDebug* XDebug_puts_(XDebug* debug, const char* str);
XDebug* XDebug_putc_(XDebug* debug, char c);
XDebug* XDebug_printf_(XDebug* debug, const char* format, ...);
XDebug* XDebug_vprintf_(XDebug* debug, const char* format, va_list args);

// 标准类型输出
XDebug* XDebug_bool_(XDebug* debug, bool value);
XDebug* XDebug_char_(XDebug* debug, char value);
XDebug* XDebug_int8_(XDebug* debug, int8_t value);
XDebug* XDebug_uint8_(XDebug* debug, uint8_t value);
XDebug* XDebug_int16_(XDebug* debug, int16_t value);
XDebug* XDebug_uint16_(XDebug* debug, uint16_t value);
XDebug* XDebug_int32_(XDebug* debug, int32_t value);
XDebug* XDebug_uint32_(XDebug* debug, uint32_t value);
XDebug* XDebug_int64_(XDebug* debug, int64_t value);
XDebug* XDebug_uint64_(XDebug* debug, uint64_t value);
XDebug* XDebug_float_(XDebug* debug, float value);
XDebug* XDebug_double_(XDebug* debug, double value);
XDebug* XDebug_ptr_(XDebug* debug, const void* ptr);
XDebug* XDebug_hex_(XDebug* debug, uint64_t value);  // 十六进制输出
XDebug* XDebug_hex8_(XDebug* debug, uint8_t value);  // 8位十六进制
XDebug* XDebug_hex16_(XDebug* debug, uint16_t value); // 16位十六进制
XDebug* XDebug_hex32_(XDebug* debug, uint32_t value); // 32位十六进制
XDebug* XDebug_hex64_(XDebug* debug, uint64_t value); // 64位十六进制

// 自定义类型输出（支持库内容器）
XDebug* XDebug_XString_(XDebug* debug, const XString* str);
XDebug* XDebug_XVector_(XDebug* debug, const XVector* vec,
    void (*print_elem)(XDebug*, const void*));
XDebug* XDebug_XListSLinked_(XDebug* debug, const XListSLinked* list,
    void (*print_elem)(XDebug*, const void*));

// 特殊操作
XDebug* XDebug_space_(XDebug* debug);          // 输出空格
XDebug* XDebug_nospace_(XDebug* debug);        // 取消后续空格
XDebug* XDebug_newline_(XDebug* debug);        // 输出换行
XDebug* XDebug_flush_(XDebug* debug);          // 刷新输出
XDebug* XDebug_reset_(XDebug* debug);          // 重置缓冲区



#if DEBUG_ON || defined(_DEBUG)

//流控制
#define XDebug_start_stream   do{XDebug* XDebug_ctx = XDebug_create();XDebug_ssetShowLocation(true);XDebug_ssetAutoSpace(true)
#define XDebug_ssetTarget(target,fd)                    XDebug_setTarget_(XDebug_ctx,target,fd)
#define XDebug_ssetAutoNewline(enable)                  XDebug_setAutoNewline_(XDebug_ctx,enable)
#define XDebug_ssetShowLocation(enable)                 XDebug_setShowLocation_(XDebug_ctx,enable)
#define XDebug_ssetAutoSpace(enable)                    XDebug_setAutoSpace_(XDebug_ctx,enable)
#define XDebug_swrite(data,len)                         XDebug_write_(XDebug_ctx,data,len)
#define XDebug_sputs(str)                               XDebug_puts_(XDebug_ctx,str)
#define XDebug_sputc(c)                                 XDebug_putc_(XDebug_ctx,c)
#define XDebug_sprintf(format,...)                      XDebug_printf_(XDebug_ctx,format,__VA_ARGS__)
#define XDebug_svprintf(format,args)                    XDebug_vprintf_(XDebug_ctx,format,args)
#define XDebug_sbool(value)                             XDebug_bool_(XDebug_ctx,value)
#define XDebug_schar(value)                             XDebug_char_(XDebug_ctx,value)
#define XDebug_sint8(value)                             XDebug_int8_(XDebug_ctx,value)
#define XDebug_suint8(value)                            XDebug_uint8_(XDebug_ctx,value)
#define XDebug_sint16(value)                            XDebug_int16_(XDebug_ctx,value)
#define XDebug_suint16(value)                           XDebug_uint16_(XDebug_ctx,value)
#define XDebug_sint32(value)                            XDebug_int32_(XDebug_ctx,value)
#define XDebug_suint32(value)                           XDebug_uint32_(XDebug_ctx,value)
#define XDebug_sint64(value)                            XDebug_int64_(XDebug_ctx,value)
#define XDebug_suint64(value)                           XDebug_uint64_(XDebug_ctx,value)
#define XDebug_sfloat(value)                            XDebug_float_(XDebug_ctx,value)
#define XDebug_sdouble(value)                           XDebug_double_(XDebug_ctx,value)
#define XDebug_sptr(value)                              XDebug_ptr_(XDebug_ctx,value)
#define XDebug_shex(value)                              XDebug_hex_(XDebug_ctx,value)
#define XDebug_shex8(value)                             XDebug_hex8_(XDebug_ctx,value)
#define XDebug_shex16(value)                            XDebug_hex16_(XDebug_ctx,value)
#define XDebug_shex32(value)                            XDebug_hex32_(XDebug_ctx,value)
#define XDebug_shex64(value)                            XDebug_hex64_(XDebug_ctx,value)
#define XDebug_sXString(value)                          XDebug_XString_(XDebug_ctx,value)
#define XDebug_sXVector(value,print_elem)               XDebug_XVector_(XDebug_ctx,value,print_elem)
#define XDebug_sXListSLinked(value,print_elem)          XDebug_XListSLinked_(XDebug_ctx,value,print_elem)
#define XDebug_sspace                                   XDebug_space_(XDebug_ctx)
#define XDebug_snospace                                 XDebug_nospace_(XDebug_ctx)
#define XDebug_snewline                                 XDebug_newline_(XDebug_ctx)
#define XDebug_sflush                                   XDebug_flush_(XDebug_ctx)
#define XDebug_sreset                                   XDebug_reset_(XDebug_ctx)
// 结束调试输出（自动刷新并添加换行）
#define XDebug_end_stream \
    if (XDebug_ctx) { \
        XDebug_flush_(XDebug_ctx); \
        XDebug_delete_(XDebug_ctx); \
    } \
} while(0)



// 带位置信息的调试宏（模拟QDebug的调试上下文）
#define xdebug_loc() \
    XDebug_create_with_location_(__FILE__, __FUNCTION__, __LINE__)

#define XDebug_create_with_location             XDebug_create_with_location_
#define XDebug_delete                           XDebug_delete_
#define XDebug_setTarget                        XDebug_setTarget_
#define XDebug_setAutoNewline                   XDebug_setAutoNewline_
#define XDebug_setShowLocation                  XDebug_setShowLocation_
#define XDebug_setAutoSpace                     XDebug_setAutoSpace_
#define XDebug_write                            XDebug_write_
#define XDebug_puts                             XDebug_puts_
#define XDebug_putc                             XDebug_putc_
#define XDebug_printf                           XDebug_printf_
#define XDebug_vprintf                          XDebug_vprintf_
#define XDebug_bool                             XDebug_bool_
#define XDebug_char                             XDebug_char_
#define XDebug_int8                             XDebug_int8_
#define XDebug_uint8                            XDebug_uint8_
#define XDebug_int16                            XDebug_int16_
#define XDebug_uint16                           XDebug_uint16_
#define XDebug_int32                            XDebug_int32_
#define XDebug_uint32                           XDebug_uint32_
#define XDebug_int64                            XDebug_int64_
#define XDebug_uint64                           XDebug_uint64_
#define XDebug_float                            XDebug_float_
#define XDebug_double                           XDebug_double_
#define XDebug_ptr                              XDebug_ptr_
#define XDebug_hex                              XDebug_hex_
#define XDebug_hex8                             XDebug_hex8_
#define XDebug_hex16                            XDebug_hex16_
#define XDebug_hex32                            XDebug_hex32_
#define XDebug_hex64                            XDebug_hex64_
#define XDebug_XString                          XDebug_XString_
#define XDebug_XVector                          XDebug_XVector_
#define XDebug_XListSLinked                     XDebug_XListSLinked_
#define XDebug_space                            XDebug_space_
#define XDebug_nospace                          XDebug_nospace_
#define XDebug_newline                          XDebug_newline_
#define XDebug_flush                            XDebug_flush_
#define XDebug_reset                            XDebug_reset_
// 结束调试输出（自动刷新并添加换行）
#define XDebug_end(XDebug_ctx) \
    if (XDebug_ctx) { \
        XDebug_flush_(XDebug_ctx); \
        XDebug_delete_(XDebug_ctx); \
    } \
while(0)
#else  // DEBUG_ON || _DEBUG

// 非调试模式下禁用所有功能
typedef struct XDebug XDebug;

#define XDebug_create() NULL
#define XDebug_create_with_location(...) NULL
#define XDebug_delete(debug) do {} while(0)
#define XDebug_setTarget(debug, ...) (debug)
#define XDebug_setAutoNewline(debug, ...) (debug)
#define XDebug_setShowLocation(debug, ...) (debug)
#define XDebug_setAutoSpace(debug, ...) (debug)
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

//流控制
#define XDebug_start_stream  NULL
#define XDebug_ssetTarget(...)                              NULL
#define XDebug_ssetAutoNewline(...)                         NULL
#define XDebug_ssetShowLocation(...)                        NULL
#define XDebug_ssetAutoSpace(...)                           NULL
#define XDebug_swrite(...)                                  NULL
#define XDebug_sputs(...)                                   NULL
#define XDebug_sputc(...)                                   NULL
#define XDebug_sprintf(...)                     NULL
#define XDebug_svprintf(...)                     NULL
#define XDebug_sbool(...)                     NULL
#define XDebug_schar(...)                     NULL
#define XDebug_sint8(...)                     NULL
#define XDebug_suint8(...)                     NULL
#define XDebug_sint16(...)                     NULL
#define XDebug_suint16(...)                     NULL
#define XDebug_sint32(...)                     NULL
#define XDebug_suint32(...)                     NULL
#define XDebug_sint64(...)                     NULL
#define XDebug_suint64(...)                     NULL
#define XDebug_sfloat(...)                     NULL
#define XDebug_sdouble(...)                     NULL
#define XDebug_sptr(...)                     NULL
#define XDebug_shex(...)                     NULL
#define XDebug_shex8(...)                     NULL
#define XDebug_shex16(...)                     NULL
#define XDebug_shex32(...)                     NULL
#define XDebug_shex64(...)                     NULL
#define XDebug_sXString(...)                     NULL
#define XDebug_sXVector(...)                     NULL
#define XDebug_sXListSLinked(...)                     NULL
#define XDebug_sspace                    NULL
#define XDebug_snospace                     NULL
#define XDebug_snewline                     NULL
#define XDebug_sflush                   NULL
#define XDebug_sreset                    NULL
#define XDebug_end_stream                     NULL

#endif  // DEBUG_ON || _DEBUG


#ifdef __cplusplus
}
#endif	
#endif  // XDEBUG_H