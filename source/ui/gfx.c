#include "gfx.h"

#include <string.h>
#include <strings.h>

#include <nds.h>

#include "icons.h"

// 1bpp 8x8 font from libnds (ASCII 32..127, bit 0 = leftmost pixel).
extern const uint32_t default_fontTiles[];

static uint8_t top_buf[SCREEN_W * SCREEN_H] __attribute__((aligned(32)));
static uint8_t bot_buf[SCREEN_W * SCREEN_H] __attribute__((aligned(32)));

Surface g_top = { top_buf, NULL, true };
Surface g_bot = { bot_buf, NULL, true };

#define PAL8(r, g, b) RGB15((r) >> 3, (g) >> 3, (b) >> 3)
#define HEX(v) PAL8(((v) >> 16) & 0xFF, ((v) >> 8) & 0xFF, (v) & 0xFF)
#define SEC3(sec, lt, mid, dk)                          \
    [C_SEC_BASE - C_PAPER + (sec) * 3]     = HEX(lt),   \
    [C_SEC_BASE - C_PAPER + (sec) * 3 + 1] = HEX(mid),  \
    [C_SEC_BASE - C_PAPER + (sec) * 3 + 2] = HEX(dk)

static const uint16_t palette[C_COUNT_END - C_PAPER] = {
    [C_PAPER - C_PAPER]      = HEX(0xF4ECD0),
    [C_RULE - C_PAPER]       = HEX(0xB8C8E0),
    [C_MARGIN - C_PAPER]     = HEX(0xD88080),
    [C_INK - C_PAPER]        = HEX(0x282838),
    [C_INK_SOFT - C_PAPER]   = HEX(0x707078),
    [C_LEATHER - C_PAPER]    = HEX(0x5A3A22),
    [C_LEATHER_HI - C_PAPER] = HEX(0x805838),
    [C_GOLD - C_PAPER]       = HEX(0xE8C870),
    [C_BODY - C_PAPER]       = HEX(0x3E2C1E),
    [C_BODY_HI - C_PAPER]    = HEX(0x5A4230),
    [C_KEY - C_PAPER]        = HEX(0xD8D4C8),
    [C_ACCENT - C_PAPER]     = HEX(0x3060A0),
    [C_ALERT - C_PAPER]      = HEX(0xC03030),
    [C_SEL - C_PAPER]        = HEX(0xF0D888),
    [C_OK - C_PAPER]         = HEX(0x388840),
    [C_WHITE - C_PAPER]      = HEX(0xFFFFFF),
    [C_BLACK - C_PAPER]      = HEX(0x101010),
    [C_LABEL - C_PAPER]      = HEX(0xE8DCC8),
    [C_DIM - C_PAPER]        = HEX(0x98948C),
    [C_PAPER_SH - C_PAPER]   = HEX(0xE4DAB8),
    [C_SHADOW - C_PAPER]     = HEX(0xC4B890),
    [C_STICKY - C_PAPER]     = HEX(0xFFF09A),
    [C_STICKY_SH - C_PAPER]  = HEX(0xDCC458),
    [C_ORANGE - C_PAPER]     = HEX(0xF29A3E),
    [C_TEAL - C_PAPER]       = HEX(0x3DBFAE),
    [C_PURPLE - C_PAPER]     = HEX(0x9A86D0),
    [C_RING - C_PAPER]       = HEX(0xD8DCE4),
    [C_RING_SH - C_PAPER]    = HEX(0x80848C),
    [C_LEATHER_SH - C_PAPER] = HEX(0x3C2614),
    [C_SNOW - C_PAPER]       = HEX(0xFFFFFF),
    [C_BROWN - C_PAPER]      = HEX(0x5A3A22),
    [C_WEEKEND - C_PAPER]    = HEX(0xA02C22),
    SEC3(SEC_PLAIN,    0xF2EEE0, 0xC4BEAC, 0x6E6858),
    SEC3(SEC_TODO,     0xCDEBC0, 0x6CBF5A, 0x2F7A32),
    SEC3(SEC_NOTES,    0xFBEFA8, 0xF2C744, 0xA87A12),
    SEC3(SEC_CAL,      0xF9CFC8, 0xE8604F, 0xA02C22),
    SEC3(SEC_HABITS,   0xFBD9B0, 0xF29A3E, 0xAD5B10),
    SEC3(SEC_TIMERS,   0xC9DDF6, 0x5B97E0, 0x255C9E),
    SEC3(SEC_GAMES,    0xBFEDE6, 0x3DBFAE, 0x147A6E),
    SEC3(SEC_SETTINGS, 0xDDD6F0, 0x9A86D0, 0x574590),
    SEC3(SEC_ALERT,    0xF9C4BC, 0xE0483C, 0x8C1C14),
};

