# HSF 网页版复刻开工盘点（PARITY_PLAN）

> 生成日期：2026-11-13。盘点员：只读盘点，未改任何源码。
> 视觉与功能**唯一基准**：`HighSpeedFilter/client_py/hsfsrv_client`（PyQt5，16 页——
> 2026-11-13 起「本地设置 + 悬浮窗设置 + 窗口设置」合并为一页，页数 18 → 16）。
> 被改造对象：`HighSpeedFilter/webui_py/hsfweb`（上一轮半成品后端骨架）。
> 纪律沿用根目录 AGENTS.md：零第三方依赖（纯 stdlib 后端 + 原生 HTML/JS/CSS）、
> 不留占位控件、不留「选了不兑现」的选项、界面动作反馈必须有成功/失败/超时三选一终态。

**常驻服务探活实测（本会话 2026-11-13 执行）**
- `curl http://127.0.0.1:8080/api/ping` → HTTP 404 "not found"（Server: BaseHTTP/0.6 Python/3.12.10）；
  `/api/version`、`/api/pages` 同样 404。**8080 在跑（PID 15580），但跑的是旧骨架代码**
  （现行 api.py:237-240 有 `/api/ping`、`/api/pages`，线上实例没有）→ 新代码落地后必须重启该进程
  才生效（`MSYS_NO_PATHCONV=1 taskkill /F /PID <pid>` 后重启 `python -m hsfweb`，先 grep 确认进程消失）。
- TCP `127.0.0.1:6100` → OPEN（PID 25840，`bin/HsfServer.exe --sim`）。**不要动它**，网页服务器
  默认上游就是它。

---

## §1 16 页逐页清单

页号/页名与导航树**逐项对齐** `client_py/hsfsrv_client/pages_registry.py:12-108`
（**不是**骨架 api.py:90-115 的旧口径「监视/变频器/流量计/电磁阀」）。产品名
`theme.APP_TITLE = "PMMA有机玻璃高速过滤器"`（theme.py:73），标题栏居中恒显产品名、不随页变
（main_window.py:87-88,188）。

通用约定（全部页面共享，`page_base.py:24-62` + `main_window.py`）：
- 控制闸 `control_blocked_reason()`（page_base.py:46-61）：未连接 / 自动模式（区分「自动运行中」与
  「自动模式未跑」两种文案）/ 无 `PERM_DEVICE_CONTROL` → 返回原因串，按钮置灰并在状态行显示。
- 权限判定一律 `can(role, perm)` 查位表（permissions.py:39-43），**禁止** `role >= X` 数值比较
  （角色值不单调：Operator=3 是追加值，proto.py:89-96）。
- 全局常驻订阅：`TOPIC_TEMPHUM_STATUS(0x00010003)` + `TOPIC_DEVICE_HEALTH(0x00010005)`
  （main_window.py:78-81，顶栏 chrome 任何页都要）；其余主题**切页收敛**（bridge.set_topics，
  bridge.py:339-351）。
- 骨架既有 SSE 面保持：`GET /api/stream`（首帧 `snapshot` 全量 + 后续按事件推送）、
  `POST /api/topics {connId, topics:[...]}`（hub 按所有连接**并集**收敛上游订阅，hub.py:355-377）。

### 页 0 主界面（HomePage, pages_other.py:92）
- **自动模式卡**（首行，无页头）：模式下拉（`produce 产水模式 / backwash 反洗模式 / rinse 顺洗模式 /
  auto 自动（流量循环）`，proto.AUTO_MODES + AUTO_MODE_AUTO）+ 启动/停止/复位三按钮 +
  状态行（短语化：手动模式提示 / 报警锁存 / 运行中·模式名·相位 / 急停锁存 / 已停止·待启动，
  pages_other.py:668-899）+ 「累计」运行时长（`N天 HH时 MM分 SS秒`，天数封顶 999+，缺键显示 `--`）
  + 剩余时间表盘 RemainDial（**只在自动循环运行时显示**：`autoStageElapsedMs >= 0` 才显，
  pages_other.py:903-920）。
- **总览卡**：电箱温度/湿度（temphum 按侧择优 A→B）；搅拌泵卡（变频器 run_state_text/
  outputFreqHz/setFreqHz，点击弹「搅拌泵详情」悬浮卡：状态/频率/设定/电流/转速）；
  设备健康卡（总数/故障/离线，点击弹「设备状态」表：5 列 设备/类型/状态/地址/错误信息，
  `d{i}_addr` 地址串）；压差卡（AI 通道 0：`e0/eu0/en0/ew0`，越限 `ew0` 警示）；
  流量计卡（flow+单位码文本 / 正反累计 / 报警中文 `fmAlarmText`：空管→上限→下限→系统）；
  电动球阀卡（6 路 `● 状态` 色点：已开绿/已关灰蓝/动作中橙/故障红/未配置浅灰）；
  浊度仪卡（2 台 `v{i}_ntu÷100` + `v{i}_temp` 水温（缺键不显示，不是 0℃）+ **进水泵状态**并进
  说明行：`通/断`（输出点，无反馈）+ 数字量 DO 兜底）；按钮组系统状态卡（按**服务端下发通道名**
  匹配，不写死通道号：`急停/启动按钮/停止按钮/复位按钮`(DI，急停·停止常闭) +
  `启动按钮灯/停止按钮灯/复位按钮灯`(DO)，pages_other.py:151-169,1130-1242）。
- 订阅：VFD/TEMPHUM/AI/FLOWMETER/VALVE/HEALTH/TURBIDITY/DIGITAL/PUMP（pages_other.py:114-123）。
- 动作：1s 轮询 `CMD_RUN_MODE`(0x0105) 无载荷 → 回 `mode/auto/autoRunning/autoMode/autoPhase/
  autoFault/autoRunMs/autoStageElapsedMs/autoStageTotalMs`；启动=`0x0115 {"on":true,"autoMode":<id>}`；
  停止=`0x0115 {"on":false}`；复位=`0x0115 {"op":4}`（AUTO_OP_RESET，需 `PERM_AUTO_CONTROL`）。
- Web 端点：`GET /api/mode`、`POST /api/mode`、`POST /api/auto/control`；SSE 事件
  `mode/vfd/temphum/analog/flowmeter/valve/turbidity/pump/digital/health`。

### 页 1 主菜单（MainMenuPage, pages_other.py:1409）
三大按钮（▤ 设备 / ≣ 数据 / ⚙ 设置）跳页 2/8/12。端点：`GET /api/pages`。

### 页 2 设备（SubMenuPage, pages_other.py:1434）
子菜单五项：数字量(3)/模拟量(4)/搅拌泵(5)/水质监测(6)/电动球阀(7)。端点：`GET /api/pages`。

### 页 3 数字量（DigitalPage, pages_monitor.py:36）
- 模块下拉（台数=服务端推送的 `dev` 数）+「请求快照」按钮；DI 只读灯阵（LampRow）+ DO 可点
  按钮阵（ToggleGrid，宽屏 8 列自适应，语义名两行）；`din%d/don%d` 名字三态保留
  （帧里**有**名字才整表替换，没有沿用缓存，model.py:358-399）；点数按 `count` 重建（16/32）；
  位宽 `0x%0*X`（>16 点用 8 位）。
- 动作：快照 `0x0108 {"dev":int}`；DO 写 `0x0109 {"dev","do","on"}` 单点或 `{"dev","mask","values"}`
  整组（应答 `{ok,dev,do}` 位图回显）。闸：`PERM_DEVICE_CONTROL` + 自动模式拒（按钮置灰+原因）。
- 端点：`POST /api/digital/snapshot {dev}`、`POST /api/digital/control {dev,do,on}|{dev,mask}`；
  SSE `digital`。

### 页 4 模拟量（AnalogPage, pages_monitor.py:250）
4 列表（通道/名称/数值/单位）8 路：`r{i}` 原始 mV、`u{i}` µA（÷1000=mA）、有工程量的通道同列显示
`11.988 0.499` 两段数字 + `mA / MPa` 双单位 + `ew{i}` 越限说明；底部原始寄存器串。只读。
端点：SSE `analog`（无控制）。订阅 `TOPIC_AI_STATUS(0x00010002)`。

### 页 5 搅拌泵（VfdPage, pages_monitor.py:1144，设备类别=变频器）
- **运行状态卡** 10 块：运行状态（`run_state_text`：减速中且 `running=false` ⇒「正在停机」，
  model.py:119-134）/输出频率/输出转速（`speedRpm`，无则按 rated 换算）/设定频率/母线电压/
  输出电压/输出电流/输出功率/输出转矩/故障（FAULT_TEXT 中文表，model.py:151-161）；
  过渡态配色（加速/减速=蓝、运行=绿、故障=红、停机=黑）。
