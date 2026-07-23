/******************************************************************************
 * @file       XUrl.c
 * @brief      XUrl URL 类实现（对标 Qt 6.8 QUrl）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XUrl.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

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
        out[1] = '\\0';
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
    dst[j] = '\\0';
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
    if (self->m_scheme) XString_clear(self->m_scheme);
    if (self->m_userName) XString_clear(self->m_userName);
    if (self->m_password) XString_clear(self->m_password);
    if (self->m_host) XString_clear(self->m_host);
    if (self->m_path) XString_clear(self->m_path);
    if (self->m_query) XString_clear(self->m_query);
    if (self->m_fragment) XString_clear(self->m_fragment);
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
            scheme[len] = '\\0';
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
                    strncpy(userName, p, ulen); userName[ulen] = '\\0';
                    strncpy(password, colonPos + 1, plen); password[plen] = '\\0';
                    XString_append_utf8(self->m_userName, userName);
                    XString_append_utf8(self->m_password, password);
                }
            } else {
                /* 只有用户名 */
                char userName[256];
                size_t ulen = (size_t)(atPos - p);
                if (ulen < sizeof(userName)) {
                    strncpy(userName, p, ulen); userName[ulen] = '\\0';
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
                    strncpy(host, hostStart + 1, hlen); host[hlen] = '\\0';
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
                    strncpy(host, hostStart, hlen); host[hlen] = '\\0';
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
                    strncpy(host, hostStart, hlen); host[hlen] = '\\0';
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
                strncpy(path, p, plen); path[plen] = '\\0';
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
                strncpy(path, p, plen); path[plen] = '\\0';
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
                strncpy(query, queryPos + 1, qlen); query[qlen] = '\\0';
                XString_append_utf8(self->m_query, query);
            }
        }
    }
    
    /* 5. 解析片段 */
    if (fragPos && fragPos + 1 < end) {
        char fragment[4096];
        size_t flen = strlen(fragPos + 1);
        if (flen < sizeof(fragment)) {
            strncpy(fragment, fragPos + 1, flen); fragment[flen] = '\\0';
            XString_append_utf8(self->m_fragment, fragment);
        }
    }
}

/* ========== 内部静态虚函数实现 ========== */

static void VXUrl_deinit(XUrl* self)
{
    if (!self) return;
    XString_delete(self->m_scheme);   self->m_scheme = NULL;
    XString_delete(self->m_userName); self->m_userName = NULL;
    XString_delete(self->m_password); self->m_password = NULL;
    XString_delete(self->m_host);     self->m_host = NULL;
    XString_delete(self->m_path);     self->m_path = NULL;
    XString_delete(self->m_query);    self->m_query = NULL;
    XString_delete(self->m_fragment); self->m_fragment = NULL;
}

static void VXUrl_copy(XUrl* dest, const XUrl* src)
{
    if (!dest || !src) return;
    if (src->m_scheme)   dest->m_scheme   = XString_create_copy(src->m_scheme);
    if (src->m_userName) dest->m_userName = XString_create_copy(src->m_userName);
    if (src->m_password) dest->m_password = XString_create_copy(src->m_password);
    if (src->m_host)     dest->m_host     = XString_create_copy(src->m_host);
    if (src->m_path)     dest->m_path     = XString_create_copy(src->m_path);
    if (src->m_query)    dest->m_query    = XString_create_copy(src->m_query);
    if (src->m_fragment) dest->m_fragment = XString_create_copy(src->m_fragment);
    dest->m_port = src->m_port;
    dest->m_isValid = src->m_isValid;
}

static void VXUrl_move(XUrl* dest, XUrl* src)
{
    if (!dest || !src) return;
    /* 转移字符串所有权 */
    dest->m_scheme   = src->m_scheme;   src->m_scheme = NULL;
    dest->m_userName = src->m_userName; src->m_userName = NULL;
    dest->m_password = src->m_password; src->m_password = NULL;
    dest->m_host     = src->m_host;     src->m_host = NULL;
    dest->m_path     = src->m_path;     src->m_path = NULL;
    dest->m_query    = src->m_query;    src->m_query = NULL;
    dest->m_fragment = src->m_fragment; src->m_fragment = NULL;
    dest->m_port = src->m_port;
    dest->m_isValid = src->m_isValid;
    src->m_port = -1;
    src->m_isValid = false;
}

/* ========== 虚函数表初始化 ========== */

XVtable* XUrl_class_init(void)
{
    XVTABLE_CREAT_DEFAULT;
    XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_SIZE);
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

