#!/usr/bin/env bash
# ============================================================
# XGuiDemo APK 打包脚本（NativeActivity 壳，无 Java 代码）
# 用法: build_apk.sh [x86_64|arm64-v8a|both]
# 依赖: ANDROID_SDK / ANDROID_NDK 环境变量或默认路径；JDK keytool
# 产物: platforms/android/out/XGuiDemo-debug.apk
#
# 目录说明（2026-10-24 迁入 platforms/）：
#   本目录只放**平台差异层**——打包脚本、清单、资源、签名。
#   Android 的平台源码（Drive/Android/**）与 demo 源码（Test/XGuiDemo/**）
#   属于框架/工程的公共部分，仍留在原处，由根 CMake 统一编译成
#   libxguidemo.so；本脚本只负责把它装进 APK。
# ============================================================
set -e

# 默认路径按平台分流：Windows=原实装路径（历史默认），Linux=~/android-tools
# 布局（免 sudo 安装，见 ~/android-tools-install.sh：JDK17+NDK r29+build-tools 36）。
case "$(uname -s)" in
    Linux)
        SDK="${ANDROID_SDK:-$HOME/android-tools/sdk}"
        NDK="${ANDROID_NDK:-$HOME/android-tools/android-ndk-r29}" ;;
    *)
        SDK="${ANDROID_SDK:-C:/Users/jxy/AppData/Local/Android/Sdk}"
        NDK="${ANDROID_NDK:-D:/android-ndk/android-ndk-r29}" ;;
esac
BT="$SDK/build-tools/36.0.0"
# 平台 android.jar 自动取已装 platforms 里版本号最高的一个（本机实装为
# android-37.0；脚本原写死 android-35 的路径在本机不存在）。
PLATFORM="$(ls -d "$SDK"/platforms/*/android.jar 2>/dev/null | sort -V | tail -1)"
[ -f "$PLATFORM" ] || { echo "找不到 android.jar（SDK=$SDK）"; exit 1; }
# 仓库根：本文件在 <repo>/platforms/android/，上溯两级
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_APK_DIR="$SELF_DIR/out"
WORK="$OUT_APK_DIR/work"
# 签名密钥放 signing/（**不在 out/ 下**）：out/ 是构建产物目录，会随清理
# 消失。密钥一旦重新生成，签名就变了——覆盖安装要么失败
# （INSTALL_FAILED_UPDATE_INCOMPATIBLE），要么被系统当作换签名重装而
# **重置全部运行时权限**（真机实证：ACCESS_FINE_LOCATION 被打回未授予，
# WiFi 扫描结果随后恒为空表，表现为"扫描功能失效"）。
KEYSTORE="$SELF_DIR/signing/xgui.keystore"
# 旧版密钥位置（2026-10-24 迁入 platforms/ 前生成在 out/ 下）：首次运行
# 时搬到新位置，保证签名不变、覆盖安装仍成立。
if [ ! -f "$KEYSTORE" ] && [ -f "$OUT_APK_DIR/xgui.keystore" ]; then
    mkdir -p "$SELF_DIR/signing"
    mv "$OUT_APK_DIR/xgui.keystore" "$KEYSTORE"
fi
ABIS="${1:-both}"

rm -rf "$WORK"; mkdir -p "$WORK/jni" "$WORK/libs" "$OUT_APK_DIR"

# ---- 1) 原生库（CMake 产物直拷，双 ABI） ----
copy_lib() {
    SRC="$ROOT/out/android-$1/libxguidemo.so"
    [ -f "$SRC" ] || { echo "缺 $SRC，先 cmake --build out/android-$1 --target xguidemo"; exit 1; }
    mkdir -p "$WORK/libs/$2"
    cp "$SRC" "$WORK/libs/$2/"
}
case "$ABIS" in
    x86_64)    copy_lib x86_64  x86_64 ;;
    arm64-v8a) copy_lib arm64  arm64-v8a ;;
    both)      copy_lib x86_64  x86_64; copy_lib arm64 arm64-v8a ;;
esac

