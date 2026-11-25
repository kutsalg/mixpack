/* MixPack Muhasebe - grafik arayüz (yerel web arayüzü, offline, MIT)
 * mixpack.c'deki iş mantığını ve veritabanını aynen kullanır; yalnızca 127.0.0.1'e bağlanır.
 * Derleme: gcc -O2 mixpack_gui.c sqlite3.c -o mixpack_gui.exe -lm -lws2_32   (Linux: -lm)
 * Kaynakça: SQLite (kamu malı) https://sqlite.org ; HTTP sunucusu BSD/Winsock soketleriyle sıfırdan yazıldı. */
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

/* ---------- metin tamponu ---------- */
typedef struct { char *p; size_t n, cap; } SB;
static void sba(SB *s, const char *t) {
    size_t l = strlen(t); if (s->n + l + 1 > s->cap) { s->cap = (s->n + l + 1) * 2 + 256; s->p = realloc(s->p, s->cap); }
    memcpy(s->p + s->n, t, l + 1); s->n += l;
}
static void sbf(SB *s, const char *f, ...) {
    va_list a, b; va_start(a, f); va_copy(b, a); int n = vsnprintf(NULL, 0, f, a); va_end(a);
    char *t = malloc(n + 1); vsnprintf(t, n + 1, f, b); va_end(b); sba(s, t); free(t);
}
static void sbj(SB *s, const char *t) {
    sba(s, "\""); char u[8];
    for (; *t; t++) { unsigned char c = (unsigned char)*t;
        if (c == '"') sba(s, "\\\""); else if (c == '\\') sba(s, "\\\\"); else if (c == '\n') sba(s, "\\n");
        else if (c < 32) { snprintf(u, 8, "\\u%04x", c); sba(s, u); } else { u[0] = (char)c; u[1] = 0; sba(s, u); } }
    sba(s, "\"");
}
static void qjson(SB *o, const char *sql) {
    sqlite3_stmt *s;
    if (sqlite3_prepare_v2(db, sql, -1, &s, 0)) { snprintf(lasterr, sizeof lasterr, "%s", sqlite3_errmsg(db)); sba(o, "[]"); return; }
    int n = sqlite3_column_count(s), r = 0; sba(o, "[");
    while (sqlite3_step(s) == SQLITE_ROW) {
        if (r++) sba(o, ","); sba(o, "{");
        for (int i = 0; i < n; i++) {
            if (i) sba(o, ","); sbj(o, sqlite3_column_name(s, i)); sba(o, ":"); int t = sqlite3_column_type(s, i);
            if (t == SQLITE_NULL) sba(o, "null"); else if (t == SQLITE_INTEGER) sbf(o, "%lld", sqlite3_column_int64(s, i));
            else if (t == SQLITE_FLOAT) sbf(o, "%.6g", sqlite3_column_double(s, i)); else sbj(o, (const char *)sqlite3_column_text(s, i));
        }
        sba(o, "}");
    }
    sba(o, "]"); sqlite3_finalize(s);
}
static void qj(SB *o, const char *f, ...) { va_list a; va_start(a, f); char *s = sqlite3_vmprintf(f, a); va_end(a); qjson(o, s); sqlite3_free(s); }
static void qcsv(SB *o, const char *sql) {
    sqlite3_stmt *s; if (sqlite3_prepare_v2(db, sql, -1, &s, 0)) return;
    int n = sqlite3_column_count(s), mon[32] = {0};
    for (int i = 0; i < n; i++) { char h[100]; snprintf(h, 100, "%s", sqlite3_column_name(s, i)); size_t l = strlen(h); if (l > 2 && !strcmp(h + l - 2, "_c")) { mon[i] = 1; h[l - 2] = 0; } sba(o, h); sba(o, i == n - 1 ? "\r\n" : ";"); }
    while (sqlite3_step(s) == SQLITE_ROW) for (int i = 0; i < n; i++) {
        char b[600]; int t = sqlite3_column_type(s, i);
        if (t == SQLITE_NULL) b[0] = 0;
        else if (mon[i]) { long long c = llround(sqlite3_column_double(s, i)); snprintf(b, 600, "%s%lld,%02lld", c < 0 ? "-" : "", llabs(c) / 100, llabs(c) % 100); }
        else if (t == SQLITE_FLOAT) { snprintf(b, 600, "%s", Q(sqlite3_column_double(s, i))); for (char *c = b; *c; c++) if (*c == '.') *c = ','; }
        else snprintf(b, 600, "%s", (const char *)sqlite3_column_text(s, i));
        if (strpbrk(b, ";\"\n")) { sba(o, "\""); sba(o, b); sba(o, "\""); } else sba(o, b);
        sba(o, i == n - 1 ? "\r\n" : ";");
    }
    sqlite3_finalize(s);
}

