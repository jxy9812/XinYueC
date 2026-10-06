# XGuiRemote —— XGuiServer / XGuiClient 远程窗口方案

> 版本: v1.0(设计冻结稿) · 日期: 2026-10-02
> 模块落点: `Src/XGui/Remote/` · 冻结契约: `XGuiRemoteProto.h` `XGuiRemoteCodec.h` `XGuiServer.h` `XGuiClient.h` `XGuiRemoteLoopback.h`
> 本文档所有"文件:行号"证据均在本仓库(分支 codex/xdevice-file-platform)逐条实读核实, 调研引用材料另注"〔调研〕"。

---

## 0. 需求 → 章节对照(逐条覆盖, 无裁剪)

| # | 需求 | 本文档对应章节 |
|---|------|----------------|
| 1 | 两个新控件/服务: XGuiServer 与 XGuiClient, 实现远程窗口 | §2 总体架构, §6 服务器设计, §7 客户端设计 |
| 2 | 连接方式不限定, 双方使用 XIODevice 交换数据(传输层抽象) | §3.1 传输无关设计, §4 传输与加密 |
| 3 | 画面增量刷新: 只传变化的矩形区域 | §3.6 FB_UPDATE, §6.3 伤害收集, §6.4 tile 编码 |
| 4 | 可选压缩算法 | §3.5 编解码协商, §5 编码与压缩 |
| 5 | 协议栈类(帧协议/会话层); SSL 为可选加密项之一 | §3 协议规范, §4.3 TLS |
| 6 | 性能 / 资源 / 低延迟三档预设 + 自动模式预留, 运行时可切换 | §5.2 档位参数表, §5.3 档位切换 |
| 7 | 服务器与现有 GUI 窗口共存镜像, 双向交互 | §6.2 共存模式, §6.6 输入注入, §7.4 输入转发 |
| 8 | 只开 XGuiServer 不开本地窗口(headless 虚拟帧缓冲) | §6.7 headless 模式 |
| 9 | XGuiClient 输入转发口径(控件原生捕获直接转发; 键盘焦点门控; 按下抓取; 无本地虚拟外设控件; 远端自绘光标/IME) | §7.4 输入转发(逐条落实) |

---

## 1. 背景与目标

XGui 已具备完整的软件渲染管线: 控件树递归合成进顶层 `XBackingStore` 软件双缓冲, 每帧经 `XPlatformBackingStore_flush` 提交, 且平台层提供 present 回调登记口(`XPlatformBackingStore_setPresentCallback`, `Src/XGui/Platform/XPlatformBackingStore.h:319`, 回调签名 `:81-89`)、公开的像素读取(`XPlatformBackingStore_paintDevice` `:118` / `XPlatformBackingStore_toImage` `:142`)与脏区集合(`XWidget` 顶层持有 `XRegion m_dirty`, `Src/XGui/Widget/XWidget.h:588`)。事件侧存在平台→GUI 的统一注入层 `XWindowSystemInterface_handle*`(`Src/XGui/Window/XWindowSystemInterface.h:259-436`, 同步自发投递)。基础设施侧 XTcpSocket/XTcpServer/XSslSocket(均 XIODevice 家族)、内嵌 zlib 与 mbedTLS、XThread/XMutex、每圈 poll 回调一应俱全。

本方案在此之上以**纯新增模块** `Src/XGui/Remote/` 实现远程窗口:

- **XGuiServer**: GUI 服务器。镜像本机既有窗口(共存模式), 或在无屏设备上以纯内存帧缓冲承载整套 GUI(headless 模式); 接受多客户端会话, 推送增量画面, 接收远端输入并注入本机事件系统。
- **XGuiClient**: XWidget 派生的远程视图控件。把远端画面合成进本地后备缓冲显示, 把本地用户的鼠标/键盘/IME 操作按需求 9 口径转发到远端; 支持断线重连。
- **XGuiRemoteProto**: 帧协议/会话层(长度前缀帧 + 消息编解码 + 握手状态数据), 会话跑在任意 `XIODevice*` 上——传输无关。
- **XGuiRemoteCodec**: tile 像素编解码(RAW/RLE/zlib)与像素格式转换。
- **XGuiRemoteLoopbackDevice**: 内存回环设备对(XIODevice 子类), 供单进程回归测试与嵌入式管道互联。

语言与风格: C 语言, 头文件 struct+函数 API, 中文注释, 命名跟随仓库既有惯例(下文逐处引用)。

---

## 2. 总体架构

```
        ┌────────────────────────── 本机(XGuiServer 侧) ──────────────────────────┐
        │                                                                          │
        │  控件树 ──PAINT──▶ XBackingStore(软件双缓冲) ──flush──▶ 平台提交驱动      │
        │                          │(XPlatformBackingStore)                        │
        │                          ▼ present 回调(GUI线程, 单槽)                   │
        │              ┌──────────────────────────────┐                            │
        │              │ XGuiServer (XObject, GUI线程) │                            │
        │              │  ┌───────────┐  ┌──────────┐ │   每会话编码线程            │
        │  远程输入 ──▶│  │ 会话#1     │  │ 会话#2    ││──▶ 认领脏tile→编码→有界队列 │
        │  (WSI 注入)  │  │影子FB+脏格 │  │  ...      ││◀───(互斥认领)               │
        │              │  └─────┬─────┘  └──────────┘ │                            │
        │              └────────┼─────────────────────┘                            │
        │                       ▼ GUI线程 poll 回调: 限预算写出                     │
        │              XIODevice*(XTcpSocket/TLS/回环/自定义)                       │
        └───────────────────────────────┬──────────────────────────────────────────┘
                                        │ TCP / TLS / 任意 XIODevice(§4)
        ┌───────────────────────────────▼──────────────────────────────────────────┐
        │        远端(XGuiClient 侧): XGuiClient(XWidget 派生, GUI线程)             │
        │   poll 回调收帧 ▶ 帧泵 ▶ 解码 tile ▶ 本地 backbuffer(XImage) 合成          │
        │        ▶ 损伤 XRegion ▶ XWidget_updateRegion ▶ 正常 PAINT 上屏            │
        │   vtable 输入槽(鼠标/滚轮/键/IME) ──捕获──▶ INPUT_* 消息 ──▶ 发往远端       │
        └───────────────────────────────────────────────────────────────────────────┘
```

模块依赖(冻结头 → 依赖):

```
XGuiRemoteProto.h ──▶ (仅 stdint/stdbool/stddef; XIODevice 为前置声明,      [A1]
                       依赖保持叶子化——传输抽象以指针穿透)
XGuiRemoteCodec.h ──▶ XGuiRemoteProto.h(线上枚举单一来源)                   [A2]
XGuiServer.h      ──▶ XObject.h(XCLASS 宏), Proto                          [B]
XGuiClient.h      ──▶ XWidget.h, Proto                                     [C]
XGuiRemoteLoopback.h ──▶ XIODevice.h(嵌首成员, 需完整定义), Proto           [A1]
```

---

## 3. 协议规范(XGuiRemoteProto, 版本 1)

### 3.1 传输无关性

协议栈只见 `XIODevice*`(`Src/XIO/XIODevice/XIODevice.h:51-60`, 公开读写 `XIODevice_read_1` `:193` / `XIODevice_write_1` `:267`, `XIODevice_bytesAvailable_base` `:413`)。TCP/TLS/回环/用户自定义管道一视同仁。**决策: 不新增"TCP 包装设备"子类**——`XTcpSocket`/`XTcpServer`/`XSslSocket` 本身就是 XIODevice 家族(XAbstractSocket 结构体首成员即 `XIODevice base`, `Src/XCode/XNetwork/XAbstractSocket/XAbstractSocket.h:160-161`), 协议层直接以 `XIODevice*` 持有它们。唯一新增的 XIODevice 子类是测试/互联用的 `XGuiRemoteLoopbackDevice`(§4.5)。

### 3.2 字节序与数值

- 线上一切多字节整数一律**小端(LE)**; 由 `XGuiRemoteProto_putU16/U32/U64` 与 `getU16/U32/U64` 显式序列化, **禁止结构体整块 cast 上网**。
- **画面 tile 坐标/尺寸**用 u16(单窗口上限 65535×65535, 恒在窗口内); **输入事件坐标用 i16**(有符号: 鼠标按下抓取期间拖拽越界, 坐标可为负或超出窗口, 远端裁剪), i16 越界饱和(±32767); 时间戳 u32 毫秒; 序号 u32 回绕。

### 3.3 横幅(banner)与帧格式

连接建立后, **双方各自**先发 8 字节横幅:

```
偏移 0..3:  魔数 'X','G','R','1'   (按 LE 读 u32 == 0x31524758; 校验口径
            冻结为 memcmp(bytes, "XGR1", 4) 字节串比较, 整数标注仅助记)
偏移 4..5:  u16 协议版本 = 1
偏移 6..7:  u16 保留 = 0
```

随后一切数据为**长度前缀帧**:

```
[u32 payloadLength (LE)][u8 msgType][payload...]
```

- payloadLength 不含这 5 字节头; 上限 `XGUI_REMOTE_MAX_FRAME_BYTES = 16 MiB`(超限即协议错误, 立即 BYE)。
- 读侧用增量帧泵 `XGuiRemoteFrameReader`(冻结头已声明, `XGuiRemoteFrameReader_init/_deinit/_feed`): 每圈把 `bytesAvailable` 内的字节喂进去, 凑满一帧返回该帧(type+payload 借用指针), 凑不满返回 0。帧缓冲由 reader 内部持有并按最大帧帽限量分配, 防内存放大攻击。

### 3.4 消息表(类型值冻结, 见 XGuiRemoteProto.h `XGuiRemoteMsgType`)

