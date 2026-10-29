# XModbus Hub 缓存层重构 — 交接单（2026-10-29）

> 用途：换机后从本文件接续。阅读顺序：本文件 → `XModbus聚合设备设计.md`
> 末节「2026-10-29 修订（缓存层重构）」→ `XModbusHub.h` 头注释。

## 0. 一句话状态

Hub 缓存层（每设备块缓存 + 读写指令合并 + 点级质量 + 写失败沿）**代码已全部落地并编译通过**；
既有回归（Hub 用例①~⑧、战役 S1..S13）**全绿**；**新增缓存层用例⑨⑩仍有 12 项失败未收口**
（用户 2026-10-29 要求停止调试先出文档）。

## 1. 改了哪些文件

| 文件 | 改动 |
| --- | --- |
| `Src/XProtocol/XModbus/XModbusHub.h` | **重写**：新增块/切片/脏区间/读组/在途写快照五组结构，删 `m_cache[4]` 系列，新增 7 个 API（见 §3），`writeCompleted` 增加失败沿语义 |
| `Src/XProtocol/XModbus/XModbusHub.c` | **重写**（约 2070 行）：块表重建/迁移、虚拟视图二分翻译、读组合并、脏区间合并与扣除、FC15/FC16 单帧打包、失败沿、质量查询、合并窗口定时器 |
| `Src/XProtocol/XModbus/XModbus聚合设备设计.md` | 追加末节（架构 + API 变更表 + 验证状态 + 已知未收口三条） |
| `Test/XProtocolTest/xmodbus_hub_test.c` | 追加用例⑨（读合并/点级质量/按需刷新）与⑩（写合并/窗口/失败沿）+ 帧日志断言工具 + 失败沿计数槽 |
| `Test/XProtocolTest/mbtest_hub_slaves.py` | 数组放大（di/coils=32、ir/hr=64）、新增 `frames_<tag>.log` 帧日志、新增 FC15 解包、`main()` 清帧日志 |
| `Industry/XChannel/XGpioDigitalInput.c`、`XGpioDigitalOutput.c` | 修 `Set_Class_Memory(obj, XFree_System)` → `Set_Class_IsHeap(obj, true)`（原先**编译不过**，函数指针喂给 `XMemoryType` 形参） |

未提交 git（工作区脏，随用户统一提交）。

> **注意**：工作区里 `Src/XProtocol/XModbus/XModbusRtuSerialServer.c` 的「即时兑现
> 帧」改动**不属于本战役**（来自并行的 485 轮询延迟那条线），本战役一行未动；
> 提交时按文件分别归入对应提交。

## 2. 架构要点（改了什么语义）

1. **缓存坐标 = 远端地址**，一块 = 同 `(寄存器区, 从站)` 下**远端连续**的映射段合并（块内无洞）。上层 API 仍是虚拟地址，经每类型一张有序视图表二分翻译。
2. **读**：读组合并（`setReadMergeGap`，默认 0 不跨洞），单帧上限寄存器 125 / 位 2000；每设备一个周期定时器 + 单飞；**收口不续泵**（保留 2026-10-26 防风暴语义）。
3. **写**：受理 = 写块缓存（乐观回显）+ 标脏 + 返回 true；空闲时取首条脏区间按 **FC15/FC16 一帧** 下发（单帧 ≤1968 位 / ≤123 寄存器）；失败留脏周期重发；attempts 达阈值发一次 `writeStalled` **和一次 `writeCompleted(ok=false)`**。
4. **写合并窗口** `setWriteCoalesceMs(ms)`：默认 **0 = 受理即下发**（与旧版一致）；>0 时首笔拉起单发窗口定时器，窗口内多次写只留最终值。
5. **点级质量**：`valueAge` / `pointValid` / `requestRefresh` / `readGroupCount`；`cacheCount` 改为「虚拟跨度」口径；`lastUpdateMs` 改为块级最大 `lastOkMs`。
6. **重建不丢质量**：`hubMigrateBlockState` 迁值 + `lastOkMs` + `failCount`（本轮已修）。

## 3. 新增 API