/* ---------- parametreler ---------- */
static char *K[256], *V[256]; static int np;
static void urldec(char *s) {
    char *o = s;
    for (; *s; s++) { if (*s == '+') *o++ = ' '; else if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) { char h[3] = {s[1], s[2], 0}; *o++ = (char)strtol(h, 0, 16); s += 2; } else *o++ = *s; }
    *o = 0;
}
static void parse(char *body) {
    np = 0; char *p = body;
    while (*p && np < 256) { char *amp = strchr(p, '&'); if (amp) *amp = 0; char *eq = strchr(p, '=');
        if (eq) { *eq = 0; K[np] = p; V[np] = eq + 1; urldec(K[np]); urldec(V[np]); np++; } if (!amp) break; p = amp + 1; }
}
static const char *P(const char *k) { for (int i = 0; i < np; i++) if (!strcmp(K[i], k)) return V[i]; return ""; }
static void err(SB *o, const char *m) { sba(o, "{\"err\":"); sbj(o, m); sba(o, "}"); }
static void okid(SB *o, long long id) { sbf(o, "{\"ok\":1,\"id\":%lld}", id); }
static int validdate(const char *d, int strict) { if (strlen(d) != 10) return 0; if (strcmp(txt("SELECT date(%Q)", d), d)) return 0; return !strict || atoi(d) == year; }
static int partyok(int id, const char *kind) { return val("SELECT COUNT(*) FROM party WHERE id=%d AND kind=%Q AND active=1", id, kind) > 0; }
static void nextno(const char *d, char *no) {
    char pre[16], pat[20]; snprintf(pre, 16, "%.2s-%.2s-", d + 2, d + 5); snprintf(pat, 20, "%s%%", pre);
    snprintf(no, 40, "%s%02d", pre, (int)val("SELECT COALESCE(MAX(CAST(substr(no,7) AS INTEGER)),0)+1 FROM invoice WHERE type='S' AND no LIKE %Q", pat));
}
#define VL(...) llround(val(__VA_ARGS__))
#define A(x) (!strcmp(a, x))
static int quitflag;

