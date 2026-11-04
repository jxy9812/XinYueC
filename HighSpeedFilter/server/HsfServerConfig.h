
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

/* ==================== 路 B · 预留站点（浊度仪 ×2） ==================== */

/** @brief 浊度仪 1 站号（预留）。 */
#define HSF_TURBIDITY1_UNIT_ID  1

/** @brief 浊度仪 2 站号（预留）。 */
#define HSF_TURBIDITY2_UNIT_ID  2

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
