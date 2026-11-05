
/**
 * @file       HsfServerConfig.h
 * @brief      服务端部署常量（编译期宏；现场改这里）。
 * @details    [两路 485] 现场有两路相互独立的 RS485 总线，各自采集一侧：
 *               - 路 A（现役）：变频器（M512）+ 温湿度变送器 共用同一条总线，
 *                 走一个串口、一个 XModbusHub，靠从站号区分；
 *               - 路 B（预留）：两台浊度仪 + 一台流量计，串口/站号先占位，
 *                 **HSF_BUS_B_ENABLED=0 时不建立任何对象**（连串口都不打开），
 *                 插上第二个 USB-485 适配器后改端口名 + 置 1 即可启用。
 *             每路总线一个独立 XModbusHub ⇒ 两路的统一虚拟地址空间互不干扰
 *             （否则两路的输入寄存器 0x0000 段必然撞车）。
 *
 *             另有一路 Modbus-TCP 模拟量模块（8 路 4-20mA），与 485 无关，
 *             自己一个 Hub。
 *
 *             运行期可覆盖项（监听端口/遥测周期）走 HsfServerSettings（落盘）。
 */
#ifndef HSFSERVERCONFIG_H
#define HSFSERVERCONFIG_H

/* ==================== 模拟设备模式（全 C 端联调开关） ==================== */

/**
 * @brief 是否用**进程内模拟设备**替代全部真实从站（1=是，0=接真设备）。
 * @details [这个开关做什么] 置 1 后，服务端装配时把每个站的 Modbus 客户端
 *          换成 XModbusVirtualClient（进程内寄存器模型应答）：
 *            · 485 两路：变频器(站2) / 温湿度(站4) / 流量计(站5) 各绑一个模型；
 *            · 数字量站：.12 与 .13 各绑一个 16 点模型（合成 32 点）；
 *            · 模拟量站：8 路 4-20mA 模型。
 *          **上层代码一行不改**——Hub 的缓存块/读组合并/写重发、链路层的
 *          role 映射、设备层的状态字/故障表/单位解析、电磁阀的超时与离线
 *          判定，全部跑真实实现。变的只有"电信号从哪来"。
 *
 *          [为什么要这个开关] 设备拆走/没接线/CI 环境里也能把整条 C 链路跑
 *          通；`HsfServer --sim` 可在任意机器上起一个"有数据"的服务端，
 *          客户端（C 或 Python）据此联调，不需要任何硬件。
 *
 *          [不模拟什么] 传输层（帧格式/分片/超时/总线仲裁）。要测传输层
 *          请起真串口或 XModbusTcpServer（现有 xmodbus-hub 套件即如此）。
 *
 * @note 默认 0（接真设备）。命令行 `--sim` 可在运行期打开，无需重新编译。
 * @warning 模拟模式下**所有写操作都只写内存模型**，不会驱动真实继电器。
 *          这正是安全默认值：不可能误动现场设备。
 */
#ifndef HSF_SIM_MODE
#define HSF_SIM_MODE            0
#endif

/** @brief 模拟模式下的数字量点数（.12 + .13 各 16 点）。 */
#define HSF_SIM_DIG_CHANNELS_PER_DEV  16

/* ==================== 485 总线（两路独立，各自一套链路参数） ==================== */

/** @brief 总线路数（数组容量；不代表全部启用）。 */
#define HSF_BUS_COUNT           2

/* ---------------- 路 A（现役：变频器 + 温湿度共用同一条 485） ---------------- */

/** @brief 路 A 串口号（换口改这一行）。 */
#define HSF_BUS_A_PORT          "COM17"

/** @brief 路 A 是否启用（1=装配并轮询，0=整路跳过）。 */
#define HSF_BUS_A_ENABLED       1

/** @brief 路 A 波特率（bps；M512 面板 PD-00 与温湿度 000BH=7 均 115200）。 */
#define HSF_BUS_A_BAUDRATE      115200

/** @brief 路 A 单帧请求超时（毫秒；错误帧滞留靠重试自愈）。 */
#define HSF_BUS_A_TIMEOUT_MS    400

