# Android 平台模板

XGui 在 Android 上的移植层。产物是一个 **NativeActivity** 的 APK：没有 Java
业务代码，整个 UI 由 C 写的 XGui 渲染，`libxguidemo.so` 就是应用本体。

> 唯一的 Java 是**框架平台层**的 JNI 桥（无障碍虚拟视图树宿主 + WiFi/网络
> 服务桥），不在本目录，见下表。

## 一、这个目录里有什么，为什么这么少

```
platforms/android/
├── CMakeLists.txt        CMake 编排层（lib / apk / install-apk 目标）
├── CMakePresets.json     预设（x86_64 / arm64-v8a）
├── build_apk.sh          APK 打包（bash，Linux / MSYS / Git Bash）
├── pack_apk.ps1          APK 打包（PowerShell，Windows 原生，无需 bash）
├── AndroidManifest.xml   NativeActivity 清单（应用 id + 平台层要求的权限/Activity）
├── res/                  资源（应用名等）
└── out/                  产物目录（APK / keystore / work，已忽略）
```

**只有打包层**。Android 的源码分几处，都不在本目录：

| 内容 | 位置 | 为什么不在模板里 |
|---|---|---|
| 平台驱动（窗口/输入/系统） | `Drive/Android/Graphics/XPlatformWindowAndroid.c`、`Drive/Android/Core/XSystemAndroid.c` | 属于**框架平台层**，被根 CMake 的 `Drive/*.c` 全局 glob 收进库 |
| NativeActivity 壳与生命周期 | `Drive/Android/android_main.c` | 同上；且它在 `ANDROID` 下被显式从框架库剔除、只编进 `libxguidemo.so` |
| Java JNI 桥（无障碍/WiFi/网络） | `Drive/Android/java/com/*.java` | 同上，且属于**框架的平台 API**（不是本 app 的业务代码）——2026-10-24 从 `platforms/android/java/` 迁到平台层，包名从 `com.xinyuec.xguidemo` 改为 `com.xinyuec.android` |
| demo UI | `Test/XGuiDemo/**` | 与桌面 demo 共用同一份源码 |

如果把这些复制进 `platforms/android/`，会立刻出现两份副本。改一处忘另一处，
就会出现"桌面好了安卓坏"这类极难定位的问题。模板层只装平台独有、别处没有的东西。

> **清单里的 Activity 类名现在归平台层**：`android:name="com.xinyuec.android.XGuiNativeActivity"`
> （在 `Drive/Android/java/` 下）。Java 包与**清单包**是两个概念——清单包仍是
> `com.xinyuec.xguidemo`（应用 id，决定数据目录与 pm grant），所以启动命令是
> `am start -n com.xinyuec.xguidemo/com.xinyuec.android.XGuiNativeActivity`：
> 左边清单包、右边完整类名，**不能写成 `.XGuiNativeActivity` 简写**。

### 包名只有一个定义点

Java **没有预处理器**，`package com.xinyuec.android;` 那行只能是字面量，
没法用宏。但 C 侧可以，所以 JNI 查类的类全名全部由一个宏拼出来：

```c
/* Drive/Android/Core/XAndroidJavaPackage.h */
#define XANDROID_JAVA_PKG       "com.xinyuec.android"
#define XANDROID_JAVA_PKG_SLASH "com/xinyuec/android"
#define XANDROID_JAVA_CLASS(name)      XANDROID_JAVA_PKG "." name        /* loadClass 用 */
#define XANDROID_JAVA_JNI_CLASS(name)  XANDROID_JAVA_PKG_SLASH "/" name  /* FindClass 用 */
```

JNI 侧 9 处查类（`XSystemAndroid.c` 8 处 + 无障碍桥 1 处）都走这两个宏，
改包名时 C 侧只改这一个头文件。

因为 Java 的 `package` 行没法跟着宏变，两边的一致性靠**打包期门禁**锁死
（`build_apk.sh` / `pack_apk.ps1` 的 "manifest <-> platform Activity drift check"）：

```
C 宏 XANDROID_JAVA_PKG  ==  Java 的 package 声明  ==  清单的 activity 名
```

三者任一不同，打包**直接失败**（不产出 APK）——否则就是运行期
`ClassNotFoundException`，在真机上极难定位。门禁两侧都做过反向验证：
注入漂移后两条打包路径都以非 0 退出码拦下。

## 二、构建全流程

分两步：**先用根 CMake 产出 `.so`**，**再用本目录脚本打成 APK**。

