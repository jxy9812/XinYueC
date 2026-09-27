param([string]$Dir = "D:\code\CMake\Container\Tools\caps")
$files = Get-ChildItem $Dir -Filter "f*.png" | Sort-Object Name
if ($files.Count -lt 2) { Write-Host "need 2+ frames"; exit 1 }
Add-Type -AssemblyName System.Drawing
$prev = [System.Drawing.Bitmap]::FromFile($files[0].FullName)
for ($i = 1; $i -lt $files.Count; $i++) {
    $cur = [System.Drawing.Bitmap]::FromFile($files[$i].FullName)
    $changed = 0
    $regions = New-Object System.Collections.Hashtable
    for ($y = 0; $y -lt $prev.Height; $y += 3) {
        for ($x = 0; $x -lt $prev.Width; $x += 3) {
            $p1 = $prev.GetPixel($x, $y)
            $p2 = $cur.GetPixel($x, $y)
            $d = [Math]::Abs($p1.R - $p2.R) + [Math]::Abs($p1.G - $p2.G) + [Math]::Abs($p1.B - $p2.B)
            if ($d -gt 30) {
                $changed++
                $cell = "{0},{1}" -f ([int]($x / 100)), ([int]($y / 100))
                if ($regions.ContainsKey($cell)) { $regions[$cell]++ } else { $regions[$cell] = 1 }
            }
        }
    }
    $top = ($regions.GetEnumerator() | Sort-Object Value -Descending | Select-Object -First 4 | ForEach-Object { $_.Key + "(" + $_.Value + ")" }) -join " "
    Write-Host ("frame {0}->{1}: changed={2} regions: {3}" -f ($i-1), $i, $changed, $top)
    $prev.Dispose()
    $prev = $cur
}
$prev.Dispose()