| 值 | 名称 | 方向 | 载荷要点 |
|----|------|------|----------|
| 1 | HELLO | C→S | u16 版本, u32 能力位, u8 建议认证法, u16+bytes 客户端名(≤64B UTF-8) |
| 2 | HELLO_ACK | S→C | u16 定版版本, u32 能力位, u8 选定认证法, u16+bytes 服务端名 |
| 3 | AUTH_CHALLENGE | S→C | u8 方法, u16+bytes nonce(32B) |
| 4 | AUTH_RESPONSE | C→S | u16+bytes 应答(32B) |
| 5 | AUTH_RESULT | S→C | u8 ok, u16+bytes 文本 |
| 6 | FB_META | S→C | u16 宽, u16 高, u8 线上格式, u16 tileW, u16 tileH, u8 档位 id, u16+bytes 窗口标题 |
| 7 | FB_REQUEST | C→S | u8 模式(0=续增量/视口通告, 1=全量刷新), u16 x/y/w/h(视口区域, 0=整幅/未通告; §3.6) |
| 8 | FB_UPDATE | S→C | u32 序号, u16 tile 数, u8 线上格式, u8 保留; 后随 N 个 tile 记录(§3.6) |
| 9 | FB_ACK | C→S(预留 V2 流控) | u32 确认序号, u32 接收窗口字节 |
| 10 | INPUT_KEY | C→S | u8 动作(1按下/2释放), u32 XKey 码位, u32 修饰掩码, u32 扫描码, u32 时间戳 |
| 11 | INPUT_POINTER | C→S | u8 动作(0移动/1按下/2释放/3双击), u8 键, u16 键集合, u32 修饰, i16 x, i16 y, u32 时间戳 |
| 12 | INPUT_WHEEL | C→S | i16 angleX, i16 angleY, i16 pixelX, i16 pixelY, u16 键集合, u32 修饰, i16 x, i16 y, u32 时间戳 |
| 13 | INPUT_TOUCH | C→S | u8 动作(0BEGIN/1UPDATE/2END/3CANCEL), u8 点数, u32 时间戳; 每点: u32 id, u8 状态, i16 x, i16 y, u8 压力Q8, u8 保留 |
| 14 | INPUT_IME | C→S | u16+bytes preedit(≤1024), u16+bytes commit(≤1024), i32 替换起点/长度, i32 光标/锚点 |
| 15 | PROFILE_SET | C→S | u8 档位 id(0xFF=自定义, 后随档位参数块) |
| 16 | PROFILE_RESULT | S→C | u8 是否接受, u8 生效档位 id |
| 17 | PING | 双向 | u64 发送时刻毫秒 |
| 18 | PONG | 双向 | u64 回显对端时刻 |
| 19 | BYE | 双向 | u8 原因(枚举), u16+bytes 文本 |
| 20 | ERROR | 双向 | u16 错误码, u16+bytes 文本 |

事件负载与仓库事件结构逐字段对齐: 修饰键 Shift=1/Ctrl=2/Alt=4/Meta=8/Keypad=0x10、鼠标键 Left=1/Right=2/Middle=4/Back=8/Forward=0x10(`Src/XCode/XEvent/XEvent.h:129-148`); 键值为 `XKey` 枚举/ASCII 码位原值(`XEvent.h:160-320`); 触摸点状态 Pressed/Updated/Stationary/Released 对齐 `Src/XGui/Window/XWindowEvent.h:798-801`, per-id 贯穿整条序列。

**未知枚举值处置(冻结)**: 封闭枚举字段——format/codec/action(PTR/KEY/TOUCH)/mode(FB_REQUEST)/method(认证)/profileId——出现协议外取值时 dec 系列返回失败, 会话按协议错误断链; 开放字段——flags/保留位/BYE reason/ERROR code——不校验, 未知值原样透传或归化为通用值, 为前向兼容留空间(配合帧层"未知名节跳过")。

### 3.5 能力协商与版本

- HELLO/HELLO_ACK 携带 u32 能力位: `CAP_ZLIB`(编译含 zlib 编解码)、`CAP_RGB565`、`CAP_TOUCH`、`CAP_IME`、`CAP_TLS`、`CAP_PROFILE_SET`、`CAP_FB_ACK`、`CAP_LATENCY`(bit8, latency 档协商)、`CAP_FB_REQUEST`(bit9, 2026-10-05 加法式: 客户端仅在能力交集含本位时才发 FB_REQUEST——老服务端永不会收到, 向后兼容不变; 合并裁决: 原 bit8 与 CAP_LATENCY 双占, 后应用优先, 迁 bit9)。双方能力按位与取交集。
- 版本协商: 取 `min(双方版本)`; 任一方低于自身可支持的最小版本 → 发 BYE(原因=版本不匹配)后断开。
- 认证法协商: 客户端在 HELLO 提建议值, 服务端在 HELLO_ACK 定值(可降级为 NONE)。

### 3.6 tile 记录与增量语义(FB_UPDATE)

```
每 tile: [u16 x][u16 y][u16 w][u16 h][u8 codec][u8 flags][u32 payloadBytes][payload]
```

- 像素为行主序、无行填充、按 FB_META 宣告的线上格式(ARGB32 或 RGB565); codec 见 §5.1。帧泵交付的 FB_UPDATE 负载已剥离 5 字节传输帧头, 负载内**首条 tile 的起始偏移 = `XGUI_REMOTE_FB_UPDATE_HEADER_BYTES` = 8**(帧级头 u32 sequence + u16 tileCount + u8 format + u8 flags)。
- **增量语义**: 服务器只对"脏 tile 网格中发生变化的 tile"出记录; tile 尺寸由 FB_META 宣告(档位决定, §5.2)。客户端解码后把 tile 矩形并入本地损伤区 → `XWidget_updateRegion` 增量重绘。全量刷新 = FB_REQUEST(mode=1) 触发的整幅 tile 序列。
- FB_UPDATE 序号单调递增(回绕允许); V1 不做确认重传(有损链路靠 TCP/TLS 自身可靠性), FB_ACK 为 V2 流控预留。

#### 3.6.1 FB_REQUEST 全量刷新与视口通告(2026-10-05 方案③④)

- **首帧全量(方案③)**: 客户端在本连接首个 FB_META 到达后, 能力交集含 `CAP_FB_REQUEST` 时自动发 FB_REQUEST(mode=1, 搭车视口字段)——根修「接入后静态画面无 tile/黑块」; **发过即止**(后续靠脏 tile 增量), 重连=新连接自然重发; 档位热切换的重发 FB_META 不再触发(服务端换档代际自含全量语义)。UDP 断流恢复路径的补救 FB_REQUEST 同受能力位门控。
- **[perf9 路2 根修 2026-10-05] 全量刷新的影子完备性**: ① FLUSH 限绘(RENDER_MODE=1 DIRECT/限绘 FAMILY)下服务端影子只按「flush 区域」累积, 从未(重)呈现的区域影子缺区——FB_REQUEST 全量刷新曾把缺区编成黑 tile 下发; 且首采集门控原以「PAINT 单槽去重」投递式 repaint 兜底, 小区域 PAINT 排队时整窗 update 被吞, 首采集=局部包围盒。修法: `xgs_enterStreaming` 与 FB_REQUEST(mode=1) 受限频通过后, 服务端在泵线程 `updateRect(整窗矩形, 含 CSD 标题栏条带)+repaint` **同步整窗重绘**, present 包装回调本调用栈内即采集整幅, 与排队事件解耦。
- **[perf9 路2 根修 2026-10-05] 换档影子转换被二次初始化抹黑**: 同尺寸换档 `xgs_sessionReallocShadowLocked` 的逐行转换分支产出新缓冲后, fall-through 的 `XImage_reinit_ex`(语义=换新分配+零化)把转换成果整体抹黑——换档全量刷新编码全零影子=客户端整幅黑, 静态屏不愈合(真机 performance↔resource 热切换黑屏根因)。修法: 转换成功置 convertedOk 跳过二次 reinit。配套: 编码线程认领门控新增 `metaPending`(FB_META 未发出前不认领, 堵「新格式 tile 先于 META 到达被客户端格式守卫整帧丢弃」窗口)。
- **[perf9 路2 任务4] 客户端未交付占位视觉**: backbuffer 重建底色深灰 `RGB(38,38,38)`(env `XGUI_REMOTE_TILE_PLACEHOLDER`, 缺省开, `=0` 回退纯黑)替代零值黑; 可选「正在接收画面…」提示(env `XGUI_REMOTE_CLIENT_HINT`, 缺省关, META 后首 tile 到达前绘)。换页同尺寸不重建 backbuffer=旧帧保持至新 tile 覆盖(双缓冲语义既有实现)。**[perf9 路2 扩 2026-10-05 晚]** FIT 信箱底色(上屏黑底铺色)同随占位门——真机 round1 定谳信箱黑边 ~48% 面积为「大面黑」观感主要构成, 占位开时信箱=深灰、关时=历史纯黑。设计稿 `out/perf9/placeholder-design.md`; 回归 `XGuiMirrorFidelity_Test`(fbdev 诊断构建)。
- **[perf9 路2 停摆根修 2026-10-05 晚] 全量标志吞没=静态屏永久停摆**: 编码线程三处分配失败路径(影子未就绪/线程缓冲 ensureBuf 失败/编码批 workBuf 失败)与换档 `xgs_pumpApplySwitch` 的 realloc 失败, 原实现都把已消费的 `allTilesDirty` 吞掉——performance 档线程缓冲 ~2.3MB+ARGB32 影子 ~1.9MB 在真机内存临界(MemAvailable ~3.9MB)下分配失败即「首帧怠速交付停摆」(真机 round1 定谳: performance×UDP/TCP 停在 80/475、0.01fps、XGC_FF full 四会话 0 完成)。修法: `full` 消费后在失败路径恢复标志+睡眠限速, 自愈重试。配套**状态序根修**: `xgs_enterStreaming` 先置 `state=STREAMING`+先发 FB_META 再同步整窗重绘——present 包装回调只对 STREAMING 会话采集, 原序(先 repaint 后置态)首采集恒不落地, 交付被押后到 FB_REQUEST; FB_REQUEST 不达即静态屏永无首帧。真机复核(ceb32daa): performance×UDP FB_REQUEST 全量 475/475 tile 361-376ms(两跑), 热切换 resource↔performance 会话存活、交互唤醒交付正常, 停摆绝迹。
- **服务端限频(设计稿一句话)**: mode=1 全量请求在服务端按会话 1s 限频——窗口内重复请求合并为一轮全量, 防节拍外请求风暴; 全量交付节拍另由 maxFps 认领门控压在 30fps 节拍内(perf8 第 4 轮, 原 15), 双层限速。
- **首帧根修配套**: 会话建影子缓冲时置的全量标志曾让首轮全量编码一份从未被采集过的影子(全零=黑块), 静态屏(fbdev 空闲不呈现)下即首帧停滞根因。现: 服务端在进入流送态时记 `needFirstCapture` 并强制宿主整幅重绘一次(present 包装回调即刻采集真实画面), 编码线程在首次真实采集落地前暂停认领——全量交付恒为真像素。
- **视口通告(方案④)**: FB_REQUEST(mode=0, (w,h)≠0) 为纯视口通告(无刷新动作, 老服务端对 mode=0 本就零操作, 线上兼容): (x,y,w,h) 为客户端可见裁剪窗(远端画面坐标, 1:1 模式由控件矩形逐级与祖先矩形求交得出, 250ms 节流变化才发); FIT 全可见→(0,0,0,0)=未通告。
- **tile 交付优先级(方案④)**: 服务端编码线程每轮认领后按「视口内 tile 先行 → 组内距上次交付变化量(伤害覆盖字节, 采集侧逐 rect∩tile 累计, 认领即清零)降序 → tile 序号稳定」重排发送次序。变化量键说明: 真像素差需留前帧副本(A33 内存不可承), 伤害覆盖字节为零内存代理且与「变化大先看见」目标单调一致。重排只影响本轮已认领 tile 的发送次序, 不改帧序号/拆帧预算/队列 FIFO——UDP 最新帧优先(seq 去重)语义不受影响。**[perf9 路2 根修 2026-10-05]** 重排实现为「阶排列」(order 排列遍历 槽/坐标/键三元组)——原实现就地排序 idx/chg/vp 三组元数据而 workBuf 槽保持认领序, 编码循环 槽 i 配坐标 i' **像素与坐标系统性错配**(乱序 mosaic, 真机 performance 档「白块碎片/叠影」直接来源; 离屏 XGuiMirrorFidelity_Test 回环实证, 关 XGUI_REMOTE_TILE_PRIORITY 即恢复)。旋钮: `XGUI_REMOTE_TILE_PRIORITY=0` 关闭(回 grid 扫描序); 探针 `XGUI_REMOTE_FIRSTFRAME_PROF=1`(S: XGS_FF 行 / C: XGC_FF 行, 交付前可关); A/B 旋钮 `XGUI_REMOTE_FB_REQUEST_OFF=1`、`XGUI_CLIENT_VIEW_MODE=1to1`(测量/专项诊断用)。

