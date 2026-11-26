/* MixPack Muhasebe - YEREL (masaüstü) arayüz. Windows'ta gerçek pencereler (Win32), Linux'ta test için metin modu.
 * Mantık: mixpack.c (veri/kurallar) + mp_api.h (doğrulanmış işlemler) + mp_core.h (sayfalar/formlar) + araç takımı.
 * Derleme (Windows, MinGW): gcc -O2 -mwindows mixpack_win.c sqlite3.c [mixpack_res.o] -o MixPack.exe -lm -lcomctl32 -lgdi32 -luser32 -lshell32
 * Kendi kendini sınama: MixPack.exe --selftest   (sonuç: mixpack_selftest.txt)   |   Pencere sınaması: MixPack.exe --uitest (sonuç: mixpack_uitest.txt)
 * Kaynakça: SQLite (kamu malı) https://sqlite.org ; Win32 API (Microsoft) - yalnızca belgelenmiş standart çağrılar. Lisans: MIT. */
#ifdef _WIN32
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#endif
#include <time.h>
#define main cli_main
#include "mixpack.c"
#undef main
#include "mp_api.h"
#include "mp_core.h"
#include "mp_tk_fake.h"
#include "mp_selftest.h"
#if MP_CORE_VERSION != 6 || MP_FAKE_VERSION != 6
#error "Dosya surumleri uyumsuz: mp_core.h / mp_tk_fake.h / mixpack_win.c dosyalarini AYNI paketten (zip) kopyalayin."
#endif
#ifdef _WIN32
#include "mp_tk_win.h"
#if MP_TK_VERSION != 6
#error "Dosya surumleri uyumsuz: mp_tk_win.h farkli surumde. Tum dosyalari AYNI paketten (zip) kopyalayin."
#endif
static int script_mode; /* --selftest: formlar pencere açmadan senaryodan yanıtlanır */
static void tk_msg(const char *t, const char *x) { if (script_mode) fk_msg(t, x); else wk_msg(t, x); }
static int tk_confirm(const char *t, const char *x) { return script_mode ? fk_confirm(t, x) : wk_confirm(t, x); }
static int tk_form(Form *f) { return script_mode ? fk_form(f) : wk_form(f); }
static int tk_table(const char *t, Table *tb, const char *foot, int pr) { return script_mode ? fk_table(t, tb, foot, pr) : wk_table(t, tb, foot, pr); }
static void tk_open(const char *p) { if (script_mode) fk_open(p); else wk_open(p); }
#else
static void tk_msg(const char *t, const char *x) { fk_msg(t, x); }
static int tk_confirm(const char *t, const char *x) { return fk_confirm(t, x); }
static int tk_form(Form *f) { return fk_form(f); }
static int tk_table(const char *t, Table *tb, const char *foot, int pr) { return fk_table(t, tb, foot, pr); }
static void tk_open(const char *p) { fk_open(p); }
#endif

