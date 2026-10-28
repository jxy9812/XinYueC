# 昆仑通态屏（mcgs，192.168.1.252）armel 交叉编译与部署指南

> 2026-10-26 定稿。目标：在开发机上交叉编译 XinYueC 的 armel（32 位 ARM
> soft-float）产物并部署到昆仑通态 MCGS 屏（全志 A33），用于断线重连等
> 网络层修复的实机验证。本文记录完整命令序列与全部已知陷阱；构建配方中
> 的 -latomic 与 fbdev 排除逻辑已固化进 CMakeLists / 依赖探测，但环境
> 变量一步（env.sh）仍须每次手动 source。

## 0. 目标设备画像

| 项 | 值 |
| --- | --- |
| 主机名 / SoC | 昆仑通态 MCGS 屏 / 全志 A33（4×Cortex-A7） |
| 内核 | 5.2.9（MCGS 定制；**无 io_uring 能力门以下版本 → 网络引擎默认 epoll**） |
| 系统 | 嵌入式 Linux，glibc **2.26**（交叉 glibc 须 ≤2.26 ABI 兼容） |
| 内存 | 56MB（常驻紧张，可用 ~7-15MB；诊断工具自身可能引爆 OOM，avail<4.5MB 高危） |
| 显示 | fbdev 直写 800×480（RGB565），软件自绘 CSD 标题栏 |
| 触摸 | fbinput（`XPLATFORM_FBINPUT_DEVICE=/dev/input/event0`，pointercal 逆变换） |
| 网络 | ssh root@192.168.1.252（开发机 `~/.ssh/config` 别名 **mcgs**，ed25519 免密） |
| 屏上路径 | 二进制 `/root/xguidemo/XGuiWindowDemo_Test`；启动脚本 `/root/xguidemo_run.sh`；日志 `/root/xguidemo/demo.log` |
| 屏上工具 | xinj2 / rawxy（触摸注入，pointercal 逆矩阵已内置）、fbsnap（可见缓冲抓帧，**禁裸 dd**） |
| 凭据 | root / mcgs（仅留档备免密恢复） |

**网络引擎注意**：内核 5.2.9 < 5.10 版本门，`XNetIoRingPosix` 自动落
**epoll 引擎**（引擎选择用户裁定：≥5.10 才用 io_uring）。该内核的
io_uring CONNECT 操作码实测恒 -EINVAL，代码里的能力门白名单（网络级
拒连 ≠ 内核拒操作码）即为此设计；`XNET_FORCE_IOURING=1` 仅调试用。

## 1. 工具链（~/xtool-armel）

Rootless 解包的 Debian 交叉链：gcc 12.3.0，**gnueabi（无硬浮点）**，
libc=stretch glibc 2.24 + 内核头 4.9——设备 glibc 2.26，ABI 兼容
（deepin 宿主 glibc 2.38 的默认 64bit-time_t 与设备 ABI 不容，**不可用
宿主工具链直编**）。

```bash
. ~/xtool-armel/env.sh   # PATH 前插工具链 bin；LD_LIBRARY_PATH 指向
                         # usr/lib/x86_64-linux-gnu（汇编器 as 运行时要
                         # dlopen libbfd-2.41-armel.so，缺它直接挂）
```

⚠ **每一次编译/链接命令前都必须 source 过 env.sh**（同一 shell 内保
持即可；新开 shell 要重 source）。漏掉的报错形态：
`arm-linux-gnueabi-as: error while loading shared libraries:
libbfd-2.41-armel.so`。

## 2. 编译命令序列（定稿配方）

```bash
cd /home/xinyue/Code/XinYueC
. ~/xtool-armel/env.sh
cmake -B /home/xinyue/armel-build \
    -DCMAKE_TOOLCHAIN_FILE=$HOME/xtool-armel/armel.cmake \
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=/home/xinyue/Code/XinYueC/out/armel-out \
    -DCMAKE_BUILD_TYPE=Release \
    -DXPLATFORM_FBDEV_ON=1 \
    -DXPLATFORM_FBINPUT_ON=1 \
    -DXGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16=1 \
    -DXGUI_BACKINGSTORE_RENDER_MODE=DIRECT
cmake --build /home/xinyue/armel-build --target XGuiWindowDemo_Test -j"$(nproc)"
```