XUrl* XUrl_create_ex(const char* urlString, XUrl_ParsingMode mode)
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
    self->m_port = -1;
    self->m_isValid = false;
}

void XUrl_init_ex(XUrl* self, const char* urlString, XUrl_ParsingMode mode)
{
    XUrl_init(self);
    XUrl_setUrl(self, urlString, mode);
}

void XUrl_copy(XUrl* self, const XUrl* other)
{
    if (!self) return;
    XUrl_init(self);
    XUrl_copy_base(self, other);
}

void XUrl_move(XUrl* self, XUrl* other)
{
    if (!self) return;
    XUrl_init(self);
    XUrl_move_base(self, other);
}

void XUrl_deinit(XUrl* self)
{
    if (!self) return;
    VXUrl_deinit(self);
    XClass_deinit_base((XClass*)self);
}

void XUrl_delete(XUrl* self)
{
    if (self) XUrl_delete_base(self);
}

/* ========== 虚函数调度 ========== */

void XUrl_copy_base(XUrl* dest, const XUrl* src)
{
    if (ISNULL(dest, "XUrl") || ISNULL(src, "XUrl")) return;
    void (*func)(XUrl*, const XUrl*) = XClassGetVirtualFunc(dest, EXClass_Copy, void(*)(XUrl*, const XUrl*));
    if (func) func(dest, src);
}

void XUrl_move_base(XUrl* dest, XUrl* src)
{
    if (ISNULL(dest, "XUrl") || ISNULL(src, "XUrl")) return;
    void (*func)(XUrl*, XUrl*) = XClassGetVirtualFunc(dest, EXClass_Move, void(*)(XUrl*, XUrl*));
    if (func) func(dest, src);
}

void XUrl_deinit_base(XUrl* self)
{
    if (ISNULL(self, "XUrl")) return;
    void (*func)(XUrl*) = XClassGetVirtualFunc(self, EXClass_Deinit, void(*)(XUrl*));
    if (func) func(self);
}

void XUrl_delete_base(XUrl* self)
{
    if (ISNULL(self, "XUrl")) return;
    XUrl_deinit_base(self);
    XClass_delete_base((XClass*)self);
}

/* ========== 设置 URL ========== */

void XUrl_setUrl(XUrl* self, const char* urlString, XUrl_ParsingMode mode)
{
    if (!self) return;
    if (!urlString || !urlString[0]) {
        XString_clear(self->m_scheme);
        XString_clear(self->m_userName);
        XString_clear(self->m_password);
        XString_clear(self->m_host);
        XString_clear(self->m_path);
        XString_clear(self->m_query);
        XString_clear(self->m_fragment);
        self->m_port = -1;
        self->m_isValid = false;
        return;
    }
    parseUrl(self, urlString, mode);
}

void XUrl_setEncodedUrl(XUrl* self, const char* urlString, XUrl_ParsingMode mode)
{
    /* 先解码再设置 */
    char decoded[4096];
    percentDecode(urlString, decoded);
    XUrl_setUrl(self, decoded, mode);
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
    const char* scheme = XUrl_scheme(self);
    return strcmp(scheme, "file") == 0;
}

/* ========== 组件访问 ========== */

const char* XUrl_scheme(const XUrl* self)
{
    return (self && self->m_scheme) ? XString_data(self->m_scheme) : "";
}

void XUrl_setScheme(XUrl* self, const char* scheme)
{
    if (!self) return;
    XString_clear(self->m_scheme);
    if (scheme) XString_append_utf8(self->m_scheme, scheme);
}

const char* XUrl_userName(const XUrl* self)
{
    return (self && self->m_userName) ? XString_data(self->m_userName) : "";
}

void XUrl_setUserName(XUrl* self, const char* userName)
{
    if (!self) return;
    XString_clear(self->m_userName);
    if (userName) XString_append_utf8(self->m_userName, userName);
}

const char* XUrl_password(const XUrl* self)
{
    return (self && self->m_password) ? XString_data(self->m_password) : "";
}

void XUrl_setPassword(XUrl* self, const char* password)
{
    if (!self) return;
    XString_clear(self->m_password);
    if (password) XString_append_utf8(self->m_password, password);
}

const char* XUrl_host(const XUrl* self)
{
    return (self && self->m_host) ? XString_data(self->m_host) : "";
}

void XUrl_setHost(XUrl* self, const char* host)
{
    if (!self) return;
    XString_clear(self->m_host);
    if (host) XString_append_utf8(self->m_host, host);
}

