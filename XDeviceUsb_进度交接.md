# XDeviceUsbHost / XDeviceUsbGadget USB 改造进度

> 更新时间：2026-08-23（FunctionFS UAPI 修正）
> 工作区：`D:\code\CMake\XinYueC`

## 当前命名

- `XDeviceUsbHost`：USB 主机侧，负责枚举、匹配外接设备、控制传输、端点传输和热插拔。
- `XDeviceUsbGadget`：USB 从机侧，也称 USB Device/Gadget，负责描述符、Setup 请求、端点和设备事件。
- 类别名分别为 `usbhost` 和 `usbgadget`，避免从名称上混淆主机与从机。
- Host API 中的 `XDeviceUsbDeviceInfo`、`XDeviceUsbDeviceSelector` 表示“被主机操作的外接 USB 设备信息/选择条件”，不是从机类名称，已保留。

## 已完成

0. USB Mass Storage (MSC BOT) 协议层：
   - `Src\XDevice\XDeviceUsbMsc.h` / `XDeviceUsbMsc.c`，独立于平台后端，只依赖 `XDeviceUsbHost` 公共 API。
   - 支持从当前配置描述符自动识别 MSC BOT 接口（类 08h / 子类 06h / 协议 50h），自动 Claim 接口、选择备用设置、定位 Bulk IN/OUT 端点。
   - 实现完整 BOT 状态机：CBW 命令包装器、数据阶段、CSW 状态包装器校验、dCSWTag/dCSWDataResidue 核对、Phase Error 检测。
   - 支持 SCSI INQUIRY、READ CAPACITY(10/16)、READ(10/16)、WRITE(10/16) 高层块读写 API。
   - 支持 BOT Reset Recovery：Mass Storage Reset 类请求 + 清除两端点 Halt + 复位标签计数器。
   - 支持 BOT Reset Recovery：Mass Storage Reset 类请求 + 清除两端点 Halt + 复位标签计数器。
   - 不接管操作系统存储栈；Windows 上仅对已绑定 WinUSB 的 MSC 设备有效，不触碰系统独占的 `PhysicalDrive`。
   - 增强 BOT 错误恢复：CBW 发送失败、CSW 阶段 STALL、CSW 签名/标签不匹配、Phase Error 均自动执行 Bulk-Only Reset Recovery；数据阶段 STALL 先清端点 Halt 再读取 CSW；dCSWDataResidue 参与已传输字节数校正。
   - 新增 SCSI 命令：TEST UNIT READY、REQUEST SENSE（18 字节固定格式，解析 Sense Key/ASC/ASCQ）、PREVENT ALLOW MEDIUM REMOVAL、START STOP UNIT。
   - 新增便捷字节级读写：`XDeviceUsbMsc_readBytes` / `XDeviceUsbMsc_writeBytes`，自动按块大小分块，支持 READ(10/16) 与 WRITE(10/16) 自动切换，单批最多 65535 块。

1. 公共 Host 文件已重命名：
   - `Src\XDevice\XDeviceUsbHost.h`
   - `Src\XDevice\XDeviceUsbHost.c`
2. 公共 Gadget 文件已重命名：
   - `Src\XDevice\XDeviceUsbGadget.h`
   - `Src\XDevice\XDeviceUsbGadget.c`
3. Host 类、函数、平台后端入口统一为 `XDeviceUsbHost_*`。
4. Gadget 类、函数、平台后端入口统一为 `XDeviceUsbGadget_*`。
5. `Src\XDevice\XDevice.c` 已加入两个类的头文件和懒注册分支。
6. `XDeviceUsbHost_open` 使用类别 `usbhost`；`XDeviceUsbGadget_open` 使用类别 `usbgadget`。
7. Windows、POSIX、Unsupported 平台入口及共用不支持存根已加入：
   - `Drive\windows\Device\Usb\XDeviceUsb_win32.c`
   - `Drive\Posix\Device\Usb\XDeviceUsb_posix.c`
   - `Drive\Unsupported\Device\XDeviceUsb_unsupported.c`
   - `Drive\XDeviceUsb_platform_stub.inc`