要点：

- **构建树放 /home**（如 `/home/xinyue/armel-build`）。开发机 /tmp 是
  tmpfs 且长期 90%+ 满，configure 产物与归档会写爆；设备侧 /tmp 同为
  tmpfs（实测 27.4M）**铁禁部署**。
- 首次/新增源文件后必须重跑 cmake（源列表 `file(GLOB_RECURSE)`，配置
  期固化；或 `touch CMakeLists.txt` 触发）。
- 全量交叉编译 ~960 个翻译单元，-j$(nproc) 约 3-6 分钟。

### 2.1 五个 -D 开关的语义

| 开关 | 值 | 作用 |
| --- | --- | --- |
| `XPLATFORM_FBDEV_ON` | 1 | fbdev 直写显示驱动。**同时是 dbus/fontconfig 依赖探测的排除闸**：不带它，pkg-config 误抓宿主 x86_64 的 libdbus 传给 ARM 链接器 → `libdbus-1.so: file format not recognized` 链接炸 |
| `XPLATFORM_FBINPUT_ON` | 1 | fbdev 触摸输入（读 event0 + pointercal 校准） |
| `XGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16` | 1 | RGB565 屏的缓冲格式 |
| `XGUI_BACKINGSTORE_RENDER_MODE` | DIRECT | 脏区绘制 + 硬件翻页（pan+VBL 防撕裂）+ 差带同步。fbdev 下不设即默认 DIRECT，无需显式指定；**BUFFER_COUNT 保持默认 1（单缓冲映射硬件翻页），设 2 在 sun4i 全屏黑块（1026 二分实证）**。⚠ 见 §2.1.1 枚举开关铁律 |
| `XGUI_BACKINGSTORE_BUFFER_COUNT` | **不设（默认 1）** | 单缓冲映射硬件翻页（2026-10-26 屏上二分定案：设 2 在 sun4i 触发「静默吞翻页」全屏黑块回归；用户裁定口径=单缓冲） |

### 2.1.1 枚举开关铁律（RENDER_MODE 必须用枚举名，2026-10-26 根修）

`XGUI_BACKINGSTORE_RENDER_MODE` 是**三值枚举**（`PARTIAL=0` / `DIRECT=1` /
`FULL=2`，见 `Src/XGui/XGuiConfig.h`），**不是布尔量**。CMake 的裁剪开关
转发循环原本只认 `ON/TRUE/OFF/FALSE`，`DIRECT` 两侧都不匹配 ⇒ 原样透传为
编译定义 `XGUI_BACKINGSTORE_RENDER_MODE=DIRECT`。

**这个宏在 `#if` 比较里展开成未定义标识符，预处理器按 0 处理 ⇒ 与
`PARTIAL(0)` 相等 ⇒ 嵌入构建实际编出的是 PARTIAL（tile 攒批）而非
DIRECT！** 真机症状：后备缓冲按 `min(w,160)×min(h,80)` 分配，present 把
160×80 的图往 1024×600 面板直写 ⇒ 逐行错位 ⇒ 面板上出现**阶梯状黑块 /
黑条纹**（黑带 `|x−2y|≤320`，非黑区域与正确帧逐位一致），且**首帧正常、
随后黑块覆盖已渲染界面**。

已修（`CMakeLists.txt` 顶部）：渲染模式单独做枚举名 → 数值映射
（`PARTIAL`→0 / `DIRECT`→1 / `FULL`→2），并对未识别的标识符 **FATAL_ERROR
拒收**——宁可构建失败，也不要静默编出与意图相反的模式。

**自检纪律**：构建后核对编译命令行的实际定义（唯一一条且为数值）：

```bash
grep -o "XGUI_BACKINGSTORE_RENDER_MODE=[^ ]*" \
    /home/xinyue/armel-build/CMakeFiles/XinYueCS.dir/flags.make | sort -u
# 期望输出恰好一行：XGUI_BACKINGSTORE_RENDER_MODE=1
```

出现两行（如 `=1` 与 `=DIRECT` 并存）或值为 `=DIRECT` 即为未修复/回归。

### 2.2 -latomic（已固化 CMakeLists，此处记录因果）

32 位 ARM soft-float 的 64 位原子操作（`XAtomic_GCC.c` 的
`__atomic_store_8` 等）不在指令集内，须链 `libatomic`。两个坑：

