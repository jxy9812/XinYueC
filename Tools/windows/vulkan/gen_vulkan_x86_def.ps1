# Build the x86 Vulkan import library's stdcall alias layer.
#
# Problem: on 32-bit Windows vulkan.h declares vk functions as __stdcall,
# so the linker looks for symbols like _vkCreateInstance@12, while the
# loader DLL exports undecorated names. lib /def cannot express "symbol
# _name@N bound to DLL export name" (the recorded export name always
# mirrors the entry name verbatim), so def aliasing is not viable.
#
# Solution: real assembly thunks. For every stdcall-decorated symbol found
# in the compiler's own object symbols (step 1), emit
#     _vkCreateInstance@12:  jmp _vkCreateInstance
# The plain import library provides _vkCreateInstance (a jmp [__imp_] thunk
# to the real stdcall DLL function, which cleans its own stack), so the
# tail-jmp preserves the stdcall ABI. Assemble with ml.exe and merge the
# object into the plain import library.
$ErrorActionPreference = 'Stop'
$root = 'D:\code\CMake\Container\Tools\windows\vulkan'
$imp  = Join-Path $root 'importlib'
$inc  = Join-Path $root 'headers\Vulkan-Headers-1.3.290\include'
$ml   = 'C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x86\ml.exe'

# Function names declared in the C headers relevant on Windows (core +
# win32 platform extensions). Platform headers for other OSes are excluded:
# their prototypes sit behind #ifdef guards that are not active here.
$hdrFiles = @('vulkan_core.h', 'vulkan_win32.h')
$hdrs = $hdrFiles | ForEach-Object { Get-Item (Join-Path $inc ('vulkan\' + $_)) }
$names = foreach ($h in $hdrs) {
    $raw = $h | Get-Content -Raw
    [regex]::Matches($raw, 'VKAPI_CALL\s+(vk[A-Za-z0-9_]+)\s*\(') |
        ForEach-Object { $_.Groups[1].Value }
}
$names = $names | Select-Object -Unique

# Restrict to names actually exported by the DLL (the plain x86 def,
# generated from dumpbin of SysWOW64 vulkan-1.dll).
$plain = Get-Content (Join-Path $imp 'vulkan-1-x86-plain.def') |
    Where-Object { $_ -match '^vk' }
$exported = @{}; foreach ($p in $plain) { $exported[$p] = $true }
$refs = $names | Where-Object { $exported.ContainsKey($_) }
Write-Host ("ref functions in DLL exports: " + $refs.Count)

# --- step 1: get the real stdcall decorations from the compiler ---
$c = @('#include <vulkan/vulkan.h>', 'const void *g_vulkan_refs[] = {')
foreach ($n in $refs) { $c += '    (const void *)&' + $n + ',' }
$c += '};'
$c | Set-Content (Join-Path $imp 'vk_alias_gen.c') -Encoding ASCII

Push-Location $imp
try {
    $clOut = & cl /nologo /c /W0 /DVK_USE_PLATFORM_WIN32_KHR /I $inc vk_alias_gen.c 2>&1
    if ($LASTEXITCODE -ne 0) {
        $clOut | ForEach-Object { Write-Host $_ }
        throw ("cl failed: " + $LASTEXITCODE)
    }

    $syms = & dumpbin /symbols vk_alias_gen.obj |
        Where-Object { $_ -match 'External\s+\|\s+(_vk[A-Za-z0-9_]+@\d+)\s*$' } |
        ForEach-Object { $Matches[1] } | Sort-Object -Unique
    Write-Host ("decorated stdcall symbols: " + $syms.Count)
    if ($syms.Count -lt 100) { throw "implausibly few decorated symbols" }

    # --- step 2: emit jmp thunks ---
    $asm = @('.686', '.model flat', 'OPTION CASEMAP:NONE', '.code')
    foreach ($s in $syms) {
        if ($s -match '^_([^@]+)@\d+$') {
            $base = $Matches[1]
            $asm += ('EXTERN _' + $base + ':PROC')
            $asm += ('PUBLIC _' + $base + '@' + ($s -replace '.*@', ''))
            $asm += ('_' + $base + '@' + ($s -replace '.*@', '') + ':')
            $asm += ('    jmp _' + $base)
        }
    }
    $asm += 'END'
    $asm | Set-Content (Join-Path $imp 'vk_x86_stubs.asm') -Encoding ASCII

    $mlOut = & $ml /nologo /c /Cp vk_x86_stubs.asm 2>&1
    if ($LASTEXITCODE -ne 0) {
        $mlOut | Select-Object -First 20 | ForEach-Object { Write-Host $_ }
        throw ("ml failed: " + $LASTEXITCODE)
    }
    Write-Host "stubs assembled: vk_x86_stubs.obj"
}
finally { Pop-Location }
