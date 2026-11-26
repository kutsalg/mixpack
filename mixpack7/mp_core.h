/* mp_core.h - yerel (masaüstü) arayüzün platformdan BAĞIMSIZ çekirdeği.
 * Sayfalar, formlar, fatura/tahsilat/rapor akışları burada; pencere çizimi ise araç takımında (tk_*):
 * Windows için mp_tk_win.h, Linux testi için mp_tk_fake.h. Tüm yazma işlemleri mp_api.h'deki doğrulanmış api() üzerinden yapılır. */
#ifndef MP_CORE_H
#define MP_CORE_H
#define MP_CORE_VERSION 6

enum { F_TEXT, F_SELECT, F_CHECK, F_DATE, F_NOTE };
#define MAXF 14
#define MAXO 400
typedef struct { char key[20], label[160]; int type, nopt; char val[300]; char oid[MAXO][20]; char *olab[MAXO]; } Field;
typedef struct { char title[120]; int n; Field f[MAXF]; } Form;
typedef struct { int nc, nr; char **h; int *right; char ***c; int *id; } Table;

/* araç takımının sağlaması gerekenler */
static void tk_msg(const char *title, const char *text);
static int tk_confirm(const char *title, const char *text);
static int tk_form(Form *f);                                                   /* 1 = Tamam */
static int tk_table(const char *title, Table *t, const char *foot, int printable); /* 1 = yazdır istendi */
static void tk_open(const char *path);

/* ---------- tablo ---------- */
static void table_free(Table *t) {
    for (int r = 0; r < t->nr; r++) { for (int i = 0; i < t->nc; i++) free(t->c[r][i]); free(t->c[r]); }
    for (int i = 0; i < t->nc; i++) free(t->h[i]);
    free(t->c); free(t->h); free(t->right); free(t->id); memset(t, 0, sizeof *t);
}
static void table_head(Table *t, int nc, const char **h, const int *right) {
    t->nc = nc; t->h = calloc(nc, sizeof(char *)); t->right = calloc(nc, sizeof(int));
    for (int i = 0; i < nc; i++) { t->h[i] = strdup(h[i]); t->right[i] = right ? right[i] : 0; }
}
static void table_row(Table *t, int id, const char **cells) {
    t->c = realloc(t->c, sizeof(char **) * (t->nr + 1)); t->id = realloc(t->id, sizeof(int) * (t->nr + 1));
    char **r = calloc(t->nc, sizeof(char *)); for (int i = 0; i < t->nc; i++) r[i] = strdup(cells[i]);
    t->c[t->nr] = r; t->id[t->nr] = id; t->nr++;
}
/* ilk kolonun adı "id" ise gizli satır numarasıdır; "_c" ile biten kolonlar para (kuruş) olarak biçimlenir */
static void table_query(Table *t, const char *sql) {
    table_free(t); sqlite3_stmt *s;
    if (sqlite3_prepare_v2(db, sql, -1, &s, 0)) { snprintf(lasterr, sizeof lasterr, "%s", sqlite3_errmsg(db)); return; }
    int n = sqlite3_column_count(s), hid = n > 0 && !strcmp(sqlite3_column_name(s, 0), "id"), nc = n - hid;
    if (nc < 0) nc = 0;
    const char *hh[40]; int rr[40] = {0}, mon[40] = {0}; static char hn[40][100];
    if (nc > 40) nc = 40;
    for (int i = 0; i < nc; i++) { snprintf(hn[i], 100, "%s", sqlite3_column_name(s, i + hid)); size_t l = strlen(hn[i]); if (l > 2 && !strcmp(hn[i] + l - 2, "_c")) { mon[i] = 1; rr[i] = 1; hn[i][l - 2] = 0; } hh[i] = hn[i]; }
    table_head(t, nc, hh, rr);
    while (sqlite3_step(s) == SQLITE_ROW) {
        static char cb[40][300]; const char *cp[40];
        for (int i = 0; i < nc; i++) {
            int ty = sqlite3_column_type(s, i + hid); cb[i][0] = 0;
            if (ty == SQLITE_NULL) {}
            else if (mon[i]) snprintf(cb[i], 300, "%s", M(llround(sqlite3_column_double(s, i + hid))));
            else if (ty == SQLITE_INTEGER) { t->right[i] = 1; snprintf(cb[i], 300, "%lld", sqlite3_column_int64(s, i + hid)); }
            else if (ty == SQLITE_FLOAT) { t->right[i] = 1; snprintf(cb[i], 300, "%s", Q(sqlite3_column_double(s, i + hid))); }
            else snprintf(cb[i], 300, "%s", (const char *)sqlite3_column_text(s, i + hid));
            cp[i] = cb[i];
        }
        table_row(t, hid ? sqlite3_column_int(s, 0) : 0, cp);
    }
    sqlite3_finalize(s);
}
static void table_html(const char *path, const char *title, Table *t, const char *foot) {
    FILE *f = fopen(path, "w"); if (!f) return;
    fputs("<!DOCTYPE html><html lang=\"tr\"><head><meta charset=\"utf-8\"><title>", f); hesc(f, title);
    fputs("</title><style>@page{size:A4;margin:15mm}body{font:12px Arial,sans-serif;max-width:185mm;margin:auto}table{width:100%;border-collapse:collapse}th,td{padding:5px;border-bottom:1px solid #ccc;text-align:left}.r{text-align:right}@media print{.np{display:none}}</style></head><body><div class=\"np\" style=\"text-align:right\"><button style=\"font-size:16px;padding:8px 18px\" onclick=\"print()\">Yazdır / PDF olarak kaydet</button></div><h2>", f);
    hesc(f, title); fputs("</h2><table><tr>", f);
    for (int i = 0; i < t->nc; i++) { fprintf(f, "<th class=\"%s\">", t->right[i] ? "r" : ""); hesc(f, t->h[i]); fputs("</th>", f); }
    fputs("</tr>", f);
    for (int r = 0; r < t->nr; r++) { fputs("<tr>", f); for (int i = 0; i < t->nc; i++) { fprintf(f, "<td class=\"%s\">", t->right[i] ? "r" : ""); hesc(f, t->c[r][i]); fputs("</td>", f); } fputs("</tr>", f); }
    fputs("</table><p><b>", f); hesc(f, foot ? foot : ""); fputs("</b></p></body></html>", f); fclose(f);
}

/* ---------- görünüm ---------- */
#define ACTIONS \
 X(A_NEWSALE) X(A_NEWPURCH) X(A_PAYQUICK) X(A_GOEXP) X(A_INV_NEW) X(A_INV_DETAIL) X(A_INV_PRINT) X(A_INV_CANCEL) X(A_INV_FILTER) X(A_CSV) \
 X(A_PAR_NEW) X(A_PAR_EDIT) X(A_PAR_STMT) X(A_PAR_PAY) X(A_PAR_PAYCANCEL) X(A_PAR_TOGGLE) X(A_PAR_DEBT) \
 X(A_PRD_NEW) X(A_PRD_EDIT) X(A_PRD_HIST) X(A_PRD_TOGGLE) X(A_PRD_ASOF) X(A_PRD_PERIOD) \
 X(A_EXP_ADD) X(A_EXP_CANCEL) X(A_EXP_SUMS) \
 X(A_REP_SUM) X(A_REP_PROD) X(A_REP_CUST) X(A_REP_DIST) X(A_REP_YEARS) X(A_REP_AUDIT) X(A_REP_PERIOD) \
 X(A_SET_FIRM) X(A_SET_OPEN) X(A_SET_NEG) X(A_SET_BACKUP) X(A_SET_RESTORE) X(A_SET_NEWYEAR) X(A_SET_YEAR)
enum {
#define X(n) n,
    ACTIONS
#undef X
    A_COUNT
};
static const char *ACTNAME[] = {
#define X(n) #n,
    ACTIONS
#undef X
    NULL
};
typedef struct { char title[120]; char *head; int nb; char btn[16][60]; int bid[16], needrow[16], style[16]; Table t; int dflt; int nk; char klab[8][60], kval[8][60]; int kstyle[8]; } View;
static View VW;
static const char *PDESC[] = {
    "Günün özeti: satış, tahsilat, borç/alacak ve kritik stoklar. Hızlı işlem için üstteki düğmeleri kullanın.",
    "Satış faturalarını oluşturun, yazdırın veya iptal edin. Bir satıra çift tıklayınca fatura satırları açılır.",
    "Mal alış faturalarını girin; stok ve tedarikçi borcu otomatik güncellenir.",
    "Müşteri kartları, hesap ekstresi ve tahsilatlar. Borçlular kırmızı ile gösterilir.",
    "Tedarikçi kartları, borcunuz ve yaptığınız ödemeler.",
    "Ürün kartları ve anlık stok. Kritik ve biten ürünler renkle işaretlenir.",
    "Nakit kasa hareketleri: peşin satışlar, tahsilatlar, ödemeler ve giderler.",
    "Banka hareketleri ve bakiye (yalnızca işletme içi takip).",
    "Mazot, kira, telefon ve diğer işletme giderleri.",
    "Satış, kâr, ürün ve müşteri raporları. Dönem seçin, Excel'e aktarın.",
    "Firma bilgileri (faturada görünür), yedekleme ve yeni yıl işlemleri."};
static const char *PAGE[] = {"Ana ekran", "Satış faturaları", "Alış faturaları", "Müşteriler", "Tedarikçiler", "Stoklar", "Kasa (nakit)", "Banka", "Giderler", "Raporlar", "Ayarlar"};
#define NPAGES 11
static int cur_page, inv_st = 1, par_debt, rep_kind, rep_group = 3;
static char inv_f[11] = "", inv_t[11] = "", inv_q[100] = "", rep_f[11], rep_t[11];