/** @brief 路 A 重试次数（≥1 才能把滞留的错误帧冲掉）。 */
#define HSF_BUS_A_RETRIES       1

/**
 * @brief 总线扫描节拍（毫秒 / 读组）——Hub 设备定时器的触发间隔。
 * @note  [两级节拍] 本值是**扫描周期**（每拍检查一次有没有到期的从站），
 *        各从站真正的刷新节拍由各自的 HSF_*_MIN_POLL_MS 决定
 *        （靠 XModbusHubUnitLimits.m_minPollIntervalMs 生效）。
 *        本值应小于等于最快要刷新的从站节拍。
 */
#define HSF_BUS_A_SCAN_MS       20

/* ---------------- 路 B（电磁流量计） ---------------- */

/**
 * @brief 路 B 是否启用。
 * @note  [2026-11-04 实锅] 这是**编译期宏**，改默认值会连带改掉所有从本树
 *        编出来的 HsfServer.exe —— 包括别的会话/别人正在跑的那个。当时把
 *        默认值改成 1 之后，另一个会话启动 6100 时也去开 COM18，串口只有
 *        一根，两边互相踢（打开错误码=3）。
 *        因此默认保持 **0（关闭）**；要用路 B 的部署在构建时覆盖：
 *          cmake -DHSF_BUS_B_ENABLED=1 ...   或本文件显式打开后再编。
 *        独立验证通道见 CMakeLists 的 HsfServerFlow 目标（同源码另出 exe）。
 */
#ifndef HSF_BUS_B_ENABLED
#define HSF_BUS_B_ENABLED       0
#endif

/** @brief 路 B 串口号。 */
#ifndef HSF_BUS_B_PORT
#define HSF_BUS_B_PORT          "COM18"
#endif

/** @brief 路 B 波特率（bps；电磁流量计实测 9600，参数 0x0002=5）。 */
#ifndef HSF_BUS_B_BAUDRATE
#define HSF_BUS_B_BAUDRATE      9600
#endif

/** @brief 路 B 单帧请求超时（毫秒）。 */
#define HSF_BUS_B_TIMEOUT_MS    500

/** @brief 路 B 重试次数。 */
#define HSF_BUS_B_RETRIES       1

/** @brief 路 B 扫描节拍（毫秒 / 读组；语义同 HSF_BUS_A_SCAN_MS）。 */
#define HSF_BUS_B_SCAN_MS       20

/* ---- 每路挂哪些站（1=挂，0=跳过） ---- */

/** @brief 路 A 是否挂变频器站。 */
#define HSF_BUS_A_HAS_VFD       1

/** @brief 路 A 是否挂温湿度站。 */
#define HSF_BUS_A_HAS_TEMPHUM   1

/** @brief 路 B 是否挂变频器站（路 B 只挂流量计）。 */
#define HSF_BUS_B_HAS_VFD       0

/** @brief 路 B 是否挂温湿度站（路 B 只挂流量计）。 */
#define HSF_BUS_B_HAS_TEMPHUM   0

/** @brief 路 B 是否挂电磁流量计站。 */
#define HSF_BUS_B_HAS_FLOWMETER 1

/** @brief 路 A 是否挂电磁流量计站（路 A 是变频器总线，不挂）。 */
#define HSF_BUS_A_HAS_FLOWMETER 0

/* ==================== 变频器（M512/513，Modbus-RTU 从站） ==================== */

/** @brief 变频器从站站号（面板 PD-02=2；两路同站号，靠串口区分）。 */
#define HSF_VFD_UNIT_ID         2

/**
 * @brief 变频器从站最小发送间隔（毫秒；分级轮询）。
 * @note  0 = 不限制（跟随总线节拍 HSF_BUS_x_POLL_MS），即变频器占满轮转。
 *        当前设 0：变频器是快设备，让它每拍都有机会发。
 */
#define HSF_VFD_MIN_POLL_MS     0

/** @brief 变频器动作监督超时（毫秒；28 组轮转实测需 8s 档）。 */
#define HSF_VFD_ACTION_MS       8000

/** @brief 变频器状态快照发布周期（毫秒）。 */
#define HSF_VFD_STATUS_MS       200

