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
  为准：阀 10s 行程/20s 超时、泵 DO6 仅输出无反馈、浊度仪站 1/3 约 2.29 / 0.045 NTU（字节序不同：
  站1 CDAB、站3 DCBA（原站 2，2026-11-10 面板改站号 3），站2 另有温度 ~19.3℃ 走 0x0000×4 同刻快照）、
  流量计站 5 约 3.6 m³/h ±2%（单位码 5=m³/h 随数据上行、空管报警
  0x1024；2026-11-09 板端 ttyS4 实测口径，取代旧 1/3·12.5/3.2·36.5，
  站号在 config 可改，详见 docs/HSF板端ttyS4三设备取数实测总表_20261109.md）、变频器爬坡按 ACC/DEC 满量程
  时间（0xF011/0xF012 缺省各 10.0s ⇒ 5Hz/s）、运行中换向＝先减速过零
  再反向加速、运行中禁点动（服务端闸拒绝+两端按钮置灰，点动保持中的
  心跳例外）——两端模型 `XVirtualModelVfd`/`VirtualVfd` 同口径，改一侧
  必须同步另一侧与 `xmodbus_virtual_test.c` ①b/`test_units.py`
  TestVfdReversal 两组镜像用例）。
- **两端协议常量**：C 侧 `HighSpeedFilter/common/XhsfLinkProto.h` 与
  Python 侧 `server_py/hsfsrv/proto.py` 必须逐值同步，注释互相点名。
- **安卓「多应用」模型（2026-11-09 定案，新增安卓应用照此接线）**：
  平台层 `platforms/android/` **不认识任何具体 app**；一个安卓应用 = 一个
  「应用目录」，内含 `AndroidManifest.xml`（应用 id/库名/应用名的**唯一
  事实源**，打包脚本从它派生包名与 .so 名）+ `app.env`（CMake 目标 /
  构建目录前缀 / 产物名）。应用目录**随应用走**（demo 在平台目录本身；
  HSF 客户端在 `HighSpeedFilter/client/android/`）。
  - **壳与 app 无关**：`Drive/Android/android_main.c` 只认编译宏
    `XPAD_APP_ENTRY`（入口符号）/ `XPAD_APP_TAG`（logcat tag）/
    `XPAD_APP_ARGS_ENV`/`XPAD_APP_ARGS_FILE`（参数来源），由各目标的
    `target_compile_definitions` 注入。**新增安卓应用不改壳文件**。
  - **一棵树只服务一个应用 id**：字体目录宏 `XFONT_EXTERNAL_FT_FONT_DIR`
    被静态库内的 `XFontFt.c` 消费 ⇒ **构建树级**常量；两应用共用一棵树
    必串（「字库释放到 A、FT 找 B」→ 中文整片缺失，真机事故）。故每个
    应用一个构建目录（demo `out/android-<abi>` / HSF `out/android-hsf-<abi>`），
    且 `-DANDROID_APP_PKG` 必须等于该应用清单里的 `package`。
  - **新增安卓壳文件会炸**：`Drive/CMakeLists.txt` 的 `GLOB_RECURSE` 会把
    新增 `.c` 收进主库 → 动态库 `--no-undefined` 链接因找不到应用入口而
    失败。要新壳就改宏开关，不要加文件。
  - 详见 `platforms/android/README.md` §一「一个安卓应用 = 一个应用目录」
    与 §六「高速过滤器客户端在安卓上」。
- **HSF 客户端上安卓（2026-11-09）**：`HighSpeedFilter/client/**` 是**纯
  TCP/XLink 客户端**——零串口、零 Modbus 主站、零本地 DB（唯一 `XIoChannelGroup`
  是离线演示的进程内假从站）；485 采集/滤波/落盘全在服务端。客户端与桌面
  `HsfClient` **共用同一份源码**（安卓下 `main` 改名 `hsf_client_main`）。
  竖屏适配**在既有客户端 UI 文件上就地改造**（不新开页面文件）：全局 compact
  按窗宽 <860 判、13/18 页有 `_adapt` 按页根宽细分档、菜单页移动态树面板、
  每页字号 50~200% 实时重放——这些 2026-11-09 三档化已具备，本次只补
  「安卓窗口手机形 / 字体走家族名 / 设置落私有目录 / 登录窗滚动壳」。
- **MSVC 编译注意**：源文件 UTF-8（CMake 已开 `/utf-8`，见 `CMake_Config.cmake`）；
  向 C 字符串写 `\n` 时严禁在 bash heredoc 里转义（会被吃掉变成真实换行
  导致 C2001）——用脚本文件或 chr(92) 拼接。
  **块注释里禁写 `*/` 序列**：如 `**/system/bin**`（粗体包路径）里的
  `*/` 会提前闭合注释 → 后续中文变代码 → C2143「缺少'{'」+ 一串
  C3872/C3873「字符不允许在标识符中」。这类报错看着像编码问题，实际是
  注释被截断（2026-11-09 实锅）。
