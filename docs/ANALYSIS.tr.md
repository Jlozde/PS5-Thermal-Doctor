# Teşhis mantığı

[English](ANALYSIS.md) · **Türkçe**

Bu belge `src/analysis.c`'nin ne yaptığını ve neden böyle yaptığını anlatır. Tüm eşikler `config.ini`'den değiştirilebilir.

## Fizik

Çip sıcaklığı yaklaşık olarak:

```
T_çip = T_ortam + P × R_th(fan)
```

- `P`: çipin çektiği güç (watt). Oyun ne kadar ağırsa o kadar yüksek.
- `R_th`: ısıl direnç. Fan hızlandıkça düşer. **Toz** (kanatçıklar tıkanır) ve **bozulmuş termal arayüz** (sıvı metal akması veya oksitlenmesi) ile artar.

PS5'in fan kontrolcüsü sıcaklığı sabit tutmaya çalışır, yani soğutma kötüleştikçe fanı açar. Bu yüzden yalnızca sıcaklığa bakmak yanıltır. **Fan, sıcaklık ve güç** birlikte okunmalıdır.

## Oturum

Önde bir oyun göründüğünde oturum başlar, oyun 15 sn görünmeyince biter. İlk `warmup_s` (600 sn) ısınma olarak atlanır; sonraki örnekler **kararlı durum** ortalamasına girer. En az `min_ss_s` (300 sn) kararlı veri yoksa oturum "veri yetersiz" sayılır, ama maks değerler yine de kaydedilir.

Her oturumun özeti:

| Alan | Anlamı |
|---|---|
| `ss_soc`, `ss_cpu` | kararlı durumdaki ortalama SoC (kanal 1–7 en sıcağı) ve CPU sıcaklığı |
| `ss_fan` | kararlı fan yüzdesi |
| `ss_power` | kararlı toplam güç (GPU + bellek + canlıysa CPU rayı) |
| `ss_fps` | ortalama ekran flip hızı |
| `warm_rate` | ilk 5 dakikadaki ısınma hızı, °C/dk |
| `hot_share` | fan ≥ %80 **ve** SoC ≥ 80 °C olan zaman payı |
| `danger_s` | SoC ≥ `hot_soc_c` (95 °C) geçirilen saniye |
| `cdi` | soğutma talep indeksi (aşağıda) |

## 1. Mutlak kurallar

İlk oturumdan itibaren çalışır. Mantık şudur: fan **düşükken** sıcaklığın yüksek olması normaldir, çünkü firmware fanı henüz açmamıştır. Fan **yüksekken** sıcaklık hâlâ yüksekse sistem elindeki tüm soğutmayı kullanıyor ve yetmiyor demektir.

| Karar | Koşul (varsayılan) |
|---|---|
| SERVİS | fan ≥ %85 **ve** SoC ≥ 86 °C; **veya** SoC ≥ 95 °C'de 60 sn+; **veya** fan %5'in altındayken SoC ≥ 70 °C (fan dönmüyor) |
| TEMİZLİK | fan ≥ %70 **ve** SoC ≥ 80 °C; **veya** oturumun en az yarısında fan ≥ %80 ve SoC ≥ 80 °C |
| İZLE | fan ≥ %60 **ve** SoC ≥ 76 °C |
| OK | diğer |

Bu eşikler topluluk ölçümlerine dayanan ilk tahminlerdir: sağlıklı bir PS5 oyun içinde SoC 65–78 °C ve fan %30–45 aralığındadır. Kendi cihazınızdan birkaç oturum toplandıktan sonra `config.ini`'de ayarlanmalıdır.

## 2. Referansla karşılaştırma (CDI)

```
CDI = T_soc + k_fan · (fan% − 40) − k_pow · (P − 150 W)
      k_fan = 0,30 °C / %      k_pow = 0,25 °C / W
```