### 3.7 握手时序

```
C                                   S
│──── banner(8B) ───────────────────▶│   双方各自先发
│◀─────────────────── banner(8B) ────│   读侧校验魔数+版本
│──── HELLO(版本/能力/认证建议) ─────▶│
│◀──────────── HELLO_ACK(定版/定认证)│
│  [认证法 != NONE 时]
│◀──────────── AUTH_CHALLENGE(nonce)─│
│──── AUTH_RESPONSE(SHA256 应答) ───▶│
│◀──────────────── AUTH_RESULT ──────│   失败 → BYE(认证失败)
│◀──────────── FB_META(尺寸/格式/tile)│   目标画面元信息
│──── FB_REQUEST(mode=1 全量) ──────▶│
│◀════════ FB_UPDATE(整幅 tile 批) ══│   进入稳态:
│◀════════ FB_UPDATE(增量批, ≤maxFps)│   伤害驱动推送
│──── INPUT_KEY/POINTER/WHEEL/... ──▶│   远端输入注入本机(§6.6)
│    PING/PONG 保活(档位间隔)        │   超时判死链
│──── BYE ──────────────────────────▶│   优雅退出(双向)
```

保活: 档位给 pingInterval/pingTimeout(§5.2); 超时未收到对端任何帧即断开会话(客户端转入重连)。

### 3.8 认证

- `XGUI_REMOTE_AUTH_NONE`(0): 无认证, 仅用于可信网络。
- `XGUI_REMOTE_AUTH_SHA256_CHALLENGE`(1): 服务端只存口令的 SHA-256(经仓内 `XCryptographicHash` SHA-2 家族, `Src/XCode/.../XCryptographicHash.h:27-46`〔调研〕; nonce 取自 `XRandomGenerator`, `XRandomGenerator.h:43-71`〔调研〕)。握手: `response = SHA256(storedHash || nonce)`; 服务端同式重算比对(常量时间, `XGuiRemoteAuth.c`)。
- **访问口令单旋钮语义(2026-10-04 用户裁定, `XGuiRemoteAuth.h/.c` 实现主体)**:
  - 服务器**未设口令 = 匿名可连**(默认行为, 完全向后兼容);
  - 服务器**已设口令 = 此后接入的新会话必须通过挑战应答认证**; nonce 每连接随机(`XRandomGenerator_fillSecure`), 防重放; 口令明文禁止过网、禁止入日志; 服务端仅存口令 SHA-256, 原文不留存;
  - 错口令拒绝; 单连接失败达上限断链, 上限经 `XGuiServer_setAuthFailureLimit` 可配置(默认 1=错 1 次即断; N>1 时前 N-1 次错误回 `AUTH_RESULT(ok=0)` 可重答, 第 N 次断链);
  - 口令运行期经公开 C API 设置/清除: `XGuiServer_setAccessPassword`(设口令即启用挑战) / `XGuiServer_accessPassword`(掩码查询, 不明文回吐) / `XGuiServer_clearAccessPassword`(清除即回匿名); **对已有会话无影响**(设口令后存量会话不断, 新会话须认证);
  - 客户端对称 API: `XGuiClient_setAccessPassword` / `XGuiClient_clearAccessPassword`; 连接流程自动适配——服务端 HELLO_ACK 选定挑战才走认证, 选定 NONE 直连; 服务端要求认证而客户端无口令 → 快速失败断链(BYE/AUTH_FAILED), 不发送必错应答;
  - **认证与 TLS 正交**(开不开 TLS 口令语义不变; TLS 开时挑战仍走, 防应用层裸奔);
  - 遗留 API(`setAuthMethod`/`setPassword`)语义不变(方法+口令双旋钮, 含 §6.8 listen 拒绝契约)。
- **强度如实注记**: 该方案防口令明文过网与重放(nonce 单次), 但**不提供服务器身份验证、无信道绑定**, 暴力字典在哈希泄露时可行; 这是"无 TLS 环境下的紧凑方案"而非强安全。真实安全选项 = TLS 档(§4.3), 二者可叠加。

### 3.9 光标与 IME 的协议边界(需求 9 落实)

协议 **V1 无 CURSOR 消息**: 客户端不绘制外挂虚拟指针(本地 OS 光标即指针), 服务端也不把本机硬件光标合成进帧缓冲; "远端的光标绘制与 IME 由远端自行完成"按以下口径落地——客户端侧不重复实现任何指针/软键盘控件; IME 文本经 INPUT_IME 透明转发, 远端注入路径见 §6.6。FB_META 保留 flag 位供 V2 做远端光标形状同步, V1 恒 0。

---

## 4. 传输与加密

### 4.1 TCP(基线)

- 客户端: `XTcpSocket_create_ex`(`XTcpSocket.h:88`)+ `XAbstractSocket_connectToHost_base`(`XAbstractSocket.h:373`); 连接状态经 `XAbstractSocket_state`(`:228`)与信号 `connected/disconnected/errorOccurred`(`:524/:532/:550`)。
- 服务端: `XTcpServer_create_ex`(`XTcpServer.h:85`)+ `XTcpServer_listen`(`:108`)+ `newConnection` 信号(`:294`)+ `XTcpServer_nextPendingConnection_base`(`:220`)。
- 二者皆 XIODevice 家族(§3.1), 会话层以 `XIODevice*` 收编。

### 4.2 传输抽象与自定义链路

`XGuiServer_attachTransport` / `XGuiClient_setTransport` 接受**任意已连通 XIODevice\***(串口桥、管道、lwIP socket、回环设备……), 会话层零改动。协议不感知加密——加密与否完全由传入的设备决定。

### 4.3 TLS(可选编译项, 协议不感知)

- 编译开关 `XGUI_REMOTE_TLS_ON`(默认 1; 置 0 时 TLS 相关 API 整体从头文件剔除, 其余功能不受影响)。依赖已内嵌并全目标链接的 mbedTLS(`CMakeLists.txt:333-335,348-349`〔调研核实〕)。
- 客户端: `XSslSocket_create_ex`(`XSslSocket.h:86`)+ `connectToHostEncrypted` 三重载(`:199/206/216`), 仍是 XIODevice, 直接喂会话层。
- 服务端: `XTcpServer` accept 明文连接 → `XSslSocket_create_ex` + `XAbstractSocket_setSocketDescriptor_base`(`XAbstractSocket.h:437`)接管描述符 → `XSslSocket_startServerEncryption`(`XSslSocket.h:226-227`, 服务端模式 `:63`)做服务端握手。**此服务端 TLS 链路的三个 API 均已逐一实读核实存在。**

### 4.4 会话 I/O 线程归属(决策)

**设备的全部读写都发生在 GUI 线程**; 每圈事件循环经 `XAbstractEventDispatcher_addPollCallback`(`Src/XCode/XEvent/XAbstractEventDispatcher.h:309,318-319,325`)轮询 `XIODevice_bytesAvailable_base` 驱动读泵, 并按档位字节预算(txBudgetBytes)限流写出。理由:

1. 对任意 XIODevice(含无 OS fd 的回环设备、TLS 设备)统一成立——XSocketNotifier 只能挂 fd, 覆盖不了全部传输;
2. 该 poll 回调设施仓内已有两个长期用户(lwIP `XDeviceNetwork.c:355-358`、fbinput `XPlatformFbInput_posix.c:494`〔调研〕), 每圈开销为一次原子计数比较;
3. 单线程设备访问天然规避 `XAbstractSocket` 读写并发问题; 输入注入(必须 GUI 线程同步执行, §6.6)与读泵同线程, 免去跨线程投递。

编码这类重 CPU 工作移交每会话独立线程(§6.5), GUI 线程只做像素拷贝与编码结果限预算写出。

### 4.5 回环设备 XGuiRemoteLoopbackDevice(测试/互联)

成对创建的内存设备(`XGuiRemoteLoopbackDevice_createPair_ex`, 冻结头已声明): A 写入 B 可读, B 写入 A 可读; 各自带固定容量环形缓冲(容量创建时指定)。**全有全无写语义**: 对端接收环剩余空间不足以容纳整块拟写字节时 write 返回 0(一个字节都不写)——与 §6.4 的帧尾待写缓冲配合, 保证 writeFrame 整帧重试不乱流; 不阻塞、不丢数据、不动态扩容, 适配嵌入式无堆场景。**生命周期**: 共享对偶块(两环形缓冲+互斥锁+双端存活标志)由对偶引用计数持有, 任一端 delete 安全(原子清存活标志、释放引用, 末删者释放共享块), 对端此后 peerAlive()==false、读 EOF、写返回 0, 无悬垂; 每端的读写/delete 仍限本端属主线程。虚表扩展照 `XFileDevice` 模式(`Src/XCode/XFile/XFileDevice/XFileDevice.h:85-92` 头内虚表枚举, `.c` 内 `XVTABLE_INIT_DEFAULT`+`XVTABLE_INHERIT_XCLASS(XIODevice)`+覆写 ReadData/WriteData——见 `XFileDevice.c:418-443`〔调研〕; 结构体首成员嵌 XIODevice)。

---

## 5. 编码与压缩(XGuiRemoteCodec)

### 5.1 编解码器

