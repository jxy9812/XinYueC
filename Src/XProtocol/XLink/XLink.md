# XLink 私有应用层协议

> 版本：v1.0　|　协议版本：`0x0100`　|　日期：2026-10-15
> 落点：`Src/XProtocol/XLink/`　|　开关：`XLINK_ON`

---

## 目录

- [1. 协议定位](#1-协议定位)
- [2. 总体架构](#2-总体架构)
- [3. 线上格式](#3-线上格式)
- [4. 消息类详解](#4-消息类详解)
- [5. 会话状态机](#5-会话状态机)
- [6. 功能使用指南](#6-功能使用指南)
  - [6.1 生命周期](#61-生命周期)
  - [6.2 建立链路与握手](#62-建立链路与握手)
  - [6.3 请求-应答](#63-请求应答)
  - [6.4 推送与订阅](#64-推送与订阅)
  - [6.5 待应答与超时](#65-待应答与超时)
  - [6.6 心跳与判线](#66-心跳与判线)
  - [6.7 TLS 加密链路](#67-tls-加密链路)
- [7. 完整示例](#7-完整示例)
- [8. 配置项](#8-配置项)
- [9. API 速查表](#9-api-速查表)
- [10. 性能数据](#10-性能数据)
- [11. 测试](#11-测试)
- [12. 设计与实现要点](#12-设计与实现要点)
- [13. 常见问题](#13-常见问题)

---

## 1. 协议定位

XLink 是 XinYueC 自有的**应用层私有协议**，用于自研程序之间的消息互通。与同目录下的 XMqtt / XModbus 等标准协议实现不同，XLink 不兼容任何公开协议规范，它的诞生直接服务于一个具体场景：

> **控制程序与 GUI 程序分离部署时的进程间通信。**

典型部署形态：

```
┌─────────────────────────┐          ┌─────────────────────────┐
│  控制进程（服务器）        │          │  GUI 进程（客户端）        │
│  ─────────────────      │          │  ─────────────────      │
│  · 串口 / RS-485 / CAN    │   TCP    │  · 界面渲染（本地 fbdev）   │
│  · 业务逻辑、数据采集       │ ◄──────► │  · 触摸 / 键盘处理         │
│  · 状态快照周期推送        │  XLink   │  · 命令下发、状态展示        │
└─────────────────────────┘          └─────────────────────────┘
```

**为什么需要它**

| 需求 | XLink 的回应 |
|------|-------------|
| 控制与界面分离，故障互不影响 | 两进程独立，GUI 崩溃不影响控制运行 |
| 界面崩溃后快速恢复画面 | 重连时握手阶段重推全量快照，无需增量补偿逻辑 |
| 双向：GUI 下发命令 + 服务器推状态 | 三个正交消息类，任意一端既能发起请求也能推送 |
| 通用：新增业务不改框架 | 加一个 topic 或命令号即可，协议层零改动 |
| 嵌入式友好 | 收发全链路稳态零堆分配；不解密压缩，帧头仅 12 字节 |

**不适用场景**：需要与第三方系统互操作时请用 MQTT / Modbus 等标准协议。

---

## 2. 总体架构

### 2.1 分层

```
┌─────────────────────────────────────────────────────────┐
│  应用层：控制进程 / GUI 进程                              │
│    · 注册分发回调（XLinkSession_setDispatchCallback）      │
│    · 发命令 / 收状态 / 订阅推送                            │
└───────────────────────┬─────────────────────────────────┘
                        │ XLinkSession_xxx（会话 API）
┌───────────────────────▼─────────────────────────────────┐
│  会话层 XLinkSession                                      │
│    · 帧边界管理（抽干/半帧/粘包）                          │
│    · 握手状态机、能力协商                                 │
│    · 待应答表、订阅表、心跳判线                             │
│    · 消息分发（框架内务消化 + 业务回调）                    │
└───────────────────────┬─────────────────────────────────┘
                        │ XIODevice*（传输抽象）
┌───────────────────────▼─────────────────────────────────┐
│  传输层：XTcpSocket / XTcpServer / XSslSocket / 回环设备    │
│    · XLink 不感知具体传输，换传输不动协议                    │
└─────────────────────────────────────────────────────────┘
```

### 2.2 两端对称

XLink 的核心设计是**没有服务器/客户端之分**。两端使用同一套编解码、同一张分发表、同一份待应答表，唯一差别是建链时谁 `accept`、谁 `connect`：

```c
/* 控制进程：被动接受连接 */
XTcpServer* server = XTcpServer_create(...);
XIODevice* conn = XTcpServer_nextPendingConnection(server);
XLinkSession* session = XLinkSession_create_ex(conn);

/* GUI 进程：主动连接 */
XIODevice* conn = XTcpSocket_create();
XTcpSocket_connect(...);
XLinkSession* session = XLinkSession_create_ex(conn);
```

传输建立之后，协议层不再区分身份。**这意味着同一个类既能跑控制进程也能跑 GUI 进程**，两端可同时向对方发起请求。

### 2.3 模块文件

| 文件 | 职责 |
|------|------|
| `XLink_config.h` | 协议开关与容量常量 |
| `XLinkProto.h` | 线上格式定义：消息类枚举、能力位、帧头结构 |
| `XLinkCodec.h/.c` | 小端序列化原语、Hello 载荷编解码、帧头编解码 |
| `XLinkSession.h/.c` | 会话对象：收发、分发、订阅表、待应答表、心跳 |

---

## 3. 线上格式

### 3.1 帧结构

定长帧头 **12 字节**，全部小端线序（fbdev/网络栈惯例，ARM 与 x86 一致）：

| 偏移 | 长度 | 字段 | 说明 |
|------|------|------|------|
| 0 | 4 | `payloadLen` | 载荷字节数（不含帧头），须 ≤ `XLINK_MAX_PAYLOAD` |
| 4 | 2 | `msgClass` | 消息类，见 [4. 消息类详解](#4-消息类详解) |
| 6 | 2 | `msgCode` | 业务码：Request/Response = 命令号；Publish/Subscribe = topicId 低 16 位；Session = 子类型 |
| 8 | 4 | `requestId` | Request/Response 关联 id；其余消息类填 0 |
| 12 | N | `payload` | 载荷 |

### 3.2 topicId 的承载规则（重要）

`msgCode` 线上只有 **u16**，而业务 topicId 区间从 `0x10000` 起（`XLINK_TOPIC_USER_BASE`），**已越出 u16 表示范围**。因此：

> **Publish / Subscribe / Unsubscribe 三类的完整 u32 topicId 一律另置于载荷首 4 字节（小端），`msgCode` 只承载其低 16 位供快速匹配与统计。解析侧一律以载荷中的 u32 为准。**

线上布局：

```
[12B 帧头][4B topicId (u32 小端)][业务载荷...]
         ↑ msgCode 仅为 topicId 低 16 位
```

调用方无需关心此细节 —— `XLinkSession_sendPublish` / `subscribe` / `unsubscribe` 已自动处理。

### 3.3 载荷序列化规范

**严禁 `memcpy` 结构体上线。** 编译器填充与端序差异会静默破坏线上格式。必须用 `XLinkCodec_putU16/putU32` 逐字段序列化：

```c
/* ✅ 正确 */
uint8_t buf[8];
XLinkCodec_putU32(&buf[0], deviceId);
XLinkCodec_putU32(&buf[4], temperature);

/* ❌ 错误：结构体可能含填充字节，且未做字节序转换 */
typedef struct { uint32_t a; uint32_t b; } DeviceState;
memcpy(buf, &state, sizeof(state));
```

### 3.4 帧原子性

**帧头与载荷必须一次写出。** 部分传输（如 `XGuiRemoteLoopbackDevice`）的写入是「全有全无」语义，分两段写会只落帧头、对端拿到截断帧流。XLink 内部通过常驻发送缓冲保证单次原子写，调用方无需关心。

---

## 4. 消息类详解

### 4.1 三个正交消息类

| 消息类 | 值 | 语义 | 是否等应答 | 谁发都行 |
|--------|-----|------|-----------|---------|
| `XLinkMsgClass_Session` | 0 | 会话管理：Hello/HelloAck/Heartbeat/Goodbye | 否 | ✅ |
| `XLinkMsgClass_Request` | 1 | 请求-响应的请求方 | 是 | ✅ |
| `XLinkMsgClass_Response` | 2 | 请求-响应的应答方 | — | ✅ |
| `XLinkMsgClass_Publish` | 3 | 单向推送 | 否 | ✅ |
| `XLinkMsgClass_Subscribe` | 4 | 订阅 | — | ✅ |
| `XLinkMsgClass_Unsubscribe` | 5 | 退订 | — | ✅ |

**为什么这样设计**：消息类正交而非逐条枚举业务语义。Request/Response 管"我问你要答"，Publish 管"我单方面告诉你"，Subscribe 管"以后这类推给我"。三类正交组合，覆盖全部交互形态。新增业务只表现为新增 msgCode/topicId，**协议层与编解码层零改动**。

### 4.2 会话子类型

`msgClass = XLinkMsgClass_Session` 时，`msgCode` 取以下值：

| 子类型 | 值 | 说明 |
|--------|-----|------|
| `XLinkSessionCode_Hello` | 1 | 建链后首帧，携版本与能力位 |
| `XLinkSessionCode_HelloAck` | 2 | 对 Hello 的应答；约定可携带全量快照 |
| `XLinkSessionCode_Heartbeat` | 3 | 心跳，静默超时即判线 |
| `XLinkSessionCode_Goodbye` | 4 | 主动断链通知 |

### 4.3 能力位

Hello / HelloAck 载荷携带，握手后取**按位与**作为协商结果：

| 能力位 | 值 | 含义 |
|--------|-----|------|
| `XLinkCapability_Zlib` | 0x0001 | 支持 zlib 载荷压缩 |
| `XLinkCapability_Tls` | 0x0002 | 链路已运行在 TLS 之上 |
| `XLinkCapability_Snapshot` | 0x0004 | HelloAck 载荷内携带全量快照 |
| `XLinkCapability_RequestTimeout` | 0x0008 | 支持请求超时通知 |

未置位的特性对端不得依赖（如未协商 `ZLIB` 则载荷必须为原始字节）。

---

## 5. 会话状态机

```
                   XLinkSession_setDevice()
        ┌──────────────────────────────────┐
        │                                  ▼
  ┌───────────┐                     ┌─────────────┐
  │ Inactive  │◄────────────────────│  Connected  │
  │ 初始态     │  reset / 传输断开     │ 已挂载传输    │
  │           │  静默超时判线        │ 握手未完成    │
  └───────────┘                     └─────────────┘
                                          │
                                  收到 Hello / HelloAck
                                          ▼
                                   ┌─────────────┐
                                   │    Ready    │
                                   │ 链路健康     │
                                   │ 全部分类投递 │
                                   └─────────────┘
```

**关键行为**

- 握手完成前，**不投递任何业务消息** —— 防止把未建立状态下的乱序帧喂给应用
- 静默超过 `XLINK_HEARTBEAT_TIMEOUT_MS`（默认 3 秒）自动转 `Inactive`
- 收到 `Goodbye` 立即转 `Inactive`（对端主动断链感知）
- `setDevice(NULL)` 立即断开，并清空帧缓冲、待应答表、订阅表

---

## 6. 功能使用指南

### 6.1 生命周期

```c
#include "XLinkSession.h"
#include "XMemory.h"

/* 堆对象 */
XLinkSession* session = XLinkSession_create();
if (session) {
    /* ... 使用 ... */
    XClassDelete((XClass*)session);
}

/* 栈对象 */
XLinkSession session;
XLinkSession_init(&session);
/* ... 使用 ... */
XClassDeinit(&session);

/* 创建并挂载传输 */
XLinkSession* s = XLinkSession_create_ex(device);
if (!s) return;
/* ... 使用 ... */
XClassDelete((XClass*)s);
```

> ⚠️ `init` 与 `XClassDeinit` 必须成对出现。

### 6.2 建立链路与握手

**第 1 步：挂载传输**

```c
/* 任一端收到连接后 */
XIODevice* conn = XTcpServer_nextPendingConnection(server);
XLinkSession* session = XLinkSession_create_ex(conn);
```

**第 2 步：声明能力与注册回调**

```c
XLinkSession_setCapability(session, XLinkCapability_Zlib | XLinkCapability_Tls);
XLinkSession_setDispatchCallback(session, my_dispatch_callback, userData);
```

**第 3 步：发送 Hello（双向对称，谁先发都行）**

```c
XLinkHelloPayload hello;
uint8_t buf[4];

hello.m_version = XLINK_VERSION;
hello.m_capability = XLinkCapability_Zlib | XLinkCapability_Tls;
XLinkCodec_encodeHello(&hello, buf, sizeof(buf), NULL);
XLinkSession_sendSession(session, XLinkSessionCode_Hello, buf, 4);
```

**第 4 步：驱动事件循环**

```c
while (running) {
    XLinkSession_pump(session, XDateTime_currentMSecsSinceEpoch());
    /* ... 其他事件处理 ... */
}
```

**对称性**：收到对端 Hello 时自动回 HelloAck，双方都会转 `Ready`。**不需要区分谁是客户端**。

**恢复现场**：重连后建议在 HelloAck 里携带全量快照（配合 `XLinkCapability_Snapshot`），GUI 侧无需任何补偿逻辑即可恢复画面。

### 6.3 请求-应答

适用于「GUI 下发操作」「GUI 查询设备数据」这类一问一答。

**发起方**

```c
uint32_t reqId = 0;
uint8_t arg = 0x5A;

if (XLinkSession_sendRequest(session, CMD_START_MOTOR, &arg, 1, &reqId)) {
    /* reqId 已登记在待应答表 */
}
```

**应答方**（在分发回调里）

```c
static void on_request(XLinkSession* s, const XLinkFrameHeader* f,
                       const uint8_t* payload)
{
    if (f->m_msgClass != XLinkMsgClass_Request) return;

    /* 真实设备 IO 耗时，允许异步应答：先回执，完成后再补结果 */
    do_device_io();
    XLinkSession_sendResponse(s, f->m_requestId, f->m_msgCode, result, len);
}
```

**请求方收到应答**

```c
if (f->m_msgClass == XLinkMsgClass_Response) {
    /* 应答到达时，待应答表已自动出表 */
    /* f->m_requestId 与发起时的 reqId 对应 */
    update_ui(f->m_payloadLen ? payload : NULL, f->m_payloadLen);
}
```

**要点**

- 两端各自独立分配 `requestId`，可同时互发请求互不干扰
- 请求**不自动应答** —— 应答可能耗时（真实设备 IO），由应用层显式回执，允许异步
- 应答必须原样回带对端的 `m_requestId` 与 `m_msgCode`

### 6.4 推送与订阅

适用于「状态实时查询」「报警事件通知」这类单向广播。

**订阅方**

```c
/* 定义业务 topic（从用户基址起） */
#define TOPIC_DEVICE_STATE  (XLINK_TOPIC_USER_BASE + 1)
#define TOPIC_ALARM_EVENT   (XLINK_TOPIC_USER_BASE + 2)

XLinkSession_subscribe(session, TOPIC_DEVICE_STATE);
XLinkSession_subscribe(session, TOPIC_ALARM_EVENT);
```

**发布方**

```c
/* 状态快照：周期推送 */
void publish_state(XLinkSession* s)
{
    uint8_t buf[256];
    int n = 0;
    XLinkCodec_putU32(&buf[n], running);        n += 4;
    XLinkCodec_putU32(&buf[n], temperature);     n += 4;
    XLinkCodec_putU32(&buf[n], pressure);        n += 4;
    XLinkSession_sendPublish(s, TOPIC_DEVICE_STATE, buf, n);
}

/* 事件通知：产生即推 */
void on_alarm(XLinkSession* s, int code)
{
    uint8_t buf[4];
    XLinkCodec_putU32(&buf[0], (uint32_t)code);
    XLinkSession_sendPublish(s, TOPIC_ALARM_EVENT, buf, 4);
}
```

**订阅方接收**

```c
if (f->m_msgClass == XLinkMsgClass_Publish) {
    uint32_t topic = XLinkCodec_getU32(payload);      /* 载荷首 4 字节 */
    const uint8_t* data = payload + 4;                /* 业务载荷 */
    uint32_t len = f->m_payloadLen - 4;

    if (topic == TOPIC_DEVICE_STATE) update_state(data, len);
    else if (topic == TOPIC_ALARM_EVENT) show_alarm(data, len);
}
```

**退订**

```c
XLinkSession_unsubscribe(session, TOPIC_ALARM_EVENT);
```

> ⚠️ 业务载荷上限为 `XLINK_MAX_PAYLOAD - 4`（4 字节 topicId 前缀）。超出直接返回 `false`，不截断、不部分发送。

### 6.5 待应答与超时

请求发出后进入待应答表，应答到达自动出表。超时需应用层主动查询：

```c
/* 每次 pump 后检查 */
uint16_t cmd;
if (XLinkSession_takeTimedOut(session, nowMs, 2000, &cmd)) {
    show_error("设备无响应 (命令 %u)", cmd);   /* 取出的槽位不会重复上报 */
}
```

**设计要点**：超时判定不由库自动触发，而是交给应用层 —— 因为「超时」在不同业务里含义不同（可重试？需回滚？弹窗提示？）。

**容量限制**：待应答表定容 `XLINK_PENDING_SLOTS`（默认 8 槽）。表满时 `sendRequest` 返回 `false`，**拒绝发请求而非静默丢失**。高频请求场景请调大该宏。

### 6.6 心跳与判线

**自动心跳**：握手完成后，`pump` 会按 `XLINK_HEARTBEAT_INTERVAL_MS`（默认 1 秒）自动发送 Heartbeat。

**自动判线**：超过 `XLINK_HEARTBEAT_TIMEOUT_MS`（默认 3 秒）未收到任何字节，会话自动转 `Inactive`。

**主动断链**

```c
XLinkSession_sendSession(session, XLinkSessionCode_Goodbye, NULL, 0);
XLinkSession_reset(session);
```

> ⚠️ **时间轴必须连续**。`pump` 的 `nowMs` 若发生回退或跨越静默阈值，会误判为断线。建议统一用 `XDateTime_currentMSecsSinceEpoch()`（单调时钟）传入。

### 6.7 TLS 加密链路

加密由**传输层**（`XSslSocket` + mbedTLS）承担，XLink 协议层不实现任何密码学。

```c
/* 明文 */
XLinkSession* s = XLinkSession_create_ex(tcpSocket);

/* 加密 */
XSslSocket* ssl = XSslSocket_create();
XSslSocket_connect_base(ssl, host, port);
XLinkSession* s = XLinkSession_create_ex((XIODevice*)ssl);

/* 握手时声明链路已加密 */
XLinkSession_setCapability(s, XLinkCapability_Zlib | XLinkCapability_Tls);
```

**要点**

- 开关 `XLINK_SSL_ON` 只决定 XLink 是否**感知/声明** TLS 能力位，关闭后仍可挂任意 `XIODevice`
- 厂内网明文即可；跨网段或公网再切 TLS
- 协商结果可查：`XLinkSession_negotiatedCapability(s) & XLinkCapability_Tls`

---

## 7. 完整示例

### 7.1 控制进程（服务端）

```c
#include "XLinkSession.h"
#include "XTcpServer.h"
#include "XDateTime.h"
#include "XPrintf.h"

/* 业务命令号 */
#define CMD_START_MOTOR   1
#define CMD_STOP_MOTOR    2
#define CMD_QUERY_STATE   3

/* 设备状态 */
static int g_motorRunning = 0;
static uint32_t g_temperature = 25;

static void on_frame(XLinkSession* s, const XLinkFrameHeader* f,
                     const uint8_t* payload)
{
    switch (f->m_msgClass) {
    case XLinkMsgClass_Request:
        switch (f->m_msgCode) {
        case CMD_START_MOTOR:
            g_motorRunning = 1;
            XLinkSession_sendResponse(s, f->m_requestId, f->m_msgCode, NULL, 0);
            break;
        case CMD_STOP_MOTOR:
            g_motorRunning = 0;
            XLinkSession_sendResponse(s, f->m_requestId, f->m_msgCode, NULL, 0);
            break;
        case CMD_QUERY_STATE: {
            uint8_t buf[8];
            XLinkCodec_putU32(&buf[0], (uint32_t)g_motorRunning);
            XLinkCodec_putU32(&buf[4], g_temperature);
            XLinkSession_sendResponse(s, f->m_requestId, f->m_msgCode, buf, 8);
            break;
        }
        default:
            /* 未知命令：回空应答表示拒绝 */
            XLinkSession_sendResponse(s, f->m_requestId, f->m_msgCode, NULL, 0);
            break;
        }
        break;

    case XLinkMsgClass_Subscribe:
        /* 收到订阅请求：记住即可，状态由定时器周期推送 */
        XPrintf("[控制] 收到订阅请求\n");
        break;

    default:
        break;
    }
}

int main(void)
{
    XTcpServer* server = XTcpServer_create();
    XTcpServer_listen_base(server, "0.0.0.0", 45200);

    XLinkSession* session = NULL;
    uint32_t lastPush = 0;

    for (;;) {
        uint32_t now = (uint32_t)XDateTime_currentMSecsSinceEpoch();

        /* 接受新连接 */
        if (!session) {
            XIODevice* conn = XTcpServer_nextPendingConnection(server);
            if (conn) {
                session = XLinkSession_create_ex(conn);
                XLinkSession_setCapability(session, XLinkCapability_Zlib);
                XLinkSession_setDispatchCallback(session, on_frame, NULL);

                /* 主动发 Hello（对称：谁先发都行） */
                XLinkHelloPayload hello = { XLINK_VERSION, XLinkCapability_Zlib };
                uint8_t buf[4];
                XLinkCodec_encodeHello(&hello, buf, sizeof(buf), NULL);
                XLinkSession_sendSession(session, XLinkSessionCode_Hello, buf, 4);
                XPrintf("[控制] GUI 已连接\n");
            }
        }

        if (session) {
            XLinkSession_pump(session, now);

            /* 状态快照周期推送（10Hz） */
            if (XLinkSession_isReady(session) && (now - lastPush) >= 100) {
                uint8_t buf[8];
                XLinkCodec_putU32(&buf[0], (uint32_t)g_motorRunning);
                XLinkCodec_putU32(&buf[4], g_temperature);
                XLinkSession_sendPublish(session,
                                         XLINK_TOPIC_USER_BASE + 1, buf, 8);
                lastPush = now;
            }

            /* 请求超时检查 */
            uint16_t cmd;
            if (XLinkSession_takeTimedOut(session, now, 2000, &cmd))
                XPrintf("[控制] 命令 %u 超时未应答\n", cmd);

            /* 断线重置 */
            if (!XLinkSession_isReady(session)) {
                XClassDelete((XClass*)session);
                session = NULL;
                XPrintf("[控制] GUI 已断开\n");
            }
        }
    }
}
```

### 7.2 GUI 进程（客户端）

```c
#include "XLinkSession.h"
#include "XTcpSocket.h"
#include "XDateTime.h"

/* 与控制进程约定的 topic */
#define TOPIC_DEVICE_STATE  (XLINK_TOPIC_USER_BASE + 1)

/* 命令号 */
#define CMD_START_MOTOR   1
#define CMD_STOP_MOTOR    2

static int g_connected = 0;
static int g_motorRunning = 0;
static uint32_t g_temperature = 0;

static void on_frame(XLinkSession* s, const XLinkFrameHeader* f,
                     const uint8_t* payload)
{
    if (f->m_msgClass == XLinkMsgClass_Publish) {
        uint32_t topic = XLinkCodec_getU32(payload);
        const uint8_t* data = payload + 4;
        if (topic == TOPIC_DEVICE_STATE && f->m_payloadLen >= 12) {
            g_motorRunning = (int)XLinkCodec_getU32(&data[0]);
            g_temperature  = XLinkCodec_getU32(&data[4]);
            update_ui(g_motorRunning, g_temperature);   /* 刷新界面 */
        }
        return;
    }

    if (f->m_msgClass == XLinkMsgClass_Response) {
        if (f->m_msgCode == CMD_START_MOTOR || f->m_msgCode == CMD_STOP_MOTOR) {
            show_ok("操作成功");
        }
        return;
    }

    if (f->m_msgClass == XLinkMsgClass_Subscribe) {
        XPrintf("[GUI] 已订阅 topic %u\n", XLinkCodec_getU32(payload));
    }
}

static void send_command(XLinkSession* s, uint16_t cmd)
{
    uint32_t reqId = 0;
    if (!XLinkSession_sendRequest(s, cmd, NULL, 0, &reqId)) {
        show_error("命令下发失败（可能待应答表已满）");
    }
}

void ui_loop(XLinkSession* s)
{
    for (;;) {
        uint32_t now = (uint32_t)XDateTime_currentMSecsSinceEpoch();
        XLinkSession_pump(s, now);

        if (!XLinkSession_isReady(s)) {
            show_disconnected_mask();
            continue;
        }

        /* 用户点击按钮 */
        if (button_start_clicked())
            send_command(s, CMD_START_MOTOR);
        if (button_stop_clicked())
            send_command(s, CMD_STOP_MOTOR);

        /* 请求超时提示 */
        uint16_t cmd;
        if (XLinkSession_takeTimedOut(s, now, 2000, &cmd))
            show_error("设备无响应 (命令 %u)", cmd);
    }
}
```

---

## 8. 配置项

在 `XLink_config.h` 中定义，或通过 CMake 编译选项覆盖。

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `XLINK_ON` | `XPROTOCOL_ON` | 协议总开关（统一在 `XProtocol_config.h` 登记） |
| `XLINK_CODEC_ON` | 1 | 帧编解码层 |
| `XLINK_SESSION_ON` | 1 | 会话层 |
| `XLINK_SSL_ON` | 1 | TLS 能力感知（实际加密由 `XSslSocket` 承担） |
| `XLINK_VERSION` | `0x0100` | 协议版本（高字节主版本，低字节次版本） |
| `XLINK_HEADER_SIZE` | 12 | 帧头固定长度 |
| `XLINK_TOPIC_USER_BASE` | `0x10000` | 业务 topicId 起始值 |
| `XLINK_MAX_PAYLOAD` | `16384` | **线上**载荷上限（含 topicId 前缀） |
| `XLINK_PENDING_SLOTS` | 8 | 待应答表槽位 |
| `XLINK_TOPIC_SLOTS` | 16 | 订阅表槽位 |
| `XLINK_HEARTBEAT_INTERVAL_MS` | 1000 | 心跳间隔（0 = 不发心跳） |
| `XLINK_HEARTBEAT_TIMEOUT_MS` | 3000 | 静默判线阈值 |

**裁剪示例**

```c
/* 极简嵌入式配置：关闭心跳、缩小缓冲 */
#define XLINK_MAX_PAYLOAD     (4 * 1024)
#define XLINK_PENDING_SLOTS   4
#define XLINK_TOPIC_SLOTS     8
#define XLINK_HEARTBEAT_INTERVAL_MS  500
```

---

## 9. API 速查表

### 9.1 生命周期

| API | 说明 |
|-----|------|
| `XLinkSession_init(obj)` | 栈上初始化 |
| `XLinkSession_create()` | 堆创建（`XClassDelete` 释放） |
| `XLinkSession_create_ex(device)` | 堆创建并挂载传输 |
| `XLinkSession_class_init()` | 虚函数表初始化（框架内部） |

### 9.2 传输与状态

| API | 说明 |
|-----|------|
| `XLinkSession_setDevice(s, dev)` | 挂载/更换传输（传 NULL 断开） |
| `XLinkSession_reset(s)` | 复位到 Inactive |
| `XLinkSession_pump(s, nowMs)` | 非阻塞泵，驱动收发与心跳 |
| `XLinkSession_state(s)` | 查询链路状态 |
| `XLinkSession_isReady(s)` | 是否已握手且健康 |
| `XLinkSession_remoteVersion(s)` | 对端协议版本 |
| `XLinkSession_negotiatedCapability(s)` | 能力交集 |

### 9.3 收发

| API | 说明 |
|-----|------|
| `XLinkSession_send(s, frame, payload)` | 发送任意帧（底层通用接口） |
| `XLinkSession_sendSession(s, code, payload, len)` | 发送会话帧（Hello/Heartbeat/Goodbye） |
| `XLinkSession_sendRequest(s, cmd, payload, len, &id)` | 发送请求，返回关联 id |
| `XLinkSession_sendResponse(s, reqId, cmd, payload, len)` | 发送应答 |
| `XLinkSession_sendPublish(s, topicId, payload, len)` | 发送推送 |

### 9.4 订阅

| API | 说明 |
|-----|------|
| `XLinkSession_subscribe(s, topicId)` | 订阅并登记本地表 |
| `XLinkSession_unsubscribe(s, topicId)` | 退订 |
| `XLinkSession_isSubscribed(s, topicId)` | 查询本地订阅状态 |

### 9.5 待应答

| API | 说明 |
|-----|------|
| `XLinkSession_isPending(s, reqId)` | 请求是否仍在等待应答 |
| `XLinkSession_takeTimedOut(s, nowMs, timeoutMs, &cmd)` | 取出超时请求（不重复上报） |

### 9.6 分发

| API | 说明 |
|-----|------|
| `XLinkSession_setDispatchCallback(s, cb, userData)` | 注册业务消息回调 |
| `XLinkSession_dispatch_base(s, frame, payload)` | 虚表分发入口（子类扩展用） |

### 9.7 编解码

| API | 说明 |
|-----|------|
| `XLinkCodec_putU16(buf, v)` / `getU16(buf)` | 小端 16 位序列化 |
| `XLinkCodec_putU32(buf, v)` / `getU32(buf)` | 小端 32 位序列化 |
| `XLinkCodec_encodeHello(payload, buf, cap, &len)` | Hello 载荷编码 |
| `XLinkCodec_decodeHello(buf, len, &out)` | Hello 载荷解码 |
| `XLinkCodec_encodeHeader(frame, buf)` | 帧头打包（12 字节） |
| `XLinkCodec_decodeHeader(buf, len, &out)` | 帧头解包（含载荷上限校验） |

---

## 10. 性能数据

### 10.1 实测结果

测试环境：x86-64 桌面，`XGuiRemoteLoopbackDevice` 内存环回（无真实网络）。

**纯编解码天花板**（仅 12 字节帧头打包，不含 I/O）：

| 指标 | 数值 |
|------|------|
| 每帧耗时 | **18.3 ns** |
| 折算吞吐 | **4999 Mbps** |

> 协议层本身几乎不是瓶颈，全链路开销主要来自内存拷贝与设备缓冲层。

**全链路吞吐**（`sendPublish` + `pump` 端到端）：

| 业务载荷 | 每帧耗时 | 线上吞吐 | 固定开销占比 |
|---------|---------|---------|-------------|
| 16 B | 0.37 us | 667 Mbps | 50.0% |
| 64 B | 0.42 us | 1437 Mbps | 20.0% |
| 256 B | 0.62 us | 3346 Mbps | 5.9% |
| 1024 B | 1.38 us | 5749 Mbps | 1.5% |
| 4096 B | 4.27 us | 7339 Mbps | 0.4% |
| 16380 B | 16.59 us | **7540 Mbps** | 0.1% |

**内存占用**：

| 项目 | 数值 |
|------|------|
| 每会话常驻内存（收发缓冲） | 约 32 KB |
| 稳态堆分配 | **0 字节**（收发全程零分配） |
| 泄漏检测（200 次创建/销毁） | **0 字节泄漏** |

### 10.2 嵌入式平台预估

桌面数据需按平台折算：

| 平台 | 预估协议吞吐 | 说明 |
|------|-------------|------|
| RK3506（A7 @1.5GHz） | 200~600 Mbps | 单核约为桌面 x86 的 1/8~1/10 |
| RK3568（A55 @1.8GHz） | 1~2 Gbps | 约为桌面 x86 的 1/4~1/5 |
| 百兆网口设备 | **≤ 100 Mbps** | 网卡为硬上限 |

> 实际以真机 `--test xlink-quality` 实测为准。

### 10.3 优化建议

1. **小包尽量攒批**。16 字节载荷时固定开销占 50%（12B 帧头 + 4B topicId），一半带宽在传元数据。状态快照建议按 100~200ms 周期打包成整帧，而非每点一帧。
2. **画面块走大帧**。4096B 帧开销仅 0.4%，吞吐是 16B 小帧的 11 倍。按 `XLINK_MAX_PAYLOAD`（16KB）打包最划算。
3. **真机瓶颈通常不在协议**。协议层仅占每帧十几纳秒，TCP 栈与网卡 DMA 才是主要成本。

---

## 11. 测试

### 11.1 运行方式

| 入口 | 命令 / 路径 |
|------|------------|
| 命令行（功能） | `XinYueC_Static.exe --test xlink` |
| 命令行（质量门禁） | `XinYueC_Static.exe --test xlink-quality` |
| 命令行（合并闸门） | `XinYueC_Static.exe --test xlink-all` |
| Shell 命令 | `test xlink` / `test xlink-quality` |
| 图形菜单 | Test 菜单「协议栈 → XLink(xlink)」 |

返回码：`0` 全部通过，非 `0` 存在失败，可直接用于 CI 闸门。

### 11.2 测试规模

| 套件 | 文件 | 断言数 | 覆盖内容 |
|------|------|--------|----------|
| 功能测试 | `XLinkTest.c` | 130 | 编解码、生命周期、状态机、握手、请求应答、推送、订阅、超时、心跳、防御性路径 |
| 质量门禁 | `XLinkQualityTest.c` | 41 | 内存泄漏、性能基准、吞吐天花板、边界与对抗 |

### 11.3 关键测试项

**内存泄漏**：反复创建/销毁 200 次 + 带传输夹具 100 次，断言系统分配器在用字节回到基线（实测差 0）。

**性能基准**：小帧高频 10000 帧、大载荷 200 帧，统计每帧耗时与吞吐。

**边界对抗**：
- 线上载荷满 `XLINK_MAX_PAYLOAD` 的帧（需多轮 pump 才能读完）
- 粘包：50 帧一次 pump 全部正确切分
- 订阅表满：超出槽位拒绝不越界
- 畸形帧流：垃圾字节不崩溃且判违例断链
- 连续 20 轮建链/断链无资源累积

---

## 12. 设计与实现要点

### 12.1 关键实现决策

| 决策 | 理由 |
|------|------|
| 12 字节定长帧头 | 嵌入式解析成本低，无变长字段处理 |
| 小端线序 | fbdev/网络栈惯例，ARM 与 x86 一致 |
| u32 topicId 走载荷而非 msgCode | msgCode 只有 u16，而 topic 从 0x10000 起会溢出 |
| 单次原子写 | 部分传输是「全有全无」，分段写会产生截断帧流 |
| 常驻收发缓冲 | 稳态零堆分配，避免高频路径 malloc/free |
| 库内不取时 | 遵循「`Src/` 禁止直呼时间源」，时间由调用方传入 |
| 请求不自动应答 | 真实设备 IO 耗时，应答可能异步 |
| 定容表（待应答/订阅） | 嵌入式内存可预测，无动态扩容 |
| 业务帧不投递半成品 | 握手完成前不投递，防止乱序帧污染应用状态 |

### 12.2 内存模型

```
会话生命周期：
  init     → 一次性申请 m_rxBuffer (12+16KB) + m_txBuffer (12+16KB)
  收发     → 稳态零堆分配（复用常驻缓冲）
  deinit   → 成对释放两块缓冲
```

> ⚠️ 载荷上限受限于 `m_txBuffer` 容量。帧总长超过 `XLINK_HEADER_SIZE + XLINK_MAX_PAYLOAD` 会被拒绝。

### 12.3 线程模型

**单线程主循环口径**，与 `XPlatformFbInput_posix` 的 fbdev 驱动一致：

- 所有 API 均**不可阻塞**
- 须在持有者的事件循环（`XSocketNotifier` 或每圈 poll 回调）内调用
- **全类不加锁**

---

## 13. 常见问题

### Q1：为什么 handshake 前收不到业务消息？

**这是设计行为**。握手完成前会话状态为 `Connected`，不投递业务消息，防止未建立状态下的乱序帧喂给应用。检查双方是否都调用了 `pump` 驱动握手。

### Q2：发送失败返回 false 怎么排查？

按顺序检查：

1. `XLinkSession_state(s)` 是否为 `Inactive`（未挂载传输或已判线）
2. `XLinkSession_isPending(s, someId)` 之前 —— 待应答表是否已满（`XLINK_PENDING_SLOTS`）
3. 载荷是否超过 `XLINK_MAX_PAYLOAD - 4`
4. 底层传输是否背压（如回环设备环写满）

### Q3：`pump` 的 `nowMs` 该怎么传？

统一用 `XDateTime_currentMSecsSinceEpoch()`（单调毫秒）。**时间轴必须连续** —— 回退或跨越静默阈值会被误判为断线。

### Q4：画面卡顿/不刷新？

- 确认 GUI 侧调用了 `XLinkSession_pump`
- 确认状态推送周期合理（建议 10Hz，别用 1000Hz）
- 大画面块考虑分帧：单帧上限 16KB，超出画面需分块推送
- 检查是否误开了 zlib 压缩但对端未协商

### Q5：如何确认对端能力？

```c
uint16_t cap = XLinkSession_negotiatedCapability(session);
bool tls = (cap & XLinkCapability_Tls) != 0;
bool zlib = (cap & XLinkCapability_Zlib) != 0;
```

### Q6：能改协议格式吗？

可以，但需两端同步升级。`XLINK_VERSION` 高字节是主版本，低字节是次版本 —— 次版本变更保持向后兼容（新增字段追加在载荷末尾），主版本变更表示不兼容。

### Q7：为什么大载荷帧需要多轮 pump？

`XIODevice_read_1` 优先读内部读缓冲层，单轮调用取不满时会回到底层设备读取。16KB 的帧可能需要多轮才能读完，这是传输层的正常语义，不是协议问题。

---

## 附：源码索引

| 内容 | 位置 |
|------|------|
| 协议常量 | `XLink_config.h` |
| 线上格式与枚举 | `XLinkProto.h` |
| 编解码实现 | `XLinkCodec.h/.c` |
| 会话对象 | `XLinkSession.h/.c` |
| 功能测试 | `Test/XProtocolTest/XLinkTest.c` |
| 质量门禁 | `Test/XProtocolTest/XLinkQualityTest.c` |
| 测试入口声明 | `Test/XProtocolTest/XLinkTest.h` |
