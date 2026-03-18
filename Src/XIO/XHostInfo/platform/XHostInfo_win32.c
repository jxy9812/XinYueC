#ifdef WIN32
// XHostInfo_win32.c
// Windows-specific implementation of XHostInfo using GetAddrInfoW.
// Copyright (C) 2026 Your Project Authors
// SPDX-License-Identifier: MIT OR LGPL-3.0-only

#include "XHostInfo_p.h"
#include "XMemory.h"
#include "XString.h"
#include <winsock2.h>
#include <ws2tcpip.h>
//#include <windows.h>

// Ensure Winsock is initialized once
static BOOL g_winsock_initialized = FALSE;
static CRITICAL_SECTION g_init_lock;

static void ensure_winsock_initialized(void) {
    EnterCriticalSection(&g_init_lock);
    if (!g_winsock_initialized) {
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0) {
            g_winsock_initialized = TRUE;
        }
    }
    LeaveCriticalSection(&g_init_lock);
}

static void cleanup_winsock(void) {
    EnterCriticalSection(&g_init_lock);
    if (g_winsock_initialized) {
        WSACleanup();
        g_winsock_initialized = FALSE;
    }
    LeaveCriticalSection(&g_init_lock);
}

// Helper: Convert wide string to UTF-8
static char* wide_to_utf8(const wchar_t* wstr) {
    if (!wstr) return NULL;
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 0) return NULL;
    char* utf8 = (char*)XMemory_malloc(len);
    if (!utf8) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, utf8, len, NULL, NULL);
    return utf8;
}

// Helper: Append address from ADDRINFOW to XHostInfoPrivate
static void appendAddressFromAddrInfoW(XHostInfoPrivate* d, ADDRINFOW* ai) {
    if (!d || !ai) return;
    XHostAddress* newAddrs = (XHostAddress*)XMemory_realloc(
        d->addresses, (d->addressCount + 1) * sizeof(XHostAddress));
    if (!newAddrs) return;
    d->addresses = newAddrs;
    XHostAddress* addr = &d->addresses[d->addressCount];
    XHostAddress_init(addr);
    if (ai->ai_family == AF_INET) {
        struct sockaddr_in* sin = (struct sockaddr_in*)ai->ai_addr;
        uint32_t ip = ntohl(sin->sin_addr.s_addr);
        XHostAddress_setAddressIPv4(addr, ip);
    }
    else if (ai->ai_family == AF_INET6) {
        struct sockaddr_in6* sin6 = (struct sockaddr_in6*)ai->ai_addr;
        XHostAddress_setAddressIPv6(addr, sin6->sin6_addr.s6_addr);
    }
    d->addressCount++;
}

// Platform-specific DNS lookup
XHostInfo* XHostInfo_platform_fromName(const char* name) {
    // Initialize Winsock on first use
    static BOOL init_done = FALSE;
    if (!init_done) {
        InitializeCriticalSection(&g_init_lock);
        atexit(cleanup_winsock); // Ensure cleanup on exit
        init_done = TRUE;
    }
    ensure_winsock_initialized();

    XHostInfo* info = XHostInfo_create();
    if (!info || !name) {
        if (info) XHostInfo_setError(info, XHostInfo_HostNotFound);
        return info;
    }
    XHostInfo_setHostName(info, name);

    // Convert UTF-8 name to wide string
    int wlen = MultiByteToWideChar(CP_UTF8, 0, name, -1, NULL, 0);
    if (wlen <= 0) {
        XHostInfo_setError(info, XHostInfo_UnknownError);
        XHostInfo_setErrorString(info, "Failed to convert hostname to wide string");
        return info;
    }
    wchar_t* wname = (wchar_t*)XMemory_malloc(wlen * sizeof(wchar_t));
    if (!wname) {
        XHostInfo_setError(info, XHostInfo_UnknownError);
        XHostInfo_setErrorString(info, "Out of memory");
        return info;
    }
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wname, wlen);

    // Perform DNS lookup
    ADDRINFOW hints = { 0 };
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    ADDRINFOW* result = NULL;
    int ret = GetAddrInfoW(wname, NULL, &hints, &result);
    XMemory_free(wname);

    if (ret != 0) {
        // Map Windows error to human-readable string
        wchar_t wmsg[256] = { 0 };
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, ret, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            wmsg, sizeof(wmsg) / sizeof(wchar_t), NULL);
        char* msg = wide_to_utf8(wmsg);
        XHostInfo_setError(info, XHostInfo_HostNotFound);
        if (msg) {
            XHostInfo_setErrorString(info, msg);
            XMemory_free(msg);
        }
        else {
            XHostInfo_setErrorString(info, "DNS lookup failed");
        }
    }
    else {
        for (ADDRINFOW* ai = result; ai; ai = ai->ai_next) {
            appendAddressFromAddrInfoW(info->d, ai);
        }
        FreeAddrInfoW(result);
    }

    return info;
}
#endif