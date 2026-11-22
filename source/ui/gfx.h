// 8bpp bitmap drawing for both screens. Each screen draws into a RAM back
// buffer (VRAM rejects 8-bit writes) and gfx_blit() copies it with DMA.
#ifndef UI_GFX_H
#define UI_GFX_H

#include <stdbool.h>
#include <stdint.h>

#define SCREEN_W 256
#define SCREEN_H 192
#define FONT_W 8

// Sections of the planner. Each has a tab color in three shades.
enum {
    SEC_PLAIN, SEC_TODO, SEC_NOTES, SEC_CAL, SEC_HABITS, SEC_TIMERS, SEC_GAMES,
    SEC_SETTINGS, SEC_ALERT,
    SEC_COUNT
};

// Palette indices start at 64. Nothing on the card depends on their order:
// a sketch file holds ink numbers, not these.
enum {
    C_PAPER = 64, C_RULE, C_MARGIN, C_INK, C_INK_SOFT,
    C_LEATHER, C_LEATHER_HI, C_GOLD, C_BODY, C_BODY_HI,
    C_KEY, C_ACCENT,
    C_ALERT, C_SEL, C_OK, C_WHITE, C_BLACK, C_LABEL, C_DIM,
    C_PAPER_SH,              // paper in shadow, page edges
    C_SHADOW,                // drop shadow on paper
    C_STICKY, C_STICKY_SH,   // sticky note and its shaded edge
    C_ORANGE, C_TEAL, C_PURPLE,
    C_RING, C_RING_SH,       // binder rings
    C_LEATHER_SH,            // leather in shadow
    // C_WHITE is the card color (text boxes, check boxes, date blocks): the
    // opposite of C_INK, so dark in the dark theme. C_SNOW stays white in
    // every theme, for text on colored chips and on the leather.
    C_SNOW,
    C_BROWN,                 // work shift ink and dots
    C_WEEKEND,               // Saturday and Sunday in month grids
    C_SEC_BASE,              // SEC_COUNT * 3 entries: light, tab, dark
    C_COUNT_END = C_SEC_BASE + SEC_COUNT * 3
};

// Color themes: the same palette entries with other colors, so a switch
// recolors both screens at once without redrawing.
enum { THEME_CLASSIC, THEME_DARK, THEME_CANDY, THEME_COUNT };
void gfx_set_theme(int theme);
int gfx_theme(void);
// "Classic", "Dark", "Candy"
const char *gfx_theme_name(int theme);
// The CONFIG.INI word: "classic", "dark", "candy" (the older "dsi" is still
// read as candy). gfx_theme_from_key() returns THEME_CLASSIC for anything it
// does not know.
const char *gfx_theme_key(int theme);
int gfx_theme_from_key(const char *key);

#define SEC_LT(sec) ((uint8_t)(C_SEC_BASE + (sec) * 3))
#define SEC_MID(sec) ((uint8_t)(C_SEC_BASE + (sec) * 3 + 1))
#define SEC_DK(sec) ((uint8_t)(C_SEC_BASE + (sec) * 3 + 2))

typedef struct {
    uint8_t *buf;        // 256x192 back buffer
    uint16_t *vram;      // 256x256 8bpp bitmap BG
    bool dirty;
} Surface;

extern Surface g_top, g_bot;

void gfx_init(void);
// Copies a 256x192 buffer (the surface's own, or a composed one) to VRAM.
void gfx_blit(Surface *s, const uint8_t *src);

void gfx_clear(Surface *s, uint8_t c);
void gfx_fill(Surface *s, int x, int y, int w, int h, uint8_t c);
void gfx_hline(Surface *s, int x, int y, int w, uint8_t c);
void gfx_vline(Surface *s, int x, int y, int h, uint8_t c);
void gfx_frame(Surface *s, int x, int y, int w, int h, uint8_t c);
void gfx_dotted_hline(Surface *s, int x, int y, int w, uint8_t c);
// Line of `thick` x `thick` pixel dots (ink, swipe trails).
void gfx_line(Surface *s, int x0, int y0, int x1, int y1, uint8_t c, int thick);
// Filled rectangle with rounded corners, radius 0..4.
void gfx_round_fill(Surface *s, int x, int y, int w, int h, int r, uint8_t c);
// Filled circle of radius r around (cx, cy).
void gfx_disc(Surface *s, int cx, int cy, int r, uint8_t c);
// Rounded panel: a 1 px edge around a face.
void gfx_panel(Surface *s, int x, int y, int w, int h, int r, uint8_t face, uint8_t edge);
// Every other pixel in c: dims what is behind a dialog.
void gfx_dither(Surface *s, int x, int y, int w, int h, uint8_t c);

