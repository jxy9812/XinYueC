/* zconf.h -- zlib压缩库的配置文件
 * 版权所有 (C) 1995-2016 Jean-loup Gailly、Mark Adler
 * 有关发布和使用的条件，请参见 zlib.h 中的版权声明
 */

 /* @(#) $Id$ */

#ifndef ZCONF_H
#define ZCONF_H

/* RT-Thread 系统配置（嵌入式实时操作系统）*/
//#include <rtconfig.h>  // 引入RT-Thread的系统配置文件（按需启用）
#ifdef RT_USING_LIBC  // 若RT-Thread启用了标准C库
#ifndef STDC       // 若未定义STDC（标准C标识）
#define STDC   // 定义STDC，标识遵循标准C语法
#endif
#define Z_HAVE_STDARG_H  // 标识支持stdarg.h（可变参数头文件）
#endif

#ifdef RT_USING_DFS  // 若RT-Thread启用了DFS（设备文件系统）
#define Z_HAVE_UNISTD_H  // 标识支持unistd.h（POSIX系统调用头文件）
#endif

/*
 * 若你确实需要为所有类型和库函数添加唯一前缀，
 * 请使用 -DZ_PREFIX 编译。“标准”zlib编译时不应添加此宏。
 * 比编译时加 -DZ_PREFIX 更好的方式是通过 configure 工具，
 * 执行 "./configure --zprefix" 在 zconf.h 中永久设置该宏。
 */
#ifdef Z_PREFIX     /* 可通过 ./configure 设为 #if 1 启用 */
#define Z_PREFIX_SET  // 标记已启用前缀模式

 /* 所有链接符号与初始化宏（添加z_前缀避免命名冲突）*/
