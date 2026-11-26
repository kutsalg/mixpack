# MixPack Muhasebe (C + SQLite, offline, MIT)

> **Önemli:** Güncelleme yaparken dosyaları tek tek değil, `MixPack_kaynak.zip` paketini komple açıp mevcut klasörün üzerine yazarak kopyalayın.
> Dosyalar birbirine bağlıdır; farklı sürümler karışırsa derleme "Dosya sürümleri uyumsuz" hatası verir.


Stok, cari, alış/satış faturası, kasa/banka, gider ve rapor. İnternet gerektirmez.
Her yıl ayrı dosya: `mixpack_2026.db`, `mixpack_2027.db` … (eski yıllar arşiv olarak durur).

## Yerel masaüstü programı (önerilen) — MixPack.exe
Gerçek Windows penceresi: sol menü, büyük yazı, liste/tablolar, form pencereleri. Tarayıcı ve siyah konsol penceresi yok.
Fatura/ekstre yazdırma sayfası varsayılan tarayıcıda açılır (A4 yazdır / PDF olarak kaydet); program kendisi tarayıcıda çalışmaz.

### Derleme ve kontrol (Windows)
1. MinGW-w64 (gcc) kur ve PATH'e ekle.
2. https://sqlite.org/download.html → "amalgamation" zip'inden `sqlite3.c` ve `sqlite3.h` dosyalarını bu klasöre koy.
3. `derle_yerel.bat` dosyasına çift tıkla. Derler, ardından iki kontrolü çalıştırır:
   - `MixPack.exe --selftest` → `mixpack_selftest.txt` (iş mantığı: fatura, stok, cari, tahsilat, rapor, yedek, yıl devri)
   - `MixPack.exe --uitest`  → `mixpack_uitest.txt` (tüm sayfalar, form ve tablo pencereleri gerçek pencerelerle denenir)
   İkisi de gerçek verine dokunmaz (geçici 2099/2100 yılı kullanır).
4. `MixPack.exe` dosyasına çift tıkla. Veri dosyaları (`mixpack_YYYY.db`) exe'nin yanına yazılır. Belirli yıl: `MixPack.exe 2026`.

## Web (tarayıcı) sürümü — isteğe bağlı
`derle.bat` → `mixpack_gui.exe` (tarayıcıda açılır). Aynı veritabanı ve kurallar.

Arayüz metnini (`ui.html`) değiştirirsen: `python make_ui.py` ile `ui_html.h` ve `mp_selftest.h` yeniden üretilir, sonra tekrar derle.

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
- Windows'ta (MinGW ile) derlendi ve `--selftest` 28/28 denetimden geçti (kullanıcı tarafından doğrulandı). Tarayıcı arayüzü ve yazdırma Windows'ta henüz elle denenmedi; Linux'ta Chromium ile test edildi.
- Alman fatura alanları (§14 UStG) yerleştirildi; mevzuat (ör. E-Rechnung) değişebilir, vergi danışmanınla doğrula.
