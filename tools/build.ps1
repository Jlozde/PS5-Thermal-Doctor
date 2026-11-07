# SPDX-License-Identifier: GPL-3.0-or-later
# PS5 Thermal Doctor - build on Windows (ps5-payload-sdk + clang/lld, no WSL needed)
# PS5 Thermal Doctor - Windows'ta derleme (ps5-payload-sdk + clang/lld, WSL gerekmez)
#
#   powershell -ExecutionPolicy Bypass -File .\tools\build.ps1
#   powershell -ExecutionPolicy Bypass -File .\tools\build.ps1 -Sdk C:\sdk\ps5-payload-sdk -LlvmBin "C:\Program Files\LLVM\bin"
#
# Default locations (can also come from environment variables):
#   SDK   : $env:PS5_PAYLOAD_SDK  or  %LOCALAPPDATA%\ps5-toolchain\sdk\ps5-payload-sdk
#   clang : $env:PS5_LLVM_BIN     or  %LOCALAPPDATA%\ps5-toolchain\llvm\bin  or clang on PATH
#
# Uses the same flags as the Makefile and does in PowerShell what the SDK's bin/prospero-clang
# (bash) wrapper does. With clang 20+ the driver adds the crt objects itself
# (SCE_PROSPERO_SDK_DIR); with older clang crt1.o is passed by hand.

[CmdletBinding()]
param(
    [string]$Sdk = $(if ($env:PS5_PAYLOAD_SDK) { $env:PS5_PAYLOAD_SDK } else { Join-Path $env:LOCALAPPDATA 'ps5-toolchain\sdk\ps5-payload-sdk' }),
    [string]$LlvmBin = $(if ($env:PS5_LLVM_BIN) { $env:PS5_LLVM_BIN } else { Join-Path $env:LOCALAPPDATA 'ps5-toolchain\llvm\bin' }),
    [string]$Out = 'ps5-thermal-doctor.elf',
    [switch]$NoStrip,
    [switch]$ShowCommands
)

