param([int]$ProcId, [int]$Count = 20, [string]$OutDir = "D:\code\CMake\Container\Tools\caps")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Cap {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT r);
    public struct RECT { public int L, T, R, B; }
}
"@
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$p = Get-Process -Id $ProcId -ErrorAction Stop
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Host "NO WINDOW"; exit 1 }
$r = New-Object Win32Cap+RECT
[Win32Cap]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
Write-Host ("window " + $w + "x" + $ht)
for ($i = 0; $i -lt $Count; $i++) {
    $bmp = New-Object System.Drawing.Bitmap($w, $ht)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Magenta)
    $hdc = $g.GetHdc()
    [Win32Cap]::PrintWindow($h, $hdc, 2) | Out-Null  # 2=PW_RENDERFULLCONTENT
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    $bmp.Save(("$OutDir\f{0:d3}.png" -f $i), [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Start-Sleep -Milliseconds 120
}
Write-Host "captured $Count frames"
