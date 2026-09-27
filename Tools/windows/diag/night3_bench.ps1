# =====================================================================
# night3_bench.ps1 — night-3 基线/终测协议（可重复跑，终测直接复用）
#
# 第三波口径（本版，02:30 攻坚）：增量口径三态扩为五态——二波
# {def, dr1, pbo0} 原样保留（历史日志名不变），追加 K 路（F 路归因
# 「压派发数+降每派发成本」的落地道）两态：
#        fbo0 = XGPU_FBO_PERSIST=0 → 关 FBO 跨帧常驻（end_frame 恢复
#               解绑；K 路逐派发固定成本削减开关之一）
#        dsp0 = XGPU_DISPATCH_MERGE=0 → 关派发合并（K 路开关名以 K 报
#               告为准：若 K 实际落地用别名——工作区已见候选 XGPU_STATE_
#               CACHE / XGPU_MAKECURRENT_ONCE——用 -KDispatchSwitch 覆盖
#               键名重跑，无需改脚本）
#      两键均留可配置位（param -KFboSwitch/-KDispatchSwitch，全脚本唯
#      一出处）。开关未落地进 exe 前（如 02:08 构建：二进制无键串，见
#      night3-commit-plan.md §0），fbo0/dsp0 为空操作，采到的就是基线
#      ——报告/引用处必须标注「未生效=基线」，不得当收益解读。
#      净化纪律同步加厚：每态把【其余四键】全部移除（含二波键与 K
#      键），单态键互斥升级为五态互斥。
#
# 第四波口径（本版增补，撕裂验收）：验收标准新增「画面无撕裂」，故加
# 两处撕裂口径（均不触碰历史格的日志名/数值口径）：
#   1) 撕裂页帧率格：图表页 --page 4 --tab 20（主会话 23:2x 实锤内层
#      页签残影/双影的复现参数，坏例截图 Tools/tear_p4t20.png）入矩阵，
#      命名 p{N}t{M}（如 gpu_incr_p4t20），与普通页同五态×两模式×SW
#      对照口径（验收「帧数超软件渲染」按页直读，撕裂页不能豁免）。
#      -SkipTearPage 可关（历史口径复现时用）。
#   2) 撕裂自动检测（-SkipTearScan 可关）：三跑两比——
#        gpu_a/gpu_b 两遍 --screenshot（demo 第 3 帧读回后退出）哈希
#        对比（确定性不变量：同参数两跑应逐位一致）；
#        gpu 截图 vs sw 截图像素 diff（GPU 走 FBO 读回=可见性真相源，
#        残影类撕裂恰是 FBO 脏区未覆盖旧区域的遗留内容；SW 走
#        paintImage 干净重渲=无残影参照）得变化像素数/占比/外接框；
#        tear_p4t20.png vs sw 截图 diff = 已知坏例基线幅度（标定）。
#      判读（机器可判部分自动下结论，余者给量化证据）：
#        gpu_a 与坏例几乎一致（diff 占比 < $TearNearIdenticalPct，默认
#        0.05%）⇒ 撕裂特征仍在；
#        gpu-vs-sw 占比 ≥ 坏例基线的 70% ⇒ 与坏例同带（未根治方向）；
#        显著低于基线 ⇒ 好于坏例（是否根治仍需人工复核截图）。
#      撕裂检测不注入新环境键：净化沿用 def 态（移除五键）——测的是
#      默认路径行为，开关归因走矩阵五态，两口径不混。
#
# 第二波口径（沿用）：在第一波冻结口径上只做三处加厚，历史数值口径
# 不变（同格同协议可纵比）：
#   1) 增量口径每格同时采三态 {def, dr1, pbo0}——终测直接量化各开关
#      收益，不再像第一波那样手工跑两遍脚本做开关对照：
#        def  = 不设开关（默认路径：脏区读回 readbackRect；PBO 落地
#               后默认态自动含 PBO 异步读回）
#        dr1  = XGPU_DIRTY_READBACK=1 → 回退每帧全窗读回（排障开关，
#               XWidget.c「置 1 回退」，实测收益 ≈+12% 的反向代价）
#        pbo0 = XGPU_PBO_READBACK=0 → 关 PBO 异步读回（第二波落地前
#               源码无此开关，为空操作，采到的就是基线——报告须标注）
#      每态都显式净化子进程环境（def 移除两键、单态键互斥），不受父
#      会话残留 env 污染；SW 格照跑充对照（开关不触碰软件路径，差值
#      应为 0，正好充当回归证据，沿 night3-final §3 惯例）。
#   2) 页面加 page 5（条目视图，慢页跟踪）：矩阵 4→5 页。
#   3) 新增空闲 CPU 口径：--gpu 普通启动闲置采 TotalProcessorTime
#      （第一波 C 路验证欠账：空闲闸门治理后应从 163% 降到近零），
#      含 XGUI_DEMO_IDLE_GATE=0 回退态对照。
# 闪烁量化（--gpu 普通启动抓 30 帧 + 差分，changed>500 帧对数）原样保留。
#
# 矩阵 = 后端 {默认SW, --gpu} × 页面 {0,2,4,5,6} × 口径 {增量 --benchmark
# （×5 态）, 整帧 --benchmark-full（单态：全窗失效下 readbackRect 的 bbox
# 即整窗、自然退化为全帧读回，开关对照无意义——night3-final §3 已实证）}，
# 每格跑 $Takes 遍取第 2 遍（预热口径，沿 night-2 bench_protocol 惯例）；
# 解析 "benchmark mode=... fps=..." 行。
#
# 锁定防护沿 night-2：exe 复制到 %TEMP%\xgui_bench3.exe 运行（CWD=源
# bin 目录解析 DLL/assets），构建目录 exe 永不被锁；每遍前后 taskkill。
#
# 用法：
#   powershell -NoProfile -File Tools\night3_bench.ps1               # 全协议
#   powershell -NoProfile -File Tools\night3_bench.ps1 -SkipFlicker  # 只跑矩阵+空闲
#   powershell -NoProfile -File Tools\night3_bench.ps1 -SkipMatrix   # 闪烁+空闲
#   powershell -NoProfile -File Tools\night3_bench.ps1 -SkipIdle     # 免空闲口径
#   powershell -NoProfile -File Tools\night3_bench.ps1 -SkipTearPage -SkipTearScan
#                                                    # 关撕裂两口径（复现历史口径）
#   预演/抽测另可 -Takes 1 -Seconds 2 与独立 -LogDir/-SummaryPath/
#   -CapsDir/-TearCapsDir（不覆盖终测产物）。
# 输出：$SummaryPath（文本）+ stdout 的 markdown 表（终测直接引用）：
#   主表 = 各格默认态（与第一波表同构，可直接纵比）；次表 = 增量口径
#   五态对照（收益 = (def-fallback)/fallback，正值 = 默认路径更快）。
# =====================================================================
param(
    [string]$Exe = "D:\code\CMake\Container\bin\XGuiWindowDemo_Test.exe",
    [int]$Seconds = 5,                # 每遍栅长（--benchmark N）
    [int]$Takes = 2,                  # 每格遍数，取第 2 遍
    [int[]]$Pages = @(0, 2, 4, 5, 6), # page 5=条目视图（慢页跟踪）
    [switch]$SkipMatrix,              # 跳过矩阵（只跑闪烁+空闲）
    [switch]$SkipFlicker,             # 跳过闪烁量化（只跑矩阵+空闲）
    [switch]$SkipIdle,                # 跳过空闲 CPU 口径
    [int]$FlickerPage = 4,            # 闪烁量化页面（要求"任意页"，默认图表页最苛刻）
    [int]$FlickerFrames = 30,
    [int]$FlickerChangedThreshold = 500,
    [int]$IdleSeconds = 5,            # 空闲采样窗长（ΔTotalProcessorTime/Δ墙钟）
    [int]$IdleSettleSeconds = 3,      # 启动后落窗+稳定时长（不计入采样）
    [switch]$SkipTearPage,            # 跳过撕裂页帧率格（p4t20，第四波增补）
    [switch]$SkipTearScan,            # 跳过撕裂自动检测（三跑两比，第四波增补）
    [int]$TearPage = 4,               # 撕裂复现页（主会话 23:2x 实锤参数）
    [int]$TearTab = 20,               # 撕裂复现内层页签（坏例 tear_p4t20.png 同参）
    [string]$TearBaselinePng = "D:\code\CMake\Container\Tools\tear_p4t20.png",
                                      # 已知坏例基线（标定 gpu-vs-sw diff 幅度）
    [string]$TearCapsDir = "D:\code\CMake\Container\Tools\tear-night3",
                                      # 撕裂检测三跑截图/日志落盘目录
    [int]$TearPixelTol = 8,           # 像素 diff 每通道容差（0-255，滤 AA/采样噪声）
    [double]$TearNearIdenticalPct = 0.05,
                                      # 「几乎一致」判定阈值（变化像素占比 %）——
                                      # gpu_a vs 坏例低于此值 ⇒ 撕裂特征仍在
    [string]$BenchTag = "xgui_bench3",
                                      # TEMP 副本名基（判崩协议：跨 lane 并跑时各传
                                      # 唯一名如 xgui_bench3_r4，防 taskkill 互杀；
                                      # 默认名不变=历史口径不变）
    [string]$ToolsDir = "D:\code\CMake\Container\Tools\windows\diag",
    [string]$LogDir = "D:\code\CMake\Container\Tools\bench-night3-logs",
    [string]$CapsDir = "D:\code\CMake\Container\Tools\caps-night3",
    [string]$SummaryPath = "D:\code\CMake\Container\Tools\bench-night3-results.txt",
    # 三波 K 路开关名可配置位（K 报告若用别名，终测以此覆盖键名重跑，
    # 脚本零改动——K 报告为准，本默认名是 F 路移交的暂定名）：
    [string]$KFboSwitch = "XGPU_FBO_PERSIST",      # fbo0 态注入键（=0）
    [string]$KDispatchSwitch = "XGPU_DISPATCH_MERGE" # dsp0 态注入键（=0）
)

