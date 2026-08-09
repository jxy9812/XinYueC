/******************************************************************************
 * @file       XUrl.c
 * @brief      XUrl URL 类实现（对标 Qt 6.8 QUrl）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XUrl.h"
#include "XMemory.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdint.h>

/* ========== 内部 URL 解析函数 ========== */

/**
 * @brief      将十六进制字符转换为数值
 * @param c    十六进制字符
 * @return     数值（0~15），无效返回 -1
 */
static int hexCharToInt(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * @brief      判断字符是否为 URL 保留字符
 * @param c    字符
 * @return     保留字符返回 true
 */
static bool isReservedChar(char c)
{
    switch (c) {
        case ':': case '/': case '?': case '#': case '[': case ']':
        case '@': case '!': case '$': case '&': case '(': case ')':
        case '*': case '+': case ',': case ';': case '=':
            return true;
        default:
            return false;
    }
}

/**
 * @brief      判断字符是否为 URL 非保留字符（可不用编码）
 * @param c    字符
 * @return     非保留字符返回 true
 */
static bool isUnreservedChar(char c)
{
    return isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~';
}

/**
 * @brief      URL 百分比编码一个字符
 * @param c    字符
 * @param out  输出缓冲区（至少 4 字节）
 * @return     编码后的字符数
 */
static int percentEncode(char c, char* out)
{
    if (isUnreservedChar(c)) {
        out[0] = c;
        out[1] = '\0';
        return 1;
    }
    snprintf(out, 4, "%%%02X", (unsigned char)c);
    return 3;
}

/**
 * @brief      URL 百分比解码
 * @param src  输入字符串
 * @param dst  输出缓冲区
 * @return     解码后的字符数
 */
static int percentDecode(const char* src, char* dst)
{
    int i = 0, j = 0;
    while (src[i]) {
        if (src[i] == '%' && src[i+1] && src[i+2]) {
            int hi = hexCharToInt(src[i+1]);
            int lo = hexCharToInt(src[i+2]);
            if (hi >= 0 && lo >= 0) {
                dst[j++] = (char)((hi << 4) | lo);
                i += 3;
                continue;
            }
        }
        dst[j++] = src[i++];
    }
    dst[j] = '\0';
    return j;
}

/**
 * @brief      解析 URL 字符串（基本实现）
 * @param self 目标 XUrl 对象指针
 * @param urlString URL 字符串
 * @param mode 解析模式
 */
static void parseUrl(XUrl* self, const char* urlString, XUrl_ParsingMode mode)
{
    if (!self || !urlString) return;
    
    /* 清除旧数据 */
    if (self->m_scheme) XString_clear_base(self->m_scheme);
    if (self->m_userName) XString_clear_base(self->m_userName);
    if (self->m_password) XString_clear_base(self->m_password);
    if (self->m_host) XString_clear_base(self->m_host);
    if (self->m_path) XString_clear_base(self->m_path);
    if (self->m_query) XString_clear_base(self->m_query);
    if (self->m_fragment) XString_clear_base(self->m_fragment);
    self->m_port = -1;
    self->m_isValid = true;

    const char* p = urlString;
    const char* end = p + strlen(p);
    
    /* 1. 解析方案（scheme） */
    const char* schemeEnd = strstr(p, "://");
    if (schemeEnd && schemeEnd > p) {
        char scheme[64];
        size_t len = (size_t)(schemeEnd - p);
        if (len < sizeof(scheme)) {
            strncpy(scheme, p, len);
            scheme[len] = '\0';
            XString_append_utf8(self->m_scheme, scheme);
        }
        p = schemeEnd + 3;
    }
    
    /* 2. 解析权限部分（authority = userinfo@host:port） */
    const char* pathStart = NULL;
    const char* slashPos = strchr(p, '/');
    const char* queryPos = strchr(p, '?');
    const char* fragPos = strchr(p, '#');
    
    /* 查找路径开始位置 */
    if (slashPos) {
        pathStart = slashPos;
    } else if (queryPos) {
        pathStart = queryPos;
    } else if (fragPos) {
        pathStart = fragPos;
    } else {
        pathStart = end;
    }
    
    /* 解析 authority */
    if (p < pathStart) {
        const char* atPos = NULL;
        const char* scan = p;
        while (scan < pathStart) {
            if (*scan == '@') {
                atPos = scan;
            } else if (*scan == '/' || *scan == '?' || *scan == '#') {
                break;
            }
            scan++;
        }
        
        if (atPos) {
            /* 解析 userinfo */
            const char* colonPos = NULL;
            scan = p;
            while (scan < atPos) {
                if (*scan == ':') { colonPos = scan; break; }
                scan++;
            }
            if (colonPos) {
                /* username:password */
                char userName[256], password[256];
                size_t ulen = (size_t)(colonPos - p);
                size_t plen = (size_t)(atPos - colonPos - 1);
                if (ulen < sizeof(userName) && plen < sizeof(password)) {
                    strncpy(userName, p, ulen); userName[ulen] = '\0';
                    strncpy(password, colonPos + 1, plen); password[plen] = '\0';
                    XString_append_utf8(self->m_userName, userName);
                    XString_append_utf8(self->m_password, password);
                }
            } else {
                /* 只有用户名 */
                char userName[256];
                size_t ulen = (size_t)(atPos - p);
                if (ulen < sizeof(userName)) {
                    strncpy(userName, p, ulen); userName[ulen] = '\0';
                    XString_append_utf8(self->m_userName, userName);
                }
            }
            p = atPos + 1;
        }
        
        /* 解析 host:port */
        const char* hostStart = p;
        const char* hostEnd = pathStart;
        
        /* IPv6 地址 */
        if (*hostStart == '[') {
            const char* bracketEnd = strchr(hostStart, ']');
            if (bracketEnd && bracketEnd < hostEnd) {
                char host[256];
                size_t hlen = (size_t)(bracketEnd - hostStart - 1);
                if (hlen < sizeof(host)) {
                    strncpy(host, hostStart + 1, hlen); host[hlen] = '\0';
                    XString_append_utf8(self->m_host, host);
                }
                hostStart = bracketEnd + 1;
            }
        }
        
        /* 解析端口 */
        const char* portColon = NULL;
        scan = hostStart;
        while (scan < hostEnd) {
            if (*scan == ':') { portColon = scan; break; }
            scan++;
        }
        
        if (portColon) {
            /* 有主机和端口 */
            if (self->m_host && XString_size(self->m_host) == 0) {
                /* 不是 IPv6，需要提取主机 */
                char host[256];
                size_t hlen = (size_t)(portColon - hostStart);
                if (hlen < sizeof(host)) {
                    strncpy(host, hostStart, hlen); host[hlen] = '\0';
                    XString_append_utf8(self->m_host, host);
                }
            }
            self->m_port = atoi(portColon + 1);
        } else {
            /* 没有端口，只有主机 */
            if (hostStart < hostEnd) {
                char host[256];
                size_t hlen = (size_t)(hostEnd - hostStart);
                if (hlen < sizeof(host)) {
                    strncpy(host, hostStart, hlen); host[hlen] = '\0';
                    XString_append_utf8(self->m_host, host);
                }
            }
        }
        p = pathStart;
    }
    
    /* 3. 解析路径 */
    if (p < end && (*p == '/')) {
        const char* pathEnd = queryPos ? queryPos : (fragPos ? fragPos : end);
        if (pathEnd > p) {
            char path[4096];
            size_t plen = (size_t)(pathEnd - p);
            if (plen < sizeof(path)) {
                strncpy(path, p, plen); path[plen] = '\0';
                XString_append_utf8(self->m_path, path);
            }
        }
        p = pathEnd;
    } else if (p < end && *p != '?' && *p != '#') {
        /* 相对路径 */
        const char* pathEnd = queryPos ? queryPos : (fragPos ? fragPos : end);
        if (pathEnd > p) {
            char path[4096];
            size_t plen = (size_t)(pathEnd - p);
            if (plen < sizeof(path)) {
                strncpy(path, p, plen); path[plen] = '\0';
                XString_append_utf8(self->m_path, path);
            }
        }
        p = pathEnd;
    }
    
    /* 4. 解析查询 */
    if (queryPos && queryPos < (fragPos ? fragPos : end)) {
        const char* qEnd = fragPos ? fragPos : end;
        if (qEnd > queryPos + 1) {
            char query[4096];
            size_t qlen = (size_t)(qEnd - queryPos - 1);
            if (qlen < sizeof(query)) {
                strncpy(query, queryPos + 1, qlen); query[qlen] = '\0';
                XString_append_utf8(self->m_query, query);
            }
        }
    }
    
    /* 5. 解析片段 */
    if (fragPos && fragPos + 1 < end) {
        char fragment[4096];
        size_t flen = strlen(fragPos + 1);
        if (flen < sizeof(fragment)) {
            strncpy(fragment, fragPos + 1, flen); fragment[flen] = '\0';
            XString_append_utf8(self->m_fragment, fragment);
        }
    }
}

/* ========== 内部静态虚函数实现 ========== */

static void VXUrl_deinit(XUrl* self)
{
    if (!self) return;
    XString_delete_base(self->m_scheme);   self->m_scheme = NULL;
    XString_delete_base(self->m_userName); self->m_userName = NULL;
    XString_delete_base(self->m_password); self->m_password = NULL;
    XString_delete_base(self->m_host);     self->m_host = NULL;
    XString_delete_base(self->m_path);     self->m_path = NULL;
    XString_delete_base(self->m_query);    self->m_query = NULL;
    XString_delete_base(self->m_fragment); self->m_fragment = NULL;
    XString_delete_base(self->m_userInfo); self->m_userInfo = NULL;
    XString_delete_base(self->m_authority); self->m_authority = NULL;
}

static void VXUrl_copy(XUrl* dest, const XUrl* src)
{
    if (!dest || !src || dest == src) return;
    /* 目标未 init 则自动 init，让 copy_base 可在未初始化目标上安全调用 */
    if (XClassIsVtableNull(dest)) {
        XUrl_init(dest);
    }
    if (!dest->m_scheme || !dest->m_userName || !dest->m_password || !dest->m_host ||
        !dest->m_path || !dest->m_query || !dest->m_fragment || !dest->m_userInfo ||
        !dest->m_authority)
        return;
    if (src->m_scheme) XString_copy_base(dest->m_scheme, src->m_scheme);
    else XString_clear_base(dest->m_scheme);
    if (src->m_userName) XString_copy_base(dest->m_userName, src->m_userName);
    else XString_clear_base(dest->m_userName);
    if (src->m_password) XString_copy_base(dest->m_password, src->m_password);
    else XString_clear_base(dest->m_password);
    if (src->m_host) XString_copy_base(dest->m_host, src->m_host);
    else XString_clear_base(dest->m_host);
    if (src->m_path) XString_copy_base(dest->m_path, src->m_path);
    else XString_clear_base(dest->m_path);
    if (src->m_query) XString_copy_base(dest->m_query, src->m_query);
    else XString_clear_base(dest->m_query);
    if (src->m_fragment) XString_copy_base(dest->m_fragment, src->m_fragment);
    else XString_clear_base(dest->m_fragment);
    /* 缓存由 const 访问按需重建，复制时不能共享临时视图。 */
    XString_clear_base(dest->m_userInfo);
    XString_clear_base(dest->m_authority);
    dest->m_port = src->m_port;
    dest->m_isValid = src->m_isValid;
}

static void VXUrl_move(XUrl* dest, XUrl* src)
{
    if (!dest || !src || dest == src) return;
    /* 目标未 init 则自动 init */
    if (XClassIsVtableNull(dest)) {
        XUrl_init(dest);
    }
    if (!dest->m_scheme || !dest->m_userName || !dest->m_password || !dest->m_host ||
        !dest->m_path || !dest->m_query || !dest->m_fragment || !dest->m_userInfo ||
        !dest->m_authority || !src->m_scheme || !src->m_userName || !src->m_password ||
        !src->m_host || !src->m_path || !src->m_query || !src->m_fragment ||
        !src->m_userInfo || !src->m_authority)
        return;
    /* 移动各字符串内容，源 URL 仍保持已初始化且可继续使用。 */
    XString_move_base(dest->m_scheme, src->m_scheme);
    XString_move_base(dest->m_userName, src->m_userName);
    XString_move_base(dest->m_password, src->m_password);
    XString_move_base(dest->m_host, src->m_host);
    XString_move_base(dest->m_path, src->m_path);
    XString_move_base(dest->m_query, src->m_query);
    XString_move_base(dest->m_fragment, src->m_fragment);
    XString_clear_base(dest->m_userInfo);
    XString_clear_base(dest->m_authority);
    XString_clear_base(src->m_userInfo);
    XString_clear_base(src->m_authority);
    dest->m_port = src->m_port;
    dest->m_isValid = src->m_isValid;
    src->m_port = -1;
    src->m_isValid = false;
}

/* ========== 虚函数表初始化 ========== */

XVtable* XUrl_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XUrl)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXUrl_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXUrl_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXUrl_deinit);
    return XVTABLE_DEFAULT;
}

