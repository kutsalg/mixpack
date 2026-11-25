/* MixPack Muhasebe - stok, cari, muhasebe ve fatura (offline, tek dosya, MIT lisansı)
 * Kaynakça: SQLite (kamu malı) - https://sqlite.org
 * Derleme (Windows/MinGW): gcc -O2 mixpack.c sqlite3.c -o mixpack.exe -lm
 * Her yıl ayrı dosya: mixpack_YYYY.db (geçmiş yıllar arşiv olarak durur). */
#ifndef __USE_MINGW_ANSI_STDIO
#define __USE_MINGW_ANSI_STDIO 1 /* MinGW: C99 uyumlu printf/vsnprintf */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>
#include <dirent.h>
#include <sqlite3.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#define OPEN_CMD "start \"\" "
#define QUIET ""
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir(p,0755)
#define OPEN_CMD "xdg-open "
#define QUIET " >/dev/null 2>&1"
#endif

static sqlite3 *db; static int year; static char dbpath[300]; static FILE *csvf;
static int gui; static char lasterr[300], lastfile[120]; static int nyopt[4]; /* gui modu için */

#define DF(c) "substr(" c ",9,2)||'.'||substr(" c ",6,2)||'.'||substr(" c ",1,4)"
#define BAL "(p.opening+COALESCE((SELECT SUM(debit-credit) FROM led WHERE pid=p.id),0))"
#define STK "(p.opening_qty+COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id),0))"
#define RF "(SELECT f FROM rng)"
#define RT "(SELECT t FROM rng)"

static const char SCHEMA[] =
"CREATE TABLE IF NOT EXISTS settings(k TEXT PRIMARY KEY,v TEXT);"
"CREATE TABLE IF NOT EXISTS party(id INTEGER PRIMARY KEY,kind TEXT,code TEXT UNIQUE,name TEXT,company TEXT,address TEXT,phone TEXT,email TEXT,note TEXT,opening INTEGER DEFAULT 0,active INTEGER DEFAULT 1);"
"CREATE TABLE IF NOT EXISTS product(id INTEGER PRIMARY KEY,code TEXT UNIQUE,name TEXT,category TEXT,unit TEXT,buy_price INTEGER DEFAULT 0,sell_price INTEGER DEFAULT 0,min_stock REAL DEFAULT 0,vat REAL DEFAULT 7,opening_qty REAL DEFAULT 0,opening_cost INTEGER DEFAULT 0,active INTEGER DEFAULT 1);"
"CREATE TABLE IF NOT EXISTS invoice(id INTEGER PRIMARY KEY,type TEXT,no TEXT,date TEXT,party_id INTEGER,method TEXT,paid INTEGER,total INTEGER,note TEXT,status TEXT DEFAULT 'A',cancel_note TEXT);"
"CREATE UNIQUE INDEX IF NOT EXISTS ux_inv ON invoice(type,(CASE WHEN type='S' THEN 0 ELSE party_id END),no);"
"CREATE TABLE IF NOT EXISTS line(id INTEGER PRIMARY KEY,inv_id INTEGER,product_id INTEGER,qty REAL,price INTEGER,total INTEGER,vat REAL);"
"CREATE TABLE IF NOT EXISTS payment(id INTEGER PRIMARY KEY,date TEXT,party_id INTEGER,amount INTEGER,method TEXT,note TEXT,status TEXT DEFAULT 'A');"
"CREATE TABLE IF NOT EXISTS expense(id INTEGER PRIMARY KEY,date TEXT,category TEXT,descr TEXT,amount INTEGER,method TEXT,status TEXT DEFAULT 'A');"
"CREATE TABLE IF NOT EXISTS audit(id INTEGER PRIMARY KEY,ts TEXT DEFAULT CURRENT_TIMESTAMP,what TEXT);"
/* silme/değiştirme koruması: kayıtlar sessizce silinemez, sadece iptal edilir */
"CREATE TRIGGER IF NOT EXISTS t_inv_del BEFORE DELETE ON invoice BEGIN SELECT RAISE(ABORT,'Fatura silinemez, iptal edin'); END;"
"CREATE TRIGGER IF NOT EXISTS t_inv_upd BEFORE UPDATE OF type,no,date,party_id,method,paid,total ON invoice BEGIN SELECT RAISE(ABORT,'Kayitli fatura degistirilemez, iptal edip yenisini girin'); END;"
"CREATE TRIGGER IF NOT EXISTS t_line_del BEFORE DELETE ON line BEGIN SELECT RAISE(ABORT,'Satir silinemez'); END;"
"CREATE TRIGGER IF NOT EXISTS t_line_upd BEFORE UPDATE ON line BEGIN SELECT RAISE(ABORT,'Satir degistirilemez'); END;"
"CREATE TRIGGER IF NOT EXISTS t_pay_del BEFORE DELETE ON payment BEGIN SELECT RAISE(ABORT,'Odeme silinemez, iptal edin'); END;"
"CREATE TRIGGER IF NOT EXISTS t_pay_upd BEFORE UPDATE OF date,party_id,amount,method ON payment BEGIN SELECT RAISE(ABORT,'Odeme degistirilemez'); END;"
"CREATE TRIGGER IF NOT EXISTS t_exp_del BEFORE DELETE ON expense BEGIN SELECT RAISE(ABORT,'Gider silinemez, iptal edin'); END;"
"CREATE TRIGGER IF NOT EXISTS t_exp_upd BEFORE UPDATE OF date,category,amount,method ON expense BEGIN SELECT RAISE(ABORT,'Gider degistirilemez'); END;"
/* stok hareketi: yalnızca aktif (iptal edilmemiş) faturalardan türer */
"CREATE VIEW IF NOT EXISTS mv AS SELECT l.product_id pid,i.date d,i.id iid,i.type ty,CASE i.type WHEN 'P' THEN l.qty ELSE -l.qty END q,l.price pr,l.total tot,l.vat vat,i.no no,i.party_id party FROM line l JOIN invoice i ON i.id=l.inv_id WHERE i.status='A';"
/* cari hareket: ödenmemiş faturalar borç, tahsilat/ödeme alacak */
"CREATE VIEW IF NOT EXISTS led AS SELECT party_id pid,date d,id sid,'Fatura '||no t,total debit,0 credit FROM invoice WHERE paid=0 AND status='A' "
"UNION ALL SELECT x.party_id,x.date,x.id+1000000,(CASE r.kind WHEN 'C' THEN 'Tahsilat' ELSE 'Ödeme' END)||CASE WHEN x.note<>'' THEN ' ('||x.note||')' ELSE '' END,0,x.amount FROM payment x JOIN party r ON r.id=x.party_id WHERE x.status='A';"
/* kasa/banka hareketi: N=nakit, B=banka */
"CREATE VIEW IF NOT EXISTS fin AS SELECT i.date d,i.method m,CASE i.type WHEN 'S' THEN i.total ELSE -i.total END a,(CASE i.type WHEN 'S' THEN 'Satış faturası ' ELSE 'Alış faturası ' END)||i.no t FROM invoice i WHERE i.paid=1 AND i.status='A' "
"UNION ALL SELECT p.date,p.method,CASE r.kind WHEN 'C' THEN p.amount ELSE -p.amount END,(CASE r.kind WHEN 'C' THEN 'Tahsilat ' ELSE 'Ödeme ' END)||r.name FROM payment p JOIN party r ON r.id=p.party_id WHERE p.status='A' "
"UNION ALL SELECT date,method,-amount,'Gider '||category||' '||descr FROM expense WHERE status='A';";

/* ---------- yardımcılar ---------- */
static int run(const char *f, ...) {
    va_list a; va_start(a, f); char *s = sqlite3_vmprintf(f, a); va_end(a);
    char *e = 0; int r = sqlite3_exec(db, s, 0, 0, &e);
    if (r) { snprintf(lasterr, sizeof lasterr, "%s", e ? e : "?"); if (!gui) printf("HATA: %s\n", lasterr); sqlite3_free(e); }
    sqlite3_free(s); return r == 0;
}
static double val(const char *f, ...) {
    va_list a; va_start(a, f); char *s = sqlite3_vmprintf(f, a); va_end(a);
    sqlite3_stmt *st; double r = 0;
    if (sqlite3_prepare_v2(db, s, -1, &st, 0) == 0) { if (sqlite3_step(st) == SQLITE_ROW) r = sqlite3_column_double(st, 0); sqlite3_finalize(st); }
    else printf("SQL hata: %s\n", sqlite3_errmsg(db));
    sqlite3_free(s); return r;
}
static const char *txt(const char *f, ...) {
    static char buf[8][256]; static int k; char *o = buf[k = (k + 1) & 7]; o[0] = 0;
    va_list a; va_start(a, f); char *s = sqlite3_vmprintf(f, a); va_end(a);
    sqlite3_stmt *st;
    if (sqlite3_prepare_v2(db, s, -1, &st, 0) == 0) {
        if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_text(st, 0)) snprintf(o, 256, "%s", (const char *)sqlite3_column_text(st, 0));
        sqlite3_finalize(st);
    } else printf("SQL hata: %s\n", sqlite3_errmsg(db));
    sqlite3_free(s); return o;
}
static void aud(const char *f, ...) {
    char b[400]; va_list a; va_start(a, f); vsnprintf(b, sizeof b, f, a); va_end(a);
    run("INSERT INTO audit(what) VALUES(%Q)", b);
}
static void bye(int c) { if (db) sqlite3_close(db); exit(c); }
static void ask(const char *p, char *b, int n) { printf("%s", p); fflush(stdout); if (!fgets(b, n, stdin)) bye(0); b[strcspn(b, "\r\n")] = 0; }
static int yes(const char *p) { char b[16]; if (gui) return 0; ask(p, b, 16); return b[0] == 'e' || b[0] == 'E' || b[0] == 'y' || b[0] == 'Y'; }
static int num(const char *p) { char b[32]; ask(p, b, 32); return atoi(b); }
static double pnum(const char *s) { char t[64]; snprintf(t, 64, "%s", s); for (char *c = t; *c; c++) if (*c == ',') *c = '.'; return atof(t); }
static long long cents(const char *s) { return llround(pnum(s) * 100); }
static void fmtm(long long c, char *o) {
    char t[32]; long long w = llabs(c) / 100; snprintf(t, 32, "%lld", w); int l = (int)strlen(t), p = 0;
    if (c < 0) o[p++] = '-';
    for (int i = 0; i < l; i++) { if (i && (l - i) % 3 == 0) o[p++] = '.'; o[p++] = t[i]; }
    sprintf(o + p, ",%02lld", llabs(c) % 100);
}
static const char *M(long long c) { static char b[8][48]; static int k; char *o = b[k = (k + 1) & 7]; fmtm(c, o); strcat(o, " €"); return o; }
static const char *Q(double q) {
    static char b[8][32]; static int k; char *o = b[k = (k + 1) & 7]; snprintf(o, 32, "%.3f", q);
    char *e = o + strlen(o) - 1; while (*e == '0') *e-- = 0; if (*e == '.') *e = 0; return o;
}
static void chdir_exe(void) {
#ifdef _WIN32
    char p[MAX_PATH]; DWORD n = GetModuleFileNameA(NULL, p, MAX_PATH);
    if (n > 0 && n < MAX_PATH) { char *s = strrchr(p, '\\'); if (s) { *s = 0; SetCurrentDirectoryA(p); } }
#endif
}
static int menu(const char *title, const char *items) { printf("\n== %s ==\n%s0) Geri/Çıkış\n", title, items); return num("Seçim: "); }