| id | 名称 | 依赖 | 说明 |
|----|------|------|------|
| 0 | RAW | 无 | 不压缩, 直拷/转格式后原样传输(基线兜底) |
| 1 | RLE | 无(自实现) | **必选基线**, 像素对齐 PackBits(§5.4), 编解码各约百行, 零依赖 |
| 2 | ZLIB | 内嵌 zlib(已全目标链接, `CMakeLists.txt:324-326,359-361`〔调研核实〕) | `compress2` 级别 1..9; 编译开关 `XGUI_REMOTE_ZLIB_ON`(默认 1, 置 0 整体剔除) |

编码入参/出参、错误枚举、`XGuiRemoteCodec_hasCodec` 运行期查询、`XGuiRemoteCodec_maxEncodedSize` 缓冲预算均已在冻结头 `XGuiRemoteCodec.h` 声明。全部函数为纯函数(无全局状态), 编码线程可直接调用。

像素格式转换: 线上 ARGB32(内存序 B,G,R,A, 0xAARRGGBB) ↔ RGB565 快径自写; 其余组合委托 `XImage_convertToFormat`(`Src/XGui/Graphics/XImage.h:624`, 实现 `XImage.c:4188-4282`〔调研〕)。

tile 去重: `XGuiRemoteCodec_tileHash` = FNV-1a 32 位(编码线程自实现, 3 行, 零依赖); 编码前先比对上一轮同格 tile 哈希, 相同则跳过(伤害归并后的二次保险)。

**编码热循环 NEON 加速(perf9 路4, 2026-10-05)**: RLE 游标扫描热点(px_eq 等值比较/脏游标扫描/按行字面量冲刷)以 intrinsic 显式向量化(`XGuiRemoteCodecNeon.h`, 内部头)。armel 交叉构建(Linaro GCC 7.3.1, 默认 `-march=armv7-a -mfloat-abi=softfp -mfpu=vfp`)实测整 TU 零向量指令; `-mfpu=neon` 下 gcc7 自动向量化只命中 convertPixels 两循环, RLE 热点全部 "control flow in loop" 拒绝——故 intrinsic 显式批比(8/4 单元一批, `vceq`+掩码压缩+`ctz` 首异定位)。三条红线: ①输出逐字节等价——rle_bench NEON 版(verify/perf9, 实现逐字提取防漂移)96 组合成模式(bpp2/4×8 尺寸含 1×1/奇宽×6 内容模式)+1824 真机 tile **0 mismatch**(A33 真机执行自证, out/perf9/neon-bench.txt); ②未初始化安全——NEON 批按行内余量夹取只读行内字节, 尾块(<8/4 单元)/零长度/封顶退出全回标量单步; ③编译开关——CMake `XGUI_REMOTE_NEON`(ARM 交叉目标默认开, `-DXGUI_REMOTE_NEON=0` 关回)仅对 XGuiRemoteCodec.c 追加 `-mfpu=neon`, ABI 恒 softfp 不变, 桌面/无 NEON 产物整头裁空恒标量。A33 真机跑分(RGB565 32×32 真机帧): **vs round5 标量 1.51~1.65×**(page608 整页 18.6→11.3ms), vs round4 5.5~6.0×; 贪婪语义/封顶 129/字面量 ≤128 与 §5.4 冻结格式逐字节一致。

### 5.2 档位参数表(冻结于 `XGuiRemoteProfile`)

| 参数 | performance(性能模式) | resource(资源/嵌入式模式) | latency(低延迟模式, 2026-10-05 加法式) |
|------|----------------------|---------------------------|----------------------------------------|
| 线上像素格式 | ARGB32(4B/px) | RGB565(2B/px, 内存减半) | RGB565(2B/px) |
| 首选编码 | ZLIB level 1(快, 无则 RLE) | RLE(或低内存 zlib level 1) | **RAW 直拷**(免编码尖峰; 慢链路安全阀见下) |
| tile 尺寸 | 128×128(大批量少头开销) | 32×32(小缓冲、细粒度增量) | **64×60**(raw 记录 7692B ≤ UDP 数据报帽 8000B; 600 高=10 整行, 800/1024 双几何零行裁切) |
| 推送帧率上限 | 60 fps | **30 fps**([perf8 第 4 轮 2026-10-05] 15→30: 稳态端到端 P50 三轮钉死 114ms, 最大段=认领门控等待(15fps 平均 33ms/最坏 66ms); 30fps 压到 16.7/33ms, 预期稳态 P50 ~81ms。代价: 编码/发送频率翻倍——稳态小变化批 p50 ~1.2ms, 每秒增量 <2% 单核; 队列 256KB 突发丢批概率上升, 全量刷新兜底) | **60 fps** |
| 影子帧缓冲格式 | ARGB32 | RGB565(直接按 565 收帧, 再省一半) | RGB565(直收直拷) |
| 编码队列字节预算 | 2 MiB | 256 KiB | **2 MiB**(≈1.5 整页 raw 批水位, 上限非预分配, 反压即最新帧优先丢旧批) |
| GUI 每圈写出预算 | 256 KiB | 32 KiB | **128 KiB**(≈100M 链路 10ms/圈, 界住 GUI 线程写出发停顿) |
| 鼠标移动合并窗口 | 0 ms(直传) | 30 ms(移动事件合并, 按下/释放永不合并) | 0 ms(直传) |
| PING 间隔 / 超时 | 5 s / 15 s | 10 s / 30 s | 5 s / 15 s |

`XGuiRemoteProfile_initPerformance / _initResource / _initAuto / _initLatency` 四个预设填充函数 + `_sanitize` 夹取 + `_isValid` 校验, 全部冻结在 `XGuiRemoteProto.h`。

**latency 档混合回退(慢链路安全阀)**: env `XGUI_REMOTE_LATENCY_RLE_FALLBACK_PCT`
(未设=缺省 **40**[perf8 第 3 轮起, 2026-10-05]; 显式 0=关闭恒 RAW; 1..100=认领批脏
tile 占网格总数百分位达到阈值时该批改走 RLE——tile 记录自带 codec 字节, 客户端逐
tile 解码, 协议支持同帧混装)。缺省 40 的依据(真机 perf8 第 3 轮实测, 原"缺省关闭"
口径作废): 纯 RAW 页切突发 = ~150 个 8KB UDP 数据报背靠背, 真机链路收侧缓冲溢出
丢报 → 丢帧 tile 成洞, 只能靠 3s 静默兜底全量刷新愈合——整页交付 +42%(1774 vs
1250ms)、镜像碎片化、兜底循环反复冲高队列与 RSS(round2 OOM 33MB 同族)。脏占比
≥40%(页切/全量刷新)转 RLE 后线载 1.2MB→~100KB(ratio 0.056-0.081)且不成突发,
无损; 小/中脏占比(<40%, 交互常态)仍 RAW 直拷保低延迟。慢链路可再调高(60~80=
更早回退)。数据 out/perf8/round3/(镜像碎片样张 clt_mirror_nav.png)、设计稿
out/perf8/latency-profile-design.md。

**latency 档逐 tile 动态 raw/RLE(perf9 路5, 2026-10-05)**: env
`XGUI_REMOTE_LATENCY_TILE_PICK`(未设/1=缺省开; 0=回退恒 RAW[批回退同阈值时=现行为])。
批级回退**未触发**(批仍 RAW)时, 认领批内**每个 tile** 先 RLE 编码, 产物 ≥ raw 尺寸
(不可压内容)即该 tile 改发 RAW——逐 tile 取 min(RAW, RLE); 批级已转 RLE 的页切/全量批
维持原整批 RLE 语义(反 UDP 突发第一道闸不动)。记录级 codec 字节本就逐 tile(协议支持
同帧混装), 客户端逐 tile 解码, 全程 in-band——FB_META(tile 仍 64×60)/CAP_LATENCY
协商零改动, 老 peer 兼容。动机: 32×30 细网格已被 round2 A/B 否决(宽扁脏区补垫反增
线载 +20%), 而稳态小变化 1 个字 tile 也背 7.7KB raw 线载; 逐 tile 二选一恒 ≤ 任一
单策略线载。回环实测(480×320, 12 次换色整窗刺激, 批回退显式 0 隔离对照): 线载
2156KB(RAW 恒)→107KB(pick), **≈20×**; 编码代价 srv-enc p50 448µs→1515µs(≈+1.1ms/批,
A33 外推 +3~4ms/批, 换页批本就批级 RLE 无新增)。设计稿 out/perf9/latency-tile-design.md §3。

### 5.3 档位切换与自动模式

- 档位 = 纯参数结构 + 预设, **运行时可切换**: 服务端 `XGuiServer_setProfileId/_setProfile`(对既有会话在帧边界生效, 换档即广播 FB_META 新 tile/格式参数, 客户端 FB_REQUEST 全量跟随); 客户端经 PROFILE_SET 请求(受服务端 `setAllowClientProfile` 策略门控, 应答 PROFILE_RESULT)。
- 换档协议约束: 线上格式或 tile 尺寸变化必须伴随 FB_META 且此后 FB_UPDATE 按新参数编码; 客户端收到 FB_META 即重建本地缓冲。
- **向后兼容(latency 档, 2026-10-05)**: 档位 id 是 dec 层**封闭枚举**
  (XGuiRemoteTest 冻结断言 "档位 id 非法 dec 拒绝"), 老 peer 收到不识的档 id =
  协议错误断链——纯靠"未知档回退"不可行。故 latency 档的兼容机制是**能力位协商**:
  新增 `XGUI_REMOTE_CAP_LATENCY`(bit8), 双端各自在 HELLO/HELLO_ACK 宣告; 服务端
  对未宣告该位的会话**逐会话降级 resource 预设**(xgs_handleHello 会话夹取, 同
  RGB565/ZLIB 夹取先例; 运行期切档路径 xgs_applyServerProfile 同守), FB_META 宣告
  resource, 老客户端正常建流; PROFILE_SET(latency) 来自未宣告 peer 时拒绝并回显
  当前档(PROFILE_RESULT 可解析, 会话不断)。新客户端 FB_META 收到未知档 id(dec
  层未来放行时)回退 resource 预设(消费端兜底冻结)。取 id=4 而非 3: 冻结断言以
  0x03 作非法样本。