- Fan terimi: cihaz aynı sıcaklığı daha yüksek fanla tutuyorsa "daha çok çabalıyor" demektir, CDI artar.
- Güç terimi: ağır oyun (daha çok watt) doğal olarak daha sıcaktır, bu yüzden düşülerek normalize edilir. Güç okunamıyorsa bu terim atlanır; o zaman benzer oyunları karşılaştırmak daha doğru olur.

İlk `baseline_sessions` (5) kararlı oturum **referans**tır; ortalaması ve standart sapması saklanır. "Şimdi", referans sonrası son `recent_sessions` (3) oturumun CDI ortalamasıdır.

| Sapma (şimdi − referans) | Karar |
|---|---|
| ≥ +14 °C | SERVİS |
| ≥ +8 °C | TEMİZLİK |
| ≥ +4 °C | İZLE |

Katsayılar da ilk tahmindir ve cihazda ölçülebilir. Aynı oyunda farklı fan eşikleriyle (etaHEN fan ayarı) sıcaklık farkı bölü fan farkı `k_fan`'ı verir. Farklı güç çeken iki oyun `k_pow`'u verir.

**Referans sayacı (epoch):** "Yeni referans" düğmesi sayacı artırır. Eski oturumlar dosyada kalır ama karşılaştırmaya girmez; yeni referans sonraki 5 oturumdan oluşur. Temizlik veya servis sonrası mutlaka kullanılmalıdır; yoksa iyileşme sapma olarak görünmez ve referans kirli kalır.

## 3. Eğilim

14 günden uzun sürede 6+ kararlı oturum varsa CDI'nin zamana göre en küçük kareler eğimi hesaplanır (°C / 30 gün). Ayda +2 °C ve üzeri, kararı en az İZLE'ye yükseltir; çünkü toz birikimi aylar içinde yavaş yavaş görünür.

## Nihai karar

`max(mutlak, göreli, eğilim)`. Web arayüzü her birinin gerekçesini ayrı gösterir.

## FPS neden karar ölçütü değil

PS5 ısınınca FPS düşürmez: fanı açar, yetmezse kapanır. FPS burada yalnızca **yük bağlamı** olarak kaydedilir; 60 fps'e kilitli bir oyun ile aynı oyunun 120 fps modu farklı güç çeker. `/dev/dce` flip sayacı V-sync'e bağlıdır ve 60 Hz'de 60 gösterir.

## Metinlerin dili

Tüm gerekçe, etiket ve öneriler aynı anda hem İngilizce hem Türkçe üretilir (`an_overall` ikisini de doldurur). API, `?lang=` ile istenen dili döndürür. Bildirimler ve günlük `lang` ayarını kullanır.

## Bilinen sınırlar

- Ortam sıcaklığı ölçülemiyor. Yazın 32 °C'lik bir odada kaydedilen oturum, kışın kaydedilene göre doğal olarak birkaç derece sıcak çıkar. Bu yüzden İZLE tek başına alarm değildir.
- CPU güç rayı canlı ölçüm olmayabilir. Bazı konsollar sabit bir bütçe değeri döndürür. FW 11.60'ta ise tepe değeri tutan bir sayaçtır: oyun başında sıfırlanır, sonra yalnızca artar ve oyun kapanınca da yüksek kalır. Kod rayı ancak son 60 sn'de hem en az iki kez artıp hem en az iki kez azalırsa canlı sayar; aksi halde toplamdan çıkarır ve arayüzde `*` ile işaretler.
- Bellek rayları (4–7) denenen konsolda hep 0 döner. Pratikte toplam güç GPU rayıdır.
- SoC kanal 0 bazı ölçümlerde 17 °C'ye kadar düşük ve geç kalan okuma verdi. Bu yüzden "çip sıcaklığı" kanal 1–7'nin en sıcağıdır. Kanal 7 GPU tarafına en yakın sensördür.
- Eşikler PS5 Slim ve Pro'da farklı olabilir; Pro'da SoC daha sıcak ve güç daha yüksektir.
