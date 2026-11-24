param([string]$Backend='opengl', [int]$Clicks=20, [string]$Tag='glrep')
# Repeated popup open/close on one session: click combo N times, final screenshot.
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class GR {
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint dx,uint dy,uint d,IntPtr e);
 public struct R { public int L,T,Rt,B; }
}
"@
Get-Process XGuiWindowDemo_Test -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
foreach($k in @('XGPU_BACKEND','XGPU_PRESENT')){Remove-Item Env:$k -ErrorAction SilentlyContinue}
$env:XGPU_BACKEND=$Backend
$p=Start-Process -FilePath 'F:\Code\CMake\XinYueC\bin\XGuiWindowDemo_Test.exe' -ArgumentList '--page','17' -PassThru
Start-Sleep -Milliseconds 4000
$proc=Get-Process -Id $p.Id -ErrorAction SilentlyContinue
if(-not $proc){Write-Output "GATE=$Tag RESULT=NOPROC pct=0";exit 2}
$h=$proc.MainWindowHandle
[void][GR]::ShowWindow($h,9)
$fgOk=$false
for($t=0;$t -lt 5;$t++){[void][GR]::SetForegroundWindow($h);Start-Sleep -Milliseconds 400;if([GR]::GetForegroundWindow() -eq $h){$fgOk=$true;break}}
if(-not $fgOk){Write-Output "GATE=$Tag RESULT=FOCUS_LOST pct=-1"; Get-Process XGuiWindowDemo_Test -ErrorAction SilentlyContinue | Stop-Process -Force; exit 3}
$r=New-Object GR+R; [void][GR]::GetWindowRect($h,[ref]$r)
for($i=0;$i -lt $Clicks;$i++){
  [GR]::SetCursorPos(($r.L+280),($r.T+148))|Out-Null; Start-Sleep -Milliseconds 120
  [GR]::mouse_event(2,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 50
  [GR]::mouse_event(4,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 450
  # click elsewhere to close popup
  [GR]::SetCursorPos(($r.L+600),($r.T+450))|Out-Null; Start-Sleep -Milliseconds 100
  [GR]::mouse_event(2,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 50
  [GR]::mouse_event(4,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 450
}
Start-Sleep -Milliseconds 800
$w=$r.Rt-$r.L; $hgt=$r.B-$r.T
$bmp=New-Object System.Drawing.Bitmap $w,$hgt; $g=[System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,(New-Object System.Drawing.Size $w,$hgt))
$bmp.Save("F:\Code\CMake\XinYueC\verify\vk_gate\repeat_$Tag.png",[System.Drawing.Imaging.ImageFormat]::Png)
$nb=0;$tot=0
for($y=40;$y -lt ($hgt-40);$y+=6){for($x=40;$x -lt ($w-40);$x+=6){$c=$bmp.GetPixel($x,$y);$tot++;if(($c.R+$c.G+$c.B) -gt 90){$nb++}}}
$g.Dispose();$bmp.Dispose()
$pct=100.0*$nb/$tot
$verdict = if($pct -ge 50){'PASS'}else{'FAIL'}
Write-Output ("GATE={0} RESULT={1} pct={2:N1} clicks={3} fg=OK" -f $Tag,$verdict,$pct,$Clicks)
Get-Process XGuiWindowDemo_Test -ErrorAction SilentlyContinue | Stop-Process -Force
if($verdict -eq 'PASS'){exit 0}else{exit 1}
