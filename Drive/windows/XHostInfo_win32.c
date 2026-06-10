// XHostInfo_win32.c
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "XHostInfo.h"
#include "XMemory.h"
#include "XString.h"
#include "XVector.h"
#include <string.h>

#pragma comment(lib, "ws2_32.lib")

// Winsock 清理函数
static void winsockCleanup(void) {
    WSACleanup();
}

// 确保Winsock已初始化
static void ensureWinsockInit(void) {
    static bool initialized = false;
    if (!initialized) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
            atexit(winsockCleanup);  // 注册程序退出时的清理函数
            initialized = true;
        }
    }
}

bool XHostInfo_platform_lookupName(const XString* name, XVector* addresses,
                                    XHostInfo_Error* error, XString** errorString) {
    if (!name || !addresses || !error) {
        if (error) *error = XHostInfo_UnknownError;
        return false;
    }
    
    ensureWinsockInit();
    
    struct addrinfo hints = {0};
    struct addrinfo* result = NULL;
    
    hints.ai_family = AF_UNSPEC;      // IPv4 或 IPv6
    hints.ai_socktype = SOCK_STREAM;  // TCP
    hints.ai_flags = AI_ADDRCONFIG;   // 只返回本地支持的地址类型
    
    int res = getaddrinfo(XString_toUtf8(name), NULL, &hints, &result);
    
    if (res != 0) {
        // 完善错误码映射
        switch (res) {
            case EAI_NONAME:  // WSAHOST_NOT_FOUND 与 EAI_NONAME 值相同
            case WSANO_DATA:
                *error = XHostInfo_HostNotFound;
                break;
            case WSATRY_AGAIN:
                // 临时错误（如 DNS 超时），可以重试
                *error = XHostInfo_UnknownError;
                break;
            case WSANO_RECOVERY:
                // 不可恢复的错误
                *error = XHostInfo_UnknownError;
                break;
            default:
                *error = XHostInfo_UnknownError;
                break;
        }
        
        if (errorString) {
            *errorString = XString_create_fmt_utf8(gai_strerrorA(res));
        }
        return false;
    }
    
    // 遍历结果，提取IP地址
    struct addrinfo* ptr = result;
    while (ptr != NULL) {
        XHostAddress addr;
        XHostAddress_init(&addr);
        
        if (ptr->ai_family == AF_INET) {
            // IPv4
            struct sockaddr_in* addr4 = (struct sockaddr_in*)ptr->ai_addr;
            XHostAddress_setAddressIPv4(&addr, ntohl(addr4->sin_addr.s_addr));
        } else if (ptr->ai_family == AF_INET6) {
            // IPv6
            struct sockaddr_in6* addr6 = (struct sockaddr_in6*)ptr->ai_addr;
            XHostAddress_setAddressIPv6(&addr, addr6->sin6_addr.s6_addr);
            XHostAddress_setScopeId(&addr, addr6->sin6_scope_id);
        }
        
        XVector_push_back_1_base(addresses, &addr);
        ptr = ptr->ai_next;
    }
    
    freeaddrinfo(result);
    
    *error = XHostInfo_NoError;
    return XVector_size_base(addresses) > 0;
}

XString* XHostInfo_platform_localHostName(void) {
    ensureWinsockInit();
    
    char buffer[256] = {0};
    if (gethostname(buffer, sizeof(buffer)) == 0) {
        return XString_create_utf8(buffer);
    }
    return NULL;
}

XString* XHostInfo_platform_localDomainName(void) {
    ensureWinsockInit();
    
    char hostname[256] = {0};
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        return NULL;
    }
    
    struct addrinfo hints = {0};
    struct addrinfo* result = NULL;
    
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_CANONNAME;
    
    if (getaddrinfo(hostname, NULL, &hints, &result) == 0) {
        if (result && result->ai_canonname) {
            char* dot = strchr(result->ai_canonname, '.');
            if (dot && dot[1] != '\0') {
                XString* domain = XString_create_utf8(dot + 1);
                freeaddrinfo(result);
                return domain;
            }
        }
        if (result) freeaddrinfo(result);
    }
    
    return NULL;
}

#endif // _WIN32