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
  按窗宽 <860 判、13/16 页有 `_adapt` 按页根宽细分档、菜单页移动态树面板、
  每页字号 50~200% 实时重放——这些 2026-11-09 三档化已具备，本次只补
  「安卓窗口手机形 / 字体走家族名 / 设置落私有目录 / 登录窗滚动壳」。
- **MSVC 编译注意**：源文件 UTF-8（CMake 已开 `/utf-8`，见 `CMake_Config.cmake`）；
  向 C 字符串写 `\n` 时严禁在 bash heredoc 里转义（会被吃掉变成真实换行
  导致 C2001）——用脚本文件或 chr(92) 拼接。
  **块注释里禁写 `*/` 序列**：如 `**/system/bin**`（粗体包路径）里的
  `*/` 会提前闭合注释 → 后续中文变代码 → C2143「缺少'{'」+ 一串
  C3872/C3873「字符不允许在标识符中」。这类报错看着像编码问题，实际是
  注释被截断（2026-11-09 实锅）。
  **C 里指针类型不匹配只是「警告」不是「错误」（2026-11-14 实锅）**：
  `XBsonDocument_insert_keyUtf8_string(doc, key, const XString*)` 收的是
  `XString*`，而 `XBsonDocument_insert_keyUtf8_utf8(doc, key, const char*)`
  收 UTF-8 串 ⇒ 传错**编译照样过**（只报 C4133）⇒ 运行时把 `char*` 当
  `XString*` 解引用 ⇒ **段错误**。⇒ **改完 C 代码必须 grep 编译日志里的
  C4133/C4028**（本项目的 BSON 调用里 `XBsonArray*`→`XVector*` 那类是
  既有噪音，但**自己新写的行**出现就得改）。
  **改公共头（尤其结构体）必须全量重编（2026-11-14 实锅）**：本仓 ninja
  **没有头文件依赖**（`AGENTS` 已记「改公共头不触发重编」）⇒ 结构体加字段
  后只有部分 TU 被重编 ⇒ **旧 obj 与新 obj 的结构体布局不一致** ⇒ 跨模块
  传结构体时读写错位 ⇒ 段错误（现象：改了代码反而崩、崩点与改动无关）。
  ⇒ **改了 `common/*.h` 或任何被多处 include 的头（含结构体）⇒ 先
  `find out/build -name '*.obj' -delete` 再全量重编**（本轮 C 回归的
  段错误就是这么来的：全量重编后 1035/0 全绿）。
  **'⇒'(U+21D2) 在 GBK 里编不了（2026-11-14 实锅）**：中文 Windows 默认
  代码页 cp936 ⇒ 输出被重定向到文件时 stdout 用 GBK ⇒ 测试里打印「⇒」
  直接 `UnicodeEncodeError` ⇒ **整套回归在打印断言时被打断，而退出码仍是
  0**（看着像"跑完了"）。⇒ Python 测试文件顶部统一加
  `sys.stdout.reconfigure(encoding="utf-8", errors="replace")`；C 端日志
  里的 '⇒' 改成 GBK 也有的 '→'。
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
- **落盘缺省：入库周期 1 分钟 / 容量上限 50MB（2026-11-11 用户定案）**：
  ① **入库周期** 缺省由 5s 改 **60000ms**（历史是慢变量，1 分钟一档足够
  看趋势，库体积压到 1/12 —— 现场机器磁盘小）；② **容量上限** 缺省由
  「0=不限」改 **50MB**（不封顶会一路写到把盘撑满；0 仍可显式选「不限」，
  满额后按 `fullPolicy`：ring 循环覆盖 / stop 停写 / rotate 分段滚动）。
  **三处同源常量，改一处要同步另两处**：
  C 端 `common/XhsfLinkProto.h` 的 `HSF_STORAGE_SAMPLE_MS_DEF` /
  `HSF_STORAGE_MAX_MB_DEF`（服务端 `HsfServerStore.h` 的
  `HSF_STORE_SAMPLE_MS`、服务端 `HsfServerApp.c` 读 `storage/max_mb` 的
  缺省、客户端 `HsfLinkConfig.c` 与 `HsfSettingsRemote.c` 的控件初值都
  引用它）；Python 端 `server_py/hsfsrv/proto.py` 与
  `client_py/hsfsrv_client/proto.py` 的 `STORAGE_SAMPLE_MS_DEF` /
  `STORAGE_MAX_MB_DEF`（服务端 `config.StorageConfig` 与客户端远端设置
  页控件初值引用它）。
  **口径细节**：配置文件里**键不存在** = 用缺省；**显式写 0** = 不限
  （`StorageConfig` 用 `kw.get("max_kb", None)` 区分，别写成 `or`）。
  **回归不依赖产品缺省**：`tests/test_hsf_regression.py` 的 `PrivateServer`
  与 `IsolatedCServer` 必须**自带快节拍**（storage/sample_ms=1000）——
  否则历史类断言（"等一拍就有一行"）在 1 分钟缺省下恒 0 行（实测 §1 与
  同形判据共 6 条红）；产品缺省改了不该让回归红。
- 构建入口：`build_hsf.bat`（HsfServer/HsfClient，build-hsf/）；
  测试宿主 `XinYueC_Static`（out/build/x64-Debug），跑法
  `bin\XinYueC_Static.exe --test <命令>`。
  **所有 MSVC 构建必须经 `vcvars64.bat`**（`/tmp/build_reg.bat`、
  `build-hsf/build_static.bat` 都是这个套路）；直接在 Git Bash 裸调
  `cmake --build` 会因缺 `INCLUDE` 报 `stdbool.h`/`stdint.h` C1083。
  `XGuiRegression_Test` 与 `XinYueC_Static` 在 `out/build/x64-Debug`，
  `HsfServer`/`HsfClient` 在 `build-hsf/`，两套互不覆盖，可分别重编。
- **板端换包必须同时保住 `hsf_data.db`（2026-11-24 实锅）**：包根
  `hsf_server_py/hsf_data.db` 是**历史/日志/报警库**，换包时只保留 `cfg/` 会把库
  一起删掉（delete-then-extract 模式）⇒ 历史页立刻「空」。本轮已从备份恢复（998 行、
  22 列迁移完成）。⇒ 换包前 `cp hsf_data.db*` 到 /tmp 或 stop 后先备份；解包脚本的
  `find ... ! -name cfg -exec rm -rf` 必须再加 `! -name 'hsf_data.db*'`。回滚包
  `backup-py-20261124-081600.tgz` 仍在板端。
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
  ⑦ **板端 6100 是谁在听，先查再连（2026-11-11 实锅）**：`hsfserverpy`
  （systemd 正式单元，enabled）早被人为 stop（status=0/SUCCESS 不是崩
  溃）；白天给 6100 服务的是 **nohup 手工进程、零 journal 痕迹** ⇒ 它
  一死无人拉起，板端自己的全屏客户端（hsfclientpy，Restart=always）只
  会无限重连。诊断口诀：`ss -tln | grep 6100` 无监听 +
  `systemctl status hsfserverpy` 是 dead ⇒ **`systemctl start
  hsfserverpy` 拉回**（WorkingDirectory/cfg 都在位）；别按「C 栈没有
  systemd 单元」的旧口径去 nohup 手搓——Python 服务端有正式单元。
  判活别只看 TCP ESTABLISHED（可能是断链后的 TCP 壳），**以
  `journalctl -u hsfserverpy` 里该源地址的订阅记录为准**。
  ⑧ **同账单会话 ⇒ 禁止用第二个会话碰板端**：同一账号再起 `--shot`
  截图进程/第二个客户端，会顶掉常驻窗口的会话或被 DenyNew 拒
  （`--shot` 能短暂拿到真数据随后「连接已断开」）；板端验证改走
  `journalctl -u hsfserverpy`（订阅/登录记录）或本地 sim，别抢板端
  会话。
  ⑨ **板端「历史数据看不到」= 三个叠因叠加（2026-11-11 实锅，60 分钟
  定位）**：① **落盘 SQL 全失败**（日志 52 条「SQL 失败」：建表/索引/INSERT
  全挂 ⇒ 库里连表都没有 ⇒ 历史页 0 条）；② **485 两路全离线**（日志
  `[485-A] 串口 COM17 打开失败` ⇒ 二进制带的是 **Windows 缺省串口名**，
  不是 /dev/ttyS3+ttyS4 ⇒ 浊度/流量/压差/变频器都没有效值）；③ **历史
  采样的「热身门」**（`hsfAppTickHistory`：所有字段组 VFD/温湿度/浊度×2/
  流量/压差/数字量 都至少见过一次有效值才开始入库，缺一个就**一行不写**）
  ⇒ 前两因导致「设备在但不在线」，第三因把结果放大成「永远 0 行」。
  **判据链**（按这个顺序查，别跳步）：① `ss -tln | grep 6100` 有没有监听
  ⇒ ② `journalctl -u hsfserverpy` 看「SQL 失败」计数 ⇒ ③ 库文件
  `SELECT count(*) FROM history` 与 `sqlite_master` 里有没有表 ⇒ ④
  日志里 485-A/B 打开的是 **`/dev/ttyS3` 还是 `COM17`** ⇒ ⑤ 设备台数
  （**5 台 = 485 没通；10 台 = 通了**）。
  **三个修法**：① 今天的 `XSqlQuery.c`「泄漏根修」顺带修好了它——
  `XSqlQuery_init_database` 二次 init 会把对象的堆标志与分配器复位
  （`XClass_init` → `Set_Class_IsHeap(false)`），查询对象释放错乱 ⇒ **堆被
  破坏** ⇒ SQLite 执行全失败（本地 Windows 内存布局不同没暴露，ARM 上
  必现）；**新编的 ARM 版 SQL 失败 0 条**。② 板端 C 端必须用
  `BOARD_BUS_A_PORT=/dev/ttyS3 BOARD_BUS_B_PORT=/dev/ttyS4 sh
  verify/board/build_board.sh` 重编（脚本第 72-73 行**缺省就是 COM17/COM18**，
  不传环境变量就编出全离线的二进制）；端口宏走命令行 ⇒ 脚本会自动清缓存
  全量重编（`BOARD_FORCE_RECONFIG`）。③ 热身门本身是对的（不写启动瞬态的
  空洞行），**不要靠放宽它来"修"历史为空**。
  **配套诊断增强**：`HsfServerStore.c` 的 `storeExec` 原来失败只打
  `XSqlQuery_lastQuery`（那是 **SQL 文本**不是错误原因）⇒ 板端日志里只有
  一串 CREATE TABLE，看不出所以然。现改打 `XSqlQuery_lastError` 的**错误
  类型 + 驱动文本 + 原生错误码**（`XSqlError_type/driverText/
  nativeErrorCode`）⇒ 下次同类问题一行看清。
  **迁移历史库的坑**：`INSERT INTO history ... SELECT ... FROM history`
  跨库时 `FROM history` 会解析到**目标库自己的表**（自我拷贝，一行不增）⇒
  必须 `ATTACH ... AS src` 后写 `FROM src.history`；且用户口径「Python 栈
  那批历史是无效数据」⇒ 不要把 Python 栈的库搬进 C 端库。
- **账户三档齐（2026-11-11 用户「设置了三种权限，是不是还缺一个」）**：
  权限角色三档实名（`permissions.py` 两端逐位一致）：普通用户=查看+下载、
  **操作者**=+设备控制（能开阀控泵，不能切模式/管账户）、管理员=全部。
  此前账户库只有 admin(普通)/root(管理员) 两户 ⇒ **缺操作者**。已补
  **oper/oper**（名字按用户口径取短，与 admin/root 齐平；角色中文
  「操作者」不变；登录名与界面显示名是两回事），**本地
  `server_py/cfg/hsf_users.json` 与板端 cfg 同步补齐**（板上原库备份
  `hsf_users.json.bak-20261111-add-oper`）。账户摘要必须用
  `server_py/hsfsrv/auth.py` 的 `derive_hash` 生成（加盐迭代 HMAC，
  不能手写 json）。**账户库是启动时加载一次、无运行期管理命令** ⇒
  改完必须重启服务端（板端 `systemctl restart hsfserverpy`，会断当前
  会话，各客户端自动重连）。
  **缺省建户已三档化（2026-11-11 用户拍板「缺省建户也改成三档」）**：
  Python `auth.create_defaults` 与 C 端
  `XAccountManager_ensureDefaults`（Src/Industry/XAccessControl/XAccount.c）
  空库时各建 **admin(只读)/oper(操作者)/root(管理员) 三户**（两端名单
  逐字一致；`DEFAULT_OPER_NAME/PASS` 进 server_py proto.py）。
  `HsfServerApp.c` 与 Python `__main__.py` 的「缺省账户已创建」打印
  文案同步三档口径。回归锁：C `--test xindustry-access`
  （XAccessControlTest，「缺省 3 账户（三档）」等断言；
  **`--test` 走的是根 main.c 的分派链**，不是 XTestCommand.c 的
  ConsoleShell 链——两条链别混）+ Python `test_units.py`
  `test_userstore_defaults_are_readonly_and_admin`（三档 + len==3）。
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
- **Python 客户端主界面加「登录 / 退出」入口（2026-11-11 用户「现在就是
  主界面登录与退出了，看看加在哪里合适」）**：入口放**状态栏右下角**
  （`widgets.py` 的 `StatusBar`），可点「账号·角色 ▾」按钮弹菜单 =
  **切换账户 / 退出登录**；位置对齐 C 端 2026-11-11 把账户从标题栏搬到
  状态栏右下角的口径（`HsfStatusBar` 的 `hsfStatusUserText()`：
  `root·管理员` / 未登录 `未登录`）。**不往标题栏加**：右侧已 7 个元素
  （时钟/温湿度/健康/模式钮/自动状态），源码记着两次「抢地方被裁」的
  实锅（`widgets.py:159-161`）；`StatusBar.message` 有 stretch=1，左侧
  文本天然让位（等价 C 端保留宽 + ElideRight）。
  **C 端已同批补齐（2026-11-11 收口，两端一致）**：
  `HsfStatusBar` 账户区**可点**（命中区与绘制公式同源同参
  `HsfStatusBar_accountHitTest`、手型光标，点弹「切换账户 / 退出登录」
  菜单；`HsfStatusBar_setAccountAction` 只发动作不碰业务，对齐 Python
  switchUserRequested/logoutRequested 两信号）；`HsfUi.c` 的
  `hsfUiAccountAction` 落地两动作——退出=断链 + 刷「未登录」+ 状态行
  「已退出登录」（不退进程），切换=断旧链 → 可重入登录窗
  （`hsfUiReloginDone` 失败留主窗、登录窗 × 只收窗，Python 坑④同款
  口径）；回归锁 `Test/XGuiTest/hsf_vfd_probe.c` [A0]（文案含
  「未登录」/▾、命中区三测）+ [A1]（退出登录动作：连接关闭、主窗仍在）。
  **四个必踩的坑（都已修）**：
  ① `bridge.disconnect_from()` 是「真退出/拆卸」语义：置
  `_shutting_down=True`（网络线程信号被 `_emit_net` 静默）且**不发
  `disconnected`** ⇒ 直接拿它做「退出登录」会状态栏永远停在「已连接 ·
  root · 管理员」。新增 `bridge.logout()`：断链 + 清 `user/role` + **主动
  emit disconnected**，且**保持 `_shutting_down=False`**（进程不退出，
  后续 `connect_to()` 的 connected 信号要能到）。
  ② **conn 双写收拢**：旧代码 `_on_connected` 把「user·role」拼进
  `set_connection`，`__main__.py` 的 `--auto`/探测路径又各写一次
  「已连接 · root」（不带角色）⇒ 文案与时序都不定。现在 **conn 只说
  连接态**，账号·角色一律走状态栏账户钮（`MainWindow._sync_account()`
  是唯一写入点，1Hz 兜底 + 同值早退零脏区）。
  ③ **登录路径要可重入**：`__main__.py` 原先把四条登录路径写成 `main()`
  顶层一次性分支，无法"退出后再登"。已抽成 `_run_initial_login()` /
  `_prompt_login()` / `_relogin_after_logout()` 三个可重入入口；
  菜单动作走 `QTimer.singleShot(0, ...)` 投递（**不能**在 `QMenu.exec_`
  的嵌套事件循环里同步再 `exec_` 模态登录窗）。
  ④ **退出登录 ≠ 退出进程**：绝不能复用 `_teardown()`（那是断链→关窗→
  退进程）；取消登录窗只留一个「未登录」主窗，点账户钮可重试。
  ⑤ **登录窗空口令必须补缺省 root（2026-11-11 连板端实锅）**：占位符写
  「口令（缺省 root）」，但 `_read_inputs()` 直接发 `.text()`，留空就是
  **空串**发给服务端 ⇒ HMAC 比对失败报「口令错误」——板端 6100 账户
  明明就是 root/root（把板端 `cfg/hsf_users.json` 拉下来用
  `auth.derive_hash` **离线比对**候选口令确认的，不碰板上服务、不触发
  锁定）。占位提示必须兑现，现在留空补 root、填了原样用。
  ⑥ **认证失败会蹦 `AttributeError: 'NoneType' object has no attribute
  'readexactly'`（同轮实锅，xlink.py `_pump`）**：认证失败/会话 Bye 是在
  `_on_frame` **里面**调 `_teardown()`（置 `_reader=None`）再返回，
  `_pump` 的 while 循环不收口 ⇒ 下一拍 `readexactly` 打在 None 上 ⇒
  AttributeError 被 `_run` 的 `except Exception` 当「连接失败」打上
  状态栏——登录窗明明报「口令错误」，状态栏却蹦一条吓人的
  AttributeError。修法：`_on_frame` 返回后判 `if self._reader is None:
  break`（帧内拆链 ⇒ 收包循环立即结束；close 场景由 `_closed` 兜住）。
  **排错口径：登录窗报业务原因（口令错误/账户已在线），状态栏同时蹦
  AttributeError ⇒ 先查 `_pump` 帧内拆链收口，不要被那条日志带去查网络。**
  ⑦ **`LoginDialog.error` 残留 ⇒ 「密码填对了程序整个退了」（2026-11-11
  连板端第二发实锅，用户「我试了登不上」）**：`self.error` 只在失败时
  写、从不清 ⇒ 第一次失败后第二次登录**成功** `accept()` 返回，`main()`
  里 `if dlg.error:` 读到**残留** ⇒ 进程按「登录失败」`_teardown` 退出
  （exit 2）——网络层明明登上了，用户看到的是程序整个关掉。修法：
  `_on_login` 每次尝试前清 `self.error`；`_on_offline`（离线浏览是主动
  选择）同样清。排错口径：**登录窗进程 exit 2 且当时口令是对的 ⇒ 先查
  `dlg.error` 残留，再查网络。**复测配方：错口令 → 1.2s 后留空重登 →
  成功 role=2；脚本必须走 `LoginDialog._read_inputs`（补缺省在那层），
  直接给 `bridge.connect_to` 传空串就是空口令，会误报「口令错误」。
  ⑧ **死 loop 上投递协程 ⇒ `RuntimeWarning: coroutine ... was never
  awaited`（同轮实锅，xlink.py）**：`call_soon(_do())` 的实参求值先
  **创建协程对象**，loop 已关时 call_soon 把它原样丢弃 ⇒ 拆卸竞态里
  每条在途请求蹦一条警告。修法：`XLinkClient._alive()` 守卫放在
  `request_async` / `subscribe_async` / `unsubscribe_async` 创建协程
  **之前**（死 loop 上 `request_async` 走 on_error("连接已关闭")）。
  回归锁：`test_hsf_regression.py` §21d「状态栏账户菜单」A1~A13（三态
  文案/两项转发/logout 回落/不置 _shutting_down/conn 不含账号/同值早退/
  空口令补缺省 root/**A11 error 残留清空/A12 离线清 error/A13 死 loop
  无协程泄漏警告**）+ §30 现场段的账户钮与 conn 双写断言。
- **自动模式配置界面（Python 客户端先行，2026-11-11 用户定案
  「先实现界面」）**：远端设置页新增「自动模式配置」卡，**产水/反洗/顺洗**
  三模式各自一块（`proto.AUTO_MODES`，协议稳定 id
  `produce/backwash/rinse`），每块 = 「启动设备」多选下拉（复用数据三页
  MultiCheckCombo；8 项 = `valve1..6/stirPump/inletPump`，
  `proto.AUTO_DEVICES`）+ 搅拌电机转速（rpm 0~1440，缺省 1440＝铭牌
  额定点，框旁 rpm→Hz 折算提示 50Hz≈1440rpm，与变频器页「转速调速」
  缺省同口径）+ 方向（正转/反转，userData=reverse 与变频器页同口径）。
  「应用」走既有 config_set 通道发 `auto` 配置节（载荷形状
  `_auto_config_values`：`{"modes":{<mode>:{"devices":[id…],
  "stirRpm":int,"stirReverse":bool}}}`）——**服务端（Python+C）尚未实现
  auto 节**，现点应用收「未知配置节」失败包络（fail-closed 不假成功）；
  接入时按 `proto.CFG_SECTION_AUTO` 注释的三处同步点做，客户端零改动
  生效。`_populate` 只在应答带 auto 节时铺模式块（局部应答不冲，A4 同
  纪律）；无管理权限整卡置灰。MultiCheckCombo 新增 `empty_text`（空选
  独立文案「未选择」，与全选可辨；缺省不传＝旧口径逐字不变）与
  `set_checked_indexes`（整体回显，不叠加 min_checked）。本轮两个布局
  实锅（离屏截图实见）：① **块根挂裸 `background:transparent` 样式表会
  级联刷黑没 own-QSS 的按钮/下拉**（widgets.py 老坑新犯，块根什么都不
  用设）；② **有 maxWidth 上限的控件在 stretch=1 行里会被 QBoxLayout
  顶到行尾**（剩余空间摊成项间隙）⇒ 控件贴标签、行尾统一 addStretch。
  回归锁：远端设置 A 段 A6a~A6l（`--only=remote`，75/0）。
- **自动三模式服务端落地 + 主界面模式卡（2026-11-11 用户定案「先分别实现
  那三个模式…桌面模拟器测完整，无泄漏无崩溃」）**：
  ① **序列状态机**（`autocontrol.AutoController`，相位 valves→pumps→
  running）：启动=开选中阀/关未选阀 → **等全部到位**；超时/判故障 ⇒
  `AUTO_ALARM_VALVE_TIMEOUT(5)` 报警 + 停止自动模式 + **报警锁存**
  （`_fault_latched`，复位前拒绝启动）；到位后按配置启动搅拌泵
  （`apply_control(OP_START, rev, rpm)` 带配置转速/方向）与进水泵 →
  运行。`reset()` 一把钥匙解两把锁（急停锁存 + 报警锁存）；启动闸顺序
  手动模式→报警→急停锁存→急停回路（`start_preconditions` 与
  `mode_config_error` 远程/现场两入口同源）。阀命令失败按 2s 节流重发；
  模式/相位/锁存经 CMD_RUN_MODE 查询与 0x0115 应答的
  `autoMode/autoPhase/autoFault` 三键上报（客户端 1s 轮询回显）。
  ② **auto 配置节**：config.py 存三模式 `{devices,stir_rpm,stir_reverse}`
  （运行期生效+落盘，未知设备 id 丢弃）；`_h_config_get/set` 支持
  section=auto（客户端远端设置页零改动接通）。
  ③ **主界面「自动模式」卡**（HomePage 第四排）：下拉产水/反洗/顺洗/
  **自动（流量循环，服务端明确拒「暂未实现」——按流量循环调用三模式的
  逻辑后续实现）** + 启动/停止 + 相位回显行
  （`bridge.send_auto_control(…, mode=)`）。
  ④ **急停口径照旧**：任意模式急停 ⇒ 全停设备（阀也关）+ 锁存，复位后
  才能再启动（既有 e_stop/_check_estop 原样复用，未动）。
  ⑤ **C 端已同口径实现（2026-11-12 对齐收口）**：`server/HsfServerAutoMode.c/h`
  （三模式状态机 + 组合循环 + 阶段记忆 + runMs 累计**持久化**
  `run/auto_run_ms` + 逐模式阀复位开关 + auto 配置节进出 +
  historyTrigger 采样门），协议常量全套在 `common/XhsfLinkProto.h`；
  C 客户端首页自动模式卡 / 远端设置自动配置卡 / 入库触发下拉同批在位。
  回归锁：C `hsf_regression_test.c` §S（S1 产水全程/S2 阀超时/S3 闸门/
  S4 config_error/S5 闸序/S7 告警链/S8 包络消费/**S9 按钮组三盏灯跟随/
  S10 组合循环（假时钟）/S11 累计时长持久化**）+ §T 配置节存在性；
  Python `tests/test_auto_modes.py` + `test_auto_cycle.py` +
  `test_sim_e2e_auto.py`；协议面端到端 `verify/hsf_auto_mode_check.py`
  （对真实 `bin/HsfServer.exe --sim`，S3 断言已随循环落地更新为
  「循环配置校验」口径）。
  **按钮组三盏灯（DO7~9）跟随（2026-11-12 补齐，Python buttons.py 同款）**：
  灯只在**自动模式**由 `HsfServerAutoMode_tick` 写（手动模式灯归人工一个
  DO 都不写）；目标=启动灯 DO7 运行中 / 停止灯 DO8 非运行（急停也亮）/
  复位灯 DO9 点动镜像 DI15；**写判据是回读≠目标才补写**（外部清零自愈，
  板端 2026-11-06 实锅口径）；急停回路三态单源
  `HsfServerAutoMode_estopState()`（启动四闸与 VFD 控制急停前置闸共用——
  Python `_h_vfd_control` 1b 闸已同步补齐：急停无信号拒启动/点动/频率/
  转速四类 op，停机类照放行，未配操作柱不拦）。
  ⑥ **端到端抓出的真 bug**：`virtualdev.VirtualDigital` 阀到位位序**写反**
  （开=奇位；现场/配置 2026-11-10 已订正为开=偶 2n）⇒ sim 下开阀永远
  读成"关到位"——sim 的手动阀控同样受害。已修模型 + 同步
  `test_digital_feedback_follows_do`（它锁的就是旧位序，按其自述一并改）。
  **教训**：订正"现场位序"这类接线口径时，**消费方要全树扫**（虚拟设备
  模型/模拟器/测试夹具都是消费方）——只改 config 不改模拟器＝sim 与
  现场两副面孔，自动序列一跑就把阀全卡成"超时"。
  **⑥-2 本地 sim 配置文件同一坑（2026-11-12 用户实跑抓出）**：
  `server_py/cfg/hsf_server_py.json` 的 valves.items 也是**过期位序**
  （openDi=奇）——代码缺省订正了、落盘的旧配置文件没订正 ⇒ 桌面
  `--sim` 真实跑自动模式时 6 阀全部超时判故障。已订正文件为开在前
  （openDi=2i/closeDi=2i+1）。**落盘的 cfg 文件同样是消费方**——订正
  口径要连"用户机器上已存在的配置文件"一起想（旧文件 load 后按文件值
  生效，不会自动跟代码缺省走）。**重启服务端必须验证旧进程真死了**：
  Git Bash 把 `taskkill /F` 的 `/F` 啃成路径（MSYS 路径转换）⇒ 静默
  失败 ⇒ 新实例 bind 失败、验证打在旧进程上（本轮 08:43"重启"实锅，
  日志「端口已被占用」+ tasklist 验尸才看见）。用
  `MSYS_NO_PATHCONV=1 taskkill /F /PID …` 并 grep 确认进程消失。
  **现场接线范围（2026-11-25 用户订正：1~6 阀全部接线）**：此前
  「只有 1 号阀接线」是早期状态，已作废。自动模式仍先在**模拟器**按
  约定点位（DO=i、开到位 DI=2i、关到位 DI=2i+1）验证；现场 6 路都有
  到位反馈，阀状态/停止灯判据按全部 6 路处理。
  **⑦ 逐模式「阀复位」开关（2026-11-12 用户定案）**：远端设置每个模式
  多两个复选框——**停止时关阀复位**（缺省 True=保持既有"停止即全停含
  阀"）/ **急停时关阀复位**（缺省 False=**阀保持现状**，与用户口径
  「按下急停后要停止所有设备(电动球阀可以不动)」一致；急停停的是泵与
  变频器，阀可能正停在中间位，叠一条"关"是叠加未知动作）。协议键
  `resetValvesOnStop` / `resetValvesOnEstop`（两端 proto 同名）；
  执行层 `stop_all_devices(reason, reset_valves=)` + `stop(reset_valves=None
  ⇒读模式配置)` + `e_stop()`（读模式配置）。
  **切模式（切自动/切手动）的全停不受这两个开关影响**——「切换模式 ⇒
  关闭所有设备（含阀）」是既有安全契约，app 切模式路径显式传
  `reset_valves=True`。
  **两个测试坑（都踩过）**：① `stop_all_devices` 是**异步投递**
  （`get_event_loop()` ⇒ 必须在**服务端 loop 内**调用才投得出去）：从
  客户端线程直接调 `ctl.stop()` 会**投递失败、动作根本没发生**（假绿）
  ⇒ 测试必须走协议（0x0115 的 op=2/op=3、0x0105 切模式）。
  ② 假数字量模块的 **DI 是静态的**（写 DO 不改反馈）⇒ 阀永远到不了位、
  状态恒"动作中"；判"阀有没有被复位"要看 **DO 输出位**，不是阀状态。
  回归锁：服务端 `test_auto_modes.py` 新增 5 条（停止不复位/停止复位/
  急停不复位/急停复位/切模式不受影响）；客户端远端设置 A6m/A6n
  （复选框存在/缺省/进载荷/回显/缺键不误改）。
  **⑧ 主界面「复位」按钮（2026-11-12 用户定案「有故障报警的时候可以按
  复位」）**：自动模式卡加第三个按钮「复位」＝服务端 `op=4`
  （AUTO_OP_RESET，解除**两把锁**：急停锁存 + 模式序列报警锁存，回停止
  态**不启动**）。**客户端原先只能发 {"on":bool}**（表达不了复位/急停，
  复位只能靠现场按钮组的物理复位按钮 ⇒ 远程无解）⇒ 本次在客户端
  proto 补齐 `AUTO_OP_START/STOP/ESTOP/RESET`（与 server_py proto 逐值
  同步）+ `bridge.send_auto_reset()`。按钮**常态灰**，只在
  `autoFault=True`（报警锁存）或 `phase=="estop"`（急停态）时亮——
  复位是"解除锁存"不是日常操作，常亮会招误触（按了会清掉正在看的故障
  现场）；状态行文案同步改成「按「复位」清除后再启动」。服务端 op=4
  通路本就有（`_h_auto_control` 四板斧），本轮只是把它暴露给客户端。
  回归锁：客户端 home 段 **D11a~e**（按钮存在/无锁存灰/报警亮/急停亮/
  载荷={op:4}）。**测试坑**：测试用的裸 socket 客户端（test_vfd_ops.
  Client）**没有心跳** ⇒ 空闲 6s 被服务端判静默断链（真实客户端有
  1s 心跳，不受影响）——写"等一段时间看推送"的用例时必须在 6s 内有
  请求往来，否则连接先死。
  **⑨ 累计自动运行时长（2026-11-12 用户定案「增加自动运行时间，只有在
  自动模式启动的时候才计时累计，显示成天 时 分 秒」）**：计时放在
  **服务端**（`AutoController._run_accum_ms` + `_run_start_ms`，
  `run_ms()` 查询；只在 running 期间累加，停止/急停/模式中止时
  `_settle_run_time()` 结算冻结，**多次运行是累计**）——客户端计时会因
  断线/重启归零，两端数字对不上。协议键 `autoRunMs`（两端 proto 同名，
  经 CMD_RUN_MODE 查询 + 0x0115 应答的 `_auto_state_extra` 上报）。
  界面：自动模式卡**状态行右侧**加「自动运行 N天 HH时 MM分 SS秒」（与状态
  文字**同一行**，不加行高——首页余量只剩几 px）；标签**定宽**（按
  QFontMetrics 实测"999天 23时 59分 59秒"）防每秒跳动重排；缺键（老
  服务端）显示「自动运行 --」而不是"0天"（不把"没数据"说成"没跑过"）。
  回归锁：服务端 `test_auto_modes.py` 新增 4 条（未启动为 0/运行中递增/
  停止冻结/再次运行叠加）；客户端 home 段 **D12a~d**（格式化/无数据不
  编造/有数据显示/应答落模）。
  **⑪ 服务端持久化 + 未运行也显示（2026-11-12 用户两条追加口径「这个
  时间要服务端持久化」「就算没运行也应该显示累计运行时间」）**：
  · 持久化：`cfg` 的 **runtime 节**新增 `auto_run_ms` 键（与 run_mode
    同一套路）——`AutoContext.load_run_ms()` 启动恢复 /
    `persist_run_ms(ms)` 落盘；控制器 **结算时落盘** + **运行中每 60s
    周期落盘**（断电/崩溃最多丢一个窗口）。服务端重启从持久化恢复，
    不归零（回归：test_auto_cycle 持久化 3 条）。
  · 显示：`model.auto_run_ms_known`（收到过服务端数据才 True）——
    known ⇒ **永远显示格式化时长（0 也显示 0天 00时…）**；未知（未
    连接/老服务端）才显示「--」。不能拿 0 冒充"没数据"、也不能把
    "没数据"说成"没跑过"。
  · 首页字号二次放开（用户「下面还有空白，可以适当大点」）：数值字号
    回到 26/32 一档（电箱/压差/流量/浊度/搅拌泵频率 32、健康 26、
    状态/设定 20~24）、色点 14→20、名字小字 font_px(9)→12、间距放宽。
    **离屏测量脚本的"视口 480"比用户真实环境小**（用户窗口 ~812px 高
    ⇒ 视口 ~700）——以用户截图为准，别拿离屏小视口当唯一判据。
  **⑩ 定宽标签裁字三坑（2026-11-12 用户截图「字显示不全」实锅，
  同一处连踩三次）**：① 定宽必须按**完整文本**测 —— 只按"只有时长"测
  ⇒ 少了「自动运行 」前缀 ⇒ 标签右对齐时**宽度不足从左边溢出** ⇒
  「自动」两字消失；现拆成两个标签（标题独立 + 时长定宽）。② **QLabel 的
  `minimumSizeHint` 不认 `setMinimumWidth`** ⇒ 布局按 sizeHint 分配，
  而初始文本「--」sizeHint 很小 ⇒ 标签被压到 297px（比需要少 17px）
  ⇒ 又裁一次；须用 `setFixedWidth`（布局压不动它）。③ **测宽要用标签
  自己的字体**：标签 QSS 是 caption 字号（18px），与窗口默认字体不同，
  拿窗口字体测 ⇒ 窄 17px；且 **QSS 要 polish 后字体才最终确定** ⇒ 构造
  那一刻测的值不准 ⇒ 刷新时按 `label.font()` **复核一次**定宽
  （`_auto_run_apply_width`，宽度通常只变一次故不抖）。另：天数封顶
  「999+天」（2.7 年），否则天位数变长又会顶破定宽。回归锁加 D12e~h
  （定宽容得下最长串/标题独立不被裁/实际文本放得下/天数封顶）。
- **历史入库触发方式（2026-11-12 用户定案「在远端增加设置历史输入入库，
  触发方式是持续采集还是自动运行采集」）**：远端设置「数据存储」卡新增
  **入库触发**下拉（storage 节键 `historyTrigger`）：
  · `always`（**缺省**）= **持续采集入库**（与自动模式无关，保持既有行为）；
  · `auto` = **仅自动运行时入库**（控制器在跑才记；停机/手动期间不记）。
  只影响**落盘**（`_store_loop` 里 `if not self._history_should_store():
  continue`）——遥测采集、界面推送、设备健康照旧（界面数据不缺），只是
  历史表里少了"没在自动生产时"的时段。判定抽成 `_history_should_store()`
  便于单测（`_store_loop` 是 while True 的循环）。
  三处 fail-closed：SET 收到非法值**明确报错**（不能让拼错的键把"停机期
  不入库"悄悄变成"一直记"）；配置文件里的非法值回缺省；客户端应答缺键
  （老服务端）保持缺省"持续采集"（不静默改语义）。
  回归锁：服务端新文件 `tests/test_history_trigger.py`（10 检：四种组合/
  缺省/落盘往返/非法值/SET 校验/GET 快照，已注册进 tests/run_all.py）；
  客户端远端设置段 **A7a~c**（下拉两项/回显/缺键不误改）。
- **自动循环模式「自动」（组合模式）服务端落地（2026-11-12 用户定案）**：
  循环顺序固定 **产水 →（压差或定时兜底）→ 反洗 →（定时）→ 顺洗 →（定时）
  → 产水**，直到人按停止。
  ① **压差触发**（产水→反洗）：读压差计（模拟量 CH0 经 `scale.engineering`
  换算 MPa，**与历史/首页同一条换算链**——`ctx.press_mpa()`），阈值 + 方向
  `gt`/`lt`（大于/小于才切）；**压差读不到（None）时不切**（不按缺数据误判）。
  ② **定时器兜底**：**默认不开启**；开启后产水到 `timerMinutes` 强制切反洗
  （防压差计坏掉把流程卡死）。
  ③ **反洗/顺洗时长**（分钟）到期各自切下一个。
  ④ **换相** = 停泵 + 按新模式重建阀目标（走一遍阀相→泵相），**累计运行
  时间不中断**；`_begin_mode_phase()` 从原 start() 里抽出来供两者共用。
  ⑤ **阶段记忆**（用户「停止/急停后从停止的状态继续运行，除非按了复位」）：
  `_cycle_resume_mode` 记住当前子阶段 ⇒ 再启动组合模式**从那一步继续**；
  `reset()` 清空记忆 ⇒ 下次从头（产水）开始。记忆在**服务端**（唯一事实
  源，客户端重启不丢）。**记的时机必须在 `_clear_phase_state()` 之前**
  （那里会清 `_cycle_root`）——踩过一次。
  ⑥ 配置节：auto 节内新增 `cycle` 子节（enabled/pressThreshold/
  pressCompare/timerFallback/timerMinutes/backwashMinutes/rinseMinutes），
  逐键 fail-closed 校验；服务端 `mode_config_error` 对组合模式改为校验
  **循环启用 + 三个子模式都配设备**（不再是"暂未实现"）。
  回归锁：服务端新文件 `tests/test_auto_cycle.py`（21 检：压差两方向/读不到
  不切/兜底默认关与开启/反洗顺洗时长/换相复位/阶段记忆四态/配置闸，已
  注册进 tests/run_all.py）；客户端远端设置段 **A8a~e**（控件齐全/兜底缺省
  关/方向两项/载荷键形/回显）。
  **两个真实坑**：① SET 只带 `cycle` 不带 `modes` 时，日志拼接那句
  `for k in modes_doc` 迭代 None ⇒ "TypeError: 'NoneType' object is not
  iterable"（现在用 `modes_doc or {}`）；② `get_event_loop()` 在**没有运行
  中 loop** 的线程会**新建**一个 loop ⇒ 异步任务投到没人跑的循环上（退出时
  "Task was destroyed but it is pending" + 动作丢失）⇒ 改
  `get_running_loop()`（`stop_all_devices` 与换相停泵两处）。
- **自动模式「换相策略」（2026-11-14 用户定案，三端同口径）**：阀门切换要
  时间（真机行程 ~9s），这期间**搅拌泵/进水泵**怎么办要可配，**逐泵一份**：
  - `stopRestart`（**缺省**）＝**保持之前的逻辑**：换相先停泵，阀到位后（泵相）
    再按新模式配置启动；
  - `carry` ＝**保持上一模式的状态**：换相途中该泵**不动**（运行中就继续运行）；
  - `direct` ＝**直接切到当前模式的状态**：换相**立即**按新模式的目标启停
    （与阀门切换并行）。
  **策略取自「即将进入的那个模式」**（目标模式）的配置——「进入本模式时泵怎么
  处理」；carry 说的"上一模式的状态"指**物理现状**。策略只管**换相窗口内**，
  换相完成（泵相/运行相）后一律按当前模式配置收敛。
  协议键（每模式）：`stirSwitchPolicy` / `inletSwitchPolicy`（三值：
  `stopRestart`/`carry`/`direct`）——Python `proto.py` ×2 与
  `common/XhsfLinkProto.h` **逐值同步**；C 端
  `HSF_AUTO_MODE_STIR_SWITCH`/`HSF_AUTO_MODE_INLET_SWITCH`。
  **实现要点（两端同款）**：① `carry` 且新模式**不选**该泵时，它在换相途中仍在
  转 ⇒ 必须记「待停集合」，**由泵相收尾停掉**（否则会一直转——泵相只"启动
  选中的"，从不停"没选的"）；② `direct` 的"立即启动"在泵相会再下发一次
  （START 幂等，无害）；③ 非法策略值**明确拒**（fail-closed，不静默回落缺省）。
  **UI**：桌面客户端「远端设置 → 自动模式配置」每个模式块两个下拉（Python
  `pages_other.py`、C 端暂不做）；**网页端同样两个下拉**
  （`webui_py/hsfweb/static/js/pages/page14_remote.js` 的 `SWITCH_POLICIES`）。
  **回归锁**：服务端 `server_py/tests/test_auto_switch_policy.py`（14 检：语义
  + 落盘往返）+ `server_py/tests/test_auto_scenarios.py`（**54 检**：12 组动作
  矩阵 + **6 组端到端**（真 sim + 真协议，受控注入压差触发换相，观测换相窗口内
  的变频器/进水泵状态））+ C 端 §S16（9 检：配置面）+ 客户端 A6p/A6q（8 检）+
  网页端 `webui_check.py` 的「网页端换相策略」段（8 检：API 往返 + 非法值拒 +
  前端 JS 接线静态检查）。
  **写这类端到端用例的三个坑**：① **受控触发**——用「阈值取当前压差×0.9 +
  比较方向取"小于" + 事后把模拟量通道 0 的 mV 拉低」来**在指定的时刻**换相
  （固定阈值要么一开始就切、观测到的是竞态，要么永远不切）；② **每组一个全新
  服务端**——共用一个会让上一组的残留（变频器还在降速）把下一组的启动拒成
  "设备忙" ⇒ 观测到的是**假红**；③ **诊断日志只取本次之后的行**——服务端日志
  是全程累积的，取最后一行会拿到别的用例的（我在这上面误判过一次"策略没生效"）。
- **网页版 webui + 模式下拉 op=5 + 报警主题常驻（2026-11-14 一轮三件事）**：
  ① **webui_check 现状**：`verify/webui/webui_check.py --server-port both`
     自管进程，**改前已是绿的**（C6100 145 + PY6200 143 = 288 通过 / 0 失败，
     退出码 0）——不是"红了要修"，而是**把新行为纳入门禁后仍然绿**
     （本轮 308 通过 / 0 失败）。口径提醒：网页端口**不写死**（本机 8080
     是别人的程序，hsfweb 实际在 8085/随机端口），脚本运行期解析；
     **只杀自己起的进程**（复用常驻 6100/8080 一律不动）。
  ①-2 **自起的 py sim 必须用「独立临时程序根」（2026-11-15 实锅）**：
     py 端的路径全是**程序根**相对（配置 `<base>/cfg/hsf_server_py.json`、
     账户 `<base>/cfg/hsf_users.json`、库 `<base>/hsf_data.db`），而
     `base_dir` 缺省 = **服务端程序目录** ⇒ **常驻 6100 那台与自起的 6200
     那台共用同一份配置与同一个 SQLite 库** ⇒ 两个真实故障：
     ① **运行模式互相覆盖**：常驻那台在自动模式 ⇒ 它每 60s 的
        `auto_run_ms` 落盘会把 `run_mode=1` 一起写回去 ⇒ 自起那台下次启动
        就是「自动模式」⇒ 手动模式段的读数/闸门全串味；
     ② **历史行混库**：常驻那台在自动模式跑 ⇒ 库里全是「自动·产水/反洗/
        顺洗」⇒ 断言「关键字筛『手动』列=模式 ⇒ 命中 > 0」必然 **0 命中**
        （实测 PY6200 段 1 条红，**根因是装置污染不是产品缺陷**）。
     修法：`--config <临时目录>/hsf_server_py.json` ⇒ `base_dir` 变临时目录
     ⇒ 配置/账户/库三样全隔离（配置内容照抄共享那份，**runtime 节清空**
     ⇒ 新实例从手动起步；账户库照抄）。C 端早就是「临时程序根」
     （`start_c_sim`），py 端这侧本轮补齐 ⇒ **两端对称**。
  ①-3 **webui_check 必须独占跑**（2026-11-15 实锅）：两个实例同时跑会
     抢 `6200`（sim）与 `8081`（web）⇒ 后起的那个 **bind 失败但探活照样
     通过**（撞上的是对方那台）⇒ 测的是**别人的进程与别人的状态** ⇒ 症状
     是「点动被自动闸拦」「自动模式段全红」这类**看不懂的红**（实测 7 条）。
     ⇒ 跑之前先确认 6200/8081 没人占（`netstat -ano | grep -E "6200|8081"`）。
  ①-4 **常驻 6100 那台的运行模式是「上一轮遗留」**：webui_check 的收尾只
     切**自己那台** ⇒ 别的实例若被留在自动模式，界面上会一直显示
     「自动运行中：远程控制已被禁用」⇒ 收工前顺手把常驻那台切回手动
     （`POST /api/mode {mode:0}`）。
  ② **模式下拉补 op=5（两端补齐，2026-11-14）**：`0x0115 op=5`＝**只选
     自动模式**（只记粘性 + 落盘，**不启动、不动设备、不判启动条件**）——
     现场物理启动按钮按「上次选中的模式」启动，客户端下拉不同步 ⇒ 服务端
     粘性为空/过期 ⇒ 现场按键被拒「未选择自动模式」。Python 服务端早就有
     （`proto.AUTO_OP_SELECT=5` + `ctl.mode=` + `ctx.persist_mode`），
     **C 端原先只认 op=1..4**（回「非法 op 操作字」）⇒ 本轮补齐：
     `XhsfLinkProto.h` 加 `HSF_AUTO_OP_SELECT 5`；C 服务端
     `HsfServerAutoMode_selectMode()`（记 `s_mode` + 落盘键
     **`run/auto_mode`** + setup 时恢复，恢复值过合法性校验，野值按未选
     处理）+ `HsfServerAutoMode_isKnownMode()`（合法集 = 三子模式 +
     组合循环 `auto`；组合循环的**配置**校验留到启动时刻）；C 客户端
     `HsfHomePage.c` 的**模式下拉挂 `XComboBox_activated_signal`**（**用户
     改选**才发；程序化回显只发 currentIndexChanged ⇒ 挂错信号会与
     「组合循环运行时把下拉切到自动」形成回环）⇒ 发 op=5 + 状态栏
     「已选模式：X（现场启动按钮将按此模式启动）」/失败「模式选择失败：…」。
     错误文案两端逐字同源：「非法 op 操作字（1启动/2停止/3急停/4复位/5选
     模式）」「未知自动模式 id（%s）」。
     回归锁：C `hsf_regression_test.c` **§S15**（12 检：记粘性/不启动/
     未知与空 id 拒且粘性不变/组合 id 合法/落盘/跨 setup 重启恢复/清键回
     空）；GUI 探针 `hsf_vfd_probe.c` **[A4]**（改选触发 + 程序化回显**不**
     触发，用状态栏文案当可观测点）；`webui_check.py` 新段「模式下拉
     op=5」（6 检 × 两端）。
  ③ **报警主题改常驻订阅（网页版缺口）**：`TOPIC_AUTO_ALARM(0x0001000B)`
     是**事件流**（急停/阀超时/动作失败）不是周期遥测——网页端原先只在
     「报警页开着」时订它（`page11_alarm.js` 的 topics）⇒ 用户在别的页面
     时上游根本不推 ⇒ 那些告警**永远收不到也不落本地报警档**（"行数据源
     与订阅集合分离"家族的网页版）。修法：`hub.GLOBAL_TOPICS` 加
     `TOPIC_AUTO_ALARM` ⇒ **无条件订阅**（`_on_publish` 收到即喂
     AlarmLog + 广播 alarm 事件，广播不分主题 ⇒ 报警页开不开都收得到）；
     `page11_alarm.js` 的 topics 去掉 0x0001000B（单一事实源）。
     **两端口径**：C 客户端连接期常驻订阅 + Python 客户端
     `bridge.PERSISTENT_TOPICS` 早就是常驻 ⇒ 本轮补的是网页版这一家。
     回归锁：`webui_check.py` SSE 段 ②（**本连接不订它**时
     `desiredTopics` 仍含它 + **上游实际订阅表**里也有它）。
  **已知既有红（不是本轮引入，勿重复排查）**：GUI 探针
  「点设备状态表表头：无排序指示」1 条偶发/恒红 + 退出段 segfault（探针
  teardown double-teardown UAF，已挂账）；C 回归 **S13「停止关阀复位：阀1
  DO0 清零」偶发红**（用例自带注释：窗口 2500→4000→6000 放宽过仍偶发，
  根因是停止沿采样偶发晚到一拍 + CPU 抢占）。**归属判定法**：改完把
  嫌疑段临时 `#if 0` 重编重跑做 A/B（禁 156 OK/1 FAIL ⇒ 启 158 OK/1 FAIL
  ⇒ 那条红与本轮无关）。
- **客户端管网页端起停 + 端口（2026-11-14 用户「在客户端中增加设置可以控制
  web 开启与端口号」「我让你起的是python客户端」）**：**两端都做了，但以
  Python 桌面端为主**（用户日常就用它；C 端暂不用但功能已就位）。
  - **Python 端（主）**：`client_py/hsfsrv_client/webserver.py`（控制器）
    + `webui_py/hsfweb/embed.py`（**同进程内嵌**：后台线程跑网页服务器）。
    **为什么内嵌而不是拉子进程**：① 客户端一退，线程跟着退 ⇒ 网页端口必然
    释放，**不留孤儿进程**（子进程方案实测踩过：客户端被强杀后子进程活下来
    占着端口，下一次启动报「端口被占用」）；② 解释器就是客户端自己这个，
    不用另外找 python；③ 装配逻辑与 `python -m hsfweb` 同款（同一份
    hsfweb 包），不存在两套实现。
    设置键 `webEnabled`（缺省**开**）与 `webPort`（缺省 **8085**——**刻意
    不用 8080**：本机 8080 常被别人的常驻程序占着）；界面落在**本地设置页
    新增分区④「网页服务」**（开关 + 端口微调框 + 状态行 + 说明）。
    起停时机：**登录之后**拉起（此刻才知道上游到底是哪台服务端——网页端
    连的是**同一台服务端**，不是客户端）；`--auto`/`--offline`/`--shot`/
    `--seconds` 这些无人值守路径**不起**；退出时显式停。
    端口改动**去抖 800ms** 再提交（边拖微调框边重启网页端会把机器折腾得
    没完）。回归锁：客户端回归 **§22E**（8 检：控件齐全/缺省 8085 不占
    8080/开关缺省开/端口框与设置一致/状态行有文案/关⇒落盘+停服/开⇒落盘+
    起服（带端口）/改端口⇒落盘）——起停用**记录桩**替换 `webserver.start/
    stop`，**回归里不真起网页端**（不许抢端口）。
  - **C 端（已就位，暂不用）**：`client/ui/HsfWebServer.{c,h}`（子进程
    `python -m hsfweb`，解释器 `HSF_WEB_PYTHON`→`python`→`python3`，包目录
    `HSF_WEB_DIR` 可覆盖，stdout/stderr 落 `cfg/hsf_web.log` +
    `cfg/hsf_web_err.log`）；设置键 `WebServer/Enabled`/`WebServer/Port`
    （缺省同上）；界面落在**本地设置页分区④「网页服务」**。CI/离线路径
    （`--no-login`/`--login`/`--shot`/自动退出秒数）**不拉**；登录成功后
    重启一次对准上游；退出时停子进程。
    回归锁：GUI 探针 **[A5]**（6 检，含「探针内不起网页服务」防留后台进程）。
  **两个真坑（都实锅，2026-11-14）**：
  - **同一日志文件开两次 ⇒ 子进程根本起不来**（C 端方案）：框架的文件打开用
    `FILE_SHARE_READ`（**不共享写**）⇒ stdout 与 stderr 指同一个文件时
    第二次开必失败 ⇒ 重定向句柄为空 ⇒ 报「Failed to start process」
    （**看着像 python 找不到，其实 python 好好的**）⇒ stdout/stderr
    **必须两个文件**。
  - **布局当控件用 ⇒ 建页即崩**（C 端方案）：把临时分配的 `XGridLayout` 用
    `XBoxLayout_addWidget` 塞进父布局 ⇒ 父布局按控件管理它 ⇒ 建页期 AV
    （栈：`HsfSettingsLocal_create → XBoxLayout_addWidget → XWidget_setParent
    → XPlatformAccessibility_notifyWidget → XAccessible_isValid`）⇒ 布局
    **必须成员化**且用 **`addLayout`** 接进父布局（本文件既有写法：
    `XHBoxLayout` + `XBoxLayout_addLayout`）。

- **导出：不限量 + 流式 + 绝不影响自动控制（2026-11-14 用户口径）**：
  用户原话「导出数据有限制吗，我需要不管多大的数据都可以导出，并且要不影响
  程序的其他功能使用，比如自动控制是绝对不能影响到的，可以用流式导出」。
  ① **改前的真相**：CSV 两端都已流式不限行 ✓；**XLSX 两端都锁 12000 行**
     （攒齐所有行 → 收尾一次性写，超出**显式截断**）；**网页端更严重**——
     纯 Python 拼 7000 行 × 19 列要 **40+ 秒纯 CPU** 跑在 asyncio 事件循环
     里 ⇒ 心跳停发 ⇒ 上游按「静默 6s」判线断链 ⇒ **导出刚结束网页自己的
     上游链路已死**，后续点动/配置全回 503（2026-11-13 实锅）——这就是用户
     担心的「导出影响自动控制」的真实现场。
  ② **改法**：纯标准库**流式 XLSX 写出器**（`zipfile` + SpreadsheetML，逐批行
     直接写进 zip 的 sheet part）——`client_py/hsfsrv_client/xlsx_stream.py`
     与 `webui_py/hsfweb/xlsx_stream.py`（**两端各一份**：纯标准库包不许互
     相依赖，仓库既有惯例就是三端各一份）。客户端历史页改 `_export_write_xlsx
     _rows` 逐批写并去掉 12000 闸（`max_rows=0`）；网页端 `export_stream` 的
     XLSX 分支改流式（写临时文件 → 分块回读给浏览器），删掉「攒齐再一次性
     拼 + 丢线程」的旧路。
  ③ **实测（判据）**：30k 行 xlsx 0.16s；**60k 行导出期间事件循环最大停顿
     63ms**（6s 静默判线阈值的 1/95）⇒ 心跳不断、链路不死、自动控制不受影
     响；产物 zip 完整、各 XML 部件可解析（合法 OOXML，与网页端已验证的
     `build_xlsx` 同款结构）。
  ④ **回归锁**：客户端 `test_hsf_regression.py` §21 的 **Q4 段**（13000 行
     > 旧 12000 上限 ⇒ 截断一旦回潮即红；产物合法 + 行数=13001）；网页端
     `verify/webui/webui_export_browser_check.py` 新增「**XLSX 与 CSV 数据
     行数一致**（XLSX 不再截断）」。
  **测试坑**：导出测试的**假桥同步回调**会让「回调里再取下一批」递归爆栈
  （13000 行 ⇒ RecursionError）——那是**假桥**的问题不是产品的；假桥改成
  `QTimer.singleShot(0, …)` **异步交付**（贴近真桥：应答经网络线程→Qt 信号
  回 UI 线程）并泵事件循环等终态即可。
  **C 端导出未改**（用户「C 版本暂时不用了」）⇒ C 端仍是 XExcel 内存闸 +
  12000 行上限（要用时再改：把 C 端也换成流式写出器即可）。
- **Python 服务端事件循环时延（2026-11-14 用户「检查下 python 的服务端是否可能
  出现事件循环停滞，影响自动模式的及时响应」）**：查出来了，**确实会**，两个
  病根，都已根修。
  ① **WAL 检查点内联在写路径**：SQLite 的自动检查点挂在写连接上
     （`wal_autocheckpoint=128`）⇒ **检查点的磁盘成本落在「写这条日志」的
     那一次 execute 里**。实测 12MB 库：单次写入 max **134~146ms**（200 次
     里 6 次 >10ms）⇒ 事件循环被堵 ⇒ **每次控制/连接/告警事件都可能把
     自动控制的应答顶掉 130ms**（自动控制往返：中位 0.9ms、**p95=134.6ms
     / max=147.7ms**，120 次里 18 次超 10ms）。A/B 实证：关掉自动检查点
     ⇒ 写入尾巴 146ms → **0.1ms**。
     **修法**：写连接 `wal_autocheckpoint=0`（内联写入恒 0.1ms 量级），
     检查点交给**独立连接的后台线程**（`Store._start_checkpointer`，每 2s
     一次 `PRAGMA wal_checkpoint(PASSIVE)`）⇒ PASSIVE 不阻塞写者 ⇒ 写与
     检查并行。
  ② **同一会话的请求串行 await**：`xlink.py` 的 `_on_frame` 里
     `await self.server.dispatch(...)` ⇒ **一个慢查询把这条会话后续所有帧
     压住**。历史分页带关键字时要在库里把 19 个显示表达式逐行拼一遍做
     LIKE（12MB 库实测 **273~397ms**）⇒ 期间排队的自动控制命令**等到查询
     结束才被处理**（实测首条往返 **238ms**，其余 0.5ms）。
     **修法**：纯读的分页命令（0x0118 历史 / 0x0119 日志）**另起任务**
     并发处理（`xlink.CONCURRENT_READ_CODES`）；**控制命令（0x0115 自动
     控制的启动/停止/急停/复位序列）仍串行** ⇒ 先后语义不能乱。应答靠
     requestId 归属 ⇒ 并发安全。
  ③ **查询挪出事件循环**：历史/日志分页与统计读改走**独立读连接**
     （`Store._reader`，与写连接互不阻塞）⇒ 可以在工作线程里跑
     （`asyncio.to_thread`）⇒ 慢查询不再阻塞事件循环。**写路径的轻量读
     （`_logical_bytes`/`row_count`）仍用写连接**——它们若去抢读连接那把
     锁，会与长查询互等 ⇒ 又把循环堵回去（实测首条自动控制被顶 233ms）。
  ④ **容量维护挪出写路径（2026-11-14 追加）**：容量维护（ring 裁剪 /
     还盘 / WAL 归并）是一次性重活（实测整轮裁剪 108~271ms、WAL 截断
     187ms、VACUUM 23ms），原先**同步**做在写路径上 ⇒ 事件循环被堵 ⇒
     自动控制应答被顶到几百毫秒之后。改法：
     · 判定（`_content_bytes()`，纯 PRAGMA+stat，亚毫秒）留前台；
     · 动作交**后台维护线程**（独立连接 + **分块小事务**：删 1000 行
       = 0.9ms 写锁持有 ⇒ 前台最多等 ~1ms；实测前台写 max **0.2ms**，
       改前 554ms）；
     · 后台裁剪循环内**只用 PASSIVE 检查点**（TRUNCATE 要等所有写者
       ⇒ 并发写下会挡前台 554ms！）；
     · **分段滚动（rotate）拆两拍**：前台标记 → 后台把 WAL 归并并置位
       `_wal_drained` → 前台下一次写入才改名归档（改名本身只要 1.8ms）；
     · **改名归档必须先关掉所有别的连接**（后台线程持有一条 + 读连接）
       ——Windows 下文件被打开时改名报 WinError 32（这是我加读连接引入
       的回归，已修）；`-wal/-shm` 要与主库**一起归档**（否则残留文件
       与新库同名 ⇒ SQLite 可能把旧 WAL 当新库日志读进去）；
     · **限额判定口径**：保持**物理占用**（逻辑 + -wal 文件大小）——
       限额是给现场守**磁盘**的 ⇒ 不能改成「内容」口径（那会让「上限
       120KB、只长 40 行」的用例测不到策略，客户端回归 B2a 就是这么红的）。
       代价是 -wal 文件在归并后**不变小**（内容走了、文件还在）⇒ 按大小
       判会虚高 ⇒ 故后台维护线程**定期把 -wal 归并并截断到 0**（-wal 平时
       就是 0 字节 ⇒ 既准确又不会误触发裁剪）；
     · **归并要自适应、不能攒一大坨**：检查点是**一个事务** ⇒ 合并期间持有
       写锁 ⇒ 前台写者要等 ⇒ 合并时长 ∝ WAL 大小（实测 351MB 的 WAL 合并
       一次挡前台 **116ms**）⇒ 故 **WAL 超过 4MB 就立刻归并**（此时只有
       4MB ⇒ 合并 ~1ms），否则每 2 秒兜底归并一次；
     · **残留请求守卫**：上限被放开/调大后，之前唤醒的那次后台裁剪
       **不再执行**（否则「首轮必删」会一路删到水位甚至删光 ⇒ 白删数据，
       实测上限放开后仍被裁掉 3600+ 行）。
     · **别用脚本改本文件**（行尾是 CRLF，脚本按 \n 匹配会只改一半 ⇒
       半改半不改；2026-11-14 踩过）。
  **实测对比（同一台机器、同一服务端）**：自动控制往返
     改前 中位 1.0ms / **p95 127.4ms / max 154.1ms**（150 次里 22 次
     >10ms）⇒ 改后 中位 1.0ms / **p95 2.2ms / max 3.6ms（0 次 >10ms）**；
     4 万行库的关键字查询（325ms）进行中，自动控制往返仍 **0.4ms**；
     容量维护进行中 **0.4ms**；8 客户端并发 **1.1ms**。
  **回归锁**：新增 `server_py/tests/test_event_loop_lag.py`（10 检，注册进
     `tests/run_all.py`）：落盘压力下 p95/max ≤ 60ms、写入尾巴 ≤ 60ms、
     **慢查询进行中自动控制 max ≤ 60ms**、**容量维护进行中 max ≤ 60ms**、
     外加两个「对照组」断言（库里确有 4 万行 + 关键字查询在 store 层确实
     >60ms，否则等于没测到东西）。
     **A/B 实证**：临时撤掉并发分页 ⇒ 该用例红（控制 max=**317.9ms**）
     ⇒ 锁是真的。**量测坑**：基类 `Client.request` 每次 `pump(0.08)` 才看
     一次 pending ⇒ **量测粒度 ≥80ms**（会量出 95ms 的"假尾巴"）⇒ 本测
     试用自带的 `_FastClient`（收包超时 5ms 的紧循环）。
  **回归锁之二（矩阵）**：`server_py/tests/test_auto_control_latency.py`
     （26 检，注册进 `tests/run_all.py`）——**8 种负载逐个打**自动控制：
     ① 空闲基线 ② 落盘压力 ③ 慢历史查询 ④ 慢日志查询 ⑤ **自动模式
     正在跑**（控制器每拍有阀门/泵动作）⑥ **导出风暴**（8 条慢查询在途）
     ⑦ **容量维护进行中** ⑧ **8 客户端并发**。每种都断言 p95/max ≤ 60ms。
     这份是「**任何情况都不许延迟**」的正面清单（用户口径）。
  **顺带清掉一条既有红**：`tests/test_ports.py`「不得绕过 ports.py 写死
     端口」——`client_py/hsfsrv_client/__main__.py` 里的 `6100` 字面量是
     上一轮留下的 ⇒ 改成 `ports.C_SERVER_PORT`（客户端/服务端两侧的
     ports.py 是同一份内容，见该测试的「两份文件内容应完全相同」）。
  **对齐测试的环境依赖（踩过）**：`tests/test_server_alignment.py` 会
     **复用** 6100/6200 上在跑的 sim ⇒ 若那个 sim 已被留在「自动模式」，
     两端文案必然对不上（6 条红："自动运行中：远程控制已被禁用" vs 具体
     原因）⇒ **不是代码回归**（把 sim 切回手动后立刻 248/0 全绿）。
     跑这个测试前先确认 6100 上那个 sim 处于**手动模式**。
- **控制全流程模拟测试（2026-11-12 用户定案「做模拟测试吗，全部控制
  模式全部流程，期间还要有急停(急停后要显示复位才可以在启动)、停止 等
  各种因素」）**：新文件 `server_py/tests/test_sim_full_flow.py`（**真实全栈
  sim**：虚拟设备 + 真 XLink + 真客户端，25 检，已注册进 tests/run_all.py）：
  A 单模式产水全流程（手动模式启动被拒 → 切自动不自动启动 → 阀相→泵相→
  运行 → 停止全停）；B **急停**（`model.set_estop(False)` 模拟按下 ⇒ 控制器
  停 + 设备全停 + 锁存 + 报警；**急停后启动被拒且提示复位** ⇒ 复位后才可
  启动）；C **报警锁存**（把模拟行程调大 > 超时造卡阀 ⇒ 报警 + 停止 +
  autoFault ⇒ 未复位启动被拒 ⇒ 复位可启动）；D **组合模式循环 + 阶段
  记忆**（产水→压差切反洗 → 停止记住阶段 → 再启动从反洗继续 → 复位 ⇒
  从头产水）；E **权限**（普通用户启动/复位被拒）；F **模式切换**（运行中
  切回手动 ⇒ 全停）。**三个写测试时的坑（都踩过）**：
  ① **急停松开后要等一拍**（≥1.2s）再复位：按钮站读的是数字量模块**缓存**，
  虚拟模型改了要等下一拍 poll 才反映；急着复位的话服务端仍看到"急停还
  按着"⇒ `_check_estop` 立刻重新锁存（这本身是**正确**的安全行为）。
  ② **等"运行相"而不是等 `autoRunning`**：`autoRunning`=控制器在跑（启动
  那一刻就 True），设备真跑起来要看 `autoPhase == "running"`；只等前者
  会读到"还在阀相"的快照 ⇒ 断言泵已启动时偶发红（竞态）。
  ③ **裸 socket 客户端没有心跳** ⇒ 空闲 6s 被服务端判静默断链：等待
  循环里必须周期性发请求（查状态兼保活）。
- **自动配置合成一张卡 + 一次提交（2026-11-12 用户定案「这两个卡片合起来
  一次提交设置」）**：远端设置的「自动模式配置」与「自动循环配置」是**同一份
  auto 配置节** ⇒ 现在**合成一张卡**（卡内用小标题「自动循环（组合模式
  「自动」）」分段，中间**没有卡边界**），底部**一个**「应用自动配置（需
  管理员）」按钮，一次下发 `{modes:{…}, cycle:{…}}`（服务端 0x0117 支持
  同批）。此前是两张 Card 各一个「应用」⇒ 看着像两份要分别保存的配置。
  `_apply_auto` 一次提交，`_apply_cycle` 已删（别复活）。
  回归锁：客户端远端设置段 **A9a/A9b/A9c**（只有一个应用按钮 / 一次载荷
  同含 modes 与 cycle / **自动配置只有一张卡**——断言只数标题含"配置"的
  卡，「自动运行」卡是另一功能不算）。
- **客户端「闪烁→永远未连接」双因（2026-11-11 板端实锅）**：桌面 Py 客户端
  连板端（192.168.1.249:6100，板子此刻由 **C 栈 HsfServer** 服务；Python
  hsfserverpy 单元抢口失败已 inactive——`ss -tlnp` 先看 6100 是谁，别按
  单元名想当然）。**因一（链路层，治不了本的治标）**：板端 wlan0 信号
  **-72dBm**、3~5% 丢包、RTT 4~1344ms 毛刺 ⇒ 客户端 6s 静默判活
  （XLINK_HEARTBEAT_TIMEOUT_MS）在坏窗口反复触发 ⇒ 掉线重连循环＝界面
  闪。`iw dev wlan0 set power_save off` 已执行并 NM 持久化（连接
  「杭煤科临江」`802-11-wireless.powersave=2`），毛刺仍在 ⇒ 是 RF 弱信
  号，治本 = AP 挪近/换 5G/外置天线；**板 eth1=192.168.0.10 与桌面经路
  由可达的另一台 192.168.0.10 撞号**（桌面 ARP 无条目 + 回包 TTL=253
  实证不是同一台）⇒ 有线直连当前不可用，要走有线得先把板 eth1 挪段
  （待拍板）。**因二（客户端真 bug，已根修）**：客户端最终彻底躺平在
  「未连接」且零重连——`xlink.py _thread_main` 只接 `Exception`，
  **CancelledError（3.8+ 属 BaseException）把网络线程打死且零日志**；
  服务端日志零「登录被拒」⇒ 排除 30 次策略收手。修法：改接
  `BaseException` 留痕 + 非 close()（auto_reconnect 还开着）时**线程
  自愈重建**（旧线程 finally 里 sleep 1s 节流后起新线程接棒同一
  `_thread_main`；close() 的 join 读 `self._thread` 最新值，新旧任一被
  等都能等对）。回归锁：§21d **A14a~c**（终止留痕/新线程接棒/close 后
  不再重建，`--only=account`）。**排查口诀**：客户端躺平且不重连 ⇒ 先
  看服务端有无「登录被拒」（有=策略收手 30 次上限），没有=线程死亡
  （本轮路径）。
- **周期推送不得阻塞事件循环（2026-11-15 用户口径「服务端不能因为某个
  客户端读不动，就让自动控制停滞」）**：
  ① **根因**：Python 侧 XLink 的推送路径是「写完 `await writer.drain()`」，
     而**周期推送是在服务端的 tick 里 await 的**（`app._publish_telemetry`
     → `server.publish` → `session.send_publish`）。`drain()` 的语义是
     "等发送缓冲降下来"，对端读不动（TCP 窗口关）时它会**一直等** ⇒ 一
     个卡死的客户端就能把**整个事件循环的 tick 卡住** ⇒ 自动控制的命令、
     全部遥测、告警推送一起停摆。
  ② **C 端没有这个坑**：XLink 用**发送队列 + 队满拒帧**（`XLinkSession.c`
     的 `m_txQueue`：队满时 publish 失败并记一次推送失步，**从不阻塞发布
     方**）⇒ 两端同口径，Python 侧改成"只写不等"。
  ③ **改法**：`Session._send_frame(..., drain=False)` ⇒ 周期推送**只写不
     等**；发送缓冲超 `SEND_BUFFER_LIMIT`（4MB）就**丢这一帧**（遥测是周
     期帧，丢一帧下一秒补上）；**应答帧**（含大导出）与**订阅补发/首连
     补发**仍走 `drain=True`（必须确保送达，且它们不在 tick 里 await）。
  ④ **回归锁**：`server_py/tests/test_publish_no_stall.py`（6 项：假 writer
     的 drain 永不返回 ⇒ 周期推送必须立即返回且不调 drain；应答仍等 drain；
     缓冲超限丢帧），已注册进 `tests/run_all.py`。
  ⑤ **排查口径**：服务端"界面全停/命令发不出去"而 CPU 正常 ⇒ 先查是不是
     某个会话的发送缓冲堵着（tick 被 await 卡住）；修复前先看
     `journalctl` 里的"推送失步"计数。
  ⑥ **同源的第二个坑（2026-11-15 实锅）**：改推送路径时把缓冲大小写成
     `writer.get_write_buffer_size()` ⇒ **StreamWriter 没有这个方法**
     （在 `writer.transport` 上）⇒ 每拍抛 AttributeError ⇒ **整个 tick
     停摆**（遥测/按钮灯/自动控制一起停），而**命令往返照样正常**（命令
     在会话协程里处理，不在 tick 里）⇒ 时延用例全绿、界面全死 ⇒ 极难查。
     ⇒ 教训：**tick 里任何一处抛异常都等于停摆**（本项目的 tick 是"一个
     大 try"）⇒ ① 取缓冲大小必须 try/except 兜底；② 回归锁要**显式盯日志
     里的「tick 异常」**（`tests/test_event_loop_lag.py` 末尾那条断言），
     并且**客户端必须订阅遥测**（不订阅 ⇒ 服务端不推 ⇒ 那条路径根本没跑
     ⇒ 断言形同虚设；A/B 实测：注入 bug ⇒ 该断言变红 ⇒ 门是真的）。
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
- **按钮组灯与「设备在动」（2026-11-15 用户定案，**取代 2026-11-10 的
  「手动模式一个 DO 都不写」**，两端同口径）**：
  ① **手动模式的启动灯 = 现场还有设备在动**：搅拌泵（变频器）在转 ∨
     进水泵在开 ∨ **任意一路电动球阀在开位**。用户原话「手动模式下除非
     有搅拌泵和进水泵任意一个在开着，才能亮启动灯」「手动模式下再加上
     电动球阀吧，任意一个球阀开着也是亮启动灯」。**旧口径的代价**：手动
     模式的灯停在旧状态 —— 现场什么设备都没在跑，启动灯却亮着，操作员
     误判（2026-11-15 现场截图）。
  ② **停止灯 = 非在动**（与启动灯严格互斥）、**复位灯 = 按住复位**（点动
     镜像）——两种模式同款。
  ③ **手动模式的停止/急停一律关阀复位**：现场按停止 ⇒ 变频器停 + 进水泵
     关 + **全部电动球阀关回**；急停同款（用户口径「按下停止也能全部
     关闭」「按下停止也关闭全部球阀，急停也有效果」）。**自动模式维持逐
     模式配置**（`resetValvesOnStop` / `resetValvesOnEstop`，2026-11-12
     口径：急停缺省阀保持现状）——手动模式没有工艺序列要保护。
  ④ **阀的"在开"判据 = 开到位反馈 ∨ 最近一条命令是开**（**不用"还没到关
     位"**）：阀动作中两个到位反馈都不吸（行程 ~9s）⇒ 单看反馈会漏掉"正在
     开到位途中"；[2026-11-25 订正] 现场 1~6 阀全部接线、都有到位反馈，
     不再有“无反馈阀”这一例外。下了关命令 ⇒ 灯灭。
  ⑤ **实现落点**：Python `buttons.py`（新增 `set_manual_running()` 外部
     驱动）+ `autocontrol.AutoContext.manual_running()`（在动判定）与
     `_reset_valves_on_stop/_on_estop()`（手动模式强制 true）；
     C `HsfServerAutoMode.c`（`amManualRunning()` + `s_autoMode` 模式位 +
     `amResetValvesOnStop/OnEstop()` 手动强制 true）。**客户端零改动** ——
     灯是服务端写的 DO，客户端只读回显（按通道名匹配，两端同款）。
  ⑥ 锁：服务端 `tests/test_mode_lamp_rules.py`（**31 项**：A 灯归属 6 +
     B 在动判定 7 + C/D 手动停止与急停全停 12 + E/F 切模式 8）；
     `test_units.py` 的灯用例（手动两态）；C `hsf_regression_test.c`
     **§S9**（灯归属 4 + 手动在动 3 + 手动停止全停 3）。
- **运行保护「进水泵空管超时」+ 流量计告警入报警表（2026-11-15 用户定案，
  两端同口径）**：
  ① **新增配置节 `protect`**（远端设置 → 新卡「运行保护」，位置在采集配置
     之后、数据存储之前）：`inletNoFlowTimeoutMs`（毫秒，界面用**秒**）
     + `inletNoFlowStop`（bool，超时后是否**停止设备**）。键名两端逐字同值
     （PY `proto.CFG_SECTION_PROTECT` / C `HSF_CFG_SECTION_PROTECT`）。
  ② **判据**：① 进水泵在开（DO6 回显）∧ ② 流量计**在线且数据新鲜**并报
     **空管** ∧ ③ 该状态**连续**成立 ≥ 时限 ⇒ 出一条告警（kind=流量计）
     + 按配置**停止设备**（等价现场停止按钮：变频器停 + 进水泵关 + 阀按
     模式规则复位）。**计时算"连续成立"的时长**，不是"泵开了多久"——
     后者会让流量计闪一次空管位就立刻停线。
  ③ **缺省=关闭该保护（0ms）**：现场实测板端流量计在**有流量**（3.6 m³/h）
     时也一直挂着"空管"位 ⇒ 缺省启用 + 时限偏短 ⇒ 每次开泵一分钟就停线。
     **读不到 ≠ 空管**：流量计离线/数据陈旧时不判定（拿"没数据"当"空管"
     会把整条线停掉）。
  ④ **流量计告警接入报警信息表**（用户提问的答复：原先**没有** —— 报警位
     只作为遥测位图下发，不进报警页也不进 alarms 表）。现按**边沿**上报：
     报警位出现/消失各一条 → `TOPIC_AUTO_ALARM`（落客户端报警页）+ alarms
     表（kind=`flowmeter`/source=流量计）。**首帧只记基线不报**（启动时
     流量计可能已经挂着空管 ⇒ 不是"新发生的报警"，否则每次重启多一条噪音）。
  ⑤ **告警帧新增可选键 `dev`**（设备类别，proto.KIND_* / HsfDeviceKind_*）：
     缺键 = 老服务端 ⇒ 客户端按"自动运行"处理（不改老行为）；带上则报警
     表那一行的"类型"列显示**流量计**而不是"自动运行"（现场看报警表时
     设备类别不能撒谎）。两端客户端的告警类型映射加第 6 类「流量计」。
  ⑥ **实现落点**：Python `app.py`（`_protect_step` / `_flowmeter_alarm_step`
     / `_publish_alarm_event`）+ `config.py`（protect 段落盘）；C
     **`server/HsfServerProtect.c`（新模块，判定独立出来才能被回归锁直接
     驱动）+ `HsfServerApp.c`（只做发布+入库）**。配置读取是**单一事实
     源**（`HsfServerProtect_timeoutMs/_stopDevices`）。
  ⑦ **框架侧补了一个 sim 能力**：`XVirtualModelFlow_setAlarm(model, idx,
     on)`（0x1022~0x1025 报警位）——模拟流量计原先只建模流量、报警位恒 0
     ⇒ 这条分支在 sim 里永远触发不到（死代码，回归锁也钉不住）。
  ⑧ 锁：服务端 `tests/test_protect.py`（**27 项**：A 配置面 7 + B 保护判定
     12 + C 告警边沿 5，已注册进 `tests/run_all.py`）；C
     `hsf_regression_test.c` **§S17**（11 项）；**跨端键集**由
     `tests/test_server_alignment.py` 的 0x0116 对照（新增 protect 节）锁。
- **运行保护②：压差过大保护（2026-11-15 用户定案，两端同口径）**：
  ① **protect 节再加三个键**：`pressHighEnabled`（bool，缺省 **false = 不
     启用**）、`pressHighLimitMpa`（double，缺省 **0.8 MPa** —— 实测正常
     运行压差约 0.5 MPa，留约 1.6 倍余量）、`pressHighStop`（bool，缺省
     true = 停）。**为什么缺省不启用**：压差上限与现场工艺强相关（滤芯阻
     力、清洗周期都会改）⇒ 写死一个值必然误动作 ⇒ 由现场自己定阈值并
     决定开不开。
  ② **判据**：压差（模拟量通道 0 换算出的 MPa）**连续**超过上限 **2 秒**
     ⇒ 触发（去抖 2s：滤芯堵塞是分钟级过程，但启泵水锤等瞬时尖峰会误动作）。
     **读不到 ≠ 压差过大**（通道离线/未标定 ⇒ 不判定、不动作）。
  ③ **动作**：出一条 kind=8「压差过高」告警（进报警历史表 + 客户端报警
     页，dev=模拟量）+ 按 `pressHighStop` 决定**是否停设备**，并**锁存**
     （压力回落后不自动恢复，必须人工复位才能再启动）。
  ④ **压差取数同源**：Python `ctx.press_mpa()` / C `HsfServerAutoMode`
     用的 `hsfAppAutoPressureMpa`（同一条换算链：界面压差卡、历史
     press_mpa、循环切相阈值、这里）⇒ C 侧经
     `HsfServerProtect_setPressureFn()` 注入（保护模块自己不写换算）。
  ⑤ **界面单位 kPa、协议单位 MPa**：桌面 C 端只有整数微调框（无小数微调
     框）⇒ MPa 的 0.8 表达不了 ⇒ 界面统一 kPa（800 kPa = 0.8 MPa），
     提交时 ÷1000。三端（桌面 Python / 桌面 C / 网页）同款。
  ⑥ **锁存未解 ⇒ 保护不重复动作**：压差还高着时若每 2 秒报一次，报警表
     会被刷爆（**实锅**：一次超压 ⇒ 连续两条告警 + 两次全停，见日志）⇒
     报警锁存未解（人还没确认）时**不重复动作**，等人工复位后才有下一次机会。
     空管保护同款。
  ⑦ 锁：服务端 `tests/test_protect.py` **E 段 12 项**（缺省不启用/未启用
     不动作/去抖前后/文案/锁存/不重复报警/未超限/读不到/只报警/落盘往返）；C
     `hsf_regression_test.c` **§S19**（9 项，压差用注入的假取数回调）；
     跨端键集由 `test_server_alignment.py` 的 protect 节锁（五个键）。
- **报警锁存 + 自动停机 + 手动复位（2026-11-15 用户定案，两端同口径）**：
  ① **变频器故障此前不进报警历史表**：只出现在设备健康里（首页设备表/报警页
     的健康行），**不入 alarms 表、不锁存、不停机** ⇒ 设备带着故障继续跑。
     现按**边沿**（故障出现时）出一条 **kind=7「设备故障」**告警 → 进 alarms
     表（kind=`device`/source=变频器）+ 客户端报警页（带 `dev` 键 ⇒ 类型列
     显示"变频器"）。
  ② **进了报警表的故障级告警一律：锁存 + 自动停设备 + 需手动复位**：
     · 锁存：复用既有的**报警锁存**（Python `AutoController._fault_latched`
       / C `HsfServerAutoMode` 的 `s_faultLatched`）——故障自己消失了**不
       解锁**，必须人工复位；
     · 自动停设备：与**现场停止按钮同款**（VFD 减速停 + 进水泵关 + 阀按模式
       规则复位）；
     · 手动复位：自动模式的**复位按钮**（op=4）或设备侧的**「故障复位」**
       （两者都是人按的）⇒ 服务端锁存一并解除（**只清报警锁存，不清急停
       锁存**——急停只能现场物理复位）。
  ③ **手动模式的远程控制同样被闸**（"必须手动复位才可以启动设备"）：启动/
     点动/给频/给转（VFD）、开阀、启泵 ⇒ 锁存未解时一律拒，文案
     「有报警未复位：禁止启动设备（请先按复位）」；停止类与故障复位类**放行**
     （否则人连停都停不了、解不开锁存 ⇒ 死锁）。
     **数字量页的原始 DO 写也是"启动设备"的途径** ⇒ 把**阀/泵的 DO 位置
     1** 的写同样被闸（清 0 放行；按钮灯 DO7..9 不受影响）。
  ④ **工况级告警不锁存**（流量计的上限/下限/空管/系统）：它们是**工况信息**
     （泵停/无流时必然置位；现场那块表的空管位更是**长期挂着**——有流量
     3.6 m³/h 时也报空管）⇒ 锁存会让线永远起不来。流量计侧的停机由
     **运行保护**的时限负责（用户可配）。保护**动作**本身进表 ⇒ 锁存 ✓。
  ⑤ 实现落点：Python `autocontrol.py`（`raise_fault/clear_fault/fault_latched`
     + `app.py` 的 `_watch_vfd_fault` / `_fault_gate_text` /
     `_device_do_channels`）；C `HsfServerAutoMode_raiseFault/clearFault` +
     `HsfServerApp.c`（故障边沿 + 各控制 handler 的闸）。
  ⑥ 锁：服务端 `tests/test_protect.py` **D 段 11 项**；C
     `hsf_regression_test.c` **§S18**（9 项：锁存 + 告警类型 + 自动停机 +
     启动被拒 + 人工复位解锁）。
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
- **Python 端数字量页「文字显示不全」三根因 + 尺寸铁律（2026-11-11
  用户「还是有文字显示不全」「能自动布局吗，不要写死，毕竟要适配不同的
  尺寸」）**：① **DO 按钮高度写死 46px**（QSS 还钉了 `max-height:46px`）
  ⇒ 界面缩放 1.5× 时字号 18px、两行文字行高 42px，加 padding/border 需
  52px，内容区只剩 39px ⇒ **上下各裁一点**；现在行高由
  `ToggleGrid.row_height()` 用 `QFontMetrics` 实测（`theme.DO_BTN_PAD_V`
  与 QSS padding 同源）。② **灯阵每格宽写死 108px**（DO 侧一直是实测，
  灯阵漏了）⇒ "电动球阀1 关到位" 被折成两行；现在
  `LampRow._min_cell_w()` 按**实际下发的语义名**实测，名字到达即重排列数。
  ③ **卡片/网格吞掉多余高度** ⇒ 卡里留白带（用户截图：DI 卡下方、DO 标题
  下方各一条）。**尺寸铁律：高度一律交给 Qt 布局** —— 网格
  `setSizePolicy(Preferred, Fixed)`（竖向不伸缩，高度=布局 sizeHint=内容
  自然高度）、卡片 `setSizePolicy(Preferred, Maximum)`（富余高度由页面
  末尾 stretch 吸收）；**页面里不许出现「按行数算高度」的代码**（常数
  估高会留白；手算行数在折列那一帧算的是上一版 ⇒ 同样留白；用
  `card.sizeHint()` 也一样滞后一帧）。回归锁：`run_client` 段的
  「DO 行高实测 / DI 每格宽随名字增大 / 名字到达后列数重排」（6 项）。
  ④ **共用工具条时间框写死 `setMaximumWidth(120)` 是同一类坑（2026-11-11
  空库联调实锅）**：历史页入页把服务端最前一条完整时刻
  （`2026-11-11 15:04:48`）回填进「起始」框 ⇒ 1.5× 缩放下这串约 330px，
  被压成「1 15:04:48」看着像数据坏了。修法：`data_toolbar.py` 的
  `_fit_time_edit_width()` 用 `QFontMetrics` 按最宽候选文本实测给宽
  （+16 内缩），不写死像素。**只加宽单个控件又会把同一行的「列」「关键字」
  挤成省略号** —— 筛选项 7 组全塞一条 `QHBoxLayout` 在缩放下必挤；终修
  把筛选块拆成**两条独立水平布局**（时间窗一行、列+关键字一行），各自
  分配宽度、互不撑压。**通用教训：宽度按字体实测之后，还要看这一行
  总共放不放得下 —— 放不下就分行，别靠压别的控件。**
  ⑤ **「状态有/无文案时用 `setVisible` 改结构」是布局跳动的头号来源
  （2026-11-11 用户「电磁阀开关，卡片布局变化有点大，按钮都动了」）**：
  `ValveCard` 的提示行（动作中「点另一侧可反向」/故障原因）与页顶
  `gate_label` 横幅原来都按 `setVisible(bool(hint))` 显隐 ⇒ 状态在
  「动作中 ↔ 已开/已关」间切换（200ms 一拍）时，整行被从 VBox 拿掉/
  加回 ⇒ **卡片高度变、按钮上下跳、整张网格被横幅往下推**。修法一律
  **恒占位**：提示行按 `QFontMetrics.lineSpacing()` 给固定高、长文案
  `WordWrap=False` 省略号、无文案只 `setText("")`；横幅固定高、无文案
  透明占位（`_set_gate` 统一入口，有文案才上色）。**两条铁律**：
  · 内容可变但**几何不可变** ⇒ 别用显隐改布局，用"占位 + 清空内容"；
  · 插入新方法时别把后面 `__init__` 的构造代码缩进进方法体（本轮实锅：
  卡片网格被吞 ⇒ `page` 没有 `cards`、整页空白，回归锁只测单卡没测
  `len(page.cards)` 就没拦住 ⇒ **构造完整性也要有断言**）。
  回归锁：`test_hsf_regression.py` §21c「阀卡片布局恒定」V0~V8：四状态
  下**卡片高/宽恒定、两按钮 x/y 全恒定**（用户追问「开阀和关阀按钮的
  位置也固定了吗，不要动了，每次点一下就跑了」⇒ 不只锁 Y 上下跳、也锁
  X 左右跑；长故障文案 `sizeHint` 宽 720px 也不许撑宽卡片）、V6a 六张卡
  建出来。**界面进程必须重启才加载新代码**：改完 Python 源码后桌面上
  那个旧窗口跑的仍是旧字节码，看着"还在跳"其实是没重载（本轮实锅）。
- **历史页图表必须「页内双视图 + 全量取数」（2026-11-10 用户定案）**：
  ① **图表不弹独立窗**：`HistoryPage.set_view(0|1)` 用 `QStackedWidget`
  在页内切表格/图表，`LineChartDialog` 以 **Qt.Widget 旗标**内嵌；「生成
  折线图」按钮已换成「表格视图 / 图表视图」两个可勾选按钮。**不要再加
  弹窗式图表**。（报警页 2026-11-11 起已去掉图表/双视图，见下条。）
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
  **2026-11-13 判据三轮化（hsf_leak_gate.py 同步）**：0x0119 运行日志入库
  （2026-11-09）后，每轮压测 ≈2000 条 conn 日志进 SQLite 页缓存（计入
  XMemory 统计）⇒ 每轮固定 +~265KB，「两轮堆零增长」旧判据恒红（11-08
  「堆回落 ~702KB」当时还没有 logs 入库，该曲线已过时）。**新判据=每轮
  0 失败 + 轮间堆增量恒定（Δ23 ≤ Δ12；恒定增量=logs 固定成本，增量递增
  才是按轮泄漏）**。压测脚本连接阶段已加**立即重试一次**（本机环回偶发
  握手丢包的环境噪声剥离；失败连接留僵尸会话 ≈3.6KB/个，也会把堆判据
  一起打爆）。**压测/泄漏门必须独占跑**——与编译/部署并发时服务端事件
  循环饿死，「连接超时」假红三次实锤；压测前先 `netstat -ano | grep 6100`
  看**谁在听**（协议面对照等留下的 Python sim 会残留监听 6100）。
  环境要求：脚本会**自动**经远端配置（`CMD_CONFIG_GET/SET` 的 `login` 节）
  打开同账并发，收尾按原值恢复（`--no-policy` 可跳过）；手工压测时也可在
  「远端设置 → 登录策略（账号）」里改（管理员），或写
  `bin/cfg/hsf_login_policy.json`（服务端落盘的策略文件）。
  跑完须回缺省，否则 `test_client.py` 的权限/策略用例口径不对。
  泄漏判读用堆计数（`HSF_HEAP_TRACE=1` 服务端 / `XHSF_HEAP_TRACE=1`
  客户端，5s 一条 `[heap]`），**绝不看工作集**。台账：
  `docs/HSF会话压测与泄漏回归_实施记录_20261108.md`。
- **Python 侧堆级泄漏门（2026-11-14 新增，两把钥匙）**：C 端有
  `verify/hsf_leak_gate.py`（堆计数 `XMemory_statistics_2`），**Python 两端
  此前只有 RSS「增长有界」**——RSS 受页回收影响**判不出 KB 级泄漏**
  （AGENTS 铁律：泄漏结论必须用堆计数）⇒ 补两把：
  · `server_py/tests/test_leak_gate.py`（**服务端**，已注册进
    `server_py/tests/run_all.py`）：5 场景 × 3 轮 —— A 会话生命周期
    （连接→登录→订阅 9 主题→快照→断开）/ B 自动模式生命周期（start/stop
    + 急停/复位）/ C 查询与导出大流量 / D 落盘容量维护（**后台维护线程**
    ⇒ 验线程/连接回收）/ E 告警事件流（阀超时→报警入库+推送）。
  · `client_py/tests/test_leak_gate.py`（**客户端 + 内嵌网页端**，已注册
    进 `client_py/tests/run_all.py` 与 `verify/run_regression.bat`）：F 会话
    生命周期 / H 流式 XLSX 导出（13000 行 ×3）/ I 内嵌网页端起停（验线程
    回收 + **端口释放**）。
  **判据口径（与 C 端不同，勿混）**：`tracemalloc`（Python 分配器精确计数，
  **不含 SQLite 的 C 侧页缓存** ⇒ 不会把「落盘库变大」误判成泄漏）+
  **分文件/分类型归因**（`server_py/hsfsrv/**` 与 `client_py/hsfsrv_client/**
  + `webui_py/hsfweb/**` 各归各的）。每场景 **3 轮**（第 1 轮吸收一次性
  初始化），**只判最后一个稳态轮的每轮增量**：
  · **为什么用「稳态轮绝对增量」而不是「轮间差」**：真泄漏是**每轮恒定**的
    （L 字节/轮）⇒ Δ23−Δ12 = 0 ⇒ 用差值判据**抓不到**；只能拿稳态轮的
    绝对增量对小容差（实测服务端堆噪声 ≤17KB/轮 ⇒ 容差 24KB；客户端
    ≤1.2KB/轮 ⇒ 容差 24KB 偏松，实测 +0 B/轮）。
  · **为什么只判最后一轮**：第 1→2 轮含一次性成本（asyncio 默认线程池
    **懒建 8~10 条**线程、SQLite 语句缓存）⇒ 判它必假红。
  · **线程判据要排除线程池**：`asyncio.to_thread`（慢查询挪出事件循环）用
    loop 的默认 `ThreadPoolExecutor` ⇒ 它的线程懒建、**不是泄漏** ⇒ 只数
    **我们自己**的线程（后台维护线程之类）。
    **[线程名有两个前缀都要排]（2026-11-14 实踩）**：Python 3.12 里 asyncio
    自建的默认 executor 线程叫 **`asyncio_N`**（**不是**
    `ThreadPoolExecutor-N_M`——那是普通 ThreadPoolExecutor）⇒ 只排后者时，
    慢查询场景每轮 +4~16 条 ⇒ **假红**。两个前缀都排：
    `("asyncio_", "ThreadPoolExecutor-")`。
  · **对象数按类判**（`gc.get_objects()` 里 `hsfsrv.*` / `hsfsrv_client.*`
    的类名计数）⇒ 直指「每会话多留一个 Session」这种真泄漏形状，且不受
    瞬时对象噪声影响（快照前 `gc.collect()` 两次）。
  · **A/B 实证（不会红的门等于没门）**：服务端门内置
    `HSF_LEAK_INJECT=1` 自检开关 ⇒ 故意让服务端每建一个会话就多留一个
    Session 对象 ⇒ 实测「Session +40/轮」与「服务端堆 +98KB/轮」双双判红；
    不开时 33/33 全绿。
  · **会话归零判据**：搅动完服务端 `session_count()` 必须回 0（无噪声的
    硬指标）。
  **四个写这类测试的坑（都踩过）**：① **搅动必须跑工作线程**——客户端是
  阻塞式 socket 读写，跑在事件循环线程里会把服务端**饿死**（实测：登录
  全超时、连接积压把 accept 背压顶爆 ⇒ 报「连接被拒」，看着像服务端崩了，
  其实是自己的测试把服务端饿死了）；② **sqlite 连接线程绑定**——store 的
  **写连接**默认 `check_same_thread=True` ⇒ 跨线程写会抛
  ProgrammingError ⇒ 用 `loop.call_soon_threadsafe` 投递回事件循环线程
  （读连接是 `check_same_thread=False`，后台维护线程用的就是它）；
  ③ **假设备不喂就没有阀反馈**——`FakeDigital.poll` 只刷 `last_ok_ms`
  **不改 DI** ⇒ 阀永远不到位 ⇒ 自动序列秒超时 ⇒ 量到的形状是「一直报警」
  而不是「正常自动模式」⇒ 必须补 feeder（照抄 `test_auto_modes` 的
  `_feed_while`）并**模拟阀行程**（写 DO ⇒ 到位反馈跟着变，留一台阀永不到
  位用于制造超时报警）；④ **HMAC 迭代式**——客户端必须与服务端
  `auth.derive_hash` **逐字节同式**（key 恒为口令：`H_{i+1} = HMAC(口令,
  H_i)`）⇒ 写成 key=摘要 ⇒ 服务端回「bad password」。
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
  （HsfDataLog）与 Python 客户端日志页都消费它。**两端都已实现**
  （server_py `_h_log_page`，应答含 count 补契约；2026-11-13 实测两端
  应答键集逐键一致——旧注「server_py 不实现」已过时作废）。改日志事件
  点/类别枚举必须同步 `XhsfLinkProto.h`、server_py/client_py 两个
  proto.py 三处注释。
- **双端协议面对照回归锁（2026-11-13）**：`server_py/tests/test_server_alignment.py`
  （已注册 run_all.py）——自管两端 sim（探活 6100/6200 复用、不在跑自己起、收尾只清自己起的），
  对照 19 命令/10 主题/配置四节的**键集·值类型·错误文案**（两端错误文案逐字相等，含权限拒绝/自动闸/未知配置节）。
  改任一端快照键/推送键/错误文案/闸序**必须跑它**。「条件键」白名单（valid 掩码门控的键）写死在测试里，两端 handler 键表变了要同步。
- **虚拟总线事务锁泄漏根修（2026-11-13 实锅）**：`virtualdev.VirtualMultiMaster`
  的 `_begin_tx/_end_tx` 拆法有取消窗口——`restart_polling`（远程改采集配置必经）cancel 轮询任务时若正落在
  `_begin_tx` **取得锁之后的间隙 sleep**（路 B 保真 200ms）上，CancelledError 越过调用方 try/finally ⇒ 锁永不释放 ⇒ 该总线所有事务永久排队
  （现象：sim 下改一次采集配置，流量计/浊度仪全部「数据过期」、0x0106 永远无应答；路 A 无事务间隙才幸免）。已改 `asynccontextmanager _transaction()`（acquire 与 release 同一 finally；
  取消落在 acquire 自身不释放）。**凡「acquire 与 release 分居两处」的异步资源都是这个雷**。
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
- **主页自动模式阶段行：进度条删除（2026-11-19 用户口径「那个进度条删掉」）**：
  阶段行 = **阶段名 + 已用时长文字**（**不再有进度条**）——Python 客户端
  `pages_other.HomePage`（删 `_phase_bar`）与网页端 `page00_home.js`（删
  `auto-phase-bar`）同批删；**C 端主页本来就没有进度条**
  （`HsfHomePage.c` 无 ProgressBar）⇒ 只两端口径。
  · 文案四分支：**有总长** ⇒「已用 X / 共 Y」；**无总长**（单模式 / 该阶段
    无定时）⇒ 只报「已用 X」（**不编造总长**）；**无数据** ⇒「--」；
    `paused` 且无定时 ⇒「按启动继续」（状态行已有「已暂停 · 按启动继续」，
    此处不重复表达）。
  · **口径演进**：ce632fe2 先做「进度条按需显隐」（暂停且无定时数据时藏空条）
    ⇒ 用户仍嫌多 ⇒ 本轮**彻底删除**。**最终口径 = 无进度条**，别把显隐
    逻辑改回来。
  · 删进度条**不引起行高跳变**（该行高度由文字撑）⇒ 与「内容可变但几何不可变」
    铁律一致。
- **变频器控制下拉随闸置灰（2026-11-19 用户「自动模式下下拉还能操作」实锅）**：
  控制区**每个可操作控件都必须跟同一个 gate 走**；漏一个下拉 ⇒ 界面看着能
  操作、点了才被服务端拒（还蹦一条拒绝文案，现场像"界面坏了"）。
  · **方向下拉**：改选**真的发换向命令**（Python `_on_dir_changed` / C 端
    `m_dirCombo` / 网页端 `dirSelect`）⇒ 自动模式或无权限时**必须置灰**。
  · **调速方式下拉**：只切**本地 Hz/rpm 单位**（不发命令），但给定框在闸下
    已禁 ⇒ 单位切换无意义 ⇒ 一并置灰（Python `speed_mode` / C 端
    `m_speedMode` / 网页端 `speedModeSel`）。
  · 回归锁：网页端 `webui_check.py`「搅拌泵页控制闸：方向/调速方式下拉随闸
    置灰」（静态锁，断言两个 `disabled = !canCtrl;`）。
- **浊度仪改名（2026-11-19 用户口径，三端显示名）**：浊度仪1 ⇒ **进水浊度仪**、
  浊度仪2 ⇒ **出水浊度仪**（显示名 / 健康条目 / 采集配置标签 / 文档 109 处）。
  · **协议键一个都不动**（`turbidity1Enabled` / `turbidity2Enabled` /
    `turbidity1PollMs` …）⇒ 存量配置与老服务端照旧；**只有显示层改名**。
  · 设备缺省名格式化命名：`浊度仪%d` → 按台号映射进水 / 出水。
  · 测试过滤改按**新名**匹配（`test_units.py` 两处 / `test_turbidity_per_meter.py`）。
- **触屏目标放大（2026-11-19，变频器控制行）**：按钮 / 下拉 `setFixedHeight(44)`。
  · **QSS 的 sizeHint 会压过 `setMinimumHeight`** ⇒ 实测后改**硬约束**
    `setFixedHeight`（与「尺寸必须交布局」不冲突：这是**触摸目标下限**，
    不是布局分配）。
  · 宽度同口径：方向130 / 启停130 / 停车复位120 / 点动140 / 调速方式150 / 给定190。
  · `theme.py` 全局 `QPushButton` / `QComboBox` min-height 18→26px（随
    `font_px` 缩放 ⇒ 板端 1.5× ≈53px 触摸目标），全应用按钮 / 下拉受益。
- **主页重排 + 最近报警卡 + 泵输出功率/转矩（2026-11-19）**：
  · 主页重排：浊度仪卡 ↔ 电箱温度卡**互换位置**、三排卡高均匀、电箱温度
    字号降档。
  · 最近报警卡：服务端快照 **`lastAlarm`** 键（Python `proto.KEY_LAST_ALARM`，
    C 端 `HsfServerStore_lastAlarm`，三端同名），点卡跳报警页；C 端原先没有
    这张卡，本轮补源码。
  · 搅拌泵卡与泵详情补**输出功率 / 转矩**（VFD 快照键 `outputPowerKw` /
    `torquePercent`，三端同口径）。
  · 自动模式卡阶段行 = 阶段名 / 进度 / 剩余，手动模式隐藏 ⇒ 其中「进度条」
    部分已被上一条「进度条删除」取代。

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

### 3. sizeHint 槽内写尺寸 ⇒ 布局自激活锁 ⇒ **界面冻结像「数据全挂」**（2026-11-11 实锅）

- **现象**：桌面模拟板 C 客户端，主界面「电动球阀·6 路」「按钮组系统状态」
  恒 `--`，随后**其余数据也全部停更**。看起来像服务端挂了。
- **真相（分层实测，逐步排除）**：
  - 服务端**完全正常**：history 每 60s 准时入库且 `turb2/flow/water_temp`
    逐行在变；轮询成功计数持续上涨；8 个主题订阅全成功。
  - **推送也正常**：另开一个协议客户端连同一端口，3 秒收 **102 帧**，
    7 个主题全覆盖（数字量 27 帧、电磁阀 26 帧），BSON 内容正确。
  - 客户端进程**单线程 100% 空转**（3 秒烧 4.2 秒 UserModeTime、
    KernelModeTime 零增长）⇒ **事件循环没空取 socket** ⇒ 帧到了网卡
    没人取 ⇒ 界面冻结在最后一帧。**「数据不刷新」≠「数据没有」。**
- **定位手法（照抄可复用）**：
  1. `cdb -p <pid> -c "~0k; qd"` 抓栈（pdb 同目录 ⇒ 符号直出）——
     栈停在 `XLayout_activate → collectGeom → sizeHint → …`；
  2. 框架自带 **`XGPU_W_FRAME_PROF=1`** 布局画像：`act/skip/inval` 三
     计数（`act`=真重解算、`skip`=幂等早退命中、`inval`=失效）。
     **判据：`skip=0` ⇒ 每次都是被 invalidate 强制重算 ⇒ 有人在疯狂
     失效父布局**（板端 CPU 100% 那次同款判据）；
  3. 在 `XWidget_updateGeometry` 入口打**临时探针**（env 门控）打印控件
     指针 + **虚表地址**，再用 cdb `u poi(vtbl)` 反查虚表首项符号 ⇒
     一次定位到控件类型（本次是 **XComboBox**）。
- **根因（两层，同一类）**：
  - **框架层**：`XComboBox::xcombo_updateSizeHint` 把「重算提示」与
    「通知父布局」**焊死**（末尾无条件 `XWidget_updateGeometry`），
    且 `XWidget_setMinimumSizeHint` 值不等就 `updateGeometry` ⇒ 一次
    刷新**两次**失效父布局。提示值来自 `XStyle_sizeFromContents`
    （样式表参与，可能随几何变 1~2px）⇒ 同值早退永不命中 ⇒
    **算 → 通知 → 父布局重排 → 再算 → 再通知**的跨事件自维持循环。
  - **框架层**：`XDateTimeEdit::xdatetimeedit_updateSizeHint` 在
    **sizeHint 虚槽内**调会通知父布局的 `XWidget_setMinimumSizeHint`
    ⇒ 同款闭环（另见下面 Qt 原文对照）。
- **修法（对齐 Qt 6.8 原文）**：
  - Qt `QComboBox::recomputeSizeHint()` 是**纯计算**，不调
    `updateGeometry()`；通知由**内容变化处理器**单独发
    （`rowsInserted/dataChanged/setSizeAdjustPolicy/`… →
    `adjustComboBoxSize(); updateGeometry();`）。框架照此拆开：
    **提示真变了才通知父布局**，并把 `setMinimumSizeHint` 换成
    **静默写**（`XWidget_setMinimumSizeHintQuiet`）。
  - Qt `QDateTimeEdit::sizeHint()` 是 const，只写内部缓存
    `d->cachedSizeHint`/`d->cachedMinimumSizeHint`，**绝不**调
    `updateGeometry()`。框架照此：`xdatetimeedit_updateSizeHint`
    改静默写，通知挪到 `xdt_refreshText`（内容真变化时）按
    「提示真变了才通知」下发。
  - `XLayout_activate` 的 `SetMinimumSize/SetFixedSize/…` 约束回写
    改用 `XWidget_setMinimumSizeQuiet/setMaximumSizeQuiet`：**布局解算
    过程中的约束回写不得 invalidate 父布局**（Qt 6.8
    `QWidget::setMinimumSize→updateGeometry()` 只给宿主发
    LayoutRequest，**不** invalidate 父布局；父级通知由 activate
    尾部「合计提示真变了才通知父级」统一负责）。
- **实测效果（同一页面同一二进制）**：`act` 1900~2200/3s → **3/3s**；
  `skip` **0 → 3**（幂等早退恢复）；`inval` 10万~14万/3s → **4~6/3s**；
  `solveAvg` 1.5~1.9ms → **0.05ms**；客户端 CPU 185% → **6%**；
  帧耗时 26.69ms → **2.19ms**；界面阀卡/按钮组从 `--` 恢复真值。
- **通用教训（务必记住）**：
  - **`sizeHint`/`minimumSizeHint` 是查询语义，不得在其中做会通知父
    布局的写操作**；要写就「静默写」，通知放到**内容真变化**处。
  - **「界面数据不刷新」先查客户端 CPU 与事件循环**（进程活着 ≠ 事件
    循环在跑）；`skip=0` 是自激活锁的**决定性指纹**。
  - **服务端在推 ≠ 客户端在收**；用「另开一个协议客户端数帧」把两者
    一次分开（本项目手段：`client_py` 的 `XLinkClient.on_publish`）。
  - 临时诊断探针用完**必须零残留**（AGENTS.md 既有纪律），env 门控
    且带注释标明删除时机。

## 界面铁律：不许留"选了不兑现"的选项，也不许留占位控件

**用户口径（2026-11-10）**：「检查下界面功能是否都是实现的，别是占位」
「没有 XLSX writer 就只留 CSV 并删下拉（别留一个选了不兑现的选项）」。

- **表格列宽：下限要按「内容」算，且 16 列要能一屏装下（2026-11-14 用户
  截图「表格列显示不全」）**：
  - **根因一（列内容被截断）**：列宽下限原先**只按表头文字**算 ⇒ 历史页
    「模式」列表头只有 2 个字（"模式"），内容却是 "自动·产水"（5 个字）
    ⇒ 列被压到下限 ⇒ **整列显示 "自动…"**（用户圈的就是这一列）。
    ⇒ 修法：下限取 max(表头宽, **该列最长单元格内容宽**) + 内边距；
    内容宽**按当前实际字体现量**（不按比例换算——字体 hinting 让像素宽
    **不是严格线性**，换算会差几像素 ⇒ 仍然截断）。
  - **根因二（右半截的列看不到）**：16 列的下限总和远大于视口 ⇒ 必然
    横向滚动 ⇒ 水温/流量/压差/温湿度/DI/DO 要滚到**表格最底部的滚动条**
    才够得到 ⇒ 用户看到的就是"列显示不全"。
    ⇒ 修法：表内字号**按视口自适应**（`DataTable.set_fit_columns(True)`，
    **按表显式开启**——首页设备表 5 列/日志页 3 列本来装得下，不该被动
    变小）。缩放比用**二分求解**「所有列都装得下的最大比例」：
    下限(scale) = 文字宽×scale + 内边距 + 全局最小列宽×scale ⇒ 单调 ⇒
    二分即得。**别用「当前总宽反推比例」** ⇒ 缩完总宽变小 ⇒ 反推出的
    比例变大 ⇒ 又缩回去，**振荡**（实测）。
  - **全局最小列宽（72/150px）是死像素 ⇒ 必须跟着字号缩**：否则字号缩到
    0.6 倍时 16×72=1152px **比全部文字还宽** ⇒ 成了绑定约束 ⇒ 表缩不动
    ⇒ 永远装不下。
  - **表头与单元格必须同一字号**：表头是 QTableView 的子控件，**app 级
    QSS 一重刷（换主题）就会把它还原成界面字号** ⇒ 实测「表头 15pt /
    单元格 11pt」⇒ 表头比内容还大、且列宽按大字号量 ⇒ **反而把内容挤到
    截断**。⇒ 表内字号用**控件级 QSS**（`QTableWidget` + `QHeaderView::section`
    同一条 font-size，控件级优先于 app 级，换主题冲不掉）+ 宽度计算一律
    用**目标字体**现量（不看控件当前实际 font）。
  - **内容下限不是无条件的硬下限**：内容很长时（首页设备表地址列
    "COM17 · 115200 · 站 2"）内容下限总和会超视口 ⇒ 全表被迫横向滚动、
    **权重也失效**（实测设备列与状态列都被压成 150px，回归红）。
    ⇒ 改成**逐列按需抬高**：先给表头口径的下限，再把**能装下**的列逐个
    抬到内容口径（便宜的先抬）⇒ 模式列这种"表头短内容长"的修好，
    首页地址列保持旧口径（权重照旧生效、不出滚动条）。
  - 回归锁：客户端 `test_hsf_regression.py` §8 尾（4 检：没有任何一列被
    省略号截断 / 1680px 视口下 16 列一次装下 / 缩放比不低于可读下限 /
    表头与单元格字号一致）—— **验的是真代码的 `_plan_widths()`**
    （测试窗口里表格宽度由布局决定、撑不宽 ⇒ 直接调规划器验算）。
- **数值块：数值与单位必须同行同基线同字号（2026-11-14 用户定案「这些数字有
  点小，而且跟单位还不在同一行同一大小」）**：
  - `StatBlock`（客户端 + 网页端共用口径）把「数值 + 单位」合成**一段富文本**
    （**一个** QLabel / **一个** span 行）⇒ 同行、同基线由**排版本身**保证 ⇒
    两个独立标签即使都 AlignBottom，字号不同基线仍会错位（截图里 25.00 与
    Hz 一个下沉一个上浮）。
  - **单位字号 = 数值字号**（原来单位写死 22px ⇒ 比 16~18px 的数值还大 ⇒
    数值显得更小）⇒ 网页端 `.stat-unit` 的字号由 JS 跟着数值给。
  - **字号按页放大**：变频器页主读数 22→28、次级量 16→24、流量计 22→24、
    首页浮窗小卡 22→24（首页大号读数本来就 26~40 ⇒ 不动）。
  - **QLabel 不自带 elide** ⇒ 长文案用 `page_base.elide_text(widget, text)`
    （按当前宽度省略，全文挂 tooltip）。
  - **富文本会改 `QLabel.text()` 的语义**（返回 HTML）⇒ 用 `RichLabel`
    （`widgets.py`）覆写 `text()` 返回**纯文本** ⇒ 既有断言与调用点零改动；
    颜色另在 QSS 里保留一份（`styleSheet()` 仍能读到"当前是红还是黑"）。
  - **单位那一段要显式写 `font-weight:normal`**：数值标签的 QSS 写着
    `font-weight:bold` ⇒ 富文本的**默认字体**是粗体 ⇒ 不钉住的话单位会
    继承粗体（数值更粗 ⇒ 单位看着更小 ⇒ 又变成"不同大小"）。
- **多主题：颜色必须单源，且不许在页面里写死（2026-11-14 用户定案「本地
  设置增加主题功能，多适配几款好看的主题，每一款都要检测每个界面显示是否
  正常」）**：
  - 5 套主题在 `client_py/hsfsrv_client/theme.py` 的 `THEMES`：亮色·工业白 /
    深色·控制室 / 亮色·ISA 仪表 / 护眼绿 / 高对比。每套 = **令牌**（QSS 用
    `%(key)s`）+ **调色板**（QPalette，给没写 QSS 的控件）+ **表盘色**（自绘面）
    + **chrome 色**（顶栏/底栏/导航）+ 深浅标记。
  - **QSS 里不许写死颜色**：新控件一律用令牌；页面里**逐控件**写的
    `QLabel { color: #1A1A1A; }` 这类**必须改成调色板角色引用**
    （`palette(text)` / `palette(mid)` / `palette(link)`）⇒ 换主题时**调色板
    一变就自动跟随**，不必逐控件重刷（实测深色主题下写死 `#1A1A1A` 的行标签
    深灰字压深底几乎看不见 ⇒ 这就是"每款主题每页都要检测"抓出来的）。
  - **主题切换即时生效 + 本机持久化**：`theme.apply_theme(app, tid, persist=True)`
    （写 `settings.K_THEME` ⇒ 客户端 INI ⇒ 重启仍是这一套）。chrome 部件
    （顶栏/底栏/导航/卡片）各自持一份 QSS ⇒ 换主题后要 `theme.refresh_widgets(app)`
    按 objectName 重下发。
  - **每款主题 × 每个页面都要自动检测**：`client_py/tests/test_theme_matrix.py`
    逐主题逐页查三件事：① **对比度**（可见文字 vs 背景 ≥ 3:1，取控件自己的
    调色板色＝实际渲染色；背景取**多个候选**再比最好的，否则按钮白字在绿底
    上会误报成 1.0:1）；② **裁字**（控件宽 ≥ minimumSizeHint ⇒ 窄了就是文字
    被裁）；③ **白板**（渲染出来是纯色 ⇒ 根本没画）。实测这一套**当场抓出 37
    处**问题（深色主题的行标签看不清 + 阀卡长提示被裁 + 「未配置」徽标浅灰底
    白字只有 2.7:1）⇒ 修完才 0 处。
  - **长文案要省略号**：QLabel 不自带 elide ⇒ 用 `page_base.elide_text(widget, text)`
    （按当前宽度省略，全文挂 tooltip）；「点位未配置（服务端 config 的
    valves.items）」这类 640px 宽的提示在 265px 的卡里会被**直接裁掉**。
- **触摸手势：长窗口要能用手指上下滑（2026-11-14 用户口径）**：
  - `QScrollArea` **原生支持触摸拖动**（`QAbstractScrollArea` 的 viewportEvent
    处理 Touch*）⇒ "拖得动"本来就有；缺的是**惯性**（松手即停）。
  - **PyQt5 没有 QScroller**：`QScroller.create()` 未导出，`QScroller(...)` 直接
    报 "cannot be instantiated or sub-classed" ⇒ 只能自己实现
    （`client_py/hsfsrv_client/flick.py`：按下记起点 → 移动按位移反向滚 →
    松手按最近速度做**衰减**滚动）。
  - **先不抢事件**：位移小于 6px 判为点击（按钮/下拉照常响应）⇒ 超过阈值才
    接管并吃掉移动事件。
  - **自检用显式 `QMouseEvent`（带 buttons=LeftButton）直接投递给视口**，
    **不要用 `QTest.mouseMove`**——它合成的事件不带按键状态 ⇒ 控件自己会忽略
    ⇒ 事件到不了"按住拖动"这条路径（实测只到 1 次 move）。
- **持久化口径（2026-11-14 用户核对）**：**远端设置**（自动模式配置 / 采集 /
  落盘 / 登录策略）存在**服务器端**（`cfg/hsf_server_py.json`，服务端
  `config.py` 的 save/load）⇒ 重启后还在（回归锁
  `server_py/tests/test_config_persist.py`，10 检）；**本机设置**（界面缩放 /
  主题 / 窗口尺寸 / 每页条数 …）存在**客户端**（`client_py/hsfsrv_client/
  settings.py` ⇒ 程序目录的 INI）⇒ 与服务端无关（回归锁：客户端总回归 §19
  A5a~A5j + A5b2/A5d2 主题两项）。

- **下拉/选项必须真兑现**：要么接真实现，要么**把选项删掉**。
  半成品选项比没有选项更糟——用户会以为功能坏了。
  XLSX 接的是框架自带 **`XExcel`**（`Src/XCore/XData/XExcel`），**不需要**
  Python 的 ctypes 桥（`xlsx_bridge.c`）；CSV 侧要求**字节级兼容旧实现**
  （UTF-8 BOM + CRLF + 最小引号转义）。
  共享模块落在 `HighSpeedFilter/client/ui/HsfExport.{c,h}`，
  历史/日志/报警三页共用。
  **「导出格式」下拉已删（2026-11-11 用户定案）**：格式只在导出文件
  对话框里选——走框架 `XFileDialog_getSaveFileName_2` 的「文件类型(T)」
  过滤器下拉（`HSF_EXPORT_FILTER`，selectedFilterIndex 回填下标，手输
  已知扩展名优先），路径与格式在**导出开始时**定格，避免导出中途改
  选项产生半份 CSV 半份 XLSX。别再往工具条上加格式选择控件。
- **导出 = 筛选命中的全部数据，不是当前页那几十行（2026-11-11 用户定案）**：
  历史/日志页按「探测批拿 total → `HsfExportPlan_planBatches`（纯逻辑
  助手，`client/ui/pages/HsfExportPlan.{c,h}`，回归锁
  `hsf_regression_test.c` §Q）→ 逐批**连续**取（批行数 41..49 轮转，
  禁用 50=页大小同键会错认领）→ 收尾一次落盘」的状态机走全量，不抽样；
  截断必须显式进状态行。0x0118 导出批必须带 `export=true`（服务端验
  Download 权限），且整轮取数钉同一 `beforeTs` 锚点（`historyQueryWindow`
  的形参，<=0 回退"现在"）——每批各取"当下"会让窗口上界逐批增长，
  逐批顺序取必重复/漏行。0x0119 无 export 键，权限闸在客户端。
  端到端复检：`verify/hsf_export_filter_check.py`（起 sim 服务端 +
  Python 客户端分页断言：导出行数==命中总数、关键字筛选后变少、
  跨页不重不漏、无权限拿不到数据，退出码表成败）。
- **Python 端导出也必须"按筛选全量"（2026-11-11 用户「数据菜单下的三个
  图表导出的数据是按筛选结果而不是当前表格显示的数据，因为数据是分页的
  顯示的不是全的」）**：C 端早就是全量导出，**Python 端此前只导当前页**
  ⇒ 导出的文件比用户以为的少一个数量级且**看不出来少了**。现已补齐
  （`client_py/hsfsrv_client/hsf_export_plan.py` 纯逻辑助手：
  `plan_batches` + `BatchExport` 状态机 + `CsvStreamWriter`/`write_xlsx`
  落盘；回归锁 `test_hsf_regression.py` §21「导出按筛选全量」22 项）：
  ① **批行数 40**（与页大小 10/20/50、图表批 50 两两不相交 ⇒ 在途应答
  不会认错来源）；② **逐批连续取**（步长=批行数，跨页不重不漏，不抽样）；
  ③ **整轮钉同一 `beforeTs` 锚点**（每批各取"当下"会让窗口上界逐批增长
  ⇒ 顺序取必重复/漏行）；④ 结束判据=短批/空批/写满服务端 total；
  ⑤ **CSV 边取边写、不限行数**（库里可能十几万行，全攒内存既撑内存又
  会给 CSV 也套上限）；**XLSX 才有限**（工作簿在收尾前都在内存 ⇒
  12000 行上限，超出**显式截断**并进状态行，不许静默丢行）；
  ⑥ 每批带 `export=true`（0x0118 服务端验 Download 权限；0x0119 无
  export 键 ⇒ 权限闸在客户端）；⑦ 转换异常的行按**原始值兜底**导出并
  计数上报（行数必须与命中数对上，静默丢行最误事）。
  实测：库 15.3 万行，导出文件 153657 行 == 命中数（导出期间新入库的
  2 行不计入——锚点已钉住）。
  **报警页按「筛选命中全集」导出**：它 2026-11-11 起也做了**客户端内部
  分页**（见下条），但 `_visible_records()` 仍返回筛选命中全集、导出与
  统计都走它 ⇒ 不受当前页那几十行影响（回归锁 §16 I 段有"导出取 25 行
  全集而非当前页 10 行"断言）。
- **Python 端数据三页工具条统一为 `DataPageToolbar`（2026-11-11 用户定案）**：
  「这些功能日志和报警都要实现，可以抽象出来作为一个单独控件，给三个使用」
  +「其他两个界面也按照历史数据的设计来」。此前三页工具条**各写一份**
  （历史三行全套 / 日志缺列多选 / 报警缺关键字·分页·滚动锁定且列是单选、
  还带一张用户不要的「报警次数/分钟」图）。现统一到
  `client_py/hsfsrv_client/data_toolbar.py`：① 搬入 `TimePickerEdit` /
  `MultiCheckCombo`（通用输入控件，页面模块 re-export 保旧导入路径），
  新增 `DataPageToolbar`（三行：筛选行 / 分页行 / 视图行，**按能力开关
  裁剪**——没有的能力不长控件，遵守「不留占位/死控件」铁律）；
  ② 页面把控件属性**别名**成原有名字（`_f_start`/`_total_label`/…），
  既有页面逻辑与既有回归锁零改动；③ **三页都有列多选**（默认全勾、
  至少留一列）且**导出列跟随可见列**（界面看到哪些列就导哪些列）。
  **日志/报警都不要图表**（用户 AskUserQuestion 定案）：报警页原有的
  「报警次数/分钟」折线图与表格/图表双视图**整体删除**；**C 端已同步
  删除（2026-11-11 用户拍板：日志/报警页筛选对齐历史页——「除了图表
  相关的去掉，其他都要」）**，C 端原「报警次数/分钟」折线图与表格/
  图表双视图一并删除（`HsfDataAlarm.c`，判定/分页纯逻辑在
  `HsfAlarmLogic`，回归锁 `hsf_regression_test.c` 报警逻辑节），两端
  口径已一致，不再是待拍板项。
  **报警页**：加客户端内部分页（10/20/50，`_visible_records()` 切片，
  统计/导出按全集）、关键字筛选（设备/类型/状态文本包含）、滚动锁定
  （锁定期新报警只入库不刷表），**隐藏「刷新」**（健康推送没有"重查"
  动作，留着就是死按钮）。回归锁：`test_hsf_regression.py` §16 报警页
  重写（删图表/双视图两段、加关键字/分页/锁定三段）+ §21b「三页工具条
  统一」24 项；`_ui_groups()` 是组名单**一事实源**，全量编排遍历它
  （新增段自动进回归，别再手写清单漏一个）。
  **坑**：`pageSizeChanged` 信号发的是**下拉 index**（0/1/2）不是页大小值
  ⇒ 页大小槽必须无参、读 `currentData()`；写成带参槽会把页大小错设成
  1/2（本轮实锅）。
- **占位控件按页裁剪，不留着骗人**：`HsfDataBar` 新增按页裁剪 API
  （`HsfDataBarFeature_Chart/ColFilter/Decimals/MaxPoints` +
  `HsfDataBar_setFeatures`）——日志页隐藏「生成折线图」+ 3 个无效控件、
  报警页隐藏 2 个无效微调框（ExportFmt 位已随下拉删除）。
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

- **主界面「自动模式」卡加剩余时间表盘（2026-11-12 用户定案「要有表盘，
  文字也要」）**：
  ① **服务端算、客户端只显示**：运行模式快照新增两键
     `autoStageElapsedMs`（当前子阶段已运行毫秒；**-1**=不在运行/无阶段）与
     `autoStageTotalMs`（本阶段总时长；**0**=无定时，剩余不可知）。口径在
     `AutoController.stage_timing()`：反洗/顺洗 = 配置分钟数；**产水仅定时
     兜底开启时**才有总时长（默认按压差切换 ⇒ 剩余本来就不可知，绝不编造
     倒计时）。**只对组合模式（auto 循环）成立**；单模式启动没有换相定时
     ⇒ (-1, 0)，界面显示「剩余 --」+「当前模式无定时」。计时起点是
     `_cycle_stage_at`（**阀相**就开始算，换相归零）。
  ② **客户端**：`widgets.RemainDial`（270° 环 + **中央两行**：小字
     「剩余/已运行」+ 读数 mm:ss；**环按「剩余/总」画**：满环=刚起步、
     随剩余递减，像燃油表；「已过/总」填法在阶段初期几乎是一圈空环 +
     一小段弧，看着像加载转圈/没画完（模拟截图实见）。剩余 ≤60s 转橙、
     到点转红；无总时长时灰环显示「已运行」/「--」）。
     **尺寸与位置（2026-11-12 三/四/五改，用户最终口径）**：直径
     44→58→72→**88**（1.5x 下 ≈132px）。用户连提三次「大点」，最后一次
     直接点出「表盘上面还有空间，不能跨到『自动模式』那行吗」——故
     自动模式卡改成 **`Card()`（无内建标题）**，标题 `QLabel` 与模式行/
     状态行进**同一个三列网格**，表盘**跨占三行**（标题行也在内）：132px
     用的是原本空着的竖向空白，卡片反而比 108px 版略矮（149→146）。
     标题样式走 `widgets.CARD_TITLE_QSS`（与 `Card.title_label` 同一份）。
     **倒计时只在表盘中央一份**，旁边**什么文字都不放**
     （用户：「表盘中间已经有倒计时了，旁边就不用其他倒计时了」）；
     **表盘只在自动循环运行时显示**（用户：「表盘只在自动模式的时候
     显示」）——服务端只在组合模式下报阶段计时（`elapsed>=0`），单模式/
     未运行/老服务端都是 -1 ⇒ 隐藏，中列宽度塔缩，卡片回到原样子
     （不留空框）。位置：自动卡**三列网格的中列、跨两行**，夹在「模式
     下拉」与「启动键」之间（用户：「挪左边」）；跨行只吃行内空白，
     不把卡片顶高。“模式/总时长/为什么没倒计时”由状态行与下拉回答。
     **连带改动**：状态行缩到左半宽（~650px），`AUTO_PHASE_TEXT` 短语化
     （`阀动作中…` / `启动泵…`）——原带括号的长串会被表盘遮住。
     **时间只属于自动循环（用户口径 2026-11-12）**：远端设置里的分钟数全部
     是循环参数，**单模式没有时间限制** ⇒ 单模式必须显示「剩余 -- /
     当前模式无定时」（服务端 `stage_timing()` 对单模式返回 (-1, 0)），
     绝不能给单模式倒计时。**同时把下拉回显成「自动（流量循环）」**
     （循环阶段上升沿回显一次，之后不抢用户的新选择），状态行写
     「运行中 · 自动循环（反洗模式）」——否则会出现「下拉是产水模式、
     却显示倒计时」的假矛盾（用户实看截图提出）。
  ③ **三处键名同源**：`server_py/hsfsrv/proto.py`、
     `client_py/hsfsrv_client/proto.py`、`common/XhsfLinkProto.h`
     （`HSF_KEY_AUTO_STAGE_ELAPSED_MS` / `HSF_KEY_AUTO_STAGE_TOTAL_MS`）。
     **C 服务端/客户端尚未接线**（Python 单边先行）；老服务端缺键时客户端
     如实显示「--」，不拿 0 冒充。
  ④ **截图/离屏脚本必须先装主题**：`theme.install(app)` +
     `theme.apply_font(app, size=int(round(10 * theme.ui_scale())))`（与
     `client_py/hsfsrv_client/__main__.py` 同序）。漏掉这两步 ⇒ QSS/调色板/
     基准字号全不生效，截出来是 Qt 原生灰按钮——会被误判成「美化没加载」
     （2026-11-12 本轮模拟实锅，产品本身没问题）。
  ⑤ 回归锁：`server_py/tests/test_auto_cycle.py` 的
     `test_stage_timing_for_dial`；`server_py/tests/test_sim_e2e_auto.py` 的
     `test_sim_cycle_stage_timing`（真 socket：配置→循环启动→快照键→停止
     回 -1/0）；`client_py/tests/test_hsf_regression.py` §D 的 D13a~D13o
     （格式化/落模/缺键不编造/三种显示态/颜色/不裁字）。
     ⑤ 文案：表盘右侧的时长前缀由「自动运行」改为「**累计**」
     （2026-11-12 用户口径：那是多次运行累计值，不是本次跑了多久）。

- **产品名 =「互嵌深层高速过滤机」（2026-11-19 用户改名，取代 2026-11-12
  的「PMMA有机玻璃高速过滤器」）**：四处**必须同改**（改一处就是界面一半
  新名一半旧名）：
  · Python 端 **唯一事实源** `client_py/hsfsrv_client/theme.py` 的
    `APP_TITLE` —— 主窗标题栏居中文字（`MainWindow.goto_page` 不再跟页名
    走）、窗口标题（`setWindowTitle`）、登录窗标题与品牌字全引用它；
  · 网页端 `webui_py/hsfweb/static/js/app.js` 的 `APP_TITLE` +
    `static/js/login.js` 的登录卡标题 + `static/index.html` 的 `<title>`；
  · C 端 `HighSpeedFilter/client/ui/HsfUi.c`（`setWindowTitle` +
    `HsfTitleBar_setTitle` 两处）与 `HsfLogin.c`（绘制文字 + 窗口标题
    两处）—— **本轮已同步**（此前长期是旧名「高速过滤器」，AGENTS 一直
    挂着"尚未同步"的欠账）。
  **改名的两个配套检查**：① 登录窗最小宽是按**字号算的汉字数**留余量
  （`login.py` 的 `font_px(20) * 13 + 56`）⇒ 名字变长要复核这个系数，
  否则标题被裁；② 网页端静态资源带**内容指纹版本戳**（`api.py` 的
  `?v=<指纹>`）⇒ JS 一改指纹就变，浏览器自动取新文件，**不用手工刷
  版本号**。

- **主界面进水泵状态实时性（2026-11-12 用户实锅）**：现象「泵实际开着，
  卡片一直『断』，要切页再回来才正确」。本地 sim 实测（真客户端 + 真
  服务端）：开泵后 **0.5s 内**卡片就变「通」，无需切页；两栈服务端都按
  500ms 推 `TOPIC_PUMP_STATUS`。即便如此仍加了**兜底**：用数字量 DO 回显
  （按语义名含「进水泵」找通道，`do` 是位图）判通/断，规则是**同源取新**
  ——`DO 帧比泵帧新`（或泵快照不新鲜）就用 DO。泵本来就是输出点，与
  DO6 是同一事实，泵主题漏推/停推时界面不会钉在旧值上（切页才对的
  现象就从根上没了）。回归锁：`test_hsf_regression.py` §D 的 D10f。

- **C 端远端设置页对齐 Python（2026-11-12 收口）**：① 三模式块的「启动设备」
  由 8 个平铺复选框（4×2 网格）改成**多选下拉**，复用从 `HsfDataBar` 抽出的公共
  组件 `HsfMultiSelect`（按钮 + 勾选浮层 + 掩码；全库只有这一份，数据工具条的
  列筛选/图例同款），**禁止再写第二份**；块内两行（「启动设备」多选 / 「搅拌
  电机」转速 + 折算 Hz + 方向）按 Python 的 minWidth 110 基线成列，卡标题/说明段/
  按钮行同口径。改动：`HighSpeedFilter/client/ui/HsfMultiSelect.{h,c}`、
  `client/ui/HsfDataBar.c`、`client/ui/pages/HsfSettingsRemote.c`。
  **两个实锅（离屏截图抓出，都只靠肉眼看图才发现）**：
  ① **内置字库缺「▾」(U+25BE/25BC)**：`Library/XFont/XFontOutlineCommon.otf` 是
     Noto Sans CJK **子集**，`CharacterToGlyphMap` 里没有 25BE/25BC/25BD/2304/
     02C5/00BB（「未」等 CJK 正常）⇒ 凡「文字尾部补下箭头」的写法**箭头恒为
     空白**：多选钮、数据工具条「全部列  ▾」、状态栏账户钮「未登录  ▾」全都
     看着像普通文字。现全树统一改 **「∨」(U+2228，字库有、宽度相近)**：
     `HsfMultiSelect` 汇总、`HsfDataBar` 四个文案宏、`HsfStatusBar` 账户三态、
     远端设置多选三态（empty/all/none）。**C 端 UI 文本禁用 ▾/▼/⌄ 这类几何
     符号**——先查字库（`[Windows.Media.GlyphTypeface]` 读 OTF 的
     CharacterToGlyphMap 即可）；真要三角形走 `XPushButton_setIcon` 画图。
     另外**多选组件「空表」态也要自带箭头**：缺省掩码=0 与创建态相同 ⇒
     `HsfMultiSelect_setMask` 同值早退，按钮文字**停在 emptyText**、永远走不到
     noneText 分支（页面三块都显示没箭头的「未选择」）。
  ② **「方向」下拉被压成「…」**：`XComboBox` 不设宽度时按内容 sizeHint 收得
     很窄 ⇒ 项文字被省略。现 `srAutoDirMetrics`：按**下拉自己的字体**实测
     「正转」宽 + 44 余量（箭头 + 内边距），与变频器页 dir_combo 的 100px 基线
     取大者；**建页首帧与字号重放各调一次**（只量一次会量到旧字体/旧字号）。
  门禁（本轮）：构建 `NINJA_EXIT=0`；C `--test hsf-regression` **919 PASS / 0
  FAIL**；Python 客户端回归 **1449 PASS / 0 FAIL / 2 SKIP**；XGui 基线门禁
  **1 条环境性新红**（`xrot 90°: SendInput 点击经 R⁻¹ 命中 probe`）——同一份
  二进制 12:31 跑是 0 新红，`Src/XGui` 自 02:12 未再编译、测试源当天没改 ⇒
  变的只是**桌面输入/前台窗口态**；该用例注释自己写着「需交互会话，
  SendInput 落到别家窗口就全红」。**跑 XGui 门禁前先让桌面空出来**，否则这条
  会假红。离屏截图 `.zcode/tmp/remote_after3.png` 实见三块「未选择 ∨」、
  方向「正转 ▾」、状态栏「未登录 ∨」。
- **C 端功能面仍落后 Python 三段（2026-11-12 盘点）**：`cycle`（自动循环配置）、
  `historyTrigger`（入库触发）、`resetValvesOnStop/Estop`（逐模式阀复位）以及
  `autoStageElapsedMs/totalMs`（阶段剩余/RemainDial）在 `common/XhsfLinkProto.h`
  与 `HsfServerApp.c`、`HsfSettingsRemote.c` 里**一处都没有**（Python 单边先行）。
  C 端要与 Python 对齐时按 `server_py/hsfsrv/proto.py` 的键名逐值补，三处同源
  （C proto / Python proto / 服务端读取）。
- **C 端对齐 Python 三段功能 + 阶段剩余（2026-11-12 收口）**：
  ① **自动循环（组合模式「自动」）**：`auto` 配置节新增 `cycle` 子节
  （enabled/pressThreshold/pressCompare/timerFallback/timerMinutes/
  backwashMinutes/rinseMinutes），服务端按 产水→（压差或定时兜底）→反洗→
  （定时）→顺洗→（定时）→产水 换相；**压差读不到不切**（App 注入
  `hsfAppAutoPressureMpa`，复用遥测/历史同一条标定链，不在自动模块复制第二份）；
  换相＝停泵 + 重建阀目标回阀相，**累计运行时间不中断**；停止/急停记住
  子阶段（再启动从那一步继续），**复位清记忆**（下次从产水开始）。
  ② **逐模式阀复位**：`resetValvesOnStop`（缺省 true）/`resetValvesOnEstop`
  （缺省 false）随模式落盘；全停执行体 `stopAllDevicesEx(reason, resetValves)`，
  false ⇒ **不发送关阀命令**（阀保持现状）。
  ③ **入库触发**：`storage/historyTrigger`（always/auto）；auto=仅自动运行时
  入库，判定放在 lastSample 更新**之后**（跳过的这一拍也算耗掉周期）。
  SET 收到非法值**明确报错**（不能让拼错的键悄悄变成「一直记」）。
  ④ **阶段剩余**：0x0115/RUN_MODE 应答新增 `autoStageElapsedMs/autoStageTotalMs`
  （-1=不在跑/无阶段；total=0=本阶段无定时）；C 端首页自动卡状态行右侧加
  「剩余 X分Y秒 / 剩余 --（按压差切换）/ 剩余 --」，定宽按标签自身字体实测。
  **实锅（探针抓出，安全相关）**：自动模式下 `hsfAppApplyModeToDevices` 把
  变频器控制闸关掉 ⇒ **服务端自动控制器自己也被锁死**：启动搅拌泵报
  「控制闸未使能」直接中止、停止/急停的全停也停不下变频器（泵继续转）。
  修法：设备闸恒保持使能，模式闸只在 XLink 命令入口（`hsfAppControlGate`）
  生效——与 Python 同口径（控制器直接驱动设备，客户端命令被服务端拒）。
  **另两个实锅**：① `loadConfig` 复用同一个 json 缓冲且 getString 缺键时
  可能不写 buf ⇒ 启动日志「auto/cycle 不是合法 JSON」；现每键先清缓冲、
  cycle 只在值以 `{` 开头时解析。② 客户端循环块「比较」下拉/阈值框
  **写死宽度**在字号缩放/窄窗下会裁字 —— 改为**按字体实测的最小宽**
  （`srCyclePressMetrics`/`srCycleCmpMetrics`），宽度交给行内布局与
  stretch（用户口径：界面要适配不同尺寸屏幕，定宽不合适、多用自动布局）。
  回归锁更新：C `hsf_regression_test.c` S3/S4 组合模式断言由「未实现」改为
  「缺子模式」，S3 急停断言改为「默认阀保持现状」；新增探针
  `.zcode/tmp/probe_c_align.py`（配置往返/只带 cycle/fail-closed/阶段键/
  停止回 -1/入库触发）与 `.zcode/tmp/probe_c_vfdgate.py`（自动模式泵能启、
  相位到 running）。门禁：构建 0；C `hsf-regression` **919/0**；
  Python 客户端回归 **1463/0/2**；截图 `.zcode/tmp/remote_align3.png`。
- **2026-11-15 全量审计（Python 服务端 + 客户端 + C 服务端 + 客户端）**：
  用户口径「python 版的实现比 c 版齐全多了，有冲突对齐 python 版的」⇒
  **本仓库的默认事实源是 Python 版**；C 版与 Python 版冲突时**改 C 版**
  （协议常量/键名/文案/纪律逐条对齐），不是各写一套。
  本轮共修 20+ 处，**P0 三条都是"平时跑得好、遇到边界就出事"的类型**：
  ① **周期推送 await drain() 把整个 tick 卡死**（Python 服务端）：推送路径
     原来「写完 `await writer.drain()`」，而**周期推送是在服务端 tick 里
     await 的** ⇒ 对端读不动（TCP 窗口关）时 drain 永远不返回 ⇒ **一个
     卡死的客户端就把整个事件循环卡住** ⇒ 自动控制、全部遥测、告警推送
     一起停摆。C 端无此坑（XLink 用**发送队列 + 队满拒帧**，从不阻塞发布
     方）⇒ Python 改成**只写不等**（`drain=False`），缓冲超 4MB 丢这一帧
     （遥测是周期帧，丢一帧下一秒补上）；**应答帧与订阅补发仍走 drain=True**
     （必须确保送达，且不在 tick 里 await）。另：**心跳/挑战帧也不能等
     drain**（它们也是在 tick 里 await 的）。
     回归锁 `server_py/tests/test_publish_no_stall.py`（6 项）。
  ② **取发送缓冲大小的 API 写错 ⇒ 每拍抛异常 ⇒ tick 停摆**：
     `writer.get_write_buffer_size()` **不存在**（在 `writer.transport` 上）
     ⇒ 每拍 AttributeError ⇒ 整个 tick 停（遥测/按钮灯/自动控制一起停，
     而**命令往返照样正常** ⇒ 极难查）。⇒ 取值必须 `writer.transport.
     get_write_buffer_size()` 且**包 try/except**；**tick 里任何一处抛异常
     都等于停摆** ⇒ 服务端 tick 的每一步各自 try（`app.__call__` 的
     `_guard`），一步坏只坏它自己（安全路径：现场停止按钮/急停受理/运行
     保护/故障看门狗不许被"遥测推送"的一次异常带走）。
  ③ **落盘写库在"连接没了"时抛 AttributeError 并传染给业务代码**
     （Python `store._write`）：rotate 失败把连接置空 ⇒ `self._conn.execute`
     抛 AttributeError（**不在 `except sqlite3.Error` 内**）⇒ 从日志调用点
     冒泡 ⇒ 打断正在处理异常的代码（严重时连 tick 一起带死）。⇒ `_write`
     先取局部连接并判 None；`app._log_with_store` 对入库**包 try/except**
     （日志入口不许把异常抛给业务）。
  另：**后台容量维护的检查点一律 PASSIVE**（TRUNCATE 要等所有写者 ⇒ 前台
  写被挡 116~554ms）；只有 WAL ≤4MB 时才补一次 TRUNCATE 把文件收回（那时
  挡前台是毫秒级）；`store.close()` 的 `thread.join(2.0)` 改 0.2s（维护线程
  用**自己的连接**⇒ 放手安全，原来会把事件循环堵 2 秒）。
  **P1 清单（已修）**：配置 SET **先校验后写 + 失败整体回滚**（Python 用
  cfg 快照回滚，C 端 protect 走 `HsfServerProtect_applyConfigDoc` 整包
  校验）；**布尔键严格**（`bool("false")` 是 True ⇒ "只报警不停机"被翻成
  "直接停机" ⇒ **现场误停线** ⇒ 两端都只认真布尔/0/1，字符串一律拒）；
  **未知配置键明确报错**（原来静默忽略 ⇒ 客户端拼错键、服务端却说"已保存"）；
  **压差上限 0 是合法值**（`float(v or DEF)` 把 0 变成 0.8 ⇒ 界面显示 800
  kPa、服务端其实是 0 ⇒ 一点「应用」就改了现场参数）；**配置读文件容错**
  （手改配置写错类型 ⇒ 原来直接抛 ⇒ **服务起不来**；节不是对象/值类型不对
  一律当"没有这节"；`stale.flowmeter_ms`/`stale.turbidity_ms` **读得到却
  存不下** ⇒ 任何一次 save 都把现场手改的阈值抹掉 ⇒ 补写）；**换相 carry
  的收尾停泵**（`_stop_pumps()` 返回列表却被按单结果验收 ⇒ ValueError ⇒
  **整个模式中止** ⇒ 改 `_stop_pumps_agg()` 汇总成单结果）；**手动模式
  「现场在动」要看遥测有效性**（变频器掉线后最后一帧还留着 ⇒ 启动灯被
  **永久钉亮**；Python 补 `_vfd_telemetry_valid`，对齐 C 端 `m_valid`）；
  **按钮沿首帧不算按下**（常开键第一帧读到 1 就算"刚按下" ⇒ 服务端一启动、
  键恰好是 1 就**自动启动** ⇒ 两端沿判据都要求"上一电平**已知**且为 0"，
  C 端抽成 `HsfServerAutoMode_buttonEdgeRule()` 纯函数供回归锁驱动）；
  **告警入库按设备类别归类**（压差过大原来落成"变频器"⇒ 运维照着查错
  设备；两端都按 dev 逐类映射）；**客户端「应用」等首帧**（没收到 protect
  快照就下发 ⇒ 把服务端保护配置静默改成缺省 ⇒ 保护被关掉）；**客户端
  回显 0 不被改写**（上限 0 回显仍是 0）；**C 端 protect 缺省回落 0**
  （原来回落 60000 ⇒ 老服务端没这键时界面显示"60 秒"⇒ 用户一点「应用」
  就把保护打开成 60 秒）；**C 端两个「停设备」勾选框缺省勾上**（与 Python
  端一致，否则两端客户端对同一份配置的读法不同）；**Python 客户端报警页
  懒创建 ⇒ 建页前的告警全丢** ⇒ model 里留有界缓冲 + 建页补放。
  回归锁：C `hsf_regression_test.c` **§S20**（18 项：首帧沿判据 6 + protect
  fail-closed 9 + 故障停机结算运行时长 3）；Python `tests/test_protect.py`
  **A7~A10**（严格布尔/整包回滚/上限 0/告警归类）；Python 新文件
  `tests/test_audit_hardening.py`（27 项：carry 汇总/配置容错/写库连接为空/
  手动在动有效性/严格布尔）；客户端 **A10d/A10e** + 报警缓冲 2 项。
  本轮门禁：C **1091/0**；Python 服务端 **22/22 组**（含新增"审计整改"组）；
  Python 客户端 **1255/0**；网页端 `webui_check` **326/0**（C6100 163 + PY6200 163）。
  **本轮自己踩的坑（写下来免得再犯）**：给 tick 加"分步护栏"时把**同步**的
  `_jog_watchdog` 也当协程 await 了 ⇒ 每拍抛 "object NoneType can't be used in
  'await' expression" ⇒ **从这一步起整拍全废**（点动看门狗从没跑过），而日志
  只有一句"自动控制异常"⇒ 现场根本定位不到。⇒ ① 护栏用
  `inspect.isawaitable(coro)` 判（可等待才 await）；② **异常日志必须带调用栈**
  （`_log_auto_error` 现在追加 traceback 尾 8 帧 + 步骤名）——没有栈就定位不到
  路径；③ **这类改动必须真起一次服务端看日志**（单元测试的假件形状与真件不一致
  ⇒ 覆盖不到：假服务端 `publish` 写成同步返回 None 就会自己炸出同款错误 ⇒
  假件必须**照抄真形状**）。

- **2026-11-16 第二轮全量审计（三路只读代理扫 Python 服务端×2 + 客户端，
  30+ 条按「现实触发路径」分诊，全修+回归锁）**：上轮（11-15）修的是
  「事件循环停摆」家族；这轮扫出的是**语义/边界**家族，高优先级 7 条：
  ① **运行中 op=5 改选模式会打断生产**（两端）：Python 控制器的
     `mode` 同时是组合循环的**当前子阶段**（_cycle_tick 按它判换相、
     stage_timing 按它取定时）⇒ 运行中一条 op=5 把产水改写成"顺洗"⇒
     下一拍就按顺洗时限判到期、换相停泵。粘性选择的用途是"现场启动按钮
     按上次选的模式启动"——运行中启动按钮本来就无效 ⇒ **两端都在运行中
     拒绝**（文案逐字同："自动运行中不可改选模式（请先停止运行）"；
     C 端 selectMode 本体也加防御）。锁：PY test_audit_hardening H1/
     H1b + C §S15b。
  ② **VFD START 两笔总线写可被停止/急停插队 ⇒ 急停后电机照跑**（PY）：
     apply_control(START)=先写 0x1000 给定、再写 0x2000 命令字，中间让出
     事件循环 ⇒ 全停的 OP_STOP 插进来、随后 START 命令字落地。⇒ 设备级
     `asyncio.Lock`（懒建）串行化 apply_control——命令按到达顺序全整
     执行、最新意图赢。C 端单线程 tick 无此竞态。
  ③ **数字量模块单拍失败即判离线 ⇒ 假急停锁存**（PY）：DigitalIo 没有
     VfdM512/TempHum 那样的 FAILS_TO_OFFLINE 去抖，而 _check_estop 又把
     "状态未知"当真急停 ⇒ 一帧空应答（现场 MT3A 文档记载会间歇性空应答）
     就全停+锁存要人工复位。⇒ 双修：DigitalIo 加 FAILS_TO_OFFLINE=3；
     _check_estop 把 **None 与 False 拆开**——False（回路确实不通，常闭
     口径"断线=按下"）仍立即锁存，None（读不到数据）要**连续
     ESTOP_NONE_LATCH_TICKS(5) 拍**才锁存；**首帧未到不锁存**的判据对
     None/False 都保留（DigitalIo 未 poll 时 di_bit 返回 False，同属
     "首帧未到"形态——这轮自己就踩了：拆分时把这个 2026-11-06 判据弄丢，
     test_no_latch_before_first_frame 抓回来了）。
  ④ **历史热身门被"配置性永久空洞"卡死**（PY）：六组字段要求**全部**见过
     有效值 ⇒ 停用浊度2/关掉数字量/模拟量未标定 ⇒ 对应字段永远 NULL ⇒
     **历史表一行不写、无任何日志**（板端 2026-11-11「历史看不到」的配置
     性版本，比那次更隐蔽）。⇒ 每组先问 `_hist_warm_keys`：停用/未装配/
     未标定的组不参与判定；浊度组**逐台**过滤。锁：test_audit_hardening
     H2/H2b/H2c。
  ⑤ **客户端趋势图把单调钟当墙钟**：点时刻是 `updated_ms`（monotonic），
     画轴/悬停直接 `localtime` ⇒ "1970+开机时长"假时刻。⇒
     `model.mono_to_epoch_ms()`（time.time()-time.monotonic() 进程内偏移）
     只在**渲染**时换算，数据侧窗口/判重不动。
  ⑥ **导出进行中再点导出 ⇒ 两个 BatchExport 交错写同一个文件**：入口
     加互斥闸（`runner.running` ⇒ 状态行"正在导出中"），历史/日志两页。
  ⑦ **顶栏模式钮失败回调跨线程碰 QLabel**（PY 客户端）：`_done` 在网络
     线程执行 ⇒ 直接 status_bar.set_message 是本文件注释明令禁止的路径
     ⇒ 改走 `statusMessage.emit`。
  中优先级（值得单说）：**store rotate 与后台维护线程竞态**——rotate
  先等旧线程退出（有上限 1.5s 补等），等不到就**本拍放弃滚动**（连接一个
  不动、`_rotate_pending` 保留下拍再试；新增 `_RotateRetry` 区别于真失败），
  成功滚动即清 `_pause_reason`（原来失败置位后永不清 ⇒ 界面恒"已暂停入库"
  而数据照写）；**读连接的 close 挪后台线程**（摘引用持锁、close 等在途
  长查询，rotate 改名前等关闭完成事件——句柄没落地改名必撞 WinError 32）；
  **并发读任务强引用 + 30s 超时**（防对端"发请求但永不读"时任务无限挂
  drain）；**carry 收尾停泵失败被永久丢弃**（`_carry_pending_stop` 原来
  建表即清 ⇒ 重投不再含停泵任务 ⇒ 泵一直转；改为验收全成功才清）；
  **循环 stop/e_stop 先取阀复位配置再还原粘性**（原来还原后读到组合根
  配置，子阶段的「停止/急停不复位阀」被静默忽略）；**循环粘性落 "auto"**
  （原来落子阶段 id ⇒ 重启后按键按单模式跑到底、循环链断；对齐 C）；
  **数字量订阅补发逐模块名字帧**（原来只回模块 0，模块 1+ 通道名永远
  收不到）；**配置四组键补读/存**（turbidity1/2+flowmeter 逐台周期、
  serial 铭牌三参数、sim 时序旋钮——原来"读得到存不下/存了读不回"）；
  **阀超时冻结**（模块断链期间动作中的阀不计 20s 超时，恢复后补时长——
  否则把"总线断了"误报成"阀超时查接线"）；**VFD health 陈旧先于故障**；
  客户端：**model.digital 迭代快照**（网络线程插键 vs UI 遍历 ⇒
  RuntimeError）、**阀告警队列封顶 200**、**温湿度/浊度值字段 apply 时
  收敛**（坏类型帧按"无数据"，不滞留 model 每拍炸 refresh）、
  **断线/退出登录复位 known 标志+清告警缓冲**（原来只置位从不清 ⇒ 断链
  期间首页继续显示"运行中·产水"）、**历史图表离页即停取数链**、
  **request_sync 先判活再建协程**、**桥 log 走 _emit_net**、**网页服务
  起停挪后台线程**（wait_ready(10s)/join(5s) 不许在 UI 线程）、
  **日志页收起时只攒不刷**、**登录 busy 期忽略 ×**（processEvents 重入
  窗）、**TimePicker 弹层 deleteLater**、**xlsx 表名先转义再安全截**
  （原截断点落在实体中间 ⇒ Excel 打不开）、**CONNECTED 阶段加握手
  看门狗**（accept 后无应答不再永久"连接中"）。
  锁：PY test_audit_hardening **H 段 9 项**（全套 39/0）；客户端报警段
  **L7a~L7h**；C §S15b 4 项。
  门禁（最终）：C **hsf-regression 1097/0**；PY 服务端 run_all **23/23 组**
  （新增「名字脏帧」组）；PY 客户端 **1263/0**；webui_check **326/0**；
  真协议冒烟（protect fail-closed 10 项）**10/0**；服务端日志零异常。
  现场两端已用本轮代码重启（6100 sim + 客户端/网页端 8085）。

- **第二轮审计的四个「写的时候自己踩的坑」（都已被回归抓回，记录防再犯）**：
  ① **拆 None/False 时把「首帧未到不锁存」弄丢了**：_check_estop 拆分后
     只给 None 留了 ever_ok 守卫，False（DigitalIo 未 poll 时 di_bit 返回
     0=False，同属"首帧未到"形态）直接落进锁存分支 ⇒
     test_no_latch_before_first_frame 红。⇒ **改安全判据必须先翻该判据
     的历史实锅注释**（2026-11-06 板端），拆分支时逐分支核对旧守卫是否
     仍覆盖。
  ② **数字量订阅补发第一版发"只含名字"的脏帧** ⇒ 双端对照抓第一帧当形状
     样本 ⇒ PY 侧 DIGITAL 主题缺 di/do/valid/staleMs（C/PY 键集对不上）
     ⇒ 补发必须用**与周期/订阅补发同形的全量帧**（encode_digital 缺省
     full=True），新发明帧形要先过双端形状对照。
  ③ **收起判据用 isVisible() 在离屏测试恒 False**（页面本身没 show）⇒
     I3 红；本页自己的收/展状态用**显式标志**（_client_log_open）。
  ④ **信号挂错类**：webOpDone 起初加在 RemoteSettingsPage，而
     _on_web_enabled/_on_web_op_done 在 LocalSettingsPage（网页服务分区
     在本地设置页④）⇒ 构造期 AttributeError。改前先确认方法与信号同一
     个类（/usr/bin/grep 两侧的行号夹在哪个 class 里）。
  另：**不进门禁的锁等于没有锁**——test_digital_names_dirty.py 只能
  standalone 跑 ⇒ 2026-11-12 阀位序订正后断言过期近四天没人发现；本轮已
  注册进 run_all（「名字脏帧」组）并按现行口径修断言。

- **Web 设置独立页 + 扫码直达 + 网页端缺省 admin（2026-11-16 用户定案）**：
  ① **网页服务从本地设置拆成独立页** `PAGE_SETTINGS_WEB=16`（**仅 Python
     客户端**，C 端不实现）：开关/端口/域名/远程端口/二维码全在这一页；
     本地设置页的分区四整体删除（**两处各配一份必然漂移**）。页号**追加在
     末尾、不重排**（0..15 是操作手册契约）；跨端同形断言的容许差异只此
     一项（测试已改为「前 15 节点与 C 端逐项一致 + 末尾多 Web 设置」）。
  ② **远程访问**：`webDomain`（域名，落盘前规范化去 scheme/路径/端口）+
     `webRemotePort`（0 = 与网页端口相同）。只**展示**地址，不做内网穿透、
     不改用户路由器。
  ③ **二维码走框架**（AGENTS 铁律：框架有的绝不手搓）：`Src/XCore/XCode/
     XRcode` 已有完整实现（L/H 两级、含不等长 RS 分块），`hsb_bridge` 加
     扁平 ABI（`hsb_qr_encode/size/matrix/destroy`；**malloc 语义不适用——
     矩阵指针由编码器内部持有，下次 encode/destroy 前有效**），
     `hsf_native.qr_encode()` 用 ctypes **立刻整块拷走**。库缺失时页面
     置灰并显示"未编译 hsb_bridge（.zcode/build_hsf_bridge.bat）"。
     **框架只内置 L 与 H**（M/Q 表项未内置，传入即失败）⇒ 缺省用 H（30%
     冗余，手机扫反光屏更稳；URL 20~40 字节也只到 v4）。
  ④ **顺手修掉框架 XRcode 的真缺陷（2026-11-16，sr/XCore/XCode/XRcode）**：
     `set_code_word` 只把数据拷进 RS 缓冲、**没清零其后的 ecc 区**，而
     `rs_encode_block` 是**移位除法**（最后一轮会读到 `data[ecc_count]`）
     ⇒ 残留来自上一次编码 ⇒ 纠错码字算错 ⇒ **码外观完全正常、尺寸/格式位/
     码字数全对，但扫码器解不出**；且**新建实例首次编码偶然正确**（create
     时内存为 0）、同一实例第二次起才错 ⇒ 现场表现为「有时能扫有时不能」。
     **库内自检抓不到它**（只查尺寸/格式位），是「hsb_bridge 渲染 → zxing
     解码」这种**端到端对照**抓出来的。修法：memcpy 后
     `memset(rsec + len, 0, ec_count)`。回归锁：`--test xcode-qrcode`
     （`Test/XCodeTest/XRcodeTest.c` 的 `XRcodeTest_run()`：同实例连续编码 /
     跨版本往返 / L↔H 交替，逐字节比对码字与矩阵）。
     **教训**：给别人的库接 UI 时，**必须用真实解码器验一遍**——尺寸对、
     格式位对、能"看起来像二维码"都不等于能扫。
  ⑤ **网页端缺省登录改 admin（最低权限）**：`webui_py/hsfweb/static/js/
     login.js` 预填 `admin`、占位符随用户名，**空口令补该账户的缺省口令**
     （admin/oper/root 与其用户名同名）——原来一律补 `root`，缺省账户改成
     admin 后空口令会拿 root 的口令去登 admin ⇒ 必报"口令错误"。
     卡片底部加一行提示：admin=只读（查看与下载）、oper=控制设备、root=管配置。
  ⑥ **客户端回归**：§22E 由「本地设置页·网页服务分区」改为「Web 设置页」并
     扩到 15 项（E9~E15：域名规范化/远程端口/局域网码内容/切换远程码/native
     缺库提示/未配域名提示）；§34 与 `test_settings_merge.py` 的页数/导航
     断言同步 17 页。门禁：客户端 **1270/0**；`--test xcode-qrcode` PASS；
     `hsb_bridge` 重编（调试导出已删）。
  ⑦ **两条观感实锅（离屏截图抓的）**：`QWidget` 缺省 `sizeHint` 无效（-1）
     ⇒ Preferred 策略下布局只给 `minimumSize`，二维码被压成 100px；
     必须实现 `sizeHint()`。**说明文字别贴在同一行的微调框右侧**（窄宽/字号
     放大时被挤成三行）⇒ 独立整行 caption。

- **数据三页（历史/日志/报警）功能核查 + 导出压力实测（2026-11-17 用户口径
  「检查数据的三个界面的所有功能是否正常；导出有没有最大条数限制；筛选是否
  都可用；导出超长数据会不会让服务端卡顿影响自动控制」）**：
  **一、导出条数上限（结论）**：CSV（历史/日志）与 XLSX（历史）**无上限**
  （`BatchExport(max_rows=0)` + 流式写出 + 服务端 SQL 只有 LIMIT/OFFSET）；
  **报警页封顶 200 条**（`AlarmPage.MAX_ROWS`，客户端内存环形，且服务端
  `alarms` 表**没有查询命令** ⇒ 更早的报警客户端拿不到）⇒ 导出状态行现在
  达上限时明确写「仅本机内存中最近 200 条」。
  **二、实测（真服务端 + 真协议，独立临时库）**：

  | 规模 | 导出耗时 | 每批 p50 | 导出期间自动控制 | 事件循环最大漂移 | tick 异常 |
  |---|---|---|---|---|---|
  | 5 万行 | 10.3s（4863 行/s） | 8ms | p95 0.3ms / max 0.8ms | 16.2ms | 0 |
  | 20 万行 | 165.8s（1206 行/s） | 33ms | p95 0.4ms / max 0.7ms | 17.7ms | 0 |
  | 20 万行+关键字（最坏：16 显示表达式 LIKE 全表扫） | ——（单批 **790ms**） | 790ms | **0.26~0.36ms** | 20.3ms | 0 |

  ⇒ **导出（含最坏的全表 LIKE）不影响自动控制**：慢 SQL 全在
  `asyncio.to_thread` + 独立读连接上；控制命令走串行路径、不排那把读锁。
  单帧载荷实测：历史 50 行 **11.9KB**（16KB 上限余量 27%）、导出批 40 行
  9.6KB（余量 41%）、日志 50 行 3.6KB。
  **已知性能限制（不修，记录）**：每批仍跑一次 `COUNT(*)` 且 OFFSET 逐批
  加深 ⇒ 总耗时随行数超线性（5 万 8ms/批 → 20 万 33ms/批）；关键字导出
  20 万行约 **66 分钟**（这是"慢"不是"卡"，自动控制无感）。要更快得改
  keyset 分页 + 导出模式跳过 COUNT（协议要加键、客户端三页共用取数器，
  面太大，留待现场真嫌慢时再做）。
  **三、本轮修掉的缺陷**：
  ① **图表批与表格页应答串台**（`pages_other.py`）：两路共用 `_inflight` +
     `_pending_kind` ⇒ 表格页 20 行应答被图表当"短批（<50）"收尾 ⇒ **图表
     只画第一页**且状态行报"取回 20 行"；反向图表在途时翻页被静默吞掉。
     修法：**独立信号通道** `chartReady/chartFailed`（应答归属由"走哪个通道"
     决定，不再靠共享状态猜）。锁：§14d A1~A3。
  ② **离页中断图表取数 ⇒ 半截数据永不自愈**：`on_leave` 只置
     `_chart_fetching=False`，`_chart_rows`/`_chart_sig` 留着 ⇒ 回来见
     "有 rows + 签名相同"就用**半截数据**重画且不再取数（只有改筛选才恢复）。
     修法：离页清 `_chart_rows`/`_chart_sig`。锁：§14d B1/B2。
  ③ **XLSX 收尾失败被成功文案覆盖**：`_export_close_xlsx` 写"导出失败"后
     紧接着 `_finished` 无条件覆盖成"已导出 N 条 → 路径"（磁盘满/权限不足
     发生在 zip 收尾时，用户看到成功、文件打不开）。修法：
     `_export_write_error` 留痕，收尾判定**失败优先**。
  ④ **导出收尾文案三件事收进纯函数** `hsf_export_plan.export_finish_text`：
     0 行 ⇒「未生成文件」（原来报"已导出 0 条 → 路径"）；写出失败优先；
     **实际行数 < 服务端 total ⇒ 说明"导出期间库被裁剪/滚动未取到 N 行"**
     （ring/rotate 与导出无快照隔离，原来装作导全了）。锁：Q5a~Q5d。
  ⑤ **流式导出不补默认后缀**：保存框未设 defaultSuffix ⇒ 无后缀文件名写出
     "打不开的无扩展名文件"；按过滤器补 `.csv`。
  ⑥ **历史页关键字回车不生效**（另两页有）⇒ 接线；**起始晚于结束不拦**
     （另两页拦并提示）⇒ 拦截并提示「起始时间晚于结束时间，未筛选」。
     锁：§14e A1/A2。
  ⑦ **服务端日志页落盘未启用静默回空表**（客户端显示"共 0 条"像真没日志）
     ⇒ 明确回「落盘未启用：服务端没有日志可查」（对齐历史页）。锁：I1/I1b。
  ⑧ **分页应答超单帧上限会踢掉整个会话**：客户端 `decode_header` 对 >16KB
     帧返回 None ⇒ 判"帧头非法" ⇒ 断链重连（表现"一翻页就掉线"）。服务端
     组帧前量大小，超限（`XLINK_MAX_PAYLOAD - 512`）改回**短失败包**。
     锁：I2/I2b。
  **四、导出顺序也修了**：服务端每批内部**升序**（取页后 reversed）、批序
  offset 递增（最新批在前）⇒ 直接追加时文件是「最新块升序→更早块升序」，
  **块与块之间时间倒跳**（Excel 直接画图出锯齿）。修法：客户端导出
  **每批反转**（历史/日志两页）⇒ 全局严格降序（与界面"最新在上"同序），
  且**仍然流式**（零内存代价，不违反"不限量"）。锁：§14f（真页面 + 假桥
  按真实批序喂数，断言文件时间列不递增）。
  **写这个用例的两个坑**：① 假桥若是按行数组正序切片，批序与真服务端
  **相反** ⇒ 导出变成"最早在前"（本用例先踩过）——夹具必须照抄"批0=最新、
  批内升序"；② 导出首列是**格式化文本**（"2026-11-06 01:33:59"），按 int
  解析会 ValueError；同格式字典序=时间序，直接字符串比较。
  **五、仍未修（如实挂账）**：报警页健康边沿只在该页打开时采（不在页期间
  的设备异常不入表，只有自动告警有 model 缓冲补放）；导出不可取消
  （`BatchExport.abort()` 从未接按钮，提示文案却写"或取消"）；类型/类别
  下拉定宽 110px；关键字 >128 字符服务端**静默截断**（无提示）；日志关键字
  搜不到界面上的中文类别（服务端 LIKE 的是 `conn/ctrl/...` 字面）。
  门禁（最终）：客户端 **1282/0**（history 318、exportq 32、log 91、alarm 91）；
  服务端 **23/23 组**（audit 43/0）。
  另：`log`/`toolbar` 两组曾因我把报警页的 `_records`/`MAX_ROWS` 提示直接塞进
  `_write_export` 而红——**`_write_export` 是报警页与日志页的同名方法**，
  日志页没有那两个属性 ⇒ 必须 `getattr` 兜底（同名方法跨类复用是这类
  AttributeError 的典型来源）。

- **关键字导出 20 万行 66 分钟 → 11.5 秒（2026-11-17 用户「太慢了，要优化」）**：
  **先拆成本再动手**（`store` 层直测 20 万行，`EXPLAIN` 确认 `SCAN history`）：

  | 项 | 耗时 | 结论 |
  |---|---|---|
  | `COUNT(*)` + 关键字 | **747ms** | 每批都跑 ⇒ 单批 790ms 的绝大部分 |
  | 取页 + 关键字 + OFFSET=0 | 0.3ms | 命中即满 40 行就停 |
  | 取页 + 关键字 + OFFSET=10 万 | 370ms | OFFSET 每批**索引从头扫** |
  | 取页 + 关键字 + OFFSET=19.9 万 | 753ms | ⇒ 总成本 O(N²) |
  | 取页 无关键字 + OFFSET=0 | 0.1ms | |
  | `COUNT(*)` 无关键字 | 2.6ms | 主键扫，便宜 |

  **两处优化（都是"消除重复劳动"，不是换算法）**：
  ① **导出模式不数总数**：`store.query_history_page/query_logs_page` 加
     `with_count`；`app` 的 0x0118/0x0119 在 `export=true` 时 `with_count=False`
     且 `total=-1`（未知），**`history_bounds()` 与 `row_count()` 也一并跳过**
     （导出一样用不到，后者还占事件循环线程）。界面翻页（未带 export）照旧。
  ② **导出改 keyset 分页**（桌面客户端 + 网页端）：每批把 `beforeTs` 推进到
     「上一批最旧那行的 **ts - 1**」（历史 `ts` 是主键唯一 ⇒ 等价严格 `<`），
     `offset` 恒 0 ⇒ 每批只扫本批需要的行。**日志页仍用 offset**（日志 `ts`
     可能重复，keyset 会重复取）——日志导出无关键字时本来就快。
  **协议零改动**：`export` 键与 `beforeTs` 都是既有键；C 端不传 export ⇒ 行为不变。
  **实测（20 万行，真服务端 + 真协议，导出期间并发量自动控制）**：

  | 场景 | 优化前 | 优化后 |
  |---|---|---|
  | 无关键字导出 | 165.8s | **10.0s**（19905 行/s） |
  | 关键字导出（全命中） | ~66 分钟 | **11.5s**（17407 行/s） |
  | 关键字导出（0 命中） | 0.8s（1 批） | 0.8s（全表扫下界，无法再省） |

  每批 p50 从 790ms → **2ms**；自动控制 p95 0.29ms、事件循环漂移 ≤21ms、tick 异常 0。
  **回归锁**：服务端 `test_audit_hardening` **I3/I3b/I3c**（导出跳过 count/bounds/
  row_count 且 total=-1；界面翻页三项照旧）；客户端 §14f（文件行数=命中数 +
  时间降序）在 keyset 下仍绿；**Q3c 断言改为 keyset 语义**（offset 恒 0 +
  beforeTs 严格递减——旧的「offset 0,40,80」已随实现作废）。
  **三个写测试/写脚本的坑**：① 假桥必须按 `beforeTs` 过滤（否则 keyset 下每次
  返回同一批 ⇒ 导出**死循环**，实测 800040 行 = 20001 批 × 40）；② 探针脚本
  手写 `"before_ts"` 服务端不认（协议键是 **`beforeTs`**）⇒ 表现为"批量一直
  从最新开始"；③ `EXPORT_ROWS` 这类模块级常量必须在 **import 之前**设环境变量。

- **导出优化补充（2026-11-17 第二轮：网页端同口径 + 夹具坑）**：
  ① **网页端历史导出也改 keyset**（`webui_py/hsfweb/api.py` 的
     `_history_export`）：`keyset={"before": anchor}`，每批 `beforeTs` 推进到
     本批最旧 ts-1、`offset` 恒 0 ⇒ 与服务端「导出不数总数」配合，网页端
     20 万行关键字导出同样从数十分钟降到十几秒。**日志导出保持 offset**
     （日志 ts 可重复，keyset 会重复取）。
  ② **客户端三处假桥必须按 `beforeTs` 过滤**（`_FakeBridge` /
     `_ExportOrderBridge` / Q3 的同步假桥）：客户端导出改 keyset 后
     `offset` 恒 0，旧假桥按 offset 切片 ⇒ **每次返回同一批** ⇒ 导出死循环
     （实测 800040 行 = 20001 批 × 40）或文件里同一段写两遍（§14f 时间列
     不单调变红）。修法：假桥按 `ts <= beforeTs` 过滤后切片，并保持
     "批0=最新、批内升序"。
  ③ **`requests` 元组的形状不要随手加字段**：`_FakeBridge` 的
     `self.requests` 被 §14b 断言按 **2 元组**解包（`for _off, lim in …`）
     ⇒ 我在导出夹具上加 `before_ts` 时误改到它 ⇒
     `ValueError: too many values to unpack`。导出专用的三元组只加在
     `_ExportOrderBridge` 上。
  ④ **§14f 的夹具批序**（既有教训再确认）：假桥按行数组正序切片会把批序
     搞反（"最早在前"）；必须照抄真服务端"批0=最新、批内升序"。
  ⑤ 残留观察：全套客户端回归出现一次 `settingsmerge` 子进程
     `exit=0xC0000005`（§34 标题后崩），**单跑该段 51/0 不复现**，重跑全套
     也未复现 ⇒ 记为偶发（与既有「探针 teardown UAF」同族，未见与本轮改动
     的因果）。若后续稳定复现，再按"子进程 + cdb 抓栈"定位。

- **网页端「打开一片白」＝JS 整份文件没执行（2026-11-17 实锅）**：
  现象：http://127.0.0.1:8085/ 打开全白，控制台级错误用户看不到。
  根因：`hsfweb/static/js/login.js` 把两段字符串写成**相邻字面量跨行**
  （`"…oper，"` 换行 `"管理配置用 root。"`）——JS **没有** C/Python 那种
  隐式字符串拼接 ⇒ 整份文件 `SyntaxError` ⇒ `HSF.login` 从未注册 ⇒
  `app.js` 启动时 `HSF.login.mount(...)` 抛 TypeError ⇒ `#login-view`
  与 `#app-shell` 都停在空/隐藏 ⇒ 页面**全白**。
  **诊断三步（照这个顺序，别猜 CSS）**：
  ① 真浏览器 evaluate：`document.querySelectorAll('#login-view *').length`
  与 `typeof window.HSF.login` —— children=0 且 login=undefined 就是
  "脚本没执行"，不是样式问题；
  ② `node --check` 逐个静态 js（一条命令定位到行号）；
  ③ 注意 **协议级 webui_check 抓不到这类**：它只打 HTTP API，从不加载
  页面 ⇒ 后端全绿、前端照样白屏（这次就是这样漏出去的）。
  **回归锁（已加进 `verify/webui/webui_check.py` 的两段「页面自举」）**：
  ① `sec_static`：index.html 引用的每个静态资源必须在盘上 +
  `node --check` 全部 31 个 js（node 缺省找 `node`，找不到记说明不假绿）；
  ② `sec_page_boot`：真起 Edge `--headless=new` + CDP 打开首页，断言
  `.login-card` 已渲染、`#login-view` 有子节点、`typeof HSF.login ==
  "object"`、文案含「登录」，并截图留证 `verify/webui/shots/webui_boot_<tag>.png`
  （无浏览器时记一条带说明的 PASS）。两端各跑一次（约 +8s/端）。
  **写 JS 的纪律**：跨行拼字符串一律 `+`；改完 webui 的 js **先跑
  `node --check` 再刷新页面**。

- **网页端缺省着陆页 = 主界面(0)，桌面/移动统一（2026-11-17 用户定案
  「web 不管是不是移动端页统一默认进入主界面」）**：
  `hsfweb/static/js/router.js` 的 `DEFAULT_PAGE` 由 1（主菜单）改 **0（主界面）**。
  此前注释写着「主界面(0) 页面模块接入后改回 0」——**页面（page00_home.js）
  早已接入，注释却在撒谎**（半年没人改）⇒ 打开 web 一律先看到主菜单。
  主菜单(1) 仍可从导航树/主界面卡片进入，深链 `#/page/N` 不受影响。
  **别在 router 里加 `isNarrow()` 特判**（"移动端进主菜单"）——用户要的就是
  两端统一；移动端靠抽屉导航（☰）访问其他页。
  回归锁：`verify/webui/webui_check.py` 的 **sec_landing**（每端 4 检）：
  用 `?authtoken=<token>` 真开页面 —— ① 桌面 1280：`HSF.router.current()==0`
  + `.home-page` 已挂 + `hash == "#/page/0"`；② CDP 设备模拟 390×844
  （mobile）：`HSF.isNarrow()` 生效 + **重新载入（hash 清空）后仍落主界面(0)**；
  两端截图留证 `verify/webui/shots/webui_land_{tag}.png` / `_mobile.png`。

- **板端网页改由客户端内嵌提供（2026-11-17 用户定案「web 跟客户端一起
  启动，把 web 的自启删掉」）**：删除 `hsfwebui.service`（独立
  `python -m hsfweb --web-port 8080`）后，网页改由**客户端内嵌**的
  hsfweb 提供（同进程线程，客户端一退端口即释放）。
  **三个必须一起改的点（少一个就"没有 web"）**：
  ① `run_hsfclient.sh` 的 `--auto 127.0.0.1 --port 6100` **去掉 `--auto`**
     （`--auto` 是无头 CI 路径，代码里明确**不起内嵌 web**）⇒ 改
     `--port 6100 --fullscreen`，走「本地探测服务端」路径：root/root 自动
     登录后才起 web（密码不对会回退登录窗）；
  ② `export HSF_WEB_DIR=/root/Desktop/XinYueC/hsf_webui`（内嵌 web 的包
     目录候选里没有 `hsf_webui` 这个名字）；
  ③ 板端 `hsf_client_py/cfg/hsf_client_py.ini` 加 `webPort=8080`（沿用旧
     web 端口，旧链接不作废；不写缺省 8085）+ `webEnabled=true`。
  删除单元：`systemctl disable hsfwebui && rm -f
  /etc/systemd/system/hsfwebui.service && systemctl daemon-reload`，
  并确认 rc.local / xdg autostart 里没有 hsfweb 残留。
  **验证**：`ss -tln` 的 8080 属主必须是**客户端 PID**（`systemctl show
  hsfclientpy -p MainPID`）；journal 有「探测到本地服务端…自动登录」与
  「网页服务器就绪：http://127.0.0.1:8080/」；`/etc/systemd/system` 只剩
  hsfclientpy/hsfserverpy。备份在
  `/root/Desktop/XinYueC/backup-20261117/`（含旧 unit 与旧启动脚本）。

- **板端客户端必须带 `hsb_bridge.so`（跨编译口径，2026-11-17 实锅）**：
  用户反馈板端「Web 设置」页提示「未编译 native 桥…无法生成二维码」——
  根因是板端部署时**把 `hsfsrv_client/bin/` 排除了**（只同步 python 包），
  而二维码/WiFi/屏幕休眠都靠 `hsb_bridge.so`（`hsf_native.library_path()`
  查 `hsfsrv_client/bin/hsb_bridge.so`）。**客户端包部署清单必须包含
  `hsfsrv_client/bin/hsb_bridge.so`（aarch64）**。
  **跨编译配方**（WSL，复用 `~/xboard`）：
  ① 源码同步到 `/root/xinyuec-src`（rsync 排除 .git/.vs/out/bin/.zcode/docs/db）；
  ② `cmake -S /root/xinyuec-src -B /root/xboard`（缓存里工具链已定）后
     `cmake --build /root/xboard --target hsb_bridge -j 12`；
  ③ **坑：共享库链接行缺 isoc23 垫片** ⇒ ctypes 加载报
     `undefined symbol: __isoc23_sscanf`（工具链只在
     `CMAKE_EXE_LINKER_FLAGS_INIT` 里放了 `/root/isoc23_shim.o`，
     `CMAKE_SHARED_LINKER_FLAGS_INIT` 没有）。修法：用 sysroot 头编一个
     **PIC 版**垫片
     `aarch64-linux-gnu-gcc -fPIC -O2 -B/root/board-sysroot/usr/lib/aarch64-linux-gnu/ -I/root/board-sysroot/usr/include --sysroot=/root/board-sysroot -c /root/isoc23_shim.c -o /root/isoc23_shim_pic.o`，
     再 `cmake -B /root/xboard -DCMAKE_SHARED_LINKER_FLAGS='/root/isoc23_shim_pic.o -B… --sysroot=… -L…'` 重配 + 重链；
  ④ 验收：`nm -D --undefined-only hsb_bridge.so | grep isoc23` 必须为空、
     `readelf --dyn-syms` 的 GLIBC 版本 ≤ 2.31（板子 glibc 2.31）、
     板端 `python3 -c "from hsfsrv_client import hsf_native; print(
     hsf_native.available()); print(hsf_native.qr_encode('http://…'))"`
     （size×size == len(data)）。
  **界面提示的坑**：`QrCanvas` 把 `NativeUnavailable` 一律显示成「未编译」，
  **把真实加载错误（如 undefined symbol）吞掉了**——排查时先手动
  `hsf_native.qr_encode()` 看异常原文，别信那句"未编译"。
  **2026-11-20 复发（文件名陷阱）**：板端 `hsfsrv_client/bin/` 里只有
  `hsb_bridge.so.linux`（当年为区分平台改的后缀）⇒ 加载器只认
  `hsb_bridge.so` ⇒ 二维码/WiFi/熄屏又全报「未编译」。三条收口：
  ① **仓内 `hsfsrv_client/bin/` 直接放正确名 `hsb_bridge.so`**（aarch64，
  `.linux` 后缀等于没有），以后整目录拷上板即生效；
  ② **加载失败在进程内被 `_DLL_TRIED` 缓存**（`hsf_native._load`）⇒
  换库后**必须重启客户端**（`systemctl restart hsfclientpy`），热换无效；
  ③ 网页端只读 WiFi 快照（`hsfweb/hsbnative.py`）的候选路径**不含**
  `XinYueC/bin` ⇒ 要放 **`hsf_webui/bin/hsb_bridge.so`**，重启 hsfwebui 生效。
  本次复建实测：24 个导出、`GLIBC ≤ 2.29`、isoc23 未定义 0；板端
  QR 渲染正常（v4/33 模块）+ `wlan0`（杭煤科临江/WPA2）读取正常。

- **Python 客户端主题切换卡顿（2026-11-17 实锅，板端 11.3s→0.18s）**：
  用户口径「客户端切换主题的时候明显卡顿」。四层根因（都对"全部控件
  重洗一遍"）：
  ① **每次 apply_theme 都 `app.setStyle("Fusion")`**：重建 QStyle 并
     重刷全树——1445 控件实测 730ms（桌面）/ 19s（板端）；样式引擎本来
     就不变 ⇒ `install()` 里用 `app._hsf_fusion_style` 标记**只装一次**
     （不能查 className：设过 QSS 后 `app.style()` 是 QStyleSheetStyle
     包壳，报基类 QCommonStyle）。
  ② **大 QSS 挂在 QApplication 上**：`app.setStyleSheet(全表)` 板端 9.7s，
     同样内容 `窗口.setStyleSheet` 1.7s、**单页** ~0.4s（三级差 5~20 倍）；
     且带 CSS 的 app 级换装会连带重刷所有弹层。
  ③ **chrome 刷新把页面栈也算了**：`refresh_widgets(root=chrome)` 的
     findChildren 包含**页面栈**（栈是 chrome 的子孙）⇒ 一次把 17 页
     54 张卡片全刷（板端另加 1.2s）⇒ 只逐控件刷 title/status/nav 三个。
  ④ **Qt 内部弹层窗口**：每个 QComboBox 的 `QComboBoxPrivateContainer`
     都是顶层窗口（界面 33 个）⇒ 逐个下发大 QSS 多花 ~0.55s；它们通过
     parentWidget 链继承下拉框（页面内）的 QSS ⇒ `windowType()==Qt.Popup`
     直接跳过。
  **最终架构**：大 QSS 挂**页面根**（`theme.apply_window(page)`，建页时 +
  换主题时只刷当前页）+ chrome 三控件各自小 QSS + 登录窗
  `_hsf_extra_qss`（专属规则与大全表拼接，换主题不丢）+ 弹层走调色板。
  隐藏页在 `MainWindow.goto_page` 按 **`theme.generation()` 代数**懒刷新
  （换主题代数 +1；切入时代数落后才补刷，板端单页 ~0.37s、只在换主题后
  第一次进该页付一次）。
  **实测**（17 页 1445 控件全建）：桌面 1116ms → **5ms**；板端 RK3568
  11300ms → **177ms**。四主题交替/回切截图逐字节一致（无残留脏色）。
  **回归锁**：客户端 `test_hsf_regression.py` 新组 `--only=theme`
  （5 检：setStyle 零调用 / 调色板变 / 当前页卡片 QSS 跟随 / **隐藏页
  切入时补刷** / 耗时 < 1500ms）。
  **注意**：`_hist_warm_keys` 的设备属性一律 `getattr`（白盒用例用
  `object.__new__(App)`，直接 `self.vfd` 会把整段回归打断）。

- **数字量模块名：单台就叫「数字量」（2026-11-17 用户定案）**：现场只有
  一台数字量模块（.12 聚合机 2026-11-06 已拆）⇒ 不再沿用历史登记名
  「数字量2」（界面上像还有第二台）。三处同源：板端/桌面 Python cfg 的
  `digital.modules[0].name`、「数字量」；服务端缺名字兜底
  `config.py`（单台 ⇒ "数字量"，多台才 "数字量N"）；C 端
  `HSF_DIG_MODULE_NAME`（N4 锁同步改成「数字量」）。客户端数字量页下拉：
  **单台不挂序号**（`_dev_label`：多台才 "1 · xx"），缺名字兜底同上。
  **板端改 cfg 必须先停服务端**——运行中的服务端会周期性把内存配置写回
  cfg，改完不停就被旧值覆盖（2026-11-17 实锅：改了两次才生效）。

- **板端 web 与客户端必须是两个进程（2026-11-17 用户改口定案「不要合成
  一个进程，一个崩了另一个还可以用」）**：上午按「web 跟客户端一起启动」
  把板端 `hsfwebui.service` 删掉、改由客户端内嵌 web（同进程线程）；用户
  随后指出这是**故障隔离倒退**（QR 用 ctypes 调 C 桥，native 崩了会连
  客户端带 web 一起没）⇒ 板端恢复**三个独立进程**：
  ① `hsfserverpy`（6100）；
  ② **`hsfwebui.service`**（`python3 -m hsfweb --server 127.0.0.1
     --port 6100 --web-port 8080`，`Restart=always`，8080）；
  ③ `hsfclientpy`（`run_hsfclient.sh` 本地探测自动登录 + 全屏，
     **`cfg/hsf_client_py.ini` 的 `webEnabled=false`** ⇒ 客户端**不**起
     内嵌 web，避免抢 8080）。
  两边都开 systemd 自动重启：**杀 web → 客户端/服务端不受影响，web 3s
  内被拉起；杀客户端 → web 照常 200，客户端 5s 内重启并自动登录**
  （2026-11-17 板端实测）。
  **桌面不同口径**：桌面客户端仍内嵌 web（设置页能开关/改端口、客户端退
  出即释放端口、不留孤儿）——那条设计不变；**板端 kiosk 以故障隔离优先**。
  恢复单元用备份 `/root/Desktop/XinYueC/backup-20261117/hsfwebui.service`
  （`daemon-reload` + `enable --now`）。

- **QSS 给 `::indicator` 设了边框/底色 ⇒ Qt 就不画原生指示器（对勾也没
  了）（2026-11-17 用户实锅「启动设备多选：选中时没有勾，看着一直以为没
  选上」）**：Qt 文档明写「给子控件设了 border/background ⇒ 样式引擎不再
  画原生子控件」⇒ 主题 QSS 里的 `QCheckBox::indicator { border: 1px …;
  background-color: … }` 会让**勾选态只剩一个空方框**（没有对勾、也没有
  圆点）。而「补一张对勾图片」又与本项目**不引入图片资源**的口径冲突
  （板端字体/图片都可能缺）⇒ **指示器自绘**：
  `data_toolbar.CheckRow`（方框 + 对勾折线全用 QPainter 画，主题令牌取色），
  `MultiCheckCombo._open_popup` 的行控件由 QCheckBox 换成 CheckRow
  （对外 API 同名：`isChecked` / `setChecked` / `toggled` / `text` ⇒
  调用方与既有回归锁不用改）。
  **踩过的坑**：① 「把 own-QSS 清空就恢复原生勾」是**假路**——祖先 QSS 的
  `::indicator` 规则照样生效（继承链照旧）；② 四种「指示器回原生」的 QSS
  写法（`border:none` / `background:none` / 只给尺寸 / 只给尺寸+image）**全
  都无效**（实测方框内部深色/强调色像素恒为 0）；③ 主题 QSS 的 `::indicator`
  规则**对页面里的普通 QCheckBox 同样生效**（2026-11-17 板端实测定论：
  本地设置页「显示顶部标题栏/底部状态栏/左侧导航栏」三个框**状态是勾上的
  却只有空方框**）——之前"页面里是原生 Fusion、浮层里才被 styled"的判断
  **不成立**，别再照抄。⇒ **全局解法**：把主题表里给 `::indicator` 的几条
  规则**整段删掉**（保留 `QCheckBox, QRadioButton { spacing: 6px; }` 这类
  控件级规则）⇒ 全部回落**原生 Fusion 指示器**（勾选态有 ✓、单选钮有圆点），
  任何主题/板端一致；多选浮层的 CheckRow 自绘不受影响。
  **回归锁**：客户端 `test_hsf_regression.py` §14a 的 **I8a/I8b**（数浮层
  里**方框内部**的强调色像素：勾选行 >0、未勾选行 =0；只看边框不算数——
  边框也是强调色）；**A/B 实证**：把对勾绘制摘掉 ⇒ I8a 的像素数变 0 ⇒ 锁
  真的能抓住缺陷。

- **健康边沿报警的检测必须放在 model（每帧都跑），不能放在页面
  （2026-11-17 用户实锅「客户端上报警无显示，网页上有信息显示」）**：报警页
  原来的边沿检测写在 `refresh()` 里，而 `refresh` **只有当前页才跑**
  （`MainWindow._refresh_current`）⇒ 用户停在别的页面期间发生的
  「数据过期/离线/故障」边沿**根本没被采样**；网页端 `hub.feed_health`
  每帧都跑 ⇒ **网页有行、客户端空表**（现场表现就是"客户端没报警"）。
  修法：检测搬到 `AppModel.track_health_alarms()`（`bridge._apply_doc`
  收到每帧健康快照就调，与当前在哪一页无关）⇒ 结果进 model 的有界缓冲
  `health_alarm_log`；报警页建页时补放、之后按 `seq` 增量拉取
  （`AlarmPage._replay_health_alarms`）。口径：异常档 = `health >= 2`
  （告警(1) 不算，与 web 的 `proto.HEALTH_STALE` 同档）；**边沿语义**——
  同一段异常只记一行（过期→离线不重复）；恢复正常**不记行**（客户端既有
  口径，web 才记「恢复正常」行）。
  **回归锁**：客户端 `test_hsf_regression.py` §16 的 **N 段（11 检）**；
  **A/B 实证**：把 `track_health_alarms` 置空 ⇒ N 段 9 条红 ⇒ 锁是真的。

- **陈旧阈值必须随「采集周期 × 倍数」联动，温湿度尤甚（2026-11-17 板端
  实锅「板子上温湿度一直在周期报数据过期」）**：板端温湿度采集周期是 1
  分钟，而阈值写死 5000ms ⇒ **每个周期都判一次过期**（界面/日志里"数据
  过期"周期闪现，设备其实正常）。修法（服务端 `config.py`
  `apply_stale_floor()`）：`stale = max(设备基线, 周期 × 3)`（倍数 3 =
  允许连丢 2 拍），逐台（vfd/temphum/flowmeter/turbidity/digital）算。
  **同源铁律**：设备层与站级层**两层都要改**——只改一层会出现"界面显示
  过期、库里却有值"的对不上。

- **模拟量低于量程下限 = 负压，不是断线（2026-11-17 用户口径「压差计在小于
  4mA 后是负压不是断线」）**：4~20mA 变送器的 **4mA 就是零点**，零点以下是
  **真负压**（吸力）⇒ 按线性继续算出**负值**并标「低于 4.0mA（负压）」，
  **不再**夹到 0、也**不再**写"断线/欠量程"（那是误报：零点附近本来就在
  4mA 以下，现场实测活零点 3.743mA ⇒ 天天误报断线）。
  - 改动**两端同源**：`server_py/hsfsrv/config.py` 的
    `AnalogChannel.engineering()`（历史 `press_note` 与遥测 `ew` 同一处算）
    + `server/HsfServerApp.c` 的 `hsfAppAiEngineering()`（C 端唯一实现）
    + 文档示例（`common/XhsfLinkProto.h`、C 客户端 `HsfDataHistory.c` 的列宽
    样本串、网页端/客户端注释）。
  - **零点容差仍保留** 0.05mA（≈0.3%）：容差带内（实测 3.996mA 的活零点）
    按零读、**不标提示** ⇒ 正常工作时不会有任何越限提示。
  - **真断线另有出处**：Modbus 读不到 ⇒ 走**设备健康**那条线（健康档「离线」
    + 报警边沿），不靠 mA 电平猜。
  - 回归锁：服务端 `tests/test_units.py` 的
    `test_below_low_ma_is_negative_pressure`（值为负 + 说明含「负压」+ 不含
    「断线」+ 线性关系未被夹断）。
  - **[显示层收口 2026-11-18 用户核「小于 4mA 是负压」]** 上面只改了换算层，
    **两端主页压差卡的显示分支把「有 note」当成「值无效」⇒ 显示 "--"**，
    负压读数被吃掉（值算出来了却看不见）。修法：主页卡统一「**值可用就
    显示，note 单独一行说明**」（负压显示负数如 -0.016 MPa、超量程显示
    端点值 + note——与 C 客户端既有行为一致）；只有值缺失（未标定/未刷新）
    才 "--"。回归锁：客户端回归 run_client 段「压差负压 ⇒ 数值显示负值
    （-0.016）」「压差超量程 ⇒ 显示端点值 + note」；旧锁里的「断线 note」
    夹具已随口径作废替换（2026-11-17 后服务端不再产生「断线」note）。
  - **[2026-11-20 订正：**`≤0.5mA` 判断线**，`0.5~4mA` 仍按负压/欠量程]**
    用户实报「温度电流值是 0 应该判断为断线，为啥主界面显示 -50」——温度量程
    配成 -50~200℃ 时，0mA 走「欠量程钳到下限」⇒ 显示 **-50℃**，看着像个真实
    温度。口径定为：**`ma ≤ BROKEN_MA(0.5)` ⇒ 断线**（返回 `None` + 「断线
    （xx mA 回路无电流）」），**`0.5~4mA` 之间的活零点仍按负压/欠量程**
    （现场实测活零点 3.743mA，不能一起判死）。三处必须同时成立：
    ① 换算层 `AnalogChannelScale.engineering()` 判 `None`+note；
    ② AI 快照**值缺失但有 note 时仍下发 `ew<n>`（文案）+ `en<n>`（名字）**
    （否则界面只剩 `--`，连"哪里断线"都看不出来）；
    ③ 客户端 model **`e/eu/en/ew` 四键各自独立判在**（原先整段被 `e<i>` 存在
    才解析 ⇒ 断线通道**既丢名字又丢文案**，实锅「模拟量界面通道1的名字没了」），
    页面取名字不再依赖"有没有换算值"。**保护类判据一律不拿断线值判定**
    （读不到 ≠ 超温/压差过大，不许误停机）。回归锁：
    `test_units.test_broken_wire_zero_ma_returns_none` + 客户端 monitor 段名字
    显示。

- **表格/DO 控件的字号与配色：必须「控件级 QSS」，因为卡片会截断祖先级联
  （2026-11-17 用户「图例/表格文字太小」「输出点开关颜色区别太小」「客户端
  表格美化成 web 端这种样子」）**：本轮三条实测根因，都是 Qt 样式表/字体
  的作用域陷阱：
  ① **祖先样式表不越过「自带样式表的容器」**：大 QSS 挂在**页面**上，而
     卡片（`QFrame#HsfCard`）自带样式表 ⇒ **卡片里的控件拿不到页级规则**
     ⇒ DO 按钮的 `QPushButton:checked`（通态浅蓝底）失效，被画成 Fusion 的
     灰面 ⇒ 「通」与「断」几乎看不出差别（用户实报）。⇒ **卡片内的控件
     必须自带样式表**（表格 `DataTable._apply_table_font`、DO 网格
     `ToggleGrid` → `theme.do_toggle_qss()`，逐条对齐网页端 `theme.css`）。
  ② **`setFont(QFont())` 不是"回到 app 字号"**：给 QTableWidget 设空 QFont，
     Qt 解析成**视图缺省 9pt**（实测：app 15pt 时表内仍 9pt）⇒ 表内文字
     凭空小一档、且不跟全局缩放。⇒ 表内/表头一律显式设**像素**字号
     （`theme.font_px(13)` ≈20px @1.5，与网页端 `--font-app` 同档）。
  ③ **表头字号会被 QSS 重解析**：QSS 里给 `QHeaderView::section` 设了
     `font-weight` ⇒ Qt **重新解析表头字体** ⇒ 没写 font-size 时回落
     视图缺省 9pt（表头比表内还小）。⇒ 表头也写显式 font-size（fz13）。
  ④ **页面被塞进 QStackedWidget 时表头字号被打回 9pt**（Qt 重新解析整棵
     子树的字体）⇒ `DataTable` 监听自己的 `ParentChange`，父一变就把字号
     再刷一遍。
  ⑤ **宽表（历史 16~19 列）不要再"缩字号硬塞"**：1680px 视口塞 16 列只
     能把字缩到 ~12px（用户实报「数据界面的表格文字太小」）⇒ 改成**网页
     端口径**：**列宽跟着内容走**（`DataTable.set_content_floors(True)`）
     ⇒ 装不下就**横向滚动**，字不被省略号截断；表内字号保持界面同字号。
     （回归锁同批改口径：旧的「16 列一次装下」断言按新口径改为
     「字号不缩 + 按内容给列宽 + 没有文字被截断」。）
  ⑥ **图表里的字是全客户端唯一不跟缩放走的**：图例/轴标签/悬停气泡原来写死
     11/12px ⇒ 界面缩放 150% 时图例仍是最小号的字（用户实报「图例文字太
     小」）⇒ 一律走 `theme.font_px()`，图例色块/偏移/绘图区边距按同一字号推
     （边距也随之缩放，否则标签会压进绘图区）。
  **回归锁**：客户端 `test_hsf_regression.py` §14a I8a/I8b（多选浮层对勾）、
  §16 N 段（健康边沿报警）、历史表三断言（不缩字号/按内容给列宽/不截字）、
  趋势图 C2（锚点误差按「1 像素」算——锚点取的是整数像素，1e-6 的容差
  本来就是靠运气过的）。

- **报警页的数据源：服务端 alarms 表 + 0x011A（2026-11-17 用户实报「web
  端报警有数据，客户端报警视图没数据」）**：**先对比两端取数路径再动手**，
  实测结论如下（此前两端都靠**各自进程内存**记边沿）：
  | | 网页端 | 桌面客户端（改前） |
  |---|---|---|
  | 边沿检测 | hub 常驻订阅健康主题，每帧 `feed_health` | bridge 收到健康帧时 `track_health_alarms` |
  | 记录范围 | **网页进程活着多久**（板端=开机起） | **客户端进程活着多久**（重启/升级即清零） |
  | 阀告警 | 也进报警表 | 只在阀页显示 |
  | 服务端 alarms 表 | 不读 | 不读 |
  ⇒ 两端**进程寿命不同** ⇒ 客户端重启后历史全丢 ⇒ 用户看到"web 有、客户端
  没有"。**修法（单一数据源）**：服务端把**健康边沿**（正常→异常记一条、
  异常→正常记「已恢复正常」、**同一段异常不重复**：过期→离线只记一条）写进
  `alarms` 表（`kind=health`、`source=设备名`、`text=健康档中文`、
  `dev=设备类别`）；新增 **0x011A 报警历史分页**（与 0x0118/0x0119 同形）；
  客户端报警页改为**向服务端取数**（本地边沿只作实时/老服务端兜底，按
  「设备+类型+状态」去重）。
  **四个必踩的坑**：
  ① **协议常量撞名**：客户端 proto 已有 `KEY_ALARM_COUNT="ac"`（**阀告警帧**
     的键，model.take_alarms 认它）⇒ 新命令的键必须用 `ALARM_KEY_*` 前缀，
     否则同名覆盖 ⇒ **阀告警帧再也认不出来**（阀页告警静默失效）。
  ② **服务端 ts 必须是墙钟**：`insert_alarm` 不传 `now_ms`（传了会写成
     **单调钟**毫秒 ⇒ 时间列显示 1970 那种假时刻）。
  ③ **客户端的补放游标要跟着模型回退**：重连时 `clear_health_alarm_log()`
     把模型 seq 归零，页面若还揣着旧游标（比如 57）⇒ 之后的新边沿要等模型
     seq 超过 57 才补进表 ⇒ **中间所有边沿都丢** ⇒ 游标必须随模型回退。
  ④ **排序要"同毫秒按插入顺序倒序"**：列表先摆成新→旧、再按 ts **稳定**
     降序排 ⇒ 否则同一毫秒的两行里旧的排前面（"最新在首行"不成立）。
  **回归锁**：服务端 `tests/test_alarm_history.py`（健康边沿入库 3 检 +
  0x011A 取数/键集/total/count/关键字过滤）；客户端 §16 的 **O 段 7 检**
  （向服务端要数、服务端行渲染三列、同源去重、老服务端兜底）。
  [待办] C 服务端 0x011A 实现暂缓（板端跑 C 服务端时要补，
  否则 web/客户端对 C 上游回落本地）。

- **工作树被「整段覆盖」的事故与还原配方（2026-11-17 续会话实锅）**：接手一个
  会话时**客户端总回归直接崩**（`§8 check_header_sort → KeyError: 9`）⇒ 根因
  不是回归红，而是 **`pages_other.py` 的 `HistoryPage` 有一段被别处的内容整段
  覆盖**：
  - 本地缓存的 `refresh` 被换成 **AlarmPage 的 refresh**（0x011A 那份）；
  - `_rpm_text`/`visible_columns`/`_rebuild`/`_header_dblclicked`/`set_view`/
    `_chart_*`/`_export_*` 等 **52 个方法整段消失**；
  - 换来的是 **LineChartDialog 的图表绘制代码**，且里面调 `QDialog.xxx(self,…)`
    —— `HistoryPage` 是 `QWidget` ⇒ `TypeError` ⇒ **历史页根本建不出来**
    （`goto_page` 把它吞成"页面构建失败" ⇒ 测试侧表现为 `KeyError: 9`）。
  **排查手法（可复用）**：① 症状是"某页建不出来"时，先看**页面自己有没有
  引用不存在的方法**（AST 扫 `self.x` 与类内定义/赋值的差集 ⇒ 一眼看出缺
  哪些方法）；② 与**上一份已知良好副本**做**方法清单差集**（本例用
  `git stash@{0}` 里的未提交版本；也可用 HEAD）—— 差集里"整类消失"就是
  被覆盖；③ 逐个文件扫（全仓 `pages_*.py`）确认**只有这一处**。
  **还原口径**：以 `git stash@{0}` 的同名文件为底，把被覆盖的区段**按段落
  边界**切回来（不是整文件覆盖——工作树里有更新的改动），再把**工作树里
  更新、但随覆盖一起丢了**的那部分（新协议通道、新导出实现）**按回归锁
  重写**（回归锁是契约，AGENTS.md 的记载也是契约）。
  **还原后必须做的三件事**：① 补齐**悬空引用**（本页的 `chartReady/
  chartFailed` 两个信号当时只声明未实现 ⇒ 补 `_on_chart_ready/_on_chart_failed`
  并让图表取数**不再共用**表格页的 `_inflight`+`_pending_kind`）；② 按
  **回归锁**补回被覆盖掉的新实现（历史页导出＝keyset 分页 + 流式不限行数，
  见上文「导出改 keyset 分页」段）；③ 补**告警页**的两处收口（见下条）。
  **教训**：改大文件前先留副本（`git stash` 或 `.zcode/tmp/` 拷一份）；写完
  大段代码后**立刻做一次"方法清单差集 + 悬空引用扫描"**——这类事故在
  编译期与运行期都**不报错**（Python 动态属性），只有跑回归才暴露，且
  症状（KeyError/AttributeError）离病因很远。

- **报警页（客户端）收口成「服务端单一数据源」后的三个实现要点（2026-11-17）**：
  报警历史改为向服务端要（0x011A）后，客户端侧**本地副本整体移除**
  （`bridge.PERSISTENT_TOPICS` 清空、`autoAlarm` 信号没了、model 的
  `auto_alarm_log`/`health_alarm_log` 缓冲没了、页面的 `_on_auto_alarm` 没了）
  ⇒ ① **列表本体叫 `_server_rows`**，`_rows` / `_records` 必须是
  **property 别名**（读**写**都代理到它）：老代码/老测试会写
  `page._records = []` 来"清空记录"，普通属性只是**另起一个名字** ⇒ 页面
  照旧用 `_server_rows` ⇒ 清空没生效、行数对不上（本轮实测"共 29 条"而
  期望 25 条＝多出前一段没清掉的 4 条）。② **排序要显式做**：`_all_records()`
  把列表**先 reverse（摆成新→旧）再按 ts 稳定降序排** ⇒ 同毫秒的两行里
  后到的那行排前面（"最新在首行"才成立）；服务端每批**批内升序**（取页后
  reversed）⇒ **每批还要反转**才全局降序。③ 服务端行的 **`dev`（设备类别）
  键对所有 kind 生效**：原来只对 `kind=health` 生效 ⇒ 带 dev 的自动/阀告警
  行「类型」列仍显示笼统的"自动运行"（现场看报警表分不出哪台设备）。
  **回归锁随之换口径**：客户端 §16 的 **L/M 段**（2026-11-13 加的"本地自动
  告警链路"断言）随功能一起作废 ⇒ 改成断言**新口径**（不订阅、不缓冲、
  推送被忽略、不崩）；`N 段`（model 健康边沿缓冲）同样作废 ⇒ 由 **O 段**
  （0x011A 取数 + 三列渲染 + 同源去重 + 老服务端兜底）接管。

- **light（亮色·工业白）主题整体对齐网页端的美化样式（2026-11-17 用户
  「亮色工业白这个主题整体对齐 web 端的美化样式，每个界面都看看」）**：
  逐页对照口径 = `verify/webui/ref_sweep.py`（桌面端 16 页整窗截图）与
  `verify/webui/web_sweep_edge.py`（网页端 16 页），再配
  `verify/webui/compare_palette.py`（**量化配色直方图**逐页比差集）。
  **改了四处**：
  ① **表头 own-QSS**（`page_base.DataTable._apply_table_font`）：把网页端
     `.table th` 那套**直接挂在 QHeaderView 自己身上**（底 #EFF3F7、文字
     #1A1A1A、粗体、右缘线 #E4E8EC、下边框 #E4E8EC、像素字号、6px 8px）。
     [机制：两头落空] 祖先（页面大 QSS）的 `QHeaderView::section` 规则
     **到不了** Card 内的 QHeaderView（Card 自带样式表 ⇒ 级联被截断）；
     而 **QTableWidget 自身**样式表里的同名规则又**不会**传给 QHeaderView
     ⇒ 表头回落 Fusion 默认渐变 + #505152 深灰竖线（网页端是平涂 #EFF3F7 +
     #E4E8EC 浅线）。
  ② **进栈/换主题后重刷表格样式**（`DataTable.refresh_style()` +
     `MainWindow._refresh_table_styles`）：页面被塞进 QStackedWidget（或重新
     挂大 QSS）时 Qt **重新解析**子树样式 ⇒ 表头 own-QSS 的 `border-right`
     被丢掉（竖线回落 #505152）⇒ **重刷一次即恢复**（与"字号被打回 9pt"
     同款处置）。
  ③ **按钮字色**（`theme.py`）：常态字色 accent_line（蓝）→ **正文色**
     （网页端 `.btn` 的 `color: var(--text)`）；"可点"的表达留给 hover/按下
     换底 + accent 边框（网页端同款）。
  ④ **图表网格线色**（`pages_monitor` / `pages_other` 的 `GRID_COLOR`）：
     #E3E6EB → **#D5DBE2**（= 网页端 `--chart-grid`）⇒ 原色太浅、白底上等于
     没有网格（两端**结构**其实一样：都有横网格 + X 时刻刻度 + Y 量程刻度）。
  **逐页结论**：量化配色对比后，16 页里 **10 页配色集合完全一致**，余 6 页
  （主菜单/设备/数据三个子菜单页、电动球阀、远端设置）只差 1~2% 的**抗锯齿
  灰桶**（#E0E0E0 桶，精确色一个都没有）⇒ 视觉上已对齐。
  **取证纪律（本轮又踩了一次）**：① **别用肉眼判颜色**——50% 缩略图里
  "黑字"看着像蓝字，裁剪放大才发现两端都是黑字；颜色一律**像素取证**
  （读 PNG 的具体像素值再比）。② **配色对比要量化**（每通道 16 级归桶），
  否则抗锯齿像素会把结论带偏。③ **表头这类"祖先规则到不了"的控件必须
  own-QSS**，且 own-QSS 挂上后祖先规则不再级联 ⇒ 该控件需要的属性要
  **写全**（否则字号回落视图缺省 9pt）。

- **Web 设置页：网页服务「跨进程探测 / 手动拉起 / 关闭时是否同步关闭」（2026-11-17
  用户定案「Web 设置网页服务支持探测不同进程下 web 服务器是否启动，不启动
  可以用设置拉起，并可以设置客户端关闭的时候是否要同步关闭 web 端」）**：
  ① **跨进程探测**（`webserver.probe(port)`）：GET 网页端的无鉴权探活
     `http://127.0.0.1:<port>/api/ping` ⇒ 认 `server == "hsf-webui"`
     （**别只看端口在不在**：那会把别的 HTTP 服务误认成网页端）。
     状态行分**三态**：**本客户端启动的独立进程**（本客户端拉的子进程）/
     **其它进程**（用户手工起的 ⇒ 本客户端既不重复拉起、也**不杀**它）/
     **已停止**。
     [own 的口径] `own` = 是不是**本客户端拉起**的那个 ⇒ 靠**自己记的子
     进程**判，不靠查端口占用进程的 PID ⇒ 跨平台一致、也不需要额外权限。
     [为什么要跨进程] 网页服务不一定��本客户端起的：用户手工
     `python -m hsfweb`、另一个客户端实例内嵌的、上一版没退干净的 ⇒
     设置页要能回答"有没有在跑、是不是我认领的那个"。
  ② **手动拉起/停止**：Web 设置页加「刷新状态」「启动网页服务」「停止网页服务」
     三个按钮（探测在**后台线程**做，HTTP 超时 1.2s，不许卡 UI）；
     **端口已被别的网页端占着 ⇒ 拒绝重复拉起**并如实说明（盲目拉起来必
     bind 失败）。
  ③ **形态：一律独立进程**（2026-11-17 用户定案「**不要内嵌**，客户端
     拉起也是独立的进程」）⇒ 网页端**永远**是 `python -m hsfweb …` 的
     **独立子进程**（日志落 `cfg/hsf_web.log` + `cfg/hsf_web_err.log`，
     **两个文件**：同文件开两次在本框架下会失败 ⇒ 重定向句柄为空）；
     同进程线程内嵌（`hsfweb.embed.start_in_thread`）**已从客户端侧移除**
     （客户端只是网页端的**配置入口**，两套生命周期要能分开）。
     **关闭策略**（新设置键 `webStopWithClient`，缺省**开** = 同步关闭）：
     · **开** ⇒ 客户端退出时**终止**那个子进程（网页端随之停、端口释放）；
     · **关** ⇒ 客户端退出时**不终止**它 ⇒ 网页端留在后台继续服务
       （这才是"不同步关闭"的用户价值）。
     [代价要说明白] 客户端被**强杀** ⇒ 子进程变孤儿、占着端口（独立进程
     形态的固有代价）⇒ 靠 **pid 记账 + 残留认领**兜底：
     · `webserver` 把子进程 pid 落到 `cfg/hsf_web.pid`（可用环境变量
       `HSF_WEB_PID_FILE` 覆盖）；
     · `webserver.stale_pid()` / `pid_alive(pid)`：读 pid 文件并**探活**
       —— [坑] **Windows 上 `os.kill(pid, 0)` 会直接终止该进程**（Python 把它
       映射成 TerminateProcess，signal 值被忽略）⇒ 拿它"探活"等于**误杀**
       （pid 可能已被系统复用）⇒ Windows 走 ctypes 的
       OpenProcess + GetExitCodeProcess，POSIX 才用 `os.kill(pid, 0)`；
     · `webserver.stop_stale()`：认领并停掉**上次残留**的那个（**只认领自己
       pid 文件里记着的**，别人的网页端不动）；`start()` 之前自动先调一次。
     ⇒ 效果：客户端被强杀后，下次启动会**先清残留再拉起**，不会卡在
     "端口被占"；回归段被父进程 kill 也不会留端口占用。
  ④ **退出路径**（`__main__.py`）：`_start_web_server` 按开关选形态；
     `_teardown` 只在"同步关闭"时 `webserver.stop()`。
  **回归锁**：客户端 `test_hsf_regression.py` **§36**（21 检，段组 `webset`）：
  未监听端口探不到 / 真起一次**独立进程**网页端后探得到且 own=True / 停掉又
  探不到 / 端口被别人占时拒绝拉起 / 三态文案 / 设置键读写 / 页面四个控件齐全；
  另 §22E 的**记录桩**按新签名改（`start()` 不再有形态参数）+ 断言"取消同步
  关闭也照样起服（形态恒为独立进程）"；§36 的 pid 记账 5 检（起服有记账 /
  记账里的 pid 活着 / 停掉记账清掉 / **残留被认领并清理**）。
  [测试口径] 真起一次内嵌网页端（**跨进程**行为，假对象测不到）；上游故意
  指**死端口 6199**（网页端绑端口与连上游是两件事）⇒ 既能绑上 18099，
  又**不抢**回归自己的 root 会话。

- **「怎么把网页端拉起来」收口到网页端自己（2026-11-17 用户「网页端自己也可以按
  客户端拉起 = 独立进程的同一套做法收口」）**：此前"谁拼 `python -m hsfweb …`
  这条命令行、谁管 pid 与日志"**三边各写一份**：桌面客户端自己拼一套、C 端
  `HsfWebServer.c` 自己拼一套 ⇒ 参数一改就漂（而且是**运行期**才炸）。
  ⇒ **新增 `webui_py/hsfweb/launcher.py`**（网页端自己的启动器，**启动方式
  的单一事实源**）：
  · `build_argv(server, port, web_port, bind, user, password, static,
    data_dir, python)` ⇒ 命令行**唯一一份**（参数与网页端 `__main__.py` 的
    argparse 一一对应）；
  · `start_process(...)` ⇒ 拉独立子进程（cwd/PYTHONPATH 按网页端包目录、
    stdout/stderr **两个文件**）、`stop_process`；
  · `pid_file_path/write_pid/clear_pid/read_pid/stale_pid/stop_stale` +
    `pid_alive(pid)` ⇒ **pid 记账与残留认领**归网页端（[坑] **Windows 上
    `os.kill(pid, 0)` 会直接终止该进程** ⇒ 探活必须走 ctypes
    OpenProcess+GetExitCodeProcess，POSIX 才用 `os.kill(pid, 0)`）。
  ⇒ 客户端 `webserver.py` 改成**只给参数**（不再拼命令行、不再自己管 pid）；
  C 端 `HsfWebServer.c` 本来就是 `-m hsfweb` 独立进程，参数名一致 ⇒ 只在
  注释里指向 launcher 作为事实源。
  ⇒ `hsfweb/embed.py`（内嵌线程形态）**降级为遗留**：头部标了 LEGACY，
    只留给测试/嵌入式场景（客户端泄漏门仍在用），**不再是任何产品路径**。
  ⇒ 网页端 `/api/version` 增报 `pid` 与 `mode: "standalone"`（自报进程
    形态，运维/客户端一眼可辨）。
  **回归锁**：客户端 §36 增两条（**H1** launcher 能产出独立进程命令行 /
    **H2** launcher 拼出的参数**网页端 argparse 全认** ⇒ 参数漂移在回归
    期就红，不必等运行期）；另 §36 的 G 组 pid 记账 5 检里"临时账本"要用
    **launcher 的 pid 文件名**（客户端的 `stop_stale` 已转调 launcher ⇒
    文件名不一致就认领不到）。

- **网页端「独立进程形态」实测抓出的三个真缺陷（2026-11-17 用户「测试过了吗，
  如果 web 没启动，启动客户端的时候会拉起 web 吗」）**：用户追问得对 —— 我先前
  只验了"客户端起 ⇒ web 跟着起"这一条，**没跑完整矩阵**，一跑就抓出三个
  真实缺陷（都发生在**独立进程形态**下，因为内嵌形态根本没有这些时序）：
  ① **探测结果乱序 ⇒ 状态行回退**：探测是异步的（后台线程 + 信号回 UI
     线程）⇒ 两次探测的结果可能**后发先至** ⇒ 旧结果（"没起"）把新结果
     （"在跑"）覆盖 ⇒ 状态行显示"已停止"而网页端明明在跑。
     **修法**：每次探测带**递增序号**（`webProbeDone` 改传 `(seq, info)`），
     **只认最新一次**的结果。
  ② **并发 start/restart 会互杀**：`start()` 开头那句 `stop_stale()`（清上次
     残留）读到的是**另一个并发 start 刚起的进程**的 pid ⇒ 把它杀了 ⇒
     "起来又被停"、网页端实际挂了（复现：改端口框的 800ms 去抖 ⇒ restart，
     与用户点「启动」并发）。**修法**：① 残留清理**从 `start()` 里挪出去**，
     只在**客户端启动时**调一次（那时才真有"上次残留"）；② 起停操作**模块级
     互斥锁串行化**。
  ③ **状态行"不带探测结果刷新"在新形态下必然错**：独立进程没有"内嵌句柄"
     可看 ⇒ 任何 `_refresh_web_status()`（无参）只会显示"已停止"。
     **修法**：状态行**一律走后台探测**刷新（起停完成、端口去抖提交两条路
     径都改）；另加两条 UX：起服期间状态行**立即**显示「正在启动…」（网页端
     起来要 1~2s，否则用户以为没起来）、**启动被拒的原因要留在页内**
     （原来只在底部状态栏；页内只显示"运行中（其它进程）"⇒ 用户不知道自
     己的启动被拒 ⇒ 改成"运行中（其它进程）…　※ 启动被拒：…"）。
  **实测矩阵（脚本 `.zcode/tmp/check_web_follow.py`，走真实页面槽）**：
  web 没跑 ⇒ 点「启动」⇒ 独立进程起来 + 状态行"运行中（本客户端启动的独立
  进程）· http://…"；点「停止」⇒ 端口释放；端口被**别的进程**占着 ⇒ 拒绝
  拉起 + 页内说明原因 + 不误杀；「刷新状态」能认出"其它进程"。
  **要记住的测试纪律**：**异步链路的断言要"等待式"**（等信号/状态到位，
  最多 N 秒），固定 sleep 1.2s 会把"还没到"误判成"失败"（本轮就误判过
  一次，报成产品 bug）。

- **下拉框的箭头消失（2026-11-17 用户实报「下拉框的箭头怎没了」）**：与
  QCheckBox 的 `::indicator` **同款**（Qt 样式引擎的"接管即不画原生件"），
  但这次有**两层**机制，逐层取证：
  ① **QSS 一旦给 `::drop-down` 设了 border/background，Qt 就不画原生箭头**
     ⇒ 只剩一条竖分隔线（用户截图里就是这样）。
     [网页端口径] `appearance:none` + **SVG 自绘**的 8×5 小三角（色
     `#5A6572`），且 2026-11-13 第 4 轮订正时明确"参考端**没有**那条分隔线、
     箭头直接画在框内右侧" ⇒ 桌面端应对齐：去掉分隔线 + 框内右侧小三角。
  ② **只要 QSS 里存在任何 `QComboBox:focus` 规则，样式引擎重 polish 时就会
     让**子控件的 image 失效** ⇒ 连自绘三角也会消失。两种伪状态写法
     （`QComboBox:focus::down-arrow` / `QComboBox::down-arrow:focus`）
     都**救不回来**（实测）。
     **像素取证**（别靠肉眼）：读下拉框**右侧 24px** 里的深色像素 ⇒
     箭头在 ⇒ 有 #5A6572 一片（≥20 px）；箭头丢 ⇒ 只剩边框色 #1A66D0。
  ⇒ **治法**：① `::drop-down` 只留宽度（去分隔线、去背景）；② 箭头用
    **运行时生成的 8×5 实心三角 PNG**（`theme._arrow_png()`：QImage 绘制
    **不需要 QApplication** ⇒ 任何阶段生成主题 QSS 都安全；缓存到
    `cfg/ui_icons/`，文件名带颜色 ⇒ 换主题自动换色；**本项目不引入图片
    资源**这条纪律不变）；③ **去掉 `QComboBox:focus` 规则**（焦点反馈改由
    `:hover` 的 accent 边框承担）。生成失败 ⇒ ① 仍成立 ⇒ 原生箭头会回来
    （有兜底）。
  **回归锁**：客户端 §22b 两条（**箭头区有 ≥20 个 #5A6572 像素** / QSS 里
  不许有 `QComboBox:focus` 规则）。
  **待办（同款坑，别再踩）**：框架层（XGui / C 端客户端）若给下拉框写了
  `::drop-down` 或 `::up-button` 的 border/background ⇒ 同样会丢原生箭头；
  且 C 端 QSS 若有 `:focus` 规则 ⇒ 换成 image 的箭头也会丢（C 端暂未启用
  皮肤开关，动之前先按本条取证）。

- **变频器故障复位透传到驱动器（2026-11-17 用户口径「变频器报警后，我需要
  按下复位后可以对变频器复位报警信息，比如过载报警，不向变频器发送复位，
  变频器他不让启动」；范围=Python 服务端/客户端 + web，C 端暂不做）**：
  [现状与缺口] 变频器页「故障复位」按钮（OP_RESET_FAULT=8）**本来就会**写
  0x2000=7（M512 故障复位，真机实测"复位后故障码=0"）并清服务端锁存；缺的是
  **主页的「复位」按钮（op=4 AUTO_OP_RESET）只清服务端两把锁、从不给变频器
  发任何东西** ⇒ 变频器自己锁在故障里（如过载），按完复位再按启动被变频器
  拒绝。
  [修法] 服务端新增 `App._auto_reset_and_vfd()`：`ctl.reset()` 之后，若变频器
  正处于故障（faulted / fault_code 非 0）⇒ `vfd.apply_control(OP_RESET_FAULT)`
  （与变频器页同一条命令，走设备级命令锁串行）；**无故障不写总线**（阀超时类
  报警不需要动变频器）；下发失败只记日志**不拦**（复位的清锁存部分照常生效
  ——人已确认过现场，不能因总线抖动把锁存留在锁着的状态）。op=4 分支改为调
  它。客户端/web 的复位按钮**本来就发 op=4** ⇒ 零改动即生效（只更新了复位
  成功文案与 web 按钮 title 如实说明）。
  [协议事实] M512 故障复位 = 写 0x2000=7（docs 151/152 断言通过；命令字是
  电平语义不是边沿，无两步归位——ATV61/CiA402 才需要两步序列）。
  [回归锁] `test_protect.py` D3c/D3d + `fake_devices.py`：**FakeVfd 补命令 7
  =清驱动器故障**（真机口径）⇒ 断言"复位后命令 7 已写到变频器（commands
  记录）+ 驱动器故障已清 + 锁存已解除"；D3d 断言"变频器无故障 ⇒ 不发命令"。
  [测试两个坑] ① `business.vfd` 是 FakeVfd（不是虚拟模型），注入
  `fault_code` 直接改属性即可，但断言驱动器效应要看 `commands` 记录与
  FakeVfd 自己的清故障；② `_watch_vfd_fault` 是**边沿检测**——D1 已见过
  故障后再置 faulted **不产生新边沿** ⇒ 重新锁存要用
  `ctl.raise_fault(..., stop_devices=False, enqueue_alarm=False)` 直注。
  [待办（用户明确暂缓）] C 服务端 `HsfServerAutoMode` 的 op=4 同款透传
  （板端跑的是 C 服务端，上板前必须补齐，否则板端复现同一问题）。

- **变频器参数设置三端落地（0x011B，2026-11-18 用户口径「显示的必须是变频器
  里实际的值，不是我以为写进去的值」；C 端只占协议号，服务端/客户端实现
  用户明确暂缓）**：0x011B `CMD_VFD_PARAMS` 远程读/写变频器（M512）**参数**，
  区别于 0x0101 运行控制：参数是**停机维护动作**，三道闸——读=登录（
  PERM_VIEW）/ 写=管理员（PERM_ACCOUNT_ADMIN，0x0117 同款闸与拒绝文案）；
  **RunLocked**（0x3000=1/2 运行中）拒写**不发总线帧**，文案
  「变频器运行中禁止写参数（请先停止）」**三端同款**（客户端按文案匹配展示，
  漂移会让现场看到两套说法）；变频器未装配/离线一律包络「变频器未就绪」。
  **参数集第一期 = P1 电机铭牌组 0xF100~0xF105**（电机类型/额定功率 kW×10/
  额定电压 V/额定电流 A×100/额定频率 Hz×10 待实机标定/额定转速 rpm）；
  **单一事实源 = server_py proto.py 的 PARAM_DEFS**，客户端（client_py
  proto.py）与 webui（webui_py proto.py）**双镜像 + 回归锁逐值对账**（客户端
  §V9）；表值来源 = docs/M512_511_512_513寄存器映射表.md §9.5 与
  XVfdM512.c s_m512Params[]（P1.04 小数位手册标「待实机标定」，标定后三端
  同步改 scale）。
  **契约**：请求 {op:0=读/1=写, values:{addr 十进制字符串: raw}}；应答
  {ok, err?, params:[{code,label,addr,raw,value,unit,writable,runLocked,
  applied?}], vfdFaulted, runState}——**params[].value 一律是服务端从驱动器
  回读的工程值**（写后重新读驱动器，重试 ≤3×0.2s 跨不应答窗口），**绝不把
  提交值当成功值**（总线 ACK ≠ 写进去了：运行锁定下驱动器会 ACK 但不生效）；
  applied 仅写操作项携带（回读值==写入值才 True）。
  **三端落点**：服务端 `app.py` 的 `_h_vfd_params`/`_vfd_params_write`（values
  键 int() 归一 + fail-closed）；客户端远端设置**「变频器参数」卡**（
  `pages_other.py`，运行保护卡之后）+ `bridge.vfd_params`；web `page14_remote.js`
  同名区块 + `hub.vfd_params` + `/api/vfd/params`（GET/POST）。**显示只认回读
  值**；写成功文案「已写入并回读确认：N 项」。
  **模拟器**：`virtualdev.VirtualVfd` 补 0xF100~0xF105（缺省 raw
  0/150/380/3000/500/1440；**运行中写不应用**=模拟真机拒写 ⇒ 服务端回读见
  不一致 applied=False，test_vfd_params C3 锁住——没有它「回读不撒谎」在
  sim 里永远测不到）。
  **三个坑（本轮实锅）**：① **0xF100=61696 不是 61440**（61440 是 0xF000）——
  报告/注释两处误算已修；十六进制↔十进制**用工具算别心算**。② values 键必须
  十进制**字符串**（BSON 文档键只能是 cstring，int 键上不了线）。③ **自洽
  测试抓不到「地址错」**——FakeVfd 与虚拟模型用同一组地址 ⇒ 地址写错了测试
  照样绿 ⇒ 地址必须对着手册 §9.5 与 s_m512Params[] **逐项核**（本轮主线程核
  过；test_vfd_params D 段把表钉进回归防漂移）。
  **回归锁**：`test_vfd_params.py` **44/0**（A 读面键面契约/B2 RunLocked 拒写
  不发总线帧/B3 回读不撒谎/D 参数表镜像）+ 客户端 §V1~V9 + webui_check
  `_vfd_params_static`/`sec_vfd_params`。**门禁终值**：服务端套件 **25/25 组**、
  客户端总回归 **1726/0**、webui_check **398/0**、泄漏门 **13/0**、
  `--only=remote` **133/0**、test_protect **68/0**。实施记录：
  `docs/HSF变频器参数设置_三端实施记录_20261118.md`。
  **战役同批**：① 网页端报警页主数据源切 **0x011A**（`page11_alarm.js`：
  hub.alarm_page + /api/alarms/page，老服务端 fail-open 回落本地边沿；
  webui_check `sec_alarm_server` 双上游绿）——见上方 0x011A 段的待办行；
  ② 2026-11-17 变频器故障复位透传收尾回归锁（FakeVfd 补命令 7 +
  test_protect D3c/D3d）；③ 10 项历史需求审计 **9 ok / 1 deferred**（C 服务端
  op=4 透传、C 服务端 0x011A、C 客户端新功能均用户暂缓；C 客户端主页转速
  已顺手改）。

- **Python 三端联调深检战役（2026-11-18 用户口径「整个python的客户端 服务端，web一起
  整体联调测试，修复隐藏的bug，性能问题，内存泄漏等等」+「可以用工作流闭环下任务」）**：
  工作流工具（dynamic-workflows/CreateWorkflow）本会话**不可用** ⇒ 按工作流的
  **形式手工闭环**：分阶段（清点需求 → 三端并改 → 独立评审 → 门禁 → 压测 →
  文档 → 真机验收）→ 并行子代理（实现/评审各一）→ 每阶段**可执行门禁** ⇒
  红灯即修 → 收尾审计 → 文档 → **真开界面验收**。门禁入口 `verify/wf/*.bat`
  （server_suite / client_reg / webui_check / client_leak / params_e2e /
  ref_sweep 六个一键脚本）+ `verify/soak_combined.py`（三端混合负载压测）。
  **本轮抓出并修掉的真缺陷（按族）**：
  ① **协议一致性（两端同表却语义不同）**：`runState` 是 **0x3000 原始字**
     （1 正转 / 2 反转 / 3 停机），两端却按 `XVfdRunState` 枚举渲染 ⇒ 运行中
     显示「停机」、停机显示「正转运行」（与 RunLocked 拒写文案自相矛盾）⇒
     两端表改 0x3000 口径（`proto.VFD_RUN_WORD_TEXT` / web `RUN_STATE_TEXT`）+
     proto 注释订正；压差过高告警行 `kind=analog` 在两端 `SERVER_KIND_ROWS`
     缺失 ⇒ 兜底成「自动运行」且**告警原文被丢** ⇒ 补 analog/digital 行，
     且保护类行的「设备」列**放告警原文**（含压差值与「已停止设备」）。
  ② **网页端假成功 / 静默截断**：写超时 10s < 服务端最坏 17s ⇒ 改 35s（否则
     客户端看到失败但写其实已下发）；`raw` 传浮点被 `int()` 静默变成另一个
     合法值 ⇒ web 层与服务端同口径 fail-closed 拒非整数；ok 分支不说哪几项
     失败（deny-write 驱动器 ACK 但不落）⇒ 补失败清单；双击「应用」= 两批写
     并发下发 ⇒ 在途闸。
  ③ **客户端显示层吞数**：负压读数（带 note）被渲染成 "--" ⇒ 统一「**值可用
     就显示、note 单独一行**」（负压显示负数、超量程显示负值 + 说明）。
  ④ **表格 / 下拉的 QSS 截断**：祖先页 QSS 被 Card 与 QTableWidget **自身** QSS
     截断 ⇒ 表头字号挂不到 ⇒ 样式**直接挂 QHeaderView**（own-QSS）+ 新增
     `refresh_style()`（重新挂载 / 换主题后补刷）；给 `::drop-down` 设边框 /
     底色会**吃掉原生箭头**、页面里存在 `:focus` 规则时 `image:` 失效 ⇒ 改为
     **运行时生成箭头 PNG**（`theme._arrow_png`）+ 去掉 `:focus` 规则。
  ⑤ **服务端 op=4 复位**：自动运行中复位会**打断正在跑的序列** ⇒ 改为拒绝
     （「运行中不可复位（请先停止自动运行）」）+ 变频器有故障时透传 **0x2000=7**
     清驱动器故障（C 端同款暂缓）；**我自造的回归**：`ok_r/note_r` 只在 op=4
     分支赋值 ⇒ **所有自动控制命令 500**（webui_check 11 条红）⇒ 提到分派链
     之前初始化。
  ⑥ **会话 / 循环健壮性**：`0x011A`/`0x011B` 纳入 `CONCURRENT_READ_CODES`
     （历史分页式慢查询不串行压住控制命令）；web `hub._mode_poll_loop` 单次
     异常 ⇒ 整个轮询循环**永久死掉** ⇒ 包 try/except；网页端 `stop_stale` 原来
     **盲杀 pid 文件里的 pid**（pid 复用 ⇒ 误杀无关进程；属主还活着 ⇒ 拆掉
     别人客户端的合法网页服务）⇒ 改**三重闸**（进程存活 + 命令行含 hsfweb +
     属主已死）；pid 记账升级 JSON `{pid,parent}`（旧纯数字格式兼容）；
     Windows 探活不能用 `os.kill(pid,0)`（那会**直接杀进程**）⇒ ctypes
     `GetExitCodeProcess`。
  **门禁终值（2026-11-18）**：服务端套件 **25/25 组**、客户端总回归
  **1728/0**、webui_check **398/0**（C6100 192 + PY6200 206）、客户端泄漏门
  **13/0**。
  **压测**：`verify/soak_combined.py` 8 分钟三端混合负载（XLink + HTTP + SSE）
  ⇒ 2753 次往返 **0 错**，p95 **29ms** / max 235ms；RSS 斜率 **+0.04MB/min**
  （sim）/ +0.02MB/min（web）；1639 个 SSE 事件；**无泄漏 / 停滞特征**。
  **真机验收**：桌面客户端「变频器参数」卡与网页端同卡**同时打开**，显示值
  全部**来自模拟变频器回读**（0 / 15.0kW / 380V / 30.00A / 50.0Hz /
  1440rpm），不出现提交值冒充回读值。
  **C 端暂缓（用户明确「c端先不管了」）**：C 服务端 op=4 变频器故障复位透传、
  C 服务端 0x011A、C 客户端 0x011B —— 协议号已在
  `common/XhsfLinkProto.h` 占位（防新旧端错配；未实现端回「未知命令」否定
  包络，客户端 / web 端 fail-open 回落）。
  **本轮三个踩坑**：① **整块覆盖**（脚本把一个类的所有方法替换掉）⇒ 用
  **AST 悬空引用扫描 + 与 `git stash@{0}` 逐方法对拍**判定损伤范围；
  ② 脚本改 CRLF 文件必须先**归一到 LF** 再写回（否则产生 `\r\r\n` ⇒
  IndentationError）；③ JS **相邻字符串字面量**换行 ⇒ SyntaxError ⇒ 整页
  白屏 ⇒ 一律用 `+` 拼接。

- **在途闸必须**成对开合**（2026-11-18 联调验收实测的真缺陷，网页端）**：网页端
  「应用到变频器」按钮**只活一次**——第一次写成功后，后续点击全被
  `if (vfdInflight) return;` 吞掉（状态行还停在上一次的「已写入并回读确认：
  6 项」，现场观感是"点了没反应"）。根因：闸**只置位不复位**（finally 缺失）。
  ⇒ 修法：函数体整体进 `try/finally`、`finally` 里复位（JS 的 return 会先跑
  finally ⇒ 所有早退路径都覆盖）；**客户端同款补上在途闸**（原来只有网页端
  有 ⇒ 双击=两批写并发下发，两批的 applied 判定互相污染），并在 `on_done` /
  `on_error` **两条终态路径**都复位。
  **门禁**：`verify/webui/webui_check.py` 的 `sec_static` 加静态判据「在途闸
  成对开合（`xxxInflight = true` ⇒ 同文件必须有 `xxxInflight = false`）」+
  客户端回归 **V10a~V10d**（在途禁入 / 有提示 / 终态复位 / 复位后可再写）。
  **判据设计坑（比缺陷本身更值得记）**：第一版判据要求「复位必须在 finally
  里」⇒ 把 `page06.js` 的 `pumpBusy`、`trendchart.js` 的 `rafPending`
  （**在异步回调里复位**，完全合法）误判成红 ⇒ **门禁自己有噪音**，真缺陷
  反而会被淹没 ⇒ 放宽为「同文件内存在复位即可」。⇒ **静态门禁的判据必须先
  在全树上跑一遍自检**（哪些形态是合法的），否则它会变成误报源。
  **通用教训**：任何"防重入/在途"的闸都是**成对**的（进入置位、**任何**出口
  ——正常返回/早退/异常/超时——都要复位）；只写进入侧 = 功能只活一次，而且
  **协议级验收打不到**（它只打 API，不点按钮），静态语法检查也打不到。

- **拖动调速滑条按转速分段着色（2026-11-18 用户口径「1000转以上拖动条变红，
  440转以下是绿色，中间段保持蓝色」）**：桌面客户端「搅拌泵」页的控制卡 +
  网页端同款。**阈值是转速 rpm 口径**：>1000 红 / <440 绿 / 440~1000 蓝
  （**1000 本身仍算蓝**——口径是「以上」＝严格大于）。
  **频率调速模式也要生效** ⇒ 滑块值先按**额定比例**换算成 rpm 再判档
  （1440rpm@50Hz ⇒ 440rpm≈15.3Hz、1000rpm≈34.7Hz ⇒ 15Hz 绿 / 25Hz 蓝 /
  35Hz 红）；换算比例取**服务端快照**的 ratedRpm/ratedHz，**不在客户端再
  写死一份 1440/50**；拿不到额定点 ⇒ 保守落蓝档（=现状，不乱变色）。
  **实现要点**：① 桌面端控件**自身**样式表优先级高于祖先 QSS ⇒ 想改色必须
  写 **own-QSS**（只覆盖 groove/handle 配色、几何照抄主题），并**自带
  `:disabled` 规则**（否则祖先的灰色手柄被盖掉、禁用时仍是档位色）；②
  **只在跨档时**才 `setStyleSheet`（每格都调 ⇒ 重新 polish + 重排 ⇒ 拖动
  卡顿；实测 145 个取值只更新 3 次）；③ 蓝档**不写覆盖样式**（清空 ⇒ 回到
  主题 QSS，外观与改造前逐像素一致）；④ 网页端 `.vfd-slider` 是**自绘**
  （`appearance:none` + 自定义 thumb）⇒ 原生 `accent-color` 无效 ⇒ 按档位
  切 `band-green/blue/red` class + CSS 变量取色；档位规则必须写在
  `:disabled` 规则**之前**（同特异性时后者赢，否则禁用态压不住档位色）。
  **回归锁**：客户端 `run_vfdsld` 段 **C1~C4**（三档判定/频率模式换算/
  145 值只更新 3 次/三档可见效果）；网页端 webui_check 静态段（阈值常量 +
  三档 class + theme.css 三档规则）。
- **自动模式配置新增第四个可配置模式「放水模式」（2026-11-18 用户口径
  「自动模式中再增加一个放水模式，一样在远端设置中页添加可自定义，直接拷贝
  之前那三模式，改个名字即可；放水模式**不进自动模式**）」**：
  **「能配」与「能跑」必须拆成两个集合**（协议层）：
  · `AUTO_MODES`（产水/反洗/顺洗）= **能跑**（首页下拉 / op=5 选模式 /
    组合「自动（流量循环）」的子模式）；
  · `AUTO_CONFIG_MODES`（三模式 + **drain/放水模式**）= **能配**（远端设置
    「自动模式配置」卡的**第四块**，字段与前三块**完全一致**：启动设备多选/
    搅拌电机转速+方向/停止与急停时关阀复位/换相策略两个下拉 ⇒ **复用同一
    个建块函数**，不复制第二份）；服务端 `config.py` 的 auto 节按
    `AUTO_CONFIG_MODES` 建表 ⇒ **接受并落盘** drain（否则用户配了没保存）；
    服务端/客户端/网页端三处常量**逐值同步**（回归锁对账）。
  **放水模式目前只能配、不能跑**（op=5/op=1 与 `mode_config_error()` 都拒它）
  ——界面提示语要写清楚，别让人误以为能跑。
  **网页端**：首页下拉与远端配置块原先**共用**一份 `autoModes` ⇒ 拆成
  `autoModes`（能跑）与 `autoConfigModes`（能配，registry 两个字段）。
  **C 服务端兼容（本轮差点引入回归）**：C 端 `HsfServerAutoMode` 的模式表
  固定三个，且 `applyModeDoc()` 对**未知模式键整包拒** ⇒ 客户端若把 drain
  也发过去，C 服务端会**整包失败**（连产水/反洗/顺洗都存不进去）⇒
  **下发必须按服务端自己回显的模式集合过滤**：回显的 auto.modes 键 = 它
  认识的模式（PY 四个 / C 三个）⇒ 只发它认识的；**还没回显（未知）⇒ 只发
  通用子集（三模式）**，绝不盲发第四个键；被跳过的模式在状态行**说明**
  （不是静默丢）。客户端与网页端同款。
  **回归锁**：服务端新文件 `server_py/tests/test_auto_drain_mode.py`
  **16 检**（四模式同批下发/落盘/重新加载仍在/未知键与非法策略仍被拒/
  op=5 与 op=1 拒放水/三模式口径不受影响）**已注册进 tests/run_all.py**；
  客户端远端设置段 **A6a/A6h/A6m/A9b**（四块口径）+ **A6n0~A6n4**（只发
  服务端认识的模式：四模式齐 / 三模式不含 drain / 未知只发子集 / 记录跳过）；
  网页端 webui_check 新段 **sec_auto_modes**（两份列表分开）+ 静态段。
  **跨端对齐锁**（`test_server_alignment.py` 的 §auto.modes）改成「**共有
  子集逐字段比对 + PY 多出来的键只允许 drain**」——C 端补齐后该条自动变严。

- **口径订正：「放水模式」是**可选/可启动**的普通自动模式（2026-11-18 用户
  当场指出「咋下拉没有放水模式」）**：上一条把「放水模式不进自动模式」实现成
  「**不进首页下拉 / 不能被启动**」⇒ 现场看板上拉里没有它 ⇒ **理解偏了**。
  用户原意是「**不进「自动（流量循环）」**」——放水是普通的可配置、可选、
  可启动的自动模式，只是**不进流量循环的那三个子模式**。
  **协议层改法**：`AUTO_MODES`（**能跑 + 能选 + 能配**）= 产水/反洗/顺洗/
  **放水**（四个）；新增 **`AUTO_CYCLE_MODES`** = 产水/反洗/顺洗（**组合循环
  的子模式**，不含放水）——循环代码本来就用硬编码的 `_CYCLE_PRODUCE/
  _CYCLE_BACKWASH/_CYCLE_RINSE`，所以放水天然不进循环；
  `AUTO_CONFIG_MODES` = `AUTO_MODES`（配置块与可运行集合同源）。
  **三端联动**：首页「模式」下拉（客户端与网页端都按 `AUTO_MODES` 建）⇒
  放水自动出现；op=5 选模式 / op=1 启动与其它三模式**同款**闸（配置了设备
  就能跑）；`mode_config_error("drain")` 按其配置判（配了设备 ⇒ 可跑）。
  **界面提示语订正**：原写「只保存配置、不能被启动」⇒ 改成「与其它三个一样
  可在主界面选来启动，只是**不进「自动（流量循环）」**」。
  **回归锁**：客户端 home 段 **D2**（下拉五项 = 四模式 + 自动）+ **D2b**
  （组合循环子模式 = 三个、不含放水）；服务端 `test_auto_drain_mode.py`
  **⑤~⑤g**（op=5 被受理 / op=1 过模式闸 / mode_config_error 为空 /
  循环集合不含放水 / AUTO_MODES 含放水）；网页端 webui_check `sec_auto_modes`
  （下拉 = 四模式 + 自动、配置块 = 四模式）+ 静态段（协议侧有
  `AUTO_CYCLE_MODES` 且不含 drain）。
  **教训**：用户说「X 不进 Y」时，**先确认 Y 指的是哪一层**——「自动模式」在
  本项目里**既指整个自动模式体系**、**又特指组合模式「自动（流量循环）」**
  ⇒ 同一个词两种含义 ⇒ 实现前先按界面口径核一遍（这次是看板上下拉缺项才
  暴露）。**凡是界面集合与协议集合不一致，先问清楚"哪个集合少了一项"**。

- **分档着色要染「条本身」，不是只染文字/手柄（2026-11-18 现场截图订正）**：
  第一版实现把**蓝档**写成「清空 own-QSS ⇒ 回到主题 QSS」⇒ 主题里轨道是
  `scroll_handle`（**灰 #C2CAD2**）、手柄才是强调蓝 ⇒ 现场看到的是
  「**文字蓝、条还是灰的**」（用户原话「中间蓝色条怎么没有只有文字变了」）
  ⇒ **三档一律写 own-QSS，把轨道与手柄一起染成档位色**。
  **条色用固定色**（绿 `CLR_OK` / 红 `CLR_FAULT` / 蓝 `CLR_INFO` #1A66D0），
  **不取主题 accent_fill**：① ISA 主题的强调色是琥珀 ⇒ 用主题色就不是用户
  说的「蓝色」；② 网页端蓝档用 `--accent-line`（亮色主题同为 #1A66D0）⇒
  两端不同色会让人以为是两套实现。**两端同色**（#1A66D0）才是同一套。
  网页端同理补 `.vfd-slider.band-blue::-webkit-slider-runnable-track` /
  `::-moz-range-track` 的背景（webkit + moz **两套都要写**：网页端是自绘滑条，
  原生 `accent-color` 无效）。
  **回归锁**：客户端 vfd 段 **C4** 改成「蓝档**有**覆盖样式且含蓝」（原来锁
  的是「蓝档无覆盖」＝**已被用户判定为错**的旧行为）；像素取证脚本确认三档
  条色分别为 #2E7D32 / #1A66D0 / #C62828（条与手柄同色）。
  **教训**：用户说「X 变蓝」时，**要问清是哪个元素变蓝**（读数？条？手柄？）；
  同一个控件有多个可视部分，只改一个就会被判「没变」。

- **板端变频器「一直有异常应答」（2026-11-18 现场排查 → 三层根修）**：
  现场：板端 192.168.1.249（Python 服务端）日志页持续刷
  「搅拌泵 采集失败（连续 1 次）：Modbus 异常应答 fc=0x03 code=3」
  +「恢复通讯」，12 分钟 16 对；设备本身在跑、遥测也有值。
  **取证链（板端只读探针 + 现场帧字节，逐步实证，不是推测）**：
  ① 同一字节串 `02 03 80 01 00 03 7d f8`（M512 的 8 字节错误帧、错误码
     0x03「数据错」）在**不同请求**（0x1010×4 / 0x8000×1 / 0x3000×1）上
     反复出现 ⇒ 不是某段地址非法，是**某一帧请求本身有问题**；
  ② 只读探针逐组单发（150ms 间隔）：7 个读组全正常、0x1010×4 连读
     12/12 正常 ⇒ **地址表没问题**；
  ③ 设备对真非法请求只回 0x04/0x05（0x0000→5、0x1020×2→4、单点连读→5），
     **从不回 0x03**；
  ④ **故意把请求的 CRC 打坏 ⇒ 设备回的正是 `…800100 03…`** ⇒
     **异常帧 0x03 = 「你这帧请求的 CRC 是坏的」** ⇒ 现场 485 链路
     **偶发位错**（命中 CRC 就回错误帧，命中站号/被截帧就静默）。
  ⑤ 请求中途被拆帧（任意位置、≥2ms 间隔）⇒ 设备**静默丢弃**（超时）；
     超时后再等 150ms+300ms 回读端口，36 次**全空** ⇒ 不是「迟到应答被
     当成本次应答」。
  ⑥ 事务间隔实测：**150ms 间隔 100+ 笔 0 失败；0~100ms 偶发失败（~1%）**
     ⇒ 失败后**立刻重发等于在从站"坏状态"里继续轰它**。
  **三层根修（Python 服务端）**：
  ① **传输层**（`modbus.py`）：`RtuMaster._with_retry` **异常帧也要重试**
     （老口径「从站明确拒绝、重试无意义」被证伪）；并加
     **`FAIL_COOLDOWN_MS=150` 失败后退避**（上一笔失败 ⇒ 下一笔先静默
     150ms，重试也走这条退避，不再隔 8ms 就轰）。
  ② **设备层**（`devices.py` `VfdM512.poll`）：**组级容错** —— 单个读组
     失败**不放弃整拍**（失败组保留上一拍的值，其余组照常刷新），
     只有**全部**组失败才算这一拍失败 ⇒ 与 C 端 XModbusHub 口径一致
     （AGENTS.md 三条契约②「异常应答是组级事实」）；组级失败走**限频**
     日志（首条 + 每 60 条）带地址与异常码，**不冒泡成告警**
     （「采集失败/恢复通讯」是告警级语义，含义是「设备失联」）。
  ③ **文案自描述**：整拍失败时把**每个读组**的地址与原因都带进告警
     （原来只有一句「Modbus 异常应答 fc=0x03 code=3」，现场根本看不出是哪
     几组坏了、是超时还是异常帧——排查这一条花了一整天）。
  **回归锁**：`server_py/tests/test_vfd_link_resilience.py`（6 检：异常帧
  重试后读成功 / 重试仍异常照旧抛= fail-closed / 单组失败不放弃整拍 /
  全部组失败照旧失败并判离线 / 状态字读失败保留上一拍结论 / 失败后退避
  ≥150ms），已注册进 `tests/run_all.py`；**A/B 反证**：把 `_with_retry`
  换回老行为，case A 立刻抛异常 ⇒ 门是真的。
  **实测效果（板端）**：修复前 12 分钟 16 条告警 → 修复后 **5 分钟 0 条**
  （连组级失败日志都没有 ⇒ 传输层就把噪声吸收了）；遥测仍持续刷新
  （ts 递增、母线 547.1V、10 台设备健康全「正常」）。
  **遗留（硬件，非软件）**：链路仍有 ~0.4% 帧位错，软件已容错，但**建议查
  物理层**：两端 120Ω 终端电阻、屏蔽层单端接地、与变频器输出电缆分开
  走线。**通用教训**：设备自有错误码可以用来**反查现场帧**
  （「故意打坏 CRC 看它回什么」是定位「这帧是设备对哪笔请求的回答」的
  决定性手法）；**"迟到应答被当成本次应答"这类猜想必须用「回读端口看
  有没有迟到帧」证伪**（本例 36 次全空，假说被否，才转向「请求本身被
  破坏」）。

- **导航切页卡顿（2026-11-18 用户反馈「有些界面明显卡顿一会儿才打开」）**
  —— 三路并发排查（客户端切页链路 / XGui 框架层 / Python 客户端）+ **探针
  实测量化**，不是凭猜改：
  **排查口径（可复用）**：用 `Test/XGuiTest/hsf_vfd_probe.c` 当测量台
  ——它走**生产同一条装配路径** `hsfUiMakeWin`（16 页全建、离屏），
  真实点击导航条目；配 `XHSF_PAINT_TRACE=1`（逐帧整树绘制耗时 µs）/
  `XGPU_W_FRAME_PROF=1`（布局画像 act/skip/inval/solveAvg）。
  **结论**：切页**没有任何同步阻塞**（无 sleep / 无 sync request / 客户端
  无 SQLite）⇒ 用户体感的「卡顿一会儿」= **同步的布局/绘制耗时**，不是
  等应答。三处真凶：
  ① **水质/流量计页每帧 229ms**：`HsfMonitorFlowMeter.c` 的仪表**没有
     静态层缓存**，且弧带仍用「逐角度画数千条 AA 径向线」的老实现
     （底盘 ≈2500 条 + 底轨 ≈1900 条）——变频器页 2026-11-08 已改成
     「路径填充 + 静态层缓存」，**这一页没同步过去**（两页范式分叉）。
  ② **导航面板每次切页无条件 15 次 setGeometry**：框架的
     `XWidget_setGeometry` **刻意不做同值早退**（每次都失效父布局）⇒
     15 次布局失效 + 15 次重绘。项目铁律「每拍都调的 setter 必须自己判
     同值」——本处缺守卫。
  ③ **Python 客户端 `lan_ip()` 走主机名解析**：板端常无 DNS ⇒
     `gethostbyname_ex` 阻塞数秒～数十秒 ⇒ 切「Web 设置」页时 UI 线程
     被冻住。
  **三处修复（均已实测+像素比对）**：
  - `HsfMonitorFlowMeter.c`：① 仪表加**静态层缓存**（按「尺寸+主题」惰性
    重建，与变频器页同款：底盘+底轨进离屏图，每帧只 blit + 进度带）；
    ② `fmDrawArcBand` 改**一条闭合路径填充**（arcTo 外弧→内弧→
    closeSubpath→fillPath），弧点按 1px 弦长取样；③ 底盘改「填充椭圆
    + AA 描边」（原先也是径向线链）。
    **实测：单帧 229ms → 0.139ms（1600×）**；改前/改后**逐像素比对：
    仪表区域完全一致**（差异块只落在状态栏时钟）——路径填充自带 4×4
    子采样 AA，与原径向线链的羽化边缘等价。
  - `HsfNavPanel.c`：新增 `navSetGeometry()`（同值早退守卫），标题/滚动
    壳/15 条目三处改用它 ⇒ 每次切页的 15 次布局失效归零。
  - `client_py/hsfsrv_client/pages_web.py`：`lan_ip()` 改为三段探测
    ① UDP connect（选路）② **枚举网卡地址（Linux ioctl SIOCGIFADDR，
    完全不碰 DNS，微秒级）** ③ 主机名解析兜底**放后台线程 + 1s 超时**
    ⇒ 板端实测 **可能阻塞数秒 → 0.3ms**（板端已部署并重启 kiosk）。
  **回归锁**：`hsf_vfd_probe.c` 新增 **[P] 段**（放在探针**末尾**：本段会
  真实点击导航、改导航树展开态，放前面会破坏「初始导航树全收起」等初始
  态断言）——两道门：**切页同步段 ≤150ms**（老路：多轮整树重解算，实测
  618ms）+ **切页首帧整树绘制 ≤80ms**（老路：径向线链式重画，实测
  229ms）。修复后实测：最慢同步段 39ms（远端设置）、首帧峰值 61ms。
  探针现状 **166 OK / 1 FAIL**（唯一失败是**既有红**「点设备状态表表头：
  无排序指示」，AGENTS.md 已挂账）。
  **剩余已知开销（本轮未动，按需再攻）**：① 趋势图 `XChartView` 首绘
  ≈50ms（水质页首帧 61ms 的大头，框架层）；② 搅拌泵页稳态 ≈31ms/帧
  （3 只仪表 + 趋势图）；③ **隐藏页定时器无可见性闸**（报警页
  `alarmTimerCb`、远端设置页 `srRefresh` 每 200ms 改隐藏页标签 ⇒ 照样
  失效布局；变频器页早就有这个闸，注释写着「实测 49 次/秒布局失效全部
  来自这里」）；④ Python 端**首访懒建页**（桌面远端设置 89.8ms，板端按
  CPU 弱估 0.6~0.9s）+ app 级 `*` 通配 QSS（实测 +42%）。
  **教训**：① **两页同款控件却不同款实现**是这类性能债的常见来源——
  变频器页 2026-11-08 的静态层优化**没同步到水质页**，差 1600 倍；
  ② 性能问题必须**先量化再改**（探针 + 像素比对），否则容易改出视觉回退；
  ③ 框架刻意不做同值早退 ⇒ **热路径上的每个 setter 都要自己判同值**。

- **物理启动/停止 ≡ 屏幕启动/停止（2026-11-18 板端实锅：下拉已切、物理
  按钮仍按旧模式启动）**：用户口径：「物理按钮直接接入屏幕启动和停止，
  然后自动运行的时候模式切换下拉禁止操作」。
  **根因**：服务端把「**用户选中的模式（粘性）**」与「**运行中的模式 /
  组合循环当前子阶段**」挤在同一个 `ctl.mode` ⇒
  ① 运行中改下拉被**拒**（op=5 怕改写正在跑的子阶段）⇒ 粘性停在旧值；
  ② 物理启动按钮取 `self.mode`（= 粘性/运行中模式）⇒ **按旧模式启动**，
  而屏幕上的启动是**显式带上下拉当前模式** ⇒ 两路不同源 ⇒ 现场以为按钮坏了。
  **C 端早就是分账的**（`s_mode` 粘性 vs `s_cycleStage` 子阶段）⇒ 本次向
  C 端口径对齐。
  **四处改动**：
  ① `server_py/hsfsrv/autocontrol.py`：新增 `selected_mode`（= C 端
     `s_mode`：只记「用户的选择」，op=5 记它、落盘它）；`start(mode=X)`
     同时更新它；**现场启动按钮改用它**（`want_mode = selected_mode or
     self.mode`）。
  ② `server_py/hsfsrv/app.py`：op=5 **运行中也受理**（只记选择、**不动
     运行中的子阶段** ⇒ 循环链不会被改写）；停止时状态回显按**粘性**
     （`autoMode`）⇒ 客户端据此回显下拉。
  ③ `client_py/hsfsrv_client/pages_other.py`：**运行中模式下拉置灰**
     （用户口径；顺带避免「两路不同源」）+ **首次**拿到服务端模式时把下拉
     与服务端粘性对齐一次（用户自己动过下拉就不动它）。
  ④ 物理**停止**侧本来就与屏幕停止同效（都走 `stop(stop_devices=True)` +
     逐模式阀复位配置）⇒ 只需保证模式来源一致（① 已覆盖）。
  **回归锁**：`server_py/tests/test_auto_modes.py` 的
  `test_gates_and_sticky_mode` 段新增两条（**单元口径**，不依赖自动序列
  跑动——夹具的阀反馈是静态的、阀相 400ms 就报警锁存，协议级「运行中」
  状态不稳）：① 运行中 op=5 被受理且 `ctl.mode`（运行中）不被改写、
     `selected_mode` 记为新选择；② 现场启动按**用户选中的模式**跑（把
     `self.mode` 留成未配置的旧模式 ⇒ 旧口径会被「未配置」拒掉 ⇒ 断言必红）。
     **A/B 反证**：把「现场启动用 selected_mode」换回旧口径 ⇒ 用例立刻红。
  [板端] 客户端侧已部署并复检（截图：运行中模式下拉**已置灰**、启动灰/
  停止可用）；服务端侧待重启生效（**重启会中断正在跑的自动序列**，见下）。
  **教训**：① **一个字段承担两种语义**（粘性 vs 运行态）迟早出事 —— C 端
  分账了、Python 端没分账，两端行为就分叉了；② 改「按钮 vs 界面同效」这类
  需求时，先确认两路**取的是不是同一个事实源**（模式来源/闸门/动作）。
  **补充（本次连带改的两端契约与一个环境坑）**：
  - **op=5「运行中一律拒绝」的老契约作废**（2026-11-16 审计锁 H1 改写）：
    拒绝的理由是「Python 端 ctl.mode 兼作组合循环子阶段 ⇒ 运行中改选会
    打断生产」。Python 端分账后该理由不成立 ⇒ **两端都改成受理**
    （C 端 `HsfServerApp.c` op=5 分支 + `HsfServerAutoMode.c`
    `selectMode` 的防御闸一并放开）⇒ 跨端行为不再分叉。
    回归锁 `test_audit_hardening.py` H1 改写为「运行中 op=5 被受理（只记
    选择、不动运行中模式）」。
  - **C 客户端不需要改**：它的 op=5 语义未变（仍只是「记选择」），C 端
    `s_mode` 早就与子阶段分账 ⇒ 放开拒绝不会影响运行中的工序 ✓。
  - **环境坑（又踩一次）**：`tests/test_server_alignment.py` 会**复用**
    6100/6200 上在跑的 sim ⇒ 若那个 sim 被留在**自动模式/运行中**，跨端
    文案对照必红（本次 6 条「C=… PY=自动运行中：远程控制已被禁用」假红）
    ⇒ **跑跨端对照前先把 sim 切回手动/停掉**（本次是我早先测模式时留在
    6200 的 sim 处于自动运行态 ⇒ 杀掉后即全绿）。

- **板端 Python kiosk 切页卡顿（2026-11-18 用户确认是「板上那台 Python
  kiosk」）—— 量化定位 + 三处修复**：先用**板端实测**（不是桌面推算）
  建了一个可复用的测量台：`/tmp/measure_page_switch.py`（离屏 + 真实 CJK
  字体 ⇒ 度量口径与真机一致），逐页量三段：**构树 / 大 QSS / 入栈首显**。
  **基线（17 页）**：构树 2773 + 首显 2100 = **4873ms**；二次进入只要
  **2~3ms** ⇒ 「卡顿一会儿才打开」= **首次进入的建页成本**（不是数据/
  布局问题）✓ 另：切页链路**没有任何同步阻塞**（无 request_sync / 无
  本地 SQLite / 无 sleep）⇒ 别再去查「等应答」。
  **三处修复（均已部署板端 + 回归绿）**：
  ① **大 QSS 挂页面容器，页面继承**（原来每页单独挂一遍）：板端总建页
     5417 → 4175ms（-23%）；**改前/改后逐页像素比对 0 差异**（17 页）。
  ② **删掉 app 级 `* { font-family }` 通配 QSS**（`theme.apply_font`）：
     app 级样式表要**每个控件**解析一遍 ⇒ 板端建一页多花 ~270ms。
     实测「有些控件不吃 app.setFont」的顾虑**不成立**：板端与桌面各两页
     逐像素对照**完全一致** ⇒ 只留 `app.setFont`。
     板端总建页 **4873 → 3901ms（-20%）**。
  ③ **`lan_ip()` 不再走 DNS**：改 Linux ioctl 枚举网卡 + 主机名兜底放
     后台线程带超时（板端无 DNS 时 `gethostbyname_ex` 会阻塞数秒 ⇒ 切到
     「Web 设置」页冻 UI）⇒ 板端 **可能数秒 → 0.3ms**。
  ④ **WiFi 原生调用挪后台线程**（`wifi_open`/`wifi_adapters` 各 ~80ms、
     `wifi_connection` **每次 ~75ms**）⇒ 网络设置页切过去不再堵 UI；
     空闲轮询的连接态查询也走后台（开着该页时原本每秒冻 75ms）。
  **板端样式表成本分解（远端设置页 1115ms）**：
  app 级通配 QSS 271ms ／ 容器级大 QSS 237ms ／ 卡片自带 QSS 230ms ／
  控件构造与布局插入 377ms ⇒ **QSS 逐控件解析是板端的大头**（桌面同样的
  代码只占几毫秒 ⇒ 板端弱 CPU 把「每控件一次样式解析」放大成秒级）。
  **尚未做（待定夺）**：① **预热（后台建页）** —— 能让切页变 2~3ms，但
  建页期间 UI 线程被占（板端一次 0.03~0.85s）⇒ 实时显示会顿，**取舍要
  现场拍板**；② 卡片自带 QSS 改成「容器一次下发 + 选择器匹配」（省 ~230ms，
  但碰 QSS 级联，历史有坑）；③ 大 QSS 按页裁剪（省 ~237ms，工作量大）。
  **⑤ 页面预热（后台建页）—— 把「第一次进某页」的建页挪到后台**：切页的
  同步段 = 「点击 → 页面显形」的**全部**工作 ⇒ 预热必须把**显形**那部分
  也提前付掉，否则只省了构树（实测：只建页 510→406ms；再连
  `ensurePolished` + 布局激活 ⇒ **295ms**）。
  - 实现：`MainWindow._prewarm_step()` —— 逐页 `_build_page` + 入栈 +
    `ensurePolished()` + `layout().activate()`（**不切过去、不取数、不
    订阅** ⇒ 预热不产生网络流量）；定时器 1500ms 一页；顺序按「常用先建、
    最重的远端设置最后」。
  - 开关：本地设置 → 界面设置 → 性能 →「预建页面」（设置键 `prewarmPages`，
    **缺省关**；改完下次启动生效）。板端 kiosk 已打开。
  - **板端真实点击实测**（开预热）：切页 **30~86ms**（原先 200~900ms）；
    预热 16 页约 21s 跑完（日志 `[prewarm] 已预建页 …`）。
  - **代价（现场要拍板）**：预建期间 UI 线程被占（板端一次 0.03~0.85s）⇒
    实时显示会短暂停顿；且**预建那一页没建完时点导航会等它**。
  - 诊断留手：`HSF_TRACE_PAGE_MS=1` ⇒ 每次切页打一行用时（现场可测
    「切页还卡不卡」）。

- **「自动（流量循环）」配置化（2026-11-18 用户口径「升级为可以配置选择需要
  循环的模式，并且可以设置他们的顺序」「所有都支持下压差或定时吧，都支持
  设置」）**：原来循环链**三重硬编码**（`autocontrol.py` 的三个私有常量 +
  `config.py` 的 7 个固定键 + `proto.AUTO_CYCLE_MODES` 只是「口径声明、
  没人读」⇒ **改配置换循环链做不到**）。
  **新契约（auto.cycle 子节）**：
  | 键 | 语义 |
  |---|---|
  | `cycleModes` | 参与循环的**有序**模式 id 数组（produce/backwash/rinse/drain 都可选）；顺序=循环顺序；末位→首位闭环；**长度 ≥2** |
  | `stageCfg` | `{modeId: {pressEnabled, pressThreshold, pressCompare, timerEnabled, timerMinutes}}` —— **每阶段各自**的切换条件：压差与定时**互不排斥、先到先切**；**两个都没开 ⇒ 配置非法（拒绝）** |
  | `stageMinutes` | `stageCfg` 的**派生兼容视图**（保留给已上线的旧界面） |
  - 旧 7 键**继续合成**：压差/兜底定时/分钟 ⇒ **列表首元素**；`backwashMinutes`/`rinseMinutes` ⇒ 对应模式的定时分钟；`stageCfg` 存在则以它为准。
  - 缺省与改动前**逐字一致**：列表 produce→backwash→rinse；produce=压差开 0.5MPa/gt+定时关 30min；backwash/rinse/drain=压差关+定时开 10min。
  - **改配置不打断正在跑的循环**（沿用 `_cycle_cfg` 快照口径 ⇒ 下次启动生效）。
  - **阶段记忆**（`_cycle_resume_mode`，内存态）若已不在新列表 ⇒ 从**首元素**
    开始并记一行日志；不在列表里的阶段不记。
  - **启动闸 fail-closed**：空列表/未知模式/重复模式/类型错/某阶段零触发 ⇒
    明确报错（文案见 `app._cfg_set_auto_cycle`）；并补上单模式分支已有的
    **设备级校验**（阀未配点位/泵未装配）。
  - **时长口径只此一处**（`_cycle_stage_minutes`）：表盘剩余时间与换相判定
    共用（原来两处各写一遍 ⇒ 隐性 bug 面）。
  - 桌面客户端「远端设置 → 自动循环配置」UI 扩展为**一个有序列表**：每行 =
    参与勾选 + 模式名 + **该阶段自己的压差/定时条件** + 上移/下移；文案点明
    「第一个是主运行阶段」「两者都开则先到先切」。
  - 回归锁：服务端 `test_auto_cycle.py` 改表驱动 + 新用例（两元素闭环、顺序
    被改后按序推进、只配压差/只配定时/两者都配、空列表被拒、阶段零触发被拒、
    记忆阶段不在列表的兜底、落盘往返保序、**缺省行为与改动前逐字一致**）；
    客户端远端设置段新增「载荷键形/勾选/排序/老服务端缺键不误改/服务端拒绝
    文案上屏」用例。
  - **本机模拟端到端已验**：SET 自定义顺序 `[rinse,backwash,produce]` + 每阶段
    条件 ⇒ 受理 ✓；GET 回显顺序与条件一致 ✓；5 类非法配置全部明确拒绝 ✓；
    恢复默认 ✓。
  - **未做**：C 端服务端 + 网页端的同款配置面（跨端对齐测试按既有「PY 多出
    键」白名单放行）。
- **2026-11-18 三端审计（服务端/客户端/网页端）修掉的 P0**：
  - **客户端 `lan_ip()` 在 Windows 崩进程**（今天引入的回归）：`import fcntl`
    在 try 之外（POSIX-only）⇒ 非 POSIX 且**无默认路由**时抛
    ModuleNotFoundError，而 **PyQt5 对槽内未捕获异常走 qFatal() ⇒ 进程
    abort**（实测 exit 127/SIGABRT）。⇒ import 进 try + `except ImportError`
    返回空串；`lan_ip()` 整体包 try/except（任何探测异常都回环回地址）。
    主机名兜底在 **UI 线程**（页面 on_enter ⇒ 切页时）⇒ 超时 1.0s 收窄到
    0.3s + 结果缓存。
  - **服务端写路径把 M512 的 8 字节错误帧当成「写成功」**：该错误帧
    `[站号][fc][80][01][00][code][CRC2]` **长度也是 8、CRC 也合法** ⇒ 原先
    `check_crc` 在前 ⇒ 错误帧命中「正常回显」分支被 return ⇒ **变频器拒收
    停机/给定/参数也报成功**。⇒ 先判错误帧形态再判回显；补写路径回归锁。
  - **服务端远程 op=1 启动取错模式字段**：无载荷时回落 `ctl.mode`（=运行中
    模式/循环子阶段）⇒ 与现场物理启动按钮（用 `selected_mode`）可能按
    **不同模式**跑。⇒ 回落顺序改为 `selected_mode` → `mode`（空串也回落
    粘性，与 C 端一致）。
  - **组合循环换相把「用户选中的模式」覆盖回 "auto"**：运行中 op=5 选了
    别的模式时内存里是用户的选择、落盘却被换相写回 ⇒ 重启后粘性丢失 ⇒
    物理启动按钮按旧模式跑。⇒ 落盘 `self.selected_mode`。
  - **客户端 VfdPage 没有 `on_leave`** ⇒ 按住点动时切页，点动定时器继续每
    200ms 发 OP_JOG ⇒ **无人按下电机仍在点动**（安全相关）。⇒ 补 on_leave
    停点动与两个下发定时器。
- **远端设置的「服务端持久化」实测（2026-11-18 用户问）**：本机 sim 协议实跑
  逐节验（写 → 看 cfg 文件 → 运行期 GET 回显）：
  | 段 | 落盘 | 服务端回话（= 生效口径）|
  |---|---|---|
  | auto（模式配置 + 循环 `cycleModes`/`stageCfg`） | ✓ `cycle_modes`/`stage_cfg` 逐字落 cfg | 运行期生效，下次启动沿用 |
  | storage | ✓（协议 `maxMb` 落 cfg 是 `max_kb` = MB×1024 ✓） | 「库路径下次启动生效」 |
  | login | ✓ | 「下次登录生效」 |
  | protect | ✓ | 「立即生效」 |
  | collect | ✓（落 INI） | 「运行期立即生效」 |
  ⇒ **服务端持久化无缺口** ✓。**口径坑**：**协议键是 camelCase**（`maxMb`/
  `loginMaxSessions`/`pressHighLimitMpa`…），**cfg 文件键是 snake_case**
  （`max_kb`/`max_sessions`/`press_high_limit_mpa`…）⇒ 服务端做转译 ⇒
  **手写测试脚本用 cfg 的键名会被服务端以「未知 xx 键」拒绝** ✓（本次踩过）。
- **网页端补齐桌面端新契约（2026-11-18）**：此前网页端缺 **0x011A 报警历史
  分页**（只存浏览器本地的边沿行）、**0x011B 变频器参数**（端点 404）、
  **放水模式**（`/api/pages` 不带 `autoConfigModes` ⇒ 远端设置里配不出放水）
  ⇒ 三个都补上（`hub.py` 加 `alarm_page`/`vfd_params`，`api.py` 加
  `/api/alarms/page` + `/api/vfd/params` 读写，`page14_remote.js` 加变频器
  参数卡 + 放水模式块）。`verify/webui/webui_check.py` **429 通过 / 0 失败**。
  - 报警历史：上游不支持 0x011A（C 端）⇒ 网页端回**显式 ok=false + err**
    包络，前端回退 `/api/alarms`（fail-open ✓）。
  - 变频器参数：写只允许**管理员**；空 `values`/非法 `op` ⇒ 400；RunLocked
    拒绝文案**三端逐字一致**透传 ✓。
  - 顺带修：`page14_remote.js` 的运行态文案曾把 0x3000 原始字当成
    `XVfdRunState` 枚举（桌面端 2026-11-18 刚纠正过）⇒ 改用
    `VFD_RUN_WORD_TEXT`（1 正转/2 反转/3 停机）✓。
- **自动循环「每阶段各自压差/定时」新契约（2026-11-18 用户定案「所有都支持下压差
  或定时吧，都支持设置」+「我看到有旧版兼容，没必要兼容旧版了」）**：
  - **两个新键**（`auto.cycle` 子节）：`cycleModes` = **有序数组**（参与循环的
    模式 + 顺序；第一个＝主运行阶段；末位→首位＝闭环）、`stageCfg` =
    {modeId: {pressEnabled, pressThreshold, pressCompare, timerEnabled,
    timerMinutes}}——**每个阶段各自**一套切换条件，压差与定时**互不排斥**
    （先到先切）；**两者都没开 ⇒ 服务端明确拒**（fail-closed，不静默跑成死
    循环）。旧扁平键（`pressThreshold`/`pressCompare`/`timerFallback`/
    `timerMinutes`/`backwashMinutes`/`rinseMinutes`）**作废**：客户端界面
    **不再有「旧版兼容」区**、**不再提交**旧键。
  - **参与模式池 = 四模式**（含「放水模式」）：服务端 2026-11-18 起支持
    `cycleModes` 后放水也能进循环（`AUTO_CYCLE_MODE_POOL`）。
  - **两端「未知键」策略不同 ⇒ 客户端提交口径要分寸**：服务端**旧版 Python
    对未知键整包拒** ⇒ 客户端「服务端回显过该键 **或** 用户动过列表/条件」
    才发这两个键，且**必须成对发**（只发其一会得到「阶段 X 未设置切换条
    件」这类半吊子错误）；C 服务端对未知键**丢弃**（前向兼容）⇒ 两端都能活。
  - **客户端真 bug（已修）**：`_cycle_apply_server_order` 原写成「按池内原序
    过滤」⇒ 服务端回显的顺序被忽略（顺序白配）⇒ 必须按**服务端给的顺序**
    排（回归 A8f12/A8f14 钉住）。
  - **回归锁**：服务端 `tests/test_auto_cycle.py` + `test_auto_switch_policy.py`；
    客户端 `test_hsf_regression.py` 的 A8 段（A8a~A8f15）。
- **变频器参数读写 0x011B（2026-11-18 新增，三端同号）**：服务端
  `app.py::_h_vfd_params`（读＝登录即可 / 写＝管理员 / **RunLocked**：运行中
  拒写且**不发总线写**）、`devices.py::read_params_snapshot/write_param`；
  客户端 `bridge.vfd_params(op, values={int addr:int raw})` +
  `proto.PARAM_DEFS`（**与服务端逐值一致**，改一处必须两端同批改）；
  网页端 `/api/vfd/params` 读写（管理员闸 + RunLocked 文案透传）。
  - **显示值只认服务端回读的 `value`**（绝不显示本地提交值）；写后用应答的
    `params` 刷新（回读 ≠ 提交 ⇒ 显示回读值，不撒谎）。
  - **`applied=false` 的项要逐个列出**（`；失败：P1.02`）——deny-write 型驱动
    器会回 ok=true + 逐项 applied=false ⇒ 只按提交项数计就是**假成功**。
  - **在途闸必须成对开合**（网页端 2026-11-18 踩过：只置位不复位 ⇒ 「应用」
    按钮只活一次）⇒ 用 try/finally 包住整个函数体（所有早退路径也复位）。
  - **P1.00 电机类型是枚举** ⇒ 写控件是**下拉框**（0 普通异步电机 / 1 变频
    异步电机 / 2 永磁同步电机），不是数值框；回读列显示**中文名**。
  - **`runState` 是 0x3000 原始字**（1 正转 / 2 反转 / 3 停机）——**不是**
    XVfdRunState 枚举（数值撞车、含义相反）⇒ 网页端单独一张
    `VFD_RUN_WORD_TEXT`；桌面端同款，别混。
- **脚本化改文件前必须逐文件备份（2026-11-18 用户口径「以后记得自己备份
  下」）**：用 `.zcode/tmp/bak.sh <文件…>`（带时间戳落 `.zcode/tmp/bak/`；
  文件名带**路径前缀**，同名文件（两端各一份 proto.py）会互相覆盖）。
  **批量替换的 Python 补丁脚本禁止用于大文件**（本轮锚点写错，一次删掉
  4830 行）。
  - **恢复源要选「工作树快照」不是「git 暂存区」**：暂存区可能与工作树不同
    （本轮恢复时暂存区比误删前工作树新 629 行、也旧 561 行）⇒ 拿暂存区恢复
    = 把工作树独有的改动一并回滚。
  - **删了东西先量化再动手**：用 `difflib` 比**唯一行集合**（按行号 diff 会因
    文件重排/重复块误报"几百行丢失"；本轮真实净损 106 行，其中约 100 行是
    用户要求删的旧版兼容 UI）⇒ 先算清"丢了什么"再决定是回滚还是重写。
- **判活口径：ping 通 ≠ 板子在线（2026-11-18）**：板子关机时本机 ping
  192.168.1.249 仍可能"0% 丢包"（同 IP 的别的设备/ARP 残留应答）⇒ 判活要
  以 **SSH 22 端口可连** + `systemctl is-active` 为准。
- **循环参与列表要 ≥2 个模式（2026-11-18 修「文案与行为不一致」）**：
  服务端 `app.py::_cfg_set_auto_cycle` 原来只拦「空列表」，但报错文案写的是
  「至少 2 个模式才能构成循环」⇒ **只勾 1 个模式能存进去**（1 个模式的
  "循环"不是循环＝同一阶段反复跑）。⇒ 现按文案口径真要求 ≥2，并把
  **当前个数**说进文案（「当前 1 个」），免得用户看着「不能为空」却明明
  勾了一个。客户端/web 端若允许勾到 1 个，服务端会明确拒（fail-closed）。
- **旧扁平键会被服务端「折进对应阶段」⇒ 三端必须都用新键（跨端互踩）**：
  `auto.cycle` 的旧键（pressThreshold/pressCompare/timerFallback/
  timerMinutes/backwashMinutes/rinseMinutes）不是被忽略，而是**折进
  stageCfg 里对应阶段**（只改值）⇒ **网页端/桌面端只要还写旧键，就会悄悄
  覆盖另一个端配好的该阶段条件**。⇒ 2026-11-18 起三端一律只发
  `cycleModes` + `stageCfg`（成对），旧键只由服务端自己派生写盘。
- **Modbus 写路径：错误帧判据必须「与请求对账」，不能只看形状（2026-11-18
  服务端回归查出的潜伏缺陷）**：M512 的 8 字节错误帧
  `[unit][fc][80][01][00][code][CRC2]` 与**正常 FC06 回显**
  `[unit][fc][addrH][addrL][valH][valL][CRC2]` **形状会撞**——写地址
  **0x8001** 时回显前 4 字节也是 `80 01` ⇒ 只按「形状」判错误帧 ⇒
  **写成功了却报失败**（上层会重试/报错，现场表现为「命令发出去就报异常」
  而设备其实已经动了）。⇒ 判据改成：回显的「地址+值」必须等于请求的
  「地址+值」（`pdu[1:5]`），不相等才有资格按错误帧解释；形状像回显但内容
  对不上 ⇒ fail-closed 报「写应答与请求不符」。回归锁在
  `server_py/tests/test_rtu_framing.py`（FC06/FC05 正常回显 + 0x8001
  撞车 + FC05/FC06 错误帧），**A/B 实证**：撤掉修复 ⇒ 0x8001 那条红。
  读路径同理加固：字节数 0x80 只有在**帧长 ≤8** 时才当错误帧（连读 64 个
  寄存器的正常读应答字节数也是 0x80，但帧长 133 字节）。
- **0x011B 进跨端命令号对照表（2026-11-18）**：`server_py/tests/
  test_server_alignment.py` 的 `CMD_NAMES` 当时是写死的 19 条、**不含**
  `VFD_PARAMS` ⇒ 两端各写各的号也不会红。已补（对齐段 255/0）。
- **主页搅拌泵卡第三项 = 转速（2026-11-18 补齐三端，C 端 11-17 已改而
  Python/网页端漏改）**：C 端 `client/ui/pages/HsfHomePage.c` 早在 11-17 就把
  主页一览卡的第三项从「设定频率」换成了「**转速**」（`HOME_TXT_VFD_SET`=
  "转速"、`HOME_RPM_PER_HZ = 1440/50` ⇒ 28.8 rpm/Hz），注释还写着"与 Python
  客户端主页、网页端主页同口径"——**这句当时是不实的**：桌面客户端与网页端
  一直还显示「设定频率」。⇒ 本轮把两端补齐（桌面客户端 `HomePage.vfd_rpm`
  + 网页端 `page00_home.js` 的 `vfdRpm`），三端现在真的一致。
  - **取值优先级（两端同款）**：① 服务端下发的 `speedRpm`（设备层按铭牌
    额定点算的换算值，变频器无转速寄存器）→ ② 没有该字段就按**输出频率**
    本地换算（25Hz ⇒ 720rpm）→ ③ 都没有显示 `--`（不编造）。
  - **「设定频率」仍在泵详情悬浮卡**（客户端 `pd_setf`、网页端 `pdSetf`），
    那里本来就有「搅拌转速」⇒ 两处都在，不是把设定频率删了。
  - **教训**：C 端注释里的"与 X 端同口径"**是断言不是事实**——跨端改一处时
    要**去对方端核一眼**（本轮就是靠读 C 端注释才发现另外两端没跟上）。
- **客户端不要重复服务端已有的校验（2026-11-18 差点犯）**：服务端对
  「参与循环的模式 < 2 个」明确拒（文案带当前个数）⇒ 我在客户端
  `_apply_auto` 里加了一道本地预判，结果回归 A8f15/A8f16 直接红——那两条
  明写口径是「**由服务端判** + 客户端显示服务端**原话**」。⇒ 客户端只做
  界面层能判的事（权限、格式），**业务口径的单一事实源在服务端**；客户端
  拿到拒绝就把原文透出去，别自己改写成"友好提示"。
- **「服务端回显 = 事实 ⇒ 覆盖界面」是硬口径，别偷偷加保护（2026-11-18 踩过）**：
  客户端回归 **A8e** 锁着：用户改过循环配置之后，服务端回显照样**覆盖**界面
  （enabled/参与顺序/每阶段条件全跟着服务端走）。⇒ 我一度加了个「用户动过
  ⇒ 跳过覆盖」的保护（想着防"配置 GET 晚到吃掉用户刚点的勾选"），
  **A8e 直接红** ⇒ 撤回。已知小缺陷（配置 GET 晚到会覆盖用户刚点的勾选，
  只影响页面刚打开的一两秒）记在两端源码注释里；真要根治得引入
  「有未应用的修改」提示，而不是改口径。
  - **网页端真浏览器用例的正确写法**：循环区控件是**先建后填**的 ⇒ 抓载荷
    前必须**等配置回显到位**（等 `populateCycle` 跑过、拿到 stageCfg），
    否则抓到的是"还没加载"的中间态（载荷里只有 `enabled`）⇒ 那不是产品
    行为，是时序。门禁里已加这个等待。
  - **「应用成功 ⇒ 清 dirty」是对的**（已提交的内容 == 服务端确认的内容 ⇒
    之后的回显可以照铺）；两端都这么做。
- **服务端「旧键折进阶段」的合并优先级：显式 stageCfg 覆盖旧键折进值**
  （2026-11-18 定的）：载荷同时带 `stageCfg` 与旧扁平键时，以 **stageCfg**
  为准（它是新契约的唯一事实源）；旧键只在**没带 stageCfg** 时才生效
  （那才是"老客户端"的场景）。⇒ 写「同时带两者」的用例时，别断言旧键
  赢。
- **Modbus 写后回读：「驱动器拒写」≠「不应答」（2026-11-18 修）**：
  `devices.py::write_param` 的回读重试原来用 `except ModbusError: continue`
  ⇒ 把**异常应答（驱动器明确拒收）**也吞成"不应答"⇒ 现场会把「参数被拒」
  误读成「通讯问题」。⇒ 单独接住 `ModbusException`，如实带 code 上报
  （口径与写路径同款）。
- **Modbus 读路径的错误帧判据要用「形状 + 前 8 字节 CRC」，不能只看帧长**
  （2026-11-18 修）：错误帧 `[站号][fc][80][01][00][code][CRC2]` 与
  「连读 64 个寄存器」的正常读应答（byteCount 也是 0x80，长 133 字节）撞
  形状 ⇒ 用「帧长 ≤8」能分开，但同一次读里**多带一个尾字节**就会掉进
  「长度不足」分支 ⇒ **异常码丢失**（仍 fail-closed，只是难诊断）。
  ⇒ 改判据：`raw[3]==0x01 且 raw[4]==0x00`（错误帧形状）**且**
  `check_crc(raw[:8])`（前 8 字节 CRC）⇒ 才按错误帧解释。
- **网页端与桌面客户端的「写参数确认项数」口径必须逐字一致（2026-11-18）**：
  确认数 = **总条数 − applied===false 的条数**（5 项成功 + 1 项被驱动器拒 ⇒
  「已写入并回读确认：5 项；失败：P1.02」）。网页端原先是「只数
  applied===true，且有任一失败就整体归零」⇒ 明明 5 项写进去却说
  「确认 0 项」⇒ **误导现场**。已对齐（网页端 `vfdWriteOkMessage`）。
- **主页压差卡：note 是说明，不是"值无效"（2026-11-18 补齐客户端缺口）**：
  服务端对越限/负压给**线性值 + note**（如 -0.016 MPa +「低于 4.0mA（负压）」）
  ⇒ 客户端**照显示数值**，note 另起一行用警示色。**旧实现「note 非空一律
  '--'」把负压读数整个吃掉了**——负压是真实读数（4mA 是零点，以下是吸力），
  现场要靠它判断；C 端 `HsfHomePage.c` 一直就是「值照显示 + note 改说明行
  颜色」⇒ 本轮把桌面客户端补齐（同 C 端）。
  - **顺带口径**：客户端的"设备异常"判定仍按「note 非空 ⇒ 越限异常」
    （与 C 端历史页 `HsfLinkHistory.c` 的「越限说明文本非空 ⇒ 异常行」一致）
    ⇒ **不要**把负压 note 从异常判定里剔除（那是另一件事，且 C 端没改）。
  - **回归锁**：客户端总回归 §L「压差负压 ⇒ 数值显示负值（-0.016）」与
    「压差超量程 ⇒ 显示端点值 + note」。
- **"通用权限闸"不许抹掉更细的状态（2026-11-18 用户实锅）**：远端设置页的
  权限闸（`RemoteSettingsPage.refresh()` 里那段 `for _mid, _row in
  self._cycle_rows.items(): ...`）原来直接 `up.setEnabled(can_cfg)` ⇒ 把
  「**首行不能上移 / 末行不能下移**」的**边界状态**抹平 ⇒ 真机看着"到头了
  还能点"（功能上本来也是空操作，但界面骗人）。⇒ 规矩：**权限闸只管权限，
  位置相关的可用性由专门的那一处算**（`_cycle_render_order`），两者不要
  互相覆盖；改这类闸之后要用**真实连接 + 真实回显**复验（离屏手填数据的
  探针看不到 —— 通用闸每 200ms 跑一次，会覆盖手填路径的结果）。
- **"禁用态"必须看得见（本项目的老坑再犯一次）**：主题的 `QPushButton:disabled`
  在「大 QSS 挂页面栈容器」的架构下**不一定**落到按钮上 ⇒ 真机里禁用的
  上移/下移与可用的**像素完全一致**（底色 #FFFFFF、字色 #1A1A1A）。⇒ 需要
  表达禁用态的按钮**本地**补一条 `:disabled` 规则（`theme.disabled_button_qss()`，
  只写禁用态 ⇒ 常态/hover 仍走主题）。网页端 CSS 已有 `.btn:disabled` ⇒ 不用。
- **隐藏控件要留占位，否则行高会跳（2026-11-18）**：未参与循环的行把
  「上移/下移」**隐藏**（用户口径「没勾选的不应该进排序」）⇒ Qt 里隐藏的控件
  **不参与布局** ⇒ 该行高度少 1~2px ⇒ 勾/不勾时下面的行上下跳（回归 A8f5b）。
  ⇒ 解法：行高下限取**首帧量到的**按钮高度（记 `row["head_h"]`），与按钮
  可见与否无关；网页端用 `visibility:hidden`（保留占位）而不是 `display:none`。
- **变频器调速：停机拖动 = 只预置给定，不发设备（2026-11-18 用户实报
  「没启动不是可以预设速度，启动的时候是按照拖动调速的给的速度启动的吗」）**：
  停机时给定是**运行参数**，服务端拒独立给定（fail-closed 不变）⇒ 客户端把
  「停机不下发」的判定收口到 **`_send_set_speed()` 唯一收口**（`preset_only
  or not self._is_running()` ⇒ 只预置不发帧）：松手（`_on_slider_released`）、
  去抖（`_on_slider_settled`）、数字框（`_on_spin_settled`）、遗留死代码
  `_on_set_freq` 四条路径全走它 ⇒ 口径天然一致。原缺陷：松手那条没判停机
  ⇒ 真发帧 ⇒ 状态栏「命令被拒：…不能下发给定」（而页面提示又说"只预置"）
  ⇒ 自相矛盾。**运行中拖动/改值 ⇒ 仍在线调速（真发帧）**。
  - **启动确实按预置值跑**：给定绑在 OP_START 上（`_given_payload()` 捎带
    rpm+hz，服务端先写频率再发命令字——这是修过的坑：给定不捎带就按变频器
    里残留旧值跑）。
  - **给定 = 0 是特例**：服务端 `if given_hz > 0` 才写频率 ⇒ 预置 0 后启动
    = 沿用变频器当前值 ⇒ 文案明说（「0 表示不指定给定，启动时沿用变频器
    当前值」），不假装"按 0 运行"。
  - **预置值只在客户端内存**：不落盘、不跨客户端；**现场物理启动按钮用不
    上它**（物理按钮走 autocontrol，速度取 auto 配置节 stir_rpm——要现场
    定速在远端设置「自动模式配置 → 搅拌电机转速」里配）。
  - 网页端 `page05.js` 当时就是对的（onGivenSettled 统一判运行态），本轮
    只补文案同源 + 回归锁（webui_check 新增 sec_vfd_preset 15 检）。
  - 回归锁：客户端 `--only=vfd` F8~F19（停机松手/数字框不发帧、无 op=2/3、
    提示含"预置"不含"被拒"、启动帧带 rpm+hz、给定 0 特例）；A/B 实证有
    牙齿（松手改回"停机也发帧" ⇒ F8/F9/F10/F15 红）。
  - **门禁计数**：客户端全量 1791/0、网页 291/0。
- **「远端设置页通用权限闸抹平排序按钮边界」实为防御性（agent A 复核）**：
  权限闸后面紧跟 `_cycle_render_order()`（按位置重算）⇒ 注入旧写法
  （`up.setEnabled(can_cfg)`）A/B 仍全绿 ⇒ 真正有牙齿的是 `_cycle_render_
  order` 的边界判定本身；A8f3c 已锁（**必须给管理员权限跑**，否则
  can_cfg=False 会假绿）。
- **回归期间别编辑测试文件（agent A 踩过）**：UI 段是**子进程隔离**跑的，
  子进程重新 import 正在被写的文件 ⇒ 一轮全量跑出 14 条"段组崩溃"假红
  ⇒ 重跑即绿。并发任务（agent + 主线程）各改各的文件没问题，但**跑回归
  的时间窗里别写测试文件**。
- **「0 是真值，不是没数据」（2026-11-19 用户实报「web 网页端转速怎么显示 -- 不是
  0」）**：变频器停机 ⇒ 输出频率 0 ⇒ **转速就是 0** ⇒ 界面显示 **0 rpm**，只有
  **字段缺失**才显示 "--"。⇒ 网页端主页转速的两个分支原来都带 `> 0`
  （`doc.speedRpm > 0` / `doc.outputFreqHz > 0`）⇒ 0 掉进 "--" ⇒ 已去掉门槛
  （与桌面客户端 `_vfd_rpm_text` 同口径：字段存在就用它）。
- **历史表压差列显示**数值**（2026-11-19 用户口径「历史数据里应该记录具体的
  负压值用负数，而不是说是个负压」）**：服务端对低于量程下限的通道给**线性
  负值**（如 -0.008 MPa）+ 说明「低于 4.0mA（负压）」；历史显示列原来
  **说明优先** ⇒ 负压行显示成那句说明、看不到数值 ⇒ 现在**数值优先**（服务端
  `store.py` 的 DISPLAY_EXPR + 客户端 `_server_row_texts` + 网页端
  `page09_history.js` 三处同改）。**「这行当时越限」的信号没丢**：说明仍在
  `press_note` 列里，**异常行标红照旧按它判**（三端都是
  `bool(press_note)`）。
  - 主页压差卡同理（网页端曾「note 非空 ⇒ 值 --」⇒ 负压读数被吃掉，已改为
    值照显示 + note 只改说明行颜色）。
- **网页端日志/报警的导出**：日志原本**只有 CSV**（`/api/log/export`），报警
  **没有导出路由** ⇒ 本轮补 `/api/log/export` 的 xlsx 形态与
  `/api/alarm/export`（csv+xlsx），前端两页都给导出按钮。
- **XLSX 数值列要是数值格**（2026-11-19 用户实报「保存为 xlsx 的时候为啥是
  文本型而不是数值型」）：三处写出器（客户端 `xlsx_stream.py`、网页端
  `xlsx_stream.py`、网页端 `export.py` 的 `build_xlsx`）都把数字写成
  `t="inlineStr"`（**docstring 写着"数字写数值格"，代码却不是**）⇒ Excel
  里当文本。⇒ 数值格**不带 `t` 属性**、内容放 `<v>`；注意 **bool 是 int 的
  子类**（先判 bool）、浮点要保精度、NaN/Inf 退回文本、字符串仍走 inlineStr
  并 XML 转义。
- **自动模式「停止＝暂停 / 启动＝续跑 / 复位＝清零」（2026-11-19 用户口径，
  PY 服务端先落，C 端待跟进）**：用户原话「自动模式下**不管是哪个模式运行**，
  按下停止后都要**保留停止时候的状态**，之后按启动是**继续运行**，而不是从
  头开始，**特别是定时器时间要保留**，除非按过**复位按钮**，屏幕上的或者
  **实体的复位**都可以才会重置状态重置时间」「**阀门状态还是按照设置的是否
  复位来**」。
  - **落法**（`server_py/hsfsrv/autocontrol.py`）：停止时把运行态记成
    **暂停态**（`_paused`/`_pause_root`/`_pause_mode`/`_pause_elapsed_ms`：
    根模式 + 模式/阶段 + 该阶段**已用时长**），相位报 **"paused"**；启动时
    命中暂停态 ⇒ **续跑**（同一模式、同一阶段，阶段计时**接着走**
    `_cycle_stage_at = now - 已用时长`），相位机照常走一遍
    **阀相（等阀到位）→ 泵相（按该模式配的设备集合启泵）→ 运行相**；
    复位（op=4 **与实体按钮组 DI15 的沿**，两者同走 `reset()`）⇒ 清空
    暂停态 ⇒ 下次从头。
  - **单模式也保留**（旧口径只有组合循环的 `_cycle_resume_mode`，单模式根
    本不记 ⇒ 与"不管是哪个模式"相悖）。**阀是否复位仍按逐模式
    `resetValvesOnStop`/`resetValvesOnEstop`**（这条口径不动）；**续跑时
    "等阀到位"是自然结果**：停止时复位了阀 ⇒ 续跑要等阀重新到位；没复位
    ⇒ 阀还在原位，阀相一拍即过（不必空等）。
  - **快照**：0x0105/0x0115 增两键 `autoPaused`(bool) / `autoPauseMode`
    (str)；停止后 `autoStageElapsedMs`/`autoStageTotalMs` 仍报**记住的阶段
    与剩余时间**（冻结在停止那一刻）⇒ 界面能显示「暂停中（还剩 X）」。
    ⇒ **老客户端不认 "paused" 相位也不崩**（相位是字符串查表）。
  - **回归锁**：`server_py/tests/test_auto_pause_resume.py`（64 检：停止保留
    /续跑接着计时/复位与实体复位清零/单模式同样保留/续跑先等阀到位/泵按模式
    配置/阀门复位口径不变/端到端真服务端），已注册进 `tests/run_all.py`。
    ⇒ **改口径后 `test_units.py` 的 `test_start_stop_idempotent` 与
    `tests/test_sim_e2e_auto.py` 的「停止后阶段计时归不可知」两条断言已按
    新口径更新**（停止后是 paused + 仍报剩余时间，不是 idle/-1）。
  - **两端协议键**：C 端尚未实现这两个键 ⇒ 双端对照表
    (`tests/test_server_alignment.py`) 把 `autoPaused`/`autoPauseMode` 放在
    **条件键表**（允许单端有）；C 端补齐后可移回固定键表。

- **板端软键盘「一弹就消失」+「闪烁」（2026-11-19 根修，客户端 Python）**：
  用户报「远端设置 → 自动循环里的『定时 N 分钟』微调框一点，屏幕键盘弹出
  来**马上就消失**；同一页『模式卡·搅拌电机转速』微调框**正常**（键盘不消
  失）」。
  - **根因甲（键盘自灭）**：配置回显到货 → `_populate` → `_populate_cycle`
    末尾**无条件**调 `_cycle_render_order()`；而它为了重排把每行
    `w.setParent(None)` ⇒ **Qt 规定 setParent 一律隐藏控件** ⇒ 行里正被
    编辑的微调框跟着隐藏 ⇒ **焦点被挪走**（板端实测焦点跳到「参与」勾选
    框）⇒ 软键盘的 FocusOut → `_review_focus` → 焦点不在输入框 ⇒
    `_close()` ⇒ **键盘当场消失**。**模式卡的微调框不在被重挂的行里 ⇒ 不
    受影响 ⇒ 现场表现正好「一个坏一个好」——这就是定位的钥匙**（同一页两个
    同类控件一个坏一个好 ⇒ 差别必在容器，不在控件）。
    修法：行控件本来就是 `_cycle_list_box` 的子控件 ⇒ `takeAt`（从布局摘掉）
    + `addWidget`（重挂）就够换序，**一个 setParent 都不能调**（连
    `setParent(同一父控件)` 也会先隐藏）。
  - **根因乙（闪烁）**：软键盘 `bind()` 原来**无条件** `_render_page()` ⇒
    每换一次焦点就把 73 个按键全部销毁重建（+ 全量 QSS 解析）⇒ RK3568
    上肉眼可见地闪一下。修法：比「渲染签名」（kind/数字盘排布/字母模式/
    拼音页/拼音大小写/**键高**），签名没变就只 `reflow()` + 刷候选带，
    **不重建按键区**。
  - **回归锁**：客户端 `test_hsf_regression.py` **A8i/A8j**（重排不隐藏行
    控件 + 重排后布局顺序仍 == 参与顺序+池顺序）、软键盘段 **D9/D10/D11**
    （同类切换面板不消失 / **同类切换不重建按键区** / 换键盘类型仍重建）。
    **A/B 实测**（把旧代码放回去再跑）：A8i 红（被隐藏 4 行）、D10 红
    （重建 1 次）⇒ 门是真的。
  - **板端真实触摸取证配方（可复用）**：真实触摸靠**往 evdev 写事件**注入
    （`verify/board/tap_inject.py`）——**XTEST 只能造核心指针事件、造不出
    触摸**；板端 ILITEK 面板同时暴露两个设备：event3（真多点触控 PROP=2）
    与 event4（驱动层指针模拟）⇒ 注入 event3 即与真手指同路（X 服务器照
    样同时给 XI2 触摸 + 指针事件）。
    **坑**：面板竖屏原生 + DSI 旋转 ⇒ X 服务器对它的轴做了 **90° 映射**，
    实测 `屏幕x = device_y/12.8`、`屏幕y = 800 - device_x/20.48` ⇒ 不标定
    就点偏（落在别的控件上，症状像「键盘弹了但没反应」）。
    复现脚本：`verify/board/kb_softkeyboard_repro.py`（真实页面 + 真实触摸
    + 喂配置回显 ⇒ 看键盘是否当场自灭）。
- **网络相机接入（RTSP → 服务端采集 → 三端分发；2026-11-19 用户定案）**：
  现场一台 TP-LINK IPC 鱼眼（`192.168.1.181:554`，Digest realm
  `TP-LINK IP-Camera`）。**画面一律经服务端**：`相机 RTSP → 服务端采集器
  → JPEG 704² → XLink 分片 → 各客户端组帧`。客户端**没有任何一条直连相机的
  路径**（用户口径：网页端也要从服务端拿数据，外网访问场景浏览器在公网、
  相机在内网，只有服务端两边都够得到）。**帧只解码/编码一次** ⇒ 相机只占
  一路 RTSP 会话（这很重要：本相机**只允许一路**会话，见下面坑①）。

  **① 相机侧的三个硬事实（实测，改采集器前必读）**
  · **帧率只有 1/10/15/20 四档**（`/ds` 能力表 `frame_rates`
    = 65536+fps ⇒ `65537/65546/65551/65556`）。**相机没有 25/30fps**
    ⇒ 用户问「远程能 30fps 吗」的答案是不能（硬件档位就没有）。
  · **只允许一路 RTSP 会话**：前一个会话没 TEARDOWN 就走，新连接拿不到
    任何数据（实测残留一个 `gst-launch-1.0` 占着会话 ⇒ 后续所有测试
    0 帧）。⇒ 采集器必须**优雅关闭**；调试时先 `pkill -x gst-launch-1.0`。
  · **SETUP 全部 3 路 track 会让会话哑掉**：本相机 SDP 有 video + audio +
    `application/TP-LINK`（智能分析）三路，**只 SETUP track1** 才有 RTP
    （板端实测 3014 帧/3s）。**这就是 GStreamer `rtspsrc` 用不了的原因**
    （它 SETUPs 所有 track）⇒ 采集器是**自研最小 RTSP 客户端**。

  **② 协议（三份 proto + C 头文件逐值同步）**
  · `0x011C` = `CMD_CAMERA_FRAME`（占号，照 0x011B 先例）+
    `TOPIC_CAMERA_FRAME = 0x0001000C`。
  · 分片片头 **16 字节小端**：
    `magic(2) frameId(4) seq(2) total(2) width(2) height(2) flags(2)`
    （格式串 `"<HIHHHHH"`；`CAM_FRAME_MAGIC=0x4341`、片长
    `CAM_CHUNK_MAX=12288`、`FLAG_FIRST=1`/`FLAG_LAST=2`）。
    **片头里不能加 version** —— 加了就得同步改格式串与三端解包，
    实测反复 `struct.error: pack expected N items`（改成 16B 后正好
    不剩一个字节）。
  · **为什么必须切**：XLink 单帧载荷上限 **16KB**
    （`XLink_config.h:295`）。超限**不是丢一帧而是整会话断链**
    （客户端 `decode_header` 返 None）⇒ 按**峰值**（板端实测 104KB）设计，
    单片总长 `16+12288=12304 < 16384`。

  **③ 配置节 `camera`（远端设置 → 新增「相机」卡）**
  键：`enabled/ip/port/username/password/stream(main|minor)/fps/width/
  quality/timeoutMs`；`CFGK_*` 与缺省 `CAMERA_*_DEF` 三端逐字同值。
  **两个安全/安全性的口径**：
  · **口令不回显**：GET 应答只给 `hasPassword` 布尔、`password` 恒空；
    SET 收到**空口令 = 不改**（否则客户端回填空串一存就把口令抹了 ⇒
    现场直接连不上相机）。
  · **逐键 fail-closed**：非法码流档/非法 IP/端口或帧率帧宽质量越界
    **明确报错**，不静默回落缺省（相机配错时现场分不清"坏了"还是"配错了"）。
  · 缺省 `enabled=false`（相机是可选外部设备，不装就不起采集进程）。

  **④ 编码后端（板端 vs 开发机）**
  `HSF_CAMERA_BACKEND`（`gst` / `ffmpeg`）+ PATH 自动挑：
  · gst（板端）：`fdsrc ! h264parse ! mppvideodec(硬解) ! videorate !
    video/x-raw,framerate=N/1 ! videoconvert n-threads=2 !
    video/x-raw,format=I420 ! jpegenc quality=Q ! fdsink`。
    **抽帧（`videorate`）必须在 `videoconvert` 之前**（2026-11-20 板端
    实测：CSC 占单帧成本 96%（147ms/帧 @704²），成本 ∝ 它处理过的帧率 ⇒
    顺序反过来是给相机全部 25fps 做 CSC，**295% 单核**；前置后 2fps
    **31~47%**、5fps 93%。整机负载 4.0 → 1.1~1.7）。
    **不做 `videoscale`**（缩放同样在解码器 buffer 上很贵；要更省 CPU
    就把**相机端子码流分辨率**调小，成本 ∝ 像素数）。
    **必须产出 `format=I420`**：`jpegenc` **不吃 NV12**（旧口径在
    `videoscale` 后钉 NV12 ⇒ 管线构建期失败、gst 立即退出 ⇒ `_pump` 的
    sink 写失败走 dead 分支**静默返回**、零日志、无休止重连）。
    **不许给 `mppvideodec` 加强制格式 capsfilter** ⇒ **静默 0 帧**。
  · **硬件 `mppjpegenc` 在本板固件（gstreamer-rockchip 1.14.4）上不可用
    ——别再试**（2026-11-20 穷尽实证）：`mppvideodec ! mppjpegenc` CPU 仅
    6% 但**画面偏紫/棕、12KB/帧（软件编码 113KB）**；`q-factor` 10→96 体积
    只 11→14KB（**质量参数失效**）；`dma-feature=true`/`memory:DMABuf`/
    强制 I420·NV21·RGB 输入（静默 0 帧）/解码器端 `colorimetry=bt601`
    （not-negotiated）全部无解。定性为插件在 NV12 输入下**色度平面解释
    错误**（+BT.709 流按 JFIF/BT.601 显示），参数层面修不了；上游补丁只补
    DMABuf sink caps、不修色度。**Mali-G52 不参与编解码**（VPU/JPEG Codec
    是独立硬件块），GL 做 CSC 在服务进程里 `EGL_BAD_PARAMETER`；板上无
    `rgaconvert` 元素 ⇒ **GPU 救不了这条路**。
  · **`quality` 与 CPU 无关**（2026-11-20 实测 q80 93% vs q20 111%，噪声内）：
    它只改量化表，影响**体积/带宽**（35/26/18KB @80/50/20）与弱网卡顿。
    降 CPU 只有三个旋钮：**fps**、**相机端分辨率**、**切 stream 模式**。
  · **两者质量参数相反**：`jpegenc quality` 1~100 **越大越清晰**；
    `mppjpegenc q-factor` 同向；**ffmpeg `-q:v` 1~31 反向（越小越好）**
    ⇒ ffmpeg 后端按 `q_ff = 31 - quality*30//100` 折算（quality 80 ⇒ 7），
    直接把 80 塞进去会**最糊**。

  **⑤ 订阅门控（用户口径：「只在客户端打开了相机界面要看画面时才推送」）**
  `_publish_camera_frame` 里 `topic_subscriber_count(TOPIC_CAMERA_FRAME)
  <= 0 ⇒ return`（连切片都不做）。**关键细节**：无人看时**不更新**
  `_camera_published_seq` —— 更新了的话客户端刚打开相机页会读到"这帧推过"
  ⇒ 得等下一帧（2fps = 0.5s）才有画面。**采集本身照常**（服务端与相机同
  局域网，成本可忽略）⇒ 打开即有画面，不用等起流。

  **⑥ 三端组帧规则（各端一份实现，规则必须一致，改一处要同步三处）**
  `server_py/hsfsrv/` 生产分片；`client_py/hsfsrv_client/camera_frames.py`
  与 `webui_py/hsfweb/camera_frames.py` 各自组帧（**不许互相 import**：
  三端是独立可分发的包）。规则：
  · `seq==0` ⇒ 丢掉未收齐的旧帧（**覆盖式**，慢客户端只保最新，不堆积）；
  · 收齐 `total` 且带 `FLAG_LAST` ⇒ 校验 JPEG 头尾（`FFD8…FFD9`）才出图；
  · **缺片即丢整帧**（不等超时）；别的 frameId 的续片 ⇒ 整帧作废。

  **⑦ 网页端的两级链路**：XLink 分片 → webui `hub._on_camera_frame` 组帧
  → `model.camera`（**只广播元信息**：seq/宽高/KB/时间；JPEG **不过 SSE**，
  那是文本通道，base64 要 +33% 体积）→ 浏览器 `<img src=
  "/api/camera/frame?seq=N">`（`image/jpeg`，`Cache-Control: no-store`；
  无帧 404 + `X-Camera-Reason: no-frame`）。SSE 事件丢了有 1500ms 兜底
  轮询。**`<img>` 带不了 Authorization 头 ⇒ 靠登录时下发的 `hsf_token`
  Cookie**。

  **⑧ 页号契约：相机 = 17，16 是保留槽位**
  桌面客户端的「Web 设置」是 16（**仅 Python 客户端**）；网页端不实现它
  但**必须占号**（页号是操作手册契约，只追加不重排、两端逐号对齐）。
  网页端原先把相机做成 16 ⇒ 与桌面端撞号，已改 17
  （`page17_camera.js`）。三端 NAV_TREE/SUBMENU 都追加在**末尾**。

  **⑨ 三个「白名单漏一项 = 整页永久静默」的坑（都实锅过）**
  · `bridge.TOPIC_KIND` / `hub.TOPIC_KIND` 漏 `camera` ⇒ 帧到了不认；
  · 网页 `store.js` 的 `KINDS` 白名单漏 `"camera"` ⇒ **事件全被丢弃**，
    页面永远"未收到画面"（`applyEvent` 静默 return）；
  · 网页端 `<img>` 显示：`img.style.display = ""` 会**落回样式表的
    `display:none`**（无帧默认态）⇒ 图片加载了却看不见（`naturalWidth`
    有值、画面空白）。必须写死 `"block"`。

  **⑨-2 `QComboBox` 首插自动选 0 ⇒ 初值必须显式钉到协议缺省（2026-11-19
  实锅，「界面撒谎」家族）**：相机卡的「码流」下拉先插「主码流」、后插
  「子码流」，而协议缺省是**子码流** ⇒ 应答到达前界面显示主码流、服务端
  实际按子码流跑（帧率下拉同理：首插 1fps vs 缺省 2fps）。**凡是用
  `addItem` 按显示顺序填的下拉，初值都要
  `setCurrentIndex(findData(proto.XXX_DEF))`** —— 显示顺序（哪个更常用）
  与缺省值（服务端按什么跑）是两件事，不能靠巧合对齐。客户端相机段
  **D2** 锁这一条（断言的正是 `currentData() == proto.CAMERA_*_DEF`）。

  **⑩ 回归锁**
  · `server_py/tests/test_camera.py`（**48 检，已注册进 `run_all.py`**）：
    A 配置面 11（含口令脱敏/空口令不改/各类越界）· B 分片协议 7（片头
    七字段/**单片 < 16KB**/首末标记/拼回逐字节/12288 边界/空帧）·
    C 订阅门控 6（无订阅不推/同帧不重推/**无人看不消费帧号**/无采集器
    静默）· D 纯函数 9（JPEG 宽高/切帧/gst 命令行含 NV12 与硬解/
    **ffmpeg `-q:v` 反向折算**）· E **三端组帧器一致** 15（两端各 import
    自己那份喂同一串分片，断言结果相同 + 常量逐值一致）。
  · 客户端跨端同形锁：`PAGE_COUNT==18` / `len(NAV_TREE)==17` /
    `c_tree` 含 `("相机",17,1)` / `PAGE_NAMES[17]=="相机"`；
    `test_settings_merge.py` A1/A4（页数 18 / 导航 17）。
  · 客户端 **§37 相机段**（`--only=camera`，**24 检**）：B 组帧器 6
    （完整拼回/宽高/丢片丢整帧/覆盖式/尾标记校验/垃圾包）· C 相机页 8
    （只订相机主题/无帧提示不显旧图/**有帧真解码贴图**/状态行/元信息
    五要素/坏帧明确提示/同帧不重复解码/真 JPEG 自检）· D 远端设置相机卡
    10（控件齐全/初值=协议缺省/口令密码态/回显/口令不铺明文/状态行实况/
    局部应答不冲/**载荷全键**/终态/未见配置就点应用必须拒）。
    **写页面断言的两个坑**：① 手搓的"形状合法"JPEG 没有 SOS 段 ⇒ Qt
    报 `missing SOS marker` 解码失败（协议面能过、页面面必红）⇒ 页面
    测试要用 `QImage.save(..., "JPG")` 现画真图；② `check(name, ok, detail)`
    的 detail 值若是**元组**，`"%r" % (t,)` 与 `"%r" % t` 天差地别
    （后者 `TypeError: not all arguments converted`，整段异常退出）。
  · `verify/webui/webui_check.py`：18 页 + 16 导航 / 相机页 17 且
    在设备组下且订阅 `0x0001000C` / 页 16 是保留槽位且不在导航里。
    **注意 `/api/pages` 的 `nav` 每项是 dict**（`{"caption","page",
    "childOf"}`）不是元组 ⇒ 按下标取会 `KeyError: 0` **整段异常退出**
    （踩过，实测 PASS 346/FAIL 1）。

  **⑪ 未做/挂账**
  · **C 端客户端不做**（用户 2026-11-19 明确「c端不用改」）：C 端只占
    协议号与配置键名，`client/ui/` 零改动。
  · `webui_check.py` 里没有「`/api/camera/frame` 真返回 image/jpeg」的
    断言（需要真相机，CI 机器上没有）⇒ 由方案文档 §10.4 的手工实测覆盖。
  · 设计稿/实测数据/实施差异全表见
    `docs/HSF相机RTSP接入_设计方案_20261119.md`（§0.0 决策、§5 带宽、
    §10 **按实现订正**）。
- **相机板端部署与性能（2026-11-19 16:18~16:28 真机验收）**：
  **部署面**：相机功能全在 **Python 栈**（`hsfsrv` / `hsfweb` /
  `hsfsrv_client` 三个包）⇒ 板端部署 = tar 打包 scp 上板 →
  `rm -rf <pkg> && tar xzf` 替换 → 写 cfg 的 `camera` 节 → 重启
  `hsfserverpy`/`hsfwebui`/`hsfclientpy`。**C 端 `HsfClient` 不动**
  （用户口径「c端不用改」）。
  **三个必踩的坑（都实锅）**：
  ① **改盘上的 cfg 必须先停服务**：服务端在配置保存时会把**内存里的值
     写回 cfg** ⇒ 运行中直接改文件再重启，改动被旧值覆盖（实测：改成
     2fps 后重启，`gst` 命令行仍是 `framerate=10/1`）。正确顺序 =
     `systemctl stop hsfserverpy` → 改 cfg → `start`。
  ② **一次 UI「应用」就重建一条 gst 管道**：连点三下「应用（需管理员）」
     ⇒ 三条「远程配置：相机已更新」⇒ 采集器反复 teardown/重建（期间
     短暂 0 帧）。`apply_settings` 目前对「值没变」也重启，现场别连点；
     后续可加「配置无变化则跳过重启」。
  ③ **UI 回显的值就是提交的值**：做性能/画面口径测量前先 `cat cfg` 确认
     fps/quality 真是你以为那档（本轮误点「应用」把 2fps/q80 改成了
     10fps/q50，CPU 读数一度偏高）。
  **性能口径（缺省 704²@2fps/q80，RK3568 四核实测）**：
  ~~gst 子进程 56~58% 单核 + 服务端进程 35~36% 单核~~ **2026-11-20 订正
  （抽帧前置后）**：gst **31~47% 单核**（5fps 93%、10fps 176%），整机负载
  1.1~1.7。**旧测里「调 fps 对 CPU 几乎没用」只对旧管线成立**——那时
  `videoconvert` 在 `videorate` 之前、给相机全帧率做 CSC，fps 自然改不动
  CPU；**抽帧前置后 `fps` 是第一号 CPU 旋钮**（≈线性）。另两个旋钮：
  **相机端分辨率**（∝ 像素数）、**切 `transport=stream`**（gst 整个不启，
  服务端近零 CPU，但要浏览器有 WebCodecs）。
  **验收判据（可原样复用）**：`pgrep -a gst-launch-1.0` 看命令行里的
  `framerate=`/`quality=`/`format=` 是否与 cfg 一致（**这是配置真生效的
  唯一硬证据**，日志里的「采集已启动」只是打印配置值）；裸
  `XLinkClient` 订阅 `0x0001000C` 组帧存 JPEG 看头尾 `ffd8…ffd9` 与
  体积；网页端 `curl -b cookie /api/camera/frame` 看
  `200`/`image/jpeg`；板端 UI 用 `tap_inject.py` 点导航再
  `xwd -root -silent | verify/xwd2png.py` 看图。
- **相机架构转向：画面直传（H.264/H.265 ES，2026-11-19 用户定案推翻 JPEG 转发）**：
  用户点破设计根基："应该直接传输 H.264，很多网站就是直接传 H.264"——当初
  "浏览器不能播"的前提是错的：浏览器播不了**裸 ES/RTP**，但能经 **WebCodecs /
  MSE** 播 H.264（网站同款）；C 端约束已随「c 端不用改」消失 ⇒ 转向成立。
  **transport="stream"（缺省）**：服务端 RtspClient 拉到的 Annex-B NAL
  **不解码直接转发**，gst 子进程整个不启 ⇒ 服务端相机 CPU 从 ~90% 单核归零
  （板端实测 gst 58%+服务端 34%），帧率从 JPEG 抽帧的 2fps 上到相机自身
  20fps；**transport="jpeg"（旧路兜底）保留**，远端设置可切。
  **H.265**：相机两条码流都能切 H265（能力表实测）⇒ 配置键 `codec=h264/h265`
  ；子码流 512K→~256K（外网带宽场景）；**浏览器 H.265 只有带硬解的
  Chrome/Edge/Safari** ⇒ 页面 isConfigSupported 探测、不支持明确提示。
  **协议**：分片 flags 加 bit2=ES（载荷是编码流 NAL，frameId=NAL 序号、宽高
  0）、bit3=H265；camera 配置节三新键 `transport/codec/hwDecode`（四份 proto
  同步）。**hwDecode 是客户端解码方式**（auto=按机器 GPU 试 qsv/cuvid/amf，
  不行回落软解 / on / off）——也进远端设置（用户口径「相机可编辑的都进远端
  设置」），管理员一处配置全端统一。
  **服务端要点**：① GOP 缓冲（参数集起、IDR 重开、**参数集不能互删**——
  SPS/PPS 是两条独立 NAL，PPS 到来把 SPS 清了 ⇒ 转发出去的 AU 缺 SPS）；
  新订阅者补发**整个最近 GOP**（参数集+IDR 自举），订阅沿判据
  `_camera_subs_last`（补发与发布协程共用一处置位防重复）；GOP 超 512KB
  清空但**保持就绪**（参数集只出现一次的流靠 IDR 重建）。②
  `_publish_camera_frame` ES 分支按 NAL 切片推（flags ES|H265）。
  **桌面端**：`esplayer.EsDecoder`（独立线程）——**三个必踩的坑（diag5~8
  实抓）**：① **硬解后端名双拼**：`_pick_decoder` 传 `h264_qsv`、
  `_open_decoder` 又拼一层 ⇒ `h264_h264_qsv` 不存在 ⇒ 全部硬解静默失败
  （本机有 Intel 核显，qsv 本可用）；② **回落死循环**：FFError 回落把内部
  `_hw_mode` 改 off，bridge 每片重发 configure(auto) 相对内部值恒"变化"
  ⇒ 每片 rebuild ⇒ 解码器刚建就被拆永远黑屏 ⇒ **外部配置 `_hw_requested`
  与内部有效值 `_hw_mode` 分离**（回落只改后者）；③ **AU 聚合**：硬解逐
  NAL 喂不出帧，参数集+IDR 拼**整包**再喂；且 **PPS 不能清掉 SPS**（同
  服务端）；**且攒着的参数集/SEI 单独 flush 会被当残包拒**（SPS-only 28B
  包直接 InvalidData ⇒ 永远出不了首帧）。PyAV 19 的坑：异常基类
  `av.error.FFmpegError`（av.AVError 别名已删）；**无容器直连 libx264 时
  slice 包是 AVCC** ⇒ 测试造流必须走 raw h264 **容器**；x264 Annex-B
  **只有首个 NAL 用 4 字节起始码、后续 3 字节** ⇒ 切 NAL 必须 find
  `\x00\x00\x01`。**GPU 硬解实测（本机 Intel UHD 770）**：h264_qsv 出帧且
  to_ndarray 可取像素；cuvid/amf 在无对应 GPU 的机器 open 报错（干净失败
  ⇒ auto 回落必要）；**704²@20fps 软解只要 0.47ms/帧** ⇒ 桌面硬解收益
  本来就小，真价值在未来主码流 2528² H.265。
  **网页端**：`/api/camera/es?cursor=N` 增量端点（二进制：cursor/codec/
  resync/count + NAL；环形缓冲 1MB，游标落后回 resync=1）；page17 双模
  （jpeg→img 轮询 / es→WebCodecs VideoDecoder + canvas，1500ms 兜底轮询
  改 ES_FETCH 120ms 增量拉）。**IAB 应用内浏览器内核无 H.264 解码管线**
  （自编码自解码也 Decoder error，diag 实锤）⇒ 页面 5 秒无帧给**可操作**
  降级提示（换正式 Chrome/Edge 或切回 JPEG），不黑屏装死；真浏览器
  Chrome/Edge 是现场验收环境。**全屏（用户口径「类似网站视频播放界面」）**
  ：桌面=独立全屏覆盖窗（黑底铺满 + 100ms 自刷 + Esc/双击/退出钮，与页面
  共享 model 不另拉流）；网页=stage.requestFullscreen + CSS fullscreen 规则。
  **回归锁**：服务端 `test_camera.py` **F 段 9 项**（GOP 缓冲/订阅门控/
  ES·H265 位/订阅沿补发）⇒ **57/0**；客户端 §37 **E 段**（AU 聚合真解码/
  后端名无双拼/configure 幂等）+ **F 段**（全屏建立/贴图/关清引用）+
  **G 段**（ES meta 不显示 KB——RGB 字节是内存量不是传输量）⇒ **35/0**；
  全量客户端 **1501/0**。本机端到端（假 ES 源 = PyAV 实时编码 → 服务端
  直传 → 桌面 qsv 出帧 1864 → meta/全屏全对）真机复测待板子开机（真相机
  GOP/IDR 间隔待实测）。
- **相机导航进设备组 + 折叠把手挂页头 + 表格叠列双端修复（2026-11-19 晚，用户逐条反馈）**：
  ① **导航**：「相机」从 NAV_TREE 顶层末尾挪到**设备组子菜单末位**（电动球阀
  之后；网页端本来就在设备组下）。**页号 17 契约不变**（只动显示分组，不
  动页号）。⇒ 客户端 NAV_TREE 不再是「C 端前缀 + 末尾追加」结构 ⇒ 跨端
  同形断言从「前 15 项 == C 端前缀」改为「**滤掉本端扩展项（相机/Web 设置）
  后与 C 端 15 项顺序一致**」（test_hsf_regression §B）。数据三页折叠把手
  同轮挪走 ⇒ web 三页的 nav 断言不受影响（网页端本无 Web 设置节点）。
  ② **数据三页折叠把手挂页头标题行右侧**（用户口径「放第一行标题栏右边，
  白白占了一行」）：web `dataToolbar` 加 `o.foldHost`（页面传标题行右侧
  宿主，把手 `dt-fold-inline` 样式挂那里、不再建独立 `dt-fold-row`）；
  桌面 `DataPageToolbar` 加 `fold_btn_host`（QLayout）同款——**handle
  容器不再加入工具条**（setVisible(False) 只当按钮的逻辑父级），btn 被
  页头 QLayout addWidget 自动 reparent，`set_collapsed` 等接口照旧。三页
  （历史/日志/报警）两端统一。
  ③ **表格叠列双端修复**（用户实报「两列的数据叠在一起」）：
  · web 历史页 `COL_MIN_TEXTS` 补 **DI/DO**（数据 `0x%08X` 10 字符比表头
    `DI(32位)` 6 字符长 ⇒ fixed 布局按表头量宽分到的列宽装不下 ⇒ nowrap
    内容**溢出画到隔壁列**＝叠列）；
  · 桌面历史页 `auto_min_texts` 实测补 **瞬时流量/正累计**（桌面 15pt×
    缩放下 "3.66 m³/h" 要 196px、分配 110px ⇒ 数据裁半截贴着列线，视觉
    ＝叠列）+ DI/DO 同款。
  **通用教训：凡「数据比表头长」的列都要进内容量宽样本表（两端同步）**；
  逐列量法：web 用 `td.scrollWidth > td.clientWidth`（真溢出判据；把
  padding 加两遍会全部假阳性），桌面量 `headerView.sectionSize(i)` vs
  `fontMetrics.horizontalAdvance(内容)+16`。
  **验证**：web 历史页 scrollWidth==clientWidth 全列零溢出 + 截图
  DI/DO 分列清晰；桌面 offscreen 16 列「分配≥需要」零溢出；数据三页+
  工具条回归 530/0；全量客户端 1501/0（camera 段 35）、webui_check 348/0、
  服务端 test_camera 57/0（F 段 ES 直传 9 项）。
  ④ **桌面相机页 50ms 快速贴拍**（用户复报「还是有卡顿」分层实测后）：
  主循环 refresh 是 200ms 全页共用节拍 ⇒ 直传 20fps 下贴图率被压到
  4.5fps（L2 解码 19.6fps vs L3 贴图 91 次/20s，L2/L3=4.3）⇒ 相机页加
  50ms QTimer 调 refresh（key 没变早退；JPEG 2fps 源多跑的只是 key 比较，
  无害）。**分层实测法**（可复用）：同一次测量量 L1 NAL 到达 / L2 解码
  输出（on_frame 时戳）/ L3 贴图（_last_key 变化时刻）三层间隔分布，
  σ 对比定位抖动产生层。50ms 上限显示 ≥12fps；「推送事件驱动贴图」仍被
  否决（突发连刷 + 空档，见 main_window 否决注）。
- **WebCodecs H.264 直传三坑 + 网页端 H.265 支持（2026-11-19 深夜，用户「Edge 不支持不可能」推翻环境限制结论）**：
  上网查证（W3C AVC registration / Chromium issue / SO）+ 实验矩阵实锤，
  Edge/Chrome 一直支持 H.264——**是网页播放器的 avcC 构造有三个 bug**：
  ① **numOfPictureParameterSets 是整字节 0x01**，不是 SPS 计数那种带 111
  前缀的 0xE1（0xE1=225；纯参数集记录 Chromium 恰好容忍，High profile
  一追加扩展字节就爆）；② **High profile（0x64…）必须在 PPS 后追加 4 字
  节** chroma_format_idc(1)/bit_depth_luma_minus8(0)/bit_depth_chroma_minus8(0)/
  numOfSequenceParameterSetsExt(0)——缺 extCount 字节（39 字节记录）照样
  "Failed to parse avcC"；③ **codec 串 hex 每字节补零**（SPS[2]=0x00 拼
  成 7 位 = 非法 codec）。修完 IAB 与用户 Edge **都出真相机画面**
  （IAB 帧 1793 / Edge 帧 406 截图留档）。**教训：实测「环境限制」前先
  用字节级标准记录核对——「自编码自解码也失败」同样可能只是 desc 构造
  错**。规范依据：`avc1.*` 无 description=Annex B 在 Chromium 有已知坑
  （configure 成功 decode 无输出）；`avc3.*`=参数集在带内（issue #857，
  浏览器实现参差）⇒ **AVCC desc 模式是正路**。
  **网页端 H.265 已支持**（用户追问「265 支持吗」）：Edge/Chrome 107+
  HEVC **仅硬件解码**（GPU 要有解码块；Edge 另需 HEVC Video Extensions；
  本机 Intel UHD 770 硬解路径实测 true，prefer-software=false）⇒
  运行时 isConfigSupported 探测；播放器实现 **hvcC 构造**
  （buildHvcCAndCodec：general_profile/tier/space、compatibility 4 字节、
  constraint 6 字节、level 全从 SPS 的 profile_tier_level（NAL header 后
  1..12 字节）拷贝；numOfArrays=3（VPS/SPS/PPS）；lengthSizeMinusOne=3；
  **configure 失败自动 hev1↔hvc1 互换**）；libx265 合成流端到端实测
  IAB 出画面（帧 937 截图）。**相机切 H.265 的障碍仍是 /ds 私有加密
  API**（写配置 401）——浏览器侧已就绪。
  **服务端审查四修（用户要求全量检查）**：① `_publish_camera_es`
  无订阅拍 **drain 丢弃 pending**（攒着会在订阅沿把过期 NAL 一股脑发
  出去——白耗 0.5MB 带宽）；② `_h_camera_frame`(0x011C) ES 分支
  **只发 GOP 不 drain pending**（原来偷走订阅者的增量 ⇒ 丢帧）；
  ③ `stop_camera` 复位 `_camera_subs_last`（重启采集后已开页面拿不到
  GOP 黑屏到下次订阅沿）；④ `camera.py stop/_restart` join 超时
  **盖过 recv 阻塞窗**（timeoutMs+2s，原 3s 等不到旧线程 ⇒ 两线程
  共抢一个 RTSP 会话）。**ES 模式 frames=NAL 计数**（诚实口径）⇒
  客户端/网页相机卡按 actualTransport 区分文案（已收 N NAL / 已收 N 帧）。
  回归：test_camera 57/0（F2 断言改「pending 丢弃」新契约）+
  §37 相机段 37/0（D6 文案双口径断言）+ 全量客户端 1503/0 +
  webui_check 348/0。最终截图：IAB 相机（帧 1793）/ IAB H.265（帧 937）/
  桌面相机页（qsv 硬解）/ 桌面历史页（3054 行）/ 用户 Edge（帧 406）。
- **web 历史页列数据重叠根修：列宽下限改为「按内容量」（2026-11-20 用户
  「web端列数据重叠」截图）**：
  ① **现象**：历史表「模式」列的值（`自动·产水`）与「搅拌状态」列的值
  （`正转运行`）糊在一起、没有间隙。
  ② **根因**：表是 `table-layout:fixed` + `td{white-space:nowrap}`，列宽
  下限只按**表头文字**量（`COL_MIN_TEXTS` 只补了时间/流量/DI/DO 5 列）
  ⇒ 「模式」表头 2 字 → 下限 72px，而内容是「自动·运行中」6 字
  （20px 字体实测 109px）⇒ **装不下就是溢出画到隔壁格**（fixed 布局
  不会自己撑开）。**2026-11-19 只往静态样本表里补 DI/DO 是治标**——
  数据一变长（模式列随运行子阶段变化）就又叠。
  ③ **修法（对齐桌面参考端，不是再补一行静态表）**：移植
  `page_base._measure_content_texts` 的口径 —— `page09_history.js` 新增
  `measureContentTexts(cols, list, font)`（**逐页量当前页最长单元格
  文字**，按 th 的现算字体量，`--`/空跳过；带 `font\0text` 宽度缓存，
  4096 条上限）+ `applyColWidths(cols, ths, list)` 下限改为
  `max(72, 表头文字+30, 内容文字+18)`；`测宽 18`（左右 padding 16 +
  边框余量 2，**内容没有排序指示器**，不能跟表头的 30 混用）。
  `COL_MIN_TEXTS` 保留为**稳定基线**（补 `1: "自动·运行中"`）：模式
  文本随子阶段变长变短，只靠现量会让列宽每 3s 抖一次。
  ④ **验证**：真浏览器（IAB 1352×680 + 窄屏 480×900，真 sim 自动产水
  入库的「自动·产水/正转运行」行）逐格判 `td.scrollWidth-clientWidth`
  ⇒ **溢出 0**；模式列 139px（> 内容 109+18）、搅拌状态 110px，间距
  清晰；窄屏同样 0 溢出（表 2345px 在 417px 容器里横向滚动）。A/B 量化：
  旧下限 72 ⇒ 内容可用 56px < 数据 89/109px（必然叠列）。截图留证
  `verify/webui/shots/history_col_no_overlap_20261120.png`（排序/每页 50/
  图表视图往返后同样 0 溢出、浏览器控制台零报错）。
  **代价口径**：下限抬升后表更宽、横向滚动更多 —— 这是本项目既定取舍
  （`theme.css` 注释：宁出横向滚动条，不截断/不叠列；桌面端
  `set_content_floors(True)` 同款）。
  ⑤ **回归锁**：`verify/webui/webui_check.py` 新增
  `_history_col_width_static()`（3 检：有 `measureContentTexts` /
  `applyColWidths` 拿到行数据 / 模式列静态基线），挂在 `sec_history`
  末尾 ⇒ 任何一件被回退都会红；本轮 `--server-port 6200` 全量
  **361/0**。
  ⑥ **板端已部署（2026-11-20，用户问「web修复有部署到板端吗」）**：
  板上网页端是独立单元 `hsfwebui.service`（`/root/Desktop/XinYueC/hsf_webui`，
  `python3 -m hsfweb --server 127.0.0.1 --port 6100 --web-port 8080`）⇒
  本次只需替换 `hsfweb/static/js/pages/page09_history.js` 一个文件
  （与板上原件的 diff 只有本修复，123 行）；**静态响应带
  `Cache-Control: no-cache`**（逐次校验）⇒ 无需重启服务、浏览器刷新即生效。
  部署前留备份 `page09_history.js.bak-20261120`（板端）。验证：板端网页
  服务的文件 md5 == 本地（`df01924b…`）；真浏览器打
  `192.168.1.249:8080` 历史页（板库真实数据「自动·顺洗/反转运行」）
  逐格量 `scrollWidth-clientWidth` ⇒ **0 溢出**、模式列 139px；截图
  `verify/webui/shots/board_history_col_no_overlap_20261120.png`。
  **通用教训**：`fixed` 布局里「表头量宽」永远只能当**基线**，
  「数据比表头长的列」这类缺陷必须**按内容量**（否则每加一个文本列就
  复发一次）；逐列溢出判据仍是 `td.scrollWidth > td.clientWidth`。

- **相机「重启相机后画面就没了」= 五层根因叠加（2026-11-20 板端实锅，
  缺一层都修不好）**：
  ① **静默空转**：`read_es` 的 `except socket.timeout: continue` 是**永久
     空转**——相机重启时 TCP 半死，收不到 FIN/RST ⇒ 采集线程卡在"等数据"
     里、重连循环永远轮不到，**零日志零退出**。⇒ `RTSP_SILENT_TIMEOUT_S
     = 30.0`（**10s 太短**：板端 WiFi -75dBm/15% 丢包/RTT 300ms 抖动下，
     丢包重传自愈要十几秒，10s 会把刚播起来的会话反复砍掉）。
  ② **会话槽堆满**：重连太密 ⇒ 相机侧残留会话占满（TP-LINK 只有 **2~4
     个**）⇒ 相机**接受 PLAY 却不推流**（"能握手没画面"）。⇒ 失败按
     `base × 2^(streak-1)`（封顶 8×）退避 + `teardown_stale()` **用一条
     新连接**给旧 session 发 TEARDOWN（死 socket 上的 TEARDOWN 发不出去，
     等于没清）。
  ③ **管线构建失败静默**：gst 参数不合（如 `jpegenc` 吃 NV12）⇒ 子进程
     **立即退出** ⇒ `_pump` 往死管道写走 dead 分支**静默返回**、零日志、
     无休止重连。⇒ 见上文管线三条硬约束；判据是 `pgrep -a gst-launch`。
  ④ **采集线程静默死亡**：异常没被兜住 ⇒ 进程活着、健康照推、线程没了。
     ⇒ `_run` 外层 `except BaseException` 留痕 + `alive()/ensure_alive()`
     看门狗（App 每轮遥测拍调一次，死了就拉起）。**看门狗装上当天就抓到
     第 6 个 bug**：多行补丁漏了 `stale_session = ""` 初始化 ⇒
     `UnboundLocalError` 把线程打死——"静默死亡"的活样本。
  ⑤ **Windows `arp -a` 是 GBK**：`text=True` 按 UTF-8 解 ⇒ reader 线程
     `UnicodeDecodeError` ⇒ MAC 永远解析不到（Linux 侧用 `/proc/net/arp`，
     没这问题，**所以本机测不出、板端才显形**）。⇒ 拿 bytes 按
     `mbcs/gbk/utf-8` 依次解。
  **回归锁**：`server_py/tests/test_camera_mac.py` E1~E4（静默判死/GBK 解码/
  会话清理/退避）。

- **相机 MAC 解析必须扫「本机所有网段」+ 可达性验证（2026-11-20 实锅：
  相机换网线后 MAC 没变，IP 从 `192.168.1.181` 变成 `10.168.1.101`）**：
  旧实现只按「配置 IP 所在 /24」做 ARP + 扫网段 ⇒ 相机换到 eth0 网段后
  **永远扫不到** ⇒ 日志「MAC 未解析到 IP ⇒ 暂用配置 IP 连接」→
  `No route to host`。修法（`server_py/hsfsrv/camera_mac.py`）：
  ① `local_ipv4s()` 枚举本机**全部**非环回 IPv4（解析 `ip -4 -o addr`；
     本板三网口 eth0=10.168.1.100 / eth1=192.168.0.10 / wlan0=192.168.1.249）；
  ② `resolve_mac_ip(seed_ip=…)` 按「seed 优先 + 其余网段依次」逐个
     `sweep_subnet`（对 /24 做 554/80 TCP 探测，**借内核填 ARP**），命中即返回；
  ③ 结果先过 `_probe_reachable()`（**ARP 里可能残留旧网段的同 MAC 条目**，
     TCP 不可达就继续找）；
  ④ 全失败且配置里有 IP ⇒ 先按 IP 连（连上后内核建 ARP，下轮恢复 MAC）；
  ⑤ **解析结果变化时打一条** `相机地址解析：MAC xx → IP`（现场唯一证据，
     没有这条日志时"到底连的哪台"无从判断）。
  **通用**：任何"名字/标识 → 地址"的解析，**不许只扫一个网段**；解析结果
  必须**验证可达**再采用。

- **触摸屏全屏必须做「主窗内覆盖层」，独立顶层窗会卡死 Qt 触摸状态机
  （2026-11-20 板端实锅，鼠标路径复现不出来）**：板端是 ILITEK 触摸屏
  （`/dev/input/event3`）。相机页原先用独立 `Qt.Window + showFullScreen()`
  ⇒ **退出全屏后主窗点任何控件都无响应**：触摸序列进行中把那个顶层窗
  close/hide ⇒ Qt 认为它仍持隐式 grab + 主窗收不到 `mouseRelease` ⇒ 后续
  点击全被当"拖拽中"。**本机离屏 + 鼠标事件完全复现不出来**（第一版
  「延迟 close + releaseMouse」对鼠标有效、用户仍报"还是无法操作"）。
  修法：`_FullscreenPlayer` 改成 `parent = page.window()` 的**普通子控件**——
  `enter()` = `setGeometry(host.rect()) + show + raise_ + setFocus`；
  `exit_fs()` = `hide + releaseMouse/Keyboard + **补发一次合成
  MouseButtonRelease** + raise_/activateWindow + 交还焦点`；
  双击/Esc/退出钮一律 `QTimer.singleShot(0, exit_fs)`（**绝不在
  `mouseDoubleClickEvent`/`keyPressEvent` 里同步切窗口**，那会吞 release）；
  主窗 resize 用 `eventFilter` 跟随。
  **通用铁律**：**顶层窗的显隐不参与触摸/鼠标序列的收尾** —— 要一个
  "盖住全窗"的东西，用同 QWindow 的子控件，不用第二个窗口。
  回归锁：`client_py/tests/_fs_exit_check.py`（6 检：同 QWindow / 退出后
  覆盖层隐藏且不遮挡下层 / 无残留 grab / 主窗可命中）。

- **`--shot` / `--seconds` 不许写共享设置（2026-11-20 实锅，坑了一个真
  用户现场）**：离屏验证脚本 `--shot --size 1280x4200` 退出时把窗口尺寸
  **写进了客户端的共享 INI** ⇒ 用户下次开真实窗口恢复成 749×4200 ⇒
  主页弹层"按页高居中"落到视口外 ⇒ **点「设备健康」弹不出卡片**
  （用户实报，排查方向一度全歪到弹层布局）。修法：`__main__` 的 teardown
  保存 `winSize` 前判 **headless**（`--shot`/`--seconds` 路径**一个字节都
  不落盘**）。
  **配套**：浮层居中要按**可见区带**（`availableGeometry`）而不是
  `page.height()`（页面比视口高时按页高算会落到屏外）；高度取布局真实
  `sizeHint` + 亏空要补**外壳**的 `minimumHeight`（外壳是 `Fixed` 策略，
  只管浮层不管外壳 ⇒ 末行被裁半行就是它）。
  **通用**：**验证工具的副作用要么不落盘，要么落独立文件**；任何"测量
  脚本把人机器改坏了"的 bug，都比被测功能本身的 bug 更贵。

- **凡「数据比表头长」的表格列都要进内容下限表（2026-11-20 桌面端实锅，
  网页端早有一份，桌面端漏了）**：历史页「模式」列表头 2 字、内容最长
  「自动·运行中」6 字符 ⇒ 列宽按下限算出来截成「自动…」。
  ⇒ 桌面端 `auto_min_texts` 补 `1: "自动·运行中"`；**网页端
  `COL_MIN_TEXTS` 早就有一模一样的项**——两端同形缺陷的典型形态是
  「一端修过、另一端没看」⇒ 改这类"按表头量宽"的地方，**先对端 grep 一遍**。
  回归锁：客户端 history 段 **E1b**（模式列宽 ≥ 「自动·运行中」实测宽度）。

- **左上角 ☰ 导航方式（2026-11-22 用户定案，桌面 Python + web 两端）**：
  本地设置「悬浮窗 → 导航方式」两档（键 `menuButtonStyle` / web 同名
  localStorage 键）：
  · `tree`（缺省）= 宽屏收起/展开左侧导航树；窄屏仍开抽屉/菜单页；
  · `page` = 点 ☰ 打开**当前页所属分类的子菜单页**（如数字量 →「设备
    选择子项」），在子菜单页再点 ☰ 回主菜单；**菜单页档强制隐藏左侧
    导航树**（「显示左侧导航栏」开关置灰、值保留，切回 tree 档恢复）。
  **主菜单必须自带「主界面」入口**（4 项 2×2）——菜单页档没有导航树，
  这是回首页的唯一入口；子菜单页右侧「返回」按钮回主菜单。
  **一/二级按钮同档**：同一份 `MENU_BTN_QSS`（36px 粗体 + QSS
  `min-height:80px` ⇒ 实际 94px；QSS min-height 会压过
  `setMinimumHeight`，plain 按钮只能用 QSS 杠杆）。子菜单 2 列、7 项设备
  子项超出视口时由页面 QScrollArea 出**垂直滚动条**，不横向撑宽。
  **web 两个坑**：① 菜单页档隐藏导航树**不能用 `hide-nav`**——≤520px
  的 `body.hide-nav .tb-menu-btn{display:none}` 会把 ☰ 一起藏掉（用户实报
  「导航图标没了」）⇒ 用独立 `nav-page-mode`（只收 #nav-panel，保留 ☰）；
  ② 首页三排原本各是独立 3 列网格，≤1023px 降 2 列后每排第 3 张折行
  ⇒ 右侧留空（压差/电动球阀各占半排）⇒ 该断点把 `.home-page` 变统一
  2 列网格、`.home-cards{display:contents}` 让 9 张卡连续配对、最近报警
  横跨两列；窄档标题栏产品名 `text-align:center`。
  回归锁：客户端 `test_settings_merge.py` **D5d/D5e**（两档/导航显隐/
  返回）、`test_hsf_regression.py` **§39-B2/B5**（主菜单含主界面且真回 0）；
  web `settings_merge_check.py` **G5~G9**（控件/落盘/菜单页/返回/☰ 保留/
  导航恢复）。

- **顶部状态栏相机徽章 + 悬浮画面卡（2026-11-23 用户定案，客户端 + web）**：
  ① 标题栏只放**相机位图徽章**（QPainter/SVG 自绘，不引资源），不放实时
     缩略图 ⇒ 平时零相机流量；本地设置「标题栏内容」新增 `showCamera`
     （缺省开，两端同名键）。
  ② 点徽章弹**可拖动、可拖边角缩放**的悬浮卡显示相机画面；**再点徽章关闭**。
     每次打开重新创建、关闭销毁：相机主题只在卡片存在期间订阅，关闭立即
     退订 ⇒ 后台不再传 20fps 大帧（用户口径）。
  ③ 客户端：`CameraPopupCard`（Qt.Tool + Frameless + WA_DeleteOnClose；
     边缘 8px 命中缩放、其余拖动；200ms 按 `model.camera.seq` 贴图，JPEG
     走 `QImage.fromData`、ES 走 RGB 裸帧 `QImage(...,w*3,Format_RGB888)`；
     **必须 `img.copy()`**，否则 model 换帧后悬空）。MainWindow
     `_apply_topics()` = 页面主题 ∪ GLOBAL ∪（卡存在时的 `TOPIC_CAMERA_FRAME`），
     `closed` + `destroyed` 双保险清引用/退订（WA_DeleteOnClose 后残留
     Python 引用会让下次点击 `close()` 抛 RuntimeError）。
  ④ web：`page17_camera.js` 的 `mountPage(host, compact)` 暴露为
     `HSF.cameraViewMount`（compact 只建画面区，复用同一套 JPEG/WebCodecs
     ES 解码）；`store._extraTopics` + `addExtraTopic/removeExtraTopic`
     管临时订阅；`app.toggleCameraPreview` 建 `.camera-float`，pointer
     events 拖动 + 边角缩放，关闭时 `cleanup()` + 移除主题。
  ⑤ 回归锁：客户端 `test_settings_merge.py` **D5f/D5g**（显隐/开关/每次
     重新创建）；web `settings_merge_check.py` **G4b~G4f**（显隐/开卡订流/
     拖动/缩放/关卡退订/重开新建）+ `webui_layout_check.py` 手机档
     44px 触达（`.tb-camera-btn` ≤860 提到 44×44）。


- **桌面渲染后端缺省：VK → GL → 软件（2026-11-23 用户定案）**：
  · `XGuiConfig.h` 的 `XGPU_RUNTIME_DEFAULT_ON` 缺省改为 **Windows=1 /
    其它平台=0**（非 Windows 仍保持 2026-11-01 板端订正「默认软件」，
    板端 GPU 直通 CPU 是软件 2~4 倍；需要时用 env/命令行显式打开）。
  · `xgpu_driver_type()` 未设置 env 时返回 **Vulkan**；`vulkan` 显式=VK；
    其余显式值（gpu/opengl/1/true/on）仍=GL（门禁/历史脚本以 `gpu` 表示
    GL，逐位语义不能改）。`xgpu_create_ex` 已有 VK 失败 → GL 有序回退，
    GL 也失败返回 NULL（上层软件光栅）。
  · 验证：`XGPU_PROF=1` 打印 `[xgpu-prof] driver=vulkan|opengl`；门禁里
    `XGUI_RENDER_BACKEND=gpu` 仍钉 GL、`software` 钉软件。
- **VK 渲染驱动三个实锅（2026-11-23）**：
  ① **手写 SPIR-V（无 glslang）**：操作码号必须逐条对规范——`OpFSub` 是
     **131**（130 是 `OpISub`，写成 130 时模块仍能装配、浮点减法按整数位
     模式算 ⇒ AA 覆盖率恒 0.25、dMean 3.879%，极难从像素反推）；
     `OpConstant` 必须落在类型/常量段（函数体里发常量 ⇒ 总线管线创建失败，
     驱动打 `aa-line pipeline unavailable` 静默回退 CPU）；VK NDC y 向下，
     顶点用 `2y/h-1`（照抄 GL 的 `1-2y/h` ⇒ 整线上下镜像）。
     排查：`XGPU_VK_TRACE=1` 看上传路径/缓存命中，`XGPU_QUAD_PROF=1` 看
     `cache=hits/uploads`。
  ② **帧内 staging 与回退顺序**：一帧多张不同尺寸图必须全部走纹理身份
     缓存上传；`xvkl_frame_stage_ensure` 原先「游标非 0 只记需求、回退
     三段式」⇒ 首张之后全回退，而回退的 `xvkl_ensure_source_image` 又在
     suspend **之前**销毁源纹理 ⇒ 已录绘制引用被销毁图 ⇒ VK 大面积丢像素
     （AA 线 20 张只剩 1 张）。现：允许帧内换缓冲（2× 增长、旧缓冲按帧槽
     退休，下一次 beginFrame fence 后销毁）；三段式顺序改
     **suspend → ensure → transfer → resume**。别丢「游标 0 可直接销毁旧
     缓冲」的前提（本帧未引用）。
  ③ **上传只支持 4 字节/像素**：`xvkl_copy_rows_for_upload` 按 `width*4`
     拷贝，此前不判源格式 ⇒ **24 位 BMP（RGB888，行距 `(w*3+3)&~3`）** 的
     QSS 背景图错位（平铺不显示）+ 末行越界读 ⇒ memcpy AV（cdb 栈：
     memcpy←XMemcpy←xvkl_copy_rows_for_upload←xvkl_tex_cache_upload）。
     GL 非 32 位走逐像素慢路径故只在 VK 暴露。修法：上传前
     `xvkl_upload_source()` 用 `XImage_convertToFormat` 转 ARGB32
     （会话级复用缓冲，仅缓存未命中转换）+ 拷贝入口行距防御；缓存身份键
     仍用**源图**（转换图只做上传数据源，不能进身份键）。

- **VK 全 GPU 收口 + 性能超越软件光栅（2026-11-23 续，接上条）**：
  远端 `4a12f049` 之后本轮又修了五个真缺陷 / 落了两项性能根修，
  门禁 p1 **46 通过 / 0 失败**，整帧 **VK 206 / GL 180 / SW 148 FPS**。
  ① **VK 逐像素 dump 段错误**（门禁 `逐像素 sw vs vk：比对无输出`）：
     `xvkl_draw_image_uv_colors` / `xvkl_draw_image_region` 两处在
     `COMBINED_IMAGE_SAMPLER` 写描述符时 `sampler = 0`（VUID-01082 违规）
     ⇒ Intel 驱动 `vkUpdateDescriptorSets` 内解引用空指针
     （cdb：igvk64 ← vulkan_1 ← XPainter.c 旧 5717）。改走
     `xvkl_update_texture_descriptor[_sampler]` helper；平滑通道的线性
     描述符集随视图一并重写。**教训**：`sampler=0` 在部分驱动静默、
     部分驱动崩，逐像素门禁必须先能跑完。
  ② **路径裁剪 AA 被过期门控导向软件重放**：`painterPathDraw` 里
     「GPU + 路径裁剪 ⇒ 软件局部提交」的门控没随 M4a（掩码烘进覆盖率、
     drawAlphaBitmap 直绘）收口 ⇒ clip-path-aa 场景 VK 整块缺失
     （2037px）。现仅「多矩形区域 + 路径裁剪」保留兜底（scissor 表达
     不了区域并集），其余走烘焙通道，场景 0 硬差。
  ③ **一个 UI 帧 = 一个驱动帧**（性能第一根修）：keep-open 让同一 UI
     帧的多个 painter 共用一条驱动帧，但**没有任何一条腿真正
     endFrame/present**（`xvkl_present_to_window` 原样返回 true，只有
     顶点压力 50% 才被动收口）⇒ 整帧基准 beginFrame/present 仅
     **2.6 次/秒**（屏幕 2.6Hz）、每次 fence 等 22ms（累积 ~50 次全窗
     过绘）。改：`xvkl_present_to_window` 在 `m_recording` 时
     `xvkl_end_frame`（提交+present）；`XPainter_closeWindowFrame` 在
     flush 尾对 VK 后端**清画家侧 keep-open 记账**（否则下一 painter
     往已结束的帧写命令 ⇒ 全量软件回退）。
  ④ **帧环槽位双推进（K=2 收益为零 + 截图偶发黑帧）**：
     `begin_frame_impl` 与 `end_frame` **各推进一次** `m_frameSlot`
     ⇒ K=2 时 0→1→0 回环、第二槽永不启用。现象：截图页 0/4/7 偶发
     整幅黑（空帧 present 把快照刷黑）+ K=2 收益失真。帧环推进只留
     endFrame 一处；K 缺省翻 **2**（一 UI 帧一 present 后真实生效：
     111→126，字缓存复用后 169→206）。
  ⑤ **GPU 轮廓字形每帧重走 FreeType + 路径展平**（性能第二根修，
     最大头）：`painterGpuDrawOutlineGlyph` 只带 24 条轮廓路径缓存
     （线性扫、必然冲刷），每帧对每个字形重做「字库解码 → 曲线展平 →
     包围盒」；采样归因 FreeType（cf2_interpT2CharString 9.7% /
     FT_MulFix 5.9% / cf2_glyphpath_* ~14%）+ painterPathFlattenCubic
     占 paint ~40%。修法：复用 SW 侧 **字形灰度图缓存**
     （`painterGlyphAlphaCacheFind/Store`，16384 槽、位置无关键）——
     命中直接拿度量 + 位图矩形，**连 contours 都不建**；未命中照旧
     光栅化并把**原始覆盖**存回（对比曲线仍在上传前烘入）。键与 SW
     同口径（gridFit=false/italic/字重/noHint/scale），互不串味。
     整帧 VK 126→206、GL 118→180（GL 同享缓存）。
  ⑥ **恢复源回退**：`xvkl_window_restore_last_presented` 在
     `lastPresentedIndex == imageIndex`（K>1 + IMMEDIATE 更常见）时
     直接失败 ⇒ 调用方清屏黑。现回退到持久快照 `m_colorImage`
     （内容=最近提交帧；同队列提交序保证拷贝时已写完），布局簿记
     原样恢复。
  **门禁**：`gpu_stage_verify.sh` p1 新增「整帧 VK ≥ 软件×1.05」断言
  （全 GPU 架构目标，不是抗负载护栏）；`XGUI_VK_FRAME_TRACE=1` 可看
  begin 恢复/清屏决策与读回分支（槽位/图像号/快照布局）。
  逐像素一致性：14 场景里 13 个 0 硬差，draw-text 82px（与 GL 同源的
  字形 AA 量化差，阈值 82 恰好满格）。

- **VK 再优化一轮：字形可构建性判定 + 顶点发射 + 构建脚本（2026-11-23 续2）**：
  整帧 **VK 441 / GL 350 / SW 208 FPS**（VK = 2.1× 软件），p1 门禁全绿。
  ① **`painterOutlineGlyphBuildable` 每字形每帧重走字库解码**（最大头）：
     GPU/SW 文本循环里它先于 draw 调用，实现是
     `painterOutlineBuildPath(...)+XPainterPath_deinit`——只为回答
     「能不能画」。采样归因 FreeType（cf2_interpT2CharString 8.4% /
     cf2_glyphpath_hintPoint 5.2% / FT_MulFix 4.3% / FT_Outline_GetCBox
     2.4% / ps_builder_add_point 2.3% …）合计 ~28% 可见符号 + 未采到的
     路径分配/deinit。修法：灰度图缓存条目 = 「已成功光栅化过」⇒ 直接
     判可构建（零字库访问）；失败时存 **1x1 零覆盖负缓存**（零度量 ⇒
     调用方 advance 仍走既有代理/M 宽回退）；空白字形（contours==0）在
     GPU draw 路径也入缓存（真实度量 + 1x1 零覆盖）。效果：VK 206→441、
     GL 181→350、SW 148→208（三后端共享该函数）。
     **排查口诀**：文本页 CPU 高而字形缓存看似命中 ⇒ 查「判定/度量类」
     辅助函数是否仍在逐帧走字库；缓存命中不等于零字库访问。
  ② **顶点发射去除法**：NDC 变换 `x*2/w-1` 原每顶点两次除法
     （6 顶点/quad ⇒ 12 次），提到 quad 外一次算 `2/w、2/h`；
     `(float)c/255.0f` 全部改成 `*(1.0f/255.0f)`（Debug 无优化：
     divss→mulss）。文本页 ~2200 quad/帧 ⇒ 省 ~2.6 万次除法/帧（收益
     小，~1%；但与①叠加不冲突）。逐像素仍 82px（与 GL 同源量化差）。
  ③ **构建脚本 vcvars 自动探测**（`Tools/windows/gpu_build_targets.bat`）：
     原硬编码 `VS 18 Enterprise` 路径，换机即 FATAL。现按
     `VCVARS` env → `vswhere.exe`（`-latest -prerelease -products *`
     + VC Tools 组件，能找到 D 盘/Preview/BuildTools）→
     Program Files（2026/18/2022/2019/2017 × 五种 edition）→ PATH 顺序
     探测；本机实测命中 `D:\...\2022\Preview\...\vcvars64.bat`。
     **别再往这个脚本里写死安装路径**。
  ④ **静态层探针确定性（2026-11-23 门禁红归因，改的是测试夹具不是产品）**：
     性能大改后 `gpu_stage_verify.sh` 的「静态层 vulkan 自检」偶发
     `A/B hard=769 / 增量 hard=576 / builds=2`（约 50%）。定位证据：
     差异恒为**页面首行 (0,0)-(575,0) 一条 576px 行**（白 vs 品牌栏
     蓝 0x1f4e79），全窗 SW/VK 截图逐像素只差 401 个散点（字形量化，
     无结构差）⇒ 不是静态层内容/产品渲染问题，而是探针**取帧不确定**：
     ① 页面首行属于页面与应用栏的 1px 交界行，GPU/软件光栅的覆盖归属
     有既有量化差（跨后端层 dump 实测正好差这一行）；② 探针在帧泵
     poll 回调内直接读回，可能拿到上一拍未完成/仅局部重绘的帧；
     ③ 页内 LCD 数码管/顶栏时钟的 1s 定时器落在捕获窗口内会让内容
     合法变化。软件后端同样偶发（实测 1/6），即夹具竞态与后端无关。
     **修法（三处，均在 demo 探针内）**：捕获统一**跳过页面首行**
     （A/B、增量逐位、跨后端 dump 同步生效）；取帧前 `XWidget_update`
     整窗重绘 + 两次 `processEvents` 排空；探针起始冻结 LCD/时钟/
     悬浮层三个 1s 定时器（探针自身帧泵不动）。修后 VK 10/10 PASS、
     SW vs VK 层 dump **0 硬差**。**排查口诀**：探针单行全宽差异 +
     全窗截图无结构差 ⇒ 先怀疑取帧时序/边界行，别去查渲染主链。
  ⑤ **非纹理实心 quad 进合批**（2026-11-23 收尾）：`xvkl_record_quad_colored`
     原只给纹理 quad 合批，`drawSolidQuad`（斜线/多边形/圆角边等任意四角）
     一律 direct（每 quad 一次 bind+draw）——文本页实测 ~1600 次 direct
     draw/帧。现非纹理且**无逐顶点色**（渐变保持 direct）走
     `xvkl_solid_quad_batch_append`；**追加必须在入口的"先冲实心批"之前**
     （先冲则每 quad 一批，实测 2M flush/4s、合批失效）。批内 6 顶点与
     direct 4 顶点共用 1-2 对角线 ⇒ 逐像素一致（parity 仍 82px）。
     实测 batch draws 1600/帧 → **41/帧**（54 quad/draw），整帧 416~434 FPS。
  ⑥ **顶点发射全展开 + 有效变换缓存**（2026-11-23 收尾2，Debug 构建收益）：
     · `xvkl_solid_quad_batch_append` / `xvkl_tex_batch_append` /
       `record_quad_colored` 直绘路径原来都带 `xs[4]/ys[4]/order[6]` 栈数组
       + 6 顶点循环；MSVC Debug 的 `/RTC1` 对每个含栈数组的函数都插
       `_RTC_CheckStackVars`（采样占 CPU **8.7%**，这些函数是最热写入点）。
       改成四角标量 + 逐顶点显式展开（UV/颜色/顶点序逐位不变），
       顺带去掉循环。parity 仍 82px。
     · `painterEffectiveTransform` 每原语调用一次（含 view 变换两除法 +
       1~2 次 3x3 矩阵乘法；采样 `painterMatrixMultiply` 6.4% + 本体 4.6%）：
       加**文件级结果缓存**，键 = 函数读取的全部输入（world 矩阵 9 字段、
       window/viewport、dpr、world/view 两个开关位），任一变化即重算——
       不做失效钩子（避免漏挂 setter 的经典坑），比较链 20 个字段。
       注意：**改本函数读取的状态字段必须同步键**。
     · 实测 paint 2.68→2.25ms，整帧 VK 398→**450 FPS**（SW/GL 同受益）。
     · 任意四角实心 quad（drawSolidQuad）合批当时仍留在
       `XGPU_VK_SOLID_QUAD_BATCH` 开关后（缺省关）。**同日稍后已翻正为
       缺省开（同二进制 A/B 中位 +13~17%）——见文末 ⑪**；旧「Intel 本机
       无帧率收益」是 Debug 构建 + 「先冲批再追加」顺序缺陷修复前的口径，
       勿再引用。
     · 剩余热点（采样）：`_RTC_CheckStackVars` 8.1%（Debug 专属）、
       `painterRaster_drawLineDevice` 6.7%、`painterEffectiveTransform`
       5.7%（键比较链）、clip 应用 ~5%、状态栈/字库拷贝 ~4% ——再往下压
       属 Debug 常数项，Release 构建会自然消失。
  ⑦ **Release 构建基准 + 「裁剪下发去重」负结果**（2026-11-23 收尾3）：
     · Release（`out/build/x64-Release`，产物 `bin-release/`）整帧实测：
       **VK 1241~1253 FPS（0.80ms）/ GL 743~763 / SW 510~541** ⇒ VK =
       **2.4× 软件光栅**（Debug 门禁口径 416~450）。Debug 的剩余热点
       （`_RTC_CheckStackVars`、函数调用开销）在 Release 消失。
     · **试过但回退**：给 `setClipRect` 加「已下发裁剪」去重（backend 记
       scissor 代次、XPainter 文件级 memo，代次在帧边界/读回/上传/图像
       绘制/RT 栈后自增）。效果：Debug clip 调用 8.16M→3.01M 次/4.1s
       （-63%），但**总 clip 耗时 324.9→322.9ms 不变**；Release 反而
       1245 vs 1090（更慢，因代次 getter + 比较链 ≥ 被省掉的驱动同值
       早退）。**结论：驱动 `xvkl_set_clip_rect` 本身已有同值缓存，画师侧
       再去重是重复劳动**——不要再加。若将来要再去重，必须让「代次查询」
       零函数调用（内联读字段），并且用 Release 决策。
  ⑧ **构建/门禁的 Release 与探针夹具（2026-11-23）**：
     · `Tools/windows/gpu_build_targets.bat` 支持 `--release`（构建
       `out/build/x64-Release`、产物 `bin-release/`）；参数解析改为
       「先收集再 shift」——**`shift` 会改写 `%0` 使 `%~dp0` 变成第一个
       参数的 dp**（实测 `cd /d "%~dp0..\.."` 落到 F:\Code），脚本目录
       必须在任何 shift 之前存 `SCRIPT_DIR`。
     · `gpu_stage_verify.sh` 支持 `--release`：同一套正确性门禁，绝对
       fps 仅记录（Debug 基线不适用），「整帧 VK ≥ 软件×1.05」两种配置
       都强制。Release 实测 VK 1241~1253 / GL 743~763 / SW 510~541。
     · 静态层探针夹具确定性三处（demo）：捕获跳过页面首行（面板交界
       1px 既有量化差）、取帧前整窗重绘+两次排空事件、探针起始冻结
       LCD/时钟/悬浮层 1s 定时器。修前 VK ~50% 假红，修后 10/10 PASS。
     · 本轮完整记录见 `docs/xgui/2026-11-23-vulkan-perf-and-gate.md`。
     · **门禁/基准必须独占跑**：Release 门禁第一次与我自己并发的手工基准
       同时跑 ⇒ 3 条假红（page5/6「未拿到 profiler 聚合行」+ GL 整帧
       `fps=163553`——窗口没建好就退出的空循环把 frames/elapsed 放大）。
       独占重跑同一套 **42/0 全绿**（SW 500 / GL 663 / VK 1035）。教训：
       性能类门禁同时只跑一个实例。
  ⑨ **RelWithDebInfo 取证构建 + 线路径快路径（2026-11-23 收尾4）**：
     · 新增构建档 `--rwdi`（`gpu_build_targets.bat`）：`out/build/
       x64-RelWithDebInfo` → `bin-relwithdebinfo/`（`/O2 /Ob1 /Zi`）。
       **Release 无符号无法采样归因**，性能问题要用这个档；实测整帧
       ~990 FPS（1.01ms，Release 1129/0.885ms，Debug 385~450）。
       bat 现在三类档（Debug/Release/RWDI）在缓存缺失时会自动
       configure（Ninja 取自 `%VSINSTALLDIR%Common7\IDE\...`）。
     · **.bat 必须 CRLF**（2026-11-23 实锅）：同样是这个 bat，**LF 行尾
       下 cmd 会把某些行切错**（报 `'ild' 不是内部或外部命令` /
       `'_targets.bat' 不是...`——token 在 "bu|ild" 处被切开），
       CRLF 恢复即绿。`.gitattributes` 已加 `*.bat text eol=crlf` /
       `*.cmd text eol=crlf`，防止提交-检出后又变回 LF。
     · **RelWithDebInfo 采样出的线路径快路径**（Release 也受益）：
       ① `painterMapPoint` 增**仿射无透视快路径**（m13=m23=0, m33=1）：
          省分母除法与 fabs 检查；线端点每帧数千次映射（采样 2.7%）。
          纯对角（m12=m21=0）再省两项乘法。像素逐位不变。
       ② 整数入口 `painterRaster_drawLine`：恒等/整数平移时**直接落
          设备坐标**，跳过 2×mapPoint + 4×painterRound（floorf+钳位）。
       ③ `painterApplyOpacity`：opacity≥1 直返原色（字节档恒 255 时
          下游本就直接返回）；`painterRaster_drawLineDevice`：单位缩放
          矩阵跳过 `painterPenWidthScale` 的两次 sqrtf（返回值恒 1）。
       实测 RelWithDebInfo 906→990 FPS、Release 1115→1129（±噪声内），
       Debug 385~405（门禁口径）。parity 仍 82px（与 GL 同值）。

- **C 端已废弃（2026-11-23 用户口径「C 端已经被放弃，不要管 C 端的」）**：
  C 服务端（`server/`、`bin/HsfServer.exe`）与 C 客户端（`client/`）**不再维护、不纳入
  验收与对齐对象**，新功能/改名/UI 调整一律只做 Python 三端（server_py/client_py/webui_py）。
  推论：`server_py/tests/run_all.py` 里依赖 C sim 的「双端协议面对照」等用例的红
  **属于 C 端不维护**（本机 C sim root 登录 WinError 10053 也是既有环境故障，不必再修）；
  `common/XhsfLinkProto.h` 只当历史契约参考，不要求与 Python 端逐值同步到新特性；
  看板/验收报告里涉及 C 的项统一标为「C 端已废弃，不在范围」。
- **三端全量工作流复核出的 8 项修复（2026-11-24）**：
  ① **网页首页浊度数字随窗宽回落**：36px 在 900 档三列卡里两列需求 335.3px >
  卡内容 311.5px（几何门禁 347>336）⇒ `window.innerWidth < 1200 ? 32 : 36`。
  ② **`select.input { max-width:100%; min-width:0 }`**：网络页长适配器名（本机
  576px）会把 `.form-row` 顶出卡宽（576>567）。
  ③ **三端 E2E 列序断言改为派生**：C10 与客户端 `proto.HISTORY_COLUMNS` 逐值比、
  D1b 用 `len(page.COLUMNS)`，不再写死 19/16（历史列 22/19 后会再也不会假红）。
  ④ **压差列数值优先统一**：网页 `export.py` 去掉 `press_note or 数值`（说明仍用于
  异常行判定）；桌面离线历史 `_press_text` 也改数值优先（读不到数值才退回说明）——
  导出/离线行与屏幕同口径。
  ⑤ **网页温度通道 CH0**：`parseInt(x) || 1` 会把合法 CH0 吞成 CH1（回显错通道 +
  应用时把服务端 temp_channel 改回 1）⇒ 新增 `channelOr()`，只把 NaN 当缺省。
  ⑥ **示例配置阀位序**：`hsf_server_py.example.json` 的 openDi/closeDi 是旧口径
  （奇数开）⇒ 改成 `2i / 2i+1`（与代码缺省、现场 cfg、模拟器一致）。
  ⑦ **按钮组状态可读**：4 列点阵格宽 ~65~75px，26px 的「● 正常」被截成「正…」/「…」
  ⇒ `.dot-grid.cols-4 .dot-state { font-size:20px }`。
  ⑧ **窄屏折线列下拉**：`▾` 原来拼在按钮文本里、被省略号一起截掉 ⇒ 文本/箭头拆
  `.mcc-text`/`.mcc-caret` 两个 span（CSS 箭头不缩），≤520 档「折线列」整行占位；
  数据存储行「容量上限/写满后/保留分段」各包 `.cfg-pair`，换行不再把标签与控件拆散。
  回归锁：`webui_check` 新增 8 条静态门禁（CH0/配对/箭头/max-width/浊度回落/导出数值），
  多尺寸门禁 9 档 **2130/0**、webui_check **474/0**、三端 E2E **311/0**。
- **HSF 2026-11-23 四项（历史压力列 / 所有模式运行时长 / 阀门改名 / 搅拌泵卡对调）**：
  ① **历史新增「压力1/MPa」「压力2/MPa」**（用户口径「压力1和压力2也要进历史数据被采集」）：
     线上列**只能末尾追加**（`HISTORY_COLUMNS` 20→22：`press1_mpa`,`press2_mpa`；C/旧客户端按下标读）；
     显示列插在「压差(MPa)」之后（17→19），其后 电箱温度/湿度/DI/DO 下标整体 +2 ⇒
     C 端 `kHistoryHeaders`/权重/`auto_min_texts`、网页 `COLUMNS/COL_WEIGHTS/COL_MIN_TEXTS/
     fallback cols`、导出表头与 `history_row_texts` 全部同步（漏一处就是"表头与格子错位"）。
     取值按**换算表名字**（`AnalogConfig.scales` 的 name=="压力1"/"压力2"，通道号远端可配）
     ∧ `analog.valid[ch]` 才算，否则 NULL（"--"，不是 0）；**不进热身门**（选配传感器）。
     老库必须 `ALTER TABLE` 补两列（PRAGMA 探测 + 幂等）。回归：`tests/test_history_pressure_cols.py`
     （新，注册进 run_all）+ `test_store_page` + 桌面 §history + webui_check 19 列表头断言。
  ② **运行时长对所有模式都显示**（用户「原先所有模式都有运行时长显示」）：
     `AutoController.stage_timing` 新口径——组合循环**有界阶段**维持「阶段已用/共」；
     **单模式**与**循环无界阶段**报「本次运行已用」（单模式 = `now-_run_start_ms`；
     循环无界 = `now-_cycle_stage_at`，换相重置，避免同一行在有界/无界间跳数字），total=0；
     `_run_start_ms==0` 不编造 ⇒ -1。客户端已有「elapsed>=0 && total<=0 ⇒ 已用 X」的渲染分支。
     **连带坑（实锅）**：网页/桌面自动卡原先拿「`autoStageElapsedMs>=0` ⇒ 正在跑组合循环」
     当判据 ⇒ 单模式一报时长就被误标成「自动循环（产水模式）」且下拉被抢到"自动（流量循环）"。
     ⇒ **循环判据只认服务端根模式 `autoMode=="auto"`**（`cycleStage`/`runSegmentKey` 两处已改；
     webui_check 加静态门禁；桌面回归 D13o 锁）。凡"新口径让某个字段在所有分支都为真"时，
     必须全树扫**以该字段推断状态**的旧逻辑。
  ③ **阀门改名（逐字）**：`产水阀(1) 反洗进水阀(2) 进水阀(3) 排空阀(4) 反洗出水阀(5) 顺洗出水阀(6)`
     （组名/页面名仍「电动球阀/电动球阀组」）。单一事实源 = `config.VALVE_DEFAULT_NAMES`；
     同名名单还在 `valves.py` 兜底、`proto.AUTO_DEVICES`（id 不变）、桌面 `HomePage.VALVE_NAMES`、
     网页 `model.VALVE_FALLBACK_NAMES` + `page00/page07` 兜底、`page03` 测量样本。**仓内 cfg 已改**；
     **板端 `cfg/hsf_server_py.json` 是显式值优先 ⇒ 部署时要一起改名**（否则板上仍显示旧名）。
     网页 3 列点阵名字变长会省略号截断 ⇒ `.dot-grid.cols-3 .dot-name` 允许折两行（静态配置，不违反
     「内容可变但几何不可变」）。
  ④ **主界面搅拌泵卡：转速升为主数值（40px）与输出频率（26px）位置互调**（用户「频率字大，但一般
     关心的是转速」），两端同款；泵详情悬浮卡里的「设定频率」不受影响。
  ⑤ **验证脚本编码**：`verify/webui/webui_check.py` 与 `webui_xlsx_check.py` 也加了
     stdout/stderr → UTF-8 的重配置（cp936 下打「⇒」直接 UnicodeEncodeError ⇒ 门禁中途被打断）。
     `webui_xlsx_check` 的 C 动态库路径把"看起来像数字"写成数值格（`42.00`→`42`）属既有口径 ⇒
     首行比对改按**数值相等**容差（值错仍红）。
  ⑥ 本轮回归：服务端 `run_all` 41/42（唯一红「双端协议面对照」死在 **C sim root 登录 10053**，
     与压力列无关，属既有环境红）；桌面全量 2010 PASS / 1 FAIL（唯红 `A6s 窄窗无横滚`，
     content 720 > 626，基线 A/B 同样红）；网页 `webui_check --server-port 6200` **457~458/0**（逐次条件项计数差 1，均 0 失败）。
  ⑦ **压力及压差卡（2026-11-23 用户四连改版，最终版）**：卡名 =「压力及压差」；
     **布局第四版 = 三行同构**（用户「压差 数值 单位写在一行，另外的两个压力一样的布局
     分别两行，总共三行」）——每行 = 标签 + 读数 + 单位同行（桌面 `widgets.InlineStat`；
     网页 `.press-rows` 两列 grid + `.press-row{display:contents}`，标签列 `max-content`
     对齐读数）。**说明行** = 卡底共享一行（前缀「压差：/压力1：/压力2：」、多条「；」
     连、超长省略+tooltip）；**无文案时收起**（桌面 `setVisible(False)+fixedHeight(0)`；
     网页 `.press-note-shared:empty{display:none}`）——用户「下面是不是空了整整一行」。
     桌面回归 E6a/E6e/E6g + F1a、网页静态门禁（三行 grid/contents/收起/标题）已锁。
  ⑧ **主界面看板：按钮组 ↔ 电动球阀默认换位 + 长按拖拽自助排序 + 本机持久化**
     （2026-11-23 用户口径「按钮与电动球阀的卡片交换位置，支持下主界面长按可以拖拽卡片
     进行自己排序，并本地持久化记录位置」）：
     · 9 张卡有**稳定 id**（`HomePage.BOARD_CARD_IDS` = turb/valve/dp/vfd/fm/btn/th/
       health/alarm，顺序=默认排布）；桌面存 INI `Home/CardOrder`，网页存 localStorage
       `hsf.home.cardOrder`（键值不是 9 个 id 的合法排列 ⇒ **回默认**，fail-closed，
       否则重排会"少一张卡"且零报错）。
     · 拖拽：桌面 `hsfsrv_client/card_dnd.py` 的 `CardDragController`（长按 600ms、
       位移 >10px 算滚动、拖拽中吃掉 move/release 防滚页+防误点、ghost 快照 + 虚线高亮，
       松手 = 两卡**交换**）；网页 `page00_home.js` 同款（550ms/10px，pointer events，
       拖拽期间 touchmove preventDefault）。
     · **性能/稳定性铁坑**：桌面控制器**不能**把过滤器装到 `QApplication` 上——
       QApplication 析构期遍历/摘除过滤器 ⇒ `settingsmerge` 子进程 exit=0xC0000005
       （3/3 复现）。改为装在**页面自己拥有的控件**（每张卡 + 全部子控件 + 滚动视口），
       控件随页销毁、Qt 各自安全摘除 ⇒ 3/3 干净。
  ⑨ **看板卡：同行等高拉伸 + 数值自动缩放填白 + 读数三件套同步放大**
     （2026-11-23 用户「同一行的卡片向下拉伸到一样高」+「文字自动缩放下减少空白」+
     「压差/压力1/压力2 这几个字也同步大点」+「单位也同步放大」+「浊度仪数字大点」）：
     · 卡片随整排**等高拉伸**（不再 AlignTop/`align-items:start`）。
     · 每卡记**基准字号**（桌面 `_board_base_sizes`；网页 `data-basePx`），resize/重排后
       一次：短卡按余高放大数值，上限 **1.3×**；放大后若 `sizeHint`/内容超过卡片高度
       **整卡回退**（宁可留白，绝不顶大行高 ⇒ 不会触发布局自激）。
     · 网页量余高**不能用 `scrollHeight`**（flex 列容器内容不足时它恒等于 clientHeight
       ⇒ 永远没余量）；要按最后一个可见子控件的 `getBoundingClientRect().bottom` 量。
     · **字号关系**：压力卡标签/单位 = 数值 × 0.72（桌面 `InlineStat._apply_caption_size`
       随 `_value_size` 联动；网页 `.press-label/.press-unit` 用
       `calc(var(--font-caption) * 1.35)`，且 `fitBoardText` 把 `.press-label/.press-unit`
       与数值乘同一 factor）；标签列宽按**自动缩放上限**量（`PRESS_VALUE_SIZE*1.3*0.72`），
       否则放大后被裁字。浊度仪数字基准 32→**36**（40 在两列并排的窄卡里会顶边裁字）；
       **CSS 同选择器后写的规则会覆盖先写的**（`.press-label` 本轮实锅：先写 1.35×、
       后面旧规则又把 font-size 写回 var(--font-caption) ⇒ 必须合并成一条/写在最后）。
     · 自动缩放加**宽度守卫**（桌面 `sizeHint().width() > card.width()` 不放/回退；
       网页 `scrollWidth > clientWidth` 同理），两列并排的大数字不会被裁。
     · 桌面回归 G10（≤1.3× 且不撑高）、F2（基准 32、显示 ≤42）、网页静态门禁已锁。
  ⑩ **负结果：1px 轴对齐线改走 fillRect（2026-11-23，已回退）**：
     动机：1px 轴对齐线的像素域恰是半开矩形 `[a0,a1+1) × [c0,c0+1)`
     （width=1 时 half=0、SquareCap 不扩展）⇒ 与 fillRect 逐位一致，想让
     它搭默认开启的实心合批（`XGPU_VK_FILL_BATCH`）。实测评测：
     · **多矩形区域裁剪下像素不等价**：parity `clip-region-quads` 73px
       差异（fillRect 的区域路径与 `painterGpuDrawSolidQuadRegion` 的逐
       区域 scissor 不等价）⇒ 需加 `m_clipRegion.count <= 1` 守卫才回到
       82px；
     · **加了守卫仍然更慢**：RelWithDebInfo 990→**900** FPS、Release
       1170→**1100**——fillRect 的 pattern/区域/透明度/裁剪分支 + 批冲
       销，比逐线 direct quad+draw 更贵（同工作量下批收益为负）。
     ⇒ 已全部回退；**轴对齐线保持 direct quad 路径**。这条与上游
     `XGPU_VK_AXIS_QUAD_BATCH`/`SOLID_QUAD_BATCH` 缺省关的 AMD 结论一致
     （Intel 上本负载同样无收益）。**别再试"线转 fillRect/大合批"**。


- **实心 quad 合批缺省翻正 + 绘制侧热点三修 + 模块级采样归因（2026-11-23 收尾5）**：
  ① **缺省翻正（+13~17%，同二进制 A/B）**：`xvkl_solid_quad_batch_enabled()`
     由「缺省关」改「缺省开」，`XGPU_VK_SOLID_QUAD_BATCH=0` 显式回退
     （对齐 `XGPU_FASTPATH_EXT` 的「缺省开、显式 0 关」口径）。测量
     （RelWithDebInfo、1000x700 图表页整帧、`--benchmark 5 --benchmark-full
     --page 4 --tab 20`，同二进制交替跑）：
     · 改前：缺省（=关）1027/1049/1054 → 开 1218/1226/1222（中位 **+17.2%**）；
     · 改后：缺省（=开）1281/1302/1310/1319/1322（中位 1310）
       vs `XGPU_VK_SOLID_QUAD_BATCH=0` 1131/1156/1159/1160/1164（中位 1159）
       ⇒ **+13.0%**；
     · **区域上传零拷贝修复后复测**（内容真的画上去了，绝对值整体下移）：
       缺省 1230/1258/1245（中位 1245）vs 关 1058/1060/1084（中位 1060）
       ⇒ **+17.5%**。结论不变：实心合批是结构性收益，与画布修复无关。
     **旧「无收益」为何错**：那次是 Debug 口径且发生在「追加必须在入口的
     先冲实心批之前」顺序缺陷修复前；采样实证合批把 `igvk64.dll` 从
     129ms/s 压到 45ms/s（每 quad 一次 bind+draw 的驱动成本）。图表页
     实心 quad ~1500~1800/帧，属结构性大头。
     **axis 合批仍缺省关**：`XGPU_VK_AXIS_QUAD_BATCH=1` 单独 1033~1038、
     与实心合批同开 1211~1231（=仅实心开），无额外收益。
  ② **具名色查找改二分**（`XStyleSheetStyle.c xsss_lookupNamedColor`）：
     149 项表**已按字典序排序且无重复**（首次调用校验一次：不满足即回退
     线性并保持正确性），线性扫 ×`XStrcmp` → 二分 ≈8 次比较。采样：
     ucrtbase 7.5%→4.1%（该函数经 strcmp 占 138/5968 样本）。
  ③ **painterRound 去 CRT**：热路径（线段端点/文本定位/图像矩形）原先
     每次 `isfinite + floorf`；现有限值且 |v|<2e9 走 `(int)(v+0.5f)` +
     负小数自减一次（float→int→float 精确），NaN/Inf/越界仍走原 CRT 分支
     （逐位语义不变）。
  ④ **变换单次扫描 + 透传、同步闸前置（中性，保留）**：
     `painterMatrixAxisShift` 一次扫描替代 `IsIdentity() || IsTranslation()`
     两遍（7 个调用点同改）；`painterRaster_drawLine` 把已算出的有效变换
     透传给 `painterRaster_drawLineDevice`（省第二次 19 字段缓存比较）；
     `xgpu_sync_readback_if_requested` 把静态开关判定前置。**实测帧率
     变化 <1%（噪声内）**——成本本已很小；保留原因是纯减法且不改变语义，
     不当作收益项。
  ⑤ **模块级采样归因（临时工具，已移除）**：demo 里临时挂 psapi 模块表 +
     1ms SuspendThread 采样，并扫描 [rsp..48 字] 找 exe 返回地址做 caller
     归因。**教训**：① 叶采样百分比会随总 CPU 下降而「搬家」，必须与
     绝对值（ms/s）一起看（win32u 36→87ms/s 那轮是合批让帧变快后
     每帧 USER32 成本占比升高的错觉/抖动）；② 栈扫 caller 会命中**陈旧
     栈字**（XRect_normalized 声称调 ucrtbase 就是假象），只采信「同时
     在叶热点里出现」的条目。
  ⑥ **最终画像（RelWithDebInfo，含采样开销 1230 FPS）**：exe 70.3% /
     ntdll 13.5%（堆分配散布：XRegion_deinit、rect_from_edges、
     xmemory_system_free/realloc、label_drawTextContent、样式串解析、
     VXFont_copy）/ win32u 4.9%（消息泵 + postPaintEvent）/ igvk64 4.6%
     （原 13.2%）/ ucrtbase 4.1% / VCRUNTIME 1.9% / vulkan-1 0.4%。
     exe 剩余热点均 ≤5.6%：painterRaster_drawLineDevice 5.6%、
     xvkl_solid_quad_batch_append 4.5%、painterGpuApplyStateClip 3.8%、
     painterEffectiveTransform 3.7%、painterRound 2.7%、region 边运算
     ~3%、样式引擎 ~3%、字形缓存 ~2%、字体拷贝 ~2%。
     **结论**：VK 侧已无单笔 >2% 墙钟的确定性优化项；再往下是结构性
     重构（逐控件样式缓存/字体共享/文本布局去分配），风险高收益低。
  ⑦ **门禁（各自独占跑，最终口径）**：Debug `p1` **44/0**（VK 469.1 /
     SW 196.5 =2.39×，parity 82px、静态层 0 硬差、CPU 光栅 ab=0/route=0
     全 17 页）；Release `p1 --release` **41/0**（VK 1247.6 / SW 539.2
     =2.31×）。计数以报告文件 `^- (✅|❌)` 唯一条目为准（stdout 与报告各
     打一遍，别拿 grep 总行数当断言数）。Release VK 由 1115~1170 升至
     ~1250（修复区域上传后内容真的画上去了，比"零拷贝"时期略低属正常）；
     Debug 由 385~452 升至 469。RWDI 同负载 ~1240（合批关 ~1160）。

  ⑧ **GPU 窗口快照的 acquire 永久阻塞（实锅，已修缺省）**：`XGuiRegression_Test`
     的 XHOV 段（acquire→beginFrame→readback→endWindowFrame，连续三次、不绘制）
     在 VK 下**冻死**：日志停在 `snap: acquire` 后一行、进程 Responding=False。
     根因 = `vkAcquireNextImageKHR` 缺省 `UINT64_MAX` 永久等待；两三张交换链图
     都被呈现占用后第三次 acquire 永远等不到可用图（GL/软件同场景不阻塞）。
     ⇒ `XGPU_VK_ACQUIRE_TIMEOUT_MS` **缺省由 0（永久）改 1000ms**：超时按
     beginFrame 失败走降级帧路径，UI 线程不再被呈现节律/合成器拖死；env 显式
     "0" 仍回旧行为（仅诊断）。验证：改前 harness 3 分钟无进展；改后同命令
     **跑完**（`XGuiRegression_Test` 292 行、exit=1，即下述 4 条红）。
  ⑨ **XGuiRegression_Test 三后端现状（2026-11-23 实测 + 已修）**：
     · 软件 `XGUI_RENDER_BACKEND=software`：**0 失败**（"tests passed"）。
     · GL `=gpu`：3 失败，全是 `xrot` 旋转样本（`蓝件样本(100,100)` 等，
       既有 GL 基线，与本轮无关）。
     · VK（缺省）：**修复后 0 失败**；修复前 4 失败 —— `theme engine
       high-DPI paint scales target origin/extent` ×2 + G3 `border-image`
       九宫/边条 ×2（另一次偶发 `t218c mismatch=591 px`）。
     **根因（已根修，勿再当"VK 离屏既有缺口"）**：HEAD `4a12f049` 给
     `xvkl_copy_rows_for_upload` 加了防御守卫（`dstStride/srcStride <
     width*4` ⇒ 直接 return），而 `xvkl_draw_image_region` 的单行拷贝
     仍按旧约定传 `dstStride=0, srcStride=0` ⇒ **整段零字节拷贝** ⇒
     区域上传上去的是空画布。现场证据：painter 侧画布 nz=1728（图标确实
     画进画布），而 region 暂存 nzA=0；`XGPU_VK_REGION_DIRECT=0`（退回
     整幅 drawImage）后全套件 0 红。⇒ 修复 = 该调用传真实行距
     （`dstPitch`/`bpl`；rows=1 时步进不参与寻址，但必须过守卫）。
     **教训**：**改公共 helper 的契约（这里"stride 不足就拒绝"）必须全树
     扫调用点**——守卫与旧调用点约定不一致时不会报错，只会**静默零拷贝**；
     与「改公共头/结构体要全量重编」「改接线口径要全树扫消费方」同族。
     **回归锁**：parity 新增场景 `draw-image-rect-scaled`（`XPainter_scale
     (2,2)` 后 drawImageRect ⇒ GPU 侧必走软件暂存画布 + 区域上传腿；
     SW vs VK **0 硬差**，全库场景数 14→15）；`XGUI_VK_REGION_TRACE=1`
     保留为区域上传诊断（源矩形/内联腿/暂存几何 + 暂存非透明像素计数）。
     **另修 acquire 冻死**（同文件见 ⑧）：无有限超时前该套件在 XHOV 段
     永久挂起，后面的 4 条红根本没机会报出来——**"测试挂住"与"测试红"
     是两件事，挂住会掩盖红**。
     该套件此前只在软件后端跑过 ⇒ **默认 VK 之后必须纳入三后端门禁**。

  ⑩ **负结果：合批改「4 顶点 + 共享索引表」（2026-11-23，已回退）**：
     动机：实心/纹理合批每个 quad 写 6 个顶点（TL,TR,BL / BL,TR,BR），
     其中 2 个是重复顶点；改成 4 顶点 + 共享索引表 `[0,1,2,2,1,3]`
     （`vkCmdDrawIndexed` + baseVertex=本批首顶点，三角形与 6 顶点列表
     共用同一 1-2 对角线 ⇒ 逐位一致），可把顶点写入/顶点着色砍 1/3
     （索引表 65536 quad 上限 = 1.5MB/会话，批次到上限即冲批）。
     **实测：帧率无变化**（RelWithDebInfo 缺省 4 连测 1238/1249/1241/1253
     中位 1245，与改前 1245 完全相同；同一二进制 `XGPU_VK_SOLID_QUAD_BATCH=0`
     对照 1038~1071 —— 合批本身的 +17% 仍在，只是"少写 2 个顶点"在墙钟
     上不可见）。正确性已验证（parity 82px 与改前同、`XGuiRegression_Test`
     VK 0 红）。⇒ 按本仓纪律**不保留**（纯增量复杂度 + 1.5MB/会话，
     零可测收益）；顶点写入量不是当前瓶颈（每 quad 的固定调用/状态
     开销才是）。**别再试这条**。

  ⑪ **负结果：painterRound 快路强制内联（2026-11-23，已回退）**：
     采样里 `painterRound` 仍占 ~2.7% exe CPU，试了「快路 `static __inline`
     + 慢路拆 out-of-line」想省调用开销。**实测无变化**（新低噪口径
     `XGUI_BENCH_CPU=1` 的主线程 CPU：改前中位 0.780ms/帧、改后 0.789ms/帧，
     在 ±3% 噪声内）——/O2 早已把热调用点内联，符号归因来自冷路径实例。
     ⇒ 已回退。**教训：采样归因的"函数占比"不等于"函数调用开销可优化"，
     先确认它没被内联再动手**。
  ⑫ **低噪性能口径（工具，保留）**：`XGUI_BENCH_CPU=1` 让 demo 的
     `--benchmark` 额外打印 **主线程 CPU ms/帧**（`GetThreadTimes`；
     非 Windows 返回 0）。本机墙钟 fps 受其它进程/调度影响有 ±10% 波动，
     1%~2% 级 A/B 会被淹没；CPU 口径只计帧循环线程，波动约 ±3%，配合
     多次取中位可判 1% 级改动。**注意**：进程级 `GetProcessTimes` 会把
     驱动内部线程算进来（实测 0.87 vs 主线程 0.78ms/帧），别混用。

  ⑬ **热路径环境闸合并（2026-11-23，**正收益**，已保留）**：
     病灶：诊断/契约开关（`XGUI_GPU_SYNC` / `XGPU_PROF` /
     `XGPU_PAINTER_PROF` / `XGPU_QUAD_PROF` / 各合批开关）在每个绘制
     原语/每个 quad 的路径上被查询，原实现都是「函数调用 + 函数内静态
     判定」。本帧 ~1800 quad × 每个原语 3~6 次闸查询 ≈ 9M 次/秒。
     修法：全部改成**文件级 int + `static __inline` 快路（一条 load+test）
     + out-of-line 慢路（首读/副作用/诊断 live 模式）**：
       · XPainter.c：`xpainter_prof_requested` / `painterGpuSyncRequested` /
         `xgpu_sync_requested` / `xgpu_env_cache_enabled` /
         `xgpu_sync_readback_if_requested`（后者整函数内联）；
       · XGpuRenderBackend.c：`xgpu_prof_requested` / `xgpu_sync_requested` /
         `xgpu_sync_upload_if_requested` / `xgpu_sync_readback_if_requested`；
       · 驱动 `XGpuRenderDriver_vulkan.c`：`xvkl_quad_prof_on` /
         `xvkl_solid_quad_batch_enabled` / `xvkl_axis_quad_batch_enabled` /
         `xvkl_fill_batch_enabled` / `xvkl_upload_batch`。
     **语义保持**：各 env 解析式逐字符未动；`XGPU_ENV_CACHE=0` 的逐次
     实时读（painter SYNC 闸）保留；`XGPU_QUAD_PROF`/`XGPU_VK_STAGE_PROF`
     的 atexit 注册移到慢路首读。
     **实测（`XGUI_BENCH_CPU=1` 主线程 CPU ms/帧，RWDI，10 次中位/min）**：
     改前 0.7845 / 0.764 → 仅 painter+后端层 0.7735 / 0.764（−1.4%）
     → 加驱动每-quad 层 0.744 / 0.711（−5.2% / −6.9%）
     → **再补第二批同族闸**（`xpainter_quad_prof_on`（逐线段/逐 quad 查
     1~3 次）、`painterGpuFastPathExtRequested`（逐斜线）、
     `painterGpuBatchFlush` 的无待定批快路内联化）**0.726 / 0.706
     ⇒ 累计 −7.5% / −7.6%**。门禁：Debug `p1` **44/0**（VK 474.7 /
     SW 198.9 =2.39×）、Release `p1` **41/0**（VK 1275.8 / SW 537.2
     =2.38×）；parity 82px、`XGuiRegression_Test` VK 0 红。
     **判据**：parity 82px 不变、`XGuiRegression_Test` VK 0 红、
     Debug `p1` 44/0（VK 446.1/SW 187.8）、Release `p1` 41/0。
     **教训/口径**：**"开关默认关"不等于免费**——闸的**调用开销本身**
     在每原语路径上会占几个百分点；新增诊断开关时应按本模式写
     （文件级 int + inline 快路），不要用「函数内 static」。

  ⑭ **表面裁剪包含快路（2026-11-23，**正收益 −3.8%**）+ 交替 A/B 纪律**：
     `painterGpuApplyStateClip` 在窗口帧里每原语一次 `XRect_intersected`
     （`rect_edges`×2 + `rect_from_edges` 的 64 位边规范化，采样合计
     ~2.8% 总 CPU）。窗口帧 `begin_image` 把**表面裁剪继承成状态裁剪**
     （同一矩形）⇒ 交集即自身：命中包含判定（4 个 int64 比较）时跳过
     通用求交；不命中（或任一矩形宽高为负）仍走 `XRect_intersected`
     （语义逐值不变）。
     **实测**：交替 A/B（同一时段、两个二进制逐轮交替，各 6 轮）：
     0.747 → **0.7185ms/帧（−3.8%，clip 版 6/6 全胜）**。门禁：Debug
     `p1` 44/0（VK 487.9 / SW 195.3 = **2.50×**）、Release `p1` 41/0
     （VK 1261.8 / SW 530.9 = 2.38×）；parity 82px、harness VK 0 红。
     **方法学（实锅）**：本机测速在**不同时间段**会漂 3~5%（同代码
     0.704~0.782 都出现过）。第一轮"改前/改后顺序测"把 −3.8% 的收益
     显示成 +4.8%（漂移叠加）；改成**两二进制逐轮交替 A/B** 才看见真值。
     ⇒ **1%~5% 级性能结论必须交替 A/B**（每轮各跑一次，取中位/胜率），
     单序列顺序测只用于 10% 级以上的粗判。
     **另一个坑**：`XGuiWindowDemo_Test.exe` 复制到 `/tmp` 再跑会**丢字库**
     （资源按 exe 所在目录解析）⇒ 回退点阵字形，CPU 5.7ms/帧（7 倍慢）。
     A/B 的两个二进制必须都放在 `bin-relwithdebinfo/` 里（同目录不同名）。

  ⑮ **GL 后端提速（2026-11-23，**正收益 −5.0% CPU / +5.4% fps**）**：
     GL 客户端 CPU 原本是 VK 的 ~2 倍（1.251 vs 0.634ms/帧，Release 同页）。
     两处改动（均经两二进制**逐轮交替 A/B**）：
     ① **热路径开关文件级缓存**（同 VK/painter 口径）：GL 每个 solid quad
        连环查询 `xgld_quad_batch_enabled` / `full_batch` / `quad_degen_legacy`
        （在 append 里**每 quad** 一次）/ `batch_prof`（每命令宏+每 quad）/
        `premul_repair`；另有 `state_cache` / `flush_lean` 每冲批、`ensure_current`
        每原语。全部改「文件级 int + `static __inline` 快路 + out-of-line 首读」，
        env 解析式/缺省值逐项不变。实测 1.447→1.3995ms/帧（−3.3%，4/5 胜）。
     ② **顶点缓冲帧内子分配**：旧实现**每次冲批一次 `glBufferData` 重分配**
        （实测 101 次/帧 × 2.4µs ≈ 0.24ms/帧，占 GL 帧 CPU ~17%）。改为
        帧首 `glBufferData(cap,NULL)` 孤儿化一次 4MB store，冲批走
        `glBufferSubData` 追加互不重叠区间 + `glDrawArrays(firstVertex)`。
        实测再 −3.1%（1.3995→1.3555，4/4 胜）；合计 **−5.0% CPU / fps
        786→830（+5.4%）**。回退：`XGPU_GL_VBO_SUBDATA=0`。
        **必须与旧负结果区分**：2026-09-24「勿改 glBufferSubData 原位更新
        （216→106 FPS）」说的是**同区间**原位写触发驱动隐式同步；本方案是
        「帧首孤儿化 + 帧内互不重叠区间」——无写后读冲突，旧 store 由 GL
        保留给在途 draw。**别再拿那条负结果否掉这条**。
        即时路径（`xgpu_vertex_data` 64B glBufferData）会把 store 改小 ⇒
        置 `m_vboFrameCap=0`，下个冲批重新孤儿化（否则 subdata 越界）。
     当前三后端（Release 全帧 1000x700 图表页，3 次中位）：**SW 542 /
     GL 830 / VK 1279 FPS**（CPU/帧 1.816 / 1.190 / 0.640）。门禁 Debug
     `p1` 44/0（GL 354.4；Debug 是 /Od，GL 驱动代码在静态库里所以提升
     主要在 Release）、Release `p1` 41/0（GL 776.2/此刻 830）；parity
     SW↔GL 82px、静态层三后端 0 硬差。
     **剩余 GL 大头（未做）**：① 101 次冲批/帧 × ~2.7µs ≈ 0.23ms（texture
     翻转 ~60 次/帧 + scissor 变更 ~63 次/帧）；若要再提 10%+，可把
     **白纹理并入字形图集**（solid quad 改采样图集白纹素）以消掉
     white↔atlas 翻转 —— 但要动共享图集打包（VK 同用），风险高；
     ② present 0.18ms/帧（SwapBuffers+DWM，平台侧，无解）。

  ⑯ **GPU AA 线段缺省翻正（2026-11-24，**真实页面 −99% 单页重绘**）+ 真实页面/长稳压测**：
     ① **症状**：真实 HSF 客户端页面每帧有数百~数千条 AA 线段（主界面/数字量页
     `xpainter-prof` 的 `ab` 达 4k~12k/5s，`abLine` 占绝大多数）。缺省走
     CPU 覆盖率 + `drawAlphaBitmap` 上传：GL 每线 ~0.037ms 尚可，**VK 每线
     ~0.4ms**（窗口内联上传要在每线结束/重开渲染通道）⇒ 数字量页整页
     重绘 **VK 135ms**、GL 12.9ms、软件 0.9ms（真实页面上 VK 比软件还慢，
     “全 GPU”并不成立；p1 门禁的 demo 页没有 AA 线，所以从未暴露）。
     ② **修法**：`XGPU_AA_LINE` 缺省由 0 翻正为 **1**（GPU 解析 AA，片元
     fwidth 羽化；`=0` 回退 CPU 逐位路径）。门禁 2b 段本来就有 GL/VK
     两条感知校验（平均墨量差 ≤1%、over8 ≤20%）与 **AA 关闭基线必须
     逐位一致**；缺省翻正后两项仍 PASS。实测真实页面（`XHSF_REPAINT_MS=250`
     强制整页重绘中位）：GL p0 7.7→**4.9ms**、p3 12.9→**2.1ms**；
     VK p0 34.2→**6.3ms**、p3 135.4→**2.0ms**；`ab` 降到 ~400~530
     （只剩路径裁剪等回退形态）。**demo 整帧**：Debug GL 354→420、
     Release GL 776→**1058.7 fps**（+36%）、VK 1284、SW 558；p1 门禁
     Debug 44/0、Release 41/0，parity 仍 82px。
     **门禁配套**：`gpu_stage_verify.sh` 的「AA 关闭基线」两条改为显式
     `XGPU_AA_LINE=0`（缺省已是 1，不显式就测的是 GPU 路径）。
     ③ **真实页面逐页成本测量（新增 app 层钩子，默认关）**：
     `HighSpeedFilter/client/ui/HsfUi.c` 加 `XHSF_REPAINT_MS=<ms>`：周期
     `XWidget_update(&m_base)` 强制整窗重绘，配合既有 `XHSF_PAINT_TRACE`
     （`[paint] page=N rect=... tree=...us`）得到逐页可比的整页成本。
     用法：`XGUI_RENDER_BACKEND=gpu XHSF_REPAINT_MS=250 XHSF_PAINT_TRACE=1
     bin-release/HsfClient.exe 8 --login root:root@127.0.0.1:6100 --page N
     --no-settings`。**两个测量坑**：① 一次整页树的多个脏矩形事件会打
     多行相同 `tree` ⇒ 按「连续相同值折叠」后再取中位；② 页面登录
     会因上一条崩溃残留会话被 DenyNew 拒（三账户轮换 + 重启 sim 服务端）。
     ④ **长稳/泄漏压测（2026-11-24，GL/Release）**：
     · Demo 连续整帧 15 分钟：1,158,832 帧、1287.6 fps、**exit=0**；WS
       142.3~143.7MB、Private 108.9~111.7MB、句柄 441 恒定（30 次采样无增长）。
     · HsfClient 数字量页 10 分钟（`XHSF_REPAINT_MS=250` + `XHSF_HEAP_TRACE`）：
       `[heap] sys` 44.77MB 启动 → 稳态 45.473MB，前/后 12 样本中位差 **+3 字节**；
       WS 192.2→192.8MB 后走平、句柄 439~443 恒定。
     ⑤-2 **专项定位结果（2026-11-24，临时诊断已回退，结论留存）**：
     用「活对象表 + vtable 快照」诊断（`XObject_init` 登记 (ptr, vtable)、
     deinit 摘除；父析构遍历 children 时校验 vtable）实测：崩溃由**同一个
     widget 的 5 个 child 槽**触发——这些指针仍在活表里（**从未 deinit**），
     但对象头里的 vtable 已变成**别的类的 vtable**（5 个各不相同），且
     `XMemory_free`/`XMemory_realloc` 守卫**一次都没命中**（不是经框架
     分配器释放/搬移）。跳过这 5 个槽后 exit=0 ⇒ 就是它们导致二次删除。
     ⇒ 结论：**父 children 表里这 5 个槽指向的内存被「原地改写/复用」**
     （更像是别处的指针写越界或把别的对象 memcpy 进了这些地址），不是
     普通 double-free。**固件/页面级线索**：5 个 child 地址与父在同一 64KB
     堆段、间距近似固定；父是普通 XWidget 派生类。下一步手段：
     `gflags /p /enable HsfClient.exe /full` 在本机 **Permission denied**
     （需提升权限/被策略拦），改用 **ASan 版 Debug 构建**（MSVC
     `/fsanitize=address`）跑同场景可直接给出首次非法写/释放栈。
     ⑤ **已知未修：HsfClient 退出段 segfault（既有缺陷，非本轮引入）**：
     软件/GL/VK 三后端、Release 与 11-21 Debug 构建全部 exit=139；cdb 栈
     `XClass_deinit_base+0x99` 读 `0xfeeefeee` ← `XClass_delete_base` ←
     `VXObject_deinit`（children 循环）——**主窗 teardown 时某 child 已被
     释放仍登记在父 children 里**（`--login-shot` 只建登录窗时 exit=0，
     故与登录窗无关；框架已有「快照式析构 + 重复登记清尾」两道防线，说明
     是别处提前释放了仍挂父的 widget，需专案定位）。运行期数据不受影响，
     但退出码 139 会污染 CI，且崩溃残留 TCP 会话会占住账户。

## HSF 2026-11-24 批次（数值渐变 / 网口设置 / 历史表 / 轮询重启）

### 数值显示渐变（三端同口径）
- 色带：**蓝 #1565C0 → 绿 #2E7D32（50%）→ 橙 #ED6C02（75%）→ 红 #C62828**，
  范围外夹端点，范围非法/缺值保持单色。桌面 `theme.gradient_color`；网页
  `util.js` 的 `HSF.gradientColor`（page00/04/05/06 共用）。
- 范围只影响显示，不是报警阈值：浊度逐台（NTU）、电箱温度（℃）、电机转速
  （rpm）、电机温度（℃）在远端 collect 节各有 pair；压力/压差沿用「模拟量
  通道与量程」的 low/high，不重复配。cfg 落盘在 `display` 节（snake_case）。
- **逐项启用开关**（2026-11-24 用户「每一个都可以单独设置」）：
  `turbidity1GradientEnabled / turbidity2GradientEnabled /
  temphumTempGradientEnabled / vfdSpeedGradientEnabled /
  vfdMotorTempGradientEnabled / pressGradientEnabled / press1GradientEnabled /
  press2GradientEnabled`，缺省全开；关=该项单色（浊度退回 ≥20 标红旧口径，
  拖动条回固定蓝）。快照随数据下发 `v{i}_gradient / gr{i} / tempGradient /
  speedGradient / motorTempGradient`；缺键=老服务端按开处理。
- 应用面：首页、水质页浊度、模拟量页工程量、搅拌泵页转速；网页 page00/04/05/06。
- **VFD 两处拼包必须共用 helper**：`encode_vfd`（周期推送）与
  `_h_vfd_snapshot`（0x0102）原来各写一份；只在推送加键 ⇒ 快照漏键。现统一
  走 `_vfd_display_ranges(doc)`。
- 拖动调速条（2026-11-24 用户两次口径）：先做过「轨道全长渐变」，被用户否定
  （「我要的是整体一个颜色改变不是这种」）。**终态=整条轨道+手柄+读数同一个
  当前值色**，按转速在显示范围里的位置取渐变，32 档量化（`_speed_color`），
  避免拖动时逐格 setStyleSheet；关闭转速渐变 ⇒ 固定 `CLR_INFO` 蓝。
- 老服务端兼容：新客户端/网页只有 GET 见过任一显示范围键（桌面
  `_display_range_seen` / 网页 `displayRangeSeen`）才把这些键随 collect 下发；
  旧 Python 服务端把未知键当整包错误，否则连采集开关都存不上。

### 客户端「网络设置」新增有线网口（2026-11-24）
- `client_py/hsfsrv_client/netcfg.py`：Windows 走 PowerShell
  （Get-NetAdapter/Get-NetIPConfiguration/Set-NetIPInterface/New-NetIPAddress），
  板端 Linux 走 `nmcli`（NetworkManager 持久化）。框架 XNetworkWiFi 只管 WiFi，
  有线 DHCP/静态没有框架能力。
- 界面：网口信息表（网口/状态·速率/IPv4/MAC）+ 选中网口详情（获取方式/网卡/
  MAC/IP·掩码/网关/DNS）+ DHCP/静态切换 + IP/掩码/网关/DNS1/DNS2；应用前
  确认（改网口可能瞬断）。查询/应用一律工作线程 + 信号回 UI，不卡界面。
- 权限：Windows 非管理员 / Linux 非 root ⇒ **只看不改**，应用置灰并说明；
  板端客户端以 root 运行可改。
- nmcli 命令必须 `LC_ALL=C/LANG=C`（否则 connected/auto/manual 被本地化，
  解析失败）；`-g` 输出要反转义 `\:`。

### 历史表不再标异常行（2026-11-24 用户「都不标了」）
- 桌面 `HistoryPage._server_row_texts` / `_is_abnormal` 恒 False；网页
  `page09_history.js` 不再挂 `abnormal` class、`rowOf` 恒 false。原来按
  VFD 故障/浊度≥20/压差说明/流量计报警/阀故障整行淡红底的规则全部取消。

### 板端「全设备数据过期」根因与根修（2026-11-24 实锅）
- 现象：远端设置点「应用」后，485/数字量设备全部「数据过期」，直到重启
  服务端才恢复；模拟量/相机/按钮组仍正常（它们走别的采集路径）。
- 根因：`restart_polling` 对已 cancel 的轮询任务逐个 `await`，某任务卡在
  串口/网络 IO 时取消不立刻生效 ⇒ 永远等不到 ⇒ 旧的停了、新的没起。
  触发条件：`_cfg_set_collect_key` 对采集开关**同值写回也返回 True** ⇒ 每次
  「应用」都重启轮询。
- 修法：① 开关 setter 返回 `changed, changed`（同值不重启）；②
  `restart_polling` 用 `asyncio.wait(tasks, timeout=3.0)`，未退出的旧任务
  不再等、先拉起新任务并记日志。回归/现场判据：应用同值采集配置后 30s 内
  health 保持正常（压力1/2 断线是真实硬件）。
- 板端部署纪律：换包**只解 `hsfsrv`/`hsfweb`/`hsfsrv_client` 代码目录**，
  保留 `cfg/`、`hsf_data.db*`、`hsfsrv_client/bin/`（ARM .so 不能被 Windows
  构建覆盖）；先停三单元再备份全包（`backup-py-<ts>.tgz` 含库），解包后
  `python3 -m compileall`（板端 Python 3.9）再启动。

### 板端网口路由（2026-11-24 现场）
- eth0（支持千兆，当前协商 100M，DHCP 10.168.1.101）与 wlan0（192.168.1.249）
  都能出外网；按用户选择配成 **wlan0 主用（metric 100）+ eth0 备用
  （metric 5000）**。eth1（硬件只支持 10/100，192.168.0.10/24）是设备专网，
  到 .11/.13 通、无网关/外网。

### 实时趋势：时间范围下拉 + Y 轴 0 基线 + 图例跟随设备名（2026-11-24）
- 用户口径：**「水质 流量 搅拌泵的图表时间范围太少了添加下拉框可以选择时间范围，
  并且 y 轴间隔太小了，选从 0 到最大值等分」** + **「水质界面的折线图图例名字
  没跟着改」**。三页共用一套趋势控件，桌面 `pages_monitor.TrendChart` /
  `_trend_card`，网页 `static/js/trendchart.js` 的 `TrendChart` / `trendCard`。
- **时间范围**：滚动窗 60 点 → **18000 点**（1h @200ms），缺省窗宽 12s → **1 分钟**，
  档位 `RANGE_CHOICES` = 1/5/10/30/60 分钟（桌面 `WIN_MAX_MS=3600000`，网页同值）。
  选档 = 设窗宽 + **回到跟随**；`follow()`/双击**不再把窗宽打回默认**（否则选完
  1 小时、拖一下再点跟随就悄悄变回 1 分钟，看着像下拉没生效）。
- **Y 轴**：`_visible_scale` / `visibleScale` 从「视口内 min/max + 8% 留白」改为
  **0（或负值下限）→ 最大值**，5 档等分。慢变量（浊度 0.045~0.05）不再显示成
  0.0446/0.0452 一串几乎相同的刻度；多序列仍各自归一化（量纲不同）。
- **图例跟随设备名**：`TrendChart.set_series_name(key, name)`（网页
  `setSeriesName`）改名并同步图例多选下拉（`MultiCheckCombo.set_labels` /
  网页 `multiCheckCombo.setLabels`，保持勾选态/索引）；`_trend_card` 把图例控件
  挂到 `chart._legend_combo`，网页用 `chart.onNamesChanged` 钩子。水质页拿到
  `v{i}_name` 后改名，**图例只留短名「进水/出水」+ NTU 单位**（用户「图例
  没必要写进水浊度仪，直接进水就可以了」+「NTU 单位还是要带上的」；卡片上
  的设备名照旧，`_turb_trend_short_name` 只裁「进水/出水」前缀、不硬截
  其它名字）。
- **不画数据点圆点**（2026-11-24 用户「点不要显示不好看」）：桌面
  `_paint_series` 与网页 draw 的序列点循环删掉，只留折线；悬停单点标记保留。
- **搅拌泵趋势图高度加倍**（同轮用户「搅拌泵的图高度增加一倍」）：桌面
  VfdPage `self._trend.setMinimumHeight(320)`（2× 控件缺省 160）；网页
  `.vfd-page .trend-chart { min-height: 400px }`（2× 缺省 200）。页面有滚动壳，
  放不下是滚动，不压其它块。
- **抽稀**：18000 点按绘图区宽度抽到约 2×宽（`_trend_thin`/`thinPoints`），每桶
  保留首/末/最小/最大 ⇒ 绘制量不随窗长膨胀，尖峰不丢（等距隔点会丢尖峰）。
- X 刻度跨度 ≥10 分钟切 `HH:MM`（秒位恒 00，6 个 HH:MM:SS 在窄窗会互相压住）。
- 本机设置新键 `trendRangeMs`（各趋势页共用一份，缺省 60000）；网页
  `HSF.prefs "trendRangeMs"`。
- 回归锁：桌面 `tests/test_hsf_regression.py` §17（`--only=charts`）——
  A 窗扩容/封顶、H1a 搅拌泵图最小高 320、K1~K11（档位/窗宽/跟随保持/
  下拉落盘/Y 0 基线/全零兜底/抽稀保极值/图例改名/水质页端到端/无数据点圆点）；
  网页 `verify/webui/webui_check.py` STANDALONE 的 Y 轴断言已改 0~max，
  新增「范围下拉」段与搅拌泵图高 ≥400px，页 6 图例期望改为「进水/出水」
  短名（C 栈回退「浊度仪1/2」两种都认）。
- **一个测试坑**：脚本合成趋势点必须**按时间升序** append；先喂一条最新点再补
  历史点会让 `_origin_ms` 停在最新点、窗口钳在 `[origin, origin+窗宽)` ⇒ 历史点
  全在窗外（画出来只有孤零零一个点）。页面真实数据天然有序，这是测试/截图
  脚本才会踩的。
- C 端趋势图未同步（用户口径「C 端不用了」）；本次**只在本机模拟验证**，未部署板端。

#### 趋势图后台采集（2026-11-24 用户「这些折线图在后台也要实时获取数据」「不在这个界面的时候不要暂停数据采集」）
- 问题：切页时订阅收敛成「只本页需要」+ 刷新只刷当前页 ⇒ 离开趋势页后
  曲线停更、切回来从空白重画（看着像"离开就暂停采集"）。
- **桌面**：`MainWindow.TREND_PAGE_IDS=(5,6,18)` 与 `TREND_TOPICS`
  （VFD/浊度/流量计）——`_apply_topics` 把它们**常驻订阅**；
  `_refresh_current` 每拍再调各后台趋势页的 `refresh_trend()`（只喂趋势
  缓冲，不动控件）。VfdPage 把原 `refresh()` 内联的追点抽成
  `_feed_trend(st, fresh)` 与 `refresh_trend()`；WaterPage/FlowPage 的
  `refresh_trend()` 直接复用既有 `_trend_feed()`。页面尚未预热建好时跳过；
  `PREWARM_ORDER` 把 5/6/18 提到最前（原来 18 在末尾 ⇒ 开局二十多秒
  流量曲线是空的）。
- **网页**：新增 `static/js/trendhist.js`（index.html 在 trendchart.js 之后
  加载）——三个趋势主题用 `store.addExtraTopic` 常驻（断线重连由 store
  重发意图），store 每帧按 `updatedMs` 去重后写入 18000 点环形缓冲；
  页面 mount 时 `HSF.trendHistory.seed(chart, group)` 回填（`TrendChart`
  新增 `appendPoints` 批量接口与 `pointCount` 查询）。切页 = 页面卸载也
  不影响采集（缓冲在全局）。
- 回归锁：桌面 §17 新增 H7a/H12b/H16c（`refresh_trend()` 无需 refresh
  也追点）+ L1~L4（后台页/常驻主题/刷新拍/订阅收敛接线）；网页
  `webui_check.py` 新增「离开水质页后浊度历史仍增长 + 回页立即灌入」。
- 口径：后台采集只记**新鲜数据**（过期帧不编点），采样节流沿用各页
  `sample_ms`；相机流仍不常驻（20fps 大帧，只在悬浮卡打开时订）。

#### 历史页折线图：全量筛选 + 等距抽样 + 日期轴 + 无圆点（2026-11-24 用户「这个图表生成的也不对，时间怎么没年月日，我是要按照筛选的结果，就是导出的全部数据平均采集其中的点数进行绘制折线图，也一样不要显示点，支持下触摸手势」）
- **数据口径**：`CHART_MAX_ROWS=12000` 上限**废弃**。桌面 HistoryPage 图表
  取数改与导出同一条 **keyset** 路径（offset 恒 0、beforeTs=上批最小 ts-1），
  扫**全部筛选命中行**；第一帧拿到 total 后按 `stride = total // 2000` 流式
  等距抽样（每线再由 `_sample_points` 夹到 2000），首帧最新一行与末帧最旧
  一行**必留**（服务端批内升序、批序最新在前）。状态行改为
  「筛选共 N 行 · 已取 N 行 · 抽样 M 行绘图（每线最多 2000 点）」。
  网页 `page09_history.js` 同款（`chartCursor/chartScanned/chartStride`，
  `CHART_MAX_ROWS` 删除）。
- **X 轴带日期**：跨度 ≥10 分钟画两行（`yyyy-MM-dd` / `HH:MM`，桌面
  `LineChartDialog.MARGIN_B` 46→60；网页 canvas `mB` 同改）；缩放细看到
  秒级时单行 `HH:MM:SS`。桌面标题的缩放区间也带年份。
- **不画数据点圆点**：桌面 `LineChartDialog._paint_series` 的 `drawEllipse`
  块删除（与实时趋势同口径）；网页历史图本来只有悬停读数层的点标记。
- **触摸手势**：桌面 `LineChartDialog` 早已支持单指拖动/双指捏合/双击复位
  + 鼠标拖动/滚轮（`touch_begin/move/end` 纯状态机有回归）；网页历史图
  2026-11-22 已补 pointer 拖动/捏合/滚轮。本次未改手势实现，只把点击命中
  与日期标签的几何随底边距调整。
- **触摸轻点弹气泡（2026-11-24 用户「鼠标点击可以弹出悬浮窗，但是触摸
  点击弹不出」）**：触摸路径原来只接捏合/平移，抬手从不设置 `_picked` ⇒
  触摸永远没有点信息气泡。修法：`touch_begin(xs, ys)` 记单指按下点
  `_touch_tap`；`touch_move(xs, ys)` 位移超过 `TAP_SLOP`（横/纵任一）标记
  `_touch_moved`；`touch_end()` 在「单指 + 未移动 + 非捏合」时
  `nearest_point()` 吸附并设置 `_picked`（点空白处收起，与鼠标单击同口径）。
  `_on_touch` 现在把 `touchPoints()` 的 y 一起传入（原来只传 x，无法吸附）。
  回归锁：§17 图表交互 I9/I10（轻点弹点信息 / 拖动抬手不弹）。
- 回归锁：桌面 `--only=history`（328/0）——B 段改 keyset 断言（offset 恒 0、
  beforeTs 递减、状态行）、C1 全量扫描 12100 行（旧 12000 截断回潮即红）、
  C1a 抽样贴近 2000、C1b 首尾必留、C5 X 轴含 `%Y-%m-%d`、C6 无 `drawEllipse`、
  C7 正累计长值（"123456.78 m³"）必须进内容宽测量。
- **正累计列显示不全（同轮用户「历史数据表格正累计列数据显示不全」）**：
  根因是历史表**直接写 item**（不走 `set_rows`），`_rebuild` 从不调
  `refresh_content_widths()` ⇒ 列宽只按固定样本 `"2.30 m³"` 量；真实累计
  到 6 位整数（`"123456.78 m³"`）就被省略号截断。修法：`_rebuild` 末尾
  自己调 `refresh_content_widths()`（每页量最长单元格，装不下横向滚动）；
  `auto_min_texts[11]` 与网页 `COL_MIN_TEXTS[11]` 同步抬到
  `"999999.99 m³"`（无数据/短值页也够宽）。
  另用真 `LinkBridge` 连本机 6300 sim 实测：263 行全量扫完 → 状态行
  「筛选共 263 行 · 已取 263 行 · 抽样 263 行绘图」、曲线 263 点（keyset
  在真服务端上闭环）。

#### 搅拌泵页「电机温度」块（2026-11-24 用户「电机温度显示到红色框的位置上」）
- 桌面 `VfdPage` 运行状态卡 specs 末尾加 `("motor_temp", "电机温度", "℃", 24)`
  ⇒ grid 第 4 行第 2 列（「故障」右侧原空位）。缺 `motorTempC` ⇒ "--"（不编 0℃）；
  有值一位小数，按远端「显示渐变范围·电机温度」上色（缺范围=正文色）。
  `self._stats_grid` 留作位置诊断；轻量测试桩用 `getattr` 兜底。
- 网页 `page05.js` 同位置 `blk("motorTemp", "电机温度", "℃", 24)`，渲染读
  `doc.motorTempC / motorTempRangeMin/Max / motorTempGradient`。
- 回归锁：桌面 `--only=charts` H1b（位置 (3,1,1,1)/单位 ℃）、H4a/H4b
  （缺键 "--" / 有值 56.3）；网页门禁新增 page05 电机温度块静态检查。
- 板端 16:11 部署 client+web：备份 `backup-py-20261124-161138.tgz`，
  DB 1193 行、三单元 active、自动登录/窗口 1280x800 正常。

#### 历史图表「每线点数」可配（2026-11-24 用户「抽样 2000 还是太多，可以设置抽样数吗」→「默认 100」）
- 桌面历史页图表顶部状态行加「每线点数」下拉（objectName
  `comboHistChartPoints`）：100/200/500/1000/2000，**缺省 100**；本机设置
  `histChartPoints`（`settings.hist_chart_points()`）。改档即落盘；已取样本
  按旧点数抽样、无法上采样 ⇒ 清签名重取；取数期间下拉禁用（防新旧样本串）。
- 流式抽样与 `_sample_points` 都用该值（不再有硬编码 2000）。
- 网页 `page09_history.js` 同款下拉（`CHART_POINT_CHOICES`/`chartPoints`，
  prefs `histChartPoints`，缺省 100）；状态行同步显示所选点数。
- 回归：`--only=history` C8（下拉档位/回显）、C9（改 200 → 落盘且
  stride=total//200 重取）；C1a 按当前每线点数验抽样行数。

#### 板端 UI 卡死加固 + 一键栈转储（2026-11-24）
- 现场「点历史折线图后标题栏时钟不走、触摸不动」：客户端主线程 CPU 100%，
  gdb C 栈停在 Python eval/富比较（deep Python 调用），无法定位到行。
- **加固一**：列宽内容重测前比 **每列最宽像素签名**，不再比原始文本——
  历史表时间列每 3s 换一行、秒数字符串必变但宽度不变，旧逻辑每拍误判
  "内容变了"→重排 19 列（板端反复阻塞 UI 线程）。文本仍更新缓存供下次量算。
  图表视图下表格隐藏 ⇒ `_rebuild` 不量内容宽，切回表格时补一次。
- **加固二**：`__main__.py` 启动即 `faulthandler.register(SIGUSR1)`；
  现场卡死时 `kill -USR1 <pid>` 直接把全线程 Python 栈打到 journal
  （禁止再用 gdb 调 `PyRun_SimpleString`——实测会 SEGV）。
- 回归：历史页 C7a（时间戳只换数字/宽度不变 ⇒ 不触发列宽重排）。
- 部署：16:24 client+web（抽样可配 + 加固，backup-py-20261124-162444.tgz）、
  16:28 默认 100（backup-py-20261124-162821.tgz）；三单元 active。

#### 导航点「远端设置」偶尔打不开（2026-11-24 用户「导航点远端设置打不开，先切主界面再切远端就能开」）
- 根因：导航在触摸屏上要先拖动滚到底部；`FlickScroller` 拖动结束会补一对
  `MouseEventSynthesizedByQt` 的 press/release，旧实现用 **250ms 时间闸**
  吞掉这期间**所有** clicked ⇒ 松手后立刻真点「远端设置」被一并吞掉；
  等一下再点（或先点主界面）就正常。
- 修法：`NavPanel` 给每个条目装 eventFilter，**只吞 source==
  MouseEventSynthesizedByQt 的 press/release**（吞到合成的 release 立即解除
  guard）；真鼠标事件/新的触摸序列直接放行。250ms 定时器保留只作兜底。
- 回归锁：§17 `--only=charts` J1~J3（拖动后真点击仍导航 / 合成事件被吞且
  guard 立即解除 / 合成后下一次真点立即可用）。
- 板端 16:35 只换客户端：备份 `backup-py-20261124-163554.tgz`，单元 active、
  窗口 1280x800。

### 按钮灯闪烁（2026-11-24/25 用户口径，**取代**旧的「启动/停止严格互斥、复位仅点动」）
- **复位灯**：需要复位（报警锁存 ∨ 急停锁存）⇒ **闪烁**（不再常亮）；
  不需要复位 ⇒ 熄灭（按住复位键仍保留点动镜像，用户口径「跟现在一样」）。
- **启动灯**：自动模式、未运行、启动条件满足（`start_ready`）⇒ **闪烁**
  （切进自动时、以及**按下停止并完全停稳后**都要重新闪，提示可再启动）；
  进入运行相 ⇒ 常亮；停止减速/换相停泵期间 ⇒ 灭（由 stopping 抢占）。
- **停止灯**：下发停止后 **VFD 仍在减速**，**或阀门还没关完** ⇒ **闪烁**；
  全部停稳 ⇒ **常亮**；运行中 ⇒ 灭。
  · 「阀门没关完」只统计**本次停止确实发了关阀命令（`resetValvesOnStop`）**
    的阀；现场 **1~6 阀全部接线/有到位反馈（2026-11-25 用户订正，此前
    “只有阀1”作废）** ⇒ app 直接按反馈判是否到关位，全部 Closed 即清。
    `AutoController._stop_close_pending` 由 `stop_all_devices(reset_valves=True)`
    置位，重新启动也清。
  · **停稳后启动灯闪 + 停止灯常亮可同时亮**（2026-11-25 用户追加口径：
    “按下停止，完全停止后应该让启动灯闪烁” + 上一轮“停稳后停止灯常亮”
    ⇒ 不再严格互斥）。
- **六项毫秒配置**：auto 节 `lights` 子节（`startOnMs/startOffMs/
  stopOnMs/stopOffMs/resetOnMs/resetOffMs`，缺省 500/500，范围 50~5000）。
  GET 恒回六键；SET 逐键校验（越界明确拒、整包回滚）、立即生效并落盘；
  桌面/网页远端设置各有三组「亮/灭 ms」控件，缺键不冲缺省、无管理权限置灰。
  ⚠ 服务端 tick 100ms：50ms 档实际只能量化到约 100ms（见待办）。
- **实现落点**：`buttons.ButtonStation`（`blink`/`_blink_t0`/`start_ready`/
  `start_prompt`/`stopping`，每拍按相位算灯电平再走原有“回读≠目标才补写”）；
  `app` 每拍 `set_start_ready`（`start_preconditions` + `mode_config_error`）、
  `set_start_prompt(auto && !running)`、`set_stopping(vfd.is_stopping() or
  阀门未关完)`；`devices.VfdM512.is_stopping()`；`autocontrol` 的
  `_stop_close_pending`。
- **回归锁**：`server_py/tests/test_button_blink.py`（20/0，灯名分别用不同
  亮灭时长；含复位闪/停、停稳后启动再闪）、`test_button_blink_sim.py`
  （11/0，真实 sim 直接观测 DO7/8/9）、`test_mode_lamp_rules.py`（36/0）、
  `test_sim_full_flow.py`（28/0）；客户端 remote 段覆盖三组控件/载荷/回显。
- **已知待办（独立复核 found，未修）**：VFD 离线时 is_stopping 可能永真、
  组合循环换相停泵会误熄运行中启动灯、50ms 档低于 100ms tick、
  复位灯“无锁存按住亮”是否保留需用户确认。

#### 运行保护扩展：电箱超温 + 压力1/2 过高（2026-11-25 用户定案）
用户口径：「远端设置运行保护没有电箱超温保护」「参考电机超温保护设计」
「还有压力1 压力2也做进运行保护」。

- **电箱超温保护**（protect 节三键）：`boxOverTempEnabled` /
  `boxOverTempLimitC`（℃，缺省 60）/ `boxOverTempStop`（缺省 true）。
  取数 = **温湿度变送器**（TOPIC_TEMPHUM 那台，`TempHum.temp_tenths()/10`），
  不是模拟量温度传感器；连续超阈值 2 秒 ⇒ 告警（kind=9 电箱超温，
  dev=KIND_TEMPHUM）+ 按配置停设备 + **报警锁存**。**数据不新鲜不判定**
  （`last_ok_ms` 距现在超过 `temphum_stale_ms` ⇒ 不判：变送器偶发读失败时
  `temp_valid` 仍为 True、值还是上一次的旧值）。
- **压力1/2 过高保护**（protect 节各三键）：`press1HighEnabled` /
  `press1HighLimitMpa` / `press1HighStop`，`press2High*` 同形。取数 =
  `_history_analog_by_name("压力N")`（与历史 press1_mpa/press2_mpa、首页
  压力卡同一条换算链；通道在 collect 节 `press1Channel/press2Channel` 可配，
  **按名字找通道**不写死下标）。连续超上限 2 秒 ⇒ 告警（kind=11/12，
  dev=KIND_ANALOG_IO）+（按配置）停设备 + 锁存。
  **读不到 ≠ 超压**（通道无效/未标定/本拍没读到 ⇒ 不判定）。
- 三组都**缺省不启用**（阈值与现场工艺强相关）；`limit <= 0` 视为不判。
  去抖 2s、锁存、只报警（stop=false）、锁存未解不重复动作——与压差/电机
  超温保护同款。
- **告警 kind 号段订正**：原来电机超温误用 **8**（8 = 压差过高）⇒ 客户端
  报警页把电机超温显示成「压差过高」。现统一为：8 压差过高 / 9 电箱超温 /
  10 电机超温 / 11 压力1过高 / 12 压力2过高（server_py、client_py、
  webui_py 三端 proto 逐值一致；web hub 的类型文案同步）。
- **三端 UI**：桌面远端设置「运行保护」卡新增三张分段（电箱超温 / 压力1 /
  压力2），每段 启用 + 阈值 + 触发后停设备；压力阈值界面 kPa、协议 MPa
  （÷1000）。网页 page14 同款控件/载荷/回显。缺键（老服务端）回缺省、
  无管理权限置灰。
- **服务端落点**：`app._protect_box_over_temp_step`（含 `_box_temp_c`
  新鲜度闸）/ `_protect_press_channel_step(now_ms, index)` / config
  `protect_*` 九字段 load/save / `_config_protect_doc` + `_cfg_protect_value`
  + `_PROTECT_ATTR`；tick 里排在电机超温之后（自动控制之后，保护停设备时
  控制器状态已是本拍最新）。
- **回归锁**：`server_py/tests/test_protect.py` A11/A11b~d + H1~H7（电箱）+
  I1~I8（压力1/2）——共 130/0；`test_server_alignment.py` 的 protect
  PY-only 白名单加九键；客户端 remote 段 A10f/A10g2/A10g3/A10i + 载荷
  十七键（224/0）；网页 `webui_check.py` 的 protect 节 GET/SET 九键往返 +
  page14 静态接线。

#### 顶栏故障详情卡 + 搅拌泵显示 600 变 599（2026-11-25）
- **故障徽章可点开详情**：标题栏「故障N / 离线N」原来是裸 QLabel 点不动。
  现为 `widgets.HealthLabel`（QLabel + clicked 信号，手型光标/▾/tooltip），
  `TitleBar.healthClicked` → `MainWindow._on_health_clicked` →
  `hsfsrv_client/health_popup.py` 的 `HealthPopupCard`。
  卡片 `Qt.Popup`（**点卡片外部自动关闭**，与二维码弹卡同款），列**非正常
  设备**：状态/名称/类型/原因/错误码/地址（`slim_addr` 紧凑显示，完整值进
  tooltip）；故障→离线→过期排序；`QScroller` 触摸拖动滚动；打开期间主窗
  200ms 刷新按 `health.seq` 变化才重铺（稳态零重排）。
  回归锁：客户端 home 段 §23-4e~4m（文案 ▾ / 信号 / 只列异常 / 原因与地址 /
  seq 未变不重建 / 主窗开收卡）。
- **主页搅拌泵「设定 600 显示 599」**：不是截断，是输出频率回读的 0.1Hz
  级量化（600rpm=20.835Hz ⇒ 20.80 ⇒ 599.0）。VFD 无转速传感器，本属换算
  估计值 ⇒ 运行中且已在给定附近（`≤max(2rpm, 1%)`）时显示**给定换算值**
  （600），加速/减速/停机差值大仍显示实际换算值；数值与渐变取色同一口径
  （`HomePage._vfd_display_rpm`）。网页 `page00_home.js` 同款。
  回归锁：home 段 §23-4b/c/d（稳态 600 / 加速 300 不跳 / 减速 120 不对齐）；
  网页静态断言「主页转速稳态按给定对齐」。

#### 自动模式逐模式「搅拌加速度」（2026-11-25 用户定案）
用户口径：「远端设置中自动模式每个单模式，搅拌机变频器增加加速度设置，用
指定的加速度加速到设定转速，用本机模拟器验证」。

- **协议/配置**：auto 节每个模式新增 `stirAccelRpmS`（int，rpm/s）：
  · 范围 0~1440，**0 = 跟随变频器当前加速时间**（缺省；老配置/老客户端缺键同值）；
  · SET 非整数明确拒（fail-closed），越界夹取；GET 恒回显；落盘 key
    `stir_accel_rpm_s`（load 兼容 camelCase）。
- **执行口径**：控制器在 `_start_stir` 启动搅拌泵前，若本模式加速度 >0 且
  该泵**当前是停的** ⇒ 按 `满量程加速时间 = 额定转速 / 加速度` 写变频器
  **0xF011 P0.17**（0.1s 定点；`VfdM512.set_accel_time_s`）→ 再 START
  ⇒ 从 0 到设定转速约 `stirRpm / stirAccelRpmS` 秒。
  · 运行中（carry/direct 保持运行）**不写**参数（RunLocked/安全）；
  · 参数写失败**不阻断启动**：记日志后按变频器原加速时间启动。
- **三端 UI**：桌面/网页每个模式块在「搅拌电机」下新增「加速度」微调框
  （rpm/s，步进 10，0 显示「跟随变频器」）+ 到速提示「≈ X 秒」；载荷
  `stirAccelRpmS`，缺键回 0；窄窗不撑横向滚动（桌面 A6s 锁 626=626）。
- **模拟器验证（关键）**：`server_py/tests/test_auto_scenarios.py` 新增
  第三段「逐模式搅拌加速度」（真 sim + 真协议，场景进线程避免阻塞事件循环）：
  · 300rpm/s、目标 600rpm ⇒ 0xF011 raw=48（4.8s 满量程）⇒ 实测 1.89s 到速
    （期望 2.0s）；
  · 0（跟随）⇒ raw=100（10s）⇒ 实测 4.00s（期望 4.17s）；
  · 全文件 **67/0**。
- **回归锁**：`test_auto_modes.py` modes SET/GET/落盘 + 非法值拒（26/0）；
  客户端 remote A6g2a/b、A6h2（234/0）；webui_check auto 节回读
  `produce.stirAccelRpmS=300` + page14 静态接线。

#### 搅拌最高转速限制（2026-11-25 用户定案）
用户口径：「远端设置中要增加搅拌机最高转速限制，你看是服务端做限制还是在
变频器上改参数，界面同步」。

- **定案：服务端做限制，不改变频器最高频率参数**。理由：服务端钳位对
  自动/手动/点动/启动捎带给定**所有入口**立即生效、可审计、不依赖总线写
  参数（真机参数可能 RunLocked/需写使能），也不影响 VFD 其它用途。
- **协议/配置**：auto 节**顶层**键 `stirMaxRpm`（int，100~1440，缺省 1440
  =不额外限制）。GET 恒回显；SET 非整数 fail-closed、越界夹取；落盘
  `auto.stir_max_rpm`（load 兼容 camelCase）。
  **改上限会同时夹取所有已存在模式的 stir_rpm**（GET/落盘/运行期同一份
  有效值，不留「配置 1440、实际按 600 跑」暗账）；同包再写超限模式转速也
  按**新上限**夹取（先处理 stirMaxRpm 再处理 modes）。
- **执行钳位**：
  · 自动模式 `_start_stir`：`rpm = min(cfg stirRpm, stirMaxRpm)`（防御旧
    配置/运行期改上限），超限记日志；
  · 手动变频器控制 `_h_vfd_control`：`rpm > 上限` 和 `|hz| > 上限对应 Hz`
    都钳到上限并记 `ctrl` 日志（OP_START/JOG/SET_SPEED/SET_FREQUENCY 全覆
    盖；停止/复位类不受影响）。
- **界面同步**：
  · 桌面远端设置自动模式配置卡**顶部**新增「搅拌最高转速」微调框（100~
    1440，缺省 1440），载荷顶层 `stirMaxRpm`；改上限立即把 4 个模式块
    rpm 框 max 压到该值（超限值自动夹回）；缺键（老服务端）不误改。
  · 桌面搅拌泵页：VFD 快照/遥测新增 `stirMaxRpm` ⇒ 手动调速输入框/滑块
    上限取 `min(额定, 限速)`，频率模式最大 Hz 同步按限速折算；提示行标
    「最高限速 X rpm」。
  · 网页 page14 同款全局控件/载荷/回显；page05 同款量程同步（model.py
    解析 `stirMaxRpm` 并随 `to_json` 下发）。
- **回归锁**：服务端 `test_auto_modes.py`（SET 上限夹取已有模式/超限模式
  转速/非法值拒/落盘往返）；`test_auto_scenarios.py` 第三段补「手动调速
  钳位」（600rpm 启动、1200rpm 调速→20.83Hz、45Hz→20.83Hz，真 sim，67/0）；
  客户端 remote A6z4a~f（控件/上限联动/载荷/回显/缺键）+ vfd 段 D4/D5
  （手动页 600rpm 量程、频率模式≈20.8Hz）；webui_check 519/0（含顶层键
  往返 + page14/page05 静态接线）。

#### 滤速显示（m/h = 瞬时流量 / 过滤面积，2026-11-25 用户定案）
用户口径：「这里增加滤速显示，单位 m/h，公式是瞬时流量/面积，面积在远端
设置中设置一个固定值，每次更新瞬时流量的时候，计算下滤速并更新」。

- **服务端算、客户端只显示**（三端同一份值，避免各写一份除法）：
  · collect 节新增 `filterAreaM2`（double，m²；0=未设置；范围 0~10000；
    GET 恒回显 / SET 非数字拒、越界夹取、落盘 `serial.filter_area_m2`）；
  · `encode_flowmeter` 每帧：在线且面积>0 ⇒ 下发 `filterAreaM2` +
    `filterVelocityMh = flow / area`（m/h，3 位小数）；面积=0 ⇒ 不下发
    滤速键（界面 "--"，不写 0）；流量计离线 ⇒ 不下发滤速键（不拿旧流量算）；
  · 流量为 0 时滤速是 **0**（真值，不是缺数据）。
- **界面**：
  · 桌面首页流量计卡主数值右侧新增「滤速 m/h」（原来截图红框空位），
    面积未设置/无数据显 `--`；`model.FlowMeterSideState` 解析两键。
  · 桌面远端设置「采集配置」新增「滤速计算面积」微调框（m²，3 位小数，
    0 显示「未设置」），与采集配置同一次 SET 下发、同卡权限闸、应答带键
    才回显（老服务端缺键不误改）。
  · 网页端同款：page00 流量计卡 `.fm-row`（瞬时流量左/滤速右）+ page14
    面积控件；hsfweb model 解析两键并随 `to_json` 下发。
- **回归锁**：服务端新文件 `tests/test_filter_velocity.py`（13/0：缺省不
  下发/面积换算/改面积下一帧生效/离线不算/流量 0/GET/拒非法/夹取/落盘，
  已注册 run_all）；客户端 home 段新增滤速落地/缺键 `--`；remote A11b2/
  A11c/A11d（面积回显/缺键不误改/进 collect 载荷）；webui_check 519/0
  （page14 面积控件 + page00 滤速块静态接线）。

#### 滤速负零 / 改名 / 本机全量回归（2026-11-25 晚）
- **「-0.00」根因**：流量计零点会报 `-0.0`/极小负值（板端快照实测
  `flow=-0.0`），滤速=`flow/area` 得 `-0.0` ⇒ 两位小数显示 "-0.00"。
  修法：服务端滤速按**过滤方向**取 `max(0, flow)/area`；桌面/网页显示端
  再把负值夹 0（负值一律 0.00，不出现负号）。流量显示也做零值收口。
- **改名**：主界面流量计卡「滤速」→ **「罐体滤速」**（桌面 StatBlock +
  网页 page00 + webui_check 静态断言）；tooltip 同步。
- **客户端泄漏门 K 假红根因（2026-11-25 实锅）**：网络设置页读/写网口用
  **匿名后台线程**（PowerShell/nmcli 子进程）持有页面；K 切页后销毁主窗
  时线程未收工 ⇒ 整个远端设置页（~1057 控件）被暂时留住 ⇒ 门禁判泄漏
  （干净 HEAD 同样复现）。修法：线程命名 `hsf-eth-refresh`/`hsf-eth-apply`
  并加入 `test_leak_gate._BG_THREAD_NAMES`，销毁前 `_wait_bg_threads` 等
  其收工。
- **本机全量回归结果**：
  · 客户端总回归 `test_hsf_regression.py`：**2149 / 0 / 0**（含全部 19 组
    UI 段、remote/vfd/home、textfit §38）；
  · 客户端泄漏门：**52 / 0**（F/H/I/J/K/L/M 全绿，K 修后 +0 控件/+0 线程）；
  · 服务端 run_all：**44/45 组**；唯一红项「双端协议面对照」是 C 端
    `bin/HsfServer.exe --sim` 登录阶段连接被对端中止（C 栈旧二进制/本机
    环境，PY 侧协议与功能项全绿；C 栈已按用户口径不用）；
  · 服务端泄漏门：**33 / 0**；滤速 13/0；自动场景 67/0；三模式 26/0；
    运行保护 132/0；
  · 网页 `webui_check --server-port 6200`：**519 / 0**。
- **本机模拟口径**：面积 0.5m²、3.65m³/h ⇒ 罐体滤速 7.31m/h（验收截图属
  `verify/*.png` 一次性产物，**不入库**）。

#### 板端部署记录（2026-11-24 ~ 11-26，汇总）
中间多次部署的过程细节已合并到这里；数据/配置保真是每次共同的硬约束。

- 2026-11-24：趋势/历史图批次（备份 `backup-py-20261124-160119.tgz` 等）；
  2026-11-25 08:52 按钮灯闪烁（`backup-py-20261125-085213.tgz`）；10:26
  运行保护扩展（`backup-py-20261125-1030.tgz`）；11:18 故障详情卡 + 转速
  对齐（`backup-py-20261125-1118.tgz`，11:20 补 HealthLabel 按下接收）；
  14:26 搅拌加速度（`backup-py-20261125-1424.tgz`，14:31 补 QSpinBox
  定宽）；16:13/16:25 限速 + 滤速 + 负零钳位（`backup-py-20261125-1612.tgz`
  / `backup-py-20261125-1620.tgz`）；2026-11-26 08:16 负零显示 + 「罐体滤速」
  改名（`backup-py-20261126-0815.tgz`）。
- 每次均：停服务 → 备份 → 解包（**排除 `hsfsrv_client/bin`**）→ 清
  `__pycache__` → `compileall` → 按 server→web→client 起；**绝不覆盖
  `hsf_data.db*`、`cfg/`、ARM `bin/hsb_bridge.so`**。
- 最终板端状态（2026-11-26 08:17 复检）：三单元 active、6100/8080 监听；
  `history=1214`；`filter_area_m2=0.075`、`stir_max_rpm=1000`；首页
  **罐体滤速 0.00 m/h（无负号）**；`.so` sha256 仍 `a3a7faf1…`。
- 现场观察（与本次改动无关）：当时压力1/2 显示 `--`、0.00mA 回路无电流，
  健康「故障2」；需另行排查模拟量/相机等设备状态。
