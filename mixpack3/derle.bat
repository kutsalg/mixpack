@echo off
chcp 65001 >nul
where gcc >nul 2>&1 || (echo gcc bulunamadi. MinGW-w64 kurup PATH'e ekleyin. & pause & exit /b 1)
if not exist sqlite3.c (echo sqlite3.c bulunamadi. https://sqlite.org/download.html adresinden "amalgamation" zip'ini indirip sqlite3.c ve sqlite3.h dosyalarini bu klasore koyun. & pause & exit /b 1)
echo Derleniyor (SQLite buyuk oldugu icin 1 dakika surebilir)...
gcc -O2 mixpack_gui.c sqlite3.c -o mixpack_gui.exe -lm -lws2_32 && (echo. & echo Derleme tamam. Simdi kontrol calistiriliyor: & mixpack_gui.exe --selftest)
pause