/* ---------- küçük yardımcılar ---------- */
static void iso2disp(const char *iso, char *o) { if (strlen(iso) == 10) snprintf(o, 11, "%.2s.%.2s.%.4s", iso + 8, iso + 5, iso); else o[0] = 0; }
static int in2iso(const char *in, char *o, int strict) {
    int d, m, y; if (sscanf(in, "%d.%d.%d", &d, &m, &y) == 3) { if (y < 100) y += 2000; } else if (sscanf(in, "%d-%d-%d", &y, &m, &d) != 3) return 0;
    char iso[16]; snprintf(iso, 16, "%04d-%02d-%02d", y, m, d); if (strcmp(txt("SELECT date(%Q)", iso), iso)) return 0;
    if (strict && y != year) return 0; snprintf(o, 11, "%s", iso); return 1;
}
static void defdate_disp(char *o) { const char *t = today(); char iso[11]; if (atoi(t) == year) snprintf(iso, 11, "%s", t); else snprintf(iso, 11, "%d-01-01", year); iso2disp(iso, o); }
static void eurin(long long c, char *o) { snprintf(o, 32, "%s%lld,%02lld", c < 0 ? "-" : "", llabs(c) / 100, llabs(c) % 100); }
static void reset_period(void) { snprintf(rep_f, 11, "%d-01-01", year); snprintf(rep_t, 11, "%d-12-31", year); }
static Form *form_new(const char *title) { Form *f = calloc(1, sizeof *f); snprintf(f->title, 120, "%s", title); return f; }
static Field *fadd(Form *f, const char *key, const char *label, int type, const char *def) {
    Field *d = &f->f[f->n++]; snprintf(d->key, 20, "%s", key); snprintf(d->label, 160, "%s", label); d->type = type; snprintf(d->val, 300, "%s", def ? def : ""); return d;
}
static void fopt(Field *d, const char *id, const char *label) { if (d->nopt >= MAXO) return; snprintf(d->oid[d->nopt], 20, "%s", id); d->olab[d->nopt] = strdup(label); d->nopt++; }
static void form_free(Form *f) { for (int i = 0; i < f->n; i++) for (int k = 0; k < f->f[i].nopt; k++) free(f->f[i].olab[k]); free(f); }
static const char *fv(Form *f, const char *key) { for (int i = 0; i < f->n; i++) if (!strcmp(f->f[i].key, key)) return f->f[i].val; return ""; }
static int run_form(Form *f, int strict) {
    for (;;) {
        if (!tk_form(f)) return 0;
        int bad = 0;
        for (int i = 0; i < f->n && !bad; i++) if (f->f[i].type == F_DATE) {
            char iso[11], m[300];
            if (!*f->f[i].val) { if (strict) { snprintf(m, 300, "“%s” boş olamaz (gg.aa.yyyy).", f->f[i].label); tk_msg("Hata", m); bad = 1; } }
            else if (!in2iso(f->f[i].val, iso, strict)) { snprintf(m, 300, "“%s” için geçerli bir tarih girin (gg.aa.yyyy)%s.", f->f[i].label, strict ? " ve dönem yılı içinde olsun" : ""); tk_msg("Hata", m); bad = 1; }
        }
        if (!bad) return 1;
    }
}
static void urlenc(SB *o, const char *s) { char b[4]; for (; *s; s++) { unsigned char c = (unsigned char)*s; if (isalnum(c) || c == '-' || c == '_' || c == '.') { b[0] = (char)c; b[1] = 0; sba(o, b); } else { snprintf(b, 4, "%%%02X", c); sba(o, b); } } }
static void pp(SB *o, const char *k, const char *v) { sba(o, "&"); sba(o, k); sba(o, "="); urlenc(o, v); }
static char *form_params(Form *f, const char *a, const char *extra) {
    SB o = {0}; sba(&o, "a="); sba(&o, a); if (extra && *extra) { sba(&o, "&"); sba(&o, extra); }
    for (int i = 0; i < f->n; i++) {
        Field *d = &f->f[i]; if (d->type == F_NOTE) continue; char iso[11];
        pp(&o, d->key, d->type == F_DATE && in2iso(d->val, iso, 0) ? iso : d->val);
    }
    return o.p;
}
static const char *json_find(const char *j, const char *key) { char pat[48]; snprintf(pat, 48, "\"%s\":", key); const char *p = strstr(j, pat); return p ? p + strlen(pat) : NULL; }
static int json_str(const char *j, const char *key, char *out, int n) {
    const char *p = json_find(j, key); if (!p || *p != '"') return 0; p++; int i = 0;
    while (*p && *p != '"' && i < n - 1) {
        if (*p == '\\') { p++; if (*p == 'n') out[i++] = '\n'; else if (*p == 'u') { unsigned u = 63; sscanf(p + 1, "%4x", &u); out[i++] = u < 128 ? (char)u : '?'; p += 4; } else out[i++] = *p; p++; }
        else out[i++] = *p++;
    }
    out[i] = 0; return 1;
}
static double json_num(const char *j, const char *key) { const char *p = json_find(j, key); return p ? atof(p) : 0; }
static char *api_raw(const char *params) { char *buf = strdup(params); SB o = {0}; parse(buf); api(&o); free(buf); if (!o.p) sba(&o, "{}"); return o.p; }
static long long last_id; static char last_no[60], last_file[160]; static int last_missing;
/* api çağır: hata -> mesaj, uyarı -> onay sorup force=1 ile yeniden dener */
static int submit(const char *params0) {
    char *params = strdup(params0); int ok = 0;
    for (;;) {
        char *j = api_raw(params), m[700];
        if (!strncmp(j, "{\"err\"", 6)) { json_str(j, "err", m, 700); tk_msg("Hata", m); free(j); break; }
        if (!strncmp(j, "{\"warn\"", 7)) {
            json_str(j, "warn", m, 700); free(j); if (!tk_confirm("Uyarı", m)) break;
            char *np2 = malloc(strlen(params) + 12); sprintf(np2, "%s&force=1", params); free(params); params = np2; continue;
        }
        last_id = (long long)json_num(j, "id"); last_no[0] = 0; json_str(j, "no", last_no, 60); last_file[0] = 0; json_str(j, "file", last_file, 160); last_missing = (int)json_num(j, "missing");
        free(j); ok = 1; break;
    }
    free(params); return ok;
}
static int need_row(int row) { if (!row) tk_msg("Seçim gerekli", "Önce listeden bir satır seçin."); return row != 0; }

