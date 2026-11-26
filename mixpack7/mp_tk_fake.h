/* mp_tk_fake.h - Linux'ta (ve Windows --selftest'te) çekirdeği pencere olmadan sınamak için SAHTE araç takımı + senaryo çalıştırıcı.
 * Yanıt satırları sırayla form alanlarına/sorulara verilir (boş satır = varsayılan, CANCEL = vazgeç). @komutlar eylemi çalıştırır. */
#ifndef MP_TK_FAKE_H
#define MP_TK_FAKE_H
#define MP_FAKE_VERSION 6
static char **ANS; static int nans, ians, fk_fail;
static void ans_clear(void) { for (int i = 0; i < nans; i++) free(ANS[i]); free(ANS); ANS = NULL; nans = ians = 0; }
static void ans_push(const char *s) { ANS = realloc(ANS, sizeof(char *) * (nans + 1)); ANS[nans++] = strdup(s); }
static const char *ans_next(void) { return ians < nans ? ANS[ians++] : ""; }
static void fk_msg(const char *t, const char *x) { printf("[MSG] %s: %s\n", t, x); }
static int fk_confirm(const char *t, const char *x) { const char *a = ans_next(); printf("[SORU] %s: %s\n       => %s\n", t, x, *a ? a : "(boş=hayır)"); return a[0] == 'e' || a[0] == 'E' || a[0] == 'y'; }
static int fk_form(Form *f) {
    printf("[FORM] %s\n", f->title);
    if (ians < nans && !strcmp(ANS[ians], "CANCEL")) { ians++; puts("       => vazgeçildi"); return 0; }
    if (ians >= nans) { puts("       !! senaryoda bu form için yanıt kalmadı -> vazgeç"); fk_fail = 1; return 0; }
    for (int i = 0; i < f->n; i++) {
        Field *d = &f->f[i]; if (d->type == F_NOTE) { printf("   (%s)\n", d->label); continue; }
        const char *a = ans_next();
        if (*a) {
            if (d->type == F_SELECT) { int ok = 0; for (int k = 0; k < d->nopt; k++) if (!strcmp(d->oid[k], a)) ok = 1; if (!ok) { printf("   !! '%s' için '%s' seçeneği yok\n", d->label, a); fk_fail = 1; continue; } }
            snprintf(d->val, 300, "%s", a);
        }
        printf("   %-45s = %s\n", d->label, d->val);
    }
    return 1;
}
static int fk_table(const char *title, Table *t, const char *foot, int printable) {
    printf("[TABLO] %s\n", title); for (int i = 0; i < t->nc; i++) printf("%s%s", i ? " | " : "   ", t->h[i]); putchar('\n');
    for (int r = 0; r < t->nr; r++) { for (int i = 0; i < t->nc; i++) printf("%s%s", i ? " | " : "   ", t->c[r][i]); putchar('\n'); }
    if (foot && *foot) printf("   %s\n", foot);
    if (printable) { const char *a = ans_next(); return a[0] == 'e'; } return 0;
}
static void fk_open(const char *p) { printf("[AÇ] %s\n", p); }
static void print_view(void) {
    printf("=== %s ===\n", VW.title); if (VW.head) printf("%s\n", VW.head);
    if (VW.nk) { printf("Kartlar:"); for (int i = 0; i < VW.nk; i++) printf(" [%s: %s (stil %d)]", VW.klab[i], VW.kval[i], VW.kstyle[i]); putchar('\n'); }
    printf("Düğmeler:"); for (int i = 0; i < VW.nb; i++) printf(" [%s%s]", VW.btn[i], VW.needrow[i] ? "*" : ""); putchar('\n');
    for (int i = 0; i < VW.t.nc; i++) printf("%s%s", i ? " | " : "", VW.t.h[i]); putchar('\n');
    for (int r = 0; r < VW.t.nr && r < 25; r++) { printf("(%d) ", VW.t.id[r]); for (int i = 0; i < VW.t.nc; i++) printf("%s%s", i ? " | " : "", VW.t.c[r][i]); putchar('\n'); }
}
static int file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }
static void chk(const char *name, int ok) { printf("%s  %s\n", ok ? "TAMAM" : "HATA ", name); if (!ok) fk_fail = 1; }
static void run_cmd(char *line) {
    if (!strncmp(line, "@page ", 6)) { core_goto(atoi(line + 6)); print_view(); }
    else if (!strncmp(line, "@act ", 5)) { char nm[48]; int row = 0, id = -1; sscanf(line + 5, "%47s %d", nm, &row); for (int i = 0; ACTNAME[i]; i++) if (!strcmp(ACTNAME[i], nm)) id = i; if (id < 0) { printf("!! bilinmeyen eylem %s\n", nm); fk_fail = 1; } else { act(id, row); build_view(); } }
    else if (!strcmp(line, "@show")) print_view();
    else if (!strcmp(line, "@exit")) core_can_exit();
    else if (!strncmp(line, "@exists ", 8)) chk(line + 8, file_exists(line + 8));
    else if (!strncmp(line, "@rm ", 4)) remove(line + 4);
    else if (!strncmp(line, "@chk ", 5)) {   /* @chk etiket|beklenen|SQL */
        char *a = strchr(line + 5, '|'); char *b = a ? strchr(a + 1, '|') : NULL; if (!b) { puts("!! @chk biçimi"); fk_fail = 1; return; }
        *a = 0; *b = 0; double got = val("%s", b + 1), want = atof(a + 1); char m[300]; snprintf(m, 300, "%s (beklenen %g, bulunan %g)", line + 5, want, got); chk(m, fabs(got - want) < 0.0001);
    }
    else if (!strncmp(line, "@chkmsg ", 8)) { puts("(not)"); }
    else { printf("!! bilinmeyen komut %s\n", line); fk_fail = 1; }
    ans_clear();
}
static int run_script(const char *script) {
    char *buf = strdup(script), *save = buf; fk_fail = 0;
    for (char *ln = buf; ln && *ln != '\1';) {
        char *nl = strchr(ln, '\n'); if (nl) *nl = 0; size_t l = strlen(ln); if (l && ln[l - 1] == '\r') ln[l - 1] = 0;
        if (ln[0] == '#') {} else if (ln[0] == '@') { printf("\n>> %s\n", ln); run_cmd(ln); } else ans_push(ln);
        if (!nl) break; ln = nl + 1;
    }
    free(save); ans_clear(); return fk_fail;
}
#endif