#define _dist_code            z__dist_code
#define _length_code          z__length_code
#define _tr_align             z__tr_align
#define _tr_flush_bits        z__tr_flush_bits
#define _tr_flush_block       z__tr_flush_block
#define _tr_init              z__tr_init
#define _tr_stored_block      z__tr_stored_block
#define _tr_tally             z__tr_tally
#define adler32               z_adler32          // Adler32校验函数
#define adler32_combine       z_adler32_combine  // 合并Adler32校验值
#define adler32_combine64     z_adler32_combine64// 64位版本合并Adler32校验值
#define adler32_z             z_adler32_z        // 带z_stream参数的Adler32校验
#ifndef Z_SOLO  // 若未启用独立模式（Z_SOLO会禁用部分功能）
#define compress              z_compress        // 基础压缩函数
#define compress2             z_compress2       // 可设置压缩级别的压缩函数
#define compressBound         z_compressBound   // 计算压缩最大可能长度
#endif
#define crc32                 z_crc32            // CRC32校验函数
#define crc32_combine         z_crc32_combine    // 合并CRC32校验值
#define crc32_combine64       z_crc32_combine64  // 64位版本合并CRC32校验值
#define crc32_z               z_crc32_z          // 带z_stream参数的CRC32校验
#define deflate               z_deflate          // deflate压缩核心函数
#define deflateBound          z_deflateBound     // 计算deflate压缩最大长度
#define deflateCopy           z_deflateCopy      // 复制压缩器状态
#define deflateEnd            z_deflateEnd       // 释放压缩器资源
#define deflateGetDictionary  z_deflateGetDictionary  // 获取压缩器使用的字典
#define deflateInit           z_deflateInit      // 初始化压缩器（默认参数）
#define deflateInit2          z_deflateInit2     // 自定义参数初始化压缩器
#define deflateInit2_         z_deflateInit2_    // 支持自定义内存分配的初始化
#define deflateInit_          z_deflateInit_     // 支持自定义内存分配的默认初始化
#define deflateParams         z_deflateParams    // 动态调整压缩参数
#define deflatePending        z_deflatePending   // 获取待输出的压缩数据长度
#define deflatePrime          z_deflatePrime     // 向压缩器输入初始位流
#define deflateReset          z_deflateReset     // 重置压缩器状态（保留字典）
#define deflateResetKeep      z_deflateResetKeep // 重置压缩器（保留更多状态）
#define deflateSetDictionary  z_deflateSetDictionary  // 为压缩器设置字典
#define deflateSetHeader      z_deflateSetHeader // 设置gzip格式的文件头
#define deflateTune           z_deflateTune      // 微调压缩器性能参数
#define deflate_copyright     z_deflate_copyright// 压缩模块版权信息
#define get_crc_table         z_get_crc_table    // 获取CRC32校验表
#ifndef Z_SOLO
#define gz_error              z_gz_error         // gz文件操作错误码处理
#define gz_intmax             z_gz_intmax        // gz模块的最大整数类型
#define gz_strwinerror        z_gz_strwinerror   // Windows系统gz错误信息转换
#define gzbuffer              z_gzbuffer         // 设置gz文件的缓冲区大小
#define gzclearerr            z_gzclearerr       // 清除gz文件的错误状态
#define gzclose               z_gzclose          // 关闭gz文件
#define gzclose_r             z_gzclose_r        // 关闭只读模式的gz文件
#define gzclose_w             z_gzclose_w        // 关闭只写模式的gz文件
#define gzdirect              z_gzdirect         // 检查gz文件是否直接读写（无压缩）
#define gzdopen               z_gzdopen          // 关联文件描述符与gz文件
#define gzeof                 z_gzeof            // 判断gz文件是否到末尾
#define gzerror               z_gzerror          // 获取gz文件错误信息
#define gzflush               z_gzflush          // 刷新gz文件缓冲区
#define gzfread               z_gzfread          // gz文件二进制读取
#define gzfwrite              z_gzfwrite         // gz文件二进制写入
#define gzgetc                z_gzgetc           // 从gz文件读一个字符
#define gzgetc_               z_gzgetc_          // 内部使用的字符读取函数
#define gzgets                z_gzgets           // 从gz文件读一行字符串
#define gzoffset              z_gzoffset         // 获取gz文件当前偏移量（32位）
#define gzoffset64            z_gzoffset64       // 获取gz文件当前偏移量（64位）
#define gzopen                z_gzopen           // 打开gz文件（32位偏移）
#define gzopen64              z_gzopen64         // 打开gz文件（64位偏移）
#ifdef _WIN32  // Windows系统特化
#define gzopen_w              z_gzopen_w        // Windows下宽字符路径打开gz文件
#endif
#define gzprintf              z_gzprintf         // 格式化写入gz文件
#define gzputc                z_gzputc           // 向gz文件写一个字符
#define gzputs                z_gzputs           // 向gz文件写字符串
#define gzread                z_gzread           // 从gz文件读取数据
#define gzrewind              z_gzrewind         // 重置gz文件指针到开头
#define gzseek                z_gzseek           // 移动gz文件指针（32位）
#define gzseek64              z_gzseek64         // 移动gz文件指针（64位）
#define gzsetparams           z_gzsetparams      // 设置gz文件的压缩参数
#define gztell                z_gztell           // 获取gz文件当前指针位置（32位）
#define gztell64              z_gztell64         // 获取gz文件当前指针位置（64位）
#define gzungetc              z_gzungetc         // 把字符放回gz文件输入缓冲区
#define gzvprintf             z_gzvprintf        // 可变参数格式化写入gz文件
#define gzwrite               z_gzwrite          // 向gz文件写入数据
#endif
#define inflate               z_inflate          // inflate解压缩核心函数
#define inflateBack           z_inflateBack      // 带回调的解压缩函数
#define inflateBackEnd        z_inflateBackEnd   // 释放inflateBack资源
#define inflateBackInit       z_inflateBackInit  // 初始化inflateBack解压缩器
#define inflateBackInit_      z_inflateBackInit_ // 支持自定义内存分配的初始化
#define inflateCodesUsed      z_inflateCodesUsed // 获取解压缩使用的编码数量
#define inflateCopy           z_inflateCopy      // 复制解压缩器状态
#define inflateEnd            z_inflateEnd       // 释放解压缩器资源
#define inflateGetDictionary  z_inflateGetDictionary  // 获取解压缩器使用的字典
#define inflateGetHeader      z_inflateGetHeader // 获取gzip文件头信息
#define inflateInit           z_inflateInit      // 初始化解压缩器（默认参数）
#define inflateInit2          z_inflateInit2     // 自定义参数初始化解压缩器
#define inflateInit2_         z_inflateInit2_    // 支持自定义内存分配的初始化
#define inflateInit_          z_inflateInit_     // 支持自定义内存分配的默认初始化
#define inflateMark           z_inflateMark      // 标记解压缩器当前状态
#define inflatePrime          z_inflatePrime     // 向解压缩器输入初始位流
#define inflateReset          z_inflateReset     // 重置解压缩器状态
#define inflateReset2         z_inflateReset2    // 按指定窗口大小重置解压缩器
#define inflateResetKeep      z_inflateResetKeep // 重置解压缩器（保留字典）
#define inflateSetDictionary  z_inflateSetDictionary  // 为解压缩器设置字典
#define inflateSync           z_inflateSync      // 同步解压缩器（跳过错误数据）
#define inflateSyncPoint      z_inflateSyncPoint // 检查解压缩同步点
#define inflateUndermine      z_inflateUndermine // 内部调试用函数
#define inflateValidate       z_inflateValidate  // 验证解压缩数据完整性
#define inflate_copyright     z_inflate_copyright// 解压缩模块版权信息
#define inflate_fast          z_inflate_fast     // 快速解压缩函数
#define inflate_table         z_inflate_table    // 构建解压缩编码表
#ifndef Z_SOLO
#define uncompress            z_uncompress       // 基础解压缩函数
#define uncompress2           z_uncompress2      // 带输出长度检查的解压缩函数
#endif
#define zError                z_zError           // 将zlib错误码转为字符串
#ifndef Z_SOLO
#define zcalloc               z_zcalloc          // zlib内部内存分配函数
#define zcfree                z_zcfree           // zlib内部内存释放函数
#endif
#define zlibCompileFlags      z_zlibCompileFlags // 获取zlib编译时的配置标志
#define zlibVersion           z_zlibVersion      // 获取zlib版本号

