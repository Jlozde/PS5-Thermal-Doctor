# Derleme ve kurulum

[English](INSTALL.md) · **Türkçe**

En hızlı yol, hazır `ps5-thermal-doctor.elf` dosyasını [Releases](https://github.com/Jlozde/PS5-Thermal-Doctor/releases/latest) sayfasından indirip doğrudan [PS5'e gönderme](#ps5e-gönderme) adımına geçmektir.

Kendiniz derlemek için üç yol var. Windows'ta en pratiği **A**'dır: iki zip açılır, yönetici hakkı ve WSL gerekmez.

## A) Windows, WSL'siz (`tools\build.ps1`)

İki gereksinim de taşınabilirdir (kurulum yok):

1. **ps5-payload-sdk** zip'i: `https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip`.
   `%LOCALAPPDATA%\ps5-toolchain\sdk\` altına açın. `...\sdk\ps5-payload-sdk\win\prospero-lld.exe` dosyası bulunmalıdır; SDK'nın resmi Windows sarmalayıcıları buradadır.
2. **clang + lld**: [llvm-mingw](https://github.com/mstorsjo/llvm-mingw/releases) sayfasından `llvm-mingw-<tarih>-ucrt-x86_64.zip` (yaklaşık 180 MB).
   Açıp klasörü `%LOCALAPPDATA%\ps5-toolchain\llvm` olarak adlandırın; `llvm\bin\clang.exe` bulunmalıdır.
   Sistemde kurulu LLVM de olur (`winget install LLVM.LLVM`, sürüm 18 veya üstü): `-LlvmBin "C:\Program Files\LLVM\bin"` verin.

```powershell
$t = "$env:LOCALAPPDATA\ps5-toolchain"; New-Item -ItemType Directory -Force "$t\dl" | Out-Null
Invoke-WebRequest https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip -OutFile "$t\dl\sdk.zip"
Expand-Archive "$t\dl\sdk.zip" "$t\sdk"
# llvm-mingw zip'ini de indirip "$t\llvm" olarak açın, sonra:
powershell -ExecutionPolicy Bypass -File .\tools\build.ps1        # -> ps5-thermal-doctor.elf
```

- Farklı konumlar: `-Sdk <yol>` / `-LlvmBin <yol>` ya da `PS5_PAYLOAD_SDK` / `PS5_LLVM_BIN` ortam değişkenleri.
- `-ShowCommands` çalıştırılan komutları gösterir. `-NoStrip` sembolleri bırakır; ayrı dosya için `-Out build\debug.elf` ile birlikte kullanın.
- Betik Makefile ile aynı bayrakları ve SDK'nın `bin/prospero-clang` sarmalayıcısının mantığını kullanır. clang 20+ ile crt nesnelerini sürücü kendisi ekler (`SCE_PROSPERO_SDK_DIR`).
- Proje ağ paylaşımındaysa (`\\sunucu\...`) PowerShell imzasız betikleri "not digitally signed" diye engeller. Bu yüzden `powershell -ExecutionPolicy Bypass -File ...` biçimi kullanılır. Kalıcı çözüm `Set-ExecutionPolicy -Scope CurrentUser Bypass`'tır; bu güvenlik tercihi size aittir.
- Denenen bileşim: SDK v0.45 (2026-10-21) + llvm-mingw 20261104 (clang 23.1.5), sonuç ~260 KB ELF.

## B) GitHub Actions

1. Bu klasörü bir GitHub deposuna gönderin.
2. Actions sekmesinde "Build PS5 payload" iş akışı çalışır. Bitince `ps5-thermal-doctor.elf` dosyasını **Artifacts → ps5-thermal-doctor** içinden indirin.
3. İş akışı ayrıca PC testlerini de çalıştırır (`make test`).
4. `v0.2.0` gibi bir etiket gönderildiğinde ELF, `SHA256SUMS.txt` ve `.github/release-notes/<etiket>.md` notlarıyla bir GitHub Release yayınlanır. Etiket `src/ps5td.h` içindeki `PS5TD_VERSION` ile aynı olmalıdır.

## C) WSL (Windows) veya Linux

```powershell
wsl --install -d Ubuntu          # bir kez; yönetici hakkı ve yeniden başlatma ister
```

Ubuntu içinde:

```bash
sudo apt-get update
sudo apt-get install -y bash clang-18 lld-18 wget unzip make python3
wget https://github.com/ps5-payload-dev/sdk/releases/latest/download/ps5-payload-sdk.zip
sudo unzip -d /opt ps5-payload-sdk.zip
echo 'export PS5_PAYLOAD_SDK=/opt/ps5-payload-sdk' >> ~/.bashrc && source ~/.bashrc

cd "/mnt/<proje yolu>"           # ağ paylaşımı için: sudo mkdir /mnt/ps5 && sudo mount -t drvfs '\\sunucu\paylasim\ps5-thermal-doctor' /mnt/ps5
make                              # -> ps5-thermal-doctor.elf
make test                         # PC testleri
make deploy PS5_HOST=192.168.1.50 # elfldr'a gönder (port 9021)
```

SDK v0.45, firmware 3.00–14.10 arası kernel ofsetlerini içerir. SDK'nın tanımadığı bir firmware'de payload `main()`'e gelmeden sessizce çıkar. Yeni firmware için SDK'yı güncelleyin.

## PS5'e gönderme

- Konsolda jailbreak ve etaHEN (ya da yalnızca elfldr) yüklü olmalı.
- PowerShell:
  ```powershell
  powershell -ExecutionPolicy Bypass -File .\tools\send_payload.ps1 -Ip <PS5 IP> -Elf .\ps5-thermal-doctor.elf -Watch
  ```
  `-Watch` payload'ın günlük çıktısını canlı gösterir; Ctrl+C ile çıkılır, payload çalışmaya devam eder. `-WatchSeconds 60` o kadar süre dinleyip kendiliğinden çıkar.
- SDK'nın kendi aracı da olur: `<sdk>\win\prospero-deploy.cmd -h <PS5 IP> ps5-thermal-doctor.elf`
- Linux/macOS: `nc -q0 <PS5 IP> 9021 < ps5-thermal-doctor.elf`
- Başarılıysa ekranda "PS5 Thermal Doctor 0.2.0 başladı · http://<ip>:8088" bildirimi çıkar.

Payload ikinci kez gönderilirse eski örnek kendiliğinden durdurulur: pid'i port üzerinden bulunur ve sonlandırılır.

## Dil

- Web arayüzü: başlıktaki dil menüsü. İngilizce, Türkçe, Almanca, Fransızca, İspanyolca, Rusça, Korece ve Basitleştirilmiş Çince seçilebilir.
- PS5 bildirimleri: `/data/ps5td/config.ini` içindeki `lang = auto | en | tr | de | fr | es | ru | ko | zh`. Web arayüzünün Ayarlar sekmesinden de değiştirilebilir. `auto`, PS5 sistem dilini izler; uygulamada olmayan bir dil İngilizceye düşer.
- Günlük ve payload çıktısı konsol dili Türkçe ise Türkçe, değilse İngilizcedir.
- Açılış günlüğü seçilen dili yazar, örneğin `Dil: tr (ayar auto, PS5 sistem dili kodu 19).`

## Sıcaklık birimi

- Varsayılan Celsius'tur. Fahrenheit'ı Ayarlar sekmesinden seçin ya da `config.ini`'de `temp_unit = f` yazın.
- Birim web arayüzünü ve PS5 bildirimlerini değiştirir. Günlük ve `config.ini`'deki eşikler her zaman °C kalır; Ayarlar sekmesi bunları gösterirken dönüştürür.

## Otomatik başlatma (etaHEN)

ELF'i `/data/etaHEN/payloads/` altına kopyalayın (FTP, port 1337). etaHEN Toolbox → Payloads menüsünde dosyayı seçip **auto-start**'ı açın; etaHEN her açılışta yükler. Oyun açmadan önce yüklenmiş olması yeterlidir. Oyun sırasında da yüklenebilir; o oturum kısmi kaydedilir.

## Sorun giderme

| Belirti | Olası neden |
|---|---|
| Bildirim gelmedi, port açılmadı | SDK bu firmware'i tanımıyor. `-Watch` ile çıktıya bakın; hiç çıktı yoksa payload main'e gelmedi. |
| Fan `–` | `sceKernelGetCurrentFanDuty` bu firmware'de yok; günlükte "FanDuty:YOK" yazar. |
| FPS `–` | `/dev/dce` açılamadı (günlükte errno). Yetki yükseltme kapalıysa `escalate = 1` deneyin. |
| Güç `–` | `sceKernelGetSocPowerConsumption` yok ya da beklenmeyen yerleşimde (günlüğe bakın). |
| Oyun açıkken "yok" görünüyor | `sceSystemServiceGetAppIdOfRunningBigApp` yanıt vermiyor. pfsmnt yedeği de boşsa günlüğü paylaşın. |
| Web arayüzü açılmıyor | PS5 ile aynı ağda mısınız? Port `config.ini`'de değiştirilmiş olabilir. |
| Metinler yanlış dilde | Web arayüzü: başlıktaki dil menüsü. Bildirimler: Ayarlar sekmesindeki **Konsol dili** (örneğin `lang = tr`). |

Günlük: `http://<ip>:8088/api/log?lang=tr` veya `/data/ps5td/ps5td.log`.
