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

## 2026-10-28 修订（B1–B5：公共组件提炼 + 写插队 + 单元退避 + 写滞留可见性）

1. **B1 公共组件提炼**：新增 `Src/XProtocol/XPoll/XPollScheduler.{h,c}`——
   `XVfdPollScheduler` 语义原样上移为通用「总线轮询计划表」（登记/注销/改
   周期/markSent/reportResult 静默退避/tick 错峰单条派发/isMuted/setBackoff；
   纯逻辑、无定时器无 IO，时刻一律由调用方注入 `nowMs`）。
   **（2026-10-29 动态化）条目表由固定 32 槽改为 XVector 按需增长**：不再
   表满拒登（只剩分配失败），登记返回的下标只在登记时刻有效（删除走尾交换
   不保序）；tick 全程下标访问、回调前快照透传参数，回调内 register/
   unregister 安全；表体惰性建表，零初始化内嵌亦可直接登记。动因：32 槽
   对 >32 从站的非典型总线会静默丢退避簿记（Hub 侧 `(void)registerKey`），
   且 2 KiB/表的固定开销换不来可感知收益。另加 Hub 用的 **key 注册扩展**（`registerKey`/`reportResultKey`/
   `isMutedKey`/`isLostKey`：owner 为空、按 unitId 寻址，tick 自动跳过），
   供 (设备, 从站) 粒度失联簿记；同一张表内 owner 登记与 key 登记互不串位。
   配置开关 `XPOLL_SCHEDULER_ON` 登记进 `XProtocol_config.h`，缺省
   `= XMODBUS_HUB_ON || XVFD_ON`（宏惰性展开，与 include 顺序无关；
   `XProtocol_config.h` 同时引入 `XVfd_config.h` 保证 `XVFD_ON` 处处可见）。
   中文 Doxygen 全套 + UTF-8 BOM + extern C。
2. **B2 兼容壳**：`XVfdPollScheduler.h` 改为纯别名/宏映射壳
   （`typedef XPollScheduler XVfdPollScheduler` 等 + 全部函数名宏），删除
   `XVfdPollScheduler.c`（实现统一在 XPoll）。结构体大小与成员布局零变化，
   `XVfdLink` 内嵌实例 `m_sched` 与 `XVfdLink_Protected.h` 的
   `XVfdLink_scheduler` 返回类型不受影响；`XVfdLink.{h,c}` 一行未改。
3. **B3 写优先级**：`hubSendWriteIntent` 提交 `sendWriteRequest` 时传
   `XModbusRequestPriority_High`（轮询读保持 Normal）——即时下发与滞留重发
   共用该入口，一处收口，写在传输层队列正确插队。
4. **B4 失联退避 + 健康事件**：每设备内嵌一张 `XPollScheduler unitSched`
   （`XModbusHubDevice` 新字段），`hubRebuildAll` 重建计划时按段内 unitId
   去重重建退避表：
   - 退避粒度 = (设备, unitId)：RTU 共口多从站时，死从站静默期间其映射段
     不再提交（`hubPumpDevice` 选段时跳过 `isMutedKey` 命中的段；全静默则
     本拍让总线），不再占用单飞槽吃超时，把总线让给活从站；静默到期自动
     恢复探活（`isMutedKey` 随时刻自然解除，下一拍即重试）。
   - 阈值/时长可配：`XModbusHub_setUnitBackoff(maxFails, muteMs)`
     （>=1 / >=1000 生效；缺省 3 次 / 5000ms，对齐 `XVfdLink_setBackoff`
     风格），已建设备即时同步。
   - 新信号 `unitLost(deviceId, unitId)` / `unitAlive(deviceId, unitId)`：
     失败结果先经 `reportResultKey` 记账，按跃迁沿发射（进入静默且此前未
     失联 → unitLost；失联中收到首个成功应答 → unitAlive），静默期内不重复。
   - 既有 API 签名与语义零破坏（只增不改）；`pollOkCount`/`pollErrCount`
     语义不变（仍按实际提交的段计）；单飞语义不变（有在途应答跳过本拍）；
     计时全部 `XDateTime_currentMSecsSinceEpoch`，无新增定时器（沿用每设备
     `pollTimer` 节拍，风险最小；kill+复位配对纪律不变）。
5. **B5 写滞留可见性**：新信号 `writeStalled(type, virtualStart, count,
   attempts)`——写意图下发次数 `attempts` 首次达阈值发一次（每意图至多一次，
   `stallNotified` 护栏）；阈值经 `XModbusHub_setWriteStallNotice(attempts)`
   配置（缺省 3，<1 关闭）。只增可见性不改重发语义（失败仍静默留队周期
   重试直至成功）。`XModbusHubWriteIntent` 相应增加 `stallNotified` 字段。
6. **B6** `XModbusHub.h`、`XModbusMapping.h` 补 UTF-8 BOM。
7. 裁剪联动：`XPOLL_SCHEDULER_ON` 显式置 0 时须同时关 `XMODBUS_HUB_ON`
   （Hub 单元退避依赖该组件）。

## 2026-10-29 修订（缓存层重构：每设备块缓存 + 读/写指令合并 + 点级质量）

