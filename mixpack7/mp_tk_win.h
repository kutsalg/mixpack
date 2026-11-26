/* mp_tk_win.h - Win32 araç takımı (yalnızca belgelenmiş standart API'ler + GDI ile çizim).
 * Görünüm: lacivert yan menü, başlık bandı, renkli düğmeler (BS_OWNERDRAW), özet kartları, renkli liste satırları (NM_CUSTOMDRAW).
 * Çekirdek (mp_core.h) tk_* çağırır; burada gerçek pencerelere çevrilir (wk_*). Metinler UTF-8 <-> UTF-16 dönüştürülür.
 * Test kolaylığı: uitest_ms > 0 iken iletişim kutuları kendiliğinden kapanır ve mesajlar günlüğe yazılır (--uitest). */
#ifndef MP_TK_WIN_H
#define MP_TK_WIN_H
#define MP_TK_VERSION 6
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#define C_BG RGB(238, 241, 246)
#define C_NAVY RGB(22, 48, 78)
#define C_SEL RGB(38, 78, 130)
#define C_ACC RGB(26, 100, 214)
#define C_GREEN RGB(20, 119, 74)
#define C_RED RGB(196, 43, 43)
#define C_ORANGE RGB(178, 95, 0)
#define C_INK RGB(27, 36, 50)
#define C_MUT RGB(86, 97, 117)
#define C_LINE RGB(213, 219, 229)
#define NAVW 230
#define BANDH 78

static HINSTANCE hInst; static HWND hMain, hNav, hHead, hTbl, hCards, hBtn[16]; static int nBtn, dpi = 96, uitest_ms; static FILE *ulog;
static HFONT fN, fB, fBig, fSm, fKpi, fNav, fNavB, fLogo, fHdr, fMono; static HBRUSH hbrBg, hbrNavy, hbrSel, hbrWhite, hbrAcc; static HIMAGELIST hIL;
static int S(int v) { return MulDiv(v, dpi, 96); }
static wchar_t *W(const char *s) { static wchar_t b[16][2048]; static int k; wchar_t *o = b[k = (k + 1) & 15]; MultiByteToWideChar(CP_UTF8, 0, s ? s : "", -1, o, 2048); o[2047] = 0; return o; }
static char *U8(const wchar_t *w) { static char b[16][4096]; static int k; char *o = b[k = (k + 1) & 15]; WideCharToMultiByte(CP_UTF8, 0, w, -1, o, 4096, NULL, NULL); o[4095] = 0; return o; }
static void ulogf(const char *f, ...) { if (!ulog) return; va_list a; va_start(a, f); vfprintf(ulog, f, a); va_end(a); fflush(ulog); }
static HFONT mkfont(const wchar_t *face, int pt, int weight) { return CreateFontW(-MulDiv(pt, dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face); }
static void setfont(HWND h, HFONT f) { SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE); }
static HWND owner_win(void) { HWND a = GetActiveWindow(); return a ? a : hMain; }
static void center_on(HWND w, HWND owner) {
    RECT r, o; GetWindowRect(w, &r); int ww = r.right - r.left, hh = r.bottom - r.top;
    if (owner && IsWindowVisible(owner) && !IsIconic(owner)) GetWindowRect(owner, &o); else { o.left = 0; o.top = 0; o.right = GetSystemMetrics(SM_CXSCREEN); o.bottom = GetSystemMetrics(SM_CYSCREEN); }
    int x = o.left + ((o.right - o.left) - ww) / 2, y = o.top + ((o.bottom - o.top) - hh) / 2; if (x < 0) x = 0; if (y < 0) y = 0; SetWindowPos(w, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}
static COLORREF scol(int st) { switch (st) { case 1: return C_ACC; case 2: return C_GREEN; case 3: return C_RED; case 4: return C_ORANGE; default: return C_INK; } }
static COLORREF darker(COLORREF c, int d) { int r = GetRValue(c) - d, g = GetGValue(c) - d, b = GetBValue(c) - d; if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0; return RGB(r, g, b); }

/* ---------- renkli düğme çizimi (BS_OWNERDRAW) ---------- */
static void btn_draw(const DRAWITEMSTRUCT *d, int st) {
    RECT rc = d->rcItem; HDC dc = d->hDC; int dis = (d->itemState & ODS_DISABLED) != 0, sel = (d->itemState & ODS_SELECTED) != 0; COLORREF fill, border, txt;
    if (dis) { fill = RGB(226, 230, 237); border = RGB(210, 215, 224); txt = RGB(150, 158, 172); }
    else if (st == 0) { fill = sel ? RGB(214, 226, 246) : RGB(255, 255, 255); border = RGB(150, 164, 188); txt = C_INK; }
    else { COLORREF base = scol(st); fill = sel ? darker(base, 40) : base; border = darker(base, 30); txt = RGB(255, 255, 255); }
    FillRect(dc, &rc, hbrBg);
    HBRUSH br = CreateSolidBrush(fill); HPEN pn = CreatePen(PS_SOLID, 1, border); HGDIOBJ ob = SelectObject(dc, br), op = SelectObject(dc, pn);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, S(12), S(12)); SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(br); DeleteObject(pn);
    wchar_t t[200]; GetWindowTextW(d->hwndItem, t, 200); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, txt); SelectObject(dc, fB); DrawTextW(dc, t, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if ((d->itemState & ODS_FOCUS) && !dis) { RECT f = rc; f.left += S(5); f.top += S(5); f.right -= S(5); f.bottom -= S(5); DrawFocusRect(dc, &f); }
}