/* ==================== 温湿度变送器（中盛 ZS-SHT30-1T-1H，Modbus-RTU 从站） ==================== */

/**
 * @brief 温湿度从站站号（实测 4；寄存器 000AH=4）。
 * @note  取数只用 FC04 读输入寄存器 0000H × 2；块读上限 20、不支持 FC10、
 *        非法请求静默丢弃——约束见 docs/温湿度采集模块_探测与验证_20261104.md。
 */
#define HSF_TEMPHUM_UNIT_ID     4

/**
 * @brief 温湿度从站最小发送间隔（毫秒；分级轮询）。
 * @note  这才是温湿度的**真实节拍**：Hub 每拍扫描全部读组，但该从站的组
 *        在未到期时被跳过，所以变频器不必陪它轮转整个周期。
 *        温湿度物理更新率 30Hz/通道，500ms 采样绰绰有余。
 */
#define HSF_TEMPHUM_MIN_POLL_MS 500

/** @brief 温湿度陈旧判定阈值（毫秒；超过即判数据失效）。 */
#define HSF_TEMPHUM_STALE_MS    5000

/**
 * @brief 温湿度采样拍周期（毫秒）：定时从 Hub 缓存重取当前值。
 * @note  房间温湿度长期稳定，Hub 的 valueChanged 只在值变化时发——纯信号
 *        驱动会让「最近一拍」停在很久以前，超过 staleMs 就被判陈旧。
 *        本拍只刷新时刻，值未变则由组内静默吞掉。取 staleMs 的 1/5。
 */
#define HSF_TEMPHUM_SAMPLE_MS   1000

/**
 * @brief 温湿度寄存器（FC04 输入寄存器）在本路虚拟空间的起点。
 * @note  避开 0x0000：同总线的浊度仪/流量计将来也大概率走输入寄存器低段，
 *        留 0x0000~0x00FF 给它们；变频器走保持寄存器/线圈，与 IR 区不冲突。
 */
#define HSF_TEMPHUM_VADDR       0x0100

/* ==================== 路 B · 电磁流量计（上自仪 LD / L_mag B 系列） ==================== */

/**
 * @brief 流量计从站站号（**实测 5**，非默认 1；参数 0x0001 读回 = 5）。
 * @note  实测记录见 docs/LD电磁流量计_RS485通讯实测与使用指南_20261104.md。
 */
#define HSF_FLOWMETER_UNIT_ID   5

/**
 * @brief 流量计寄存器在路 B 虚拟空间的起点。
 * @note  该路只挂流量计一台，用低段即可；避开 0x0000 留给将来可能加的站。
 */
#define HSF_FLOWMETER_VADDR     0x0200

/**
 * @brief 流量计陈旧判定阈值（毫秒；超过即判数据失效）。
 * @note  管道静止时数值恒定，Hub「变化才发」；本站有采样定时器刷新
 *        设备层时刻，但**质量口径取 Hub 块级新鲜度**，故本值用于 Hub 判定。
 */
#define HSF_FLOWMETER_STALE_MS  5000

/**
 * @brief 流量计采样拍周期（毫秒）：定时从 Hub 缓存重取全部 22 个寄存器。
 * @note  语义同 HSF_TEMPHUM_SAMPLE_MS：静止量的「拍」靠它刷新，避免设备层
 *        因「太久没有新拍」被判陈旧。取 staleMs 的 1/5。
 */
#define HSF_FLOWMETER_SAMPLE_MS 1000

/**
 * @brief 流量计从站最小发送间隔（毫秒；分级轮询）。
 * @note  流量计是快设备（22 个寄存器一条帧，RTT 数毫秒），但工艺上流量
 *        不需要 20ms 级刷新；500ms 与温湿度同档，减少总线占用。
 */
#define HSF_FLOWMETER_MIN_POLL_MS 500

/* ==================== 路 B · 浊度仪 ×2（与流量计共用同一条 485） ==================== */

/**
 * @brief 浊度仪台数（两台，同一 485 口不同站号）。
 * @note  与 Python 侧 hsfsrv/proto.py 的 HSF_TURBIDITY_COUNT 同值。两台在
 *        工艺上分别测「原水」与「出水」，成对显示、成对刷新。
 */