/* ========== 创建与初始化 ========== */

XUrl* XUrl_create(void)
{
    XUrl* self = (XUrl*)XMalloc_System(sizeof(XUrl));
    if (!self) return NULL;
    XUrl_init(self);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

XUrl* XUrl_create_copy(const XUrl* other)
{
    if (!other) return NULL;
    XUrl* self = XUrl_create();
    if (!self) return NULL;
    XUrl_copy_base(self, other);
    return self;
}

XUrl* XUrl_create_move(XUrl* other)
{
    if (!other) return NULL;
    XUrl* self = XUrl_create();
    if (!self) return NULL;
    XUrl_move_base(self, other);
    return self;
}

XUrl* XUrl_create_ex(const XString* urlString, XUrl_ParsingMode mode)
{
    XUrl* self = XUrl_create();
    if (!self) return NULL;
    XUrl_setUrl(self, urlString, mode);
    return self;
}

void XUrl_init(XUrl* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XUrl));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XUrl);
    self->m_scheme   = XString_create();
    self->m_userName = XString_create();
    self->m_password = XString_create();
    self->m_host     = XString_create();
    self->m_path     = XString_create();
    self->m_query    = XString_create();
    self->m_fragment = XString_create();
    self->m_userInfo = XString_create();
    self->m_authority = XString_create();
    self->m_port = -1;
    self->m_isValid = false;
}