$ErrorActionPreference = "Continue"
$binDir = Split-Path -Parent $Exe
$benchExe = Join-Path $env:TEMP "$BenchTag.exe"
$killNames = @("$BenchTag.exe", "XGuiWindowDemo_Test.exe")

# 增量口径五态（第三波；键名进 cell/日志名；def 无后缀，历史日志名不变）。
# env 表值 $null = 从子进程环境移除该键（净化，防父会话残留污染）。
# 五态互斥：每态只注入自己的键，其余四键（二波两键+K 路两键）一律移除
# ——K 开关落地重编后，历史三态的口径也自动含 K 键净化，不残留父会话值。
$IncrStates = @(
    @{ key = "def";  env = @{ "XGPU_DIRTY_READBACK" = $null; "XGPU_PBO_READBACK" = $null; $KFboSwitch = $null;     $KDispatchSwitch = $null } },
    @{ key = "dr1";  env = @{ "XGPU_DIRTY_READBACK" = "1";   "XGPU_PBO_READBACK" = $null; $KFboSwitch = $null;     $KDispatchSwitch = $null } },
    @{ key = "pbo0"; env = @{ "XGPU_DIRTY_READBACK" = $null; "XGPU_PBO_READBACK" = "0";   $KFboSwitch = $null;     $KDispatchSwitch = $null } },
    @{ key = "fbo0"; env = @{ "XGPU_DIRTY_READBACK" = $null; "XGPU_PBO_READBACK" = $null; $KFboSwitch = "0";       $KDispatchSwitch = $null } },
    @{ key = "dsp0"; env = @{ "XGPU_DIRTY_READBACK" = $null; "XGPU_PBO_READBACK" = $null; $KFboSwitch = $null;     $KDispatchSwitch = "0" } }
)

