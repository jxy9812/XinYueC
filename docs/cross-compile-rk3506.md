# RK3506 板（rk3506-buildroot）armhf 交叉编译、部署与开机自启指南

> 2026-10-29 打通部署与自启闭环（交叉编译链路 2026-10-25 首次跑通）。
> 目标：Windows/Git Bash 一键交叉编译 XinYueC 的 armv7 产物，经 adb 部署到
> RK3506 Buildroot 板并配置开机自启，替代出厂 LVGL 演示 `rk_demo`。
> 一键脚本：`Tools/posix/cross-compile-rk3506.sh`；本文记录完整机制、
> 踩坑与手工命令序列。

## 0. 目标设备画像

| 项 | 值 |
| --- | --- |
| 主机名 / SoC | rk3506-buildroot / RK3506（3×Cortex-A7，armv7l，104MB 内存） |
| 系统 | Buildroot linux-6.1-stan-rkr4.2（kernel 6.1.99），glibc **2.38** |
| init | BusyBox init（**无 systemd**）：`inittab → rcS.early → rcS` |
| 根分区 | `/dev/ubiblock0_0` 挂 `/` 为 **squashfs 只读**（38.5M 全满，不可写不可删） |
| 可写分区 | `/oem`（ubifs，38M）、`/userdata`（ubifs，111M）；`/data -> userdata` 软链 |
| 显示 | 无 `/dev/fb0`，DRM `/dev/dri/card0`（KMS dumb 直写，800x480 RGB565 pitch=1600） |
| 触摸 | `/dev/input/event0` Goodix 电容屏（MT 协议 B，8 槽） |
| adb 序列号 | `94b6bbcaebf928a9`（USB；adb 在 `D:\Program Files\Android\SDK\platform-tools\`，不在 PATH） |
| 出厂自启 GUI | `/etc/init.d/pre_init/S99lv_demo` 启动 `/usr/bin/rk_demo`（LVGL 演示） |
| 用户定制钩子 | `S99appinit → sh /oem/RkLunch.sh`（rcS 收尾阶段，唯一可写开机钩子） |
| 时钟 | 无 RTC 电池，重启后时间回 2021-01-01（顶栏时钟显示异常非程序问题） |

## 1. 总路线

```
Windows 仓库 (D:\code\CMake\XinYueC)
  └─ git archive HEAD → WSL Ubuntu-22.04 (/root/xinyuec-src)
       └─ arm-linux-gnueabihf-gcc (gcc-11) + 仓库自带工具链文件
            （Drive/Posix/arm_none_linux_gnueabihf.toolchain.cmake，Cortex-A7/NEON）
            └─ 产物 /root/out-rk/XGuiWindowDemo_Test（GLIBC 上限实测 2.34）
                 ├─ strip → adb push → 板子 /userdata/gui/XGuiWindowDemo
                 └─ 首次部署: 生成 /oem/RkLunch.sh 自启脚本（kill rk_demo + 拉起 demo）
```

关键结论先行：

- **WSL 的 armhf 交叉 glibc（2.35）低于板子 2.38**，与 RK3568 那套"必须做板子
  sysroot"的麻烦（见 docs/cross-compile-rk3568.md §4 坑③~⑧）在本板**全部不存在**，
  apt 装 `gcc-arm-linux-gnueabihf` 直接可用；
- **根分区 squashfs 只读** ⇒ "删掉自启 GUI"物理不可行，只能走 `/oem/RkLunch.sh`
  在启动收尾时 `killall rk_demo` 再拉起自己的程序（启动过程出厂界面会闪现数秒，
  不重刷镜像无法避免）；
- 产物部署在 `/userdata`（可写、111M），**必须保持 `gui/` 与 `Library/` 兄弟目录**
  （demo 按可执行文件同级的 `../Library/XFont/XFontOutlineCommon` 探测字体）。

## 2. 环境准备（一次性）

```bash
# WSL2 发行版名固定为 Ubuntu-22.04（脚本默认值，可用环境变量 WSL_DISTRO 覆盖）
wsl -d Ubuntu-22.04 -u root -- bash -c \
  'apt-get install -y gcc-arm-linux-gnueabihf cmake ninja-build'
