# XModbus Hub 缓存层重构 — 交接单（2026-10-29）

> 用途：换机后从本文件接续。阅读顺序：本文件 → `XModbus聚合设备设计.md`
> 末节「2026-10-29 修订（缓存层重构）」→ `XModbusHub.h` 头注释。

## 0. 一句话状态

Hub 缓存层（每设备块缓存 + 读写指令合并 + 点级质量 + 写失败沿）**代码已全部落地并编译通过**；
既有回归（Hub 用例①~⑧、战役 S1..S13）**全绿**；~~新增缓存层用例⑨⑩仍有 12 项失败未收口~~
**→ 2026-10-29 换机续役已全部收口：用例①~⑩ 0 失败（RC=0），战役 S1..S13 ALL-PASS（io），
收口根因见 §5 各条「收口记录」。**

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
| **Windows MSVC** | **✔ 已构建通过（2026-10-29 换机续役，VS 2026 / `-G "Visual Studio 18 2026"`，`build-msvc`，0 error；产物 `bin/Debug/XModbusHubTest.exe`。Windows 无 pty 跑不了从站脚本，运行时验证以 WSL 为准）** |
| Hub 既有用例①~⑧ | **0 失败**（三设备上线 / 轮询回填 / 跨段读洞 / 写直通 / 写重发 / 宕机重连补写 / 映射刷新） |
| 战役 `XProtocolCampaignTest` S1..S13 | **0 失败**（io 引擎；含 S7 writeStalled 语义） |
| **新增用例⑨⑩** | ~~12 项失败~~ → **2026-10-29 换机续役收口：0 失败**（见 §5） |

## 5. 未收口的三条 → 【已全部收口 2026-10-29 换机续役】

### 5.1 写合并断言（⑩前半，5 项）✔ 已收口
现象：三笔相邻线圈写最终上的是逐点帧，`writeCompleted` 只回 2 条（`v=1 n=1`、`v=2 n=2`），不是一帧 FC15。
根因判断：**默认 `writeCoalesceMs=0` ⇒ 每笔写受理后立刻泵**，第一笔已上线，后两笔只能等它回。即「无窗口即下发」与「多笔合成一帧」本身矛盾。

**收口记录（修法 A：改用例不改实现）**：断言前 `setWriteCoalesceMs(40)`，窗口内三笔并成单条脏区间 → 一帧 FC15 [1..3]。另修正两处断言口径：
- FC15 起址断言 `start=0` → `start=1`（脏区间从 v1 起，0 从未写过）；
- `writeCompleted ≥3` → **恰 1 次**（`v=1 n=3`）：`hubEmitWriteDone` 契约粒度=映射段，单段内合并切片只回一次，原期望与「只上 1 帧」自相矛盾。

### 5.2 写落定 / 失败沿（⑩后半，6 项）✔ 已收口
现象：`pendingWriteCount` 不归零、解吞后也不落定 ⇒ FC06 写应答被判失败、无限重发。

**收口记录**：真根因在**从站脚本**——FC05/FC06 应答回 `pdu[:4]`（4 字节**含功能码**），而主站 `VXModbusClient_processResponse` 的 `respData` 已剥功能码，FC06 要求 4 字节净荷（addr2+val2），实收 3 字节 → 解析必败。交接单原结论「从站须回 `pdu[:4]`」**数错了字节**；已改为标准整请求回显 `pdu[:5]`（FC16 本就回 `pdu[:5]`，故既有用例全绿）。TCP `length`/`tid` 回填核对无问题。
**附带根修（真 bug）**：`hubSendNextWrite` 的 `writeStalled`/失败沿把**远端地址**当 `virtualStart` 发（违反两信号的头文件契约），远端≠虚拟映射下上层会把故障挂错点位——此前日志的神秘 `[SIG] v=5` 即用例⑧映射（远端 5）经 stdout 缓冲错位后的残影。新增 `hubDirtyVirtualStart()` 段表反查，`removeDevice` 的同款内联循环一并收敛。

### 5.3 帧日志断言（⑨后半，2 项）✔ 已收口
`gap=0 时远端 0 与 16 各自成帧`、`按需刷新确实发了一帧 FC04` 失败。

**收口记录**：真根因是**时序**而非基线残留——「值已回填」≠「帧已落盘」：gap=0 对照轮的值断言立即返回、按需刷新轮 v0 本就等于 7，CHECK 抢在从站把帧写进日志之前。新增 `PredFrameLog` 谓词 + `waitUntil` 带超时等日志行出现后再断言；`frameLogSize` 字节基线机制本身无恙（从站每帧 open-append-close，行原子）。

## 6. 复现环境（重要，都是实锅）

> **[2026-10-29 收编更新]** XModbusHubTest 独立 main 可执行已下线（同批：
> XProtocolCampaignTest / XVfdM512Test / XVfdBus485Test / XVfdAtv61Test），
> 改走主程序分派：`./bin/XinYueC_Static --test xmodbus-hub`（菜单「测试代码 →
> 协议栈 → XModbus → Hub聚合设备闭环」同源；`Test xmodbus-hub` 命令亦通）。
> 下方 run_hub.sh 模板中的 `./bin/XModbusHubTest 15331 15332` 相应替换为
> `./bin/XinYueC_Static --test xmodbus-hub`（仓库内 mbtest_*_run.sh 已改）。

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
5. WSL 与 Windows 是两份源码副本（`/root/xinyuec-src` ↔ 仓库根），改完必须同步，否则测的是旧代码。
6. **换机续役（2026-10-29，新机 D:\code\CMake\XinYueC）**：本机 WSL 发行版名是
   `Ubuntu-22.04`（非 `Ubuntu`）；副本重建用 `git archive HEAD | wsl -d Ubuntu-22.04 -u root
   -- bash -c "mkdir -p /root/xinyuec-src && tar -x -C /root/xinyuec-src"` 再补拷
   gitignore 的 `CMake_Config.cmake`（cmake 报 include 缺失即此因）。`wsl -- bash -c`
   传脚本会被外层展开 `$`，写文件走 base64 管道。二进制产出在仓库根 `bin/`
   （`CMAKE_RUNTIME_OUTPUT_DIRECTORY`），不在 `build-native/bin`。

## 7. Windows 侧现状（换机前最后状态）

- 工作区有大量未提交改动（`git status` 见下），其中**本战役**只涉及 §1 那张表的文件。
- `bin/` 下 `XinYueCd.dll`、`XGuiWindowDemo_Test.exe`、`XGuiRegression_Test.exe` 是 15:01~15:29 的构建产物，**不含**本次 Hub 改动（Hub 改动在 16:00 后落地）。
- 换机后建议顺序：~~`git stash`/提交当前改动 → 在 WSL 或 MSVC 里先编 `XModbusHubTest` → 跑 §6 的两套回归 → 按 §5 收口三条~~
  **→ 全部已于 2026-10-29 完成：§5 三条收口、WSL 两套回归全绿、MSVC 构建通过。剩余即提交。**

## 8. 与本战役相关的既往结论

- `industry-channel-bus-layer-gaps.md`：本文正是其**第 1 条（写失败不可见）**与**第 2 条（轮询节拍按从站）**的落地——块缓存让节拍可按「读组」组织，失败沿让通道层能进 Fault。
- `rk3568-wifi-page-recovery.md`：`XTimerId` = `XFd`，全局 128 槽。本次**没有**新增按点定时器（每设备仍 1 个 pollTimer + 1 个 retryTimer + 1 个 flush 窗口定时器），多点位不再吃槽。
