// XHostAddress.c
#include "XHostAddress.h"
#include "XMemory.h"
#include "XString.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

// ==================== 工具函数 ====================

static uint32_t ipv4FromBytes(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
}

static void ipv4ToBytes(uint32_t ip, uint8_t* a, uint8_t* b, uint8_t* c, uint8_t* d) {
    *a = (ip >> 24) & 0xFF;
    *b = (ip >> 16) & 0xFF;
    *c = (ip >> 8) & 0xFF;
    *d = ip & 0xFF;
}

static void ipv4ToIpv6Mapped(uint8_t out[16], uint32_t ipv4) {
    static const uint8_t prefix[12] = { 0,0,0,0,0,0,0,0,0,0,0xff,0xff };
    memcpy(out, prefix, 12);
    ipv4ToBytes(ipv4, &out[12], &out[13], &out[14], &out[15]);
}

static uint32_t ipv6MappedToIpv4(const uint8_t ip6[16]) {
    return ipv4FromBytes(ip6[12], ip6[13], ip6[14], ip6[15]);
}

static bool isIPv4MappedAddress(const uint8_t ip6[16]) {
    static const uint8_t prefix[12] = { 0,0,0,0,0,0,0,0,0,0,0xff,0xff };
    return memcmp(ip6, prefix, 12) == 0;
}

// ==================== IPv4 解析 ====================