void XUrl_init_ex(XUrl* self, const XString* urlString, XUrl_ParsingMode mode)
{
    XUrl_init(self);
    XUrl_setUrl(self, urlString, mode);
}

/* XUrl_copy / XUrl_move / XUrl_copy_base / XUrl_move_base / XUrl_deinit_base / XUrl_delete_base
   通过宏映射到 XClass_*_base，使用前需确保目标已初始化（XUrl_init） */

/* ========== 虚函数调度 ========== */
/* copy_base / move_base / deinit_base / delete_base 全部通过宏映射到 XClass_*_base */

/* ========== 设置 URL ========== */

void XUrl_setUrl(XUrl* self, const XString* urlString, XUrl_ParsingMode mode)
{
    if (!self) return;
    if (!urlString || XString_isEmpty_base(urlString)) {
        XString_clear_base(self->m_scheme);
        XString_clear_base(self->m_userName);
        XString_clear_base(self->m_password);
        XString_clear_base(self->m_host);
        XString_clear_base(self->m_path);
        XString_clear_base(self->m_query);
        XString_clear_base(self->m_fragment);
        self->m_port = -1;
        self->m_isValid = false;
        return;
    }
    parseUrl(self, XString_toUtf8(urlString), mode);
}

void XUrl_setEncodedUrl(XUrl* self, const XString* urlString, XUrl_ParsingMode mode)
{
    if (!urlString) return;
    /* 先解码再设置 */
    char decoded[4096];
    percentDecode(XString_toUtf8(urlString), decoded);
    XUrl_setUrl(self, urlString, mode);
}