### 2.0 推荐：用 CMake 编排层一键完成

`platforms/android/CMakeLists.txt` 把"配置根工程 + 编译 xguidemo + 打包 +
安装"串成 targets，不需要手动敲两条命令：

```bat
:: 配置（只需一次）
cmake -S platforms/android -B build_android -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="C:\Program Files\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

:: 编译 + 打包
cmake --build build_android --target apk

:: 编译 + 打包 + 安装 + 启动（一条龙）
cmake --build build_android --target install-apk
```

可用参数：

| 变量 | 默认 | 说明 |
|---|---|---|
| `ANDROID_ABI_NAME` | `x86_64` | `x86_64` 或 `arm64-v8a` |
| `ANDROID_API_LEVEL` | `24` | minSdkVersion |
| `ANDROID_NDK_ROOT` | `D:/android-ndk/android-ndk-r29` | NDK 位置 |
| `ANDROID_SERIAL` | `127.0.0.1:5555` | adb 目标设备；多设备时必须指定，留空让 adb 自选 |

> 这个 CMakeLists **不**自己编译源码——它用 `add_custom_target` 调
> `cmake -S <仓库根> -B out/android-<abi>` 再 build `xguidemo` 目标。
> 因为根工程的 CMake 缓存是桌面/MSVC 的，必须另起一个构建目录给 NDK。

下面的 2.1–2.3 是手动等价流程，排查问题时更有用。

### 2.1 前置依赖

| 组件 | 本机实测路径 | 说明 |
|---|---|---|
| Android SDK | `C:\Users\jxy\AppData\Local\Android\Sdk` | 需 `build-tools/36.0.0`、`platforms/android-37.0` |
| Android NDK | `D:\android-ndk\android-ndk-r29` | 用其 `android.toolchain.cmake` |
| JDK | `C:\Program Files\Android\Android Studio\jbr` | `keytool` / `apksigner` 需要 |
| CMake + Ninja | VS 18 自带 | `cmake 4.3.1` / `ninja 1.13.2` |

两者都可用环境变量覆盖：`ANDROID_SDK`、`ANDROID_NDK`、`JAVA_HOME`。
脚本里的默认值就是本机路径，换机器时设环境变量即可，不必改脚本。

### 2.2 第一步：编译原生库

```bat
cmake -S . -B out/android-x86_64 -G Ninja ^
  -DCMAKE_TOOLCHAIN_FILE=D:/android-ndk/android-ndk-r29/build/cmake/android.toolchain.cmake ^
  -DANDROID_ABI=x86_64 -DANDROID_PLATFORM=android-24
cmake --build out/android-x86_64 --target xguidemo
```

产物：`out/android-x86_64/libxguidemo.so`（约 41 MB，未 strip）。

> **ABI 命名陷阱**：CMake 的 `ANDROID_ABI` 是 `arm64-v8a`，但 Apple/CMake 的
> 构建目录习惯写 `arm64`。`build_apk.sh` 里已经做了映射（`arm64` 目录 →
> `arm64-v8a` APK 目录），别把两边混用。

> **并行构建警告**：同一个 `out/android-*` 目录不能同时跑两个 ninja，
> 否则报 `ninja: error: failed recompaction: Permission denied`。
> 桌面构建与安卓构建要串行。

### 2.3 第二步：打包 APK

Windows 上**推荐用 PowerShell 版**（不依赖 bash）：

```powershell
powershell -ExecutionPolicy Bypass -File platforms\android\pack_apk.ps1
```

有 bash 的环境（Linux / MSYS / Git Bash）：

```bash
bash platforms/android/build_apk.sh both      # x86_64 | arm64-v8a | both
```

产物：`platforms/android/out/XGuiDemo-debug.apk`

打包管线做的事（顺序不能变）：

```
javac --release 8  Drive/Android/java/com/*.java  → classes/
d8 --min-api 26   classes/*.class                               → classes.dex
aapt2 compile --dir res          → res.zip
aapt2 link --manifest ...        → base.apk（含 AndroidManifest.xml + resources.arsc）
aapt add lib/<abi>/libxguidemo.so
aapt add assets/fonts/XFontOutlineCommon.otf
aapt add classes.dex
zipalign -f 4
apksigner sign --ks ... --out XGuiDemo-debug.apk
```

> **classes.dex 不能省**：APK 里的 Activity 是 Java 子类
> （`com.xinyuec.android.XGuiNativeActivity`），没有 classes.dex 就没有这个类，
> 启动直接 `ClassNotFoundException` / "Activity class does not exist"。
> Java 源码在**平台层** `Drive/Android/java/com/`，两条打包路径
> （`build_apk.sh` / `pack_apk.ps1`）都从那里编译，保证产物一致。