/* tarih: gg.aa.yyyy veya yyyy-aa-gg. def!=NULL ise boş girişte def döner; NULL ise zorunlu. strict: dönem yılı içinde olmalı */
static void askdate(const char *p, char *o, const char *def, int strict) {
    for (;;) {
        char b[40]; int d, m, y; ask(p, b, 40);
        if (!*b && def) { snprintf(o, 11, "%s", def); return; }
        if (sscanf(b, "%d.%d.%d", &d, &m, &y) == 3) { if (y < 100) y += 2000; }
        else if (sscanf(b, "%d-%d-%d", &y, &m, &d) != 3) { puts("Geçersiz tarih (gg.aa.yyyy)."); continue; }
        char iso[16]; snprintf(iso, 16, "%04d-%02d-%02d", y, m, d);
        if (strcmp(txt("SELECT date(%Q)", iso), iso)) { puts("Geçersiz tarih."); continue; }
        if (strict && y != year) { printf("Bu dönem %d yılıdır; tarih bu yıl içinde olmalı.\n", year); continue; }
        snprintf(o, 11, "%s", iso); return;
    }
}
static const char *today(void) { return txt("SELECT date('now','localtime')"); }
static const char *defdate(void) { const char *t = today(); return atoi(t) == year ? t : NULL; }

/* tablo yazdırıcı: "_c" ile biten kolon = kuruş (para) olarak biçimlenir; csvf açıksa CSV'ye de yazar */
static int ulen(const char *s) { int n = 0; for (; *s; s++) if ((*s & 0xC0) != 0x80) n++; return n; }
static void show(const char *sql) {
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(db, sql, -1, &s, 0)) { printf("SQL hata: %s\n", sqlite3_errmsg(db)); return; }
    int n = sqlite3_column_count(s), rows = 0, cap = 32; int mon[32] = {0}, isn[32] = {0}, w[32] = {0};
    char **c = malloc(sizeof(char *) * n * cap);
    for (int i = 0; i < n; i++) {
        const char *nm = sqlite3_column_name(s, i); size_t l = strlen(nm);
        mon[i] = l > 2 && !strcmp(nm + l - 2, "_c"); c[i] = strdup(nm); if (mon[i]) c[i][l - 2] = 0;
    }
    while (sqlite3_step(s) == SQLITE_ROW) {
        if ((rows + 2) * n > cap * n) { cap *= 2; c = realloc(c, sizeof(char *) * n * cap); }
        for (int i = 0; i < n; i++) {
            char b[512]; int t = sqlite3_column_type(s, i);
            if (t == SQLITE_NULL) b[0] = 0;
            else if (mon[i]) { isn[i] = 1; snprintf(b, 512, "%s", M(llround(sqlite3_column_double(s, i)))); }
            else if (t == SQLITE_INTEGER) { isn[i] = 1; snprintf(b, 512, "%lld", sqlite3_column_int64(s, i)); }
            else if (t == SQLITE_FLOAT) { isn[i] = 1; snprintf(b, 512, "%s", Q(sqlite3_column_double(s, i))); }
            else snprintf(b, 512, "%s", (const char *)sqlite3_column_text(s, i));
            c[(rows + 1) * n + i] = strdup(b);
        }
        rows++;
    }
    sqlite3_finalize(s);
    for (int r = 0; r <= rows; r++) for (int i = 0; i < n; i++) { int l = ulen(c[r * n + i]); if (l > w[i]) w[i] = l; }
    for (int r = 0; r <= rows; r++) {
        for (int i = 0; i < n; i++) {
            const char *x = c[r * n + i]; int pad = w[i] - ulen(x);
            if (isn[i] && r) printf("%*s%s  ", pad, "", x); else printf("%s%*s  ", x, pad, "");
        }
        putchar('\n');
        if (r == 0) { for (int i = 0; i < n; i++) { for (int k = 0; k < w[i]; k++) putchar('-'); printf("  "); } putchar('\n'); }
        if (csvf) {
            for (int i = 0; i < n; i++) {
                char t[512]; snprintf(t, 512, "%s", c[r * n + i]);
                if (r && mon[i]) { char *e = strstr(t, " €"); if (e) *e = 0; }
                if (strpbrk(t, ";\"\n")) fprintf(csvf, "\"%s\"", t); else fputs(t, csvf);
                fputc(i == n - 1 ? '\n' : ';', csvf);
            }
        }
    }
    if (!rows) puts("(kayıt yok)"); else printf("(%d satır)\n", rows);
    for (int r = 0; r <= rows; r++) for (int i = 0; i < n; i++) free(c[r * n + i]);
    free(c);
}
#define SHOW(...) do { char *_s = sqlite3_vmprintf_wrap(__VA_ARGS__); show(_s); sqlite3_free(_s); } while (0)
static char *sqlite3_vmprintf_wrap(const char *f, ...) { va_list a; va_start(a, f); char *s = sqlite3_vmprintf(f, a); va_end(a); return s; }

static long long setn(const char *k) { return (long long)val("SELECT COALESCE((SELECT CAST(v AS INTEGER) FROM settings WHERE k=%Q),0)", k); }
static void sets(const char *k, const char *v) { run("INSERT OR REPLACE INTO settings(k,v) VALUES(%Q,%Q)", k, v); }
static long long totbal(char kind) {
    char k[2] = {kind, 0};
    return llround(val("SELECT COALESCE(SUM(p.opening),0) FROM party p WHERE kind=%Q", k) +
                   val("SELECT COALESCE(SUM(debit-credit),0) FROM led JOIN party p ON p.id=pid WHERE p.kind=%Q", k));
}
static long long cashbal(char m) {
    char k[2] = {m, 0};
    return llround(val("SELECT COALESCE(SUM(a),0) FROM fin WHERE m=%Q", k)) + setn(m == 'N' ? "cash_open" : "bank_open");
}
static void setrange(const char *f, const char *t) { run("DELETE FROM rng"); run("INSERT INTO rng VALUES(%Q,%Q)", f, t); }

/* ---------- ortalama maliyet yöntemi (kâr + yıl devri) ---------- */
typedef struct { int pid; double q, avg, vat; } Cy;
static int replay(const char *from, const char *to, double *rev, double *cogs, double *sval, Cy **out) {
    sqlite3_stmt *ps, *ms; int n = 0; Cy *arr = NULL; *rev = *cogs = *sval = 0;
    sqlite3_prepare_v2(db, "SELECT id,opening_qty,opening_cost,vat FROM product ORDER BY id", -1, &ps, 0);
    sqlite3_prepare_v2(db, "SELECT d,ty,q,tot,vat FROM mv WHERE pid=? ORDER BY d,iid", -1, &ms, 0);
    while (sqlite3_step(ps) == SQLITE_ROW) {
        int id = sqlite3_column_int(ps, 0); double q = sqlite3_column_double(ps, 1), v = sqlite3_column_double(ps, 3);
        double avg = sqlite3_column_double(ps, 2) / 100.0 / (1 + v / 100);
        sqlite3_reset(ms); sqlite3_bind_int(ms, 1, id);
        while (sqlite3_step(ms) == SQLITE_ROW) {
            const char *d = (const char *)sqlite3_column_text(ms, 0); char ty = *sqlite3_column_text(ms, 1);
            double mq = sqlite3_column_double(ms, 2), tot = sqlite3_column_double(ms, 3), mvat = sqlite3_column_double(ms, 4);
            double net = tot / (1 + mvat / 100);
            if (ty == 'P') { double base = q > 0 ? q : 0; if (mq > 0) avg = (base * avg + net / 100.0) / (base + mq); q += mq; }
            else { double o = -mq; if (strcmp(d, from) >= 0 && strcmp(d, to) <= 0) { *rev += net; *cogs += o * avg * 100.0; } q -= o; }
        }
        if (q > 0) *sval += q * avg * 100.0;
        if (out) { arr = realloc(arr, sizeof(Cy) * (n + 1)); arr[n].pid = id; arr[n].q = q; arr[n].avg = avg; arr[n].vat = v; }
        n++;
    }
    sqlite3_finalize(ps); sqlite3_finalize(ms);
    if (out) *out = arr;
    return n;
}