/* zlib.h 和 zconf.h 中所有的类型定义（添加z_前缀）*/
#define Byte                  z_Byte             // 8位无符号字节类型
#define Bytef                 z_Bytef            // 远指针版Byte（兼容16位系统）
#define alloc_func            z_alloc_func       // 内存分配函数类型
#define charf                 z_charf            // 远指针版char
#define free_func             z_free_func        // 内存释放函数类型
#ifndef Z_SOLO
#define gzFile                z_gzFile           // gz文件句柄类型
#endif
#define gz_header             z_gz_header        // gzip文件头结构体类型
#define gz_headerp            z_gz_headerp       // gz_header的指针类型
#define in_func               z_in_func          // 输入回调函数类型（inflateBack）
#define intf                  z_intf             // 远指针版int
#define out_func              z_out_func         // 输出回调函数类型（inflateBack）
#define uInt                  z_uInt             // 无符号整数类型（至少16位）
#define uIntf                 z_uIntf            // 远指针版uInt
#define uLong                 z_uLong            // 无符号长整数类型（至少32位）
#define uLongf                z_uLongf           // 远指针版uLong
#define voidp                 z_voidp            // void指针（用于输出参数）
#define voidpc                z_voidpc           // const void指针（用于输入参数）
#define voidpf                z_voidpf           // 远指针版void指针

/* zlib.h 和 zconf.h 中所有的结构体定义（添加z_前缀）*/
#define gz_header_s           z_gz_header_s      // gz_header的结构体实现
#define internal_state        z_internal_state   // 压缩/解压缩内部状态结构体
#endif

/* 操作系统宏定义兼容处理（统一不同编译器的系统标识）*/
#if defined(__MSDOS__) && !defined(MSDOS)
#define MSDOS  // 标记MS-DOS系统
#endif
#if (defined(OS_2) || defined(__OS2__)) && !defined(OS2)
#define OS2    // 标记OS/2系统
#endif
#if defined(_WINDOWS) && !defined(WINDOWS)
#define WINDOWS // 标记Windows系统（旧版）
#endif
#if defined(_WIN32) || defined(_WIN32_WCE) || defined(__WIN32__)
#ifndef WIN32
#define WIN32  // 标记32位/64位Windows系统
#endif
#endif
/* 16位系统兼容处理（MSDOS/OS2/旧Windows）*/
#if (defined(MSDOS) || defined(OS2) || defined(WINDOWS)) && !defined(WIN32)
#if !defined(__GNUC__) && !defined(__FLAT__) && !defined(__386__)
#ifndef SYS16BIT
#define SYS16BIT  // 标记16位系统（需特殊内存处理）
#endif
#endif
#endif

