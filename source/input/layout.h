// Big-key keyboard geometry for the 256x192 bottom screen. Plain C (no
// libnds) so the swipe decoder and tests/host use the same key positions.
//
//   y  0..19   status strip (owned by ui.c)
//   y 20..39   suggestion strip
//   y 40..191  four rows of 38 px keys
#ifndef INPUT_LAYOUT_H
#define INPUT_LAYOUT_H

#include <stdbool.h>

#define KBD_STRIP_Y 20
#define KBD_STRIP_H 20
#define KBD_ROWS_Y 40
#define KBD_ROW_H 38

typedef enum {
    KK_CHAR,     // types ch
    KK_SHIFT,
    KK_BKSP,
    KK_PAGE,     // letters <-> symbols
    KK_PEN,      // switch to handwriting
    KK_SPACE,
    KK_ENTER,
} KeyKind;

typedef struct {
    short x, y, w, h;
    KeyKind kind;
    char ch;
} KbdKey;

enum { PAGE_LETTERS, PAGE_SYMBOLS };

#define KBD_MAX_KEYS 40

// Fills out[] with the keys of a page. Returns the number of keys.
int layout_keys(int page, KbdKey *out, int max);
// The key at (x, y) on a page, or -1.
int layout_hit(const KbdKey *keys, int n, int x, int y);
// Center of a lowercase letter's key on the letters page.
bool layout_letter_center(char c, int *x, int *y);

#endif
