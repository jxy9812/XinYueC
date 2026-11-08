# XinYueC 项目工作约定（给 AI 助手的常驻记忆）

## 铁律：框架有的，绝不手搓

**本项目框架（Src/XCore 等）已经提供并测试过的基础组件，一律直接复用，
禁止在业务/模块代码里手写重复实现。** 动手写任何队列/缓冲/映射/字符串
容器之前，先查框架是否已有，找到就用在装配处。

已知常被重复造轮子、实际框架现成的组件（不完全清单，动手前先 grep Src/XCore）：

| 需求 | 用框架的 | 位置 |
|---|---|---|
| 环形缓冲/字节环 | `XRingBuffer` | `Src/XCore/XContainer/XRingBuffer/` |
| 定容循环队列（FIFO，值语义） | `XCircularQueue`（`_init(typeSize, count)` / `_push` / `_receive` / `_pop` / `_count`） | `Src/XCore/XContainer/XQueue/XCircularQueue/` |
| 无锁队列 | `XLockFreeQueue` | 同上 XQueue/ |
| 优先队列 | `XPriorityQueue` | 同上 XQueue/ |
| 动态数组 | `XVector` | XContainer/XVector/ |
| 字节数组 | `XByteArray` | XContainer/XByteArray/ |
| 键值映射 | `XMap` / `XHashMap` | XContainer/XMap/ |
| 集合 | `XSet` / `XHashSet` | XContainer/XSet/ |
| 位图 | `XBitArray` | XContainer/XBitArray/ |
| UTF-8 字符串 | `XString`（**取 UTF-8 用 `XString_toUtf8`，不要用 `XString_constData`——那是内码（unicode）缓冲**） | XContainer/XString/ |
| JSON / BSON | `XJsonDocument` / `XBsonDocument`（互转走 `XBsonDocument_toJson/fromJsonObject`） | XData/ |
| 文件读写 | `XDeviceFile`（open/read/write/stat/close；参考 XLinkUserStore.c 的完整套路） | XDevice/ |
| 对象/信号槽/定时器 | `XObject` / `XSignal` / `XObject_startTimer_ms` | XClass/ XCode/XEvent/ |

手写实现的教训（2026-11-05 实锅）：XAutoController 报警队列手写了环形数组，
回绕用了"越界清零"而非取模，head+count 越过 2×容量后插入位错乱、覆盖队尾
元素——框架的 `XCircularQueue` 早就是对的。

## 其它工程约定

- **Python 参考实现对齐**：HighSpeedFilter 的 C 服务端对齐
  `HighSpeedFilter/server_py`（现场模拟口径以
  `server_py/run_server_sim.bat` 即 `python -m hsfsrv --sim --port 6200`
  为准：阀 10s 行程/20s 超时、泵 DO22 仅输出无反馈、浊度仪站 1/3
  约 12.5/3.2 NTU、流量计 36.5 m³/h ±2%、变频器爬坡按 ACC/DEC 满量程
  时间（0xF011/0xF012 缺省各 10.0s ⇒ 5Hz/s）、运行中换向＝先减速过零
  再反向加速、运行中禁点动（服务端闸拒绝+两端按钮置灰，点动保持中的
  心跳例外）——两端模型 `XVirtualModelVfd`/`VirtualVfd` 同口径，改一侧
  必须同步另一侧与 `xmodbus_virtual_test.c` ①b/`test_units.py`
  TestVfdReversal 两组镜像用例）。
- **两端协议常量**：C 侧 `HighSpeedFilter/common/XhsfLinkProto.h` 与
  Python 侧 `server_py/hsfsrv/proto.py` 必须逐值同步，注释互相点名。
- **MSVC 编译注意**：源文件 UTF-8；向 C 字符串写 `\n` 时严禁在 bash
  heredoc 里转义（会被吃掉变成真实换行导致 C2001）——用脚本文件或
  chr(92) 拼接。
- **XString 是内码缓冲**：跨进程序列化/拷贝一律 `XString_toUtf8`。
- 构建入口：`build_hsf.bat`（HsfServer/HsfClient，build-hsf/）；
  测试宿主 `XinYueC_Static`（out/build/x64-Debug），跑法
  `bin\XinYueC_Static.exe --test <命令>`。
  **所有 MSVC 构建必须经 `vcvars64.bat`**（`/tmp/build_reg.bat`、
  `build-hsf/build_static.bat` 都是这个套路）；直接在 Git Bash 裸调
  `cmake --build` 会因缺 `INCLUDE` 报 `stdbool.h`/`stdint.h` C1083。
  `XGuiRegression_Test` 与 `XinYueC_Static` 在 `out/build/x64-Debug`，
  `HsfServer`/`HsfClient` 在 `build-hsf/`，两套互不覆盖，可分别重编。