1. **落点**：`link_libraries(atomic)` 会把 `-latomic` 放在
   `libXinYueCS.a` **之前**——静态库从左到右解析，放前面解析不到后置
   归档的符号，照样 undefined。正确做法是
   `target_link_libraries(${EXPORT_NAME}S PUBLIC atomic)`
   （消费端链接行自动置于库后）。**此修复已入库 CMakeLists**（仅
   `CMAKE_CROSSCOMPILING AND processor==arm` 时生效，桌面/安卓不受影响）。
2. 与坑 1 无关的同类报错（`__atomic_store_8` undefined）若出现，先查
   链接行里 -latomic 的位置再怀疑别的。

## 3. 部署流程

```bash
# 0) 屏上资源预检（根分区 ubi0 须有余量；上一次部署前 64% 用 / 36MB 可用）
ssh mcgs "df /root | tail -1"

# 1) 裁剪（6.8MB 级别，勿跳过——未裁剪 8MB+ 加剧分区与内存压力）
arm-linux-gnueabi-strip out/armel-out/XGuiWindowDemo_Test -o /tmp/xfdlg/XGuiWindowDemo_Test.stripped
md5sum /tmp/xfdlg/XGuiWindowDemo_Test.stripped        # 记录本地 md5

# 2) 管道推送 + 双端 md5 核对（先落 .new，核对后再原子换名）
ssh mcgs "cat > /root/xguidemo/XGuiWindowDemo_Test.new && md5sum /root/xguidemo/XGuiWindowDemo_Test.new" \
    < /tmp/xfdlg/XGuiWindowDemo_Test.stripped
ssh mcgs "mv /root/xguidemo/XGuiWindowDemo_Test.new /root/xguidemo/XGuiWindowDemo_Test && \
          chmod 700 /root/xguidemo/XGuiWindowDemo_Test && md5sum /root/xguidemo/XGuiWindowDemo_Test"

# 3) 启动（包装脚本自动处理与 MCGS 自启应用 demo_app 的帧缓冲交接）
ssh mcgs "nohup /root/xguidemo_run.sh >/dev/null 2>&1 & sleep 4; pidof XGuiWindowDemo_Test"

# 4) 稳态核验（进程存活 + md5 + 日志）
ssh mcgs "pidof XGuiWindowDemo_Test; md5sum /root/xguidemo/XGuiWindowDemo_Test; tail -3 /root/xguidemo/demo.log"
```

### 3.1 启动包装脚本语义（`/root/xguidemo_run.sh`）

- 运行期间对 MCGS 自启应用 **demo_app**（帧缓冲唯一所有者）做临时
  SIGSTOP，demo 退出（含崩溃）时 trap 自动 SIGCONT 恢复——**绝不
  kill、不改自启配置**。
- 前置检查：demo_app 不在运行 → 拒绝启动（帧缓冲无人释放）；已在运行
  → 打印 already running 幂等退出。
- 运行环境：`cd /root/xguidemo` + `LD_LIBRARY_PATH=/root/xguidemo/lib`
  + `XPLATFORM_FBINPUT_DEVICE=/dev/input/event0`，日志追加
  `demo.log`。

### 3.2 部署纪律（历史教训，逐条有事故档案）

- **根分区先 df**（ubi0 容量小）；推送目标一律 `/root`，**设备 /tmp
  是 27.4M tmpfs，铁禁部署**（写爆即 OOM 自杀）。
- **先 strip 后推送**；管道 cat + 双端 md5，不裸 scp 盲传。
- 设备**无 pgrep**：用 `ps | grep | awk '{print $1}'` + kill PID。
- 抓屏用屏上 **fbsnap**（可见缓冲），**禁裸 dd /dev/fb0**（只读得到
  不可见缓冲/陈旧面板）。
- 设备侧诊断输出走 **stderr**（stdout 块缓冲——「日志无记录」常是缓
  冲假象而非没收到）。
- 设备 /tmp 写入的抓帧/临时文件用完即删（tmpfs 计入内存，累计会引爆
  56MB 板 OOM）。
- 触摸注入用屏上现成 **xinj2 / rawxy**（内置 pointercal 逆矩阵；
  自研注入器缺释放沿曾污染整条渲染证据链）。
