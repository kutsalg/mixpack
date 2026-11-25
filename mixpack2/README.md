# MixPack Muhasebe (C + SQLite, offline, MIT)

Stok, cari, alış/satış faturası, kasa/banka, gider ve rapor. İnternet gerektirmez.
Her yıl ayrı dosya: `mixpack_2026.db`, `mixpack_2027.db` … (eski yıllar arşiv olarak durur).

## Grafik arayüz (önerilen)
Program açılınca tarayıcıda büyük yazılı, sol menülü bir pencere açar. Arayüz yalnızca bu bilgisayardan
(127.0.0.1) erişilebilir, internete bağlanmaz. Kapatmak için sol alttaki **Kapat** düğmesini kullanın.

### Derleme (Windows)
1. MinGW-w64 (gcc) kur.
2. https://sqlite.org/download.html → "amalgamation" zip'inden `sqlite3.c` ve `sqlite3.h` dosyalarını bu klasöre koy.
3. `gcc -O2 mixpack_gui.c sqlite3.c -o mixpack_gui.exe -lm -lws2_32`
4. `mixpack_gui.exe` çalıştır. (İlk açılışta yıl yoksa otomatik oluşturulur; belirli yıl: `mixpack_gui.exe 2026`)

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
- Windows'ta derlenip denenmedi (test Linux'ta yapıldı). Windows'a özgü kod küçüktür ve `#ifdef _WIN32` ile ayrılmıştır.
- Alman fatura alanları (§14 UStG) yerleştirildi; mevzuat (ör. E-Rechnung) değişebilir, vergi danışmanınla doğrula.