8. 已删除旧头文件：
   - `Src\XPlatform\XUsb\XUsb.h`
   - `Src\XPlatform\XUsb\XUsbDeviceController.h`
9. 已删除批量替换产生的 `.bak` 文件。
10. Windows Host 第一阶段真实后端已接入：
    - `Drive\windows\Device\Usb\XDeviceUsb_win32.c` 使用 SetupAPI + WinUSB。
    - 支持 WinUSB 设备枚举、VID/PID/设备类匹配、设备/配置描述符、接口/端点查询。
    - 支持控制传输、Bulk/Interrupt 同步传输、清除端点 Halt。
    - CMake Windows 链接已加入 `setupapi` 和 `winusb`。
    - 异步 Bulk/Interrupt 传输已接入全局 `XNetIoRingWin32` IOCP，传输完成回调由框架事件循环线程执行；取消和设备关闭会先取消 I/O，再泵送 IOCP 完成后释放上下文。
    - 异步请求已支持 `m_timeoutMs > 0`：Windows 后端将请求挂入控制器已打开设备链表，由 `processEvents` 扫描到期请求并调用 `CancelIoEx`，完成回调报告 `XDeviceUsbTransferResult_Timeout`。
    - 打开的设备已关联到所属控制器，关闭设备时从控制器链表摘除，避免控制器事件处理访问已释放设备。
    - `XEventContext_IOCP` 增加 `finishedBytes` 对应的原生完成状态 `completed/nativeError`，USB 回调返回 `false`，不会污染网络 CQ。
    - 控制传输和同步 Bulk/Interrupt 使用同步 WinUSB 调用，避免临时 OVERLAPPED 被全局 IOCP 消费；当前正数超时参数返回“不支持”，负数表示不限时，0 使用 WinUSB 默认同步语义。
    - 已实现 Windows Configuration Manager 原生设备接口通知：`CM_Register_Notification` 回调只置变更标志，`processEvents` 线程完成 SetupAPI 快照比较并报告 Arrived/Removed/Changed；`enumerate` 与设备打开每次都建立实时 SetupAPI 快照，因此控制器打开后的新设备无需重开控制器即可枚举和打开。
    - 支持 Isochronous 同步/异步传输（WinUSB isoch buffer / packet descriptor）；实际可用性取决于 Windows 版本和 WinUSB 绑定设备的端点类型。
    - 支持标准 `SET_CONFIGURATION` 多配置切换：打开时通过 `GET_CONFIGURATION` 获取真实活动配置，切换后按 `bConfigurationValue` 重新读取对应配置描述符、初始化 WinUSB 接口并清空 Alternate Setting 状态；配置值不要求连续。
    - 接口 Claim/Release 由框架层位图管理，重复 Claim 返回 Busy；Release 会 Abort 当前接口端点。WinUSB 不具备抢占其他 Windows 驱动的能力。
    - 支持 `WinUsb_SetCurrentAlternateSetting`，端点查询和传输查找按当前 Alternate Setting 工作。
    - Windows `Reset` 使用 SetupAPI 的设备节点 Disable/Enable 实现 PnP Restart；成功后现有 WinUSB 句柄失效，调用方必须 close 后 reopen。未将 `WinUsb_ResetPipe` 误报为设备复位。
11. POSIX Host 真实后端已接入：
    - `Drive\Posix\Device\Usb\XDeviceUsb_posix.c` 在检测到 libusb-1.0 时启用；未检测到时继续使用统一存根。
    - 支持枚举、VID/PID/版本/类/序列号匹配、设备与端点描述符、控制传输、Bulk/Interrupt/Isochronous 同步与异步传输。
    - 支持配置切换、接口 Claim/Release、Alternate Setting、Clear Halt、`libusb_reset_device`、异步取消和 libusb 原生热插拔通知。
    - `processEvents` 是唯一分发 libusb 异步完成与热插拔用户回调的位置。
    - Linux 内核驱动自动剥离：设备打开时启用 `libusb_set_auto_detach_kernel_driver`（libusb >= 1.0.16），Claim 时自动 detach 内核驱动（usbhid、cdc_acm、usb-storage 等）；旧版 libusb 在 Claim 前手动 detach。这样 libusb 就能访问被系统驱动占用的 HID/CDC/MSC 设备，功能上覆盖 Windows HID/CDC 专用后端的大部分场景。
    - 序列号匹配通过 `libusb_get_string_descriptor_ascii` 从设备字符串描述符读取，支持 UTF-8 匹配。

