/******************************************************************************
 * @file       XUrl.h
 * @brief      XUrl URL 类（对标 Qt 6.8 QUrl）
 * @author     XinYueC 团队
 * @note       提供 URL 解析、编码/解码、组件访问等操作
 ******************************************************************************/
#ifndef XURL_H
#define XURL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XClass.h"
#include "XString.h"
#include "XStringList.h"

/* ========== XUrl 虚函数表枚举 ========== */
XCLASS_DEFINE_BEGING(XUrl)
XCLASS_DEFINE_EXTEND_END(XUrl, XClass)

/* ========== 枚举定义（对标 Qt 6.8 QUrl） ========== */

/**
 * @brief      URL 解析模式枚举
 */
typedef enum XUrl_ParsingMode
{
    XUrl_TolerantMode = 0, /**< 宽松模式（容忍常见错误） */
    XUrl_StrictMode = 1,   /**< 严格模式（严格遵循 RFC 规范） */
    XUrl_DecodedMode = 2   /**< 解码模式（输入已解码） */
} XUrl_ParsingMode;

/**
 * @brief      URL 格式化选项枚举（位标志）
 */
typedef enum XUrl_UrlFormattingOption
{
    XUrl_None               = 0x0000, /**< 无标志 */
    XUrl_RemoveScheme       = 0x0001, /**< 移除方案 */
    XUrl_RemovePassword     = 0x0002, /**< 移除密码 */
    XUrl_RemoveUserInfo     = 0x0006, /**< 移除用户信息（密码+用户名） */
    XUrl_RemovePort         = 0x0008, /**< 移除端口 */
    XUrl_RemoveAuthority    = 0x001E, /**< 移除权限部分 */
    XUrl_RemovePath         = 0x0020, /**< 移除路径 */
    XUrl_RemoveQuery        = 0x0040, /**< 移除查询 */
    XUrl_RemoveFragment     = 0x0080, /**< 移除片段 */
    XUrl_PreferLocalFile    = 0x0200, /**< 优先本地文件格式 */
    XUrl_StripTrailingSlash = 0x0400, /**< 移除尾部斜杠 */
    XUrl_RemoveFilename     = 0x0800, /**< 移除文件名 */
    XUrl_NormalizePathSegments = 0x1000 /**< 规范化路径段 */
} XUrl_UrlFormattingOption;

/**
 * @brief      组件格式化选项枚举（位标志）
 */
typedef enum XUrl_ComponentFormattingOption
{
    XUrl_PrettyDecoded     = 0x000000, /**< 美化解码 */
    XUrl_EncodeSpaces      = 0x100000, /**< 编码空格 */
    XUrl_EncodeUnicode     = 0x200000, /**< 编码 Unicode */
    XUrl_EncodeDelimiters  = 0x400000, /**< 编码分隔符 */
    XUrl_EncodeReserved    = 0x800000, /**< 编码保留字符 */
    XUrl_DecodeReserved    = 0x1000000,/**< 解码保留字符 */
    XUrl_FullyEncoded      = 0x700000, /**< 完全编码 */
    XUrl_FullyDecoded      = 0x1000000 /**< 完全解码 */
} XUrl_ComponentFormattingOption;

/* ========== XUrl 结构体 ========== */

/**
 * @brief      XUrl URL 结构体（对标 Qt 6.8 QUrl）
 * @note       继承自 XClass，包含 URL 的各个组件
 */
typedef struct XUrl
{
    XClass     m_class;      /**< 继承的基类成员 */
    XString*   m_scheme;     /**< 方案（如 http、https、ftp） */
    XString*   m_userName;   /**< 用户名 */
    XString*   m_password;   /**< 密码 */
    XString*   m_host;       /**< 主机名 */
    XString*   m_path;       /**< 路径 */
    XString*   m_query;      /**< 查询字符串 */
    XString*   m_fragment;   /**< 片段标识符 */
    int        m_port;       /**< 端口号（-1 表示未指定） */
    bool       m_isValid;     /**< URL 是否有效 */
} XUrl;

/* ========== 虚函数表初始化 ========== */

/**
 * @brief      初始化 XUrl 类的虚函数表
 * @return     指向初始化完成的 XVtable 的指针
 */
XVtable* XUrl_class_init(void);

/* ========== 创建与初始化 ========== */

/**
 * @brief      在堆上创建 XUrl 实例
 * @return     指向新创建的 XUrl 对象的指针，失败返回 NULL
 */
XUrl* XUrl_create(void);

/**
 * @brief      在堆上拷贝创建 XUrl 实例（深拷贝）
 * @param other 源 XUrl 对象指针
 * @return     指向新创建的 XUrl 对象的指针，失败返回 NULL
 */