- **HSF 跨端联调（改服务端快照键/字段名/设备类别必跑）**：
  `bin\HsfServer.exe --sim`（默认 6100）+
  `QT_QPA_PLATFORM=offscreen <python> HighSpeedFilter/client_py/tests/test_client.py --auto 127.0.0.1 --port 6100`
  （需 PyQt5；本机可用 `D:\anaconda3\python.exe`）。
  VFD 组合链路：`tests/test_vfd_combo.py --port 6100`。
  连续两次连接间隔 ~8s（DenyNew 下旧会话按静默超时回收，立即重连会被拒）。
  已修复的 C/Python 口径：健康快照必须含进水泵/数字量/电磁阀组条目；
  数字量 DO6 名必须是"进水泵"、阀通道名为"<阀名> 控制/开到位/关到位"
  （2026-11-06 .12 聚合器拆除、点表收窄为 16 点：阀 1~6=DO0~DO5、
  进水泵=DO6、按钮组=DO7~DO9；旧口径 DO21/DO22 是聚合编号，已作废）。
  停机斜坡期运行位必须报 False（「正在停机」口径，两端一致）；
  客户端重连后必须重发订阅帧（xlink 去重集合随连接作废，2026-11-07 修）。
- **会话压测 / 泄漏回归（改会话、订阅、登录或服务端 per-session 状态必跑）**：
  `tests/test_stress_reconnect.py --port 6100 --clients 8 --cycles 1250`
  （8 进程 × 1250 轮 = 1 万次连接→登录→订阅 9 主题→快照→断开）。
  环境要求：脚本会**自动**经远端配置（`CMD_CONFIG_GET/SET` 的 `login` 节）
  打开同账并发，收尾按原值恢复（`--no-policy` 可跳过）；手工压测时也可在
  「远端设置 → 登录策略（账号）」里改（管理员），或写
  `bin/cfg/hsf_login_policy.json`（服务端落盘的策略文件）。
  跑完须回缺省，否则 `test_client.py` 的权限/策略用例口径不对。
  泄漏判读用堆计数（`HSF_HEAP_TRACE=1` 服务端 / `XHSF_HEAP_TRACE=1`
  客户端，5s 一条 `[heap]`）；2026-11-08 实测 2 万次会话 0 失败、
  服务端堆回到 ~702KB 基线、句柄第二轮 0 增长。台账：
  `docs/HSF会话压测与泄漏回归_实施记录_20261108.md`。

## 文件修改工具铁律：禁止用 Python 脚本改源码

**修改本仓库任何文件一律用编辑工具（Edit/Write）直接编辑，严禁写 Python
脚本做字符串替换来"批量改文件"。**（2026-11-07 实锅：流量计页布局调整用
Python 脚本链连续改了五六次，转义/行尾/锚点匹配层层出错，多行补丁互相
踩踏把布局结构弄丢，页面直接改废，最后只能 git 回滚重来。）

- Edit 工具自带精确匹配校验（old_string 不唯一/不存在会报错）、所见即所得，
  改错立刻可见；
- Python 脚本的问题：`\x` 转义被 bash heredoc 二次吞改、CRLF/LF 混合文件
  匹配失败、失败后无感知继续跑后续补丁、多处 replace 相互叠加不可追溯；
- **确需批量处理**（如全库转义还原）时：脚本只做**只读扫描与报告**，实际
  修改逐文件用 Edit 工具人工确认后落地；或脚本改完立即 `git diff` 全文
  审查 + 编译验证，发现结构损伤当场回滚。

## 所有权命名铁律（_move / _ref / _const / _copy）

详细契约见根目录《代码风格，类的创建，虚函数的重载注意，api命名风格和注意事项.md》
「`set*` 三态所有权契约」「获取函数的所有权契约」。日常必须记住：

| 形态 | 后缀 | 调用方义务 |
|---|---|---|
| 拷贝 | 无后缀 / `_copy` | 原对象仍归调用方，**必须自行释放** |
| 借用 | `_const`；键查询 `value_keyUtf8`（显式例外） | **禁止释放**；随容器修改/析构失效 |
| 移动 | `_move` | 内容已转走，**空壳仍由调用方按分配方式释放** |
| 接管 | `set*_ref` / `insert*_ref` | 调用即失所有权（失败也由被调方释放；NULL 入参不接管） |
| 借用包装 | `to*_ref` / `from*_ref` | 非拥有包装；删除包装前把 `m_data` 置 `NULL` |