function Kill-Leftovers {
    # 残留清理：跑前跑后都清（杀同名进程防串扰/防锁）
    foreach ($n in $killNames) {
        taskkill /F /IM $n 2>$null | Out-Null
    }
}

function Copy-Exe {
    Copy-Item -Force $Exe $benchExe
    if (-not (Test-Path $benchExe)) { throw "copy exe failed: $Exe" }
}

function Run-Bench([string[]]$AppArgs, [string]$LogPath, [hashtable]$EnvVars = $null) {
    # 跑一遍基准：同步等待退出（超时强杀），stdout 落日志，返回日志全文
    # $EnvVars：键值注入子进程；值 $null = 移除该键（态净化用）
    Kill-Leftovers
    Copy-Exe | Out-Null
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $benchExe
    $psi.Arguments = ($AppArgs -join " ")
    $psi.WorkingDirectory = $binDir      # CWD=源 bin：解析 DLL/assets
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    if ($EnvVars) {
        foreach ($k in $EnvVars.Keys) {
            if ($null -eq $EnvVars[$k]) { $psi.EnvironmentVariables.Remove($k) | Out-Null }
            else { $psi.EnvironmentVariables[$k] = [string]$EnvVars[$k] }
        }
    }
    $p = [System.Diagnostics.Process]::Start($psi)
    $timeoutMs = $Seconds * 1000 + 45000
    if (-not $p.WaitForExit($timeoutMs)) {
        Kill-Leftovers
        Write-Host ("TIMEOUT: " + ($AppArgs -join " "))
    }
    $out = $p.StandardOutput.ReadToEnd()
    $err = $p.StandardError.ReadToEnd()
    $p.Dispose()
    ("args: " + ($AppArgs -join " ")) | Out-File -FilePath $LogPath -Encoding utf8
    if ($EnvVars) {
        $envTag = ($EnvVars.Keys | ForEach-Object {
            if ($null -eq $EnvVars[$_]) { "$_-<unset>" } else { "$_=$($EnvVars[$_])" }
        }) -join " "
        ("env: " + $envTag) | Out-File -FilePath $LogPath -Append -Encoding utf8
    }
    $out | Out-File -FilePath $LogPath -Append -Encoding utf8
    if ($err) { $err | Out-File -FilePath $LogPath -Append -Encoding utf8 }
    Kill-Leftovers
    return ($out + "`n" + $err)
}

function Parse-Fps([string]$LogText) {
    # 解析 fps= 行（GBK 输出中该行 ASCII 可靠）；顺带取 frames= 与后端取证
    $fps = $null; $frames = $null; $backend = "sw-or-fallback"
    foreach ($line in ($LogText -split "`r?`n")) {
        if ($line -match "benchmark mode=(\w+) .*frames=(\d+).*fps=([\d.]+)") {
            $frames = $Matches[2]; $fps = [double]$Matches[3]
        }
        if ($line -match "\[xgpu-prof\] driver=(\w+)") { $backend = $Matches[1] }
        if ($line -match "renderer=(\S+)") { $glRenderer = $Matches[1] }
    }
    return @{ fps = $fps; frames = $frames; backend = $backend }
}