# ---- 1.5) 清单 ↔ 平台层 Activity 漂移检查 ----
# 清单里的 Activity 是**平台层**的 Java 类（Drive/Android/java/，包
# com.xinyuec.android）。两边不同步（改类名/换包忘了改清单）时，APK 装得上
# 但启动即 "Activity class does not exist"——真机上很难一眼看出根因。
# 这里以 Java 源为事实源校验清单，把失败提前到打包期。
#
# 同时校验**三方一致**：C 侧宏 XANDROID_JAVA_PKG（JNI 查类拼全名的唯一
# 字符串来源）== Java 的 package 声明 == 清单的 activity 名。Java 无预处理器，
# package 那行只能是字面量，所以 C 宏与它只能靠本门禁锁死。
check_activity_manifest() {
    JFILE="$ROOT/Drive/Android/java/com/XGuiNativeActivity.java"
    HDR="$ROOT/Drive/Android/Core/XAndroidJavaPackage.h"
    if [ ! -f "$JFILE" ]; then
        echo "MISSING platform-layer Activity source: $JFILE"
        exit 1
    fi
    if [ ! -f "$HDR" ]; then
        echo "MISSING Java package header: $HDR"
        exit 1
    fi
    PKG=$(grep -oE '^[[:space:]]*package[[:space:]]+[A-Za-z0-9_.]+' "$JFILE" | awk '{print $2}' | head -1)
    CLS=$(grep -oE 'public[[:space:]]+(static[[:space:]]+)?(final[[:space:]]+)?class[[:space:]]+[A-Za-z0-9_]+' "$JFILE" \
          | awk '{print $NF}' | head -1)
    if [ -z "$PKG" ] || [ -z "$CLS" ]; then
        echo "CANNOT parse package/class from platform Activity: $JFILE"
        exit 1
    fi
    CPKG=$(sed -n 's/^#define[[:space:]]\+XANDROID_JAVA_PKG[[:space:]]\+"\([^"]*\)".*/\1/p' "$HDR" | head -1)
    if [ -z "$CPKG" ]; then
        echo "CANNOT parse XANDROID_JAVA_PKG from: $HDR"
        exit 1
    fi
    if [ "$CPKG" != "$PKG" ]; then
        echo "JAVA PACKAGE MISMATCH:"
        echo "  C macro (XANDROID_JAVA_PKG): $CPKG   ($HDR)"
        echo "  Java decl (package ...)    : $PKG   ($JFILE)"
        echo "  -> both must name the same package."
        exit 1
    fi
    FQCN="$PKG.$CLS"
    if ! grep -q "android:name=\"$FQCN\"" "$SELF_DIR/AndroidManifest.xml"; then
        # 诊断输出用 ASCII：控制台代码页可能是 GBK，中文会成乱码。
        echo "MANIFEST / PLATFORM ACTIVITY MISMATCH:"
        echo "  platform Java : $FQCN   ($JFILE)"
        echo "  manifest lacks: android:name=\"$FQCN\"   ($SELF_DIR/AndroidManifest.xml)"
        echo "  -> fix the Java source or the manifest; they must agree."
        echo "     Otherwise the APK installs but never launches."
        exit 1
    fi
    echo "  manifest Activity matches platform: $FQCN"
}
check_activity_manifest

# ---- 2) 资源编译（清单 + 外挂字库 assets）----
# 字库必须随包：框架 FT 轮廓字按编译期目录找 <dir>/<family>.{ttf,otf,ttc}，
# 安卓上该目录是构建机路径，设备上不存在 → 装载失败回退位图字库
# XFont8x16（仅 ASCII）→ 中文整片缺失。APK 内 assets/fonts/ 由
# android_main.c 启动时经 AAssetManager 释放到应用私有目录 fonts/
# （CMake 安卓分支注入 XFONT_EXTERNAL_FT_FONT_DIR 指向该目录）。
FONT_ASSETS="$WORK/assets"
rm -rf "$FONT_ASSETS"; mkdir -p "$FONT_ASSETS/fonts"
FONT_SRC="$ROOT/Library/XFont/XFontOutlineCommon.otf"
[ -f "$FONT_SRC" ] || { echo "缺 $FONT_SRC"; exit 1; }
cp "$FONT_SRC" "$FONT_ASSETS/fonts/"
"$BT/aapt2" compile --dir "$SELF_DIR/res" -o "$WORK/res.zip"
"$BT/aapt2" link -o "$WORK/base.apk" \
    -I "$PLATFORM" \
    --manifest "$SELF_DIR/AndroidManifest.xml" \
    "$WORK/res.zip"

# ---- 3) 塞入原生库 + 字体 asset ----
# 字库以 "aapt add" 入包（不用 aapt2 -A）：Windows 版 aapt2 的 -A 会把
# 条目名写成 "assets\fonts\XFontOutlineCommon.otf"（反斜杠），zip 路径
# 须为正斜杠，AAssetManager_open("fonts/...") 才会命中（真机日志实证：
# "font: asset fonts/XFontOutlineCommon.otf not found in APK"）。
cd "$WORK"
mkdir -p lib
cp -r libs/* lib/
"$BT/aapt" add -f base.apk $(cd lib && ls */libxguidemo.so | sed 's|^|lib/|') > /dev/null
"$BT/aapt" add -f base.apk assets/fonts/XFontOutlineCommon.otf > /dev/null