- **插入接口同样适用三态**：`insert_*`（拷贝）/ `insert_*_move`（内容转移留壳）/
  `insert_*_ref`（指针接管零分配）。`_move` 后不要忘记释放源壳。
- **JSON/BSON 同名必须同义**：`XJsonObject_value_keyUtf8` 与
  `XBsonDocument_value_keyUtf8` 都是**借用**（缺失 `NULL`，不得释放）；
  需要独立生命周期用两库同名的 `value_copy_keyUtf8`（堆拷贝，必须释放）。
  改一处必须同步另一处与 `Test/XDataTest/XJsonBsonCoverageTest.c`。
- **`*_init(type)` 只设类型标签**：JSON/BSON 值类型的 `init` 不分配容器载荷；
  需要可直接用 `toDocument/toArray/toString` 的空容器时用 `create(type)`
  或显式 `set*`。`move` 后源类型清零（JSON `Invalid` / BSON `type 0`）。
- 新增 `_move`/`_ref` 接口必须成套提供（copy/move/ref），头文件写清
  `@warning`，并补 `--test xjson-bson` 场景（含泄漏回基线断言）。

## 延迟删除（deleteLater）已对齐 Qt：禁止自造延迟队列

**`XObject_deleteLater` 在事件循环里是好的，不要怀疑投递机制。**
2026-11-08 实锅：排查 socket 滞留时误判「同层投递永不生效」，在
`XLinkServer` 加了 `m_graveSockets` 队列、`XLinkClient` 加了 `m_graveSocket`
槽，两套自造机件共约 90 行。复核框架实现后确认判断是错的，已全部撤除，
改回 `abort + deleteLater` 两个原语。

投递机制（`XAbstractEventDispatcher_processEvents` +
`XEvent.c` 的 `XDeferredDeleteEvent_shouldDeliver`）：判据是
`eventLevel > currentLevel`，看似同层不投递；但 `notifyInternal2`
（`XCoreApplication.c`）在**派发事件期间自增 `scopeLevel`**，所以
「槽函数里 post 的删除事件」层号比 `processEvents` 顶层大 1，满足 `>` ⇒
下一轮派发时正常投递。这与 Qt 6.8 `qcoreapplication.cpp:1904-1912` 的
`allowDeferredDelete` 逐条同构（`>` + 显式请求 + 外层循环三个分支）。

**排查纪律：删除没生效，先查调用点的所有权账，不要先怀疑投递。**

**同类红线：`VXObject_deinit` 的 threadData 清槽位必须在末尾，不能提前。**
2026-11-08 实锅：为修 HsfClient 退出时 `XThreadData_deref` 读脏指针，
把 `store(m_threadData, NULL)` 从 `VXObject_deinit` 末尾挪到了开头。
deinit 中段 `XObject_thread(object)` / `XObject_eventDispatcher(object)`
都经 `m_threadData` 取线程亲和性，槽位被提前清空 ⇒
`XCoreApplication_removePostedEvents` 整段跳过 ⇒ posted 队列残留悬垂
`m_receiver` ⇒ 下一次 `processEvents` 读 `m_is_deleting_children`
（`XAbstractEventDispatcher.c:700`）AV，`XGuiRegression_Test` 稳定段错误。
正确做法（对标 Qt 6.8.3 `qobject.cpp:184-206` `QObjectPrivate::~QObjectPrivate`）：
**开头只 load 快照到局部变量**（守住退出脏指针修复），
**store NULL + deref 一并留到函数末尾**；Qt 整个析构根本不清槽。
台账：`docs/HSF会话收尾与两项回归根修_实施记录_20261108.md` §一.1。

**同类红线：临时诊断探针必须零残留。** 用 env 门控的自测钩子
（`XHSF_HIST_WHEELTEST` / `XHSF_HIST_AUTOTEST` 之类）验收完必须整段删除，
连字段、自测专属 include 一起清；**非 env 门控的调试打印**
（如 `[vfd-trend]` 每 10 拍一条）更是生产常驻污染。
全库扫描口径：`grep -rnE '\[hist-|\[vfd-trend\]|XHSF_HIST_|s_traceCnt'
 --include='*.c' --include='*.h' HighSpeedFilter/ Src/`。
正式诊断路径保留：`HSF_HEAP_TRACE` / `XHSF_HEAP_TRACE` /
`XGUI_*_PROF` / `XHSF_PAINT_TRACE`（有文档、env 门控）。