// ---------------------------------------------------------------- themes

// A theme is the classic palette with some entries replaced.
typedef struct {
    uint8_t index;
    uint32_t rgb;
} PalSet;

#define SET3(sec, lt, mid, dk)                                                  \
    { C_SEC_BASE + (sec) * 3, lt }, { C_SEC_BASE + (sec) * 3 + 1, mid },     \
    { C_SEC_BASE + (sec) * 3 + 2, dk }

// Dark: slate paper, light ink; section colors keep their middle tones (the
// chips carry both ink and white text) and their light faces go dark.
static const PalSet DARK[] = {
    { C_PAPER, 0x24272D },     { C_RULE, 0x363C48 },        { C_MARGIN, 0x8E4652 },
    { C_INK, 0xE6E2D8 },       { C_INK_SOFT, 0x9C9E9E },    { C_LEATHER, 0x15171B },
    { C_LEATHER_HI, 0x2E323A },{ C_BODY, 0x0E0F12 },        { C_BODY_HI, 0x24272D },
    { C_KEY, 0x3A3E46 },       { C_ACCENT, 0x78AEEA },      { C_ALERT, 0xF26A5E },
    { C_SEL, 0x4C4428 },       { C_OK, 0x62C46A },          { C_WHITE, 0x33373F },
    { C_BLACK, 0x050506 },     { C_LABEL, 0xCFCAC0 },       { C_DIM, 0x686A6E },
    { C_PAPER_SH, 0x1C1E23 },  { C_SHADOW, 0x141519 },      { C_STICKY, 0x5E5224 },
    { C_STICKY_SH, 0x3C3414 }, { C_RING, 0x8C9098 },        { C_RING_SH, 0x4C5058 },
    { C_LEATHER_SH, 0x0B0C0F },{ C_BROWN, 0xD29A62 },       { C_WEEKEND, 0xEE8478 },
    SET3(SEC_PLAIN,    0x3A3D44, 0x5C6068, 0x15171B),
    SET3(SEC_TODO,     0x2C4A2C, 0x4E9A46, 0x12301A),
    SET3(SEC_NOTES,    0x4C4220, 0xB8922C, 0x2C2208),
    SET3(SEC_CAL,      0x50302C, 0xC64C3E, 0x2E1210),
    SET3(SEC_HABITS,   0x503A22, 0xC4762C, 0x2E1A08),
    SET3(SEC_TIMERS,   0x263C58, 0x4280CA, 0x10223A),
    SET3(SEC_GAMES,    0x1E4844, 0x2E9E90, 0x0C2E2A),
    SET3(SEC_SETTINGS, 0x3A3254, 0x7C68BA, 0x1E1636),
    SET3(SEC_ALERT,    0x5A2A26, 0xCC3E34, 0x341010),
};

// Candy: made from a clear ocean-blue DSi shell with raspberry-pink buttons
// and bezels.
// Blue leather, pink plain buttons and highlights, a cool white page.
static const PalSet CANDY[] = {
    { C_PAPER, 0xF1F5F3 },     { C_RULE, 0xB2D2E4 },        { C_MARGIN, 0xE2638C },
    { C_LEATHER, 0x1371A8 },   { C_LEATHER_HI, 0x3C9DCF },  { C_LEATHER_SH, 0x0A4C74 },
    { C_GOLD, 0xFFD2E0 },      { C_BODY, 0x0D5A88 },        { C_BODY_HI, 0x1F7DB2 },
    { C_ACCENT, 0x1A7DB8 },    { C_SEL, 0xF9CBDA },         { C_LABEL, 0xDCEEF8 },
    { C_PAPER_SH, 0xDCE8EA },  { C_SHADOW, 0xB6CCD4 },      { C_STICKY, 0xFFD8E6 },
    { C_STICKY_SH, 0xEAA2BC }, { C_WEEKEND, 0xC0285A },
    SET3(SEC_PLAIN,    0xFAD6E2, 0xE0527E, 0x9E2450),
};

