/* SPDX-License-Identifier: GPL-3.0-only */
/* The Nano Freak screen painter's drawing layer (from the test bed, dsp/nf_gfx.c): 240 x 240 RGB565, drawn a band of rows at a time.
 * Included by nano_app.c. The 5 x 7 glyphs for digits, letters and + - / . < > come from fm1-emu (MIT, Joakim Eriksson). */
/* Nano Freak - small portable drawing layer for the FM-1 screen (240 x 240, RGB565).
 *
 * Everything draws into a band of rows, so the same code serves the device
 * (fm1_screen_submit takes at most 240 x 16 pixels) and the web page (the whole frame
 * is 15 bands). Primitives clip to the band, so a screen painter can simply be run
 * once per band. Licence: MIT, see THIRD_PARTY.md for the font. */

#define font nano_font       /* (names the one compilation unit already has) */
#define NF_W 240
#define NF_H 240
#define NF_BAND_ROWS 16
#define NF_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

/* Select the band being drawn: buf holds rows y0 .. y0 + rows - 1, 240 pixels per row. */
void nf_gfx_begin(uint16_t *buf, int y0, int rows);
void nf_gfx_fill(int x, int y, int w, int h, uint16_t c);
void nf_gfx_rect(int x, int y, int w, int h, uint16_t c);      /* 1 px outline */
void nf_gfx_hline(int x, int y, int w, uint16_t c);
void nf_gfx_vline(int x, int y, int h, uint16_t c);
void nf_gfx_line(int x0, int y0, int x1, int y1, uint16_t c);
void nf_gfx_disc(int cx, int cy, int r, uint16_t c);
void nf_gfx_ring(int cx, int cy, int r, uint16_t c);
void nf_gfx_pixel(int x, int y, uint16_t c);
/* Text uses a 5 x 7 font, upper case only, scale pixels per dot. Returns the advance in pixels. */
int nf_gfx_text(int x, int y, const char *s, int scale, uint16_t c);
int nf_gfx_text_w(const char *s, int scale);
uint16_t nf_gfx_mix(uint16_t a, uint16_t b, int t256);         /* t256 = 0 gives a, 256 gives b */
/* See nf_gfx.h. The 5 x 7 glyph data for digits, letters and + - / . < > comes from
 * fm1-emu (MIT, Joakim Eriksson); the other glyphs are our own. */

static uint16_t *g_buf;
static int g_y0, g_rows;