- 换部署前记录旧 md5 作回滚点。**部署后必须核验进程实际运行的二进制**
  （`md5sum /proc/PID/exe`），不能只看文件 md5 + 进程存在——启动包装脚本
  撞「已在运行」会幂等退出，旧实例继续跑旧 inode（/proc/exe 指向 deleted）。
- 现役版本：`c1968bdc5d41f4f2173e156f70d2fc8b`（2026-10-26 渲染模式根修后
  的纯净构建，md5 由隔离树 `git archive HEAD` + 仅 CMake 根修产出，不含
  并发开发中的半成品）。回滚点：`2f032d99e1f822560324547cf4f78d68`
  （含网络四文件增量的版本）。

## 4. 断线重连验证要点（本指南的触发战役）

屏上网络引擎为 epoll（见 §0）。2026-10-25/26 修复的 CONNECT 能力门
误判（网络级 ECONNREFUSED 被误锁成内核拒操作码 → 永久楔死在
Connecting）在屏上代码路径与桌面 epoll 一致。实机验证序列：

1. 触屏进 Modbus 主站页，连接 `192.168.1.117:502`（真实远程 IO，
   与屏同网段）。
2. 断开 117 网络供电/网线 ≥30 秒——**必须覆盖多次 connect 被拒**
   （旧 bug 正是首拒即楔死；短于一个重连周期测不出）。
3. 恢复网络，预期数十秒内自动恢复「已连接」+ 数据刷新，全程无手动操作。
4. 不符预期时抓 `/root/xguidemo/demo.log`（stderr 通道）与
   `XDEVNET_IO_DEBUG` 编译开关联查。

修复内容详见仓库根 `安卓Modbus主站断线重连失效排查录_20261025.md`
（含桌面双引擎 11 组矩阵回归记录）。

## 5. 快速排错索引

| 症状 | 根因 | 处置 |
| --- | --- | --- |
| `as: error while loading shared libraries: libbfd-2.41-armel.so` | 编译命令未 source env.sh | `. ~/xtool-armel/env.sh` 后重跑 |
| `libdbus-1.so: file format not recognized`（链接期） | 缺 `-DXPLATFORM_FBDEV_ON=1`，dbus 探测误抓宿主库 | 带 §2 全套 -D 重配 |
| `__atomic_store_8` undefined | -latomic 缺失或落在静态库前 | CMakeLists 已固化 PUBLIC 挂法；确认构建树重配过 |
| 部署后启动即退，日志无输出 | demo_app 不在运行（包装脚本拒启） | 先启 MCGS 自启应用或接受暂不启动 |
| 触摸无响应/坐标错位 | fbinput 设备/pointercal | 查 `XPLATFORM_FBINPUT_DEVICE` 与 /etc/pointercal |
| 界面闪烁或内容缺失 | 曾误判 DIRECT 缺陷（实为注入器缺释放沿） | 维持 DIRECT+2+差带同步定版，勿改 RENDER_MODE |
| **面板出现阶梯状黑块/黑条纹（首帧正常、随后覆盖已渲染界面）** | **RENDER_MODE 枚举开关被 CMake 透传成未定义标识符 ⇒ 实际编出 PARTIAL**（见 §2.1.1） | 核对编译定义唯一且为 `=1`；重配 cmake 后重建 |
| 编译期报 `XGUI_BACKINGSTORE_RENDER_MODE 取值非法` | 新增的枚举值校验生效（有意拒收） | 只接受 `PARTIAL`/`DIRECT`/`FULL` 或数值 0/1/2 |

## 6. 与 RK3568 指南（docs/cross-compile-rk3568.md）的差异速查

| 项 | 昆仑通态 mcgs（本文） | RK3568（hd-rk3568） |
| --- | --- | --- |
| 架构 | armel（gnueabi soft-float，**须 -latomic**） | aarch64 |
| 交叉链 | ~/xtool-armel（gcc 12.3，glibc 2.24） | WSL aarch64-linux-gnu-gcc 15 + 板 sysroot |
| 显示 | fbdev 直写（无窗口系统） | X11 + Xfce |
| 网络引擎 | epoll（内核 5.2.9 版本门） | 按内核版本门 |
| glibc 兼容 | 交叉 2.24 ≤ 设备 2.26 | 须绕 2.42→2.31 差距（sysroot） |