#define HSF_TURBIDITY_COUNT     2

/**
 * @brief 浊度仪 1 站号（仪表"机号设置"菜单里设的那个值）。
 * @note  [为什么是 1/3 而不是 1/2] 现场两台仪表的机号是手工在面板上设的，
 *        Python 客户端页面提示写的就是「站号 1 / 站号 3」。本文件原先把
 *        第二台占位写成 2，与现场不符——站号错了表现为"一台在线、另一台
 *        一直离线"，很容易被误判成仪表坏。
 */
#define HSF_TURBIDITY1_UNIT_ID  1

/** @brief 浊度仪 2 站号。 */
#define HSF_TURBIDITY2_UNIT_ID  3

/**
 * @brief 浊度仪寄存器在路 B 虚拟空间的起点（第一台）。
 * @note  每台占 2 个输入寄存器（32 位量），故第二台起点 = 本值 + 2。
 *        路 B 现用段：0x0200~0x0215 流量计、0x0300 起浊度仪，互不重叠。
 */
#define HSF_TURBIDITY_VADDR     0x0300

/**
 * @brief 浊度仪寄存器起始地址（仪表侧，FC03 读 0x0000 ×2）。
 * @note  手册 4.3.10.2 的测试命令即 `01 03 00 00 00 02`。
 */
#define HSF_TURBIDITY_REMOTE_ADDR 0x0000u

/**
 * @brief 浊度仪寄存器数（32 位量 = 2 个）。
 * @note  与 Python 侧 devices.TurbidityMeter.READ_COUNT 同值。
 */
#define HSF_TURBIDITY_REG_COUNT   2

/**
 * @brief 浊度仪功能码：FC03（读保持寄存器）。
 * @note  [手册明确是 03 不是 04] 4.3.10.2 的测试命令是 `01 03 00 00 00 02`。
 *        这与温湿度（FC04）和流量计（FC04）都不同，写错会被从站拒绝或
 *        静默丢弃，表现为"仪表在线但读数一直是 0"。
 */
#define HSF_TURBIDITY_FUNCTION    3

/** @brief 浊度仪陈旧判定阈值（毫秒；超过即判数据失效）。 */
#define HSF_TURBIDITY_STALE_MS  5000

/**
 * @brief 浊度仪采样拍周期（毫秒）：定时从 Hub 缓存重取两个寄存器。
 * @note  语义同 HSF_TEMPHUM_SAMPLE_MS / HSF_FLOWMETER_SAMPLE_MS：静止量的
 *        「拍」靠它刷新，避免设备层因「太久没有新拍」被判陈旧。
 *        水样浊度是分钟级慢变量，1s 一拍绰绰有余。
 */
#define HSF_TURBIDITY_SAMPLE_MS 1000

/**
 * @brief 浊度仪从站最小发送间隔（毫秒；分级轮询）。
 * @note  与 Python 侧 turbidity_poll_ms 同值（500ms）。浊度是慢变量，
 *        500ms 与温湿度/流量计同档，减少总线占用。
 */
#define HSF_TURBIDITY_MIN_POLL_MS 500

/**
 * @brief 浊度仪遥测推送周期（毫秒）。
 * @note  与 Python 侧 TURBIDITY_PUSH_MS 同值。采集节拍 500ms，按 500ms 推
 *        刚好一拍一推；再快只是重复同一份缓存。
 */
#define HSF_TURBIDITY_PUSH_MS   500

/** @brief 路 B 是否挂浊度仪站（1=挂两台，0=跳过）。 */
#define HSF_BUS_B_HAS_TURBIDITY 1

/** @brief 路 A 是否挂浊度仪站（按现场接法，浊度仪在路 B）。 */
#define HSF_BUS_A_HAS_TURBIDITY 0


/* ==================== 模拟量输入模块（中盛 8 路 4-20mA，Modbus-TCP 从站） ==================== */

/** @brief 模拟量模块主机地址。 */
#define HSF_AI_HOST             "192.168.0.11"

/** @brief 模拟量模块 TCP 端口。 */
#define HSF_AI_TCP_PORT         8234

