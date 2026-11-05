param(
    [string]$ProcName = "python",
    [string]$Out = "F:\Code\CMake\XinYueC\verify\digital_io\shots\live_sim.png"
)
# 按窗口矩形截取指定进程的主窗口。
# [为什么不截整屏] 只截这个窗口，避免把桌面其它内容带进验收图。
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinCap {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  public struct RECT { public int L,T,R,B; }
}
"@
$p = Get-Process $ProcName -ErrorAction SilentlyContinue |
     Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Output "no window"; exit 1 }
$r = New-Object WinCap+RECT
[void][WinCap]::GetWindowRect($p.MainWindowHandle, [ref]$r)
[void][WinCap]::SetForegroundWindow($p.MainWindowHandle)
Start-Sleep -Milliseconds 500
$w = $r.R - $r.L
$h = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size $w, $h))
$dir = Split-Path $Out -Parent
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir | Out-Null }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output ("saved {0} {1}x{2}" -f $Out, $w, $h)