static const struct {
    const char *key, *name;
    const PalSet *set;
    int n;
} THEMES[THEME_COUNT] = {
    [THEME_CLASSIC] = { "classic", "Classic", NULL, 0 },
    [THEME_DARK] = { "dark", "Dark", DARK, (int)(sizeof(DARK) / sizeof(DARK[0])) },
    [THEME_CANDY] = { "candy", "Candy", CANDY, (int)(sizeof(CANDY) / sizeof(CANDY[0])) },
};

static int theme = THEME_CLASSIC;

void gfx_set_theme(int t)
{
    if (t < 0 || t >= THEME_COUNT)
        t = THEME_CLASSIC;
    theme = t;
    uint16_t pal[C_COUNT_END - C_PAPER];
    memcpy(pal, palette, sizeof(pal));
    for (int i = 0; i < THEMES[t].n; i++)
    {
        uint32_t v = THEMES[t].set[i].rgb;
        pal[THEMES[t].set[i].index - C_PAPER] = HEX(v);
    }
    // Wait for the blank so a switch never shows half old, half new colors.
    swiWaitForVBlank();
    for (int i = 0; i < C_COUNT_END - C_PAPER; i++)
    {
        BG_PALETTE[C_PAPER + i] = pal[i];
        BG_PALETTE_SUB[C_PAPER + i] = pal[i];
    }
    BG_PALETTE[0] = pal[C_BODY - C_PAPER];
}

int gfx_theme(void)
{
    return theme;
}

const char *gfx_theme_name(int t)
{
    return t >= 0 && t < THEME_COUNT ? THEMES[t].name : THEMES[0].name;
}

const char *gfx_theme_key(int t)
{
    return t >= 0 && t < THEME_COUNT ? THEMES[t].key : THEMES[0].key;
}

int gfx_theme_from_key(const char *key)
{
    for (int t = 0; t < THEME_COUNT; t++)
        if (strcasecmp(key, THEMES[t].key) == 0)
            return t;
    if (strcasecmp(key, "dsi") == 0)
        return THEME_CANDY;   // its name before it was Candy
    return THEME_CLASSIC;
}

void gfx_init(void)
{
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    // Nothing here is 3D: its two power lines stay off to spare the battery.
    powerOff(POWER_3D_CORE | POWER_MATRIX);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);

    int bg_top = bgInit(3, BgType_Bmp8, BgSize_B8_256x256, 0, 0);
    // Sub BG3 bitmap at 64 KiB. The keyboard and pad draw into this bitmap.
    int bg_bot = bgInitSub(3, BgType_Bmp8, BgSize_B8_256x256, 4, 0);
    bgSetPriority(bg_top, 3);
    bgSetPriority(bg_bot, 3);
    g_top.vram = bgGetGfxPtr(bg_top);
    g_bot.vram = bgGetGfxPtr(bg_bot);

    gfx_set_theme(THEME_CLASSIC);   // device_init() applies the saved theme
    g_top.dirty = g_bot.dirty = true;
}

void gfx_blit(Surface *s, const uint8_t *src)
{
    // Words, not halfwords (dmaCopy): half the bus transfers per frame. Both
    // buffers are 32-byte aligned and the size is a multiple of 4.
    DC_FlushRange(src, SCREEN_W * SCREEN_H);
    dmaCopyWords(3, src, s->vram, SCREEN_W * SCREEN_H);
}

void gfx_clear(Surface *s, uint8_t c)
{
    memset(s->buf, c, SCREEN_W * SCREEN_H);
    s->dirty = true;
}

void gfx_fill(Surface *s, int x, int y, int w, int h, uint8_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0)
        return;
    for (int j = 0; j < h; j++)
        memset(s->buf + (y + j) * SCREEN_W + x, c, (size_t)w);
    s->dirty = true;
}

void gfx_hline(Surface *s, int x, int y, int w, uint8_t c)
{
    gfx_fill(s, x, y, w, 1, c);
}

void gfx_vline(Surface *s, int x, int y, int h, uint8_t c)
{
    gfx_fill(s, x, y, 1, h, c);
}

void gfx_frame(Surface *s, int x, int y, int w, int h, uint8_t c)
{
    gfx_hline(s, x, y, w, c);
    gfx_hline(s, x, y + h - 1, w, c);
    gfx_vline(s, x, y, h, c);
    gfx_vline(s, x + w - 1, y, h, c);
}

void gfx_dotted_hline(Surface *s, int x, int y, int w, uint8_t c)
{
    for (int i = 0; i < w; i += 3)
        gfx_fill(s, x + i, y, 2, 1, c);
}