- **自动模式**: `XGuiRemoteProfileId_Auto=2` 仅预留——`_initAuto` 当前等价 resource 预设; `CAP_PROFILE_SET` 协商通过后 V2 可基于 RTT/队列水位在运行期自动调档。接口与协议位已冻结, 行为 V1 不实现(文档明示)。
- **编码帧率自适应降档(perf9 路4, 2026-10-05)**: 服务端编码线程认领门控上限取 `min(档位 maxFps, 阶梯值)`, 阶梯由纯函数状态机驱动(`XGuiRemoteAdapt.h`, 状态编码线程私有零锁)。触发= 单轮(认领→扫描→编码→入队)耗时超预算 **或** 队列高水位(>半容量/背压标志), 连续 2 轮即沿阶梯下行一步; 恢复= 连续 8 轮耗时低于半预算且距上次移动 ≥1s(非对称迟滞防抖动); 阶梯= 原速→半速→15fps 地板(由会话档位派生, 恒不高于档位——resource 15fps 档天然平阶梯零影响, 60fps 档即 60→30→15)。预算派生= 1000/当前阶梯 fps(60→16.7ms/30→33.3/15→66.7)。**换档并集**: PROFILE_SET/能力协商致档位变化即重置阶梯到步 0; 纯服务端认领节流、无线上语义变化, 老 peer 完全兼容。背景: performance 档(A33 单轮倾倒 29.21ms > 16.7ms 预算)交付崩塌 0.05fps——硬钉高帧率在慢端只积压+丢批, 主动降档把同一份编码时间摊进更长节拍, 交付反而连续。env: `XGUI_REMOTE_ADAPT_FPS=0` 关(恒档位门控, 历史行为); `XGUI_REMOTE_ADAPT_BUDGET_MS` 预算显式覆盖(缺省 0=派生); 探针 `XGUI_REMOTE_ADAPT_PROF=1`(阶梯移动单行 stderr: `XGS_ADAPT t= base= step= fps=a->b roundMs= qHigh= q=cap`)。回环量化(out/perf9/verify/adapt-quant-*.log): budget=1ms 强制超预算, 阶梯实测 60→30→15 下行(XGS_ADAPT 两行), 认领率 63.5/s→16.3/s、交付 45.1→12.8fps 非停摆; 单元 27 断言(阶梯映射/迟滞/换档重置, XGuiRemoteAdapt_Test 独立可执行, Xvfb 直跑口径)。

### 5.4 RLE 线上格式(冻结, 供互操作实现)

按像素单元(2B 或 4B)的 PackBits 流:

- 控制字节 `c < 0x80`: 字面量段, 后随 `(c+1)` 个单元原样字节(1..128 单元);
- 控制字节 `c >= 0x80`: 重复段, 后随 1 个单元, 重复 `(c-0x80+2)` 次(2..129 次);
- 编码器贪婪选择(≥3 单元重复用重复段, 否则字面量); 解码器遇截断/越界即报 `CorruptData`。

---

## 6. 服务器设计(XGuiServer)

### 6.1 对象形态

`XGuiServer` 是 **XObject 派生的服务对象**(非控件): GUI 线程构造, 信号 `clientConnected(sessionId)` / `clientDisconnected(sessionId, reason)` / `sessionError(sessionId, code)` / `fbMetaChanged()`。会话句柄为 int sessionId; 会话实现体(状态机、影子缓冲、编码线程)封装在 `XGuiServer.c` 内部(冻结头不暴露)。

### 6.2 共存模式: 镜像既有窗口(需求 7)

- `XGuiServer_host(XWidget* topLevel)`: 绑定任一**顶层控件**。取链: `XWidget_backingStore(top)`(`XWidget.h:1851`, 公开访问器, 已核实) → `XBackingStore_handle`(`Src/XGui/Graphics/XBackingStore.h:202`, 返回 `XPlatformBackingStore*`) → `XPlatformBackingStore_setPresentCallback(store, cb, userData)`(`XPlatformBackingStore.h:319`)。
- **单槽回调的共存策略(决策)**: present 回调是单槽(重复登记覆盖旧值, 头注 `XPlatformBackingStore.h:313-314`)。XGuiServer 登记**包装回调**: 先保存旧(回调, userData), 捕获完成后再转发旧回调——`unhost` 时原样恢复。这样与其他 present 消费者(显示驱动等)和平共处, 不要求独占。
- 回调收到 `(store, flushedRegion 窗口坐标脏区集合, offset)`(`XPlatformBackingStore.h:81-89`)。触发点是三个提交口——整帧 flush、tile 即时、tile 攒批——的统一收口 `xpbs_invokePresent`(`XPlatformBackingStore.c:451-463`〔调研〕), 且**限频跳帧不经过**(跳帧时区域并回 `top->m_dirty`, `XWidget.c:7352-7355`〔调研〕), 所以回调只在真实上屏帧触发。注意: 框架呈现限频闸默认 `XGUI_PRESENT_MAX_FPS=0` 即**不限帧**(XWidget.c:7310-7317 实读: 宏默认 0, 无 env 覆盖时 `throttleMinMs=0.0` 本闸短路), 不能作为本模块帧率上限的依据——maxFps 由本模块自行执行, 执行点见 §6.4。
- 像素读取在回调内(即 GUI 线程)经 `XPlatformBackingStore_paintDevice(store)`(`:118`)按 flushedRegion 矩形逐块 memcpy 进会话影子缓冲——回调返回后后备缓冲将翻转(`m_activeIndex ^= 1`, `XPlatformBackingStore.c:865-871`〔调研〕), 故**必须在回调内完成拷贝**, 这是设计红线(冻结头注释已写明)。
- 窗口标题/尺寸变化: 尺寸变化在 resize 后首个 flush 帧检测(影子缓冲尺寸 ≠ store 尺寸即重分配并发 FB_META; resize 帧天然带全量脏区——无脏区时 whole 回退整窗, `XWidget.c:7437-7440`〔调研〕); 标题用 `XWindow_title`(`XWindow.h:569`)在 FB_META/标题变更时携带。
- **双向性**: 远程输入 → §6.6 注入本机事件系统 → 本机控件响应重绘 → present 回调 → 增量推回远端。本地操作同样经 present 可见。闭环成立, 无需额外通道。

### 6.3 伤害收集与会话数据结构

每会话(GUI 线程写, 编码线程读, `XMutex` 保护, `Src/XCode/XSync/XMutex/XMutex.h:39-72`):

- **影子帧缓冲**: 会话自有 XImage 尺寸缓冲(档位格式, resource 档 RGB565 减半); 回调内把脏矩形转换后拷入。
- **脏 tile 网格**: 影子缓冲按 FB_META tile 尺寸划分的位图数组; 脏矩形覆盖到的 tile 置位并在存在时广播唤醒。
- **上一轮已编码 tile 哈希表**: 跳过内容未变的 tile。

采集循环(回调内): 对 flushedRegion 每矩形 → 裁剪进窗口 → 转格式拷贝入影子 → 标脏覆盖 tile → 置"有新伤害"原子标志(编码线程轮询/唤醒)。

### 6.4 tile 编码流水

**[perf9 路3 输入驱动认领门 2026-10-05]**: 门控升级为输入驱动——近 `XGUI_REMOTE_GATE_ACTIVE_MS`(缺省 500)毫秒内有远端输入注入则**豁免**立即认领(打掉门控等待主犯, 回环 srv-lat p50 30ms→0.11ms); 无输入自交互档位(30/60fps 联动, `XGUI_REMOTE_GATE_BOOST_FPS`)按 ×2 指数衰减回基础帧率, 静默期 CPU 不高于原静态门控; `XGUI_REMOTE_GATE_OFF=1` 一键回退, `XGUI_REMOTE_GATE_TRACE=1` 逐认领可观测。**[交付修复路 2026-10-06 溢出封顶]**: 衰减状态原样 ×2 无上界, GATE_TRACE 实证普通交互会话 156 次评估达 ≈2^62、再一次 ×2 即 int64 溢出(UB, 交互越密翻倍越快)——已在一处 `×2` 后钳回 baseMs, eff 输出逐位不变(本就 min(state, base)), 状态从此有界 ∈ [boostMs, baseMs]。同批: 编码线程内部按发送序劈两半双 worker 并行(`XGUI_REMOTE_ENC_WORKERS`, 缺省 2; 小批 `XGUI_REMOTE_ENC_SPLIT_MIN`=16 以下恒单核), 归并重放拆帧 fold 与单核**逐字节等价**(`XGUI_REMOTE_ENC_PARANOID=1` 每轮自比硬断言; 线载字节配对一致实证)。设计稿与数据: out/perf9/gate-design.md、out/perf9/gate/。

**[交付修复路 空闲 scratch 裁剪 2026-10-06]**: 编码线程兆级线程私有 scratch(workBuf/encBuf/encBufB/batchBuf/outA/B.stream, resource ~1MB / performance ~2.6MB)原为会话期常驻——测量定谳 performance×TCP 空闲内存谷值 5008kB 距历史临界 3908kB 仅 1.1MB。现 workBuf **按本轮脏 tile 数配额**(扫描前数脏位; 槽区发送序紧凑排布, 与满格配额逐位等价), 且距上次「大认领」耐久 `XGUI_REMOTE_BUF_TRIM_IDLE_MS`(缺省 5000, **0=禁用回退**)即在无伤害暂停分支免锁释放全部 scratch。tile 哈希去重态保留→零客户端可见变化; 涓流认领重配只花 KB 级。
**[第二轮死码修正 2026-10-06]**: 标准镜像会话实测(GATE_TRACE bufbusy 序列+buftrim=0+RSS 13s 恒平)定谳 FPS HUD(悬浮层 180×87px=6×3 tile)每 1.8s 恰产 18 tile 涓流 ≥ 初版阈值 16 → 大认领基准被永久刷新、裁剪成死码; 修正为**整页级认领口径**: 阈值 16→40(高于全部已知常驻涓流 HUD 18/秒时钟 6/滑杆点击 6-18, 对齐整页切换 35-475 下界), 且 env `XGUI_REMOTE_TRIM_ACTIVE_TILES`(缺省 40)可调。初版 +276KB 复原实证成立于 6 tile(<16)涓流上下文, 与本定谳不矛盾。

#### 编码线程每轮: 编码线程每轮: **maxFps 认领门控**(冻结执行点): 距上次认领不足 1000/maxFps 毫秒则本轮不认领——脏位保留、伤害零丢失仅延后, 资源档 30fps(perf8 第 4 轮, 原 15) / 性能档 60fps 上限由此落实(框架限频闸默认不限, 见 §6.2, 不可依赖); 门控通过后加锁 → 从影子缓冲**逐 tile 认领**(把脏 tile 像素拷进线程私有工作缓冲, 16~64KB 级 memcpy, 微秒级临界区)→ 清脏位 → 解锁; 然后无锁编码(tileHash 比对 → 转格式 → RLE/zlib)→ 编码结果推入**有界队列**(档位 encodeQueueBytes; 队列满则丢弃最旧整批并记 `sessionError`, 保证有界内存)。

