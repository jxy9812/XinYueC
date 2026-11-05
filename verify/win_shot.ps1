param([string]$Out = "F:\Code\CMake\XinYueC\verify\digital_io\shots\live.png")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class WinRect {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  public struct RECT { public int L,T,R,B; }
}
"@
$p = Get-Process HsfClient -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $p) { Write-Output "no client"; exit 1 }
$r = New-Object WinRect+RECT
[void][WinRect]::GetWindowRect($p.MainWindowHandle, [ref]$r)
[void][WinRect]::SetForegroundWindow($p.MainWindowHandle)
Start-Sleep -Milliseconds 400
$w = $r.R - $r.L
$h = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size $w, $h))
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output ("saved {0} rect={1},{2} {3}x{4}" -f $Out, $r.L, $r.T, $w, $h)
