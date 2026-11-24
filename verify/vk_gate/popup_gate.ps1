param([string]$Backend = 'vulkan', [string]$Tag = 'popup')
# Hardened popup gate: launch, click channel combo, FOREGROUND-VERIFY,
# screenshot, overall>=50% AND popup-area>=20%.
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class PG2 {
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint dx,uint dy,uint d,IntPtr e);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out int pid);
 public struct R { public int L,T,Rt,B; }
}
"@
Get-Process XGuiWindowDemo_Test -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
foreach($k in @('XGPU_BACKEND','XGPU_PRESENT','XGUI_VK_FRAME_TRACE')){Remove-Item Env:$k -ErrorAction SilentlyContinue}
$env:XGPU_BACKEND=$Backend
$p=Start-Process -FilePath 'F:\Code\CMake\XinYueC\bin\XGuiWindowDemo_Test.exe' -ArgumentList '--page','17' -PassThru
Start-Sleep -Milliseconds 4000
$proc=Get-Process -Id $p.Id -ErrorAction SilentlyContinue
if(-not $proc){Write-Output "GATE=$Tag RESULT=NOPROC overall=0 popup=0";exit 2}
$h=$proc.MainWindowHandle
[void][PG2]::ShowWindow($h,9)
$fgOk=$false
for($t=0;$t -lt 5;$t++){
  [void][PG2]::SetForegroundWindow($h)
  Start-Sleep -Milliseconds 400
  if([PG2]::GetForegroundWindow() -eq $h){$fgOk=$true;break}
}
if(-not $fgOk){Write-Output "GATE=$Tag RESULT=FOCUS_LOST overall=-1 popup=-1"; Get-Process XGuiWindowDemo_Test -ErrorAction SilentlyContinue | Stop-Process -Force; exit 3}
$demoPid = $p.Id
function Test-FgInProc { $fg=[PG2]::GetForegroundWindow(); if($fg -eq [IntPtr]::Zero){return $false}; $pid2=0; [void][PG2]::GetWindowThreadProcessId($fg,[ref]$pid2); return ($pid2 -eq $demoPid) }
$r=New-Object PG2+R; [void][PG2]::GetWindowRect($h,[ref]$r)
[PG2]::SetCursorPos(($r.L+280),($r.T+148))|Out-Null; Start-Sleep -Milliseconds 150
[PG2]::mouse_event(2,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 60
[PG2]::mouse_event(4,0,0,0,[IntPtr]::Zero)
Start-Sleep -Milliseconds 1500
# popup open state: foreground must belong to the demo process (popup own HWND is fine)
if(-not (Test-FgInProc)){Write-Output "GATE=$Tag RESULT=FOCUS_LOST2 overall=-1 popup=-1"; Get-Process XGuiWindowDemo_Test -ErrorAction SilentlyContinue | Stop-Process -Force; exit 3}
$w=$r.Rt-$r.L; $hgt=$r.B-$r.T
$bmp=New-Object System.Drawing.Bitmap $w,$hgt; $g=[System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L,$r.T,0,0,(New-Object System.Drawing.Size $w,$hgt))
$bmp.Save("F:\Code\CMake\XinYueC\verify\vk_gate\popup_$Tag.png",[System.Drawing.Imaging.ImageFormat]::Png)
$nb=0;$tot=0
for($y=40;$y -lt ($hgt-40);$y+=6){for($x=40;$x -lt ($w-40);$x+=6){$c=$bmp.GetPixel($x,$y);$tot++;if(($c.R+$c.G+$c.B) -gt 90){$nb++}}}
$overall=100.0*$nb/$tot
$pn=0;$pt=0
for($y=160;$y -lt 270;$y+=3){for($x=240;$x -lt 470;$x+=3){$c=$bmp.GetPixel($x,$y);$pt++;if(($c.R+$c.G+$c.B) -gt 90){$pn++}}}
$popup=100.0*$pn/$pt
$g.Dispose();$bmp.Dispose()
$verdict = if($overall -ge 50 -and $popup -ge 20){'PASS'}else{'FAIL'}
Write-Output ("GATE={0} RESULT={1} overall={2:N1} popup={3:N1} fg=OK" -f $Tag,$verdict,$overall,$popup)
Get-Process XGuiWindowDemo_Test -ErrorAction SilentlyContinue | Stop-Process -Force
if($verdict -eq 'PASS'){exit 0}else{exit 1}
