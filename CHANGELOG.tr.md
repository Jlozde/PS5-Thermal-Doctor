# Değişiklik günlüğü

[English](CHANGELOG.md) · **Türkçe**

İndirmeler [Releases](https://github.com/Jlozde/PS5-Thermal-Doctor/releases) sayfasındadır.

## 0.2.0 (2026-11-07)

### Eklenenler
- Kullanıcının baktığı her yerde iki dilli İngilizce / Türkçe metin: web arayüzü, PS5 bildirimleri, günlük, `config.ini` açıklamaları, API metinleri ve araç çıktıları.
- `lang = auto | en | tr` ayarı. `auto`, PS5 sistem dilini izler.
- Web arayüzünde EN / TR düğmesi; seçim tarayıcıda hatırlanır. Adres ayrıca `?lang=en` veya `?lang=tr` kabul eder.
- `/api/status` ve `/api/log` için `?lang=`, ayrıca `lang`, `console_lang`, `config_lang` ve `overall.badge` alanları.
- Release iş akışı: `v*` etiketi gönderilince ELF derlenir ve GitHub Release yayınlanır.
- README'de ekran görüntüleri ve "nasıl çalışır" diyagramı.

### Değişenler
- API'deki karar kodları İngilizce: `OK`, `WATCH`, `CLEAN`, `SERVICE`, `NO_DATA`.
- CI, ps5-payload-sdk v0.45 sürümünü sabitler ve derlemeden önce PC testlerini çalıştırır.

### Düzeltmeler
- CPU güç rayı yalnızca gerçekten inip çıkıyorsa hesaba katılır. Firmware 11.60'ta bu ray tepe değeri tutan bir sayaçtır ve toplam gücü şişiriyordu.
- Yüksek DPI ekranlarda canlı grafikler her çizimde uzuyordu.
- Örnekleme döngüsündeki eski ayarı geri yazan satır, web arayüzünden yapılan ayar değişikliğini geri alabiliyordu.
- PowerShell 5.1 çıktısında Türkçe karakterler bozuluyordu; betikler artık UTF-8 BOM taşıyor.

## 0.1.0 (2026-11-07)

İlk çalışan sürüm; firmware 11.60'lı bir PS5'te çalıştırıldı. Release olarak yayınlanmadı.

- CPU ve SoC sıcaklığı, fan devri, güç rayları ve FPS için 1 Hz örnekleme.
- Isınma süresi, kararlı durum özeti ve oturum başına CSV dosyalarıyla oyun oturumları.
- Mutlak kurallar, referansa göre soğutma talep indeksi ve uzun vadeli eğilimden karar.
- 8088 portunda canlı grafikli ve oturum geçmişli web arayüzü, PS5 bildirimleri.
- WSL'siz Windows derlemesi, payload gönderici, sahte devserver ve PC testleri.
