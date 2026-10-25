# XGuiDemo APK packaging (PowerShell port of build_apk.sh)
# 本目录只放**平台差异层**：打包脚本 / 清单 / 资源 / 签名。
# Android 平台源码（Drive/Android/**）与 demo 源码（Test/XGuiDemo/**）是
# 公共部分，由根 CMake 编译成 libxguidemo.so，本脚本只负责装进 APK。
# Java 桥（无障碍宿主 Activity / WiFi / 网络 JNI 桥）源码在框架平台层
# Drive/Android/java/com，本脚本与 build_apk.sh 都从那里编译，
# 保证两条打包路径产物一致。
#
# 用法：
#   powershell -ExecutionPolicy Bypass -File pack_apk.ps1
#   powershell -ExecutionPolicy Bypass -File pack_apk.ps1 -Abi arm64-v8a
param(
    [ValidateSet('x86_64', 'arm64-v8a', 'both')]
    [string]$Abi = 'x86_64'
)
$ErrorActionPreference = 'Stop'
$SDK   = if ($env:ANDROID_SDK) { $env:ANDROID_SDK } else { 'C:\Users\jxy\AppData\Local\Android\Sdk' }
$BT    = "$SDK\build-tools\36.0.0"
# 平台 jar 取已装版本里最高的一个（原写死 android-37.0，换机器即失效）
$PLAT  = (Get-ChildItem "$SDK\platforms\*\android.jar" -ErrorAction SilentlyContinue |
          Sort-Object FullName -Descending | Select-Object -First 1).FullName
if (-not $PLAT) { throw "找不到 android.jar (SDK=$SDK)" }
# 仓库根：<script>/../..（原为写死的 D:\code\CMake\Container）
# $PSScriptRoot 在 -Command 方式调用时可能为空，故双保险回退到 MyCommand.Path
$SELF  = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $SELF) { throw "无法确定脚本所在目录（请用 -File 方式调用本脚本）" }
$ROOT  = (Resolve-Path (Join-Path $SELF '..\..')).Path
$OUT   = Join-Path $SELF 'out'
$WORK  = "$OUT\work"
# 签名密钥放 signing\（**不在 out\ 下**）：out\ 是构建产物目录，会被清理。
# 密钥一旦重新生成签名就变了——覆盖安装要么失败
# （INSTALL_FAILED_UPDATE_INCOMPATIBLE），要么被系统当作换签名重装而
# **重置全部运行时权限**（真机实证：ACCESS_FINE_LOCATION 被打回未授予，
# WiFi 扫描结果随后恒为空表，表现为"扫描功能失效"）。
$KS    = "$SELF\signing\xgui.keystore"
# 旧版密钥位置（2026-10-24 迁入 platforms\ 前生成在 out\ 下）：首次运行
# 时搬到新位置，保证签名不变、覆盖安装仍成立。
if (-not (Test-Path $KS) -and (Test-Path "$OUT\xgui.keystore")) {
    New-Item -ItemType Directory -Path "$SELF\signing" -Force | Out-Null
    Move-Item "$OUT\xgui.keystore" $KS -Force
}
$JAVA  = if ($env:JAVA_HOME) { $env:JAVA_HOME } else { 'C:\Program Files\Android\Android Studio\jbr' }

# ABI 映射：CMake 构建目录名 ↔ APK 内 jniLibs 目录名
# （arm64-v8a 的构建目录历史上缩写为 arm64）
function Get-AbiDir([string]$a) {
    switch ($a) {
        'arm64-v8a' { return 'arm64' }
        default     { return $a }
    }
}
$abiList = @($Abi)
if ($Abi -eq 'both') { $abiList = @('x86_64', 'arm64-v8a') }

$env:JAVA_HOME = $JAVA
$env:PATH = "$JAVA\bin;$env:PATH"

if (Test-Path $WORK) { Remove-Item $WORK -Recurse -Force }
New-Item -ItemType Directory -Path $OUT -Force | Out-Null