- **未初始化局部量在 Debug 构建会「冻住」进程（2026-11-10 实锅）**：
  MSVC Debug 默认带 `/RTC1`（含 `-RTCu` 局部变量初始化检查）——**读一个
  未初始化的局部量**（哪怕只是拿去 printf）就会触发运行期检查失败 ⇒
  `DebugBreak()` ⇒ 进程被 JIT 调试器（`vsjitdebugger.exe`）挂住等人工
  附加 ⇒ 现场表现为「服务端启动后第一条 Modbus 请求就把进程冻住，客户端
  读不到任何快照、stdout 停在调用前一行、CPU 0%」。**排查口诀**：日志
  停在某行且进程活着不动 ⇒ 先查那一行读了什么没初始化的量（本轮的
  `[vsim]` 诊断读了尚未赋值的 `fc`）。同理，**结构体字段漏初始化**会让
  后续逻辑读到栈垃圾（Debug 下是 0xCCCCCCCC）——`XModbusHubUnitLimits`
  漏 `memset` ⇒ `m_preTxQuietMs` = 34 亿毫秒 ⇒ 该从站读组**永远**轮不到
  （现场：变频器整站哑掉、失败计数=0、游标卡死）。**凡是"栈上局部结构体
  交给框架"的调用点，一律先 `memset`。**
- **Modbus Hub 读组活性三条契约（2026-11-10 框架级根修）**：
  ① **在途必须有超时收口**：从站对某组不应答（真机 M512 对 0xFA00×5 无
     应答）⇒ 在途标记永挂 ⇒ 单飞闸把该设备后续**所有**读组永久堵死。
     现由 `XModbusHub_setInflightTimeoutMs` / 客户端超时 / 缺省 1000ms
     三级取时限，超时即判失败 + 记失败计数 + 报从站退避 + 释放在途标记，
     **一个组的沉默拖不垮整条总线**。
  ② **异常应答是组级事实**：EXC02/EXC03 只进失败计数，**不进从站退避**
     （否则连续 3 次后该从站所有组一起静默 ⇒ "一台设备整体离线"）。
  ③ **静默闸不得饿死从站**：`m_preTxQuietMs` 超上限会被夹到 2000ms，
     且某从站被静默闸连续挡住超 2s 会**逃逸放行一帧**。
  回归锁在 `Test/XProtocolTest/hsf_regression_test.c` §M（M1/M2/M3）。
- **栈数组长度必须与"列数常量"绑死（2026-11-10 实锅）**：历史页显示列
  15→16，三处 `uint32_t cols[15]` 栈数组没跟着改 ⇒ `histVisibleCols()`
  写满 16 个 ⇒ 越界 4 字节 ⇒ MSVC `/RTC1` 弹
  `Run-Time Check Failure #2 - Stack around the variable 'cols' was corrupted`
  ⇒ **客户端启动即崩**（历史页在 `HsfUi.c` 启动时全部建页 ⇒ 崩在开机）。
  治法不是"记得改长度"，而是**编译期断言**：
  `typedef uint32_t HistCols[HSF_HISTORY_DISPLAY_COLS];` +
  `typedef char assert_t[(sizeof(HistCols)==sizeof(uint32_t)*N)?1:-1];`
  **两个坑**：① 本项目是 C99，MSVC 默认 C 模式**不认 `_Static_assert`**
  ⇒ 用**负数组长度 typedef 断言**（两家编译器都生效，不匹配报 C2118/C2148）；
  ② **C 里 typedef 的数组名不可下标**（`sizeof(HistCols[0])` 非法）
  ⇒ 文件级断言只能比 `sizeof(HistCols)==sizeof(uint32_t)*N`，
  调用点宏（真变量）才能下标校验。
- **陈旧阈值必须随采集周期联动，且"设备层+站级层"两层都要改**
  （2026-11-10 板端实锅）：现场把温湿度周期改成 1 分钟（60000ms），
  固定 stale 5000ms ⇒ 界面在「---」与真值之间反复闪。口径
  `stale = max(设备基线, 周期 × HSF_STALE_FROM_POLL_MULT(=3))`
  （倍数 3 = 允许连丢 2 拍）。**只改设备层修不好**：站级质量判定
  （`HsfServerIoTempHum/Turbidity/FlowMeter` 的 `pointValid`）仍按编译期宏
  ⇒ 必须给各站结构体加 `m_staleMs`、挂载时赋值、运行期改周期时同步。
  变频器另有基线 `HSF_VFD_STALE_MS=3000`：Hub 每拍只发**一个**读组，
  现场变频器 14 组 × 200ms = 2.8s 一圈 ⇒ 1000ms 连一圈都盖不住。
- **SQL 条件缓冲长度要按"最宽一行"算，且构造失败必须 fail-closed**
  （2026-11-10 实锅）：`kwCond[1536]` 而 16 列条件实长 **1544 字节**
  ⇒ 截断 ⇒ SQL 语法错 ⇒ 查询返回 0 行，**且毫无错误提示**（用户看到的是
  "筛不出东西"）。修法：缓冲放大到 8192 + 构造函数返回 bool，
  失败即**显式记日志并返回 0 行**，不让截断 SQL 悄悄跑。
  缓冲区不足的 bug **不会自己暴露**——必须主动构造超长条件去试。
- **XString 是内码缓冲**：跨进程序列化/拷贝一律 `XString_toUtf8`。
- **新鲜度阈值必须按「数据真实刷新节拍」算，不能按「发布周期 × N」**
  （2026-11-10 现场"变频器 --- 与真值循环闪"根修）：Modbus Hub **每拍只发
  一个读组** ⇒ 数据的刷新节拍是**轮转一圈**（该总线读组数 × 拍长；板端
  变频器 14 组 × 200ms ≈ 2.8s），不是采集周期。设备层缺省阈值
  「发布周期 × 5」=1s **盖不住一圈** ⇒ 每隔两拍把快照判陈旧、掩码清零 ⇒
  客户端遥测闪 `--`。口径：阈值 = max(设备基线, 轮转周期 × 2)，由装配层
  按**Hub 的真实拍长**算出来下发（`XVfdDevice_setStaleMs` /
  `HsfServerIoVfd_applyStaleMs`；拍长取 `XModbusHub_pollInterval`）。
  温湿度/流量计/浊度同理（`hsfIoStaleMs`）。