/* ---------- ListView: doldurma, satır yüksekliği, renkli hücreler ---------- */
static void lv_fill(HWND lv, Table *t) {
    SendMessageW(lv, WM_SETREDRAW, FALSE, 0); ListView_DeleteAllItems(lv); while (ListView_DeleteColumn(lv, 0)) {}
    for (int i = 0; i < t->nc; i++) {
        int mx = ulen(t->h[i]) + 2; for (int r = 0; r < t->nr && r < 300; r++) { int l = ulen(t->c[r][i]); if (l > mx) mx = l; }
        int w = S(10) * mx + S(28); if (w < S(90)) w = S(90); if (w > S(460)) w = S(460);
        LVCOLUMNW c; memset(&c, 0, sizeof c); c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT; c.fmt = (t->right[i] && i > 0) ? LVCFMT_RIGHT : LVCFMT_LEFT; c.pszText = W(t->h[i]); c.cx = w;
        SendMessageW(lv, LVM_INSERTCOLUMNW, i, (LPARAM)&c);
    }
    for (int r = 0; r < t->nr; r++) {
        LVITEMW it; memset(&it, 0, sizeof it); it.mask = LVIF_TEXT | LVIF_PARAM; it.iItem = r; it.pszText = W(t->nc ? t->c[r][0] : ""); it.lParam = t->id[r];
        SendMessageW(lv, LVM_INSERTITEMW, 0, (LPARAM)&it);
        for (int i = 1; i < t->nc; i++) { LVITEMW s; memset(&s, 0, sizeof s); s.iSubItem = i; s.pszText = W(t->c[r][i]); SendMessageW(lv, LVM_SETITEMTEXTW, r, (LPARAM)&s); }
    }
    SendMessageW(lv, WM_SETREDRAW, TRUE, 0); InvalidateRect(lv, NULL, TRUE);
}
static HWND make_lv(HWND parent, int id) {
    HWND lv = CreateWindowExW(WS_EX_CLIENTEDGE, L"SysListView32", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, hInst, NULL);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER); setfont(lv, fN); if (hIL) ListView_SetImageList(lv, hIL, LVSIL_SMALL); return lv;
}
static void cell_style(Table *t, int row, int col, COLORREF *clr, int *bold) {
    *clr = C_INK; *bold = 0; if (!t || row < 0 || row >= t->nr || col < 0 || col >= t->nc) return;
    const char *x = t->c[row][col], *h = t->h[col];
    if (!strncmp(x, "ÖDENDİ", strlen("ÖDENDİ"))) { *clr = C_GREEN; *bold = 1; }
    else if (!strncmp(x, "ÖDENMEDİ", strlen("ÖDENMEDİ"))) { *clr = C_ORANGE; *bold = 1; }
    else if (!strncmp(x, "İPTAL", strlen("İPTAL")) || !strncmp(x, "PASİF", strlen("PASİF")) || !strncmp(x, "STOK YOK", strlen("STOK YOK"))) { *clr = C_RED; *bold = 1; }
    else if (!strncmp(x, "KRİTİK", strlen("KRİTİK"))) { *clr = C_ORANGE; *bold = 1; }
    else if (!strncmp(h, "Bakiye (", 8) && *x) { if (x[0] == '-') { *clr = C_GREEN; *bold = 1; } else if (strcmp(x, "0,00 €")) { *clr = C_RED; *bold = 1; } }
    else if (!strcmp(h, "Bakiye") && x[0] == '-') { *clr = C_RED; *bold = 1; }
    else if (!strcmp(h, "Stok") && x[0] == '-') { *clr = C_RED; *bold = 1; }
    else if (!strcmp(h, "Giriş") && strcmp(x, "0,00 €")) { *clr = C_GREEN; *bold = 1; }
    else if (!strcmp(h, "Çıkış") && strcmp(x, "0,00 €")) { *clr = C_RED; *bold = 1; }
    else if (!strcmp(h, "Bakiye") || !strcmp(h, "Tutar") || !strcmp(h, "Toplam")) *bold = 1;
}
static LRESULT lv_customdraw(LPARAM l, Table *t) {
    NMLVCUSTOMDRAW *cd = (NMLVCUSTOMDRAW *)l; int row = (int)cd->nmcd.dwItemSpec;
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT: return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT: if (!(cd->nmcd.uItemState & CDIS_SELECTED)) cd->clrTextBk = (row & 1) ? RGB(243, 247, 252) : RGB(255, 255, 255); return CDRF_NOTIFYSUBITEMDRAW;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM: {
        COLORREF c; int b; cell_style(t, row, cd->iSubItem, &c, &b);
        if (!(cd->nmcd.uItemState & CDIS_SELECTED)) { cd->clrText = c; cd->clrTextBk = (row & 1) ? RGB(243, 247, 252) : RGB(255, 255, 255); }
        SelectObject(cd->nmcd.hdc, b ? fB : fN); return CDRF_NEWFONT; }
    }
    return CDRF_DODEFAULT;
}