static bool parseIPv4(const char* src, uint32_t* out) {
    if (!src || !out) return false;
    unsigned int a, b, c, d;
    if (sscanf(src, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    *out = ipv4FromBytes((uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d);
    return true;
}

// ==================== IPv6 解析（支持 :: 和 %zone）====================

static bool parseIPv6(const char* src, uint8_t dst[16], char scopeOut[64]) {
    if (!src || !dst) return false;
    memset(dst, 0, 16);
    if (scopeOut) scopeOut[0] = '\0';

    char buf[64];
    size_t len = strlen(src);
    if (len >= sizeof(buf)) return false;
    strcpy(buf, src);

    // 提取 scope ID
    char* percent = strchr(buf, '%');
    if (percent) {
        if (scopeOut) {
            size_t slen = strlen(percent + 1);
            if (slen >= 64) slen = 63;
            memcpy(scopeOut, percent + 1, slen);
            scopeOut[slen] = '\0';
        }
        *percent = '\0';
    }

    // 去掉方括号
    char* s = buf;
    if (*s == '[') {
        s++;
        char* end = strchr(s, ']');
        if (end) *end = '\0';
    }

    // 快速处理常见情况
    if (strcmp(s, "::1") == 0) {
        dst[15] = 1;
        return true;
    }
    if (strcmp(s, "::") == 0) {
        return true;
    }

    // 分段解析
    int parts[8] = { 0 };
    int count = 0;
    char temp[64];
    strcpy(temp, s);
    char* token = strtok(temp, ":");
    while (token && count < 8) {
        if (strlen(token) == 0) {
            // 遇到 "::"，停止计数
            break;
        }
        char* endptr;
        unsigned long val = strtoul(token, &endptr, 16);
        if (*endptr != '\0' || val > 0xFFFF) return false;
        parts[count++] = (int)val;
        token = strtok(NULL, ":");
    }

    if (count == 8) {
        for (int i = 0; i < 8; i++) {
            dst[i * 2] = (parts[i] >> 8) & 0xFF;
            dst[i * 2 + 1] = parts[i] & 0xFF;
        }
        return true;
    }

    // 简化：不支持压缩格式以外的复杂 IPv6（如 "1::2:3"）
    // 若需完全兼容 RFC 4291，可后续增强
    return false;
}

// ==================== 地址分类 ====================

static bool isIPv4Loopback(uint32_t ip) { return (ip & 0xFF000000U) == 0x7F000000U; }
static bool isIPv4Multicast(uint32_t ip) { return (ip & 0xF0000000U) == 0xE0000000U; }
static bool isIPv4LinkLocal(uint32_t ip) { return (ip & 0xFFFF0000U) == 0xA9FE0000U; }
static bool isIPv4Broadcast(uint32_t ip) { return ip == 0xFFFFFFFFU; }
static bool isIPv4Global(uint32_t ip) {
    if (isIPv4Loopback(ip) || isIPv4LinkLocal(ip) || isIPv4Broadcast(ip)) return false;
    if ((ip & 0xFF000000U) == 0x0A000000U) return false; // 10.0.0.0/8
    if ((ip & 0xFFF00000U) == 0xAC100000U) return false; // 172.16.0.0/12
    if ((ip & 0xFFFF0000U) == 0xC0A80000U) return false; // 192.168.0.0/16
    return true;
}

static bool isIPv6Loopback(const uint8_t ip6[16]) {
    static const uint8_t loopback[16] = { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 };
    return memcmp(ip6, loopback, 16) == 0;
}
static bool isIPv6Multicast(const uint8_t ip6[16]) { return ip6[0] == 0xFF; }
static bool isIPv6LinkLocal(const uint8_t ip6[16]) { return (ip6[0] == 0xFE) && ((ip6[1] & 0xC0) == 0x80); }
static bool isIPv6SiteLocal(const uint8_t ip6[16]) { return (ip6[0] == 0xFE) && ((ip6[1] & 0xC0) == 0xC0); }
static bool isIPv6UniqueLocal(const uint8_t ip6[16]) { return (ip6[0] == 0xFD) || (ip6[0] == 0xFC); }
static bool isIPv6Global(const uint8_t ip6[16]) {
    if (isIPv6Loopback(ip6) || isIPv6LinkLocal(ip6) ||
        isIPv6SiteLocal(ip6) || isIPv6UniqueLocal(ip6) ||
        isIPv6Multicast(ip6)) return false;
    if (memcmp(ip6, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16) == 0) return false;
    if (isIPv4MappedAddress(ip6)) return false;
    return true;
}

// ==================== 虚函数 ====================

static void VXHostAddress_copy(XHostAddress* self, const XHostAddress* other) {
    if (!self || !other) return;
    memcpy(self->a6, other->a6, 16);
    self->protocol = other->protocol;
    strcpy(self->scopeId, other->scopeId);
    self->isNull = other->isNull;
}

static void VXHostAddress_move(XHostAddress* self, XHostAddress* other) {
    if (!self || !other) return;
    memcpy(self, other, sizeof(XHostAddress));
}

static void VXHostAddress_deinit(XHostAddress* self) {
    (void)self; // 无动态资源
}

bool XHostAddress_operator_equal(const XHostAddress* a, const XHostAddress* b) {
    if (!a || !b) return false;
    if (a->protocol != b->protocol) return false;
    if (memcmp(a->a6, b->a6, 16) != 0) return false;
    return strcmp(a->scopeId, b->scopeId) == 0;
}

int XHostAddress_operator_compare(const XHostAddress* a, const XHostAddress* b) {
    if (!a || !b) return (a != NULL) - (b != NULL);
    if (a->protocol != b->protocol) return (int)a->protocol - (int)b->protocol;
    int cmp = memcmp(a->a6, b->a6, 16);
    if (cmp != 0) return cmp;
    return strcmp(a->scopeId, b->scopeId);
}

// ==================== 虚函数表 ====================

XVtable* XHostAddress_class_init(void) 
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XHostAddress))
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        XVTABLE_INHERIT_XCLASS(XClass);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHostAddress_copy);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHostAddress_move);
        XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHostAddress_deinit);
#if SHOWCONTAINERSIZE
        printf("XHostAddress size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
        return XVTABLE_DEFAULT;
}

// ==================== 初始化 ====================

void XHostAddress_init(XHostAddress* addr) {
    if (!addr) return;
    memset(addr, 0, sizeof(XHostAddress));
    XClass_init(addr);
    XClassGetVtable(addr) = XHostAddress_class_init();
    addr->protocol = XHostAddress_UnknownNetworkLayerProtocol;
    addr->isNull = true;
}

// ==================== 构造函数 ====================

XHostAddress* XHostAddress_create(void) {
    XHostAddress* addr = (XHostAddress*)XMalloc_System(sizeof(XHostAddress));
    if (addr) XHostAddress_init(addr);
    Set_Class_MemoryFree(addr, XFree_System);
    return addr;
}

XHostAddress* XHostAddress_create_copy(const XHostAddress* other) {
    XHostAddress* addr = XHostAddress_create();
    if (addr && other) XClass_copy_base(addr, other);
    return addr;
}

XHostAddress* XHostAddress_create_fromString(const char* address) {
    XHostAddress* addr = XHostAddress_create();
    if (addr && address) XHostAddress_setAddress(addr, address);
    return addr;
}