- **应答里没有的字段不要当成「已关闭」**（C 端 2026-11-10 补齐，Python 端
  早有）：远端设置里「落盘」勾选框**构造态是未勾选** ⇒ 服务端快照没到就点
  「应用」⇒ 把 `storage/enabled=0` 写回服务端 ⇒ 落盘被静默关闭 ⇒ 历史表格
  从此空（现场"表格数据咋没了"）。修法：`HsfConfig.m_storagePresent` 记录
  应答里**确实有** storage 节，没收到就不写这一节；勾选框缺省与服务端口径
  一致（勾上）。另外**失败包络不是分页文档**：服务端回
  `{"ok":false,"err":"落盘未启用…"}` 时，客户端要认出来并把原文显示在状态行
  （`HsfHistoryPage.m_error`），不能只剩「共 0 条 + 读取中」。
- 构建入口：`build_hsf.bat`（HsfServer/HsfClient，build-hsf/）；
  测试宿主 `XinYueC_Static`（out/build/x64-Debug），跑法
  `bin\XinYueC_Static.exe --test <命令>`。
  **所有 MSVC 构建必须经 `vcvars64.bat`**（`/tmp/build_reg.bat`、
  `build-hsf/build_static.bat` 都是这个套路）；直接在 Git Bash 裸调
  `cmake --build` 会因缺 `INCLUDE` 报 `stdbool.h`/`stdint.h` C1083。
  `XGuiRegression_Test` 与 `XinYueC_Static` 在 `out/build/x64-Debug`，
  `HsfServer`/`HsfClient` 在 `build-hsf/`，两套互不覆盖，可分别重编。
- **板端（RK3568）一键链路**：`Tools/posix/hsf-board-deploy.sh`
  （Windows Git Bash 入口）= 同步工作区 → WSL 交叉编译 → scp 部署 →
  板端重启 → 复检，一条命令跑完（板 192.168.1.249:6100，路 A
  `/dev/ttyS3`、路 B `/dev/ttyS4`）。配套：`verify/board/build_board.sh`
  （WSL 内编译）、`verify/board/restart_c_stack.sh`（板端起停 C 栈）、
  `verify/board/verify_temphum_stale.py`（温湿度「数据过期」复检，零 Qt
  依赖）。三条纪律：**板端 C 栈没有 systemd 单元**（只有
  `hsfserverpy`/`hsfclientpy` 两个 Python 服务）⇒ 重启按手工 `nohup`
  口径；**同账并发默认关** ⇒ 复检前只停客户端、不能停服务端（停了就没
  帧可采）；**运行中的二进制不能被 scp 直写**（ETXTBSY）⇒ 先停再落盘或
  传 `.new` 后 `mv`。
- **板端手搓部署（2026-11-10 实操补充，一键脚本之外的细活）**：
  ① **端口/总线开关必须走编译器宏**（`-DCMAKE_C_FLAGS="-DHSF_BUS_A_PORT=
  \"/dev/ttyS3\" …"`）——当 CMake **变量**传是**无效的**（真实机制见
  `verify/board/build_board.sh`）⇒ 传错则二进制用 Windows 缺省 COM17/COM18
  ⇒ 板端 485 全离线（"服务在跑但一个设备都不在线"）；
  ② **WSL 同步**：`rsync -a --delete --exclude='.git' --exclude='.vs'
  --exclude='out' --exclude='bin' --exclude='.zcode' --exclude='docs'
  /mnt/f/... /root/xinyuec-src/`——**`.vs` 必须排除**
  （`FileContentIndex/*.vsidx` 权限拒绝 ⇒ 整条 rsync 失败）；
  ③ `wsl.exe` 会把 `/root/...` 啃成 `D:/Program Files/Git/root/...`
  ⇒ WSL 内执行的脚本先 `cp` 进 WSL 本地再 `bash /root/x.sh`，
  且一律 `wsl.exe -e bash -lc "…"`；
  ④ **SSH 无免密** ⇒ 用 askpass：`printf '#!/bin/sh\necho <口令>\n' >
  /tmp/askpass.sh` + `export SSH_ASKPASS=/tmp/askpass.sh
  SSH_ASKPASS_REQUIRE=force DISPLAY=dummy:0`；
  ⑤ 板端 C 栈用 `systemd-run --unit=…` 起的是 **transient unit**，
  `systemctl stop` 后单元即消失 ⇒ 重启必须**重建**；
  ⑥ 板端截图：`xwd -root -silent` → `verify/xwd2png.py`（纯标准库，
  无 PIL）转 PNG，再 `scp` 回本地看。
