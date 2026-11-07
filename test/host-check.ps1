# SPDX-License-Identifier: GPL-3.0-or-later
# On a PC: tests of the portable modules + syntax check of the PS5-only files.
# PC'de: taşınabilir modüllerin testleri + PS5'e özel dosyaların sözdizimi denetimi.
# Needs MinGW gcc on PATH. / Gereksinim: PATH'te MinGW gcc.
$ErrorActionPreference = "Stop"
Set-Location (Split-Path $PSScriptRoot -Parent)
New-Item -ItemType Directory -Force build | Out-Null

$portable = @('src/analysis.c', 'src/history.c', 'src/config.c', 'src/util.c', 'src/i18n.c', 'src/log.c')

Write-Host "== 1/3 Building the diagnosis tests / Teşhis testleri derleniyor" -ForegroundColor Cyan
gcc -std=gnu11 -Wall -Wextra -O1 -pthread -Isrc -o build/test_analysis.exe @portable test/test_analysis.c -lm
if ($LASTEXITCODE -ne 0) { Write-Host "build error / derleme hatası" -ForegroundColor Red; exit 1 }

Write-Host "== 2/3 Running the tests / Testler çalışıyor" -ForegroundColor Cyan
& build/test_analysis.exe
$testRc = $LASTEXITCODE

Write-Host "== 3/3 Syntax check of the PS5 files / PS5 dosyaları sözdizimi denetimi" -ForegroundColor Cyan
gcc -fsyntax-only -std=gnu11 -Wall -Wextra -Isrc -Itest/stubs -include test/stubs/syntax_compat.h `
  src/platform.c src/http.c src/sampler.c src/probe.c src/main.c src/tile.c
$synRc = $LASTEXITCODE

if ($testRc -ne 0 -or $synRc -ne 0) { Write-Host "FAILED / BAŞARISIZ (tests=$testRc, syntax=$synRc)" -ForegroundColor Red; exit 1 }
Write-Host "OK / TAMAM" -ForegroundColor Green