### 2.4 安装与启动

```bat
adb connect 127.0.0.1:5555
adb install -r platforms\android\out\XGuiDemo-debug.apk
adb shell am start -n com.xinyuec.xguidemo/com.xinyuec.android.XGuiNativeActivity
adb logcat -s XGuiDemo
```

> 组件名是 **清单包 + 完整类名**（清单包 `com.xinyuec.xguidemo` ≠ Java 包
> `com.xinyuec.android`）。写成 `com.xinyuec.xguidemo/.XGuiNativeActivity`
> 会报 "Activity class does not exist"——简写只在类名与清单包同前缀时成立。

启动日志应出现：

```
I XGuiDemo: font: released /data/data/com.xinyuec.xguidemo/files/fonts/XFontOutlineCommon.otf (3511576 bytes)
```

### 2.5 签名密钥（`signing/xgui.keystore`）

密钥**不在 `out/` 下**，而在 `platforms/android/signing/`。原因是 `out/` 是
构建产物目录、会被整体清理；密钥在那里被重新生成时签名就变了，覆盖安装
要么失败（`INSTALL_FAILED_UPDATE_INCOMPATIBLE`），要么被系统当成"换签名
重装"而**重置全部运行时权限**。

真机实证（2026-10-25）：目录从 `Tools/android/` 迁到 `platforms/android/`
后，密钥在 `out/` 重新生成，重装把 `ACCESS_FINE_LOCATION` 打回未授予，
WiFi 扫描从此**静默返回空表**——状态机照常走完"扫描中 → 扫描完成"，只有
网络下拉框永远是空的，看起来就像"扫描功能失效"。

> **不要删 `signing/xgui.keystore`**。删掉后重新生成的密钥会触发同样的
> 权限重置；届时重装后需要在界面里重新授权一次（应用启动时会自动弹窗，
> 见坑 8）。

## 三、十个必须知道的坑

这些是实机踩出来的，每条都有日志/截图实证。

### 坑 1：`aapt2 link -A` 在 Windows 上写出反斜杠路径

Windows 版 `aapt2` 的 `-A <dir>` 会把 zip 条目名写成
`assets\fonts\XFontOutlineCommon.otf`（反斜杠），而 `AAssetManager` 按 POSIX
路径查找，永远命中不了。

**症状**：真机日志 `font: asset fonts/XFontOutlineCommon.otf not found in APK`，
中文整片缺失。

**修法**：不用 `-A`，改用 `aapt add -f base.apk assets/fonts/XFontOutlineCommon.otf`
显式写入正斜杠路径。脚本已按此实现。

### 坑 2：`aapt add` 会抹掉清单

在 `aapt2 link` **之前**对同一个 APK 执行 `aapt add`，会把
`AndroidManifest.xml` / `resources.arsc` 丢掉，随后 `apksigner` 报
`Missing AndroidManifest.xml`。

**修法**：严格按 `aapt2 link` → `aapt add` → `zipalign` → `apksigner` 的顺序。
脚本已按此实现。

### 坑 3：字体释放把整个文件读进 8KB 栈缓冲

`android_main.c` 首次启动时要把 APK assets 里的字库释放到应用私有目录。
早期实现把「剩余总长」直接当作 `AAsset_read` 的读入量传给 8KB 栈缓冲，
3.5 MB 的字库直接踩爆栈。

**症状**：tombstone `SIGSEGV in memcpy ← StreamingZipInflater::read ←
AAsset_read ← ANativeActivity_onCreate`，启动即崩。

**修法**：循环读，每次读入量夹在 `sizeof(chunk)` 内，长度未知时按 EOF 收敛。
已修复。

### 坑 4：释放目录 ≠ 字体查找目录

框架的 FT 轮廓字按编译期宏 `XFONT_EXTERNAL_FT_FONT_DIR` 找
`<dir>/<family>.{ttf,otf,ttc}`。这个目录在安卓上必须指向**应用私有目录**
（`/data/data/<pkg>/files/fonts`），因为 APK 内的 assets 不能被 FreeType
按普通文件打开。

**症状**：如果释放到 `/data/user/0/...` 而查找用 `/data/data/...`，两者虽
通常同 inode，但一旦不一致就表现为"主界面中文正常、导航栏乱码"这类局部异常。