function Invoke-IncrCell([object]$Backend, [object]$Mode, [hashtable]$State, [int]$Page, [int]$Tab, [string]$Cell) {
    # 单格执行（$Takes 遍取第 2 遍为采纳遍）：矩阵普通页与撕裂页（p{N}t{M}）
    # 共用同一路径——同净化/同栅长/同日志名规则，保证两类格口径逐位一致可纵比。
    # $Tab<0 = 不传 --tab（普通页，历史日志名不变）。
    $cellKey = if ($State.key -eq "def") { $Cell } else { "$Cell@$($State.key)" }
    $logTag = if ($State.key -eq "def") { "" } else { "-$($State.key)" }
    $fpsLog = @()
    $last = $null
    for ($take = 1; $take -le $Takes; $take++) {
        $logPath = Join-Path $LogDir ("{0}{1}-take{2}.log" -f $Cell, $logTag, $take)
        $appArgs = $Backend.args + $Mode.args + @("--benchmark", "$Seconds", "--page", "$Page")
        if ($Tab -ge 0) { $appArgs += @("--tab", "$Tab") }
        $txt = Run-Bench $appArgs $logPath $State.env
        $last = Parse-Fps $txt
        $fpsLog += ("take{0}={1}" -f $take, $last.fps)
        Write-Host ("{0}{1}: {2}" -f $Cell, $logTag, ($fpsLog -join " "))
    }
    $results[$cellKey] = $last
    $summary.Add(("{0}{1}: {2} fps (adopted take{3}, frames={4}) backend={5}" -f
        $Cell, $logTag, ($fpsLog -join " "), $Takes, $last.frames, $last.backend))
}

# ---------------------------------------------------------------------
$summary = New-Object System.Collections.Generic.List[string]
$summary.Add("XGui night-3 bench summary (wave-3 protocol)")
$summary.Add(("date        : " + (Get-Date -Format "yyyy/MM/dd HH:mm:ss")))
$summary.Add(("source exe  : $Exe (built " + (Get-Item $Exe).LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss") + ")"))
$summary.Add(("seconds/run : $Seconds  ($Takes takes, take $Takes adopted)"))
$summary.Add("incr states : def=unset(默认路径) dr1=XGPU_DIRTY_READBACK=1(回退全窗读回) pbo0=XGPU_PBO_READBACK=0(关PBO,落地前为空操作)")
$summary.Add("                fbo0=$KFboSwitch=0(关FBO常驻,L道) dsp0=$KDispatchSwitch=0(K/L道覆盖键,语义以键名为准——默认关派发合并,经 -KDispatchSwitch 覆盖后为该键自身语义)")
$summary.Add("k-switch note: 开关未编入本 exe 时 fbo0/dsp0 为空操作=基线（报告须标注未生效）")
$summary.Add("idle method : TotalProcessorTime delta / wall, --gpu normal idle, load=%of-one-core")
$summary.Add("tear method : page=$TearPage tab=$TearTab baseline=$TearBaselinePng tol=$TearPixelTol (三跑两比: gpu_a/gpu_b hash + gpu/sw/坏例 两两像素 diff)")
$summary.Add("tear note   : K 开关若未编入 exe，矩阵 fbo0/dsp0 仍为空操作=基线（撕裂检测不受影响，恒测默认路径）")

$results = @{}   # key -> @{fps;frames;backend}；def 态 key 无 @ 后缀（主表用）
if (-not $SkipMatrix) {
    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
    $backends = @( @{ key = "sw";  args = @("--software") },
                   @{ key = "gpu"; args = @("--gpu") } )
    # 口径：增量=仅 --benchmark；整帧=--benchmark-full + --benchmark N
    #（xgui_window_demo.c：--benchmark-full 只置整帧开关，时长由 --benchmark 给）
    $modes = @( @{ key = "incr"; args = @() },
                @{ key = "full"; args = @("--benchmark-full") } )
    foreach ($b in $backends) {
        foreach ($pg in $Pages) {
            foreach ($m in $modes) {
                # 增量口径采五态；整帧口径单态（def）——见文件头注 1)/矩阵说明
                $states = @($IncrStates[0])
                if ($m.key -eq "incr") { $states = $IncrStates }
                foreach ($st in $states) {
                    Invoke-IncrCell $b $m $st $pg (-1) ("{0}_{1}_p{2}" -f $b.key, $m.key, $pg)
                }
            }
        }
        # 撕裂页帧率格（第四波增补）：--page $TearPage --tab $TearTab（主会
        # 话实锤残影复现参数），命名 p{N}t{M}，口径与普通页完全一致——
        # 验收「帧数超软件渲染」在撕裂页同样要过 SW 对照，不豁免。
        if (-not $SkipTearPage) {
            foreach ($m in $modes) {
                $states = @($IncrStates[0])
                if ($m.key -eq "incr") { $states = $IncrStates }
                foreach ($st in $states) {
                    Invoke-IncrCell $b $m $st $TearPage $TearTab ("{0}_{1}_p{2}t{3}" -f $b.key, $m.key, $TearPage, $TearTab)
                }
            }
        }
    }
}

# ---------------------------------------------------------------------
$flickerLine = "flicker: skipped"
if (-not $SkipFlicker) {
    Kill-Leftovers
    if (Test-Path $CapsDir) { Remove-Item -Recurse -Force $CapsDir }
    New-Item -ItemType Directory -Force -Path $CapsDir | Out-Null
    Copy-Exe | Out-Null
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $benchExe
    $psi.Arguments = "--gpu --page $FlickerPage"
    $psi.WorkingDirectory = $binDir
    $psi.UseShellExecute = $false
    $p = [System.Diagnostics.Process]::Start($psi)
    Start-Sleep -Seconds 4        # 等窗口建立
    $p.Refresh()
    $capPid = $p.Id
    Write-Host ("flicker: app pid=$capPid capturing $FlickerFrames frames ...")
    $capOut = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ToolsDir "capture_frames.ps1") -ProcId $capPid -Count $FlickerFrames -OutDir $CapsDir 2>&1
    $capOut | ForEach-Object { Write-Host "  $_" }
    Kill-Leftovers
    $diffOut = & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ToolsDir "diff_frames.ps1") -Dir $CapsDir 2>&1
    $pairs = 0; $over = 0; $maxChanged = 0
    foreach ($line in $diffOut) {
        $s = "$line"
        if ($s -match "changed=(\d+)") {
            $c = [int]$Matches[1]; $pairs++
            if ($c -gt $maxChanged) { $maxChanged = $c }
            if ($c -gt $FlickerChangedThreshold) { $over++ }
            Write-Host ("  " + $s)
        }
    }
    $flickerLine = ("flicker: page=$FlickerPage gpu frames=$FlickerFrames pairs=$pairs " +
        "changed>$FlickerChangedThreshold : $over maxChanged=$maxChanged (capture: " +
        (($capOut | Where-Object { "$_" -match "window|captured" }) -join "; ") + ")")
    $summary.Add($flickerLine)
}