/*
 * 若内存分配函数一次最多只能分配64KB（16位整数系统），请用 -DMAXSEG_64K 编译。
 */
#ifdef SYS16BIT
#define MAXSEG_64K  // 启用64KB内存分段限制
#endif
#ifdef MSDOS
#define UNALIGNED_OK  // MSDOS系统支持非对齐内存访问
#endif

 /* 标准C版本标识处理 */
#ifdef __STDC_VERSION__
#ifndef STDC
#define STDC  // 标记支持标准C
#endif
#if __STDC_VERSION__ >= 199901L  // 若支持C99标准（1999年发布）
#ifndef STDC99
#define STDC99  // 标记支持C99标准
#endif
#endif
#endif
#if !defined(STDC) && (defined(__STDC__) || defined(__cplusplus))
#define STDC  // C++编译器或显式声明__STDC__时，标记支持标准C
#endif
#if !defined(STDC) && (defined(__GNUC__) || defined(__BORLANDC__))
#define STDC  // GNU编译器或Borland编译器默认支持标准C
#endif
#if !defined(STDC) && (defined(MSDOS) || defined(WINDOWS) || defined(WIN32))
#define STDC  // Windows/MSDOS系统默认标记支持标准C
#endif
#if !defined(STDC) && (defined(OS2) || defined(__HOS_AIX__))
#define STDC  // OS2或AIX系统默认标记支持标准C
#endif

#if defined(__OS400__) && !defined(STDC)    /* IBM iSeries（原AS/400）系统 */
#define STDC
#endif

/* 无标准C支持时的兼容处理（为const关键字定义空宏）*/
#ifndef STDC
#ifndef const /* Mac系统不支持 !defined(STDC) && !defined(const) 写法，需单独判断 */
#define const       /* 注意：此方案仅为兼容，可能存在隐患，建议使用标准C编译器 */
#endif
#endif

/* z_const 宏定义（控制是否添加const修饰）*/
#ifdef ZLIB_CONST && !defined(z_const)
#define z_const const  // 启用ZLIB_CONST时，z_const等价于const
#else
#define z_const        // 禁用时，z_const为空（兼容旧代码）
#endif

/* z_size_t 类型定义（zlib内部使用的大小类型）*/
#ifdef Z_SOLO  // 独立模式（仅保留核心压缩/解压缩功能）
typedef unsigned long z_size_t;  // 用unsigned long作为大小类型
#else
#define z_longlong long long  // 临时定义64位长整数类型
#if defined(NO_SIZE_T)  // 若系统无size_t类型
typedef unsigned NO_SIZE_T z_size_t;
#elif defined(STDC)     // 标准C系统（包含stddef.h）
#include <stddef.h>
typedef size_t z_size_t;  // 用标准size_t作为大小类型
#else                   // 非标准C系统
typedef unsigned long z_size_t;
#endif
#undef z_longlong  // 释放临时定义
#endif

/* deflateInit2 函数中 memLevel（内存级别）的最大值 */
#ifndef MAX_MEM_LEVEL
#ifdef MAXSEG_64K  // 16位系统（64KB内存限制）
#define MAX_MEM_LEVEL 8  // 内存级别最大值设为8
#else               // 32/64位系统
#define MAX_MEM_LEVEL 6  // 默认内存级别最大值设为6
#endif
#endif

/*
 * deflateInit2 和 inflateInit2 函数中 windowBits（滑动窗口大小）的最大值
 * 警告：减小窗口大小会导致当前程序可能无法解压由标准gzip生成的.gz文件，
 * 但当前程序生成的压缩文件仍可被标准gzip解压
 */
#ifndef MAX_WBITS
 // 桌面系统（Windows/Linux/macOS）使用默认参数（32K窗口，压缩率优先）