**修法**：两处**共用同一个宏**——`CMakeLists.txt` 的安卓分支注入
`XFONT_EXTERNAL_FT_FONT_DIR`，`android_main.c` 的释放目标也读它。
不要在两处各写一遍字符串。

### 坑 5：找不到字体时静默回退，中文全丢

FT 探测失败**不会报错**，只会往 stderr 打一行警告，然后回退到内建位图
provider `XFont8x16`。那个 provider 只有 **96 个汉字**（是当年按 demo 界面
文案挑的子集），而且完全没有中文标点。

**症状**：UI 上中文整片空白/方块，但看日志只有一行不起眼的
`FT outline font resolve failed`。安卓上 stderr 还进 `/dev/null`，连这行都看不到。

**排查**：先查 `adb shell ls -l /data/data/com.xinyuec.xguidemo/files/fonts/`
有没有字库文件，再看 logcat 的 `XGuiDemo` tag。

### 坑 6：PowerShell 5.1 把无 BOM 的 UTF-8 脚本按 GBK 解析

`pack_apk.ps1` 里加中文注释后，脚本突然报
`表达式或语句中包含意外的标记"}"`——语法错误指向一个完全正确的括号。

**根因**：Windows PowerShell 5.1 在**没有 BOM** 时按系统 ANSI 代码页（中文
Windows 是 GBK/936）读取 `.ps1`，UTF-8 的中文字节被误当成双字节 GBK 字符，
吃掉了后续的引号/括号，整段语法崩掉。

**修法**：所有含非 ASCII 字符的 `.ps1` 必须存成 **UTF-8 with BOM**。
`.gitattributes` 不控制 BOM，这条只能靠约定（或用 PowerShell 7+，它默认 UTF-8）。

**排查**：`powershell -File x.ps1` 报的语法错误位置看起来毫无道理时，
先用十六进制看文件头三个字节是不是 `EF BB BF`。

### 坑 7：特征宏是**空定义**时，不能拿来做算术

`XNETWORK_USE_PLATFORM_API` / `XNETWORK_USE_LWIP` 在 `XNetwork_config.h` 里是
**空定义**（`#define XNETWORK_USE_PLATFORM_API`，没有值），只表示"开"。

**症状**：`#if defined(__ANDROID__) && XNETWORK_USE_PLATFORM_API` 报
`error: expected value in expression`（错误点在行尾，因为宏展开后是 `1 && `）。

**修法**：用 `defined()` 判定，不要把这类宏当 0/1 参与运算——
写成 `#if defined(__ANDROID__) && defined(XNETWORK_USE_PLATFORM_API)`。
仓库里其它特征宏（`XGUI_ON` 等）都是 `0`/`1`，两种写法都成立；
**只有这几个网络后端开关是空定义**，别把它们和别的宏混着写。

### 坑 8：WiFi 扫描"完成"但下拉框空 = 定位权限没给

安卓自 API 26 起把 WiFi 扫描结果算作**位置数据**：没有
`ACCESS_FINE_LOCATION` 时 `WifiManager.getScanResults()` **不抛异常，
只静默返回空表**。

**症状**：点「扫描」后状态行正常走「扫描中 → 扫描完成」，但「网络」下拉框
一直是空的。看起来完全像扫描功能坏了，实际扫描本身没问题。

**定位**：系统侧对照即可区分——

```bat
adb shell cmd wifi list-scan-results   :: 系统能看到一堆 AP
adb shell dumpsys package com.xinyuec.xguidemo | findstr ACCESS_FINE_LOCATION
adb shell cmd appops get com.xinyuec.xguidemo
```

若后者是 `granted=false` / appops `ignore`，就是权限问题。

**两个容易踩的点**：

1. 清单里的 `<uses-permission>` 只是 install-time 声明，**运行时权限必须
   由应用在界面里申请**。本应用已在 `XGuiNativeActivity.onCreate` 里调
   `requestPermissions`（首次安装/换签名重装后会自动弹窗）。
2. MIUI 上 `adb shell pm grant` 常失败
   （`Neither user 2000 nor current process has GRANT_RUNTIME_PERMISSIONS`），
   只有 root 下 `su -c 'pm grant ...'` 才行。**不要依赖 adb 授权**——
   那只是调试手段，真机上必须靠应用自己申请。

**关联坑**：换签名重装会把运行时权限**全部重置**（见 §2.5），这也是一条
"以前能扫、重新部署后不能扫"的常见路径。

### 坑 9：真机部署后务必核对 `lastUpdateTime`——签名漂移会静默装失败