/* ---------- görünüm kurulumu ---------- */
static int btn_style(int a) {
    switch (a) {
    case A_NEWSALE: case A_NEWPURCH: case A_INV_NEW: case A_PAR_NEW: case A_PRD_NEW: case A_EXP_ADD: return 1;
    case A_PAYQUICK: case A_PAR_PAY: case A_INV_PRINT: case A_SET_BACKUP: case A_GOEXP: return 2;
    case A_INV_CANCEL: case A_EXP_CANCEL: case A_PAR_PAYCANCEL: case A_SET_RESTORE: return 3;
    default: return 0;
    }
}
static void vclear(void) { table_free(&VW.t); free(VW.head); VW.head = NULL; VW.nb = 0; VW.nk = 0; VW.dflt = -1; }
static void vb(int id, const char *label, int needrow) { int i = VW.nb++; VW.bid[i] = id; snprintf(VW.btn[i], 60, "%s", label); VW.needrow[i] = needrow; VW.style[i] = btn_style(id); }
/* özet kartı: stil 0 koyu, 1 mavi, 2 yeşil, 3 kırmızı, 4 turuncu */
static void kpi(const char *label, const char *value, int style) { if (VW.nk >= 8) return; int i = VW.nk++; snprintf(VW.klab[i], 60, "%s", label); snprintf(VW.kval[i], 60, "%s", value); VW.kstyle[i] = style; }
static int kneg(long long c, int pos_style) { return c < 0 ? 3 : pos_style; }
static void vq(const char *fmt, ...) { va_list a; va_start(a, fmt); char *s = sqlite3_vmprintf(fmt, a); va_end(a); table_query(&VW.t, s); sqlite3_free(s); }
static void kv(SB *h, const char *lab, long long c) { int pad = 38 - ulen(lab); if (pad < 1) pad = 1; sba(h, "  "); sba(h, lab); for (int i = 0; i < pad; i++) sba(h, " "); char b[48]; fmtm(c, b); sbf(h, "%14s €\r\n", b); }
#define JN(j, k) llround(json_num(j, k))
static void years_table(Table *t) {
    static const char *hh[] = {"Yıl", "Satış", "Alış", "Gider", "Cari tahsilat", "Fatura"}; static const int rr[] = {0, 1, 1, 1, 1, 1}; table_head(t, 6, hh, rr);
    int ys[64], n = scan_years(ys); sqlite3 *keep = db;
    for (int i = 0; i < n; i++) {
        char p[40]; snprintf(p, 40, "mixpack_%d.db", ys[i]); if (sqlite3_open_v2(p, &db, SQLITE_OPEN_READONLY, 0)) { db = keep; continue; }
        char c0[16], c1[48], c2[48], c3[48], c4[48], c5[16]; const char *cl[6] = {c0, c1, c2, c3, c4, c5};
        snprintf(c0, 16, "%d", ys[i]);
        snprintf(c1, 48, "%s", M(llround(val("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='S' AND status='A'"))));
        snprintf(c2, 48, "%s", M(llround(val("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type='P' AND status='A'"))));
        snprintf(c3, 48, "%s", M(llround(val("SELECT COALESCE(SUM(amount),0) FROM expense WHERE status='A'"))));
        snprintf(c4, 48, "%s", M(llround(val("SELECT COALESCE(SUM(x.amount),0) FROM payment x JOIN party r ON r.id=x.party_id WHERE r.kind='C' AND x.status='A'"))));
        snprintf(c5, 16, "%d", (int)val("SELECT COUNT(*) FROM invoice WHERE status='A'"));
        sqlite3_close(db); db = keep; table_row(t, 0, cl);
    }
}
static void build_view(void) {
    vclear(); snprintf(VW.title, 120, "%s", PAGE[cur_page]);
    switch (cur_page) {
    case 0: {
        char *j = api_raw("a=dash"); SB h = {0};
        kpi("Bugünkü satış", M(JN(j, "sales_today_c")), 1); kpi("Bu ayki satış", M(JN(j, "sales_month_c")), 1);
        kpi("Bugünkü tahsilat (cari)", M(JN(j, "coll_c")), 2); kpi("Bugünkü peşin satış", M(JN(j, "cashsale_c")), 2);
        kpi("Müşterilerden alacak", M(JN(j, "recv_c")), JN(j, "recv_c") > 0 ? 3 : 0); kpi("Tedarikçilere borç", M(JN(j, "debt_c")), JN(j, "debt_c") > 0 ? 3 : 0);
        kpi("Kasa bakiyesi", M(JN(j, "cash_c")), kneg(JN(j, "cash_c"), 0)); kpi("Banka bakiyesi", M(JN(j, "bank_c")), kneg(JN(j, "bank_c"), 0));
        sqlite3_stmt *s; sba(&h, "Kritik stoklar: "); int c = 0;
        if (!sqlite3_prepare_v2(db, "SELECT p.name,p.unit," STK " FROM product p WHERE p.active=1 AND (" STK "<=0 OR (p.min_stock>0 AND " STK "<=p.min_stock)) ORDER BY " STK " LIMIT 8", -1, &s, 0)) {
            while (sqlite3_step(s) == SQLITE_ROW) { sbf(&h, "%s%s (%s %s)", c++ ? ", " : "", sqlite3_column_text(s, 0), Q(sqlite3_column_double(s, 2)), sqlite3_column_text(s, 1)); } sqlite3_finalize(s);
        }
        if (!c) sba(&h, "yok"); sba(&h, "\r\n");
        double bd = json_num(j, "backup_days"); if (bd < 0) sba(&h, "⚠ Henüz hiç yedek alınmamış! Ayarlar > Yedek al\r\n"); else if (bd > 7) sbf(&h, "⚠ Son yedek %.0f gün önce alındı. Ayarlar > Yedek al\r\n", bd);
        free(j); VW.head = h.p;
        vb(A_NEWSALE, "＋ Yeni satış faturası", 0); vb(A_NEWPURCH, "＋ Yeni alış faturası", 0); vb(A_PAYQUICK, "＋ Tahsilat al", 0); vb(A_GOEXP, "＋ Gider ekle", 0); vb(A_INV_DETAIL, "Fatura detayı", 1); VW.dflt = 4;
        vq("SELECT i.id,i.no \"No\"," DF("i.date") " \"Tarih\",CASE i.type WHEN 'S' THEN 'Satış' ELSE 'Alış' END \"Tür\",p.name \"Cari\",i.total \"Tutar_c\",CASE i.paid WHEN 1 THEN 'ÖDENDİ' ELSE 'ÖDENMEDİ' END \"Durum\" FROM invoice i JOIN party p ON p.id=i.party_id WHERE i.status='A' ORDER BY i.id DESC LIMIT 15");
        break; }
    case 1: case 2: {
        char ty = cur_page == 1 ? 'S' : 'P'; char tys[2] = {ty, 0};
        vb(A_INV_NEW, "＋ Yeni fatura", 0); vb(A_INV_DETAIL, "Detay / satırlar", 1); VW.dflt = 1; if (ty == 'S') vb(A_INV_PRINT, "🖨 Yazdır / PDF", 1); vb(A_INV_CANCEL, "İptal et", 1); vb(A_INV_FILTER, "Filtrele / ara…", 0); vb(A_CSV, "Excel/CSV…", 0);
        { long long tot = llround(val("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type=%Q AND status='A'", tys)), open = llround(val("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type=%Q AND status='A' AND paid=0", tys)),
                    mo = llround(val("SELECT COALESCE(SUM(total),0) FROM invoice WHERE type=%Q AND status='A' AND substr(date,1,7)=substr(%Q,1,7)", tys, today())); char n[16]; snprintf(n, 16, "%d", (int)val("SELECT COUNT(*) FROM invoice WHERE type=%Q AND status='A'", tys));
          kpi(ty == 'S' ? "Toplam satış (dönem)" : "Toplam alış (dönem)", M(tot), 1); kpi(ty == 'S' ? "Bu ay" : "Bu ay", M(mo), 1); kpi(ty == 'S' ? "Ödenmemiş (alacak)" : "Ödenmemiş (borcumuz)", M(open), open > 0 ? 3 : 2); kpi("Fatura adedi", n, 0); }
        const char *f = *inv_f ? inv_f : "0000-01-01", *t = *inv_t ? inv_t : "9999-12-31";
        if (*inv_q || *inv_f || *inv_t || inv_st != 1) { SB h = {0}; sbf(&h, "Filtre açık: durum=%s", inv_st == 2 ? "ödenen" : inv_st == 3 ? "ödenmeyen" : inv_st == 4 ? "iptal" : "hepsi"); if (*inv_q) sbf(&h, ", arama=“%s”", inv_q); if (*inv_f) sbf(&h, ", başlangıç=%s", inv_f); if (*inv_t) sbf(&h, ", bitiş=%s", inv_t); VW.head = h.p; }
        vq("SELECT i.id,i.no \"Fatura no\"," DF("i.date") " \"Tarih\",p.name \"%s\",i.total \"Tutar_c\",CASE i.status WHEN 'X' THEN 'İPTAL' ELSE CASE i.paid WHEN 1 THEN 'ÖDENDİ '||CASE i.method WHEN 'B' THEN '(Banka)' ELSE '(Nakit)' END ELSE 'ÖDENMEDİ' END END \"Durum\" "
           "FROM invoice i JOIN party p ON p.id=i.party_id WHERE i.type=%Q AND i.date BETWEEN %Q AND %Q AND "
           "(%d=1 OR (%d=2 AND i.paid=1 AND i.status='A') OR (%d=3 AND i.paid=0 AND i.status='A') OR (%d=4 AND i.status='X')) AND "
           "(%Q='' OR i.no LIKE '%%'||%Q||'%%' OR p.name LIKE '%%'||%Q||'%%' OR i.note LIKE '%%'||%Q||'%%' OR EXISTS(SELECT 1 FROM line l JOIN product r ON r.id=l.product_id WHERE l.inv_id=i.id AND r.name LIKE '%%'||%Q||'%%')) ORDER BY i.date DESC,i.id DESC",
           ty == 'S' ? "Müşteri" : "Tedarikçi", tys, f, t, inv_st, inv_st, inv_st, inv_st, inv_q, inv_q, inv_q, inv_q, inv_q);
        break; }
    case 3: case 4: {
        char k[2] = {cur_page == 3 ? 'C' : 'S', 0}; int c = cur_page == 3;
        vb(A_PAR_NEW, c ? "＋ Yeni müşteri" : "＋ Yeni tedarikçi", 0); vb(A_PAR_EDIT, "Düzenle", 1); VW.dflt = 1; vb(A_PAR_STMT, "📄 Hesap ekstresi", 1); vb(A_PAR_PAY, c ? "Tahsilat al" : "Ödeme yap", 1);
        vb(A_PAR_PAYCANCEL, c ? "Tahsilat iptali" : "Ödeme iptali", 1); vb(A_PAR_TOGGLE, "Aktif / Pasif", 1); vb(A_PAR_DEBT, par_debt ? "Hepsini göster" : (c ? "Sadece borçlular" : "Sadece borcumuz olanlar"), 0); vb(A_CSV, "Excel/CSV…", 0);
        { long long tb = totbal(k[0]); char n1[16], n2[16]; snprintf(n1, 16, "%d", (int)val("SELECT COUNT(*) FROM party p WHERE p.kind=%Q AND p.active=1", k)); snprintf(n2, 16, "%d", (int)val("SELECT COUNT(*) FROM party p WHERE p.kind=%Q AND " BAL ">0", k));
          kpi(c ? "Müşterilerden toplam alacak" : "Tedarikçilere toplam borcumuz", M(tb), tb > 0 ? 3 : 2); kpi(c ? "Borçlu müşteri sayısı" : "Borçlu olduğumuz tedarikçi", n2, 0); kpi(c ? "Aktif müşteri" : "Aktif tedarikçi", n1, 0); }
        if (par_debt) VW.head = strdup(c ? "Gösterilen: yalnızca borçlu müşteriler (borca göre sıralı)" : "Gösterilen: yalnızca borcumuz olan tedarikçiler");
        vq("SELECT p.id,p.code \"Kod\",p.name \"Ad\",p.company \"Firma\",p.phone \"Telefon\"," BAL " \"%s_c\",CASE p.active WHEN 1 THEN '' ELSE 'PASİF' END \"Durum\" FROM party p WHERE p.kind=%Q AND (%d=0 OR " BAL ">0) ORDER BY %s",
           c ? "Bakiye (borcu)" : "Bakiye (borcumuz)", k, par_debt, par_debt ? BAL " DESC" : "p.name");
        break; }
    case 5:
        vb(A_PRD_NEW, "＋ Yeni ürün", 0); vb(A_PRD_EDIT, "Düzenle", 1); VW.dflt = 1; vb(A_PRD_HIST, "Hareketler", 1); vb(A_PRD_TOGGLE, "Aktif / Pasif", 1); vb(A_PRD_ASOF, "Tarih itibarıyla stok…", 0); vb(A_PRD_PERIOD, "Dönem stok raporu…", 0); vb(A_CSV, "Excel/CSV…", 0);
        { char a[16], b[16], c2[16]; double rv, cg, sv; replay("0000-01-01", "9999-12-31", &rv, &cg, &sv, NULL);
          snprintf(a, 16, "%d", (int)val("SELECT COUNT(*) FROM product WHERE active=1")); snprintf(b, 16, "%d", (int)val("SELECT COUNT(*) FROM product p WHERE p.active=1 AND p.min_stock>0 AND " STK ">0 AND " STK "<=p.min_stock")); snprintf(c2, 16, "%d", (int)val("SELECT COUNT(*) FROM product p WHERE p.active=1 AND " STK "<=0"));
          kpi("Aktif ürün", a, 1); kpi("Kritik stok (minimum altı)", b, atoi(b) ? 4 : 0); kpi("Stokta yok", c2, atoi(c2) ? 3 : 0); kpi("Stok değeri (ort. maliyet)", M(llround(sv)), 2); }
        vq("SELECT p.id,p.code \"Kod\",p.name \"Ürün\",p.category \"Kategori\",p.unit \"Birim\"," STK " \"Stok\",p.min_stock \"Minimum\",p.buy_price \"Alış fiyatı_c\",p.sell_price \"Satış fiyatı_c\",CASE WHEN p.active=0 THEN 'PASİF' WHEN " STK "<=0 THEN 'STOK YOK' WHEN " STK "<=p.min_stock AND p.min_stock>0 THEN 'KRİTİK' ELSE '' END \"Durum\" FROM product p ORDER BY p.name");
        break;
    case 6: case 7: {
        char m = cur_page == 6 ? 'N' : 'B', k[2] = {m, 0}; vb(A_CSV, "Excel/CSV…", 0);
        { long long bal = cashbal(m), in = llround(val("SELECT COALESCE(SUM(a),0) FROM fin WHERE m=%Q AND a>0", k)), out = llround(val("SELECT COALESCE(-SUM(a),0) FROM fin WHERE m=%Q AND a<0", k));
          kpi(m == 'N' ? "Kasa bakiyesi" : "Banka bakiyesi", M(bal), kneg(bal, 2)); kpi("Toplam giriş", M(in), 2); kpi("Toplam çıkış", M(out), out > 0 ? 3 : 0); }
        vq("SELECT " DF("d") " \"Tarih\",t \"Açıklama\",CASE WHEN a>0 THEN a ELSE 0 END \"Giriş_c\",CASE WHEN a<0 THEN -a ELSE 0 END \"Çıkış_c\",run \"Bakiye_c\" FROM (SELECT d,t,a,%lld+SUM(a) OVER (ORDER BY d,t,a) run FROM fin WHERE m=%Q) ORDER BY d DESC,t DESC,a DESC",
           setn(m == 'N' ? "cash_open" : "bank_open"), k);
        break; }
    case 8:
        vb(A_EXP_ADD, "＋ Yeni gider", 0); vb(A_EXP_CANCEL, "Gideri iptal et", 1); vb(A_EXP_SUMS, "Kategori toplamları", 0); vb(A_CSV, "Excel/CSV…", 0);
        { long long tot = llround(val("SELECT COALESCE(SUM(amount),0) FROM expense WHERE status='A'")), mo = llround(val("SELECT COALESCE(SUM(amount),0) FROM expense WHERE status='A' AND substr(date,1,7)=substr(%Q,1,7)", today())); char n[16]; snprintf(n, 16, "%d", (int)val("SELECT COUNT(*) FROM expense WHERE status='A'"));
          kpi("Toplam gider (dönem)", M(tot), 3); kpi("Bu ay", M(mo), 3); kpi("Gider adedi", n, 0); }
        vq("SELECT id," DF("date") " \"Tarih\",category \"Kategori\",descr \"Açıklama\",amount \"Tutar_c\",CASE method WHEN 'B' THEN 'Banka' ELSE 'Nakit' END \"Ödeme\",CASE status WHEN 'X' THEN 'İPTAL' ELSE '' END \"Durum\" FROM expense ORDER BY date DESC,id DESC");
        break;
    case 9: {
        vb(A_REP_SUM, "Özet ve kâr", 0); vb(A_REP_PROD, "Ürünler", 0); vb(A_REP_CUST, "Müşteriler", 0); vb(A_REP_DIST, "Satış dağılımı", 0); vb(A_REP_YEARS, "Yıl karşılaştırma", 0); vb(A_REP_AUDIT, "İşlem geçmişi", 0); vb(A_REP_PERIOD, "Dönem seç…", 0); vb(A_CSV, "Excel/CSV…", 0);
        char d1[11], d2[11]; iso2disp(rep_f, d1); iso2disp(rep_t, d2); SB h = {0};
        if (rep_kind <= 3) sbf(&h, "Dönem: %s – %s\r\n", d1, d2);
        if (rep_kind == 0) {
            char p[100]; snprintf(p, 100, "a=rep_summary&f=%s&t=%s", rep_f, rep_t); char *j = api_raw(p);
            kpi("Satış (KDV dahil)", M(JN(j, "sales_c")), 1); kpi("Toplam alış", M(JN(j, "buy_c")), 0); kpi("İşletme giderleri", M(JN(j, "exp_c")), 3); kpi("Brüt kâr", M(JN(j, "gross_c")), kneg(JN(j, "gross_c"), 2)); kpi("Giderler sonrası sonuç", M(JN(j, "result_c")), kneg(JN(j, "result_c"), 2));
            sba(&h, "\r\nSATIŞ\r\n"); kv(&h, "Satış (KDV dahil)", JN(j, "sales_c")); kv(&h, "  - Peşin / ödenmiş", JN(j, "cashsale_c")); kv(&h, "  - Veresiye (cariye yazılan)", JN(j, "credit_c")); kv(&h, "Cari tahsilatları", JN(j, "coll_c"));
            sba(&h, "\r\nALIŞ VE GİDER\r\n"); kv(&h, "Toplam alış", JN(j, "buy_c")); kv(&h, "Tedarikçiye yapılan ödemeler", JN(j, "supp_c")); kv(&h, "İşletme giderleri", JN(j, "exp_c"));
            sba(&h, "\r\nKÂR (ortalama maliyet, KDV hariç)\r\n"); kv(&h, "Satış cirosu (net)", JN(j, "net_c")); kv(&h, "Satılan ürün maliyeti", -JN(j, "cogs_c")); kv(&h, "Brüt kâr", JN(j, "gross_c")); kv(&h, "İşletme giderleri", -JN(j, "exp_c")); kv(&h, "GİDERLER SONRASI SONUÇ", JN(j, "result_c")); kv(&h, "Stok değeri (ort. maliyet)", JN(j, "stockval_c"));
            sba(&h, "\r\nANLIK DURUM\r\n"); kv(&h, "Müşteri alacağı", JN(j, "recv_c")); kv(&h, "Tedarikçi borcu", JN(j, "debt_c")); kv(&h, "Kasa", JN(j, "cash_c")); kv(&h, "Banka", JN(j, "bank_c")); free(j);
        }
        VW.head = h.p; setrange(rep_f, rep_t);
        if (rep_kind == 1) vq("SELECT p.name \"Ürün\",p.unit \"Birim\",COALESCE(SUM(CASE WHEN m.ty='S' THEN -m.q END),0) \"Satılan\",COALESCE(SUM(CASE WHEN m.ty='S' THEN m.tot END),0) \"Satış tutarı_c\",COALESCE(SUM(CASE WHEN m.ty='P' THEN m.q END),0) \"Alınan\",COALESCE(SUM(CASE WHEN m.ty='P' THEN m.tot END),0) \"Alış tutarı_c\" FROM product p LEFT JOIN mv m ON m.pid=p.id AND m.d BETWEEN " RF " AND " RT " GROUP BY p.id HAVING \"Satılan\">0 OR \"Alınan\">0 ORDER BY \"Satış tutarı_c\" DESC");
        else if (rep_kind == 2) vq("SELECT p.code \"Kod\",p.name \"Müşteri\",COUNT(i.id) \"Fatura\",SUM(i.total) \"Satış_c\",SUM(CASE WHEN i.paid=1 THEN i.total ELSE 0 END) \"Peşin ödenen_c\" FROM party p JOIN invoice i ON i.party_id=p.id AND i.type='S' AND i.status='A' AND i.date BETWEEN " RF " AND " RT " WHERE p.kind='C' GROUP BY p.id ORDER BY 4 DESC");
        else if (rep_kind == 3) vq("SELECT strftime(%Q,date) \"Dönem\",COUNT(*) \"Fatura\",SUM(total) \"Satış_c\" FROM invoice WHERE type='S' AND status='A' AND date BETWEEN " RF " AND " RT " GROUP BY 1 ORDER BY 1", rep_group == 3 ? "%Y-%m" : rep_group == 2 ? "%Y-W%W" : "%Y-%m-%d");
        else if (rep_kind == 4) years_table(&VW.t);
        else if (rep_kind == 5) table_query(&VW.t, "SELECT id,ts \"Zaman\",what \"İşlem\" FROM audit ORDER BY id DESC LIMIT 300");
        break; }
    case 10:
        vb(A_SET_FIRM, "Firma bilgileri…", 0); vb(A_SET_OPEN, "Açılış kasa/banka…", 0); vb(A_SET_NEG, "Negatif stok izni aç/kapat", 0); vb(A_SET_BACKUP, "💾 Yedek al…", 0); vb(A_SET_RESTORE, "Yedekten geri yükle…", 0); vb(A_SET_NEWYEAR, "Yeni yıl aç…", 0); vb(A_SET_YEAR, "Yıl değiştir…", 0);
        VW.head = strdup("Ayarlar. Yedek almayı unutmayın: bilgisayarı değiştirmeden önce mutlaka yedek alın.");
        table_query(&VW.t, "SELECT CASE k WHEN 'co_name' THEN 'Firma adı' WHEN 'co_addr' THEN 'Adres' WHEN 'co_tax' THEN 'Steuernummer' WHEN 'co_vat' THEN 'USt-IdNr.' WHEN 'co_phone' THEN 'Telefon' WHEN 'co_email' THEN 'E-posta' WHEN 'co_iban' THEN 'IBAN/Banka' "
                           "WHEN 'allow_neg' THEN 'Negatif stoğa satış (1=izinli)' WHEN 'cash_open' THEN 'Açılış kasa (€)' WHEN 'bank_open' THEN 'Açılış banka (€)' WHEN 'last_backup' THEN 'Son yedek' ELSE k END \"Ayar\","
                           "CASE WHEN k IN ('cash_open','bank_open') THEN printf('%.2f',CAST(v AS INTEGER)/100.0) ELSE v END \"Değer\" FROM settings ORDER BY k");
        break;
    }
    if (VW.t.nc > 0 && VW.t.nr == 0 && !VW.head && cur_page != 9) VW.head = strdup("Bu listede henüz kayıt yok. Üstteki renkli düğmelerle ekleme yapabilirsiniz.");
}