static void api(SB *o) {
    const char *a = P("a");
    if (A("years")) { int ys[64], n = scan_years(ys); sbf(o, "{\"cur\":%d,\"years\":[", year); for (int i = 0; i < n; i++) sbf(o, "%s%d", i ? "," : "", ys[i]); sba(o, "]}"); }
    else if (A("setyear")) {
        int y = atoi(P("y")); char p[40]; snprintf(p, 40, "mixpack_%d.db", y); FILE *t = fopen(p, "rb");
        if (!t) { err(o, "Bu yıl yok."); return; } fclose(t); sqlite3_close(db); db = NULL; open_year(y); okid(o, y);
    }
    else if (A("dash")) {
        char t[11]; snprintf(t, 11, "%s", today()); char mo[8]; snprintf(mo, 8, "%.7s", t);
        sbf(o, "{\"year\":%d,\"today\":\"%s\",\"sales_today_c\":%lld,\"sales_month_c\":%lld,\"coll_c\":%lld,\"cashsale_c\":%lld,\"recv_c\":%lld,\"debt_c\":%lld,\"cash_c\":%lld,\"bank_c\":%lld,\"backup_days\":%.0f,\"low\":",
            year, t,
            VL("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='S' AND status='A' AND date=%Q", t),
            VL("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='S' AND status='A' AND substr(date,1,7)=%Q", mo),
            VL("SELECT COALESCE(SUM(x.amount),0) FROM payment x JOIN party r ON r.id=x.party_id WHERE r.kind='C' AND x.status='A' AND x.date=%Q", t),
            VL("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='S' AND paid=1 AND status='A' AND date=%Q", t),
            totbal('C'), totbal('S'), cashbal('N'), cashbal('B'),
            val("SELECT COALESCE(julianday('now','localtime')-julianday((SELECT v FROM settings WHERE k='last_backup')),-1)"));
        qj(o, "SELECT p.name,p.unit," STK " stock,p.min_stock minq FROM product p WHERE p.active=1 AND (" STK "<=0 OR (p.min_stock>0 AND " STK "<=p.min_stock)) ORDER BY " STK " LIMIT 10");
        sba(o, ",\"recent\":");
        qj(o, "SELECT i.id,i.no,i.date d,i.type,p.name party,i.total total_c,i.paid FROM invoice i JOIN party p ON p.id=i.party_id WHERE i.status='A' ORDER BY i.id DESC LIMIT 6");
        sba(o, "}");
    }
    /* ----- cari ----- */
    else if (A("parties")) {
        qj(o, "SELECT p.id,p.code,p.name,p.company,p.address,p.phone,p.email,p.note,p.opening opening_c,p.active," BAL " bal_c FROM party p WHERE p.kind=%Q ORDER BY p.name", P("k")[0] == 'S' ? "S" : "C");
    }
    else if (A("party_save")) {
        int id = atoi(P("id")); char kind[2] = {P("k")[0] == 'S' ? 'S' : 'C', 0};
        if (!*P("name")) { err(o, "Ad boş olamaz."); return; }
        if (id) {
            if (run("UPDATE party SET name=%Q,company=%Q,address=%Q,phone=%Q,email=%Q,note=%Q WHERE id=%d", P("name"), P("company"), P("address"), P("phone"), P("email"), P("note"), id)) { aud("CARI duzenlendi id=%d", id); okid(o, id); } else err(o, lasterr);
        } else {
            char code[16]; snprintf(code, 16, "%c%04d", kind[0] == 'C' ? 'M' : 'T', (int)val("SELECT COUNT(*) FROM party WHERE kind=%Q", kind) + 1);
            if (run("INSERT INTO party(kind,code,name,company,address,phone,email,note,opening) VALUES(%Q,%Q,%Q,%Q,%Q,%Q,%Q,%Q,%lld)", kind, code, P("name"), P("company"), P("address"), P("phone"), P("email"), P("note"), cents(P("opening")))) {
                int nid = (int)sqlite3_last_insert_rowid(db); aud("CARI olusturuldu %s %s", code, P("name")); okid(o, nid);
            } else err(o, lasterr);
        }
    }
    else if (A("party_toggle")) { int id = atoi(P("id")); run("UPDATE party SET active=1-active WHERE id=%d", id); aud("CARI aktif/pasif id=%d", id); okid(o, id); }
    else if (A("stmt")) {
        int id = atoi(P("id")); const char *f = *P("f") ? P("f") : "0000-01-01", *t = *P("t") ? P("t") : "9999-12-31";
        sba(o, "{\"rows\":");
        qj(o, "SELECT d,t,debit debit_c,credit credit_c,run run_c FROM (SELECT d,sid,t,debit,credit,SUM(debit-credit) OVER (ORDER BY d,sid) run FROM ("
              "SELECT '0000-01-01' d,0 sid,'Açılış bakiyesi' t,MAX(opening,0) debit,MAX(-opening,0) credit FROM party WHERE id=%d "
              "UNION ALL SELECT d,sid,t,debit,credit FROM led WHERE pid=%d)) WHERE d>=%Q AND d<=%Q ORDER BY d,sid", id, id, f, t);
        sba(o, ",\"info\":");
        qj(o, "SELECT p.name,p.code,p.company,p.address,p.kind,COALESCE((SELECT SUM(total) FROM invoice WHERE party_id=p.id AND status='A'),0) invoices_c,"
              "COALESCE((SELECT SUM(amount) FROM payment WHERE party_id=p.id AND status='A'),0) payments_c," BAL " bal_c FROM party p WHERE p.id=%d", id);
        sba(o, "}");
    }
    else if (A("pay_add")) {
        int id = atoi(P("id")); if (!val("SELECT COUNT(*) FROM party WHERE id=%d", id)) { err(o, "Cari bulunamadı."); return; }
        long long amt = cents(P("amount")), bal = VL("SELECT " BAL " FROM party p WHERE p.id=%d", id); const char *m = P("method")[0] == 'B' ? "B" : "N", *d = P("d");
        if (amt <= 0) { err(o, "Tutar geçersiz."); return; } if (!validdate(d, 1)) { err(o, "Tarih geçersiz veya dönem yılı dışında."); return; }
        if (!*P("force")) {
            if (amt > bal) { sba(o, "{\"warn\":\"Tutar mevcut bakiyeden büyük (fazlası avans olarak kalır). Devam edilsin mi?\"}"); return; }
            if (val("SELECT COUNT(*) FROM payment WHERE party_id=%d AND date=%Q AND amount=%lld AND method=%Q AND status='A'", id, d, amt, m)) { sba(o, "{\"warn\":\"Aynı gün, aynı tutar ve yöntemle bir kayıt zaten var (çift kayıt olabilir). Yine de kaydedilsin mi?\"}"); return; }
        }
        if (run("INSERT INTO payment(date,party_id,amount,method,note) VALUES(%Q,%d,%lld,%Q,%Q)", d, id, amt, m, P("note"))) { aud("ODEME kaydedildi cari=%d %lld kurus %s", id, amt, m); okid(o, id); } else err(o, lasterr);
    }
    else if (A("pay_list")) qj(o, "SELECT id,date d,amount amount_c,method,note,status FROM payment WHERE party_id=%d ORDER BY id DESC LIMIT 50", atoi(P("id")));
    else if (A("pay_cancel")) {
        int id = atoi(P("id")); if (!*P("reason")) { err(o, "İptal nedeni gerekli."); return; }
        if (!val("SELECT COUNT(*) FROM payment WHERE id=%d AND status='A'", id)) { err(o, "Aktif kayıt bulunamadı."); return; }
        if (run("UPDATE payment SET status='X' WHERE id=%d", id)) { aud("ODEME iptal id=%d neden=%s", id, P("reason")); okid(o, id); } else err(o, lasterr);
    }
    /* ----- ürün / stok ----- */
    else if (A("products")) qj(o, "SELECT p.id,p.code,p.name,p.category,p.unit,p.buy_price buy_c,p.sell_price sell_c,p.min_stock minq,p.vat,p.active," STK " stock FROM product p ORDER BY p.name");
    else if (A("product_save")) {
        int id = atoi(P("id")); if (!*P("name")) { err(o, "Ürün adı boş olamaz."); return; }
        if (id) {
            if (run("UPDATE product SET name=%Q,category=%Q,unit=%Q,buy_price=%lld,sell_price=%lld,min_stock=%f,vat=%f WHERE id=%d", P("name"), P("category"), P("unit"), cents(P("buy")), cents(P("sell")), pnum(P("minq")), *P("vat") ? pnum(P("vat")) : 7.0, id)) { aud("URUN duzenlendi id=%d", id); okid(o, id); } else err(o, lasterr);
        } else {
            char code[32]; snprintf(code, 32, "%s", P("code")); if (!*code) snprintf(code, 32, "U%04d", (int)val("SELECT COUNT(*) FROM product") + 1);
            if (val("SELECT COUNT(*) FROM product WHERE code=%Q", code)) { err(o, "Bu ürün kodu zaten var."); return; }
            long long oc = *P("ocost") ? cents(P("ocost")) : cents(P("buy"));
            if (run("INSERT INTO product(code,name,category,unit,buy_price,sell_price,min_stock,vat,opening_qty,opening_cost) VALUES(%Q,%Q,%Q,%Q,%lld,%lld,%f,%f,%f,%lld)", code, P("name"), P("category"), *P("unit") ? P("unit") : "adet", cents(P("buy")), cents(P("sell")), pnum(P("minq")), *P("vat") ? pnum(P("vat")) : 7.0, pnum(P("oqty")), oc)) {
                int nid = (int)sqlite3_last_insert_rowid(db); aud("URUN olusturuldu %s %s", code, P("name")); okid(o, nid);
            } else err(o, lasterr);
        }
    }
    else if (A("product_toggle")) { int id = atoi(P("id")); run("UPDATE product SET active=1-active WHERE id=%d", id); aud("URUN aktif/pasif id=%d", id); okid(o, id); }
    else if (A("prod_hist")) {
        int id = atoi(P("id")); sba(o, "{\"p\":"); qj(o, "SELECT name,unit,opening_qty FROM product WHERE id=%d", id); sba(o, ",\"rows\":");
        qj(o, "SELECT h.d,h.ty,h.no,COALESCE(pa.name,'') party,h.q,h.pr pr_c,h.run FROM (SELECT d,iid,ty,no,party,q,pr,(SELECT opening_qty FROM product WHERE id=%d)+SUM(q) OVER (ORDER BY d,iid) run FROM mv WHERE pid=%d) h LEFT JOIN party pa ON pa.id=h.party ORDER BY h.d,h.iid", id, id);
        sba(o, "}");
    }
    else if (A("stock_asof")) {
        if (!validdate(P("d"), 0)) { err(o, "Tarih geçersiz."); return; }
        qj(o, "SELECT p.name,p.unit,p.opening_qty+COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id AND d<=%Q),0) stock FROM product p ORDER BY p.name", P("d"));
    }
    else if (A("stock_period")) {
        setrange(P("f"), P("t"));
        qj(o, "SELECT p.name,p.unit,p.opening_qty+COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id AND d<" RF "),0) start_q,"
              "COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id AND ty='P' AND d BETWEEN " RF " AND " RT "),0) bought,"
              "-COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id AND ty='S' AND d BETWEEN " RF " AND " RT "),0) sold,"
              "p.opening_qty+COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id AND d<=" RT "),0) end_q FROM product p ORDER BY p.name");
    }
    /* ----- fatura ----- */
    else if (A("nextno")) { char no[40]; if (!validdate(P("d"), 1)) { err(o, "Tarih geçersiz."); return; } nextno(P("d"), no); sba(o, "{\"no\":"); sbj(o, no); sba(o, "}"); }
    else if (A("inv_list")) {
        const char *ty = P("type"); int st = atoi(P("st")); if (st < 1 || st > 4) st = 1; const char *f = *P("f") ? P("f") : "0000-01-01", *t = *P("t") ? P("t") : "9999-12-31", *q = P("q");
        qj(o, "SELECT i.id,i.type,i.no,i.date d,p.name party,i.total total_c,i.paid,i.method,i.status FROM invoice i JOIN party p ON p.id=i.party_id WHERE (i.type=%Q OR %Q='*') AND i.date BETWEEN %Q AND %Q AND "
              "(%d=1 OR (%d=2 AND i.paid=1 AND i.status='A') OR (%d=3 AND i.paid=0 AND i.status='A') OR (%d=4 AND i.status='X')) AND "
              "(%Q='' OR i.no LIKE '%%'||%Q||'%%' OR p.name LIKE '%%'||%Q||'%%' OR i.note LIKE '%%'||%Q||'%%' OR EXISTS(SELECT 1 FROM line l JOIN product r ON r.id=l.product_id WHERE l.inv_id=i.id AND r.name LIKE '%%'||%Q||'%%')) ORDER BY i.date DESC,i.id DESC",
           ty, ty, f, t, st, st, st, st, q, q, q, q, q);
    }
    else if (A("inv_detail")) {
        int id = atoi(P("id")); sba(o, "{\"h\":"); qj(o, "SELECT i.id,i.type,i.no,i.date d,p.name party,p.company,p.address,i.total total_c,i.paid,i.method,i.note,i.status,i.cancel_note FROM invoice i JOIN party p ON p.id=i.party_id WHERE i.id=%d", id);
        sba(o, ",\"l\":"); qj(o, "SELECT r.name,r.unit,l.qty,l.price price_c,l.total total_c,l.vat FROM line l JOIN product r ON r.id=l.product_id WHERE l.inv_id=%d ORDER BY l.id", id); sba(o, "}");
    }
    else if (A("inv_save")) {
        char type = P("type")[0] == 'P' ? 'P' : 'S', k[2] = {type, 0}; const char *d = P("d");
        if (!validdate(d, 1)) { err(o, "Tarih geçersiz veya dönem yılı dışında."); return; }
        int pid = atoi(P("pid")); if (!partyok(pid, type == 'S' ? "C" : "S")) { err(o, type == 'S' ? "Müşteri seçin." : "Tedarikçi seçin."); return; }
        char no[40]; snprintf(no, 40, "%s", P("no"));
        if (!*no) { if (type == 'P') { err(o, "Alış fatura numarası zorunlu."); return; } nextno(d, no); }
        if (val("SELECT COUNT(*) FROM invoice WHERE type=%Q AND no=%Q AND (type='S' OR party_id=%d)", k, no, pid)) { err(o, "Bu fatura numarası zaten kullanılmış (iptal edilenler dahil)."); return; }
        static L ln[200]; int nl = 0;
        for (int i = 0; i < np && nl < 200; i++) if (!strcmp(K[i], "lp")) {
            L *l = &ln[nl]; l->pid = atoi(V[i]); l->qty = 0; l->price = 0;
            for (int j = i + 1; j < np && strcmp(K[j], "lp"); j++) { if (!strcmp(K[j], "lq")) l->qty = pnum(V[j]); if (!strcmp(K[j], "lx")) l->price = cents(V[j]); }
            nl++;
        }
        if (!nl) { err(o, "En az bir ürün satırı ekleyin."); return; }
        char warn[400] = ""; long long tot = 0;
        for (int i = 0; i < nl; i++) {
            L *l = &ln[i];
            if (!val("SELECT COUNT(*) FROM product WHERE id=%d AND active=1", l->pid)) { err(o, "Satırlarda geçersiz ürün var."); return; }
            if (l->qty <= 0) { err(o, "Miktar sıfırdan büyük olmalı."); return; } if (l->price <= 0) { err(o, "Birim fiyat sıfırdan büyük olmalı."); return; }
            l->total = llround(l->qty * l->price); l->vat = val("SELECT vat FROM product WHERE id=%d", l->pid); snprintf(l->name, 100, "%s", txt("SELECT name FROM product WHERE id=%d", l->pid)); tot += l->total;
            if (type == 'S' && !*warn) {
                double cur = val("SELECT " STK " FROM product p WHERE p.id=%d", l->pid), need = 0; for (int j = 0; j < nl; j++) if (ln[j].pid == l->pid) need += ln[j].qty;
                if (need > cur) snprintf(warn, 400, "Stok yetersiz: %s (mevcut %s, satış %s). Stok negatife düşecek.", l->name, Q(cur), Q(need));
            }
        }
        if (*warn) { if (!setn("allow_neg")) { char m[450]; snprintf(m, 450, "%s Negatif stoğa satış ayarlardan kapalı.", warn); err(o, m); return; }
            if (!*P("force")) { sba(o, "{\"warn\":"); sbj(o, warn); sba(o, "}"); return; } }
        int paid = P("paid")[0] == '1'; const char *m = paid ? (P("method")[0] == 'B' ? "B" : "N") : "";
        if (!run("BEGIN")) { err(o, lasterr); return; }
        int ok = run("INSERT INTO invoice(type,no,date,party_id,method,paid,total,note) VALUES(%Q,%Q,%Q,%d,%Q,%d,%lld,%Q)", k, no, d, pid, m, paid, tot, P("note")); int iid = (int)sqlite3_last_insert_rowid(db);
        for (int i = 0; ok && i < nl; i++) { ok = run("INSERT INTO line(inv_id,product_id,qty,price,total,vat) VALUES(%d,%d,%f,%lld,%lld,%f)", iid, ln[i].pid, ln[i].qty, ln[i].price, ln[i].total, ln[i].vat);
            if (ok && type == 'P') run("UPDATE product SET buy_price=%lld WHERE id=%d", ln[i].price, ln[i].pid); }
        if (ok) { run("COMMIT"); aud("FATURA kaydedildi %s %s %lld kurus", k, no, tot); sbf(o, "{\"ok\":1,\"id\":%d,\"no\":", iid); sbj(o, no); sba(o, "}"); }
        else { char e[300]; snprintf(e, 300, "%s", lasterr); run("ROLLBACK"); err(o, e); }
    }
    else if (A("inv_cancel")) {
        int id = atoi(P("id")); if (!*P("reason")) { err(o, "İptal nedeni gerekli."); return; }
        if (!val("SELECT COUNT(*) FROM invoice WHERE id=%d AND status='A'", id)) { err(o, "Aktif fatura bulunamadı."); return; }
        if (run("UPDATE invoice SET status='X',cancel_note=%Q WHERE id=%d", P("reason"), id)) { aud("FATURA iptal id=%d no=%s neden=%s", id, txt("SELECT no FROM invoice WHERE id=%d", id), P("reason")); okid(o, id); } else err(o, lasterr);
    }
    else if (A("inv_print")) {
        int id = atoi(P("id")); if (!val("SELECT COUNT(*) FROM invoice WHERE id=%d AND type='S'", id)) { err(o, "Satış faturası bulunamadı."); return; }
        lastfile[0] = 0; print_invoice(id); if (!*lastfile) { err(o, "Fatura dosyası oluşturulamadı."); return; }
        sba(o, "{\"file\":"); sbj(o, lastfile); sbf(o, ",\"missing\":%d}", !*txt("SELECT v FROM settings WHERE k='co_name'") || !*txt("SELECT v FROM settings WHERE k='co_addr'") || !*txt("SELECT v FROM settings WHERE k='co_tax'"));
    }
    /* ----- kasa / banka / gider ----- */
    else if (A("cash")) {
        char m = P("m")[0] == 'B' ? 'B' : 'N'; char k[2] = {m, 0}; sbf(o, "{\"bal_c\":%lld,\"rows\":", cashbal(m));
        qj(o, "SELECT d,t,CASE WHEN a>0 THEN a ELSE 0 END in_c,CASE WHEN a<0 THEN -a ELSE 0 END out_c,run run_c FROM (SELECT d,t,a,%lld+SUM(a) OVER (ORDER BY d,t,a) run FROM fin WHERE m=%Q) ORDER BY d DESC,t DESC,a DESC", setn(m == 'N' ? "cash_open" : "bank_open"), k);
        sba(o, "}");
    }
    else if (A("exp_list")) { sba(o, "{\"rows\":"); qj(o, "SELECT id,date d,category cat,descr,amount amount_c,method,status FROM expense ORDER BY date DESC,id DESC"); sba(o, ",\"sums\":"); qj(o, "SELECT category cat,COUNT(*) n,SUM(amount) total_c FROM expense WHERE status='A' GROUP BY category ORDER BY 3 DESC"); sba(o, "}"); }
    else if (A("exp_save")) {
        long long amt = cents(P("amount")); if (!*P("cat")) { err(o, "Kategori seçin."); return; } if (amt <= 0) { err(o, "Tutar geçersiz."); return; } if (!validdate(P("d"), 1)) { err(o, "Tarih geçersiz veya dönem yılı dışında."); return; }
        if (run("INSERT INTO expense(date,category,descr,amount,method) VALUES(%Q,%Q,%Q,%lld,%Q)", P("d"), P("cat"), P("descr"), amt, P("method")[0] == 'B' ? "B" : "N")) { aud("GIDER eklendi %s %lld kurus", P("cat"), amt); okid(o, 0); } else err(o, lasterr);
    }
    else if (A("exp_cancel")) {
        int id = atoi(P("id")); if (!*P("reason")) { err(o, "İptal nedeni gerekli."); return; }
        if (!val("SELECT COUNT(*) FROM expense WHERE id=%d AND status='A'", id)) { err(o, "Aktif gider bulunamadı."); return; }
        if (run("UPDATE expense SET status='X' WHERE id=%d", id)) { aud("GIDER iptal id=%d neden=%s", id, P("reason")); okid(o, id); } else err(o, lasterr);
    }
    /* ----- raporlar ----- */
    else if (A("rep_summary")) {
        const char *f = P("f"), *t = P("t"); if (!validdate(f, 0) || !validdate(t, 0)) { err(o, "Tarih aralığı geçersiz."); return; }
        setrange(f, t); double rev, cogs, sv; replay(f, t, &rev, &cogs, &sv, NULL);
        long long sales = VL("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='S' AND status='A' AND date BETWEEN %Q AND %Q", f, t);
        long long cashsale = VL("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='S' AND status='A' AND paid=1 AND date BETWEEN %Q AND %Q", f, t);
        long long buy = VL("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='P' AND status='A' AND date BETWEEN %Q AND %Q", f, t);
        long long exp = VL("SELECT COALESCE(SUM(amount),0) FROM expense WHERE status='A' AND date BETWEEN %Q AND %Q", f, t);
        long long col = VL("SELECT COALESCE(SUM(x.amount),0) FROM payment x JOIN party r ON r.id=x.party_id WHERE r.kind='C' AND x.status='A' AND x.date BETWEEN %Q AND %Q", f, t);
        long long sp = VL("SELECT COALESCE(SUM(x.amount),0) FROM payment x JOIN party r ON r.id=x.party_id WHERE r.kind='S' AND x.status='A' AND x.date BETWEEN %Q AND %Q", f, t);
        long long r = llround(rev), cg = llround(cogs);
        sbf(o, "{\"sales_c\":%lld,\"cashsale_c\":%lld,\"credit_c\":%lld,\"coll_c\":%lld,\"buy_c\":%lld,\"supp_c\":%lld,\"exp_c\":%lld,\"net_c\":%lld,\"cogs_c\":%lld,\"gross_c\":%lld,\"result_c\":%lld,\"stockval_c\":%lld,\"recv_c\":%lld,\"debt_c\":%lld,\"cash_c\":%lld,\"bank_c\":%lld}",
            sales, cashsale, sales - cashsale, col, buy, sp, exp, r, cg, r - cg, r - cg - exp, llround(sv), totbal('C'), totbal('S'), cashbal('N'), cashbal('B'));
    }
    else if (A("rep_prod")) {
        setrange(P("f"), P("t"));
        qj(o, "SELECT p.name,p.unit,COALESCE(SUM(CASE WHEN m.ty='S' THEN -m.q END),0) sold,COALESCE(SUM(CASE WHEN m.ty='S' THEN m.tot END),0) sales_c,COALESCE(SUM(CASE WHEN m.ty='P' THEN m.q END),0) bought,COALESCE(SUM(CASE WHEN m.ty='P' THEN m.tot END),0) buy_c "
              "FROM product p LEFT JOIN mv m ON m.pid=p.id AND m.d BETWEEN " RF " AND " RT " GROUP BY p.id HAVING sold>0 OR bought>0 ORDER BY sales_c DESC");
    }
    else if (A("rep_cust"))
        qj(o, "SELECT p.code,p.name,COUNT(i.id) n,SUM(i.total) sales_c,SUM(CASE WHEN i.paid=1 THEN i.total ELSE 0 END) cash_c FROM party p JOIN invoice i ON i.party_id=p.id AND i.type='S' AND i.status='A' AND i.date BETWEEN %Q AND %Q WHERE p.kind='C' GROUP BY p.id ORDER BY 4 DESC", P("f"), P("t"));
    else if (A("rep_dist")) {
        int g = atoi(P("g")); const char *fm = g == 3 ? "%Y-%m" : g == 2 ? "%Y-W%W" : "%Y-%m-%d";
        qj(o, "SELECT strftime(%Q,date) per,COUNT(*) n,SUM(total) sales_c FROM invoice WHERE type='S' AND status='A' AND date BETWEEN %Q AND %Q GROUP BY 1 ORDER BY 1", fm, P("f"), P("t"));
    }
    else if (A("rep_years")) {
        int ys[64], n = scan_years(ys); sqlite3 *keep = db; sba(o, "["); int c = 0;
        for (int i = 0; i < n; i++) {
            char p[40]; snprintf(p, 40, "mixpack_%d.db", ys[i]); if (sqlite3_open_v2(p, &db, SQLITE_OPEN_READONLY, 0)) { db = keep; continue; }
            sbf(o, "%s{\"year\":%d,\"sales_c\":%lld,\"buy_c\":%lld,\"exp_c\":%lld,\"coll_c\":%lld,\"n\":%d}", c++ ? "," : "", ys[i],
                VL("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='S' AND status='A'"), VL("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='P' AND status='A'"),
                VL("SELECT COALESCE(SUM(amount),0) FROM expense WHERE status='A'"), VL("SELECT COALESCE(SUM(x.amount),0) FROM payment x JOIN party r ON r.id=x.party_id WHERE r.kind='C' AND x.status='A'"),
                (int)val("SELECT COUNT(*) FROM invoice WHERE status='A'"));
            sqlite3_close(db); db = keep;
        }
        sba(o, "]");
    }
    else if (A("audit")) qj(o, "SELECT id,ts,what FROM audit ORDER BY id DESC LIMIT 200");
    /* ----- ayarlar ----- */
    else if (A("settings_get")) qj(o, "SELECT k,v FROM settings");
    else if (A("settings_set")) {
        static const char *ok_keys[] = {"co_name", "co_addr", "co_tax", "co_vat", "co_phone", "co_email", "co_iban", "allow_neg", NULL};
        for (int i = 0; i < np; i++) {
            for (int j = 0; ok_keys[j]; j++) if (!strcmp(K[i], ok_keys[j])) sets(K[i], V[i]);
            if (!strcmp(K[i], "cash_open") || !strcmp(K[i], "bank_open")) { char b[32]; snprintf(b, 32, "%lld", cents(V[i])); sets(K[i], b); }
        }
        aud("AYARLAR guncellendi"); okid(o, 0);
    }
    else if (A("backup")) {
        char dir[200], p[420]; snprintf(dir, 200, "%s", *P("dir") ? P("dir") : "yedekler"); MKDIR(dir);
        snprintf(p, 420, "%s/mixpack_%d_%s.db", dir, year, txt("SELECT strftime('%%Y%%m%%d_%%H%%M%%S','now','localtime')"));
        if (run("VACUUM INTO %Q", p)) { sets("last_backup", today()); sba(o, "{\"ok\":1,\"path\":"); sbj(o, p); sba(o, "}"); } else err(o, "Yedek alınamadı (klasörü/diski kontrol edin).");
    }
    else if (A("restore")) {
        const char *p = P("path"); sqlite3 *t; int ok = sqlite3_open_v2(p, &t, SQLITE_OPEN_READONLY, 0) == 0;
        if (ok) { sqlite3 *k = db; db = t; ok = *txt("SELECT name FROM sqlite_master WHERE name='invoice'") != 0; db = k; sqlite3_close(t); }
        if (!ok) { err(o, "Geçerli bir MixPack yedeği değil."); return; }
        sqlite3_close(db); db = NULL; char bk[320]; snprintf(bk, 320, "%s.oncesi", dbpath); cpfile(dbpath, bk);
        int c = cpfile(p, dbpath); open_year(year); if (c) okid(o, 0); else err(o, "Kopyalama başarısız.");
    }
    else if (A("newyear")) {
        int y = atoi(P("y")); char p[40]; snprintf(p, 40, "mixpack_%d.db", y); FILE *t = fopen(p, "rb");
        if (y < 2000 || y > 2100) { err(o, "Yıl geçersiz."); return; } if (t) { fclose(t); err(o, "Bu yıl zaten var."); return; }
        nyopt[0] = P("cc")[0] == '1'; nyopt[1] = P("dbt")[0] == '1'; nyopt[2] = P("pc")[0] == '1'; nyopt[3] = P("st")[0] == '1'; new_year(y); okid(o, y);
    }
    else if (A("quit")) { quitflag = 1; okid(o, 0); }
    else err(o, "Bilinmeyen işlem.");
}

