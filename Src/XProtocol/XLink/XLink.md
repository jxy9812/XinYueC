# XLink 私有应用层协议

> 版本：v1.3-dev　|　协议版本：`0x0101`　|　日期：2026-10-27（进行中，见 §14）
> 落点：`Src/XProtocol/XLink/`　|　开关：`XLINK_ON`
>
> v1.3-dev 变更（登录认证 + 文件传输 + 底层加固，**进行中**，进度与已知问题见文末 §14）：
>   - **登录认证**（`XLINK_AUTH_ON`，默认开）：挑战应答（盐 + 迭代 HMAC-SHA256）
>     + 服务端证明防假冒 + 常量时间比较；新模块 `XLinkAuth`（协议与派生）、
>     `XLinkUserStore`（JSON 账户库，XString 存储）；会话新增 Authenticating 态
>     与 AuthRequest/AuthChallenge/AuthResponse/AuthResult 四个子码；服务端
>     `XLinkServer_addUser/setAuthEnabled` + 审计回调，客户端
>     `XLinkClient_setCredential` 自动登录 + `authFailed` 信号 + 认证失败不重连。
>     能力位降级默认拒绝（防绕过认证），`setAuthAllowAnonymous` 显式兼容。
>   - **文件传输**（`XLINK_FILE_ON`，默认开）：新消息类 `File=6`，七个操作码
>     （Open/OpenAck/Data/Ack/Finish/FinishAck/Abort），块 4KB、窗口 32KB、
>     每块必 ACK、断点续传、整文件 SHA-256 校验、沙箱（`XLinkFileServer`：
>     相对路径白名单 + 规范化根目录前缀校验 + 软链接拒绝）、引擎 `XLinkFileTransfer`、
>     客户端 `XLinkFileClient`（upload/download/abort + progress/finished 信号）。
>   - **底层加固**：发送队列（整帧原子入队 + pump 续写，非阻塞 TCP 短写不再
>     丢半帧）、握手超时（半开连接回收）、`keepAlive`（长耗时单向业务续命）、
>     会话统计（`XLinkSession_stats`）、`XLinkServer` 会话销毁前钩子
>     （`setSessionGoneHook`，扩展清理借用指针）、**接收缓冲越界根修**
>     （读取空间改为"剩余容量"，原公式在半帧残留时最多越界写 m_rxFilled 字节）。
>   - 测试：新增分组十六（认证 5 子组）与十七（文件 5 子组）；
>     当前 **621 通过 / 0 失败**（Linux x86-64 实测，2026-10-28；
>     下载端到端闭环与 Windows 平台说明见 §14.3）。
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
  - [6.10 登录认证](#610-登录认证xlink_auth_on)
  - [6.11 文件传输](#611-文件传输xlink_file_on)
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
| 双向：GUI 下发命令 + 服务器推状态 | 消息类正交（请求-应答/推送/订阅），任意一端既能发起请求也能推送 |
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
XIODevice* conn = (XIODevice*)XTcpServer_nextPendingConnection_base(server);
XLinkSession* session = XLinkSession_create_ex(conn);

/* GUI 进程：主动连接 */
XIODevice* conn = XTcpSocket_create();
XTcpSocket_connectToHost_base((XAbstractSocket*)conn, host, port,
                              XIODevice_ReadWrite,
                              XAbstractSocket_AnyIPProtocol);
XLinkSession* session = XLinkSession_create_ex(conn);
```

传输建立之后，协议层不再区分身份。**这意味着同一个类既能跑控制进程也能跑 GUI 进程**，两端可同时向对方发起请求。

### 2.3 模块文件

| 文件 | 职责 |
|------|------|
| `XLink_config.h` | 协议开关与容量常量 |
| `XLinkProto.h` | 线上格式定义：消息类枚举、能力位、帧头结构、认证/文件载荷 |
| `XLinkCodec.h/.c` | 小端序列化原语、Hello 载荷编解码、帧头编解码 |
| `XLinkSession.h/.c` | 会话对象：收发、分发、订阅表、待应答表、心跳、发送队列、认证内核；XObject 事件驱动 |
| `XLinkServer.h/.c` | 服务端包装（可选，`XLINK_SERVER_ON`）：监听、自动建会话、会话回收、认证集成 |
| `XLinkClient.h/.c` | 客户端包装（可选，`XLINK_CLIENT_ON`）：自动重连（默认关）、登录凭据 |
| `XLinkAuth.h/.c` | 登录认证支撑层（`XLINK_AUTH_ON`）：认证帧编解码、口令派生、挑战应答、常量时间比较 |
| `XLinkUserStore.h/.c` | JSON 账户库（`XLINK_AUTH_ON`）：盐 + 迭代 HMAC 摘要 + 角色/锁定 |
| `XLinkFile.h/.c` | 文件传输线上格式（`XLINK_FILE_ON`）：七个操作码编解码 + 文件名安全校验 |
| `XLinkFileTransfer.h/.c` | 文件传输引擎（`XLINK_FILE_ON`）：收发状态机、流控、断点续传、SHA-256 |
| `XLinkFileServer.h/.c` | 文件服务端沙箱（`XLINK_FILE_ON`）：根目录校验 + 策略位 + 会话槽 |
| `XLinkFileClient.h/.c` | 文件客户端（`XLINK_FILE_ON`）：upload/download/abort + progress/finished 信号 |

---

## 3. 线上格式

### 3.1 帧结构

定长帧头 **12 字节**，全部小端线序（fbdev/网络栈惯例，ARM 与 x86 一致）：

| 偏移 | 长度 | 字段 | 说明 |
|------|------|------|------|
| 0 | 4 | `payloadLen` | 载荷字节数（不含帧头），须 ≤ `XLINK_MAX_PAYLOAD` |
| 4 | 2 | `msgClass` | 消息类，见 [4. 消息类详解](#4-消息类详解) |
| 6 | 2 | `msgCode` | 业务码：Request/Response = 命令号；Publish/Subscribe = topicId 低 16 位；Session = 子类型（含认证四子码）；File = `XLinkFileCode` |
| 8 | 4 | `requestId` | Request/Response 关联 id；File 类 = transferId；其余消息类填 0 |
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

### 4.1 消息类总览

| 消息类 | 值 | 语义 | 是否等应答 | 谁发都行 |
|--------|-----|------|-----------|---------|
| `XLinkMsgClass_Session` | 0 | 会话管理：Hello/HelloAck/Heartbeat/Goodbye + 登录认证四子码（见 4.2） | 否 | ✅ |
| `XLinkMsgClass_Request` | 1 | 请求-响应的请求方 | 是 | ✅ |
| `XLinkMsgClass_Response` | 2 | 请求-响应的应答方 | — | ✅ |
| `XLinkMsgClass_Publish` | 3 | 单向推送 | 否 | ✅ |
| `XLinkMsgClass_Subscribe` | 4 | 订阅 | — | ✅ |
| `XLinkMsgClass_Unsubscribe` | 5 | 退订 | — | ✅ |
| `XLinkMsgClass_File` | 6 | 文件传输（`XLINK_FILE_ON`）：msgCode = `XLinkFileCode`，requestId = transferId | 否 | ✅ |

**为什么这样设计**：消息类正交而非逐条枚举业务语义。Request/Response 管"我问你要答"，Publish 管"我单方面告诉你"，Subscribe 管"以后这类推给我"。三类正交组合，覆盖常规交互形态。新增业务只表现为新增 msgCode/topicId，**协议层与编解码层零改动**。

**File 类的特殊性**（v1.3 新增）：它不是业务消息——帧由文件传输引擎消费，**永不投递给业务分发回调**；`requestId` 字段被复用为 transferId（发起方分配、接收方回显）。仅当能力协商含 `XLinkCapability_FileTransfer` 时才允许收发，未协商时收到的 File 帧按协议违例丢弃（见 `XLinkSession.h` 的 `XLinkSessionFileCallback` 声明）。

### 4.2 会话子类型

`msgClass = XLinkMsgClass_Session` 时，`msgCode` 取以下值：

| 子类型 | 值 | 说明 |
|--------|-----|------|
| `XLinkSessionCode_Hello` | 1 | 建链后首帧，携版本与能力位 |
| `XLinkSessionCode_HelloAck` | 2 | 对 Hello 的应答；约定可携带全量快照 |
| `XLinkSessionCode_Heartbeat` | 3 | 心跳，静默超时即判线 |
| `XLinkSessionCode_Goodbye` | 4 | 主动断链通知；可选载荷首字节为原因（`XLinkGoodbyeReason`：Normal/Protocol/AuthFailed/AuthRequired/Timeout/ServerShutdown） |
| `XLinkSessionCode_AuthRequest` | 5 | 登录请求（客户端 → 服务端，`XLINK_AUTH_ON`）：`[u8 nameLen][name...]` |
| `XLinkSessionCode_AuthChallenge` | 6 | 登录挑战（服务端 → 客户端）：方法 + 盐 + 迭代次数 + nonce 32 |
| `XLinkSessionCode_AuthResponse` | 7 | 登录应答（客户端 → 服务端）：`HMAC-SHA256(H, nonce)` 摘要 |
| `XLinkSessionCode_AuthResult` | 8 | 登录结果（服务端 → 客户端）：结果码 + 角色 + 文本 + 服务端证明 32 |

认证四子码（5~8）为追加式扩展：载荷编解码在 `XLinkAuth.h/.c`（纯函数，逐字段小端，禁止 memcpy 结构体上线）；老对端走 default 分支静默忽略，能力位 Auth 未协商时不参与线上交互。

### 4.3 能力位

Hello / HelloAck 载荷携带，握手后取**按位与**作为协商结果：

| 能力位 | 值 | 含义 |
|--------|-----|------|
| `XLinkCapability_Zlib` | 0x0001 | 支持 zlib 载荷压缩 |
| `XLinkCapability_Tls` | 0x0002 | 链路已运行在 TLS 之上 |
| `XLinkCapability_Snapshot` | 0x0004 | HelloAck 载荷内携带全量快照 |
| `XLinkCapability_RequestTimeout` | 0x0008 | 支持请求超时通知 |
| `XLinkCapability_Auth` | 0x0010 | 支持登录认证（`XLINK_AUTH_ON`）。服务端仅在确实配置了口令/账户库时置位；双方交集含本位时，握手后进入认证流程（见 §5/§6.10） |
| `XLinkCapability_FileTransfer` | 0x0020 | 支持文件传输（`XLINK_FILE_ON`）。交集含本位才允许收发 File 消息类（见 §6.11） |

未置位的特性对端不得依赖（如未协商 `ZLIB` 则载荷必须为原始字节）。

> ⚠️ 「对端要求认证」的判定看**对端原始声明**（`m_peerDeclaredCapability`），不看交集——客户端不宣告 Auth 时交集里没有该位，但服务端强制置位，必须按原始声明识别并快速失败，否则会出现短暂 Ready 窗口（详见 §5）。

> 测试口径：四个 v1.2 能力位（Zlib/Tls/Snapshot/RequestTimeout）的协商矩阵（双侧不置位 / 单侧置位两方向 / 对称置位 / 全组合）与 HelloAck 附加快照的 `4+N` 超长载荷容忍语义（解析只读前 4 字节，附加字节被忽略），由 `Test/XProtocolTest/XLinkTest.c` 分组十五全量覆盖；Auth/FileTransfer 两位的协商、降级与门控行为由分组十六覆盖（见 §11）。

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
  └───────────┘                     └──────┬──────┘
        ▲                                  │ 收到 Hello / HelloAck
        │                                  ▼
        │                        能力协商后分流（XLINK_AUTH_ON）
        │                     ┌─────────────┐
        │      认证通过/匿名链路 │Authenticating│ 本端要求认证或对端要求认证
        │   ┌─────────────────│ 握手完成但    │（凭据就绪时客户端主动发
        │   │                 │ 认证未完成    │  AuthRequest，服务端等）
        │   ▼                 └──────┬──────┘
  ┌─────────────┐                   │ 认证通过（AuthResult=Ok）
  │    Ready    │◄──────────────────┘
  │ 链路健康     │  认证失败/超时/失败达上限 → Goodbye + Inactive
  │ 全部分类投递 │
  └─────────────┘
```

状态枚举（`XLinkSessionState`）：`Inactive=0`、`Connected=1`、`Ready=2`、`Authenticating=3`（`XLINK_AUTH_ON` 门控）。

**关键行为**

- 握手完成前，**不投递任何业务消息** —— 防止把未建立状态下的乱序帧喂给应用
- `Authenticating` 态同样不投递业务消息，**仅登录帧可通行**（File 类帧在非 Ready 态也不处理）
- 静默超过 `XLINK_HEARTBEAT_TIMEOUT_MS`（默认 3 秒）自动转 `Inactive`
- 收到 `Goodbye` 立即转 `Inactive`（对端主动断链感知）
- `setDevice(NULL)` 立即断开，并清空帧缓冲、待应答表、订阅表

**握手完成后的分流规则**（`XLINK_AUTH_ON`，实现在 `XLinkSession.c` 的 Hello/HelloAck 处理内，按优先级依次判定）：

| # | 条件 | 结果 |
|---|------|------|
| 1 | 客户端配了凭据（`setAuthResponse` + `setAuthUser`）且**对端原始声明**含 Auth | 主动发 `AuthRequest`，进入 `Authenticating`（不置 authPassed） |
| 2 | 本端要求认证（服务端 `setAuthLookup`）且能力**交集**含 Auth | 进入 `Authenticating` 等对端发 AuthRequest |
| 3 | 本端要求认证但对端不宣告 Auth（老对端/能力位降级） | **默认拒绝**：Goodbye(AuthRequired) + 断链。`setAuthAllowAnonymous(true)` 才显式回落匿名 Ready（防"不声明能力位即绕过认证"） |
| 4 | 对端要求认证但本端无凭据 | **快速失败**：不发必错的登录请求、不谎报 Ready；Goodbye(AuthRequired) + `authChanged(false, NoCredential)` + 断链 |
| 5 | 其余 | 匿名链路，直接 `Ready`（authPassed 置位，`isAuthenticated` 为 true） |

**认证阶段的守卫与超时**

- **Connected 守卫防双 Hello 重复触发**：自动握手会让双方各收到 Hello 与 HelloAck 两帧；分流逻辑整体包裹在 `state == Connected` 判定内，第二次进入时状态已是 Ready/Authenticating，必须整体跳过——否则认证态会被覆盖回 Ready，认证门控失效（v1.3 联调中曾致 6 项测试变红）。
- **认证超时**：进入 `Authenticating` 后超过 `XLINK_AUTH_TIMEOUT_MS`（默认 5 秒，运行时 `setAuthTimeout` 可调）未完成认证即断链，服务端资源不被悬挂。
- **失败上限**：服务端对每连接累计认证失败次数，达 `XLINK_AUTH_FAILURE_LIMIT`（默认 3，`setAuthFailureLimit` 可调）即下发 `AuthResult(Rejected)` + `Goodbye(AuthFailed)` 并断链；未达上限允许对端换用户/换口令重试。客户端收到任何拒绝即终局断链（口令错误属永久失败，不重试）。
- **Ready 后的重复认证帧忽略**（幂等，防重放干扰）。
- **握手超时**与认证超时互斥生效：`XLINK_HANDSHAKE_TIMEOUT_MS`（默认 5 秒）只管 Connected 态"连上但不说话"的半开连接；进入 Ready/Authenticating 后不再受握手超时约束（认证另有超时）。

**认证通过后的状态**：`isReady()` 仅在 `Ready` 态为 true；`isAuthenticated()` 在认证通过或匿名链路（直接 Ready）下为 true——业务层可统一按「可服务」判断。认证结果经 `authChanged` 信号上报（服务端在应答校验后、客户端在收到 AuthResult 后各发射一次）。

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
XIODevice* conn = (XIODevice*)XTcpServer_nextPendingConnection_base(server);
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
XLinkServer_setNotifyCallback(srv, on_session_added_removed); /* 可选，2 参 */
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
XSslSocket_connectToHost_base(ssl, host, port, XIODevice_ReadWrite,
                              XAbstractSocket_AnyIPProtocol);
XLinkSession* s = XLinkSession_create_ex((XIODevice*)ssl);

/* 握手时声明链路已加密 */
XLinkSession_setCapability(s, XLinkCapability_Zlib | XLinkCapability_Tls);
```

**要点**

- 开关 `XLINK_SSL_ON` 只决定 XLink 是否**感知/声明** TLS 能力位，关闭后仍可挂任意 `XIODevice`
- 厂内网明文即可；跨网段或公网再切 TLS
- 协商结果可查：`XLinkSession_negotiatedCapability(s) & XLinkCapability_Tls`

---

### 6.10 登录认证（`XLINK_AUTH_ON`）

v1.3 新增的可选扩展（默认开，关闭后能力位 Auth 不置位、登录子码整体裁空，行为与未引入前完全一致）。定位：厂内网"控制进程 ↔ GUI 进程"之间的**最低限度接入控制**——口令明文不过网、服务端不存口令明文、客户端能识别假冒服务端。

**协议时序**（四个会话子码 5~8，见 §4.2）：

```
客户端                                     服务端
  │  Hello(version, capability 含 Auth)       │
  │─────────────────────────────────────────►│
  │  HelloAck(cap 交集)                       │
  │◄─────────────────────────────────────────│
  │            （握手完成 → Authenticating）    │
  │  AuthRequest[nameLen][name]              │
  │─────────────────────────────────────────►│ 查账户库（盐+迭代次数+H+角色）
  │  AuthChallenge[method][salt][iter][nonce] │
  │◄─────────────────────────────────────────│
  │  本地派生 H = iterHMAC-SHA256(口令, 盐)     │
  │  AuthResponse[HMAC-SHA256(H, nonce)]     │
  │─────────────────────────────────────────►│ 同式重算 + 常量时间比较
  │  AuthResult[status][role][serverProof]   │
  │◄─────────────────────────────────────────│ serverProof = HMAC(H, nonce‖0x01)
  │  校验服务端证明 → Ready                    │ → Ready
```

**服务端用法**（`XLinkServer` 已内嵌 `XLinkUserStore` 账户库）：

```c
static void on_auth_audit(XLinkServer* srv, XLinkSession* session,
                          XLinkServerAuthEvent event,
                          const char* user, uint8_t role)
{
    /* 审计：Success/Failure/Timeout/Anonymous 四类事件。
       回调拿不到口令（协议里就没有），禁止把摘要打到日志。 */
    (void)srv; (void)session;
    XPrintf("[认证] %s -> %s (role=%u)\n", user ? user : "-",
            event == XLinkServerAuthEvent_Success ? "ok" : "denied", role);
}

XLinkServer* srv = XLinkServer_create();
XLinkServer_setUserStorePath(srv, "xlink-users.json");  /* 立即加载；不存在视为空库 */
if (XLinkServer_userCount(srv) == 0)                    /* 首次部署建初始账户 */
    XLinkServer_addUser(srv, "admin", "secret", XLinkAuthRole_Admin);
XLinkServer_saveUserStore(srv);      /* addUser 是内存态，须显式落盘（原子写） */
XLinkServer_setAuthEnabled(srv, true);  /* 新会话须登录；存量会话不被踢 */
XLinkServer_setAuthAuditCallback(srv, on_auth_audit, NULL);
/* 可选：XLinkServer_setAuthAllowAnonymous(srv, true) 才允许不宣告 Auth 的
   老对端匿名接入——默认 false 是安全基线（防能力位降级绕过，见 §5）。 */
XLinkServer_listen_base(srv, "0.0.0.0", 45200);
```

要点：

- `setAuthEnabled(true)` 会把新会话能力位模板自动 OR 上 `XLinkCapability_Auth`，会话握手完成后进入 `Authenticating`（状态机规则见 §5）
- **添加首个账户不会自动启用认证**——必须显式 `setAuthEnabled(true)`，避免"加了个账户就连不上"
- 账户库为空时启用认证无意义：所有登录都会失败
- 失败上限/超时模板：`setAuthFailureLimit`（默认 3 次）/ `setAuthTimeout`（默认 5 秒），只影响之后建立的会话
- 在线会话查询：`sessionUser_const(srv, session)` / `sessionRole(srv, session)`；账户管理：`removeUser` / `setUserLocked` / `userAt`（展示层不要把摘要输出到界面/日志）

**客户端用法**（`XLinkClient` 内嵌凭据，**自动重登**）：

```c
static void on_auth_failed(XObject* sender, XVarList* args)
{
    int reason;               /* XLinkClientAuthFailure */
    uint8_t status;           /* 协议层 XLinkAuthStatus，便于诊断 */
    (void)sender;
    XVarList_args_2(args, int, reason, uint8_t, status);
    show_login_error(reason); /* 口令错误重试无意义——自动重连已被库停止 */
}

XLinkClient* cli = XLinkClient_create();
XLinkClient_setHostPort(cli, "192.168.1.10", 45200);
XLinkClient_setCredential(cli, "admin", "secret");  /* 深拷贝保存；清除时零化 */
XObject_connect_2((XObject*)cli, XSignal(XLinkClient_authFailed_signal),
                  on_auth_failed);
XLinkClient_setAutoReconnect(cli, true);
XLinkClient_connectToServer(cli);
/* 之后每次连接/重连成功都会自动登录；isReady 即"已认证可服务"。 */
```

要点：

- **认证失败停重连**：口令错误属永久失败，`authFailed` 发射后自动重连被停止，状态转 `UnconnectedState`——不会拿着错口令反复撞服务端（撞到服务端失败上限也会被断）
- 凭据查询：`hasCredential` / `user_const` / `lastAuthFailure`（信号到达后取，驱动界面提示）；`clearCredential` 零化后释放
- 无凭据遇强制认证：快速失败（`authFailed(NoCredential)`），不挂起、不出现短暂 Ready 窗口

**会话级高级接口**（不用 XLinkServer/XLinkClient 包装、直接驱动 `XLinkSession` 时挂接；包装类内部正是调它们）：

| API | 角色 | 说明 |
|-----|------|------|
| `XLinkSession_setAuthLookup(s, cb, ud)` | 服务端 | 注册凭据查询回调即"本端要求认证"；回调填盐/迭代次数/H/角色，返回 `XLinkSessionAuthLookupResult`（Ok/BadUser/Locked）。须在握手完成前设置 |
| `XLinkSession_setAuthAllowAnonymous(s, allow)` | 服务端 | 降级兼容开关，默认 false（见 §5 分流规则 3） |
| `XLinkSession_setAuthResponse(s, cb, ud)` | 客户端 | 挑战应答回调：入参挑战，出参 method/response/H（H 供会话校验服务端证明）；返回 false 视为无凭据 |
| `XLinkSession_setAuthUser(s, user)` | 客户端 | 登录用户名（随 AuthRequest 上报；超长截断不产生畸形帧） |
| `XLinkSession_isAuthenticated(s)` | 双端 | 认证通过为 true；匿名链路进 Ready 即 true（业务层统一按「可服务」判断） |
| `XLinkSession_authUser_const(s)` / `authRole(s)` | 双端 | 认证通过的用户名（借用）/角色 |
| `XLinkSession_setAuthTimeout(s, ms)` / `setAuthFailureLimit(s, n)` | 服务端 | 运行期覆盖编译期默认（5s / 3 次） |
| `XLinkSession_authChallenge_signal(s, user, result)` | 服务端 | 挑战下发时发射（审计/限流观察） |
| `XLinkSession_authChanged_signal(s, passed, status, role)` | 双端 | 认证结论确定时发射（服务端应答校验后 / 客户端收到 AuthResult 后） |

**安全口径**（与 `XGuiRemoteAuth`/`XConsoleShellLogin` 对齐并加强）：

- 口令明文禁止过网、禁止入日志；服务端只持有派生摘要 H 与随机盐（账户库格式与 XConsoleShellLogin 一致，可互换/迁移）
- nonce 每连接随机（`XRandomGenerator_fillSecure` 安全随机），防重放
- 服务端证明 `HMAC(H, nonce‖0x01)` 与客户端应答 `HMAC(H, nonce)` 输入不同、互相不可替代——客户端据此识别假冒服务端
- 应答比较走**常量时间**（`XLinkAuth_constantTimeEqual`），用户不存在与口令错误对外同码（不暴露账户存在性）
- 挑战应答**不防离线字典攻击**（盐与响应在线上可见）——跨网段必须叠加 TLS（§6.9）

> 测试口径：分组十六覆盖认证编解码与口令派生、账户库内存态语义、真实 TCP 端到端（成功/错误口令/无凭据）、能力位降级不可绕过 + 认证超时 + 握手超时、统计与发送队列观测（见 §11）。

### 6.11 文件传输（`XLINK_FILE_ON`）

v1.3 新增的可选扩展（默认开，关闭后消息类 File 整体裁空、能力位 FileTransfer 不置位）。定位：把"大内容走文件通道"从业务分帧里解放出来——协议帧上限 16KB，日志/固件/采集数据这类块状内容不该塞进 Publish。

**一次上传的帧序列**（操作码见 §4.1 File 类，msgCode = `XLinkFileCode`，requestId = transferId）：

```
发起方（客户端）                             接收方（服务端）
  │ Open[mode=Upload][name][totalSize][chunk][window][startOffset]
  │─────────────────────────────────────────►│ 沙箱校验（§下文）+ 策略判定
  │ OpenAck[accepted][chunk][window][startOffset][totalSize]
  │◄─────────────────────────────────────────│
  │ Data[offset][bytes...]（窗口内连续发）      │
  │─────────────────────────────────────────►│ 落盘并回 Ack（每块必回）
  │ Ack[nextOffset][creditBytes]             │
  │◄─────────────────────────────────────────│
  │ ... 循环至尾部 ...                         │
  │ Finish[totalSize][sha256 32]             │
  │─────────────────────────────────────────│ 复算整文件 SHA-256 比对
  │ FinishAck[ok][status]                    │ .part 原子改名
  │◄─────────────────────────────────────────│
  │        Abort[status] 任意时刻双向可发       │
```

**协议要点**（线上常量全部可在 `XLink_config.h` 裁剪）：

- **块 4KB、窗口 32KB、每块必 ACK**：发送方维护 credit（未确认字节额度），`offset - ackOffset` 达到窗口即停发等 Ack；接收方**每收到一块就回 Ack**——把 Ack 当作"已落盘水位"的即时通告。攒半窗口再回会在"额度刚好耗尽"时形成一次往返停顿，且末块 Ack 的边界条件容易让双方互等（实测下载方向会卡住）；每块一 Ack 的开销仅 12+8+4 字节帧，相对 4KB 数据可忽略
- **OpenAck 头部 28 字节**（`XLINK_FILE_OPEN_ACK_HEADER`）：`accepted(1)+status(1)+textLen(2)+chunkBytes(4)+windowBytes(4)+startOffset(8)+totalSize(8)`。**totalSize 下载方向必填**——发起方 Open 时并不知道文件多大，必须由服务端在 OpenAck 里告知（曾误写 24 字节导致解析错位）；Open 头部同为 28 字节（`XLINK_FILE_OPEN_HEADER`）
- **断点续传（.part）**：接收侧写入 `<目标>.part`，若已存在且小于总大小则按其大小作为起始偏移续写（Open/OpenAck 的 `startOffset` 双方对齐）；完成后原子改名。SHA-256 不符即 Abort 并**保留 .part**（便于排查）
- **整文件 SHA-256**：发送侧在发起时预计算（`beginSendFile` 用已打开句柄 seek 回 0 哈希，避免传完再二次打开同一路径在 Windows 共享模式下失败）；Finish 携带摘要，接收侧复算比对
- **空闲超时**：超过 `XLINK_FILE_IDLE_TIMEOUT_MS`（默认 30 秒）无任何进展即 Abort
- 方向枚举 `XLinkFileMode`：Upload=0（发起方发文件）、Download=1（发起方请求对端发文件）；结果码 `XLinkFileStatus` 共 11 种（Ok/Rejected/NotFound/AlreadyExists/TooLarge/NoSpace/Checksum/Protocol/Aborted/Timeout/RoleDenied）

**服务端沙箱**（`XLinkFileServer`，值类型 init/deinit，attach 后由服务端节拍驱动）：

```c
static XLinkFileServer g_fs;

XLinkFileServer_init(&g_fs);   /* 默认策略：禁止任何方向 + 要求认证 */
XLinkFileServer_setRootDir(&g_fs, "./xfs_root");     /* 目录必须已存在 */
XLinkFileServer_setFlags(&g_fs, XLINK_FILESRV_ALLOW_UPLOAD
                              | XLINK_FILESRV_ALLOW_DOWNLOAD
                              | XLINK_FILESRV_ALLOW_OVERWRITE);
XLinkFileServer_setMaxFileBytes(&g_fs, 64ull * 1024ull * 1024ull);
XLinkFileServer_setMinRole(&g_fs, XLinkAuthRole_User);  /* 配合认证按角色门控 */
XLinkFileServer_attach(&g_fs, srv);  /* 接管 File 帧；自动补 FileTransfer 能力位 */
/* 收摊：XLinkFileServer_detach(&g_fs) → XLinkFileServer_deinit(&g_fs) */
```

策略位（`XLINK_FILESRV_*`，可按位或）：

| 策略位 | 值 | 含义 |
|--------|-----|------|
| `XLINK_FILESRV_ALLOW_UPLOAD` | 0x01 | 允许上传（客户端 → 服务端） |
| `XLINK_FILESRV_ALLOW_DOWNLOAD` | 0x02 | 允许下载（服务端 → 客户端） |
| `XLINK_FILESRV_ALLOW_OVERWRITE` | 0x04 | 允许覆盖已存在文件（否则拒绝 `AlreadyExists`） |
| `XLINK_FILESRV_REQUIRE_AUTH` | 0x08 | 要求会话已通过认证（**默认开启**） |
| `XLINK_FILESRV_ALLOW_SUBDIR` | 0x10 | 允许在根目录下创建子目录（默认关闭） |

沙箱三道闸（缺一不可，代码在 `XLinkFile.c` / `XLinkFileServer.c`）：

1. **文件名白名单**：`XLinkFile_isSafeName` 拒绝绝对路径（`/`、`\`、盘符）、`..` 目录穿越、`.` 开头隐藏名、NUL/控制字符、超长名
2. **根目录前缀校验**：相对路径拼根目录后规范化（Canonical），失败回退 Absolute，前缀不符即拒——软链接逃逸在这一步被拦
3. **软链接/目录拒绝**：写入侧拒绝符号链接，读取侧拒绝越界

**客户端**（`XLinkFileClient`，XObject；同一时刻只允许一个活跃传输，重连时自动中止旧传输防悬挂）：

```c
static void on_progress(XObject* sender, XVarList* args)
{
    uint32_t transferId; uint64_t done, total;
    (void)sender;
    XVarList_args_3(args, uint32_t, transferId, uint64_t, done, uint64_t, total);
    update_progress_bar(done, total);
}

static void on_finished(XObject* sender, XVarList* args)
{
    uint32_t transferId; bool ok; uint8_t status; const char* text;
    (void)sender;
    XVarList_args_4(args, uint32_t, transferId, bool, ok,
                    uint8_t, status, const char*, text);
    /* 成功/失败各恰一次；status 见 XLinkFileStatus */
}

XLinkFileClient* fc = XLinkFileClient_create();
XLinkFileClient_attach(fc, cli);                 /* 借用 XLinkClient */
XObject_connect_2((XObject*)fc, XSignal(XLinkFileClient_progress_signal),
                  on_progress);
XObject_connect_2((XObject*)fc, XSignal(XLinkFileClient_finished_signal),
                  on_finished);

uint32_t tid = 0;
XLinkFileClient_upload(fc, "local.bin", "log/remote.bin", 1, &tid);
/* flags bit0 = 允许覆盖；上传 = 客户端发文件到服务端沙箱 */
XLinkFileClient_download(fc, "remote.bin", "local_copy.bin", 1, &tid);
/* 下载 = 请求服务端发文件，落到本地 local_copy.bin */
XLinkFileClient_abort(fc, XLinkFileStatus_Aborted, "user cancel");

/* 状态查询：isBusy / state / doneBytes / totalBytes；引擎借用：transfer() */
```

**手动驱动与单驱动门控**：`XLinkFileClient_pump(fc, nowMs)` 每圈必泵传输引擎（它无任何异步驱动，不泵则发送侧不续发数据块）；承载会话默认**不**代泵（Auto 门控）——异步环境（本线程有事件分发器且心跳拍 > 0）会话已由框架驱动（内建定时器 + readyRead），再泵一次即成「顺序双泵」；无分发器的手动环境或心跳拍编译为 0 时才由本函数代泵会话。可用 `XLinkFileClient_setSessionDrive` 切 Force（每圈强制代泵，旧版行为）或 `XLinkFileClient_willPumpSession` 查询下一圈的代泵判定（与 pump 同源）。注意真实 TCP 的读写均为事件驱动（见 XAbstractSocket）：「分发器在位却不转事件」的宿主即便 Force 也无法搬运数据，Force 面向环回等同步传输的手动驱动与诊断对比。异步环境调用 pump 请传 `nowMs=0`（内部取实时），勿注入异构时钟。

**会话续命钩子（preSessionPump）**：文件传输是长耗时单向流量——本端可能长时间"只发不收"，若对端 Ack 间隔超过静默阈值（3 秒）会话会被误判断链。两级防线：

- 会话级：`XLinkSession_keepAlive(s, nowMs)` 刷新静默计时（只在确有进展时调，不要用它掩盖真死链）
- 服务端级：`XLinkServer_setPreSessionPumpHook(srv, cb, ud)` 在**每个托管会话 pump 之前**调用（判线逻辑在 pump 内！等 tick 回调——pump 末尾——再续命已判线断链）。`XLinkFileServer_attach` 自动挂接，为活跃传输的会话续命；配套 `setSessionGoneHook` 在会话删除前清槽防悬垂指针

> 测试口径：分组十七覆盖文件协议编解码与文件名安全校验（纯函数）、服务端沙箱路径策略、真实 TCP 端到端上传/下载（含 37KB 逐字节比对、SHA-256 校验与断点续传）、激进双泵节奏与单驱动门控子句锁（5 个子分组，见 §11.3）。下载端到端的排查与闭环结论见 §14.3。

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
#include "XHostAddress.h"
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
    XHostAddress any;
    XHostAddress_setAddressSpecial(&any, XHostAddress_AnySpecial); /* 0.0.0.0 */
    XTcpServer_listen(server, &any, 45200);

    XLinkSession* session = NULL;
    uint32_t lastPush = 0;

    for (;;) {
        uint32_t now = (uint32_t)XDateTime_currentMSecsSinceEpoch();

        /* 接受新连接 */
        if (!session) {
            XIODevice* conn =
                (XIODevice*)XTcpServer_nextPendingConnection_base(server);
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

在 `XLink_config.h` 中定义，或通过 CMake 编译选项覆盖。按下表顺序分组登记（与头文件编排一致，全量）：

**协议总开关与子功能开关**

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `XLINK_ON` | `XPROTOCOL_ON` | 协议总开关（统一在 `XProtocol_config.h` 登记） |
| `XLINK_CODEC_ON` | 1 | 帧编解码层 |
| `XLINK_SESSION_ON` | 1 | 会话层 |
| `XLINK_SSL_ON` | 1 | TLS 能力感知（实际加密由 `XSslSocket` 承担） |
| `XLINK_SERVER_ON` | 1 | 服务端包装 XLinkServer（依赖 XTcpServer） |
| `XLINK_CLIENT_ON` | 1 | 客户端包装 XLinkClient（自动重连，默认关） |
| `XLINK_AUTH_ON` | 1 | 登录认证扩展（XLinkAuth/XLinkUserStore）；关闭时能力位 Auth 不置位、会话子码 5~8 整体裁空 |
| `XLINK_FILE_ON` | 1 | 文件传输扩展（XLinkFile/XLinkFileServer/XLinkFileClient）；关闭时消息类 File 整体裁空 |

**请求自动超时**

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `XLINK_REQUEST_TIMEOUT_MS` | 0 | 请求自动超时阈值（毫秒；0 = 被动模式，运行时可经 `setRequestTimeout` 调整） |
| `XLINK_RETRY_MAX_DEFAULT` | 0 | 请求自动重发次数默认值（0 = 仅报超时；运行时可经 `setNumberOfRetries` 调整） |
| `XLINK_RETRY_PAYLOAD_MAX` | 128 | 可自动重发的请求载荷快照上限（超过则该请求超时不重试） |

**发送队列与超时（v1.3 加固）**

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `XLINK_TX_QUEUE_BYTES` | `128*1024` | 每会话发送队列容量（字节）。整帧原子入队 + pump 续写，短写不丢数据；0 = 关闭队列（退回一次性直写语义）。默认足以容纳一整窗口（32KB）文件数据并留控制帧余量 |
| `XLINK_HANDSHAKE_TIMEOUT_MS` | 5000 | 握手超时（毫秒）：挂载传输后未完成握手（未进 Ready/Authenticating）即判链路无效断开，回收"连上但从不说话"的半开连接；0 = 不检查 |

**登录认证常量（`XLINK_AUTH_ON`）**

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `XLINK_AUTH_TIMEOUT_MS` | 5000 | 认证超时（毫秒）：进入 Authenticating 后未完成认证即断链；0 = 不检查 |
| `XLINK_AUTH_FAILURE_LIMIT` | 3 | 单连接认证失败次数上限（服务端达到即 AuthResult(Rejected) + Goodbye(AuthFailed) 断链） |
| `XLINK_AUTH_SALT_BYTES` | 16 | 口令盐字节数（随机盐，随账户存储） |
| `XLINK_AUTH_NONCE_BYTES` | 32 | 挑战 nonce 与应答摘要字节数（SHA-256 输出宽度） |
| `XLINK_AUTH_HASH_ITERATIONS` | 10000 | 口令派生默认迭代次数（可被账户库逐账户覆盖） |
| `XLINK_AUTH_NAME_BYTES` | 32 | 用户名最大字节数（不含终止符） |
| `XLINK_AUTH_USER_SLOTS` | 16 | 账户库最多容纳的用户数 |
| `XLINK_AUTH_TEXT_BYTES` | 96 | 认证失败原因/结果文本最大字节数 |

**文件传输常量（`XLINK_FILE_ON`）**

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `XLINK_FILE_CHUNK_BYTES` | 4096 | 传输块大小（字节）：DATA 帧承载的业务数据量，须 ≤ `XLINK_MAX_PAYLOAD - 12`（DATA 载荷含 u64 偏移） |
| `XLINK_FILE_WINDOW_BYTES` | `32*1024` | 流控窗口（字节）：收到 ACK 前允许的未确认数据量 |
| `XLINK_FILE_MAX_TRANSFERS` | 4 | 服务端同时托管的传输数上限（每会话最多一个传输） |
| `XLINK_FILE_MAX_BYTES` | `128MB` | 单个文件大小上限（字节）；0 = 不限制 |
| `XLINK_FILE_IDLE_TIMEOUT_MS` | 30000 | 单次传输空闲超时（毫秒）：无任何进展即中止 |
| `XLINK_FILE_TICK_MS` | 10 | 传输期间高频驱动节拍（毫秒）：有活动传输时把 `XLinkFileClient` 节拍切到此值；0 = 不切换 |
| `XLINK_FILE_NAME_BYTES` | 128 | 远程文件名最大字节数（不含终止符） |
| `XLINK_FILE_TEXT_BYTES` | 128 | 文件传输结果文本最大字节数 |

**协议常量**

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `XLINK_VERSION` | `0x0101` | 协议版本（高字节主版本，低字节次版本）。正本定义在 `XLinkProto.h`（1.1 新增认证/文件两个可选能力位）；`XLink_config.h` 保留 `#ifndef` 兜底宏 |
| `XLINK_HEADER_SIZE` | 12 | 帧头固定长度 |
| `XLINK_TOPIC_USER_BASE` | `0x10000` | 业务 topicId 起始值 |
| `XLINK_MAX_PAYLOAD` | `16384` | **线上**载荷上限（含 topicId 前缀）；同时是接收/发送缓冲的容量依据 |
| `XLINK_PENDING_SLOTS` | 8 | 待应答表槽位 |
| `XLINK_TOPIC_SLOTS` | 16 | 订阅表槽位 |
| `XLINK_HEARTBEAT_INTERVAL_MS` | 1000 | 心跳间隔（0 = 不发心跳） |
| `XLINK_HEARTBEAT_TIMEOUT_MS` | 3000 | 静默判线阈值 |
| `XLINK_HEARTBEAT_TICK_MS` | 250 | 异步节拍默认值（运行中可经 `XLinkServer_setTickInterval` 调整） |

**裁剪示例**

```c
/* 极简嵌入式配置：关闭可选扩展、缩小缓冲 */
#define XLINK_AUTH_ON         0
#define XLINK_FILE_ON         0
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
| `XLinkSession_pump(s, nowMs)` | 非阻塞泵，驱动收发、队列续写、握手/认证超时与心跳 |
| `XLinkSession_state(s)` | 查询链路状态（Inactive/Connected/Ready/Authenticating） |
| `XLinkSession_isReady(s)` | 是否已握手且健康（仅 Ready 态为 true） |
| `XLinkSession_remoteVersion(s)` | 对端协议版本 |
| `XLinkSession_negotiatedCapability(s)` | 能力交集 |
| `XLinkSession_pendingBytes(s)` | 发送队列未写出字节数（背压观测；未启用队列恒为 0） |
| `XLinkSession_stats(s, &out)` | 累计统计快照（收发帧/字节、超时/违例/复位计数；reset 不清零） |
| `XLinkSession_keepAlive(s, nowMs)` | 刷新链路活跃时间，抑制静默判线（长耗时单向业务续命，见 §6.11） |
| `XLinkSession_setHandshakeTimeout(s, ms)` / `handshakeTimeout(s)` | 握手超时设置/读取（0 = 不检查，默认 `XLINK_HANDSHAKE_TIMEOUT_MS`） |
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
| `XLinkServer_setTickInterval(srv, ms)` / `tickInterval(srv)` | 运行中调整/读取内建节拍周期；0 = 停节拍转手动 pump |
| `XLinkServer_publish(srv, topic, payload, len)` | 一对多发布：只推"已订阅且握手完成"的会话，返回推送到的会话数 |
| `XLinkServer_broadcast(srv, topic, payload, len)` | 广播：不管订阅关系推给全部握手完成的会话 |
| `XLinkServer_pump(srv, nowMs)` | 手动兜底泵：accept + 驱动 + 回收 |
| `XLinkServer_sessionCount(srv)` / `sessionAt(srv, i)` | 遍历在线会话 |
| `XLinkServer_tcpServer_const(srv)` | 内嵌监听器（高级配置，借用） |

**登录认证（`XLINK_AUTH_ON`，详见 §6.10）**

| API | 说明 |
|-----|------|
| `XLinkServer_setAuthEnabled(srv, on)` / `authEnabled(srv)` | 启用/查询登录认证（新会话能力位自动 OR Auth，存量不踢） |
| `XLinkServer_setUserStorePath(srv, path)` | 设置账户库 JSON 路径并立即加载（不存在视为空库） |
| `XLinkServer_saveUserStore(srv)` | 账户库写回已设置路径（原子写） |
| `XLinkServer_addUser(srv, user, pwd, role)` / `removeUser(srv, user)` | 内存态增删账户（口令只用于即时派生，不留存） |
| `XLinkServer_setUserLocked(srv, user, locked)` | 锁定/解锁账户（内存态） |
| `XLinkServer_userCount(srv)` / `userAt(srv, i)` | 账户数 / 只读视图（展示层勿输出摘要） |
| `XLinkServer_setAuthAllowAnonymous(srv, allow)` | 认证启用时是否允许不宣告 Auth 的老对端匿名接入（默认 false） |
| `XLinkServer_setAuthFailureLimit(srv, n)` / `setAuthTimeout(srv, ms)` | 失败上限/认证超时模板（只影响之后建立的会话） |
| `XLinkServer_setAuthAuditCallback(srv, cb, ud)` | 认证审计回调（Success/Failure/Timeout/Anonymous） |
| `XLinkServer_sessionUser_const(srv, s)` / `sessionRole(srv, s)` | 查询在线会话的登录用户/角色 |

**文件传输挂接（`XLINK_FILE_ON`，详见 §6.11）**

| API | 说明 |
|-----|------|
| `XLinkServer_setFileCallback(srv, cb, ud)` | File 消息类处理回调模板（`XLinkFileServer_attach` 自动安装） |
| `XLinkServer_setSessionGoneHook(srv, cb, ud)` | 会话删除前内部钩子（扩展清理借用指针防悬垂；后注册覆盖先注册） |
| `XLinkServer_setPreSessionPumpHook(srv, cb, ud)` | 会话 pump **之前**钩子（扩展在判线前续命，见 §6.11） |

### 9.8 客户端包装（XLINK_CLIENT_ON）

| API | 说明 |
|-----|------|
| `XLinkClient_create()` / `init` | 堆/栈创建（自持 socket+会话） |
| `XLinkClient_setHostPort(cli, host, port)` | 目标地址（UTF-8，深拷贝） |
| `XLinkClient_host_const(cli)` / `port(cli)` | 读取已存地址/端口（借用视图） |
| `XLinkClient_setCapability` / `setDispatchCallback` | 新会话模板 |
| `XLinkClient_setAutoReconnect` / `setReconnectInterval` / `setMaxReconnectAttempts` | 自动重连配置（默认关/1000ms/-1 无限） |
| `XLinkClient_reconnectAttempts(cli)` | 本次失线以来已尝试的重连次数 |
| `XLinkClient_connectToServer` / `disconnectFromServer` | 异步建链 / 优雅断开（Goodbye+排空） |
| `XLinkClient_session(cli)` | 借用内部会话（业务收发入口；重连后旧指针失效） |
| `XLinkClient_state` / `isReady` | 状态查询（Unconnected/Connecting/Connected/Reconnecting） |
| `XLinkClient_stateChanged_signal` | 状态迁移信号（XEmitSignal 发射，XModbus 风格） |

**登录凭据（`XLINK_AUTH_ON`，详见 §6.10）**

| API | 说明 |
|-----|------|
| `XLinkClient_setCredential(cli, user, pwd)` | 设置登录用户名与口令（深拷贝；之后每次连接/重连自动登录） |
| `XLinkClient_clearCredential(cli)` | 清除凭据（零化后释放） |
| `XLinkClient_hasCredential(cli)` / `user_const(cli)` | 是否已设凭据 / 登录用户名 |
| `XLinkClient_lastAuthFailure(cli)` | 最近一次认证失败原因（`XLinkClientAuthFailure`） |
| `XLinkClient_isAuthenticated(cli)` | 当前会话是否已通过认证 |
| `XLinkClient_authFailed_signal(cli, reason, status)` | 信号：认证失败终局（同时停止自动重连，转 UnconnectedState） |

### 9.9 编解码

| API | 说明 |
|-----|------|
| `XLinkCodec_putU16(buf, v)` / `getU16(buf)` | 小端 16 位序列化 |
| `XLinkCodec_putU32(buf, v)` / `getU32(buf)` | 小端 32 位序列化 |
| `XLinkCodec_encodeHello(payload, buf, cap, &len)` | Hello 载荷编码 |
| `XLinkCodec_decodeHello(buf, len, &out)` | Hello 载荷解码 |
| `XLinkCodec_encodeHeader(frame, buf)` | 帧头打包（12 字节） |
| `XLinkCodec_decodeHeader(buf, len, &out)` | 帧头解包（含载荷上限校验） |

### 9.10 登录认证 XLinkAuth（`XLINK_AUTH_ON`，详见 §6.10）

| API | 说明 |
|-----|------|
| `XLinkAuth_encodeRequest(&msg, buf, cap, &len)` / `decodeRequest(buf, len, &out)` | AuthRequest 载荷编解码（`[u8 nameLen][name...]`；解码保证 `m_name[m_nameLen]` 处写 `'\0'`） |
| `XLinkAuth_encodeChallenge(...)` / `decodeChallenge(...)` | AuthChallenge 编解码（方法未知/盐长不符/迭代 0/越界拒绝） |
| `XLinkAuth_encodeResponse(...)` / `decodeResponse(...)` | AuthResponse 编解码（摘要须 == `XLINK_AUTH_NONCE_BYTES`） |
| `XLinkAuth_encodeResult(...)` / `decodeResult(...)` | AuthResult 编解码（文本超长按 `XLINK_AUTH_TEXT_BYTES` 截断） |
| `XLinkAuth_deriveHash(password, salt, iterations, outHash)` | H = 迭代 HMAC-SHA256(口令, 盐)；空口令非法 |
| `XLinkAuth_computeResponse(hash, nonce, out)` | response = HMAC-SHA256(H, nonce) |
| `XLinkAuth_computeServerProof(hash, nonce, out)` | serverProof = HMAC-SHA256(H, nonce‖0x01)（域分离，与应答互不可替代） |
| `XLinkAuth_constantTimeEqual(a, b, len)` | 常量时间比较（防时序侧信道） |
| `XLinkAuth_randomFill(buf, len)` | 密码学安全随机字节（盐/nonce） |
| `XLinkAuth_secureZero(data, len)` | 零化敏感缓冲（防编译器优化掉） |

### 9.11 账户库 XLinkUserStore（`XLINK_AUTH_ON`）

值类型（init/deinit 配对），可嵌入服务端私有块或栈上使用；JSON 持久化经 XDeviceFile/XJsonDocument，保存为原子写（临时文件 + 重命名），格式与 XConsoleShellLogin 账户库一致（可互换/迁移）。

| API | 说明 |
|-----|------|
| `XLinkUserStore_init(&store)` / `clear(&store)` | 初始化空表 / 清空（敏感摘要零化；不落盘） |
| `XLinkUserStore_load(&store, path, &exists)` | 从 JSON 加载；文件不存在视为成功（空库）；损坏返回 false 且库保持清空 |
| `XLinkUserStore_save(&store, path)` | 原子写 JSON；权限尽力仅属主可读写 |
| `XLinkUserStore_upsert(&store, user, pwd, role)` | 添加/更新账户（口令 NULL/空 = 不改；新建必须给口令） |
| `XLinkUserStore_remove(&store, user)` | 删除账户 |
| `XLinkUserStore_setLocked(&store, user, locked)` | 设置锁定状态 |
| `XLinkUserStore_find(&store, user)` / `at(&store, i)` / `count(&store)` | 只读查询（借用指针，修改后失效） |
| `XLinkUserStore_name_const(&rec)` / `isLocked(&rec)` / `hasPassword(&rec)` | 记录字段便捷查询 |
| `XLinkUserStore_verify(&store, user, pwd, &role)` | 校验口令：0 通过/1 无此用户/2 锁定/3 口令错（与 `XLinkSessionAuthLookupResult` 数值对齐，常量时间比较） |

### 9.12 文件协议与传输引擎（`XLINK_FILE_ON`，详见 §6.11）

**线上格式层 `XLinkFile.h`（纯函数，无状态无堆）**

| API | 说明 |
|-----|------|
| `XLinkFile_encodeOpen(...)` / `decodeOpen(...)` | Open 载荷编解码（头部 28 字节 `XLINK_FILE_OPEN_HEADER` + 文件名） |
| `XLinkFile_encodeOpenAck(...)` / `decodeOpenAck(...)` | OpenAck 编解码（头部 28 字节，含 u64 totalSize——下载方向必填） |
| `XLinkFile_encodeData(offset, data, len, ...)` / `decodeData(...)` | DATA 编解码（载荷 = `[u64 offset][bytes...]`；dataOut 指向 buf 内部） |
| `XLinkFile_encodeAck(...)` / `decodeAck(...)` | Ack 编解码（`[u64 nextOffset][u32 creditBytes]`） |
| `XLinkFile_encodeFinish(...)` / `decodeFinish(...)` | Finish 编解码（总大小 + 整文件 SHA-256） |
| `XLinkFile_encodeFinishAck(...)` / `decodeFinishAck(...)` | FinishAck 编解码 |
| `XLinkFile_encodeAbort(...)` / `decodeAbort(...)` | Abort 编解码（含原因与对端/本端发起标志） |
| `XLinkFile_isSafeName(name, nameLen)` | 文件名安全校验（沙箱第一道闸，规则见 §6.11） |
| `XLinkFile_baseName_const(name)` | 取文件名最后一段（仅展示用；落盘路径须走完整沙箱校验） |

**传输引擎 `XLinkFileTransfer.h`（值类型；一个对象承载一个方向的一次传输）**

| API | 说明 |
|-----|------|
| `XLinkFileTransfer_init(&t)` / `deinit(&t)` | 初始化 / 反初始化（deinit 不发 Abort、**保留回调**——progress/finished/userData 只由 setCallbacks 替换） |
| `XLinkFileTransfer_setCallbacks(&t, progress, finished, ud)` | 注册进度/终局回调（借用，可为 NULL） |
| `XLinkFileTransfer_beginUpload(&t, s, localPath, remoteName, flags, &id)` | 发起上传（Open 入队；须会话 Ready 且协商文件能力） |
| `XLinkFileTransfer_beginDownload(&t, s, remoteName, localPath, flags, &id)` | 发起下载 |
| `XLinkFileTransfer_beginReceive(&t, s, &open, localPath, id, chunk, window)` | 接收方受理 Open（已有 `.part` 按其大小续传） |
| `XLinkFileTransfer_beginSendFile(&t, s, localPath, remoteName, nameLen, flags, totalSize, id, chunk, window, startOffset)` | 服务端受理下载：不发 Open，直接回 OpenAck 进入 Active |
| `XLinkFileTransfer_pump(&t, nowMs)` | 推进一步（每轮最多一个 chunk，不阻塞） |
| `XLinkFileTransfer_onFrame(&t, frame, payload, nowMs)` | 处理收到的 File 帧（会话 File 回调内调用） |
| `XLinkFileTransfer_abort(&t, status, text)` | 主动中止（向对端发 Abort） |
| `XLinkFileTransfer_isFinished(&t)` / `state(&t)` | 是否终局（Done/Failed）/ 当前状态（Idle/Opening/Active/Finishing/Done/Failed） |
| `XLinkFileTransfer_doneBytes(&t)` / `totalBytes(&t)` | 已处理字节数 / 文件总字节数 |

**会话挂接**

| API | 说明 |
|-----|------|
| `XLinkSession_setFileCallback(s, cb, ud)` | 注册 File 帧处理回调：返回 true 已消费；File 帧永不投递业务分发回调；未协商 FileTransfer 能力时收到的帧按违例丢弃 |

### 9.13 文件服务端沙箱 XLinkFileServer（`XLINK_FILE_ON`，详见 §6.11）

值类型（init/deinit 配对）；attach 后由 `XLinkServer` 节拍驱动，自动安装 File 回调与两个内部钩子（会话销毁清槽 / pump 前续命）。

| API | 说明 |
|-----|------|
| `XLinkFileServer_init(&fs)` / `deinit(&fs)` | 初始化（默认策略：禁止任何方向 + 要求认证）/ 中止并清理全部传输 |
| `XLinkFileServer_setRootDir(&fs, dir)` | 设置沙箱根目录（必须已存在；规范化在首次使用时进行） |
| `XLinkFileServer_setFlags(&fs, flags)` | 策略位（`XLINK_FILESRV_*`，见 §6.11 策略位表） |
| `XLinkFileServer_setMaxFileBytes(&fs, bytes)` | 单文件大小上限（0 = 不限制） |
| `XLinkFileServer_setMinRole(&fs, role)` | 允许传输的最低角色（配合认证） |
| `XLinkFileServer_attach(&fs, srv)` / `detach(&fs)` | 挂接/摘除服务端（业务 dispatch/tick 以链式方式保留） |
| `XLinkFileServer_rootDir_const(&fs)` | 根目录只读借用 |

### 9.14 文件客户端 XLinkFileClient（`XLINK_FILE_ON` 且 `XLINK_CLIENT_ON`，详见 §6.11）

XObject 子类；同一时刻只允许一个活跃传输，客户端重连时自动中止旧传输。

| API | 说明 |
|-----|------|
| `XLinkFileClient_create()` / `init(obj)` | 堆/栈创建 |
| `XLinkFileClient_attach(obj, cli)` | 关联 XLinkClient（借用；存活期须覆盖本对象） |
| `XLinkFileClient_upload(obj, localPath, remoteName, flags, &id)` | 上传本地文件到服务端沙箱（Open 已入队返回 true） |
| `XLinkFileClient_download(obj, remoteName, localPath, flags, &id)` | 从服务端下载文件到本地 |
| `XLinkFileClient_abort(obj, status, text)` | 中止当前传输（status 0 归一为 Aborted） |
| `XLinkFileClient_pump(obj, nowMs)` | 手动驱动（每圈必泵传输引擎；会话按单驱动门控决定是否代泵；nowMs 可传 0 由内部取时） |
| `XLinkFileClient_setSessionDrive(obj, mode)` | 会话驱动模式：Auto（默认门控）/ ForceSessionPump（强制代泵，旧版行为） |
| `XLinkFileClient_sessionDrive(obj)` | 读取当前会话驱动模式（NULL 返回 Auto） |
| `XLinkFileClient_willPumpSession(obj)` | 查询下一圈 pump 是否代泵会话（与 pump 判定同源；动态求值） |
| `XLinkFileClient_isBusy(obj)` / `state(obj)` | 是否有活跃传输 / 当前传输状态 |
| `XLinkFileClient_doneBytes(obj)` / `totalBytes(obj)` | 已传输 / 文件总字节数 |
| `XLinkFileClient_transfer(obj)` | 借用传输引擎（高级用法，可再挂回调） |
| `XLinkFileClient_progress_signal(obj, id, done, total)` | 信号：传输推进（参数 uint32/uint64/uint64） |
| `XLinkFileClient_finished_signal(obj, id, ok, status, text)` | 信号：传输终局，成功/失败各一次（参数 uint32/bool/uint8/const char*） |

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
| 功能测试 | `XLinkTest.c` | 621（2026-10-28 Linux x86-64 实测 621 通过 / 0 失败） | 编解码、生命周期、状态机、换设备接线、自动握手与能力协商、请求应答、推送（含超限/空载荷）、订阅（含幂等）、自动超时（严格大于阈值/终局出表/不重报）、心跳、防御性路径、服务端包装（含复用/边界）、实战案例（含登录认证+文件传输剧情）、信号与自动超时重试（stateChanged/requestFinished/静默重发/终局上报/sendTick==0 起算回归）、客户端包装 XLinkClient（NULL 防御、默认值与栈上生命周期、正常链路、踢线自动重连复活、重连关停、手动断开 Goodbye、最大重连次数；原 `XLinkClientTest.c` 已并入分组十四）、能力位全矩阵（None/对称/不对称两方向/全组合、HelloAck 附加快照 4+N 容忍、RequestTimeout 位不干扰自动超时；分组十五）、**登录认证与底层加固（分组十六）**、**文件传输（分组十七）** |
| 质量门禁 | `XLinkQualityTest.c` | 69（含 v1.3 新增「五、文件传输质量」：内存 churn 回基线、吞吐基准、会话统计冒烟） | 内存泄漏（会话/换设备接线/订阅 churn/文件传输 churn/服务端生命周期+真实接入）、性能基准、吞吐天花板、文件传输吞吐、边界与对抗 |

### 11.3 新增分组覆盖（v1.3）

**分组十六：登录认证与底层加固**（`XLINK_AUTH_ON` 门控，5 个子分组）：

| 子分组 | 覆盖内容 |
|--------|----------|
| 一·编解码与口令派生 | 四个认证帧载荷编解码往返与畸形拒绝；H 派生 / challenge 应答 / 服务端证明 / 常量时间比较 / 安全随机的纯函数正确性 |
| 二·账户库 | `XLinkUserStore` 内存态语义：upsert/remove/锁定/查找/verify 返回码、库满与参数非法防御 |
| 三·端到端认证 | 真实 TCP 链路：正确口令登录成功（含服务端证明校验）、错误口令 authFailed(BadPassword)、无凭据快速失败（NoCredential） |
| 四·加固 | 能力位降级不可绕过认证（对端不宣告 Auth 即拒绝）、认证超时断链、握手超时回收半开连接 |
| 五·统计与队列 | `XLinkSession_stats` 计数正确性、`pendingBytes` 背压观测、发送队列短写续传 |

**分组十七：文件传输**（`XLINK_FILE_ON` 门控，5 个子分组）：

| 子分组 | 覆盖内容 |
|--------|----------|
| 一·协议编解码 | 七个操作码载荷编解码往返与畸形拒绝；`isSafeName` 文件名安全校验（绝对路径/穿越/隐藏名/NUL/控制字符/超长逐项覆盖） |
| 二·沙箱路径策略 | 服务端根目录规范化前缀校验、软链接/目录拒绝、策略位（方向/覆盖/大小/角色）判定（不依赖网络） |
| 三·端到端传输 | 真实 TCP 上传/下载：37KB 文件逐字节比对、SHA-256 校验、`.part` 断点续传、每块 ACK 流控、进度与终局信号 |
| 四·激进双泵节奏 | 驱动模式 API 语义 + Auto/Force 两模式下「processEvents 与 pump 背靠背」的端到端上传+下载连跑（驱动链回归锁） |
| 五·单驱动门控子句锁 | `willPumpSession` 查询锁门控子句（Auto 拒泵/Force 代泵/切回恢复）；无事件观察窗内传输停滞在 Opening、恢复转事件后同一笔传输照常到 Done（停滞只来自窗口，门控不破坏传输；Force 无事件救援不可断言——真实 TCP 读写均为事件驱动，见 §14.3） |

### 11.4 关键测试项

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
| 登录认证支撑（编解码/派生/比较） | `XLinkAuth.h/.c` |
| JSON 账户库 | `XLinkUserStore.h/.c` |
| 文件协议编解码 + 文件名校验 | `XLinkFile.h/.c` |
| 文件传输引擎（流控/续传/校验） | `XLinkFileTransfer.h/.c` |
| 文件服务端沙箱 | `XLinkFileServer.h/.c` |
| 文件客户端 | `XLinkFileClient.h/.c` |
| 实战案例·服务端程序 | `Test/XProtocolTest/XLinkDemoServer.c` |
| 实战案例·客户端程序 | `Test/XProtocolTest/XLinkDemoClient.c` |
| 实战案例·公共头（协议/日志/入口） | `Test/XProtocolTest/XLinkDemo.h` |
| 功能测试（含分组十四~十七） | `Test/XProtocolTest/XLinkTest.c` |
| 质量门禁 | `Test/XProtocolTest/XLinkQualityTest.c` |
| 测试入口声明 | `Test/XProtocolTest/XLinkTest.h` |

---

## 14. v1.3-dev 进度与交接（2026-10-27，换机续作）

### 14.1 已完成（全部编译通过 + 测试验证）

| 项 | 状态 | 说明 |
|---|---|---|
| P0 发送队列 | ✅ 完成 | `XLINK_TX_QUEUE_BYTES`（128KB）整帧原子入队；`XLinkSession_pendingBytes()` 观测背压；非阻塞 TCP 短写续传。原 411 项旧测试全绿。 |
| P0 接收缓冲越界根修 | ✅ 完成 | `XLinkSession_pump` 读取空间原为 `m_rxFilled + MAX` 截断到总容量，半帧残留时**最多越界写 m_rxFilled 字节**（堆损坏）。已改为 `cap - m_rxFilled`。这是文件传输联调中段错误/会话幽灵断链的根源之一。 |
| P0 握手超时 | ✅ 完成 | `XLINK_HANDSHAKE_TIMEOUT_MS`（5s）+ `setHandshakeTimeout`；半开连接回收（分组十六 4.4 用例覆盖）。 |
| P1 登录协议 | ✅ 完成 | 4 个会话子码（5~8）+ 载荷编解码 + 迭代 HMAC 派生 + 服务端证明（`HMAC(H, nonce‖0x01)`，域分离防应答替代证明）+ 常量时间比较。 |
| P1 会话认证状态机 | ✅ 完成 | `Authenticating` 态；握手分流规则（含 Connected 守卫防双 Hello 重复触发）；认证超时；失败上限；能力位降级默认拒绝。 |
| P1 用户库 | ✅ 完成 | `XLinkUserStore`：JSON 持久化（XDeviceFile/XJson），盐+迭代 HMAC 摘要，角色/锁定，XString 存用户名，位域状态。 |
| P1 服务端/客户端 API | ✅ 完成 | `XLinkServer_setAuthEnabled/addUser/setUserStorePath/审计回调`；`XLinkClient_setCredential`（XString 存储，清除时零化）+ 自动重登 + 认证失败停重连 + `authFailed` 信号。 |
| P2 文件协议 | ✅ 完成 | `XLinkFile.h/.c`：7 操作码编解码 + `isSafeName`（绝对路径/盘符/穿越/隐藏名/NUL/控制字符拒绝）。**注意 OpenAck 头部 28 字节（含 u64 totalSize，下载方向必填）。** |
| P2 传输引擎 | ✅ 完成 | `XLinkFileTransfer`：发/收状态机、窗口流控（`offset - ackOffset < window`）、断点续传（.part 续写）、整文件 SHA-256（发起时预计算，`beginSendFile` 用已打开句柄 hash 避免二次打开失败）。 |
| P2 服务端沙箱 | ✅ 完成 | `XLinkFileServer`：根目录规范化前缀校验（Canonical 失败回退 Absolute）、软链接/目录拒绝、策略位（方向/覆盖/大小/角色/认证）；`setSessionGoneHook` 清槽防悬垂；`setPreSessionPumpHook` 在会话 pump 前续命（判线前！tick 回调时已晚）。 |
| P2 客户端 | ✅ 完成 | `XLinkFileClient`：upload/download/abort + progress/finished 信号（XEmitSignal 风格）；收帧后立即 pump（防无事件分发器时停滞）；`XLinkFileClient_pump` 手动驱动口。 |
| P2 测试 | ✅ 全绿 | 分组十六 93 断言 + 分组十七（5 子分组，含激进双泵与单驱动门控子句锁）。**2026-10-28 Linux x86-64 实测：功能 621 通过 / 0 失败、质量 69 通过 / 0 失败（`--test xlink-all` 退出码 0）。** 下载端到端在 Linux 从不复现（旧机 Windows 平台相关，闭环结论见 14.3）。 |

### 14.2 关键设计要点（换机续作必读）

1. **认证分流必须带 `state == Connected` 守卫**：自动握手双方各收 Hello+HelloAck 两帧，不守卫会把 Authenticating 覆盖回 Ready（认证门控失效，曾有 6 项测试因此变红）。
2. **挑战必须携带账户存储盐**：客户端按挑战里的盐派生 H，服务端按存储 H 校验；曾因服务端发随机新盐导致正确口令也被拒（bad password）。
3. **"对端要求认证"看原始声明**（`m_peerDeclaredCapability`），不是能力交集——客户端不宣告 Auth 时交集里没有该位，但服务端强制置位，必须能识别并快速失败，否则出现短暂 Ready 窗口。
4. **会话 pump 前续命**：`XLinkServer` 的 preSessionPump 钩子在会话 pump（内含静默判线）**之前**调用；文件传输长耗时单向流量靠它防误判线。tick 回调（pump 末尾）里续命已经太晚。
5. **传输引擎 `deinit` 保留回调**（progress/finished/userData 只由 `setCallbacks` 替换），否则客户端收帧槽挂接会在重置时被清掉。
6. **OpenAck 帧头是 28 字节**：1+1+2+4+4+8+8，字段 totalSize（下载方向由服务端告知真实大小）。曾误写 24 导致解析错位。
7. **哈希用一次性接口 + 整读**：`XCryptographicHash_hash`（一次性）；服务端下载侧用 `xft_hashOpenFile`（已打开句柄 seek 回 0），二次打开同一路径在 Windows 共享模式下会失败。
8. **服务端 tick 回调内禁止再 pump 会话**（与 `XLinkServer_pump` 交织会搅乱收发游标，实测段错误）；`m_inTick` 重入保护保留。
9. **测试文件写入要带 `XIODevice_Create`**，且清理前先恢复写权限（Windows 只读文件删不掉，曾让测试第二次运行失败）。

### 14.3 下载端到端问题——已闭环（2026-10-28）

> **v1.3-dev 整改落点（状态同步）**：怀疑 1 已落地为 `XLinkFileClient_pump`
> 的单驱动门控（Auto 按分发器/心跳拍门控代泵，Force 回旧双泵行为，
> 分组十七·四/五锁定）；传输活跃期注入时钟的隔离靠「异步环境请传 0」
> 契约（keepAlive 对 0 内部自取实时），Opening 无应答重发加了
> `XLINK_FILE_OPEN_RETRY_MAX` 上限，不再无限停滞。全量闸门复跑中暴露的
> 「沙箱外写 / 下载落盘」3 项失败，根因是旧版测试 cleanup 以八进制
> 0666 误投 Qt 风格权限位域（chmod 出 0136 不可写残留且 removeRecursively
> 删不掉文件）污染下一轮运行——cleanup 已改用 `XFile_ReadOwner|WriteOwner`
> 枚举并分流删文件，测试写前/下载前自愈清残留。
>
> **闭环结论（换机 Linux 复测 + 四路整改工作流）**：
> 1. 下载 4 断言在 Linux **从未复现**（接手首跑 574/574 直接过）——旧机
>    Windows 平台相关，协议/会话层静态审查未发现缺陷。「Open 帧已写出但
>    对端收不到任何字节（连心跳都断）」的形态最符合**套接字写缓冲滞留**：
>    `pendingBytes=0` 只代表 XLink 发送队列排进 XTcpSocket 写缓冲，不等于
>    落到内核；若上传突发后写事件通知器失活，后续帧（含心跳）全部滞留，
>    客户端无感知（反方向心跳正常），服务端 3s 静默判线——与全部观察吻合。
> 2. 双驱动分析定论：**顺序双泵今日无害**（pump 幂等、单圈自洽无跨圈
>    游标），门控只是收敛暴露面；真正的雷是「回调栈内再泵会话」的重入
>    （§14.2 注 8 前科）。
> 3. 复跑另根修一处测试前提错误：门控子句锁（十七·五）初版断言「Force
>    代泵在无事件宿主里救活真实 TCP 传输」——**真实 TCP 的读写均为事件
>    驱动**（收包进环形缓冲、写残留推进都在分发器事件处理内，见
>    XAbstractSocket），无事件窗口里连 Force 也搬不动任何数据，客户端
>    会话 3s 静默判线正由此来；已改为 `willPumpSession` 查询锁子句 +
>    「无事件停滞 / 事件恢复后同一传输完成」行为锁。
> 4. **最终状态：功能 621 / 0、质量 69 / 0，`--test xlink-all` 退出码 0。**
>
> **Windows 复测指引（唯一遗留）**：在旧 Windows 机拉取本版跑
> `--test xlink`（预期 621/0）。若仍复现「Open 帧滞留」：抓 XTcpSocket
> 写缓冲水位与 bytesWritten 事件链（疑点：上传突发后写通知器失活），
> cdb 命令见下；同时确认 17-5 修正版全绿（排除测试侧残留干扰）。

**下表现象与排查记录保留作归档。**

**现象**：上传 37KB 全链路通过；随后发起下载，Open 帧（`open sent ... mode=1`，会话态 Ready、pending=0，即已完整写出到套接字）**从未到达服务端**（`xfs_onFileFrame` 无日志）；约 3 秒后服务端会话静默判线回收（`SILENCE delta≈3015`）。

**已排除**：
- 协议违例（服务端无 decode FAILED 日志）
- 背压（客户端 pending=0、服务端无 BACKPRESSURE）
- Goodbye（未触发）
- transferId 不匹配 / 无槽（服务端帧入口无日志 = 帧根本没被读出）
- 接收缓冲越界（已根修，段错误消失但下载仍不通）

**关键观察**：客户端 `XLinkSession_send` 返回 true 且 pendingBytes=0（数据已交给内核），但服务端**没读到任何字节**；期间服务端→客户端方向的心跳/帧正常。上传（同一条 TCP 反方向跑满 37KB）成功。怀疑方向：
1. 客户端 `XLinkFileClient_pump` 里显式调用的 `XLinkSession_pump`（与 XLinkClient 的内建定时器驱动**双驱动同一会话**）可能搅乱读缓冲/静默计时——`XLinkClient` 内部已有自己的驱动链，建议验证：把 `XLinkFileClient_pump` 里的会话 pump 去掉、只保留 `XLinkFileTransfer_pump` 再测；
2. 或下载 Open 帧写出到 socket 的时机与上传 Finish 的收尾排空（`XLinkClient_disconnectFromServer` 的 drain）存在交互——但测试中上传后并不断链，此假设优先级低；
3. 服务端 `xfs_preSessionPump` 钩子里的 keepAlive 会把 `m_lastReceiveTick` 置为当前值，可能与"测试等待循环里手动 pump 服务端"的时间轴交互产生判线放大——可尝试只在传输 Active 时续命、Opening 时不续命。

**建议续作顺序（2026-10-28 全部完成，唯余 Windows 复测）**：
1. ✅ 怀疑 1 双驱动：分析定论「顺序双泵今日无害」+ 单驱动门控加固（`setSessionDrive`/`willPumpSession`，分组十七·四/五锁定）；
2. ✅ 全量闸门 `--test xlink-all` 全绿（功能 621 + 质量 69，退出码 0）；
3. ✅ 本文档 §4/§5/§6/§8/§9/§11 已扩写至 v1.3-dev 并按终态校正；
4. ✅ demo（`XLinkDemoServer/Client`）已加登录 + 上传/下载剧情（单进程形态覆盖新剧情，随闸门验证）；质量门禁已补文件传输 churn/吞吐/会话统计三项；
5. ⬜ Windows 旧机复测（平台相关，指引见上方闭环结论；cdb 用法：`cdb -g -G -c "sxe -c \"kv;q\" av; g; q"`，注意 `-c` 与被调试程序参数之间不要加 `--`）。

### 14.4 新增文件清单

| 文件 | 职责 |
|---|---|
| `XLinkAuth.h/.c` | 认证载荷编解码、口令派生、挑战应答、常量时间比较、secureZero |
| `XLinkUserStore.h/.c` | JSON 账户库（盐/迭代摘要/角色/锁定，XString 用户名） |
| `XLinkFile.h/.c` | 文件协议编解码 + 文件名安全校验 |
| `XLinkFileTransfer.h/.c` | 收发状态机（流控/续传/校验/回调） |
| `XLinkFileServer.h/.c` | 服务端沙箱 + 策略 + 会话槽 + 三钩子挂接 |
| `XLinkFileClient.h/.c` | 客户端 upload/download/abort + 信号 |

修改：`XLinkProto.h`（能力位 Auth/FileTransfer、会话子码 5~8、GoodbyeReason、消息类 File=6、认证/文件载荷结构、版本 0x0101）、`XLink_config.h`（新增开关与容量宏）、`XLinkSession.h/.c`（发送队列、Authenticating 态、认证内核、文件钩子、keepAlive、统计、握手超时、**接收缓冲越界根修**）、`XLinkServer.h/.c`（认证集成、File 回调、三钩子）、`XLinkClient.h/.c`（凭据、自动重登、authFailed）、`Test/XProtocolTest/XLinkTest.c`（分组十六/十七）。