/* ---------- mesaj kutuları ---------- */
static void wk_msg(const char *title, const char *text) {
    if (uitest_ms) { ulogf("[MSG] %s: %s\n", title, text); return; }
    MessageBoxW(owner_win(), W(text), W(title), MB_OK | (strcmp(title, "Hata") == 0 ? MB_ICONERROR : strcmp(title, "Uyarı") == 0 || strcmp(title, "Dikkat") == 0 ? MB_ICONWARNING : MB_ICONINFORMATION));
}
static int wk_confirm(const char *title, const char *text) {
    if (uitest_ms) { ulogf("[SORU] %s: %s (otomatik: hayır)\n", title, text); return 0; }
    return MessageBoxW(owner_win(), W(text), W(title), MB_YESNO | MB_ICONQUESTION) == IDYES;
}
static void wk_open(const char *path) {
    if (uitest_ms) { ulogf("[AÇ] %s\n", path); return; }
    if ((INT_PTR)ShellExecuteW(NULL, L"open", W(path), NULL, NULL, SW_SHOWNORMAL) <= 32) wk_msg("Hata", "Dosya açılamadı. Dosya program klasöründe duruyor; elle açabilirsiniz.");
}
static void modal_loop(HWND dlg, volatile int *done) {
    MSG msg; HWND own = GetWindow(dlg, GW_OWNER);
    if (own) EnableWindow(own, FALSE);
    while (!*done) { int r = (int)GetMessageW(&msg, NULL, 0, 0); if (r <= 0) { if (r == 0) PostQuitMessage((int)msg.wParam); break; } if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); } }
    if (own) EnableWindow(own, TRUE);
}
static LRESULT dlg_color(UINT m, WPARAM w, LPARAM l, HWND hdr) {
    (void)m; HDC dc = (HDC)w;
    if ((HWND)l == hdr) { SetBkColor(dc, C_NAVY); SetTextColor(dc, RGB(255, 255, 255)); return (LRESULT)hbrNavy; }
    SetBkColor(dc, C_BG); SetTextColor(dc, C_INK); return (LRESULT)hbrBg;
}

