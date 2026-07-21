// XHostAddress.c
#include "XHostAddress.h"
#include "XMemory.h"
#include "XString.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

// ==================== IPv4 解析 ====================

static bool parseIPv4(const char* src, uint32_t* out) {
    if (!src || !out) return false;
    unsigned int a, b, c, d;
    if (sscanf(src, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    *out = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
    return true;
}

// ==================== IPv6 解析（支持 :: 格式）====================

static bool parseIPv6(const char* src, uint8_t dst[16]) {
    if (!src || !dst) return false;
    memset(dst, 0, 16);

    char buf[64];
    size_t len = strlen(src);
    if (len >= sizeof(buf)) return false;
    strcpy(buf, src);

    // Strip scope ID (e.g., %eth0)
    char* percent = strchr(buf, '%');
    if (percent) *percent = '\0';

    // Strip brackets
    char* s = buf;
    if (*s == '[') {
        s++;
        char* end = strchr(s, ']');
        if (end) *end = '\0';
    }

    // Handle common shortcuts
    if (strcmp(s, "::1") == 0) {
        dst[15] = 1;
        return true;
    }
    if (strcmp(s, "::") == 0) {
        return true;
    }

    // Parse segments
    int parts[8] = { 0 };
    int count = 0;
    char temp[64];
    strcpy(temp, s);
    char* token = strtok(temp, ":");
    while (token && count < 8) {
        if (strlen(token) == 0) {
            break; // "::" encountered
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

    return false;
}

// ==================== 地址分类 ====================

static bool isIPv4Loopback(uint32_t ip) { return (ip & 0xFF000000U) == 0x7F000000U; }
static bool isIPv4Multicast(uint32_t ip) { return (ip & 0xF0000000U) == 0xE0000000U; }
static bool isIPv4LinkLocal(uint32_t ip) { return (ip & 0xFFFF0000U) == 0xA9FE0000U; }
static bool isIPv4Broadcast(uint32_t ip) { return ip == 0xFFFFFFFFU; }
static bool isIPv4Global(uint32_t ip) {
    if (isIPv4Loopback(ip) || isIPv4LinkLocal(ip) || isIPv4Broadcast(ip)) return false;
    if ((ip & 0xFF000000U) == 0x0A000000U) return false;
    if ((ip & 0xFFF00000U) == 0xAC100000U) return false;
    if ((ip & 0xFFFF0000U) == 0xC0A80000U) return false;
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
    return true;
}

// ==================== 虚函数 ====================

static void VXHostAddress_copy(XHostAddress* self, const XHostAddress* other) {
    if (!self || !other) return;
    if (XClassIsVtableNull(self))
        XHostAddress_init(self);
    self->protocol = other->protocol;
    self->isNull = other->isNull;
    if (other->protocol == XHostAddress_IPv4Protocol) {
        self->ip4 = other->ip4;
    } else {
        memcpy(self->ip6, other->ip6, 16);
    }
}

static void VXHostAddress_move(XHostAddress* self, XHostAddress* other) {
    if (!self || !other) return;
    if (XClassIsVtableNull(self))
        XHostAddress_init(self);
    memcpy(self, other, sizeof(XHostAddress));
}

static void VXHostAddress_deinit(XHostAddress* self) {
    (void)self;
}

bool XHostAddress_operator_equal(const XHostAddress* a, const XHostAddress* b) {
    if (!a || !b) return false;
    if (a->protocol != b->protocol) return false;
    if (a->protocol == XHostAddress_IPv4Protocol) return a->ip4 == b->ip4;
    return memcmp(a->ip6, b->ip6, 16) == 0;
}

int XHostAddress_operator_compare(const XHostAddress* a, const XHostAddress* b) {
    if (!a || !b) return (a != NULL) - (b != NULL);
    if (a->protocol != b->protocol) return (int)a->protocol - (int)b->protocol;
    if (a->protocol == XHostAddress_IPv4Protocol) {
        if (a->ip4 < b->ip4) return -1;
        if (a->ip4 > b->ip4) return 1;
        return 0;
    }
    return memcmp(a->ip6, b->ip6, 16);
}

// ==================== 虚函数表 ====================

XVtable* XHostAddress_class_init(void) 
{
    XVTABLE_CREAT_DEFAULT
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
        if (addr) { addr->protocol = XHostAddress_UnknownNetworkLayerProtocol; addr->isNull = true; }
        return;
    }

    uint32_t ipv4;
    if (parseIPv4(address, &ipv4)) {
        XHostAddress_setAddressIPv4(addr, ipv4);
        return;
    }

    uint8_t ip6[16];
    if (parseIPv6(address, ip6)) {
        XHostAddress_setAddressIPv6(addr, ip6);
        return;
    }

    addr->protocol = XHostAddress_UnknownNetworkLayerProtocol;
    addr->isNull = true;
}

void XHostAddress_setAddressIPv4(XHostAddress* addr, uint32_t ip) {
    if (!addr) return;
    addr->ip4 = ip;
    addr->protocol = XHostAddress_IPv4Protocol;
    addr->isNull = false;
}

void XHostAddress_setAddressIPv6(XHostAddress* addr, const uint8_t ip[16]) {
    if (!addr || !ip) return;
    memcpy(addr->ip6, ip, 16);
    addr->protocol = XHostAddress_IPv6Protocol;
    addr->isNull = false;
}

void XHostAddress_setAddressSpecial(XHostAddress* addr, XHostAddress_SpecialAddress special) {
    if (!addr) return;
    memset(&addr->ip4, 0, sizeof(addr->ip4) > 16 ? sizeof(addr->ip4) : 16);
    memset(addr->ip6, 0, 16);
    addr->isNull = false;

    switch (special) {
    case XHostAddress_NullSpecial:
        addr->protocol = XHostAddress_UnknownNetworkLayerProtocol;
        addr->isNull = true;
        break;
    case XHostAddress_AnySpecial:
    case XHostAddress_AnyIPv4Special:
        addr->ip4 = 0;
        addr->protocol = XHostAddress_IPv4Protocol;
        break;
    case XHostAddress_AnyIPv6Special:
        addr->protocol = XHostAddress_IPv6Protocol;
        break;
    case XHostAddress_LocalHostSpecial:
        addr->ip4 = 0x7F000001U;
        addr->protocol = XHostAddress_IPv4Protocol;
        break;
    case XHostAddress_LocalHostIPv6Special:
        addr->ip6[15] = 1;
        addr->protocol = XHostAddress_IPv6Protocol;
        break;
    case XHostAddress_BroadcastSpecial:
        addr->ip4 = 0xFFFFFFFFU;
        addr->protocol = XHostAddress_IPv4Protocol;
        break;
    case XHostAddress_AnyAllSpecial:
        addr->protocol = XHostAddress_IPv6Protocol;
        break;
    }
}

void XHostAddress_setScopeId(XHostAddress* addr, const char* id) {
    (void)addr;
    (void)id;
    // scope ID is not stored (aligned with Qt QHostAddress)
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
    return addr->ip4;
}

void XHostAddress_toIPv6Address(const XHostAddress* addr, uint8_t out[16]) {
    if (!addr || !out) {
        if (out) memset(out, 0, 16);
        return;
    }
    if (addr->protocol == XHostAddress_IPv4Protocol) {
        memset(out, 0, 10);
        out[10] = 0xFF; out[11] = 0xFF;
        out[12] = (addr->ip4 >> 24) & 0xFF;
        out[13] = (addr->ip4 >> 16) & 0xFF;
        out[14] = (addr->ip4 >> 8) & 0xFF;
        out[15] = addr->ip4 & 0xFF;
    } else {
        memcpy(out, addr->ip6, 16);
    }
}

const char* XHostAddress_scopeId(const XHostAddress* addr) {
    (void)addr;
    return "";
}

XString* XHostAddress_toString(const XHostAddress* addr) {
    if (!addr || addr->isNull) return NULL;

    char buffer[128];
    int len = 0;

    if (addr->protocol == XHostAddress_IPv4Protocol) {
        len = snprintf(buffer, sizeof(buffer), "%d.%d.%d.%d",
            (addr->ip4 >> 24) & 0xFF, (addr->ip4 >> 16) & 0xFF,
            (addr->ip4 >> 8) & 0xFF, addr->ip4 & 0xFF);
    } else if (addr->protocol == XHostAddress_IPv6Protocol) {
        len = snprintf(buffer, sizeof(buffer),
            "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
            addr->ip6[0], addr->ip6[1], addr->ip6[2], addr->ip6[3],
            addr->ip6[4], addr->ip6[5], addr->ip6[6], addr->ip6[7],
            addr->ip6[8], addr->ip6[9], addr->ip6[10], addr->ip6[11],
            addr->ip6[12], addr->ip6[13], addr->ip6[14], addr->ip6[15]);
    }

    if (len <= 0) return NULL;
    return XString_create_with_length_utf8(buffer, len);
}

bool XHostAddress_isLoopback(const XHostAddress* addr) {
    if (!addr || addr->isNull) return false;
    if (addr->protocol == XHostAddress_IPv4Protocol) return isIPv4Loopback(addr->ip4);
    if (addr->protocol == XHostAddress_IPv6Protocol) return isIPv6Loopback(addr->ip6);
    return false;
}

bool XHostAddress_isMulticast(const XHostAddress* addr) {
    if (!addr || addr->isNull) return false;
    if (addr->protocol == XHostAddress_IPv4Protocol) return isIPv4Multicast(addr->ip4);
    if (addr->protocol == XHostAddress_IPv6Protocol) return isIPv6Multicast(addr->ip6);
    return false;
}

bool XHostAddress_isGlobal(const XHostAddress* addr) {
    if (!addr || addr->isNull) return false;
    if (addr->protocol == XHostAddress_IPv4Protocol) return isIPv4Global(addr->ip4);
    if (addr->protocol == XHostAddress_IPv6Protocol) return isIPv6Global(addr->ip6);
    return false;
}

bool XHostAddress_isLinkLocal(const XHostAddress* addr) {
    if (!addr || addr->isNull) return false;
    if (addr->protocol == XHostAddress_IPv4Protocol) return isIPv4LinkLocal(addr->ip4);
    if (addr->protocol == XHostAddress_IPv6Protocol) return isIPv6LinkLocal(addr->ip6);
    return false;
}

bool XHostAddress_isSiteLocal(const XHostAddress* addr) {
    if (!addr || addr->isNull) return false;
    if (addr->protocol == XHostAddress_IPv6Protocol) return isIPv6SiteLocal(addr->ip6);
    return false;
}

bool XHostAddress_isUniqueLocal(const XHostAddress* addr) {
    if (!addr || addr->isNull) return false;
    if (addr->protocol == XHostAddress_IPv6Protocol) return isIPv6UniqueLocal(addr->ip6);
    return false;
}

bool XHostAddress_isInSubnet(const XHostAddress* addr, const XHostAddress* subnet, int prefixLength) {
    if (!addr || !subnet || addr->isNull || subnet->isNull) return false;
    if (addr->protocol != subnet->protocol) return false;
    if (prefixLength < 0) return false;

    int bits = (addr->protocol == XHostAddress_IPv4Protocol) ? 32 : 128;
    if (prefixLength > bits) prefixLength = bits;

    const uint8_t* a = (addr->protocol == XHostAddress_IPv4Protocol) ? ((const uint8_t*)&addr->ip4) : addr->ip6;
    const uint8_t* s = (subnet->protocol == XHostAddress_IPv4Protocol) ? ((const uint8_t*)&subnet->ip4) : subnet->ip6;
    int byteLen = (addr->protocol == XHostAddress_IPv4Protocol) ? 4 : 16;

    int full_bytes = prefixLength / 8;
    int remaining_bits = prefixLength % 8;

    if (memcmp(a, s, full_bytes) != 0) return false;
    if (remaining_bits > 0) {
        uint8_t mask = (0xFF << (8 - remaining_bits)) & 0xFF;
        if ((a[full_bytes] & mask) != (s[full_bytes] & mask)) return false;
    }
    return true;
}

bool XHostAddress_parseSubnet(const char* subnetStr, XHostAddress* host, int* prefixLen) {
    if (!subnetStr || !host || !prefixLen) return false;

    const char* slash = strchr(subnetStr, '/');
    if (!slash) return false;

    size_t len = slash - subnetStr;
    if (len == 0) return false;

    char* addrStr = (char*)XMalloc_System(len + 1);
    if (!addrStr) return false;
    memcpy(addrStr, subnetStr, len);
    addrStr[len] = '\0';

    int prefix = atoi(slash + 1);
    if (prefix <= 0) { XFree_System(addrStr); return false; }

    XHostAddress* tmp = XHostAddress_create_fromString(addrStr);
    XFree_System(addrStr);

    if (!tmp || XHostAddress_isNull(tmp)) { XHostAddress_delete_base(tmp); return false; }

    XClass_move_base(host, tmp);
    XHostAddress_delete_base(tmp);
    *prefixLen = prefix;
    return true;
}

// ==================== 辅助函数 ====================

bool XHostAddress_isIPv4Address(const char* address) {
    uint32_t dummy;
    return parseIPv4(address, &dummy);
}

bool XHostAddress_isIPv6Address(const char* address) {
    uint8_t dummy[16];
    return parseIPv6(address, dummy);
}

// ==================== 静态常量 ====================

const XHostAddress XHostAddress_Null = {
    .m_class = {0},
    .protocol = XHostAddress_UnknownNetworkLayerProtocol,
    .isNull = true,
    .ip6 = {0}
};

const XHostAddress XHostAddress_Any = {
    .m_class = {0},
    .protocol = XHostAddress_IPv4Protocol,
    .isNull = false,
    .ip4 = 0
};

const XHostAddress XHostAddress_AnyIPv4 = {
    .m_class = {0},
    .protocol = XHostAddress_IPv4Protocol,
    .isNull = false,
    .ip4 = 0
};

const XHostAddress XHostAddress_AnyIPv6 = {
    .m_class = {0},
    .protocol = XHostAddress_IPv6Protocol,
    .isNull = false,
    .ip6 = {0}
};

const XHostAddress XHostAddress_LocalHost = {
    .m_class = {0},
    .protocol = XHostAddress_IPv4Protocol,
    .isNull = false,
    .ip4 = 0x7F000001U
};

const XHostAddress XHostAddress_LocalHostIPv6 = {
    .m_class = {0},
    .protocol = XHostAddress_IPv6Protocol,
    .isNull = false,
    .ip6 = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1}
};

const XHostAddress XHostAddress_Broadcast = {
    .m_class = {0},
    .protocol = XHostAddress_IPv4Protocol,
    .isNull = false,
    .ip4 = 0xFFFFFFFFU
};

const XHostAddress XHostAddress_AnyAll = {
    .m_class = {0},
    .protocol = XHostAddress_IPv6Protocol,
    .isNull = false,
    .ip6 = {0}
};