/* ---------- cari (müşteri C / tedarikçi S) ---------- */
static int new_party(char kind) {
    char n[100], c[100], a[200], ph[40], em[100], no[200], ob[40], k[2] = {kind, 0};
    ask("Ad: ", n, 100); if (!*n) { puts("Ad boş olamaz."); return 0; }
    ask("Firma adı: ", c, 100); ask("Adres: ", a, 200); ask("Telefon: ", ph, 40);
    ask("E-posta (isteğe bağlı): ", em, 100); ask("Açıklama: ", no, 200);
    ask("Açılış bakiyesi € (borç +, boş=0): ", ob, 40);
    char code[16]; snprintf(code, 16, "%c%04d", kind == 'C' ? 'M' : 'T', (int)val("SELECT COUNT(*) FROM party WHERE kind=%Q", k) + 1);
    if (!run("INSERT INTO party(kind,code,name,company,address,phone,email,note,opening) VALUES(%Q,%Q,%Q,%Q,%Q,%Q,%Q,%Q,%lld)", k, code, n, c, a, ph, em, no, cents(ob))) return 0;
    int nid = (int)sqlite3_last_insert_rowid(db); /* aud()'dan ÖNCE oku */
    aud("CARI olusturuldu %s %s", code, n); printf("Cari kodu: %s\n", code);
    return nid;
}
static void list_parties(char kind, int onlydebt) {
    char k[2] = {kind, 0};
    SHOW("SELECT p.id ID,p.code Kod,p.name Ad,p.company Firma,p.phone Tel," BAL " Bakiye_c,CASE p.active WHEN 1 THEN '' ELSE 'PASİF' END Durum FROM party p WHERE p.kind=%Q AND (%d=0 OR " BAL ">0) ORDER BY %s", k, onlydebt, onlydebt ? BAL " DESC" : "p.id");
}
static int pick_party(char kind) {
    char k[2] = {kind, 0};
    SHOW("SELECT p.id ID,p.code Kod,p.name Ad,p.company Firma," BAL " Bakiye_c FROM party p WHERE p.kind=%Q AND p.active=1 ORDER BY p.name", k);
    int id = num("Cari ID (0=yeni cari aç, boşta iptal için -1): ");
    if (id == 0) return new_party(kind);
    if (id < 0 || !val("SELECT COUNT(*) FROM party WHERE id=%d AND kind=%Q AND active=1", id, k)) { puts("Geçersiz cari."); return 0; }
    return id;
}
static void statement(int id) {
    char f[11], t[11]; askdate("Başlangıç (boş=en baştan): ", f, "0000-01-01", 0); askdate("Bitiş (boş=hepsi): ", t, "9999-12-31", 0);
    printf("\nCari: %s  (%s)\n", txt("SELECT name FROM party WHERE id=%d", id), txt("SELECT code FROM party WHERE id=%d", id));
    SHOW("SELECT CASE WHEN d='0000-01-01' THEN '' ELSE " DF("d") " END Tarih,t İşlem,debit Borç_c,credit Alacak_c,run Bakiye_c FROM ("
         "SELECT d,sid,t,debit,credit,SUM(debit-credit) OVER (ORDER BY d,sid) run FROM ("
         "SELECT '0000-01-01' d,0 sid,'Açılış bakiyesi' t,MAX(opening,0) debit,MAX(-opening,0) credit FROM party WHERE id=%d "
         "UNION ALL SELECT d,sid,t,debit,credit FROM led WHERE pid=%d)) WHERE d>=%Q AND d<=%Q ORDER BY d,sid", id, id, f, t);
    SHOW("SELECT p.opening Açılış_c,COALESCE((SELECT SUM(total) FROM invoice WHERE party_id=p.id AND status='A'),0) Toplam_fatura_c,"
         "COALESCE((SELECT SUM(amount) FROM payment WHERE party_id=p.id AND status='A'),0) Toplam_tahsilat_c," BAL " GÜNCEL_BAKİYE_c FROM party p WHERE p.id=%d", id);
}
static void take_payment(int id, char kind) {
    long long bal = llround(val("SELECT " BAL " FROM party p WHERE p.id=%d", id)); char b[64], m[8], d[11], nt[100];
    printf("Güncel bakiye: %s\n", M(bal));
    ask(kind == 'C' ? "Tahsilat tutarı € (boş=tamamı): " : "Ödeme tutarı € (boş=tamamı): ", b, 64);
    long long amt = *b ? cents(b) : bal; if (amt <= 0) { puts("Tutar geçersiz."); return; }
    if (amt > bal && !yes("Tutar bakiyeden büyük (avans olur). Devam? (e/h): ")) return;
    ask("Ödeme şekli (N=Nakit, B=Banka): ", m, 8); m[0] = (m[0] == 'b' || m[0] == 'B') ? 'B' : 'N'; m[1] = 0;
    askdate("Tarih (boş=bugün): ", d, defdate(), 1); ask("Not: ", nt, 100);
    if (val("SELECT COUNT(*) FROM payment WHERE party_id=%d AND date=%Q AND amount=%lld AND method=%Q AND status='A'", id, d, amt, m) &&
        !yes("UYARI: Aynı gün/tutar/yöntemle kayıt zaten var (çift kayıt olabilir). Yine de kaydet? (e/h): ")) return;
    if (run("INSERT INTO payment(date,party_id,amount,method,note) VALUES(%Q,%d,%lld,%Q,%Q)", d, id, amt, m, nt)) {
        aud("ODEME kaydedildi cari=%d %lld kurus %s", id, amt, m);
        printf("Kaydedildi. Kalan bakiye: %s\n", M(llround(val("SELECT " BAL " FROM party p WHERE p.id=%d", id))));
    }
}
static void cancel_payment(int id) {
    SHOW("SELECT id ID," DF("date") " Tarih,amount Tutar_c,CASE method WHEN 'N' THEN 'Nakit' ELSE 'Banka' END Yöntem,note Not_,CASE status WHEN 'X' THEN 'İPTAL' ELSE '' END Durum FROM payment WHERE party_id=%d ORDER BY id DESC LIMIT 20", id);
    int pid = num("İptal edilecek kayıt ID (0=vazgeç): "); if (!pid) return;
    if (!val("SELECT COUNT(*) FROM payment WHERE id=%d AND party_id=%d AND status='A'", pid, id)) { puts("Bulunamadı / zaten iptal."); return; }
    char r[100]; ask("İptal nedeni: ", r, 100);
    if (yes("Emin misiniz? (e/h): ") && run("UPDATE payment SET status='X' WHERE id=%d", pid)) { aud("ODEME iptal id=%d neden=%s", pid, r); puts("İptal edildi (bakiye geri yüklendi)."); }
}
static void party_menu(char kind) {
    const char *T = kind == 'C' ? "Müşteri cari hesapları" : "Tedarikçi cari hesapları";
    for (;;) {
        int c = menu(T, "1) Liste\n2) Yeni cari\n3) Düzenle\n4) Aktif/Pasif\n5) Hesap ekstresi\n6) Tahsilat/Ödeme kaydet\n7) Borçlular (borca göre sıralı)\n8) Tahsilat/Ödeme iptali\n9) Cari faturaları\n");
        if (!c) return; char k[2] = {kind, 0};
        if (c == 1) list_parties(kind, 0);
        else if (c == 2) new_party(kind);
        else if (c == 7) list_parties(kind, 1);
        else {
            list_parties(kind, 0); int id = num("Cari ID: ");
            if (!val("SELECT COUNT(*) FROM party WHERE id=%d AND kind=%Q", id, k)) { puts("Cari bulunamadı."); continue; }
            if (c == 3) {
                static const char *f[][2] = {{"name", "Ad"}, {"company", "Firma"}, {"address", "Adres"}, {"phone", "Telefon"}, {"email", "E-posta"}, {"note", "Açıklama"}};
                for (int i = 0; i < 6; i++) { char b[200], p[80]; snprintf(p, 80, "%s (boş=aynı): ", f[i][1]); ask(p, b, 200); if (*b) run("UPDATE party SET %s=%Q WHERE id=%d", f[i][0], b, id); }
                aud("CARI duzenlendi id=%d", id);
            } else if (c == 4) { run("UPDATE party SET active=1-active WHERE id=%d", id); aud("CARI aktif/pasif id=%d", id); puts("Durum değiştirildi."); }
            else if (c == 5) statement(id);
            else if (c == 6) take_payment(id, kind);
            else if (c == 8) cancel_payment(id);
            else if (c == 9) SHOW("SELECT id ID,no No," DF("date") " Tarih,total Tutar_c,CASE paid WHEN 1 THEN 'ÖDENDİ' ELSE 'ÖDENMEDİ' END Durum,CASE status WHEN 'X' THEN 'İPTAL' ELSE '' END İptal FROM invoice WHERE party_id=%d ORDER BY date,id", id);
        }
    }
}

