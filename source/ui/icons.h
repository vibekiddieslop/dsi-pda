// 16x16 pixel icons, kept as ASCII art in icons.c. tools/icon_preview.py
// renders them to a PNG for a quick look without building the ROM.
#ifndef UI_ICONS_H
#define UI_ICONS_H

#include "gfx.h"

enum {
    ICON_NONE = 0,
    ICON_TODO, ICON_NOTES, ICON_CAL, ICON_CAL_BLANK, ICON_HABITS, ICON_TIMERS, ICON_GAMES,
    ICON_GEAR, ICON_PLUS, ICON_BACK, ICON_MORE, ICON_EDIT, ICON_TRASH, ICON_REPEAT,
    ICON_UP, ICON_CHECK, ICON_BELL, ICON_CAKE, ICON_SORT, ICON_REORDER, ICON_SUN,
    ICON_MOON, ICON_PLAY, ICON_SKETCH, ICON_WORK, ICON_HEART, ICON_LIST, ICON_SOUND,
    ICON_PAUSE, ICON_PAGE, ICON_UNDO, ICON_ERASER, ICON_BILL,
    ICON_COUNT,

    // Mirrored drawing: OR one of these into an icon id.
    ICON_FLIP_H = 0x100,
    ICON_FLIP_V = 0x200,
    ICON_NEXT = ICON_BACK | ICON_FLIP_H,
    ICON_DOWN = ICON_UP | ICON_FLIP_V,
};

// scale 1 = 16x16 px, 2 = 32x32 px.
void gfx_icon(Surface *s, int x, int y, int icon, int scale);
// The icon's shape in one color (disabled buttons).
void gfx_icon_tint(Surface *s, int x, int y, int icon, int scale, uint8_t c);

#endif