#if defined(_WIN32) || defined(_WIN64) || defined(__linux__) || defined(__APPLE__)
#define MAX_WBITS   15  /* 默认值，对应32K字节的LZ77滑动窗口 */
// 嵌入式系统使用优化参数（1K窗口，内存优先）
#else
#define MAX_WBITS   9   /* 适配嵌入式设备，对应1K字节的LZ77滑动窗口，节省RAM */
#endif
#endif

/*
 * 压缩过程中使用的内存级别（1~9）
 * 数值越大，使用内存越多，压缩速度可能越快，压缩率可能越高
 */
#ifndef MEM_LEVEL
 // 桌面系统使用默认内存级别（平衡压缩率和速度）
#if defined(_WIN32) || defined(_WIN64) || defined(__linux__) || defined(__APPLE__)
#define MEM_LEVEL   6   /* 默认值，适合桌面系统，平衡内存占用和压缩效率 */
// 嵌入式系统使用低内存级别（优先节省内存）
#else
#define MEM_LEVEL   3   /* 适配嵌入式设备，低内存占用模式，牺牲部分压缩率换取RAM节省 */
#endif
#endif

/* deflate压缩的内存需求（单位：字节）：
            (1 << (windowBits+2)) +  (1 << (memLevel+9))
 即：windowBits=15（默认）对应128KB + memLevel=8（默认）对应128KB，共256KB
 再加上少量小对象占用的内存。例如，若需将默认内存需求从256KB降至128KB，
 可使用以下命令编译：
     make CFLAGS="-O -DMAX_WBITS=14 -DMAX_MEM_LEVEL=7"
 当然，这通常会降低压缩率（没有免费的午餐）。

 inflate解压缩的内存需求（单位：字节）：1 << windowBits
 即：windowBits=15（默认）对应32KB，再加上约7KB小对象占用的内存。
*/

/* 类型声明 */

#ifndef OF /* 函数原型格式定义（兼容非标准C的函数声明）*/
#ifdef STDC
#define OF(args)  args  // 标准C：保留参数列表
#else
#define OF(args)  ()    // 非标准C：省略参数列表（兼容旧编译器）
#endif
#endif

#ifndef Z_ARG /* 可变参数函数原型格式定义 */
#if defined(STDC) || defined(Z_HAVE_STDARG_H)
#define Z_ARG(args)  args  // 支持stdarg.h：保留参数列表
#else
#define Z_ARG(args)  ()    // 不支持：省略参数列表
#endif
#endif

/* 以下FAR宏的定义仅用于MSDOS混合内存模型编程（小/中模型下的远内存分配）。
 * 仅在MSC编译器下测试过；对于其他MSDOS编译器，可能需要在zutil.h中定义NO_MEMCPY。
 * 若无需混合内存模型，直接将FAR定义为空即可。
 */
#ifdef SYS16BIT
#if defined(M_I86SM) || defined(M_I86MM)
 /* MSC编译器的小/中内存模型 */
#define SMALL_MEDIUM
#ifdef _MSC_VER
#define FAR _far  // MSC用_far关键字表示远指针
#else
#define FAR far
#endif
#endif
#if (defined(__SMALL__) || defined(__MEDIUM__))
    /* Turbo C编译器的小/中内存模型 */
#define SMALL_MEDIUM
#ifdef __BORLANDC__
#define FAR _far  // Borland C用_far关键字表示远指针
#else
#define FAR far
#endif
#endif
#endif

/* Windows系统特化配置（DLL编译与调用约定）*/
#if defined(WINDOWS) || defined(WIN32)
    /* 若编译或使用zlib作为DLL，定义ZLIB_DLL。
    * 非强制，但可提升少量性能。
    */
#ifdef ZLIB_DLL
#if defined(WIN32) && (!defined(__BORLANDC__) || (__BORLANDC__ >= 0x500))
#ifdef ZLIB_INTERNAL  // zlib内部编译（生成DLL）
#define ZEXTERN extern __declspec(dllexport)  // 导出符号
#else                   // 外部调用（使用DLL）
#define ZEXTERN extern __declspec(dllimport)  // 导入符号
#endif
#endif
#endif  /* ZLIB_DLL */
    /* 若编译或使用zlib时采用WINAPI/WINAPIV调用约定，定义ZLIB_WINAPI。
    * 注意：标准ZLIB1.DLL并非用ZLIB_WINAPI编译。
    */