/* ---------- akışlar ---------- */
static void fill_parties(Field *p, char kind) {
    char k[2] = {kind, 0}; sqlite3_stmt *s; char *q = sqlite3_mprintf("SELECT id,name,company FROM party WHERE kind=%Q AND active=1 ORDER BY name", k);
    if (!sqlite3_prepare_v2(db, q, -1, &s, 0)) { while (sqlite3_step(s) == SQLITE_ROW) { char id[16], lb[300]; snprintf(id, 16, "%d", sqlite3_column_int(s, 0)); const char *c = (const char *)sqlite3_column_text(s, 2); snprintf(lb, 300, "%s%s%s", sqlite3_column_text(s, 1), c && *c ? " · " : "", c && *c ? c : ""); fopt(p, id, lb); } sqlite3_finalize(s); }
    sqlite3_free(q);
}
static int party_flow(char kind, int id) {
    char k[2] = {kind, 0}, nm[256] = "", co[256] = "", ad[256] = "", ph[256] = "", em[256] = "", no[256] = "";
    if (id) { snprintf(nm, 256, "%s", txt("SELECT name FROM party WHERE id=%d", id)); snprintf(co, 256, "%s", txt("SELECT company FROM party WHERE id=%d", id)); snprintf(ad, 256, "%s", txt("SELECT address FROM party WHERE id=%d", id)); snprintf(ph, 256, "%s", txt("SELECT phone FROM party WHERE id=%d", id)); snprintf(em, 256, "%s", txt("SELECT email FROM party WHERE id=%d", id)); snprintf(no, 256, "%s", txt("SELECT note FROM party WHERE id=%d", id)); }
    Form *f = form_new(id ? "Cari kartını düzenle" : (kind == 'C' ? "Yeni müşteri" : "Yeni tedarikçi"));
    fadd(f, "name", "Ad *", F_TEXT, nm); fadd(f, "company", "Firma adı", F_TEXT, co); fadd(f, "address", "Adres", F_TEXT, ad); fadd(f, "phone", "Telefon", F_TEXT, ph); fadd(f, "email", "E-posta (isteğe bağlı)", F_TEXT, em); fadd(f, "note", "Açıklama", F_TEXT, no);
    if (!id) fadd(f, "opening", "Açılış bakiyesi € (borç +, boş=0)", F_TEXT, "");
    int res = 0;
    if (run_form(f, 0)) { char ex[60]; snprintf(ex, 60, "id=%d&k=%s", id, k); char *p = form_params(f, "party_save", ex); res = submit(p); free(p); }
    form_free(f); return res ? (int)last_id : 0;
}
static void pay_flow(int id) {
    if (!need_row(id)) return;
    char kind = txt("SELECT kind FROM party WHERE id=%d", id)[0]; long long bal = llround(val("SELECT " BAL " FROM party p WHERE p.id=%d", id)); char amt[32] = "", dd[11], t[300];
    if (bal > 0) eurin(bal, amt); defdate_disp(dd);
    snprintf(t, 300, "%s — %s", kind == 'C' ? "Tahsilat" : "Ödeme", txt("SELECT name FROM party WHERE id=%d", id)); Form *f = form_new(t);
    snprintf(t, 300, "Güncel bakiye: %s", M(bal)); fadd(f, "info", t, F_NOTE, "");
    fadd(f, "amount", "Tutar (€)", F_TEXT, amt); Field *m = fadd(f, "method", "Ödeme şekli", F_SELECT, "N"); fopt(m, "N", "Nakit"); fopt(m, "B", "Banka"); fadd(f, "d", "Tarih (gg.aa.yyyy)", F_DATE, dd); fadd(f, "note", "Not", F_TEXT, "");
    if (run_form(f, 1)) { char ex[40]; snprintf(ex, 40, "id=%d", id); char *p = form_params(f, "pay_add", ex); if (submit(p)) tk_msg("Kaydedildi", kind == 'C' ? "Tahsilat kaydedildi." : "Ödeme kaydedildi."); free(p); }
    form_free(f);
}
static void paycancel_flow(int id) {
    if (!need_row(id)) return; Form *f = form_new("Kaydı iptal et"); Field *s = fadd(f, "id", "İptal edilecek kayıt", F_SELECT, ""); sqlite3_stmt *st; int n = 0;
    char *q = sqlite3_mprintf("SELECT id,date,amount,method,note FROM payment WHERE party_id=%d AND status='A' ORDER BY id DESC LIMIT 50", id);
    if (!sqlite3_prepare_v2(db, q, -1, &st, 0)) { while (sqlite3_step(st) == SQLITE_ROW) { char idb[16], lb[300], d[11]; snprintf(idb, 16, "%d", sqlite3_column_int(st, 0)); iso2disp((const char *)sqlite3_column_text(st, 1), d); snprintf(lb, 300, "%s — %s — %s %s", d, M(sqlite3_column_int64(st, 2)), sqlite3_column_text(st, 3)[0] == 'B' ? "Banka" : "Nakit", sqlite3_column_text(st, 4)); fopt(s, idb, lb); n++; } sqlite3_finalize(st); }
    sqlite3_free(q);
    if (!n) { tk_msg("Bilgi", "İptal edilecek aktif kayıt yok."); form_free(f); return; }
    fadd(f, "reason", "İptal nedeni (zorunlu)", F_TEXT, "");
    if (run_form(f, 0)) { char *p = form_params(f, "pay_cancel", NULL); if (submit(p)) tk_msg("Tamam", "Kayıt iptal edildi, bakiye geri yüklendi."); free(p); }
    form_free(f);
}
static void stmt_flow(int id) {
    if (!need_row(id)) return; Form *f = form_new("Hesap ekstresi — tarih aralığı"); fadd(f, "f", "Başlangıç (boş = en baştan)", F_DATE, ""); fadd(f, "t", "Bitiş (boş = hepsi)", F_DATE, "");
    if (run_form(f, 0)) {
        char f1[11] = "0000-01-01", t1[11] = "9999-12-31"; if (*fv(f, "f")) in2iso(fv(f, "f"), f1, 0); if (*fv(f, "t")) in2iso(fv(f, "t"), t1, 0);
        Table t = {0}; char *q = sqlite3_mprintf("SELECT CASE WHEN d='0000-01-01' THEN 'Açılış' ELSE " DF("d") " END \"Tarih\",t \"İşlem\",debit \"Borç_c\",credit \"Alacak_c\",run \"Bakiye_c\" FROM (SELECT d,sid,t,debit,credit,SUM(debit-credit) OVER (ORDER BY d,sid) run FROM ("
            "SELECT '0000-01-01' d,0 sid,'Açılış bakiyesi' t,MAX(opening,0) debit,MAX(-opening,0) credit FROM party WHERE id=%d UNION ALL SELECT d,sid,t,debit,credit FROM led WHERE pid=%d)) WHERE d>=%Q AND d<=%Q ORDER BY d,sid", id, id, f1, t1);
        table_query(&t, q); sqlite3_free(q);
        char title[300], foot[400]; snprintf(title, 300, "Hesap ekstresi — %s (%s)", txt("SELECT name FROM party WHERE id=%d", id), txt("SELECT code FROM party WHERE id=%d", id));
        snprintf(foot, 400, "Güncel bakiye: %s     Toplam fatura: %s     Toplam %s: %s", M(llround(val("SELECT " BAL " FROM party p WHERE p.id=%d", id))), M(llround(val("SELECT COALESCE(SUM(total),0) FROM invoice WHERE party_id=%d AND status='A'", id))),
                 txt("SELECT kind FROM party WHERE id=%d", id)[0] == 'C' ? "tahsilat" : "ödeme", M(llround(val("SELECT COALESCE(SUM(amount),0) FROM payment WHERE party_id=%d AND status='A'", id))));
        if (tk_table(title, &t, foot, 1)) { table_html("ekstre.html", title, &t, foot); tk_open("ekstre.html"); }
        table_free(&t);
    }
    form_free(f);
}
static void print_flow(int id) {
    if (!need_row(id)) return;
    char p[40]; snprintf(p, 40, "a=inv_print&id=%d", id); if (!submit(p)) return;
    if (last_missing) tk_msg("Dikkat", "Firma adı/adresi/vergi no eksik. Ayarlar > Firma bilgileri bölümünden tamamlayın (yasal fatura için gerekli).");
    tk_open(last_file);
}
static void inv_detail_flow(int id) {
    if (!need_row(id)) return; if (!val("SELECT COUNT(*) FROM invoice WHERE id=%d", id)) return;
    Table t = {0}; char *q = sqlite3_mprintf("SELECT r.name \"Ürün\",r.unit \"Birim\",l.qty \"Miktar\",l.price \"Birim fiyat_c\",l.total \"Toplam_c\",l.vat \"KDV %%\" FROM line l JOIN product r ON r.id=l.product_id WHERE l.inv_id=%d ORDER BY l.id", id); table_query(&t, q); sqlite3_free(q);
    char ty = txt("SELECT type FROM invoice WHERE id=%d", id)[0], stt = txt("SELECT status FROM invoice WHERE id=%d", id)[0], title[200], foot[700], d[11];
    iso2disp(txt("SELECT date FROM invoice WHERE id=%d", id), d); snprintf(title, 200, "Fatura %s", txt("SELECT no FROM invoice WHERE id=%d", id));
    snprintf(foot, 700, "%s · %s · %s · %s · Toplam: %s%s%s", d, ty == 'S' ? "Müşteri" : "Tedarikçi", txt("SELECT p.name FROM party p JOIN invoice i ON i.party_id=p.id WHERE i.id=%d", id),
             stt == 'X' ? "İPTAL" : (val("SELECT paid FROM invoice WHERE id=%d", id) > 0 ? "ÖDENDİ" : "ÖDENMEDİ"), M(llround(val("SELECT total FROM invoice WHERE id=%d", id))), stt == 'X' ? " · İptal nedeni: " : "", stt == 'X' ? txt("SELECT cancel_note FROM invoice WHERE id=%d", id) : "");
    if (tk_table(title, &t, foot, ty == 'S' && stt == 'A')) print_flow(id);
    table_free(&t);
}
static void inv_cancel_flow(int id) {
    if (!need_row(id)) return;
    if (!val("SELECT COUNT(*) FROM invoice WHERE id=%d AND status='A'", id)) { tk_msg("Bilgi", "Bu fatura zaten iptal edilmiş."); return; }
    Form *f = form_new("Faturayı iptal et"); char t[300]; snprintf(t, 300, "Fatura %s iptal edilecek. Stok, cari ve kasa/banka etkisi geri alınır; kayıt arşivde kalır.", txt("SELECT no FROM invoice WHERE id=%d", id)); fadd(f, "info", t, F_NOTE, ""); fadd(f, "reason", "İptal nedeni (zorunlu)", F_TEXT, "");
    if (run_form(f, 0)) { char ex[40]; snprintf(ex, 40, "id=%d", id); char *p = form_params(f, "inv_cancel", ex); if (submit(p)) tk_msg("Tamam", "Fatura iptal edildi. Düzeltme için yeni fatura girin."); free(p); }
    form_free(f);
}
static void inv_filter_flow(void) {
    Form *f = form_new("Fatura filtresi"); char a[11], b[11], st[4]; iso2disp(inv_f, a); iso2disp(inv_t, b); snprintf(st, 4, "%d", inv_st);
    Field *s = fadd(f, "st", "Durum", F_SELECT, st); fopt(s, "1", "Hepsi"); fopt(s, "2", "Ödenen"); fopt(s, "3", "Ödenmeyen"); fopt(s, "4", "İptal edilen");
    fadd(f, "f", "Başlangıç tarihi (boş=hepsi)", F_DATE, a); fadd(f, "t", "Bitiş tarihi (boş=hepsi)", F_DATE, b); fadd(f, "q", "Ara (no, cari, ürün, açıklama)", F_TEXT, inv_q);
    if (run_form(f, 0)) { inv_st = atoi(fv(f, "st")); inv_f[0] = inv_t[0] = 0; if (*fv(f, "f")) in2iso(fv(f, "f"), inv_f, 0); if (*fv(f, "t")) in2iso(fv(f, "t"), inv_t, 0); snprintf(inv_q, 100, "%s", fv(f, "q")); }
    form_free(f);
}
static void new_invoice_flow(char type) {
    char kind = type == 'S' ? 'C' : 'S', hd[11], hno[60] = "", hpaid[4] = "0", hm[4] = "N", hnote[200] = ""; int pid = 0; defdate_disp(hd);
    for (;;) {   /* başlık formu (yeni cari eklenirse tekrar gösterilir) */
        Form *f = form_new(type == 'S' ? "Yeni satış faturası" : "Yeni alış faturası"); char ps[16]; snprintf(ps, 16, "%d", pid);
        fadd(f, "d", "Fatura tarihi (gg.aa.yyyy)", F_DATE, hd);
        Field *p = fadd(f, "pid", type == 'S' ? "Müşteri" : "Tedarikçi", F_SELECT, pid ? ps : ""); fopt(p, "0", type == 'S' ? "＋ Yeni müşteri ekle…" : "＋ Yeni tedarikçi ekle…"); fill_parties(p, kind);
        fadd(f, "no", type == 'S' ? "Fatura no (boş = otomatik)" : "Tedarikçi fatura no (zorunlu)", F_TEXT, hno);
        Field *pd = fadd(f, "paid", "Ödeme durumu", F_SELECT, hpaid); fopt(pd, "0", "ÖDENMEDİ (açık fatura, cariye borç yazılır)"); fopt(pd, "1", "ÖDENDİ (kapalı fatura, kasa/bankaya işlenir)");
        Field *m = fadd(f, "method", "Ödendiyse ödeme şekli", F_SELECT, hm); fopt(m, "N", "Nakit"); fopt(m, "B", "Banka"); fadd(f, "note", "Açıklama", F_TEXT, hnote);
        if (!run_form(f, 1)) { form_free(f); return; }
        snprintf(hd, 11, "%s", fv(f, "d")); snprintf(hno, 60, "%s", fv(f, "no")); snprintf(hpaid, 4, "%s", fv(f, "paid")); snprintf(hm, 4, "%s", fv(f, "method")); snprintf(hnote, 200, "%s", fv(f, "note"));
        int sel = atoi(fv(f, "pid")); form_free(f);
        if (sel == 0) { int nid = party_flow(kind, 0); if (nid) pid = nid; continue; }
        pid = sel; break;
    }
    typedef struct { int pid; char q[40], x[40], name[100]; double qty; long long price; } Ln; static Ln ln[100]; int nl = 0; long long tot = 0;
    for (;;) {   /* satırlar */
        Form *l = form_new("Fatura satırı ekle"); Field *p = fadd(l, "pid", "Ürün", F_SELECT, ""); sqlite3_stmt *s;
        if (!sqlite3_prepare_v2(db, "SELECT p.id,p.name,p.unit," STK " FROM product p WHERE p.active=1 ORDER BY p.name", -1, &s, 0)) {
            while (sqlite3_step(s) == SQLITE_ROW) { char id[16], lb[300]; snprintf(id, 16, "%d", sqlite3_column_int(s, 0)); snprintf(lb, 300, "%s (stok %s %s)", sqlite3_column_text(s, 1), Q(sqlite3_column_double(s, 3)), sqlite3_column_text(s, 2)); fopt(p, id, lb); } sqlite3_finalize(s);
        }
        if (!p->nopt) { tk_msg("Ürün yok", "Önce Stoklar bölümünden ürün ekleyin."); form_free(l); return; }
        fadd(l, "q", "Miktar", F_TEXT, ""); fadd(l, "x", "Birim fiyat € (KDV dahil; boş = ürün fiyatı)", F_TEXT, "");
        if (!run_form(l, 0)) { form_free(l); if (!nl) return; break; }
        Ln *n = &ln[nl]; n->pid = atoi(fv(l, "pid")); n->qty = pnum(fv(l, "q")); snprintf(n->q, 40, "%s", fv(l, "q")); snprintf(n->x, 40, "%s", fv(l, "x")); form_free(l);
        if (n->qty <= 0) { tk_msg("Hata", "Miktar sıfırdan büyük olmalı."); continue; }
        n->price = *n->x ? cents(n->x) : (long long)val("SELECT %s FROM product WHERE id=%d", type == 'S' ? "sell_price" : "buy_price", n->pid);
        if (n->price <= 0) { tk_msg("Hata", "Birim fiyat gerekli (ürünün kayıtlı fiyatı yok)."); continue; }
        snprintf(n->name, 100, "%s", txt("SELECT name FROM product WHERE id=%d", n->pid)); tot += llround(n->qty * n->price); nl++;
        char m[400]; snprintf(m, 400, "Eklendi: %s — %s × %s\nŞimdiye kadar %d satır, toplam %s.\n\nBaşka bir ürün satırı eklensin mi?", n->name, Q(n->qty), M(n->price), nl, M(tot));
        if (!tk_confirm("Satır eklendi", m) || nl >= 100) break;
    }
    SB c = {0}; char iso[11]; if (!in2iso(hd, iso, 1)) { tk_msg("Hata", "Tarih geçersiz."); return; }
    sbf(&c, "Fatura özeti\nTarih: %s\n%s: %s\n", hd, type == 'S' ? "Müşteri" : "Tedarikçi", txt("SELECT name FROM party WHERE id=%d", pid));
    for (int i = 0; i < nl; i++) sbf(&c, "  • %s — %s × %s = %s\n", ln[i].name, Q(ln[i].qty), M(ln[i].price), M(llround(ln[i].qty * ln[i].price)));
    sbf(&c, "\nToplam: %s\nÖdeme: %s\n\nFatura kaydedilsin mi?", M(tot), hpaid[0] == '1' ? (hm[0] == 'B' ? "ÖDENDİ (Banka)" : "ÖDENDİ (Nakit)") : "ÖDENMEDİ (cariye borç yazılır)");
    int go = tk_confirm("Faturayı kaydet", c.p); free(c.p); if (!go) return;
    SB o = {0}; sbf(&o, "a=inv_save&type=%c", type); pp(&o, "d", iso); char pidb[16]; snprintf(pidb, 16, "%d", pid); pp(&o, "pid", pidb); pp(&o, "no", hno); pp(&o, "paid", hpaid); pp(&o, "method", hm); pp(&o, "note", hnote);
    for (int i = 0; i < nl; i++) { char b[16]; snprintf(b, 16, "%d", ln[i].pid); pp(&o, "lp", b); pp(&o, "lq", ln[i].q); pp(&o, "lx", ln[i].x); }
    if (submit(o.p)) {
        int id = (int)last_id; char no[60], m[200]; snprintf(no, 60, "%s", last_no);
        if (type == 'S') { snprintf(m, 200, "Fatura %s kaydedildi.\nYazdırma sayfası açılsın mı?", no); if (tk_confirm("Kaydedildi", m)) print_flow(id); }
        else { snprintf(m, 200, "Fatura %s kaydedildi. Stok ve tedarikçi borcu güncellendi.", no); tk_msg("Kaydedildi", m); }
    }
    free(o.p);
}
static void product_flow(int id) {
    char nm[256] = "", ca[256] = "", un[40] = "kg", bp[32] = "", sp[32] = "", mq[32] = "", vt[8] = "7";
    if (id) { snprintf(nm, 256, "%s", txt("SELECT name FROM product WHERE id=%d", id)); snprintf(ca, 256, "%s", txt("SELECT category FROM product WHERE id=%d", id)); snprintf(un, 40, "%s", txt("SELECT unit FROM product WHERE id=%d", id));
        long long b = (long long)val("SELECT buy_price FROM product WHERE id=%d", id), s = (long long)val("SELECT sell_price FROM product WHERE id=%d", id); if (b) eurin(b, bp); if (s) eurin(s, sp); snprintf(mq, 32, "%s", Q(val("SELECT min_stock FROM product WHERE id=%d", id))); snprintf(vt, 8, "%s", Q(val("SELECT vat FROM product WHERE id=%d", id))); }
    Form *f = form_new(id ? "Ürünü düzenle" : "Yeni ürün"); fadd(f, "name", "Ürün adı *", F_TEXT, nm); if (!id) fadd(f, "code", "Ürün kodu (boş = otomatik)", F_TEXT, ""); fadd(f, "category", "Kategori", F_TEXT, ca);
    Field *u = fadd(f, "unit", "Birim", F_SELECT, un); fopt(u, "kg", "kg"); fopt(u, "adet", "adet"); fopt(u, "kasa", "kasa"); fopt(u, "paket", "paket");
    fadd(f, "buy", "Alış fiyatı (€)", F_TEXT, bp); fadd(f, "sell", "Satış fiyatı (€, isteğe bağlı)", F_TEXT, sp); fadd(f, "minq", "Minimum stok uyarısı", F_TEXT, mq);
    Field *v = fadd(f, "vat", "KDV %", F_SELECT, vt); fopt(v, "7", "7 (gıda)"); fopt(v, "19", "19"); fopt(v, "0", "0");
    if (!id) { fadd(f, "oqty", "Açılış stoku (boş = 0)", F_TEXT, ""); fadd(f, "ocost", "Açılış birim maliyeti € (boş = alış fiyatı)", F_TEXT, ""); }
    if (run_form(f, 0)) { char ex[40]; snprintf(ex, 40, "id=%d", id); char *p = form_params(f, "product_save", ex); if (submit(p)) tk_msg("Kaydedildi", "Ürün kaydedildi."); free(p); }
    form_free(f);
}
static void hist_flow(int id) {
    if (!need_row(id)) return; Table t = {0};
    char *q = sqlite3_mprintf("SELECT " DF("h.d") " \"Tarih\",CASE h.ty WHEN 'P' THEN 'Alış' ELSE 'Satış' END \"İşlem\",h.no \"Fatura\",COALESCE(pa.name,'') \"Cari\",CASE WHEN h.q>0 THEN h.q ELSE 0 END \"Giren\",CASE WHEN h.q<0 THEN -h.q ELSE 0 END \"Çıkan\",h.pr \"Birim fiyat_c\",h.run \"Kalan stok\" "
        "FROM (SELECT d,iid,ty,no,party,q,pr,(SELECT opening_qty FROM product WHERE id=%d)+SUM(q) OVER (ORDER BY d,iid) run FROM mv WHERE pid=%d) h LEFT JOIN party pa ON pa.id=h.party ORDER BY h.d,h.iid", id, id);
    table_query(&t, q); sqlite3_free(q); char title[200], foot[200]; snprintf(title, 200, "Ürün hareketleri — %s", txt("SELECT name FROM product WHERE id=%d", id)); snprintf(foot, 200, "Devir / açılış stoku: %s %s", Q(val("SELECT opening_qty FROM product WHERE id=%d", id)), txt("SELECT unit FROM product WHERE id=%d", id));
    tk_table(title, &t, foot, 0); table_free(&t);
}
static int ask_period(void) {
    Form *f = form_new("Dönem seçin"); Field *p = fadd(f, "p", "Dönem", F_SELECT, "year"); fopt(p, "year", "Tüm yıl"); fopt(p, "today", "Bugün"); fopt(p, "week", "Bu hafta"); fopt(p, "month", "Bu ay"); fopt(p, "last", "Geçen ay"); fopt(p, "custom", "İki tarih arası (aşağıya yazın)");
    char a[11], b[11]; iso2disp(rep_f, a); iso2disp(rep_t, b); fadd(f, "f", "Başlangıç (yalnızca 'iki tarih arası' için)", F_DATE, a); fadd(f, "t", "Bitiş (yalnızca 'iki tarih arası' için)", F_DATE, b);
    int ok = run_form(f, 0);
    if (ok) {
        const char *v = fv(f, "p");
        if (!strcmp(v, "today")) { snprintf(rep_f, 11, "%s", today()); snprintf(rep_t, 11, "%s", rep_f); }
        else if (!strcmp(v, "week")) { snprintf(rep_f, 11, "%s", txt("SELECT date('now','localtime','weekday 0','-6 days')")); snprintf(rep_t, 11, "%s", txt("SELECT date(%Q,'+6 days')", rep_f)); }
        else if (!strcmp(v, "month")) { snprintf(rep_f, 11, "%s", txt("SELECT date('now','localtime','start of month')")); snprintf(rep_t, 11, "%s", txt("SELECT date(%Q,'+1 month','-1 day')", rep_f)); }
        else if (!strcmp(v, "last")) { snprintf(rep_f, 11, "%s", txt("SELECT date('now','localtime','start of month','-1 month')")); snprintf(rep_t, 11, "%s", txt("SELECT date(%Q,'+1 month','-1 day')", rep_f)); }
        else if (!strcmp(v, "custom")) { char x[11], y[11]; if (in2iso(fv(f, "f"), x, 0) && in2iso(fv(f, "t"), y, 0)) { strcpy(rep_f, x); strcpy(rep_t, y); } else { tk_msg("Hata", "İki tarih arası için iki tarihi de girin."); ok = 0; } }
        else reset_period();
    }
    form_free(f); return ok;
}
static void asof_flow(void) {
    Form *f = form_new("Tarih itibarıyla stok"); char d[11]; defdate_disp(d); fadd(f, "d", "Tarih (gg.aa.yyyy)", F_DATE, d);
    if (run_form(f, 0)) { char iso[11]; if (in2iso(fv(f, "d"), iso, 0)) { Table t = {0}; char *q = sqlite3_mprintf("SELECT p.name \"Ürün\",p.unit \"Birim\",p.opening_qty+COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id AND d<=%Q),0) \"Stok\" FROM product p ORDER BY p.name", iso); table_query(&t, q); sqlite3_free(q); char title[100]; snprintf(title, 100, "%s tarihi itibarıyla stok", fv(f, "d")); tk_table(title, &t, "", 0); table_free(&t); } }
    form_free(f);
}
static void stperiod_flow(void) {
    if (!ask_period()) return; setrange(rep_f, rep_t); Table t = {0};
    table_query(&t, "SELECT p.name \"Ürün\",p.unit \"Birim\",p.opening_qty+COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id AND d<" RF "),0) \"Başlangıç stok\",COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id AND ty='P' AND d BETWEEN " RF " AND " RT "),0) \"Alınan\","
                    "-COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id AND ty='S' AND d BETWEEN " RF " AND " RT "),0) \"Satılan\",p.opening_qty+COALESCE((SELECT SUM(q) FROM mv WHERE pid=p.id AND d<=" RT "),0) \"Bitiş stok\" FROM product p ORDER BY p.name");
    char a[11], b[11], foot[100]; iso2disp(rep_f, a); iso2disp(rep_t, b); snprintf(foot, 100, "Dönem: %s – %s", a, b); tk_table("Dönem stok raporu", &t, foot, 0); table_free(&t);
}
static void exp_add_flow(void) {
    char dd[11]; defdate_disp(dd); Form *f = form_new("Yeni gider"); fadd(f, "d", "Tarih (gg.aa.yyyy)", F_DATE, dd); Field *c = fadd(f, "cat", "Kategori", F_SELECT, "Mazot");
    static const char *cats[] = {"Mazot", "Araç", "Kira", "Telefon", "Bakım", "Sigorta", "Vergi", "Diğer", NULL}; for (int i = 0; cats[i]; i++) fopt(c, cats[i], cats[i]);
    fadd(f, "descr", "Açıklama", F_TEXT, ""); fadd(f, "amount", "Tutar (€)", F_TEXT, ""); Field *m = fadd(f, "method", "Ödeme", F_SELECT, "N"); fopt(m, "N", "Nakit"); fopt(m, "B", "Banka");
    if (run_form(f, 1)) { char *p = form_params(f, "exp_save", NULL); if (submit(p)) tk_msg("Kaydedildi", "Gider kaydedildi (kasa/banka güncellendi)."); free(p); }
    form_free(f);
}
static void exp_cancel_flow(int id) {
    if (!need_row(id)) return; if (!val("SELECT COUNT(*) FROM expense WHERE id=%d AND status='A'", id)) { tk_msg("Bilgi", "Bu gider zaten iptal edilmiş."); return; }
    Form *f = form_new("Gideri iptal et"); fadd(f, "reason", "İptal nedeni (zorunlu)", F_TEXT, "");
    if (run_form(f, 0)) { char ex[40]; snprintf(ex, 40, "id=%d", id); char *p = form_params(f, "exp_cancel", ex); if (submit(p)) tk_msg("Tamam", "Gider iptal edildi."); free(p); }
    form_free(f);
}
static void csv_flow(void) {
    Form *f = form_new("Excel/CSV dışa aktar"); Field *k = fadd(f, "x", "Rapor", F_SELECT, cur_page == 2 ? "2" : cur_page == 3 ? "3" : cur_page == 4 ? "4" : cur_page == 5 ? "5" : cur_page == 8 ? "7" : cur_page == 6 || cur_page == 7 ? "8" : "1");
    static const char *ls[] = {"Satış faturaları", "Alış faturaları", "Müşteri bakiyeleri", "Tedarikçi bakiyeleri", "Stok listesi", "Ürün satış raporu (Raporlar sayfasındaki dönem)", "Giderler", "Kasa + Banka hareketleri"};
    for (int i = 0; i < 8; i++) { char id[4]; snprintf(id, 4, "%d", i + 1); fopt(k, id, ls[i]); }
    if (run_form(f, 0)) {
        int x = atoi(fv(f, "x")); SB o = {0}; sba(&o, "\xEF\xBB\xBF"); csv_export(&o, x, rep_f, rep_t); char path[80]; snprintf(path, 80, "mixpack_export_%d.csv", x);
        FILE *fp = fopen(path, "wb"); if (fp) { fwrite(o.p, 1, o.n, fp); fclose(fp); tk_open(path); } else tk_msg("Hata", "Dosya yazılamadı.");
        free(o.p);
    }
    form_free(f);
}
static void settings_firm_flow(void) {
    static const char *k[] = {"co_name", "co_addr", "co_tax", "co_vat", "co_phone", "co_email", "co_iban"}, *l[] = {"Firma adı", "Adres (sokak, PLZ, şehir)", "Steuernummer", "USt-IdNr.", "Telefon", "E-posta", "IBAN / Banka"};
    Form *f = form_new("Firma bilgileri (faturada görünür)"); for (int i = 0; i < 7; i++) { char v[256]; snprintf(v, 256, "%s", txt("SELECT v FROM settings WHERE k=%Q", k[i])); fadd(f, k[i], l[i], F_TEXT, v); }
    if (run_form(f, 0)) { char *p = form_params(f, "settings_set", NULL); if (submit(p)) tk_msg("Kaydedildi", "Firma bilgileri kaydedildi."); free(p); } form_free(f);
}
static void settings_open_flow(void) {
    char a[32], b[32]; eurin(setn("cash_open"), a); eurin(setn("bank_open"), b); Form *f = form_new("Açılış bakiyeleri"); fadd(f, "cash_open", "Açılış kasa bakiyesi (€)", F_TEXT, a); fadd(f, "bank_open", "Açılış banka bakiyesi (€)", F_TEXT, b);
    if (run_form(f, 0)) { char *p = form_params(f, "settings_set", NULL); if (submit(p)) tk_msg("Kaydedildi", "Açılış bakiyeleri kaydedildi."); free(p); } form_free(f);
}
static void backup_flow(void) {
    Form *f = form_new("Yedek al"); fadd(f, "info", "Yedek klasörü: USB bellek veya harici disk yolu yazabilirsiniz (örn. E:\\yedek). Boş klasör adı = 'yedekler'.", F_NOTE, ""); fadd(f, "dir", "Yedek klasörü", F_TEXT, "yedekler");
    if (run_form(f, 0)) { char *p = form_params(f, "backup", NULL); char *j = api_raw(p); free(p); char m[400], t[500]; if (json_str(j, "path", m, 400)) { snprintf(t, 500, "Yedek alındı:\n%s", m); tk_msg("Yedek", t); } else tk_msg("Hata", "Yedek alınamadı (klasörü/diski kontrol edin)."); free(j); } form_free(f);
}
static void restore_flow(void) {
    Form *f = form_new("Yedekten geri yükle"); fadd(f, "info", "DİKKAT: Açık olan yılın verisi seçtiğiniz yedekle DEĞİŞTİRİLİR (öncekinin kopyası .oncesi dosyasında saklanır).", F_NOTE, ""); fadd(f, "path", "Yedek dosyasının yolu", F_TEXT, "");
    if (run_form(f, 0)) { if (tk_confirm("Emin misiniz?", "Mevcut yıl verisi yedekle değiştirilecek. Devam edilsin mi?")) { char *p = form_params(f, "restore", NULL); if (submit(p)) { reset_period(); tk_msg("Tamam", "Yedek geri yüklendi."); } free(p); } } form_free(f);
}
static void newyear_flow(void) {
    char y[8]; snprintf(y, 8, "%d", year + 1); Form *f = form_new("Yeni yıl / dönem aç");
    char n[300]; snprintf(n, 300, "%d yılındaki kayıtlardan devir yapılır. Eski yıl arşiv olarak korunur; fatura numaraları yeni yılda 01'den başlar.", year); fadd(f, "info", n, F_NOTE, "");
    fadd(f, "y", "Yeni yıl", F_TEXT, y); fadd(f, "cc", "Müşteri/tedarikçi kartları aktarılsın", F_CHECK, "1"); fadd(f, "dbt", "Cari borç/alacak bakiyeleri devredilsin", F_CHECK, "1"); fadd(f, "pc", "Ürün kartları aktarılsın", F_CHECK, "1"); fadd(f, "st", "Yıl sonu stokları (ort. maliyetle) devredilsin", F_CHECK, "1");
    if (run_form(f, 0)) { snprintf(n, 300, "%s yılı oluşturulsun mu?", fv(f, "y")); if (tk_confirm("Yeni yıl", n)) { char *p = form_params(f, "newyear", NULL); if (submit(p)) { reset_period(); tk_msg("Tamam", "Yeni yıl açıldı ve ekrana geçildi."); } free(p); } } form_free(f);
}
static void year_flow(void) {
    int ys[64], n = scan_years(ys); char cur[8]; snprintf(cur, 8, "%d", year); Form *f = form_new("Yıl değiştir"); Field *s = fadd(f, "y", "Çalışılacak yıl", F_SELECT, cur);
    for (int i = 0; i < n; i++) { char b[8]; snprintf(b, 8, "%d", ys[i]); fopt(s, b, b); }
    if (run_form(f, 0)) { char *p = form_params(f, "setyear", NULL); if (submit(p)) reset_period(); free(p); } form_free(f);
}