> 用户裁定：「缓存按需申请，其他按你的来」。目标是把 Hub 从「全局虚拟地址
> 连续缓存图 + 逐段单帧轮询 + 逐点写」升级为**按设备组织的缓存层**，让
> 一条 485 总线上的多点位、多从站场景把帧数压下来，同时补齐点级质量与
> 写失败可见性——这是 Industry 通道层（`Industry/XChannel`）接总线底座的前置。

### 一、数据模型（缓存坐标从虚拟地址改为远端地址）

| 结构 | 作用 |
| --- | --- |
| `XModbusHubCacheBlock` | 一块 = 同 `(寄存器区, 从站)` 下**远端连续**的若干映射段合并（块内无洞）；持有 `data`（`XModbusDataUnit`，位区自动 XBitArray）、`segs`（块内映射切片）、`dirty`（脏区间表）、`lastOkMs`/`failCount`/`refreshRequested` |
| `XModbusHubCacheSeg` | 块内切片 `{virtualStart, remoteStart, count}`——回填/写回显按切片拆段发 `valueChanged`，上层看到的粒度仍是「每条映射段一次」 |
| `XModbusHubDirtyRange` | 块内待写区间 `{offset, count, attempts, stallNotified, failNotified}`；重叠/相邻写入即合并（窗口内多次写只留最终值） |
| `XModbusHubReadGroup` | 合并后的轮询读帧 `{type, unitId, remoteStart, count, firstBlock, blockCount}` |
| `XModbusHubCacheSlice` | Hub 每类型一张按 `virtualStart` 升序的**视图切片表**（二分定位 → 块内偏移），读写都经它翻译虚拟地址 |
| `XModbusHubInflightWrite` | 在途写快照（远端绝对坐标 + 已发值数组）：在途期间上层再改同一点，收口按「缓存值 vs 快照值」逐点比较决定脏区间去留 |

**块内无洞是写安全的前提**：块由远端连续的映射段合并而来，`writeValues`
只接受「整段落在一个块内」的区间，跨块有洞一律拒绝（防把哨兵 0xFFFF
写进设备）。读则允许跨洞合并（见下）。

### 二、读路径

- **读组合并**：块表按 `(区, 从站, 远端起点)` 有序；同区同从站、相邻块
  远端间隔 ≤ `setReadMergeGap(gap)` 的并成一帧，再按单帧上限切分
  （寄存器区 125、位区 2000）。`gap` 默认 **0（不跨洞）**，现场建议 4~8：
  读洞只多几个字节，省一帧更划算。单块超上限时同块拆多组。
- **每设备一个周期定时器**（沿用旧节拍语义：`pollIntervalMs` = 组发送节拍），
  单飞：设备有在途应答则跳过本拍；收口**不**续泵（防轮询风暴，2026-10-26 根修语义不变）。
- `pollAreas` 掩码内的组才自动轮询；`requestRefresh(type, vStart, count)`
  可把任意映射块标 `refreshRequested` 供下一拍优先读（并按需即时泵一次），
  上层不必自己发 Modbus 事务。
- **回填**：一帧应答按组内各块的远端交集切分落块；变化检测按块内映射切片
  做，**首次回填（`lastOkMs==0`）恒发一次 `valueChanged`**——否则首采为 0
  的点永远收不到初值，违反 Industry 通道「挂接后至少推送一次供播种」契约。
- **轮询不覆写未确认写点**：回填逐点跳过脏区间内的点，避免乐观回显被同区
  轮询冲掉。
- `valueChanged` 粒度保持「每条映射段一次」，与旧契约一致。

### 三、写路径

```text
writeValue(s)
  → 虚拟→(设备, 块, 块内偏移) 翻译；整段须落同一块且无洞
  → 立即写块缓存（乐观回显，微秒级）+ 发 valueChanged
  → 标脏区间（相邻/重叠合并；attempts 与提示沿复位）
  → 返回 true（受理=内存写，不走总线）
  → 刷写：设备空闲时取首条脏区间，按 FC15/FC16 一帧下发
         （寄存器区单帧 ≤123、位区 ≤1968），超限按上限切分
```

- **写优先**：泵入口先写后轮询；写在传输层走 `High` 优先级插队（B3 语义不变）。
- **失败语义**：脏区间保留、按 `m_writeRetryIntervalMs` 周期重发；下发次数
  `attempts` 达阈值（`setWriteStallNotice`，缺省 3）时发一次
  `writeStalled` **与一次 `writeCompleted(..., ok=false)` 失败沿**（各自每
  意图至多一次）。失败沿是 Industry `XDigitalOutput_writeDone(false)` 的故障
  判据——旧版 Hub 只在成功时发信号，接通道后阀只能等满监督超时才进 Fault。
- **成功收口**：按快照逐点比较，相等的点从脏区间扣除（`hubDirtySubtract`
  支持中间挖空切两段），在途期间被改过的点重新标脏；随后按视图切片逐段发
  `writeCompleted(ok=true)`（上层分辨率仍是一条映射段一次）。
