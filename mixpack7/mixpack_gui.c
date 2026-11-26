/* MixPack Muhasebe - grafik arayüz (yerel web arayüzü, offline, MIT)
 * mixpack.c'deki iş mantığını ve veritabanını aynen kullanır; yalnızca 127.0.0.1'e bağlanır.
 * Kendi kendini sınama: mixpack_gui.exe --selftest   (soket, API, fatura, yedek, yıl devri; gerçek verinize dokunmaz)
 * Derleme: gcc -O2 mixpack_gui.c sqlite3.c -o mixpack_gui.exe -lm -lws2_32   (Linux: -lm)
 * Kaynakça: SQLite (kamu malı) https://sqlite.org ; HTTP sunucusu BSD/Winsock soketleriyle sıfırdan yazıldı. */
#ifndef __USE_MINGW_ANSI_STDIO
#define __USE_MINGW_ANSI_STDIO 1
#endif
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define CLOSESOCK closesocket
#define STRNICMP _strnicmp
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <strings.h>
typedef int sock_t;
#define CLOSESOCK close
#define STRNICMP strncasecmp
#endif
#include <ctype.h>
#include <time.h>
#define main cli_main
#include "mixpack.c"
#undef main
#include "ui_html.h"

#include "mp_api.h"

/* ---------- HTTP ---------- */
static char rq[1 << 21], token[40];
static void sendr(sock_t c, const char *status, const char *ctype, const char *body, size_t n, const char *extra) {
    char h[400]; int l = snprintf(h, 400, "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\nCache-Control: no-store\r\nConnection: close\r\nX-Content-Type-Options: nosniff\r\n%s\r\n", status, ctype, (unsigned long)n, extra ? extra : "");
    send(c, h, l, 0); size_t off = 0; while (off < n) { int r = (int)send(c, body + off, (int)(n - off > 65536 ? 65536 : n - off), 0); if (r <= 0) break; off += r; }
}
static const char *hdrval(const char *req, const char *name, char *out, int n) {
    size_t nl = strlen(name); const char *p = strstr(req, "\r\n"); out[0] = 0;
    while (p && p[0] && !(p[2] == '\r')) { p += 2; if (!STRNICMP(p, name, nl) && p[nl] == ':') { const char *v = p + nl + 1; while (*v == ' ') v++; int i = 0; while (*v && *v != '\r' && i < n - 1) out[i++] = *v++; out[i] = 0; return out; } p = strstr(p, "\r\n"); }
    return out;
}
static void handle(sock_t c) {
    int n = 0, hl = 0, cl = 0; char tmp[300];
    for (;;) {
        int r = (int)recv(c, rq + n, (int)sizeof rq - 1 - n, 0); if (r <= 0) break; n += r; rq[n] = 0; char *h = strstr(rq, "\r\n\r\n");
        if (h) { hl = (int)(h - rq) + 4; cl = atoi(hdrval(rq, "Content-Length", tmp, 300)); if (n >= hl + cl) break; }
        if (n >= (int)sizeof rq - 2) break;
    }
    if (!hl) return; rq[n] = 0;
    char host[100]; hdrval(rq, "Host", host, 100);
    if (strncmp(host, "127.0.0.1", 9) && strncmp(host, "localhost", 9)) { sendr(c, "403 Forbidden", "text/plain", "yasak", 5, NULL); return; }
    char method[8] = "", path[600] = ""; sscanf(rq, "%7s %599s", method, path);
    char *body = rq + hl; body[cl] = 0;
    if (!strcmp(method, "GET") && (!strcmp(path, "/") || !strncmp(path, "/?", 2))) {
        SB o = {0}; for (int i = 0; UI[i]; i++) sba(&o, UI[i]);
        char *ph = strstr(o.p, "__TOKEN__"); if (ph) { memmove(ph + strlen(token), ph + 9, strlen(ph + 9) + 1); memcpy(ph, token, strlen(token)); o.n = strlen(o.p); }
        sendr(c, "200 OK", "text/html; charset=utf-8", o.p, o.n, NULL); free(o.p); return;
    }
    char *q = strchr(path, '?'); char qs[500] = ""; if (q) { *q = 0; snprintf(qs, 500, "%s", q + 1); }
    if (!strcmp(method, "GET") && (!strncmp(path, "/f/", 3) || !strcmp(path, "/csv"))) {
        np = 0; parse(qs); if (strcmp(P("k"), token)) { sendr(c, "403 Forbidden", "text/plain", "yasak", 5, NULL); return; }
        if (!strcmp(path, "/csv")) {
            SB o = {0}; sba(&o, "\xEF\xBB\xBF"); csv_export(&o, atoi(P("x")), P("f"), P("t"));
            sendr(c, "200 OK", "text/csv; charset=utf-8", o.p, o.n, "Content-Disposition: attachment; filename=\"mixpack_export.csv\"\r\n"); free(o.p); return;
        }
        const char *fn = path + 3; if (strncmp(fn, "fatura_", 7) || strstr(fn, "..") || strchr(fn, '/') || strchr(fn, '\\') || !strstr(fn, ".html")) { sendr(c, "404 Not Found", "text/plain", "yok", 3, NULL); return; }
        FILE *f = fopen(fn, "rb"); if (!f) { sendr(c, "404 Not Found", "text/plain", "yok", 3, NULL); return; }
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET); char *b = malloc(sz + 1); size_t rd = fread(b, 1, sz, f); fclose(f);
        sendr(c, "200 OK", "text/html; charset=utf-8", b, rd, NULL); free(b); return;
    }
    if (!strcmp(method, "POST") && !strcmp(path, "/api")) {
        char tk[60]; hdrval(rq, "X-MP", tk, 60); if (strcmp(tk, token)) { sendr(c, "403 Forbidden", "text/plain", "yasak", 5, NULL); return; }
        parse(body); SB o = {0}; lasterr[0] = 0; api(&o); if (!o.p) sba(&o, "{}");
        sendr(c, "200 OK", "application/json; charset=utf-8", o.p, o.n, NULL); free(o.p); return;
    }
    sendr(c, "404 Not Found", "text/plain", "yok", 3, NULL);
}