/** @brief 模拟量模块 Unit ID（实测 1）。 */
#define HSF_AI_UNIT_ID          1

/** @brief 模拟量轮询周期（毫秒；手册 30Hz/通道更新率，200ms 足够）。 */
#define HSF_AI_POLL_MS          200

/** @brief 模拟量通道数（8 路输入寄存器 0x0000~0x0007）。 */
#define HSF_AI_CHANNELS         8

/** @brief 电流换算采样电阻（Ω）：I(mA) = 寄存器毫伏值 / 249。 */
#define HSF_AI_SHUNT_OHM        249.0

/* ==================== 数字量模块（MT3A-IO1632，Modbus-TCP 从站） ==================== */

/**
 * @brief 数字量模块是否启用。
 * @note  [2026-11-05 决策] 本组参数**只写在头文件**，不进 INI 配置文件：
 *        它们是现场接线/部署的一部分（哪个 IP、几个点），跟设备一起版本化，
 *        不该让运行期随便改——改错 IP 会让整块 IO 静默失联。
 */
#define HSF_DIG_ENABLED         1

/** @brief 数字量模块主机地址（实测 192.168.0.12）。 */
#define HSF_DIG_HOST            "192.168.0.12"

/** @brief 数字量模块 TCP 端口（标准 Modbus TCP）。 */
#define HSF_DIG_TCP_PORT        502

/** @brief 数字量模块 Unit ID（实测 1；0/2/3/255 一律超时）。 */
#define HSF_DIG_UNIT_ID         1

/**
 * @brief 扩展模块地址（.13；与 .12 组成 32 点时挂在本地址上）。
 * @details [2026-11-05 32 点方案] 单台 MT3A-IO1632 物理只有 16 点，实测
 *          FC02/FC01 读 32 点一律回 EXC03（非法数据值）。现场 32 点靠
 *          **两台模块聚合**：.12 提供逻辑点 0..15、.13 提供逻辑点 16..31。
 *          故本模块按"探测 .13 是否在线"决定装 16 点还是 32 点：
 *            · .13 在线 ⇒ 两台各 16 点，合成为 32 点对外；
 *            · .13 不在线（拆机/断网/上电中）⇒ 只读 .12 自己的 16 点。
 *          降级是**自动**的，不需要改配置——现场拔掉一台不该让整块 IO 失联。
 */
#define HSF_DIG_HOST_EXT        "192.168.0.13"

/**
 * @brief 点数组大小（**单台模块** DI 与 DO 各多少路）。
 * @details 服务端是唯一事实源：客户端不写死点数，全部按推送的 count 显示。
 */
#define HSF_DIG_CHANNELS        16

/**
 * @brief 对外总点数上限（.12 + .13 聚合后的逻辑点数）。
 * @details 客户端/服务端共用的数组容量上限是 HSF_DIGITAL_CHANNELS(32)，
 *          本值必须 ≤ 它。.13 不在线时实际点数退回 HSF_DIG_CHANNELS。
 */
#define HSF_DIG_CHANNELS_MAX    32

/**
 * @brief 远端起始地址。
 * @note  [实测硬约束] 起始 ≥0x0010 或起始+点数跨出 16 点 ⇒ 模块一律回
 *        EXC02。故只能 0，且 HSF_DIG_CHANNELS ≤ 16。
 */
#define HSF_DIG_START_ADDR      0

/** @brief 轮询周期（毫秒；DI/DO 两区同拍）。 */
#define HSF_DIG_POLL_MS         200

/**
 * @brief 单帧超时（毫秒）。
 * @note  该模块应答实测 0.4~0.8s，比其它设备慢一个数量级；用缺省 1000ms
 *        会偶发假超时（超时后重发容易乱帧序）。
 */
#define HSF_DIG_TIMEOUT_MS      2000

/* ==================== 电磁阀组（6 路，每路 1 DO + 2 DI 反馈） ==================== */

/** @brief 电磁阀组是否启用（0=界面可见但控制被拒）。 */
#define HSF_VALVE_ENABLED       1