Write-Host "== 0b) manifest <-> platform Activity drift check =="
# The manifest's Activity is a **platform-layer** Java class
# (Drive/Android/java/com). If the Java package/class and the
# manifest disagree, the APK installs but fails to launch with
# "Activity class does not exist" -- hard to diagnose on a device.
# The Java source is the source of truth here.
#
# Also verifies **three-way consistency**: the C-side macro
# XANDROID_JAVA_PKG (the single string source for JNI class lookup)
# == the Java package declaration == the manifest activity name.
# Java has no preprocessor, so the package line can only be a literal --
# the C macro and it can only be locked together by this gate.
$JFILE = Join-Path $ROOT "Drive\Android\java\com\XGuiNativeActivity.java"
$PHDR  = Join-Path $ROOT "Drive\Android\Core\XAndroidJavaPackage.h"
if (-not (Test-Path $JFILE)) { throw "missing platform Activity source: $JFILE" }
if (-not (Test-Path $PHDR))  { throw "missing Java package header: $PHDR" }
$JText = Get-Content $JFILE -Raw
if ($JText -notmatch '(?m)^\s*package\s+([A-Za-z0-9_.]+)\s*;') { throw "cannot parse package from $JFILE" }
$jPkg = $Matches[1]
if ($JText -notmatch 'public\s+(static\s+)?(final\s+)?class\s+([A-Za-z0-9_]+)') { throw "cannot parse class from $JFILE" }
$jCls = $Matches[3]
$HText = Get-Content $PHDR -Raw
if ($HText -notmatch '#define\s+XANDROID_JAVA_PKG\s+"([^"]+)"') { throw "cannot parse XANDROID_JAVA_PKG from $PHDR" }
$cPkg = $Matches[1]
if ($cPkg -ne $jPkg) {
    Write-Host "Java package mismatch:" -ForegroundColor Red
    Write-Host "  C macro (XANDROID_JAVA_PKG): $cPkg   ($PHDR)"
    Write-Host "  Java decl (package ...)    : $jPkg   ($JFILE)"
    Write-Host "  -> both must name the same package."
    throw "Java package mismatch (C macro $cPkg vs Java $jPkg)"
}
$fqcn = "$jPkg.$jCls"
$manifestText = Get-Content (Join-Path $SELF "AndroidManifest.xml") -Raw
if ($manifestText -notlike "*android:name=`"$fqcn`"*") {
    Write-Host "manifest does not match the platform-layer Activity:" -ForegroundColor Red
    Write-Host "  platform Java : $fqcn   ($JFILE)"
    Write-Host "  manifest lacks: android:name=`"$fqcn`""
    Write-Host "  -> change the Java source or the manifest; they must agree,"
    Write-Host "     otherwise the app installs but never launches."
    throw "manifest / platform Activity mismatch ($fqcn)"
}
Write-Host "  manifest Activity matches platform: $fqcn"

Write-Host "== 1) copy native lib (ABI: $Abi) =="
foreach ($a in $abiList) {
    $srcDir = Join-Path $ROOT "out\android-$(Get-AbiDir $a)\libxguidemo.so"
    if (-not (Test-Path $srcDir)) {
        throw "缺 $srcDir`n  先构建：cmake --build out\android-$(Get-AbiDir $a) --target xguidemo"
    }
    $dst = "$WORK\lib\$a"
    New-Item -ItemType Directory -Path $dst -Force | Out-Null
    Copy-Item $srcDir "$dst\libxguidemo.so" -Force
    Get-Item "$dst\libxguidemo.so" | Select-Object FullName, Length | Format-List
}

Write-Host "== 2) font assets (fonts/XFontOutlineCommon.otf) =="
$ASSETS = "$WORK\assets"
New-Item -ItemType Directory -Path "$ASSETS\fonts" -Force | Out-Null
Copy-Item "$ROOT\Library\XFont\XFontOutlineCommon.otf" "$ASSETS\fonts\XFontOutlineCommon.otf" -Force
Get-ChildItem "$ASSETS\fonts" | Select-Object Name,Length | Format-Table -AutoSize

Write-Host "== 2b) aapt2 compile =="
& "$BT\aapt2.exe" compile --dir "$SELF\res" -o "$WORK\res.zip"
if ($LASTEXITCODE -ne 0) { throw "aapt2 compile failed ($LASTEXITCODE)" }