**发送侧(GUI 线程 poll 回调, 帧尾待写缓冲状态机, 冻结)**: 每圈先按 txBudgetBytes 用 `XIODevice_write_1` 直写续传帧尾待写缓冲余量(非阻塞 fd 的短写是背压而非链路错误——`XAbstractSocket.c:631-635` WriteData 直写 fd 无缓冲, `XIODevice.c:283-303` write_1 原样透传, 均实读核实); 待写缓冲清空后从编码队列取整批组装 FB_UPDATE, 执行**拆帧纪律**: 单帧 tile 载荷合计不超过 txBudgetBytes(单个超过预算的 RAW 大 tile 独立成帧), 协议硬帽 `XGUI_REMOTE_MAX_FRAME_BYTES`(16MiB)只作解析侧防线, 发送侧不产生接近该帽的单帧; `XGuiRemoteProto_writeFrame` 返回已接受字节数(≥0, 可为部分写), 未写出余量存入帧尾待写缓冲留待下圈。待写缓冲存在期间队列水位照常反压编码线程(暂停认领)。回环传输侧由全有全无写语义(§4.5)保证整帧重试不乱流。多会话互不影响: 每会话独立影子缓冲、独立编码线程、独立队列与待写缓冲。

### 6.5 会话线程模型(总览)

```
GUI 线程                                会话编码线程(每会话 1 个, XThread)
────────                               ─────────────────────────────
present 回调: 拷脏矩形→影子, 标脏位  ──▶ 轮询脏位/唤醒
poll 回调: 帧泵读 XIODevice;           认领 tile(短临界区拷出)
  INPUT_* 消息 → WSI 注入(同步);        FNV 去重 → 转格式 → RLE/zlib
  编码队列 → 组 FB_UPDATE 限预算写出 ◀── 有界队列(encodeQueueBytes)
XTcpServer newConnection 信号建会话     纯计算, 不触设备
```

线程创建用 `XThread_create_func`(`Src/XCode/XSync/XThread/XThread.h:89`)+`XThread_start`(`:125`); 会话关闭时置停机标志后 `join` 语义收尾(经 XThread API)。

### 6.6 远程输入注入(需求 7 远→本)

INPUT_* 消息在 GUI 线程帧泵中解码后立即注入(全部为同步自发投递, 返回即处理完):

| 协议消息 | 注入 API(均在 `Src/XGui/Window/XWindowSystemInterface.h`, 已逐一实读) | 要点 |
|----------|----------------------------------------------------------------------|------|
| INPUT_POINTER | `handleMouseEvent_ex`(~`:342`) | 目标 XWindow = `XWidget_windowHandle(top)`(`XWidget.h:1125`); 坐标传**窗口客户区逻辑坐标**(客户端已按远端窗口局部坐标编码), globalPosition 传 NULL; **不补 DPR**(WSI 入口即逻辑坐标, `Drive/Posix/Graphics/XPlatformNativeWindow_posix.c:2670-2681`〔调研〕) |
| INPUT_KEY | `handleKeyEvent_ex`(~`:268`) | XKey 码位+修饰掩码原样透传; PRESS/RELEASE 成对由客户端保证 |
| INPUT_WHEEL | `handleWheelEvent`(~`:362`) | angleDelta ±120/格 透传 |
| INPUT_TOUCH | `handleTouchPoints_ex`(~`:426-436`) | per-id 稳定 + 四态 + 主点放 points[0]; 压力 Q8 还原为 float, 无压力传 1.0f |
| INPUT_IME | `handleInputMethodEvent`(~`:293-296`) | 直达焦点控件, 不替代 KEY_PRESS(头注原文) |

双击: 客户端显式发 action=3(DBL_CLICK), 服务端映射 `XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK`——双击不依赖服务端平台阈值识别。鼠标 PRESS/RELEASE 成对由客户端协议纪律保证(不配对会滞留 grabMouse, `Test/XGuiTest/XKeyboardTest.c:407-412`〔调研〕)。

多窗口目标: V1 会话镜像**单个顶层控件**; 服务端把输入全部注入该顶层, 命中分发由框架 childAt/父链机制完成(`XWidget.c:1889-1908`〔调研〕), 服务端不做控件级命中。多窗口同时镜像 = 多个 XGuiServer 实例(各自 host 不同顶层、不同端口), V2 可做单会话多窗口。

### 6.7 headless 模式(需求 8)

**决策: 不新增任何平台后端, 复用既有"无窗口系统"纯软件路径。** 依据(全部实读): `XPlatformNativeWindow` 的 Unsupported 存根 `isAvailable` 恒 false(`Drive/Unsupported/Graphics/XPlatformNativeWindow_unsupported.c:35`〔调研〕)→ XWindow 回落自增虚拟 WId 的纯软件行为〔调研〕; 后备存储主体在公共层(`XPlatformBackingStore.c`), software 后端（XPLATFORMBACKINGSTORE_SOFTWARE_ON=1 时的 `Drive/Unsupported/Graphics/XPlatformBackingStore_unsupported.c` 软件形态；原独立文件 XPlatformBackingStore_software.c 已并入该文件）钩子全 no-op 且其头注明确 present 回调即收帧点〔调研〕。因此:

- 无屏设备上: 不初始化真实显示后端, 应用照常创建控件树; 顶层控件的后备存储仍走公共层软件双缓冲, present 回调照常触发(Windows 无 DC 时也仅触发回调的语义, `XPlatformBackingStore.h:150-152` 头注明文), XGuiServer host 该顶层即得虚拟帧缓冲画面。
- "只开 XGuiServer 不开任何本地窗口": 应用创建的服务窗口本就不上屏(平台存根), 唯一消费者就是远程会话; 需要窗口不被本地窗口管理器感知时, 不调用平台 setVisible(存根上本为 no-op)。
- 与"有头透传"的关系: 有显示的设备上同一套代码即共存模式(§6.2), 无需切换开关——headless 与共存是**部署形态差异**, 不是代码分支。

### 6.8 认证与安全(服务端侧)

`setAuthMethod(NONE / SHA256_CHALLENGE)` + `setPassword`(内部即刻哈希, 原文不留存); TLS 档下服务端 accept 后经 XSslSocket_startServerEncryption 升级(§4.3)。策略: 方法=SHA256_CHALLENGE 但未设口令 → listen 时报错拒绝。

访问口令(§3.8 单旋钮语义, 2026-10-04 加法式扩展): `setAccessPassword` / `accessPassword`(掩码) / `clearAccessPassword` 运行期即时生效于**新会话**, 存量会话不断; 未设口令=匿名可连。会话认证形态经 `XGuiServer_sessionAuthState(server, sessionId)` 查询(1=已认证 / 0=匿名 / -1=会话不存在)。实现主体在 `Src/XGui/Remote/XGuiRemoteAuth.c`(server/client 只留挂接, 降低合并冲突面)。

---

## 7. 客户端设计(XGuiClient)

### 7.1 对象形态

`XGuiClient` 是 **XWidget 派生控件**(`XCLASS_DEFINE_EXTEND_END(XGuiClient, XWidget)`, 仿 `XSplitter.h:56-57` 惯例): `m_base` 首成员, 内部状态封装 `m_d` 私有块(冻结头不暴露)。内部会话(帧泵、传输、重连定时器)全在 GUI 线程。

### 7.2 画面合成

- 本地 backbuffer = `XImage`(线上格式解至本地, 尺寸=远端窗口), 由 `XImage_init_ex`(`XImage.h:105`)创建。
- FB_UPDATE 解码: 逐 tile `XGuiRemoteCodec_decodeTile` 直写 backbuffer(带 stride), tile 矩形并入损伤 `XRegion`(`Src/XData/XGeometry.h:255` 结构, `:497-584` API)→ `XWidget_updateRegion`(`XWidget.h:1832`)→ 走既有 PAINT 闭环增量上屏。
- `paintEvent` 虚槽内 `XPainter_drawImage`(`XPainter.h:831`, 最近邻采样)把 backbuffer 整幅/脏区绘出。
- FB_META(尺寸变化)→ 重建 backbuffer + `setFixedSize` 语义(控件采用远端尺寸; 可选 `ScaleFit` 档用最近邻缩放适配父容器, V1 默认 1:1)。

- **[perf9 路5 2026-10-05] 呈现三改**(全部 env 可回退, 缺省开):
  1. **首伤即现**: FB_UPDATE 批解码完成且当前无待上屏损伤(=突发/稳态首块)时立即就地 present, 不等泵圈合并窗——首现延迟从「静默 6ms/上限 20ms」(泵 8ms 粒度实测 8~11ms)砍到 µs 级; 突发后续帧仍由泵圈按窗合并。env `XGUI_REMOTE_PRESENT_IMMEDIATE=0` 回退。
  2. **合并窗自适应**: 最近转发输入(<500ms)判交互期, 窗取基值一半(下限 1ms); 静止期原基值。env `XGUI_REMOTE_PRESENT_QUIET_MS`/`XGUI_REMOTE_PRESENT_MAX_MS` 覆盖静止期基值(缺省 6/20), `XGUI_REMOTE_PRESENT_ACTIVE_MS` 调交互窗宽(0=关自适应)。
  3. **FIT 增量缩放缓存**: 缓存已缩视图帧(恒 ARGB32, RGB16 源按 XImage_expand5/6 位复制展开), present 时只对损伤矩形增量重缩放(±1px 护栏, 只多不少), paint 走 `XPainter_drawImage` 行级快车道 1:1 直绘——替代原「每 paint 整幅信箱逐像素逆映射」(painterRaster_drawImageRect 与损伤无关)。像素正确性: 离屏断言(env `XGUI_REMOTE_FIT_SCALE_SELFTEST=1`) 增量==全量==painter 全量路径逐字节(ARGB32/RGB16 双格式); 采样式与 painter 逐位同式。失效重建: FB_META 重建/变换变化/尺寸变化。回环实测(480×320 源, 830×700 视图): cli-blit p50 22.0ms→0.17ms; cli-merg p50 8.0ms→1µs。env `XGUI_REMOTE_FIT_CACHE=0` 整体回退旧全量路径(内存受限设备可关, 省 ~2.4MB 级缓存); 分配失败/非常规格式自动落旧路径。设计稿 out/perf9/latency-tile-design.md。

### 7.3 连接与重连

- `connectToHost(host, port)` / `connectToHostEncrypted(host, port, peerName)` / `setTransport(XIODevice*)`; 状态枚举 `XGuiRemoteSessionState`(Disconnected/BannerWait/Handshaking/Authenticating/Streaming)。
- 断线自动重连: `setAutoReconnect(true, intervalMs)` 经 XTimer(`Src/XTimer/XTimer.h:42,192-199`)指数退避(上限 30s); 重连成功自动 FB_REQUEST 全量刷新, 期间控件绘制"连接中断"占位文本, 不绘制陈旧帧(旧 backbuffer 作废)。
- 保活: 客户端按档位间隔发 PING, 超时未收到对端帧判死链 → 重连流程。

