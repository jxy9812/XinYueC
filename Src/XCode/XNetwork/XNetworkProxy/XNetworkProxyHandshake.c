/**
 * @file XNetworkProxyHandshake.c
 * @brief 代理握手协议实现（对齐Qt 6.8）
 */

#include "XNetworkProxyHandshake.h"
#include "XCryptographic.h"
#include "XByteArray.h"
#include "XAbstractSocket.h"
#include "XDateTime.h"
#include "XBase64.h"
#include "XMemory.h"
#include "XNetwork.h"
#include "XRandomGenerator.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#if XNETWORK_ON
#if XNETWORK_PROXY_ON

/**
 * @brief 生成随机cnonce（客户端nonce）
 * @note 使用 XRandomGenerator（对齐Qt QRandomGenerator）
 */
static void generateCnonce(char* buffer, size_t bufferSize) {
    static const char hexChars[] = "0123456789abcdef";
    
    /* 使用全局随机数生成器生成随机十六进制字符串 */
    for (size_t i = 0; i < bufferSize - 1 && i < 32; i++) {
        buffer[i] = hexChars[XRandomGenerator_boundedU32(XRandomGenerator_global(), 16)];
    }
    buffer[bufferSize - 1] = '\0';
}

/**
 * @brief 将字节数组转换为十六进制字符串
 */
