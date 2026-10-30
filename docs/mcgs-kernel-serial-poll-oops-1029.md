# mcgs 屏（sun8i 5.2.9 内核）串口探测原语崩内核——立案与应用侧能力门修复

> 立案日期：2026-10-29
> 板子：mcgs 昆仑通态屏 192.168.1.252（Allwinner sun8i，vendor 内核 5.2.9 #17）
> 结论：**该内核 tty 子系统的「串口可用性探测」两条标准原语全灭**
> （ioctl FIONREAD 与 poll/select 都会崩内核）；应用侧按能力门规避
> （sunxi 板走「非阻塞 fd + read 泵」，其余平台一字不改），已真机闭环。

## 1. 两案现场（dmesg 实证）

| # | 原语 | 崩点 | 触发 | 现场 |
|---|---|---|---|---|
| ① | `ioctl(fd, FIONREAD)` | `PC at tty_ioctl+0x20`，`r1=0x541b` | RX 轮询每 2ms 探测，**十几秒必触发** | 2026-10-29 从站页 ttyS9 两档波特率同崩点 |
| ② | `poll(fd, POLLIN, 0)` | `PC at tty_poll+0x1c`，`LR pserial_poll+0x20` | 同上，**秒级触发** | 干净内核（重启后 oops=0 起测）4s 即崩 |

②的调用链说明是 **sun8i 串口驱动（pserial）自己的 poll 方法调坏指针**：
`pserial_poll+0x20` 调进核心层 `tty_poll` 随即 NULL 解引用——驱动级缺陷，
应用层换什么探测 API 都躲不开，唯一不经过坏路径的原语只剩 `read()`。

另有一次 `sock_poll` oops 发生在**内核已被前序 oops 污染之后**（Tainted "D"，
LR 被污染成 revents 值 0x19）：重启清场后连续浸泡 6 分钟零 oops，确证为
**post-oops 连带损坏，非独立 bug**。教训：**老内核上取证必须先重启清场**，
被污染内核上看到的第二个崩点很可能是第一個崩点的下游假象。

## 2. 修复（`Drive/Posix/Device/XSerialPortPosix.c`，能力门）

**其他设备零回归是硬约束**（用户 2026-10-29 明确要求），故不对齐内核版本
粗分档，而按设备树精确门控——对齐库里已有的 XNET 引擎「内核版本门」先例：

- 门判据：`/proc/device-tree/compatible` 含 `sunxi`/`sun8i`（mcgs 这类全志
  板）→ 走 **read 泵路径**；其余平台维持原 `FIONREAD` 路径**一字不改**：
  - 桌面 x86：无设备树文件 → 原路；
  - RK3568：compatible 为 `rockchip,rk3568` → 原路（485 主战场不受影响）；
  - Windows/STM32/ESP 有各自平台文件，本文件改动不可见。
- 泵路径 = fd 恒 `O_NONBLOCK`（open 时按门加旗标）+ `BytesAvailable`
  探测改为「非阻塞 `read()` 泵进 `m_readBuffer`」——`platformRead` 本就
  优先读 ring，数据面语义不变；`BytesAvailable` 全部消费方只用 `>0`
  判据（全库已审计）。单次泵上限 256B×4 轮，防对端连发饿死主循环，
  余量留内核缓冲下个 2ms 节拍续泵。
- 逃生口（测试/现场取证）：`XSERIAL_FORCE_READPUMP=1` 强制泵路径，
  `XSERIAL_FORCE_FIONREAD=1` 反向强制（对照复现原崩点用）。

## 3. 验证（双路径 × 双平台）

| 项 | 结果 |
|---|---|
| 桌面门禁（regression + demo autotest） | 0 失败 ×2 |
| 桌面 pty：默认路（FIONREAD，原样） | 115200 生效、slave 启动 ✓ |
| 桌面 pty：`FORCE_READPUMP=1` | 115200 生效、slave 启动 ✓ |
| Hub 测试套件（真 pty RTU 读写）：默认 vs 泵 | RC 同为 12（=交接单既有未收口 12 项），**失败集逐条一致** ⇒ 数据面零回归 |
| mcgs 真机（原生命中泵门）干净内核浸泡 6min | **oops=0**，双实例健在，115200 生效，背光正常 |

## 4. 遗留与建议

- **内核侧不在本仓范围**：5.2.9 sun8i vendor BSP 内核，升级/修补需面板
  厂商或 mainline sunxi 内核（mainline ≥5.x 的 sunxi 串口为标准 8250
  核心，poll/ioctl 无此缺陷）。若日后换固件，能力门可随时用
  `XSERIAL_FORCE_FIONREAD=1` 取证对照。
- 该内核既往案底：无 FAST_POLL accept 僵死（10-11，能力门回退 EPOLL）、
  io_uring CONNECT=-EINVAL（10-16，版本门 ≥5.10）——本门是同款纪律第三案。
- 桌面回归基线：Hub 套件在本树的失败集 = 交接单 §0 的既有 12 项
  （缓存层用例⑨⑩未收口），与串口改动无关，收口责任在 Hub 缓存层战役。