/* ---------- ürün / stok ---------- */
static void new_product(void) {
    char code[32], n[100], cat[60], u[20], bp[32], sp[32], ms[32], vt[32], oq[32], oc[32];
    ask("Ürün adı: ", n, 100); if (!*n) return;
    ask("Ürün kodu (boş=otomatik): ", code, 32);
    if (!*code) snprintf(code, 32, "U%04d", (int)val("SELECT COUNT(*) FROM product") + 1);
    ask("Kategori: ", cat, 60); ask("Birim (kg/adet/kasa/paket) [adet]: ", u, 20); if (!*u) strcpy(u, "adet");
    ask("Alış fiyatı €: ", bp, 32); ask("Satış fiyatı € (isteğe bağlı): ", sp, 32);
    ask("Minimum stok uyarısı: ", ms, 32); ask("KDV % (gıda 7, diğer 19) [7]: ", vt, 32);
    ask("Açılış stoku (boş=0): ", oq, 32); ask("Açılış stok birim maliyeti € (boş=alış fiyatı): ", oc, 32);
    long long ocost = *oc ? cents(oc) : cents(bp);
    if (run("INSERT INTO product(code,name,category,unit,buy_price,sell_price,min_stock,vat,opening_qty,opening_cost) VALUES(%Q,%Q,%Q,%Q,%lld,%lld,%f,%f,%f,%lld)",
            code, n, cat, u, cents(bp), cents(sp), pnum(ms), *vt ? pnum(vt) : 7.0, pnum(oq), ocost)) { aud("URUN olusturuldu %s %s", code, n); puts("Ürün eklendi."); }
}
static int pick_product(void) {
    SHOW("SELECT p.id ID,p.code Kod,p.name Ürün,p.unit Birim," STK " Stok,p.sell_price Satış_c FROM product p WHERE p.active=1 ORDER BY p.name");
    int id = num("Ürün ID (0=bitir): ");
    if (id > 0 && !val("SELECT COUNT(*) FROM product WHERE id=%d AND active=1", id)) { puts("Geçersiz ürün."); return -1; }
    return id;
}
static void product_history(int id) {
    printf("\nÜrün: %s  (Devir/açılış stoku: %s %s)\n", txt("SELECT name FROM product WHERE id=%d", id), Q(val("SELECT opening_qty FROM product WHERE id=%d", id)), txt("SELECT unit FROM product WHERE id=%d", id));
    SHOW("SELECT " DF("h.d") " Tarih,CASE h.ty WHEN 'P' THEN 'Alış' ELSE 'Satış' END İşlem,h.no Fatura,COALESCE(pa.name,'') Cari,CASE WHEN h.q>0 THEN h.q ELSE 0 END Giren,CASE WHEN h.q<0 THEN -h.q ELSE 0 END Çıkan,h.pr Birim_fiyat_c,h.run Kalan_stok "
         "FROM (SELECT d,iid,ty,no,party,q,pr,(SELECT opening_qty FROM product WHERE id=%d)+SUM(q) OVER (ORDER BY d,iid) run FROM mv WHERE pid=%d) h LEFT JOIN party pa ON pa.id=h.party ORDER BY h.d,h.iid", id, id);
}
static void pick_period(char *f, char *t);
static void prodsales(const char *order, int limit) {
    SHOW("SELECT p.name Ürün,p.unit Birim,COALESCE(SUM(CASE WHEN m.ty='S' THEN -m.q END),0) Satılan,COALESCE(SUM(CASE WHEN m.ty='S' THEN m.tot END),0) Satış_c,"
         "COALESCE(SUM(CASE WHEN m.ty='P' THEN m.q END),0) Alınan,COALESCE(SUM(CASE WHEN m.ty='P' THEN m.tot END),0) Alış_c FROM product p LEFT JOIN mv m ON m.pid=p.id AND m.d BETWEEN " RF " AND " RT
         " GROUP BY p.id %s LIMIT %d", order, limit);
}
static void stock_menu(void) {
    for (;;) {
        int c = menu("Stoklar", "1) Güncel stok listesi\n2) Yeni ürün\n3) Ürün düzenle (fiyat/min stok)\n4) Aktif/Pasif\n5) Ürün hareket dökümü\n6) Tarih itibarıyla stok\n7) Dönem: başlangıç stok / alınan / satılan / bitiş\n8) Ürün bazında alış-satış miktar ve tutar\n9) En çok / en az satılanlar\n10) Stokta olmayanlar ve minimum altı\n");
        if (!c) return; char f[11], t[11];
        if (c == 1) SHOW("SELECT p.id ID,p.code Kod,p.name Ürün,p.category Kategori,p.unit Birim," STK " Stok,p.min_stock Min,p.buy_price Alış_c,p.sell_price Satış_c,CASE WHEN p.active=0 THEN 'PASİF' WHEN " STK "<=0 THEN 'STOK YOK' WHEN " STK "<=p.min_stock THEN 'KRİTİK' ELSE '' END Durum FROM product p ORDER BY p.name");
        else if (c == 2) new_product();
        else if (c == 3) {
            int id = pick_product(); if (id <= 0) continue; char b[64];
            ask("Yeni alış fiyatı € (boş=aynı): ", b, 64); if (*b) run("UPDATE product SET buy_price=%lld WHERE id=%d", cents(b), id);
            ask("Yeni satış fiyatı € (boş=aynı): ", b, 64); if (*b) run("UPDATE product SET sell_price=%lld WHERE id=%d", cents(b), id);
            ask("Yeni minimum stok (boş=aynı): ", b, 64); if (*b) run("UPDATE product SET min_stock=%f WHERE id=%d", pnum(b), id);
            aud("URUN duzenlendi id=%d", id);
        } else if (c == 4) { int id = num("Ürün ID: "); run("UPDATE product SET active=1-active WHERE id=%d", id); aud("URUN aktif/pasif id=%d", id); }
        else if (c == 5) { int id = pick_product(); if (id > 0) product_history(id); }
        else if (c == 6) {
            askdate("Tarih: ", t, NULL, 0);
            SHOW("SELECT p.name Ürün,p.unit Birim,p.opening_qty+COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id AND d<=%Q),0) Stok FROM product p ORDER BY p.name", t);
        } else if (c >= 7 && c <= 9) {
            pick_period(f, t); setrange(f, t);
            if (c == 7) SHOW("SELECT p.name Ürün,p.unit Birim,p.opening_qty+COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id AND d<" RF "),0) Başlangıç,"
                             "COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id AND ty='P' AND d BETWEEN " RF " AND " RT "),0) Alınan,"
                             "-COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id AND ty='S' AND d BETWEEN " RF " AND " RT "),0) Satılan,"
                             "p.opening_qty+COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id AND d<=" RT "),0) Bitiş FROM product p ORDER BY p.name");
            else if (c == 8) prodsales("HAVING Satılan>0 OR Alınan>0 ORDER BY Satış_c DESC", 1000);
            else { puts("\nEn çok satılan 10:"); prodsales("HAVING Satılan>0 ORDER BY Satılan DESC", 10); puts("\nEn az satılan 10 (satışı olanlar):"); prodsales("HAVING Satılan>0 ORDER BY Satılan ASC", 10); }
        } else if (c == 10) SHOW("SELECT p.code Kod,p.name Ürün,p.unit Birim," STK " Stok,p.min_stock Min FROM product p WHERE p.active=1 AND (" STK "<=0 OR " STK "<=p.min_stock AND p.min_stock>0) ORDER BY " STK);
    }
}