# ---------------------------------------------------------------------
# 撕裂自动检测（第四波增补）：三跑两比。复现参数 = --page $TearPage
# --tab $TearTab（主会话 23:2x 实锤内层页签残影；坏例基线
# $TearBaselinePng）。demo --screenshot 在第 3 帧读回保存后退出
# （xgui_window_demo.c demo_framePumpBell），GPU 路径读 FBO（残影类
# 撕裂=脏区未覆盖旧区域，FBO 遗留内容恰被读出=可见性真相源）；SW 路径
# paintImage 干净重渲=无残影参照。两跑 GPU 哈希对比=确定性不变量。
# 像素 diff 统计（变化数/占比/外接框/行带）由内嵌 C#（LockBits+容差）
# 完成——纯 PS 逐像素循环在 800x600 上慢两个量级，不可接受。
$tearLines = New-Object System.Collections.Generic.List[string]
if (-not $SkipTearScan) {
    $tearCs = @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class XGuiTearDiff {
    // 两图逐像素比（BGRA，A 通道不比——PNG 均不透明）；|通道差|>tol 记变化。
    // 返回 changed/total/pct/bbox/行带（行带=变化像素>行宽5%的连续行段，
    // 按像素数取前5——页签残影预期集中在页签行带，均匀噪点则无集中带）。
    public static string Compare(string pathA, string pathB, int tol) {
        using (Bitmap a = new Bitmap(pathA))
        using (Bitmap b = new Bitmap(pathB)) {
            if (a.Width != b.Width || a.Height != b.Height)
                return string.Format("size-mismatch {0}x{1} vs {2}x{3}",
                    a.Width, a.Height, b.Width, b.Height);
            int w = a.Width, h = b.Height;
            BitmapData da = a.LockBits(new Rectangle(0, 0, w, h),
                ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            BitmapData db = b.LockBits(new Rectangle(0, 0, w, h),
                ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            long changed = 0; long total = (long)w * h;
            long[] rowCnt = new long[h];
            int minX = w, minY = h, maxX = -1, maxY = -1;
            byte[] ra = new byte[w * 4]; byte[] rb = new byte[w * 4];
            for (int y = 0; y < h; y++) {
                Marshal.Copy(new IntPtr(da.Scan0.ToInt64() + y * da.Stride), ra, 0, w * 4);
                Marshal.Copy(new IntPtr(db.Scan0.ToInt64() + y * db.Stride), rb, 0, w * 4);
                for (int x = 0; x < w; x++) {
                    int o = x * 4;
                    if (Math.Abs(ra[o] - rb[o]) > tol ||
                        Math.Abs(ra[o + 1] - rb[o + 1]) > tol ||
                        Math.Abs(ra[o + 2] - rb[o + 2]) > tol) {
                        changed++; rowCnt[y]++;
                        if (x < minX) minX = x; if (x > maxX) maxX = x;
                        if (y < minY) minY = y; if (y > maxY) maxY = y;
                    }
                }
            }
            a.UnlockBits(da); b.UnlockBits(db);
            string bands = "";
            if (maxY >= 0) {
                long bandThr = w / 20; int y0 = -1; long bandPx = 0;
                var found = new System.Collections.Generic.List<long[]>();
                for (int y = 0; y <= h; y++) {
                    bool on = y < h && rowCnt[y] > bandThr;
                    if (on && y0 < 0) { y0 = y; bandPx = 0; }
                    if (on) bandPx += rowCnt[y];
                    if (!on && y0 >= 0) { found.Add(new long[] { y0, y - 1, bandPx }); y0 = -1; }
                }
                found.Sort((u, v) => v[2].CompareTo(u[2]));
                for (int i = 0; i < found.Count && i < 5; i++)
                    bands += string.Format("{0}{1}-{2}(n={3})", i == 0 ? "" : ";",
                        found[i][0], found[i][1], found[i][2]);
                if (found.Count > 5) bands += ";...";
            }
            double pct = total > 0 ? changed * 100.0 / total : 0.0;
            return string.Format("changed={0} total={1} pct={2:F4} bbox=({3},{4})-({5},{6}) bands=[{7}]",
                changed, total, pct, minX, minY, maxX, maxY, bands);
        }
    }
}
"@
    if (-not ("XGuiTearDiff" -as [type])) {
        Add-Type -TypeDefinition $tearCs -ReferencedAssemblies System.Drawing
    }
    New-Item -ItemType Directory -Force -Path $TearCapsDir | Out-Null
    $gpuA = Join-Path $TearCapsDir "tear_gpu_a.png"
    $gpuB = Join-Path $TearCapsDir "tear_gpu_b.png"
    $swC = Join-Path $TearCapsDir "tear_sw.png"
    $defEnv = $IncrStates[0].env   # def 态净化：移除五键，恒测默认路径
    $shotBase = @("--page", "$TearPage", "--tab", "$TearTab")
    $runs = @(
        @{ key = "gpu_a"; args = @("--gpu") + $shotBase + @("--screenshot", $gpuA) },
        @{ key = "gpu_b"; args = @("--gpu") + $shotBase + @("--screenshot", $gpuB) },
        @{ key = "sw";    args = @("--software") + $shotBase + @("--screenshot", $swC) }
    )
    foreach ($r in $runs) {
        $logPath = Join-Path $TearCapsDir ("tear_{0}.log" -f $r.key)
        $txt = Run-Bench $r.args $logPath $defEnv
        $drv = "n/a"
        foreach ($line in ($txt -split "`r?`n")) {
            if ($line -match "\[xgpu-prof\] driver=(\w+)") { $drv = $Matches[1] }
        }
        $shot = $r.args[$r.args.Count - 1]
        $ok = (Test-Path $shot) -and ((Get-Item $shot).Length -gt 0)
        $tearLines.Add(("tear-scan: run={0} shot={1} driver={2}" -f $r.key, $(if ($ok) { "saved" } else { "MISSING" }), $drv))
        Write-Host $tearLines[-1]
    }
    $hashA = $null; $hashB = $null
    if (Test-Path $gpuA) { $hashA = (Get-FileHash -Algorithm SHA256 $gpuA).Hash }
    if (Test-Path $gpuB) { $hashB = (Get-FileHash -Algorithm SHA256 $gpuB).Hash }
    $hashLine = "tear-scan: hash(gpu_a)=((n/a)) hash(gpu_b)=((n/a))"
    if ($hashA -and $hashB) {
        $eq = if ($hashA -eq $hashB) { "EQUAL(逐位一致)" } else { "DIFFER(两跑不一致=非确定渲染)" }
        $hashLine = "tear-scan: hash(gpu_a)={0} hash(gpu_b)={1} compare={2}" -f $hashA.Substring(0, 12), $hashB.Substring(0, 12), $eq
    }
    $tearLines.Add($hashLine); Write-Host $hashLine
    if (Test-Path $gpuA) {
        $dAB = [XGuiTearDiff]::Compare($gpuA, $gpuB, $TearPixelTol)
        $tearLines.Add("tear-scan: diff gpu_a-vs-gpu_b (确定性): $dAB"); Write-Host $tearLines[-1]
        $dGS = [XGuiTearDiff]::Compare($gpuA, $swC, $TearPixelTol)
        $tearLines.Add("tear-scan: diff gpu_a-vs-sw (当前撕裂幅度): $dGS"); Write-Host $tearLines[-1]
        if (Test-Path $TearBaselinePng) {
            $dBS = [XGuiTearDiff]::Compare($TearBaselinePng, $swC, $TearPixelTol)
            $tearLines.Add("tear-scan: diff baseline-vs-sw (已知坏例基线幅度): $dBS"); Write-Host $tearLines[-1]
            $dGB = [XGuiTearDiff]::Compare($gpuA, $TearBaselinePng, $TearPixelTol)
            $tearLines.Add("tear-scan: diff gpu_a-vs-baseline (与坏例一致度): $dGB"); Write-Host $tearLines[-1]
            # 机器可判结论（阈值见文件头第四波口径；证据不足只给数值不下结论）
            if ($dGB -match "pct=([\d.]+)") {
                $pctGB = [double]$Matches[1]
                if ($pctGB -lt $TearNearIdenticalPct) {
                    $tearLines.Add("tear-scan: VERDICT gpu_a≈坏例(pct=$pctGB<$TearNearIdenticalPct) ⇒ 撕裂特征仍在（与已知坏例几乎逐位一致）")
                    Write-Host $tearLines[-1]
                }
                elseif ($pctGB -lt 1.0) {
                    # <1% 且残差成单一带（如 0.4%/2087px@悬浮层小区）= 同帧撕裂
                    # + 活区域（FPS 悬浮层等每帧变字）噪声——撕裂本体仍逐位一致。
                    $tearLines.Add("tear-scan: VERDICT gpu_a 与坏例仅局部差异(pct=$pctGB<1%) ⇒ 残差带（见上行 bands）即活区域噪声，撕裂本体与坏例同帧，人工复核残差带位置")
                    Write-Host $tearLines[-1]
                }
                else {
                    $tearLines.Add("tear-scan: VERDICT gpu_a 与坏例不同帧(pct=$pctGB≥1%) ⇒ 幅度对比看 gpu-vs-sw / baseline-vs-sw 两行")
                    Write-Host $tearLines[-1]
                }
            }
            if ($dGS -match "pct=([\d.]+)") {
                $pctGS = [double]$Matches[1]
                if ($dBS -match "pct=([\d.]+)") {
                    $pctBS = [double]$Matches[1]
                    if ($pctBS -gt 0) {
                        $ratio = [math]::Round($pctGS / $pctBS, 3)
                        $band = if ($ratio -ge 0.7) { "与坏例同带(≥0.7×, 未根治方向)" } else { "低于坏例基线(<0.7×, 好转方向)" }
                        $tearLines.Add("tear-scan: VERDICT gpu-vs-sw 基线比 = $ratio ⇒ $band")
                        Write-Host $tearLines[-1]
                    }
                }
            }
        }
        else {
            $tearLines.Add("tear-scan: 基线缺失 $TearBaselinePng —— 标定不可用，仅 gpu/sw 两跑统计有效")
            Write-Host $tearLines[-1]
        }
    }
    $summary.AddRange($tearLines)
}

# ---------------------------------------------------------------------
# 空闲 CPU 口径（第一波 C 路验证欠账）：--gpu 普通启动（无基准、默认页、
# 不交互），落窗稳定 $IdleSettleSeconds 后采 TotalProcessorTime，静置
# $IdleSeconds 再采；load = ΔCpu/ΔWall×100（单核算术口径，多线程可 >100%
# ——第一波 163% 即此算法，纵比必须同法）。两态：默认（空闲闸门开，
# 治理后预期近零）与 XGUI_DEMO_IDLE_GATE=0（回退旧「帧泵逐轮强制重绘」
# 口径，预期回高位——闸门收益的反向对照）。
$idleLines = New-Object System.Collections.Generic.List[string]
if (-not $SkipIdle) {
    $idleStates = @(
        @{ key = "gate-def"; env = @{ "XGUI_DEMO_IDLE_GATE" = $null } },
        @{ key = "gate0";    env = @{ "XGUI_DEMO_IDLE_GATE" = "0" } }
    )
    $nCores = [Environment]::ProcessorCount
    foreach ($st in $idleStates) {
        Kill-Leftovers
        Copy-Exe | Out-Null
        $psi = New-Object System.Diagnostics.ProcessStartInfo
        $psi.FileName = $benchExe
        $psi.Arguments = "--gpu"
        $psi.WorkingDirectory = $binDir
        $psi.UseShellExecute = $false
        foreach ($k in $st.env.Keys) {
            if ($null -eq $st.env[$k]) { $psi.EnvironmentVariables.Remove($k) | Out-Null }
            else { $psi.EnvironmentVariables[$k] = [string]$st.env[$k] }
        }
        $p = [System.Diagnostics.Process]::Start($psi)
        Start-Sleep -Seconds $IdleSettleSeconds
        if ($p.HasExited) {
            $idleLines.Add(("idle-cpu: state={0} EXITED code={1} (启动即退，无样本)" -f $st.key, $p.ExitCode))
            Write-Host $idleLines[-1]
        }
        else {
            $p.Refresh(); $c0 = $p.TotalProcessorTime; $w0 = Get-Date
            Start-Sleep -Seconds $IdleSeconds
            $p.Refresh(); $c1 = $p.TotalProcessorTime; $w1 = Get-Date
            $cpuSec = [math]::Round(($c1 - $c0).TotalSeconds, 3)
            $wallSec = [math]::Round(($w1 - $w0).TotalSeconds, 3)
            $load = [math]::Round(($c1 - $c0).TotalSeconds / ($w1 - $w0).TotalSeconds * 100.0, 1)
            $idleLines.Add(("idle-cpu: state={0} cpu={1}s wall={2}s load={3}% (单核口径; 系统逻辑核={4}; 100%=满单核)" -f
                $st.key, $cpuSec, $wallSec, $load, $nCores))
            Write-Host $idleLines[-1]
        }
        Kill-Leftovers
    }
    $summary.AddRange($idleLines)
}

$summary | Out-File -FilePath $SummaryPath -Encoding utf8
Write-Host ""
Write-Host "=== summary -> $SummaryPath ==="
$summary | ForEach-Object { Write-Host $_ }

# markdown 主表（默认态，与第一波表同构可纵比；增量格三态对照见次表）
Write-Host ""
Write-Host "| 口径 | 页面 | SW fps | GPU fps | GPU/SW 比值 |"
Write-Host "|---|---|---|---|---|"
foreach ($pg in $Pages) {
    foreach ($m in @("incr", "full")) {
        $swc = $results["sw_${m}_p$pg"]; $glc = $results["gpu_${m}_p$pg"]
        $swF = if ($swc -and $swc.fps) { $swc.fps } else { "n/a" }
        $glF = if ($glc -and $glc.fps) { $glc.fps } else { "n/a" }
        $ratio = "n/a"
        if ($swc -and $glc -and $swc.fps -and $glc.fps -and $swc.fps -gt 0) {
            $ratio = [math]::Round($glc.fps / $swc.fps, 3)
        }
        $mName = if ($m -eq "incr") { "增量" } else { "整帧" }
        Write-Host "| $mName | page $pg | $swF | $glF | $ratio |"
    }
}
# 撕裂页行（第四波增补）：与主表同构——验收「帧数超软件渲染」在撕裂页
# 直读，不豁免（SW 列就是该页的软件渲染对照）。
if (-not $SkipTearPage) {
    $tKey = "${TearPage}t${TearTab}"
    foreach ($m in @("incr", "full")) {
        $swc = $results["sw_${m}_p$tKey"]; $glc = $results["gpu_${m}_p$tKey"]
        $swF = if ($swc -and $swc.fps) { $swc.fps } else { "n/a" }
        $glF = if ($glc -and $glc.fps) { $glc.fps } else { "n/a" }
        $ratio = "n/a"
        if ($swc -and $glc -and $swc.fps -and $glc.fps -and $swc.fps -gt 0) {
            $ratio = [math]::Round($glc.fps / $swc.fps, 3)
        }
        $mName = if ($m -eq "incr") { "增量" } else { "整帧" }
        Write-Host "| $mName | page ${tKey}(撕裂页) | $swF | $glF | $ratio |"
    }
}

# markdown 次表：增量口径三态对照（收益 = (def-fallback)/fallback，
# 正值 = 默认路径更快；SW 行差值应为 ~0，充当天关对照）
function Get-StateFps([string]$Key) {
    $r = $results[$Key]
    if ($r -and $r.fps) { return [double]$r.fps }
    return $null
}
function Format-Benefit($Def, $Fall) {
    # 收益格式化：缺样本/零除一律 n/a，不猜
    if ($null -ne $Def -and $null -ne $Fall -and $Fall -gt 0) {
        $pct = [math]::Round(($Def - $Fall) / $Fall * 100.0, 1)
        return ("{0}{1}%" -f $(if ($pct -gt 0) { "+" } else { "" }), $pct)
    }
    return "n/a"
}
Write-Host ""
Write-Host "增量口径五态对照（def=默认路径 / dr1=XGPU_DIRTY_READBACK=1 全窗读回 / pbo0=XGPU_PBO_READBACK=0 / fbo0=$KFboSwitch=0 关FBO常驻 / dsp0=$KDispatchSwitch=0 关派发合并——K 开关未落地时后两态=基线，须标注）："
Write-Host "| 后端 | 页面 | def | dr1 | pbo0 | fbo0 | dsp0 | 脏区读回收益 (def vs dr1) | PBO 收益 (def vs pbo0) | FBO 常驻收益 (def vs fbo0) | 派发合并收益 (def vs dsp0) |"
Write-Host "|---|---|---|---|---|---|---|---|---|---|---|"
$pageKeys = @($Pages | ForEach-Object { "$_" })
if (-not $SkipTearPage) { $pageKeys += "${TearPage}t${TearTab}" }  # 撕裂页同入五态对照
foreach ($b in @("sw", "gpu")) {
    foreach ($pgKey in $pageKeys) {
        $base = "{0}_incr_p{1}" -f $b, $pgKey
        $fDef = Get-StateFps $base
        $fDr1 = Get-StateFps "$base@dr1"
        $fPbo = Get-StateFps "$base@pbo0"
        $fFbo = Get-StateFps "$base@fbo0"
        $fDsp = Get-StateFps "$base@dsp0"
        $v = { param($x) if ($null -ne $x) { $x } else { "n/a" } }
        Write-Host ("| {0} | page {1} | {2} | {3} | {4} | {5} | {6} | {7} | {8} | {9} | {10} |" -f
            $b, $pgKey, (& $v $fDef), (& $v $fDr1), (& $v $fPbo), (& $v $fFbo), (& $v $fDsp),
            (Format-Benefit $fDef $fDr1), (Format-Benefit $fDef $fPbo),
            (Format-Benefit $fDef $fFbo), (Format-Benefit $fDef $fDsp))
    }
}

# 空闲 CPU 口径（markdown 化输出，终测直接引用）
if (-not $SkipIdle) {
    Write-Host ""
    Write-Host "空闲 CPU（--gpu 普通启动闲置 $IdleSeconds s，TotalProcessorTime 法，单核口径）："
    Write-Host "| 态 | cpu 秒 | 墙钟秒 | load |"
    Write-Host "|---|---|---|---|"
    foreach ($line in $idleLines) {
        if ($line -match "state=(\S+) cpu=([\d.]+)s wall=([\d.]+)s load=([\d.]+)%") {
            Write-Host ("| {0} | {1} | {2} | {3}% |" -f $Matches[1], $Matches[2], $Matches[3], $Matches[4])
        }
        else { Write-Host ("| {0} | - | - | - |" -f $line) }
    }
}

Write-Host ""
Write-Host $flickerLine

# 撕裂自动检测明细（第四波增补，终测直接引用；判读口径见文件头）
if (-not $SkipTearScan) {
    Write-Host ""
    Write-Host ("撕裂自动检测（--page $TearPage --tab $TearTab 三跑两比；像素容差 $TearPixelTol；" +
        "坏例基线 $(Split-Path -Leaf $TearBaselinePng)；截图落盘 $TearCapsDir）：")
    foreach ($line in $tearLines) {
        if ($line -match "^tear-scan: (.+)$") { Write-Host ("- " + $Matches[1]) }
    }
}