static sock_t make_listener(int *port) {
    sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
#ifndef _WIN32
    int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one); /* Windows'ta açılmaz: port çalma riski */
#endif
    struct sockaddr_in ad; memset(&ad, 0, sizeof ad); ad.sin_family = AF_INET; ad.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    for (*port = 8765; *port < 8790; (*port)++) { ad.sin_port = htons((unsigned short)*port); if (!bind(ls, (struct sockaddr *)&ad, sizeof ad)) break; }
    if (*port >= 8790 || listen(ls, 16)) return (sock_t)-1;
    return ls;
}
static void set_timeout(sock_t c) {
#ifdef _WIN32
    DWORD to = 1500; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof to);
#else
    struct timeval to = {1, 500000}; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof to);
#endif
}
static void make_token(void) {
    srand((unsigned)time(0) ^ (unsigned)clock() ^ 0x9e3779b9u); snprintf(token, 40, "%08x%08x%08x", (unsigned)rand() * 2654435761u, (unsigned)rand(), (unsigned)rand());
}

/* ---------- kendi kendini sınama (tek işlemde istemci+sunucu; gerçek soketler kullanılır) ---------- */
static int st_fail, st_port;
static void chk(const char *name, int ok) { printf("%s  %s\n", ok ? "TAMAM" : "HATA ", name); if (!ok) st_fail = 1; }
static char *http(sock_t ls, const char *req) {
    sock_t c = socket(AF_INET, SOCK_STREAM, 0); struct sockaddr_in a; memset(&a, 0, sizeof a); a.sin_family = AF_INET; a.sin_port = htons((unsigned short)st_port); a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(c, (struct sockaddr *)&a, sizeof a)) { CLOSESOCK(c); return strdup(""); }
    send(c, req, (int)strlen(req), 0);
    sock_t s = accept(ls, 0, 0); set_timeout(s); handle(s); CLOSESOCK(s);
    SB o = {0}; char b[4096]; int r; while ((r = (int)recv(c, b, sizeof b - 1, 0)) > 0) { b[r] = 0; sba(&o, b); }
    CLOSESOCK(c); return o.p ? o.p : strdup("");
}
static char *post(sock_t ls, const char *body, const char *tok, const char *host) {
    char *rq2 = malloc(strlen(body) + 300); sprintf(rq2, "POST /api HTTP/1.1\r\nHost: %s\r\nX-MP: %s\r\nContent-Length: %d\r\n\r\n%s", host, tok, (int)strlen(body), body);
    char *r = http(ls, rq2); free(rq2); return r;
}
static const char *bodyof(const char *r) { const char *b = strstr(r, "\r\n\r\n"); return b ? b + 4 : r; }
static int selftest(void) {
    puts("MixPack kendi kendini sınama (gerçek verinize dokunmaz: mixpack_2099.db / mixpack_2100.db geçici dosyalardır)");
    remove("mixpack_2099.db"); remove("mixpack_2100.db"); open_year(2099); make_token();
    sock_t ls = make_listener(&st_port); chk("yerel sunucu 127.0.0.1 üzerinde açıldı", ls != (sock_t)-1); if (ls == (sock_t)-1) return 1;
    char *r, host[40]; snprintf(host, 40, "127.0.0.1:%d", st_port);
    char q[300]; snprintf(q, 300, "GET / HTTP/1.1\r\nHost: %s\r\n\r\n", host); r = http(ls, q);
    chk("ana sayfa açılıyor ve anahtar yerleşti", strstr(r, "200 OK") && strstr(r, "MixPack") && !strstr(r, "__TOKEN__") && strstr(r, token)); free(r);
    r = post(ls, "a=dash", "yanlis", host); chk("anahtarsız/yanlış anahtarlı istek reddedildi", strstr(r, "403") != NULL); free(r);
    r = post(ls, "a=dash", token, "evil.com"); chk("yabancı Host başlığı reddedildi", strstr(r, "403") != NULL); free(r);
    snprintf(q, 300, "GET /f/..%%2Fmixpack_2099.db?k=%s HTTP/1.1\r\nHost: %s\r\n\r\n", token, host); r = http(ls, q); chk("dosya yolu hilesi engellendi", !strstr(r, "200 OK")); free(r);
#define API(name, body, cond) do { r = post(ls, body, token, host); const char *J = bodyof(r); (void)J; chk(name, !!(cond)); free(r); } while (0)
    API("ürün kaydı (Türkçe karakter)", "a=product_save&id=0&name=Salatal%C4%B1k&unit=kg&buy=1,50&sell=2,00&minq=5&vat=7&oqty=0", strstr(J, "\"ok\":1"));
    API("tedarikçi kaydı", "a=party_save&id=0&k=S&name=Hollanda+Gem%C3%BCse", strstr(J, "\"ok\":1"));
    API("müşteri kaydı", "a=party_save&id=0&k=C&name=%C5%9Ei%C5%9Fli+M%C3%BC%C5%9Fteri+%C4%B0%C4%9Fne", strstr(J, "\"ok\":1"));
    API("alış faturası (100 kg)", "a=inv_save&type=P&d=2099-11-01&pid=1&no=T-1&paid=0&lp=1&lq=100&lx=1,50", strstr(J, "\"ok\":1"));
    API("satış faturası otomatik no", "a=inv_save&type=S&d=2099-11-02&pid=2&paid=0&lp=1&lq=30&lx=2,00", strstr(J, "\"no\":\"99-11-01\""));
    API("stok 100-30=70", "a=products", strstr(J, "\"stock\":70"));
    API("Türkçe karakterler bozulmadan döndü", "a=parties&k=C", strstr(J, "Şişli Müşteri İğne") && strstr(J, "\"bal_c\":6000"));
    API("kısmi tahsilat 20 €", "a=pay_add&id=2&amount=20&method=N&d=2099-11-03", strstr(J, "\"ok\":1"));
    API("bakiye 40 €, kasa 20 €", "a=cash&m=N", strstr(J, "\"bal_c\":2000"));
    API("mükerrer fatura no reddedildi", "a=inv_save&type=S&d=2099-11-04&pid=2&no=99-11-01&paid=0&lp=1&lq=1&lx=2", strstr(J, "zaten kullan"));
    API("dönem dışı tarih reddedildi", "a=inv_save&type=S&d=2098-11-04&pid=2&paid=0&lp=1&lq=1&lx=2", strstr(J, "err"));
    API("yasak karakterli fatura no (99/11:05*)", "a=inv_save&type=S&d=2099-11-05&pid=2&no=99%2F11%3A05*&paid=1&method=B&lp=1&lq=5&lx=2", strstr(J, "\"ok\":1"));
    r = post(ls, "a=inv_print&id=3", token, host); const char *J = bodyof(r); char fn[200] = ""; const char *f = strstr(J, "\"file\":\""); if (f) sscanf(f + 8, "%199[^\"]", fn);
    chk("fatura dosyası yazıldı (güvenli dosya adı)", *fn && !strpbrk(fn, "/\\:*") ); free(r);
    snprintf(q, 300, "GET /f/%s?k=%s HTTP/1.1\r\nHost: %s\r\n\r\n", fn, token, host); r = http(ls, q); chk("fatura sayfası sunuluyor (Rechnung)", strstr(r, "Rechnung") && strstr(r, "MwSt")); free(r); if (*fn) remove(fn);
    snprintf(q, 300, "GET /csv?x=1&k=%s HTTP/1.1\r\nHost: %s\r\n\r\n", token, host); r = http(ls, q); chk("CSV dışa aktarma", strstr(r, "text/csv") && strstr(r, "99-11-01")); free(r);
    API("fatura iptali", "a=inv_cancel&id=2&reason=test", strstr(J, "\"ok\":1"));
    API("iptal sonrası stok 100-5=95", "a=products", strstr(J, "\"stock\":95"));
    r = post(ls, "a=backup&dir=.", token, host); J = bodyof(r); char bk[300] = ""; f = strstr(J, "\"path\":\""); if (f) sscanf(f + 8, "%299[^\"]", bk);
    FILE *bf = *bk ? fopen(bk, "rb") : NULL; chk("yedek dosyası oluştu", bf != NULL); if (bf) { fclose(bf); remove(bk); } free(r);
    API("yeni yıl devri (2100)", "a=newyear&y=2100&cc=1&dbt=1&pc=1&st=1", strstr(J, "\"ok\":1"));
    API("devir: yeni yılda stok 95 ve cari bakiye", "a=products", strstr(J, "\"stock\":95"));
    API("devir: müşteri bakiyesi taşındı", "a=parties&k=C", strstr(J, "Şişli") && strstr(J, "\"bal_c\":-2000"));
    API("yeni yılda fatura no 00-01'den değil 00-01-01 serisi", "a=nextno&d=2100-01-05", strstr(J, "\"no\":\"00-01-01\""));
    CLOSESOCK(ls); sqlite3_close(db); db = NULL; remove("mixpack_2099.db"); remove("mixpack_2100.db");
    printf("\n%s\n", st_fail ? "SONUÇ: BAŞARISIZ — yukarıdaki HATA satırlarını geliştiriciye iletin." : "SONUÇ: Tüm denetimler başarılı."); return st_fail;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); SetConsoleOutputCP(65001);