### 7.4 输入转发(需求 9 逐条落实, 决策冻结)

| 需求 9 口径 | 实现决策 |
|-------------|----------|
| 无本地外挂虚拟指针/软键盘控件 | XGuiClient 就是一个普通控件, 不派生、不创建任何指针/键盘子控件; 远端光标绘制与 IME UI 由远端负责(§3.9) |
| 鼠标操作(移动/按下/释放/滚轮)由控件直接捕获转发 | 重写 XWidget 虚槽 `MousePressEvent/MouseReleaseEvent/MouseDoubleClickEvent/MouseMoveEvent/WheelEvent`(槽位表 `XWidget.h:452-482`, 已核实)→ 组 INPUT_POINTER/INPUT_WHEEL 发送; 坐标用**控件局部坐标 = 事件 m_position**(`XEvent.h:414-430`), 1:1 模式下即远端窗口局部坐标, 零换算 |
| 指针位于控件区域内即转发 | 控件事件本就只在命中自身时到达(框架 childAt 命中分发); MouseMove 持续转发, 档位合并窗口内合并移动(resource 档 30ms), 按下/释放/双击/滚轮永不合并 |
| 按下期间抓取, 拖拽越界仍持续到释放 | MousePress 处理中调 `XWidget_grabMouse`(`XWidget.h:1447`, 已核实), MouseRelease 时释放; 抓取期间 MouseMove 越界事件仍派发到本控件(Qt 同语义), 转发坐标允许负值/超界: 按 **i16 有符号编码, ±32767 饱和**(§3.2), 远端注入前裁剪到窗口边界 |
| 首次点击自然夺焦, 与"焦点在该控件"一致 | 控件默认 `XWidgetFocusPolicy::StrongFocus`(经 `XWidget_setFocusPolicy` `:1345`); 首次点击由框架标准夺焦逻辑完成, 无特殊代码 |
| 键盘仅在该控件持有焦点时转发 | KeyPressEvent/KeyReleaseEvent 虚槽开头检查 `XWidget_hasFocus`(`:1347`); 未持有焦点直接不转发(控件无焦点时框架本就不投递按键, 双保险) |
| 文本/IME | 控件声明 IME 提示启用本地输入法, `InputMethodEvent` 槽把 preedit/commit 原文组 INPUT_IME 转发; 不在本地实现任何输入法逻辑 |

按键纪律: PRESS/RELEASE 永远成对转发(按下时置按下键集合, 释放时清除; 断链时清空集合并向远端补发 RELEASE, 防服务端 grabMouse/按键滞留)。自动重复(autoRepeat)原样转发 m_autoRepeat 对应的重复 PRESS(服务端 handleKeyEvent_ex 有 autoRepeat 参数)。

### 7.5 统计

`XGuiClient_statistics` 输出 `XGuiRemoteStats{ u64 bytesSent/bytesReceived; u32 updateCount/tileCount; u32 fpsMilli(帧率×1000); u32 rttMs(PING 往返) }`。

---

## 8. 构建接入(计划, 本次不改 CMakeLists)

- 新源文件落 `Src/XGui/Remote/*.c`, 会被 `file(GLOB_RECURSE SRC_FILE "Src/*.c")`(`CMakeLists.txt:111`)收编, **但 glob 在配置期固化**——既有构建树不重配时感知不到新文件(拼音引擎 LNK2019 同款教训, `:125-129` 注释), 故按惯例在 `:124-144` 的显式 APPEND 区块**之后、`list(REMOVE_DUPLICATES SRC_FILE)` 之前**追加新 `list(APPEND SRC_FILE ...)` 块登记 5 个 .c(XGuiRemoteProto/Codec/Server/Client/Loopback)。
- 头文件目录经 `FIND_INCLUDE_DIR(INCLUDE_SRC_LIST "Src")` 自动纳入, 无需加 include 路径。
- zlib/mbedtls 已 `add_subdirectory` 并链接全部目标(`:324-335,348-361`), **不改链接行**。
- 编译开关不改 `XGuiConfig.h`(该文件归既有战役所有): `XGUI_REMOTE_ON / XGUI_REMOTE_ZLIB_ON / XGUI_REMOTE_TLS_ON` 以 `#ifndef` 形式定义在 `XGuiRemoteProto.h` 文件头——先例: `XGUI_BACKINGSTORE_TILE_BATCHING_ON` 即定义在 `XPlatformBackingStore.h:37-51`(头注原文: "配置头必须保持叶子且不归本模块所有权")。
- 测试接入: ① 回归套件 `XGuiRegression_Test` 显式文件清单(`:483-498`)追加 `Test/XGuiTest/XGuiRemoteTest.c`, 并在 `xgui_regression_test.c` 挂 `#include "XGuiRemoteTest.h"`(锚点 :100 邻域)与 `expect_true(XGuiRemoteTest_runAll(), ...)`(锚点 :34712 邻域); ② demo 新建 `Test/XGuiDemo/xgui_demo_remote.c`(自带 main, 单文件双角色 `--server/--client`), 新增可执行目标 `XGuiRemoteDemo_Test`(注册锚点: `XGuiWindowDemo_Test` 目标块 `:559-575` 之后)。
- 具体锚点与编辑纪律(只追加不修改既有行)在泳道 D 任务书中逐条给出; **锚点以"行内容模式"定位而非行号**, 执行前必须在落地后的最新树上重新实读全部锚点(禁照抄本文档行号——CMakeLists.txt 与 xgui_regression_test.c 均处于其他战役未提交修改状态, 并行落盘会使行号漂移); 这两个文件与其他占用战役**串行交接**, 交接窗口未到前 D 先交付测试/演示新文件与 CMake 变更清单, 不动既有文件。

---

## 9. 测试与验收方案

1. **单元(协议)**: 消息编解码回环(每消息 enc→dec 逐字段相等)、帧泵跨包/粘包/超长帧拒绝、banner 校验、RLE 规范向量(冻结格式 §5.4)、zlib 编解码、ARGB32↔RGB565 转换无损性(不透明像素往返位相等)。归入 `XGuiRemoteTest_runAll`。
2. **单元(编码管线)**: 用 `XGuiRemoteLoopbackDevice` 对跑完整握手→全量→增量→输入转发→BYE 全流程(单进程、零网络); 伤害归并正确性(重叠脏矩形 tile 去重); 有界队列水位与丢批计数。
3. **控件级**: XGuiClient 虚拟窗口下离屏渲染断言(backbuffer 像素抽样 = 服务端源像素), 输入转发断言(注入点击→远端控件状态变化), 焦点门控(焦点在旁控件时不转发), 抓取越界(按下后移出控件仍收到 Move)。
4. **回归门禁**: `XGuiRegression_Test` 挂入 runAll; ctest `XGuiRegression`(`:524-532`)保持绿。
5. **端到端演示**: `xgui_demo_remote --server`(起 GUI + XGuiServer)与 `--client`(同机另进程连 127.0.0.1)互操作; 人工验收: 共存镜像双向操作、两档画质切换、断网重连、TLS 档握手。
6. **验收口径**: 构建命令、日志落盘路径(禁长输出直冲流)写入泳道 D 任务书; 门禁 = 全量 `cmake --build` + `ctest -R XGuiRegression` 通过。

---

## 10. 分期与风险

**分期**

- **P1(骨架可通)**: A1 协议+回环设备、A2 编解码 → B 服务器(全量帧推送+输入注入)、C 客户端(显示+输入转发) → D 集成。验收: 回环设备端到端。
- **P2(增量与档位)**: 伤害收集挂点接线、tile 增量、两档参数、运行时切档、断线重连。验收: demo 双进程增量刷新。
- **P3(安全与打磨)**: TLS 两端、SHA256 挑战应答、统计、资源档内存优化; FB_ACK 流控预留位不动。

**风险与缓解**

| 风险 | 缓解 |
|------|------|
| present 回调单槽被其他消费者占用 | 包装转发旧回调(§6.2); 若未来平台层升级为多槽, 包装层无损移除 |
| 编码线程与 GUI 线程数据竞争 | 影子缓冲+脏位图仅两处访问, 互斥锁粒度=单 tile 拷贝; 设备读写单线程化(§4.4) |
| 高帧率下 GUI 线程写出阻塞 | txBudgetBytes 每圈预算 + 帧尾待写缓冲续传(§6.4) + 队列水位反压编码线程, GUI 每圈最多预算字节 |
| 低带宽链路 FB_UPDATE 巨帧 | 发送侧拆帧纪律: 单帧 tile 载荷 ≤ txBudgetBytes(§6.4), 16MiB 硬帽仅作解析侧防线; 慢客户端短写=背压入待写缓冲, 不误判断链; FB_ACK 流控为 V2 预留 |
| 编码产物在慢链路积压 | 有界队列满丢弃最旧整批并记 sessionError; maxFps 认领门控从源头限流(§6.4) |
| 头文件冻结后字段演进 | 消息结构仅含定长字段与内联缓冲, 版本协商保留前向兼容空间; 未知名节一律跳过(framing 层保证) |
| RGB16 编译变体(`XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16`, `XGuiConfig.h:250-251`〔调研〕)下源格式非 ARGB32 | 采集链不假设源格式: 用 `XPlatformBackingStore_surfaceFormat`(`XPlatformBackingStore.h:356`)取运行期真值, 转换矩阵覆盖 ARGB32/RGB565 互转 |
| TLS 服务端链路(API 已核实存在)未经端到端实测 | P3 首项即 demo TLS 档实测; 失败回退 = 仅客户端 TLS 档先发布, 服务端 TLS 档标注实验 |

---

## 11. 待用户定夺(openQuestions)

- 无阻塞性开放问题。以下两点为非阻塞默认值, 可随时改: ① 默认监听端口暂定 46000(demo 用), 正式部署由调用方指定; ② 自动模式 V1 等价 resource 预设, V2 自适应策略(基于 RTT/队列水位)待真实负载画像后再定。

---

## 12. 评审意见处置(独立评审 10 条, 全部采纳, 无不成立项)