| 症状 | 真正常见的原因 | 正确做法 |
|---|---|---|
| 对象/连接回收后内存不降 | 取到了**返回副本**的 getter 却没释放 | 见下表；整表遍历必须用 `_const` 借用版 |
| 想把「回收时机」推迟一拍 | 其实只是缺 `close/abort`（引擎没关） | `XAbstractSocket_abort()` 后再 `deleteLater` |
| 怀疑 deleteLater 不投递 | 几乎总是误判 | 先按本表核对所有权，再考虑机制 |

本轮实测确认的三处**返回副本必须释放**的接口：

| 接口 | 语义 | 漏掉的后果（实测） |
|---|---|---|
| `XSqlQuery_record(q)` | 返回**新记录**所有权（`XSqlQuery.h` 明写） | 每次查询漏 ≈1KB（单字段记录）/ ≈7.5KB（18 字段行记录） |
| `XSqlRecord_value(rec, i)` | 返回 `XVariant_create_copy` 堆副本 | 20 行 × 18 列 = 360 个副本 ≈153KB/次；整表遍历改用 `XSqlRecord_value_const` |
| `XBsonDocument_insert_keyUtf8_document(doc,k,sub)` | **深拷贝**，`sub` 仍归调用方 | 每调用一次漏 ≈13,580 字节 |

另外 `XBsonDocument_insert_keyUtf8_array_move` 只转移**内容**，源数组/值对
象的**壳**仍要 `XClassDelete`（与 `insert_*_move` 的三态一致）。

### 泄漏结论必须用堆计数下，不能用工作集

`tasklist` / `Get-Process` 的工作集有 ~200KB 抖动、且受页回收影响，判不出
几 KB ~ 几十 KB/轮的泄漏。判泄漏一律用框架自己的分配器计数
（`XMemory_statistics_2(XMEMORY_TYPE_SYSTEM).systemBytes`，或既有诊断路径），
并按下面的对照组排除误判：

- **时长对照**：2s 与 10s 两种连接时长，净增量接近 ⇒ 固定成本泄漏（每连接一次）；
  10s 明显更大 ⇒ 与帧数/时长线性相关。
- **命令二分**：裸连接不订阅 vs 订阅、单发某条命令，逐档测净增量，定位到具体 handler。
- **同命令二次**：跑第二轮同样规模，内存**不再涨**（甚至回落）⇒ 堆 arena 平台期，
  不是泄漏；继续线性上涨才是泄漏。

## 重绘/帧数问题：先找持续标脏源，**禁止加全局帧率闸**

> 完整台账（12 类脏源、逐页前后实测数据、诊断变量表、复现命令）见
> `docs/HSF客户端重绘脏源根修_实施记录_20261108.md`；框架级语义见
> `XGui.md` §17。本节只留日常必记的红线。

2026-11-08 实锅与终修（用户口径「帧数该降到个位数、帧耗时该是 0.0x ms」）：

1. 加过一道全局时间闸（`XWidget_paintEvent_base` 里按时间戳限帧）把重绘压到
   2fps。结果：下拉弹层（独立 Popup 顶层窗）被拦成空白、交互延迟 500ms、
   帧耗时读数变成「两帧间隔」507ms。**该闸已整体移除**，不要再加回来。
2. 真正要修的是**持续标脏源**。框架自带画像直接读，不要猜：
   `XGPU_W_FRAME_PROF=1` 输出三行/3 秒——`[wprof]`（各段耗时）、
   `[rprof]`（帧区域像素/单笔最大失效类名）、`[rprof3]`（**入队按
   「父类/自身类」直方图**，稳态下哪一类笔数≈帧数，它就是节拍器）。
3. 本轮查出的四类典型脏源（都属"内容没变却在标脏"）：
   - **setText 之后又显式 `XWidget_update`**——`XLabel_setText` 内部
     `label_updateLabel` 末尾已 `updateGeometry + update`；外面再补一次
     等于废掉同值早退（`HsfHomePage.hsfStatSet` 曾每 200ms × 15 个标签）。
   - **`setPalette` 无同值早退**——Qt `QWidgetPrivate::setPalette_helper`
     （qwidget.cpp:4629-4633）相同调色板直接 return；本框架已补同值早退，
     业务侧「按状态着色」不要再在 setPalette 后追加 `XWidget_update`。
   - **隐藏页仍按节拍刷新**——`HsfMonitorVfd` 的 200ms 定时器在页切走后照样
     `vfdRefresh` 全页回填（49 次/秒布局失效）。刷新入口必须过
     `XWidget_isVisible` 闸；数据本身继续推进，切回时下一拍补齐。
   - **悬浮层自身刷新过密**——`XGUI_PERFORMANCE_OVERLAY_UPDATE_MS`
     已由 250 改 1000（诊断读数 1Hz 足够）；`updateNetwork` 不再自己
     `updateText`，文本刷新统一归统计窗口，否则每秒两次文本变化=两帧。