/**
 * @brief 动作超时（毫秒；超时未到位判故障 / 提示查接线）。
 * @note  [20s 的依据 — 与 Python 侧 cfg.valve_move_timeout_ms 同口径]
 *        现场实测一个阀满开→满关约 10s；20s 是两倍余量，既能容忍阀体
 *        老化/水压偏低导致的偏慢，又不至于让人等太久才看到告警。
 *        （原 8000ms 对 10s 行程根本不够，会**误报超时**。）
 */
#define HSF_VALVE_MOVE_TIMEOUT_MS  20000

/** @brief 阀路数（≤ HSF_VALVE_COUNT）。服务端下发，客户端不写死。 */
#define HSF_VALVE_ACTIVE_COUNT  6

/**
 * @brief 6 路阀的点位映射（DO 通道 / 开到位 DI / 关到位 DI）。
 * @details 返回 -1 = 该路未配置（界面显示"未配置"、控制被拒）。
 *          [为什么放头文件] 点位就是**现场接线**，与模块 IP 同性质：
 *          接线改了就重新部署，不该让运行期配置漂移。
 *
 *          [2026-11-05 对齐 Python 侧 server_py/cfg/hsf_server_py.json]
 *          六路全部落在**聚合上半区**（逻辑点 16..31，即扩展模块 .13 的
 *          16 个点映射进 .12 地址空间后的位置）：
 *
 *            阀1  DO16  开到位 DI17  关到位 DI16
 *            阀2  DO17  开到位 DI19  关到位 DI18
 *            阀3  DO18  开到位 DI21  关到位 DI20
 *            阀4  DO19  开到位 DI23  关到位 DI22
 *            阀5  DO20  开到位 DI25  关到位 DI24
 *            阀6  DO21  开到位 DI27  关到位 DI26
 *
 *          ⇒ 这六路**全部依赖扩展模块 .13 在线**。.13 掉线时 C 侧会明确
 *          报"反馈模块离线，无法控制"，而不是伪装成"阀卡住"或"无到位反馈"
 *          （见 HsfServerIoValve_poll 的按通道新鲜度判定）。
 *
 *          [与 Python 侧的差异] Python 配置定位到 module=0（.12）并直接给
 *          16..27 的通道号，前提是 .12 的固件聚合（esDoNu1/esDiNu1）已配好。
 *          C 侧同样按 .12 的聚合地址空间理解——两边口径一致，接线只改一处。
 */
#define HSF_VALVE_DO(i)         (((i) >= 0 && (i) < 6) ? (16 + (i)) : (-1))  /**< 第 i 路动作出口 DO 通道号。 */
#define HSF_VALVE_OPEN_DI(i)    (((i) >= 0 && (i) < 6) ? (17 + 2 * (i)) : (-1)) /**< 第 i 路开到位 DI 通道号。 */
#define HSF_VALVE_CLOSE_DI(i)   (((i) >= 0 && (i) < 6) ? (16 + 2 * (i)) : (-1)) /**< 第 i 路关到位 DI 通道号。 */

/* ==================== 通道名（**服务端是名字的唯一事实源**） ==================== */

/*
 * [设计] 通道名是**现场语义**（"进水阀反馈""1#泵运行"这类），只有服务端
 * 知道它跟物理接线的对应关系。故名字一律写在服务端宏里：
 *   · 客户端首次连上 ⇒ 拉一次全量快照，其中带全部通道名；
 *   · 之后周期推送只带状态/数值（不带名字，省带宽）；
 *   · 名字改了 ⇒ 服务端**主动**下发一个只含名字的帧，客户端就地更新。
 * 客户端因此**不知道也不该知道**有几个点、每个点叫什么、哪个口对应什么。
 *
 * 改名字只改这里；HSF_DIG_CHANNELS 个 DI + 同样多个 DO。
 */

/**
 * @brief DI 通道名表（长度 = HSF_DIG_CHANNELS_MAX；NULL 项按"输入<n+1>"缺省）。
 * @note  前 16 项属主模块(.12)，后 16 项属扩展模块(.13)。点位尚未现场定名，
 *        先给"输入1..32"的通用名；接线定名后改这里。
 *        **[必须写满 MAX 项]** 数组按 HSF_DIGITAL_CHANNELS 编译，少写的尾部
 *        会被填成 NULL（C 语义）——CH 号照常显示但永远没名字，且不报错。
 */