#ifdef ZLIB_WINAPI
#ifdef FAR
#undef FAR  // WINAPI下无需FAR，取消定义
#endif
#include <windows.h>  // 引入Windows系统头文件
    /* 无需用_export，改用ZLIB.DEF文件导出符号。 */
    /* 为完全兼容Windows，使用WINAPI而非__stdcall。 */
#define ZEXPORT WINAPI  // 定义函数导出调用约定
#ifdef WIN32
#define ZEXPORTVA WINAPIV  // 可变参数函数的调用约定
#else
#define ZEXPORTVA FAR CDECL
#endif
#endif
#endif

/* BEOS系统特化配置（DLL编译）*/
#if defined (__BEOS__)
#ifdef ZLIB_DLL
#ifdef ZLIB_INTERNAL  // 内部编译（生成DLL）
#define ZEXPORT   __declspec(dllexport)
#define ZEXPORTVA __declspec(dllexport)
#else                   // 外部调用（使用DLL）
#define ZEXPORT   __declspec(dllimport)
#define ZEXPORTVA __declspec(dllimport)
#endif
#endif
#endif

/* 默认符号与调用约定定义（无特殊系统配置时使用）*/
#ifndef ZEXTERN
#define ZEXTERN extern  // 外部符号声明
#endif
#ifndef ZEXPORT
#define ZEXPORT         // 函数导出（默认空，无特殊调用约定）
#endif
#ifndef ZEXPORTVA
#define ZEXPORTVA       // 可变参数函数导出（默认空）
#endif

#ifndef FAR
#define FAR  // 默认空（非16位系统无需远指针）
#endif

/* 基础数据类型定义 */
#if !defined(__MACTYPES__)  // Mac系统已定义Byte，避免重复
typedef unsigned char  Byte;  /* 8位无符号字节 */
#endif
typedef unsigned int   uInt;  /* 无符号整数（至少16位）*/
typedef unsigned long  uLong; /* 无符号长整数（至少32位）*/

/* 远指针类型定义（仅16位系统需要）*/
#ifdef SMALL_MEDIUM
    /* Borland C/C++ 和部分旧MSC版本会忽略typedef中的FAR，需特殊处理 */
#define Bytef Byte FAR
#else
typedef Byte  FAR Bytef;
#endif
typedef char  FAR charf;
typedef int   FAR intf;
typedef uInt  FAR uIntf;
typedef uLong FAR uLongf;

/* 通用指针类型定义（兼容const修饰）*/
#ifdef STDC
typedef void const* voidpc;  // const void指针（输入参数）
typedef void FAR* voidpf;  // 远指针版void指针
typedef void* voidp;   // void指针（输出参数）
#else
typedef Byte const* voidpc;  // 非标准C用Byte const*模拟const void*
typedef Byte FAR* voidpf;
typedef Byte* voidp;
#endif

/* z_crc_t 类型定义（CRC32校验值的存储类型）*/
#if !defined(Z_U4) && !defined(Z_SOLO) && defined(STDC)
#include <limits.h>  // 引入整数范围定义
#if (UINT_MAX == 0xffffffffUL)  // 若unsigned为32位
#define Z_U4 unsigned
#elif (ULONG_MAX == 0xffffffffUL)  // 若unsigned long为32位
#define Z_U4 unsigned long
#elif (USHRT_MAX == 0xffffffffUL)  // 若unsigned short为32位（极少情况）
#define Z_U4 unsigned short
#endif
#endif
#ifdef Z_U4
typedef Z_U4 z_crc_t;
#else
typedef unsigned long z_crc_t;  // 默认用unsigned long存储CRC32值
#endif

/* 头文件包含控制（由configure工具配置）*/
#ifdef HAVE_UNISTD_H    /* 可由 ./configure 设为 #if 1 启用 */
#define Z_HAVE_UNISTD_H  // 标记支持unistd.h
#endif
#ifdef HAVE_STDARG_H    /* 可由 ./configure 设为 #if 1 启用 */
#define Z_HAVE_STDARG_H  // 标记支持stdarg.h
#endif