/* ---------- fatura ---------- */
typedef struct { int pid; double qty; long long price, total; double vat; char name[100]; } L;
static void print_invoice(int id);
static void new_invoice(char type) {
    char d[11], no[40], b[128], k[2] = {type, 0}, note[200], m[8] = "N"; L ln[200]; int nl = 0, paid = 0;
    askdate("Fatura tarihi (gg.aa.yyyy, boş=bugün): ", d, defdate(), 1);
    int pid = pick_party(type == 'S' ? 'C' : 'S'); if (pid <= 0) return;
    for (;;) {
        if (type == 'S') {
            char pre[16], pat[20]; snprintf(pre, 16, "%.2s-%.2s-", d + 2, d + 5); snprintf(pat, 20, "%s%%", pre);
            int next = (int)val("SELECT COALESCE(MAX(CAST(substr(no,7) AS INTEGER)),0)+1 FROM invoice WHERE type='S' AND no LIKE %Q", pat);
            char def[40]; snprintf(def, 40, "%s%02d", pre, next); snprintf(b, 128, "Fatura no [%s] (Enter=otomatik): ", def);
            ask(b, no, 40); if (!*no) strcpy(no, def);
        } else { ask("Tedarikçi fatura numarası: ", no, 40); if (!*no) { puts("Alış fatura no zorunlu."); continue; } }
        if (val("SELECT COUNT(*) FROM invoice WHERE type=%Q AND no=%Q AND (type='S' OR party_id=%d)", k, no, pid)) { puts("Bu fatura numarası zaten kullanılmış (iptal edilenler dahil)!"); continue; }
        break;
    }
    for (;;) {
        int id = pick_product(); if (id == 0) break; if (id < 0) continue; if (nl >= 200) { puts("Satır sınırı."); break; }
        L *l = &ln[nl]; l->pid = id; snprintf(l->name, 100, "%s", txt("SELECT name FROM product WHERE id=%d", id));
        ask("Miktar: ", b, 128); l->qty = pnum(b); if (l->qty <= 0) { puts("Miktar geçersiz."); continue; }
        long long def = (long long)val("SELECT %s FROM product WHERE id=%d", type == 'S' ? "sell_price" : "buy_price", id);
        snprintf(b, 128, "Birim fiyat € [%s]: ", M(def)); ask(b, b, 128);
        l->price = *b ? cents(b) : def; if (l->price <= 0) { puts("Fiyat gerekli."); continue; }
        l->total = llround(l->qty * l->price); l->vat = val("SELECT vat FROM product WHERE id=%d", id);
        if (type == 'S') {
            double cur = val("SELECT " STK " FROM product p WHERE p.id=%d", id); for (int i = 0; i < nl; i++) if (ln[i].pid == id) cur -= ln[i].qty;
            if (l->qty > cur) {
                printf("UYARI: Stok yetersiz! Mevcut: %s, satış: %s\n", Q(cur), Q(l->qty));
                if (!setn("allow_neg")) { puts("Negatif stoğa satış ayarlardan kapalı."); continue; }
                if (!yes("Yine de ekle (stok negatife düşer)? (e/h): ")) continue;
            }
        }
        nl++; printf("  + %s  %s x %s = %s\n", l->name, Q(l->qty), M(l->price), M(l->total));
    }
    if (!nl) { puts("Satır yok, iptal."); return; }
    long long tot = 0; for (int i = 0; i < nl; i++) tot += ln[i].total;
    paid = yes("Ödendi mi? (e=ödendi/kapalı, h=ödenmedi/açık): ");
    if (paid) { ask("Ödeme şekli (N=Nakit, B=Banka): ", b, 128); m[0] = (b[0] == 'b' || b[0] == 'B') ? 'B' : 'N'; m[1] = 0; } else m[0] = 0;
    ask("Açıklama: ", note, 200);
    printf("\nFatura %s | %s | %s\nToplam: %s | %s\n", no, d, txt("SELECT name FROM party WHERE id=%d", pid), M(tot), paid ? (m[0] == 'B' ? "ÖDENDİ - Banka" : "ÖDENDİ - Nakit") : "ÖDENMEDİ (cariye borç)");
    if (!yes("Kaydedilsin mi? (e/h): ")) { puts("Vazgeçildi."); return; }
    if (!run("BEGIN")) return;
    int ok = run("INSERT INTO invoice(type,no,date,party_id,method,paid,total,note) VALUES(%Q,%Q,%Q,%d,%Q,%d,%lld,%Q)", k, no, d, pid, m, paid, tot, note);
    int iid = (int)sqlite3_last_insert_rowid(db);
    for (int i = 0; ok && i < nl; i++) {
        ok = run("INSERT INTO line(inv_id,product_id,qty,price,total,vat) VALUES(%d,%d,%f,%lld,%lld,%f)", iid, ln[i].pid, ln[i].qty, ln[i].price, ln[i].total, ln[i].vat);
        if (ok && type == 'P') run("UPDATE product SET buy_price=%lld WHERE id=%d", ln[i].price, ln[i].pid);
    }
    if (ok) { run("COMMIT"); aud("FATURA kaydedildi %s %s %lld kurus", k, no, tot); printf("Kaydedildi: %s (stok/cari/kasa otomatik işlendi)\n", no); if (type == 'S' && yes("Fatura HTML/PDF olarak hazırlansın mı? (e/h): ")) print_invoice(iid); }
    else { run("ROLLBACK"); puts("Kayıt yapılamadı, hiçbir değişiklik yazılmadı."); }
}
static void list_invoices(char type) {
    char ty[2] = {type, 0}, f[11], t[11], q[100];
    int st = num("Durum: 1 Hepsi  2 Ödenen  3 Ödenmeyen  4 İptal edilen [1]: "); if (st < 1 || st > 4) st = 1;
    askdate("Başlangıç tarihi (boş=hepsi): ", f, "0000-01-01", 0); askdate("Bitiş tarihi (boş=hepsi): ", t, "9999-12-31", 0);
    ask("Ara (no / cari / ürün / açıklama, boş=hepsi): ", q, 100);
    SHOW("SELECT i.id ID,CASE i.type WHEN 'S' THEN 'Satış' ELSE 'Alış' END Tür,i.no No," DF("i.date") " Tarih,p.name Cari,i.total Tutar_c,CASE i.paid WHEN 1 THEN 'ÖDENDİ '||CASE i.method WHEN 'B' THEN '(Banka)' ELSE '(Nakit)' END ELSE 'ÖDENMEDİ' END Durum,CASE i.status WHEN 'X' THEN 'İPTAL' ELSE '' END İptal "
         "FROM invoice i JOIN party p ON p.id=i.party_id WHERE (i.type=%Q OR %Q='*') AND i.date BETWEEN %Q AND %Q AND "
         "(%d=1 OR (%d=2 AND i.paid=1 AND i.status='A') OR (%d=3 AND i.paid=0 AND i.status='A') OR (%d=4 AND i.status='X')) AND "
         "(%Q='' OR i.no LIKE '%%'||%Q||'%%' OR p.name LIKE '%%'||%Q||'%%' OR i.note LIKE '%%'||%Q||'%%' OR EXISTS(SELECT 1 FROM line l JOIN product r ON r.id=l.product_id WHERE l.inv_id=i.id AND r.name LIKE '%%'||%Q||'%%')) ORDER BY i.date,i.id",
         ty, ty, f, t, st, st, st, st, q, q, q, q, q);
}
static void invoice_detail(int id) {
    if (!val("SELECT COUNT(*) FROM invoice WHERE id=%d", id)) { puts("Fatura bulunamadı."); return; }
    SHOW("SELECT i.no No," DF("i.date") " Tarih,p.name Cari,i.total Tutar_c,CASE i.paid WHEN 1 THEN 'ÖDENDİ' ELSE 'ÖDENMEDİ' END Durum,i.note Açıklama,CASE i.status WHEN 'X' THEN 'İPTAL: '||COALESCE(i.cancel_note,'') ELSE '' END İptal FROM invoice i JOIN party p ON p.id=i.party_id WHERE i.id=%d", id);
    SHOW("SELECT r.name Ürün,r.unit Birim,l.qty Miktar,l.price Fiyat_c,l.total Toplam_c,l.vat KDV FROM line l JOIN product r ON r.id=l.product_id WHERE l.inv_id=%d", id);
}
static void cancel_invoice(int id) {
    if (!val("SELECT COUNT(*) FROM invoice WHERE id=%d AND status='A'", id)) { puts("Aktif fatura bulunamadı."); return; }
    invoice_detail(id); char r[100]; ask("İptal nedeni (zorunlu): ", r, 100); if (!*r) { puts("Neden gerekli."); return; }
    if (!yes("Fatura iptal edilsin mi? Stok, cari ve kasa/banka etkisi geri alınır, kayıt arşivde kalır (e/h): ")) return;
    if (run("UPDATE invoice SET status='X',cancel_note=%Q WHERE id=%d", r, id)) { aud("FATURA iptal id=%d no=%s neden=%s", id, txt("SELECT no FROM invoice WHERE id=%d", id), r); puts("İptal edildi. Düzeltme için yeni fatura girin (numara yeniden kullanılmaz)."); }
}
static void inv_menu(char type) {
    for (;;) {
        int c = menu(type == 'S' ? "Satış faturaları" : "Alış faturaları", type == 'S' ? "1) Yeni fatura\n2) Liste / arama\n3) Fatura detayı\n4) Yazdır / PDF (A4 HTML)\n5) İptal et\n" : "1) Yeni fatura\n2) Liste / arama\n3) Fatura detayı\n5) İptal et\n");
        if (!c) return;
        if (c == 1) new_invoice(type);
        else if (c == 2) list_invoices(type);
        else if (c == 3 || c == 5 || (c == 4 && type == 'S')) {
            int id = num("Fatura ID: ");
            if (!val("SELECT COUNT(*) FROM invoice WHERE id=%d AND type=%Q", id, type == 'S' ? "S" : "P")) { puts("Bu türde fatura yok."); continue; }
            if (c == 3) invoice_detail(id); else if (c == 4) print_invoice(id); else cancel_invoice(id);
        }
    }
}
static void hesc(FILE *f, const char *s) {
    for (; *s; s++) switch (*s) { case '&': fputs("&amp;", f); break; case '<': fputs("&lt;", f); break; case '>': fputs("&gt;", f); break; case '"': fputs("&quot;", f); break; case '\n': fputs("<br>", f); break; default: fputc(*s, f); }
}
static void print_invoice(int id) {
    char no[40], date[11], fn[100], pay[8]; int paid, canc;
    snprintf(no, 40, "%s", txt("SELECT no FROM invoice WHERE id=%d", id)); snprintf(date, 11, "%s", txt("SELECT date FROM invoice WHERE id=%d", id));
    snprintf(pay, 8, "%s", txt("SELECT method FROM invoice WHERE id=%d", id)); paid = (int)val("SELECT paid FROM invoice WHERE id=%d", id); canc = txt("SELECT status FROM invoice WHERE id=%d", id)[0] == 'X';
    long long tot = (long long)val("SELECT total FROM invoice WHERE id=%d", id);
    const char *need[] = {"co_name", "co_addr", "co_tax"};
    for (int i = 0; i < 3; i++) if (!*txt("SELECT v FROM settings WHERE k=%Q", need[i])) { puts("UYARI: Firma adı/adresi/vergi no eksik (Ayarlar > Firma bilgileri). Yasal fatura için tamamlayın!"); break; }
    snprintf(fn, 100, "fatura_%s.html", no); for (char *c = fn + 7; *c; c++) if (!(isalnum((unsigned char)*c) || *c == '-' || *c == '_' || *c == '.')) *c = '_'; snprintf(lastfile, sizeof lastfile, "%s", fn); FILE *f = fopen(fn, "w"); if (!f) { puts("Dosya yazılamadı."); return; }
    fputs("<!DOCTYPE html><html lang=\"de\"><head><meta charset=\"utf-8\"><title>Rechnung ", f); hesc(f, no);
    fputs("</title><style>@page{size:A4;margin:15mm}body{font:12px Arial,sans-serif;max-width:180mm;margin:auto}table{width:100%;border-collapse:collapse}th,td{padding:5px;border-bottom:1px solid #ccc;text-align:left}.r{text-align:right}h1{margin:20px 0 5px}.sm{font-size:10px;color:#444;margin-top:25px;border-top:1px solid #000;padding-top:6px}.wm{position:fixed;top:40%;left:15%;font-size:90px;color:rgba(200,0,0,.25);transform:rotate(-20deg)}@media print{.np{display:none}}</style></head><body><div class=\"np\" style=\"text-align:right\"><button style=\"font-size:16px;padding:8px 18px\" onclick=\"print()\">Yazdır / PDF olarak kaydet</button></div>", f);
    if (canc) fputs("<div class=\"wm\">STORNIERT</div>", f);
    fputs("<div style=\"text-align:right\"><b style=\"font-size:16px\">", f); hesc(f, txt("SELECT v FROM settings WHERE k='co_name'")); fputs("</b><br>", f); hesc(f, txt("SELECT v FROM settings WHERE k='co_addr'")); fputs("<br>", f); hesc(f, txt("SELECT v FROM settings WHERE k='co_phone'")); fputs(" ", f); hesc(f, txt("SELECT v FROM settings WHERE k='co_email'"));
    fputs("</div><p style=\"font-size:9px\">", f); hesc(f, txt("SELECT v FROM settings WHERE k='co_name'")); fputs(" · ", f); hesc(f, txt("SELECT v FROM settings WHERE k='co_addr'")); fputs("</p><p><b>", f);
    hesc(f, txt("SELECT p.name FROM party p JOIN invoice i ON i.party_id=p.id WHERE i.id=%d", id)); fputs("</b><br>", f); hesc(f, txt("SELECT p.company FROM party p JOIN invoice i ON i.party_id=p.id WHERE i.id=%d", id)); fputs("<br>", f); hesc(f, txt("SELECT p.address FROM party p JOIN invoice i ON i.party_id=p.id WHERE i.id=%d", id));
    fprintf(f, "</p><h1>Rechnung</h1><p>Rechnungsnummer: <b>%s</b><br>Rechnungsdatum: %.2s.%.2s.%.4s<br>Liefer-/Leistungsdatum: %.2s.%.2s.%.4s</p><table><tr><th>Pos.</th><th>Bezeichnung</th><th class=\"r\">Menge</th><th>Einheit</th><th class=\"r\">Einzelpreis (brutto)</th><th class=\"r\">Gesamt (brutto)</th></tr>", no, date + 8, date + 5, date, date + 8, date + 5, date);
    sqlite3_stmt *s; char *q = sqlite3_vmprintf_wrap("SELECT r.name,r.unit,l.qty,l.price,l.total FROM line l JOIN product r ON r.id=l.product_id WHERE l.inv_id=%d ORDER BY l.id", id); sqlite3_prepare_v2(db, q, -1, &s, 0); sqlite3_free(q);
    for (int pos = 1; sqlite3_step(s) == SQLITE_ROW; pos++) { fprintf(f, "<tr><td>%d</td><td>", pos); hesc(f, (const char *)sqlite3_column_text(s, 0)); fprintf(f, "</td><td class=\"r\">%s</td><td>", Q(sqlite3_column_double(s, 2))); hesc(f, (const char *)sqlite3_column_text(s, 1)); fprintf(f, "</td><td class=\"r\">%s</td><td class=\"r\">%s</td></tr>", M(sqlite3_column_int64(s, 3)), M(sqlite3_column_int64(s, 4))); }
    sqlite3_finalize(s);
    fputs("</table><br><table style=\"width:60%;margin-left:auto\"><tr><th>MwSt.-Satz</th><th class=\"r\">Netto</th><th class=\"r\">MwSt.</th><th class=\"r\">Brutto</th></tr>", f);
    q = sqlite3_vmprintf_wrap("SELECT vat,SUM(total) FROM line WHERE inv_id=%d GROUP BY vat", id); sqlite3_prepare_v2(db, q, -1, &s, 0); sqlite3_free(q);
    while (sqlite3_step(s) == SQLITE_ROW) { double v = sqlite3_column_double(s, 0); long long g = sqlite3_column_int64(s, 1), n = llround(g / (1 + v / 100)); fprintf(f, "<tr><td>%s %%</td><td class=\"r\">%s</td><td class=\"r\">%s</td><td class=\"r\">%s</td></tr>", Q(v), M(n), M(g - n), M(g)); }
    sqlite3_finalize(s);
    fprintf(f, "<tr><th colspan=\"3\">Rechnungsbetrag (brutto)</th><th class=\"r\">%s</th></tr></table><p>", M(tot));
    if (paid) fprintf(f, "Der Betrag wurde <b>bezahlt</b> (%s).", pay[0] == 'B' ? "Überweisung" : "bar"); else fputs("Der Betrag ist <b>offen</b> und zur Zahlung fällig.", f);
    fputs("</p><div class=\"sm\">Steuernummer: ", f); hesc(f, txt("SELECT v FROM settings WHERE k='co_tax'")); fputs(" · USt-IdNr.: ", f); hesc(f, txt("SELECT v FROM settings WHERE k='co_vat'")); fputs("<br>Bankverbindung: ", f); hesc(f, txt("SELECT v FROM settings WHERE k='co_iban'")); fputs("</div></body></html>", f);
    fclose(f); printf("Fatura dosyası: %s (tarayıcıdan Ctrl+P ile A4 yazdır veya 'PDF olarak kaydet')\n", fn);
    if (yes("Önizleme için tarayıcıda açılsın mı? (e/h): ")) { char cmd[200]; snprintf(cmd, 200, OPEN_CMD "\"%s\"" QUIET, fn); if (system(cmd)) puts("Açılamadı; dosyayı elle açın."); }
}