#endif
    gui = 1; chdir_exe();
    if (argc > 1 && !strcmp(argv[1], "--selftest")) return selftest();
    int ys[64], n = scan_years(ys), y; time_t tt = time(0);
    if (argc > 1 && atoi(argv[1]) >= 2000) y = atoi(argv[1]); else if (n) y = ys[n - 1]; else y = localtime(&tt)->tm_year + 1900;
    open_year(y); make_token();
    int port; sock_t ls = make_listener(&port);
    if (ls == (sock_t)-1) { puts("Port açılamadı."); return 1; }
    printf("MixPack Muhasebe %d\nTarayıcıda açılan pencere programın arayüzüdür: http://127.0.0.1:%d/\nBu siyah pencereyi KAPATMAYIN (programı arayüzdeki 'Kapat' ile kapatın).\n", year, port);
    if (!getenv("MIXPACK_NOBROWSER")) { char cmd[120]; snprintf(cmd, 120, OPEN_CMD "\"http://127.0.0.1:%d/\"" QUIET, port); if (system(cmd)) puts("Tarayıcı otomatik açılamadı; adresi elle açın."); }
    while (!quitflag) {
        sock_t c = accept(ls, 0, 0);
#ifdef _WIN32
        if (c == INVALID_SOCKET) continue;
#else
        if (c < 0) continue;
#endif
        set_timeout(c); handle(c); CLOSESOCK(c);
    }
    CLOSESOCK(ls); sqlite3_close(db); return 0;
}