- **HSF 跨端联调（改服务端快照键/字段名/设备类别必跑）**：
  `bin\HsfServer.exe --sim`（默认 6100）+
  `QT_QPA_PLATFORM=offscreen <python> HighSpeedFilter/client_py/tests/test_client.py --auto 127.0.0.1 --port 6100`
  （需 PyQt5；本机可用 `D:\anaconda3\python.exe`）。
  VFD 组合链路：`tests/test_vfd_combo.py --port 6100`。
  连续两次连接间隔 ~8s（DenyNew 下旧会话按静默超时回收，立即重连会被拒）。
  已修复的 C/Python 口径：健康快照必须含进水泵/数字量/电动球阀组条目；
  数字量 DO6 名必须是"进水泵"、阀通道名为"<阀名> 控制/开到位/关到位"
  （2026-11-06 .12 聚合器拆除、点表收窄为 16 点：阀 1~6=DO0~DO5、
  进水泵=DO6、按钮组=DO7~DO9；旧口径 DO21/DO22 是聚合编号，已作废）。
  停机斜坡期运行位必须报 False（「正在停机」口径，两端一致）；
  客户端重连后必须重发订阅帧（xlink 去重集合随连接作废，2026-11-07 修）。
- **运行模式与按钮灯口径（2026-11-10 用户定案，两端同口径）**：
  ① **手动模式不跑自动程序 ⇒ 按钮组灯归人工**：服务端按钮站**每拍写灯**
  是老口径（灯态由「急停+自动运行位」推导）⇒ 人工点的灯每拍被覆盖，
  现场表现「手动模式下无法单独控制按钮灯」。现改为：灯只在**自动模式**
  跟随运行态；手动模式自动逻辑**一个 DO 都不写**（采集/急停/停止仍照做，
  那是安全件）。
  ② **切换模式 ⇒ 关闭所有设备**（手动⇄自动**双向**）：切自动也要全停
  （自动模式拒掉全部远程控制、控制器又等人按"启动"，设备若还转着现场
  既停不掉也看不到谁在驱动它）；切手动同样全停。**同一模式重复下发
  不动设备**（手动模式下设备正由人操作）。
  ③ **只有按「启动」后设备才允许工作**：模式开关只是授权，**不是**启动
  按钮——切自动不启动控制器（自动运行位保持"已停止"）。
  ④ 锁：服务端 `tests/test_mode_lamp_rules.py`（12 项）+ 客户端
  `test_units.py` 的灯用例（自动/手动两态都断言）。
- **设备命名与菜单分组（2026-11-10 用户定案，两端同口径）**：
  ① 阀 = **电动球阀**（现场就是 6 台规格一样的电动球阀，不是"电磁阀"，
     也没有加药/排气这类功能差别）：阀名统一 **电动球阀1~6**，阀组条目名
     **电动球阀组**。两端都要改：服务端 cfg 阀名 + 缺省名（C
     `HsfServerIoValve` 的组内缺省名、Python `valves.py`/`config.py` 的
     兜底名）、客户端页标题/卡片名/导航名、健康表里的设备条目名。
  ② 6 路各占各的点位（第 i 路 i=0..5）：**DO=i、开到位 DI=2i、关到位
     DI=2i+1**（DI0~11 = 每阀一对）。板端 cfg 曾把第 5、6 路**复用**了
     第 1、2 路的 DO/DI ⇒ 界面上多出两路"跟着别人动"的假阀、动作超时也
     成对重复报。
  ③ 动作超时 **20s**（仓库 cfg 与 C 端 `HSF_VALVE_MOVE_TIMEOUT_MS` 均为
     20000）：真机行程约 9s，8s 必然误报「动作超时未到位」。
  ④ 客户端菜单分组「**监视**」改名「**设备**」（导航组、菜单页标题、
     首页快捷入口，两端同改；页名/导航树有跨端同形断言，改名必须同步改
     `client_py/tests/test_hsf_regression.py` 的 `c_tree`/`c_names`）。
- **浊度仪温度与流量计报警文案（2026-11-10）**：带温度的机型（浙仪
  ZHEYI-5100ZDY，站3）随帧下发 `v{i}_temp`（℃×100，**缺键=该机型无
  温度，不是 0℃**）；主页浊度卡与水质监测页都显示水温。流量计报警码
  **直接说中文**（说明书口径：上限/下限/空管/系统），不显示 "0x0004"；
  顺序照 C 端 `fmAlarmText`：空管→上限→下限→系统。
- **两台浊度仪是两台独立设备（2026-11-10 用户定案，两端同口径）**：
  ① **采集开关逐台**：`turbidity1Enabled` / `turbidity2Enabled`（C
  `HSF_CFGK_TURBIDITY{1,2}_ENABLED`、两个 proto.py 同名常量），旧的
  `turbidityEnabled` 降为**共同缺省**（缺逐台键时回落它；旧客户端只发
  它时服务端视为"两台一起"）。远端设置页两行各勾各的，**禁止**再做
  「勾一台同步另一台」；周期本来就逐台（`turbidity{1,2}PollMs`）。
  ② **语义**：停用的台只停该从站轮询，另一台照采（Python 健康条目文字
  「已停用」；C 端健康由 Hub 数据新鲜度推导 ⇒ 显示数据过期/离线，口径
  差异已记，别把停用当故障去查接线）。C 服务端没有 Hub 级 enabled 字段
  ⇒ 停用＝把该从站 `m_minPollIntervalMs` 顶到 `HSF_IO_UNIT_DISABLED_MS`
  （≈24.8 天，`hubUnitDue` 永不到期）；重新启用同一入口写回正常周期。
  ③ Python 服务端设备对象**无论开关都建**（逐台开关只门控轮询），
  运行期改开关 `restart_polling` 立刻生效；sim 下停用的台不建模。