4. 复杂自绘控件（仪表弧带等）**不要用上千条 AA 细线拼"面"**——每条 AA 线
   各自建一张 4×4 覆盖图，代价随周长线性堆。对齐 Qt 用**一次路径填充**
   （`XPainterPath` + `XPainter_fillPath`，AA 提示下整块一次光栅化），
   静态部分再缓存到 `XImage` 离屏图（`HsfVfdGaugeWidget.m_staticCache`）。
   实测单表 66ms → 6ms，变频器页帧耗时 33.81ms → 2.64ms。
5. 判据（改完必须实测）：
   - 稳态 FPS 应落在 **1~2**（画面不变时只剩时钟 1Hz + 悬浮层 1Hz）；
   - 帧耗时 **0.0x~2ms**（量的是本帧真实绘制耗时，口径见
     `Test/XGuiDemo/xgui_window_demo.c:4872-4903`）；
   - `[rprof3]` 里不应出现"笔数≈帧数"的业务控件类。
6. **整板自绘的看板必须做局部刷新**（模拟量页 8 卡实测 13.46/25.71ms）：
   - 标脏按**元素矩形**（`XWidget_updateRect(卡片矩形)`），不要 `XWidget_update`
     整块看板——后者即使只变一张卡也整板重画；
   - paintEvent 要**消费 PAINT 矩形**：translate 之后
     `XPainter_setClipRect(&painter, &XPaintEvent_rect(pe), ReplaceClip)`，
     并跳过与该矩形不相交的元素（光栅层有裁剪，但文字整形/省略号计算的
     开销裁剪兜不住）。参照 `XPushButton.c` 与 `HsfMonitorAnalog.c`
     `analog_cardRect/analog_rectDirty`；
   - 卡片矩形计算必须与绘制公式**同源同参**（两处同改），否则局部刷新漏脏
     留残影。
7. **自绘控件取数必须走页面统一入口**：远端（XLink）provider 的
   `device()` 槽**恒返回 NULL**（无本地设备对象），只认 `snapshot` 槽。
   HsfMonitorVfd 的仪表弧曾直取 `device()->status_const` ⇒ 联机时数值标签
   有值、进度弧永远 0%（2026-11-08 实锅）。取状态一律走 `vfdPageStatus`
   这类封装（snapshot 优先、device 回落）；同页多套数据源必然打架。
8. **「推送里每拍都调」的 setter 必须自己判同值**（框架这几处刻意不做
   同值早退，重复调用的代价是布局/脏区风暴）：
   - `XWidget_setVisible`：显隐承载布局激活，框架**故意**不复刻 Qt 的同值
     早退（XWidget.c 注释有据）。每拍重复 setVisible 会 `XLayout_invalidate`
     父布局 ⇒ 布局风暴（阀卡片实测 662 次失效/秒、卡内按钮 248 笔脏区/秒）。
     调用点先比 `XWidget_isHidden()`（显式隐藏位）：状态没变就别调。
   - `setEnabled` 有真同值早退，但**「先启用再禁用」两步写法等于两步真翻转**：
     `setEnabled(close, canControl); if (Closed) setEnabled(close, false);`
     每拍两次标脏（阀页 60.9fps 的直接来源）。先算最终态再一次下发。
   - `setName`/`setText` 这类文本 setter：XLabel 有同值早退，但业务层的
     `setName` 若自己 alloc/free 字符串，要在**业务层**先 `strcmp` 早退
     （数字量 16 点 × 10Hz 重放名称 = 160fps，2026-11-08 实锅）。
9. **PAINT 事件区域 = 入队时脏区快照 ∪ 当前脏区**（2026-11-08 对齐 Qt
   `QWidgetWindow::handleUpdateRequest`：Qt 直接画累积 dirty，忽略事件携带
   区域）。只信快照时「一次推送标脏 N 格 = N 帧」（16 点数字量页 159.8fps）。
   现在 flush 取并集，同一轮事件循环的标脏一帧画完。诊断回退：
   `XGUI_FLUSH_DIRTY_UNION=0` 恢复旧口径。
10. **裸字面量语句 = 未初始化缓冲**：`"原始: N mV";` 这种「把注释当占位」
   的写法编译照过，真正画出去的是栈垃圾（模拟量页的「原始」行、区段标题
   「输入 (AI) N 点」、无名点兜底「点 N」三处 2026-11-08 全修）。搜索口径：
   `grep -rn '^\s*"[^"]*";\s*/\*' HighSpeedFilter/client/`。