/* ---------- eylem dağıtımı ---------- */
static void act(int a, int row) {
    switch (a) {
    case A_NEWSALE: new_invoice_flow('S'); break;
    case A_NEWPURCH: new_invoice_flow('P'); break;
    case A_PAYQUICK: { Form *f = form_new("Tahsilat — müşteri seçin"); Field *p = fadd(f, "pid", "Müşteri", F_SELECT, ""); fill_parties(p, 'C'); if (!p->nopt) tk_msg("Bilgi", "Önce müşteri ekleyin."); else if (run_form(f, 0)) pay_flow(atoi(fv(f, "pid"))); form_free(f); break; }
    case A_GOEXP: cur_page = 8; exp_add_flow(); break;
    case A_INV_NEW: new_invoice_flow(cur_page == 1 ? 'S' : 'P'); break;
    case A_INV_DETAIL: inv_detail_flow(row); break;
    case A_INV_PRINT: print_flow(row); break;
    case A_INV_CANCEL: inv_cancel_flow(row); break;
    case A_INV_FILTER: inv_filter_flow(); break;
    case A_CSV: csv_flow(); break;
    case A_PAR_NEW: party_flow(cur_page == 3 ? 'C' : 'S', 0); break;
    case A_PAR_EDIT: if (need_row(row)) party_flow(cur_page == 3 ? 'C' : 'S', row); break;
    case A_PAR_STMT: stmt_flow(row); break;
    case A_PAR_PAY: pay_flow(row); break;
    case A_PAR_PAYCANCEL: paycancel_flow(row); break;
    case A_PAR_TOGGLE: if (need_row(row)) { char p[40]; snprintf(p, 40, "a=party_toggle&id=%d", row); submit(p); } break;
    case A_PAR_DEBT: par_debt = !par_debt; break;
    case A_PRD_NEW: product_flow(0); break;
    case A_PRD_EDIT: if (need_row(row)) product_flow(row); break;
    case A_PRD_HIST: hist_flow(row); break;
    case A_PRD_TOGGLE: if (need_row(row)) { char p[40]; snprintf(p, 40, "a=product_toggle&id=%d", row); submit(p); } break;
    case A_PRD_ASOF: asof_flow(); break;
    case A_PRD_PERIOD: stperiod_flow(); break;
    case A_EXP_ADD: exp_add_flow(); break;
    case A_EXP_CANCEL: exp_cancel_flow(row); break;
    case A_EXP_SUMS: { Table t = {0}; table_query(&t, "SELECT category \"Kategori\",COUNT(*) \"Adet\",SUM(amount) \"Toplam_c\" FROM expense WHERE status='A' GROUP BY category ORDER BY 3 DESC"); tk_table("Gider kategori toplamları", &t, "", 0); table_free(&t); break; }
    case A_REP_SUM: rep_kind = 0; break; case A_REP_PROD: rep_kind = 1; break; case A_REP_CUST: rep_kind = 2; break;
    case A_REP_DIST: { Form *f = form_new("Satış dağılımı"); char g[4]; snprintf(g, 4, "%d", rep_group); Field *s = fadd(f, "g", "Gruplama", F_SELECT, g); fopt(s, "1", "Günlük"); fopt(s, "2", "Haftalık"); fopt(s, "3", "Aylık"); if (run_form(f, 0)) { rep_group = atoi(fv(f, "g")); rep_kind = 3; } form_free(f); break; }
    case A_REP_YEARS: rep_kind = 4; break; case A_REP_AUDIT: rep_kind = 5; break;
    case A_REP_PERIOD: ask_period(); break;
    case A_SET_FIRM: settings_firm_flow(); break;
    case A_SET_OPEN: settings_open_flow(); break;
    case A_SET_NEG: { char p[60]; snprintf(p, 60, "a=settings_set&allow_neg=%d", setn("allow_neg") ? 0 : 1); if (submit(p)) tk_msg("Tamam", setn("allow_neg") ? "Negatif stoğa satış: AÇIK (uyarı verir)." : "Negatif stoğa satış: KAPALI."); break; }
    case A_SET_BACKUP: backup_flow(); break;
    case A_SET_RESTORE: restore_flow(); break;
    case A_SET_NEWYEAR: newyear_flow(); break;
    case A_SET_YEAR: year_flow(); break;
    }
}

/* ---------- araç takımının çağırdığı giriş noktaları ---------- */
static void core_init(void) { reset_period(); build_view(); }
static void core_goto(int p) { cur_page = p; build_view(); }
static void core_button(int bi, int row) { if (bi >= 0 && bi < VW.nb) act(VW.bid[bi], row); build_view(); }
static void core_open_row(int row) { if (VW.dflt >= 0 && row) core_button(VW.dflt, row); }
static void core_title(char *o, int n) { snprintf(o, n, "MixPack Muhasebe %d", year); }
static int core_can_exit(void) {
    char *j = api_raw("a=dash"); double bd = json_num(j, "backup_days"); free(j);
    if ((bd < 0 || bd > 7) && tk_confirm("Yedekleme", "Son 7 günde yedek alınmamış. Bilgisayar bozulursa veriler kaybolabilir.\n\nÇıkmadan önce yedek alınsın mı?")) {
        char *r = api_raw("a=backup&dir=yedekler"); char m[300], t[400]; if (json_str(r, "path", m, 300)) { snprintf(t, 400, "Yedek alındı:\n%s", m); tk_msg("Yedek", t); } else tk_msg("Yedek", "Yedek alınamadı."); free(r);
    }
    return 1;
}
#endif