- **历史表新增「水温(℃)」列（2026-11-10，两端同口径）**：
  ① **线上列序追加在末尾**：`history.water_temp`（19 列）——
     `HSF_HISTORY_WIRE_COLS`（`common/XhsfLinkProto.h`，**单一定义**，
     服务端 `HSF_HISTORY_COLUMNS` 就是它）与 Python `store.HISTORY_COLUMNS`
     同序。追加而非插中间：行值按**下标**发给 C 客户端
     （`HsfLinkHistory.c` 的 `kSrc/kDst`），插中间会平移既有下标。
  ② **显示列序另有一套**（16 列）：水温插在第 8 位（紧跟「浊度2」）——
     服务端 `DISPLAY_COLUMNS/DISPLAY_EXPR`（PY+C 各一份、逐条同口径）、
     C `kHistoryHeaders[16]`、PY `HistoryPage.COLUMNS` 三处同步。
     显示列右移后 C 客户端 `HIST_NUM_LAST` 12→13（水温也画折线）。
  ③ **老库必须迁移**：`CREATE TABLE IF NOT EXISTS` 不动已存在的表 ⇒
     SELECT 点名 water_temp 会整页失败。C `storeEnsureHistoryWaterTemp`
     / PY `_migrate_history_water_temp`（PRAGMA 探测 + ALTER TABLE）。
  ④ **无温度机型入 NULL**（显示 "--"），不写 0℃。夹具里那份 18 列老载荷
     保留作**向后兼容**证据（水温格缺失 → "--"）。
- **远端设置：局部应答只刷新自己那一节（2026-11-10 实锅）**：
  `RemoteSettingsPage._populate(doc)` 里「当前库文件」「用量」两行原来
  **无条件**按 `storageDbPath/storageStats` 刷新 ⇒ 点「应用登录策略」
  （应答只带 login 节）就把数据存储卡清成「用量：--」「未启用（落盘关闭）」，
  切页再回来才恢复。原则：**应答里没有的字段不要当成"已关闭"**，
  只在键存在时才刷新（回归锁 §19 A4a/A4b/A4c）。
- **数字量页灯阵/按钮阵列数必须随宽度自适应（2026-11-10 用户「显示不全」）**：
  `LampRow` / `ToggleGrid` 原来固定 8 列 ⇒ 窗宽不足时每格被压窄，
  第二行语义名被裁（"电动球阀1 控制" 只剩半截）。现在走
  `page_base.adaptive_columns(控件, 上限 8, 每格最小宽)`，每格最小宽按
  **当前字体实测**最长语义名 + 26px（不写死像素，字号缩放后仍成立）。
  宽屏仍 8 列（与 C 端口径一致），窄窗自动折成 6/5/4 列 —— 宁可多几行不裁字。
  回归锁：`run_client` 段的 `adaptive_columns` 判定断言（4 项）。
- **历史页图表必须「页内双视图 + 全量取数」（2026-11-10 用户定案）**：
  ① **图表不弹独立窗**：`HistoryPage.set_view(0|1)` 用 `QStackedWidget`
  在页内切表格/图表，`LineChartDialog` 以 **Qt.Widget 旗标**内嵌（与报警页
  `set_view` 同款）；「生成折线图」按钮已换成「表格视图 / 图表视图」两个
  可勾选按钮。**不要再加弹窗式图表**。
  ② **图表数据不是"当前页"**：切图表视图时按**当前筛选条件分页累加全量**
  （`limit=proto.HISTORY_PAGE_MAX` 50/批，offset 递增，短批即停），上限
  `CHART_MAX_ROWS=12000`（对齐 C 端 `HIST_CHART_MAX_ROWS`）。旧实现只画
  当前页 ≤50 行，趋势看不出来。
  ③ **等距抽样**：每条线最多 `CHART_MAX_POINTS=2000` 点、**首尾必留**
  （对齐 C 端 `histSamplePoints`）；状态行写明「筛选共 N 行 · 图表取回 M 行
  · 每线最多 K 点（是否抽样）」。
  ④ **筛选变化要重取**：页面应答尾部 `if view_mode == 1: _chart_refresh()`；
  用筛选签名（lo/hi/kw/列/before_ts）避免同条件重复跑上百批请求。
  ⑤ 回归锁：`client_py/tests/test_hsf_regression.py` 的 `run_histchart()`
  （§14b，17 项，已挂进 `merge_history_page`）。
  **本机验收配方**：`bin/HsfServer.exe --sim 6100` + 离屏脚本（真窗口
  `render()` 出图，别用 `--shot`——离屏收不到 XLink 推送）。
- **实时趋势图（搅拌泵页 / 水质页）轴与刷新率（2026-11-10 用户定案）**：
  ① **X 轴必须显示本地时钟时间**（`HH:MM:SS`），不能只写相对秒
  （"6 s / 8 s"）——对齐 C 端 `vfdTrendXLabelText` / `fmTrendXLabelText`
  的回调口径。
  ② **Y 轴必须有数值刻度**：`TrendChart` 原来左边距只有 14px、压根不画
  Y 标签。现按**首序列**视口量程画 5 档（变频器页=输出频率 Hz、水质页=
  瞬时流量 m³/h）；多序列仍各自归一化（量纲不同不做双轴），各线真实
  量程看图例（`_trend_num` 按量级选小数位）。
  ③ **刷新率可调**：趋势卡右上「刷新」下拉（200ms/500ms/1s/2s，
  `TrendChart.SAMPLE_CHOICES`），**纯客户端节流**——只改本图记录密度，
  服务端采集/推送不变；缺省 200ms = 原行为。节流闸在**页面层**
  （`accept_sample(t_ms, stream)`），因为一拍要往多条序列各追一点；
  变频器页一条流、水质页流量/浊度两条**独立时钟**（共用一个上次时刻会
  互相压掉）。
  **窗宽不随刷新率变**（第一版按 60×间隔放大，切档瞬间老点全挤左边、
  像图坏了）——与 C 固定窗宽同口径，点变稀就是"采集慢了"的直观表现。
  ④ 回归锁：`test_hsf_regression.py` 的 `m_trendch_part_h_pages` 段
  I1~I8（下拉档位、X 时钟、Y 刻度、节流比例、窗宽不变、双流独立）。
