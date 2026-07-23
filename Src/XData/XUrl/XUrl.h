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
 * @brief      在堆上创建 XUrl 实例（从字符串）
 * @param urlString URL 字符串
 * @param mode      解析模式
 * @return     指向新创建的 XUrl 对象的指针，失败返回 NULL
 */
XUrl* XUrl_create_ex(const char* urlString, XUrl_ParsingMode mode);

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
void XUrl_init_ex(XUrl* self, const char* urlString, XUrl_ParsingMode mode);

/**
 * @brief      复制构造函数
 * @param self 目标 XUrl 对象指针
 * @param other 源 XUrl 对象指针
 */
void XUrl_copy(XUrl* self, const XUrl* other);

/**
 * @brief      移动构造函数
 * @param self 目标 XUrl 对象指针
 * @param other 源 XUrl 对象指针（移动后源对象变为空）
 */
void XUrl_move(XUrl* self, XUrl* other);

/**
 * @brief      释放 XUrl 资源
 * @param self 待释放的 XUrl 对象指针
 */
void XUrl_deinit(XUrl* self);

/**
 * @brief      在堆上删除 XUrl 实例
 * @param self 待删除的 XUrl 对象指针
 */
void XUrl_delete(XUrl* self);

/* ========== 虚函数调度 ========== */

void XUrl_copy_base(XUrl* dest, const XUrl* src);
void XUrl_move_base(XUrl* dest, XUrl* src);
void XUrl_deinit_base(XUrl* self);
void XUrl_delete_base(XUrl* self);

/* ========== 设置 URL ========== */

/**
 * @brief      从字符串设置 URL
 * @param self      目标 XUrl 对象指针
 * @param urlString URL 字符串
 * @param mode      解析模式
 */
void XUrl_setUrl(XUrl* self, const char* urlString, XUrl_ParsingMode mode);

/**
 * @brief      从编码后的字符串设置 URL
 * @param self      目标 XUrl 对象指针
 * @param urlString 编码后的 URL 字符串
 * @param mode      解析模式
 */
void XUrl_setEncodedUrl(XUrl* self, const char* urlString, XUrl_ParsingMode mode);

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
 * @return     方案字符串
 */
const char* XUrl_scheme(const XUrl* self);

/**
 * @brief      设置方案
 * @param self   目标 XUrl 对象指针
 * @param scheme 方案字符串
 */
void XUrl_setScheme(XUrl* self, const char* scheme);

/**
 * @brief      获取用户名
 * @param self 目标 XUrl 对象指针
 * @return     用户名字符串
 */
const char* XUrl_userName(const XUrl* self);

/**
 * @brief      设置用户名
 * @param self     目标 XUrl 对象指针
 * @param userName 用户名字符串
 */
void XUrl_setUserName(XUrl* self, const char* userName);

/**
 * @brief      获取密码
 * @param self 目标 XUrl 对象指针
 * @return     密码字符串
 */
const char* XUrl_password(const XUrl* self);

/**
 * @brief      设置密码
 * @param self     目标 XUrl 对象指针
 * @param password 密码字符串
 */
void XUrl_setPassword(XUrl* self, const char* password);

/**
 * @brief      获取主机名
 * @param self 目标 XUrl 对象指针
 * @return     主机名字符串
 */
const char* XUrl_host(const XUrl* self);

/**
 * @brief      设置主机名
 * @param self 目标 XUrl 对象指针
 * @param host 主机名字符串
 */
void XUrl_setHost(XUrl* self, const char* host);

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
 * @return     路径字符串
 */
const char* XUrl_path(const XUrl* self);

/**
 * @brief      设置路径
 * @param self 目标 XUrl 对象指针
 * @param path 路径字符串
 */
void XUrl_setPath(XUrl* self, const char* path);

/**
 * @brief      获取查询字符串
 * @param self 目标 XUrl 对象指针
 * @return     查询字符串
 */
const char* XUrl_query(const XUrl* self);

/**
 * @brief      设置查询字符串
 * @param self  目标 XUrl 对象指针
 * @param query 查询字符串
 */
void XUrl_setQuery(XUrl* self, const char* query);

/**
 * @brief      获取片段标识符
 * @param self 目标 XUrl 对象指针
 * @return     片段标识符
 */
const char* XUrl_fragment(const XUrl* self);

/**
 * @brief      设置片段标识符
 * @param self     目标 XUrl 对象指针
 * @param fragment 片段标识符
 */
void XUrl_setFragment(XUrl* self, const char* fragment);

/**
 * @brief      获取用户信息（username:password）
 * @param self 目标 XUrl 对象指针
 * @return     用户信息字符串
 */
const char* XUrl_userInfo(const XUrl* self);

/**
 * @brief      设置用户信息
 * @param self     目标 XUrl 对象指针
 * @param userInfo 用户信息字符串（username:password 格式）
 */
void XUrl_setUserInfo(XUrl* self, const char* userInfo);

/**
 * @brief      获取权限部分（userinfo@host:port）
 * @param self 目标 XUrl 对象指针
 * @return     权限字符串
 */
const char* XUrl_authority(const XUrl* self);

/* ========== 转换方法 ========== */

/**
 * @brief      将 URL 转换为字符串
 * @param self   目标 XUrl 对象指针
 * @param out    输出字符串缓冲区
 * @param size   缓冲区大小
 * @return      指向 out 的指针
 */
char* XUrl_toString(const XUrl* self, char* out, size_t size);

/**
 * @brief      将 URL 转换为编码后的字符串
 * @param self   目标 XUrl 对象指针
 * @param out    输出字符串缓冲区
 * @param size   缓冲区大小
 * @return      指向 out 的指针
 */
char* XUrl_toEncoded(const XUrl* self, char* out, size_t size);

/**
 * @brief      将 URL 转换为显示字符串（考虑格式化选项）
 * @param self   目标 XUrl 对象指针
 * @param options 格式化选项（位标志组合）
 * @param out    输出字符串缓冲区
 * @param size   缓冲区大小
 * @return      指向 out 的指针
 */
char* XUrl_toDisplayString(const XUrl* self, int options, char* out, size_t size);

/* ========== 静态方法 ========== */

/**
 * @brief      从本地文件路径创建 URL
 * @param localfile 本地文件路径
 * @return     指向新 XUrl 的指针，调用者负责释放
 */
XUrl* XUrl_fromLocalFile(const char* localfile);

/**
 * @brief      将 URL 转换为本地文件路径
 * @param self 目标 XUrl 对象指针
 * @return     本地文件路径字符串（内部缓冲区，每次调用覆盖）
 */
const char* XUrl_toLocalFile(const XUrl* self);

/**
 * @brief      解析相对 URL
 * @param self     目标 XUrl 对象指针（基础 URL）
 * @param relative 相对 URL 字符串
 * @param out      输出 XUrl 对象指针
 */
void XUrl_resolved(const XUrl* self, const char* relative, XUrl* out);

/**
 * @brief      URL 编码
 * @param input   输入字符串
 * @param exclude 排除字符（不需要编码的字符）
 * @param include 强制编码的字符
 * @return     指向新 XString 的指针，调用者负责释放
 */
XString* XUrl_toPercentEncoding(const char* input, const char* exclude, const char* include);

/**
 * @brief      URL 解码
 * @param input 编码后的输入字符串
 * @return     指向新 XString 的指针，调用者负责释放
 */
XString* XUrl_fromPercentEncoding(const char* input);

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
