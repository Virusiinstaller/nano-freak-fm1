/* SPDX-License-Identifier: GPL-3.0-only */
/* The Nano Freak screen painter (from the test bed, dsp/nf_ui.c): the same code the test page runs.
 * Included by nano_app.c. */
/* Nano Freak - FM-1 screen painter (240 x 240 RGB565).
 *
 * The control code (page or firmware) sets values and strings, then paints the screen
 * band by band with nf_ui_paint_band(). The same C runs in the test page (WebAssembly)
 * and on the device, so the page shows exactly what the FM-1 will show. */

#include "nano_arp_demo.h"
#define NFA_COUNT 14
#define NFA_REST 255
/* the arp screen's picture of the pattern (four keys held): the test bed's nf_arp_demo, tabulated */
static int nf_arp_demo(int pattern, int range, int length, int nkeys, uint8_t out[16], int *levels)
{
    const uint8_t *s = NANO_DEMO[pattern < 0 ? 0 : pattern > 13 ? 13 : pattern][(range < 1 ? 1 : range > 4 ? 4 : range) - 1][(length < 1 ? 1 : length > 16 ? 16 : length) - 1];
    int i;
    (void)nkeys;
    for (i = 0; i < 16; i++)
        out[i] = s[i];
    if (levels)
        *levels = 4 * (range < 1 ? 1 : range > 4 ? 4 : range);
    return 16;
}

#define cards nu_cards       /* (names the one compilation unit already has) */
#define curve nu_curve
#define str_len nu_str_len
#define v nu_v
#define wave nu_wave
enum {                        /* integer values */
  UI_GROUP,                   /* 0 OSC, 1 ENV, 2 FX, 3 LFO, 4 MATRIX, 5 ARP, 6 SEQ, 7 VOICE (SCL), 8 GLOBAL, 9 PRESET, 10 start-up splash */
  UI_SUB, UI_NSUB,            /* current sub page and how many there are */
  UI_N0, UI_N1, UI_N2, UI_N3, /* the four knob positions 0..100, or -1 when the knob is unused */
  UI_OCT, UI_POLY, UI_PLAY,   /* octave -3..3, voice mode 0 mono 1 unison 2 paraphonic 3 poly, 1 playing */
  UI_TYPE, UI_NTYPES,         /* oscillator index and count */
  UI_SEL_ROW, UI_SEL_COL,     /* selected matrix point */
  UI_STEP,                    /* current sequencer step, -1 when stopped */
  UI_P = 16,                  /* 16 page specific numbers, see nf_ui.c */
  UI_MX = 32,                 /* 5 x 8 matrix amounts, -100..100 (columns Pitch, Wave, Timbre, Cutoff, A1 to A4) */
  UI_ARPN = 72,               /* how many keys the arpeggiator holds, 0 = none, then the screen shows four as a demo */
  UI_SEQ = 73,                /* the 64 steps of the shown sequence: note (255 = rest) | length code << 8 (0 semiquaver, 1 quaver, 2 crotchet, 3 minim) */
  UI_SEQCUR = 137,            /* step being edited or recorded */
  UI_SEQREC = 138,            /* 1 while recording */
  UI_COUNT = 139
};
enum {                        /* strings, up to 43 characters */
  UIS_TITLE, UIS_SUB, UIS_TYPE, UIS_L0, UIS_L1, UIS_L2, UIS_L3, UIS_V0, UIS_V1, UIS_V2, UIS_V3, UIS_HINT, UIS_PRESET,
  UIS_ROW0,                   /* 6 list rows, UIS_ROW0 .. UIS_ROW0 + 5, for the preset, BLE and startup pages */
  UIS_COUNT = UIS_ROW0 + 6
};
#define NF_UI_STR_LEN 44
#define NF_UI_WAVE_LEN 120

void nf_ui_init(void);
void nf_ui_set(int id, int value);
int nf_ui_get(int id);
char *nf_ui_str(int slot);               /* writable buffer of NF_UI_STR_LEN bytes */
int8_t *nf_ui_wave(void);                /* NF_UI_WAVE_LEN samples of the oscillator preview, -127..127 */
void nf_ui_paint_band(uint16_t *band, int y0, int rows);
/* See nf_ui.h. */

static int v[UI_COUNT];
static char str[UIS_COUNT][NF_UI_STR_LEN];
static int8_t wave[NF_UI_WAVE_LEN];

void nf_ui_init(void) {
  for (int i = 0; i < UI_COUNT; ++i) v[i] = 0;
  for (int i = 0; i < UIS_COUNT; ++i) str[i][0] = 0;
  for (int i = 0; i < NF_UI_WAVE_LEN; ++i) wave[i] = 0;
  v[UI_N0] = v[UI_N1] = v[UI_N2] = v[UI_N3] = -1; v[UI_STEP] = -1; v[UI_NSUB] = 1;
}
void nf_ui_set(int id, int value) { if (id >= 0 && id < UI_COUNT) v[id] = value; }
int nf_ui_get(int id) { return id >= 0 && id < UI_COUNT ? v[id] : 0; }
char *nf_ui_str(int slot) { return str[slot < 0 || slot >= UIS_COUNT ? 0 : slot]; }
int8_t *nf_ui_wave(void) { return wave; }

#define C_BG      NF_RGB(10, 12, 20)
#define C_PANEL   NF_RGB(20, 24, 38)
#define C_LINE    NF_RGB(52, 60, 84)
#define C_DIM     NF_RGB(96, 106, 134)
#define C_TEXT    NF_RGB(226, 230, 240)
#define C_NEG     NF_RGB(240, 90, 90)
#define C_DARK    NF_RGB(8, 10, 16)
static const uint16_t group_color[11] = {
  NF_RGB(0, 196, 206), NF_RGB(96, 206, 96), NF_RGB(244, 172, 44), NF_RGB(222, 84, 192),
  NF_RGB(160, 128, 250), NF_RGB(250, 124, 60), NF_RGB(240, 96, 120), NF_RGB(96, 160, 255), NF_RGB(120, 214, 150), NF_RGB(250, 210, 90), NF_RGB(0, 196, 206)
};
static const char *const group_name[11] = { "OSC", "ENV", "FX", "LFO", "MATRIX", "ARP", "SEQ", "VOICE", "GLOBAL", "PRESET", "NANO FREAK" };

#define PX 6       /* graphic panel */
#define PY 118
#define PW 228
#define PH 90

static int clampi(int x, int lo, int hi) { return x < lo ? lo : x > hi ? hi : x; }