/* ---------- form penceresi ---------- */
typedef struct { Form *f; HWND ctl[MAXF], hdr; int done, ok; } FormCtx;
static LRESULT CALLBACK FormProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) { SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)l)->lpCreateParams); return DefWindowProcW(h, m, w, l); }
    FormCtx *c = (FormCtx *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (c) {
        if (m == WM_DRAWITEM) { const DRAWITEMSTRUCT *d = (const DRAWITEMSTRUCT *)l; btn_draw(d, d->CtlID == IDOK ? 2 : 0); return TRUE; }
        if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLORBTN) return dlg_color(m, w, l, c->hdr);
        if (m == WM_USER) return MAKELRESULT(IDOK, 0x534B);   /* DM_GETDEFID: Enter = Tamam */
        if (m == WM_COMMAND) {
            if (LOWORD(w) == IDOK) {
                for (int i = 0; i < c->f->n; i++) {
                    Field *d = &c->f->f[i]; if (d->type == F_NOTE) continue;
                    if (d->type == F_TEXT || d->type == F_DATE) { wchar_t b[700]; GetWindowTextW(c->ctl[i], b, 700); snprintf(d->val, 300, "%s", U8(b)); }
                    else if (d->type == F_SELECT) { int ix = (int)SendMessageW(c->ctl[i], CB_GETCURSEL, 0, 0); if (ix >= 0 && ix < d->nopt) snprintf(d->val, 300, "%s", d->oid[ix]); }
                    else if (d->type == F_CHECK) snprintf(d->val, 300, "%s", SendMessageW(c->ctl[i], BM_GETCHECK, 0, 0) == BST_CHECKED ? "1" : "0");
                }
                c->ok = 1; c->done = 1; return 0;
            }
            if (LOWORD(w) == IDCANCEL) { c->done = 1; return 0; }
        }
        if (m == WM_CLOSE) { c->done = 1; return 0; }
        if (m == WM_TIMER) { KillTimer(h, 1); PostMessageW(h, WM_COMMAND, IDOK, 0); return 0; }
    }
    return DefWindowProcW(h, m, w, l);
}
static int wk_form(Form *f) {
    FormCtx c; memset(&c, 0, sizeof c); c.f = f; HWND own = owner_win();
    int cols = f->n > 7 ? 2 : 1, cw = S(540), pad = S(20), per = (f->n + cols - 1) / cols, top = S(60), y[2] = {top + S(12), top + S(12)};
    DWORD st = WS_POPUP | WS_CAPTION | WS_SYSMENU, ex = WS_EX_DLGMODALFRAME;
    HWND hd = CreateWindowExW(ex, L"MPForm", W(f->title), st, 0, 0, 100, 100, own, NULL, hInst, &c);
    if (!hd) { ulogf("HATA: form penceresi oluşturulamadı (%lu)\n", GetLastError()); return 0; }
    c.hdr = CreateWindowExW(0, L"STATIC", W(f->title), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 100, top, hd, NULL, hInst, NULL); setfont(c.hdr, fHdr);
    for (int i = 0; i < f->n; i++) {
        Field *d = &f->f[i]; int col = i / per, x = pad + col * (cw + pad), yy = y[col];
        if (d->type == F_NOTE) {
            int lines = (int)strlen(d->label) / ((cw / S(8)) > 10 ? (cw / S(8)) : 10) + 1; int hh = lines * S(24) + S(8);
            c.ctl[i] = CreateWindowExW(0, L"STATIC", W(d->label), WS_CHILD | WS_VISIBLE | SS_LEFT, x, yy, cw, hh, hd, NULL, hInst, NULL); setfont(c.ctl[i], fN); y[col] = yy + hh + S(8); continue;
        }
        if (d->type == F_CHECK) {
            c.ctl[i] = CreateWindowExW(0, L"BUTTON", W(d->label), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, x, yy, cw, S(32), hd, (HMENU)(INT_PTR)(200 + i), hInst, NULL); setfont(c.ctl[i], fN);
            SendMessageW(c.ctl[i], BM_SETCHECK, d->val[0] == '1' ? BST_CHECKED : BST_UNCHECKED, 0); y[col] = yy + S(40); continue;
        }
        HWND lb = CreateWindowExW(0, L"STATIC", W(d->label), WS_CHILD | WS_VISIBLE | SS_LEFT, x, yy, cw, S(24), hd, NULL, hInst, NULL); setfont(lb, fB);
        if (d->type == F_SELECT) {
            c.ctl[i] = CreateWindowExW(0, L"COMBOBOX", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, x, yy + S(26), cw, S(300), hd, (HMENU)(INT_PTR)(200 + i), hInst, NULL); setfont(c.ctl[i], fN);
            int sel = 0; for (int k = 0; k < d->nopt; k++) { SendMessageW(c.ctl[i], CB_ADDSTRING, 0, (LPARAM)W(d->olab[k])); if (!strcmp(d->oid[k], d->val)) sel = k; }
            SendMessageW(c.ctl[i], CB_SETCURSEL, sel, 0);
        } else {
            c.ctl[i] = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", W(d->val), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, x, yy + S(26), cw, S(34), hd, (HMENU)(INT_PTR)(200 + i), hInst, NULL); setfont(c.ctl[i], fN);
        }
        y[col] = yy + S(26) + S(34) + S(14);
    }
    int ymax = y[0] > y[1] ? y[0] : y[1], clientW = pad + cols * (cw + pad), clientH = ymax + S(66);
    MoveWindow(c.hdr, 0, 0, clientW, top, TRUE);
    HWND bo = CreateWindowExW(0, L"BUTTON", L"✔ Tamam", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, clientW - pad - S(280), ymax + S(6), S(135), S(46), hd, (HMENU)(INT_PTR)IDOK, hInst, NULL); setfont(bo, fB);
    HWND bc = CreateWindowExW(0, L"BUTTON", L"Vazgeç", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, clientW - pad - S(135), ymax + S(6), S(135), S(46), hd, (HMENU)(INT_PTR)IDCANCEL, hInst, NULL); setfont(bc, fB);
    RECT r = {0, 0, clientW, clientH}; AdjustWindowRectEx(&r, st, FALSE, ex); SetWindowPos(hd, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOMOVE);
    center_on(hd, own); ShowWindow(hd, SW_SHOW); UpdateWindow(hd);
    for (int i = 0; i < f->n; i++) if (f->f[i].type != F_NOTE) { SetFocus(c.ctl[i]); break; }
    if (uitest_ms) SetTimer(hd, 1, uitest_ms, NULL);
    modal_loop(hd, &c.done); DestroyWindow(hd); if (own) SetForegroundWindow(own); return c.ok;
}

