# Değişiklik günlüğü

[English](CHANGELOG.md) · **Türkçe**

İndirmeler [Releases](https://github.com/Jlozde/PS5-Thermal-Doctor/releases) sayfasındadır.

## 0.2.3 (2026-11-07)

### Eklenenler
- **Logo.** Web arayüzünün renklerinde, üzerinden nabız çizgisi geçen bir fan. README'de, tarayıcı sekmesinde ve web arayüzü başlığında yer alır.
- **Ana ekran kutucuğu.** Payload başlarken kutucuk yoksa PS5 ana ekranının medya bölümüne PS5 Thermal Doctor eklenir. Kutucuk web arayüzünü PS5 tarayıcısında açar. Güncel olmayan bir kutucuk (başka port, eski sürüm) bir sonraki açılışta yeniden kaydedilir. Diğer homebrew başlatıcıları gibi `sceAppInstUtilAppInstallTitleDir` ile kaydedilir; toplu kurulum çağrısı hiç kullanılmaz.
- `config.ini` içinde ve Ayarlar sekmesinde `home_tile = 1 | 0` (varsayılan açık). Bakım kartı kutucuğun durumunu gösterir.
- `/api/status` içinde `tile`, `tile_code`, `tile_port` ve `tile_want` alanları; `/icon.png` simgeyi sunar.

### Değişenler
- ELF, `libSceAppInstUtil` ile bağlanır ve 512 × 512 simgeyi gömer (yaklaşık 40 KB).

## 0.2.2 (2026-11-07)

### Eklenenler
- **Altı yeni uygulama dili:** İngilizce ve Türkçenin yanına Almanca, Fransızca, İspanyolca, Rusça, Korece ve Basitleştirilmiş Çince. Web arayüzünü, kararları, gerekçeleri, önerileri ve PS5 bildirimlerini kapsar.
- `config.ini` içinde ve Ayarlar sekmesinde `lang = de | fr | es | ru | ko | zh`. `lang = auto` artık bu PS5 sistem dillerini de tanır; diğer sistem dilleri İngilizce kullanır.
- Web arayüzünde ve API'de `?lang=` yeni kodları kabul eder.
- `tools/i18n_texts.py` tüm uygulama metinlerini tek tabloda tutar; `tools/gen_i18n.py` bu tablodan `src/texts.h` dosyasını ve web arayüzü metin tablosunu üretir, her çevirinin İngilizce metinle aynı değerleri aldığını denetler. CI bu denetimi çalıştırır.

### Değişenler
- Web arayüzü başlığındaki **EN / TR** düğmeleri dil menüsüne dönüştü.
- Yüzdeler ve süreler seçilen dilin yazımına uyar (Almanca ve Fransızcada `38 %`, Türkçede `%38`).
- Günlük ve `config.ini` açıklamaları İngilizce ve Türkçe kalır. Başka bir dilde günlük İngilizce gösterilir; Ayarlar sekmesi bunu belirtir.

## 0.2.1 (2026-11-07)

### Eklenenler
- Web arayüzünde **Ayarlar sekmesi**. Her ayarın kısa açıklamalı kendi alanı var; genel, PS5 bildirimleri, ölçüm, oturumlar, karar eşikleri ve referans karşılaştırması olarak gruplanmış. Değişiklikler kaydedilene kadar işaretli kalır ve geri alınabilir. Aralık dışı değerler kaydetmeden önce gösterilir.
- **Sıcaklık birimi:** `temp_unit = c | f`. Varsayılan Celsius; Fahrenheit web arayüzünü ve PS5 bildirimlerini değiştirir. Değerler, eşikler ve günlük °C kalır.
- `/api/status` içinde `temp_unit` alanı.

### Değişenler
- Web arayüzünde iki sekme var: Genel bakış ve Ayarlar. Bakım işlemleri ve günlük Ayarlar sekmesine taşındı.
- Ham `config.ini` metin kutusu web arayüzünden kaldırıldı. Dosya yine elle düzenlenebilir.
- Web arayüzünden kayıt yalnızca değişen anahtarları gönderir; payload bunları kendi ayarlarıyla birleştirir.

## 0.2.0 (2026-11-07)

### Eklenenler
- Kullanıcının baktığı her yerde iki dilli İngilizce / Türkçe metin: web arayüzü, PS5 bildirimleri, günlük, `config.ini` açıklamaları, API metinleri ve araç çıktıları.
- `lang = auto | en | tr` ayarı. `auto`, PS5 sistem dilini izler.
- Web arayüzünde EN / TR düğmesi; seçim tarayıcıda hatırlanır. Adres ayrıca `?lang=en` veya `?lang=tr` kabul eder.
- `/api/status` ve `/api/log` için `?lang=`, ayrıca `lang`, `console_lang`, `config_lang` ve `overall.badge` alanları.
- Release iş akışı: `v*` etiketi gönderilince ELF derlenir ve GitHub Release yayınlanır.
- README'de ekran görüntüleri ve "nasıl çalışır" diyagramı.
- README'de, sürüm notunda ve web arayüzünün alt kısmında sorumluluk reddi: kararlar tahmindir, garanti değildir.

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