/* ========== 查询方法 ========== */

bool XUrl_isValid(const XUrl* self)
{
    return self && self->m_isValid;
}

bool XUrl_isEmpty(const XUrl* self)
{
    if (!self) return true;
    return XString_size(self->m_scheme) == 0 &&
           XString_size(self->m_host) == 0 &&
           XString_size(self->m_path) == 0 &&
           XString_size(self->m_query) == 0 &&
           XString_size(self->m_fragment) == 0;
}

bool XUrl_isRelative(const XUrl* self)
{
    return self && XString_size(self->m_scheme) == 0;
}

bool XUrl_isLocalFile(const XUrl* self)
{
    if (!self) return false;
    const XString* scheme = XUrl_scheme_const(self);
    return XString_equals_utf8(scheme, "file", XChar_CaseSensitive);
}

/* ========== 组件访问 ========== */

const XString* XUrl_scheme_const(const XUrl* self)
{
    return self ? self->m_scheme : NULL;
}

void XUrl_setScheme(XUrl* self, const XString* scheme)
{
    if (!self) return;
    XString_clear_base(self->m_scheme);
    if (scheme) XString_append(self->m_scheme, scheme);
}

const XString* XUrl_userName_const(const XUrl* self)
{
    return self ? self->m_userName : NULL;
}