```
void     XModbusHub_setReadMergeGap(hub, uint16_t gap);      // 读洞合并容限，默认 0
void     XModbusHub_setWriteCoalesceMs(hub, size_t ms);      // 写合并窗口，默认 0
void     XModbusHub_flush(hub);                              // 立即结束窗口并刷写
bool     XModbusHub_requestRefresh(hub, type, vStart, count);// 按需读（不必自己发事务）
bool     XModbusHub_valueAge(hub, type, vAddr, uint64_t* ageMs);
bool     XModbusHub_pointValid(hub, type, vAddr, maxAgeMs);
int      XModbusHub_readGroupCount(hub, deviceId);           // 诊断/回归
```
宏：`XMODBUS_HUB_WRITE_MAX_REGISTERS(123)` / `_BITS(1968)` / `_VALUES`、
`XMODBUS_HUB_READ_MAX_REGISTERS(125)` / `_BITS(2000)`。
`writeCompleted(type, vStart, count, ok)`：`ok=false` 是**新增失败沿**（每意图至多一次，仍继续重发）。

## 4. 验证状态（精确）

| 项 | 结果 |
| --- | --- |
| WSL 原生 gcc 编译（`build-native`） | 通过（仅全库同款 `XVector_size_base` 指针类型警告） |
| aarch64 交叉（`build-x`） | `libXinYueC.so`、静态库、`XIndustryModbus_Test`、`XIndustryValve_Test`、`XModbusHubTest`、`XProtocolCampaignTest` 链接成功；`out-arm` 另有既有 `__isoc23_scanf` 工具链坑（与本改动无关） |
| **Windows MSVC** | **未构建**（换机后请先跑一次） |
| Hub 既有用例①~⑧ | **0 失败**（三设备上线 / 轮询回填 / 跨段读洞 / 写直通 / 写重发 / 宕机重连补写 / 映射刷新） |
| 战役 `XProtocolCampaignTest` S1..S13 | **0 失败**（io 引擎；含 S7 writeStalled 语义） |
| **新增用例⑨⑩** | **12 项失败（未收口）** |

## 5. 未收口的三条（接着修从这里开始）

### 5.1 写合并断言（⑩前半，5 项）
现象：三笔相邻线圈写最终上的是逐点帧，`writeCompleted` 只回 2 条（`v=1 n=1`、`v=2 n=2`），不是一帧 FC15。
根因判断：**默认 `writeCoalesceMs=0` ⇒ 每笔写受理后立刻泵**，第一笔已上线，后两笔只能等它回。即「无窗口即下发」与「多笔合成一帧」本身矛盾。
两个修法（择一，前者不改语义）：
- **改用例**：断言写合并时先 `setWriteCoalesceMs(40)`，或先让设备处于在途状态再连写；
- **改实现**：给「同一拍内的写」加一次微任务延迟（例如把首次泵改到 `XTimer_singleShot1(0, ...)`），使同拍写入合并后再发。

### 5.2 写落定 / 失败沿（⑩后半，6 项）
现象：`pendingWriteCount` 不归零、解吞后也不落定 ⇒ FC06 写应答被判失败、无限重发。
已核：`XModbusClient.c:820` 起，FC05/FC06 响应解析按「respData **不含功能码**、4 字节」；从站脚本须回 `pdu[:4]`（现在是 `pdu[:4]`）。
下一步：在 `XModbusHub_onReplyFinished` 打 `XModbusReply_error(reply)` 实际值；查 TCP 从站回帧的 `length`（应 = PDU 长度 + 1）与 `tid` 回填；确认 `store.respond()` 的 `self.lock` 未与 `frame_log` 文件 IO 冲突（`frame_log` 在锁内做磁盘写，9600 下不明显但可挪到锁外）。

### 5.3 帧日志断言（⑨后半，2 项）
`gap=0 时远端 0 与 16 各自成帧`、`按需刷新确实发了一帧 FC04` 失败。
原因：`frames_A.log` 跨轮累积，`frameLogSize()` 用字节偏移做基线；从站只在 `main()` 清一次日志，若同一从站进程被复用（本轮跑 15 个用例），基线可跨越上一轮内容。
修法：断言前用 `ftruncate` 清空，或让从站在每次 `rtu_slave`/监听重建时清；也可把断言改成「窗口内出现且只出现一次指定起始地址的帧」。