/* ---------- tablo penceresi ---------- */
typedef struct { HWND hdr, lv, foot, bp, bc; Table *t; int done, print; } TblCtx;
static void tbl_layout(HWND h, TblCtx *c) {
    RECT rc; GetClientRect(h, &rc); int pad = S(14), top = S(60), W0 = rc.right, H0 = rc.bottom;
    MoveWindow(c->hdr, 0, 0, W0, top, TRUE); MoveWindow(c->lv, pad, top + pad, W0 - 2 * pad, H0 - top - pad - S(150), TRUE); MoveWindow(c->foot, pad, H0 - S(130), W0 - 2 * pad, S(56), TRUE);
    MoveWindow(c->bc, W0 - pad - S(140), H0 - S(68), S(140), S(48), TRUE); if (c->bp) MoveWindow(c->bp, W0 - pad - S(140) - S(10) - S(220), H0 - S(68), S(220), S(48), TRUE);
}
static LRESULT CALLBACK TblProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_NCCREATE) { SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)l)->lpCreateParams); return DefWindowProcW(h, m, w, l); }
    TblCtx *c = (TblCtx *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (c) {
        if (m == WM_DRAWITEM) { const DRAWITEMSTRUCT *d = (const DRAWITEMSTRUCT *)l; btn_draw(d, d->CtlID == 302 ? 2 : 0); return TRUE; }
        if (m == WM_CTLCOLORSTATIC) return dlg_color(m, w, l, c->hdr);
        if (m == WM_NOTIFY && ((NMHDR *)l)->hwndFrom == c->lv && ((NMHDR *)l)->code == NM_CUSTOMDRAW) return lv_customdraw(l, c->t);
        if (m == WM_USER) return MAKELRESULT(IDCANCEL, 0x534B);
        if (m == WM_COMMAND) { if (LOWORD(w) == IDCANCEL) { c->done = 1; return 0; } if (LOWORD(w) == 302) { c->print = 1; c->done = 1; return 0; } }
        if (m == WM_CLOSE) { c->done = 1; return 0; }
        if (m == WM_SIZE && c->lv) { tbl_layout(h, c); return 0; }
        if (m == WM_GETMINMAXINFO) { MINMAXINFO *mi = (MINMAXINFO *)l; mi->ptMinTrackSize.x = S(680); mi->ptMinTrackSize.y = S(480); return 0; }
        if (m == WM_TIMER) { KillTimer(h, 1); PostMessageW(h, WM_COMMAND, IDCANCEL, 0); return 0; }
    }
    return DefWindowProcW(h, m, w, l);
}
static int wk_table(const char *title, Table *t, const char *foot, int printable) {
    TblCtx c; memset(&c, 0, sizeof c); c.t = t; HWND own = owner_win(); DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX;
    HWND hd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MPTable", W(title), st, 0, 0, S(1040), S(680), own, NULL, hInst, &c);
    if (!hd) { ulogf("HATA: tablo penceresi oluşturulamadı (%lu)\n", GetLastError()); return 0; }
    c.hdr = CreateWindowExW(0, L"STATIC", W(title), WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE, 0, 0, 10, 10, hd, NULL, hInst, NULL); setfont(c.hdr, fHdr);
    c.lv = make_lv(hd, 300); lv_fill(c.lv, t);
    c.foot = CreateWindowExW(0, L"STATIC", W(foot ? foot : ""), WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 10, 10, hd, (HMENU)(INT_PTR)301, hInst, NULL); setfont(c.foot, fB);
    if (printable) { c.bp = CreateWindowExW(0, L"BUTTON", L"Yazdır / PDF", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, hd, (HMENU)(INT_PTR)302, hInst, NULL); setfont(c.bp, fB); }
    c.bc = CreateWindowExW(0, L"BUTTON", L"Kapat", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, hd, (HMENU)(INT_PTR)IDCANCEL, hInst, NULL); setfont(c.bc, fB);
    tbl_layout(hd, &c); center_on(hd, own); ShowWindow(hd, SW_SHOW); UpdateWindow(hd);
    if (uitest_ms) SetTimer(hd, 1, uitest_ms, NULL);
    modal_loop(hd, &c.done); DestroyWindow(hd); if (own) SetForegroundWindow(own); return c.print;
}

/* ---------- özet kartları ---------- */
static int cards_cols(int w) { int c = w / S(250); if (c < 2) c = 2; if (VW.nk && c > VW.nk) c = VW.nk; return c < 1 ? 1 : c; }
static int cards_total(int w) { if (!VW.nk) return 0; int cols = cards_cols(w), rows = (VW.nk + cols - 1) / cols; return rows * (S(96) + S(12)) - S(12); }
static void paint_cards(HWND h, HDC dc) {
    RECT rc; GetClientRect(h, &rc); int W0 = rc.right, cols = cards_cols(W0), gap = S(12), cw = (W0 - (cols - 1) * gap) / cols, ch = S(96);
    HPEN pen = CreatePen(PS_SOLID, 1, C_LINE); HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, hbrWhite); SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < VW.nk; i++) {
        int x = (i % cols) * (cw + gap), y = (i / cols) * (ch + gap), st = VW.kstyle[i];
        RoundRect(dc, x, y, x + cw, y + ch, S(14), S(14));
        HBRUSH br = CreateSolidBrush(scol(st)); RECT bar = {x + S(2), y + S(16), x + S(8), y + ch - S(16)}; FillRect(dc, &bar, br); DeleteObject(br);
        SelectObject(dc, fSm); SetTextColor(dc, C_MUT); RECT rl = {x + S(22), y + S(12), x + cw - S(10), y + S(40)}; DrawTextW(dc, W(VW.klab[i]), -1, &rl, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, fKpi); SetTextColor(dc, scol(st)); RECT rv = {x + S(22), y + S(40), x + cw - S(10), y + ch - S(8)}; DrawTextW(dc, W(VW.kval[i]), -1, &rv, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }
    SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(pen);
}
static LRESULT CALLBACK CardsProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) { RECT rc; GetClientRect(h, &rc); FillRect((HDC)w, &rc, hbrBg); return 1; }
    if (m == WM_PAINT) { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); paint_cards(h, dc); EndPaint(h, &ps); return 0; }
    return DefWindowProcW(h, m, w, l);
}

