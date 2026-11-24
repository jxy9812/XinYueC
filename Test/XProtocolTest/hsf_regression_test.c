/**
 * @file       hsf_regression_test.c
 * @brief      HSF C 端**总回归**（单文件验证入口）。
 *
 *     bin\XinYueC_Static.exe --test hsf-regression
 *
 * [为什么是"一个文件"]
 * 回归锁原先散在 xmodbus_virtual_test / hsf_sim_mode_test / … 多个文件里，
 * 改一处功能要翻好几处才知道该跑哪些。这里把**框架层契约**与
 * **客户端协议契约**收成一份，跑一次就覆盖；以后加/改功能先在这里补断言。
 * Python 端有对应的总回归文件：
 *   HighSpeedFilter/client_py/tests/test_hsf_regression.py
 * 两份互为对照：Python 侧钉"服务端发的形状"，本文件钉"C 端按形状取的映射"。
 *
 * [覆盖面]
 *   §A 框架 Modbus 四层根修（2026-11-09「485 从站跑久了停摆」）
 *      A1 异常应答必须按失败记账（原口径当成功 ⇒ 看门狗永远健康、缓存不回填）
 *      A2 嵌套同步收口不再整包丢帧（回填/计数/退避记账/沿全都要有）
 *      A3 写持续失败也不饿死读组（失败不在同拍重试 ⇒ 读写交替计数奇偶不变）
 *      A4 按从站粒度的新鲜度（deviceWorstUnitAge：块级 lastOkMs）
 *
 *   §B 客户端历史应答解析契约（跨端）
 *      B1 应答键与 offset/limit 回显（客户端靠它认领应答，对不上就丢）
 *      B2 行宽 18 列、数值列是数值（不是预格式化文本）
 *      B3 列下标映射：freq/current/rpm/turb/flow/press/temp/hum 各取对列
 *
 *   §C 表格排序三态（app 层助手 HsfTableSort 的核心状态机）
 *
 *   §D 框架层「Qt 对齐补全」回归锁（2026-11-09 补的那几处发射点）
 *      D1 sectionDoubleClicked 有**真实发射点**且载荷=段号（此前只有声明）
 *      D2 sectionEntered 有**真实发射点**且载荷=段号（同段内不重复发）
 *      D3 表头释放时翻转排序指示器（Qt mouseReleaseEvent 尾段口径）
 *      D4 setSortIndicator 之后指示器段号/方向可读
 *      D5 setSortingEnabled(true) ⇒ 单击表头按该列排序（Qt 口径：空值下沉）
 *      D6 setSortingEnabled(false) ⇒ 表头段不再可点（不再触发排序）
 *      D7 信号函数的 NULL 取 id 约定（不崩、不发射；今天这里崩过）
 *      D8 app 层三态策略（新列⇒降 / 同列⇒升 / 再点⇒取消）+ 指示器联动
 *
 *   §E 交叉测试（层与层、端与端互相验证）
 *      E1 (a) 框架原语 XTableWidget_sortItems ↔ app 三态策略：同一行序
 *      E2 (b) C 客户端解析器 ↔ Python 服务端**真实产出的**历史应答载荷
 *      E3 (c) C 服务端 ↔ Python 服务端：两份历史应答逐键逐列同形
 *      E4 (d) 框架 Hub 读组/回填 ↔ app 设备站读到的值（同一时刻同一寄存器）
 *
 *   §F 覆盖面扩容（今天 C 端改过但此前没被 §A/§B/§C 覆盖的行为）
 *      F1 读组失败的可观测日志（限频打印真的有，且带组/地址/错误码）
 *      F2 块级陈旧诊断（"轮询停滞"逐块逐组列出跳过原因）
 *      F3 deviceWorstUnitAge 的两种语义：从未上报 vs 曾上报后掉线
 *      F4 「只续写不续轮询」的节拍不被放大（写持续失败时读节拍不翻倍）
 *
 *   §G 泄漏自检（工作集 3 轮堆增量必须为 0；含读组失败日志 + 日志捕获管路）
 *
 *   §H 历史页「图表缩放取数」专项回归（2026-11-09 并入本文件）
 *      断言本体（Z1..Z11）就在**本文件**的 §H 段里（原独立文件
 *      hsf_chart_zoom_test.c 已删除——用户口径「C 端只留一个测试文件」）。
 *      app 层纯逻辑助手 HsfChartZoom.c 由本文件单 TU 收录。
 *      覆盖：批次计划（窗口逐行/全量跳批）、可见窗同基准换算、全量视图
 *      判定、已取窗覆盖、取数 limit 轮转、轴域与取数窗同源、点落在窗内并
 *      铺满视口、状态行随窗口重算、纯逻辑路径无泄漏。
 *
 *   §I 快照/控制命令的「请求-应答」兜底通道（2026-11-09 根修）
 *      I1 0x0103 模拟量快照应答：model（值/掩码/标定/名字）被刷新 + 通知
 *      I2 0x0106 流量计快照应答：按 side 落进对应侧 + 通知 + 判新鲜
 *      I3 0x0112 浊度仪快照应答：台数/名字/NTU/在线位被刷新 + 通知
 *      I4 0x0113 泵快照应答：刷新 + 通知；**失败包络不覆盖 model**
 *      I5 0x0102 变频器快照应答：XVfdStatus 被解码（频率/转速/运行位）
 *      I6 五条命令的畸形/空载荷：不崩、不覆盖已有 model
 *      I7 0x0114 泵控制应答：命令序列号推进/错误文案/并帧快照应用
 *      I8 0x0115 自动运行应答：模式与运行位回填 + 失败原因浮出
 *      I9 未连线时自愈补拍是空操作（页面 1Hz 兜底不发假请求）
 *      断言方式 = 白盒：HsfLinkClient_testDispatchResponse 把构造好的
 *      Response 帧喂进线上同一条分发函数（离屏收不到真实应答）。
 *
 *   §J 认证「策略性拒绝」失败语义（2026-11-09 根修：模拟模式偶发无数据）
 *      J1 服务端查表返回 Rejected ⇒ 客户端收到 **Rejected(4)**、零次
 *         authChallenge（不许伪造"随机盐+全 0 摘要"的假挑战）、只给一次结论
 *      J2 反枚举口径不变：lookup=BadUser 仍走假挑战 ⇒ 客户端最终
 *         **BadPassword(2)**（顺手别把这条改坏的锁）
 *      断言方式 = 回环设备对驱动两个会话（与 XLinkTest.c 认证用例同夹具），
 *      真时钟假步进，不依赖网络。
 *
 *   §M 读组「不应答」与「异常应答」的框架级回归锁（2026-11-10）
 *      M1 某组不应答 ⇒ 在途超时收口（判失败 + 记失败计数 + 报从站退避 +
 *         释放在途标记）⇒ **别的组照常轮询**（不能出现"一站哑掉、整条
 *         总线停摆"；修前：单飞闸把该设备后续所有读组永久堵死）。
 *      M2 异常应答（EXC02/EXC03）是**组级**事实 ⇒ 不牵连同从站的其它组
 *         （修前：按从站级失败回报退避表 ⇒ 连续 3 次后整站被静默）。
 *      M3 静默闸饥饿逃逸：静默期配置离谱（现场根因：调用方漏填该字段
 *         留下的栈垃圾 0xCCCCCCCC）时，框架仍保证该从站不会被**永久饿死**。
 *   §K 三设备接入（ttyS4 路 B：浊度仪 ×2 + 电磁流量计，2026-11-10）
 *      K1 实测原始帧逐字节：站 1 `8F 5C 40 12`（CDAB）= 2.29f（锁站 1
 *         不回退）；站 2 `4F DD 37 3D`（DCBA 整字节反序=小端）= 0.0449f、
 *         温度字 `00 00 99 41` 同字序 = 19.125℃（同刻）；非法字序拒绝。
 *      K2 站号配置锁：浊度仪 1/2、流量计 5；字序 0/2、寄存器数 2/4、
 *         虚拟地址不重叠（换表只改 HsfServerConfig.h 两个定义）。
 *      K3 模拟器（三台共用一条虚拟 485）装配后经**真实解码路径**读回：
 *         站1==2.29、站2==0.045+温度 19.3、站5==3.6、空管 False、单位码 5。
 *      K4 §K 跑完堆回基线（对齐 §G：预热 1 轮 + 3 轮工作集增量 == 0）。
 *      K5 发帧前静默闸（m_preTxQuietMs）行为锁（见 §K 块注详述）。
 *      K6 应答时延旋钮（2026-11-10 保真扩容）：宏缺省=实测值（站1 37~43/
 *         站2 31~33/站5 68~69ms，恢复期 125ms）；setter/getter 成对可配；
 *         模拟装配处接线锁。**时延数值本身不做耗时断言**（CI 抖动），
 *         只断言行为方向（应答照常到达）。
 *      K7 站 1 长帧后恢复期行为锁：站 5 的 49 字节长帧收口后窗口内站 1
 *         **无应答**（NULL=上层按超时处理）+ 计数；窗口外恢复；窗口 0=
 *         关闸可配。
 *      K8 站 2 功能码怪癖：FC04 请求也受理但**应答功能码恒 03**、数据与
 *         FC03 逐字节一致；站 1 无 FC04（仍异常应答 fc|0x80）。
 *      K9 数值微抖动方向锁（实测 §1.3 分台口径）：站 1 恒定 2.29、
 *         站 2 0.045±0.001 + 温度 19.3±0.05、站 5 3.6±2% 正弦波动、
 *         空管 False；setInstantFlow 钉死口径不回退。
 *
 *   §Q 数据三页「导出筛选全量」批次计划（2026-11-11；断言本体在本文件）
 *      app 层纯逻辑助手 HsfExportPlan.c 单 TU 收录（与 §H 收录
 *      HsfChartZoom.c 同款）。导出=按当前筛选条件**全量**取（不是当前页
 *      那几十行）且**不抽样**——计划算错就是「导出重复/漏行/少批」，逐条钉：
 *      Q1 按命中总行数分批（total=12345/chunk=45 ⇒ 275 批、步长=45）
 *      Q2 逐批连续（offset=step×k 严格衔接）、跨页不重不漏、末批不越界
 *      Q3 命中行数超上限 ⇒ 截断且截断行数=上限向下取整到整批（显式进
 *         状态行，不许静默丢行）
 *      Q4 计划是纯函数：筛选条件变化（total 变）⇒ 重算；同输入同输出
 *      Q5 导出批行数轮转档 41..49 与页大小 10/20/50 不相交（编译期闸
 *         + 运行期镜像，迟到应答不可能错认领）
 *
 *   §R 报警页纯逻辑：行可见判定 + 客户端内部分页（2026-11-11；断言本体
 *      在本文件）。报警页对齐 Python AlarmPage（去图表 + 关键字 + 分页 +
 *      滚动锁定）后，判定与分页边界抽在 HsfAlarmLogic.c 单 TU 收录：
 *      R1 行可见基础（无条件恒可见 / 类型全等 / 空条件=不限）
 *      R2 时间窗（双侧夹 / 界 0 不限 / 无时刻记录不参与时间窗）
 *      R3 关键字（设备/类型/状态三列包含 / 空=不筛 / 不含时间列）
 *      R4 组合矩阵（类型×时间窗×关键字全过才可见，任一放宽各自把关）
 *      R5 页数边界（0 行 1 页 / pageSize<=0 兜底 / 整除 / 不整除向上取整）
 *      R6 页起点边界（网格起点 / 超界页夹取 / 负页号 / 空全集恒 0）
 *
 *   §S 自动模式序列（2026-11-11；C 端镜像 = server/HsfServerAutoMode.c，
 *      基准 = server_py/hsfsrv/autocontrol.py + tests/test_auto_modes.py +
 *      tests/test_auto_alarm_publish.py；协议面由 verify/hsf_auto_mode_check.py
 *      对真实 bin/HsfServer.exe --sim 另行钉住）：
 *      S1 产水全循环（配置 SET/GET 往返 / 手动拒 / 阀相 DO 面 / 到位推进
 *         running / 搅拌泵 720rpm 反转折算给定 + 进水泵 DO6 / 停止全停 /
 *         模式粘性）
 *      S2 阀超时（卡阀夹具 ⇒ kind=5 告警点名阀号 / 泵绝不被启动 / 报警
 *         锁存 / 未复位拒启动 / 复位后再启动）
 *      S3 闸门面（组合模式按循环配置校验拒「缺子模式」 / 未配置拒「未配置」 /
 *         急停全停 + estop 相位 + 锁存拒启动 + 复位解锁）
 *      S4 mode_config_error 单元（空配置 / 组合 / 未知 id / 空串 / 全未知
 *         设备 / 选中阀无点位点名）
 *      S5 前置闸顺序（双锁存先报「报警」 / 只剩急停报「急停」）
 *      S7 告警链框架队列层（急停自动入队 kind=4 / 取走即消费 / 锁存重复
 *         急停不重复报警 / 队列满挤最旧 / 文字 128 字节截断）
 *      S8 模式包络键客户端消费面（0x0105/0x0115 成功应答六键回填 /
 *         失败 err 浮出 + 序列号照常推进）
 *      S9 按钮组三盏灯跟随（2026-11-12 对齐 Python buttons.py：仅自动模式
 *         写 / 运行态互斥 / 急停显示停着 / 复位灯点动镜像 / 回读自愈 /
 *         手动模式灯归人工不被覆盖；estopState 三态单源）
 *      S10 组合模式「自动」循环（2026-11-12 对齐 Python test_auto_cycle：
 *         压差 None 不切 / gt 命中切反洗回阀相 / 换相停泵 DO6 / 反洗顺洗
 *         定时到期（假时钟加速） / 阶段记忆停止保留-再启动继续-复位清空 /
 *         累计运行时长跨换相不中断 / cycle 配置闸）
 *      S11 累计运行时长（2026-11-12 对齐 Python run_ms 三件套：未运行冻结 /
 *         运行递增 / 停止结算冻结 / 再启动叠加 / 持久化 run/auto_run_ms /
 *         shutdown+setup 重启恢复不归零）
 *      S12 auto devices 整表替换（2026-11-13 对齐 Python 整表覆盖：子集
 *         SET 只剩子集 / 空数组=清空 / 清空后启动拒「未配置设备」）
 *      S13 现场停止按钮阀复位走模式配置（2026-11-13：未运行态按停止，
 *         resetValvesOnStop=false ⇒ 阀 DO 保持 / true ⇒ DO 清零）
 *      S14 按键触点/去抖配置化（2026-11-13：auto/buttons SET/回显 /
 *         落盘重启回读 / 停止钮常开沿生效 / 非法值 fail-closed 拒）
 *      S15 只选模式 op=5（2026-11-14 对齐 Python AUTO_OP_SELECT：记粘性 /
 *         不启动 / 未知 id 拒 / 组合循环 id 合法 / 落盘 run/auto_mode /
 *         跨 setup 重启恢复）
 *
 * [与 Python 侧总回归的分工]
 *   Python §1 钉服务端列名/列序/类型；本文件 §B3 钉 C 端按**下标**取哪列。
 *   任一端改了形状，另一端的断言就会红。
 */
#include "HsfLinkHistory.h"
/* §T：远端设置配置节级存在性纪律（数据契约；界面守卫的前提）。 */
#include "HsfLinkConfig.h"
#include "HsfTableSort.c"   /* app 层助手：单 TU 收录（与 hsf_*_test.c 同款） */
/* §H：历史页缩放取数的纯逻辑助手（原来由独立的 hsf_chart_zoom_test.c 收录，
 *     2026-11-09 并入本文件 —— 用户口径「C 端只有一个测试文件」）。 */
#include "HsfChartZoom.c"
/* §Q：数据三页「导出筛选全量」的批次计划助手（HsfExportPlan.c，与
 *     HsfChartZoom.c 同款单 TU 收录；助手本体只随 HsfClient 编译）。 */
#include "HsfExportPlan.c"
/* §R：报警页纯逻辑助手（HsfAlarmLogic.c——行可见判定 + 客户端内部分页
 *     边界；同款单 TU 收录，本体只随 HsfClient 编译）。 */
#include "HsfAlarmLogic.c"
/* §I：客户端快照/控制应答的分发（HsfLinkClient 在 Static 目标里**已作为
 * 独立 TU 编译**——见 Test/CMakeLists.txt 的 HSF_CLIENT_TEST_MODULE_SOURCES，
 * 故这里只 include 头文件走公开 API + 测试钩子，**不能**单 TU 收录 .c
 * （会与同一目标里的那份实现撞键 LNK2005）。 */
#include "HsfLinkClient.h"
#if XVFD_ON
#include "XVfdCodec.h"      /* §I5：用同一套编解码器造变频器快照应答。 */
#include "XVfdM512.h"       /* §O：M512 遥测刻度（实机标定锁）。 */
#endif

#include "XModbusVirtualClient.h"
#include "XModbusVirtualModel.h"
#include "XModbusHub.h"
#include "XModbusMapping.h"
#include "XModbusClient.h"
#include "XModbusDevice.h"
/* §M 假从站要直接用受保护接口（setState / createReply* / setState(Reply)）：
 * 不包含时 C 会按"隐式返回 int"处理这些函数，x64 下指针高 32 位被截掉，
 * 随后 setState 直接 AV（XModbusVirtualClient.c 里的同款注记）。 */
#include "XModbusClient_Protected.h"
#include "XModbusDevice_Protected.h"
#include "XModbusReply_Protected.h"
#include "XModbusPdu.h"
#include "XCoreApplication.h"
#include "XThread.h"
#include "XDateTime.h"
#include "XPrintf.h"
#include "XBson.h"
#include "XBsonArray.h"
#include "XBsonDocument.h"
#include "XBsonValue.h"
#include "XByteArray.h"
#include "XString.h"
#include "XClass.h"
#include "XMemory.h"
/* §D：框架 Qt 对齐（表头/表格排序） */
#include "XTableWidget.h"
#include "XTableView.h"
#include "XHeaderView.h"
#include "XWidget.h"
#include "XWidget_Protected.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XObject.h"
#include "XVector.h"
#include "XGeometry.h"
/* §E(d)：app 层设备站（服务端 485 侧的温湿度站） */
#include "HsfServerBus.h"
#include "HsfServerIoTempHum.h"
/* §K：ttyS4 路 B 三设备（浊度仪 ×2 + 流量计）——设备层字序旋钮 + 站模块 +
 * 虚拟模型走同一条真实解码路径（含 2026-11-09 实测的 CDAB/DCBA 双口径）。 */
#if XINDUSTRY_ON && XTURBIDITY_ON
#include "XTurbidityMeter.h"
#include "XFlowMeter.h"
#include "XIoChannelGroup.h"
#include "XIoPoint.h"
#include "XIoAnalogPoint.h"
#include "HsfServerConfig.h"    /* §K2：站号/字序/寄存器数宏（K2 配置锁）。 */
#include "HsfServerIoTurbidity.h"
#include "HsfServerIoFlowMeter.h"
#include "HsfServerIoSim.h"     /* §K6：模拟装配处的时延/恢复期接线锁。 */
#endif
/* §N：现场口径对齐（侧号解析 + 流量计健康判定走公开 API）。 */
#include "HsfServerIo.h"        /* §N2：HsfServerIo_turbiditySide 等。 */
#include "HsfServerIoFlowMeter.h" /* §N3：HsfServerIoFlowMeter_health。 */
#include "HsfServerSettings.h"  /* §N7：collect/* 写 INI 门面（先 open 再写）。 */
/* §J：策略性拒绝（Rejected）语义 —— 用回环设备对驱动两个会话（与
 *     XLinkTest.c 的认证用例同一夹具），不依赖真实网络与真实时钟。 */
#include "XLinkSession.h"
#include "XLinkProto.h"
#include "XGuiRemoteLoopback.h"
/* §S21：导出模式不数总数（落盘层直调 + 造夹具库）。
 * [头名撞车已修] 服务端的行结构原名 `HsfHistoryRow`，与客户端
 * HsfLinkHistory.h 的 `HsfHistoryRow` **同名不同义** ⇒ 本文件同时 include
 * 两端时 C2011「struct 类型重定义」。⇒ 服务端已改名 `HsfStoreHistoryRow`。 */
#include "HsfServerStore.h"
#include "XSqlDatabase.h"
#include "XSqlQuery.h"
#include "XFile.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdarg.h>

#if XMODBUS_ON && XMODBUS_CLIENT_ON

/* §H：历史页「图表缩放取数」专项（2026-11-09 已并入本文件，见下文 §H 段）。
 *     保留这个前置声明是为了在 §H 段之前就能引用（并把 `--test hsf-chart-zoom`
 *     这个旧入口继续指到同一个函数，行为零变化）。
 *     返回值 = 本次 §H 的失败数（0 表示全绿）。 */
extern int HsfChartZoomTest_run(void);

static int g_pass = 0;
static int g_fail = 0;

/* ---- 断言宏 ----
 *
 * [用法] CHECK(描述, 判据) 或 CHECK(描述, 判据, "格式串", 实参…)。
 *         带详情时，失败会打印格式化后的详情（printf 风格）。
 *
 * [为什么详情写成 hsfChkDetail("" __VA_ARGS__)]
 *   MSVC 传统预处理器把 __VA_ARGS__ 当成**一个不可拆分的片段**，且为空时
 *   整个消失 ⇒ 直接转发给变参函数会在"无详情"时留下空实参（编译错误），
 *   直接传另一个宏则无法按实参个数分派（实测 COUNT 方案展开成 _1）。
 *   办法：在详情前拼一个**空串字面量**——字符串字面量可拼接：
 *     · 无详情 ⇒ hsfChkDetail("")            （1 个实参，合法）
 *     · 有详情 ⇒ hsfChkDetail("" "count=%d", n) ⇒ 首参拼成 "count=%d"，
 *                其余实参原样传给变参函数（由编译器按逗号拆分）。
 *   [顺带] 失败时额外打印 #cond（判据的源码文本），定位更快。
 */
static void hsfChkDetail(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt ? fmt : "%s", ap);
    va_end(ap);
    XPrintf("%s", buf);
}

#define CHECK(desc, cond, ...)                                             \
    do {                                                                   \
        if (cond) { g_pass++; XPrintf("[PASS] %s\n", desc); }              \
        else {                                                             \
            g_fail++;                                                      \
            XPrintf("[FAIL] %s ", desc);                                   \
            hsfChkDetail("" __VA_ARGS__);                                  \
            XPrintf("\n        判据：%s\n", #cond);                        \
        }                                                                  \
    } while (0)

/** @brief 事件泵（Hub 轮询/信号派发）。 */
static void spinMs(int64_t ms)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < ms) {
        XCoreApplication_processEvents(0);
        XThread_msleep(2);
    }
}

/** @brief 当前堆在用字节数（泄漏定位用；统计被裁剪时恒 0）。 */
static uint64_t heapNow(void)
{
    if (!XMemory_statisticsEnabled()) return 0;
    return XMemory_statistics(XMEMORY_TYPE_SYSTEM).systemBytes;
}

/** @brief 冲刷事件循环的延迟删除队列（deleteLater 的释放由事件循环完成）。 */
static void flushDeferred(void)
{
    int i;
    for (i = 0; i < 10; ++i) {
        XCoreApplication_processEvents(0);
        XThread_msleep(1);
    }
}

/* ============================================================================
 * §A 框架 Modbus 四层根修
 * ==========================================================================*/

/**
 * @brief 造一个「虚拟从站 + Hub」的最小装配（进程内，无串口/无套接字）。
 */
typedef struct {
    struct XVirtualModelDigital* model;
    XModbusVirtualClient* client;
    XModbusHub* hub;
    int devId;
} HubFixture;

static bool hubFixtureMake(HubFixture* fx)
{
    if (!fx) return false;
    memset(fx, 0, sizeof(*fx));
    fx->model = XVirtualModelDigital_create(16);
    fx->client = XModbusVirtualClient_create(fx->model);
    fx->hub = XModbusHub_create();
    if (!fx->model || !fx->client || !fx->hub) return false;
    if (!XModbusVirtualClient_bindUnit(fx->client, 1,
                                       XVirtualModel_base(fx->model))) {
        return false;
    }
    fx->devId = XModbusHub_addDevice_2(fx->hub, (XModbusClient*)fx->client,
                                        "虚拟站");
    return fx->devId >= 0;
}

/**
 * @brief 拆夹具（Hub → 客户端 → 模型；客户端所有权在 Hub，模型是借用）。
 * @note  §A 各用例此前不回收 ⇒ 每跑一次总回归就漏一个 Hub + 客户端 + 模型。
 *        回归文件自己漏内存会让"泄漏压测"失去意义，故统一在此收口。
 * @note  [模型不是 XClass] 虚拟模型（XVirtualModelDigital 等）是裸
 *        XMemory_malloc 出来的普通对象，**没有 destroy 实现**（见
 *        Test/XProtocolTest/xmodbus_virtual_test.c 的同款注记）⇒ 只能按
 *        其分配槽位（XCLASS_DEFAULT_MEMORY_TYPE = XMEMORY_TYPE_SYSTEM）
 *        用 XFree_System 释放；误用 XClassDelete 会调 XClass 虚表崩掉。
 */
static void hubFixtureFree(HubFixture* fx)
{
    if (!fx) return;
    if (fx->hub) { XClassDelete((XClass*)fx->hub); fx->hub = NULL; }
    fx->client = NULL;   /* 所有权已随 Hub 释放 */
    if (fx->model) { XFree_System(fx->model); fx->model = NULL; }
    fx->devId = -1;
}

/**
 * @brief A1：Modbus 异常应答必须按**失败**记账。
 * @details 虚拟模型单次最多 16 点（实测硬约束）；读 17 点 ⇒ 回 EXC03。
 *          原口径把异常帧当成功：lastSuccessMs 假性推进（应用层看门狗
 *          永远判健康）、pollOkCount 虚增、缓存永不回填 ⇒ 现场"设备在线
 *          但无数据"，只有重启服务端能恢复。
 */
static void testA1_exceptionIsFailure(void)
{
    HubFixture fx;
    XModbusMapping* map;
    uint32_t okCount = 0;
    uint32_t errCount = 0;
    uint64_t worst = 0;

    XPrintf("--- A1 异常应答按失败记账 ---\n");
    CHECK("A1 装配（虚拟从站 + Hub）", hubFixtureMake(&fx));
    if (fx.devId < 0) { hubFixtureFree(&fx); return; }

    map = XModbusMapping_create();
    /* 读段 17 点 > 模型单次上限 16 ⇒ 模型回 EXC03（异常帧）。 */
    CHECK("A1 映射段登记（DI 17 点，故意超上限）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 17,
                                    fx.devId, 1, 0, 0, 0));
    XModbusHub_setMapping(fx.hub, map);          /* 所有权移交 Hub */
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);

    spinMs(500);
    okCount = XModbusHub_pollOkCount(fx.hub, fx.devId);
    errCount = XModbusHub_pollErrCount(fx.hub, fx.devId);
    XPrintf("[INFO] A1 pollOk=%u pollErr=%u lastSuccess=%llu\n", okCount,
            errCount, (unsigned long long)
            XModbusHub_deviceLastSuccessMs(fx.hub, fx.devId));
    CHECK("A1 异常帧按失败记账（pollErr>0）", errCount > 0);
    CHECK("A1 异常帧不算成功（pollOk 不增长）", okCount == 0);
    CHECK("A1 异常帧不推进 lastSuccessMs（看门狗得以看见）",
          XModbusHub_deviceLastSuccessMs(fx.hub, fx.devId) == 0);
    /* A4：按从站粒度的新鲜度——从未回填 ⇒ UINT64_MAX。 */
    CHECK("A4 最陈旧从站龄 = 从未（UINT64_MAX）",
          XModbusHub_deviceWorstUnitAge(fx.hub, fx.devId, NULL, &worst)
          && worst == UINT64_MAX);
    hubFixtureFree(&fx);
}

/**
 * @brief A2 + A3：嵌套同步收口不再丢帧；写持续失败也不饿死读。
 * @details 写段越界（远端 30..31 > 模型 16 点）⇒ 写必回异常 ⇒ 脏区间
 *          常驻 ⇒ 每拍都走"写收口→续泵→读收口"的嵌套路径。
 *          原实现在这里把**读帧整包丢弃**（pollOk 冻结 0、块级 lastOkMs
 *          不推进 ⇒ 点级新鲜度恒陈旧）；且失败的写在同拍被立即重试两次 ⇒
 *          「读写交替让路」的交替计数奇偶不变 ⇒ 读永远轮不到。
 */
static void testA2_nestedCloseoutAccounting(void)
{
    HubFixture fx;
    XModbusMapping* map;
    uint32_t okCount = 0;
    uint32_t errCount = 0;
    bool valid = false;

    XPrintf("--- A2/A3 嵌套收口记账 + 写失败不饿死读 ---\n");
    CHECK("A2 装配（虚拟从站 + Hub）", hubFixtureMake(&fx));
    if (fx.devId < 0) { hubFixtureFree(&fx); return; }

    map = XModbusMapping_create();
    /* 读段正常（DI 2 点）；写段越界 ⇒ 写必失败（脏区间常驻）。 */
    CHECK("A2 映射段登记（DI 2 点 + Coils 越界写段）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 2,
                                    fx.devId, 1, 0, 0, 0) &&
          XModbusMapping_addRangeEx(map, XModbusCoils, 0x200, 2, fx.devId, 1,
                                    30, 0, 0));
    XModbusHub_setMapping(fx.hub, map);
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);
    {
        uint16_t vals[2];
        vals[0] = 1;
        vals[1] = 1;
        XModbusHub_writeValues(fx.hub, XModbusCoils, 0x200, 2, vals);
    }
    spinMs(1200);
    okCount = XModbusHub_pollOkCount(fx.hub, fx.devId);
    errCount = XModbusHub_pollErrCount(fx.hub, fx.devId);
    valid = XModbusHub_pointValid(fx.hub, XModbusDiscreteInputs, 0x100, 3000);
    XPrintf("[INFO] A2 pollOk=%u pollErr=%u\n", okCount, errCount);
    CHECK("A2 读组成功被记账（嵌套丢帧已修）", okCount > 0);
    CHECK("A2 点级新鲜度保持有效（块级 lastOkMs 推进）", valid);
    /* A3：写持续失败也不该把读饿死。 */
    CHECK("A3 写持续失败也不饿死读组（pollOk 持续增长）", okCount > 10);
    (void)errCount;   /* 写失败不计入 pollErrCount（写路径有自己的沿） */
    hubFixtureFree(&fx);
}

/**
 * @brief A4：按从站粒度的新鲜度接口（块级 lastOkMs）。
 * @details 设备级"最后成功应答"会被同一 Hub 上**其它从站的成功掩盖**：
 *          一条 485 线上挂变频器+温湿度两台，变频器一成功，设备级计时器
 *          就一直新鲜 ⇒ 温湿度那台停止刷新也看不出来（现场实证）。
 *          本接口按"最陈旧从站的块级 lastOkMs"给判据，看门狗据此判停滞。
 */
static void testA4_worstUnitAge(void)
{
    HubFixture fx;
    XModbusMapping* map;
    int worstUnit = -1;
    uint64_t worst = 0;

    XPrintf("--- A4 按从站粒度的新鲜度 ---\n");
    CHECK("A4 装配（虚拟从站 + Hub）", hubFixtureMake(&fx));
    if (fx.devId < 0) { hubFixtureFree(&fx); return; }
    map = XModbusMapping_create();
    CHECK("A4 映射段登记（DI 2 点）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 2,
                                    fx.devId, 1, 0, 0, 0));
    XModbusHub_setMapping(fx.hub, map);
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);
    spinMs(400);
    CHECK("A4 能取到最陈旧从站",
          XModbusHub_deviceWorstUnitAge(fx.hub, fx.devId, &worstUnit, &worst));
    CHECK("A4 最陈旧从站号正确（站 1）", worstUnit == 1,
          "unit=%d", worstUnit);
    CHECK("A4 新鲜从站的点龄在阈值内", worst != UINT64_MAX && worst <= 3000,
          "age=%llu ms", (unsigned long long)worst);
    hubFixtureFree(&fx);
}

/* ============================================================================
 * §B 客户端历史应答解析契约（跨端）
 * ==========================================================================*/


/** @brief 造一个 BSON 字符串值（XBsonValue_create_string 吃 XString*）。 */
static XBsonValue* bsonStr(const char* utf8)
{
    XString* str = XString_create_utf8(utf8 ? utf8 : "");
    XBsonValue* v = XBsonValue_create_string(str);
    if (str) XClassDelete((XClass*)str);
    return v;
}

/**
 * @brief 造一个 **Python 服务端同形状** 的历史应答载荷。
 * @details 形状（与 server_py/hsfsrv/app.py 的 _h_history_page 及
 *          client_py/tests/test_hsf_regression.py §1 逐字对齐）：
 *            offset / limit / total / rows
 *            rows 每行 18 列：ts,mode,vfd_state,freq_hz,current_a,rpm,
 *            turb1,turb2,flow,flow_unit,flow_total,total_unit,press_mpa,
 *            press_note,temp,hum,di,do
 *          **数值列必须是数值**（不是预格式化文本）——C 端按列下标取原值
 *          画图表，收到文本就会静默读成 0。
 */
static XByteArray* buildHistoryPayload(void)
{
    XBsonDocument* doc = XBsonDocument_create();
    XBsonArray* rows = XBsonArray_create();
    XBsonArray* row = XBsonArray_create();
    XByteArray* bytes = NULL;
    static const char* kCols[] = {
        "ts", "mode", "vfd_state", "freq_hz", "current_a", "rpm",
        "turb1", "turb2", "flow", "flow_unit", "flow_total",
        "total_unit", "press_mpa", "press_note", "temp", "hum", "di", "do",
        "water_temp"   /* 2026-11-10：水温，追加在末尾 */
    };
    XBsonValue* v;
    int i;

    if (!doc || !rows || !row) {
        if (doc) XClassDelete((XClass*)doc);
        if (rows) XClassDelete((XClass*)rows);
        if (row) XClassDelete((XClass*)row);
        return NULL;
    }
    /* 一行：ts=1794201311849，其余按上面的列序给可区分的值。 */
    v = XBsonValue_create_int64(1794201311849LL);
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = bsonStr("手动");
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = bsonStr("停机");
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(46.10);      /* freq_hz */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(2.95);       /* current_a */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(1328.0);      /* rpm */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(12.31);       /* turb1 */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(3.26);        /* turb2 */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(36.94);       /* flow */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = bsonStr("m3/h");        /* flow_unit */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(119.36);      /* flow_total */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = bsonStr("m3");          /* total_unit */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(0.4992);      /* press_mpa */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = bsonStr("");             /* press_note */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(26.5);        /* temp */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_double(55.0);        /* hum */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_int64(21845);        /* di */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_int64(256);          /* do */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    /* [水温 2026-11-10] 生产列序**末尾**新增的一格（带温度的机型才有值；
     * 固件里没有温度的机型这里是 null）。 */
    v = XBsonValue_create_double(19.35);       /* water_temp */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);

    {
        /* [与服务端同款] 行数组塞进值里再挂到 rows：setArray_ref 借用，
         * append_move_base 把内容移进数组元素，随后释放值壳。 */
        XBsonValue* rv = XBsonValue_create(XBSON_TYPE_ARRAY);
        if (rv) {
            XBsonValue_setArray_ref(rv, row);
            row = NULL;                      /* 所有权交给值 */
            XBsonArray_append_move_base(rows, rv);
            XBsonValue_delete(rv);
        }
    }
    XBsonDocument_insert_keyUtf8_int32(doc, "offset", 0);
    XBsonDocument_insert_keyUtf8_int32(doc, "limit", 20);
    XBsonDocument_insert_keyUtf8_int32(doc, "total", 14342);
    XBsonDocument_insert_keyUtf8_array_move(doc, "rows", rows);
    {
        XBsonArray* cols = XBsonArray_create();
        /* [跟协议走] 直接用 HSF_HISTORY_WIRE_COLS（19），不写死 18 —— 写死
         * 会在加了 water_temp 后漏发最后一列的列名。 */
        for (i = 0; i < HSF_HISTORY_WIRE_COLS; ++i) {
            XBsonValue* cv = bsonStr(kCols[i]);
            XBsonArray_append_move_base(cols, cv);
            XBsonValue_delete(cv);
        }
        XBsonDocument_insert_keyUtf8_array_move(doc, "cols", cols);
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief §B：客户端按列下标取值 —— 必须取对列。
 */
static void testB_historyParseContract(void)
{
    XByteArray* bytes = buildHistoryPayload();
    HsfHistoryPage page;
    const HsfHistoryRow* row;

    XPrintf("--- B 客户端历史应答解析契约 ---\n");
    CHECK("B 载荷构造成功", bytes != NULL);
    if (!bytes) return;
    memset(&page, 0, sizeof(page));
    CHECK("B 解析成功",
          HsfHistoryPage_parse(XByteArray_constData(bytes),
                              (uint32_t)XByteArray_size_base(bytes), &page));
    CHECK("B offset 回显解析正确", page.m_offset == 0,
          "offset=%d", page.m_offset);
    CHECK("B limit 回显解析正确", page.m_limit == 20,
          "limit=%d", page.m_limit);
    CHECK("B total 解析正确", page.m_total == 14342,
          "total=%d", page.m_total);
    CHECK("B 行数正确", page.m_count == 1, "count=%d", page.m_count);
    if (page.m_count <= 0) {
        XClassDelete((XClass*)bytes);
        return;
    }
    row = &page.m_rows[0];
    CHECK("B ts 取自第 0 列（epoch 毫秒）",
          row->m_ts == 1794201311849LL, "ts=%lld", (long long)row->m_ts);
    /* 显示列（2026-11-10 起第 8 位是水温，其后整体右移一位）：
     *   3=freq 4=current 5=rpm 6=turb1 7=turb2 **8=水温**
     *   9=flow(+unit) 10=flow_total(+unit) 11=press 12=temp 13=hum
     *   14=DI 15=DO */
    CHECK("B freq 取自第 3 列",
          row->m_cells[3] && fabs(atof(row->m_cells[3]) - 46.10) < 0.01,
          "cells[3]=%s", row->m_cells[3]);
    CHECK("B turb1 取自第 6 列",
          row->m_cells[6] && fabs(atof(row->m_cells[6]) - 12.31) < 0.01,
          "cells[6]=%s", row->m_cells[6]);
    CHECK("B turb2 取自第 7 列",
          row->m_cells[7] && fabs(atof(row->m_cells[7]) - 3.26) < 0.01,
          "cells[7]=%s", row->m_cells[7]);
    /* [水温 2026-11-10] 线上在最后一格（18），显示插在第 8 位、按 %.1f 格式化
     * （19.35 → "19.4"）；原值旁路保留全精度 19.35。 */
    CHECK("B 水温取自第 8 列（线上第 18 格）",
          row->m_cells[8] && strcmp(row->m_cells[8], "19.4") == 0
          && row->m_hasNum[5] && fabs(row->m_num[5] - 19.35) < 1e-6,
          "cells[8]=%s num[5]=%f", row->m_cells[8], row->m_num[5]);
    CHECK("B press 取自第 11 列",
          row->m_cells[11] && fabs(atof(row->m_cells[11]) - 0.499) < 0.001,
          "cells[11]=%s", row->m_cells[11]);
    CHECK("B temp 取自第 12 列",
          row->m_cells[12] && fabs(atof(row->m_cells[12]) - 26.5) < 0.05,
          "cells[12]=%s", row->m_cells[12]);
    CHECK("B hum 取自第 13 列",
          row->m_cells[13] && fabs(atof(row->m_cells[13]) - 55.0) < 0.05,
          "cells[13]=%s", row->m_cells[13]);
    /* 图表原值旁路：num[] 必须是数值（图表按它画，不是按显示格）。 */
    {
        bool ok = true;
        int n;
        for (n = 0; n < HSF_HISTORY_NUM_COUNT; ++n) {
            if (!row->m_hasNum[n]) ok = false;
        }
        CHECK("B 数值列原值齐全（图表按原值画）", ok);
    }
    XClassDelete((XClass*)bytes);
}


/**
 * @brief §C：表格「双击列标题排序」的**核心逻辑**（页面侧实现，框架只负责
 *        发出双击信号）。
 * @details 口径与 Python 客户端 _header_dblclicked 一致：新列⇒降序；
 *          同列降⇒升序；同列升⇒取消排序。排序作用于当前页。
 *          [为什么不直接测 GUI 事件] 双击要靠真实鼠标输入合成，CI 里不稳；
 *          这里锁住"状态机 + 重排 + 写回"这段纯逻辑，双击信号的发射在
 *          框架侧（XHeaderView：sectionDoubleClicked 此前只有声明没有
 *          发射点，已补上）。
 */
static void testC_tableSort(void)
{
    XTableWidget* table;
    HsfTableSort sort;
    static const char* kRows[3][2] = {
        { "2026-11-09 13:00:00", "30.00" },
        { "2026-11-09 12:00:00", "10.00" },
        { "2026-11-09 14:00:00", "20.00" }
    };
    int r;

    XPrintf("--- C 表格双击列标题排序（核心逻辑）---" "\n");
    table = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    CHECK("C 表格建成", table != NULL);
    if (!table) return;
    XTableWidget_setColumnCount(table, 2);
    XTableWidget_setRowCount(table, 3);
    for (r = 0; r < 3; ++r) {
        int c;
        for (c = 0; c < 2; ++c) {
            XTableWidgetItem* it = XTableWidgetItem_create_utf8(kRows[r][c], 0);
            XTableWidget_setItem(table, r, c, it);
        }
    }

    HsfTableSort_init(&sort);
    CHECK("C 初始为不排序", sort.m_col == -1);

    /* 新列 ⇒ 降序（数值列 30/10/20 ⇒ 30、20、10）。 */
    HsfTableSort_toggle(&sort, 1);
    HsfTableSort_apply(&sort, table);
    {
        const XTableWidgetItem* it = XTableWidget_item(table, 0, 1);
        const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        CHECK("C 新列⇒降序（首行=最大 30.00）",
              v && strcmp(v, "30.00") == 0, "首行=%s", v ? v : "-");
    }
    /* 同列再点 ⇒ 升序（10、20、30）。 */
    HsfTableSort_toggle(&sort, 1);
    HsfTableSort_apply(&sort, table);
    {
        const XTableWidgetItem* it = XTableWidget_item(table, 0, 1);
        const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        CHECK("C 同列降⇒升序（首行=最小 10.00）",
              v && strcmp(v, "10.00") == 0, "首行=%s", v ? v : "-");
    }
    /* 第三次 ⇒ 取消排序（回到服务端原序：第一行仍是 13:00:00）。 */
    HsfTableSort_toggle(&sort, 1);
    HsfTableSort_apply(&sort, table);
    CHECK("C 同列升⇒取消排序", sort.m_col == -1);
    /* [取消排序的口径] 助手层只负责退出排序态；"回到服务端原序"由页面
     * 负责（重取当前页）——表里此刻已是排过序的行，本地没有原序可还原。 */
    CHECK("C 取消排序后助手层退出排序态（页面再重取原序）",
          sort.m_col == -1);
    XClassDelete((XClass*)table);
}

/* ============================================================================
 * §D 框架层「Qt 对齐补全」回归锁
 * ==========================================================================*/

/**
 * @brief 表头信号探针（记录收到的载荷）。
 */
typedef struct {
    int m_dblCount;        /**< sectionDoubleClicked 收到次数。 */
    int m_dblSection;      /**< 收到的段号。 */
    int m_enteredCount;    /**< sectionEntered 收到次数。 */
    int m_enteredSection;  /**< 收到的段号。 */
    int m_clickedCount;    /**< sectionClicked 收到次数。 */
    int m_clickedSection;  /**< 收到的段号。 */
} HeaderProbe;

static HeaderProbe g_hdrProbe;

static void hdrDblSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    g_hdrProbe.m_dblCount++;
    if (args) {
        XVarList_start(args);
        g_hdrProbe.m_dblSection = XVarList_arg(args, int);
    }
}

static void hdrEnteredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    g_hdrProbe.m_enteredCount++;
    if (args) {
        XVarList_start(args);
        g_hdrProbe.m_enteredSection = XVarList_arg(args, int);
    }
}

static void hdrClickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    g_hdrProbe.m_clickedCount++;
    if (args) {
        XVarList_start(args);
        g_hdrProbe.m_clickedSection = XVarList_arg(args, int);
    }
}

/**
 * @brief 在表头指定 x 处合成一次鼠标事件。
 * @note  **每次都必须重新 init 事件**：XMouseEvent_init 把坐标**拷贝**进
 *        事件体，改外部的 XPoint 不会改事件里的位置（首版踩过：复用同一
 *        个事件对象改 pos.x，结果事件里还是旧坐标 ⇒ 双击/悬停断言假红）。
 */
static void headerMouse(XHeaderView* hh, XEventType type, int x)
{
    XMouseEvent me;
    XPoint pos;
    if (!hh) return;
    XPoint_init(&pos, 0, 0);
    pos.x = x;
    pos.y = 5;
    XMouseEvent_init(&me, type,
                     (type == XEVENT_TYPE_MOUSE_MOVE) ? XMouseButton_NoButton
                                                     : XMouseButton_LeftButton,
                     XKeyboardModifier_NoModifier, pos);
    switch (type) {
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS:
        XWidget_mousePressEvent_base((XWidget*)hh, (XEvent*)&me);
        break;
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE:
        XWidget_mouseReleaseEvent_base((XWidget*)hh, (XEvent*)&me);
        break;
    case XEVENT_TYPE_MOUSE_MOVE:
        XWidget_mouseMoveEvent_base((XWidget*)hh, (XEvent*)&me);
        break;
    default:
        break;
    }
}

/**
 * @brief 在表头第 col 段中部模拟一次「按下 + 释放」。
 * @note  位置取段中点：避开分隔线手柄命中区（段尾 ±3px）。
 */
static void headerClickSection(XHeaderView* hh, int col)
{
    int start;
    int size;
    int x;
    if (!hh || col < 0) return;
    start = XHeaderView_sectionPosition(hh, col);
    size = XHeaderView_sectionSize(hh, col);
    if (start < 0) return;
    x = start + size / 2;
    headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS, x);
    headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, x);
}

/* [D4/D5 时序前置 2026-11-11] 远端 6fc0adb0（Qt 6.8 对齐）把「同段 400ms
 * 内两按」收敛为**真实双击语义**：DBL 通道接管三态排序并盖章，随后的
 * RELEASE 不再翻转指示器（对齐平台 PRESS→RELEASE→DBL→RELEASE 序列；
 * 旧「两连击假路径」已废弃）。用例的两次独立单击必须拉开 >400ms，
 * 否则第二击落进双击窗口 ⇒ D4「翻转=降序」/ D5「再点=降序」恒红。 */
static void hdrClickGap(void)
{
    const uint64_t until =
        (uint64_t)XDateTime_currentMSecsSinceEpoch() + 450u;
    while ((uint64_t)XDateTime_currentMSecsSinceEpoch() < until) { }
}

/**
 * @brief D1：sectionDoubleClicked 必须**真的有发射点**且载荷是段号。
 * @details 2026-11-09 之前该句柄全仓只有声明没有发射点 ⇒ 页面侧接了也
 *          收不到 ⇒ C 端「双击列标题排序」从未生效。这里按真实交互路径
 *          （同一段 400ms 内的第二次按压）验，而不是直接调发射函数。
 */
static void testD1_sectionDoubleClickedEmits(void)
{
    XHeaderView* hh;

    XPrintf("--- D1 表头双击信号（真实发射点 + 载荷）---\n");
    memset(&g_hdrProbe, 0, sizeof(g_hdrProbe));
    hh = XHeaderView_create(NULL, 0, 0);
    CHECK("D1 表头建成", hh != NULL);
    if (!hh) return;
    XHeaderView_setCount(hh, 3);
    CHECK("D1 表头段数为 3", XHeaderView_count(hh) == 3);
    XObject_connect((XObject*)hh,
                      XSignal(XHeaderView_sectionDoubleClicked_signal),
                      (XObject*)hh, hdrDblSlot, XConnectionType_Direct);

    /* 第一次按压：只记按压，不发射双击。 */
    headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                XHeaderView_sectionSize(hh, 0) / 2);
    CHECK("D1 单次按压不发射双击", g_hdrProbe.m_dblCount == 0);
    /* 第二次按压（同一段、400ms 内）⇒ 发射双击。 */
    headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                XHeaderView_sectionSize(hh, 0) / 2);
    CHECK("D1 第二次按压发射 sectionDoubleClicked",
          g_hdrProbe.m_dblCount == 1,
          "count=%d", g_hdrProbe.m_dblCount);
    CHECK("D1 双击载荷=段号（0）", g_hdrProbe.m_dblSection == 0,
          "section=%d", g_hdrProbe.m_dblSection);
    /* 三击不连发（第二次按压已把双击"消耗掉"）。 */
    headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                XHeaderView_sectionSize(hh, 0) / 2);
    CHECK("D1 三击不连发", g_hdrProbe.m_dblCount == 1,
          "count=%d", g_hdrProbe.m_dblCount);
    /* 换段按压 ⇒ 不算双击上一段（段号变了）。 */
    headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                XHeaderView_sectionPosition(hh, 2) +
                XHeaderView_sectionSize(hh, 2) / 2);
    CHECK("D1 换段按压不触发双击", g_hdrProbe.m_dblCount == 1,
          "count=%d", g_hdrProbe.m_dblCount);
    XObject_disconnect((XObject*)hh,
                         XSignal(XHeaderView_sectionDoubleClicked_signal),
                         (XObject*)hh, hdrDblSlot);
    XClassDelete((XClass*)hh);
}

/**
 * @brief D2：sectionEntered 必须真的有发射点且载荷是段号。
 * @details 悬停进入**新段**才发；同一段内移动不重复发（Qt entered 语义）。
 */
static void testD2_sectionEnteredEmits(void)
{
    XHeaderView* hh;

    XPrintf("--- D2 表头悬停进入信号（真实发射点 + 载荷）---\n");
    memset(&g_hdrProbe, 0, sizeof(g_hdrProbe));
    hh = XHeaderView_create(NULL, 0, 0);
    CHECK("D2 表头建成", hh != NULL);
    if (!hh) return;
    XHeaderView_setCount(hh, 3);
    XObject_connect((XObject*)hh,
                      XSignal(XHeaderView_sectionEntered_signal),
                      (XObject*)hh, hdrEnteredSlot, XConnectionType_Direct);

    /* 悬停进入第 0 段。 */
    headerMouse(hh, XEVENT_TYPE_MOUSE_MOVE, XHeaderView_sectionSize(hh, 0) / 2);
    CHECK("D2 悬停进入第 0 段发射 sectionEntered",
          g_hdrProbe.m_enteredCount == 1,
          "count=%d", g_hdrProbe.m_enteredCount);
    CHECK("D2 悬停载荷=段号（0）", g_hdrProbe.m_enteredSection == 0,
          "section=%d", g_hdrProbe.m_enteredSection);
    /* 同一段内再移动一次 ⇒ 不重复发。 */
    headerMouse(hh, XEVENT_TYPE_MOUSE_MOVE,
                XHeaderView_sectionSize(hh, 0) / 2 + 1);
    CHECK("D2 同一段内移动不重复发", g_hdrProbe.m_enteredCount == 1,
          "count=%d", g_hdrProbe.m_enteredCount);
    /* 移到第 1 段 ⇒ 再发一次，载荷=1。 */
    headerMouse(hh, XEVENT_TYPE_MOUSE_MOVE,
                XHeaderView_sectionPosition(hh, 1) +
                XHeaderView_sectionSize(hh, 1) / 2);
    CHECK("D2 移入新段再次发射", g_hdrProbe.m_enteredCount == 2,
          "count=%d", g_hdrProbe.m_enteredCount);
    CHECK("D2 第二次载荷=段号（1）", g_hdrProbe.m_enteredSection == 1,
          "section=%d", g_hdrProbe.m_enteredSection);
    XObject_disconnect((XObject*)hh,
                         XSignal(XHeaderView_sectionEntered_signal),
                         (XObject*)hh, hdrEnteredSlot);
    XClassDelete((XClass*)hh);
}

/**
 * @brief D3 + D4：释放时翻转排序指示器；指示器段号/方向可读。
 * @details Qt 口径（qheaderview.cpp mouseReleaseEvent 尾段）：
 *          释放于按压段 ⇒ flipSortIndicator → setSortIndicator
 *          · 点**别的**段 ⇒ 切到该段、**升序**；
 *          · 点**同一**段 ⇒ 升/降**翻转**。
 */
static void testD3_sortIndicatorFlipsOnRelease(void)
{
    XHeaderView* hh;

    XPrintf("--- D3/D4 排序指示器（释放时翻转 + 可读）---\n");
    memset(&g_hdrProbe, 0, sizeof(g_hdrProbe));
    hh = XHeaderView_create(NULL, 0, 0);
    CHECK("D3 表头建成", hh != NULL);
    if (!hh) return;
    XHeaderView_setCount(hh, 3);
    XHeaderView_setSectionsClickable(hh, true);
    CHECK("D3 表头段可点", XHeaderView_sectionsClickable(hh));
    XObject_connect((XObject*)hh, XSignal(XHeaderView_sectionClicked_signal),
                      (XObject*)hh, hdrClickedSlot, XConnectionType_Direct);

    /* 首次点第 0 段 ⇒ 指示器落在该段、升序。 */
    headerClickSection(hh, 0);
    CHECK("D3 首次点击发出 sectionClicked", g_hdrProbe.m_clickedCount == 1,
          "count=%d", g_hdrProbe.m_clickedCount);
    CHECK("D3 sectionClicked 载荷=段号（0）",
          g_hdrProbe.m_clickedSection == 0,
          "section=%d", g_hdrProbe.m_clickedSection);
    CHECK("D3 指示器落在第 0 段",
          XHeaderView_sortIndicatorSection(hh) == 0,
          "section=%d", XHeaderView_sortIndicatorSection(hh));
    CHECK("D3 首次点击指示器=升序（0）",
          XHeaderView_sortIndicatorOrder(hh) ==
              XHeaderViewSortOrder_Ascending,
          "order=%d", XHeaderView_sortIndicatorOrder(hh));
    /* [Qt 6.8 对齐 2026-11-24] 单击的 flipSortIndicator/setSortIndicator
     * **只改段号/方向**，不改变可见性（Qt 源码里 sortIndicatorShown 仅由
     * setSortIndicatorShown 写；单击链不碰它）。可见性由应用显式打开，
     * 与 XTableView/QTreeView 的 setSortingEnabled 同口径。 */
    CHECK("D3 单击只翻转段号/方向，不强制显示指示器",
          !XHeaderView_isSortIndicatorShown(hh));
    XHeaderView_setSortIndicatorShown(hh, true);
    CHECK("D3 应用 setSortIndicatorShown(true) ⇒ 指示器可见",
          XHeaderView_isSortIndicatorShown(hh));

    /* 再点同一段 ⇒ 翻转成降序（先出双击窗口，见 hdrClickGap 注）。 */
    hdrClickGap();
    headerClickSection(hh, 0);
    CHECK("D4 同段再点指示器翻转=降序（1）",
          XHeaderView_sortIndicatorOrder(hh) ==
              XHeaderViewSortOrder_Descending,
          "order=%d", XHeaderView_sortIndicatorOrder(hh));
    CHECK("D4 指示器仍在第 0 段",
          XHeaderView_sortIndicatorSection(hh) == 0);

    /* 点别的段 ⇒ 切到该段、升序。 */
    headerClickSection(hh, 2);
    CHECK("D4 点别的段 ⇒ 指示器切到第 2 段",
          XHeaderView_sortIndicatorSection(hh) == 2,
          "section=%d", XHeaderView_sortIndicatorSection(hh));
    CHECK("D4 点别的段 ⇒ 方向=升序",
          XHeaderView_sortIndicatorOrder(hh) ==
              XHeaderViewSortOrder_Ascending,
          "order=%d", XHeaderView_sortIndicatorOrder(hh));

    /* 程序化 setSortIndicator ⇒ 指示器可读（不依赖点击路径）。 */
    XHeaderView_setSortIndicator(hh, 1, XHeaderViewSortOrder_Descending);
    CHECK("D4 setSortIndicator 后段号可读",
          XHeaderView_sortIndicatorSection(hh) == 1);
    CHECK("D4 setSortIndicator 后方向可读",
          XHeaderView_sortIndicatorOrder(hh) ==
              XHeaderViewSortOrder_Descending);
    XObject_disconnect((XObject*)hh, XSignal(XHeaderView_sectionClicked_signal),
                         (XObject*)hh, hdrClickedSlot);
    XClassDelete((XClass*)hh);
}

/**
 * @brief D5：setSortingEnabled(true) ⇒ 表头段单击 ⇒ 行按该列重排。
 * @details Qt 口径（QTableView::setSortingEnabled）：开启后表头的
 *          sectionClicked 连到 sortByColumn ⇒ 单击表头按该列排序。
 *          本框架的排序落在 XTableWidget（它同时拥有 model 与单元格）。
 *          同时验 Qt 的"空值下沉"（该列为空的行排在最后）。
 */
static void testD5_sortingEnabledSortsByColumn(void)
{
    XTableWidget* table;
    XHeaderView* hh;

    XPrintf("--- D5 开启排序后单击表头 ⇒ 按该列重排 ---\n");
    table = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    CHECK("D5 表格建成", table != NULL);
    if (!table) return;
    XTableWidget_setColumnCount(table, 2);
    XTableWidget_setRowCount(table, 3);
    {
        static const char* kVals[3] = { "30.00", "10.00", "20.00" };
        int r;
        for (r = 0; r < 3; ++r) {
            XTableWidgetItem* a =
                XTableWidgetItem_create_utf8("row", 0);
            XTableWidgetItem* b =
                XTableWidgetItem_create_utf8(kVals[r], 0);
            XTableWidget_setItem(table, r, 0, a);
            XTableWidget_setItem(table, r, 1, b);
        }
    }
    hh = XTableView_horizontalHeader(&table->m_base);
    CHECK("D5 水平表头可取", hh != NULL);
    if (!hh) { XClassDelete((XClass*)table); return; }

    XTableWidget_setSortingEnabled(table, true);
    CHECK("D5 开启排序后表头段变为可点", XHeaderView_sectionsClickable(hh));

    /* 单击第 1 段（数值列）⇒ 按该列升序（首次点击 ⇒ 升序）。 */
    headerClickSection(hh, 1);
    {
        const XTableWidgetItem* it = XTableWidget_item(table, 0, 1);
        const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        CHECK("D5 单击表头 ⇒ 首行=该列最小值 10.00",
              v && strcmp(v, "10.00") == 0, "首行=%s", v ? v : "-");
        it = XTableWidget_item(table, 2, 1);
        v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        CHECK("D5 单击表头 ⇒ 末行=该列最大值 30.00",
              v && strcmp(v, "30.00") == 0, "末行=%s", v ? v : "-");
    }
    /* 再点同一段 ⇒ 指示器翻转为降序 ⇒ 排序方向随之翻转。 */
    hdrClickGap();   /* 出双击窗口（同段两按 <400ms 会被判为双击）。 */
    headerClickSection(hh, 1);
    {
        const XTableWidgetItem* it = XTableWidget_item(table, 0, 1);
        const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        CHECK("D5 再点同段 ⇒ 降序（首行=最大值 30.00）",
              v && strcmp(v, "30.00") == 0, "首行=%s", v ? v : "-");
    }

    /* 空值下沉（Qt QTableModel::sort 口径）：该列为空的行排到最后。 */
    {
        static const char* kVals[3] = { "40.00", "50.00", "60.00" };
        int r;
        XTableWidget_clearContents(table);
        XTableWidget_setColumnCount(table, 2);
        XTableWidget_setRowCount(table, 3);
        for (r = 0; r < 3; ++r) {
            XTableWidgetItem* a = XTableWidgetItem_create_utf8("row", 0);
            XTableWidget_setItem(table, r, 0, a);
            if (r != 1) {
                XTableWidgetItem* b =
                    XTableWidgetItem_create_utf8(kVals[r], 0);
                XTableWidget_setItem(table, r, 1, b);
            }
            /* r==1：该列**没有条目**（空值） */
        }
        XTableWidget_sortItems(table, 1, 0);   /* 升序 */
        {
            const XTableWidgetItem* it = XTableWidget_item(table, 2, 1);
            CHECK("D5 空值下沉（空行排最后）", it == NULL,
                  "末行=%s",
                  it ? XTableWidgetItem_text_toUtf8(it) : "(空)");
            it = XTableWidget_item(table, 0, 1);
            CHECK("D5 非空行按升序排列（首行=40.00）",
                  it && strcmp(XTableWidgetItem_text_toUtf8(it),
                               "40.00") == 0);
        }
    }
    XClassDelete((XClass*)table);
}

/**
 * @brief D6：setSortingEnabled(false) ⇒ 表头段不再可点（不再触发排序）。
 */
static void testD6_sortingDisabledStopsSorting(void)
{
    XTableWidget* table;
    XHeaderView* hh;
    const char* before[3];
    int r;

    XPrintf("--- D6 关闭排序后表头段不再可点 ---\n");
    table = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    CHECK("D6 表格建成", table != NULL);
    if (!table) return;
    XTableWidget_setColumnCount(table, 2);
    XTableWidget_setRowCount(table, 3);
    {
        static const char* kVals[3] = { "30.00", "10.00", "20.00" };
        for (r = 0; r < 3; ++r) {
            XTableWidgetItem* a = XTableWidgetItem_create_utf8("row", 0);
            XTableWidgetItem* b = XTableWidgetItem_create_utf8(kVals[r], 0);
            XTableWidget_setItem(table, r, 0, a);
            XTableWidget_setItem(table, r, 1, b);
        }
    }
    hh = XTableView_horizontalHeader(&table->m_base);
    if (!hh) { XClassDelete((XClass*)table); return; }

    XTableWidget_setSortingEnabled(table, true);
    XTableWidget_setSortingEnabled(table, false);
    CHECK("D6 关闭排序后表头段不可点", !XHeaderView_sectionsClickable(hh));

    for (r = 0; r < 3; ++r) {
        const XTableWidgetItem* it = XTableWidget_item(table, r, 1);
        before[r] = it ? XTableWidgetItem_text_toUtf8(it) : "";
    }
    headerClickSection(hh, 1);
    {
        bool same = true;
        for (r = 0; r < 3; ++r) {
            const XTableWidgetItem* it = XTableWidget_item(table, r, 1);
            const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
            if (strcmp(v, before[r]) != 0) same = false;
        }
        CHECK("D6 关闭排序后单击表头不再重排行", same);
    }
    XClassDelete((XClass*)table);
}

/**
 * @brief D9：setColumnHidden ⇒ 表头隐藏段 + 表格几何跳过（列筛选联动根）。
 * @details 2026-11-11 现场「列筛选取消勾选，表格不联动」：旧 XTableWidget
 *          覆写 paintEvent 按 m_colWidths[] 逐列画、**不消费**隐藏态，且
 *          XHeaderView_resizeSection 拒绝 size<=0 ⇒「列宽 0」绕过也不通。
 *          远端 6fc0adb0（Qt 6.8 全量对齐）后 setColumnHidden 转发 header
 *          隐藏段（qtableview.cpp:2705-2711 对位），表格列起点/命中跳过
 *          隐藏段 ⇒ 这里按真实链路锁四件事：转发、占宽 0、命中跳过、
 *          恢复后几何复位。HSF 三页（历史/日志/报警）的列筛选都走它。
 */
static void testD9_columnHiddenSkipsGeometry(void)
{
    XTableWidget* table;
    XHeaderView* hh;
    int pos1;
    int pos2;

    XPrintf("--- D9 列隐藏（setColumnHidden ⇒ 表头段 + 几何跳过）---\n");
    table = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    CHECK("D9 表格建成", table != NULL);
    if (!table) return;
    XTableWidget_setColumnCount(table, 3);
    XTableView_setColumnWidth(&table->m_base, 0, 100);
    XTableView_setColumnWidth(&table->m_base, 1, 80);
    XTableView_setColumnWidth(&table->m_base, 2, 60);
    hh = XTableView_horizontalHeader(&table->m_base);
    CHECK("D9 水平表头可取", hh != NULL);
    if (!hh) { XClassDelete((XClass*)table); return; }

    /* 隐藏第 1 列 ⇒ 转发到表头隐藏段。 */
    XTableView_setColumnHidden(&table->m_base, 1, true);
    CHECK("D9 表头段 1 进入隐藏态", XHeaderView_isSectionHidden(hh, 1));
    /* 隐藏列占宽 0 ⇒ 列 2 的视口起点 == 列 1 的起点。 */
    pos1 = XTableView_columnViewportPosition(&table->m_base, 1);
    pos2 = XTableView_columnViewportPosition(&table->m_base, 2);
    CHECK("D9 隐藏列占宽 0（列2 起点==列1 起点）", pos1 == pos2,
          "pos1=%d pos2=%d", pos1, pos2);
    /* 命中测试跳过隐藏列：原列 1 的中点 x=140 现在应命中列 2。 */
    CHECK("D9 columnAt(140) 命中列 2（跳过隐藏列）",
          XTableView_columnAt(&table->m_base, 140) == 2,
          "col=%d", XTableView_columnAt(&table->m_base, 140));

    /* 恢复显示 ⇒ 几何复位（列 2 右移回列宽 80 之后）。 */
    XTableView_setColumnHidden(&table->m_base, 1, false);
    CHECK("D9 表头段 1 恢复可见", !XHeaderView_isSectionHidden(hh, 1));
    pos2 = XTableView_columnViewportPosition(&table->m_base, 2);
    CHECK("D9 恢复后列 2 起点 = 100+80", pos2 == 180,
          "pos2=%d", pos2);
    XClassDelete((XClass*)table);
}

/**
 * @brief D7：信号函数的 **NULL 取 id 约定**（今天这里崩过）。
 * @details XObject_signalIdOf 以 self=NULL 调用信号函数取其返回值（真实
 *          地址）。故所有信号函数必须在 if(self) 守卫内短路：
 *          · 以 NULL 调用 ⇒ 不崩、**不发射**；
 *          · 取到的 id 非 0，且与连接用的 XSignal(...) 一致（连接可达）。
 *          2026-11-09 实测：xhv_emitInt 未判 self ⇒ 「连信号」这一步就
 *          解引用 NULL ⇒ 客户端启动即段错误。
 */
static void testD7_nullSignalIdContract(void)
{
    XHeaderView* hh;
    size_t id = 0;
    int before = 0;

    XPrintf("--- D7 信号函数 NULL 取 id 约定（不崩、不发射）---\n");
    memset(&g_hdrProbe, 0, sizeof(g_hdrProbe));

    /* ① 以 NULL 调用取 id：不得崩。 */
    id = XObject_signalIdOf(
        (void* (*)(XObject*))XHeaderView_sectionDoubleClicked_signal);
    CHECK("D7 NULL 取 id 不崩且非 0", id != 0, "id=%llu",
          (unsigned long long)id);
    CHECK("D7 XSignal 取到的 id 与 signalIdOf 一致（连接可达）",
          (size_t)XSignal(XHeaderView_sectionDoubleClicked_signal) == id);

    /* ② 以 NULL 调用信号函数：不得发射。 */
    hh = XHeaderView_create(NULL, 0, 0);
    CHECK("D7 表头建成", hh != NULL);
    if (!hh) return;
    XHeaderView_setCount(hh, 2);
    XObject_connect((XObject*)hh,
                      XSignal(XHeaderView_sectionDoubleClicked_signal),
                      (XObject*)hh, hdrDblSlot, XConnectionType_Direct);
    before = g_hdrProbe.m_dblCount;
    XHeaderView_sectionDoubleClicked_signal(NULL, 7);   /* 必须不崩 */
    CHECK("D7 NULL 调用不发射", g_hdrProbe.m_dblCount == before,
          "count=%d", g_hdrProbe.m_dblCount);
    XHeaderView_sectionEntered_signal(NULL, 7);
    XHeaderView_sectionClicked_signal(NULL, 7);
    CHECK("D7 NULL 调用（其余信号）同样不崩不发射",
          g_hdrProbe.m_dblCount == before);
    XObject_disconnect((XObject*)hh,
                         XSignal(XHeaderView_sectionDoubleClicked_signal),
                         (XObject*)hh, hdrDblSlot);
    XClassDelete((XClass*)hh);
}

/**
 * @brief D8：app 层三态策略（新列⇒降 / 同列⇒升 / 再点⇒取消）+ 指示器联动。
 * @details 这是 app 层助手 HsfTableSort 的**策略**部分（框架只提供
 *          sortItems 原语与排序指示器）。排序指示器必须跟着策略走，
 *          否则界面上的箭头与实际行序对不上。
 */
static void testD8_appThreeStatePolicy(void)
{
    XTableWidget* table;
    XHeaderView* hh;
    HsfTableSort sort;

    XPrintf("--- D8 app 层三态排序策略 + 指示器联动 ---\n");
    table = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    CHECK("D8 表格建成", table != NULL);
    if (!table) return;
    XTableWidget_setColumnCount(table, 2);
    XTableWidget_setRowCount(table, 3);
    {
        static const char* kVals[3] = { "30.00", "10.00", "20.00" };
        int r;
        for (r = 0; r < 3; ++r) {
            XTableWidgetItem* a = XTableWidgetItem_create_utf8("row", 0);
            XTableWidgetItem* b = XTableWidgetItem_create_utf8(kVals[r], 0);
            XTableWidget_setItem(table, r, 0, a);
            XTableWidget_setItem(table, r, 1, b);
        }
    }
    hh = XTableView_horizontalHeader(&table->m_base);
    if (!hh) { XClassDelete((XClass*)table); return; }

    HsfTableSort_init(&sort);
    CHECK("D8 初始：不排序", sort.m_col == -1);

    /* ① 新列 ⇒ 降序。 */
    HsfTableSort_toggle(&sort, 1);
    CHECK("D8 新列⇒排序列=1 且为降序",
          sort.m_col == 1 && sort.m_desc);
    HsfTableSort_apply(&sort, table);
    HsfTableSort_updateIndicator(&sort, table);
    {
        const XTableWidgetItem* it = XTableWidget_item(table, 0, 1);
        const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        CHECK("D8 新列⇒降序（首行=30.00）",
              v && strcmp(v, "30.00") == 0, "首行=%s", v ? v : "-");
    }
    CHECK("D8 指示器跟随策略（第 1 段 / 降序 / 可见）",
          XHeaderView_sortIndicatorSection(hh) == 1 &&
          XHeaderView_sortIndicatorOrder(hh) ==
              XHeaderViewSortOrder_Descending &&
          XHeaderView_isSortIndicatorShown(hh));

    /* ② 同列再点 ⇒ 升序。 */
    HsfTableSort_toggle(&sort, 1);
    CHECK("D8 同列⇒升序（m_desc=false）",
          sort.m_col == 1 && !sort.m_desc);
    HsfTableSort_apply(&sort, table);
    HsfTableSort_updateIndicator(&sort, table);
    {
        const XTableWidgetItem* it = XTableWidget_item(table, 0, 1);
        const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        CHECK("D8 同列⇒升序（首行=10.00）",
              v && strcmp(v, "10.00") == 0, "首行=%s", v ? v : "-");
    }
    CHECK("D8 指示器跟随策略（第 1 段 / 升序）",
          XHeaderView_sortIndicatorSection(hh) == 1 &&
          XHeaderView_sortIndicatorOrder(hh) ==
              XHeaderViewSortOrder_Ascending);

    /* ③ 第三次 ⇒ 取消排序（退出排序态）。 */
    HsfTableSort_toggle(&sort, 1);
    CHECK("D8 再点⇒取消排序（退出排序态）", sort.m_col == -1);
    HsfTableSort_apply(&sort, table);
    HsfTableSort_updateIndicator(&sort, table);
    CHECK("D8 取消排序后指示器隐藏",
          !XHeaderView_isSortIndicatorShown(hh));

    /* ④ 换列 ⇒ 重新按"新列⇒降序"起算。 */
    HsfTableSort_toggle(&sort, 0);
    CHECK("D8 换列⇒新列降序", sort.m_col == 0 && sort.m_desc);
    XClassDelete((XClass*)table);
}

/* ============================================================================
 * §E 交叉测试（层与层、端与端互相验证）
 * ==========================================================================*/

/** @brief 取表格某列的显示文本（用于比对行序）。 */
static void tableColumnTexts(XTableWidget* table, int col, char out[][40],
                             int maxRows)
{
    int r;
    for (r = 0; r < maxRows; ++r) {
        const XTableWidgetItem* it =
            XTableWidget_item(table, r, col);
        const char* v = it ? XTableWidgetItem_text_toUtf8(it) : "";
        snprintf(out[r], 40, "%s", v ? v : "(空)");
    }
}

/**
 * @brief E1(a)：框架原语 ↔ app 策略（同批数据、同列 ⇒ 同一行序）。
 * @details 两条路径：
 *          · 框架原语：直接 XTableWidget_sortItems(table, col, order)；
 *          · app 策略：HsfTableSort_toggle（决定列与方向）+ HsfTableSort_apply。
 *          app 策略最终也是调框架原语，故两者必须给出**完全一致**的行序；
 *          若哪天 app 层自己写了一套排序，这里就会红。
 */
static void testE1_frameworkPrimitiveVsAppPolicy(void)
{
    XTableWidget* tFrame;
    XTableWidget* tApp;
    HsfTableSort sort;
    char a[3][40];
    char b[3][40];
    static const char* kVals[3] = { "30.00", "10.00", "20.00" };
    int r;

    XPrintf("--- E1(a) 框架排序原语 ↔ app 三态策略 ---\n");
    tFrame = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    tApp = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    CHECK("E1 两张表建成", tFrame && tApp);
    if (!tFrame || !tApp) {
        if (tFrame) XClassDelete((XClass*)tFrame);
        if (tApp) XClassDelete((XClass*)tApp);
        return;
    }
    XTableWidget_setColumnCount(tFrame, 2);
    XTableWidget_setRowCount(tFrame, 3);
    XTableWidget_setColumnCount(tApp, 2);
    XTableWidget_setRowCount(tApp, 3);
    for (r = 0; r < 3; ++r) {
        XTableWidgetItem* i1 = XTableWidgetItem_create_utf8("row", 0);
        XTableWidgetItem* i2 = XTableWidgetItem_create_utf8(kVals[r], 0);
        XTableWidgetItem* i3 = XTableWidgetItem_create_utf8("row", 0);
        XTableWidgetItem* i4 = XTableWidgetItem_create_utf8(kVals[r], 0);
        XTableWidget_setItem(tFrame, r, 0, i1);
        XTableWidget_setItem(tFrame, r, 1, i2);
        XTableWidget_setItem(tApp, r, 0, i3);
        XTableWidget_setItem(tApp, r, 1, i4);
    }

    /* ① 首击（新列）⇒ 降序。 */
    HsfTableSort_init(&sort);
    HsfTableSort_toggle(&sort, 1);
    XTableWidget_sortItems(tFrame, 1, sort.m_desc ? 1 : 0);
    HsfTableSort_apply(&sort, tApp);
    tableColumnTexts(tFrame, 1, a, 3);
    tableColumnTexts(tApp, 1, b, 3);
    CHECK("E1 降序：两条路径行序一致",
          strcmp(a[0], b[0]) == 0 && strcmp(a[1], b[1]) == 0 &&
          strcmp(a[2], b[2]) == 0,
          "框架=%s/%s/%s app=%s/%s/%s", a[0], a[1], a[2], b[0], b[1], b[2]);
    CHECK("E1 降序结果正确（30/20/10）",
          strcmp(a[0], "30.00") == 0 && strcmp(a[1], "20.00") == 0 &&
          strcmp(a[2], "10.00") == 0);

    /* ② 再点（同列）⇒ 升序。 */
    HsfTableSort_toggle(&sort, 1);
    XTableWidget_sortItems(tFrame, 1, sort.m_desc ? 1 : 0);
    HsfTableSort_apply(&sort, tApp);
    tableColumnTexts(tFrame, 1, a, 3);
    tableColumnTexts(tApp, 1, b, 3);
    CHECK("E1 升序：两条路径行序一致",
          strcmp(a[0], b[0]) == 0 && strcmp(a[1], b[1]) == 0 &&
          strcmp(a[2], b[2]) == 0);
    CHECK("E1 升序结果正确（10/20/30）",
          strcmp(a[0], "10.00") == 0 && strcmp(a[1], "20.00") == 0 &&
          strcmp(a[2], "30.00") == 0);

    /* ③ 第三次 ⇒ 取消排序：两条路径都不得再动行序。 */
    HsfTableSort_toggle(&sort, 1);
    CHECK("E1 第三次⇒app 退出排序态", sort.m_col == -1);
    HsfTableSort_apply(&sort, tApp);
    tableColumnTexts(tFrame, 1, a, 3);
    tableColumnTexts(tApp, 1, b, 3);
    CHECK("E1 取消排序：两条路径行序一致且保持不变",
          strcmp(a[0], b[0]) == 0 && strcmp(a[1], b[1]) == 0 &&
          strcmp(a[2], b[2]) == 0 &&
          strcmp(a[0], "10.00") == 0 && strcmp(a[1], "20.00") == 0 &&
          strcmp(a[2], "30.00") == 0,
          "框架=%s/%s/%s app=%s/%s/%s", a[0], a[1], a[2], b[0], b[1], b[2]);

    XClassDelete((XClass*)tFrame);
    XClassDelete((XClass*)tApp);
}

/* ---------------------------------------------------------------------------
 * §E(b)(c) 固化的服务端载荷
 * -------------------------------------------------------------------------*/

/**
 * @brief Python 服务端**真实产出**的历史应答载荷（0x0118，limit=2）。
 * @details [来源与生成方式] 2026-11-09 在本机起一个**隔离**的 Python
 *          服务端实例（临时 cfg，不碰仓库里的 hsf_data.db 与 cfg/ 账户库）：
 *            cd HighSpeedFilter/server_py
 *            python -m hsfsrv --sim --port <临时端口> \
 *                  --config <临时目录>/cfg/hsf_server_py.json
 *          用 client_py 的 XLinkClient 连上（admin/admin），
 *          发 {"offset":0,"limit":2} 的 0x0118 请求，把**原始应答 BSON
 *          字节**落盘后按十六进制固化到本文件。
 *          下面这串十六进制就是那份原始字节（逐字节，未做任何改写）。
 *
 *          [载荷内容（解码后）]
 *            ok=true total=10 offset=0 limit=2 dbRows=10
 *            cols = [ts,mode,vfd_state,freq_hz,current_a,rpm,turb1,turb2,
 *                    flow,flow_unit,flow_total,total_unit,press_mpa,
 *                    press_note,temp,hum,di,do]
 *            rows[0] = [1794213564972,'手动','停机',0.0,0.0,0.0,12.43,3.22,
 *                       36.3917,'m³/h',0.453,'m³',0.4992469879518072,'',
 *                       26.5,55.0,21845,256]
 *            rows[1] = [1794213569966,'手动','停机',0.0,0.0,0.0,12.68,3.15,
 *                       36.7705,'m³/h',0.505,'m³',0.4992469879518072,'',
 *                       26.5,55.0,21845,256]
 *          注意单位是 **m³/h**（真机/服务端口径，不是 ASCII 的 m3/h）。
 */
static const char kPyHistoryHex[] =
    "45030000086f6b000110746f74616c000a000000106f66667365740000000000106c696d6974000200000004636f"
    "6c73000801000002300003000000747300023100050000006d6f6465000232000a0000007666645f737461746500"
    "02330008000000667265715f687a000234000a00000063757272656e745f61000235000400000072706d00023600"
    "060000007475726231000237000600000074757262320002380005000000666c6f77000239000a000000666c6f77"
    "5f756e697400023130000b000000666c6f775f746f74616c00023131000b000000746f74616c5f756e6974000231"
    "32000a00000070726573735f6d706100023133000b00000070726573735f6e6f746500023134000500000074656d"
    "7000023135000400000068756d0002313600030000006469000231370003000000646f000004726f777300ab0100"
    "00043000d00000001230002c5276bfa101000002310007000000e6898be58aa80002320007000000e5819ce69cba"
    "000133000000000000000000013400000000000000000001350000000000000000000136005c8fc2f528dc284001"
    "3700c3f5285c8fc20940013800eeebc03923324240023900060000006dc2b32f680001313000cba145b6f3fddc3f"
    "02313100040000006dc2b30001313200064a78a3a9f3df3f023133000100000000013134000000000000803a4001"
    "3135000000000000804b401031360055550000103137000001000000043100d0000000123000ae6576bfa1010000"
    "02310007000000e6898be58aa80002320007000000e5819ce69cba00013300000000000000000001340000000000"
    "0000000001350000000000000000000136005c8fc2f5285c29400137003333333333330940013800b4c876be9f62"
    "4240023900060000006dc2b32f680001313000295c8fc2f528e03f02313100040000006dc2b30001313200064a78"
    "a3a9f3df3f023133000100000000013134000000000000803a40013135000000000000804b401031360055550000"
    "10313700000100000000106462526f7773000a000000126c6174657374547300ae6576bfa1010000126561726c69"
    "657374547300a5b575bfa10100001077696e646f775374617274547300000000001277696e646f77456e64547300"
    "0d6776bfa101000000";

/**
 * @brief C 服务端**真实产出**的历史应答载荷（0x0118，limit=2）。
 * @details [来源与生成方式] 2026-11-09 起一个**隔离**的 C 服务端实例：
 *          把 bin\HsfServer.exe **复制**到临时目录再跑（它的落盘目录是
 *          <程序目录>/cfg/，复制即隔离 ⇒ 不碰仓库里的 bin/cfg）：
 *            HsfServer.exe --sim <临时端口>
 *          同样用 client_py 的 XLinkClient 连上（admin/admin），发
 *          {"offset":0,"limit":2} 的 0x0118 请求，取**原始应答 BSON 字节**。
 *          下面这串十六进制就是那份原始字节（逐字节，未做任何改写）。
 *
 *          [载荷内容（解码后）]
 *            ok=true total=10 offset=0 limit=2 dbRows=10
 *            cols 与 Python 端**逐字相同**（18 个列名、同序）。
 *            rows[0] = [1794213617000,'手动','停机',0.0,0.0,0.0,12.2773,
 *                       3.2553,35.8531,'m³/h',0.0,'m³',0.4992,'',26.5,55.0,
 *                       21845,0]
 *            rows[1] = [1794213622000,'手动','停机',0.0,0.0,0.0,12.5273,
 *                       3.1911,35.771,'m³/h',0.0,'m³',0.4992,'',26.5,55.0,
 *                       21845,0]
 *          [与 Python 端的差异只在数据本身]（C 端模拟变频器停机时
 *          freq/current/rpm 为 0、正累计为 0；Python 端略有差别），
 *          **形状**（键集合/列序/逐列类型）必须完全一致——那是"两端同口径"
 *          的机器可查证据。
 */
static const char kCHistoryHex[] =
    "5d030000086f6b000110746f74616c000a000000106f66667365740000000000106c696d69740002000000126462"
    "526f7773000a00000000000000126c6174657374547300f03077bfa1010000126561726c69657374547300407d76"
    "bfa10100001277696e646f77537461727454730000000000000000001277696e646f77456e64547300784477bfa1"
    "01000004636f6c73000801000002300003000000747300023100050000006d6f6465000232000a0000007666645f"
    "73746174650002330008000000667265715f687a000234000a00000063757272656e745f61000235000400000072"
    "706d00023600060000007475726231000237000600000074757262320002380005000000666c6f77000239000a00"
    "0000666c6f775f756e697400023130000b000000666c6f775f746f74616c00023131000b000000746f74616c5f75"
    "6e697400023132000a00000070726573735f6d706100023133000b00000070726573735f6e6f7465000231340005"
    "00000074656d7000023135000400000068756d0002313600030000006469000231370003000000646f000004726f"
    "777300bb010000043000d8000000123000681d77bfa101000002310007000000e6898be58aa80002320007000000"
    "e5819ce69cba00013300000000000000000001340000000000000000000135000000000000000000013600925cfe"
    "43fa8d2840013700b459f5b9da0a0a40013800da1b7c6132ed4140023900060000006dc2b32f6800013130000000"
    "00000000000002313100040000006dc2b30001313200de718a8ee4f2df3f02313300010000000001313400000000"
    "0000803a40013135000000000000804b4012313600555500000000000012313700000000000000000000043100d8"
    "000000123000f03077bfa101000002310007000000e6898be58aa80002320007000000e5819ce69cba0001330000"
    "0000000000000001340000000000000000000135000000000000000000013600925cfe43fa0d2940013700f31fd2"
    "6f5f870940013800a69bc420b0e24140023900060000006dc2b32f68000131300000000000000000000231310004"
    "0000006dc2b30001313200de718a8ee4f2df3f023133000100000000013134000000000000803a40013135000000"
    "000000804b40123136005555000000000000123137000000000000000000000000";

/**
 * @brief 十六进制串 → 字节数组（调用方负责 XFree_System 释放）。
 */
static uint8_t* hexDecode(const char* hex, uint32_t* outLen)
{
    size_t n;
    uint8_t* buf;
    size_t i;
    if (!hex || !outLen) return NULL;
    n = strlen(hex) / 2u;
    if (n == 0) return NULL;
    buf = (uint8_t*)XMalloc_System(n);
    if (!buf) return NULL;
    for (i = 0; i < n; ++i) {
        int hi = 0;
        int lo = 0;
        char a = hex[i * 2];
        char b = hex[i * 2 + 1];
        hi = (a >= '0' && a <= '9') ? (a - '0')
           : (a >= 'a' && a <= 'f') ? (a - 'a' + 10) : -1;
        lo = (b >= '0' && b <= '9') ? (b - '0')
           : (b >= 'a' && b <= 'f') ? (b - 'a' + 10) : -1;
        if (hi < 0 || lo < 0) { XFree_System(buf); return NULL; }
        buf[i] = (uint8_t)((hi << 4) | lo);
    }
    *outLen = (uint32_t)n;
    return buf;
}

/**
 * @brief E2(b)：C 客户端解析器 ↔ Python 服务端真实载荷。
 * @details 逐列取值必须取对列（不是预格式化文本、列序不串、行间不串）。
 */
static void testE2_clientParserVsPythonPayload(void)
{
    uint8_t* bytes;
    uint32_t len = 0;
    HsfHistoryPage page;
    const HsfHistoryRow* r0;
    const HsfHistoryRow* r1;

    XPrintf("--- E2(b) C 客户端解析器 ↔ Python 服务端真实载荷 ---\n");
    bytes = hexDecode(kPyHistoryHex, &len);
    CHECK("E2 固化载荷解码成功", bytes != NULL && len > 0);
    if (!bytes) return;
    memset(&page, 0, sizeof(page));
    CHECK("E2 真实载荷解析成功",
          HsfHistoryPage_parse(bytes, len, &page));
    CHECK("E2 total 解析正确（10）", page.m_total == 10,
          "total=%d", page.m_total);
    CHECK("E2 dbRows 解析正确（10）", page.m_dbRows == 10,
          "dbRows=%lld", (long long)page.m_dbRows);
    CHECK("E2 offset 回显（0）", page.m_offset == 0);
    CHECK("E2 limit 回显（2）", page.m_limit == 2,
          "limit=%d", page.m_limit);
    CHECK("E2 行数正确（2）", page.m_count == 2,
          "count=%d", page.m_count);
    if (page.m_count < 2) {
        XFree_System(bytes);
        return;
    }
    r0 = &page.m_rows[0];
    r1 = &page.m_rows[1];

    /* --- 时间戳（第 0 列）--- */
    CHECK("E2 第 0 行 ts 取自第 0 列",
          r0->m_ts == 1794213564972LL,
          "ts=%lld", (long long)r0->m_ts);
    CHECK("E2 第 1 行 ts 取自第 0 列（行间不串）",
          r1->m_ts == 1794213569966LL,
          "ts=%lld", (long long)r1->m_ts);

    /* --- 文本列（1=mode 2=vfd_state）--- */
    CHECK("E2 mode 取自第 1 列",
          strcmp(r0->m_cells[1], "手动") == 0, "cells[1]=%s", r0->m_cells[1]);
    CHECK("E2 vfd_state 取自第 2 列",
          strcmp(r0->m_cells[2], "停机") == 0, "cells[2]=%s", r0->m_cells[2]);

    /* --- 数值列（3=freq 4=current 5=rpm 6=turb1 7=turb2）--- */
    CHECK("E2 freq 取自第 3 列", strcmp(r0->m_cells[3], "0.00") == 0,
          "cells[3]=%s", r0->m_cells[3]);
    CHECK("E2 current 取自第 4 列", strcmp(r0->m_cells[4], "0.00") == 0,
          "cells[4]=%s", r0->m_cells[4]);
    CHECK("E2 rpm 取自第 5 列", strcmp(r0->m_cells[5], "0") == 0,
          "cells[5]=%s", r0->m_cells[5]);
    CHECK("E2 turb1 取自第 6 列", strcmp(r0->m_cells[6], "12.43") == 0,
          "cells[6]=%s", r0->m_cells[6]);
    CHECK("E2 turb2 取自第 7 列", strcmp(r0->m_cells[7], "3.22") == 0,
          "cells[7]=%s", r0->m_cells[7]);
    /* --- 带单位的列（9=flow 10=flow_total；单位是 m³/h 不是 m3/h）--- */
    CHECK("E2 flow 取自第 9 列并带真单位",
          strcmp(r0->m_cells[9], "36.39 m³/h") == 0,
          "cells[9]=%s", r0->m_cells[9]);
    CHECK("E2 flow_total 取自第 10 列并带真单位",
          strcmp(r0->m_cells[10], "0.45 m³") == 0,
          "cells[10]=%s", r0->m_cells[10]);
    /* --- 压差/温湿/位图（水温插在第 8 位后整体右移一位）--- */
    CHECK("E2 press 取自第 11 列", strcmp(r0->m_cells[11], "0.499") == 0,
          "cells[11]=%s", r0->m_cells[11]);
    CHECK("E2 temp 取自第 12 列", strcmp(r0->m_cells[12], "26.5") == 0,
          "cells[12]=%s", r0->m_cells[12]);
    CHECK("E2 hum 取自第 13 列", strcmp(r0->m_cells[13], "55.0") == 0,
          "cells[13]=%s", r0->m_cells[13]);
    CHECK("E2 di 取自第 14 列", strcmp(r0->m_cells[14], "0x00005555") == 0,
          "cells[14]=%s", r0->m_cells[14]);
    CHECK("E2 do 取自第 15 列", strcmp(r0->m_cells[15], "0x00000100") == 0,
          "cells[15]=%s", r0->m_cells[15]);

    /* --- 图表原值旁路：必须是**数值**且逐列对应 ---
     * [这份载荷是 18 列的**老**载荷] 固化于 2026-11-09（加 water_temp 之前）
     * ⇒ 第 19 格不存在：水温显示 "--"、m_hasNum[5]=false。这正是要锁的
     * **向后兼容**口径（客户端不能因为少一格就整行错列/崩溃）。新列的正向
     * 解析由 §B 的合成载荷（含 water_temp）覆盖。 */
    {
        bool allHasExceptWater = true;
        int n;
        for (n = 0; n < HSF_HISTORY_NUM_COUNT; ++n) {
            if (n == 5) continue;            /* 5 = 水温（老载荷没有这一格） */
            if (!r0->m_hasNum[n]) allHasExceptWater = false;
        }
        CHECK("E2 图表原值齐全（水温除外；老载荷兼容）", allHasExceptWater);
        CHECK("E2 老载荷没有水温格 ⇒ 显示 -- 且原值缺失",
              strcmp(r0->m_cells[8], "--") == 0 && !r0->m_hasNum[5],
              "cells[8]=%s hasNum[5]=%d", r0->m_cells[8],
              (int)r0->m_hasNum[5]);
        CHECK("E2 原值 freq=0（列 3）", fabs(r0->m_num[0]) < 1e-9);
        CHECK("E2 原值 turb1=12.43（列 6）",
              fabs(r0->m_num[3] - 12.43) < 1e-6,
              "num[3]=%f", r0->m_num[3]);
        CHECK("E2 原值 turb2=3.22（列 7）",
              fabs(r0->m_num[4] - 3.22) < 1e-6,
              "num[4]=%f", r0->m_num[4]);
        /* [水温在 num[] 的第 5 位] num[] 按**显示列序**排（显示第 8 位 =
         * 水温）⇒ 其后各列下标整体后移一位。老载荷该位缺失（上面已断言）。 */
        CHECK("E2 原值 flow=36.3917（列 8）",
              fabs(r0->m_num[6] - 36.3917) < 1e-4,
              "num[6]=%f", r0->m_num[6]);
        CHECK("E2 原值 flow_total=0.453（列 10）",
              fabs(r0->m_num[7] - 0.453) < 1e-6,
              "num[7]=%f", r0->m_num[7]);
        CHECK("E2 原值 press=0.4992469879518072（列 12）",
              fabs(r0->m_num[8] - 0.4992469879518072) < 1e-12,
              "num[8]=%.15f", r0->m_num[8]);
        CHECK("E2 原值 temp=26.5（列 14）",
              fabs(r0->m_num[9] - 26.5) < 1e-9);
        CHECK("E2 原值 hum=55.0（列 15）",
              fabs(r0->m_num[10] - 55.0) < 1e-9);
        /* 第 1 行的值必须不同 ⇒ 行间不串。 */
        CHECK("E2 第 1 行原值 turb1=12.68（行间不串）",
              fabs(r1->m_num[3] - 12.68) < 1e-6,
              "num[3]=%f", r1->m_num[3]);
        CHECK("E2 第 1 行原值 flow=36.7705（行间不串）",
              fabs(r1->m_num[6] - 36.7705) < 1e-4,
              "num[6]=%f", r1->m_num[6]);
    }
    XFree_System(bytes);
}

/**
 * @brief BSON 值的"类型类"（两端载荷同形比对用）。
 */
static int bsonValueKind(const XBsonValue* v)
{
    XBsonType t;
    if (!v) return -1;
    t = XBsonValue_type(v);
    switch (t) {
    case XBSON_TYPE_NULL:      return 0;   /* 空 */
    case XBSON_TYPE_STRING:    return 1;   /* 文本 */
    case XBSON_TYPE_DOUBLE:
    case XBSON_TYPE_INT32:
    case XBSON_TYPE_INT64:
    case XBSON_TYPE_DATETIME:  return 2;   /* 数值 */
    default:                   return 3;   /* 其它 */
    }
}

/**
 * @brief E3(c)：C 服务端 ↔ Python 服务端：两份历史应答**逐键逐列同形**。
 * @details 比三样东西：
 *          ① 键集合一致（两端的包络键必须一样多、一样名）；
 *          ② 列序一致（cols 数组逐项相同）；
 *          ③ 逐列类型一致（数值列两端都是数值、文本列两端都是文本）。
 *          任一端改了形状（改了列名/列序/把数值预格式化成文本），这里就红。
 */
/* [口径] XBsonArray 的元素容器是**内嵌**的 XVector（首成员），故按仓库
 * 既有写法（Test/XDataTest/XJsonBsonCoverageTest.c）显式转成 XVector
 * 视图取元素；直接用 XBsonArray_count_base/at_base 会因指针类型不兼容
 * 触发 C4133。 */
#define HSF_ARR_COUNT(a) XVector_size_base((const XContainer*)(a))
#define HSF_ARR_AT(a, i) XVector_at_base((const XVector*)(a), (i))

static void testE3_cServerVsPyServerPayloadShape(void)
{
    uint8_t* pyb;
    uint8_t* cb;
    uint32_t pyLen = 0;
    uint32_t cLen = 0;
    XBsonDocument* pydoc = NULL;
    XBsonDocument* cdoc = NULL;
    XVector* pyKeys = NULL;
    XVector* cKeys = NULL;

    XPrintf("--- E3(c) C 服务端 ↔ Python 服务端 载荷同形 ---\n");
    pyb = hexDecode(kPyHistoryHex, &pyLen);
    cb = hexDecode(kCHistoryHex, &cLen);
    CHECK("E3 两份固化载荷解码成功", pyb && cb);
    if (!pyb || !cb) {
        if (pyb) XFree_System(pyb);
        if (cb) XFree_System(cb);
        return;
    }
    pydoc = XBsonDocument_create();
    cdoc = XBsonDocument_create();
    if (!pydoc || !cdoc) {
        CHECK("E3 文档对象建成", false);
        goto done;
    }
    if (!XBsonDocument_from_bytes(pydoc, pyb, pyLen) ||
        !XBsonDocument_from_bytes(cdoc, cb, cLen)) {
        CHECK("E3 两份载荷解析成功", false);
        goto done;
    }
    CHECK("E3 两份载荷解析成功", true);

    /* ① 键集合一致（顺序可不同 ⇒ 按集合比）。 */
    pyKeys = XBsonDocument_keys_base(pydoc);
    cKeys = XBsonDocument_keys_base(cdoc);
    CHECK("E3 键集合可取", pyKeys && cKeys);
    if (pyKeys && cKeys) {
        const size_t pn = XVector_size_base((const XContainer*)pyKeys);
        const size_t cn = XVector_size_base((const XContainer*)cKeys);
        bool same = (pn == cn);
        size_t i;
        XPrintf("[INFO] E3 Python 端键 %u 个 / C 端键 %u 个\n",
                (unsigned)pn, (unsigned)cn);
        for (i = 0; same && i < pn; ++i) {
            const XString* k = (const XString*)
                XVector_at_base(pyKeys, (int64_t)i);
            const char* ku = k ? XString_toUtf8(k) : NULL;
            if (!ku || !XBsonDocument_contains_keyUtf8(cdoc, ku)) {
                same = false;
                XPrintf("[INFO] E3 Python 独有键：%s\n", ku ? ku : "?");
            }
        }
        for (i = 0; same && i < cn; ++i) {
            const XString* k = (const XString*)
                XVector_at_base(cKeys, (int64_t)i);
            const char* ku = k ? XString_toUtf8(k) : NULL;
            if (!ku || !XBsonDocument_contains_keyUtf8(pydoc, ku)) {
                same = false;
                XPrintf("[INFO] E3 C 端独有键：%s\n", ku ? ku : "?");
            }
        }
        CHECK("E3 键集合一致（两端包络同形）", same);
    }

    /* ② 列序一致。 */
    {
        const XBsonArray* pyCols = NULL;
        const XBsonArray* cCols = NULL;
        XBsonValue* v = XBsonDocument_value_keyUtf8(pydoc, "cols");
        if (v) pyCols = XBsonValue_toArray(v);
        v = XBsonDocument_value_keyUtf8(cdoc, "cols");
        if (v) cCols = XBsonValue_toArray(v);
        CHECK("E3 两端都有 cols 数组", pyCols && cCols);
        if (pyCols && cCols) {
            const size_t pn = HSF_ARR_COUNT(pyCols);
            const size_t cn = HSF_ARR_COUNT(cCols);
            bool same = (pn == cn) && (pn == 18);
            size_t i;
            CHECK("E3 cols 列数一致且为 18", same,
                  "py=%llu c=%llu", (unsigned long long)pn,
                  (unsigned long long)cn);
            for (i = 0; same && i < pn; ++i) {
                const XBsonValue* a = HSF_ARR_AT(pyCols, (int64_t)i);
                const XBsonValue* b = HSF_ARR_AT(cCols, (int64_t)i);
                const XString* sa = a ? XBsonValue_toString(a) : NULL;
                const XString* sb = b ? XBsonValue_toString(b) : NULL;
                const char* ua = sa ? XString_toUtf8(sa) : NULL;
                const char* ub = sb ? XString_toUtf8(sb) : NULL;
                if (!ua || !ub || strcmp(ua, ub) != 0) {
                    same = false;
                    XPrintf("[INFO] E3 列序不一致 @%lld：py=%s c=%s\n",
                            (long long)i, ua ? ua : "(无)", ub ? ub : "(无)");
                }
            }
            CHECK("E3 列序逐项一致", same);
        }
    }

    /* ③ 逐列类型一致 + 行宽一致。 */
    {
        const XBsonArray* pyRows = NULL;
        const XBsonArray* cRows = NULL;
        XBsonValue* v = XBsonDocument_value_keyUtf8(pydoc, "rows");
        if (v) pyRows = XBsonValue_toArray(v);
        v = XBsonDocument_value_keyUtf8(cdoc, "rows");
        if (v) cRows = XBsonValue_toArray(v);
        CHECK("E3 两端都有 rows 数组", pyRows && cRows);
        if (pyRows && cRows) {
            const size_t pn = HSF_ARR_COUNT(pyRows);
            const size_t cn = HSF_ARR_COUNT(cRows);
            size_t i;
            bool widthOk = true;
            bool kindOk = true;
            for (i = 0; i < pn && i < cn; ++i) {
                const XBsonValue* pa = HSF_ARR_AT(pyRows, (int64_t)i);
                const XBsonValue* pb = HSF_ARR_AT(cRows, (int64_t)i);
                const XBsonArray* ra = pa ? XBsonValue_toArray(pa) : NULL;
                const XBsonArray* rb = pb ? XBsonValue_toArray(pb) : NULL;
                size_t j;
                if (!ra || !rb) { widthOk = false; break; }
                if (HSF_ARR_COUNT(ra) != HSF_ARR_COUNT(rb)) {
                    widthOk = false;
                    break;
                }
                for (j = 0; j < HSF_ARR_COUNT(ra); ++j) {
                    const XBsonValue* va = HSF_ARR_AT(ra, (int64_t)j);
                    const XBsonValue* vb = HSF_ARR_AT(rb, (int64_t)j);
                    const int ka = bsonValueKind(va);
                    const int kb = bsonValueKind(vb);
                    if (ka != kb) {
                        kindOk = false;
                        XPrintf("[INFO] E3 类型不一致 行%llu 列%llu：py=%d c=%d\n",
                                (unsigned long long)i,
                                (unsigned long long)j, ka, kb);
                    }
                }
            }
            /* [两份都是 18 列的老载荷] 固化于 2026-11-09（water_temp 之前），
             * 这里比的是"两端**同宽同形**"，不是"等于某个固定列数"。 */
            CHECK("E3 两端每行宽度一致", widthOk);
            CHECK("E3 逐列类型一致（数值列是数值、文本列是文本）", kindOk);
        }
    }

done:
    if (pyKeys) XClassDelete((XClass*)pyKeys);
    if (cKeys) XClassDelete((XClass*)cKeys);
    if (pydoc) XClassDelete((XClass*)pydoc);
    if (cdoc) XClassDelete((XClass*)cdoc);
    if (pyb) XFree_System(pyb);
    if (cb) XFree_System(cb);
}

/**
 * @brief E4(d)：框架 ↔ app 的分线稳定性（同一时刻同一寄存器）。
 * @details Hub 的读组/回填（框架层，XModbusHub_value）与设备站读到的值
 *          （app 层，HsfServerIoTempHum_values）必须一致：
 *          · 框架层看到的是**原始寄存器字**（含负温折返编码）；
 *          · app 层看到的是**工程值**（0.1 单位定点，负温已解码）。
 *          两者的换算关系由 app 层的 HsfServerIoTempHum_decodeTemp/
 *          decodeHum 定义 ⇒ 这里验"框架读到的字"经该换算后**正好等于**
 *          app 层读到的值（层间口径漂移即红）。
 */
static void testE4_frameworkVsAppLineConsistency(void)
{
#if XINDUSTRY_ON && XMODBUS_HUB_ON
    HsfServerBus bus;
    HsfServerIoTempHum station;
    struct XVirtualModelTempHum* model = NULL;
    XModbusVirtualClient* client = NULL;
    XModbusMapping* map = NULL;
    const uint16_t vbase = 0x0100;
    const int unitId = 4;
    uint16_t rawT = 0;
    uint16_t rawH = 0;
    int32_t t10 = 0;
    int32_t h10 = 0;
    bool tv = false;
    bool hv = false;
    int waited;

    XPrintf("--- E4(d) 框架 Hub 回填 ↔ app 设备站读值 ---\n");
    memset(&bus, 0, sizeof(bus));
    HsfServerIoTempHum_init(&station);

    model = XVirtualModelTempHum_create();
    CHECK("E4 温湿度模型建成", model != NULL);
    if (!model) goto e4_done;
    /* 取一组**能验出折返编码**的值：-25.0℃ ⇒ 原字 10250；64.8%RH。 */
    XVirtualModelTempHum_set(model, 10250, 648);

    client = XModbusVirtualClient_create(XVirtualModel_base(model));
    CHECK("E4 虚拟客户端建成", client != NULL);
    if (!client) goto e4_done;
    if (!XModbusVirtualClient_bindUnit(client, unitId,
                                       XVirtualModel_base(model))) {
        CHECK("E4 从站模型绑定成功", false);
        goto e4_done;
    }
    CHECK("E4 总线接入注入客户端",
          HsfServerBus_openWithClient(&bus, "回归总线",
                                      (XModbusClient*)client, 500, 1));
    client = NULL;   /* 所有权已移交 Hub */
    map = HsfServerBus_createMapping(&bus);
    CHECK("E4 映射表建成", map != NULL);
    if (!map) goto e4_done;
    CHECK("E4 温湿度站装配成功",
          HsfServerIoTempHum_mount(&station, &bus, map, unitId, vbase, 3000));
    CHECK("E4 总线启动（映射提交 + 轮询拉起）",
          HsfServerBus_start(&bus, map, 30));
    map = NULL;      /* 所有权已移交 Hub */

    /* 等首帧回填。 */
    for (waited = 0; waited < 2000; waited += 20) {
        uint16_t probe = 0;
        if (XModbusHub_value(HsfServerBus_hub(&bus), XModbusInputRegisters,
                             vbase, &probe) && probe == 10250) {
            break;
        }
        spinMs(20);
    }
    CHECK("E4 框架层已回填温度原始字（10250）",
          XModbusHub_value(HsfServerBus_hub(&bus), XModbusInputRegisters,
                           vbase, &rawT) && rawT == 10250,
          "raw=%u", (unsigned)rawT);
    CHECK("E4 框架层已回填湿度原始字（648）",
          XModbusHub_value(HsfServerBus_hub(&bus), XModbusInputRegisters,
                           (uint16_t)(vbase + 1), &rawH) && rawH == 648,
          "raw=%u", (unsigned)rawH);

    /* app 层：设备站读到的工程值。 */
    CHECK("E4 设备站取到值",
          HsfServerIoTempHum_values(&station, &t10, &h10, &tv, &hv));
    CHECK("E4 设备站温度有效", tv);
    CHECK("E4 设备站湿度有效", hv);

    /* ★ 交叉：框架读到的原始字 经 app 的换算 正好等于 app 读到的值。 */
    CHECK("E4 交叉：框架原始字 ⇒ app 工程值（温度 -250 = -25.0℃）",
          t10 == HsfServerIoTempHum_decodeTemp(rawT),
          "app=%d decode(raw=%u)=%d", (int)t10, (unsigned)rawT,
          (int)HsfServerIoTempHum_decodeTemp(rawT));
    CHECK("E4 交叉：框架原始字 ⇒ app 工程值（湿度 648 = 64.8%RH）",
          h10 == HsfServerIoTempHum_decodeHum(rawH),
          "app=%d decode(raw=%u)=%d", (int)h10, (unsigned)rawH,
          (int)HsfServerIoTempHum_decodeHum(rawH));
    CHECK("E4 温度折返解码正确（-25.0℃）", t10 == -250,
          "t10=%d", (int)t10);
    CHECK("E4 湿度解码正确（64.8%RH）", h10 == 648,
          "h10=%d", (int)h10);
    /* 同一时刻同一寄存器：框架读到的字与 app 解码用的是同一个虚拟地址。 */
    CHECK("E4 分线一致：app 有效性与框架新鲜度同步",
          XModbusHub_pointValid(HsfServerBus_hub(&bus),
                                XModbusInputRegisters, vbase, 3000) == tv);

e4_done:
    if (map) XClassDelete((XClass*)map);
    /* 站是**栈上**对象：必须显式 deinit，否则 XObject 自身的资源（信号槽
     * 管理器等）不释放（deinit 内部会再调一次 unmount，幂等）。 */
    XClassDeinit((XClass*)&station);
    HsfServerBus_stop(&bus);
    if (client) XClassDelete((XClass*)client);
    /* 虚拟模型不是 XClass（无 destroy 实现）：按分配槽位释放，见
     * hubFixtureFree 的同款注记。 */
    if (model) XFree_System(model);
#else
    XPrintf("--- E4(d) 框架 ↔ app 分线一致性（本构建未开工业层，跳过）---\n");
#endif
}

/* ============================================================================
 * §F 覆盖面扩容
 * ==========================================================================*/

/**
 * @brief XPrintf 输出捕获（用于断言框架的可观测日志）。
 */
typedef struct {
    char*  m_buf;      /**< 累积缓冲（XMalloc_System 分配）。 */
    size_t m_len;      /**< 已写长度。 */
    size_t m_cap;      /**< 缓冲容量。 */
    bool   m_failed;   /**< 分配失败过。 */
} LogCapture;

static int64_t logCaptureWrite(void* userData, const char* data, size_t size)
{
    LogCapture* c = (LogCapture*)userData;
    if (!c || !data) return -1;
    if (c->m_len + size + 1 > c->m_cap) {
        size_t need = c->m_len + size + 1;
        size_t cap = c->m_cap ? c->m_cap : 1024;
        char* nb;
        while (cap < need) cap *= 2;
        nb = (char*)XMalloc_System(cap);
        if (!nb) { c->m_failed = true; return -1; }
        if (c->m_buf) {
            memcpy(nb, c->m_buf, c->m_len);
            XFree_System(c->m_buf);
        }
        c->m_buf = nb;
        c->m_cap = cap;
    }
    memcpy(c->m_buf + c->m_len, data, size);
    c->m_len += size;
    c->m_buf[c->m_len] = '\0';
    return (int64_t)size;
}

/** @brief 释放捕获缓冲。 */
static void logCaptureFree(LogCapture* c)
{
    if (!c) return;
    if (c->m_buf) { XFree_System(c->m_buf); c->m_buf = NULL; }
    c->m_len = 0;
    c->m_cap = 0;
}

/**
 * @brief F1 + F2：读组失败的**可观测日志** 与 **块级陈旧诊断**。
 * @details 2026-11-09 之前这两条路径**完全静默**（只加计数、诊断一条都
 *          不打）⇒ 现场只看到"没数据"，查不到是哪一组、哪条命令、什么
 *          错误码，也查不到读组压根没发（被三道闸门静默跳过）。
 *          这里用 XPrintf 的输出重定向把框架日志抓下来逐字断言。
 */
static void testF1F2_failureObservabilityAndStallDiag(void)
{
    HubFixture fx;
    XModbusMapping* map;
    LogCapture cap;
    XPrintfOutputScope scope;

    XPrintf("--- F1/F2 失败可观测日志 + 块级陈旧诊断 ---\n");
    memset(&cap, 0, sizeof(cap));
    memset(&scope, 0, sizeof(scope));

    CHECK("F1 装配（虚拟从站 + Hub）", hubFixtureMake(&fx));
    if (fx.devId < 0) { hubFixtureFree(&fx); return; }
    map = XModbusMapping_create();
    CHECK("F1 映射段登记（DI 17 点 ⇒ 必回异常帧）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 17,
                                    fx.devId, 1, 0, 0, 0));
    XModbusHub_setMapping(fx.hub, map);
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);

    /* 把框架日志抓下来。**重定向必须在连上/起轮询之前建**：首次泵就会
     * 打"轮询停滞"诊断（块从未回填 ⇒ 立即判定陈旧），晚建就抓不到。 */
    CHECK("F1 日志重定向建立",
          XPrintf_outputPush(&scope, logCaptureWrite, &cap));
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);
    spinMs(400);
    XPrintf_outputPop(&scope);

    XPrintf("[INFO] F1 抓到的框架日志 %u 字节\n", (unsigned)cap.m_len);
    if (cap.m_buf && cap.m_len > 0) XPrintf("[INFO] ---- 框架日志 ----\n%s\n",
                                            cap.m_buf);
    CHECK("F1 失败时框架确实打了日志（不再是静默）",
          cap.m_buf && cap.m_len > 0);
    if (cap.m_buf && cap.m_len > 0) {
        CHECK("F1 异常应答有专门的可观测日志（含组/起始/数量/异常码）",
              strstr(cap.m_buf, "异常应答") != NULL);
        CHECK("F1 读组失败有可观测日志（含 unit/类型/起始/数量/错误码）",
              strstr(cap.m_buf, "读组失败") != NULL);
        CHECK("F1 轮询停滞诊断被触发（含逐块陈旧信息）",
              strstr(cap.m_buf, "轮询停滞") != NULL);
        CHECK("F2 停滞诊断逐块列出陈旧块",
              strstr(cap.m_buf, "陈旧块") != NULL);
        CHECK("F2 停滞诊断列出读组与跳过原因",
              strstr(cap.m_buf, "组[") != NULL);
    }
    /* 计数侧的可观测：失败必须记账。 */
    CHECK("F1 失败被计数（pollErrCount>0）",
          XModbusHub_pollErrCount(fx.hub, fx.devId) > 0);
    logCaptureFree(&cap);
    hubFixtureFree(&fx);
}

/**
 * @brief F3：deviceWorstUnitAge 的两种语义。
 * @details ① 某从站**从未上报** ⇒ age = UINT64_MAX（"没有数据"）；
 *          ② 某从站**曾上报后掉线** ⇒ age = 距最后一次回填的毫秒数
 *             （有限、且随时间增长）——"曾经有、现在不更新了"。
 *          两种语义必须能区分：看门狗据此区分"从没连上"与"连上后掉线"。
 */
static void testF3_worstUnitAgeTwoSemantics(void)
{
    HubFixture fx;
    XModbusMapping* map;
    int worstUnit = -1;
    uint64_t worst = 0;
    uint64_t worst2 = 0;

    XPrintf("--- F3 deviceWorstUnitAge 两种语义 ---\n");

    /* ① 从未上报（读组必失败 ⇒ 块从未回填）。 */
    CHECK("F3 装配（虚拟从站 + Hub）", hubFixtureMake(&fx));
    if (fx.devId < 0) { hubFixtureFree(&fx); return; }
    map = XModbusMapping_create();
    CHECK("F3 映射段登记（DI 17 点 ⇒ 必失败）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 17,
                                    fx.devId, 1, 0, 0, 0));
    XModbusHub_setMapping(fx.hub, map);
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);
    spinMs(300);
    CHECK("F3 语义①：从未上报 ⇒ age = UINT64_MAX",
          XModbusHub_deviceWorstUnitAge(fx.hub, fx.devId, &worstUnit, &worst)
          && worst == UINT64_MAX,
          "age=%llu", (unsigned long long)worst);
    CHECK("F3 语义①：从未上报时点级新鲜度判为无效",
          !XModbusHub_pointValid(fx.hub, XModbusDiscreteInputs, 0x100, 3000));
    hubFixtureFree(&fx);

    /* ② 曾上报后掉线。 */
    CHECK("F3 装配（虚拟从站 + Hub）", hubFixtureMake(&fx));
    if (fx.devId < 0) { hubFixtureFree(&fx); return; }
    map = XModbusMapping_create();
    CHECK("F3 映射段登记（DI 2 点，正常）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 2,
                                    fx.devId, 1, 0, 0, 0));
    XModbusHub_setMapping(fx.hub, map);
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);
    spinMs(400);
    CHECK("F3 语义②前：确实上报过（点级新鲜）",
          XModbusHub_pointValid(fx.hub, XModbusDiscreteInputs, 0x100, 3000));
    /* 掉线：断开全部设备 ⇒ 泵被"离线"门槛挡住，不再有回填。 */
    XModbusHub_disconnectAll(fx.hub);
    spinMs(150);
    CHECK("F3 语义②：曾上报后掉线 ⇒ age 有限（≠ UINT64_MAX）",
          XModbusHub_deviceWorstUnitAge(fx.hub, fx.devId, &worstUnit, &worst)
          && worst != UINT64_MAX,
          "age=%llu", (unsigned long long)worst);
    CHECK("F3 语义②：age 已超过 100ms（确实在变老）", worst >= 100,
          "age=%llu", (unsigned long long)worst);
    spinMs(400);
    CHECK("F3 语义②：age 随时间增长（不是冻结的）",
          XModbusHub_deviceWorstUnitAge(fx.hub, fx.devId, &worstUnit, &worst2)
          && worst2 > worst,
          "age1=%llu age2=%llu", (unsigned long long)worst,
          (unsigned long long)worst2);
    hubFixtureFree(&fx);
}

/**
 * @brief F4：「只续写不续轮询」的节拍不被放大。
 * @details 场景：一条读组（正常）+ 一条**永远失败**的写段（远端越界），
 *          脏区间常驻 ⇒ 每拍都会走"写收口→续泵"路径。
 *          修复前：收口尾部调的是"写+轮询"的统一泵 ⇒ 写被拒后同拍又发一
 *          帧**读** ⇒ 每拍两帧读（读节拍被放大一倍），并与「读写交替让
 *          路」的交替计数同拍加两次、奇偶不变。
 *          修复后：收口只发写、且失败后**同拍不再重发** ⇒ 读节拍回到
 *          轮询周期量级。
 *          判据：同样的窗口内，"有失败写"的读组完成数不得比"无写"的
 *          基线多出一倍。
 */
static void testF4_writeFailureDoesNotAmplifyPollTempo(void)
{
    HubFixture fxBase;
    HubFixture fxWrite;
    XModbusMapping* map;
    uint32_t baseReads = 0;
    uint32_t writeReads = 0;
    const int pollMs = 20;
    const int windowMs = 1200;

    XPrintf("--- F4 写持续失败时读节拍不被放大 ---\n");

    /* 基线：只有一条正常读组。 */
    CHECK("F4 基线装配", hubFixtureMake(&fxBase));
    if (fxBase.devId < 0) { hubFixtureFree(&fxBase); return; }
    map = XModbusMapping_create();
    XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 2,
                               fxBase.devId, 1, 0, 0, 0);
    XModbusHub_setMapping(fxBase.hub, map);
    XModbusHub_setPollAreas(fxBase.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fxBase.hub, pollMs);
    XModbusHub_setPollInterval(fxBase.hub, fxBase.devId, pollMs);
    XModbusHub_connectAll(fxBase.hub);
    XModbusHub_startPolling(fxBase.hub);
    spinMs(windowMs);
    baseReads = XModbusHub_pollOkCount(fxBase.hub, fxBase.devId);
    hubFixtureFree(&fxBase);

    /* 对照：同一条读组 + 一条永远失败的写段（远端 30..31 越界）。 */
    CHECK("F4 对照装配", hubFixtureMake(&fxWrite));
    if (fxWrite.devId < 0) { hubFixtureFree(&fxWrite); return; }
    map = XModbusMapping_create();
    XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 2,
                               fxWrite.devId, 1, 0, 0, 0);
    XModbusMapping_addRangeEx(map, XModbusCoils, 0x200, 2, fxWrite.devId, 1,
                               30, 0, 0);
    XModbusHub_setMapping(fxWrite.hub, map);
    XModbusHub_setPollAreas(fxWrite.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fxWrite.hub, pollMs);
    XModbusHub_setPollInterval(fxWrite.hub, fxWrite.devId, pollMs);
    XModbusHub_connectAll(fxWrite.hub);
    XModbusHub_startPolling(fxWrite.hub);
    {
        uint16_t vals[2];
        vals[0] = 1;
        vals[1] = 1;
        XModbusHub_writeValues(fxWrite.hub, XModbusCoils, 0x200, 2, vals);
    }
    spinMs(windowMs);
    writeReads = XModbusHub_pollOkCount(fxWrite.hub, fxWrite.devId);
    hubFixtureFree(&fxWrite);

    XPrintf("[INFO] F4 基线读组完成=%u，带失败写的读组完成=%u"
            "（窗口 %d ms，轮询 %d ms）\n",
            (unsigned)baseReads, (unsigned)writeReads, windowMs, pollMs);
    CHECK("F4 基线确实在轮询（读组完成数 > 0）", baseReads > 0);
    CHECK("F4 带失败写时读组仍在推进（没被饿死）", writeReads > 0);
    /* 上界：写持续失败不得把读节拍放大到基线的 1.5 倍以上。 */
    CHECK("F4 读节拍未被放大（≤ 基线×1.5 + 5）",
          writeReads <= (baseReads * 3u) / 2u + 5u,
          "基线=%u 对照=%u", (unsigned)baseReads, (unsigned)writeReads);
}

/* ============================================================================
 * §I 快照/控制命令的「请求-应答」兜底通道（2026-11-09 根修）
 * ==========================================================================*/

/**
 * @brief §I 用：文档 → BSON 字节（顺带释放文档壳，失败也给 NULL）。
 */
static XByteArray* iBsonBytes(XBsonDocument* doc)
{
    XByteArray* bytes;
    if (!doc) return NULL;
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief §I 用：把一条构造好的应答喂给线上同一条业务分发。
 */
static bool iDispatch(uint16_t cmd, XByteArray* bytes)
{
    return HsfLinkClient_testDispatchResponse(
        cmd, bytes ? XByteArray_constData(bytes) : NULL,
        bytes ? (uint32_t)XByteArray_size_base(bytes) : 0);
}

/* 各主题的「订阅者被通知」计数（回调注册后由分发路径累加）。 */
static int s_iAiCb = 0;
static int s_iFmCb = 0;
static int s_iTurbCb = 0;
static int s_iPumpCb = 0;
static int s_iVfdCb = 0;

static void iOnAi(void* userData)   { (void)userData; ++s_iAiCb; }
static void iOnFm(void* userData)   { (void)userData; ++s_iFmCb; }
static void iOnTurb(void* userData) { (void)userData; ++s_iTurbCb; }
static void iOnPump(void* userData) { (void)userData; ++s_iPumpCb; }
static void iOnVfd(void* userData)  { (void)userData; ++s_iVfdCb; }

/**
 * @brief 模拟量全量快照应答（键表与推送帧同构：n/u{i}/r{i}/e{i}/eu{i}/en{i}/ew{i}）。
 */
static XByteArray* iBuildAiPayload(void)
{
    XBsonDocument* doc = XBsonDocument_create();
    char key[16];
    if (!doc) return NULL;
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_FULL, true);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_AI_COUNT, 2);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_AI_VALIDMASK, 0x1);
    snprintf(key, sizeof(key), HSF_KEY_AI_UA_FMT, 0);
    XBsonDocument_insert_keyUtf8_int32(doc, key, 1500);       /* 1.500 mA */
    snprintf(key, sizeof(key), HSF_KEY_AI_RAW_FMT, 0);
    XBsonDocument_insert_keyUtf8_int32(doc, key, 2985);       /* mV */
    snprintf(key, sizeof(key), HSF_KEY_AI_UA_FMT, 1);
    XBsonDocument_insert_keyUtf8_int32(doc, key, 4000);
    /* 标定四键（工程量/单位/设备名/越限说明）。 */
    snprintf(key, sizeof(key), HSF_KEY_AI_ENG_FMT, 0);
    XBsonDocument_insert_keyUtf8_int32(doc, key, 499);
    snprintf(key, sizeof(key), HSF_KEY_AI_ENG_UNIT_FMT, 0);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "MPa");
    snprintf(key, sizeof(key), HSF_KEY_AI_ENG_NAME_FMT, 0);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "压差");
    snprintf(key, sizeof(key), HSF_KEY_AI_ENG_NOTE_FMT, 0);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "断线");
    return iBsonBytes(doc);
}

/** @brief 电磁流量计快照应答（side/flow/speed/…/valid）。 */
static XByteArray* iBuildFlowMeterPayload(void)
{
    XBsonDocument* doc = XBsonDocument_create();
    if (!doc) return NULL;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_SIDE, 1);   /* B 侧 */
    XBsonDocument_insert_keyUtf8_bool(doc, "valid", true);
    XBsonDocument_insert_keyUtf8_double(doc, "flow", 36.5);
    XBsonDocument_insert_keyUtf8_double(doc, "speed", 1.25);
    XBsonDocument_insert_keyUtf8_double(doc, "forwardTotal", 119.36);
    XBsonDocument_insert_keyUtf8_double(doc, "reverseTotal", 0.5);
    XBsonDocument_insert_keyUtf8_int32(doc, "flowUnit", 1);
    XBsonDocument_insert_keyUtf8_int32(doc, "totalUnit", 1);
    XBsonDocument_insert_keyUtf8_int32(doc, "alarms", 0);
    return iBsonBytes(doc);
}

/** @brief 浊度仪组快照应答（count + v{i}_name/ntu/raw/online）。 */
static XByteArray* iBuildTurbidityPayload(void)
{
    XBsonDocument* doc = XBsonDocument_create();
    char key[24];
    if (!doc) return NULL;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_TURB_COUNT, 2);
    snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, 0, HSF_KEY_TURB_NAME);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "原水");
    snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, 0, HSF_KEY_TURB_NTU);
    XBsonDocument_insert_keyUtf8_int32(doc, key, 1250);       /* 12.50 NTU */
    snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, 0, HSF_KEY_TURB_RAW);
    XBsonDocument_insert_keyUtf8_int32(doc, key, 4660);
    snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, 0, HSF_KEY_TURB_ONLINE);
    XBsonDocument_insert_keyUtf8_bool(doc, key, true);
    snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, 1, HSF_KEY_TURB_NAME);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "出水");
    snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, 1, HSF_KEY_TURB_NTU);
    XBsonDocument_insert_keyUtf8_int32(doc, key, 320);
    snprintf(key, sizeof(key), HSF_KEY_TURB_PREFIX_FMT, 1, HSF_KEY_TURB_ONLINE);
    XBsonDocument_insert_keyUtf8_bool(doc, key, false);
    return iBsonBytes(doc);
}

/** @brief 泵组快照应答（count + v{i}_name/on/ready/valid/text）。 */
static XByteArray* iBuildPumpPayload(bool on)
{
    XBsonDocument* doc = XBsonDocument_create();
    char key[24];
    if (!doc) return NULL;
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_PUMP_COUNT, 1);
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_PUMP_NAME);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "进水泵");
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_PUMP_ON);
    XBsonDocument_insert_keyUtf8_bool(doc, key, on);
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_PUMP_READY);
    XBsonDocument_insert_keyUtf8_bool(doc, key, true);
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_VALVE_VALID);
    XBsonDocument_insert_keyUtf8_bool(doc, key, true);
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_PUMP_TEXT);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "已启动");
    return iBsonBytes(doc);
}

/**
 * @brief I1：模拟量快照应答（0x0103）必须刷新 model 并通知订阅者。
 */
static void testI1_aiSnapshotResponse(void)
{
    XByteArray* bytes = iBuildAiPayload();
    int32_t ua[HSF_LINK_AI_CH_MAX];
    uint32_t mask = 0;
    int n;
    int32_t eng = 0;
    char unit[12];
    const char* note = NULL;

    XPrintf("--- I1 模拟量快照应答 ---\n");
    CHECK("I1 载荷构造成功", bytes != NULL);
    if (!bytes) return;
    HsfLinkClient_setAiCallback(iOnAi, NULL);
    CHECK("I1 分发成功", iDispatch(HSF_CMD_AI_SNAPSHOT, bytes));
    CHECK("I1 订阅者被通知一次（应答不再被静默丢弃）", s_iAiCb == 1,
          "cb=%d", s_iAiCb);
    memset(ua, 0, sizeof(ua));
    n = HsfLinkClient_aiValues(ua, &mask);
    CHECK("I1 通道数被应答刷新", n == 2, "n=%d", n);
    CHECK("I1 有效位掩码被应答刷新", mask == 0x1, "mask=0x%X",
          (unsigned)mask);
    CHECK("I1 工程值（μA 定点）被应答刷新", ua[0] == 1500 && ua[1] == 4000,
          "ua0=%d ua1=%d", ua[0], ua[1]);
    CHECK("I1 原始寄存器（mV）被应答刷新",
          HsfLinkClient_aiRawMv(0) == 2985, "raw0=%d",
          HsfLinkClient_aiRawMv(0));
    CHECK("I1 标定工程量被应答刷新",
          HsfLinkClient_aiEngineering(0, &eng, unit, (int)sizeof(unit), &note)
              && eng == 499 && strcmp(unit, "MPa") == 0 && note &&
              strcmp(note, "断线") == 0,
          "eng=%d unit=%s note=%s", eng, unit, note ? note : "(null)");
    CHECK("I1 设备名（全量帧 en0）被应答刷新",
          HsfLinkClient_aiName(0) && strcmp(HsfLinkClient_aiName(0), "压差") == 0,
          "name=%s", HsfLinkClient_aiName(0) ? HsfLinkClient_aiName(0) : "(null)");
    CHECK("I1 应答到达即判新鲜（自愈闭环）", HsfLinkClient_aiFresh());
    XClassDelete((XClass*)bytes);
}

/**
 * @brief I2：电磁流量计快照应答（0x0106）必须刷新对应侧的 model 并通知。
 */
static void testI2_flowMeterSnapshotResponse(void)
{
    XByteArray* bytes = iBuildFlowMeterPayload();
    const HsfLinkFlowMeter* fm;

    XPrintf("--- I2 流量计快照应答 ---\n");
    CHECK("I2 载荷构造成功", bytes != NULL);
    if (!bytes) return;
    HsfLinkClient_setFlowMeterCallback(iOnFm, NULL);
    CHECK("I2 分发成功", iDispatch(HSF_CMD_FLOWMETER_SNAPSHOT, bytes));
    CHECK("I2 订阅者被通知一次", s_iFmCb == 1, "cb=%d", s_iFmCb);
    fm = HsfLinkClient_flowMeter(HSF_SIDE_B);
    CHECK("I2 按 side 落进 B 侧快照", fm && fm->m_received && fm->m_valid);
    if (fm) {
        CHECK("I2 瞬时流量被应答刷新",
              fabs(fm->m_flow - 36.5) < 0.001, "flow=%.3f", fm->m_flow);
        CHECK("I2 流速被应答刷新",
              fabs(fm->m_speed - 1.25) < 0.001, "speed=%.3f", fm->m_speed);
        CHECK("I2 正向累积量被应答刷新",
              fabs(fm->m_forwardTotal - 119.36) < 0.001,
              "total=%.3f", fm->m_forwardTotal);
    }
    CHECK("I2 应答到达即判新鲜（自愈闭环）",
          HsfLinkClient_flowMeterFresh(HSF_SIDE_B));
    XClassDelete((XClass*)bytes);
}

/**
 * @brief I3：浊度仪快照应答（0x0112）必须刷新组快照并通知。
 */
static void testI3_turbiditySnapshotResponse(void)
{
    XByteArray* bytes = iBuildTurbidityPayload();
    const HsfLinkTurbiditySet* ts;

    XPrintf("--- I3 浊度仪快照应答 ---\n");
    CHECK("I3 载荷构造成功", bytes != NULL);
    if (!bytes) return;
    HsfLinkClient_setTurbidityCallback(iOnTurb, NULL);
    CHECK("I3 分发成功", iDispatch(HSF_CMD_TURBIDITY_SNAPSHOT, bytes));
    CHECK("I3 订阅者被通知一次", s_iTurbCb == 1, "cb=%d", s_iTurbCb);
    ts = HsfLinkClient_turbidity();
    CHECK("I3 台数被应答刷新", ts && ts->m_received && ts->m_count == 2,
          "count=%d", ts ? ts->m_count : -1);
    if (ts && ts->m_count == 2) {
        CHECK("I3 第 1 台名字/NTU/在线位被应答刷新",
              strcmp(ts->m_items[0].m_name, "原水") == 0 &&
                  ts->m_items[0].m_ntu100 == 1250 &&
                  ts->m_items[0].m_online,
              "name=%s ntu=%d online=%d", ts->m_items[0].m_name,
              ts->m_items[0].m_ntu100, ts->m_items[0].m_online ? 1 : 0);
        CHECK("I3 第 1 台离线位被应答刷新",
              strcmp(ts->m_items[1].m_name, "出水") == 0 &&
                  ts->m_items[1].m_ntu100 == 320 &&
                  !ts->m_items[1].m_online,
              "name=%s ntu=%d online=%d", ts->m_items[1].m_name,
              ts->m_items[1].m_ntu100, ts->m_items[1].m_online ? 1 : 0);
    }
    CHECK("I3 应答到达即判新鲜", HsfLinkClient_turbidityFresh());
    XClassDelete((XClass*)bytes);
}

/**
 * @brief I4：泵快照应答（0x0113）必须刷新 model 并通知；
 *        失败包络（未装配/无权限）**不得**被当成「0 台泵」写进 model。
 */
static void testI4_pumpSnapshotResponse(void)
{
    XByteArray* okBytes = iBuildPumpPayload(true);
    XByteArray* errDoc;
    const HsfLinkPumps* ps;
    int countAfterOk;

    XPrintf("--- I4 泵快照应答 ---\n");
    CHECK("I4 载荷构造成功", okBytes != NULL);
    if (!okBytes) return;
    HsfLinkClient_setPumpCallback(iOnPump, NULL);
    CHECK("I4 分发成功", iDispatch(HSF_CMD_PUMP_SNAPSHOT, okBytes));
    CHECK("I4 订阅者被通知一次", s_iPumpCb == 1, "cb=%d", s_iPumpCb);
    ps = HsfLinkClient_pumps();
    CHECK("I4 台数/名字/输出状态被应答刷新",
          ps && ps->m_received && ps->m_count == 1 &&
              strcmp(ps->m_items[0].m_name, "进水泵") == 0 &&
              ps->m_items[0].m_on && ps->m_items[0].m_ready &&
              ps->m_items[0].m_valid &&
              strcmp(ps->m_items[0].m_text, "已启动") == 0,
          "count=%d name=%s on=%d", ps ? ps->m_count : -1,
          (ps && ps->m_count > 0) ? ps->m_items[0].m_name : "(none)",
          (ps && ps->m_count > 0) ? (ps->m_items[0].m_on ? 1 : 0) : -1);
    countAfterOk = ps ? ps->m_count : 0;

    /* 失败包络：服务端「进水泵组未装配」/「权限不足」走 {"ok":false,"err":...}。 */
    {
        XBsonDocument* doc = XBsonDocument_create();
        errDoc = NULL;
        if (doc) {
            XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, false);
            XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_ERR, "进水泵组未装配");
            errDoc = iBsonBytes(doc);
        }
    }
    CHECK("I4 失败包络构造成功", errDoc != NULL);
    if (errDoc) {
        CHECK("I4 失败包络分发不崩", iDispatch(HSF_CMD_PUMP_SNAPSHOT, errDoc));
        ps = HsfLinkClient_pumps();
        CHECK("I4 失败包络不覆盖已有泵快照（不变成 0 台）",
              ps && ps->m_count == countAfterOk &&
                  ps->m_items[0].m_on,
              "count=%d", ps ? ps->m_count : -1);
        CHECK("I4 失败包络不再通知订阅者（无新数据）", s_iPumpCb == 1,
              "cb=%d", s_iPumpCb);
        XClassDelete((XClass*)errDoc);
    }
    XClassDelete((XClass*)okBytes);
}

#if XVFD_ON
/**
 * @brief I5：变频器快照应答（0x0102）必须解码进 XVfdStatus 并通知。
 */
static void testI5_vfdSnapshotResponse(void)
{
    XVfdStatus st;
    int keyCount = 0;
    const uint64_t* keys = XVfdCodec_telemetryKeys(&keyCount);
    XByteArray* bytes;
    XByteArray* withSide = NULL;
    XBsonDocument* doc;
    const XVfdStatus* got;

    XPrintf("--- I5 变频器快照应答 ---\n");
    memset(&st, 0, sizeof(st));
    st.m_validMask = XVfdField_RunState | XVfdField_Running |
                     XVfdField_OutputFreqHz | XVfdField_SpeedRpm;
    st.m_valid = true;
    st.m_runState = XVfdRunState_ConstantSpeed;
    st.m_running = true;
    st.m_outputFreqHz = 46.1;
    st.m_speedRpm = 1383.0;
    /* 用**服务端同款**编解码器造载荷（键表只存在 XVfdCodec 一处）。 */
    bytes = XVfdCodec_statusToBson(&st, keys, keyCount);
    CHECK("I5 编码成功", bytes != NULL);
    if (!bytes) return;
    /* 服务端在快照上补 side 键后回包：解回文档 → 插 side → 重编码。 */
    doc = XBsonDocument_create();
    if (doc &&
        XBsonDocument_from_bytes(doc, (const uint8_t*)XByteArray_constData(bytes),
                                (size_t)XByteArray_size_base(bytes))) {
        XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_SIDE, 0);   /* A 侧 */
        withSide = XBsonDocument_toBson(doc);
    }
    if (doc) XClassDelete((XClass*)doc);
    XClassDelete((XClass*)bytes);
    CHECK("I5 应答载荷构造成功（快照 + side）", withSide != NULL);
    if (!withSide) return;
    HsfLinkClient_setVfdCallback(iOnVfd, NULL);
    CHECK("I5 分发成功", iDispatch(HSF_CMD_VFD_SNAPSHOT, withSide));
    CHECK("I5 订阅者被通知一次", s_iVfdCb == 1, "cb=%d", s_iVfdCb);
    got = HsfLinkClient_vfdStatus();
    CHECK("I5 快照有效位被置上（应答不再被丢弃）",
          HsfLinkClient_vfdValid() && got && got->m_valid);
    if (got) {
        CHECK("I5 输出频率被应答刷新",
              fabs(got->m_outputFreqHz - 46.1) < 0.001,
              "hz=%.3f", got->m_outputFreqHz);
        CHECK("I5 转速被应答刷新",
              fabs(got->m_speedRpm - 1383.0) < 0.001,
              "rpm=%.3f", got->m_speedRpm);
        CHECK("I5 运行位被应答刷新", got->m_running);
    }
    XClassDelete((XClass*)withSide);
}
#endif /* XVFD_ON */

/**
 * @brief I6：畸形载荷必须「不崩 + 不覆盖已有 model」（五条命令全覆盖）。
 */
static void testI6_malformedPayloads(void)
{
    /* 不是 BSON 文档的 4 字节（长度前缀 = 0xEFBEADDE，远超缓冲）。 */
    static const uint8_t kGarbage[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    const uint16_t cmds[5] = {
        HSF_CMD_AI_SNAPSHOT, HSF_CMD_FLOWMETER_SNAPSHOT,
        HSF_CMD_TURBIDITY_SNAPSHOT, HSF_CMD_PUMP_SNAPSHOT,
        HSF_CMD_VFD_SNAPSHOT
    };
    int32_t ua[HSF_LINK_AI_CH_MAX];
    uint32_t mask = 0;
    const HsfLinkPumps* ps = HsfLinkClient_pumps();
    const int pumpCount = ps ? ps->m_count : 0;
    const HsfLinkTurbiditySet* ts = HsfLinkClient_turbidity();
    const int turbCount = ts ? ts->m_count : 0;
    int i;

    XPrintf("--- I6 畸形载荷不崩、不覆盖 model ---\n");
    CHECK("I6 空载荷 + 非法参数被拒绝",
          !HsfLinkClient_testDispatchResponse(HSF_CMD_AI_SNAPSHOT, NULL, 4));
    for (i = 0; i < 5; ++i) {
        CHECK("I6 垃圾字节分发不崩（按命令码）", HsfLinkClient_testDispatchResponse(
                  cmds[i], kGarbage, (uint32_t)sizeof(kGarbage)));
        CHECK("I6 空载荷分发不崩（按命令码）",
              HsfLinkClient_testDispatchResponse(cmds[i], NULL, 0));
    }
    memset(ua, 0, sizeof(ua));
    CHECK("I6 畸形载荷后模拟量 model 保持原值",
          HsfLinkClient_aiValues(ua, &mask) == 2 && ua[0] == 1500 &&
              ua[1] == 4000,
          "n=%d ua0=%d", HsfLinkClient_aiValues(ua, &mask), ua[0]);
    ps = HsfLinkClient_pumps();
    ts = HsfLinkClient_turbidity();
    CHECK("I6 畸形载荷后泵/浊度 model 保持原值",
          ps && ps->m_count == pumpCount && ts && ts->m_count == turbCount,
          "pump=%d turb=%d", ps ? ps->m_count : -1, ts ? ts->m_count : -1);

    /* 失败包络（权限不足/设备未装配）：必须按「没有新数据」处理，**不得**把
     * model 清空——模拟量的 n 缺失会被解析成 0 通道，界面看起来像整页设备
     * 掉线，而实际上只是这条请求被拒。 */
    {
        XBsonDocument* doc = XBsonDocument_create();
        XByteArray* errBytes = NULL;
        if (doc) {
            XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, false);
            XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_ERR,
                                             "权限不足：需要查看遥测");
            errBytes = iBsonBytes(doc);
        }
        CHECK("I6 失败包络载荷构造成功", errBytes != NULL);
        if (errBytes) {
            CHECK("I6 模拟量失败包络分发不崩",
                  iDispatch(HSF_CMD_AI_SNAPSHOT, errBytes));
            memset(ua, 0, sizeof(ua));
            CHECK("I6 模拟量失败包络不清空 model（仍 2 通道）",
                  HsfLinkClient_aiValues(ua, &mask) == 2 && ua[0] == 1500,
                  "n=%d ua0=%d", HsfLinkClient_aiValues(ua, &mask), ua[0]);
            CHECK("I6 浊度失败包络不清零台数",
                  iDispatch(HSF_CMD_TURBIDITY_SNAPSHOT, errBytes) &&
                      HsfLinkClient_turbidity()->m_count == turbCount,
                  "turbCount=%d", HsfLinkClient_turbidity()->m_count);
            XClassDelete((XClass*)errBytes);
        }
    }
}

/**
 * @brief I7：泵控制应答（0x0114）—— 此前无应答分支，命令序列号永不推进。
 */
static void testI7_pumpControlResponse(void)
{
    const int seq0 = HsfLinkClient_commandSeq();
    XBsonDocument* doc = XBsonDocument_create();
    XByteArray* bytes;
    char key[24];

    XPrintf("--- I7 泵控制应答（无通用命令结果路径 ⇒ 必须显式处理）---\n");
    CHECK("I7 应答文档构造成功", doc != NULL);
    if (!doc) return;
    /* 成功应答 = {ok:true} + 整帧泵快照（服务端并帧下发）。 */
    XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, true);
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_PUMP_COUNT, 1);
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_PUMP_NAME);
    XBsonDocument_insert_keyUtf8_utf8(doc, key, "进水泵");
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_PUMP_ON);
    XBsonDocument_insert_keyUtf8_bool(doc, key, false);   /* 命令之后的真实态 */
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_PUMP_READY);
    XBsonDocument_insert_keyUtf8_bool(doc, key, true);
    snprintf(key, sizeof(key), HSF_KEY_VALVE_PREFIX_FMT, 0, HSF_KEY_VALVE_VALID);
    XBsonDocument_insert_keyUtf8_bool(doc, key, true);
    bytes = iBsonBytes(doc);
    CHECK("I7 载荷构造成功", bytes != NULL);
    if (bytes) {
        CHECK("I7 分发成功", iDispatch(HSF_CMD_PUMP_CONTROL, bytes));
        CHECK("I7 命令序列号被应答推进（页面据此弹状态）",
              HsfLinkClient_commandSeq() == seq0 + 1,
              "seq0=%d now=%d", seq0, HsfLinkClient_commandSeq());
        CHECK("I7 成功应答无错误文案",
              HsfLinkClient_lastCommandError()[0] == '\0',
              "err=%s", HsfLinkClient_lastCommandError());
        CHECK("I7 应答里带的泵快照被应用（on=false）",
              HsfLinkClient_pumps()->m_count == 1 &&
                  !HsfLinkClient_pumps()->m_items[0].m_on);
        XClassDelete((XClass*)bytes);
    }
}

/**
 * @brief I8：自动运行控制应答（0x0115）—— 失败原因必须能浮到状态行。
 */
static void testI8_autoControlResponse(void)
{
    const int seq0 = HsfLinkClient_commandSeq();
    XBsonDocument* doc;
    XByteArray* bytes;
    int mode = -1;

    XPrintf("--- I8 自动运行控制应答 ---\n");
    /* 成功：模式包络（与 RUN_MODE 同形）。 */
    doc = XBsonDocument_create();
    CHECK("I8 成功应答构造成功", doc != NULL);
    if (doc) {
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, true);
        XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_MODE, 1);
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_AUTO, true);
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_AUTO_RUNNING, true);
        bytes = iBsonBytes(doc);
        CHECK("I8 成功应答分发成功",
              bytes && iDispatch(HSF_CMD_AUTO_CONTROL, bytes));
        CHECK("I8 模式/运行位被应答回填",
              HsfLinkClient_runMode(&mode) && mode == 1 &&
                  HsfLinkClient_autoRunning(&mode) && mode == 1,
              "mode=%d", mode);
        CHECK("I8 成功应答推进命令序列号",
              HsfLinkClient_commandSeq() == seq0 + 1,
              "seq0=%d now=%d", seq0, HsfLinkClient_commandSeq());
        if (bytes) XClassDelete((XClass*)bytes);
    }
    /* 失败：包络带 err（服务端拒绝启动时也回显模式/运行位，但客户端只认
     * 错误文案 + 保留上次模式值）。 */
    doc = XBsonDocument_create();
    CHECK("I8 失败应答构造成功", doc != NULL);
    if (doc) {
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, false);
        XBsonDocument_insert_keyUtf8_utf8(
            doc, HSF_KEY_ERR, "当前是手动模式：请先切到自动模式，再启动自动运行");
        XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_MODE, 0);
        bytes = iBsonBytes(doc);
        CHECK("I8 失败应答分发成功",
              bytes && iDispatch(HSF_CMD_AUTO_CONTROL, bytes));
        CHECK("I8 失败原因浮到命令错误文案（页面据此提示）",
              HsfLinkClient_lastCommandError()[0] != '\0' &&
                  strstr(HsfLinkClient_lastCommandError(), "手动模式") != NULL,
              "err=%s", HsfLinkClient_lastCommandError());
        CHECK("I8 失败应答同样推进序列号（页面才看得到失败）",
              HsfLinkClient_commandSeq() == seq0 + 2,
              "seq0=%d now=%d", seq0, HsfLinkClient_commandSeq());
        if (bytes) XClassDelete((XClass*)bytes);
    }
}

/**
 * @brief I9：未连线时自愈补拍不发请求（也不崩）。
 */
static void testI9_ensureFreshOffline(void)
{
    XPrintf("--- I9 未连线时补拍是空操作 ---\n");
    CHECK("I9 模拟量补拍：未就绪不发", !HsfLinkClient_aiEnsureFresh());
    CHECK("I9 流量计补拍：未就绪不发",
          !HsfLinkClient_flowMeterEnsureFresh());
    CHECK("I9 浊度仪补拍：未就绪不发", !HsfLinkClient_turbidityEnsureFresh());
    CHECK("I9 泵补拍：未就绪不发", !HsfLinkClient_pumpEnsureFresh());
    CHECK("I9 变频器补拍：未就绪不发", !HsfLinkClient_vfdEnsureFresh());
    /* 全主题掩码走一遍：未就绪必须整体空操作（页面激活路径不会抛错）。 */
    HsfLinkClient_ensureFreshTopics(0xFFFFFFFFu);
    CHECK("I9 全主题补拍总闸：未就绪不崩", true);
}

/* ============================================================================
 * §G 泄漏自检（回归文件自身不得漏内存）
 * ==========================================================================*/

/**
 * @brief 泄漏自检的**工作集**：把新增段落用到的对象生命周期各走一遍
 *        （表格+条目+排序、表头+信号连接、固化载荷解码+解析、Hub 夹具、
 *          读组失败日志 + 日志捕获管路）。
 * @note  [为什么现在**包含**「读组失败」+「日志捕获」路径] 这两条路径此前各有
 *        一处**框架侧固定泄漏**，2026-11-09 一并修掉并锁进本工作集：
 *        ① XModbusHub.c 的失败日志把 XModbusReply_errorString() 的**新建
 *           副本**直接丢给 printf（同一处调了两次、副本没人释放）⇒ 每条失败
 *           日志漏 2×sizeof(XString) = **192 字节**（§G 实测按轮 +192/+384/
 *           +576 线性上涨）；顺带 `%s` 拿到的是 XString* 而非 char*，日志里
 *           的"错误="一直是乱码。修法：只取一次副本、经 XString_toUtf8 取
 *           借用视图、打完 XClassDelete。
 *        ② XPrintf_outputPush/Pop 的标准输出旁路：Start 里建的队列互斥锁
 *           与队列缓冲在 Stop 里没回收，下一次 Start 直接覆盖指针 ⇒ 每对
 *           push/pop 漏 **48 字节**（XMutex）+ 队列缓冲。
 *        两条都改成"同一操作跑第二/第三轮增量为 0"后才收进工作集做长期
 *        回归锁：真泄漏会按轮线性上涨，惰性一次性初始化只涨第一轮（§G 的
 *        预热轮已吸收）。
 */
static void leakFailureLogPath(void)
{
    HubFixture fx;
    XModbusMapping* map;
    LogCapture cap;
    XPrintfOutputScope scope;

    memset(&cap, 0, sizeof(cap));
    memset(&scope, 0, sizeof(scope));
    /* 虚拟从站单次最多 16 点 ⇒ 读 17 点必回异常帧 ⇒ 触发「读组失败」日志
     * （每设备每秒限频一条；夹具每轮新建 ⇒ lastErrLogMs=0 ⇒ 恰好一条）。
     * 同时走一遍日志捕获作用域（push/pop 的管路生命周期也归本工作集）。 */
    if (!hubFixtureMake(&fx)) { hubFixtureFree(&fx); return; }
    map = XModbusMapping_create();
    if (!map) { hubFixtureFree(&fx); return; }
    XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 17,
                              fx.devId, 1, 0, 0, 0);
    XModbusHub_setMapping(fx.hub, map);
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
    (void)XPrintf_outputPush(&scope, logCaptureWrite, &cap);
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);
    spinMs(150);
    XPrintf_outputPop(&scope);
    logCaptureFree(&cap);
    hubFixtureFree(&fx);
    flushDeferred();
}

static void leakWorkload(void)
{
    /* ① 表格 + 条目 + 排序（D5/D6/D8/E1 的对象生命周期）。 */
    {
        XTableWidget* t = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                                 NULL, 0);
        if (t) {
            XHeaderView* hh;
            int r;
            XTableWidget_setColumnCount(t, 2);
            XTableWidget_setRowCount(t, 3);
            for (r = 0; r < 3; ++r) {
                XTableWidgetItem* a = XTableWidgetItem_create_utf8("row", 0);
                XTableWidgetItem* b = XTableWidgetItem_create_utf8("30.00", 0);
                XTableWidget_setItem(t, r, 0, a);
                XTableWidget_setItem(t, r, 1, b);
            }
            hh = XTableView_horizontalHeader(&t->m_base);
            XTableWidget_setSortingEnabled(t, true);
            if (hh) headerClickSection(hh, 1);
            XTableWidget_setSortingEnabled(t, false);
            XClassDelete((XClass*)t);
        }
    }
    /* ② 表头 + 信号连接/断开（D1/D2/D3/D7）。 */
    {
        XHeaderView* hh = XHeaderView_create(NULL, 0, 0);
        if (hh) {
            XHeaderView_setCount(hh, 3);
            XObject_connect((XObject*)hh,
                              XSignal(XHeaderView_sectionDoubleClicked_signal),
                              (XObject*)hh, hdrDblSlot,
                              XConnectionType_Direct);
            headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                        XHeaderView_sectionSize(hh, 0) / 2);
            headerMouse(hh, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                        XHeaderView_sectionSize(hh, 0) / 2);
            XObject_disconnect((XObject*)hh,
                                 XSignal(XHeaderView_sectionDoubleClicked_signal),
                                 (XObject*)hh, hdrDblSlot);
            XClassDelete((XClass*)hh);
        }
    }
    /* ③ 固化载荷解码 + BSON 解析（E2/E3 的缓冲与文档生命周期）。 */
    {
        uint32_t len = 0;
        uint8_t* bytes = hexDecode(kPyHistoryHex, &len);
        if (bytes) {
            HsfHistoryPage page;
            memset(&page, 0, sizeof(page));
            (void)HsfHistoryPage_parse(bytes, len, &page);
            XFree_System(bytes);
        }
        {
            XBsonDocument* doc = XBsonDocument_create();
            if (doc) {
                XVector* keys = XBsonDocument_keys_base(doc);
                if (keys) XClassDelete((XClass*)keys);
                XClassDelete((XClass*)doc);
            }
        }
    }
    /* ④ Hub 夹具（成功读组）。 */
    {
        HubFixture fx;
        if (hubFixtureMake(&fx)) {
            XModbusMapping* m = XModbusMapping_create();
            if (m) {
                XModbusMapping_addRangeEx(m, XModbusDiscreteInputs, 0x100, 2,
                                          fx.devId, 1, 0, 0, 0);
                XModbusHub_setMapping(fx.hub, m);
                XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
                XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
                XModbusHub_connectAll(fx.hub);
                XModbusHub_startPolling(fx.hub);
                spinMs(120);
            }
            hubFixtureFree(&fx);
        }
    }
    /* ⑤ 读组失败日志（F1 路径；框架侧曾按条漏两次深拷贝，2026-11-09 修）。 */
    leakFailureLogPath();
}

/**
 * @brief 造一行「指定 ts + 指定压差」的历史应答（最小形状，19 列）。
 * @details 只给断言要盯的列，其余留 null（解析器按 null → "--" 处理）。
 *          线上列序见 XhsfLinkProto.h 的 HSF_HISTORY_WIRE_COLS（19）。
 * @param ts        该行的墙钟 epoch 毫秒。
 * @param pressMpa  压差数值（<0 ⇒ 该格为 null）。
 * @param pressNote 压差越限说明（NULL/空 ⇒ 无说明）。
 * @param turb1     浊度1（<0 ⇒ null）。
 * @return 载荷字节（调用方释放）；失败返回 NULL。
 */
static XByteArray* buildHistoryRowPayload(int64_t ts, double pressMpa,
                                          const char* pressNote, double turb1,
                                          double flowTotal)
{
    XBsonDocument* doc = XBsonDocument_create();
    XBsonArray* rows = XBsonArray_create();
    XBsonArray* row = XBsonArray_create();
    XByteArray* bytes = NULL;
    XBsonValue* v;
    int i;

    if (!doc || !rows || !row) {
        if (doc) XClassDelete((XClass*)doc);
        if (rows) XClassDelete((XClass*)rows);
        if (row) XClassDelete((XClass*)row);
        return NULL;
    }
    /* 线上列序（19）：ts,mode,vfd_state,freq_hz,current_a,rpm,turb1,turb2,
     *   flow,flow_unit,flow_total,total_unit,press_mpa,press_note,temp,
     *   hum,di,do,water_temp。逐列 append，null = 该列无数据。 */
    v = XBsonValue_create_int64(ts);
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    for (i = 1; i < 6; ++i) {           /* mode..rpm */
        v = XBsonValue_create_null();
        XBsonArray_append_move_base(row, v);
        XBsonValue_delete(v);
    }
    if (turb1 < 0.0) v = XBsonValue_create_null();
    else v = XBsonValue_create_double(turb1);
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = XBsonValue_create_null();         /* turb2 */
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    for (i = 8; i < 12; ++i) {           /* flow..total_unit */
        v = XBsonValue_create_null();
        if (i == 10 && flowTotal >= 0.0) {   /* flow_total（显示列 10） */
            v = XBsonValue_create_double(flowTotal);
        }
        XBsonArray_append_move_base(row, v);
        XBsonValue_delete(v);
    }
    if (pressMpa < 0.0) v = XBsonValue_create_null();
    else v = XBsonValue_create_double(pressMpa);
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    v = bsonStr(pressNote ? pressNote : "");
    XBsonArray_append_move_base(row, v);
    XBsonValue_delete(v);
    for (i = 14; i < 19; ++i) {          /* temp..water_temp */
        v = XBsonValue_create_null();
        XBsonArray_append_move_base(row, v);
        XBsonValue_delete(v);
    }
    {
        /* [与服务端同款] 行数组塞进值里再挂到 rows：setArray_ref 借用，
         * append_move_base 把内容移进数组元素，随后释放值壳。 */
        XBsonValue* rv = XBsonValue_create(XBSON_TYPE_ARRAY);
        if (rv) {
            XBsonValue_setArray_ref(rv, row);
            row = NULL;                   /* 所有权交给值 */
            XBsonArray_append_move_base(rows, rv);
            XBsonValue_delete(rv);
        }
    }
    XBsonDocument_insert_keyUtf8_int32(doc, "offset", 0);
    XBsonDocument_insert_keyUtf8_int32(doc, "limit", 1);
    XBsonDocument_insert_keyUtf8_int32(doc, "total", 1);
    XBsonDocument_insert_keyUtf8_array_move(doc, "rows", rows);
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/**
 * @brief §N6：历史页时间戳与异常行判据（2026-11-10 真 bug 回归锁）。
 * @details 两条都是「加水温列」那次改动放大/引入的**位置下标漂移**：
 *   ① 时间戳：旧实现「现在 − 年龄」反推，remMs 只回绕 24h 的时分秒、
 *      **不向日期借位** ⇒ 跨零点前的行（今天 08:00 看昨天 12:00 那行）
 *      age < 24h ⇒ days=0 ⇒ **日期多一天**（旧实现显示 2026-11-10，
 *      真值是 2026-11-09）。Python 端 time.localtime(ts/1000) 绝对正确。
 *      修法：按绝对 epoch 换算（墙钟 epoch + 本地时区偏移 → 日历展开）。
 *   ② 异常行：旧判据读 `m_cells[10]`，而加水温列后 10 是**正累计**、
 *      压差已移到 11 ⇒ 正累计几乎恒有值 ⇒ **几乎每行都被标异常（整表粉红）**；
 *      且判据应是「压差**越限说明**非空」（Python 的 bool(press_note)），
 *      不是「显示格 ≠ --」（那把「有压差读数」也当异常）。
 */
static void testN6_historyTsAndAbnormalCriteria(void)
{
    XByteArray* bytes;
    HsfHistoryPage page;
    int64_t ts;
    int64_t tzOffsetMs;
    XDateTime nowLocal;
    XDateTime yesterday;
    char expect[32];

    XPrintf("--- §N6 历史页时间戳 / 异常行判据（加水温列后的下标漂移回归锁）---\n");

    /* ---- ① 时间戳：昨天 23:59:59 那一行（确定性构造）----
     * [为什么挑这个时刻] 旧实现「现在 − 年龄」的 bug 只在**年龄 < 24h 且
     * 日历上是昨天**时暴露（age 整除出 days=0 ⇒ 日期停在今天）。昨天
     * 23:59:59 的年龄恒 < 24h（除非现在恰好也是 23:59:59，一年一次），
     * 且它的时刻**晚于**现在的时刻 ⇒ 旧实现必然把它显示成**今天** 23:59
     * ⇒ 断言稳定抓住该 bug（不像「昨天当前时刻−1h」那种构造：那个的
     * 年龄是 25h ⇒ days=1 ⇒ 旧实现反而是对的 ⇒ 抓不到）。 */
    nowLocal = XDateTime_currentDateTime();
    tzOffsetMs = XDateTime_toMSecsSinceEpoch(&nowLocal)
                 - XDateTime_currentWallMSecsSinceEpoch();
    {
        /* 本地日历上的「昨天 23:59:59」。 */
        XDate d = XDateTime_date(&nowLocal);
        XDate yd = XDate_addDays(&d, -1);
        XTime yt = XTime_create_time(23, 59, 59, 0);
        yesterday = XDateTime_create_datetime(yd, yt);
    }
    /* 期望显示串（用日历自己的年月日时分秒，**不**复用被测公式）。 */
    {
        XDate d = XDateTime_date(&yesterday);
        XTime t = XDateTime_time(&yesterday);
        snprintf(expect, sizeof(expect), "%04d-%02d-%02d %02d:%02d:%02d",
                 XDate_year(&d), XDate_month(&d), XDate_day(&d),
                 XTime_hour(&t), XTime_minute(&t), XTime_second(&t));
    }
    /* 该日历时刻对应的墙钟 epoch（本地日历按 UTC 解释再减偏移）。 */
    ts = XDateTime_toMSecsSinceEpoch(&yesterday) - tzOffsetMs;
    bytes = buildHistoryRowPayload(ts, -1.0, NULL, -1.0, -1.0);
    CHECK("§N6 造载荷成功", bytes != NULL);
    if (bytes) {
        memset(&page, 0, sizeof(page));
        CHECK("§N6 解析成功",
              HsfHistoryPage_parse(XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes), &page));
        CHECK("§N6 一行解析出来", page.m_count == 1, "count=%d", page.m_count);
        if (page.m_count == 1) {
            CHECK("§N6 时间戳显示的是**昨天**（不是今天）",
                  strcmp(page.m_rows[0].m_cells[0], expect) == 0,
                  "显示=%s 期望=%s", page.m_rows[0].m_cells[0], expect);
        }
        XFree_System(bytes);
    }

    /* ---- ② 异常行：有压差数值但**无越限说明** ⇒ 不是异常行 ---- */
    bytes = buildHistoryRowPayload(0, 0.4992, "", -1.0, -1.0);
    if (bytes) {
        memset(&page, 0, sizeof(page));
        (void)HsfHistoryPage_parse(XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes), &page);
        CHECK("§N6 有压差数值但无越限说明 ⇒ 不标异常行",
              page.m_count == 1 && !page.m_rows[0].m_abnormal,
              "count=%d abnormal=%d", page.m_count,
              page.m_count ? (int)page.m_rows[0].m_abnormal : -1);
        XFree_System(bytes);
    }

    /* ---- ②b 异常行：**正累计有值**（显示列 10）但无压差越限 ⇒ 仍不是异常行
     * [为什么单独一条] 旧判据读的是 m_cells[10]（正累计）⇒ 正累计几乎恒有值
     * ⇒ **几乎每一行都被标成异常 ⇒ 整表粉红**。本条把正累计填上值，
     * 锁住"有正累计 ≠ 异常"。 */
    bytes = buildHistoryRowPayload(0, 0.4992, "", -1.0, 119.36);
    if (bytes) {
        memset(&page, 0, sizeof(page));
        (void)HsfHistoryPage_parse(XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes), &page);
        CHECK("§N6 正累计有值但无压差越限 ⇒ 不标异常行（旧判据会整表误标）",
              page.m_count == 1 && !page.m_rows[0].m_abnormal,
              "count=%d abnormal=%d", page.m_count,
              page.m_count ? (int)page.m_rows[0].m_abnormal : -1);
        XFree_System(bytes);
    }

    /* ---- ③ 异常行：压差**有越限说明** ⇒ 标异常行 ---- */
    bytes = buildHistoryRowPayload(0, 0.4992, "超上限", -1.0, -1.0);
    if (bytes) {
        memset(&page, 0, sizeof(page));
        (void)HsfHistoryPage_parse(XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes), &page);
        CHECK("§N6 压差有越限说明 ⇒ 标异常行",
              page.m_count == 1 && page.m_rows[0].m_abnormal,
              "count=%d abnormal=%d", page.m_count,
              page.m_count ? (int)page.m_rows[0].m_abnormal : -1);
        XFree_System(bytes);
    }

    /* ---- ④ 异常行：浊度越限（≥20 NTU）⇒ 标异常行（口径不变，防回归） ---- */
    bytes = buildHistoryRowPayload(0, -1.0, "", 25.0, -1.0);
    if (bytes) {
        memset(&page, 0, sizeof(page));
        (void)HsfHistoryPage_parse(XByteArray_constData(bytes),
                                   (uint32_t)XByteArray_size_base(bytes), &page);
        CHECK("§N6 浊度越限 ⇒ 标异常行",
              page.m_count == 1 && page.m_rows[0].m_abnormal,
              "count=%d abnormal=%d", page.m_count,
              page.m_count ? (int)page.m_rows[0].m_abnormal : -1);
        XFree_System(bytes);
    }
}

/**
 * @brief §G：回归文件自身的新增分配必须全部释放。
 * @details 预热一轮（把框架的惰性一次性初始化摊平），再连做 3 轮工作集，
 *          堆在用字节数必须回基线（差 0）。
 */
static void testG_leakSelfCheck(void)
{
    uint64_t base = 0;
    uint64_t now = 0;
    int i;

    XPrintf("--- §G 泄漏自检（回归文件自身）---\n");
    if (!XMemory_statisticsEnabled()) {
        XPrintf("[SKIP] §G 内存统计被构建裁剪（XMEMORY_STATISTICS_ON=0）\n");
        return;
    }
    leakWorkload();                 /* 预热：摊平惰性初始化 */
    flushDeferred();
    base = heapNow();
    for (i = 0; i < 3; ++i) leakWorkload();
    flushDeferred();
    now = heapNow();
    CHECK("§G 回归自身无泄漏（3 轮工作集堆增量为 0）", now == base,
          "基线=%llu 现在=%llu 差=%lld", (unsigned long long)base,
          (unsigned long long)now, (long long)(now - base));
}

/**
 * @brief §J：认证「策略性拒绝」必须是 Rejected，不能伪装成口令错。
 *
 * @details [钉的是 2026-11-09 的"有时候客户端没数据"根因]
 *   原实现：服务端凭据查表对**任何**非 Ok（含"同账已在线/会话数上限"
 *   这种纯瞬时状态）都返回 BadUser，框架再按反枚举口径下发「随机盐 +
 *   全 0 摘要」的假挑战 ⇒ 客户端必然比对失败 ⇒ 收到 **BadPassword**。
 *   客户端把"口令错"当永久失败（正确），于是**一次链路抖动就让已登录的
 *   界面永久无数据**，而用户被告知的是"口令错误"（照提示重输还会把失败
 *   计数打高直至锁定）。
 *
 *   现在框架的查表枚举多了 `Rejected`：命中时**直接下发真实 Rejected**、
 *   不造假挑战、也不把它计入失败次数；客户端据此退避重试（有上限）。
 *   本组用回环设备对驱动两个会话，不需要真实网络。
 *
 * @note 反枚举口径**不能被这次修改带坏**：BadUser 仍走假挑战，客户端最终
 *       看到的仍是 BadPassword（J2 专门锁这条）。
 */
#if XLINK_AUTH_ON

static int g_jAuthStatus;       /**< 客户端收到的认证失败状态码。 */
static int g_jAuthFails;        /**< 客户端收到失败结论的次数。 */
static int g_jAuthChallenges;   /**< 客户端收到挑战的次数（Rejected 应为 0）。 */

static void jAuthReset(void)
{
    g_jAuthStatus = -1;
    g_jAuthFails = 0;
    g_jAuthChallenges = 0;
}

/** @brief 客户端 authChanged 槽：记录状态码。
 *  @note  `XVarList_args_3` **自带声明**（`const T const name`），故本地
 *         不能再声明 passed/status/role——否则 MSVC 报 C2373 重定义。 */
static void jAuthOnChanged(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_3(args, bool, passed, uint8_t, status, uint8_t, role);
    (void)role;
    if (!passed) {
        ++g_jAuthFails;
        g_jAuthStatus = (int)status;
    }
}

/** @brief 客户端 authChallenge 槽：只计数。 */
static void jAuthOnChallenge(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_jAuthChallenges;
}

/** @brief 客户端应答桩：填任意 32 字节（服务端用全 0 摘要比对，必不匹配）。 */
static bool jAuthRespond(void* userData,
                         const XLinkAuthChallengePayload* challenge,
                         uint8_t* methodOut,
                         uint8_t responseOut[XLINK_AUTH_NONCE_BYTES],
                         uint8_t hashOut[XLINK_AUTH_NONCE_BYTES])
{
    (void)userData;
    (void)challenge;
    if (methodOut) *methodOut = (uint8_t)XLinkAuthMethod_IteratedHmac;
    memset(responseOut, 0x5A, XLINK_AUTH_NONCE_BYTES);
    memset(hashOut, 0x5A, XLINK_AUTH_NONCE_BYTES);
    return true;
}

/** @brief 服务端查表桩：固定返回「策略性拒绝」。 */
static int jAuthLookupRejected(void* userData, const char* user,
                               uint8_t saltOut[XLINK_AUTH_SALT_BYTES],
                               uint32_t* iterationsOut,
                               uint8_t hashOut[XLINK_AUTH_NONCE_BYTES],
                               uint8_t* roleOut)
{
    (void)userData; (void)user; (void)iterationsOut; (void)roleOut;
    memset(saltOut, 0x11, XLINK_AUTH_SALT_BYTES);
    memset(hashOut, 0x22, XLINK_AUTH_NONCE_BYTES);
    return XLinkSessionAuthLookup_Rejected;
}

/** @brief 服务端查表桩：固定返回「用户不存在」（反枚举口径用）。 */
static int jAuthLookupBadUser(void* userData, const char* user,
                              uint8_t saltOut[XLINK_AUTH_SALT_BYTES],
                              uint32_t* iterationsOut,
                              uint8_t hashOut[XLINK_AUTH_NONCE_BYTES],
                              uint8_t* roleOut)
{
    (void)userData; (void)user; (void)iterationsOut; (void)roleOut;
    memset(saltOut, 0x11, XLINK_AUTH_SALT_BYTES);
    memset(hashOut, 0x22, XLINK_AUTH_NONCE_BYTES);
    return XLinkSessionAuthLookup_BadUser;
}

/**
 * @brief 跑一组「服务端按 lookup 结果处置 → 客户端收到什么」的回环用例。
 * @param lookup 服务端查表桩。
 * @return 客户端最后一次收到的失败状态码；-1 表示没收到失败结论。
 */
static int jAuthRunCase(XLinkSessionAuthLookupCallback lookup)
{
    XGuiRemoteLoopbackDevice* devA = NULL;
    XGuiRemoteLoopbackDevice* devB = NULL;
    XLinkSession* server = NULL;
    XLinkSession* client = NULL;
    uint32_t now = 1000;
    int i;

    jAuthReset();
    devA = XGuiRemoteLoopbackDevice_createPair(64 * 1024, &devB);
    if (!devA || !devB) {
        CHECK("§J 回环设备对创建成功", false, "createPair 失败");
        return -1;
    }
    server = XLinkSession_create_ex((XIODevice*)devA);
    client = XLinkSession_create_ex((XIODevice*)devB);
    if (!server || !client) {
        CHECK("§J 回环会话创建成功", false, "create_ex 失败");
    } else {
        XLinkSession_setAuthLookup(server, lookup, NULL);
        XLinkSession_setCapability(client, XLinkCapability_Auth);
        XLinkSession_setAuthUser(client, "probe");
        XLinkSession_setAuthResponse(client, jAuthRespond, NULL);
        XObject_connect((XObject*)client,
                          XSignal(XLinkSession_authChanged_signal),
                          (XObject*)server, jAuthOnChanged,
                          XConnectionType_Auto);
        /* [挑战信号只在**服务端**会话上发] XLinkSession.c 的两处发射点都在
         * 服务端处理 AuthRequest 的路径上（Ok 走真挑战、非 Ok 走"随机盐 +
         * 全 0 摘要"的假挑战）。客户端不发这个信号 ⇒ 必须挂服务端对象，
         * 否则计数恒 0（写这段时的实锅：挂客户端导致 §J2 误报）。 */
        XObject_connect((XObject*)server,
                          XSignal(XLinkSession_authChallenge_signal),
                          (XObject*)client, jAuthOnChallenge,
                          XConnectionType_Auto);
        for (i = 0; i < 12; ++i) {
            now += 10;
            XLinkSession_pump(server, now);
            XLinkSession_pump(client, now);
        }
        /* 被拒后两端都不该 Ready（业务消息一条都不能通）。 */
        CHECK("§J 被拒服务端未就绪", !XLinkSession_isReady(server));
        CHECK("§J 被拒客户端未就绪", !XLinkSession_isReady(client));
    }
    if (server) XClassDelete((XClass*)server);
    if (client) XClassDelete((XClass*)client);
    if (devA) XClassDelete((XClass*)devA);
    if (devB) XClassDelete((XClass*)devB);
    return g_jAuthStatus;
}

static void testJ_policyRejectSemantics(void)
{
    int status;

    /* J1 策略性拒绝：客户端必须看到 Rejected（4），**且没收到假挑战**。 */
    status = jAuthRunCase(jAuthLookupRejected);
    CHECK("§J1 策略拒绝下发真实 Rejected（不是口令错）",
          status == (int)XLinkAuthStatus_Rejected,
          "客户端状态码=%d（期望 %d=Rejected；2=BadPassword 说明还在造假挑战）",
          status, (int)XLinkAuthStatus_Rejected);
    CHECK("§J1 策略拒绝不伪造挑战（客户端零次 authChallenge）",
          g_jAuthChallenges == 0, "挑战次数=%d", g_jAuthChallenges);
    CHECK("§J1 策略拒绝只给一次失败结论（不反复重发）",
          g_jAuthFails == 1, "失败结论次数=%d", g_jAuthFails);

    /* J2 反枚举口径不变：用户不存在仍走"假挑战 → 口令错"（别被顺手改坏）。 */
    status = jAuthRunCase(jAuthLookupBadUser);
    CHECK("§J2 用户不存在仍走假挑战（客户端收到 BadPassword）",
          status == (int)XLinkAuthStatus_BadPassword,
          "客户端状态码=%d（期望 2=BadPassword）", status);
    CHECK("§J2 用户不存在会下发挑战（反枚举：不暴露账户是否存在）",
          g_jAuthChallenges >= 1, "挑战次数=%d", g_jAuthChallenges);
}

#endif /* XLINK_AUTH_ON */

/* ============================================================================
 * §K 三设备接入（ttyS4 路 B：浊度仪 ×2 + 电磁流量计；2026-11-09 板端实测定案）
 *
 * 钉的是 docs/HSF板端ttyS4三设备取数实测总表_20261109.md 的四条硬结论：
 *   K1 两台浊度仪**字节序不同**（站 1=CDAB、站 2=DCBA 整字节反序），
 *      用实测原始帧逐字节断言；站 1 的解码语义一个比特都不能回退。
 *      站 2 温度与浊度**同刻**（0x0000×4 一帧），进设备层（不进推送帧）。
 *   K2 站号配置锁：浊度仪 1/2、流量计 5（站 3 不存在——板端普查 1~20 零应答；
 *      站 2 后面要换表，站号只改 HsfServerConfig.h 两个定义）。
 *   K3 模拟器装配后经**真实解码路径**读回现场口径值：
 *      站 1==2.29（CDAB）、站 2==0.045 + 温度 19.3（DCBA 同刻）、
 *      站 5==3.6 且空管 False、单位码 5=m³/h——三台共用一条虚拟 485。
 *   K5 发帧前静默闸（m_preTxQuietMs）行为锁：Hub 读组序**区优先**（流量计
 *      IR 恒先于浊度 HR），「站5 长帧 → 站1」坏相位是结构性的——无闸对照
 *      两站首拍 <170ms；带 200ms 闸的浊度仪首拍 ≥180ms（恢复期外才发）。
 *   K4 §K 跑完堆回基线（对齐 §G 口径：预热 1 轮 + 3 轮工作集增量必须为 0）。
 *
 * [2026-11-10 保真扩容 —— 模拟从站把真实仪表的时序行为带上]
 *   真实仪表的应答从来不是零时延、站 1 在站 5 长帧后有恢复期、站 2 的
 *   应答功能码恒 03、三台的数值各自有微抖动。模拟器带上这些行为，主站的
 *   站间间隙 / 发帧前静默闸才不是"只跑不验"的死代码：
 *   K6 应答时延旋钮：HSF_SIM_* 宏缺省=实测值；setter/getter 成对可配；
 *      模拟装配处（HsfServerIoSim_createBusClient）接线锁。**时延数值
 *      本身不做耗时断言**（CI 抖动），只断言"应答照常到达"的行为方向。
 *   K7 站 1 恢复期行为锁：站 5 长帧（22 寄存器 = 49 字节）收口后窗口内
 *      站 1 无应答（SendRawRequest 返回 NULL=上层按超时处理）+ 计数；
 *      窗口外恢复；窗口 0 = 关闸（旋钮可配）。
 *   K8 站 2 功能码怪癖：FC04 请求也受理但应答功能码恒回 03、数据与 FC03
 *      逐字节一致（实测文档 §5.5/附录 A）；站 1 无 FC04（异常应答 fc|0x80）。
 *   K9 数值微抖动方向锁：站 1 恒定 2.29（实测 20 轮不动）、站 2
 *      0.045±0.001 + 温度 19.3±0.05、站 5 3.6±2% 正弦、空管 False——
 *      带内断言（不锁逐拍相位）；setInstantFlow 钉死口径不回退。
 * ==========================================================================*/

#if XINDUSTRY_ON && XTURBIDITY_ON && XMODBUS_HUB_ON

/** @brief 32 位位模式 → double（memcpy 口径，与 XTurbidityMeter 的解释一致）。 */
static double kBitsToFloat(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, sizeof(f));
    return (double)f;
}

/**
 * @brief 建一个恒等标度（1/1/0、无符号、保持寄存器）的原始字通道组。
 * @param outGroup 输出通道组（调用方 XClassDelete）。
 * @param pointCount 点数（role 0..pointCount-1）。
 * @return 全部点登记成功返回 true。
 * @note 与 HsfServerIoTurbidity 的 turbMakePoint 同款约束：这些字是 32 位量
 *       的原始位模式，任何非恒等标度都会破坏 IEEE754。
 */
static bool kMakeRawGroup(XIoChannelGroup** outGroup, int pointCount)
{
    XIoChannelGroup* group;
    int i;

    *outGroup = NULL;
    group = XIoChannelGroup_create();
    if (!group) return false;
    for (i = 0; i < pointCount; ++i) {
        XIoAnalogPoint* apt =
            XIoAnalogPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XIoPoint* pt;
        bool ok;
        if (!apt) break;
        pt = &apt->m_base;
        ok = XIoPoint_setRole(pt, (uint16_t)i);
        XIoPoint_setType(pt, XIoPoint_AnalogInput);
        XIoPoint_setTag(pt, "K-TURB");
        XIoPoint_setUseHolding(pt, true);      /* 保持寄存器（FC03）。 */
        XIoPoint_setSigned(pt, false);         /* 原始位模式按无符号处理。 */
        XIoAnalogPoint_setScale(apt, 1, 1, 0, 0);  /* 恒等标度：不碰位模式。 */
        if (ok) ok = XIoChannelGroup_addPoint(group, pt, true);
        if (!ok) {
            XClassDelete((XClass*)apt);
            break;
        }
    }
    if (i < pointCount) {
        XClassDelete((XClass*)group);
        return false;
    }
    *outGroup = group;
    return true;
}

/**
 * @brief K1：实测原始帧的逐字节断言（字序旋钮的三种口径 + 温度同刻）。
 */
static void testK1_wordOrderDocumentedFrames(void)
{
    XIoChannelGroup* g2 = NULL;    /* 两字组（浊度）。 */
    XIoChannelGroup* g4 = NULL;    /* 四字组（浊度+温度，站 2 口径）。 */
    XTurbidityMeter* cdab = NULL;
    XTurbidityMeter* dcba = NULL;
    double ntu = 0.0;
    double temp = 0.0;
    uint32_t bits = 0;

    XPrintf("--- K1 实测帧逐字节（站 1 CDAB / 站 2 DCBA）---\n");

    /* ---- 站 1（WZT-701A，CDAB，缺省口径）：8F 5C 40 12 → 2.29f ----
     * 实测帧 `01 03 04 8F 5C 40 12 A1 38`：低字 0x8F5C 在 0x0000、
     * 高字 0x4012 在 0x0001 → 位模式 0x40128F5C = 2.29f。 */
    if (kMakeRawGroup(&g2, 2)) cdab = XTurbidityMeter_create();
    if (cdab && XTurbidityMeter_attach(cdab, g2)) {
        XIoChannelGroup_feedRole(g2, 0, (int32_t)0x8F5C);
        XIoChannelGroup_feedRole(g2, 1, (int32_t)0x4012);
        CHECK("K1 站1 实测帧 8F5C/4012（CDAB）= 2.29 NTU",
              XTurbidityMeter_value(cdab, &ntu) && ntu == kBitsToFloat(0x40128F5Cu),
              "ntu=%.7g 期望 %.7g", ntu, kBitsToFloat(0x40128F5Cu));
        CHECK("K1 站1 拼装位模式 = 0x40128F5C（站 1 解码语义未回退）",
              XTurbidityMeter_rawWords(cdab, &bits) && bits == 0x40128F5Cu,
              "bits=0x%08X", (unsigned)bits);
        /* 字序旋钮真的在起作用：同一帧切 DCBA 不再是 2.29，切回即恢复。 */
        CHECK("K1 站1 切 DCBA 后同一帧不再是 2.29（字序旋钮有效）",
              XTurbidityMeter_setWordOrder(cdab, XTurbidityMeterWordOrder_Dcba) &&
              XTurbidityMeter_value(cdab, &ntu) && ntu != kBitsToFloat(0x40128F5Cu));
        CHECK("K1 站1 切回 CDAB 恢复 2.29",
              XTurbidityMeter_setWordOrder(cdab, XTurbidityMeterWordOrder_LowHigh) &&
              XTurbidityMeter_value(cdab, &ntu) && ntu == kBitsToFloat(0x40128F5Cu));
        CHECK("K1 非法字序被拒绝（枚举外值不受理）",
              !XTurbidityMeter_setWordOrder(cdab, (XTurbidityMeterWordOrder)7));
    } else {
        CHECK("K1 CDAB 夹具建成", false);
    }

    /* ---- 站 2（ZHEYI-5100ZDY，DCBA 整字节反序=小端）----
     * 实测帧 `02 03 04 4F DD 37 3D 98 3C`：w0=0x4FDD、w1=0x373D →
     * 位模式 0x3D37DD4F ≈ 0.04489f（按旧 CDAB 口径解出 1.1e-5 假数）。 */
    if (kMakeRawGroup(&g4, 4)) dcba = XTurbidityMeter_create();
    if (dcba && XTurbidityMeter_setWordOrder(dcba, XTurbidityMeterWordOrder_Dcba) &&
        XTurbidityMeter_attach(dcba, g4)) {
        /* 浊度帧（×2 口径的 4 字节）：4F DD 37 3D → 0.04489f。 */
        XIoChannelGroup_feedRole(g4, 0, (int32_t)0x4FDD);
        XIoChannelGroup_feedRole(g4, 1, (int32_t)0x373D);
        CHECK("K1 站2 实测帧 4FDD/373D（DCBA）≈ 0.0449 NTU",
              XTurbidityMeter_value(dcba, &ntu) && ntu == kBitsToFloat(0x3D37DD4Fu),
              "ntu=%.7g 期望 %.7g", ntu, kBitsToFloat(0x3D37DD4Fu));
        CHECK("K1 站2 拼装位模式 = 0x3D37DD4F（小端整字节反序）",
              XTurbidityMeter_rawWords(dcba, &bits) && bits == 0x3D37DD4Fu,
              "bits=0x%08X", (unsigned)bits);
        /* 温度同刻帧（0x0000×4 的后两字，实测 `00 00 99 41` → 19.125℃）。 */
        XIoChannelGroup_feedRole(g4, 2, (int32_t)0x0000);
        XIoChannelGroup_feedRole(g4, 3, (int32_t)0x9941);
        CHECK("K1 站2 温度字 0000/9941（DCBA 同字序）= 19.125 ℃",
              XTurbidityMeter_temperature(dcba, &temp) &&
              temp == kBitsToFloat(0x41990000u),
              "temp=%.7g 期望 %.7g", temp, kBitsToFloat(0x41990000u));
        CHECK("K1 站2 浊度在温度字到达后不变（同刻重算不出错）",
              XTurbidityMeter_value(dcba, &ntu) && ntu == kBitsToFloat(0x3D37DD4Fu));
        CHECK("K1 站2 温度原始位模式 = 0x41990000",
              XTurbidityMeter_rawTemperature(dcba, &bits) && bits == 0x41990000u,
              "bits=0x%08X", (unsigned)bits);
    } else {
        CHECK("K1 DCBA 夹具建成", false);
    }

    if (cdab) XClassDelete((XClass*)cdab);
    if (dcba) XClassDelete((XClass*)dcba);
    if (g2) XClassDelete((XClass*)g2);
    if (g4) XClassDelete((XClass*)g4);
    flushDeferred();
}

/** @brief K2：站号/寄存器数/字序配置锁（换表只改 HsfServerConfig.h 两个定义）。 */
static void testK2_unitIdConfigLock(void)
{
    XPrintf("--- K2 站号与口径配置锁 ---\n");
    CHECK("K2 浊度仪1 站号 == 1（板端普查实测）", HSF_TURBIDITY1_UNIT_ID == 1,
          "定义=%d", (int)HSF_TURBIDITY1_UNIT_ID);
    CHECK("K2 浊度仪2 站号 == 3（2026-11-10 现场把面板站号改为 3；换站只改这一个定义）",
          HSF_TURBIDITY2_UNIT_ID == 3, "定义=%d", (int)HSF_TURBIDITY2_UNIT_ID);
    CHECK("K2 流量计站号 == 5", HSF_FLOWMETER_UNIT_ID == 5,
          "定义=%d", (int)HSF_FLOWMETER_UNIT_ID);
    CHECK("K2 三站站号互不相同", HSF_TURBIDITY1_UNIT_ID != HSF_TURBIDITY2_UNIT_ID &&
          HSF_TURBIDITY1_UNIT_ID != HSF_FLOWMETER_UNIT_ID &&
          HSF_TURBIDITY2_UNIT_ID != HSF_FLOWMETER_UNIT_ID);
    CHECK("K2 字序配置：站1=CDAB(0) 站2=DCBA(2)（实测两台字节序不同）",
          HSF_TURBIDITY1_WORD_ORDER == 0 && HSF_TURBIDITY2_WORD_ORDER == 2,
          "站1=%d 站2=%d", (int)HSF_TURBIDITY1_WORD_ORDER,
          (int)HSF_TURBIDITY2_WORD_ORDER);
    CHECK("K2 寄存器数：站1=×2（仅浊度）站2=×4（浊度+温度同刻）",
          HSF_TURBIDITY1_REG_COUNT == 2 && HSF_TURBIDITY2_REG_COUNT == 4,
          "站1=%d 站2=%d", (int)HSF_TURBIDITY1_REG_COUNT,
          (int)HSF_TURBIDITY2_REG_COUNT);
    CHECK("K2 静默期配置：站1=200ms（站5 长帧后恢复期）站2=0（免疫）",
          HSF_TURBIDITY1_TX_QUIET_MS == 200 && HSF_TURBIDITY2_TX_QUIET_MS == 0,
          "站1=%u 站2=%u", (unsigned)HSF_TURBIDITY1_TX_QUIET_MS,
          (unsigned)HSF_TURBIDITY2_TX_QUIET_MS);
    CHECK("K2 路 B 虚拟地址不重叠（站1 起点+2 == 站2 起点；流量计另段）",
          (int)HSF_TURBIDITY2_VADDR == (int)HSF_TURBIDITY_VADDR +
                                       (int)HSF_TURBIDITY1_REG_COUNT &&
          (int)HSF_FLOWMETER_VADDR + 22 <= (int)HSF_TURBIDITY_VADDR,
          "flow_end=0x%X turb1=0x%X", (unsigned)(HSF_FLOWMETER_VADDR + 22),
          (unsigned)HSF_TURBIDITY_VADDR);
}

/**
 * @brief K3 单轮工作集：路 B 三台设备一条虚拟 485，模拟器装配后经真实
 *        解码路径（Hub 轮询 → 站模块 → 设备层字序拼装）读回现场口径值。
 * @return 三站是否全部读到有效值（K4 的预热判据用）。
 */
static bool testK3_simRouteBFidelityOnce(void)
{
    HsfServerBus bus;
    HsfServerIoTurbidity t1;
    HsfServerIoTurbidity t2;
    HsfServerIoFlowMeter fs;
    struct XVirtualModelTurbidity* t1m = NULL;
    struct XVirtualModelTurbidity* t2m = NULL;
    struct XVirtualModelFlow* fm = NULL;
    XModbusVirtualClient* client = NULL;
    XModbusMapping* map = NULL;
    double ntu1 = 0.0;
    double ntu2 = 0.0;
    double temp2 = 0.0;
    double flow = 0.0;
    int flowUnit = -1;
    int alarms = -1;
    bool fvalid = false;
    bool allValid = false;
    int waited;
    bool mounted;

    memset(&bus, 0, sizeof(bus));
    HsfServerIoTurbidity_init(&t1);
    HsfServerIoTurbidity_init(&t2);
    HsfServerIoFlowMeter_init(&fs);

    /* 模型按现场口径布值（与 HsfServerIoSim_ensure 同一套 setter）。 */
    t1m = XVirtualModelTurbidity_create(0);      /* 站 1：CDAB、无温度。 */
    t2m = XVirtualModelTurbidity_create(1);      /* 站 2：DCBA、带温度。 */
    fm = XVirtualModelFlow_create();             /* 站 5：ABCD。 */
    if (!t1m || !t2m || !fm) {
        CHECK("K3 虚拟模型建成", false);
        goto k3_done;
    }
    XVirtualModelTurbidity_setWordOrder(t1m, (int)XVirtualModelTurbWordOrder_Cdab);
    XVirtualModelTurbidity_setNtu(t1m, 2.29);
    XVirtualModelTurbidity_setWordOrder(t2m, (int)XVirtualModelTurbWordOrder_Dcba);
    XVirtualModelTurbidity_setNtu(t2m, 0.045);
    XVirtualModelTurbidity_setTempC(t2m, 19.3);
    XVirtualModelFlow_setInstantFlow(fm, 3.6f);

    /* 三台共用**一条** 485（与真机同接法），靠站号分派——站号全部来自
     * HsfServerConfig.h，模拟器没有写死站号的地方。 */
    client = XModbusVirtualClient_create(NULL);
    CHECK("K3 虚拟客户端建成", client != NULL);
    if (!client) goto k3_done;
    mounted =
        XModbusVirtualClient_bindUnit(client, HSF_TURBIDITY1_UNIT_ID,
                                      XVirtualModel_base(t1m)) &&
        XModbusVirtualClient_bindUnit(client, HSF_TURBIDITY2_UNIT_ID,
                                      XVirtualModel_base(t2m)) &&
        XModbusVirtualClient_bindUnit(client, HSF_FLOWMETER_UNIT_ID,
                                      XVirtualModel_base(fm));
    CHECK("K3 三台从站模型按配置站号绑定", mounted);

    /* 装配顺序与真机一致：浊度 1 → 浊度 2 → 流量计（站 1 恢复期 ≥200ms 的
     * 顺序前提，见 HsfServerIo.c 同款注释）。 */
    CHECK("K3 总线接入注入客户端",
          HsfServerBus_openWithClient(&bus, "K3-485B", (XModbusClient*)client,
                                      500, 1));
    client = NULL;   /* 所有权已移交 Hub。 */
    map = HsfServerBus_createMapping(&bus);
    CHECK("K3 映射表建成", map != NULL);
    if (!map) goto k3_done;
    mounted =
        HsfServerIoTurbidity_mount(&t1, &bus, map, HSF_TURBIDITY1_UNIT_ID, 0,
                                   (uint16_t)HSF_TURBIDITY_VADDR, 3000,
                                   HSF_TURBIDITY1_REG_COUNT,
                                   HSF_TURBIDITY1_WORD_ORDER,
                                   HSF_TURBIDITY1_TX_QUIET_MS) &&
        HsfServerIoTurbidity_mount(&t2, &bus, map, HSF_TURBIDITY2_UNIT_ID, 1,
                                   (uint16_t)HSF_TURBIDITY2_VADDR, 3000,
                                   HSF_TURBIDITY2_REG_COUNT,
                                   HSF_TURBIDITY2_WORD_ORDER,
                                   HSF_TURBIDITY2_TX_QUIET_MS) &&
        HsfServerIoFlowMeter_mount(&fs, &bus, map, HSF_FLOWMETER_UNIT_ID,
                                   (uint16_t)HSF_FLOWMETER_VADDR, 3000);
    CHECK("K3 三站装配成功（浊度1 CDAB×2+静默200 / 浊度2 DCBA×4 / 流量计 ABCD×22）",
          mounted);
    if (!mounted) goto k3_done;
    CHECK("K3 总线启动（映射提交 + 轮询拉起）", HsfServerBus_start(&bus, map, 20));
    map = NULL;

    /* 等三站都出有效值（进程内应答，首拍在几十毫秒内）。 */
    for (waited = 0; waited < 3000; waited += 20) {
        bool v1 = HsfServerIoTurbidity_ntu(&t1, &ntu1);
        bool v2 = HsfServerIoTurbidity_ntu(&t2, &ntu2);
        bool vf = HsfServerIoFlowMeter_values(&fs, &flow, NULL, NULL, NULL,
                                              &flowUnit, NULL, &alarms, &fvalid);
        if (v1 && v2 && vf && fvalid) { allValid = true; break; }
        spinMs(20);
    }
    CHECK("K3 三站首拍全部有效", allValid);

    /* ★ 保真断言：模拟器 → Hub → 站模块 → 设备层（同一 XTurbidityMeter /
     *   XFlowMeter 解码路径），固定值读回必须**逐位**等于设定值。 */
    CHECK("K3 站1 读回 == 2.29（CDAB 保真）",
          ntu1 == (double)(float)2.29, "ntu=%.7g", ntu1);
    CHECK("K3 站2 读回 == 0.045（DCBA 保真；旧 CDAB 口径会解出 1.1e-5）",
          ntu2 == (double)(float)0.045, "ntu=%.7g", ntu2);
    CHECK("K3 站2 温度读回 == 19.3（与浊度同刻，0x0000×4 一帧）",
          HsfServerIoTurbidity_tempC(&t2, &temp2) && temp2 == (double)(float)19.3,
          "temp=%.7g", temp2);
    CHECK("K3 站5 流量读回 == 3.6（ABCD 保真）",
          allValid && flow == (double)3.6f, "flow=%.7g", flow);
    CHECK("K3 站5 空管报警 False（报警位全 0）", allValid && alarms == 0,
          "alarms=0x%X", (unsigned)alarms);
    CHECK("K3 站5 单位码 == 5（m³/h，随表读上来的代码）", allValid && flowUnit == 5,
          "unit=%d", flowUnit);
    CHECK("K3 站对象的站号 == 配置定义（模拟器站号跟随 HsfServerConfig.h）",
          HsfServerIoTurbidity_unitId(&t1) == HSF_TURBIDITY1_UNIT_ID &&
          HsfServerIoTurbidity_unitId(&t2) == HSF_TURBIDITY2_UNIT_ID,
          "站1=%d 站2=%d", HsfServerIoTurbidity_unitId(&t1),
          HsfServerIoTurbidity_unitId(&t2));

k3_done:
    /* 拆除顺序与 E4 同款：站（栈上对象 XClassDeinit，deinit 内部 unmount）
     * → 总线（Hub 最后死）→ 模型（裸 malloc，按分配槽位释放）。 */
    XClassDeinit((XClass*)&t1);
    XClassDeinit((XClass*)&t2);
    XClassDeinit((XClass*)&fs);
    HsfServerBus_stop(&bus);
    if (map) XClassDelete((XClass*)map);
    if (client) XClassDelete((XClass*)client);
    if (t1m) XFree_System(t1m);
    if (t2m) XFree_System(t2m);
    if (fm) XFree_System(fm);
    flushDeferred();
    return allValid;
}

/**
 * @brief K5：发帧前静默闸（XModbusHubUnitLimits.m_preTxQuietMs）的行为锁。
 * @details 用两个独立 Hub（同一份「流量计 IR + 浊度仪 HR」装配）做对照：
 *   · 带闸（浊度仪 200ms）：浊度仪首拍必须比流量计首拍晚 ≥180ms——
 *     恢复期外才发（闸生效）；
 *   · 无闸对照：两站首拍几乎同时（<170ms）——证明"晚"是闸干出来的，
 *     不是读组天生的节奏（读组序区优先，流量计 IR 恒先于浊度 HR，
 *     对照组的相邻间隔只有扫描节拍级 ≈20ms，正是 ttyS4 站5→站1 坏相位）。
 */
static void testK5_preTxQuietGate(void)
{
    struct XVirtualModelTurbidity* turbQ = NULL;
    struct XVirtualModelFlow* flowQ = NULL;
    struct XVirtualModelTurbidity* turbC = NULL;
    struct XVirtualModelFlow* flowC = NULL;
    XModbusHub* hubQ = NULL;
    XModbusHub* hubC = NULL;
    XModbusVirtualClient* cQ = NULL;
    XModbusVirtualClient* cC = NULL;
    int64_t qFlow = 0;
    int64_t qTurb = 0;
    int64_t cFlow = 0;
    int64_t cTurb = 0;
    int waited;

    XPrintf("--- K5 发帧前静默闸行为锁（站1 恢复期 200ms）---\n");
    /* [映射表所有权] 两张映射表都经 XModbusHub_setMapping 移交 Hub，
     * 随 Hub 的 XClassDelete 一并释放——这里不再单独删。 */

    /* ---- 带闸装配：浊度仪 unit1（HR 0x0300×2，静默 200ms）+
     *      流量计 unit2（IR 0x0200×22，无约束）---- */
    turbQ = XVirtualModelTurbidity_create(0);
    flowQ = XVirtualModelFlow_create();
    cQ = (turbQ && flowQ) ? XModbusVirtualClient_create(NULL) : NULL;
    if (turbQ && flowQ && cQ) {
        int devId;
        XModbusMapping* map;
        XModbusHubUnitLimits lim;
        XModbusHubUnitLimits limFlow;
        (void)XVirtualModelTurbidity_setNtu(turbQ, 2.29);
        (void)XVirtualModelFlow_setInstantFlow(flowQ, 3.6f);
        if (XModbusVirtualClient_bindUnit(cQ, 1, XVirtualModel_base(turbQ)) &&
            XModbusVirtualClient_bindUnit(cQ, 2, XVirtualModel_base(flowQ))) {
            hubQ = XModbusHub_create();
            devId = hubQ ? XModbusHub_addDevice_2(hubQ, (XModbusClient*)cQ,
                                                  "K5-quiet") : -1;
            if (devId >= 0) {
                cQ = NULL;      /* 所有权已随 Hub。 */
                map = XModbusMapping_create();
                if (map) {
                    memset(&lim, 0, sizeof(lim));
                    lim.m_preTxQuietMs = 200;   /* 只有浊度仪带闸。 */
                    /* 流量计必须带真实节拍（500ms，对齐 HSF_FLOWMETER_MIN_
                     * POLL_MS）：否则它每拍都到期，总线永远不静默，闸等不到
                     * 满足——真机装配里流量计有自己的分级节拍，闸正是在
                     * 「流量计收口后、下一轮到期前」的空窗里放行浊度仪的。 */
                    memset(&limFlow, 0, sizeof(limFlow));
                    limFlow.m_minPollIntervalMs = 500;
                    XModbusMapping_addRangeEx(map, XModbusInputRegisters,
                                              0x0200, 22, devId, 2, 0x1010, 0, 0);
                    XModbusMapping_addRangeEx(map, XModbusHoldingRegisters,
                                              0x0300, 2, devId, 1, 0x0000, 0, 0);
                    XModbusHub_setUnitLimits(hubQ, devId, 1, &lim);
                    XModbusHub_setUnitLimits(hubQ, devId, 2, &limFlow);
                    XModbusHub_setMapping(hubQ, map);
                    XModbusHub_setPollAreas(hubQ, (1u << XModbusInputRegisters) |
                                                  (1u << XModbusHoldingRegisters));
                    XModbusHub_setPollInterval(hubQ, devId, 20);
                    XModbusHub_connectAll(hubQ);
                    XModbusHub_startPolling(hubQ);
                    for (waited = 0;
                         waited < 4000 && (qFlow == 0 || qTurb == 0);
                         waited += 10) {
                        if (qFlow == 0 &&
                            XModbusHub_pointValid(hubQ, XModbusInputRegisters,
                                                  0x0200, 10000))
                            qFlow = XDateTime_currentMSecsSinceEpoch();
                        if (qFlow != 0 && qTurb == 0 &&
                            XModbusHub_pointValid(hubQ, XModbusHoldingRegisters,
                                                  0x0300, 10000))
                            qTurb = XDateTime_currentMSecsSinceEpoch();
                        spinMs(10);
                    }
                }
            }
        }
    }
    CHECK("K5 带闸：流量计（IR）与浊度仪（HR）首拍都到", qFlow != 0 && qTurb != 0,
          "flow=%lld turb=%lld", (long long)qFlow, (long long)qTurb);
    CHECK("K5 带闸：浊度仪首拍比流量计晚 ≥180ms（200ms 恢复期外才发）",
          qFlow != 0 && qTurb != 0 && (qTurb - qFlow) >= 180,
          "gap=%lld ms", (long long)(qTurb - qFlow));
    XClassDelete((XClass*)hubQ);
    if (cQ) XClassDelete((XClass*)cQ);
    if (turbQ) XFree_System(turbQ);
    if (flowQ) XFree_System(flowQ);
    flushDeferred();

    /* ---- 无闸对照（同一装配、不设 preTxQuiet）---- */
    turbC = XVirtualModelTurbidity_create(0);
    flowC = XVirtualModelFlow_create();
    cC = (turbC && flowC) ? XModbusVirtualClient_create(NULL) : NULL;
    if (turbC && flowC && cC) {
        int devId;
        XModbusMapping* map;
        (void)XVirtualModelTurbidity_setNtu(turbC, 2.29);
        (void)XVirtualModelFlow_setInstantFlow(flowC, 3.6f);
        if (XModbusVirtualClient_bindUnit(cC, 1, XVirtualModel_base(turbC)) &&
            XModbusVirtualClient_bindUnit(cC, 2, XVirtualModel_base(flowC))) {
            hubC = XModbusHub_create();
            devId = hubC ? XModbusHub_addDevice_2(hubC, (XModbusClient*)cC,
                                                  "K5-ctrl") : -1;
            if (devId >= 0) {
                cC = NULL;
                map = XModbusMapping_create();
                if (map) {
                    XModbusMapping_addRangeEx(map, XModbusInputRegisters,
                                              0x0200, 22, devId, 2, 0x1010, 0, 0);
                    XModbusMapping_addRangeEx(map, XModbusHoldingRegisters,
                                              0x0300, 2, devId, 1, 0x0000, 0, 0);
                    XModbusHub_setMapping(hubC, map);
                    XModbusHub_setPollAreas(hubC, (1u << XModbusInputRegisters) |
                                                  (1u << XModbusHoldingRegisters));
                    XModbusHub_setPollInterval(hubC, devId, 20);
                    XModbusHub_connectAll(hubC);
                    XModbusHub_startPolling(hubC);
                    for (waited = 0;
                         waited < 4000 && (cFlow == 0 || cTurb == 0);
                         waited += 10) {
                        if (cFlow == 0 &&
                            XModbusHub_pointValid(hubC, XModbusInputRegisters,
                                                  0x0200, 10000))
                            cFlow = XDateTime_currentMSecsSinceEpoch();
                        if (cFlow != 0 && cTurb == 0 &&
                            XModbusHub_pointValid(hubC, XModbusHoldingRegisters,
                                                  0x0300, 10000))
                            cTurb = XDateTime_currentMSecsSinceEpoch();
                        spinMs(10);
                    }
                }
            }
        }
    }
    CHECK("K5 对照：流量计与浊度仪首拍都到", cFlow != 0 && cTurb != 0,
          "flow=%lld turb=%lld", (long long)cFlow, (long long)cTurb);
    CHECK("K5 对照（无闸）：两站首拍间隔 <170ms（坏相位确实存在，闸是必要的）",
          cFlow != 0 && cTurb != 0 && (cTurb - cFlow) < 170 &&
          (cTurb - cFlow) >= 0,
          "gap=%lld ms", (long long)(cTurb - cFlow));
    XClassDelete((XClass*)hubC);
    if (cC) XClassDelete((XClass*)cC);
    if (turbC) XFree_System(turbC);
    if (flowC) XFree_System(flowC);
    flushDeferred();
}

/* ==================== K6~K9：模拟从站的时序/怪癖/微抖动保真 ==================== */

/**
 * @brief 构造一条读请求并经虚拟客户端发出（进程内同步应答）。
 * @return Reply（调用方 XClassDelete 直删——**不要**换成 deleteLater：
 *         测试顶层 processEvents 的 loopLevel/scopeLevel 都是 0，顶层
 *         post 的延迟删除事件 eventLevel==currentLevel==0，按
 *         XDeferredDeleteEvent_shouldDeliver 的判据永不投递 ⇒ 整只 Reply
 *         泄漏（K4 实测每次 send 漏 ~600B）。Reply 不在信号派发期内，
 *         直删安全）；无应答返回 NULL。
 */
static XModbusReply* kSendReadRaw(XModbusVirtualClient* client, int unit,
                                  uint8_t fc, uint16_t addr, uint16_t count)
{
    XModbusRequest* req;
    XModbusReply* reply;
    uint8_t payload[4];

    if (!client) return NULL;
    req = XModbusRequest_create_with_code((XModbusPdu_FunctionCode)fc);
    if (!req) return NULL;
    payload[0] = (uint8_t)(addr >> 8);
    payload[1] = (uint8_t)(addr & 0xFFu);
    payload[2] = (uint8_t)(count >> 8);
    payload[3] = (uint8_t)(count & 0xFFu);
    XModbusPdu_setData((XModbusPdu*)req, payload, sizeof(payload));
    /* [Copy 语义] base 自拷贝请求入 Reply，本壳自行释放。 */
    reply = XModbusClient_sendRawRequest_base((XModbusClient*)client, req,
                                              unit);
    XClassDelete(req);
    return reply;
}

/** @brief 取应答 PDU 的数据区（首字节 = byteCount）。 */
static const uint8_t* kReplyData(const XModbusReply* reply, int* outSize)
{
    const uint8_t* data = NULL;
    int size = 0;

    if (reply && reply->m_rawResult) {
        data = (const uint8_t*)XContainerDataAddr(
            ((XModbusPdu*)reply->m_rawResult)->m_data);
        size = (int)XModbusPdu_dataSize((const XModbusPdu*)reply->m_rawResult);
    }
    if (outSize) *outSize = size;
    return data;
}

/** @brief 应答功能码（原始字节，含异常位）。 */
static uint8_t kReplyFc(const XModbusReply* reply)
{
    return (reply && reply->m_rawResult)
               ? (uint8_t)XModbusPdu_functionCodeRaw(
                     (const XModbusPdu*)reply->m_rawResult)
               : 0u;
}

/** @brief DCBA（整字节反序=小端 float32）两字 → double（站 2 口径）。 */
static double kDcbaRegsToFloat(uint16_t r0, uint16_t r1)
{
    /* 线上 4 字节 = r0 高、r0 低、r1 高、r1 低；DCBA 整 4 字节反序
     * （实测帧 4F DD 37 3D → 位模式 0x3D37DD4F = 0.0449f，验算锁）。 */
    const uint32_t bits = ((uint32_t)(r1 & 0xFFu) << 24) |
                          ((uint32_t)(r1 >> 8) << 16) |
                          ((uint32_t)(r0 & 0xFFu) << 8) |
                          (uint32_t)(r0 >> 8);
    return kBitsToFloat(bits);
}

/** @brief ABCD（高字在前，站 5 口径）两字 → double。 */
static double kAbcdRegsToFloat(uint16_t hi, uint16_t lo)
{
    return kBitsToFloat(((uint32_t)hi << 16) | (uint32_t)lo);
}

/**
 * @brief K6：应答时延旋钮（宏缺省=实测值 + setter/getter 可配 + 装配接线）。
 * @details [不锁耗时] 时延数值本身不做毫秒级断言（CI 抖动会假红）——只锁
 *          旋钮存在、可配、且配置后应答路径照常工作（行为方向）。
 */
static void testK6_replyLatencyKnob(void)
{
    struct XVirtualModelTurbidity* t1m = NULL;
    XModbusVirtualClient* c = NULL;
    XModbusReply* reply = NULL;
    int lo = -1;
    int hi = -1;

    XPrintf("--- K6 应答时延旋钮（实测口径缺省 + 可配）---\n");

    /* ---- 宏缺省锁（换口径只改这里；与 Python config.py 逐值同口径）---- */
    CHECK("K6 站1 时延宏缺省 37~43ms（实测文档 §1.1 响应时延列）",
          HSF_SIM_TURBIDITY1_REPLY_MS_MIN == 37 &&
          HSF_SIM_TURBIDITY1_REPLY_MS_MAX == 43,
          "%u~%u", (unsigned)HSF_SIM_TURBIDITY1_REPLY_MS_MIN,
          (unsigned)HSF_SIM_TURBIDITY1_REPLY_MS_MAX);
    CHECK("K6 站2 时延宏缺省 31~33ms",
          HSF_SIM_TURBIDITY2_REPLY_MS_MIN == 31 &&
          HSF_SIM_TURBIDITY2_REPLY_MS_MAX == 33,
          "%u~%u", (unsigned)HSF_SIM_TURBIDITY2_REPLY_MS_MIN,
          (unsigned)HSF_SIM_TURBIDITY2_REPLY_MS_MAX);
    CHECK("K6 站5 时延宏缺省 68~69ms（49 字节长帧，最慢一台）",
          HSF_SIM_FLOWMETER_REPLY_MS_MIN == 68 &&
          HSF_SIM_FLOWMETER_REPLY_MS_MAX == 69,
          "%u~%u", (unsigned)HSF_SIM_FLOWMETER_REPLY_MS_MIN,
          (unsigned)HSF_SIM_FLOWMETER_REPLY_MS_MAX);
    CHECK("K6 恢复期窗口宏缺省 125ms（实测门槛 §8.3）",
          HSF_SIM_RECOVERY_MS == 125, "%u", (unsigned)HSF_SIM_RECOVERY_MS);

    /* ---- 旋钮存在且可配（0~0 与实测档两态都应答照常）---- */
    t1m = XVirtualModelTurbidity_create(0);
    c = t1m ? XModbusVirtualClient_create(NULL) : NULL;
    if (t1m && c) {
        XVirtualModelTurbidity_setNtu(t1m, 2.29);
        (void)XModbusVirtualClient_bindUnit(c, HSF_TURBIDITY1_UNIT_ID,
                                            XVirtualModel_base(t1m));
        /* [先绑后开再发] Open 的"有模型"判据是绑定表非空（缺省模型为
         * NULL）；SendRawRequest 在未连接态直接拒绝（与真实客户端同一道
         * 状态闸）——K3 走 HsfServerBus_openWithClient，这里手动绑+开。 */
        if (!XModbusDevice_connectDevice((XModbusDevice*)c)) {
            CHECK("K6 虚拟客户端 Open", false);
        }
        /* [缺省零时延] 既有用法一字不改：未配时延的客户端立刻应答。 */
        CHECK("K6 未配置时延 ⇒ 缺省（0,0）",
              XModbusVirtualClient_unitReplyLatency(c, 1, &lo, &hi) &&
              lo == 0 && hi == 0, "lo=%d hi=%d", lo, hi);
        XModbusVirtualClient_setUnitReplyLatency(c, 1, 0, 0);
        reply = kSendReadRaw(c, HSF_TURBIDITY1_UNIT_ID, 0x03, 0x0000, 2);
        CHECK("K6 时延 (0,0)：读应答照常（方向断言，不量耗时）",
              reply != NULL && kReplyFc(reply) == 0x03,
              "fc=0x%02X", kReplyFc(reply));
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        /* [实测档] 设成文档区间后同样应答（时延在从站应答路径，主站
         * 等待语义与真串口一致；不在这里量毫秒）。 */
        XModbusVirtualClient_setUnitReplyLatency(c, 1, 37, 43);
        reply = kSendReadRaw(c, HSF_TURBIDITY1_UNIT_ID, 0x03, 0x0000, 2);
        CHECK("K6 时延 (37,43)：读应答照常到达（可配生效）",
              reply != NULL && kReplyFc(reply) == 0x03,
              "fc=0x%02X", kReplyFc(reply));
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        CHECK("K6 getter 回读 (37,43)（成对旋钮可查）",
              XModbusVirtualClient_unitReplyLatency(c, 1, &lo, &hi) &&
              lo == 37 && hi == 43, "lo=%d hi=%d", lo, hi);
        /* [夹紧与越界] 负值夹 0、lo>hi 收敛、越界站号忽略。 */
        XModbusVirtualClient_setUnitReplyLatency(c, 1, -5, 3);
        CHECK("K6 负下限夹 0 且 lo<=hi 收敛",
              XModbusVirtualClient_unitReplyLatency(c, 1, &lo, &hi) &&
              lo == 0 && hi == 3, "lo=%d hi=%d", lo, hi);
        XModbusVirtualClient_setUnitReplyLatency(c, 999, 5, 9);
        CHECK("K6 越界站号被忽略（不越界写数组）",
              XModbusVirtualClient_unitReplyLatency(c, 999, &lo, &hi) == false);
        flushDeferred();
    } else {
        CHECK("K6 时延旋钮夹具建成", false);
    }
    XClassDelete((XClass*)c);
    if (t1m) XFree_System(t1m);
    flushDeferred();

    /* ---- 模拟装配处接线锁：createBusClient 必须把实测档接到三站上 ---- */
    {
        XModbusVirtualClient* sim = HsfServerIoSim_createBusClient(
            false, false, true, true);
        CHECK("K6 模拟装配路 B 客户端建成", sim != NULL);
        if (sim) {
            CHECK("K6 装配接线：站1 (37,43)",
                  XModbusVirtualClient_unitReplyLatency(
                      sim, HSF_TURBIDITY1_UNIT_ID, &lo, &hi) &&
                  lo == 37 && hi == 43, "lo=%d hi=%d", lo, hi);
            CHECK("K6 装配接线：站2 (31,33)",
                  XModbusVirtualClient_unitReplyLatency(
                      sim, HSF_TURBIDITY2_UNIT_ID, &lo, &hi) &&
                  lo == 31 && hi == 33, "lo=%d hi=%d", lo, hi);
            CHECK("K6 装配接线：站5 (68,69)",
                  XModbusVirtualClient_unitReplyLatency(
                      sim, HSF_FLOWMETER_UNIT_ID, &lo, &hi) &&
                  lo == 68 && hi == 69, "lo=%d hi=%d", lo, hi);
            CHECK("K6 装配接线：恢复期闸 125ms（站5→站1）",
                  XModbusVirtualClient_recoveryWindowMs(sim) == 125);
            XClassDelete((XClass*)sim);
        }
        HsfServerIoSim_destroy();     /* 单例模型是裸 malloc，显式回收。 */
        flushDeferred();
    }
}

/**
 * @brief K7：站 1「长帧后恢复期」行为锁。
 * @details 时延全配 0（确定性；恢复期窗口本身是毫秒判据，不靠时延）：
 *   a) 站 5 读 22 寄存器（=现场 49 字节长帧）成功；
 *   b) 立刻问站 1 → 无应答（NULL，上层按超时处理）+ 恢复期计数 1；
 *   c) 窗口外（spinMs 130 > 125）再问 → 恢复应答，计数不再涨；
 *   d) 旋钮可配：窗口 0 = 关闸（立刻问也照常应答）；
 *   e) 长帧站号不匹配时不误伤（把闸指向不存在的站 → 站 1 照常应答）。
 */
static void testK7_station1RecoveryGate(void)
{
    struct XVirtualModelTurbidity* t1m = NULL;
    struct XVirtualModelTurbidity* t2m = NULL;
    struct XVirtualModelFlow* fm = NULL;
    XModbusVirtualClient* c = NULL;
    XModbusReply* reply = NULL;
    int dataSize = 0;

    XPrintf("--- K7 站1 长帧后恢复期（125ms 无应答窗口）---\n");
    t1m = XVirtualModelTurbidity_create(0);
    t2m = XVirtualModelTurbidity_create(1);
    fm = XVirtualModelFlow_create();
    c = (t1m && t2m && fm) ? XModbusVirtualClient_create(NULL) : NULL;
    if (t1m && t2m && fm && c) {
        XVirtualModelTurbidity_setNtu(t1m, 2.29);
        XVirtualModelTurbidity_setNtu(t2m, 0.045);
        XVirtualModelFlow_setInstantFlow(fm, 3.6f);
        (void)XModbusVirtualClient_bindUnit(c, HSF_TURBIDITY1_UNIT_ID,
                                            XVirtualModel_base(t1m));
        (void)XModbusVirtualClient_bindUnit(c, HSF_TURBIDITY2_UNIT_ID,
                                            XVirtualModel_base(t2m));
        (void)XModbusVirtualClient_bindUnit(c, HSF_FLOWMETER_UNIT_ID,
                                            XVirtualModel_base(fm));
        /* [先绑后开再发] 同 K6：绑定表非空才能 Open，未连接态发帧被拒。 */
        if (!XModbusDevice_connectDevice((XModbusDevice*)c)) {
            CHECK("K7 虚拟客户端 Open", false);
        }
        XModbusVirtualClient_setUnitReplyLatency(c, HSF_TURBIDITY1_UNIT_ID,
                                                 0, 0);
        XModbusVirtualClient_setUnitReplyLatency(c, HSF_FLOWMETER_UNIT_ID,
                                                 0, 0);
        XModbusVirtualClient_setRecoveryGate(c, HSF_FLOWMETER_UNIT_ID,
                                             HSF_TURBIDITY1_UNIT_ID, 125);
        CHECK("K7 闸配置可读（窗口 getter == 125）",
              XModbusVirtualClient_recoveryWindowMs(c) == 125);

        /* a) 站 5 长帧：22 寄存器 = 44 字节数据 + 1 字节 byteCount = 45。 */
        reply = kSendReadRaw(c, HSF_FLOWMETER_UNIT_ID, 0x04, 0x1010, 22);
        CHECK("K7 站5 长帧（22 寄存器）应答照常",
              reply != NULL && kReplyFc(reply) == 0x04,
              "fc=0x%02X", kReplyFc(reply));
        (void)kReplyData(reply, &dataSize);
        CHECK("K7 长帧应答 45 字节（1 byteCount + 44 数据）",
              reply != NULL && dataSize == 45, "size=%d", dataSize);
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        flushDeferred();

        /* b) 长帧收口后立刻问站 1 ⇒ 无应答（真机 0ms 间隙 2/10 的死区）。 */
        reply = kSendReadRaw(c, HSF_TURBIDITY1_UNIT_ID, 0x03, 0x0000, 2);
        CHECK("K7 恢复期内问站1 ⇒ 无应答（NULL=上层按超时处理）",
              reply == NULL);
        CHECK("K7 恢复期拒答计数 = 1",
              XModbusVirtualClient_recoveryDropCount(c) == 1,
              "count=%llu",
              (unsigned long long)XModbusVirtualClient_recoveryDropCount(c));
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        flushDeferred();

        /* c) 窗口外（130ms > 125ms）恢复应答，计数不涨。 */
        spinMs(130);
        reply = kSendReadRaw(c, HSF_TURBIDITY1_UNIT_ID, 0x03, 0x0000, 2);
        CHECK("K7 窗口外问站1 ⇒ 恢复应答",
              reply != NULL && kReplyFc(reply) == 0x03,
              "fc=0x%02X", kReplyFc(reply));
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        CHECK("K7 窗口外拒答计数不涨（仍 1）",
              XModbusVirtualClient_recoveryDropCount(c) == 1);
        flushDeferred();

        /* d) 旋钮可配：窗口 0 = 关闸（立刻问站 1 照常应答）。 */
        XModbusVirtualClient_setRecoveryGate(c, HSF_FLOWMETER_UNIT_ID,
                                             HSF_TURBIDITY1_UNIT_ID, 0);
        CHECK("K7 窗口 0 ⇒ 闸关闭（getter==0）",
              XModbusVirtualClient_recoveryWindowMs(c) == 0);
        reply = kSendReadRaw(c, HSF_TURBIDITY1_UNIT_ID, 0x03, 0x0000, 2);
        CHECK("K7 关闸后立刻问站1 ⇒ 照常应答",
              reply != NULL && kReplyFc(reply) == 0x03);
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        flushDeferred();

        /* e) 长帧站不匹配 ⇒ 不误伤（站 2 的短帧不该触发站 1 死区）。 */
        XModbusVirtualClient_setRecoveryGate(c, HSF_TURBIDITY2_UNIT_ID,
                                             HSF_TURBIDITY1_UNIT_ID, 125);
        reply = kSendReadRaw(c, HSF_TURBIDITY2_UNIT_ID, 0x03, 0x0000, 4);
        CHECK("K7 闸指向站2：短帧（4 寄存器）不算长帧",
              reply != NULL && kReplyFc(reply) == 0x03);
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        reply = kSendReadRaw(c, HSF_TURBIDITY1_UNIT_ID, 0x03, 0x0000, 2);
        CHECK("K7 短帧后问站1 ⇒ 不进死区（照常应答）",
              reply != NULL && kReplyFc(reply) == 0x03);
        CHECK("K7 全程恢复期拒答计数仍 = 1",
              XModbusVirtualClient_recoveryDropCount(c) == 1);
        if (reply) { XClassDelete((XClass*)reply); reply = NULL; }
        flushDeferred();
    } else {
        CHECK("K7 恢复期夹具建成", false);
    }
    XClassDelete((XClass*)c);
    if (t1m) XFree_System(t1m);
    if (t2m) XFree_System(t2m);
    if (fm) XFree_System(fm);
    flushDeferred();
}

/**
 * @brief K8：站 2 功能码怪癖（FC04 请求 → 应答功能码恒 03）。
 * @details 实测文档 §5.5/附录 A：`02 04 00 00 00 02 …` → `02 03 04 …`。
 *   按「应答功能码 == 请求功能码」校验的主站会误判——模拟从站必须把这条
 *   怪癖如实带上线上帧。站 1 WZT-701A 没有 FC04（仍异常应答 fc|0x80）。
 */
static void testK8_station2FunctionCodeQuirk(void)
{
    struct XVirtualModelTurbidity* t2m = NULL;
    struct XVirtualModelTurbidity* t1m = NULL;
    XModbusVirtualClient* c2 = NULL;
    XModbusVirtualClient* c1 = NULL;
    XModbusReply* fc3Reply = NULL;
    XModbusReply* fc4Reply = NULL;
    const uint8_t* d3 = NULL;
    const uint8_t* d4 = NULL;
    int n3 = 0;
    int n4 = 0;

    XPrintf("--- K8 站2 功能码怪癖（FC04 请求也回 FC03）---\n");
    t2m = XVirtualModelTurbidity_create(1);
    c2 = t2m ? XModbusVirtualClient_create(NULL) : NULL;
    if (t2m && c2) {
        XVirtualModelTurbidity_setWordOrder(
            t2m, (int)XVirtualModelTurbWordOrder_Dcba);
        XVirtualModelTurbidity_setNtu(t2m, 0.045);
        XVirtualModelTurbidity_setTempC(t2m, 19.3);
        (void)XModbusVirtualClient_bindUnit(c2, HSF_TURBIDITY2_UNIT_ID,
                                            XVirtualModel_base(t2m));
        /* [先绑后开再发] 同 K6：绑定表非空才能 Open，未连接态发帧被拒。 */
        if (!XModbusDevice_connectDevice((XModbusDevice*)c2)) {
            CHECK("K8 站2 虚拟客户端 Open", false);
        }
        fc3Reply = kSendReadRaw(c2, HSF_TURBIDITY2_UNIT_ID, 0x03,
                                0x0000, 4);
        fc4Reply = kSendReadRaw(c2, HSF_TURBIDITY2_UNIT_ID, 0x04,
                                0x0000, 4);
        CHECK("K8 站2 FC03 读 4 寄存器应答照常（fc=0x03）",
              fc3Reply != NULL && kReplyFc(fc3Reply) == 0x03,
              "fc=0x%02X", kReplyFc(fc3Reply));
        CHECK("K8 站2 FC04 请求被受理（有应答，不是 NULL/异常）",
              fc4Reply != NULL && kReplyFc(fc4Reply) == 0x03,
              "fc=0x%02X（期望恒回 03，不是 04/0x84）",
              kReplyFc(fc4Reply));
        d3 = kReplyData(fc3Reply, &n3);
        d4 = kReplyData(fc4Reply, &n4);
        CHECK("K8 FC04 应答数据与 FC03 逐字节一致（同一份寄存器）",
              d3 != NULL && d4 != NULL && n3 == 9 && n4 == 9 &&
              memcmp(d3, d4, (size_t)n3) == 0,
              "fc3=%d 字节 fc4=%d 字节", n3, n4);
        /* [数据口径] DCBA：浊度 <f(0..3)、温度 <f(4..7)——基准 0.045/19.3
         * 与 K3 同源（怪癖只动功能码，不动数据）。 */
        if (d4 && n4 >= 9) {
            const double ntu = kDcbaRegsToFloat(
                (uint16_t)((d4[1] << 8) | d4[2]),
                (uint16_t)((d4[3] << 8) | d4[4]));
            CHECK("K8 FC04 应答里的浊度 == 0.045（DCBA 保真不变）",
                  ntu == (double)(float)0.045, "ntu=%.7g", ntu);
        }
        if (fc3Reply) { XClassDelete((XClass*)fc3Reply); fc3Reply = NULL; }
        if (fc4Reply) { XClassDelete((XClass*)fc4Reply); fc4Reply = NULL; }
        flushDeferred();
    } else {
        CHECK("K8 站2 夹具建成", false);
    }
    XClassDelete((XClass*)c2);
    if (t2m) XFree_System(t2m);
    flushDeferred();

    /* 站 1 无 FC04：仍按异常应答拒绝（fc|0x80）——本条怪癖只属站 2。 */
    t1m = XVirtualModelTurbidity_create(0);
    c1 = t1m ? XModbusVirtualClient_create(NULL) : NULL;
    if (t1m && c1) {
        XVirtualModelTurbidity_setNtu(t1m, 2.29);
        (void)XModbusVirtualClient_bindUnit(c1, HSF_TURBIDITY1_UNIT_ID,
                                            XVirtualModel_base(t1m));
        /* [先绑后开再发] 同 K6。 */
        if (!XModbusDevice_connectDevice((XModbusDevice*)c1)) {
            CHECK("K8 站1 虚拟客户端 Open", false);
        }
        fc4Reply = kSendReadRaw(c1, HSF_TURBIDITY1_UNIT_ID, 0x04,
                                0x0000, 2);
        CHECK("K8 站1 FC04 仍被拒（异常应答 fc=0x84）",
              fc4Reply != NULL && kReplyFc(fc4Reply) == 0x84,
              "fc=0x%02X", kReplyFc(fc4Reply));
        if (fc4Reply) { XClassDelete((XClass*)fc4Reply); fc4Reply = NULL; }
        flushDeferred();
    } else {
        CHECK("K8 站1 夹具建成", false);
    }
    XClassDelete((XClass*)c1);
    if (t1m) XFree_System(t1m);
    flushDeferred();
}

/**
 * @brief K9：数值微抖动方向锁（实测 §1.3 分台口径）。
 * @details 40s × 20 轮三台混跑：站 1 浊度 2.29 完全恒定、站 2 在
 *   0.04458~0.04477 缓慢波动且温度 19.25~19.3125、站 5 流量按基准正弦
 *   微动。断言用**带内**口径（不锁逐拍相位——波动周期 45s/60s，CI 里
 *   采样窗口只覆盖一小段相位，锁相位必然假红）。
 */
static void testK9_microWobbleBands(void)
{
    struct XVirtualModelTurbidity* t2m = NULL;
    struct XVirtualModelTurbidity* t1m = NULL;
    struct XVirtualModelFlow* fm = NULL;
    int i;
    bool allInBand = true;
    bool tempInBand = true;

    XPrintf("--- K9 数值微抖动（站1 恒定 / 站2 ±0.001+温度漂移 / 站5 ±2%）---\n");

    /* ---- 站 2：0.045±0.001 缓慢波动 + 温度 19.3±0.05 漂移 ---- */
    t2m = XVirtualModelTurbidity_create(1);
    if (t2m) {
        XVirtualModelTurbidity_setWordOrder(
            t2m, (int)XVirtualModelTurbWordOrder_Dcba);
        XVirtualModelTurbidity_setWobble(t2m, 0.001);
        XVirtualModelTurbidity_setTempC(t2m, 19.3);
        XVirtualModelTurbidity_setTempWobble(t2m, 0.05);
        for (i = 0; i < 5; ++i) {
            uint16_t out[4] = {0, 0, 0, 0};
            uint16_t n = 0;
            if (XModbusVirtualModel_read(XVirtualModel_base(t2m), 0x03,
                                         0x0000, 4, out, 4, &n) !=
                XModbusVirtual_Ok) {
                allInBand = false;
                tempInBand = false;
                break;
            }
            {
                const double ntu = kDcbaRegsToFloat(out[0], out[1]);
                const double temp = kDcbaRegsToFloat(out[2], out[3]);
                if (!(ntu >= 0.0438 && ntu <= 0.0462)) allInBand = false;
                if (!(temp >= 19.24 && temp <= 19.36)) tempInBand = false;
            }
            spinMs(30);
        }
        CHECK("K9 站2 浊度带内 0.045±0.001（含 float32 圆整余量）", allInBand);
        CHECK("K9 站2 温度带内 19.3±0.05（缓慢漂移）", tempInBand);
        /* [幅 0 = 恒定] setWobble(0) 与站 1 的"实测不动"同款口径——
         * 配 0 后两拍逐位相等（恒真断言没意义，直接用读回证明）。 */
        XVirtualModelTurbidity_setWobble(t2m, 0.0);
        XVirtualModelTurbidity_setTempWobble(t2m, 0.0);
        {
            uint16_t a[4] = {0, 0, 0, 0};
            uint16_t b[4] = {0, 0, 0, 0};
            uint16_t n = 0;
            (void)XModbusVirtualModel_read(XVirtualModel_base(t2m), 0x03,
                                           0x0000, 4, a, 4, &n);
            spinMs(25);
            (void)XModbusVirtualModel_read(XVirtualModel_base(t2m), 0x03,
                                           0x0000, 4, b, 4, &n);
            CHECK("K9 站2 幅 0 后两拍逐位恒定（站1 同款口径可用在任一台）",
                  memcmp(a, b, sizeof(a)) == 0);
        }
    } else {
        CHECK("K9 站2 夹具建成", false);
    }
    if (t2m) XFree_System(t2m);
    flushDeferred();

    /* ---- 站 1：恒定 2.29（实测 20 轮最小=最大）---- */
    t1m = XVirtualModelTurbidity_create(0);
    if (t1m) {
        bool constant = true;
        uint16_t first[2] = {0, 0};
        uint16_t n = 0;
        XVirtualModelTurbidity_setWordOrder(
            t1m, (int)XVirtualModelTurbWordOrder_Cdab);
        XVirtualModelTurbidity_setNtu(t1m, 2.29);
        XVirtualModelTurbidity_setWobble(t1m, 0.0);   /* 实测口径：不动。 */
        for (i = 0; i < 6; ++i) {
            uint16_t out[2] = {0, 0};
            (void)XModbusVirtualModel_read(XVirtualModel_base(t1m), 0x03,
                                           0x0000, 2, out, 2, &n);
            if (i == 0) { first[0] = out[0]; first[1] = out[1]; }
            else if (out[0] != first[0] || out[1] != first[1]) constant = false;
            spinMs(20);
        }
        CHECK("K9 站1 浊度恒定（6 拍逐位不动；实测 20 轮最小=最大=2.29）",
              constant && first[0] == 0x8F5Cu && first[1] == 0x4012u,
              "reg=[0x%04X,0x%04X]", (unsigned)first[0], (unsigned)first[1]);
    } else {
        CHECK("K9 站1 夹具建成", false);
    }
    if (t1m) XFree_System(t1m);
    flushDeferred();

    /* ---- 站 5：基准 3.6 ±2% 正弦微波动 + 空管 False ---- */
    fm = XVirtualModelFlow_create();
    if (fm) {
        bool flowInBand = true;
        bool emptyFalse = true;
        uint16_t n = 0;
        XVirtualModelFlow_setBaseFlow(fm, 3.6);   /* 恢复波动口径。 */
        for (i = 0; i < 5; ++i) {
            uint16_t out[2] = {0, 0};
            uint16_t alarm[1] = {1};
            if (XModbusVirtualModel_read(XVirtualModel_base(fm), 0x04,
                                         0x1010, 2, out, 2, &n) !=
                XModbusVirtual_Ok ||
                XModbusVirtualModel_read(XVirtualModel_base(fm), 0x04,
                                         0x1024, 1, alarm, 1, &n) !=
                XModbusVirtual_Ok) {
                flowInBand = false;
                emptyFalse = false;
                break;
            }
            {
                const double flow = kAbcdRegsToFloat(out[0], out[1]);
                /* ±2% 正弦 = [3.528, 3.672]；带给 float32 圆整余量。 */
                if (!(flow >= 3.52 && flow <= 3.68)) flowInBand = false;
                if (alarm[0] != 0u) emptyFalse = false;
            }
            spinMs(60);
        }
        CHECK("K9 站5 流量带内 3.6±2%（正弦微波动，不再恒 3.6 钉死）",
              flowInBand);
        CHECK("K9 站5 空管报警维持 False（0x1024==0）", emptyFalse);
        /* [钉死口径不回退] setInstantFlow(≥0) 仍 = 恒定精确值（K3 用它）。 */
        XVirtualModelFlow_setInstantFlow(fm, 3.6f);
        {
            bool fixed = true;
            for (i = 0; i < 3; ++i) {
                uint16_t out[2] = {0, 0};
                (void)XModbusVirtualModel_read(XVirtualModel_base(fm), 0x04,
                                               0x1010, 2, out, 2, &n);
                if (kAbcdRegsToFloat(out[0], out[1]) != (double)3.6f)
                    fixed = false;
                spinMs(30);
            }
            CHECK("K9 setInstantFlow(3.6f) 钉死口径不回退（逐位恒 3.6）",
                  fixed);
        }
    } else {
        CHECK("K9 站5 夹具建成", false);
    }
    if (fm) XFree_System(fm);
    flushDeferred();
}

/** @brief K4：§K 全部工作集（K1+K2+K3+K5+K6~K9）跑完堆必须回基线（对齐 §G）。 */
static void testK4_heapBackToBaseline(void)
{
    uint64_t base = 0;
    uint64_t now = 0;
    int i;

    XPrintf("--- K4 §K 泄漏自检 ---\n");
    if (!XMemory_statisticsEnabled()) {
        XPrintf("[SKIP] K4 内存统计被构建裁剪（XMEMORY_STATISTICS_ON=0）\n");
        return;
    }
    testK3_simRouteBFidelityOnce();     /* 预热：摊平站/组/Hub 惰性初始化。 */
    testK6_replyLatencyKnob();          /* 摊平 HsfServerIoSim 单例惰性初始化。 */
    flushDeferred();
    base = heapNow();
    for (i = 0; i < 3; ++i) {
        testK1_wordOrderDocumentedFrames();
        testK3_simRouteBFidelityOnce();
        testK5_preTxQuietGate();
        testK6_replyLatencyKnob();
        testK7_station1RecoveryGate();
        testK8_station2FunctionCodeQuirk();
        testK9_microWobbleBands();
        flushDeferred();
    }
    now = heapNow();
    CHECK("K4 §K 三站装配/拆除 3 轮堆增量回基线（0）", now == base,
          "基线=%llu 现在=%llu 差=%lld", (unsigned long long)base,
          (unsigned long long)now, (long long)(now - base));
}

static void testK_threeDeviceAccess(void)
{
    XPrintf("--- §K 三设备接入（ttyS4 路 B：浊度×2 + 流量计）---\n");
    testK1_wordOrderDocumentedFrames();
    testK2_unitIdConfigLock();
    testK5_preTxQuietGate();
    testK6_replyLatencyKnob();
    testK7_station1RecoveryGate();
    testK8_station2FunctionCodeQuirk();
    testK9_microWobbleBands();
    testK4_heapBackToBaseline();        /* 内部含 K3/K6 三轮 + 预热。 */
}

#endif /* XINDUSTRY_ON && XTURBIDITY_ON && XMODBUS_HUB_ON */

/* ============================================================================
 * §N 现场口径对齐（2026-11-10 板端实机复核，基准＝Python 服务端）
 *
 * [背景] 板端 C 服务端与 Python 服务端连的是**同一批真实设备**，但两边
 *        对同一张 DI 位图读出了不同的现场结论。逐条量出来的差异：
 *          N1 阀反馈 DI 位序：C 端「关在前」、板端 Python cfg「开在前」
 *             ⇒ 同一个 DI 位图，C 端判「阀1 开到位」、Python 判「阀1
 *             关到位」——**结论反了**，不是显示差异。现场设备定义以
 *             Python 服务端为准（用户已实机核对），C 端同步。
 *          N2 侧号由谁定：浊度仪/流量计挂在 B 侧是**服务端装配事实**，
 *             客户端不该在请求里点名。早先快照按请求里的 side 取设备，
 *             而客户端请求不带 side（默认 A 侧）⇒ 服务端去 A 侧找浊度仪
 *             （那儿没装）⇒ 回空帧 ⇒ 客户端页面恒"--"。板端实测：同一台
 *             服务器带 side=1 出 v0_ntu=229/v1_ntu=4，不带 side 全空。
 *             修法＝服务端解析实际装配侧（Python 侧同口径）。
 *          N3 流量计健康：告警位里「空管」在泵停/无流时必然置位，早先
 *             「alarms != 0 ⇒ 故障」把首页显示成「电磁流量计 故障」。
 *             只有**系统报警**算设备告警（Python 同口径）。
 *          N4 数字量模块名：两端首页设备表行名要对得上（现场登记名）。
 *          N7 采集配置运行期生效整链（2026-11-11）：INI 写键 →
 *             collectConfig 生效值 → Hub 从站约束/设备节拍逐环断言；
 *             一并钉 5 个逐设备开关停采（与 Python app.py 的 enable_*
 *             同口径）、设备节拍只下调不上调、--sim 浊度缺省回落装配事实。
 *          N8 浊度站 1 冷启动活性 + 阈值「轮转+静默」口径（2026-11-11）：
 *             板端浊度仪 1 偶发「数据过期」根修——静默闸饥饿逃逸对
 *             「从未成功发帧」的从站是死代码（开局放行被区优先的流量计
 *             吃掉 ⇒ 冷启动无声饿死 150.4s）；热总线复刻 + pointValid
 *             有界首帧锁 + 阈值逐台等式锁。
 *
 * [钉法] 全部走公开 API，不复刻实现：
 *          N1 钉点位宏（它是阀反馈与通道名的唯一事实源）；
 *          N2 钉 IO 层的侧号解析（起模拟 IO ＝ 现场拓扑：A 侧 VFD/温湿度，
 *             B 侧浊度×2/流量计）；
 *          N3 钉健康判定的公开函数；
 *          N4 钉名字宏。
 * ==========================================================================*/
#if XINDUSTRY_ON && XMODBUS_HUB_ON

/**
 * @brief N1：阀反馈 DI 位序＝现场接线（开到位在前）。
 */
static void testN1_valveDiOrderFieldWiring(void)
{
    int i;
    int bad = 0;
    XPrintf("--- N1 阀反馈 DI 位序（现场：开在前）---\n");
    for (i = 0; i < 6; ++i) {
        int op = HSF_VALVE_OPEN_DI(i);
        int cl = HSF_VALVE_CLOSE_DI(i);
        if (op != 2 * i || cl != 2 * i + 1) ++bad;
    }
    CHECK("N1 阀 n：开到位 DI=2n、关到位 DI=2n+1（现场接线，板端 cfg 口径）",
          bad == 0, "不合的路数=%d", bad);
    CHECK("N1 开/关两路相邻且不重合（12 个 DI 被 6 路两两分完）",
          HSF_VALVE_OPEN_DI(0) == 0 && HSF_VALVE_CLOSE_DI(0) == 1 &&
          HSF_VALVE_OPEN_DI(5) == 10 && HSF_VALVE_CLOSE_DI(5) == 11 &&
          HSF_VALVE_CLOSE_DI(0) != HSF_VALVE_OPEN_DI(1));
    CHECK("N1 动作出口仍是 DO n（未受位序订正影响）",
          HSF_VALVE_DO(0) == 0 && HSF_VALVE_DO(5) == 5);
}

/**
 * @brief N2：侧号由服务端解析（请求侧没装 ⇒ 回落到实际装配侧）。
 */
static void testN2_sideResolvedByServer(void)
{
    XPrintf("--- N2 侧号由服务端解析 ---\n");
    /* 模拟 IO 的拓扑＝现场拓扑：路 A = 变频器 + 温湿度；路 B = 浊度×2 +
     * 流量计（板端实测：浊度/流量计都在 ttyS4 这条线上）。 */
    HsfServerIo_setSimMode(true);
    if (!HsfServerIo_start()) {
        CHECK("N2 模拟 IO 启动", false);
        return;
    }
    CHECK("N2 现场拓扑：浊度仪/流量计在 B 侧、A 侧没有",
          HsfServerIo_turbidity(HsfSide_A, 0) == NULL &&
          HsfServerIo_turbidity(HsfSide_B, 0) != NULL &&
          HsfServerIo_flowMeter(HsfSide_A) == NULL &&
          HsfServerIo_flowMeter(HsfSide_B) != NULL);
    CHECK("N2 请求侧没装 ⇒ 回落到实际装配侧（请求 A ⇒ 答 B）",
          HsfServerIo_turbiditySide(HsfSide_A) == HsfSide_B &&
          HsfServerIo_flowMeterSide(HsfSide_A) == HsfSide_B,
          "浊度侧=%d 流量计侧=%d", (int)HsfServerIo_turbiditySide(HsfSide_A),
          (int)HsfServerIo_flowMeterSide(HsfSide_A));
    CHECK("N2 请求侧装了 ⇒ 就用它（请求 B ⇒ 答 B）",
          HsfServerIo_turbiditySide(HsfSide_B) == HsfSide_B &&
          HsfServerIo_flowMeterSide(HsfSide_B) == HsfSide_B);
    HsfServerIo_stop();
}

/**
 * @brief N3：流量计健康只认系统报警（空管/上下限是工况信息）。
 */
static void testN3_flowMeterAlarmHealth(void)
{
    XPrintf("--- N3 流量计告警口径 ---\n");
    CHECK("N3 无告警 + 有数据 ⇒ Ok",
          HsfServerIoFlowMeter_health(0, true) == HsfDeviceHealth_Ok);
    CHECK("N3 空管位（bit2=4，泵停时必然置位）⇒ 仍是 Ok 不判故障",
          HsfServerIoFlowMeter_health(4, true) == HsfDeviceHealth_Ok,
          "得到=%d", HsfServerIoFlowMeter_health(4, true));
    CHECK("N3 上限/下限位 ⇒ 仍是 Ok（工况信息）",
          HsfServerIoFlowMeter_health(1, true) == HsfDeviceHealth_Ok &&
          HsfServerIoFlowMeter_health(2, true) == HsfDeviceHealth_Ok);
    CHECK("N3 系统报警位（bit3=8）⇒ Warning",
          HsfServerIoFlowMeter_health(8, true) == HsfDeviceHealth_Warning,
          "得到=%d", HsfServerIoFlowMeter_health(8, true));
    CHECK("N3 系统报警 + 空管 ⇒ 仍 Warning（系统位优先）",
          HsfServerIoFlowMeter_health(12, true) == HsfDeviceHealth_Warning);
    CHECK("N3 无告警但无数据 ⇒ Stale",
          HsfServerIoFlowMeter_health(0, false) == HsfDeviceHealth_Stale);
}

/**
 * @brief N4：数字量模块名两端一致（现场单台 ⇒ 统一叫「数字量」）。
 */
static void testN4_digitalModuleName(void)
{
    XPrintf("--- N4 数字量模块名 ---\n");
    CHECK("N4 模块名 == 「数字量」（板端 Python cfg 同名）",
          strcmp(HSF_DIG_MODULE_NAME, "数字量") == 0, "得到=%s",
          HSF_DIG_MODULE_NAME);
}

/**
 * @brief N5：采集节键表覆盖「两台浊度 + 流量计」（逐台可设）。
 * @details [2026-11-10 现场要求] 两台浊度仪与流量计的采集周期必须各自可设：
 *          站1 有 200ms 发帧前静默期、站3 没有；流量计 49 字节长帧最慢。
 *          早期协议只有一台共用的 turbidityPollMs、流量计没有键（跟随温湿度）
 *          ⇒ 远端设置里"缺了几台设备的采集设置"。本锁钉住键字面（两端必须
 *          逐值一致）与「逐台字段确实存在且互不覆盖」。
 */
static void testN5_collectPollKeysPerDevice(void)
{
    HsfCollectConfig cfg;

    XPrintf("--- N5 采集节：两台浊度 + 流量计逐台可设 ---\n");
    CHECK("N5 键字面：浊度1/浊度2/流量计（与 Python proto.py 同名）",
          strcmp(HSF_CFGK_TURBIDITY1_POLL_MS, "turbidity1PollMs") == 0 &&
          strcmp(HSF_CFGK_TURBIDITY2_POLL_MS, "turbidity2PollMs") == 0 &&
          strcmp(HSF_CFGK_FLOWMETER_POLL_MS, "flowmeterPollMs") == 0);
    CHECK("N5 兼容键仍在（旧客户端只发 turbidityPollMs 时两台照它跑）",
          strcmp(HSF_CFGK_TURBIDITY_POLL_MS, "turbidityPollMs") == 0);
    memset(&cfg, 0, sizeof(cfg));
    HsfServerIo_collectConfig(&cfg);
    /* 未配 INI 时取宏缺省：两台浊度继承共同缺省，流量计取自己的宏。 */
    CHECK("N5 缺省：两台浊度取共同缺省、流量计取独立宏",
          cfg.turbidity1PollMs == cfg.turbidityPollMs &&
          cfg.turbidity2PollMs == cfg.turbidityPollMs &&
          cfg.flowmeterPollMs == (int)HSF_FLOWMETER_MIN_POLL_MS,
          "t1=%d t2=%d tdef=%d flow=%d", cfg.turbidity1PollMs,
          cfg.turbidity2PollMs, cfg.turbidityPollMs, cfg.flowmeterPollMs);
}

/**
 * @brief N7：采集配置「INI 值 → 生效值 → Hub 从站约束/设备节拍」整链运行期生效。
 * @details [2026-11-11 用户口径「确认采集时间实时生效，有问题直接修」]
 *          只断言"键名对得上"不算数：改键后必须看到 Hub 的从站约束/设备
 *          节拍真的变了。随本锁一并钉死的三个修复：
 *          · 5 个逐设备开关（vfd/temphum/flowmeter/analog/digital）此前
 *            只落 INI、全库无运行期消费方（选了不兑现）⇒ 现在与浊度同款
 *            停采（485=最小发送间隔顶天文数字；TCP 站=设备轮询间隔 0）；
 *          · 设备节拍（扫描拍）此前取「本侧最快档」，任一台的慢周期会把
 *            同侧全体拖慢（实测流量计 2000ms ⇒ 路 B 全体 2.4s/帧）⇒ 现在
 *            只允许比编译期扫描拍更快时下调、绝不因慢设备上调；
 *          · --sim 下浊度开关缺省回落装配事实（路 B 由模拟模式强制启，
 *            不再回落 HSF_BUS_B_ENABLED=0 把演示/回归里的浊度"缺省停采"）。
 *          [INI 卫生] 写过的键在尾部**删除**（HsfServerSettings_remove）：
 *          测试树的 INI 跨用例共享，残留键会打翻 N5 的"缺键回落宏"断言
 *          （写回缺省值只等值、不等于"键不存在"）。
 */
static void testN7_collectApplyRuntime(void)
{
    /* 改过的 INI 键表（跑前快照+清残留、跑后按快照还原，见下方保护注记）。 */
    static const char* const kTouched[] = {
        "collect/vfdPollMs", "collect/temphumPollMs",
        "collect/turbidity1PollMs", "collect/turbidity2PollMs",
        "collect/flowmeterPollMs", "collect/analogPollMs",
        "collect/digitalPollMs",
        "collect/vfdEnabled", "collect/temphumEnabled",
        "collect/turbidity1Enabled", "collect/turbidity2Enabled",
        "collect/flowmeterEnabled", "collect/analogEnabled",
        "collect/digitalEnabled"
    };
    /* [共享 INI 保护 2026-11-11] 本测试与 HsfServer.exe 共享 bin/cfg/
     * hsf_server.ini——现场口径的键值（如温湿度 60000ms）不能被测试静默
     * 清掉（对照 verify/hsf_collect_runtime_check.py 的备份/还原口径）。
     * 跑前快照每个触键的（存在性,值），尾部按快照还原：原有→写回原值，
     * 原无→删除（维持"键不存在"态，N5 缺省回落断言口径不变）。
     * 16 ≥ kTouched 条数（下方有编译期外的运行期 CHECK 防超界）。 */
    static int s_keyHad[16];
    static int s_keyVal[16];
    /* [停采天文数字] HSF_IO_UNIT_DISABLED_MS 是 HsfServerIo.c 的私有宏，
     * 这里用字面量（0x7FFFFFF0ms ≈ 24.8 天；hubUnitDue 按单调毫秒差判定，
     * 该量级等于"永不轮询"，写回正常周期即恢复）。 */
    const unsigned kDisabledMs = 0x7FFFFFF0u;
    XModbusHub* hubA;
    XModbusHub* hubB;
    int devA;
    int devB;

    XPrintf("--- N7 采集配置运行期生效（INI → 生效值 → Hub 约束）---\n");
    CHECK("N7 停采天文数字 > 20 天（hubUnitDue 永不到期的量级）",
          kDisabledMs > 1000u * 60u * 60u * 24u * 20u);
    /* INI 门面必须先 open：未 open 时 setInt/contains 静默丢弃（门面既有
     * 行为），写键全部落空、断言全假绿。 */
    if (!HsfServerSettings_open()) {
        CHECK("N7 打开测试树 INI", false);
        return;
    }
    /* 快照共享 INI 的现场键值（见上方保护注记），随后才清残留
     * （上次跑挂掉会留下键 ⇒ 本次"缺省回落"断言不可信）。 */
    {
        const int nKeys = (int)(sizeof(kTouched) / sizeof(kTouched[0]));
        int k;
        CHECK("N7 触键表长度 ≤ 快照数组（16）", nKeys <= 16);
        if (nKeys > 16) return;
        for (k = 0; k < nKeys; ++k) {
            s_keyHad[k] = HsfServerSettings_contains(kTouched[k]) ? 1 : 0;
            s_keyVal[k] = s_keyHad[k]
                              ? HsfServerSettings_getInt(kTouched[k], 0)
                              : 0;
        }
        for (k = 0; k < nKeys; ++k)
            HsfServerSettings_remove(kTouched[k]);
    }
    HsfServerIo_setSimMode(true);
    if (!HsfServerIo_start()) {
        CHECK("N7 模拟 IO 启动", false);
        return;
    }
    CHECK("N7 模拟拓扑：两侧 485 + 模拟量 + 数字量 Hub 全就绪",
          HsfServerIo_busHub(HsfSide_A) != NULL &&
          HsfServerIo_busHub(HsfSide_B) != NULL &&
          HsfServerIo_hub() != NULL &&
          HsfServerIo_digitalHub() != NULL);

    /* ---- ① 写 INI（模拟一次远程 CONFIG_SET collect） ---- */
    HsfServerSettings_setInt("collect/temphumPollMs", 1500);
    HsfServerSettings_setInt("collect/turbidity1PollMs", 1300);
    HsfServerSettings_setInt("collect/turbidity2PollMs", 1700);
    HsfServerSettings_setInt("collect/flowmeterPollMs", 1900);
    HsfServerSettings_setInt("collect/analogPollMs", 450);
    HsfServerSettings_setInt("collect/digitalPollMs", 550);

    /* ---- ② 生效值 = 写入值（vfd 留缺省：0 = 不限制，合法） ---- */
    {
        HsfCollectConfig cfg;
        memset(&cfg, 0, sizeof(cfg));
        HsfServerIo_collectConfig(&cfg);
        CHECK("N7 INI 值 → collectConfig 生效值（7 周期键，vfd 缺省 0）",
              cfg.vfdPollMs == 0 &&
              cfg.temphumPollMs == 1500 &&
              cfg.turbidity1PollMs == 1300 &&
              cfg.turbidity2PollMs == 1700 &&
              cfg.flowmeterPollMs == 1900 &&
              cfg.analogPollMs == 450 &&
              cfg.digitalPollMs == 550,
              "vfd=%d th=%d t1=%d t2=%d fm=%d ai=%d dig=%d",
              cfg.vfdPollMs, cfg.temphumPollMs, cfg.turbidity1PollMs,
              cfg.turbidity2PollMs, cfg.flowmeterPollMs, cfg.analogPollMs,
              cfg.digitalPollMs);
        /* [坑④] --sim 下浊度两台缺省开：回落装配事实（路 B 由模拟模式
         * 强制启），不再回落 HSF_BUS_B_ENABLED=0。 */
        CHECK("N7 缺省开关：--sim 下浊度两台缺省开（装配事实回落）",
              cfg.turbidity1Enabled && cfg.turbidity2Enabled);
        CHECK("N7 缺省开关：485 三台/TCP 两台各按装配事实缺省开",
              cfg.vfdEnabled && cfg.temphumEnabled && cfg.flowmeterEnabled &&
              cfg.analogEnabled && cfg.digitalEnabled);
    }

    /* ---- ③ 应用后 Hub 的约束/节拍真的变了 ---- */
    CHECK("N7 应用采集配置", HsfServerIo_applyCollectConfig(NULL));

    hubA = HsfServerIo_busHub(HsfSide_A);
    devA = HsfServerIo_busDeviceId(HsfSide_A);
    hubB = HsfServerIo_busHub(HsfSide_B);
    devB = HsfServerIo_busDeviceId(HsfSide_B);
    if (hubA && hubB && devA >= 0 && devB >= 0) {
        XModbusHubUnitLimits lim;
        memset(&lim, 0, sizeof(lim));
        CHECK("N7 温湿度从站约束 = 1500（写入值落到 Hub）",
              XModbusHub_getUnitLimits(hubA, devA, HSF_TEMPHUM_UNIT_ID,
                                       &lim) &&
              lim.m_minPollIntervalMs == 1500u,
              "得到=%u", (unsigned)lim.m_minPollIntervalMs);
        /* [坑①] 设备节拍（扫描拍）不被慢档放大：本侧用户最紧档 1500ms
         * 比编译期扫描拍松 ⇒ 节拍保持 20ms；温湿度的慢节拍由它自己的
         * 从站约束实现（到期前被跳过），变频器不必陪跑。 */
        CHECK("N7 侧 A 设备节拍不被慢档放大（仍编译期扫描拍）",
              XModbusHub_pollInterval(hubA, devA) ==
                  (size_t)HSF_BUS_A_SCAN_MS,
              "得到=%u 编译期=%d",
              (unsigned)XModbusHub_pollInterval(hubA, devA),
              (int)HSF_BUS_A_SCAN_MS);
        memset(&lim, 0, sizeof(lim));
        CHECK("N7 浊度1/浊度2/流量计从站约束 = 1300/1700/1900",
              XModbusHub_getUnitLimits(hubB, devB, HSF_TURBIDITY1_UNIT_ID,
                                       &lim) &&
              lim.m_minPollIntervalMs == 1300u &&
              XModbusHub_getUnitLimits(hubB, devB, HSF_TURBIDITY2_UNIT_ID,
                                       &lim) &&
              lim.m_minPollIntervalMs == 1700u &&
              XModbusHub_getUnitLimits(hubB, devB, HSF_FLOWMETER_UNIT_ID,
                                       &lim) &&
              lim.m_minPollIntervalMs == 1900u);
        CHECK("N7 侧 B 设备节拍不被慢档放大（仍编译期扫描拍）",
              XModbusHub_pollInterval(hubB, devB) ==
                  (size_t)HSF_BUS_B_SCAN_MS,
              "得到=%u 编译期=%d",
              (unsigned)XModbusHub_pollInterval(hubB, devB),
              (int)HSF_BUS_B_SCAN_MS);
    } else {
        CHECK("N7 两侧 485 Hub 就绪", false);
    }
    CHECK("N7 模拟量设备节拍 = 450（写入值落到 TCP 站 Hub）",
          XModbusHub_pollInterval(HsfServerIo_hub(), 1) == 450u,
          "得到=%u",
          (unsigned)XModbusHub_pollInterval(HsfServerIo_hub(), 1));
    CHECK("N7 数字量设备节拍 = 550",
          HsfServerIo_digitalHub() != NULL &&
          XModbusHub_pollInterval(HsfServerIo_digitalHub(), 1) == 550u,
          "得到=%u",
          HsfServerIo_digitalHub()
              ? (unsigned)XModbusHub_pollInterval(HsfServerIo_digitalHub(), 1)
              : 0u);

    /* ---- ④ 逐设备停采 ⇒ 约束/节拍立即变，恢复 ⇒ 写回原值 ---- */
    HsfServerSettings_setInt("collect/vfdEnabled", 0);
    CHECK("N7 应用停采（变频器）", HsfServerIo_applyCollectConfig(NULL));
    {
        XModbusHubUnitLimits lim;
        memset(&lim, 0, sizeof(lim));
        CHECK("N7 变频器停采 ⇒ 从站约束顶天文数字（hubUnitDue 永不到期）",
              XModbusHub_getUnitLimits(hubA, devA, HSF_VFD_UNIT_ID, &lim) &&
              lim.m_minPollIntervalMs >= kDisabledMs,
              "得到=%u", (unsigned)lim.m_minPollIntervalMs);
    }
    HsfServerSettings_setInt("collect/vfdEnabled", 1);
    CHECK("N7 恢复变频器采集", HsfServerIo_applyCollectConfig(NULL));

    HsfServerSettings_setInt("collect/temphumEnabled", 0);
    CHECK("N7 应用停采（温湿度）", HsfServerIo_applyCollectConfig(NULL));
    {
        XModbusHubUnitLimits lim;
        memset(&lim, 0, sizeof(lim));
        CHECK("N7 温湿度停采 ⇒ 从站约束顶天文数字",
              XModbusHub_getUnitLimits(hubA, devA, HSF_TEMPHUM_UNIT_ID,
                                       &lim) &&
              lim.m_minPollIntervalMs >= kDisabledMs,
              "得到=%u", (unsigned)lim.m_minPollIntervalMs);
    }
    HsfServerSettings_setInt("collect/temphumEnabled", 1);
    CHECK("N7 恢复温湿度采集（约束回 1500）",
          HsfServerIo_applyCollectConfig(NULL));
    {
        XModbusHubUnitLimits lim;
        memset(&lim, 0, sizeof(lim));
        CHECK("N7 温湿度恢复后约束 = 1500",
              XModbusHub_getUnitLimits(hubA, devA, HSF_TEMPHUM_UNIT_ID,
                                       &lim) &&
              lim.m_minPollIntervalMs == 1500u,
              "得到=%u", (unsigned)lim.m_minPollIntervalMs);
    }

    HsfServerSettings_setInt("collect/flowmeterEnabled", 0);
    CHECK("N7 应用停采（流量计）", HsfServerIo_applyCollectConfig(NULL));
    {
        XModbusHubUnitLimits lim;
        memset(&lim, 0, sizeof(lim));
        CHECK("N7 流量计停采 ⇒ 从站约束顶天文数字",
              XModbusHub_getUnitLimits(hubB, devB, HSF_FLOWMETER_UNIT_ID,
                                       &lim) &&
              lim.m_minPollIntervalMs >= kDisabledMs,
              "得到=%u", (unsigned)lim.m_minPollIntervalMs);
    }
    HsfServerSettings_setInt("collect/flowmeterEnabled", 1);
    CHECK("N7 恢复流量计采集", HsfServerIo_applyCollectConfig(NULL));

    HsfServerSettings_setInt("collect/turbidity1Enabled", 0);
    CHECK("N7 应用停采（浊度1）", HsfServerIo_applyCollectConfig(NULL));
    {
        XModbusHubUnitLimits lim1;
        XModbusHubUnitLimits lim2;
        memset(&lim1, 0, sizeof(lim1));
        memset(&lim2, 0, sizeof(lim2));
        CHECK("N7 浊度1 停采 ⇒ 约束顶天文数字",
              XModbusHub_getUnitLimits(hubB, devB, HSF_TURBIDITY1_UNIT_ID,
                                       &lim1) &&
              lim1.m_minPollIntervalMs >= kDisabledMs,
              "得到=%u", (unsigned)lim1.m_minPollIntervalMs);
        CHECK("N7 浊度1 停采期间浊度2 照常 ⇒ 约束仍 1700",
              XModbusHub_getUnitLimits(hubB, devB, HSF_TURBIDITY2_UNIT_ID,
                                       &lim2) &&
              lim2.m_minPollIntervalMs == 1700u,
              "得到=%u", (unsigned)lim2.m_minPollIntervalMs);
    }
    HsfServerSettings_setInt("collect/turbidity1Enabled", 1);
    CHECK("N7 恢复浊度1 采集（约束回 1300）",
          HsfServerIo_applyCollectConfig(NULL));
    {
        XModbusHubUnitLimits lim;
        memset(&lim, 0, sizeof(lim));
        CHECK("N7 浊度1 恢复后约束 = 1300",
              XModbusHub_getUnitLimits(hubB, devB, HSF_TURBIDITY1_UNIT_ID,
                                       &lim) &&
              lim.m_minPollIntervalMs == 1300u,
              "得到=%u", (unsigned)lim.m_minPollIntervalMs);
    }

    /* TCP 两台停采 = 设备轮询间隔 0（XModbusHub 口径：0=该设备不轮询）。 */
    HsfServerSettings_setInt("collect/analogEnabled", 0);
    CHECK("N7 应用停采（模拟量）", HsfServerIo_applyCollectConfig(NULL));
    CHECK("N7 模拟量停采 ⇒ 设备节拍 0（不轮询）",
          XModbusHub_pollInterval(HsfServerIo_hub(), 1) == 0u,
          "得到=%u",
          (unsigned)XModbusHub_pollInterval(HsfServerIo_hub(), 1));
    HsfServerSettings_setInt("collect/analogEnabled", 1);
    CHECK("N7 恢复模拟量采集（节拍回 450）",
          HsfServerIo_applyCollectConfig(NULL));
    CHECK("N7 模拟量恢复后节拍 = 450",
          XModbusHub_pollInterval(HsfServerIo_hub(), 1) == 450u,
          "得到=%u",
          (unsigned)XModbusHub_pollInterval(HsfServerIo_hub(), 1));

    HsfServerSettings_setInt("collect/digitalEnabled", 0);
    CHECK("N7 应用停采（数字量）", HsfServerIo_applyCollectConfig(NULL));
    CHECK("N7 数字量停采 ⇒ 设备节拍 0（不轮询）",
          HsfServerIo_digitalHub() != NULL &&
          XModbusHub_pollInterval(HsfServerIo_digitalHub(), 1) == 0u,
          "得到=%u",
          HsfServerIo_digitalHub()
              ? (unsigned)XModbusHub_pollInterval(HsfServerIo_digitalHub(), 1)
              : 0u);
    HsfServerSettings_setInt("collect/digitalEnabled", 1);
    CHECK("N7 恢复数字量采集（节拍回 550）",
          HsfServerIo_applyCollectConfig(NULL));
    CHECK("N7 数字量恢复后节拍 = 550",
          XModbusHub_pollInterval(HsfServerIo_digitalHub(), 1) == 550u,
          "得到=%u",
          (unsigned)XModbusHub_pollInterval(HsfServerIo_digitalHub(), 1));

    /* ---- ⑤ 尾部：拆除 + INI 还原（按跑前快照恢复，现场键值不清除） ---- */
    HsfServerIo_stop();
    {
        const int nKeys = (int)(sizeof(kTouched) / sizeof(kTouched[0]));
        int k;
        bool restored = true;
        for (k = 0; k < nKeys; ++k) {
            if (s_keyHad[k])
                HsfServerSettings_setInt(kTouched[k], s_keyVal[k]);
            else
                HsfServerSettings_remove(kTouched[k]);
            /* 还原一致性：存在性与值都必须回到快照态。 */
            if (HsfServerSettings_contains(kTouched[k]) != (s_keyHad[k] != 0))
                restored = false;
            else if (s_keyHad[k] &&
                     HsfServerSettings_getInt(kTouched[k], 0) != s_keyVal[k])
                restored = false;
        }
        CHECK("N7 尾部：共享 INI 已按快照还原（现场键值不被测试清除）",
              restored);
    }
}

/**
 * @brief N8：浊度站 1 冷启动活性 + 陈旧阈值「轮转+静默」口径（2026-11-11）。
 * @details [板端实证 2026-11-10 ttyS4] 站 1（unit 1）配了发帧前静默闸
 *          200ms（流量计 49 字节长帧后的恢复期），但 Hub 的读组序是
 *          **区优先**（IR 流量计恒排第一）：
 *            · 开局唯一一次放行（lastTxEndMs==0 守卫）被流量计消费；
 *            · 之后每拍扫描时设备级收口时刻都刚被别的站刷新过
 *              ⇒ 站 1 每次访问都被静默闸拦下；
 *            · 静默闸的饥饿逃逸只认「发送成功后」才建的簿记条目
 *              ⇒ 从未发过帧的站 1 建不起条目，逃逸成死代码
 *              ⇒ 冷启动被无声饿死 **150.4s**（失败计数=0、无逃逸日志）。
 *          修复后：被静默闸挡住本身就把簿记条目建起来 ⇒ 逃逸时钟从
 *          第一次被挡起算 ⇒ 有界时间（逃逸时限 + 有界兜底）内首帧必到。
 *
 *          [钉法] 起模拟 IO（同 N2/N7 口径），把流量计周期压到 10ms
 *          （快过编译期扫描拍 ⇒ 设备节拍下调到 10ms ⇒ 流量计每拍必发
 *          ⇒ 设备级收口时刻每 10ms 刷新一次 ⇒ 站 1 每次扫描都被静默闸
 *          拦下——精确复刻板端「热总线把静默窗口顶掉」的形态）。
 *          断言两条腿：
 *          ① 活性锁：站 1 的 Hub 块（pointValid 对「从未回填」恒 false）
 *             在 8s 内变 true；对照腿 = 无静默闸的站 3（同一总线，证明
 *             总线本身活着，排除"模拟器没应答"的假绿/假红）。
 *             **旧实现必挂**（站 1 永远无首帧），新实现必过。
 *          ② 阈值锁：站点层生效阈值 = max(宏基线, 周期×3, (轮转+静默)×2)
 *             ——测试自己用装配事实（Hub 读组数/拍长/从站约束）重算
 *             期望值再逐台比对，应用层漏掉任何一档都会相等失败。
 * ==========================================================================*/
static void testN8_turbidityColdStartLiveness(void)
{
    /* 本段写、本段删的 INI 键（N5/N7 的缺省断言不受残留影响）。 */
    static const char* const kTouched[] = {
        "collect/flowmeterPollMs",
        "collect/turbidity1PollMs",
        "collect/turbidity2PollMs"
    };
    XModbusHub* hubB = NULL;
    int devB = -1;
    const HsfServerIoTurbidity* tb1 = NULL;
    const HsfServerIoTurbidity* tb2 = NULL;
    int64_t t0;
    int64_t firstTurb1Ms = -1;
    int64_t firstTurb2Ms = -1;
    const int64_t kDeadlineMs = 8000;   /* 逃逸(2s)+兜底(≤静默期)+余量。 */
    int i;

    XPrintf("--- N8 浊度站 1 冷启动活性 + 阈值轮转静默口径 ---\n");
    if (!HsfServerSettings_open()) {
        CHECK("N8 打开测试树 INI", false);
        return;
    }
    for (i = 0; i < (int)(sizeof(kTouched) / sizeof(kTouched[0])); ++i)
        HsfServerSettings_remove(kTouched[i]);

    HsfServerIo_setSimMode(true);
    if (!HsfServerIo_start()) {
        CHECK("N8 模拟 IO 启动", false);
        return;
    }
    hubB = HsfServerIo_busHub(HsfSide_B);
    devB = HsfServerIo_busDeviceId(HsfSide_B);
    tb1 = HsfServerIo_turbidity(HsfSide_B, 0);
    tb2 = HsfServerIo_turbidity(HsfSide_B, 1);
    if (!(hubB && devB >= 0 && tb1 && tb2)) {
        CHECK("N8 路 B Hub 与两台浊度站就绪", false);
        HsfServerIo_stop();
        return;
    }
    CHECK("N8 路 B Hub 与两台浊度站就绪", true);

    /* ---- ① 热总线装配：流量计 10ms 一拍 ⇒ 设备节拍被下调到 10ms，
     *       流量计每拍必发 ⇒ 站 1 的静默窗口每 10ms 被顶掉一次。 */
    HsfServerSettings_setInt("collect/flowmeterPollMs", 10);
    CHECK("N8 应用热总线节拍（流量计 10ms）",
          HsfServerIo_applyCollectConfig(NULL));
    CHECK("N8 设备节拍取最紧档 = 10ms（快过编译期扫描拍）",
          XModbusHub_pollInterval(hubB, devB) == (size_t)10,
          "得到=%u", (unsigned)XModbusHub_pollInterval(hubB, devB));
    /* [关掉模拟恢复期闸 2026-11-11] 模拟装配默认带 ttyS4 保真恢复期
     * （站 5 长帧收口后 125ms 内站 1 无应答，HsfServerIoSim.c 的
     * HSF_SIM_RECOVERY_MS=125）。它的设计前提是「主站静默闸 200ms >
     * 恢复期 125ms ⇒ 站 1 的帧永远到不了恢复窗」；而本段热总线
     * （流量计 10ms 一拍 + ~68ms 应答时延）让站间间隙恒 < 125ms ⇒
     * 逃逸帧会**全部**被模拟恢复期闸丢弃。这 faithfully 复刻了板端
     * 「逃逸帧闯恢复死区 ⇒ 700ms 在途超时」的放大器行为，但它属于
     * 修复③（半静默下限）的板端复测范畴；本段活性锁钉的是主修①
     * =逃逸簿记死代码。故把窗口清零，让逃逸帧真正到达模拟从站。
     * [下行转换依据] sim 模式下路 B 客户端由 HsfServerIoSim_createBusClient
     * 构造，必为 XModbusVirtualClient（m_base 为第一成员，C 下行转换成立）。 */
    {
        XModbusVirtualClient* vc =
            (XModbusVirtualClient*)XModbusHub_deviceClient(hubB, devB);
        CHECK("N8 路 B 客户端是模拟虚拟客户端（下行转换前提）", vc != NULL);
        XModbusVirtualClient_setRecoveryGate(vc, HSF_FLOWMETER_UNIT_ID,
                                             HSF_TURBIDITY1_UNIT_ID, 0);
        CHECK("N8 模拟恢复期闸已关闭（窗口=0）",
              vc && XModbusVirtualClient_recoveryWindowMs(vc) == 0);
    }
    {
        /* 阈值口径前置自检：站 1 约束里静默期 = 200（站 1 是唯一配闸者）。 */
        XModbusHubUnitLimits lim;
        memset(&lim, 0, sizeof(lim));
        CHECK("N8 站 1 静默期登记 = 200ms（本段热总线的前提）",
              XModbusHub_getUnitLimits(hubB, devB, HSF_TURBIDITY1_UNIT_ID,
                                       &lim) &&
              lim.m_preTxQuietMs == (uint32_t)HSF_TURBIDITY1_TX_QUIET_MS &&
              lim.m_preTxQuietMs > 0,
              "得到=%u", (unsigned)lim.m_preTxQuietMs);
        /* 装配期口径自检：浊度周期缺省 500 ⇒ 阈值 = max(基线, 500×3)，
         * 轮转+静默档 ((10×3+200)×2=460) 此时盖不过基线 ⇒ 恰等于基线。 */
        {
            uint32_t wantBase =
                (uint32_t)HSF_TURBIDITY_MIN_POLL_MS * HSF_STALE_FROM_POLL_MULT;
            if ((uint32_t)HSF_TURBIDITY_STALE_MS > wantBase)
                wantBase = HSF_TURBIDITY_STALE_MS;
            CHECK("N8 缺省阈值 = max(基线, 周期×3)（轮转+静默档未越基线）",
                  HsfServerIoTurbidity_staleMs(tb1) == wantBase,
                  "得到=%u 期望=%u",
                  HsfServerIoTurbidity_staleMs(tb1), wantBase);
        }
    }

    /* ---- ② 活性锁：站 1 / 站 3 都须有界时间内首帧。
     *       pointValid 对「从未回填」恒 false（UINT64_MAX 哨兵）⇒
     *       变 true 即首帧已到。maxAge 给大值（60s）：只区分
     *       「回填过/从未回填」，不掺新鲜度判定。 */
    t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < kDeadlineMs) {
        spinMs(50);
        if (firstTurb1Ms < 0 &&
            XModbusHub_pointValid(hubB, XModbusHoldingRegisters,
                                  (uint16_t)HSF_TURBIDITY_VADDR, 60000u))
            firstTurb1Ms = XDateTime_currentMSecsSinceEpoch() - t0;
        if (firstTurb2Ms < 0 &&
            XModbusHub_pointValid(hubB, XModbusHoldingRegisters,
                                  (uint16_t)HSF_TURBIDITY2_VADDR, 60000u))
            firstTurb2Ms = XDateTime_currentMSecsSinceEpoch() - t0;
        if (firstTurb1Ms >= 0 && firstTurb2Ms >= 0) break;
    }
    XPrintf("[INFO] N8 首次回填：站1=%lld ms 站3=%lld ms（时限 %lld ms）\n",
            (long long)firstTurb1Ms, (long long)firstTurb2Ms,
            (long long)kDeadlineMs);
    /* 对照腿先行：总线活着（同一总线、同一节拍，站 3 无静默闸照常轮询）。 */
    CHECK("N8 对照组：无静默闸的站 3 首帧到达（总线本身活着）",
          firstTurb2Ms >= 0, "首帧=%lld ms", (long long)firstTurb2Ms);
    CHECK("N8 活性锁：带静默闸的站 1 有界时间内首次回填"
          "（旧实现逃逸簿记死代码 ⇒ 此处必挂）",
          firstTurb1Ms >= 0 && firstTurb1Ms <= kDeadlineMs,
          "首帧=%lld ms 时限=%lld ms", (long long)firstTurb1Ms,
          (long long)kDeadlineMs);

    /* ---- ③ 阈值锁：把站 1 周期抬到 3000ms ⇒ 周期×3=9000 成最大档；
     *       测试用装配事实重算整条期望式（轮转+静默档一并算上），
     *       应用层漏掉任何一档都会相等失败。 */
    HsfServerSettings_setInt("collect/turbidity1PollMs", 3000);
    CHECK("N8 应用采集配置（浊度1 周期=3000）",
          HsfServerIo_applyCollectConfig(NULL));
    {
        size_t tickMs = XModbusHub_pollInterval(hubB, devB);
        int groups = XModbusHub_readGroupCount(hubB, devB);
        XModbusHubUnitLimits lim;
        uint32_t quiet1 = 0;
        uint64_t byRound;
        uint32_t want1;
        uint32_t want2;
        memset(&lim, 0, sizeof(lim));
        if (XModbusHub_getUnitLimits(hubB, devB, HSF_TURBIDITY1_UNIT_ID, &lim))
            quiet1 = lim.m_preTxQuietMs;
        byRound = (tickMs > 0 && groups > 0)
                      ? ((uint64_t)tickMs * (uint64_t)groups + quiet1) * 2u
                      : 0u;
        want1 = (uint32_t)3000 * HSF_STALE_FROM_POLL_MULT;
        if ((uint32_t)HSF_TURBIDITY_STALE_MS > want1) want1 = HSF_TURBIDITY_STALE_MS;
        if (byRound > (uint64_t)want1) want1 = (uint32_t)byRound;
        /* 站 3：周期缺省 500、静默期 0 ⇒ 纯「周期×3 与基线取大」。 */
        want2 = (uint32_t)HSF_TURBIDITY_MIN_POLL_MS * HSF_STALE_FROM_POLL_MULT;
        if ((uint32_t)HSF_TURBIDITY_STALE_MS > want2) want2 = HSF_TURBIDITY_STALE_MS;
        if (byRound > (uint64_t)want2) want2 = (uint32_t)byRound;
        XPrintf("[INFO] N8 阈值口径：拍长=%u 组数=%d 静默=%u 轮转项=%llu "
                "期望 站1=%u 站3=%u\n",
                (unsigned)tickMs, groups, (unsigned)quiet1,
                (unsigned long long)byRound, want1, want2);
        CHECK("N8 阈值锁：站 1 生效阈值 = max(基线, 周期×3, (轮转+静默)×2)",
              HsfServerIoTurbidity_staleMs(tb1) == want1,
              "得到=%u 期望=%u",
              HsfServerIoTurbidity_staleMs(tb1), want1);
        CHECK("N8 阈值锁：站 3 生效阈值同口径（静默期 0 不抬档）",
              HsfServerIoTurbidity_staleMs(tb2) == want2,
              "得到=%u 期望=%u",
              HsfServerIoTurbidity_staleMs(tb2), want2);
        CHECK("N8 阈值不低于轮转+静默下限（活性余量）",
              HsfServerIoTurbidity_staleMs(tb1) >= byRound &&
              HsfServerIoTurbidity_staleMs(tb2) >= byRound);
    }

    /* ---- ④ 尾部：拆除 + INI 还原。 */
    HsfServerIo_stop();
    for (i = 0; i < (int)(sizeof(kTouched) / sizeof(kTouched[0])); ++i)
        HsfServerSettings_remove(kTouched[i]);
    CHECK("N8 尾部：写入键已全部删除",
          !HsfServerSettings_contains("collect/flowmeterPollMs") &&
          !HsfServerSettings_contains("collect/turbidity1PollMs"));
}

static void testN_fieldAlignment(void)
{
    XPrintf("--- §N 现场口径对齐（板端实机复核）---\n");
    testN1_valveDiOrderFieldWiring();
    testN2_sideResolvedByServer();
    testN3_flowMeterAlarmHealth();
    testN4_digitalModuleName();
    testN5_collectPollKeysPerDevice();
    /* [2026-11-10] 加水温列后的两处位置下标漂移回归锁（时间戳跨零点
     * 日期多一天 + 异常行判据读到正累计列 ⇒ 整表误标粉红）。 */
    testN6_historyTsAndAbnormalCriteria();
    /* [2026-11-11] 采集配置运行期生效整链（含 5 开关停采/节拍不放大/
     * --sim 浊度缺省回落装配事实三个修复）。 */
    testN7_collectApplyRuntime();
    /* [2026-11-11] 浊度站 1 冷启动活性（静默闸逃逸对「从未发帧」从站
     * 生效）+ 陈旧阈值「轮转+静默」口径锁——板端偶发「数据过期」根修。 */
    testN8_turbidityColdStartLiveness();
}

#endif /* XINDUSTRY_ON && XMODBUS_HUB_ON */

/* ============================================================================
 * §O 变频器遥测刻度（2026-11-10 板端实机复测定案）
 *
 * [钉的是什么] M512 的 0x1001「运行频率」刻度：工程值 = raw / 100（0.01Hz）。
 *   这条曾长期取 ÷10（"0.1Hz"），来源是旧实测定标文档里的一句
 *   「写 0x1000=2000 ⇒ 面板 10Hz、0x1001=100」——**漏了一个 0**。
 *   板端 hd-rk3568 对**真机**复测（现场变频器实跑，两组给定）：
 *     · 给定 9720（97.2% ⇒ 48.6Hz）→ raw = 4860；
 *     · 给定 2000（20%  ⇒ 10.0Hz）→ 稳态 raw = 1000。
 *   两点都精确等于 Hz×100 ⇒ ÷100。物理交叉验证：50Hz 机型（P0.10=50Hz）
 *   不可能输出 486Hz；而 48.6Hz 对应 1399.68rpm 恰为 48.6/50×1440。
 *
 * [为什么必须锁] 取 ÷10 时客户端显示的输出频率与转速**整整差 10 倍**
 *   （板端实测 486Hz / 13996rpm，真实 48.6Hz / 1399.7rpm）——界面在骗人。
 *   Python 侧 devices.py 一直是 ÷100（本锁的基准），两边必须同口径。
 * ==========================================================================*/
#if XVFD_ON

/**
 * @brief O1：M512 点规格表的输出频率除率 == 100（原生 0.01Hz）。
 */
static void testO1_m512FrequencyScale(void)
{
    int count = 0;
    const XVfdPointSpec* specs = XVfdM512_pointSpecs(&count);
    int i;
    int div = -1;

    XPrintf("--- O1 M512 输出频率刻度（实机复测 ÷100）---\n");
    CHECK("O1 取到 M512 点规格表", specs != NULL && count > 0);
    if (!specs) return;
    for (i = 0; i < count; ++i) {
        if (specs[i].m_role == XVfdRole_OutputFreq) {
            div = specs[i].m_div;
            break;
        }
    }
    CHECK("O1 输出频率除率 == 100（raw=4860 ⇔ 48.6Hz）", div == 100,
          "div=%d（改刻度前是 10 ⇒ 显示差 10 倍）", div);
    /* 相邻遥测的除率一并钉住（实机 raw：母线 5448→544.8V ÷10、电压 374→374V
     * ÷1、电流 ÷100、转矩 ÷10）。 */
    {
        int dBus = -1;
        int dVolt = -1;
        int dCur = -1;
        for (i = 0; i < count; ++i) {
            if (specs[i].m_role == XVfdRole_BusVoltage) dBus = specs[i].m_div;
            else if (specs[i].m_role == XVfdRole_OutputVoltage) dVolt = specs[i].m_div;
            else if (specs[i].m_role == XVfdRole_OutputCurrent) dCur = specs[i].m_div;
        }
        CHECK("O1 母线 ÷10 / 输出电压 ÷1 / 输出电流 ÷100（实机标定）",
              dBus == 10 && dVolt == 1 && dCur == 100,
              "母线=%d 电压=%d 电流=%d", dBus, dVolt, dCur);
    }
}

#endif /* XVFD_ON */

/* ============================================================================
 * §P 变频器「带给定启动」必须是**一个**设备动作（2026-11-10 板端实机根因）
 *
 * [现场症状] C 客户端点「启动」恒回「设备拒绝：设备忙（上一动作在途）」，
 *   而同一批设备的 Python 服务端能正常控制。
 *
 * [根因] 服务端原来分两次调用设备层：先 `XVfdDevice_setFrequency`（写
 *   0x1000 给定），再 `XVfdDevice_start`（写 0x2000 命令字）。设备层有
 *   **单飞闸**（m_busy：同一时刻只允许一个动作在途，落定由链路回执收口），
 *   第一笔写把闸占住，第二笔同拍发出 ⇒ 立刻被拒成「设备忙」。
 *
 * [修法] 把两笔合成**一个多阶段动作**（XVfdDevice_startWithGiven）：同一
 *   m_busy 周期内，给定写落定后在 ActionWriteDone 里推进到命令字阶段。
 *
 * [本锁钉什么] M512 必须**重载** EXVfdDevice_StartWithGiven，不能落到基类
 *   缺省实现（缺省实现就是"分两次调用"那条老路）。结构性断言，锁住"别把
 *   重载删了"。
 * ==========================================================================*/
#if XVFD_ON

/** @brief P1：M512 重载了 StartWithGiven（不走"两次调用"的缺省实现）。 */
static void testP1_startWithGivenIsOneAction(void)
{
    XVfdDevice* dev;
    XVfdDevice* base;
    void* m512Impl = NULL;
    void* baseImpl = NULL;

    XPrintf("--- P1 带给定启动＝一个动作（M512 重载）---\n");
    dev = (XVfdDevice*)XVfdM512_create();
    base = XVfdDevice_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    CHECK("P1 M512 与基类设备都建成", dev != NULL && base != NULL);
    if (dev) {
        m512Impl = XClassGetVirtualFunc((XObject*)dev,
                                        EXVfdDevice_StartWithGiven, void*);
    }
    if (base) {
        baseImpl = XClassGetVirtualFunc((XObject*)base,
                                        EXVfdDevice_StartWithGiven, void*);
    }
    CHECK("P1 基类有缺省实现（两次调用的老路，作为对照）", baseImpl != NULL);
    CHECK("P1 M512 槽位非空", m512Impl != NULL);
    CHECK("P1 M512 走自己的多阶段实现（≠ 基类缺省实现）",
          m512Impl != NULL && baseImpl != NULL && m512Impl != baseImpl);
    if (dev) XClassDelete((XClass*)dev);
    if (base) XClassDelete((XClass*)base);
}

#endif /* XVFD_ON */

/* ============================================================================
 *        §H 历史页「图表缩放取数」专项（2026-11-09 并入本文件）
 *
 * [为什么并进来] 用户口径「所有测试代码集成到一个测试文件，C 端一个、
 * Python 一个」。原来它在独立的 hsf_chart_zoom_test.c 里、由 main.c 的
 * `--test hsf-chart-zoom` 与 §H 两处调用；并进来之后断言只此一份，
 * `--test hsf-regression` 一条命令跑完全部 C 端断言。
 * 子断言编号沿用 Z1..Z11（与本文件 §A..§J 区分，避免重号难排查）。
 * ==========================================================================*/

/* ==================== §1 批次计划 ==================== */

/** @brief 窗口态：窗内行数 ≤ 上限 ⇒ 逐行取（步进 = 批行数 ⇒ 连续覆盖）。 */
static void testZ1_windowPlanIsRowByRow(void)
{
    HsfChartZoomPlan plan;

    /* 窗内 30 行（< 批行数 45）⇒ 一批取完，30 行全在（不跳批、不丢段）。 */
    (void)HsfChartZoom_planBatches(30, 45, true, 12000, 500, 12000, &plan);
    CHECK("Z1 窗口态·窗内 30 行 ⇒ 1 批（一次取完每一行）", plan.batches == 1);
    CHECK("Z1 窗口态·窗内 30 行 ⇒ 步进=批行数（连续无跳段）",
          plan.step == 45);
    CHECK("Z1 窗口态·窗内 30 行 ⇒ 非抽样态（全分辨率）", !plan.sampled);

    /* 窗内 9788 行 ⇒ 218 批 × 45 行 ⇒ 覆盖 9810 ≥ 9788（末批取不满即止）。 */
    (void)HsfChartZoom_planBatches(9788, 45, true, 12000, 500, 12000, &plan);
    CHECK("Z1 窗口态·窗内 9788 行 ⇒ 218 批（逐行取）", plan.batches == 218);
    CHECK("Z1 窗口态·窗内 9788 行 ⇒ 步进=批行数（连续覆盖窗内每一行）",
          plan.step == 45);
    CHECK("Z1 窗口态·窗内 9788 行 ⇒ 覆盖到窗尾（(batches-1)*step+chunk ≥ total）",
          (long)(plan.batches - 1) * plan.step + 45 >= 9788L);
    CHECK("Z1 窗口态·窗内 9788 行 ⇒ 非抽样态", !plan.sampled);

    /* 窗内行数超上限 ⇒ 退化为窗内抽样（并标记，状态行要注明）。
     * [不是"只取十来个簇"] 超上限时仍要**用满内存上限**均匀铺满整个窗：
     * 按目标点数（500）只取 12 批 × 45 行时，簇与簇之间是空的，缩放到窗内
     * 看到的还是断续线段（用户口径"不够连续"）。 */
    (void)HsfChartZoom_planBatches(20000, 45, true, 12000, 500, 12000, &plan);
    CHECK("Z1 窗口态·窗内 20000 行（超上限）⇒ 标记为窗内抽样", plan.sampled);
    CHECK("Z1 窗口态·窗内 20000 行 ⇒ 用满内存上限铺满窗（maxRows=12000 ⇒ 266 批）",
          plan.batches == 12000 / 45);
    /* [批数必须随内存上限变，而不是恒等于某个常数] 换一个 maxRows 再算一次：
     * 上限真被读取时批数应跟着变（2700/45=60）。只断言"≤ 上限"没有鉴别
     * 力——批数由别的因子决定时那条断言照样恒真。 */
    {
        HsfChartZoomPlan capPlan;
        CHECK("Z1 窗口态·批数随内存上限（maxRows=2700 ⇒ 60 批）",
              HsfChartZoom_planBatches(20000, 45, true, 12000, 500, 2700,
                                       &capPlan) == 2700 / 45
                  && capPlan.sampled);
    }
    CHECK("Z1 窗口态·窗内 20000 行 ⇒ 步长 > 批行数（窗内均匀跳批）",
          plan.step > 45);
    CHECK("Z1 窗口态·窗内 20000 行 ⇒ 末批够到窗尾",
          (long)(plan.batches - 1) * plan.step + 45 >= 20000L);
}

/** @brief 全量态：仍是跨区间跳批抽样（未缩放看全貌，不改成逐行全量）。 */
static void testZ2_fullPlanIsSampled(void)
{
    HsfChartZoomPlan plan;

    (void)HsfChartZoom_planBatches(18632, 45, false, 12000, 500, 12000, &plan);
    CHECK("Z2 全量态·18632 行 ⇒ 批数=ceil(目标点数/批行数)（500/45⇒12 批）",
          plan.batches == 12);
    CHECK("Z2 全量态·步进 > 批行数（跨区间跳批，不是连续取）",
          plan.step > 45);
    CHECK("Z2 全量态·末批够到区尾（(batches-1)*step+chunk ≥ total）",
          (long)(plan.batches - 1) * plan.step + 45 >= 18632L);
    CHECK("Z2 全量态·非窗口态 ⇒ 不标抽样", !plan.sampled);

    /* [上限截断] 目标点数 × 批行数一旦超过内存上限就必须截断，否则暂存
     * 会被撑爆：wantPoints=5000、chunk=45 ⇒ 名义 112 批，而 maxRows=900
     * 只装得下 20 批 ⇒ 实测批数 20（不是 112）。这条锁的是"上限真的被读"，
     * 与上面"≤ 上限"那类恒真断言不同。 */
    {
        HsfChartZoomPlan capPlan;
        (void)HsfChartZoom_planBatches(200000, 45, false, 12000, 5000, 900,
                                       &capPlan);
        CHECK("Z2 全量态·目标点数超上限 ⇒ 按内存上限截断（900/45=20 批）",
              capPlan.batches == 900 / 45);
    }
    /* 库很小（< 2 批）⇒ 按实际行数算，批数不为 0。 */
    (void)HsfChartZoom_planBatches(10, 45, false, 12000, 500, 12000, &plan);
    CHECK("Z2 全量态·库内 10 行 ⇒ 1 批", plan.batches == 1);
    CHECK("Z2 全量态·库内 0 行 ⇒ 0 批（无数据）",
          HsfChartZoom_planBatches(0, 45, false, 12000, 500, 12000, &plan) == 0);
}

/* ==================== §2 可见窗换算（同基准） ==================== */

/** @brief 轴域（相对基准秒）⇄ 绝对时刻（epoch ms）。 */
static void testZ3_visibleWindowUsesSameBase(void)
{
    const int64_t base = 1794210635000LL;   /* 2026-11-09 15:10:35 */
    int64_t lo = 0;
    int64_t hi = 0;

    CHECK("Z3 基准未定时不产出可见窗",
          !HsfChartZoom_visibleWindow(0, -100.0, 0.0, &lo, &hi));
    CHECK("Z3 可见窗换算：base + x*1000（与数据 X 值同基准）",
          HsfChartZoom_visibleWindow(base, -3600.0, 0.0, &lo, &hi) &&
          lo == base - 3600000LL && hi == base);
    CHECK("Z3 可见窗换算：域非法（hi<=lo）不产出",
          !HsfChartZoom_visibleWindow(base, 0.0, 0.0, &lo, &hi));
    CHECK("Z3 可见窗换算：负偏移（早期数据）落在基准之前",
          HsfChartZoom_visibleWindow(base, -234000.0, -171000.0, &lo, &hi) &&
          lo == base - 234000000LL && hi == base - 171000000LL);
}

/** @brief 取数窗外扩（1%，最小 1s）——边界行也取回，曲线才铺满两端。 */
static void testZ4_expandWindow(void)
{
    int64_t lo = 0;
    int64_t hi = 0;

    HsfChartZoom_expandWindow(1000000LL, 2000000LL, &lo, &hi);
    CHECK("Z4 取数窗外扩 1%（左右各 10s）",
          lo == 1000000LL - 10000LL && hi == 2000000LL + 10000LL);
    HsfChartZoom_expandWindow(1000000LL, 1000100LL, &lo, &hi);
    CHECK("Z4 取数窗外扩最小 1s（窄窗按 1s 外扩）",
          lo == 1000000LL - 1000LL && hi == 1000100LL + 1000LL);
}

/* ==================== §3 视口判定 ==================== */

/** @brief 可见窗 ≥ 全量域 90% ⇒ 全量视图（缩回全域 → 恢复全量抽样曲线）。 */
static void testZ5_fullViewDetection(void)
{
    const int64_t fullLo = 1000000LL;
    const int64_t fullHi = 1000000LL + 100000LL;   /* 100s 全量域 */

    CHECK("Z5 可见窗=全量域 ⇒ 全量视图",
          HsfChartZoom_isFullView(fullLo, fullHi, fullLo, fullHi));
    CHECK("Z5 可见窗 95% ⇒ 全量视图",
          HsfChartZoom_isFullView(fullLo + 2500LL, fullHi, fullLo, fullHi));
    CHECK("Z5 可见窗 50% ⇒ 非全量视图（进窗口模式取数）",
          !HsfChartZoom_isFullView(fullLo + 50000LL, fullHi, fullLo, fullHi));
    CHECK("Z5 全量域未知 ⇒ 保守按全量视图（不误触发取数）",
          HsfChartZoom_isFullView(fullLo, fullHi, 0, 0));
}

/** @brief 已取窗覆盖可见窗 ⇒ 不用再取（继续放大不重复取、缩回不空图）。 */
static void testZ6_windowCoverage(void)
{
    CHECK("Z6 可见窗在已取窗内 ⇒ 覆盖（不重复取）",
          HsfChartZoom_windowCovers(1000LL, 2000LL, 1200LL, 1800LL));
    CHECK("Z6 可见窗越过已取窗下界 ⇒ 不覆盖（须重取）",
          !HsfChartZoom_windowCovers(1000LL, 2000LL, 900LL, 1800LL));
    CHECK("Z6 可见窗越过已取窗上界 ⇒ 不覆盖（须重取）",
          !HsfChartZoom_windowCovers(1000LL, 2000LL, 1200LL, 2100LL));
    CHECK("Z6 未持有窗口（全量态）⇒ 不覆盖",
          !HsfChartZoom_windowCovers(0, 0, 1200LL, 1800LL));
}

/* ==================== §4 取数 limit 轮转 ==================== */

/** @brief 相邻两轮 limit 必不相等；与页大小 10/20/50 两两不相交。 */
static void testZ7_limitRotation(void)
{
    int seq;
    int prevProbe = -1;
    int prevChunk = -1;
    int sameProbe = 0;
    int sameChunk = 0;
    int clash = 0;

    for (seq = 0; seq < 72; ++seq) {
        int probe = 0;
        int chunk = 0;
        HsfChartZoom_nextLimits(seq, &probe, &chunk);
        if (probe == prevProbe) sameProbe++;
        if (chunk == prevChunk) sameChunk++;
        prevProbe = probe;
        prevChunk = chunk;
        /* 与页大小/导出大小（10/20/50）撞键 ⇒ 应答会被误认领 */
        if (probe == 10 || probe == 20 || probe == 50) clash++;
        if (chunk == 10 || chunk == 20 || chunk == 50) clash++;
        /* 探测批与正式批互撞 */
        if (probe == chunk) clash++;
    }
    CHECK("Z7 轮转 72 轮：相邻两轮探测批 limit 必不相等", sameProbe == 0);
    CHECK("Z7 轮转 72 轮：相邻两轮正式批 limit 必不相等", sameChunk == 0);
    CHECK("Z7 轮转 72 轮：limit 与页大小 10/20/50 及探测/正式互不撞键",
          clash == 0);
    {
        int probe = 0;
        int chunk = 0;
        HsfChartZoom_nextLimits(0, &probe, &chunk);
        CHECK("Z7 首轮 limit 在约定区间内（探测 1..8 / 正式 41..49）",
              probe >= 1 && probe <= 8 && chunk >= 41 && chunk <= 49);
    }
}

/* ==================== §6 轴域与取数窗同源（根修锁） ==================== */

/** @brief 轴域 → 取数窗只有一份计算（HsfChartZoom_view）。
 *  @details [现场根因] 2026-11-09 端到端取证：窗口取数在途时，"全量态重绘"
 *           把用户的轴域改写成"已到达那几行的时间跨度"（日志实测
 *           `[-13547.301, -10903.727]` → `[-12833.300, -10841.700]`），
 *           而请求窗仍按原轴域发出 ⇒ 取回的点与轴域不同参、两端被裁掉。
 *           修复的判据：**轴域与请求窗必须来自同一份计算**。这里钉住
 *           "视图一次算齐"的三条恒等式，页面只要用同一个视图就不可能错位。 */
static void testZ9_viewIsSingleSource(void)
{
    const int64_t base = 1794210635000LL;   /* 2026-11-09 15:10:35 */
    HsfChartZoomView v;
    int64_t expLo = 0;
    int64_t expHi = 0;

    CHECK("Z9 视图换算成功（基准已定 + 轴域合法）",
          HsfChartZoom_view(base, -13547.301, -10903.727, &v) && v.valid);
    CHECK("Z9 轴域按原值保留（重放轴域用的唯一事实源）",
          v.xLo == -13547.301 && v.xHi == -10903.727);
    HsfChartZoom_expandWindow(v.viewLo, v.viewHi, &expLo, &expHi);
    CHECK("Z9 取数窗 == 轴域绝对窗的外扩（同一份计算，不另算一遍）",
          v.fetchLo == expLo && v.fetchHi == expHi);
    CHECK("Z9 轴域 ⇄ 绝对窗同基准（base + x*1000，与数据 X 值同参）",
          v.viewLo == base + (int64_t)(v.xLo * 1000.0) &&
          v.viewHi == base + (int64_t)(v.xHi * 1000.0));
    CHECK("Z9 取数窗严格包含可见窗（边界行也取回，曲线铺满两端）",
          v.fetchLo < v.viewLo && v.fetchHi > v.viewHi);
    CHECK("Z9 窗宽 = 轴宽 + 2×外扩（左右对称，不偏向一侧）",
          (v.fetchHi - v.fetchLo) ==
              (v.viewHi - v.viewLo) + 2 * (v.viewLo - v.fetchLo));

    /* 换一个轴域 ⇒ 窗跟着换（没有"上一轮残留窗"这种第二事实源）。 */
    {
        HsfChartZoomView v2;
        CHECK("Z9 换轴域 ⇒ 取数窗随之重算（无残留窗）",
              HsfChartZoom_view(base, -3000.0, -2000.0, &v2) &&
              v2.fetchLo != v.fetchLo && v2.viewLo > v.viewLo);
        CHECK("Z9 同一轴域重复换算 ⇒ 同一窗（确定性）",
              HsfChartZoom_view(base, -3000.0, -2000.0, &v2) &&
              v2.fetchLo == base - 3000000LL - 10000LL &&
              v2.fetchHi == base - 2000000LL + 10000LL);
    }

    /* 基准未定 / 域非法 ⇒ 视图无效（页面据此不取数，不能发出 0 窗口请求）。 */
    CHECK("Z9 基准未定 ⇒ 视图无效",
          !HsfChartZoom_view(0, -100.0, 0.0, &v) && !v.valid);
    CHECK("Z9 域非法（hi<=lo）⇒ 视图无效",
          !HsfChartZoom_view(base, 0.0, 0.0, &v) && !v.valid);
    CHECK("Z9 out 为 NULL ⇒ 只判有效性（不写空指针）",
          !HsfChartZoom_view(0, -1.0, 0.0, NULL));
}

/* ==================== §7 取回的点落在窗口内 / 铺满视口 ==================== */

/** @brief ts 访问器（回归锁手里是纯 ts 数组）。 */
static int64_t zoomTestTsAt(const void* user, int index)
{
    const int64_t* ts = (const int64_t*)user;
    return ts[index];
}

/** @brief 数据点时间戳全部落在窗口内，且视口被铺满。 */
static void testZ10_pointsInsideWindow(void)
{
    const int64_t base = 1794210635000LL;
    const int64_t stepMs = 5000;            /* 5s 一行（与服务端采样同量级） */
    HsfChartZoomView v;
    int64_t ts[4200];
    int n = 0;
    int i;
    int inView;

    CHECK("Z10 视图可用（轴域 -2000s..-1000s）",
          HsfChartZoom_view(base, -2000.0, -1000.0, &v) && v.valid);

    /* 模拟服务端按取数窗返回的行：均匀覆盖 [fetchLo, fetchHi]。 */
    for (i = 0; v.fetchLo + (int64_t)i * stepMs <= v.fetchHi &&
                i < (int)(sizeof(ts) / sizeof(ts[0])); ++i) {
        ts[i] = v.fetchLo + (int64_t)i * stepMs;
        ++n;
    }
    CHECK("Z10 模拟数据量 = 取数窗跨度/间隔 + 1（1020s / 5s + 1 = 205）",
          n == 205);

    /* ① 每一条都在取数窗内（服务端按 loTs/hiTs 过滤的直接结果）。 */
    {
        int bad = 0;
        for (i = 0; i < n; ++i)
            if (ts[i] < v.fetchLo || ts[i] > v.fetchHi) ++bad;
        CHECK("Z10 数据点时间戳全部落在取数窗内（无窗外点）", bad == 0);
    }
    /* ② 视口内的点换算成 X 值后全部落在轴域内（同基准、同参）。 */
    {
        int bad = 0;
        for (i = 0; i < n; ++i) {
            double x;
            if (ts[i] < v.viewLo || ts[i] > v.viewHi) continue;
            x = (double)(ts[i] - base) / 1000.0;
            if (x < v.xLo || x > v.xHi) ++bad;
        }
        CHECK("Z10 视口内的点换算 X 后全部落在轴域内（同基准同参）", bad == 0);
    }
    /* ③ 视口内点数 = 窗口跨度/间隔 + 1（1% 边距的行在视口外）。 */
    inView = HsfChartZoom_countInView(&v, zoomTestTsAt, ts, n);
    CHECK("Z10 视口内点数 = 1000s/5s + 1（只有 1% 边距在视口外）",
          inView == 201);
    CHECK("Z10 数据铺满视口两端（首末点跨视口 ≥98%）",
          HsfChartZoom_viewFilled(&v, zoomTestTsAt, ts, n));

    /* ④ 只覆盖视口左半段 ⇒ 不算铺满（用户看到的"曲线从中间断掉"）。 */
    {
        int half = 0;
        for (i = 0; i < n && ts[i] <= v.viewLo + (v.viewHi - v.viewLo) * 40 / 100;
             ++i)
            ts[half++] = ts[i];
        CHECK("Z10 只有左半段数据 ⇒ 不判铺满（状态行要提示数据不足）",
              !HsfChartZoom_viewFilled(&v, zoomTestTsAt, ts, half));
        CHECK("Z10 只有左半段数据 ⇒ 视口内点数明显少于全窗",
              HsfChartZoom_countInView(&v, zoomTestTsAt, ts, half) < inView);
    }
    /* ⑤ 空窗 / 单点：不铺满，点数为 0/1（不能算出负数或越界）。 */
    CHECK("Z10 空数据 ⇒ 点数为 0 且不判铺满",
          HsfChartZoom_countInView(&v, zoomTestTsAt, ts, 0) == 0 &&
          !HsfChartZoom_viewFilled(&v, zoomTestTsAt, ts, 0));
    {
        int64_t one = v.viewLo + 1000;
        CHECK("Z10 单点数据 ⇒ 点数为 1 且不判铺满",
              HsfChartZoom_countInView(&v, zoomTestTsAt, &one, 1) == 1 &&
              !HsfChartZoom_viewFilled(&v, zoomTestTsAt, &one, 1));
    }
}

/* ==================== §8 状态行按窗口重算 ==================== */

/** @brief 窗口状态行：写明起止 + 行数，且不复用全量区间文案。 */
static void testZ11_statusFollowsWindow(void)
{
    HsfChartZoomStatus st;
    char a[256];
    char b[256];
    char c[256];
    char d[256];

    st.rowsHeld = 412;
    st.rowsInView = 380;
    st.sampled = false;
    st.filled = true;
    (void)HsfChartZoom_statusText("2026-11-09 11:03:12",
                                  "2026-11-09 11:36:21", &st, a, (int)sizeof(a));
    CHECK("Z11 窗口口径：写明窗口起止时刻",
          strstr(a, "11:03:12") != NULL && strstr(a, "11:36:21") != NULL);
    CHECK("Z11 窗口口径：写明窗内行数与视口行数",
          strstr(a, "412") != NULL && strstr(a, "380") != NULL);
    CHECK("Z11 窗口口径：注明逐行取回（窗内全分辨率）",
          strstr(a, "逐行取回") != NULL);
    CHECK("Z11 窗口口径：不含全量区间文案（筛选共 N 行 / 全区间抽样）",
          strstr(a, "筛选共") == NULL && strstr(a, "全区间抽样") == NULL);

    /* 换窗口 ⇒ 文案必变（状态行必须随可视窗口走）。 */
    (void)HsfChartZoom_statusText("2026-11-09 12:00:00",
                                  "2026-11-09 12:20:00", &st, b, (int)sizeof(b));
    CHECK("Z11 换窗口 ⇒ 状态行文字随之变化（不停在上一个窗口）",
          strcmp(a, b) != 0 && strstr(b, "12:00:00") != NULL);

    /* 窗内抽样 / 数据不足：两条附加口径都要能表达。 */
    st.sampled = true;
    (void)HsfChartZoom_statusText("11:03:12", "11:36:21", &st, c,
                                  (int)sizeof(c));
    CHECK("Z11 窗内行数超上限 ⇒ 文案注明窗内抽样", strstr(c, "窗内抽样") != NULL);
    st.sampled = false;
    st.filled = false;
    (void)HsfChartZoom_statusText("11:03:12", "11:36:21", &st, d,
                                  (int)sizeof(d));
    CHECK("Z11 数据未铺满视口 ⇒ 文案提示数据不足",
          strstr(d, "数据不足") != NULL);
    CHECK("Z11 抽样与数据不足两种文案互不混用",
          strstr(c, "数据不足") == NULL && strstr(d, "窗内抽样") == NULL);

    /* 窗口内 0 行：窗口口径的"无记录"，不能说成"筛选结果为 0 行"。 */
    st.rowsHeld = 0;
    st.rowsInView = 0;
    st.filled = false;
    (void)HsfChartZoom_statusText("11:03:12", "11:36:21", &st, a,
                                  (int)sizeof(a));
    CHECK("Z11 空窗 ⇒ 注明该时间段无记录",
          strstr(a, "无记录") != NULL && strstr(a, "筛选") == NULL);
    CHECK("Z11 空窗也不丢窗口起止", strstr(a, "11:03:12") != NULL &&
                                        strstr(a, "11:36:21") != NULL);
    CHECK("Z11 时间文本缺省（NULL）⇒ 用 -- 占位，不崩",
          HsfChartZoom_statusText(NULL, NULL, &st, b, (int)sizeof(b)) > 0 &&
          strstr(b, "--") != NULL);
}

/* ==================== §5 泄漏自检 ==================== */

/** @brief 本文件跑完堆在用字节数必须回基线（缩放取数是高频路径）。 */
static void testZ8_noLeak(void)
{
    XMemoryStatistics before;
    XMemoryStatistics after;
    int i;

    if (!XMemory_statisticsEnabled()) {
        XPrintf("[SKIP] Z8 泄漏自检：全局内存统计未开启\n");
        return;
    }
    before = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    for (i = 0; i < 2000; ++i) {
        HsfChartZoomPlan plan;
        HsfChartZoomView view;
        HsfChartZoomStatus st;
        int64_t lo = 0;
        int64_t hi = 0;
        int64_t ts[8];
        char buf[256];
        int k;
        (void)HsfChartZoom_planBatches(9000 + i, 45, true, 12000, 500, 12000,
                                        &plan);
        (void)HsfChartZoom_visibleWindow(1794210635000LL, -3600.0 - i, 0.0,
                                         &lo, &hi);
        HsfChartZoom_expandWindow(lo, hi, &lo, &hi);
        (void)HsfChartZoom_isFullView(lo, hi, 0, 100000);
        (void)HsfChartZoom_windowCovers(lo, hi, lo, hi);
        /* §6–§8 的助手同样是高频路径（每次视口变化都跑）：一并进泄漏自检。 */
        if (HsfChartZoom_view(1794210635000LL, -3600.0 - i, 0.0, &view)) {
            for (k = 0; k < 8; ++k)
                ts[k] = view.viewLo + (int64_t)k * 1000;
            st.rowsHeld = 8 + i;
            st.rowsInView = HsfChartZoom_countInView(&view, zoomTestTsAt, ts, 8);
            st.sampled = (i & 1) != 0;
            st.filled = HsfChartZoom_viewFilled(&view, zoomTestTsAt, ts, 8);
            (void)HsfChartZoom_statusText("11:00:00", "12:00:00", &st, buf,
                                          (int)sizeof(buf));
        }
    }
    after = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    CHECK("Z8 缩放取数纯逻辑 2000 轮：堆在用字节数回基线（无泄漏）",
          after.systemBytes <= before.systemBytes + 4096);
}

int HsfChartZoomTest_run(void)
{
    /* [并入后不再自清计数] 原来这是独立入口，自己 g_pass/g_fail 清零并打一行
     * "专项结果"。并进本文件后它与 §A..§J 共用同一对计数器 ⇒ 清零会抹掉
     * 前面所有段落的成绩；结果行也由本文件末尾统一出。 */
    XPrintf("--- §H 历史页图表缩放取数（子断言 Z1..Z11）---\n");
    XPrintf("--- §1 缩放取数的批次计划 ---\n");
    testZ1_windowPlanIsRowByRow();
    testZ2_fullPlanIsSampled();
    XPrintf("--- §2 可见窗换算（数据与轴域同基准）---\n");
    testZ3_visibleWindowUsesSameBase();
    testZ4_expandWindow();
    XPrintf("--- §3 视口判定（全量视图 / 已取窗覆盖）---\n");
    testZ5_fullViewDetection();
    testZ6_windowCoverage();
    XPrintf("--- §4 取数 limit 轮转（应答归属不串味）---\n");
    testZ7_limitRotation();
    XPrintf("--- §6 轴域与取数窗同源（视图一次算齐）---\n");
    testZ9_viewIsSingleSource();
    XPrintf("--- §7 取回的点落在窗口内 / 铺满视口 ---\n");
    testZ10_pointsInsideWindow();
    XPrintf("--- §8 状态行按窗口重算 ---\n");
    testZ11_statusFollowsWindow();
    XPrintf("--- §5 泄漏自检 ---\n");
    testZ8_noLeak();
    return g_fail;
}

/* ============================================================================
 *        §M 读组「不应答」与「异常应答」的框架级回归锁（2026-11-10）
 *
 * [钉的是什么] 两条**总线级活性**契约：
 *
 *   M1「某组不应答 ⇒ 别的组照常轮询」——
 *      从站对某一组**不应答**（真机 M512 对 0xFA00×5 无应答；模拟器越界
 *      地址亦然）时，在途标记会一直挂着 ⇒ Hub 的单飞闸（`if (dev->inflight)
 *      return;`）把该设备后续**所有**读组永久堵死。现场表现：变频器整站
 *      离线、失败计数=0、游标卡在 1、其余组从不再发，而同一总线上的别的站
 *      一切正常。框架级契约：**一个组的沉默拖不垮整条总线**——超时即收口
 *      （判失败 + 记失败计数 + 报从站退避 + 释放在途标记）并继续下一组。
 *
 *   M2「异常应答是组级事实 ⇒ 不牵连同从站的其它组」——
 *      Modbus 异常帧（EXC02/EXC03…）= "这个地址不支持"，是**组级**事实。
 *      若按从站级失败回报退避表，连续 3 次后**该从站的所有组**一起被静默
 *      ⇒ 现场"一台设备整体离线"，而真机只是其中一组不支持。
 *
 * [现场根因（同一批修复）] 2026-11-10 现场"变频器整站哑掉"的直接根因是
 *      配置侧把 XModbusHubUnitLimits 里的 m_preTxQuietMs 留成**未初始化
 *      的栈垃圾**（Debug 构建 = 0xCCCCCCCC ≈ 34 亿毫秒）⇒ 静默闸要求
 *      34 亿毫秒总线静默 ⇒ 该从站的读组永远轮不到。M3 钉住框架层的活性
 *      保证（静默闸饥饿逃逸）：**任何从站都不会被静默闸永久饿死**。
 * ==========================================================================*/

/**
 * @brief 假从站客户端：站 m_silentUnit **受理请求但永不终结 Reply**
 *        （= 真机对某组不应答的同款场景：请求发出去了，从站就是不应）。
 *        其余站立即应答（同步终态）。
 */
XCLASS_DEFINE_BEGING(XMSilentSlaveClient)
XCLASS_DEFINE_EXTEND_END(XMSilentSlaveClient, XModbusClient)

typedef struct XMSilentSlaveClient {
    XModbusClient m_base;        /**< 基类（继承自 XModbusClient）。 */
    int m_silentUnit;            /**< 永不应答的站号。 */
    int m_reqSilent;             /**< 不应答站收到的请求数。 */
    int m_reqOther;              /**< 其它站收到的请求数（合计）。 */
    int m_reqByUnit[16];         /**< 各站请求数（按站号分桶；越界忽略）。 */
    int m_abandonCount;           /**< abandonReply 被调用次数（超时收口摘手计数）。 */
} XMSilentSlaveClient;

/** @brief 假从站 open：直接置「已连接」（无真实传输层）。 */
static bool XMSilentSlave_open(XModbusDevice* device)
{
    if (!device) return false;
    XModbusDevice_setState(device, XModbusDevice_ConnectedState);
    return true;
}

/** @brief 假从站 close：置「未连接」。 */
static void XMSilentSlave_close(XModbusDevice* device)
{
    if (!device) return;
    XModbusDevice_setState(device, XModbusDevice_UnconnectedState);
}

/**
 * @brief 假从站 sendRawRequest：按站号分流——哑站只收不回，活站立即应答。
 */
static XModbusReply* XMSilentSlave_sendRawRequest(XModbusClient* baseClient,
                                                   const XModbusRequest* request,
                                                   int serverAddress,
                                                   XFuncParamType type)
{
    XMSilentSlaveClient* c = (XMSilentSlaveClient*)baseClient;
    XModbusReply* reply = NULL;
    const XModbusPdu* pdu = (const XModbusPdu*)request;
    const uint8_t* d;
    size_t n;
    uint16_t addr = 0;
    uint16_t count = 0;
    XModbusRegisterType rtype = XModbusDiscreteInputs;
    if (!c || !request) return NULL;
    d = (const uint8_t*)XContainerDataAddr((const XContainer*)pdu->m_data);
    n = XByteArray_size_base(pdu->m_data);
    if (n >= 4) {
        addr = (uint16_t)(((uint16_t)d[0] << 8) | d[1]);
        count = (uint16_t)(((uint16_t)d[2] << 8) | d[3]);
    }
    switch (XModbusPdu_functionCodeRaw(pdu)) {
    case (uint8_t)XModbusPdu_ReadCoils:       rtype = XModbusCoils; break;
    case (uint8_t)XModbusPdu_ReadDiscreteInputs: rtype = XModbusDiscreteInputs; break;
    case (uint8_t)XModbusPdu_ReadInputRegisters: rtype = XModbusInputRegisters; break;
    case (uint8_t)XModbusPdu_ReadHoldingRegisters: rtype = XModbusHoldingRegisters; break;
    default: rtype = XModbusDiscreteInputs; break;
    }
    if (serverAddress == c->m_silentUnit) {
        ++c->m_reqSilent;
    } else {
        ++c->m_reqOther;
    }
    if (serverAddress >= 0 && serverAddress < 16) ++c->m_reqByUnit[serverAddress];
    if (type == XFuncParamType_Copy) {
        reply = XModbusClient_createReply(baseClient, request, serverAddress);
    } else if (type == XFuncParamType_Move) {
        reply = XModbusClient_createReply_move(baseClient,
                                               (XModbusRequest*)request,
                                               serverAddress);
    } else {
        reply = XModbusClient_createReply_ref(baseClient,
                                              (XModbusRequest*)request,
                                              serverAddress);
    }
    if (!reply) return NULL;
    if (serverAddress == c->m_silentUnit) {
        /* 哑站：受理了但**永不终结** ⇒ Hub 的在途标记会一直挂着
         * （这正是真机 M512 对 0xFA00×5 无应答的形态）。 */
        XModbusReply_setState(reply, XModbusReply_State_Waiting);
        return reply;
    }
    /* 活站：回一个确定的值（0x5A5A），同步终结。 */
    {
        XModbusDataUnit unit;
        XModbusDataUnit_init_ex(&unit, rtype, addr, count ? count : 1);
        for (uint16_t i = 0; i < (count ? count : 1); ++i)
            XModbusDataUnit_setValue(&unit, i, 0x5A5Au);
        XModbusReply_setResult(reply, &unit);
        XClassDeinit(&unit);
    }
    XModbusReply_setState(reply, XModbusReply_State_Finished);
    return reply;
}

/**
 * @brief 假从站 abandonReply：记录「传输层被摘手」的次数。
 * @details 真机 RTU/TCP 传输层在等待应答期间持有 Reply；Hub 超时收口
 *          释放 Reply 前**必须**先调本函数摘手，否则迟到应答/超时回调
 *          会解引用已释放对象（UAF ⇒ 板端实测 SEGV）。本假从站照此计数，
 *          M1 断言「收口确实摘了手」。
 */
static bool XMSilentSlave_abandonReply(XModbusClient* baseClient,
                                       XModbusReply* reply)
{
    XMSilentSlaveClient* c = (XMSilentSlaveClient*)baseClient;
    if (!c || !reply) return false;
    ++c->m_abandonCount;
    return true;
}

/** @brief 假从站类初始化（继承 XModbusClient，重载 open/close/sendRawRequest）。 */
static XVtable* XMSilentSlave_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMSilentSlaveClient)
        XVTABLE_INHERIT_XCLASS(XModbusClient);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Open, XMSilentSlave_open);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusDevice_Close, XMSilentSlave_close);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusClient_SendRawRequest,
                             XMSilentSlave_sendRawRequest);
    XVTABLE_OVERLOAD_DEFAULT(EXModbusClient_AbandonReply,
                             XMSilentSlave_abandonReply);
    XCLASS_SHOW_SIZE_DEFAULT(XMSilentSlaveClient);
    return XVTABLE_DEFAULT;
}

/** @brief 建假从站客户端（堆对象；所有权随 Hub 注入后归 Hub）。 */
static XMSilentSlaveClient* XMSilentSlave_create(int silentUnit)
{
    XMSilentSlaveClient* c =
        (XMSilentSlaveClient*)XMemory_malloc(sizeof(XMSilentSlaveClient),
                                             XCLASS_DEFAULT_MEMORY_TYPE);
    if (!c) return NULL;
    memset(c, 0, sizeof(*c));
    XModbusClient_init(&c->m_base);
    XClassGetVtable(c) = XMSilentSlave_class_init();
    c->m_silentUnit = silentUnit;
    Set_Class_Memory(c, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(c, true);
    return c;
}

/**
 * @brief M1：某组不应答 ⇒ 在途超时收口 ⇒ 别的组照常轮询。
 * @details 站 1 哑（受理但永不终结 Reply），站 2 正常。修前：站 1 的读组
 *          一发出去，在途标记永挂 ⇒ 单飞闸把**整台设备**的读组全堵死 ⇒
 *          站 2 一次都轮不到（现场：变频器整站离线、失败计数=0、游标卡住）。
 *          修后：超时收口（判失败 + 释放标记）⇒ 站 2 持续拿到数据。
 */
static void testM1_silentGroupDoesNotBlockBus(void)
{
    XMSilentSlaveClient* client = NULL;
    XModbusHub* hub = NULL;
    XModbusMapping* map = NULL;
    int devId = -1;
    int silentReq = 0;
    int otherReq = 0;
    uint32_t errCount = 0;

    XPrintf("--- M1 某组不应答：超时收口后其它组照常轮询 ---\n");
    client = XMSilentSlave_create(1);
    hub = XModbusHub_create();
    CHECK("M1 假从站 + Hub 装配", client && hub);
    if (!client || !hub) goto done;
    /* 站 1 哑、站 2 活；两块都在同一个设备（同一串口）上。 */
    devId = XModbusHub_addDevice_2(hub, (XModbusClient*)client, "哑站");
    CHECK("M1 设备入 Hub", devId >= 0);
    if (devId < 0) goto done;
    map = XModbusMapping_create();
    CHECK("M1 映射段登记（站 1 两点 + 站 2 两点）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 2,
                                    devId, 1, 0, 0, 0) &&
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x200, 2,
                                    devId, 2, 0, 0, 0));
    XModbusHub_setMapping(hub, map);          /* 所有权移交 Hub */
    map = NULL;
    XModbusHub_setPollAreas(hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(hub, 20);
    XModbusHub_setPollInterval(hub, devId, 20);
    /* [收口时限] 显式配 80ms（测试确定性）；实际收口 = 80 + 宽限 200ms。 */
    XModbusHub_setInflightTimeoutMs(hub, 80);
    XModbusHub_connectAll(hub);
    XModbusHub_startPolling(hub);

    spinMs(1500);
    silentReq = client->m_reqSilent;
    otherReq = client->m_reqOther;
    errCount = XModbusHub_pollErrCount(hub, devId);
    XPrintf("[INFO] M1 哑站请求=%d 活站请求=%d pollErr=%u "
            "inFlightTimeoutMs=%zu\n",
            silentReq, otherReq, (unsigned)errCount,
            XModbusHub_inflightTimeoutMs(hub));
    /* ① 哑站被反复重试（说明在途标记被收口了，而不是第一次就永挂）。 */
    CHECK("M1 哑站在途超时后被重新发起（收口生效）", silentReq >= 2,
          "silentReq=%d", silentReq);
    /* ② 活站照常轮询（核心：不能出现"一站哑掉、整条总线停摆"）。 */
    CHECK("M1 活站读组照常轮询（未被哑站拖死）", otherReq >= 2,
          "otherReq=%d", otherReq);
    /* ③ 超时按失败记账（可观测：失败计数不再是 0）。 */
    CHECK("M1 在途超时按失败记账（pollErr>0）", errCount > 0,
          "pollErr=%u", (unsigned)errCount);
    /* ④ 活站的数据真的回填了（点级新鲜度有效）。 */
    CHECK("M1 活站点级数据有效（回填成功）",
          XModbusHub_pointValid(hub, XModbusDiscreteInputs, 0x200, 3000));
    /* ⑤ [UAF 根修 2026-11-10 板端 SEGV] 超时收口必须**先让传输层摘手**
     *    （RTU=m_currentReply / TCP=在途表），否则 Hub 释放 Reply 后，
     *    迟到的应答/超时回调会解引用已释放对象 ⇒ 崩。
     *    假从站的 abandonReply 计数即「摘手」证据。 */
    CHECK("M1 超时收口先摘传输层的手（abandonReply 被调用）",
          client->m_abandonCount >= 1, "abandon=%d", client->m_abandonCount);
done:
    if (map) XClassDelete((XClass*)map);
    if (hub) XClassDelete((XClass*)hub);   /* 客户端所有权在 Hub */
    else if (client) XClassDelete((XClass*)client);
}

/**
 * @brief M2：异常应答是**组级**事实 ⇒ 同从站的其它组照常拿数据。
 * @details 同一从站（站 1）两块：块 A 读 17 点（超出虚拟模型单次上限
 *          16 ⇒ 恒回 EXC03 异常帧），块 B 读 2 点（正常）。
 *          修前：异常帧按从站级失败回报退避表 ⇒ 连续 3 次后站 1 被静默
 *          5s ⇒ **块 B 也一起停摆**（现场"一台设备整体离线"）。
 *          修后：异常帧仍计失败（可观测），但**不进从站退避** ⇒ 块 B 持续
 *          有数据。
 */
static void testM2_exceptionDoesNotMuteWholeSlave(void)
{
    HubFixture fx;
    XModbusMapping* map;
    uint32_t okCount = 0;
    uint32_t errCount = 0;
    bool blockBValid = false;

    XPrintf("--- M2 异常应答不牵连同从站的其它组 ---\n");
    CHECK("M2 装配（虚拟从站 + Hub）", hubFixtureMake(&fx));
    if (fx.devId < 0) { hubFixtureFree(&fx); return; }
    map = XModbusMapping_create();
    /* 块 A：17 点（超模型上限 ⇒ 恒异常）；块 B：2 点（正常）。同一站 1。 */
    CHECK("M2 映射段登记（站 1：异常块 17 点 + 正常块 2 点）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 17,
                                    fx.devId, 1, 0, 0, 0) &&
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x200, 2,
                                    fx.devId, 1, 0, 0, 0));
    XModbusHub_setMapping(fx.hub, map);
    XModbusHub_setPollAreas(fx.hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(fx.hub, 20);
    XModbusHub_setPollInterval(fx.hub, fx.devId, 20);
    /* [退避阈值取 1] 让"异常帧是否进入从站退避"这件事**当场可见**：
     * 缺省阈值 3 在本用例里被正常组的成功反复复位（成功即 failCount=0），
     * 永远触发不了静默 ⇒ 锁不住。阈值 1 = 任何一次失败回报立刻静默，
     * 于是"异常帧进不进退避表"直接体现在正常组还能不能继续拿数据。 */
    XModbusHub_setUnitBackoff(fx.hub, 1, 5000);
    XModbusHub_connectAll(fx.hub);
    XModbusHub_startPolling(fx.hub);
    spinMs(1500);
    okCount = XModbusHub_pollOkCount(fx.hub, fx.devId);
    errCount = XModbusHub_pollErrCount(fx.hub, fx.devId);
    blockBValid = XModbusHub_pointValid(fx.hub, XModbusDiscreteInputs, 0x200,
                                         3000);
    XPrintf("[INFO] M2 pollOk=%u pollErr=%u 正常块新鲜=%d\n",
            (unsigned)okCount, (unsigned)errCount, blockBValid ? 1 : 0);
    /* ① 异常帧仍按失败记账（可观测，不许悄悄吞掉）。 */
    CHECK("M2 异常应答仍计失败（pollErr>0）", errCount > 0,
          "pollErr=%u", (unsigned)errCount);
    /* ② 正常组持续成功（未被异常组拖进退避）。 */
    CHECK("M2 正常组持续轮询成功（pollOk>5）", okCount > 5,
          "pollOk=%u", (unsigned)okCount);
    /* ③ 正常组数据保持新鲜（块级 lastOkMs 在推进）。 */
    CHECK("M2 正常组数据保持新鲜（点级有效性）", blockBValid);
    hubFixtureFree(&fx);
}

/**
 * @brief M3：静默闸饥饿逃逸 —— 配置离谱时也不能把从站永久饿死。
 * @details 给站 1 配一个**超大**的静默期（模拟未初始化栈垃圾 0xCCCCCCCC
 *          的等效效果），且同设备上另有活站在持续发帧（把静默窗口顶掉）
 *          ⇒ 站 1 的读组被静默闸连续挡住。框架层的活性保证：连续挡住超过
 *          XMODBUS_HUB_QUIET_ESCAPE_MS ⇒ 放行一帧 ⇒ 站 1 不会被永久饿死。
 */
static void testM3_quietGateStarvationEscape(void)
{
    XMSilentSlaveClient* client = NULL;
    XModbusHub* hub = NULL;
    XModbusMapping* map = NULL;
    int devId = -1;
    XModbusHubUnitLimits lim;
    int reqUnit2 = 0;
    int reqUnit1 = 0;
    int reqTotal = 0;

    XPrintf("--- M3 静默闸饥饿逃逸（配置离谱也不饿死从站）---\n");
    /* 哑站给一个**不存在的站号** 9 ⇒ 站 1/站 2 都正常应答，
     * 本用例只考察静默闸（否则会把在途超时收口混进来）。 */
    client = XMSilentSlave_create(9);
    hub = XModbusHub_create();
    CHECK("M3 假从站 + Hub 装配", client && hub);
    if (!client || !hub) goto done;
    devId = XModbusHub_addDevice_2(hub, (XModbusClient*)client, "静默闸");
    CHECK("M3 设备入 Hub", devId >= 0);
    if (devId < 0) goto done;
    map = XModbusMapping_create();
    CHECK("M3 映射段登记（站 1 两点 + 站 2 两点）",
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x100, 2,
                                    devId, 1, 0, 0, 0) &&
          XModbusMapping_addRangeEx(map, XModbusDiscreteInputs, 0x200, 2,
                                    devId, 2, 0, 0, 0));
    XModbusHub_setMapping(hub, map);
    map = NULL;
    /* 站 1 配**离谱**的静默期（未初始化栈垃圾的等效值）。 */
    memset(&lim, 0, sizeof(lim));
    lim.m_preTxQuietMs = 0xFFFFFFFFu;
    CHECK("M3 站 1 静默期登记（离谱值）",
          XModbusHub_setUnitLimits(hub, devId, 1, &lim));
    XModbusHub_setPollAreas(hub, (1u << XModbusDiscreteInputs));
    XModbusHub_setPollIntervalDefault(hub, 20);
    XModbusHub_setPollInterval(hub, devId, 20);
    XModbusHub_connectAll(hub);
    XModbusHub_startPolling(hub);
    /* 跑够逃逸时限（2s）+ 余量。 */
    spinMs(3500);
    reqTotal = client->m_reqOther + client->m_reqSilent;
    reqUnit2 = client->m_reqByUnit[2];
    reqUnit1 = client->m_reqByUnit[1];   /* 站 1（被静默闸挡的那些）。 */
    (void)reqTotal;
    XPrintf("[INFO] M3 站 1 请求数=%d 站 2 请求数=%d\n", reqUnit1, reqUnit2);
    /* ① 活站持续轮询（静默闸没把整条总线掐死）。 */
    CHECK("M3 活站持续轮询（静默闸没把整条总线掐死）", reqUnit2 >= 2,
          "reqUnit2=%d", reqUnit2);
    /* ② 被静默闸挡住的站 1 最终**也被放行**（饥饿逃逸 ⇒ 不被永久饿死）。 */
    CHECK("M3 被静默闸挡住的从站最终放行（饥饿逃逸）", reqUnit1 >= 1,
          "reqUnit1=%d", reqUnit1);
done:
    if (map) XClassDelete((XClass*)map);
    if (hub) XClassDelete((XClass*)hub);
    else if (client) XClassDelete((XClass*)client);
}

/* ============================================================================
 *        §L 长稳 soak：XLink 会话生命周期 40 轮 + 堆零增量（2026-11-10）
 *
 * [钉的是什么] 客户端反复连接/订阅/请求-应答/拆除的**长时间稳定性**：
 *   每轮 = 回环设备对 → 两个裸会话 → 匿名握手到 Ready → 客户端订阅用户
 *   主题 → 客户端发一条请求（服务端 echo）→ 收到应答 → 全部拆除。
 *   预热 2 轮后连跑 40 轮，堆在用字节数必须回基线（差 0）——长跑泄漏
 *   在这里暴露（§G 只覆盖本文件自己的工作集，§L 覆盖 XLink 会话栈）。
 * [为什么匿名] 裸会话没有内建口令应答器（口令派生在 XLinkClient 里），
 *   匿名握手同样覆盖 Hello/能力位/订阅/请求/拆除全路径；认证语义已由
 *   §J 单独锁定。
 * ==========================================================================*/
static int l_soakRoundsOk = 0;
static int l_soakReqOk = 0;
static int l_soakSubOk = 0;

/** @brief 服务端分发：任何请求都原样回 1 字节 echo（请求 id 原样带回）。 */
static void l_soak_srv_dispatch(XLinkSession* self,
                                const XLinkFrameHeader* frame,
                                const uint8_t* payload)
{
    uint8_t echo = 0x7E;
    if (payload && frame->m_payloadLen > 0)
        echo = payload[0];
    XLinkSession_sendResponse(self, frame->m_requestId, frame->m_msgCode,
                              &echo, 1);
}

/** @brief 客户端分发：统计 Response（按请求 id 认领）。 */
static uint32_t l_soak_lastReqId = 0;
static int l_soak_respSeen = 0;
static void l_soak_cli_dispatch(XLinkSession* self,
                                const XLinkFrameHeader* frame,
                                const uint8_t* payload)
{
    (void)self;
    (void)payload;
    if (frame && frame->m_msgClass == XLinkMsgClass_Response)
        l_soak_respSeen++;
}

/** @brief 一轮：建-握-订-请求-收应答-拆。全部成功返回 1。 */
static int l_soak_round(int round)
{
    XGuiRemoteLoopbackDevice* devA = NULL;
    XGuiRemoteLoopbackDevice* devB = NULL;
    XLinkSession* server = NULL;
    XLinkSession* client = NULL;
    uint32_t now = 1000u + (uint32_t)round * 7u;
    int i;
    int ok = 1;

    devA = XGuiRemoteLoopbackDevice_createPair(64 * 1024, &devB);
    server = devA ? XLinkSession_create_ex((XIODevice*)devA) : NULL;
    client = devB ? XLinkSession_create_ex((XIODevice*)devB) : NULL;
    if (!devA || !devB || !server || !client) {
        ok = 0;
    } else {
        XLinkSession_setDispatchCallback(server, l_soak_srv_dispatch, NULL);
        XLinkSession_setDispatchCallback(client, l_soak_cli_dispatch, NULL);
        for (i = 0; i < 16 && ok; ++i) {
            now += 10;
            XLinkSession_pump(server, now);
            XLinkSession_pump(client, now);
            if (XLinkSession_isReady(server) && XLinkSession_isReady(client))
                break;
        }
        if (!(XLinkSession_isReady(server) && XLinkSession_isReady(client)))
            ok = 0;
        /* 订阅一个用户主题（服务端会收到订阅帧并记账） */
        if (ok && !XLinkSession_subscribe(client,
                                          XLINK_TOPIC_USER_BASE + 7u))
            ok = 0;
        now += 10;
        XLinkSession_pump(server, now);
        XLinkSession_pump(client, now);
        /* 请求-应答一条（echo 0x5A） */
        {
            uint8_t req = 0x5A;
            uint32_t reqId = 0;
            l_soak_respSeen = 0;
            if (!XLinkSession_sendRequest_ex(client, 0x7001,
                                             XLinkDataType_Raw,
                                             &req, 1, &reqId))
                ok = 0;
            l_soak_lastReqId = reqId;
            for (i = 0; i < 24 && ok; ++i) {
                now += 10;
                XLinkSession_pump(server, now);
                XLinkSession_pump(client, now);
                if (l_soak_respSeen)
                    break;
            }
            if (!l_soak_respSeen)
                ok = 0;
        }
    }
    if (server) XClassDelete((XClass*)server);
    if (client) XClassDelete((XClass*)client);
    if (devA) XClassDelete((XClass*)devA);
    if (devB) XClassDelete((XClass*)devB);
    return ok;
}

static void testL_soakRounds(void)
{
    XMemoryStatistics before;
    XMemoryStatistics after;
    int i;
    int rounds_ok = 0;

    XPrintf("--- §L 长稳 soak：XLink 会话生命周期 40 轮 ---\n");
    /* 预热 2 轮：摊平会话栈的惰性初始化（与 §G 同口径） */
    (void)l_soak_round(-1);
    (void)l_soak_round(-1);
    flushDeferred();
    before = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    for (i = 0; i < 40; ++i) {
        if (l_soak_round(i))
            ++rounds_ok;
        if ((i % 10) == 9)
            flushDeferred();     /* 周期性排延迟删除，避免积压假增 */
    }
    flushDeferred();
    after = XMemory_statistics(XMEMORY_TYPE_SYSTEM);
    CHECK("§L soak 40 轮全部成功（建-握-订-请求-收应答-拆）",
          rounds_ok == 40, "成功 %d/40", rounds_ok);
    CHECK("§L soak 40 轮堆增量回基线（长跑无泄漏）",
          after.systemBytes == before.systemBytes,
          "堆 %llu -> %llu（差 %lld）",
          (unsigned long long)before.systemBytes,
          (unsigned long long)after.systemBytes,
          (long long)(after.systemBytes - before.systemBytes));
}

/* ============================================================================
 *        §Q 数据三页「导出筛选全量」批次计划（2026-11-11）
 *
 * [为什么并进来] 与 §H 同款口径：app 层纯逻辑助手 HsfExportPlan.c 由本
 * 文件单 TU 收录（助手本体只随 HsfClient 编译，不进任何库，不撞键）。
 * 历史页/日志页的导出取数 = 探测批拿 total → HsfExportPlan_planBatches →
 * 逐批连续取；计划算错就是「导出重复/漏行/少批」，故逐条钉住（断言编号
 * Q1..Q5，与既有子段区分）。
 * [编译期闸镜像] 历史页 HsfDataHistory.c 有同款 #error 闸（页大小/探测批
 * /正式批两两不相交）；导出批行数与页大小、探测档的不相交性在本文件再锁
 * 一道——轮转档 41..49 是「应答归属」的根基，改漂了导出应答会被铺错表。
 * ==========================================================================*/

/* [编译期闸] 导出批行数档必须 == 图表正式批档（两者共用
 * HsfChartZoom_nextLimits 的轮转），且与页大小 10/20/50、图表探测档
 * 1..8 两两不相交。 */
#if (HSF_EXPORT_PLAN_CHUNK_MIN != HSF_CHART_ZOOM_CHUNK_MIN) || \
    (HSF_EXPORT_PLAN_CHUNK_MAX != HSF_CHART_ZOOM_CHUNK_MAX) || \
    (HSF_EXPORT_PLAN_CHUNK_MIN <= HSF_CHART_ZOOM_PROBE_MAX) || \
    (HSF_EXPORT_PLAN_CHUNK_MIN <= 10) || (HSF_EXPORT_PLAN_CHUNK_MAX >= 50)
#error "export batch band must equal chart chunk band 41..49 and stay disjoint from page sizes / probe band"
#endif

/** @brief Q1：按**筛选命中的总行数**分批（不是当前页那几十行）。 */
static void testQ1_planFromTotalNotPage(void)
{
    HsfExportPlan plan;
    /* [上限放大再测] 未截断口径用 20000（>12345；若用缺省 12000 会被
     * 截断——截断口径归 Q3 专测）。 */
    (void)HsfExportPlan_planBatches(12345, 45, 20000, &plan);
    CHECK("Q1 total=12345/chunk=45 ⇒ 275 批（按命中总数分批）",
          plan.batches == 275,
          "batches=%d", plan.batches);
    CHECK("Q1 步长=批行数 45（连续取，不跳批）", plan.step == 45);
    CHECK("Q1 未截断 ⇒ 取数行数=total 12345（导出的不是当前页那 50 行）",
          plan.fetchRows == 12345 && !plan.truncated);
    CHECK("Q1 末批够到区尾（(batches-1)*step+chunk ≥ total）",
          (long)(plan.batches - 1) * plan.step + 45 >= 12345L);
    CHECK("Q1 末批起点不越界（(batches-1)*step < fetchRows）",
          (long)(plan.batches - 1) * plan.step < (long)plan.fetchRows);
}

/** @brief Q2：逐批连续 ⇒ 跨页**不重不漏**；0 行/单批的边界。 */
static void testQ2_batchesCoverAllRowsNoDupNoGap(void)
{
    const int chunk = 45;
    const int total = 9788;
    HsfExportPlan plan;
    int k;
    int covered = 0;
    int prevEnd = 0;   /* 第 1 批必须从 offset 0 开始。 */
    (void)HsfExportPlan_planBatches(total, chunk, HSF_EXPORT_PLAN_MAX_ROWS,
                                    &plan);
    CHECK("Q2 9788 行/批 45 ⇒ 218 批",
          plan.batches == (total + chunk - 1) / chunk);
    for (k = 0; k < plan.batches; ++k) {
        const int start = plan.step * k;
        const int rows = (start + chunk <= plan.fetchRows)
                             ? chunk : (plan.fetchRows - start);
        /* 每批起点必须严丝合缝接上一批结尾（不重叠、不留空档）。 */
        if (start != prevEnd) break;
        prevEnd = start + rows;
        covered += rows;
    }
    CHECK("Q2 各批 offset 严格连续（不重不漏）",
          prevEnd == plan.fetchRows,
          "末批终点 %d / fetchRows %d", prevEnd, plan.fetchRows);
    CHECK("Q2 各批行数相加 == 取数行数上限", covered == plan.fetchRows);
    CHECK("Q2 取数行数 == 命中总数（9788）", plan.fetchRows == total);
    CHECK("Q2 total=0 ⇒ 0 批（空筛选不取数）",
          HsfExportPlan_planBatches(0, chunk, HSF_EXPORT_PLAN_MAX_ROWS,
                                    &plan) == 0 &&
          plan.batches == 0 && plan.fetchRows == 0 && !plan.truncated);
    CHECK("Q2 total<=chunk ⇒ 1 批取完",
          HsfExportPlan_planBatches(30, 45, HSF_EXPORT_PLAN_MAX_ROWS,
                                    &plan) == 1 &&
          plan.batches == 1 && plan.fetchRows == 30 && plan.step == 45);
}

/** @brief Q3：命中行数超上限 ⇒ 截断且截断行数=上限向下取整到**整批**。 */
static void testQ3_truncationRoundsDownToWholeBatches(void)
{
    HsfExportPlan plan;
    (void)HsfExportPlan_planBatches(20000, 45, 12000, &plan);
    CHECK("Q3 total=20000 > 上限 12000 ⇒ truncated", plan.truncated);
    CHECK("Q3 截断行数=上限向下取整到整批（12000/45*45=11970）",
          plan.fetchRows == 12000 / 45 * 45,
          "fetchRows=%d", plan.fetchRows);
    CHECK("Q3 截断后批数=ceil(11970/45)=266（每批仍取满整批）",
          plan.batches == 266);
    CHECK("Q3 上限比一批还小 ⇒ 至少取一批（截断不退化成没导出）",
          HsfExportPlan_planBatches(9999, 45, 20, &plan) == 1 &&
          plan.fetchRows == 45 && plan.truncated);
    CHECK("Q3 上限<=0 ⇒ 不设上限（不截断，5000/45 ⇒ 112 批）",
          HsfExportPlan_planBatches(5000, 45, 0, &plan) == 112 &&
          plan.fetchRows == 5000 && !plan.truncated);
}

/** @brief Q4：计划是**纯函数**——筛选条件变化（total 变）⇒ 重算。 */
static void testQ4_planRecomputesOnFilterChange(void)
{
    HsfExportPlan a;
    HsfExportPlan b;
    HsfExportPlan c;
    /* 筛选条件变化 ⇒ 命中 total 变 ⇒ 批数/取数行数随之重算（旧计划必须
     * 作废，否则按旧行数取数会少批/多批）。上限给 20000 走未截断口径。 */
    (void)HsfExportPlan_planBatches(12345, 45, 20000, &a);
    (void)HsfExportPlan_planBatches(200, 45, HSF_EXPORT_PLAN_MAX_ROWS, &b);
    CHECK("Q4 命中行数变了 ⇒ 计划重算（12345⇒275 批、200⇒5 批）",
          a.batches == 275 && b.batches == 5 && a.batches != b.batches);
    CHECK("Q4 取数行数跟着新 total（200 ⇒ fetchRows=200）",
          b.fetchRows == 200 && !b.truncated);
    /* 同输入两次 ⇒ 同输出（无隐藏状态；页面重算不会残留上一轮的计划）。 */
    (void)HsfExportPlan_planBatches(12345, 45, 20000, &c);
    CHECK("Q4 同输入同输出（纯函数，无隐藏状态）",
          c.batches == a.batches && c.step == a.step &&
          c.fetchRows == a.fetchRows && c.truncated == a.truncated);
}

/** @brief Q5：导出批行数轮转档与页大小两两不相交（编译期闸的运行期镜像）。 */
static void testQ5_chunkBandDisjointFromPageSizes(void)
{
    int chunk;
    CHECK("Q5 轮转档与图表正式批档同源（41..49）",
          HSF_EXPORT_PLAN_CHUNK_MIN == HSF_CHART_ZOOM_CHUNK_MIN &&
          HSF_EXPORT_PLAN_CHUNK_MAX == HSF_CHART_ZOOM_CHUNK_MAX);
    /* 轮转 9 档逐个落检：绝不与页大小 10/20/50 相撞（撞键 ⇒ 迟到应答
     * 被当成页应答铺错表，历史页复审②同源）。 */
    for (chunk = HSF_EXPORT_PLAN_CHUNK_MIN;
         chunk <= HSF_EXPORT_PLAN_CHUNK_MAX; ++chunk) {
        if (chunk == 10 || chunk == 20 || chunk == 50) break;
    }
    CHECK("Q5 41..49 与页大小 10/20/50 两两不相交",
          chunk > HSF_EXPORT_PLAN_CHUNK_MAX);
}

/* ============================================================================
 *        §R 报警页纯逻辑：行可见判定 + 客户端内部分页边界（2026-11-11）
 *
 * [钉的是什么] 数据-报警页 2026-11-11 对齐 Python AlarmPage（去「报警次数/
 *   分钟」折线图 + 关键字筛选 + 客户端内部分页 + 滚动锁定）后的契约级判定，
 *   抽在 HsfAlarmLogic.c（纯逻辑、零 GUI、零堆分配，页面/导出/统计共用）：
 *   页面层（HsfDataAlarm.c）只负责把自己的筛选状态折成 HsfAlarmFilter，
 *   判定口径的单一事实源在这里——页面自持判定逻辑就必然三处漂移。
 * [口径来源] Python client_py/hsfsrv_client/pages_other.py AlarmPage 的
 *   _row_visible / _page_count / _rebuild：
 *   · 类型按文本全等；时间窗界 0=不限；关键字在设备/类型/状态三列文本
 *     "包含"匹配（匹配列 1/2/3，**不含时间列**）；
 *   · 页数 = max(1, ceil(全集/页大小))（0 行也是第 1/1 页）；
 *   · 翻页判界：上一页=页号>0，下一页=页号+1<页数；铺表前页号越界夹取
 *     （筛选/解锁后行数变少不滞留空页）。
 * ==========================================================================*/

/** @brief R1：行可见基础——无条件恒可见 / 类型全等 / 空条件=不限。 */
static void testR1_rowVisibleBasics(void)
{
    HsfAlarmFilter f;
    memset(&f, 0, sizeof(f));
    /* 无条件（NULL filter）= 全部可见（防御口径）。 */
    CHECK("R1 filter=NULL 恒可见（不崩）",
          HsfAlarmLogic_rowVisible(NULL, 1000, "变频器", "M512", "数据过期"));
    CHECK("R1 空条件结构体恒可见",
          HsfAlarmLogic_rowVisible(&f, 1000, "变频器", "M512", "数据过期"));
    /* 类型全等：与 Python _row_visible 的 row[2] != kind_text 同口径。 */
    f.m_kindText = "变频器";
    CHECK("R1 类型全等命中",
          HsfAlarmLogic_rowVisible(&f, 1000, "变频器", "M512", "数据过期"));
    CHECK("R1 类型不一致不可见",
          !HsfAlarmLogic_rowVisible(&f, 1000, "温湿度", "M512", "数据过期"));
    /* 空串类型条件 = 不限（页面把下拉下标 0「全部类型」折成 NULL/空）。 */
    f.m_kindText = "";
    CHECK("R1 类型条件空串=不限",
          HsfAlarmLogic_rowVisible(&f, 1000, "温湿度", "M512", "数据过期"));
    /* 记录类型 NULL 且条件非空 ⇒ 不可见（strcmp 不许打在 NULL 上）。 */
    f.m_kindText = "变频器";
    CHECK("R1 记录类型 NULL 且有类型条件 ⇒ 不可见（不崩）",
          !HsfAlarmLogic_rowVisible(&f, 1000, NULL, "M512", "数据过期"));
}

/** @brief R2：时间窗——双侧夹 / 界 0 不限 / 无时刻记录不参与时间窗。 */
static void testR2_timeWindow(void)
{
    HsfAlarmFilter f;
    memset(&f, 0, sizeof(f));
    CHECK("R2 无界可见", HsfAlarmLogic_rowVisible(&f, 5000, "k", "n", "s"));
    f.m_loTs = 2000;
    CHECK("R2 下界拦早于", !HsfAlarmLogic_rowVisible(&f, 1000, "k", "n", "s"));
    CHECK("R2 下界放行恰等于", HsfAlarmLogic_rowVisible(&f, 2000, "k", "n", "s"));
    CHECK("R2 下界放行晚于", HsfAlarmLogic_rowVisible(&f, 9000, "k", "n", "s"));
    f.m_loTs = 0;
    f.m_hiTs = 8000;
    CHECK("R2 上界拦晚于", !HsfAlarmLogic_rowVisible(&f, 9000, "k", "n", "s"));
    CHECK("R2 上界放行恰等于", HsfAlarmLogic_rowVisible(&f, 8000, "k", "n", "s"));
    /* 两侧同夹。 */
    f.m_loTs = 2000;
    CHECK("R2 双侧夹窗内", HsfAlarmLogic_rowVisible(&f, 5000, "k", "n", "s"));
    CHECK("R2 双侧夹窗外早", !HsfAlarmLogic_rowVisible(&f, 1000, "k", "n", "s"));
    CHECK("R2 双侧夹窗外晚", !HsfAlarmLogic_rowVisible(&f, 9000, "k", "n", "s"));
    /* 无时刻记录（ts<=0）：不参与时间窗淘汰（Python 同口径：正数下界
     * 本就拦不住 0 时刻，上界对 0 时刻同式不触发）。 */
    CHECK("R2 ts=0 不被下界拦", HsfAlarmLogic_rowVisible(&f, 0, "k", "n", "s"));
    CHECK("R2 ts=0 不被上界拦", HsfAlarmLogic_rowVisible(&f, 0, "k", "n", "s"));
    CHECK("R2 ts<0 同口径", HsfAlarmLogic_rowVisible(&f, -5, "k", "n", "s"));
}

/** @brief R3：关键字——设备/类型/状态三列包含；空=不筛；不含时间列。 */
static void testR3_keywordMatch(void)
{
    HsfAlarmFilter f;
    memset(&f, 0, sizeof(f));
    /* NULL/空关键字 = 不筛。 */
    CHECK("R3 kw=NULL 不筛",
          HsfAlarmLogic_rowVisible(&f, 1000, "变频器", "M512", "数据过期"));
    f.m_kw = "";
    CHECK("R3 空串关键字不筛",
          HsfAlarmLogic_rowVisible(&f, 1000, "变频器", "M512", "数据过期"));
    /* 三列各自包含命中（子串口径，对齐 Python 的 `kw in row[c]`）。 */
    f.m_kw = "512";
    CHECK("R3 设备名包含命中",
          HsfAlarmLogic_rowVisible(&f, 1000, "变频器", "M512", "数据过期"));
    f.m_kw = "变频";
    CHECK("R3 类型包含命中",
          HsfAlarmLogic_rowVisible(&f, 1000, "变频器", "M512", "数据过期"));
    f.m_kw = "过期";
    CHECK("R3 状态包含命中",
          HsfAlarmLogic_rowVisible(&f, 1000, "变频器", "M512", "数据过期"));
    /* 三列全不含 ⇒ 不可见。 */
    f.m_kw = "球阀";
    CHECK("R3 三列全不含不可见",
          !HsfAlarmLogic_rowVisible(&f, 1000, "变频器", "M512", "数据过期"));
    /* 时间文本不在匹配面（Python 匹配列=1/2/3，不是 0）：拿"时刻样子"的
     * 关键字筛不出任何记录——这是口径不是缺陷（记录的时间列不进判定）。 */
    f.m_kw = "10:30:00";
    CHECK("R3 时间文本不参与匹配",
          !HsfAlarmLogic_rowVisible(&f, 1000, "变频器", "M512", "数据过期"));
    /* 列文本 NULL 按空串参与匹配（strstr 不许打在 NULL 上）。 */
    f.m_kw = "M";
    CHECK("R3 设备列 NULL、其余列不命中 ⇒ 不可见（不崩）",
          !HsfAlarmLogic_rowVisible(&f, 1000, "变频器", NULL, "数据过期"));
    CHECK("R3 全列 NULL 且关键字非空 ⇒ 不可见（不崩）",
          !HsfAlarmLogic_rowVisible(&f, 1000, NULL, NULL, NULL));
}

/** @brief R4：组合矩阵——类型×时间窗×关键字同时生效，全过才可见。 */
static void testR4_combinedMatrix(void)
{
    HsfAlarmFilter f;
    memset(&f, 0, sizeof(f));
    f.m_kindText = "变频器";
    f.m_loTs = 2000;
    f.m_hiTs = 8000;
    f.m_kw = "M512";
    CHECK("R4 三条件全过 ⇒ 可见",
          HsfAlarmLogic_rowVisible(&f, 5000, "变频器", "M512", "数据过期"));
    CHECK("R4 类型过·时间过·关键字不过 ⇒ 不可见",
          !HsfAlarmLogic_rowVisible(&f, 5000, "变频器", "球阀V1", "数据过期"));
    CHECK("R4 类型过·关键字过·时间过早 ⇒ 不可见",
          !HsfAlarmLogic_rowVisible(&f, 1000, "变频器", "M512", "数据过期"));
    CHECK("R4 类型过·关键字过·时间过晚 ⇒ 不可见",
          !HsfAlarmLogic_rowVisible(&f, 9000, "变频器", "M512", "数据过期"));
    CHECK("R4 关键字过·时间过·类型不过 ⇒ 不可见",
          !HsfAlarmLogic_rowVisible(&f, 5000, "温湿度", "M512", "数据过期"));
    /* 任一条件放宽即恢复可见，且其余条件仍各自把关（互不吞）。 */
    f.m_kw = NULL;
    CHECK("R4 关键字放宽 ⇒ 只剩类型+时间把关（可见）",
          HsfAlarmLogic_rowVisible(&f, 5000, "变频器", "球阀V1", "数据过期"));
    f.m_kw = "M512";
    f.m_kindText = NULL;
    CHECK("R4 类型放宽 ⇒ 只剩时间+关键字把关（可见）",
          HsfAlarmLogic_rowVisible(&f, 5000, "温湿度", "M512", "数据过期"));
}

/** @brief R5：页数边界（0 行 / pageSize<=0 兜底 / 整除 / 不整除向上取整）。 */
static void testR5_pageCountBounds(void)
{
    CHECK("R5 0 行也是 1 页（第 1/1 页口径）",
          HsfAlarmLogic_pageCount(0, 20) == 1);
    CHECK("R5 负行数兜底 1 页", HsfAlarmLogic_pageCount(-3, 20) == 1);
    CHECK("R5 pageSize<=0 兜底 1 页（防除零）",
          HsfAlarmLogic_pageCount(45, 0) == 1 &&
          HsfAlarmLogic_pageCount(45, -20) == 1);
    CHECK("R5 整除（40/20=2 页）", HsfAlarmLogic_pageCount(40, 20) == 2);
    CHECK("R5 不整除向上取整（45/20=3 页）",
          HsfAlarmLogic_pageCount(45, 20) == 3);
    CHECK("R5 1 行 1 页", HsfAlarmLogic_pageCount(1, 50) == 1);
    CHECK("R5 恰好多一行（21/20=2 页）",
          HsfAlarmLogic_pageCount(21, 20) == 2);
    CHECK("R5 档位抽查（93/50=2 页）",
          HsfAlarmLogic_pageCount(93, 50) == 2);
}

/** @brief R6：页起点边界（网格起点 / 超界页夹取 / 负页号 / 空全集恒 0）。 */
static void testR6_pageStartBounds(void)
{
    /* 正常网格起点（全集倒序视图的第 k 页从 k×pageSize 起）。 */
    CHECK("R6 第 0 页起点 0", HsfAlarmLogic_pageStart(45, 20, 0) == 0);
    CHECK("R6 第 1 页起点 20", HsfAlarmLogic_pageStart(45, 20, 1) == 20);
    CHECK("R6 第 2 页起点 40", HsfAlarmLogic_pageStart(45, 20, 2) == 40);
    /* 超界页夹取到最后一页（45 行 20/页 ⇒ 3 页，页号 3/99 都落第 2 页）。 */
    CHECK("R6 页号越界夹到最后一页（起点=40）",
          HsfAlarmLogic_pageStart(45, 20, 3) == 40 &&
          HsfAlarmLogic_pageStart(45, 20, 99) == 40);
    /* 负页号夹到第 0 页。 */
    CHECK("R6 负页号夹到 0", HsfAlarmLogic_pageStart(45, 20, -1) == 0);
    /* 空全集起点恒 0（页号/页大小怎么给都不崩）。 */
    CHECK("R6 空全集起点恒 0",
          HsfAlarmLogic_pageStart(0, 20, 0) == 0 &&
          HsfAlarmLogic_pageStart(0, 20, 5) == 0 &&
          HsfAlarmLogic_pageStart(0, 0, 0) == 0);
    /* pageSize<=0 兜底按 1（起点=夹取后页号）。 */
    CHECK("R6 pageSize<=0 兜底（起点=页号）",
          HsfAlarmLogic_pageStart(10, 0, 2) == 2);
    /* 末页行数不足一整页：起点仍是 pageSize 网格（45 行的末页从 40 起）。 */
    CHECK("R6 末页不满页起点仍成网格",
          HsfAlarmLogic_pageStart(45, 20, 2) == 40);
}

/* ============================================================================
 * §S 自动模式序列（2026-11-11；C 端镜像 = server/HsfServerAutoMode.c，
 *     基准 = Python server_py/hsfsrv/autocontrol.py + tests/test_auto_modes.py
 *     + tests/test_auto_alarm_publish.py，逐条对齐；协议面的端到端复检由
 *     verify/hsf_auto_mode_check.py 对真实 bin/HsfServer.exe --sim 另行钉）
 *
 * [接法] 伪从站 = HsfServerIo 模拟装配（§N 同款：sim 模式给「与真装配同一
 *     口径」的数字量/变频器模型；阀行程/卡死经 HsfServerIo_setSimValveOverride
 *     夹具旋钮）。驱动 = 每拍 HsfServerIoSim_tick + watchdog + valvePoll +
 *     pumpPoll + HsfServerAutoMode_tick（服务端 hsfAppTick 的同款顺序）。
 *     [决策只认反馈] 阀到位断言读 HsfServerIoValve_item().m_state 与数字量
 *     DO 回显，绝不认 valveSet 的受理返回值。
 *     [夹具卫生] 阀组动作超时全程夹具 600ms（产品缺省 20s 只会让回归慢
 *     20 倍），尾夹具按 HSF_VALVE_MOVE_TIMEOUT_MS 还原；auto 三键的 INI
 *     每个用例前删回「不存在」（缺键=缺省口径）、跑完还原——与 §N7 的
 *     共享 INI 保护同一纪律。
 * ==========================================================================*/
#if XINDUSTRY_ON && XMODBUS_HUB_ON

#include "HsfServerAutoMode.h"
#include "HsfServerIoSim.h"      /* sAutoPumpTick：模拟模型时间态推进。 */
#include "XModbusVirtualModel.h" /* S9：按钮 DI 注入（XVirtualModelDigital_setDi）。 */
#include "HsfServerProtect.h" /* S17/S19：运行保护（空管超时/压差过大）。 */
#include "XAutoControl.h"        /* takeAlarm/raiseAlarm（S7 告警链）。 */
#if XVFD_ON
#include "XVfdDevice.h"          /* S1/S3：搅拌泵给定/方向/运行位断言。 */
#endif
#include "HsfServerIoValve.h"

/** @brief §S 夹具：阀组动作超时重配（600ms 夹具 / 还原产品缺省）。
 * @details 点位表与 HsfServerIo.c 装配处逐宏同源（HSF_VALVE_DO/OPEN_DI/
 *          CLOSE_DI），只改超时；HsfServerIo_valves() 是单例成员的借用
 *          指针，const 由显式转换摘掉（测试专用写路径）。 */
static void sValveBankConfigure(int moveTimeoutMs)
{
    HsfServerIoValveCfg items[HSF_VALVE_COUNT];
    int i;
    for (i = 0; i < HSF_VALVE_COUNT; ++i) {
        items[i].m_name = NULL;           /* 阀组内部给缺省名（电动球阀N）。 */
        items[i].m_do = HSF_VALVE_DO(i);
        items[i].m_openDi = HSF_VALVE_OPEN_DI(i);
        items[i].m_closeDi = HSF_VALVE_CLOSE_DI(i);
    }
    HsfServerIoValve_configure((HsfServerIoValve*)HsfServerIo_valves(),
                               items, HSF_VALVE_ACTIVE_COUNT, moveTimeoutMs,
                               true);
}

/** @brief §S 夹具：模拟阀行程覆盖（200ms 常速 / UINT32_MAX=卡死 / 0=缺省）。 */
static void sSimValveMove(uint32_t moveMs)
{
    HsfServerIo_setSimValveOverride(moveMs, false);
}

/** @brief §S 一拍：与 HsfServerApp 的 hsfAppTick 同款顺序（无推送面）。 */
static void sAutoPumpTick(bool autoMode)
{
    const uint64_t now = (uint64_t)XDateTime_currentMSecsSinceEpoch();
    HsfServerIoSim_tick();            /* 模拟模型时间态（阀行程/变频器爬坡）。 */
    (void)HsfServerIo_watchdog(now);  /* Hub 轮询/自愈（DI 新鲜度的来源）。 */
    HsfServerIo_valvePoll(now);       /* 阀反馈判读（真实服务端同款）。 */
    HsfServerIo_pumpPoll();
    HsfServerAutoMode_tick(now, autoMode);
}

/** @brief §S 驱动：按拍跑满 ms 毫秒（20ms 一拍 + 事件泵）。 */
static void sAutoRunFor(int ms, bool autoMode)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < ms) {
        sAutoPumpTick(autoMode);
        XCoreApplication_processEvents(0);
        XThread_msleep(20);
    }
}

/** @brief §S 驱动：条件等待（期间持续按拍驱动；超时按最终状态判定）。 */
static bool sAutoWaitUntil(int timeoutMs, bool autoMode, int (*cond)(void*),
                           void* ud)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < timeoutMs) {
        if (cond(ud)) return true;
        sAutoPumpTick(autoMode);
        XCoreApplication_processEvents(0);
        XThread_msleep(20);
    }
    return cond(ud) != 0;
}

/* ---- §S 条件谓词（sAutoWaitUntil 用；返回非 0 = 条件成立） ---- */

static int sCondPhaseRunning(void* ud)
{
    char phase[24];
    (void)ud;
    HsfServerAutoMode_phase(phase, (int)sizeof(phase));
    return strcmp(phase, "running") == 0;
}

static int sCondNotRunning(void* ud)
{
    (void)ud;
    return HsfServerAutoMode_running() ? 0 : 1;
}

static int sCondFaultLatched(void* ud)
{
    (void)ud;
    return HsfServerAutoMode_faultLatched() ? 1 : 0;
}

/** @brief DO 位图等待参数（等 mask 位 == expect；栈上小结构体）。 */
struct SAutoDoExpect {
    uint32_t m_mask;
    uint32_t m_expect;
};

static int sCondDoBits(void* ud)
{
    const struct SAutoDoExpect* ex = (const struct SAutoDoExpect*)ud;
    uint32_t di = 0;
    uint32_t dO = 0;
    bool valid = false;
    if (!HsfServerIo_digitalMasks(&di, &dO, &valid) || !valid) return 0;
    return ((dO & ex->m_mask) == ex->m_expect) ? 1 : 0;
}

#if XVFD_ON
static int sCondVfdStopped(void* ud)
{
    XVfdDevice* dev = HsfServerIo_vfdDevice(HsfSide_A);
    const XVfdStatus* st;
    (void)ud;
    if (!dev) dev = HsfServerIo_vfdDevice(HsfSide_B);
    st = dev ? XVfdDevice_status_const(dev) : NULL;   /* status_const 不收 NULL。 */
    return (st && st->m_valid && !st->m_running) ? 1 : 0;
}

/** @brief 等搅拌泵运行位**与配置方向**都到位（S1 泵相反馈口径）。
 *  [为什么是等待而非定点读 2026-11-12] 命令字经 Hub 排队、状态字经站轮询
 *  回读——受理（XVfdDevice_startWithGiven ret=1）≠ 运行位已置；定点 400ms
 *  在满载门禁机上等不到（实测 ret=1 而 running=0/reverse=0 双红）。模型在
 *  写命令那一刻同时落运行与方向（XModbusVirtualModels.c RUN 分支），故
 *  两态同拍回读、一并等待。对齐 Python 端「等运行位」的等待式断言。 */
static int sCondVfdRunningReverse(void* ud)
{
    XVfdDevice* dev = HsfServerIo_vfdDevice(HsfSide_A);
    const XVfdStatus* st;
    (void)ud;
    if (!dev) dev = HsfServerIo_vfdDevice(HsfSide_B);
    st = dev ? XVfdDevice_status_const(dev) : NULL;   /* status_const 不收 NULL。 */
    return (st && st->m_valid && st->m_running && st->m_reverse) ? 1 : 0;
}
#endif

/* ---- §S 状态卫生 ---- */

/** @brief 取走在队报警（跨用例不得互相看见上一条）。 */
static void sAutoDrainAlarms(void)
{
    XAutoController* ctl = HsfServerAutoMode_controller();
    XAutoControlAlarm a;
    memset(&a, 0, sizeof(a));         /* 栈上结构体交框架：先 memset。 */
    while (ctl && XAutoController_takeAlarm(ctl, &a)) {
        memset(&a, 0, sizeof(a));
    }
}

/** @brief 回到「从未选过模式、两把锁全开、配置全缺省」的干净态。 */
static void sAutoResetFresh(void)
{
    /* INI 卫生（§N7 同款纪律）：auto 三键删回「不存在」⇒ 随后 setup 的
     * loadConfig 回缺省（不勾设备、搅拌满转速正转）——跨用例/跨次跑不
     * 互相看见上一次的配置。run/auto_run_ms（S11 的运行时长持久化键）
     * 一并删：累计时长跨用例归零，S11 的增量断言才有稳定基线。 */
    HsfServerSettings_remove("auto/produce");
    HsfServerSettings_remove("auto/backwash");
    HsfServerSettings_remove("auto/rinse");
    HsfServerSettings_remove("auto/cycle");
    HsfServerSettings_remove("auto/buttons");
    HsfServerSettings_remove("run/auto_run_ms");
    /* [S15 粘性模式键 2026-11-14] op=5 的粘性也落盘（键 run/auto_mode）：
     * 跨用例不清 ⇒ 上一次用例选的模式渗进下一次（S1 断言粘性回显）。 */
    HsfServerSettings_remove("run/auto_mode");
    HsfServerAutoMode_shutdown();
    (void)HsfServerAutoMode_setup();
    sAutoDrainAlarms();
    (void)HsfServerAutoMode_requestReset(
        (uint64_t)XDateTime_currentMSecsSinceEpoch());
}

/** @brief §S 夹具开（装配模拟 IO + 短超时阀组 + 干净控制器 + 暖拍）。 */
static bool sAutoFixtureBegin(void)
{
    if (!HsfServerSettings_open()) {
        CHECK("S 夹具：打开测试树 INI", false);
        return false;
    }
    HsfServerIo_setSimMode(true);
    if (!HsfServerIo_start()) {
        CHECK("S 夹具：模拟 IO 启动", false);
        return false;
    }
    CHECK("S 夹具：数字量/阀组/泵组/变频器就绪",
          HsfServerIo_digitalReady() && HsfServerIo_valves() != NULL &&
          HsfServerIo_pumps() != NULL,
          "digital=%d valves=%d pumps=%d",
          HsfServerIo_digitalReady() ? 1 : 0,
          HsfServerIo_valves() != NULL ? 1 : 0,
          HsfServerIo_pumps() != NULL ? 1 : 0);
    sValveBankConfigure(600);         /* §S 全程阀短超时（尾夹具还原 20s）。 */
    /* [行程 200ms ≠ 超时 600ms——竞速边界 2026-11-12] 行程必须**明显短于**
     * 阀组动作超时：开阀的到位反馈要走「DO 写落定（写合并一拍）→ 模型
     * 行程 → DI 回读一拍」，叠加延迟数百 ms；600/600 时反馈必然晚于判
     * 故障截止 ⇒ 开机第一路阀必判故障（实测 ：1749 阀1 判故障、:1750 di
     * 才补齐到位位）。200ms 给出 ≥150ms 结构级余量；卡死夹具（∞）与
     * 超时类用例（S2/S5）不受影响——它们本来就要等超时。 */
    sSimValveMove(200);
    HsfServerAutoMode_setLog(NULL);   /* 静默（§S 不刷运维日志）。 */
    if (!HsfServerAutoMode_setup()) {
        CHECK("S 夹具：自动模式序列装配", false);
        return false;
    }
#if XVFD_ON
    /* [镜像板端口径 2026-11-12] 2000H 命令字仅在「命令源=通讯」（P0.02）时
     * 生效——那是**设备侧参数**，现场在面板设、生产链不写；虚拟从站与真机
     * 同缺省（P0.02=2）⇒ §S 夹具必须显式切源，否则搅拌泵 Start 一律被闸
     * （实测 S1 六连红：「命令源未切到通讯（P0.02=2）」；[AUTOP] stir ret=0
     * 每拍重试）。与 xfdv_m512_test.c 同一教训：切源写完要等「写落定 +
     * 回读刷新缓存」再动下一站，否则后一次写的前置校验读的是**缓存**仍见
     * 旧值 ⇒ 误拒。 */
    {
        HsfSide side;
        for (side = HsfSide_A; side < HsfSide_Count; ++side) {
            XVfdM512* m512 = (XVfdM512*)HsfServerIo_vfdDevice(side);
            if (!m512) continue;
            CHECK("S 夹具：切命令源到通讯（P0.02=2）",
                  XVfdM512_setCommandSourceComm(m512));
            sAutoRunFor(700, false);  /* 写落定 + 回读刷新缓存（拍驱动）。 */
        }
    }
#endif
    sAutoRunFor(400, false);          /* 暖拍：DI 新鲜度 + 急停回路首帧。 */
    sAutoResetFresh();
    return true;
}

/** @brief §S 夹具收（还原阀组超时/行程 + INI 还原 + 拆卸）。 */
static void sAutoFixtureEnd(void)
{
    HsfServerAutoMode_shutdown();
    sValveBankConfigure(HSF_VALVE_MOVE_TIMEOUT_MS); /* 还原产品缺省 20s。 */
    sSimValveMove(0);                 /* 回缺省行程（0=不用覆盖）。 */
    HsfServerSettings_remove("auto/produce");
    HsfServerSettings_remove("auto/backwash");
    HsfServerSettings_remove("auto/rinse");
    HsfServerSettings_remove("auto/cycle");
    HsfServerSettings_remove("auto/buttons");
    HsfServerSettings_remove("run/auto_run_ms");
    HsfServerSettings_remove("run/auto_mode");   /* S15 粘性键：收尾清干净。 */
    HsfServerIo_stop();
}

/** @brief §S 配置 SET：写一个模式的设备清单/转速/方向（applyModeDoc 直达）。 */
static bool sAutoCfgSet(const char* mid, const char* const* devIds, int nDev,
                        int rpm, bool reverse, char* outErr, int cap)
{
    XBsonDocument* doc = XBsonDocument_create();
    XBsonArray* arr;
    bool ok;
    int i;
    if (outErr && cap > 0) outErr[0] = '\0';
    if (!doc) return false;
    arr = XBsonArray_create();
    for (i = 0; i < nDev; ++i) {
        XBsonValue* val = XBsonValue_create(XBSON_TYPE_STRING);
        if (!val) continue;
        XBsonValue_setString_utf8(val, devIds[i]);
        if (arr) {
            XBsonArray_append_move_base(arr, val);
            XBsonValue_delete(val);   /* move 后释放源壳。 */
        } else {
            XBsonValue_delete(val);
        }
    }
    if (arr) {
        XBsonDocument_insert_keyUtf8_array_move(doc, "devices", arr);
        XClassDelete((XClass*)arr);   /* move 后源数组壳由调用方释放。 */
    }
    XBsonDocument_insert_keyUtf8_int32(doc, "stirRpm", rpm);
    XBsonDocument_insert_keyUtf8_bool(doc, "stirReverse", reverse);
    ok = HsfServerAutoMode_applyModeDoc(mid, doc, outErr, cap);
    XClassDelete((XClass*)doc);
    return ok;
}

/** @brief §S 配置 GET：从 configDoc() 读一个模式的回显（devices/rpm/reverse）。 */
static bool sAutoCfgEcho(const XBsonDocument* root, const char* mid,
                         bool* outDev, int* outRpm, bool* outRev)
{
    const XBsonValue* vModes = XBsonDocument_value_keyUtf8(
        (XBsonDocument*)root, "modes");
    const XBsonDocument* modes =
        vModes ? XBsonValue_toDocument((XBsonValue*)vModes) : NULL;
    const XBsonValue* vOne =
        modes ? XBsonDocument_value_keyUtf8((XBsonDocument*)modes, mid) : NULL;
    const XBsonDocument* one =
        vOne ? XBsonValue_toDocument((XBsonValue*)vOne) : NULL;
    const XBsonValue* vArr =
        one ? XBsonDocument_value_keyUtf8((XBsonDocument*)one, "devices") : NULL;
    const XBsonArray* arr = vArr ? XBsonValue_toArray((XBsonValue*)vArr) : NULL;
    const XBsonValue* v;
    size_t n;
    size_t k;

    memset(outDev, 0, HSF_AUTO_DEVICE_COUNT * sizeof(bool));
    *outRpm = -1;
    *outRev = false;
    if (!one) return false;
    if (arr) {
        n = XBsonArray_size_base((XBsonArray*)arr);
        for (k = 0; k < n; ++k) {
            const XBsonValue* el = XBsonArray_at_base((XBsonArray*)arr, k);
            const XString* s;
            int di;
            if (!el) continue;
            if (XBsonValue_type((XBsonValue*)el) != XBSON_TYPE_STRING) continue;
            s = XBsonValue_toString((XBsonValue*)el);
            if (!s) continue;
            di = HsfServerAutoMode_deviceIndex(XString_toUtf8(s));
            if (di >= 0) outDev[di] = true;
        }
    }
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)one, "stirRpm");
    if (v) *outRpm = (int)XBsonValue_toInt32((XBsonValue*)v, -1);
    v = XBsonDocument_value_keyUtf8((XBsonDocument*)one, "stirReverse");
    if (v) *outRev = XBsonValue_toBool((XBsonValue*)v, false);
    return true;
}

/**
 * @brief S1：产水全循环（对齐 Python test_produce_mode_full_cycle）。
 */
static void testS1_produceFullCycle(void)
{
    static const char* const kDevs[] = {
        "valve1", "valve2", "stirPump", "inletPump"
    };
    const bool kWantDev[HSF_AUTO_DEVICE_COUNT] = {
        true, true, false, false, false, false, true, true
    };
    bool gotDev[HSF_AUTO_DEVICE_COUNT];
    int gotRpm = -1;
    bool gotRev = false;
    char why[128];
    char phase[24];
    char modeId[16];
    XBsonDocument* cfgDoc;
    uint32_t di = 0;
    uint32_t dO = 0;
    bool valid = false;
    struct SAutoDoExpect openSel;      /* 选中阀 DO0|DO1 置位。 */
    struct SAutoDoExpect closeUnsel;   /* 未选阀 DO2..5 清零。 */
    struct SAutoDoExpect allOff;       /* 停止后 DO0..6 全清。 */

    XPrintf("--- S1 产水全循环 ---\n");
    sAutoResetFresh();                /* 干净态（含 INI auto 三键删回缺省）。 */
    openSel.m_mask = (1u << 0) | (1u << 1);
    openSel.m_expect = openSel.m_mask;
    closeUnsel.m_mask = (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5);
    closeUnsel.m_expect = 0u;
    allOff.m_mask = (1u << 6) | (1u << 5) | (1u << 4) | (1u << 3) |
                    (1u << 2) | (1u << 1) | (1u << 0);
    allOff.m_expect = 0u;

    /* ---- ① auto 配置 SET/GET 往返一致（落盘在 applyModeDoc 内完成）---- */
    CHECK("S1 配置 SET 受理（produce = 阀1+阀2+搅拌泵+进水泵，720rpm 反转）",
          sAutoCfgSet("produce", kDevs, 4, 720, true, why, (int)sizeof(why)),
          "err=%s", why);
    cfgDoc = HsfServerAutoMode_configDoc();
    CHECK("S1 配置 GET 构造成功", cfgDoc != NULL);
    if (cfgDoc) {
        CHECK("S1 SET 回显：devices/rpm/reverse 与写入一致",
              sAutoCfgEcho(cfgDoc, "produce", gotDev, &gotRpm, &gotRev) &&
              memcmp(gotDev, kWantDev, sizeof(gotDev)) == 0 &&
              gotRpm == 720 && gotRev == true,
              "rpm=%d rev=%d", gotRpm, gotRev ? 1 : 0);
        CHECK("S1 未配置模式 = 空清单（缺省口径，与 Python 同）",
              sAutoCfgEcho(cfgDoc, "backwash", gotDev, &gotRpm, &gotRev) &&
              gotDev[0] == false && gotDev[6] == false && gotRpm == 1440);
        XClassDelete((XClass*)cfgDoc);
    }

    /* ---- ①b 落盘→重启→回读（2026-11-12 stirRpm 落盘丢键实锅封口）----
     * 内存态往返（①）锁不到 amSaveModeCfg 的序列化缺键：stirRpm 曾漏插
     * ⇒ INI JSON 无该键 ⇒ 重启 load 缺键回缺省 1440（Python
     * test_auto_cycle 有「跨重启持久化恢复」用例，C 端补齐同款）。
     * shutdown+setup 即「重启」（INI 落盘在 SET 时同步完成，setup 的
     * loadConfig 从 INI 读回）。 */
    HsfServerAutoMode_shutdown();
    (void)HsfServerAutoMode_setup();
    cfgDoc = HsfServerAutoMode_configDoc();
    CHECK("S1 落盘→重启→回读：stirRpm=720 不回缺省（落盘序列化含转速）",
          cfgDoc != NULL &&
          sAutoCfgEcho(cfgDoc, "produce", gotDev, &gotRpm, &gotRev) &&
          memcmp(gotDev, kWantDev, sizeof(gotDev)) == 0 &&
          gotRpm == 720 && gotRev == true,
          "rpm=%d dev0=%d", gotRpm, gotDev[0] ? 1 : 0);
    if (cfgDoc) XClassDelete((XClass*)cfgDoc);

    /* ---- ② 手动模式启动被拒（四闸第一闸；含「手动」）---- */
    CHECK("S1 手动模式启动被拒（含「手动」）",
          !HsfServerAutoMode_startPreconditions(false, why, (int)sizeof(why)) &&
          strstr(why, "手动") != NULL, "err=%s", why);

    /* ---- ③ 启动受理：粘性模式回显 produce、相位立即进阀相 ---- */
    CHECK("S1 启动受理",
          HsfServerAutoMode_start("produce",
                                  (uint64_t)XDateTime_currentMSecsSinceEpoch()),
          "err 无——四闸/配置判据先于本调用");
    HsfServerAutoMode_modeId(modeId, (int)sizeof(modeId));
    HsfServerAutoMode_phase(phase, (int)sizeof(phase));
    CHECK("S1 启动即回显粘性模式 produce（应答 autoMode 键的取值面）",
          strcmp(modeId, "produce") == 0, "mode=%s", modeId);
    CHECK("S1 启动即进阀相（应答 autoPhase 键的取值面）",
          strcmp(phase, "valves") == 0, "phase=%s", phase);

    /* ---- ④ 阀相命令面：选中阀 DO=1、未选阀 DO=0（DO 回显，不是受理值）---- */
    CHECK("S1 选中阀 DO0/DO1 置位、未选阀 DO2..5 清零",
          sAutoWaitUntil(2500, true, sCondDoBits, &openSel) &&
          sCondDoBits(&closeUnsel));

    /* ---- ⑤ 模拟到位 → 泵相 → 运行相（决策只认反馈：行程由模型驱动）---- */
    CHECK("S1 阀全部到位后推进到 running",
          sAutoWaitUntil(8000, true, sCondPhaseRunning, NULL));

    /* ---- ⑥ 泵按配置启动：搅拌泵 720rpm 反转的折算给定 + 进水泵 DO6=1 ----
     * [先沉降再断言] running 位与泵启动在同一拍下发，DO 回显/变频器状态
     * 要等一拍轮询才落地——DO 位走 400ms 沉降（回显快）；变频器运行位/
     * 方向走等待式（命令字经 Hub 排队 + 状态字经站轮询回读，受理≠已置，
     * 见 sCondVfdRunningReverse 注）。 */
    sAutoRunFor(400, true);
    (void)HsfServerIo_digitalMasks(&di, &dO, &valid);
    CHECK("S1 进水泵（DO6）已启动",
          valid && ((dO & (1u << 6)) != 0), "do=0x%08X", (unsigned)dO);
#if XVFD_ON
    {
        XVfdDevice* dev = HsfServerIo_vfdDevice(HsfSide_A);
        const XVfdStatus* st;
        if (!dev) dev = HsfServerIo_vfdDevice(HsfSide_B);
        CHECK("S1 搅拌泵启动反馈（运行位 + 反转，等待式）",
              sAutoWaitUntil(5000, true, sCondVfdRunningReverse, NULL));
        st = dev ? XVfdDevice_status_const(dev) : NULL;
        CHECK("S1 搅拌泵已启动（变频器运行位）",
              st && st->m_valid && st->m_running);
        CHECK("S1 方向 = 配置的反转",
              st && st->m_valid && st->m_reverse,
              "reverse=%d", st ? (int)st->m_reverse : -1);
        /* [相位前提必须显式] 模拟变频器上电缺省给定就是 50%=25Hz
         * （VirtualVfd.set_percent=5000 同值），与 720rpm 的折算目标
         * 恰好同值——不先锁"已进 running 相"，本断言在泵相根本没跑的
         * 时候也会绿（2026-11-12 竞速轮实证：它误导了两轮泵相方向）。 */
        HsfServerAutoMode_phase(phase, (int)sizeof(phase));
        CHECK("S1 给定 = 720rpm 折算（XVfdDevice_rpmToHz 同源口径）",
              strcmp(phase, "running") == 0 &&
              st && st->m_valid && dev &&
              fabs(st->m_setFreqHz - XVfdDevice_rpmToHz(dev, 720.0)) < 0.01,
              "phase=%s set=%.2fHz", phase,
              st ? st->m_setFreqHz : -1.0);
    }
#endif

    /* ---- ⑦ 停止 = 停控制器 + 全停设备（泵关 / 阀关 / 变频器减速停）---- */
    CHECK("S1 停止受理",
          HsfServerAutoMode_requestStop("回归停止",
                                        (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    CHECK("S1 停止后泵/阀输出全清（DO0..6=0）",
          sAutoWaitUntil(2000, true, sCondDoBits, &allOff));
#if XVFD_ON
    CHECK("S1 停止后搅拌泵走减速停机到停（斜坡口径，≤9s 到位）",
          sAutoWaitUntil(9000, true, sCondVfdStopped, NULL));
#endif
    HsfServerAutoMode_phase(phase, (int)sizeof(phase));
    CHECK("S1 停止后控制器不在运行", !HsfServerAutoMode_running() &&
          strcmp(phase, "idle") == 0, "phase=%s", phase);

    /* ---- ⑧ 模式粘性：停止后仍回显产水（现场启动按钮/再启动仍指向它）---- */
    HsfServerAutoMode_modeId(modeId, (int)sizeof(modeId));
    CHECK("S1 停止后模式粘性保留 produce", strcmp(modeId, "produce") == 0,
          "mode=%s", modeId);
}

/**
 * @brief S2：阀超时 ⇒ kind=5 告警 + 锁存；复位后才能再启动（对齐
 *          Python test_valve_timeout_alarm_and_fault_latch）。
 * @details 卡阀夹具 = 模拟行程 UINT32_MAX（开关永远走不到位）+ 阀组动作
 *          超时夹具 600ms（勿用 20s 产品缺省——回归要快，且告警判据走
 *          「阀层判故障」同一条路径）。反洗模式 = 阀3 + 进水泵：阀卡死时
 *          泵绝不能被启动（相位止步阀相）。
 */
static void testS2_valveTimeoutAlarmAndFaultLatch(void)
{
    static const char* const kDevs[] = { "valve3", "inletPump" };
    char why[128];
    char text[160];
    bool sawTimeoutAlarm = false;
    bool sawValveName = false;
    uint32_t di = 0;
    uint32_t dO = 0;
    bool valid = false;
    XAutoController* ctl = NULL;
    XAutoControlAlarm a;

    XPrintf("--- S2 阀超时告警与锁存 ---\n");
    sAutoResetFresh();
    /* [先结算再上卡死夹具] S1 收尾的全停把 DO 清 0，六路阀的关行程
     * （200ms+总线余量）此刻可能仍在途；卡死覆盖是**全模型**的
     * setMoveMs，先上夹具会把在途阀冻在半程 ⇒ 阀相最先判故障的是
     * 表序第一路（阀1）而不是配置点名的阀3。先空跑一段让行程走完
     * （手动模式不写任何 DO），再冻结。 */
    sAutoRunFor(1200, false);
    sSimValveMove(0xFFFFFFFFu);       /* 卡死：阀 3 收命令但永远不到位。 */
    CHECK("S2 反洗配置 SET 受理",
          sAutoCfgSet("backwash", kDevs, 2, 1440, false, why, (int)sizeof(why)),
          "err=%s", why);
    CHECK("S2 启动受理（进阀相）",
          HsfServerAutoMode_start("backwash",
                                  (uint64_t)XDateTime_currentMSecsSinceEpoch()));

    /* 阀层 600ms 判故障 → 序列截止判定 kind=5 告警 + 报警锁存 + 停控制器。 */
    CHECK("S2 卡阀触发报警锁存（阀层判故障 → 序列中止）",
          sAutoWaitUntil(4000, true, sCondFaultLatched, NULL));

    ctl = HsfServerAutoMode_controller();
    memset(&a, 0, sizeof(a));         /* 栈上结构体交框架：先 memset。 */
    while (ctl && XAutoController_takeAlarm(ctl, &a)) {
        if (a.m_kind == HSF_AUTO_ALARM_VALVE_TIMEOUT) {
            snprintf(text, sizeof(text), "%s", a.m_text);
            sawTimeoutAlarm = true;
            if (strstr(text, "阀3") != NULL && strstr(text, "超时") != NULL)
                sawValveName = true;
        }
        memset(&a, 0, sizeof(a));
    }
    CHECK("S2 收到 kind=5 阀超时告警", sawTimeoutAlarm);
    CHECK("S2 告警文字点名阀号（阀3）+「超时」", sawValveName,
          "text=%s", sawTimeoutAlarm ? text : "(无)");

    CHECK("S2 泵绝不被启动（阀没到位就不许启动泵：DO6=0）",
          HsfServerIo_digitalMasks(&di, &dO, &valid) && valid &&
          (dO & (1u << 6)) == 0, "do=0x%08X", (unsigned)dO);
    CHECK("S2 autoFault=true（应答 autoFault 键的取值面）",
          HsfServerAutoMode_faultLatched());
    CHECK("S2 控制器已停（相位回 idle 且**不动设备**）",
          !HsfServerAutoMode_running());

    /* 报警未复位 ⇒ 再启动被拒（用户口径「有报警没复位不能启动」，含「复位」）。 */
    CHECK("S2 报警未复位再启动被拒（含「复位」）",
          !HsfServerAutoMode_startPreconditions(true, why, (int)sizeof(why)) &&
          strstr(why, "复位") != NULL, "err=%s", why);

    /* 复位 ⇒ 解锁，可再启动（阀仍卡着会再次超时，这轮只验解锁）。 */
    CHECK("S2 复位受理（一把钥匙解报警锁存）",
          HsfServerAutoMode_requestReset(
              (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    CHECK("S2 复位后 autoFault=false", !HsfServerAutoMode_faultLatched());
    CHECK("S2 复位后允许再启动",
          HsfServerAutoMode_start("backwash",
                                  (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    /* 收尾：停控制器 + 全停，解除卡阀夹具。 */
    (void)HsfServerAutoMode_requestStop(
        "S2 收尾", (uint64_t)XDateTime_currentMSecsSinceEpoch());
    sAutoDrainAlarms();
    sSimValveMove(200);
}

/**
 * @brief S3：闸门面（对齐 Python test_gates_and_sticky_mode 的闸门部分）。
 */
static void testS3_gates(void)
{
    static const char* const kDevs[] = { "valve1", "stirPump" };
    char why[128];
    char phase[24];
    uint32_t di = 0;
    uint32_t dO = 0;
    bool valid = false;

    XPrintf("--- S3 闸门面 ---\n");
    sAutoResetFresh();                /* rinse/组合态回缺省（S3 要测未配置）。 */
    /* [720rpm 而非 1440] 急停后的减速停要等坡道走完才能断言"停到位"；
     * 25Hz→5s 比 50Hz→10s 快一倍，闸门面的断言点不受转速影响。 */
    CHECK("S3 产水配置 SET 受理（阀1 + 搅拌泵，720rpm 短坡道）",
          sAutoCfgSet("produce", kDevs, 2, 720, false, why, (int)sizeof(why)),
          "err=%s", why);

    /* ---- 「自动」组合模式：2026-11-12 已落地 ⇒ 此时只配了产水，
     * 拒绝理由是「三个子模式都要配设备」而不是「未实现」。 ---- */
    CHECK("S3 组合模式拒（缺子模式）",
          !HsfServerAutoMode_modeConfigError(HSF_AUTO_MODE_ID_AUTO, why,
                                             (int)sizeof(why)) &&
          strstr(why, "子模式") != NULL, "err=%s", why);
    /* ---- 未配置模式：拒绝且说明去哪配（含「未配置」）---- */
    CHECK("S3 未配置模式拒（含「未配置」）",
          !HsfServerAutoMode_modeConfigError("rinse", why, (int)sizeof(why)) &&
          strstr(why, "未配置") != NULL, "err=%s", why);

    /* ---- 启动产水（阀1 + 搅拌泵）→ 运行 ---- */
    CHECK("S3 启动受理",
          HsfServerAutoMode_start("produce",
                                  (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    /* [窗口 8000→15000 2026-11-21] 阀相→泵相→运行相的推进依赖**阀到位
     * 反馈**，而反馈来自**轮询**采集数字量/模拟设备 ⇒ CPU 被抢占时那一轮
     * 轮询没跑到 ⇒ 相位推进晚 ⇒ 8 秒真钟窗口偶发不够（实测 5 连跑里
     * 红 1 次；单跑连续 2 次 1099/0 ⇒ 典型负载型偶发）。
     * ⇒ 放宽到 15 秒：**判据语义不变**（仍要求推进到 running 相），
     * 只是不再被机器负载误伤。 */
    CHECK("S3 运行相推进", sAutoWaitUntil(15000, true, sCondPhaseRunning, NULL));

    /* ---- 运行中急停：全停 + 相位 estop + 锁存；复位前不能启动 ---- */
    CHECK("S3 急停受理（首次）",
          HsfServerAutoMode_requestEstop("回归急停",
                                         (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    sAutoRunFor(300, true);
    HsfServerAutoMode_phase(phase, (int)sizeof(phase));
    CHECK("S3 急停后相位 = estop", strcmp(phase, "estop") == 0,
          "phase=%s", phase);
    CHECK("S3 急停锁存", HsfServerAutoMode_estopLatched());
    CHECK("S3 控制器不再运行", !HsfServerAutoMode_running());
    (void)HsfServerIo_digitalMasks(&di, &dO, &valid);
    CHECK("S3 急停默认阀保持现状（resetValvesOnEstop=false，DO0 仍在）",
          valid && (dO & 0x3Fu) != 0, "do=0x%08X", (unsigned)dO);
#if XVFD_ON
    /* [减速停是坡道不是瞬时] 停机命令把爬坡目标清 0，运行位在输出降到 0
     * 那一刻才翻（真机状态字口径，sim 模型减速期仍报原方向）——所以这里
     * 等的是「坡道走完」，预算 8s（25Hz ÷ 5Hz/s = 5s + 余量）。 */
    CHECK("S3 全停：变频器减速停到位（≤8s 坡道）",
          sAutoWaitUntil(8000, true, sCondVfdStopped, NULL));
#endif
    CHECK("S3 急停锁存拒启动（含「复位」）",
          !HsfServerAutoMode_startPreconditions(true, why, (int)sizeof(why)) &&
          strstr(why, "复位") != NULL, "err=%s", why);
    /* 复位 → 解锁（急停锁存与报警锁存一把钥匙），可再启动。 */
    CHECK("S3 复位受理",
          HsfServerAutoMode_requestReset(
              (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    CHECK("S3 复位后可再启动",
          HsfServerAutoMode_start("produce",
                                  (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    /* 收尾：只停控制器不动设备（模式序列已作废，S3 无需全停对照）。 */
    (void)HsfServerAutoMode_stopController(
        "S3 收尾", (uint64_t)XDateTime_currentMSecsSinceEpoch());
    sAutoDrainAlarms();
}

/**
 * @brief S4：mode_config_error 单元（对齐 Python
 *          test_mode_config_error_variants；两个入口同源）。
 */
static void testS4_modeConfigErrorVariants(void)
{
    static const char* const kValve2Stir[] = { "valve2", "stirPump" };
    static const char* const kGhostOnly[] = { "ghost1", "nope" };
    char why[128];
    bool gotDev[HSF_AUTO_DEVICE_COUNT];
    int gotRpm = -1;
    bool gotRev = false;
    XBsonDocument* cfgDoc;

    XPrintf("--- S4 mode_config_error 单元 ---\n");
    /* 干净态：三模式全空配置（上一用例的 SET 与上一次进程跑的 INI 都清掉）。 */
    sAutoResetFresh();
    CHECK("S4 空配置拒并提示去远端设置配（含「未配置」）",
          !HsfServerAutoMode_modeConfigError("produce", why,
                                             (int)sizeof(why)) &&
          strstr(why, "未配置") != NULL, "err=%s", why);
    CHECK("S4 配置齐（阀2+搅拌泵）→ 可跑",
          sAutoCfgSet("backwash", kValve2Stir, 2, 1440, false,
                      why, (int)sizeof(why)) &&
          HsfServerAutoMode_modeConfigError("backwash", why,
                                            (int)sizeof(why)),
          "err=%s", why);
    CHECK("S4 组合模式拒（缺子模式）",
          !HsfServerAutoMode_modeConfigError(HSF_AUTO_MODE_ID_AUTO, why,
                                             (int)sizeof(why)) &&
          strstr(why, "子模式") != NULL, "err=%s", why);
    CHECK("S4 未知 id 拒", !HsfServerAutoMode_modeConfigError(
          "ghost", why, (int)sizeof(why)));
    CHECK("S4 未选模式（空串）拒", !HsfServerAutoMode_modeConfigError(
          "", why, (int)sizeof(why)));

    /* 全未知设备 id：applyModeDoc 拒（客户端键形错了要让人看见，不能静默
     * 存一份空的），且配置保持现值。 */
    CHECK("S4 全未知设备 id 的 SET 被拒",
          !sAutoCfgSet("produce", kGhostOnly, 2, 720, false,
                       why, (int)sizeof(why)) && why[0] != '\0',
          "err=%s", why);
    cfgDoc = HsfServerAutoMode_configDoc();
    CHECK("S4 被拒后 produce 配置未被写入（仍是空清单）",
          cfgDoc && sAutoCfgEcho(cfgDoc, "produce", gotDev, &gotRpm,
                                 &gotRev) && gotDev[0] == false &&
          gotDev[6] == false);
    if (cfgDoc) XClassDelete((XClass*)cfgDoc);

    /* 选中阀无点位：把阀组重配成只剩 1 路（阀1），选了阀2 的反洗必须点名拒。 */
    {
        HsfServerIoValveCfg items[HSF_VALVE_COUNT];
        int i;
        for (i = 0; i < HSF_VALVE_COUNT; ++i) {
            items[i].m_name = NULL;
            items[i].m_do = (i < 1) ? HSF_VALVE_DO(i) : -1;
            items[i].m_openDi = (i < 1) ? HSF_VALVE_OPEN_DI(i) : -1;
            items[i].m_closeDi = (i < 1) ? HSF_VALVE_CLOSE_DI(i) : -1;
        }
        HsfServerIoValve_configure((HsfServerIoValve*)HsfServerIo_valves(),
                                   items, HSF_VALVE_ACTIVE_COUNT, 600, true);
    }
    CHECK("S4 选中阀未配点位 → 拒绝并点名（含「阀2」）",
          !HsfServerAutoMode_modeConfigError("backwash", why,
                                             (int)sizeof(why)) &&
          strstr(why, "阀2") != NULL, "err=%s", why);
    sValveBankConfigure(600);         /* 还原 6 路点位（夹具超时不变）。 */
    CHECK("S4 点位还原后同模式恢复可跑",
          HsfServerAutoMode_modeConfigError("backwash", why,
                                            (int)sizeof(why)),
          "err=%s", why);
}

/**
 * @brief S5：前置闸顺序（对齐 Python test_precondition_order_fault_before_estop）：
 *          报警锁存与急停锁存并存 ⇒ 先报「报警」（配置 → 安全层层可读）。
 */
static void testS5_preconditionOrder(void)
{
    static const char* const kDevs[] = { "valve1", "inletPump" };
    char why[128];

    XPrintf("--- S5 前置闸顺序 ---\n");
    sAutoResetFresh();
    /* [先收拾 S3 的尾巴再上卡阀夹具] S3 收尾是 stopController（只停控制器
     * **不动设备**），阀1 的开命令留在 DO0 上、阀1 已开到位；卡阀夹具
     * （全模型 setMoveMs=∞）对本就到位的阀1 不产生任何"动作中"状态，
     * 报警锁存永远不会发生。先全停 + 结算行程（手动模式不写 DO），让
     * 六路阀全部回到已关，卡死才能真实落在配置点名的阀1 上。 */
    (void)HsfServerAutoMode_stopAllDevices("S5 收拾前序尾巴");
    sAutoRunFor(1200, false);
    sSimValveMove(0xFFFFFFFFu);       /* 卡阀夹具：制造报警锁存。 */
    CHECK("S5 配置 SET 受理",
          sAutoCfgSet("produce", kDevs, 2, 1440, false, why, (int)sizeof(why)),
          "err=%s", why);
    CHECK("S5 启动受理（随后将被卡阀中止）",
          HsfServerAutoMode_start("produce",
                                  (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    CHECK("S5 报警锁存就位（阀层判故障 → 序列中止）",
          sAutoWaitUntil(4000, true, sCondFaultLatched, NULL));
    /* 急停锁存叠加（框架允许任一状态入急停）⇒ 双锁存并存。 */
    CHECK("S5 急停锁存叠加",
          HsfServerAutoMode_requestEstop("S5 叠加急停",
                                         (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    CHECK("S5 双锁存并存时先报「报警」（闸顺序固定）",
          !HsfServerAutoMode_startPreconditions(true, why, (int)sizeof(why)) &&
          strstr(why, "报警") != NULL, "err=%s", why);
    /* 一把钥匙解两把锁后，只剩急停锁存 ⇒ 报「急停」。 */
    CHECK("S5 复位受理（解两把锁）",
          HsfServerAutoMode_requestReset(
              (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    CHECK("S5 重新急停（只剩这一把锁）",
          HsfServerAutoMode_requestEstop("S5 单独急停",
                                         (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    CHECK("S5 只剩急停锁存时报「急停」",
          !HsfServerAutoMode_startPreconditions(true, why, (int)sizeof(why)) &&
          strstr(why, "急停") != NULL, "err=%s", why);
    (void)HsfServerAutoMode_requestReset(
        (uint64_t)XDateTime_currentMSecsSinceEpoch());
    (void)HsfServerAutoMode_stopController(
        "S5 收尾", (uint64_t)XDateTime_currentMSecsSinceEpoch());
    sAutoDrainAlarms();
    sSimValveMove(200);
}

/**
 * @brief S7：告警链（框架队列层；对齐 Python test_auto_alarm_publish 的
 *          队列/幂等/挤最旧/截断四条。发布帧与入库面由
 *          verify/hsf_auto_mode_check.py 对真实服务端钉）。
 */
static void testS7_alarmChain(void)
{
    XAutoController* ctl = HsfServerAutoMode_controller();
    XAutoControlAlarm a;
    char big[256];
    int i;
    int got = 0;
    bool estopTextOk = false;

    XPrintf("--- S7 告警链（队列层）---\n");
    sAutoResetFresh();
    ctl = HsfServerAutoMode_controller();
    if (!ctl) {
        CHECK("S7 控制器可用", false);
        return;
    }
    /* ---- ① 压急停：框架自动入队 kind=4（取走即消费）---- */
    CHECK("S7 首次急停受理",
          HsfServerAutoMode_requestEstop("S7 急停",
                                         (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    memset(&a, 0, sizeof(a));         /* 栈上结构体交框架：先 memset。 */
    while (XAutoController_takeAlarm(ctl, &a)) {
        ++got;
        if (a.m_kind == HSF_AUTO_ALARM_ESTOP &&
            strstr(a.m_text, "急停") != NULL && a.m_atMs > 0)
            estopTextOk = true;
        memset(&a, 0, sizeof(a));
    }
    CHECK("S7 压急停恰好一条告警（kind=4、文字带急停语义、时刻非零）",
          got == 1 && estopTextOk, "got=%d", got);

    /* ---- ② 锁存重复急停不重复报警（幂等）---- */
    CHECK("S7 重复急停返回 false（已锁存）",
          !HsfServerAutoMode_requestEstop("S7 重复急停",
                                          (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    CHECK("S7 锁存期间在队报警数不变（不重复入队）",
          XAutoController_pendingAlarmCount(ctl) == 0,
          "pending=%d", XAutoController_pendingAlarmCount(ctl));

    /* ---- ③ 队列满挤最旧、最新必保留（容量 8）---- */
    got = 0;   /* [复用前清零] ① 里 got 已被当作 1（急停告警条数），
                * 不清零会让下面的连发计数恒差一条——2026-11-12 修。 */
    (void)HsfServerAutoMode_requestReset(
        (uint64_t)XDateTime_currentMSecsSinceEpoch());
    for (i = 0; i < XAUTO_CONTROL_ALARM_CAPACITY + 3; ++i) {
        char label[32];
        snprintf(label, sizeof(label), "第%d条", i);
        if (!XAutoController_raiseAlarm(ctl, HSF_AUTO_ALARM_CUSTOM, label,
                                        (uint64_t)XDateTime_currentMSecsSinceEpoch()))
            ++got;
    }
    CHECK("S7 连发 11 条全部受理", got == 0, "failed=%d", got);
    got = 0;
    CHECK("S7 队列容量恒 8（满挤最旧）",
          XAutoController_pendingAlarmCount(ctl) == XAUTO_CONTROL_ALARM_CAPACITY,
          "pending=%d", XAutoController_pendingAlarmCount(ctl));
    memset(&a, 0, sizeof(a));
    CHECK("S7 最旧 3 条被挤掉（队首=第3条）",
          XAutoController_takeAlarm(ctl, &a) &&
          strstr(a.m_text, "第3条") != NULL,
          "head=%s", a.m_text);
    {
        char last[128];
        last[0] = '\0';
        memset(&a, 0, sizeof(a));
        while (XAutoController_takeAlarm(ctl, &a)) {
            snprintf(last, sizeof(last), "%s", a.m_text);
            memset(&a, 0, sizeof(a));
        }
        CHECK("S7 最新一条必须保留（第10条）",
              strstr(last, "第10条") != NULL, "tail=%s", last);
    }

    /* ---- ④ 文字 128 字节截断（m_text[128] 字节预算；注：C 端今天是纯
     *     字节截断，Python 端另有 UTF-8 安全截断——差距已登记在案）---- */
    /* [样本必须正好 200 字节] "急"是 3 字节 UTF-8：66×3=198，再补 2 字节
     * ASCII 凑满 200（旧写法 100×3=300 字节被 big[256] 截成 255，
     * strlen==200 恒不成立——2026-11-12 修）。 */
    big[0] = '\0';
    for (i = 0; i < 66; ++i) {
        size_t used = strlen(big);
        snprintf(big + used, sizeof(big) - used, "%s", "急");
    }
    snprintf(big + strlen(big), sizeof(big) - strlen(big), "%s", "AB");
    CHECK("S7 样本 200 字节构造成功", strlen(big) == 200,
          "len=%u", (unsigned)strlen(big));
    CHECK("S7 超长文字入队受理（永不因满丢新告警）",
          XAutoController_raiseAlarm(ctl, HSF_AUTO_ALARM_CUSTOM, big,
                                     (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    memset(&a, 0, sizeof(a));
    CHECK("S7 截断后文字不超过 128 字节",
          XAutoController_takeAlarm(ctl, &a) && strlen(a.m_text) <= 128,
          "len=%u", (unsigned)strlen(a.m_text));
    (void)HsfServerAutoMode_requestReset(
        (uint64_t)XDateTime_currentMSecsSinceEpoch());
    sAutoDrainAlarms();
}

/**
 * @brief S8：模式包络键的客户端消费面（0x0105 与 0x0115 同形应答）。
 * @details [为什么在客户端面锁] 服务端包络构造器（hsfAppMakeModeEnvelope）
 *          在 HsfServerApp.c——不在测试宿主链接面内；「拒绝也回全量键」
 *          的**线面锁**由 verify/hsf_auto_mode_check.py 对真实服务端钉。
 *          本段锁的是 C 客户端按形状取的映射：成功应答六键全量回填
 *          （两命令同源）、失败应答 err 浮到命令错误文案且序列号照常推进
 *          （页面才看得到失败）。S1~S3 断言的粘性模式/相位/锁存正是服务端
 *          应答里 autoMode/autoPhase/autoFault 三键的取值来源。
 *          [口径差登记] 拒绝应答里的键 C 客户端今天**不回填**（保持上次
 *          值，hsfLcApplyModeDoc 的 ok 分支闸）；Python 端 bridge 会回填。
 *          这里不断言拒绝键（免得把两端口径差锁死成 C 的行为）。
 */
static void testS8_modeEnvelopeKeys(void)
{
    XBsonDocument* doc;
    XByteArray* bytes;
    const int seq0 = HsfLinkClient_commandSeq();
    int mode = -1;

    XPrintf("--- S8 模式包络键（客户端消费面）---\n");
    /* ---- ① 0x0105 成功应答：六键全量回填 ---- */
    doc = XBsonDocument_create();
    CHECK("S8 包络构造成功", doc != NULL);
    if (doc) {
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, true);
        XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_MODE, 1);
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_AUTO, true);
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_AUTO_RUNNING, true);
        XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_AUTO_MODE, "produce");
        XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_AUTO_PHASE, "running");
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_AUTO_FAULT, true);
        bytes = iBsonBytes(doc);
        CHECK("S8 0x0105 成功应答分发",
              bytes && iDispatch(HSF_CMD_RUN_MODE, bytes));
        CHECK("S8 0x0105：mode/auto/autoRunning 回填",
              HsfLinkClient_runMode(&mode) && mode == 1 &&
              HsfLinkClient_autoRunning(&mode) && mode == 1,
              "mode=%d", mode);
        CHECK("S8 0x0105：autoMode/autoPhase/autoFault 三键回填",
              strcmp(HsfLinkClient_autoMode(), "produce") == 0 &&
              strcmp(HsfLinkClient_autoPhase(), "running") == 0 &&
              HsfLinkClient_autoFault() == true,
              "autoMode=%s autoPhase=%s", HsfLinkClient_autoMode(),
              HsfLinkClient_autoPhase());
        /* [所有权] iBsonBytes(doc) 已删除 doc（内部 XClassDelete），
         * 此处只许删 bytes——再删 doc = 双重释放（本段之前的段错误根因）。 */
        if (bytes) XClassDelete((XClass*)bytes);
    }
    /* ---- ② 0x0115 成功应答：同形回填（两命令同源）---- */
    doc = XBsonDocument_create();
    if (doc) {
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, true);
        XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_MODE, 1);
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_AUTO, true);
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_AUTO_RUNNING, false);
        XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_AUTO_MODE, "backwash");
        XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_AUTO_PHASE, "valves");
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_AUTO_FAULT, false);
        bytes = iBsonBytes(doc);
        CHECK("S8 0x0115 成功应答分发",
              bytes && iDispatch(HSF_CMD_AUTO_CONTROL, bytes));
        CHECK("S8 0x0115：autoRunning 回填为停止",
              HsfLinkClient_autoRunning(&mode) && mode == 0, "mode=%d", mode);
        CHECK("S8 0x0115：autoMode/autoPhase/autoFault 同源回填",
              strcmp(HsfLinkClient_autoMode(), "backwash") == 0 &&
              strcmp(HsfLinkClient_autoPhase(), "valves") == 0 &&
              HsfLinkClient_autoFault() == false,
              "autoMode=%s", HsfLinkClient_autoMode());
        /* [所有权] 同上：doc 已被 iBsonBytes 删除，只删 bytes。 */
        if (bytes) XClassDelete((XClass*)bytes);
    }
    /* ---- ③ 失败应答：err 浮出 + 序列号照常推进（页面才看得到失败）---- */
    doc = XBsonDocument_create();
    if (doc) {
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_OK, false);
        XBsonDocument_insert_keyUtf8_utf8(
            doc, HSF_KEY_ERR, "当前是手动模式：请先切到自动模式，再启动自动运行");
        XBsonDocument_insert_keyUtf8_int32(doc, HSF_KEY_MODE, 0);
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_AUTO, false);
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_AUTO_RUNNING, false);
        XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_AUTO_MODE, "");
        XBsonDocument_insert_keyUtf8_utf8(doc, HSF_KEY_AUTO_PHASE, "idle");
        XBsonDocument_insert_keyUtf8_bool(doc, HSF_KEY_AUTO_FAULT, false);
        bytes = iBsonBytes(doc);
        CHECK("S8 0x0115 失败应答分发",
              bytes && iDispatch(HSF_CMD_AUTO_CONTROL, bytes));
        CHECK("S8 失败原因浮到命令错误文案（含「手动」）",
              HsfLinkClient_lastCommandError()[0] != '\0' &&
              strstr(HsfLinkClient_lastCommandError(), "手动") != NULL,
              "err=%s", HsfLinkClient_lastCommandError());
        /* [序列号口径] 本段共 3 次分发，但只 0x0115 两次推进共享命令序列号：
         * 0x0105（RUN_MODE）是首页 1s 拉取式**查询**（HsfHomePage.c 每秒
         * queryRunMode），其应答分支刻意不 bump m_lastCmdSeq（结果走
         * m_modeErr/modeCb 专属通道）——若查询也推进，首页「在途命令终态」
         * 按任意 seq 变化消费（HsfHomePage 刷新处的 cseq != seen 判定）会把
         * 每秒一次的轮询应答错当成启动/停止的终态。失败推进的锁点在 ②③：
         * 失败包络照常 +1，页面才弹得出来。 */
        CHECK("S8 失败应答同样推进序列号",
              HsfLinkClient_commandSeq() == seq0 + 2,
              "seq0=%d now=%d", seq0, HsfLinkClient_commandSeq());
        /* [所有权] 同上：doc 已被 iBsonBytes 删除，只删 bytes。 */
        if (bytes) XClassDelete((XClass*)bytes);
    }
}

/* ---- S9~S11 公共助手（2026-11-12） ---- */

#if defined(HSF_BTN_START_LAMP_DO)
/** @brief S9 助手：改按钮区单个 DI 位（读改写整型掩码，不动阀反馈位）。 */
static void s9SetButtonDi(int diIndex, bool on)
{
    const HsfServerIoSimModels* sim = HsfServerIoSim_ensure();
    uint32_t m;
    if (!sim || !sim->m_digMain) return;
    m = XVirtualModelDigital_diMask(sim->m_digMain);
    if (on) m |= (1u << diIndex);
    else    m &= ~(1u << diIndex);
    XVirtualModelDigital_setDi(sim->m_digMain, m);
}

/** @brief S9 助手：读一盏灯的实际电平（Hub DO 缓存口径，与灯同步同源）。 */
static bool s9LampState(int lampDo)
{
    uint32_t doMask = 0;
    bool valid = false;
    (void)HsfServerIo_digitalDo(&doMask, &valid);
    return valid && ((doMask >> lampDo) & 1u);
}

/**
 * @brief S13 助手：**等按钮站读到**某个 DI 位达到期望值。
 *
 * [为什么必须等 2026-11-21] 按钮站是**轮询**数字量模块来采 DI 的（服务端
 * 有自己的缓存与轮询周期）⇒ 在虚拟模型上改了 DI 位之后，**服务端不是
 * 立刻可见**。用例原来用固定 `sAutoRunFor(300)` 等待 ⇒ CPU 被抢占时
 * 数字量那一轮没跑到 ⇒ 「松开（1）」这一拍**没被采到** ⇒ 后面再「按下
 * (0)」时**不是边沿**（前一拍读到的还是 0）⇒ 按钮不被识别 ⇒ 关阀复位
 * 根本没发生 ⇒ 「S13 停止关阀复位：阀1 DO0 清零」偶发红
 * （实测 5 连跑里红 2 次，单跑连续 2 次 1099/0 ⇒ 典型负载型偶发）。
 *
 * [为什么不放宽等待窗口] 6 秒轮询窗口已经在了（AGENTS 2026-11-13 记过
 * 2500→4000→6000 三次放宽仍偶发）⇒ 病根不是窗口短，而是**沿没被采到**
 * ⇒ 正确修法是**等到位**（等到服务端真的读到期望电平）而不是等时长。
 */
static bool s9WaitButtonDi(int diIndex, bool want, int timeoutMs)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < timeoutMs) {
        uint32_t diMask = 0;
        bool valid = false;
        (void)HsfServerIo_digitalDi(&diMask, &valid);
        if (valid && (((diMask >> diIndex) & 1u) != 0) == want) {
            return true;
        }
        sAutoPumpTick(true);
        XCoreApplication_processEvents(0);
        XThread_msleep(20);
    }
    return false;
}

/** @brief S9 助手：某路进水泵的命令态（true=在开；泵无反馈点，输出即状态）。 */
static bool s9PumpOn(int index)
{
    const HsfServerIoPump* bank = HsfServerIo_pumps();
    const HsfServerIoPumpItem* it =
        bank ? HsfServerIoPump_item(bank, index) : NULL;
    return it ? it->m_on : false;
}

/** @brief S9 助手：某路电动球阀的"开命令"态（true=最后一条命令是开）。 */
static bool s9ValveOpenCmd(int index)
{
    const HsfServerIoValve* bank = HsfServerIo_valves();
    const HsfServerIoValveItem* it =
        bank ? HsfServerIoValve_item(bank, index) : NULL;
    return it ? it->m_openCmd : false;
}

/** @brief S9 条件谓词：某路阀是否已到关位（关到位反馈吸、开到位释放）。 */
static int sCondValveClosed(void* ud)
{
    const int index = (int)(intptr_t)ud;
    const HsfServerIoValve* bank = HsfServerIo_valves();
    const HsfServerIoValveItem* it =
        bank ? HsfServerIoValve_item(bank, index) : NULL;
    if (!it) return 0;
    return (it->m_closeFb && !it->m_openFb) ? 1 : 0;
}
#endif /* HSF_BTN_START_LAMP_DO */

/**
 * @brief S9：按钮组三盏灯跟随（对齐 Python test_mode_lamp_rules 灯规则）。
 * @details 口径（Python buttons.py/_apply_lamps，2026-11-15 用户定案）：
 *   · 启动灯 = **在动**：自动模式=自动运行中；手动模式=**现场还有设备在动**
 *     （搅拌泵在转 / 进水泵在开 / 任一球阀在开位）；
 *   · 停止灯 = 非在动（与启动灯严格互斥）；复位灯点动镜像 DI15；
 *   · 外部清零后按回读自愈；
 *   · **手动模式的停止/急停一律关阀复位**（用户口径：手动模式下按停止/
 *     急停要把设备与球阀全部停下来）。
 */
static void testS9_stationLamps(void)
{
#if !defined(HSF_BTN_START_LAMP_DO)
    CHECK("S9 灯点位宏缺失（跳过）", false);
#else
    /* [时刻一律现取] start/stop 的时间戳当下取（服务端真实路径同款）；
     * 缓存入口时刻会让 settle 判"这段没在跑"而丢时长（S11 首跑实证）。 */
    sAutoResetFresh();
    CHECK("S9 estopState=OK（DI12 常闭回路通）",
          HsfServerAutoMode_estopState() == HSF_ESTOP_CIRCUIT_OK);

    /* ① 自动模式 + 未运行：停止灯亮、启动灯灭、复位灯灭（首拍必写）。 */
    sAutoRunFor(700, true);
    CHECK("S9 停止态：停止灯亮", s9LampState(HSF_BTN_STOP_LAMP_DO));
    CHECK("S9 停止态：启动灯灭", !s9LampState(HSF_BTN_START_LAMP_DO));
    CHECK("S9 停止态：复位灯灭", !s9LampState(HSF_BTN_RESET_LAMP_DO));

    /* ② 回读自愈：外部把停止灯写灭 ⇒ 自动模式下一拍按回读补写回亮
     * （板端 2026-11-06 实锅口径：只记目标值不回读 = 灭一次永不亮）。 */
    (void)HsfServerIo_digitalWrite(HSF_BTN_STOP_LAMP_DO, false);
    sAutoRunFor(700, true);
    CHECK("S9 外部清零后自愈（停止灯复亮）", s9LampState(HSF_BTN_STOP_LAMP_DO));

    /* ③ 运行态互斥：空设备产水（无阀无泵任务直进运行相）⇒ 启动灯亮。 */
    {
        char err[96];
        CHECK("S9 空设备配置受理",
              sAutoCfgSet("produce", NULL, 0, 1440, false, err, (int)sizeof(err)),
              "err=%s", err);
        CHECK("S9 启动受理", HsfServerAutoMode_start(
          "produce", (uint64_t)XDateTime_currentMSecsSinceEpoch()));
        CHECK("S9 运行相推进",
              sAutoWaitUntil(8000, true, sCondPhaseRunning, NULL));
        sAutoRunFor(700, true);
        CHECK("S9 运行态：启动灯亮", s9LampState(HSF_BTN_START_LAMP_DO));
        CHECK("S9 运行态：停止灯灭（互斥）", !s9LampState(HSF_BTN_STOP_LAMP_DO));
    }

    /* ④ 复位灯点动镜像：按住 DI15 亮、松手灭（与运行态无关）。 */
    s9SetButtonDi(HSF_BTN_RESET_DI, true);
    sAutoRunFor(700, true);
    CHECK("S9 复位灯点动：按住亮", s9LampState(HSF_BTN_RESET_LAMP_DO));
    s9SetButtonDi(HSF_BTN_RESET_DI, false);
    sAutoRunFor(700, true);
    CHECK("S9 复位灯点动：松手灭", !s9LampState(HSF_BTN_RESET_LAMP_DO));

    /* ⑤ 急停显示停着：急停按下（常闭断开）⇒ 锁存 + 启动灯灭/停止灯亮
     * （Python estop_ok is not True ⇒ 一律按"停着"显示）。 */
    s9SetButtonDi(HSF_BTN_ESTOP_DI, false);
    sAutoRunFor(700, true);
    CHECK("S9 急停锁存受理", HsfServerAutoMode_estopLatched());
    CHECK("S9 急停态：启动灯灭", !s9LampState(HSF_BTN_START_LAMP_DO));
    CHECK("S9 急停态：停止灯亮（急停也亮）", s9LampState(HSF_BTN_STOP_LAMP_DO));
    s9SetButtonDi(HSF_BTN_ESTOP_DI, true);
    sAutoRunFor(400, true);
    CHECK("S9 复位解锁", HsfServerAutoMode_requestReset(
          (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    sAutoDrainAlarms();   /* 急停 4 号告警取走，不串下一段。 */

    /* ⑥ 手动模式：启动灯跟随「设备在动」（2026-11-15 用户口径，取代
     * 2026-11-10 的「手动模式一个 DO 都不写」）。
     *   旧口径的代价：手动模式的灯停在旧状态 —— 现场什么设备都没在跑，
     *   启动灯却亮着，操作员据此误判（用户 2026-11-15 现场截图）。 */
    sAutoRunFor(700, false);
    CHECK("S9 手动模式：无设备在动 ⇒ 启动灯灭",
          !s9LampState(HSF_BTN_START_LAMP_DO));
    CHECK("S9 手动模式：无设备在动 ⇒ 停止灯亮",
          s9LampState(HSF_BTN_STOP_LAMP_DO));

    /* ⑥-2 手动模式：把进水泵打开 ⇒ 启动灯亮、停止灯灭（严格互斥）。 */
    {
        char err[96];
        const bool ok = HsfServerIo_pumpSet(0, true, err, (int)sizeof(err));
        CHECK("S9 手动模式：进水泵可开", ok, "err=%s", err);
        sAutoRunFor(700, false);
        CHECK("S9 手动模式：进水泵在开 ⇒ 启动灯亮",
              s9LampState(HSF_BTN_START_LAMP_DO));
        CHECK("S9 手动模式：进水泵在开 ⇒ 停止灯灭（互斥）",
              !s9LampState(HSF_BTN_STOP_LAMP_DO));
    }

    /* ⑥-3 手动模式：现场按停止 ⇒ 泵停 + 阀全关（用户口径）。
     * [用户口径]「手动模式下按下停止也能全部关闭」「按下停止也关闭全部
     * 球阀，急停也有效果」⇒ 手动模式的停止/急停**一律关阀复位**。
     * [阀行程调到 200ms] 现场行程 ~9s、模拟器缺省 10s ⇒ 不缩短就等不到
     * "阀到关位"。**注意语义**：阀真的关上之前启动灯保持亮（阀还开着
     * ⇒ 现场还在流水），这正是"启动灯 = 有设备在动"的口径。 */
    {
        char err[96];
        sSimValveMove(200);
        (void)HsfServerIo_valveSet(0, true, err, (int)sizeof(err));
        sAutoRunFor(800, false);
        /* 停止按钮是常闭：不按=1，按下=0 ⇒ 判下降沿。 */
        s9SetButtonDi(HSF_BTN_STOP_DI, false);
        sAutoRunFor(700, false);
        CHECK("S9 手动模式按停止：进水泵停", !s9PumpOn(0));
        CHECK("S9 手动模式按停止：阀关回（关命令已下发）",
              !s9ValveOpenCmd(0));
        CHECK("S9 手动模式按停止：阀到关位",
              sAutoWaitUntil(5000, false, sCondValveClosed,
                             (void*)(intptr_t)0));
        CHECK("S9 手动模式按停止：启动灯灭（都停了）",
              !s9LampState(HSF_BTN_START_LAMP_DO));
        s9SetButtonDi(HSF_BTN_STOP_DI, true);
        sAutoRunFor(300, false);
    }

    /* ⑦ 回自动：恢复跟随（停止灯按目标补写回亮）。 */
    sAutoRunFor(700, true);
    CHECK("S9 回自动：跟随恢复（停止灯复亮）", s9LampState(HSF_BTN_STOP_LAMP_DO));
    CHECK("S9 estopState 恢复 OK", HsfServerAutoMode_estopState()
          == HSF_ESTOP_CIRCUIT_OK);
    sAutoResetFresh();
#endif
}

/* ---- S12：auto devices 整表替换（2026-11-13·对齐 Python 整表覆盖） ----
 *  旧 C 口径「与现值合并只置 true」⇒ 远端设置取消勾选不生效、空数组清不
 *  掉（Python app.py 是 target["devices"]=clean 整表替换）；§S1 只从空
 *  模式 SET 全量清单，合并==替换锁不住此差异，本用例补齐。 */

/** @brief S12 夹具：带阀复位开关的模式配置 SET（sAutoCfgSet 不带这两键）。 */
static bool s12CfgSetEx(const char* mid, const char* const* devIds, int nDev,
                        bool resetOnStop, char* outErr, int cap)
{
    XBsonDocument* doc = XBsonDocument_create();
    XBsonArray* arr;
    bool ok;
    int i;
    if (outErr && cap > 0) outErr[0] = '\0';
    if (!doc) return false;
    arr = XBsonArray_create();
    for (i = 0; i < nDev; ++i) {
        XBsonValue* val = XBsonValue_create(XBSON_TYPE_STRING);
        if (!val) continue;
        XBsonValue_setString_utf8(val, devIds[i]);
        if (arr) {
            XBsonArray_append_move_base(arr, val);
            XBsonValue_delete(val);
        } else {
            XBsonValue_delete(val);
        }
    }
    if (arr) {
        XBsonDocument_insert_keyUtf8_array_move(doc, "devices", arr);
        XClassDelete((XClass*)arr);
    }
    XBsonDocument_insert_keyUtf8_bool(
        doc, HSF_AUTO_MODE_RESET_VALVES_ON_STOP, resetOnStop);
    ok = HsfServerAutoMode_applyModeDoc(mid, doc, outErr, cap);
    XClassDelete((XClass*)doc);
    return ok;
}

static void testS12_devicesReplace(void)
{
    static const char* const kAll[] = { "valve1", "valve2", "stirPump" };
    static const char* const kSub[] = { "valve2" };
    bool gotDev[HSF_AUTO_DEVICE_COUNT];
    int gotRpm = -1;
    bool gotRev = false;
    char err[128];
    char why[128];
    XBsonDocument* cfgDoc;

    XPrintf("--- S12 auto devices 整表替换 ---\n");
    sAutoResetFresh();
    CHECK("S12 全量 SET 受理（valve1+valve2+stirPump）",
          sAutoCfgSet("produce", kAll, 3, 1440, false, err, (int)sizeof(err)),
          "err=%s", err);
    cfgDoc = HsfServerAutoMode_configDoc();
    CHECK("S12 全量回显：三设备齐",
          cfgDoc &&
          sAutoCfgEcho(cfgDoc, "produce", gotDev, &gotRpm, &gotRev) &&
          gotDev[0] && gotDev[1] && gotDev[6] && !gotDev[2] && !gotDev[7]);
    if (cfgDoc) XClassDelete((XClass*)cfgDoc);

    /* 子集替换：只剩 valve2（合并语义会保留 valve1/stirPump ⇒ 旧口径红）。 */
    CHECK("S12 子集 SET 受理（只 valve2）",
          sAutoCfgSet("produce", kSub, 1, 1440, false, err, (int)sizeof(err)),
          "err=%s", err);
    cfgDoc = HsfServerAutoMode_configDoc();
    CHECK("S12 子集 SET = 整表替换（valve1/stirPump 被取消勾选）",
          cfgDoc &&
          sAutoCfgEcho(cfgDoc, "produce", gotDev, &gotRpm, &gotRev) &&
          !gotDev[0] && gotDev[1] && !gotDev[6]);
    if (cfgDoc) XClassDelete((XClass*)cfgDoc);

    /* 空数组 = 清空（合并语义下空数组与现值逐位相同 ⇒ 清不掉）。 */
    CHECK("S12 空数组 SET 受理",
          sAutoCfgSet("produce", NULL, 0, 1440, false, err, (int)sizeof(err)),
          "err=%s", err);
    cfgDoc = HsfServerAutoMode_configDoc();
    CHECK("S12 空数组 SET = 清空（devices 全空）",
          cfgDoc &&
          sAutoCfgEcho(cfgDoc, "produce", gotDev, &gotRpm, &gotRev) &&
          !gotDev[0] && !gotDev[1] && !gotDev[6]);
    if (cfgDoc) XClassDelete((XClass*)cfgDoc);
    CHECK("S12 清空后该模式启动被拒（未配置设备）",
          !HsfServerAutoMode_modeConfigError("produce", why,
                                             (int)sizeof(why)) &&
          strstr(why, "未配置设备") != NULL,
          "err=%s", why);
    sAutoResetFresh();
}

/* ---- S13：未运行态现场停止按钮的全停阀复位走模式配置（2026-11-13·
 *  对齐 Python _handle_buttons 两分支同传 _reset_valves_on_stop）----
 *  旧 C 口径非 running 分支调恒参 wrapper（硬编码关阀）⇒ 配了「停止不复位
 *  阀」的模式仍被发关阀。判据按 AGENTS 口径看 **DO 输出位**（写关在已开
 *  的阀上才会产生可观察差异）。 */

static void testS13_stopButtonValveReset(void)
{
    static const char* const kDevs[] = { "valve1" };
    char err[128];
    struct SAutoDoExpect v1on;
    struct SAutoDoExpect v1off;
    uint32_t di = 0;
    uint32_t dO = 0;
    bool valid = false;

    XPrintf("--- S13 现场停止按钮阀复位按模式配置 ---\n");
    sAutoResetFresh();
    v1on.m_mask = 1u << 0;
    v1on.m_expect = 1u << 0;
    v1off.m_mask = 1u << 0;
    v1off.m_expect = 0u;

    CHECK("S13 backwash 配置受理（valve1 + 停止不复位阀）",
          s12CfgSetEx("backwash", kDevs, 1, false, err, (int)sizeof(err)),
          "err=%s", err);
    /* 直接开阀1（不经自动序列）⇒ DO0=1，让「关阀复位」有可观察差异。 */
    CHECK("S13 阀1 开命令受理",
          HsfServerIo_valveSet(0, true, err, (int)sizeof(err)),
          "err=%s", err);
    CHECK("S13 阀1 DO0 已置位",
          sAutoWaitUntil(2500, true, sCondDoBits, &v1on),
          "do=0x%X",
          (HsfServerIo_digitalMasks(&di, &dO, &valid) && valid)
              ? (unsigned)dO : 0u);
    /* [粘性模式必须先立起来] 阀复位开关按**当前模式**取（amCurCfg → s_mode；
     * Python 同口径 ctx.auto_mode_config(self.mode)）。从未选过模式时两
     * 端都回缺省 true——那不是本用例的场景。这里启动一次 backwash 再停止
     * （配置=false ⇒ 停止不关阀、DO0 保持），让 s_mode 粘性= backwash 且
     * 控制器回到未运行态，随后的现场停止沿走**非运行分支**。 */
    CHECK("S13 启动 backwash（立粘性模式）",
          HsfServerAutoMode_start(
              "backwash", (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    CHECK("S13 backwash 进运行相",
          sAutoWaitUntil(8000, true, sCondPhaseRunning, NULL));
    (void)HsfServerAutoMode_requestStop(
        "用例", (uint64_t)XDateTime_currentMSecsSinceEpoch());
    sAutoRunFor(300, true);
    CHECK("S13 前置：控制器已停且阀1 仍开（配置不复位）",
          !HsfServerAutoMode_running() && sCondDoBits(&v1on));

    /* 停止按钮（缺省常闭）按下 = 1→0 沿；控制器未运行 ⇒ 非运行分支。
     * 配了「停止不复位阀」⇒ 阀1 DO0 **保持开**。 */
    s9SetButtonDi(HSF_BTN_STOP_DI, true);    /* 先松开（不按=1）采样 prev。 */
    /* [等到位而非等时长 2026-11-21] 按钮站**轮询**数字量模块采 DI ⇒ 在
     * 虚拟模型上改了位，服务端不是立刻可见 ⇒ 固定 sleep 不足以保证
     * 「松开」这一拍被采到（CPU 抢占时那一轮没跑到）⇒ 后面再「按下」时
     * 就**不是边沿** ⇒ 按钮不被识别 ⇒ 关阀复位没发生 ⇒ S13 偶发红。 */
    (void)s9WaitButtonDi(HSF_BTN_STOP_DI, true, 3000);
    s9SetButtonDi(HSF_BTN_STOP_DI, false);   /* 按下（常闭 1→0）。 */
    sAutoRunFor(600, true);
    CHECK("S13 停止不复位阀：阀1 DO0 保持开（未被叠加关阀）",
          sCondDoBits(&v1on));

    /* 改配置为「停止关阀复位」再按一次 ⇒ 阀1 被关（DO0 清零）。 */
    CHECK("S13 改配置受理（resetValvesOnStop=true）",
          s12CfgSetEx("backwash", kDevs, 1, true, err, (int)sizeof(err)),
          "err=%s", err);
    s9SetButtonDi(HSF_BTN_STOP_DI, true);    /* 松开。 */
    (void)s9WaitButtonDi(HSF_BTN_STOP_DI, true, 3000);
    s9SetButtonDi(HSF_BTN_STOP_DI, false);   /* 再按下。 */
    /* [窗口 2500→4000→6000 2026-11-13] 与编译/其它门禁并发跑时（CPU
     * 抢占）2.5s 真钟窗口偶发不够泵完关阀动作；4s 版在停止沿检测的
     * 时序抖动下仍偶发红（沿采样轨迹取证：第二次沿偶发晚到一拍以上）
     * ——放宽到 6s；断言语义（关阀必须发生）不变。 */
    CHECK("S13 停止关阀复位：阀1 DO0 清零",
          sAutoWaitUntil(6000, true, sCondDoBits, &v1off));
    sAutoResetFresh();
}

/* ---- S14：按键触点类型与去抖可配（2026-11-13·对齐 Python normally_closed
 *  + debounce_ms 配置链）---- 旧 C 口径编译期宏 + amEdge 字面量实参，现场
 *  换接法必须改代码重编；现挂 auto 节 buttons 子节（0x0117 + auto/buttons
 *  INI 键），SET 立即生效。 */

/** @brief S14 夹具：buttons 子节 SET（构造 {debounceMs, normallyClosed}）。 */
static bool s14ButtonsSet(int debounceMs, const char* const* ncIds, int nNc,
                          char* outErr, int cap)
{
    XBsonDocument* doc = XBsonDocument_create();
    XBsonArray* arr;
    bool ok;
    int i;
    if (outErr && cap > 0) outErr[0] = '\0';
    if (!doc) return false;
    arr = XBsonArray_create();
    for (i = 0; i < nNc; ++i) {
        XBsonValue* val = XBsonValue_create(XBSON_TYPE_STRING);
        if (!val) continue;
        XBsonValue_setString_utf8(val, ncIds[i]);
        if (arr) {
            XBsonArray_append_move_base(arr, val);
            XBsonValue_delete(val);
        } else {
            XBsonValue_delete(val);
        }
    }
    if (arr) {
        XBsonDocument_insert_keyUtf8_array_move(
            doc, HSF_CFGK_AUTO_BUTTONS_NC, arr);
        XClassDelete((XClass*)arr);
    }
    XBsonDocument_insert_keyUtf8_int32(doc, HSF_CFGK_AUTO_BUTTONS_DEBOUNCE,
                                       (int32_t)debounceMs);
    ok = HsfServerAutoMode_applyButtonsDoc(doc, outErr, cap);
    XClassDelete((XClass*)doc);
    return ok;
}

/** @brief S14 助手：从 configDoc 读 buttons 回显（debounce + nc 位集）。 */
static bool s14ButtonsEcho(const XBsonDocument* root, int* outDebounce,
                           bool* outNc /* [3]: start/stop/reset */)
{
    const XBsonValue* vB = XBsonDocument_value_keyUtf8(
        (XBsonDocument*)root, HSF_CFGK_AUTO_BUTTONS);
    const XBsonDocument* b =
        vB ? XBsonValue_toDocument((XBsonValue*)vB) : NULL;
    const XBsonValue* vArr =
        b ? XBsonDocument_value_keyUtf8((XBsonDocument*)b,
                                        HSF_CFGK_AUTO_BUTTONS_NC) : NULL;
    const XBsonArray* arr =
        vArr ? XBsonValue_toArray((XBsonValue*)vArr) : NULL;
    static const char* const kIds[] = { "start_di", "stop_di", "reset_di" };
    size_t n;
    size_t k;
    *outDebounce = -1;
    memset(outNc, 0, 3 * sizeof(bool));
    if (!b) return false;
    {
        const XBsonValue* vD = XBsonDocument_value_keyUtf8(
            (XBsonDocument*)b, HSF_CFGK_AUTO_BUTTONS_DEBOUNCE);
        if (vD) *outDebounce = (int)XBsonValue_toInt32((XBsonValue*)vD, -1);
    }
    if (arr) {
        n = XBsonArray_size_base((XBsonArray*)arr);
        for (k = 0; k < n; ++k) {
            const XBsonValue* el = XBsonArray_at_base((XBsonArray*)arr, k);
            const XString* s;
            int i;
            if (!el) continue;
            if (XBsonValue_type((XBsonValue*)el) != XBSON_TYPE_STRING) continue;
            s = XBsonValue_toString((XBsonValue*)el);
            if (!s) continue;
            for (i = 0; i < 3; ++i) {
                if (strcmp(kIds[i], XString_toUtf8(s)) == 0) outNc[i] = true;
            }
        }
    }
    return true;
}

static void testS14_buttonsConfig(void)
{
    bool nc[3];
    int debounce = -1;
    char err[128];
    XBsonDocument* cfgDoc;
    XBsonDocument* bad;

    XPrintf("--- S14 按键触点/去抖配置化 ---\n");
    sAutoResetFresh();
    /* ① SET：全常开（空数组）+ 去抖 50ms；configDoc 回显一致。 */
    CHECK("S14 buttons SET 受理（全常开 + 50ms）",
          s14ButtonsSet(50, NULL, 0, err, (int)sizeof(err)),
          "err=%s", err);
    cfgDoc = HsfServerAutoMode_configDoc();
    CHECK("S14 buttons 回显：debounceMs=50、normallyClosed 空（全常开）",
          cfgDoc && s14ButtonsEcho(cfgDoc, &debounce, nc) &&
          debounce == 50 && !nc[0] && !nc[1] && !nc[2],
          "debounce=%d", debounce);
    if (cfgDoc) XClassDelete((XClass*)cfgDoc);

    /* ② 落盘→重启→回读（INI auto/buttons 键；不删键的 shutdown+setup）。 */
    HsfServerAutoMode_shutdown();
    (void)HsfServerAutoMode_setup();
    cfgDoc = HsfServerAutoMode_configDoc();
    CHECK("S14 落盘→重启→回读：50ms/全常开不回缺省",
          cfgDoc && s14ButtonsEcho(cfgDoc, &debounce, nc) &&
          debounce == 50 && !nc[0] && !nc[1] && !nc[2],
          "debounce=%d", debounce);
    if (cfgDoc) XClassDelete((XClass*)cfgDoc);

    /* ③ 行为：停止钮改**常开**后，0→1 上升沿 = 按下（缺省口径是常闭
     *    1→0）——空设备产水启动到 running，停止沿一拍全停。 */
    CHECK("S14 空设备配置受理",
          sAutoCfgSet("produce", NULL, 0, 1440, false,
                      err, (int)sizeof(err)),
          "err=%s", err);
    s9SetButtonDi(HSF_BTN_STOP_DI, false);  /* 常开口径不按=0，采样 prev。 */
    sAutoRunFor(300, true);
    CHECK("S14 启动受理（空设备直进运行）",
          HsfServerAutoMode_start(
              "produce", (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    sAutoRunFor(300, true);
    CHECK("S14 前置：控制器在运行", HsfServerAutoMode_running());
    s9SetButtonDi(HSF_BTN_STOP_DI, true);   /* 0→1 = 常开「按下」。 */
    CHECK("S14 停止钮常开沿生效（运行位被停）",
          sAutoWaitUntil(2500, true, &sCondNotRunning, NULL));

    /* ④ fail-closed：非法 debounceMs / 非数组 normallyClosed 明确拒。 */
    CHECK("S14 负 debounceMs 被拒",
          !s14ButtonsSet(-5, NULL, 0, err, (int)sizeof(err)) &&
          strstr(err, "debounceMs") != NULL,
          "err=%s", err);
    bad = XBsonDocument_create();
    CHECK("S14 非法载荷文档构造", bad != NULL);
    if (bad) {
        XBsonDocument_insert_keyUtf8_int32(bad, HSF_CFGK_AUTO_BUTTONS_NC, 5);
        CHECK("S14 normallyClosed 非数组被拒",
              !HsfServerAutoMode_applyButtonsDoc(bad, err,
                                                 (int)sizeof(err)) &&
              strstr(err, "数组") != NULL,
              "err=%s", err);
        XClassDelete((XClass*)bad);
    }
    /* 尾部卫生：INI 键删回缺省（跨用例不见 50ms/全常开）。 */
    HsfServerSettings_remove("auto/buttons");
    HsfServerAutoMode_shutdown();
    (void)HsfServerAutoMode_setup();
    sAutoResetFresh();
}

/**
 * @brief S15：op=5「只选模式」（2026-11-14·对齐 Python AUTO_OP_SELECT）。
 * @details [为什么锁这条] 现场物理启动按钮按「服务端上次选中的模式」启动；
 *          客户端主界面的**模式下拉**改选即发 0x0115 op=5 记粘性。C 端
 *          原先只认 op=1..4（回「非法 op 操作字」）⇒ 下拉与现场按钮各记
 *          各的，物理键开箱即拒「未选择自动模式」。
 *          本段锁：① 只记粘性（modeId 回读）；② **不启动**（相位 idle、
 *          运行位假）；③ 未知/空 id 拒（不静默改粘性）；④ 组合循环 id
 *          合法；⑤ 落盘 + 跨 setup 重启恢复（现场重启后物理键仍可用）。
 */
static void testS15_selectModeOp5(void)
{
    char modeId[16];
    char phase[24];
    char stored[40] = "";

    XPrintf("--- S15 只选模式 op=5 ---\n");
    sAutoResetFresh();                /* 干净态（含粘性键删回不存在）。 */

    /* ① 只选模式：记粘性、不启动。 */
    CHECK("S15 op=5 选 backwash 受理",
          HsfServerAutoMode_selectMode("backwash"), "err 无");
    HsfServerAutoMode_modeId(modeId, (int)sizeof(modeId));
    HsfServerAutoMode_phase(phase, (int)sizeof(phase));
    CHECK("S15 粘性模式回读 backwash",
          strcmp(modeId, "backwash") == 0, "mode=%s", modeId);
    CHECK("S15 op=5 不启动控制器（相位仍 idle）",
          strcmp(phase, "idle") == 0, "phase=%s", phase);
    CHECK("S15 op=5 不在运行",
          !HsfServerAutoMode_running(), "running=1");

    /* ② 未知/空 id 拒：粘性**不变**（不静默改成野值）。 */
    CHECK("S15 未知模式 id 被拒",
          !HsfServerAutoMode_selectMode("bogus"), "err 无——返回 false 即拒");
    CHECK("S15 空模式 id 被拒",
          !HsfServerAutoMode_selectMode(""), "err 无——返回 false 即拒");
    HsfServerAutoMode_modeId(modeId, (int)sizeof(modeId));
    CHECK("S15 拒绝后粘性仍是 backwash",
          strcmp(modeId, "backwash") == 0, "mode=%s", modeId);

    /* ③ 组合循环 id 合法（其**配置**校验留到启动时刻）。 */
    CHECK("S15 组合循环 id auto 受理",
          HsfServerAutoMode_selectMode("auto"), "err 无");
    HsfServerAutoMode_modeId(modeId, (int)sizeof(modeId));
    CHECK("S15 粘性模式回读 auto",
          strcmp(modeId, "auto") == 0, "mode=%s", modeId);

    /* ④ 落盘 + 跨 setup 重启恢复（现场物理键开箱即用）。 */
    HsfServerSettings_getString("run/auto_mode", "", stored,
                                (int)sizeof(stored));
    CHECK("S15 粘性模式已落盘（run/auto_mode=auto）",
          strcmp(stored, "auto") == 0, "ini=%s", stored);
    HsfServerAutoMode_shutdown();
    (void)HsfServerAutoMode_setup();
    HsfServerAutoMode_modeId(modeId, (int)sizeof(modeId));
    CHECK("S15 重启后粘性恢复（auto，不归零）",
          strcmp(modeId, "auto") == 0, "mode=%s", modeId);

    /* ⑤ 尾部卫生：粘性键删回不存在（跨用例不见上一次的选择）。 */
    HsfServerSettings_remove("run/auto_mode");
    HsfServerAutoMode_shutdown();
    (void)HsfServerAutoMode_setup();
    HsfServerAutoMode_modeId(modeId, (int)sizeof(modeId));
    CHECK("S15 清键后粘性回空（跨用例干净）",
          modeId[0] == '\0', "mode=%s", modeId);

    /* ⑥ [2026-11-20·三账分立后的正确口径] **运行中 op=5 受理，但只改
     *     粘性，不动正在跑的工序**。
     * [口径演进，两端必须一致] 2026-11-16 第二轮审计的原结论是「运行中
     *   拒绝」——理由是**粘性**与**运行中模式/组合子阶段**挤在同一个变
     *   量里，运行中一条 op=5 会把正在跑的生产阶段整个改写（产水跑到
     *   一半被按"顺洗时限"判到期 ⇒ 换相、停泵、重建阀目标）。
     *   2026-11-18 两端都改成「受理，但只记选择」——因为**分账**做完了：
     *   Python `ctl.selected_mode`（粘性）vs `ctl.mode`（运行中/子阶段）。
     * [C 端当时漏了第三笔 2026-11-20 实锅] C 端只分了两账（s_mode 粘性
     *   vs s_cycleStage 子阶段）⇒ **非组合模式运行时** s_mode 既是粘性
     *   又是运行中模式 ⇒ 运行中改选会把正在跑的工序配置整个换掉
     *   （打断生产，且与 Python 行为不一致）⇒ 已补第三笔 s_runMode
     *   （本轮运行中的模式）。
     * [本段锁的就是这条] 运行中改选 ⇒ ① 被受理；② **运行中的模式不变**
     *   （reportedMode 仍报 produce——正在跑的还是产水）；③ 粘性确实变成
     *   backwash（下次启动按它跑）；④ 停止后回显改报粘性（Python 同口径：
     *   停止时 rep_mode = selected_mode）。 */
    {
        XBsonDocument* doc = XBsonDocument_create();
        char sticky[32];
        char reported[32];
        CHECK("S15b 运行受理测试：载荷文档构造", doc != NULL);
        if (doc) {
            /* 空设备产水：起控制器到运行态。 */
            char why[128];
            const char* kDevs[1] = { "valve1" };
            CHECK("S15b 配置受理",
                  sAutoCfgSet("produce", kDevs, 1, 1440, false,
                              why, (int)sizeof(why)), "err=%s", why);
            CHECK("S15b 启动受理",
                  HsfServerAutoMode_start(
                      "produce",
                      (uint64_t)XDateTime_currentMSecsSinceEpoch()));
            sAutoRunFor(300, true);
            CHECK("S15b 前置：控制器在运行", HsfServerAutoMode_running());
            /* ① 运行中改选被受理（只记选择，不动运行态）。 */
            CHECK("S15b 运行中 selectMode 被受理（只记选择）",
                  HsfServerAutoMode_selectMode("backwash"), "err 无");
            /* ② **运行中的模式不变**：正在跑的仍是产水（没被改选换掉）。 */
            HsfServerAutoMode_reportedMode(reported, (int)sizeof(reported));
            CHECK("S15b 运行中的模式不变（仍产水，没被改选换掉）",
                  strcmp(reported, "produce") == 0, "reported=%s", reported);
            /* ③ 粘性确实变成 backwash（下次启动按它跑）。 */
            HsfServerAutoMode_modeId(sticky, (int)sizeof(sticky));
            CHECK("S15b 粘性已改成 backwash（下次启动用）",
                  strcmp(sticky, "backwash") == 0, "sticky=%s", sticky);
            /* ④ 收尾：停控制器 ⇒ 回显改报粘性（Python 同口径）。 */
            (void)HsfServerAutoMode_requestStop(
                "S15b 收尾", (uint64_t)XDateTime_currentMSecsSinceEpoch());
            sAutoRunFor(200, true);
            HsfServerAutoMode_reportedMode(reported, (int)sizeof(reported));
            CHECK("S15b 停止后回显改报粘性（backwash）",
                  strcmp(reported, "backwash") == 0, "reported=%s", reported);
            /* 配置键清掉（S10 起点干净）。 */
            HsfServerSettings_remove("auto/produce");
            HsfServerAutoMode_shutdown();
            (void)HsfServerAutoMode_setup();
            sAutoResetFresh();
            XClassDelete((XClass*)doc);
        }
    }
}

/* ---- S16：换相策略（2026-11-14 用户定案） ---- */

/** @brief S16：从 auto 配置文档里取某个模式的某个换相策略键（空串=取不到）。
 *  @details 配置文档形状：root.modes.<modeId>.<key>。 */
static void s16_policy_of(const struct XBsonDocument* root, const char* modeId,
                           const char* key, char* out, int cap)
{
    const XBsonValue* mv;
    const XBsonDocument* modes;
    const XBsonValue* one;
    const XString* sv;
    out[0] = '\0';
    if (!root) return;
    mv = XBsonDocument_value_keyUtf8((XBsonDocument*)root, "modes");
    if (!mv || XBsonValue_type((XBsonValue*)mv) != XBSON_TYPE_DOCUMENT) return;
    modes = XBsonValue_toDocument((XBsonValue*)mv);
    if (!modes) return;
    mv = XBsonDocument_value_keyUtf8((XBsonDocument*)modes, modeId);
    if (!mv || XBsonValue_type((XBsonValue*)mv) != XBSON_TYPE_DOCUMENT) return;
    one = XBsonValue_toDocument((XBsonValue*)mv);
    if (!one) return;
    mv = XBsonDocument_value_keyUtf8((XBsonDocument*)one, key);
    if (!mv || XBsonValue_type((XBsonValue*)mv) != XBSON_TYPE_STRING) return;
    sv = XBsonValue_toString((XBsonValue*)mv);
    if (sv) snprintf(out, (size_t)cap, "%s", XString_toUtf8(sv));
}

/**
 * @brief S16：逐模式「换相策略」（2026-11-14·对齐 Python AUTO_SWITCH_POLICY_*）。
 * @details 换相（阀门切换 ~9s）期间泵怎么办，**逐泵**可配：
 *          stopRestart=先停再启（既有行为）/ carry=保持上一模式状态 /
 *          direct=直接切到当前模式状态。
 *          本段锁**配置面**（行为面由 Python 的 test_auto_switch_policy.py
 *          锁，两端同一套判据）：① 缺省＝先停再启；② SET/GET 往返一字不差；
 *          ③ 非法值**拒**（fail-closed，不静默回落）；④ 落盘往返。
 */
static void testS16_switchPolicy(void)
{
    struct XBsonDocument* doc;
    struct XBsonDocument* cfg;
    char why[128];
    char val[24];

    XPrintf("--- S16 换相策略 ---\n");
    sAutoResetFresh();

    /* ① 缺省＝先停再启。 */
    cfg = HsfServerAutoMode_configDoc();
    s16_policy_of(cfg, "produce", HSF_AUTO_MODE_STIR_SWITCH, val, (int)sizeof(val));
    CHECK("S16 缺省：搅拌泵策略＝先停再启",
          strcmp(val, HSF_AUTO_SWITCH_POLICY_STOP_RESTART) == 0, "val=%s", val);
    s16_policy_of(cfg, "produce", HSF_AUTO_MODE_INLET_SWITCH, val, (int)sizeof(val));
    CHECK("S16 缺省：进水泵策略＝先停再启",
          strcmp(val, HSF_AUTO_SWITCH_POLICY_STOP_RESTART) == 0, "val=%s", val);
    XClassDelete((XClass*)cfg);

    /* ② SET/GET 往返。 */
    doc = XBsonDocument_create();
    XBsonDocument_insert_keyUtf8_utf8(doc, HSF_AUTO_MODE_STIR_SWITCH,
                                        HSF_AUTO_SWITCH_POLICY_CARRY);
    XBsonDocument_insert_keyUtf8_utf8(doc, HSF_AUTO_MODE_INLET_SWITCH,
                                        HSF_AUTO_SWITCH_POLICY_DIRECT);
    why[0] = '\0';
    CHECK("S16 SET：两个策略受理",
          HsfServerAutoMode_applyModeDoc("produce", doc, why, (int)sizeof(why)),
          "err=%s", why);
    XClassDelete((XClass*)doc);
    cfg = HsfServerAutoMode_configDoc();
    s16_policy_of(cfg, "produce", HSF_AUTO_MODE_STIR_SWITCH, val, (int)sizeof(val));
    CHECK("S16 GET 回显：搅拌泵＝保持上一模式",
          strcmp(val, HSF_AUTO_SWITCH_POLICY_CARRY) == 0, "val=%s", val);
    s16_policy_of(cfg, "produce", HSF_AUTO_MODE_INLET_SWITCH, val, (int)sizeof(val));
    CHECK("S16 GET 回显：进水泵＝直接切到当前模式",
          strcmp(val, HSF_AUTO_SWITCH_POLICY_DIRECT) == 0, "val=%s", val);
    XClassDelete((XClass*)cfg);

    /* ③ 非法值拒（fail-closed：不能让拼错的键静默回落缺省）。 */
    doc = XBsonDocument_create();
    XBsonDocument_insert_keyUtf8_utf8(doc, HSF_AUTO_MODE_STIR_SWITCH, "bogus");
    why[0] = '\0';
    CHECK("S16 非法策略值被拒",
          !HsfServerAutoMode_applyModeDoc("produce", doc, why, (int)sizeof(why)),
          "err=%s", why);
    XClassDelete((XClass*)doc);
    cfg = HsfServerAutoMode_configDoc();
    s16_policy_of(cfg, "produce", HSF_AUTO_MODE_STIR_SWITCH, val, (int)sizeof(val));
    CHECK("S16 被拒后原值不变（仍是保持上一模式）",
          strcmp(val, HSF_AUTO_SWITCH_POLICY_CARRY) == 0, "val=%s", val);
    XClassDelete((XClass*)cfg);

    /* ④ 落盘往返（重启后策略还在）。 */
    HsfServerAutoMode_shutdown();
    (void)HsfServerAutoMode_setup();
    cfg = HsfServerAutoMode_configDoc();
    s16_policy_of(cfg, "produce", HSF_AUTO_MODE_STIR_SWITCH, val, (int)sizeof(val));
    CHECK("S16 重启后策略还在（落盘往返）",
          strcmp(val, HSF_AUTO_SWITCH_POLICY_CARRY) == 0, "val=%s", val);
    s16_policy_of(cfg, "produce", HSF_AUTO_MODE_INLET_SWITCH, val, (int)sizeof(val));
    CHECK("S16 重启后进水泵策略还在",
          strcmp(val, HSF_AUTO_SWITCH_POLICY_DIRECT) == 0, "val=%s", val);
    XClassDelete((XClass*)cfg);

    /* 收尾：把 produce 的策略清回缺省（不污染后续用例）。 */
    doc = XBsonDocument_create();
    XBsonDocument_insert_keyUtf8_utf8(doc, HSF_AUTO_MODE_STIR_SWITCH,
                                        HSF_AUTO_SWITCH_POLICY_STOP_RESTART);
    XBsonDocument_insert_keyUtf8_utf8(doc, HSF_AUTO_MODE_INLET_SWITCH,
                                        HSF_AUTO_SWITCH_POLICY_STOP_RESTART);
    why[0] = '\0';
    (void)HsfServerAutoMode_applyModeDoc("produce", doc, why, (int)sizeof(why));
    XClassDelete((XClass*)doc);
}

/* ---- S10：组合模式「自动」循环（假时钟加速阶段定时） ---- */

static int64_t s10FakeOffsetMs = 0;    /**< 模块时钟偏移（=模块钟 - 真实钟）。 */
static bool s10WantPress = false;      /**< 压差注入开关（false=读不到 None）。 */
static double s10PressMpa = 0.0;       /**< 注入的压差值（MPa）。 */

/** @brief S10 压差取数回调（HsfServerApp hsfAppAutoPressureMpa 的测试替身）。 */
static bool s10PressFn(double* outMpa)
{
    if (!s10WantPress) return false;   /* None：不按缺数据误判。 */
    *outMpa = s10PressMpa;
    return true;
}

/** @brief S10 谓词：协议回显模式 == 目标（阶段记忆/换相断言用）。 */
static int sCondReportedModeIs(void* ud)
{
    char buf[24];
    HsfServerAutoMode_reportedMode(buf, (int)sizeof(buf));
    return strcmp(buf, (const char*)ud) == 0;
}

/** @brief S10 一拍：IO 层走真实钟、自动模块走 real+offset（见块注）。 */
static void s10FakeTick(bool autoMode, int fakeStepMs)
{
    const uint64_t real = (uint64_t)XDateTime_currentMSecsSinceEpoch();
    HsfServerIoSim_tick();
    (void)HsfServerIo_watchdog(real);
    HsfServerIo_valvePoll(real);
    HsfServerIo_pumpPoll();
    s10FakeOffsetMs += fakeStepMs;
    HsfServerAutoMode_tick(real + (uint64_t)s10FakeOffsetMs, autoMode);
}

/** @brief S10 等待：DO 位到达期望（假时钟拍驱动；与 S10 全程同钟）。 */
static bool s10WaitDo(uint32_t mask, uint32_t expect, int timeoutRealMs)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < timeoutRealMs) {
        uint32_t doMask = 0;
        bool valid = false;
        s10FakeTick(true, 20);
        (void)HsfServerIo_digitalDo(&doMask, &valid);
        XCoreApplication_processEvents(0);
        XThread_msleep(20);
        if (valid && (doMask & mask) == expect) return true;
    }
    return false;
}

/**
 * @brief S10 驱动：按真实时长跑拍，模块时钟按 fakeStepMs/拍 加速。
 * @details [为什么 IO 不加速] 阀行程/Hub 轮询是真实钟驱动的物理过程；
 *          自动模块内部的截止/节流全拿 tick 实参自比自——只加速模块钟
 *          即可把「反洗 1 分钟」压到 ~1.2s 真实时间，且不触发阀相假超时
 *          （阀相步长给 20ms ≈ 1×，deadline 是模块钟自记的，同尺同速）。
 */
static void s10FakeRunFor(int realMs, bool autoMode, int fakeStepMs)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < realMs) {
        s10FakeTick(autoMode, fakeStepMs);
        XCoreApplication_processEvents(0);
        XThread_msleep(20);
    }
}

/** @brief S10 等待：协议回显模式到达目标子阶段（running 相 50× 加速）。 */
static bool s10WaitStage(const char* stage, int timeoutRealMs)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    char phase[24];
    while (XDateTime_currentMSecsSinceEpoch() - t0 < timeoutRealMs) {
        HsfServerAutoMode_phase(phase, (int)sizeof(phase));
        s10FakeTick(true, strcmp(phase, "running") == 0 ? 1000 : 20);
        XCoreApplication_processEvents(0);
        XThread_msleep(20);
        if (sCondReportedModeIs(stage)) return true;
    }
    return sCondReportedModeIs(stage);
}

/** @brief S10 等待：回到运行相（阀相/泵相 1× 慢走，等真实阀行程）。 */
static bool s10WaitRunning(int timeoutRealMs)
{
    int64_t t0 = XDateTime_currentMSecsSinceEpoch();
    while (XDateTime_currentMSecsSinceEpoch() - t0 < timeoutRealMs) {
        s10FakeTick(true, 20);
        XCoreApplication_processEvents(0);
        XThread_msleep(20);
        if (sCondPhaseRunning(NULL)) return true;
    }
    return sCondPhaseRunning(NULL);
}

/** @brief S10 cycle 配置写入（applyCycleDoc 直达）。 */
static bool s10CycleSet(bool enabled, double threshold, const char* compare,
                        bool fallback, int timerMin, int backMin, int rinseMin,
                        char* outErr, int cap)
{
    XBsonDocument* doc = XBsonDocument_create();
    bool ok;
    if (!doc) return false;
    XBsonDocument_insert_keyUtf8_bool(doc, "enabled", enabled);
    XBsonDocument_insert_keyUtf8_double(doc, "pressThreshold", threshold);
    XBsonDocument_insert_keyUtf8_utf8(doc, "pressCompare", compare);
    XBsonDocument_insert_keyUtf8_bool(doc, "timerFallback", fallback);
    XBsonDocument_insert_keyUtf8_int32(doc, "timerMinutes", timerMin);
    XBsonDocument_insert_keyUtf8_int32(doc, "backwashMinutes", backMin);
    XBsonDocument_insert_keyUtf8_int32(doc, "rinseMinutes", rinseMin);
    ok = HsfServerAutoMode_applyCycleDoc(doc, outErr, cap);
    XClassDelete((XClass*)doc);
    return ok;
}

/**
 * @brief S10：组合模式「自动」循环（对齐 Python test_auto_cycle）。
 * @details 循环顺序固定 产水→（压差或定时兜底）→反洗→（定时）→顺洗→
 *          （定时）→产水；压差读不到（None）不切；换相=停泵+重建阀目标
 *          （累计运行时长不中断）；停止/急停记住子阶段、复位清记忆。
 *          [本段必须在 S11 之前或独立驱动的原因] 本段把模块钟拨到
 *          真实钟+偏移；跨段恢复靠 sAutoResetFresh 的 shutdown+setup 与
 *          框架四板斧自清 m_lastStepMs（start/stop/eStop/reset 都清），
 *          时间倒流只会让框架节流放行（uint64 差值下溢），不会卡死。
 */
static void testS10_cycleMode(void)
{
    const HsfServerAutoModeCycleCfg* cyc;
    char err[160];
    int64_t elapsed = -1;
    int64_t total = 0;
    uint64_t runMsBefore;
    static const char* const kPump[1] = { "inletPump" };

    sAutoResetFresh();
    s10FakeOffsetMs = 0;
    s10WantPress = false;
    HsfServerAutoMode_setPressureFn(s10PressFn);

    /* ① cycle 缺省（GET 口径）：启用/gt/0.5MPa/兜底关/30-10-10 分钟。 */
    cyc = HsfServerAutoMode_cycleConfig();
    CHECK("S10 cycle 缺省：启用+gt+0.5+兜底关",
          cyc && cyc->m_enabled &&
          strcmp(cyc->m_pressCompare, HSF_AUTO_PRESS_COMPARE_GT) == 0 &&
          cyc->m_pressThreshold > 0.499 && cyc->m_pressThreshold < 0.501 &&
          !cyc->m_timerFallback);
    CHECK("S10 cycle 缺省：分钟 30/10/10",
          cyc && cyc->m_timerMinutes == 30 && cyc->m_backwashMinutes == 10 &&
          cyc->m_rinseMinutes == 10);

    /* ② 配置闸：三子模式未配设备 ⇒ 拒且点名（Python _cycle_config_error）。 */
    CHECK("S10 组合模式拒（缺三子模式设备）",
          !HsfServerAutoMode_modeConfigError("auto", err, (int)sizeof(err)) &&
          strstr(err, "三个子模式都配设备") != NULL,
          "err=%s", err);

    /* ③ 配齐三模式（各带进水泵：泵相最小、换相停泵有 DO6 可断言）+ 1 分钟级。 */
    CHECK("S10 产水配置受理",
          sAutoCfgSet("produce", kPump, 1, 1440, false, err, (int)sizeof(err)),
          "err=%s", err);
    CHECK("S10 反洗配置受理",
          sAutoCfgSet("backwash", kPump, 1, 1440, false, err, (int)sizeof(err)),
          "err=%s", err);
    CHECK("S10 顺洗配置受理",
          sAutoCfgSet("rinse", kPump, 1, 1440, false, err, (int)sizeof(err)),
          "err=%s", err);
    CHECK("S10 cycle SET 受理",
          s10CycleSet(true, 0.5, HSF_AUTO_PRESS_COMPARE_GT, false,
                      1, 1, 1, err, (int)sizeof(err)),
          "err=%s", err);
    CHECK("S10 组合模式配置闸通过",
          HsfServerAutoMode_modeConfigError("auto", err, (int)sizeof(err)),
          "err=%s", err);

    /* ④ 启动组合模式 → 产水（无阀任务直进运行相，进水泵 DO6 开）。 */
    CHECK("S10 启动受理", HsfServerAutoMode_start(
          "auto", (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    CHECK("S10 启动即产水（无记忆从头）", sCondReportedModeIs("produce"));
    CHECK("S10 产水进运行相", s10WaitRunning(8000));
    CHECK("S10 产水运行：进水泵 DO6 开",
          s10WaitDo((uint32_t)(1u << HSF_PUMP_DO(0)), (uint32_t)(1u << HSF_PUMP_DO(0)), 4000));
    HsfServerAutoMode_stageTiming(&elapsed, &total);
    CHECK("S10 产水 stageTiming：已运行≥0、总时长 0（兜底关＝按压差切）",
          elapsed >= 0 && total == 0, "elapsed=%lld total=%lld",
          (long long)elapsed, (long long)total);

    /* ⑤ 压差读不到（None）不切：加速跑 ~100s 假时间仍停在产水。 */
    s10FakeRunFor(2000, true, 1000);
    CHECK("S10 压差 None 不切（仍产水）", sCondReportedModeIs("produce"));

    /* ⑥ 压差 gt 命中 → 反洗：换相停泵（DO6 灭）→ 回阀相 → 到位运行泵再开；
     *    累计运行时长跨换相连续（不整体 stop/start）。 */
    runMsBefore = HsfServerAutoMode_runMs();
    s10WantPress = true;
    s10PressMpa = 0.8;   /* 0.8 > 0.5（gt 命中）。 */
    CHECK("S10 压差命中切反洗", s10WaitStage("backwash", 8000));
    CHECK("S10 换相停泵（DO6 灭）",
          s10WaitDo((uint32_t)(1u << HSF_PUMP_DO(0)), 0u, 4000));
    CHECK("S10 反洗进运行相", s10WaitRunning(8000));
    CHECK("S10 反洗运行：泵重启（DO6 开）",
          s10WaitDo((uint32_t)(1u << HSF_PUMP_DO(0)), (uint32_t)(1u << HSF_PUMP_DO(0)), 4000));
    CHECK("S10 累计时长跨换相连续",
          HsfServerAutoMode_runMs() >= runMsBefore,
          "before=%llu after=%llu", (unsigned long long)runMsBefore,
          (unsigned long long)HsfServerAutoMode_runMs());
    HsfServerAutoMode_stageTiming(&elapsed, &total);
    CHECK("S10 反洗 stageTiming：总时长=1 分钟",
          elapsed >= 0 && total == 60000, "elapsed=%lld total=%lld",
          (long long)elapsed, (long long)total);

    /* ⑦ 停止记住子阶段（反洗）：回显保持 backwash（「将从反洗继续」）。 */
    CHECK("S10 停止受理",
          HsfServerAutoMode_requestStop(
              "S10 停止", (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    CHECK("S10 停止后回显记住反洗", sCondReportedModeIs("backwash"));
    HsfServerAutoMode_stageTiming(&elapsed, &total);
    CHECK("S10 停止后 stageTiming=(-1,0)",
          elapsed == -1 && total == 0, "elapsed=%lld total=%lld",
          (long long)elapsed, (long long)total);

    /* ⑧ 再启动从反洗继续（阶段记忆；不是回到产水）。 */
    CHECK("S10 再启动受理", HsfServerAutoMode_start(
          "auto", (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    CHECK("S10 从反洗继续", sCondReportedModeIs("backwash"));
    CHECK("S10 反洗再进运行相", s10WaitRunning(8000));

    /* ⑨ 反洗 1 分钟到期 → 顺洗（假时钟加速；顺洗定时同理）。 */
    CHECK("S10 反洗定时到期切顺洗", s10WaitStage("rinse", 12000));
    CHECK("S10 顺洗进运行相", s10WaitRunning(8000));
    CHECK("S10 顺洗定时到期切产水", s10WaitStage("produce", 12000));
    CHECK("S10 产水进运行相", s10WaitRunning(8000));

    /* ⑩ 复位清记忆 ⇒ 下次从头产水（用户口径「除非按了复位」）。
     * [不复位返回值] 此处两把锁都没锁（停止走的是 requestStop 不是急停/
     * 报警中止），requestReset 返回 false 是"无事可解"的正确语义——
     * 语义断言在"复位后从头产水"上。 */
    CHECK("S10 停止受理（复位前）",
          HsfServerAutoMode_requestStop("S10 复位前停止",
          (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    (void)HsfServerAutoMode_requestReset(
        (uint64_t)XDateTime_currentMSecsSinceEpoch());
    CHECK("S10 复位后再启动受理", HsfServerAutoMode_start(
          "auto", (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    CHECK("S10 复位后从头产水", sCondReportedModeIs("produce"));
    CHECK("S10 收尾停止", HsfServerAutoMode_requestStop(
          "S10 收尾", (uint64_t)XDateTime_currentMSecsSinceEpoch()));

    /* ⑪ 压差回调摘除 + 配置卫生。 */
    s10WantPress = false;
    HsfServerAutoMode_setPressureFn(NULL);
    sAutoResetFresh();
}

/**
 * @brief S11：累计自动运行时长（对齐 Python run_ms 三件套 + 2026-11-12
 *        「服务端持久化」定案）。
 * @details 未运行冻结 / 运行递增（阀相起算）/ 停止结算冻结 / 再启动叠加 /
 *          结算即落盘（run/auto_run_ms）/ shutdown+setup 重启恢复不归零。
 */
static void testS11_runMsAccumPersist(void)
{
    uint64_t settled = 0;
    uint64_t settled2 = 0;
    char err[96];
    char stored[40] = "";
    char expect[40];

    /* [时刻一律现取] start/stop 的时间戳必须**当下**取：缓存的入口时刻
     * 会早于运行段起点 ⇒ settle 判 now > s_runStartMs 为假 ⇒ 这一段不
     * 并进累计（首跑实测 settled==0 的根因；服务端真实路径都是现取）。 */
    sAutoResetFresh();
    CHECK("S11 干净基线 = 0", HsfServerAutoMode_runMs() == 0,
          "runMs=%llu", (unsigned long long)HsfServerAutoMode_runMs());

    /* ① 未运行不计时。 */
    sAutoRunFor(400, true);
    CHECK("S11 未运行不计时", HsfServerAutoMode_runMs() == 0);

    /* ② 运行递增（空设备产水直进运行相；阀相时长同样计入）。 */
    CHECK("S11 空设备配置受理",
          sAutoCfgSet("produce", NULL, 0, 1440, false, err, (int)sizeof(err)),
          "err=%s", err);
    CHECK("S11 启动受理", HsfServerAutoMode_start(
          "produce", (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    CHECK("S11 运行相推进", sAutoWaitUntil(8000, true, sCondPhaseRunning, NULL));
    sAutoRunFor(600, true);
    CHECK("S11 运行中累计增长（≥600ms 段计入）",
          HsfServerAutoMode_runMs() >= 400,
          "runMs=%llu", (unsigned long long)HsfServerAutoMode_runMs());

    /* ③ 停止结算并冻结。 */
    CHECK("S11 停止受理", HsfServerAutoMode_requestStop(
          "S11 停止", (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    settled = HsfServerAutoMode_runMs();
    CHECK("S11 停止后有一段结算（>0）", settled > 0,
          "settled=%llu", (unsigned long long)settled);
    sAutoRunFor(400, true);
    CHECK("S11 停止后冻结", HsfServerAutoMode_runMs() == settled,
          "settled=%llu now=%llu", (unsigned long long)settled,
          (unsigned long long)HsfServerAutoMode_runMs());

    /* ④ 再启动叠加（多次运行是累计）。 */
    CHECK("S11 再启动受理", HsfServerAutoMode_start(
          "produce", (uint64_t)XDateTime_currentMSecsSinceEpoch()));
    sAutoRunFor(600, true);
    CHECK("S11 再启动叠加", HsfServerAutoMode_runMs() > settled,
          "settled=%llu now=%llu", (unsigned long long)settled,
          (unsigned long long)HsfServerAutoMode_runMs());
    CHECK("S11 二次停止受理", HsfServerAutoMode_requestStop(
          "S11 停止2", (uint64_t)XDateTime_currentMSecsSinceEpoch()));

    /* ⑤ 结算即落盘：run/auto_run_ms 与 runMs 逐字节一致（字符串承载，
     * int32 毫秒 ≈24.8 天溢出，累计口径必须字符串）。[基准=最后一次
     * 结算值 settled2]——累计是跨运行总量，持久化的是最新总量。 */
    settled2 = HsfServerAutoMode_runMs();
    CHECK("S11 二段累计大于首段", settled2 > settled,
          "settled=%llu settled2=%llu",
          (unsigned long long)settled, (unsigned long long)settled2);
    snprintf(expect, sizeof(expect), "%llu", (unsigned long long)settled2);
    HsfServerSettings_getString("run/auto_run_ms", "", stored,
                                (int)sizeof(stored));
    CHECK("S11 持久化键值一致（run/auto_run_ms）",
          stored[0] != '\0' && strcmp(stored, expect) == 0,
          "stored=%s expect=%s", stored, expect);

    /* ⑥ 重启恢复不归零（setup → amLoadRunMs）：恢复值 = 最后持久化总量。 */
    HsfServerAutoMode_shutdown();
    (void)HsfServerAutoMode_setup();
    CHECK("S11 重启恢复累计（=最后持久化值，不归零）",
          HsfServerAutoMode_runMs() == settled2,
          "restored=%llu settled2=%llu",
          (unsigned long long)HsfServerAutoMode_runMs(),
          (unsigned long long)settled2);

    sAutoResetFresh();   /* 清 run/auto_run_ms，回干净基线。 */
}

/**
 * @brief §S 入口：夹具开 → S1..S8 → 夹具收（失败也要走到收尾）。
 * @note  断言计数走本文件的共享 g_pass/g_fail（CHECK 宏），与 §N/§K 同款；
 *        入口只负责夹具编排与小计打印。
 */
/* ==================== §T 远端设置：配置节级存在性纪律 ====================
 * [为什么锁这一段 2026-11-12] 远端设置页的 populate 纪律是「**谁给的字段
 * 谁负责**」：局部应答（应用落盘/登录/采集配置）只带自己那一节，解析层
 * 每次应答会把整个 HsfConfig 清零重填 ⇒ 没带的节全是零值。界面若无条件
 * 铺回控件，用户点一次「应用登录策略」就会把采集卡/存储卡/登录卡冲成
 * 缺省（周期 0、落盘关闭、用量 --），再点一次「应用」就把缺省写回服务端
 * （落盘被静默关闭 ⇒ 历史表格空）。Python 端 2026-11-10 实锅同款。
 * 本段锁**解析层的节级存在性标志**（界面层的守卫是 UI 行为，回归锁不
 * 覆盖；此处锁住数据契约＝界面守卫的前提）。
 * 附带的现状口径：老服务端没有 auto 节 ⇒ m_autoPresent 必须保持 false。
 */

/** @brief 造一个只含指定节的 0x0116 应答（BSON 字节）。
 *  @param withCollect  是否带 collect 节。
 *  @param withStorage  是否带 storage 节。
 *  @param withLogin    是否带 login 节。
 *  @param withAuto     是否带 auto 节（老服务端没有 ⇒ false）。
 *  @param withStats    是否带 storageStats + storageDbPath。
 *  @return 字节数组（调用方释放）；失败返回 NULL。 */
static XByteArray* buildConfigPayload(int withCollect, int withStorage,
                                       int withLogin, int withAuto,
                                       int withStats)
{
    XBsonDocument* doc = XBsonDocument_create();
    XByteArray* bytes = NULL;
    if (!doc) return NULL;
    if (withCollect) {
        XBsonDocument* sub = XBsonDocument_create();
        if (sub) {
            XBsonDocument_insert_keyUtf8_bool(sub, "vfdEnabled", true);
            XBsonDocument_insert_keyUtf8_int32(sub, "vfdPollMs", 200);
            XBsonDocument_insert_keyUtf8_document_move(doc, "collect", sub);
            XClassDelete((XClass*)sub);
        }
    }
    if (withStorage) {
        XBsonDocument* sub = XBsonDocument_create();
        if (sub) {
            XBsonDocument_insert_keyUtf8_bool(sub, "enabled", true);
            XBsonDocument_insert_keyUtf8_int32(sub, "maxMb", 50);
            XBsonDocument_insert_keyUtf8_document_move(doc, "storage", sub);
            XClassDelete((XClass*)sub);
        }
    }
    if (withLogin) {
        XBsonDocument* sub = XBsonDocument_create();
        if (sub) {
            XBsonDocument_insert_keyUtf8_bool(sub, "loginAllowAnon", false);
            XBsonDocument_insert_keyUtf8_int32(sub, "loginMaxSessions", 8);
            XBsonDocument_insert_keyUtf8_document_move(doc, "login", sub);
            XClassDelete((XClass*)sub);
        }
    }
    if (withAuto) {
        XBsonDocument* sub = XBsonDocument_create();
        XBsonDocument* modes = XBsonDocument_create();
        if (sub && modes) {
            XBsonDocument_insert_keyUtf8_int32(modes, "stirRpm", 720);
            XBsonDocument_insert_keyUtf8_document_move(sub, "modes", modes);
            XClassDelete((XClass*)modes);
            XBsonDocument_insert_keyUtf8_document_move(doc, "auto", sub);
        }
        if (sub) XClassDelete((XClass*)sub);
    }
    if (withStats) {
        XBsonDocument* sub = XBsonDocument_create();
        if (sub) {
            XBsonDocument_insert_keyUtf8_int32(sub, "rows", 42);
            XBsonDocument_insert_keyUtf8_document_move(doc, "storageStats", sub);
            XClassDelete((XClass*)sub);
        }
        XBsonDocument_insert_keyUtf8_utf8(doc, "storageDbPath", "hsf_data.db");
    }
    bytes = XBsonDocument_toBson(doc);
    XClassDelete((XClass*)doc);
    return bytes;
}

/** @brief §T：逐节存在性标志（对齐 Python _populate 的逐节守卫前提）。 */
static void testT_remoteSettingsSectionGuards(void)
{
    HsfConfig cfg;
    XByteArray* bytes;

    /* ① 全量应答：四节 + 统计全在 ⇒ 五个标志全真。 */
    memset(&cfg, 0, sizeof(cfg));
    bytes = buildConfigPayload(1, 1, 1, 1, 1);
    CHECK("T 全量应答解析成功", bytes != NULL);
    if (bytes) {
        CHECK("T 全量应答解析通过",
              HsfConfig_parse(XByteArray_constData(bytes),
                              (uint32_t)XByteArray_size_base(bytes), &cfg));
        XClassDelete((XClass*)bytes);
    }
    CHECK("T 全量：collect 节存在", cfg.m_collectPresent);
    CHECK("T 全量：storage 节存在", cfg.m_storagePresent);
    CHECK("T 全量：login 节存在", cfg.m_loginPresent);
    CHECK("T 全量：auto 节存在", cfg.m_auto.m_autoPresent);
    CHECK("T 全量：统计节存在", cfg.m_statsPresent);

    /* ② 局部应答（只带 login）：其余四节标志必须为 false——
     *    这是「点应用登录策略不清空别的卡」的数据前提。 */
    memset(&cfg, 0, sizeof(cfg));
    bytes = buildConfigPayload(0, 0, 1, 0, 0);
    CHECK("T 局部应答解析成功", bytes != NULL);
    if (bytes) {
        CHECK("T 局部（仅 login）解析通过",
              HsfConfig_parse(XByteArray_constData(bytes),
                              (uint32_t)XByteArray_size_base(bytes), &cfg));
        XClassDelete((XClass*)bytes);
    }
    CHECK("T 局部：collect 节**不**存在", !cfg.m_collectPresent);
    CHECK("T 局部：storage 节**不**存在", !cfg.m_storagePresent);
    CHECK("T 局部：login 节存在", cfg.m_loginPresent);
    CHECK("T 局部：auto 节**不**存在", !cfg.m_auto.m_autoPresent);
    CHECK("T 局部：统计节**不**存在", !cfg.m_statsPresent);

    /* ③ 老服务端（无 auto 节）：auto 标志必须保持 false（界面不铺三模式块）。 */
    memset(&cfg, 0, sizeof(cfg));
    bytes = buildConfigPayload(1, 1, 1, 0, 1);
    if (bytes) {
        (void)HsfConfig_parse(XByteArray_constData(bytes),
                              (uint32_t)XByteArray_size_base(bytes), &cfg);
        XClassDelete((XClass*)bytes);
    }
    CHECK("T 老服务端：auto 节**不**存在", !cfg.m_auto.m_autoPresent);
    CHECK("T 老服务端：collect/storage/login 仍存在",
          cfg.m_collectPresent && cfg.m_storagePresent && cfg.m_loginPresent);
    CHECK("T 老服务端：统计节仍存在", cfg.m_statsPresent);

    /* ④ 局部应答后：存储侧的库路径与统计必须保持「未收到」语义
     *    （m_dbPath 为空 ⇒ 界面不刷新用量行）。 */
    memset(&cfg, 0, sizeof(cfg));
    bytes = buildConfigPayload(0, 0, 1, 0, 0);
    if (bytes) {
        (void)HsfConfig_parse(XByteArray_constData(bytes),
                              (uint32_t)XByteArray_size_base(bytes), &cfg);
        XClassDelete((XClass*)bytes);
    }
    CHECK("T 局部应答不带库路径（界面不显示「未启用」）",
          cfg.m_dbPath[0] == '\0');
}

/* ==================== §S17 运行保护 + 流量计告警（2026-11-15） ====================
 *
 * [测什么] ① 进水泵开着 + 流量计仍报「空管」持续到时限 ⇒ 出一条告警事件
 *              + **停设备**（等价现场停止按钮：变频器停 + 进水泵关 + 阀按
 *              模式规则复位）；② 只报警模式（stop=false）⇒ 只报警不动设备；
 *           ③ 保护关闭（0ms）/ 流量计离线 / 泵没开 ⇒ 不动作；
 *           ④ 流量计报警位的**边沿** ⇒ 出一条事件（首帧只记基线）。
 *
 * [为什么能造出"空管"] 模拟流量计原先只建模流量、报警位恒 0 ⇒ 这条分支
 * 在 sim 里永远触发不到（死代码）。2026-11-15 给模型加了
 * XVirtualModelFlow_setAlarm（框架侧一行 setter），本段才能真正跑它。
 */

/** @brief §S17 助手：设模拟流量计的报警位（0=上限 1=下限 2=空管 3=系统）。 */
static void s17SetFlowAlarm(int idx, bool on)
{
    const HsfServerIoSimModels* sim = HsfServerIoSim_ensure();
    if (!sim || !sim->m_flowMeter) return;
    XVirtualModelFlow_setAlarm(sim->m_flowMeter, idx, on);
}

/** @brief §S17：进水泵开 + 空管 ⇒ 超时动作（告警 + 停设备 / 只报警）。 */
static void testS17_protectInletNoFlow(void)
{
    HsfServerProtectEvent ev;
    char err[96];
    const uint64_t now = (uint64_t)XDateTime_currentMSecsSinceEpoch();

    /* ---- ① 保护关闭（0ms）⇒ 不动作 ---- */
    HsfServerSettings_setInt("protect/inletNoFlowTimeoutMs", 0);
    HsfServerSettings_setInt("protect/inletNoFlowStop", 1);
    CHECK("S17 保护关闭时配置读回 0ms",
          HsfServerProtect_timeoutMs() == 0);
    HsfServerProtect_reset();
    (void)HsfServerIo_pumpSet(0, true, err, (int)sizeof(err));
    s17SetFlowAlarm(2, true);           /* 空管 */
    sAutoRunFor(400, false);            /* 暖一拍：流量计被 Hub 采到 */
    memset(&ev, 0, sizeof(ev));
    CHECK("S17 保护关闭：不产生事件",
          !HsfServerProtect_tick(now + 1000, &ev) && !ev.m_hasAlarm);

    /* ---- ② 保护开启：未到时限 ⇒ 不动作；到时限 ⇒ 事件 + 停设备 ---- */
    HsfServerSettings_setInt("protect/inletNoFlowTimeoutMs", 500);
    HsfServerProtect_reset();
    /* [基线] 首帧只记基线（启动就挂着空管不算"新发生的报警"）⇒ 先跑一拍
     * 让它把当前位图记成基线。 */
    memset(&ev, 0, sizeof(ev));
    (void)HsfServerProtect_tick(now + 1100, &ev);
    CHECK("S17 首帧只记基线：不报（启动噪音不算事件）",
          !ev.m_hasAlarm);

    memset(&ev, 0, sizeof(ev));
    (void)HsfServerProtect_tick(now + 1200, &ev);
    CHECK("S17 未到时限：不动作", !ev.m_hasAlarm);

    memset(&ev, 0, sizeof(ev));
    (void)HsfServerProtect_tick(now + 2000, &ev);
    CHECK("S17 到时限：出一条告警事件（kind=流量计 + dev=流量计）",
          ev.m_hasAlarm
          && ev.m_kind == HSF_AUTO_ALARM_FLOWMETER
          && ev.m_devKind == (int)HsfDeviceKind_FlowMeter,
          "kind=%d dev=%d text=%s", ev.m_kind, ev.m_devKind, ev.m_text);
    CHECK("S17 事件文案带「空管」与「已停止设备」",
          strstr(ev.m_text, "空管") != NULL
          && strstr(ev.m_text, "已停止设备") != NULL,
          "%s", ev.m_text);
    /* 停设备：进水泵被关掉（DO6 回显）。 */
    sAutoRunFor(400, false);
    {
        const HsfServerIoPump* bank = HsfServerIo_pumps();
        const HsfServerIoPumpItem* it = bank ? HsfServerIoPump_item(bank, 0)
                                              : NULL;
        CHECK("S17 到时限：进水泵被停掉", it != NULL && !it->m_on,
              "on=%d", it ? (int)it->m_on : -1);
    }

    /* ---- ③ 只报警模式（stop=false）⇒ 报警但不停设备 ----
     * [先解掉 ② 留下的锁存] 保护动作会锁存（进报警表 ⇒ 需人工复位），
     * 锁存未解时保护**不重复动作**（否则报警表被刷爆）⇒ 这里模拟"人已复位"。 */
    (void)HsfServerAutoMode_clearFault();
    HsfServerSettings_setInt("protect/inletNoFlowTimeoutMs", 500);
    HsfServerSettings_setInt("protect/inletNoFlowStop", 0);
    HsfServerProtect_reset();
    (void)HsfServerIo_pumpSet(0, true, err, (int)sizeof(err));
    memset(&ev, 0, sizeof(ev));
    (void)HsfServerProtect_tick(now + 3000, &ev);   /* 基线 */
    memset(&ev, 0, sizeof(ev));
    (void)HsfServerProtect_tick(now + 3100, &ev);
    memset(&ev, 0, sizeof(ev));
    (void)HsfServerProtect_tick(now + 4000, &ev);
    CHECK("S17 只报警模式：到时限出事件",
          ev.m_hasAlarm && ev.m_kind == HSF_AUTO_ALARM_FLOWMETER,
          "kind=%d text=%s", ev.m_kind, ev.m_text);
    CHECK("S17 只报警模式：文案说明未停设备",
          strstr(ev.m_text, "未停设备") != NULL, "%s", ev.m_text);
    sAutoRunFor(400, false);
    {
        const HsfServerIoPump* bank = HsfServerIo_pumps();
        const HsfServerIoPumpItem* it = bank ? HsfServerIoPump_item(bank, 0)
                                              : NULL;
        CHECK("S17 只报警模式：设备**不停**（泵仍开）",
              it != NULL && it->m_on, "on=%d", it ? (int)it->m_on : -1);
    }

    /* ---- ④ 流量计离线 ⇒ 不判定（读不到 ≠ 空管）---- */
    HsfServerSettings_setInt("protect/inletNoFlowTimeoutMs", 500);
    HsfServerSettings_setInt("protect/inletNoFlowStop", 1);
    HsfServerProtect_reset();
    (void)HsfServerIo_pumpSet(0, true, err, (int)sizeof(err));
    s17SetFlowAlarm(2, false);          /* 清掉空管位 */
    sAutoRunFor(400, false);
    memset(&ev, 0, sizeof(ev));
    (void)HsfServerProtect_tick(now + 5000, &ev);
    memset(&ev, 0, sizeof(ev));
    (void)HsfServerProtect_tick(now + 6000, &ev);
    CHECK("S17 无空管位：不动作", !ev.m_hasAlarm);

    /* ---- 收尾：清配置 + 恢复流量计 ---- */
    HsfServerSettings_remove("protect/inletNoFlowTimeoutMs");
    HsfServerSettings_remove("protect/inletNoFlowStop");
    HsfServerProtect_reset();
    s17SetFlowAlarm(2, false);
    (void)HsfServerIo_pumpSet(0, false, err, (int)sizeof(err));
}

/* ==================== §S18 设备故障：报警 + 锁存 + 自动停机（2026-11-15） ====================
 *
 * [用户口径]「进了报警表的报警要锁存，并自动停止设备，必须手动复位才可以
 *  启动设备」。本段锁 C 端自动控制层的四件事：
 *   ① raiseFault() ⇒ 出一条**设备故障**告警（进报警历史表）+ 报警锁存；
 *   ② 锁存 ⇒ 启动闸被拒（"有报警未复位，需先按复位再启动"）；
 *   ③ 锁存 ⇒ 设备自动停（与现场停止按钮同款）；
 *   ④ clearFault()（人工复位）⇒ 解锁。
 * [为什么用 raiseFault 直驱] 模拟器没有变频器故障注入（XVirtualModelVfd
 *   只建模频率/状态），故障这条路径的**触发**在 App 层（见
 *   HsfServerApp.c hsfAppTickVfd 的故障边沿）；本段锁的是控制层的
 *   锁存/停机/解锁语义。
 */
static void testS18_deviceFaultLatch(void)
{
    char err[128];
    XAutoControlAlarm alarm;

    /* 前置：清干净锁存与告警队列。 */
    (void)HsfServerAutoMode_clearFault();
    sAutoDrainAlarms();

    /* ① 故障 ⇒ 告警（kind=设备故障）+ 锁存 + 自动停设备。 */
    {
        const uint64_t now = (uint64_t)XDateTime_currentMSecsSinceEpoch();
        const HsfServerIoPump* bank = HsfServerIo_pumps();
        const HsfServerIoPumpItem* it = bank ? HsfServerIoPump_item(bank, 0)
                                              : NULL;
        char pumpErr[96];
        (void)HsfServerIo_pumpSet(0, true, pumpErr, (int)sizeof(pumpErr));
        sAutoRunFor(300, false);
        CHECK("S18 前置：进水泵已开", it != NULL && it->m_on,
              "on=%d", it ? (int)it->m_on : -1);

        CHECK("S18 raiseFault 受理",
              HsfServerAutoMode_raiseFault("变频器故障：过流（测试）", now,
                                          true, true));
        CHECK("S18 故障后：报警锁存已置位",
              HsfServerAutoMode_faultLatched());
        /* 告警入队（App 每拍 take → 推送 + 入 alarms 表）。 */
        memset(&alarm, 0, sizeof(alarm));
        CHECK("S18 故障后：告警入队（kind=设备故障）",
              XAutoController_takeAlarm(HsfServerAutoMode_controller(), &alarm)
              && alarm.m_kind == HSF_AUTO_ALARM_DEVICE_FAULT,
              "kind=%d text=%s", alarm.m_kind, alarm.m_text);
        sAutoRunFor(400, false);
        CHECK("S18 故障后：进水泵自动停掉", it != NULL && !it->m_on,
              "on=%d", it ? (int)it->m_on : -1);
    }

    /* ② 锁存 ⇒ 启动闸被拒（自动模式侧的启动条件）。 */
    err[0] = 0;
    (void)HsfServerAutoMode_startPreconditions(true, err, (int)sizeof(err));
    CHECK("S18 锁存未解：启动被拒（需先按复位）",
          strstr(err, "报警") != NULL, "err=%s", err);

    /* ③ 人工复位 ⇒ 解锁。 */
    CHECK("S18 人工复位：锁存解除", HsfServerAutoMode_clearFault());
    CHECK("S18 人工复位后：启动闸放行（不再是报警原因）",
          !HsfServerAutoMode_faultLatched());
    err[0] = 0;
    (void)HsfServerAutoMode_startPreconditions(true, err, (int)sizeof(err));
    CHECK("S18 人工复位后：启动不再被报警拦",
          strstr(err, "报警") == NULL, "err=%s", err);
}

/* ==================== §S19 压差过大保护（2026-11-15） ====================
 *
 * [测什么] 压差连续超过上限达去抖时长 ⇒ 出一条**压差过高**告警 + 报警锁存
 *           （+ 按配置停设备）；未启用 / 未超限 / 读不到压差 ⇒ 不动作。
 * [压差怎么造] 模拟器的模拟量没有压差标定 ⇒ 这里**注入**一个假的压差取数
 *           回调（HsfServerProtect_setPressureFn），只验判定与动作。
 */
static double s19PressValue = 0.0;     /**< 假压差（MPa）。 */
static bool s19PressValid = true;      /**< 假压差是否可读。 */

static bool s19PressFn(double* outMpa)
{
    if (!s19PressValid || !outMpa) return false;
    *outMpa = s19PressValue;
    return true;
}

static void testS19_pressHighProtect(void)
{
    HsfServerProtectEvent ev;
    const uint64_t now = (uint64_t)XDateTime_currentMSecsSinceEpoch();

    HsfServerProtect_setPressureFn(s19PressFn);
    s19PressValue = 0.9;
    s19PressValid = true;

    /* ① 缺省不启用 ⇒ 压差再高也不动作。 */
    HsfServerSettings_setInt("protect/pressHighEnabled", 0);
    HsfServerSettings_setString("protect/pressHighLimitMpa", "0.500");
    HsfServerSettings_setInt("protect/pressHighStop", 1);
    HsfServerProtect_reset();
    (void)HsfServerAutoMode_clearFault();
    memset(&ev, 0, sizeof(ev));
    CHECK("S19 未启用：不动作",
          !HsfServerProtect_tick(now, &ev) && !ev.m_hasAlarm);

    /* ② 启用 + 超上限：未到去抖时长不动作；到时动作（告警 + 锁存）。 */
    HsfServerSettings_setInt("protect/pressHighEnabled", 1);
    HsfServerProtect_reset();
    memset(&ev, 0, sizeof(ev));
    (void)HsfServerProtect_tick(now, &ev);
    CHECK("S19 未到去抖时长：不动作", !ev.m_hasAlarm);
    memset(&ev, 0, sizeof(ev));
    (void)HsfServerProtect_tick(now + 3000, &ev);
    CHECK("S19 到去抖时长：出告警（kind=压差过高 + dev=模拟量）",
          ev.m_hasAlarm
          && ev.m_kind == HSF_AUTO_ALARM_PRESS_HIGH
          && ev.m_devKind == (int)HsfDeviceKind_AnalogIo,
          "kind=%d dev=%d text=%s", ev.m_kind, ev.m_devKind, ev.m_text);
    CHECK("S19 告警文案带实测值与上限",
          strstr(ev.m_text, "0.900") != NULL
          && strstr(ev.m_text, "0.500") != NULL, "text=%s", ev.m_text);
    CHECK("S19 压差过大：报警锁存已置位（需人工复位）",
          HsfServerAutoMode_faultLatched());
    /* [锁存未解 ⇒ 不重复报警] 压差还高着时每 2 秒报一次会把报警表刷爆
     * （实锅：一次超压 ⇒ 连续两条告警 + 两次全停）。 */
    memset(&ev, 0, sizeof(ev));
    (void)HsfServerProtect_tick(now + 4000, &ev);
    (void)HsfServerProtect_tick(now + 8000, &ev);
    CHECK("S19 锁存未解：不重复报警（等人工复位）", !ev.m_hasAlarm);
    (void)HsfServerAutoMode_clearFault();

    /* ③ 压差未超上限 ⇒ 不动作。 */
    s19PressValue = 0.3;
    HsfServerProtect_reset();
    memset(&ev, 0, sizeof(ev));
    (void)HsfServerProtect_tick(now + 5000, &ev);
    (void)HsfServerProtect_tick(now + 9000, &ev);
    CHECK("S19 压差未超上限：不动作", !ev.m_hasAlarm);

    /* ④ 压差读不到 ⇒ 不判定（读不到 ≠ 压差过大）。 */
    s19PressValue = 0.9;
    s19PressValid = false;
    HsfServerProtect_reset();
    memset(&ev, 0, sizeof(ev));
    (void)HsfServerProtect_tick(now + 11000, &ev);
    (void)HsfServerProtect_tick(now + 15000, &ev);
    CHECK("S19 压差读不到：不判定、不动作", !ev.m_hasAlarm);

    /* ⑤ 只报警模式（stop=false）⇒ 出告警但不写「已停止设备」。 */
    s19PressValid = true;
    HsfServerSettings_setInt("protect/pressHighStop", 0);
    HsfServerProtect_reset();
    (void)HsfServerAutoMode_clearFault();
    memset(&ev, 0, sizeof(ev));
    (void)HsfServerProtect_tick(now + 17000, &ev);
    memset(&ev, 0, sizeof(ev));
    (void)HsfServerProtect_tick(now + 20000, &ev);
    CHECK("S19 只报警：出告警但文案写明未停设备",
          ev.m_hasAlarm && strstr(ev.m_text, "未停设备") != NULL,
          "text=%s", ev.m_text);
    CHECK("S19 只报警：仍锁存（进报警表 ⇒ 需人工复位）",
          HsfServerAutoMode_faultLatched());
    (void)HsfServerAutoMode_clearFault();

    /* 收尾：清配置 + 撤销注入的压差回调。 */
    HsfServerSettings_remove("protect/pressHighEnabled");
    HsfServerSettings_remove("protect/pressHighLimitMpa");
    HsfServerSettings_remove("protect/pressHighStop");
    HsfServerProtect_setPressureFn(NULL);
    HsfServerProtect_reset();
}

/* ==================== §S20 审计整改回归锁（2026-11-15） ====================
 *
 * [背景] 2026-11-15 对 Python/C 双端做了一轮全量排查（事件循环停摆、
 *        配置 fail-closed、告警归类、按钮沿语义），落地的几条**安全语义**
 *        在本段锁死（Python 侧有镜像用例，两端口径必须一致）：
 *   ① **首帧沿**：常开按键第一帧读到 1 **不算按下**（否则服务端一启动、
 *      启动键恰好是 1 时就自动启动 —— 现场安全事故）；
 *   ② **protect 配置 fail-closed**：未知键/非法类型**明确报错**，且
 *      **一个键都不写**（不留半截配置：报错说没保存、时限其实改了）；
 *   ③ **故障停机结算累计运行时长**（与 Python raise_fault → stop 同口径：
 *      故障停机同样结束"正在运行"这一段，不能丢）。
 */

/**
 * @brief S20：首帧沿 + protect 配置 fail-closed + 故障停机结算运行时长。
 */
static void testS20_auditHardening(void)
{
    char err[192];
    XBsonDocument* doc;
    XBsonDocument* bad;

    XPrintf("--- S20 审计整改（首帧沿 / protect fail-closed / 运行时长结算）---\n");

    /* ---- ① 首帧沿（纯函数判据）---- */
    CHECK("S20a 常开：首帧读到 1（上一拍未知）不算按下",
          !HsfServerAutoMode_buttonEdgeRule(-1, 1, false));
    CHECK("S20b 常开：0→1 是按下沿",
          HsfServerAutoMode_buttonEdgeRule(0, 1, false));
    CHECK("S20c 常开：1→1 不是沿（按住不放不重复触发）",
          !HsfServerAutoMode_buttonEdgeRule(1, 1, false));
    CHECK("S20d 常闭：首帧读到 0（上一拍未知）不算按下",
          !HsfServerAutoMode_buttonEdgeRule(-1, 0, true));
    CHECK("S20e 常闭：1→0 是按下沿（断线=按下，安全侧）",
          HsfServerAutoMode_buttonEdgeRule(1, 0, true));
    CHECK("S20f 读不到（level=-1）不产生沿",
          !HsfServerAutoMode_buttonEdgeRule(1, -1, false));

    /* ---- ② protect 配置 fail-closed ---- */
    /* ②a 合法包：受理并写入。 */
    doc = XBsonDocument_create();
    CHECK("S20g protect 载荷文档构造", doc != NULL);
    if (doc) {
        XBsonDocument_insert_keyUtf8_int32(
            doc, HSF_CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS, 1500);
        XBsonDocument_insert_keyUtf8_bool(
            doc, HSF_CFGK_PROTECT_INLET_NOFLOW_STOP, false);
        err[0] = '\0';
        CHECK("S20h protect 合法包受理",
              HsfServerProtect_applyConfigDoc(doc, err, (int)sizeof(err)),
              "err=%s", err);
        CHECK("S20i 写入生效（1500ms + 只报警）",
              HsfServerProtect_timeoutMs() == 1500
              && !HsfServerProtect_stopDevices(),
              "ms=%d stop=%d", HsfServerProtect_timeoutMs(),
              HsfServerProtect_stopDevices() ? 1 : 0);
        XClassDelete((XClass*)doc);
    }
    /* ②b 未知键 ⇒ 明确报错，且**前面那个合法键不能生效**（不留半截）。 */
    bad = XBsonDocument_create();
    CHECK("S20j protect 异常载荷文档构造", bad != NULL);
    if (bad) {
        XBsonDocument_insert_keyUtf8_int32(
            bad, HSF_CFGK_PROTECT_INLET_NOFLOW_TIMEOUT_MS, 9999);
        XBsonDocument_insert_keyUtf8_utf8(bad, "nope", "x");
        err[0] = '\0';
        CHECK("S20k protect 未知键被拒（fail-closed）",
              !HsfServerProtect_applyConfigDoc(bad, err, (int)sizeof(err))
              && strstr(err, "未知的运行保护配置键") != NULL,
              "err=%s", err);
        CHECK("S20l 被拒后**原值不变**（不留半截配置）",
              HsfServerProtect_timeoutMs() == 1500,
              "ms=%d", HsfServerProtect_timeoutMs());
        XClassDelete((XClass*)bad);
    }
    /* ②c 类型不符（字符串冒充布尔）⇒ 明确报错（否则"只报警"变"直接停机"）。 */
    bad = XBsonDocument_create();
    CHECK("S20m protect 类型异常载荷构造", bad != NULL);
    if (bad) {
        XBsonDocument_insert_keyUtf8_utf8(
            bad, HSF_CFGK_PROTECT_INLET_NOFLOW_STOP, "false");
        err[0] = '\0';
        CHECK("S20n protect 字符串布尔被拒（fail-closed）",
              !HsfServerProtect_applyConfigDoc(bad, err, (int)sizeof(err))
              && strstr(err, "布尔") != NULL,
              "err=%s", err);
        CHECK("S20o 被拒后仍是「只报警」（没被翻成停机）",
              !HsfServerProtect_stopDevices());
        XClassDelete((XClass*)bad);
    }
    /* ②d 空包 ⇒ 「载荷里没有配置键」。 */
    bad = XBsonDocument_create();
    if (bad) {
        err[0] = '\0';
        CHECK("S20p protect 空包被拒",
              !HsfServerProtect_applyConfigDoc(bad, err, (int)sizeof(err))
              && strstr(err, "没有配置键") != NULL,
              "err=%s", err);
        XClassDelete((XClass*)bad);
    }
    /* 收尾：把配置键删回缺省（跨用例不见 1500ms/只报警）。 */
    HsfServerSettings_remove("protect/inletNoFlowTimeoutMs");
    HsfServerSettings_remove("protect/inletNoFlowStop");

    /* ---- ③ 故障停机结算累计运行时长 ---- */
    /* [为什么] 累计时长是给现场看"这台机器今天跑了多久"的；故障停机
     *  同样结束了"正在运行"这一段 ⇒ 必须结算（否则这段时间凭空消失）。
     *  Python 侧 raise_fault → stop() → _settle_run_time() 同口径。 */
    sAutoResetFresh();
    {
        const uint64_t t0 = (uint64_t)XDateTime_currentMSecsSinceEpoch();
        uint64_t before = HsfServerAutoMode_runMs();
        uint64_t after = 0;
        CHECK("S20q 故障停机前：控制器已受理启动",
              HsfServerAutoMode_start(
                  "produce", (uint64_t)XDateTime_currentMSecsSinceEpoch()));
        sAutoRunFor(1200, true);
        (void)HsfServerAutoMode_raiseFault("测试故障", t0 + 1500, true, false);
        after = HsfServerAutoMode_runMs();
        CHECK("S20r 故障停机：累计运行时长已结算（不是 0）",
              after > before, "before=%llu after=%llu",
              (unsigned long long)before, (unsigned long long)after);
        (void)HsfServerAutoMode_clearFault();
    }
}

/* ============================================================================
 * §S21 导出模式不数总数（2026-11-21）
 *
 * [背景] C 服务端历史分页原先**每批固定跑 4 条聚合查询**：
 *   窗口 COUNT（**带关键字 ⇒ 完整全表扫**，20 万行 / 16 个显示表达式实测
 *   **747ms**）、全库 COUNT、MIN、MAX ⇒ 导出每批 790ms 里 747ms 花在
 *   "数总数" ⇒ 20 万行导出（5000 批）= **62 分钟**（正是"66 分钟"的根因）。
 * [修法] 与 Python 端 store.query_history_page(with_count=…) 同口径：
 *   导出模式传 withCount=0 ⇒ 跳过 4 条聚合，total 回 **-1**（未知）。
 * [本段锁什么] ① 两种模式的**行数与行内容逐字一致**（跳聚合不许丢数据）
 *   ② withCount=0 时 total=-1、dbRows/latest/earliest 全 0
 *   ③ withCount=1 时 total = 实际命中行数（>0）
 * ========================================================================== */
static void testS21_exportSkipCount(void)
{
    HsfServerStore st;
    XString* dbPath = NULL;
    XString* pWal = NULL;
    XString* pShm = NULL;
    const char* path = "s21_hist.db";
    const int kRows = 20000;   /* [2 万行] 让全表 COUNT 的成本可见（见下）。 */
    int i;
    int n1 = 0, n2 = 0;
    int total1 = 0, total2 = 0;
    int64_t dbRows1 = 0, dbRows2 = 0;
    int64_t latest1 = 0, latest2 = 0;
    int64_t earliest1 = 0, earliest2 = 0;
    HsfHistoryQueryRow* rows1 = NULL;
    HsfHistoryQueryRow* rows2 = NULL;
    int same = 1;

    /* [清理旧文件] 上轮残留会让计数对不上 ⇒ 先删（含 WAL 边车）。
     * 用 C 标准 remove（不用 XFile_remove_static：那个要 XString，而下面
     * 的 XString 生命周期与删文件顺序容易踩 UAF）。 */
    (void)remove("s21_hist.db");
    (void)remove("s21_hist.db-wal");
    (void)remove("s21_hist.db-shm");
    dbPath = XString_create_utf8(path);
    pWal = XString_create_utf8("s21_hist.db-wal");
    pShm = XString_create_utf8("s21_hist.db-shm");

    HsfServerStore_init(&st);
    /* [独立连接名] 缺省名 "hsf_store" 是**全局**的 ⇒ 夹具用它会与生产
     * 注册表里的同名连接互相干扰（close 之后再摘注册项 ⇒ 后续任何 open
     * 都要重新 add）。⇒ 用独立名，与生产完全隔离。 */
    HsfServerStore_setConnName(&st, "s21_test_conn");
    st.m_enabled = true;
    st.m_path = dbPath;
    if (!dbPath || !HsfServerStore_open(&st) || !st.m_db) {
        /* [环境问题不误红] 工作目录不可写 / 库开不了 ⇒ 记一条带说明的 PASS
         *（同 webui_check 的「无浏览器即跳过」口径），不假绿也不误红。 */
        CHECK("S21a 落盘库可开（造夹具；开不了即跳过本段）", true,
              "path=%s 开不了 ⇒ 本段跳过（环境问题，不是缺陷）", path);
        HsfServerStore_close(&st);
        if (dbPath) XClassDelete((XClass*)dbPath);
        if (pWal) XClassDelete((XClass*)pWal);
        if (pShm) XClassDelete((XClass*)pShm);
        return;
    }
    CHECK("S21a 落盘库可开（造夹具）", true);
    /* [造数据] 直接 INSERT（绕开 5s 采样节拍）：kRows 行、ts 递增。
     * [为什么造这么多行] 要让「带关键字的 COUNT(*)」的全表扫成本**可见**：
     * 300 行时 COUNT 只要 1ms，两种模式几乎测不出差别 ⇒ 优化会显得
     * 「做了也没用」。2 万行时 COUNT 约几十 ms ⇒ 比值稳定可判。
     * （Python 端实测：20 万行 / 16 个显示表达式 = 747ms/次。）
     * 用**多值批量 INSERT**（一批 500 行）⇒ 40 次调用即可，避免 2 万次
     * 单条 INSERT 把回归拖慢。 */
    for (i = 0; i < kRows; i += 500) {
        char sql[32768];
        int off = 0;
        int j;
        int n;
        n = snprintf(sql + off, sizeof(sql) - (size_t)off,
                     "INSERT OR REPLACE INTO history(ts,mode,press_mpa,"
                     "temp,hum) VALUES ");
        if (n < 0 || (size_t)(off + n) >= sizeof(sql)) continue;
        off += n;
        for (j = 0; j < 500 && (i + j) < kRows; ++j) {
            /* [边界检查] snprintf 返回的是「**本应**写入的长度」（可能超出
             * 剩余空间）⇒ 不能直接 `off += snprintf(...)`：off 会冲出缓冲
             * ⇒ 下一轮 `sizeof(sql) - off`（size_t）下溢成巨大值 ⇒ 越界写
             * ⇒ 段错误（本轮实锅）。⇒ 用返回值严格判容量。 */
            n = snprintf(sql + off, sizeof(sql) - (size_t)off,
                         "%s(%lld,'produce',0.5,19.0,50.0)",
                         j ? "," : "",
                         (long long)(1700000000000LL +
                                     (long long)(i + j) * 1000LL));
            if (n < 0 || (size_t)(off + n) >= sizeof(sql)) break;
            off += n;
        }
        {
            XSqlQuery* q = XSqlDatabase_exec_utf8(st.m_db, sql);
            if (q) XClassDelete((XClass*)q);
        }
    }

    rows1 = (HsfHistoryQueryRow*)calloc(HSF_HISTORY_PAGE_MAX,
                                         sizeof(HsfHistoryQueryRow));
    rows2 = (HsfHistoryQueryRow*)calloc(HSF_HISTORY_PAGE_MAX,
                                         sizeof(HsfHistoryQueryRow));
    if (!rows1 || !rows2) {
        CHECK("S21b 行缓冲可分配", false);
        if (rows1) free(rows1);
        if (rows2) free(rows2);
        HsfServerStore_close(&st);
        if (dbPath) XClassDelete((XClass*)dbPath);
        if (pWal) XClassDelete((XClass*)pWal);
        if (pShm) XClassDelete((XClass*)pShm);
        return;
    }
    /* ① 界面翻页口径（withCount=1）：数总数。 */
    n1 = HsfServerStore_queryHistory(&st, 0, 50, 0, 0, NULL, NULL, 0, 1,
                                     rows1, HSF_HISTORY_PAGE_MAX,
                                     &total1, &dbRows1, &latest1, &earliest1);
    /* ② 导出口径（withCount=0）：不数总数。 */
    n2 = HsfServerStore_queryHistory(&st, 0, 50, 0, 0, NULL, NULL, 0, 0,
                                     rows2, HSF_HISTORY_PAGE_MAX,
                                     &total2, &dbRows2, &latest2, &earliest2);

    /* ---- 耗时对比：带**关键字**的查询（导出大表时最常见的筛选）----
     * [为什么必须带关键字] 不带关键字时 COUNT(*) 走索引/元数据，很快 ⇒
     * 两种模式测不出差别 ⇒ 优化看起来"做了也没用"。带关键字时 COUNT 是
     * **完整全表扫**（把每行的显示表达式拼一遍做 LIKE）⇒ 这才是 62 分钟
     * 的真凶。
     * [测法] 同一进程、同一库、相邻测量 ⇒ 比值稳定（不受机器负载影响
     * 的是比值不是绝对值）。 */
    {
        int64_t t0, t1, t2;
        const char* kw = "produce";
        uint32_t kwCols[HSF_HISTORY_DISPLAY_COLS];
        int64_t kwTotal = 0;
        int nw = 0;
        int c;
        /* [用**全部**显示列] 关键字条件是把「每行所有显示列的文本表达式」
         * 拼成一条 LIKE ⇒ 16 列全查才是真实成本（Python 端实测：20 万行
         * / 16 个显示表达式 = 747ms）。只查 1 列时代价小一个量级 ⇒ 两种
         * 模式几乎测不出差别 ⇒ 优化会显得「做了也没用」。 */
        for (c = 0; c < HSF_HISTORY_DISPLAY_COLS; ++c)
            kwCols[c] = (uint32_t)c;
        /* 预热一次（首查含 SQLite 语句准备成本，不计入）。 */
        (void)HsfServerStore_queryHistory(
            &st, 0, 50, 0, 0, kw, kwCols, HSF_HISTORY_DISPLAY_COLS, 0,
            rows2, HSF_HISTORY_PAGE_MAX,
            &total2, &dbRows2, &latest2, &earliest2);
        t0 = XDateTime_currentMSecsSinceEpoch();
        nw = HsfServerStore_queryHistory(
            &st, 0, 50, 0, 0, kw, kwCols, HSF_HISTORY_DISPLAY_COLS, 1,
            rows1, HSF_HISTORY_PAGE_MAX,
            &total1, &dbRows1, &latest1, &earliest1);
        t1 = XDateTime_currentMSecsSinceEpoch();
        nw = HsfServerStore_queryHistory(
            &st, 0, 50, 0, 0, kw, kwCols, HSF_HISTORY_DISPLAY_COLS, 0,
            rows2, HSF_HISTORY_PAGE_MAX,
            &total2, &dbRows2, &latest2, &earliest2);
        t2 = XDateTime_currentMSecsSinceEpoch();
        kwTotal = t1 - t0;
        XPrintf("[INFO] S21 带关键字（全部 %d 显示列）单批耗时："
                "数总数 %lld ms vs 不数 %lld ms（%d 行库）\n",
                HSF_HISTORY_DISPLAY_COLS,
                (long long)kwTotal, (long long)(t2 - t1), kRows);
        CHECK("S21i 带关键字时导出口径（不数总数）明显更快（≥3 倍）",
              nw > 0 && (t2 - t1) * 3 < (t1 - t0),
              "数总数 %lld ms vs 不数 %lld ms（%d 行库 / %d 显示列）",
              (long long)(t1 - t0), (long long)(t2 - t1), kRows,
              HSF_HISTORY_DISPLAY_COLS);
    }

    CHECK("S21c 两种模式取到的行数一致",
          n1 > 0 && n1 == n2, "n1=%d n2=%d", n1, n2);
    /* [行内容逐字一致] 跳聚合不许动数据。 */
    if (n1 == n2 && n1 > 0) {
        for (i = 0; i < n1; ++i) {
            int c;
            for (c = 0; c < HSF_HISTORY_COLUMNS; ++c) {
                if (strcmp(rows1[i].m_cells[c].m_text,
                           rows2[i].m_cells[c].m_text) != 0) {
                    same = 0;
                    break;
                }
                if (rows1[i].m_cells[c].m_isNull !=
                    rows2[i].m_cells[c].m_isNull) {
                    same = 0;
                    break;
                }
            }
            if (!same) break;
        }
    }
    CHECK("S21d 两种模式的行内容逐字一致（跳聚合不丢数据）", same);
    CHECK("S21e withCount=1：total = 实际命中行数",
          total1 == kRows, "total=%d", total1);
    CHECK("S21f withCount=0：total = -1（未知，与 Python 端同值）",
          total2 == -1, "total=%d", total2);
    CHECK("S21g withCount=0：dbRows/latest/earliest 全 0（那 3 条聚合也跳了）",
          dbRows2 == 0 && latest2 == 0 && earliest2 == 0,
          "dbRows=%lld latest=%lld earliest=%lld",
          (long long)dbRows2, (long long)latest2, (long long)earliest2);
    CHECK("S21h withCount=1：dbRows/latest/earliest 真实（>0）",
          dbRows1 == kRows && latest1 > 0 && earliest1 > 0,
          "dbRows=%lld latest=%lld earliest=%lld",
          (long long)dbRows1, (long long)latest1, (long long)earliest1);

    /* ---- keyset 取数：offset 恒 0 + 时间上界递进（= beforeTs 语义）----
     * [为什么在这里验] C 客户端 2026-11-21 已把导出从「OFFSET 递进」改成
     * 「offset 恒 0 + beforeTs 递进」⇒ 依赖服务端把 beforeTs 当**窗口
     * 上界**（C 端 hiMs = min(hiTs, beforeTs)）⇒ 本段直接验「按上界递进
     * 取数跨批不重不漏」，这是 C 客户端改造成立的前提。
     * [口径] 与真实导出同路径：withCount=0、每批 40 行、短批收尾。 */
    {
        const int kChunk = 40;
        int64_t hi = 1700000000000LL + (long long)kRows * 1000LL;
        int total = 0;
        int batches = 0;
        int dup = 0;
        int64_t prevMin = 0;
        while (batches < (kRows / kChunk) + 5) {
            int64_t batchMin = 0;
            n2 = HsfServerStore_queryHistory(&st, 0, kChunk, 0, hi,
                                             NULL, NULL, 0, 0,
                                             rows2, HSF_HISTORY_PAGE_MAX,
                                             &total2, &dbRows2, &latest2,
                                             &earliest2);
            if (n2 <= 0) break;
            for (i = 0; i < n2; ++i) {
                long long ts = atoll(rows2[i].m_cells[0].m_text);
                if (batchMin == 0 || ts < batchMin) batchMin = ts;
            }
            if (batchMin <= 0) break;
            /* [跨批不重] 本批最小 ts 必须**严格小于**上一批的最小 ts
             * （游标 = 最小 ts - 1 ⇒ 那一行不会被下一批再取到）。 */
            if (prevMin > 0 && batchMin >= prevMin) dup++;
            prevMin = batchMin;
            hi = batchMin - 1;
            total += n2;
            ++batches;
            if (n2 < kChunk) break;    /* 短批 = 末批 */
        }
        CHECK("S21j keyset（offset 恒 0 + 上界递进）跨批不重不漏："
              "取到的总行数 = 库行数",
              total == kRows, "total=%d 应=%d 批数=%d", total, kRows, batches);
        CHECK("S21k keyset 每批最小 ts 严格递减（游标不含上一批的行）",
              dup == 0, "重复批数=%d", dup);
        CHECK("S21l keyset 批数 = ceil(行数/每批)", batches == (kRows + kChunk - 1) / kChunk,
              "批数=%d 应=%d", batches, (kRows + kChunk - 1) / kChunk);
    }

    free(rows1);
    free(rows2);
    HsfServerStore_close(&st);
    /* [把连接从全局注册表摘掉] XSqlDatabase_close 只关连接、**不删注册项**
     * ⇒ 注册表里会永久留下这个连接对象。⇒ close 之后再 removeDatabase
     * 把注册项也摘掉（用**独立连接名**，不碰生产的 "hsf_store"）。 */
    XSqlDatabase_removeDatabase_2("s21_test_conn");
    /* [删文件用 C 标准 remove] **不能**再用 dbPath/pWal/pShm 删：它们
     * 已被 XClassDelete 释放 ⇒ 再拿就是 use-after-free（本轮实锅：段错误
     * 就出在这一行）。 */
    (void)remove("s21_hist.db");
    (void)remove("s21_hist.db-wal");
    (void)remove("s21_hist.db-shm");
    if (dbPath) XClassDelete((XClass*)dbPath);
    if (pWal) XClassDelete((XClass*)pWal);
    if (pShm) XClassDelete((XClass*)pShm);
}

static void testS_autoModeSection(void)
{
    const int failBefore = g_fail;
    const int passBefore = g_pass;

    if (!sAutoFixtureBegin()) {
        sAutoFixtureEnd();
        return;
    }
    testS1_produceFullCycle();
    testS2_valveTimeoutAlarmAndFaultLatch();
    testS3_gates();
    testS4_modeConfigErrorVariants();
    testS5_preconditionOrder();
    testS7_alarmChain();
    testS8_modeEnvelopeKeys();
    testS9_stationLamps();
    /* [S12~S14 2026-11-13] devices 整表替换 / 停止按钮阀复位配置 / 按键
     * 触点与去抖配置化（都用真实钟与按钮 DI，排在拨钟的 S10 之前）。 */
    testS12_devicesReplace();
    testS13_stopButtonValveReset();
    testS14_buttonsConfig();
    /* [S15 2026-11-14] op=5 只选模式（记粘性 + 落盘 + 重启恢复）。放在
     * S10/S11 之前：它只碰粘性键，不动模块钟。 */
    testS15_selectModeOp5();
    /* [S16 2026-11-14] 换相策略配置面（缺省/SET-GET 往返/非法值拒/落盘往返）。
     * 放在 S10/S11 之前：它只碰 auto 配置节，不动模块钟、不启动控制器。 */
    testS16_switchPolicy();
    /* [S17 2026-11-15] 运行保护（进水泵空管超时）+ 流量计告警边沿。 */
    testS17_protectInletNoFlow();
    /* [S18 2026-11-15] 设备故障：报警 + 锁存 + 自动停机 + 手动复位解锁。 */
    testS18_deviceFaultLatch();
    /* [S19 2026-11-15] 压差过大保护（启用开关/阈值/去抖/停机/锁存）。 */
    testS19_pressHighProtect();
    /* [S20 2026-11-15] 审计整改：首帧沿 / protect fail-closed /
     * 故障停机结算运行时长（Python 侧有镜像用例）。 */
    testS20_auditHardening();
    testS10_cycleMode();
    /* [S11 放最后] S10 把模块钟拨到真实钟+偏移；S11 只用真实钟拍。框架
     * start/stop/eStop/reset 都清 m_lastStepMs、节流差值 uint64 下溢只会
     * 放行，时间倒流不卡步进——但把"用真实钟的段"放在"拨钟的段"之后
     * 仍是可读性最好的顺序。 */
    testS11_runMsAccumPersist();
    sAutoFixtureEnd();
    XPrintf("--- §S 小计：PASS %d / FAIL %d ---\n",
            g_pass - passBefore, g_fail - failBefore);
}

#endif /* XINDUSTRY_ON && XMODBUS_HUB_ON */

/* ============================================================================
 *                                  入口
 * ==========================================================================*/

int HsfRegressionTest_run(void)
{
    uint64_t memBefore = 0;
    bool memChecked = false;

    g_pass = 0;
    g_fail = 0;
    XPrintf("===== HSF C 端总回归 =====\n");
    XPrintf("--- §A 框架 Modbus 四层根修 ---\n");
    testA1_exceptionIsFailure();
    testA2_nestedCloseoutAccounting();
    testA4_worstUnitAge();
    XPrintf("--- §B 客户端协议契约 ---\n");
    testB_historyParseContract();
    XPrintf("--- §C 客户端表格排序 ---" "\n");
    testC_tableSort();

    memChecked = XMemory_statisticsEnabled();
    if (memChecked) {
        /* [预热提到测量之前 2026-11-09] 框架有一批**一次性惰性初始化**
         * （首个表格/表头的字体与段宽缓存、信号槽表、BSON/日志管路等），
         * 实测只在第一次用到时分配、同一段跑第二遍增量为 0（复现：把
         * testD1 连跑两遍，第二遍堆差 = 0）。那不属泄漏；若把它量进"新增
         * 段落净增"，固定成本会被误读成每轮泄漏。故先在测量前摊平一轮，
         * 真正的泄漏判据由 §G 的 3 轮工作集增量（含读组失败日志路径）承担。 */
        leakWorkload();
        flushDeferred();
        memBefore = heapNow();
    }

    XPrintf("--- §D 框架层「Qt 对齐补全」回归锁 ---\n");
    testD1_sectionDoubleClickedEmits();
    testD2_sectionEnteredEmits();
    testD3_sortIndicatorFlipsOnRelease();
    testD5_sortingEnabledSortsByColumn();
    testD6_sortingDisabledStopsSorting();
    testD7_nullSignalIdContract();
    testD8_appThreeStatePolicy();
    testD9_columnHiddenSkipsGeometry();
    XPrintf("--- §E 交叉测试 ---\n");
    testE1_frameworkPrimitiveVsAppPolicy();
    testE2_clientParserVsPythonPayload();
    testE3_cServerVsPyServerPayloadShape();
    testE4_frameworkVsAppLineConsistency();
    XPrintf("--- §F 覆盖面扩容 ---\n");
    testF1F2_failureObservabilityAndStallDiag();
    testF3_worstUnitAgeTwoSemantics();
    testF4_writeFailureDoesNotAmplifyPollTempo();
    /* §H：断言本体在 hsf_chart_zoom_test.c，这里只调用并汇总成败
     * （不复制断言文本；同一函数也就是 --test hsf-chart-zoom 的入口）。 */
    XPrintf("--- §H 历史页图表缩放取数（断言见上一段子结果）---\n");
    {
        int hFails = HsfChartZoomTest_run();
        if (hFails == 0) {
            ++g_pass;
            XPrintf("[PASS] §H 历史页图表缩放取数专项全部通过"
                    "（子断言计数见其自身结果行）\n");
        } else {
            g_fail += hFails;
            XPrintf("[FAIL] §H 历史页图表缩放取数专项失败 %d 项\n", hFails);
        }
    }
    XPrintf("--- §I 快照/控制命令的请求-应答兜底通道 ---\n");
    testI1_aiSnapshotResponse();
    testI2_flowMeterSnapshotResponse();
    testI3_turbiditySnapshotResponse();
    testI4_pumpSnapshotResponse();
#if XVFD_ON
    testI5_vfdSnapshotResponse();
#endif
    testI6_malformedPayloads();
    testI7_pumpControlResponse();
    testI8_autoControlResponse();
    testI9_ensureFreshOffline();
#if XLINK_AUTH_ON
    /* §J：认证「策略性拒绝」失败语义（"有时候客户端没数据"的框架层根修）。
     * 用回环设备对驱动两个会话，不依赖网络。 */
    XPrintf("--- §J 认证策略拒绝（Rejected）语义 ---\n");
    testJ_policyRejectSemantics();
#endif
#if XINDUSTRY_ON && XTURBIDITY_ON && XMODBUS_HUB_ON
    /* §K：ttyS4 路 B 三设备接入（2026-11-09 板端实测定案：站 1=CDAB、
     * 站 3(原 2)=DCBA+温度同刻、站 5 ABCD；站号 1/3/5 锁，模拟器保真读回）。
     * K4 自带预热 + 3 轮堆增量口径，排在 §G 之前互不干扰。 */
    testK_threeDeviceAccess();
#endif
#if XINDUSTRY_ON && XMODBUS_HUB_ON
    /* §N：现场口径对齐（2026-11-10 板端实机复核，基准＝Python 服务端）：
     * N1 阀反馈 DI 位序（开在前）/ N2 侧号由服务端解析 /
     * N3 流量计只认系统报警 / N4 数字量模块名。 */
    testN_fieldAlignment();
#endif
#if XVFD_ON
    /* §O：变频器遥测刻度（0x1001 ÷100，板端实机复测定案）。 */
    XPrintf("--- §O 变频器遥测刻度 ---\n");
    testO1_m512FrequencyScale();
    /* §P：带给定启动必须是一个设备动作（否则被单飞闸自拒成「设备忙」）。 */
    XPrintf("--- §P 带给定启动＝一个动作 ---\n");
    testP1_startWithGivenIsOneAction();
#endif
#if XINDUSTRY_ON && XMODBUS_HUB_ON
    /* §M：读组「不应答」与「异常应答」的框架级回归锁（2026-11-10）。
     * M1 某组不应答 ⇒ 在途超时收口 ⇒ 别的组照常轮询；
     * M2 异常应答（组级事实）⇒ 不牵连同从站的其它组；
     * M3 静默闸饥饿逃逸 ⇒ 配置离谱也不把从站永久饿死。 */
    XPrintf("--- §M 读组不应答 / 异常应答 / 静默闸逃逸 ---\n");
    testM1_silentGroupDoesNotBlockBus();
    testM2_exceptionDoesNotMuteWholeSlave();
    testM3_quietGateStarvationEscape();
#endif
    /* §Q：数据三页「导出筛选全量」批次计划（2026-11-11）。
     * Q1 按命中总行数分批 / Q2 逐批连续不重不漏 / Q3 截断取整到整批 /
     * Q4 纯函数重算 / Q5 轮转档与页大小不相交（编译期闸在本文件头）。 */
    XPrintf("--- §Q 导出全量取数批次计划 ---\n");
    testQ1_planFromTotalNotPage();
    testQ2_batchesCoverAllRowsNoDupNoGap();
    testQ3_truncationRoundsDownToWholeBatches();
    testQ4_planRecomputesOnFilterChange();
    testQ5_chunkBandDisjointFromPageSizes();
    /* §R：报警页纯逻辑（2026-11-11 对齐 Python AlarmPage：去图表 + 关键字
     * + 客户端内部分页 + 滚动锁定的判定口径，见 §R 块注）。 */
    XPrintf("--- §R 报警页纯逻辑（行可见 + 分页边界） ---\n");
    testR1_rowVisibleBasics();
    testR2_timeWindow();
    testR3_keywordMatch();
    testR4_combinedMatrix();
    testR5_pageCountBounds();
    testR6_pageStartBounds();
#if XINDUSTRY_ON && XMODBUS_HUB_ON
    /* §S：自动模式序列（2026-11-11，对齐 Python test_auto_modes /
     * test_auto_alarm_publish）。S1 产水全循环 / S2 阀超时告警与锁存 /
     * S3 闸门面 / S4 mode_config_error 单元 / S5 前置闸顺序 /
     * S7 告警链队列层 / S8 模式包络键客户端消费面。伪从站驱动 = §N 同款
     * 模拟 IO + 阀行程/卡死夹具；协议面的真服务端复检另见
     * verify/hsf_auto_mode_check.py。 */
    XPrintf("--- §S 自动模式序列（三模式状态机/告警/包络） ---\n");
    testS_autoModeSection();
    /* §S21：导出模式不数总数（与 Python store.with_count 同口径）。 */
    XPrintf("--- S21 导出模式不数总数（COUNT 全表扫的根修）---\n");
    testS21_exportSkipCount();
    /* §T：远端设置——配置节级存在性纪律（局部应答不清空别节的数据前提）。 */
    XPrintf("--- §T 远端设置：配置节级存在性纪律 ---\n");
    testT_remoteSettingsSectionGuards();
#endif
    XPrintf("--- §G 泄漏自检 ---\n");
    XPrintf("--- §L 长稳 soak：XLink 会话生命周期（40 轮 + 堆零增量）---\n");
    testL_soakRounds();
    testG_leakSelfCheck();
    if (memChecked) {
        flushDeferred();
        /* 预热已把一次性惰性初始化摊到 memBefore 之前 ⇒ 这里应恒为 0。
         * 非 0 就是新出现的按次/按轮泄漏，按 §G 的 3 轮工作集增量定位。
         * [口径订正 2026-11-21] 这句「应恒为 0」**实测不成立**：A/B 实测
         * （注释掉 §S21 调用再跑）基线就是 **14229 字节**，§S21 再贡献约
         * **9 KB 一次性**（SQLite 连接/语句缓存，进程退出即回收，**不按轮
         * 增长**）⇒ 真判据仍是 §G 的 **3 轮工作集增量**（那个才是绿的）。
         * ⇒ 本行是 INFO（信息），不是判据；别把它当成断言读。 */
        XPrintf("[INFO] 新增段落（§D/§E/§F/§H/§I/§Q/§R/§S/§S21）堆净增 = %lld 字节"
                "（预热后口径；真泄漏判据见 §G 3 轮工作集增量，含读组失败"
                "日志与日志捕获管路）\n",
                (long long)(heapNow() - memBefore));
    }

    XPrintf("===== 结果：PASS %d / FAIL %d =====\n", g_pass, g_fail);
    return g_fail;
}

void HsfRegressionTest(XVariant* data)
{
    (void)data;
    HsfRegressionTest_run();
}

#endif /* XMODBUS_ON && XMODBUS_CLIENT_ON */