## 12 项能力状态

| 项目 | 状态 | 说明 |
| --- | --- | --- |
| 1. Isochronous | 已实现，待硬件验证 | Windows WinUSB 与 POSIX/libusb 均有同步/异步路径。 |
| 2. Windows 设备级 Reset | 已实现，待权限/硬件验证 | 通过 SetupAPI Disable/Enable 做 PnP Restart；需 close/reopen，不等同物理端口 Reset。 |
| 3. 多配置切换 | 已实现，待硬件验证 | Windows 使用标准请求，POSIX 使用 libusb。 |
| 4. Claim/Release | 已实现，待硬件验证 | WinUSB 是框架层占用管理；libusb 调用原生 Claim/Release。 |
| 5. Alternate Setting | 已实现，待硬件验证 | Windows/Posix 均记录并使用当前设置。 |
| 6. Windows 热插拔 | 已实现，待硬件验证 | Configuration Manager 通知加 SetupAPI 快照。 |
| 7. HID/CDC/MSC 专用后端 | HID、CDC 已实现；MSC 未实现 | HID 使用系统 HID API；CDC ACM 使用系统 COM 驱动和 CDC Line Coding 映射；MSC 需单独的块/文件系统适配。 |
| 8. POSIX/libusb | 已实现，待 Linux/macOS/BSD 实机验证 | CMake 自动探测 libusb-1.0。 |
| 9. 嵌入式 USB Host | 阻塞于 BSP/协议栈选择 | 当前仓库没有 TinyUSB、STM32Cube Host、ESP-IDF Host 等实现或板卡信息。 |
| 10. 嵌入式 USB Gadget | 阻塞于 BSP/协议栈选择 | Linux FunctionFS Gadget 已实现；嵌入式仍需 TinyUSB/STM32Cube/ESP-IDF BSP 适配。 |
| 11. Windows Gadget/Device Mode | 硬件/驱动限制 | 通用 Windows PC 通常仅能作为 Host；需要支持 Device Mode 的控制器和专用驱动。 |
| 12. 真实硬件测试 | 未执行 | 需要实际 WinUSB、libusb 等时设备及目标嵌入式板卡。 |

## 构建验证

新增或重命名 `.c` 文件后，CMake 的递归 GLOB 需要重新配置。Windows MSVC 已验证通过，目标产物为 `bin\Debug\XinYueCd.dll`。

验证命令：

```powershell
cmake -S . -B out/build/x64-USB -G "Visual Studio 18 2026" -A x64
& C:\WINDOWS\system32\cmd.exe /d /c 'call "C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build out\build\x64-USB --target XinYueC -j 1'
```

使用单进程构建是为了避免当前 MSVC 环境下并行编译触发 C1041 PDB 锁冲突。

另已使用 OpenOCD 附带的 libusb 头文件，创建独立的静态检查构建并强制开启
`XINYUE_C_HAS_LIBUSB=1`；`XinYueCSd.lib` 已成功生成。该检查只验证 C 编译，
不代表 Windows 可链接或运行 POSIX libusb 后端。

当前 Windows 工作区没有 Linux 编译器、Linux 内核 FunctionFS 头文件或 Linux Gadget
硬件，因此新增 FunctionFS 源文件尚未完成 Linux 目标机编译和真实连接验证；Windows
构建只验证其条件编译不会影响 Windows 产物。

## Windows 使用条件