int XUrl_port(const XUrl* self)
{
    return self ? self->m_port : -1;
}

void XUrl_setPort(XUrl* self, int port)
{
    if (self) self->m_port = port;
}

const char* XUrl_path(const XUrl* self)
{
    return (self && self->m_path) ? XString_data(self->m_path) : "";
}

void XUrl_setPath(XUrl* self, const char* path)
{
    if (!self) return;
    XString_clear(self->m_path);
    if (path) XString_append_utf8(self->m_path, path);
}

const char* XUrl_query(const XUrl* self)
{
    return (self && self->m_query) ? XString_data(self->m_query) : "";
}

void XUrl_setQuery(XUrl* self, const char* query)
{
    if (!self) return;
    XString_clear(self->m_query);
    if (query) XString_append_utf8(self->m_query, query);
}

const char* XUrl_fragment(const XUrl* self)
{
    return (self && self->m_fragment) ? XString_data(self->m_fragment) : "";
}

void XUrl_setFragment(XUrl* self, const char* fragment)
{
    if (!self) return;
    XString_clear(self->m_fragment);
    if (fragment) XString_append_utf8(self->m_fragment, fragment);
}

const char* XUrl_userInfo(const XUrl* self)
{
    if (!self) return "";
    static char buf[512];
    const char* user = XUrl_userName(self);
    const char* pass = XUrl_password(self);
    if (user[0] && pass[0]) {
        snprintf(buf, sizeof(buf), "%s:%s", user, pass);
    } else if (user[0]) {
        strncpy(buf, user, sizeof(buf));
    } else {
        buf[0] = '\\0';
    }
    return buf;
}

void XUrl_setUserInfo(XUrl* self, const char* userInfo)
{
    if (!self) return;
    XString_clear(self->m_userName);
    XString_clear(self->m_password);
    if (!userInfo) return;
    const char* colon = strchr(userInfo, ':');
    if (colon) {
        char user[256];
        size_t ulen = (size_t)(colon - userInfo);
        if (ulen < sizeof(user)) {
            strncpy(user, userInfo, ulen); user[ulen] = '\\0';
            XString_append_utf8(self->m_userName, user);
        }
        XString_append_utf8(self->m_password, colon + 1);
    } else {
        XString_append_utf8(self->m_userName, userInfo);
    }
}

const char* XUrl_authority(const XUrl* self)
{
    if (!self) return "";
    static char buf[1024];
    buf[0] = '\\0';
    const char* user = XUrl_userName(self);
    const char* pass = XUrl_password(self);
    const char* host = XUrl_host(self);
    int port = XUrl_port(self);
    
    if (user[0] || pass[0]) {
        if (pass[0])
            snprintf(buf, sizeof(buf), "%s:%s@", user, pass);
        else
            snprintf(buf, sizeof(buf), "%s@", user);
    }
    size_t len = strlen(buf);
    if (host[0]) {
        strncat(buf, host, sizeof(buf) - len - 1);
        len = strlen(buf);
    }
    if (port >= 0) {
        snprintf(buf + len, sizeof(buf) - len, ":%d", port);
    }
    return buf;
}

/* ========== 转换方法 ========== */

char* XUrl_toString(const XUrl* self, char* out, size_t size)
{
    if (!self || !out || size == 0) return out;
    out[0] = '\\0';
    const char* scheme = XUrl_scheme(self);
    const char* authority = XUrl_authority(self);
    const char* path = XUrl_path(self);
    const char* query = XUrl_query(self);
    const char* fragment = XUrl_fragment(self);
    
    if (scheme[0]) {
        snprintf(out, size, "%s://%s%s", scheme, authority, path);
    } else {
        snprintf(out, size, "%s%s", authority, path);
    }
    size_t len = strlen(out);
    if (query[0] && len < size - 1) {
        snprintf(out + len, size - len, "?%s", query);
        len = strlen(out);
    }
    if (fragment[0] && len < size - 1) {
        snprintf(out + len, size - len, "#%s", fragment);
    }
    return out;
}

char* XUrl_toEncoded(const XUrl* self, char* out, size_t size)
{
    /* 先获取普通字符串，再编码 */
    char plain[4096];
    XUrl_toString(self, plain, sizeof(plain));
    if (!out || size == 0) return out;
    int j = 0;
    for (int i = 0; plain[i] && j < (int)size - 4; i++) {
        char enc[4];
        int n = percentEncode(plain[i], enc);
        if (j + n < (int)size) {
            memcpy(out + j, enc, (size_t)n);
            j += n;
        } else break;
    }
    out[j] = '\\0';
    return out;
}

