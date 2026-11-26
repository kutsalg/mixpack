@echo off
chcp 65001 >nul
where gcc >nul 2>&1 || (echo gcc bulunamadi. MinGW-w64 kurup PATH'e ekleyin. & pause & exit /b 1)
if not exist sqlite3.c (echo sqlite3.c bulunamadi. https://sqlite.org/download.html adresinden "amalgamation" zip'ini indirip sqlite3.c ve sqlite3.h dosyalarini bu klasore koyun. & pause & exit /b 1)
echo Derleniyor (SQLite buyuk oldugu icin 1 dakika surebilir)...
set RES=
windres mixpack.rc -O coff -o mixpack_res.o >nul 2>&1 && set RES=mixpack_res.o
if "%RES%"=="" echo (Uyari: windres bulunamadi; program calisir ama modern gorunum/yuksek DPI destegi olmaz.)
gcc -O2 -mwindows mixpack_win.c sqlite3.c %RES% -o MixPack.exe -lm -lcomctl32 -lgdi32 -luser32 -lshell32
if errorlevel 1 (echo. & echo DERLEME HATASI - yukaridaki mesajlari bana iletin. & pause & exit /b 1)
echo Derleme tamam. Kontroller calistiriliyor...
start /wait "" MixPack.exe --selftest
start /wait "" MixPack.exe --uitest
echo.
echo Sonuclar: mixpack_selftest.txt (cekirdek) ve mixpack_uitest.txt (pencereler)
type mixpack_selftest.txt | findstr /C:"SONUÇ" /C:"HATA"
type mixpack_uitest.txt
echo.
echo Programi baslatmak icin MixPack.exe dosyasina cift tiklayin.
pause