| # | 意见(摘要) | 严重度 | 处置 | 落点 |
|---|-----------|--------|------|------|
| 1 | 写出链路缺部分写/背压状态机, writeFrame"短写=false"契约在非阻塞 fd 上不可实施 | high | **采纳建议①**: writeFrame 契约改为"返回已接受字节数(≥0, 可部分写; -1=硬错误)"; 会话层冻结"帧尾待写缓冲"状态机——余量入待写缓冲, poll 回调按 txBudgetBytes 每圈 `XIODevice_write_1` 续传, 清空前不组新帧; 发送侧拆帧纪律(单帧 tile 载荷 ≤ txBudgetBytes)使 16MiB 硬帽退为解析侧防线; 回环设备补"全有全无写"(对端环空间不足时一个字节都不写返回 0), 整帧重试不乱流 | XGuiRemoteProto.h writeFrame 契约; XGuiRemoteLoopback.h 语义; 本文档 §3.3/§4.5/§6.4/§10 |
| 2 | decFbTile 注释"首条传 5+9"错误(帧泵已剥 5 字节帧头; 帧级头实为 8 字节) | medium | **采纳**: 删除"5+9"表述, 注释改为"首条传 XGUI_REMOTE_FB_UPDATE_HEADER_BYTES"; 新增常量 `XGUI_REMOTE_FB_UPDATE_HEADER_BYTES 8` 作单一来源(u32+u16+u8+u8=8), encFbUpdate 注释同步"恒返回 8" | XGuiRemoteProto.h 常量+encFbUpdate/decFbTile 注释; 本文档 §3.6 |
| 3 | maxFps 无执行机制, §6.2"框架限频闸先行约束"表述失实(闸默认 0=不限, 无 env 即短路) | medium | **采纳**: §6.2 失实表述改为如实引用实读证据(XWidget.c:7310-7317 宏默认 0、throttleMinMs=0.0 短路); maxFps 执行点冻结为**编码线程认领门控**(距上次认领 < 1000/maxFps 则本轮不认领, 脏位保留零丢失仅延后——不用"丢批"方案, 因丢弃已编码批会使客户端永久缺帧) | 本文档 §6.2/§6.4 |
| 4 | 横幅魔数 LE 整数值算错(0x58524731 → 实为 0x31524758), 互操作陷阱 | medium | **采纳**: 文档改为 0x31524758 并冻结实现口径为 `memcmp(bytes, "XGR1", 4)` 字节串比较, 整数标注仅助记 | 本文档 §3.3; XGuiRemoteProto.h bannerIsValid 注释 |
| 5 | 回环设备跨线程对端生命周期语义不可实现(锁不覆盖"对端对象是否存在"竞态) | medium | **采纳建议②+peerAlive 降级**: 共享对偶块(两环形缓冲+互斥锁+双端存活标志)改由**对偶引用计数**持有, 任一端 delete 原子清存活标志、末删者释放共享块, 对端对象不悬垂; peerAlive 语义冻结为"本端视角经共享块原子标志判定", 不持有对端对象指针 | XGuiRemoteLoopback.h 文件头/m_d/peerAlive 注释; 本文档 §4.5 |
| 6 | 泳道 D 锚点行号随其他战役未提交改动漂移, 双 M 文件并行编辑必冲突 | medium | **采纳**: D 任务书增两条硬约束——① 执行前按落地后最新树重新实读全部锚点(锚点以行内容模式定位, 禁照抄文档行号); ② 与占用 CMakeLists.txt/xgui_regression_test.c 的战役串行交接, 交接窗口前只交付新文件与 CMake 变更清单 | 本文档 §8; 泳道 D 任务书 |
| 7 | XGUIREMOTELOOPBACK_VTABLE_SIZE 悬空宏(枚举未同头声明, 使用即编译错) | low | **采纳**: 头内补 `XCLASS_DEFINE_BEGING/EXTEND_END(XGuiRemoteLoopbackDevice, XIODevice)` 枚举声明(照 XFileDevice.h:85-92 先例), 宏保留且紧随其后; 已用引用该宏的测试 TU 复验编译通过(见自查记录) | XGuiRemoteLoopback.h |
| 8 | poll 回调挂点路径写错(多写了一层子目录) | low | **采纳**: 修正为 `Src/XCode/XEvent/XAbstractEventDispatcher.h`(find 全仓唯一, 本次实跑确认) | 本文档 §4.4 |
| 9 | §7.4 残留"u16 编码前不裁剪/溢出回绕"与 §3.2 i16 饱和口径自相矛盾 | low | **采纳**: 删除 u16/回绕残留句, 统一为"i16 有符号编码, ±32767 饱和, 远端注入前裁剪到窗口边界" | 本文档 §7.4 |
| 10 | 消息内未知枚举值的错误路径未冻结 | low | **采纳**: 冻结双轨口径——封闭枚举(format/codec/action/mode/method/profileId)值域严格校验, 非法值 dec 返回 false 按协议错误断链; 开放字段(flags/保留位/BYE reason/ERROR code)不校验、透传或归化, 为前向兼容留空间 | XGuiRemoteProto.h dec 统一约定注释; 本文档 §3.4 |

---

## 13. 诊断探针(2026-10-05 perf8 战役增补, 全部 env 门控, 关闭零成本)

### 13.1 事件循环唤醒探针 XGUI_REMOTE_WAKE_PROF=1
环层读完成打点 → 消息层测「socket 数据到达→解析派发」逐消息延迟, 每 5s 输出
`[wake][标签]` 摘要行(环层 [wake][ring] + 端点 [wake][srv-kern]/[wake][cli-kern]/
[wake][cli-tcp]/[wake][cli-udp])。落点: XAbstractNetIoRing.c 探针节 +
XGuiRemoteUdpChannel.c 收侧。

### 13.2 fb→镜像腿分段探针 XGUI_REMOTE_STAGE_PROF=1
逐段打点, 每 5s 每标签一行 `[stage][标签] n=.. p50=..us p95=..us max=..us
[bytes=.. avg=..]`(stderr; pump 回调驱动落盘)。分段口径:

| 标签 | 段 | 字节列口径 |
|------|----|-----------|
| srv-cap | 呈现回调采集拷贝(锁内) | 采集写影线格式字节 |
| srv-lat | 采集端→编码线程认领(maxFps 门控+唤醒+扫描) | — |
| srv-scan | 锁内脏扫描+tile 拷出(lat 子段) | — |
| srv-enc | 认领→批次入队(哈希去重+RLE+拆帧组批) | 编码输出批字节 |
| srv-q | 入队→泵发送始 | — |
| srv-send | 发送调用时长 | 发出帧线载字节 |
| srv-in2fb | 远端输入注入→呈现回调端(设备本地单钟) | — |
| cli-dec | 单帧 FB_UPDATE 解码(帧头+逐 tile) | 收侧线载字节(与 srv-send 对账) |
| cli-merg | 首块损伤到达→present 调用(µs 单调戳; perf9 路5 起含首伤即现路径, 即「首现延迟」口径) | — |
| cli-paint | 上屏触发→paintEvent 派发 | — |
| cli-blit | paintEvent 绘制(黑底填充+FIT 缩放 blit) | — |

跨线程段(srv-lat/srv-q)以单调钟戳经队列节点/会话字段传递; 跨机器段不做减法。
paintEvent 之后的框架 flush 上屏走 XGPU_W_FRAME_PROF=1 的 [wprof] 行。
字节数聚合 API: `XGuiRemoteUdp_stageProfBytes(tag, n)`(XGuiRemoteUdpChannel.h)。
回环实测账本样例见战役数据 out/perf8/ledger.md; 编码基准源码
verify/round5/rle_bench_main.c(+rle_impl.h 旧/新实现对偶), 运行结果归档
out/perf8/rle_bench_desktop_*.txt。

---

## 14. 构建卫生(交叉/诊断构建纪律, 2026-10-05 路 D 收口; 合并改号 13→14)

- **诊断/交叉构建一律私有输出目录，严禁写仓库 `bin/`**（历史事故：armel 产物覆盖桌面 x86 门禁二进制 `./bin/XGuiRegression_Test`）。
- **configure 期硬门（CMakeLists.txt，本版新增）**：`CMAKE_CROSSCOMPILING`（armel 等 toolchain 配置）下 `CMAKE_RUNTIME_OUTPUT_DIRECTORY` 经 REALPATH 解析后等于本仓库 `bin/` 即 `FATAL_ERROR`，错误信息给出私有目录正确姿势（`-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=/tmp/armel-out`）。三例实测：交叉+指向仓库 bin=拒绝(exit 1)；交叉+私有目录=放行；非交叉=不受影响。
- **部署回滚锚（out/perf8/deploy_mcgs.sh）**：替换前 `cp APP APP.bak`（.bak=上一可用版本）；启动后三验（进程+TCP 46300+UDP 绑定）不过自动回滚 .bak。禁无锚覆盖。
- armel 构建定版五开关（RENDER_MODE=1/BUFFER_COUNT=2/RGB16=ON/FBDEV=ON/FBINPUT=ON）与部署动作序列见现网战役任务书；本节为防呆纪律的单一归档点。

---

## 15. 拖动快照 blit 与镜像帧同步(2026-10-06 拖动流畅性战役增补)

- **主树改动**（主窗口拖动卡顿根治，Qt4 QWS `QScreen::blit` 同款语义）：装饰拖拽
  移动（`XWindowDecoration` xwd_applyMove）在 fbdev 直写面板上默认启用快照
  blit——拖动开始把窗口像素自后备缓冲已合成内容快照进窗口大小离屏 buffer，
  每步「条带归位（复用 blitPanelRects/fill 家族）+ 窗口快照整块直写 fb 可见
  面（`XPlatformBackingStore_blitSnapshotPanelRects`，带弹层遮挡剔除）+ 收窄
  cacheSync + pan 收敛」，旁路整窗 flush（软件双缓冲互同步/差带账本重搬/翻页
  全免）；拖动结束立即释放快照并经 `XWidget_update` 真实整窗 PAINT 落定（差
  带账本补齐后台缓冲、恢复轮换写，红线不动：setPresentCallback 挂点、
  requestPanelClear 一次性语义）。env 门控 `XGUI_DRAG_SNAPSHOT_BLIT`（默认开，
  =0 回退旧路径逐位旧行为）；`XGUI_GESTURE_PROF=1` 输出每手势分相一行
  （`[xgesture] drag mode=... steps=... snap=...`）。
- **镜像帧同步契约**：快照路径绕过 flush，但每步经
  `XPlatformBackingStore_notifyPresentRegion`（公共层新出口，内部即既有
  present 回调；登记机制零改动）以与 flush 完全同形的区域/offset 通知——
  xgs_presentWrapper 采集链对快照拖动帧的可见性与旧路径逐位一致，§6.2
  host/采集/编码流水零改动。
- **mcgs A33 实测**（800×600 主窗、xinj2 标题条走廊、213 步/臂）：无镜像
  legacy 11.94ms/步 vs snapshot 11.68ms/步（本板 cacheSync=板级占位 no-op，
  fb 直写 ~1.13MB/步≈97MB/s 为共同硬底，即 Qt4 QWS 同硬件 10-15ms/步口径）；
  镜像 1 会话时每步 ~31ms 由采集+编码 CPU 竞争主导，两模式持平。判据与
  边界归档 out/perf9/dialog-verify-checklist.md（D1-D8）。