/* ---------- ana pencere ---------- */
static int sel_id(void) { int s = ListView_GetNextItem(hTbl, -1, LVNI_SELECTED); return (s >= 0 && s < VW.t.nr) ? VW.t.id[s] : 0; }
static void update_btns(void) { int sel = ListView_GetNextItem(hTbl, -1, LVNI_SELECTED); for (int i = 0; i < nBtn; i++) EnableWindow(hBtn[i], !VW.needrow[i] || sel >= 0); }
static void layout(void) {
    if (!hTbl) return; RECT rc; GetClientRect(hMain, &rc); int W0 = rc.right, H0 = rc.bottom, nav = S(NAVW), band = S(BANDH), pad = S(16), x0 = nav + pad, w = W0 - nav - 2 * pad, y = band + pad, bh = S(46), gap = S(10), x = x0;
    MoveWindow(hNav, 0, band, nav, H0 - band, TRUE);
    HDC dc = GetDC(hMain); HGDIOBJ old = SelectObject(dc, fB);
    for (int i = 0; i < nBtn; i++) {
        wchar_t b[200]; GetWindowTextW(hBtn[i], b, 200); SIZE sz; GetTextExtentPoint32W(dc, b, (int)wcslen(b), &sz); int bw = sz.cx + S(40);
        if (x + bw > x0 + w && x > x0) { x = x0; y += bh + gap; } MoveWindow(hBtn[i], x, y, bw, bh, TRUE); x += bw + gap;
    }
    SelectObject(dc, old); ReleaseDC(hMain, dc); if (nBtn) y += bh + gap + S(4);
    if (VW.nk) { int chh = cards_total(w); ShowWindow(hCards, SW_SHOW); MoveWindow(hCards, x0, y, w, chh, TRUE); InvalidateRect(hCards, NULL, TRUE); y += chh + gap + S(4); } else ShowWindow(hCards, SW_HIDE);
    int hh = 0; if (VW.head && *VW.head) { int lines = 1; for (const char *p = VW.head; *p; p++) if (*p == '\n') lines++; hh = lines * S(24) + S(14); int cap = (H0 - y) * 55 / 100; if (hh > cap) hh = cap; }
    ShowWindow(hHead, hh ? SW_SHOW : SW_HIDE); if (hh) MoveWindow(hHead, x0, y, w, hh, TRUE); y += hh + (hh ? gap : 0);
    MoveWindow(hTbl, x0, y, w, H0 - y - pad > S(80) ? H0 - y - pad : S(80), TRUE);
}
static void render_view(void) {
    char t[200]; core_title(t, 200); SetWindowTextW(hMain, W(t)); SendMessageW(hNav, LB_SETCURSEL, cur_page, 0);
    for (int i = 0; i < nBtn; i++) DestroyWindow(hBtn[i]); nBtn = 0;
    for (int i = 0; i < VW.nb; i++) { hBtn[i] = CreateWindowExW(0, L"BUTTON", W(VW.btn[i]), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, hMain, (HMENU)(INT_PTR)(1000 + i), hInst, NULL); setfont(hBtn[i], fB); }
    nBtn = VW.nb;
    { int n = MultiByteToWideChar(CP_UTF8, 0, VW.head ? VW.head : "", -1, NULL, 0); wchar_t *wh = malloc(sizeof(wchar_t) * (n + 2)); MultiByteToWideChar(CP_UTF8, 0, VW.head ? VW.head : "", -1, wh, n + 1); wh[n] = 0; SetWindowTextW(hHead, wh); free(wh); }
    lv_fill(hTbl, &VW.t); layout(); update_btns(); InvalidateRect(hMain, NULL, TRUE);
}
static void paint_main(HWND h) {
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT rc; GetClientRect(h, &rc); int nav = S(NAVW), band = S(BANDH);
    RECT r1 = {0, 0, nav, rc.bottom}; FillRect(dc, &r1, hbrNavy);
    RECT r2 = {nav, 0, rc.right, band}; FillRect(dc, &r2, hbrWhite); RECT r3 = {nav, band - S(3), rc.right, band}; FillRect(dc, &r3, hbrAcc);
    SetBkMode(dc, TRANSPARENT);
    SelectObject(dc, fLogo); SetTextColor(dc, RGB(255, 255, 255)); RECT rl = {S(22), S(12), nav, S(48)}; DrawTextW(dc, L"MixPack", -1, &rl, DT_LEFT | DT_SINGLELINE);
    SelectObject(dc, fSm); SetTextColor(dc, RGB(150, 178, 218)); RECT rs = {S(23), S(46), nav, S(72)}; DrawTextW(dc, W("Muhasebe · offline"), -1, &rs, DT_LEFT | DT_SINGLELINE);
    SelectObject(dc, fBig); SetTextColor(dc, C_NAVY); RECT rt = {nav + S(22), S(8), rc.right - S(200), S(44)}; DrawTextW(dc, W(PAGE[cur_page]), -1, &rt, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, fSm); SetTextColor(dc, C_MUT); RECT rd = {nav + S(23), S(44), rc.right - S(200), band - S(6)}; DrawTextW(dc, W(PDESC[cur_page]), -1, &rd, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);
    char yb[40]; snprintf(yb, 40, "Dönem: %d", year); RECT rb = {rc.right - S(180), S(18), rc.right - S(22), S(54)};
    HBRUSH bb = CreateSolidBrush(C_ACC); HPEN bp = CreatePen(PS_SOLID, 1, C_ACC); HGDIOBJ ob = SelectObject(dc, bb), op = SelectObject(dc, bp); RoundRect(dc, rb.left, rb.top, rb.right, rb.bottom, S(16), S(16)); SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(bb); DeleteObject(bp);
    SelectObject(dc, fB); SetTextColor(dc, RGB(255, 255, 255)); DrawTextW(dc, W(yb), -1, &rb, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    EndPaint(h, &ps);
}
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        hMain = h;
        hNav = CreateWindowExW(0, L"LISTBOX", NULL, WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 0, 0, 10, 10, h, (HMENU)(INT_PTR)100, hInst, NULL); setfont(hNav, fNav);
        SendMessageW(hNav, LB_SETITEMHEIGHT, 0, S(48)); for (int i = 0; i < NPAGES; i++) SendMessageW(hNav, LB_ADDSTRING, 0, (LPARAM)W(PAGE[i]));
        hHead = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL, 0, 0, 10, 10, h, (HMENU)(INT_PTR)101, hInst, NULL); setfont(hHead, fMono);
        hCards = CreateWindowExW(0, L"MPCards", L"", WS_CHILD, 0, 0, 10, 10, h, (HMENU)(INT_PTR)103, hInst, NULL);
        hTbl = make_lv(h, 102);
        return 0;
    case WM_PAINT: paint_main(h); return 0;
    case WM_MEASUREITEM: { MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)l; if (mi->CtlType == ODT_LISTBOX) { mi->itemHeight = S(48); return TRUE; } break; }
    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT *d = (const DRAWITEMSTRUCT *)l;
        if (d->CtlType == ODT_BUTTON) { int bi = (int)d->CtlID - 1000; btn_draw(d, bi >= 0 && bi < VW.nb ? VW.style[bi] : 0); return TRUE; }
        if (d->CtlType == ODT_LISTBOX) {
            if ((int)d->itemID < 0 || (int)d->itemID >= NPAGES) return TRUE;
            int sel = (d->itemState & ODS_SELECTED) != 0; RECT rc = d->rcItem; FillRect(d->hDC, &rc, sel ? hbrSel : hbrNavy);
            if (sel) { RECT bar = {rc.left, rc.top, rc.left + S(6), rc.bottom}; FillRect(d->hDC, &bar, hbrAcc); }
            SetBkMode(d->hDC, TRANSPARENT); SetTextColor(d->hDC, sel ? RGB(255, 255, 255) : RGB(196, 212, 236)); SelectObject(d->hDC, sel ? fNavB : fNav);
            RECT tr = rc; tr.left += S(24); DrawTextW(d->hDC, W(PAGE[d->itemID]), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE); return TRUE;
        }
        break; }
    case WM_COMMAND: {
        int id = LOWORD(w), code = HIWORD(w);
        if (id == 100 && code == LBN_SELCHANGE) { int s = (int)SendMessageW(hNav, LB_GETCURSEL, 0, 0); if (s >= 0) { core_goto(s); render_view(); } }
        else if (id >= 1000 && id < 1016) { core_button(id - 1000, sel_id()); render_view(); }
        return 0; }
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)l;
        if (nh->hwndFrom == hTbl) {
            if (nh->code == NM_CUSTOMDRAW) return lv_customdraw(l, &VW.t);
            if (nh->code == NM_DBLCLK) { core_open_row(sel_id()); render_view(); }
            else if (nh->code == LVN_ITEMCHANGED) update_btns();
            else if (nh->code == LVN_KEYDOWN && ((NMLVKEYDOWN *)l)->wVKey == VK_RETURN) { core_open_row(sel_id()); render_view(); }
        }
        return 0; }
    case WM_CTLCOLORLISTBOX: SetBkColor((HDC)w, C_NAVY); return (LRESULT)hbrNavy;
    case WM_CTLCOLORSTATIC: if ((HWND)l == hHead) { SetBkColor((HDC)w, RGB(255, 255, 255)); SetTextColor((HDC)w, RGB(20, 30, 50)); return (LRESULT)hbrWhite; } break;
    case WM_SIZE: layout(); InvalidateRect(h, NULL, TRUE); return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO *mi = (MINMAXINFO *)l; mi->ptMinTrackSize.x = S(980); mi->ptMinTrackSize.y = S(620); return 0; }
    case WM_CLOSE: if (core_can_exit()) DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static int tk_init(HINSTANCE hI) {
    hInst = hI;
    typedef BOOL(WINAPI * DpiFn)(void); HMODULE u = GetModuleHandleW(L"user32.dll"); DpiFn fn = u ? (DpiFn)GetProcAddress(u, "SetProcessDPIAware") : NULL; if (fn) fn();
    HDC dc = GetDC(NULL); dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(NULL, dc); if (dpi < 96) dpi = 96;
    INITCOMMONCONTROLSEX ic; ic.dwSize = sizeof ic; ic.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES; InitCommonControlsEx(&ic);
    fN = mkfont(L"Segoe UI", 11, FW_NORMAL); fB = mkfont(L"Segoe UI", 11, FW_BOLD); fBig = mkfont(L"Segoe UI", 20, FW_BOLD); fSm = mkfont(L"Segoe UI", 10, FW_NORMAL); fKpi = mkfont(L"Segoe UI", 22, FW_BOLD);
    fNav = mkfont(L"Segoe UI", 12, FW_NORMAL); fNavB = mkfont(L"Segoe UI", 12, FW_BOLD); fLogo = mkfont(L"Segoe UI", 22, FW_BOLD); fHdr = mkfont(L"Segoe UI", 15, FW_BOLD); fMono = mkfont(L"Consolas", 12, FW_NORMAL);
    hbrBg = CreateSolidBrush(C_BG); hbrNavy = CreateSolidBrush(C_NAVY); hbrSel = CreateSolidBrush(C_SEL); hbrWhite = CreateSolidBrush(RGB(255, 255, 255)); hbrAcc = CreateSolidBrush(C_ACC);
    hIL = ImageList_Create(1, S(34), ILC_COLOR, 1, 1);
    WNDCLASSEXW wc; memset(&wc, 0, sizeof wc); wc.cbSize = sizeof wc; wc.hInstance = hI; wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.hIcon = LoadIconW(NULL, (LPCWSTR)IDI_APPLICATION);
    wc.lpfnWndProc = MainProc; wc.hbrBackground = hbrBg; wc.lpszClassName = L"MPMain"; if (!RegisterClassExW(&wc)) return 0;
    wc.lpfnWndProc = FormProc; wc.lpszClassName = L"MPForm"; if (!RegisterClassExW(&wc)) return 0;
    wc.lpfnWndProc = TblProc; wc.lpszClassName = L"MPTable"; if (!RegisterClassExW(&wc)) return 0;
    wc.lpfnWndProc = CardsProc; wc.hbrBackground = NULL; wc.lpszClassName = L"MPCards"; if (!RegisterClassExW(&wc)) return 0;
    return 1;
}