/* Columns, bit 0 at the top. ASCII 32 to 95; lower case is drawn as upper case. */
static const uint8_t font[64][5] = {
  {0x00, 0x00, 0x00, 0x00, 0x00}, /*   */
  {0x00, 0x00, 0x5F, 0x00, 0x00}, /* ! */
  {0x00, 0x00, 0x00, 0x00, 0x00}, /* " */
  {0x12, 0x3F, 0x12, 0x3F, 0x12}, /* # */
  {0x00, 0x00, 0x00, 0x00, 0x00}, /* $ */
  {0x13, 0x0B, 0x04, 0x32, 0x30}, /* % */
  {0x00, 0x00, 0x00, 0x00, 0x00}, /* & */
  {0x00, 0x04, 0x03, 0x00, 0x00}, /* ' */
  {0x00, 0x1C, 0x22, 0x41, 0x00}, /* ( */
  {0x00, 0x41, 0x22, 0x1C, 0x00}, /* ) */
  {0x2A, 0x1C, 0x3E, 0x1C, 0x2A}, /* * */
  {0x08, 0x08, 0x3E, 0x08, 0x08}, /* + */
  {0x00, 0x40, 0x30, 0x10, 0x00}, /* , */
  {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
  {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
  {0x20, 0x10, 0x08, 0x04, 0x02}, /* / */
  {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
  {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
  {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
  {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
  {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
  {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
  {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
  {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
  {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
  {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
  {0x00, 0x00, 0x12, 0x00, 0x00}, /* : */
  {0x00, 0x00, 0x00, 0x00, 0x00}, /* ; */
  {0x08, 0x14, 0x22, 0x41, 0x00}, /* < */
  {0x14, 0x14, 0x14, 0x14, 0x14}, /* = */
  {0x00, 0x41, 0x22, 0x14, 0x08}, /* > */
  {0x02, 0x01, 0x51, 0x09, 0x06}, /* ? */
  {0x00, 0x00, 0x00, 0x00, 0x00}, /* @ */
  {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
  {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
  {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
  {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
  {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
  {0x7F, 0x09, 0x09, 0x09, 0x01}, /* F */
  {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* G */
  {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
  {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
  {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
  {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
  {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
  {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* M */
  {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
  {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
  {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
  {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
  {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
  {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
  {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
  {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
  {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
  {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* W */
  {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
  {0x07, 0x08, 0x70, 0x08, 0x07}, /* Y */
  {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
  {0x00, 0x00, 0x00, 0x00, 0x00}, /* [ */
  {0x00, 0x00, 0x00, 0x00, 0x00}, /* backslash */
  {0x00, 0x00, 0x00, 0x00, 0x00}, /* ] */
  {0x04, 0x02, 0x01, 0x02, 0x04}, /* ^ */
  {0x40, 0x40, 0x40, 0x40, 0x40}, /* _ */
};

void nf_gfx_begin(uint16_t *buf, int y0, int rows) { g_buf = buf; g_y0 = y0; g_rows = rows; }

void nf_gfx_pixel(int x, int y, uint16_t c) {
  if (x < 0 || x >= NF_W || y < g_y0 || y >= g_y0 + g_rows) return;
  g_buf[(y - g_y0) * NF_W + x] = c;
}

void nf_gfx_fill(int x, int y, int w, int h, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  int x1 = x + w, y1 = y + h;
  if (x < 0) x = 0;
  if (x1 > NF_W) x1 = NF_W;
  if (y < g_y0) y = g_y0;
  if (y1 > g_y0 + g_rows) y1 = g_y0 + g_rows;
  for (int j = y; j < y1; ++j) {
    uint16_t *p = g_buf + (j - g_y0) * NF_W;
    for (int i = x; i < x1; ++i) p[i] = c;
  }
}

void nf_gfx_hline(int x, int y, int w, uint16_t c) { nf_gfx_fill(x, y, w, 1, c); }
void nf_gfx_vline(int x, int y, int h, uint16_t c) { nf_gfx_fill(x, y, 1, h, c); }
void nf_gfx_rect(int x, int y, int w, int h, uint16_t c) {
  if (w < 2 || h < 2) { nf_gfx_fill(x, y, w, h, c); return; }
  nf_gfx_hline(x, y, w, c); nf_gfx_hline(x, y + h - 1, w, c);
  nf_gfx_vline(x, y + 1, h - 2, c); nf_gfx_vline(x + w - 1, y + 1, h - 2, c);
}

void nf_gfx_line(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
  int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;     /* dy is negative */
  int err = dx + dy;
  for (int n = 0; n < 1000; ++n) {
    nf_gfx_pixel(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void nf_gfx_disc(int cx, int cy, int r, uint16_t c) {
  for (int j = -r; j <= r; ++j) {
    int yy = cy + j;
    if (yy < g_y0 || yy >= g_y0 + g_rows) continue;
    for (int i = -r; i <= r; ++i) if (i * i + j * j <= r * r + r) nf_gfx_pixel(cx + i, yy, c);
  }
}

void nf_gfx_ring(int cx, int cy, int r, uint16_t c) {
  for (int j = -r; j <= r; ++j) {
    int yy = cy + j;
    if (yy < g_y0 || yy >= g_y0 + g_rows) continue;
    for (int i = -r; i <= r; ++i) {
      int d = i * i + j * j;
      if (d <= r * r + r && d >= (r - 1) * (r - 1)) nf_gfx_pixel(cx + i, yy, c);
    }
  }
}

int nf_gfx_text_w(const char *s, int scale) {
  int n = 0; while (s[n]) ++n;
  return n ? n * 6 * scale - scale : 0;
}

int nf_gfx_text(int x, int y, const char *s, int scale, uint16_t c) {
  if (y + 7 * scale <= g_y0 || y >= g_y0 + g_rows) return nf_gfx_text_w(s, scale);
  int px = x;
  for (; *s; ++s) {
    int ch = (unsigned char)*s;
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 95) ch = 32;
    const uint8_t *g = font[ch - 32];
    for (int gx = 0; gx < 5; ++gx)
      for (int gy = 0; gy < 7; ++gy)
        if (g[gx] >> gy & 1) nf_gfx_fill(px + gx * scale, y + gy * scale, scale, scale, c);
    px += 6 * scale;
  }
  return px - x - scale;
}

uint16_t nf_gfx_mix(uint16_t a, uint16_t b, int t) {
  if (t < 0) t = 0;
  if (t > 256) t = 256;
  int r = ((a >> 11) * (256 - t) + (b >> 11) * t) >> 8;
  int g = (((a >> 5) & 63) * (256 - t) + ((b >> 5) & 63) * t) >> 8;
  int bl = ((a & 31) * (256 - t) + (b & 31) * t) >> 8;
  return (uint16_t)(r << 11 | g << 5 | bl);
}

#undef font