static void rm_matching(const char *prefix) {
    DIR *d = opendir("."); struct dirent *e; char names[64][260]; int n = 0;
    while (d && (e = readdir(d)) && n < 64) if (!strncmp(e->d_name, prefix, strlen(prefix))) snprintf(names[n++], 260, "%s", e->d_name);
    if (d) closedir(d); for (int i = 0; i < n; i++) remove(names[i]);
}
static int selftest(void) {
    SB sc = {0}; for (int i = 0; SELFTEST_LINES[i]; i++) sba(&sc, SELFTEST_LINES[i]);
    int fail;
#ifdef _WIN32
    freopen("mixpack_selftest.txt", "w", stdout);
#endif
    puts("MixPack yerel arayüz çekirdeği kendi kendini sınama (gerçek verilere dokunmaz: mixpack_2099.db / mixpack_2100.db geçici)");
    gui = 1; remove("mixpack_2099.db"); remove("mixpack_2100.db"); open_year(2099); core_init();
#ifdef _WIN32
    script_mode = 1;
#endif
    fail = run_script(sc.p); free(sc.p);
#ifdef _WIN32
    script_mode = 0;
#endif
    sqlite3_close(db); db = NULL; remove("mixpack_2099.db"); remove("mixpack_2100.db"); rm_matching("mixpack_2099_"); remove("ekstre.html"); remove("fatura_99-11-01.html");
    printf("\n%s\n", fail ? "SONUÇ: BAŞARISIZ — yukarıdaki HATA / !! satırlarını geliştiriciye iletin." : "SONUÇ: Tüm denetimler başarılı."); fflush(stdout);
#ifdef _WIN32
    MessageBoxW(NULL, fail ? L"Kendi kendini sınama BAŞARISIZ. Ayrıntı: mixpack_selftest.txt" : L"Kendi kendini sınama başarılı. Ayrıntı: mixpack_selftest.txt", L"MixPack", MB_OK | (fail ? MB_ICONERROR : MB_ICONINFORMATION));
#endif
    return fail;
}
static void pick_year(int argc, char **argv, int pos) {
    int ys[64], n = scan_years(ys), y; time_t tt = time(0);
    if (argc > pos && atoi(argv[pos]) >= 2000) y = atoi(argv[pos]); else if (n) y = ys[n - 1]; else y = localtime(&tt)->tm_year + 1900;
    gui = 1; open_year(y); core_init();
}
#ifndef _WIN32
int main(int argc, char **argv) {
    chdir_exe();
    if (argc > 1 && !strcmp(argv[1], "--selftest")) return selftest();
    /* geliştirici modu: senaryoyu stdin'den oku (yanıt satırları + @komutlar) */
    pick_year(argc, argv, 1); SB in = {0}; char b[1000]; while (fgets(b, sizeof b, stdin)) sba(&in, b);
    int f = run_script(in.p ? in.p : ""); free(in.p); return f;
}
#endif
#ifdef _WIN32
int WINAPI WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR cmd, int show) {
    (void)hP; (void)show; chdir_exe(); gui = 1;
    if (cmd && strstr(cmd, "--selftest")) return selftest();
    int ut = cmd && strstr(cmd, "--uitest") != NULL;
    if (!ut) { HANDLE mx = CreateMutexW(NULL, TRUE, L"Local\\MixPackMuhasebe"); (void)mx; if (GetLastError() == ERROR_ALREADY_EXISTS) { MessageBoxW(NULL, L"MixPack zaten açık (görev çubuğuna bakın).", L"MixPack", MB_OK | MB_ICONINFORMATION); return 0; } }
    if (!tk_init(hI)) { MessageBoxW(NULL, L"Pencere sınıfları kaydedilemedi.", L"MixPack", MB_OK | MB_ICONERROR); return 1; }
    if (ut) { remove("mixpack_2099.db"); open_year(2099); }
    else {
        int ys[64], n = scan_years(ys), y = 0; time_t tt = time(0); for (const char *p = cmd ? cmd : ""; *p; p++) if (isdigit((unsigned char)*p)) { y = atoi(p); break; }
        if (y < 2000) y = n ? ys[n - 1] : localtime(&tt)->tm_year + 1900;
        open_year(y);
    }
    core_init();
    hMain = CreateWindowExW(0, L"MPMain", L"MixPack Muhasebe", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, S(1240), S(780), NULL, NULL, hI, NULL);
    if (!hMain) { MessageBoxW(NULL, L"Ana pencere oluşturulamadı.", L"MixPack", MB_OK | MB_ICONERROR); return 1; }
    render_view();
    if (ut) { ShowWindow(hMain, SW_SHOW); UpdateWindow(hMain); int f = run_uitest(); DestroyWindow(hMain); sqlite3_close(db); remove("mixpack_2099.db"); return f; }
    ShowWindow(hMain, SW_SHOWMAXIMIZED); UpdateWindow(hMain);
    MSG msg; while (GetMessageW(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    sqlite3_close(db); return (int)msg.wParam;
}
#endif