/* ---------- kasa / banka / gider ---------- */
static void cashbank(char m) {
    char k[2] = {m, 0}; const char *nm = m == 'N' ? "Kasa (nakit)" : "Banka";
    printf("\n%s bakiyesi: %s\n", nm, M(cashbal(m)));
    SHOW("SELECT " DF("d") " Tarih,t Açıklama,CASE WHEN a>0 THEN a ELSE 0 END Giriş_c,CASE WHEN a<0 THEN -a ELSE 0 END Çıkış_c,run Bakiye_c FROM (SELECT d,t,a,%lld+SUM(a) OVER (ORDER BY d,t,a) run FROM fin WHERE m=%Q) ORDER BY d,t,a", setn(m == 'N' ? "cash_open" : "bank_open"), k);
}
static void expense_menu(void) {
    for (;;) {
        int c = menu("Giderler", "1) Gider ekle\n2) Gider listesi\n3) Kategori toplamları\n4) Gider iptal\n");
        if (!c) return;
        if (c == 1) {
            char d[11], cat[60], ds[100], a[32], m[8]; static const char *cats[] = {"Mazot", "Araç", "Kira", "Telefon", "Bakım", "Sigorta", "Vergi", "Diğer"};
            for (int i = 0; i < 8; i++) printf("%d) %s  ", i + 1, cats[i]);
            ask("\nKategori no veya yeni ad: ", cat, 60); int ci = atoi(cat); if (ci >= 1 && ci <= 8) snprintf(cat, 60, "%s", cats[ci - 1]);
            if (!*cat) continue; askdate("Tarih (boş=bugün): ", d, defdate(), 1); ask("Açıklama: ", ds, 100); ask("Tutar €: ", a, 32);
            if (cents(a) <= 0) { puts("Tutar geçersiz."); continue; }
            ask("Ödeme (N=Nakit, B=Banka): ", m, 8); m[0] = (m[0] == 'b' || m[0] == 'B') ? 'B' : 'N'; m[1] = 0;
            if (run("INSERT INTO expense(date,category,descr,amount,method) VALUES(%Q,%Q,%Q,%lld,%Q)", d, cat, ds, cents(a), m)) { aud("GIDER eklendi %s %lld kurus", cat, cents(a)); puts("Gider kaydedildi (kasa/banka güncellendi)."); }
        } else if (c == 2) SHOW("SELECT id ID," DF("date") " Tarih,category Kategori,descr Açıklama,amount Tutar_c,CASE method WHEN 'B' THEN 'Banka' ELSE 'Nakit' END Ödeme,CASE status WHEN 'X' THEN 'İPTAL' ELSE '' END Durum FROM expense ORDER BY date,id");
        else if (c == 3) SHOW("SELECT category Kategori,COUNT(*) Adet,SUM(amount) Toplam_c FROM expense WHERE status='A' GROUP BY category ORDER BY 3 DESC");
        else if (c == 4) {
            int id = num("İptal edilecek gider ID: "); char r[100]; if (!val("SELECT COUNT(*) FROM expense WHERE id=%d AND status='A'", id)) { puts("Bulunamadı."); continue; }
            ask("Neden: ", r, 100); if (yes("Emin misiniz? (e/h): ") && run("UPDATE expense SET status='X' WHERE id=%d", id)) { aud("GIDER iptal id=%d neden=%s", id, r); puts("İptal edildi."); }
        }
    }
}