void gfx_line(Surface *s, int x0, int y0, int x1, int y1, uint8_t c, int thick)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, h = thick / 2;
    for (;;)
    {
        gfx_fill(s, x0 - h, y0 - h, thick, thick, c);
        if (x0 == x1 && y0 == y1)
            break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_round_fill(Surface *s, int x, int y, int w, int h, int r, uint8_t c)
{
    // Pixels cut from each end of the first rows (mirrored for the last).
    static const uint8_t CUT[5][4] = {
        { 0, 0, 0, 0 }, { 1, 0, 0, 0 }, { 2, 1, 0, 0 }, { 3, 1, 1, 0 }, { 4, 2, 1, 1 },
    };
    if (r < 0)
        r = 0;
    if (r > 4)
        r = 4;
    while (r > 0 && (2 * r > h || 2 * r > w))
        r--;
    for (int j = 0; j < r; j++)
    {
        int cut = CUT[r][j];
        gfx_fill(s, x + cut, y + j, w - 2 * cut, 1, c);
        gfx_fill(s, x + cut, y + h - 1 - j, w - 2 * cut, 1, c);
    }
    gfx_fill(s, x, y + r, w, h - 2 * r, c);
}

void gfx_disc(Surface *s, int cx, int cy, int r, uint8_t c)
{
    for (int dy = -r; dy <= r; dy++)
    {
        int w = 0;
        while ((w + 1) * (w + 1) + dy * dy <= r * r + r)
            w++;
        gfx_fill(s, cx - w, cy + dy, 2 * w + 1, 1, c);
    }
}

void gfx_panel(Surface *s, int x, int y, int w, int h, int r, uint8_t face, uint8_t edge)
{
    gfx_round_fill(s, x, y, w, h, r, edge);
    gfx_round_fill(s, x + 1, y + 1, w - 2, h - 2, r > 0 ? r - 1 : 0, face);
}

void gfx_dither(Surface *s, int x, int y, int w, int h, uint8_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    for (int j = 0; j < h; j++)
    {
        uint8_t *row = s->buf + (y + j) * SCREEN_W;
        for (int i = (x + y + j) & 1; i < w; i += 2)
            row[x + i] = c;
    }
    s->dirty = true;
}

static void draw_char(Surface *s, int x, int y, char ch, uint8_t c, int scale)
{
    unsigned idx = (unsigned char)ch;
    if (idx < 32 || idx > 127)
        idx = '?';
    const uint8_t *glyph = (const uint8_t *)default_fontTiles + (idx - 32) * 8;
    for (int row = 0; row < 8; row++)
    {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++)
        {
            if (!(bits & (1 << col)))
                continue;
            int px = x + col * scale, py = y + row * scale;
            if (scale == 1)
            {
                if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H)
                    s->buf[py * SCREEN_W + px] = c;
            }
            else
            {
                gfx_fill(s, px, py, scale, scale, c);
            }
        }
    }
    s->dirty = true;
}

int gfx_text_scaled(Surface *s, int x, int y, const char *str, uint8_t c, int scale)
{
    for (; *str; str++, x += FONT_W * scale)
        draw_char(s, x, y, *str, c, scale);
    return x;
}

int gfx_text(Surface *s, int x, int y, const char *str, uint8_t c)
{
    return gfx_text_scaled(s, x, y, str, c, 1);
}

int gfx_text2x(Surface *s, int x, int y, const char *str, uint8_t c)
{
    return gfx_text_scaled(s, x, y, str, c, 2);
}

int gfx_text_n(Surface *s, int x, int y, const char *str, int max_chars, uint8_t c)
{
    int len = (int)strlen(str);
    if (len <= max_chars)
        return gfx_text(s, x, y, str, c);
    for (int i = 0; i < max_chars - 1; i++, x += FONT_W)
        draw_char(s, x, y, str[i], c, 1);
    draw_char(s, x, y, '~', c, 1);
    return x + FONT_W;
}

void gfx_text_center(Surface *s, int cx, int y, const char *str, uint8_t c)
{
    gfx_text(s, cx - (int)strlen(str) * FONT_W / 2, y, str, c);
}

// ---------------------------------------------------------------- buttons