#ifdef STDC
#ifndef Z_SOLO
//        #include <sys/types.h>      /* 引入off_t类型（按需启用）*/
#endif
#endif

/* 可变参数头文件包含 */
#if defined(STDC) || defined(Z_HAVE_STDARG_H)
#ifndef Z_SOLO
#include <stdarg.h>         /* 引入va_list类型（可变参数）*/
#endif
#endif

/* Windows系统宽字符支持 */
#ifdef _WIN32
#ifndef Z_SOLO
#include <stddef.h>         /* 引入wchar_t类型 */
#endif
#endif

/* 64位文件支持宏处理（兼容不同编译器的64位标识）*/
/* 兼容 "#define _LARGEFILE64_SOURCE" 和 "#define _LARGEFILE64_SOURCE 1" 两种写法，
 * 均视为请求64位操作；同时将 "#undef _LARGEFILE64_SOURCE" 和 "#define _LARGEFILE64_SOURCE 0"
 * 均视为不请求64位操作
 */
#if defined(_LARGEFILE64_SOURCE) && -_LARGEFILE64_SOURCE - -1 == 1
#undef _LARGEFILE64_SOURCE
#endif

 /* Watcom编译器特化（默认支持unistd.h）*/
#if defined(__WATCOMC__) && !defined(Z_HAVE_UNISTD_H)
#define Z_HAVE_UNISTD_H
#endif

/* 文件偏移量类型定义（z_off_t）*/
#ifndef Z_SOLO
#if defined(Z_HAVE_UNISTD_H) || defined(_LARGEFILE64_SOURCE)
#include <unistd.h>         /* 引入SEEK_*宏、off_t类型和_LFS64_LARGEFILE */
#ifdef VMS
#include <unixio.h>       /* VMS系统引入off_t类型 */
#endif
#ifndef z_off_t
#define z_off_t off_t  // 用系统off_t作为z_off_t
#endif
#endif
#endif

/* 64位文件系统支持标记 */
#if defined(_LFS64_LARGEFILE) && _LFS64_LARGEFILE-0
#define Z_LFS64  // 标记支持LFS64（大文件系统）
#endif
#if defined(_LARGEFILE64_SOURCE) && defined(Z_LFS64)
#define Z_LARGE64  // 标记启用64位文件操作
#endif
#if defined(_FILE_OFFSET_BITS) && _FILE_OFFSET_BITS-0 == 64 && defined(Z_LFS64)
#define Z_WANT64  // 标记希望使用64位偏移量
#endif

/* 若未定义SEEK_*宏（文件定位参数），默认定义 */
#ifndef z_off_t
#define z_off_t long  // 默认用long作为偏移量类型
#endif

/* 64位偏移量类型定义（z_off64_t）*/
#if !defined(_WIN32) && defined(Z_LARGE64)
#define z_off64_t off64_t  // 非Windows系统用off64_t
#else
#if defined(_WIN32) && !defined(__GNUC__) && !defined(Z_SOLO)
#define z_off64_t __int64  // Windows系统（非GCC）用__int64
#else
#define z_off64_t z_off_t  // 其他情况用z_off_t（32位偏移）
#endif
#endif

/* MVS系统链接器兼容（外部名称长度不超过8字节，需缩短函数名）*/
#if defined(__MVS__)
#pragma map(deflateInit_,"DEIN")
#pragma map(deflateInit2_,"DEIN2")
#pragma map(deflateEnd,"DEEND")
#pragma map(deflateBound,"DEBND")
#pragma map(inflateInit_,"ININ")
#pragma map(inflateInit2_,"ININ2")
#pragma map(inflateEnd,"INEND")
#pragma map(inflateSync,"INSY")
#pragma map(inflateSetDictionary,"INSEDI")
#pragma map(compressBound,"CMBND")
#pragma map(inflate_table,"INTABL")
#pragma map(inflate_fast,"INFA")
#pragma map(inflate_copyright,"INCOPY")
#endif

#endif /* ZCONF_H */