/* ---------- raporlar ---------- */
static void pick_period(char *f, char *t) {
    int c = num("Dönem: 1 Bugün 2 Bu hafta 3 Bu ay 4 Geçen ay 5 Tüm yıl 6 İki tarih arası [5]: ");
    if (c == 1) { snprintf(f, 11, "%s", today()); snprintf(t, 11, "%s", f); }
    else if (c == 2) { snprintf(f, 11, "%s", txt("SELECT date('now','localtime','weekday 0','-6 days')")); snprintf(t, 11, "%s", txt("SELECT date(%Q,'+6 days')", f)); }
    else if (c == 3) { snprintf(f, 11, "%s", txt("SELECT date('now','localtime','start of month')")); snprintf(t, 11, "%s", txt("SELECT date(%Q,'+1 month','-1 day')", f)); }
    else if (c == 4) { snprintf(f, 11, "%s", txt("SELECT date('now','localtime','start of month','-1 month')")); snprintf(t, 11, "%s", txt("SELECT date(%Q,'+1 month','-1 day')", f)); }
    else if (c == 6) { askdate("Başlangıç: ", f, NULL, 0); askdate("Bitiş: ", t, NULL, 0); }
    else { snprintf(f, 11, "%d-01-01", year); snprintf(t, 11, "%d-12-31", year); }
    printf("Dönem: %s - %s\n", f, t);
}
static void summary(const char *f, const char *t) {
    setrange(f, t); double rev, cogs, sv; replay(f, t, &rev, &cogs, &sv, NULL);
    long long sales = llround(val("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='S' AND status='A' AND date BETWEEN %Q AND %Q", f, t));
    long long cashsale = llround(val("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='S' AND status='A' AND paid=1 AND date BETWEEN %Q AND %Q", f, t));
    long long buy = llround(val("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='P' AND status='A' AND date BETWEEN %Q AND %Q", f, t));
    long long exp = llround(val("SELECT COALESCE(SUM(amount),0) FROM expense WHERE status='A' AND date BETWEEN %Q AND %Q", f, t));
    long long col = llround(val("SELECT COALESCE(SUM(x.amount),0) FROM payment x JOIN party r ON r.id=x.party_id WHERE r.kind='C' AND x.status='A' AND x.date BETWEEN %Q AND %Q", f, t));
    long long sp = llround(val("SELECT COALESCE(SUM(x.amount),0) FROM payment x JOIN party r ON r.id=x.party_id WHERE r.kind='S' AND x.status='A' AND x.date BETWEEN %Q AND %Q", f, t));
    long long r = llround(rev), cg = llround(cogs);
    printf("\n--- SATIŞ ---\nSatış (KDV dahil)            %16s\n  Peşin/ödenmiş satış        %16s\n  Veresiye (cariye yazılan)  %16s\nCari tahsilatları            %16s\n", M(sales), M(cashsale), M(sales - cashsale), M(col));
    printf("\n--- ALIŞ / GİDER ---\nToplam alış                  %16s\nTedarikçiye yapılan ödemeler %16s\nİşletme giderleri            %16s\n", M(buy), M(sp), M(exp));
    printf("\n--- KÂR (ortalama maliyet, KDV hariç) ---\nSatış cirosu (net)           %16s\nSatılan ürün maliyeti        %16s\nBrüt kâr                     %16s\nİşletme giderleri            %16s\nGiderler sonrası sonuç       %16s\nStok değeri (yıl sonu, ort. maliyet) %s\n", M(r), M(-cg), M(r - cg), M(-exp), M(r - cg - exp), M(llround(sv)));
    printf("\n--- ANLIK DURUM ---\nMüşteri alacağı              %16s\nTedarikçi borcu              %16s\nKasa bakiyesi                %16s\nBanka bakiyesi               %16s\n", M(totbal('C')), M(totbal('S')), M(cashbal('N')), M(cashbal('B')));
}
static void year_compare(void) {
    DIR *dir = opendir("."); struct dirent *e; int ys[64], n = 0, y; char tail;
    while (dir && (e = readdir(dir))) if (strlen(e->d_name) == 15 && sscanf(e->d_name, "mixpack_%4d.d%c", &y, &tail) == 2 && tail == 'b' && n < 64) ys[n++] = y;
    if (dir) closedir(dir);
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (ys[j] < ys[i]) { int t = ys[i]; ys[i] = ys[j]; ys[j] = t; }
    sqlite3 *keep = db; printf("\n%-6s %16s %16s %16s %16s %8s\n", "Yıl", "Satış", "Alış", "Gider", "Tahsilat(cari)", "Fatura");
    for (int i = 0; i < n; i++) {
        char p[40]; snprintf(p, 40, "mixpack_%d.db", ys[i]); if (sqlite3_open_v2(p, &db, SQLITE_OPEN_READONLY, 0)) { db = keep; continue; }
        printf("%-6d %16s ", ys[i], M(llround(val("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='S' AND status='A'"))));
        printf("%16s ", M(llround(val("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='P' AND status='A'"))));
        printf("%16s ", M(llround(val("SELECT COALESCE(SUM(amount),0) FROM expense WHERE status='A'"))));
        printf("%16s %8d\n", M(llround(val("SELECT COALESCE(SUM(x.amount),0) FROM payment x JOIN party r ON r.id=x.party_id WHERE r.kind='C' AND x.status='A'"))), (int)val("SELECT COUNT(*) FROM invoice WHERE status='A'"));
        sqlite3_close(db); db = keep;
    }
}
static void export_csv(void) {
    char f[11], t[11], q[100];
    int c = num("Dışa aktar: 1 Satış faturaları 2 Alış faturaları 3 Müşteri bakiyeleri 4 Tedarikçi bakiyeleri 5 Stok listesi 6 Ürün satış raporu 7 Giderler 8 Kasa+Banka hareketleri: ");
    if (c < 1 || c > 8) return;
    snprintf(q, 100, "mixpack_export_%d.csv", c); csvf = fopen(q, "wb"); if (!csvf) { puts("Dosya açılamadı."); return; }
    fputs("\xEF\xBB\xBF", csvf);
    if (c == 1 || c == 2) SHOW("SELECT i.no No," DF("i.date") " Tarih,p.name Cari,i.total Tutar_c,CASE i.paid WHEN 1 THEN 'ÖDENDİ' ELSE 'ÖDENMEDİ' END Durum,i.status Aktif FROM invoice i JOIN party p ON p.id=i.party_id WHERE i.type=%Q ORDER BY i.date,i.id", c == 1 ? "S" : "P");
    else if (c == 3 || c == 4) SHOW("SELECT p.code Kod,p.name Ad,p.company Firma," BAL " Bakiye_c FROM party p WHERE p.kind=%Q ORDER BY p.name", c == 3 ? "C" : "S");
    else if (c == 5) SHOW("SELECT p.code Kod,p.name Ürün,p.unit Birim," STK " Stok,p.min_stock Min FROM product p ORDER BY p.name");
    else if (c == 6) { pick_period(f, t); setrange(f, t); prodsales("HAVING Satılan>0 OR Alınan>0 ORDER BY Satış_c DESC", 1000); }
    else if (c == 7) SHOW("SELECT " DF("date") " Tarih,category Kategori,descr Açıklama,amount Tutar_c,method Ödeme FROM expense WHERE status='A' ORDER BY date");
    else SHOW("SELECT " DF("d") " Tarih,CASE m WHEN 'B' THEN 'Banka' ELSE 'Kasa' END Hesap,t Açıklama,a Tutar_c FROM fin ORDER BY d,t");
    fclose(csvf); csvf = NULL; printf("Kaydedildi: %s (Excel ile açılır, ayırıcı ;)\n", q);
}
static void report_menu(void) {
    for (;;) {
        int c = menu("Raporlar", "1) Dönem özeti + kâr raporu\n2) Satış dağılımı (günlük/haftalık/aylık)\n3) Ürün bazında alış/satış\n4) Müşteri bazında satış\n5) Fatura raporları (tümü/ödenen/ödenmeyen/ara)\n6) Müşteri alacakları (borçlular)\n7) Tedarikçi borçları\n8) Yıl karşılaştırma\n9) Excel/CSV dışa aktar\n10) İşlem geçmişi (iptal/düzeltme izleri)\n");
        if (!c) return; char f[11], t[11];
        if (c == 1) { pick_period(f, t); summary(f, t); }
        else if (c == 2) { pick_period(f, t); int g = num("Gruplama: 1 Gün 2 Hafta 3 Ay [1]: "); const char *fm = g == 3 ? "%Y-%m" : g == 2 ? "%Y-W%W" : "%Y-%m-%d";
            SHOW("SELECT strftime(%Q,date) Dönem,COUNT(*) Fatura,SUM(total) Satış_c FROM invoice WHERE type='S' AND status='A' AND date BETWEEN %Q AND %Q GROUP BY 1 ORDER BY 1", fm, f, t); }
        else if (c == 3) { pick_period(f, t); setrange(f, t); prodsales("HAVING Satılan>0 OR Alınan>0 ORDER BY Satış_c DESC", 1000); }
        else if (c == 4) { pick_period(f, t); SHOW("SELECT p.code Kod,p.name Müşteri,COUNT(i.id) Fatura,SUM(i.total) Satış_c,SUM(CASE WHEN i.paid=1 THEN i.total ELSE 0 END) Peşin_c FROM party p JOIN invoice i ON i.party_id=p.id AND i.type='S' AND i.status='A' AND i.date BETWEEN %Q AND %Q WHERE p.kind='C' GROUP BY p.id ORDER BY 4 DESC", f, t); }
        else if (c == 5) { int k = num("1 Satış 2 Alış 3 İkisi [1]: "); list_invoices(k == 3 ? '*' : k == 2 ? 'P' : 'S'); }
        else if (c == 6) list_parties('C', 1);
        else if (c == 7) list_parties('S', 1);
        else if (c == 8) year_compare();
        else if (c == 9) export_csv();
        else if (c == 10) SHOW("SELECT id ID,ts Zaman,what İşlem FROM audit ORDER BY id DESC LIMIT 100");
    }
}
static void dashboard(void) {
    char t[11]; snprintf(t, 11, "%s", today()); char mo[8]; snprintf(mo, 8, "%.7s", t);
    printf("\n===== MixPack Muhasebe %d | %s =====\n", year, t);
    printf("Bugünkü satış          %s\nBu ayki satış          %s\n", M(llround(val("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='S' AND status='A' AND date=%Q", t))), M(llround(val("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='S' AND status='A' AND substr(date,1,7)=%Q", mo))));
    printf("Bugünkü tahsilat       %s (cari tahsilat) + %s (peşin satış)\n", M(llround(val("SELECT COALESCE(SUM(x.amount),0) FROM payment x JOIN party r ON r.id=x.party_id WHERE r.kind='C' AND x.status='A' AND x.date=%Q", t))), M(llround(val("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='S' AND paid=1 AND status='A' AND date=%Q", t))));
    printf("Müşteri alacağı        %s\nTedarikçi borcu        %s\nKasa bakiyesi          %s\nBanka bakiyesi         %s\n", M(totbal('C')), M(totbal('S')), M(cashbal('N')), M(cashbal('B')));
    puts("\nKritik stoklar:"); SHOW("SELECT p.name Ürün,p.unit Birim," STK " Stok,p.min_stock Min FROM product p WHERE p.active=1 AND (" STK "<=0 OR (p.min_stock>0 AND " STK "<=p.min_stock)) ORDER BY " STK " LIMIT 10");
    puts("\nSon faturalar:"); SHOW("SELECT i.no No," DF("i.date") " Tarih,CASE i.type WHEN 'S' THEN 'Satış' ELSE 'Alış' END Tür,p.name Cari,i.total Tutar_c,CASE i.paid WHEN 1 THEN 'ÖDENDİ' ELSE 'ÖDENMEDİ' END Durum FROM invoice i JOIN party p ON p.id=i.party_id WHERE i.status='A' ORDER BY i.id DESC LIMIT 5");
}