# 验证
wsl -d Ubuntu-22.04 -u root -- arm-linux-gnueabihf-gcc --version | head -1   # gcc-11

# Windows 侧 adb：D:\Program Files\Android\SDK\platform-tools\adb.exe
# （脚本默认此路径，可用环境变量 RK3506_ADB 覆盖；序列号 RK3506_SERIAL 覆盖）
```

## 3. 交叉编译（手工流程备查，日常用一键脚本 §6）

```bash
# —— 同步源码：必须 git archive，禁 Git Bash tar 直读 DrvFs（静默丢文件，坑见 §7）——
cd /d/code/CMake/XinYueC
git archive --format=tar HEAD | wsl -d Ubuntu-22.04 -u root -- bash -c \
  'rm -rf /root/xinyuec-src && mkdir -p /root/xinyuec-src && tar xf - -C /root/xinyuec-src'
# 校验：解出文件数 == git 跟踪数（注意假阳性，见 §7 坑③）
git ls-files | wc -l
wsl -d Ubuntu-22.04 -u root -- bash -c 'find /root/xinyuec-src -type f | wc -l'
# gitignore 的本地配置单独补（缺了 cmake 配置期即挂）
wsl -d Ubuntu-22.04 -u root -- bash -c \
  'cp /mnt/d/code/CMake/XinYueC/CMake_Config.cmake /root/xinyuec-src/'

# —— 编译（开关与 2026-10-25 首次跑通完全一致）——
wsl -d Ubuntu-22.04 -u root -- bash -c '
cd /root/xinyuec-src && rm -rf build-rk out-rk && cmake -B build-rk -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=Drive/Posix/arm_none_linux_gnueabihf.toolchain.cmake \
  -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=/root/out-rk \
  -DXPLATFORM_FBDEV_ON=ON -DXPLATFORM_DRM_ON=ON -DXPLATFORM_FBINPUT_ON=ON \
  -DXGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16=ON \
  -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build-rk --target XGuiWindowDemo_Test'

# —— GLIBC 红线验收：必须 ≤ 2.38（板子 glibc；gcc-11 交叉链构造上限 2.35）——
wsl -d Ubuntu-22.04 -u root -- bash -c \
  'arm-linux-gnueabihf-readelf --dyn-syms -W /root/out-rk/XGuiWindowDemo_Test \
   | grep -oE "GLIBC_[0-9.]+" | sort -uV | tail -1'    # 实测 2.34

# —— strip（板上无调试需求；未 strip 7.0M，strip 后 5.4M）——
wsl -d Ubuntu-22.04 -u root -- bash -c \
  'cp /root/out-rk/XGuiWindowDemo_Test /root/out-rk/xyd \
   && arm-linux-gnueabihf-strip /root/out-rk/xyd'
