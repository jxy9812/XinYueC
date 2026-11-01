#ifndef XInfo_H
#define XInfo_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XDebug.h"
//流控制
#define XInfo_start_stream   do{XDebug* XInfo_ctx = XDebug_create_with_location_(__FILE__, __FUNCTION__, __LINE__);\
XInfo_ssetAutoSpace(true)
#define XInfo_ssetTarget(target,fd)                    XDebug_setTarget_(XInfo_ctx,target,fd)
#define XInfo_ssetAutoNewline(enable)                  XDebug_setAutoNewline_(XInfo_ctx,enable)
#define XInfo_ssetShowLocation(enable)                 XDebug_setShowLocation_(XInfo_ctx,enable)
#define XInfo_ssetAutoSpace(enable)                    XDebug_setAutoSpace_(XInfo_ctx,enable)
#define XInfo_swrite(data,len)                         XDebug_write_(XInfo_ctx,data,len)
#define XInfo_sputs(str)                               XDebug_puts_(XInfo_ctx,str)
#define XInfo_sputc(c)                                 XDebug_putc_(XInfo_ctx,c)
#define XInfo_sprintf(...)                              XDebug_printf_(XInfo_ctx, __VA_ARGS__)
#define XInfo_svprintf(format,args)                    XDebug_vprintf_(XInfo_ctx,format,args)
#define XInfo_sbool(value)                             XDebug_bool_(XInfo_ctx,value)
#define XInfo_schar(value)                             XDebug_char_(XInfo_ctx,value)
#define XInfo_sint8(value)                             XDebug_int8_(XInfo_ctx,value)
#define XInfo_suint8(value)                            XDebug_uint8_(XInfo_ctx,value)
#define XInfo_sint16(value)                            XDebug_int16_(XInfo_ctx,value)
#define XInfo_suint16(value)                           XDebug_uint16_(XInfo_ctx,value)
#define XInfo_sint32(value)                            XDebug_int32_(XInfo_ctx,value)
#define XInfo_suint32(value)                           XDebug_uint32_(XInfo_ctx,value)
#define XInfo_sint64(value)                            XDebug_int64_(XInfo_ctx,value)
#define XInfo_suint64(value)                           XDebug_uint64_(XInfo_ctx,value)
#define XInfo_sfloat(value)                            XDebug_float_(XInfo_ctx,value)
#define XInfo_sdouble(value)                           XDebug_double_(XInfo_ctx,value)
#define XInfo_sptr(value)                              XDebug_ptr_(XInfo_ctx,value)
#define XInfo_shex(value)                              XDebug_hex_(XInfo_ctx,value)
#define XInfo_shex8(value)                             XDebug_hex8_(XInfo_ctx,value)
#define XInfo_shex16(value)                            XDebug_hex16_(XInfo_ctx,value)
#define XInfo_shex32(value)                            XDebug_hex32_(XInfo_ctx,value)
#define XInfo_shex64(value)                            XDebug_hex64_(XInfo_ctx,value)
#define XInfo_sXString(value)                          XDebug_XString_(XInfo_ctx,value)
#define XInfo_sXVector(value,print_elem)               XDebug_XVector_(XInfo_ctx,value,print_elem)
#define XInfo_sXListSLinked(value,print_elem)          XDebug_XListSLinked_(XInfo_ctx,value,print_elem)
#define XInfo_sspace                                   XDebug_space_(XInfo_ctx)
#define XInfo_snospace                                 XDebug_nospace_(XInfo_ctx)
#define XInfo_snewline                                 XDebug_newline_(XInfo_ctx)
#define XInfo_sflush                                   XDebug_flush_(XInfo_ctx)
#define XInfo_sreset                                   XDebug_reset_(XInfo_ctx)
// 结束调试输出（自动刷新并添加换行）
#define XInfo_end_stream \
    if (XInfo_ctx) { \
        XDebug_flush_(XInfo_ctx); \
        XDebug_delete_(XInfo_ctx); \
    } \
} while(0)

#ifdef __cplusplus
}
#endif	
#endif  // XDEBUG_H
