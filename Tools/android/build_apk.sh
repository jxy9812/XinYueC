#!/usr/bin/env bash
# ============================================================
# XGuiWindowDemo APK 打包脚本（NativeActivity 壳，无 Java 代码）
# 用法: build_apk.sh [x86_64|arm64-v8a|both]
# 依赖: ANDROID_SDK / ANDROID_NDK 环境变量或默认路径；JDK keytool
# 产物: Tools/android/out/XGuiDemo-debug.apk
# ============================================================
set -e

SDK="${ANDROID_SDK:-D:/Program Files/Android/Sdk}"
NDK="${ANDROID_NDK:-D:/Program Files/Android/Sdk/ndk/android-ndk-r29}"
BT="$SDK/build-tools/36.0.0"
PLATFORM="$SDK/platforms/android-35/android.jar"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT_APK_DIR="$ROOT/Tools/android/out"
WORK="$OUT_APK_DIR/work"
KEYSTORE="$OUT_APK_DIR/xgui.keystore"
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

# ---- 2) 资源编译（仅清单，无 res）----
"$BT/aapt2" compile --dir "$ROOT/Tools/android/res" -o "$WORK/res.zip"
"$BT/aapt2" link -o "$WORK/base.apk" \
    -I "$PLATFORM" \
    --manifest "$ROOT/Tools/android/AndroidManifest.xml" \
    "$WORK/res.zip"

# ---- 3) 塞入原生库 ----
cd "$WORK"
mkdir -p lib
cp -r libs/* lib/
"$BT/aapt" add -f base.apk $(cd lib && ls */libxguidemo.so | sed 's|^|lib/|') > /dev/null

# ---- 4) debug 签名（无 keystore 则生成一次）----
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