// Up to two lines of a label, centered on cx; y is the top of the first.
static void label_lines(Surface *s, int cx, int y, const char *label, uint8_t ink)
{
    const char *nl = strchr(label, '\n');
    if (nl == NULL)
    {
        gfx_text_center(s, cx, y, label, ink);
        return;
    }
    char first[24];
    int n = (int)(nl - label);
    if (n > (int)sizeof(first) - 1)
        n = sizeof(first) - 1;
    memcpy(first, label, (size_t)n);
    first[n] = '\0';
    gfx_text_center(s, cx, y, first, ink);
    gfx_text_center(s, cx, y + 10, nl + 1, ink);
}

void gfx_button(Surface *s, int x, int y, int w, int h, const char *label, int icon, int sec,
                bool pressed, bool enabled)
{
    int r = (w < 16 || h < 16) ? 2 : 3;
    int lip = h >= 50 ? 3 : 2;           // the darker side below the cap
    int sink = pressed ? lip - 1 : 0;    // pressed: the cap sits on its lip
    int top = y + sink, hh = h - sink;
    gfx_round_fill(s, x, top, w, hh, r, SEC_DK(sec));
    gfx_round_fill(s, x + 1, top + 1, w - 2, hh - 2, r - 1, SEC_MID(sec));
    int fh = hh - 2 - (lip - sink);      // face height
    gfx_round_fill(s, x + 1, top + 1, w - 2, fh, r - 1, SEC_LT(sec));
    if (h >= 16 && w >= 12)
        gfx_hline(s, x + 3, top + 2, w - 6, C_WHITE);

    uint8_t ink = enabled ? C_INK : C_DIM;
    int cx = x + w / 2;
    int lines = label == NULL ? 0 : (strchr(label, '\n') ? 2 : 1);
    int text_h = lines == 2 ? 18 : 8;
    if (icon != ICON_NONE && h >= 50)
    {
        // Tile: big icon above the label.
        int content = 32 + (lines ? 5 + text_h : 0);
        int cy = top + 1 + (fh - content) / 2;
        if (enabled)
            gfx_icon(s, cx - 16, cy, icon, 2);
        else
            gfx_icon_tint(s, cx - 16, cy, icon, 2, C_DIM);
        if (lines)
            label_lines(s, cx, cy + 37, label, ink);
        return;
    }
    int cy = top + 1 + fh / 2;           // middle of the face
    if (icon != ICON_NONE)
    {
        int len = lines == 1 ? (int)strlen(label) : 0;
        int group = 16 + (len ? 3 + len * FONT_W : 0);
        if (group > w - 4 && len && len * FONT_W <= w - 4)
        {
            gfx_text_center(s, cx, cy - 4, label, ink);   // no room for both: the word
            return;
        }
        if (group > w - 4)
        {
            group = 16;
            len = 0;
        }
        int ix = cx - group / 2;
        if (enabled)
            gfx_icon(s, ix, cy - 8, icon, 1);
        else
            gfx_icon_tint(s, ix, cy - 8, icon, 1, C_DIM);
        if (len)
            gfx_text(s, ix + 19, cy - 4, label, ink);
        return;
    }
    if (lines)
        label_lines(s, cx, cy - text_h / 2, label, ink);
}

void gfx_key(Surface *s, int x, int y, int w, int h, const char *label, bool pressed,
             bool enabled)
{
    gfx_button(s, x, y, w, h, label, ICON_NONE, SEC_PLAIN, pressed, enabled);
}

int gfx_chip(Surface *s, int x, int y, const char *text, uint8_t face, uint8_t ink)
{
    int w = (int)strlen(text) * FONT_W + 7;
    gfx_round_fill(s, x, y, w, 11, 3, face);
    gfx_text(s, x + 4, y + 2, text, ink);
    return x + w;
}

void gfx_pad_badge(Surface *s, int x, int y, char letter)
{
    char t[2] = { letter, 0 };
    gfx_round_fill(s, x, y, 11, 11, 4, C_INK);
    gfx_text(s, x + 2, y + 2, t, C_WHITE);
}

void gfx_stamp_check(Surface *s, int cx, int cy, int size, uint8_t c)
{
    int thick = size > 9 ? 3 : 2;
    int mx = cx - size / 3, my = cy + (size * 2) / 3;
    gfx_line(s, cx - size, cy, mx, my, c, thick);
    gfx_line(s, mx, my, cx + size, cy - size, c, thick);
}

void gfx_check_box(Surface *s, int x, int y)
{
    gfx_panel(s, x, y, 14, 14, 2, C_WHITE, C_INK_SOFT);
    gfx_hline(s, x + 2, y + 1, 10, C_PAPER_SH);   // a little depth
}