/* ---------- CSV dışa aktarma ---------- */
static void csv_export(SB *o, int c, const char *f, const char *t) {
    char *q = NULL;
    if (c == 1 || c == 2) q = sqlite3_mprintf("SELECT i.no No," DF("i.date") " Tarih,p.name Cari,i.total Tutar_c,CASE i.paid WHEN 1 THEN 'ÖDENDİ' ELSE 'ÖDENMEDİ' END Durum,CASE i.status WHEN 'X' THEN 'İPTAL' ELSE '' END İptal FROM invoice i JOIN party p ON p.id=i.party_id WHERE i.type='%c' ORDER BY i.date,i.id", c == 1 ? 'S' : 'P');
    else if (c == 3 || c == 4) q = sqlite3_mprintf("SELECT p.code Kod,p.name Ad,p.company Firma," BAL " Bakiye_c FROM party p WHERE p.kind='%c' ORDER BY p.name", c == 3 ? 'C' : 'S');
    if (q) { qcsv(o, q); sqlite3_free(q); }
    else if (c == 5) qcsv(o, "SELECT p.code Kod,p.name Ürün,p.unit Birim," STK " Stok,p.min_stock Min FROM product p ORDER BY p.name");
    else if (c == 6) { setrange(f, t); qcsv(o, "SELECT p.name Ürün,p.unit Birim,COALESCE(SUM(CASE WHEN m.ty='S' THEN -m.q END),0) Satılan,COALESCE(SUM(CASE WHEN m.ty='S' THEN m.tot END),0) Satış_c,COALESCE(SUM(CASE WHEN m.ty='P' THEN m.q END),0) Alınan,COALESCE(SUM(CASE WHEN m.ty='P' THEN m.tot END),0) Alış_c FROM product p LEFT JOIN mv m ON m.pid=p.id AND m.d BETWEEN " RF " AND " RT " GROUP BY p.id HAVING Satılan>0 OR Alınan>0 ORDER BY Satış_c DESC"); }
    else if (c == 7) qcsv(o, "SELECT " DF("date") " Tarih,category Kategori,descr Açıklama,amount Tutar_c,CASE method WHEN 'B' THEN 'Banka' ELSE 'Nakit' END Ödeme FROM expense WHERE status='A' ORDER BY date");
    else qcsv(o, "SELECT " DF("d") " Tarih,CASE m WHEN 'B' THEN 'Banka' ELSE 'Kasa' END Hesap,t Açıklama,a Tutar_c FROM fin ORDER BY d,t");
}

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