char* XUrl_toDisplayString(const XUrl* self, int options, char* out, size_t size)
{
    if (!self || !out || size == 0) return out;
    /* 对于简单实现，先获取 toString，再根据选项处理 */
    char buf[4096];
    XUrl_toString(self, buf, sizeof(buf));
    
    /* 应用选项 */
    char temp[4096];
    strncpy(temp, buf, sizeof(temp));
    temp[sizeof(temp) - 1] = '\\0';
    
    if (options & XUrl_RemoveScheme) {
        const char* scheme = XUrl_scheme(self);
        if (scheme[0]) {
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
        if (hash) *hash = '\\0';
    }
    if (options & XUrl_RemoveQuery) {
        char* qm = strchr(temp, '?');
        if (qm) *qm = '\\0';
    }
    if (options & XUrl_StripTrailingSlash) {
        size_t len = strlen(temp);
        if (len > 0 && temp[len-1] == '/') temp[len-1] = '\\0';
    }
    
    strncpy(out, temp, size);
    out[size-1] = '\\0';
    return out;
}

/* ========== 静态方法 ========== */

XUrl* XUrl_fromLocalFile(const char* localfile)
{
    if (!localfile) return NULL;
    char urlBuf[4096];
    snprintf(urlBuf, sizeof(urlBuf), "file:///%s", localfile);
    return XUrl_create_ex(urlBuf, XUrl_TolerantMode);
}

const char* XUrl_toLocalFile(const XUrl* self)
{
    if (!self) return "";
    static char buf[4096];
    buf[0] = '\\0';
    if (!XUrl_isLocalFile(self)) return buf;
    const char* path = XUrl_path(self);
    if (path[0] == '/') path++;
    strncpy(buf, path, sizeof(buf));
    buf[sizeof(buf) - 1] = '\\0';
    return buf;
}

void XUrl_resolved(const XUrl* self, const char* relative, XUrl* out)
{
    if (!self || !out) return;
    if (!relative || !relative[0]) {
        XUrl_copy(out, self);
        return;
    }
    /* 简单实现：如果 relative 以 http:// 等开头，直接解析 */
    if (strstr(relative, "://")) {
        XUrl_setUrl(out, relative, XUrl_TolerantMode);
        return;
    }
    /* 否则拼接路径 */
    char base[4096];
    XUrl_toString(self, base, sizeof(base));
    /* 移除末尾的路径部分 */
    char* lastSlash = strrchr(base, '/');
    if (lastSlash && lastSlash > base + 6) {
        *(lastSlash + 1) = '\\0';
    } else {
        strcat(base, "/");
    }
    strncat(base, relative, sizeof(base) - strlen(base) - 1);
    XUrl_setUrl(out, base, XUrl_TolerantMode);
}

XString* XUrl_toPercentEncoding(const char* input, const char* exclude, const char* include)
{
    if (!input) return NULL;
    XString* result = XString_create();
    if (!result) return NULL;
    
    for (int i = 0; input[i]; i++) {
        char c = input[i];
        bool shouldEncode = true;
        if (isUnreservedChar(c)) shouldEncode = false;
        if (exclude && strchr(exclude, c)) shouldEncode = false;
        if (include && strchr(include, c)) shouldEncode = true;
        
        if (shouldEncode) {
            char enc[4];
            percentEncode(c, enc);
            XString_append_utf8(result, enc);
        } else {
            char str[2] = {c, '\\0'};
            XString_append_utf8(result, str);
        }
    }
    return result;
}

XString* XUrl_fromPercentEncoding(const char* input)
{
    if (!input) return NULL;
    char decoded[4096];
    percentDecode(input, decoded);
    return XString_create_utf8(decoded);
}

bool XUrl_equals(const XUrl* a, const XUrl* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->m_port != b->m_port) return false;
    if (a->m_isValid != b->m_isValid) return false;
    return strcmp(XUrl_scheme(a), XUrl_scheme(b)) == 0 &&
           strcmp(XUrl_host(a), XUrl_host(b)) == 0 &&
           strcmp(XUrl_path(a), XUrl_path(b)) == 0 &&
           strcmp(XUrl_query(a), XUrl_query(b)) == 0 &&
           strcmp(XUrl_fragment(a), XUrl_fragment(b)) == 0 &&
           strcmp(XUrl_userName(a), XUrl_userName(b)) == 0 &&
           strcmp(XUrl_password(a), XUrl_password(b)) == 0;
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
