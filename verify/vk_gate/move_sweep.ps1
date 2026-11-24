param([string]$Backend='vulkan',[string]$Tag='sweep',[int]$MinPct=50)
# A/B negative-control gate: sample non-black % continuously DURING a modal
# title-bar drag. BEFORE fix (no WM_MOVE invalidate): 0-1% (FAIL).
# AFTER fix (InvalidateRect on WM_MOVE): 66-99% (PASS).
# PASS iff every sample >= MinPct (a single low sample = blackout).
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class MS1 {
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out R r);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint dx,uint dy,uint d,IntPtr e);
 public struct R { public int L,T,Rt,B; }
}
"@
function Get-Pct([IntPtr]$hh,[string]$snap=""){
  $r=New-Object MS1+R; [void][MS1]::GetWindowRect($hh,[ref]$r)
  $w=$r.Rt-$r.L; $hgt=$r.B-$r.T
  $bmp=New-Object System.Drawing.Bitmap $w,$hgt; $g=[System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L,$r.T,0,0,(New-Object System.Drawing.Size $w,$hgt))
  if($snap -ne ""){$bmp.Save($snap,[System.Drawing.Imaging.ImageFormat]::Png)}
  $nb=0;$tot=0
  for($y=40;$y -lt ($hgt-40);$y+=10){for($x=40;$x -lt ($w-40);$x+=10){$c=$bmp.GetPixel($x,$y);$tot++;if(($c.R+$c.G+$c.B) -gt 90){$nb++}}}
  $g.Dispose();$bmp.Dispose(); return (100.0*$nb/$tot)
}
Get-Process XGuiWindowDemo_Test -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
foreach($k in @('XGPU_BACKEND','XGPU_PRESENT')){Remove-Item Env:$k -ErrorAction SilentlyContinue}
$env:XGPU_BACKEND=$Backend
$p=Start-Process -FilePath 'F:\Code\CMake\XinYueC\bin\XGuiWindowDemo_Test.exe' -ArgumentList '--page','17' -PassThru
Start-Sleep -Milliseconds 4000
$proc=Get-Process -Id $p.Id -ErrorAction SilentlyContinue
if(-not $proc){Write-Output "GATE=$Tag RESULT=NOPROC";exit 2}
$h=$proc.MainWindowHandle
[void][MS1]::ShowWindow($h,9)
for($t=0;$t -lt 5;$t++){[void][MS1]::SetForegroundWindow($h);Start-Sleep -Milliseconds 300;if([MS1]::GetForegroundWindow() -eq $h){break}}
$r=New-Object MS1+R; [void][MS1]::GetWindowRect($h,[ref]$r)
$w=$r.Rt-$r.L; $hgt=$r.B-$r.T
$cx=$r.L+[int]($w/2); $cy=$r.T+14
[MS1]::SetCursorPos($cx,$cy)|Out-Null; Start-Sleep -Milliseconds 150
[MS1]::mouse_event(2,0,0,0,[IntPtr]::Zero); Start-Sleep -Milliseconds 100
$samples=@()
for($s=1;$s -le 20;$s++){
  [MS1]::SetCursorPos(($cx+[int](400*$s/20)),($cy+[int](260*$s/20)))|Out-Null
  Start-Sleep -Milliseconds 120
  $sv=[int](Get-Pct $h); if($sv -lt 10 -and $s -eq 5){$void=Get-Pct $h (Join-Path $PSScriptRoot "sweep_black.png")}; $samples += $sv
}
[MS1]::mouse_event(4,0,0,0,[IntPtr]::Zero)
$min=(($samples|Measure-Object -Minimum).Minimum)
$verdict = if($min -ge $MinPct){'PASS'}else{'FAIL'}
Write-Output ("GATE={0} RESULT={1} backend={2} min={3} samples=[{4}]" -f $Tag,$verdict,$Backend,$min,($samples -join ','))
Get-Process XGuiWindowDemo_Test -ErrorAction SilentlyContinue | Stop-Process -Force
if($verdict -eq 'PASS'){exit 0}else{exit 1}