XUrl* XUrl_create_copy(const XUrl* other);

/**
 * @brief      在堆上移动创建 XUrl 实例（转移 other 资源所有权）
 * @param other 源 XUrl 对象指针（移动后被置空）
 * @return     指向新创建的 XUrl 对象的指针，失败返回 NULL
 */
XUrl* XUrl_create_move(XUrl* other);

/**
 * @brief      在堆上创建 XUrl 实例（从字符串）
 * @param urlString URL 字符串
 * @param mode      解析模式
 * @return     指向新创建的 XUrl 对象的指针，失败返回 NULL
 */
XUrl* XUrl_create_ex(const XString* urlString, XUrl_ParsingMode mode);

/**
 * @brief      初始化 XUrl 实例
 * @param self 待初始化的 XUrl 对象指针
 */
void XUrl_init(XUrl* self);

/**
 * @brief      初始化 XUrl 实例（从字符串）
 * @param self      待初始化的 XUrl 对象指针
 * @param urlString URL 字符串
 * @param mode      解析模式
 */
void XUrl_init_ex(XUrl* self, const XString* urlString, XUrl_ParsingMode mode);

/* ========== 虚函数调度 ========== */

#define  XUrl_copy_base            XClass_copy_base
#define  XUrl_move_base            XClass_move_base
#define  XUrl_deinit_base          XClass_deinit_base
#define  XUrl_delete_base          XClass_delete_base

/* ========== 设置 URL ========== */

/**
 * @brief      从字符串设置 URL
 * @param self      目标 XUrl 对象指针
 * @param urlString URL 字符串
 * @param mode      解析模式
 */
void XUrl_setUrl(XUrl* self, const XString* urlString, XUrl_ParsingMode mode);

/**
 * @brief      从编码后的字符串设置 URL
 * @param self      目标 XUrl 对象指针
 * @param urlString 编码后的 URL 字符串
 * @param mode      解析模式
 */
void XUrl_setEncodedUrl(XUrl* self, const XString* urlString, XUrl_ParsingMode mode);

/* ========== 查询方法 ========== */

/**
 * @brief      判断 URL 是否有效
 * @param self 目标 XUrl 对象指针
 * @return     有效返回 true
 */
bool XUrl_isValid(const XUrl* self);

/**
 * @brief      判断 URL 是否为空
 * @param self 目标 XUrl 对象指针
 * @return     空返回 true
 */
bool XUrl_isEmpty(const XUrl* self);

/**
 * @brief      判断 URL 是否为相对 URL
 * @param self 目标 XUrl 对象指针
 * @return     相对 URL 返回 true
 */
bool XUrl_isRelative(const XUrl* self);

/**
 * @brief      判断 URL 是否为本地文件
 * @param self 目标 XUrl 对象指针
 * @return     本地文件 URL 返回 true
 */
bool XUrl_isLocalFile(const XUrl* self);

/* ========== 组件访问 ========== */

/**
 * @brief      获取方案
 * @param self 目标 XUrl 对象指针
 * @return     方案字符串（XString，需调用方按需使用）
 */
const XString* XUrl_scheme_const(const XUrl* self);

/**
 * @brief      设置方案
 * @param self   目标 XUrl 对象指针
 * @param scheme 方案字符串
 */
void XUrl_setScheme(XUrl* self, const XString* scheme);

/**
 * @brief      获取用户名
 * @param self 目标 XUrl 对象指针
 * @return     用户名字符串（XString）
 */
const XString* XUrl_userName_const(const XUrl* self);

/**
 * @brief      设置用户名
 * @param self     目标 XUrl 对象指针
 * @param userName 用户名字符串
 */
void XUrl_setUserName(XUrl* self, const XString* userName);

/**
 * @brief      获取密码
 * @param self 目标 XUrl 对象指针
 * @return     密码字符串（XString）
 */
const XString* XUrl_password_const(const XUrl* self);

/**
 * @brief      设置密码
 * @param self     目标 XUrl 对象指针
 * @param password 密码字符串
 */
void XUrl_setPassword(XUrl* self, const XString* password);

/**
 * @brief      获取主机名
 * @param self 目标 XUrl 对象指针
 * @return     主机名字符串（XString）
 */
const XString* XUrl_host_const(const XUrl* self);

/**
 * @brief      设置主机名
 * @param self 目标 XUrl 对象指针
 * @param host 主机名字符串
 */
void XUrl_setHost(XUrl* self, const XString* host);

/**
 * @brief      获取端口号
 * @param self 目标 XUrl 对象指针
 * @return     端口号（-1 表示未指定）
 */
int XUrl_port(const XUrl* self);