- **控制卡**：方向下拉（正转/反转，`rev`）；**启停合一按钮**（运行中显示「停止」=`{"op":5}`；
  停止时显示「启动」=`{"op":4,"rev":<方向>,"hz":...,"rpm":...}` —— 给定**必须捎带在启动帧里**，
  服务端拒绝停机独立给定，pages_monitor.py:1554-1573）；自由停车 `{"op":6}`（仅运行中可点）；
  故障复位 `{"op":8}`；**点动**：按住每 200ms 重发 `{"op":9,"rev":...,"hz","rpm"}`、松手发
  `{"op":101}`（JOG_RELEASE；运行中禁点动；服务端看门狗 600ms）。Web 侧点动由服务器代持
  （`POST /api/jog/start` 起后台 200ms 循环、SSE 断开自动释放、JOG_MAX_MS=10s 强停，hub.py:83-89）。
- **调速**：频率(0~50 Hz)/转速(0~1440 rpm)下拉切换；数字框 400ms 去抖、滑块 250ms 去抖改值即下发
  （停机只预置不发，`_preset_dirty` 启动时捎带）；额定折算点 `ratedRpm/ratedHz` 来自
  `CMD_VFD_SNAPSHOT`(0x0102) 应答。
- **实时趋势**：3 序列（输出频率/给定频率/输出电流）60 点滚动窗；「刷新」下拉 200/500/1000/2000ms
  纯客户端节流；拖动/滚轮缩放=接管视口、「跟随」恢复；X 轴 `HH:MM:SS` 本地时钟、Y 轴按首序列 5 档
  （pages_monitor.py:419-1057）。
- 端点：`POST /api/vfd/control {op,rev,hz,rpm}`、`POST /api/vfd/snapshot`、
  `POST /api/jog/start {rev,connId}`、`POST /api/jog/stop`；SSE `vfd`。

### 页 6 水质监测（WaterPage, pages_monitor.py:1961）
- 流量计卡（LD 系列）：瞬时流量/瞬时流速/正向累积/反向累积（单位随 `flowUnit/totalUnit` 单位码，
  FLOW_UNIT_TEXT/TOTAL_UNIT_TEXT）+ 报警中文（`flow_alarm_text` 位图：空管→上限→下限→系统）。
- 浊度监测卡：2 台 `进水浊度仪/2`（名字取服务端 `v{i}_name`），`v{i}_ntu÷100` 两位小数，
  `v{i}_temp` 水温（缺键不显示）；≥20 NTU 标红（**显示口径**，非报警判据）。
- 进水泵卡：启动/停止按钮（`0x0114 {"index":0,"on":bool}`；应答整帧回显直接落地）；状态文案
  `运行（输出已开）/停止（输出已关）/未就绪`；**必须注明「仅有输出、无运行反馈」**。
- 实时趋势：瞬时流量 + 浊度1 + 浊度2 三序列；流量计与浊度**双流独立时钟**节流。
- 端点：`POST /api/pump/control {index,on}`、`POST /api/turbidity/snapshot`、
  `POST /api/flowmeter/snapshot`；SSE `flowmeter/turbidity/pump`。

### 页 7 电动球阀（ValvePage, pages_valve.py:279）
- 顶部横幅（恒占位不跳动：控制禁用原因 / N 路未配置 / 最近告警 `⚠ 阀名：文字`）。
- 6 张卡片：状态徽标（VALVE_STATE_TEXT：状态未知/已关/已开/动作中/故障）+ 顶部状态点；
  开到位/关到位反馈点（`openFb/closeFb`）；开阀/关阀按钮（**已到位那一侧置灰**；动作中两侧都可点
  =反向）；提示行（服务端 `text` / 未配置提示 / 「点另一侧可反向」）。
- 动作：`0x0111 {"index":int,"on":bool}`；应答带 `v{i}_cmdResult`（issued/repeated/reversed/ignored
  → 四种状态栏文案 VALVE_CMD_TEXT，pages_valve.py:79-84）+ 整帧 6 路状态落地 model。
  入页拉 `0x0110 CMD_VALVE_SNAPSHOT`。告警帧与状态帧同主题（`ac`/`a{i}_*` 键，先认告警再认状态，
  model.py:871-917）。
- 端点：`POST /api/valve/control {index,on}`、`POST /api/valve/snapshot`；SSE `valve`（含告警帧）。

### 页 8 数据（SubMenuPage）
历史数据(9)/日志(10)/报警(11)。端点：`GET /api/pages`。

### 页 9 历史数据（HistoryPage, pages_other.py:1457）
- **数据源 = 服务端历史库**（0x0118），不是本地缓存。三行工具条（data_toolbar.DataPageToolbar，
  data_toolbar.py:419）：
  - 筛选行：起始/结束时间（`HH:MM[:SS]` 或带日期，起点缺省补 00:00:00、终点补 23:59:59）+ 列多选
    （16 列，默认全勾、至少一列，勾选**立即生效**）+ 关键字 + 筛选/清除筛选；
  - 分页行：每页 10/20/50 + 上一页/下一页 + 「第 N/M 页」+「共 N 条（本页 M）　库内共 D 条」+ 刷新；
  - 视图行：「表格视图/图表视图」双按钮 + 滚动锁定 + 导出数据。
- **显示列 16**（pages_other.COLUMNS:1494-1497）：时间/模式/搅拌状态/输出频率(Hz)/输出电流(A)/
  搅拌转速(rpm)/浊度1(NTU)/浊度2(NTU)/水温(℃)/瞬时流量/正累计/压差(MPa)/电箱温度(℃)/
  电箱湿度(%RH)/DI(32位)/DO(32位)。**线上列 19**（`proto.HISTORY_COLUMNS`，water_temp 在末尾）——
  服务端发**原值数组 + cols 名**，格式化在客户端（`_server_row_texts`：`%.2f`、`0x%08X`、NULL→"--"）。
- **分页窗口锚点**：`beforeTs` 首屏取一次、翻页原样回传；第 1 页每 3s 带新锚点整页重取（追最新），
  第 N 页只探总数不动内容（pages_other.py:1770-1789）。在途请求单飞 + 页码夹取。
- 时间/关键字/列选择**全部下推服务端同一条 WHERE**（`loTs/hiTs/kw/kwCols`）——本地筛=「关键字筛选无效」。
  起始框默认回填 `earliestTs`（只填一次、用户碰过不覆盖；`VALID_TS_MIN=1577836800000` 坏数据闸）。
- 行异常淡红底（故障/浊度≥20/压差越限说明/阀故障）；双击表头排序（数值列按数值、非数值排后）。
- **图表视图**：页内 QStackedWidget 二选一（**不弹窗**）；按筛选条件分批累加**全量**
  （批 50 行=协议上限，上限 12000 行显式停）；每线等距抽样 ≤2000 点且首尾必留；状态行
  「筛选共 N 行 · 图表取回 M 行 · 每线最多 K 点（已等距抽样/未抽样）」；筛选签名（lo/hi/kw/列）防重取；
  折线列多选、多序列各自归一化不做双 Y 轴。
- **导出 = 筛选命中全量**：见 §2「导出方案」。
- 端点：`GET /api/history/page?offset&limit&beforeTs&loTs&hiTs&kw&kwCols&export`（转发 0x0118）、
  `GET /api/history/export?format=csv|xlsx&...`（流式下载）。订阅：无推送依赖（查询型），历史页在线时
  不订遥测主题。