## 6. 复现环境（重要，都是实锅）

WSL 侧（Ubuntu，root）：

```sh
cd /root/xinyuec-src && cmake -S . -B build-native -DCMAKE_BUILD_TYPE=Debug -DXINYUE_CROSS_COMPILE=OFF
cd build-native && make XModbusHubTest -j8
sh /root/run_hub.sh          # 内含 setsid 起从站 + 跑测试
```

`run_hub.sh` 内容（Windows 侧也有一份 `%TEMP%\run_hub.sh` 模板）：

```sh
#!/bin/sh
cd /root/xinyuec-src || exit 2
for pid in $(pgrep -f "mbtest_hub_slaves.py"); do kill -9 "$pid" 2>/dev/null; done
sleep 0.6
rm -rf /tmp/mbsoak/hub; mkdir -p /tmp/mbsoak/hub
setsid nohup python3 Test/XProtocolTest/mbtest_hub_slaves.py 15331 15332 > /root/slaves.log 2>&1 < /dev/null &
i=0
while [ "$i" -lt 60 ]; do
  if [ -s /tmp/mbsoak/hub/rtu_pty_path ] && ss -tln | grep -q ":15331 " && ss -tln | grep -q ":15332 "; then break; fi
  sleep 0.1; i=$((i + 1))
done
export XNET_FORCE_EPOLL=0
timeout 180 ./bin/XModbusHubTest 15331 15332 > /root/hubtest.log 2>&1
echo RC=$?
for pid in $(pgrep -f "mbtest_hub_slaves.py"); do kill -9 "$pid" 2>/dev/null; done
```

战役回归：

```sh
cd /root/xinyuec-src && sed -i "s/\r$//" Test/XProtocolTest/mbtest_campaign_run.sh Test/XProtocolTest/mbtest_hub_run.sh
REPO=/root/xinyuec-src CAMPAIGN_BASE=/tmp/mbsoak/campaign1028 bash Test/XProtocolTest/mbtest_campaign_run.sh io
```

五个环境坑：
1. 从站必须 `setsid nohup ... < /dev/null &`，否则命令结束就被带走（表现为整轮 connect 失败）。
2. 仓库里的 `mbtest_*_run.sh` 是 CRLF，`sh` 会报 `$'\r'`，跑前 `sed -i "s/\r$//"`。
3. `pkill -f "mbtest_hub_slaves.py"` 会杀掉自己那条 `sh -c`；用 `pgrep` 取 PID 再 kill。
4. `wsl -d Ubuntu -u root -- /root/x.sh` 会被 Git Bash 路径转换吃掉 → 加 `MSYS_NO_PATHCONV=1` 或写 `-- sh /root/x.sh`。
5. WSL 与 Windows 是两份源码副本（`/root/xinyuec-src` ↔ `F:\Code\CMake\XinYueC`），改完必须 `cp` 同步，否则测的是旧代码。

## 7. Windows 侧现状（换机前最后状态）

- 工作区有大量未提交改动（`git status` 见下），其中**本战役**只涉及 §1 那张表的文件。
- `bin/` 下 `XinYueCd.dll`、`XGuiWindowDemo_Test.exe`、`XGuiRegression_Test.exe` 是 15:01~15:29 的构建产物，**不含**本次 Hub 改动（Hub 改动在 16:00 后落地）。
- 换机后建议顺序：`git stash`/提交当前改动 → 在 WSL 或 MSVC 里先编 `XModbusHubTest` → 跑 §6 的两套回归 → 按 §5 收口三条。

## 8. 与本战役相关的既往结论

- `industry-channel-bus-layer-gaps.md`：本文正是其**第 1 条（写失败不可见）**与**第 2 条（轮询节拍按从站）**的落地——块缓存让节拍可按「读组」组织，失败沿让通道层能进 Fault。
- `rk3568-wifi-page-recovery.md`：`XTimerId` = `XFd`，全局 128 槽。本次**没有**新增按点定时器（每设备仍 1 个 pollTimer + 1 个 retryTimer + 1 个 flush 窗口定时器），多点位不再吃槽。