/**
 * @brief      设置端口号
 * @param self 目标 XUrl 对象指针
 * @param port 端口号（-1 表示未指定）
 */
void XUrl_setPort(XUrl* self, int port);

/**
 * @brief      获取路径
 * @param self 目标 XUrl 对象指针
 * @return     路径字符串（XString）
 */
const XString* XUrl_path_const(const XUrl* self);

/**
 * @brief      设置路径
 * @param self 目标 XUrl 对象指针
 * @param path 路径字符串
 */
void XUrl_setPath(XUrl* self, const XString* path);

/**
 * @brief      获取查询字符串
 * @param self 目标 XUrl 对象指针
 * @return     查询字符串（XString）
 */
const XString* XUrl_query_const(const XUrl* self);

/**
 * @brief      设置查询字符串
 * @param self  目标 XUrl 对象指针
 * @param query 查询字符串
 */
void XUrl_setQuery(XUrl* self, const XString* query);

/**
 * @brief      获取片段标识符
 * @param self 目标 XUrl 对象指针
 * @return     片段标识符（XString）
 */
const XString* XUrl_fragment_const(const XUrl* self);

/**
 * @brief      设置片段标识符
 * @param self     目标 XUrl 对象指针
 * @param fragment 片段标识符
 */
void XUrl_setFragment(XUrl* self, const XString* fragment);

/**
 * @brief      获取用户信息（username:password）
 * @param self 目标 XUrl 对象指针
 * @return     用户信息字符串（XString）
 */
const XString* XUrl_userInfo_const(const XUrl* self);

/**
 * @brief      设置用户信息
 * @param self     目标 XUrl 对象指针
 * @param userInfo 用户信息字符串（username:password 格式）
 */
void XUrl_setUserInfo(XUrl* self, const XString* userInfo);

/**
 * @brief      获取权限部分（userinfo@host:port）
 * @param self 目标 XUrl 对象指针
 * @return     权限字符串（XString）
 */
const XString* XUrl_authority_const(const XUrl* self);

/* ========== 转换方法 ========== */

/**
 * @brief      将 URL 转换为字符串
 * @param self   目标 XUrl 对象指针
 * @return      新分配的 XString 字符串（调用方负责释放）
 */
XString* XUrl_toString(const XUrl* self);

/**
 * @brief      将 URL 转换为编码后的字符串
 * @param self   目标 XUrl 对象指针
 * @return      新分配的 XString 字符串（调用方负责释放）
 */
XString* XUrl_toEncoded(const XUrl* self);

/**
 * @brief      将 URL 转换为显示字符串（考虑格式化选项）
 * @param self   目标 XUrl 对象指针
 * @param options 格式化选项（位标志组合）
 * @return      新分配的 XString 字符串（调用方负责释放）
 */
XString* XUrl_toDisplayString(const XUrl* self, int options);

/* ========== 静态方法 ========== */

/**
 * @brief      从本地文件路径创建 URL
 * @param localfile 本地文件路径
 * @return     指向新 XUrl 的指针，调用者负责释放
 */
XUrl* XUrl_fromLocalFile(const XString* localfile);

/**
 * @brief      将 URL 转换为本地文件路径
 * @param self 目标 XUrl 对象指针
 * @return     新分配的本地文件路径 XString（调用方负责释放），非本地文件返回 NULL
 */
const XString* XUrl_toLocalFile_const(const XUrl* self);

/**
 * @brief      解析相对 URL
 * @param self     目标 XUrl 对象指针（基础 URL）
 * @param relative 相对 URL 字符串
 * @param out      输出 XUrl 对象指针
 */
void XUrl_resolved(const XUrl* self, const XString* relative, XUrl* out);

/**
 * @brief      URL 编码
 * @param input   输入字符串
 * @param exclude 排除字符（不需要编码的字符）
 * @param include 强制编码的字符
 * @return     指向新 XString 的指针，调用者负责释放
 */
XString* XUrl_toPercentEncoding(const XString* input, const XString* exclude, const XString* include);

/**
 * @brief      URL 解码
 * @param input 编码后的输入字符串
 * @return     指向新 XString 的指针，调用者负责释放
 */
XString* XUrl_fromPercentEncoding(const XString* input);

/**
 * @brief      判断两个 URL 是否相等
 * @param a URL A
 * @param b URL B
 * @return     相等返回 true
 */
bool XUrl_equals(const XUrl* a, const XUrl* b);

/**
 * @brief      交换两个 URL
 * @param a URL A
 * @param b URL B
 */
void XUrl_swap(XUrl* a, XUrl* b);

#ifdef __cplusplus
}
#endif
#endif /* XURL_H */