XHostAddress* XHostAddress_create_fromIPv4Address(uint32_t ip) {
    XHostAddress* addr = XHostAddress_create();
    if (addr) XHostAddress_setAddressIPv4(addr, ip);
    return addr;
}

XHostAddress* XHostAddress_create_fromIPv6Address(const uint8_t ip[16]) {
    XHostAddress* addr = XHostAddress_create();
    if (addr && ip) XHostAddress_setAddressIPv6(addr, ip);
    return addr;
}

XHostAddress* XHostAddress_create_fromSpecial(XHostAddress_SpecialAddress special) {
    XHostAddress* addr = XHostAddress_create();
    if (addr) XHostAddress_setAddressSpecial(addr, special);
    return addr;
}

// ==================== 设置函数 ====================

void XHostAddress_setAddress(XHostAddress* addr, const char* address) {
    if (!addr || !address) {
        XHostAddress_setAddressSpecial(addr, XHostAddress_NullSpecial);
        return;
    }

    uint32_t ipv4;
    if (parseIPv4(address, &ipv4)) {
        XHostAddress_setAddressIPv4(addr, ipv4);
        return;
    }

    char scope[64] = { 0 };
    if (parseIPv6(address, addr->a6, scope)) {
        addr->protocol = XHostAddress_IPv6Protocol;
        strcpy(addr->scopeId, scope);
        addr->isNull = false;
        return;
    }

    XHostAddress_setAddressSpecial(addr, XHostAddress_NullSpecial);
}

void XHostAddress_setAddressIPv4(XHostAddress* addr, uint32_t ip) {
    if (!addr) return;
    ipv4ToIpv6Mapped(addr->a6, ip);
    addr->protocol = XHostAddress_IPv4Protocol;
    addr->scopeId[0] = '\0';
    addr->isNull = false;
}

void XHostAddress_setAddressIPv6(XHostAddress* addr, const uint8_t ip[16]) {
    if (!addr || !ip) return;
    memcpy(addr->a6, ip, 16);
    addr->protocol = XHostAddress_IPv6Protocol;
    addr->scopeId[0] = '\0';
    addr->isNull = false;
}

void XHostAddress_setAddressSpecial(XHostAddress* addr, XHostAddress_SpecialAddress special) {
    if (!addr) return;
    memset(addr->a6, 0, 16);
    addr->scopeId[0] = '\0';
    addr->isNull = false;

    switch (special) {
    case XHostAddress_NullSpecial:
        addr->protocol = XHostAddress_UnknownNetworkLayerProtocol;
        addr->isNull = true;
        break;
    case XHostAddress_AnySpecial:
    case XHostAddress_AnyIPv4Special:
        ipv4ToIpv6Mapped(addr->a6, 0);
        addr->protocol = XHostAddress_IPv4Protocol;
        break;
    case XHostAddress_AnyIPv6Special:
        addr->protocol = XHostAddress_IPv6Protocol;
        break;
    case XHostAddress_LocalHostSpecial:
        ipv4ToIpv6Mapped(addr->a6, 0x7F000001U);
        addr->protocol = XHostAddress_IPv4Protocol;
        break;
    case XHostAddress_LocalHostIPv6Special:
        addr->a6[15] = 1;
        addr->protocol = XHostAddress_IPv6Protocol;
        break;
    case XHostAddress_BroadcastSpecial:
        ipv4ToIpv6Mapped(addr->a6, 0xFFFFFFFFU);
        addr->protocol = XHostAddress_IPv4Protocol;
        break;
    case XHostAddress_AnyAllSpecial:
        addr->protocol = XHostAddress_IPv6Protocol;
        break;
    }
}

void XHostAddress_setScopeId(XHostAddress* addr, const char* id) {
    if (!addr || addr->protocol != XHostAddress_IPv6Protocol) return;
    if (id) {
        size_t len = strlen(id);
        if (len >= 64) len = 63;
        memcpy(addr->scopeId, id, len);
        addr->scopeId[len] = '\0';
    }
    else {
        addr->scopeId[0] = '\0';
    }
}

// ==================== 查询函数 ====================

bool XHostAddress_isNull(const XHostAddress* addr) {
    return !addr || addr->isNull;
}

