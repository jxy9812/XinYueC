# XModbus 聚合设备（XModbusMapping + XModbusHub）设计

> 2026-10-26 立项。目标：把多个 Modbus 从站（RTU 串口 / TCP 网口混合）虚拟成
> **一个统一地址空间的大设备**，输入区定时自动轮询缓存、输出区按需写入失败
> 重发成功即停、底层断线自动重连对上层透明。用户全程不感知底层传输方式。

## 两个类，职责正交

| 类 | 职责 | 不负责 |
| --- | --- | --- |
| `XModbusMapping`（映射管理器） | 统一虚拟地址 ↔（设备, 从站号, 远端类型, 远端地址）的翻译；段表管理与重叠校验；一次虚拟读/写到多个底层段的拆分（切片） | 任何 IO、任何定时器 |
| `XModbusHub`（聚合设备） | 持有 N 个底层 `XModbusClient*`；注入 `XModbusMapping`；输入区定时轮询并回填统一缓存；输出写按需下发 + 失败重发成功即停；聚合各设备断线重连状态并转发信号 | 协议帧、传输层（全部复用既有 XModbusTcpClient / XModbusRtuSerialClient） |

## 关键决策

1. **Hub 是组合器不是协议设备**：继承 `XObject`（需要信号），**不继承**
   `XModbusDevice/Client`——虚拟设备不需要发起 Modbus 事务的虚表，若日后要被
   `XModbusServer` 对外服务，另加薄适配器，不往 Hub 里塞。
2. **传输无关**：Hub 只依赖 `XModbusClient` 基类虚表（`sendReadRequest /
   sendWriteRequest / state`），TCP/RTU 由调用方构造后 `addDevice` 注入，
   **所有权转移给 Hub**（析构统一 deleteLater）。
3. **断线重连复用既有机制**：每设备 `setAutoReconnect(true)`，Hub 经
   `stateChanged`（connect_2，第一参=真 sender=client）跟踪每设备状态并转发
   `deviceStateChanged`；设备恢复 Connected 时自动恢复其轮询与补发滞留写。
4. **输入区（DI/IR）自动轮询**：每设备一个周期定时器（间隔可按设备覆盖，
   0=不轮询）；每拍按该设备的映射段计划逐段 `sendReadRequest`，应答经映射
   回填统一缓存并发 `inputUpdated`。**单飞原则**：该设备有在途应答则跳过本拍
   （对齐单事务从机语义，防堆积）。默认只轮询输入区（效率最大化——输出区
   状态以本地写回显为准，不花流量读回）；`setPollAreas` 可放宽。
5. **输出区（Coils/HR）按需写 + 重发**：`writeValue(s)` 先乐观更新本地缓存，
   再入重发队列下发；失败/超时/设备离线则滞留队列，重发定时器周期重试，
   **收到成功应答即出队停发**并发 `writeCompleted(ok)`。设备离线期间的写
   在重连后自动补写。写意图必须完整落在单条映射段内（跨段写 v1 拒绝）。
6. **统一缓存**：attach 映射时按每类型的虚拟地址跨度分配 4 个
   `XModbusDataUnit`（位区 XBitArray 紧凑、寄存器区 2B/点）；读写统一走
   `value/values`，输入区值=轮询回填，输出区值=本地写回显。
7. **线程模型**：与库一致的单线程事件循环，零锁。计时一律
   `XDateTime_currentMSecsSinceEpoch()`（时间源规则）。

## 虚拟地址空间示例

```
统一空间(DI)          底层
[0..7]   ──────────▶ dev0(TCP 117:502, unit1).DI[0..7]
[8..15]  ──────────▶ dev1(RTU /dev/ttyS2, unit5).DI[0..7]
统一空间(HR)
[0..9]   ──────────▶ dev2(TCP 10.0.0.3:502, unit1).HR[100..109]
```

一次 `writeValue(HR, 5)` 翻译为 dev2 的 HR[105]；一次跨 dev0/dev1 的
`values(DI, 4, 8)` 读由 `resolveSegments` 切成两段分别取自缓存。

## 复用清单（不重复造轮子）

`XModbusClient`（基类虚表+超时+自动重连+单事务队列）/ `XModbusTcpClient` /
`XModbusRtuSerialClient` / `XModbusDataUnit`（缓存与请求单元直接用它）/
`XModbusReply`（finished 信号驱动回填）/ `XVector`（段表/重发队列）/
`XObject` 定时器与信号槽。

## 裁剪开关

`XModbus_config.h` 新增 `XMODBUS_MAPPING_ON`、`XMODBUS_HUB_ON`
（默认随 `XMODBUS_CLIENT_ON`）。

## 闭环验证（2026-10-26 深夜）

`Test/XProtocolTest/xmodbus_hub_test.c`（XModbusHubTest 目标）+
`mbtest_hub_slaves.py`（2×TCP 从站 + 1×RTU pty 虚拟串口从站）+
`mbtest_hub_run.sh`（双引擎驱动）：**io_uring / epoll 双引擎 62/62 全绿**，
ASan 零报告。覆盖：映射纯逻辑（重叠/切片/按设备收集/回绕拒绝）、三设备
上线、输入区轮询回填与改值跟上、跨段读与洞、输出写直通（TCP 线圈 + RTU
寄存器）、写重发（pause 吞帧→恢复补写）、宕机重连+滞留写补发、映射刷新。

## 已知缺口（待办）

1. ~~**串口异步收**~~ **【2026-10-26 已根修】**：`XSerialPort` 现自带
   **RX 轮询定时器**（默认 2 ms，`XSerialPort_DefaultRxPollIntervalMs`），
   每拍检查 `BytesAvailable > 0` 即发射 `XIODevice_readyRead_signal`；
   RTU 客户端原已连该信号，无需改动即恢复收帧。API：
   `XSerialPort_setRxPollInterval(port, ms)` / `XSerialPort_rxPollInterval(port)`
   （≤0 关闭轮询）。
   实现落点：`Src/XIO/XSerialPort/XSerialPort.{h,c}`（RX 定时器字段 +
   `VXSerialPort_timerEvent` + open/close/deinit 生命周期挂钩）。
   选择"轮询"而非"dispatcher fd 事件"的理由：socket-notifier 机制面向
   socket fd，io_uring/epoll 双引擎对 tty fd 可读事件支持不齐；轮询在
   POSIX / Windows / Android-NDK 三平台行为一致。
   **遗留**：轮询带来每 2 ms 一次系统调用的固定开销（多路串口可调至 5 ms）；
   通用串口"fd 可读事件"方案仍可作为后续优化。
2. ~~RTU 轮询节拍实测依赖上条~~ **【2026-10-26 已闭环】**：RTU 读回填已在
   桌面 pty 从站上实测闭环（`Test/XProtocolTest/mbtest_atv61_slave.py` +
   `xmodbus_atv61_test.c`，io/epoll 双引擎 0 失败）。

## 调试过程中顺手根修的预存缺陷

1. **`XModbusTcpClient_sendRawRequest` Ref 契约 UAF**（ASan 实证）：失败路径
   `deleteLater(reply)` 后仍持 `m_request`，调用方按契约立即释放请求 →
   延迟删除二次释放。修=失败时先 `reply->m_request = NULL` 再异步删。
2. `XModbusMapping`/Hub 缓存的**枚举-稠密下标混用**（`XModbusInvalid=0` 使
   四类区实值 1..4，直接当数组下标对 HR=4 越界一字）——统一入口
   `mappingTypeIndex`/`hubTypeIndex` 转稠密，出口 `hubEnumOfType` 转回。