void XUrl_setUserName(XUrl* self, const XString* userName)
{
    if (!self) return;
    XString_clear_base(self->m_userName);
    if (userName) XString_append(self->m_userName, userName);
}

const XString* XUrl_password_const(const XUrl* self)
{
    return self ? self->m_password : NULL;
}

void XUrl_setPassword(XUrl* self, const XString* password)
{
    if (!self) return;
    XString_clear_base(self->m_password);
    if (password) XString_append(self->m_password, password);
}

const XString* XUrl_host_const(const XUrl* self)
{
    return self ? self->m_host : NULL;
}

void XUrl_setHost(XUrl* self, const XString* host)
{
    if (!self) return;
    XString_clear_base(self->m_host);
    if (host) XString_append(self->m_host, host);
}

int XUrl_port(const XUrl* self)
{
    return self ? self->m_port : -1;
}

void XUrl_setPort(XUrl* self, int port)
{
    if (self) self->m_port = port;
}

const XString* XUrl_path_const(const XUrl* self)
{
    return self ? self->m_path : NULL;
}

void XUrl_setPath(XUrl* self, const XString* path)
{
    if (!self) return;
    XString_clear_base(self->m_path);
    if (path) XString_append(self->m_path, path);
}

const XString* XUrl_query_const(const XUrl* self)
{
    return self ? self->m_query : NULL;
}

void XUrl_setQuery(XUrl* self, const XString* query)
{
    if (!self) return;
    XString_clear_base(self->m_query);
    if (query) XString_append(self->m_query, query);
}

const XString* XUrl_fragment_const(const XUrl* self)
{
    return self ? self->m_fragment : NULL;
}

void XUrl_setFragment(XUrl* self, const XString* fragment)
{
    if (!self) return;
    XString_clear_base(self->m_fragment);
    if (fragment) XString_append(self->m_fragment, fragment);
}


const XString* XUrl_userInfo_const(const XUrl* self)
{
    if (!self) return NULL;
    XString* result = ((XUrl*)self)->m_userInfo;
    if (!result) return NULL;
    XString_clear_base(result);
    const XString* user = XUrl_userName_const(self);
    const XString* pass = XUrl_password_const(self);
    bool hasUser = user && !XString_isEmpty_base(user);
    bool hasPass = pass && !XString_isEmpty_base(pass);
    if (hasUser) XString_append(result, user);
    if (hasUser && hasPass) XString_append_utf8(result, ":");
    if (hasPass) XString_append(result, pass);
    return result;
}

void XUrl_setUserInfo(XUrl* self, const XString* userInfo)
{
    if (!self) return;
    XString_clear_base(self->m_userName);
    XString_clear_base(self->m_password);
    if (!userInfo) return;
    const char* utf8 = XString_toUtf8(userInfo);
    const char* colon = strchr(utf8, ':');
    if (colon) {
        char user[256];
        size_t ulen = (size_t)(colon - utf8);
        if (ulen < sizeof(user)) {
            strncpy(user, utf8, ulen); user[ulen] = '\0';
            XString_append_utf8(self->m_userName, user);
        }
        XString_append_utf8(self->m_password, colon + 1);
    } else {
        XString_append(self->m_userName, userInfo);
    }
}

const XString* XUrl_authority_const(const XUrl* self)
{
    if (!self) return NULL;
    XString* result = ((XUrl*)self)->m_authority;
    if (!result) return NULL;
    XString_clear_base(result);
    const XString* user = XUrl_userName_const(self);
    const XString* pass = XUrl_password_const(self);
    const XString* host = XUrl_host_const(self);
    int port = XUrl_port(self);
    bool hasUser = user && !XString_isEmpty_base(user);
    bool hasPass = pass && !XString_isEmpty_base(pass);
    bool hasHost = host && !XString_isEmpty_base(host);
    if (hasUser) {
        XString_append(result, user);
        if (hasPass) {
            XString_append_utf8(result, ":");
            XString_append(result, pass);
        }
        XString_append_utf8(result, "@");
    }
    if (hasHost) {
        XString_append(result, host);
    }
    if (port >= 0) {
        char portBuf[16];
        snprintf(portBuf, sizeof(portBuf), ":%d", port);
        XString_append_utf8(result, portBuf);
    }
    return result;
}

