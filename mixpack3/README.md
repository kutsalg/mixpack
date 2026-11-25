# MixPack Muhasebe (C + SQLite, offline, MIT)

Stok, cari, alış/satış faturası, kasa/banka, gider ve rapor. İnternet gerektirmez.
Her yıl ayrı dosya: `mixpack_2026.db`, `mixpack_2027.db` … (eski yıllar arşiv olarak durur).

## Grafik arayüz (önerilen)
Program açılınca tarayıcıda büyük yazılı, sol menülü bir pencere açar. Arayüz yalnızca bu bilgisayardan
(127.0.0.1) erişilebilir, internete bağlanmaz. Kapatmak için sol alttaki **Kapat** düğmesini kullanın.

### Derleme ve kontrol (Windows)
1. MinGW-w64 (gcc) kur ve PATH'e ekle.
2. https://sqlite.org/download.html → "amalgamation" zip'inden `sqlite3.c` ve `sqlite3.h` dosyalarını bu klasöre koy.
3. `derle.bat` dosyasına çift tıkla (veya elle: `gcc -O2 mixpack_gui.c sqlite3.c -o mixpack_gui.exe -lm -lws2_32`).
   Derleme bitince kendi kendini sınama otomatik çalışır; sonda "SONUÇ: Tüm denetimler başarılı." yazmalı.
4. `mixpack_gui.exe` çalıştır. (Belirli yıl: `mixpack_gui.exe 2026`.) Veri dosyaları exe'nin yanına yazılır.

`mixpack_gui.exe --selftest` gerçek verine dokunmaz (geçici `mixpack_2099.db` ve `mixpack_2100.db` kullanır, bitince siler).
Windows Güvenlik Duvarı/SmartScreen uyarısı çıkarsa: program yalnızca 127.0.0.1'e bağlanır, internete çıkmaz; imzasız exe için SmartScreen "Yine de çalıştır" diyebilir.

Arayüz metnini (`ui.html`) değiştirirsen: `python make_ui.py` ile `ui_html.h` yeniden üretilir, sonra tekrar derle.

## Konsol sürümü (isteğe bağlı)
`gcc -O2 mixpack.c sqlite3.c -o mixpack.exe -lm`

## Kaynakça
- SQLite (kamu malı / public domain) – https://sqlite.org – veritabanı motoru.
- HTTP sunucusu ve arayüz sıfırdan yazıldı, başka kod kopyalanmadı. Lisans: MIT.

## Kurallar (veritabanı seviyesinde zorlanır)
- Fatura/ödeme/gider kaydı silinemez ve değiştirilemez, yalnızca iptal edilir.
- Fatura no: satışta YY-AA-NN (her ay sıfırlanır), aynı no iki kez verilemez (iptaller dahil).
- Stok, cari ve kasa/banka hareketleri faturadan türetilir; iptalde otomatik geri alınır.
- Kâr: ortalama maliyet yöntemi, KDV hariç net ciro üzerinden.

## Varsayımlar / sınırlar
- Fiyatlar KDV DAHİL girilir (ürün başına KDV %, varsayılan 7). Faturada Netto/MwSt/Brutto ayrımı yazılır.
- Fatura çıktısı A4 HTML sayfasıdır: sayfadaki "Yazdır / PDF olarak kaydet" düğmesi ile yazdırılır veya PDF yapılır.
- Tahsilat bakiye bazlıdır (kısmi ödeme çalışır); tek tek faturaya dağıtma yok.
- Windows'ta derlenip ÇALIŞTIRILMADI (geliştirme ortamında Windows yok; test Linux'ta yapıldı, Windows kod yolları yalnızca tip denetiminden geçti). Gerçek doğrulama için `--selftest` çıktısını paylaş.
- Alman fatura alanları (§14 UStG) yerleştirildi; mevzuat (ör. E-Rechnung) değişebilir, vergi danışmanınla doğrula.