- **本地客户端设置必须持久化（2026-11-10 用户定案）**：统一走
  `client_py/hsfsrv_client/settings.py`，**存程序目录的 INI、不用注册表**
  —— `<程序目录>/cfg/hsf_client_py.ini`（板端 `/root/Desktop/XinYueC/
  hsf_client_py/cfg/`；客户端与服务端同目录部署时它就在 `hsf_data.db`
  旁边）。**不要在各页里各写一份 QSettings/ini**；登录记忆
  （host/port/user/remember）也在这份文件里（旧版注册表值由
  `settings.migrate_from_legacy()` 首启搬迁并清除）。`cfg/` 已进 .gitignore。
  已持久化的项：
  界面缩放（`uiScalePct`，**重启生效**——QSS 与页面字号在建窗时按比例算好）、
  强制紧凑（`forceCompact`，`MainWindow.set_compact_override`）、启动全屏
  （`fullscreen`）、主界面刷新周期（`refreshMs`，主窗 refresh 定时器）、
  趋势刷新间隔（`trendSampleMs`）、窗口尺寸（`winSize`，应用/退出时都记）、
  悬浮窗显隐（`showTitleBar/showStatusBar/showNav`，`pages_other.apply_overlay`
  启动还原与设置页共用同一判定）、历史/日志每页条数
  （`histPageSize/logPageSize`）。
  **这些旋钮此前多数是死 UI**（改了既不生效也不落盘），改它们时务必同时：
  ① 接上真实效应（定时器/字体/可见性/页大小）；② 写 settings；③ 启动路径
  `__main__` 还原（命令行 `--size/--fullscreen` 优先于存档）。
  回归锁：`test_hsf_regression.py` §19 A5a~A5j（隔离临时 INI、断言各页读到、
  断言默认后端是程序目录 INI 而非注册表）。
- **会话压测 / 泄漏回归（改会话、订阅、登录或服务端 per-session 状态必跑）**：
  `tests/test_stress_reconnect.py --port 6100 --clients 8 --cycles 1250`
  （8 进程 × 1250 轮 = 1 万次连接→登录→订阅 9 主题→快照→断开）。
  **2026-11-09 起加码：改 XLinkSession/XLinkServer 传输层必须连跑两轮**，
  判据=0 失败 + 第二轮堆计数零增长（首轮 arena 平台期不作数）。
  环境要求：脚本会**自动**经远端配置（`CMD_CONFIG_GET/SET` 的 `login` 节）
  打开同账并发，收尾按原值恢复（`--no-policy` 可跳过）；手工压测时也可在
  「远端设置 → 登录策略（账号）」里改（管理员），或写
  `bin/cfg/hsf_login_policy.json`（服务端落盘的策略文件）。
  跑完须回缺省，否则 `test_client.py` 的权限/策略用例口径不对。
  泄漏判读用堆计数（`HSF_HEAP_TRACE=1` 服务端 / `XHSF_HEAP_TRACE=1`
  客户端，5s 一条 `[heap]`）；2026-11-08 实测 2 万次会话 0 失败、
  服务端堆回到 ~702KB 基线、句柄第二轮 0 增长。台账：
  `docs/HSF会话压测与泄漏回归_实施记录_20261108.md`。
- **推送失步两级自愈（2026-11-09 根修，详见 `Src/XCore/XProtocol/XLink/XLink.md` §15）**：
  Publish 帧队满被拒不再是无账目的有损丢弃——会话层记
  `m_txOverflowFrames` + 失步戳，队列排空发 `txResyncNeeded` 信号（服务端
  `hsfAppTxResync` 按订阅表补发全量）；失步超
  `XLINK_TX_DESYNC_KICK_MS`（15s）不排空踢线 reset。排查「界面数据冻结、
  连接显示正常」先看服务端日志三行：`推送失步补发` / `推送失步踢线` /
  `发方向判线`，都没有才是新问题。信号函数**必须容忍 self=NULL**
  （`XObject_signalIdOf` 以 NULL 调用采集信号 id）；`XVar` 宏对表达式
  取址，右值先落局部。新增信号函数照 `XLinkSession_txResyncNeeded_signal`
  的写法。
- **运行日志分页（0x0119，2026-11-09）**：C 服务端 logs 表
  （ts/category/text，category=conn/ctrl/alarm/auto/auth）+
  `HSF_CMD_LOG_PAGE` 分页查询（键与 0x0118 同字面）；C 客户端日志页
  （HsfDataLog）与 Python 客户端日志页都消费它；server_py 不实现该命令
  （proto.py 仅登记常量，注释点名 C 端）。改日志事件点/类别枚举必须同步
  `XhsfLinkProto.h`、server_py/client_py 两个 proto.py 三处注释。