```

工具链文件 `Drive/Posix/arm_none_linux_gnueabihf.toolchain.cmake` 说明：

- 默认 `-mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard`，对位 RK3506；
- 编译器定位顺序：`-DXARM_LINUX_GNUEABIHF_ROOT` 显式根 → PATH（WSL apt 装的
  `arm-linux-gnueabihf-gcc` 即被此项命中）→ 常见解压位置；
- `CMAKE_FIND_ROOT_PATH_MODE_* = ONLY` 把库/头文件圈在工具链目标根内；
- **交叉产物严禁写仓库 `bin/`**（CMakeLists 配置期守卫直接报错），必须用
  `-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=` 指到私有目录（本流程指 `/root/out-rk`）。

## 4. 板上部署布局

```
/userdata/                      # ubifs 可写 111M（= /data）
├── gui/
│   └── XGuiWindowDemo          # armhf 产物（strip 后 ~5.4M）
├── Library/
│   └── XFont/
│       └── XFontOutlineCommon.otf   # demo 字体（已预置）
└── cfg/                        # demo 配置持久化目录（页面状态等）
```

- **`gui/` 与 `Library/` 必须是兄弟目录**：demo 按 `<exeDir>/../Library/XFont/...`
  绝对路径探测字体；字体缺失时启动仍会成功但 UI 无字。
- 部署动作（先杀旧实例再覆盖，避免 text file busy）：

```bash
ADB="/d/Program Files/Android/SDK/platform-tools/adb.exe"
MSYS_NO_PATHCONV=1 "$ADB" -s 94b6bbcaebf928a9 shell 'killall XGuiWindowDemo 2>/dev/null; sleep 1'
wsl -d Ubuntu-22.04 -u root -- bash -c 'cp /root/out-rk/xyd /mnt/d/code/CMake/XinYueC/build-rk3506/xyd_new'
cd /d/code/CMake/XinYueC/build-rk3506
"$ADB" -s 94b6bbcaebf928a9 push xyd_new /data/gui/XGuiWindowDemo.new   # 注意 §7 坑②
MSYS_NO_PATHCONV=1 "$ADB" -s 94b6bbcaebf928a9 shell \
  'mv /data/gui/XGuiWindowDemo.new /data/gui/XGuiWindowDemo && chmod +x /data/gui/XGuiWindowDemo'
# md5 双侧比对（本地 md5sum xyd_new vs 设备 md5sum /data/gui/XGuiWindowDemo）
```

## 5. 开机自启机制（squashfs 约束下的唯一解）

启动时序与改造点：

```
inittab
├─ ::sysinit:/etc/init.d/rcS.early          # 遍历 pre_init/S??*（squashfs，改不了）
│    └─ pre_init/S99lv_demo → rk_demo &     # 出厂 LVGL 演示，开机即上屏（闪现数秒）
└─ ::sysinit:/etc/init.d/rcS                # 遍历 init.d/S00..S99
     └─ S99appinit → sh /oem/RkLunch.sh     # ★ 唯一可写开机钩子，本方案在此接管
```

`/oem/RkLunch.sh` 全文（原厂内容只有一行 echo，已备份为 `/oem/RkLunch.sh.bak`）：

```sh
#!/bin/sh
echo "The user's boot script is executed here"

# ---- XGuiDemo 开机自启（2026-10-29） ----
# 出厂演示 GUI rk_demo 由 pre_init/S99lv_demo 在 rcS.early 阶段拉起，
# 根分区为 squashfs 只读镜像无法删除脚本，这里在启动收尾阶段结束它，
# 并拉起 XGuiWindowDemo（可写 userdata 分区）。
killall rk_demo 2>/dev/null
killall rk_demo_test 2>/dev/null

if [ -x /userdata/gui/XGuiWindowDemo ]; then
    (cd /userdata/gui && ./XGuiWindowDemo > /tmp/xguidemo.log 2>&1 &)
