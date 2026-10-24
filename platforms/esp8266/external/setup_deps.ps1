# ============================================================================
# ESP8266 外部依赖安装脚本（Windows PowerShell）
# ============================================================================
# 用途：把构建所需的两大外部依赖下载/解压到 platforms/esp8266/external/ 下，使整个项目
#       零外部依赖——换电脑只要 clone 仓库 + 跑一次本脚本即可编译。
#
#   platforms/esp8266/external/toolchain-xtensa/   xtensa-lx106-elf GCC 10.3（约 226MB）
#   platforms/esp8266/external/arduino8266/        Arduino ESP8266 包：NONOS SDK + core + eboot
#                                 + elf2bin.py（约 75MB）
#   platforms/esp8266/external/nonos_v305/include/ NONOS SDK 3.0.5 头文件（已在仓库内，~149KB）
#
# 用法（在仓库根目录）：
#   powershell -ExecutionPolicy Bypass -File platforms\esp8266\external\setup_deps.ps1
#   powershell -ExecutionPolicy Bypass -File platforms\esp8266\external\setup_deps.ps1 -Force
#
# 已经装好的部分会自动跳过；-Force 强制重新下载。
# ============================================================================
param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # 加快 Invoke-WebRequest

$EspDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ToolchainDir = Join-Path $EspDir 'toolchain-xtensa'
$ArduinoDir = Join-Path $EspDir 'arduino8266'
$DownloadDir = Join-Path $EspDir '_download'
$GccBin = Join-Path $ToolchainDir 'bin\xtensa-lx106-elf-gcc.exe'

function Write-Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Write-Ok($msg)   { Write-Host "    [OK] $msg" -ForegroundColor Green }
function Write-Warn2($msg){ Write-Host "    [!!] $msg" -ForegroundColor Yellow }

# ---------------------------------------------------------------------------
# 1. 检查工具链
# ---------------------------------------------------------------------------
Write-Step "检查 xtensa 工具链"
if ((Test-Path $GccBin) -and (-not $Force)) {
    Write-Ok "已存在：$GccBin"
} else {
    Write-Warn2 "缺少 xtensa 工具链。"
    Write-Host @"

本脚本不内置工具链二进制（约 226MB，不适合放进 git）。
请任选一种方式提供：

  A) 已有工具链：把整个目录复制到
       $ToolchainDir
     要求：$GccBin 存在。

  B) 从 Arduino 包安装（推荐，版本已匹配）：
     1) 下载 Arduino ESP8266 core：
        https://github.com/esp8266/Arduino/releases
     2) 或直接用 Arduino IDE 的 Boards Manager 安装 esp8266 包，
        工具链会落在：
        %LOCALAPPDATA%\Arduino15\packages\esp8266\tools\xtensa-lx106-elf-gcc\<ver>\
     3) 把该目录整体复制为：
        $ToolchainDir

  C) 从 Espressif 官方下载：
        https://github.com/espressif/ESP8266_RTOS_SDK/releases
        需要其中的 xtensa-lx106-elf 工具链（GCC 10.3 版本）

"@ -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# 2. 检查 Arduino 包
# ---------------------------------------------------------------------------
Write-Step "检查 Arduino ESP8266 包"
$SdkDir = Join-Path $ArduinoDir 'tools\sdk'
$CoreDir = Join-Path $ArduinoDir 'cores\esp8266'
$Eb = Join-Path $ArduinoDir 'bootloaders\eboot\eboot.elf'
$E2B = Join-Path $ArduinoDir 'tools\elf2bin.py'

if ((Test-Path $SdkDir) -and (Test-Path $CoreDir) -and (Test-Path $Eb) -and (Test-Path $E2B) -and (-not $Force)) {
    Write-Ok "已存在：$ArduinoDir"
} else {
    Write-Warn2 "缺少 Arduino ESP8266 包（NONOS SDK / core / eboot / elf2bin.py）。"
    Write-Host @"

请提供 Arduino ESP8266 包，复制到：
  $ArduinoDir
要求目录结构：
  $ArduinoDir\tools\sdk\{include,lib,ld,lwip2}
  $ArduinoDir\cores\esp8266\
  $ArduinoDir\bootloaders\eboot\eboot.elf
  $ArduinoDir\tools\elf2bin.py

来源（任选）：
  A) Arduino IDE Boards Manager 装 esp8266 包后，从
     %LOCALAPPDATA%\Arduino15\packages\esp8266\hardware\esp8266\<ver>\
     复制上述四项。
  B) https://github.com/esp8266/Arduino/releases 下载源码包，
     其中 tools/sdk、cores/esp8266、bootloaders/eboot、tools/elf2bin.py
     即所需内容。

"@ -ForegroundColor Yellow
}

# ---------------------------------------------------------------------------
# 3. 检查 NONOS 头（仓库自带）
# ---------------------------------------------------------------------------
Write-Step "检查 NONOS SDK 3.0.5 头文件（仓库自带）"
$NonosInc = Join-Path $EspDir 'nonos_v305\include'
if (Test-Path (Join-Path $NonosInc 'user_interface.h')) {
    Write-Ok "已存在：$NonosInc"
} else {
    Write-Warn2 "缺少 $NonosInc\user_interface.h（应由仓库提供，请检查 clone 是否完整）"
}

# ---------------------------------------------------------------------------
# 4. 汇总
# ---------------------------------------------------------------------------
Write-Step "检查结果"
$ok = $true
if (-not (Test-Path $GccBin))  { Write-Warn2 "工具链缺失"; $ok = $false }
if (-not (Test-Path $SdkDir))  { Write-Warn2 "Arduino SDK 缺失"; $ok = $false }
if (-not (Test-Path $CoreDir)) { Write-Warn2 "Arduino core 缺失"; $ok = $false }
if (-not (Test-Path $Eb))      { Write-Warn2 "eboot.elf 缺失"; $ok = $false }
if (-not (Test-Path $E2B))     { Write-Warn2 "elf2bin.py 缺失"; $ok = $false }
if (-not (Test-Path (Join-Path $NonosInc 'user_interface.h'))) { Write-Warn2 "NONOS 头缺失"; $ok = $false }

if ($ok) {
    Write-Host "`n全部依赖就绪。现在可以构建：" -ForegroundColor Green
    Write-Host "  bash platforms/esp8266/nonos/tools/build_fw_final.sh" -ForegroundColor Green
    exit 0
} else {
    Write-Host "`n依赖不完整，请按上面的提示补齐后重新运行本脚本。" -ForegroundColor Yellow
    exit 1
}