# ---- Java 桥编译（无障碍桥宿主 Activity + WiFi/网络 JNI 桥）----
# Java 源码属于框架平台层，在 Drive/Android/java/com 下
# （与 build_apk.sh 保持单一来源）。缺这一步打出的 APK 没有 classes.dex，
# 清单里的 Activity 子类不存在，启动即 ClassNotFoundException。
$JAVA_SRC = Join-Path $ROOT "Drive\Android\java\com"
if (-not (Test-Path $JAVA_SRC)) { throw "缺 Java 桥源码目录: $JAVA_SRC" }
$CLASSES = Join-Path $WORK "classes"
New-Item -ItemType Directory -Path $CLASSES -Force | Out-Null
$javaFiles = Get-ChildItem "$JAVA_SRC\*.java" | ForEach-Object { $_.FullName }
if (-not $javaFiles) { throw "Java 桥源码目录为空: $JAVA_SRC" }
Write-Host "== 2c) javac (java bridge) =="
# -encoding UTF-8：Java 桥源码是 UTF-8（无 BOM），JDK 11 的默认源码编码
# 跟随系统 ANSI 代码页（中文 Windows = GBK），不显式指定会报"编码 GBK 的
# 不可映射字符"。JDK 18+ 默认已是 UTF-8，显式写上才能在 JDK 11 上同样通过。
& "$JAVA\bin\javac.exe" --release 8 -nowarn -encoding UTF-8 -classpath "$PLAT" -d "$CLASSES" @javaFiles
if ($LASTEXITCODE -ne 0) { throw "javac failed ($LASTEXITCODE)" }
$classFiles = Get-ChildItem "$CLASSES\*.class" -Recurse | ForEach-Object { $_.FullName }
if (-not $classFiles) { throw "javac 未产出 class 文件" }
Write-Host "== 2d) d8 -> classes.dex =="
& "$BT\d8.bat" --min-api 26 --lib "$PLAT" --output "$WORK" @classFiles
if ($LASTEXITCODE -ne 0) { throw "d8 failed ($LASTEXITCODE)" }
if (-not (Test-Path "$WORK\classes.dex")) { throw "d8 未产出 classes.dex" }

Write-Host "== 3) aapt2 link =="
& "$BT\aapt2.exe" link -o "$WORK\base.apk" -I "$PLAT" --manifest "$SELF\AndroidManifest.xml" "$WORK\res.zip"
if ($LASTEXITCODE -ne 0) { throw "aapt2 link failed ($LASTEXITCODE)" }

Write-Host "== 4) add native lib + font asset + classes.dex (forward slashes!) =="
Push-Location $WORK
foreach ($a in $abiList) {
    # 逐 ABI 加入；路径必须是正斜杠，否则 AAssetManager / 动态链接器找不到
    & "$BT\aapt.exe" add -f base.apk "lib/$a/libxguidemo.so"
    if ($LASTEXITCODE -ne 0) { Pop-Location; throw "aapt add lib/$a failed ($LASTEXITCODE)" }
}
& "$BT\aapt.exe" add -f base.apk "assets/fonts/XFontOutlineCommon.otf"
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "aapt add font failed ($LASTEXITCODE)" }
& "$BT\aapt.exe" add -f base.apk "classes.dex"
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "aapt add classes.dex failed ($LASTEXITCODE)" }
Pop-Location

Write-Host "== 5) zipalign =="
& "$BT\zipalign.exe" -f 4 "$WORK\base.apk" "$WORK\base-aligned.apk"
if ($LASTEXITCODE -ne 0) { throw "zipalign failed ($LASTEXITCODE)" }

Write-Host "== 6) keystore =="
if (-not (Test-Path $KS)) {
  & "$JAVA\bin\keytool.exe" -genkeypair -keystore $KS -alias xgui -keyalg RSA -keysize 2048 -validity 10000 `
      -storepass xguidemo -keypass xguidemo -dname "CN=XGuiDemo,O=XinYueC,C=CN"
  if ($LASTEXITCODE -ne 0) { throw "keytool failed ($LASTEXITCODE)" }
}

Write-Host "== 7) apksigner sign =="
& "$BT\apksigner.bat" sign --ks $KS --ks-pass pass:xguidemo --ks-key-alias xgui --key-pass pass:xguidemo `
    --out "$OUT\XGuiDemo-debug.apk" "$WORK\base-aligned.apk"
if ($LASTEXITCODE -ne 0) { throw "apksigner failed ($LASTEXITCODE)" }

Write-Host "== OK =="
Get-Item "$OUT\XGuiDemo-debug.apk" | Select-Object FullName,Length,LastWriteTime | Format-List