XString* XUrl_toString(const XUrl* self)
{
    if (!self) return NULL;
    XString* result = XString_create();
    if (!result) return NULL;
    const XString* scheme = XUrl_scheme_const(self);
    const XString* authority = XUrl_authority_const(self);
    const XString* path = XUrl_path_const(self);
    const XString* query = XUrl_query_const(self);
    const XString* fragment = XUrl_fragment_const(self);
    bool hasScheme = scheme && !XString_isEmpty_base(scheme);
    if (hasScheme) {
        XString_append(result, scheme);
        XString_append_utf8(result, "://");
    }
    if (authority) XString_append(result, authority);
    if (path) XString_append(result, path);
    if (query && !XString_isEmpty_base(query)) {
        XString_append_utf8(result, "?");
        XString_append(result, query);
    }
    if (fragment && !XString_isEmpty_base(fragment)) {
        XString_append_utf8(result, "#");
        XString_append(result, fragment);
    }
    return result;
}

XString* XUrl_toEncoded(const XUrl* self)
{
    if (!self) return NULL;
    XString* plain = XUrl_toString(self);
    if (!plain) return NULL;
    XString* result = XString_create();
    if (!result) { XString_delete_base(plain); return NULL; }
    const char* utf8 = XString_toUtf8(plain);
    for (int i = 0; utf8[i]; i++) {
        char enc[4];
        int n = percentEncode(utf8[i], enc);
        XString_append_utf8(result, enc);
    }
    XString_delete_base(plain);
    return result;
}

XString* XUrl_toDisplayString(const XUrl* self, int options)
{
    if (!self) return NULL;
    XString* result = XUrl_toString(self);
    if (!result) return NULL;
    char* temp = XMemory_strdup(XString_toUtf8(result));
    if (!temp) { XString_delete_base(result); return NULL; }
    if (options & XUrl_RemoveScheme) {
        const XString* scheme = XUrl_scheme_const(self);
        if (scheme && !XString_isEmpty_base(scheme)) {
            char* sp = strstr(temp, "://");
            if (sp) {
                size_t remain = strlen(sp + 3) + 1;
                memmove(temp, sp + 3, remain);
            }
        }
    }
    if (options & XUrl_RemovePassword) {
        char* at = strchr(temp, '@');
        char* colon = strchr(temp, ':');
        if (colon && at && colon < at) {
            size_t remain = strlen(at) + 1;
            memmove(colon, at, remain);
        }
    }
    if (options & XUrl_RemoveFragment) {
        char* hash = strchr(temp, '#');
        if (hash) *hash = '\0';
    }
    if (options & XUrl_RemoveQuery) {
        char* qm = strchr(temp, '?');
        if (qm) *qm = '\0';
    }
    if (options & XUrl_StripTrailingSlash) {
        size_t len = strlen(temp);
        if (len > 0 && temp[len-1] == '/') temp[len-1] = '\0';
    }
    XString_assign_utf8(result, temp);
    XFree_System(temp);
    return result;
}
XUrl* XUrl_fromLocalFile(const XString* localfile)
{
    if (!localfile) return NULL;
    XString* urlBuf = XString_create();
    XString_append_utf8(urlBuf, "file:///");
    XString_append(urlBuf, localfile);
    XUrl* url = XUrl_create_ex(urlBuf, XUrl_TolerantMode);
    XString_delete_base(urlBuf);
    return url;
}


const XString* XUrl_toLocalFile_const(const XUrl* self)
{
    if (!self) return NULL;
    if (!XUrl_isLocalFile(self)) return NULL;
    const XString* path = XUrl_path_const(self);
    if (!path) return NULL;
    const char* utf8 = XString_toUtf8(path);
    if (utf8[0] == '/') utf8++;
    return XString_create_utf8(utf8);
}

