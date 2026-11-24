param(
  [int]$WaitSec = 125,
  [string]$Backend = 'vulkan',
  [string]$ExtraName = '',
  [string]$ExtraVal = '',
  [string]$Tag = 'gate'
)
# Hardened gate: launch demo, wait, FOREGROUND-VERIFY (retry; FAIL if lost),
# screenshot, non-black ratio. PASS if >= 50% AND window verified foreground.
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class VKG2 {
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out R r);
 public struct R { public int L,T,Rt,B; }
}
"@
Get-Process XGuiWindowDemo_Test -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
foreach($k in @('XGPU_BACKEND','XGPU_PRESENT','XGPU_VK_FRAMES_IN_FLIGHT','XGPU_VK_CANVAS_KEEP','XGPU_VK_BEGIN_RESTORE','XGUI_VK_FRAME_TRACE','XGUI_BENCH_CPU')){Remove-Item Env:$k -ErrorAction SilentlyContinue}
$env:XGPU_BACKEND=$Backend
if($ExtraName){Set-Item Env:$ExtraName $ExtraVal}
$p=Start-Process -FilePath 'F:\Code\CMake\XinYueC\bin\XGuiWindowDemo_Test.exe' -ArgumentList '--page','17' -PassThru
Start-Sleep -Seconds $WaitSec
$proc=Get-Process -Id $p.Id -ErrorAction SilentlyContinue
if(-not $proc){Write-Output "GATE=$Tag RESULT=NOPROC pct=0";exit 2}
$h=$proc.MainWindowHandle
[void][VKG2]::ShowWindow($h,9)
# foreground verify loop: up to 5 tries
$fgOk=$false
for($t=0;$t -lt 5;$t++){
  [void][VKG2]::SetForegroundWindow($h)
  Start-Sleep -Milliseconds 400
  if([VKG2]::GetForegroundWindow() -eq $h){$fgOk=$true;break}
}
if(-not $fgOk){Write-Output "GATE=$Tag RESULT=FOCUS_LOST pct=-1"; Get-Process XGuiWindowDemo_Test -ErrorAction SilentlyContinue | Stop-Process -Force; exit 3}
$r=New-Object VKG2+R; [void][VKG2]::GetWindowRect($h,[ref]$r)
$w=$r.Rt-$r.L; $hgt=$r.B-$r.T
$bmp=New-Object System.Drawing.Bitmap $w,$hgt; $g=[System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,(New-Object System.Drawing.Size $w,$hgt))
$bmp.Save("F:\Code\CMake\XinYueC\verify\vk_gate\gate_$Tag.png",[System.Drawing.Imaging.ImageFormat]::Png)
$nb=0;$tot=0
for($y=40;$y -lt ($hgt-40);$y+=6){for($x=40;$x -lt ($w-40);$x+=6){$c=$bmp.GetPixel($x,$y);$tot++;if(($c.R+$c.G+$c.B) -gt 90){$nb++}}}
$g.Dispose();$bmp.Dispose()
$pct=100.0*$nb/$tot
$verdict = if($pct -ge 50){'PASS'}else{'FAIL'}
Write-Output ("GATE={0} RESULT={1} pct={2:N1} fg=OK ({3})" -f $Tag,$verdict,$pct,$ExtraName)
Get-Process XGuiWindowDemo_Test -ErrorAction SilentlyContinue | Stop-Process -Force
if($verdict -eq 'PASS'){exit 0}else{exit 1}
