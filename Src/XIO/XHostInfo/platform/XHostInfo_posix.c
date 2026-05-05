#ifdef __linux__ || defined(__APPLE__) || defined(__BSD__)
// XHostInfo_posix.c
#include "XHostInfo_p.h"
#include "XMemory.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string.h>

static void appendAddressFromAddrInfo(XHostInfoPrivate* d, struct addrinfo* ai) {
    if (!d || !ai) return;
    XHostAddress* newAddrs = (XHostAddress*)XCalloc_System(
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

XHostInfo* XHostInfo_platform_fromName(const char* name) {
    XHostInfo* info = XHostInfo_create();
    if (!info || !name) {
        if (info) XHostInfo_setError(info, XHostInfo_HostNotFound);
        return info;
    }
    XHostInfo_setHostName(info, name);
    struct addrinfo hints = { 0 };
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    struct addrinfo* result = NULL;
    int ret = getaddrinfo(name, NULL, &hints, &result);
    if (ret != 0) {
        XHostInfo_setError(info, XHostInfo_HostNotFound);
        XHostInfo_setErrorString(info, gai_strerror(ret));
    }
    else {
        for (struct addrinfo* ai = result; ai; ai = ai->ai_next) {
            appendAddressFromAddrInfo(info->d, ai);
        }
        freeaddrinfo(result);
    }
    return info;
}
#endif