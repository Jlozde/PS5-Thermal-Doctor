# SPDX-License-Identifier: GPL-3.0-or-later
# Sends an ELF payload to elfldr on the PS5 (a .NET socket instead of netcat).
# ELF payload'ı PS5'teki elfldr'a gönderir (netcat yerine .NET soketi).
#
#   powershell -ExecutionPolicy Bypass -File .\tools\send_payload.ps1 -Ip 192.168.1.50 -Elf .\ps5-thermal-doctor.elf -Watch
#   Port: 9021 (elfldr / etaHEN), 9020 (the exploit page's loader / exploit sayfasının yükleyicisi)
#   Scripts on a network share need "-ExecutionPolicy Bypass" (unsigned script policy).
#   Ağ paylaşımındaki betikler için "-ExecutionPolicy Bypass" gerekir (imzasız betik engeli).
param(
  [Parameter(Mandatory = $true)] [string] $Ip,
  [string] $Elf = "ps5-thermal-doctor.elf",
  [int] $Port = 9021,
  [switch] $Watch,          # after sending, print the payload's stdout (Ctrl+C to quit)
  [int] $WatchSeconds = 0   # >0: listen this many seconds, then quit
)

if (-not (Test-Path $Elf)) { Write-Error "ELF not found / bulunamadı: $Elf"; exit 1 }
# .ProviderPath: on a UNC share plain Resolve-Path returns a provider-prefixed path .NET cannot open
$bytes = [System.IO.File]::ReadAllBytes((Resolve-Path $Elf).ProviderPath)
if ($bytes.Length -lt 64) { Write-Error "ELF empty or broken / boş ya da bozuk: $Elf ($($bytes.Length) bytes / bayt)"; exit 1 }
Write-Host ("{0} ({1:N0} bytes / bayt) -> {2}:{3}" -f (Split-Path $Elf -Leaf), $bytes.Length, $Ip, $Port)

$client = New-Object System.Net.Sockets.TcpClient
try {
  $client.Connect($Ip, $Port)
  $stream = $client.GetStream()
  $stream.Write($bytes, 0, $bytes.Length)
  $stream.Flush()
  Write-Host "Sent / Gönderildi."
  if ($Watch -or $WatchSeconds -gt 0) {
    if ($WatchSeconds -gt 0) { Write-Host "Listening for $WatchSeconds s / Çıktı $WatchSeconds sn dinleniyor..." }
    else { Write-Host "Listening, Ctrl+C to quit / Çıktı dinleniyor, Ctrl+C ile çık..." }
    $deadline = (Get-Date).AddSeconds($WatchSeconds)
    $buf = New-Object byte[] 4096
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $stream.ReadTimeout = 1000
    while ($true) {
      if ($WatchSeconds -gt 0 -and (Get-Date) -gt $deadline) {
        Write-Host "(time is up, closing; the payload keeps running / süre doldu, bağlantı kapatılıyor; payload çalışmaya devam eder)"
        break
      }
      try {
        $n = $stream.Read($buf, 0, $buf.Length)
        if ($n -le 0) { Write-Host "(connection closed / bağlantı kapandı)"; break }
        Write-Host -NoNewline $utf8.GetString($buf, 0, $n)
      } catch [System.IO.IOException] {
        # read timeout: no new data, check the deadline and keep going
        if ($WatchSeconds -le 0) { continue }
      }
    }
  }
} catch {
  Write-Error "Send failed / Gönderilemedi: $($_.Exception.Message)"
  exit 1
} finally {
  $client.Close()
}