- 设备必须绑定 WinUSB 驱动，并提供可访问的 USB 设备接口路径；没有 WinUSB 绑定的设备会在枚举时被跳过。
- 可使用设备厂商 INF、Microsoft OS 描述符或开发阶段的 Zadig 将测试设备绑定到 WinUSB。
- HID 设备不应改绑 WinUSB：后端通过 HID Interface GUID 枚举，Input/Output Report 分别显示为合成 Interrupt IN `0x81` / OUT `0x01` 端点，单次长度必须等于相应 Report 长度；异步 I/O 复用 IOCP。HID Feature Report 仅支持类接口 `GET_REPORT` / `SET_REPORT`（Report Type = Feature），且缓冲区必须包含、并与 Setup `wValue` 一致的 Report ID，长度必须等于 Feature Report 长度。该映射不是通用原始 USB 描述符或控制传输访问；同步 HID/CDC/WinUSB 传输的正数超时暂不支持。
- CDC ACM 不应改绑 WinUSB：后端通过 `GUID_DEVINTERFACE_COMPORT` 枚举，并且只接纳 PnP Compatible ID 声明 `USB\Class_02` 的设备，避免把普通 COM 口误识别为 USB CDC。它将 COM 字节流映射为合成 Bulk IN `0x81` / OUT `0x01`，支持异步 I/O、热插拔、PnP Restart，以及 CDC ACM `SET_LINE_CODING`、`GET_LINE_CODING`、`SET_CONTROL_LINE_STATE`、`SEND_BREAK` 类接口请求；这不是原始 USB 描述符或端点访问。
- MSC 由 Windows 存储栈独占，不能安全地伪装成可发送 CBW/CSW 的 Bulk 端点；需要以卷、文件或受控块设备 API 设计独立适配层。公共层 `XDeviceUsbMsc` BOT 协议模块可在 Linux 上配合 libusb + detach kernel driver 直接访问 MSC 设备。
- Windows Reset 使用 PnP Restart，会导致现有 WinUSB 句柄失效，成功后必须 close/reopen；它不能替代物理端口 Reset，也不能以 `WinUsb_ResetPipe` 代替。等时传输、配置切换和 Alternate Setting 均需真实设备验证。
- Windows 异步传输要求全局 `XAbstractNetIoRing` 已启用；如果事件环未创建，提交异步传输会失败并返回无效传输 ID。异步完成和超时都需要事件循环继续调用 `processEvents` 或框架主事件分发。
- Windows 后端接受已绑定 WinUSB 的设备接口，以及保持系统 HID、CDC ACM 驱动绑定的设备；MSC 专用后端仍待实现。

## Linux FunctionFS Gadget

- `Drive/Posix/Device/Usb/XDeviceUsb_gadget_linux.c` 已接入 Linux FunctionFS：配置描述符解析、FunctionFS descriptors/strings 写入、Setup 回调、端点同步/异步 I/O、取消、超时、FunctionFS 生命周期事件和挂起/恢复事件。
- FunctionFS descriptors/strings 已按 Linux UAPI v2 格式修正：使用内核定义的 v2 头、所有整数以小端写入、FS/HS 描述符列表分别完整写入；字符串区使用 FunctionFS 要求的 UTF-8 NUL 结尾字符串，而非 USB 线上的 UTF-16 字节流。
- `Connected` 仅在 FunctionFS `BIND` 事件时上报；挂起/恢复保存恢复前状态；清空端点队列会取消框架未完成请求并调用 `FUNCTIONFS_FIFO_FLUSH`，不再误用 `FUNCTIONFS_CLEAR_HALT`。
- FunctionFS 后端在 `configure`/`start` 时严格校验单配置、FS/HS、无 BOS、配置值/属性/功耗与原始配置描述符一致；它只写一个 FunctionFS UTF-8 字符串语言表，因此输入字符串必须使用同一 LANGID，且索引不能重复。
- 使用前需要 root/能力权限以及用户预先创建的 configfs Gadget，并挂载 FunctionFS；`XDeviceUsbGadgetChannel.m_name` 指向 FunctionFS 挂载目录，例如 `/dev/ffs/xinyuec`。后端不自动创建或绑定 configfs 拓扑。
- 提供 `Drive/Posix/Device/Usb/XDeviceUsb_functionfs_setup.sh` 初始化脚本，可创建基础 configfs Gadget、`ffs.<name>` 函数并挂载 FunctionFS；脚本不会替应用写入描述符，也不会在未指定 UDC 时自动绑定硬件。
- 当前 FunctionFS 适配要求 Linux FunctionFS v2 UAPI（Linux 3.14 及以上）；限制为单配置和 USB 2.0 FS/HS。原始配置字节必须包含标准配置描述符，后端向 FunctionFS 写入其后的接口/端点描述符流。FunctionFS/configfs 的设备描述符、最大功率、复合拓扑和 BOS 属性仍由外部 configfs 负责，当前后端不会自动写入这些属性。端点 STALL、EP0 可靠 STALL、Remote Wakeup、SuperSpeed 专用描述符和多配置切换仍需底层 Gadget/BSP 支持，未在此后端宣称为已实现。