### 页 10 日志（LogPage, pages_other.py:3659）
- 数据源 `CMD_LOG_PAGE`(0x0119)。3 列表：时间/类别/事件。
- 类别下拉**立即生效**：全部/conn 连接/ctrl 控制/alarm 告警/auto 自动/auth 鉴权/**sys 系统**
  （sys=Python 服务端专有档；C 端选中恒空但不报错，pages_other.py:3712-3720）。
- 请求 `{offset,limit,beforeTs,loTs,hiTs,category,kw}`；应答 `{ok,rows:[{ts,category,text}],
  total,count,offset,limit}`——rows 是**对象数组**（区别于 0x0118 值数组）；**应答归属判定**：
  `offset/limit` 必须回显同值才铺表（防串页）；锚点 `<=0` 时发包前定格本地「现在」；
  时间窗 DESC 取页、页内正序；页大小 10/20/50（服务端夹 1..50）。
- 老服务端「未知命令」否定应答 ⇒ 提示「服务端不支持运行日志查询（0x0119）」+ 空表不崩；
  其余 `err`（如「落盘未启用…」）原样显示。
- 导出 CSV（`utf-8-sig`）：**权限闸在客户端**（0x0119 无 export 键形态；需 `PERM_DOWNLOAD`）；
  按筛选全量逐批取（批 40，见 §2）。「客户端运行日志」折叠副视图（web 侧可映射为页面内
  自身的消息流，非必建）。
- 端点：`GET /api/log/page?offset&limit&beforeTs&loTs&hiTs&category&kw`、`GET /api/log/export?...`。

### 页 11 报警（AlarmPage, pages_other.py:4487）
- **数据源（2026-11-17 改「服务端单一数据源」，对齐桌面客户端）**：报警历史
  一律向服务端要——XLink `0x011A` 报警历史分页（web 后端 `GET /api/alarms/page`
  转发，`hub.alarm_page`；与 0x0118/0x0119 同形）。服务端 alarms 表记
  健康边沿 + 自动告警 + 阀告警 + 保护动作；SSE `alarm`/`health`/`valve`
  **只当「有新行」的触发器**（2s 节流 + 250ms 合并抖动，到达后整表重取，
  不做本地增量拼行）。**老服务端兜底 fail-open**：上游没有 0x011A
  （旧 C 服务端）/链路未就绪/超时 ⇒ 回落 `GET /api/alarms`（hub 本地
  AlarmLog 边沿存档，MAX_ROWS 500），别让报警页变空白。
- 工具条：类型下拉（全部 + 8 类 KIND_TEXT：变频器/温湿度/流量计/模拟量/485总线/数字量/
  电动球阀组/水质 + 自动运行）+ 起止时间窗 + 关键字 + 列多选（类型/时间/关键字
  均**客户端筛**，取数不带）；**客户端内部分页** 10/20/50；统计/导出按
  **筛选命中全集**（`_visible_records()`），不受当前页影响；滚动锁定（锁定期新报警只入库不刷表）；
  **无刷新按钮**（推送没有「重查」动作）、**无图表**（用户定案删）。
- 导出 CSV（`PERM_DOWNLOAD` 客户端闸；全集在内存，Blob 下载即可）。
- 端点：`GET /api/alarms/page`（0x011A 转发，主数据源）+ `GET /api/alarms`、
  `GET /api/alarms/active`（老服务端兜底 + 诊断面；store.AlarmLog 的
  feed_health/feed_valve_alarm/query/active 保留）；SSE `health`（+`alarm`）。

### 页 12 设置（SubMenuPage）
本地(13)/远端(14)/网络(15)。端点：`GET /api/pages`。
（2026-11-13 三页合并后：原悬浮窗(16)/窗口(17)两页并入 13，条目随之删除。）

### 页 13 本地设置（LocalSettingsPage + OverlayPage + WindowPage 三页合并，2026-11-13）
**三分区，一个功能项不许丢**（页数 18 → 16，页号 0..15，其余页号不变）：
- **分区一「界面设置」**（原 LocalSettingsPage, pages_other.py:4675）：显示卡：紧凑模式
  （按窗宽自动 <600px）/全屏显示/界面缩放（100/110/125/150%，改后刷新生效）；
  数据刷新卡：界面刷新周期 100~2000ms（立即生效）+ **趋势刷新间隔**（200/500/1000/2000ms，
  与页 5/6 趋势卡「刷新」下拉同一份偏好键 `trendSampleMs`）；
  **每页条数卡**：历史数据 / 运行日志 各一档（10/20/50 条，与页 9/10 工具条同一份偏好键
  `histPageSize` / `logPageSize`）——这三项参考客户端合并后也收进本页（总入口）。
- **分区二「悬浮窗」**（原 OverlayPage, pages_other.py:5849）：显示元素卡：显示顶部标题栏/
  显示底部状态栏/显示左侧导航栏（勾选即时生效 + localStorage）；标题栏内容卡：显示时钟/
  显示温湿度（web 侧做成可隐藏并持久化——**兑现才摆**）。
- **分区三「窗口与屏幕」**（原 WindowPage, pages_other.py:5920）：尺寸卡（1280×720 /
  1024×600 / 800×480 / 全屏 +「应用」）；屏幕休眠卡（状态行 + 自动熄屏下拉 + 系统挂起下拉
  （浏览器无原语 ⇒ 置灰）+ 唤醒来源三勾 + 立即熄屏/唤醒屏幕/保存策略/请求授权（置灰）；
  浏览器真能兑现的是 Screen Wake Lock，语义逐条改写、不谎称「熄屏支持」）；信息卡（窄窗
  自动收起导航说明）。
全部落**浏览器 localStorage**（§3 键表）。无服务端交互。

### 页 14 远端设置（RemoteSettingsPage, pages_other.py:4783）
- **运行模式卡**：大标签（自动（远程控制已禁用）/手动（允许远程操作）/未知）+ 切手动/切自动
  （`0x0105 {"mode":0|1}`，需 `PERM_MODE_SWITCH`；**同模式重复下发不动设备**）。
- **自动运行卡**（与模式切换分开——切模式≠启动）：`0x0115 {"on":true|false}`，
  需 `PERM_AUTO_CONTROL`；三态文案 手动模式（不可用）/运行状态未知/运行中/已停止。
- **自动模式配置 + 自动循环合并一卡、一次提交**：**4 个模式块**
  （produce/backwash/rinse/**drain 放水模式**，列表取 `GET /api/pages` 的
  `autoConfigModes`；`autoModes` = 首页下拉用的「能跑」列表 = 四模式 + 组合「自动」，
  两份列表**必须分开**）各含
  「启动设备」多选（`AUTO_DEVICES` 8 项：valve1..6/stirPump/inletPump，空选独立文案「未选择」）、
  搅拌电机转速 `stirRpm`（0~1440，缺省 1440=铭牌点，框旁 `≈Hz` 折算提示 50Hz≈1440rpm）、
  方向 `stirReverse`（正转/反转）、阀复位两开关 `resetValvesOnStop`（缺省 true）/
  `resetValvesOnEstop`（缺省 false）、换相策略两下拉 `stirSwitchPolicy`/`inletSwitchPolicy`
  （stopRestart 先停再启【缺省】/carry 保持上一模式状态/direct 直接切到当前模式状态）；
  循环子节 `cycle`：`enabled`（缺省 true）/
  `pressThreshold`（MPa，0~10，缺省 0.5）/`pressCompare`（gt 大于|lt 小于）/
  `timerFallback`（缺省 false）/`timerMinutes`（缺省 30）/`backwashMinutes`（10）/
  `rinseMinutes`（10），分钟项 1~1440。载荷一次下发
  `0x0117 {"section":"auto","modes":{<mode>:{"devices":[id...],"stirRpm":int,"stirReverse":bool,
  "resetValvesOnStop":bool,"resetValvesOnEstop":bool,"stirSwitchPolicy":str,
  "inletSwitchPolicy":str}},"cycle":{...}}`。
  **回显纪律**：应答带哪节铺哪节（`_populate` 局部应答不冲别的卡，pages_other.py:5504-5568）；
  未知设备 id 丢弃；无 `PERM_ACCOUNT_ADMIN` 整卡置灰。
- **变频器参数卡（2026-11-18 新增，`0x011B`）**：参数表 P1.00~P1.05（`0xF100`~`0xF105`
  电机铭牌组）三列＝参数名（**参数名/单位跟服务端应答**）/当前值（**服务端回读工程值**
  `params[].value`，枚举型显中文名 `valueText`）/写入值（工程值输入框，量程按
  `PARAM_DEFS` 的 vmin/vmax 夹取；枚举型渲染 `<select>`，选项优先用服务端
  `options`，缺键回落本地 `PARAM_OPTIONS`）。读＝`GET /api/vfd/params`（登录即可），
  写＝`POST /api/vfd/params {op:1,values:{"<十进制地址>":raw}}`（**管理员**）；
  写后用**应答回读值**刷新（`applied` 逐项判「已写入并回读确认：N 项；失败：P1.0x…」）；
  RunLocked（变频器运行中写）由服务端终审拒绝，err **原样透传**
  （「变频器运行中禁止写参数（请先停止）」，三端同款文案）；
  旧 C 服务端无 `0x011B` ⇒ 显式失败包络（`ok:false` + err），卡内状态行显示原因，
  **不洗白成「已读 0 项」**。
- **连接信息卡**：`已连接 · 用户 X（角色中文）`。
- **采集配置卡** 7 行（搅拌泵/温湿度/进水浊度仪/出水浊度仪/流量计/模拟量/数字量）：
  逐台开关 `vfdEnabled/temphumEnabled/turbidity1Enabled/turbidity2Enabled/flowmeterEnabled/
  analogEnabled/digitalEnabled` + 逐台周期 `vfdPollMs/temphumPollMs/turbidity1PollMs/
  turbidity2PollMs/flowmeterPollMs/analogPollMs/digitalPollMs`（50~60000ms，步进 50）。
  兼容兜底：`turbidityEnabled = (1||2)`、`turbidityPollMs = turbidity1PollMs` 一并下发；
  `0x0117 {"section":"collect", ...}`。
- **数据存储卡**：`enabled`/`path`（空=服务端程序目录）/`sampleMs`（1000~3600000，缺省 60000）/
  `maxMb`（0=不限「不限」特显，缺省 50）/`fullPolicy`（ring 循环覆盖/stop 到限停写/rotate 分段滚动，
  选中项下方显示 STORAGE_POLICY_HINTS 一句话）/`keepFiles`（1~99，仅 rotate 可用）/
  `historyTrigger`（always 持续采集入库【缺省】/auto 仅自动运行时入库）→
  `0x0117 {"section":"storage", ...}`。用量实况：进度条 + `storageDbPath` +
  `storageStats`（`usedMb/maxMb/rows/segments/droppedRows/policy/paused/pauseReason`，
  pages_other.py:5570-5614；C 端还回 `usedBytes/maxKb`，web 按 usedMb 口径取）。
- **登录策略卡**：`loginAllowAnonymous`/`loginAllowConcurrent`/`loginMaxSessions`（0=不限，0~256）
  → `0x0117 {"section":"login", ...}`；说明「写入立即对下一次登录生效并落盘，已在线会话不受影响」。
- 配置读取：入页 `0x0116 CMD_CONFIG_GET`（无载荷）→ 应答 `{ok,collect,storage,login,auto,
  storageDbPath,storageStats}`。
- 端点：`GET /api/config`、`POST /api/config {section,...}`、
  `GET /api/vfd/params`（0x011B 读）、`POST /api/vfd/params {op:1,values}`（0x011B 写）；
  SSE `mode`。

### 页 15 网络设置（NetworkPage, pages_other.py:5801）
当前连接卡（`服务器 host:port` + 状态 已就绪/未连接）+ 重新连接按钮（提示「退出后重新登录以更换
服务器」）。Web 版：上游链路状态直接读 SSE `link`（upstream 状态对象，hub.py:349-353）或
`GET /api/status`；「重新连接」= 断开/重连**网页服务器到上游**的链路或提示改登录参数。
登录参数（服务器地址/端口）在登录页改，本页只读。

---

## §2 后端改造清单（webui_py/hsfweb）

### 2.1 proto.py —— 全量重写（逐常量对齐 client_py/hsfsrv_client/proto.py）
现有骨架 proto.py（466 行）**严重过期**，缺下列全部条目（对照 client proto.py 行号）：
1. **角色四档 + 权限位**（proto.py:89-105）：`ROLE_ANONYMOUS=0 / ROLE_USER=1 / ROLE_ADMIN=2 /
   ROLE_OPERATOR=3`（骨架只有 0/1/2，api.py/auth.py 还在用 `role >= ROLE_ADMIN` 数值比较——
   操作者=3 会被误放行成管理员，必须改查表）；`PERM_VIEW=1<<0 / PERM_DEVICE_CONTROL=1<<1 /
   PERM_MODE_SWITCH=1<<2 / PERM_AUTO_CONTROL=1<<3 / PERM_ACCOUNT_ADMIN=1<<4 /
   PERM_DOWNLOAD=1<<5 / PERM_ALL=0x3F`。
2. **命令号补齐**：`CMD_TURBIDITY_SNAPSHOT=0x0112`、`CMD_PUMP_SNAPSHOT=0x0113`、
   `CMD_PUMP_CONTROL=0x0114`、`CMD_AUTO_CONTROL=0x0115`、`CMD_CONFIG_GET=0x0116`、
   `CMD_CONFIG_SET=0x0117`、`CMD_HISTORY_PAGE=0x0118`、`CMD_LOG_PAGE=0x0119`
   （骨架只有 0x0101~0x0111，proto.py:117-136,273-277）。
3. **主题号补齐**：`TOPIC_TURBIDITY_STATUS=0x00010009`、`TOPIC_PUMP_STATUS=0x0001000A`、
   `TOPIC_AUTO_ALARM=0x0001000B`；保留 `TOPIC_VALVE_ALARM=0x00010008` 常量但标注**遗留不推**
   （C 头 XhsfLinkProto.h:1033-1050：告警随 0x00010007 合帧，`ac` 键判型）。
4. **运行状态/自动键**（proto.py:380-400,1092-1154 同名）：`KEY_AUTO_MODE="autoMode"`、
   `KEY_AUTO_PHASE="autoPhase"`、`KEY_AUTO_FAULT="autoFault"`、`KEY_AUTO_RUN_MS="autoRunMs"`、
   `KEY_AUTO_STAGE_ELAPSED_MS="autoStageElapsedMs"`、`KEY_AUTO_STAGE_TOTAL_MS="autoStageTotalMs"`、
   `KEY_AUTO_RUNNING="autoRunning"`；`AUTO_OP_START/STOP/ESTOP/RESET = 1/2/3/4`；
   `AUTO_ALARM_VALVE_TIMEOUT=5` 等 5 类告警枚举。
5. **自动配置键**（proto.py:242-286,328-378）：`CFG_SECTION_AUTO="auto"`、`AUTO_MODES`、
   `AUTO_MODE_AUTO="auto"`、`AUTO_DEVICES`（8 项）、`AUTO_STIR_RPM_*`、
   `AUTO_MODE_RESET_VALVES_ON_STOP="resetValvesOnStop"` / `_ON_ESTOP="resetValvesOnEstop"`
   （缺省 True/False）、`CFG_AUTO_CYCLE="cycle"` 及 7 个循环子键 + 缺省值/范围、
   `AUTO_PRESS_COMPARE_GT/LT="gt"/"lt"`。
6. **配置键全组**（proto.py:172-240,296-326）：collect 8 开关 + 8 周期键（含逐台
   `turbidity1Enabled/turbidity2Enabled/turbidity1PollMs/turbidity2PollMs`）；storage 7 键
   （`enabled/path/sampleMs/maxMb/fullPolicy/keepFiles/historyTrigger`）+ `STORAGE_POLICIES` +
   `STORAGE_SAMPLE_MS_DEF=60000` / `STORAGE_MAX_MB_DEF=50`（三处同源：C
   `HSF_STORAGE_SAMPLE_MS_DEF/HSF_STORAGE_MAX_MB_DEF`，XhsfLinkProto.h:322,331）+
   `HISTORY_TRIGGER_ALWAYS/AUTO="always"/"auto"`；login 3 键 + 范围 0~256；
   `CFG_POLL_MS_MIN=50 / CFG_POLL_MS_MAX=60000`。
7. **历史/日志分页键**（proto.py:402-486）：`HIST_KEY_*` 全 15 个（offset/limit/beforeTs/loTs/hiTs/
   kw/kwCols/total/rows/cols/dbRows/latestTs/earliestTs/windowStartTs/windowEndTs/export）、
   `HISTORY_PAGE_MAX=50 / MIN=1 / DEFAULT=20`、`HISTORY_COLUMNS`（19 列，water_temp 末尾）、
   `CMD_LOG_PAGE` 键组 `KEY_LOG_*`（与 HIST_KEY_* 同字面 + `category/count/ts/text`）、
   `LOG_PAGE_MAX=50 / LOG_PAGE_DEFAULT=20`、`LOG_CAT_CONN/CTRL/ALARM/AUTO/AUTH/SYS`。
8. **浊度/泵/阀键**（proto.py:641-732,749-777）：`KEY_TURB_NTU="ntu"/KEY_TURB_RAW="raw"/
   KEY_TURB_ONLINE="online"`、`KEY_PUMP_INDEX/NAME/ON/READY`、`KEY_VALVE_CMD_RESULT="cmdResult"`
   （issued/repeated/reversed/ignored 四文案）；`KIND_WATER=7`（骨架只到 KIND_VALVE=6，
   proto.py:377-386 —— 缺它健康表「水质」行会显示未知）。
9. **缺省账户**：`DEFAULT_OPER_NAME="oper"/DEFAULT_OPER_PASS="oper"`（三档建户，
   server_py proto.py:937-938）。
10. 同步点注释互相点名：`server_py/hsfsrv/proto.py` 与 `client_py/hsfsrv_client/proto.py`
    （两端已逐值一致，webui 抄 client 副本即可）；改任何值必须三处同步（铁律）。

### 2.2 permissions（新增 webui 版 `permissions.py`，或并入 auth.py）
照抄 `client_py/hsfsrv_client/permissions.py` + `server_py/hsfsrv/permissions.py` 缺省表：
| 角色 | 值 | 账户 | VIEW | DEV_CTRL | MODE | AUTO | ACCT | DOWNLOAD |
|---|---|---|---|---|---|---|---|---|
| 匿名 Anonymous | 0 | （未登录） | ✔ | | | | | |
| 普通用户 User | 1 | admin/admin | ✔ | | | | | ✔ |
| 管理员 Admin | 2 | root/root | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ |
| 操作者 Operator | 3 | oper/oper | ✔ | ✔ | | | | ✔ |
（缺省三户由服务端空库自动建：`auth.create_defaults` / C `XAccountManager_ensureDefaults`，
两端名单逐字一致。）判定一律 `can_role(role, perm)` 位表；拒绝文案用
`server_py permissions.DENIED_TEXT` 原文（「权限不足：切换运行模式需要管理员账户」等）。

### 2.3 xlink.py（上游链路）
保留现有重连/心跳/请求-应答/订阅收敛（骨架 xlink.py 已可用）。改动：
- 无（协议层不动）；0x0118/0x0119/0x0116/0x0117 都是普通 request 转发，走既有 `request()`。
- 登录用的一次性探测连接（auth.verify_with_upstream）保留。
- 主链路登录账号固定 `--user/--password` 参数（缺省 root/root），与浏览器身份无关（auth.py:33-37
  的单连接约束）。

### 2.4 model.py（数据模型）
- **补按侧分存 + 择优**（照 client model.py:164-257,418-537,639-797）：`TempHumState`、
  `FlowMeterState`、`TurbidityState` 各持 `sides{0,1}`、`pick(prefer)`——温湿度优先 A 侧、
  流量计/浊度优先 B 侧；「无效帧不顶替」「每侧只留最近一帧」；挑不到=显示 `--`/「无数据」，
  不拿旧值冒充。骨架现有单槽实现（model.py:303-375,593-655）会被空帧盖掉真值，必须改。
- **补 TurbidityState**（`count` + `v{i}_ntu(raw 端 ×100)/raw/online/temp/temp÷100`，缺 temp 键
  =无温度）与 **PumpState**（`v{i}_index/name/on/ready`；on=输出无反馈）——骨架完全没有这两类。
- `VfdState`：补「正在停机」口径（runState==8 且 !running）；`snapshot()` 已有。
- `DigitalState`：名字三态保留已有（骨架 model.py:507-541 处理了 din/don 前缀），核对「帧里没有
  名字键绝不擦缓存」。
- `ValveState`：补 `cmdResult` 键透传；告警帧判 `ac`（已有，hub.py:283-301）。
- **补自动状态族**：`run_mode/run_mode_known/auto_running(None=未知)/auto_mode/auto_phase/
  auto_fault/auto_run_ms/auto_run_ms_known/auto_stage_elapsed_ms(-1=无)/auto_stage_total_ms/
  auto_stage_known`；解析 0x0105/0x0115 应答时「缺键保持原值」（老服务端兼容，bridge.py:417-447
  同款口径）。
- `HealthSnapshot`：补 `d{i}_addr`（连接地址串）与 `errorText`。

### 2.5 hub.py（扇出与代理）
- `TOPIC_KIND` 补 `TOPIC_TURBIDITY_STATUS→"turbidity"`、`TOPIC_PUMP_STATUS→"pump"`、
  `TOPIC_AUTO_ALARM→"alarm"`（hub.py:65-73 现缺前两个 ⇒ 浊度/泵主题**全被静默丢弃**）。
- 0x0105/0x0115 应答解析补 auto 状态键落 model + broadcast `mode` 事件。
- 控制应答落 model 的既有做法保留（vfd/digital/valve 快照应答同步 broadcast，api.py:521-597）。
- 点动代持保留（hub.py:432-534：200ms 重发、10s 强停、SSE 断开自动释放、JOG_RELEASE）。
- 订阅收敛保留（并集 + 全局常驻 TEMPHUM/HEALTH）。

### 2.6 api.py（REST 面，35 端点）
保留：`POST /api/login`、`POST /api/logout`、`GET /api/me`、`GET /api/ping`、`GET /api/version`、
`GET /api/pages`（**重写注册表**：页名/导航树按 §1，`pageTopics` 补 turbidity/pump）、
`GET /api/state`、`GET /api/stream`（SSE）、`POST /api/topics`、`GET /api/status`、
`GET /api/alarms`、`GET /api/alarms/active`。
改造/新增：
| 端点 | 方法 | 上游命令 | 权限 | 载荷/查询键（照 proto 键名） |
|---|---|---|---|---|
| `/api/mode` | GET | 0x0105 | VIEW | —；回 `mode/auto/autoRunning/autoMode/autoPhase/autoFault/autoRunMs/autoStageElapsedMs/autoStageTotalMs` |
| `/api/mode` | POST | 0x0105 `{"mode":0\|1}` | MODE_SWITCH | `{"mode":int}`；同模式幂等提示 |
| `/api/auto/control` | POST | 0x0115 | AUTO_CONTROL | `{"on":bool,"autoMode":"produce\|backwash\|rinse\|auto"?}` 或 `{"op":4}`（复位）；应答全量状态键透传 |
| `/api/vfd/control` | POST | 0x0101 | DEVICE_CONTROL | `{"op","rev"?,"hz"?,"rpm"?}`；op=9/101 拒收（点动走 /api/jog） |
| `/api/vfd/snapshot` | POST | 0x0102 | VIEW | —；应答含 `ratedRpm/ratedHz` |
| `/api/jog/start`、`/api/jog/stop` | POST | 0x0101 op=9/101 | DEVICE_CONTROL | `{rev,connId}` / `{}`（服务器代持重发） |
| `/api/digital/control` | POST | 0x0109 | DEVICE_CONTROL | `{"dev","do","on"}` 或 `{"dev","mask","values"}` |
| `/api/digital/snapshot` | POST | 0x0108 | VIEW | `{"dev":int}` |
| `/api/valve/control` | POST | 0x0111 | DEVICE_CONTROL | `{"index":0..5,"on":bool}`；应答带 `v{i}_cmdResult` |
| `/api/valve/snapshot` | POST | 0x0110 | VIEW | — |
| `/api/pump/control` | POST | 0x0114 | DEVICE_CONTROL | `{"index":0,"on":bool}`（新增） |
| `/api/pump/snapshot` | POST | 0x0113 | VIEW | —（新增） |
| `/api/turbidity/snapshot` | POST | 0x0112 | VIEW | —（新增） |
| `/api/flowmeter/snapshot` | POST | 0x0106 | VIEW | —（新增） |
| `/api/temphum/snapshot` | POST | 0x0104 | VIEW | —（新增，可选） |
| `/api/history/page` | GET | 0x0118 | VIEW（export=true 时 DOWNLOAD） | `offset,limit(1..50),beforeTs,loTs,hiTs,kw,kwCols,export`；应答透传 `{ok,total,rows,cols,dbRows,latestTs,earliestTs,windowStartTs,windowEndTs,offset,limit}` |
| `/api/history/export` | GET | 0x0118 批40 逐批 | DOWNLOAD | `format=csv\|xlsx` + 同上筛选键；服务端侧 BatchExport 全量转发，流式回浏览器（见 2.8） |
| `/api/log/page` | GET | 0x0119 | VIEW | `offset,limit,beforeTs,loTs,hiTs,category,kw`；应答 `{ok,rows:[{ts,category,text}],total,count,offset,limit}` |
| `/api/log/export` | GET | 0x0119 批40 逐批 | DOWNLOAD（web 侧闸） | `format=csv` + 同上筛选键 |
| `/api/config` | GET | 0x0116 | VIEW | —；应答 `{ok,collect,storage,login,auto,storageDbPath,storageStats}` 透传 |
| `/api/config` | POST | 0x0117 | ACCOUNT_ADMIN | `{"section":"collect"\|"storage"\|"login"\|"auto", ...键}`；应答透传（局部节回显） |
| `/api/serverlog` | GET | — | 登录 | web 服务器自身 RunLog（诊断用，替代旧 `/api/log` 语义） |
- 错误统一形状 `{"ok":false,"err":"...","status":N}`；控制应答**原样透传**服务端 ok/err（不洗白）。
- 权限闸逐端点查位表（§2.2 矩阵），不再用 `role >= ROLE_ADMIN`；「自动模式拒绝远程控制」的
  预检**只对 0x0101/0x0109/0x0111/0x0114 做下限提示**（0x0115/0x0105/0x0117 不受该闸——服务端
  真闸为准，api.py:606-609 既有注记保留）。

### 2.7 auth.py（会话与登录）
- `role_text` 补四档中文（匿名/普通用户/操作者/管理员，对齐 permissions.ROLE_NAMES；
  骨架 auth.py:87-90 只有 匿名/只读/管理员）。
- `check_control_gate` 改查位表 + 文案对齐 `page_base.control_blocked_reason`（区分
  「自动运行中：…」与「自动模式：…」两种措辞）。
- `?authtoken=` 直登通路（§3）：`GET /*?authtoken=<webtoken>` 由静态壳读取后写 localStorage 并
  立即 `GET /api/me` 校验；token 仍由 `POST /api/login` 颁发（Bearer 或 Cookie 双通道，保留现状）。
  无头截图/自动化用：先 POST /api/login 拿 token，再带 `?authtoken=` 打开页面。

### 2.8 store.py（本地存储）
- `RunLog`（web 服务器自身日志）与 `AlarmLog`（报警页数据源：feed_health/feed_valve_alarm/
  query/active）**保留**。
- `HistoryStore`（自采 JSONL，store.py:173-351）**降级为可选离线兜底**——历史真源在服务端
  SQLite（19 列 HISTORY_COLUMNS），页面查询全部走 `/api/history/page` 转发；web 不再自采
  （自采会与服务端 19 列口径漂移）。`/api/history/files` 端点随之删除或改名（不留「选了不兑现」）。

### 2.9 httpd.py
- 静态服务：`read_static` 的 realpath 前缀防目录穿越**已就绪**（httpd.py:508-532），保留；
  HTML `Cache-Control: no-cache` 保留。
- 下载端点：给 Response 增加「流式 body 生成器」形态（现有 `_CoroBody`+chunk() 已具备，
  httpd.py:414-441），导出响应加 `Content-Disposition: attachment; filename="hsf_history_*.csv"`、
  `Cache-Control: no-store`。
- SSE：`sse_producer` 15s 心跳保留（httpd.py:441-481）。

### 2.10 `__main__.py`（新建入口）
参数（对齐任务口径）：
`--server HOST`（上游 XLink，缺省 127.0.0.1）、`--port N`（上游端口，缺省 6100=ports.C_SERVER_PORT；
连 Python 服务端时显式 `--port 6200`）、`--bind`（缺省 0.0.0.0）、`--web-port`（缺省 8080）、
`--user/--password`（上游主链路登录，缺省 root/root）、`--static DIR`（缺省包内 static/）。
启动顺序：RunLog → SessionStore → WebHub(上游连接+重连) → HttpServer；打印
`http://<bind>:8080/`。上游不在时**照常起 web**（页面显示「未连接服务端」，控制端点回 503），
不崩不退出。

### 2.11 导出 CSV/XLSX 全量取数方案（照 hsf_export_plan.py 口径，两端一致）
- **批行数 40**（`EXPORT_CHUNK=40`）：与页大小档 10/20/50、图表批 50 **两两不相交** ⇒
  在途应答不会认错来源（hsf_export_plan.py:29-35）。
- **逐批连续取**（步长=本批实到行数），不抽样；结束判据 = 短批/空批/写满服务端 total。
- **整轮钉同一 `beforeTs` 锚点**（导出开始那一刻定格；每批各取「当下」会让窗口上界逐批增长 ⇒
  重复/漏行）。历史导出每批带 `export=true`（服务端验 DOWNLOAD）；日志 0x0119 无 export 键 ⇒
  权限闸在 web 端（无 DOWNLOAD 直接 403）。
- **CSV**：流式（边取边写、不限行数）；编码 `UTF-8 BOM(EF BB BF) + CRLF`；最小引号转义
  （含 `"` `,` CR LF 的字段加引号、内部 `"` 翻倍）——与 C 端 HsfExport 字节级兼容口径。
- **XLSX**：工作簿在收尾前都在内存 ⇒ **12000 行上限（`EXPORT_MAX_ROWS=12000`）显式截断**并在
  状态行注明（不许静默丢行）。零依赖实现：Python 标准库 `zipfile` + SpreadsheetML——
  `[Content_Types].xml` + `_rels/.rels` + `xl/workbook.xml` + `xl/worksheets/sheet1.xml`
  （单元格用 `inlineStr`，免 sharedStrings；`xl/styles.xml` 最小集），`zipfile.ZIP_DEFLATED` 写出。
  2000 行内全量写入，超出先截断再写。
- **转换异常的行按原始值兜底导出并计数上报**（行数必须与命中数对上）。
- 导出发起那一刻冻结：列集合/关键字/时间窗/锚点；取数期间用户改筛选不影响本轮。

### 2.12 常驻服务与验收环境
- 上游：`bin/HsfServer.exe --sim`（6100，已在跑，勿动）。
- web：`cd HighSpeedFilter/webui_py/hsfweb && python -m hsfweb --server 127.0.0.1 --port 6100`
  （8080）。**现跑实例是旧代码**（探活 404 证据见文首）——新代码就绪后须重启该进程。
- 联调时「连续两次连接间隔 ~8s」（DenyNew 回收节奏，AGENTS 联调口径）同样适用于 web→上游
  的一次性登录连接；主链路重连由 xlink 自动节流处理。

---

## §3 前端文件约定（`HighSpeedFilter/webui_py/hsfweb/static/`）

零第三方依赖：原生 ES Modules（`<script type="module">`）+ Canvas 2D 自绘图表 + CSS 变量主题。

```
static/
  index.html            壳：chrome（标题栏/导航/状态栏）+ <main id="page"> + <script type=module src=js/app.js>
  css/theme.css         全部配色/字号/间距（取材 theme.py，见下）
  js/app.js             入口：装配 chrome + router + hub
  js/api.js             fetch 封装（Bearer/Cookie、ok/err 统一、超时终态）；EventSource 管理 + POST /api/topics
  js/permissions.js     四档角色×权限位表 + can(role,perm) + role_name（照 client permissions.py）
  js/model.js           全局数据模型（照 client model.py 移植：VfdState/TempHum 按侧/Analog/Digital/
                        FlowMeter 按侧/Turbidity 按侧/Pump/Valve/Health/auto 状态族；updated_ms 新鲜度）
  js/chrome.js          TitleBar（☰ + 产品名「PMMA有机玻璃高速过滤器」 + 时钟 + 温湿度 + 故障N·离线N +
                        模式钮 + 自动运行位）、StatusBar（消息 + 连接态 + 右下「账号·角色 ▾」菜单：
                        切换账户/退出登录）、NavPanel（NAV_TREE 树，分组+子项）
  js/router.js          hash 深链 #/page/N ↔ 页注册表；切页 = 旧页 leave() → 新页 enter() →
                        POST /api/topics(页主题∪全局) → 立即 refresh()
  js/widgets.js         Card / StatBlock / LampRow / ToggleGrid(自适应列数) / DataTable / MultiCheckCombo /
                        TimePicker（日期时间弹层）/ 模态浮层（首页悬浮详情卡）
  js/charts.js          TrendChart（60 点滚动窗/跟随-接管/HH:MM:SS X 轴/Y 5 档）与 LineChart
                        （多序列归一化/等距抽样/单击气泡/滚轮缩放）Canvas 实现；RemainDial 表盘
  js/export.js          CSV Blob（\uFEFF 前缀 + CRLF，前端本地导出报警页用）；XLSX/历史/日志全量导出
                        一律走后端下载端点（§2.8）
  js/pages/page00_home.js    …（每页一个模块，见下）
  js/pages/page01_menu.js / page02_monitor_menu.js / page08_data_menu.js / page12_settings_menu.js
                        （四个菜单页共用 submenu 渲染器，数据来自 GET /api/pages）
  js/pages/page03_digital.js … page15_network.js
                        （2026-11-13 三页合并后：原 page16_overlay.js / page17_window.js
                         已并入 page13_local.js 的三个分区，文件随之删除）
```

页注册表（router 内）：每页 `{index, name, topics:[...], mount(el), enter(), leave(), refresh()}`；
`refresh` 节拍 = `localStorage.refreshMs`（缺省 200ms，仅刷新当前页）。

**配色/字体/间距取材 theme.py（theme.py:31-94）**：
- chrome：标题栏深蓝 `#1F4E79` 白字；状态栏黑底 `#000000` 浅灰字 `#D0D0D0`；导航底 `#F4F6F8`、
  选中 `#DBE7FD` + `#1A66D0`；页底 `#F4F6F8`。
- 控件：白底、描边 `#5A6572`、软描边 `#D5DBE2`、悬停 `#EAF2FF`、按压/选中 `#DBE7FD`、
  强调 `#2E7CE8`/`#1A66D0`、禁用底 `#F0F2F5` 字 `#757575`；控件圆角 4px、卡片圆角 5px。
- 语义色：正常 `#2E7D32`、告警 `#ED6C02`、故障 `#C62828`、离线 `#757575`、信息 `#1A66D0`；
  「通」态禁用底 `#E3F1E4`（QSS disabled:checked 口径，theme.py:294-306）。
- 图表：网格 `#E3E6EB`、轴字 `#5A6572`、序列色 `(#2E7CE8,#E8730C,#2E9E4F,#B33DC6,#C62E2E,#7A6F00,#0F9BA8,#8B5A2B)`
  （pages_other.py:3014-3015）；接管角标琥珀 `#B26A00`。
- 字体族（theme.py:106-126）：`"Microsoft YaHei","SimHei","Noto Sans CJK SC","WenQuanYi Zen Hei",sans-serif`；
  字号 CSS 变量 `--fs-*`，默认 1.0，`uiScalePct`（100/110/125/150）改根字号并重建 QSS 变量。

**本地偏好落 localStorage（对齐 client settings.py 键名）**：
`uiScalePct, forceCompact, fullscreen, refreshMs(100..2000), trendSampleMs(200/500/1000/2000),
winSize("WxH"), showTitleBar, showStatusBar, showNav, histPageSize, logPageSize,
host, port, user, remember`。读写集中在一个 `prefs.js` 式小模块（等价 settings.py 单一事实源）。

**?authtoken= 直登通路**：`index.html` 启动时解析 `?authtoken=<token>` → 写 localStorage.hsfToken →
`history.replaceState` 清掉 URL → `GET /api/me` 校验通过即进主界面（失败清 token 落登录页）。
供无头截图/自动化：`POST /api/login` 拿 token → 打开 `http://host:8080/?authtoken=<token>#/page/9`。

**布局纪律（AGENTS 界面铁律的 web 版）**：
- 内容可变但几何不可变：提示行/横幅**恒占位**（空文案=透明占位，不上色），不用 `display:none`
  改结构（对应阀页「按钮上下跳」实锅）。
- 数值文本同值早退（`el.textContent !== next` 才写），稳态零重排。
- 表头/列宽按字体实测（`canvas.measureText`），不写死像素；放不下出省略号不截半字。
- 定宽文本（如「累计 999+天 23时 59分 59秒」）按最长串一次性 `ch` 定宽，防每秒抖动。
- 交互动作反馈必须三态终态（成功/失败/超时），不许停在「正在…」；按钮禁用态要给原因。

---

## §4 双端服务端差异表（C 端 vs server_py，逐命令/逐主题/逐配置键核实）

### 4.1 命令（19 条，两端**全部已实现**）
| 命令 | 名称 | C 端 | server_py | 差异/备注 |
|---|---|---|---|---|
| 0x0101 | VFD 控制 | HsfServerApp.c:3495 | app.py:2749 | 两端同（op 白名单/急停闸/停机拒给定）。Python 端 `_h_vfd_control` 另有急停四类 op 前置闸（AGENTS ⑦） |
| 0x0102 | VFD 快照 | :3498 | :2885 | 两端回 `ratedRpm/ratedHz` |
| 0x0103 | 模拟量快照 | :3501 | :2902 | 同（`n/v/r{i}/u{i}/e{i}/eu{i}/en{i}/ew{i}`） |
| 0x0104 | 温湿度快照 | :3510 | :2915 | 同（`side/temp/hum/tempValid/humValid/staleMs`） |
| 0x0105 | 运行模式 | :3504 | :3207 | 同；**拒绝也回全量状态键**（C :1229 注释，Python 同批） |
| 0x0106 | 流量计快照 | :3513 | :2922 | 同 |
| 0x0107 | 设备健康 | :3507 | :3346 | 同（`devCount/seq/ts/d{i}_*` 9 键，XhsfLinkProto.h:778-787） |
| 0x0108 | 数字量快照 | :3519 | :2933 | 同（全量帧带 `din%d/don%d`，`full` 键） |
| 0x0109 | DO 控制 | :3522 | :2961 | 同（单点/整组两形态） |
| 0x0110 | 阀快照 | :3525 | :3020 | 同 |
| 0x0111 | 阀控制 | :3528 | :3035 | 同（应答整帧 + `v{i}_cmdResult`） |
| 0x0112 | 浊度快照 | :3516 | :3094 | 同（`v{i}_ntu/raw/online/temp`，缺 temp=无温度） |
| 0x0113 | 泵快照 | :3534 | :3113 | 同 |
| 0x0114 | 泵控制 | :3537 | :3145 | 同（DO6 仅输出无反馈） |
| 0x0115 | 自动控制 | :3531 | :3245 | 同（op=1/2/3/4 + 兼容 `{"on":bool}`；C HsfServerAutoMode.c / Py autocontrol.py） |
| 0x0116 | 配置读 | :3546 | app.py:1476 | **键集差异**：C storageStats 另回 `usedBytes/maxKb`（HsfServerApp.c:2827-2834）；Python `_storage_stats_doc` 未列 maxKb —— web 按 `usedMb` 取即可，建议两端对齐键集 |
| 0x0117 | 配置写 | :3549 | app.py:1487 | 同闸（ACCOUNT_ADMIN）。**生效时机口径**：C 头 0x0117 注释（XhsfLinkProto.h:220-227）写「collect 落 INI 重启生效」，而 259-271 行写 8 个开关已**运行期消费**（HsfServerIo.c applyCollectConfig，2026-11-11）——周期键是否运行期生效需实测确认；Python 端 collect/storage 全部运行期生效+落盘（app.py:1487-1560） |
| 0x0118 | 历史分页 | :3540 | :3352 | 同（19 线上列/锚点/kw/kwCols/export 闸）。C 端 16 显示列另有 DISPLAY_EXPR（服务端算显示文本），Python 端格式化在客户端 —— **web 端取 Python 口径**（发原值、前端格式化，§1 页 9） |
| 0x0119 | 日志分页 | HsfServerApp.c:2494,3543 | app.py:3455（dispatch :2719） | **两端都已实现**。旧口径「server_py 缺 0x0119」已过时：`XhsfLinkProto.h:453` 头注释「Python 侧暂无同号命令」、client proto.py:443-458 注释「server_py 不实现该命令」均为历史记述，**建议订正这两处注释**（防下一个人再照抄）。类别字面两端一致（conn/ctrl/alarm/auto/auth），Python 另产 `sys` 类别（proto.py:755 LOG_CAT_SYS；C 端不产生 sys 行 ⇒ 选中恒空，已容错） |

### 4.2 主题（推送）
| 主题 | 值 | C 端 | server_py | 备注 |
|---|---|---|---|---|
| VFD/AI/TEMPHUM/FLOWMETER/HEALTH/DIGITAL/VALVE/TURBIDITY/PUMP | 0x00010001..07,09,0A | ✔ | ✔ | 两端同；web `TOPIC_KIND` 必须九个全登记（hub.py:65 现缺 turbidity/pump） |
| 阀告警（合帧） | 随 0x00010007，`ac` 键 | ✔（hsfAppPushValveAlarms :4517） | ✔ | 同构键布局 `{ac:N, a{i}_vi/vn/k/t/at/to}` |
| TOPIC_VALVE_ALARM | 0x00010008 | 遗留不推 | 遗留不推 | XhsfLinkProto.h:1033-1050 明示「新代码勿再使用」；web 也不订 |
| TOPIC_AUTO_ALARM | 0x0001000B | ✔（HsfServerApp.c:4677） | ✔（app.py:1741） | 载荷 `{kind,atMs,text}`；两端都有，web 收了转 `alarm` 事件 |

### 4.3 配置节键
- `collect`：8 开关 + 8 周期键逐台同名同值（XhsfLinkProto.h:259-287 ⇔ server_py proto.py:521-543）——已对齐。
- `storage`：`enabled/path/sampleMs/maxMb/fullPolicy/keepFiles/historyTrigger` 同名；缺省
  `sampleMs=60000/maxMb=50` 三处同源（XhsfLinkProto.h:322,331 ⇔ server_py proto.py:633,637）——已对齐。
- `login`：3 键同名（XhsfLinkProto.h:340-342 ⇔ proto.py:648-650）——已对齐。
- `auto`：`modes{produce,backwash,rinse}`+`cycle{enabled,pressThreshold,pressCompare,timerFallback,
  timerMinutes,backwashMinutes,rinseMinutes}` 同名同缺省（XhsfLinkProto.h:365-388 ⇔ proto.py:588-621；
  C HsfServerAutoMode.c / Py autocontrol.py）——已对齐。
- **权限/角色**：四档角色 + 6 权限位两端逐位一致（XhsfLinkProto.h:691-706 ⇔ server_py proto.py:95-107
  + permissions.py；client permissions.py 镜像）——已对齐。**骨架 webui 缺整组**（§2.1 补）。
- **缺省账户**：admin(普通)/oper(操作者)/root(管理员) 两端名单逐字一致
  （server_py proto.py:929-938；C `XAccountManager_ensureDefaults`）——已对齐。
- **数据面差异（服务端存储）**：server_py 另有 `alarms` 表（store.py:104，报警记录持久化），
  C 端无同表——但两端**报警页口径一致**（客户端边沿记录），web 照 AlarmPage 口径做，无需补 C 端。
- **告警帧 atMs 口径**：C 端阀/自动告警的 `atMs` 是**墙钟毫秒**，Python 端阀告警的 `atMs` 是
  **monotonic 毫秒**（webui 2026-11-13 实测：阀超时告警 `at≈开机毫秒` ⇒ 报警页时间列 1970）。
  web 端已在 store.py `AlarmLog._append` 做下界闸（< 2020-01-01 视为非墙钟，退回「收到时刻」——
  参考客户端报警页本就按本机收到时刻记账）。**两端协议值不一致是真缺陷**，修 py 端不在 webui 阶段做。
- **TOPIC_AUTO_ALARM（0x0001000B）C sim 下不达订阅者**（2026-11-13 实测）：裸 XLink 客户端直连
  6100 订阅 0x0001000B 后经 0x0115 op=3 触发真急停（服务端 logs 表有「自动运行：急停（已锁存…）」
  行），订阅者**零帧**；对照组浊度主题照常到帧。同链路 Python 端（6200）推送正常（app.py:1741-1765）。
  疑似 `hsfAppPushAutoAlarms`（HsfServerApp.c:5006）publish 环节或报警队列出队在 sim 构建下不生效——
  **C 端修复不在 webui 阶段做**（路由给框架/服务端工匠），web 报警页的健康/阀告警源不受影响。
- **【2026-11-13 实测缺陷】C sim 僵尸会话不回收 ⇒ 全账户登录被拒**：客户端异常死亡后 C sim 侧
  套接字滞留 CLOSE_WAIT 不关（对端进程已死、无任何 ESTABLISHED），此时 admin/root 登录一律
  「该账户可能已在别处登录，或会话数已达上限」（凭据离线核对正确、策略 allowConcurrent=true/
  maxSessions=8 都正常），等 3 分钟不自愈 ⇒ 只能重启 C sim 恢复（口径见 verify/webui/README.md）。
  py 端同样场景会回收 ⇒ **两端向外行为不一致**，疑似 XLinkServer 未接 disconnected 一类老缺陷
  在粗暴断开路径复发；C 框架修复路由给独立阶段（要 vcvars64 重编）。恢复实录：taskkill /F
  PID 42924（旧 sim）→ `HsfServer.exe --sim` 原样拉起 → smoke_backend.py 39/0 全绿。

**结论：双端协议面 19 命令/10 主题/4 配置节已对齐**（除 4.1 表中 0x0116 storageStats 键集与
0x0117 collect 生效时机两处口径需实测/订正注释外，无缺命令缺主题）。改造重心不在服务端，而在
**webui 骨架的过期 proto/api/model 与缺失前端**。

---

## §5 截图对比方案与风险顺序建议

### 5.1 截图对比方案
1. **基准图**：PyQt 客户端离屏截图（现成能力）：
   `QT_QPA_PLATFORM=offscreen D:/anaconda3/python.exe HighSpeedFilter/client_py/hsfsrv_client …
   --auto 127.0.0.1 --port 6100 --shot out.png --page N`（client __main__.py:5-7,43-45；
   `--size 1280x720` 对齐 web 视口）。16 页各截一张 → `verify/shots_py/pageNN.png`。
2. **网页图**：无头 Chromium/Edge `--headless --screenshot=… --window-size=1280,720
   "http://127.0.0.1:8080/?authtoken=<token>#/page/N"`（token 由 `curl -X POST /api/login` 取）；
   每页截 `verify/shots_web/pageNN.png`。等 2~3s 让 SSE 首帧+轮询填充后再截
   （浏览器 `--virtual-time-budget=5000` 或截图工具的延迟参数）。
3. **对比口径**：人眼+分区块核对（本盘点不引入图像依赖）：页名/导航树逐项、卡片布局层级、
   状态色语义（绿/橙/红/灰）、表格列序与格式（`%.2f`/`0x%08X`/`--`）、按钮使能态。
   **数据动态**：先连 `--sim` 服务端 10 分钟让历史/日志/报警有真实数据再截，空库截图会误判
   「页面坏了」（AGENTS 空库联调口径）。
4. **自动化判定**（可选）：用 Playwright/浏览器自动化取每个页面的 DOM 文本快照与 Qt 客户端
   `--auto` 模式的 model 打印对账（同源数据应逐值一致），截图只做版式复核。
5. **端到端功能复检脚本**（仿 `verify/hsf_export_filter_check.py` 惯例，纯 stdlib）：
   起 sim → `urllib` 调 REST 面（登录→订阅→控制→分页→导出）逐条断言 → 退出码表成败；
   导出断言「CSV 行数==命中总数、跨页不重不漏、无权限 403」。

### 5.2 风险顺序（建议实现/验收顺序：先通链路，再补页面，先数据后控制）
1. **R0 协议基座**：proto.py 全量重写（§2.1）——一切对齐的地基；同时补 permissions 表。
2. **R1 链路打通**：model 补 turbidity/pump/按侧/auto 族 + hub TOPIC_KIND 补登记 +
   `/api/state`、`/api/stream` 首帧全量。**验收**：页 0 主界面十张卡有真值（sim 下）。
3. **R2 前端壳**：index.html + chrome + router + theme.css + 登录页 + `?authtoken=`。
   **验收**：#/page/N 深链、切页订阅收敛、状态栏账户钮三态。
4. **R3 只读页**（低风险批量）：页 4 模拟量、页 3 数字量(读)、页 6 水质、页 7 阀(读)、
   页 15/16/17 设置三小页。
5. **R4 数据三页**（中风险）：页 9 历史（分页锚点/kw 下推/图表全量/导出）、页 10 日志
   （0x0119 转发/归属判定）、页 11 报警（边沿+分页）。导出 XLSX 用 zipfile 方案单独验
   （Excel 能打开、12000 截断有提示）。
6. **R5 控制页**（高风险，权限与闸先行）：页 5 搅拌泵（启停/调速去抖/点动代持）、
   页 7 阀控制、页 3 DO 写、页 6 泵控制、页 0/14 自动控制与配置。**验收必须逐条过闸**：
   匿名 403、admin 可看不可控、oper 可控设备但 403 模式/自动/配置、root 全通；
   自动模式下控制 403 且文案区分「运行中/未跑」。
7. **R6 页 14 远端设置**（配置写 + 局部回显纪律 + 三节独立应用）。
8. **R7 收尾**：16 页截图对比、`verify/` 端到端脚本、临时诊断打印清零、重启 8080 常驻、
   订正两处「server_py 缺 0x0119」过时注释（XhsfLinkProto.h:453、client proto.py:443）。

### 5.3 主要风险点（盘点实测依据）
- **8080 旧进程**：现跑代码无 /api/ping（探活 404），新代码落地必须重启，否则验收打在旧实例上
  （AGENTS「重启服务端必须验证旧进程真死了」口径）。
- **权限位表**：骨架 `role >= ROLE_ADMIN` 在 Operator=3 下行为颠倒（auth.py:69-70,241；
  api.py:607），上线前必须换查表。
- **按侧分存**：不实现则温湿度/流量计/浊度会被空帧盖掉（client model.py 实锅记录），
  页 0/4/6 恒 `--`。
- **订阅收敛**：hub 按并集订阅上游——多标签页各自 topics 并集；漏登记 turbidity/pump 主题
  会整页静默（bridge.py:59-64「漏一个就没数据」同款）。
- **XLSX 零依赖**：zipfile 方案需先做「最小可开」验证（Excel/WPS 双开），不行就只留 CSV 并删
  下拉（不留「选了不兑现」）。
- **导出锚点**： forgetting beforeTs 冻结会逐批重复/漏行（hsf_export_plan.py:18-21 实锅记录）。