int main(int argc, char **argv) {
#ifdef _WIN32
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); SetConsoleOutputCP(65001);
#endif
    gui = 1; int ys[64], n = scan_years(ys), y; time_t tt = time(0);
    if (argc > 1 && atoi(argv[1]) >= 2000) y = atoi(argv[1]); else if (n) y = ys[n - 1]; else y = localtime(&tt)->tm_year + 1900;
    open_year(y);
    srand((unsigned)tt ^ (unsigned)clock() ^ 0x9e3779b9u); snprintf(token, 40, "%08x%08x%08x", (unsigned)rand() * 2654435761u, (unsigned)rand(), (unsigned)rand());
    sock_t ls = socket(AF_INET, SOCK_STREAM, 0); 
#ifndef _WIN32
    int one = 1; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one); /* Windows'ta açılmaz: port çalma riski */
#endif
   
    struct sockaddr_in ad; memset(&ad, 0, sizeof ad); ad.sin_family = AF_INET; ad.sin_addr.s_addr = htonl(INADDR_LOOPBACK); int port = 8765;
    for (; port < 8790; port++) { ad.sin_port = htons((unsigned short)port); if (!bind(ls, (struct sockaddr *)&ad, sizeof ad)) break; }
    if (port >= 8790 || listen(ls, 16)) { puts("Port açılamadı."); return 1; }
    printf("MixPack Muhasebe %d\nTarayıcıda açılan pencere programın arayüzüdür: http://127.0.0.1:%d/\nBu siyah pencereyi KAPATMAYIN (programı arayüzdeki 'Kapat' ile kapatın).\n", year, port);
    if (!getenv("MIXPACK_NOBROWSER")) { char cmd[120]; snprintf(cmd, 120, OPEN_CMD "\"http://127.0.0.1:%d/\"" QUIET, port); if (system(cmd)) puts("Tarayıcı otomatik açılamadı; adresi elle açın."); }
    while (!quitflag) {
        sock_t c = accept(ls, 0, 0);
#ifdef _WIN32
        if (c == INVALID_SOCKET) continue;
#else
        if (c < 0) continue;
#endif
#ifdef _WIN32
        DWORD to = 1500; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof to);
#else
        struct timeval to = {1, 500000}; setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&to, sizeof to);
#endif
        handle(c); CLOSESOCK(c);
    }
    CLOSESOCK(ls); sqlite3_close(db); return 0;
}