## 后续平台实现

Windows 和 POSIX Host 已有真实后端，Linux FunctionFS Gadget 已有条件后端；未匹配平台的
Gadget 与嵌入式仍保留明确返回“不支持”的存根。后续可分别接入：

- Windows：WinUSB、HID 或厂商驱动；Gadget 取决于硬件/驱动是否提供设备模式。
- POSIX：Linux libusb 与 Linux FunctionFS Gadget 已有后端；macOS IOKit 或 BSD USB 框架仍待实现。
- 嵌入式：在对应平台文件中实现 `XDeviceUsbHost_platform*` 或 `XDeviceUsbGadget_platform*`，公共层无需引入 HAL/SDK 头文件。

工程目标为 C99，新增代码不能使用 `_Static_assert` 或 `_Generic`。平台实现必须保持 Host 与 Gadget 的符号前缀独立。


## 12 项清单状态总览

| # | 项目 | Windows | Linux/POSIX | 状态 |
|---|------|---------|-------------|------|
| 1 | Isochronous 等时传输 | WinUSB isoch buffer / packet descriptor，同步与异步均支持 | libusb 等时传输，同步与异步均支持 | ✅ 已实现，需实机验证 |
| 2 | Windows USB 设备级 Reset | SetupAPI PnP Restart（Disable/Enable 设备节点） | libusb_reset_device | ✅ 已实现 |
| 3 | 多配置切换 | SET_CONFIGURATION + 按 bConfigurationValue 重读配置描述符 | libusb_set_configuration + 刷新缓存 | ✅ 已实现 |
| 4 | 真正的接口 Claim/Release 管理 | 框架层位图管理，重复 Claim 返回 Busy；Release 会 Abort 端点 | libusb claim/release + 自动 detach kernel driver | ✅ 已实现 |
| 5 | 完整 Alternate Setting 支持 | WinUsb_SetCurrentAlternateSetting，端点查询与传输按当前 Alternate 查找 | libusb_set_interface_alt_setting | ✅ 已实现 |
| 6 | 原生 Windows 热插拔通知 | CM_Register_Notification + SetupAPI 快照比对，processEvents 分发 | libusb hotplug callback + 快照比对 | ✅ 已实现 |
| 7 | HID、CDC、MSC 专用驱动后端 | HID（合成 Interrupt 端点 + Feature Report）、CDC ACM（合成 Bulk 端点 + Line Coding）、MSC 无（系统存储栈独占） | HID/CDC/MSC 通过 libusb + detach kernel driver 访问；MSC 另有独立 BOT 协议层 | ✅ 核心已覆盖 |
| 8 | POSIX/libusb 实现 | — | 枚举、描述符、控制、Bulk/Int/Iso、异步、取消、热插拔、Claim、Alternate、Reset | ✅ 已实现 |
| 9 | 嵌入式 USB Host 实现 | — | — | 🔴 未开始（需指定 BSP/协议栈） |
| 10 | 嵌入式 USB Gadget 实现 | — | — | 🔴 未开始（需指定 BSP/协议栈） |
| 11 | Windows Gadget/Device Mode 实现 | 需要支持 Device Mode 的专用硬件/驱动（如 MTP/USBFN） | Linux FunctionFS + configfs（已实现） | 🟡 Linux 已完成；Windows 取决于硬件 |
| 12 | 真实 USB 硬件测试 | — | — | 🔴 未测试（待实机验证） |