#define HSF_DIG_DI_NAMES   { \
    "输入1",  "输入2",  "输入3",  "输入4", \
    "输入5",  "输入6",  "输入7",  "输入8", \
    "输入9",  "输入10", "输入11", "输入12", \
    "输入13", "输入14", "输入15", "输入16", \
    "输入17", "输入18", "输入19", "输入20", \
    "输入21", "输入22", "输入23", "输入24", \
    "输入25", "输入26", "输入27", "输入28", \
    "输入29", "输入30", "输入31", "输入32" }

/**
 * @brief DO 通道名表（长度 = HSF_DIG_CHANNELS_MAX；NULL 项按"输出<n+1>"缺省）。
 */
#define HSF_DIG_DO_NAMES   { \
    "输出1",  "输出2",  "输出3",  "输出4", \
    "输出5",  "输出6",  "输出7",  "输出8", \
    "输出9",  "输出10", "输出11", "输出12", \
    "输出13", "输出14", "输出15", "输出16", \
    "输出17", "输出18", "输出19", "输出20", \
    "输出21", "输出22", "输出23", "输出24", \
    "输出25", "输出26", "输出27", "输出28", \
    "输出29", "输出30", "输出31", "输出32" }

/**
 * @brief 模拟量通道名表（长度 = HSF_AI_CHANNELS；NULL 项按"AI<n+1>"缺省）。
 */
#define HSF_AI_NAMES       { \
    "模拟量1", "模拟量2", "模拟量3", "模拟量4", \
    "模拟量5", "模拟量6", "模拟量7", "模拟量8" }

/* ==================== 遥测推送（XLink publish → 已订阅客户端） ==================== */

/**
 * @brief 遥测推送基准间隔（毫秒）。
 * @details [2026-11-04 提速] 基准节拍 200→100ms：客户端变频器页要求
 *          "数值连续变化"的观感，200ms 在 8 位小字下仍显迟滞；100ms 与
 *          变频器设备状态发布节拍（HSF_VFD_STATUS_MS）对齐。
 *          服务端唯一 tick 槽的周期：每拍向各主题 publish 一次，只有
 *          **订阅了该主题**的客户端会收到（XLinkServer_publish_ex 按订阅
 *          关系投递，无人订阅时静默返回 0，不视为错误）。
 *          改这里 = 改全部主题的推送频率；要按主题分频见下面两个宏。
 * @note   实际投递间隔还受总线采集节拍约束：数据没更新时推的是同一份
 *         快照（变频器侧额外要求 m_valid，未就绪该拍不推）。
 */
#define HSF_TELEMETRY_MS        100

/**
 * @brief 温湿度推送间隔（毫秒；0 = 跟随 HSF_TELEMETRY_MS）。
 * @details 温湿度是慢变量（采集节拍 HSF_TEMPHUM_MIN_POLL_MS=500ms），
 *          按 200ms 推会推四遍同一份值。设为 ≥ 采集节拍即可。
 */
#define HSF_TEMPHUM_PUSH_MS     500

/**
 * @brief 模拟量推送间隔（毫秒；0 = 跟随 HSF_TELEMETRY_MS）。
 * @details 采集节拍 HSF_AI_POLL_MS=200ms，按 200ms 推刚好每拍一份。
 */
#define HSF_AI_PUSH_MS          100

/**
 * @brief 变频器推送间隔（毫秒；0 = 跟随 HSF_TELEMETRY_MS）。
 * @details 设备状态发布节拍 HSF_VFD_STATUS_MS=200ms，与基准对齐。
 */
#define HSF_VFD_PUSH_MS         100

/**
 * @brief 流量计推送间隔（毫秒；0 = 跟随 HSF_TELEMETRY_MS）。
 * @details 采集节拍 HSF_FLOWMETER_MIN_POLL_MS=500ms；按 500ms 推刚好
 *          每拍一份，与工艺刷新需求匹配。
 */
#define HSF_FLOWMETER_PUSH_MS   500

#endif /* HSFSERVERCONFIG_H */