fi
```

要点：

- `killall rk_demo`：rk_demo 在 inittab 里**没有 respawn**，杀掉即不会复生；
  `rk_demo_test` 一并杀（S99lv_demo 有 SD 卡分支会起它）；
- `cd /userdata/gui` 后再启动：码表按进程 cwd 相对路径加载、字体按 exe 兄弟
  目录探测，统一锚在 gui/ 下；
- 日志固定 `/tmp/xguidemo.log`（tmpfs，重启即清，适合排障不留垃圾）；
- 手工改 RkLunch.sh 必须 **LF 行尾**（CRLF 会让 busybox sh 解析出错），push 后
  用 `grep -q $'\r'` 验一道；`S99appinit` 用 `sh` 调它，无需可执行位。

## 6. 一键脚本（日常入口）

```bash
Tools/posix/cross-compile-rk3506.sh                    # 同步源码→WSL→编译→GLIBC 验收
Tools/posix/cross-compile-rk3506.sh --deploy           # 编译后自动部署+杀旧起新+自启检查
Tools/posix/cross-compile-rk3506.sh --deploy --reboot  # 部署后整机重启，冷启动验证自启
Tools/posix/cross-compile-rk3506.sh --deploy --shot    # 部署后抓屏转 PNG（fb_last.png）
Tools/posix/cross-compile-rk3506.sh --target XObjectTest --deploy
Tools/posix/cross-compile-rk3506.sh --worktree --deploy  # 叠加工作区未提交改动
```

脚本固化本文结论：git archive 同步（含 CMake_Config.cmake 补拷与文件数校验）→
Ninja 交叉编译 → GLIBC ≤ 2.38 红线 → strip → adb 部署（MD5 双侧比对）→
RkLunch.sh 幂等安装（带标记检测，首次安装自动备份原文件）→ 拉起并验活；
adb 调用全线 `MSYS_NO_PATHCONV=1`（坑②）。

## 7. 踩坑实录（问题 → 根因 → 解法）

### 坑①：根分区 squashfs 只读，自启脚本删不掉
- 现象：`rm /etc/init.d/pre_init/S99lv_demo` 报 `Read-only file system`，
  `mount -o remount,rw /` 无效（squashfs 无 rw 语义）。
- 根因：出厂镜像以 squashfs 挂 `/`，任何对根分区的修改（含删除）都不可能。
- 解法：用户态唯一开机钩子 `/oem/RkLunch.sh`（S99appinit 调）里 `killall rk_demo`。
  inittab 未给 rk_demo 配 respawn，杀掉不复生。代价：开机时出厂界面闪现数秒。

### 坑②：Git Bash 调 adb push/pull，远端路径被 MSYS 改写（本案踩两次）
- 现象：`adb push xyd_new /data/gui/XGuiWindowDemo.new` 报
  `failed to copy ... to 'C:/Program Files/Git/data/gui/...'`；
  `adb pull /tmp/fb.raw` 报 `failed to stat remote object 'C:/Users/.../Temp/fb.raw'`。
- 根因：MSYS 对「看起来像 POSIX 绝对路径的独立 argv token」做路径改写；
  adb 的 push/pull 恰好把远端路径放在独立 token 里。引号救不了（不是引号问题）。
- 解法：每条带远端 POSIX 路径的 adb 命令前缀 `MSYS_NO_PATHCONV=1`。
  `adb shell '整串命令'` 因 token 以 `shell 子命令` 开头不被改写，可不加。

### 坑③：同步校验的文件数对不上是假阳性，别被吓住
- 现象：`git archive | tar tf - | wc -l`(4153+目录条目) ≠ `git ls-files | wc -l`(3658)。
- 根因有三，全是"清单比对"侧的显示问题，不是真丢文件：
  1. tar 清单含目录条目（`Drive/` 等 ~495 行），`grep -v '/$'` 在行尾混入 `\r` 时
     过滤失效（MSYS 管道文本转换）；
  2. `git ls-files` 默认 `core.quotepath=on`，中文文件名显示为八进制转义
     （`"Src/.../\350\256\276..."`），与 tar 清单里的原生 UTF-8 名字比对必然 mismatch；
  3. MSYS 的 `tr -d '\r'`、`sort` 等文本工具链经管道时行为受文本模式影响。
- 解法（脚本采用）：**以 WSL 侧落地事实为准**——`find $WSL_SRC -type f | wc -l`
  必须等于 `git ls-files | wc -l`（本板实测 3658 == 3659-1 个补拷的
  CMake_Config.cmake），不做 tar 清单级比对。

### 坑④：RkLunch.sh 行尾必须是 LF
- 现象：Windows 侧生成脚本若走 CRLF，busybox sh 逐行解析尾随 `\r`，
  轻则变量值带 `\r`，重则 `if/fi` 配对失败。
- 解法：用 bash heredoc/`printf` 生成（天然 LF），push 后
  `grep -q $'\r' /oem/RkLunch.sh && echo HAS_CR` 验一道。

### 坑⑤：drm_dump 把裸帧打到 stdout
- 现象：直接 `adb shell drm_dump` 输出 750KB 乱码刷屏（800x480x2B=768000B）。
- 解法：设备侧重定向 `/userdata/gui/drm_dump > /tmp/fb.raw 2>/dev/null`，adb pull
  回来后转 PNG（`python raw2png.py <raw> 800 480 16 <png>`，转换器在
  gitignore 的 build-rk3506/raw2png.py，一键脚本 --shot 已内联同逻辑）。

### 坑⑥（沿承 RK3568 文档）：源码同步必须 git archive
- Git Bash tar 直读 DrvFs 按 inode 归并成员，个别文件（如 mbedtls build_info.h）
  静默丢失且退出码 0；git archive 从对象库出流，无此问题。
  同理：CMake_Config.cmake 是 gitignore 本地文件，archive 不含，需单独补拷。

### 坑⑦：wsl.exe 的 argv 路径改写与 stdin 吸收（沿承，脚本已规避）
- 独立 argv 传 `/root/...` 会被 MSYS 改写成 `D:/Program Files/Git/root/...`：
  WSL 侧路径一律写死在带引号 heredoc / `bash -c '...'` 字符串内；
- `wsl ... bash -s <<EOF` 时 ssh 等会吸 stdin：跨 wsl 传参用 `bash -s "$ARG"`
  位置参数，不用 stdin 传数据。

## 8. 验收红线与运行判据

| 红线 | 判据 |
| --- | --- |
| GLIBC 版本 | readelf 取符号版本最大值 ≤ **2.38**（gcc-11 交叉链构造上限 2.35，实测 2.34） |
| 同步完整性 | WSL 侧 `find -type f | wc -l` == `git ls-files | wc -l` |
| 部署完整性 | 本地/板上 md5 一致 |
| 自启安装 | `/oem/RkLunch.sh` 含标记行且无 `\r`；重启后 `ps` 无 rk_demo、有 XGuiWindowDemo |

启动日志正常判据（`/tmp/xguidemo.log`）：

```
[0.118] phase: app created
XGuiWindowDemo: DRM 显示驱动已注册（KMS dumb 直写）       # 必须是 DRM 而非 fbdev
[fbinput] 触摸输入已接入：/dev/input/event0（Goodix ... MT 协议 B）
XGuiWindowDemo: switch page=0 (按钮演示)
[1.689] phase: postShow done
XGuiWindowDemo: 屏幕尺寸=800x480
[0.439] phase: first full paint done
```

已知无害告警：`fbdev 显示驱动不可用`（本来就没 /dev/fb0）、
`cacheSync reported NOT-synced`（缓存一致内存上无影响，日志自带说明）、
顶栏时钟显示 2021-01-01（板子无 RTC 电池，重启回出厂时间）。

## 9. 快速排障速查

| 症状 | 指向 |
| --- | --- |
| `Read-only file system`（改 /etc 下任何文件） | 坑①，squashfs 只读，走 /oem/RkLunch.sh |
| adb push/pull 目标变 `C:/Program Files/Git/...` | 坑②，`MSYS_NO_PATHCONV=1` |
| 同步文件数对不上 | 坑③，以 WSL 侧 find 为准，比对侧全是显示假阳性 |
| 板上程序起不来 / 无字 | §4 布局（gui/ 与 Library/ 必须兄弟）+ `cat /tmp/xguidemo.log` |
| 开机仍是出厂演示界面 | RkLunch.sh 被 `--` 开头内容破坏 / 无执行到（查 S99appinit）、或 killall 名字不对 |
| `GLIBC_2.xx not found`（板上运行时） | 工具链被宿主库污染，重验 §3 红线（本板 gcc-11 链路尚未复现过） |
| cmake 配置期找不到 CMake_Config.cmake | 坑⑥，补拷 gitignore 的本地配置 |
| 黑屏但进程在 | 抓屏确认：`drm_dump > /tmp/fb.raw` → pull → raw2png；仍黑查 DRM conn 状态 |