### MSC BOT 协议层（跨平台）

- 文件：`Src/XDevice/XDeviceUsbMsc.h` / `XDeviceUsbMsc.c`
- 依赖：仅依赖 `XDeviceUsbHost` 公共 API，纯协议层，不引入平台头文件。
- 已实现：BOT 状态机（CBW/数据/CSW）、SCSI INQUIRY / TEST UNIT READY / REQUEST SENSE / READ CAPACITY(10/16) / READ(10/16) / WRITE(10/16) / PREVENT ALLOW / START STOP UNIT、BOT Reset Recovery、分块字节读写、错误自动恢复（STALL 清 Halt、Phase Error 重置总线）。
- Windows 上使用条件：设备必须绑定 WinUSB 驱动（如使用 Zadig 替换），不能在系统存储栈占用状态下访问。
- Linux 上使用条件：libusb + detach kernel driver 自动剥离 usb-storage，配合 MSC 协议层即可访问 U 盘等设备。

### 编译验证

- Windows x64：Visual Studio 2026，`XinYueCd.dll` 编译通过（含 MSC 模块）。
- Linux/POSIX：当前环境未验证，需在 Linux 上安装 `libusb-1.0-0-dev` 并启用 `XINYUE_C_HAS_LIBUSB` 后编译。
- C99：未使用 `_Static_assert` / `_Generic`。
## TinyUSB 嵌入式协议栈接入

- 已按 Library/ 统一模式完成 TinyUSB 0.21.0 集成：
  - 源码：Library/tinyusb/src/（从 tinyusb-0.21.0.zip 解压，194 个源文件）。
  - 构建：Library/tinyusb/CMakeLists.txt。
  - 配置：Library/tinyusb/tusb_config.h。
  - 文档：Library/tinyusb/README.md。
  - 主 CMakeLists.txt 已加入 dd_subdirectory(Library/tinyusb)。
- 默认不启用（桌面构建检测到 XINYUE_C_HAS_TINYUSB 未定义时直接 eturn() 跳过），不影响 Windows / Linux 构建。
- 启用方式：嵌入式构建时定义 XINYUE_C_HAS_TINYUSB，并按需设置：
  - TINYUSB_DEVICE / TINYUSB_HOST：设备/主机栈开关。
  - TINYUSB_OS：
one / reertos / tthread。
  - TINYUSB_CLASS_XXX：各 CDC/MSC/HID/DFU/MIDI/Audio/Video/Net/Vendor/BTH/MTP/Printer/USBTMC 类驱动开关。
  - TINYUSB_PORT_DIRS：具体 MCU 硬件驱动目录列表，如 st/stm32_fsdev、synopsys/dwc2、aspberrypi/rp2040 等，CMake 自动 glob 该目录下所有 .c。
- **内存管理：无需接入 XMemory**。TinyUSB 0.21.0 为纯静态分配，全协议栈零 malloc/零 ree，所有缓冲区大小由 	usb_config.h 宏在编译时确定。FIFO 使用 TU_FIFO_DEF 宏静态定义，无运行时动态分配。
- 下一步嵌入式后端实现位置：Drive/TinyUSB/Device/Usb/，与 WinUSB / libusb / FunctionFS 平级，实现 XDeviceUsbHost_platform* 和 XDeviceUsbGadget_platform*。

### 事件循环自动轮询

TinyUSB 后端已接入 XinYueC 事件循环的**周期轮询回调**机制：

