// Screen stack, shared header, input routing, page-turn transitions, the
// sticky-note messages and the pop-up menu and confirm dialogs.
#ifndef UI_UI_H
#define UI_UI_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "gfx.h"
#include "icons.h"

#define UI_STACK_MAX 6

// Bottom screen layout of the new-style screens.
#define UI_HEAD_H 24                       // header: back, title, menu, add
#define UI_BAR_Y 164                       // action bar
#define UI_BAR_H (SCREEN_H - UI_BAR_Y)
#define UI_ROW_H 20                        // list rows

typedef struct {
    uint32_t down;     // Keys pressed this frame
    uint32_t held;
    uint32_t repeat;   // Pressed or auto-repeating this frame
    bool touch_down;   // Stylus touched this frame
    bool touching;
    bool touch_up;     // Stylus lifted this frame; tx, ty hold where it was
    int tx, ty;
} Input;

typedef struct Screen {
    void (*enter)(void);                 // Pushed, or uncovered by a pop
    void (*leave)(void);                 // Popped
    void (*update)(const Input *in);     // Buttons; called every frame
    void (*draw_top)(Surface *s);
    void (*draw_bottom)(Surface *s);
    const char *(*hint)(void);           // Status line of a screen without a title
    bool (*ticking)(void);               // Optional: true = redraw top every second
    bool keep_b;                        // B is not "back" (editor uses it)
    bool back_touch;                     // With keep_b: the <Back key still works

    // Screens set a title: the bottom screen then gets the shared header
    // (back, section tab, menu, add) and the same buttons everywhere: B back,
    // X add, Y menu. Only the text editor does without, for its keyboard.
    const char *title;
    const char *(*title_fn)(void);       // Optional: a title that changes
    int icon;                            // ICON_* on the section tabs
    int sec;                             // SEC_* tab color
    void (*on_add)(void);                // "+" in the header, or X
    void (*on_more)(void);               // "..." in the header, or Y
    int more_icon;                       // 0 = ICON_MORE
    // Set: the header's right corner holds a trash key for the picked item
    // instead of "+". X still calls on_add; the screen puts Add on its bar.
    void (*on_delete)(void);
    bool (*can_delete)(void);            // Optional: false greys the trash out
    bool (*on_back)(void);               // Optional: true = B or Back was used up
} Screen;

// Status strip "< Back" touch area (x < UI_BACK_W, y < 20).
#define UI_BACK_W 52

void ui_init(const Screen *first);
// One frame: input, update, redraw when dirty. Call once per VBlank.
void ui_frame(void);

void ui_push(const Screen *s);
void ui_pop(void);
const Screen *ui_current(void);
// True while a page's enter() runs because it was just opened (ui_push),
// false when it is uncovered by Back (ui_pop).
bool ui_pushed(void);
int ui_depth(void);

// Request a redraw of both screens.
void ui_dirty(void);
// Only one screen changed (cursor blink, ink, swipe trail): skips drawing
// and copying the other one.
void ui_dirty_top(void);
void ui_dirty_bottom(void);
// Shows a sticky note at the foot of the top screen for ~3 seconds: two
// lines of 28 characters.
void ui_toast(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
// Shows "label [=====     ]" on the note for ~1.5 seconds (volume,
// brightness). value 0..max.
void ui_meter(const char *label, int value, int max);

// True while the last input came from the buttons: screens then show their
// cursor and the A/X/Y badges.
bool ui_pad_mode(void);

// Pop-up menu on the bottom screen: up to 6 rows with an icon. done gets
// the row tapped; closing it (B, or a tap outside) calls nothing.
typedef struct {
    const char *label;   // must stay valid while the menu is open
    int icon;
    int sec;             // SEC_PLAIN, or a color (SEC_ALERT for delete)
} SheetItem;
void ui_sheet(const char *title, const SheetItem *items, int n, void (*done)(int index));
// After a delete: an Undo key on the bottom screen for 5 seconds (tap it,
// or SELECT); undo() puts the thing back. Opening or leaving a page, or a
// newer offer, drops it.
void ui_undo_offer(void (*undo)(void));
// Asks before something is destroyed: [Keep] [yes]. detail may be NULL.
void ui_confirm(const char *question, const char *detail, const char *yes,
                void (*on_yes)(void));

// Overlay that takes over both screens (used by ringing alarms). It is drawn
// when it appears and when the minute changes; update() calls ui_dirty()
// for any other change to its picture.
typedef struct {
    bool (*active)(void);
    void (*update)(const Input *in);
    void (*draw)(Surface *top, Surface *bot);
} Overlay;
void ui_set_overlay(const Overlay *o);

// The time now, read once per frame.
time_t ui_now(void);

#endif
