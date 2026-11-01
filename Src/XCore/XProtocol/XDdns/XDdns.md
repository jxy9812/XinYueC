# XDdns 动态域名解析客户端

> 版本：v1.0　|　阿里云 API：云解析 DNS 2015-01-09（POP 签名）　|　日期：2026-10-16
> 落点：`Src/XProtocol/XDdns/`　|　开关：`XDDNS_ON`

---

## 目录

- [1. 协议定位](#1-协议定位)
- [2. 总体架构](#2-总体架构)
- [3. 工作流程](#3-工作流程)
- [4. 阿里云 POP 签名](#4-阿里云-pop-签名)
- [5. 功能使用指南](#5-功能使用指南)
- [6. 完整示例](#6-完整示例)
- [7. 配置项](#8-配置项)
- [8. API 速查表](#9-api-速查表)
- [9. 公网 IP 探测](#5-功能使用指南)
- [10. 安全模型](#10-安全模型)
- [11. 测试](#11-测试)
- [12. 扩展新厂商指南](#12-扩展新厂商指南)
- [13. 常见问题](#13-常见问题)

---

## 1. 协议定位

XDdns 是**动态域名解析（DDNS）客户端**：设备公网 IP 变化时，自动更新
云厂商 DNS 解析记录，让固定域名始终指向设备的当前地址。

**为什么自研**：与 NTP 同理——最小化嵌入式固件里没有 ddclient/阿里云 SDK，
且应用需要可控策略（何时探测、探测源、更新阈值、多厂商切换）。

| 需求 | XDdns 的回应 |
|------|-------------|
| 必须支持阿里云 | `XDdnsAliyun.c`：POP 签名 + alidns API（Describe/Add/Update） |
| 将来接 DNSPod/Cloudflare | `XDdnsProvider` ops 抽象层，新厂商一个 .c |
| NAT 后设备拿公网 IP | 可配置的 HTTP 探测接口列表（内置 ipify/icanhazip）+ 手动注入 |
| 不刷爆云端 API | IP 未变化时短路（不发任何 API 请求） |
| 嵌入式友好 | XObject 定时器驱动周期检查；单飞锁防并发 |

**不适用场景**：设备直接持有公网 IP 且有 dnsmasq 类本地设施时，RFC 2136
（DNS 动态更新协议）更标准——当前未实现。

---

## 2. 总体架构

```
┌──────────────────────────────────────────────────┐
│ 应用层：setCredentials/setRecord/checkNow          │
└─────────────────┬────────────────────────────────┘
                  │ XDdnsClient_xxx
┌─────────────────▼────────────────────────────────┐
│ XDdnsClient（XObject 派生）                        │
│  · 公网 IP 探测（HTTP 列表逐个尝试 / 手动注入）      │
│  · 短路比对（IP 未变不发 API）                      │
│  · 编排：查记录 → 无则 Add / 不同则 Update           │
│  · 周期定时器 + 信号（4 个）                        │
└───────┬──────────────────────┬───────────────────┘
        │ XDdnsProvider ops    │ XNetworkAccessManager
┌───────▼──────────┐   ┌───────▼────────────────────┐
│ XDdnsAliyun      │   │ HTTP/HTTPS（XUpgrade 生产    │
│ POP 签名 + JSON  │   │ 同款同步等待口径）            │
└──────────────────┘   └────────────────────────────┘
```

### 模块文件

| 文件 | 职责 |
|------|------|
| `XDdns_config.h` | 开关（`XDDNS_ON`/`XDDNS_CLIENT_ON`/`XDDNS_ALIYUN_ON`）与常量 |
| `XDdnsProto.h` | 记录类型/状态/错误码/记录信息结构 |
| `XDdnsProvider.h/.c` | 厂商 ops 抽象与查找表 |
| `XDdnsAliyun.h/.c` | 阿里云实现（签名/URL 构造/JSON 解析） |
| `XDdnsClient.h/.c` | 客户端编排类 |
| `XDdnsClient_Protected.h` | 私有状态 |

---

## 3. 工作流程

`checkNow` 一轮的完整决策树：

```
checkNow
  │
  ├─ 缺配置（凭据/域名/RR）──────► 失败 NoConfig
  │
  ├─ 获取当前 IP
  │   ├─ 手动注入过 ──► 用注入值（跳过探测）
  │   └─ 自动探测 ────► 按槽位顺序请求探测 URL
  │                     首个返回合法 IP 者生效
  │                     全部失败 ──► 失败 ProbeExhausted
  │                     非法内容 ──► 失败 BadIp
  │
  ├─ IP == 上次推送值？──是──► 信号 recordUpToDate（不发 API 请求）
  │
  └─ 查询云端记录（provider.buildQueryUrl → GET → parseQuery）
      ├─ 应答损坏 ────► 失败 BadResponse
      ├─ 云端报错 ────► 失败 ApiError
      ├─ 记录不存在 ──► buildAddUrl → GET → parseMutation
      │                 成功 ──► 信号 recordUpdated
      ├─ 值不同 ──────► buildUpdateUrl(RecordId) → GET → parseMutation
      │                 成功 ──► 信号 recordUpdated
      └─ 值相同 ──────► 信号 recordUpToDate（本次白查了，下次被短路拦住）
```

**单飞锁**：上一轮未结束（Detecting/Updating 态）时 `checkNow` 返回 false。

---

## 4. 阿里云 POP 签名

XDdnsAliyun 按阿里云官方《签名机制》实现 RPC 风格 GET 签名：

### 4.1 五步流程

```
① 组装参数
   公共：AccessKeyId / Format=JSON / SignatureMethod=HMAC-SHA1 /
         SignatureNonce(随机16hex) / SignatureVersion=1.0 /
         Timestamp(UTC yyyy-MM-ddTHH:mm:ssZ) / Version=2015-01-09
   业务：Action + DomainName + RR + Type + Value(写) / RecordId(改)

② 规范化
   参数按名升序 → k=v 用 & 连接 → k、v 各做 POP 百分号编码
   （RFC3986 unreserved = A-Za-z0-9-._，但 ~ 和 * 也强制编码）

③ StringToSign
   "GET" + "&" + percentEncode("/") + "&" + percentEncode(规范化串)
   （percentEncode("/") = "%2F"；二次编码使 %3A 变 %253A）

④ 签名
   Signature = Base64( HMAC-SHA1( StringToSign, AccessSecret + "&" ) )

⑤ 最终 URL
   endpoint + "/?" + 规范化串 + "&Signature=" + percentEncode(Signature)
```

### 4.2 黄金向量（测试锚点）

测试用固定 nonce/Timestamp 断言整条 URL 逐字符一致，覆盖上述每一步：

```
AccessKeyId=testid, Secret=testsecret, RR=www, Type=A,
Nonce=1234567890abcdef, Timestamp=2026-01-15T08:00:00Z
→ 签名 68MhfS+9I1LpLG4Qd6/2e8lsU4M=（URL 中编码为
  68MhfS%2B9I1LpLG4Qd6%2F2e8lsU4M%3D）
```

### 4.3 使用的 API

| 步骤 | 库内设施 |
|------|---------|
| POP 编码 | `XUrl_toPercentEncoding(input, NULL, "~*")`（include 强制 `~*`） |
| HMAC-SHA1 | `XCryptographicHash_hmac(..., XCryptographicHash_Sha1)` |
| Base64 | `XBase64_encode` |
| Nonce | `XRandomGenerator_fillSecure`（8 字节 → hex） |
| UTC 时间戳 | `XDateTime_currentDateTimeUtc()` 手动拼接（toString 无零填充，勿用） |
| 应答解析 | `XJsonDocument_fromJson` + `XJsonObject_value_keyUtf8` |

---

## 5. 功能使用指南

### 5.1 生命周期

```c
#include "XDdnsClient.h"

XDdnsClient* c = XDdnsClient_create();
if (c) {
    /* ... */
    XClassDelete((XClass*)c);
}
```

### 5.2 最小配置与一轮检查

```c
XDdnsClient_setProvider(c, XDDNS_PROVIDER_ALIYUN);   /* 默认即 aliyun */
XDdnsClient_setCredentials_2(c, "AccessKeyId", "AccessKeySecret");
XDdnsClient_setRecord_2(c, "example.com", "www", XDdnsRecordType_A);
XDdnsClient_setTimeout(c, 10000);

XDdnsClient_checkNow(c);   /* 异步语义：内部自旋事件循环直至本轮结束 */
```

### 5.3 周期自动检查

```c
/* 每 10 分钟检查一次（默认）。IP 未变化时短路，仅消耗探测请求。 */
XDdnsClient_setCheckInterval(c, 10 * 60 * 1000);
/* 关闭：XDdnsClient_setCheckInterval(c, 0); */
```

### 5.4 公网 IP 探测源

```c
/* 默认列表（按记录类型自动选择）：
   A    → https://api4.ipify.org/ → https://ipv4.icanhazip.com/
   AAAA → https://api6.ipify.org/ → https://ipv6.icanhazip.com/

   自定义（覆盖对应槽位，最多 4 个；应答体须为纯 IP 文本）： */
XDdnsClient_setProbeUrl_2(c, "https://my-server.example.com/whatismyip", 0);

/* 手动注入（跳过探测；路由器/外部脚本告知 IP 的兜底）： */
XDdnsClient_setLocalIp_2(c, "203.0.113.7");
XDdnsClient_setLocalIp_2(c, NULL);   /* 清除，回到自动探测 */
```

### 5.5 HTTPS 证书策略

```c
/* 默认跳过校验（嵌入式无 CA 库的现实选择；API 本身有 POP 签名保护）。 */
XDdnsClient_setSslPeerVerify(c, true);   /* 开启严格校验（需 CA 链可信） */
XDdnsClient_setSslPeerVerify(c, false);  /* 回到跳过 */
```

### 5.6 信号订阅

```c
static void on_updated(XObject* receiver, XVarList* args) {
    XDdnsClient* c = (XDdnsClient*)receiver;
    const XString* ip = XDdnsClient_pushedIp_const(c);
    XPrintf("[DDNS] 已更新记录 → %s\n", XString_toUtf8(ip));
}
static void on_failed(XObject* receiver, XVarList* args) {
    XDdnsClient* c = (XDnsClient*)receiver;
    XPrintf("[DDNS] 检查失败 err=%d\n", (int)XDdnsClient_lastError(c));
}

XObject_connect_2((XObject*)c, XSignal(XDdnsClient_recordUpdated_signal),
                  on_updated);
XObject_connect_2((XObject*)c, XSignal(XDdnsClient_checkFailed_signal),
                  on_failed);
```

四个信号：`ipDetected(ip)` / `recordUpdated(ip)` / `recordUpToDate(ip)` /
`checkFailed(error)`。IP 参数为深拷贝 XString，接收方只读借用。

---

## 6. 完整示例

```c
#include "XDdnsClient.h"
#include "XDateTime.h"
#include "XCoreApplication.h"
#include "XPrintf.h"

static void on_updated(XObject* receiver, XVarList* args)
{
    XDdnsClient* c = (XDdnsClient*)receiver;
    XPrintf("[DDNS] 记录已指向 %s（delay 无关）\n",
            XString_toUtf8(XDdnsClient_pushedIp_const(c)));
}

int main(void)
{
    XDdnsClient* ddns = XDdnsClient_create();

    /* 阿里云：建议用 RAM 子账号（仅授权 AliyunDNSFullAccess） */
    XDdnsClient_setCredentials_2(ddns, "LTAI5t...", "yourSecret...");
    XDdnsClient_setRecord_2(ddns, "example.com", "home", XDdnsRecordType_A);
    XDdnsClient_setTimeout(ddns, 10000);
    XDdnsClient_setCheckInterval(ddns, 10 * 60 * 1000);

    XObject_connect_2((XObject*)ddns,
                      XSignal(XDdnsClient_recordUpdated_signal), on_updated);

    XDdnsClient_checkNow(ddns); /* 开机立即同步一次 */

    for (;;)
        XCoreApplication_processEvents(XEventLoop_AllEvents);
}
```

---

## 7. 配置项

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `XDDNS_ON` | `XPROTOCOL_ON` | 协议总开关 |
| `XDDNS_CLIENT_ON` | 1 | 客户端编排 |
| `XDDNS_ALIYUN_ON` | 1 | 阿里云实现 |
| `XDDNS_ALIYUN_ENDPOINT` | `https://alidns.aliyuncs.com` | 默认端点（可 setEndpoint 覆盖） |
| `XDDNS_ALIYUN_VERSION` | `2015-01-09` | POP API 版本 |
| `XDDNS_HTTP_TIMEOUT_MS` | 10000 | 单次 HTTP 超时 |
| `XDDNS_DEFAULT_CHECK_INTERVAL_MS` | 600000 | 自动检查周期（10 分钟） |
| `XDDNS_DEFAULT_TTL` | 600 | 新记录 TTL（秒） |
| `XDDNS_MAX_PROBE_URLS` | 4 | 探测列表槽位数 |
| `XDDNS_URL_MAX` | 2048 | 请求 URL 缓冲上限 |
| `XDDNS_MAX_PAYLOAD`（无） | — | DDNS 请求全为 GET，无载荷上限概念 |

---

## 8. API 速查表

### 8.1 生命周期

| API | 说明 |
|-----|------|
| `XDdnsClient_init(obj)` | 栈上初始化 |
| `XDdnsClient_create()` | 堆创建（`XClassDelete` 释放） |

### 8.2 配置

| API | 说明 |
|-----|------|
| `XDdnsClient_setProvider(s, name)` | 选择厂商（查找 `XDdnsProvider_find`） |
| `XDdnsClient_setEndpoint(s, url)` | 覆盖 API 端点（测试/专有云） |
| `XDdnsClient_setCredentials(s, id, secret)` | 凭据（XString 版） |
| `XDdnsClient_setCredentials_2(s, id, secret)` | 凭据（UTF-8 版） |
| `XDdnsClient_setRecord(s, domain, rr, type)` | 目标记录（XString 版） |
| `XDdnsClient_setRecord_2(s, domain, rr, type)` | 目标记录（UTF-8 版） |
| `XDdnsClient_setProbeUrl_2(s, url, index)` | 探测 URL（NULL 清除槽位） |
| `XDdnsClient_setLocalIp_2(s, ip)` | 手动注入 IP（NULL 回到探测） |
| `XDdnsClient_setCheckInterval(s, ms)` | 周期检查（0 关闭） |
| `XDdnsClient_setSslPeerVerify(s, on)` | HTTPS 证书校验开关 |
| `XDdnsClient_setTimeout(s, ms)` | HTTP 超时 |

### 8.3 检查与查询

| API | 说明 |
|-----|------|
| `XDdnsClient_checkNow(s)` | 立即执行一轮（单飞锁） |
| `XDdnsClient_state(s)` | Idle/Detecting/Updating/Synchronized/Failed |
| `XDdnsClient_lastError(s)` | 最近失败原因 |
| `XDdnsClient_currentIp_const(s)` | 最近探测/注入的 IP（借用） |
| `XDdnsClient_pushedIp_const(s)` | 最近确认与云端一致的 IP（借用） |

### 8.4 信号

| 信号 | 时机 |
|------|------|
| `XDdnsClient_ipDetected_signal` | 探测/注入成功（参数：IP） |
| `XDdnsClient_recordUpdated_signal` | 记录发生 Add 或 Update（参数：IP） |
| `XDdnsClient_recordUpToDate_signal` | 记录已一致（参数：IP） |
| `XDdnsClient_checkFailed_signal` | 本轮失败（参数：错误码） |

---

## 9. 公网 IP 探测

| 项 | 说明 |
|----|------|
| 内置 v4 源 | `https://api4.ipify.org/`、`https://ipv4.icanhazip.com/` |
| 内置 v6 源 | `https://api6.ipify.org/`、`https://ipv6.icanhazip.com/` |
| 选择规则 | 按记录类型选 v4/v6 列表；按槽位顺序尝试，首个成功生效 |
| 应答要求 | 体为纯 IP 文本（允许首尾空白）；按 `XHostAddress` 解析并校验协议族（A↔IPv4，AAAA↔IPv6） |
| 手动注入 | `setLocalIp_2` 优先于一切探测；非法 IP 拒绝注入 |
| 内网部署 | 探测接口不可达时用路由器/上位机注入，或换内网自建探测页 |

---

## 10. 安全模型

| 威胁 | 防护 |
|------|------|
| AccessKey Secret 泄露 | Secret 只参与本地 HMAC，永不过网 |
| 请求被篡改 | POP 签名覆盖全部参数（含 Value），改一个字符签名即失效 |
| 重放攻击 | SignatureNonce（随机）+ Timestamp（UTC），云端窗口校验 |
| 伪应答 | HTTPS（默认跳过证书校验时可开启 `setSslPeerVerify(true)`）|
| 凭据权限过大 | 建议用 RAM 子账号，仅授予目标域名的 DNS 写权限 |

**证书校验的取舍**：默认 `XSSL_VerifyNone` 是嵌入式无 CA 库的务实选择——
即便流量被中间人截获，伪造应答只能让设备看到假 IP（再由签名保护写路径）；
开启校验需预置 DigiCert 根证书（阿里云证书链）。

---

## 11. 测试

| 入口 | 命令 / 路径 |
|------|------------|
| 命令行 | `XinYueC_Static.exe --test ddns` |
| Shell 命令 | `test ddns` |
| 图形菜单 | Test 菜单「协议栈 → XDdns(ddns动态域名)」 |

**48 项断言，全离线**（本地 XHttpServer 同时扮演探测源与阿里云 API）：

| 分组 | 覆盖 |
|------|------|
| POP 签名确定性 | 黄金 URL 逐字符比对（排序/二次编码/时间戳/nonce/HMAC/Base64 链）；缺凭据拒绝 |
| Add/Update URL | RR=@ → %40、TTL、RecordId、签名追加 |
| 应答解析 | Describe 命中/多记录过滤（跳过 CNAME）/空记录/错误形态；Update 判定 |
| 端到端 | 四场景：Update→短路 UpToDate→再 Update→Add；信号计数与请求数逐一断言 |
| 探测容错 | 主探测不可达自动切换备用 URL |
| 内存泄漏 | 100 次创建/销毁（含定时器与凭据路径）回基线 |
| 原语探针 | SHA1 RFC6234 向量、HMAC-SHA1 RFC2202 向量、尺寸扫描（见下） |

### 关联缺陷：MultiPool 512B 档（已绕开，待修复）

测试中暴露一个 **XMemory 既有缺陷**：全局 MultiPool 的 512B 档对
「恰好 512 字节」的分配返回 NULL（257..400 与 513+ 正常）。加密哈希的
输入缓冲默认走 HYBRID（≤512 落 MultiPool），`addData` 扩容失败又是静默
跳过——表现为 **257..512 字节消息的哈希悄悄返回空输入摘要**，阿里云签名
（STS 约 250 字节，加上内层 ipad 恰破 256）正中此坑。

处置：`XCryptographic_config.h` 的
`XCRYPTOGRAPHIC_HASH_MEMORY_POOL_TYPE` 默认改为 `XMEMORY_TYPE_SYSTEM`
（哈希输入缓冲走系统堆，注释记录根因），并在 XDdnsTest 保留尺寸扫描探针
作回归锁。**MultiPool 512B 档本身仍待修复**（`XMultiPool.c`
`XMultiPool_initGlobal` 的档位容量/头部开销核算），修复后可把默认改回。

---

## 12. 扩展新厂商指南

以 DNSPod 为例（2026-10-16 起注册表带 `m_defaultEndpoint` 字段与
count/at 枚举，GUI「服务商」下拉框自动收录新厂商）：

1. 新建 `Src/XProtocol/XDdns/XDdnsDnspod.c`，实现五个 ops 函数
   （登录方式为 `login_token`/`format=json` POST——如需 POST，ops 签名需
   扩展 build* 返回请求体，属既定演进方向）；
2. 定义 `XDdnsProviderOps` 实例：`m_name`="dnspod"、
   `m_defaultEndpoint`="https://dnsapi.cn"、五个函数指针；
3. 暴露 getter `XDdnsProvider_dnspod(void)`，并在 `XDdnsProvider.c` 的
   `XDdnsProvider_find` 查找链、`XDdnsProvider_count`/`XDdnsProvider_at`
   枚举各登记一行——GUI「服务商」下拉框即自动收录；
4. `XDdns_config.h` 加 `XDDNS_DNSPOD_ON` 开关；
5. 复制 XDdnsTest 的 mock 分支补该厂商的应答形态用例。

客户端编排层（探测/短路/单飞/信号）零改动；GUI 页切换厂商时自动恢复
该厂商 `m_defaultEndpoint`（用户 setEndpoint 的覆盖会被清除，属预期）。

---

## 13. 常见问题

### Q1：checkNow 返回 false？

- 上一轮还在进行（单飞锁，等信号或轮询 state）
- 缺凭据/域名/RR（`lastError == NoConfig`）

### Q2：一直 ProbeExhausted？

设备出网受限。依次试：`setLocalIp_2` 手动注入、换 `setProbeUrl_2`
指向内网自建探测页、检查 DNS 与 443 出网策略。

### Q3：收到 Kiss-o'-Death / ApiError？

DDNS 无 KoD 概念（那是 NTP 的），XDdns 的 `ApiError` 是云端 JSON 带
`Code`。常见码：`InvalidAccessKeyId.NotFound`（Key 错）、
`SignatureDoesNotMatch`（Secret 错或系统时间偏差过大——先 NTP 校时）、
`Forbidden.RAM`（RAM 子账号未授权 DNS 权限）。

### Q4：记录一直不更新但探测正常？

看是否命中短路：`pushedIp` 与当前 IP 相同时不发 API（设计行为）。
强制刷新可 `setLocalIp_2` 换值或重建客户端。另外确认 RR/Type 拼写——
查询按 RR+Type 精确匹配，Type 不匹配（如云端是 CNAME）会走 Add 而
被云端拒绝。

### Q5：SignatureDoesNotMatch 但代码看着没错？

按序检查：系统时间偏差（Timestamp 参与签名，先 NTP 校时）、
Secret 是否带了首尾空格、是否用了 RAM 子账号的 Secret（不是主账号的）。
测试侧可用 `--test ddns` 的黄金向量确认本地实现未漂移。

### Q6：TTL/线路可以配吗？

TTL 当前固定 600（新记录时提交）；解析线路默认「默认」。
需要可配置时在 `XDdnsAliyun_buildAddUrl` 加参数并透传 API 即可
（ops 接口已预留 Value 之外的扩展空间）。

---

## 附：源码索引

| 内容 | 位置 |
|------|------|
| 协议常量与开关 | `XDdns_config.h` |
| 公共类型 | `XDdnsProto.h` |
| 厂商抽象与查找 | `XDdnsProvider.h/.c` |
| 阿里云 POP 实现 | `XDdnsAliyun.h/.c` |
| 客户端编排 | `XDdnsClient.h/.c` + `XDdnsClient_Protected.h` |
| 功能测试 | `Test/XProtocolTest/XDdnsTest.c` |
| 依赖：HTTP 客户端 | `Src/XProtocol/XHttp/XNetworkAccessManager.*` |
| 依赖：写系统时钟无关 | 时间戳用 `XDateTime_currentDateTimeUtc()`（只读） |