// ---------------------------------------------------------------- paper

void gfx_paper(Surface *s, int y0)
{
    gfx_fill(s, 0, y0, SCREEN_W, SCREEN_H - y0, C_PAPER);
    for (int y = y0 + 11; y < SCREEN_H - 6; y += 12)
        gfx_hline(s, 0, y, SCREEN_W, C_RULE);
    gfx_vline(s, 16, y0, SCREEN_H - y0, C_MARGIN);
    if (s == &g_top)
        gfx_rings(s, true);
}

void gfx_page(Surface *s, int y0)
{
    gfx_fill(s, 0, y0, SCREEN_W, SCREEN_H - y0, C_PAPER);
    gfx_vline(s, 0, y0, SCREEN_H - y0, C_PAPER_SH);
    gfx_vline(s, SCREEN_W - 1, y0, SCREEN_H - y0, C_PAPER_SH);
}

void gfx_rings(Surface *s, bool top_screen)
{
    // A spiral binding through the hinge: holes in the page above, and the
    // wire running off the edge of both screens.
    for (int cx = 8; cx < SCREEN_W; cx += 16)
    {
        if (top_screen)
        {
            gfx_round_fill(s, cx - 3, SCREEN_H - 6, 7, 4, 1, C_INK_SOFT);
            gfx_fill(s, cx - 1, SCREEN_H - 4, 3, 4, C_RING);
            gfx_vline(s, cx + 1, SCREEN_H - 4, 4, C_RING_SH);
        }
        else
        {
            gfx_fill(s, cx - 1, 0, 3, 3, C_RING);
            gfx_vline(s, cx + 1, 0, 3, C_RING_SH);
        }
    }
}

// ---------------------------------------------------------------- header

static int battery_bars = -1;     // -1: not known yet, no icon
static bool battery_charging = false;
static int section_icon = ICON_NONE, section = SEC_PLAIN;

bool gfx_set_battery(int bars, bool charging)
{
    if (bars == battery_bars && charging == battery_charging)
        return false;
    battery_bars = bars;
    battery_charging = charging;
    return true;
}

void gfx_set_section(int icon, int sec)
{
    section_icon = icon;
    section = sec;
}

#define BATTERY_W 20              // Icon plus the gap before the right text.

// 16x8 outline with a nub; four 2 px bars inside.
static void draw_battery(Surface *s, int x, int y)
{
    uint8_t bar = battery_charging ? C_OK : (battery_bars <= 1 ? C_ALERT : C_GOLD);
    gfx_frame(s, x, y, 15, 8, battery_bars == 0 && !battery_charging ? C_ALERT : C_GOLD);
    gfx_fill(s, x + 15, y + 2, 2, 4, C_GOLD);
    int n = battery_bars == 0 ? 1 : battery_bars;  // An empty battery shows one red bar.
    for (int i = 0; i < n; i++)
        gfx_fill(s, x + 2 + i * 3, y + 2, 2, 4, bar);
}

void gfx_header(Surface *s, const char *title, const char *right)
{
    gfx_fill(s, 0, 0, SCREEN_W, 18, C_LEATHER);
    gfx_hline(s, 0, 17, SCREEN_W, C_LEATHER_SH);
    gfx_dotted_hline(s, 2, 1, SCREEN_W - 4, C_LEATHER_HI);
    gfx_dotted_hline(s, 2, 15, SCREEN_W - 4, C_LEATHER_HI);
    if (section != SEC_PLAIN)
    {
        // An index tab in the section's color, standing on the page.
        int w = (int)strlen(title) * FONT_W + (section_icon ? 30 : 12);
        gfx_round_fill(s, 3, 1, w, 20, 3, SEC_DK(section));
        gfx_round_fill(s, 4, 2, w - 2, 19, 2, SEC_MID(section));
        int tx = 9;
        if (section_icon)
        {
            gfx_icon(s, 6, 1, section_icon, 1);
            tx = 26;
        }
        gfx_text(s, tx, 5, title, C_INK);
    }
    else
    {
        gfx_text(s, 6, 5, title, C_GOLD);
    }
    int right_end = SCREEN_W - 6;
    if (battery_bars >= 0)
    {
        draw_battery(s, SCREEN_W - 4 - 17, 5);
        right_end -= BATTERY_W;
    }
    if (right != NULL)
        gfx_text(s, right_end - (int)strlen(right) * FONT_W, 5, right, C_GOLD);
}