# ---- 3b) Java 侧（无障碍桥）编译 + dex ----
# 2026-10-24 安卓无障碍接入：TalkBack 只认 Java View 树，故 Activity 改
# 为自有子类（继承 NativeActivity）并挂虚拟视图树宿主。Java 侧仅三个
# 类（XGuiNativeActivity / XGuiA11yHost / XGuiA11yBridge），编译成
# classes.dex 随包发布。缺 javac/d8 时**不静默跳过**——跳过则 Activity
# 类名在清单里但不存在，运行必崩；此时直接报错让构建失败。
_JAVAC_BIN=javac.exe
_D8_PROG="$BT/d8.bat"
JAVA_HOME_CAND=""
case "$(uname -s)" in
    Linux)
        # Linux：javac 按 JAVA_HOME → ~/android-tools/jdk → PATH 探测（无
        # .exe 后缀）；d8 是 build-tools 自带 POSIX 脚本（内部调 java，须
        # JAVA_HOME/bin 已入 PATH）。
        _JAVAC_BIN=javac
        _D8_PROG="$BT/d8"
        for _j in "${JAVA_HOME:-}" "$HOME/android-tools/jdk"; do
            if [ -n "$_j" ] && [ -x "$_j/bin/javac" ]; then JAVA_HOME_CAND="$_j"; break; fi
        done
        if [ -z "$JAVA_HOME_CAND" ] && command -v javac > /dev/null 2>&1; then
            JAVA_HOME_CAND="$(dirname "$(dirname "$(command -v javac)")")"
        fi
        ;;
    *)
        # JAVA_HOME 优先（与 pack_apk.ps1 口径一致：本机 JDK 不一定装在
        # 程序Files 惯例路径，如 D:\Program\Java\jdk-11）。
        for _j in \
            "${JAVA_HOME:-}" \
            "C:/Program Files/Android/Android Studio/jbr" \
            "C:/Program Files/Eclipse Adoptium"/* \
            "C:/Program Files/Java"/*
        do
            if [ -n "$_j" ] && [ -x "$_j/bin/javac.exe" ]; then JAVA_HOME_CAND="$_j"; break; fi
        done
        ;;
esac
if [ -n "$JAVA_HOME_CAND" ] && [ -f "$_D8_PROG" ]; then
    JAVAC="$JAVA_HOME_CAND/bin/$_JAVAC_BIN"
    export JAVA_HOME="$JAVA_HOME_CAND"
    # PATH 里不能直接放 Windows 风格 "$JAVA_HOME/bin"：盘符冒号会被 POSIX
    # PATH 按分隔符拆开（"D:\Program\..." → 条目 D 和 \Program\...），
    # 子进程（d8.bat→cmd）解析到半截路径报 "'D:\Program' 不是内部或外部
    # 命令"。cygpath 转成 /d/... 形态再入 PATH（PortableGit 自带 cygpath）。
    _JAVA_BIN_POSIX="$JAVA_HOME/bin"
    if command -v cygpath > /dev/null 2>&1; then
        _JAVA_BIN_POSIX="$(cygpath -u "$JAVA_HOME/bin")"
    fi
    export PATH="$_JAVA_BIN_POSIX:$PATH"
    PLATFORM_JAR="$(ls "$SDK"/platforms/*/android.jar | sort -V | tail -1)"
    rm -rf "$WORK/classes"; mkdir -p "$WORK/classes"
    # --release 8：安卓运行时 dex 支持（javac 25 已移除 -source 8）。
    # 输出目录用绝对路径（Windows 路径在 msys 下需显式转换，$WORK 已是
    # POSIX 形态的 /d/... —— javac 接受）。
    # 源目录：框架安卓平台层的 JNI 桥（与 Drive/Android/ 的 C 侧平台实现
    # 同层），不在本目录下——2026-10-24 自应用包迁出。
    JAVA_SRC="$ROOT/Drive/Android/java/com"
    if [ ! -d "$JAVA_SRC" ]; then
        echo "缺 Java 桥源码目录: $JAVA_SRC"; exit 1
    fi
    # -encoding UTF-8：Java 桥源码是 UTF-8（无 BOM），而 JDK 11 的默认
    # 源码编码跟随系统 ANSI 代码页（中文 Windows = GBK），不显式指定就会
    # 报"编码 GBK 的不可映射字符"。JDK 18+ 默认已是 UTF-8，但显式写上
    # 才能在 JDK 11 上同样通过（JDK 8 ~ 17 都认这个开关）。
    "$JAVAC" --release 8 -nowarn -encoding UTF-8 \
        -classpath "$PLATFORM_JAR" \
        -d "$WORK/classes" \
        "$JAVA_SRC"/*.java
    if [ ! -d "$WORK/classes/com" ]; then
        echo "javac 未产出 class 文件"; exit 1
    fi
    # d8 路径形态按平台：Linux d8 为 POSIX 脚本，正斜杠路径直用；Windows
    # d8 是 .bat 且只认反斜杠形态（POSIX 斜杠路径会 NoSuchFileException），
    # 用 cygpath 转换（PortableGit 自带）；无 cygpath 时退化为手工替换。
    case "$(uname -s)" in
        Linux)
            CLASS_FILES=$(cd "$WORK/classes" && find . -name '*.class' | \
                          sed 's|^\./||' | while read -r f; do
                              echo "$WORK/classes/$f"; done)
            "$_D8_PROG" --min-api 26 --lib "$PLATFORM_JAR" \
                --output "$WORK" $CLASS_FILES
            ;;
        *)
            if command -v cygpath > /dev/null 2>&1; then
                CLASS_FILES=$(cd "$WORK/classes" && find . -name '*.class' | \
                             sed 's|^\./||' | while read -r f; do
                                 cygpath -w "$WORK/classes/$f"; done)
            else
                CLASS_FILES=$(cd "$WORK/classes" && find . -name '*.class' | \
                             sed 's|^\./||' | while read -r f; do
                                 echo "$WORK/classes/$f" | sed 's|/|\\\\|g'; done)
            fi
            # d8.bat 不能直调：msys 起批处理时**含空格的参数不会被重新
            # 加引号**（--lib "D:\Program Files\..." 在 cmd 命令行被拆开，
            # 报 "'D:\Program' 不是内部或外部命令"）。改为 java 直调
            # d8.jar（.exe 传参的引号/路径转换语义正常），调用面与
            # d8.bat 内部 call "%java_exe%" ... -cp d8.jar 完全一致；
            # SDK 装在带空格路径（D:\Program Files\Android\SDK，本机
            # 实装）时必踩，故不走 _D8_PROG。
            "$JAVA_HOME_CAND/bin/java.exe" -Xmx1024M \
                -cp "$BT/lib/d8.jar" com.android.tools.r8.D8 \
                --min-api 26 --lib "$PLATFORM_JAR" \
                --output "$WORK" $CLASS_FILES
            ;;
    esac
    if [ ! -f "$WORK/classes.dex" ]; then
        echo "d8 未产出 classes.dex"; exit 1
    fi
    "$BT/aapt" add -f base.apk classes.dex > /dev/null
    echo "java: classes.dex 已入包（Activity=XGuiNativeActivity）"
else
    echo "缺 javac/d8（无障碍桥需要 Java 侧）——请装 Android Studio JBR 或 build-tools d8"
    exit 1
fi

# ---- 4) debug 签名（无 keystore 则生成一次）----
# JAVA_HOME / PATH：git-bash 不继承 Windows 的 JAVA_HOME 时，keytool 与
# apksigner 都会失败（apksigner 是 .bat，内部要 java）。按
# 环境变量 → Android Studio 自带 JBR → PATH 里的 java 顺序探测。
if [ -z "${JAVA_HOME:-}" ]; then
    for _j in \
        "C:/Program Files/Android/Android Studio/jbr" \
        "C:/Program Files/Eclipse Adoptium"/* \
        "C:/Program Files/Java"/*
    do
        if [ -x "$_j/bin/java.exe" ] || [ -x "$_j/bin/java" ]; then
            JAVA_HOME="$_j"
            break
        fi
    done
fi
if [ -n "${JAVA_HOME:-}" ]; then
    export JAVA_HOME
    # 同 3b：Windows 风格路径先转 POSIX 再入 PATH（盘符冒号拆分问题）。
    _JAVA_BIN_POSIX="$JAVA_HOME/bin"
    if command -v cygpath > /dev/null 2>&1; then
        _JAVA_BIN_POSIX="$(cygpath -u "$JAVA_HOME/bin")"
    fi
    export PATH="$_JAVA_BIN_POSIX:$PATH"
fi

if [ ! -f "$KEYSTORE" ]; then
    keytool -genkeypair -keystore "$KEYSTORE" -alias xgui \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -storepass xguidemo -keypass xguidemo \
        -dname "CN=XGuiDemo,O=XinYueC,C=CN"
fi
"$BT/zipalign" -f 4 base.apk base-aligned.apk
# apksigner 在 Windows 发行里是 .bat（内部调 java -jar lib/apksigner.jar）
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*)
        apksigner_cmd="$BT/apksigner.bat" ;;
    *)
        apksigner_cmd="$BT/apksigner" ;;
esac
"$apksigner_cmd" sign --ks "$KEYSTORE" --ks-pass pass:xguidemo \
    --ks-key-alias xgui --key-pass pass:xguidemo \
    --out "$OUT_APK_DIR/XGuiDemo-debug.apk" base-aligned.apk

echo "OK: $OUT_APK_DIR/XGuiDemo-debug.apk"