- **合并窗口**：`setWriteCoalesceMs(ms)`，默认 0（受理即下发，行为与旧版
  一致）；>0 时首笔写拉起一个单发窗口定时器，窗口内同块多次写只留最终值，
  到期统一刷写。`flush()` 可立即结束窗口。Industry 高频滑条建议 20~50ms。

### 四、点级质量

- 块级 `lastOkMs`（成功读回时刻）、`failCount`（连续读失败）支撑：
  - `valueAge(type, vAddr, &ageMs)`：命中映射返回 true；从未成功读回时
    `ageMs = UINT64_MAX`；
  - `pointValid(type, vAddr, maxAgeMs)`：命中且年龄 ≤ 门限。
- `lastUpdateMs(type)` 改为「该区全部块 `lastOkMs` 最大值」。
- `cacheCount(type)` 语义改为「该类型映射虚拟跨度」（仅跨度口径，实际数据
  按块存放；测试用法不变）。

### 五、API 变更一览

| API | 处理 |
| --- | --- |
| `value` / `values` / `writeValue` / `writeValues` | 签名不变，实现改走块缓存 + 视图翻译 |
| `cacheCount` | 语义=虚拟跨度（映射覆盖范围），不再是「连续缓存点数」 |
| `lastUpdateMs` | 语义=该类型块 `lastOkMs` 最大值 |
| `pendingWriteCount` | 改为「待写脏区间数（合并后）」 |
| `writeCompleted` | **新增 ok=false 失败沿**（原先恒 true） |
| 新增 | `setReadMergeGap` / `setWriteCoalesceMs` / `flush` / `requestRefresh` / `valueAge` / `pointValid` / `readGroupCount` |
| 删除 | `m_cache[4]` / `m_cacheStart` / `m_cacheCount`（不留双缓存，避免双写不一致） |

协议上限宏：`XMODBUS_HUB_WRITE_MAX_REGISTERS(123)` / `_BITS(1968)` /
`_VALUES(=BITS)`、`XMODBUS_HUB_READ_MAX_REGISTERS(125)` / `_BITS(2000)`。

### 六、验证状态（2026-10-29）

- **编译**：WSL 原生 gcc + aarch64 交叉均通过（`libXinYueC.so` / 静态库 /
  `XIndustryModbus_Test` / `XIndustryValve_Test` / `XModbusHubTest` /
  `XProtocolCampaignTest` 全部链接成功）。**Windows MSVC 尚未构建**。
- **既有 Hub 闭环回归（用例①~⑧）**：0 失败（三设备上线、轮询回填、跨段读、
  写直通、写重发、宕机重连补写、映射刷新全绿）。
- **战役回归 `XProtocolCampaignTest` S1..S13**：0 失败（io 引擎）。
- **新增缓存层用例（⑨读合并 / ⑩写合并+失败沿）**：已写入
  `Test/XProtocolTest/xmodbus_hub_test.c`（含帧日志断言工具
  `frameLogSize/frameLogCount/frameLogHasLine`；从站脚本
  `mbtest_hub_slaves.py` 配套新增 `frames_<tag>.log`、FC15 解包、
  数组放大到 di/coils=32 / ir/hr=64）。
  **已全部收口（2026-10-29 换机续役）：用例①~⑩ 0 失败**。三条根因与修法：
  1. **写合并断言（⑩前半，5 项）**——根因确认为「默认 coalesce=0 受理即
     下发」与「合并成一帧」矛盾（首轮判断成立）。按修法 A 改用例：断言前
     `setWriteCoalesceMs(40)`，窗口内三笔并成单条脏区间 → 一帧 FC15
     [1..3]。另修正两处断言口径：FC15 起址应为远端 **1**（脏区间起点，
     非 0）；`writeCompleted` 粒度=映射段（`hubEmitWriteDone` 契约），
     单段内合并切片只回 **1 次**（v=1 n=3），原「≥3」期望与设计矛盾。
  2. **写落定/失败沿（⑩后半，6 项）**——真根因在**从站脚本**：FC05/FC06
     应答回 `pdu[:4]`（4 字节**含功能码**），主站 `VXModbusClient_processResponse`
     的 `respData` 已剥功能码，FC06 要求 4 字节净荷（addr2+val2），实收
     3 字节 → 解析必败 → 无限重发。此前「从站须回 pdu[:4]」的结论数错了
     字节；已改为标准整请求回显 `pdu[:5]`。**附带根修**：
     `hubSendNextWrite` 的 `writeStalled`/失败沿把**远端地址**当
     `virtualStart` 发（违反两信号头文件契约），远端≠虚拟映射下上层会把
     故障挂错点位（此前日志里的神秘 `v=5` 即用例⑧映射的远端地址）；
     新增 `hubDirtyVirtualStart()` 段表反查统一三处发射口径。
  3. **帧日志断言（⑨后半，2 项）**——真根因是**时序**而非基线残留：
     「值已回填」≠「帧已落盘」，gap=0 对照轮与按需刷新轮的值断言立即
     返回，CHECK 抢在帧写日志之前。新增 `PredFrameLog` 谓词 +
     `waitUntil` 带超时等帧日志行出现后再断言，基线机制不变。