- **Py 客户端退出拆卸三闸（2026-11-09 根修，证据链与三机制详见
  `docs/HSF双端对齐竖屏与推送失步根修_实施记录_20261109.md` §六）**：
  ① `sip.setdestroyonexit(False)` 已落在 `hsfsrv_client/__init__.py`
  （对齐 C 端 XGui.md §8.2 同款裁定；退出持有交 OS 回收）——**不要再往
  回加「退出时全量销毁」逻辑**；② bridge 的 Qt 信号发射一律走
  `_emit_net`（`_shutting_down` 闸），新增网络线程 emit 禁止裸 `.emit`；
  ③ `XLinkClient.close(join_timeout=3)` 同步停线程，`_set_state` 首行
  `_closed` 静默闸——自动重连的最后一拍不得在终结期触碰 Qt 层。
  新增信号/清理代码先看台账 §6 的三机制取证（core vtable 指向匿名堆=
  已复用，追类名无意义）；测试/工具**禁止一进程创建多个 QApplication**
  （Qt5 未定义行为，复现器工件会污染归因）。

## XGui 框架层：改尺寸/省略号必须对齐 Qt 6.8 原文

**框架层改动不许"看着差不多就改"——先取 Qt 6.8 对应函数的原文当依据**
（`cdn.jsdelivr.net/gh/qt/qtbase@v6.8.0/...` 可直取；`raw.githubusercontent`
与 `code.qt.io` 在本机常超时/404）。本轮已取原文的两处：

### 1. 表头放不下 ⇒ 省略号，**不是留空**（2026-11-10 实锅）

- 现象：历史页 16 列里 4 个中文表头**整格空白**（输出频率(Hz)/
  搅拌转速(rpm)/浊度1(NTU)/浊度2(NTU)）。
- 根因两层：**应用层**列宽按经验值给，比 QSS 下表头实宽少 1~2px
  （`padding: 6px 8px` + `border-right: 1px` + `font-weight:bold`）；
  **框架层**QSS 表头绘制路径里有「文字放不下就**不画**」的分支
  ⇒ 不是省略号而是空白。
- 修法：应用层按**当前字体实测表头文字**给列宽（与 Python `auto_min_texts`
  同口径）；框架层改成**先省略再画**，复用既有原语
  `XCommonStyle_elideText`（Qt 6.8 语义：文字永远有输出）。
- 顺带补齐 `QStyleOptionHeader::m_textElideMode`（枚举
  `XAbstractItemViewTextElideMode` 迁到 `XStyleOption.h` 全树单一编号），
  `XTableWidget` 注入与单元格同款。
- **通用教训**：样式表有 padding/border 时，**列宽不能靠经验值**，
  必须实测；框架层"放不下就不画"的分支一律违反 Qt。

### 2. 微调框 sizeHint 必须预留**内嵌编辑框自身的边框内缩**（2026-11-10 实锅）

- 现象：远端设置页「温湿度」周期框显示 `0000 ms`（应为 `10000 ms`），
  **首位数字被裁**。
- 根因（Qt 6.8 `qabstractspinbox.cpp:866-898` 原文比对）：Qt 的
  `QAbstractSpinBox::sizeHint()` = 文本宽 + `style()->sizeFromContents(CT_SpinBox)`，
  且高度取 `d->edit->sizeHint().height()`；我们的
  `spinbox_computeSizeHint()` ① 只量**当前文本宽**（值变宽不跟）
  ② **漏了内嵌编辑框自身的边框内缩**（`xlineedit_textStartX`/`textEnd`
  各让 `frame?4:2` ⇒ 左右共 8px）⇒ 文本比可用区宽 ⇒ 行编辑按光标位置
  **横向滚动**（保后缀、裁前导数字）。
- 修法：取**最宽候选值**（`textFromValue(min/max)` + 当前值），
  并用**内嵌编辑框自己的 sizeHint 减其当前文本度量**求差值
  （`chromeW/chromeH`，**与绘制路径同源**、不写死像素、度量用编辑框
  自己的字体），最终宽 = `sizeFromContents(CT_SpinBox)` + `chromeW`。
- **通用教训**：复合控件（SpinBox/ComboBox 等）算首选宽时，
  **子编辑框的边框/内缩必须计入**，否则"文本放不下"会退化成
  "滚动显示"（表现为数字被裁、看不出是尺寸问题）。

## 界面铁律：不许留"选了不兑现"的选项，也不许留占位控件

**用户口径（2026-11-10）**：「检查下界面功能是否都是实现的，别是占位」
「没有 XLSX writer 就只留 CSV 并删下拉（别留一个选了不兑现的选项）」。

- **下拉/选项必须真兑现**：要么接真实现，要么**把选项删掉**。
  半成品选项比没有选项更糟——用户会以为功能坏了。
  本轮「导出格式」下拉据此接了**真 XLSX**：C 端直接用框架自带
  **`XExcel`**（`Src/XCore/XData/XExcel`）写 xlsx，**不需要** Python 的
  ctypes 桥（`xlsx_bridge.c`）；CSV 侧要求**字节级兼容旧实现**
  （UTF-8 BOM + CRLF + 最小引号转义）。
  共享模块落在 `HighSpeedFilter/client/ui/HsfExport.{c,h}`，
  历史/日志/报警三页共用；导出格式在**导出开始时**快照取值，
  避免导出中途改下拉产生半份 CSV 半份 XLSX。
- **占位控件按页裁剪，不留着骗人**：`HsfDataBar` 新增按页裁剪 API
  （`HsfDataBarFeature_Chart/ColFilter/Decimals/MaxPoints/ExportFmt` +
  `HsfDataBar_setFeatures`）——日志页隐藏「生成折线图」+ 3 个无效控件、
  报警页隐藏 2 个无效微调框。