/* ---------- pencere sınaması (--uitest): tüm sayfaları, bir formu ve bir tabloyu gerçek pencerelerle dener ---------- */
static int run_uitest(void) {
    int fail = 0; ulog = fopen("mixpack_uitest.txt", "w"); uitest_ms = 250; ulogf("MixPack pencere sınaması\n");
    for (int p = 0; p < NPAGES; p++) {
        core_goto(p); render_view(); UpdateWindow(hMain); if (VW.nk) UpdateWindow(hCards); UpdateWindow(hTbl); HWND hdr = ListView_GetHeader(hTbl);
        int ok = ListView_GetItemCount(hTbl) == VW.t.nr && nBtn == VW.nb && (int)SendMessageW(hdr, HDM_GETITEMCOUNT, 0, 0) == VW.t.nc && IsWindowVisible(hTbl) && (IsWindowVisible(hCards) != 0) == (VW.nk > 0);
        ulogf("%s  sayfa %d (%s): %d düğme, %d kart, %d kolon, %d satır\n", ok ? "TAMAM" : "HATA ", p, PAGE[p], nBtn, VW.nk, VW.t.nc, VW.t.nr); if (!ok) fail = 1;
    }
    Form *f = form_new("Deneme formu"); const char *tr = "Şişli İğne ğüşiöç €";
    fadd(f, "t", "Metin", F_TEXT, tr); Field *s = fadd(f, "s", "Seçim", F_SELECT, "b"); fopt(s, "a", "Birinci"); fopt(s, "b", "İkinci"); fopt(s, "c", "Üçüncü"); fadd(f, "c", "Onay", F_CHECK, "1"); fadd(f, "d", "Tarih", F_DATE, "01.01.2026"); fadd(f, "n", "Not metni", F_NOTE, "");
    int r = wk_form(f); int ok = r == 1 && !strcmp(fv(f, "t"), tr) && !strcmp(fv(f, "s"), "b") && fv(f, "c")[0] == '1' && !strcmp(fv(f, "d"), "01.01.2026");
    ulogf("%s  form: pencere açıldı, Türkçe karakter ve seçimler geri okundu (r=%d, t=%s, s=%s)\n", ok ? "TAMAM" : "HATA ", r, fv(f, "t"), fv(f, "s")); if (!ok) fail = 1; form_free(f);
    Table t = {0}; table_query(&t, "SELECT 1 id,'ÖDENDİ' \"Durum\",'Şişli' \"Ad\",12345 \"Tutar_c\""); int pr = wk_table("Deneme tablosu", &t, "Alt bilgi satırı", 1); ok = (pr == 0); ulogf("%s  tablo penceresi açıldı ve kapandı\n", ok ? "TAMAM" : "HATA "); if (!ok) fail = 1; table_free(&t);
    ulogf("\n%s\n", fail ? "SONUÇ: BAŞARISIZ" : "SONUÇ: Tüm pencere denetimleri başarılı."); fclose(ulog); ulog = NULL; uitest_ms = 0;
    MessageBoxW(NULL, fail ? L"Pencere sınaması BAŞARISIZ. Ayrıntı: mixpack_uitest.txt" : L"Pencere sınaması başarılı. Ayrıntı: mixpack_uitest.txt", L"MixPack", MB_OK | (fail ? MB_ICONERROR : MB_ICONINFORMATION));
    return fail;
}
#endif
