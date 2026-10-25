# XLink 私有应用层协议

> 版本：v1.2　|　协议版本：`0x0100`　|　日期：2026-10-24
> 落点：`Src/XProtocol/XLink/`　|　开关：`XLINK_ON`
>
> v1.2 变更：删除被自动超时（setRequestTimeout + requestFinished 信号）取代的
> 待应答被动超时查询 API（含声明/实现/文档/测试）；示例与实战代码全面收敛到
> 自动握手与信号消费口径。线上格式零变化。
>
> v1.1 变更：会话升级为 XObject 事件驱动（readyRead + 内建定时器）、握手零样板
> （自动 Hello）、新增服务端包装 XLinkServer；线上格式零变化，全 API 向后兼容。

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
  - [6.7 服务端包装 XLinkServer](#67-服务端包装-xlinkserver推荐)
  - [6.8 客户端包装 XLinkClient](#68-客户端包装-xlinkclient)
  - [6.9 TLS 加密链路](#69-tls-加密链路)
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
| `XLinkSession.h/.c` | 会话对象：收发、分发、订阅表、待应答表、心跳；XObject 事件驱动 |
| `XLinkServer.h/.c` | 服务端包装（可选，`XLINK_SERVER_ON`）：监听、自动建会话、会话回收 |
| `XLinkClient.h/.c` | 客户端包装（可选，`XLINK_CLIENT_ON`）：自动重连（默认关） |

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

> 测试口径：四个能力位的协商矩阵（双侧不置位 / 单侧置位两方向 / 对称置位 /
> 全组合）与 HelloAck 附加快照的 `4+N` 超长载荷容忍语义（解析只读前 4 字节，
> 附加字节被忽略），由 `Test/XProtocolTest/XLinkTest.c` 分组十五全量覆盖。

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

> ⚠️ 能力位须在事件循环转起来之前设置（通常在 create_ex 之后同步调用）：
> 会话挂载传输后首次驱动会自动补发 Hello，能力位随 Hello 上线。

**第 3 步：握手——什么都不用做**

自 v1.1 起握手零样板：挂载传输后首次 pump（或首个 readyRead/定时器节拍）
自动补发 Hello；收到对端 Hello 时自动回 HelloAck，双方转 `Ready`。
手动 `sendSession(Hello)` 仍然支持（发送成功会抑制自动补发，不会产生重复帧），
"谁先发都行"的对称语义不变。

```c
/* 旧式手动握手（仍可用，可选） */
XLinkHelloPayload hello;
uint8_t buf[4];

hello.m_version = XLINK_VERSION;
hello.m_capability = XLinkCapability_Zlib | XLinkCapability_Tls;
XLinkCodec_encodeHello(&hello, buf, sizeof(buf), NULL);
XLinkSession_sendSession(session, XLinkSessionCode_Hello, buf, 4);
```

**第 4 步：驱动事件循环（二选一或混用）**

```c
/* 方式 A：异步驱动（推荐）——什么都不写。
   会话继承 XObject，挂载传输时已自动接线 readyRead（收包事件驱动）并
   启动内建定时器（XLINK_HEARTBEAT_TICK_MS 节拍，驱动心跳与判线），
   事件循环跑起来链路就自己活着。 */

/* 方式 B：手动驱动——按圈 pump，时间注入单调毫秒 */
while (running) {
    XLinkSession_pump(session, XDateTime_currentMSecsSinceEpoch());
    /* ... 其他事件处理 ... */
}
```

两种方式可混用（pump 幂等）。回环设备不发 readyRead、无事件分发器的裸主循环、
需要注入固定时钟的测试，走方式 B；真实 TCP/TLS 传输走方式 A。

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
>
> 订阅幂等：对已订阅的 topic 重复 `subscribe` 返回 `true`，不重复占用定容表槽位、不向对端重发帧；`unsubscribe` 对未订阅 topic 返回 `false`（不发无意义帧）。推送谁发都行——任意一端都可向对端 `sendPublish`，空载荷（仅 topicId 通知）合法。

### 6.5 待应答与超时

请求发出后进入待应答表，应答到达自动出表并发射 `requestFinished(succeeded=true)`。超时处理由**自动超时引擎**承担（设置阈值大于 0 即启用）：pump 在时间检查区内自动扫描待应答表，对超时槽执行"静默重发或终局上报"，应用层零轮询：

```c
XLinkSession_setRequestTimeout(session, 500); /* 超时阈值 500ms（0 = 被动模式） */
XLinkSession_setNumberOfRetries(session, 2);  /* 超时后自动重发上限 2 次 */

/* 终局结果走信号（应答成功 / 最终超时各发一次） */
XObject_connect_2((XObject*)session,
                  XSignal(XLinkSession_requestFinished_signal),
                  on_request_finished);   /* 参数：requestId, command, succeeded */

static void on_request_finished(XObject* sender, XVarList* args)
{
    uint32_t requestId;
    uint16_t command;
    bool succeeded;

    (void)sender;
    XVarList_args_3(args, uint32_t, requestId, uint16_t, command,
                    bool, succeeded);
    if (!succeeded)
        show_error("设备无响应 (命令 %u)", command);   /* 槽已出表，不会重复上报 */
}
```

自动模式行为细则：

- **静默重发**：超时槽若可重试且重试额度未满，按**原 requestId** 原载荷（快照自待应答槽）重新发送，对端无感知 id 变化；重发成功即刷新该槽的超时起算点
- **可重试条件**：请求载荷不超过 `XLINK_RETRY_PAYLOAD_MAX`（默认 128 字节，快照进定容槽位）；空载荷（len=0）也算可重试；超长请求照常发送但不可重试，首次超时即终局
- **终局上报**：额度用尽 / 不可重试 / 重发失败时，发射 `requestFinished(succeeded=false)` 并出表，同一请求不会产生第二次上报
- **成功路径**：应答命中待应答表时发射 `requestFinished(succeeded=true)`；漂移应答（无对应槽）与重复应答不发射
- **判定口径**：自请求发出（或最近一次重发）起严格大于阈值才算超时；未经 pump 就发出的请求自首次 pump 那一拍起算（`sendRequest` 不自行取时，首拍不判超时）
- **被动模式**：默认参数（阈值 0、重试 0）下整段逻辑被跳过，超时请求长期滞留待应答表（可经 `isPending` 查询，断链时随表清空），行为与无自动超时时完全一致

**状态信号**：链路状态真变化时（挂载→Connected、握手→Ready、断链→Inactive）自动发射 `stateChanged`，同态重复设置不重发，UI 可据此免轮询刷新连接图标。

**容量限制**：待应答表定容 `XLINK_PENDING_SLOTS`（默认 8 槽）。表满时 `sendRequest` 返回 `false`，**拒绝发请求而非静默丢失**。高频请求场景请调大该宏。

### 6.6 心跳与判线

**自动心跳**：握手完成后按 `XLINK_HEARTBEAT_INTERVAL_MS`（默认 1 秒）的间隔门限
自动发送 Heartbeat。异步模式下由会话内建定时器按 `XLINK_HEARTBEAT_TICK_MS`
（默认 250ms）节拍驱动；手动模式下由每圈 pump 驱动。

**自动判线**：超过 `XLINK_HEARTBEAT_TIMEOUT_MS`（默认 3 秒）未收到任何字节，
会话自动转 `Inactive`。判线检查密度由节拍决定（异步 250ms / 手动 = pump 圈频），
默认配置下判定误差不超过一个节拍。

**主动断链**

```c
XLinkSession_sendSession(session, XLinkSessionCode_Goodbye, NULL, 0);
XLinkSession_reset(session);
```

> ⚠️ **手动模式的时间轴必须连续**。pump 的 `nowMs` 若发生回退或跨越静默阈值，
> 会误判为断线。建议统一用 `XDateTime_currentMSecsSinceEpoch()`（单调时钟）传入。
> 异步模式时间由库内经同一接口取，天然连续；但**两种模式不要混用不同时间源**
> （如手动传假时钟、定时器用真实时钟），否则静默计时被污染。

### 6.7 服务端包装 XLinkServer（推荐）

服务端样板（accept → create_ex → setCapability → setCallback → 逐会话驱动与
回收）收敛为一个对象，开关 `XLINK_SERVER_ON`（默认开）：

```c
XLinkServer* srv = XLinkServer_create();
XLinkServer_setCapability(srv, XLinkCapability_Zlib);      /* 新会话模板 */
XLinkServer_setDispatchCallback(srv, on_frame, NULL);      /* 新会话模板 */
XLinkServer_setNotifyCallback(srv, on_session_added_removed, NULL); /* 可选 */
XLinkServer_listen_base(srv, "127.0.0.1", 45200);

/* 事件循环里：什么都不用写。
   - newConnection 信号到达即自动建会话（套模板）；
   - 会话各自异步驱动（readyRead + 内建定时器）；
   - Inactive 会话（Goodbye/判线/违例/断开）由内建节拍自动回收。 */
```

手动模式（无事件分发器）下按圈 `XLinkServer_pump(srv, now)` 兜底：接受连接、
驱动会话、回收会话。`XLinkServer_sessionAt/count` 遍历在线会话；notify 回调
（Added/Removed）维护"会话指针 → 应用上下文"映射。

> 回收时机安全性：同步删除只发生在最外层 pump 末尾或事件分派的定时器回调里，
> 此刻任何会话的调用栈均已退出，符合「对象释放方式选择约束」的同步释放判据；
> pump 重入（在分发回调里再调 pump）时跳过回收，推迟到外层。

### 6.8 客户端包装 XLinkClient

与服务端包装对偶的客户端形态（开关 `XLINK_CLIENT_ON`，默认开）：自持 XTcpSocket +
XLinkSession，命名与状态机对齐 XModbusDevice/QModbus 惯例。

```c
XLinkClient* cli = XLinkClient_create();
XLinkClient_setHostPort(cli, "192.168.1.10", 45200);
XLinkClient_setCapability(cli, XLinkCapability_Zlib);
XLinkClient_setDispatchCallback(cli, on_frame, NULL);   /* 会话模板 */
XLinkClient_setAutoReconnect(cli, true);                 /* 默认关 */
XLinkClient_setReconnectInterval(cli, 1000);             /* 默认 1s */
XLinkClient_setMaxReconnectAttempts(cli, -1);            /* -1 无限 */
XLinkClient_connectToServer(cli);                        /* 异步发起，之后全自动 */
/* 状态经 XLinkClient_stateChanged_signal 通知：
   UnconnectedState → ConnectingState → ConnectedState(会话已握手) →…
   断线且自动重连开 → ReconnectingState（内建定时器驱动重试）→ 回 ConnectedState */
XLinkSession* s = XLinkClient_session(cli);              /* 借用，业务收发照旧 */
XLinkClient_disconnectFromServer(cli);                   /* Goodbye+排空写队列+拆除 */
```

要点：TCP 连上才建会话（会话自身自动握手）；曾连上后判线且 autoReconnect 开则成对拆除旧
会话/套接字并按 reconnectInterval 重试，达 maxReconnectAttempts（非 -1）转 UnconnectedState
收手；手动 `disconnectFromServer` 永不触发重连。

### 6.9 TLS 加密链路

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

> 可运行的实战案例分立两个程序文件（推荐从这里读用法）：`XLinkDemoServer.c`
> 与 `XLinkDemoClient.c`，一个"远程电机监控"故事：3 条命令（PING 同步回执 / ADD 序列化 /
> SLOW 异步回执）+ 2 个 topic（STATE 周期推送 / ALARM 事件通知）。三个入口：
>
> | 命令 | 形态 |
> |------|------|
> | `--test xlink-demo-server` | 双进程联调·服务端（监听 45200，等客户端） |
> | `--test xlink-demo-client` | 双进程联调·客户端（带重试连接，跑完全流程） |
> | `--test xlink-demo` | 本进程联调：两个角色同进程跑真实 TCP 环回（CI 友好） |
>
> 双进程用法（两个终端，先起服务端后起客户端），两侧日志带毫秒时刻可对轴，
> 均以 exit 0 退出：案例本身就是"异步模式下应用代码长什么样"的答案——
> 两端事件循环只有 processEvents + sleep，没有任何 XLink 手动调用。
>
> 下面的示例保留手动握手与手动 pump 的写法（两种模式都受支持，便于对照
> 理解协议细节）；生产代码按案例的异步形态写。

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

/* 请求终局槽：自动超时模式下，超时终局经 requestFinished 信号上报 */
static void on_request_finished(XObject* sender, XVarList* args)
{
    uint32_t requestId;
    uint16_t command;
    bool succeeded;

    (void)sender;
    XVarList_args_3(args, uint32_t, requestId, uint16_t, command,
                    bool, succeeded);
    if (!succeeded)
        XPrintf("[控制] 命令 %u 超时未应答\n", command);
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

                /* 请求自动超时 + 终局信号（挂载后设置一次即可） */
                XLinkSession_setRequestTimeout(session, 2000);
                XObject_connect_2((XObject*)session,
                                  XSignal(XLinkSession_requestFinished_signal),
                                  on_request_finished);

                /* 主动发 Hello（对称：谁先发都行；不写则首次 pump 自动补发） */
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

/* 请求终局槽：自动超时模式下，超时终局经 requestFinished 信号上报 */
static void on_request_finished(XObject* sender, XVarList* args)
{
    uint32_t requestId;
    uint16_t command;
    bool succeeded;

    (void)sender;
    XVarList_args_3(args, uint32_t, requestId, uint16_t, command,
                    bool, succeeded);
    if (!succeeded)
        show_error("设备无响应 (命令 %u)", command);
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
    /* 自动超时 + 终局信号：会话建立后设置一次即可，主循环零轮询 */
    XLinkSession_setRequestTimeout(s, 2000);
    XObject_connect_2((XObject*)s,
                      XSignal(XLinkSession_requestFinished_signal),
                      on_request_finished);

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
| `XLINK_SERVER_ON` | 1 | 服务端包装 XLinkServer（依赖 XTcpServer） |
| `XLINK_VERSION` | `0x0100` | 协议版本（高字节主版本，低字节次版本） |
| `XLINK_HEADER_SIZE` | 12 | 帧头固定长度 |
| `XLINK_TOPIC_USER_BASE` | `0x10000` | 业务 topicId 起始值 |
| `XLINK_MAX_PAYLOAD` | `16384` | **线上**载荷上限（含 topicId 前缀） |
| `XLINK_PENDING_SLOTS` | 8 | 待应答表槽位 |
| `XLINK_REQUEST_TIMEOUT_MS` | 0 | 请求自动超时阈值（毫秒；0 = 被动模式，运行时可经 `setRequestTimeout` 调整） |
| `XLINK_RETRY_MAX_DEFAULT` | 0 | 请求自动重发次数默认值（运行时可经 `setNumberOfRetries` 调整） |
| `XLINK_RETRY_PAYLOAD_MAX` | 128 | 可自动重发的请求载荷上限（超长请求超时不重试） |
| `XLINK_TOPIC_SLOTS` | 16 | 订阅表槽位 |
| `XLINK_HEARTBEAT_INTERVAL_MS` | 1000 | 心跳间隔（0 = 不发心跳） |
| `XLINK_HEARTBEAT_TIMEOUT_MS` | 3000 | 静默判线阈值 |
| `XLINK_HEARTBEAT_TICK_MS` | 250 | 异步节拍默认值（运行中可经 `XLinkServer_setTickInterval` 调整） |
| `XLINK_CLIENT_ON` | 1 | 客户端包装 XLinkClient（自动重连，默认关） |
| `XLINK_RETRY_MAX_DEFAULT` | 0 | 请求超时自动重试默认次数（0=仅报超时；经 `setNumberOfRetries` 调整） |
| `XLINK_RETRY_PAYLOAD_MAX` | 128 | 可重试请求的载荷快照上限（超过则该请求超时不重试） |
| `XLINK_REQUEST_TIMEOUT_MS` | 0 | 请求自动超时判定阈值（0=被动模式；经 `setRequestTimeout` 调整） |

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
| `XLinkSession_stateChanged_signal(s, state)` | 信号：链路状态真变化（同态不重发） |

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
| `XLinkSession_setRequestTimeout(s, ms)` | 设自动超时阈值（0 = 被动模式，默认） |
| `XLinkSession_requestTimeout(s)` | 读自动超时阈值 |
| `XLinkSession_setNumberOfRetries(s, count)` | 设超时自动重发次数上限 |
| `XLinkSession_numberOfRetries(s)` | 读自动重发次数上限 |
| `XLinkSession_requestFinished_signal(s, reqId, cmd, ok)` | 信号：请求终局（应答 true / 超时终局 false） |

### 9.6 分发

| API | 说明 |
|-----|------|
| `XLinkSession_setDispatchCallback(s, cb, userData)` | 注册业务消息回调 |
| `XLinkSession_dispatch_base(s, frame, payload)` | 虚表分发入口（子类扩展用） |

### 9.7 服务端包装（XLINK_SERVER_ON）

| API | 说明 |
|-----|------|
| `XLinkServer_create()` | 堆创建（内嵌 XTcpServer） |
| `XLinkServer_listen_base(srv, host, port)` | 监听；port=0 自动分配 |
| `XLinkServer_close(srv)` | 停止监听并回收全部会话 |
| `XLinkServer_setCapability(srv, cap)` | 新会话能力位模板 |
| `XLinkServer_setDispatchCallback(srv, cb, ud)` | 新会话分发回调模板 |
| `XLinkServer_setNotifyCallback(srv, cb)` | 会话 Added/Removed 通知 |
| `XLinkServer_setTickCallback(srv, cb, ud)` | 注册周期业务回调（随内建节拍触发） |
| `XLinkServer_setTickInterval(srv, ms)` | 运行中调整内建节拍周期；0 = 停节拍转手动 pump |
| `XLinkServer_tickInterval(srv)` | 读取当前节拍周期 |
| `XLinkServer_pump(srv, nowMs)` | 手动兜底泵：accept + 驱动 + 回收 |
| `XLinkServer_sessionCount(srv)` / `sessionAt(srv, i)` | 遍历在线会话 |
| `XLinkServer_tcpServer_const(srv)` | 内嵌监听器（高级配置，借用） |

### 9.8 客户端包装（XLINK_CLIENT_ON）

| API | 说明 |
|-----|------|
| `XLinkClient_create()` / `init` | 堆/栈创建（自持 socket+会话） |
| `XLinkClient_setHostPort(cli, host, port)` | 目标地址（UTF-8，深拷贝） |
| `XLinkClient_setCapability` / `setDispatchCallback` | 新会话模板 |
| `XLinkClient_setAutoReconnect` / `setReconnectInterval` / `setMaxReconnectAttempts` | 自动重连配置（默认关/1000ms/-1 无限） |
| `XLinkClient_connectToServer` / `disconnectFromServer` | 异步建链 / 优雅断开（Goodbye+排空） |
| `XLinkClient_session(cli)` | 借用内部会话（业务收发入口） |
| `XLinkClient_state` / `isReady` | 状态查询（Unconnected/Connecting/Connected/Reconnecting） |
| `XLinkClient_stateChanged_signal` | 状态迁移信号（XEmitSignal 发射，XModbus 风格） |

### 9.9 编解码

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
| 功能测试 | `XLinkTest.c` | 411（含分组十五，绿灯实测） | 编解码、生命周期、状态机、换设备接线、自动握手与能力协商、请求应答、推送（含超限/空载荷）、订阅（含幂等）、自动超时（严格大于阈值/终局出表/不重报）、心跳、防御性路径、服务端包装（含复用/边界）、实战案例、信号与自动超时重试（stateChanged/requestFinished/静默重发/终局上报/sendTick==0 起算回归）、客户端包装 XLinkClient（NULL 防御、默认值与栈上生命周期、正常链路、踢线自动重连复活、重连关停、手动断开 Goodbye、最大重连次数；原 `XLinkClientTest.c` 已并入分组十四）、能力位全矩阵（None/对称/不对称两方向/全组合、HelloAck 附加快照 4+N 容忍、RequestTimeout 位不干扰自动超时；分组十五） |
| 质量门禁 | `XLinkQualityTest.c` | 48 | 内存泄漏（会话/换设备接线/订阅 churn/服务端生命周期+真实接入）、性能基准、吞吐天花板、边界与对抗 |

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
| 双驱动时间口径 | 手动 pump 由调用方注入单调毫秒（测试可注入固定时钟）；异步路径（readyRead/内建定时器）库内经 XDateTime_currentMSecsSinceEpoch 取时——与 XAbstractSocket 等框架模块同口径（仓库时间源裁定：时长测量用单调毫秒） |
| XObject 内建定时器而非 XTimer 组合 | startTimer/killTimer + timerEvent 重载即框架正统用法，零额外对象 |
| 请求不自动应答 | 真实设备 IO 耗时，应答可能异步 |
| 定容表（待应答/订阅） | 嵌入式内存可预测，无动态扩容 |
| 业务帧不投递半成品 | 握手完成前不投递，防止乱序帧污染应用状态 |
| 服务端同步回收而非 deleteLater | 回收点固定在"任何会话调用栈均已退出"的上下文（最外层 pump 末尾/事件分派的定时器回调），符合同步释放判据；deleteLater 依赖事件循环真正释放，无分发器环境会悬挂 |

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

统一用 `XDateTime_currentMSecsSinceEpoch()`（单调毫秒）。**手动模式时间轴必须连续** —— 回退或跨越静默阈值会被误判为断线。异步模式（readyRead/内建定时器驱动）无需关心：库内经同一接口取时。**两种驱动混用时不要传不同时间源**（如手动传假时钟），否则静默计时被污染。

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

### Q8：进程退出 / 删除会话前，最后几帧丢了？

**传输的写是异步管线**：`sendRequest/sendPublish` 等返回 `true` 只表示帧已进入套接字内部写缓冲排队，真正落到内核由写完成事件逐步推进。发送后立即退出进程或删除会话/套接字，会把队列中未 flush 的帧一起丢掉。正确姿势（见实战案例客户端收尾）：

```c
XLinkSession_sendSession(session, XLinkSessionCode_Goodbye, NULL, 0);
/* 排空写队列：每次调用至少推进一个写完成，循环到排空 */
while (XTcpSocket_waitForBytesWritten_base((XAbstractSocket*)sock, 200)) {}
XClassDelete((XClass*)session);
```

长时间运行的常驻进程无需关心——队列会在后续事件循环中自然 flush；只有"发完即走"的收尾路径需要显式排空。

### Q9：自动超时模式下，超时请求会重复上报吗？怎么和被动用法区分？

**不会重复上报**。每条请求只有一次终局：应答命中发 `requestFinished(true)`，超时终局（重试额度用尽 / 不可重试 / 重发失败）发 `requestFinished(false)` 并出表，出表后不再产生任何上报。

- **自动模式（阈值 > 0，推荐）**：pump 自动扫描待应答表，静默重发或终局上报，应用层只消费信号。
- **被动模式（默认，阈值 0）**：库不碰超时，请求长期滞留待应答表（可经 `isPending` 查询，断链时随表清空）；需要超时语义的业务请开启自动模式，不要混用于同一会话的同一批请求。

---

## 附：源码索引

| 内容 | 位置 |
|------|------|
| 协议常量 | `XLink_config.h` |
| 线上格式与枚举 | `XLinkProto.h` |
| 编解码实现 | `XLinkCodec.h/.c` |
| 会话对象 | `XLinkSession.h/.c` |
| 服务端包装 | `XLinkServer.h/.c` |
| 客户端包装（自动重连） | `XLinkClient.h/.c` |
| 实战案例·服务端程序 | `Test/XProtocolTest/XLinkDemoServer.c` |
| 实战案例·客户端程序 | `Test/XProtocolTest/XLinkDemoClient.c` |
| 实战案例·公共头（协议/日志/入口） | `Test/XProtocolTest/XLinkDemo.h` |
| 功能测试（含客户端包装分组十四） | `Test/XProtocolTest/XLinkTest.c` |
| 质量门禁 | `Test/XProtocolTest/XLinkQualityTest.c` |
| 测试入口声明 | `Test/XProtocolTest/XLinkTest.h` |