- **假数据 = 误控风险**：数字量页原**无条件**播种 16 个演示点，
  这些点带**真实通道号** ⇒ 一旦有人点它就可能对真机下发控制。
  现在只在离线演示模式（`--no-login` / `XHSF_DIGITAL_AUTOTEST`）下播种。
- **头注释里的"占位"字样要同步改**：实现补齐后，头注释还写着
  「（占位）…真实内容后续替换」= 文档骗人，按实际改写。

## 文档管理：台账写完即收，别让 docs/ 变垃圾场

**判据（三条全中才可删）**：① 内容已完成（自述"全部完成/根修完成/收口完成"）；
② 结论已被 **AGENTS.md 或记忆库**取代；③ 无人接手且无按路径引用。

- **活文档永远不删**：设备协议/寄存器映射、实测总表与使用指南、
  交叉编译与移植指南、架构设计、以及 **AGENTS.md 按路径硬引用的台账**
  （删它们要连带改写 AGENTS.md 的取证指引＝信息搬移，不是整理）。
- **删文档必须连带清死链**（2026-11-10 实锅）：本轮删 41 份时发现
  `Tools/analysis/xgui_api_scan.py` **会真的往已删目录写文件**（工具直接
  跑不起来）＋ 4 处代码注释按路径引用 ⇒ 一律改指留存载体
  （机制级复现器 / 配置头注释 / 记忆库），删完跑一遍全仓库引用扫描。
- **归档史料（`docs/xgui/history/`）逐字保留**，不改历史记述；里面按裸文件名
  提到的清单（已被删）加一句说明即可。
- **删除用 `git rm`**（可回滚），**不自行提交** ⇒ 随时
  `git checkout -- docs/` 整体撤销。
- 台账索引：`docs/HSF现场口径与客户端完善_实施记录_20261110.md`（§续篇指向）、
  `docs/HSF客户端崩溃与板端采集根修_实施记录_20261110.md`、
  `docs/文档整理_实施记录_20261110.md`（本节判据的来源）。

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

## 结构体初值铁律：位置初始化器 = 与字段顺序硬绑定

**结构体用位置初始化器（`= { a, b, c }`）时，初值与字段顺序强耦合：新增
字段、或把字段插到中间 ⇒ 后面所有初值整体挪位 ⇒ 静默串位（不报错、不崩，
只是值落到别的字段上）。**

- **2026-11-11 实锅（两例同族）**：
  ① 安卓平台 `Drive/Android/Graphics/XPlatformWindowAndroid.c` 的
  `static XPadState g_xpad` 位置初值块按旧字段顺序排，本批往结构体**中段**
  插了 13 个「四点触摸校准」字段 ⇒ 初值错位 13 槽 ⇒ `m_runtimeTrimX/Y` 拿到
  **160**（正是初值块里的 DPI 兜底值）⇒ **安卓每个触摸被平移 −160 逻辑 px**
  （现场「点击整体偏左上 320 物理 px」）；`m_calibActive` 也拿到 160 ⇒ 校准
  模式开机自启、吞掉前 4 个触摸。
  ② `Src/XGui/Graphics/XPainter.h` 的 `XPainterState` 中段插
  `m_devicePixelRatio` ⇒ `m_font` 偏移平移 8 字节 ⇒ XPicture SetFont 回放
  `VXFont_move` 把旧槽位 `pointSizeF`（double 12.0 位模式）当 `XString*`
  删 ⇒ 段错误。已立规：**该结构体新增字段一律尾部追加**。
  取证与修复见 `docs/DPR渲染层_Qt对齐_实施记录_20261109.md` §八/§九。
- **规矩**：结构体初值一律用**指定初始化器**（C99：`.m_dpr = 1.0f` 逐字段
  点名），字段增删/插位不再影响其余初值；确要用位置初始化器时，新增字段
  **追加在结构体末尾**并同步初值块。
- **此类 bug 的特征签名**：**与输入无关的等量常数偏移**（X/Y 同量、恒定、
  与点在哪无关）。排查顺序：先假设「有个常数被加在映射结果上」⇒ 把映射链上
  每个可加常数的量逐条列出（origin / trim / scale / offset / 初值）⇒ 查
  **它们的初值从哪来**（结构体初值最易被字段插位打乱），**而不是**先怀疑
  外部环境（模拟器宿主缩放、状态栏内缩、窗口换算）。本例前几轮先后误判为
  「宿主窗口缩放残数」「输入原点算错」，绕了三轮弯路。
- **取证纪律**：先**逐条排除补偿通道**（marker 文件 / env / runtime trim /
  origin 覆盖 / 一次性校准），全部排除后仍存在的常数，才是结构体初值。
  巧合会骗人：DPI 兜底 160 与「手感丢失」的 160 是同一个数。
- **配套铁律（救旧锁不得反向改框架）**：行为语义变更后若旧契约测试崩，
  先查新口径**有没有更权威的锁**（qt-align 套件）——有则改旧测试对齐新锁，
  严禁为了让旧测试不崩而把框架改回非 Qt 行为（2026-11-11 XWizard_setField
  二修实锅：曾恢复「自动建项」救旧锁，反把 qt-align 权威锁打红 3 断言）。
  顺带：`XStrcmp` 无 NULL 守卫，语义改动的调用方断言要防 NULL 直喂。

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