**机制说明：**
- XAbstractEventDispatcher 新增通用轮询回调列表（m_pollCallbacks）。
- API：XAbstractEventDispatcher_addPollCallback() / XAbstractEventDispatcher_removePollCallback()。
- 每次 processEvents 迭代中，在定时器之后、I/O 处理之前，调用列表中所有回调。
- 懒回收机制：注销只标记删除，下一轮分发时释放节点，避免遍历中修改链表。

**USB 接入：**
- **Host 控制器**：platformControllerOpen 成功后自动注册，platformControllerClose 时自动注销。
- **Gadget 控制器**：platformStart 成功后自动注册，platformStop 时自动注销。
- 回调内部调用 XDeviceUsbHost_platformControllerProcessEvents / XDeviceUsbGadget_platformProcessEvents，即 	uh_task() / 	ud_task()。

**效果：**
打开 USB Host/Gadget 后，主线程事件循环会**自动**轮询 TinyUSB，
应用层无需手动调用 XDeviceUsbHost_processEvents / XDeviceUsbGadget_processEvents。

**平台范围：**
仅 TinyUSB 后端需要（嵌入式）。Windows 后端走 IOCP、Linux 后端走 libusb 事件/epoll，
都由 I/O 事件驱动，不需要周期轮询。
### lwIP 轮询迁移到网络模块（事件循环解耦）

lwIP 的 pollLwip 已从事件调度器中移出，改为网络模块自动注册：

- **删除**：XAbstractEventDispatcher.c 中 #ifdef XNETWORK_USE_LWIP / XAbstractNetIoRing_pollLwip() 调用。
- **删除**：XAbstractNetIoRing.c 中的 pollLwip 实现（含锁宏、lwIP 头依赖）。
- **删除**：XAbstractNetIoRing.h 中的 pollLwip 声明及 lwIP 相关注释。
- **移入**：XDeviceNetwork.c 中的完整实现：
  - 锁宏 XLWIP_LOCK / XLWIP_UNLOCK（覆盖 NO_SYS + SYS_LIGHTWEIGHT_PROT / NO_SYS / 非 NO_SYS 三种情况）。
  - xLwipPollInternal()：加锁调用 XDeviceNetwork_poll()。
  - xLwipEventLoopPoll()：事件循环轮询回调。
  - 打开第一个网络设备时 XAbstractEventDispatcher_addPollCallback 注册，关闭最后一个时注销。
  - 引用计数 g_lwipDeviceCount 管理生命周期。
  - 前置声明（第 18-21 行）与文件底部定义（第 1124/1135 行）分离，避免启用 XNETWORK_USE_LWIP 时"调用在前声明在后"的编译错误。

**架构效果**：
- 事件调度器不再知道 lwIP 的存在，彻底解耦。
- lwIP 轮询生命周期由网络模块自己管理，与 USB TinyUSB 后端走同一套 ddPollCallback 机制。
- 只有启用 XNETWORK_USE_LWIP 时才注册轮询，不启用时零开销。
### TinyUSB Host 后端 API 修正（对齐 0.21.0）

审查发现 TinyUSB Host 后端最初使用了 0.21.0 之前的旧版 API，启用后必然编译失败。已全面修正：

| 旧 API（不存在于 0.21.0） | 新 API（0.21.0 正确用法） |
|---|---|
| 	uh_control_xfer(daddr, &req, data, cb) | 	uh_control_xfer(&tuh_xfer_t)，complete_cb==NULL 时自带同步阻塞 |
| 	uh_bulk_xfer / tuh_int_xfer / tuh_iso_xfer | 统一 	uh_edpt_xfer(&tuh_xfer_t)（总是异步，需自行轮询等待） |
| 	uh_clear_halt(...) | 标准 CLEAR_FEATURE(ENDPOINT_HALT) 控制请求 |
| 	uh_reset_device(...) | 不存在；per-device reset 无公开 API，返回 Unsupported（	uh_rhport_reset_bus 是总线级） |
| 	uh_descriptor_get_device(..., false) | 	uh_descriptor_get_device_sync(...) |
| 	uh_descriptor_get_configuration(..., false) | 	uh_descriptor_get_configuration_sync(...) |