// Text in the 8x8 libnds font. Returns the x after the last character.
int gfx_text(Surface *s, int x, int y, const char *str, uint8_t c);
// At most max_chars characters; adds '~' when cut.
int gfx_text_n(Surface *s, int x, int y, const char *str, int max_chars, uint8_t c);
void gfx_text_center(Surface *s, int cx, int y, const char *str, uint8_t c);
// Text scaled 2x (16x16 cells).
int gfx_text2x(Surface *s, int x, int y, const char *str, uint8_t c);
// Text scaled by an integer factor (scale x 8 px cells).
int gfx_text_scaled(Surface *s, int x, int y, const char *str, uint8_t c, int scale);

// Sticker button: rounded, with a darker lip below that it sinks into when
// pressed. icon is an ICON_* (0 = none); the label may be NULL and '\n'
// splits it in two lines. Buttons 50 px or taller draw the icon at 2x above
// the label.
void gfx_button(Surface *s, int x, int y, int w, int h, const char *label, int icon, int sec,
                bool pressed, bool enabled);
// Plain sticker button (keyboard keys and the like).
void gfx_key(Surface *s, int x, int y, int w, int h, const char *label, bool pressed,
             bool enabled);
// Rounded pill with text, 11 px tall. Returns the x after it.
int gfx_chip(Surface *s, int x, int y, const char *text, uint8_t face, uint8_t ink);
// A console button's letter in a small disc, 11x11 px: A, B, X, Y.
void gfx_pad_badge(Surface *s, int x, int y, char letter);
// Hand-stamped check mark around (cx, cy); size 7 is at rest, larger while
// the stamp comes down.
void gfx_stamp_check(Surface *s, int cx, int cy, int size, uint8_t c);
// Check box for a list row, 14x14 px.
void gfx_check_box(Surface *s, int x, int y);

// Planner page: cream paper, ruled lines from y0, red margin.
void gfx_paper(Surface *s, int y0);
// Blank paper from y0 down, with the page's shaded edges.
void gfx_page(Surface *s, int y0);
// Binder rings into the hinge: along the bottom edge of the top screen, or
// the top edge of the bottom screen.
void gfx_rings(Surface *s, bool top_screen);

// Ruled paper as every screen draws it, gfx_paper(s, PAPER_Y): rule k is at
// PAPER_RULE(k). 8x8 text drawn at PAPER_TEXT(k) sits on rule k: letters
// end one pixel above it and the descenders of g and y just reach it.
// Row k's band (for highlights) is PAPER_Y + k * PAPER_ROW, 11 px tall.
#define PAPER_Y 18
#define PAPER_ROW 12
#define PAPER_ROWS 14
#define PAPER_RULE(k) (PAPER_Y + 11 + (k) * PAPER_ROW)
#define PAPER_TEXT(k) (PAPER_RULE(k) - 8)
// 2x text (16 px cells) whose letters end just above rule k.
#define PAPER_TEXT2X(k) (PAPER_RULE(k) - 15)
// Leather header band (18 px) with a title, right-aligned text and, once
// gfx_set_battery() has been called, a battery icon at the far right. With
// a section set, the title sits on that section's colored tab with its icon.
void gfx_header(Surface *s, const char *title, const char *right);
// The section whose tab gfx_header draws (icon 0 and SEC_PLAIN: no tab).
void gfx_set_section(int icon, int sec);
// bars 0..4. Returns true when the icon changed (the caller redraws).
bool gfx_set_battery(int bars, bool charging);

#endif