`adb install -r` 遇签名不匹配报 `INSTALL_FAILED_UPDATE_INCOMPATIBLE` 后，
**旧包原样留在手机上继续跑**。打包脚本换过签名密钥（如 2026-10-25 起
统一用 `signing/xgui.keystore`）或密钥重新生成过，就会出现"明明装了新包、
真机跑的还是老界面"的错觉——本轮下拉框排查的收尾阶段即被它误导过
（装了修复版 → 看到的仍是 `3845bf94` 老界面，误以为修复无效）。

**固定动作**：每次部署后核对安装时间，时间没动就是没装上：

```bat
adb shell dumpsys package com.xinyuec.xguidemo | findstr lastUpdateTime
```

签名不一致时先 `adb uninstall com.xinyuec.xguidemo` 再装（代价：运行时
权限重置，见坑 8；新包已内置权限申请，首启会自动弹窗）。

### 坑 10：安卓点下拉框卡死（单线程 100% CPU）/ 崩溃 —— 红黑树定时器环

"周期型+精确定时器"是红黑树高精度组的第一批周期居民（重绘节奏闸补投
定时器首开此例），曾触发 `XBBTree_insertAlign` 空树分支残留指针入树 →
环状破坏 → `insertAdjust` 永旋（卡死）与删除路径 SIGSEGV 同根。
桌面宏=0 编译期裁剪导致该路径桌面覆盖率为零——**桌面复现不了不代表没病**。
完整根因、A/B 实证与压力测试回归锁见根目录
《红黑树定时器环状破坏排查录_20261025.md》。

## 四、字体：两条路线

| 路线 | 产物 | 体积 | 依赖 |
|---|---|---|---|
| FT 轮廓字（当前） | `assets/fonts/XFontOutlineCommon.otf` | 3.3 MB | FreeType + 启动释放到私有目录 |
| 位图字库 | `assets/fonts/XFont16x16.bin` | 490 KB | 无（纯数据，框架直读） |

位图路线可以省掉 2.8 MB 和整个 FreeType 依赖，代价是只能固定字号。
字库由 `Tools/fonts/gen_bitmap_bin.py` 生成，覆盖 6763 汉字 + 118 个中文标点，
详见 [Tools/fonts/README.md](../../Tools/fonts/README.md)。

若要切到位图路线，改两处：

1. `build_apk.sh` / `pack_apk.ps1`：把 `XFontOutlineCommon.otf` 换成 `XFont16x16.bin`
2. `android_main.c`：`XPAD_FONT_FILE` 改成对应文件名，并让 `XFont_setFamily` 用
   `XFont16x16` 家族名（走 `.bin` 外挂路径，不经 FT）

## 五、平台能力现状

| 能力 | 状态 | 说明 |
|---|---|---|
| 窗口/输入 | ✅ | `XPlatformWindowAndroid.c` 经 fbdev 契约报告 surface 能力 |
| 中文渲染 | ✅ | FT 轮廓字，实机验证 |
| 内容旋转 | ✅ | 安卓为软件渲染直写 surface，旋转在 present 侧逐像素 R⁻¹ 转置实现，与 dpr 解耦（`XPlatformNativeWindow_rotationSupported` 放行） |
| 无障碍（TalkBack） | ✅ | `XGuiA11yBridge`/`XGuiA11yHost` 提供虚拟视图树；未装 TalkBack 时可用 `uiautomator dump` 当验收代理 |
| WiFi 扫描/连接 | ✅ | `XDeviceWiFi_android.c` + `XWifiConnector.java`（连接归系统持有） |
| 网卡配置查询/设置 | ✅ | `XDeviceNetwork_android.c` + `XNetAdapter.java`（DHCP/静态 v4；静态 v6 系统级不支持） |
| 剪贴板 | ✅ | `XClipboardAndroid.c` + ClipboardManager |
| 原生装饰（标题栏） | 不适用 | NativeActivity 无系统标题栏，只有框架自绘 CSD |
| 显示边框开关 | ✅ | 只影响框架自绘标题栏，不会出现双标题栏 |

> 桌面上"显示边框导致双标题栏 + 卡死"是 win32 特有的 `WS_CAPTION` 残留
> 与 `SetWindowLongPtr` 抹掉 `WS_VISIBLE` 两个 bug，安卓不存在这两条代码路径。

## 六、清理

```bat
:: 只删产物
rmdir /s /q platforms\android\out

:: 连构建树一起删
rmdir /s /q out\android-x86_64
```