XHostAddress_NetworkLayerProtocol XHostAddress_protocol(const XHostAddress* addr) {
    return addr ? addr->protocol : XHostAddress_UnknownNetworkLayerProtocol;
}

uint32_t XHostAddress_toIPv4Address(const XHostAddress* addr) {
    if (!addr || addr->protocol != XHostAddress_IPv4Protocol) return 0;
    return ipv6MappedToIpv4(addr->a6);
}

void XHostAddress_toIPv6Address(const XHostAddress* addr, uint8_t out[16]) {
    if (!addr || !out) {
        if (out) memset(out, 0, 16);
        return;
    }
    memcpy(out, addr->a6, 16);
}

const char* XHostAddress_scopeId(const XHostAddress* addr) {
    return addr ? addr->scopeId : "";
}

XString* XHostAddress_toString(const XHostAddress* addr) {
    if (!addr || addr->isNull) {
        return NULL;
    }

    char buffer[128];
    int len = 0;

    if (addr->protocol == XHostAddress_IPv4Protocol) {
        uint32_t ip = ipv6MappedToIpv4(addr->a6);
        len = snprintf(buffer, sizeof(buffer), "%d.%d.%d.%d",
            (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
            (ip >> 8) & 0xFF, ip & 0xFF);
    }
    else if (addr->protocol == XHostAddress_IPv6Protocol) {
        // 简化输出（不压缩零段，但功能正确）
        len = snprintf(buffer, sizeof(buffer),
            "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
            addr->a6[0], addr->a6[1], addr->a6[2], addr->a6[3],
            addr->a6[4], addr->a6[5], addr->a6[6], addr->a6[7],
            addr->a6[8], addr->a6[9], addr->a6[10], addr->a6[11],
            addr->a6[12], addr->a6[13], addr->a6[14], addr->a6[15]);
        if (addr->scopeId[0]) {
            len += snprintf(buffer + len, sizeof(buffer) - len, "%%%s", addr->scopeId);
        }
    }

    if (len <= 0) return NULL;
    return XString_create_with_length_utf8(buffer, len);
}

bool XHostAddress_isLoopback(const XHostAddress* addr) {
    if (!addr || addr->isNull) return false;
    if (addr->protocol == XHostAddress_IPv4Protocol) {
        return isIPv4Loopback(ipv6MappedToIpv4(addr->a6));
    }
    if (addr->protocol == XHostAddress_IPv6Protocol) {
        return isIPv6Loopback(addr->a6);
    }
    return false;
}

bool XHostAddress_isMulticast(const XHostAddress* addr) {
    if (!addr || addr->isNull) return false;
    if (addr->protocol == XHostAddress_IPv4Protocol) {
        return isIPv4Multicast(ipv6MappedToIpv4(addr->a6));
    }
    if (addr->protocol == XHostAddress_IPv6Protocol) {
        return isIPv6Multicast(addr->a6);
    }
    return false;
}

bool XHostAddress_isGlobal(const XHostAddress* addr) {
    if (!addr || addr->isNull) return false;
    if (addr->protocol == XHostAddress_IPv4Protocol) {
        return isIPv4Global(ipv6MappedToIpv4(addr->a6));
    }
    if (addr->protocol == XHostAddress_IPv6Protocol) {
        return isIPv6Global(addr->a6);
    }
    return false;
}

bool XHostAddress_isLinkLocal(const XHostAddress* addr) {
    if (!addr || addr->isNull) return false;
    if (addr->protocol == XHostAddress_IPv4Protocol) {
        return isIPv4LinkLocal(ipv6MappedToIpv4(addr->a6));
    }
    if (addr->protocol == XHostAddress_IPv6Protocol) {
        return isIPv6LinkLocal(addr->a6);
    }
    return false;
}

bool XHostAddress_isSiteLocal(const XHostAddress* addr) {
    if (!addr || addr->isNull) return false;
    if (addr->protocol == XHostAddress_IPv6Protocol) {
        return isIPv6SiteLocal(addr->a6);
    }
    return false;
}

bool XHostAddress_isUniqueLocal(const XHostAddress* addr) {
    if (!addr || addr->isNull) return false;
    if (addr->protocol == XHostAddress_IPv6Protocol) {
        return isIPv6UniqueLocal(addr->a6);
    }
    return false;
}

bool XHostAddress_isInSubnet(const XHostAddress* addr, const XHostAddress* subnet, int prefixLength) {
    if (!addr || !subnet || addr->isNull || subnet->isNull) return false;
    if (addr->protocol != subnet->protocol) return false;
    if (prefixLength < 0) return false;

    int bits = (addr->protocol == XHostAddress_IPv4Protocol) ? 32 : 128;
    if (prefixLength > bits) prefixLength = bits;

    int full_bytes = prefixLength / 8;
    int remaining_bits = prefixLength % 8;

    if (memcmp(addr->a6, subnet->a6, full_bytes) != 0) return false;

    if (remaining_bits > 0) {
        uint8_t mask = (0xFF << (8 - remaining_bits)) & 0xFF;
        if ((addr->a6[full_bytes] & mask) != (subnet->a6[full_bytes] & mask)) {
            return false;
        }
    }

    return true;
}

bool XHostAddress_parseSubnet(const char* subnetStr, XHostAddress* host, int* prefixLen) {
    if (!subnetStr || !host || !prefixLen) return false;

    char* slash = strchr(subnetStr, '/');
    if (!slash) return false;

    int len = (int)(slash - subnetStr);
    if (len <= 0) return false;

    char* addrStr = (char*)XMalloc_System(len + 1);
    if (!addrStr) return false;
    memcpy(addrStr, subnetStr, len);
    addrStr[len] = '\0';

    int prefix = atoi(slash + 1);
    if (prefix <= 0) {
        XFree_System(addrStr);
        return false;
    }

    XHostAddress* tmp = XHostAddress_create_fromString(addrStr);
    XFree_System(addrStr);

    if (!tmp || XHostAddress_isNull(tmp)) {
        XHostAddress_delete_base(tmp);
        return false;
    }

    XClass_move_base(host, tmp);
    XHostAddress_delete_base(tmp);
    *prefixLen = prefix;
    return true;
}

// ==================== 静态常量 ====================

const XHostAddress XHostAddress_Null = {
    .m_class = {0},
    .a6 = {0},
    .protocol = XHostAddress_UnknownNetworkLayerProtocol,
    .scopeId = {0},
    .isNull = true
};

const XHostAddress XHostAddress_Any = {
    .m_class = {0},
    .a6 = {0,0,0,0,0,0,0,0,0,0,0xff,0xff,0,0,0,0},
    .protocol = XHostAddress_IPv4Protocol,
    .scopeId = {0},
    .isNull = false
};

const XHostAddress XHostAddress_AnyIPv4 = {
    .m_class = {0},
    .a6 = {0,0,0,0,0,0,0,0,0,0,0xff,0xff,0,0,0,0},
    .protocol = XHostAddress_IPv4Protocol,
    .scopeId = {0},
    .isNull = false
};

const XHostAddress XHostAddress_AnyIPv6 = {
    .m_class = {0},
    .a6 = {0},
    .protocol = XHostAddress_IPv6Protocol,
    .scopeId = {0},
    .isNull = false
};

const XHostAddress XHostAddress_LocalHost = {
    .m_class = {0},
    .a6 = {0,0,0,0,0,0,0,0,0,0,0xff,0xff,127,0,0,1},
    .protocol = XHostAddress_IPv4Protocol,
    .scopeId = {0},
    .isNull = false
};

const XHostAddress XHostAddress_LocalHostIPv6 = {
    .m_class = {0},
    .a6 = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    .protocol = XHostAddress_IPv6Protocol,
    .scopeId = {0},
    .isNull = false
};

const XHostAddress XHostAddress_Broadcast = {
    .m_class = {0},
    .a6 = {0,0,0,0,0,0,0,0,0,0,0xff,0xff,255,255,255,255},
    .protocol = XHostAddress_IPv4Protocol,
    .scopeId = {0},
    .isNull = false
};

const XHostAddress XHostAddress_AnyAll = {
    .m_class = {0},
    .a6 = {0},
    .protocol = XHostAddress_IPv6Protocol,
    .scopeId = {0},
    .isNull = false
};

// ==================== 辅助函数 ====================

bool XHostAddress_isIPv4Address(const char* address) {
    uint32_t dummy;
    return parseIPv4(address, &dummy);
}

bool XHostAddress_isIPv6Address(const char* address) {
    uint8_t dummy[16];
    return parseIPv6(address, dummy, NULL);
}