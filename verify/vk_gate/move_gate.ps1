param([string]$Backend = 'vulkan', [string]$Tag = 'move', [int]$Dx = 220, [int]$Dy = 140, [int]$Cycles = 5)
# Deterministic move-window blackout gate, settled frames.
# Drags title bar Dx,Dy in steps (alternating direction), measures non-black
# ratio after each settled frame. Any cycle <50% = blackout regression.
# NOTE: transient during-drag blackout is covered by move_sweep.ps1 (A/B proof).
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class MG1 {
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
if(-not $proc){Write-Output "GATE=$Tag RESULT=NOPROC overall=0";exit 2}
$h=$proc.MainWindowHandle
[void][MG1]::ShowWindow($h,9)
$fgOk=$false
for($t=0;$t -lt 5;$t++){
  [void][MG1]::SetForegroundWindow($h)
  Start-Sleep -Milliseconds 400
  if([MG1]::GetForegroundWindow() -eq $h){$fgOk=$true;break}
}
if(-not $fgOk){Write-Output "GATE=$Tag RESULT=FOCUS_LOST overall=-1"; Get-Process XGuiWindowDemo_Test -ErrorAction SilentlyContinue | Stop-Process -Force; exit 3}
$demoPid = $p.Id
function Test-FgInProc { $fg=[MG1]::GetForegroundWindow(); if($fg -eq [IntPtr]::Zero){return $false}; $pid2=0; [void][MG1]::GetWindowThreadProcessId($fg,[ref]$pid2); return ($pid2 -eq $demoPid) }

function Get-Pct {
  param([int]$L,[int]$T,[int]$W,[int]$H,[string]$Png)
  $bmp=New-Object System.Drawing.Bitmap $W,$H; $g=[System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($L,$T,0,0,(New-Object System.Drawing.Size $W,$H))
  $bmp.Save($Png,[System.Drawing.Imaging.ImageFormat]::Png)
  $nb=0;$tot=0
  for($y=40;$y -lt ($H-40);$y+=6){for($x=40;$x -lt ($W-40);$x+=6){$c=$bmp.GetPixel($x,$y);$tot++;if(($c.R+$c.G+$c.B) -gt 90){$nb++}}}
  $g.Dispose();$bmp.Dispose()
  return (100.0*$nb/$tot)
}

# baseline at original position
$r=New-Object MG1+R; [void][MG1]::GetWindowRect($h,[ref]$r)
$w=$r.Rt-$r.L; $hgt=$r.B-$r.T
$base=Get-Pct $r.L $r.T $w $hgt ("F:\Code\CMake\XinYueC\verify\vk_gate\move_${Tag}_before.png")

$minAfter=100.0
$movedAll=$true
$afterSamples=@()
for($c=1;$c -le $Cycles;$c++){
  $r=New-Object MG1+R; [void][MG1]::GetWindowRect($h,[ref]$r)
  $dir = if($c % 2 -eq 1){1}else{-1}
  $cx=$r.L+[int]($w/2); $cy=$r.T+14
  [MG1]::SetCursorPos($cx,$cy)|Out-Null; Start-Sleep -Milliseconds 150
  [MG1]::mouse_event(2,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 120
  for($s=1;$s -le 8;$s++){
    $nx=$cx+[int]($dir*$Dx*$s/8); $ny=$cy+[int]($dir*$Dy*$s/8)
    [MG1]::SetCursorPos($nx,$ny)|Out-Null; Start-Sleep -Milliseconds 35
  }
  [MG1]::mouse_event(4,0,0,0,[IntPtr]::Zero)
  Start-Sleep -Milliseconds 1500
  if(-not (Test-FgInProc)){Write-Output "GATE=$Tag RESULT=FOCUS_LOST2 cycle=$c base=$($base.ToString('N1'))"; Get-Process XGuiWindowDemo_Test -ErrorAction SilentlyContinue | Stop-Process -Force; exit 3}
  $r2=New-Object MG1+R; [void][MG1]::GetWindowRect($h,[ref]$r2)
  if(($r2.L -eq $r.L) -and ($r2.T -eq $r.T)){$movedAll=$false}
  $after=Get-Pct $r2.L $r2.T $w $hgt ("F:\Code\CMake\XinYueC\verify\vk_gate\move_${Tag}_c${c}.png")
  $afterSamples += $after.ToString('N1')
  if($after -lt $minAfter){$minAfter=$after}
}

$verdict = if($movedAll -and $minAfter -ge 50){'PASS'}else{'FAIL'}
Write-Output ("GATE={0} RESULT={1} base={2:N1} minAfter={3:N1} moved={4} cycles={5} after=[{6}] fg=OK" -f $Tag,$verdict,$base,$minAfter,$movedAll,$Cycles,($afterSamples -join ','))
Get-Process XGuiWindowDemo_Test -ErrorAction SilentlyContinue | Stop-Process -Force
if($verdict -eq 'PASS'){exit 0}else{exit 1}