/* ---------- dosya, yıl devri, yedek, ayarlar ---------- */
static void open_year(int y) {
    year = y; snprintf(dbpath, 300, "mixpack_%d.db", y);
    if (sqlite3_open(dbpath, &db)) { printf("Veritabanı açılamadı: %s\n", dbpath); bye(1); }
    char *e = 0; if (sqlite3_exec(db, SCHEMA, 0, 0, &e)) { printf("Şema hatası: %s\n", e); bye(1); }
    sqlite3_exec(db, "CREATE TEMP TABLE IF NOT EXISTS rng(f TEXT,t TEXT);", 0, 0, 0);
    run("INSERT OR IGNORE INTO settings VALUES('allow_neg','1')"); run("INSERT OR IGNORE INTO settings VALUES('cash_open','0')"); run("INSERT OR IGNORE INTO settings VALUES('bank_open','0')");
    run("INSERT OR IGNORE INTO settings VALUES('co_name','MixPack')");
}
static int scan_years(int *ys) {
    DIR *dir = opendir("."); struct dirent *e; int n = 0, y; char tail;
    while (dir && (e = readdir(dir))) if (strlen(e->d_name) == 15 && sscanf(e->d_name, "mixpack_%4d.d%c", &y, &tail) == 2 && tail == 'b' && n < 64) ys[n++] = y;
    if (dir) closedir(dir);
    for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (ys[j] < ys[i]) { int t = ys[i]; ys[i] = ys[j]; ys[j] = t; }
    return n;
}
static int cpfile(const char *a, const char *b) {
    FILE *i = fopen(a, "rb"), *o = fopen(b, "wb"); if (!i || !o) { if (i) fclose(i); if (o) fclose(o); return 0; }
    char buf[8192]; size_t n; while ((n = fread(buf, 1, sizeof buf, i)) > 0) fwrite(buf, 1, n, o); fclose(i); fclose(o); return 1;
}
/* Açık olan (eski) yıldan hedef yıla devir: cari, ürün, stok, borç, kasa/banka */
static void new_year(int target) {
    int cc = gui ? nyopt[0] : yes("Müşteri/tedarikçi kartları aktarılsın mı? (e/h): "), dbt = cc && (gui ? nyopt[1] : yes("Cari borç/alacak bakiyeleri devredilsin mi? (e/h): "));
    int pc = gui ? nyopt[2] : yes("Ürün kartları aktarılsın mı? (e/h): "), st = pc && (gui ? nyopt[3] : yes("Yıl sonu stokları (ort. maliyetle) devredilsin mi? (e/h): "));
    double rev, cogs, sv; Cy *cy = NULL; int ncy = replay("0000-01-01", "9999-12-31", &rev, &cogs, &sv, &cy);
    long long cash = cashbal('N'), bank = cashbal('B'); char old[300]; snprintf(old, 300, "%s", dbpath); int oy = year;
    sqlite3_close(db); db = NULL; open_year(target);
    run("ATTACH DATABASE %Q AS old", old);
    run("INSERT OR REPLACE INTO settings SELECT k,v FROM old.settings WHERE k NOT IN ('cash_open','bank_open','last_backup')");
    sets("cash_open", txt("SELECT %lld", cash)); sets("bank_open", txt("SELECT %lld", bank));
    if (cc) run("INSERT INTO party(kind,code,name,company,address,phone,email,note,opening,active) SELECT kind,code,name,company,address,phone,email,note,%s,active FROM old.party p",
                dbt ? "p.opening+COALESCE((SELECT SUM(total) FROM old.invoice WHERE party_id=p.id AND paid=0 AND status='A'),0)-COALESCE((SELECT SUM(amount) FROM old.payment WHERE party_id=p.id AND status='A'),0)" : "0");
    if (pc) {
        run("INSERT INTO product(code,name,category,unit,buy_price,sell_price,min_stock,vat,active) SELECT code,name,category,unit,buy_price,sell_price,min_stock,vat,active FROM old.product");
        if (st) for (int i = 0; i < ncy; i++) run("UPDATE product SET opening_qty=%f,opening_cost=%lld WHERE code=(SELECT code FROM old.product WHERE id=%d)", cy[i].q, llround(cy[i].avg * (1 + cy[i].vat / 100) * 100), cy[i].pid);
    }
    run("DETACH DATABASE old"); free(cy); aud("YIL DEVRI %d -> %d", oy, target);
    printf("%d yılı oluşturuldu; fatura numaraları %02d-01-01'den başlar. %d yılı arşiv olarak korundu.\n", target, target % 100, oy);
}
static void do_backup(void) {
    char dir[200], p[400]; ask("Yedek klasörü (USB/harici disk yolu, boş=yedekler): ", dir, 200); if (!*dir) strcpy(dir, "yedekler");
    MKDIR(dir); snprintf(p, 400, "%s/mixpack_%d_%s.db", dir, year, txt("SELECT strftime('%%Y%%m%%d_%%H%%M%%S','now','localtime')"));
    if (run("VACUUM INTO %Q", p)) { sets("last_backup", today()); printf("Yedek alındı: %s\n", p); } else puts("Yedek alınamadı (klasörü/diski kontrol edin).");
}
static void settings_menu(void) {
    for (;;) {
        int c = menu("Ayarlar", "1) Firma bilgileri (fatura için)\n2) Negatif stok satış izni aç/kapat\n3) Açılış kasa/banka bakiyesi\n4) Yedek al\n5) Yedekten geri yükle\n6) Yeni yıl / dönem aç (devir)\n7) Mevcut ayarları göster\n");
        if (!c) return;
        if (c == 1) {
            static const char *f[][2] = {{"co_name", "Firma adı"}, {"co_addr", "Adres (sokak, PLZ, şehir)"}, {"co_tax", "Steuernummer"}, {"co_vat", "USt-IdNr."}, {"co_phone", "Telefon"}, {"co_email", "E-posta"}, {"co_iban", "IBAN/Banka"}};
            for (int i = 0; i < 7; i++) { char b[200], p[200]; snprintf(p, 200, "%s [%s] (boş=aynı): ", f[i][1], txt("SELECT v FROM settings WHERE k=%Q", f[i][0])); ask(p, b, 200); if (*b) sets(f[i][0], b); }
        } else if (c == 2) { sets("allow_neg", setn("allow_neg") ? "0" : "1"); printf("Negatif stoğa satış: %s\n", setn("allow_neg") ? "AÇIK (uyarı verir)" : "KAPALI"); }
        else if (c == 3) { char b[40]; ask("Açılış kasa € : ", b, 40); sets("cash_open", txt("SELECT %lld", cents(b))); ask("Açılış banka €: ", b, 40); sets("bank_open", txt("SELECT %lld", cents(b))); aud("ACILIS kasa/banka ayarlandi"); }
        else if (c == 4) do_backup();
        else if (c == 5) {
            char p[300]; ask("Geri yüklenecek yedek dosyası: ", p, 300); sqlite3 *t; int ok = sqlite3_open_v2(p, &t, SQLITE_OPEN_READONLY, 0) == 0;
            if (ok) { sqlite3 *k = db; db = t; ok = val("SELECT COUNT(*) FROM party") >= 0 && *txt("SELECT name FROM sqlite_master WHERE name='invoice'"); db = k; sqlite3_close(t); }
            if (!ok) { puts("Geçerli bir MixPack yedeği değil."); continue; }
            if (!yes("MEVCUT yıl verisi bu yedekle DEĞİŞTİRİLİR. Emin misiniz? (e/h): ")) continue;
            sqlite3_close(db); db = NULL; char bk[320]; snprintf(bk, 320, "%s.oncesi", dbpath); cpfile(dbpath, bk);
            if (!cpfile(p, dbpath)) puts("Kopyalama başarısız!"); open_year(year); puts("Geri yüklendi (önceki hali .oncesi dosyasında).");
        } else if (c == 6) {
            int y = num("Açılacak yıl (örn. 2027): "); char p[40]; snprintf(p, 40, "mixpack_%d.db", y);
            FILE *t = fopen(p, "rb"); if (t) { fclose(t); puts("Bu yıl zaten var."); continue; } if (y < 2000) continue;
            new_year(y);
        } else if (c == 7) SHOW("SELECT k Ayar,v Değer FROM settings ORDER BY k");
    }
}

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(65001); SetConsoleCP(65001);
#endif
    chdir_exe();
    puts("MixPack Muhasebe - offline / yerel / açık kaynak");
    int ys[64], n = scan_years(ys), y = 0;
    if (argc > 1) y = atoi(argv[1]);
    else { if (n) { printf("Mevcut yıllar:"); for (int i = 0; i < n; i++) printf(" MixPack %d", ys[i]); puts(""); } y = num("Çalışılacak yıl (yoksa yeni açılır): "); }
    if (y < 2000) return 0;
    int exists = 0, prev = 0; for (int i = 0; i < n; i++) { if (ys[i] == y) exists = 1; if (ys[i] < y) prev = ys[i]; }
    if (!exists) {
        if (!yes("Bu yıl yok, oluşturulsun mu? (e/h): ")) return 0;
        if (prev && yes("Önceki yıldan devir yapılsın mı? (e/h): ")) { open_year(prev); new_year(y); } else open_year(y);
    } else open_year(y);
    dashboard();
    for (;;) {
        int c = menu("ANA MENÜ", "1) Ana ekran\n2) Satış faturaları\n3) Alış faturaları\n4) Cari hesaplar - Müşteriler\n5) Cari hesaplar - Tedarikçiler\n6) Stoklar\n7) Kasa\n8) Banka\n9) Giderler\n10) Raporlar\n11) Ayarlar\n");
        if (c == 0) {
            const char *lb = txt("SELECT v FROM settings WHERE k='last_backup'");
            if ((!*lb || val("SELECT julianday('now','localtime')-julianday(%Q)", lb) > 7) && yes("UYARI: Son 7 günde yedek alınmamış. Bilgisayar değişir/bozulursa veri kaybolur. Şimdi yedek alınsın mı? (e/h): ")) do_backup();
            break;
        }
        switch (c) {
            case 1: dashboard(); break; case 2: inv_menu('S'); break; case 3: inv_menu('P'); break;
            case 4: party_menu('C'); break; case 5: party_menu('S'); break; case 6: stock_menu(); break;
            case 7: cashbank('N'); break; case 8: cashbank('B'); break; case 9: expense_menu(); break;
            case 10: report_menu(); break; case 11: settings_menu(); break;
        }
    }
    bye(0); return 0;
}