static void bytesToHex(const uint8_t* bytes, size_t len, char* hex, size_t hexSize) {
    static const char hexChars[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < len && i * 2 + 1 < hexSize; i++) {
        hex[i * 2] = hexChars[(bytes[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = hexChars[bytes[i] & 0x0F];
    }
    hex[i * 2] = '\0';
}

/**
 * @brief 将XByteArray数据转换为十六进制字符串
 */
static void byteArrayToHex(const XByteArray* ba, char* hex, size_t hexSize) {
    const uint8_t* data = (const uint8_t*)XByteArray_data((XByteArray*)ba);
    size_t len = XByteArray_size_base(ba);
    bytesToHex(data, len, hex, hexSize);
}

/**
 * @brief 解析IPv6地址字符串为16字节二进制
 * @param ipv6Str IPv6地址字符串（如 "2001:db8::1"）
 * @param outBytes 输出16字节二进制数据
 * @return 成功返回true
 */
static bool parseIPv6Address(const char* ipv6Str, uint8_t* outBytes) {
    if (!ipv6Str || !outBytes) {
        return false;
    }
    
    memset(outBytes, 0, 16);
    
    size_t len = strlen(ipv6Str);
    if (len < 2 || len > 45) {
        return false;
    }
    
    // 检查是否是IPv6地址（包含冒号）
    if (strchr(ipv6Str, ':') == NULL) {
        return false;
    }
    
    // 检查是否是IPv4映射地址 (::ffff:x.x.x.x)
    if (strncmp(ipv6Str, "::ffff:", 7) == 0 || strncmp(ipv6Str, "::FFFF:", 7) == 0) {
        // IPv4映射地址，解析IPv4部分
        const char* ipv4Part = ipv6Str + 7;
        uint8_t ipv4[4] = {0};
        int part = 0, idx = 0;
        for (size_t i = 0; i <= strlen(ipv4Part) && idx < 4; i++) {
            if (ipv4Part[i] >= '0' && ipv4Part[i] <= '9') {
                part = part * 10 + (ipv4Part[i] - '0');
            } else if (ipv4Part[i] == '.' || ipv4Part[i] == '\0') {
                ipv4[idx++] = (uint8_t)part;
                part = 0;
            }
        }
        // 设置IPv4映射地址
        outBytes[10] = 0xFF;
        outBytes[11] = 0xFF;
        memcpy(outBytes + 12, ipv4, 4);
        return true;
    }
    
    // 解析标准IPv6地址
    uint16_t groups[8] = {0};
    int groupIdx = 0;
    int doubleColonPos = -1;
    const char* p = ipv6Str;
    
    // 处理开头的 ::
    if (*p == ':') {
        if (*(p + 1) != ':') {
            return false;
        }
        doubleColonPos = 0;
        p += 2;
    }
    
    while (*p && groupIdx < 8) {
        if (*p == ':') {
            if (*(p + 1) == ':') {
                // 双冒号
                if (doubleColonPos >= 0) {
                    return false; // 只能有一个 ::
                }
                doubleColonPos = groupIdx;
                p += 2;
            } else {
                p++;
            }
            continue;
        }
        
        // 解析十六进制组
        uint16_t value = 0;
        int digitCount = 0;
        while (*p && *p != ':' && digitCount < 4) {
            char c = *p;
            if (c >= '0' && c <= '9') {
                value = (value << 4) | (c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value = (value << 4) | (c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value = (value << 4) | (c - 'A' + 10);
            } else {
                break;
            }
            p++;
            digitCount++;
        }
        
        if (digitCount == 0) {
            return false;
        }
        
        groups[groupIdx++] = value;
        
        if (*p == ':') {
            p++;
        }
    }
    
    // 处理双冒号展开
    if (doubleColonPos >= 0) {
        int missingGroups = 8 - groupIdx;
        if (missingGroups < 0) {
            return false;
        }
        // 移动后面的组
        for (int i = groupIdx - 1; i >= doubleColonPos; i--) {
            groups[i + missingGroups] = groups[i];
        }
        // 填充0
        for (int i = doubleColonPos; i < doubleColonPos + missingGroups; i++) {
            groups[i] = 0;
        }
    }
    
    // 转换为字节数组（大端序）
    for (int i = 0; i < 8; i++) {
        outBytes[i * 2] = (groups[i] >> 8) & 0xFF;
        outBytes[i * 2 + 1] = groups[i] & 0xFF;
    }
    
    return true;
}

/**
 * @brief 格式化IPv6地址为字符串
 * @param bytes 16字节IPv6地址
 * @param outStr 输出字符串缓冲区
 * @param outSize 输出缓冲区大小
 */
static void formatIPv6Address(const uint8_t* bytes, char* outStr, size_t outSize) {
    if (!bytes || !outStr || outSize < 40) {
        if (outStr && outSize > 0) {
            outStr[0] = '\0';
        }
        return;
    }
    
    // 转换为16位组
    uint16_t groups[8];
    for (int i = 0; i < 8; i++) {
        groups[i] = ((uint16_t)bytes[i * 2] << 8) | bytes[i * 2 + 1];
    }
    
    // 找到最长的连续0序列
    int bestStart = -1, bestLen = 0;
    int curStart = -1, curLen = 0;
    for (int i = 0; i < 8; i++) {
        if (groups[i] == 0) {
            if (curStart < 0) {
                curStart = i;
            }
            curLen++;
        } else {
            if (curLen > bestLen) {
                bestStart = curStart;
                bestLen = curLen;
            }
            curStart = -1;
            curLen = 0;
        }
    }
    if (curLen > bestLen) {
        bestStart = curStart;
        bestLen = curLen;
    }
    
    // 构建字符串
    int offset = 0;
    for (int i = 0; i < 8 && offset < (int)outSize - 1; i++) {
        if (bestStart >= 0 && i == bestStart && bestLen > 1) {
            outStr[offset++] = ':';
            i += bestLen - 1;
            if (i >= 7) {
                outStr[offset++] = ':';
            }
        } else {
            if (i > 0) {
                outStr[offset++] = ':';
            }
            offset += snprintf(outStr + offset, outSize - offset, "%x", groups[i]);
        }
    }
    outStr[offset] = '\0';
}

// =============== 上下文管理 ===============

XProxyHandshakeContext* XNetworkProxyHandshake_createContext(
    XNetworkProxy* proxy,
    const char* targetHost,
    uint16_t targetPort
) {
    if (!proxy || !targetHost) {
        return NULL;
    }
    
    XProxyHandshakeContext* ctx = (XProxyHandshakeContext*)XCalloc_System(1, sizeof(XProxyHandshakeContext));
    if (!ctx) {
        return NULL;
    }
    
    ctx->state = XProxyHandshakeState_None;
    ctx->proxy = proxy;
    ctx->targetHost = XString_create_utf8(targetHost);
    ctx->targetPort = targetPort;
    ctx->socks5Command = XSocks5Cmd_Connect;
    ctx->timeoutMs = 30000; // 默认30秒超时
    ctx->socks5Buffer = XByteArray_create();
    ctx->httpBuffer = XByteArray_create();
    ctx->errorMessage = XString_create();
    
    return ctx;
}

XProxyHandshakeContext* XNetworkProxyHandshake_createBindContext(
    XNetworkProxy* proxy,
    uint16_t bindPort
) {
    if (!proxy) {
        return NULL;
    }
    
    XProxyHandshakeContext* ctx = (XProxyHandshakeContext*)XCalloc_System(1, sizeof(XProxyHandshakeContext));
    if (!ctx) {
        return NULL;
    }
    
    ctx->state = XProxyHandshakeState_None;
    ctx->proxy = proxy;
    ctx->targetHost = XString_create_utf8("0.0.0.0");
    ctx->targetPort = bindPort;
    ctx->socks5Command = XSocks5Cmd_Bind;
    ctx->timeoutMs = 30000;
    ctx->socks5Buffer = XByteArray_create();
    ctx->httpBuffer = XByteArray_create();
    ctx->errorMessage = XString_create();
    
    return ctx;
}

XProxyHandshakeContext* XNetworkProxyHandshake_createUdpContext(
    XNetworkProxy* proxy
) {
    if (!proxy) {
        return NULL;
    }
    
    XProxyHandshakeContext* ctx = (XProxyHandshakeContext*)XCalloc_System(1, sizeof(XProxyHandshakeContext));
    if (!ctx) {
        return NULL;
    }
    
    ctx->state = XProxyHandshakeState_None;
    ctx->proxy = proxy;
    ctx->targetHost = NULL;
    ctx->targetPort = 0;
    ctx->socks5Command = XSocks5Cmd_UdpAssociate;
    ctx->timeoutMs = 30000;
    ctx->socks5Buffer = XByteArray_create();
    ctx->httpBuffer = XByteArray_create();
    ctx->errorMessage = XString_create();
    
    return ctx;
}

void XNetworkProxyHandshake_destroyContext(XProxyHandshakeContext* ctx) {
    if (!ctx) {
        return;
    }
    
    XString_delete_base(ctx->targetHost);
    XByteArray_delete_base(ctx->socks5Buffer);
    XByteArray_delete_base(ctx->httpBuffer);
    XByteArray_delete_base(ctx->httpAuthHeader);
    
    // 释放Digest参数
    XString_delete_base(ctx->digestParams.realm);
    XString_delete_base(ctx->digestParams.nonce);
    XString_delete_base(ctx->digestParams.opaque);
    XString_delete_base(ctx->digestParams.algorithm);
    XString_delete_base(ctx->digestParams.qop);
    XString_delete_base(ctx->digestParams.cnonce);
    
    // 释放NTLM上下文
    XByteArray_delete_base(ctx->ntlmContext.type1Message);
    XByteArray_delete_base(ctx->ntlmContext.type2Message);
    XByteArray_delete_base(ctx->ntlmContext.type3Message);
    XString_delete_base(ctx->ntlmContext.workstation);
    XString_delete_base(ctx->ntlmContext.domain);
    
    XString_delete_base(ctx->errorMessage);
    
    XFree_System(ctx);
}

void XNetworkProxyHandshake_setTimeout(XProxyHandshakeContext* ctx, int timeoutMs) {
    if (ctx) {
        ctx->timeoutMs = timeoutMs;
    }
}

// =============== SOCKS5 协议实现 ===============

int XSocks5_buildGreetingRequest(
    uint8_t* buffer,
    size_t bufferSize,
    const XSocks5AuthMethod* methods,
    size_t methodCount
) {
    if (!buffer || !methods || methodCount == 0 || methodCount > 255) {
        return -1;
    }
    
    if (bufferSize < 2 + methodCount) {
        return -1;
    }
    
    buffer[0] = XSOCKS5_VERSION;
    buffer[1] = (uint8_t)methodCount;
    
    for (size_t i = 0; i < methodCount; i++) {
        buffer[2 + i] = (uint8_t)methods[i];
    }
    
    return (int)(2 + methodCount);
}

bool XSocks5_parseGreetingResponse(
    const uint8_t* response,
    size_t responseLen,
    XSocks5AuthMethod* outMethod
) {
    if (!response || !outMethod || responseLen < 2) {
        return false;
    }
    
    if (response[0] != XSOCKS5_VERSION) {
        return false;
    }
    
    *outMethod = (XSocks5AuthMethod)response[1];
    return true;
}

int XSocks5_buildAuthRequest(
    uint8_t* buffer,
    size_t bufferSize,
    const char* username,
    const char* password
) {
    if (!buffer || !username || !password) {
        return -1;
    }
    
    size_t ulen = strlen(username);
    size_t plen = strlen(password);
    
    if (ulen > 255 || plen > 255) {
        return -1;
    }
    
    size_t totalLen = 3 + ulen + plen;
    if (bufferSize < totalLen) {
        return -1;
    }
    
    buffer[0] = 0x01; // 子协商版本
    buffer[1] = (uint8_t)ulen;
    memcpy(buffer + 2, username, ulen);
    buffer[2 + ulen] = (uint8_t)plen;
    memcpy(buffer + 3 + ulen, password, plen);
    
    return (int)totalLen;
}

bool XSocks5_parseAuthResponse(
    const uint8_t* response,
    size_t responseLen
) {
    if (!response || responseLen < 2) {
        return false;
    }
    
    // 版本号应为0x01，状态码为0x00表示成功
    return (response[0] == 0x01 && response[1] == 0x00);
}

int XSocks5_buildRequest(
    uint8_t* buffer,
    size_t bufferSize,
    XSocks5Command command,
    const char* host,
    uint16_t port
) {
    if (!buffer) {
        return -1;
    }
    
    size_t hostLen = host ? strlen(host) : 0;
    size_t minSize = 6 + (hostLen > 0 ? 1 + hostLen : 4);
    
    if (bufferSize < minSize) {
        return -1;
    }
    
    size_t offset = 0;
    
    buffer[offset++] = XSOCKS5_VERSION;
    buffer[offset++] = (uint8_t)command;
    buffer[offset++] = 0x00; // 保留
    
    if (host && hostLen > 0) {
        // 判断是IPv4、IPv6还是域名
        bool isIPv4 = false, isIPv6 = false;
        
        // 简单判断IPv4（包含.且不包含:）
        if (strchr(host, '.') != NULL && strchr(host, ':') == NULL) {
            isIPv4 = true;
        } else if (strchr(host, ':') != NULL) {
            isIPv6 = true;
        }
        
        if (isIPv4) {
            buffer[offset++] = XSocks5Atyp_IPv4;
            // 解析IPv4地址
            uint8_t ip[4] = {0};
            int part = 0, idx = 0;
            for (size_t i = 0; i <= hostLen && idx < 4; i++) {
                if (host[i] >= '0' && host[i] <= '9') {
                    part = part * 10 + (host[i] - '0');
                } else if (host[i] == '.' || host[i] == '\0') {
                    ip[idx++] = (uint8_t)part;
                    part = 0;
                }
            }
            memcpy(buffer + offset, ip, 4);
            offset += 4;
        } else if (isIPv6) {
            buffer[offset++] = XSocks5Atyp_IPv6;
            // IPv6地址解析（简化处理，填充0）
            memset(buffer + offset, 0, 16);
            offset += 16;
        } else {
            // 域名
            if (hostLen > 255) {
                return -1;
            }
            buffer[offset++] = XSocks5Atyp_DomainName;
            buffer[offset++] = (uint8_t)hostLen;
            memcpy(buffer + offset, host, hostLen);
            offset += hostLen;
        }
    } else {
        // UDP ASSOCIATE时可以为空地址
        buffer[offset++] = XSocks5Atyp_IPv4;
        memset(buffer + offset, 0, 4);
        offset += 4;
    }
    
    // 端口（大端序）
    buffer[offset++] = (uint8_t)(port >> 8);
    buffer[offset++] = (uint8_t)(port & 0xFF);
    
    return (int)offset;
}

bool XSocks5_parseReply(
    const uint8_t* response,
    size_t responseLen,
    XSocks5ReplyCode* outReplyCode,
    XHostAddress* outBindAddress,
    uint16_t* outBindPort
) {
    if (!response || responseLen < 10) {
        return false;
    }
    
    if (response[0] != XSOCKS5_VERSION) {
        return false;
    }
    
    if (outReplyCode) {
        *outReplyCode = (XSocks5ReplyCode)response[1];
    }
    
    // response[2] 保留，应为0
    uint8_t atyp = response[3];
    
    if (atyp == XSocks5Atyp_IPv4) {
        if (responseLen < 10) {
            return false;
        }
        
        if (outBindAddress) {
            uint32_t ip = (response[4] << 24) | (response[5] << 16) | 
                          (response[6] << 8) | response[7];
            XHostAddress_setAddressIPv4(outBindAddress, ip);
        }
        
        if (outBindPort) {
            *outBindPort = (response[8] << 8) | response[9];
        }
    } else if (atyp == XSocks5Atyp_DomainName) {
        uint8_t domainLen = response[4];
        if (responseLen < (size_t)(5 + domainLen + 2)) {
            return false;
        }
        
        // 域名暂不处理
        if (outBindPort) {
            *outBindPort = (response[5 + domainLen] << 8) | response[6 + domainLen];
        }
    } else if (atyp == XSocks5Atyp_IPv6) {
        if (responseLen < 22) {
            return false;
        }
        
        // IPv6暂不处理
        if (outBindPort) {
            *outBindPort = (response[20] << 8) | response[21];
        }
    }
    
    return true;
}

int XSocks5_buildUdpDatagram(
    uint8_t* buffer,
    size_t bufferSize,
    const uint8_t* data,
    size_t dataLen,
    uint8_t fragNumber,
    XSocks5AddressType atyp,
    const char* host,
    uint16_t port
) {
    if (!buffer || !data || !host) {
        return -1;
    }
    
    size_t hostLen = strlen(host);
    size_t headerLen;
    
    if (atyp == XSocks5Atyp_IPv4) {
        headerLen = 10; // 2 + 1 + 1 + 4 + 2
    } else if (atyp == XSocks5Atyp_DomainName) {
        headerLen = 7 + hostLen; // 2 + 1 + 1 + 1 + hostLen + 2
    } else if (atyp == XSocks5Atyp_IPv6) {
        headerLen = 22; // 2 + 1 + 1 + 16 + 2
    } else {
        return -1;
    }
    
    if (bufferSize < headerLen + dataLen) {
        return -1;
    }
    
    size_t offset = 0;
    
    buffer[offset++] = 0x00; // 保留
    buffer[offset++] = fragNumber; // 分片号
    buffer[offset++] = (uint8_t)atyp;
    
    if (atyp == XSocks5Atyp_IPv4) {
        // 解析IPv4
        uint8_t ip[4] = {0};
        int part = 0, idx = 0;
        for (size_t i = 0; i <= hostLen && idx < 4; i++) {
            if (host[i] >= '0' && host[i] <= '9') {
                part = part * 10 + (host[i] - '0');
            } else if (host[i] == '.' || host[i] == '\0') {
                ip[idx++] = (uint8_t)part;
                part = 0;
            }
        }
        memcpy(buffer + offset, ip, 4);
        offset += 4;
    } else if (atyp == XSocks5Atyp_DomainName) {
        buffer[offset++] = (uint8_t)hostLen;
        memcpy(buffer + offset, host, hostLen);
        offset += hostLen;
    } else if (atyp == XSocks5Atyp_IPv6) {
        memset(buffer + offset, 0, 16);
        offset += 16;
    }
    
    buffer[offset++] = (uint8_t)(port >> 8);
    buffer[offset++] = (uint8_t)(port & 0xFF);
    
    memcpy(buffer + offset, data, dataLen);
    
    return (int)(offset + dataLen);
}

bool XSocks5_parseUdpDatagram(
    const uint8_t* buffer,
    size_t bufferLen,
    const uint8_t** outData,
    size_t* outDataLen,
    char* outHost,
    uint16_t* outPort
) {
    if (!buffer || bufferLen < 10) {
        return false;
    }
    
    size_t offset = 0;
    
    offset += 2; // 跳过保留和分片号
    
    uint8_t atyp = buffer[offset++];
    
    if (atyp == XSocks5Atyp_IPv4) {
        if (bufferLen < 10) {
            return false;
        }
        
        if (outHost) {
            snprintf(outHost, 64, "%d.%d.%d.%d",
                     buffer[offset], buffer[offset + 1],
                     buffer[offset + 2], buffer[offset + 3]);
        }
        offset += 4;
    } else if (atyp == XSocks5Atyp_DomainName) {
        uint8_t domainLen = buffer[offset++];
        if (bufferLen < offset + domainLen + 2) {
            return false;
        }
        
        if (outHost) {
            memcpy(outHost, buffer + offset, domainLen);
            outHost[domainLen] = '\0';
        }
        offset += domainLen;
    } else if (atyp == XSocks5Atyp_IPv6) {
        if (bufferLen < 22) {
            return false;
        }
        
        if (outHost) {
            // IPv6格式化
            outHost[0] = '\0';
        }
        offset += 16;
    }
    
    if (outPort) {
        *outPort = (buffer[offset] << 8) | buffer[offset + 1];
    }
    offset += 2;
    
    if (outData) {
        *outData = buffer + offset;
    }
    if (outDataLen) {
        *outDataLen = bufferLen - offset;
    }
    
    return true;
}

// =============== HTTP CONNECT 协议实现 ===============

int XHttpProxy_buildConnectRequest(
    char* buffer,
    size_t bufferSize,
    const char* host,
    uint16_t port,
    const XNetworkProxy* proxy,
    XHttpProxyAuthType authType,
    const char* authHeader
) {
    if (!buffer || !host) {
        return -1;
    }
    
    int len;
    
    if (authType == XHttpProxyAuth_None || !authHeader) {
        // 无认证请求
        len = snprintf(buffer, bufferSize,
                       "CONNECT %s:%u HTTP/1.1\r\n"
                       "Host: %s:%u\r\n"
                       "Proxy-Connection: keep-alive\r\n"
                       "\r\n",
                       host, port, host, port);
    } else {
        // 带认证请求
        len = snprintf(buffer, bufferSize,
                       "CONNECT %s:%u HTTP/1.1\r\n"
                       "Host: %s:%u\r\n"
                       "Proxy-Connection: keep-alive\r\n"
                       "Proxy-Authorization: %s\r\n"
                       "\r\n",
                       host, port, host, port, authHeader);
    }
    
    return (len > 0 && (size_t)len < bufferSize) ? len : -1;
}

bool XHttpProxy_parseResponse(
    const char* response,
    size_t responseLen,
    int* outCode,
    char** outHeaders,
    char** outAuthHeader
) {
    if (!response || responseLen == 0) {
        return false;
    }
    
    // 解析HTTP状态行
    int major, minor, code;
    if (sscanf(response, "HTTP/%d.%d %d", &major, &minor, &code) != 3) {
        return false;
    }
    
    if (outCode) {
        *outCode = code;
    }
    
    // 查找头部开始位置
    const char* headers = strstr(response, "\r\n");
    if (!headers) {
        return true; // 无头部
    }
    headers += 2;
    
    if (outHeaders) {
        size_t headersLen = responseLen - (headers - response);
        *outHeaders = (char*)XMalloc_System(headersLen + 1);
        if (*outHeaders) {
            memcpy(*outHeaders, headers, headersLen);
            (*outHeaders)[headersLen] = '\0';
        }
    }
    
    // 查找WWW-Authenticate头
    if (outAuthHeader) {
        *outAuthHeader = NULL;
        
        const char* authStart = strstr(headers, "WWW-Authenticate:");
        if (!authStart) {
            authStart = strstr(headers, "WWW-Authenticate: ");
        }
        
        if (authStart) {
            authStart = strchr(authStart, ':');
            if (authStart) {
                authStart++;
                while (*authStart == ' ') authStart++;
                
                const char* authEnd = strstr(authStart, "\r\n");
                if (!authEnd) {
                    authEnd = response + responseLen;
                }
                
                size_t authLen = authEnd - authStart;
                *outAuthHeader = (char*)XMalloc_System(authLen + 1);
                if (*outAuthHeader) {
                    memcpy(*outAuthHeader, authStart, authLen);
                    (*outAuthHeader)[authLen] = '\0';
                }
            }
        }
    }
    
    return true;
}

bool XHttpProxy_parseAuthHeader(
    const char* authHeader,
    XHttpProxyAuthType* outAuthType,
    XHttpDigestParams* outParams
) {
    if (!authHeader || !outAuthType) {
        return false;
    }
    
    // 检查认证类型
    if (strncmp(authHeader, "Basic", 5) == 0) {
        *outAuthType = XHttpProxyAuth_Basic;
        return true;
    }
    
    if (strncmp(authHeader, "Digest", 6) == 0) {
        *outAuthType = XHttpProxyAuth_Digest;
        
        if (outParams) {
            memset(outParams, 0, sizeof(XHttpDigestParams));
            
            const char* p = authHeader + 6;
            while (*p == ' ') p++;
            
            // 解析Digest参数
            while (*p) {
                const char* eq = strchr(p, '=');
                if (!eq) break;
                
                size_t nameLen = eq - p;
                char name[32];
                if (nameLen >= sizeof(name)) nameLen = sizeof(name) - 1;
                strncpy(name, p, nameLen);
                name[nameLen] = '\0';
                
                p = eq + 1;
                char quote = (*p == '"') ? '"' : 0;
                if (quote) p++;
                
                const char* end;
                if (quote) {
                    end = strchr(p, quote);
                    if (!end) break;
                } else {
                    end = strchr(p, ',');
                    if (!end) end = p + strlen(p);
                }
                
                size_t valueLen = end - p;
                char* value = (char*)XMalloc_System(valueLen + 1);
                if (value) {
                    strncpy(value, p, valueLen);
                    value[valueLen] = '\0';
                }
                
                if (strcmp(name, "realm") == 0) outParams->realm = value;
                else if (strcmp(name, "nonce") == 0) outParams->nonce = value;
                else if (strcmp(name, "opaque") == 0) outParams->opaque = value;
                else if (strcmp(name, "algorithm") == 0) outParams->algorithm = value;
                else if (strcmp(name, "qop") == 0) outParams->qop = value;
                else XFree_System(value);
                
                p = end;
                if (quote) p++;
                while (*p == ' ' || *p == ',') p++;
            }
            
            // 生成cnonce
            outParams->cnonce = (char*)XMalloc_System(33);
            if (outParams->cnonce) {
                generateCnonce(outParams->cnonce, 33);
            }
            outParams->nc = 1;
        }
        
        return true;
    }
    
    if (strncmp(authHeader, "NTLM", 4) == 0) {
        *outAuthType = XHttpProxyAuth_NTLM;
        return true;
    }
    
    if (strncmp(authHeader, "Negotiate", 9) == 0) {
        *outAuthType = XHttpProxyAuth_Negotiate;
        return true;
    }
    
    return false;
}

int XHttpProxy_buildBasicAuth(
    char* buffer,
    size_t bufferSize,
    const char* username,
    const char* password
) {
    if (!buffer || !username || !password) {
        return -1;
    }
    
    // 构建 "username:password" 字符串
    size_t credLen = strlen(username) + 1 + strlen(password);
    char* credentials = (char*)XMalloc_System(credLen + 1);
    if (!credentials) {
        return -1;
    }
    
    snprintf(credentials, credLen + 1, "%s:%s", username, password);
    
    // Base64编码
    int result = snprintf(buffer, bufferSize, "Basic ");
    if (result < 0 || (size_t)result >= bufferSize) {
        XFree_System(credentials);
        return -1;
    }
    
    size_t offset = result;
    size_t encodedLen = bufferSize - offset;
    if (XBase64_encode((const uint8_t*)credentials, strlen(credentials),
                       buffer + offset, &encodedLen) != 0) {
        XFree_System(credentials);
        return -1;
    }
    
    XFree_System(credentials);
    
    return (int)(offset + encodedLen - 1); // XBase64包含null终止符，减1
}

int XHttpProxy_buildDigestAuth(
    char* buffer,
    size_t bufferSize,
    const char* method,
    const char* uri,
    const char* username,
    const char* password,
    const XHttpDigestParams* params
) {
    if (!buffer || !method || !uri || !username || !password || !params) {
        return -1;
    }
    
    if (!params->realm || !params->nonce) {
        return -1;
    }
    
    // 使用XCryptographicHash计算MD5
    // HA1 = MD5(username:realm:password)
    char ha1Input[512];
    snprintf(ha1Input, sizeof(ha1Input), "%s:%s:%s", username, params->realm, password);
    
    XByteArray* ha1Hash = XCryptographicHash_hash(ha1Input, strlen(ha1Input), XCryptographicHash_Md5);
    if (!ha1Hash) {
        return -1;
    }
    
    char ha1[33];
    byteArrayToHex(ha1Hash, ha1, sizeof(ha1));
    XByteArray_delete_base(ha1Hash);
    
    // HA2 = MD5(method:uri)
    char ha2Input[512];
    snprintf(ha2Input, sizeof(ha2Input), "%s:%s", method, uri);
    
    XByteArray* ha2Hash = XCryptographicHash_hash(ha2Input, strlen(ha2Input), XCryptographicHash_Md5);
    if (!ha2Hash) {
        return -1;
    }
    
    char ha2[33];
    byteArrayToHex(ha2Hash, ha2, sizeof(ha2));
    XByteArray_delete_base(ha2Hash);
    
    // 计算response
    char responseInput[1024];
    char nc[9];
    snprintf(nc, sizeof(nc), "%08x", params->nc);
    
    if (params->qop) {
        snprintf(responseInput, sizeof(responseInput), "%s:%s:%s:%s:%s:%s",
                 ha1, params->nonce, nc, params->cnonce, params->qop, ha2);
    } else {
        snprintf(responseInput, sizeof(responseInput), "%s:%s:%s",
                 ha1, params->nonce, ha2);
    }
    
    XByteArray* responseHash = XCryptographicHash_hash(responseInput, strlen(responseInput), XCryptographicHash_Md5);
    if (!responseHash) {
        return -1;
    }
    
    char response[33];
    byteArrayToHex(responseHash, response, sizeof(response));
    XByteArray_delete_base(responseHash);
    
    // 构建Authorization头
    int len;
    if (params->qop && params->opaque) {
        len = snprintf(buffer, bufferSize,
                       "Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", "
                       "uri=\"%s\", qop=%s, nc=%s, cnonce=\"%s\", "
                       "response=\"%s\", opaque=\"%s\"",
                       username, params->realm, params->nonce,
                       uri, params->qop, nc, params->cnonce,
                       response, params->opaque);
    } else if (params->qop) {
        len = snprintf(buffer, bufferSize,
                       "Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", "
                       "uri=\"%s\", qop=%s, nc=%s, cnonce=\"%s\", "
                       "response=\"%s\"",
                       username, params->realm, params->nonce,
                       uri, params->qop, nc, params->cnonce,
                       response);
    } else if (params->opaque) {
        len = snprintf(buffer, bufferSize,
                       "Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", "
                       "uri=\"%s\", response=\"%s\", opaque=\"%s\"",
                       username, params->realm, params->nonce,
                       uri, response, params->opaque);
    } else {
        len = snprintf(buffer, bufferSize,
                       "Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", "
                       "uri=\"%s\", response=\"%s\"",
                       username, params->realm, params->nonce,
                       uri, response);
    }
    
    return (len > 0 && (size_t)len < bufferSize) ? len : -1;
}

// =============== NTLM 实现 ===============

/**
 * @brief 计算NTLM哈希（NT Hash = MD4(UTF-16LE(password))）
 */
static void ntlmHash(const char* password, uint8_t* hashOut) {
    if (!password || !hashOut) return;
    
    size_t pwdLen = strlen(password);
    
    // 将密码转换为UTF-16LE
    // 每个ASCII字符变成2字节（低字节=字符，高字节=0）
    size_t utf16Len = pwdLen * 2;
    char* utf16Pwd = (char*)XMalloc_System(utf16Len);
    if (!utf16Pwd) return;
    
    for (size_t i = 0; i < pwdLen; i++) {
        utf16Pwd[i * 2] = password[i];
        utf16Pwd[i * 2 + 1] = 0;
    }
    
    // 使用MD4计算NT哈希
    XByteArray* hash = XCryptographicHash_hash(utf16Pwd, utf16Len, XCryptographicHash_Md4);
    if (hash) {
        const char* data = XByteArray_data(hash);
        size_t len = XByteArray_size_base(hash);
        if (len >= 16) {
            memcpy(hashOut, data, 16);
        }
        XByteArray_delete_base(hash);
    }
    
    XFree_System(utf16Pwd);
}

/**
 * @brief 计算NTLMv2哈希
 * @note NTLMv2 Hash = HMAC_MD5(NT_Hash, uppercase(username) + domain)
 */
static void ntlm2Hash(const char* password, const char* username, const char* domain,
                      uint8_t* hashOut) {
    if (!password || !username || !hashOut) return;
    
    // 首先计算NT哈希
    uint8_t ntHash[16] = {0};
    ntlmHash(password, ntHash);
    
    // 构建用户名+域名（大写）
    char userDomain[256];
    if (domain && *domain) {
        snprintf(userDomain, sizeof(userDomain), "%s%s", username, domain);
    } else {
        snprintf(userDomain, sizeof(userDomain), "%s", username);
    }
    
    // 转大写
    for (char* p = userDomain; *p; p++) {
        if (*p >= 'a' && *p <= 'z') {
            *p = *p - 'a' + 'A';
        }
    }
    
    // 使用HMAC-MD5：HMAC(key=ntHash, message=userDomain)
    XByteArray* hmacResult = XCryptographicHash_hmac(
        (const char*)ntHash, 16,
        userDomain, strlen(userDomain),
        XCryptographicHash_Md5
    );
    
    if (hmacResult) {
        const char* data = XByteArray_data(hmacResult);
        size_t len = XByteArray_size_base(hmacResult);
        if (len >= 16) {
            memcpy(hashOut, data, 16);
        }
        XByteArray_delete_base(hmacResult);
    }
}

int XHttpProxy_buildNtlmType1(
    uint8_t* buffer,
    size_t bufferSize,
    const char* domain,
    const char* workstation
) {
    if (!buffer || bufferSize < 16) {
        return -1;
    }
    
    // NTLM Type-1消息最小16字节
    memset(buffer, 0, bufferSize);
    
    // 签名 "NTLMSSP\0"
    memcpy(buffer, "NTLMSSP", 8);
    buffer[8] = 0x01; // 消息类型 Type-1
    buffer[9] = 0x00;
    buffer[10] = 0x00;
    buffer[11] = 0x00;
    
    // 标志
    uint32_t flags = 0x00000202; // Negotiate NTLM | Negotiate Unicode
    if (domain && *domain) {
        flags |= 0x00001000; // Negotiate Domain Supplied
    }
    if (workstation && *workstation) {
        flags |= 0x00002000; // Negotiate Workstation Supplied
    }
    
    buffer[12] = flags & 0xFF;
    buffer[13] = (flags >> 8) & 0xFF;
    buffer[14] = (flags >> 16) & 0xFF;
    buffer[15] = (flags >> 24) & 0xFF;
    
    // 简化实现，不添加域名和工作站名字段
    return 16;
}

bool XHttpProxy_parseNtlmType2(
    const uint8_t* buffer,
    size_t bufferLen,
    XNtlmContext* outContext
) {
    if (!buffer || bufferLen < 48 || !outContext) {
        return false;
    }
    
    // 验证签名
    if (memcmp(buffer, "NTLMSSP", 7) != 0) {
        return false;
    }
    
    // 验证消息类型
    if (buffer[8] != 0x02) {
        return false;
    }
    
    // 保存Type-2消息
    outContext->type2Message = XByteArray_create_with_data((const char*)buffer, bufferLen);
    if (!outContext->type2Message) {
        return false;
    }
    
    return true;
}

int XHttpProxy_buildNtlmType3(
    uint8_t* buffer,
    size_t bufferSize,
    const char* username,
    const char* password,
    const XNtlmContext* context
) {
    if (!buffer || !username || !password || !context || !context->type2Message) {
        return -1;
    }
    
    // 简化实现：构建基本的Type-3消息
    if (bufferSize < 64) {
        return -1;
    }
    
    memset(buffer, 0, bufferSize);
    
    // 签名
    memcpy(buffer, "NTLMSSP", 8);
    buffer[8] = 0x03; // Type-3
    
    // 从Type-2获取challenge（偏移24，8字节）
    const uint8_t* challenge = context->type2Message + 24;
    
    // 计算NTLM响应（简化）
    uint8_t ntlmHash[16] = {0};
    ntlm2Hash(password, username, NULL, ntlmHash);
    
    // LM响应（简化，填充0）
    size_t offset = 64;
    
    // LM响应字段
    buffer[12] = 24; buffer[13] = 0; // 长度
    buffer[14] = offset & 0xFF; buffer[15] = (offset >> 8) & 0xFF; // 偏移
    
    // NTLM响应字段
    buffer[20] = 24; buffer[21] = 0; // 长度
    buffer[22] = (offset + 24) & 0xFF; buffer[23] = ((offset + 24) >> 8) & 0xFF; // 偏移
    
    // 域名字段
    buffer[28] = 0; buffer[29] = 0; // 长度
    
    // 用户名字段
    size_t userLen = strlen(username);
    buffer[36] = userLen & 0xFF; buffer[37] = (userLen >> 8) & 0xFF;
    buffer[38] = (offset + 48) & 0xFF; buffer[39] = ((offset + 48) >> 8) & 0xFF;
    
    // 工作站名字段
    buffer[44] = 0; buffer[45] = 0; // 长度
    
    // 会话密钥字段
    buffer[52] = 0; buffer[53] = 0;
    
    // 标志
    buffer[60] = 0x02; buffer[61] = 0x02; buffer[62] = 0; buffer[63] = 0;
    
    // 填充响应数据
    if (bufferSize >= offset + 48 + userLen) {
        // LM响应（填充0）
        memset(buffer + offset, 0, 24);
        
        // NTLM响应（简化：使用challenge和hash）
        memcpy(buffer + offset + 24, challenge, 8);
        memcpy(buffer + offset + 32, ntlmHash, 16);
        
        // 用户名
        memcpy(buffer + offset + 48, username, userLen);
        
        return (int)(offset + 48 + userLen);
    }
    
    return 64;
}

int XHttpProxy_buildNtlmAuthHeader(
    char* buffer,
    size_t bufferSize,
    const uint8_t* ntlmMessage,
    size_t messageLen
) {
    if (!buffer || !ntlmMessage || messageLen == 0) {
        return -1;
    }
    
    int result = snprintf(buffer, bufferSize, "NTLM ");
    if (result < 0 || (size_t)result >= bufferSize) {
        return -1;
    }
    
    size_t offset = result;
    size_t encodedLen = bufferSize - offset;
    if (XBase64_encode(ntlmMessage, messageLen, buffer + offset, &encodedLen) != 0) {
        return -1;
    }
    
    return (int)(offset + encodedLen - 1); // XBase64包含null终止符，减1
}

// =============== 握手执行 ===============

bool XNetworkProxyHandshake_perform(
    XAbstractSocket* sock,
    XProxyHandshakeContext* ctx
) {
    if (!sock || !ctx || !ctx->proxy) {
        return false;
    }
    
    ctx->startTime = XDateTime_currentMSecsSinceEpoch();
    ctx->errorCode = XProxyHandshakeError_None;
    
    XNetworkProxy_ProxyType proxyType = XNetworkProxy_type(ctx->proxy);
    
    if (proxyType == XNetworkProxy_Socks5Proxy) {
        // SOCKS5握手
        ctx->state = XProxyHandshakeState_Socks5_Greeting;
        
        // 构建认证方法协商请求
        XSocks5AuthMethod methods[3];
        size_t methodCount = 0;
        
        const XString* user = XNetworkProxy_user_const(ctx->proxy);
        const XString* pass = XNetworkProxy_password_const(ctx->proxy);
        
        methods[methodCount++] = XSocks5Auth_NoAuth;
        if (user && pass) {
            methods[methodCount++] = XSocks5Auth_UsernamePassword;
        }
        
        uint8_t greetBuf[16];
        size_t greetBufSize = sizeof(greetBuf);
        int reqLen = XSocks5_buildGreetingRequest(greetBuf, greetBufSize,
                                                   methods, methodCount);
        if (reqLen < 0) {
            ctx->errorCode = XProxyHandshakeError_Unknown;
            ctx->state = XProxyHandshakeState_Failed;
            return false;
        }
        
        // 发送协商请求
        int64_t written = XAbstractSocket_write(sock, (const char*)greetBuf, reqLen);
        if (written != reqLen) {
            ctx->errorCode = XProxyHandshakeError_ConnectionClosed;
            ctx->state = XProxyHandshakeState_Failed;
            return false;
        }
        
        ctx->state = XProxyHandshakeState_Socks5_WaitGreeting;
        ctx->socks5BytesNeeded = 2;
        
        // 等待响应
        while (ctx->state != XProxyHandshakeState_Completed && 
               ctx->state != XProxyHandshakeState_Failed) {
            
            // 检查超时
            if (ctx->timeoutMs > 0) {
                int64_t elapsed = XDateTime_currentMSecsSinceEpoch() - ctx->startTime;
                if (elapsed > ctx->timeoutMs) {
                    ctx->errorCode = XProxyHandshakeError_Timeout;
                    ctx->state = XProxyHandshakeState_Failed;
                    return false;
                }
            }
            
            XProxyHandshakeState newState = XNetworkProxyHandshake_process(sock, ctx);
            if (newState == ctx->state) {
                // 状态未变化，等待数据
                // 简化实现：使用阻塞等待
            }
        }
        
        return ctx->state == XProxyHandshakeState_Completed;
    }
    else if (proxyType == XNetworkProxy_HttpProxy) {
        // HTTP CONNECT握手
        ctx->state = XProxyHandshakeState_Http_Connecting;
        
        // 构建初始CONNECT请求
        char request[1024];
        int reqLen = XHttpProxy_buildConnectRequest(request, sizeof(request),
                                                     ctx->targetHost, ctx->targetPort,
                                                     ctx->proxy, XHttpProxyAuth_None, NULL);
        if (reqLen < 0) {
            ctx->errorCode = XProxyHandshakeError_Unknown;
            ctx->state = XProxyHandshakeState_Failed;
            return false;
        }
        
        // 发送请求
        int64_t written = XAbstractSocket_write(sock, request, reqLen);
        if (written != reqLen) {
            ctx->errorCode = XProxyHandshakeError_ConnectionClosed;
            ctx->state = XProxyHandshakeState_Failed;
            return false;
        }
        
        ctx->state = XProxyHandshakeState_Http_WaitResponse;
        ctx->httpBuffer = XByteArray_create();
        
        // 等待响应
        while (ctx->state != XProxyHandshakeState_Completed && 
               ctx->state != XProxyHandshakeState_Failed) {
            
            if (ctx->timeoutMs > 0) {
                int64_t elapsed = XDateTime_currentMSecsSinceEpoch() - ctx->startTime;
                if (elapsed > ctx->timeoutMs) {
                    ctx->errorCode = XProxyHandshakeError_Timeout;
                    ctx->state = XProxyHandshakeState_Failed;
                    return false;
                }
            }
            
            XNetworkProxyHandshake_process(sock, ctx);
        }
        
        return ctx->state == XProxyHandshakeState_Completed;
    }
    
    ctx->errorCode = XProxyHandshakeError_MethodNotSupported;
    ctx->state = XProxyHandshakeState_Failed;
    return false;
}

bool XNetworkProxyHandshake_start(
    XAbstractSocket* sock,
    XProxyHandshakeContext* ctx
) {
    if (!sock || !ctx || !ctx->proxy) {
        return false;
    }
    
    ctx->startTime = XDateTime_currentMSecsSinceEpoch();
    ctx->errorCode = XProxyHandshakeError_None;
    
    XNetworkProxy_ProxyType proxyType = XNetworkProxy_type(ctx->proxy);
    
    if (proxyType == XNetworkProxy_Socks5Proxy) {
        // SOCKS5异步握手开始
        ctx->state = XProxyHandshakeState_Socks5_Greeting;
        
        // 构建认证方法协商请求
        XSocks5AuthMethod methods[3];
        size_t methodCount = 0;
        
        const XString* user = XNetworkProxy_user_const(ctx->proxy);
        const XString* pass = XNetworkProxy_password_const(ctx->proxy);
        
        methods[methodCount++] = XSocks5Auth_NoAuth;
        if (user && pass) {
            methods[methodCount++] = XSocks5Auth_UsernamePassword;
        }
        
        uint8_t greetBuf[16];
        int reqLen = XSocks5_buildGreetingRequest(greetBuf, sizeof(greetBuf),
                                                   methods, methodCount);
        if (reqLen < 0) {
            ctx->errorCode = XProxyHandshakeError_Unknown;
            ctx->state = XProxyHandshakeState_Failed;
            return false;
        }
        
        // 发送协商请求（非阻塞）
        int64_t written = XAbstractSocket_write(sock, (const char*)greetBuf, reqLen);
        if (written != reqLen) {
            ctx->errorCode = XProxyHandshakeError_ConnectionClosed;
            ctx->state = XProxyHandshakeState_Failed;
            return false;
        }
        
        ctx->state = XProxyHandshakeState_Socks5_WaitGreeting;
        ctx->socks5BytesNeeded = 2;
        XByteArray_clear_base(ctx->socks5Buffer);
        ctx->socks5BufferLen = 0;
        
        return true;
    }
    else if (proxyType == XNetworkProxy_HttpProxy) {
        // HTTP CONNECT异步握手开始
        ctx->state = XProxyHandshakeState_Http_Connecting;
        
        // 构建初始CONNECT请求
        char request[1024];
        int reqLen = XHttpProxy_buildConnectRequest(request, sizeof(request),
                                                     ctx->targetHost, ctx->targetPort,
                                                     ctx->proxy, XHttpProxyAuth_None, NULL);
        if (reqLen < 0) {
            ctx->errorCode = XProxyHandshakeError_Unknown;
            ctx->state = XProxyHandshakeState_Failed;
            return false;
        }
        
        // 发送请求（非阻塞）
        int64_t written = XAbstractSocket_write(sock, request, reqLen);
        if (written != reqLen) {
            ctx->errorCode = XProxyHandshakeError_ConnectionClosed;
            ctx->state = XProxyHandshakeState_Failed;
            return false;
        }
        
        ctx->state = XProxyHandshakeState_Http_WaitResponse;
        XByteArray_clear_base(ctx->httpBuffer);
        
        return true;
    }
    
    ctx->errorCode = XProxyHandshakeError_MethodNotSupported;
    ctx->state = XProxyHandshakeState_Failed;
    return false;
}

XProxyHandshakeState XNetworkProxyHandshake_process(
    XAbstractSocket* sock,
    XProxyHandshakeContext* ctx
) {
    if (!sock || !ctx) {
        return XProxyHandshakeState_Failed;
    }
    
    switch (ctx->state) {
        case XProxyHandshakeState_Socks5_WaitGreeting: {
            // 等待认证方法响应
            uint8_t tempBuf[16];
            int64_t bytesRead = XAbstractSocket_read(sock, (char*)tempBuf, sizeof(tempBuf));
            if (bytesRead > 0) {
                XByteArray_append_2(ctx->socks5Buffer, tempBuf, bytesRead);
                ctx->socks5BufferLen = XByteArray_size_base(ctx->socks5Buffer);
                
                if (ctx->socks5BufferLen >= ctx->socks5BytesNeeded) {
                    XSocks5AuthMethod method;
                    const uint8_t* data = (const uint8_t*)XByteArray_data(ctx->socks5Buffer);
                    if (XSocks5_parseGreetingResponse(data, ctx->socks5BufferLen, &method)) {
                        ctx->socks5AuthMethod = method;
                        
                        if (method == XSocks5Auth_NoAuth) {
                            // 无需认证，发送请求
                            ctx->state = XProxyHandshakeState_Socks5_Requesting;
                        }
                        else if (method == XSocks5Auth_UsernamePassword) {
                            // 需要用户名密码认证
                            ctx->state = XProxyHandshakeState_Socks5_Authenticating;
                        }
                        else {
                            ctx->errorCode = XProxyHandshakeError_MethodNotSupported;
                            ctx->state = XProxyHandshakeState_Failed;
                        }
                    }
                }
            }
            break;
        }
        
        case XProxyHandshakeState_Socks5_Authenticating: {
            // 发送用户名密码认证
            const XString* userStr = XNetworkProxy_user_const(ctx->proxy);
            const XString* passStr = XNetworkProxy_password_const(ctx->proxy);
            
            // 获取UTF-8字符串
            const char* username = userStr ? XString_toUtf8(userStr) : "";
            const char* password = passStr ? XString_toUtf8(passStr) : "";
            
            // 预分配缓冲区（用户名+密码+开销，最大513字节）
            XByteArray_resize_base(ctx->socks5Buffer, 513);
            uint8_t* bufferData = (uint8_t*)XByteArray_data(ctx->socks5Buffer);
            
            int reqLen = XSocks5_buildAuthRequest(bufferData, 513, username, password);
            if (reqLen > 0) {
                XAbstractSocket_write(sock, (const char*)bufferData, reqLen);
                ctx->state = XProxyHandshakeState_Socks5_WaitAuth;
                ctx->socks5BufferLen = 0;
                ctx->socks5BytesNeeded = 2;
            }
            break;
        }
        
        case XProxyHandshakeState_Socks5_WaitAuth: {
            // 确保缓冲区足够大
            XByteArray_resize_base(ctx->socks5Buffer, 16);
            char* bufferData = XByteArray_data(ctx->socks5Buffer);
            
            int64_t bytesRead = XAbstractSocket_read(sock, bufferData + ctx->socks5BufferLen,
                                                      ctx->socks5BytesNeeded - ctx->socks5BufferLen);
            if (bytesRead > 0) {
                ctx->socks5BufferLen += bytesRead;
                
                if (ctx->socks5BufferLen >= 2) {
                    if (XSocks5_parseAuthResponse((uint8_t*)bufferData, ctx->socks5BufferLen)) {
                        ctx->state = XProxyHandshakeState_Socks5_Requesting;
                    } else {
                        ctx->errorCode = XProxyHandshakeError_AuthFailed;
                        ctx->state = XProxyHandshakeState_Failed;
                    }
                }
            }
            break;
        }
        
        case XProxyHandshakeState_Socks5_Requesting: {
            // 预分配缓冲区（最大请求长度：域名最大255+开销）
            XByteArray_resize_base(ctx->socks5Buffer, 262);
            uint8_t* bufferData = (uint8_t*)XByteArray_data(ctx->socks5Buffer);
            const char* targetHost = ctx->targetHost ? XString_toUtf8(ctx->targetHost) : NULL;
            
            int reqLen = XSocks5_buildRequest(bufferData, 262,
                                               ctx->socks5Command, targetHost, ctx->targetPort);
            if (reqLen > 0) {
                XAbstractSocket_write(sock, (const char*)bufferData, reqLen);
                ctx->state = XProxyHandshakeState_Socks5_WaitReply;
                ctx->socks5BufferLen = 0;
                ctx->socks5BytesNeeded = 10; // 最小响应长度
            }
            break;
        }
        
        case XProxyHandshakeState_Socks5_WaitReply: {
            // 确保缓冲区足够大（最大响应：IPv6地址+端口=22字节）
            XByteArray_resize_base(ctx->socks5Buffer, 64);
            char* bufferData = XByteArray_data(ctx->socks5Buffer);
            size_t bufferSize = XByteArray_size_base(ctx->socks5Buffer);
            
            int64_t bytesRead = XAbstractSocket_read(sock, bufferData + ctx->socks5BufferLen,
                                                      bufferSize - ctx->socks5BufferLen);
            if (bytesRead > 0) {
                ctx->socks5BufferLen += bytesRead;
                
                if (ctx->socks5BufferLen >= 10) {
                    XSocks5ReplyCode replyCode;
                    if (XSocks5_parseReply((uint8_t*)bufferData, ctx->socks5BufferLen,
                                           &replyCode, &ctx->bindResult.bindAddress,
                                           &ctx->bindResult.bindPort)) {
                        if (replyCode == XSocks5Rep_Succeeded) {
                            // 根据命令类型决定下一步
                            if (ctx->socks5Command == XSocks5Cmd_Bind) {
                                // BIND命令需要等待第二次响应
                                ctx->state = XProxyHandshakeState_Socks5_BindWaiting;
                                ctx->socks5BufferLen = 0;
                                ctx->socks5BytesNeeded = 10;
                            } else {
                                ctx->state = XProxyHandshakeState_Completed;
                            }
                        } else {
                            ctx->errorCode = (XProxyHandshakeError)(XProxyHandshakeError_Socks5GeneralFailure + replyCode - 1);
                            ctx->state = XProxyHandshakeState_Failed;
                        }
                    }
                }
            }
            break;
        }
        
        case XProxyHandshakeState_Socks5_BindWaiting: {
            // BIND第二次响应等待（FTP被动模式）
            // 代理服务器在远程客户端连接后会发送第二次响应
            XByteArray_resize_base(ctx->socks5Buffer, 64);
            char* bufferData = XByteArray_data(ctx->socks5Buffer);
            size_t bufferSize = XByteArray_size_base(ctx->socks5Buffer);
            
            int64_t bytesRead = XAbstractSocket_read(sock, bufferData + ctx->socks5BufferLen,
                                                      bufferSize - ctx->socks5BufferLen);
            if (bytesRead > 0) {
                ctx->socks5BufferLen += bytesRead;
                
                if (ctx->socks5BufferLen >= 10) {
                    XSocks5ReplyCode replyCode;
                    XHostAddress remoteAddress;
                    uint16_t remotePort = 0;
                    
                    if (XSocks5_parseReply((uint8_t*)bufferData, ctx->socks5BufferLen,
                                           &replyCode, &remoteAddress, &remotePort)) {
                        if (replyCode == XSocks5Rep_Succeeded) {
                            // 第二次响应成功，远程客户端已连接
                            ctx->bindResult.hasSecondConnection = true;
                            ctx->state = XProxyHandshakeState_Completed;
                        } else {
                            ctx->errorCode = (XProxyHandshakeError)(XProxyHandshakeError_Socks5GeneralFailure + replyCode - 1);
                            ctx->state = XProxyHandshakeState_Failed;
                        }
                    }
                }
            }
            break;
        }
        
        case XProxyHandshakeState_Http_WaitResponse: {
            char tempBuf[4096];
            int64_t bytesRead = XAbstractSocket_read(sock, tempBuf, sizeof(tempBuf) - 1);
            if (bytesRead > 0) {
                XByteArray_append_2(ctx->httpBuffer, tempBuf, bytesRead);
                
                // 检查是否收到完整响应
                const char* httpData = XByteArray_data(ctx->httpBuffer);
                size_t httpBufferLen = XByteArray_size_base(ctx->httpBuffer);
                if (strstr(httpData, "\r\n\r\n")) {
                    int code;
                    char* authHeader = NULL;
                    
                    if (XHttpProxy_parseResponse(httpData, httpBufferLen,
                                                  &code, NULL, &authHeader)) {
                        if (code == 200) {
                            // 成功
                            ctx->state = XProxyHandshakeState_Completed;
                        }
                        else if (code == 407) {
                            // 需要认证
                        if (authHeader) {
                                XHttpProxyAuthType authType;
                                if (XHttpProxy_parseAuthHeader(authHeader, &authType, &ctx->digestParams)) {
                                    ctx->httpAuthType = authType;
                                    ctx->httpAuthHeader = authHeader;
                                    ctx->state = XProxyHandshakeState_Http_Authenticating;
                                } else {
                                    XFree_System(authHeader);
                                    ctx->errorCode = XProxyHandshakeError_AuthRequired;
                                    ctx->state = XProxyHandshakeState_Failed;
                                }
                            } else {
                                ctx->errorCode = XProxyHandshakeError_AuthRequired;
                                ctx->state = XProxyHandshakeState_Failed;
                            }
                        }
                        else {
                            ctx->errorCode = XProxyHandshakeError_HttpError;
                            char errBuf[128];
                            snprintf(errBuf, sizeof(errBuf), "HTTP error: %d", code);
                            XString_assign_utf8(ctx->errorMessage, errBuf);
                            ctx->state = XProxyHandshakeState_Failed;
                        }
                    }
                }
            }
            break;
        }
        
        case XProxyHandshakeState_Http_Authenticating: {
            char authValue[1024];
            const XString* userStr = XNetworkProxy_user_const(ctx->proxy);
            const XString* passStr = XNetworkProxy_password_const(ctx->proxy);
            
            // 获取UTF-8字符串
            const char* username = userStr ? XString_toUtf8(userStr) : "";
            const char* password = passStr ? XString_toUtf8(passStr) : "";
            
            int authLen = -1;
            
            if (ctx->httpAuthType == XHttpProxyAuth_Basic) {
                authLen = XHttpProxy_buildBasicAuth(authValue, sizeof(authValue),
                                                     username, password);
            }
            else if (ctx->httpAuthType == XHttpProxyAuth_Digest) {
                char uri[256];
                snprintf(uri, sizeof(uri), "%s:%u", ctx->targetHost, ctx->targetPort);
                authLen = XHttpProxy_buildDigestAuth(authValue, sizeof(authValue),
                                                      "CONNECT", uri, username, password,
                                                      &ctx->digestParams);
            }
            else if (ctx->httpAuthType == XHttpProxyAuth_NTLM) {
                // NTLM认证流程
                uint8_t ntlmMsg[256];
                int msgLen;
                
                if (!ctx->ntlmContext.type1Message) {
                    // 发送Type-1
                    msgLen = XHttpProxy_buildNtlmType1(ntlmMsg, sizeof(ntlmMsg), NULL, NULL);
                    if (msgLen > 0) {
                        ctx->ntlmContext.type1Message = XByteArray_create_with_data((const char*)ntlmMsg, msgLen);
                        
                        authLen = XHttpProxy_buildNtlmAuthHeader(authValue, sizeof(authValue),
                                                                  ntlmMsg, msgLen);
                    }
                } else {
                    // 发送Type-3
                    msgLen = XHttpProxy_buildNtlmType3(ntlmMsg, sizeof(ntlmMsg),
                                                        username, password, &ctx->ntlmContext);
                    if (msgLen > 0) {
                        authLen = XHttpProxy_buildNtlmAuthHeader(authValue, sizeof(authValue),
                                                                  ntlmMsg, msgLen);
                    }
                }
            }
            else if (ctx->httpAuthType == XHttpProxyAuth_Negotiate) {
                // Negotiate/SPNEGO认证流程（使用GSSAPI）
                XByteArray* outputToken = XByteArray_create();
                if (outputToken) {
                    // 构建服务名称（HTTP@proxyhost）
                    const XString* proxyHost = XNetworkProxy_hostName_const(ctx->proxy);
                    const char* hostName = proxyHost ? XString_toUtf8(proxyHost) : "proxy";
                    char serviceName[256];
                    snprintf(serviceName, sizeof(serviceName), "HTTP@%s", hostName);
                    
                    // 调用平台GSSAPI认证
                    XString* svcStr = XString_create_utf8(serviceName);
                    int gssResult = XNetwork_gssapiAuth(svcStr, NULL, outputToken, &ctx->gssContext);
                    XString_delete_base(svcStr);
                    
                    if (gssResult >= 0 && XByteArray_size_base(outputToken) > 0) {
                        // Base64编码输出令牌
                        const char* tokenData = XByteArray_data(outputToken);
                        size_t tokenLen = XByteArray_size_base(outputToken);
                        
                        // 构建Negotiate认证头
                        size_t encodedLen = ((tokenLen + 2) / 3) * 4 + 1;
                        char* encoded = (char*)XMalloc_System(encodedLen);
                        if (encoded) {
                            // 简单Base64编码（使用XCryptographicHash如果有）
                            extern int XBase64_encode(const uint8_t* input, size_t inputLen, char* output, size_t* outputLen);
                            size_t outLen = encodedLen;
                            if (XBase64_encode((const uint8_t*)tokenData, tokenLen, encoded, &outLen) == 0 && outLen > 0) {
                                snprintf(authValue, sizeof(authValue), "Negotiate %s", encoded);
                                authLen = (int)strlen(authValue);
                            }
                            XFree_System(encoded);
                        }
                    }
                    
                    XByteArray_delete_base(outputToken);
                }
            }
            
            if (authLen > 0) {
                // 重新发送CONNECT请求
                char request[2048];
                int reqLen = XHttpProxy_buildConnectRequest(request, sizeof(request),
                                                             ctx->targetHost, ctx->targetPort,
                                                             ctx->proxy, ctx->httpAuthType, authValue);
                if (reqLen > 0) {
                    XAbstractSocket_write(sock, request, reqLen);
                    XByteArray_clear_base(ctx->httpBuffer);
                    ctx->state = XProxyHandshakeState_Http_WaitResponse;
                }
            } else {
                ctx->errorCode = XProxyHandshakeError_AuthFailed;
                ctx->state = XProxyHandshakeState_Failed;
            }
            break;
        }
        
        case XProxyHandshakeState_Http_ChunkedBody: {
            // 处理分块传输编码响应体
            // 某些代理服务器可能返回分块响应
            char tempBuf[4096];
            int64_t bytesRead = XAbstractSocket_read(sock, tempBuf, sizeof(tempBuf) - 1);
            if (bytesRead > 0) {
                XByteArray_append_2(ctx->httpBuffer, tempBuf, bytesRead);
                
                const char* httpData = XByteArray_data(ctx->httpBuffer);
                size_t httpBufferLen = XByteArray_size_base(ctx->httpBuffer);
                
                // 查找分块结束标记 "0\r\n\r\n"
                const char* chunkEnd = strstr(httpData, "\r\n0\r\n\r\n");
                if (chunkEnd) {
                    // 分块传输结束，检查最终响应
                    // 重新解析响应
                    int code;
                    if (XHttpProxy_parseResponse(httpData, httpBufferLen, &code, NULL, NULL)) {
                        if (code == 200) {
                            ctx->state = XProxyHandshakeState_Completed;
                        } else {
                            ctx->errorCode = XProxyHandshakeError_HttpError;
                            ctx->state = XProxyHandshakeState_Failed;
                        }
                    } else {
                        ctx->errorCode = XProxyHandshakeError_InvalidResponse;
                        ctx->state = XProxyHandshakeState_Failed;
                    }
                }
            } else if (bytesRead == 0) {
                // 连接关闭
                ctx->errorCode = XProxyHandshakeError_ConnectionClosed;
                ctx->state = XProxyHandshakeState_Failed;
            }
            break;
        }
        
        default:
            break;
    }
    
    return ctx->state;
}

bool XNetworkProxyHandshake_isCompleted(const XProxyHandshakeContext* ctx) {
    return ctx && ctx->state == XProxyHandshakeState_Completed;
}

bool XNetworkProxyHandshake_isFailed(const XProxyHandshakeContext* ctx) {
    return ctx && ctx->state == XProxyHandshakeState_Failed;
}

XProxyHandshakeError XNetworkProxyHandshake_errorCode(const XProxyHandshakeContext* ctx) {
    return ctx ? ctx->errorCode : XProxyHandshakeError_Unknown;
}

const char* XNetworkProxyHandshake_errorMessage(const XProxyHandshakeContext* ctx) {
    if (!ctx) {
        return "Unknown error";
    }
    
    if (ctx->errorMessage && XString_size_base(ctx->errorMessage) > 0) {
        return XString_toUtf8(ctx->errorMessage);
    }
    
    switch (ctx->errorCode) {
        case XProxyHandshakeError_None: return "No error";
        case XProxyHandshakeError_Timeout: return "Operation timed out";
        case XProxyHandshakeError_ConnectionRefused: return "Connection refused";
        case XProxyHandshakeError_ConnectionClosed: return "Connection closed";
        case XProxyHandshakeError_ProxyNotFound: return "Proxy server not found";
        case XProxyHandshakeError_InvalidResponse: return "Invalid proxy response";
        case XProxyHandshakeError_AuthFailed: return "Authentication failed";
        case XProxyHandshakeError_AuthRequired: return "Authentication required";
        case XProxyHandshakeError_MethodNotSupported: return "Authentication method not supported";
        case XProxyHandshakeError_Socks5GeneralFailure: return "SOCKS5 general failure";
        case XProxyHandshakeError_Socks5NotAllowed: return "SOCKS5 connection not allowed";
        case XProxyHandshakeError_Socks5NetworkUnreachable: return "SOCKS5 network unreachable";
        case XProxyHandshakeError_Socks5HostUnreachable: return "SOCKS5 host unreachable";
        case XProxyHandshakeError_Socks5ConnectionRefused: return "SOCKS5 connection refused";
        case XProxyHandshakeError_Socks5TTLExpired: return "SOCKS5 TTL expired";
        case XProxyHandshakeError_Socks5CommandUnsupported: return "SOCKS5 command unsupported";
        case XProxyHandshakeError_Socks5AddressUnsupported: return "SOCKS5 address unsupported";
        case XProxyHandshakeError_HttpError: return "HTTP proxy error";
        default: return "Unknown error";
    }
}

const XSocks5BindResult* XNetworkProxyHandshake_bindResult(const XProxyHandshakeContext* ctx) {
    return ctx ? &ctx->bindResult : NULL;
}

const XSocks5UdpAssociateResult* XNetworkProxyHandshake_udpResult(const XProxyHandshakeContext* ctx) {
    return ctx ? &ctx->udpResult : NULL;
}

// =============== 系统代理获取API ===============

/**
 * @brief 从环境变量获取代理配置
 */
static bool getProxyFromEnv(const char* envVar, XNetworkProxy* outProxy);

static bool getProxyFromEnv(const char* envVar, XNetworkProxy* outProxy) {
    const char* proxyEnv = getenv(envVar);
    if (!proxyEnv || !*proxyEnv) {
        return false;
    }
    
    // 解析代理URL (格式: http://host:port 或 socks5://host:port)
    const char* hostStart = NULL;
    uint16_t port = 0;
    XNetworkProxy_ProxyType proxyType = XNetworkProxy_HttpProxy;
    
    if (strncmp(proxyEnv, "socks5://", 9) == 0) {
        proxyType = XNetworkProxy_Socks5Proxy;
        hostStart = proxyEnv + 9;
    } else if (strncmp(proxyEnv, "socks4://", 9) == 0) {
        // SOCKS4 作为 SOCKS5 处理（简化）
        proxyType = XNetworkProxy_Socks5Proxy;
        hostStart = proxyEnv + 9;
    } else if (strncmp(proxyEnv, "http://", 7) == 0) {
        proxyType = XNetworkProxy_HttpProxy;
        hostStart = proxyEnv + 7;
    } else if (strncmp(proxyEnv, "https://", 8) == 0) {
        proxyType = XNetworkProxy_HttpProxy;
        hostStart = proxyEnv + 8;
    } else {
        // 默认为HTTP代理
        hostStart = proxyEnv;
    }
    
    // 查找端口
    const char* portSep = strchr(hostStart, ':');
    const char* pathSep = strchr(hostStart, '/');
    
    char host[256] = {0};
    if (portSep) {
        size_t hostLen = portSep - hostStart;
        if (hostLen >= sizeof(host)) hostLen = sizeof(host) - 1;
        strncpy(host, hostStart, hostLen);
        host[hostLen] = '\0';
        
        // 解析端口
        const char* portStart = portSep + 1;
        size_t portLen = pathSep ? (size_t)(pathSep - portStart) : strlen(portStart);
        char portStr[8] = {0};
        if (portLen >= sizeof(portStr)) portLen = sizeof(portStr) - 1;
        strncpy(portStr, portStart, portLen);
        port = (uint16_t)atoi(portStr);
    } else {
        // 没有端口，使用默认端口
        size_t hostLen = pathSep ? (size_t)(pathSep - hostStart) : strlen(hostStart);
        if (hostLen >= sizeof(host)) hostLen = sizeof(host) - 1;
        strncpy(host, hostStart, hostLen);
        host[hostLen] = '\0';
        port = (proxyType == XNetworkProxy_Socks5Proxy) ? 1080 : 8080;
    }
    
    if (host[0] && port > 0) {
        XNetworkProxy_setType(outProxy, proxyType);
        XString* hostStr = XString_create_utf8(host);
        if (hostStr) {
            XNetworkProxy_setHostName(outProxy, hostStr);
            XString_delete_base(hostStr);
        }
        XNetworkProxy_setPort(outProxy, port);
        return true;
    }
    
    return false;
}

bool XNetworkProxy_getSystemProxy(
    const XNetworkProxyQuery* query,
    XNetworkProxy* outProxy
) {
    if (!outProxy) {
        return false;
    }
    
    // 初始化为无代理
    XNetworkProxy_setType(outProxy, XNetworkProxy_NoProxy);
    
    // 构建查询URL（如果提供了查询信息）
    const char* queryUrl = NULL;
    char queryUrlBuf[512] = {0};
    if (query && query->peerHostName) {
        const char* hostName = XString_toUtf8(query->peerHostName);
        if (hostName) {
            uint16_t port = query->peerPort;
            if (port == 0) {
                // 根据查询类型推断默认端口
                switch (XNetworkProxyQuery_queryType(query)) {
                    case XNetworkProxyQuery_UrlRequest:
                        port = 443; // HTTPS
                        break;
                    default:
                        port = 80;
                        break;
                }
            }
            snprintf(queryUrlBuf, sizeof(queryUrlBuf), "https://%s:%u", hostName, port);
            queryUrl = queryUrlBuf;
        }
    }
    
    // 首先尝试平台特定的系统代理获取（Windows/macOS）
    // XNetwork_getSystemProxy 在 Windows 上使用 WinHTTP，在其他平台可能使用不同机制
    XString* queryUrlStr = queryUrl ? XString_create_utf8(queryUrl) : NULL;
    bool sysProxyOk = XNetwork_getSystemProxy(queryUrlStr, outProxy);
    XString_delete_base(queryUrlStr);
    if (sysProxyOk) {
        // 平台函数成功获取代理
        // 检查是否需要绕过代理
        if (query && query->peerHostName) {
            const char* peerName = XString_toUtf8(query->peerHostName);
            // Windows 平台函数已经处理了 bypass 检查
            // 这里我们信任平台函数的结果
            (void)peerName; // 避免未使用警告
        }
        return true;
    }
    
    // 平台函数失败或不可用，回退到环境变量
    const char* httpProxy = getenv("http_proxy");
    const char* httpsProxy = getenv("https_proxy");
    const char* allProxy = getenv("all_proxy");
    
    // 检查no_proxy
    const char* noProxy = getenv("no_proxy");
    if (query && query->peerHostName && noProxy) {
        const char* peerName = XString_toUtf8(query->peerHostName);
        if (peerName && XNetworkProxy_isBypassed(peerName, noProxy)) {
            return true;
        }
    }
    
    // 优先使用all_proxy
    if (allProxy && *allProxy) {
        return getProxyFromEnv("all_proxy", outProxy);
    }
    
    // 根据协议选择代理
    if (query) {
        XNetworkProxyQuery_QueryType queryType = XNetworkProxyQuery_queryType(query);
        switch (queryType) {
            case XNetworkProxyQuery_TcpSocket:
            case XNetworkProxyQuery_UdpSocket:
            case XNetworkProxyQuery_UrlRequest:
                if (httpsProxy && *httpsProxy) {
                    return getProxyFromEnv("https_proxy", outProxy);
                }
                if (httpProxy && *httpProxy) {
                    return getProxyFromEnv("http_proxy", outProxy);
                }
                break;
            default:
                break;
        }
    }
    
    // 尝试http_proxy
    if (httpProxy && *httpProxy) {
        return getProxyFromEnv("http_proxy", outProxy);
    }
    
    return true;
}

bool XNetworkProxy_isBypassed(
    const char* url,
    const char* bypassList
) {
    if (!url || !bypassList) {
        return false;
    }
    
    // 复制bypassList以便解析
    size_t listLen = strlen(bypassList);
    char* listCopy = (char*)XMalloc_System(listLen + 1);
    if (!listCopy) {
        return false;
    }
    memcpy(listCopy, bypassList, listLen + 1);
    
    bool result = false;
    char* token = strtok(listCopy, ",;");
    
    while (token) {
        // 跳过空白
        while (*token == ' ' || *token == '\t') token++;
        
        size_t tokenLen = strlen(token);
        // 移除尾部空白
        while (tokenLen > 0 && (token[tokenLen - 1] == ' ' || token[tokenLen - 1] == '\t')) {
            token[--tokenLen] = '\0';
        }
        
        // 检查匹配
        if (strcmp(token, "*") == 0) {
            result = true;
            break;
        }
        
        // 检查域名后缀匹配
        if (token[0] == '.') {
            // 匹配子域名
            size_t urlLen = strlen(url);
            if (urlLen >= tokenLen) {
                if (strcmp(url + urlLen - tokenLen, token) == 0) {
                    result = true;
                    break;
                }
            }
        } else if (strcmp(token, "<local>") == 0) {
            // 本地地址（无点号）
            if (strchr(url, '.') == NULL) {
                result = true;
                break;
            }
        } else {
            // 精确匹配或后缀匹配
            size_t urlLen = strlen(url);
            size_t tLen = strlen(token);
            if (urlLen == tLen && strcmp(url, token) == 0) {
                result = true;
                break;
            }
            // 检查是否是主机名的一部分
            if (urlLen > tLen && url[urlLen - tLen - 1] == '.' &&
                strcmp(url + urlLen - tLen, token) == 0) {
                result = true;
                break;
            }
        }
        
        token = strtok(NULL, ",;");
    }
    
    XFree_System(listCopy);
    return result;
}
#endif // XNETWORK_PROXY_ON
#endif /* XNETWORK_ON */
