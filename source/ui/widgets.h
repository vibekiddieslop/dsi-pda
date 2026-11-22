// Touch widgets for the bottom screen: buttons that fire on release, the
// action bar, and the scrolling list.
#ifndef UI_WIDGETS_H
#define UI_WIDGETS_H

#include "ui.h"

typedef struct {
    short x, y, w, h;
} Rect;

static inline bool rect_has(const Rect *r, int x, int y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}

// ---- a set of buttons. One is held while the stylus is on it; it fires
// when the stylus lifts there, so sliding off cancels.
typedef struct {
    int held;    // index + 1 of the button touched, 0 for none
    bool hot;    // the stylus is still on it
} Press;

// Returns the index of the button that fired this frame, or -1.
int press_update(Press *p, const Input *in, const Rect *rects, int n);
// True while button i shows as pressed.
static inline bool press_is(const Press *p, int i)
{
    return p->held == i + 1 && p->hot;
}

// ---- action bar: up to 5 buttons along the bottom edge, for the picked
// item.
typedef struct {
    const char *label;
    int icon;
    int sec;         // SEC_PLAIN, or SEC_ALERT for delete
    bool enabled;
} BarItem;

typedef struct {
    BarItem item[5];
    uint8_t narrow;  // bit i: button i is a narrow arrow key (32 px)
    int n;
    Press press;
} Bar;

void bar_draw(Surface *s, const Bar *b);
// Returns the index of the enabled button that fired, or -1.
int bar_update(Bar *b, const Input *in);

// ---- list: rows of UI_ROW_H px on a ruled page. Drag to scroll. A tap
// picks a row; a tap on the picked row opens it; a tap in the box zone at
// the left is the row's quick action. Up/Down move, L/R page, A is the
// quick action (or opens, without a box zone).
typedef struct {
    int count;       // rows; set before list_update and list_draw
    int sel;         // picked row
    int scroll;      // px scrolled
    int top, height; // area on the bottom screen
    int box_w;       // width of the quick-action zone, 0 for none
    // touch tracking
    bool tracking, dragging;
    int start_y, start_scroll, press_row;
} List;

typedef enum { LE_NONE, LE_SELECT, LE_BOX, LE_OPEN } ListEventKind;
typedef struct {
    ListEventKind kind;
    int index;
} ListEvent;

// Draws row `index` with its top at y: the page, rule and highlight are
// already there.
typedef void (*ListRow)(Surface *s, int index, int y, bool selected);

void list_init(List *l, int top, int height, int box_w);
ListEvent list_update(List *l, const Input *in);
void list_draw(List *l, Surface *s, ListRow row);
// Picks a row and scrolls it into view.
void list_show(List *l, int index);
// Keeps sel and scroll in range after count changed.
void list_fix(List *l);

// "A" badge and friends for the legend line on the top screen: draws
// [A] Check  [X] Add ... from pairs of (letter, text); a NULL text ends it.
void legend_draw(Surface *s, int y, const char *const *pairs);

#endif
