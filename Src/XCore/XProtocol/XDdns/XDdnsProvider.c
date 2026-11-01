/** @file       XDdnsProvider.c
 * @brief      XDdns 厂商查找表与公共枚举辅助的实现。
 * @details    新增厂商的接线步骤：实现一个 ops 实例（参照 XDdnsAliyun.c）
 *             并在 xdns_provider_find 的查找链中登记一行。
 * @author     XinYueC 团队
 */
#include "XDdnsProvider.h"
#include "XDdnsAliyun.h"

#if XDDNS_ON

const char* XDdnsRecordType_name(XDdnsRecordType type)
{
    switch (type) {
    case XDdnsRecordType_AAAA:
        return "AAAA";
    case XDdnsRecordType_A:
    default:
        return "A";
    }
}

const XDdnsProviderOps* XDdnsProvider_find(const char* name)
{
#if XDDNS_ALIYUN_ON
    if (name && strcmp(name, XDDNS_PROVIDER_ALIYUN) == 0)
        return XDdnsProvider_aliyun();
#endif
    (void)name;
    return NULL;
}

int XDdnsProvider_count(void)
{
    int count = 0;
#if XDDNS_ALIYUN_ON
    ++count;
#endif
    return count;
}

const XDdnsProviderOps* XDdnsProvider_at(int index)
{
#if XDDNS_ALIYUN_ON
    if (index == 0)
        return XDdnsProvider_aliyun();
#else
    (void)index;
#endif
    return NULL;
}

#endif /* XDDNS_ON */