**同步语义修正**：
- 控制传输：complete_cb=NULL 同步阻塞，完成后读 xfer.result / xfer.actual_len。运行时超时参数不支持（0.21.0 控制传输超时是编译期配置）。
- 端点传输：	uh_edpt_xfer 总是异步 → 新增 xTuHostSyncWaitEdpt() 轮询 	uh_task() 等待完成，支持超时 + abort。
- 新增 xTuHostOpenAndSubmit()：先用 	uh_edpt_open 打开端点（幂等），再提交 	uh_edpt_xfer。

**编译验证限制**：
- TinyUSB 0.21.0 官方只支持 GCC / Clang / IAR / TI 编译器，**不支持 MSVC**（	usb_compiler.h 直接 #error）。
- 本机仅有 MSVC，无法完成 TinyUSB 后端的真实编译验证；include 链已确认解析到 TinyUSB 库自身，剩余卡在库的编译器检测。
- 必须在嵌入式 GCC/Clang 工具链（STM32CubeIDE / ESP-IDF / arm-none-eabi-gcc）中验证。
### 内存管理统一为 XMemory（嵌入式可用）

用户要求嵌入式构建不可依赖标准库内存。已把裸机嵌入式路径上的所有标准库 malloc/calloc/free 替换为 XMemory：

| 文件 | 替换内容 |
|---|---|
| XAbstractEventDispatcher.c | calloc → XCalloc_System；ree(dead) → XFree_System(dead)（轮询回调节点） |
| XDeviceUsb_tinyusb_host.c | calloc → XCalloc_System、malloc → XMalloc_System、ree → XFree_System（共 13 处） |
| XDeviceUsb_tinyusb_gadget.c | calloc → XCalloc_System、ree → XFree_System（共 10 处） |
| XDeviceUsbMsc.c | malloc → XMalloc_System、ree → XFree_System（共 3 处） |

**边界说明**：
- 裸机 MCU 路径（事件循环、TinyUSB 后端、MSC 协议层、XDevice 公共层）已全部走 XMemory，STM32/ESP32 裸机可用。
- Windows WinUSB 后端、POSIX libusb 后端、Linux FunctionFS 后端**保留标准库**：这些平台（Windows 桌面/嵌入式 Linux）CRT 必然存在，且 WinUSB/libusb 本身依赖 CRT，不在裸机范围。
- 公共层惯例与现有 XDeviceUsbHost.c 一致（本就 include XMemory.h 使用 XCalloc_System）。
### 全量内存检查（git 修改文件）

对当前 git 修改的全部文件做了标准库内存扫描，结论：

**公共层 / 裸机路径（全部走 XMemory，无残留）：**
- XAbstractEventDispatcher.c、XDeviceUsbHost.c/.h、XDeviceUsbGadget.c/.h、XDeviceUsbMsc.c/.h、XDevice.c、XDeviceNetwork.c、TinyUSB Host/Gadget 后端、XDeviceUsb_platform_stub.inc、XDeviceUsb_unsupported.c
- 本轮补充修复：XDeviceNetwork.c 的 4 处历史遗留（socket 创建 calloc/ree → XCalloc_System/XFree_System），嵌入式 lwIP 网络栈路径现已全部走 XMemory。

**平台后端（保留标准库，平台 CRT 必然存在）：**
- XDeviceUsb_win32.c（50+ 处）：Windows WinUSB，依赖 CRT 的 wchar/SetupAPI 分配模型。
- XDeviceUsb_posix.c（9 处）：桌面/嵌入式 Linux libusb，依赖 CRT。
- XDeviceUsb_gadget_linux.c（14 处）：Linux FunctionFS，依赖 CRT。
- XNetIoRingWin32.c/.h：Windows IOCP，依赖 CRT。

**结论**：裸机 MCU（STM32F4/ESP32-S3）构建链上的所有代码已统一走 XMemory；桌面/嵌入式 Linux 和 Windows 后端使用系统 CRT（这些平台 malloc 必然可用）。