static void text_c(int cx, int y, const char *s, int scale, uint16_t c) {
  nf_gfx_text(cx - nf_gfx_text_w(s, scale) / 2, y, s, scale, c);
}
static void text_r(int rx, int y, const char *s, int scale, uint16_t c) {
  nf_gfx_text(rx - nf_gfx_text_w(s, scale), y, s, scale, c);
}
static int str_len(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static void int_str(char *d, int x) {         /* d needs 12 bytes */
  char t[12]; int n = 0, neg = x < 0; unsigned u = neg ? (unsigned)(-x) : (unsigned)x;
  do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
  int k = 0; if (neg) d[k++] = '-';
  while (n) d[k++] = t[--n];
  d[k] = 0;
}

/* a filled curve: points given as x, y, drawn left to right */
static void curve(const int *xs, const int *ys, int n, int base, uint16_t fill, uint16_t line) {
  for (int i = 0; i + 1 < n; ++i) {
    int x0 = xs[i], x1 = xs[i + 1];
    for (int x = x0; x <= x1; ++x) {
      int y = x1 == x0 ? ys[i + 1] : ys[i] + (ys[i + 1] - ys[i]) * (x - x0) / (x1 - x0);
      if (base > y) nf_gfx_vline(x, y, base - y, fill);
    }
  }
  for (int i = 0; i + 1 < n; ++i) nf_gfx_line(xs[i], ys[i], xs[i + 1], ys[i + 1], line);
}

static void panel_frame(const char *title, uint16_t accent) {
  nf_gfx_fill(PX, PY, PW, PH, C_PANEL);
  nf_gfx_rect(PX, PY, PW, PH, C_LINE);
  if (title && title[0]) nf_gfx_text(PX + 4, PY + 4, title, 1, accent);
}

/* ---------------------------------------------------------------- graphics -- */

static void g_wave(uint16_t acc) {
  panel_frame("WAVEFORM", acc);
  char a[12], b[12], t[28]; int_str(a, v[UI_TYPE] + 1); int_str(b, v[UI_NTYPES]);
  int n = 0; for (const char *p = "TYPE "; *p; ++p) t[n++] = *p;
  for (const char *p = a; *p; ++p) t[n++] = *p;
  t[n++] = '/'; for (const char *p = b; *p; ++p) t[n++] = *p;
  t[n] = 0; text_r(PX + PW - 4, PY + 4, t, 1, C_DIM);
  const int mid = PY + 50, amp = 32;
  nf_gfx_hline(PX + 2, mid, PW - 4, C_LINE);
  int px = 0, py = mid;
  for (int i = 0; i < NF_UI_WAVE_LEN; ++i) {
    int x = PX + 4 + i * (PW - 8) / (NF_UI_WAVE_LEN - 1), y = mid - wave[i] * amp / 127;
    if (i) nf_gfx_line(px, py, x, y, acc);
    px = x; py = y;
  }
}

static void g_env(uint16_t acc) {
  panel_frame("ENVELOPE", acc);
  const int bx = PX + 10, base = PY + PH - 8, top = PY + 22, hgt = base - top;
  int a = 6 + v[UI_P] * 40 / 100, d = 6 + v[UI_P + 1] * 40 / 100, s = v[UI_P + 2];
  int sy = base - hgt * s / 100, hold = 36;
  int xs[6] = { bx, bx + a, bx + a + d, bx + a + d + hold, bx + a + d + hold + d, 0 };
  int ys[6] = { base, top, sy, sy, base, 0 };
  curve(xs, ys, 5, base, nf_gfx_mix(C_PANEL, acc, 70), acc);
  nf_gfx_hline(bx, base, PW - 20, C_LINE);
  nf_gfx_vline(xs[3], top, base - top, C_LINE);
  text_c(xs[3], PY + 4, "GATE OFF", 1, C_DIM);
}

static void g_cyc(uint16_t acc) {
  static const char *const mode[3] = { "ENV", "RUN", "LOOP" };
  int m = clampi(v[UI_P + 3], 0, 2);
  panel_frame("CYCLING ENVELOPE", acc);
  text_r(PX + PW - 4, PY + 4, mode[m], 1, C_TEXT);
  const int bx = PX + 10, base = PY + PH - 8, top = PY + 22, hgt = base - top;
  int rise = 6 + v[UI_P] * 40 / 100, fall = 6 + v[UI_P + 1] * 40 / 100, hld = v[UI_P + 2];
  int xs[10], ys[10], n = 0, x = bx;
  if (m == 0) {
    int lvl = base - hgt * hld / 100;
    xs[n] = x; ys[n++] = base; x += rise; xs[n] = x; ys[n++] = top; x += fall; xs[n] = x; ys[n++] = lvl;
    x += 40; xs[n] = x; ys[n++] = lvl; x += fall; xs[n] = x; ys[n++] = base;
  } else {
    int hw = 4 + hld * 14 / 100;
    for (int c = 0; c < 2; ++c) {
      xs[n] = x; ys[n++] = base; x += rise; xs[n] = x; ys[n++] = top; x += hw; xs[n] = x; ys[n++] = top; x += fall;
      if (c == 1) { xs[n] = x; ys[n++] = base; }
    }
    n = n > 8 ? 8 : n;
  }
  for (int i = 0; i < n; ++i) if (xs[i] > PX + PW - 6) xs[i] = PX + PW - 6;
  curve(xs, ys, n, base, nf_gfx_mix(C_PANEL, acc, 70), acc);
  nf_gfx_hline(bx, base, PW - 20, C_LINE);
}

static void g_cycmode(uint16_t acc) {
  static const char *const mode[3] = { "ENV", "RUN", "LOOP" };
  static const char *const note[3] = { "ONCE PER KEY", "FREE RUNNING", "RESTARTS ON KEY" };
  int m = clampi(v[UI_P + 3], 0, 2);
  panel_frame("CYCLING MODE", acc);
  for (int i = 0; i < 3; ++i) {
    int x = PX + 10 + i * 72;
    nf_gfx_fill(x, PY + 26, 64, 26, i == m ? acc : C_DARK);
    nf_gfx_rect(x, PY + 26, 64, 26, i == m ? acc : C_LINE);
    text_c(x + 32, PY + 34, mode[i], 1, i == m ? C_DARK : C_DIM);
  }
  text_c(PX + PW / 2, PY + 64, note[m], 1, C_TEXT);
}

static void g_toggle(const char *title, const char *on, const char *off, int state, uint16_t acc) {
  panel_frame(title, acc);
  nf_gfx_fill(PX + 20, PY + 30, PW - 40, 34, state ? acc : C_DARK);
  nf_gfx_rect(PX + 20, PY + 30, PW - 40, 34, state ? acc : C_LINE);
  text_c(PX + PW / 2, PY + 40, state ? on : off, 2, state ? C_DARK : C_DIM);
}

static void g_filter(uint16_t acc) {
  panel_frame("FILTER", acc);
  int type = clampi(v[UI_P + 2], 0, 2);
  text_r(PX + PW - 4, PY + 4, type == 0 ? "LOWPASS" : type == 1 ? "BANDPASS" : "HIGHPASS", 1, C_TEXT);
  const int x0 = PX + 6, w = PW - 12, base = PY + PH - 6, top = PY + 20, hgt = base - top;
  int cut = v[UI_P], res = v[UI_P + 1], steep = v[UI_P + 3] ? 2 : 1;   /* 24 dB per octave rolls off twice as fast */
  int px = 0, py = 0;
  for (int i = 0; i < w; ++i) {
    int f = i * 1000 / w, c = cut * 10;               /* tenths of a percent of the axis, both 0..1000 */
    int d = f - c;                                    /* distance from the cutoff, 1000 = full axis */
    int ad = d < 0 ? -d : d;
    int db10;                                         /* tenths of a dB */
    if (type == 0) db10 = d <= 0 ? 0 : -(ad * 12 * steep) / 10;
    else if (type == 2) db10 = d >= 0 ? 0 : -(ad * 12 * steep) / 10;
    else db10 = -(ad * 12 * steep) / 10;
    int bump = res * 240 / 100 * 1000 / (1000 + (ad * ad) / 4);   /* up to 24 dB at the cutoff */
    db10 += bump;
    int y = base - (db10 + 480) * hgt / 720;          /* -48 dB .. +24 dB */
    y = clampi(y, top, base);
    if (i) nf_gfx_line(px, py, x0 + i, y, acc);
    if (base > y) nf_gfx_vline(x0 + i, y, base - y, nf_gfx_mix(C_PANEL, acc, 60));
    px = x0 + i; py = y;
  }
  nf_gfx_vline(x0 + cut * (w - 1) / 100, top, base - top, C_LINE);
}

/* Second filter page: the transfer curve of Drive (soft saturation in front of the filter) and Clip (hard clip after it), and the slope. */
static void g_filter2(uint16_t acc) {
  panel_frame("FILTER 2", acc);
  text_r(PX + PW - 4, PY + 4, v[UI_P] ? "24 DB" : "12 DB", 1, C_TEXT);
  int drive = clampi(v[UI_P + 1], 0, 100), clip = clampi(v[UI_P + 2], 0, 100);
  const int x0 = PX + 6, w = PW - 12, top = PY + 22, base = PY + PH - 8, mid = (top + base) / 2, half = (base - top) / 2;
  nf_gfx_vline(x0 + w / 2, top, base - top, C_LINE);
  nf_gfx_hline(x0, mid, w, C_LINE);
  int dg = 100 + 7 * drive, cg = 100 + 6 * clip;              /* gains in percent */
  int px = 0, py = mid;
  for (int i = 0; i < w; ++i) {
    int x = (i - w / 2) * 2000 / w;                            /* input -1000..1000 */
    int u = x * dg / 100;
    u = clampi(u, -3000, 3000);
    long u2 = (long)u * u / 1000;                              /* soft clip, same curve as the filter */
    int y = (int)((long)u * (27000 + u2) / (27000 + 9 * u2));
    y = y * 100 / (100 + (dg - 100) / 10);                     /* the drive levelling */
    y = clampi(y * cg / 100, -1000, 1000);                     /* clip at the edge of the plot */
    if (clip == 0) y = clampi(y, -1500, 1500);
    int yy = mid - y * half / 1000;
    yy = clampi(yy, top, base);
    if (i) nf_gfx_line(px, py, x0 + i, yy, acc);
    px = x0 + i; py = yy;
  }
}

static void g_glide(uint16_t acc) {
  static const char *const mode[3] = { "TIME", "SYNC", "RATE" };
  panel_frame("GLIDE", acc);
  text_r(PX + PW - 4, PY + 4, mode[clampi(v[UI_P + 1], 0, 2)], 1, C_TEXT);
  int len = 6 + v[UI_P] * 150 / 100, xa = PX + 20, lo = PY + 70, hi = PY + 32;
  nf_gfx_hline(xa, lo, 30, acc); nf_gfx_hline(xa + 30 + len, hi, 60, acc);
  int px = xa + 30, py = lo;
  for (int i = 1; i <= len; ++i) {
    int y = lo - (lo - hi) * i / len;
    nf_gfx_line(px, py, xa + 30 + i, y, acc); px = xa + 30 + i; py = y;
  }
}

static void g_bend(uint16_t acc) {
  panel_frame("PITCH BEND", acc);
  int steps = v[UI_P];                 /* 0..48 */
  int half = steps * 80 / 48 / 2;
  int cx = PX + PW / 2;
  nf_gfx_fill(cx - half * 2 + 0, PY + 38, half * 4, 14, nf_gfx_mix(C_PANEL, acc, 140));
  nf_gfx_rect(PX + 12, PY + 34, PW - 24, 22, C_LINE);
  nf_gfx_vline(cx, PY + 30, 30, C_TEXT);
  char t[16]; int_str(t, steps / 2);
  text_c(cx, PY + 66, "+/- SEMITONES", 1, C_DIM);
  char m[16] = { '-', 0 }, p[16] = { '+', 0 }; int k = 0; while (t[k]) { m[k + 1] = p[k + 1] = t[k]; ++k; } m[k + 1] = p[k + 1] = 0;
  text_c(cx - 70, PY + 38, m, 1, C_TEXT); text_c(cx + 70, PY + 38, p, 1, C_TEXT);
}

static void g_reverb(uint16_t acc) {                       /* the tail: pre-delay gap, then a decay whose length is the size, its height the mix */
  panel_frame("REVERB", acc);
  const int mix = clampi(v[UI_P], 0, 100), size = clampi(v[UI_P + 1], 0, 100), damp = clampi(v[UI_P + 2], 0, 100), pre = clampi(v[UI_P + 3], 0, 100);
  const int x0 = PX + 12, w = PW - 24, base = PY + PH - 22, hmax = 40;
  const int gap = pre * 30 / 100, h = 6 + mix * (hmax - 6) / 100;
  nf_gfx_fill(x0, base - hmax, 2, hmax, C_TEXT);                                /* the dry hit */
  const int tau = 10 + size * 2;                                                /* pixels: the longer the size the slower the fall */
  uint32_t s = 12345u + (uint32_t)damp;
  for (int x = 0; x < w - 8 - gap; ++x) {
    int e = h * 1000 / (1000 + x * 1000 / tau * x / (tau + 1) + x * 8);
    s = s * 1664525u + 1013904223u;
    const int ln = 1 + (int)(((s >> 16) & 255) * (uint32_t)e / 256u);          /* a dense run of echoes inside the envelope */
    if ((x % (damp > 60 ? 3 : 2)) == 0) nf_gfx_fill(x0 + 6 + gap + x, base - ln, 1, ln, acc);
  }
  char l[40]; int n = 0; const char *k[4] = { "MIX ", " SIZE ", " DAMP ", " PRE " }; const int val[4] = { mix, size, damp, pre };
  for (int q = 0; q < 4; ++q) { for (const char *p = k[q]; *p; ++p) l[n++] = *p; char t[12]; int_str(t, val[q]); for (int i = 0; t[i]; ++i) l[n++] = t[i]; }
  l[n] = 0;
  text_c(PX + PW / 2, PY + PH - 12, l, 1, C_DIM);
}

static void g_delay(uint16_t acc) {                        /* the echoes: spacing is the time, height falls by the feedback, brightness by the tone, each one on its side */
  panel_frame("DELAY", acc);
  const int mix = clampi(v[UI_P], 0, 100), tm = clampi(v[UI_P + 1], 0, 100), fb = clampi(v[UI_P + 2], 0, 100), tone = clampi(v[UI_P + 3], 0, 100);
  const int x0 = PX + 12, w = PW - 24, base = PY + PH - 22, hmax = 40;
  const int ms = 10 + tm * 590 / 100;
  const int gap = 14 + ms * 60 / 600;                                           /* pixels between echoes: more time, wider */
  const int h0 = 6 + mix * (hmax - 6) / 100;
  nf_gfx_fill(x0, base - hmax, 2, hmax, C_TEXT);                                /* the dry hit */
  int h = h0;
  for (int k = 1; k < 12; ++k) {
    const int x = x0 + k * gap; if (x + 3 > x0 + w) break;
    const int lane = (k & 1) ? 0 : 1;                                           /* left echoes high, right echoes low: the ping-pong */
    const int hh = h < 2 ? 2 : h;
    const uint16_t col = nf_gfx_mix(C_PANEL, acc, 90 + tone * 150 / 100);       /* a dark tone dims the repeats */
    nf_gfx_fill(x, base - hh - lane * 0, 3, hh, col);
    nf_gfx_fill(x - 1, base + 2 + lane * 3, 5, 2, lane ? C_DIM : acc);
    h = h * (8 + fb * 92 / 100) / 100;                                          /* the next echo is the feedback times this one */
  }
  char l[40]; int n = 0; const char *k[4] = { "MIX ", " TIME ", " FB ", " TONE " }; const int val[4] = { mix, ms, fb * 92 / 100, tone };
  for (int q = 0; q < 4; ++q) { for (const char *p = k[q]; *p; ++p) l[n++] = *p; char t[12]; int_str(t, val[q]); for (int i = 0; t[i]; ++i) l[n++] = t[i]; if (q == 1) { l[n++] = 'M'; l[n++] = 'S'; } }
  l[n] = 0;
  text_c(PX + PW / 2, PY + PH - 12, l, 1, C_DIM);
}

static void g_spice(uint16_t acc) {
  panel_frame("IMPRESS AND CHANCE", acc);
  const char *name[2] = { "IMPRESS", "CHANCE" };
  for (int i = 0; i < 2; ++i) {
    int y = PY + 24 + i * 28, val = clampi(v[UI_P + i], 0, 100);
    nf_gfx_text(PX + 10, y + 3, name[i], 1, C_TEXT);
    nf_gfx_rect(PX + 70, y, 140, 14, C_LINE);
    nf_gfx_fill(PX + 71, y + 1, 138 * val / 100, 12, acc);
  }
  text_c(PX + PW / 2, PY + PH - 11, "F5 KEY, THEN THE BEND BUTTONS", 1, C_DIM);
}

static int lfo_y(int shape, int ph, int cyc) {          /* -100..100 */
  static const int8_t rnd[8] = { 40, -70, 90, -20, -90, 60, -50, 30 };
  switch (shape) {
    case 1: { int y = ph < 64 ? ph * 2 : ph < 192 ? 128 - (ph - 64) : (ph - 192) * 2 - 128; return y * 100 / 128; }
    case 2: return (ph - 128) * 100 / 128;
    case 3: return ph < 128 ? 100 : -100;
    case 4: return rnd[(cyc * 3 + ph / 64) & 7];
    case 5: { int a = rnd[(cyc * 3 + ph / 64) & 7], b = rnd[(cyc * 3 + ph / 64 + 1) & 7]; return a + (b - a) * (ph & 63) / 64; }
    default: return SINE[(ph * 4) & 1023] * 100 / 32768;
  }
}

static void g_lfo(uint16_t acc) {
  static const char *const names[6] = { "SINE", "TRIANGLE", "SAW", "SQUARE", "SAMPLE+HOLD", "RANDOM" };
  int shape = clampi(v[UI_P], 0, 5), morph = clampi(v[UI_P + 3], 0, 100);
  panel_frame("LFO", acc);
  text_r(PX + PW - 4, PY + 4, names[shape], 1, C_TEXT);
  const int x0 = PX + 8, w = PW - 16, mid = PY + 56, amp = 28;
  nf_gfx_hline(x0, mid, w, C_LINE);
  int cycles = 2 + (v[UI_P + 1] * 3) / 100;       /* faster rate, more cycles drawn */
  int px = 0, py = 0;
  for (int i = 0; i < w; ++i) {
    int ph = (i * cycles * 256 / w) & 255;        /* 0..255 within one cycle */
    int cyc = (i * cycles / w);
    int y = lfo_y(shape, ph, cyc);
    if (morph) y += (lfo_y(shape == 5 ? 0 : shape + 1, ph, cyc) - y) * morph / 100;     /* Morph blends into the next shape */
    y = mid - y * amp / 100;
    if (i) nf_gfx_line(px, py, x0 + i, y, acc);
    px = x0 + i; py = y;
  }
  nf_gfx_text(PX + 34, PY + 4, v[UI_P + 2] ? "SYNCED" : "FREE", 1, C_DIM);
  if (morph) { char t[16] = { 'M', 'O', 'R', 'P', 'H', ' ' }; int_str(t + 6, morph); nf_gfx_text(PX + 4, PY + PH - 12, t, 1, C_DIM); }
}

static void g_matrix(uint16_t acc) {
  static const char *const rows[5] = { "CYC", "ENV", "LFO", "PRS", "KEY" };
  static const char *const cols[8] = { "PIT", "OK2", "OK3", "CUT", "A1", "A2", "A3", "A4" };
  nf_gfx_fill(PX, 44, PW, 164, C_PANEL);
  nf_gfx_rect(PX, 44, PW, 164, C_LINE);
  for (int c = 0; c < 8; ++c) text_c(PX + 34 + c * 24 + 11, 50, cols[c], 1, c < 4 ? C_DIM : acc);
  for (int r = 0; r < 5; ++r) {
    int y = 64 + r * 26;
    text_c(PX + 18, y + 9, rows[r], 1, C_DIM);
    for (int c = 0; c < 8; ++c) {
      int x = PX + 34 + c * 24, a = v[UI_MX + r * 8 + c], off = a >= 1000 || a <= -1000;     /* a route switched off is sent as amount +/- 1000 */
      if (off) a = a > 0 ? a - 1000 : a + 1000;
      int sel = r == v[UI_SEL_ROW] && c == v[UI_SEL_COL];
      nf_gfx_fill(x, y, 22, 24, sel ? nf_gfx_mix(C_PANEL, acc, 60) : C_DARK);
      nf_gfx_rect(x, y, 22, 24, sel ? acc : C_LINE);
      int m = a < 0 ? -a : a;
      if (m && !off) nf_gfx_disc(x + 11, y + 12, 1 + m * 9 / 100, a < 0 ? C_NEG : acc);
      else if (m) nf_gfx_ring(x + 11, y + 12, 1 + m * 9 / 100, C_DIM);       /* off: an outline only */
    }
  }
  nf_gfx_text(PX + 4, 196, str[UIS_L0], 1, C_TEXT);
}

/* Second MATRIX page: which parameter each Assign column drives. The page sets the four names in the value strings. */
static void g_assign(uint16_t acc) {
  nf_gfx_fill(PX, 44, PW, 164, C_PANEL);
  nf_gfx_rect(PX, 44, PW, 164, C_LINE);
  nf_gfx_text(PX + 6, 50, "ASSIGN COLUMNS", 1, C_DIM);
  for (int i = 0; i < 4; ++i) {
    int y = 68 + i * 34;
    nf_gfx_fill(PX + 6, y, 216, 28, C_DARK);
    nf_gfx_rect(PX + 6, y, 216, 28, C_LINE);
    char t[3] = { 'A', (char)('1' + i), 0 };
    nf_gfx_text(PX + 12, y + 3, t, 2, acc);
    char k[3] = { 'K', (char)('1' + i), 0 };
    text_r(PX + 216, y + 3, k, 1, C_DIM);
    nf_gfx_text(PX + 44, y + 11, str[UIS_V0 + i], 1, C_TEXT);
  }
}

static const char *const arp_names[NFA_COUNT] = { "UP", "DOWN", "UP/DOWN", "ORDER", "RANDOM", "PATTERN", "AS PLAYED", "CONVERGE", "DIVERGE", "HOLLOW", "JEOPARDY", "CHORD", "BROKEN", "ALTERNATING" };

static void g_arp(uint16_t acc) {
  int p = clampi(v[UI_P], 0, NFA_COUNT - 1), range = clampi(v[UI_P + 1], 1, 4), len = clampi(v[UI_P + 3], 1, 16);
  panel_frame("ARPEGGIO", acc);
  text_r(PX + PW - 4, PY + 4, arp_names[p], 1, C_TEXT);
  char r[32]; int_str(r, range); int n = str_len(r); r[n++] = ' '; r[n++] = 'O'; r[n++] = 'C'; r[n++] = 'T';
  r[n++] = ' '; r[n++] = ' '; r[n++] = 'L'; r[n++] = 'E'; r[n++] = 'N'; r[n++] = ' '; { char l[12]; int_str(l, len); for (int i = 0; l[i]; ++i) r[n++] = l[i]; }
  r[n] = 0;
  if (v[UI_ARPN] > 0) { r[n++] = ' '; r[n++] = ' '; char k[12]; int_str(k, v[UI_ARPN]); for (int i = 0; k[i]; ++i) r[n++] = k[i]; r[n++] = 'K'; r[n] = 0; }
  text_c(PX + PW / 2, PY + PH - 11, r, 1, C_DIM);
  uint8_t d[16]; int levels = 4; nf_arp_demo(p, range, len, v[UI_ARPN] > 0 ? v[UI_ARPN] : 4, d, &levels);
  const int base = PY + PH - 20, top = PY + 20;
  const int slot = (PW - 16) / len > 26 ? 26 : (PW - 16) / len, bw = slot - 2 > 18 ? 18 : slot - 2;   /* one bar per step of the loop */
  const int x0 = PX + (PW - slot * len) / 2 + 1;
  for (int i = 0; i < len; ++i) {
    int x = x0 + i * slot, cur = i == v[UI_STEP] && v[UI_STEP] >= 0;
    uint16_t c = cur ? C_TEXT : acc;
    if (i > 0 && (i & 3) == 0 && len > 4) nf_gfx_fill(x - 1, top - 4, 1, base - top + 4, C_LINE);      /* a division every 4 steps */
    if (d[i] == NFA_REST) { nf_gfx_fill(x + bw / 2 - 1, base - 3, bw > 5 ? 3 : 2, 3, C_LINE); continue; }
    if (d[i] == 254) { for (int j = 0; j < 4; ++j) nf_gfx_fill(x, base - 6 - j * (base - top - 6) / 3 - 4, bw, 5, c); continue; }
    int hgt = levels > 1 ? 6 + d[i] * (base - top - 6) / (levels - 1) : 6 + (base - top - 6) / 2;
    nf_gfx_fill(x, base - hgt, bw, hgt, c);
  }
}

static const char *const chord_names[9] = { "OFF", "MAJ", "MIN", "7", "MIN7", "MAJ7", "SUS4", "DIM7", "POWER" };
static void g_arp3(uint16_t acc) {                          /* type: single notes or chords */
  panel_frame("ARP TYPE", acc);
  const int chord = v[UI_P + 6] != 0, q = clampi(v[UI_P + 7], 0, 8);
  text_r(PX + PW - 4, PY + 4, chord ? "CHORD" : "MONO", 1, C_TEXT);
  nf_gfx_fill(PX + 10, PY + 22, 100, 20, !chord ? acc : C_DARK); nf_gfx_rect(PX + 10, PY + 22, 100, 20, !chord ? acc : C_LINE);
  text_c(PX + 60, PY + 28, "MONO", 1, !chord ? C_DARK : C_DIM);
  nf_gfx_fill(PX + 118, PY + 22, 100, 20, chord ? acc : C_DARK); nf_gfx_rect(PX + 118, PY + 22, 100, 20, chord ? acc : C_LINE);
  text_c(PX + 168, PY + 28, "CHORD", 1, chord ? C_DARK : C_DIM);
  uint8_t d[16]; int levels = 4; nf_arp_demo(clampi(v[UI_P], 0, NFA_COUNT - 1), 1, 8, 4, d, &levels);
  const int base = PY + PH - 20, top = PY + 52;
  for (int i = 0; i < 8; ++i) {                             /* the pattern: a note per step, or a four note chord per step */
    int x = PX + 14 + i * 26;
    if (d[i] == NFA_REST) { nf_gfx_fill(x + 6, base - 3, 6, 3, C_LINE); continue; }
    int y = d[i] == 254 ? base - 10 : base - 6 - d[i] * (base - top - 14) / 3;
    if (chord && q > 0) for (int j = 0; j < 4; ++j) nf_gfx_fill(x, y - j * 5 - 2, 18, 3, acc);
    else nf_gfx_fill(x, y - 2, 18, 4, acc);
  }
  text_c(PX + PW / 2, PY + PH - 11, chord ? chord_names[q] : "SINGLE NOTES", 1, C_DIM);
}

static void g_arp2(uint16_t acc) {                          /* swing, latch and sync */
  panel_frame("ARP TIMING", acc);
  int swing = clampi(v[UI_P + 4], 0, 100), pct = 50 + swing / 4;
  char t[16]; int_str(t, pct); int n = str_len(t); t[n] = '%'; t[n + 1] = 0;
  char u[24] = { 'S', 'W', 'I', 'N', 'G', ' ' }; for (int i = 0; t[i]; ++i) u[6 + i] = t[i]; u[6 + n + 1] = 0;
  text_r(PX + PW - 4, PY + 4, u, 1, C_TEXT);
  /* eight steps on a time line, the off-beats pushed late by the swing */
  const int base = PY + 52, unit = 26;
  const int s256 = 128 + swing * 64 / 100;                  /* 0.5 .. 0.75 in 1/256 */
  for (int i = 0; i < 8; ++i) {
    int x = PX + 14 + (i / 2) * 2 * unit + ((i & 1) ? 2 * unit * s256 / 256 : 0);
    nf_gfx_fill(x, base - (i & 1 ? 14 : 24), 10, i & 1 ? 14 : 24, acc);
  }
  nf_gfx_hline(PX + 8, base + 1, PW - 16, C_LINE);
  const int on = v[UI_P + 5], sy = v[UI_P + 2];
  nf_gfx_fill(PX + 10, PY + 64, 100, 20, on ? acc : C_DARK); nf_gfx_rect(PX + 10, PY + 64, 100, 20, on ? acc : C_LINE);
  text_c(PX + 60, PY + 70, on ? "LATCH ON" : "LATCH OFF", 1, on ? C_DARK : C_DIM);
  nf_gfx_fill(PX + 118, PY + 64, 100, 20, sy ? acc : C_DARK); nf_gfx_rect(PX + 118, PY + 64, 100, 20, sy ? acc : C_LINE);
  text_c(PX + 168, PY + 70, sy ? "SYNC" : "FREE", 1, sy ? C_DARK : C_DIM);
}

static void g_voice(uint16_t acc) {
  static const char *const names[4] = { "MONO", "UNISON", "PARAPHONIC", "POLY" };
  static const char *const note[4] = { "ONE VOICE, LEGATO GLIDE", "FOUR VOICES ON ONE NOTE", "FOUR NOTES, ONE SHARED FILTER", "FOUR NOTES, A FILTER EACH" };
  int m = clampi(v[UI_SUB], 0, 3);
  panel_frame("VOICE MODE", acc);
  text_c(PX + PW / 2, PY + 14, names[m], 2, C_TEXT);
  const int nv = m == 0 ? 1 : 4, nfilt = m == 3 ? 4 : 1;
  const int vx = PX + 24, fx = PX + 118, ox = PX + 196, mid = PY + 45;
  for (int i = 0; i < nv; ++i) {
    int y = nv == 1 ? mid - 3 : PY + 30 + i * 8;
    nf_gfx_fill(vx, y, 24, 5, acc);
    if (nfilt == 4) {
      nf_gfx_line(vx + 24, y + 2, fx, y + 2, C_LINE);
      nf_gfx_fill(fx, y, 24, 5, nf_gfx_mix(C_PANEL, acc, 160));
      nf_gfx_line(fx + 24, y + 2, ox, mid, C_LINE);
    } else nf_gfx_line(vx + 24, y + 2, fx, mid, C_LINE);
  }
  if (nfilt == 1) { nf_gfx_fill(fx, mid - 14, 24, 28, nf_gfx_mix(C_PANEL, acc, 160)); nf_gfx_line(fx + 24, mid, ox, mid, C_LINE); }
  nf_gfx_text(vx, PY + 66, "VOICES", 1, C_DIM);
  nf_gfx_text(fx, PY + 66, nfilt == 4 ? "FILTERS" : "FILTER", 1, C_DIM);
  nf_gfx_text(ox - 4, PY + 66, "OUT", 1, C_DIM);
  if (m == 1) { char t[16] = { 'D', 'E', 'T', 'U', 'N', 'E', ' ' }; int_str(t + 7, clampi(v[UI_P], 0, 100)); text_r(PX + PW - 4, PY + 4, t, 1, C_DIM); }
  text_c(PX + PW / 2, PY + PH - 12, note[m], 1, C_TEXT);
}

static void note_str(char *o, int n) {
  static const char nm[12][3] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
  int k = 0; for (const char *p = nm[n % 12]; *p; ++p) o[k++] = *p;
  int oc = n / 12 - 1; if (oc < 0) { o[k++] = '-'; oc = 1; } o[k++] = (char)('0' + oc); o[k] = 0;
}
static const char *const seq_len_name[4] = { "SEMIQUAVER 1/16", "QUAVER 1/8", "CROTCHET 1/4", "MINIM 1/2" };

/* The steps as cells: the bar's height is the pitch, its width is the note length, a dot is a rest. */
static void seq_cells(uint16_t acc, int len, int cur, int play) {
  int lo = 127, hi = 0;
  for (int i = 0; i < len; ++i) { int nt = v[UI_SEQ + i] & 255; if (nt != 255) { if (nt < lo) lo = nt; if (nt > hi) hi = nt; } }
  if (lo > hi) { lo = 48; hi = 60; }
  if (hi == lo) hi = lo + 1;
  const int rows = (len + 15) / 16, rowh = rows == 1 ? 42 : rows == 2 ? 28 : 15, ch = rowh - 2;
  for (int i = 0; i < len; ++i) {
    const int r = i / 16, c = i % 16, x = PX + 10 + c * 13, y = PY + 20 + r * rowh;
    const int nt = v[UI_SEQ + i] & 255, d = (v[UI_SEQ + i] >> 8) & 3;
    nf_gfx_fill(x, y, 12, ch, i == play ? nf_gfx_mix(C_PANEL, acc, 190) : ((i & 7) < 4 ? nf_gfx_mix(C_PANEL, acc, 70) : nf_gfx_mix(C_PANEL, acc, 40)));
    if (nt == 255) nf_gfx_fill(x + 3, y + ch / 2, 6, 1, C_DIM);
    else {
      const int bw = 3 + d * 3, by = y + ch - 3 - (nt - lo) * (ch - 3) / (hi - lo);
      nf_gfx_fill(x, by, bw, 3, i == play ? C_TEXT : acc);
    }
    if (i == cur) nf_gfx_rect(x - 1, y - 1, 14, ch + 2, C_TEXT);
  }
}

static void g_seq(uint16_t acc) {
  panel_frame("SEQUENCE", acc);
  char t[8] = { 'P', 'A' + (char)clampi(v[UI_P], 0, 1), 0 };
  text_r(PX + PW - 4, PY + 4, t, 1, C_TEXT);
  const int len = clampi(v[UI_P + 2], 1, 64), rec = v[UI_SEQREC];
  seq_cells(acc, len, rec ? v[UI_SEQCUR] : -1, v[UI_STEP]);
  char l[40]; int_str(l, len); int n = str_len(l);
  const char *tail = len == 1 ? " STEP" : " STEPS"; for (int i = 0; tail[i]; ++i) l[n++] = tail[i];
  if (rec) { l[n++] = ' '; l[n++] = ' '; l[n++] = 'R'; l[n++] = 'E'; l[n++] = 'C'; }
  if (v[UI_P + 1] > 0) { l[n++] = ' '; l[n++] = ' '; l[n++] = 'R'; l[n++] = 'N'; l[n++] = 'D'; l[n++] = ' '; char q[12]; int_str(q, v[UI_P + 1]); for (int i = 0; q[i]; ++i) l[n++] = q[i]; }
  if (v[UI_P + 13] > 0) { l[n++] = ' '; l[n++] = ' '; l[n++] = 'M'; l[n++] = 'U'; l[n++] = 'T'; l[n++] = ' '; char q[12]; int_str(q, v[UI_P + 13]); for (int i = 0; q[i]; ++i) l[n++] = q[i]; }
  l[n] = 0;
  text_c(PX + PW / 2, PY + PH - 11, l, 1, rec ? C_TEXT : C_DIM);
}

static const char *const scale_names[7] = { "MIN PENT", "MAJ PENT", "MAJOR", "MINOR", "DORIAN", "BLUES", "CHROMATIC" };

static void g_seqlen(uint16_t acc) {
  panel_frame("SEQ LENGTH", acc);
  const int len = clampi(v[UI_P + 2], 1, 64), oct = clampi(v[UI_P + 12], 1, 3);
  char l[16]; int_str(l, len); int n = str_len(l); l[n++] = ' '; l[n++] = 'S'; l[n++] = 'T'; l[n++] = 'E'; l[n++] = 'P'; l[n++] = len == 1 ? 0 : 'S'; l[n] = 0;
  text_r(PX + PW - 4, PY + 4, l, 1, C_TEXT);
  seq_cells(acc, len, -1, v[UI_STEP]);
  char o[12] = { (char)('0' + oct), ' ', 'O', 'C', 'T', 0 };
  nf_gfx_text(PX + 6, PY + PH - 11, scale_names[clampi(v[UI_P + 10], 0, 6)], 1, C_TEXT);
  text_r(PX + PW - 4, PY + PH - 11, o, 1, C_DIM);
}

static void g_seqrand(uint16_t acc) {                      /* random pattern: scale, share of rests, octaves, roll */
  panel_frame("RANDOM", acc);
  const int len = clampi(v[UI_P + 2], 1, 64), roll = v[UI_P + 1];
  char r[16] = { 'R', 'O', 'L', 'L', ' ' }; if (roll > 0) int_str(r + 5, roll); else { r[4] = 0; text_r(PX + PW - 4, PY + 4, "OFF", 1, C_DIM); r[0] = 0; }
  if (r[0]) text_r(PX + PW - 4, PY + 4, r, 1, C_TEXT);
  seq_cells(acc, len, -1, v[UI_STEP]);
  char l[48]; int n = 0; for (const char *p = seq_len_name[clampi(v[UI_P + 3], 0, 3)]; *p; ++p) l[n++] = *p;
  l[n++] = ' '; l[n++] = ' '; l[n++] = 'R'; l[n++] = 'E'; l[n++] = 'S'; l[n++] = 'T'; l[n++] = ' '; { char q[12]; int_str(q, clampi(v[UI_P + 11], 0, 50)); for (int i = 0; q[i]; ++i) l[n++] = q[i]; } l[n++] = '%';
  l[n++] = ' '; l[n++] = ' '; l[n++] = 'G'; l[n++] = 'A'; l[n++] = 'T'; l[n++] = 'E'; l[n++] = ' '; { char q[12]; int_str(q, clampi(v[UI_P + 8], 10, 100)); for (int i = 0; q[i]; ++i) l[n++] = q[i]; } l[n++] = '%'; l[n] = 0;
  text_c(PX + PW / 2, PY + PH - 11, l, 1, C_DIM);
}

/* GLO, as on the FM1 Open firmware: the Bluetooth settings and the MIDI settings. */
static void g_radio(uint16_t acc) {
  static const char *const lab[4] = { "K1 BLUETOOTH", "K2 BT AUDIO", "K3 BLE ROLE", "K4 CLOCK" };
  panel_frame("BLUETOOTH", acc);
  const int on = v[UI_P] != 0, audio = v[UI_P + 1] != 0, role = v[UI_P + 2] != 0, clk = v[UI_P + 3] != 0;
  const char *val[4] = { on ? "ON" : "OFF", audio ? "ON" : "OFF", role ? "HOST" : "DEVICE", clk ? "MIDI" : "INT" };
  for (int i = 0; i < 4; ++i) {
    const int y = PY + 20 + i * 15;
    nf_gfx_text(PX + 10, y, lab[i], 1, C_DIM);
    text_r(PX + PW - 10, y, val[i], 1, (i == 0 || on) ? acc : C_DIM);
  }
  text_c(PX + PW / 2, PY + PH - 10, "RADIO AVAILABLE ON DEVICE", 1, C_DIM);
}

/* MIDI settings, three pages of four values as in the firmware: output, routing, input. The page number is v[UI_SUB] - 1. */
static void g_midi(uint16_t acc) {
  static const char *const title[3] = { "MIDI OUT", "MIDI ROUTING", "MIDI IN" };
  static const char *const lab[3][4] = { { "K1 SEQ/ARP", "K2 OUT CH", "K3 OUT PORT", "K4 KEY OUT" }, { "K1 LOCAL", "K2 CLOCK OUT", "K3 THRU IN", "K4 THRU OUT" }, { "K1 IN CH", "K2 CLOCK", "", "" } };
  static const char *const mode[3] = { "LOCAL", "BOTH", "MIDI" };
  static const char *const port[8] = { "OFF", "DIN", "USB", "DIN+USB", "BLE", "DIN+BLE", "USB+BLE", "ALL" };
  static const char *const src[3] = { "DIN", "USB", "BLE" };
  const int pg = clampi(v[UI_SUB] - 1, 0, 2);
  panel_frame(title[pg], acc);
  char val[4][12]; int n = pg == 2 ? 2 : 4;
  for (int i = 0; i < 4; ++i) val[i][0] = 0;
  const int a = v[UI_P], b = v[UI_P + 1], c = v[UI_P + 2], d = v[UI_P + 3];
  if (pg == 0) { const char *m = mode[clampi(a, 0, 2)]; for (int i = 0; (val[0][i] = m[i]); ++i) ; int_str(val[1], clampi(b, 0, 15) + 1); const char *p = port[clampi(c, 0, 7)]; for (int i = 0; (val[2][i] = p[i]); ++i) ; const char *k = d ? "ON" : "OFF"; for (int i = 0; (val[3][i] = k[i]); ++i) ; }
  else if (pg == 1) { const char *o[2] = { "OFF", "ON" }; const char *l = o[a != 0], *k = o[b != 0], *s = src[clampi(c, 0, 2)], *p = port[clampi(d, 0, 7)];
    for (int i = 0; (val[0][i] = l[i]); ++i) ; for (int i = 0; (val[1][i] = k[i]); ++i) ; for (int i = 0; (val[2][i] = s[i]); ++i) ; for (int i = 0; (val[3][i] = p[i]); ++i) ; }
  else { int_str(val[0], clampi(a, 0, 15) + 1); const char *k = b ? "MIDI" : "INT"; for (int i = 0; (val[1][i] = k[i]); ++i) ; }
  for (int i = 0; i < n; ++i) {
    const int y = PY + 20 + i * 15;
    nf_gfx_text(PX + 10, y, lab[pg][i], 1, C_DIM);
    text_r(PX + PW - 10, y, val[i], 1, acc);
  }
  text_c(PX + PW / 2, PY + PH - 10, pg == 2 ? "MIDI CLOCK DRIVES ARP AND SEQ" : "PORTS ARE ON THE DEVICE", 1, C_DIM);
}

/* A list page: UI_P is the selected row, UI_P + 1 the number of rows (0 to 6), UIS_ROW0 the row text.
   A row that starts with '*' is drawn dim (empty or unavailable). Used for the preset, BLE device and start-up pages. */
static void g_list(const char *title, uint16_t acc) {
  panel_frame(title, acc);
  const int sel = v[UI_P], n = clampi(v[UI_P + 1], 0, 6);
  for (int i = 0; i < n; ++i) {
    const char *t = str[UIS_ROW0 + i]; int y = PY + 17 + i * 12, dim = t[0] == '*';
    if (dim) ++t;
    if (i == sel) { nf_gfx_fill(PX + 3, y - 2, PW - 6, 12, nf_gfx_mix(C_PANEL, acc, 90)); nf_gfx_text(PX + 6, y, ">", 1, acc); }
    nf_gfx_text(PX + 16, y, t, 1, dim ? C_DIM : C_TEXT);
  }
}

/* start-up splash, shown for a moment at power-on */
static void g_boot(void) {
  nf_gfx_fill(0, 0, NF_W, NF_H, C_BG);
  text_c(NF_W / 2, 84, "NANO FREAK", 3, C_TEXT);
  nf_gfx_hline(40, 118, NF_W - 80, group_color[0]);
  text_c(NF_W / 2, 132, "SYNTH FOR THE M-VAVE FM-1", 1, C_DIM);
  text_c(NF_W / 2, 150, str[UIS_HINT], 1, group_color[2]);
}

static void g_seqedit(uint16_t acc) {                      /* one step: its number, pitch and length */
  panel_frame("STEP EDIT", acc);
  const int len = clampi(v[UI_P + 2], 1, 64), cur = clampi(v[UI_SEQCUR], 0, 63);
  seq_cells(acc, len, cur, v[UI_STEP]);
  const int nt = v[UI_SEQ + cur] & 255, d = (v[UI_SEQ + cur] >> 8) & 3;
  char l[24]; int_str(l, cur + 1); int n = str_len(l); l[n++] = ' '; l[n++] = ' ';
  if (nt == 255) { l[n++] = 'R'; l[n++] = 'E'; l[n++] = 'S'; l[n++] = 'T'; l[n] = 0; } else { note_str(l + n, nt); }
  text_c(PX + 40, PY + PH - 11, l, 1, C_TEXT);
  text_r(PX + PW - 4, PY + PH - 11, seq_len_name[d], 1, C_DIM);
}

static void g_smooth(uint16_t acc) {
  panel_frame("MOD TRACK SMOOTHING", acc);
  for (int i = 0; i < 4; ++i) {
    int x = PX + 10 + i * 54, on = v[UI_P + 4 + i];
    char t[4] = { 'M', (char)('1' + i), 0 };
    nf_gfx_fill(x, PY + 28, 48, 34, on ? acc : C_DARK);
    nf_gfx_rect(x, PY + 28, 48, 34, on ? acc : C_LINE);
    text_c(x + 24, PY + 34, t, 1, on ? C_DARK : C_DIM);
    text_c(x + 24, PY + 47, on ? "ON" : "OFF", 1, on ? C_DARK : C_DIM);
  }
}

/* ------------------------------------------------------------------ screen -- */

static void cards(uint16_t acc) {
  const int n[4] = { v[UI_N0], v[UI_N1], v[UI_N2], v[UI_N3] };
  for (int i = 0; i < 4; ++i) {
    int x = 6 + i * 57;
    const char *lab = str[UIS_L0 + i], *val = str[UIS_V0 + i];
    int used = lab[0] != 0;
    nf_gfx_fill(x, 44, 54, 68, used ? C_PANEL : C_BG);
    nf_gfx_rect(x, 44, 54, 68, used ? C_LINE : nf_gfx_mix(C_BG, C_LINE, 120));
    if (!used) continue;
    nf_gfx_text(x + 3, 49, lab, 1, C_DIM);
    int sc = nf_gfx_text_w(val, 2) <= 48 ? 2 : 1;
    nf_gfx_text(x + 3, 64 + (sc == 1 ? 3 : 0), val, sc, C_TEXT);
    char k[3] = { 'K', (char)('1' + i), 0 };
    nf_gfx_text(x + 3, 84, k, 1, acc);
    if (n[i] >= 0) {
      nf_gfx_rect(x + 3, 98, 48, 9, C_LINE);
      nf_gfx_fill(x + 4, 99, 46 * clampi(n[i], 0, 100) / 100, 7, acc);
    }
  }
}

void nf_ui_paint_band(uint16_t *band, int y0, int rows) {
  nf_gfx_begin(band, y0, rows);
  nf_gfx_fill(0, y0, NF_W, rows, C_BG);
  int g = clampi(v[UI_GROUP], 0, 10), sub = v[UI_SUB];
  if (g == 10) { g_boot(); return; }
  uint16_t acc = group_color[g];

  /* header */
  nf_gfx_fill(0, 0, NF_W, 26, acc);
  nf_gfx_text(8, 5, str[UIS_TITLE][0] ? str[UIS_TITLE] : group_name[g], 2, C_DARK);
  text_r(NF_W - 8, 9, str[UIS_TYPE], 1, C_DARK);
  /* sub page strip */
  nf_gfx_text(6, 31, str[UIS_SUB], 1, C_TEXT);
  for (int i = 0; i < v[UI_NSUB] && i < 8; ++i)
    nf_gfx_fill(NF_W - 8 - (v[UI_NSUB] - i) * 9, 31, 6, 6, i == sub ? acc : C_LINE);

  if (g == 4) {
    if (sub == 1) g_assign(acc); else g_matrix(acc);
  } else {
    cards(acc);
    switch (g) {
      case 0: g_wave(acc); break;
      case 1:
        if (sub == 0) g_env(acc);
        else if (sub == 1) g_cyc(acc);
        else if (sub == 2) g_cycmode(acc);
        else g_toggle("AMP ENVELOPE", "ENVELOPE ON", "GATE ONLY", v[UI_P], acc);
        break;
      case 2: if (sub == 0) g_filter(acc); else if (sub == 1) g_filter2(acc); else if (sub == 2) g_reverb(acc); else if (sub == 3) g_delay(acc); else if (sub == 4) g_glide(acc); else if (sub == 5) g_bend(acc); else g_spice(acc); break;
      case 3: g_lfo(acc); break;
      case 5: if (sub == 0) g_arp(acc); else if (sub == 1) g_arp2(acc); else g_arp3(acc); break;
      case 7: g_voice(acc); break;
      case 8: if (sub == 0) g_radio(acc); else if (sub <= 3) g_midi(acc); else g_list(sub == 4 ? "BLE DEVICES" : "START-UP", acc); break;
      case 9: g_list(sub == 0 ? "PRESETS" : "SAVE PRESET", acc); break;
      default: if (sub == 0) g_seq(acc); else if (sub == 1) g_seqlen(acc); else if (sub == 2) g_seqedit(acc); else if (sub == 3) g_seqrand(acc); else g_smooth(acc); break;
    }
  }

  /* footer */
  nf_gfx_hline(0, 212, NF_W, C_LINE);
  nf_gfx_text(6, 218, str[UIS_HINT], 1, C_DIM);
  char o[8] = { 'O', 'C', 'T', ' ', v[UI_OCT] < 0 ? '-' : '+', (char)('0' + (v[UI_OCT] < 0 ? -v[UI_OCT] : v[UI_OCT])), 0 };
  nf_gfx_text(6, 229, str[UIS_PRESET][0] ? str[UIS_PRESET] : "INIT", 1, C_TEXT);
  text_r(NF_W - 6, 229, o, 1, C_TEXT);
  { static const char *const vn[4] = { "MONO", "UNI", "PARA", "POLY" }; text_c(NF_W - 70, 229, vn[clampi(v[UI_POLY], 0, 3)], 1, acc); }
  if (v[UI_PLAY]) { nf_gfx_fill(NF_W - 36, 218, 0, 0, acc); text_r(NF_W - 6, 218, "PLAY", 1, acc); }
}

#undef cards
#undef curve
#undef str_len
#undef v
#undef wave