void XUrl_resolved(const XUrl* self, const XString* relative, XUrl* out)
{
    if (!self || !out) return;
    if (!relative || XString_isEmpty_base(relative)) {
        XUrl_init(out);
        XUrl_copy_base(out, self);
        return;
    }
    const char* relUtf8 = XString_toUtf8(relative);
    if (strstr(relUtf8, "://")) {
        XUrl_setUrl(out, relative, XUrl_TolerantMode);
        return;
    }
    XString* base = XUrl_toString(self);
    if (!base) return;
    char* baseUtf8 = XMemory_strdup(XString_toUtf8(base));
    XString_delete_base(base);
    if (!baseUtf8) return;
    char* lastSlash = strrchr(baseUtf8, '/');
    size_t baseLength = strlen(baseUtf8);
    size_t relativeLength = strlen(relUtf8);
    size_t prefixLength;
    bool hasDirectoryPrefix = lastSlash && lastSlash > baseUtf8 + 6;
    if (hasDirectoryPrefix)
        prefixLength = (size_t)(lastSlash - baseUtf8) + 1;
    else
        prefixLength = baseLength + 1;
    if (prefixLength > SIZE_MAX - relativeLength - 1) {
        XFree_System(baseUtf8);
        return;
    }
    char* combinedUtf8 = (char*)XMalloc_System(prefixLength + relativeLength + 1);
    if (!combinedUtf8) {
        XFree_System(baseUtf8);
        return;
    }
    if (hasDirectoryPrefix)
        memcpy(combinedUtf8, baseUtf8, prefixLength);
    else {
        memcpy(combinedUtf8, baseUtf8, baseLength);
        combinedUtf8[baseLength] = '/';
    }
    memcpy(combinedUtf8 + prefixLength, relUtf8, relativeLength);
    combinedUtf8[prefixLength + relativeLength] = '\0';
    XString* combined = XString_create_utf8(combinedUtf8);
    XFree_System(combinedUtf8);
    XFree_System(baseUtf8);
    XUrl_setUrl(out, combined, XUrl_TolerantMode);
    XString_delete_base(combined);
}

XString* XUrl_toPercentEncoding(const XString* input, const XString* exclude, const XString* include)
{
    if (!input) return NULL;
    XString* result = XString_create();
    if (!result) return NULL;
    const char* inUtf8 = XString_toUtf8(input);
    const char* exUtf8 = exclude ? XString_toUtf8(exclude) : NULL;
    const char* inUtf8Inc = include ? XString_toUtf8(include) : NULL;
    for (int i = 0; inUtf8[i]; i++) {
        char c = inUtf8[i];
        bool shouldEncode = true;
        if (isUnreservedChar(c)) shouldEncode = false;
        if (exUtf8 && strchr(exUtf8, c)) shouldEncode = false;
        if (inUtf8Inc && strchr(inUtf8Inc, c)) shouldEncode = true;
        if (shouldEncode) {
            char enc[4];
            percentEncode(c, enc);
            XString_append_utf8(result, enc);
        } else {
            char str[2] = {c, '\0'};
            XString_append_utf8(result, str);
        }
    }
    return result;
}
XString* XUrl_fromPercentEncoding(const XString* input)
{
    if (!input) return NULL;
    char decoded[4096];
    percentDecode(XString_toUtf8(input), decoded);
    return XString_create_utf8(decoded);
}

bool XUrl_equals(const XUrl* a, const XUrl* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->m_port != b->m_port) return false;
    if (a->m_isValid != b->m_isValid) return false;
    return XString_equals(XUrl_scheme_const(a), XUrl_scheme_const(b), XChar_CaseSensitive) &&
           XString_equals(XUrl_host_const(a), XUrl_host_const(b), XChar_CaseSensitive) &&
           XString_equals(XUrl_path_const(a), XUrl_path_const(b), XChar_CaseSensitive) &&
           XString_equals(XUrl_query_const(a), XUrl_query_const(b), XChar_CaseSensitive) &&
           XString_equals(XUrl_fragment_const(a), XUrl_fragment_const(b), XChar_CaseSensitive) &&
           XString_equals(XUrl_userName_const(a), XUrl_userName_const(b), XChar_CaseSensitive) &&
           XString_equals(XUrl_password_const(a), XUrl_password_const(b), XChar_CaseSensitive);
}

void XUrl_swap(XUrl* a, XUrl* b)
{
    if (!a || !b) return;
    XUrl tmp = *a;
    *a = *b;
    *b = tmp;
    XClassSetVtable(a, XUrl);
    XClassSetVtable(b, XUrl);
}