$ErrorActionPreference = 'Stop'
# GetFullPath: on a UNC share Resolve-Path ".Path" returns a provider-prefixed path that clang cannot use
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Push-Location $root
try {
    # --- tools ---------------------------------------------------------------
    if (-not (Test-Path (Join-Path $Sdk 'toolchain\prospero.mk'))) {
        throw "ps5-payload-sdk not found / bulunamadı: $Sdk  (docs/INSTALL.md)"
    }
    $Sdk = (Resolve-Path $Sdk).ProviderPath
    $lldWrap = Join-Path $Sdk 'win\prospero-lld.exe'
    if (-not (Test-Path $lldWrap)) { throw "win\prospero-lld.exe missing in the SDK / SDK'da yok: $Sdk (SDK zip incomplete or old / eksik ya da eski)" }

    $clang = Join-Path $LlvmBin 'clang.exe'
    if (-not (Test-Path $clang)) {
        $g = Get-Command clang -ErrorAction SilentlyContinue
        if (-not $g) { throw "clang not found / bulunamadı: $LlvmBin  (docs/INSTALL.md -> Windows)" }
        $clang = $g.Source
        $LlvmBin = Split-Path $clang
    }
    $LlvmBin = (Resolve-Path $LlvmBin).ProviderPath
    $strip = Join-Path $LlvmBin 'llvm-strip.exe'

    $verLine = (& $clang --version 2>&1 | Select-Object -First 1) -join ''
    if ($verLine -notmatch 'version\s+(\d+)') { throw "could not read the clang version / clang sürümü okunamadı: $verLine" }
    $clangMajor = [int]$Matches[1]
    Write-Host "SDK   : $Sdk"
    Write-Host "clang : $clang  ($verLine)"

    $python = $null
    foreach ($cand in @('python', 'python3', 'py')) {
        $g = Get-Command $cand -ErrorAction SilentlyContinue
        if ($g) { $python = $g.Source; break }
    }
    if (-not $python) { throw 'python not found, needed to embed web/index.html / python bulunamadı, web/index.html gömmek için gerekli' }

    # The clang driver looks for "prospero-lld" on PATH as the linker; the SDK's win\ folder provides it.
    $env:PATH = "$LlvmBin;$(Join-Path $Sdk 'win');$env:PATH"
    $env:SCE_PROSPERO_SDK_DIR = $Sdk

    # --- sources (same as the Makefile) -------------------------------------
    $srcs = @('src/main.c', 'src/platform.c', 'src/probe.c', 'src/sampler.c', 'src/http.c',
              'src/tile.c', 'src/analysis.c', 'src/history.c', 'src/config.c', 'src/log.c', 'src/util.c', 'src/i18n.c')
    $gen = @('gen/web_index.c', 'gen/icon0.c')

    $cflags = @('-Os', '-std=gnu11', '-Wall', '-Wextra', '-Isrc', '-ffunction-sections', '-fdata-sections',
                '-fno-asynchronous-unwind-tables', '-fno-unwind-tables')
    $ldflags = @('-Wl,--gc-sections')
    $ldadd = @('-lkernel_sys', '-lkernel', '-lSceSystemService', '-lSceAppInstUtil', '-lpthread')

    # common flags added by the bin/prospero-clang wrapper
    $common = @('-target', 'x86_64-sie-ps5',
                '-fvisibility-nodllstorageclass=default',
                '-isysroot', $Sdk,
                '-isystem', (Join-Path $Sdk 'target\include'),
                '-fno-stack-protector', '-fno-plt', '-femulated-tls',
                '-Wno-unused-command-line-argument')
    $linkDirs = @('-L', (Join-Path $Sdk 'target\lib'), '-L', (Join-Path $Sdk 'target\user\homebrew\lib'))

    function Run([string[]]$argv) {
        if ($ShowCommands) { Write-Host ('  > ' + ($argv -join ' ')) -ForegroundColor DarkGray }
        & $argv[0] $argv[1..($argv.Length - 1)]
        if ($LASTEXITCODE -ne 0) { throw "command failed / komut başarısız (exit $LASTEXITCODE): $($argv[0])" }
    }

    # --- 1) embed the web UI as a C array ------------------------------------
    New-Item -ItemType Directory -Force 'gen', 'build' | Out-Null
    Write-Host '[1/3] web/index.html -> gen/web_index.c, assets/icon0.png -> gen/icon0.c'
    Run @($python, 'tools/gen_assets.py', 'web/index.html', 'gen/web_index.c', 'web_index_html')
    Run @($python, 'tools/gen_assets.py', 'assets/icon0.png', 'gen/icon0.c', 'icon0_png')

    # --- 2) compile ----------------------------------------------------------
    Write-Host '[2/3] compiling / derleniyor'
    $objs = @()
    foreach ($s in ($srcs + $gen)) {
        $o = Join-Path $root ('build\' + [IO.Path]::GetFileNameWithoutExtension($s) + '.o')
        Write-Host "      $s"
        Run (@($clang) + $common + $cflags + @('-c', (Join-Path $root $s), '-o', $o))
        $objs += $o
    }

    # --- 3) link -------------------------------------------------------------
    # The SDK's win\prospero-lld.exe wrapper runs through cmd.exe, and cmd cannot use a UNC
    # working directory (project on a network share), so every path is absolute and linking
    # runs from a local directory.
    $outAbs = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }
    Write-Host "[3/3] linking / bağlanıyor -> $Out"
    $crt = @()
    if ($clangMajor -lt 20) { $crt = @((Join-Path $Sdk 'target\lib\crt1.o')) }
    Push-Location $LlvmBin
    try {
        # Same order as bin/prospero-clang: -lc first, crt + SceLibcInternal/SceNet last.
        Run (@($clang) + $common + $linkDirs + @('-lc') + $ldflags + @('-o', $outAbs) + $objs +
             @('--sysroot', $Sdk) + $crt + $ldadd + @('-lSceLibcInternal', '-lSceNet'))
    }
    finally { Pop-Location }

    if (-not $NoStrip -and (Test-Path $strip)) {
        Run @($strip, '--strip-all', $outAbs)
    }
    $len = (Get-Item $outAbs).Length
    Write-Host ("DONE / TAMAM: {0} ({1:N0} bytes / bayt)" -f $outAbs, $len) -ForegroundColor Green
}
finally {
    Pop-Location
}
